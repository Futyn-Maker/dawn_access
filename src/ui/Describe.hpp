#pragma once
// What is said of a control and of a screen's prompts, composed from the pieces the widget
// layer reads: the words and their order live here, away from the engine, so that they can be
// tested.

#include <string>
#include <vector>

namespace da::ui
{
    enum class Kind
    {
        Button,
        Selector,
        Slider,
        Checkbox,
        Tab,
        Other,
    };

    struct Description
    {
        Kind kind = Kind::Other;
        std::wstring group; // the heading the control sits under, when it has one
        std::wstring label;
        std::wstring value; // selectors, sliders, checkboxes
        std::wstring state; // "unavailable", "locked"
        int index = 0;      // 1-based among the controls beside it, 0 when unknown
        int count = 0;
        std::wstring tip;      // the description line the game shows for it
        bool narrated = false; // the label is the game's own narration of the control
    };

    struct Prompt
    {
        std::wstring label;
        std::wstring key;
        std::wstring action; // the game's input action behind the key, for the log
    };

    // The spoken form of a control. At the detailed level the kind, the position and the
    // state are said; briefly only the label, the value and the tip. A changed group is said
    // first, as the heading it is, except before the game's own narration of the control, which
    // names what the control belongs to in its own words: a totem slot's is "Тотем смерти, 6,
    // Недоступно" under the caption "Смерть".
    std::wstring Speak(const Description& description, bool detailed, bool sayGroup);

    // The value alone, as said when it changes under the focus.
    std::wstring SpeakValue(const Description& description);

    // "Select: Enter, Back: Backspace".
    std::wstring SpeakPrompts(const std::vector<Prompt>& prompts);

    // "Accessibility, tab, 2 of 5".
    std::wstring SpeakTab(const std::wstring& name, int index, int count, bool detailed);
}
