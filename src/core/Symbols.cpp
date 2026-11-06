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
            // The Lua 5.1 library the game's scripts run on (LuaRuntime), linked into the
            // executable: enough of it to read a field of a global table and leave the stack as
            // it was. Each begins as the library's own does (lua_gettop: top - base, >> 4).
            {L"lua_gettop", 0x393df50},
            {L"lua_settop", 0x393ed40},
            {L"lua_checkstack", 0x393d9d0},
            {L"lua_getfield", 0x393de10},
            {L"lua_type", 0x393efa0},
            {L"lua_tonumber", 0x393eeb0},
            {L"lua_toboolean", 0x393eda0},
            {L"lua_rawgeti", 0x393e8b0},
            {L"lua_tolstring", 0x393ee10},
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
            // The character's binding state, which the game's movement code reads before it
            // takes the player's input and the glints read before they are drawn.
            {L"UBatesBindableComponent.BindingState", 0x160}, // uint8 EBindingState
            // A use location's own flags: the glint is drawn while the first is set, and the
            // game's interact test asks the others as well.
            {L"ABatesUseLocation.bIsLinkedActivityEnabled", 0x500},  // bool
            {L"ABatesUseLocation.bIsLinkedActivitySleeping", 0x501}, // bool
            {L"ABatesUseLocation.bIsUseLocationEnabled", 0x502},     // bool
            {L"ABatesUseLocation.bInteractionInProgress", 0x503},    // bool
            // The camera manager's active layer, an optional byte the game's own exploration
            // and look-at tests read (IsExplorationCameraActiveLayer: layers 1 to 8).
            {L"ABatesPlayerCameraManager.ActiveLayer", 0x2438},       // uint8 EBatesCameraLayer
            {L"ABatesPlayerCameraManager.ActiveLayer.IsSet", 0x2439}, // bool
            // How many times a trigger volume's action has fired, which the game checks
            // against TimesCanBeTriggered before it fires again, and keeps in the save.
            {L"UBatesCharacterTriggerAction.TimesTriggered", 0x58}, // int32
            // The hold of the character to a path (an optional FBatesLimitedLocomotionState,
            // 0x110 bytes: Parameters with the spline actor's soft pointer at +0x0 and
            // AngleLimits at +0x30, the path's Transform at +0x80, the distance along the
            // spline at +0xe0, the dot limits at +0xe8 and +0xf0), which the movement code
            // bends the stick by.
            {L"UBatesCharacterMovementComponent.LimitedLocomotionState", 0x1430},
            {L"UBatesCharacterMovementComponent.LimitedLocomotionState.IsSet", 0x1540}, // bool
            // The game's own walk of the character (an optional FAutoLocoState, 0x100 bytes):
            // while it is set the movement code puts the walk's direction in place of the
            // player's stick (GetAndUpdateWorldInput, 0x45e1590). A trigger's auto-walk sets it
            // for the jog to the deer, a stair for the step onto its start.
            {L"UBatesCharacterMovementComponent.AutoLocoState.IsSet", 0x13e0}, // bool
            // An anim graph's class lists the struct properties of its nodes; the index the game's
            // GetCurrentStateName takes counts them from the last.
            {L"UAnimBlueprintGeneratedClass.AnimNodeProperties", 0x3f8}, // TArray<FStructProperty*>
            // The game's scripts run in a Lua state of their own, kept by the script the updater
            // runs (ULuaScriptUpdater::RunningScript), behind a lock for the threads that load.
            {L"UScript.mScriptData", 0x1b0},        // lua_State*
            {L"UScript.mScriptAccessMutex", 0x1b8}, // FWindowsCriticalSection (a CRITICAL_SECTION)
            // The camera layer the controller's navigation camera stands on.
            {L"UBatesNavigationCameraControllerComponent.CameraLayer", 0xcc}, // uint8 EBatesCameraLayer
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
            // What holds an interaction's input back (a bit each: the initial delay, a telescope's
            // own lock); its look input is taken only while none is set.
            {L"ABatesInteractionBase.InputBlockers", 0xc78}, // uint8
            // An examine stage measures the turn of the item in hand only while it is fully held and
            // no ramp of its own moves it (UBatesInteractionStageExamine::UpdateMovementInput,
            // 0x4659600): the share of the turn, and whether it is past the reveal (both handed to
            // the item's anim instances every frame as InteractionEvaluation and bIsRevealing).
            {L"UBatesInteractionStageBase.bIsGrabbing", 0x1c9},                    // bool
            {L"UBatesInteractionStageBase.bIsFullyHolding", 0x1ce},                // bool
            // The time a stage has run, which a stage on a clock times out by (SecondsUntilTimeout).
            {L"UBatesInteractionStageBase.TimeSpentInStage", 0x1d4},               // float
            {L"UBatesInteractionStageExamine.bIsRampingIK", 0x2a8},                // bool
            {L"UBatesInteractionStageExamine.bIsRevealing", 0x2a9},                // bool
            {L"UBatesInteractionStageExamine.IKRampSpeed", 0x2b0},                 // float
            {L"UBatesInteractionStageExamine.NormalisedExamineEvaluation", 0x2b4}, // float
            // A stage of an activity keeps its own clock (real seconds, still in a pause) and
            // whether it ran out.
            {L"UBatesActivitySequenceStage.SecondsInCurrentStage", 0x90}, // float
            {L"UBatesActivitySequenceStage.bHasTimedOut", 0x94},          // bool
            // A shooting stage runs between StartShootingStage and OnFinished; its aim is kept as
            // degrees off the direction it began in, and the nearest target's distance from the
            // aim in the screen's pixels, which its highlight is judged by.
            {L"UBatesShootingStage.bShootingStageStarted", 0x450},               // bool
            {L"UBatesShootingStage.bAimOnlyStageCompleted", 0x452},              // bool
            {L"UBatesShootingStage.bUseAimAssist", 0x361},                       // bool
            {L"UBatesShootingStage.CachedScreenDistanceToClosestTarget", 0x378}, // float
            {L"UBatesShootingStage.CachedYaw", 0x390},                           // float
            {L"UBatesShootingStage.CachedPitch", 0x394},                         // float
            // The fade the HUD lays over the picture between scenes: whether it goes to black
            // (or holds black) rather than coming back from it.
            {L"UBatesUIFade.bFadeToBlack", 0x2d1}, // bool
            // The game's interact test (ABatesUseLocation::CanCharacterInteract) asks whether
            // the character is still blending out of what it was bound to, and the use
            // location keeps its verdict for the glint's prompt (set by its tick).
            {L"UBatesBindableCharacterComponent.bIsBlending", 0x220},       // bool
            {L"ABatesUseLocation.bOverlappingCharacterCanInteract", 0x505}, // bool
            // A glint is spawned while the player's character stands in its own volume
            // (ABatesGlint::OnCharacterVolumeStateChanged): only then does the game draw it.
            {L"ABatesGlint.bIsSpawned", 0x47a}, // bool
            // A glint's widget shows its prompt while the glint's intensity is full, the same
            // moment the glint authorises the press (UBatesGlintWidget::Update, 0x46bf680), fades
            // it in by CurrentShowPrompt, and is going away while bIsDestroying is set. The
            // widgets are pooled: one serves glint after glint.
            {L"UBatesGlintWidget.bIsShowingPrompt", 0x348},  // bool
            {L"UBatesGlintWidget.bIsDestroying", 0x349},     // bool
            {L"UBatesGlintWidget.CurrentShowPrompt", 0x378}, // float, 0 to 1
            // The phone's unlock swipe (ABatesInteractionPhoneUnlock): its state (an optional
            // enum: 0 out of the pocket, 1 the intro, 2 the swipe, 3 the outro), the progress the
            // swipes have made towards the target, and how fast it slides back once no swipe
            // came for a moment (UpdateSwipeInteraction, 0x4659c20).
            {L"ABatesInteractionPhoneUnlock.SwipeDecay", 0xd88},             // float, progress lost per second
            {L"ABatesInteractionPhoneUnlock.SwipeDecayDelay", 0xd8c},        // float, seconds
            {L"ABatesInteractionPhoneUnlock.CursorDeltaMultiplier", 0xd90},  // float
            {L"ABatesInteractionPhoneUnlock.LeftStickMultiplier", 0xd98},    // float
            {L"ABatesInteractionPhoneUnlock.CumulativeProgress", 0xe00},     // float
            {L"ABatesInteractionPhoneUnlock.TargetProgress", 0xe04},         // float
            {L"ABatesInteractionPhoneUnlock.InteractionState", 0xe0c},       // int32
            {L"ABatesInteractionPhoneUnlock.InteractionState.IsSet", 0xe10}, // bool
            // The stairs (and every special locomotion) keep a record of each character they
            // handle: BoundCharacterData, a map of a weak pointer to the character to the record.
            // A staircase walks the character onto its first step itself
            // (ABatesStairActivityActor::StartAutoLocoToStartPosition, 0x44f1770) and marks her
            // record while it does.
            {L"ABatesSpecialLocoActivityActor.BoundCharacterData", 0x328}, // TMap
            {L"FStairActivityCharacterData.bIsInAutoLoco", 0x10b},         // bool, in the record
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
