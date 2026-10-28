// features/TextRules: which subtitles are new, and which lines of a HUD element are said.
#include "Support.hpp"
#include "features/TextRules.hpp"

#include <string>
#include <vector>

using namespace da;
using Lines = std::vector<std::wstring>;

TEST_CASE("lines: a subtitle is said as it appears, and not again while it stays or blinks for a frame")
{
    lines::Subtitles subtitles;
    CHECK(subtitles.Update({L"Beth: Hannah?"}) == Lines{L"Beth: Hannah?"});
    CHECK(subtitles.Update({L"Beth: Hannah?"}).empty());
    // Gone for a frame, back the next: the two polls before still hold it.
    CHECK(subtitles.Update({}).empty());
    CHECK(subtitles.Update({L"Beth: Hannah?"}).empty());
    // Two new lines at once (a subtitle wrapped, or two speakers): together, once each.
    CHECK(subtitles.Update({L"Sam: Over here!", L"Josh: Wait!", L"Sam: Over here!"}) == Lines{L"Sam: Over here!", L"Josh: Wait!"});
    CHECK(subtitles.Shown().size() == 3);
    subtitles.Reset();
    CHECK(subtitles.Shown().empty());
    CHECK(subtitles.Update({L"Josh: Wait!"}) == Lines{L"Josh: Wait!"});
}

TEST_CASE("lines: a HUD element is said once its lines stand two polls, new lines only")
{
    lines::Element element;
    CHECK(element.Shown({L"Find Hannah"}, 0.0).empty());
    CHECK(element.Shown({L"Find Hannah"}, 0.2).empty());
    CHECK(element.Shown({L"Find Hannah"}, 0.4) == Lines{L"Find Hannah"});
    CHECK(element.Shown({L"Find Hannah"}, 0.6).empty());
    // A title over a sequence of sentences: the title once, each sentence as it comes.
    CHECK(element.Shown({L"Find Hannah", L"She went outside"}, 0.8).empty());
    CHECK(element.Shown({L"Find Hannah", L"She went outside"}, 1.0).empty());
    CHECK(element.Shown({L"Find Hannah", L"She went outside"}, 1.2) == Lines{L"She went outside"});
    CHECK(element.Seen() == Lines{L"Find Hannah", L"She went outside"});
}

TEST_CASE("lines: the camcorder's clock is said at every tick it shows, and a blink is the same line")
{
    // The prologue's prank film, 2026-10-25: the timecode was cut to its first value.
    lines::Element clock;
    double now = 0.0;
    std::vector<Lines> said;
    for (const auto* tick : {L"0:21", L"0:21", L"0:21", L"0:22", L"0:22", L"0:22", L"0:23", L"0:23", L"0:23"})
    {
        const auto fresh = clock.Shown({tick}, now);
        if (!fresh.empty()) said.push_back(fresh);
        now += 0.2;
    }
    CHECK(said == std::vector<Lines>{{L"0:21"}, {L"0:22"}, {L"0:23"}});
    // A line that leaves and is back within two seconds blinked: not said again.
    lines::Element blink;
    blink.Shown({L"Press R1"}, 0.0);
    blink.Shown({L"Press R1"}, 0.2);
    CHECK(blink.Shown({L"Press R1"}, 0.4) == Lines{L"Press R1"});
    blink.Shown({}, 0.6);
    blink.Shown({}, 0.8);
    blink.Shown({}, 1.0);
    blink.Shown({L"Press R1"}, 1.2);
    blink.Shown({L"Press R1"}, 1.4);
    CHECK(blink.Shown({L"Press R1"}, 1.6).empty());
    // Back after longer: new again.
    lines::Element back;
    back.Shown({L"Press R1"}, 0.0);
    back.Shown({L"Press R1"}, 0.2);
    back.Shown({L"Press R1"}, 0.4);
    back.Shown({}, 0.6);
    back.Shown({}, 0.8);
    back.Shown({}, 3.0);
    back.Shown({L"Press R1"}, 3.2);
    back.Shown({L"Press R1"}, 3.4);
    CHECK(back.Shown({L"Press R1"}, 3.6) == Lines{L"Press R1"});
}

TEST_CASE("lines: hidden and shown again, an element is read again")
{
    lines::Element element;
    element.Shown({L"Totem found"}, 0.0);
    element.Shown({L"Totem found"}, 0.2);
    CHECK(element.Shown({L"Totem found"}, 0.4) == Lines{L"Totem found"});
    element.Hidden();
    element.Shown({L"Totem found"}, 0.6);
    element.Shown({L"Totem found"}, 0.8);
    CHECK(element.Shown({L"Totem found"}, 1.0) == Lines{L"Totem found"});
}

TEST_CASE("lines: the same words through two elements at once are said once")
{
    lines::Echo echo;
    CHECK(echo.Says(L"Find Hannah", 0.0));
    CHECK_FALSE(echo.Says(L"Find Hannah", 0.1));
    CHECK(echo.Says(L"Go outside", 0.2));
    CHECK(echo.Says(L"Find Hannah", 0.3));
    CHECK_FALSE(echo.Says(L"Find Hannah", 5.3));
    CHECK(echo.Says(L"Find Hannah", 5.4));
}
