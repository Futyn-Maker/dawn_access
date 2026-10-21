#include "features/Hud.hpp"

#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "hooks/HookDispatcher.hpp"
#include "locale/Locale.hpp"
#include "speech/Speech.hpp"
#include "ui/Widgets.hpp"
#include "watch/GameState.hpp"
#include "watch/Watchers.hpp"

#include <algorithm>
#include <map>
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

        struct Tracked
        {
            UObject* instance = nullptr;
            std::vector<std::wstring> seen;        // its lines at the last poll
            std::map<std::wstring, double> spoken; // the shapes said, by the time each was last on it
            int stablePolls = 0;
        };

        // A line's shape: its digits made alike, so that a counter or a clock that goes on
        // ticking is one line, said once with its first value.
        std::wstring Shape(const std::wstring& line)
        {
            std::wstring shape = line;
            for (auto& c : shape)
            {
                if (c >= L'0' && c <= L'9') c = L'#';
            }
            return shape;
        }
        std::vector<Tracked> g_tracked;
        std::set<std::wstring> g_classesSeen; // for the log: each kind of element once
        // How long a line that left the element is remembered as said.
        constexpr double kLineForgotten = 2.0;
        std::wstring g_lastText;
        double g_lastTextAt = -1.0;

        // The lines an element shows: the texts it draws now (not the ones still fading in,
        // nor the ones faded out), then the prompts of its key caps, one line each.
        std::vector<std::wstring> Lines(UObject* widget)
        {
            auto lines = ui::DrawnTexts(widget);
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

        // An element is read once its words have stopped changing: a caption is filled in a
        // moment after the element appears, and the same element can show several messages
        // in a row.
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
                    const auto lines = Lines(it->instance);
                    if (lines != it->seen)
                    {
                        it->seen = lines;
                        it->stablePolls = 0;
                    }
                    else if (++it->stablePolls >= 2)
                    {
                        // Only the lines that are new are said: a line that stays while
                        // others come and go (a title over a sequence of sentences) is said
                        // once; a line that went away and comes back is new again.
                        // A line that left and is back within a moment (the blinking colon
                        // of a clock) is the same line still.
                        std::vector<std::wstring> fresh;
                        std::set<std::wstring> present;
                        for (const auto& line : lines)
                        {
                            const auto shape = Shape(line);
                            if (!present.insert(shape).second) continue;
                            const auto known = it->spoken.find(shape);
                            if (known == it->spoken.end() || now - known->second > kLineForgotten) fresh.push_back(line);
                        }
                        for (const auto& shape : present)
                            it->spoken[shape] = now;
                        std::erase_if(it->spoken, [&](const auto& entry) { return now - entry.second > kLineForgotten; });
                        if (!fresh.empty())
                        {
                            const auto text = str::JoinSentences(fresh);
                            log::Info(L"hud: {} says \"{}\"", obj::ClassName(it->instance), text);
                            // The same words can arrive through two elements at once.
                            if (text != g_lastText || now - g_lastTextAt > 5.0)
                            {
                                g_lastText = text;
                                g_lastTextAt = now;
                                speech::Announce(text);
                            }
                        }
                    }
                }
                else
                {
                    // Hidden and shown again, it is read again.
                    it->spoken.clear();
                    it->seen.clear();
                }
                ++it;
            }
        }

        void Poll(float)
        {
            obj::SafeInvokeLogged(L"hud.PollImpl", [](void*) { PollImpl(); }, nullptr);
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
        gamethread::AddPoller(L"hud", &Poll);
    }

    void HudFeature::Describe(std::vector<std::wstring>& out)
    {
        for (const auto& t : g_tracked)
        {
            if (obj::IsLive(t.instance) && obj::IsWidgetShown(t.instance) && !t.seen.empty()) out.push_back(str::JoinSentences(t.seen));
        }
    }
}
