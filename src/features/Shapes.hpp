#pragma once
// A trigger volume's collision as the game tests the character against it, worked out without
// the game so that the tests can measure it. The game counts the character in a volume while her
// capsule overlaps the volume's body (UBatesCharacterTriggerComponent::FindOverlappingTriggers
// asks her capsule's overlaps for ABatesCharacterTriggerVolume actors), and the body's collision
// is its mesh's own: convex pieces, boxes, balls and capsules (UBodySetup::AggGeom), placed with
// the body. A mesh can carry its shape in its points: act2_1's way on to Mike's choice
// (SM_T_lovetriangle_mike_choice) is one convex piece, a slab 2.25 m thick, 15 m long and 10 m
// high set aslant in the mesh itself, and the box around it (9 by 14 m) reached the spot 4.5 m
// short of it where Matt circled and gave up (2026-11-02).

#include "features/Vec.hpp"

#include <vector>

namespace da::shapes
{
    using features::world::Vec;

    // A turn, as the engine keeps one (FQuat).
    struct Quat
    {
        double x = 0.0, y = 0.0, z = 0.0, w = 1.0;
    };

    // The engine's FRotator, in degrees: pitch about the right axis, yaw about the up axis, roll
    // about the forward axis.
    Quat FromRotator(double pitch, double yaw, double roll);
    Vec Turn(const Quat& q, const Vec& v);

    // A placement as the engine composes one (FTransform): scaled, then turned, then moved.
    struct Pose
    {
        Quat rotation;
        Vec translation;
        Vec scale{1.0, 1.0, 1.0};
    };
    Vec Place(const Pose& pose, const Vec& local);

    // The pieces of a volume's collision, in the world.
    struct Hull // a convex piece: the convex hull of its points
    {
        std::vector<Vec> points;
    };
    struct Ball
    {
        Vec middle;
        double radius = 0.0;
    };
    struct Pill // a capsule: the points within its radius of the segment ab
    {
        Vec a, b;
        double radius = 0.0;
    };
    struct Shape
    {
        std::vector<Hull> hulls;
        std::vector<Ball> balls;
        std::vector<Pill> pills;

        bool Empty() const;
        // The box square with the world that holds every piece.
        void Bounds(Vec& low, Vec& high) const;
    };

    // A box with its middle, its half sizes along its own axes and its turn, as a hull.
    Hull BoxHull(const Vec& middle, const Vec& half, const Quat& turn);

    // The distance between the segment ab and a piece or the whole shape, zero where they meet
    // (the hull's by the Gilbert-Johnson-Keerthi distance over the hull's points).
    double Distance(const Hull& hull, const Vec& a, const Vec& b);
    double Distance(const Shape& shape, const Vec& a, const Vec& b);
    // The same for a convex piece, with `gap` set to the way from the segment's nearest point to the
    // piece's, as long as the distance (none where they meet).
    double HullGap(const Hull& hull, const Vec& a, const Vec& b, Vec& gap);

    // Where the shape lies nearest the segment ab: how far, and the way there from the segment's
    // nearest point, as long as the distance (none where they meet). A way on's volume is set off
    // where her capsule first touches it, not at its middle: the cinema's trigger at the lodge, a
    // room 11 m across, was said 8 m off, behind and to the left, as Sam stood 1.5 m short of its near
    // side and walked into it three seconds later (research/sessions/s56-full.log, 23:50:45).
    struct Gap
    {
        double distance = 0.0;
        Vec toward;
    };
    Gap Nearest(const Shape& shape, const Vec& a, const Vec& b);

    // Whether an upright capsule with its middle at `middle` touches the shape: the segment
    // between the middles of its end spheres comes within its radius of a piece.
    bool CapsuleTouches(const Shape& shape, const Vec& middle, double radius, double halfHeight);

    // Whether that capsule, swept from one middle to another, runs into the shape: touches it at half
    // way or where it ends, and nearer than where it starts. One that starts against the shape may
    // step away from it, as the engine's sweep lets a capsule that starts in penetration move out.
    bool SweepTouches(const Shape& shape, const Vec& from, const Vec& to, double radius, double halfHeight);
}
