// core/Symbols: the table of places known by offset, and the build they belong to.
#include "Support.hpp"
#include "core/Symbols.hpp"

#include <set>

using namespace da::sym;

TEST_CASE("the pad library's stubs and their pointers are in the table, and nothing else answers")
{
    CHECK(Rva(L"scePadReadState") != 0);
    CHECK(Rva(L"scePadReadState.pointer") != 0);
    CHECK(Rva(L"scePadRead") != 0);
    CHECK(Rva(L"scePadRead.pointer") != 0);
    CHECK(Rva(L"scePadOpen") == 0);
    CHECK(Rva(L"") == 0);
    // A stub and the pointer it reads are two places, and the two stubs are two places.
    const std::set<uintptr_t> places = {Rva(L"scePadReadState"), Rva(L"scePadReadState.pointer"), Rva(L"scePadRead"), Rva(L"scePadRead.pointer"),
                                        Rva(L"FShaderPipelineCache::NumPrecompilesRemaining")};
    CHECK(places.size() == 5);
    CHECK(Rva(L"FShaderPipelineCache::NumPrecompilesRemaining") == 0x224f130);
}

TEST_CASE("the members known by offset are in the table, and nothing else answers")
{
    CHECK(Offset(L"UBMUISubsystem.ForceGamepadVendorIcons") != 0);
    CHECK(Offset(L"UBMTTSQueueItem.TTSLine") != 0);
    CHECK(Offset(L"UBatesKeyRebindSlotWidget.CurrentKey") != 0);
    CHECK(Offset(L"UBMTTSQueueItem.Priority") < Offset(L"UBMTTSQueueItem.PriorityTag"));
    CHECK(Offset(L"UBMTTSQueueItem.PriorityTag") < Offset(L"UBMTTSQueueItem.TTSLine"));
    CHECK(Offset(L"UBatesBindableComponent.BindingState") == 0x160);
    CHECK(Offset(L"ABatesUseLocation.bIsLinkedActivityEnabled") < Offset(L"ABatesUseLocation.bIsLinkedActivitySleeping"));
    CHECK(Offset(L"ABatesUseLocation.bIsLinkedActivitySleeping") < Offset(L"ABatesUseLocation.bIsUseLocationEnabled"));
    CHECK(Offset(L"ABatesUseLocation.bIsUseLocationEnabled") < Offset(L"ABatesUseLocation.bInteractionInProgress"));
    CHECK(Offset(L"UBatesNavigationCameraControllerComponent.CameraLayer") != 0);
    CHECK(Offset(L"ABatesPlayerCameraManager.ActiveLayer") + 1 == Offset(L"ABatesPlayerCameraManager.ActiveLayer.IsSet"));
    CHECK(Offset(L"UBatesCharacterTriggerAction.TimesTriggered") == 0x58);
    CHECK(Offset(L"UBatesCharacterMovementComponent.LimitedLocomotionState") + 0x110 ==
          Offset(L"UBatesCharacterMovementComponent.LimitedLocomotionState.IsSet"));
    CHECK(Offset(L"UBatesLevelStreamingManager.StreamingFlags") == 0xd0);
    CHECK(Offset(L"UBatesQTEStage.QTEResult") + 1 == Offset(L"UBatesQTEStage.QTEState"));
    CHECK(Offset(L"UBatesQTEStage.bHasStarted") == 0x189);
    CHECK(Offset(L"UBatesShaderCacheSubsystem.CompilationMode") < Offset(L"UBatesShaderCacheSubsystem.PrecompilesTotal"));
    CHECK(Offset(L"UBatesUIFade.bFadeToBlack") == 0x2d1);
    CHECK(Offset(L"UBatesBindableCharacterComponent.bIsBlending") == 0x220);
    CHECK(Offset(L"ABatesUseLocation.bInteractionInProgress") + 2 == Offset(L"ABatesUseLocation.bOverlappingCharacterCanInteract"));
    CHECK(Offset(L"ABatesGlint.bIsSpawned") == 0x47a);
    CHECK(Offset(L"UBatesCharacterMovementComponent.AutoLocoState.IsSet") + 0x50 == Offset(L"UBatesCharacterMovementComponent.LimitedLocomotionState"));
    CHECK(Offset(L"UBatesGlintWidget.bIsShowingPrompt") + 1 == Offset(L"UBatesGlintWidget.bIsDestroying"));
    CHECK(Offset(L"UBatesGlintWidget.CurrentShowPrompt") == 0x378);
    CHECK(Offset(L"ABatesInteractionPhoneUnlock.CumulativeProgress") + 4 == Offset(L"ABatesInteractionPhoneUnlock.TargetProgress"));
    CHECK(Offset(L"ABatesInteractionPhoneUnlock.InteractionState") + 4 == Offset(L"ABatesInteractionPhoneUnlock.InteractionState.IsSet"));
    CHECK(Offset(L"ABatesInteractionPhoneUnlock.SwipeDecay") + 4 == Offset(L"ABatesInteractionPhoneUnlock.SwipeDecayDelay"));
    CHECK(Offset(L"ABatesSpecialLocoActivityActor.BoundCharacterData") == 0x328);
    CHECK(Offset(L"FStairActivityCharacterData.bIsInAutoLoco") == 0x10b);
    CHECK(Offset(L"UBMUISubsystem.CurrentGamepadVendor") == 0);
    CHECK(Offset(L"") == 0);
}

TEST_CASE("the build canary is a GUID of 32 hex digits with an age")
{
    CHECK(kBuildGuid.size() == 32);
    for (const wchar_t c : kBuildGuid)
        CHECK(((c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'F')));
    CHECK(kBuildAge > 0);
}

TEST_CASE("the test runner is not the game, so no place is handed out")
{
    // The tests run in their own executable, which names another database or none.
    CHECK_FALSE(BuildMatches());
    CHECK(Address(L"scePadReadState") == nullptr);
    CHECK(Address(L"nothing") == nullptr);
    int object = 0;
    CHECK(MemberOf(&object, L"UBMUISubsystem.ForceGamepadVendorIcons") == nullptr);
}
