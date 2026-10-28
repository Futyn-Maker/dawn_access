// features/Hold: when the game hands the player the character, replayed over the timelines the
// logs gave, above all the moments the game's own states look like the player's hands while no
// player has anything to do.
#include "Support.hpp"
#include "features/Hold.hpp"

#include <string>
#include <vector>

using namespace da;
using hold::Why;

namespace
{
    // The frames of play: the character free in the player's hands unless a segment says
    // otherwise.
    hold::Frame Free()
    {
        hold::Frame f;
        f.character = true;
        f.state = hold::State::Game;
        f.binding = 0;
        f.cameraFree = true;
        f.inputTaken = true;
        return f;
    }

    hold::Frame Covered()
    {
        hold::Frame f = Free();
        f.covered = true;
        return f;
    }

    hold::Frame Bound(long long binding)
    {
        hold::Frame f = Free();
        f.binding = binding;
        f.cameraFree = false;
        f.inputTaken = false;
        return f;
    }

    hold::Frame InputIgnored()
    {
        hold::Frame f = Free();
        f.inputTaken = false;
        return f;
    }

    hold::Frame GameWalks()
    {
        hold::Frame f = Free();
        f.gameWalks = true;
        return f;
    }

    hold::Frame Paused()
    {
        hold::Frame f;
        f.state = hold::State::Paused;
        return f;
    }

    hold::Frame Loading()
    {
        hold::Frame f;
        f.state = hold::State::Other;
        return f;
    }

    struct Segment
    {
        double seconds;
        hold::Frame frame;
    };

    struct Change
    {
        double at;
        bool control;
        Why why;
        bool silent;
    };

    // Plays the segments frame by frame at `fps` and returns every change of the hold, timed from
    // the first segment; `startHeld` hands the character over first.
    std::vector<Change> Play(const std::vector<Segment>& segments, double fps = 30.0, bool startHeld = false)
    {
        hold::Hold h;
        std::vector<Change> changes;
        double now = 0.0;
        const double dt = 1.0 / fps;
        if (startHeld)
        {
            for (int i = 0; i < 30; ++i, now += dt)
                h.Update(now, Free());
            REQUIRE(h.Control());
        }
        const double origin = now;
        for (const auto& segment : segments)
        {
            const double end = now + segment.seconds;
            for (; now < end - 1e-9; now += dt)
            {
                if (h.Update(now, segment.frame)) changes.push_back({now - origin, h.Control(), h.Reason(), !h.Control() && h.TakeSilentEnd()});
            }
        }
        return changes;
    }
}

TEST_CASE("hold: the game's moments of seeming freedom hand nothing over")
{
    for (const double fps : {20.0, 30.0, 60.0})
    {
        CAPTURE(fps);
        SUBCASE("the new story's opening film, 2026-10-28")
        {
            // 19:29:15.658 the game in play, Beth unbound, the camera free, the controller taking
            // movement input, while the butterfly film the level's loading put up still covered
            // the picture (still shown at the watcher's scan of 16.108); 16.417 bound to the opening
            // sequence on the activity camera. The film was missed then, and the hold sounded a
            // handing over at 16.267 and a taking back at 16.417.
            const auto changes = Play({{4.0, Loading()}, {0.55, Covered()}, {0.21, Free()}, {3.0, Bound(1)}}, fps);
            CHECK(changes.empty());
            // The film not counted, as it was not then: the handing over and the taking back heard.
            const auto missed = Play({{4.0, Loading()}, {0.76, Free()}, {3.0, Bound(1)}}, fps);
            REQUIRE(missed.size() == 2);
            CHECK(missed[0].control);
            CHECK_FALSE(missed[1].control);
            CHECK(missed[1].why == Why::Bound);
        }
        SUBCASE("the prologue's start under the loading screen and the fade, 2026-10-24")
        {
            const auto changes = Play({{4.0, Loading()}, {0.1, Free()}, {1.0, Covered()}, {3.0, Bound(1)}}, fps);
            CHECK(changes.empty());
        }
        SUBCASE("a tenth of a second between a level's loading and its first scene")
        {
            // The end of the woods, the end of the first session with the doctor: 37 to 80 ms.
            for (const double blip : {0.037, 0.08, 0.1, 0.45})
            {
                CAPTURE(blip);
                const auto changes = Play({{2.0, Loading()}, {blip, Free()}, {3.0, Bound(1)}}, fps);
                CHECK(changes.empty());
            }
        }
        SUBCASE("a cutscene played around an unbound character: the camera is not one to explore through")
        {
            hold::Frame cutscene = Free();
            cutscene.cameraFree = false;
            const auto changes = Play({{2.0, Loading()}, {10.0, cutscene}}, fps);
            CHECK(changes.empty());
        }
    }
}

TEST_CASE("hold: what the game does with the character in play takes nothing back")
{
    SUBCASE("a stair: binding 3 and movement input ignored for a third of a second")
    {
        hold::Frame stair = InputIgnored();
        stair.binding = 3;
        hold::Frame onStairs = Free();
        onStairs.binding = 3;
        const auto changes = Play({{0.32, stair}, {5.0, onStairs}}, 30.0, true);
        CHECK(changes.empty());
    }
    SUBCASE("an interaction letting go: input ignored for half a second")
    {
        const auto changes = Play({{0.5, InputIgnored()}, {5.0, Free()}}, 30.0, true);
        CHECK(changes.empty());
    }
    SUBCASE("a staircase walking her onto its first step is hers, not the game's walk")
    {
        // 1.4 s at the lodge's porch on 2026-10-28: StairsWalkHer, so gameWalks stays false.
        const auto changes = Play({{1.4, Free()}, {5.0, Free()}}, 30.0, true);
        CHECK(changes.empty());
    }
}

TEST_CASE("hold: what keeps the character from the player takes her back")
{
    SUBCASE("a scripted stop on the woods' path: input ignored two and a half seconds")
    {
        const auto changes = Play({{2.5, InputIgnored()}, {2.0, Free()}}, 30.0, true);
        REQUIRE(changes.size() == 2);
        CHECK_FALSE(changes[0].control);
        CHECK(changes[0].why == Why::InputIgnored);
        CHECK(changes[0].at == doctest::Approx(hold::kSoftTakeBack).epsilon(0.05));
        CHECK(changes[1].control);
        CHECK(changes[1].at == doctest::Approx(2.5 + hold::kHandOverSettle).epsilon(0.05));
    }
    SUBCASE("the game's own walk: the jog to the deer")
    {
        const auto changes = Play({{17.0, GameWalks()}, {2.0, Free()}}, 30.0, true);
        REQUIRE(changes.size() == 2);
        CHECK(changes[0].why == Why::GameWalks);
        CHECK(changes[1].control);
    }
    SUBCASE("a door grabbed: bound to an interaction, at once")
    {
        const auto changes = Play({{1.0, Bound(2)}}, 30.0, true);
        REQUIRE(changes.size() == 1);
        CHECK(changes[0].why == Why::Bound);
        CHECK(changes[0].at < 0.04);
    }
    SUBCASE("a film over the picture, at once")
    {
        const auto changes = Play({{1.0, Covered()}}, 30.0, true);
        REQUIRE(changes.size() == 1);
        CHECK(changes[0].why == Why::Covered);
    }
}

TEST_CASE("hold: a real hand-over, once everything has stood half a second")
{
    const auto changes = Play({{2.0, Loading()}, {2.0, Free()}});
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].control);
    CHECK(changes[0].at - 2.0 == doctest::Approx(hold::kHandOverSettle).epsilon(0.08));
}

TEST_CASE("hold: a pause freezes the hold and plays no tone")
{
    SUBCASE("the pause menu over play: nothing changes")
    {
        const auto changes = Play({{3.0, Paused()}, {2.0, Free()}}, 30.0, true);
        CHECK(changes.empty());
    }
    SUBCASE("the scene left from the pause menu: the hold ends without its tone")
    {
        const auto changes = Play({{3.0, Paused()}, {2.0, Loading()}}, 30.0, true);
        REQUIRE(changes.size() == 1);
        CHECK_FALSE(changes[0].control);
        CHECK(changes[0].silent);
    }
    SUBCASE("a hold ended in play, not out of a pause, is sounded")
    {
        const auto changes = Play({{1.0, Bound(1)}}, 30.0, true);
        REQUIRE(changes.size() == 1);
        CHECK_FALSE(changes[0].silent);
    }
    SUBCASE("time paused does not count toward a hand-over")
    {
        const auto changes = Play({{2.0, Loading()}, {0.3, Free()}, {5.0, Paused()}, {0.3, Free()}, {2.0, Bound(1)}});
        CHECK(changes.empty());
    }
}
