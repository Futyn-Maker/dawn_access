#pragma once
// Places in the game's executable that no reflection leads to, taken from the program
// database the game ships. The offsets belong to one build, so they are handed out only
// while the running executable names that very database in its debug directory.

#include <cstdint>
#include <string>
#include <string_view>

namespace da::sym
{
    // The build the offsets were taken from: the GUID and age of its program database, as
    // the executable's CodeView record spells them.
    constexpr std::wstring_view kBuildGuid = L"303AD133527FF19AE0F8417A59CFEE79";
    constexpr uint32_t kBuildAge = 2;

    // The offset of a named place in the executable's image, or 0 for a name not in the table.
    uintptr_t Rva(std::wstring_view name);

    // The offset of a named member inside an object of the game's ("Class.Member"), for the
    // members reflection does not list, or 0 for a name not in the table.
    size_t Offset(std::wstring_view name);

    // The GUID and age of the program database the running executable names, in the spelling
    // of kBuildGuid, or an empty GUID when the executable names none.
    struct Build
    {
        std::wstring guid;
        uint32_t age = 0;
    };
    Build RunningBuild();

    // Whether the running executable is the build the table describes. Decided once; the
    // outcome goes to the log either way.
    bool BuildMatches();

    // The address of a named place in the running executable, or null when the build does
    // not match or the name is not in the table.
    void* Address(std::wstring_view name);

    // The address of a named member inside an object, or null when the build does not match
    // or the name is not in the table.
    void* MemberOf(void* object, std::wstring_view name);
}
