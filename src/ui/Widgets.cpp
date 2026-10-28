#include "ui/Widgets.hpp"

#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "core/Symbols.hpp"
#include "input/InputNames.hpp"
#include "locale/GameText.hpp"
#include "locale/Locale.hpp"
#include "watch/Narration.hpp"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <map>
#include <optional>
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
            bool skipPages = false;      // the pages of the menus and everything in them
            std::wstring_view skipNamed; // a text block left out by its name
            bool joinRows = false;       // a row of text blocks alone is one line
        };

        std::wstring StatusBarText(UObject* bar);

        bool HasLetter(const std::wstring& text)
        {
            return std::any_of(text.begin(), text.end(), [](wchar_t c) { return std::iswalpha(c) != 0; });
        }

        // A horizontal box that holds text blocks and nothing else, as one line: the visible
        // pieces in their order, run together where one of the two is no word (a number, a
        // colon), a space between two words. A piece's own fading is left aside (the blinking
        // colon of a clock); the row's is not. Nothing when the box holds anything else.
        std::optional<std::wstring> RowText(UObject* row, const TextWalk& walk)
        {
            const auto children = obj::PanelChildren(row);
            if (children.size() < 2 || !std::all_of(children.begin(), children.end(), [](UObject* c) { return IsTextWidget(c); })) return std::nullopt;
            if (walk.skipFaded && obj::WidgetOpacity(row) < kFadedOpacity) return std::wstring();
            std::wstring line;
            std::wstring last;
            for (UObject* piece : children)
            {
                if (!walk.includeHidden && !obj::IsWidgetVisible(piece)) continue;
                const auto text = str::StripMarkup(obj::TextOf(piece));
                if (text.empty() || IsTemplate(text)) continue;
                if (!line.empty() && HasLetter(last) && HasLetter(text)) line += L' ';
                line += text;
                last = text;
            }
            return line;
        }

        std::vector<std::wstring> Texts(UObject* root, const TextWalk& walk)
        {
            std::vector<std::wstring> texts;
            obj::WalkWidgetTree(root, 48,
                                [&](UObject* w, int depth)
                                {
                                    if (!walk.includeHidden && !obj::IsWidgetVisible(w)) return false;
                                    if (walk.skipPages && (obj::IsA(w, L"BMUIPage") || obj::IsA(w, L"BatesSubtitlesWidget"))) return false;
                                    if (depth > 0 && walk.skipControls && IsButton(w)) return false;
                                    if (IsKeyIcon(w)) return false;
                                    if (walk.joinRows && obj::IsA(w, L"HorizontalBox"))
                                    {
                                        if (const auto row = RowText(w, walk))
                                        {
                                            if (!row->empty()) texts.push_back(*row);
                                            return false;
                                        }
                                    }
                                    if (obj::IsA(w, L"BatesStatusBarWidget"))
                                    {
                                        const auto bar = StatusBarText(w);
                                        if (!bar.empty()) texts.push_back(bar);
                                        return false;
                                    }
                                    if (IsTextWidget(w))
                                    {
                                        if (!walk.skipNamed.empty() && obj::ObjectName(w) == walk.skipNamed) return false;
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

        // The game's own word for a locked item, the one its padlock and its clue rows show.
        std::wstring LockedWord()
        {
            return gametext::ResolveWithFallback(L"SMG013_64519", locale::Mod(L"ui.state.locked"));
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

        // A bar of the status page: its name with the level the game fills it to, as a
        // percentage of the bar, and the game's own mark of a recent rise or fall. The level
        // is the value the game animates the bar towards, read from the bar itself.
        std::wstring StatusBarText(UObject* bar)
        {
            const auto name = NamedText(bar, L"Text_Name");
            if (name.empty()) return {};
            // The value the game animates the bar towards and the bar's maximum are members
            // reflection does not list; the fill the bar draws now is the weight of its left
            // half against its right (the game's own way of drawing the level).
            double level = -1.0;
            float target = 0.0f;
            float max = 0.0f;
            if (auto* t = static_cast<const float*>(sym::MemberOf(bar, L"UBatesStatusBarWidget.TargetAnimValue"))) target = *t;
            if (auto* m = static_cast<const float*>(sym::MemberOf(bar, L"UBatesStatusBarWidget.MaxValue"))) max = *m;
            if (max > 0.0f) level = std::clamp(static_cast<double>(target) / max, 0.0, 1.0);
            double drawn = -1.0;
            UObject* fill = nullptr;
            UObject* slot = nullptr;
            if (obj::ReadObject(bar, L"Fill_L", fill) && fill && obj::IsLive(fill) && obj::ReadObject(fill, L"Slot", slot) && slot && obj::IsLive(slot))
            {
                if (auto* size = obj::FindProperty(slot, L"Size"))
                {
                    double value = 0.0;
                    if (obj::ReadFloatAt(obj::ValuePtr(slot, size), obj::StructMember(size, L"Value"), value)) drawn = std::clamp(value, 0.0, 1.0);
                }
            }
            if (level < 0.0) level = drawn;
            if (level < 0.0) return name;
            const int percent = static_cast<int>(std::lround(level * 100.0));
            std::wstring text = locale::Mod(L"status.bar", name, std::to_wstring(percent));
            int64_t state = 0;
            obj::ReadInt(bar, L"In State", state);
            if (state == 1) text += L", " + locale::Mod(L"status.up");
            if (state == 2) text += L", " + locale::Mod(L"status.down");
            static std::map<obj::ObjectKey, std::wstring> logged;
            auto& last = logged[obj::KeyOf(bar)];
            if (last != text)
            {
                last = text;
                log::Info(L"status: {} target {:.2f} of {:.2f}, drawn {:.2f}, state {}: \"{}\"", obj::ObjectName(bar), target, max, drawn, state, text);
            }
            return text;
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

        // The census of the widgets beside a control: which are controls, which of those carry
        // words of their own, and the headings among the rest. A long settings page has rows
        // thirty layouts deep, and walking every row's words costs more than a frame, so the
        // census of a panel is kept for a second and shared by its rows.
        std::wstring Label(UObject* control);

        struct CensusEntry
        {
            obj::ObjectKey key;
            std::wstring name; // class and instance, for the log
            bool shown = false;
            bool control = false;
            bool labelled = false; // a control with words of its own
            std::wstring heading;  // the words of a sibling that is no control
        };
        struct Census
        {
            double at = -1.0;
            UObject* panel = nullptr;
            UObject* grid = nullptr;
            std::vector<CensusEntry> entries;
        };
        std::map<obj::ObjectKey, Census> g_census;

        const Census& CensusOf(UObject* control)
        {
            UObject* parent = obj::ParentWidget(control);
            const double now = gamethread::NowSeconds();
            Census& census = g_census[obj::KeyOf(parent)];
            if (census.at >= 0.0 && now - census.at < 1.0 && (!parent || obj::IsLive(parent))) return census;
            if (g_census.size() > 64)
            {
                for (auto it = g_census.begin(); it != g_census.end();)
                    it = (now - it->second.at > 5.0 && &it->second != &census) ? g_census.erase(it) : std::next(it);
            }
            UObject* panel = nullptr;
            UObject* grid = nullptr;
            const auto siblings = Siblings(control, panel, grid);
            census.at = now;
            census.panel = panel;
            census.grid = grid;
            census.entries.clear();
            for (auto* sibling : siblings)
            {
                CensusEntry entry;
                entry.key = obj::KeyOf(sibling);
                entry.shown = obj::IsWidgetShown(sibling);
                if (log::IsVerbose()) entry.name = obj::ClassName(sibling) + L" " + obj::ObjectName(sibling);
                if (entry.shown)
                {
                    entry.control = IsInteractable(sibling);
                    if (entry.control)
                    {
                        entry.labelled = !Label(sibling).empty();
                    }
                    else
                    {
                        const auto texts = OwnTexts(sibling, true, false);
                        if (!texts.empty()) entry.heading = str::Join(texts, L", ");
                    }
                }
                census.entries.push_back(std::move(entry));
            }
            return census;
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
            if (obj::IsA(control, L"BMUISettingSlider"))
            {
                // A slider's own texts are its name and its number; the number moves under the
                // player's hand and is its value, never its name (the brightness slider draws
                // nothing but the number).
                const auto named = Texts(control, TextWalk{false, false, false, false, L"Text_SliderValue"});
                return named.empty() ? std::wstring() : named.front();
            }
            const auto texts = OwnTexts(control, false, false);
            if (obj::IsA(control, L"BMUISettingCarousel"))
            {
                // The row's own texts are its name and its value; the name comes first.
                return texts.empty() ? std::wstring() : texts.front();
            }
            if (!texts.empty()) return str::Join(texts, L", ");
            // A carousel item whose content is locked (its data says so) draws a padlock and
            // no name, so it is the locked word and nothing more; unlocked, it draws its name
            // as text, which is read above.
            if (obj::IsA(control, L"BatesCarouselMenuItemWidget"))
            {
                UObject* data = nullptr;
                bool unlocked = true;
                if (obj::ReadObject(control, L"ItemData", data) && data && obj::IsLive(data) && obj::ReadBool(data, L"bIsUnlocked", unlocked) && !unlocked)
                    return LockedWord();
            }
            label = AccessibleText(control);
            if (!label.empty()) return label;
            for (const auto& picture : PictureNames(control))
            {
                label = PictureText(picture);
                if (!label.empty()) return label;
            }
            // What the game's own narration says of the control once it is focused: the
            // game's evaluation of its data, such as the locked word for a butterfly not yet
            // earned, which it draws as a dimmed picture.
            return narration::LabelFor(control, 30.0);
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

        // Whether the game's widget pool has a pooled widget (a glint, an interaction prompt) out
        // in use: one taken back keeps every flag it was drawn with, and, off any parent and out
        // of the viewport, looks like a widget of a screen layer. The pool is the UI subsystem's
        // (UBMUIUserWidgetPool::Get), and taking a widget back removes it from ActiveWidgets
        // (InternalReleaseWidget, 0x421a320). A widget of no pool, or a pool that cannot be
        // read, is taken as in use.
        bool PoolHolds(UObject* widget)
        {
            if (!obj::IsA(widget, L"BMUIPooledUserWidget")) return true;
            static UObject* subsystem = nullptr;
            static obj::ObjectKey key;
            static double soughtAt = -10.0;
            if (!subsystem || !obj::IsSame(key))
            {
                subsystem = nullptr;
                const double now = gamethread::NowSeconds();
                if (now - soughtAt < 2.0) return true;
                soughtAt = now;
                subsystem = obj::FindFirstLive(L"BMUISubsystem");
                key = obj::KeyOf(subsystem);
                if (!subsystem) return true;
            }
            UObject* pool = nullptr;
            std::vector<UObject*> active;
            if (!obj::ReadObject(subsystem, L"UserWidgetPool", pool) || !obj::IsLive(pool) || !obj::ReadObjectArray(pool, L"ActiveWidgets", active))
                return true;
            return std::find(active.begin(), active.end(), widget) != active.end();
        }

        // The one direction a stick's key cap draws, by its picture's name (MI_PS_LeftStick_Up,
        // MI_XB_RightStick_Left, ...): Up, Down, Left or Right; empty for the whole stick
        // (_All), both ways of an axis (_LeftRight, _UpDown), its press or a half turn, and for
        // anything that is no stick.
        std::wstring StickDirectionOfCap(UObject* cap)
        {
            for (const auto& name : PictureNames(cap))
            {
                const auto at = name.find(L"Stick_");
                if (at == std::wstring::npos) continue;
                const auto suffix = name.substr(at + 6);
                if (suffix == L"Up" || suffix == L"Down" || suffix == L"Left" || suffix == L"Right") return suffix;
                return {};
            }
            return {};
        }

        // The keys the icon of a prompt shows: one key, or one for each direction of an axis
        // (left and right, up and down), which the icon draws as that many key caps.
        std::wstring PromptKeys(UObject* icon, const std::wstring& action, UObject* under = nullptr)
        {
            int caps = 0;
            UObject* shown = nullptr;
            for (const wchar_t* name : {L"XPosInternalWidget", L"XNegInternalWidget", L"YPosInternalWidget", L"YNegInternalWidget"})
            {
                UObject* cap = nullptr;
                if (obj::ReadObject(icon, name, cap) && cap && obj::IsLive(cap) && (under ? obj::IsWidgetShownUnder(cap, under) : obj::IsWidgetShown(cap)))
                {
                    ++caps;
                    shown = cap;
                }
            }
            if (caps <= 1)
            {
                // One direction of a stick drawn alone is that direction: "left stick up". The
                // direction is the cap's picture's, not its slot's: the hint to walk draws the
                // whole stick (MI_PS_LeftStick_All) in the slot for up, and says "left stick".
                if (caps == 1 && shown)
                {
                    const auto direction = StickDirectionOfCap(shown);
                    const auto directional = direction.empty() ? std::wstring() : input::DirectionalKeyForAction(action, direction);
                    if (!directional.empty()) return directional;
                }
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

    std::wstring LabelOf(UObject* control)
    {
        return control ? Label(control) : std::wstring();
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
        default:
            // A key-rebind row's value is the key the player has under each of its mappings,
            // which the row draws as key caps.
            if (obj::IsA(control, L"BatesKeyRebindSettingWidget")) return input::KeysOfRebindRow(control);
            return {};
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
        const bool wordless = !HasVisibleText(control);
        const obj::ObjectKey controlKey = obj::KeyOf(control);
        std::vector<std::wstring> counted;
        const Census& census = CensusOf(control);
        for (const CensusEntry& entry : census.entries)
        {
            const bool self = entry.key == controlKey;
            if (!entry.shown && !self) continue;
            if (entry.control || self)
            {
                if (!self && !wordless && !entry.labelled) continue;
                ++count;
                if (log::IsVerbose()) counted.push_back(entry.name);
                if (self)
                {
                    index = count;
                    d.group = heading;
                }
                continue;
            }
            // A sibling with text that is no control is the heading of what follows.
            if (!entry.heading.empty()) heading = entry.heading;
        }
        UObject* panel = census.panel;
        UObject* grid = census.grid;
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
                // The page's description panel names the row it describes; a panel the game
                // has not refreshed yet, as after a return from a sub-page, describes another
                // row and is not this control's.
                const auto described = str::ToLower(str::Trim(NamedText(page, L"Text_SettingName")));
                const bool describesThis = described.empty() || IsTemplate(described) || described == str::ToLower(str::Trim(d.label));
                std::vector<std::wstring> lines;
                for (const wchar_t* name : {L"Text_SettingBlurb", L"Text_DefaultValue"})
                {
                    if (!describesThis) break;
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
                                if (obj::IsA(w, L"BatesStatusBarWidget"))
                                {
                                    const auto bar = StatusBarText(w);
                                    if (!bar.empty() && seen.insert(bar).second) texts.push_back(bar);
                                    return false;
                                }
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
                                        {
                                            prompt.action = obj::ObjectName(action);
                                            prompt.key = PromptKeys(icon, prompt.action);
                                        }
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

    std::vector<std::wstring> VisibleTexts(UObject* root)
    {
        return root ? OwnTexts(root, false, false) : std::vector<std::wstring>{};
    }

    std::vector<std::wstring> DrawnTexts(UObject* root)
    {
        return root ? OwnTexts(root, false, true) : std::vector<std::wstring>{};
    }

    std::vector<std::wstring> DrawnRows(UObject* root)
    {
        if (!root) return {};
        TextWalk walk;
        walk.skipFaded = true;
        walk.joinRows = true;
        return Texts(root, walk);
    }

    std::vector<std::wstring> AuditTexts(UObject* root)
    {
        if (!root) return {};
        TextWalk walk;
        walk.skipFaded = true;
        walk.skipPages = true;
        return Texts(root, walk);
    }

    std::wstring TextNamed(UObject* root, std::wstring_view name)
    {
        return root ? NamedText(root, name) : std::wstring();
    }

    bool InsideMenuPage(UObject* widget)
    {
        return widget && obj::NearestAncestorOfClass(widget, L"BMUIPage") != nullptr;
    }

    bool IsVideoText(UObject* widget)
    {
        return widget && obj::IsA(widget, L"WBP_VideoTextBase_C");
    }

    bool InsideMenuHost(UObject* widget)
    {
        UObject* cur = widget;
        for (int depth = 0; cur && depth < 12; ++depth)
        {
            cur = obj::FindOuterUserWidget(cur);
            if (!cur) return false;
            if (!str::StartsWith(obj::ClassName(cur), L"WBP_HUDContainer") && !obj::IsA(cur, L"BatesHUDContainer")) continue;
            // A container that holds a page background hosts pages; the game's own HUD
            // container holds none.
            UObject* background = nullptr;
            return obj::ReadObject(cur, L"PageBackgroundContainer", background) && background && obj::IsLive(background);
        }
        return false;
    }

    bool PromptOfCap(UObject* cap, Prompt& out, UObject* under)
    {
        out = Prompt{};
        if (!cap || !obj::IsLive(cap) || !obj::IsA(cap, L"BMUIInputActionWidget")) return false;
        if (!(under ? obj::IsWidgetShownUnder(cap, under) : obj::IsWidgetShown(cap))) return false;
        std::wstring label;
        obj::ReadString(cap, L"ContextText", label);
        label = str::Trim(str::StripMarkup(label));
        if (label.empty() || IsTemplate(label)) label = NamedText(cap, L"Text_ContextText");
        if (IsTemplate(label)) label.clear();
        std::wstring actionName;
        if (auto* setup = obj::FindProperty(cap, L"CachedSetupData"))
        {
            UObject* action = nullptr;
            if (obj::ReadObjectAt(obj::ValuePtr(cap, setup), obj::StructMember(setup, L"InputAction"), action) && action) actionName = obj::ObjectName(action);
        }
        out.label = label;
        out.action = actionName;
        if (!actionName.empty())
        {
            out.key = PromptKeys(cap, actionName, under);
        }
        else
        {
            // A cap set up with a key rather than an action prints the key's letter.
            const auto printed = NamedText(cap, L"Text_Key");
            if (!printed.empty()) out.key = input::KeyDisplayName(printed);
        }
        return !out.label.empty() || !out.key.empty();
    }

    std::vector<Prompt> PromptsIn(UObject* root, bool layer)
    {
        std::vector<Prompt> prompts;
        if (!root) return prompts;
        obj::WalkWidgetTree(root, 48,
                            [&](UObject* w, int)
                            {
                                if (!obj::IsWidgetVisible(w)) return false;
                                if (!obj::IsA(w, L"BMUIInputActionWidget")) return true;
                                Prompt prompt;
                                if (PromptOfCap(w, prompt, layer ? root : nullptr)) prompts.push_back(prompt);
                                return false;
                            });
        return prompts;
    }

    bool LayerWidgetDrawn(UObject* root)
    {
        return obj::IsLayerWidget(root) && obj::IsWidgetVisible(root) && obj::WidgetOpacity(root) > 0.02 && PoolHolds(root);
    }

    std::wstring PromptLines(const std::vector<Prompt>& prompts)
    {
        std::vector<std::wstring> lines;
        for (const auto& prompt : prompts)
        {
            if (prompt.label.empty() && prompt.key.empty()) continue;
            if (prompt.label.empty())
                lines.push_back(prompt.key);
            else if (prompt.key.empty())
                lines.push_back(prompt.label);
            else
                lines.push_back(prompt.label + L": " + prompt.key);
        }
        return str::Join(lines, L", ");
    }
}
