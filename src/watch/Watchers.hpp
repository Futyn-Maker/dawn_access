#pragma once
// Per-frame pollers on the game thread, which tell the features what the game shows without
// each of them walking the objects itself:
//  - the screen watcher: the user widgets drawn at the top of the viewport, as they appear
//    and go;
//  - the page watcher: the page each page stack displays and the pages the game activates;
//  - the text watcher: registered text widgets, as their text changes;
//  - the focus watcher: the control that holds the focus, and the page it belongs to.

#include <Unreal/UObject.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace da::watch
{
    using RC::Unreal::UObject;

    struct HudEvent
    {
        UObject* instance = nullptr; // the widget (valid on appear; may be dead on disappear)
        std::wstring instanceClass;
        bool appeared = false;
    };
    using HudListener = std::function<void(const HudEvent&)>;
    void AddHudListener(HudListener listener);
    // The top-level user widgets on display: (class name, instance).
    std::vector<std::pair<std::wstring, UObject*>> LiveHudInstances();

    using TextListener = std::function<void(UObject* textWidget, const std::wstring& text)>;
    int WatchText(UObject* textWidget, TextListener listener, bool fireInitial = false);
    void UnwatchText(int id);

    using FocusListener = std::function<void(UObject* screen, UObject* focused)>;
    void AddFocusListener(FocusListener listener);
    UObject* CurrentScreen();
    UObject* CurrentFocused();
    // The pages the game's page stacks display right now, most useful when nothing is focused.
    std::vector<UObject*> CurrentScreens();
    // Counts up every time a page stack starts displaying another page, or the same page anew:
    // a screen the game shows again in the same widget is told apart from one left on display.
    unsigned long long DisplayGeneration();

    // Widgets of a native class the game shows, as they come and go: with each scan of the
    // screen (twice a second) the user widgets on show are sorted by the classes asked for;
    // `appeared` false means gone. The name "HudElement" stands for a user widget drawn
    // directly in one of the game's HUD containers or on its own in the viewport.
    using ClassListener = std::function<void(UObject* widget, bool appeared)>;
    void OnWidgetClass(std::wstring nativeClass, ClassListener listener);
    // The shown widgets of a watched class at the last scan.
    std::vector<UObject*> ShownWidgetsOf(std::wstring_view nativeClass);

    void Install();
    void Reset(); // after map load
}
