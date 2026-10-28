#pragma once
// A record of the ground as the mod felt it out in play: the grid's anchor, every floor
// probe, every step between cells, every search and the character's positions, written as
// lines of text. The pathfinder can then be run over the recorded ground again without the
// game, in tests and in diagnosis, and a failure in play can be replayed offline.
//
// Lines (whitespace-separated, one event each):
//   ground 1
//   A x y z cell level half radius step walkable     the grid laid at a spot, with the character's sizes
//   F x y level floor normalZ                        a floor found at a column, probed from a level
//   N x y level why                                  no floor at a column from a level (n none, s too steep)
//   S ax ay alevel bx by land why clear              a step: land = the level landed on, or X with why
//                                                    (A/B no floor at a/b, H too high, W swept into something)
//   Q fx fy flevel tx ty tlevel radius levels        a search begins
//   R found|failed cells sweeps nx ny nlevel         a search ends, with the cell that got nearest the goal
//   P x y z                                          where the character stands
//   T x y level floor steps                          a cell's floor and the eight steps out of it, in a
//                                                    test fixture cut from a record: one character per
//                                                    neighbour (dy -1 to 1, dx -1 to 1, the cell itself
//                                                    left out), '.' not free, '-' '0' '+' free onto the
//                                                    level below, the same or above
#include "features/GridPath.hpp"

#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace da::ground
{
    struct Anchor
    {
        double x = 0.0, y = 0.0, z = 0.0;
        double cell = 20.0, level = 50.0, half = 88.0, radius = 34.0, step = 45.0, walkable = 0.71;
    };

    struct Probe
    {
        int x = 0, y = 0, level = 0;
        bool found = false;
        double floor = 0.0, normalZ = 1.0;
        char why = 0; // 'n' none, 's' too steep
    };

    struct Step
    {
        grid::Cell from;
        int bx = 0, by = 0;
        bool free = false;
        int land = 0;
        char why = 0; // 'A', 'B', 'H', 'W'
        double clear = 0.0;
    };

    struct Search
    {
        grid::Cell from, to;
        double radius = 0.0;
        int levels = 0;
        bool done = false, found = false;
        int cells = 0, sweeps = 0;
        grid::Cell nearest;
    };

    struct Position
    {
        double x = 0.0, y = 0.0, z = 0.0;
    };

    std::string HeaderLine();
    std::string AnchorLine(const Anchor& a);
    std::string ProbeLine(const Probe& p);
    std::string StepLine(const Step& s);
    std::string SearchLine(const Search& s);
    std::string ResultLine(const Search& s);
    std::string PositionLine(const Position& p);

    using StepKey = std::tuple<int, int, int, int, int>;

    // A record read back: the events, and the steps as a map the search can be run over.
    struct Record
    {
        std::vector<Anchor> anchors;
        std::vector<Probe> probes;
        std::vector<Step> steps;
        std::vector<Search> searches;
        std::vector<Position> positions;
        std::map<StepKey, int> stepLevels;                  // the level a free step lands on
        std::map<std::tuple<int, int, int>, double> floors; // the floor of a cell (x, y, level), where known

        // Reads a whole record; unknown lines are skipped. False when no header was found.
        bool Parse(const std::string& text);
        // The recorded steps as the search's answer: a step not recorded is not free, and is
        // counted in `unknown` when given.
        grid::Passable Passable(int* unknown = nullptr) const;
        // The step recorded from a cell into a column, if any.
        const Step* Find(grid::Cell from, int bx, int by) const;
    };
}
