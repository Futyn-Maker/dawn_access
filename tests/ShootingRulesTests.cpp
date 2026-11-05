// features/ShootingRules: which target the aim sound leads to, when the word to fire is said, and the
// blip's pace, side and pitch.
#include "Support.hpp"
#include "features/ShootingRules.hpp"

#include <cmath>
#include <vector>

using namespace da;
using shoot::Candidate;
using shoot::Sight;
using tests::U8;

namespace
{
    // The stage is polled every frame; a frame every 1/64 s keeps the sums exact.
    constexpr double kFrame = 1.0 / 64.0;

    Sight At(double horizontal, double vertical, bool onTarget = false, bool solid = false)
    {
        Sight s;
        s.placed = true;
        s.horizontal = horizontal;
        s.vertical = vertical;
        s.angle = std::sqrt(horizontal * horizontal + vertical * vertical);
        s.onTarget = onTarget;
        s.solid = solid;
        return s;
    }

    // The times the word to fire is said over frames [from, to) with one reading.
    std::vector<double> Words(shoot::FireWord& word, int from, int to, int led, bool solid)
    {
        std::vector<double> at;
        for (int frame = from; frame < to; ++frame)
        {
            if (word.Update(frame * kFrame, led, solid)) at.push_back(frame * kFrame);
        }
        return at;
    }
}

TEST_CASE("shoot: a target is named by the designers' name of the thing, never by its place marker")
{
    // The shooting range of chapter 1: the targets are bare markers (Actor_0 .. Actor_7) and the
    // things are named by the stages a hit leads to.
    CHECK(U8(shoot::TargetLabel(false, L"", L"Shoot_Squirrel")) == "Squirrel");
    CHECK(U8(shoot::TargetLabel(false, L"", L"Shoot_Sack_2")) == "Sack");
    CHECK(U8(shoot::TargetLabel(false, L"", L"Shoot_Can_1")) == "Can");
    CHECK(U8(shoot::TargetLabel(false, L"", L"Shoot_Bottle_1")) == "Bottle");
    // The snowball fight: found by the actor's name.
    CHECK(U8(shoot::TargetLabel(true, L"mike", L"Bench_attack_Mike")) == "mike");
    CHECK(U8(shoot::TargetLabel(true, L"BASA_Robin", L"Log_pile_attack_robin")) == "Robin");
    // Nothing named: empty, and the feature counts the targets instead.
    CHECK(shoot::TargetLabel(false, L"", L"").empty());
    CHECK(shoot::TargetLabel(false, L"", L"Shoot").size() == 5);
}

TEST_CASE("shoot: the sound leads to the target a shot now takes, over a nearer one")
{
    const std::vector<Candidate> targets{{0, At(1.0, 0.0)}, {1, At(2.5, 0.5, true)}};
    CHECK(shoot::Lead(targets, -1, -1) == 1);
    // Two under the shot: the nearer of them.
    const std::vector<Candidate> both{{0, At(1.0, 0.0, true)}, {1, At(2.5, 0.5, true)}};
    CHECK(shoot::Lead(both, -1, 1) == 0);
}

TEST_CASE("shoot: a target chosen by key is led alone, the shot on another or not")
{
    const std::vector<Candidate> targets{{0, At(1.0, 0.0, true)}, {1, At(20.0, 3.0)}};
    CHECK(shoot::Lead(targets, 1, 0) == 1);
    // Chosen, but not placed this poll: nothing to lead to.
    const std::vector<Candidate> unplaced{{0, At(1.0, 0.0)}, {1, Sight{}}};
    CHECK(shoot::Lead(unplaced, 1, 1) == -1);
}

TEST_CASE("shoot: the target led stays led until another is clearly nearer the aim")
{
    // Targets three degrees apart: the aim drifting between them keeps the one it led.
    std::vector<Candidate> targets{{0, At(2.0, 0.0)}, {1, At(-1.5, 0.0)}};
    CHECK(shoot::Lead(targets, -1, 0) == 0);
    targets[1].sight = At(-1.1, 0.0);
    CHECK(shoot::Lead(targets, -1, 0) == 1);
    // With nothing led before, the nearest.
    CHECK(shoot::Lead({{0, At(5.0, 0.0)}, {1, At(4.0, 0.0)}}, -1, -1) == 1);
    CHECK(shoot::Lead({}, -1, -1) == -1);
}

TEST_CASE("shoot: the word to fire comes after a fifth of a second sure of the target, once")
{
    shoot::FireWord word;
    CHECK(Words(word, 0, 64, 0, true) == std::vector<double>{0.203125});
}

TEST_CASE("shoot: an aim swept across a target does not call the word")
{
    shoot::FireWord word;
    CHECK(Words(word, 0, 10, 0, true).empty());
    CHECK(Words(word, 10, 40, 0, false).empty());
    CHECK(Words(word, 40, 50, 0, true).empty());
}

TEST_CASE("shoot: the word again only after a second off the target, or for a new target")
{
    shoot::FireWord word;
    CHECK(Words(word, 0, 32, 0, true).size() == 1);
    // Off for half a second and back: not again.
    CHECK(Words(word, 32, 64, 0, false).empty());
    CHECK(Words(word, 64, 128, 0, true).empty());
    // Off for over a second: again, a fifth of a second after it is back.
    CHECK(Words(word, 128, 200, 0, false).empty());
    CHECK(Words(word, 200, 240, 0, true) == std::vector<double>{(200 + 13) * kFrame});
    // Another target: earned afresh.
    CHECK(Words(word, 240, 280, 1, true) == std::vector<double>{(240 + 13) * kFrame});
}

TEST_CASE("shoot: a stage ends with a hit, the aim reached, the time run out, or nothing to tell")
{
    using shoot::Verdict;
    shoot::Ending e;
    e.timed = true;
    e.limit = 25.0;
    e.seconds = 7.3;
    // The press on a highlighted target: its circle goes as shot, and the chosen option changes.
    e.shotCircle = true;
    e.chosenChanged = true;
    CHECK(shoot::Judge(e) == Verdict::Hit);
    // The circle's verdict missed (its hook did not fire): the chosen option tells it alone.
    e.shotCircle = false;
    CHECK(shoot::Judge(e) == Verdict::Hit);
    // No press counted: a stage ended early by the story says nothing.
    e.chosenChanged = false;
    CHECK(shoot::Judge(e) == Verdict::None);
    // The clock ran out.
    e.seconds = 25.0;
    CHECK(shoot::Judge(e) == Verdict::TimedOut);
    // A highlighted shot after the time (a stage that waits for its end marker) is still a hit.
    e.chosenChanged = true;
    CHECK(shoot::Judge(e) == Verdict::Hit);
    // Ended while the world stood still: nothing, whatever the clock says.
    e.still = true;
    CHECK(shoot::Judge(e) == Verdict::None);
}

TEST_CASE("shoot: a stale timed-out flag is not this play's timeout; the clock tells it")
{
    // The range plays a round again after a timeout, the same stage object. The game clears the
    // timed-out flag as a stage starts; were it ever still set, the clock decides.
    using shoot::Verdict;
    shoot::Ending e;
    e.timed = true;
    e.limit = 25.0;
    e.timedOutBefore = true;
    e.timedOutNow = true;
    e.seconds = 3.0;
    // Set before and set now, its clock short of the time: not this play's timeout.
    CHECK(shoot::Judge(e) == Verdict::None);
    // Its own clock run out: the timeout.
    e.seconds = 24.96;
    CHECK(shoot::Judge(e) == Verdict::TimedOut);
    // The flag set only now: the timeout, whatever the clock says.
    e.timedOutBefore = false;
    e.seconds = 3.0;
    CHECK(shoot::Judge(e) == Verdict::TimedOut);
    // A stage without a clock never times out by it.
    shoot::Ending untimed;
    untimed.seconds = 99.0;
    CHECK(shoot::Judge(untimed) == Verdict::None);
}

TEST_CASE("shoot: an aim-only stage ends when the aim reaches what it asks")
{
    using shoot::Verdict;
    shoot::Ending e;
    e.aimingOnly = true;
    CHECK(shoot::Judge(e) == Verdict::None);
    e.aimReached = true;
    CHECK(shoot::Judge(e) == Verdict::AimReached);
    // The flag means nothing outside an aim-only stage.
    e.aimingOnly = false;
    CHECK(shoot::Judge(e) == Verdict::None);
}

TEST_CASE("shoot: the blip's pace, side and pitch")
{
    CHECK(shoot::BlipInterval(At(0.5, 0.0, true)) == doctest::Approx(0.12));
    CHECK(shoot::BlipInterval(At(0.0, 0.0)) == doctest::Approx(0.09));
    CHECK(shoot::BlipInterval(At(25.0, 0.0)) == doctest::Approx(0.49));
    CHECK(shoot::BlipInterval(At(80.0, 0.0)) == doctest::Approx(0.49));
    const auto right = shoot::BlipOf(At(6.0, -3.0));
    CHECK(right.pan == doctest::Approx(0.5));
    CHECK(right.level == doctest::Approx(0.25));
    CHECK_FALSE(right.locked);
    CHECK_FALSE(right.muffled);
    const auto far = shoot::BlipOf(At(-120.0, 20.0));
    CHECK(far.pan == doctest::Approx(-1.0));
    CHECK(far.level == doctest::Approx(1.0));
    CHECK(far.muffled);
    CHECK(shoot::BlipOf(At(0.2, 0.1, true)).locked);
}

TEST_CASE("shoot: the double ping comes at once when the aim comes on target, 2026-11-02")
{
    // Chapter one's telescope: the view was led toward Emily and Mike with blips about 0.2 s apart
    // and came to rest in the framing 46 ms after the last; the game took the view 28 ms later and
    // the interaction moved on. Waiting for the interval, the double ping never sounded.
    shoot::Cadence blips;
    CHECK(blips.Due(10.300, false, 0.2));
    CHECK_FALSE(blips.Due(10.400, false, 0.2));
    CHECK(blips.Due(10.501, false, 0.2));
    CHECK(blips.Due(10.546, true, shoot::kOnTargetInterval));
    // On target, at its own pace afterwards.
    CHECK_FALSE(blips.Due(10.600, true, shoot::kOnTargetInterval));
    CHECK(blips.Due(10.670, true, shoot::kOnTargetInterval));
    // Off target and back on: at once again.
    CHECK_FALSE(blips.Due(10.700, false, 0.3));
    CHECK(blips.Due(10.710, true, shoot::kOnTargetInterval));
    // A stage going on after a pause waits an interval before its next blip.
    blips.Rest(20.0);
    CHECK_FALSE(blips.Due(20.05, true, shoot::kOnTargetInterval));
    CHECK(blips.Due(20.13, true, shoot::kOnTargetInterval));
}
