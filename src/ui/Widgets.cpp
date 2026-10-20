#include "ui/Widgets.hpp"

#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "input/InputNames.hpp"
#include "locale/GameText.hpp"
#include "locale/Locale.hpp"

#include <algorithm>
#include <set>

namespace da::ui
{
    namespace
    {
        // Text drawn this faint is a status the page keeps in the background, not a line the
        // player is meant to read.
        constexpr double kFadedOpacity = 0.6;

        bool IsButton(UObject* widget)
        {
            return widget && obj::IsA(widget, L"BMUIButton");
        }

        // The buttons that only carry a prompt into the action bar: the bar's own entries, and
        // the invisible helpers a page keeps for the same purpose.
        bool IsPromptButton(UObject* widget)
        {
            return obj::IsA(widget, L"BMUIActionBarButton") || str::StartsWith(obj::ClassName(widget), L"WBP_ActionBarOnlyButton");
        }

        bool IsTextWidget(UObject* widget)
        {
            return obj::IsA(widget, L"TextBlock") || obj::IsA(widget, L"RichTextBlock") || obj::IsA(widget, L"EditableTextBox") ||
                   obj::IsA(widget, L"EditableText") || obj::IsA(widget, L"MultiLineEditableText");
        }

        // The widget that draws the key of an action: its key cap prints the key's letter as
        // text, which is not a word of any label.
        bool IsKeyIcon(UObject* widget)
        {
            return obj::IsA(widget, L"BMUIInputActionWidget");
        }

        // The names of the text widgets a settings page fills in for the focused row: its name,
        // its description and its default value. They are said with the row, not as the page.
        bool IsBlurbWidget(UObject* widget)
        {
            const std::wstring name = obj::ObjectName(widget);
            return name == L"Text_SettingName" || name == L"Text_SettingBlurb" || name == L"Text_DefaultValue";
        }

        // The panel of a settings page that describes the focused row: the one that holds the
        // row's name and its default value, with whatever example the game draws between them.
        // Everything in it belongs to the row.
        UObject* BlurbPanel(UObject* page)
        {
            UObject* name = nullptr;
            UObject* value = nullptr;
            obj::WalkWidgetTree(page, 48,
                                [&](UObject* w, int)
                                {
                                    const std::wstring n = obj::ObjectName(w);
                                    if (n == L"Text_SettingName") name = w;
                                    if (n == L"Text_DefaultValue") value = w;
                                    return !(name && value);
                                });
            if (!name || !value) return nullptr;
            // The nearest panel above the name that also holds the default value.
            for (UObject* cur = obj::ParentWidget(name); cur && cur != page; cur = obj::ParentWidget(cur))
            {
                if (obj::IsA(cur, L"PanelWidget") && obj::IsDescendantOf(value, cur)) return cur;
            }
            return nullptr;
        }

        // A text the game has not filled in: its placeholders are still in it, or it is the
        // engine's default for a text block nobody has set.
        bool IsTemplate(const std::wstring& text)
        {
            return text.find(L"{0}") != std::wstring::npos || text.find(L"{1}") != std::wstring::npos || text == L"Text Block" ||
                   text == L"TextBlockDefaultValue";
        }

        bool HasVisibleText(UObject* widget);

        // Whether a panel holds controls among its children, and whether any of them carries
        // words of its own.
        void ControlsIn(UObject* panel, bool& any, bool& labelled)
        {
            any = false;
            labelled = false;
            for (auto* child : obj::PanelChildren(panel))
            {
                if (!IsButton(child) || IsPromptButton(child)) continue;
                any = true;
                if (HasVisibleText(child)) labelled = true;
            }
        }

        // A panel that lays out controls: whatever text it holds beside them heads them, and
        // is said with the control it heads rather than with the page. A panel whose child
        // panel holds wordless controls (a row of pictures) heads them the same way; a text
        // above a row of labelled buttons is the page's own, such as the question of a modal.
        bool IsListPanel(UObject* widget)
        {
            if (!obj::IsA(widget, L"PanelWidget")) return false;
            bool any = false;
            bool labelled = false;
            ControlsIn(widget, any, labelled);
            if (any) return true;
            for (auto* child : obj::PanelChildren(widget))
            {
                if (!obj::IsA(child, L"PanelWidget")) continue;
                ControlsIn(child, any, labelled);
                if (any && !labelled) return true;
            }
            return false;
        }

        // The texts a widget shows, walking as deep as the game's layouts go. Controls below
        // the root are left out when asked, key caps always, faded texts when asked; a text
        // that is hidden is taken only when asked.
        struct TextWalk
        {
            bool skipControls = false;
            bool skipFaded = false;
            bool includeHidden = false;
        };

        std::vector<std::wstring> Texts(UObject* root, const TextWalk& walk)
        {
            std::vector<std::wstring> texts;
            obj::WalkWidgetTree(root, 48,
                                [&](UObject* w, int depth)
                                {
                                    if (!walk.includeHidden && !obj::IsWidgetVisible(w)) return false;
                                    if (depth > 0 && walk.skipControls && IsButton(w)) return false;
                                    if (IsKeyIcon(w)) return false;
                                    if (IsTextWidget(w))
                                    {
                                        if (walk.skipFaded && obj::WidgetOpacity(w) < kFadedOpacity) return false;
                                        const auto text = str::StripMarkup(obj::TextOf(w));
                                        if (!text.empty() && !IsTemplate(text)) texts.push_back(text);
                                    }
                                    return true;
                                });
            return texts;
        }

        std::vector<std::wstring> OwnTexts(UObject* root, bool skipControls, bool skipFaded)
        {
            return Texts(root, TextWalk{skipControls, skipFaded, false});
        }

        bool HasVisibleText(UObject* widget)
        {
            return !OwnTexts(widget, false, false).empty();
        }

        // The text found in a widget's own text block of the given name, drawn or not.
        std::wstring NamedText(UObject* root, std::wstring_view name)
        {
            std::wstring found;
            obj::WalkWidgetTree(root, 48,
                                [&](UObject* w, int)
                                {
                                    if (!found.empty()) return false;
                                    if (!obj::IsWidgetVisible(w)) return false;
                                    if (IsTextWidget(w) && obj::ObjectName(w) == name)
                                    {
                                        found = str::StripMarkup(obj::TextOf(w));
                                        return false;
                                    }
                                    return true;
                                });
            return found;
        }

        // The text the game attaches to a widget for its own narration, when it attached one.
        std::wstring AccessibleText(UObject* widget)
        {
            UObject* data = nullptr;
            if (!obj::ReadObject(widget, L"AccessibleWidgetData", data) || !data || !obj::IsLive(data)) return {};
            std::wstring text;
            obj::ReadString(data, L"AccessibleText", text);
            return str::StripMarkup(text);
        }

        // The names of a picture: the texture or material behind an image and, for a material
        // instance, the textures set as its parameters, through its parents.
        void PictureNamesOf(UObject* resource, std::vector<std::wstring>& names)
        {
            for (int depth = 0; resource && obj::IsLive(resource) && depth < 4; ++depth)
            {
                names.push_back(obj::ObjectName(resource));
                if (!obj::IsA(resource, L"MaterialInstance")) break;
                obj::ForEachArrayElement(resource, obj::FindProperty(resource, L"TextureParameterValues"),
                                         [&](void* element, obj::FProperty* inner)
                                         {
                                             UObject* texture = nullptr;
                                             if (obj::ReadObjectAt(element, obj::StructMember(inner, L"ParameterValue"), texture) && texture &&
                                                 obj::IsLive(texture))
                                                 names.push_back(obj::ObjectName(texture));
                                         });
                UObject* parent = nullptr;
                if (!obj::ReadObject(resource, L"Parent", parent)) break;
                resource = parent;
            }
        }

        // The pictures a widget draws, by every name they go under.
        std::vector<std::wstring> PictureNames(UObject* root)
        {
            std::vector<std::wstring> names;
            obj::WalkWidgetTree(root, 48,
                                [&](UObject* w, int)
                                {
                                    if (!obj::IsWidgetVisible(w)) return false;
                                    if (!obj::IsA(w, L"Image")) return true;
                                    if (obj::WidgetOpacity(w) <= 0.02) return true;
                                    auto* brush = obj::FindProperty(w, L"Brush");
                                    UObject* resource = nullptr;
                                    if (brush && obj::ReadObjectAt(obj::ValuePtr(w, brush), obj::StructMember(brush, L"ResourceObject"), resource))
                                        PictureNamesOf(resource, names);
                                    return true;
                                });
            return names;
        }

        // What the mod's picture table says of a picture: its own words, or, when the entry
        // names one of the game's strings with "@", that string in the game's language.
        std::wstring PictureText(const std::wstring& pictureName)
        {
            const std::wstring key = L"picture." + str::ToLower(pictureName);
            if (!locale::Has(key)) return {};
            const std::wstring value = locale::Mod(key);
            if (str::StartsWith(value, L"@")) return gametext::Resolve(value.substr(1));
            return value;
        }

        // The panels a grid of controls is laid out in: the panel of the control and, when that
        // panel is one of several alike beside it that all hold controls, those panels too, in
        // their order.
        std::vector<UObject*> Columns(UObject* panel)
        {
            std::vector<UObject*> columns{panel};
            UObject* grid = obj::ParentWidget(panel);
            if (!grid || !obj::IsA(grid, L"PanelWidget")) return columns;
            std::vector<UObject*> alike;
            for (auto* child : obj::PanelChildren(grid))
            {
                if (!obj::IsA(child, L"PanelWidget") || !obj::IsWidgetShown(child)) continue;
                bool any = false;
                bool labelled = false;
                ControlsIn(child, any, labelled);
                if (any) alike.push_back(child);
            }
            if (alike.size() >= 2 && std::find(alike.begin(), alike.end(), panel) != alike.end()) return alike;
            return columns;
        }

        // The widgets laid out beside a control, in reading order: the children of its panel,
        // or of every column of its grid.
        std::vector<UObject*> Siblings(UObject* control, UObject*& panel, UObject*& grid)
        {
            panel = obj::ParentWidget(control);
            grid = nullptr;
            if (!panel || !obj::IsA(panel, L"PanelWidget")) return {control};
            const auto columns = Columns(panel);
            if (columns.size() > 1) grid = obj::ParentWidget(panel);
            std::vector<UObject*> siblings;
            for (auto* column : columns)
            {
                for (auto* child : obj::PanelChildren(column))
                    siblings.push_back(child);
            }
            return siblings;
        }

        // The text that names a row of pictures, which sits one level above them: the last text
        // before the row among the children of the panel above it, else the first text after
        // it, a caption under the row.
        std::wstring HeadingAbove(UObject* node)
        {
            UObject* above = obj::ParentWidget(node);
            if (!above || !obj::IsA(above, L"PanelWidget")) return {};
            std::wstring before;
            std::wstring after;
            bool passed = false;
            for (auto* child : obj::PanelChildren(above))
            {
                if (child == node)
                {
                    passed = true;
                    continue;
                }
                if (!obj::IsWidgetShown(child) || IsInteractable(child) || IsListPanel(child)) continue;
                const auto texts = OwnTexts(child, true, false);
                if (texts.empty()) continue;
                if (!passed)
                    before = str::Join(texts, L", ");
                else if (after.empty())
                    after = str::Join(texts, L", ");
            }
            return before.empty() ? after : before;
        }

        bool Enabled(UObject* control)
        {
            bool enabled = true;
            if (obj::ReadBool(control, L"bIsEnabled", enabled) && !enabled) return false;
            return true;
        }

        // The name of a control: what a setting row calls itself; else the text it shows; else
        // what the game attached for its narration; else what the mod's picture table says of
        // the picture it shows.
        std::wstring Label(UObject* control)
        {
            std::wstring label;
            if (obj::IsA(control, L"BMUISettingButton") && obj::ReadString(control, L"SettingName", label) && !str::Trim(label).empty())
                return str::StripMarkup(label);
            const auto texts = OwnTexts(control, false, false);
            if (obj::IsA(control, L"BMUISettingCarousel") || obj::IsA(control, L"BMUISettingSlider"))
            {
                // The row's own texts are its name and its value; the name comes first.
                return texts.empty() ? std::wstring() : texts.front();
            }
            if (!texts.empty()) return str::Join(texts, L", ");
            label = AccessibleText(control);
            if (!label.empty()) return label;
            for (const auto& picture : PictureNames(control))
            {
                label = PictureText(picture);
                if (!label.empty()) return label;
            }
            return {};
        }

        double SliderValue(UObject* control)
        {
            double value = 0.0;
            obj::CallReturn(control, L"GetValue",
                            [&](void* params, obj::FProperty* returnValue)
                            {
                                if (returnValue) obj::ReadFloatAt(params, returnValue, value);
                            });
            return value;
        }

        // The keys the icon of a prompt shows: one key, or one for each direction of an axis
        // (left and right, up and down), which the icon draws as that many key caps.
        std::wstring PromptKeys(UObject* icon, const std::wstring& action)
        {
            int caps = 0;
            for (const wchar_t* name : {L"XPosInternalWidget", L"XNegInternalWidget", L"YPosInternalWidget", L"YNegInternalWidget"})
            {
                UObject* cap = nullptr;
                if (obj::ReadObject(icon, name, cap) && cap && obj::IsLive(cap) && obj::IsWidgetShown(cap)) ++caps;
            }
            if (caps <= 1)
            {
                const auto key = input::KeyForAction(action);
                if (!key.empty()) return key;
                // The letter printed on the cap, when the action is not one the game maps.
                const auto printed = NamedText(icon, L"Text_Key");
                return printed.empty() ? std::wstring() : input::KeyDisplayName(printed);
            }
            return str::Join(input::KeysForAction(action, caps), L", ");
        }
    }

    bool IsInteractable(UObject* widget)
    {
        return IsButton(widget) && !IsPromptButton(widget);
    }

    UObject* Interactable(UObject* focused)
    {
        UObject* found = nullptr;
        for (UObject* cur = focused; cur; cur = obj::ParentWidget(cur))
        {
            if (IsInteractable(cur)) found = cur;
            if (obj::IsA(cur, L"BMUIPage")) break;
        }
        // Whatever the focus landed on is described when it is no button of the game's.
        if (!found && focused && obj::IsA(focused, L"UserWidget") && !IsPromptButton(focused)) found = focused;
        return found;
    }

    Kind KindOf(UObject* control)
    {
        if (!control) return Kind::Other;
        if (obj::IsA(control, L"BMUISettingCarousel")) return Kind::Selector;
        if (obj::IsA(control, L"BMUISettingSlider")) return Kind::Slider;
        if (obj::IsA(control, L"BMUISettingCheckBox")) return Kind::Checkbox;
        if (obj::IsA(control, L"BatesTabGroupButton")) return Kind::Tab;
        if (IsButton(control)) return Kind::Button;
        return Kind::Other;
    }

    std::wstring ValueOf(UObject* control)
    {
        switch (KindOf(control))
        {
        case Kind::Selector: return str::StripMarkup(obj::CallForText(control, L"GetCurrentEntryText"));
        case Kind::Slider:
        {
            // The text the row draws beside its slider, else the number itself.
            UObject* text = nullptr;
            if (obj::ReadObject(control, L"Text_SliderValue", text) && text && obj::IsLive(text))
            {
                const auto drawn = str::StripMarkup(obj::TextOf(text));
                if (!drawn.empty()) return drawn;
            }
            return std::format(L"{:g}", SliderValue(control));
        }
        case Kind::Checkbox:
        {
            const bool on = obj::CallForBool(control, L"GetValue");
            std::wstring text;
            obj::ReadString(control, on ? L"CheckEnabledText" : L"CheckDisabledText", text);
            text = str::StripMarkup(text);
            return text.empty() ? locale::Mod(on ? L"ui.checked" : L"ui.unchecked") : text;
        }
        default: return {};
        }
    }

    Description Describe(UObject* control)
    {
        Description d;
        if (!control) return d;
        d.kind = KindOf(control);
        d.label = Label(control);
        d.value = ValueOf(control);
        if (!Enabled(control)) d.state = locale::Mod(L"ui.state.disabled");

        // The place among the controls beside it, and the heading last seen above it. Wordless
        // controls, such as a row of pictures, count whether or not the mod has a name for them.
        int index = 0;
        int count = 0;
        std::wstring heading;
        UObject* panel = nullptr;
        UObject* grid = nullptr;
        const bool wordless = !HasVisibleText(control);
        std::vector<std::wstring> counted;
        for (auto* sibling : Siblings(control, panel, grid))
        {
            if (!obj::IsWidgetShown(sibling)) continue;
            if (IsInteractable(sibling) || sibling == control)
            {
                if (sibling != control && !wordless && Label(sibling).empty()) continue;
                ++count;
                if (log::IsVerbose()) counted.push_back(obj::ClassName(sibling) + L" " + obj::ObjectName(sibling));
                if (sibling == control)
                {
                    index = count;
                    d.group = heading;
                }
                continue;
            }
            // A sibling with text that is no control is the heading of what follows.
            const auto texts = OwnTexts(sibling, true, false);
            if (!texts.empty()) heading = str::Join(texts, L", ");
        }
        if (d.group.empty() && wordless && panel) d.group = HeadingAbove(grid ? grid : panel);
        d.index = index;
        d.count = count;
        log::Verbose(L"menus: {} {} is {} of {} [{}]", obj::ClassName(control), obj::ObjectName(control), index, count, str::Join(counted, L", "));

        // The description line: the footer the button brings, else the blurb a settings page
        // shows beside its rows.
        bool hasFooter = false;
        std::wstring footer;
        if (obj::ReadBool(control, L"bHasFooterText", hasFooter) && hasFooter && obj::ReadString(control, L"FooterText", footer))
            d.tip = str::StripMarkup(footer);
        if (d.tip.empty())
        {
            if (UObject* page = obj::NearestAncestorOfClass(control, L"BMUIPage"))
            {
                std::vector<std::wstring> lines;
                for (const wchar_t* name : {L"Text_SettingBlurb", L"Text_DefaultValue"})
                {
                    const auto text = NamedText(page, name);
                    if (!text.empty() && !IsTemplate(text)) lines.push_back(text);
                }
                d.tip = str::JoinSentences(lines);
            }
        }
        return d;
    }

    std::wstring ScreenTitle(UObject* page)
    {
        if (!page) return {};
        return NamedText(page, L"Text_Title");
    }

    std::vector<std::wstring> ScreenBodyTexts(UObject* page, const OpacityOf& opacityOf)
    {
        std::vector<std::wstring> texts;
        if (!page) return texts;
        std::set<std::wstring> seen;
        const std::wstring title = ScreenTitle(page);
        UObject* blurb = BlurbPanel(page);
        obj::WalkWidgetTree(page, 48,
                            [&](UObject* w, int depth)
                            {
                                if (!obj::IsWidgetVisible(w)) return false;
                                if (depth > 0 && IsButton(w)) return false;
                                if (depth > 0 && IsListPanel(w)) return false;
                                if (w == blurb || IsBlurbWidget(w) || IsKeyIcon(w)) return false;
                                if (IsTextWidget(w))
                                {
                                    const double opacity = opacityOf ? opacityOf(w) : obj::WidgetOpacity(w);
                                    if (opacity < kFadedOpacity) return false;
                                    if (obj::ObjectName(w) == L"Text_Title") return false;
                                    const auto text = str::StripMarkup(obj::TextOf(w));
                                    if (!text.empty() && !IsTemplate(text) && text != title && seen.insert(text).second) texts.push_back(text);
                                }
                                return true;
                            });
        return texts;
    }

    std::wstring ScreenBody(UObject* page, const OpacityOf& opacityOf)
    {
        return str::JoinSentences(ScreenBodyTexts(page, opacityOf));
    }

    bool TabOf(UObject* page, Tab& out)
    {
        UObject* group = page ? obj::NearestAncestorOfClass(obj::ParentWidget(page), L"BatesTabGroupPage") : nullptr;
        if (!group) return false;
        UObject* header = nullptr;
        if (!obj::ReadObject(group, L"TabGroupHeader", header) || !header || !obj::IsLive(header)) return false;
        std::vector<UObject*> buttons;
        obj::ReadObjectArray(header, L"SpawnedButtons", buttons);
        UObject* pageClass = page->GetClassPrivate();
        int index = 0;
        int count = 0;
        std::wstring name;
        for (auto* button : buttons)
        {
            if (!button || !obj::IsLive(button) || !obj::IsWidgetShown(button)) continue;
            ++count;
            UObject* cls = nullptr;
            if (obj::ReadObject(button, L"PageSubclass", cls) && cls == pageClass)
            {
                index = count;
                name = str::Join(OwnTexts(button, false, false), L", ");
            }
        }
        if (index == 0) return false;
        out.name = name;
        out.index = index;
        out.count = count;
        return true;
    }

    std::vector<Prompt> Prompts(UObject* page)
    {
        std::vector<Prompt> prompts;
        static UObject* bar = nullptr;
        if (!bar || !obj::IsLive(bar)) bar = obj::FindFirstLive(L"BMUIActionBarWidget");
        UObject* panel = nullptr;
        if (bar && obj::IsWidgetShown(bar) && obj::ReadObject(bar, L"Panel_ActionBar", panel) && panel && obj::IsLive(panel))
        {
            obj::WalkWidgetTree(panel, 12,
                                [&](UObject* w, int)
                                {
                                    if (!obj::IsWidgetVisible(w)) return false;
                                    if (!obj::IsA(w, L"BMUIActionBarButton")) return true;
                                    Prompt prompt;
                                    prompt.label = str::Join(OwnTexts(w, false, false), L", ");
                                    // The key: the action the button's icon widget was set up for.
                                    UObject* icon = nullptr;
                                    if (obj::ReadObject(w, L"InputActionWidget", icon) && icon && obj::IsLive(icon))
                                    {
                                        auto* setup = obj::FindProperty(icon, L"CachedSetupData");
                                        UObject* action = nullptr;
                                        if (setup && obj::ReadObjectAt(obj::ValuePtr(icon, setup), obj::StructMember(setup, L"InputAction"), action) && action)
                                            prompt.key = PromptKeys(icon, obj::ObjectName(action));
                                    }
                                    if (!prompt.label.empty()) prompts.push_back(prompt);
                                    return false;
                                });
        }
        // A page among tabs shows the keys that turn them beside the tab names.
        Tab tab;
        if (page && TabOf(page, tab))
        {
            Prompt turn;
            turn.label = locale::Mod(L"ui.prompt.tabs", input::KeyForAction(L"IA_UI_TabLeft"), input::KeyForAction(L"IA_UI_TabRight"));
            prompts.push_back(turn);
        }
        return prompts;
    }
}
