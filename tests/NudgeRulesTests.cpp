// features/NudgeRules: where the framing a camera-nudge interaction waits for lies from the view, how
// the sound and the words lead to it, and the moment a timed light comes on.
#include "Support.hpp"
#include "features/NudgeRules.hpp"

#include <cmath>
#include <algorithm>

using namespace da;

namespace
{
    // Chapter one's telescope (act2_1): the nudge reaches 8 degrees either way across and 3 up and
    // down, and the anim graph moves on once the view rests on Emily and Mike: CameraYaw from 0.4 to
    // 0.8 (turned left) and CameraPitch from -0.9 to -0.1 (turned down), both at once.
    const nudge::Box kEmilyAndMike{0.4, 0.8, -0.9, -0.1};

    nudge::View Telescope(double yaw, double pitch)
    {
        return nudge::View{yaw, pitch, 8.0, 3.0};
    }
}

TEST_CASE("nudge: from the telescope's resting view the framing lies left and down, both said")
{
    const auto l = nudge::Toward(Telescope(0.0, 0.0), kEmilyAndMike);
    CHECK(l.sight.placed);
    CHECK_FALSE(l.sight.onTarget);
    // Toward the middle of the box: 4.8 degrees left and 1.5 down.
    CHECK(l.sight.horizontal == doctest::Approx(-4.8));
    CHECK(l.sight.vertical == doctest::Approx(-1.5));
    CHECK(l.turnRight == -1);
    CHECK(l.turnUp == -1);
}

TEST_CASE("nudge: turned left with the view level, the framing is still below, and the blip says so")
{
    // The review's case: a lead to the box's nearest edge put the framing 0.3 degrees down, too
    // little for a word or an audible pitch, and the game waited.
    const auto l = nudge::Toward(Telescope(0.6, 0.0), kEmilyAndMike);
    CHECK_FALSE(l.sight.onTarget);
    CHECK(l.turnRight == 0);
    CHECK(l.turnUp == -1);
    const auto blip = nudge::BlipOf(l);
    CHECK(blip.pan == doctest::Approx(0.0));
    CHECK(blip.level == doctest::Approx(0.0));
    CHECK_FALSE(blip.locked);
    // A sliver below the box's top edge is still named.
    CHECK(nudge::Toward(Telescope(0.6, -0.05), kEmilyAndMike).turnUp == -1);
}

TEST_CASE("nudge: a push straight left overshoots to the right, and the framing is in once both shares are")
{
    auto l = nudge::Toward(Telescope(1.0, 0.0), kEmilyAndMike);
    CHECK(l.turnRight == 1);
    CHECK(l.turnUp == -1);
    CHECK(nudge::BlipOf(l).pan > 0.0);
    l = nudge::Toward(Telescope(0.6, -0.5), kEmilyAndMike);
    CHECK(l.sight.onTarget);
    CHECK(l.turnRight == 0);
    CHECK(l.turnUp == 0);
    CHECK(nudge::BlipOf(l).locked);
    CHECK(nudge::BlipInterval(l) == doctest::Approx(shoot::kOnTargetInterval));
    // The game's test is inclusive at every edge.
    CHECK(nudge::Inside(Telescope(0.4, -0.1), kEmilyAndMike));
    CHECK(nudge::Inside(Telescope(0.8, -0.9), kEmilyAndMike));
    CHECK_FALSE(nudge::Inside(Telescope(0.39, -0.5), kEmilyAndMike));
}

TEST_CASE("nudge: the blip quickens as the view nears the framing, by the view's own reach")
{
    const auto far = nudge::Toward(Telescope(-0.8, 0.8), kEmilyAndMike);
    const auto near = nudge::Toward(Telescope(0.35, -0.2), kEmilyAndMike);
    CHECK(nudge::BlipInterval(far) == doctest::Approx(0.49));
    CHECK(nudge::BlipInterval(near) < nudge::BlipInterval(far));
    CHECK(nudge::BlipOf(far).pan == doctest::Approx(-1.0));
    CHECK(nudge::BlipOf(far).level == doctest::Approx(0.0));
}

TEST_CASE("nudge: the selfie's good shot lies left of the resting view; its pitch is in already")
{
    // Chapter two's selfie: the nudge reaches 20 degrees across and 37.5 up and down; a good shot
    // is CameraYaw 0.4 to 1 and CameraPitch 0 to 0.4, and the view rests at the pitch's edge.
    const nudge::Box good{0.4, 1.0, 0.0, 0.4};
    const auto l = nudge::Toward(nudge::View{0.0, 0.0, 20.0, 37.5}, good);
    CHECK(l.turnRight == -1);
    CHECK(l.turnUp == 0);
    CHECK(l.sight.horizontal == doctest::Approx(-14.0));
    CHECK_FALSE(l.sight.onTarget);
    // Sagging below the edge is named.
    CHECK(nudge::Toward(nudge::View{0.7, -0.01, 20.0, 37.5}, good).turnUp == 1);
}

TEST_CASE("nudge: a box the graph can never be in is no box")
{
    CHECK(nudge::Valid(kEmilyAndMike));
    CHECK_FALSE(nudge::Valid(nudge::Box{}));
    CHECK_FALSE(nudge::Valid(nudge::Box{0.8, 0.4, -0.9, -0.1}));
    CHECK_FALSE(nudge::Valid(nudge::Box{1.5, 2.0, 0.0, 0.5}));
}

namespace
{
    // The newspaper clipping in the lodge (act2_11, BP_Newspaper_edge_clipping): its examine stage
    // reveals at 0.7 of the turn along the stick's X axis; in session 42 (2026-11-04) Chris picked
    // it up and turned it a little, never to the point, and its words never came.
    nudge::Turn Newspaper(double share)
    {
        return nudge::Turn{share, 0.7, false, false};
    }
}

TEST_CASE("nudge: the newspaper's turn is led to the right, quicker as the reveal nears")
{
    const auto start = nudge::Toward(Newspaper(0.0));
    CHECK(start.right == 1);
    CHECK(start.up == 0);
    CHECK(start.remaining == doctest::Approx(0.7));
    CHECK_FALSE(start.reached);
    const auto blip = nudge::BlipOf(start);
    CHECK(blip.pan == doctest::Approx(1.0));
    CHECK(blip.level == doctest::Approx(0.5));
    CHECK_FALSE(blip.locked);
    CHECK(nudge::BlipInterval(start) == doctest::Approx(0.37));
    const auto half = nudge::Toward(Newspaper(0.35));
    const auto near = nudge::Toward(Newspaper(0.68));
    CHECK(nudge::BlipInterval(half) < nudge::BlipInterval(start));
    CHECK(nudge::BlipInterval(near) < nudge::BlipInterval(half));
    // The side stays plain however near the point is.
    CHECK(nudge::BlipOf(near).pan == doctest::Approx(0.4));
    CHECK(nudge::BlipOf(half).pan == doctest::Approx(0.7));
}

TEST_CASE("nudge: the turn reaches its point where the game's own test does, at the threshold itself")
{
    CHECK_FALSE(nudge::Toward(Newspaper(0.6999)).reached);
    const auto at = nudge::Toward(Newspaper(0.7));
    CHECK(at.reached);
    CHECK(at.remaining == doctest::Approx(0.0));
    CHECK(nudge::BlipOf(at).locked);
    CHECK(nudge::BlipInterval(at) == doctest::Approx(shoot::kOnTargetInterval));
}

TEST_CASE("nudge: the player's inverted setting and a turn along the stick's Y axis change the way to push")
{
    const auto inverted = nudge::Toward(nudge::Turn{0.0, 0.7, false, true});
    CHECK(inverted.right == -1);
    CHECK(nudge::BlipOf(inverted).pan == doctest::Approx(-1.0));
    const auto up = nudge::Toward(nudge::Turn{0.2, 0.95, true, false});
    CHECK(up.right == 0);
    CHECK(up.up == 1);
    CHECK(nudge::BlipOf(up).pan == doctest::Approx(0.0));
    CHECK(nudge::BlipOf(up).level > 0.5);
    const auto down = nudge::Toward(nudge::Turn{0.2, 0.95, true, true});
    CHECK(down.up == -1);
    CHECK(nudge::BlipOf(down).level < 0.5);
}

TEST_CASE("nudge: an item whose own numbers turn it the other way is led the other way: Hannah's locket, 2026-11-08")
{
    // act6_1's locket (BP_Hannahslocket) reveals at 0.95 along the stick's X axis. Its share stood at
    // 0.49 when the stick was pushed right and fell to 0 (16:07:33.98 to 16:07:35.00); pushed left it
    // rose to the reveal (16:07:43.15 to 16:07:44.04): its InputMultiplier and EvaluationMultiplier
    // differ in sign, and the lead had said right all along.
    nudge::Turn locket{0.49, 0.95, false, false};
    locket.reversed = true;
    const auto lead = nudge::Toward(locket);
    CHECK(lead.right == -1);
    CHECK(lead.up == 0);
    CHECK(nudge::BlipOf(lead).pan < 0.0);
    CHECK(nudge::PercentLeft(locket) == 48);
    // The player's own setting turned round as well brings the way back to the right.
    locket.inverted = true;
    CHECK(nudge::Toward(locket).right == 1);
    // Along the Y axis the same: a reversed item is pushed down to rise.
    CHECK(nudge::Toward(nudge::Turn{0.2, 0.95, true, false, true}).up == -1);
}

TEST_CASE("nudge: what is left of a turn is said in per cent of the way, never 0 before the point")
{
    CHECK(nudge::PercentLeft(Newspaper(0.0)) == 100);
    CHECK(nudge::PercentLeft(Newspaper(0.35)) == 50);
    CHECK(nudge::PercentLeft(Newspaper(0.6999)) == 1);
    CHECK(nudge::PercentLeft(Newspaper(0.7)) == 0);
    // The headless doll (act2_3) starts half turned and reveals at 0.85.
    CHECK(nudge::PercentLeft(nudge::Turn{0.5, 0.85, false, false}) == 41);
}

TEST_CASE("nudge: a turn is led while the item is in hand, and the reveal is told once")
{
    nudge::TurnCourse course;
    nudge::TurnState s;
    s.usesReveal = true;
    s.graphReadsReveal = true;
    // Picked up: the grab plays before the item is fully held, and the stick counts for nothing.
    CHECK(course.Update(s) == nudge::TurnCourse::Event::None);
    s.fullyHeld = true;
    CHECK(course.Update(s) == nudge::TurnCourse::Event::Begins);
    CHECK(course.Led());
    CHECK(course.Update(s) == nudge::TurnCourse::Event::None);
    s.revealing = true;
    CHECK(course.Update(s) == nudge::TurnCourse::Event::Reveals);
    CHECK_FALSE(course.Led());
    CHECK(course.Revealed());
    // Chris's note turned back to its front: the player's own look, nothing more is led.
    s.revealing = false;
    CHECK(course.Update(s) == nudge::TurnCourse::Event::None);
    CHECK_FALSE(course.Led());
    s.revealing = true;
    CHECK(course.Update(s) == nudge::TurnCourse::Event::None);
    // The next use of it is led again: its words come only at the reveal.
    course.Reset();
    s.revealing = false;
    CHECK(course.Update(s) == nudge::TurnCourse::Event::Begins);
}

TEST_CASE("nudge: a turn stops being led when the item leaves the hand or a ramp of the game's takes it")
{
    nudge::TurnCourse course;
    nudge::TurnState s{true, true, true, false, false};
    CHECK(course.Update(s) == nudge::TurnCourse::Event::Begins);
    s.ramping = true;
    CHECK(course.Update(s) == nudge::TurnCourse::Event::Ends);
    s.ramping = false;
    CHECK(course.Update(s) == nudge::TurnCourse::Event::Begins);
    s.fullyHeld = false;
    CHECK(course.Update(s) == nudge::TurnCourse::Event::Ends);
    // A reveal seen while the item is out of the hand is not told: the stick did not make it.
    s.revealing = true;
    CHECK(course.Update(s) == nudge::TurnCourse::Event::None);
}

TEST_CASE("nudge: a turn whose reveal the item's graph never reads has no end, and nothing is led")
{
    // The cigar stubs (Act2_2, act2_3): the stage reveals at 0.95, but the template never looks at
    // bIsRevealing; the clue came with the pick-up.
    nudge::TurnCourse course;
    nudge::TurnState s{true, false, true, false, false};
    CHECK(course.Update(s) == nudge::TurnCourse::Event::None);
    s.revealing = true;
    CHECK(course.Update(s) == nudge::TurnCourse::Event::None);
    CHECK_FALSE(course.Led());
    // Nor a stage without a reveal at all.
    nudge::TurnCourse plain;
    CHECK(plain.Update(nudge::TurnState{false, true, true, false, false}) == nudge::TurnCourse::Event::None);
}

TEST_CASE("nudge: a timed light is told the poll it comes on, and again only after it went out")
{
    nudge::Light light;
    CHECK_FALSE(light.Update(false));
    CHECK(light.Update(true));
    CHECK_FALSE(light.Update(true));
    CHECK(light.On());
    CHECK_FALSE(light.Update(false));
    CHECK(light.Update(true));
    light.Reset();
    CHECK(light.Update(true));
}

TEST_CASE("nudge: the premonition is placed against the game's own line, which the stick moves, the danger totem, 2026-11-07")
{
    // act4_2's danger totem (research/sessions/s51-full.log, 17:14:04 to 17:14:24): placed against the
    // view's centre line, the point lay "+7 cm across, -11 up" all the while the game's line missed
    // the totem; the sound said right, the pose went to its right limit and nothing moved. The game
    // looks along its own line, from the eye through the query point the pose moves
    // (CachedTotemInspectionQueryLocation): a point 7 cm right of the centre line lies 2 cm left of a
    // line moved 9 cm right of it at the totem's distance.
    using features::world::Vec;
    const Vec eye{0.0, 0.0, 0.0};
    const Vec ahead{1.0, 0.0, 0.0};
    const Vec premonition{100.0, 7.0, -11.0};
    // The view's centre line straight ahead: 7 right, 11 down.
    Vec d = nudge::AcrossLine(premonition, eye, Vec{100.0, 0.0, 0.0}, ahead);
    CHECK(d.x == doctest::Approx(0.0));
    CHECK(d.y == doctest::Approx(7.0));
    CHECK(d.z == doctest::Approx(-11.0));
    // The game's line through a query point 9 cm right and 8 cm down at the totem: 2 left, 3 down.
    d = nudge::AcrossLine(premonition, eye, Vec{100.0, 9.0, -8.0}, ahead);
    CHECK(d.y == doctest::Approx(-2.0).epsilon(0.05));
    CHECK(d.z == doctest::Approx(-3.0).epsilon(0.05));
    // A line of no length is taken along the fallback.
    d = nudge::AcrossLine(premonition, eye, eye, ahead);
    CHECK(d.y == doctest::Approx(7.0));
}

TEST_CASE("nudge: the totem's lead goes the way a push brings the premonition nearer, the danger totem, 2026-11-07")
{
    // 17:14:21 to 17:14:22: the game's line on the totem, the premonition 8 cm left and 2 cm down of
    // the point it met; the stick pushed left moved the pose from +0.21 to -0.20 and the point came
    // to 3 cm left, and the vision came.
    nudge::TotemLead lead;
    nudge::TotemSample s;
    s.turnKnown = true;
    s.onTotem = true;
    s.closeness = 0.19;
    s.turnX = 0.21;
    s.turnY = -0.75;
    s.lateral = -8.0;
    s.vertical = -2.0;
    auto aim = lead.Update(s);
    // A push toward its side: left and down.
    CHECK(aim.pan == doctest::Approx(-std::sqrt(8.0 / 15.0)));
    CHECK(aim.level < 0.5);
    s.turnX = -0.20;
    s.closeness = 0.76;
    s.lateral = -3.0;
    aim = lead.Update(s);
    CHECK(lead.WayX() == 1);
    CHECK(aim.pan == doctest::Approx(-std::sqrt(3.0 / 15.0)));
    // A pose moved by less than a step weighs nothing, however the point jumps.
    s.turnX = -0.18;
    s.lateral = 9.0;
    lead.Update(s);
    CHECK(lead.WayX() == 1);
}

TEST_CASE("nudge: a push that takes the premonition further off turns the totem's lead round, and a new totem starts over")
{
    // The totem leans in the hands against the line's move where its controls are inverted: a push
    // right may take the point further right. Once the pose has moved enough to tell, the lead goes
    // the other way.
    nudge::TotemLead lead;
    nudge::TotemSample s;
    s.turnKnown = true;
    s.onTotem = true;
    s.closeness = -0.40; // on the totem, far from the point
    s.lateral = 4.0;
    s.vertical = 3.0;
    auto aim = lead.Update(s);
    CHECK(aim.pan > 0.0);
    CHECK(aim.level > 0.5);
    // Pushed right a tenth at a time, the point 1.5 cm further right each time.
    for (int step = 1; step <= 3; ++step)
    {
        s.turnX = 0.1 * step;
        s.lateral = 4.0 + 1.5 * step;
        lead.Update(s);
    }
    CHECK(lead.WayX() == 1); // not moved enough to tell
    for (int step = 4; step <= 6; ++step)
    {
        s.turnX = 0.1 * step;
        s.lateral = 4.0 + 1.5 * step;
        aim = lead.Update(s);
    }
    CHECK(lead.WayX() == -1);
    CHECK(aim.pan == doctest::Approx(-std::min(1.0, std::sqrt(s.lateral / 15.0))));
    // Up and down never moved: the way stays.
    CHECK(lead.WayY() == 1);
    CHECK(aim.level > 0.5);
    // The line leaving the totem: what the point is measured from changes, and that step is not
    // weighed, though the point's place jumps the other way as the pose goes back.
    const double xx = s.turnX;
    s.onTotem = false;
    s.closeness = 0.0;
    s.turnX = xx - 0.3;
    s.lateral = 53.0;
    lead.Update(s);
    CHECK(lead.WayX() == -1);
    // A totem taken up again starts over, a push toward the point's side.
    lead.Reset();
    aim = lead.Update(s);
    CHECK(lead.WayX() == 1);
    CHECK(aim.pan == doctest::Approx(1.0));
}

TEST_CASE("nudge: the totem's lead is clear a few centimetres from the point, where the game wants it within one or two, 2026-11-07")
{
    // research/sessions/s55-full.log, 23:05:02 to 23:05:05: the point 3 cm across and 1 up, then 1 to 3
    // cm across and 4 down, the pose held at its top limit while the stick pushed up; the blip panned
    // 0.17 to 0.21 and pitched 0.41, near the middle.
    nudge::TotemLead lead;
    nudge::TotemSample s;
    s.turnKnown = true;
    s.onTotem = true;
    s.closeness = 0.61;
    s.turnX = 0.08;
    s.turnY = 0.42;
    s.lateral = 3.0;
    s.vertical = 1.0;
    auto aim = lead.Update(s);
    CHECK(aim.pan > 0.4);
    s.closeness = 0.50;
    s.turnY = 0.75;
    s.vertical = -4.0;
    aim = lead.Update(s);
    CHECK(aim.level < 0.3); // clearly low: push down
    // Far off it stays full, and on the point it is the middle.
    s.lateral = -20.0;
    s.vertical = 0.0;
    aim = lead.Update(s);
    CHECK(aim.pan == doctest::Approx(-1.0));
    s.lateral = 0.0;
    aim = lead.Update(s);
    CHECK(aim.pan == doctest::Approx(0.0));
    CHECK(aim.level == doctest::Approx(0.5));
}
