// features/ShootingRules: which target the aim sound leads to, when the word to fire is said, and the
// blip's pace, side and pitch.
#include "Support.hpp"
#include "features/ShootingRules.hpp"

#include <algorithm>
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

TEST_CASE("shoot: an aim-only stage is led past the turn that completes it: the rifle scope, chapter 4")
{
    // act3_2's use_scope: AimOnlyStageCompletionConditions[0] is AimAxis YAW, GREATER_OR_EQUAL, 20.0
    // (research/mechanics/gaps.md); no target, no clock, the scene waits for the turn.
    const std::vector<shoot::AimCondition> scope{{1, 20.0, 5}};
    // Looking where it began: the turn lies 23 degrees right (past the threshold by the margin).
    auto s = shoot::AimGoal(scope, 0.0, -2.0);
    CHECK(s.placed);
    CHECK_FALSE(s.onTarget);
    CHECK(s.horizontal == doctest::Approx(20.0 + shoot::kAimGoalMargin));
    CHECK(s.vertical == doctest::Approx(0.0));
    CHECK(shoot::BlipOf(s).pan > 0.9);
    // Turned the wrong way: still right, farther.
    CHECK(shoot::AimGoal(scope, -10.0, 0.0).horizontal == doctest::Approx(33.0));
    // At 20 degrees the stage completes: on target, the double ping.
    s = shoot::AimGoal(scope, 20.0, 0.0);
    CHECK(s.onTarget);
    CHECK(shoot::BlipOf(s).locked);
    // A pitch condition leads up or down; one stage asks both at once.
    const std::vector<shoot::AimCondition> both{{1, -15.0, 3}, {0, 10.0, 4}};
    s = shoot::AimGoal(both, 0.0, 0.0);
    CHECK(s.horizontal == doctest::Approx(-18.0));
    CHECK(s.vertical == doctest::Approx(13.0));
    CHECK_FALSE(s.onTarget);
    CHECK(shoot::AimGoal(both, -16.0, 11.0).onTarget);
    // No condition: nothing to lead to.
    CHECK_FALSE(shoot::AimGoal({}, 0.0, 0.0).placed);
}

TEST_CASE("a hidden stage the game resolves by itself has no verdict: the Wendigo's dodge, 2026-11-07")
{
    // research/sessions/s49-full.log, 00:26:21: after the missed shot the stage Wendigo_Dodge (targets
    // and reticle hidden) took its only option, "aim timeout", 0.15 s in; the hit's tone was played.
    shoot::Ending e;
    e.chosenChanged = true;
    e.unseen = true;
    CHECK(shoot::Judge(e) == shoot::Verdict::None);
    e.unseen = false;
    CHECK(shoot::Judge(e) == shoot::Verdict::Hit);
    // Nor a clock run out on it.
    shoot::Ending t;
    t.timed = true;
    t.limit = 5.0;
    t.seconds = 5.0;
    t.unseen = true;
    CHECK(shoot::Judge(t) == shoot::Verdict::None);
}

TEST_CASE("the death room's dilemma is judged by the yaw's place between the stage's limits, as the game highlights it")
{
    // act5_3's StartPose (research/mechanics/shooting_stages.tsv): yaw -5 to 3.7, the middle 0.5.
    const auto mid = shoot::DilemmaAt(0.0, -5.0, 3.7, 0.5);
    CHECK(mid.t == doctest::Approx(5.0 / 8.7));
    CHECK(mid.region == -1);
    CHECK(mid.toFirst == doctest::Approx(-2.825));  // the first side's edge at -5 + 8.7 / 4
    CHECK(mid.toSecond == doctest::Approx(1.525)); // the second's at -5 + 8.7 * 3 / 4
    const auto chris = shoot::DilemmaAt(-3.0, -5.0, 3.7, 0.5);
    CHECK(chris.region == 0);
    CHECK(chris.toFirst == 0.0);
    const auto ashley = shoot::DilemmaAt(1.6, -5.0, 3.7, 0.5);
    CHECK(ashley.region == 1);
    CHECK(ashley.toSecond == 0.0);
    // The shots begin on the side their names give: ShootChoice_AimAsh (-8.7 to 0) on the second,
    // ShootChoice_AimChris (0 to 8.7) on the first, the aim at 0 as a stage begins.
    CHECK(shoot::DilemmaAt(0.0, -8.7, 0.0, 0.5).region == 1);
    CHECK(shoot::DilemmaAt(0.0, 0.0, 8.7, 0.5).region == 0);
    // Limits that make no span: the middle, nothing highlighted.
    CHECK(shoot::DilemmaAt(0.0, 0.0, 0.0, 0.5).region == -1);
}

TEST_CASE("the dilemma's sound leads to no side the player has not chosen, and to the one he has")
{
    const auto mid = shoot::DilemmaAt(0.0, -5.0, 3.7, 0.5);
    // Nothing chosen: the aim's place between the sides, at a steady pace, no lead.
    auto lead = shoot::LeadDilemma(mid, -1, false, false);
    CHECK(lead.blip.pan == doctest::Approx(2.0 * mid.t - 1.0));
    CHECK(lead.interval == doctest::Approx(shoot::kDilemmaPace));
    CHECK_FALSE(lead.blip.locked);
    // The aiming's X axis inverted: the sides change ears with the stick.
    CHECK(shoot::LeadDilemma(mid, -1, false, true).blip.pan == doctest::Approx(-(2.0 * mid.t - 1.0)));
    // A side highlighted: the double ping.
    lead = shoot::LeadDilemma(shoot::DilemmaAt(1.6, -5.0, 3.7, 0.5), -1, true, false);
    CHECK(lead.blip.locked);
    CHECK(lead.interval == doctest::Approx(shoot::kOnTargetInterval));
    // Chosen by key: led toward it, past its edge.
    lead = shoot::LeadDilemma(mid, 1, false, false);
    CHECK(lead.blip.pan > 0.0);
    CHECK_FALSE(lead.blip.locked);
    CHECK(shoot::LeadDilemma(mid, 1, false, true).blip.pan < 0.0);
    lead = shoot::LeadDilemma(mid, 0, false, false);
    CHECK(lead.blip.pan < 0.0);
    // Within the chosen side: on target.
    CHECK(shoot::LeadDilemma(shoot::DilemmaAt(-3.0, -5.0, 3.7, 0.5), 0, true, false).blip.locked);
}

namespace
{
    // The angle in degrees between two directions.
    double Apart(const shoot::Vec& a, const shoot::Vec& b)
    {
        const double dot = a.x * b.x + a.y * b.y + a.z * b.z;
        const double la = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
        const double lb = std::sqrt(b.x * b.x + b.y * b.y + b.z * b.z);
        return std::acos(std::clamp(dot / (la * lb), -1.0, 1.0)) * 180.0 / 3.14159265358979323846;
    }
}

TEST_CASE("the aim put on a target aims along the camera's line to it, wherever it stands, 2026-11-08")
{
    // The stage aims along the start's direction turned by its two angles (UpdateStage: Euler of
    // MakeFromXZ(start - camera, up), pitch and yaw added, MakeFromEuler, Vector). The angles AimOnto
    // gives must aim along the line from the camera to the target, for targets on every side, near
    // and far, above and below, and for a start on either side of the yaw's seam at 180 degrees.
    const std::vector<shoot::Vec> cameras{{0, 0, 0}, {4551.2, -7686.2, 3071.1}, {-27235.4, 12406.0, -5694.2}};
    const std::vector<shoot::Vec> starts{{1000, 0, 0}, {-1000, 5, 40}, {-1000, -5, -40}, {300, 900, 120}, {-20, -1500, -300}};
    const std::vector<shoot::Vec> targets{{1200, 150, 60}, {-990, -30, 10}, {-990, 40, 70}, {500, 700, 300}, {800, -1300, -500}, {50, 50, 900}};
    int cases = 0;
    for (const auto& c : cameras)
    {
        for (const auto& s0 : starts)
        {
            for (const auto& t0 : targets)
            {
                const shoot::Vec start{c.x + s0.x, c.y + s0.y, c.z + s0.z};
                const shoot::Vec target{c.x + t0.x, c.y + t0.y, c.z + t0.z};
                const auto angles = shoot::AimOnto(c, start, target);
                CHECK(std::fabs(angles.yaw) <= 180.0);
                const auto aimed = shoot::AimDirection(c, start, angles);
                CHECK(Apart(aimed, t0) < 1e-6);
                ++cases;
            }
        }
    }
    CHECK(cases == 90);
    // Across the seam the yaw is taken the short way: a start at 179 degrees and a target at -179
    // is 2 degrees to the right, not 358 to the left.
    const shoot::Vec camera{0, 0, 0};
    const double r = 1000.0;
    const double d = 3.14159265358979323846 / 180.0;
    const auto across = shoot::AimOnto(camera, {r * std::cos(179 * d), r * std::sin(179 * d), 0}, {r * std::cos(-179 * d), r * std::sin(-179 * d), 0});
    CHECK(across.yaw == doctest::Approx(2.0).epsilon(1e-9));
    CHECK(across.pitch == doctest::Approx(0.0).epsilon(1e-9));
    // A target straight on the start's line asks for no turn at all; one higher up for pitch alone.
    const auto ahead = shoot::AimOnto(camera, {1000, 0, 0}, {2000, 0, 0});
    CHECK(std::fabs(ahead.yaw) < 1e-9);
    CHECK(std::fabs(ahead.pitch) < 1e-9);
    const auto up = shoot::AimOnto(camera, {1000, 0, 0}, {1000, 0, 1000});
    CHECK(up.pitch == doctest::Approx(45.0));
    CHECK(std::fabs(up.yaw) < 1e-9);
}

TEST_CASE("with aim assist on, the yaw is the game's own unwrapped offset, which aims the same way")
{
    // A start at 179 degrees and a target at -179: the camera limits measure 2 to the right; the
    // game's aim assist measures -358 (CalculateAimAssistTargetBounds takes the two orientations'
    // difference unwrapped) and pulls a highlighted aim there; written the short way, the two pulled
    // against each other each frame.
    const shoot::Vec camera{0, 0, 0};
    const double r = 1000.0;
    const double d = 3.14159265358979323846 / 180.0;
    const shoot::Vec start{r * std::cos(179 * d), r * std::sin(179 * d), 0};
    const shoot::Vec target{r * std::cos(-179 * d), r * std::sin(-179 * d), 0};
    const auto assisted = shoot::AimOnto(camera, start, target, true);
    CHECK(assisted.yaw == doctest::Approx(-358.0).epsilon(1e-9));
    CHECK(Apart(shoot::AimDirection(camera, start, assisted), target) < 1e-6);
    CHECK(shoot::AimOnto(camera, start, target, false).yaw == doctest::Approx(2.0).epsilon(1e-9));
    // Away from the seam the two agree.
    const auto plain = shoot::AimOnto(camera, {1000, 0, 0}, {1000, 300, 100}, false);
    const auto both = shoot::AimOnto(camera, {1000, 0, 0}, {1000, 300, 100}, true);
    CHECK(plain.yaw == doctest::Approx(both.yaw));
    CHECK(plain.pitch == doctest::Approx(both.pitch));
}

TEST_CASE("the walk key puts the aim on targets to shoot, and not on the scope, hidden targets, a highlight that ends the stage or the dilemma")
{
    // research/mechanics/shooting_stages.tsv: the range's rounds, the Wendigo fights' stages of one
    // target and of six (act7_2, act8_1).
    CHECK(shoot::Snaps({1, false, false, false, false}));
    CHECK(shoot::Snaps({6, false, false, false, false}));
    // act3_2's rifle scope is aimed only, and its aim is what it waits for.
    CHECK_FALSE(shoot::Snaps({0, true, false, false, false}));
    // act7_2's barrel stage has no target.
    CHECK_FALSE(shoot::Snaps({0, false, false, false, false}));
    // act3_2's Wendigo_Dodge hides its targets and the game resolves it.
    CHECK_FALSE(shoot::Snaps({1, false, true, false, false}));
    // A stage a highlight ends: the aim put on a target would choose it.
    CHECK_FALSE(shoot::Snaps({2, false, false, true, false}));
    // The death room's dilemma, its first turn and each shot after it (Andrey: left as it is).
    CHECK_FALSE(shoot::Snaps({2, false, true, true, true}));
    CHECK_FALSE(shoot::Snaps({2, false, false, false, true}));
}

TEST_CASE("the aim is held on its target until the stage ends, the target goes, or the player aims herself")
{
    CHECK(shoot::LockHolds(true, true, false) == shoot::Lock::Holds);
    CHECK(shoot::LockHolds(true, true, true) == shoot::Lock::PlayerAims);
    CHECK(shoot::LockHolds(true, false, false) == shoot::Lock::TargetGone);
    CHECK(shoot::LockHolds(false, true, false) == shoot::Lock::StageOver);
    CHECK(shoot::LockHolds(false, false, true) == shoot::Lock::StageOver);
    // The aim's keys cannot be read: the player's own aim cannot be told, so it is not held.
    CHECK(shoot::LockHolds(true, true, false, false) == shoot::Lock::AimUnknown);
}

TEST_CASE("the walk key puts the aim on nothing while the fire key is held")
{
    // IA_SequenceShoot has no trigger: a held key fires the frame a target lights up.
    CHECK(shoot::SnapNow(false));
    CHECK_FALSE(shoot::SnapNow(true));
}

TEST_CASE("a stage with no target that is not aimed asks nothing as it begins: the barrel's second in act7_2")
{
    CHECK_FALSE(shoot::Asks(0, false)); // barrelidle_combat
    CHECK(shoot::Asks(0, true));        // the rifle scope, aimed only
    CHECK(shoot::Asks(1, false));
}

TEST_CASE("shooting: an aim-only stage names the way its stick turns")
{
    // s71 21:31:02: chapter 4's scope begins level, the turn that completes it 23 deg right.
    shoot::Sight goal;
    goal.placed = true;
    goal.horizontal = 23.0;
    goal.angle = 23.0;
    CHECK(shoot::AimWay(goal, false) == L"Right");
    CHECK(shoot::AimWay(goal, true) == L"Left");
    goal.horizontal = -4.0;
    goal.vertical = 12.0;
    goal.angle = 12.6;
    CHECK(shoot::AimWay(goal, false) == L"Up");
    goal.vertical = -12.0;
    CHECK(shoot::AimWay(goal, false) == L"Down");
    // Reached, or no turn named: nothing.
    goal.onTarget = true;
    CHECK(shoot::AimWay(goal, false).empty());
    CHECK(shoot::AimWay(shoot::Sight{}, false).empty());
}
