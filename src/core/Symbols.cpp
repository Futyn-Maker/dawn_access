#include "core/Symbols.hpp"

#include "core/Log.hpp"

#include <windows.h>

#include <cstdio>

namespace da::sym
{
    namespace
    {
        struct Entry
        {
            std::wstring_view name;
            uintptr_t rva;
        };

        // Offsets from Bates-Win64-Shipping.exe (build of the database above), read with the
        // disassembly beside them on 2026-10-20.
        //
        // The game loads Sony's pad library, libScePad.dll, at run time: every call into it
        // goes through a stub of the executable's own, which names the call in a string for
        // the runtime's error path and takes the library's function from a pointer the
        // executable keeps, filled once the library has been loaded. The two reads of the pad
        // are scePadRead, for the reports queued since the last frame, and scePadReadState,
        // for the pad's state when nothing was queued (FSonyControllers::SendControllerEvents,
        // 0x47bbcf0, calls both).
        constexpr Entry kEntries[] = {
            {L"scePadRead", 0x5d05fb0},              // the stub
            {L"scePadRead.pointer", 0x8a33c08},      // the pointer it calls through
            {L"scePadReadState", 0x5d06050},         // the stub
            {L"scePadReadState.pointer", 0x8a33c00}, // the pointer it calls through
            // The engine's count of shader pipelines still to compile (static, no arguments),
            // which the game's shader cache reads every tick; a loading screen waits for it.
            {L"FShaderPipelineCache::NumPrecompilesRemaining", 0x224f130},
        };

        struct Member
        {
            std::wstring_view name;
            size_t offset;
        };

        // Members of the game's objects that reflection does not list, from the database's
        // layouts. The UI subsystem keeps the vendor whose icons the player chose in the
        // controller settings ("Xbox", "PS5", or nothing for the pad connected): its icon
        // picker (UBMUIActionIcons::GetBestIconForKey, 0x4213f50) draws for that vendor
        // when it is set, and for GetCurrentGamepadVendor() otherwise.
        //
        // The narration subsystem's queue items (UBMTTSQueueItem, 0x50 bytes) are plain
        // objects without reflected members: the line, its priority and the priority tag.
        constexpr Member kMembers[] = {
            {L"UBMUISubsystem.ForceGamepadVendorIcons", 0x100}, // FString
            {L"UBMTTSQueueItem.Priority", 0x30},                // int32
            {L"UBMTTSQueueItem.PriorityTag", 0x34},             // FGameplayTag (an FName)
            {L"UBMTTSQueueItem.TTSLine", 0x40},                 // FString
            // A key-rebind slot of the settings keeps the key it draws its cap from.
            {L"UBatesKeyRebindSlotWidget.CurrentKey", 0x330}, // FKey, whose first member is the key's name
            // A bar of the status page animates towards the value the game gave it, out of its maximum.
            {L"UBatesStatusBarWidget.CurrentAnimValue", 0x2cc}, // float
            {L"UBatesStatusBarWidget.TargetAnimValue", 0x2d0},  // float
            {L"UBatesStatusBarWidget.MaxValue", 0x2d4},         // float
            // What a level load waits for before the game state turns to Game and the loading
            // screen goes (UBatesLevelStreamingManager::UpdateStreaming broadcasts "finished"
            // only with these flags clear: 2 a streaming request pending, 4 character variants
            // loading, 8 shader pipelines compiling), and the shader cache's own state (its
            // mode, 0 once its stall check has paused the compile, and the count it began with).
            {L"UBatesLevelStreamingManager.StreamingFlags", 0xd0},  // uint8
            {L"UBatesShaderCacheSubsystem.CompilationMode", 0x60},  // int32
            {L"UBatesShaderCacheSubsystem.PrecompilesTotal", 0x88}, // int32
            // A quick-time event's stage: its verdict (EBatesQTEResult, set by FinishQTE before
            // it tells the widget), its state (0 none, 1 warm-up, 2 running) and whether it has
            // begun; a stage that moves on with no verdict (its end marker reached first) only
            // takes its widget off the screen, and the game counts the event as failed.
            {L"UBatesQTEStage.QTEResult", 0x163},   // uint8
            {L"UBatesQTEStage.QTEState", 0x164},    // uint8
            {L"UBatesQTEStage.bHasStarted", 0x189}, // bool
            // A glint's widget shows its prompt while the glint's intensity is full, the same
            // moment the glint authorises the press (UBatesGlintWidget::Update, 0x46bf680), fades
            // it in by CurrentShowPrompt, and is going away while bIsDestroying is set. The
            // widgets are pooled: one serves glint after glint.
            {L"UBatesGlintWidget.bIsShowingPrompt", 0x348},  // bool
            {L"UBatesGlintWidget.bIsDestroying", 0x349},     // bool
            {L"UBatesGlintWidget.CurrentShowPrompt", 0x378}, // float, 0 to 1
        };

        struct CodeViewRecord
        {
            uint32_t signature; // 'RSDS'
            uint8_t guid[16];
            uint32_t age;
            // the database's path follows
        };
        constexpr uint32_t kRsds = 0x53445352;

        // The GUID as the database spells it: the first three fields as little-endian numbers,
        // the last eight bytes in order, all upper-case hex and no dashes.
        std::wstring SpellGuid(const uint8_t* g)
        {
            wchar_t buffer[40] = {};
            swprintf_s(buffer, L"%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X", g[3], g[2], g[1], g[0], g[5], g[4], g[7], g[6], g[8], g[9],
                       g[10], g[11], g[12], g[13], g[14], g[15]);
            return buffer;
        }
    }

    uintptr_t Rva(std::wstring_view name)
    {
        for (const Entry& entry : kEntries)
        {
            if (entry.name == name) return entry.rva;
        }
        return 0;
    }

    size_t Offset(std::wstring_view name)
    {
        for (const Member& member : kMembers)
        {
            if (member.name == name) return member.offset;
        }
        return 0;
    }

    Build RunningBuild()
    {
        Build build;
        auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
        if (!base) return build;
        auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return build;
        auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return build;
        const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
        if (directory.VirtualAddress == 0 || directory.Size < sizeof(IMAGE_DEBUG_DIRECTORY)) return build;
        auto* entries = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(base + directory.VirtualAddress);
        const size_t count = directory.Size / sizeof(IMAGE_DEBUG_DIRECTORY);
        for (size_t i = 0; i < count; ++i)
        {
            if (entries[i].Type != IMAGE_DEBUG_TYPE_CODEVIEW || entries[i].SizeOfData < sizeof(CodeViewRecord)) continue;
            auto* record = reinterpret_cast<const CodeViewRecord*>(base + entries[i].AddressOfRawData);
            if (record->signature != kRsds) continue;
            build.guid = SpellGuid(record->guid);
            build.age = record->age;
            return build;
        }
        return build;
    }

    bool BuildMatches()
    {
        static int verdict = -1;
        if (verdict >= 0) return verdict == 1;
        const Build running = RunningBuild();
        const bool matches = running.guid == kBuildGuid && running.age == kBuildAge;
        verdict = matches ? 1 : 0;
        if (matches)
            log::Info(L"symbols: the game is the build the mod knows ({} age {})", running.guid, running.age);
        else if (running.guid.empty())
            log::Info(L"symbols: the game names no program database, so the places the mod knows by offset are not used");
        else
            log::Info(L"symbols: the game is build {} age {}, not {} age {}; the places the mod knows by offset are not used", running.guid, running.age,
                      kBuildGuid, kBuildAge);
        return matches;
    }

    void* Address(std::wstring_view name)
    {
        const uintptr_t rva = Rva(name);
        if (rva == 0 || !BuildMatches()) return nullptr;
        auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
        return base ? base + rva : nullptr;
    }

    void* MemberOf(void* object, std::wstring_view name)
    {
        const size_t offset = Offset(name);
        if (!object || offset == 0 || !BuildMatches()) return nullptr;
        return static_cast<uint8_t*>(object) + offset;
    }
}
