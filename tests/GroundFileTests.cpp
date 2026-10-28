// features/GroundFile: the record of the ground felt out in play, read back and searched.
#include "Support.hpp"
#include "features/GroundFile.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace da;

namespace
{
    // A corridor three cells long on level 0 with a step up onto level 1 at its end, as the
    // mod would have recorded it; the side columns have no floor.
    std::string Corridor()
    {
        std::string text = ground::HeaderLine() + "\n";
        ground::Anchor a;
        a.x = 100.0;
        a.y = 200.0;
        a.z = 388.0;
        text += ground::AnchorLine(a) + "\n";
        for (int x = 0; x <= 3; ++x)
        {
            ground::Probe p;
            p.x = x;
            p.found = true;
            p.floor = x == 3 ? 340.0 : 300.0;
            text += ground::ProbeLine(p) + "\n";
            ground::Step s;
            s.from = {x, 0, x == 3 ? 1 : 0};
            s.bx = x + 1;
            s.free = x < 3;
            s.land = x + 1 == 3 ? 1 : 0;
            s.why = x < 3 ? 0 : 'B';
            text += ground::StepLine(s) + "\n";
            ground::Step back;
            back.from = {x + 1, 0, x + 1 == 3 ? 1 : 0};
            back.bx = x;
            back.free = x < 3;
            back.land = 0;
            if (x < 3) text += ground::StepLine(back) + "\n";
            ground::Probe side;
            side.x = x;
            side.y = 1;
            side.found = false;
            side.why = 's';
            text += ground::ProbeLine(side) + "\n";
        }
        ground::Search q;
        q.from = {0, 0, 0};
        q.to = {3, 0, 1};
        q.radius = 0.0;
        q.levels = 4;
        text += ground::SearchLine(q) + "\n";
        q.done = true;
        q.found = true;
        q.cells = 4;
        q.sweeps = 7;
        q.nearest = {3, 0, 1};
        text += ground::ResultLine(q) + "\n";
        ground::Position at;
        at.x = 110.0;
        at.y = 210.0;
        at.z = 388.0;
        text += ground::PositionLine(at) + "\n";
        return text;
    }
}

TEST_CASE("a record is read back as it was written")
{
    ground::Record record;
    REQUIRE(record.Parse(Corridor()));
    REQUIRE(record.anchors.size() == 1);
    CHECK(record.anchors[0].x == doctest::Approx(100.0));
    CHECK(record.anchors[0].half == doctest::Approx(88.0));
    CHECK(record.probes.size() == 8);
    CHECK(record.steps.size() == 7);
    REQUIRE(record.searches.size() == 1);
    CHECK(record.searches[0].done);
    CHECK(record.searches[0].found);
    CHECK(record.searches[0].cells == 4);
    CHECK(record.searches[0].nearest == grid::Cell{3, 0, 1});
    CHECK(record.positions.size() == 1);
    const auto* blocked = record.Find({3, 0, 1}, 4, 0);
    REQUIRE(blocked != nullptr);
    CHECK(!blocked->free);
    CHECK(blocked->why == 'B');
}

TEST_CASE("the search runs over the recorded steps, climbing the recorded level, and counts what was never probed")
{
    ground::Record record;
    REQUIRE(record.Parse(Corridor()));
    int unknown = 0;
    grid::Search search({0, 0, 0}, {3, 0, 1}, 0.0, 4, 100, 100000);
    while (search.Step(50, record.Passable(&unknown)) == grid::Search::State::Running)
    {
    }
    REQUIRE(search.Current() == grid::Search::State::Found);
    const auto path = search.Path();
    REQUIRE(path.size() == 4);
    CHECK(path.back().level == 1);
    CHECK(unknown > 0); // the diagonal and side steps were never recorded

    // Beyond the recorded end nothing is free: the search says so after bounded work.
    grid::Search beyond({0, 0, 0}, {6, 0, 1}, 0.0, 4, 100, 100000);
    while (beyond.Step(50, record.Passable()) == grid::Search::State::Running)
    {
    }
    CHECK(beyond.Current() == grid::Search::State::Failed);
    CHECK(beyond.Nearest() == grid::Cell{3, 0, 1});
}

TEST_CASE("a record without the header is refused, and odd lines are skipped")
{
    ground::Record record;
    CHECK(!record.Parse("P 1 2 3\n"));
    ground::Record other;
    CHECK(other.Parse("ground 1\nwhat is this\nS 1 2\nF 0 0 0 1.0 1.0\n"));
    CHECK(other.probes.size() == 1);
    CHECK(other.steps.empty());
}

TEST_CASE("the reach map over the woods' pocket after the duck-under reaches the deer's trigger and nothing past the walls")
{
    // The ground the mod feels out from where the game hands Beth back after the duck-under
    // scene, (11875, -16130, 3451), made offline from the level's collision by
    // research/offline_probe/make_fixture.py: a pocket the path's invisible walls close, 311
    // cells (the game's own search there opened 315 on 2026-10-24). The goals are the shack's
    // trigger volumes as Exploration's GoalOf takes them (research/triggers/woods_volumes.tsv).
    std::ifstream in(std::filesystem::path(DA_REPO_ROOT) / "tests" / "fixtures" / "ground-woods-pocket.tsv");
    REQUIRE(in.good());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ground::Record record;
    REQUIRE(record.Parse(text));
    REQUIRE(!record.anchors.empty());
    const auto& anchor = record.anchors.front();
    const grid::Cell start{0, 0, static_cast<int>(std::floor((anchor.z - anchor.half) / anchor.level))};
    int unknown = 0;
    grid::Flood flood(start, 550, 80000);
    while (flood.Step(50, record.Passable(&unknown)) == grid::Flood::State::Running)
    {
    }
    CHECK(flood.Current() == grid::Flood::State::Complete);
    CHECK_FALSE(flood.RadiusReached());
    CHECK(flood.Expanded() == 311);
    CHECK(unknown == 0); // every step the map asks was felt out offline
    struct Goal
    {
        const char* name;
        grid::Cell cell;
        double radius;
        bool reached;
    };
    const Goal goals[] = {
        {"C_13, a pace change on the way", {-18, -13, 67}, 5.08, true},
        {"C_8, the game's jog to the deer", {-28, -21, 67}, 4.35, true},
        {"C_2, the deer scene", {-40, -31, 67}, 3.42, true},
        {"C_5, the door loop", {-97, -37, 68}, 5.45, false},
        {"C_3, the walk to the path", {-115, 4, 68}, 1.50, false},
        {"C_6, the path forward only", {-114, 17, 68}, 1.50, false},
        {"C_0, the short hold", {-114, 30, 68}, 1.50, false},
        {"C_4, the shack's door", {-114, 63, 68}, 1.52, false},
    };
    for (const auto& goal : goals)
    {
        CAPTURE(goal.name);
        grid::Cell best;
        CHECK(flood.BestIn(goal.cell, goal.radius, 4, best) == goal.reached);
    }
    // The deer's trigger is the farthest reached: 49.4 cells on foot.
    grid::Cell deer;
    REQUIRE(flood.BestIn({-40, -31, 67}, 3.42, 4, deer));
    CHECK(flood.Cost(deer) == doctest::Approx(49.426).epsilon(0.001));
}
