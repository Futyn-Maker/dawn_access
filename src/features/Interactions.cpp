#include "features/Interactions.hpp"

#include "features/Exploration.hpp"
#include "features/NudgeRules.hpp"
#include "features/PromptRules.hpp"
#include "features/Hud.hpp"
#include "features/Prompts.hpp"
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

#include <cmath>
#include <cstring>
#include <format>
#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace da::features
{
    using RC::Unreal::FProperty;
    using RC::Unreal::UClass;
    using RC::Unreal::UObject;

    namespace
    {
        using namespace world;

        // The interaction the player's character is bound to, asked of its bindable component
        // (UBatesBindableComponent::GetBoundActorAsInteraction), however the interaction began: by
        // the player at a use location, or by the scene cutting to it.
        UObject* g_interaction = nullptr;
        obj::ObjectKey g_interactionKey;
        int64_t g_bindings = 0; // counts the interactions bound, so that a stage of the next is another stage

        UObject* BoundInteraction()
        {
            UObject* pawn = Pawn();
            UObject* bindable = nullptr;
            if (!pawn || !obj::ReadObject(pawn, L"BindableCharacterComponent", bindable) || !obj::IsLive(bindable)) return nullptr;
            UObject* bound = nullptr;
            obj::CallReturn(bindable, L"GetBoundActorAsInteraction", [&](void* params, FProperty* ret) { obj::ReadObjectAt(params, ret, bound); });
            return obj::IsLive(bound) ? bound : nullptr;
        }

        // The anim instances an interaction drives (AnimInstanceData.AnimInstances): its anim graphs
        // decide its stages, and it hands them its measure of the view every frame.
        std::vector<UObject*> AnimInstances(UObject* interaction)
        {
            std::vector<UObject*> out;
            auto* data = obj::FindProperty(interaction, L"AnimInstanceData");
            if (!data) return out;
            obj::ForEachArrayElement(obj::ValuePtr(interaction, data), obj::StructMember(data, L"AnimInstances"),
                                     [&](void* element, FProperty* inner)
                                     {
                                         UObject* instance = nullptr;
                                         if (obj::ReadObjectAt(element, inner, instance) && obj::IsLive(instance)) out.push_back(instance);
                                     });
            return out;
        }

        // ---- a framing to rest the view on ----------------------------------------------------

        // The box an anim template tests the view against, by the names its variables go by (the
        // telescope's template: YawMin...; the selfie's: MinYaw...).
        bool ReadBox(UObject* instance, nudge::Box& box)
        {
            static const wchar_t* const kNames[][4] = {{L"YawMin", L"YawMax", L"PitchMin", L"PitchMax"}, {L"MinYaw", L"MaxYaw", L"MinPitch", L"MaxPitch"}};
            for (const auto& names : kNames)
            {
                double v[4] = {};
                bool all = true;
                for (int i = 0; i < 4 && all; ++i)
                    all = obj::FindProperty(instance, names[i]) && obj::ReadFloat(instance, names[i], v[i]);
                if (!all) continue;
                box = nudge::Box{v[0], v[1], v[2], v[3]};
                return nudge::Valid(box);
            }
            return false;
        }

        // A control parameter the interaction hands its anim instances (CameraYaw, CameraPitch).
        bool Parameter(UObject* instance, const wchar_t* name, double& out)
        {
            auto* fn = obj::FindFunction(instance, L"GetLocalStateControlParameterValue");
            if (!fn) return false;
            bool ok = false;
            obj::Call(
                instance, fn,
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"LocalStateName")
                            std::construct_at(reinterpret_cast<RC::Unreal::FName*>(obj::ValuePtrAt(params, prop)), name, RC::Unreal::FNAME_Add);
                    }
                },
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"ReturnValue") ok = obj::ReadFloatAt(params, prop, out);
                    }
                });
            return ok;
        }

        // The nudge's limits in degrees: the camera modifier the interaction lays over the scene's
        // camera, among the camera manager's modifiers (MaxNudgeAngleDegrees over NudgeSensitivity).
        bool NudgeLimits(double& maxYaw, double& maxPitch)
        {
            UObject* controller = Controller();
            UObject* camera = nullptr;
            std::vector<UObject*> modifiers;
            if (!controller || !obj::ReadObject(controller, L"PlayerCameraManager", camera) || !obj::IsLive(camera) ||
                !obj::ReadObjectArray(camera, L"ModifierList", modifiers))
                return false;
            for (UObject* modifier : modifiers)
            {
                if (!obj::IsLive(modifier) || !obj::IsA(modifier, L"BatesNudge_CameraModifier") || obj::CallForBool(modifier, L"IsDisabled")) continue;
                auto* prop = obj::FindProperty(modifier, L"MaxNudgeAngleDegrees");
                void* limits = prop ? obj::ValuePtr(modifier, prop) : nullptr;
                double x = 0.0;
                double y = 0.0;
                if (!limits || !obj::ReadFloatAt(limits, obj::StructMember(prop, L"X"), x) || !obj::ReadFloatAt(limits, obj::StructMember(prop, L"Y"), y))
                    continue;
                double sensitivity = 1.0;
                obj::ReadFloat(modifier, L"NudgeSensitivity", sensitivity);
                if (sensitivity <= 0.0 || x <= 0.0 || y <= 0.0) continue;
                maxYaw = x / sensitivity;
                maxPitch = y / sensitivity;
                return true;
            }
            return false;
        }

        // The view led to a framing: while the interaction is in its first stage (the telescope's
        // search; the selfie's one stage), its input is live, and its anim graph names a box. The
        // telescope's later stage that waits for the view to leave the framing gets no lead: its own
        // box lies inside the one led to.
        struct Framing
        {
            bool on = false;
            UObject* holder = nullptr; // the anim instance whose template names the box
            nudge::Box box;
            double maxYaw = 0.0; // the nudge's limits, read once its modifier is up
            double maxPitch = 0.0;
            nudge::Lead lead;
            bool inside = false;
            shoot::Cadence blips;
            double loggedAt = 0.0;
        };
        Framing g_framing;
        double g_framingLookedAt = -100.0;
        // The press the framing was waiting for has been made (the selfie's shot is judged at the
        // press): nothing more is led in this interaction.
        bool g_framingTaken = false;

        void StopFraming(const wchar_t* why)
        {
            if (!g_framing.on) return;
            log::Info(L"interactions: the framing is no longer led: {}", why);
            g_framing = Framing{};
        }

        void PollFraming(UObject* interaction, double now)
        {
            int64_t stage = -1;
            obj::ReadInt(interaction, L"CurrentStage", stage);
            const auto* blockers = static_cast<const uint8_t*>(sym::MemberOf(interaction, L"ABatesInteractionBase.InputBlockers"));
            const bool inputLive = blockers && *blockers == 0;
            if (g_framingTaken) return;
            if (stage != 0 || !inputLive)
            {
                StopFraming(stage != 0 ? L"the interaction moved on" : L"its input is held");
                return;
            }
            if (!g_framing.on)
            {
                if (now - g_framingLookedAt < 0.5) return;
                g_framingLookedAt = now;
                for (UObject* instance : AnimInstances(interaction))
                {
                    nudge::Box box;
                    if (!ReadBox(instance, box)) continue;
                    g_framing.on = true;
                    g_framing.holder = instance;
                    g_framing.box = box;
                    log::Info(L"interactions: {} waits for the view to rest on a framing: CameraYaw {:.2f} to {:.2f}, CameraPitch {:.2f} to {:.2f} ({})",
                              obj::ObjectName(interaction), box.yawMin, box.yawMax, box.pitchMin, box.pitchMax, obj::ClassName(instance));
                    break;
                }
                if (!g_framing.on) return;
            }
            Framing& f = g_framing;
            if (!obj::IsLive(f.holder))
            {
                StopFraming(L"its anim instance went");
                return;
            }
            bool pressed = false;
            if (obj::ReadBool(f.holder, L"bIsInputPressed", pressed) && pressed)
            {
                g_framingTaken = true;
                StopFraming(L"the press was made, and the game has judged the framing");
                return;
            }
            if (f.maxYaw <= 0.0 && !NudgeLimits(f.maxYaw, f.maxPitch)) return;
            nudge::View view;
            view.maxYaw = f.maxYaw;
            view.maxPitch = f.maxPitch;
            if (!Parameter(f.holder, L"CameraYaw", view.yaw) || !Parameter(f.holder, L"CameraPitch", view.pitch)) return;
            f.lead = nudge::Toward(view, f.box);
            if (f.lead.sight.onTarget != f.inside)
            {
                f.inside = f.lead.sight.onTarget;
                log::Info(L"interactions: the view {} the framing (CameraYaw {:.2f}, CameraPitch {:.2f})", f.inside ? L"rests on" : L"leaves", view.yaw,
                          view.pitch);
            }
            if (now - f.loggedAt >= 0.5)
            {
                f.loggedAt = now;
                log::Verbose(L"interactions: the framing's middle {:.1f} deg {}, {:.1f} deg {}; still to turn {}{} (CameraYaw {:.2f}, CameraPitch {:.2f}, "
                             L"limits {:.1f} by "
                             L"{:.1f} deg)",
                             std::fabs(f.lead.sight.horizontal), f.lead.sight.horizontal >= 0 ? L"right" : L"left", std::fabs(f.lead.sight.vertical),
                             f.lead.sight.vertical >= 0 ? L"up" : L"down", f.lead.turnRight > 0 ? L"right " : (f.lead.turnRight < 0 ? L"left " : L""),
                             f.lead.turnUp > 0 ? L"up" : (f.lead.turnUp < 0 ? L"down" : L""), view.yaw, view.pitch, view.maxYaw, view.maxPitch);
            }
            if (!LookBeaconOn() || !f.blips.Due(now, f.lead.sight.onTarget, nudge::BlipInterval(f.lead))) return;
            const auto blip = nudge::BlipOf(f.lead);
            sounds::Aim(blip.pan, blip.level, blip.locked, blip.muffled);
        }

        // ---- an item turned to its reveal ---------------------------------------------------------

        // An examine stage (UBatesInteractionStageExamine) measures the turn of the item in hand
        // along one axis of the stick, and one that reveals moves the item's anim graph to its
        // reveal at RevealThreshold (a document's other side, a clue), while the game's prompt
        // names the stick both ways and says nothing of how far. The turn is led to that point
        // (nudge::TurnCourse), in the stage's own measure.
        std::vector<UObject*> g_examineStages; // the interaction's examine stages, found once
        nudge::TurnCourse g_turnCourse;
        nudge::Turn g_turn;
        nudge::TurnLead g_turnLead;
        shoot::Cadence g_turnBlips;
        int g_turnGraph = -1; // whether the item's graph reads the reveal: -1 not known yet
        double g_turnGraphAskedAt = -100.0;
        bool g_turnUnread = false; // said once: the stage's measure cannot be read in this build
        double g_turnLoggedAt = 0.0;
        // Whether an anim class reads the reveal, asked once of each class.
        std::map<obj::ObjectKey, bool> g_graphReads;

        void FindExamineStages(UObject* interaction)
        {
            g_examineStages.clear();
            std::vector<UObject*> components;
            const bool listed = obj::ReadObjectArray(interaction, L"BlueprintCreatedComponents", components);
            for (UObject* component : components)
            {
                if (obj::IsLive(component) && obj::IsA(component, L"BatesInteractionStageExamine")) g_examineStages.push_back(component);
            }
            // Should the actor's list of its components not be read, every live examine stage it owns.
            if (!listed)
            {
                for (UObject* stage : obj::FindAllLive(L"BatesInteractionStageExamine"))
                {
                    if (obj::Outer(stage) == interaction) g_examineStages.push_back(stage);
                }
            }
            for (UObject* stage : g_examineStages)
            {
                bool reveals = false;
                double threshold = 0.0;
                int64_t index = -1;
                obj::ReadBool(stage, L"bUsesReveal", reveals);
                obj::ReadFloat(stage, L"RevealThreshold", threshold);
                obj::ReadInt(stage, L"Stage", index);
                log::Info(L"interactions: {} examines in its stage {} ({}){}", obj::ObjectName(interaction), index + 1, obj::ObjectName(stage),
                          reveals ? std::format(L", which reveals at {:.2f} of the turn", threshold) : std::wstring(L", which reveals nothing"));
            }
        }

        // ---- a stage on a clock ----------------------------------------------------------------

        // The stages of the interaction that run on a clock (UBatesInteractionStageBase's
        // bStageCanTimeout and SecondsUntilTimeout), by their index, found once on binding. A
        // sighted player sees the countdown the game draws; the Timer cue sounds once as such a
        // stage begins: Jessica's phone in chapter 3 gives 5 s, a lever later on decides who lives
        // in 30 (research/mechanics/gaps.md).
        struct TimedStage
        {
            double seconds = 0.0;
            obj::ObjectKey component;
        };
        std::map<int64_t, TimedStage> g_timedStages;
        int64_t g_timedCued = -1;    // the stage the cue sounded for
        int64_t g_timedOutSaid = -1; // the stage whose running out was said

        void FindTimedStages(UObject* interaction)
        {
            g_timedStages.clear();
            g_timedCued = -1;
            g_timedOutSaid = -1;
            std::vector<UObject*> components;
            obj::ReadObjectArray(interaction, L"BlueprintCreatedComponents", components);
            for (UObject* component : components)
            {
                if (!obj::IsLive(component) || !obj::IsA(component, L"BatesInteractionStageBase")) continue;
                bool timed = false;
                double seconds = 0.0;
                int64_t index = -1;
                obj::ReadBool(component, L"bStageCanTimeout", timed);
                obj::ReadFloat(component, L"SecondsUntilTimeout", seconds);
                obj::ReadInt(component, L"Stage", index);
                if (!timed || seconds <= 0.0) continue;
                g_timedStages[index] = TimedStage{seconds, obj::KeyOf(component)};
                log::Info(L"interactions: {} runs its stage {} ({}) on a clock of {:.1f} s", obj::ObjectName(interaction), index + 1,
                          obj::ObjectName(component), seconds);
            }
        }

        // The stage the interaction is in, when it runs on a clock: its seconds, else 0.
        double TimedStageSeconds(UObject* interaction)
        {
            int64_t current = -1;
            if (g_timedStages.empty() || !obj::IsLive(interaction) || !obj::ReadInt(interaction, L"CurrentStage", current)) return 0.0;
            const auto it = g_timedStages.find(current);
            return it == g_timedStages.end() ? 0.0 : it->second.seconds;
        }

        // A stage on a clock sounds the Timer cue as it begins, with the word at the detailed level;
        // the clock running out while the stage still stands is said as every clock that runs out is
        // (the Fail cue, "Time's up."), whatever running out leads to: whether letting it run is the
        // answer the scene wanted is not the mod's to say (Andrey, 2026-11-06). The stage counts its
        // own time (TimeSpentInStage) against SecondsUntilTimeout.
        // The timed stage last seen standing, to be asked once more as it goes: a clock that runs out
        // may end its stage, or the interaction, before a look of the mod's sees it run out.
        int64_t g_timedWatched = -1;
        // The moment a stage's count passed its seconds, and the states its graphs stood in then
        // (prompt::TimeoutTaken): watched for a moment for a graph taking the timeout.
        double g_limitAt = -1.0;
        int64_t g_limitStage = -1;
        std::vector<std::wstring> g_statesAtLimit;
        constexpr double kTimeoutTakenWithin = 1.5;
        std::vector<std::wstring> CurrentStates(UObject* interaction);

        void SayRanOut(const wchar_t* how)
        {
            g_timedOutSaid = g_limitStage;
            log::Info(L"interactions: the stage's clock ran out, {}", how);
            sounds::Play(sounds::Cue::Fail);
            if (cfg::Detailed()) speech::Announce(locale::Mod(L"result.timeout"));
        }

        // Whether the stage's clock has run out as its answer; said once a stage.
        void CheckRanOut(int64_t index, UObject* interaction, double now)
        {
            const auto it = g_timedStages.find(index);
            if (index < 0 || index == g_timedOutSaid || it == g_timedStages.end() || !obj::IsSame(it->second.component)) return;
            if (g_limitStage != index)
            {
                const auto* spent = static_cast<const float*>(sym::MemberOf(it->second.component.object, L"UBatesInteractionStageBase.TimeSpentInStage"));
                if (!spent || *spent <= it->second.seconds) return;
                g_limitStage = index;
                g_limitAt = now;
                g_statesAtLimit = interaction ? CurrentStates(interaction) : std::vector<std::wstring>{};
                log::Info(L"interactions: the stage's count passes its {:.1f} s, its graphs in {}", it->second.seconds,
                          g_statesAtLimit.empty() ? std::wstring(L"no state") : str::Join(g_statesAtLimit, L", "));
                return;
            }
            if (now - g_limitAt > kTimeoutTakenWithin) return;
            const auto states = interaction ? CurrentStates(interaction) : std::vector<std::wstring>{};
            if (prompt::TimeoutTaken(g_statesAtLimit, states)) SayRanOut(std::format(L"its graphs moving on to {}", str::Join(states, L", ")).c_str());
        }

        void PollTimedStage(UObject* interaction)
        {
            int64_t current = -1;
            if (g_timedStages.empty() || !obj::ReadInt(interaction, L"CurrentStage", current)) return;
            const double now = gamethread::NowSeconds();
            // A stage that went on as its clock ran out out of a waiting state ran out.
            if (g_timedWatched >= 0 && g_timedWatched != current && g_limitStage == g_timedWatched && g_timedOutSaid != g_timedWatched &&
                now - g_limitAt <= kTimeoutTakenWithin && std::any_of(g_statesAtLimit.begin(), g_statesAtLimit.end(), prompt::WaitingState))
                SayRanOut(L"its stage going on");
            const auto it = g_timedStages.find(current);
            g_timedWatched = it == g_timedStages.end() ? -1 : current;
            if (it == g_timedStages.end()) return;
            if (current != g_timedCued)
            {
                g_timedCued = current;
                log::Info(L"interactions: the stage on a clock begins: {:.1f} s", it->second.seconds);
                sounds::Play(sounds::Cue::Timer);
                if (cfg::Detailed()) speech::Announce(locale::Mod(L"cue.timer"));
            }
            if (gamethread::FrameCount() % 6 == 3) CheckRanOut(current, interaction, now);
        }

        // ---- the states its anim graphs stand in -------------------------------------------------

        // The indices UAnimInstance::GetCurrentStateName takes for an anim graph's state machines,
        // from its class's nodes (prompt::MachineIndices).
        std::vector<int> MachineIndices(UObject* instance)
        {
            UClass* cls = instance ? instance->GetClassPrivate() : nullptr;
            if (!cls || obj::ClassName(static_cast<UObject*>(cls)) != L"AnimBlueprintGeneratedClass") return {};
            const auto* nodes = static_cast<const uint8_t*>(sym::MemberOf(cls, L"UAnimBlueprintGeneratedClass.AnimNodeProperties"));
            if (!nodes) return {};
            auto* const* data = *reinterpret_cast<FProperty* const* const*>(nodes);
            const int32_t count = *reinterpret_cast<const int32_t*>(nodes + 8);
            if (!data || count <= 0 || count > 4096) return {};
            std::vector<bool> machine(static_cast<size_t>(count), false);
            for (int32_t j = 0; j < count; ++j)
            {
                FProperty* node = data[j];
                if (!node || obj::PropertyTypeName(node) != L"StructProperty") continue;
                RC::Unreal::UScriptStruct* type = static_cast<RC::Unreal::FStructProperty*>(node)->GetStruct();
                machine[static_cast<size_t>(j)] = type && type->GetName() == L"AnimNode_StateMachine";
            }
            return prompt::MachineIndices(machine);
        }

        // The states the interaction's anim graphs stand in now: the current state of each of their
        // state machines (UAnimInstance::GetCurrentStateName).
        std::vector<std::wstring> CurrentStates(UObject* interaction)
        {
            std::vector<std::wstring> out;
            for (UObject* instance : AnimInstances(interaction))
            {
                auto* fn = obj::FindFunction(instance, L"GetCurrentStateName");
                if (!fn) continue;
                for (const int machine : MachineIndices(instance))
                {
                    std::wstring name;
                    obj::Call(
                        instance, fn,
                        [&](void* params)
                        {
                            for (auto* prop : fn->ForEachProperty())
                            {
                                if (prop && prop->GetName() == L"MachineIndex") *static_cast<int32_t*>(obj::ValuePtrAt(params, prop)) = machine;
                            }
                        },
                        [&](void* params)
                        {
                            for (auto* prop : fn->ForEachProperty())
                            {
                                if (prop && prop->GetName() == L"ReturnValue") obj::ReadStringAt(params, prop, name);
                            }
                        });
                    if (!name.empty() && name != L"None") out.push_back(name);
                }
            }
            return out;
        }

        std::wstring g_statesSeen;
        double g_statesSince = -1.0;
        bool g_promptRepeated = false;
        bool g_sidesSaid = false; // the sides of a lever whose side is the choice (prompt::SidesOf)

        // The staged climbs and shimmies of the interaction in use, by stage: the one-way action that
        // goes on (prompt::LinearForward); the stage last seen current, since when, and whether its
        // way on has been said.
        std::map<int64_t, std::wstring> g_linearForward;
        std::map<int64_t, std::wstring> g_linearBack; // and its way back, named where the stage stands past its first step
        std::map<int64_t, int64_t> g_linearInitial;   // the step each stage begins at (InitialAnimationStage)
        int64_t g_linearStage = -1;
        double g_linearSince = -1.0;
        bool g_linearSaid = false;
        // Each staged stage's last step (MaxAnimationStage where the stage clamps its steps, else its
        // graph's: prompt::LastComparedStep), and the step sounded as it changes.
        std::map<int64_t, int64_t> g_linearLast;
        // The staged stages that keep no last step of their own, to be asked of their graph once the
        // graphs are up: whether each has been asked, and the MaxAnimationStage it names all the same
        // (taken where no graph compares the step, as the stage was read before).
        struct Unclamped
        {
            bool asked = false;
            int64_t named = 0;
        };
        std::map<int64_t, Unclamped> g_linearUnclamped;
        prompt::LinearProgress g_linearProgress;
        constexpr double kLinearAfterPrompt = 1.0; // the game's own prompt comes up first

        void FindLinearStages(UObject* interaction)
        {
            g_linearForward.clear();
            g_linearBack.clear();
            g_linearInitial.clear();
            g_linearLast.clear();
            g_linearUnclamped.clear();
            g_linearProgress.Reset();
            g_linearStage = -1;
            g_linearSaid = false;
            std::vector<UObject*> components;
            obj::ReadObjectArray(interaction, L"BlueprintCreatedComponents", components);
            for (UObject* component : components)
            {
                if (!obj::IsLive(component) || !obj::IsA(component, L"BatesInteractionLinearStaged")) continue;
                UObject* action = nullptr;
                bool invert = false;
                bool clamp = false;
                int64_t index = -1;
                obj::ReadObject(component, L"InputActionAssigned", action);
                obj::ReadBool(component, L"bInvertInput", invert);
                obj::ReadBool(component, L"bClampAnimationStage", clamp);
                obj::ReadInt(component, L"Stage", index);
                int64_t last = 0;
                obj::ReadInt(component, L"MaxAnimationStage", last);
                // MaxAnimationStage holds the steps only where the stage clamps them
                // (UBatesInteractionLinearStaged::OnIncreaseAnimationStage); else the graph ends them.
                if (clamp && last > 0)
                    g_linearLast[index] = *prompt::LastStep(true, last, std::nullopt);
                else
                    g_linearUnclamped[index] = Unclamped{false, last};
                const std::wstring name = obj::IsLive(action) ? obj::ObjectName(action) : std::wstring();
                const auto forward = prompt::LinearForward(name, invert);
                log::Info(L"interactions: {} has a staged stage {} ({}) on {}{}{}{}", obj::ObjectName(interaction), index + 1, obj::ObjectName(component),
                          name.empty() ? L"no action" : name, invert ? L", inverted" : L"", forward.empty() ? std::wstring() : L": it goes on with " + forward,
                          clamp && last > 0 ? std::format(L"; its steps run to {}", last) : std::wstring(L"; it keeps no last step, its graph ends it"));
                if (!forward.empty())
                {
                    g_linearForward[index] = forward;
                    g_linearBack[index] = prompt::LinearForward(name, !invert);
                    int64_t initial = 0;
                    obj::ReadInt(component, L"InitialAnimationStage", initial);
                    g_linearInitial[index] = initial;
                }
            }
        }

        // The last step the item's graph takes (prompt::LastComparedStep): the largest number its
        // compiled script compares the step it is handed with, in any of the Blueprint classes of the
        // anim instances the stage writes the step into; none where no graph compares it.
        std::optional<int64_t> GraphLastStep(UObject* interaction, std::wstring& from)
        {
            std::optional<int64_t> last;
            for (UObject* instance : AnimInstances(interaction))
            {
                if (!obj::IsA(instance, L"BatesIntSimpleGrabAnimInst")) continue;
                FProperty* property = obj::FindProperty(instance, L"AnimationStage");
                if (!property) continue;
                for (UClass* cls = instance->GetClassPrivate(); cls && !cls->HasAnyClassFlags(RC::Unreal::CLASS_Native); cls = cls->GetSuperClass())
                {
                    for (RC::Unreal::UFunction* fn : cls->ForEachFunction())
                    {
                        if (!fn) continue;
                        const auto& script = fn->GetScript();
                        const auto step = prompt::LastComparedStep(reinterpret_cast<const uint8_t*>(script.GetData()), static_cast<size_t>(script.Num()),
                                                                   static_cast<const void*>(property));
                        if (!step || (last && *last >= *step)) continue;
                        last = step;
                        from = obj::ClassName(cls);
                    }
                }
            }
            return last;
        }

        // The step a staged stage stands at, as the stage hands it to the anim graphs: its step on or
        // back (UBatesInteractionLinearStaged::OnIncreaseAnimationStage, OnDecreaseAnimationStage) is
        // written by ApplyAnimationStage into every one of the interaction's anim instances that is a
        // UBatesIntSimpleGrabAnimInst (AnimationStage, 0x5B8), the complex graphs of the diary, the
        // sketchbook and the tower's ladders among them. The like-named member of
        // UBatesInteractionLinStageAnimInst is not written there: read from it, the diary of
        // 2026-11-08 sounded no step at all (research/sessions/s58-whole.log, 11:22 to 11:25).
        int64_t LinearStep(UObject* interaction)
        {
            for (UObject* instance : AnimInstances(interaction))
            {
                int64_t step = -1;
                if (obj::IsA(instance, L"BatesIntSimpleGrabAnimInst") && obj::ReadInt(instance, L"AnimationStage", step)) return step;
            }
            return -1;
        }

        // Each step of a staged stage as a pitch, and the way on of a staged climb or shimmy whose
        // input has two ways, said once its stage is the current one and the game's prompt has come up.
        void PollLinear(UObject* interaction, double now)
        {
            if (g_linearForward.empty() && g_linearLast.empty() && g_linearUnclamped.empty()) return;
            int64_t current = -1;
            if (!obj::ReadInt(interaction, L"CurrentStage", current)) return;
            if (current != g_linearStage)
            {
                g_linearStage = current;
                g_linearSince = now;
                g_linearSaid = false;
                g_linearProgress.Reset();
            }
            // A stage that keeps no last step: its graph's, asked once the stage stands and its graphs
            // are up.
            if (const auto open = g_linearUnclamped.find(current); open != g_linearUnclamped.end() && !open->second.asked && LinearStep(interaction) >= 0)
            {
                open->second.asked = true;
                std::wstring from;
                const auto graph = GraphLastStep(interaction, from);
                if (const auto last = prompt::LastStep(false, open->second.named, graph))
                {
                    g_linearLast[current] = *last;
                    if (graph && *graph == *last)
                        log::Info(L"interactions: the staged stage {}'s steps run to {}, the last its graph {} takes", current + 1, *last, from);
                    else
                        log::Info(L"interactions: the staged stage {}'s steps run to {}, its MaxAnimationStage, no graph of it comparing the step",
                                  current + 1, *last);
                }
                else
                {
                    log::Info(L"interactions: the staged stage {} keeps no last step and no graph of it compares the step with a number: no step sounds",
                              current + 1);
                }
            }
            // Each step on or back, as a pitch (prompt::LinearProgress).
            if (const auto last = g_linearLast.find(current); last != g_linearLast.end())
            {
                const int64_t step = LinearStep(interaction);
                if (const auto level = g_linearProgress.Update(step, last->second))
                {
                    sounds::Tick(*level);
                    log::Verbose(L"interactions: the staged stage {} stands at step {} of {}", current + 1, step, last->second);
                }
            }
            if (g_linearForward.empty()) return;
            const auto forward = g_linearForward.find(current);
            if (forward == g_linearForward.end() || g_linearSaid || now - g_linearSince < kLinearAfterPrompt) return;
            // After the game's own words: the page and its card the HUD is still to say, then the
            // stage's prompt (at most kLinearAfterPrompt more for a prompt that does not come).
            if (hud::TextSettling(now)) return;
            if (LastInteractionPromptAt() < g_linearSince && now - g_linearSince < 2.0 * kLinearAfterPrompt) return;
            g_linearSaid = true;
            const auto key = input::KeyForAction(forward->second);
            // Standing past its first step, the stage's way back leads somewhere too (prompt::NamesWayBack).
            const int64_t step = LinearStep(interaction);
            const auto back = g_linearBack.find(current);
            const auto initial = g_linearInitial.find(current);
            const auto backKey = back != g_linearBack.end() && prompt::NamesWayBack(step, initial != g_linearInitial.end() ? initial->second : 0)
                                     ? input::KeyForAction(back->second)
                                     : std::wstring();
            log::Info(L"interactions: the stage goes on with {} ({}){}", forward->second, key,
                      backKey.empty() ? std::wstring() : std::format(L"; it stands at step {}, and {} ({}) goes back", step, back->second, backKey));
            if (key.empty()) return;
            auto line = locale::Mod(L"interact.linear.forward", key);
            if (!backKey.empty()) line += L" " + locale::Mod(L"interact.linear.back", backKey);
            speech::Announce(line);
        }

        // What the states say that nothing else does. A fire laid with no matches to light it plays
        // its no_matches state, the character's empty hands, and the scene waits for the fire; and an
        // idle the interaction comes back to after a step takes the stage's input again with no prompt
        // drawn (prompt::IdleAfterStep): its last prompt is said once more, two seconds on.
        void PollStates(UObject* interaction, double now)
        {
            const auto states = CurrentStates(interaction);
            const auto joined = str::Join(states, L", ");
            if (joined != g_statesSeen)
            {
                g_statesSeen = joined;
                g_statesSince = now;
                g_promptRepeated = false;
                log::Verbose(L"interactions: {} stands in {}", obj::ObjectName(interaction), joined.empty() ? L"no state" : joined);
                if (std::find(states.begin(), states.end(), L"no_matches") != states.end())
                {
                    log::Info(L"interactions: {} has no matches to light it", obj::ObjectName(interaction));
                    speech::Announce(locale::Mod(L"interact.nomatches"));
                }
                // A lever whose side is the choice says the sides as it takes the stick.
                if (const auto sides = prompt::SidesOf(obj::ClassName(interaction));
                    sides && !g_sidesSaid && std::find(states.begin(), states.end(), std::wstring(sides->state)) != states.end())
                {
                    g_sidesSaid = true;
                    const auto left = gametext::Resolve(sides->left);
                    const auto right = gametext::Resolve(sides->right);
                    log::Info(L"interactions: {} takes the stick: left \"{}\", right \"{}\"", obj::ObjectName(interaction), left, right);
                    if (!left.empty() && !right.empty())
                        speech::Announce(locale::Mod(L"dir.left") + L": " + left + L". " + locale::Mod(L"dir.right") + L": " + right + L".");
                }
            }
            const bool idle = std::any_of(states.begin(), states.end(), [](const std::wstring& st) { return prompt::IdleAfterStep(st); });
            if (!idle || g_promptRepeated || now - g_statesSince < 2.0 || LastInteractionPromptAt() >= g_statesSince || InteractionPromptShown()) return;
            const auto again = LastInteractionPrompt();
            if (again.empty()) return;
            g_promptRepeated = true;
            log::Info(L"interactions: {} waits in {} for its input again, no prompt drawn: \"{}\" said again", obj::ObjectName(interaction), joined, again);
            speech::Announce(again);
        }

        // The examine stage of the stage the interaction is in, or null.
        UObject* CurrentExamineStage(UObject* interaction)
        {
            int64_t current = -1;
            if (g_examineStages.empty() || !obj::ReadInt(interaction, L"CurrentStage", current)) return nullptr;
            for (UObject* stage : g_examineStages)
            {
                int64_t own = -2;
                if (obj::IsLive(stage) && obj::ReadInt(stage, L"Stage", own) && own == current) return stage;
            }
            return nullptr;
        }

        // Whether compiled script names a property: a member of the instance is read there by the
        // address of its property (EX_InstanceVariable).
        bool ScriptNames(RC::Unreal::UStruct* code, const void* property)
        {
            const auto& script = code->GetScript();
            const auto* bytes = reinterpret_cast<const uint8_t*>(script.GetData());
            const int32_t size = script.Num();
            for (int32_t i = 0; bytes && i + static_cast<int32_t>(sizeof property) <= size; ++i)
            {
                if (std::memcmp(bytes + i, &property, sizeof property) == 0) return true;
            }
            return false;
        }

        // Whether an anim class's property access copies a member of the instance by its name: a
        // path segment of the library in the class's constant data, which is how the fast path of
        // its transition rules reads the instance.
        bool AccessNames(UClass* cls, std::wstring_view member)
        {
            void* data = cls->GetSparseClassData();
            RC::Unreal::UScriptStruct* layout = cls->GetSparseClassDataStruct().Get();
            if (!data || !layout) return false;
            for (FProperty* extension : layout->ForEachPropertyInChain())
            {
                if (!extension || extension->GetName() != L"AnimBlueprintExtension_PropertyAccess") continue;
                FProperty* library = obj::StructMember(extension, L"Library");
                FProperty* segments = library ? obj::StructMember(library, L"PathSegments") : nullptr;
                if (!segments) return false;
                bool named = false;
                obj::ForEachArrayElement(obj::ValuePtrAt(obj::ValuePtrAt(data, extension), library), segments,
                                         [&](void* element, FProperty* inner)
                                         {
                                             std::wstring segment;
                                             if (!named && obj::ReadStringAt(element, obj::StructMember(inner, L"Name"), segment)) named = segment == member;
                                         });
                return named;
            }
            return false;
        }

        // How an anim graph reads a member of its instance in deciding its states, empty for not at
        // all: in the compiled script of its rules or through their property access, in any of the
        // instance's Blueprint classes down to the native one.
        std::wstring GraphReads(UObject* instance, std::wstring_view member)
        {
            FProperty* property = obj::FindProperty(instance, member);
            if (!property) return {};
            for (UClass* cls = instance->GetClassPrivate(); cls && !cls->HasAnyClassFlags(RC::Unreal::CLASS_Native); cls = cls->GetSuperClass())
            {
                for (RC::Unreal::UFunction* fn : cls->ForEachFunction())
                {
                    if (fn && ScriptNames(fn, property)) return std::format(L"in the script {} of {}", obj::ObjectName(fn), obj::ClassName(cls));
                }
                if (AccessNames(cls, member)) return std::format(L"through the property access of {}", obj::ClassName(cls));
            }
            return {};
        }

        // Whether an anim instance the interaction drives reads the reveal: 1 yes, 0 no, -1 none
        // drives the item yet.
        int GraphReadsReveal(UObject* interaction)
        {
            int verdict = -1;
            for (UObject* instance : AnimInstances(interaction))
            {
                if (!obj::IsA(instance, L"BatesInteractionGrabAnimInst")) continue;
                const auto key = obj::KeyOf(instance->GetClassPrivate());
                auto known = g_graphReads.find(key);
                if (known == g_graphReads.end())
                {
                    const auto how = GraphReads(instance, L"bIsRevealing");
                    log::Info(L"interactions: the anim graph {} {}", obj::ClassName(instance),
                              how.empty() ? std::wstring(L"never reads bIsRevealing: its turn has no end") : L"reads bIsRevealing " + how);
                    known = g_graphReads.emplace(key, !how.empty()).first;
                }
                if (known->second) return 1;
                verdict = 0;
            }
            return verdict;
        }

        // Whether the player's setting turns the stick's axis round for interactions (a modifier of
        // the game's own over the input, so the measure falls where it would rise).
        bool InvertedAxis(bool vertical)
        {
            static UObject* settings = nullptr;
            if (!settings || !obj::IsLive(settings)) settings = obj::FindFirstLive(L"BatesGameUserSettings");
            bool inverted = false;
            if (settings) obj::ReadBool(settings, vertical ? L"bInvertInteractionInputY" : L"bInvertInteractionInputX", inverted);
            return inverted;
        }

        const wchar_t* PushWord(const nudge::TurnLead& l)
        {
            return l.right > 0 ? L"right" : l.right < 0 ? L"left" : l.up > 0 ? L"up" : L"down";
        }

        void PollTurn(UObject* interaction, double now)
        {
            if (g_turnCourse.Revealed()) return;
            UObject* stage = CurrentExamineStage(interaction);
            nudge::TurnState s;
            if (stage)
            {
                obj::ReadBool(stage, L"bUsesReveal", s.usesReveal);
                obj::ReadFloat(stage, L"RevealThreshold", g_turn.threshold);
                const auto* held = static_cast<const bool*>(sym::MemberOf(stage, L"UBatesInteractionStageBase.bIsFullyHolding"));
                const auto* ramping = static_cast<const bool*>(sym::MemberOf(stage, L"UBatesInteractionStageExamine.bIsRampingIK"));
                const auto* rampSpeed = static_cast<const float*>(sym::MemberOf(stage, L"UBatesInteractionStageExamine.IKRampSpeed"));
                const auto* revealing = static_cast<const bool*>(sym::MemberOf(stage, L"UBatesInteractionStageExamine.bIsRevealing"));
                const auto* share = static_cast<const float*>(sym::MemberOf(stage, L"UBatesInteractionStageExamine.NormalisedExamineEvaluation"));
                if (held && ramping && rampSpeed && revealing && share)
                {
                    s.fullyHeld = *held;
                    s.ramping = *ramping && *rampSpeed > 0.0f;
                    s.revealing = *revealing;
                    g_turn.share = *share;
                }
                else if (s.usesReveal && !g_turnUnread)
                {
                    g_turnUnread = true;
                    log::Info(L"interactions: the examine stage's own measure is not known in this build of the game, so its turn is not led");
                }
                // The item's graph is asked once it is in hand, when the anim instances are surely up.
                if (s.usesReveal && s.fullyHeld && g_turnGraph < 0 && now - g_turnGraphAskedAt >= 0.5)
                {
                    g_turnGraphAskedAt = now;
                    g_turnGraph = GraphReadsReveal(interaction);
                }
                s.graphReadsReveal = g_turnGraph == 1;
            }
            switch (g_turnCourse.Update(s))
            {
            case nudge::TurnCourse::Event::Begins:
            {
                int64_t axis = 0;
                // The item's own numbers: the stick's value times InputMultiplier makes the turn's
                // speed, and the speed times EvaluationMultiplier its share (nudge::Turn).
                double inputMultiplier = 1.0;
                double evaluationMultiplier = 1.0;
                auto* data = obj::FindProperty(stage, L"InspectionData");
                if (data)
                {
                    void* inspect = obj::ValuePtr(stage, data);
                    obj::ReadIntAt(inspect, obj::StructMember(data, L"InputAxis"), axis);
                    obj::ReadFloatAt(inspect, obj::StructMember(data, L"InputMultiplier"), inputMultiplier);
                    obj::ReadFloatAt(inspect, obj::StructMember(data, L"EvaluationMultiplier"), evaluationMultiplier);
                }
                g_turn.vertical = axis == 1;
                g_turn.inverted = InvertedAxis(g_turn.vertical);
                g_turn.reversed = inputMultiplier * evaluationMultiplier < 0.0;
                g_turnLead = nudge::Toward(g_turn);
                g_turnBlips.Reset();
                log::Info(L"interactions: the item of {} is in hand; its turn is led {} from {:.2f} to the reveal at {:.2f}, along the stick's {} axis "
                          L"(InputMultiplier {:.2f}, EvaluationMultiplier {:.2f}{}){}",
                          obj::ObjectName(interaction), PushWord(g_turnLead), g_turn.share, g_turn.threshold, g_turn.vertical ? L"Y" : L"X",
                          inputMultiplier, evaluationMultiplier, g_turn.reversed ? L": the item turns against the stick" : L"",
                          g_turn.inverted ? L", which the player's setting turns round" : L"");
                break;
            }
            case nudge::TurnCourse::Event::Reveals:
                log::Info(L"interactions: the item of {} is revealed at {:.2f} of the turn (the point {:.2f})", obj::ObjectName(interaction), g_turn.share,
                          g_turn.threshold);
                if (LookBeaconOn()) sounds::Ping();
                return;
            case nudge::TurnCourse::Event::Ends:
                log::Info(L"interactions: the turn is no longer led at {:.2f}: {}", g_turn.share,
                          !stage ? L"the interaction moved on" : (s.ramping ? L"a ramp of the game's moves the item" : L"the item is out of hand"));
                return;
            case nudge::TurnCourse::Event::None: break;
            }
            if (!g_turnCourse.Led()) return;
            g_turnLead = nudge::Toward(g_turn);
            if (now - g_turnLoggedAt >= 0.5)
            {
                g_turnLoggedAt = now;
                const auto pad = input::ReadPad();
                log::Verbose(L"interactions: the turn stands at {:.2f} of {:.2f}, {}% left, push {}; stick ({}, {})", g_turn.share, g_turn.threshold,
                             nudge::PercentLeft(g_turn), PushWord(g_turnLead), pad.valid ? pad.leftX : 0, pad.valid ? pad.leftY : 0);
            }
            if (!LookBeaconOn() || !g_turnBlips.Due(now, false, nudge::BlipInterval(g_turnLead))) return;
            const auto blip = nudge::BlipOf(g_turnLead);
            sounds::Aim(blip.pan, blip.level, blip.locked, blip.muffled);
        }

        void ResetTurn()
        {
            if (g_turnCourse.Led()) log::Info(L"interactions: the turn is no longer led: the character left the interaction");
            g_examineStages.clear();
            g_turnCourse.Reset();
            g_turn = nudge::Turn{};
            g_turnLead = nudge::TurnLead{};
            g_turnBlips.Reset();
            g_turnGraph = -1;
            g_turnGraphAskedAt = -100.0;
        }

        // ---- a light the press is timed by --------------------------------------------------------

        // A press the game judges by a moment it marks with a global flag: the double ping marks
        // the moment coming, with its word at the detailed level. The boiler
        // (ABatesInteractionComplexFireItUp) judges the press after the turn against the pressure
        // lamp, TimerReadyForButtonPress, and a press while it is dark fails. Chapter 5's Wendigo trap
        // (ABatesInteractionComplexWendigoTrap) takes the grab only while the trap's own animation
        // holds MikeCanGrabWendigoTrap up ("grab when arm is in right position", its scene's script);
        // a grab otherwise misses and is tried again. The grab taken is what springs the trap: its
        // graph has no other way out of reach_idle than reach_to_caught (R2 in the moment), miss (R2
        // outside it) and the back-out, so being caught is the scene's own end of a grab that lands
        // (Andrey caught at the third moment on 2026-11-07, then the open/cut choice three times over).
        struct PressWindow
        {
            const wchar_t* cls;
            const wchar_t* flag;
            const wchar_t* word;
            const wchar_t* what;
        };
        constexpr PressWindow kPressWindows[] = {
            {L"BatesInteractionComplexFireItUp", L"TimerReadyForButtonPress", L"interact.lamp", L"the boiler's lamp"},
            {L"BatesInteractionComplexWendigoTrap", L"MikeCanGrabWendigoTrap", L"interact.now", L"the trap's moment to grab"},
        };
        nudge::Light g_lamp;
        const PressWindow* g_window = nullptr;
        bool g_lampActive = false;

        bool GlobalInt(std::wstring_view name, int64_t& out)
        {
            static UObject* library = nullptr;
            static RC::Unreal::UFunction* fn = nullptr;
            if (!library || !obj::IsLive(library))
            {
                library = obj::FindObject(L"/Script/Bates.Default__BatesGlobalStateBlueprintLibrary");
                fn = library ? obj::FindFunction(library, L"GetGlobalStateControlParameterInt") : nullptr;
            }
            UObject* pawn = Pawn();
            if (!fn || !pawn) return false;
            bool read = false;
            obj::Call(
                library, fn,
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (!prop) continue;
                        const auto pname = prop->GetName();
                        if (pname == L"Context")
                            *static_cast<UObject**>(obj::ValuePtrAt(params, prop)) = pawn;
                        else if (pname == L"GlobalStateName")
                            obj::WriteStringAt(params, prop, name);
                    }
                },
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"ReturnValue") read = obj::ReadIntAt(params, prop, out);
                    }
                });
            return read;
        }

        // A dial the stick turns (prompt::DialOf): its place as last read, and the way to its station,
        // said once the stage asks for the turn.
        std::optional<prompt::Dial> g_dial;
        int64_t g_dialPos = -1;
        double g_dialAskedSince = -1.0;
        bool g_dialWaySaid = false;

        void PollDial(double now)
        {
            if (!g_dial) return;
            int64_t pos = -1;
            if (!GlobalInt(g_dial->state, pos)) return;
            if (pos != g_dialPos)
            {
                if (g_dialPos >= 0)
                {
                    log::Info(L"interactions: the dial turns from {} to {}{}", g_dialPos, pos, pos == g_dial->station ? L": the station" : L"");
                    speech::Announce(locale::Mod(L"interact.dial.position", std::to_wstring(pos)));
                }
                else
                {
                    log::Info(L"interactions: the dial stands at {}, the station at {}", pos, g_dial->station);
                }
                g_dialPos = pos;
            }
            if (!InteractionAsks(g_dial->turnAction))
            {
                g_dialAskedSince = -1.0;
                return;
            }
            if (g_dialAskedSince < 0.0) g_dialAskedSince = now;
            if (g_dialWaySaid || pos >= g_dial->station || now - g_dialAskedSince < kLinearAfterPrompt) return;
            g_dialWaySaid = true;
            const auto key = input::KeyForAction(g_dial->forward);
            log::Info(L"interactions: the dial's way to the station is {} ({})", g_dial->forward, key);
            if (!key.empty()) speech::Announce(locale::Mod(L"interact.linear.forward", key));
        }

        bool GlobalBool(const wchar_t* name, bool& out)
        {
            static UObject* library = nullptr;
            static RC::Unreal::UFunction* fn = nullptr;
            if (!library || !obj::IsLive(library))
            {
                library = obj::FindObject(L"/Script/Bates.Default__BatesGlobalStateBlueprintLibrary");
                fn = library ? obj::FindFunction(library, L"GetGlobalStateControlParameterBool") : nullptr;
            }
            UObject* pawn = Pawn();
            if (!fn || !pawn) return false;
            bool read = false;
            obj::Call(
                library, fn,
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (!prop) continue;
                        const auto pname = prop->GetName();
                        if (pname == L"Context")
                            *static_cast<UObject**>(obj::ValuePtrAt(params, prop)) = pawn;
                        else if (pname == L"GlobalStateName")
                            obj::WriteStringAt(params, prop, name);
                    }
                },
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"ReturnValue") read = obj::ReadBoolAt(params, prop, out);
                    }
                });
            return read;
        }

        void PollLamp(double now)
        {
            if (!g_window) return;
            bool lit = false;
            if (!GlobalBool(g_window->flag, lit)) return;
            const bool was = g_lamp.On();
            if (g_lamp.Update(lit))
            {
                log::Info(L"interactions: {} comes ({}): a press now is taken", g_window->what, g_window->flag);
                sounds::Ping();
                if (cfg::Detailed()) speech::Announce(locale::Mod(g_window->word));
            }
            else if (was && !lit)
            {
                log::Info(L"interactions: {} is over", g_window->what);
            }
            (void)now;
        }

        // ---- the poll ----------------------------------------------------------------------------

        void PollImpl()
        {
            const double now = gamethread::NowSeconds();
            // The interaction in use is asked while play runs; a pause or a screen keeps what it held.
            if (gamethread::FrameCount() % 10 == 0 && !gamestate::Paused() && !watch::CurrentScreen())
            {
                UObject* bound = gamestate::InPlay() ? BoundInteraction() : nullptr;
                const auto key = obj::KeyOf(bound);
                if (key != g_interactionKey)
                {
                    g_interactionKey = key;
                    g_interaction = bound;
                    ++g_bindings;
                    StopFraming(L"the character left the interaction");
                    g_framingTaken = false;
                    ResetTurn();
                    g_lamp.Reset();
                    g_window = nullptr;
                    for (const auto& w : kPressWindows)
                    {
                        if (bound && obj::IsA(bound, w.cls)) g_window = &w;
                    }
                    g_lampActive = g_window != nullptr;
                    g_sidesSaid = false;
                    if (bound) log::Info(L"interactions: the character uses {} ({})", obj::ObjectName(bound), obj::ClassName(bound));
                    if (bound && obj::IsA(bound, L"BatesInteractionComplex") && !obj::IsA(bound, L"BatesInteractionComplexCameraNudge"))
                        FindExamineStages(bound);
                    // The interaction ending as its stage's clock ran out out of a waiting state ran out.
                    if (g_timedWatched >= 0 && g_limitStage == g_timedWatched && g_timedOutSaid != g_timedWatched &&
                        gamethread::NowSeconds() - g_limitAt <= kTimeoutTakenWithin &&
                        std::any_of(g_statesAtLimit.begin(), g_statesAtLimit.end(), prompt::WaitingState))
                        SayRanOut(L"the interaction ending");
                    g_timedWatched = -1;
                    g_limitStage = -1;
                    g_statesAtLimit.clear();
                    if (bound) FindTimedStages(bound);
                    else g_timedStages.clear();
                    if (bound) FindLinearStages(bound);
                    else g_linearForward.clear();
                    g_dial = bound ? prompt::DialOf(obj::ClassName(bound)) : std::nullopt;
                    g_dialPos = -1;
                    g_dialAskedSince = -1.0;
                    g_dialWaySaid = false;
                    g_statesSeen.clear();
                    g_statesSince = -1.0;
                    g_promptRepeated = false;
                }
            }
            if (!g_interaction || !obj::IsSame(g_interactionKey)) return;
            // Standing still with the world: nothing is sounded.
            if (gamestate::Paused() || watch::CurrentScreen()) return;
            if (obj::IsA(g_interaction, L"BatesInteractionComplexCameraNudge"))
                PollFraming(g_interaction, now);
            else if (!g_examineStages.empty())
                PollTurn(g_interaction, now);
            if (g_lampActive && gamethread::FrameCount() % 2 == 0) PollLamp(now);
            PollTimedStage(g_interaction);
            if (gamethread::FrameCount() % 6 == 3) PollLinear(g_interaction, now);
            if (gamethread::FrameCount() % 6 == 5) PollDial(now);
            if (gamethread::FrameCount() % 6 == 1) PollStates(g_interaction, now);
        }

        void Poll(float)
        {
            obj::SafeInvokeLogged(L"interactions.PollImpl", [](void*) { PollImpl(); }, nullptr);
        }

        // Every axis still outside the framing, however little (nudge::Lead).
        std::wstring FramingText()
        {
            const auto& l = g_framing.lead;
            if (l.sight.onTarget) return locale::Mod(L"nudge.on");
            std::vector<std::wstring> words;
            if (l.turnRight != 0) words.push_back(locale::Mod(l.turnRight > 0 ? L"dir8.right" : L"dir8.left"));
            if (l.turnUp != 0) words.push_back(locale::Mod(l.turnUp > 0 ? L"explore.higher" : L"explore.lower"));
            if (words.empty()) words.push_back(locale::Mod(L"dir8.ahead"));
            return locale::Mod(L"nudge.where", str::Join(words, L" "));
        }

        // The way to push and what is left of the turn.
        std::wstring TurnText()
        {
            const auto& l = g_turnLead;
            const wchar_t* key = l.right > 0 ? L"turn.right" : (l.right < 0 ? L"turn.left" : (l.up > 0 ? L"turn.up" : L"turn.down"));
            return locale::Mod(key, std::to_wstring(nudge::PercentLeft(g_turn)));
        }

        bool OnWhere()
        {
            if (FramingLed())
                speech::Now(FramingText());
            else if (RevealLed())
                speech::Now(TurnText());
            else
                return false;
            return true;
        }

        bool OnSound()
        {
            if (!FramingLed() && !RevealLed()) return false;
            ToggleLookBeacon();
            return true;
        }
    }

    namespace
    {
        // The values an activity's stage conditions are judged by (BatesControlParameterStageCondition),
        // which Blueprints write by name. Some activities of chapters 9 and 10 branch on the stick
        // (act7_2's Wendigo sighting on LeftStickXAxis, Act7_3's on LeftStickYAxis and LocoMoveAny,
        // Josh's visions on LocoMoveForward and the like) behind interaction stages that start no
        // interaction and go on at once, and nothing in the game was found to write those values: the
        // scenes go on by their triggers. Logged as they are written, each value's first and every
        // change, so that a play shows it should anything write them.
        std::map<std::wstring, double> g_controlParameters;

        void OnControlParameter(UObject* self, RC::Unreal::FFrame& stack)
        {
            std::wstring name;
            double value = 0.0;
            if (!params::String(stack, L"ParameterName", name) || !params::Float(stack, L"Value", value)) return;
            const std::wstring key = obj::ObjectName(self) + L"." + name;
            const auto known = g_controlParameters.find(key);
            if (known != g_controlParameters.end() && std::fabs(known->second - value) < 0.01) return;
            g_controlParameters[key] = value;
            if (g_controlParameters.size() > 512) g_controlParameters.clear();
            log::Info(L"interactions: activity {} has its control parameter {} set to {:.2f}", obj::ObjectName(self), name, value);
        }
    }

    void InteractionsFeature::Install()
    {
        hooks::OnNative(L"/Script/Bates.BatesActivityActor:SetLocalControlParameterValue", &OnControlParameter, nullptr);
        using hotkeys::Command;
        hotkeys::AddContext(&FramingLed);
        hotkeys::AddContext(&RevealLed);
        hotkeys::OnCommand(Command::Where, &OnWhere);
        hotkeys::OnCommand(Command::Beacon, &OnSound);
        gamethread::AddPoller(L"interactions", &Poll);
    }

    void InteractionsFeature::Describe(std::vector<std::wstring>& out)
    {
        if (FramingLed()) out.push_back(FramingText());
        if (RevealLed()) out.push_back(TurnText());
    }

    // The examine stage the interaction stands in takes an optional input the game never shows: the
    // snow globe of act6_2 is shaken by IA_ControllerShake (BP_rush_of_blood_snowglobe's examine,
    // bUseOptionalInputAction), its prompt naming only the left stick's turn and the way back. The
    // shake: a PlayStation pad shaken, the game's six-axis on (UBatesInputRouter::DoControllerShakeWork:
    // five of thirty samples past 2.0); another pad's left stick up or down (its vendor modifier
    // gives a PlayStation pad's stick nothing); the keyboard's W or S (MC_Gameplay).
    static bool ShakeStageNow()
    {
        if (!g_interaction || !obj::IsSame(g_interactionKey)) return false;
        int64_t current = -1;
        if (!obj::ReadInt(g_interaction, L"CurrentStage", current)) return false;
        std::vector<UObject*> components;
        obj::ReadObjectArray(g_interaction, L"BlueprintCreatedComponents", components);
        for (UObject* component : components)
        {
            if (!obj::IsLive(component) || !obj::IsA(component, L"BatesInteractionStageExamine")) continue;
            int64_t index = -1;
            bool optional = false;
            UObject* action = nullptr;
            obj::ReadInt(component, L"Stage", index);
            obj::ReadBool(component, L"bUseOptionalInputAction", optional);
            obj::ReadObject(component, L"InputActionAssigned", action);
            if (index == current && optional && obj::IsLive(action) && obj::ObjectName(action) == L"IA_ControllerShake") return true;
        }
        return false;
    }

    void InteractionsFeature::Help(std::vector<std::wstring>& out)
    {
        using hotkeys::Command;
        if (ShakeStageNow())
        {
            const bool pad = input::CurrentScheme() == input::Scheme::Gamepad;
            const bool playStation = pad && input::PlayStationPrompts();
            // The game's own name for its setting, as its controls page shows it.
            out.push_back(locale::Mod(prompt::ShakeHelp(pad, playStation, playStation && input::MotionSensorOn()),
                                      gametext::ResolveWithFallback(L"BATES_SETTING_CONTROL_MOTIONSENSOR", L"Motion Sensor Function")));
        }
        if (FramingLed())
            out.push_back(locale::Mod(input::CurrentScheme() == input::Scheme::Gamepad ? L"help.nudge.pad" : L"help.nudge",
                                      {hotkeys::SpokenKey(Command::Where), hotkeys::SpokenKey(Command::Beacon)}));
        if (RevealLed()) out.push_back(locale::Mod(L"help.turn", {hotkeys::SpokenKey(Command::Where), hotkeys::SpokenKey(Command::Beacon)}));
        if (g_lampActive && g_window == &kPressWindows[0] && g_interaction && obj::IsSame(g_interactionKey))
            out.push_back(locale::Mod(L"help.boiler", input::KeyForAction(L"IA_GrabInteraction")));
    }

    bool FramingLed()
    {
        return g_framing.on && !gamestate::Paused() && !watch::CurrentScreen();
    }

    bool RevealLed()
    {
        return g_turnCourse.Led() && g_interaction && obj::IsSame(g_interactionKey) && !gamestate::Paused() && !watch::CurrentScreen();
    }

    bool InteractionStageTimed()
    {
        return g_interaction && obj::IsSame(g_interactionKey) && TimedStageSeconds(g_interaction) > 0.0;
    }

    int64_t SoundingStagedStage()
    {
        if (!g_interaction || !obj::IsSame(g_interactionKey)) return -1;
        int64_t current = -1;
        if (!obj::ReadInt(g_interaction, L"CurrentStage", current) || current < 0 || !g_linearLast.count(current)) return -1;
        return g_bindings * 1024 + current;
    }

    std::optional<bool> GrabTogglesFor(UObject* widget)
    {
        if (!widget || !g_interaction || !obj::IsSame(g_interactionKey)) return std::nullopt;
        int64_t current = -1;
        obj::ReadInt(g_interaction, L"CurrentStage", current);
        std::vector<UObject*> components;
        obj::ReadObjectArray(g_interaction, L"BlueprintCreatedComponents", components);
        std::optional<bool> found;
        for (UObject* component : components)
        {
            if (!obj::IsLive(component) || !obj::IsA(component, L"BatesInteractionStageBase")) continue;
            UObject* shown = nullptr;
            if (!obj::ReadObject(component, L"InteractionWidget", shown) || shown != widget) continue;
            const auto* toggles = static_cast<const uint8_t*>(sym::MemberOf(component, L"UBatesInteractionStageBase.bUsingToggleGrab"));
            if (!toggles) continue;
            int64_t index = -2;
            obj::ReadInt(component, L"Stage", index);
            if (index == current) return *toggles != 0;
            if (!found) found = *toggles != 0;
        }
        return found;
    }
}
