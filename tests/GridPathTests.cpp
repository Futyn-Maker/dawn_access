// features/GridPath: the search over a grid of cells, with a map drawn in characters, and the hall
// of 2026-11-07 flooded over its recorded ground.
#include "Support.hpp"
#include "features/GridPath.hpp"
#include "features/GroundFile.hpp"
#include "features/Shapes.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
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

TEST_CASE("a flood that knows where steps land asks none into cells it has reached, and reaches the same: the asylum's hall, 2026-11-07")
{
    // The hall as it was first felt out (tests/fixtures/ground-asylum-hall-streamed.tsv, the T lines):
    // the first map after the hall's checkpoint was loaded swept 246078 times over 27710 cells and
    // took 43 s (research/sessions/s54-full.log), nearly half of it steps back into cells it had settled.
    std::ifstream in(std::filesystem::path(DA_REPO_ROOT) / "tests" / "fixtures" / "ground-asylum-hall-streamed.tsv");
    REQUIRE(in.good());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ground::Record record;
    REQUIRE(record.Parse(text));
    std::map<ground::StepKey, int> hall = record.stepLevels;
    for (const auto& s : record.steps)
        hall.erase(ground::StepKey{s.from.x, s.from.y, s.from.level, s.bx, s.by});
    record.stepLevels = hall;
    const auto& anchor = record.anchors.front();
    const grid::Cell start{static_cast<int>(std::floor((record.positions[0].x - anchor.x) / anchor.cell)),
                           static_cast<int>(std::floor((record.positions[0].y - anchor.y) / anchor.cell)), 173};
    const auto base = record.Passable();
    size_t asked = 0;
    const grid::Passable counted = [&](grid::Cell from, grid::Cell& to)
    {
        ++asked;
        return base(from, to);
    };
    const auto run = [&](bool landing)
    {
        asked = 0;
        auto flood = std::make_unique<grid::Flood>(start, 400, 200000);
        if (landing)
        {
            // Where a step lands is the floor of its column, known once probed: the record's own answer.
            flood->SetLanding(
                [&](grid::Cell from, int x, int y, int& level)
                {
                    const auto known = hall.find(ground::StepKey{from.x, from.y, from.level, x, y});
                    if (known == hall.end()) return false;
                    level = known->second;
                    return true;
                });
        }
        while (flood->Step(64, counted) == grid::Flood::State::Running)
        {
        }
        return std::make_pair(std::move(flood), asked);
    };
    const auto [plain, askedPlain] = run(false);
    const auto [knowing, askedKnowing] = run(true);
    REQUIRE(plain->Current() == grid::Flood::State::Complete);
    REQUIRE(knowing->Current() == grid::Flood::State::Complete);
    CHECK(knowing->Expanded() == plain->Expanded());
    CHECK(plain->Expanded() > 10000);
    // Every cell reached at the same cost.
    for (const auto& c : plain->Order())
    {
        REQUIRE(knowing->Reached(c));
        CHECK(knowing->Cost(c) == doctest::Approx(plain->Cost(c)));
    }
    // Far fewer steps asked.
    CHECK(static_cast<double>(askedKnowing) < 0.75 * static_cast<double>(askedPlain));
}

TEST_CASE("a walk to a thing to use ends well inside its place, not at its rim: Sam's sock, 2026-11-07")
{
    // Sam's bathroom as the map felt it out (tests/fixtures/ground-sams-bathroom.tsv, cut from
    // research/dumps/ground-20261107-230006.tsv) and the sock's place as the log gave its collision,
    // within (3511, -9673, 3284) to (3650, -9502, 3484). The walks ended at the first cell whose capsule
    // touched it, 31 cm outside: she crossed its corner, the game took the press for a quarter of a
    // second, and her stop slid her out; the second walk too (research/sessions/s55-full.log).
    std::ifstream in(std::filesystem::path(DA_REPO_ROOT) / "tests" / "fixtures" / "ground-sams-bathroom.tsv");
    REQUIRE(in.good());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ground::Record record;
    REQUIRE(record.Parse(text));
    const auto& anchor = record.anchors.front();
    using shapes::Vec;
    const Vec low{3511.0, -9673.0, 3284.0};
    const Vec high{3650.0, -9502.0, 3484.0};
    shapes::Shape place;
    place.hulls.push_back(shapes::BoxHull(Vec{(low.x + high.x) / 2.0, (low.y + high.y) / 2.0, (low.z + high.z) / 2.0},
                                          Vec{(high.x - low.x) / 2.0, (high.y - low.y) / 2.0, (high.z - low.z) / 2.0}, shapes::Quat{}));
    const auto middle = [&](grid::Cell c)
    {
        const auto floor = record.floors.find({c.x, c.y, c.level});
        const double z = (floor != record.floors.end() ? floor->second : c.level * anchor.level) + anchor.half;
        return Vec{anchor.x + (c.x + 0.5) * anchor.cell, anchor.y + (c.y + 0.5) * anchor.cell, z};
    };
    const grid::Cell start{static_cast<int>(std::floor((record.positions[0].x - anchor.x) / anchor.cell)),
                           static_cast<int>(std::floor((record.positions[0].y - anchor.y) / anchor.cell)), 67};
    grid::Flood flood(start, 200, 100000);
    const auto passable = record.Passable();
    while (flood.Step(64, passable) == grid::Flood::State::Running)
    {
    }
    REQUIRE(flood.Current() == grid::Flood::State::Complete);
    const grid::Cell goal{static_cast<int>(std::floor(((low.x + high.x) / 2.0 - anchor.x) / anchor.cell)),
                          static_cast<int>(std::floor(((low.y + high.y) / 2.0 - anchor.y) / anchor.cell)), 67};
    const int reach = static_cast<int>(std::ceil((std::hypot(high.x - low.x, high.y - low.y) / 2.0 + anchor.radius) / anchor.cell + 1.0));
    const auto touches = [&](grid::Cell c) { return shapes::CapsuleTouches(place, middle(c), anchor.radius, anchor.half); };
    const auto deep = [&](grid::Cell c) { return shapes::CapsuleTouches(place, middle(c), 0.0, anchor.half); };
    const auto inside = [&](const Vec& m) { return m.x >= low.x && m.x <= high.x && m.y >= low.y && m.y <= high.y; };
    // The rim the walks headed for: her capsule touching the place, her middle outside it.
    const auto rim = grid::PickArrival(flood, goal, reach, nullptr, touches, nullptr);
    REQUIRE(rim.found);
    CHECK_FALSE(rim.deep);
    CHECK_FALSE(inside(middle(rim.cell)));
    // Well inside: her middle within the place, so the stop's slide carries her further in.
    const auto arrival = grid::PickArrival(flood, goal, reach, deep, touches, nullptr);
    REQUIRE(arrival.found);
    CHECK(arrival.deep);
    CHECK(inside(middle(arrival.cell)));
    CHECK(flood.Reached(arrival.cell));
}

TEST_CASE("an arrival keeps out of the volumes the thing is off in, and is got from inside one only where there is no other ground")
{
    const Map map{{
        "#######",
        "#S....#",
        "#.....#",
        "#######",
    }};
    const auto flood = RunFlood(map);
    // The place: the right half of the room; the volume it is off in: the bottom row of it.
    const auto touches = [](grid::Cell c) { return c.x >= 3; };
    const auto deep = [](grid::Cell c) { return c.x >= 4; };
    const auto offBottom = [](grid::Cell c) { return c.y == 2; };
    auto a = grid::PickArrival(flood, map.Find('S'), 10, deep, touches, offBottom);
    REQUIRE(a.found);
    CHECK(a.deep);
    CHECK(a.cell.x >= 4);
    CHECK(a.cell.y != 2);
    CHECK_FALSE(a.offInside);
    // No deep ground: the touching ground outside the volume.
    a = grid::PickArrival(flood, map.Find('S'), 10, [](grid::Cell) { return false; }, touches, offBottom);
    REQUIRE(a.found);
    CHECK_FALSE(a.deep);
    CHECK(a.cell.x >= 3);
    CHECK(a.cell.y != 2);
    // Every touching cell inside the volume: from inside it, said so.
    a = grid::PickArrival(flood, map.Find('S'), 10, nullptr, touches, [](grid::Cell c) { return c.x >= 3; });
    REQUIRE(a.found);
    CHECK(a.offInside);
}

TEST_CASE("a step known free one way lands, the other way, where the step back began")
{
    std::map<std::tuple<int, int, int, int, int>, int> steps{
        {{5, 5, 10, 6, 5}, 10}, // level
        {{7, 7, 11, 8, 7}, 12}, // a step up
    };
    const grid::KnownStep known = [&](grid::Cell from, int x, int y) -> std::optional<int>
    {
        const auto it = steps.find({from.x, from.y, from.level, x, y});
        if (it == steps.end()) return std::nullopt;
        return it->second;
    };
    CHECK(grid::MirroredLanding(known, grid::Cell{6, 5, 10}, 5, 5) == 10);
    CHECK(grid::MirroredLanding(known, grid::Cell{8, 7, 12}, 7, 7) == 11); // the step down
    CHECK_FALSE(grid::MirroredLanding(known, grid::Cell{8, 7, 11}, 7, 7)); // the step back landed above it
    CHECK_FALSE(grid::MirroredLanding(known, grid::Cell{6, 5, 10}, 4, 5)); // nothing known back
    CHECK_FALSE(grid::MirroredLanding(nullptr, grid::Cell{6, 5, 10}, 5, 5));
}

TEST_CASE("a step is free both ways: the map begun in the hall gets back into Sam's bathroom through the open door, 2026-11-07")
{
    // tests/fixtures/ground-sams-bath-door.tsv: the bathroom, its doorway and the hall beyond as the
    // mod's ground knew them when the map began past the doorway at 23:47:21.508 (cut from
    // research/dumps/ground-20261107-234549.tsv up to its line 10923 by research/tools/cut_ground.py).
    // The door pushed open stood ajar across the doorway; the first map, begun in the bathroom, got out
    // by the channel beside its leaf, and every step of that channel taken back from the hall was swept
    // into the leaf or the hall's wall: the map begun past it opened the hall's 281 cells and left the
    // sock, 4 m off, out as another room's until the chapter's end (research/sessions/s56-full.log).
    std::ifstream in(std::filesystem::path(DA_REPO_ROOT) / "tests" / "fixtures" / "ground-sams-bath-door.tsv");
    REQUIRE(in.good());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ground::Record record;
    REQUIRE(record.Parse(text));
    const grid::Cell past{-7, 16, 67}; // where that map began
    const grid::Cell bath{0, 0, 67};   // where Sam had stood by her sock, the first map's start
    const auto run = [&](bool mirror)
    {
        auto flood = std::make_unique<grid::Flood>(past, 200, 100000);
        const auto passable = record.Passable(nullptr, mirror);
        while (flood->Step(64, passable) == grid::Flood::State::Running)
        {
        }
        REQUIRE(flood->Current() == grid::Flood::State::Complete);
        return flood;
    };
    const auto before = run(false);
    CHECK(before->Expanded() == 281);
    CHECK_FALSE(before->Reached(bath));
    const auto now = run(true);
    CHECK(now->Expanded() == 467);
    CHECK(now->Reached(bath));
}

TEST_CASE("the way between two cells of one flood goes back to where their paths part and on")
{
    const grid::Cell r{0, 0, 0}, a1{1, 0, 0}, a2{2, 0, 0}, b1{0, 1, 0}, b2{0, 2, 0}, b3{1, 3, 0};
    size_t shared = 0;
    CHECK(grid::TreeWay({r, a1, a2}, {r, b1, b2, b3}, &shared) == std::vector<grid::Cell>{a2, a1, r, b1, b2, b3});
    CHECK(shared == 1);
    // Standing on the target's own path: on along it.
    CHECK(grid::TreeWay({r, b1}, {r, b1, b2, b3}) == std::vector<grid::Cell>{b1, b2, b3});
    // Standing at the target: the cell itself.
    CHECK(grid::TreeWay({r, b1, b2}, {r, b1, b2}) == std::vector<grid::Cell>{b2});
    // Beyond it on its path: back to it.
    CHECK(grid::TreeWay({r, b1, b2, b3}, {r, b1}) == std::vector<grid::Cell>{b3, b2, b1});
    // Paths of two floods share nothing.
    CHECK(grid::TreeWay({a1, a2}, {b1, b2}).empty());
}
