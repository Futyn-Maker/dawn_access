#pragma once
// When the ground the mod has felt out stops being the ground she walks on. The ground keeps a step
// found free for as long as the world holds (features/WorldStream): what the game changes between
// two holds opens ways far more often than it closes them, and asking every step anew had every map
// sweep the whole ground again. But the game also closes ways under the player's feet, with no hold
// between: in the lodge's cinema on 2026-11-08 Ashley walked into the volume that slams the cinema's
// exit door, the door shut the doorway to the hall, and every map after it took her through the
// doorway on the steps found free before; the vase, the hall's photo and the hall itself stayed
// listed, and every walk to them stopped against the door (research/sessions/s57-full.log, 02:03:13
// to 02:05:27; the steps re-swept after the stall met something of the persistent level, the
// slammed door, where the free ones of the doorway were never asked again).
//
// Two things say the ground has changed. A walk that gets nowhere: the steps around her and under
// the way ahead are asked again, and any found shut that was free means the ground has changed
// there (ColumnsAhead, StepsIn). And a prop the game moves: the bodies of a door a sequence slams
// stand where they did not, and the steps about it, before and after, are asked again (PropWatch,
// ColumnsInBox). What was found changed is forgotten for a stretch around it (ColumnsAround), and
// the list is judged afresh by a map begun since.

#include "features/Vec.hpp"

#include <optional>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

namespace da::recheck
{
    using Vec = features::world::Vec;
    using Column = std::pair<int, int>;
    using StepKey = std::tuple<int, int, int, int, int>; // from x, y, level, to x, y

    // The grid's columns, as Exploration lays them: a cell `cell` cm wide, its corner at `origin`.
    struct Grid
    {
        Vec origin;
        double cell = 20.0;
        Column ColumnOf(const Vec& at) const;
    };

    // The columns a walk that gets nowhere asks again: those within `radius` cells of her own and
    // of the way ahead (`ahead`: the corners she walks to, in order), up to `reach` cm along it.
    std::set<Column> ColumnsAhead(const Grid& grid, const Vec& here, const std::vector<Vec>& ahead, double reach, int radius);

    // The columns within `radius` cells of any of `cells`.
    std::set<Column> ColumnsAround(const std::vector<Column>& cells, int radius);

    // The columns under a box grown by `margin` cm on every side.
    std::set<Column> ColumnsInBox(const Grid& grid, const Vec& low, const Vec& high, double margin);

    // The steps of `steps` (keyed as the ground keys them) that start or land in one of `columns`.
    template <class Steps>
    std::vector<StepKey> StepsIn(const Steps& steps, const std::set<Column>& columns)
    {
        std::vector<StepKey> out;
        for (const auto& [key, value] : steps)
        {
            const auto& [ax, ay, alevel, bx, by] = key;
            if (columns.count({ax, ay}) || columns.count({bx, by})) out.push_back(key);
        }
        return out;
    }

    // A box in the world, low and high corners.
    struct Box
    {
        Vec low, high;
    };

    // Whether two boxes differ by more than `tolerance` cm at any corner.
    bool Moved(const Box& a, const Box& b, double tolerance);

    // The smallest box holding both.
    Box Union(const Box& a, const Box& b);

    constexpr double kPropTolerance = 3.0; // cm a prop's box may shift without moving (animation's breath)
    constexpr double kPropRest = 1.0;      // seconds a prop that moved must stand still before its ground is asked

    // A prop's box (its colliding components, as the engine bounds them) watched as the game moves
    // it: Update gives each reading, or none where the prop has no collision now; it says once, when
    // a prop that moved (or took up or put down its collision) has stood still for kPropRest, the box
    // it stood in before and the one it stands in now. A prop seen for the first time has not moved;
    // one that never comes to rest is never said.
    class PropWatch
    {
    public:
        bool Update(double now, const std::optional<Box>& box, Box& before, Box& after);

    private:
        bool m_seen = false;
        std::optional<Box> m_rest;   // where it stood at rest, as the ground knows it
        std::optional<Box> m_last;   // the last reading
        bool m_moving = false;
        double m_stillSince = 0.0;
    };
}
