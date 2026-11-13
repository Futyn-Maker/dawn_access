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

    hold::Frame Falling()
    {
        hold::Frame f = Free();
        f.falling = true;
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
            // In play already, past the level's first moments.
            for (int i = 0; i < static_cast<int>(fps * (hold::kLevelSettle + 0.5)) && !h.Control(); ++i, now += dt)
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
            // The film not counted, as it was not then: a level's first moments hand nothing over
            // all the same.
            const auto missed = Play({{4.0, Loading()}, {0.76, Free()}, {3.0, Bound(1)}}, fps);
            CHECK(missed.empty());
        }
        SUBCASE("chapter one's start after its film was skipped, 2026-11-01")
        {
            // 17:04:57.161 the game turned to play from loading; Sam unbound from 57.203, the camera
            // free, the controller taking movement input; the bus film, skipped at 56.937, still on
            // the screen's list until 58.808 but never seen to cover the picture; 58.459 bound to the
            // chapter's opening sequence. The hold sounded a handing over at 58.174 (58.371) and a
            // taking back at 58.459.
            const auto changes = Play({{4.0, Loading()}, {0.04, Bound(0)}, {1.26, Free()}, {3.0, Bound(1)}, {2.0, Free()}}, fps);
            REQUIRE(changes.size() == 1);
            // The hand-over comes after the opening scene, settled as usual.
            CHECK(changes[0].control);
            CHECK(changes[0].at - (4.0 + 0.04 + 1.26 + 3.0) == doctest::Approx(hold::kHandOverSettle).epsilon(0.1));
            // With the film seen for part of the gap, the same.
            const auto filmed = Play({{4.0, Loading()}, {0.7, Covered()}, {0.56, Free()}, {3.0, Bound(1)}}, fps);
            CHECK(filmed.empty());
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
    // After the level's first scene let her go.
    const auto changes = Play({{2.0, Loading()}, {0.3, Free()}, {2.0, Bound(1)}, {2.0, Free()}});
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].control);
    CHECK(changes[0].at - 4.3 == doctest::Approx(hold::kHandOverSettle).epsilon(0.08));
}

TEST_CASE("hold: a level whose first moments are the player's hands her over once they have stood a while")
{
    // A level loaded straight into play (a save resumed at a door): no scene binds her first.
    const auto changes = Play({{2.0, Loading()}, {5.0, Free()}});
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].control);
    CHECK(changes[0].at - 2.0 == doctest::Approx(hold::kLevelSettle).epsilon(0.03));
    // A frozen screen (a character's card) is a level's first moments again.
    const auto card = Play({{2.0, Loading()}, {4.0, Free()}, {3.0, Loading()}, {4.0, Free()}});
    REQUIRE(card.size() == 3);
    CHECK_FALSE(card[1].control);
    CHECK(card[2].at - 9.0 == doctest::Approx(hold::kLevelSettle).epsilon(0.03));
    // A pause is not: the hold comes back as it stood.
    const auto paused = Play({{3.0, Paused()}, {2.0, Free()}}, 30.0, true);
    CHECK(paused.empty());
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

TEST_CASE("hold: a character the game lets drop through the air is no one's, 2026-11-01")
{
    SUBCASE("the cable car's end at chapter one")
    {
        // 23:47:30.7 Chris's last line on the ride; 33.5 the ride's sequence let him go on the
        // navigation camera, unbound, the controller taking movement input, with no ground under
        // him (each new reach map a metre lower, a tenth of a second apart); 34.6 the next level's
        // loading. The hold sounded a handing over at 34.03 and a taking back at 34.59.
        const auto changes = Play({{3.0, Loading()}, {10.0, Bound(1)}, {1.08, Falling()}, {2.0, Loading()}});
        CHECK(changes.empty());
    }
    SUBCASE("Matt put in the player's hands at act2_1")
    {
        // 23:58:46.6 the player's character became Matt, unbound by the world's origin and
        // dropping; 48.68 the walk with Emily bound him. The hold sounded at 47.08 and 48.68, and
        // the list's lesson was spoken between.
        const auto changes = Play({{3.0, Loading()}, {5.0, Bound(1)}, {2.1, Falling()}, {3.0, Bound(1)}});
        CHECK(changes.empty());
    }
    SUBCASE("a step off a kerb in the player's hands takes nothing back")
    {
        const auto changes = Play({{0.3, Falling()}, {3.0, Free()}}, 30.0, true);
        CHECK(changes.empty());
    }
    SUBCASE("a fall that lasts takes her back, and she is handed over once she stands")
    {
        const auto changes = Play({{1.5, Falling()}, {1.0, Free()}}, 30.0, true);
        REQUIRE(changes.size() == 2);
        CHECK_FALSE(changes[0].control);
        CHECK(changes[0].why == Why::Falling);
        CHECK(changes[0].at == doctest::Approx(hold::kSoftTakeBack).epsilon(0.05));
        CHECK(changes[1].control);
        CHECK(changes[1].at == doctest::Approx(1.5 + hold::kHandOverSettle).epsilon(0.05));
    }
}

TEST_CASE("hold: the way back from the collectibles is the pause's, 2026-11-01")
{
    // Jessica on act2_1's path with the collectibles opened and closed: at 23:54:53.09 the game ran
    // again with the menu's camera still up (layer 15, the front end), the controller taking
    // movement input, until 53.63; at 23:56:30.06 the same for 0.38 s. The hold was handed over
    // anew at 54.13 and 30.96, a tone each time.
    hold::Frame menuCamera = Free();
    menuCamera.cameraFree = false;
    SUBCASE("the menu's camera turns back to the scene: nothing changes")
    {
        for (const double back : {0.38, 0.54})
        {
            CAPTURE(back);
            const auto changes = Play({{2.0, Free()}, {5.0, Paused()}, {back, menuCamera}, {3.0, Free()}}, 30.0, true);
            CHECK(changes.empty());
        }
    }
    SUBCASE("a camera that does not come back ends the hold without a tone")
    {
        const auto changes = Play({{5.0, Paused()}, {3.0, menuCamera}}, 30.0, true);
        REQUIRE(changes.size() == 1);
        CHECK_FALSE(changes[0].control);
        CHECK(changes[0].silent);
        CHECK(changes[0].why == Why::Camera);
        CHECK(changes[0].at == doctest::Approx(5.0 + hold::kPauseReturn).epsilon(0.02));
    }
    SUBCASE("a scene that binds her as the game runs again ends the hold at once, without a tone")
    {
        const auto changes = Play({{5.0, Paused()}, {2.0, Bound(1)}}, 30.0, true);
        REQUIRE(changes.size() == 1);
        CHECK(changes[0].silent);
        CHECK(changes[0].at == doctest::Approx(5.0).epsilon(0.01));
    }
    SUBCASE("a camera taken away later in play takes her back as ever, with its tone")
    {
        const auto changes = Play({{5.0, Paused()}, {2.0, Free()}, {1.0, menuCamera}}, 30.0, true);
        REQUIRE(changes.size() == 1);
        CHECK_FALSE(changes[0].silent);
        CHECK(changes[0].at == doctest::Approx(7.0).epsilon(0.01));
    }
}

TEST_CASE("hold: a character the scene has just made the player's waits out the scene's own pause, 2026-11-02")
{
    // act2_1 (s39): at 10:26:12.31 the camera turned free and Ashley went unbound, at 12.36
    // Jessica was the player, unbound on the exploration camera, and at 14.38 her scene bound her.
    // The snowball fight's script had made her the player and waited its SceneTransitionFadeWaitTime
    // of 2 s before starting the scene; the hold handed her over at 12.81 and took her back at 14.38.
    hold::Frame waits = Free();
    waits.sceneWaits = true;
    SUBCASE("the scene takes her as its wait ends: nothing changes")
    {
        const auto changes = Play({{3.0, Loading()}, {20.0, Bound(1)}, {2.0, waits}, {0.02, Free()}, {5.0, Bound(1)}});
        CHECK(changes.empty());
    }
    SUBCASE("a scene that leaves her to the player hands her over once its wait is out")
    {
        const auto changes = Play({{3.0, Loading()}, {20.0, Bound(1)}, {2.0, waits}, {3.0, Free()}});
        REQUIRE(changes.size() == 1);
        CHECK(changes[0].control);
        CHECK(changes[0].at == doctest::Approx(23.0 + 2.0 + hold::kHandOverSettle).epsilon(0.01));
    }
}

TEST_CASE("hold: where the game stops her at a thing it offers, the press is hers and the hold stands")
{
    // The cabin of chapter 3 (research/inventory/act2_5.md): the volumes at Jessica's phone and beside
    // the shutters ignore movement as she enters (BatesSetMoveInputTriggerAction) and the story goes on
    // only by the press there. Taken back after a second, the hold would leave the press unread.
    const auto stopped = []
    {
        hold::Frame f = InputIgnored();
        f.offered = true;
        return f;
    };
    const auto changes = Play({{1.0, Free()}, {6.0, stopped()}, {1.0, Bound(2)}}, 30.0, true);
    REQUIRE(changes.size() == 1);
    CHECK_FALSE(changes[0].control);
    CHECK(changes[0].why == Why::Bound);
    CHECK(changes[0].at == doctest::Approx(7.0).epsilon(0.01));
    // With nothing offered the same stop takes her back after a second, as on the woods' path.
    const auto plain = Play({{1.0, Free()}, {6.0, InputIgnored()}}, 30.0, true);
    REQUIRE(plain.size() == 1);
    CHECK(plain[0].why == Why::InputIgnored);
    CHECK(plain[0].at == doctest::Approx(1.0 + hold::kSoftTakeBack).epsilon(0.05));
}
