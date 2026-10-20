#pragma once
// The game's menu widgets, read by reflection: which user widget is the control the focus
// sits on, its label, kind, value, state and place among its neighbours, the heading above
// it, the description the game shows for it; a page's title and body text; the tab a page
// is in; and the prompts of the action bar with their keys.

#include "ui/Describe.hpp"

#include <Unreal/UObject.hpp>

#include <functional>
#include <string>
#include <vector>

namespace da::ui
{
    using RC::Unreal::UObject;

    // True for the widgets the focus lands on: the game's buttons, including its setting
    // rows, but not the prompt buttons of the action bar or the arrows inside a row.
    bool IsInteractable(UObject* widget);

    // The control a focused widget belongs to: the outermost of the game's buttons above it,
    // the widget itself when it is one, or the focused user widget itself when the game put
    // its focus on something that is no button of its own.
    UObject* Interactable(UObject* focused);

    Kind KindOf(UObject* control);

    // Everything said of a control: label, kind, value, state, place among the controls
    // beside it, the heading above it and its description line.
    Description Describe(UObject* control);
    // The label alone, as Describe would find it: cheap enough to ask every few frames.
    std::wstring LabelOf(UObject* control);

    // The value a control shows right now, as a string: the selected entry, the slider's
    // text, the checkbox's state. Empty for a plain button.
    std::wstring ValueOf(UObject* control);

    // The title a page shows, when it has one (the heading of a modal). Empty otherwise.
    std::wstring ScreenTitle(UObject* page);

    // The opacity a text is taken to have; the caller may answer with the highest it has
    // seen, so that a text that pulses is not taken for one kept faded.
    using OpacityOf = std::function<double(UObject* textWidget)>;

    // The static texts of a page besides its title, its controls and the headings of its
    // lists: the message of a modal, a line of small print, each once. Text the page keeps
    // faded out, or has not filled in yet, is left out.
    std::vector<std::wstring> ScreenBodyTexts(UObject* page, const OpacityOf& opacityOf = nullptr);
    std::wstring ScreenBody(UObject* page, const OpacityOf& opacityOf = nullptr);

    // The tab a page is shown under, when it belongs to a group of tabs: its name, its place
    // and the number of tabs. False when the page is not in a tab group.
    struct Tab
    {
        std::wstring name;
        int index = 0;
        int count = 0;
    };
    bool TabOf(UObject* page, Tab& out);

    // The prompts of the action bar, in their order, each with the spoken keys of its action,
    // and, for a page among tabs, the keys that turn the tabs.
    std::vector<Prompt> Prompts(UObject* page);
}
