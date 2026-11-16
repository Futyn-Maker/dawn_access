#include "ui/Describe.hpp"

#include "core/Strings.hpp"
#include "locale/Locale.hpp"

namespace da::ui
{
    namespace
    {
        std::wstring KindWord(Kind kind)
        {
            switch (kind)
            {
            case Kind::Selector: return locale::Mod(L"ui.type.selector");
            case Kind::Slider: return locale::Mod(L"ui.type.slider");
            case Kind::Checkbox: return locale::Mod(L"ui.type.checkbox");
            case Kind::Tab: return locale::Mod(L"ui.type.tab");
            // A button is the common case and goes unnamed.
            default: return {};
            }
        }
    }

    std::wstring Speak(const Description& d, bool detailed, bool sayGroup)
    {
        std::vector<std::wstring> parts;
        if (sayGroup && !d.group.empty() && !d.narrated) parts.push_back(d.group);
        std::vector<std::wstring> words;
        words.push_back(d.label);
        if (detailed)
        {
            const auto kind = KindWord(d.kind);
            if (!kind.empty()) words.push_back(kind);
        }
        if (!d.value.empty()) words.push_back(d.value);
        // A control the game has switched off says so at either level: the state is the game's
        // own, drawn as the row greyed out, as its label and value are, and a row heard without it
        // would seem to take a press.
        if (!d.state.empty()) words.push_back(d.state);
        // The place is a detail, except for a control with no words of its own, which has
        // nothing else to be told apart by.
        const bool nameless = d.label.empty() && d.value.empty();
        // A place is worth a word only among several.
        if ((detailed || nameless) && d.index > 0 && d.count > 1) words.push_back(locale::Mod(L"ui.pos", std::to_wstring(d.index), std::to_wstring(d.count)));
        parts.push_back(str::Join(words, L", "));
        if (!d.tip.empty()) parts.push_back(d.tip);
        return str::JoinSentences(parts);
    }

    std::wstring SpeakValue(const Description& d)
    {
        return d.value;
    }

    std::wstring SpeakPrompts(const std::vector<Prompt>& prompts)
    {
        std::vector<std::wstring> parts;
        for (const auto& prompt : prompts)
        {
            if (prompt.label.empty()) continue;
            parts.push_back(prompt.key.empty() ? prompt.label : prompt.label + L": " + prompt.key);
        }
        return str::Join(parts, L", ");
    }

    std::wstring SpeakTab(const std::wstring& name, int index, int count, bool detailed)
    {
        std::vector<std::wstring> words;
        words.push_back(name);
        if (detailed)
        {
            words.push_back(locale::Mod(L"ui.type.tab"));
            if (index > 0 && count > 0) words.push_back(locale::Mod(L"ui.pos", std::to_wstring(index), std::to_wstring(count)));
        }
        return str::Join(words, L", ");
    }
}
