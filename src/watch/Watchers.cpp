#include "watch/Watchers.hpp"

#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"

#include <algorithm>
#include <map>
#include <vector>

namespace da::watch
{
    namespace
    {
        // ---- screen watcher ----
        // The user widgets that sit at the top of the drawn hierarchy, with no user widget
        // above them: the containers the game builds its screens in, its overlays, its fades.
        std::vector<HudListener> g_hudListeners;
        std::map<obj::ObjectKey, std::wstring> g_shownTop;

        void Emit(UObject* instance, const std::wstring& cls, bool appeared)
        {
            HudEvent ev;
            ev.instance = instance;
            ev.instanceClass = cls;
            ev.appeared = appeared;
            log::Verbose(L"screen: {} {} {}", appeared ? L"appeared" : L"gone", cls, appeared ? obj::ObjectName(instance) : std::wstring());
            for (auto& listener : g_hudListeners)
            {
                try
                {
                    listener(ev);
                }
                catch (...)
                {
                    log::Error(L"screen listener failed");
                }
            }
        }

        void PollScreensImpl()
        {
            // Every object is scanned for the user widgets on top, which costs a few
            // milliseconds; twice a second is enough for a screen that appears or goes.
            if (gamethread::FrameCount() % 30 != 0) return;
            std::map<obj::ObjectKey, std::wstring> current;
            for (auto* widget : obj::FindAllLive(L"UserWidget"))
            {
                if (obj::FindOuterUserWidget(widget)) continue;
                if (!obj::IsWidgetShown(widget)) continue;
                current.emplace(obj::KeyOf(widget), obj::ClassName(widget));
            }
            for (const auto& [key, cls] : g_shownTop)
            {
                if (!current.contains(key)) Emit(key.object, cls, false);
            }
            for (const auto& [key, cls] : current)
            {
                if (!g_shownTop.contains(key)) Emit(key.object, cls, true);
            }
            g_shownTop = std::move(current);
        }

        // ---- page watcher ----
        // The game builds its menus and its in-game screens as pages in page stacks, and
        // marks the page each stack displays and the pages it has activated; both are
        // followed here, for the log and for the screens the mod reads when nothing is
        // focused.
        std::vector<UObject*> g_stacks;
        std::vector<UObject*> g_pages;
        std::map<obj::ObjectKey, obj::ObjectKey> g_displayed; // stack -> the page it displays
        std::map<obj::ObjectKey, bool> g_activated;           // page -> whether it is activated
        unsigned long long g_lastPageScan = 0;
        unsigned long long g_displayGeneration = 0;

        void PollPagesImpl()
        {
            const auto frame = gamethread::FrameCount();
            if (frame % 10 != 0) return;
            if (frame - g_lastPageScan > 60 || g_stacks.empty())
            {
                g_lastPageScan = frame;
                g_stacks = obj::FindAllLive(L"BMUIPageStack");
                g_pages = obj::FindAllLive(L"BMUIPage");
                for (auto it = g_displayed.begin(); it != g_displayed.end();)
                    it = obj::IsSame(it->first) ? std::next(it) : g_displayed.erase(it);
                for (auto it = g_activated.begin(); it != g_activated.end();)
                    it = obj::IsSame(it->first) ? std::next(it) : g_activated.erase(it);
            }
            for (auto* stack : g_stacks)
            {
                if (!obj::IsLive(stack)) continue;
                UObject* page = nullptr;
                obj::ReadObject(stack, L"DisplayedWidget", page);
                if (page && !obj::IsLive(page)) page = nullptr;
                const obj::ObjectKey key = obj::KeyOf(page);
                auto& known = g_displayed[obj::KeyOf(stack)];
                if (known == key) continue;
                known = key;
                ++g_displayGeneration;
                log::Verbose(L"page: {} {} displays {} {}", obj::ClassName(stack), obj::ObjectName(stack), obj::ClassName(page), obj::ObjectName(page));
            }
            for (auto* page : g_pages)
            {
                if (!obj::IsLive(page)) continue;
                bool activated = false;
                obj::ReadBool(page, L"bIsActivated", activated);
                const obj::ObjectKey key = obj::KeyOf(page);
                const auto known = g_activated.find(key);
                if (known != g_activated.end() && known->second == activated) continue;
                g_activated[key] = activated;
                if (known != g_activated.end() && activated) ++g_displayGeneration;
                if (known != g_activated.end() || activated)
                    log::Verbose(L"page: {} {} {}", activated ? L"activated" : L"deactivated", obj::ClassName(page), obj::ObjectName(page));
            }
        }

        // ---- text watcher ----
        struct TextWatch
        {
            int id = 0;
            UObject* widget = nullptr;
            TextListener listener;
            std::wstring last;
            bool initialized = false;
        };
        std::vector<TextWatch> g_textWatches;
        int g_nextTextId = 1;

        // The text as the tree walk reads it, so that blocks drawing a bound getter are
        // followed as well as plain ones.
        std::wstring WatchedText(UObject* textWidget)
        {
            return str::StripMarkup(str::Join(obj::DescendantTexts(textWidget, 0), L" "));
        }

        void PollText(float)
        {
            if (gamethread::FrameCount() % 2 != 0) return;
            for (auto it = g_textWatches.begin(); it != g_textWatches.end();)
            {
                if (!obj::IsLive(it->widget))
                {
                    it = g_textWatches.erase(it);
                    continue;
                }
                const auto text = WatchedText(it->widget);
                if (!it->initialized || text != it->last)
                {
                    const bool fire = it->initialized || !text.empty();
                    it->initialized = true;
                    it->last = text;
                    if (fire)
                    {
                        try
                        {
                            it->listener(it->widget, text);
                        }
                        catch (...)
                        {
                            log::Error(L"text listener failed");
                        }
                    }
                }
                ++it;
            }
        }

        // ---- focus watcher ----
        // The game keeps its own record of the focus: its page reflection system holds the
        // active navigation tree, whose leaf node names the page in front and the widget that
        // page has the focus on. Both are read from there every other frame.
        std::vector<FocusListener> g_focusListeners;
        UObject* g_screen = nullptr;
        UObject* g_focused = nullptr;
        obj::ObjectKey g_focusedKey; // the identity of g_focused, beyond its address
        UObject* g_reflection = nullptr;

        UObject* Reflection()
        {
            if (g_reflection && obj::IsLive(g_reflection)) return g_reflection;
            g_reflection = obj::FindFirstLive(L"BMUIPageReflectionSystem");
            return g_reflection;
        }

        void PollFocusImpl()
        {
            const auto frame = gamethread::FrameCount();
            if (frame % 2 != 0) return;

            UObject* page = nullptr;
            UObject* focused = nullptr;
            if (UObject* reflection = Reflection())
            {
                UObject* tree = nullptr;
                UObject* leaf = nullptr;
                if (obj::ReadObject(reflection, L"ActiveTree", tree) && tree && obj::IsLive(tree) && obj::ReadObject(tree, L"LeafNode", leaf) && leaf &&
                    obj::IsLive(leaf))
                {
                    obj::ReadObject(leaf, L"PageObject", page);
                    obj::ReadObject(leaf, L"CachedFocusWidget", focused);
                }
            }
            if (page && (!obj::IsLive(page) || !obj::IsWidgetShown(page))) page = nullptr;
            if (focused && (!obj::IsLive(focused) || !obj::IsWidgetShown(focused))) focused = nullptr;

            UObject* screen = page ? page : (focused ? obj::RootScreen(focused) : nullptr);
            const obj::ObjectKey focusedKey = obj::KeyOf(focused);
            if (screen != g_screen || focusedKey != g_focusedKey)
            {
                g_screen = screen;
                g_focused = focused;
                g_focusedKey = focusedKey;
                log::Verbose(L"focus: screen={} {} focused={} {}", obj::ClassName(screen), obj::ObjectName(screen), obj::ClassName(focused),
                             obj::ObjectName(focused));
                for (auto& listener : g_focusListeners)
                {
                    try
                    {
                        listener(screen, focused);
                    }
                    catch (...)
                    {
                        log::Error(L"focus listener failed");
                    }
                }
            }
        }

        // The pollers read live game widgets, which can be torn down between frames.
        // A fault costs one skipped frame instead of the process.
        void PollScreens(float)
        {
            obj::SafeInvokeLogged(L"watchers.PollScreensImpl", [](void*) { PollScreensImpl(); }, nullptr);
        }

        void PollPages(float)
        {
            obj::SafeInvokeLogged(L"watchers.PollPagesImpl", [](void*) { PollPagesImpl(); }, nullptr);
        }

        void PollFocus(float)
        {
            obj::SafeInvokeLogged(L"watchers.PollFocusImpl", [](void*) { PollFocusImpl(); }, nullptr);
        }
    }

    void AddHudListener(HudListener listener)
    {
        g_hudListeners.push_back(std::move(listener));
    }

    std::vector<std::pair<std::wstring, UObject*>> LiveHudInstances()
    {
        std::vector<std::pair<std::wstring, UObject*>> out;
        for (const auto& [key, cls] : g_shownTop)
        {
            if (obj::IsSame(key)) out.emplace_back(cls, key.object);
        }
        return out;
    }

    int WatchText(UObject* textWidget, TextListener listener, bool fireInitial)
    {
        TextWatch w;
        w.id = g_nextTextId++;
        w.widget = textWidget;
        w.listener = std::move(listener);
        if (!fireInitial)
        {
            w.last = WatchedText(textWidget);
            w.initialized = true;
        }
        g_textWatches.push_back(std::move(w));
        return g_textWatches.back().id;
    }

    void UnwatchText(int id)
    {
        for (auto it = g_textWatches.begin(); it != g_textWatches.end(); ++it)
        {
            if (it->id == id)
            {
                g_textWatches.erase(it);
                return;
            }
        }
    }

    void AddFocusListener(FocusListener listener)
    {
        g_focusListeners.push_back(std::move(listener));
    }

    UObject* CurrentScreen()
    {
        return obj::IsLive(g_screen) ? g_screen : nullptr;
    }

    std::vector<UObject*> CurrentScreens()
    {
        std::vector<UObject*> screens;
        for (const auto& [stack, page] : g_displayed)
        {
            if (page.object && obj::IsSame(page) && obj::IsWidgetShown(page.object)) screens.push_back(page.object);
        }
        return screens;
    }

    UObject* CurrentFocused()
    {
        return obj::IsSame(g_focusedKey) ? g_focused : nullptr;
    }

    unsigned long long DisplayGeneration()
    {
        return g_displayGeneration;
    }

    void Install()
    {
        gamethread::AddPoller(L"screens", &PollScreens);
        gamethread::AddPoller(L"pages", &PollPages);
        gamethread::AddPoller(L"text", &PollText);
        gamethread::AddPoller(L"focus", &PollFocus);
        log::Info(L"watchers installed");
    }

    void Reset()
    {
        g_shownTop.clear();
        g_stacks.clear();
        g_pages.clear();
        g_displayed.clear();
        g_activated.clear();
        g_textWatches.clear();
        g_reflection = nullptr;
        g_screen = nullptr;
        g_focused = nullptr;
        g_focusedKey = {};
    }
}
