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

TEST_CASE("a volume's collision is recorded with the ground and read back, 2026-11-02")
{
    // The record keeps the collision of the ways on it judged, so that a walk to one replays
    // over the ground without the game.
    shapes::Shape shape;
    shape.hulls.push_back(shapes::BoxHull({-4381.89, -19627.33, 1428.38}, {100.0, 50.0, 500.0}, shapes::FromRotator(0.0, -61.0, 0.0)));
    shape.balls.push_back({{1.0, 2.0, 3.0}, 40.0});
    shape.pills.push_back({{0.0, 0.0, 0.0}, {0.0, 0.0, 120.0}, 30.0});
    std::string text = ground::HeaderLine() + "\n";
    for (const auto& line : ground::VolumeLines("T_lovetriangle_mike_choice", shape))
        text += line + "\n";
    ground::Record record;
    REQUIRE(record.Parse(text));
    REQUIRE(record.volumes.count("T_lovetriangle_mike_choice") == 1);
    const auto& back = record.volumes.at("T_lovetriangle_mike_choice");
    REQUIRE(back.hulls.size() == 1);
    REQUIRE(back.hulls[0].points.size() == 8);
    for (size_t i = 0; i < 8; ++i)
    {
        CHECK(back.hulls[0].points[i].x == doctest::Approx(shape.hulls[0].points[i].x).epsilon(1e-6));
        CHECK(back.hulls[0].points[i].z == doctest::Approx(shape.hulls[0].points[i].z).epsilon(1e-6));
    }
    REQUIRE(back.balls.size() == 1);
    CHECK(back.balls[0].radius == doctest::Approx(40.0));
    REQUIRE(back.pills.size() == 1);
    CHECK(back.pills[0].b.z == doctest::Approx(120.0));
    // The same distance from the recorded collision as from the one written.
    CHECK(shapes::Distance(back, {-4600.0, -19500.0, 1300.0}, {-4600.0, -19500.0, 1400.0}) ==
          doctest::Approx(shapes::Distance(shape, {-4600.0, -19500.0, 1300.0}, {-4600.0, -19500.0, 1400.0})).epsilon(1e-4));
}

TEST_CASE("a guidance passage is recorded with the ground and read back, 2026-11-04")
{
    ways::Passage p;
    p.name = "BP_Doorway_Shed";
    p.a = ways::Vec{8736.9, -5910.8, 2317.7};
    p.b = ways::Vec{8849.5, -5975.8, 2317.7};
    p.free = true;
    const std::string line = ground::PassageLine(p);
    ground::Record record;
    REQUIRE(record.Parse(ground::HeaderLine() + "\n" + line + "\n"));
    REQUIRE(record.passages.size() == 1);
    CHECK(record.passages[0].name == "BP_Doorway_Shed");
    CHECK(record.passages[0].a.x == doctest::Approx(8736.9));
    CHECK(record.passages[0].b.y == doctest::Approx(-5975.8));
    CHECK(record.passages[0].free);
    p.free = false;
    ground::Record shut;
    REQUIRE(shut.Parse(ground::HeaderLine() + "\n" + ground::PassageLine(p) + "\n"));
    CHECK_FALSE(shut.passages[0].free);
}

TEST_CASE("the generator shed's doorway, which the cells could not pass, is passed by the game's guidance passage once walked free, 2026-11-04")
{
    // Session 43 (research/sessions/s43.log): Mike's every reach map from beside the security gate got
    // no nearer the generator than 248 cm, wedged in the shed's doorway, and the generator, the very
    // thing the objective named, was left out as closed off; a grid laid in the lodge at 01:08 the
    // night before had happened to lie near the doorway's line and got in. The fixture is cut from that
    // session's record (research/dumps/ground-20261104-025228.tsv, research/tools/cut_ground.py): the
    // cells the map reached from her spot within (8600, -7300) to (10600, -5700); the shed's floor
    // where the passage ends inside (2317.7, as the 01:08 grid measured it there); and the level's own
    // passage there, BP_Doorway_Shed (its middle (8793, -5943), turned -30 degrees, its entry points
    // 65 cm either side), set down as free. That it is free is the walk's verdict in play, which this
    // test does not give: in session 44 (2026-11-05) a straight sweep along its line stopped 41 cm in at
    // an invisible wall (research/dumps/ground-20261105-202926.tsv, G line 0), and the walk that slides
    // and tries the lines beside it (ways::PassageWalk) is seen in play only from the next session.
    // What is tested here is the link: given the passage free, the map gets into the shed by it.
    std::ifstream in(std::filesystem::path(DA_REPO_ROOT) / "tests" / "fixtures" / "ground-generator-shed.tsv");
    REQUIRE(in);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ground::Record record;
    REQUIRE(record.Parse(text));
    REQUIRE(record.passages.size() == 1);
    const ground::Anchor a = record.anchors.back();
    // The generator's place as the list had it.
    const double gx = 8533.0, gy = -5879.0;
    const auto nearest = [&](const grid::Flood& flood)
    {
        double best = 1e300;
        for (const auto& c : flood.Order())
            best = std::min(best, std::hypot(a.x + (c.x + 0.5) * a.cell - gx, a.y + (c.y + 0.5) * a.cell - gy));
        return best;
    };
    const grid::Cell start{0, 0, 46};
    grid::Flood walled(start, 500, 500000);
    while (walled.Step(1000, record.Passable()) == grid::Flood::State::Running)
    {
    }
    CHECK(walled.Current() == grid::Flood::State::Complete);
    CHECK(nearest(walled) > 240.0);
    const grid::Cell inside{-86, 36, 46};
    CHECK_FALSE(walled.Reached(inside));
    grid::Flood linked(start, 500, 500000);
    linked.SetLinks(record.Links());
    while (linked.Step(1000, record.Passable()) == grid::Flood::State::Running)
    {
    }
    CHECK(linked.Reached(inside));
    // Inside the shed: nearer the generator than where the 01:08 grid's map counted it reached (216 cm).
    CHECK(nearest(linked) < 216.0);
    // The way in runs through the passage, its ramp end next to its shed end.
    const auto path = linked.PathTo(inside);
    REQUIRE(path.size() >= 2);
    const grid::Cell before = path[path.size() - 2];
    CHECK(std::hypot(a.x + (before.x + 0.5) * a.cell - 8849.5, a.y + (before.y + 0.5) * a.cell + 5975.8) <= ways::kPassageReach);
}

TEST_CASE("the recorded passages link a cell by its middle, where the live map places it")
{
    // A cell whose middle (10, 10) lies 22 cm from the passage's end and whose corner lies 36 cm from
    // it: the live map takes the passage from it (WorldOf: half a cell in), and so must a replay.
    const std::string text = ground::HeaderLine() + "\n"
                                                    "A 0 0 150 20 50 100 42 45 0.71\n"
                                                    "T 0 0 2 100.0 ........\n"
                                                    "T 10 0 2 100.0 ........\n"
                                                    "G test 205.0 5.0 100.0 28.0 22.0 100.0 1\n";
    ground::Record record;
    REQUIRE(record.Parse(text));
    const auto links = record.Links();
    REQUIRE(links);
    std::vector<grid::Link> out;
    links(grid::Cell{0, 0, 2}, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].to == grid::Cell{10, 0, 2});
    // From the far cell the passage leads back to the column of its other end, (1, 1), whose floor
    // the record does not have: no link.
    out.clear();
    links(grid::Cell{10, 0, 2}, out);
    CHECK(out.empty());
}
