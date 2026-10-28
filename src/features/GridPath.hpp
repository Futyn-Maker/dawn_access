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
#include <queue>
#include <set>
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
        // is not near it.
        Search(Cell start, Cell goal, double goalRadius, int goalLevels, int maxRadius, int maxExpansions, double levelWeight = 0.0);

        // Expands up to `steps` cells and says how the search stands.
        State Step(int steps, const Passable& passable);
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
        int m_expanded = 0;
        bool m_radiusReached = false;
        double m_frontier = 0.0;
        State m_state = State::Running;
        std::priority_queue<Open, std::vector<Open>, std::greater<Open>> m_open;
        std::map<Cell, double> m_best;
        std::map<Cell, Cell> m_cameFrom;
        std::set<Cell> m_closed;
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
}
