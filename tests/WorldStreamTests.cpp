// features/WorldStream: the world as the player finds it while the game streams an act's sections in
// and out, and what the ground felt out keeps of each answer at a change; with the asylum's hall of
// 2026-11-07 replayed over its recorded ground.
#include "Support.hpp"
#include "features/GridPath.hpp"
#include "features/GroundFile.hpp"
#include "features/WorldStream.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace da;

namespace
{
    // The names act4_2's streaming triggers load (LoadOnEntry): the sections' own levels.
    const std::set<std::wstring> kSections = {L"LV_Act_4_2_Section_03", L"LV_Act_4_2_Section_04"};

    std::wstring Wide(const std::string& narrow)
    {
        return std::wstring(narrow.begin(), narrow.end());
    }

    ground::StepKey KeyOf(const ground::Step& s)
    {
        return ground::StepKey{s.from.x, s.from.y, s.from.level, s.bx, s.by};
    }

    // Whether a way over the ground takes one of the steps given.
    // Whether a path takes one of the steps, in either way: a wall stands for both.
    bool Takes(const std::vector<grid::Cell>& path, const std::set<ground::StepKey>& steps)
    {
        for (size_t i = 1; i < path.size(); ++i)
        {
            const auto& a = path[i - 1];
            const auto& b = path[i];
            if (steps.count(ground::StepKey{a.x, a.y, a.level, b.x, b.y}) > 0 || steps.count(ground::StepKey{b.x, b.y, b.level, a.x, a.y}) > 0) return true;
        }
        return false;
    }
}

TEST_CASE("a wall a section's level held stands while the section is away and is asked again only once it is back: the asylum's hall, 2026-11-07")
{
    stream::World world;
    const std::vector<std::wstring> hall = {L"LV_Game", L"LV_Act_4_2_Section_03", L"LV_player_hard_collision_entryhall",
                                            L"LV_church_corridor_left_Structural_VTS_UP"};
    auto change = world.Count(hall, kSections, true);
    CHECK(change.fresh);
    const int level = world.Id(L"LV_player_hard_collision_entryhall");
    const stream::Basis wall{true, level};
    CHECK(stream::Askable(wall, world.Present(level), world.Streamed(level)));

    // Section_03 went as Mike walked to the basement's trigger, its parts with it.
    change = world.Count({L"LV_Game"}, kSections, false);
    CHECK_FALSE(change.fresh);
    CHECK(change.in.empty());
    CHECK(change.out.size() == 3);
    CHECK_FALSE(world.Present(level));
    CHECK(world.Streamed(level));
    CHECK(stream::Kept(wall, true, false, world.Present(level), world.Streamed(level)));
    CHECK_FALSE(stream::Askable(wall, world.Present(level), world.Streamed(level)));

    // Section_04 came in as he walked into its trigger: the hall's wall stands as he will find it.
    change = world.Count({L"LV_Game", L"LV_Act_4_2_Section_04", L"LV_player_hard_collision_basement"}, kSections, false);
    CHECK(change.in.size() == 2);
    CHECK(change.out.empty());
    CHECK(stream::Kept(wall, true, true, world.Present(level), world.Streamed(level)));
    CHECK_FALSE(stream::Askable(wall, world.Present(level), world.Streamed(level)));

    // Section_04 went, and Section_03 came back as he walked to the chapel door.
    change = world.Count({L"LV_Game"}, kSections, false);
    CHECK(change.out.size() == 2);
    change = world.Count(hall, kSections, false);
    CHECK(change.in.size() == 3);
    CHECK(world.Present(level));
    CHECK(stream::Kept(wall, true, true, world.Present(level), world.Streamed(level)));
    CHECK(stream::Askable(wall, world.Present(level), world.Streamed(level)));
}

TEST_CASE("what was found free is asked again once a level comes in, and stands while levels only go")
{
    const stream::Basis open; // rests on nothing in the way
    CHECK(stream::Kept(open, false, false, true, false));
    CHECK_FALSE(stream::Kept(open, false, true, true, false));
    CHECK(stream::Askable(open, true, false));
}

TEST_CASE("a floor found stands while levels only go and is asked again when one comes in, a wall stands either way")
{
    // A level come in may lay a floor above the one found, a stair over a ledge.
    const stream::Basis floor{true, 2};
    CHECK(stream::Kept(floor, false, false, true, false));
    CHECK_FALSE(stream::Kept(floor, false, true, true, false));
    // More of the world blocks no less.
    CHECK(stream::Kept(floor, true, true, true, false));
}

TEST_CASE("what a level gone with no section held is asked again: a scene's own level, gone for good")
{
    stream::World world;
    world.Count({L"LV_Game", L"LV_Scene_Props"}, kSections, true);
    const auto change = world.Count({L"LV_Game"}, kSections, false);
    CHECK(change.out.size() == 1);
    const int props = world.Id(L"LV_Scene_Props");
    CHECK_FALSE(world.Streamed(props));
    const stream::Basis wall{true, props};
    CHECK_FALSE(stream::Kept(wall, true, false, world.Present(props), world.Streamed(props)));
    CHECK(stream::Askable(wall, world.Present(props), world.Streamed(props)));
    // A thing whose level is not known counts as the world's own.
    const stream::Basis unknown{true, -1};
    CHECK(world.Present(-1));
    CHECK(stream::Askable(unknown, world.Present(-1), world.Streamed(-1)));
}

TEST_CASE("a level the streaming triggers name is the game's to bring back from the first count")
{
    stream::World world;
    world.Count({L"LV_Game", L"LV_Act_4_2_Section_03"}, kSections, true);
    CHECK(world.Streamed(world.Id(L"LV_Act_4_2_Section_03")));
    CHECK_FALSE(world.Streamed(world.Id(L"LV_Game")));
}

TEST_CASE("a checkpoint loaded anew is another world, though its levels keep their names")
{
    stream::World world;
    world.Count({L"LV_Game", L"LV_Act_4_2_Section_03"}, kSections, true);
    const auto again = world.Count({L"LV_Game", L"LV_Act_4_2_Section_03"}, kSections, true);
    CHECK(again.fresh);
    CHECK(again.in.size() == 2);
    CHECK(again.out.empty());
}

TEST_CASE("the world is felt out only once a change is carried through and has stood a moment")
{
    CHECK_FALSE(stream::Settled(true, false, true, 10.0));
    CHECK_FALSE(stream::Settled(false, true, true, 10.0));
    CHECK_FALSE(stream::Settled(false, false, false, 10.0));
    CHECK_FALSE(stream::Settled(false, false, true, stream::kSettle - 0.1));
    CHECK(stream::Settled(false, false, true, stream::kSettle));
}

TEST_CASE("the ways through the hall run round its walls while Section_03 is away and once it is back: the asylum's hall, 2026-11-07")
{
    // Mike stood in the hall at (28767, 29976) when Section_03 came back and walked to the chapel
    // door twice, circling at a corner of the hall at (28640, 30120) each time: the mod's ground had
    // asked again every wall it knew when Section_03 went, found 1306 steps through the hall's walls
    // free while they were away, and kept them free once they were back. The fixture holds the hall
    // as it was first felt out with Section_03 in (cut from research/dumps/ground-20261107-205052.tsv
    // by tools/ground/cut_world_change.py), those 1306 walls, the changes the game made, and the
    // free answers the old rules took while Section_03 was away. The record does not say which of
    // its 33 levels held each wall: the section's own level stands for them.
    std::ifstream in(std::filesystem::path(DA_REPO_ROOT) / "tests" / "fixtures" / "ground-asylum-hall-streamed.tsv");
    REQUIRE(in.good());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ground::Record record;
    REQUIRE(record.Parse(text));
    REQUIRE(record.anchors.size() == 1);
    REQUIRE(record.positions.size() == 1);
    REQUIRE(record.levels.size() == 4);
    const auto& anchor = record.anchors.front();

    std::set<ground::StepKey> walls;
    for (const auto& s : record.steps)
    {
        if (!s.free) walls.insert(KeyOf(s));
    }
    REQUIRE(walls.size() == 1306);
    // The hall as first felt out: what the T lines carry.
    std::map<ground::StepKey, int> hall = record.stepLevels;
    for (const auto& s : record.steps)
        hall.erase(KeyOf(s));

    // The levels the world held before Section_03 went: those that went, and the rest.
    std::set<std::wstring> held = {L"LV_Game"};
    for (const auto& name : record.levels.front().names)
        held.insert(Wide(name));
    stream::World world;
    world.Count(std::vector<std::wstring>(held.begin(), held.end()), kSections, true);

    // The answers the mod holds for those steps, as each change leaves them; an answer in the record
    // is taken where none is held, or the one held may be asked again.
    struct Answer
    {
        bool free = false;
        int land = 0;
        stream::Basis basis;
    };
    std::map<ground::StepKey, Answer> heldAnswers;
    size_t next = 0;
    const auto take = [&](size_t upto)
    {
        for (; next < upto && next < record.steps.size(); ++next)
        {
            const auto& s = record.steps[next];
            const auto known = heldAnswers.find(KeyOf(s));
            if (known != heldAnswers.end() &&
                !stream::Askable(known->second.basis, world.Present(known->second.basis.level), world.Streamed(known->second.basis.level)))
                continue;
            Answer a;
            a.free = s.free;
            a.land = s.land;
            if (!s.free && !s.on.empty()) a.basis = stream::Basis{true, world.Id(Wide(s.on))};
            heldAnswers[KeyOf(s)] = a;
        }
    };
    const auto change = [&](const ground::Levels& levels)
    {
        for (const auto& name : levels.names)
        {
            if (levels.in)
                held.insert(Wide(name));
            else
                held.erase(Wide(name));
        }
        const auto c = world.Count(std::vector<std::wstring>(held.begin(), held.end()), kSections, false);
        const bool cameIn = !c.in.empty();
        for (auto it = heldAnswers.begin(); it != heldAnswers.end();)
        {
            const auto& a = it->second;
            if (stream::Kept(a.basis, !a.free && a.basis.hit, cameIn, world.Present(a.basis.level), world.Streamed(a.basis.level)))
                ++it;
            else
                it = heldAnswers.erase(it);
        }
    };
    // The ground the search runs over: the hall, and the answers held.
    const auto ground = [&]
    {
        ground::Record over;
        over.stepLevels = hall;
        for (const auto& [key, a] : heldAnswers)
        {
            if (a.free) over.stepLevels[key] = a.land;
        }
        return over;
    };
    const grid::Cell start{static_cast<int>(std::floor((record.positions[0].x - anchor.x) / anchor.cell)),
                           static_cast<int>(std::floor((record.positions[0].y - anchor.y) / anchor.cell)), 173};
    // Where the walks to the chapel door ended their way (26258, 29666).
    const grid::Cell door{static_cast<int>(std::floor((26258.0 - anchor.x) / anchor.cell)), static_cast<int>(std::floor((29666.0 - anchor.y) / anchor.cell)),
                          173};
    const auto way = [&](const ground::Record& over, bool mirror = true)
    {
        grid::Search search(start, door, 1.5, 4, 400, 300000);
        const auto passable = over.Passable(nullptr, mirror);
        while (search.Step(256, passable) == grid::Search::State::Running)
        {
        }
        REQUIRE(search.Current() == grid::Search::State::Found);
        return search.Path();
    };

    // Section_03 goes, and the walls stand while the answers the old rules took come in.
    take(record.levels[0].steps);
    change(record.levels[0]);
    take(record.levels[1].steps);
    CHECK_FALSE(Takes(way(ground()), walls));
    // Section_04 comes and goes, Section_03 comes back.
    for (size_t i = 1; i < record.levels.size(); ++i)
    {
        take(record.levels[i].steps);
        change(record.levels[i]);
    }
    take(record.steps.size());
    CHECK_FALSE(Takes(way(ground()), walls));

    // The old rules' ground, every answer taken as it came and no step taken by the step back: the
    // way runs through the walls.
    ground::Record old;
    old.stepLevels = hall;
    for (const auto& s : record.steps)
    {
        if (s.free)
            old.stepLevels[KeyOf(s)] = s.land;
        else
            old.stepLevels.erase(KeyOf(s));
    }
    CHECK(Takes(way(old, false), walls));
}

TEST_CASE("the ground is laid once for a world and a body: the trail above the cable car's station, 2026-11-07")
{
    // research/sessions/s55-full.log: laid anew 66 m from its first spot at 23:08:20 after the station's
    // levels had gone, the new map walked through the station's walls and listed its things again.
    CHECK_FALSE(stream::Reanchors(true, true, 6597.0));
    CHECK_FALSE(stream::Reanchors(true, true, stream::kGridReach - 1.0));
    CHECK(stream::Reanchors(true, true, stream::kGridReach));
    CHECK(stream::Reanchors(false, true, 0.0));
    CHECK(stream::Reanchors(true, false, 0.0)); // another character's body
}
