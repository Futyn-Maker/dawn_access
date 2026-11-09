#include "watch/Narration.hpp"

#include "core/Fault.hpp"
#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/ParamReader.hpp"
#include "core/Strings.hpp"
#include "core/Symbols.hpp"
#include "features/MenuRules.hpp"
#include "hooks/HookDispatcher.hpp"

#include <Unreal/NameTypes.hpp>

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace da::narration
{
    using RC::Unreal::FFrame;
    using RC::Unreal::FName;
    using RC::Unreal::UObject;

    namespace
    {
        // A gameplay tag is a name; "None" is no tag.
        std::wstring TagName(void* tag)
        {
            if (!tag) return {};
            const auto name = reinterpret_cast<FName*>(tag)->ToString();
            return name == L"None" ? std::wstring() : name;
        }

        // What the game queued for each owner: its last utterance at the button priority, and
        // the name it announced the owner's page with.
        struct Queued
        {
            menu::Utterance label;
            double labelAt = -1.0;
            std::wstring pageName;
        };
        std::mutex g_queuedMutex;
        std::map<UObject*, Queued> g_queued;

        void Remember(UObject* owner, const std::wstring& line, const std::wstring& priority, const std::wstring& format, bool clears)
        {
            // An empty line that clears begins the control's utterance, its name left out
            // (menu::Utterance::Nameless).
            if (!owner || (line.empty() && !clears)) return;
            fault::Guard lock(g_queuedMutex, L"narration");
            if (g_queued.size() > 512) g_queued.clear();
            Queued& queued = g_queued[owner];
            if (priority == L"UI.TTS.Button")
            {
                queued.label.Queued(line, clears, gamethread::FrameCount());
                queued.labelAt = gamethread::NowSeconds();
            }
            else if (priority == L"UI.TTS.Page" && format == L"UI.TTSFormat.Menu")
            {
                queued.pageName = line;
            }
        }

        // The game asked for a line to be narrated, in the words it composed.
        void OnQueueLine(UObject*, FFrame& stack)
        {
            std::wstring line;
            params::String(stack, L"InTTSLine", line);
            UObject* owner = nullptr;
            params::Object(stack, L"InOwner", owner);
            const auto priority = TagName(params::StructPtr(stack, L"InPriorityTag"));
            const auto format = TagName(params::StructPtr(stack, L"InFormatTag"));
            bool clears = false;
            params::Bool(stack, L"bInClearPrio", clears);
            const auto text = str::StripMarkup(line);
            log::Info(L"NARRATION queued \"{}\" by {} priority {} format {}{}", text, obj::ClassName(owner), priority, format, clears ? L", clearing" : L"");
            Remember(owner, text, priority, format, clears);
        }

        // The same from a text block of a widget, with a prefix the game puts before it.
        void OnQueueTextBlock(UObject*, FFrame& stack)
        {
            UObject* block = nullptr;
            params::Object(stack, L"InTextBlock", block);
            UObject* owner = nullptr;
            params::Object(stack, L"InOwner", owner);
            const auto priority = TagName(params::StructPtr(stack, L"InPriorityTag"));
            bool clears = false;
            params::Bool(stack, L"bInClearPrio", clears);
            const auto text = block ? str::StripMarkup(obj::TextOf(block)) : std::wstring();
            log::Info(L"NARRATION queued text \"{}\" of {} by {} priority {} prefix {}{}", text, obj::ObjectName(block), obj::ClassName(owner), priority,
                      TagName(params::StructPtr(stack, L"InPrefix")), clears ? L", clearing" : L"");
            Remember(owner, text, priority, std::wstring(), clears);
        }

        // What the game speaks, from its queue's active item: this catches the lines its own
        // code queues without going through the functions above (the action bar, the tabs).
        UObject* g_subsystem = nullptr;
        UObject* g_lastItem = nullptr;
        std::wstring g_lastLine;

        std::wstring LineOf(UObject* item)
        {
            void* member = sym::MemberOf(item, L"UBMTTSQueueItem.TTSLine");
            if (!member) return {};
            struct RawString
            {
                const wchar_t* data;
                int32_t num;
                int32_t max;
            };
            const auto* text = static_cast<const RawString*>(member);
            if (!text->data || text->num <= 1 || text->num > 4096) return {};
            return std::wstring(text->data, static_cast<size_t>(text->num - 1));
        }

        void Poll(float)
        {
            if (!g_subsystem || !obj::IsLive(g_subsystem)) g_subsystem = obj::FindFirstLive(L"BMTTSSubsystem");
            if (!g_subsystem) return;
            UObject* item = nullptr;
            obj::ReadObject(g_subsystem, L"ActiveTTSItem", item);
            if (!item || !obj::IsLive(item))
            {
                g_lastItem = nullptr;
                return;
            }
            const std::wstring line = LineOf(item);
            if (item == g_lastItem && line == g_lastLine) return;
            g_lastItem = item;
            g_lastLine = line;
            int32_t priority = 0;
            if (void* p = sym::MemberOf(item, L"UBMTTSQueueItem.Priority")) priority = *static_cast<int32_t*>(p);
            log::Info(L"NARRATION speaks \"{}\" priority {} {}", str::StripMarkup(line), priority,
                      TagName(sym::MemberOf(item, L"UBMTTSQueueItem.PriorityTag")));
        }
    }

    std::wstring LabelFor(UObject* owner, double maxAgeSeconds, bool* nameless)
    {
        if (nameless) *nameless = false;
        if (!owner) return {};
        fault::Guard lock(g_queuedMutex, L"narration");
        const auto it = g_queued.find(owner);
        if (it == g_queued.end() || it->second.labelAt < 0.0 || gamethread::NowSeconds() - it->second.labelAt > maxAgeSeconds) return {};
        if (nameless) *nameless = it->second.label.Nameless();
        return it->second.label.Line();
    }

    std::wstring PageNameFor(UObject* page)
    {
        if (!page) return {};
        fault::Guard lock(g_queuedMutex, L"narration");
        const auto it = g_queued.find(page);
        return it == g_queued.end() ? std::wstring() : it->second.pageName;
    }

    void Install()
    {
        hooks::OnNative(L"/Script/BMTTS.BMTTSSubsystem:QueueTTSLine", &OnQueueLine, nullptr);
        hooks::OnNative(L"/Script/BMTTS.BMTTSSubsystem:QueueTTSLineFromTextBlock", &OnQueueTextBlock, nullptr);
        gamethread::AddPoller(L"narration", &Poll);
    }
}
