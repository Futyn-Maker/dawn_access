#pragma once
// Places in the game's executable that no reflection leads to, and the offsets of members that
// reflection does not list. They belong to one build of the executable each, so the mod keeps a
// table per build it knows, keyed by the program database the build names in its debug directory,
// and hands a table out only while the running executable names that very database.

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace da::sym
{
    // A named place in the executable's image, by its offset from the image's base.
    struct Place
    {
        std::wstring_view name;
        uintptr_t rva;
    };

    // A named member inside an object of the game's ("Class.Member"), by its offset.
    struct Member
    {
        std::wstring_view name;
        size_t offset;
    };

    // A build the mod knows: the GUID and age of its program database, as the executable's
    // CodeView record spells them (the first three fields as little-endian numbers, the last
    // eight bytes in order, upper-case hex, no dashes), the version the executable states, and
    // its places and members. Every table names the same places and members as the first.
    struct Table
    {
        std::wstring_view guid;
        uint32_t age = 0;
        std::wstring_view version;
        std::span<const Place> places;
        std::span<const Member> members;
    };

    // Every build the mod knows. The first is the reference: its places and members were read
    // from its own program database, and the others were found from it.
    std::span<const Table> Tables();

    // The table of a build, or null for a build the mod does not know.
    const Table* TableFor(std::wstring_view guid, uint32_t age);

    // A place's offset in a table's build, or 0 for a name the table lacks.
    uintptr_t Rva(const Table& table, std::wstring_view name);

    // A member's offset in a table's build, or 0 for a name the table lacks.
    size_t Offset(const Table& table, std::wstring_view name);

    // The GUID and age of the program database the running executable names, in the spelling
    // of Table::guid, or an empty GUID when the executable names none.
    struct Build
    {
        std::wstring guid;
        uint32_t age = 0;
    };
    Build RunningBuild();

    // The running executable's table, or null when the mod does not know the build. Decided
    // once; the outcome goes to the log either way.
    const Table* Running();

    // Whether the mod knows the running build.
    bool BuildKnown();

    // A place's offset or a member's offset on the running build, or 0 when the build is
    // unknown or the name is not in its table.
    uintptr_t Rva(std::wstring_view name);
    size_t Offset(std::wstring_view name);

    // The address of a named place in the running executable, or null when the build is
    // unknown or the name is not in its table.
    void* Address(std::wstring_view name);

    // The address of a named member inside an object, or null when the build is unknown or the
    // name is not in its table.
    void* MemberOf(void* object, std::wstring_view name);
}
