#pragma once
// A way through a grid of cells found step by step: the search over the cells needs no game,
// only an answer to whether one cell can be stepped into from its neighbour, which the game
// gives by sweeping the character's own collision between the two. A cell is a place on the
// ground at one level: a staircase climbs from level to level, and a bridge and the path
// under it are two cells at one place. The search is resumable, so that a frame spends only
// its share of time on it.

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <utility>
#include <vector>

namespace da::grid
{
    struct Cell
    {
        int x = 0;
        int y = 0;
        int level = 0; // the band of height the floor of the cell lies in
        bool operator==(const Cell&) const = default;
        auto operator<=>(const Cell&) const = default;
    };

    // Whether the step from one cell into the neighbouring place `to` is free. `to` comes in
    // at the level of `from` and goes out at the level the step lands on, so that a step up
    // a stair or down one is a step onto the cell of that other level. Asked once per step
    // from a cell; the caller may cache it.
    using Passable = std::function<bool(Cell from, Cell& to)>;

    // Where a step from a cell into the neighbouring column would land, when that is known without
    // asking the step itself (the floor there already probed): true with the level, false when not
    // known. A run that has settled the cell it would land on asks nothing: the step could not
    // take it anywhere new.
    using Landing = std::function<bool(Cell from, int toX, int toY, int& level)>;

    // A step as the step store knows it: the level a step from a cell into the neighbouring column
    // lands on when it is known free, else none.
    using KnownStep = std::function<std::optional<int>(Cell from, int toX, int toY)>;

    // The landing of a step from `a` into the column (bx, by) that the step back proves: her capsule
    // swept free from a cell of that column into `a` passes back along the same line, so the step is
    // free, onto the level the step back began on (`a`'s own level first, then the one above and the
    // one below). The sweep that tells a step is not even both ways where a passage is no wider than
    // she is: one begun against a wall is pulled out of it first, one that ends against it stops short
    // of the cell. In Sam's bathroom on 2026-11-07 the door's open leaf left such a channel to the hall:
    // the map got out of the bathroom by it, and from the hall every step back was swept into the leaf
    // or the hall's wall, so the sock 4 m off through the open door was left out as another room's
    // (research/sessions/s56-full.log, 23:47:21). None when no step back is known free.
    std::optional<int> MirroredLanding(const KnownStep& known, Cell a, int bx, int by);

    // What a line down a column met: whether it met anything, how high, and how steep (the z of the
    // surface's normal).
    struct FloorProbe
    {
        bool hit = false;
        double z = 0.0;
        double normalZ = 1.0;
    };
    // A column's floor as her capsule stands on it: the line down its middle, unless what it meets there is
    // too steep to stand on (`normalZ` under `walkableZ`), when the lines kFloorAround cm off it along x and
    // y are asked (`around`) and the highest walkable of them is the floor: her capsule rests on the highest
    // ground under it, and a sliver of steep triangle under one point holds nothing up. In act8_5's mine a
    // sliver of the floor mesh 1 to 2 cm wide (normal z 0.58) lay under the one column between the
    // corridor's walls at its narrowest, on the grid Matt's map was laid on; the map took it for no floor,
    // and the scene's trigger beyond was left out as closed off, the player stuck with Jessica at the lift
    // (2026-11-14, a friend's run; the level's collision replayed offline, scratchpad s74 corridor). A line
    // that meets nothing is no floor, whatever lies beside it: a hole is not bridged. None for no floor.
    constexpr double kFloorAround = 5.0;
    std::optional<FloorProbe> FloorAmid(const FloorProbe& middle, const std::vector<FloorProbe>& around, double walkableZ);

    // A way from a cell to one that is not its neighbour, which no step between neighbours finds:
    // the game's guidance passage through a doorway too tight for the cells' middles, which it takes
    // the character through from one end to the other. `cost` is in cells, as a step's.
    struct Link
    {
        Cell to;
        double cost = 1.0;
    };
    // The links out of a cell, asked once as the cell is expanded; none when not given.
    using Links = std::function<void(Cell from, std::vector<Link>& out)>;

    class Search
    {
    public:
        enum class State
        {
            Running,
            Found,
            Failed,
        };

        // `goalRadius` cells from the goal, over the ground, count as arriving (a thing stands
        // in furniture the character cannot enter), on a level within `goalLevels` of the
        // goal's (any level when negative; a thing on a balcony is not reached from under it);
        // `maxRadius` bounds the search around the start, and `maxExpansions` its total work.
        // `levelWeight` is how many cells over the ground one level of height counts as when
        // the cell nearest the goal is kept: a cell right under a goal out of reach upstairs
        // is not near it. `fits`, when given, must hold of a cell for it to count as arriving
        // too: a volume is reached where the character's capsule touches its own collision,
        // which the circle around its middle only bounds.
        Search(Cell start, Cell goal, double goalRadius, int goalLevels, int maxRadius, int maxExpansions, double levelWeight = 0.0,
               std::function<bool(Cell)> fits = {});

        // Expands up to `steps` cells and says how the search stands.
        State Step(int steps, const Passable& passable);
        // The links it takes besides the steps between neighbours.
        void SetLinks(Links links)
        {
            m_links = std::move(links);
        }
        State Current() const
        {
            return m_state;
        }
        // The cells from the start to the cell reached, once found.
        std::vector<Cell> Path() const;
        // The cells from the start to the cell nearest the goal that the search got to, found
        // or not: as far as the ground is known to carry the character toward the goal.
        std::vector<Cell> PartialPath() const;
        int Expanded() const
        {
            return m_expanded;
        }
        Cell Start() const
        {
            return m_start;
        }
        Cell Goal() const
        {
            return m_goal;
        }
        // The cell closed so far that lies nearest the goal: where a failed search got to.
        Cell Nearest() const
        {
            return m_nearest;
        }
        // Whether the search's bound around the start kept it from a cell it could have
        // stepped into: then a failure is no proof that the goal is closed off.
        bool RadiusReached() const
        {
            return m_radiusReached;
        }

    private:
        struct Open
        {
            double f = 0.0;
            double g = 0.0;
            Cell cell;
            bool operator>(const Open& o) const
            {
                return f > o.f;
            }
        };
        double Heuristic(Cell a) const;
        bool AtGoal(Cell a) const;
        double Nearness(Cell a) const;
        std::vector<Cell> PathTo(Cell end) const;

        Cell m_start;
        Cell m_goal;
        double m_goalRadius;
        int m_goalLevels;
        int m_maxRadius;
        int m_maxExpansions;
        double m_levelWeight;
        std::function<bool(Cell)> m_fits;
        Links m_links;
        std::vector<Link> m_linked;
        int m_expanded = 0;
        bool m_radiusReached = false;
        State m_state = State::Running;
        Cell m_reached;
        Cell m_nearest;
        double m_nearestDistance = 1e300;
        std::priority_queue<Open, std::vector<Open>, std::greater<Open>> m_open;
        std::map<Cell, double> m_best;   // the cheapest cost found to a cell
        std::map<Cell, Cell> m_cameFrom; // the cell each was reached from
        std::set<Cell> m_closed;
    };

    // Every cell the character can walk to from a start, nearest first: a reach map over the
    // same steps as Search (a diagonal step only where both straight steps beside it are free),
    // so that one run answers for every target at once, and the same way for each. Resumable
    // and bounded like Search. Once it has run out of cells (Complete) a cell it has not
    // reached is closed off from the start, unless the bound around the start cut it off
    // (RadiusReached); a run cut short by its budget proves nothing about the cells it has not
    // reached yet.
    class Flood;

    // Where a walk to a thing to use ends, over a run's reached cells within `reach` of `goal`: the
    // cell of least cost with her middle well inside the thing's place (`deep`) and outside every
    // volume the game turns it off in (`off`); else one her capsule touches the place from (`touches`)
    // outside them; else one touching it at all, the thing then got at only from inside such a volume.
    // In Sam's bathroom on 2026-11-07 the walks to her sock ended at the first cell whose capsule
    // touched its place, 31 cm outside it: she crossed its corner, the game took the press for a
    // quarter of a second, and her stop slid her out (research/sessions/s55-full.log).
    struct Arrival
    {
        bool found = false;
        Cell cell;
        bool deep = false;      // her middle inside the place
        bool offInside = false; // only from inside a volume the thing is off in
    };
    Arrival PickArrival(const Flood& flood, Cell goal, int reach, const std::function<bool(Cell)>& deep, const std::function<bool(Cell)>& touches,
                        const std::function<bool(Cell)>& off);

    class Flood
    {
    public:
        enum class State
        {
            Running,
            Complete,
            Budget,
        };

        Flood(Cell start, int maxRadius, int maxExpansions);

        // Expands up to `steps` cells and says how the run stands.
        State Step(int steps, const Passable& passable);
        // The links it takes besides the steps between neighbours.
        void SetLinks(Links links)
        {
            m_links = std::move(links);
        }
        // Where steps land without being asked: a step into a cell already reached is not asked.
        // At the asylum's grounds on 2026-11-07 the first map swept 246078 times over 27710 cells
        // and took 43 s; nearly half were steps back into cells the map had already settled.
        void SetLanding(Landing landing)
        {
            m_landing = std::move(landing);
        }
        State Current() const
        {
            return m_state;
        }
        bool Reached(Cell c) const
        {
            return m_closed.count(c) > 0;
        }
        // The cost of the cheapest way to a reached cell, in cells (a diagonal step counts its
        // length); -1 when it is not reached.
        double Cost(Cell c) const;
        // How far the run has got: the cost of the cell it reached last. Every cell cheaper
        // than this that it can reach at all, it has reached.
        double Frontier() const
        {
            return m_frontier;
        }
        // The cells from the start to a reached cell.
        std::vector<Cell> PathTo(Cell end) const;
        // The reached cell of least cost within `radius` cells of `goal` over the ground and on a
        // level within `levels` of the goal's (any when negative); false when there is none.
        bool BestIn(Cell goal, double radius, int levels, Cell& out) const;
        // The reached cell of least cost within `reach` cells of `goal` over the ground, on a level
        // within `levels` of the goal's (any when negative), that `fits` takes; false when there is
        // none.
        bool BestWhere(Cell goal, int reach, int levels, const std::function<bool(Cell)>& fits, Cell& out) const;
        // The reached cell nearest a goal, the levels between them weighed as in Search.
        Cell Nearest(Cell goal, double levelWeight) const;
        // The levels reached at a column, in the order they were reached; null when none is.
        const std::vector<int>* LevelsAt(int x, int y) const
        {
            const auto it = m_columns.find({x, y});
            return it == m_columns.end() ? nullptr : &it->second;
        }
        int Expanded() const
        {
            return m_expanded;
        }
        // The cells reached, in the order the run reached them: what a caller weighs cell by cell
        // takes up the ones reached since it last looked.
        const std::vector<Cell>& Order() const
        {
            return m_order;
        }
        Cell Start() const
        {
            return m_start;
        }
        bool RadiusReached() const
        {
            return m_radiusReached;
        }

    private:
        struct Open
        {
            double g = 0.0;
            Cell cell;
            bool operator>(const Open& o) const
            {
                return g > o.g;
            }
        };
        Cell m_start;
        int m_maxRadius;
        int m_maxExpansions;
        Links m_links;
        Landing m_landing;
        std::vector<Link> m_linked;
        int m_expanded = 0;
        bool m_radiusReached = false;
        double m_frontier = 0.0;
        State m_state = State::Running;
        std::priority_queue<Open, std::vector<Open>, std::greater<Open>> m_open;
        std::map<Cell, double> m_best;
        std::map<Cell, Cell> m_cameFrom;
        std::set<Cell> m_closed;
        std::vector<Cell> m_order;
        std::map<std::pair<int, int>, std::vector<int>> m_columns; // the levels reached at each column
    };

    // Pulls a cell path straight where the direct way between two of its cells is clear,
    // a few checks at a time: the cells left are the corners. `clear(a, b)` says whether the
    // straight way between two cells is free; no run longer than `maxRun` cells is tried.
    class Straightener
    {
    public:
        Straightener(std::vector<Cell> path, size_t maxRun);
        // Runs up to `checks` clear checks; true once the corners are complete.
        bool Step(int checks, const std::function<bool(Cell, Cell)>& clear);
        bool Done() const
        {
            return m_done;
        }
        const std::vector<Cell>& Corners() const
        {
            return m_corners;
        }

    private:
        std::vector<Cell> m_path;
        size_t m_maxRun;
        std::vector<Cell> m_corners;
        size_t m_anchor = 0;
        size_t m_candidate = 0; // the farthest cell still to be tried from the anchor
        bool m_done = false;
    };

    // The corners of a path, pulled straight in one go.
    std::vector<Cell> Corners(const std::vector<Cell>& path, const std::function<bool(Cell, Cell)>& clear, size_t maxRun = 1000000);

    // Distance between two cells in cells, over the ground, levels aside.
    double Distance(Cell a, Cell b);

    // The way between the ends of two paths of one flood, each from its start (Flood::PathTo): back
    // along the first to the last cell the two share, then on along the second; every step of it a
    // step the flood took. Empty when they share no cell (paths of two floods). `shared`, when asked,
    // is how many cells the two share from the start: the way goes back to the last of them.
    std::vector<Cell> TreeWay(const std::vector<Cell>& from, const std::vector<Cell>& to, size_t* shared = nullptr);
}
