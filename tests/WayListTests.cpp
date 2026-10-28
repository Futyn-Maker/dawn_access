// features/WayList: the roads over the reach map, the row rule and the settling of the list,
// on drawn ground and replayed over the ground the mod felt out in play.
#include "Support.hpp"
#include "features/GroundFile.hpp"
#include "features/WayList.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
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
        w.radius = std::max(1.5, std::min(w.box.high.x - w.box.low.x, w.box.high.y - w.box.low.y) / 2.0 / anchor.cell);
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
            if (w.reached || !flood.BestIn(w.goal, w.radius, w.levels, best)) continue;
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

    // The map reaches the four ways of the path at the costs the game logged.
    CHECK(woods[0].distance == doctest::Approx(771.0).epsilon(0.01));
    CHECK(woods[1].distance == doctest::Approx(3998.0).epsilon(0.01));
    CHECK(woods[2].distance == doctest::Approx(4236.0).epsilon(0.01));
    CHECK(woods[3].distance == doctest::Approx(4839.0).epsilon(0.01));
    for (size_t i = 4; i < woods.size(); ++i)
    {
        CAPTURE(woods[i].name);
        CHECK_FALSE(woods[i].reached); // past the duck-under, or far behind
    }
    // Pulled straight over the map, the road to the way past the tree is as long as the one the
    // game's own straightener swept for the walk once it was chosen (4514 cm in play), where the
    // map's cells, every third, said 47 m a moment before: one distance before and after a choice.
    double length = 0.0;
    for (size_t i = 1; i < woods[3].road.size(); ++i)
        length += std::hypot(woods[3].road[i].x - woods[3].road[i - 1].x, woods[3].road[i].y - woods[3].road[i - 1].y);
    CHECK(length == doctest::Approx(4514.0).epsilon(0.015));
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
