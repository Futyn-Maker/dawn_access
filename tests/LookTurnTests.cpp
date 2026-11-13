// features/LookTurn: which turn a thing's press waits for, when it is said, and the walk's turn
// on the spot, played against a model of the game's own locomotion input.
#include "Support.hpp"
#include "features/GridPath.hpp"
#include "features/GroundFile.hpp"
#include "features/LookTurn.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numbers>
#include <optional>
#include <string>

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
        look::Vec here{0.0, 0.0, 0.0}; // where she stands
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
                    if (flicker.Pushing() && frame != flickFrame && !flicker.Holding(now)) flicker.Release(now);
                    const bool turning = std::fabs(Normalize(turnTo - facing)) > 0.01;
                    if (std::fabs(Normalize(target - facing)) > 110.0)
                        push = flicker.Update(now, target, facing, speed, here);
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
                const int stepsBefore = loco.steps;
                loco.Frame(dt, right, forward, !keys, facing);
                // A step carries her about a stride; a walk at a walk's pace.
                const double radiansFacing = facing * std::numbers::pi / 180.0;
                const double carried = (loco.steps > stepsBefore ? 30.0 : 0.0) + (loco.walking ? 105.0 * dt : 0.0);
                here.x += std::cos(radiansFacing) * carried;
                here.y += std::sin(radiansFacing) * carried;
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
    SUBCASE("a framing camera the camera's movement ends: the right stick brings the glint in, the old hotel's chains, 2026-11-08")
    {
        // research/sessions/s58-whole.log, 11:43:17: after the kitchen's door slam the shot (layer 4)
        // left the chains and knives out; the walk stepped her about for 8 s and said it could not get
        // there. Its BP_InitialFramingCamera_Mover ends on the camera's movement.
        look::Parts p = AtHand();
        p.framingEndsOnLook = true;
        p.characterOk = true;
        CHECK(look::CanBeLed(p));
        CHECK(look::Missing(p) == Lead::Camera);
        CHECK(look::Leads(p) == Lead::Camera);
        // On it, with the glint in the picture and she facing away, it is still her turn.
        p.onScreen = true;
        p.cameraOk = true;
        p.characterOk = false;
        CHECK(look::Missing(p) == Lead::Character);
    }
    SUBCASE("the camera looks at it by the game's measure, and the game shows no glint: the cellar's dollhouse, 2026-11-08")
    {
        // research/sessions/s58-whole.log, 11:15:59 to 11:17:28: on the exploration camera, inside the
        // ellipse and the glint off the screen, again and again for 93 s; the walk said nothing more.
        look::Parts p = AtHand();
        p.explorationView = true;
        p.characterOk = true;
        CHECK(look::GlintMissing(p, true, true));
        // Off the ellipse, the camera is merely turned away: the look is led as ever.
        CHECK_FALSE(look::GlintMissing(p, false, true));
        CHECK(look::Missing(p) == Lead::Camera);
        // On the screen, the glint is up.
        p.onScreen = true;
        CHECK_FALSE(look::GlintMissing(p, true, true));
        // A fixed shot or a press the game would take is no missing glint.
        p.onScreen = false;
        p.explorationView = false;
        CHECK_FALSE(look::GlintMissing(p, true, true));
        p.explorationView = true;
        p.verdict = true;
        CHECK_FALSE(look::GlintMissing(p, true, true));
    }
    SUBCASE("the camera looks at it, its widget on the screen, its line of sight blocked: no glint is drawn: the cellar's wine rack, 2026-11-12")
    {
        // research/sessions/s73-play.log, 17:18:35 to 17:22:26: "within the ellipse yes, on the screen yes,
        // the line of sight blocked" for two minutes whichever way the camera turned; the mod said only
        // "look at it".
        look::Parts p = AtHand();
        p.explorationView = true;
        p.characterOk = true;
        p.onScreen = true;
        p.sightBlocked = true;
        CHECK(look::GlintMissing(p, true, true));
        // Turned off the ellipse, the camera's look is what is missing.
        CHECK_FALSE(look::GlintMissing(p, false, true));
        // The line clear and the widget on the screen: the glint is up.
        p.sightBlocked = false;
        CHECK_FALSE(look::GlintMissing(p, true, true));
    }
    SUBCASE("inside the look's ellipse but out of the picture: the camera's look is missing, not the glint: the guidance totem by the cable car, 2026-11-11")
    {
        // research/sessions/s68-play.log, 01:17:25 to 01:17:48: the glint 24 degrees off across and 24
        // down, inside the ellipse, its widget off the screen; "the game shows no glint here" was said at
        // 01:17:32, and the camera turned to 15 degrees off, the glint came on the screen and the press
        // was taken.
        look::Parts p = AtHand();
        p.explorationView = true;
        p.characterOk = true;
        CHECK_FALSE(look::GlintMissing(p, true, false));
        CHECK(look::Missing(p) == Lead::Camera);
        CHECK(look::Leads(p) == Lead::Camera);
        // The billboard in the picture and the widget still off it: the widget is what is missing.
        CHECK(look::GlintMissing(p, true, true));
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
        CHECK_FALSE(flicker.Update(frame * dt, 90.0, 0.0, 40.0, {}).has_value());
    CHECK(flicker.Count() == 0);
    // Standing, her facing settled: one frame's push, then let go.
    double began = -1.0;
    double released = -1.0;
    double again = -1.0;
    for (int frame = 30; frame < 60; ++frame)
    {
        const double now = frame * dt;
        const auto push = flicker.Update(now, 90.0, 0.0, 0.0, {});
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
        if (flicker.Update(now, 90.0, frame * 2.0, 40.0, {})) began = now;
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
    auto push = flicker.Update(now, -90.0, 0.0, 0.0, {});
    REQUIRE(push.has_value());
    CHECK(push->right == doctest::Approx(-1.0));
    CHECK(std::fabs(push->forward) < 1e-9);
    CHECK(flicker.Pushing());
    flicker.Hold(now + dt, 0.0);
    CHECK_FALSE(flicker.Pushing());
    // Let go: the next frame wanting a turn waits for the push to leave the input buffer.
    CHECK_FALSE(flicker.Update(now + 2.0 * dt, -90.0, 0.0, 0.0, {}).has_value());
    // A frame on which the stretch is not reached lets it go from outside (Exploration's PollImpl).
    now += 2.0;
    push = flicker.Update(now, -90.0, 0.0, 0.0, {});
    REQUIRE(push.has_value());
    flicker.Release(now + dt);
    CHECK_FALSE(flicker.Pushing());
    CHECK_FALSE(flicker.Update(now + 2.0 * dt, -90.0, 0.0, 0.0, {}).has_value());
}

TEST_CASE("the walk's last stretch keeps her near her spot after she slides out of a small place")
{
    // The lodge's family portrait, 2026-11-04 00:13:33 to 00:13:47 (research/sessions/s41.log): her
    // spot at (-819, -7595); the place took her 126 cm from it and she slid out 97 cm from it half a
    // second later. The old walk went on at full stick from there, round and round, and gave up.
    const auto from = [](double x, double y) { return std::hypot(x - -819.0, y - -7595.0); };
    // The place offers the thing: kept, begun or not.
    CHECK(look::StretchKeeps(true, false, from(-826, -7469)));
    // Slid out, 97 cm from the spot: still the last stretch's, to be stepped back in.
    CHECK(look::StretchKeeps(false, true, from(-904, -7547)));
    // Two metres off: the walk takes her again.
    CHECK_FALSE(look::StretchKeeps(false, true, from(-939, -7432)));
    // On her way, never in the place: the walk's.
    CHECK_FALSE(look::StretchKeeps(false, false, from(-904, -7547)));
}

TEST_CASE("out of the music box's place the last stretch steps her toward ground of the place, not toward the start arrow past the dresser, 2026-11-05")
{
    // Hannah's music box, 23:11:25 to 23:11:33 (research/sessions/s45-full.log): the walk brought Sam
    // into the place at (5035, -8068), the camera's look alone missing; resting, she slid out to
    // (5051, -8040), and nine flicks toward the box's start arrow (5165, -8016) turned her to it and
    // moved her nowhere: "could not get there", a metre from it. Pushed again at full stick she stood
    // two seconds against the dresser, then a lean took her round its slanted edge and in. The fixture
    // is the ground the session's record reached from where she slid to, within (4900, -8200) to
    // (5300, -7900) (research/dumps/ground-20261105-230156.tsv, grid 1, research/tools/cut_ground.py).
    std::ifstream in(std::filesystem::path(DA_REPO_ROOT) / "tests" / "fixtures" / "ground-musicbox.tsv");
    REQUIRE(in);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ground::Record record;
    REQUIRE(record.Parse(text));
    const ground::Anchor a = record.anchors.back();
    const auto cellOf = [&](double x, double y) { return std::pair{static_cast<int>(std::floor((x - a.x) / a.cell)), static_cast<int>(std::floor((y - a.y) / a.cell))}; };
    const auto [sx, sy] = cellOf(5051, -8040);
    const grid::Cell start{sx, sy, 69};
    grid::Flood flood(start, 500, 100000);
    while (flood.Step(1000, record.Passable()) == grid::Flood::State::Running)
    {
    }
    // The start arrow stands past what the ground carries her to: no cell of its column is reached.
    const auto [ax, ay] = cellOf(5165, -8016);
    for (int level = 66; level <= 72; ++level)
        CHECK_FALSE(flood.Reached(grid::Cell{ax, ay, level}));
    // Every single step from her cell straight toward it (+x, the box's way) is refused: the dresser.
    const ground::Step* toward = record.Find(start, sx + 1, sy);
    CHECK((toward == nullptr || !toward->free));
    // Where the place took her, the end of the way the walk had felt out, is ground she walks to.
    const auto [mx, my] = cellOf(5037, -8050);
    const grid::Cell placed{mx, my, 69};
    CHECK(flood.Reached(placed));
    // So the step heads there: a spot of the place the ground carries her to wins over the arrow,
    // the map's over the way felt out.
    const look::Vec arrow{5165, -8016, 3479};
    const look::Vec mapped{5037, -8050, 3479};
    const look::Vec felt{5035, -8068, 3479};
    const look::Vec chosen = look::StepSpot(mapped, felt, arrow);
    CHECK(chosen.x == mapped.x);
    CHECK(chosen.y == mapped.y);
    CHECK(look::StepSpot(std::nullopt, felt, arrow).y == felt.y);
    CHECK(look::StepSpot(std::nullopt, std::nullopt, arrow).x == arrow.x);
    // From there she is within the stretch's reach of it, out of the place.
    CHECK(look::StretchKeeps(false, true, std::hypot(5051.0 - 5037.0, -8040.0 + 8050.0)));
}

TEST_CASE("a walk begun within a step of the end of its way takes it in single steps, 2026-11-05")
{
    // The lodge's patio door, 23:25:48 (research/sessions/s45-full.log): Sam stood 43 cm from the door,
    // out of its place by a hair, and the way felt out was 11 cm to a cell behind her. The walk pushed
    // full stick toward it and ran her 125 cm past the door before it turned her back.
    CHECK(look::StretchKeeps(false, false, 11.0, 11.0));
    // A way longer than a step is walked as before.
    CHECK_FALSE(look::StretchKeeps(false, false, 11.0, 80.0));
    // A step's way to a spot too far for the stretch (a place round a wall): walked.
    CHECK_FALSE(look::StretchKeeps(false, false, 400.0, 40.0));
    // No way felt out yet: walked.
    CHECK_FALSE(look::StretchKeeps(false, false, 30.0));
}

TEST_CASE("the walk ends where the press is taken once she has come to rest, not at the first still reading, 2026-11-05")
{
    // The lodge's patio door, 23:25:38 (research/sessions/s45-full.log): the press would be taken at
    // 38.011 while she moved; her speed first read still at 38.425 and the walk ended there; her
    // stopping step then took her on, the press was refused at 38.899 (she no longer faced the door)
    // and she was out of its place by 39.338.
    look::Rest rest;
    rest.Reset();
    CHECK_FALSE(rest.Update(38.011, false));
    CHECK_FALSE(rest.Update(38.2, false));
    CHECK_FALSE(rest.Update(38.425, true)); // ended here before
    CHECK_FALSE(rest.Update(38.5, false));  // the stopping step
    CHECK_FALSE(rest.Update(38.7, false));
    CHECK_FALSE(rest.Update(38.85, true));
    // 38.899: the press refused; the rest starts over and the stretch steps her back in.
    rest.Reset();
    // 23:25:58.9, back in, facing the door: she slows and stands.
    CHECK_FALSE(rest.Update(58.9, false));
    CHECK_FALSE(rest.Update(59.2, true));
    CHECK_FALSE(rest.Update(59.6, true));
    CHECK(rest.Update(59.75, true));
    // Moving on and on, she is let go as she is.
    rest.Reset();
    CHECK_FALSE(rest.Update(10.0, false));
    CHECK_FALSE(rest.Update(11.9, false));
    CHECK(rest.Update(12.0, false));
    // A still reading, a stopping step, and still again a little later with the press still taken:
    // the rest counts from the second still reading, not the first.
    rest.Reset();
    CHECK_FALSE(rest.Update(20.0, false));
    CHECK_FALSE(rest.Update(20.1, true));
    CHECK_FALSE(rest.Update(20.3, false));
    CHECK_FALSE(rest.Update(20.65, true));
    CHECK_FALSE(rest.Update(21.1, true));
    CHECK(rest.Update(21.2, true));
}

TEST_CASE("look: flicks that move her nowhere give way to a held push, the library's guidance totem, 2026-11-06")
{
    // research/sessions/s47-full.log, 22:35:31.8 to 22:35:39.8: the last stretch to the guidance totem
    // flicked ten times toward a spot of its place 30 cm off, +5 degrees in the stick's frame, and
    // Ashley neither turned (she faced +3 degrees off the glint at every flick) nor stepped; the walk
    // ended "could not get there". A flick that did nothing counts; after kDeadFlicks of them the push
    // is held kPulse, past the 0.16 s Into needs at full stick, so a walk starts and carries her.
    constexpr double dt = 1.0 / 30.0;
    look::Flicker flicker;
    flicker.Reset(0.0);
    const look::Vec stuck{-1632.0, -7374.0, 3097.0};
    std::vector<double> holds;
    double now = 0.0;
    for (int frame = 0; frame < 300 && holds.size() < 4; ++frame)
    {
        now = frame * dt;
        if (flicker.Pushing() && !flicker.Holding(now))
        {
            flicker.Release(now);
            continue;
        }
        const auto push = flicker.Update(now, 5.0, 91.0, 0.0, stuck);
        if (flicker.Began()) holds.push_back(push->hold);
    }
    REQUIRE(holds.size() == 4);
    CHECK(holds[0] == 0.0);
    CHECK(holds[1] == 0.0);
    CHECK(holds[2] == doctest::Approx(look::kPulse));
    CHECK(holds[3] == doctest::Approx(look::kPulse));
    CHECK(look::kPulse > 0.16);
    // A held push stays on, frame after frame, until its time is up.
    look::Flicker held;
    held.Reset(0.0);
    double t = 0.0;
    int began = 0;
    for (; began < 3 && t < 10.0; t += dt)
    {
        if (held.Pushing() && !held.Holding(t))
        {
            held.Release(t);
            continue;
        }
        held.Update(t, 5.0, 91.0, 0.0, stuck);
        if (held.Began()) ++began;
    }
    REQUIRE(held.Holding(t));
    int framesHeld = 0;
    for (; held.Holding(t); t += dt)
    {
        REQUIRE(held.Update(t, 5.0, 91.0, 0.0, stuck).has_value());
        ++framesHeld;
    }
    CHECK(framesHeld * dt == doctest::Approx(look::kPulse).epsilon(0.2));
    // A flick that stepped her, or turned her, is no dead one: the next is a flick again.
    look::Flicker moving;
    moving.Reset(0.0);
    look::Vec there = stuck;
    std::vector<double> movingHolds;
    for (int frame = 0; frame < 300 && movingHolds.size() < 4; ++frame)
    {
        now = frame * dt;
        if (moving.Pushing())
        {
            moving.Release(now);
            there.x += 20.0; // each flick's step carries her on
            continue;
        }
        const auto push = moving.Update(now, 5.0, 91.0, 0.0, there);
        if (moving.Began()) movingHolds.push_back(push->hold);
    }
    REQUIRE(movingHolds.size() == 4);
    for (const double h : movingHolds)
        CHECK(h == 0.0);
}

TEST_CASE("look: toward a spot, flicks that only turn her give way to a held push: the guidance totem by the cable car, 2026-11-11")
{
    // research/sessions/s68-play.log, 01:16:49 to 01:16:57: out of the totem's place, 66 cm from her
    // spot, six flicks turned Sam by 45 to 90 degrees four times and nowhere twice and never stepped
    // her; each turn counted as something done, the held push never came, and the walk said it could
    // not get there 2 m from the totem. Walked to again, the walk's own push took her in.
    constexpr double dt = 1.0 / 30.0;
    const look::Vec stuck{26149.0, -39630.0, -17041.0};
    // Her facing as each flick found her, the log's (off the glint): the n-th flick left her at the
    // (n+1)-th.
    const double facings[] = {-11.0, -56.0, -101.0, -101.0, -11.0, -11.0, -53.0};
    const auto run = [&](bool step, bool steps)
    {
        look::Flicker flicker;
        flicker.Reset(0.0);
        look::Vec here = stuck;
        std::vector<double> holds;
        double facing = facings[0];
        double turnAt = -1.0;
        size_t flicks = 0;
        for (int frame = 0; frame < 900 && holds.size() < 6; ++frame)
        {
            const double now = frame * dt;
            if (turnAt >= 0.0 && now >= turnAt)
            {
                facing = facings[std::min(flicks, std::size(facings) - 1)];
                if (steps) here.x += 20.0;
                turnAt = -1.0;
            }
            if (flicker.Pushing() && !flicker.Holding(now))
            {
                flicker.Release(now);
                turnAt = now + 0.3; // the turn on the spot plays out after the push
                continue;
            }
            const auto push = flicker.Update(now, 54.0, facing, 0.0, here, step);
            if (flicker.Began())
            {
                holds.push_back(push->hold);
                ++flicks;
            }
        }
        return holds;
    };
    // Toward her spot: two flicks that moved her nowhere, and the third is held.
    const auto toward = run(true, false);
    REQUIRE(toward.size() >= 3);
    CHECK(toward[0] == 0.0);
    CHECK(toward[1] == 0.0);
    CHECK(toward[2] == doctest::Approx(look::kPulse));
    // Turning her to the glint, a turn is what a flick is for: flicks, as before.
    const auto turning = run(false, false);
    REQUIRE(turning.size() >= 6);
    for (const double h : turning)
        CHECK(h == 0.0);
    // A flick that stepped her did something toward the spot: the next is a flick again.
    const auto stepping = run(true, true);
    REQUIRE(stepping.size() >= 6);
    for (const double h : stepping)
        CHECK(h == 0.0);
}

TEST_CASE("look: a map begun far off is begun again from where the last stretch finds her, 2026-11-06")
{
    // research/sessions/s47-full.log: the map the walk to the guidance totem judged by was begun at
    // the glasses case upstairs, (-1017, -7812), and touched the totem's place where the stairs' road
    // came to it, at (-1656, -7356); Ashley's last stretch found her at (-1632, -7374), on the near
    // side. Walked to again, a map begun from there touched the place 18 cm from her.
    CHECK(look::FreshSpotMap(std::hypot(-1017.0 - -1632.0, -7812.0 - -7374.0)));
    // A map begun where she stands, or within the stretch's reach, is kept.
    CHECK_FALSE(look::FreshSpotMap(0.0));
    CHECK_FALSE(look::FreshSpotMap(look::kStretchReach));
}

TEST_CASE("look: on a beam the lead sounds where she faces in the stick's frame, chapter 5")
{
    // act4_2's BP_BeamWalk (ABP_BeamTemplate): she moves only while the stick, past half, points in
    // the world within 46 degrees of where she faces (research/mechanics/gaps.md).
    // She faces 60 degrees right of the stick's forward: the lead is on the right.
    auto lead = look::LeadAlongBeam(60.0, 0.0, 0.0);
    CHECK(lead.pan == doctest::Approx(std::sin(60.0 * std::numbers::pi / 180.0)));
    CHECK_FALSE(lead.locked);
    CHECK_FALSE(lead.muffled);
    // The stick pushed right: 30 degrees off where she faces, within the cone: the double ping.
    CHECK(look::LeadAlongBeam(60.0, 1.0, 0.0).locked);
    // Pushed straight up: 60 degrees off, outside the cone.
    CHECK_FALSE(look::LeadAlongBeam(60.0, 0.0, 1.0).locked);
    // Barely pushed: under half, the beam does not move her.
    CHECK_FALSE(look::LeadAlongBeam(60.0, 0.4, 0.0).locked);
    // She faces the camera's way back: the lead is muffled, the stick down.
    lead = look::LeadAlongBeam(170.0, 0.0, -1.0);
    CHECK(lead.muffled);
    CHECK(lead.locked);
}

TEST_CASE("look: on a staircase the walk pushes along the stairs, toward the end its way leaves by: the lodge's lower hall, 2026-11-12")
{
    // research/sessions/s72-play.log, 11:43:54 to 11:44:16. The way to the smashed vase ran down the
    // stairs into the lower hall along x 1929 (2855 cm high at y -7381, 2574 at -7881), then on along
    // the hall to the vase at (1969, -9115). The stairs held Ashley on her track at x 1862 to 1894.
    const look::Vec bottom{1870, -7881, 2574};
    const look::Vec top{1870, -7381, 2855};
    const look::Vec here{1862, -7805, 2723};
    const std::vector<look::Vec> ahead{{1929, -7841, 2600}, {1929, -7881, 2574}, {1929, -8761, 2555}, {1949, -8801, 2555}, {1949, -9041, 2538}};
    const look::Vec vase{1969, -9115, 2536};
    const look::Vec rise{0.0, 1.0, 0.0};
    const auto dot = [](const look::Vec& a, const look::Vec& b) { return a.x * b.x + a.y * b.y; };
    // The walk pushed toward the next corner, 76 cm off and 67 of it aside: barely down the stairs,
    // and leaning 40 degrees as it does when stuck, up them.
    const double toCorner = std::atan2(ahead[0].y - here.y, ahead[0].x - here.x);
    const look::Vec leaned{std::cos(toCorner + 40.0 * std::numbers::pi / 180.0), std::sin(toCorner + 40.0 * std::numbers::pi / 180.0), 0.0};
    CHECK(dot(look::Vec{std::cos(toCorner), std::sin(toCorner), 0.0}, rise) > -0.5);
    CHECK(dot(leaned, rise) > 0.0);
    // Along the stairs, down: the way's first move along them is down.
    const auto decided = look::StairPush(here, bottom, top, ahead, vase, 0);
    CHECK(decided.way == -1);
    CHECK(dot(decided.push, rise) == doctest::Approx(-1.0));
    CHECK(std::hypot(decided.push.x, decided.push.y) == doctest::Approx(1.0));
    // A corner beside her says nothing; the first one that moves her along the stairs does.
    CHECK(look::StairPush(here, bottom, top, {{1929, -7800, 2700}, {1929, -7860, 2590}, {1929, -8761, 2555}}, vase, 0).way == -1);
    // Coming the other way, up the stairs to the hall above: up.
    const std::vector<look::Vec> upward{{1929, -7600, 2700}, {1929, -7381, 2855}, {1949, -7321, 2855}, {2329, -7381, 2968}};
    CHECK(look::StairPush(look::Vec{1866, -7700, 2700}, bottom, top, upward, look::Vec{2349, -7401, 2968}, 0).way == 1);
    // A way down that turns back under the stairs at their foot: down, though its corners run back
    // alongside them past their top's line.
    const std::vector<look::Vec> turnBack{{1870, -7870, 2580}, {1700, -7870, 2560}, {1700, -7300, 2560}};
    CHECK(look::StairPush(here, bottom, top, turnBack, look::Vec{1700, -7250, 2560}, 0).way == -1);
    // No corners to go by (the way's corners laid anew): by the target's height. The lodge's kitchen
    // door upstairs lies past the stairs' bottom along them, and above her: up.
    CHECK(look::StairPush(here, bottom, top, {}, look::Vec{2344, -8665, 2968}, 0).way == 1);
    CHECK(look::StairPush(here, bottom, top, {}, vase, 0).way == -1);
    // Level with her: where it lies along them; beside her: the way last pushed.
    CHECK(look::StairPush(here, bottom, top, {}, look::Vec{1870, -7500, 2740}, 0).way == 1);
    CHECK(look::StairPush(here, bottom, top, {}, look::Vec{1950, -7810, 2730}, -1).way == -1);
    CHECK(look::StairPush(here, bottom, top, {}, look::Vec{1950, -7810, 2730}, 0).way == 1);
}

TEST_CASE("look: a thing is reached only where it can be used, or a look of the player's is all it waits for, 2026-11-07")
{
    // act4_1, the kitchen's blood after its first use (research/sessions/s49-full.log, 00:40:05):
    // Chris put back inside its volume, everything in place but the look, on a fixed shot that left
    // the glint out ("the glint's verdict no on layer 4: another camera, overlapping yes, on the
    // screen no"). The list said "arrived" while no press was taken and no turn of his brought it.
    look::Parts used;
    used.measured = true;
    used.atHand = true;
    used.verdict = false;
    used.byGlint = true;
    used.explorationView = false;
    used.onScreen = false;
    used.characterOk = true;
    CHECK_FALSE(look::CanBeLed(used));
    CHECK_FALSE(look::Arrived(used, false, true, false));
    // Reached before, at rest: still not, once the shot leaves it out.
    CHECK_FALSE(look::Arrived(used, false, true, true));
    // Before its use, the game would take the press: reached.
    look::Parts ready = used;
    ready.verdict = true;
    CHECK(look::Arrived(ready, false, true, false));
    // A thing that starts by itself.
    CHECK(look::Arrived(used, true, false, false));
    // On the exploration camera, only the camera's look missing: the player's to make, so reached
    // once she stands still, and kept while she moves within it.
    look::Parts look = used;
    look.explorationView = true;
    look.onScreen = true;
    CHECK(look::CanBeLed(look));
    CHECK(look::Arrived(look, false, true, false));
    CHECK_FALSE(look::Arrived(look, false, false, false));
    CHECK(look::Arrived(look, false, false, true));
    // Out of its place (nothing at hand): not reached, however near.
    look::Parts outside = look;
    outside.atHand = false;
    CHECK_FALSE(look::Arrived(outside, false, true, true));
}

TEST_CASE("she moves by where she stands, not by the game's velocity: the cable car's station, 2026-11-07")
{
    // research/sessions/s55-full.log: Matt walked at 112 cm/s, then pushed against something at
    // (-14394, -23869) for five seconds, the game's velocity 111 to 140 cm/s, his position unchanged.
    using look::Vec;
    look::Motion motion;
    double now = 0.0;
    Vec at{-14402.0, -23757.0, -651.0};
    for (int i = 0; i < 40; ++i, now += 0.025)
    {
        at.y -= 112.0 * 0.025;
        motion.Update(now, at);
    }
    CHECK(motion.Moving(now));
    CHECK_FALSE(motion.Still(now));
    CHECK(motion.Speed(now) == doctest::Approx(112.0).epsilon(0.15));
    const double stuckFrom = now;
    for (int i = 0; i < 200; ++i, now += 0.025)
        motion.Update(now, at);
    CHECK_FALSE(motion.Moving(now));
    CHECK(motion.Still(now));
    CHECK(now - stuckFrom > look::kMovingWindow);
    // A turn on the spot is still.
    for (int i = 0; i < 20; ++i, now += 0.025)
        motion.Update(now, Vec{at.x + (i % 2) * 0.5, at.y, at.z});
    CHECK(motion.Still(now));
    // Nothing known yet is neither.
    look::Motion fresh;
    CHECK_FALSE(fresh.Moving(0.0));
    CHECK_FALSE(fresh.Still(0.0));
    fresh.Update(0.0, at);
    CHECK_FALSE(fresh.Still(0.1)); // not watched for a whole window yet
}

TEST_CASE("look: the look's lead tells nearer from further over the whole turn the game takes the look over")
{
    // The audit of 2026-11-08: the game took looks with the glint up to 80 degrees across and 24 up or
    // down of it; s56 at 23:48:15 had the camera 79 degrees off and 30 down, s47 46 and 91 off.
    const auto far = look::LookSound(-79.0, -30.0, false);
    const auto nearer = look::LookSound(-38.0, -10.0, false);
    const auto near = look::LookSound(-10.0, -2.0, false);
    CHECK(far.pan == doctest::Approx(-1.0));
    CHECK(nearer.pan > far.pan);
    CHECK(near.pan > nearer.pan);
    CHECK(near.pan < 0.0);
    CHECK(far.level < nearer.level);
    CHECK(nearer.level < near.level);
    CHECK(near.level < 0.5);
    CHECK(far.interval > nearer.interval);
    CHECK(nearer.interval > near.interval);
    // Looked at: the double ping's pace; behind her: muffled.
    const auto on = look::LookSound(-5.0, 0.0, true);
    CHECK(on.locked);
    CHECK(on.interval == doctest::Approx(0.12));
    CHECK(look::LookSound(150.0, 0.0, false).muffled);
}

TEST_CASE("look: the thing just used is led to no more until it is chosen again or she leaves its place, the lodge's patio door, 2026-11-07")
{
    // research/sessions/s56-full.log, 23:51:41 to 23:51:48: Sam tried the locked patio door, was handed
    // back in its place facing away, and "Повернитесь к цели." was said for it at once.
    int door = 0, photo = 0;
    look::JustUsed used;
    CHECK_FALSE(used.Waits(&door)); // not used yet: led
    used.Used(&door);
    CHECK(used.Waits(&door));
    CHECK(used.Waits(&door)); // still, while she stands there
    // She leaves its place: it is let go, and led again when she comes back.
    CHECK_FALSE(used.Waits(nullptr));
    CHECK_FALSE(used.Waits(&door));
    // Chosen again by the player: led at once.
    used.Used(&door);
    used.Chosen();
    CHECK_FALSE(used.Waits(&door));
    // Another thing offered lets the used one go.
    used.Used(&door);
    CHECK_FALSE(used.Waits(&photo));
    CHECK_FALSE(used.Waits(&door));
}

TEST_CASE("the walk lets go where her slide ends at the way's end: the damaged doors, 2026-11-08")
{
    // research/sessions/s62-play.log, 21:56:47 to 21:56:49.8: let go at 138 cm/s as the place took
    // her, she came to rest 139 cm on, 77 cm past the spot found well inside the place, and out of it.
    look::Slide slide;
    CHECK(slide.Distance(138.0) == 0.0); // nothing measured yet: the walk lets go a step before the end
    slide.LetGo(47.98, look::Vec{20119, 36232, 8398}, 138.0);
    CHECK_FALSE(slide.Update(48.5, look::Vec{20090, 36163, 8398}, false));
    CHECK(slide.Update(49.6, look::Vec{20065, 36104, 8398}, true));
    CHECK(slide.LastSeconds() == doctest::Approx(139.0 / 138.0).epsilon(0.01));
    CHECK(slide.Distance(138.0) == doctest::Approx(139.0).epsilon(0.01));
    // The next walk there, 131 cm of way left at 138 cm/s and 131 cm from her spot: it lets go now,
    // and the stride ends at the spot; a step's let-go would have run her past it again.
    const double letGoAt = std::max(look::kStepWay, slide.Distance(138.0));
    CHECK(look::StretchKeeps(false, false, 131.0, 131.0, letGoAt));
    CHECK_FALSE(look::StretchKeeps(false, false, 131.0, 131.0));
    // A slow let-go says nothing of the stride; nor one a flick of the mod's own moved on; nor a rest
    // long after.
    slide.LetGo(60.0, look::Vec{0, 0, 0}, 30.0);
    CHECK_FALSE(slide.Update(61.0, look::Vec{20, 0, 0}, true));
    slide.LetGo(70.0, look::Vec{0, 0, 0}, 140.0);
    slide.Spoiled();
    CHECK_FALSE(slide.Update(71.0, look::Vec{300, 0, 0}, true));
    slide.LetGo(80.0, look::Vec{0, 0, 0}, 140.0);
    CHECK_FALSE(slide.Update(80.0 + look::kSlideWithin + 0.1, look::Vec{140, 0, 0}, true));
    CHECK(slide.Distance(100.0) == doctest::Approx(100.0 * 139.0 / 138.0).epsilon(0.01));
    // The median of the last ones measured, and never past the stretch's reach.
    for (const double seconds : {0.4, 1.1, 1.05, 3.0})
    {
        slide.LetGo(90.0, look::Vec{0, 0, 0}, 100.0);
        CHECK(slide.Update(91.0, look::Vec{seconds * 100.0, 0, 0}, true));
    }
    CHECK(slide.Distance(100.0) == doctest::Approx(105.0)); // of 0.4, 1.007, 1.05, 1.1 and 3.0 s: 1.05
    CHECK(slide.Distance(1000.0) == look::kStretchReach);
}

TEST_CASE("the walk lets go early only on the way's last straight leg, heading along it")
{
    // Her slide goes on the way she heads: a corner within it is walked round first.
    CHECK(look::LetGoAt(139.0, true, 5.0) == doctest::Approx(139.0));
    CHECK(look::LetGoAt(139.0, true, -look::kSlideAlong) == doctest::Approx(139.0));
    CHECK(look::LetGoAt(139.0, false, 5.0) == look::kStepWay); // a corner still to turn
    CHECK(look::LetGoAt(139.0, true, 40.0) == look::kStepWay); // turning onto the leg yet
    CHECK(look::LetGoAt(139.0, true, std::nullopt) == look::kStepWay);
    CHECK(look::LetGoAt(30.0, true, 0.0) == look::kStepWay); // never later than a step
    // The way she goes, by where she stands.
    look::Motion motion;
    for (int i = 0; i <= 10; ++i)
        motion.Update(i * 0.05, look::Vec{i * 7.0, 0.0, 0.0});
    REQUIRE(motion.Heading(0.5).has_value());
    CHECK(*motion.Heading(0.5) == doctest::Approx(0.0).epsilon(1e-6));
    look::Motion north;
    for (int i = 0; i <= 10; ++i)
        north.Update(i * 0.05, look::Vec{0.0, i * 7.0, 0.0});
    CHECK(*north.Heading(0.5) == doctest::Approx(90.0).epsilon(1e-6));
    look::Motion still;
    for (int i = 0; i <= 10; ++i)
        still.Update(i * 0.05, look::Vec{0.0, 0.0, 0.0});
    CHECK_FALSE(still.Heading(0.5).has_value());
}

TEST_CASE("a press is taken from her as the game asks it of her, whoever its tick judged: the lair's pool, 2026-11-09")
{
    // research/sessions/s63-play.log 01:12:34.829: Sam inside "wade into pool", the glint's verdict yes
    // on the exploration camera; the tick had cached Mike, who walked in after her.
    CHECK(look::PressTaken(true, true, true, true, 1, true, true));
    // Not at hand: nothing is taken.
    CHECK_FALSE(look::PressTaken(false, false, false, false, 1, false, false));
    // No look asked, or a look with no glint needed: taken at hand.
    CHECK(look::PressTaken(true, false, true, false, 1, false, false));
    CHECK(look::PressTaken(true, true, false, false, 1, false, false));
    // A glint needed and none: never.
    CHECK_FALSE(look::PressTaken(true, true, true, false, 1, false, false));
    // Off the navigation and framing layers the look is passed.
    CHECK(look::PressTaken(true, true, true, true, 12, false, false));
    // On them, as the look is measured.
    CHECK_FALSE(look::PressTaken(true, true, true, true, 4, true, false));
    CHECK_FALSE(look::PressTaken(true, true, true, true, 1, false, true));
}

TEST_CASE("look: a glint the game has not put up for her is a step to make, not a look: Sam's drop-down, 2026-11-09")
{
    // research/sessions/s65-play.log, 14:57:20 to 15:01:54: inside the drop-down's volume, facing it,
    // the glint's widget not on the screen because the game had not spawned it (she stood outside
    // the glint's own volume); she was told to look at it, said reached, and the walk key's walks
    // ended at once.
    look::Parts p;
    p.measured = true;
    p.atHand = true;
    p.byGlint = true;
    p.explorationView = true;
    p.onScreen = false;
    p.cameraOk = false;
    p.characterOk = true;
    p.glintAway = true;
    CHECK_FALSE(look::CanBeLed(p));
    CHECK(look::Missing(p) == look::Lead::None);
    CHECK_FALSE(look::Arrived(p, false, true, false));
    CHECK_FALSE(look::GlintMissing(p, true, true));
    // The glint put up: the camera's look is what is missing, as before.
    p.glintAway = false;
    CHECK(look::CanBeLed(p));
    CHECK(look::Missing(p) == look::Lead::Camera);
    CHECK(look::Arrived(p, false, true, false));
    // Put up but not on the screen while the camera looks at it: missing.
    CHECK(look::GlintMissing(p, true, true));
}

TEST_CASE("look: into the glint's volume she steps toward where its box meets the use location's")
{
    const look::Vec useLow{5580.0, 7236.0, 4581.0}, useHigh{5780.0, 7382.0, 4720.0};
    const look::Vec glintLow{5600.0, 7300.0, 4600.0}, glintHigh{5760.0, 7500.0, 4660.0};
    const look::Vec step = look::GlintStepPoint(useLow, useHigh, glintLow, glintHigh, 4675.0);
    CHECK(step.x == doctest::Approx(5680.0));
    CHECK(step.y == doctest::Approx(7341.0));
    CHECK(step.z == doctest::Approx(4675.0));
    // Boxes that do not meet: the glint's own middle.
    const look::Vec apart = look::GlintStepPoint(useLow, useHigh, look::Vec{5600.0, 7600.0, 4600.0}, look::Vec{5700.0, 7800.0, 4660.0}, 4675.0);
    CHECK(apart.x == doctest::Approx(5650.0));
    CHECK(apart.y == doctest::Approx(7700.0));
}

TEST_CASE("look: the walk key at a thing reached says it, asks for the camera's look, or walks: Sam's drop-down, 2026-11-09")
{
    using look::Lead;
    using look::WalkKey;
    // The press would be taken, or she stands in the way on: there.
    CHECK(look::WalkKeyAt(true, false, true, Lead::None) == WalkKey::Reached);
    CHECK(look::WalkKeyAt(true, true, false, Lead::None) == WalkKey::Reached);
    // Only the camera's look is missing: the look is asked for, no walk begun.
    CHECK(look::WalkKeyAt(true, false, false, Lead::Camera) == WalkKey::Look);
    // Her own turn, or a step into the glint's volume (no turn completes it): the walk does it.
    CHECK(look::WalkKeyAt(true, false, false, Lead::Character) == WalkKey::Walk);
    CHECK(look::WalkKeyAt(true, false, false, Lead::None) == WalkKey::Walk);
    // Not reached: walked to, whatever the look.
    CHECK(look::WalkKeyAt(false, false, false, Lead::Camera) == WalkKey::Walk);
    CHECK(look::WalkKeyAt(false, true, true, Lead::None) == WalkKey::Walk);
}

TEST_CASE("look: the last stretch gives up only once she has settled after its last push: Hannah's diary, 2026-11-10")
{
    // research/sessions/s66-play.log: the last stretch began at 16:20:00.060; its eighth flick at
    // 16:20:07.707; at 16:20:08.066 the walk said it could not get there, she 30 cm out of the place;
    // at 16:20:08.619 the game offered the diary, only the camera's look missing.
    const double begun = 0.0;
    const double flick8 = 7.647;
    // At the old give-up, 0.36 s after the flick: not yet, though she reads still (a turn on the spot).
    CHECK_FALSE(look::GivesUp(8.006 - begun, 8.006 - flick8, true));
    // As the game offers the diary, 0.9 s after the flick: still waiting (the look-only branch takes it).
    CHECK_FALSE(look::GivesUp(8.559 - begun, 8.559 - flick8, true));
    // Settled long enough after the last push: it gives up.
    CHECK(look::GivesUp(9.0 - begun, 9.0 - flick8, true));
    // Still moving: it waits, but not past its last wait.
    CHECK_FALSE(look::GivesUp(10.0, 3.0, false));
    CHECK(look::GivesUp(look::kArriveMost + look::kGiveUpWaitMost + 0.1, 0.0, false));
    // Within its time it never gives up.
    CHECK_FALSE(look::GivesUp(look::kArriveMost, 10.0, true));
}

TEST_CASE("look: held to a path and level with a thing not offered, the walk waits for her slide: the stream, 2026-11-10")
{
    // research/sessions/s66-play.log: at 16:42:17.284 the walk found the climb out of the stream level
    // with her on the path and gave up; at 16:42:17.753 the game offered it, she still sliding on.
    CHECK_FALSE(look::PathLevelGivesUp(0.0, false));
    CHECK_FALSE(look::PathLevelGivesUp(0.469, false));
    // At rest at once (pressed against something): a moment, then it gives up.
    CHECK_FALSE(look::PathLevelGivesUp(0.2, true));
    CHECK(look::PathLevelGivesUp(look::kPathLevelSettle, true));
    // Never longer than its last wait.
    CHECK(look::PathLevelGivesUp(look::kPathLevelWaitMost, false));
}

TEST_CASE("last stretch: given up out of the place, the walk steps back along its way and walks in again")
{
    // The bathroom door (s71 23:35): the way came along x and turned toward the door's place.
    const std::vector<look::Vec> route{{3715, -9425, 3388}, {3687, -9431, 3386}, {3567, -9431, 3386}, {3567, -9411, 3386}};
    // 150 cm back from the way's end: past its last two legs (20 + 120 cm), 10 cm along the first.
    const auto back = look::BackOffPoint(route, 150.0);
    REQUIRE(back);
    CHECK(back->x == doctest::Approx(3687.0 + 28.0 * 10.0 / std::hypot(28.0, 6.0)).epsilon(0.001));
    CHECK(back->y == doctest::Approx(-9431.0 + 6.0 * 10.0 / std::hypot(28.0, 6.0)).epsilon(0.001));
    // A way shorter than the step back (169 cm against 200): its start.
    const auto start = look::BackOffPoint(route);
    REQUIRE(start);
    CHECK(start->x == doctest::Approx(3715.0));
    CHECK(start->y == doctest::Approx(-9425.0));
    // Within the last leg.
    const auto near = look::BackOffPoint(route, 10.0);
    REQUIRE(near);
    CHECK(near->y == doctest::Approx(-9421.0));
    // No way: nothing to step back along.
    CHECK_FALSE(look::BackOffPoint({}));
}

TEST_CASE("look: a framing camera that ends only by distance is walked out of: the old hotel kitchen's chains and knives, s73 11:35:35 to 11:37:34")
{
    // The shot Ashley's scene left lay over the kitchen until she walked 4 m to the photos; the chains'
    // glint lay off it, and every step about their place moved her nowhere. The game ends such a shot
    // once she stands farther than DistanceToTravel from where it began (in three dimensions).
    look::Framing f;
    f.waitsForDistance = true;
    f.distance = 300.0;
    f.start = look::Vec{-2458.0, -6675.0, 1804.0};
    const look::Vec atChains{-2472.0, -6596.0, 1804.0};
    CHECK(look::FramingLeft(f, atChains) == doctest::Approx(300.0 - std::hypot(14.0, 79.0)));
    // Far enough: nothing left; on another floor the height counts too.
    CHECK(look::FramingLeft(f, look::Vec{-2458.0, -6975.0, 1804.0}) == 0.0);
    CHECK(look::FramingLeft(f, look::Vec{-2458.0, -6675.0, 1804.0 + 400.0}) == 0.0);
    // A shot that ends at a push of the stick waits for no distance.
    look::Framing push = f;
    push.waitsForDistance = false;
    CHECK(look::FramingLeft(push, atChains) == 0.0);
    // The way out: on the line from where it began through her, kFramingMargin past its distance, at her
    // height; turned about where it began where that is walled.
    const look::Vec straight = look::FramingExit(f, atChains, 0.0, 0.0);
    CHECK(std::hypot(straight.x - f.start.x, straight.y - f.start.y) == doctest::Approx(f.distance + look::kFramingMargin));
    CHECK(straight.z == atChains.z);
    const double outward = std::atan2(atChains.y - f.start.y, atChains.x - f.start.x);
    CHECK(std::atan2(straight.y - f.start.y, straight.x - f.start.x) == doctest::Approx(outward));
    const look::Vec turned = look::FramingExit(f, atChains, 0.0, 90.0);
    CHECK(std::hypot(turned.x - f.start.x, turned.y - f.start.y) == doctest::Approx(f.distance + look::kFramingMargin));
    CHECK(std::remainder(std::atan2(turned.y - f.start.y, turned.x - f.start.x) - (outward + std::numbers::pi / 2.0), 2.0 * std::numbers::pi) ==
          doctest::Approx(0.0).epsilon(1e-9));
    // Standing where it began: straight on from her forward.
    const look::Vec fromStart = look::FramingExit(f, f.start, 90.0, 0.0);
    CHECK(fromStart.x == doctest::Approx(f.start.x));
    CHECK(fromStart.y == doctest::Approx(f.start.y + f.distance + look::kFramingMargin));
    // The turns tried begin with the straight way and go both ways.
    CHECK(look::kFramingTurns.front() == 0.0);
    CHECK(look::kFramingTurns[1] == -look::kFramingTurns[2]);
}

TEST_CASE("look: a last stretch with no way to step back along steps back away from its spot: the cart in the mine, s73 13:22:23 to 13:22:41")
{
    // The stretch's pushes moved her nowhere 37 cm from her spot and the walk had no way of its own to step
    // back along; walked to again from 1.7 m off, she was in the place at once.
    const look::Vec spot{-32645.0, 1671.0, -6960.0};
    const look::Vec here{-32667.0, 1640.0, -6965.0};
    const look::Vec back = look::BackOffFrom(spot, here, 0.0, 0.0);
    CHECK(std::hypot(back.x - spot.x, back.y - spot.y) == doctest::Approx(std::hypot(here.x - spot.x, here.y - spot.y) + look::kBackOff));
    CHECK(std::hypot(back.x - here.x, back.y - here.y) == doctest::Approx(look::kBackOff));
    CHECK(back.z == here.z);
    // Turned about the spot where the straight way back is walled, as far from the spot.
    const look::Vec turned = look::BackOffFrom(spot, here, 0.0, 70.0);
    CHECK(std::hypot(turned.x - spot.x, turned.y - spot.y) == doctest::Approx(std::hypot(here.x - spot.x, here.y - spot.y) + look::kBackOff));
    // At the spot itself: straight back from her forward.
    const look::Vec fromSpot = look::BackOffFrom(spot, spot, 180.0, 0.0);
    CHECK(fromSpot.x == doctest::Approx(spot.x - look::kBackOff));
    CHECK(fromSpot.y == doctest::Approx(spot.y).epsilon(1e-6));
}
