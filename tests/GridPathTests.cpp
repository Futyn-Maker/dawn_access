// features/GridPath: the search over a grid of cells, with a map drawn in characters.
#include "Support.hpp"
#include "features/GridPath.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

using namespace da;

namespace
{
    // A map of rows: '#' blocks a cell, a digit is a free cell on that level, anything else
    // is free on level 0 (the start and the goal on the levels given). Rows go down the y
    // axis. A step may climb or drop one level, as a stair does, never more.
    struct Map
    {
        std::vector<std::string> rows;
        int startLevel = 0;
        int goalLevel = 0;
        char At(grid::Cell c) const
        {
            if (c.y < 0 || c.y >= static_cast<int>(rows.size())) return '#';
            if (c.x < 0 || c.x >= static_cast<int>(rows[static_cast<size_t>(c.y)].size())) return '#';
            return rows[static_cast<size_t>(c.y)][static_cast<size_t>(c.x)];
        }
        bool Free(grid::Cell c) const
        {
            return At(c) != '#';
        }
        int Level(grid::Cell c) const
        {
            const char ch = At(c);
            if (ch >= '0' && ch <= '9') return ch - '0';
            if (ch == 'S') return startLevel;
            if (ch == 'G') return goalLevel;
            return 0;
        }
        grid::Passable Passable() const
        {
            return [this](grid::Cell from, grid::Cell& to)
            {
                if (!Free(to)) return false;
                const int level = Level(to);
                if (std::abs(level - from.level) > 1) return false;
                to.level = level;
                return true;
            };
        }
        grid::Cell Find(char mark) const
        {
            for (int y = 0; y < static_cast<int>(rows.size()); ++y)
            {
                const auto x = rows[static_cast<size_t>(y)].find(mark);
                if (x != std::string::npos) return {static_cast<int>(x), y, Level({static_cast<int>(x), y})};
            }
            return {-1, -1, 0};
        }
    };

    std::vector<grid::Cell> Solve(const Map& map, double goalRadius = 0.0, int goalLevels = -1)
    {
        grid::Search search(map.Find('S'), map.Find('G'), goalRadius, goalLevels, 100, 100000);
        while (search.Step(50, map.Passable()) == grid::Search::State::Running)
        {
        }
        return search.Path();
    }
}

TEST_CASE("a way is found round a wall, and it starts at the start and ends at the goal")
{
    const Map map{{
        "S....#....",
        ".....#....",
        ".....#....",
        ".....#..G.",
        "..........",
    }};
    const auto path = Solve(map);
    REQUIRE(!path.empty());
    CHECK(path.front() == map.Find('S'));
    CHECK(path.back() == map.Find('G'));
    for (const auto& c : path)
        CHECK(map.Free(c));
    // Round the wall's end: the way passes the free row below the wall.
    CHECK(std::any_of(path.begin(), path.end(), [](const grid::Cell& c) { return c.y == 4; }));
}

TEST_CASE("a goal walled in is not reached, and the search says so after bounded work")
{
    const Map map{{
        "S.........",
        "....###...",
        "....#G#...",
        "....###...",
        "..........",
    }};
    grid::Search search(map.Find('S'), map.Find('G'), 0.0, -1, 100, 100000);
    while (search.Step(50, map.Passable()) == grid::Search::State::Running)
    {
    }
    CHECK(search.Current() == grid::Search::State::Failed);
    CHECK(search.Path().empty());
    CHECK(search.Expanded() <= 50);
}

TEST_CASE("a failed search still knows the way to the cell it got nearest the goal")
{
    const Map map{{
        "S.........",
        "....###...",
        "....#G#...",
        "....###...",
        "..........",
    }};
    grid::Search search(map.Find('S'), map.Find('G'), 0.0, -1, 100, 100000);
    while (search.Step(50, map.Passable()) == grid::Search::State::Running)
    {
    }
    REQUIRE(search.Current() == grid::Search::State::Failed);
    const auto partial = search.PartialPath();
    REQUIRE(partial.size() >= 2);
    CHECK(partial.front() == map.Find('S'));
    CHECK(partial.back() == search.Nearest());
    // The nearest cell touches the wall round the goal, two cells from the goal straight on.
    CHECK(grid::Distance(partial.back(), map.Find('G')) == doctest::Approx(2.0));
    for (size_t i = 0; i < partial.size(); ++i)
    {
        CHECK(map.Free(partial[i]));
        if (i > 0) CHECK(std::max(std::abs(partial[i].x - partial[i - 1].x), std::abs(partial[i].y - partial[i - 1].y)) == 1);
    }
}

TEST_CASE("the nearest cell weighs the levels between it and the goal when asked to")
{
    // The goal stands on level 3, reached from nowhere. Right beside it lies level 0; a ramp
    // climbs to level 2 farther off. Over the ground alone the cell beside the goal is the
    // nearest; with a level counted as 2.5 cells, the top of the ramp is.
    const Map map{{
        "S0000002",
        "00000012",
        "#####0##",
        "#####0##",
        "##0#####",
        "##0G####",
    }};
    Map tall = map;
    tall.goalLevel = 3;
    const auto run = [&](double weight)
    {
        grid::Search search(tall.Find('S'), tall.Find('G'), 0.0, 0, 100, 100000, weight);
        while (search.Step(50, tall.Passable()) == grid::Search::State::Running)
        {
        }
        CHECK(search.Current() == grid::Search::State::Failed);
        return search.Nearest();
    };
    const grid::Cell flat = run(0.0);
    CHECK(flat.level == 0);
    const grid::Cell weighed = run(2.5);
    CHECK(weighed.level == 2);
}

TEST_CASE("a found search's partial path is its whole path")
{
    const Map map{{
        "S....#....",
        ".....#....",
        ".....#..G.",
        "..........",
    }};
    grid::Search search(map.Find('S'), map.Find('G'), 0.0, -1, 100, 100000);
    while (search.Step(50, map.Passable()) == grid::Search::State::Running)
    {
    }
    REQUIRE(search.Current() == grid::Search::State::Found);
    CHECK(search.PartialPath() == search.Path());
}

TEST_CASE("a goal inside furniture is reached at the nearest free cell within the goal radius")
{
    const Map map{{
        "S.........",
        "....###...",
        "....#G#...",
        "....###...",
        "..........",
    }};
    // Every neighbour of the goal is a wall, so the way ends two cells off.
    const auto path = Solve(map, 2.5);
    REQUIRE(!path.empty());
    CHECK(grid::Distance(path.back(), map.Find('G')) <= 2.5);
    CHECK(grid::Distance(path.back(), map.Find('G')) >= 2.0);
    CHECK(map.Free(path.back()));
}

TEST_CASE("no corner is cut: a diagonal step needs both straight steps beside it free")
{
    const Map map{{
        "S#...",
        "#....",
        "....G",
    }};
    const auto path = Solve(map);
    CHECK(path.empty());
}

TEST_CASE("the search is resumable and bounded by its radius")
{
    const Map map{{
        "S.........G",
    }};
    grid::Search search(map.Find('S'), map.Find('G'), 0.0, -1, 4, 100000);
    int rounds = 0;
    while (search.Step(1, map.Passable()) == grid::Search::State::Running)
        ++rounds;
    CHECK(search.Current() == grid::Search::State::Failed);
    CHECK(rounds >= 4);
    // The bound, not a wall, stopped it: the failure proves nothing closed.
    CHECK(search.RadiusReached());
}

TEST_CASE("a goal walled in within the bound fails without the bound having a say")
{
    const Map map{{
        "S.........",
        "....###...",
        "....#G#...",
        "....###...",
        "..........",
    }};
    grid::Search search(map.Find('S'), map.Find('G'), 0.0, -1, 100, 100000);
    while (search.Step(50, map.Passable()) == grid::Search::State::Running)
    {
    }
    CHECK(search.Current() == grid::Search::State::Failed);
    CHECK_FALSE(search.RadiusReached());
}

TEST_CASE("a stair is climbed level by level, and the cells of the way carry their levels")
{
    Map map{{
        "S.0123456G",
    }};
    map.goalLevel = 6;
    const auto path = Solve(map);
    REQUIRE(path.size() == 10);
    CHECK(path.front().level == 0);
    CHECK(path[5].level == 3);
    CHECK(path.back().level == 6);
}

TEST_CASE("a drop or a climb of more than one level is no way, round or not")
{
    Map map{{
        "S.0125..G",
        ".........",
    }};
    map.goalLevel = 5;
    // The only way onto level 5 jumps from level 2; the row below stays on level 0.
    CHECK(Solve(map).empty());
    map.rows[0] = "S.012345G";
    CHECK(!Solve(map).empty());
}

TEST_CASE("a goal on another level is not reached from under it unless levels do not matter")
{
    Map map{{
        "S....",
        "....G",
    }};
    map.goalLevel = 5;
    // Within one cell of the goal's place the search stands on level 0; the goal is five up.
    CHECK(Solve(map, 1.5, 1).empty());
    CHECK(!Solve(map, 1.5, -1).empty());
    CHECK(!Solve(map, 1.5, 5).empty());
}

TEST_CASE("Corners keeps only the cells where the straight way turns")
{
    const Map map{{
        "S....#....",
        ".....#....",
        ".....#....",
        ".....#..G.",
        "..........",
    }};
    const auto path = Solve(map);
    const auto clear = [&](grid::Cell a, grid::Cell b)
    {
        // Every cell along the straight line must be free.
        const int steps = std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
        for (int i = 1; i < steps; ++i)
        {
            const grid::Cell c{a.x + (b.x - a.x) * i / steps, a.y + (b.y - a.y) * i / steps};
            if (!map.Free(c)) return false;
        }
        return true;
    };
    const auto corners = grid::Corners(path, clear);
    REQUIRE(corners.size() >= 2);
    CHECK(corners.front() == path.front());
    CHECK(corners.back() == path.back());
    CHECK(corners.size() < path.size());
}

TEST_CASE("the Straightener pulls the same corners a few checks at a time, and keeps a run within its limit")
{
    const Map map{{
        "S....#....",
        ".....#....",
        ".....#....",
        ".....#..G.",
        "..........",
    }};
    const auto path = Solve(map);
    const auto clear = [&](grid::Cell a, grid::Cell b)
    {
        const int steps = std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
        for (int i = 1; i < steps; ++i)
        {
            const grid::Cell c{a.x + (b.x - a.x) * i / steps, a.y + (b.y - a.y) * i / steps};
            if (!map.Free(c)) return false;
        }
        return true;
    };
    const auto whole = grid::Corners(path, clear);
    grid::Straightener piecemeal(path, 1000);
    int rounds = 0;
    while (!piecemeal.Step(1, clear))
        ++rounds;
    CHECK(piecemeal.Corners() == whole);
    CHECK(rounds >= 2);
    // With runs of at most two cells every other cell stays a corner.
    const auto limited = grid::Corners(path, clear, 2);
    REQUIRE(limited.size() >= 2);
    for (size_t i = 1; i < limited.size(); ++i)
        CHECK(std::max(std::abs(limited[i].x - limited[i - 1].x), std::abs(limited[i].y - limited[i - 1].y)) <= 2);
    CHECK(limited.back() == path.back());
}

namespace
{
    grid::Flood RunFlood(const Map& map, int maxRadius = 100, int maxExpansions = 100000)
    {
        grid::Flood flood(map.Find('S'), maxRadius, maxExpansions);
        while (flood.Step(7, map.Passable()) == grid::Flood::State::Running)
        {
        }
        return flood;
    }

    double PathCost(const std::vector<grid::Cell>& path)
    {
        double cost = 0.0;
        for (size_t i = 1; i < path.size(); ++i)
            cost += (path[i].x != path[i - 1].x && path[i].y != path[i - 1].y) ? 1.4142135623730951 : 1.0;
        return cost;
    }
}

TEST_CASE("a flood reaches every free cell of a closed place and stops when it has run out of them")
{
    const Map map{{
        "#######",
        "#S....#",
        "#.###.#",
        "#.....#",
        "#######",
    }};
    const auto flood = RunFlood(map);
    CHECK(flood.Current() == grid::Flood::State::Complete);
    CHECK_FALSE(flood.RadiusReached());
    int free = 0;
    for (int y = 0; y < 5; ++y)
    {
        for (int x = 0; x < 7; ++x)
        {
            if (!map.Free({x, y})) continue;
            ++free;
            CHECK(flood.Reached({x, y, 0}));
        }
    }
    CHECK(flood.Expanded() == free);
}

TEST_CASE("a flood's cost to a cell is the cost of the search's way there, and its path leads there step by step")
{
    const Map map{{
        "S....#....",
        ".....#....",
        ".....#....",
        ".....#..G.",
        "..........",
    }};
    const auto flood = RunFlood(map);
    const auto path = Solve(map);
    REQUIRE(!path.empty());
    const grid::Cell goal = map.Find('G');
    CHECK(flood.Cost(goal) == doctest::Approx(PathCost(path)));
    const auto walked = flood.PathTo(goal);
    REQUIRE(walked.size() >= 2);
    CHECK(walked.front() == map.Find('S'));
    CHECK(walked.back() == goal);
    for (size_t i = 1; i < walked.size(); ++i)
    {
        CHECK(map.Free(walked[i]));
        CHECK(std::max(std::abs(walked[i].x - walked[i - 1].x), std::abs(walked[i].y - walked[i - 1].y)) == 1);
    }
    CHECK(PathCost(walked) == doctest::Approx(flood.Cost(goal)));
}

TEST_CASE("a place walled off from the start is not reached by a complete flood, and BestIn finds nothing there")
{
    const Map map{{
        "S.........",
        "....###...",
        "....#G#...",
        "....###...",
        "..........",
    }};
    const auto flood = RunFlood(map);
    CHECK(flood.Current() == grid::Flood::State::Complete);
    CHECK_FALSE(flood.Reached(map.Find('G')));
    grid::Cell best;
    CHECK_FALSE(flood.BestIn(map.Find('G'), 0.5, -1, best));
    // Within reach of the walls round it, the nearest reached cells are found.
    CHECK(flood.BestIn(map.Find('G'), 2.0, -1, best));
    CHECK(grid::Distance(best, map.Find('G')) == doctest::Approx(2.0));
}

TEST_CASE("BestIn takes the cheapest reached cell within the radius, on the levels asked")
{
    Map map{{
        "S.......",
        "########",
        ".......G",
    }};
    // The far side of the wall is reached round no end: closed off.
    auto flood = RunFlood(map);
    grid::Cell best;
    CHECK_FALSE(flood.BestIn(map.Find('G'), 1.5, -1, best));
    map.rows[1] = "#######.";
    flood = RunFlood(map);
    REQUIRE(flood.BestIn(map.Find('G'), 1.5, -1, best));
    // The gap itself is the cheapest of the cells within a cell and a half of the goal: seven
    // steps along the top row, one down into the gap.
    CHECK(best == grid::Cell{7, 1, 0});
    CHECK(flood.Cost(best) == doctest::Approx(8.0));
    // A goal five levels up is not reached from level 0 when only one level counts.
    CHECK_FALSE(flood.BestIn(grid::Cell{7, 2, 5}, 1.5, 1, best));
}

TEST_CASE("a flood cut short by its budget says so, and so does one cut by its bound")
{
    const Map map{{
        "S...................",
        "....................",
        "....................",
    }};
    grid::Flood small(map.Find('S'), 100, 10);
    while (small.Step(3, map.Passable()) == grid::Flood::State::Running)
    {
    }
    CHECK(small.Current() == grid::Flood::State::Budget);
    CHECK(small.Expanded() == 10);
    const auto bounded = RunFlood(map, 5);
    CHECK(bounded.Current() == grid::Flood::State::Complete);
    CHECK(bounded.RadiusReached());
    CHECK_FALSE(bounded.Reached({10, 0, 0}));
}

TEST_CASE("a flood climbs a stair level by level and its frontier only grows")
{
    Map map{{
        "S.0123456G",
    }};
    map.goalLevel = 6;
    grid::Flood flood(map.Find('S'), 100, 100000);
    double frontier = 0.0;
    while (flood.Step(1, map.Passable()) == grid::Flood::State::Running)
    {
        CHECK(flood.Frontier() >= frontier);
        frontier = flood.Frontier();
    }
    CHECK(flood.Reached({9, 0, 6}));
    CHECK(flood.Cost({9, 0, 6}) == doctest::Approx(9.0));
    CHECK_FALSE(flood.Reached({9, 0, 0}));
}

TEST_CASE("a link takes the search and the flood through a wall the steps cannot pass, from one end to the other only")
{
    // A wall with a doorway too tight for the cells (no step through it), and the game's passage
    // through it as a link between the cells at its two ends.
    const Map map{{
        "S..#...",
        "...#...",
        "...#..G",
    }};
    const grid::Cell a{2, 1, 0};
    const grid::Cell b{4, 1, 0};
    const auto links = [&](grid::Cell from, std::vector<grid::Link>& out)
    {
        if (from == a) out.push_back({b, 2.0});
        if (from == b) out.push_back({a, 2.0});
    };
    // Without the link the goal is walled in.
    CHECK(Solve(map).empty());
    grid::Search search(map.Find('S'), map.Find('G'), 0.0, -1, 100, 100000);
    search.SetLinks(links);
    while (search.Step(50, map.Passable()) == grid::Search::State::Running)
    {
    }
    const auto path = search.Path();
    REQUIRE_FALSE(path.empty());
    // The way goes through the link: its two ends follow each other.
    const auto at = std::find(path.begin(), path.end(), a);
    REQUIRE(at != path.end());
    REQUIRE(at + 1 != path.end());
    CHECK(*(at + 1) == b);
    // The flood reaches the far side only through the link, and counts its cost.
    grid::Flood walled(map.Find('S'), 100, 100000);
    while (walled.Step(50, map.Passable()) == grid::Flood::State::Running)
    {
    }
    CHECK_FALSE(walled.Reached(map.Find('G')));
    grid::Flood linked(map.Find('S'), 100, 100000);
    linked.SetLinks(links);
    while (linked.Step(50, map.Passable()) == grid::Flood::State::Running)
    {
    }
    CHECK(linked.Reached(map.Find('G')));
    CHECK(linked.Cost(b) == doctest::Approx(linked.Cost(a) + 2.0));
}
