// features/WayList: the roads over the reach map, the row rule and the settling of the list,
// on drawn ground and replayed over the ground the mod felt out in play.
#include "Support.hpp"
#include "features/GroundFile.hpp"
#include "features/LookTurn.hpp"
#include "features/Shapes.hpp"
#include "features/WayList.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numbers>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace da;
using ways::Vec;

namespace
{
    // Open ground on level 0 wherever `free` says so.
    template <typename Free>
    grid::Passable OpenGround(Free free)
    {
        return [free](grid::Cell from, grid::Cell& to)
        {
            to.level = from.level;
            return free(to.x, to.y);
        };
    }

    grid::Flood FloodAll(grid::Cell start, const grid::Passable& passable, int expansions = 100000)
    {
        grid::Flood flood(start, 1000, expansions);
        while (flood.Step(100, passable) == grid::Flood::State::Running)
        {
        }
        return flood;
    }

    // Three ways along one road and a thing off to its side, each with a straight road from the
    // start (0, 0) along the x axis: the row rule in miniature.
    struct Row
    {
        std::vector<ways::Entry> entries;
        std::vector<std::vector<Vec>> roads;
        std::vector<ways::Box> boxes;

        size_t Add(bool way, double x, double y)
        {
            ways::Entry e;
            e.way = way;
            e.hasBox = way;
            e.distance = std::hypot(x, y);
            entries.push_back(e);
            roads.push_back({Vec{0.0, 0.0, 0.0}, Vec{x, y, 0.0}});
            boxes.push_back(ways::TurnedBox(Vec{x, y, 64.0}, Vec{60.0, 200.0, 128.0}, 0.0));
            return entries.size() - 1;
        }
        std::vector<int> Judge() const
        {
            return ways::RowRule(entries, [&](size_t way, size_t other) { return ways::RoadEntersBox(boxes[way], roads[other], 42.0, 90.0); });
        }
    };
}

TEST_CASE("the row rule lists only the farthest of a row of ways along one road")
{
    Row row;
    const size_t near = row.Add(true, 500.0, 0.0);
    const size_t middle = row.Add(true, 1000.0, 0.0);
    const size_t far = row.Add(true, 2000.0, 0.0);
    const size_t aside = row.Add(true, 0.0, 800.0);
    const auto on = row.Judge();
    CHECK(on[far] == -1);
    CHECK(on[middle] == static_cast<int>(far));
    CHECK(on[near] == static_cast<int>(far));
    CHECK(on[aside] == -1);
}

TEST_CASE("a road counts only from a target listed: a thing to use always, a way only once kept, a closed-off one never")
{
    SUBCASE("a thing to use nearer than the way still counts")
    {
        Row row;
        const size_t way = row.Add(true, 500.0, 0.0);
        const size_t thing = row.Add(false, 1500.0, 0.0);
        row.entries[thing].distance = 100.0; // said nearer, still its road runs through the way
        CHECK(row.Judge()[way] == static_cast<int>(thing));
    }
    SUBCASE("a farther way not judged yet has no road the player is sure to walk")
    {
        Row row;
        const size_t way = row.Add(true, 500.0, 0.0);
        const size_t far = row.Add(true, 1500.0, 0.0);
        row.entries[far].unjudged = true;
        const auto on = row.Judge();
        CHECK(on[way] == -1);
        CHECK(on[far] == -1);
    }
    SUBCASE("the road to a thing closed off ends short of it and leads nowhere")
    {
        Row row;
        const size_t way = row.Add(true, 500.0, 0.0);
        const size_t thing = row.Add(false, 1500.0, 0.0);
        row.entries[thing].closedOff = true;
        CHECK(row.Judge()[way] == -1);
    }
    SUBCASE("a farther way left out itself leaves nothing out")
    {
        Row row;
        const size_t near = row.Add(true, 500.0, 0.0);
        const size_t middle = row.Add(true, 1000.0, 0.0);
        const size_t far = row.Add(true, 2000.0, 0.0);
        row.entries[far].unreachable = true;
        const auto on = row.Judge();
        CHECK(on[middle] == -1);
        CHECK(on[near] == static_cast<int>(middle));
    }
    SUBCASE("the player's choice is listed whatever lies beyond it, and its road counts")
    {
        Row row;
        const size_t near = row.Add(true, 500.0, 0.0);
        const size_t far = row.Add(true, 2000.0, 0.0);
        row.entries[near].chosen = true;
        auto on = row.Judge();
        CHECK(on[near] == -1);
        row.entries[near].chosen = false;
        row.entries[far].unjudged = true;
        row.entries[far].chosen = true;
        on = row.Judge();
        CHECK(on[near] == static_cast<int>(far));
    }
}

TEST_CASE("a road enters a turned box within the character's width and at her height")
{
    // A box 400 cm along its own x, 50 across, turned 45 degrees, from the floor to 128 cm up.
    const auto box = ways::TurnedBox(Vec{1000.0, 0.0, 64.0}, Vec{400.0, 50.0, 128.0}, 45.0);
    CHECK(box.low.x == doctest::Approx(1000.0 - (400.0 + 50.0) / 2.0 * std::sqrt(0.5)));
    // Along the y axis at x = 1000 the road crosses the box's middle.
    CHECK(ways::RoadEntersBox(box, {Vec{1000.0, -500.0, 0.0}, Vec{1000.0, 500.0, 0.0}}, 42.0, 90.0));
    // Parallel to the box's long side, 60 cm off it: out of reach of a 42 cm character...
    const double off = 25.0 + 60.0;
    const Vec a{1000.0 - 300.0 * std::sqrt(0.5) - off * std::sqrt(0.5), -300.0 * std::sqrt(0.5) + off * std::sqrt(0.5), 0.0};
    const Vec b{1000.0 + 300.0 * std::sqrt(0.5) - off * std::sqrt(0.5), 300.0 * std::sqrt(0.5) + off * std::sqrt(0.5), 0.0};
    CHECK_FALSE(ways::RoadEntersBox(box, {a, b}, 42.0, 90.0));
    // ...and within reach of a wider one.
    CHECK(ways::RoadEntersBox(box, {a, b}, 70.0, 90.0));
    // Walked a storey below the box, the capsule never reaches up into it.
    CHECK_FALSE(ways::RoadEntersBox(box, {Vec{1000.0, -500.0, -400.0}, Vec{1000.0, 500.0, -400.0}}, 42.0, 90.0));
}

TEST_CASE("a road over the reach map is pulled straight over the ground the map reached, and turns round what it did not")
{
    SUBCASE("on open ground the zigzag of equal steps becomes one straight leg")
    {
        const auto flood = FloodAll({0, 0, 0}, OpenGround([](int x, int y) { return std::abs(x) <= 40 && std::abs(y) <= 40; }));
        const auto path = flood.PathTo({30, 12, 0});
        REQUIRE(path.size() == 31);
        const auto corners = ways::StraightOverMap(flood, path, 60);
        REQUIRE(corners.size() == 2);
        CHECK(corners.front() == grid::Cell{0, 0, 0});
        CHECK(corners.back() == grid::Cell{30, 12, 0});
    }
    SUBCASE("a wall in the way keeps a corner at its end")
    {
        // A wall along x = 10 from y = -20 up to y = 10: the way to (20, 0) goes round its end.
        const auto flood =
            FloodAll({0, 0, 0}, OpenGround([](int x, int y) { return std::abs(x) <= 40 && std::abs(y) <= 40 && !(x == 10 && y >= -20 && y <= 10); }));
        const auto path = flood.PathTo({20, 0, 0});
        REQUIRE(!path.empty());
        const auto corners = ways::StraightOverMap(flood, path, 60);
        REQUIRE(corners.size() >= 3);
        for (size_t i = 1; i < corners.size(); ++i)
        {
            // No leg crosses the wall's column below its end.
            const auto& a = corners[i - 1];
            const auto& b = corners[i];
            if ((a.x < 10) == (b.x < 10) || a.x == 10 || b.x == 10) continue;
            const double y = a.y + (b.y - a.y) * (10.0 - a.x) / (b.x - a.x);
            CHECK(y > 10.0);
        }
    }
    SUBCASE("a ramp that passes over itself keeps its climb: the same column four levels up is no straight run")
    {
        // A square spiral of four cells climbing a level a step: (0, 0) on level 0 leads round to
        // (0, 0) on level 4, right over the start.
        const grid::Passable passable = [](grid::Cell from, grid::Cell& to)
        {
            static const grid::Cell ring[] = {{0, 0, 0}, {1, 0, 1}, {1, 1, 2}, {0, 1, 3}, {0, 0, 4}, {1, 0, 5}};
            for (size_t i = 0; i + 1 < std::size(ring); ++i)
            {
                if (from == ring[i] && to.x == ring[i + 1].x && to.y == ring[i + 1].y)
                {
                    to.level = ring[i + 1].level;
                    return true;
                }
            }
            return false;
        };
        const auto flood = FloodAll({0, 0, 0}, passable);
        const auto path = flood.PathTo({0, 0, 4});
        REQUIRE(path.size() == 5);
        const auto corners = ways::StraightOverMap(flood, path, 60);
        CHECK(corners.size() == 5); // every step of the climb stays
    }
    SUBCASE("a step up a level is followed, a drop of two is not")
    {
        // Level 0 for x < 10, level 1 from 10 on: the map climbs the step, and a straight line
        // over it keeps to the levels the map reached.
        const grid::Passable passable = [](grid::Cell from, grid::Cell& to)
        {
            if (std::abs(to.x) > 30 || std::abs(to.y) > 30) return false;
            to.level = to.x >= 10 ? 1 : 0;
            return std::abs(to.level - from.level) <= 1;
        };
        const auto flood = FloodAll({0, 0, 0}, passable);
        const auto path = flood.PathTo({20, 5, 1});
        REQUIRE(!path.empty());
        const auto corners = ways::StraightOverMap(flood, path, 60);
        CHECK(corners.size() == 2);
    }
}

TEST_CASE("the list settles once the map has judged every target, has run out, or has run its while")
{
    ways::Settling s;
    s.Reset();
    CHECK_FALSE(s.Settled());
    CHECK_FALSE(s.Update(0.0, true, 3, 3)); // no map yet: nothing to settle on
    s.Begin(10.0);
    CHECK_FALSE(s.Keeping()); // the first map of a hold keeps no list
    CHECK_FALSE(s.Update(10.5, false, 2, 3));
    CHECK(s.Update(10.6, false, 3, 3)); // every target judged
    CHECK(s.Settled());
    CHECK_FALSE(s.Update(10.7, false, 3, 3)); // it settles once

    s.Begin(20.0);
    CHECK(s.Keeping()); // a later map leaves the list it found
    CHECK_FALSE(s.Update(20.0 + ways::kSettleCap - 0.01, false, 0, 5));
    CHECK(s.Update(20.0 + ways::kSettleCap, false, 0, 5)); // its while run out
    s.Begin(30.0);
    CHECK(s.Update(30.2, true, 0, 5)); // the map ran out of cells

    s.Reset();
    s.Begin(40.0);
    CHECK_FALSE(s.Keeping()); // a new hold keeps nothing
    s.Clear();
    CHECK(s.Settled()); // the navigation mesh gives every road at once
}

namespace
{
    // A way on as the level places it (research/triggers/woods_volumes.tsv), and how the replay
    // finds it: Exploration's GoalOf over the record's grid.
    struct Way
    {
        const char* name;
        Vec middle;
        Vec size;
        double yaw;
        ways::Box box;
        grid::Cell goal;
        double radius = 1.5;
        int levels = 4;
        bool reached = false;
        double distance = 0.0;
        std::vector<Vec> road; // at the feet, from the start
    };

    int LevelOf(double z)
    {
        return static_cast<int>(std::floor(z / 50.0));
    }

    // Exploration's ReachedFrom for a way on whose own box is known: the reached cell of least
    // cost where the character's capsule overlaps that box, turned as the volume stands.
    template <typename Feet>
    bool ReachesVolume(const grid::Flood& flood, const ways::Box& box, grid::Cell goal, int levels, double radius, double cell, Feet feet, grid::Cell& best)
    {
        const int reach = static_cast<int>(std::ceil((std::max(box.high.x - box.low.x, box.high.y - box.low.y) / 2.0 + radius) / cell)) + 1;
        return flood.BestWhere(goal, reach, levels, [&](grid::Cell c) { return ways::CapsuleOverlaps(box, feet(c), radius); }, best);
    }

    // The same over the box's bounds square with the world, as the engine gives them: the rule of
    // the build of 2026-11-01, which took the way at act2_1's lodge path for reached at Matt's feet.
    template <typename Feet>
    bool ReachesBounds(const grid::Flood& flood, const ways::Box& turned, grid::Cell goal, int levels, double radius, double cell, Feet feet, grid::Cell& best)
    {
        const Vec size{turned.high.x - turned.low.x, turned.high.y - turned.low.y, turned.high.z - turned.low.z};
        const auto bounds = ways::TurnedBox(Vec{turned.low.x + size.x / 2.0, turned.low.y + size.y / 2.0, turned.low.z + size.z / 2.0}, size, 0.0);
        return ReachesVolume(flood, bounds, goal, levels, radius, cell, feet, best);
    }

    // One look of the list, as Exploration's ChooseListed takes it: the ways the map has judged
    // wait for the list to settle, the row rule judges at the look it settles, once a second
    // after, and at once when the map has judged another target.
    struct Listing
    {
        double at = 0.0;
        std::set<std::string> listed;
        std::vector<int> onRoadOf;
    };
}

TEST_CASE("the woods' start: the way at the duck-under alone is listed, and only once the map has settled")
{
    // The ground the reach map felt out from where the game hands Beth over at the start of the
    // chase, (16995, -15580, 3396), on 2026-10-28 (research/dumps/ground-20261028-152307.tsv, cut
    // by research/offline_probe/trim_record.py up to where the walk began): 15084 cells along the
    // path. In play the list said four ways the moment the map had run four seconds and left three
    // of them out a moment later, the third only once the chosen way's road was pulled straight
    // (39 m, then 47, then 45 to the next press). The roads here are pulled straight over the map
    // for every target alike, and the list settles before any way is listed.
    std::ifstream in(std::filesystem::path(DA_REPO_ROOT) / "tests" / "fixtures" / "ground-woods-start.tsv");
    REQUIRE(in.good());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ground::Record record;
    REQUIRE(record.Parse(text));
    REQUIRE(!record.anchors.empty());
    const auto& anchor = record.anchors.front();
    const grid::Cell start{0, 0, LevelOf(anchor.z - anchor.half)};
    const auto feet = [&](grid::Cell c)
    {
        const auto floor = record.floors.find({c.x, c.y, c.level});
        return Vec{anchor.x + (c.x + 0.5) * anchor.cell, anchor.y + (c.y + 0.5) * anchor.cell,
                   floor != record.floors.end() ? floor->second : c.level * anchor.level + anchor.level / 2.0};
    };

    std::vector<Way> woods = {
        {"Intro C_13, the bird scare", {16277, -15322, 3409}, {51, 512, 128}, -5.9},
        {"TreeDuckUnder C_3, the game's walk to the tree", {14643, -12912, 3426}, {256, 51, 128}, 43.1},
        {"TreeDuckUnder C_1, the duck-under", {14483, -12785, 3395}, {228, 26, 128}, 65.9},
        {"TreeDuckUnder C_6, past the tree", {14034, -12556, 3394}, {64, 128, 128}, -26.7},
        {"Shack C_8", {11331, -16538, 3374}, {26, 256, 128}, 36.8},
        {"Shack C_5", {9936, -16855, 3418}, {343, 18, 253}, 53.6},
        {"Shack C_0", {9609, -15511, 3419}, {192, 26, 128}, 0.0},
        {"Shack C_6", {9597, -15786, 3418}, {192, 26, 128}, 0.0},
        {"Shack C_3", {9592, -16042, 3416}, {192, 26, 128}, 0.0},
        {"Shack C_4", {9610, -14863, 3433}, {192, 61, 128}, 0.0},
        {"Intro C_1, behind", {24359, -17465, 3395}, {620, 855, 256}, -29.6},
    };
    for (auto& w : woods)
    {
        w.box = ways::TurnedBox(w.middle, w.size, w.yaw);
        w.goal = grid::Cell{static_cast<int>(std::floor((w.middle.x - anchor.x) / anchor.cell)),
                            static_cast<int>(std::floor((w.middle.y - anchor.y) / anchor.cell)), LevelOf(w.middle.z)};
        w.radius = ways::VolumeGoalRadius(w.box, anchor.radius, anchor.cell);
        w.levels = std::max(4, (LevelOf(w.box.high.z) - LevelOf(w.box.low.z)) / 2 + 2);
        w.distance = std::hypot(w.middle.x - anchor.x, w.middle.y - anchor.y);
    }

    // The map at the mod's pace: in play it opened 8298 cells in its first 4.2 s while the list
    // settled (10 ms of each frame at 30 frames a second), and a third as many after (3 ms). The
    // record ends where the walk began, so the replay stops short of it: a map cut short proves
    // nothing about what it has not reached.
    grid::Flood flood(start, 550, 15000);
    const auto passable = record.Passable();
    ways::Settling settle;
    settle.Reset();
    settle.Begin(0.0);
    std::vector<Listing> looks;
    ways::Lister lister;
    std::vector<ways::Entry> entries(woods.size());
    for (auto& e : entries)
    {
        e.way = true;
        e.hasBox = true;
    }
    std::optional<double> settledAt;
    for (int frame = 1; frame <= 600; ++frame)
    {
        const double now = frame / 30.0;
        const int target = flood.Expanded() + (settle.Settled() ? 20 : 66);
        while (flood.Current() == grid::Flood::State::Running && flood.Expanded() < target)
            flood.Step(4, passable);
        const bool done = flood.Current() != grid::Flood::State::Running;
        // JudgeByReach: a way whose goal the map has reached has its road, and the list is judged
        // again at the next look.
        for (auto& w : woods)
        {
            grid::Cell best;
            if (w.reached || !ReachesVolume(flood, w.box, w.goal, w.levels, anchor.radius, anchor.cell, feet, best)) continue;
            w.reached = true;
            w.distance = std::max(w.distance, flood.Cost(best) * anchor.cell);
            w.road = {feet(start)};
            for (const auto& c : ways::StraightOverMap(flood, flood.PathTo(best), 60))
                w.road.push_back(feet(c));
            lister.Again();
        }
        // ChooseListed.
        const size_t judged = static_cast<size_t>(std::count_if(woods.begin(), woods.end(), [](const Way& w) { return w.reached; }));
        const bool settlesNow = settle.Update(now, done, judged, woods.size());
        if (settlesNow) settledAt = now;
        for (size_t i = 0; i < woods.size(); ++i)
        {
            entries[i].distance = woods[i].distance;
            entries[i].known = woods[i].reached;
        }
        const bool due = lister.Due(settle, settlesNow, now);
        lister.Judge(entries, settle, due, true, true, now, [&](size_t way, size_t other)
                     { return woods[other].road.size() >= 2 && ways::RoadEntersBox(woods[way].box, woods[other].road, 42.0, 90.0); });
        Listing look;
        look.at = now;
        for (size_t i = 0; i < woods.size(); ++i)
        {
            look.onRoadOf.push_back(entries[i].onRoadOf);
            if (!entries[i].unjudged && entries[i].onRoadOf < 0) look.listed.insert(woods[i].name);
        }
        looks.push_back(look);
        if (done) break;
    }

    // The map reaches the four ways of the path where the character's capsule first overlaps
    // each (ways::CapsuleOverlaps), a little short of the costs the game logged in play, when
    // the map's cell had to stand within the volume's own half width (771, 3998, 4236, 4839 cm).
    CHECK(woods[0].distance == doctest::Approx(763.0).epsilon(0.01));
    CHECK(woods[1].distance == doctest::Approx(4026.0).epsilon(0.01));
    CHECK(woods[2].distance == doctest::Approx(4216.0).epsilon(0.01));
    CHECK(woods[3].distance == doctest::Approx(4779.0).epsilon(0.01));
    for (size_t i = 4; i < woods.size(); ++i)
    {
        CAPTURE(woods[i].name);
        CHECK_FALSE(woods[i].reached); // past the duck-under, or far behind
    }
    // Pulled straight over the map, the road to the way past the tree ends where her capsule first
    // overlaps it, 0.9 m short of the one the game's own straightener swept for the walk to its
    // middle in play (4514 cm), where the map's cells, every third, had said 47 m a moment before:
    // one distance before and after a choice.
    double length = 0.0;
    for (size_t i = 1; i < woods[3].road.size(); ++i)
        length += std::hypot(woods[3].road[i].x - woods[3].road[i - 1].x, woods[3].road[i].y - woods[3].road[i - 1].y);
    CHECK(length == doctest::Approx(4422.0).epsilon(0.015));
    // The ways beyond the start's reach keep the list from settling until its while has run.
    REQUIRE(settledAt.has_value());
    CHECK(*settledAt <= ways::kSettleCap + 1.0 / 30.0);
    CHECK(*settledAt >= ways::kSettleCap - 1.0 / 30.0);
    // Nothing is listed before it settles; from then on the way past the tree alone, the other
    // three lying on its road, and it stays so to the end of the record.
    const std::string expected = "TreeDuckUnder C_6, past the tree";
    for (const auto& look : looks)
    {
        CAPTURE(look.at);
        std::string listed;
        for (const auto& name : look.listed)
            listed += (listed.empty() ? "" : "; ") + name;
        if (look.at < *settledAt)
            CHECK(listed.empty());
        else
            CHECK(listed == expected);
    }
    const auto& last = looks.back().onRoadOf;
    CHECK(last[0] == 3);
    CHECK(last[1] == 3);
    CHECK(last[2] == 3);
    CHECK(last[3] == -1);
}

TEST_CASE("a way the game turns on beside the character waits for the map, and a door's road leaves it out; the list found stays meanwhile")
{
    // As at the lodge's totem on 2026-10-28: the hold resumed after the totem's vision, the game
    // turned on a trigger 3 m away, and the list said it as news before the map, which had begun
    // a moment earlier, reached the door back into the lodge, whose road runs through it. Drawn
    // here as a corridor 5 cells wide along x with a side passage up at x = 60: the new way spans
    // the corridor near the start, the door stands at its far end, and a way the list kept from
    // before waits up the side passage.
    const auto open = [](int x, int y) { return (x >= 0 && x <= 120 && y >= 0 && y <= 4) || (x >= 58 && x <= 62 && y >= 0 && y <= 40); };
    const grid::Cell start{3, 2, 0};
    const auto feet = [](grid::Cell c) { return Vec{(c.x + 0.5) * 20.0, (c.y + 0.5) * 20.0, 0.0}; };
    struct Target
    {
        const char* name;
        bool way;
        grid::Cell goal;
        ways::Box box;
        bool passed;     // judged by the list that stood before
        bool keptListed; // that list's verdict: listed
        bool reached = false;
        double distance = 0.0;
        std::vector<Vec> road;
    };
    std::vector<Target> targets = {
        {"the new way beside her", true, {9, 2, 0}, ways::TurnedBox(Vec{190.0, 50.0, 64.0}, Vec{40.0, 120.0, 128.0}, 0.0), false, false},
        {"the door at the far end", false, {118, 2, 0}, {}, true, true},
        {"the way kept up the passage", true, {60, 38, 0}, ways::TurnedBox(Vec{1210.0, 770.0, 64.0}, Vec{100.0, 40.0, 128.0}, 0.0), true, true},
        {"a thing beyond a door, judged once the map is done", false, {200, 2, 0}, {}, true, true},
    };
    grid::Flood flood(start, 550, 80000);
    const auto passable = OpenGround(open);
    ways::Settling settle;
    settle.Reset();
    settle.Begin(-30.0); // the map of the hold before the vision
    REQUIRE(settle.Update(-29.0, true, 4, 4));
    settle.Begin(0.0); // the hold resumes: a new map, the list found stays
    // The verdicts of the list that stood: the door, the thing beyond a door and the way up the
    // passage judged and listed; the new way never judged.
    ways::Lister lister;
    std::vector<ways::Entry> entries(targets.size());
    for (size_t i = 0; i < targets.size(); ++i)
    {
        entries[i].way = targets[i].way;
        entries[i].hasBox = targets[i].way;
        entries[i].passed = targets[i].passed;
        entries[i].unjudged = targets[i].way && !targets[i].keptListed;
    }
    bool everListed = false;
    bool keptAlways = true;
    std::optional<double> settledAt;
    for (int frame = 1; frame <= 120; ++frame)
    {
        const double now = frame / 30.0;
        const int target = flood.Expanded() + 30;
        while (flood.Current() == grid::Flood::State::Running && flood.Expanded() < target)
            flood.Step(4, passable);
        const bool done = flood.Current() != grid::Flood::State::Running;
        for (auto& t : targets)
        {
            grid::Cell best;
            if (t.reached || !flood.BestIn(t.goal, 1.5, 4, best)) continue;
            t.reached = true;
            t.distance = flood.Cost(best) * 20.0;
            t.road = {feet(start)};
            for (const auto& c : ways::StraightOverMap(flood, flood.PathTo(best), 60))
                t.road.push_back(feet(c));
            lister.Again();
        }
        // Judged by this map: reached, or, once it is done, found closed off. What the list that
        // stood found counts for nothing here (the lodge's trigger was said for the old roads).
        const size_t judged = static_cast<size_t>(std::count_if(targets.begin(), targets.end(), [&](const Target& t) { return t.reached || done; }));
        const bool settlesNow = settle.Update(now, done, judged, targets.size());
        if (settlesNow) settledAt = now;
        for (size_t i = 0; i < targets.size(); ++i)
        {
            entries[i].distance = targets[i].distance;
            entries[i].known = targets[i].reached;
            entries[i].closedOff = done && !targets[i].reached && !targets[i].way;
        }
        const bool due = lister.Due(settle, settlesNow, now);
        lister.Judge(entries, settle, due, true, true, now, [&](size_t way, size_t other)
                     { return targets[other].road.size() >= 2 && ways::RoadEntersBox(targets[way].box, targets[other].road, 42.0, 90.0); });
        everListed = everListed || (!entries[0].unjudged && entries[0].onRoadOf < 0);
        keptAlways = keptAlways && !entries[2].unjudged && entries[2].onRoadOf < 0;
        if (settle.Settled() && done && now - *settledAt > 1.0) break;
    }
    REQUIRE(settledAt.has_value());
    CHECK(targets[0].reached);
    CHECK(targets[1].reached);
    CHECK_FALSE(everListed);              // never listed, so never said as news
    CHECK(entries[0].onRoadOf == 1);      // on the road to the door
    CHECK(keptAlways);                    // the way the list kept never went and came back
    CHECK(*settledAt < ways::kSettleCap); // the map ran out first
}

TEST_CASE("a way the list said stays listed while a map begun as she walked on has not reached it, and a new hold trusts no old verdict")
{
    // A corridor 400 cells long, the way at its far end, and a map slow enough not to reach it
    // before the list settles on its while.
    const auto open = [](int x, int y) { return x >= 0 && x <= 400 && y >= 0 && y <= 4; };
    const auto passable = OpenGround(open);
    const auto run = [&](bool resumed)
    {
        ways::Settling settle;
        settle.Reset();
        if (resumed)
        {
            settle.Begin(-10.0);
            REQUIRE(settle.Update(-9.0, true, 1, 1));
        }
        settle.Begin(0.0);
        CHECK(settle.Kept() == resumed);
        ways::Lister lister;
        std::vector<ways::Entry> entries(1);
        entries[0].way = true;
        entries[0].hasBox = true;
        entries[0].distance = 7900.0;
        entries[0].passed = resumed;
        entries[0].unjudged = !resumed;
        entries[0].earlier = true; // the map before found it
        grid::Flood flood({2, 2, 0}, 1000, 100000);
        std::vector<bool> listed;
        bool reachedBeforeSettling = false;
        for (int frame = 1; frame <= 150; ++frame)
        {
            const double now = frame / 30.0;
            const int target = flood.Expanded() + 10;
            while (flood.Current() == grid::Flood::State::Running && flood.Expanded() < target)
                flood.Step(4, passable);
            grid::Cell best;
            entries[0].known = flood.BestIn({398, 2, 0}, 1.5, 4, best);
            reachedBeforeSettling = reachedBeforeSettling || (entries[0].known && !settle.Settled());
            const bool settlesNow = settle.Update(now, flood.Current() != grid::Flood::State::Running, entries[0].known ? 1 : 0, 1);
            lister.Judge(entries, settle, lister.Due(settle, settlesNow, now), true, true, now, [](size_t, size_t) { return false; });
            listed.push_back(!entries[0].unjudged && entries[0].onRoadOf < 0);
        }
        CHECK(settle.Settled());
        CHECK_FALSE(reachedBeforeSettling);
        return listed;
    };
    const auto resumed = run(true);
    CHECK(std::all_of(resumed.begin(), resumed.end(), [](bool b) { return b; }));
    const auto fresh = run(false);
    CHECK(std::none_of(fresh.begin(), fresh.end(), [](bool b) { return b; }));
}

TEST_CASE("with neither a map nor the navigation mesh to judge the roads, a list that stands stays as it is")
{
    // As when the character steps off the navigation mesh, or a search lays the grid afresh: no
    // way she has heard goes for want of a judge, while a fresh hold still lists none.
    std::vector<ways::Entry> entries(2);
    entries[0].way = true;
    entries[0].hasBox = true;
    entries[0].known = false; // the mesh judged it; nothing judges it this look
    entries[0].passed = true;
    entries[0].unjudged = false;
    entries[1].way = true;
    entries[1].hasBox = true;
    entries[1].known = false;
    entries[1].passed = true;
    entries[1].unjudged = false;
    entries[1].onRoadOf = 0;
    ways::Settling settle;
    settle.Reset();
    settle.Clear(); // the mesh gave the roads
    ways::Lister lister;
    const auto never = [](size_t, size_t) { return false; };
    lister.Judge(entries, settle, lister.Due(settle, false, 0.0), false, true, 0.0, never);
    CHECK_FALSE(entries[0].unjudged);
    CHECK(entries[1].onRoadOf == 0);
    // With a judge again the list follows it.
    lister.Judge(entries, settle, lister.Due(settle, false, 0.0), true, true, 0.0, never);
    CHECK(entries[0].unjudged);
    // A fresh hold before its first map lists no way, judge or none.
    ways::Settling fresh;
    fresh.Reset();
    entries[0].unjudged = false;
    lister.Judge(entries, fresh, false, false, true, 1.0, never);
    CHECK(entries[0].unjudged);
}

TEST_CASE("a door the character only tries leaves what lies behind it closed off, 2026-11-01")
{
    // Chapter one's station: the front door Sam rattles binds her to the locked door's
    // animation; counted as a door she could open, it kept the hotel poster and the security
    // monitor behind the station's front from being found closed off, and their roads, felt out
    // as far as the ground went and then straight on, could hide the ways on beside them.
    CHECK(ways::OnlyTried({L"ABP_Door_Locked_Right_Female_C"}));
    CHECK(ways::OnlyTried({L"ABP_Door_Locked_Left_Female_C", L"ABP_Door_Locked_Left_Male_C"}));
    // The doors that open.
    CHECK_FALSE(ways::OnlyTried({L"ABP_Door_Push_Right_Open_Female_C"}));
    CHECK_FALSE(ways::OnlyTried({L"ABP_Door_Multi_Pull_Character_Female_C", L"ABP_Door_Multi_Pull_Character_Male_C"}));
    // Locked for one body and not the other: it opens for someone.
    CHECK_FALSE(ways::OnlyTried({L"ABP_Door_Locked_Right_Female_C", L"ABP_Door_Pull_Left_Open_Male_C"}));
    // No animation named: not known locked.
    CHECK_FALSE(ways::OnlyTried({}));
}

TEST_CASE("the cable car's doorway is walked into from the platform's edge, 2026-11-01")
{
    // Chapter one, once the cable car had come down: the reach map begun at Chris's hand-over at
    // (24627, -42066) at 22:48:04 (research/dumps/ground-20261101-223302.tsv, cut by
    // research/offline_probe/trim_record.py --start -46 62 -344) ran out at 817 cells, the
    // platform's ground, and found the car's doorway (BP_CharacterTriggerVolume_EnterCableCar, 128
    // by 51 cm, turned -5.9 degrees, the volume that starts the boarding) closed off, 49 cm from
    // its middle. The one way on was never offered, and the player had nothing to walk to.
    std::ifstream in(std::filesystem::path(DA_REPO_ROOT) / "tests" / "fixtures" / "ground-cablecar-door.tsv");
    REQUIRE(in.good());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ground::Record record;
    REQUIRE(record.Parse(text));
    REQUIRE(!record.anchors.empty());
    const auto& anchor = record.anchors.front();
    const auto feet = [&](grid::Cell c)
    {
        const auto floor = record.floors.find({c.x, c.y, c.level});
        return Vec{anchor.x + (c.x + 0.5) * anchor.cell, anchor.y + (c.y + 0.5) * anchor.cell,
                   floor != record.floors.end() ? floor->second : c.level * anchor.level + anchor.level / 2.0};
    };
    const grid::Cell start{-46, 62, -344};
    grid::Flood flood(start, 550, 15000);
    const auto passable = record.Passable();
    while (flood.Current() == grid::Flood::State::Running)
        flood.Step(64, passable);
    // The platform's ground, every cell of it within reach, as in play.
    CHECK(flood.Expanded() >= 800);
    CHECK(flood.Expanded() <= 830);
    const Vec middle{24574.14, -42374.56, -17065.73};
    const auto door = ways::TurnedBox(middle, {128.0, 51.2, 256.0}, -5.93);
    const grid::Cell goal{static_cast<int>(std::floor((middle.x - anchor.x) / anchor.cell)), static_cast<int>(std::floor((middle.y - anchor.y) / anchor.cell)),
                          LevelOf(middle.z)};
    const int levels = std::max(4, (LevelOf(door.high.z) - LevelOf(door.low.z)) / 2 + 2);
    grid::Cell best;
    // A cell within the doorway's own half width of its middle, as the map asked in play: none.
    const double own = std::max(1.5, std::min(door.high.x - door.low.x, door.high.y - door.low.y) / 2.0 / anchor.cell);
    CHECK_FALSE(flood.BestIn(goal, own, levels, best));
    // A cell where her capsule overlaps it: there is one, along the car's threshold.
    REQUIRE(ReachesVolume(flood, door, goal, levels, anchor.radius, anchor.cell, feet, best));
    const Vec at = feet(best);
    CHECK(std::hypot(at.x - middle.x, at.y - middle.y) < 100.0);
    CHECK(ways::CapsuleOverlaps(door, at, anchor.radius + 15.0)); // the turned box itself, give or take a cell's corner
}

TEST_CASE("a capsule overlaps a volume when it reaches over the volume's edge, turned or not")
{
    const auto door = ways::TurnedBox({0.0, 0.0, 0.0}, {128.0, 51.2, 256.0}, 0.0);
    // Across its thin side: half its depth and the capsule's radius.
    CHECK(ways::CapsuleOverlaps(door, {0.0, 25.6 + 42.0 - 0.1, 0.0}, 42.0));
    CHECK_FALSE(ways::CapsuleOverlaps(door, {0.0, 25.6 + 42.0 + 0.1, 0.0}, 42.0));
    // Along its long side.
    CHECK(ways::CapsuleOverlaps(door, {64.0 + 41.9, 0.0, 0.0}, 42.0));
    CHECK_FALSE(ways::CapsuleOverlaps(door, {64.0 + 42.1, 0.0, 0.0}, 42.0));
    // Past a corner: the distance to the corner.
    CHECK_FALSE(ways::CapsuleOverlaps(door, {64.0 + 30.0, 25.6 + 30.0, 0.0}, 42.0));
    // Turned a quarter, its thin side faces the other way.
    const auto turned = ways::TurnedBox({0.0, 0.0, 0.0}, {128.0, 51.2, 256.0}, 90.0);
    CHECK(ways::CapsuleOverlaps(turned, {25.6 + 41.9, 0.0, 0.0}, 42.0));
    CHECK_FALSE(ways::CapsuleOverlaps(turned, {25.6 + 42.1, 0.0, 0.0}, 42.0));
    // The circle for the searches reaches as far as the overlap does across the thin side, and
    // lies inside it all round.
    CHECK(ways::VolumeGoalRadius(door, 42.0, 20.0) == doctest::Approx((25.6 + 42.0) / 20.0));
    const double circle = ways::VolumeGoalRadius(door, 42.0, 20.0) * 20.0;
    for (int degrees = 0; degrees < 360; degrees += 15)
    {
        const double r = degrees * 3.14159265358979 / 180.0;
        CHECK(ways::CapsuleOverlaps(door, {std::cos(r) * (circle - 0.1), std::sin(r) * (circle - 0.1), 0.0}, 42.0));
    }
    CHECK(ways::VolumeGoalRadius(ways::TurnedBox({0.0, 0.0, 0.0}, {10.0, 10.0, 256.0}, 0.0), 0.0, 20.0) == doctest::Approx(1.5));
}

TEST_CASE("a way on set aslant is reached at its own box, not at its bounds, 2026-11-02")
{
    // act2_1, Matt and Emily on the path to the lodge: the one way on, T_LoveTriangle_MikeScare_Intro,
    // is a wall of a mesh (SM_T_LoveTriangle_MikeScare_Intro: 138 by 1040 by 466 cm) turned -24.25
    // degrees across the path, 3 m ahead of where the game handed Matt over at (-6967, -19805).
    // Judged by its bounds square with the world (5.5 by 10 m), the map took it for reached 60 cm
    // from him; every walk circled there and gave up (research/dumps/ground-20261101-234442.tsv
    // from its line 221416, cut by research/offline_probe/trim_record.py to 3000 cells).
    std::ifstream in(std::filesystem::path(DA_REPO_ROOT) / "tests" / "fixtures" / "ground-lovetriangle-gate.tsv");
    REQUIRE(in.good());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ground::Record record;
    REQUIRE(record.Parse(text));
    REQUIRE(!record.anchors.empty());
    const auto& anchor = record.anchors.front();
    const auto feet = [&](grid::Cell c)
    {
        const auto floor = record.floors.find({c.x, c.y, c.level});
        return Vec{anchor.x + (c.x + 0.5) * anchor.cell, anchor.y + (c.y + 0.5) * anchor.cell,
                   floor != record.floors.end() ? floor->second : c.level * anchor.level + anchor.level / 2.0};
    };
    const grid::Cell start{0, 0, LevelOf(anchor.z - anchor.half)};
    grid::Flood flood(start, 550, 15000);
    const auto passable = record.Passable();
    while (flood.Current() == grid::Flood::State::Running)
        flood.Step(64, passable);
    const Vec middle{-6581.34, -19873.24, 1358.87};
    const auto wall = ways::TurnedBox(middle, {138.09, 1040.41, 465.85}, -24.25);
    const grid::Cell goal{static_cast<int>(std::floor((middle.x - anchor.x) / anchor.cell)), static_cast<int>(std::floor((middle.y - anchor.y) / anchor.cell)),
                          LevelOf(middle.z)};
    const int levels = std::max(4, (LevelOf(wall.high.z) - LevelOf(wall.low.z)) / 2 + 2);
    // By the bounds: a cell a step from where he stood, nowhere near the wall.
    grid::Cell near;
    REQUIRE(ReachesBounds(flood, wall, goal, levels, anchor.radius, anchor.cell, feet, near));
    CHECK(flood.Cost(near) * anchor.cell < 100.0);
    CHECK_FALSE(ways::CapsuleOverlaps(wall, feet(near), anchor.radius));
    // By its own box: the wall, some 3 m on, where his capsule touches it.
    grid::Cell at;
    REQUIRE(ReachesVolume(flood, wall, goal, levels, anchor.radius, anchor.cell, feet, at));
    CHECK(flood.Cost(at) * anchor.cell > 250.0);
    CHECK(flood.Cost(at) * anchor.cell < 450.0);
    CHECK(ways::CapsuleOverlaps(wall, feet(at), anchor.radius));
}

TEST_CASE("a way on is reached where the character touches its collision, not the box around it, 2026-11-02")
{
    // act2_1, Matt past the gate: the next way on, T_lovetriangle_mike_choice, is one convex piece
    // set aslant in its mesh, a slab 2.25 m thick, 15 m long and 10 m high. The box around it (the
    // mesh's bounds, unturned) held the spot where he circled 4.5 m short of the slab, and every
    // walk gave up there (s38: the ground of research/dumps/ground-20261102-005922.tsv flooded
    // from his cell at (-5113, -20008), cut by research/offline_probe/trim_record.py to 10800 cells).
    std::ifstream in(std::filesystem::path(DA_REPO_ROOT) / "tests" / "fixtures" / "ground-lovetriangle-slab.tsv");
    REQUIRE(in.good());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ground::Record record;
    REQUIRE(record.Parse(text));
    REQUIRE(!record.anchors.empty());
    const auto& anchor = record.anchors.front();
    // The cell's floor, and the character's middle standing on it.
    const auto feet = [&](grid::Cell c)
    {
        const auto floor = record.floors.find({c.x, c.y, c.level});
        return Vec{anchor.x + (c.x + 0.5) * anchor.cell, anchor.y + (c.y + 0.5) * anchor.cell,
                   floor != record.floors.end() ? floor->second : c.level * anchor.level + anchor.level / 2.0};
    };
    const auto middle = [&](grid::Cell c)
    {
        const Vec f = feet(c);
        return Vec{f.x, f.y, f.z + anchor.half};
    };
    const grid::Cell start{87, -10, 23};
    grid::Flood flood(start, 550, 15000);
    const auto passable = record.Passable();
    while (flood.Current() == grid::Flood::State::Running)
        flood.Step(64, passable);
    // The slab as the game's asset gives it, placed where the level puts the volume's mesh.
    const Vec at{-4381.890538116746, -19627.325593564325, 1428.3767401785706};
    shapes::Shape slab;
    slab.hulls.push_back({{{at.x - 452.9, at.y + 596.1, at.z + 500.0},
                           {at.x - 255.5, at.y + 703.8, at.z + 500.0},
                           {at.x + 255.5, at.y - 703.8, at.z + 500.0},
                           {at.x + 452.9, at.y - 596.1, at.z + 500.0},
                           {at.x + 255.5, at.y - 703.8, at.z - 500.0},
                           {at.x + 452.9, at.y - 596.1, at.z - 500.0},
                           {at.x - 452.9, at.y + 596.1, at.z - 500.0},
                           {at.x - 255.5, at.y + 703.8, at.z - 500.0}}});
    Vec low, high;
    slab.Bounds(low, high);
    const grid::Cell goal{static_cast<int>(std::floor((at.x - anchor.x) / anchor.cell)), static_cast<int>(std::floor((at.y - anchor.y) / anchor.cell)),
                          LevelOf(at.z)};
    const int reach = static_cast<int>(std::ceil((std::hypot(high.x - low.x, high.y - low.y) / 2.0 + anchor.radius) / anchor.cell)) + 1;
    // By the box around it, as the mod judged it before: reached at the box's edge, nowhere near
    // the slab.
    const auto box = ways::TurnedBox(at, {high.x - low.x, high.y - low.y, high.z - low.z}, 0.0);
    grid::Cell edge;
    REQUIRE(ReachesVolume(flood, box, goal, -1, anchor.radius, anchor.cell, feet, edge));
    CHECK_FALSE(shapes::CapsuleTouches(slab, middle(edge), anchor.radius, anchor.half));
    const Vec e = middle(edge);
    CHECK(shapes::Distance(slab, Vec{e.x, e.y, e.z - (anchor.half - anchor.radius)}, Vec{e.x, e.y, e.z + (anchor.half - anchor.radius)}) > 300.0);
    // By the slab itself: reached where his capsule touches it, farther on.
    grid::Cell touching;
    REQUIRE(flood.BestWhere(goal, reach, -1, [&](grid::Cell c) { return shapes::CapsuleTouches(slab, middle(c), anchor.radius, anchor.half); }, touching));
    CHECK(shapes::CapsuleTouches(slab, middle(touching), anchor.radius, anchor.half));
    // The edge 2.6 m from him, the slab 7.1 m on, at its face by (-4568, -19594).
    CHECK(flood.Cost(edge) * anchor.cell == doctest::Approx(257.0).epsilon(0.05));
    CHECK(flood.Cost(touching) * anchor.cell == doctest::Approx(714.0).epsilon(0.05));
    CHECK(std::hypot(middle(touching).x + 4568.0, middle(touching).y + 19594.0) < 60.0);
    // The search that sets out for it arrives there too, and only where he touches it.
    grid::Search search(start, goal, reach, -1, 550, 200000, 0.0,
                        [&](grid::Cell c) { return shapes::CapsuleTouches(slab, middle(c), anchor.radius, anchor.half); });
    while (search.Current() == grid::Search::State::Running)
        search.Step(256, passable);
    REQUIRE(search.Current() == grid::Search::State::Found);
    const auto path = search.Path();
    REQUIRE(!path.empty());
    CHECK(shapes::CapsuleTouches(slab, middle(path.back()), anchor.radius, anchor.half));
}

TEST_CASE("a use location for the other body is not the character's, 2026-11-02")
{
    // act2_11's lodge: I_hannah_and_beth_beach_photo and its _female twin stand at one spot, each
    // requiring an animation for the character (bRequireValidCharacterABP); Chris was offered both.
    ways::UseRule male;
    male.requiresAnimation = true;
    male.animated = true;
    male.anyone = true;
    ways::UseRule female = male;
    female.animated = false;
    CHECK(ways::UsableBy(male));
    CHECK_FALSE(ways::UsableBy(female));
    // A use location that asks for no animation goes by who it is for.
    ways::UseRule named;
    named.hers = true;
    CHECK(ways::UsableBy(named));
    named.hers = false;
    CHECK_FALSE(ways::UsableBy(named));
}

TEST_CASE("the road walked back from the danger totem is followed on the walkway, not on the platform over it, 2026-11-04")
{
    // Session 43 (research/sessions/s43.log, 02:56:37): the road felt out from the totem's platform
    // to the security gate, its corners at the floor's height as the log gives them, and Mike at the
    // foot of the stairs on the walkway beneath the platform the road began on.
    using ways::Vec;
    const std::vector<Vec> road = {
        {5733, -8848, 2955}, {5700, -8877, 2953}, {5540, -9097, 2953},  {5080, -9257, 2953},  {4900, -9297, 2953}, {4840, -9217, 2953},
        {4880, -9137, 2954}, {5580, -8937, 2560}, {6340, -8617, 2567},  {7400, -7577, 2406},  {7920, -7497, 2320}, {9120, -7517, 2307},
        {9620, -7557, 2373}, {9980, -7397, 2388}, {10060, -7317, 2385}, {10480, -6697, 2310},
    };
    // 02:56:59, at (5711, -8847) on the floor 2562: the walk was turned back 303 cm toward the
    // platform's corner (5540, -9097), 391 cm up. Over the ground alone the platform's first
    // stretch lies nearest.
    const Vec landing{5711, -8847, 2562};
    CHECK(ways::StretchOn(road, road.size(), landing, 150.0, 1e9) == 1);
    // At her own height the stretch she is on is the walkway's, on toward the gate.
    CHECK(ways::StretchOn(road, road.size(), landing, 150.0, ways::kStretchRise) == 8);
    CHECK(ways::StretchOn(road, road.size(), Vec{5603, -8878, 2562}, 150.0, ways::kStretchRise) == 8);
    // Halfway down the stairs she is on the stairs' stretch, whose height falls with them.
    CHECK(ways::StretchOn(road, road.size(), Vec{5300, -9017, 2718}, 150.0, ways::kStretchRise) == 7);
    // On the platform at the start, the first corner itself.
    CHECK(ways::StretchOn(road, road.size(), Vec{5733, -8848, 2955}, 150.0, ways::kStretchRise) == 0);
    // Off the road, or past the corners the walk may use: none.
    CHECK(ways::StretchOn(road, road.size(), Vec{7000, -9500, 2500}, 150.0, ways::kStretchRise) == -1);
    CHECK(ways::StretchOn(road, 7, landing, 150.0, ways::kStretchRise) == -1);
}

TEST_CASE("a guidance passage links the ground at one end to the cell of the other, only where it was swept free")
{
    using ways::Vec;
    ways::Passage door;
    door.name = "BP_Doorway_Shed";
    door.a = Vec{8736.9, -5910.8, 2317.7};
    door.b = Vec{8849.5, -5975.8, 2317.7};
    door.free = true;
    const std::vector<ways::Passage> passages{door};
    int asked = 0;
    const auto cellAt = [&](const Vec& end, grid::Cell& cell, Vec& middle)
    {
        ++asked;
        cell = grid::Cell{static_cast<int>(std::floor(end.x / 20.0)), static_cast<int>(std::floor(end.y / 20.0)), 46};
        middle = Vec{(cell.x + 0.5) * 20.0, (cell.y + 0.5) * 20.0, end.z};
        return true;
    };
    std::vector<grid::Link> out;
    // A cell beside the ramp's end links to the shed's end, at the cost of the way between them.
    ways::PassageLinks(passages, Vec{8840.1, -5976.7, 2317.7}, 20.0, cellAt, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].to == grid::Cell{436, -296, 46});
    CHECK(out[0].cost == doctest::Approx(std::hypot(8730.0 - 8840.1, -5910.0 + 5976.7) / 20.0));
    // And back from the shed's.
    out.clear();
    ways::PassageLinks(passages, Vec{8740.0, -5915.0, 2317.7}, 20.0, cellAt, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].to == grid::Cell{442, -299, 46});
    // Too far from an end over the ground, or off its floor: none.
    out.clear();
    ways::PassageLinks(passages, Vec{8800.0, -5940.0, 2317.7}, 20.0, cellAt, out);
    ways::PassageLinks(passages, Vec{8840.1, -5976.7, 2400.0}, 20.0, cellAt, out);
    CHECK(out.empty());
    // A passage the capsule did not get along (a door's leaf across it, a wall) links nothing.
    door.free = false;
    asked = 0;
    ways::PassageLinks({door}, Vec{8840.1, -5976.7, 2317.7}, 20.0, cellAt, out);
    CHECK(out.empty());
    CHECK(asked == 0);
}

TEST_CASE("a use location the game's list kept after she left it is no offer, 2026-11-05")
{
    // Session 44 (research/sessions/s44.log): from 20:57:29, when Mike stepped up to the vista's
    // telescope, its use location stayed on his list of things at hand to the end of the session,
    // through the info board beside it, the gate and the walk down to the loss totem 14 m off, while
    // the press test found him outside its volume. The walks to the info board (20:59:04) and to the
    // totem (21:03:11, 21:04:14, 21:04:41) stopped short, "near, but the game offers Telescope".
    // At the info board: the list held the telescope first, then the board, which he stood in.
    std::vector<bool> board = {false, true};
    const auto atBoard = ways::StoodIn(board);
    REQUIRE(atBoard.size() == 1);
    CHECK(atBoard[0] == 1);
    // Two metres from the totem, the telescope alone, and he out of its volume: nothing is offered,
    // and the walk goes on.
    CHECK(ways::StoodIn({false}).empty());
    // In both of two volumes that overlap, both are offered, in the list's order.
    const auto both = ways::StoodIn({true, true});
    REQUIRE(both.size() == 2);
    CHECK(both[0] == 0);
    CHECK(both[1] == 1);
}

TEST_CASE("the list's verdicts stand once the hold's first map is done, though the roads change as she walks, 2026-11-05")
{
    // The mine after the cart (research/sessions/s44.log, 20:44:44 to 20:45:06): the way out,
    // T_ExitMine, listed, and the two ways nearer along its road left out; as Mike walked to the totem
    // the roads from where he stood no longer ran through them, and they were listed for two seconds
    // and left out again.
    ways::Settling settle;
    settle.Reset();
    settle.Begin(0.0);
    REQUIRE(settle.Update(1.0, true, 3, 3));
    ways::Lister lister;
    std::vector<ways::Entry> e(3);
    for (auto& x : e)
    {
        x.way = true;
        x.hasBox = true;
        x.known = true;
    }
    e[0].distance = 2418.0; // BP_CharacterTriggerVolume_C_17
    e[1].distance = 1827.0; // T_BeamFall
    e[2].distance = 4518.0; // T_ExitMine
    bool through = true;    // the road to the way out runs through the nearer two
    const auto enters = [&](size_t way, size_t other) { return through && other == 2 && way != 2; };
    lister.Judge(e, settle, lister.Due(settle, true, 1.0), true, true, 1.0, enters);
    CHECK(e[0].onRoadOf == 2);
    CHECK(e[1].onRoadOf == 2);
    CHECK(e[2].onRoadOf == -1);
    // The map is done: the verdicts stand, judged again however the roads run from where she walks.
    lister.Freeze();
    through = false;
    for (double now = 2.0; now < 6.0; now += 1.0)
    {
        lister.Again();
        lister.Judge(e, settle, lister.Due(settle, false, now), true, true, now, enters);
        CHECK(e[0].onRoadOf == 2);
        CHECK(e[1].onRoadOf == 2);
        CHECK(e[2].onRoadOf == -1);
    }
    // A way that came since is judged, and its verdict stands too.
    e.push_back(ways::Entry{});
    e[3].way = true;
    e[3].hasBox = true;
    e[3].known = true;
    e[3].distance = 900.0;
    through = true;
    lister.Again();
    lister.Judge(e, settle, lister.Due(settle, false, 7.0), true, true, 7.0, enters);
    CHECK(e[3].onRoadOf == 2);
    CHECK(e[3].passed);
    // A way whose road-giver left the list is judged afresh (ChooseListed takes its verdict back).
    through = false;
    e[0].passed = false;
    lister.Again();
    lister.Judge(e, settle, lister.Due(settle, false, 8.0), true, true, 8.0, enters);
    CHECK(e[0].onRoadOf == -1);
    CHECK(e[1].onRoadOf == 2);
    // A new hold judges every one afresh by the roads of now.
    lister.NewHold();
    CHECK_FALSE(lister.Frozen());
    lister.Judge(e, settle, lister.Due(settle, false, 9.0), true, true, 9.0, enters);
    CHECK(e[1].onRoadOf == -1);
    CHECK(e[3].onRoadOf == -1);
    // Frozen at once in a new hold (its map was done before it began), the last hold's verdicts are
    // still no verdicts of this one.
    through = true;
    lister.NewHold();
    lister.Freeze();
    lister.Judge(e, settle, lister.Due(settle, false, 10.0), true, true, 10.0, enters);
    CHECK(e[1].onRoadOf == 2);
    CHECK(e[3].onRoadOf == 2);
}

TEST_CASE("a target stays in the list a little beyond the range it came in at, 2026-11-05")
{
    // The vista's telescope from the mine floor (research/sessions/s44.log, 20:44:07): 100.2 m off with
    // a range of 100 m. Not yet a target, it stays out; a target already, it stays in.
    const double range = 10000.0;
    CHECK_FALSE(ways::InRange(10016.0, range, false));
    CHECK(ways::InRange(10016.0, range, true));
    CHECK(ways::InRange(9990.0, range, false));
    CHECK_FALSE(ways::InRange(range + ways::kRangeMargin + 1.0, range, true));
}

namespace
{
    // A world of upright posts and walls seen from above, and the character's capsule, radius r, swept
    // through it as the engine sweeps it: the first point along the way where it comes within r of
    // something, a quarter centimetre at a time.
    struct Plan
    {
        std::vector<std::pair<Vec, Vec>> walls; // segments; a post is a wall of no length
        double r = 42.0;
        static constexpr double kSkin = 0.1;

        static double ToSegment(const Vec& p, const Vec& a, const Vec& b, Vec& closest)
        {
            const double abx = b.x - a.x, aby = b.y - a.y;
            const double ll = abx * abx + aby * aby;
            double t = ll > 0.0 ? ((p.x - a.x) * abx + (p.y - a.y) * aby) / ll : 0.0;
            t = std::clamp(t, 0.0, 1.0);
            closest = Vec{a.x + abx * t, a.y + aby * t, p.z};
            return std::hypot(p.x - closest.x, p.y - closest.y);
        }
        double Nearest(const Vec& p, Vec& closest) const
        {
            double best = 1e300;
            for (const auto& [a, b] : walls)
            {
                Vec c;
                const double d = ToSegment(p, a, b, c);
                if (d < best)
                {
                    best = d;
                    closest = c;
                }
            }
            return best;
        }
        ways::SweepHit Sweep(const Vec& from, const Vec& to) const
        {
            ways::SweepHit hit;
            const double length = std::sqrt((to.x - from.x) * (to.x - from.x) + (to.y - from.y) * (to.y - from.y) + (to.z - from.z) * (to.z - from.z));
            Vec closest;
            const double start = Nearest(from, closest);
            // A capsule touching a surface it moves along does not catch on it: the engine keeps a
            // hair's skin, as here.
            if (start < r - kSkin)
            {
                hit.hit = true;
                hit.penetrating = true;
                hit.depth = r - start;
                hit.normal = start > 1e-9 ? Vec{(from.x - closest.x) / start, (from.y - closest.y) / start, 0.0} : Vec{1.0, 0.0, 0.0};
                return hit;
            }
            const int n = std::max(1, static_cast<int>(length / 0.25));
            for (int i = 1; i <= n; ++i)
            {
                const double f = static_cast<double>(i) / n;
                const Vec p{from.x + (to.x - from.x) * f, from.y + (to.y - from.y) * f, from.z + (to.z - from.z) * f};
                const double d = Nearest(p, closest);
                if (d >= r - kSkin) continue;
                hit.hit = true;
                hit.distance = length * static_cast<double>(i - 1) / n;
                hit.normal = Vec{(p.x - closest.x) / d, (p.y - closest.y) / d, 0.0};
                return hit;
            }
            return hit;
        }
    };
}

TEST_CASE("a passage is walked as the character walks a doorway: on its line, beside it, and sliding along what she touches, 2026-11-05")
{
    // The game's line through a doorway from a to b along x, the capsule's middle at one height.
    const Vec a{-65.0, 0.0, 122.5};
    const Vec b{65.0, 0.0, 122.5};
    const auto run = [&](const Plan& plan) { return ways::PassageWalk([&](const Vec& f, const Vec& t) { return plan.Sweep(f, t); }, a, b); };
    // An open doorway: straight through on the line.
    Plan open;
    open.walls = {{Vec{0.0, 60.0, 0.0}, Vec{0.0, 200.0, 0.0}}, {Vec{0.0, -60.0, 0.0}, Vec{0.0, -200.0, 0.0}}};
    auto way = run(open);
    CHECK(way.passed);
    CHECK(way.shift == doctest::Approx(0.0));
    CHECK(way.touches == 0);
    // The generator shed's kind: a jamb the line grazes on its left and the other well clear. The
    // straight sweep stops at the jamb (at the shed on 2026-11-05 41 cm in); she slides past it.
    Plan grazed;
    grazed.walls = {{Vec{0.0, -40.0, 0.0}, Vec{0.0, -200.0, 0.0}}, {Vec{0.0, 70.0, 0.0}, Vec{0.0, 200.0, 0.0}}};
    CHECK(grazed.Sweep(a, b).hit);
    way = run(grazed);
    CHECK(way.passed);
    CHECK(way.touches >= 1);
    // A doorway whose gap lies off the game's line, between two jambs: the middle gets through only
    // 3 to 9 cm to the right of it. Sliding along the jamb the line runs into takes her there.
    Plan off;
    off.walls = {{Vec{0.0, -39.0, 0.0}, Vec{0.0, -200.0, 0.0}}, {Vec{0.0, 51.0, 0.0}, Vec{0.0, 200.0, 0.0}}};
    way = run(off);
    CHECK(way.passed);
    // Met head on, she slides nowhere: the lines beside the game's are walked, nearest first, the
    // right side first.
    const auto headOn = [&](const Vec& f, const Vec& t)
    {
        ways::SweepHit hit;
        if (std::fabs(f.y) < 0.5 && std::fabs(t.y) < 0.5 && f.x < 0.0)
        {
            hit.hit = true;
            hit.distance = std::max(0.0, -f.x - 10.0);
            hit.normal = Vec{-1.0, 0.0, 0.0};
        }
        return hit;
    };
    int touches = 0;
    const Vec head = ways::SlideWalk(headOn, a, b, touches);
    CHECK(head.x < -9.0);
    way = ways::PassageWalk(headOn, a, b);
    CHECK(way.passed);
    CHECK(way.shift == doctest::Approx(ways::kPassageShift));
    // A gap narrower than the capsule: nothing passes, and the walk on the line stops at the doorway.
    Plan shut;
    shut.walls = {{Vec{0.0, -40.0, 0.0}, Vec{0.0, -200.0, 0.0}}, {Vec{0.0, 40.0, 0.0}, Vec{0.0, 200.0, 0.0}}};
    way = run(shut);
    CHECK_FALSE(way.passed);
    CHECK(way.reached.x < 0.0);
    // A wall across the way: nothing passes.
    Plan across;
    across.walls = {{Vec{0.0, -300.0, 0.0}, Vec{0.0, 300.0, 0.0}}};
    CHECK_FALSE(run(across).passed);
}

TEST_CASE("a door opened in a short exchange gives every verdict anew: the way on at the head of the lodge's stairs, 2026-11-05")
{
    // The lodge, 23:16:30 to 23:21:27 (research/sessions/s45-full.log). The list stood frozen since
    // 23:16:30, with the way on T_joshlightingfire beyond the landing door (the map stopped short of it
    // at an invisible wall in the doorway) and the answerphone downstairs another room's. Sam opened the
    // door; the game handed her back on the ground she had left, the hold took its list up again, and the
    // new map reached both. The way kept the verdict it was given while out of reach, and stayed listed
    // for four minutes, until the walk to the answerphone set it off on the way.
    //
    // That road runs through it: the way felt out to the answerphone from the landing at 23:18:05, its
    // corners as the log gives them, against the volume's collision as the session recorded it
    // (research/dumps/ground-20261105-230156.tsv, V line of T_joshlightingfire).
    ground::Record record;
    REQUIRE(record.Parse("ground 1\nV T_joshlightingfire H 2362.71 -8762.37 3739.23 2426.78 -8400.92 3739.23 2679.34 -8713.00 3739.23 2727.02 "
                         "-8444.01 3739.23 2679.34 -8713.00 3343.13 2727.02 -8444.01 3343.13 2362.71 -8762.37 3343.13 2426.78 -8400.92 3343.13\n"));
    REQUIRE(record.volumes.count("T_joshlightingfire") == 1);
    const shapes::Shape& volume = record.volumes.at("T_joshlightingfire");
    const std::vector<Vec> road = {{2726, -8925, 3388}, {2617, -8910, 3381}, {2597, -8850, 3381}, {2497, -8130, 3382}, {2417, -8030, 3382},
                                   {2337, -7370, 3382}, {2297, -7350, 3382}, {1977, -7310, 3268}, {1917, -7370, 3268}, {1917, -8470, 2968},
                                   {2057, -8630, 2968}, {2197, -9230, 2968}, {2257, -9290, 2968}, {2597, -9250, 2968}, {2657, -9190, 2968}};
    bool enters = false;
    for (size_t i = 1; i < road.size() && !enters; ++i)
    {
        const int steps = std::max(1, static_cast<int>(std::hypot(road[i].x - road[i - 1].x, road[i].y - road[i - 1].y) / 25.0));
        for (int s = 0; s <= steps && !enters; ++s)
        {
            const double f = static_cast<double>(s) / steps;
            const Vec feet{road[i - 1].x + (road[i].x - road[i - 1].x) * f, road[i - 1].y + (road[i].y - road[i - 1].y) * f,
                           road[i - 1].z + (road[i].z - road[i - 1].z) * f};
            enters = shapes::CapsuleTouches(volume, Vec{feet.x, feet.y, feet.z + 88.0}, 42.0, 88.0);
        }
    }
    CHECK(enters);

    // The list: the way, 393 cm off, and the answerphone, 45 m off on foot.
    ways::Settling settle;
    settle.Reset();
    settle.Begin(0.0);
    REQUIRE(settle.Update(1.0, true, 2, 2));
    ways::Lister lister;
    lister.NewHold();
    std::vector<ways::Entry> e(2);
    e[0].way = true;
    e[0].hasBox = true;
    e[0].known = true;
    e[0].distance = 393.0;
    e[1].distance = 4500.0;
    // Before the door: both out of reach, the way beyond the door and the answerphone another room's.
    e[0].unreachable = true;
    e[1].unreachable = true;
    const auto roads = [&](size_t way, size_t other) { return way == 0 && other == 1 && enters; };
    lister.Judge(e, settle, lister.Due(settle, true, 1.0), true, true, 1.0, roads);
    lister.Freeze();
    const int wayThen = 3, phoneThen = 3; // how the map judged them at the freeze
    // The door opens; she stands where she stood, so the hold takes its list up again.
    CHECK(ways::ResumesList(false, true));
    // The new map reaches the answerphone and the way: out of reach then, reached now.
    e[0].unreachable = false;
    e[1].unreachable = false;
    if (ways::Reopened(phoneThen, 1) || ways::Reopened(wayThen, 1)) lister.NewHold();
    CHECK_FALSE(lister.Frozen());
    lister.Judge(e, settle, lister.Due(settle, false, 30.0), true, true, 30.0, roads);
    // The way lies on the road to the answerphone, and only the answerphone is listed.
    CHECK(e[0].onRoadOf == 1);
    // A target reached then and now, or out of reach then and now, reopens nothing.
    CHECK_FALSE(ways::Reopened(1, 1));
    CHECK_FALSE(ways::Reopened(3, 3));
    CHECK_FALSE(ways::Reopened(2, 0));
    CHECK_FALSE(ways::Reopened(0, 1));
    CHECK(ways::Reopened(2, 1));
}

TEST_CASE("a hold handed back off the ground the last map reached begins afresh: the drop to the snowman, 2026-11-05")
{
    // The end of chapter 2, 00:02:40 to 00:03:59 (research/sessions/s45-full.log): the list stood on the
    // map of the ledge; the drop-down scene took Matt 2.4 m down the slope and handed him back 6.6 m from
    // where he had left it, within the 20 m and 30 s of a short exchange. The list from the ledge stood
    // for a minute while the new map ran: the ledge's way on, T_rescue_emily, out of reach from below and
    // walked to twice, "could not get there"; the way on short of the note, judged while it lay beyond
    // the slope, never weighed against the note's road.
    CHECK_FALSE(ways::ResumesList(false, false));
    // On the ground it left (a talk, a clue picked up and put down), the list is taken up again.
    CHECK(ways::ResumesList(false, true));
    // A pause moves nobody.
    CHECK(ways::ResumesList(true, false));
}

TEST_CASE("a scene of any length on the ground the map reached takes the list up again: Mike's trail, 2026-11-06")
{
    // research/sessions/s46-full.log. The heart carving's talk held Mike from 20:40:51 to 20:41:50 (59 s)
    // and handed him back where he had stood. Only a scene of 30 s or less was taken up then: the hold
    // began afresh, every thing behind him waited for the new map, and they came back one by one over
    // 24 s (the guidance totem 51.8, the baseball 53.6, the tractor 42:03, the hunger totem 42:06, the
    // mask 42:14). After the bush throw (20:48:46) the same, for 27 s.
    CHECK(ways::ResumesList(false, true));
    // Carried off the ground (a drop, a door gone through in the scene), afresh still.
    CHECK_FALSE(ways::ResumesList(false, false));
}

TEST_CASE("a way on beside a thing she cannot go to stays: the cabin's bedroom, 2026-11-06")
{
    // research/sessions/s46-full.log, 21:22:19 to 21:24:14. Walking toward the phone, Mike set off the
    // bedroom's volume and the game held him to BP_BatesSplineActor_C_9, the stick taken within 0 degrees
    // of it. The phone's spot (38386, -7068, 7576) lies inside the volume at the path's end, the phone's
    // camera (BP_CharacterTriggerVolume_C_42, within (38276, -7304, 7459) to (38633, -6947, 7715)), which
    // turns him to the phone and holds him for the press. The phone was left out as beside the path, the
    // volume as the phone's twin: the list stood empty, "nothing nearby to walk to".
    const Vec phone{38386.0, -7068.0, 7576.0};
    const Vec camera{(38276.0 + 38633.0) / 2.0, (-7304.0 + -6947.0) / 2.0, (7459.0 + 7715.0) / 2.0};
    // A thing she can go to: the volume beside it is its own way, and left out.
    CHECK(ways::LeftForThing(phone, camera, true));
    // One she cannot go to: the volume is the road there, and stays.
    CHECK_FALSE(ways::LeftForThing(phone, camera, false));
    // A volume farther off is no twin either way.
    CHECK_FALSE(ways::LeftForThing(phone, Vec{phone.x + 300.0, phone.y, phone.z}, true));
    CHECK_FALSE(ways::LeftForThing(phone, Vec{phone.x, phone.y, phone.z + 250.0}, true));
}

TEST_CASE("a path the game holds her to reaches only what lies by it: the lodge's basement path, 2026-11-05")
{
    // The basement of the lodge, 23:41:47 to 23:44:38 (research/sessions/s45-full.log): the game held Sam
    // to BP_BatesSplineActor_C_2, 709 cm long, forward at yaw 62 from (1113, -6297), the stick taken within
    // 0 degrees of it. The dollhouse (its place at (1154, -6856)) and the picture (at (1828, -5247)) were
    // listed; each walk stopped level with it, "could not get there"; the scare at the path's end
    // (T_sam_scare, within (1326, -5691) to (1884, -5078)) was the way on.
    CHECK(ways::PathStray(709.0, 0.0) == doctest::Approx(0.0));
    const double yaw = 62.0 * std::numbers::pi / 180.0;
    std::vector<Vec> path;
    for (int i = 0; i <= 14; ++i)
        path.push_back(Vec{1113.0 + std::cos(yaw) * 709.0 * i / 14.0, -6297.0 + std::sin(yaw) * 709.0 * i / 14.0, 2204.0});
    const auto placeAt = [](double x, double y) { return ways::TurnedBox(Vec{x, y, 2253.0}, Vec{150.0, 150.0, 128.0}, 0.0); };
    const double reach = 42.0 + ways::PathStray(709.0, 0.0);
    CHECK_FALSE(ways::RoadEntersBox(placeAt(1154, -6856), path, reach, 88.0));
    CHECK_FALSE(ways::RoadEntersBox(placeAt(1828, -5247), path, reach, 88.0));
    const ways::Box scare = ways::TurnedBox(Vec{1605.0, -5384.5, 2507.0}, Vec{558.0, 613.0, 1000.0}, 0.0);
    CHECK(ways::RoadEntersBox(scare, path, reach, 88.0));
    // A path that lets the stick stray (the cabin's, 291 cm within 10 degrees): a place 40 cm beside
    // its end is reached, one 2 m off is not.
    const double stray = ways::PathStray(291.0, 10.0);
    CHECK(stray == doctest::Approx(291.0 * std::tan(10.0 * std::numbers::pi / 180.0)));
    const std::vector<Vec> cabin = {{0.0, 0.0, 0.0}, {291.0, 0.0, 0.0}};
    const auto beside = [](double y) { return ways::TurnedBox(Vec{291.0, y, 64.0}, Vec{40.0, 40.0, 128.0}, 0.0); };
    CHECK(ways::RoadEntersBox(beside(100.0), cabin, 42.0 + stray, 88.0));
    CHECK_FALSE(ways::RoadEntersBox(beside(100.0), cabin, 42.0, 88.0));
    CHECK_FALSE(ways::RoadEntersBox(beside(220.0), cabin, 42.0 + stray, 88.0));
    // Never more than the cap, however long the path or wide the cone.
    CHECK(ways::PathStray(1e6, 45.0) == doctest::Approx(ways::kPathStrayMax));
    CHECK(ways::PathStray(-5.0, 10.0) == doctest::Approx(0.0));
}

TEST_CASE("only what the game has just turned on is news, not what comes within the list's range, 2026-11-05")
{
    // research/sessions/s45-full.log. The lodge's baseball bat was seen off from 23:05:30 and turned on
    // at 23:34:46.121 when Sam took the torch; it joined the list at once: news ("New: baseball bat").
    const double batOn = 0.0;
    CHECK(ways::IsNews(batOn, 0.012, false));
    // A way on whose ground takes a while to be known is still news when it is.
    CHECK(ways::IsNews(batOn, 15.0, false));
    // Drop Down Emily was on from the moment the level was first looked at (23:47:00), never seen
    // off; it joined the list at 23:53:42, 98.9 m off, as Matt walked within the range: no news.
    CHECK_FALSE(ways::IsNews(std::nullopt, 400.0, false));
    // Turned on long before it came within the range: no news either.
    CHECK_FALSE(ways::IsNews(batOn, 300.0, false));
}

TEST_CASE("the trigger that streams in more of the world is the way on where the map ends: the kitchen's airlock, 2026-11-07")
{
    // act4_1, Chris after the kitchen's open door (research/dumps/ground-20261107-001916.tsv, the
    // third grid, cut by research/tools/cut_ground.py from his cell at (4022, -8651)): the map ran
    // through the airlock to a strip of the porch and stopped at its edge, "no floor". The porch's
    // stairs, the yard and the path to the hanging shed are levels of Section_02, which the game
    // streams in only once he walks into T_Section2_Load in the airlock's corridor (its slab placed
    // from the level's own data, research/extract/json_act41); the map judged every way on beyond
    // closed off and listed none. The trigger itself, reached on foot, is the way on.
    std::ifstream in(std::filesystem::path(DA_REPO_ROOT) / "tests" / "fixtures" / "ground-kitchen-airlock.tsv");
    REQUIRE(in.good());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ground::Record record;
    REQUIRE(record.Parse(text));
    REQUIRE(!record.anchors.empty());
    const auto& anchor = record.anchors.front();
    const auto middle = [&](grid::Cell c)
    {
        const auto floor = record.floors.find({c.x, c.y, c.level});
        return Vec{anchor.x + (c.x + 0.5) * anchor.cell, anchor.y + (c.y + 0.5) * anchor.cell,
                   (floor != record.floors.end() ? floor->second : c.level * anchor.level + anchor.level / 2.0) + anchor.half};
    };
    const grid::Cell start{67, 2, 59};
    grid::Flood flood(start, 550, 120000);
    const auto passable = record.Passable();
    while (flood.Current() == grid::Flood::State::Running)
        flood.Step(64, passable);
    // Every cell within reach opened: the kitchen, the airlock and the porch's strip.
    REQUIRE(flood.Current() == grid::Flood::State::Complete);
    CHECK_FALSE(flood.RadiusReached());
    CHECK(flood.Expanded() > 2000);
    // The ground reaches the streaming trigger: his capsule touches its slab in the corridor.
    REQUIRE(record.volumes.count("T_Section2_Load") == 1);
    const auto& gate = record.volumes.at("T_Section2_Load");
    Vec low, high;
    gate.Bounds(low, high);
    const Vec at{(low.x + high.x) / 2.0, (low.y + high.y) / 2.0, (low.z + high.z) / 2.0};
    const grid::Cell goal{static_cast<int>(std::floor((at.x - anchor.x) / anchor.cell)), static_cast<int>(std::floor((at.y - anchor.y) / anchor.cell)),
                          LevelOf(at.z)};
    const int reach = static_cast<int>(std::ceil((std::hypot(high.x - low.x, high.y - low.y) / 2.0 + anchor.radius) / anchor.cell)) + 1;
    grid::Cell touching;
    REQUIRE(flood.BestWhere(goal, reach, -1, [&](grid::Cell c) { return shapes::CapsuleTouches(gate, middle(c), anchor.radius, anchor.half); }, touching));
    CHECK(flood.Cost(touching) * anchor.cell < 400.0);
    // The way on past the yard's gate (T_gate_crows, round (5729, -5010)) lies far beyond the ground.
    double nearest = 1e9;
    for (const auto& c : flood.Order())
    {
        const Vec m = middle(c);
        nearest = std::min(nearest, std::hypot(m.x - 5729.0, m.y + 5010.0));
    }
    CHECK(nearest > 2000.0);
    // The map proves what it did not reach closed off, the yard's gate with it, whatever the game has
    // still to stream in: no map can tell how the ground runs there (a room behind a closed door
    // stays another room's however many sections the act has still to load).
    const int opened = static_cast<int>(flood.Expanded());
    CHECK(ways::MapProves(true, flood.RadiusReached(), opened, 50));
    // The trigger he reaches is the way on while its section is not loaded, he stands outside it and
    // the act has not shown the section yet; in it, the section comes in, and with an answer unknown
    // it is none.
    CHECK(ways::StreamsMore(false, false, false));
    CHECK_FALSE(ways::StreamsMore(false, true, false));
    CHECK_FALSE(ways::StreamsMore(true, false, false));
    CHECK_FALSE(ways::StreamsMore(std::nullopt, false, false));
    // A map cut short, or too small, proves nothing either way.
    CHECK_FALSE(ways::MapProves(false, false, 3000, 50));
    CHECK_FALSE(ways::MapProves(true, true, 3000, 50));
    CHECK_FALSE(ways::MapProves(true, false, 20, 50));
}

TEST_CASE("a use location at the world's origin is no place to walk to: the death machine's, 2026-11-07")
{
    // act4_1's I_deathmachinelever and A_joshisdead's use location belong to activities placed at the
    // origin (LV_Scene02_hanging_shed: their roots carry no placement), and stay there: the list
    // said "deathmachinelever, 95 m, behind right, lower" from the kitchen, toward the origin.
    CHECK(ways::Unplaced(Vec{0.0, 0.0, 0.0}));
    CHECK(ways::Unplaced(Vec{0.4, -0.3, 0.0}));
    // Placed things, the shed's door among them, are where they are.
    CHECK_FALSE(ways::Unplaced(Vec{5655.3, 3516.9, 3780.2}));
    CHECK_FALSE(ways::Unplaced(Vec{70.0, 0.0, 0.0}));
    CHECK_FALSE(ways::Unplaced(Vec{0.0, 0.0, 2970.0}));
}

TEST_CASE("a thing the list has shown before is no news when the game turns it on again: the scarecrow's look-at, 2026-11-07")
{
    // act4_1: I_scarecrow_lookat is put off while Chris stands in T_scarecrow_lookat_disable and on
    // again once he leaves it; "New: scarecrow lookat" came each time it was back.
    const std::optional<double> backOn = 100.0;
    CHECK(ways::IsNews(backOn, 101.0, false));
    CHECK_FALSE(ways::IsNews(backOn, 101.0, true));
    CHECK_FALSE(ways::IsNews(std::nullopt, 101.0, false));
}

TEST_CASE("a walk goes on only with its own target, or the player's new choice, 2026-11-07")
{
    // act4_1: the walk to the scarecrow's look-at turned back to the kitchen's door once the look-at
    // was put off and the mod chose the door in its place.
    int lookAt = 0, door = 0, entrails = 0;
    const void* walkFor = &lookAt;
    CHECK(ways::WalkGoesOn(walkFor, &lookAt));
    // Its target gone, and the mod's choice in its place: the walk ends.
    CHECK_FALSE(ways::WalkGoesOn(walkFor, &door));
    CHECK_FALSE(ways::WalkGoesOn(walkFor, nullptr));
    // The player chose another with N: the walk follows the choice.
    walkFor = &entrails;
    CHECK(ways::WalkGoesOn(walkFor, &entrails));
    // No walk.
    CHECK_FALSE(ways::WalkGoesOn(nullptr, nullptr));
}

TEST_CASE("the volume that puts her on a beam is the way on where the ground's map ends: the asylum's walkway, 2026-11-07")
{
    // act4_2, Mike on the ledge above the cellar after the barrel's climb (research/dumps/
    // ground-20261107-170946.tsv, the second grid, cut by research/tools/cut_ground.py from his cell
    // at (28258, 33418)): the map ran down the walkway and stopped at its middle, 108 cells, and the
    // list stood empty; the way on is over BP_BeamWalk_C_1, whose ActivationVolume
    // (BP_CharacterTriggerVolume_C_1, an engine cube of 256 scaled 3.41 by 0.28 by 0.5, turned
    // -60.4 degrees: research/extract/json_act42) runs the walkway's whole length.
    std::ifstream in(std::filesystem::path(DA_REPO_ROOT) / "tests" / "fixtures" / "ground-asylum-walkway.tsv");
    REQUIRE(in.good());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ground::Record record;
    REQUIRE(record.Parse(text));
    REQUIRE(!record.anchors.empty());
    const auto& anchor = record.anchors.front();
    const auto middle = [&](grid::Cell c)
    {
        const auto floor = record.floors.find({c.x, c.y, c.level});
        return Vec{anchor.x + (c.x + 0.5) * anchor.cell, anchor.y + (c.y + 0.5) * anchor.cell,
                   (floor != record.floors.end() ? floor->second : c.level * anchor.level + anchor.level / 2.0) + anchor.half};
    };
    const grid::Cell start{-16, 21, 168};
    grid::Flood flood(start, 550, 120000);
    const auto passable = record.Passable();
    while (flood.Current() == grid::Flood::State::Running)
        flood.Step(64, passable);
    REQUIRE(flood.Current() == grid::Flood::State::Complete);
    CHECK(flood.Expanded() == 108);
    // The walkway's far end, where the guidance passage BP_Doorway_C_10 begins, is not reached.
    double nearest = 1e9;
    for (const auto& c : flood.Order())
    {
        const Vec m = middle(c);
        nearest = std::min(nearest, std::hypot(m.x - 28757.0, m.y - 32533.0));
    }
    CHECK(nearest > 250.0);
    // His capsule touches the beam's volume a few steps down the walkway.
    REQUIRE(record.volumes.count("BP_CharacterTriggerVolume_C_1") == 1);
    const auto& volume = record.volumes.at("BP_CharacterTriggerVolume_C_1");
    Vec low, high;
    volume.Bounds(low, high);
    const Vec at{(low.x + high.x) / 2.0, (low.y + high.y) / 2.0, (low.z + high.z) / 2.0};
    const grid::Cell goal{static_cast<int>(std::floor((at.x - anchor.x) / anchor.cell)), static_cast<int>(std::floor((at.y - anchor.y) / anchor.cell)),
                          LevelOf(at.z)};
    const int reach = static_cast<int>(std::ceil((std::hypot(high.x - low.x, high.y - low.y) / 2.0 + anchor.radius) / anchor.cell)) + 1;
    grid::Cell touching;
    REQUIRE(flood.BestWhere(goal, reach, -1, [&](grid::Cell c) { return shapes::CapsuleTouches(volume, middle(c), anchor.radius, anchor.half); }, touching));
    CHECK(flood.Cost(touching) * anchor.cell < 300.0);
    // It is a way on while it is on, the beam does not hold him, and it has not carried him over.
    CHECK(ways::BeamWay(true, false, false));
    CHECK_FALSE(ways::BeamWay(false, false, false));
    CHECK_FALSE(ways::BeamWay(true, true, false));
    CHECK_FALSE(ways::BeamWay(true, false, true));
    // Over the walkway's 8.7 m it carried him over; turned back at once, it did not.
    CHECK(ways::BeamCrossed(Vec{28362.0, 33234.0, 8460.0}, Vec{28794.0, 32476.0, 8460.0}));
    CHECK_FALSE(ways::BeamCrossed(Vec{28362.0, 33234.0, 8460.0}, Vec{28400.0, 33180.0, 8460.0}));
}

TEST_CASE("a hold begun afresh off the ground the last map reached keeps none of its verdicts: the asylum's cellar, 2026-11-07")
{
    // act4_2: the storm cellar's door opened, Mike dropped into the cellar 3 m on, and the list said
    // "totem danger, 17 m behind, above", reached by the map from the grounds outside; the cellar's
    // own map left it out a second later.
    CHECK(ways::KeptReach(1, false) == 0);
    CHECK(ways::KeptReach(3, false) == 0);
    // Handed back on that ground (a short exchange, the end of an interaction), what listed a target
    // stays until a new map judges it, and a way found closed off goes at once.
    CHECK(ways::KeptReach(1, true) == 1);
    CHECK(ways::KeptReach(3, true) == 3);
    CHECK(ways::KeptReach(2, true) == 0);
    CHECK(ways::KeptReach(0, true) == 0);
}

TEST_CASE("an empty list still to come says so, and its map hurries while nothing is listed: the asylum's grounds, 2026-11-07")
{
    // act4_2's start (research/sessions/s51-full.log, 17:09:46 to 17:10:02): the list's while ran out
    // with none of the 41 things judged, six presses said "nothing to walk to nearby", and the map,
    // at the smaller share of a frame from then on, took 16 s to reach the front door 51 m on foot.
    CHECK(ways::StillComing(true, 41));
    CHECK_FALSE(ways::StillComing(false, 41)); // done: nothing near is the truth
    CHECK_FALSE(ways::StillComing(true, 0));   // nothing waits on the map
    // The first list of a hold settling: the larger share.
    CHECK(ways::MapHurries(false, false, false, 0));
    CHECK(ways::MapHurries(false, false, true, 0));
    // Settled with nothing listed: still the larger share.
    CHECK(ways::MapHurries(true, false, false, 0));
    // A list stands, and nothing waits for the map: the smaller.
    CHECK_FALSE(ways::MapHurries(true, false, true, 0));
    CHECK_FALSE(ways::MapHurries(false, true, true, 0));
}

TEST_CASE("the map hurries while things wait for it: the hall's checkpoint, 2026-11-07")
{
    // research/sessions/s54-full.log, 21:47:13 to 21:47:56: the list settled after 3.3 s with 2 of 34
    // things judged and stood; the chapel door, 39 m off, came 29 s later and the map was done at 43 s.
    CHECK(ways::MapHurries(true, false, true, 32));
    CHECK(ways::MapHurries(true, true, true, 1));
    CHECK_FALSE(ways::MapHurries(true, false, true, 0));
}

TEST_CASE("a trigger back into a section she has been through is no way on: the asylum's hall, 2026-11-07")
{
    // act4_2's second run of the evening (research/sessions/s52-full.log, 18:46:37 to 19:15:46), the
    // levels of the act's sections as the world showed them ("the game has streamed ... in/out"). Begun
    // at the hospital's chair, Mike went to the morgue, took the keycard, fled up into Jack's lair and
    // dropped into the hall. There the game streams the wings in and out as he walks, and with every
    // trigger back into an unloaded wing a way on, the list said "Путь дальше" six times over in the
    // hall, none of them the story's way: the chapel door ("jacks lair door open") was listed with them.
    ways::LevelsSeen seen;
    // The first count: the hospital's wing (sections 4 and 6) shown; the hall, the morgue, the
    // church and Jack's lair's way out not.
    seen.Count({L"LV_Act_4_2_Section_01", L"LV_Act_4_2_Section_02", L"LV_Act_4_2_Section_04", L"LV_Act_4_2_Section_06", L"LV_Game_entities"}, true);
    struct Trigger
    {
        const char* name;
        const wchar_t* section;
    };
    const Trigger triggers[] = {
        {"T_Section3_Load1", L"LV_Act_4_2_Section_03"}, {"T_Section3_Load2", L"LV_Act_4_2_Section_03"}, {"T_Section4_Load", L"LV_Act_4_2_Section_04"},
        {"T_Section5_Load", L"LV_Act_4_2_Section_05"},  {"T_Section7_Load", L"LV_Act_4_2_Section_07"},  {"T_Section8_Load", L"LV_Act_4_2_Section_08"},
    };
    // Which are ways on with every section unloaded but the one she stands in (`here`).
    const auto ways = [&](const wchar_t* here)
    {
        std::vector<std::string> out;
        for (const auto& t : triggers)
        {
            const bool loaded = std::wstring(t.section) == here;
            if (ways::StreamsMore(loaded, false, seen.Seen(t.section))) out.push_back(t.name);
        }
        return out;
    };
    // In the hospital: the wings still to come are the ways on.
    CHECK(ways(L"LV_Act_4_2_Section_04") ==
          std::vector<std::string>{"T_Section3_Load1", "T_Section3_Load2", "T_Section5_Load", "T_Section7_Load", "T_Section8_Load"});
    // 18:51:37, on the way to the telegram: the morgue's section comes in.
    seen.Count({L"LV_Act_4_2_Section_05"}, false);
    // 19:04:04, the keycard taken: the way up to Jack's lair is still to come (T_Section7_Load, 52 m,
    // the hospital's wolf scare on its road), and was the one walked.
    CHECK(ways(L"LV_Act_4_2_Section_05") == std::vector<std::string>{"T_Section3_Load1", "T_Section3_Load2", "T_Section7_Load", "T_Section8_Load"});
    // 19:04:59 the church and Jack's lair come in; 19:06:42, dropped into the hall, the hall does.
    seen.Count({L"LV_Act_4_2_Section_07", L"LV_church_Structural_VTS_UP"}, false);
    seen.Loaded(L"LV_Act_4_2_Section_03");
    // From 19:10 on, walking about the hall as the wings swap: the way out past Jack's lair alone is
    // still to come (and lies beyond the chapel door).
    CHECK(ways(L"LV_Act_4_2_Section_03") == std::vector<std::string>{"T_Section8_Load"});
    CHECK(ways(L"LV_Act_4_2_Section_04") == std::vector<std::string>{"T_Section8_Load"});
    // A checkpoint loaded, or the next act: a new world, nothing been through but what it shows.
    seen.Count({L"LV_Act_4_2_Section_04"}, true);
    CHECK_FALSE(seen.Seen(L"LV_Act_4_2_Section_03"));
    CHECK(seen.Seen(L"LV_Act_4_2_Section_04"));
}

TEST_CASE("the hand-over names a choice the list shows: the asylum's hall, 2026-11-07")
{
    // act4_2, 18:30:39: Mike climbed the barrel up into the hall. At the hand-over's first look the
    // barrel he had climbed from (37 m below) was listed for that look alone, the game still counting
    // him in its volume, and was chosen; a second later the hand-over named it, "find way in climb
    // barrel, 37 m, ahead, below", while the list showed only the reader at Jack's door.
    const std::vector<bool> shown{false, true};
    CHECK(ways::HandOverChoice(0, true, shown) == 1);
    // The mod's choice the list shows is named.
    CHECK(ways::HandOverChoice(1, true, shown) == 1);
    // The player's own choice is named, the list keeping it whatever its rules say.
    CHECK(ways::HandOverChoice(0, false, shown) == 0);
    // Nothing shown, nothing named; no choice, the first shown.
    CHECK(ways::HandOverChoice(0, true, {false, false}) == -1);
    CHECK(ways::HandOverChoice(-1, true, shown) == 1);
    CHECK(ways::HandOverChoice(5, false, shown) == 1);
}

TEST_CASE("a character's turn is named before the keys and the choice: the cable car's upper station, 2026-11-07")
{
    // research/sessions/s54-full.log: Matt handed over at 22:04:04.7, his card up at 22:04:06.8 and gone
    // at 22:04:13.1, his objective said at 22:04:15.0; the keys and the choice had been said at 22:04:05.7.
    using ways::HintWaitsForTurn;
    CHECK(HintWaitsForTurn(true, false, false, true, 1.0)); // the card not come yet
    CHECK(HintWaitsForTurn(true, true, true, true, 2.1));   // the card up
    CHECK(HintWaitsForTurn(true, true, true, false, 2.1));  // the card up, no objective to wait for
    CHECK(HintWaitsForTurn(true, false, true, true, 8.4));  // the card gone, the objective due
    CHECK_FALSE(HintWaitsForTurn(true, false, true, false, 10.4)); // the objective said
    // No card comes: the keys wait kCardWait for one.
    CHECK(HintWaitsForTurn(true, false, false, false, ways::kCardWait - 0.1));
    CHECK_FALSE(HintWaitsForTurn(true, false, false, false, ways::kCardWait));
    // Never longer than kTurnWait.
    CHECK_FALSE(HintWaitsForTurn(true, true, true, true, ways::kTurnWait));
    // The same character handed back after a scene: nothing waits.
    CHECK_FALSE(HintWaitsForTurn(false, true, true, true, 1.0));
}

TEST_CASE("a neighbour the game offers at the end of the way is said once the offer stands: the station's map, 2026-11-07")
{
    // research/sessions/s54-full.log: at 22:12:13.101 the game offered the graffiti beside the map as
    // Matt stepped up, and at 22:12:13.583 the map, which the press would take.
    ways::OfferHold hold;
    CHECK_FALSE(hold.Update(true, 13.101));
    CHECK_FALSE(hold.Update(true, 13.400));
    CHECK_FALSE(hold.Update(false, 13.583));
    CHECK_FALSE(hold.Update(false, 15.0));
    // A neighbour offered on and on is said.
    CHECK_FALSE(hold.Update(true, 20.0));
    CHECK_FALSE(hold.Update(true, 21.0));
    CHECK(hold.Update(true, 20.0 + ways::kOtherOfferHold));
    hold.Reset();
    CHECK_FALSE(hold.Update(true, 30.0));
}

namespace
{
    // The list after the landing door on 2026-11-07 (research/sessions/s56-full.log, 23:48:36.6 to
    // 23:48:55.1), in seconds of that minute: the map of the lodge begun at 36.6 and done at 55.1, the
    // things it reached, the ways and the roads that ran through them.
    struct LodgeLook
    {
        std::vector<ways::Entry> e;
        std::vector<bool> listed;
    };

    // Runs the looks of that minute. `waitForMap`: the map's end and what was listed are told (as
    // now); without it every look is taken as one of a map done and nothing as shown (the rule before
    // 2026-11-08). Returns, for each way, the times it was listed.
    std::vector<std::vector<double>> RunLodge(bool waitForMap, LodgeLook& out)
    {
        enum
        {
            Bedroom, // hannah bedroom door locked, listed from before
            Totem,   // totem guidance, listed from before
            Trophy,  // hannahs trophy, reached at 38.3
            Shadow,  // way T_samsbath_psychoshadow, known at 36.8
            Seq8,    // way BP_CharacterTriggerVolume_C_8, known at 39.4
            Balloon, // way T_samsbath_floatingballoon, known at 40.0
            Prom,    // Prom night photo female, reached at 41.7
            Kitchen, // kitchen door locked, reached at 43.2
            Count
        };
        const double knownAt[Count] = {0.0, 0.0, 38.3, 36.8, 39.4, 40.0, 41.7, 43.2};
        const double distance[Count] = {1354.0, 1847.0, 1569.0, 440.0, 1539.0, 1480.0, 1069.0, 687.0};
        const double done = 55.1;
        ways::Settling settle;
        settle.Reset();
        settle.Begin(36.6);
        ways::Lister lister;
        auto& e = out.e;
        e.assign(Count, ways::Entry{});
        for (int i = 0; i < Count; ++i)
        {
            e[i].way = i == Shadow || i == Seq8 || i == Balloon;
            e[i].hasBox = true;
            e[i].distance = distance[i];
        }
        // The roads: the prom photo's and the kitchen door's run through all three ways, the
        // balloon's through the other two, the sequence's through the shadow's.
        const auto enters = [](size_t way, size_t other)
        {
            if (other == Prom || other == Kitchen) return way == Shadow || way == Seq8 || way == Balloon;
            if (other == Balloon) return way == Shadow || way == Seq8;
            if (other == Seq8) return way == Shadow;
            return false;
        };
        std::vector<std::vector<double>> listedAt(Count);
        out.listed.assign(Count, false);
        std::vector<bool> wasKnown(Count, false);
        for (double now = 36.6; now < 57.0; now += 0.2)
        {
            size_t judged = 0;
            for (int i = 0; i < Count; ++i)
            {
                const bool known = now >= knownAt[i];
                if (known && !wasKnown[i]) lister.Again();
                wasKnown[i] = known;
                judged += known ? 1 : 0;
                e[i].known = known;
                // A thing the map has not reached waits for it, and is no road to weigh.
                e[i].unreachable = !e[i].way && !known;
                e[i].shown = waitForMap && out.listed[i] && (!e[i].way || e[i].passed);
            }
            const bool settlesNow = settle.Update(now, now >= done, judged, Count);
            lister.Judge(e, settle, lister.Due(settle, settlesNow, now), true, !waitForMap || now >= done, now, enters);
            for (int i = 0; i < Count; ++i)
            {
                out.listed[i] = !e[i].unreachable && !(e[i].way && (e[i].unjudged || e[i].onRoadOf >= 0));
                if (e[i].way && out.listed[i]) listedAt[i].push_back(now);
            }
        }
        return listedAt;
    }
}

TEST_CASE("a way waits for the map to be done while anything is listed: the lodge after the landing door, 2026-11-07")
{
    // As it was: the sequence's way listed 3 s into the map and the balloon's at 3.6 s, each left out
    // a moment later for a road the map found since (s56: "the way on appeared and disappeared at once").
    LodgeLook before;
    const auto was = RunLodge(false, before);
    bool flicker = false;
    for (const auto& times : was)
    {
        if (!times.empty() && times.back() < 56.0) flicker = true; // listed, then left out
    }
    CHECK(flicker);
    // Now: no way is listed while the map runs, and the map done leaves all three on the roads.
    LodgeLook now;
    const auto is = RunLodge(true, now);
    for (const auto& times : is)
        CHECK(times.empty());
    CHECK(now.e[3].onRoadOf >= 0);
    CHECK(now.e[4].onRoadOf >= 0);
    CHECK(now.e[5].onRoadOf >= 0);
}

TEST_CASE("with nothing else listed a way is judged as the list settles, and once listed keeps its verdict for the hold")
{
    ways::Settling settle;
    settle.Reset();
    settle.Begin(0.0);
    ways::Lister lister;
    std::vector<ways::Entry> e(2);
    e[0].way = true;
    e[0].hasBox = true;
    e[0].known = true;
    e[0].distance = 800.0;
    e[1].hasBox = true;
    e[1].distance = 2500.0;
    e[1].unreachable = true; // a thing the map has not reached yet
    bool through = true;     // the thing's road runs through the way
    const auto enters = [&](size_t way, size_t other) { return through && way == 0 && other == 1; };
    // Settled by its while, the map still running, nothing listed: the way is judged and listed.
    REQUIRE(settle.Update(3.0, false, 1, 2));
    lister.Judge(e, settle, lister.Due(settle, true, 3.0), true, false, 3.0, enters);
    CHECK_FALSE(e[0].unjudged);
    CHECK(e[0].onRoadOf == -1);
    // The map reaches the thing beyond it: the way stays listed.
    e[0].shown = true;
    e[1].unreachable = false;
    lister.Again();
    lister.Judge(e, settle, lister.Due(settle, false, 4.0), true, false, 4.0, enters);
    CHECK(e[0].onRoadOf == -1);
    e[1].shown = true;
    lister.Again();
    lister.Judge(e, settle, lister.Due(settle, false, 6.0), true, true, 6.0, enters);
    CHECK(e[0].onRoadOf == -1);
    // A new hold judges it afresh.
    lister.NewHold();
    lister.Judge(e, settle, lister.Due(settle, false, 7.0), true, true, 7.0, enters);
    CHECK(e[0].onRoadOf == 1);
}

namespace
{
    // The deer's hill on 2026-11-08 (research/sessions/s57-full.log, 01:28:12.3 to 01:28:17), in
    // seconds of that minute: the hold's first map begun at 12.29 and done at 15.89; the way of the
    // herd's first stage (T_DeerWall_Stage1, 20 cm on foot) reached at 12.39, the way to the deer's
    // charge (T_DeerCombat_Start_Evil, 453 cm) at 12.53, the walk to the charge running through the
    // first stage's slab; a thing far beyond, listed at the first looks and found beside the path the
    // game holds her to at the first judgement (15.33), which the list settled at by its while.
    // Returns, for the near way, the times it was listed.
    std::vector<double> RunDeerHill(std::vector<ways::Entry>& e)
    {
        enum
        {
            Stage1,
            Evil,
            Thing,
            Count
        };
        const double knownAt[Count] = {12.39, 12.53, 1000.0};
        const double distance[Count] = {20.0, 453.0, 5720.0};
        ways::Settling settle;
        settle.Reset();
        settle.Begin(12.29);
        ways::Lister lister;
        e.assign(Count, ways::Entry{});
        for (int i = 0; i < Count; ++i)
        {
            e[i].way = i != Thing;
            e[i].hasBox = true;
            e[i].distance = distance[i];
        }
        const auto enters = [](size_t way, size_t other) { return way == Stage1 && other == Evil; };
        std::vector<bool> listed(Count, false);
        listed[Thing] = true; // listed before any judgement: beside the path is found only then
        bool judgedOnce = false;
        std::vector<double> listedAt;
        std::vector<bool> wasKnown(Count, false);
        for (double now = 12.39; now < 17.0; now += 0.05)
        {
            size_t judged = 0;
            for (int i = 0; i < Count; ++i)
            {
                const bool known = now >= knownAt[i];
                if (known && !wasKnown[i]) lister.Again();
                wasKnown[i] = known;
                judged += known ? 1 : 0;
                e[i].known = known;
            }
            const bool settlesNow = settle.Update(now, now >= 15.89, judged, Count);
            const bool due = lister.Due(settle, settlesNow, now);
            // The thing is found beside the path at the first judgement, before the list is weighed.
            if (due) judgedOnce = true;
            e[Thing].unreachable = judgedOnce;
            for (int i = 0; i < Count; ++i)
                e[i].shown = listed[i] && (!e[i].way || e[i].passed);
            lister.Judge(e, settle, due, true, now >= 15.89, now, enters);
            for (int i = 0; i < Count; ++i)
                listed[i] = !e[i].unreachable && !(e[i].way && (e[i].unjudged || e[i].onRoadOf >= 0));
            if (listed[Stage1]) listedAt.push_back(now);
        }
        return listedAt;
    }
}

TEST_CASE("a way the row rule has not weighed is not listed, and a verdict it never gave is never kept: the deer's hill, 2026-11-08")
{
    std::vector<ways::Entry> e;
    // Now: the herd's first stage is never listed, and the last judgement leaves it on the road to the
    // deer's charge, which stays listed.
    const auto now = RunDeerHill(e);
    CHECK(now.empty());
    CHECK(e[0].onRoadOf == 1);
    CHECK(e[1].onRoadOf == -1);
    CHECK_FALSE(e[1].unjudged);
}

TEST_CASE("the story moved on judges every target afresh but a way listed: the security light, 2026-11-08")
{
    // Emily on the tower's hill (research/sessions/s57-full.log, 01:32:54 to 01:33:18): the security
    // light's way listed and said as news; a trigger she walked through moved the story on, and the
    // roads judged again ran the walk to the death whistle through the way.
    ways::Settling settle;
    settle.Reset();
    settle.Begin(0.0);
    ways::Lister lister;
    std::vector<ways::Entry> e(2);
    e[0].way = true; // the security light
    e[0].hasBox = true;
    e[0].known = true;
    e[0].distance = 921.0;
    e[1].hasBox = true; // the death whistle
    e[1].distance = 3788.0;
    e[1].unreachable = true; // not reached by the map at the way's judgement
    const auto enters = [](size_t way, size_t other) { return way == 0 && other == 1; };
    REQUIRE(settle.Update(3.0, true, 2, 2));
    lister.Judge(e, settle, lister.Due(settle, true, 3.0), true, true, 3.0, enters);
    REQUIRE(e[0].onRoadOf == -1);
    e[0].shown = true;
    // The story moves on; the death whistle's road now known.
    e[1].unreachable = false;
    e[1].shown = true;
    lister.StoryMoved();
    lister.Judge(e, settle, lister.Due(settle, false, 25.0), true, true, 25.0, enters);
    CHECK(e[0].onRoadOf == -1); // the way stays listed
    CHECK_FALSE(e[0].unjudged);
    lister.Judge(e, settle, lister.Due(settle, false, 27.0), true, true, 27.0, enters);
    CHECK(e[0].onRoadOf == -1);
    // A way not listed is judged afresh as the story moves on, and a new hold judges every one.
    e[0].shown = false;
    lister.StoryMoved();
    lister.Judge(e, settle, lister.Due(settle, false, 30.0), true, true, 30.0, enters);
    CHECK(e[0].onRoadOf == 1);
    e[0].onRoadOf = -1;
    e[0].shown = true;
    lister.NewHold();
    lister.Judge(e, settle, lister.Due(settle, false, 31.0), true, true, 31.0, enters);
    CHECK(e[0].onRoadOf == 1);
}

TEST_CASE("a trigger that only streams the world in gives way to a way of the story: the lodge, 2026-11-08")
{
    // Ashley at the lodge's balcony (research/sessions/s57-full.log, 01:58:17): the candlelight's way
    // 75 cm off, and four triggers into the sections of the kitchen, the cinema and the cellar.
    std::vector<ways::Entry> e(4);
    for (auto& one : e)
    {
        one.way = true;
        one.hasBox = true;
    }
    e[0].distance = 75.0; // T_candlelight
    e[1].streams = true;  // T_STREAMING_SECTION_HALL_TO_KITCHEN_LOAD
    e[1].distance = 539.0;
    e[2].streams = true; // T_STREAMING_SECTION_HALL_TO_CINEMA_LOAD
    e[2].distance = 1051.0;
    e[3].streams = true; // T_STREAMING_SECTION_CINEMA_TO_CELLAR_LOAD
    e[3].distance = 2211.0;
    auto out = ways::StreamsSuperseded(e);
    CHECK_FALSE(out[0]);
    CHECK(out[1]);
    CHECK(out[2]);
    CHECK(out[3]);
    // A way of the story left out on a listed road leads on all the same.
    e[0].onRoadOf = 2;
    CHECK(ways::StreamsSuperseded(e)[2]);
    // None reachable (the asylum's grounds, the map stopped at ground not loaded): the triggers lead.
    e[0].closedOff = true;
    out = ways::StreamsSuperseded(e);
    CHECK_FALSE(out[1]);
    CHECK_FALSE(out[2]);
    e[0].closedOff = false;
    e[0].unreachable = true;
    CHECK_FALSE(ways::StreamsSuperseded(e)[1]);
    // Nor does one not judged yet, or behind on the path.
    e[0].unreachable = false;
    e[0].unjudged = true;
    CHECK_FALSE(ways::StreamsSuperseded(e)[1]);
    e[0].unjudged = false;
    e[0].behind = true;
    CHECK_FALSE(ways::StreamsSuperseded(e)[1]);
    // A thing to use does not lead on: the story may wait beyond the section.
    e[0].behind = false;
    e[0].way = false;
    CHECK_FALSE(ways::StreamsSuperseded(e)[1]);
}

TEST_CASE("a walk whose way left the list as she walked into it has arrived: the cinema's door slam, 2026-11-08")
{
    // The walk to the door slam's volume (research/sessions/s57-full.log, 02:03:13.5): she stood in
    // it, its scene took it off the list in the same look; the walk ended and nothing was heard.
    CHECK(ways::WalkEndsArrived(true, true));
    CHECK_FALSE(ways::WalkEndsArrived(true, false)); // turned off before she got there: gone
    CHECK_FALSE(ways::WalkEndsArrived(false, true)); // a use location the game took away: gone
}

TEST_CASE("the character standing in a walk's way: Chris in the cinema airlock, 2026-11-08")
{
    // Ashley at (832, -9121), the walk turned back west to the way's next corner at (305, -8997);
    // Chris 91 cm off and 976 cm from the airlock door the walk was aimed at (453, -8206): on the
    // west side of her, in the way (research/sessions/s57-full.log, 02:04:57).
    const ways::Vec here{832, -9121, 0};
    const ways::Vec corner{305, -8997, 0};
    const ways::Vec chris{743, -9138, 0}; // 91 cm west-south-west of her
    CHECK(std::hypot(chris.x - here.x, chris.y - here.y) == doctest::Approx(90.6).epsilon(0.02));
    CHECK(ways::InTheWay(here, corner, {chris}) == 0);
    // Walking east to the vase from the doorway (926, -9141), Chris 148 cm behind: not in the way.
    const ways::Vec door{926, -9141, 0};
    const ways::Vec east{985, -9157, 0};
    const ways::Vec behind{778, -9130, 0};
    CHECK(ways::InTheWay(door, east, {behind}) == -1);
    // Beside the way, or past reach: not in it; the nearer of two in it is named.
    CHECK(ways::InTheWay(here, corner, {ways::Vec{832, -9021, 0}}) == -1);
    CHECK(ways::InTheWay(here, corner, {ways::Vec{600, -9070, 0}}) == -1);
    CHECK(ways::InTheWay(here, corner, {ways::Vec{720, -9110, 0}, chris}) == 1);
}

TEST_CASE("a walk to a way on goes on to the thing that takes the way's place: the old kitchen's stairs, 2026-11-08")
{
    // research/sessions/s58-whole.log, 11:38:55: walking down to the old kitchen toward the streaming
    // trigger, the game turned on the psycho revelation 4 m ahead; the trigger lay beside it and left
    // the list for it, the walk ended "Target gone", and the mod chose the wine cellar's door 24 m back.
    int streaming = 0, revelation = 0, cellarDoor = 0;
    // The mod's choice moved on to the cellar door; the way's twin is the revelation: the walk goes on to it.
    CHECK(ways::WalkGoesTo(&streaming, &cellarDoor, &revelation) == &revelation);
    // Still chosen, the walk goes on to its own target.
    CHECK(ways::WalkGoesTo(&streaming, &streaming, &revelation) == &streaming);
    // No twin: nothing; the walk ends saying its target is gone.
    CHECK(ways::WalkGoesTo(&streaming, &cellarDoor, nullptr) == nullptr);
    CHECK(ways::WalkGoesTo(nullptr, &cellarDoor, &revelation) == nullptr);
}

TEST_CASE("the thing that stands for a way takes its place while its first verdict waits: the psycho revelation, 2026-11-08")
{
    // research/sessions/s58-whole.log, 11:38:55.212: the game turned the revelation on 4 m ahead of
    // the walk; its room's verdict waited for the reach map in the same look as it took the
    // streaming trigger's place, and the walk to the trigger ended "Target gone" at 11:38:55.244.
    ways::LeftOut revelation;
    revelation.hidden = true; // its room's verdict waiting for the map
    CHECK(ways::TakesWayPlace(revelation));
    // Listed, it does.
    revelation.hidden = false;
    CHECK(ways::TakesWayPlace(revelation));
    // Left out for a reason of its own, it does not: the walk ends saying its target is gone.
    const auto leftOutFor = [](bool ways::LeftOut::*reason)
    {
        ways::LeftOut thing;
        thing.hidden = true;
        thing.*reason = true;
        return ways::TakesWayPlace(thing);
    };
    CHECK_FALSE(leftOutFor(&ways::LeftOut::behind));
    CHECK_FALSE(leftOutFor(&ways::LeftOut::offPath));
    CHECK_FALSE(leftOutFor(&ways::LeftOut::unreachable));
    CHECK_FALSE(leftOutFor(&ways::LeftOut::superseded));
    CHECK_FALSE(leftOutFor(&ways::LeftOut::onRoad));
    CHECK_FALSE(leftOutFor(&ways::LeftOut::apart));
    // The player's own choice is listed whatever the reasons: it does.
    ways::LeftOut chosen;
    chosen.behind = true;
    CHECK(ways::TakesWayPlace(chosen));
}

TEST_CASE("a hold resumed after a scene that moved her begins a new map: the lodge's bedroom door, 2026-11-08")
{
    // research/sessions/s58-whole.log, 11:03:30: handed back 280 cm from where the map began, the
    // airlock door's trigger the scene had turned on was judged by the roads from there, listed, and
    // left out 0.3 s later as the map from where she stood ran through it.
    CHECK(ways::MovedFromMap(280.0));
    // A short exchange where she stands keeps the map: its roads hold.
    CHECK_FALSE(ways::MovedFromMap(40.0));
    CHECK_FALSE(ways::MovedFromMap(ways::kMovedFromMap));
}

TEST_CASE("a way on the road to a thing stays left out whichever target is chosen: the shed's exit, 2026-11-08")
{
    // research/sessions/s61-play.log, 18:48:44 to 18:48:47: T_exit_shed, 12 m off, on the road to the
    // scarecrow he had looked at, 85 m off. A road that left ways out only while its thing was chosen
    // listed the way, and took it back as his list came to the scarecrow; Andrey asked for the rule to go.
    std::vector<ways::Entry> e(2);
    e[0].way = true;
    e[0].hasBox = true;
    e[0].distance = 1233.0;
    e[1].hasBox = true;
    e[1].distance = 8463.0;
    const auto enters = [](size_t way, size_t other) { return way == 0 && other == 1; };
    CHECK(ways::RowRule(e, enters)[0] == 1);
    e[1].chosen = true;
    CHECK(ways::RowRule(e, enters)[0] == 1);
}

TEST_CASE("a way of the story on the road to a trigger that streams the world in is listed, the trigger given way: the dead wolf, 2026-11-08")
{
    // Mike after the dead wolf in act7_2 (research/sessions/s62-play.log, 21:54:42): the scene's way on
    // T_leave_dead_wolf2 came 10 m off, its ground not known yet, while the trigger into the act's
    // Section_03 stood listed 34 m off on foot, its road running through the way; and a thing to use.
    // Weighed apart, the row rule put the way on the trigger's road, the trigger gave way to the way a
    // look later, and neither was listed again.
    ways::Settling settle;
    settle.Reset();
    settle.Begin(42.0);
    ways::Lister lister;
    std::vector<ways::Entry> e(3);
    e[0].way = true; // T_Streaming_Load_Section02, into Section_03
    e[0].streams = true;
    e[0].hasBox = true;
    e[0].distance = 3441.0;
    e[0].shown = true;
    e[1].way = true; // T_leave_dead_wolf2
    e[1].hasBox = true;
    e[1].known = false; // its ground not known yet
    e[1].distance = 1111.0;
    e[2].hasBox = true; // the doctor's report
    e[2].distance = 1262.0;
    e[2].shown = true;
    const auto enters = [](size_t way, size_t other) { return way == 1 && other == 0; };
    // The map still running: the scene's way waits for it, and the trigger stands.
    REQUIRE_FALSE(settle.Update(42.3, false, 2, 3));
    lister.Judge(e, settle, lister.Due(settle, false, 42.3), true, false, 42.3, enters);
    CHECK(e[1].unjudged);
    CHECK_FALSE(e[0].superseded);
    // The map done, the list settles: the scene's way is weighed, the trigger gives way to it at that
    // very judgement, and its road is no road.
    e[1].known = true;
    REQUIRE(settle.Update(42.77, true, 3, 3));
    lister.Judge(e, settle, lister.Due(settle, true, 42.77), true, true, 42.77, enters);
    CHECK_FALSE(e[1].unjudged);
    CHECK(e[0].superseded);
    CHECK(e[1].onRoadOf == -1);
    CHECK(e[1].passed);
    lister.Freeze();
    // And so it stays, look after look.
    e[0].shown = false;
    e[1].shown = true;
    for (const double at : {42.95, 43.2, 44.0, 45.0})
    {
        lister.Judge(e, settle, lister.Due(settle, false, at), true, true, at, enters);
        CHECK(e[0].superseded);
        CHECK(e[1].onRoadOf == -1);
        CHECK_FALSE(e[1].unjudged);
    }
}

TEST_CASE("a verdict kept on the road of a trigger that has given way is judged again")
{
    // The verdict the session's list froze with: the scene's way on the trigger's road, the trigger
    // given way to it since.
    std::vector<ways::Entry> e(2);
    e[0].way = true;
    e[0].streams = true;
    e[0].hasBox = true;
    e[0].distance = 3441.0;
    e[0].superseded = true;
    e[1].way = true;
    e[1].hasBox = true;
    e[1].distance = 1111.0;
    e[1].kept = true;
    e[1].onRoadOf = 0;
    const auto enters = [](size_t way, size_t other) { return way == 1 && other == 0; };
    CHECK(ways::RowRule(e, enters)[1] == -1);
    // Chosen by the player (Andrey had the trigger chosen as the way came, 21:54:41), it is listed
    // all the same, and still no road: the scene's way stays listed beside it.
    e[0].chosen = true;
    e[1].kept = false;
    e[1].onRoadOf = -1;
    CHECK(ways::RowRule(e, enters)[1] == -1);
    e[0].chosen = false;
    e[1].kept = true;
    e[1].onRoadOf = 0;
    // A trigger that stands is a road as ever, and a verdict on it is kept.
    e[0].superseded = false;
    CHECK(ways::RowRule(e, enters)[1] == 0);
    e[1].kept = false;
    CHECK(ways::RowRule(e, enters)[1] == 0);
    // A trigger given way to is not judged itself: no thing's road takes it.
    e[0].superseded = true;
    e[1].way = false;
    e[1].kept = false;
    e[1].onRoadOf = -1;
    e[1].distance = 500.0;
    CHECK(ways::RowRule(e, [](size_t, size_t) { return true; })[0] == -1);
    e[0].superseded = false;
    CHECK(ways::RowRule(e, [](size_t, size_t) { return true; })[0] == 1);
}

TEST_CASE("a way a judgement cannot weigh keeps no older verdict it did not keep")
{
    // From the review of the dead wolf's fix: the story moves on, and a judgement on the map that
    // stands leaves way W on thing X's road; a map begun since settles by its while before it is done,
    // W, left out, waits for it unweighed, its verdict overwritten; the map then runs out at a look
    // that judges nothing, and W was listed with no verdict for the rest of the hold.
    ways::Settling settle;
    settle.Reset();
    settle.Begin(0.0);
    ways::Lister lister;
    std::vector<ways::Entry> e(2);
    e[0].way = true;
    e[0].hasBox = true;
    e[0].known = true;
    e[0].distance = 500.0;
    e[1].hasBox = true; // a thing listed, its road through W
    e[1].distance = 2000.0;
    e[1].shown = true;
    const auto enters = [](size_t way, size_t other) { return way == 0 && other == 1; };
    REQUIRE(settle.Update(1.0, true, 2, 2));
    lister.Judge(e, settle, lister.Due(settle, true, 1.0), true, true, 1.0, enters);
    REQUIRE(e[0].onRoadOf == 1);
    lister.StoryMoved();
    lister.Judge(e, settle, lister.Due(settle, false, 2.0), true, true, 2.0, enters);
    REQUIRE(e[0].onRoadOf == 1);
    REQUIRE_FALSE(e[0].shown);
    settle.Begin(3.0);
    REQUIRE(settle.Update(6.0, false, 1, 2));
    lister.Judge(e, settle, lister.Due(settle, true, 6.0), true, false, 6.0, enters);
    CHECK(e[0].unjudged);
    CHECK_FALSE(e[0].passed);
    // The map runs out at a look that judges nothing: W waits for the judgement that weighs it ...
    lister.Judge(e, settle, lister.Due(settle, false, 6.4), true, true, 6.4, enters);
    CHECK(e[0].unjudged);
    // ... which leaves it on the road.
    lister.Judge(e, settle, lister.Due(settle, false, 7.1), true, true, 7.1, enters);
    CHECK_FALSE(e[0].unjudged);
    CHECK(e[0].onRoadOf == 1);
}

TEST_CASE("a verdict on the road of a trigger given way to is not kept by the judgement either")
{
    // A trigger into one section on the road of a trigger into another, the list frozen; then the
    // story's way is weighed, both give way to it, and the frozen verdict on the given-way road is
    // weighed again rather than kept.
    ways::Settling settle;
    settle.Reset();
    settle.Begin(0.0);
    ways::Lister lister;
    std::vector<ways::Entry> e(3);
    for (auto& one : e)
    {
        one.way = true;
        one.hasBox = true;
        one.known = true;
    }
    e[0].streams = true; // the farther trigger
    e[0].distance = 3000.0;
    e[1].streams = true; // the nearer, on its road
    e[1].distance = 1000.0;
    e[2].known = false; // the story's way, not judged yet
    e[2].distance = 1500.0;
    const auto enters = [](size_t way, size_t other) { return way == 1 && other == 0; };
    REQUIRE(settle.Update(1.0, true, 3, 3));
    lister.Judge(e, settle, lister.Due(settle, true, 1.0), true, true, 1.0, enters);
    REQUIRE(e[1].onRoadOf == 0);
    REQUIRE_FALSE(e[0].superseded);
    lister.Freeze();
    e[0].shown = true;
    e[2].known = true;
    lister.Again();
    lister.Judge(e, settle, lister.Due(settle, false, 2.0), true, true, 2.0, enters);
    CHECK(e[0].superseded);
    CHECK(e[1].superseded);
    CHECK_FALSE(e[1].kept);
    CHECK(e[1].onRoadOf == -1);
    CHECK(e[2].onRoadOf == -1);
}

TEST_CASE("the way left along the road the walk follows, and its last leg: the damaged doors, 2026-11-08")
{
    // The way felt out to the damaged doors (research/sessions/s62-play.log, 21:56:42.883): from the
    // writing to a corner, then to the spot inside the place.
    const std::vector<ways::Vec> corners{{20167, 36398, 8298}, {20099, 36173, 8295}};
    bool lastLeg = false;
    // On the last stretch, 131 cm from the spot: one straight leg.
    const auto left = ways::LeftAlong(corners, ways::Vec{20141, 36297, 8298}, lastLeg);
    REQUIRE(left.has_value());
    CHECK(*left == doctest::Approx(std::hypot(20141.0 - 20099.0, 36297.0 - 36173.0)));
    CHECK(lastLeg);
    // Short of the corner by more than 60 cm: two legs left.
    const auto before = ways::LeftAlong(corners, ways::Vec{20190, 36480, 8298}, lastLeg);
    REQUIRE(before.has_value());
    CHECK_FALSE(lastLeg);
    CHECK(*before > *left);
    // On no stretch of it.
    CHECK_FALSE(ways::LeftAlong(corners, ways::Vec{25000, 30000, 8298}, lastLeg).has_value());
    CHECK_FALSE(lastLeg);
}

namespace
{
    // Her capsule seen from above against walls of no height: a circle swept over line segments, for
    // the steps of the ground over a level's hard collision.
    struct Wall
    {
        double ax, ay, bx, by;
    };

    double ToWall(double x, double y, const Wall& w, double& px, double& py)
    {
        const double dx = w.bx - w.ax;
        const double dy = w.by - w.ay;
        const double l2 = dx * dx + dy * dy;
        const double t = l2 > 0.0 ? std::clamp(((x - w.ax) * dx + (y - w.ay) * dy) / l2, 0.0, 1.0) : 0.0;
        px = w.ax + t * dx;
        py = w.ay + t * dy;
        return std::hypot(x - px, y - py);
    }

    double NearestWall(const std::vector<Wall>& walls, double x, double y, double& px, double& py)
    {
        double best = 1e300;
        for (const auto& w : walls)
        {
            double qx = 0.0, qy = 0.0;
            const double d = ToWall(x, y, w, qx, qy);
            if (d < best)
            {
                best = d;
                px = qx;
                py = qy;
            }
        }
        return best;
    }

    // A sweep as the engine answers it, the capsule `radius` across. Begun pressed in, the engine's
    // push out of a level's triangle collision gives no way out where `pushFails` (its normal back
    // along the sweep, no depth: what every step out of a cell pressed into the mine's spline wall
    // met in play); else it gives the way out from the nearest point of the wall.
    ways::SweepHit CircleSweep(const std::vector<Wall>& walls, double radius, bool pushFails, const Vec& from, const Vec& to)
    {
        ways::SweepHit out;
        double px = 0.0, py = 0.0;
        const double at = NearestWall(walls, from.x, from.y, px, py);
        const double length = std::hypot(to.x - from.x, to.y - from.y);
        if (at < radius)
        {
            out.hit = true;
            out.penetrating = true;
            if (pushFails || at < 1e-9)
            {
                out.normal = length > 0.0 ? Vec{-(to.x - from.x) / length, -(to.y - from.y) / length, 0.0} : Vec{};
                out.depth = 0.0;
            }
            else
            {
                out.normal = Vec{(from.x - px) / at, (from.y - py) / at, 0.0};
                out.depth = radius - at;
            }
            return out;
        }
        const int samples = std::max(1, static_cast<int>(length / 0.1));
        for (int k = 1; k <= samples; ++k)
        {
            const double f = static_cast<double>(k) / samples;
            if (NearestWall(walls, from.x + (to.x - from.x) * f, from.y + (to.y - from.y) * f, px, py) < radius)
            {
                const double g = static_cast<double>(k - 1) / samples;
                const double x = from.x + (to.x - from.x) * g, y = from.y + (to.y - from.y) * g;
                const double d = NearestWall(walls, x, y, px, py);
                out.hit = true;
                out.distance = length * g;
                out.normal = d > 0.0 ? Vec{(x - px) / d, (y - py) / d, 0.0} : Vec{};
                return out;
            }
        }
        return out;
    }
}

TEST_CASE("a step begun pressed into a wall the engine's push does not free her from is swept from a free spot of its cell")
{
    // A corridor 96 cm wide, for her capsule of 84 (42 across): its middle has 6 cm either way. A
    // cell's middle 9 cm off the corridor's own lies 3 cm in the wall.
    const std::vector<Wall> walls{{-500.0, -48.0, 500.0, -48.0}, {-500.0, 48.0, 500.0, 48.0}};
    const auto sweep = [&](const Vec& f, const Vec& t) { return CircleSweep(walls, 42.0, true, f, t); };
    const Vec from{0.0, 9.0, 0.0};
    const Vec to{20.0, 9.0, 0.0};
    const auto swept = ways::SweepOut(sweep, from, to);
    CHECK(swept.stuck);
    CHECK(swept.clear < 0.5);
    // A spot 5 cm across the way stands free: the step from there gets to the next cell.
    const auto got = ways::SweepFromFreeSpot(sweep, from, to, 10.0);
    REQUIRE(got.has_value());
    CHECK(std::hypot(got->x - to.x, got->y - to.y) <= 10.0);
    // Where the engine's push frees her, the step is free from the middle and not stuck.
    const auto pushes = [&](const Vec& f, const Vec& t) { return CircleSweep(walls, 42.0, false, f, t); };
    const auto out = ways::SweepOut(pushes, from, to);
    CHECK_FALSE(out.stuck);
    CHECK(std::hypot(out.reached.x - to.x, out.reached.y - to.y) <= 10.0);
    // A cell whose every spot within half a cell lies in the wall has none to stand at.
    CHECK_FALSE(ways::SweepFromFreeSpot(sweep, Vec{0.0, 30.0, 0.0}, Vec{20.0, 30.0, 0.0}, 10.0).has_value());
    // Nor does the free spot take a step into the wall: one toward it gets no nearer than it did.
    CHECK_FALSE(ways::SweepFromFreeSpot(sweep, from, Vec{0.0, 29.0, 0.0}, 10.0).has_value());
}

TEST_CASE("the mine's corridor to the scene's trigger is walked by the map: act8_5, 2026-11-09")
{
    // The hard collision's walls about the corridor (tests/fixtures/mine-corridor-walls.tsv, cut from
    // the level's cooked mesh). The reach map of research/sessions/s63-play.log got up the corridor
    // from the doorway at its foot (BP_DoubleDoorway_C_3) to (-10380, -14575), where every step out
    // of a cell began pressed into the wall and was refused 0 cm in; the scene's trigger lay past the
    // doorway at its head (BP_DoubleDoorway_C_5, -10577, -13995) and was left out as closed off.
    std::ifstream in(std::filesystem::path(DA_REPO_ROOT) / "tests" / "fixtures" / "mine-corridor-walls.tsv");
    REQUIRE(in.good());
    std::vector<Wall> walls;
    std::string line;
    while (std::getline(in, line))
    {
        if (line.size() < 2 || line[0] != 'W') continue;
        Wall w{};
        std::istringstream fields(line.substr(1));
        if (fields >> w.ax >> w.ay >> w.bx >> w.by) walls.push_back(w);
    }
    REQUIRE(walls.size() > 50);
    // The grid of the session's map (its anchor), on one level: the corridor's floor is one slope.
    const double ox = -10970.69, oy = -13865.07, cell = 20.0, reach = 10.0;
    const auto middle = [&](grid::Cell c) { return Vec{ox + (c.x + 0.5) * cell, oy + (c.y + 0.5) * cell, 0.0}; };
    const auto sweep = [&](const Vec& f, const Vec& t) { return CircleSweep(walls, 42.0, true, f, t); };
    const auto run = [&](bool freeSpots)
    {
        const grid::Passable step = [&](grid::Cell from, grid::Cell& to)
        {
            to.level = from.level;
            const Vec a = middle(from), b = middle(to);
            if (b.x < -10760.0 || b.x > -10200.0 || b.y < -15500.0 || b.y > -13800.0) return false;
            const auto swept = ways::SweepOut(sweep, a, b);
            if (std::hypot(swept.reached.x - b.x, swept.reached.y - b.y) <= reach) return true;
            return freeSpots && swept.stuck && ways::SweepFromFreeSpot(sweep, a, b, reach).has_value();
        };
        // From a cell the session's map reached at the corridor's foot.
        const grid::Cell start{static_cast<int>(std::floor((-10441.0 - ox) / cell)), static_cast<int>(std::floor((-15435.0 - oy) / cell)), 0};
        grid::Flood flood(start, 200, 100000);
        while (flood.Step(64, step) == grid::Flood::State::Running)
        {
        }
        double north = -1e300;
        for (const auto& c : flood.Order())
            north = std::max(north, middle(c).y);
        return north;
    };
    // Swept from the cells' middles alone the map stops in the corridor, short of its head.
    CHECK(run(false) < -14060.0);
    // Swept from where she stands free, it gets through the doorway at its head.
    CHECK(run(true) > -13990.0);
}

TEST_CASE("the distance said is the one on foot as far as it is known: the bolt door, 2026-11-09")
{
    // research/sessions/s63-play.log: straight up the stairwell 2479 cm; the map had got 11746 cm by
    // 00:30:54.832, when the door was said; the road came at 11786 cm.
    CHECK(ways::SpokenDistance(2479.0, std::nullopt, 11746.0) == doctest::Approx(11746.0));
    CHECK(ways::SpokenDistance(2479.0, 11786.0, 11746.0) == doctest::Approx(11786.0));
    CHECK(ways::SpokenDistance(2479.0, 11786.0, std::nullopt) == doctest::Approx(11786.0));
    // Nothing known on foot: the straight line, and never less than it.
    CHECK(ways::SpokenDistance(2479.0, std::nullopt, std::nullopt) == doctest::Approx(2479.0));
    CHECK(ways::SpokenDistance(2479.0, std::nullopt, 300.0) == doctest::Approx(2479.0));
    CHECK(ways::SpokenDistance(2479.0, 2000.0, std::nullopt) == doctest::Approx(2479.0));
}

TEST_CASE("a way never weighed waits at a hand-over that has no roads yet: Josh's cave, 2026-11-09")
{
    // research/sessions/s63-play.log 01:08:15.647: the hold handed back after a vision kept the
    // settling of the hold before; two ways listed before, two the game turned on meanwhile.
    ways::Settling settle;
    settle.Reset();
    settle.Begin(0.0);
    REQUIRE(settle.Update(1.0, true, 4, 4));
    ways::Lister lister;
    std::vector<ways::Entry> e(4);
    for (auto& x : e)
    {
        x.way = true;
        x.hasBox = true;
        x.known = true;
        x.distance = 500.0;
    }
    e[0].passed = e[1].passed = true;
    e[0].shown = e[1].shown = true;
    // The map dropped at the hand-over: no roads to judge by, the judgement not due.
    lister.Judge(e, settle, false, false, false, 2.0, [](size_t, size_t) { return false; });
    CHECK_FALSE(e[0].unjudged);
    CHECK_FALSE(e[1].unjudged);
    CHECK(e[2].unjudged);
    CHECK(e[3].unjudged);
}

TEST_CASE("a way the rule cannot weigh keeps the road it was last found on while it waits: the bolt door's road, 2026-11-09")
{
    // research/sessions/s63-play.log 00:22:41 to 00:22:52: two ways on the bolt door's road, judged so;
    // a map begun as Mike walked off settled the list before it got to the door, and the ways waited.
    ways::Settling settle;
    settle.Reset();
    settle.Begin(0.0);
    ways::Lister lister;
    std::vector<ways::Entry> e(3);
    e[0].hasBox = true; // the bolt door, 120 m on foot
    e[0].distance = 12029.0;
    e[0].shown = true;
    for (size_t i = 1; i < 3; ++i)
    {
        e[i].way = true;
        e[i].hasBox = true;
        e[i].known = true;
        e[i].distance = 1800.0;
    }
    const auto enters = [](size_t way, size_t other) { return way != 0 && other == 0; };
    REQUIRE(settle.Update(1.0, true, 3, 3));
    lister.Judge(e, settle, lister.Due(settle, true, 1.0), true, true, 1.0, enters);
    REQUIRE(e[1].onRoadOf == 0);
    REQUIRE(e[2].onRoadOf == 0);
    // The new map: the ways' ground not known yet, the door not reached; they wait, on its road still.
    lister.NewHold();
    settle.Begin(2.0);
    e[1].known = e[2].known = false;
    e[0].hasBox = true;
    REQUIRE(settle.Update(5.1, false, 1, 3));
    lister.Judge(e, settle, lister.Due(settle, true, 5.1), true, false, 5.1, enters);
    CHECK(e[1].unjudged);
    CHECK(e[2].unjudged);
    CHECK(e[1].onRoadOf == 0);
    CHECK(e[2].onRoadOf == 0);
}

TEST_CASE("the reach map's measure of the way on foot is its own, from her cell: the bolt door, 2026-11-09")
{
    // research/sessions/s63-play.log 00:30:54: the map got 11746 cm (587 cells of 20 cm), the door at
    // 12319 cm once reached.
    const double frontier = 11746.0 / 20.0, door = 12319.0 / 20.0;
    // At the map's start, while it runs: how far it has got.
    REQUIRE(ways::MapMeasure(false, false, 0.0, true, false, frontier, 0.0).has_value());
    CHECK(*ways::MapMeasure(false, false, 0.0, true, false, frontier, 0.0) == doctest::Approx(frontier));
    // Reached: its cost, less hers.
    CHECK(*ways::MapMeasure(true, true, door, true, false, frontier, 0.0) == doctest::Approx(door));
    CHECK(*ways::MapMeasure(true, true, door, true, false, frontier, 20.0) == doctest::Approx(door - 20.0));
    CHECK(*ways::MapMeasure(false, false, 0.0, true, false, frontier, 20.0) == doctest::Approx(frontier - 20.0));
    // Off its ground, or a target it found out of reach: nothing.
    CHECK_FALSE(ways::MapMeasure(false, false, 0.0, true, false, frontier, std::nullopt).has_value());
    CHECK_FALSE(ways::MapMeasure(true, false, 0.0, false, false, frontier, 0.0).has_value());
    // Ended with every cell opened and the target not judged: nothing; ended short: how far it got.
    CHECK_FALSE(ways::MapMeasure(false, false, 0.0, false, false, frontier, 0.0).has_value());
    CHECK(*ways::MapMeasure(false, false, 0.0, false, true, frontier, 0.0) == doctest::Approx(frontier));
    // Never below nothing.
    CHECK(*ways::MapMeasure(true, true, 5.0, true, false, frontier, 20.0) == doctest::Approx(0.0));
}

TEST_CASE("the scene's goal is never left out on the road to a thing beyond it: the lodge's basement, 2026-11-09")
{
    // research/sessions/s64-play.log, 11:22:20.209: T_arriveinbasement, where Scene03_find_our_friends 3
    // waits for Sam to turn "Разыщите остальных" off, 976 cm off (1494 on foot), on the road to the beach
    // photo in the basement beyond it, 2032 cm off. Left out, it left Andrey no way on for seven minutes.
    std::vector<ways::Entry> e(2);
    e[0].way = true;
    e[0].hasBox = true;
    e[0].distance = 1494.0;
    e[1].hasBox = true;
    e[1].distance = 2032.0;
    const auto enters = [](size_t way, size_t other) { return way == 0 && other == 1; };
    CHECK(ways::RowRule(e, enters)[0] == 1);
    e[0].goal = true;
    CHECK(ways::RowRule(e, enters)[0] == -1);
    // A verdict kept from before the scene waited there is no verdict for the goal.
    e[0].kept = true;
    e[0].onRoadOf = 1;
    CHECK(ways::RowRule(e, enters)[0] == -1);
}

TEST_CASE("a way of no goal on the goal's road stays left out: the goal counts as a listed way")
{
    // A relay of the scene (a scare, a companion's lead) on the road to the trigger the objective leads
    // to: walking to the goal sets it off, as ever.
    std::vector<ways::Entry> e(3);
    e[0].way = true; // the relay
    e[0].hasBox = true;
    e[0].distance = 500.0;
    e[1].way = true; // the goal
    e[1].goal = true;
    e[1].hasBox = true;
    e[1].distance = 1494.0;
    e[2].hasBox = true; // the thing beyond the goal
    e[2].distance = 2032.0;
    // The roads are weighed from the farthest in: the thing's runs through both.
    const auto enters = [](size_t way, size_t other) { return (way == 0 && (other == 1 || other == 2)) || (way == 1 && other == 2); };
    auto onRoadOf = ways::RowRule(e, enters);
    CHECK(onRoadOf[1] == -1);
    CHECK(onRoadOf[0] == 2);
    // Where only the goal's own road runs through the relay, the goal leaves it out: it counts as a
    // listed way.
    const auto goalOnly = [](size_t way, size_t other) { return (way == 0 && other == 1) || (way == 1 && other == 2); };
    onRoadOf = ways::RowRule(e, goalOnly);
    CHECK(onRoadOf[1] == -1);
    CHECK(onRoadOf[0] == 1);
}

TEST_CASE("a road not known proves nothing: the way keeps its verdict on it, 2026-11-09")
{
    // research/sessions/s64-play.log, 11:29:35.741: Sam walked off toward the police card, the beach
    // photo's road came back empty (her way back to where it parts from hers over 6 m), and the way
    // whose road it was taken to be a way on no road.
    std::vector<ways::Entry> e(2);
    e[0].way = true;
    e[0].hasBox = true;
    e[0].distance = 454.0;
    e[0].onRoadOf = 1;
    e[0].passed = true;
    e[1].hasBox = true;
    e[1].distance = 1000.0;
    const auto unknown = [](size_t, size_t) -> std::optional<bool> { return std::nullopt; };
    CHECK(ways::RowRule(e, unknown)[0] == 1);
    // Known now to run through nothing: offered.
    CHECK(ways::RowRule(e, [](size_t, size_t) -> std::optional<bool> { return false; })[0] == -1);
    // Never found on it, a road not known leaves it listed.
    e[0].onRoadOf = -1;
    CHECK(ways::RowRule(e, unknown)[0] == -1);
}

TEST_CASE("the path the game holds her to is a road at her feet: the climb out of the stream, 2026-11-09")
{
    // act8_6's stream (LV_Game_entities, BP_BatesSplineActor_C_0, its points in the world) runs at the
    // water's surface, z 4199; Sam's middle stood at z 4091, half her capsule 88 cm high, her radius 42.
    // The climb out's box (BP_BatesUseLocation_C_8, research/sessions/s64-play.log, its ground record's V
    // line) spans z 3991.5 to 4191.5: the path at the spline's height passes over it, and the climb out
    // was left out until she stood in it (11:15:59.326 to 11:17:28.981).
    const std::vector<Vec> spline{Vec{7469.0, 11071.0, 4199.0}, Vec{7237.0, 10893.0, 4199.0}, Vec{6564.0, 10280.0, 4199.0}};
    const auto box = ways::TurnedBox(Vec{6472.6, 10204.3, 4091.5}, Vec{200.0, 200.0, 200.0}, 235.5);
    CHECK_FALSE(ways::RoadEntersBox(box, spline, 42.0, 88.0));
    const auto road = ways::PathAtFeet(spline, 4199.0, 4091.0 - 88.0);
    CHECK(road.size() == spline.size());
    CHECK(road.front().z == doctest::Approx(4003.0));
    CHECK(road.back().x == doctest::Approx(6564.0));
    CHECK(ways::RoadEntersBox(box, road, 42.0, 88.0));
    // The path's own rise is kept: a path climbing a metre climbs a metre from her feet.
    const auto rising = ways::PathAtFeet({Vec{0.0, 0.0, 500.0}, Vec{100.0, 0.0, 600.0}}, 500.0, 0.0);
    CHECK(rising[1].z == doctest::Approx(100.0));
}

TEST_CASE("a map begun for the change the list's verdicts are given anew for counts: the prom photo, 2026-11-09")
{
    // research/sessions/s64-play.log, 11:29:20.552: the game moved the prom photo, a new map began at
    // 20.570, and the verdicts were given anew at 20.631 for the change at 20.552; that map could not let
    // them stand, and the list waited 25 s for the map after it.
    CHECK(ways::FreezeAfter(7, 20.570, 20.552) == 6);
    // A map begun before the change judged the ground as it was: the next one counts.
    CHECK(ways::FreezeAfter(7, 10.0, 20.552) == 7);
    // The world changed and the map dropped for it begun again in the same frame, at the frame's own
    // time: that map is the one begun for the change (research/sessions/s60-live.log, 15:58:09.562 to
    // .568, one frame; the list froze only 28 s later).
    CHECK(ways::FreezeAfter(7, 20.552, 20.552) == 6);
    // No map yet.
    CHECK(ways::FreezeAfter(0, -1000.0, 20.552) == 0);
}

TEST_CASE("ways: the beacon leads to the corner ahead of her, never one she walked past: the pool, 2026-11-10")
{
    // The road to "wade into pool" as the map felt it out (research/sessions/s66-play.log, 16:15:16.745),
    // its corners at the height of her middle.
    const std::vector<Vec> road{{-25346, 2104, -7985}, {-25106, 1815, -7985}, {-24986, 1455, -7991}, {-24986, 1375, -7990}};
    // Where the road begins: its second corner, the whole road left (835 cm, as the map said).
    auto lead = ways::LeadCorner(road, road.size(), road[0], 150.0, ways::kStretchRise);
    REQUIRE(lead);
    CHECK(lead->corner == 1);
    CHECK(lead->length == doctest::Approx(835.1).epsilon(0.002));
    // Gone on a stride past the first corner, a little off the line: the corner after it, the road left
    // from her through it.
    lead = ways::LeadCorner(road, road.size(), {-25030, 1720, -7985}, 150.0, ways::kStretchRise);
    REQUIRE(lead);
    CHECK(lead->corner == 2);
    CHECK(lead->length == doctest::Approx(268.6 + 80.0).epsilon(0.002));
    // A sharp turn walked past: the stretch nearest her is still the one that ends at the corner behind
    // her, where the walk's own following would lead her back; the beacon leads on.
    const std::vector<Vec> turn{{0, 0, 0}, {500, 0, 0}, {500, 500, 0}};
    const Vec past{600, -50, 0};
    CHECK(ways::StretchOn(turn, turn.size(), past, 150.0, ways::kStretchRise) == 1);
    lead = ways::LeadCorner(turn, turn.size(), past, 150.0, ways::kStretchRise);
    REQUIRE(lead);
    CHECK(lead->corner == 2);
    // Short of a corner by more than a step: that corner still.
    lead = ways::LeadCorner(turn, turn.size(), {300, 20, 0}, 150.0, ways::kStretchRise);
    REQUIRE(lead);
    CHECK(lead->corner == 1);
    // Within a step of it: the next.
    lead = ways::LeadCorner(turn, turn.size(), {470, 10, 0}, 150.0, ways::kStretchRise);
    REQUIRE(lead);
    CHECK(lead->corner == 2);
    // On no stretch: nothing; a new map gives the road from where she stands.
    CHECK_FALSE(ways::LeadCorner(turn, turn.size(), {2000, 2000, 0}, 150.0, ways::kStretchRise));
}

TEST_CASE("walks: a thing found closed off by a map begun during the walk ends the walk")
{
    // s71 23:28:57: ten levels streamed in, the new map found the danger totem closed off behind a
    // spline wall; the walk went on into the wall.
    CHECK(ways::WalkLosesTarget(true, false, true));
    // Closed off already as the player chose it: the player's choice stands, the walk goes on.
    CHECK_FALSE(ways::WalkLosesTarget(true, true, true));
    // Still reached: the walk goes on.
    CHECK_FALSE(ways::WalkLosesTarget(false, false, true));
    // A map the walk began itself, the world as it was: no new fact, the walk goes on (the morgue's
    // certificate, found closed off by the maps after it, a step from where the game offered it).
    CHECK_FALSE(ways::WalkLosesTarget(true, false, false));
}

TEST_CASE("walks: a door shut behind her during the walk is a change of the world: the cellar's door and the airlock's, 2026-11-12")
{
    // research/sessions/s72-play.log. The cellar: the walk to the airlock's door beyond the cellar's
    // set off at 11:59:26; nothing streamed; the walk's own asking found 68 steps shut at 11:59:58
    // (the cellar's door, slammed at 11:50:47, locked); the map begun for it found the door's things
    // closed off at 11:59:59; the walk ended "Пройти не удалось" at 12:00:09.
    // The play clock's seconds since the launch (11:16:41).
    const double setOff = 2565.0;    // 11:59:26
    const double streamed = 1530.0;  // the act's last streaming, long before
    const double shutFound = 2597.0; // 11:59:58
    CHECK(ways::WorldChangedSince(streamed, shutFound, setOff));
    CHECK(ways::WalkLosesTarget(true, false, ways::WorldChangedSince(streamed, shutFound, setOff)));
    // The asking found the ground as it was: no change, and the walk's own map tells nothing new.
    const double neverChanged = -1000.0;
    CHECK_FALSE(ways::WorldChangedSince(streamed, neverChanged, setOff));
    // A change before the walk set off was known as it set off (the airlock's, 11:46:58).
    CHECK_FALSE(ways::WorldChangedSince(streamed, 1817.0, setOff));
    // Levels streamed during the walk, as at s71's danger totem.
    CHECK(ways::WorldChangedSince(2577.0, neverChanged, setOff));
    // The airlock (11:46:41 to 11:47:29): the walk went for the way on into the kitchen 70 m off; a
    // door shut behind her, the asking found 54 steps shut at 11:46:58 and the new map found no way to
    // the way on. A way on found closed off so ends the walk too.
    CHECK(ways::WalkLosesTarget(true, false, ways::WorldChangedSince(streamed, 1817.0, 1800.0)));
}

TEST_CASE("walks: a step found shut alone is no change of the world, a door's dozens are")
{
    // The walk's own asking again in sessions 57 to 72: doors' leaves (s57's cinema exit door 106 steps,
    // s72's airlock 54 and cellar 68) and single steps from where she stood pressed (ten times, 1 step).
    CHECK(ways::GroundChangeTells(54));
    CHECK(ways::GroundChangeTells(68));
    CHECK(ways::GroundChangeTells(106));
    CHECK_FALSE(ways::GroundChangeTells(1));
    CHECK_FALSE(ways::GroundChangeTells(0));
    CHECK(ways::GroundChangeTells(ways::kWorldChangeSteps));
    CHECK_FALSE(ways::GroundChangeTells(ways::kWorldChangeSteps - 1));
}

TEST_CASE("ways: a way fired by walking in waits, while she stands in it already, for her to walk in anew")
{
    // s71 20:31:03: Matt left in the snowman's trigger by the prank's scene; "на месте" for a minute.
    CHECK(ways::WaitsForEntry(true, false, true));
    // Out of it: listed, and walking into it fires it.
    CHECK_FALSE(ways::WaitsForEntry(true, false, false));
    // A scene asking whether she stands in it: standing there answers it.
    CHECK_FALSE(ways::WaitsForEntry(false, true, true));
    CHECK_FALSE(ways::WaitsForEntry(true, true, true));
}

TEST_CASE("walks: the way back to walk in again holds the walk until the point stepped back to")
{
    // A last stretch that gave up by its spot, on a way that came along x and turned in at its end (the
    // bathroom door's of s71, 23:35, so): the point stepped back to lies on the way, more than 150 cm off.
    const std::vector<look::Vec> route{{3300, -9431, 3386}, {3567, -9431, 3386}, {3567, -9411, 3386}};
    const look::Vec here{3567, -9415, 3386};
    const auto back = look::BackOffPoint(route);
    REQUIRE(back);
    REQUIRE(std::hypot(back->x - here.x, back->y - here.y) > 150.0);
    // The point alone was a way round the walk dropped at once: she stood more than 150 cm from it.
    CHECK(ways::StretchOn(std::vector<look::Vec>{*back}, 1, here, 150.0, ways::kStretchRise) < 0);
    // The line back holds her from where she gave up to the point.
    const auto line = look::BackOffLine(here, *back);
    CHECK(ways::StretchOn(line, line.size(), here, 150.0, ways::kStretchRise) >= 0);
    const look::Vec halfway{(here.x + back->x) / 2.0, (here.y + back->y) / 2.0, here.z};
    CHECK(ways::StretchOn(line, line.size(), halfway, 150.0, ways::kStretchRise) >= 0);
    CHECK(ways::StretchOn(line, line.size(), *back, 150.0, ways::kStretchRise) >= 0);
    // On the way back no last stretch begins, though the way left is short and the spot near; a thing
    // offered where she stands still ends it.
    CHECK_FALSE(look::StretchKeeps(false, false, 120.0, 30.0, 60.0, true));
    CHECK(look::StretchKeeps(false, false, 120.0, 30.0, 60.0, false));
    CHECK(look::StretchKeeps(true, false, 120.0, 30.0, 60.0, true));
}

TEST_CASE("ways: a walk with no way round a companion names him only where the ground without him has a way: the cellar's corridor, s73 17:32:01")
{
    // The scene walled the corridor off while Ashley said she would go no further, Chris standing by the
    // wall: "Chris is in the way" was said, and half a second later the map found the totem closed off by
    // the scene's spline wall. With him left out the ground had no way: the thing is gone, not blocked by him.
    CHECK(ways::WhoStops(false) == ways::StopBy::Ground);
    // The cellar's corridor of s72 (11:54): a way past him over the ground, he standing in it.
    CHECK(ways::WhoStops(true) == ways::StopBy::Him);
}

TEST_CASE("ways: a companion in the walk's only way is stepped back from once before the walk names him: the old hotel's doorway, s73 11:46:39 to 11:47:16")
{
    // The ground without him had a way: the first time the walk steps back for him to come on.
    CHECK(ways::BacksOffFrom(false, true));
    // The second time in the walk it names him.
    CHECK_FALSE(ways::BacksOffFrom(true, true));
    // Where the ground without him was not asked (no way round searched), nothing is known of him.
    CHECK_FALSE(ways::BacksOffFrom(false, false));
}
