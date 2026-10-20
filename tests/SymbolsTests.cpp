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
    const std::set<uintptr_t> places = {Rva(L"scePadReadState"), Rva(L"scePadReadState.pointer"), Rva(L"scePadRead"), Rva(L"scePadRead.pointer")};
    CHECK(places.size() == 4);
}

TEST_CASE("the members known by offset are in the table, and nothing else answers")
{
    CHECK(Offset(L"UBMUISubsystem.ForceGamepadVendorIcons") != 0);
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
