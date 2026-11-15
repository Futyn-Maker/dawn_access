#include "features/Qte.hpp"

#include "features/QteRules.hpp"

#include "core/Config.hpp"
#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/ParamReader.hpp"
#include "core/Symbols.hpp"
#include "hooks/HookDispatcher.hpp"
#include "hotkeys/Hotkeys.hpp"
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
        // The game's own stage of a prompt that takes the press.
        constexpr int64_t kStageMain = 1;

        // A prompt: its state as the rules weigh it (qte::Prompt), and what the mod knows of it.
        struct Qte : qte::Prompt
        {
            UObject* widget = nullptr;
            UObject* stage = nullptr; // the activity's QTE stage that drives the widget
            std::wstring action;      // the input action the game asks for
            std::wstring text;        // what is said for it: the key or button
            sounds::Cue cue = sounds::Cue::Up;
            bool exampleWaits = false; // an example not said yet (qte::Example)
            bool voiced = false;       // an example the demonstration voices as a prompt in play (qte::Demo)
            bool standing = false;     // an example the game ended with no verdict and left drawn (qte::Stands)
        };
        std::vector<Qte> g_qtes;
        qte::Examples g_examples; // a page can show the same example in several places at once
        qte::Demo g_demo;         // the settings' examples voiced or not (the player's key)

        // A prompt the settings show as an example: on a page of a menu, or set up while a menu is up
        // and no play runs (the copies the QTE rows set up as a value changes stand outside the page).
        bool IsDemonstration(UObject* widget)
        {
            return ui::InsideMenuPage(widget) || (watch::CurrentScreen() != nullptr && !gamestate::InPlay());
        }

        // The demonstration stops: the examples it voiced fall silent where they stand.
        void SilenceExamples()
        {
            for (auto& q : g_qtes)
            {
                if (!q.voiced) continue;
                q.voiced = false;
                q.demonstration = true;
            }
        }

        Qte* Find(UObject* widget)
        {
            for (auto& q : g_qtes)
            {
                if (q.widget == widget) return &q;
            }
            return nullptr;
        }

        // The cue of the direction a prompt points (qte::DirectionOf).
        sounds::Cue CueOf(const std::wstring& action)
        {
            switch (qte::DirectionOf(action))
            {
            case qte::Direction::Down: return sounds::Cue::Down;
            case qte::Direction::Left: return sounds::Cue::Left;
            case qte::Direction::Right: return sounds::Cue::Right;
            case qte::Direction::Up: break;
            }
            return sounds::Cue::Up;
        }

        // The game gives a second or so for the press, so the key of a prompt cuts in over
        // anything being said the first time (a lesson on the sidebar comes up with the first
        // prompts): queued, it would come after the prompt has gone. While a prompt is up,
        // what the game says by itself waits (speech::Hold), since a word in the same moment
        // drowns the key: on 2026-10-24 a choice's "chosen" and a status card came within
        // 0.2 s of a prompt's key and the player heard only the timeout. The repeats and the
        // verdict are the prompt's own words; the examples a settings page shows wait their
        // turn.
        void Speak(Qte& qte, double now, bool first)
        {
            qte.spokenAt = now;
            sounds::Play(qte.cue);
            if (qte.text.empty()) return;
            if (qte.demonstration)
                speech::Announce(qte.text);
            else if (first)
                speech::Urgent(qte.text);
            else
                speech::Exclusive(qte.text);
        }

        bool g_holding = false; // speech is held for a prompt that is up

        // The stage of an activity that drives a prompt: the one whose widget it is. The stage
        // keeps it before it sets the prompt up.
        UObject* StageOf(UObject* widget)
        {
            for (auto* stage : obj::FindAllLive(L"BatesQTEStage"))
            {
                UObject* shown = nullptr;
                if (obj::ReadObject(stage, L"QTEWidget", shown) && shown == widget) return stage;
            }
            return nullptr;
        }

        int StageByte(UObject* stage, const wchar_t* member)
        {
            const auto* p = static_cast<const uint8_t*>(sym::MemberOf(stage, member));
            return p ? *p : -1;
        }

        // The game set a prompt up: the key or button is named by the device in hand and
        // played as its direction at once; the game takes the press only after its warm-up.
        void SetUp(UObject* widget, UObject* action, double warmup, const wchar_t* readFrom)
        {
            if (!widget || !obj::IsLive(widget)) return;
            const double now = gamethread::NowSeconds();
            const std::wstring actionName = action ? obj::ObjectName(action) : std::wstring();
            if (Qte* known = Find(widget))
            {
                // The same set-up told twice, by the game's own event after its call.
                if (qte::SameSetUp(known->action, known->setUpAt, actionName, now)) return;
                g_qtes.erase(std::remove_if(g_qtes.begin(), g_qtes.end(), [&](const Qte& q) { return q.widget == widget; }), g_qtes.end());
            }
            Qte qte;
            qte.widget = widget;
            qte.action = actionName;
            qte.text = actionName.empty() ? std::wstring() : input::KeyForAction(actionName);
            qte.cue = CueOf(actionName);
            qte.setUpAt = now;
            qte.warmup = warmup;
            // A prompt the settings show is the game's example of what its prompts look like: silent,
            // unless the player has the demonstration play (qte::Demo).
            qte.demonstration = IsDemonstration(widget);
            if (!qte.demonstration) qte.stage = StageOf(widget);
            log::Info(L"qte: {} set up with {} (\"{}\"), warm-up {:.2f} s (from {}){}", obj::ObjectName(widget), actionName, qte.text, warmup, readFrom,
                      qte.demonstration ? std::wstring(L", a demonstration")
                      : qte.stage       ? L", driven by " + obj::ObjectName(qte.stage)
                                        : std::wstring(L", its stage not found"));
            if (qte.demonstration)
            {
                // Voiced, while the demonstration plays, once it has stood a moment on its page
                // (qte::Example).
                qte.exampleWaits = true;
                g_qtes.push_back(qte);
                return;
            }
            g_qtes.push_back(qte);
            if (!g_holding)
            {
                g_holding = true;
                speech::Hold(L"qte");
            }
            Speak(g_qtes.back(), now, true);
        }

        void OnSetUpEvent(UObject* self, FFrame& stack)
        {
            UObject* action = nullptr;
            double warmup = 0.0;
            params::Object(stack, L"InInputAction", action);
            params::Float(stack, L"InWarmupDuration", warmup);
            SetUp(self, action, warmup, L"the widget's event");
        }

        void OnSetUpCall(UObject* self, FFrame& stack)
        {
            UObject* action = nullptr;
            double warmup = 0.0;
            params::Object(stack, L"InInputAction", action);
            params::Float(stack, L"InWarmupDuration", warmup);
            SetUp(self, action, warmup, L"the game's call");
        }

        // The prompt's progress: once the game is past its warm-up it takes the press, which
        // the tone marks again.
        void OnProgress(UObject* self, FFrame& stack)
        {
            Qte* qte = Find(self);
            if (!qte || qte->finished) return;
            int64_t stage = 0;
            params::Int(stack, L"InStage", stage);
            qte->paused = false;
            if (stage == kStageMain && !qte->opened)
            {
                qte->opened = true;
                if (qte->demonstration) return;
                const double now = gamethread::NowSeconds();
                const bool cue = qte::CuesOpening(now - qte->setUpAt);
                log::Info(L"qte: {} takes the press from {:.2f} s after its set-up{}", obj::ObjectName(self), now - qte->setUpAt,
                          cue ? L": its cue again" : L", its cue at the set-up standing for it");
                if (cue) sounds::Play(qte->cue);
                qte->spokenAt = now;
            }
        }

        void OnPaused(UObject* self, FFrame&)
        {
            if (Qte* qte = Find(self))
            {
                qte->paused = true;
                log::Info(L"qte: {} paused", obj::ObjectName(self));
            }
        }

        void Finish(Qte& qte, int64_t result, const wchar_t* readFrom)
        {
            if (qte.finished) return;
            // The pause on timeout's example stands on its page, as a paused prompt in play waits for the
            // press: kept, and voiced as such while the demonstration plays (its key every second), until
            // it is taken from the page.
            if (qte::Stands(qte.demonstration || qte.voiced, result, obj::IsWidgetShown(qte.widget) || ui::LayerWidgetDrawn(qte.widget)))
            {
                if (!qte.standing)
                {
                    qte.standing = true;
                    log::Info(L"qte: the example {} stands after {:.2f} s, ended with no verdict (from {}){}", obj::ObjectName(qte.widget),
                              gamethread::NowSeconds() - qte.setUpAt, readFrom, qte.voiced ? L", voiced on" : L"");
                }
                return;
            }
            qte.finished = true;
            // Ended with no verdict: no word and no tone, as the prompt shows none.
            const qte::Outcome outcome = qte::OutcomeOf(result);
            if (outcome == qte::Outcome::None)
            {
                log::Info(L"qte: {} ended with no verdict ({}) after {:.2f} s (from {})", obj::ObjectName(qte.widget), result,
                          gamethread::NowSeconds() - qte.setUpAt, readFrom);
                return;
            }
            const bool success = outcome == qte::Outcome::Success;
            const bool timeout = outcome == qte::Outcome::Timeout;
            log::Info(L"qte: {} {} after {:.2f} s (from {}){}", obj::ObjectName(qte.widget), success ? L"succeeded" : (timeout ? L"timed out" : L"failed"),
                      gamethread::NowSeconds() - qte.setUpAt, readFrom, qte.demonstration ? L", a demonstration" : L"");
            if (qte.demonstration) return;
            sounds::Play(success ? sounds::Cue::Confirm : sounds::Cue::Fail);
            if (cfg::Detailed()) speech::Exclusive(locale::Mod(success ? L"result.success" : (timeout ? L"result.timeout" : L"result.failure")));
        }

        void OnEndEvent(UObject* self, FFrame& stack)
        {
            int64_t result = 0;
            params::Int(stack, L"InEndState", result);
            if (Qte* qte = Find(self)) Finish(*qte, result, L"the widget's event");
        }

        void OnEndCall(UObject* self, FFrame& stack)
        {
            int64_t result = 0;
            params::Int(stack, L"InEndState", result);
            if (Qte* qte = Find(self)) Finish(*qte, result, L"the game's call");
        }

        void PollImpl()
        {
            const double now = gamethread::NowSeconds();
            for (auto it = g_qtes.begin(); it != g_qtes.end();)
            {
                if (!obj::IsLive(it->widget) || (it->finished && now - it->spokenAt > 5.0))
                {
                    it = g_qtes.erase(it);
                    continue;
                }
                Qte& qte = *it;
                if (qte.exampleWaits)
                {
                    const auto step = qte::Example(qte.setUpAt, now, obj::IsWidgetShown(qte.widget) || ui::LayerWidgetDrawn(qte.widget));
                    if (step != qte::ExampleStep::Wait)
                    {
                        qte.exampleWaits = false;
                        if (step == qte::ExampleStep::Drop)
                            log::Verbose(L"qte: the example {} went before it was said", obj::ObjectName(qte.widget));
                        else if (!g_demo.Playing())
                            log::Verbose(L"qte: the example {} stays silent: the demonstration is off", obj::ObjectName(qte.widget));
                        else if (g_examples.Says(qte.text, now))
                        {
                            // Voiced as a prompt in play: its cue and key, the cue as it takes the press, the
                            // key every second, the tone of its end.
                            qte.voiced = true;
                            qte.demonstration = false;
                            log::Info(L"qte: the demonstration voices the example {}", obj::ObjectName(qte.widget));
                            Speak(qte, now, true);
                        }
                    }
                }
                if (!qte.finished)
                {
                    // How it ended, by its stage or its own flags, or its key once more
                    // (qte::Poll). Whether the stage has begun matters only while it has no
                    // verdict and still drives this prompt, and is asked only then.
                    const bool drawn = obj::IsWidgetShown(qte.widget) || ui::LayerWidgetDrawn(qte.widget);
                    qte::Stage stage;
                    stage.known = qte.stage != nullptr;
                    if (stage.known && obj::IsLive(qte.stage))
                    {
                        stage.live = true;
                        stage.verdict = StageByte(qte.stage, L"UBatesQTEStage.QTEResult");
                        UObject* shown = nullptr;
                        obj::ReadObject(qte.stage, L"QTEWidget", shown);
                        stage.drivesIt = shown == qte.widget;
                        if (stage.verdict <= 0 && stage.drivesIt) stage.started = StageByte(qte.stage, L"UBatesQTEStage.bHasStarted") != 0;
                    }
                    const qte::Step step = qte::Poll(qte, now, drawn, stage);
                    if (step.finish)
                        Finish(qte, step.result, step.from);
                    else if (step.repeat)
                        Speak(qte, now, false);
                }
                ++it;
            }
            // What the game says by itself is held while a prompt is up and running in play with no
            // screen over it; a paused prompt, a menu or a lesson brought up over it lets the game
            // speak. Once none is, what was held is said, after the verdict.
            const bool hold = gamestate::InPlay() && !watch::CurrentScreen() &&
                              std::any_of(g_qtes.begin(), g_qtes.end(), [](const Qte& q) { return qte::Running(q) && obj::IsLive(q.widget); });
            if (hold && !g_holding)
            {
                g_holding = true;
                speech::Hold(L"qte");
            }
            else if (!hold && g_holding)
            {
                g_holding = false;
                speech::Release(L"qte");
            }
        }

        void Poll(float)
        {
            obj::SafeInvokeLogged(L"qte.PollImpl", [](void*) { PollImpl(); }, nullptr);
        }
    }

    void QteDemoFocus(const std::wstring& row, const std::wstring& value)
    {
        if (!g_demo.Focus(row, value)) return;
        SilenceExamples();
        log::Info(L"qte: the demonstration stops: the focus is on {} at \"{}\" now", row.empty() ? L"no row" : row, value);
    }

    namespace
    {
        // The settings row with the examples has the focus: the demonstration's key is the mod's.
        bool DemoRowFocused()
        {
            return g_demo.Here();
        }

        // The player's key on a QTE row of the settings: the examples voiced or silent. Turned on, the
        // example on the page now is voiced at once, as it stands.
        bool OnDemonstrationKey()
        {
            const auto toggled = g_demo.Toggled();
            if (toggled == qte::Demo::Toggle::NotHere) return false;
            if (toggled == qte::Demo::Toggle::Off)
            {
                SilenceExamples();
                log::Info(L"qte: the demonstration is turned off");
                speech::Now(locale::Mod(L"qte.demo.off"));
                return true;
            }
            log::Info(L"qte: the demonstration is turned on");
            speech::Now(locale::Mod(L"qte.demo.on"));
            const double now = gamethread::NowSeconds();
            for (auto& q : g_qtes)
            {
                if (!q.demonstration || q.exampleWaits || q.finished || !obj::IsLive(q.widget)) continue;
                if (!(obj::IsWidgetShown(q.widget) || ui::LayerWidgetDrawn(q.widget))) continue;
                // The example on the page now, running or standing (the pause on timeout's, qte::Stands):
                // voiced on from here as a prompt in play is. One that has ended is over.
                q.voiced = true;
                q.demonstration = false;
                log::Info(L"qte: the demonstration voices the example on the page, {}{}", obj::ObjectName(q.widget), q.standing ? L", which stands" : L"");
                Speak(q, now, true);
                break;
            }
            return true;
        }
    }

    void QteFeature::Install()
    {
        // The game's calls on the widget and the events it raises in the widget's own
        // Blueprint: whichever of the two the game goes through, the mod hears it.
        hooks::OnScript(L"BatesQTEWidget", L"BP_SetUpQTEPrompt", &OnSetUpEvent);
        hooks::OnNative(L"/Script/Bates.BatesQTEWidget:SetUpQTEPrompt", &OnSetUpCall, nullptr);
        hooks::OnScript(L"BatesQTEWidget", L"BP_UpdateQTEPromptProgress", &OnProgress);
        hooks::OnNative(L"/Script/Bates.BatesQTEWidget:UpdateQTEPromptProgress", &OnProgress, nullptr);
        hooks::OnScript(L"BatesQTEWidget", L"BP_SetQTEPaused", &OnPaused);
        hooks::OnScript(L"BatesQTEWidget", L"BP_EndQTEPrompt", &OnEndEvent);
        hooks::OnNative(L"/Script/Bates.BatesQTEWidget:EndQTEPrompt", &OnEndCall, nullptr);
        gamethread::AddPoller(L"qte", &Poll);
        hotkeys::OnCommand(hotkeys::Command::Demonstration, &OnDemonstrationKey);
        hotkeys::AddDemoContext(&DemoRowFocused);
    }

    void QteFeature::Describe(std::vector<std::wstring>& out)
    {
        for (const auto& qte : g_qtes)
        {
            if (obj::IsLive(qte.widget) && !qte.finished && !qte.demonstration && !qte.text.empty()) out.push_back(qte.text);
        }
    }

    void QteFeature::Help(std::vector<std::wstring>& out)
    {
        if (std::none_of(g_qtes.begin(), g_qtes.end(), [](const Qte& q) { return obj::IsLive(q.widget) && !q.finished && !q.demonstration; })) return;
        out.push_back(locale::Mod(input::CurrentScheme() == input::Scheme::Gamepad ? L"help.qte.pad" : L"help.qte"));
    }

    bool QteInProgress()
    {
        return std::any_of(g_qtes.begin(), g_qtes.end(), [](const Qte& q) { return !q.finished && !q.demonstration && obj::IsLive(q.widget); });
    }

    sounds::Cue DirectionCue(const std::wstring& action)
    {
        return CueOf(action);
    }
}
