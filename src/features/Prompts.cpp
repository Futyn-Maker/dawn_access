#include "features/Prompts.hpp"

#include "features/Hud.hpp"
#include "features/Interactions.hpp"

#include "features/Exploration.hpp"
#include "features/PromptRules.hpp"

#include "core/Config.hpp"
#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/ParamReader.hpp"
#include "core/Strings.hpp"
#include "core/Symbols.hpp"
#include "hooks/HookDispatcher.hpp"
#include "input/InputNames.hpp"
#include "locale/Locale.hpp"
#include "speech/Sounds.hpp"
#include "speech/Speech.hpp"
#include "ui/Widgets.hpp"
#include "watch/GameState.hpp"
#include "watch/Watchers.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace da::features
{
    using RC::Unreal::FFrame;
    using RC::Unreal::UObject;

    namespace
    {
        // One prompt widget of the game's, an interaction prompt or a glint's: its question as the
        // rules weigh it (prompt::Question), and what the mod knows of the widget.
        struct Shown : prompt::Question
        {
            UObject* widget = nullptr;
            obj::ObjectKey key; // the widget's identity, so that a freed slot reused is not taken for it
            bool glint = false;
            double fade = 0.0; // a glint: the fade of its prompt its Blueprint was last handed
            bool up = false;   // a glint: its prompt shown, for the log
        };
        std::vector<Shown> g_shown;

        // The action bar in play (prompt::Bar); the menus read the same bar while a page is up.
        prompt::Bar g_bar;

        // The first question of an interaction waits a moment for its hint (prompt::HintFirst).
        prompt::HintFirst g_hintFirst;

        // Whether the interaction's prompts hold back for a hint at this look: one the HUD is still
        // to say (hud::TextSettling), or one the game may still be about to show.
        // The last question an interaction asked that was read, and when (LastInteractionPrompt).
        std::wstring g_lastQuestion;
        double g_lastQuestionAt = -1000.0;

        bool HoldForHint(double now)
        {
            // A stage on a clock gives no moment to wait for its hint.
            return hud::TextSettling(now) || (!InteractionStageTimed() && g_hintFirst.Waits(now, hud::LastSaidAt()));
        }

        Shown* Find(UObject* widget)
        {
            for (auto it = g_shown.begin(); it != g_shown.end(); ++it)
            {
                if (it->widget != widget) continue;
                if (obj::IsSame(it->key)) return &*it;
                // Another widget in the slot of one that was freed: tracked afresh.
                g_shown.erase(it);
                return nullptr;
            }
            return nullptr;
        }

        void Track(UObject* widget, bool glint)
        {
            if (!widget || !obj::IsLive(widget) || Find(widget)) return;
            Shown s;
            s.widget = widget;
            s.key = obj::KeyOf(widget);
            s.glint = glint;
            g_shown.push_back(s);
            log::Verbose(L"prompts: {} {} tracked", obj::ClassName(widget), obj::ObjectName(widget));
        }

        std::wstring Actions(const std::vector<ui::Prompt>& prompts)
        {
            std::vector<std::wstring> names;
            for (const auto& p : prompts)
            {
                if (!p.action.empty()) names.push_back(p.action);
            }
            return str::Join(names, L", ");
        }

        // A glint's prompt as the glint's own widget keeps it: shown (the glint's intensity
        // full, the same moment the glint authorises the press), faded in, and the widget not
        // going away. Without those members (another build of the game) the fade its Blueprint
        // was last handed stands in, which a widget that stopped ticking leaves as it was.
        bool GlintPromptUp(Shown& s)
        {
            bool up = false;
            if (obj::IsA(s.widget, L"BatesGlintWidget"))
            {
                const auto* showing = static_cast<const uint8_t*>(sym::MemberOf(s.widget, L"UBatesGlintWidget.bIsShowingPrompt"));
                const auto* destroying = static_cast<const uint8_t*>(sym::MemberOf(s.widget, L"UBatesGlintWidget.bIsDestroying"));
                const auto* faded = static_cast<const float*>(sym::MemberOf(s.widget, L"UBatesGlintWidget.CurrentShowPrompt"));
                up = showing && destroying && faded ? *showing != 0 && *destroying == 0 && *faded >= 0.99f : s.fade >= 0.99;
            }
            if (up != s.up)
            {
                s.up = up;
                log::Verbose(L"prompts: glint {} prompt {}", obj::ObjectName(s.widget), up ? L"up" : L"down");
            }
            return up;
        }

        // What a prompt widget says: each key cap's words with its key.
        std::wstring Readout(Shown& s, std::wstring* actions)
        {
            if (s.hidden) return {};
            // A glint and the prompt on the character's wrist are drawn on a screen layer of
            // their own, in no widget tree: their own flags say whether they are drawn.
            const bool shown = obj::IsWidgetShown(s.widget);
            const bool layer = !shown && ui::LayerWidgetDrawn(s.widget);
            if (!shown && !layer) return {};
            // A glint's widget is pooled and serves one glint after another, so what it showed
            // last says nothing: its prompt is read while its own flags have it up and a glint
            // spawned for the character holds it, with the name of that glint's thing.
            UObject* owner = nullptr;
            if (s.glint)
            {
                if (!GlintPromptUp(s)) return {};
                owner = UseLocationOfGlintWidget(s.widget);
                if (!owner) return {};
            }
            auto prompts = ui::PromptsIn(s.widget, layer);
            // A glint's cap can stand without its action; the glint itself knows the action
            // its prompt is for.
            if (s.glint && prompts.empty())
            {
                if (auto* definition = obj::FindProperty(s.widget, L"GlintAction"))
                {
                    UObject* action = nullptr;
                    if (obj::ReadObjectAt(obj::ValuePtr(s.widget, definition), obj::StructMember(definition, L"InputAction"), action) && obj::IsLive(action))
                    {
                        ui::Prompt prompt;
                        prompt.action = obj::ObjectName(action);
                        prompt.key = input::KeyForAction(prompt.action);
                        if (!prompt.key.empty()) prompts.push_back(prompt);
                    }
                }
            }
            // The glint marks a thing: the key is said with the thing's name, which is what a
            // sighted player sees the glint on.
            if (s.glint)
            {
                const auto label = UseLocationLabelOf(owner);
                for (auto& prompt : prompts)
                {
                    if (prompt.label.empty()) prompt.label = label;
                }
            }
            if (actions) *actions = Actions(prompts);
            return ui::PromptLines(prompts);
        }

        // The game sets an interaction prompt up for each question it asks and sends it away
        // with HideAndDestroy (a fade, then off the screen 0.3 s later): prompt::SetUp, Hide.
        void OnSetup(UObject* self, FFrame&)
        {
            Track(self, false);
            Shown* s = Find(self);
            if (!s) return;
            if (s->hidden) log::Verbose(L"prompts: {} set up anew", obj::ObjectName(self));
            prompt::SetUp(*s, gamethread::NowSeconds());
        }

        void OnHide(UObject* self, FFrame&)
        {
            Shown* s = Find(self);
            if (!s || s->hidden) return;
            prompt::Hide(*s);
            log::Verbose(L"prompts: {} sent away", obj::ObjectName(self));
        }

        // A glint's widget hands its prompt's fade to its Blueprint every frame it ticks: what
        // is ticking is tracked.
        void OnGlintPrompt(UObject* self, FFrame& stack)
        {
            Track(self, true);
            double fade = 0.0;
            if (Shown* s = Find(self); s && params::Float(stack, L"InProgress", fade)) s->fade = fade;
        }

        std::wstring Line(const ui::Prompt& p)
        {
            return ui::PromptLines({p});
        }

        void PollBar(double now)
        {
            // While a page is up the menus own the bar (prompt::Bar).
            if (watch::CurrentScreen())
            {
                g_bar.Update(now, true, {});
                return;
            }
            auto prompts = ui::Prompts(nullptr);
            // The menus' own prompts (IA_UI_*) linger on the bar a moment after a page goes; in
            // play they ask nothing, but for a document's transcript drawn now, whose scroll
            // (IA_UI_Scroll, "Scroll") the bar shows beside its other keys: the audit found it unread
            // at every long page (s52, s57, Hannah's diary in s58).
            const bool document = now - hud::DocumentDrawnAt() < 0.5;
            std::erase_if(prompts, [&](const ui::Prompt& p) { return str::StartsWith(p.action, L"IA_UI_") && !(document && p.action == L"IA_UI_Scroll"); });
            // A hint the HUD shows with it is said first.
            if (!prompts.empty()) g_hintFirst.Asked(now);
            if (HoldForHint(now)) return;
            std::vector<std::wstring> lines;
            for (const auto& p : prompts)
                lines.push_back(Line(p));
            const auto fresh = g_bar.Update(now, false, lines);
            if (fresh.empty()) return;
            const auto text = str::Join(fresh, L", ");
            log::Info(L"prompts: action bar \"{}\" [{}]", text, Actions(prompts));
            speech::Announce(text);
        }

        // ---- the phone's unlock swipe -------------------------------------------------------

        // The phone in the woods is unlocked by swipes (ABatesInteractionPhoneUnlock): from the
        // moment the game shows the swipe's prompt (its state 2) every swipe it takes adds to a
        // progress towards a target, and once no swipe came for a moment (SwipeDecayDelay) the
        // progress slides back (SwipeDecay a second). A push of the stick is taken once as it
        // goes over, and adds its value times CursorDeltaMultiplier times LeftStickMultiplier
        // (5 of the 50 in the prologue, slid back at 30 a second after 0.12 s): about ten quick
        // pushes in a row. A sighted player watches the slider on the phone move; the blips
        // follow it, rising as it fills and falling as it slides back, and the unlock is the
        // challenge won.
        UObject* g_phone = nullptr;
        obj::ObjectKey g_phoneKey;
        prompt::Swipe g_swipe; // the blips and the unlock (prompt::Swipe)
        double g_phoneLoggedAt = 0.0;
        double g_phoneLoggedLevel = -1.0;

        float PhoneFloat(const wchar_t* member)
        {
            const auto* at = static_cast<const float*>(sym::MemberOf(g_phone, member));
            return at ? *at : 0.0f;
        }

        int PhoneState()
        {
            const auto* set = static_cast<const uint8_t*>(sym::MemberOf(g_phone, L"ABatesInteractionPhoneUnlock.InteractionState.IsSet"));
            const auto* state = static_cast<const int32_t*>(sym::MemberOf(g_phone, L"ABatesInteractionPhoneUnlock.InteractionState"));
            return set && state && *set != 0 ? *state : -1;
        }

        void OnPhone(UObject* self, FFrame&)
        {
            if (self == g_phone && obj::IsSame(g_phoneKey)) return;
            g_phone = self;
            g_phoneKey = obj::KeyOf(self);
            g_swipe.Reset();
            log::Info(L"phone: {} takes the phone out", obj::ObjectName(self));
        }

        bool PhoneLive()
        {
            return g_phone && obj::IsSame(g_phoneKey) && obj::IsA(g_phone, L"BatesInteractionPhoneUnlock");
        }

        void PollPhone(double now)
        {
            if (!g_phone) return;
            if (!PhoneLive())
            {
                g_phone = nullptr;
                return;
            }
            const int state = PhoneState();
            const float target = PhoneFloat(L"ABatesInteractionPhoneUnlock.TargetProgress");
            const float progress = PhoneFloat(L"ABatesInteractionPhoneUnlock.CumulativeProgress");
            const prompt::Swipe::Step step = g_swipe.Update(now, state, progress, target);
            if (step.stateChanged) log::Info(L"phone: the unlock is in state {} (0 out of the pocket, 1 the intro, 2 the swipe, 3 the outro)", state);
            if (step.began)
            {
                g_phoneLoggedLevel = -1.0;
                log::Info(L"phone: the swipe counts now: a push of the stick adds up to {:.1f} of {:.0f}, and {:.2f} s after the last swipe the progress "
                          L"slides back {:.0f} a second",
                          PhoneFloat(L"ABatesInteractionPhoneUnlock.CursorDeltaMultiplier") * PhoneFloat(L"ABatesInteractionPhoneUnlock.LeftStickMultiplier"),
                          target, PhoneFloat(L"ABatesInteractionPhoneUnlock.SwipeDecayDelay"), PhoneFloat(L"ABatesInteractionPhoneUnlock.SwipeDecay"));
            }
            if (step.unlocked)
            {
                log::Info(L"phone: unlocked");
                sounds::Play(sounds::Cue::Confirm);
                if (cfg::Detailed()) speech::Announce(locale::Mod(L"result.success"));
            }
            if (state != 2 || target <= 0.0f) return;
            const double level = step.level;
            if (step.blip) sounds::Tick(level);
            if (level != g_phoneLoggedLevel && now - g_phoneLoggedAt >= 0.5)
            {
                g_phoneLoggedAt = now;
                g_phoneLoggedLevel = level;
                log::Verbose(L"phone: the swipe at {:.1f} of {:.0f}", progress, target);
            }
        }

        bool PhoneSwipeAsked()
        {
            return PhoneLive() && g_swipe.State() == 2;
        }

        void PollImpl()
        {
            if (gamethread::FrameCount() % 2 != 0) return;
            const double now = gamethread::NowSeconds();
            PollPhone(now);
            // While the game is paused or a menu is up every prompt keeps what it had: one still
            // up when play comes back is the same question, not a new one. Out of play otherwise
            // (a level loading behind its loading screen or a film) the play prompts are not on
            // the screen whatever their own flags say: a prompt of the last scene kept alive
            // behind the loading screen is not a question the game asks. What is still up when
            // play comes back is read afresh then. A glint's prompt is the game's answer to a
            // press in exploration: it is read while the character is the player's to walk (one
            // drawn as an interaction lets go, before the hand-over, is the last scene's), and
            // not while the mod walks her, whose arrival says it.
            const bool frozen = gamestate::Paused() || watch::CurrentScreen() != nullptr;
            const bool inPlay = gamestate::InPlay();
            const bool glintsAsked = ExplorationActive() && !WalkInProgress();
            if (!frozen) g_hintFirst.Interaction(inPlay && CharacterInInteraction());
            for (auto it = g_shown.begin(); it != g_shown.end();)
            {
                if (!obj::IsSame(it->key))
                {
                    it = g_shown.erase(it);
                    continue;
                }
                if (frozen)
                {
                    ++it;
                    continue;
                }
                // Out of the player's hands in play (a scene holding the character, the mod's
                // walk, the moment after a page before exploring resumes) a glint's prompt is not
                // read, and one still up stays the same prompt: only its going down makes its
                // next showing a new one.
                if (it->glint && inPlay && !glintsAsked)
                {
                    const bool drawn = obj::IsWidgetShown(it->widget) || ui::LayerWidgetDrawn(it->widget);
                    prompt::Withheld(*it, drawn && GlintPromptUp(*it) && UseLocationOfGlintWidget(it->widget));
                    ++it;
                    continue;
                }
                std::wstring actions;
                const auto text = inPlay && (!it->glint || glintsAsked) ? Readout(*it, &actions) : std::wstring();
                // A hint the HUD shows with it is said first.
                if (!it->glint && inPlay)
                {
                    if (!text.empty()) g_hintFirst.Asked(now);
                    if (HoldForHint(now))
                    {
                        ++it;
                        continue;
                    }
                }
                if (prompt::Read(*it, now, text, actions, it->glint))
                {
                    log::Info(L"prompts: {} {} \"{}\" [{}]", obj::ClassName(it->widget), obj::ObjectName(it->widget), text, actions);
                    speech::Announce(text);
                    if (!it->glint)
                    {
                        g_lastQuestion = text;
                        g_lastQuestionAt = now;
                    }
                }
                ++it;
            }
            PollBar(now);
        }

        void Poll(float)
        {
            obj::SafeInvokeLogged(L"prompts.PollImpl", [](void*) { PollImpl(); }, nullptr);
        }
    }

    void PromptsFeature::Install()
    {
        // The interaction prompt is set up by the game as the character reaches something.
        hooks::OnNative(L"/Script/Bates.BatesInteractionWidget:Setup", nullptr, [](UObject* self, FFrame&) { Track(self, false); });
        hooks::OnScript(L"BatesInteractionWidget", L"SetupInteractionWidgets", &OnSetup);
        hooks::OnScript(L"BatesInteractionWidget", L"HideAndDestroy", &OnHide);
        watch::OnWidgetClass(L"BatesInteractionWidget",
                             [](UObject* widget, bool appeared)
                             {
                                 if (appeared) Track(widget, false);
                             });
        hooks::OnScript(L"BatesGlintWidget", L"BP_UpdateShowPrompt", &OnGlintPrompt);
        // The phone's unlock: taken out of the pocket, its intro done, its outro done.
        for (const wchar_t* event : {L"OnReadyToPullFromPocket", L"OnIntroCompleted", L"OnOutroCompleted"})
            hooks::OnNative(std::wstring(L"/Script/Bates.BatesInteractionPhoneUnlock:") + event, nullptr, &OnPhone);
        gamethread::AddPoller(L"prompts", &Poll);
    }

    void PromptsFeature::Describe(std::vector<std::wstring>& out)
    {
        for (const auto& s : g_shown)
        {
            if (obj::IsLive(s.widget) && s.announced && !s.text.empty()) out.push_back(s.text);
        }
        if (!watch::CurrentScreen())
        {
            auto prompts = ui::Prompts(nullptr);
            std::erase_if(prompts, [](const ui::Prompt& p) { return str::StartsWith(p.action, L"IA_UI_"); });
            const auto bar = ui::PromptLines(prompts);
            if (!bar.empty()) out.push_back(bar);
        }
    }

    void PromptsFeature::Help(std::vector<std::wstring>& out)
    {
        if (PhoneSwipeAsked()) out.push_back(locale::Mod(L"help.swipe", input::KeyForAction(L"IA_Swipe_Right")));
        // A half circle of the stick, which nothing on the screen spells out beyond its picture.
        const bool gesture = std::any_of(g_shown.begin(), g_shown.end(),
                                         [](const Shown& s) { return obj::IsLive(s.widget) && !s.hidden && s.actions.find(L"Gesture") != std::wstring::npos; });
        if (gesture) out.push_back(locale::Mod(L"help.gesture"));
        // A half circle drawn with the grab beside it: the item is let go with the button (the
        // lantern of chapter 3), where the boiler of chapter 2 wanted R2 released.
        const bool held = std::any_of(g_shown.begin(), g_shown.end(),
                                      [](const Shown& s)
                                      {
                                          return obj::IsLive(s.widget) && !s.hidden && s.actions.find(L"Gesture") != std::wstring::npos &&
                                                 s.actions.find(L"IA_GrabInteraction") != std::wstring::npos;
                                      });
        if (held) out.push_back(locale::Mod(L"help.gesture.held", input::KeyForAction(L"IA_GrabInteraction")));
    }

    std::wstring LastInteractionPrompt()
    {
        return g_lastQuestion;
    }

    double LastInteractionPromptAt()
    {
        return g_lastQuestionAt;
    }

    bool InteractionAsks(std::wstring_view action)
    {
        return std::any_of(g_shown.begin(), g_shown.end(),
                           [&](const Shown& q)
                           { return !q.glint && !q.hidden && obj::IsLive(q.widget) && obj::IsWidgetShown(q.widget) && q.actions.find(action) != std::wstring::npos; });
    }

    bool InteractionPromptShown()
    {
        return std::any_of(g_shown.begin(), g_shown.end(),
                           [](const Shown& q) { return !q.glint && !q.hidden && !q.text.empty() && obj::IsLive(q.widget) && obj::IsWidgetShown(q.widget); });
    }
}
