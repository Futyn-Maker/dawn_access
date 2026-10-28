#pragma once
// Which of the ways on that a scene offers the exploration list keeps, worked out without the
// game so that it can be replayed over recorded ground in the tests: the road the walk to a
// target takes over the reach map, pulled straight; the row rule, by which of a row of ways
// along one road only the farthest is listed; and when a fresh reach map has settled the list,
// so that no way is listed, or announced, only to be left out a moment later.

#include "features/GridPath.hpp"
#include "features/Vec.hpp"

#include <cstddef>
#include <functional>
#include <vector>

namespace da::ways
{
    using features::world::Vec;

    // ---- the road over the reach map ---------------------------------------------------------

    // Pulls a path the reach map found straight wherever the map reached the ground all along the
    // straight line between two of its cells: the columns between them, one per cell along the
    // longer side, each reached on a level within one of the last. Those are the corners the
    // game's own straightener keeps where it sweeps the character between the cells, found
    // without a sweep, and the same whichever of two equal ways the map took: its own cells
    // zigzag between such ways, and whether that zigzag brushed a way beside it decided whether
    // the way was listed (the woods' start on 2026-10-28). `maxRun` bounds a straight run, in
    // cells along the path, as Straightener's does.
    std::vector<grid::Cell> StraightOverMap(const grid::Flood& flood, const std::vector<grid::Cell>& path, size_t maxRun);

    // ---- the row rule ------------------------------------------------------------------------

    // A target as the row rule weighs it.
    struct Entry
    {
        bool way = false;         // a way on, a volume the level waits for the character in; else a thing to use
        bool hasBox = false;      // the way's volume is known
        double distance = 0.0;    // as it is said: on foot where the road is known
        bool behind = false;      // behind the character on the path she is held to
        bool unreachable = false; // a way no walk gets to for now
        bool unjudged = false;    // a way whose ground is not known yet
        bool reached = false;     // the character is there
        bool chosen = false;      // the player chose it: listed whatever the rules say
        bool closedOff = false;   // the ground was proven closed off from it, no door between: its road ends short of it
        bool known = true;        // its road is known now: the navigation mesh, the map, the path she is held to
        bool earlier = false;     // an earlier map found it reachable, or beyond a door, and this one has not judged it yet
        // The list's verdict, kept from look to look: whether the row rule has judged it since it
        // came, and the index of the target whose road it lies on, or -1.
        bool passed = false;
        int onRoadOf = -1;
    };

    // Whether the road the walk to target `other` takes runs through the volume of way `way`
    // (indices into the entries).
    using Enters = std::function<bool(size_t way, size_t other)>;

    // The row rule: a way on the road the walk to another listed target takes is left out, since
    // that walk sets it off anyway. Ways are judged from the farthest in, each against the roads of
    // every thing to use, of the chosen target, and of the farther ways found to stay listed, so
    // that two ways never leave each other out, nor a road that is itself left out leaves a way
    // out. A way behind, unreachable, unjudged, reached or chosen is not judged; a road counts only
    // from a target listed and not closed off (the road to a closed-off thing ends short of it, a
    // dead end no walk gets through). Returns, for each entry, the index of the target whose road
    // it lies on, or -1.
    std::vector<int> RowRule(const std::vector<Entry>& entries, const Enters& enters);

    // A way's volume as a box turned about the upright: its middle, its sides along its own axes,
    // its yaw in degrees, and the box round it on the world's axes (the actor's bounds).
    struct Box
    {
        Vec middle;
        Vec size;
        double yaw = 0.0;
        Vec low;
        Vec high;
    };
    Box TurnedBox(const Vec& middle, const Vec& size, double yaw);

    // Whether a road walked at the character's feet, sampled every `step` cm, comes within
    // `radius` of a box at a height the character's capsule spans (`halfHeight` is half of it):
    // the walk there sets the volume off. Exploration asks the engine for the distance to the
    // volume itself (GetClosestPointOnCollision), which comes out the same for a box.
    bool RoadEntersBox(const Box& box, const std::vector<Vec>& road, double radius, double halfHeight, double step = 25.0);

    // ---- settling ----------------------------------------------------------------------------

    // When a fresh reach map has settled which ways the list keeps. The map reaches the near
    // ground first, and the road to a farther target may yet run through a nearer way, so the ways
    // it judges wait until it has judged every target, has run out of cells or of budget, or has
    // run kSettleCap seconds, whichever comes first. The first map of a hold lists no way
    // meanwhile; a map begun while an earlier one's list stands (the character walked on, the
    // story moved on, a short exchange ended) leaves that list as it was until it settles, so
    // that no way goes and comes back.
    constexpr double kSettleCap = 3.0;

    class Settling
    {
    public:
        // A hold begins afresh: no list stands, and none settles before a map has begun.
        void Reset();
        // A map has begun.
        void Begin(double now);
        // No map judges the ways (the navigation mesh gives the roads): the list stands as it is.
        void Clear();
        // Takes the map's state at a look: whether it has run out (`done`), and how many of the
        // targets it has judged. True at the look the list settles, when the row rule must judge
        // it before anything is listed or said.
        bool Update(double now, bool done, size_t judged, size_t targets);
        bool Settled() const
        {
            return m_settled;
        }
        // Settling while an earlier list stands: the targets judged then keep their verdicts.
        bool Keeping() const
        {
            return !m_settled && m_keep;
        }
        // The map began while an earlier list stood: once it has settled, a way it has not judged
        // yet keeps what the earlier map found until it does (a way the list has said does not go
        // for a while because the map begun as she walked on has not reached it yet).
        bool Kept() const
        {
            return m_kept;
        }
        double StartedAt() const
        {
            return m_startedAt;
        }

    private:
        bool m_settled = false;
        bool m_keep = false;
        bool m_once = false;  // a list has settled since the hold began
        bool m_begun = false; // a map has begun since
        bool m_kept = false;  // the map began while an earlier list stood
        double m_startedAt = 0.0;
    };

    // The list at every look, as Exploration's ChooseListed takes it. A way's `unjudged` follows the
    // settling: no way is judged before the list settles, and none whose road is not known after
    // (but what an earlier map found, under a map begun while its list stood), while a target the
    // rule judged before keeps its verdict as a later map settles. The row rule
    // judges the list at the look it settles, before anything is listed or said (the woods' start
    // on 2026-10-28 listed four ways at the look it settled and left three out at the next), then
    // once a second, and at the first look after Again().
    class Lister
    {
    public:
        // Whether the row rule judges the list at this look.
        bool Due(const Settling& settle, bool settlesNow, double now) const;
        // Each entry's `unjudged` at this look, and, when `due`, the row rule over the entries:
        // `onRoadOf` and `passed` for every one. Without `roads` (neither a map nor the navigation
        // mesh judges them: a hand-over before its first map, a search that laid the grid afresh)
        // a list that stands stays as it is.
        void Judge(std::vector<Entry>& entries, const Settling& settle, bool due, bool roads, double now, const Enters& enters);
        // The next look judges the list: a target was judged, came or went.
        void Again()
        {
            m_judgedAt = -10.0;
        }

    private:
        double m_judgedAt = -10.0;
    };
}
