#include "features/DontMove.hpp"

#include "core/Config.hpp"
#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/ParamReader.hpp"
#include "core/Strings.hpp"
#include "hooks/HookDispatcher.hpp"
#include "input/InputNames.hpp"
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
    using RC::Unreal::UObject;

    namespace
    {
        // The game's kinds of drift, as its set-up names them.
        constexpr int64_t kDriftKinds = 9;

        // The marker's share of the way to the edge within which it counts as in the middle;
        // the pace of its blip from the middle to the edge; the pace of the double ping inside;
        // the pace of the steering signal and of the thudded blip past the edge; the least
        // time between two clock cues; the trace's pace.
        constexpr double kCentred = 0.15;
        constexpr double kSlowestBlip = 0.4;
        constexpr double kFastestBlip = 0.12;
        constexpr double kInsideTick = 0.5;
        constexpr double kSteerApart = 0.35;
        constexpr double kBeyondBlip = 0.2;
        constexpr double kClockCueApart = 1.0;
        constexpr double kPingHeard = 0.25; // nothing else sounds this long after the double ping
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
            // The live marker.
            bool beyond = false;
            bool draining = false; // the game's clock is eating the pool
            int poolStep = 0;      // the shares of the pool already told
            double lastBlipAt = -10.0;
            double lastSteerAt = -10.0;
            double lastPingAt = -10.0;
            double lastClockAt = -10.0;
            double lastTraceAt = -10.0;
            int steerRight = 0; // the way last steered, in steps
            int steerUp = 0;
            bool positionLogged = false;
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
            log::Info(L"dontmove: {} runs the {} version (the game's function says {}, {}motion input); its clock allows {:.2f} s past the edge in all, after "
                      L"a grace of {:.2f} s each time",
                      obj::ObjectName(e.widget), recorded ? L"passive" : L"active", asked ? L"passive" : L"active", motion ? L"" : L"no ", pool, grace);
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

        // The start: the tone of the danger and the game's own words on the widget.
        void Start(Encounter& e)
        {
            if (e.started) return;
            e.started = true;
            e.startedAt = gamethread::NowSeconds();
            e.words = str::JoinSentences(ui::DrawnTexts(e.widget));
            log::Info(L"dontmove: {} started: \"{}\"", obj::ObjectName(e.widget), e.words);
            sounds::Play(sounds::Cue::Down);
            if (!e.words.empty())
            {
                e.labelSaid = true;
                speech::Announce(e.words);
            }
            Instruct(e);
        }

        // The game draws its label ("Don't move!") when it sets the widget up, which can be
        // long after the widget first appears: said once, when it is there.
        void SayLabel(Encounter& e)
        {
            if (e.labelSaid) return;
            const auto label = str::JoinSentences(ui::DrawnTexts(e.widget));
            if (label.empty()) return;
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

        // The marker as the game draws and judges it. Its place is a share of the widget's own
        // limit of travel (left to right, top to bottom); the game counts it past the edge when
        // the larger of the two shares reaches its threshold, lets it settle below another, and
        // in the active version pulls it towards a point, the pull, for a while at a time. The
        // game's clock: a grace runs while the marker is out past the limit, and after it every
        // moment past the edge drains a pool that never refills; the pool empty is the failure.
        struct Marker
        {
            double x = 0.0;
            double y = 0.0;
            bool flagWithin = true; // the game's own flag, which lags its judgement
            double edge = 0.8;
            double centre = 1.0;
            double pullX = 0.0;
            double pullY = 0.0;
            bool pulled = false;
            double clock = 0.0;
            double pool = 0.0;
            double grace = 0.0;
            double graceLimit = 0.0;
            bool canFail = false;
        };

        bool ReadMarker(UObject* widget, bool passive, Marker& m)
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
            if (m.edge <= 0.0) m.edge = 0.8;
            return true;
        }

        // The sounds of the marker, told apart by how many notes they have.
        //
        // Active version: the double ping means the marker is inside and nothing is asked
        // (every half second, and at once when it comes back in). A signal of two equal notes
        // in one ear means push to that side; three notes rising mean push up, falling mean
        // push down: at once when a pull begins or turns, and, past the edge, every third of
        // a second with a low thud in front, pointing back to the middle.
        //
        // Passive version: the marker's place is the sound. A blip sounds on the side the
        // marker has drifted to, higher when it is high; past the edge a low thud comes
        // first; the double ping when it sits in the middle.
        //
        // The game's clock: the clock cue sounds when its grace runs out and the pool starts to
        // drain, and again as half and three quarters of the pool are gone.
        void FollowMarker(Encounter& e, double now)
        {
            Marker m;
            if (!ReadMarker(e.widget, e.passive, m)) return;
            const double offset = std::max(std::fabs(m.x), std::fabs(m.y));
            const bool beyond = offset >= m.edge; // the game's own test for its clock
            const double share = std::min(offset / m.edge, 1.0);
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
                log::Verbose(L"dontmove: {} marker {:.2f}, {:.2f} {} (flag {}), {} {:.2f}, {:.2f}, clock {:.2f} of {:.2f}, grace {:.2f} of {:.2f}, stage {}",
                             obj::ObjectName(e.widget), m.x, m.y, beyond ? L"past the edge" : L"inside", m.flagWithin ? L"within" : L"outside",
                             m.pulled ? L"pulled to" : L"last pull", m.pullX, m.pullY, m.clock, m.pool, m.grace, m.graceLimit, stage);
            }
            bool cameBack = false;
            if (beyond != e.beyond)
            {
                e.beyond = beyond;
                cameBack = !beyond;
                log::Info(L"dontmove: {} marker {} at {:.2f}, {:.2f}, clock {:.2f} of {:.2f}", obj::ObjectName(e.widget),
                          beyond ? L"past the edge" : L"back inside", m.x, m.y, m.clock, m.pool);
            }
            // The marker back inside: the double ping, at once and before anything else.
            if (cameBack && e.modeKnown && !e.passive)
            {
                e.lastPingAt = now;
                e.lastBlipAt = now;
                log::Verbose(L"dontmove: {} sound: ping (back inside)", obj::ObjectName(e.widget));
                sounds::Ping();
            }
            const bool pingHeard = now - e.lastPingAt < kPingHeard;
            // The clock: its grace spent and the marker still out, the pool drains (logged); the
            // clock cue sounds as half and three quarters of the pool are gone.
            const bool draining = m.canFail && m.graceLimit > 0.0 && m.grace >= m.graceLimit && beyond;
            if (draining != e.draining)
            {
                e.draining = draining;
                if (draining) log::Info(L"dontmove: {} clock running, {:.2f} of {:.2f} s used", obj::ObjectName(e.widget), m.clock, m.pool);
            }
            if (m.pool > 0.0 && !pingHeard)
            {
                const double used = m.clock / m.pool;
                const int step = used >= 0.75 ? 2 : used >= 0.5 ? 1 : 0;
                if (step > e.poolStep)
                {
                    e.poolStep = step;
                    log::Info(L"dontmove: {} {} of the clock gone: sound: clock", obj::ObjectName(e.widget), step == 2 ? L"three quarters" : L"half");
                    e.lastClockAt = now;
                    sounds::Play(sounds::Cue::Timer);
                }
            }
            if (e.modeKnown && !e.passive)
            {
                // The way to push: past the edge, back towards the middle; inside, against a
                // pull as it begins or turns. Up on the screen is a smaller y.
                double wayX = 0.0;
                double wayY = 0.0;
                if (beyond)
                {
                    wayX = -m.x;
                    wayY = m.y;
                }
                else if (m.pulled)
                {
                    wayX = -m.pullX;
                    wayY = m.pullY;
                }
                const double way = std::max(std::fabs(wayX), std::fabs(wayY));
                if (way > 0.05)
                {
                    const int right = static_cast<int>(std::lround(std::clamp(wayX / way, -1.0, 1.0) * 2.0));
                    const int up = static_cast<int>(std::lround(std::clamp(wayY / way, -1.0, 1.0) * 2.0));
                    const bool changed = right != e.steerRight || up != e.steerUp;
                    if ((changed || (beyond && now - e.lastSteerAt >= kSteerApart)) && !pingHeard)
                    {
                        const wchar_t* vertical = up > 0 ? L"up" : (up < 0 ? L"down" : L"");
                        const wchar_t* side = right > 0 ? L" right" : (right < 0 ? L" left" : L"");
                        if (changed)
                            log::Info(L"dontmove: {} steer {}{} ({}): sound: signal{}", obj::ObjectName(e.widget), vertical, side,
                                      beyond ? L"back from the edge" : L"against the pull", beyond ? L" with thud" : L"");
                        else
                            log::Verbose(L"dontmove: {} sound: signal {}{} with thud", obj::ObjectName(e.widget), vertical, side);
                        e.steerRight = right;
                        e.steerUp = up;
                        e.lastSteerAt = now;
                        sounds::Steer(right / 2.0, up / 2.0, beyond);
                        return;
                    }
                }
                else if (!beyond)
                {
                    e.steerRight = 0;
                    e.steerUp = 0;
                }
                if (beyond) return; // the signal is the sound past the edge
                // Inside: the double ping every half second.
                if (now - e.lastBlipAt >= kInsideTick)
                {
                    e.lastBlipAt = now;
                    log::Trace(L"dontmove: {} sound: ping", obj::ObjectName(e.widget));
                    sounds::Ping();
                }
                return;
            }
            // Passive: the blip where the marker is; the double ping when it sits in the middle.
            const bool centred = offset < kCentred;
            const double interval = centred ? kInsideTick : beyond ? kBeyondBlip : kSlowestBlip - (kSlowestBlip - kFastestBlip) * share;
            if (now - e.lastBlipAt < interval) return;
            e.lastBlipAt = now;
            if (centred)
                sounds::Ping();
            else
                sounds::Marker(std::clamp(m.x / m.edge, -1.0, 1.0), 0.5 - 0.5 * std::clamp(m.y / m.edge, -1.0, 1.0), false, beyond);
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
                if (e.started && !e.finished && !shown && now - e.startedAt > 1.0)
                {
                    log::Info(L"dontmove: {} hidden without a verdict", obj::ObjectName(e.widget));
                    e.finished = true;
                }
                if (e.started && !e.finished && shown)
                {
                    SayLabel(e);
                    FollowMarker(e, now);
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
            return;
        }
    }

    bool DontMoveInProgress()
    {
        return std::any_of(g_encounters.begin(), g_encounters.end(), [](const Encounter& e) { return e.started && !e.finished && obj::IsLive(e.widget); });
    }
}
