#include "features/Totems.hpp"

#include "features/Exploration.hpp"
#include "features/WorldRead.hpp"

#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/ParamReader.hpp"
#include "hooks/HookDispatcher.hpp"
#include "hotkeys/Hotkeys.hpp"
#include "input/InputNames.hpp"
#include "locale/Locale.hpp"
#include "speech/Sounds.hpp"
#include "speech/Speech.hpp"

#include <algorithm>
#include <cmath>
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

        // Leads the view to the premonition with the aim sound: the blip on the side the
        // point lies from the point under the view, higher or lower by their difference up
        // the totem, quicker as the game's own closeness grows, muffled while the point is
        // round the back, and the quick double ping while the game counts the time on it.
        void Lead(double now, double closeness, double progress, bool onPoint)
        {
            Vec eye, hit, discovery;
            double pitch = 0.0, yaw = 0.0;
            UObject* arrow = nullptr;
            if (!CameraLine(eye, pitch, yaw) || !obj::ReadObject(g_totem, L"DiscoveryPoint", arrow) || !ComponentLocation(arrow, discovery)) return;
            Vec forward, right, up;
            ViewBasis(pitch, yaw, forward, right, up);
            // Off the totem the view's point is stale; the point is then placed against the
            // view's centre line itself.
            const bool hitKnown = closeness > 0.001 && VecProperty(g_totem, L"CachedTotemInspectionLocation", hit);
            Vec d = Minus(discovery, hitKnown ? hit : eye);
            if (!hitKnown)
            {
                const double along = Dot(d, forward);
                d = Minus(d, Vec{forward.x * along, forward.y * along, forward.z * along});
            }
            const double lateral = Dot(d, right);
            const double vertical = Dot(d, up);
            const double depth = hitKnown ? Dot(d, forward) : 0.0;
            // Round the side or the back the point lies deeper than the one under the view;
            // the blip then stays full on the side the shorter way round goes.
            double pan = std::clamp(lateral / 15.0, -1.0, 1.0);
            if (depth > 5.0) pan = (lateral < 0.0 ? -1.0 : 1.0) * std::max(std::fabs(pan), std::clamp(depth / 10.0, 0.0, 1.0));
            const double level = 0.5 + std::clamp(vertical / 20.0, -1.0, 1.0) * 0.5;
            const bool behind = depth > 10.0 && std::fabs(lateral) < 4.0;
            if (now - g_loggedAt >= 1.0)
            {
                g_loggedAt = now;
                const auto pad = input::ReadPad();
                log::Info(L"totems: the point lies {:+.0f} cm across, {:+.0f} up and {:+.0f} deep of the view's point{}; closeness {:.2f}, time {:.0f}%, {}; "
                          L"stick ({}, {})",
                          lateral, vertical, depth, hitKnown ? L"" : L" (the view misses the totem)", closeness, progress * 100.0,
                          onPoint ? L"on the point" : L"off it", pad.valid ? pad.leftX : 0, pad.valid ? pad.leftY : 0);
            }
            if (!LookBeaconOn()) return;
            const double interval = onPoint ? 0.12 : 0.09 + 0.4 * std::clamp(1.0 - closeness, 0.0, 1.0);
            if (now - g_blipAt < interval) return;
            g_blipAt = now;
            sounds::Aim(pan, level, onPoint, behind);
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
                    if (obj::ReadFloat(totem, L"CachedTotemDistanceToPoint", closeness) && closeness > 0.001)
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
            if (!g_held && closeness > 0.001)
            {
                g_held = true;
                log::Info(L"totems: {} counts as held: the inspection is running", TotemName(g_totem));
            }
            if (!g_held || g_visionSaid || g_witnessed) return;
            if (closeness <= 0.001 && now - g_missLoggedAt >= 5.0)
            {
                g_missLoggedAt = now;
                log::Info(L"totems: the view's centre misses the totem");
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
