// core/Greeting: what the mod says as it starts.
#include "Support.hpp"
#include "core/Greeting.hpp"
#include "locale/Locale.hpp"

using namespace da;
using tests::TempDir;
using tests::U8;

namespace
{
    void LoadEnglish(TempDir& dir)
    {
        dir.Write("lang/en_US.ini", "[strings]\ngreeting=Dawn Access {0} loaded. Screen reader: {1}. Language: {2}.\n"
                                    "greeting.brief=Dawn Access loaded.\n"
                                    "greeting.unknownbuild=This copy of the game is a build Dawn Access does not support.\n");
        REQUIRE(locale::LoadTables((dir.Path() / "lang").wstring(), L"en_US"));
    }

    greeting::Facts Facts(bool detailed, bool buildKnown, bool speakOnLoad = true)
    {
        greeting::Facts facts;
        facts.speakOnLoad = speakOnLoad;
        facts.detailed = detailed;
        facts.version = L"1.0.0";
        facts.reader = L"NVDA";
        facts.language = L"en_US";
        facts.buildKnown = buildKnown;
        return facts;
    }
}

TEST_CASE("the greeting says the mod is there, in detail with its version, reader and language")
{
    TempDir dir;
    LoadEnglish(dir);
    CHECK(U8(greeting::Words(Facts(false, true))) == U8(L"Dawn Access loaded."));
    CHECK(U8(greeting::Words(Facts(true, true))) == U8(L"Dawn Access 1.0.0 loaded. Screen reader: NVDA. Language: en_US."));
}

TEST_CASE("a build of the game the mod has no table for is said at either level, and alone with the greeting off")
{
    // A player's copy of another build heard nothing of exploration and aiming, and no reason why.
    TempDir dir;
    LoadEnglish(dir);
    CHECK(U8(greeting::Words(Facts(false, false))) == U8(L"Dawn Access loaded. This copy of the game is a build Dawn Access does not support."));
    CHECK(U8(greeting::Words(Facts(true, false))) ==
          U8(L"Dawn Access 1.0.0 loaded. Screen reader: NVDA. Language: en_US. This copy of the game is a build Dawn Access does not support."));
    CHECK(U8(greeting::Words(Facts(true, false, false))) == U8(L"This copy of the game is a build Dawn Access does not support."));
    CHECK(greeting::Words(Facts(true, true, false)).empty());
}
