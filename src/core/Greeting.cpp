#include "core/Greeting.hpp"

#include "locale/Locale.hpp"

#include <vector>

namespace da::greeting
{
    std::wstring Words(const Facts& facts)
    {
        // Exploration, aiming and much else read places of the game that core/Symbols knows only
        // for the builds of its tables, and on any other build they stand down in silence: the
        // player hears why as the mod starts, and is asked for the report that adds the build.
        const std::wstring unknown = facts.buildKnown ? L"" : locale::Mod(L"greeting.unknownbuild");
        if (!facts.speakOnLoad) return unknown;
        std::wstring words =
            facts.detailed ? locale::Mod(L"greeting", std::vector<std::wstring>{facts.version, facts.reader, facts.language}) : locale::Mod(L"greeting.brief");
        if (!unknown.empty()) words += L" " + unknown;
        return words;
    }
}
