#include "features/Totems.hpp"

#include "features/Exploration.hpp"
#include "features/NudgeRules.hpp"
#include "features/WorldRead.hpp"

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

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <numbers>
#include <string>

using namespace RC::Unreal;

namespace da::features
{
    namespace
    {
        using namespace world;

        // How the game judges the inspection, read from the totem itself each frame: the
        // closeness of the point under the view's centre to the premonition's (1 on it, 0
        // off the totem), kept at `CachedTotemDistanceToPoint`; the time the view has rested
        // on it as a share of the time the vision needs, at `CachedTotemTimeOnPoint`; and the
        // closeness it starts counting that time from, `OpeningLookDistanceThreshold`. The
        // point under the view is `CachedTotemInspectionLocation`; the premonition is the
        // `DiscoveryPoint` component, which turns with the totem.
        UObject* g_totem = nullptr;
        bool g_held = false;
        bool g_witnessed = false; // the game remembers its vision as seen: it measures nothing and shows nothing
        // A totem put down stays put down until its interaction is over: after the vision the
        // game puts it down and the interaction winds down for a few seconds, and the figures the
        // inspection leaves behind are no new pick-up (taken for one, it was "found already").
        obj::ObjectKey g_putDown;
        bool g_onPoint = false;
        bool g_visionSaid = false;
        double g_blipAt = 0.0;
        double g_loggedAt = 0.0;
        double g_missLoggedAt = 0.0;
        nudge::TotemLead g_lead; // the aim sound's way, on the totem and off it
        bool g_pointMeasured = false; // the premonition's point found as the game finds it, as logged
        const wchar_t* g_measuredFrom = nullptr; // what the premonition is placed against, as logged

        std::wstring TotemName(UObject* totem)
        {
            return obj::IsLive(totem) ? obj::ObjectName(totem) : L"<gone>";
        }

        void End(const wchar_t* why)
        {
            if (!g_totem) return;
            log::Info(L"totems: {} is out of hand: {}", TotemName(g_totem), why);
            g_totem = nullptr;
            g_held = false;
            g_onPoint = false;
        }

        // A vision's film by the name the descriptions are kept under: its file's name without
        // the folder, the extension, or the Japanese cut's suffix (the same vision with its
        // words burnt in).
        std::wstring VisionName(std::wstring movie)
        {
            if (const auto slash = movie.find_last_of(L"/\\"); slash != std::wstring::npos) movie = movie.substr(slash + 1);
            if (const auto dot = movie.rfind(L'.'); dot != std::wstring::npos) movie = movie.substr(0, dot);
            if (movie.size() > 3 && movie.ends_with(L"_ja")) movie.resize(movie.size() - 3);
            return movie;
        }

        // A vision is shown, in the world or replayed in the totems' menu: the film's words when
        // the mod has them, the bare word otherwise.
        void AnnounceVision(const std::wstring& film, const std::wstring& whose, const wchar_t* how)
        {
            const std::wstring movie = VisionName(film);
            const std::wstring key = L"totem." + movie;
            const bool described = !movie.empty() && locale::Has(key);
            log::Info(L"totems: the vision \"{}\" of {} begins ({}); {}", movie, whose, how, described ? L"its words are said" : L"no words for it yet");
            speech::Announce(described ? locale::Mod(key) : locale::Mod(L"totem.vision"));
        }

        // The vision of the totem in hand begins.
        void Vision(const wchar_t* how)
        {
            if (g_visionSaid || !obj::IsLive(g_totem)) return;
            g_visionSaid = true;
            std::wstring movie;
            obj::ReadString(g_totem, L"TotemVisionMovieName", movie);
            AnnounceVision(movie, TotemName(g_totem), how);
        }

        // The totems' menu plays a found totem's vision on its page (WBP_TotemInfoPage: its
        // PlayVideo opens CurrentMoviePath in the page's own film player), when the page opens
        // and again on the replay prompt. Nothing is looked for on that page: the stick only
        // turns the totem, so there is nothing to lead.
        void OnMenuVision(UObject* self, FFrame&)
        {
            if (!obj::IsLive(self)) return;
            std::wstring path;
            obj::ReadString(self, L"CurrentMoviePath", path);
            if (path.empty()) obj::ReadString(self, L"CurrentMovie", path);
            if (path.empty())
            {
                log::Info(L"totems: the totems' menu plays a vision, its film not known");
                return;
            }
            AnnounceVision(path, L"the totems' menu", L"its page plays it");
        }

        // Whether the game holds this totem's vision as already witnessed (its own getter,
        // over the saved state): then it neither measures the view nor plays the film again.
        void NoteWitnessed(UObject* totem)
        {
            const bool witnessed = obj::CallForBool(totem, L"GetWitnessedVisionState");
            if (witnessed == g_witnessed) return;
            g_witnessed = witnessed;
            if (!witnessed) return;
            log::Info(L"totems: the vision of {} was witnessed before, so the game measures nothing and shows nothing", TotemName(totem));
            speech::Announce(locale::Mod(L"totem.found"));
        }

        void OnPickedUp(UObject* self, FFrame&)
        {
            if (!obj::IsLive(self)) return;
            g_totem = self;
            g_putDown = {};
            g_held = false;
            g_onPoint = false;
            g_visionSaid = false;
            g_witnessed = false;
            g_blipAt = 0.0;
            g_loggedAt = 0.0;
            g_lead.Reset();
            g_pointMeasured = false;
            g_measuredFrom = nullptr;
            log::Info(L"totems: {} is picked up", TotemName(self));
            NoteWitnessed(self);
        }

        void OnFullyHeld(UObject* self, FFrame&)
        {
            if (!obj::IsLive(self)) return;
            g_totem = self;
            g_held = true;
            g_visionSaid = false;
            double threshold = 0.0, vision = 0.0, locked = 0.0, decay = 0.0;
            obj::ReadFloat(self, L"OpeningLookDistanceThreshold", threshold);
            obj::ReadFloat(self, L"TimeToLookAtBeforeVision", vision);
            obj::ReadFloat(self, L"TimeToLookAtBeforeLocked", locked);
            obj::ReadFloat(self, L"TimeDecayRate", decay);
            log::Info(L"totems: {} is held, and the inspection begins: the time counts from closeness {:.2f}, the view locks after {:.1f} s and the "
                      L"vision comes after {:.1f} s; off the point the time decays {:.1f} times as fast",
                      TotemName(self), threshold, locked, vision, decay);
            // How far the pose moves the game's line across the totem (UpdateTotemExamination: the
            // pose scaled by the bound of PhantomOffset*Limits on its side, in the line's own frame).
            bool phantom = false;
            obj::ReadBool(self, L"bUsePhantomOffset", phantom);
            const auto bounds = [&](const wchar_t* name)
            {
                auto* prop = obj::FindProperty(self, name);
                const auto* at = prop ? static_cast<const uint8_t*>(obj::ValuePtr(self, prop)) : nullptr;
                return at ? std::format(L"{:.1f}..{:.1f}", *reinterpret_cast<const float*>(at + 4), *reinterpret_cast<const float*>(at + 12))
                          : std::wstring(L"?");
            };
            log::Info(L"totems: the pose moves the game's line{}: across {}, up and down {}", phantom ? L"" : L" not (no phantom offset)",
                      bounds(L"PhantomOffsetHorizontalLimits"), bounds(L"PhantomOffsetVerticalLimits"));
            NoteWitnessed(self);
        }

        void OnPutDown(UObject* self, FFrame&)
        {
            if (self != g_totem) return;
            g_putDown = obj::KeyOf(self);
            End(L"put down");
        }

        void OnMovieEnded(UObject* self, FFrame& stack)
        {
            std::wstring url;
            params::String(stack, L"MovieUrl", url);
            log::Info(L"totems: the vision film of {} ended ({})", TotemName(self), url);
        }

        void OnVisionBegin(UObject* self, FFrame&)
        {
            if (obj::IsLive(self)) g_totem = self;
            Vision(L"the game's event");
        }

        void OnVisionEnd(UObject* self, FFrame&)
        {
            log::Info(L"totems: the vision of {} ends", TotemName(self));
        }

        // Where the stick has sent the totem's turn, for the log: the game adds the stick, through
        // its speed curves, to a pose of two numbers held within the totem's limits
        // (ABatesInteractionTotem::CalculateExamineInput: CumulativeInspectionInput clamped to
        // HorizontalBlendspaceLimits and VerticalBlendspaceLimits), which the pose drawn eases
        // towards (CurrentInspectionInput, at InputInterpRate); a totem may turn against the stick
        // (bInvertHorizontalMovement, bInvertVerticalMovement).
        std::wstring TurnState(UObject* totem)
        {
            const auto* sent = static_cast<const double*>(sym::MemberOf(totem, L"ABatesInteractionTotem.CumulativeInspectionInput"));
            const auto* drawn = static_cast<const double*>(sym::MemberOf(totem, L"ABatesInteractionTotem.CurrentInspectionInput"));
            const auto* invertH = static_cast<const bool*>(sym::MemberOf(totem, L"ABatesInteractionTotem.bInvertHorizontalMovement"));
            const auto* invertV = static_cast<const bool*>(sym::MemberOf(totem, L"ABatesInteractionTotem.bInvertVerticalMovement"));
            if (!sent || !drawn || !invertH || !invertV) return L"";
            // An FFloatRange: each bound a kind (int) and a value (float).
            const auto range = [&](const wchar_t* name, float& low, float& high)
            {
                auto* prop = obj::FindProperty(totem, name);
                const auto* at = prop ? static_cast<const uint8_t*>(obj::ValuePtr(totem, prop)) : nullptr;
                if (!at) return false;
                low = *reinterpret_cast<const float*>(at + 4);
                high = *reinterpret_cast<const float*>(at + 12);
                return true;
            };
            float hLow = 0, hHigh = 0, vLow = 0, vHigh = 0;
            const bool limits = range(L"HorizontalBlendspaceLimits", hLow, hHigh) && range(L"VerticalBlendspaceLimits", vLow, vHigh);
            return std::format(L"; the turn sent to ({:+.2f}, {:+.2f}){}, drawn at ({:+.2f}, {:+.2f}){}{}", sent[0], sent[1],
                               limits ? std::format(L" of {:+.2f}..{:+.2f} by {:+.2f}..{:+.2f}", hLow, hHigh, vLow, vHigh) : std::wstring(), drawn[0], drawn[1],
                               *invertH ? L", across inverted" : L"", *invertV ? L", up and down inverted" : L"");
        }

        // Where the game measures the premonition from (ABatesInteractionTotem::FindUVFromPremonition,
        // 0x4640340): the point of the totem's collider (TotemMeshCollider) that a line traced in from
        // as far again beyond the DiscoveryPoint arrow as the mesh's "totem_centre" socket lies within
        // it, toward that socket, meets first; its texture's spot there is the one the view must rest
        // on. The arrow itself lies off that point: at the death totem on 2026-11-07 the view lay square
        // on the arrow at closeness 0.70, and the vision came with the arrow 3 cm right and 3 cm down of
        // it. Traced out from the socket instead, the line met no face (a mesh's faces are met from
        // outside only) and the point was never found that evening.
        bool PremonitionPoint(UObject* totem, const Vec& arrow, Vec& out)
        {
            UObject* mesh = nullptr;
            UObject* collider = nullptr;
            if (!obj::ReadObject(totem, L"TotemMesh", mesh) || !obj::IsLive(mesh) || !obj::ReadObject(totem, L"TotemMeshCollider", collider) ||
                !obj::IsLive(collider))
                return false;
            Vec centre;
            auto* socket = obj::FindFunction(mesh, L"GetSocketLocation");
            if (!socket) return false;
            bool placed = false;
            obj::Call(
                mesh, socket,
                [&](void* params)
                {
                    for (auto* prop : socket->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"InSocketName")
                            std::construct_at(reinterpret_cast<FName*>(obj::ValuePtrAt(params, prop)), L"totem_centre", FNAME_Add);
                    }
                },
                [&](void* params)
                {
                    for (auto* prop : socket->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"ReturnValue") placed = ReadVec(obj::ValuePtrAt(params, prop), prop, centre);
                    }
                });
            if (!placed) return false;
            const Vec start{2.0 * arrow.x - centre.x, 2.0 * arrow.y - centre.y, 2.0 * arrow.z - centre.z};
            auto* trace = obj::FindFunction(collider, L"K2_LineTraceComponent");
            if (!trace) return false;
            bool hit = false;
            Vec at;
            obj::Call(
                collider, trace,
                [&](void* params)
                {
                    for (auto* prop : trace->ForEachProperty())
                    {
                        if (!prop) continue;
                        const auto name = prop->GetName();
                        if (name == L"TraceStart")
                            WriteVec(obj::ValuePtrAt(params, prop), prop, start);
                        else if (name == L"TraceEnd")
                            WriteVec(obj::ValuePtrAt(params, prop), prop, centre);
                        else if (name == L"bTraceComplex")
                            obj::WriteBoolAt(params, prop, true);
                    }
                },
                [&](void* params)
                {
                    for (auto* prop : trace->ForEachProperty())
                    {
                        if (!prop) continue;
                        const auto name = prop->GetName();
                        if (name == L"ReturnValue")
                            obj::ReadBoolAt(params, prop, hit);
                        else if (name == L"HitLocation")
                            ReadVec(obj::ValuePtrAt(params, prop), prop, at);
                    }
                });
            if (!hit) return false;
            out = at;
            return true;
        }

        // Leads the view to the premonition with the aim sound: the blip on the side the point
        // lies from the point the game's line meets the totem at, higher or lower by their
        // difference up the totem, quicker as the game's own closeness grows, muffled while the
        // point is round the back, and the quick double ping while the game counts the time on
        // it. The game's line runs from the camera's eye to its query point
        // (CachedTotemInspectionQueryLocation), which the stick moves; while it misses the totem
        // the point is placed against that line itself (nudge::TotemLead).
        void Lead(double now, double closeness, double progress, bool onPoint)
        {
            Vec eye, hit, discovery;
            double pitch = 0.0, yaw = 0.0;
            UObject* arrow = nullptr;
            if (!CameraLine(eye, pitch, yaw) || !obj::ReadObject(g_totem, L"DiscoveryPoint", arrow) || !ComponentLocation(arrow, discovery)) return;
            // The point the game measures from, where the line through the arrow meets the totem.
            Vec point;
            const bool measured = PremonitionPoint(g_totem, discovery, point);
            if (measured) discovery = point;
            if (measured != g_pointMeasured)
            {
                g_pointMeasured = measured;
                log::Info(L"totems: the premonition is measured {}", measured ? L"at the point the game traces through its arrow"
                                                                             : L"at its arrow (the game's trace through it found nothing)");
            }
            Vec forward, right, up;
            ViewBasis(pitch, yaw, forward, right, up);
            // The game finds the two places on the totem's texture only while its line meets the
            // totem, the closeness staying 0 otherwise and going below 0 where they lie far apart
            // (UpdateTimeLookedAt, 0x4659dc0); the point it last met is stale then. Off the totem
            // the premonition is placed against the game's line itself, from the eye to the query
            // point, the view's centre line as a last resort.
            const bool hitKnown = std::fabs(closeness) > 1e-6 && VecProperty(g_totem, L"CachedTotemInspectionLocation", hit);
            Vec query;
            const bool lineKnown = !hitKnown && VecProperty(g_totem, L"CachedTotemInspectionQueryLocation", query);
            const Vec centreAhead{eye.x + forward.x * 100.0, eye.y + forward.y * 100.0, eye.z + forward.z * 100.0};
            const Vec d = hitKnown ? Minus(discovery, hit) : nudge::AcrossLine(discovery, eye, lineKnown ? query : centreAhead, forward);
            const wchar_t* from = hitKnown    ? L"the point the game's line meets the totem at"
                                  : lineKnown ? L"the game's line"
                                              : L"the view's centre line";
            if (from != g_measuredFrom)
            {
                g_measuredFrom = from;
                log::Info(L"totems: the premonition is placed against {}", from);
            }
            nudge::TotemSample sample;
            sample.lateral = Dot(d, right);
            sample.vertical = Dot(d, up);
            sample.depth = hitKnown ? Dot(d, forward) : 0.0;
            sample.onTotem = hitKnown;
            sample.closeness = closeness;
            if (const auto* turn = static_cast<const double*>(sym::MemberOf(g_totem, L"ABatesInteractionTotem.CumulativeInspectionInput")))
            {
                sample.turnKnown = true;
                sample.turnX = turn[0];
                sample.turnY = turn[1];
            }
            // By where the point lies from the game's line, each axis the way the pose was seen to
            // move it (nudge::TotemLead).
            const int wayX = g_lead.WayX(), wayY = g_lead.WayY();
            const auto aim = g_lead.Update(sample);
            if (g_lead.WayX() != wayX || g_lead.WayY() != wayY)
                log::Info(L"totems: a push toward the premonition's side brings it nearer {} across and {} up and down, by how it answered the pose",
                          g_lead.WayX() > 0 ? L"as it should" : L"never (led the other way)", g_lead.WayY() > 0 ? L"as it should" : L"never (led the other way)");
            if (now - g_loggedAt >= 1.0)
            {
                g_loggedAt = now;
                const auto pad = input::ReadPad();
                log::Info(L"totems: the point lies {:+.0f} cm across, {:+.0f} up and {:+.0f} deep of the view's point{}; closeness {:.2f}, time {:.0f}%, {}; "
                          L"stick ({}, {}){}; led pan {:+.2f}, level {:.2f}",
                          sample.lateral, sample.vertical, sample.depth, hitKnown ? L"" : L" (the game's line misses the totem)", closeness,
                          progress * 100.0, onPoint ? L"on the point" : L"off it", pad.valid ? pad.leftX : 0, pad.valid ? pad.leftY : 0, TurnState(g_totem),
                          aim.pan, aim.level);
            }
            if (!LookBeaconOn()) return;
            const double interval = onPoint ? 0.12 : 0.09 + 0.4 * std::clamp(1.0 - closeness, 0.0, 1.0);
            if (now - g_blipAt < interval) return;
            g_blipAt = now;
            sounds::Aim(aim.pan, aim.level, onPoint, aim.behind);
        }

        void PollImpl()
        {
            const double now = gamethread::NowSeconds();
            if (!g_totem)
            {
                // The pick-up event missed: the totem being inspected is the one the game
                // measures the view against.
                if (gamethread::FrameCount() % 30 != 0) return;
                if (!CharacterInInteraction())
                {
                    g_putDown = {};
                    return;
                }
                for (auto* totem : obj::FindAllLive(L"BatesInteractionTotem"))
                {
                    if (totem == g_putDown.object && obj::IsSame(g_putDown)) continue;
                    double closeness = 0.0;
                    if (obj::ReadFloat(totem, L"CachedTotemDistanceToPoint", closeness) && std::fabs(closeness) > 1e-6)
                    {
                        g_totem = totem;
                        g_held = true;
                        g_visionSaid = false;
                        g_witnessed = false;
                        log::Info(L"totems: {} is in hand (found by the inspection running)", TotemName(totem));
                        NoteWitnessed(totem);
                        break;
                    }
                }
                if (!g_totem) return;
            }
            if (!obj::IsLive(g_totem))
            {
                End(L"gone");
                return;
            }
            if (!CharacterInInteraction())
            {
                End(L"the interaction is over");
                return;
            }
            double closeness = 0.0, progress = 0.0, threshold = 0.0;
            obj::ReadFloat(g_totem, L"CachedTotemDistanceToPoint", closeness);
            obj::ReadFloat(g_totem, L"CachedTotemTimeOnPoint", progress);
            obj::ReadFloat(g_totem, L"OpeningLookDistanceThreshold", threshold);
            // The game counts the time on the point while the closeness is over its threshold.
            const bool onPoint = closeness > threshold;
            if (onPoint != g_onPoint)
            {
                g_onPoint = onPoint;
                log::Info(L"totems: the view is {} the point (closeness {:.2f} against {:.2f}, time {:.0f}%)", onPoint ? L"on" : L"off", closeness, threshold,
                          progress * 100.0);
            }
            if (progress >= 0.999) Vision(L"the time on the point is full");
            if (!g_held && std::fabs(closeness) > 1e-6)
            {
                g_held = true;
                log::Info(L"totems: {} counts as held: the inspection is running", TotemName(g_totem));
            }
            if (!g_held || g_visionSaid || g_witnessed) return;
            if (std::fabs(closeness) <= 1e-6 && now - g_missLoggedAt >= 5.0)
            {
                g_missLoggedAt = now;
                log::Info(L"totems: the game's line misses the totem");
            }
            Lead(now, closeness, progress, onPoint);
        }

        void Poll(float)
        {
            obj::SafeInvokeLogged(L"totems.PollImpl", [](void*) { PollImpl(); }, nullptr);
        }

        // The turn is led while the game measures the view against the premonition: from the
        // moment the totem is held until its vision, and never for a vision seen before.
        bool PremonitionLed()
        {
            return g_totem && g_held && !g_visionSaid && !g_witnessed && obj::IsLive(g_totem);
        }

        bool OnBeacon()
        {
            if (!PremonitionLed()) return false;
            ToggleLookBeacon();
            return true;
        }
    }

    void TotemsFeature::Install()
    {
        hooks::OnNative(L"/Script/Bates.BatesInteractionTotem:OnTotemPickedUp", nullptr, &OnPickedUp);
        hooks::OnNative(L"/Script/Bates.BatesInteractionTotem:OnTotemFullyHeld", nullptr, &OnFullyHeld);
        hooks::OnNative(L"/Script/Bates.BatesInteractionTotem:OnTotemPutDown", nullptr, &OnPutDown);
        hooks::OnNative(L"/Script/Bates.BatesInteractionTotem:OnMovieEnded", nullptr, &OnMovieEnded);
        // The vision's events are the game's Blueprint events: hooked where a Blueprint
        // implements them and where none does.
        hooks::OnNative(L"/Script/Bates.BatesInteractionTotem:BP_OnWitnessVisionBegin", &OnVisionBegin, nullptr);
        hooks::OnNative(L"/Script/Bates.BatesInteractionTotem:BP_OnWitnessVisionEnd", &OnVisionEnd, nullptr);
        hooks::OnScript(L"BatesInteractionTotem", L"BP_OnWitnessVisionBegin", &OnVisionBegin);
        hooks::OnScript(L"BatesInteractionTotem", L"BP_OnWitnessVisionEnd", &OnVisionEnd);
        hooks::OnScript(L"WBP_TotemInfoPage_C", L"PlayVideo", &OnMenuVision);
        hooks::OnScript(L"BatesTotemInspectPage", L"BP_OnReplayAction",
                        [](UObject* self, FFrame&) { log::Info(L"totems: the totems' menu replays the vision of {}", obj::ObjectName(self)); });
        hotkeys::AddContext(&PremonitionLed);
        hotkeys::OnCommand(hotkeys::Command::Beacon, &OnBeacon);
        gamethread::AddPoller(L"totems", &Poll);
    }

    void TotemsFeature::Help(std::vector<std::wstring>& out)
    {
        if (!PremonitionLed()) return;
        out.push_back(locale::Mod(L"help.totem", hotkeys::SpokenKey(hotkeys::Command::Beacon)));
    }

    bool TotemInHand()
    {
        return g_totem != nullptr && obj::IsLive(g_totem);
    }
}
