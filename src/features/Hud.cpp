#include "features/Hud.hpp"

#include "features/TextRules.hpp"

#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/ParamReader.hpp"
#include "core/Strings.hpp"
#include "core/Symbols.hpp"
#include "hooks/HookDispatcher.hpp"
#include "locale/Locale.hpp"
#include "speech/Speech.hpp"
#include "ui/Widgets.hpp"
#include "watch/GameState.hpp"
#include "watch/Watchers.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace da::features
{
    using RC::Unreal::FFrame;
    using RC::Unreal::UObject;

    namespace
    {
        // The elements the player acts on, read by their own features, and the pages of the
        // menus: never read here, whatever text they carry.
        const wchar_t* const kOwnedElsewhere[] = {L"BatesSubtitlesWidget",
                                                  L"BatesQTEWidget",
                                                  L"BatesDirectionalChoiceWidget",
                                                  L"BatesDontMoveWidget",
                                                  L"BatesDontMoveTutorialWidget",
                                                  L"BatesInteractionWidget",
                                                  L"BatesGlintWidget",
                                                  L"BMUIPage",
                                                  L"BMUIActionBarWidget",
                                                  L"BatesPersistentCentreDotWidget",
                                                  L"BatesTTSIconWidget",
                                                  L"BMUIHUDContainer",
                                                  L"BatesHUDContainer"};

        bool OwnedElsewhere(UObject* widget)
        {
            for (const wchar_t* cls : kOwnedElsewhere)
            {
                if (obj::IsA(widget, cls)) return true;
            }
            return false;
        }

        // An element of the HUD and its lines as the rules weigh them (lines::Element).
        struct Tracked
        {
            UObject* instance = nullptr;
            lines::Element lines;
        };

        std::vector<Tracked> g_tracked;
        std::set<std::wstring> g_classesSeen; // for the log: each kind of element once
        lines::Echo g_echo;                   // the same words through two elements at once

        // The lines an element shows: the texts it draws now (not the ones still fading in,
        // nor the ones faded out), a row of pieces side by side as one line (the camcorder's
        // minutes, colon and seconds), then the prompts of its key caps, one line each.
        std::vector<std::wstring> Lines(UObject* widget)
        {
            auto lines = ui::DrawnRows(widget);
            const auto prompts = ui::PromptsIn(widget);
            // A hint of one line with a bare key beside it (a stick next to "Walk around the
            // kitchen") is one prompt: the line names the key.
            if (lines.size() == 1 && !prompts.empty() &&
                std::all_of(prompts.begin(), prompts.end(), [](const ui::Prompt& p) { return p.label.empty() && !p.key.empty(); }))
            {
                return {lines.front() + L": " + ui::PromptLines(prompts)};
            }
            for (const auto& prompt : prompts)
            {
                const auto line = ui::PromptLines({prompt});
                if (!line.empty()) lines.push_back(line);
            }
            return lines;
        }

        std::wstring Readout(UObject* widget)
        {
            return str::JoinSentences(Lines(widget));
        }

        void Track(UObject* widget)
        {
            if (!widget || !obj::IsLive(widget)) return;
            if (std::any_of(g_tracked.begin(), g_tracked.end(), [&](const Tracked& t) { return t.instance == widget; })) return;
            // The front end's furniture (its footer, its backgrounds) belongs to the menus; the
            // HUD is read in play, the loading screen and the words over a film whenever they come.
            if (!gamestate::InPlay() && !gamestate::Paused() && !obj::IsA(widget, L"BatesLoadingScreen") && !ui::IsVideoText(widget)) return;
            const auto cls = obj::ClassName(widget);
            // Whatever a menu's container draws (the pause menu's footer, the examples beside
            // the settings) is the menu's; the menus read it, or leave it, as they read the
            // same thing in the front end.
            if (ui::InsideMenuHost(widget) || ui::InsideMenuPage(widget))
            {
                if (g_classesSeen.insert(cls).second) log::Verbose(L"hud: {} belongs to a menu, left to the menus", cls);
                return;
            }
            if (g_classesSeen.insert(cls).second) log::Info(L"hud: {} first shown as {}, says \"{}\"", cls, obj::ObjectName(widget), Readout(widget));
            if (obj::IsA(widget, L"BatesLoadingScreen"))
            {
                speech::Announce(locale::Mod(L"hud.loading"));
                return;
            }
            g_tracked.push_back(Tracked{widget});
        }

        // An element is read once its words have stopped changing, line by new line
        // (lines::Element).
        void PollImpl()
        {
            if (gamethread::FrameCount() % 6 != 0) return;
            const double now = gamethread::NowSeconds();
            for (auto it = g_tracked.begin(); it != g_tracked.end();)
            {
                if (!obj::IsLive(it->instance))
                {
                    it = g_tracked.erase(it);
                    continue;
                }
                if (obj::IsWidgetShown(it->instance))
                {
                    const auto fresh = it->lines.Shown(Lines(it->instance), now);
                    if (!fresh.empty())
                    {
                        const auto text = str::JoinSentences(fresh);
                        log::Info(L"hud: {} says \"{}\"", obj::ClassName(it->instance), text);
                        if (g_echo.Says(text, now)) speech::Announce(text);
                    }
                }
                else
                {
                    it->lines.Hidden();
                }
                ++it;
            }
        }

        // The objective the game keeps for the player, set by the scene's script and shown
        // on the status page ("Find Hannah"): said when it changes, and read with the screen.
        std::wstring g_objective;
        bool g_objectiveKnown = false;

        std::wstring CurrentObjective()
        {
            UObject* manager = obj::FindFirstLive(L"BatesTraitsRelationshipsManager");
            if (!manager) return {};
            return str::CollapseWhitespace(obj::CallForText(manager, L"GetCurrentObjective"));
        }

        void PollObjective()
        {
            if (gamethread::FrameCount() % 30 != 7) return;
            const auto objective = CurrentObjective();
            if (g_objectiveKnown && objective == g_objective) return;
            const bool first = !g_objectiveKnown;
            g_objectiveKnown = true;
            g_objective = objective;
            if (objective.empty())
            {
                if (!first) log::Info(L"hud: the objective is turned off");
                return;
            }
            log::Info(L"hud: the objective is \"{}\"", objective);
            if (gamestate::InPlay()) speech::Announce(locale::Mod(L"hud.objective", objective));
        }

        // The loading screen's bar: the share of the level's streamed parts already in, as
        // the screen draws it (UBatesLoadingScreen::NativeTick hands it to its Blueprint every
        // frame). It is full long before the screen goes when the game compiles shaders
        // behind it.
        UObject* g_loadingScreen = nullptr;
        double g_loadingShare = -1.0;

        void OnLoadingShare(UObject* self, FFrame& stack)
        {
            double share = 0.0;
            if (!params::Float(stack, L"InPerc", share)) return;
            g_loadingScreen = self;
            g_loadingShare = std::clamp(share, 0.0, 1.0);
        }

        bool LoadingScreenShown()
        {
            return g_loadingScreen && obj::IsLive(g_loadingScreen) && obj::IsWidgetShown(g_loadingScreen);
        }

        // While a level loads, what it waits for, now and then, for the log: the bar; the
        // streaming manager's flags (2 a streaming request pending, 4 character variants
        // loading, 8 shaders compiling), which must all clear before the game turns to play;
        // and the shader compile behind the bar: the game's shader cache's mode (0 once its
        // own stall check has paused the compile, which only a change of the loading screen's
        // phase resumes) and the pipelines left of those it began with.
        double g_loadingLoggedAt = -100.0;

        void PollLoading()
        {
            const auto state = gamestate::Current();
            if (state != gamestate::State::LoadingNewLevel && state != gamestate::State::Loading)
            {
                g_loadingLoggedAt = -100.0;
                return;
            }
            const double now = gamethread::NowSeconds();
            if (g_loadingLoggedAt < 0.0) g_loadingLoggedAt = now;
            if (now - g_loadingLoggedAt < 15.0) return;
            g_loadingLoggedAt = now;
            int flags = -1;
            int mode = -1;
            int total = -1;
            int remaining = -1;
            if (UObject* streaming = obj::FindFirstLive(L"BatesLevelStreamingManager"))
            {
                if (const auto* p = static_cast<const uint8_t*>(sym::MemberOf(streaming, L"UBatesLevelStreamingManager.StreamingFlags"))) flags = *p;
            }
            if (UObject* shaders = obj::FindFirstLive(L"BatesShaderCacheSubsystem"))
            {
                if (const auto* p = static_cast<const int32_t*>(sym::MemberOf(shaders, L"UBatesShaderCacheSubsystem.CompilationMode"))) mode = *p;
                if (const auto* p = static_cast<const int32_t*>(sym::MemberOf(shaders, L"UBatesShaderCacheSubsystem.PrecompilesTotal"))) total = *p;
            }
            using Count = int32_t (*)();
            if (auto* count = reinterpret_cast<Count>(sym::Address(L"FShaderPipelineCache::NumPrecompilesRemaining"))) remaining = count();
            log::Info(L"loading: the bar at {}, the screen {}; streaming flags {} (2 a request pending, 4 character variants, 8 shaders); shader cache mode {} "
                      L"(0 paused), {} of {} pipelines left to compile",
                      g_loadingShare < 0.0 ? std::wstring(L"unknown") : std::format(L"{:.0f}%", g_loadingShare * 100.0),
                      LoadingScreenShown() ? L"shown" : L"not shown", flags < 0 ? std::wstring(L"unknown") : std::format(L"{:#x}", flags), mode, remaining,
                      total);
        }

        void Poll(float)
        {
            obj::SafeInvokeLogged(L"hud.PollImpl", [](void*) { PollImpl(); }, nullptr);
            obj::SafeInvokeLogged(L"hud.PollObjective", [](void*) { PollObjective(); }, nullptr);
            obj::SafeInvokeLogged(L"hud.PollLoading", [](void*) { PollLoading(); }, nullptr);
        }
    }

    void HudFeature::Install()
    {
        // The elements of the HUD as the watcher finds them; those read elsewhere are left.
        watch::OnWidgetClass(L"HudElement",
                             [](UObject* widget, bool appeared)
                             {
                                 if (appeared && !OwnedElsewhere(widget)) Track(widget);
                             });
        hooks::OnScript(L"BatesLoadingScreen", L"BP_SetPercLoaded", &OnLoadingShare);
        gamethread::AddPoller(L"hud", &Poll);
    }

    void HudFeature::Describe(std::vector<std::wstring>& out)
    {
        // The loading screen with its bar, as a glance at it shows; its own word is then in the
        // line already.
        const bool bar = LoadingScreenShown() && g_loadingShare >= 0.0;
        if (bar) out.push_back(locale::Mod(L"hud.loading.share", locale::Mod(L"hud.loading"), std::to_wstring(static_cast<int>(g_loadingShare * 100.0 + 0.5))));
        for (const auto& t : g_tracked)
        {
            if (bar && t.instance == g_loadingScreen) continue;
            if (obj::IsLive(t.instance) && obj::IsWidgetShown(t.instance) && !t.lines.Seen().empty()) out.push_back(str::JoinSentences(t.lines.Seen()));
        }
        if (gamestate::InPlay() && !g_objective.empty()) out.push_back(locale::Mod(L"hud.objective", g_objective));
    }
}
