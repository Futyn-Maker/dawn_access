// features/Shapes: a trigger volume's collision as the game tests the character against it, the
// distance between a segment and a piece measured against brute force, and the slab of act2_1's
// way on to Mike's choice as the game's asset gives it.
#include "Support.hpp"
#include "features/Shapes.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

using namespace da;
using shapes::Vec;

namespace
{
    shapes::Quat Inverse(const shapes::Quat& q)
    {
        return {-q.x, -q.y, -q.z, q.w};
    }

    // The distance from a point to a turned box, in the box's own frame.
    double PointToBox(const Vec& p, const Vec& middle, const Vec& half, const shapes::Quat& turn)
    {
        const Vec local = shapes::Turn(Inverse(turn), Vec{p.x - middle.x, p.y - middle.y, p.z - middle.z});
        const double dx = std::max(std::fabs(local.x) - half.x, 0.0);
        const double dy = std::max(std::fabs(local.y) - half.y, 0.0);
        const double dz = std::max(std::fabs(local.z) - half.z, 0.0);
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // The least of the distances from points along the segment, 4001 of them.
    double SampledToBox(const Vec& a, const Vec& b, const Vec& middle, const Vec& half, const shapes::Quat& turn)
    {
        double best = 1e300;
        for (int i = 0; i <= 4000; ++i)
        {
            const double t = i / 4000.0;
            best = std::min(best, PointToBox(Vec{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}, middle, half, turn));
        }
        return best;
    }

    bool Near(const Vec& a, const Vec& b, double tolerance)
    {
        return std::fabs(a.x - b.x) <= tolerance && std::fabs(a.y - b.y) <= tolerance && std::fabs(a.z - b.z) <= tolerance;
    }

    // SM_T_lovetriangle_mike_choice's one convex piece (its BodySetup's AggGeom), placed where
    // LV_Scene02_love_triangle puts the volume's mesh component (BatesCharacterTriggerVolume_0,
    // its TriggerVolume component at (-4381.89, -19627.33, 1428.38), unturned and unscaled).
    shapes::Shape MikeChoiceSlab()
    {
        const Vec at{-4381.890538116746, -19627.325593564325, 1428.3767401785706};
        const Vec local[] = {{-452.9, 596.1, 500.0},  {-255.5, 703.8, 500.0},  {255.5, -703.8, 500.0},  {452.9, -596.1, 500.0},
                             {255.5, -703.8, -500.0}, {452.9, -596.1, -500.0}, {-452.9, 596.1, -500.0}, {-255.5, 703.8, -500.0}};
        shapes::Pose pose;
        pose.translation = at;
        shapes::Shape shape;
        shapes::Hull hull;
        for (const Vec& p : local)
            hull.points.push_back(shapes::Place(pose, p));
        shape.hulls.push_back(hull);
        return shape;
    }
}

TEST_CASE("shapes: the engine's turns, as FRotator and FQuat make them")
{
    // Yaw turns forward toward the right, pitch lifts forward up, roll lowers the right side.
    CHECK(Near(shapes::Turn(shapes::FromRotator(0.0, 90.0, 0.0), {1.0, 0.0, 0.0}), {0.0, 1.0, 0.0}, 1e-9));
    CHECK(Near(shapes::Turn(shapes::FromRotator(90.0, 0.0, 0.0), {1.0, 0.0, 0.0}), {0.0, 0.0, 1.0}, 1e-9));
    CHECK(Near(shapes::Turn(shapes::FromRotator(0.0, 0.0, 90.0), {0.0, 1.0, 0.0}), {0.0, 0.0, -1.0}, 1e-9));
    // A placement scales, then turns, then moves.
    shapes::Pose pose;
    pose.rotation = shapes::FromRotator(0.0, 90.0, 0.0);
    pose.translation = {100.0, 0.0, 0.0};
    pose.scale = {2.0, 1.0, 1.0};
    CHECK(Near(shapes::Place(pose, {10.0, 0.0, 0.0}), {100.0, 20.0, 0.0}, 1e-9));
}

TEST_CASE("shapes: a segment's distance to a turned box is the least over the segment")
{
    std::mt19937 random(20261102);
    std::uniform_real_distribution<double> spread(-900.0, 900.0);
    std::uniform_real_distribution<double> size(5.0, 600.0);
    std::uniform_real_distribution<double> angle(-180.0, 180.0);
    int inside = 0;
    for (int i = 0; i < 400; ++i)
    {
        CAPTURE(i);
        const Vec middle{-6000.0 + spread(random), -19000.0 + spread(random), 1300.0 + spread(random) / 3.0};
        const Vec half{size(random), size(random), size(random)};
        const auto turn = shapes::FromRotator(angle(random) / 4.0, angle(random), angle(random) / 4.0);
        shapes::Shape shape;
        shape.hulls.push_back(shapes::BoxHull(middle, half, turn));
        const Vec a{middle.x + spread(random), middle.y + spread(random), middle.z + spread(random) / 2.0};
        // A point, an upright segment like a capsule's, and a segment at any angle.
        const Vec ends[] = {a, Vec{a.x, a.y, a.z + 116.0}, Vec{a.x + spread(random), a.y + spread(random), a.z + spread(random) / 2.0}};
        for (const Vec& b : ends)
        {
            const double measured = shapes::Distance(shape, a, b);
            const double sampled = SampledToBox(a, b, middle, half, turn);
            if (sampled == 0.0) ++inside;
            // Never further than a point of the segment is, and short of the sampled least by no
            // more than the gap between two samples.
            CHECK(measured <= sampled + 1e-6);
            CHECK(measured >= sampled - 0.6);
        }
    }
    // The cases run through boxes as well as past them.
    CHECK(inside > 20);
}

TEST_CASE("shapes: balls, capsules and a hull that is no box")
{
    shapes::Shape ball;
    ball.balls.push_back({{0.0, 0.0, 0.0}, 50.0});
    CHECK(shapes::Distance(ball, {100.0, 0.0, -10.0}, {100.0, 0.0, 10.0}) == doctest::Approx(50.0));
    shapes::Shape pill;
    pill.pills.push_back({{0.0, 0.0, 0.0}, {0.0, 0.0, 200.0}, 30.0});
    CHECK(shapes::Distance(pill, {100.0, 0.0, 50.0}, {100.0, 0.0, 150.0}) == doctest::Approx(70.0));
    // A pyramid of a square base 200 cm wide and 100 cm high: straight above its tip, and beside
    // its base.
    shapes::Shape pyramid;
    pyramid.hulls.push_back({{{-100.0, -100.0, 0.0}, {100.0, -100.0, 0.0}, {100.0, 100.0, 0.0}, {-100.0, 100.0, 0.0}, {0.0, 0.0, 100.0}}});
    CHECK(shapes::Distance(pyramid, {0.0, 0.0, 160.0}, {0.0, 0.0, 300.0}) == doctest::Approx(60.0));
    CHECK(shapes::Distance(pyramid, {130.0, 0.0, -20.0}, {130.0, 0.0, 0.0}) == doctest::Approx(30.0));
    // Through its middle: inside.
    CHECK(shapes::Distance(pyramid, {0.0, 0.0, -50.0}, {0.0, 0.0, 50.0}) == 0.0);
    // Its slanted face: a point straight out from the face's middle, 40 cm off.
    const double slope = std::sqrt(2.0) / 2.0;
    const Vec face{50.0, 0.0, 50.0};
    CHECK(shapes::Distance(pyramid, {face.x + 40.0 * slope, 0.0, face.z + 40.0 * slope}, {face.x + 40.0 * slope, 0.0, face.z + 40.0 * slope}) ==
          doctest::Approx(40.0));
}

TEST_CASE("shapes: the way on to Mike's choice is a slab set aslant in its mesh, 2026-11-02")
{
    // act2_1: after the gate, Matt walked to the way on and circled 4.5 m short of the slab, at
    // the edge of the box around it (s38: around (-4850, -19955), his middle at height 1295.5),
    // until the walk gave up.
    const auto slab = MikeChoiceSlab();
    constexpr double radius = 42.0;
    constexpr double halfHeight = 100.0;
    // The box around the slab holds the spot where he circled, with his capsule's reach.
    Vec low, high;
    slab.Bounds(low, high);
    CHECK(low.x == doctest::Approx(-4834.8).epsilon(0.001));
    CHECK(high.y == doctest::Approx(-18923.5).epsilon(0.001));
    CHECK(-4850.0 + radius > low.x);
    // The slab itself is 4.5 m off.
    CHECK_FALSE(shapes::CapsuleTouches(slab, {-4850.0, -19955.0, 1295.5}, radius, halfHeight));
    CHECK(shapes::Distance(slab, {-4850.0, -19955.0, 1253.5}, {-4850.0, -19955.0, 1337.5}) == doctest::Approx(455.4).epsilon(0.01));
    // Its middle line runs from (-4736, -18977) to (-4028, -20277), 2.25 m thick: a capsule
    // standing on it touches it, one a metre and a half off its side does not.
    CHECK(shapes::CapsuleTouches(slab, {-4382.0, -19627.0, 1295.5}, radius, halfHeight));
    CHECK(shapes::CapsuleTouches(slab, {-4382.0 - 0.878 * 150.0, -19627.0 - 0.478 * 150.0, 1295.5}, radius, halfHeight));
    CHECK_FALSE(shapes::CapsuleTouches(slab, {-4382.0 - 0.878 * 200.0, -19627.0 - 0.478 * 200.0, 1295.5}, radius, halfHeight));
    // Under its bottom (928) and over its top (1928) a capsule misses it.
    CHECK_FALSE(shapes::CapsuleTouches(slab, {-4382.0, -19627.0, 800.0}, radius, halfHeight));
    CHECK_FALSE(shapes::CapsuleTouches(slab, {-4382.0, -19627.0, 2050.0}, radius, halfHeight));
    CHECK(shapes::CapsuleTouches(slab, {-4382.0, -19627.0, 1950.0}, radius, halfHeight));
}

TEST_CASE("the lodge's garage door, shut, is a wall to the steps through its doorway and none to its own place")
{
    // The door between the garage's vestibule and the lodge's front vestibule, 2026-11-04
    // (research/sessions/s41.log): its leaf, the prop BP_Lodge_interior_door_C_0 of
    // LV_Scene05_into_the_lodge_inside, stands at (606.16, -8272.17, 3005.53) turned 169.65 degrees,
    // and its one physics body (PHYS_hangingshed_interior_door) is a box 127.7 by 7.5 by 235.7 cm,
    // its middle at (62.5, 0, 112.35) on the hinge's bone, taken here at the prop's own place. The
    // map swept the doorway's steps as free (the prop asleep, its collision off) while Chris, once
    // there, stood at the shut door at (567, -8218) with the stick pushed.
    shapes::Pose hinge;
    hinge.translation = Vec{606.1617, -8272.17255, 3005.53322};
    hinge.rotation = shapes::FromRotator(0.0, 169.653537, 0.0);
    shapes::Shape leaf;
    auto box =
        shapes::BoxHull(Vec{62.50117974777319, 0.0, 112.35061080204551}, Vec{127.70365142822266 / 2.0, 7.5 / 2.0, 235.70169067382812 / 2.0}, shapes::Quat{});
    for (auto& p : box.points)
        p = shapes::Place(hinge, p);
    leaf.hulls.push_back(box);
    // The grid of that session: laid at (-1291.41, -9660.52), 20 cm cells; the capsule of radius
    // 42 swept a step above the floor (3006 at the door) with its half height of 77.5.
    const auto middle = [](int x, int y) { return Vec{-1291.41 + (x + 0.5) * 20.0, -9660.52 + (y + 0.5) * 20.0, 3006.0 + 100.0 + 22.5}; };
    const double radius = 42.0, half = 77.5;
    // Through the doorway, as the map's road to the hall went: a wall.
    CHECK(shapes::SweepTouches(leaf, middle(91, 70), middle(91, 69), radius, half));
    CHECK(shapes::SweepTouches(leaf, middle(92, 70), middle(92, 69), radius, half));
    // Up to the shut door: her capsule at row 71 would stand in the leaf, as she could not.
    CHECK(shapes::SweepTouches(leaf, middle(91, 72), middle(91, 71), radius, half));
    // The ground before it, where the game offers the door (its place at (536, -8140)): free, and so
    // is a step back from the leaf.
    CHECK_FALSE(shapes::SweepTouches(leaf, middle(91, 73), middle(91, 72), radius, half));
    CHECK_FALSE(shapes::SweepTouches(leaf, middle(91, 75), middle(92, 75), radius, half));
    CHECK_FALSE(shapes::SweepTouches(leaf, middle(91, 71), middle(91, 72), radius, half));
    // Opened (swung a quarter turn on its hinge), it stands out of the doorway.
    shapes::Pose open = hinge;
    open.rotation = shapes::FromRotator(0.0, 169.653537 + 90.0, 0.0);
    shapes::Shape swung;
    auto openBox =
        shapes::BoxHull(Vec{62.50117974777319, 0.0, 112.35061080204551}, Vec{127.70365142822266 / 2.0, 7.5 / 2.0, 235.70169067382812 / 2.0}, shapes::Quat{});
    for (auto& p : openBox.points)
        p = shapes::Place(open, p);
    swung.hulls.push_back(openBox);
    CHECK_FALSE(shapes::SweepTouches(swung, middle(90, 70), middle(90, 69), radius, half));
}
