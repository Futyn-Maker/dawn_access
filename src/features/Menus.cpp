#include "features/Menus.hpp"

#include "features/MenuRules.hpp"

#include "core/Config.hpp"
#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "input/InputNames.hpp"
#include "locale/Locale.hpp"
#include "speech/Speech.hpp"
#include "ui/Describe.hpp"
#include "ui/Widgets.hpp"
#include "watch/Narration.hpp"
#include "watch/Watchers.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <set>

namespace da::features
{
    using RC::Unreal::UObject;

    namespace
    {
        // The screen on display and the control in focus, as last read.
        obj::ObjectKey g_screen;
        unsigned long long g_screenGeneration = 0; // the display generation the screen was read at
        obj::ObjectKey g_control;
        std::wstring g_title;                           // the page's title as read, so that a page refilled under the same widget is read again
        std::wstring g_arrivalText;                     // what the page's arrival said, so that the same page shown anew with the same words is not read again
        std::wstring g_controlLabel;                    // the focused control's label as read, for the same reason
        std::wstring g_group;                           // the heading last said
        menu::Value g_value;                            // the focused control's value, as said and paced (menu::Value)
        std::vector<ui::Prompt> g_prompts;              // the prompts last seen
        std::set<std::wstring> g_promptsSaid;           // the prompts said on this screen, as label and key
        std::set<std::wstring> g_bodySaid;              // the body texts said on this screen, digits aside
        std::wstring g_pageName;                        // the page's name as its arrival said it
        std::map<obj::ObjectKey, double> g_opacitySeen; // the highest opacity seen of each text of the screen
        menu::Steady g_promptsSteady;                   // a change of the prompts standing (menu::Steady)
        menu::Arrival g_arrival;                        // an arrival waiting for the page to settle (menu::Arrival)

        std::wstring PromptKey(const ui::Prompt& prompt)
        {
            return prompt.label + L"\n" + prompt.key;
        }

        // The highest opacity each text of the page has shown while the page is on display,
        // sampled as the page is watched: a text that pulses is read, a text kept faded is not.
        void SampleOpacity(UObject* page)
        {
            obj::WalkWidgetTree(page, 48,
                                [&](UObject* w, int)
                                {
                                    if (!obj::IsWidgetVisible(w)) return false;
                                    if (obj::IsA(w, L"TextBlock") || obj::IsA(w, L"RichTextBlock"))
                                    {
                                        auto& seen = g_opacitySeen[obj::KeyOf(w)];
                                        seen = std::max(seen, obj::WidgetOpacity(w));
                                    }
                                    return true;
                                });
        }

        // A text's opacity as the page's texts are walked, the highest seen kept (SampleOpacity's
        // record): the walk that reads the page samples it too, and needs no walk of its own.
        double OpacitySeen(UObject* text)
        {
            const double now = obj::WidgetOpacity(text);
            auto& seen = g_opacitySeen[obj::KeyOf(text)];
            seen = std::max(seen, now);
            return seen;
        }

        std::wstring TabLine(UObject* page)
        {
            ui::Tab tab;
            if (!ui::TabOf(page, tab)) return {};
            return ui::SpeakTab(tab.name, tab.index, tab.count, cfg::Detailed());
        }

        // Everything a screen says on arrival, in reading order; the page's texts as walked are
        // handed back in `walked`, for the record of what was said.
        std::wstring ScreenText(UObject* page, UObject* control, bool withPrompts, std::vector<std::wstring>* walked = nullptr)
        {
            std::vector<std::wstring> parts;
            const auto tab = TabLine(page);
            if (!tab.empty()) parts.push_back(tab);
            // A page that draws no title of its own is named as the game's narration names it.
            auto title = ui::ScreenTitle(page);
            if (title.empty()) title = narration::PageNameFor(page);
            if (!title.empty()) parts.push_back(title);
            g_pageName = title;
            // The body without the name, where a widget of the page draws it as a text.
            std::vector<std::wstring> body;
            const auto texts = ui::ScreenBodyTexts(page, &OpacitySeen);
            for (const auto& text : texts)
            {
                if (title.empty() || menu::Shape(text) != menu::Shape(title)) body.push_back(text);
            }
            if (walked) *walked = texts;
            if (!body.empty()) parts.push_back(str::JoinSentences(body));
            if (control)
            {
                const auto spoken = ui::Speak(ui::Describe(control), cfg::Detailed(), true);
                if (!spoken.empty()) parts.push_back(spoken);
            }
            if (withPrompts)
            {
                const auto prompts = ui::SpeakPrompts(ui::Prompts(page));
                if (!prompts.empty()) parts.push_back(prompts);
            }
            return str::JoinSentences(parts);
        }

        UObject* FocusedControl()
        {
            return ui::Interactable(watch::CurrentFocused());
        }

        void RememberPrompts(UObject* page)
        {
            g_prompts = ui::Prompts(page);
            g_promptsSaid.clear();
            for (const auto& prompt : g_prompts)
                g_promptsSaid.insert(PromptKey(prompt));
            g_promptsSteady.Reset();
        }

        void RememberBodyTexts(const std::vector<std::wstring>& body)
        {
            g_bodySaid.clear();
            for (const auto& text : body)
                g_bodySaid.insert(menu::Shape(text));
            // The page's name, as said on arrival, is not news when its widget draws it later.
            if (!g_pageName.empty()) g_bodySaid.insert(menu::Shape(g_pageName));
        }

        void RememberBody(UObject* page)
        {
            RememberBodyTexts(ui::ScreenBodyTexts(page, &OpacitySeen));
        }

        void RememberControl(UObject* control, const ui::Description* d)
        {
            g_control = obj::KeyOf(control);
            g_controlLabel = d ? d->label : std::wstring();
            g_group = d ? d->group : std::wstring();
            g_value.Known(d ? d->value : std::wstring());
        }

        void SpeakArrival(UObject* page, bool anew)
        {
            UObject* control = FocusedControl();
            ui::Description d;
            if (control) d = ui::Describe(control);
            std::vector<std::wstring> body;
            const auto text = ScreenText(page, control, true, &body);
            log::Verbose(L"menus: arrival on {} {} focus {} {}", obj::ClassName(page), obj::ObjectName(page), obj::ClassName(control),
                         obj::ObjectName(control));
            // The same page shown anew says nothing new when its words are the ones just said:
            // its stack displayed it a moment after it was read, or re-displayed it as it was.
            if (anew && text == g_arrivalText)
                log::Verbose(L"menus: shown anew with the same words, not read again");
            else if (!text.empty())
                speech::Focus(text);
            g_arrivalText = text;
            g_screen = obj::KeyOf(page);
            g_screenGeneration = watch::DisplayGeneration();
            g_title = ui::ScreenTitle(page);
            RememberControl(control, control ? &d : nullptr);
            RememberPrompts(page);
            RememberBodyTexts(body);
        }

        // The prompts not said on this screen before, as a line; the ones already said are
        // the player's to remember.
        std::wstring NewPrompts(UObject* page)
        {
            const auto prompts = ui::Prompts(page);
            std::vector<ui::Prompt> fresh;
            for (const auto& prompt : prompts)
            {
                if (g_promptsSaid.insert(PromptKey(prompt)).second) fresh.push_back(prompt);
            }
            g_prompts = prompts;
            return ui::SpeakPrompts(fresh);
        }

        void SpeakControl(UObject* page, UObject* control, const std::vector<std::wstring>& before = {})
        {
            const auto d = ui::Describe(control);
            const std::wstring lastGroup = g_group;
            RememberControl(control, &d);
            std::vector<std::wstring> parts = before;
            const auto said = ui::Speak(d, cfg::Detailed(), d.group != lastGroup);
            if (!said.empty()) parts.push_back(said);
            const auto spoken = str::JoinSentences(parts);
            if (spoken.empty()) return;
            speech::Focus(spoken);
            // The prompts a control brings with it are said after it when they are new here.
            const auto fresh = NewPrompts(page);
            if (!fresh.empty()) speech::Announce(fresh);
        }

        void Forget()
        {
            g_screen = {};
            g_screenGeneration = 0;
            g_control = {};
            g_title.clear();
            g_arrivalText.clear();
            g_controlLabel.clear();
            g_group.clear();
            g_value.Forget();
            g_prompts.clear();
            g_promptsSaid.clear();
            g_bodySaid.clear();
            g_pageName.clear();
            g_opacitySeen.clear();
            g_promptsSteady.Reset();
            g_arrival.Clear();
        }

        void Poll(float)
        {
            const double now = gamethread::NowSeconds();
            UObject* page = watch::CurrentScreen();
            if (!page)
            {
                if (g_screen.object) Forget();
                return;
            }
            const obj::ObjectKey pageKey = obj::KeyOf(page);
            // A page shown anew in the same widget, after another took its place, is an arrival
            // too; so is a page refilled under the same widget, which shows as its title changing.
            const bool shownAnew =
                pageKey == g_screen && !g_arrival.Pending() && (watch::DisplayGeneration() != g_screenGeneration || ui::ScreenTitle(page) != g_title);
            if (pageKey != g_screen || shownAnew)
            {
                if (g_arrival.Begin(now, shownAnew))
                {
                    g_opacitySeen.clear();
                    g_screen = {};
                    log::Verbose(L"menus: new screen {} {}", obj::ClassName(page), obj::ObjectName(page));
                }
                // A text that pulses while the page settles is sampled every fourth frame: a walk of
                // the whole page each frame cost up to 60 ms on the collectibles' pages
                // (research/sessions/s65-play.log, 14:28 to 14:30).
                if (gamethread::FrameCount() % 4 == 0) SampleOpacity(page);
                // The page is read once its focus has landed and its stack displays it, or after
                // the longest wait (menu::Arrival).
                bool anew = false;
                if (g_arrival.Due(now, [&] { return FocusedControl() != nullptr && obj::IsWidgetShown(page, false); }, anew)) SpeakArrival(page, anew);
                return;
            }
            g_arrival.Clear();

            UObject* control = FocusedControl();
            const obj::ObjectKey controlKey = obj::KeyOf(control);
            if (control && controlKey != g_control)
            {
                // Come from another group of controls, the focus brings the page's texts that came
                // with it, a question over its buttons, before the control (menu::TextsWithFocus).
                const bool newGroup = !obj::IsSame(g_control) || !ui::SameGroup(control, g_control.object);
                std::vector<std::wstring> before;
                if (newGroup)
                {
                    // One walk of the page's texts serves both: what came with the control, and what
                    // is remembered as said. A move within the group walks nothing: the periodic
                    // check says what the page gained, as it always did.
                    const auto body = ui::ScreenBodyTexts(page, &OpacitySeen);
                    before = menu::TextsWithFocus(newGroup, body, g_bodySaid);
                    RememberBodyTexts(body);
                }
                if (!before.empty())
                    log::Info(L"menus: the focus came to {} {} from another group, with the page's new text \"{}\": said first", obj::ClassName(control),
                              obj::ObjectName(control), str::JoinSentences(before));
                SpeakControl(page, control, before);
                return;
            }

            // Every eighth frame: the control's value, the page's texts and the prompts.
            if (gamethread::FrameCount() % 8 != 0) return;
            if (control)
            {
                // A value that moves under a held key is paced (menu::Value).
                if (const auto said = g_value.Update(now, ui::ValueOf(control)); !said.empty()) speech::Focus(said);
            }
            if (gamethread::FrameCount() % 24 == 0)
            {
                // The same widget given another control's words, as a pooled row is when the
                // game refills it, is a control the player has not heard.
                if (control && !g_controlLabel.empty())
                {
                    const auto label = ui::LabelOf(control);
                    if (!label.empty() && label != g_controlLabel)
                    {
                        SpeakControl(page, control);
                        RememberBody(page);
                        return;
                    }
                }
                // A text the page gained since it was read is said once, as the game's own
                // message; a count that goes on ticking is one text. The walk samples the texts'
                // opacity as it goes (OpacitySeen).
                for (const auto& text : ui::ScreenBodyTexts(page, &OpacitySeen))
                {
                    if (g_bodySaid.insert(menu::Shape(text)).second) speech::Announce(text);
                }
            }
            const auto prompts = ui::Prompts(page);
            bool changed = prompts.size() != g_prompts.size();
            for (size_t i = 0; !changed && i < prompts.size(); ++i)
                changed = PromptKey(prompts[i]) != PromptKey(g_prompts[i]);
            if (g_promptsSteady.Stood(now, changed))
            {
                const auto fresh = NewPrompts(page);
                if (!fresh.empty()) speech::Announce(fresh);
            }
        }

        void PollGuarded(float dt)
        {
            struct Context
            {
                float dt;
            } context{dt};
            const auto started = std::chrono::steady_clock::now();
            obj::SafeInvokeLogged(L"menus.Poll", [](void* c) { Poll(static_cast<Context*>(c)->dt); }, &context);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            if (ms > 8.0) log::Verbose(L"menus: a poll took {:.1f} ms on frame {}", ms, gamethread::FrameCount());
        }
    }

    void MenusFeature::Install()
    {
        gamethread::AddPoller(L"menus", &PollGuarded);
    }

    void MenusFeature::Describe(std::vector<std::wstring>& out)
    {
        UObject* page = watch::CurrentScreen();
        if (!page) return;
        const auto text = ScreenText(page, FocusedControl(), true);
        if (!text.empty()) out.push_back(text);
    }

    void MenusFeature::Help(std::vector<std::wstring>& out)
    {
        UObject* page = watch::CurrentScreen();
        if (!page) return;
        out.push_back(locale::Mod(L"help.menu", input::KeyForAction(L"IA_UI_Confirm"), input::KeyForAction(L"IA_UI_Back")));
        ui::Tab tab;
        if (ui::TabOf(page, tab)) out.push_back(locale::Mod(L"ui.prompt.tabs", input::KeyForAction(L"IA_UI_TabLeft"), input::KeyForAction(L"IA_UI_TabRight")));
        UObject* control = FocusedControl();
        const ui::Kind kind = ui::KindOf(control);
        if (kind == ui::Kind::Selector || kind == ui::Kind::Slider)
            out.push_back(locale::Mod(L"ui.prompt.selector", input::KeyDisplayName(L"Left"), input::KeyDisplayName(L"Right")));
    }

    void ResetMenus()
    {
        Forget();
    }
}
