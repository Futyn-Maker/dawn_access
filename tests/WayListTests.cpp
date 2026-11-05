// features/WayList: the roads over the reach map, the row rule and the settling of the list,
// on drawn ground and replayed over the ground the mod felt out in play.
#include "Support.hpp"
#include "features/GroundFile.hpp"
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
        lister.Judge(entries, settle, due, true, now, [&](size_t way, size_t other)
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
        lister.Judge(entries, settle, due, true, now, [&](size_t way, size_t other)
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
            lister.Judge(entries, settle, lister.Due(settle, settlesNow, now), true, now, [](size_t, size_t) { return false; });
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
    lister.Judge(entries, settle, lister.Due(settle, false, 0.0), false, 0.0, never);
    CHECK_FALSE(entries[0].unjudged);
    CHECK(entries[1].onRoadOf == 0);
    // With a judge again the list follows it.
    lister.Judge(entries, settle, lister.Due(settle, false, 0.0), true, 0.0, never);
    CHECK(entries[0].unjudged);
    // A fresh hold before its first map lists no way, judge or none.
    ways::Settling fresh;
    fresh.Reset();
    entries[0].unjudged = false;
    lister.Judge(entries, fresh, false, false, 1.0, never);
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
    lister.Judge(e, settle, lister.Due(settle, true, 1.0), true, 1.0, enters);
    CHECK(e[0].onRoadOf == 2);
    CHECK(e[1].onRoadOf == 2);
    CHECK(e[2].onRoadOf == -1);
    // The map is done: the verdicts stand, judged again however the roads run from where she walks.
    lister.Freeze();
    through = false;
    for (double now = 2.0; now < 6.0; now += 1.0)
    {
        lister.Again();
        lister.Judge(e, settle, lister.Due(settle, false, now), true, now, enters);
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
    lister.Judge(e, settle, lister.Due(settle, false, 7.0), true, 7.0, enters);
    CHECK(e[3].onRoadOf == 2);
    CHECK(e[3].passed);
    // A way whose road-giver left the list is judged afresh (ChooseListed takes its verdict back).
    through = false;
    e[0].passed = false;
    lister.Again();
    lister.Judge(e, settle, lister.Due(settle, false, 8.0), true, 8.0, enters);
    CHECK(e[0].onRoadOf == -1);
    CHECK(e[1].onRoadOf == 2);
    // A new hold judges every one afresh by the roads of now.
    lister.NewHold();
    CHECK_FALSE(lister.Frozen());
    lister.Judge(e, settle, lister.Due(settle, false, 9.0), true, 9.0, enters);
    CHECK(e[1].onRoadOf == -1);
    CHECK(e[3].onRoadOf == -1);
    // Frozen at once in a new hold (its map was done before it began), the last hold's verdicts are
    // still no verdicts of this one.
    through = true;
    lister.NewHold();
    lister.Freeze();
    lister.Judge(e, settle, lister.Due(settle, false, 10.0), true, 10.0, enters);
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
    lister.Judge(e, settle, lister.Due(settle, true, 1.0), true, 1.0, roads);
    lister.Freeze();
    const int wayThen = 3, phoneThen = 3; // how the map judged them at the freeze
    // The door opens; she stands where she stood, so the hold takes its list up again.
    CHECK(ways::ResumesList(true, true));
    // The new map reaches the answerphone and the way: out of reach then, reached now.
    e[0].unreachable = false;
    e[1].unreachable = false;
    if (ways::Reopened(phoneThen, 1) || ways::Reopened(wayThen, 1)) lister.NewHold();
    CHECK_FALSE(lister.Frozen());
    lister.Judge(e, settle, lister.Due(settle, false, 30.0), true, 30.0, roads);
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
    CHECK_FALSE(ways::ResumesList(true, false));
    // On the ground it left (a talk, a clue picked up and put down), the list is taken up again.
    CHECK(ways::ResumesList(true, true));
    // No short exchange (gone long, or far): afresh.
    CHECK_FALSE(ways::ResumesList(false, true));
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
    CHECK(ways::IsNews(batOn, 0.012));
    // A way on whose ground takes a while to be known is still news when it is.
    CHECK(ways::IsNews(batOn, 15.0));
    // Drop Down Emily was on from the moment the level was first looked at (23:47:00), never seen
    // off; it joined the list at 23:53:42, 98.9 m off, as Matt walked within the range: no news.
    CHECK_FALSE(ways::IsNews(std::nullopt, 400.0));
    // Turned on long before it came within the range: no news either.
    CHECK_FALSE(ways::IsNews(batOn, 300.0));
}
