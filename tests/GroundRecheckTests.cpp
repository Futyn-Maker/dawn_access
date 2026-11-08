#include "features/GroundRecheck.hpp"

#include "features/GridPath.hpp"
#include "features/GroundFile.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>

using namespace da;

namespace
{
    ground::Record ReadFixture(const char* name)
    {
        std::ifstream in(std::filesystem::path(DA_REPO_ROOT) / "tests" / "fixtures" / name);
        REQUIRE(in.good());
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        ground::Record record;
        REQUIRE(record.Parse(text));
        return record;
    }

    std::unique_ptr<grid::Flood> Flood(const ground::Record& record, grid::Cell from)
    {
        auto flood = std::make_unique<grid::Flood>(from, 200, 100000);
        const auto passable = record.Passable();
        while (flood->Step(64, passable) == grid::Flood::State::Running)
        {
        }
        REQUIRE(flood->Current() == grid::Flood::State::Complete);
        return flood;
    }

    bool ReachesColumnsFrom(const grid::Flood& flood, int minX)
    {
        for (int x = minX; x <= 10; ++x)
        {
            for (int y = -80; y <= 0; ++y)
            {
                if (flood.LevelsAt(x, y)) return true;
            }
        }
        return false;
    }

    // The cinema's exit door as the game shut it on 2026-11-08: a leaf across the doorway between
    // the airlock's columns (up to -58) and the hall's (from -57), the doorway's rows. The steps the
    // mod swept after the stall met something of the persistent level there (02:04:51).
    bool ShutByDoor(const recheck::StepKey& step)
    {
        const auto& [ax, ay, alevel, bx, by] = step;
        const bool across = (ax <= -58 && bx >= -57) || (ax >= -57 && bx <= -58);
        return across && ay >= -56 && ay <= -50 && by >= -56 && by <= -50;
    }
}

TEST_CASE("recheck: the columns a stopped walk asks again run from her along the way ahead")
{
    // The walk to the vase stopped at (926, -9141) in the lodge's cinema airlock, the way felt out
    // through the doorway at (925..985, -9157) and on across the hall (research/sessions/s57-full.log,
    // 02:04:18); the grid laid at (2075.21, -8106.85), 20 cm cells.
    const recheck::Grid grid{recheck::Vec{2075.21, -8106.85, 3060.15}, 20.0};
    const std::vector<recheck::Vec> ahead{{925, -9157, 0}, {945, -9157, 0}, {965, -9157, 0}, {985, -9157, 0}, {1785, -9037, 0}};
    const auto columns = recheck::ColumnsAhead(grid, recheck::Vec{926, -9141, 0}, ahead, 300.0, 2);
    CHECK(grid.ColumnOf(recheck::Vec{926, -9141, 0}) == recheck::Column{-58, -52});
    CHECK(columns.count({-58, -52}));
    CHECK(columns.count({-57, -53})); // the doorway
    CHECK(columns.count({-56, -54}));
    CHECK(columns.count({-55, -53}));
    CHECK(columns.count({-44, -52})); // on across the hall, within the reach
    CHECK_FALSE(columns.count({-40, -52})); // past it
    CHECK_FALSE(columns.count({-58, -47})); // beside the way, past its breadth
}

TEST_CASE("recheck: a walk stopped by a door the game shut asks the ground ahead again, and the map no longer takes her through it")
{
    // tests/fixtures/ground-cinema-airlock-slam.tsv: the airlock and the lower hall as the mod's ground
    // knew them at the session's end (cut from research/dumps/ground-20261108-012812.tsv by
    // research/tools/cut_ground.py from (926, -9141)): the doorway's steps found free before the door
    // slammed, never asked again.
    auto record = ReadFixture("ground-cinema-airlock-slam.tsv");
    const grid::Cell stall{-58, -52, 51};
    // As it was: every map took her through the doorway to the hall, where the vase and the photo lay.
    const auto before = Flood(record, stall);
    CHECK(ReachesColumnsFrom(*before, -40));
    // The walk stops; the steps around her and under the way ahead are asked again, and the door's
    // leaf shuts those across the doorway.
    const recheck::Grid grid{recheck::Vec{2075.21, -8106.85, 3060.15}, 20.0};
    const std::vector<recheck::Vec> ahead{{925, -9157, 0}, {945, -9157, 0}, {965, -9157, 0}, {985, -9157, 0}, {1785, -9037, 0}};
    const auto columns = recheck::ColumnsAhead(grid, recheck::Vec{926, -9141, 0}, ahead, 300.0, 2);
    std::vector<recheck::Column> shut;
    for (const auto& key : recheck::StepsIn(record.stepLevels, columns))
    {
        if (ShutByDoor(key)) shut.emplace_back(std::get<0>(key), std::get<1>(key));
    }
    REQUIRE_FALSE(shut.empty());
    // What changed is asked again for a stretch around it, as the next map asks it.
    const auto around = recheck::ColumnsAround(shut, 12);
    for (const auto& key : recheck::StepsIn(record.stepLevels, around))
    {
        if (ShutByDoor(key)) record.stepLevels.erase(key);
    }
    const auto after = Flood(record, stall);
    CHECK_FALSE(ReachesColumnsFrom(*after, -40));
    // The way back west, to the bedroom door the scene waits at, stays open.
    CHECK(after->LevelsAt(-95, -46));
}

TEST_CASE("recheck: a prop the game moves is said once it comes to rest, with where it stood and stands")
{
    recheck::PropWatch door;
    recheck::Box before, after;
    const recheck::Box open{{900, -9300, 2550}, {910, -9180, 2790}};  // a leaf swung back against the wall
    const recheck::Box shut{{900, -9180, 2550}, {1010, -9170, 2790}}; // across the doorway
    // Seen first: no move.
    CHECK_FALSE(door.Update(0.0, open, before, after));
    CHECK_FALSE(door.Update(0.5, open, before, after));
    // A breath of animation is no move.
    recheck::Box breath = open;
    breath.high.x += 2.0;
    CHECK_FALSE(door.Update(1.0, breath, before, after));
    CHECK_FALSE(door.Update(1.5, open, before, after));
    // The slam: the leaf swings across, and stands.
    recheck::Box half{{900, -9250, 2550}, {980, -9180, 2790}};
    CHECK_FALSE(door.Update(2.0, half, before, after));
    CHECK_FALSE(door.Update(2.5, shut, before, after));
    CHECK_FALSE(door.Update(3.0, shut, before, after)); // still for half a second
    REQUIRE(door.Update(3.5, shut, before, after));     // still for kPropRest
    CHECK_FALSE(recheck::Moved(before, open, 0.5));
    CHECK_FALSE(recheck::Moved(after, shut, 0.5));
    // Said once.
    CHECK_FALSE(door.Update(4.0, shut, before, after));
    // Swung out and back to where it stood: nothing changed.
    CHECK_FALSE(door.Update(5.0, half, before, after));
    CHECK_FALSE(door.Update(5.5, shut, before, after));
    CHECK_FALSE(door.Update(7.0, shut, before, after));
    // Its collision put down (a prop gone to sleep) is a change too.
    CHECK_FALSE(door.Update(8.0, std::nullopt, before, after));
    REQUIRE(door.Update(9.5, std::nullopt, before, after));
    CHECK_FALSE(recheck::Moved(before, shut, 0.5));
    CHECK_FALSE(recheck::Moved(after, shut, 0.5));
    // A prop that swings and puts its collision down as it does comes to rest only once it has
    // stood without it for kPropRest.
    recheck::PropWatch sleeper;
    CHECK_FALSE(sleeper.Update(20.0, open, before, after));
    CHECK_FALSE(sleeper.Update(20.5, half, before, after));
    CHECK_FALSE(sleeper.Update(21.4, std::nullopt, before, after));
    CHECK_FALSE(sleeper.Update(21.9, std::nullopt, before, after));
    CHECK(sleeper.Update(22.5, std::nullopt, before, after));
    // A prop that never comes to rest is never said.
    recheck::PropWatch swinging;
    CHECK_FALSE(swinging.Update(0.0, open, before, after));
    bool said = false;
    for (int i = 1; i < 40; ++i)
        said = said || swinging.Update(i * 0.5, (i % 2) ? half : open, before, after);
    CHECK_FALSE(said);
}

TEST_CASE("recheck: a step counts where it starts or where it lands")
{
    std::map<recheck::StepKey, int> steps{{{0, 0, 1, 1, 0}, 1}, {{1, 0, 1, 0, 0}, 1}, {{5, 5, 1, 6, 5}, 1}};
    const auto in = recheck::StepsIn(steps, {{0, 0}});
    CHECK(in.size() == 2); // out of the column and into it
    CHECK(recheck::StepsIn(steps, {{6, 5}}).size() == 1);
    CHECK(recheck::StepsIn(steps, {{9, 9}}).empty());
}

TEST_CASE("recheck: the columns under a box and its margin")
{
    const recheck::Grid grid{recheck::Vec{0, 0, 0}, 20.0};
    const auto columns = recheck::ColumnsInBox(grid, recheck::Vec{100, 100, 0}, recheck::Vec{139, 119, 0}, 20.0);
    CHECK(columns.count({4, 4}));
    CHECK(columns.count({7, 6}));
    CHECK_FALSE(columns.count({8, 4}));
    CHECK_FALSE(columns.count({3, 4}));
    const auto u = recheck::Union(recheck::Box{{0, 0, 0}, {1, 1, 1}}, recheck::Box{{-1, 2, 0}, {0, 3, 4}});
    CHECK(u.low.x == -1.0);
    CHECK(u.high.y == 3.0);
    CHECK(u.high.z == 4.0);
}
