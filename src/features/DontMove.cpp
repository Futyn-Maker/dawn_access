#include "features/DontMove.hpp"

#include "features/CalmRules.hpp"

#include "core/Config.hpp"
#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/ParamReader.hpp"
#include "core/Strings.hpp"
#include "hooks/HookDispatcher.hpp"
#include "input/InputNames.hpp"
#include "locale/GameText.hpp"
#include "locale/Locale.hpp"
#include "speech/Sounds.hpp"
#include "speech/Speech.hpp"
#include "ui/Widgets.hpp"
#include "watch/Watchers.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace da::features
{
    using RC::Unreal::FFrame;
    using RC::Unreal::FProperty;
    using RC::Unreal::UObject;

    namespace
    {
        // The game's kinds of drift, as its set-up names them.
        constexpr int64_t kDriftKinds = 9;

        // The trace's pace.
        constexpr double kTraceApart = 0.5;

        struct Encounter
        {
            UObject* widget = nullptr;
            bool setUp = false;     // the game has set the widget up
            bool modeKnown = false; // the version the widget runs has been read back
            bool passive = false;   // the controller held still, not a marker steered
            bool instructed = false;
            bool labelSaid = false; // the game's own label, drawn a moment after the start
            std::wstring words;     // what was said at the start
            bool started = false;
            bool finished = false;
            double startedAt = 0.0;
            calm::Follower follower; // the live marker's sounds
            double lastTraceAt = -10.0;
            bool positionLogged = false;
            // The version the game takes the player's input by now (IsUsingPassiveVersion, asked anew at
            // every input: UBatesDontMoveWidget::OnAction), 1 passive, 0 active, -1 not asked yet.
            int liveMode = -1;
        };
        std::vector<Encounter> g_encounters;

        struct Tutorial
        {
            UObject* widget = nullptr;
            std::wstring text; // the game's current lesson
        };
        std::vector<Tutorial> g_tutorials;

        Encounter* Find(UObject* widget)
        {
            for (auto& e : g_encounters)
            {
                if (e.widget == widget) return &e;
            }
            return nullptr;
        }

        Tutorial* FindTutorial(UObject* widget)
        {
            for (auto& t : g_tutorials)
            {
                if (t.widget == widget) return &t;
            }
            return nullptr;
        }

        // A lesson of the game's is on the screen: its words explain the mechanic.
        bool LessonShown()
        {
            return std::any_of(g_tutorials.begin(), g_tutorials.end(), [](const Tutorial& t) { return obj::IsLive(t.widget) && obj::IsWidgetShown(t.widget); });
        }

        // What the player steers with: the stick on a pad, the movement keys or the mouse
        // otherwise, as the game's own lessons name them.
        std::wstring Steering()
        {
            return locale::Mod(input::CurrentScheme() == input::Scheme::Gamepad ? L"input.leftstick" : L"input.keysormouse");
        }

        // The version the widget runs. The game decides it with IsUsingPassiveVersion() when
        // it sets the widget up and writes the answer into the widget's own record, which its
        // logic then follows; the record is read a frame after the set-up, once the game's own
        // handler has run, and the game's function is asked beside it for the log.
        void ReadMode(Encounter& e)
        {
            bool recorded = false;
            if (!obj::ReadBool(e.widget, L"bIsUsingPassiveVersion", recorded)) return;
            const bool asked = obj::CallForBool(e.widget, L"IsUsingPassiveVersion");
            bool motion = false;
            obj::ReadBool(e.widget, L"bUseMotionInput", motion);
            e.passive = recorded;
            e.modeKnown = true;
            double pool = 0.0;
            double grace = 0.0;
            obj::ReadFloat(e.widget, L"TimeOverThresholdLimit", pool);
            obj::ReadFloat(e.widget, recorded ? L"GraceTimerLimitPassive" : L"GraceTimerLimitActive", grace);
            // The passive version counts the controller's motion after one grace at its start; the active
            // one counts the marker past the edge, its grace begun again each time the marker is back.
            log::Info(L"dontmove: {} runs the {} version (the game's function says {}, {}motion input); its clock allows {:.2f} s {} in all, after a grace "
                      L"of {:.2f} s {}",
                      obj::ObjectName(e.widget), recorded ? L"passive" : L"active", asked ? L"passive" : L"active", motion ? L"" : L"no ", pool,
                      recorded ? L"of the controller moving" : L"past the edge", grace, recorded ? L"at its start" : L"each time");
        }

        // The game's Don't Move mode setting (UBatesGameUserSettings::GetDontMoveType: 0 passive, 1 active,
        // 2 dynamic, its default); -1 when it cannot be read.
        int ModeSetting()
        {
            static UObject* settings = nullptr;
            if (!settings || !obj::IsLive(settings)) settings = obj::FindFirstLive(L"BatesGameUserSettings");
            int64_t mode = -1;
            if (settings)
                obj::CallReturn(settings, L"GetDontMoveType",
                                [&](void* params, FProperty* returnValue)
                                {
                                    if (returnValue) obj::ReadIntAt(params, returnValue, mode);
                                });
            return static_cast<int>(mode);
        }

        // Which version the game takes the player's input by, as it asks at every input
        // (UBatesDontMoveWidget::OnAction calls IsUsingPassiveVersion each time): with the mode setting
        // on Dynamic it follows the device used last (UBatesGameUserSettings::GetActiveDontMoveType: a
        // PlayStation pad passive, anything else active), and an active run whose input is then taken as
        // the passive version's keeps the marker where it is pulled: the stick and the keys do nothing.
        // Jessica's run in act8_5 began active after a key of the keyboard and failed with the stick, and
        // two of Sam's with the keys failed with the marker never moving
        // (research/sessions/s65-play.log, 14:51:07, 15:05:34, 15:06:45). Logged as it changes.
        void TraceLiveMode(Encounter& e)
        {
            const int live = obj::CallForBool(e.widget, L"IsUsingPassiveVersion") ? 1 : 0;
            if (live == e.liveMode) return;
            const bool first = e.liveMode < 0;
            e.liveMode = live;
            if (first && live == (e.passive ? 1 : 0)) return;
            log::Info(L"dontmove: {} the game now takes the player's input as the {} version would (the device it last saw: {}){}", obj::ObjectName(e.widget),
                      live ? L"passive" : L"active", input::SchemeName(input::CurrentScheme()),
                      !e.passive && live ? L": the stick and the keys move the marker no more" : L"");
        }

        // The mod's own words for the version are kept for the help key alone: during the
        // encounter the game's own prompt and the sounds are all there is to hear (the
        // player's decision, 2026-10-21).
        void Instruct(Encounter& e)
        {
            if (e.instructed || !e.modeKnown || !e.started) return;
            e.instructed = true;
            log::Info(L"dontmove: {} version known while running: {} (said in the help only)", obj::ObjectName(e.widget), e.passive ? L"passive" : L"active");
        }

        // The active version draws no words of its own, where the passive one says "Не двигаться!": a
        // word of the mod's, short, at either level, so that the signals that follow are known for what
        // they are (Andrey's request after Jessica's run in act8_5, research/sessions/s65-play.log,
        // 14:51:07). Said once the version is known, the game's own words absent.
        void SayActiveStart(Encounter& e)
        {
            if (calm::StartWordsOf(false, e.labelSaid, e.modeKnown, e.passive) != calm::StartWords::Mod) return;
            e.labelSaid = true;
            e.words = locale::Mod(L"dontmove.active.start");
            log::Info(L"dontmove: {} the active version draws no words: \"{}\" said", obj::ObjectName(e.widget), e.words);
            speech::Urgent(e.words);
        }

        // The start: the tone of the danger and the game's own words on the widget.
        void Start(Encounter& e)
        {
            if (e.started) return;
            e.started = true;
            e.startedAt = gamethread::NowSeconds();
            e.words = str::JoinSentences(ui::DrawnTexts(e.widget));
            log::Info(L"dontmove: {} started: \"{}\"", obj::ObjectName(e.widget), e.words);
            sounds::Play(sounds::Cue::Down);
            if (calm::StartWordsOf(!e.words.empty(), e.labelSaid, e.modeKnown, e.passive) == calm::StartWords::Game)
            {
                e.labelSaid = true;
                speech::Announce(e.words);
            }
            else
            {
                SayActiveStart(e);
            }
            Instruct(e);
        }

        // The game draws its label ("Don't move!") when it sets the widget up, which can be
        // long after the widget first appears: said once, when it is there.
        void SayLabel(Encounter& e)
        {
            if (e.labelSaid) return;
            const auto label = str::JoinSentences(ui::DrawnTexts(e.widget));
            if (label.empty())
            {
                SayActiveStart(e);
                return;
            }
            e.labelSaid = true;
            e.words = str::JoinSentences({label, e.words});
            log::Info(L"dontmove: {} label \"{}\"", obj::ObjectName(e.widget), label);
            speech::Announce(label);
        }

        void Track(UObject* widget)
        {
            if (!widget || !obj::IsLive(widget) || Find(widget)) return;
            Encounter e;
            e.widget = widget;
            g_encounters.push_back(e);
            log::Info(L"dontmove: {} {} appeared", obj::ClassName(widget), obj::ObjectName(widget));
        }

        // The game's set-up of the widget: the drift it chose is logged for the record; the
        // version is read back on the next poll, after the game's own handler has written it.
        void OnSetupBefore(UObject* self, FFrame& stack)
        {
            Track(self);
            if (auto* data = static_cast<const uint8_t*>(params::StructPtr(stack, L"InDontMoveSetupData")))
            {
                // The set-up record: a time stamp, then the kind of drift as a byte.
                const int64_t drift = data[4];
                if (drift >= 0 && drift < kDriftKinds)
                    log::Info(L"dontmove: {} set up with drift {}", obj::ObjectName(self), drift);
                else
                    log::Info(L"dontmove: {} set up with an unnamed drift ({})", obj::ObjectName(self), drift);
            }
            if (Encounter* e = Find(self))
            {
                e->setUp = true;
                e->modeKnown = false;
                e->instructed = false;
            }
        }

        void Finish(Encounter& e, bool success, const wchar_t* readFrom)
        {
            if (e.finished) return;
            e.finished = true;
            log::Info(L"dontmove: {} {} after {:.1f} s (from {})", obj::ObjectName(e.widget), success ? L"succeeded" : L"failed",
                      gamethread::NowSeconds() - e.startedAt, readFrom);
            sounds::Play(success ? sounds::Cue::Confirm : sounds::Cue::Fail);
            if (cfg::Detailed()) speech::Announce(locale::Mod(success ? L"result.success" : L"result.failure"));
        }

        bool ReadVector2(UObject* object, std::wstring_view name, double& x, double& y)
        {
            auto* prop = obj::FindProperty(object, name);
            if (!prop) return false;
            void* container = obj::ValuePtr(object, prop);
            return obj::ReadFloatAt(container, obj::StructMember(prop, L"X"), x) && obj::ReadFloatAt(container, obj::StructMember(prop, L"Y"), y);
        }

        // The marker as the game draws and judges it (calm::Marker), read from the widget.
        bool ReadMarker(UObject* widget, bool passive, calm::Marker& m)
        {
            UObject* image = nullptr;
            if (!obj::ReadObject(widget, L"Image_Gyro", image) || !image || !obj::IsLive(image)) return false;
            auto* transform = obj::FindProperty(image, L"RenderTransform");
            if (!transform) return false;
            auto* translation = obj::StructMember(transform, L"Translation");
            if (!translation) return false;
            void* container = obj::ValuePtrAt(obj::ValuePtr(image, transform), translation);
            double px = 0.0;
            double py = 0.0;
            if (!obj::ReadFloatAt(container, obj::StructMember(translation, L"X"), px) ||
                !obj::ReadFloatAt(container, obj::StructMember(translation, L"Y"), py))
                return false;
            double limit = 0.0;
            if (!obj::ReadFloat(widget, L"TranslationLimit", limit) || limit <= 0.0) return false;
            m.x = px / limit;
            m.y = py / limit;
            obj::ReadBool(widget, L"bWithinArea", m.flagWithin);
            obj::ReadFloat(widget, L"Threshold", m.edge);
            obj::ReadFloat(widget, L"InnerCenteringThreshold", m.centre);
            if (ReadVector2(widget, L"TargetDislocation", m.pullX, m.pullY))
            {
                m.pullX /= limit;
                m.pullY /= limit;
            }
            // The pull runs for a while at a time; between pulls the target keeps its last value.
            obj::ReadBool(widget, L"bIsDislocating", m.pulled);
            obj::ReadFloat(widget, L"TimeOverThreshold", m.clock);
            obj::ReadFloat(widget, L"TimeOverThresholdLimit", m.pool);
            obj::ReadFloat(widget, L"GraceTimer", m.grace);
            obj::ReadFloat(widget, passive ? L"GraceTimerLimitPassive" : L"GraceTimerLimitActive", m.graceLimit);
            obj::ReadBool(widget, L"CanFail", m.canFail);
            obj::ReadFloat(widget, L"CurrentInstability", m.instability);
            if (m.edge <= 0.0) m.edge = 0.8;
            return true;
        }

        // The sounds of the marker (calm::Follower), with the log of what they follow.
        void FollowMarker(Encounter& e, double now)
        {
            calm::Marker m;
            if (!ReadMarker(e.widget, e.passive, m)) return;
            const bool beyond = calm::Beyond(m);
            if (!e.positionLogged)
            {
                e.positionLogged = true;
                log::Info(L"dontmove: {} marker read at {:.2f}, {:.2f}; the game counts it past the edge from {:.2f} of its limit and settles it below {:.2f}",
                          obj::ObjectName(e.widget), m.x, m.y, m.edge, m.centre);
            }
            if (now - e.lastTraceAt >= kTraceApart)
            {
                e.lastTraceAt = now;
                int64_t stage = -1;
                obj::ReadInt(e.widget, L"Stage", stage);
                log::Verbose(L"dontmove: {} marker {:.2f}, {:.2f} {} (flag {}), {} {:.2f}, {:.2f}, clock {:.2f} of {:.2f}, grace {:.2f} of {:.2f}, "
                             L"instability {:.2f}, stage {}",
                             obj::ObjectName(e.widget), m.x, m.y, beyond ? L"past the edge" : L"inside", m.flagWithin ? L"within" : L"outside",
                             m.pulled ? L"pulled to" : L"last pull", m.pullX, m.pullY, m.clock, m.pool, m.grace, m.graceLimit, m.instability, stage);
            }
            const calm::Step step = e.follower.Update(now, m, e.modeKnown, e.passive);
            const auto name = [&] { return obj::ObjectName(e.widget); };
            if (step.beyondChanged && !e.passive)
                log::Info(L"dontmove: {} marker {} at {:.2f}, {:.2f}, clock {:.2f} of {:.2f}", name(), step.beyond ? L"past the edge" : L"back inside", m.x,
                          m.y, m.clock, m.pool);
            if (step.movingChanged)
                log::Info(L"dontmove: {} the game {} (instability {:.2f}), clock {:.2f} of {:.2f}: sound: {}", name(),
                          step.moving ? L"sees the controller move" : L"sees it still", m.instability, m.clock, m.pool, step.moving ? L"thud" : L"ping");
            if (step.drainingChanged && step.draining) log::Info(L"dontmove: {} clock running, {:.2f} of {:.2f} s used", name(), m.clock, m.pool);
            if (step.clockStep > 0) log::Info(L"dontmove: {} {} of the clock gone: sound: clock", name(), step.clockStep == 2 ? L"three quarters" : L"half");
            if (step.steered)
            {
                const wchar_t* vertical = step.steerUp > 0 ? L"up" : (step.steerUp < 0 ? L"down" : L"");
                const wchar_t* side = step.steerRight > 0 ? L" right" : (step.steerRight < 0 ? L" left" : L"");
                if (step.steerChanged)
                    log::Info(L"dontmove: {} steer {}{} ({}): sound: signal{}", name(), vertical, side,
                              step.beyond ? L"back from the edge" : L"against the pull", step.beyond ? L" with thud" : L"");
                else
                    log::Verbose(L"dontmove: {} sound: signal {}{} with thud", name(), vertical, side);
                // The new way in one word, at both levels, over whatever is being said.
                if (step.word != calm::Way::None)
                {
                    const wchar_t* key = step.word == calm::Way::Up     ? L"dir.up"
                                         : step.word == calm::Way::Down ? L"dir.down"
                                         : step.word == calm::Way::Left ? L"dir.left"
                                                                        : L"dir.right";
                    log::Info(L"dontmove: {} the way said: {}", name(), key);
                    speech::Urgent(locale::Mod(key));
                }
            }
            for (size_t i = 0; i < step.sounds.size(); ++i)
            {
                const calm::Sound& sound = step.sounds[i];
                switch (sound.kind)
                {
                case calm::Sound::Kind::Ping:
                    // The passive version's ping in the middle goes unlogged, as the blips do.
                    if (step.pingBack && i == 0)
                        log::Verbose(L"dontmove: {} sound: ping (back inside)", name());
                    else if (!e.passive)
                        log::Trace(L"dontmove: {} sound: ping", name());
                    sounds::Ping();
                    break;
                case calm::Sound::Kind::Steer: sounds::Steer(sound.right, sound.up, sound.beyond); break;
                case calm::Sound::Kind::Blip: sounds::Marker(sound.right, sound.up, false, sound.beyond); break;
                case calm::Sound::Kind::Moving: sounds::Marker(0.0, 0.0, false, true); break;
                case calm::Sound::Kind::Clock:
                    sounds::Play(sounds::Cue::Timer);
                    if (cfg::Detailed()) speech::Announce(locale::Mod(step.clockStep == 2 ? L"dontmove.clock.most" : L"dontmove.clock.half"));
                    break;
                }
            }
        }

        void PollImpl()
        {
            if (gamethread::FrameCount() % 3 != 0) return;
            const double now = gamethread::NowSeconds();
            for (auto it = g_encounters.begin(); it != g_encounters.end();)
            {
                if (!obj::IsLive(it->widget))
                {
                    if (!it->finished && it->started) log::Info(L"dontmove: {} went away without a verdict", obj::ObjectName(it->widget));
                    it = g_encounters.erase(it);
                    continue;
                }
                Encounter& e = *it;
                const bool shown = obj::IsWidgetShown(e.widget);
                if (e.setUp && !e.modeKnown) ReadMode(e);
                if (!e.started && shown) Start(e);
                if (!e.instructed) Instruct(e);
                // The widget's own stage, the twin of the game's events (calm::VerdictOfStage).
                if (e.started && !e.finished)
                {
                    int64_t stage = -1;
                    if (obj::ReadInt(e.widget, L"Stage", stage))
                    {
                        const calm::StageVerdict verdict = calm::VerdictOfStage(stage);
                        if (verdict != calm::StageVerdict::None) Finish(e, verdict == calm::StageVerdict::Success, L"the widget's stage");
                    }
                }
                if (e.started && !e.finished && !shown && now - e.startedAt > 1.0)
                {
                    log::Info(L"dontmove: {} hidden without a verdict", obj::ObjectName(e.widget));
                    e.finished = true;
                }
                if (e.started && !e.finished && shown)
                {
                    SayLabel(e);
                    FollowMarker(e, now);
                    TraceLiveMode(e);
                }
                ++it;
            }
            for (auto it = g_tutorials.begin(); it != g_tutorials.end();)
            {
                if (!obj::IsLive(it->widget))
                    it = g_tutorials.erase(it);
                else
                    ++it;
            }
        }

        void Poll(float)
        {
            obj::SafeInvokeLogged(L"dontmove.PollImpl", [](void*) { PollImpl(); }, nullptr);
        }

        // ---- the game's lesson ---------------------------------------------------------

        void TrackTutorial(UObject* widget)
        {
            if (!widget || !obj::IsLive(widget) || FindTutorial(widget)) return;
            Tutorial t;
            t.widget = widget;
            g_tutorials.push_back(t);
            log::Info(L"dontmove: lesson {} {} appeared", obj::ClassName(widget), obj::ObjectName(widget));
        }

        std::wstring LessonText(UObject* widget, std::wstring_view block)
        {
            return str::CollapseWhitespace(str::StripMarkup(ui::TextNamed(widget, block)));
        }

        // The game has put the words of a stage of its lesson on the screen: said as it
        // sets them, before they have faded in.
        void OnLessonStage(UObject* self, FFrame& stack)
        {
            TrackTutorial(self);
            Tutorial* t = FindTutorial(self);
            if (!t) return;
            int64_t stage = -1;
            params::Int(stack, L"TutorialStage", stage);
            const auto text = LessonText(self, L"BMUIStyledTextBlock_55");
            log::Info(L"dontmove: lesson {} stage {}: \"{}\"", obj::ObjectName(self), stage, text);
            if (text.empty() || text == t->text) return;
            t->text = text;
            speech::Announce(text);
        }

        // The game's reminder to try the marker, shown when the player has not.
        void OnLessonReminder(UObject* self, FFrame& stack)
        {
            TrackTutorial(self);
            bool show = false;
            params::Bool(stack, L"ShowText", show);
            const auto text = LessonText(self, L"BMUIStyledTextBlock_Reminder");
            log::Info(L"dontmove: lesson {} reminder {}: \"{}\"", obj::ObjectName(self), show ? L"shown" : L"hidden", text);
            if (show && !text.empty()) speech::Announce(text);
        }
    }

    void DontMoveFeature::Install()
    {
        hooks::OnNative(L"/Script/Bates.BatesDontMoveWidget:Setup", &OnSetupBefore, nullptr);
        watch::OnWidgetClass(L"BatesDontMoveWidget",
                             [](UObject* widget, bool appeared)
                             {
                                 if (appeared) Track(widget);
                             });
        hooks::OnScript(L"BatesDontMoveWidget", L"BP_Success",
                        [](UObject* self, FFrame&)
                        {
                            if (Encounter* e = Find(self)) Finish(*e, true, L"the widget's event");
                        });
        hooks::OnNative(L"/Script/Bates.BatesDontMoveWidget:TriggerSuccess", nullptr,
                        [](UObject* self, FFrame&)
                        {
                            if (Encounter* e = Find(self)) Finish(*e, true, L"the game's call");
                        });
        hooks::OnNative(L"/Script/Bates.BatesDontMoveWidget:TriggerFailure", nullptr,
                        [](UObject* self, FFrame&)
                        {
                            if (Encounter* e = Find(self)) Finish(*e, false, L"the game's call");
                        });
        // The game's lesson in the mechanic: its words are set by the game's own events.
        watch::OnWidgetClass(L"BatesDontMoveTutorialWidget",
                             [](UObject* widget, bool appeared)
                             {
                                 if (appeared) TrackTutorial(widget);
                             });
        hooks::OnScript(L"BatesDontMoveTutorialWidget", L"FadeInTutorialText", &OnLessonStage);
        hooks::OnScript(L"BatesDontMoveTutorialWidget", L"ToggleReminderText", &OnLessonReminder);
        gamethread::AddPoller(L"dontmove", &Poll);
    }

    void DontMoveFeature::Describe(std::vector<std::wstring>& out)
    {
        for (const auto& t : g_tutorials)
        {
            if (obj::IsLive(t.widget) && obj::IsWidgetShown(t.widget) && !t.text.empty()) out.push_back(t.text);
        }
        for (const auto& e : g_encounters)
        {
            if (obj::IsLive(e.widget) && e.started && !e.finished && !e.words.empty()) out.push_back(e.words);
        }
    }

    void DontMoveFeature::Help(std::vector<std::wstring>& out)
    {
        for (const auto& e : g_encounters)
        {
            if (!obj::IsLive(e.widget) || !e.started || e.finished) continue;
            out.push_back(e.modeKnown && e.passive ? locale::Mod(L"help.dontmove.passive") : locale::Mod(L"help.dontmove.active", Steering()));
            // With the game's mode on Dynamic the version follows the device used last, and switching
            // devices during a run makes the game ignore the steering (TraceLiveMode).
            if (ModeSetting() == 2)
                out.push_back(locale::Mod(L"help.dontmove.dynamic",
                                          std::vector<std::wstring>{gametext::ResolveWithFallback(L"BATES_SETTING_DM_TYPE", L"Don't Move Type"),
                                                                    gametext::ResolveWithFallback(L"BATES_SETTING_DM_TYPE_OPTION_DYNAMIC", L"Dynamic"),
                                                                    gametext::ResolveWithFallback(L"BATES_SETTING_DM_TYPE_OPTION_ACTIVE", L"Active"),
                                                                    gametext::ResolveWithFallback(L"BATES_SETTING_DM_TYPE_OPTION_PASSIVE", L"Passive")}));
            return;
        }
    }

    bool DontMoveInProgress()
    {
        return std::any_of(g_encounters.begin(), g_encounters.end(), [](const Encounter& e) { return e.started && !e.finished && obj::IsLive(e.widget); });
    }
}
