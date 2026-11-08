#include "features/Shooting.hpp"

#include "features/ShootingRules.hpp"
#include "features/WorldRead.hpp"

#include "core/Config.hpp"
#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/ParamReader.hpp"
#include "core/Strings.hpp"
#include "core/Symbols.hpp"
#include "hooks/HookDispatcher.hpp"
#include "hotkeys/Hotkeys.hpp"
#include "input/InputNames.hpp"
#include "locale/GameText.hpp"
#include "locale/Locale.hpp"
#include "speech/Sounds.hpp"
#include "speech/Speech.hpp"
#include "watch/GameState.hpp"
#include "watch/Watchers.hpp"

#include <Unreal/NameTypes.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

namespace da::features
{
    using RC::Unreal::FFrame;
    using RC::Unreal::FProperty;
    using RC::Unreal::UObject;

    namespace
    {
        using namespace world;

        // How the game runs a shooting stage (UBatesShootingStage): StartShootingStage sets
        // bShootingStageStarted and OnFinished clears it. Every frame UpdateStage projects the aim
        // point (the centre of the screen: the stage's camera modifier looks at it) and each target
        // onto the screen, takes the target nearest the aim, and UpdateOptions highlights it while
        // it lies within InitialRadius pixels of the aim and keeps it until SelectedRadius; only
        // that one can be highlighted. A press (ConfirmShootInput) takes the highlighted target and
        // ends the stage; with nothing highlighted it does nothing at all: no shot, no miss. The
        // stage ends otherwise by its clock (SecondsUntilTimeout, real seconds, standing still in a
        // pause). The game never clears ChosenOption or HighlightedOption between plays of a
        // stage, so they count only while the stage runs.
        constexpr const wchar_t* kStarted = L"UBatesShootingStage.bShootingStageStarted";
        constexpr const wchar_t* kAimOnlyDone = L"UBatesShootingStage.bAimOnlyStageCompleted";
        constexpr const wchar_t* kTimedOut = L"UBatesActivitySequenceStage.bHasTimedOut";
        constexpr const wchar_t* kSecondsIn = L"UBatesActivitySequenceStage.SecondsInCurrentStage";

        // A target of the stage: one of its options, a thing a shot may take.
        struct Target
        {
            UObject* option = nullptr; // the game's UBatesShootingOption
            int index = 0;
            std::wstring label;        // the designers' name of the thing (shoot::TargetLabel)
            UObject* widget = nullptr; // the circle the game draws on it
            double metres = 0.0;       // from the camera, at the last look
            // Where the game puts it (UBatesShootingOption::GetTargetLocation), found once.
            UObject* actor = nullptr;
            bool fromBone = false;
            UObject* mesh = nullptr; // the actor's skeletal mesh, for a target on a bone
            std::wstring bone;
            Vec offset;
        };

        struct Stage
        {
            bool on = false;
            UObject* stage = nullptr;
            std::wstring name;
            std::vector<Target> targets;
            UObject* chosenBefore = nullptr; // what ChosenOption held as the stage began
            bool timedOutBefore = false;     // and whether it had timed out before
            bool timed = false;
            double timeLimit = 0.0;
            bool aimingOnly = false;
            std::vector<shoot::AimCondition> conditions; // an aim-only stage's, which complete it
            bool targetsHidden = false;
            bool reticleHidden = false;
            // The Chris and Ashley dilemma (shoot::DilemmaAt): its limits, its middle, whether the
            // turn commits on the highlight, whether the aiming's X axis is inverted, and the region
            // the aim stood in at the last look.
            bool dilemma = false;
            bool finishOnHighlight = false;
            double middle = 0.5;
            double minYaw = 0.0;
            double maxYaw = 0.0;
            bool invertX = false;
            int dilemmaRegion = -2;
            // The walk key's aim on a target (shoot::Snaps, shoot::AimOnto): whether the stage takes it,
            // the pitch's limits beside the yaw's, and the target the aim is held on (-1 for none).
            bool snaps = false;
            double minPitch = 0.0;
            double maxPitch = 0.0;
            int locked = -1;
            UObject* shotWidget = nullptr; // a target's circle the game destroyed as shot
            int chosen = -1;               // the target the player chose by key
            int leading = -1;              // the target the sound led to at the last look
            int highlighted = -1;          // the target the game would take a shot on, at the last look
            shoot::Sight sight;            // of the target led, at the last look
            shoot::FireWord fire;
            double startedAt = 0.0;
            double heldSince = -1.0;
            shoot::Cadence blips;
            double loggedAt = 0.0;
        };
        Stage g_stage;
        bool g_aimSound = true;
        double g_lookedForAt = -100.0;
        // A stage that draws no widget to name its start (hidden targets and reticle) is looked for
        // this often while the camera stands on an activity's layer: a scan of every object.
        constexpr double kLookEvery = 3.0;
        double g_seekUntil = -1.0; // a widget of a stage came up: look for the stage at once until then

        bool Flag(UObject* object, const wchar_t* member)
        {
            const auto* p = static_cast<const uint8_t*>(sym::MemberOf(object, member));
            return p && *p != 0;
        }

        double FloatAt(UObject* object, const wchar_t* member)
        {
            const auto* p = static_cast<const float*>(sym::MemberOf(object, member));
            return p ? *p : 0.0;
        }

        bool Started(UObject* stage)
        {
            return stage && obj::IsLive(stage) && obj::IsA(stage, L"BatesShootingStage") && Flag(stage, kStarted);
        }

        UObject* StartedStage()
        {
            for (UObject* stage : obj::FindAllLive(L"BatesShootingStage"))
            {
                if (Started(stage)) return stage;
            }
            return nullptr;
        }

        // Whether the camera stands on an activity's layer (9 to 11), where a stage of an activity
        // runs; elsewhere no shooting stage is looked for.
        bool ActivityCamera()
        {
            UObject* controller = Controller();
            UObject* camera = nullptr;
            if (!controller || !obj::ReadObject(controller, L"PlayerCameraManager", camera) || !obj::IsLive(camera)) return false;
            const auto* layer = static_cast<const uint8_t*>(sym::MemberOf(camera, L"ABatesPlayerCameraManager.ActiveLayer"));
            const auto* set = static_cast<const uint8_t*>(sym::MemberOf(camera, L"ABatesPlayerCameraManager.ActiveLayer.IsSet"));
            return layer && set && *set && *layer >= 9 && *layer <= 11;
        }

        // ---- where a target is ---------------------------------------------------------------

        UObject* ComponentOfClass(UObject* actor, const wchar_t* classPath)
        {
            UObject* cls = obj::FindObject(classPath);
            auto* fn = cls ? obj::FindFunction(actor, L"GetComponentByClass") : nullptr;
            if (!fn) return nullptr;
            UObject* found = nullptr;
            obj::Call(
                actor, fn,
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"ComponentClass") *static_cast<UObject**>(obj::ValuePtrAt(params, prop)) = cls;
                    }
                },
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"ReturnValue") obj::ReadObjectAt(params, prop, found);
                    }
                });
            return obj::IsLive(found) ? found : nullptr;
        }

        bool SocketLocation(UObject* component, const std::wstring& socket, Vec& out)
        {
            auto* fn = obj::FindFunction(component, L"GetSocketLocation");
            if (!fn) return false;
            bool ok = false;
            obj::Call(
                component, fn,
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"InSocketName")
                            std::construct_at(reinterpret_cast<RC::Unreal::FName*>(obj::ValuePtrAt(params, prop)), socket.c_str(), RC::Unreal::FNAME_Add);
                    }
                },
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"ReturnValue") ok = ReadVec(obj::ValuePtrAt(params, prop), prop, out);
                    }
                });
            return ok;
        }

        // Where the game puts a target (UBatesShootingOption::GetTargetLocation): on a bone of the
        // actor's skeletal mesh, moved by the option's offset, or where the actor's root stands.
        void FindPlace(Target& t)
        {
            t.actor = nullptr;
            t.mesh = nullptr;
            if (!obj::ReadObject(t.option, L"CachedTargetActor", t.actor) || !obj::IsLive(t.actor))
            {
                t.actor = nullptr;
                return;
            }
            obj::ReadBool(t.option, L"bFindTargetLocationFromBone", t.fromBone);
            if (!t.fromBone) return;
            obj::ReadString(t.option, L"TargetBoneName", t.bone);
            VecProperty(t.option, L"WorldOffsetFromTargetBone", t.offset);
            t.mesh = ComponentOfClass(t.actor, L"/Script/Engine.SkeletalMeshComponent");
            if (!t.mesh) obj::ReadObject(t.actor, L"RootComponent", t.mesh);
        }

        bool TargetLocation(Target& t, Vec& out)
        {
            if (!t.actor || !obj::IsLive(t.actor) || (t.fromBone && (!t.mesh || !obj::IsLive(t.mesh)))) FindPlace(t);
            if (!t.actor) return false;
            if (!t.fromBone) return ActorLocation(t.actor, out);
            Vec at;
            if (!t.mesh || !SocketLocation(t.mesh, t.bone, at)) return false;
            out = Vec{at.x + t.offset.x, at.y + t.offset.y, at.z + t.offset.z};
            return true;
        }

        // Where a target stands against the aim: the camera looks at the stage's aim point.
        shoot::Sight Look(const Vec& eye, double pitch, double yaw, const Vec& at, bool highlighted)
        {
            shoot::Sight s;
            s.placed = true;
            s.horizontal = BearingDegrees(eye, at, yaw);
            s.vertical = ElevationDegrees(eye, at) - pitch;
            Vec forward, right, up;
            ViewBasis(pitch, yaw, forward, right, up);
            const Vec d = Minus(at, eye);
            const double length = std::sqrt(Dot(d, d));
            s.angle = length > 0.0 ? std::acos(std::clamp(Dot(forward, d) / length, -1.0, 1.0)) * 180.0 / std::numbers::pi : 0.0;
            s.onTarget = highlighted;
            // The game keeps a highlighted target until it is SelectedRadius away, more than it
            // took to highlight it: the aim's own drift keeps it.
            s.solid = highlighted;
            return s;
        }

        const Target* TargetAt(const Stage& s, int index)
        {
            for (const auto& t : s.targets)
            {
                if (t.index == index) return &t;
            }
            return nullptr;
        }

        std::wstring LabelOf(const Target& t)
        {
            return t.label.empty() ? locale::Mod(L"shoot.unnamed") : t.label;
        }

        // ---- the stage -----------------------------------------------------------------------

        // Whether the stage stands still with the world: a pause, or a screen of the game over the
        // picture. Nothing is judged, sounded or timed meanwhile.
        bool Held()
        {
            return gamestate::Paused() || watch::CurrentScreen() != nullptr;
        }

        // The limits of the stage's aim, from its camera settings (ShootingStageCameraSettings:
        // MinYawAngle, MaxYawAngle, MinPitchAngle, MaxPitchAngle), which UpdateInputAngles clamps it to.
        void ReadLimits(UObject* stage, Stage& s)
        {
            auto* camera = obj::FindProperty(stage, L"ShootingStageCameraSettings");
            void* settings = camera ? obj::ValuePtr(stage, camera) : nullptr;
            if (!settings) return;
            obj::ReadFloatAt(settings, obj::StructMember(camera, L"MinYawAngle"), s.minYaw);
            obj::ReadFloatAt(settings, obj::StructMember(camera, L"MaxYawAngle"), s.maxYaw);
            obj::ReadFloatAt(settings, obj::StructMember(camera, L"MinPitchAngle"), s.minPitch);
            obj::ReadFloatAt(settings, obj::StructMember(camera, L"MaxPitchAngle"), s.maxPitch);
        }

        // The dilemma's measure as the stage keeps it (shoot::DilemmaAt): the yaw's limits
        // (ReadLimits), its middle (ChrisAshleyMiddleChoiceSize), and the player's setting that
        // inverts the aiming's X axis (bInvertAimingXAxis).
        void ReadDilemma(UObject* stage, Stage& s)
        {
            obj::ReadFloat(stage, L"ChrisAshleyMiddleChoiceSize", s.middle);
            if (UObject* user = obj::FindFirstLive(L"BatesGameUserSettings")) obj::ReadBool(user, L"bInvertAimingXAxis", s.invertX);
        }

        void Begin(UObject* stage, const wchar_t* why)
        {
            g_stage = Stage{};
            Stage& s = g_stage;
            s.on = true;
            s.stage = stage;
            s.startedAt = gamethread::NowSeconds();
            obj::ReadString(stage, L"StageName", s.name);
            obj::ReadObject(stage, L"ChosenOption", s.chosenBefore);
            s.timedOutBefore = Flag(stage, kTimedOut);
            obj::ReadBool(stage, L"bShouldTimeOut", s.timed);
            obj::ReadFloat(stage, L"SecondsUntilTimeout", s.timeLimit);
            obj::ReadBool(stage, L"bStageIsAimingOnly", s.aimingOnly);
            if (s.aimingOnly)
            {
                if (auto* list = obj::FindProperty(stage, L"AimOnlyStageCompletionConditions"))
                {
                    obj::ForEachArrayElement(stage, list,
                                             [&](void* element, FProperty* inner)
                                             {
                                                 shoot::AimCondition c;
                                                 int64_t axis = 1, op = 5;
                                                 double value = 0.0;
                                                 obj::ReadIntAt(element, obj::StructMember(inner, L"AimAxis"), axis);
                                                 obj::ReadIntAt(element, obj::StructMember(inner, L"Operator"), op);
                                                 obj::ReadFloatAt(element, obj::StructMember(inner, L"ExpectedValue"), value);
                                                 c.axis = static_cast<int>(axis);
                                                 c.op = static_cast<int>(op);
                                                 c.value = value;
                                                 s.conditions.push_back(c);
                                             });
                }
                std::vector<std::wstring> said;
                static const wchar_t* const kOps[] = {L"==", L"!=", L"<", L"<=", L">", L">="};
                for (const auto& c : s.conditions)
                    said.push_back(std::format(L"{} {} {:g}", c.axis == 0 ? L"pitch" : L"yaw", c.op >= 0 && c.op < 6 ? kOps[c.op] : L"?", c.value));
                log::Info(L"shooting: {} is aimed only, done once {}", s.name, said.empty() ? L"(no condition)" : str::Join(said, L" and "));
            }
            obj::ReadBool(stage, L"bHideTargets", s.targetsHidden);
            obj::ReadBool(stage, L"bHideReticule", s.reticleHidden);
            obj::ReadBool(stage, L"bIsChrisAshleyDilemma", s.dilemma);
            obj::ReadBool(stage, L"bFinishOnHighlight", s.finishOnHighlight);
            ReadLimits(stage, s);
            if (s.dilemma) ReadDilemma(stage, s);
            int64_t type = -1;
            obj::ReadInt(stage, L"ShootingType", type);
            std::vector<UObject*> options;
            obj::ReadObjectArray(stage, L"Options", options);
            std::vector<std::wstring> logged;
            for (size_t i = 0; i < options.size(); ++i)
            {
                UObject* option = options[i];
                if (!obj::IsLive(option)) continue;
                Target t;
                t.option = option;
                t.index = static_cast<int>(i);
                bool byName = false;
                std::wstring actorName;
                std::wstring next;
                obj::ReadBool(option, L"bFindTargetActorByName", byName);
                obj::ReadString(option, L"TargetActorName", actorName);
                UObject* outcome = nullptr;
                if (obj::ReadObject(option, L"TargetStageOutcome", outcome) && obj::IsLive(outcome)) obj::ReadString(outcome, L"NextStageName", next);
                t.label = shoot::TargetLabel(byName, actorName, next);
                obj::ReadObject(option, L"TargetWidget", t.widget);
                double initial = 0.0;
                double selected = 0.0;
                obj::ReadFloat(option, L"InitialRadius", initial);
                obj::ReadFloat(option, L"SelectedRadius", selected);
                UObject* actor = nullptr;
                obj::ReadObject(option, L"CachedTargetActor", actor);
                logged.push_back(std::format(L"{} \"{}\" ({}{}, highlighted within {:.0f} px and kept to {:.0f} px, a hit leads to {})", t.index, t.label,
                                             obj::IsLive(actor) ? obj::ObjectName(actor) : std::wstring(L"no actor"),
                                             byName ? L", found by name " + actorName : std::wstring(), initial, selected, next.empty() ? L"?" : next));
                FindPlace(t);
                s.targets.push_back(t);
            }
            // The dilemma's sides by the game's own names for the two (DA_CharacterMeta_Chris and
            // _Ashley: LocalisedName SMG013_66598 and SMG013_66601), the first option Chris's.
            if (s.dilemma && s.targets.size() == 2)
            {
                s.targets[0].label = gametext::Resolve(L"SMG013_66598");
                s.targets[1].label = gametext::Resolve(L"SMG013_66601");
                const auto& left = s.targets[s.invertX ? 1 : 0].label;
                const auto& right = s.targets[s.invertX ? 0 : 1].label;
                log::Info(L"shooting: {} is the dilemma: the stick's left takes the aim to {}, its right to {}; yaw {:g} to {:g}, the middle {:g}{}", s.name,
                          left, right, s.minYaw, s.maxYaw, s.middle, s.finishOnHighlight ? L"; a side reached commits" : L"");
                // The aim chooses: the sides named as the aim takes them (Andrey, 2026-11-08: "left:
                // Chris, right: Ashley" left him asking what makes the choice).
                if (!left.empty() && !right.empty()) speech::Announce(locale::Mod(L"shoot.dilemma", left, right));
            }
            s.snaps = shoot::Snaps({s.targets.size(), s.aimingOnly, s.targetsHidden, s.finishOnHighlight, s.dilemma});
            log::Info(L"shooting: {} begins ({}): type {}, {} target(s) [{}]; {}; aim assist {}; aiming only {}; targets {}, reticle {}; the aim between yaw "
                      L"{:g} and {:g}, pitch {:g} and {:g}; {}",
                      s.name, why, type, s.targets.size(), str::Join(logged, L"; "),
                      s.timed ? std::format(L"{:.1f} s on the clock", s.timeLimit) : std::wstring(L"no clock"), Flag(stage, L"UBatesShootingStage.bUseAimAssist"),
                      s.aimingOnly, s.targetsHidden ? L"hidden" : L"shown", s.reticleHidden ? L"hidden" : L"shown", s.minYaw, s.maxYaw, s.minPitch, s.maxPitch,
                      s.snaps ? L"the walk key puts the aim on a target" : L"the walk key puts the aim on nothing here");
            // A timed stage draws its clock on every target as a ring: the timer cue marks it, as it
            // marks a timed choice. The key a shot takes is what the circles show, said once.
            if (s.timed) sounds::Play(sounds::Cue::Timer);
            std::vector<std::wstring> parts;
            if (cfg::Detailed()) parts.push_back(locale::Mod(L"shoot.aim"));
            if (cfg::Detailed() && s.timed) parts.push_back(locale::Mod(L"cue.timer"));
            if (!s.aimingOnly && !s.targetsHidden)
            {
                const auto key = input::KeyForAction(L"IA_SequenceShoot");
                if (!key.empty()) parts.push_back(key);
            }
            if (!parts.empty()) speech::Announce(str::JoinSentences(parts));
        }

        void End(const wchar_t* why)
        {
            Stage& s = g_stage;
            if (!s.on) return;
            const double now = gamethread::NowSeconds();
            // A stage that ends while the world stands still ends with the world taken apart or
            // turned back, and has no verdict to tell.
            shoot::Ending e;
            e.still = s.heldSince >= 0.0 || !obj::IsLive(s.stage);
            int hit = -1;
            if (!e.still)
            {
                for (const auto& t : s.targets)
                {
                    if (s.shotWidget && t.widget == s.shotWidget) hit = t.index;
                }
                e.shotCircle = hit >= 0;
                UObject* chosen = nullptr;
                if (obj::ReadObject(s.stage, L"ChosenOption", chosen) && chosen && chosen != s.chosenBefore)
                {
                    e.chosenChanged = true;
                    for (const auto& t : s.targets)
                    {
                        if (hit < 0 && t.option == chosen) hit = t.index;
                    }
                }
                e.timedOutNow = Flag(s.stage, kTimedOut);
                e.timedOutBefore = s.timedOutBefore;
                e.timed = s.timed;
                e.seconds = FloatAt(s.stage, kSecondsIn);
                e.limit = s.timeLimit;
                e.aimingOnly = s.aimingOnly;
                e.aimReached = Flag(s.stage, kAimOnlyDone);
                e.unseen = s.targetsHidden && s.reticleHidden && !s.dilemma;
            }
            switch (shoot::Judge(e))
            {
            case shoot::Verdict::Hit:
            {
                const Target* t = TargetAt(s, hit);
                // The dilemma's first turn shoots nobody: reaching a side turns the gun on that one.
                if (s.dilemma && s.finishOnHighlight)
                {
                    log::Info(L"shooting: {} ended: the aim turned to {} after {:.2f} s ({})", s.name, t ? t->label : L"?", now - s.startedAt, why);
                    sounds::Play(sounds::Cue::Confirm);
                    if (cfg::Detailed() && t && !t->label.empty()) speech::Announce(t->label + L".");
                    break;
                }
                log::Info(L"shooting: {} ended: target {} \"{}\" shot after {:.2f} s ({})", s.name, hit, t ? t->label : L"", now - s.startedAt, why);
                sounds::Play(sounds::Cue::Confirm);
                if (cfg::Detailed()) speech::Announce(locale::Mod(L"shoot.hit"));
                break;
            }
            case shoot::Verdict::AimReached:
                log::Info(L"shooting: {} ended: the aim reached what the stage asked after {:.2f} s ({})", s.name, now - s.startedAt, why);
                sounds::Play(sounds::Cue::Confirm);
                if (cfg::Detailed()) speech::Announce(locale::Mod(L"result.success"));
                break;
            case shoot::Verdict::TimedOut:
                log::Info(L"shooting: {} ended: the time ran out after {:.2f} s, the clock at {:.2f} of {:.1f} s ({})", s.name, now - s.startedAt, e.seconds,
                          e.limit, why);
                sounds::Play(sounds::Cue::Fail);
                if (cfg::Detailed()) speech::Announce(locale::Mod(L"result.timeout"));
                break;
            case shoot::Verdict::None:
                log::Info(L"shooting: {} ended with no verdict after {:.2f} s ({}{})", s.name, now - s.startedAt, why,
                          e.still    ? L", standing still"
                          : e.unseen ? L", its targets and reticle hidden: the game resolved it by itself"
                                     : L"");
                break;
            }
            g_stage = Stage{};
        }

        // The stage takes up where it stood: the word to fire is earned afresh.
        void Resume(Stage& s, double now)
        {
            log::Info(L"shooting: {} goes on after standing still for {:.1f} s", s.name, now - s.heldSince);
            s.heldSince = -1.0;
            s.fire.Reset();
            s.blips.Rest(now);
        }

        void LogAim(const Stage& s, const std::vector<shoot::Candidate>& candidates)
        {
            std::vector<std::wstring> parts;
            for (const auto& c : candidates)
            {
                const Target* t = TargetAt(s, c.index);
                parts.push_back(std::format(L"{} \"{}\" {:.1f} deg {}, {:.1f} deg {}, {:.1f} off{}", c.index, t ? t->label : L"", std::fabs(c.sight.horizontal),
                                            c.sight.horizontal >= 0 ? L"right" : L"left", std::fabs(c.sight.vertical), c.sight.vertical >= 0 ? L"up" : L"down",
                                            c.sight.angle, c.sight.onTarget ? L", highlighted" : L""));
            }
            // With no target on the screen the game keeps its largest float as the distance.
            const double px = FloatAt(s.stage, L"UBatesShootingStage.CachedScreenDistanceToClosestTarget");
            log::Verbose(L"shooting: aim yaw {:.2f} pitch {:.2f} (the game's), {}, {:.1f} of {:.1f} s; {}", FloatAt(s.stage, L"UBatesShootingStage.CachedYaw"),
                         FloatAt(s.stage, L"UBatesShootingStage.CachedPitch"),
                         px < 1e6 ? std::format(L"{:.1f} px from the nearest target", px) : std::wstring(L"no target on the screen"), FloatAt(s.stage, kSecondsIn),
                         s.timeLimit, str::Join(parts, L"; "));
        }

        // ---- the aim put on a target --------------------------------------------------------

        // Where the game's camera stands, as the stage's update asks it (GetCameraLocation).
        bool CameraPlace(Vec& out)
        {
            UObject* controller = Controller();
            UObject* camera = nullptr;
            if (!controller || !obj::ReadObject(controller, L"PlayerCameraManager", camera) || !obj::IsLive(camera)) return false;
            return VecReturn(camera, L"GetCameraLocation", out);
        }

        // The stage's aim turned onto a target (shoot::AimOnto): the game clamps it to its own limits
        // and aims there from its next update on, and judges the highlight as ever. Only a stage that
        // runs is written to.
        bool PutAim(Stage& s, Target& t, shoot::AimAngles* put = nullptr)
        {
            if (!Started(s.stage)) return false;
            const auto* start = static_cast<const double*>(sym::MemberOf(s.stage, L"UBatesShootingStage.InitialTargetWorldPosition"));
            auto* yaw = static_cast<float*>(sym::MemberOf(s.stage, L"UBatesShootingStage.CachedYaw"));
            auto* pitch = static_cast<float*>(sym::MemberOf(s.stage, L"UBatesShootingStage.CachedPitch"));
            Vec eye;
            Vec at;
            if (!start || !yaw || !pitch || !CameraPlace(eye) || !TargetLocation(t, at)) return false;
            const auto angles = shoot::AimOnto(eye, Vec{start[0], start[1], start[2]}, at, Flag(s.stage, L"UBatesShootingStage.bUseAimAssist"));
            *yaw = static_cast<float>(angles.yaw);
            *pitch = static_cast<float>(angles.pitch);
            if (put) *put = angles;
            return true;
        }

        // The game's dead zone of the aim's stick (MC_Gameplay: IA_SequenceAim on Gamepad_Right2D).
        constexpr double kAimDeadZone = 0.1;

        // The aim held on its target while the player does not aim herself (shoot::LockHolds): put on
        // it again at each look, so that it stays on a target that moves.
        void HoldAim(Stage& s)
        {
            if (s.locked < 0) return;
            Target* t = nullptr;
            for (auto& x : s.targets)
            {
                if (x.index == s.locked) t = &x;
            }
            Vec at;
            const bool placed = t && TargetLocation(*t, at);
            const auto aims = input::ActionWorked(L"IA_SequenceAim", kAimDeadZone);
            const auto lock = shoot::LockHolds(Started(s.stage), placed, aims.value_or(false), aims.has_value());
            if (lock == shoot::Lock::Holds && PutAim(s, *t)) return;
            log::Info(L"shooting: the aim is no longer held on target {}: {}", s.locked,
                      lock == shoot::Lock::PlayerAims   ? L"the player aims herself"
                      : lock == shoot::Lock::AimUnknown ? L"the keys of the aim cannot be read, so the player's own aim cannot be told"
                      : lock == shoot::Lock::StageOver  ? L"the stage is over"
                                                        : L"the target is no longer placed");
            s.locked = -1;
        }

        // The aim sound: a blip on the side of the target, higher when it stands above the aim
        // and lower below, quicker as the aim nears it, and the double ping while the game would
        // take a shot on it. Where the game hides its targets, nothing leads to them: only the
        // double ping, while the reticle shows the game's highlight.
        void GuideDilemma(Stage& s, double now);

        void Guide(Stage& s, double now)
        {
            if (s.dilemma && s.targets.size() == 2)
            {
                GuideDilemma(s, now);
                return;
            }
            HoldAim(s);
            Vec eye;
            double pitch = 0.0;
            double yaw = 0.0;
            if (!CameraLine(eye, pitch, yaw)) return;
            // An aim-only stage with conditions: the sound leads past the turn that completes it
            // (shoot::AimGoal), in the stage's own measure of its aim.
            if (s.aimingOnly && !s.conditions.empty())
            {
                const double aimYaw = FloatAt(s.stage, L"UBatesShootingStage.CachedYaw");
                const double aimPitch = FloatAt(s.stage, L"UBatesShootingStage.CachedPitch");
                s.sight = shoot::AimGoal(s.conditions, aimYaw, aimPitch);
                if (now - s.loggedAt >= 0.5)
                {
                    s.loggedAt = now;
                    log::Verbose(L"shooting: aim yaw {:.2f} pitch {:.2f}; the turn that completes the stage lies {:.1f} deg right and {:.1f} deg up{}", aimYaw,
                                 aimPitch, s.sight.horizontal, s.sight.vertical, s.sight.onTarget ? L", reached" : L"");
                }
                if (!g_aimSound || !s.sight.placed || !s.blips.Due(now, s.sight.onTarget, shoot::BlipInterval(s.sight))) return;
                const auto blip = shoot::BlipOf(s.sight);
                sounds::Aim(blip.pan, blip.level, blip.locked, blip.muffled);
                return;
            }
            UObject* highlightedOption = nullptr;
            if (!s.aimingOnly) obj::ReadObject(s.stage, L"HighlightedOption", highlightedOption);
            std::vector<shoot::Candidate> candidates;
            int highlighted = -1;
            for (auto& t : s.targets)
            {
                Vec at;
                if (!TargetLocation(t, at)) continue;
                const bool on = highlightedOption && t.option == highlightedOption;
                if (on) highlighted = t.index;
                t.metres = Distance(eye, at) / 100.0;
                candidates.push_back({t.index, Look(eye, pitch, yaw, at, on)});
            }
            if (highlighted != s.highlighted)
            {
                const Target* t = TargetAt(s, highlighted);
                log::Info(L"shooting: {}", t ? std::format(L"the game would take a shot on target {} \"{}\"", t->index, t->label)
                                             : std::wstring(L"the game would take no shot now"));
                s.highlighted = highlighted;
            }
            const int led = s.targetsHidden ? highlighted : shoot::Lead(candidates, s.chosen, s.leading);
            if (led != s.leading)
            {
                if (led >= 0) log::Info(L"shooting: the sound leads to target {}", led);
                s.leading = led;
            }
            s.sight = shoot::Sight{};
            for (const auto& c : candidates)
            {
                if (c.index == led) s.sight = c.sight;
            }
            if (s.fire.Update(now, led, s.sight.solid))
            {
                log::Info(L"shooting: a shot has been sure of target {} for a fifth of a second: the word to fire", led);
                if (cfg::Detailed()) speech::Announce(locale::Mod(L"shoot.fire"));
            }
            if (now - s.loggedAt >= 0.5)
            {
                s.loggedAt = now;
                LogAim(s, candidates);
            }
            if (!g_aimSound || led < 0 || !s.sight.placed) return;
            if (s.targetsHidden && (s.reticleHidden || !s.sight.onTarget)) return;
            if (!s.blips.Due(now, s.sight.onTarget, shoot::BlipInterval(s.sight))) return;
            const auto blip = shoot::BlipOf(s.sight);
            sounds::Aim(blip.pan, blip.level, blip.locked, blip.muffled);
        }

        // The dilemma's aim (shoot::LeadDilemma): its place between the two sides, led to the side the
        // player chose by key, the double ping while the game highlights a side.
        void GuideDilemma(Stage& s, double now)
        {
            const double yaw = FloatAt(s.stage, L"UBatesShootingStage.CachedYaw");
            const auto place = shoot::DilemmaAt(yaw, s.minYaw, s.maxYaw, s.middle);
            UObject* highlightedOption = nullptr;
            obj::ReadObject(s.stage, L"HighlightedOption", highlightedOption);
            int highlighted = -1;
            for (const auto& t : s.targets)
            {
                if (highlightedOption && t.option == highlightedOption) highlighted = t.index;
            }
            if (highlighted != s.highlighted)
            {
                const Target* t = TargetAt(s, highlighted);
                log::Info(L"shooting: {}", t ? std::format(L"the game highlights {}", t->label) : std::wstring(L"the game highlights neither side"));
                s.highlighted = highlighted;
            }
            if (place.region != s.dilemmaRegion)
            {
                const Target* t = TargetAt(s, place.region);
                log::Info(L"shooting: the aim stands {} (yaw {:.2f}, {:.2f} of the way from the first side to the second)",
                          t ? L"on the side of " + t->label : std::wstring(L"between the two sides"), yaw, place.t);
                s.dilemmaRegion = place.region;
            }
            const int about = s.chosen >= 0 ? s.chosen : highlighted;
            s.leading = about;
            s.sight = shoot::Sight{};
            if (about >= 0)
            {
                s.sight.placed = true;
                s.sight.onTarget = place.region == about;
                s.sight.solid = s.sight.onTarget;
                s.sight.horizontal = s.sight.onTarget ? 0.0 : (s.invertX ? -1.0 : 1.0) * (about == 0 ? place.toFirst : place.toSecond);
                s.sight.angle = std::fabs(s.sight.horizontal);
            }
            // The word to fire where a press shoots: not in the first turn, which commits by itself.
            if (!s.finishOnHighlight && s.fire.Update(now, highlighted, highlighted >= 0))
            {
                log::Info(L"shooting: the game has highlighted {} for a fifth of a second: the word to fire", highlighted);
                if (cfg::Detailed()) speech::Announce(locale::Mod(L"shoot.fire"));
            }
            if (now - s.loggedAt >= 0.5)
            {
                s.loggedAt = now;
                log::Verbose(L"shooting: aim yaw {:.2f} (the game's), {:.2f} of the way, region {}, {:.1f} of {:.1f} s", yaw, place.t, place.region,
                             FloatAt(s.stage, kSecondsIn), s.timeLimit);
            }
            if (!g_aimSound) return;
            const auto lead = shoot::LeadDilemma(place, s.chosen, highlighted >= 0, s.invertX);
            if (!s.blips.Due(now, lead.blip.locked, lead.interval)) return;
            sounds::Aim(lead.blip.pan, lead.blip.level, lead.blip.locked, lead.blip.muffled);
        }

        void PollImpl()
        {
            const double now = gamethread::NowSeconds();
            Stage& s = g_stage;
            if (!s.on)
            {
                // Its widgets coming up name the moment; a stage that draws none is looked for now
                // and then while the camera stands on an activity's layer.
                const bool seeking = now < g_seekUntil;
                if (seeking ? gamethread::FrameCount() % 5 != 0 : now - g_lookedForAt < kLookEvery) return;
                if (!seeking && (!gamestate::InPlay() || !ActivityCamera())) return;
                g_lookedForAt = now;
                if (UObject* stage = StartedStage())
                {
                    g_seekUntil = -1.0;
                    Begin(stage, seeking ? L"its widgets came up" : L"found running");
                }
                return;
            }
            if (!obj::IsLive(s.stage))
            {
                End(L"the stage is gone");
                return;
            }
            if (Held())
            {
                if (s.heldSince < 0.0)
                {
                    s.heldSince = now;
                    log::Info(L"shooting: {} stands still: {}", s.name, gamestate::Paused() ? L"the game is paused" : L"a screen is over the picture");
                }
                return;
            }
            if (s.heldSince >= 0.0) Resume(s, now);
            if (!Started(s.stage))
            {
                End(L"the stage finished");
                return;
            }
            Guide(s, now);
        }

        void Poll(float)
        {
            obj::SafeInvokeLogged(L"shooting.PollImpl", [](void*) { PollImpl(); }, nullptr);
        }

        // ---- the widgets' events ---------------------------------------------------------------

        void Seek(const wchar_t* what, UObject* widget)
        {
            if (!g_stage.on) log::Verbose(L"shooting: {} {} came up", what, obj::ObjectName(widget));
            // Looked for at once, even while the stage before is still being closed: a stage that
            // follows straight on (the snowball fight's throws) must not wait for the slow look.
            g_seekUntil = gamethread::NowSeconds() + 1.0;
        }

        void OnReticleShown(UObject* self, FFrame&)
        {
            Seek(L"the reticle", self);
        }

        void OnTargetSetUp(UObject* self, FFrame&)
        {
            Seek(L"a target's circle", self);
        }

        void OnTargetHighlighted(UObject* self, FFrame& stack)
        {
            bool on = false;
            params::Bool(stack, L"bInIsHighlighted", on);
            if (g_stage.on) log::Verbose(L"shooting: the circle {} {}", obj::ObjectName(self), on ? L"lights up" : L"goes dark");
        }

        // The game's verdict on each circle as the stage ends: 0 shot, 1 not shot (CleanupUI).
        void OnTargetDestroyed(UObject* self, FFrame& stack)
        {
            int64_t result = -1;
            params::Int(stack, L"InResult", result);
            if (!g_stage.on) return;
            log::Info(L"shooting: the circle {} goes, {}", obj::ObjectName(self), result == 0 ? L"shot" : (result == 1 ? L"not shot" : L"timed out"));
            if (result == 0) g_stage.shotWidget = self;
        }

        // ---- the player's keys ---------------------------------------------------------------

        std::wstring PlaceText(const shoot::Sight& s)
        {
            if (s.onTarget) return locale::Mod(L"shoot.on");
            std::vector<std::wstring> words;
            if (std::fabs(s.horizontal) > 90.0)
                words.push_back(locale::Mod(L"dir8.behind"));
            else if (std::fabs(s.horizontal) >= 2.0)
                words.push_back(locale::Mod(s.horizontal > 0 ? L"dir8.right" : L"dir8.left"));
            if (std::fabs(s.vertical) >= 2.0 && std::fabs(s.horizontal) <= 90.0)
                words.push_back(locale::Mod(s.vertical > 0 ? L"explore.higher" : L"explore.lower"));
            if (words.empty()) words.push_back(locale::Mod(L"dir8.ahead"));
            return str::Join(words, L" ");
        }

        // A target as it is said: its name, its place among the targets at the detailed level,
        // how far and on which side of the aim.
        std::wstring TargetText(const Stage& s, const Target& t, const shoot::Sight& sight)
        {
            std::vector<std::wstring> parts;
            if (s.targets.size() > 1 && cfg::Detailed())
                parts.push_back(locale::Mod(L"shoot.target", {LabelOf(t), std::to_wstring(t.index + 1), std::to_wstring(s.targets.size())}));
            else
                parts.push_back(LabelOf(t));
            if (sight.onTarget)
                parts.push_back(locale::Mod(L"shoot.on"));
            else
                parts.push_back(locale::Mod(L"shoot.where", {std::to_wstring(std::max<long>(1, std::lround(t.metres))), PlaceText(sight)}));
            return str::JoinSentences(parts);
        }

        std::wstring CurrentText()
        {
            const Stage& s = g_stage;
            // An aim-only stage: where the turn it waits for lies.
            if (s.aimingOnly && !s.conditions.empty()) return PlaceText(s.sight);
            const Target* t = TargetAt(s, s.leading);
            // Targets the game hides are not named, nor placed; the dilemma's two sides are.
            if (!t || (s.targetsHidden && !s.dilemma)) return locale::Mod(L"explore.notarget");
            return TargetText(s, *t, s.sight);
        }

        bool OnNext();
        bool OnPrevious();

        bool Step(int direction)
        {
            Stage& s = g_stage;
            if (!ShootingActive()) return false;
            if (s.targets.empty() || (s.targetsHidden && !s.dilemma))
            {
                speech::Now(locale::Mod(L"explore.notarget"));
                return true;
            }
            // The aim held on a target lets go of it: the target chosen now is the one the sound leads
            // to, and the walk key puts the aim on it.
            if (s.locked >= 0)
            {
                log::Info(L"shooting: the aim is no longer held on target {}: another target is being chosen", s.locked);
                s.locked = -1;
            }
            int at = 0;
            const int current = s.chosen >= 0 ? s.chosen : s.leading;
            for (size_t i = 0; i < s.targets.size(); ++i)
            {
                if (s.targets[i].index == current) at = static_cast<int>(i);
            }
            const int count = static_cast<int>(s.targets.size());
            const int next = s.targets.size() == 1 ? 0 : (current < 0 ? (direction > 0 ? 0 : count - 1) : (at + direction + count) % count);
            s.chosen = s.targets[static_cast<size_t>(next)].index;
            s.leading = s.chosen;
            s.fire.Reset();
            // The sight of the chosen target, now.
            Guide(s, gamethread::NowSeconds());
            log::Info(L"shooting: target {} chosen", s.chosen);
            speech::Now(CurrentText());
            return true;
        }

        bool OnNext()
        {
            return Step(1);
        }

        bool OnPrevious()
        {
            return Step(-1);
        }

        bool OnWhere()
        {
            if (!ShootingActive()) return false;
            speech::Now(CurrentText());
            return true;
        }

        bool OnAimSound()
        {
            if (!ShootingActive()) return false;
            ToggleShootingSound();
            return true;
        }

        // The walk key puts the aim on the target chosen, else on the one the sound leads to, and holds
        // it there (HoldAim); nothing is said: the double ping says the game would take the shot, and
        // the shot is the player's to fire. The dilemma is left as it is: its aim is the choice.
        bool OnSnap()
        {
            Stage& s = g_stage;
            if (!ShootingActive() || s.dilemma) return false;
            const int index = s.chosen >= 0 ? s.chosen : s.leading;
            Target* t = nullptr;
            for (auto& x : s.targets)
            {
                if (x.index == index) t = &x;
            }
            // Not while the fire key is held: the shot would take whatever the aim is put on.
            if (s.snaps && t && !shoot::SnapNow(input::ActionWorked(L"IA_SequenceShoot", kAimDeadZone).value_or(false)))
            {
                log::Info(L"shooting: the walk key puts the aim on nothing while the fire key is held");
                speech::Now(locale::Mod(L"shoot.snap.release", input::KeyForAction(L"IA_SequenceShoot")));
                return true;
            }
            shoot::AimAngles angles;
            if (!s.snaps || !t || !PutAim(s, *t, &angles))
            {
                log::Info(L"shooting: the walk key puts the aim on nothing: {}", !s.snaps ? L"the stage takes no aim put on a target" : L"no target placed");
                speech::Now(locale::Mod(L"explore.notarget"));
                return true;
            }
            s.locked = t->index;
            s.fire.Reset();
            log::Info(L"shooting: the aim is put on target {} \"{}\" (yaw {:.2f}, pitch {:.2f}) and held there until the player aims herself", t->index, t->label,
                      angles.yaw, angles.pitch);
            return true;
        }
    }

    void ShootingFeature::Install()
    {
        g_aimSound = cfg::Get().aimSound;
        using hotkeys::Command;
        hotkeys::AddContext(&ShootingActive);
        hotkeys::OnCommand(Command::NextTarget, &OnNext);
        hotkeys::OnCommand(Command::PreviousTarget, &OnPrevious);
        hotkeys::OnCommand(Command::Where, &OnWhere);
        hotkeys::OnCommand(Command::Beacon, &OnAimSound);
        hotkeys::OnCommand(Command::Walk, &OnSnap);
        hooks::OnScript(L"BatesShootingStageReticuleWidget", L"Show", &OnReticleShown);
        hooks::OnScript(L"BatesShootingStageTargetWidget", L"Setup", &OnTargetSetUp);
        hooks::OnScript(L"BatesShootingStageTargetWidget", L"SetHighlighted", &OnTargetHighlighted);
        hooks::OnScript(L"BatesShootingStageTargetWidget", L"Destroy", &OnTargetDestroyed);
        gamethread::AddPoller(L"shooting", &Poll);
    }

    void ShootingFeature::Describe(std::vector<std::wstring>& out)
    {
        if (!ShootingActive()) return;
        out.push_back(locale::Mod(L"shoot.aim"));
        if (!g_stage.targetsHidden) out.push_back(CurrentText());
    }

    void ShootingFeature::Help(std::vector<std::wstring>& out)
    {
        if (!ShootingActive()) return;
        using hotkeys::Command;
        const bool pad = input::CurrentScheme() == input::Scheme::Gamepad;
        // The death room's dilemma: how the aim makes the choice, and the shot after it.
        if (g_stage.dilemma && g_stage.targets.size() == 2)
        {
            const auto& left = g_stage.targets[g_stage.invertX ? 1 : 0].label;
            const auto& right = g_stage.targets[g_stage.invertX ? 0 : 1].label;
            out.push_back(locale::Mod(pad ? L"help.dilemma.pad" : L"help.dilemma", {left, right, input::KeyForAction(L"IA_SequenceShoot")}));
            return;
        }
        out.push_back(locale::Mod(pad ? L"help.shoot.pad" : L"help.shoot",
                                  {input::KeyForAction(L"IA_SequenceShoot"), hotkeys::SpokenKey(Command::NextTarget),
                                   hotkeys::SpokenKey(Command::PreviousTarget), hotkeys::SpokenKey(Command::Where), hotkeys::SpokenKey(Command::Beacon)}));
        if (g_stage.snaps) out.push_back(locale::Mod(L"help.shoot.snap", {hotkeys::SpokenKey(Command::Walk), input::KeyForAction(L"IA_SequenceShoot")}));
    }

    bool ShootingActive()
    {
        return g_stage.on && !Held();
    }

    bool ShootingSoundOn()
    {
        return g_aimSound;
    }

    void ToggleShootingSound()
    {
        g_aimSound = !g_aimSound;
        log::Info(L"shooting: aim sound {}", g_aimSound ? L"on" : L"off");
        speech::Now(locale::Mod(g_aimSound ? L"shoot.sound.on" : L"shoot.sound.off"));
        if (!cfg::Persist(L"Shooting", L"AimSound", g_aimSound ? L"1" : L"0")) log::Error(L"shooting: the aim sound setting could not be saved");
    }
}
