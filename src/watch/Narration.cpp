#include "watch/Narration.hpp"

#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/ParamReader.hpp"
#include "core/Strings.hpp"
#include "core/Symbols.hpp"
#include "hooks/HookDispatcher.hpp"

#include <Unreal/NameTypes.hpp>

#include <cstdint>
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

        // The game asked for a line to be narrated, in the words it composed.
        void OnQueueLine(UObject*, FFrame& stack)
        {
            std::wstring line;
            params::String(stack, L"InTTSLine", line);
            UObject* owner = nullptr;
            params::Object(stack, L"InOwner", owner);
            log::Info(L"NARRATION queued \"{}\" by {} priority {} format {}", str::StripMarkup(line), obj::ClassName(owner),
                      TagName(params::StructPtr(stack, L"InPriorityTag")), TagName(params::StructPtr(stack, L"InFormatTag")));
        }

        // The same from a text block of a widget, with a prefix the game puts before it.
        void OnQueueTextBlock(UObject*, FFrame& stack)
        {
            UObject* block = nullptr;
            params::Object(stack, L"InTextBlock", block);
            UObject* owner = nullptr;
            params::Object(stack, L"InOwner", owner);
            log::Info(L"NARRATION queued text \"{}\" of {} by {} priority {} prefix {}", block ? str::StripMarkup(obj::TextOf(block)) : std::wstring(),
                      obj::ObjectName(block), obj::ClassName(owner), TagName(params::StructPtr(stack, L"InPriorityTag")),
                      TagName(params::StructPtr(stack, L"InPrefix")));
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

    void Install()
    {
        hooks::OnNative(L"/Script/BMTTS.BMTTSSubsystem:QueueTTSLine", &OnQueueLine, nullptr);
        hooks::OnNative(L"/Script/BMTTS.BMTTSSubsystem:QueueTTSLineFromTextBlock", &OnQueueTextBlock, nullptr);
        gamethread::AddPoller(L"narration", &Poll);
    }
}
