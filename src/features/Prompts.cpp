#include "features/Prompts.hpp"

#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/ParamReader.hpp"
#include "core/Strings.hpp"
#include "hooks/HookDispatcher.hpp"
#include "speech/Speech.hpp"
#include "ui/Widgets.hpp"
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
        // One prompt widget of the game's: an interaction prompt, or a glint whose prompt has
        // come up.
        struct Shown
        {
            UObject* widget = nullptr;
            bool glint = false;
            std::wstring text; // its readout at the last poll
            int stablePolls = 0;
            bool announced = false;
            bool promptUp = false; // a glint: its prompt is drawn
        };
        std::vector<Shown> g_shown;

        // The action bar in play: the prompts the game pins to the bottom of the screen while
        // no menu page is up (continue a lesson, hide a note's transcript). The menus read the
        // same bar while a page is up.
        // The bar in play answers the player's own press (a film's skip prompt comes up on the
        // first press and the second press skips), so it is read almost at once.
        constexpr double kBarStableSeconds = 0.1;
        std::vector<ui::Prompt> g_bar;
        std::set<std::wstring> g_barSaid; // the lines said while they stayed on the bar
        double g_barChangedAt = -1.0;

        Shown* Find(UObject* widget)
        {
            for (auto& s : g_shown)
            {
                if (s.widget == widget) return &s;
            }
            return nullptr;
        }

        void Track(UObject* widget, bool glint)
        {
            if (!widget || !obj::IsLive(widget) || Find(widget)) return;
            Shown s;
            s.widget = widget;
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

        // What a prompt widget says: each key cap's words with its key.
        std::wstring Readout(const Shown& s, std::wstring* actions)
        {
            if (!obj::IsWidgetShown(s.widget)) return {};
            if (s.glint && !s.promptUp) return {};
            const auto prompts = ui::PromptsIn(s.widget);
            if (actions) *actions = Actions(prompts);
            return ui::PromptLines(prompts);
        }

        // A glint's prompt fades in as the character comes close; it is up once the fade is done.
        void OnGlintPrompt(UObject* self, FFrame& stack)
        {
            double progress = 0.0;
            if (!params::Float(stack, L"InProgress", progress)) return;
            Track(self, true);
            Shown* s = Find(self);
            if (!s) return;
            const bool up = progress >= 0.99;
            if (up != s->promptUp)
            {
                s->promptUp = up;
                if (!up) s->announced = false;
                log::Verbose(L"prompts: glint {} prompt {}", obj::ObjectName(self), up ? L"up" : L"down");
            }
        }

        std::wstring Line(const ui::Prompt& p)
        {
            return ui::PromptLines({p});
        }

        void PollBar(double now)
        {
            // While a page is up the menus own the bar.
            if (watch::CurrentScreen())
            {
                g_bar.clear();
                g_barSaid.clear();
                g_barChangedAt = -1.0;
                return;
            }
            const auto prompts = ui::Prompts(nullptr);
            bool changed = prompts.size() != g_bar.size();
            for (size_t i = 0; !changed && i < prompts.size(); ++i)
                changed = Line(prompts[i]) != Line(g_bar[i]);
            if (!changed)
            {
                g_barChangedAt = -1.0;
                return;
            }
            // The bar is read once its prompts have stopped changing for a moment.
            if (g_barChangedAt < 0.0) g_barChangedAt = now;
            if (now - g_barChangedAt < kBarStableSeconds) return;
            g_barChangedAt = -1.0;
            g_bar = prompts;
            // A line that left the bar is said again when it comes back.
            std::set<std::wstring> current;
            std::vector<std::wstring> fresh;
            for (const auto& p : prompts)
            {
                const auto line = Line(p);
                if (line.empty()) continue;
                current.insert(line);
                if (!g_barSaid.count(line)) fresh.push_back(line);
            }
            g_barSaid = current;
            if (fresh.empty()) return;
            const auto text = str::Join(fresh, L", ");
            log::Info(L"prompts: action bar \"{}\" [{}]", text, Actions(prompts));
            speech::Announce(text);
        }

        void PollImpl()
        {
            if (gamethread::FrameCount() % 2 != 0) return;
            const double now = gamethread::NowSeconds();
            for (auto it = g_shown.begin(); it != g_shown.end();)
            {
                if (!obj::IsLive(it->widget))
                {
                    it = g_shown.erase(it);
                    continue;
                }
                std::wstring actions;
                const auto text = Readout(*it, &actions);
                if (text != it->text)
                {
                    it->text = text;
                    it->stablePolls = 0;
                    it->announced = false;
                }
                else if (!text.empty() && !it->announced && ++it->stablePolls >= 2)
                {
                    it->announced = true;
                    log::Info(L"prompts: {} \"{}\" [{}]", obj::ClassName(it->widget), text, actions);
                    speech::Announce(text);
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
        watch::OnWidgetClass(L"BatesInteractionWidget",
                             [](UObject* widget, bool appeared)
                             {
                                 if (appeared) Track(widget, false);
                             });
        hooks::OnScript(L"BatesGlintWidget", L"BP_UpdateShowPrompt", &OnGlintPrompt);
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
            const auto bar = ui::PromptLines(ui::Prompts(nullptr));
            if (!bar.empty()) out.push_back(bar);
        }
    }
}
