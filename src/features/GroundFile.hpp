#pragma once
// A record of the ground as the mod felt it out in play: the grid's anchor, every floor
// probe, every step between cells, every search and the character's positions, written as
// lines of text. The pathfinder can then be run over the recorded ground again without the
// game, in tests and in diagnosis, and a failure in play can be replayed offline.
//
// Lines (whitespace-separated, one event each):
//   ground 1
//   A x y z cell level half radius step walkable     the grid laid at a spot, with the character's sizes
//   F x y level floor normalZ [on]                   a floor found at a column, probed from a level, and
//                                                    the level of the world it lies in, where known
//   N x y level why                                  no floor at a column from a level (n none, s too steep)
//   S ax ay alevel bx by land why clear [on]         a step: land = the level landed on, why '-' (swept),
//                                                    M (free by the step back, grid::MirroredLanding), or R
//                                                    (swept again from a spot of the cell where she stands
//                                                    free, ways::SweepFromFreeSpot); or X with
//                                                    why (A/B no floor at a/b, H too high, W swept into something,
//                                                    D a door's leaf across it), and for W and D the level of
//                                                    the world what stopped it lies in, where known
//   L in|out name ...                                levels of the world the game streamed in or out, the
//                                                    ground felt out anew by the rules of features/WorldStream
//   Q fx fy flevel tx ty tlevel radius levels        a search begins
//   R found|failed cells sweeps nx ny nlevel         a search ends, with the cell that got nearest the goal
//   P x y z                                          where the character stands
//   V name H x y z x y z ...                         a piece of a volume's collision, in the world, by the
//   V name B x y z radius                            name the level's scripts call it: a convex piece's
//   V name C ax ay az bx by bz radius                points, a ball, a capsule (features/Shapes)
//   G name ax ay az bx by bz free                    a guidance passage of the game's: its two entry
//                                                    points on the floor, and whether the character's
//                                                    capsule was walked from one to the other (1) or
//                                                    not (0) (ways::PassageWalk)
//   K name ax ay az bx by bz along bits              a row of where her capsule fits in a passage too
//                                                    tight for every way tried, its middle `along` cm
//                                                    from the near end, one bit each 2 cm across from
//                                                    40 cm left of the line to 40 right, '1' free; the
//                                                    ends at the height of her middle (not read back)
//   T x y level floor steps                          a cell's floor and the eight steps out of it, in a
//                                                    test fixture cut from a record: one character per
//                                                    neighbour (dy -1 to 1, dx -1 to 1, the cell itself
//                                                    left out), '.' not free, '-' '0' '+' free onto the
//                                                    level below, the same or above
#include "features/GridPath.hpp"
#include "features/Shapes.hpp"
#include "features/WayList.hpp"

#include <map>
#include <optional>
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
        std::string on; // the level of the world the floor lies in, empty where not known
    };

    struct Step
    {
        grid::Cell from;
        int bx = 0, by = 0;
        bool free = false;
        int land = 0;
        char why = 0; // 'A', 'B', 'H', 'W', 'D'
        double clear = 0.0;
        std::string on; // for W and D, the level of the world what stopped it lies in, where known
    };

    // Levels of the world streamed in or out, after the steps and probes recorded before it.
    struct Levels
    {
        bool in = false;
        std::vector<std::string> names;
        size_t steps = 0;
        size_t probes = 0;
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
    std::string LevelsLine(const Levels& l);
    // A volume's collision, a line for each piece.
    std::vector<std::string> VolumeLines(const std::string& name, const shapes::Shape& shape);
    std::string PassageLine(const ways::Passage& p);

    using StepKey = std::tuple<int, int, int, int, int>;

    // A record read back: the events, and the steps as a map the search can be run over.
    struct Record
    {
        std::vector<Anchor> anchors;
        std::vector<Probe> probes;
        std::vector<Step> steps;
        std::vector<Search> searches;
        std::vector<Position> positions;
        std::vector<Levels> levels;
        std::map<StepKey, int> stepLevels;                  // the level a free step lands on
        std::map<std::tuple<int, int, int>, double> floors; // the floor of a cell (x, y, level), where known
        std::map<std::string, shapes::Shape> volumes;       // the collision of the volumes the ways on were, by name
        std::vector<ways::Passage> passages;                // the game's guidance passages, as swept

        // Reads a whole record; unknown lines are skipped. False when no header was found.
        bool Parse(const std::string& text);
        // The recorded steps as the search's answer, as the game's ground answers them: a step not
        // recorded is free where the step back was recorded free (grid::MirroredLanding, unless
        // `mirror` is false, as the mod took the ground before 2026-11-08), else not free, and counted
        // in `unknown` when given.
        grid::Passable Passable(int* unknown = nullptr, bool mirror = true) const;
        // The level a recorded free step lands on.
        std::optional<int> FreeStep(grid::Cell from, int bx, int by) const;
        // The step recorded from a cell into a column, if any.
        const Step* Find(grid::Cell from, int bx, int by) const;
        // The links the recorded passages give the cells of the last anchor's grid, the cell of an
        // end found by the floor recorded at its column nearest the end's height.
        grid::Links Links() const;
    };
}
