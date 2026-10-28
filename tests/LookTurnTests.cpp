// features/LookTurn: which turn a thing's press waits for, when it is said, and the walk's turn
// on the spot, played against a model of the game's own locomotion input.
#include "Support.hpp"
#include "features/LookTurn.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <numbers>
#include <optional>

using namespace da;
using look::Lead;

namespace
{
    double Normalize(double degrees)
    {
        degrees = std::fmod(degrees, 360.0);
        if (degrees > 180.0) degrees -= 360.0;
        if (degrees < -180.0) degrees += 360.0;
        return degrees;
    }

    double Degrees(double right, double forward)
    {
        return std::atan2(right, forward) * 180.0 / std::numbers::pi;
    }

    // The game's locomotion as it judges a character's stick, from its own code and data:
    // - IA_Movement's pad mapping has a radial dead zone of 0.2 to 1 (InputModifierDeadZone_13
    //   in MC_Gameplay, defaults); the keys' mappings have none, and two keys make a diagonal
    //   of 1.41;
    // - each frame's input carries the time the stick had been off its middle before the frame,
    //   which then grows by the frame's time while the input's square is at least 1e-4 and falls
    //   to zero on the first frame it is not (UBatesCharacterMovementComponent::TickComponent,
    //   TimeHadMovementInput; GetAndUpdateWorldInput reads it first);
    // - the input buffer keeps the inputs of the last half second (UpdateInputBuffer, +0x15f8
    //   = 0.5), and the largest of them is the one a command takes, newest first, an older one
    //   winning only when strictly larger (ProcessMovementInput);
    // - BP_LocoInputHandler_Into starts a walk once that time is 0.16 s at 0.8 or more, or over
    //   1.5 s at 0.4, or over 0.1 s at 0.4 just after an interaction (the OutOf state,
    //   OutOfInterruptHoldDuration; HasMetInputRequirements);
    // - BP_LocoInputHandler_IdlePivot, on the first frame the stick is back within 0.05 of its
    //   middle after more than none and less than a second of input, turns her on the spot
    //   toward the largest input, or steps her that way when it lies within 23 degrees of where
    //   she faces (HasBeenTriggered, GenerateCommand, IsASingleStep).
    // A walk started takes her out of the standing state, where the pivot is judged, until the
    // stick is let go.
    struct GameLoco
    {
        struct Input
        {
            double time = 0.0;
            double value = 0.0;
            double bearing = 0.0;
        };
        std::deque<Input> buffer;
        double clock = 0.0;
        double timeHad = 0.0;
        bool outOf = false; // just handed back after an interaction
        bool walking = false;
        int walks = 0;
        int pivots = 0;
        int steps = 0;
        double pivotBearing = 0.0;

        // One frame of `dt` with the stick at (right, forward) as the pad or the keys give it,
        // the character facing `facing`.
        void Frame(double dt, double right, double forward, bool deadZone, double facing)
        {
            clock += dt;
            const double size = std::hypot(right, forward);
            const double value = !deadZone ? size : size <= 0.2 ? 0.0 : std::min(1.0, (size - 0.2) / 0.8);
            buffer.push_back({clock, value, Degrees(right, forward)});
            while (!buffer.empty() && buffer.front().time < clock - 0.5)
                buffer.pop_front();
            const double longHold = outOf ? 0.1 : 1.5;
            if (!walking && ((timeHad >= 0.16 && value >= 0.8) || (value >= 0.4 && timeHad > longHold)))
            {
                walking = true;
                ++walks;
            }
            else if (!walking && value < 0.05 && timeHad > 0.0 && timeHad < 1.0)
            {
                const Input* largest = nullptr;
                for (auto it = buffer.rbegin(); it != buffer.rend(); ++it)
                {
                    if (!largest || it->value > largest->value) largest = &*it;
                }
                if (largest && largest->value > 0.0)
                {
                    if (std::fabs(Normalize(largest->bearing - facing)) < 23.0)
                    {
                        ++steps;
                    }
                    else
                    {
                        ++pivots;
                        pivotBearing = largest->bearing;
                    }
                }
            }
            if (value * value >= 1e-4)
            {
                timeHad += dt;
            }
            else
            {
                timeHad = 0.0;
                walking = false;
            }
        }
    };

    // The pad's reading of a push of the walk's: XInput's scale, full being 30000 of 32767.
    constexpr double kPad = 30000.0 / 32767.0;

    // The stick or the keys a push of the walk's gives the game (input::HoldWalkKeys: on keys, a
    // key goes down for each way the push leans 0.38 or more toward).
    std::pair<double, double> Given(const look::Push& push, bool keys)
    {
        if (!keys) return {push.right * kPad, push.forward * kPad};
        const double right = (push.right > 0.38 ? 1.0 : 0.0) - (push.right < -0.38 ? 1.0 : 0.0);
        const double forward = (push.forward > 0.38 ? 1.0 : 0.0) - (push.forward < -0.38 ? 1.0 : 0.0);
        return {right, forward};
    }

    // The walk's last stretch, played frame by frame against GameLoco: the mod's poll comes
    // first in a frame, lets a flick's push go when it was given on an earlier frame
    // (Exploration's PollImpl), then flicks while she does not face the glint within the 110
    // degrees the glint asks (GetCharacterLookIntensity); the game reads the push `delay` frames
    // later. Before the stretch she may have walked in, the walk pushing toward `walkHeading`.
    struct Stretch
    {
        double fps = 30.0;
        double jitter = 0.0;      // each frame is up to this much longer, in turn
        int hitchEvery = 0;       // a frame of 0.2 s every so many
        int delay = 0;            // frames from the mod's push to the game's reading of it
        double target = -148.0;   // where the glint lies from her, in the stick's frame
        double facing = 0.0;      // her yaw in that frame
        double turnRate = 300.0;  // how fast a pivot turns her, degrees a second
        double pivotMost = 180.0; // how far one pivot turns her at most
        bool keys = false;        // the walk holds keys, not the stick
        double walkBefore = 0.0;  // seconds of walking in before the stretch
        double walkHeading = 0.0; // where that walk pushed, in the stick's frame
        GameLoco loco;
        look::Flicker flicker;
        int flicksWhileTurning = 0;
        int walksInStretch = 0;
        int pivotsInStretch = 0;
        int stepsInStretch = 0;

        void Run(double seconds)
        {
            std::deque<std::optional<look::Push>> pipe(static_cast<size_t>(delay), std::nullopt);
            double turnTo = facing;
            double now = 0.0;
            double speed = 0.0;
            bool started = false;
            unsigned long long flickFrame = 0;
            int pivots = 0;
            for (unsigned long long frame = 1; now < walkBefore + seconds; ++frame)
            {
                double dt = 1.0 / fps + jitter * static_cast<double>(frame % 7) / 6.0;
                if (hitchEvery > 0 && frame % static_cast<unsigned long long>(hitchEvery) == 0) dt = 0.2;
                std::optional<look::Push> push;
                if (now < walkBefore)
                {
                    const double radians = walkHeading * std::numbers::pi / 180.0;
                    push = look::Push{std::cos(radians), std::sin(radians)};
                    speed = 105.0;
                }
                else
                {
                    if (!started)
                    {
                        started = true;
                        flicker.Reset(now);
                        walksInStretch = -loco.walks;
                        stepsInStretch = -loco.steps;
                        pivots = loco.pivots;
                    }
                    if (flicker.Pushing() && frame != flickFrame) flicker.Release(now);
                    const bool turning = std::fabs(Normalize(turnTo - facing)) > 0.01;
                    if (std::fabs(Normalize(target - facing)) > 110.0)
                        push = flicker.Update(now, target, facing, speed);
                    else
                        flicker.Hold(now, facing);
                    if (push) flickFrame = frame;
                    if (flicker.Began() && turning) ++flicksWhileTurning;
                    // She slides to a stop over 0.4 s once the walk lets go.
                    speed = std::max(0.0, speed - 105.0 * dt / 0.4);
                }
                pipe.push_back(push);
                const auto seen = pipe.front();
                pipe.pop_front();
                const auto [right, forward] = seen ? Given(*seen, keys) : std::pair<double, double>{0.0, 0.0};
                const int before = loco.pivots;
                loco.Frame(dt, right, forward, !keys, facing);
                if (loco.pivots > before)
                {
                    const double wanted = Normalize(loco.pivotBearing - facing);
                    turnTo = facing + std::clamp(wanted, -pivotMost, pivotMost);
                }
                const double left = Normalize(turnTo - facing);
                const double step = turnRate * dt;
                facing = std::fabs(left) <= step ? turnTo : facing + (left > 0.0 ? step : -step);
                now += dt;
            }
            walksInStretch += loco.walks;
            stepsInStretch += loco.steps;
            pivotsInStretch = loco.pivots - pivots;
        }
    };

    look::Parts AtHand()
    {
        look::Parts p;
        p.measured = true;
        p.atHand = true;
        p.byGlint = true;
        return p;
    }
}

TEST_CASE("look: the turn missing, as the glint judges it")
{
    // The lodge's doors on 2026-10-28, as the log gave them.
    SUBCASE("a fixed shot after the door: on the screen, inside the ellipse, she faces away")
    {
        look::Parts p = AtHand();
        p.onScreen = true;
        p.cameraOk = true;
        CHECK(look::CanBeLed(p));
        CHECK(look::Missing(p) == Lead::Character);
        CHECK(look::Leads(p) == Lead::Character);
    }
    SUBCASE("the exploration camera back: the glint outside the ellipse, she faces away")
    {
        look::Parts p = AtHand();
        p.explorationView = true;
        p.onScreen = true;
        CHECK(look::Missing(p) == Lead::Camera);
        CHECK(look::Leads(p) == Lead::Camera);
    }
    SUBCASE("she faces it, the camera does not")
    {
        look::Parts p = AtHand();
        p.explorationView = true;
        p.characterOk = true;
        CHECK(look::Missing(p) == Lead::Camera);
    }
    SUBCASE("the camera does, she does not")
    {
        look::Parts p = AtHand();
        p.explorationView = true;
        p.onScreen = true;
        p.cameraOk = true;
        CHECK(look::Missing(p) == Lead::Character);
    }
    SUBCASE("a fixed shot that leaves the glint out: no turn of hers brings it in")
    {
        look::Parts p = AtHand();
        CHECK_FALSE(look::CanBeLed(p));
        CHECK(look::Missing(p) == Lead::None);
        CHECK(look::Leads(p) == Lead::None);
    }
    SUBCASE("the press would be taken, or the verdict is a tick behind: nothing is missing")
    {
        look::Parts p = AtHand();
        p.explorationView = true;
        p.verdict = true;
        CHECK(look::Missing(p) == Lead::None);
        p.verdict = false;
        p.cameraOk = true;
        p.characterOk = true;
        CHECK(look::Missing(p) == Lead::None);
    }
    SUBCASE("not at hand, or not measured: nothing")
    {
        look::Parts p = AtHand();
        p.explorationView = true;
        p.atHand = false;
        CHECK(look::Missing(p) == Lead::None);
        p.atHand = true;
        p.measured = false;
        CHECK(look::Missing(p) == Lead::None);
    }
}

TEST_CASE("look: the words follow what is missing")
{
    SUBCASE("the lodge's kitchen door: turn toward it, then the exploration camera's look")
    {
        // 19:35:50.679 the fixed shot asked for her turn; at 54.015 the push gave the exploration
        // camera back, which asked for its own while she still turned; at 54.766 she stood
        // facing it, the camera off. The old code said the first word and never the second.
        look::Words words;
        CHECK(words.Update(0.0, Lead::Character, true) == Lead::Character);
        CHECK(words.Update(0.4, Lead::Character, true) == Lead::None);
        CHECK(words.Update(3.3, Lead::Camera, false) == Lead::None);
        CHECK(words.Update(3.6, Lead::Camera, true) == Lead::None); // not lasted yet
        CHECK(words.Update(4.1, Lead::Camera, true) == Lead::Camera);
        CHECK(words.Update(8.1, Lead::Camera, true) == Lead::None);
    }
    SUBCASE("the first word waits for her to stand, then comes at once")
    {
        look::Words words;
        CHECK(words.Update(0.0, Lead::Camera, false) == Lead::None);
        CHECK(words.Update(0.2, Lead::Camera, true) == Lead::Camera);
    }
    SUBCASE("a turn passing the edge of the look says nothing")
    {
        look::Words words;
        CHECK(words.Update(0.0, Lead::Character, true) == Lead::Character);
        CHECK(words.Update(2.0, Lead::Camera, true) == Lead::None);
        CHECK(words.Update(2.3, Lead::Character, true) == Lead::None);
        CHECK(words.Update(3.0, Lead::Character, true) == Lead::None);
    }
    SUBCASE("the press taken and lost again: a word already said is not said again")
    {
        look::Words words;
        CHECK(words.Update(0.0, Lead::Camera, true) == Lead::Camera);
        CHECK(words.Update(1.0, Lead::None, true) == Lead::None);
        CHECK(words.Update(5.0, Lead::Camera, true) == Lead::None);
    }
    SUBCASE("words come no closer than the gap")
    {
        look::Words words;
        CHECK(words.Update(0.0, Lead::Character, true) == Lead::Character);
        CHECK(words.Update(0.1, Lead::Camera, true) == Lead::None);
        CHECK(words.Update(0.7, Lead::Camera, true) == Lead::None);
        CHECK(words.Update(1.5, Lead::Camera, true) == Lead::Camera);
    }
    SUBCASE("another thing at hand starts afresh")
    {
        look::Words words;
        CHECK(words.Update(0.0, Lead::Camera, true) == Lead::Camera);
        words.Reset();
        CHECK(words.Update(0.1, Lead::Camera, true) == Lead::Camera);
    }
}

TEST_CASE("look: the game's locomotion, as the model of it takes a player's stick")
{
    constexpr double dt = 1.0 / 30.0;
    SUBCASE("the walk's old nudge: a held push of 45% neither turns her nor walks her")
    {
        // The doors of the lodge on 2026-10-28: four walks of 8 s each, her facing 148 degrees
        // off all along.
        GameLoco loco;
        const double radians = -148.0 * std::numbers::pi / 180.0;
        for (int frame = 0; frame < 8 * 30; ++frame)
            loco.Frame(dt, 0.45 * std::sin(radians) * kPad, 0.45 * std::cos(radians) * kPad, true, 0.0);
        CHECK(loco.walks == 0);
        CHECK(loco.pivots == 0);
        CHECK(loco.steps == 0);
    }
    SUBCASE("the stick held toward the sound walks her")
    {
        GameLoco loco;
        for (int frame = 0; frame < 30; ++frame)
            loco.Frame(dt, -kPad, 0.0, true, 0.0);
        for (int frame = 0; frame < 5; ++frame)
            loco.Frame(dt, 0.0, 0.0, true, 0.0);
        CHECK(loco.walks == 1);
        CHECK(loco.pivots == 0);
    }
    SUBCASE("a flick and let go turns her on the spot")
    {
        GameLoco loco;
        for (int frame = 0; frame < 3; ++frame)
            loco.Frame(dt, -kPad, 0.0, true, 0.0);
        loco.Frame(dt, 0.0, 0.0, true, 0.0);
        CHECK(loco.walks == 0);
        CHECK(loco.pivots == 1);
        CHECK(loco.pivotBearing == doctest::Approx(-90.0));
    }
    SUBCASE("a flick toward where she faces steps her that way")
    {
        GameLoco loco;
        loco.Frame(dt, 0.0, kPad, true, 10.0);
        loco.Frame(dt, 0.0, 0.0, true, 10.0);
        CHECK(loco.steps == 1);
        CHECK(loco.pivots == 0);
    }
    SUBCASE("just after an interaction a full flick of a tenth of a second walks her, a light push let go turns her")
    {
        GameLoco full;
        full.outOf = true;
        for (int frame = 0; frame < 5; ++frame)
            full.Frame(dt, -kPad, 0.0, true, 0.0);
        full.Frame(dt, 0.0, 0.0, true, 0.0);
        CHECK(full.walks == 1);
        CHECK(full.pivots == 0);
        GameLoco light;
        light.outOf = true;
        for (int frame = 0; frame < 15; ++frame)
            light.Frame(dt, -0.45, 0.0, true, 0.0);
        light.Frame(dt, 0.0, 0.0, true, 0.0);
        CHECK(light.walks == 0);
        CHECK(light.pivots == 1);
    }
}

TEST_CASE("look: the walk turns her on the spot with a flick, whatever the frame rate")
{
    for (const double fps : {10.0, 15.0, 20.0, 30.0, 60.0, 144.0})
    {
        for (const int delay : {0, 1, 2})
        {
            for (const bool keys : {false, true})
            {
                CAPTURE(fps);
                CAPTURE(delay);
                CAPTURE(keys);
                Stretch s;
                s.fps = fps;
                s.delay = delay;
                s.keys = keys;
                s.Run(6.0);
                CHECK(s.walksInStretch == 0);
                CHECK(s.stepsInStretch == 0);
                CHECK(s.pivotsInStretch == 1);
                CHECK(s.flicker.Count() == 1);
                CHECK(std::fabs(Normalize(s.target - s.facing)) <= 110.0);
            }
        }
    }
}

TEST_CASE("look: just after an interaction, on uneven frames and hitches, a flick never walks her")
{
    // The OutOf state starts a walk on 0.1 s of a push at 0.4: a flick is one frame, which
    // carries no time of its own, so no hitch of the frame before makes it one.
    for (const int delay : {0, 1, 2})
    {
        for (const bool keys : {false, true})
        {
            CAPTURE(delay);
            CAPTURE(keys);
            Stretch s;
            s.loco.outOf = true;
            s.fps = 30.0;
            s.jitter = 0.04;
            s.hitchEvery = 11;
            s.delay = delay;
            s.keys = keys;
            s.Run(8.0);
            CHECK(s.walksInStretch == 0);
            CHECK(s.stepsInStretch == 0);
            CHECK(s.pivotsInStretch >= 1);
            CHECK(std::fabs(Normalize(s.target - s.facing)) <= 110.0);
        }
    }
}

TEST_CASE("look: the first flick after walking in turns her toward the glint, not where she walked")
{
    // She walked in toward 45 degrees (a diagonal: two keys, 1.41 against a flick's one key) and
    // faces that way, the glint 148 degrees off. The game's turn takes the largest push of the
    // last half second, so a flick given while the walk's push is still in it is a single step
    // the way she walked (the review of 2026-10-28).
    for (const int delay : {0, 1})
    {
        for (const bool keys : {false, true})
        {
            CAPTURE(delay);
            CAPTURE(keys);
            Stretch s;
            s.delay = delay;
            s.keys = keys;
            s.walkBefore = 2.0;
            s.walkHeading = 45.0;
            s.facing = 45.0;
            s.target = 45.0 - 148.0;
            s.Run(6.0);
            CHECK(s.walksInStretch == 0);
            CHECK(s.stepsInStretch == 0);
            CHECK(s.pivotsInStretch == 1);
            CHECK(std::fabs(Normalize(s.loco.pivotBearing - s.target)) <= 45.0);
            CHECK(std::fabs(Normalize(s.target - s.facing)) <= 110.0);
        }
    }
}

TEST_CASE("look: a turn short of the look is flicked again only once it is over")
{
    // A pivot that turns her at most 30 degrees: she needs a second after the first, begun only
    // when the first has stopped turning her.
    for (const double fps : {20.0, 30.0, 60.0})
    {
        CAPTURE(fps);
        Stretch s;
        s.fps = fps;
        s.pivotMost = 30.0;
        s.turnRate = 90.0;
        s.Run(10.0);
        CHECK(s.walksInStretch == 0);
        CHECK(s.pivotsInStretch == 2);
        CHECK(s.flicker.Count() == 2);
        CHECK(s.flicksWhileTurning == 0);
        CHECK(std::fabs(Normalize(s.target - s.facing)) <= 110.0);
    }
}

TEST_CASE("look: the flick waits for her to stand, one frame long")
{
    constexpr double dt = 1.0 / 30.0;
    look::Flicker flicker;
    flicker.Reset(0.0);
    // She is still sliding to a stop: no flick.
    for (int frame = 0; frame < 30; ++frame)
        CHECK_FALSE(flicker.Update(frame * dt, 90.0, 0.0, 40.0).has_value());
    CHECK(flicker.Count() == 0);
    // Standing, her facing settled: one frame's push, then let go.
    double began = -1.0;
    double released = -1.0;
    double again = -1.0;
    for (int frame = 30; frame < 60; ++frame)
    {
        const double now = frame * dt;
        const auto push = flicker.Update(now, 90.0, 0.0, 0.0);
        if (push && began < 0.0) began = now;
        if (!push && began >= 0.0 && released < 0.0) released = now;
        if (flicker.Began() && released >= 0.0 && again < 0.0) again = now;
    }
    REQUIRE(began >= 0.0);
    CHECK(began == doctest::Approx(1.0));
    CHECK(released - began == doctest::Approx(dt));
    // She did not turn (nothing here turns her): the next flick waits for the push to leave the
    // game's input buffer.
    REQUIRE(again >= 0.0);
    CHECK(again - released >= look::kAfterPush - 1e-9);
    CHECK(flicker.Count() == 2);
}

TEST_CASE("look: a first flick is not waited for past the settling time")
{
    // She never seems to stand (an idle sway, a slope): the flick comes all the same.
    look::Flicker flicker;
    flicker.Reset(0.0);
    double began = -1.0;
    for (int frame = 0; frame < 90 && began < 0.0; ++frame)
    {
        const double now = frame / 30.0;
        if (flicker.Update(now, 90.0, frame * 2.0, 40.0)) began = now;
    }
    REQUIRE(began >= 0.0);
    CHECK(began >= look::kSettleMax - 1e-9);
    CHECK(began < look::kSettleMax + 0.05);
}

TEST_CASE("look: a push is let go at once when nothing is to be turned, or from outside")
{
    constexpr double dt = 1.0 / 30.0;
    look::Flicker flicker;
    flicker.Reset(0.0);
    double now = 0.0;
    for (; now < 1.0; now += dt)
        flicker.Hold(now, 0.0);
    auto push = flicker.Update(now, -90.0, 0.0, 0.0);
    REQUIRE(push.has_value());
    CHECK(push->right == doctest::Approx(-1.0));
    CHECK(std::fabs(push->forward) < 1e-9);
    CHECK(flicker.Pushing());
    flicker.Hold(now + dt, 0.0);
    CHECK_FALSE(flicker.Pushing());
    // Let go: the next frame wanting a turn waits for the push to leave the input buffer.
    CHECK_FALSE(flicker.Update(now + 2.0 * dt, -90.0, 0.0, 0.0).has_value());
    // A frame on which the stretch is not reached lets it go from outside (Exploration's PollImpl).
    now += 2.0;
    push = flicker.Update(now, -90.0, 0.0, 0.0);
    REQUIRE(push.has_value());
    flicker.Release(now + dt);
    CHECK_FALSE(flicker.Pushing());
    CHECK_FALSE(flicker.Update(now + 2.0 * dt, -90.0, 0.0, 0.0).has_value());
}
