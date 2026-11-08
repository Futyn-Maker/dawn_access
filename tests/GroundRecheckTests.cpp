#include "features/GroundRecheck.hpp"

#include "features/GridPath.hpp"
#include "features/GroundFile.hpp"
#include "features/WayList.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
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

namespace
{
    // The steps as the mod's ground keeps them: each step's last answer, its landing or refused.
    std::map<recheck::StepKey, int> StepStore(const ground::Record& record)
    {
        std::map<recheck::StepKey, int> store;
        for (const auto& s : record.steps)
            store[recheck::StepKey{s.from.x, s.from.y, s.from.level, s.bx, s.by}] = s.free ? s.land : recheck::kRefused;
        return store;
    }

    // The step back known free, by its landing, as Exploration's KnownFreeStep answers it.
    std::optional<int> MirrorIn(const std::map<recheck::StepKey, int>& store, const recheck::StepKey& key)
    {
        const auto& [ax, ay, alevel, bx, by] = key;
        return grid::MirroredLanding(
            [&](grid::Cell from, int x, int y) -> std::optional<int>
            {
                const auto it = store.find(recheck::StepKey{from.x, from.y, from.level, x, y});
                if (it == store.end() || it->second == recheck::kRefused) return std::nullopt;
                return it->second;
            },
            grid::Cell{ax, ay, alevel}, bx, by);
    }

    // A stretch asked again as Exploration asks it (StepReask): each step forgotten, then asked in
    // turn as GridStepFree answers, a step known free, the step back known free, else the sweep
    // (`sweep`); then each refusal answered again.
    recheck::Reask AskAgain(std::map<recheck::StepKey, int> store, const std::vector<std::pair<recheck::StepKey, int>>& kept,
                            const std::function<int(const recheck::StepKey&)>& sweep)
    {
        for (const auto& [key, answer] : kept)
            store.erase(key);
        const auto ask = [&](const recheck::StepKey& key) -> int
        {
            if (const auto it = store.find(key); it != store.end() && it->second != recheck::kRefused) return it->second;
            if (const auto back = MirrorIn(store, key)) return store[key] = *back;
            if (const auto it = store.find(key); it != store.end()) return recheck::kRefused;
            return store[key] = sweep(key);
        };
        recheck::Reask reask;
        reask.Begin(kept);
        while (const auto key = reask.Next())
            reask.Answer(ask(*key));
        reask.AnswerRefusalsAgain(
            [&](const recheck::StepKey& key) -> std::optional<int>
            {
                const int answer = ask(key);
                if (answer == recheck::kRefused) return std::nullopt;
                return answer;
            });
        return reask;
    }
}

TEST_CASE("recheck: a prop moved by a passage swept free one way only leaves the ground as it was: the cellar's cupboard door, 2026-11-08")
{
    // tests/fixtures/ground-cupboard-door-steps.tsv: every step the ground of the lodge's cellar
    // answered last about the cupboard door the ghost scare swung open (research/sessions/
    // s58-whole.log, 11:27:43: from (1372, -5200)-(1434, -5082) to (1315, -5089)-(1433, -5026)), the
    // stretch a move of it asks again (its boxes grown by her radius and a cell; cut by
    // research/tools/cut_steps.py from research/dumps/ground-20261108-110204.tsv, anchor 0).
    const auto record = ReadFixture("ground-cupboard-door-steps.tsv");
    const auto store = StepStore(record);
    REQUIRE(store.size() == 341);
    const recheck::Grid grid{recheck::Vec{463.79, -9065.50, 2647.11}, 20.0};
    const auto columns = recheck::ColumnsInBox(grid, recheck::Vec{1315, -5200, 2200}, recheck::Vec{1434, -5026, 2432}, 42.0 + 20.0);
    // From (41, 200) into (41, 201) the sweep met the activity level's collision at 6.6 cm; the step
    // back was swept free, onto the same level: the ground takes the step as free.
    const recheck::StepKey oneSided{41, 200, 43, 41, 201};
    REQUIRE(store.at(oneSided) == recheck::kRefused);
    REQUIRE(store.at(recheck::StepKey{41, 201, 43, 41, 200}) == 43);
    const auto back = [&](const recheck::StepKey& key) { return MirrorIn(store, key); };
    const auto kept = recheck::KeptAnswers(store, columns, false, back);
    REQUIRE(kept.size() == 341);
    const auto keptOf = [&](const std::vector<std::pair<recheck::StepKey, int>>& answers, const recheck::StepKey& key)
    {
        const auto it = std::find_if(answers.begin(), answers.end(), [&](const auto& a) { return a.first == key; });
        REQUIRE(it != answers.end());
        return it->second;
    };
    CHECK(keptOf(kept, oneSided) == 43);
    // A wall refused both ways stays refused.
    size_t refusedKept = 0;
    for (const auto& [key, answer] : kept)
        refusedKept += answer == recheck::kRefused;
    CHECK(refusedKept == 59);
    // The ground as it was: each sweep answers as it did.
    const auto same = [&](const recheck::StepKey& key) { return store.at(key); };
    const auto reask = AskAgain(store, kept, same);
    CHECK(reask.Changed().empty());
    // Two steps into (51, 194), whose floor the ground keyed on levels 43 and 44, were swept onto 44
    // and are answered 43 by the step back asked before them: free either way, no change.
    const auto relanded = std::count_if(reask.Steps().begin(), reask.Steps().end(),
                                        [](const recheck::Reask::Step& s) { return s.kept != recheck::kRefused && s.now != recheck::kRefused && s.kept != s.now; });
    CHECK(relanded == 2);
    CHECK(reask.Opened() == 0);
    CHECK(reask.Shut() == 0);
    // Weighed against the refusal the ground stored, the step back answering for it again would be
    // a door opened, the map dropped and the list given anew at every move by the doorway.
    const auto stored = recheck::KeptAnswers(store, columns, false, [](const recheck::StepKey&) { return std::optional<int>{}; });
    CHECK(keptOf(stored, oneSided) == recheck::kRefused);
    const auto misread = AskAgain(store, stored, same);
    CHECK(misread.Opened() == 1);
    // A leaf come across the passage, both ways refused: shut, and said.
    const auto across = [&](const recheck::StepKey& key)
    {
        const auto& [ax, ay, alevel, bx, by] = key;
        if (ax == 41 && bx == 41 && ((ay == 200 && by == 201) || (ay == 201 && by == 200))) return recheck::kRefused;
        return store.at(key);
    };
    const auto shut = AskAgain(store, kept, across);
    CHECK(shut.Shut() == 2);
    CHECK_FALSE(shut.Changed().empty());
    // A walk's stretch asks only what was free: the step taken free is asked again with them.
    const auto freeOnly = recheck::KeptAnswers(store, columns, true, back);
    CHECK(freeOnly.size() == 341 - 59);
    CHECK(keptOf(freeOnly, oneSided) == 43);
}

TEST_CASE("recheck: a door's leaf moved by the game is weighed whatever its collision; a prop that stops no one is not")
{
    // A door's leaf rests asleep with its collision off (UBatesSleepComponent::ApplySleepSettings):
    // the ground takes it for a wall all the same, and a sequence that swings it open opens a way.
    CHECK(recheck::Weighed(true, false));
    CHECK(recheck::Weighed(true, true));
    // The cellar's cupboard door, its PhysicsActor blocking every channel.
    CHECK(recheck::Weighed(false, recheck::StopsPawn(3, 2)));
    // Chris's torch in his hand (HeldItemActor overlaps a pawn): its 338 moves change nothing.
    CHECK_FALSE(recheck::Weighed(false, recheck::StopsPawn(1, 1)));
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

TEST_CASE("recheck: a prop's bounds that read wider than any prop are no reading: act6_1's lift cog, 2026-11-08")
{
    // BP_Cog_lift stood (-30301, 5107, -8091) to (-30101, 5323, -6198): 2 by 2 m, 19 m high. As the
    // lift began to go down its colliding bounds read (-130334, -102550, -953639) to (69933, 112980,
    // 939349), and the ground under that box was laid out column by column in one frame: the game
    // stood 62 s (research/sessions/s59-full.log 16:11:58 to 16:13:01).
    const recheck::Box cog{{-30301, 5107, -8091}, {-30101, 5323, -6198}};
    const recheck::Box blown{{-130334, -102550, -953639}, {69933, 112980, 939349}};
    CHECK(recheck::PropFills(cog));
    CHECK_FALSE(recheck::PropFills(blown));
    // The cupboard door of the cellar and a lift's cage fill their place.
    CHECK(recheck::PropFills(recheck::Box{{463, -9065, 2647}, {695, -9040, 2850}}));
    CHECK(recheck::PropFills(recheck::Box{{0, 0, 0}, {400, 400, 300}}));
    recheck::PropWatch watch;
    recheck::Box before, after;
    CHECK_FALSE(watch.Update(0.0, cog, before, after));
    // The blown readings stand for nothing, however long they last.
    bool said = false;
    for (int i = 1; i <= 10; ++i)
        said = said || watch.Update(i * 0.5, blown, before, after);
    CHECK_FALSE(said);
    // Read where it stood again: it never moved.
    CHECK_FALSE(watch.Update(6.0, cog, before, after));
    CHECK_FALSE(watch.Update(8.0, cog, before, after));
    // Read at its place down the shaft after the blown readings: a move from where it stood, said once
    // it stands.
    recheck::Box down = cog;
    down.low.z -= 1500.0;
    down.high.z -= 1500.0;
    CHECK_FALSE(watch.Update(9.0, blown, before, after));
    CHECK_FALSE(watch.Update(9.5, down, before, after));
    CHECK_FALSE(watch.Update(10.0, down, before, after));
    REQUIRE(watch.Update(10.6, down, before, after));
    CHECK_FALSE(recheck::Moved(before, cog, 0.5));
    CHECK_FALSE(recheck::Moved(after, down, 0.5));
}

TEST_CASE("recheck: the ground asked again under a moved prop is where it stood and where it stands, not the box holding both")
{
    const recheck::Grid grid{recheck::Vec{0, 0, 0}, 20.0};
    // A leaf carried 40 m off: two door-sized places, not the 40 m between.
    const recheck::Box was{{100, 100, 0}, {300, 120, 200}};
    const recheck::Box gone{{4100, 100, 0}, {4300, 120, 200}};
    const auto columns = recheck::ColumnsUnder(grid, was, gone, 20.0);
    CHECK(columns.count({5, 5}));
    CHECK(columns.count({210, 5}));
    CHECK_FALSE(columns.count({100, 5}));
    const auto apart = recheck::ColumnsInBox(grid, was.low, was.high, 20.0).size() + recheck::ColumnsInBox(grid, gone.low, gone.high, 20.0).size();
    CHECK(columns.size() == apart);
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

TEST_CASE("recheck: a prop moved is weighed by the answers of the steps about it, asked again: Chris's torch and the cinema's door")
{
    // research/sessions/s58-whole.log, 11:10:34: Chris's torch, carried about the cellar, "moved" from
    // (1034, -6363)-(1060, -6323) to (1001, -6345)-(1042, -6307), one of 338 such moves of the
    // companions' torches in the session, each of which had forgotten the ground about it and given
    // the list's verdicts anew. The torch is a held item: its preset overlaps a pawn and stops no one.
    CHECK_FALSE(recheck::StopsPawn(3, 1)); // HeldItemActor: QueryAndPhysics, overlaps the pawn
    CHECK_FALSE(recheck::StopsPawn(3, 0)); // HeldItemPhysicsActor: ignores the pawn
    CHECK_FALSE(recheck::StopsPawn(0, 2)); // no collision
    CHECK_FALSE(recheck::StopsPawn(2, 2)); // physics only: no query meets it
    CHECK(recheck::StopsPawn(3, 2));       // a door's PhysicsActor blocks
    CHECK(recheck::StopsPawn(1, 2));
    // Asked again all the same, the ground about it answers as it did: nothing has changed.
    auto cellar = ReadFixture("ground-cellar-chris.tsv");
    const recheck::Grid cellarGrid{recheck::Vec{463.79, -9065.50, 2647.11}, 20.0};
    const auto torch = recheck::Union(recheck::Box{{1034, -6363, 2282}, {1060, -6323, 2308}}, recheck::Box{{1001, -6345, 2281}, {1042, -6307, 2305}});
    const auto about = recheck::ColumnsInBox(cellarGrid, torch.low, torch.high, 42.0 + 20.0);
    std::vector<std::pair<recheck::StepKey, int>> kept;
    for (const auto& key : recheck::StepsIn(cellar.stepLevels, about))
        kept.emplace_back(key, cellar.stepLevels.at(key));
    REQUIRE(kept.size() > 100);
    recheck::Reask torchAsked;
    torchAsked.Begin(kept);
    while (const auto key = torchAsked.Next())
        torchAsked.Answer(cellar.stepLevels.at(*key));
    CHECK_FALSE(torchAsked.Active());
    CHECK(torchAsked.Changed().empty());
    CHECK(torchAsked.Shut() == 0);
    // The cinema's exit door slammed across its doorway (s57): the steps across it answer shut now.
    auto airlock = ReadFixture("ground-cinema-airlock-slam.tsv");
    const recheck::Grid grid{recheck::Vec{2075.21, -8106.85, 3060.15}, 20.0};
    const auto door = recheck::ColumnsInBox(grid, recheck::Vec{900, -9180, 2550}, recheck::Vec{1010, -9140, 2790}, 42.0 + 20.0);
    kept.clear();
    for (const auto& key : recheck::StepsIn(airlock.stepLevels, door))
        kept.emplace_back(key, airlock.stepLevels.at(key));
    recheck::Reask doorAsked;
    doorAsked.Begin(kept);
    while (const auto key = doorAsked.Next())
        doorAsked.Answer(ShutByDoor(*key) ? recheck::kRefused : airlock.stepLevels.at(*key));
    CHECK(doorAsked.Shut() > 0);
    CHECK(doorAsked.Opened() == 0);
    CHECK_FALSE(doorAsked.Changed().empty());
    // A door swung open: what was refused answers free now.
    recheck::Reask opened;
    opened.Begin({{recheck::StepKey{-58, -52, 51, -57, -52}, recheck::kRefused}, {recheck::StepKey{-57, -52, 51, -58, -52}, 51}});
    opened.Answer(51);
    opened.Answer(51);
    CHECK(opened.Opened() == 1);
    CHECK(opened.Changed() == std::vector<recheck::Column>{{-58, -52}});
    // Asking nothing is no asking.
    recheck::Reask none;
    none.Begin({});
    CHECK_FALSE(none.Active());
    CHECK_FALSE(none.Next().has_value());
}

TEST_CASE("ways: a way round a companion standing at a corridor's mouth: Chris in the cellar, 2026-11-08")
{
    // research/sessions/s58-whole.log, 11:13:49, 11:15:40 and 11:18:20: every walk from the wine
    // cellar's door to the creaky door stopped at (986, -6292) against Chris, standing at
    // (1067, -6316) for five minutes, 84 cm from her (both capsules 42 cm): the map's road, laid
    // with the characters left out, ran through where he stood. tests/fixtures/ground-cellar-chris.tsv
    // is the cellar as the mod's ground knew it (cut from research/dumps/ground-20261108-110204.tsv).
    auto record = ReadFixture("ground-cellar-chris.tsv");
    const recheck::Vec origin{463.79, -9065.50, 0.0};
    const auto middle = [&](const grid::Cell& c) { return recheck::Vec{origin.x + (c.x + 0.5) * 20.0, origin.y + (c.y + 0.5) * 20.0, 0.0}; };
    const auto cellOf = [&](double x, double y) { return grid::Cell{static_cast<int>(std::floor((x - origin.x) / 20.0)), static_cast<int>(std::floor((y - origin.y) / 20.0)), 43}; };
    const grid::Cell her = cellOf(986, -6292);
    const recheck::Vec chris{1067, -6316, 0};
    // The corridor to the creaky door, a few metres on.
    const grid::Cell corridor = cellOf(1534, -5456);
    const auto passable = record.Passable();
    auto plain = std::make_unique<grid::Flood>(her, 200, 100000);
    while (plain->Step(64, passable) == grid::Flood::State::Running)
    {
    }
    REQUIRE(plain->LevelsAt(corridor.x, corridor.y));
    // The road the map gives runs within his reach: the walk stops against him.
    const auto road = plain->PathTo(corridor);
    REQUIRE_FALSE(road.empty());
    ways::KeepClear keep{{chris}, 42.0 + 42.0 + ways::kKeepClearMargin};
    CHECK(std::any_of(road.begin(), road.end(), [&](const grid::Cell& c) { return !keep.Clear(middle(c)); }));
    // Kept clear of him, the ground still carries her round his far side to the corridor.
    const auto around = [&](grid::Cell a, grid::Cell& b) { return keep.StepClear(middle(a), middle(b)) && passable(a, b); };
    auto detour = std::make_unique<grid::Flood>(her, 200, 100000);
    while (detour->Step(64, around) == grid::Flood::State::Running)
    {
    }
    REQUIRE(detour->LevelsAt(corridor.x, corridor.y));
    const auto way = detour->PathTo(corridor);
    REQUIRE_FALSE(way.empty());
    // Out of his reach once she has stepped away from him, the way never comes near him again.
    bool out = false;
    for (const auto& c : way)
    {
        if (keep.Clear(middle(c))) out = true;
        if (out) CHECK(keep.Clear(middle(c)));
    }
    CHECK(out);
    // A straight run past him is no run of the way round.
    CHECK_FALSE(keep.RunClear(recheck::Vec{986, -6292, 0}, recheck::Vec{1154, -6396, 0}));
    CHECK(keep.RunClear(recheck::Vec{880, -6292, 0}, recheck::Vec{880, -6500, 0}));
    // A step toward him from within his reach is refused; one away from him is not, though it ends
    // within his reach still: pressed against him, she walks out of it a step at a time.
    CHECK_FALSE(keep.StepClear(recheck::Vec{986, -6292, 0}, recheck::Vec{1006, -6292, 0}));
    CHECK(keep.StepClear(recheck::Vec{986, -6292, 0}, recheck::Vec{966, -6292, 0}));
    CHECK(keep.StepClear(recheck::Vec{1007, -6316, 0}, recheck::Vec{987, -6316, 0}));
    CHECK_FALSE(keep.StepClear(recheck::Vec{987, -6316, 0}, recheck::Vec{1007, -6316, 0}));
}

namespace
{
    // Whether a flood reaches the entrails' place in act6_4: the use location stands at (7290, -3651),
    // column (136, 201) of the grid laid at (4551.18, -7686.20).
    bool ReachesEntrails(const grid::Flood& flood)
    {
        for (int x = 130; x <= 140; ++x)
        {
            for (int y = 196; y <= 206; ++y)
            {
                if (flood.LevelsAt(x, y)) return true;
            }
        }
        return false;
    }
}

TEST_CASE("recheck: a thing carried is no door's leaf: the shotgun in Chris's hands walled him in at the scarecrow, 2026-11-08")
{
    // tests/fixtures/ground-scarecrow-shotgun.tsv: the ground between the scarecrow and the entrails
    // as the session's record had it at its end, cut by research/tools/cut_ground.py from where Chris
    // stood after the scarecrow (6663, -3611) out of research/dumps/ground-20261108-181921.tsv, and
    // the "leaf" the map laid out at 18:40:16: the two capsules of BP_Shotgun_jack_C_0, the shotgun
    // he carried, which the locked kitchen door's interaction had bound for its animation.
    auto record = ReadFixture("ground-scarecrow-shotgun.tsv");
    const grid::Cell scarecrow{105, 203, 56};
    // The ground as it is: the entrails 6 m on are reached from where he stood.
    CHECK(ReachesEntrails(*Flood(record, scarecrow)));
    // The shotgun taken for the door's leaf: every step whose sweep it touches refused, as the map's
    // sweeps refused them ('D'), from the cells' middles as the grid lays them (the anchor's 20 cm
    // cells, half height 100, radius 42, step 45).
    REQUIRE(record.volumes.count("door:I_door_kitchenexterior_locked"));
    recheck::Leaf shotgun;
    shotgun.shape = record.volumes.at("door:I_door_kitchenexterior_locked");
    shotgun.shape.Bounds(shotgun.low, shotgun.high);
    const double radius = 42.0, half = 100.0, step = 45.0;
    const double sweepHalf = std::max(25.0, half - step / 2.0);
    const auto middle = [&](int x, int y, int level)
    {
        const auto floor = record.floors.find({x, y, level});
        REQUIRE(floor != record.floors.end());
        return recheck::Vec{4551.18 + (x + 0.5) * 20.0, -7686.20 + (y + 0.5) * 20.0, floor->second + half + step / 2.0};
    };
    std::vector<recheck::StepKey> shut;
    for (const auto& [key, land] : record.stepLevels)
    {
        const auto& [ax, ay, alevel, bx, by] = key;
        if (record.floors.count({bx, by, land}) == 0) continue;
        if (recheck::LeafTouches(shotgun, middle(ax, ay, alevel), middle(bx, by, land), radius, sweepHalf)) shut.push_back(key);
    }
    // Among them the steps out of his own cell east and north, which the record refused so at
    // 18:40:16 (S 105 203 56 106 203 X D, S 105 203 56 105 204 X D), and the way on north-east the
    // map would have taken: the leaf stood between him and the corridor to the entrails. (The record
    // keeps only part of the map's sweeps, and the refusals the leaf left on the corridor itself at
    // 18:40:02, where he stood with it, are not all in it: the whole closure cannot be replayed.)
    CHECK(std::find(shut.begin(), shut.end(), recheck::StepKey{105, 203, 56, 106, 203}) != shut.end());
    CHECK(std::find(shut.begin(), shut.end(), recheck::StepKey{105, 203, 56, 105, 204}) != shut.end());
    CHECK(std::find(shut.begin(), shut.end(), recheck::StepKey{105, 203, 56, 106, 204}) != shut.end());
    // A held item is no door's leaf; the door's own prop is.
    CHECK_FALSE(recheck::LeafOfDoor(true));
    CHECK(recheck::LeafOfDoor(false));
}
