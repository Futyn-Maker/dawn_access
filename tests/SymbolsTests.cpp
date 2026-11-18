// core/Symbols: the tables of places known by offset, one per build of the game, and the build
// each belongs to.
#include "Support.hpp"
#include "core/Symbols.hpp"

#include <iterator>
#include <set>

using namespace da::sym;

namespace
{
    const Table& Reference()
    {
        return Tables().front();
    }

    std::set<std::wstring_view> PlaceNames(const Table& table)
    {
        std::set<std::wstring_view> names;
        for (const Place& place : table.places)
            names.insert(place.name);
        return names;
    }

    std::set<std::wstring_view> MemberNames(const Table& table)
    {
        std::set<std::wstring_view> names;
        for (const Member& member : table.members)
            names.insert(member.name);
        return names;
    }
}

TEST_CASE("every build's table names the reference's places and members, each once, each place at an address of its own")
{
    REQUIRE(!Tables().empty());
    for (const Table& table : Tables())
    {
        CAPTURE(std::wstring(table.version));
        CHECK(PlaceNames(table) == PlaceNames(Reference()));
        CHECK(MemberNames(table) == MemberNames(Reference()));
        CHECK(PlaceNames(table).size() == table.places.size());
        CHECK(MemberNames(table).size() == table.members.size());
        // No two places of a build share an address, and none is missing.
        std::set<uintptr_t> rvas;
        for (const Place& place : table.places)
        {
            CAPTURE(std::wstring(place.name));
            CHECK(place.rva != 0);
            rvas.insert(place.rva);
        }
        CHECK(rvas.size() == table.places.size());
        for (const Member& member : table.members)
        {
            CAPTURE(std::wstring(member.name));
            CHECK(member.offset != 0);
        }
        // An optional's flag lies right after its value: the mod reads both, and reads the hold's
        // path inside its 0x110 bytes by the offsets of Exploration.
        CHECK(Offset(table, L"ABatesPlayerCameraManager.ActiveLayer") + 1 == Offset(table, L"ABatesPlayerCameraManager.ActiveLayer.IsSet"));
        CHECK(Offset(table, L"ABatesInteractionPhoneUnlock.InteractionState") + 4 == Offset(table, L"ABatesInteractionPhoneUnlock.InteractionState.IsSet"));
        CHECK(Offset(table, L"UBatesCharacterMovementComponent.LimitedLocomotionState") + 0x110 ==
              Offset(table, L"UBatesCharacterMovementComponent.LimitedLocomotionState.IsSet"));
    }
}

TEST_CASE("each build's database is a GUID of 32 hex digits with an age, and no two builds share one")
{
    std::set<std::wstring> builds;
    for (const Table& table : Tables())
    {
        CHECK(table.guid.size() == 32);
        for (const wchar_t c : table.guid)
            CHECK(((c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'F')));
        CHECK(table.age > 0);
        CHECK_FALSE(table.version.empty());
        builds.insert(std::wstring(table.guid) + L"/" + std::to_wstring(table.age));
    }
    CHECK(builds.size() == Tables().size());
}

TEST_CASE("a build is known by its database's GUID and age, spelled as the executable spells them")
{
    const Table* steam = TableFor(L"303AD133527FF19AE0F8417A59CFEE79", 2);
    REQUIRE(steam != nullptr);
    CHECK(steam == &Reference());
    CHECK(TableFor(L"303AD133527FF19AE0F8417A59CFEE79", 3) == nullptr);
    CHECK(TableFor(L"303ad133527ff19ae0f8417a59cfee79", 2) == nullptr);
    CHECK(TableFor(L"00000000000000000000000000000000", 2) == nullptr);
    CHECK(TableFor(L"", 0) == nullptr);
}

TEST_CASE("the reference's places: the pad library's stubs and their pointers, the shader count, and nothing else answers")
{
    const Table& steam = Reference();
    CHECK(Rva(steam, L"scePadReadState") != 0);
    CHECK(Rva(steam, L"scePadReadState.pointer") != 0);
    CHECK(Rva(steam, L"scePadRead") != 0);
    CHECK(Rva(steam, L"scePadRead.pointer") != 0);
    CHECK(Rva(steam, L"scePadOpen") == 0);
    CHECK(Rva(steam, L"") == 0);
    CHECK(Rva(steam, L"FShaderPipelineCache::NumPrecompilesRemaining") == 0x224f130);
}

TEST_CASE("the reference's members: the layouts of its database")
{
    const Table& table = Reference();
    CHECK(Offset(table, L"UBMUISubsystem.ForceGamepadVendorIcons") != 0);
    CHECK(Offset(table, L"UBMTTSQueueItem.TTSLine") != 0);
    CHECK(Offset(table, L"UBatesKeyRebindSlotWidget.CurrentKey") != 0);
    CHECK(Offset(table, L"UBMTTSQueueItem.Priority") < Offset(table, L"UBMTTSQueueItem.PriorityTag"));
    CHECK(Offset(table, L"UBMTTSQueueItem.PriorityTag") < Offset(table, L"UBMTTSQueueItem.TTSLine"));
    CHECK(Offset(table, L"UBatesBindableComponent.BindingState") == 0x160);
    CHECK(Offset(table, L"ABatesUseLocation.bIsLinkedActivityEnabled") < Offset(table, L"ABatesUseLocation.bIsLinkedActivitySleeping"));
    CHECK(Offset(table, L"ABatesUseLocation.bIsLinkedActivitySleeping") < Offset(table, L"ABatesUseLocation.bIsUseLocationEnabled"));
    CHECK(Offset(table, L"ABatesUseLocation.bIsUseLocationEnabled") < Offset(table, L"ABatesUseLocation.bInteractionInProgress"));
    CHECK(Offset(table, L"UBatesNavigationCameraControllerComponent.CameraLayer") != 0);
    CHECK(Offset(table, L"ABatesPlayerCameraManager.ActiveLayer") + 1 == Offset(table, L"ABatesPlayerCameraManager.ActiveLayer.IsSet"));
    CHECK(Offset(table, L"UBatesCharacterTriggerAction.TimesTriggered") == 0x58);
    CHECK(Offset(table, L"UBatesCharacterMovementComponent.LimitedLocomotionState") + 0x110 ==
          Offset(table, L"UBatesCharacterMovementComponent.LimitedLocomotionState.IsSet"));
    CHECK(Offset(table, L"UBatesLevelStreamingManager.StreamingFlags") == 0xd0);
    CHECK(Offset(table, L"UBatesQTEStage.QTEResult") + 1 == Offset(table, L"UBatesQTEStage.QTEState"));
    CHECK(Offset(table, L"UBatesQTEStage.bHasStarted") == 0x189);
    CHECK(Offset(table, L"UBatesShaderCacheSubsystem.CompilationMode") < Offset(table, L"UBatesShaderCacheSubsystem.PrecompilesTotal"));
    CHECK(Offset(table, L"UBatesUIFade.bFadeToBlack") == 0x2d1);
    CHECK(Offset(table, L"UBatesBindableCharacterComponent.bIsBlending") == 0x220);
    CHECK(Offset(table, L"ABatesUseLocation.bInteractionInProgress") + 2 == Offset(table, L"ABatesUseLocation.bOverlappingCharacterCanInteract"));
    CHECK(Offset(table, L"ABatesGlint.bIsSpawned") == 0x47a);
    CHECK(Offset(table, L"UBatesCharacterMovementComponent.AutoLocoState.IsSet") + 0x50 ==
          Offset(table, L"UBatesCharacterMovementComponent.LimitedLocomotionState"));
    CHECK(Offset(table, L"UBatesGlintWidget.bIsShowingPrompt") + 1 == Offset(table, L"UBatesGlintWidget.bIsDestroying"));
    CHECK(Offset(table, L"UBatesGlintWidget.CurrentShowPrompt") == 0x378);
    CHECK(Offset(table, L"ABatesInteractionPhoneUnlock.CumulativeProgress") + 4 == Offset(table, L"ABatesInteractionPhoneUnlock.TargetProgress"));
    CHECK(Offset(table, L"ABatesInteractionPhoneUnlock.InteractionState") + 4 == Offset(table, L"ABatesInteractionPhoneUnlock.InteractionState.IsSet"));
    CHECK(Offset(table, L"ABatesInteractionPhoneUnlock.SwipeDecay") + 4 == Offset(table, L"ABatesInteractionPhoneUnlock.SwipeDecayDelay"));
    CHECK(Offset(table, L"ABatesSpecialLocoActivityActor.BoundCharacterData") == 0x328);
    CHECK(Offset(table, L"FStairActivityCharacterData.bIsInAutoLoco") == 0x10b);
    CHECK(Offset(table, L"UBMUISubsystem.CurrentGamepadVendor") == 0);
    CHECK(Offset(table, L"") == 0);
}

TEST_CASE("the test runner is no build of the game, so no place is handed out")
{
    // The tests run in their own executable, which names another database or none.
    CHECK(Running() == nullptr);
    CHECK_FALSE(BuildKnown());
    CHECK(Rva(L"scePadReadState") == 0);
    CHECK(Offset(L"UBatesBindableComponent.BindingState") == 0);
    CHECK(Address(L"scePadReadState") == nullptr);
    CHECK(Address(L"nothing") == nullptr);
    int object = 0;
    CHECK(MemberOf(&object, L"UBMUISubsystem.ForceGamepadVendorIcons") == nullptr);
}
