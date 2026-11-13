// features/Rooms: the rooms the game's reverb volumes lay out, their names, the character crossing
// from one into another, and the room rule by which the exploration list keeps to her area; with
// every reverb volume the game ships, the room names the table holds, and the lodge's yard of
// 2026-11-02 replayed over its recorded ground.
#include "Support.hpp"
#include "features/GridPath.hpp"
#include "features/GroundFile.hpp"
#include "features/Rooms.hpp"
#include "locale/Locale.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace da;
using rooms::Vec;
using tests::U8;

namespace
{
    // Every reverb volume of the game's levels (the 21 acts' LV_reverb, research/triggers/audio).
    const wchar_t* const kEveryVolume[] = {
        L"T_GlobalReverb_Exterior",
        L"T_GlobalReverb_Interior",
        L"T_GlobalReverb_Lodge",
        L"T_GlobalReverb_LodgeInterior",
        L"T_GlobalReverb_Mine",
        L"T_GlobalReverb_MineExterior",
        L"T_ReverbGlobal_Exterior",
        L"T_ReverbGlobal_Mine",
        L"T_Reverb_BeautySpot",
        L"T_Reverb_Bedroom",
        L"T_Reverb_BeforeCoveredBridge",
        L"T_Reverb_BridgeToFallenTree",
        L"T_Reverb_Cabin",
        L"T_Reverb_CabinToMinetown",
        L"T_Reverb_CabinToMinetown_Enclosed",
        L"T_Reverb_Cabin_LivingRoom",
        L"T_Reverb_Cellar",
        L"T_Reverb_CellarStairs",
        L"T_Reverb_Cellar_Corridors",
        L"T_Reverb_Cellar_DollHouse_Exit",
        L"T_Reverb_Cellar_ExitRoom",
        L"T_Reverb_Cellar_Main",
        L"T_Reverb_Cellar_To_KitchenStairs",
        L"T_Reverb_Cellar_To_KitchenStairs1",
        L"T_Reverb_Cellar_To_KitchenStairs2",
        L"T_Reverb_Cellar_To_Kitchen_Elevator",
        L"T_Reverb_Cellar_WineRack_Corridor",
        L"T_Reverb_Cinema",
        L"T_Reverb_CinemaAirlock",
        L"T_Reverb_CinemaAirlock1",
        L"T_Reverb_CinemaAirlock2",
        L"T_Reverb_ControlRoom",
        L"T_Reverb_CoveredBridge",
        L"T_Reverb_Deathroom",
        L"T_Reverb_Deathroom1",
        L"T_Reverb_Deathroom2",
        L"T_Reverb_Deathroom3",
        L"T_Reverb_Deathroom4",
        L"T_Reverb_Deathroom5",
        L"T_Reverb_DiningRoom",
        L"T_Reverb_DollHouse",
        L"T_Reverb_FallenTree",
        L"T_Reverb_GeneratorShed",
        L"T_Reverb_Global_Basement",
        L"T_Reverb_Hall",
        L"T_Reverb_Hall_Airlock",
        L"T_Reverb_Hall_Lower",
        L"T_Reverb_HangingShed",
        L"T_Reverb_HannahsBedroom",
        L"T_Reverb_HotTub",
        L"T_Reverb_HotTubCorridor",
        L"T_Reverb_Kitchen",
        L"T_Reverb_Kitchen2",
        L"T_Reverb_Kitchen3",
        L"T_Reverb_Kitchen4",
        L"T_Reverb_KitchenBalcony",
        L"T_Reverb_KitchenBalconyCorridor",
        L"T_Reverb_KitchenBalcony_1",
        L"T_Reverb_KitchenBalcony_2",
        L"T_Reverb_KitchenCorridor_To_Kitchen",
        L"T_Reverb_KitchenCorridor_To_Kitchen1",
        L"T_Reverb_KitchenStairs_To_KitchenCorridor",
        L"T_Reverb_KitchenStairs_To_KitchenCorridor1",
        L"T_Reverb_Kitchen_1",
        L"T_Reverb_Kitchen_2",
        L"T_Reverb_Kitchen_3",
        L"T_Reverb_Kitchen_Airlock",
        L"T_Reverb_Kitchen_To_PigRoom",
        L"T_Reverb_Kitchen_To_PigRoom1",
        L"T_Reverb_Larder",
        L"T_Reverb_LarderToSkiview_Exterior",
        L"T_Reverb_LarderToSkiview_LiftBase",
        L"T_Reverb_LarderToSkiview_LiftShaft",
        L"T_Reverb_LarderToSkiview_Tunnel",
        L"T_Reverb_Library",
        L"T_Reverb_LibraryHiddenRoom",
        L"T_Reverb_Library_01",
        L"T_Reverb_Library_02",
        L"T_Reverb_LodgeExterior_KitchenToHangingShed",
        L"T_Reverb_LodgeExterior_RightSide",
        L"T_Reverb_LodgeInterior_Garage",
        L"T_Reverb_LodgeInterior_GarageAirlock",
        L"T_Reverb_LodgeToStoneBridge",
        L"T_Reverb_Lodge_Exterior_KitchenVeranda",
        L"T_Reverb_LookOut",
        L"T_Reverb_LookOutToFallenTree",
        L"T_Reverb_MineChaseInterior_Lift",
        L"T_Reverb_MineChaseInterior_Tunnels_1",
        L"T_Reverb_MineChaseInterior_Tunnels_2",
        L"T_Reverb_MineChase_Shimmy",
        L"T_Reverb_MineDecentBuilding",
        L"T_Reverb_MineDecent_Tunnels",
        L"T_Reverb_MineExitHole",
        L"T_Reverb_MineExterior_Hillside",
        L"T_Reverb_MineExterior_RockShute",
        L"T_Reverb_MineExterior_ShackExterior_1",
        L"T_Reverb_MineExterior_ShackExterior_2",
        L"T_Reverb_MineExterior_Shack_1",
        L"T_Reverb_MineExterior_Shack_2",
        L"T_Reverb_MineToLookOut",
        L"T_Reverb_Mine_LiftShaft",
        L"T_Reverb_Mine_LiftShaft_Combat",
        L"T_Reverb_Mine_LookOutTunnel_Entrance",
        L"T_Reverb_Mine_LookOutTunnel_Main",
        L"T_Reverb_Minetown_Building_1",
        L"T_Reverb_Minetown_Building_2",
        L"T_Reverb_Minetown_Building_3",
        L"T_Reverb_Minetown_High",
        L"T_Reverb_Minetown_Open_1",
        L"T_Reverb_Minetown_Open_2",
        L"T_Reverb_Minetown_OutdoorEnclosed",
        L"T_Reverb_OldHotel_Kitchen",
        L"T_Reverb_OldHotel_Projector_room",
        L"T_Reverb_OutsideLodge",
        L"T_Reverb_PathToBeautySpot",
        L"T_Reverb_PathToBeautySpot_Enclosed",
        L"T_Reverb_PathToCoveredBridge",
        L"T_Reverb_PathToSnowman",
        L"T_Reverb_PigRoom",
        L"T_Reverb_PigRoom_To_PsychoRoom",
        L"T_Reverb_PigRoom_To_PsychoRoom1",
        L"T_Reverb_PigRoom_To_PsychoRoom2",
        L"T_Reverb_PigRoom_To_PsychoRoom3",
        L"T_Reverb_PsychoCorridor",
        L"T_Reverb_PsychoCorridor1",
        L"T_Reverb_PsychoCorridor2",
        L"T_Reverb_PsychoCorridor4",
        L"T_Reverb_PsychoRoom",
        L"T_Reverb_PsychoRoom1",
        L"T_Reverb_PsychoRoom2",
        L"T_Reverb_RiverToShack",
        L"T_Reverb_SamsRoom",
        L"T_Reverb_SanatoriumExterior_RearBuilding",
        L"T_Reverb_Sanatorium_BoilerRoomRear",
        L"T_Reverb_Sanatorium_ServiceTunnel",
        L"T_Reverb_ScenicView",
        L"T_Reverb_ScenicView_Telescope",
        L"T_Reverb_ServiceTunnelToSanatorium_Chamber",
        L"T_Reverb_ServiceTunnelToSanatorium_Pipe",
        L"T_Reverb_ServiceTunnelToSanatorium_Tunnels",
        L"T_Reverb_Shack",
        L"T_Reverb_ShackRiverCrossing",
        L"T_Reverb_ShackToCabin",
        L"T_Reverb_SkiStation_Exterior",
        L"T_Reverb_SkiStation_Internal",
        L"T_Reverb_SnowmanArea",
        L"T_Reverb_StoneBridgeToMine",
        L"T_Reverb_TowerToSanatoriumview_A",
        L"T_Reverb_TowerToSanatoriumview_B",
        L"T_Reverb_library_stairs",
    };

    // The reverb volumes of the levels of chapters 1 and 2 (act2_11, act2_3, act2_4; the other acts
    // of the two chapters have none).
    const wchar_t* const kChapters12[] = {
        L"T_Reverb_Bedroom",
        L"T_Reverb_PathToCoveredBridge",
        L"T_Reverb_BeautySpot",
        L"T_Reverb_HotTub",
        L"T_Reverb_PathToBeautySpot_Enclosed",
        L"T_Reverb_ScenicView",
        L"T_Reverb_library_stairs",
        L"T_Reverb_CoveredBridge",
        L"T_GlobalReverb_LodgeInterior",
        L"T_Reverb_SkiStation_Internal",
        L"T_Reverb_Hall_Airlock",
        L"T_Reverb_Kitchen_1",
        L"T_Reverb_PathToBeautySpot",
        L"T_Reverb_Kitchen_3",
        L"T_Reverb_Cinema",
        L"T_Reverb_LodgeInterior_GarageAirlock",
        L"T_Reverb_HotTubCorridor",
        L"T_Reverb_LodgeInterior_Garage",
        L"T_Reverb_ScenicView_Telescope",
        L"T_Reverb_SkiStation_Exterior",
        L"T_Reverb_Kitchen_2",
        L"T_Reverb_Library",
        L"T_GlobalReverb_Exterior",
        L"T_Reverb_Hall",
        L"T_Reverb_OutsideLodge",
        L"T_Reverb_BeforeCoveredBridge",
        L"T_Reverb_Hall_Lower",
        L"T_Reverb_SnowmanArea",
        L"T_Reverb_PathToSnowman",
        L"T_Reverb_KitchenBalcony",
        L"T_Reverb_Kitchen2",
        L"T_Reverb_Cellar",
        L"T_Reverb_HannahsBedroom",
        L"T_Reverb_Kitchen",
        L"T_Reverb_CellarStairs",
        L"T_Reverb_Library_01",
        L"T_Reverb_Global_Basement",
        L"T_Reverb_Kitchen3",
    };

    // A box square with the world as a zone, with the name the table would give it.
    rooms::Zone BoxZone(const wchar_t* name, Vec low, Vec high, const wchar_t* spoken)
    {
        shapes::Shape shape;
        shape.hulls.push_back(shapes::BoxHull(Vec{(low.x + high.x) / 2.0, (low.y + high.y) / 2.0, (low.z + high.z) / 2.0},
                                              Vec{(high.x - low.x) / 2.0, (high.y - low.y) / 2.0, (high.z - low.z) / 2.0}, shapes::Quat{}));
        auto zone = rooms::MakeZone(name, shape);
        zone.spoken = spoken;
        return zone;
    }

    // A lodge of three: a broad volume under the house, a hall from x 0 to 600 and a kitchen from
    // x 580 to 1000, their volumes overlapping by 20 cm at the doorway, and the kitchen's two parts
    // (one name) side by side.
    std::vector<rooms::Zone> Lodge()
    {
        return {
            BoxZone(L"T_GlobalReverb_LodgeInterior", {-100, -500, 0}, {1600, 500, 400}, L"Lodge"),
            BoxZone(L"T_Reverb_Hall", {0, -400, 0}, {600, 400, 300}, L"Hall"),
            BoxZone(L"T_Reverb_Kitchen_1", {580, -400, 0}, {800, 400, 300}, L"Kitchen"),
            BoxZone(L"T_Reverb_Kitchen_2", {800, -400, 0}, {1000, 400, 300}, L"Kitchen"),
            BoxZone(L"T_Reverb_Cellar", {1100, -400, 0}, {1500, 400, 300}, L""),
        };
    }

    int LevelOf(double z)
    {
        return static_cast<int>(std::floor(z / 50.0));
    }
}

TEST_CASE("a reverb volume's name gives its room's key, the parts of one room one key")
{
    bool broad = true;
    CHECK(U8(rooms::Key(L"T_Reverb_Kitchen_1", &broad)) == "kitchen");
    CHECK_FALSE(broad);
    CHECK(U8(rooms::Key(L"T_Reverb_Kitchen3")) == "kitchen");
    CHECK(U8(rooms::Key(L"T_Reverb_Kitchen")) == "kitchen");
    CHECK(U8(rooms::Key(L"T_Reverb_Library_01")) == "library");
    CHECK(U8(rooms::Key(L"T_Reverb_library_stairs")) == "library_stairs");
    CHECK(U8(rooms::Key(L"T_Reverb_Hall_Airlock")) == "hall_airlock");
    CHECK(U8(rooms::Key(L"T_Reverb_HannahsBedroom")) == "hannahs_bedroom");
    CHECK(U8(rooms::Key(L"T_Reverb_PathToBeautySpot_Enclosed")) == "path_to_beauty_spot_enclosed");
    CHECK(U8(rooms::Key(L"T_Reverb_LodgeInterior_GarageAirlock")) == "lodge_interior_garage_airlock");
    CHECK(U8(rooms::Key(L"T_Reverb_Minetown_Open_2")) == "minetown_open");
    CHECK(U8(rooms::Key(L"T_Reverb_TowerToSanatoriumview_A")) == "tower_to_sanatoriumview");
    CHECK(U8(rooms::Key(L"T_Reverb_OldHotel_Projector_room")) == "old_hotel_projector_room");
    CHECK(U8(rooms::Key(L"T_Reverb_Mine_LookOutTunnel_Main")) == "mine_look_out_tunnel_main");
    // The broad volumes laid under the rooms, by all three spellings the levels use.
    CHECK(U8(rooms::Key(L"T_GlobalReverb_LodgeInterior", &broad)) == "lodge_interior");
    CHECK(broad);
    CHECK(U8(rooms::Key(L"T_ReverbGlobal_Mine", &broad)) == "mine");
    CHECK(broad);
    CHECK(U8(rooms::Key(L"T_Reverb_Global_Basement", &broad)) == "basement");
    CHECK(broad);
    // Volumes that are no reverb volume.
    CHECK(rooms::Key(L"T_Cam_bridge_to_mine_08").empty());
    CHECK(rooms::Key(L"T_window_rattle").empty());
    CHECK(rooms::Key(L"BP_CharacterTriggerVolume_C_5").empty());
    CHECK(rooms::Key(L"").empty());
}

TEST_CASE("a numbered part of a room is looked up with its number first: the old hotel's kitchen, 2026-11-09")
{
    const auto keys = [](const wchar_t* name)
    {
        std::vector<std::string> out;
        for (const auto& key : rooms::NameKeys(name))
            out.push_back(U8(key));
        return out;
    };
    CHECK(keys(L"T_Reverb_Kitchen4") == std::vector<std::string>{"kitchen_4", "kitchen"});
    CHECK(keys(L"T_Reverb_Kitchen_1") == std::vector<std::string>{"kitchen_1", "kitchen"});
    CHECK(keys(L"T_Reverb_TowerToSanatoriumview_A") == std::vector<std::string>{"tower_to_sanatoriumview_a", "tower_to_sanatoriumview"});
    CHECK(keys(L"T_Reverb_Hall") == std::vector<std::string>{"hall"});
    CHECK(keys(L"T_GlobalReverb_LodgeInterior") == std::vector<std::string>{"lodge_interior"});
    CHECK(keys(L"T_Cam_bridge_to_mine_08").empty());
    CHECK(keys(L"").empty());
}

TEST_CASE("every reverb volume the game ships gives a room, and every room has a name")
{
    std::set<std::string> keys;
    for (const wchar_t* name : kEveryVolume)
    {
        const auto key = rooms::Key(name);
        CAPTURE(U8(name));
        CHECK_FALSE(key.empty());
        CHECK(key.find(L"__") == std::wstring::npos);
        CHECK(std::none_of(key.begin(), key.end(), [](wchar_t c) { return c >= L'A' && c <= L'Z'; }));
        keys.insert(U8(key));
    }
    // 150 volumes, 109 rooms: the parts of a room are one.
    CHECK(keys.size() < std::size(kEveryVolume));

    // The table of room names, as the mod ships it, in English and in Russian (scripts\check-lang.ps1
    // holds the other languages to the English keys): every room of every chapter is named, by the
    // first of its keys the table names, as the mod looks it up.
    const auto lang = (std::filesystem::path(DA_REPO_ROOT) / "mod" / "lang").wstring();
    const auto nameOf = [](const wchar_t* volume)
    {
        for (const auto& key : rooms::NameKeys(volume))
        {
            if (locale::Has(L"room." + key)) return U8(locale::Mod(L"room." + key));
        }
        return std::string();
    };
    for (const wchar_t* code : {L"en_US", L"ru_RU"})
    {
        REQUIRE(locale::LoadTables(lang, code));
        for (const wchar_t* name : kEveryVolume)
        {
            CAPTURE(U8(name));
            CHECK_FALSE(nameOf(name).empty());
        }
        for (const wchar_t* name : kChapters12)
        {
            CAPTURE(U8(name));
            CHECK(locale::Has(L"room." + rooms::Key(name)));
        }
    }
    CHECK(nameOf(L"T_Reverb_Kitchen2") == "Кухня");
    CHECK(nameOf(L"T_Reverb_Hall_Airlock") == "Тамбур");
    // The old hotel's kitchen under the lodge, whatever the part laid over it is called.
    CHECK(nameOf(L"T_Reverb_Kitchen4") == "Кухня старого отеля");
    CHECK(nameOf(L"T_Reverb_OldHotel_Kitchen") == "Кухня старого отеля");
    CHECK(nameOf(L"T_Reverb_PsychoRoom1") == "Мастерская");
    REQUIRE(locale::LoadTables(lang, L"en_US"));
    CHECK(nameOf(L"T_Reverb_Kitchen4") == "Old hotel kitchen");
}

TEST_CASE("a point lies in the narrowest zone that holds it")
{
    const auto lodge = Lodge();
    CHECK(rooms::ZoneAt(lodge, {300, 0, 100}) == 1);   // the hall, within the broad volume
    CHECK(rooms::ZoneAt(lodge, {590, 0, 100}) == 2);   // the doorway, in both: the kitchen's part is the narrower
    CHECK(rooms::ZoneAt(lodge, {1050, 0, 100}) == 0);  // between the kitchen and the cellar: the broad volume alone
    CHECK(rooms::ZoneAt(lodge, {300, 0, 350}) == 0);   // above the hall's ceiling
    CHECK(rooms::ZoneAt(lodge, {3000, 0, 100}) == -1); // outside the lodge
    CHECK(rooms::Outside(lodge[1], {650, 0, 100}) == doctest::Approx(50.0));
    CHECK(rooms::Outside(lodge[1], {300, 0, 100}) == doctest::Approx(0.0));
}

TEST_CASE("crossing into a room says its name once; the doorway, the parts of a room and the first room say nothing")
{
    const auto lodge = Lodge();
    rooms::Crossing crossing;
    std::vector<std::string> said;
    const auto walk = [&](double x)
    {
        const auto s = crossing.Update(lodge, {x, 0, 100});
        if (!s.empty()) said.push_back(U8(s));
    };
    // The hold begins in the hall: not said.
    walk(100);
    CHECK(said.empty());
    CHECK(U8(crossing.Room()) == "T_Reverb_Hall");
    CHECK(U8(crossing.Spoken()) == "Hall");
    // Into the doorway, where the kitchen's volume begins: she is in the kitchen.
    walk(590);
    CHECK(said == std::vector<std::string>{"Kitchen"});
    // Standing in the doorway, swaying back past the kitchen's edge by a few centimetres: she is in
    // the kitchen until she is kCrossMargin beyond it.
    walk(575);
    walk(590);
    walk(560);
    CHECK(said.size() == 1);
    // On into the kitchen's other part: one room, nothing said.
    walk(900);
    CHECK(said.size() == 1);
    CHECK(U8(crossing.Room()) == "T_Reverb_Kitchen_2");
    // Back into the hall, past the margin.
    walk(700);
    walk(540);
    CHECK(said == std::vector<std::string>{"Kitchen", "Hall"});
    // Into the broad volume between the rooms, then the cellar, which the table does not name.
    walk(700);
    walk(1050);
    CHECK(said == std::vector<std::string>{"Kitchen", "Hall", "Kitchen", "Lodge"});
    walk(1200);
    CHECK(said.size() == 4);
    CHECK(U8(crossing.Room()) == "T_Reverb_Cellar");
    CHECK(crossing.Spoken().empty());
    // Out of the lodge, and back into the broad volume: said again, having been left.
    walk(3000);
    CHECK(crossing.Room().empty());
    walk(1050);
    CHECK(said.back() == "Lodge");
    CHECK(said.size() == 5);
    // A new character's first room is taken in silence.
    crossing.Reset();
    walk(300);
    CHECK(said.size() == 5);
    CHECK(U8(crossing.Spoken()) == "Hall");
}

TEST_CASE("the strip of the broad volume between two rooms says nothing, and the first room waits for the rooms")
{
    // The lodge's front vestibule and its hall, their volumes 40 cm apart with the broad volume
    // between, as Chris walked from one into the other on 2026-11-04 (research/sessions/s41.log
    // 00:19:11 to 00:19:16: "vestibule", "house", "hall").
    std::vector<rooms::Zone> lodge = {
        BoxZone(L"T_GlobalReverb_LodgeInterior", {-100, -500, 0}, {2000, 500, 400}, L"Lodge"),
        BoxZone(L"T_Reverb_Hall_Airlock", {0, -300, 0}, {400, 300, 300}, L"Vestibule"),
        BoxZone(L"T_Reverb_Hall", {440, -400, 0}, {1400, 400, 300}, L"Hall"),
    };
    rooms::Crossing crossing;
    std::vector<std::string> said;
    const auto walk = [&](const std::vector<rooms::Zone>& zones, double x)
    {
        const auto s = crossing.Update(zones, {x, 0, 100});
        if (!s.empty()) said.push_back(U8(s));
    };
    // The level's rooms come a look after the character: the first room is still taken in silence.
    walk({}, 200);
    CHECK(crossing.Room().empty());
    walk(lodge, 200);
    CHECK(said.empty());
    CHECK(U8(crossing.Spoken()) == "Vestibule");
    for (double x = 200; x <= 900; x += 30)
        walk(lodge, x);
    CHECK(said == std::vector<std::string>{"Hall"});
    // Out of the hall into the broad volume beyond it: the house, once a metre past the hall.
    walk(lodge, 1450);
    CHECK(said.size() == 1);
    walk(lodge, 1520);
    CHECK(said == std::vector<std::string>{"Hall", "Lodge"});
}

TEST_CASE("a room counts in the area once a square metre of the map's cells lies in it")
{
    rooms::Area area;
    area.Reset(3);
    for (size_t i = 0; i < rooms::kAreaCells - 1; ++i)
        area.Add(1);
    area.Add(-1);
    area.Add(7);
    CHECK_FALSE(area.Covers(1));
    area.Add(1);
    CHECK(area.Covers(1));
    CHECK(area.Cells(1) == rooms::kAreaCells);
    CHECK_FALSE(area.Covers(0));
    CHECK_FALSE(area.Covers(-1));
    area.Reset(3);
    CHECK_FALSE(area.Covers(1));
}

TEST_CASE("the room rule leaves a thing out only by the map's proof, and holds a verdict while a map settles")
{
    using rooms::Verdict;
    rooms::Facts f;
    f.settled = true;
    f.proves = true;
    // Closed off, or beyond a door: apart.
    f.reach = 2;
    CHECK(rooms::Judge(f, Verdict::None) == Verdict::Apart);
    f.reach = 3;
    CHECK(rooms::Judge(f, Verdict::In) == Verdict::Apart);
    // Reached, or offered: in, whatever the map said before.
    f.reach = 1;
    CHECK(rooms::Judge(f, Verdict::Apart) == Verdict::In);
    f.reach = 3;
    f.offered = true;
    CHECK(rooms::Judge(f, Verdict::Apart) == Verdict::In);
    f.offered = false;
    // A map that proves nothing (cut short by its budget or its bound) leaves nothing out.
    f.proves = false;
    CHECK(rooms::Judge(f, Verdict::Apart) == Verdict::In);
    f.proves = true;
    // Not judged yet by a map the list has settled on (its cap passed while it ran): it waits for the
    // map, or an earlier map's verdict stands until this one gets to it.
    f.reach = 0;
    f.running = true;
    CHECK(rooms::Judge(f, Verdict::None) == Verdict::Waits);
    CHECK(rooms::Judge(f, Verdict::Waits) == Verdict::Waits);
    CHECK(rooms::Judge(f, Verdict::Apart) == Verdict::Apart);
    CHECK(rooms::Judge(f, Verdict::In) == Verdict::In);
    // Done without a verdict for it: nothing proves it closed off.
    f.running = false;
    CHECK(rooms::Judge(f, Verdict::Apart) == Verdict::In);
    CHECK(rooms::Judge(f, Verdict::Waits) == Verdict::In);
    // While the list settles: a thing never judged waits, an earlier verdict stands.
    f.settled = false;
    CHECK(rooms::Judge(f, Verdict::None) == Verdict::Waits);
    CHECK(rooms::Judge(f, Verdict::Waits) == Verdict::Waits);
    CHECK(rooms::Judge(f, Verdict::Apart) == Verdict::Apart);
    CHECK(rooms::Judge(f, Verdict::In) == Verdict::In);
    f.reach = 1;
    CHECK(rooms::Judge(f, Verdict::None) == Verdict::In);
}

TEST_CASE("a map of too few cells changes no verdict: the asylum's walkway, 2026-11-07")
{
    // act4_2 (research/sessions/s51-full.log, 17:19:24 to 17:19:26): Mike at the ledge's edge above the
    // cellar, his capsule for a moment in a cell boxed in on every side; a map of one cell came
    // between maps of 108 cells, and with each the building's forty things came back ("listed again:
    // no map proves it closed off") and "hospital writing on wall, 46 m" was said, then left out again.
    using rooms::Verdict;
    rooms::Facts f;
    f.settled = true;
    f.running = false;
    f.proves = false;
    f.reach = 0;
    f.stuck = true;
    CHECK(rooms::Judge(f, Verdict::Apart) == Verdict::Apart);
    CHECK(rooms::Judge(f, Verdict::In) == Verdict::In);
    CHECK(rooms::Judge(f, Verdict::None) == Verdict::Waits);
    // What it does reach, or the game offers, is in as ever.
    f.reach = 1;
    CHECK(rooms::Judge(f, Verdict::Apart) == Verdict::In);
    f.reach = 0;
    f.offered = true;
    CHECK(rooms::Judge(f, Verdict::Apart) == Verdict::In);
    // Without the stuck map, a map that proves nothing lists the thing.
    f.offered = false;
    f.stuck = false;
    CHECK(rooms::Judge(f, Verdict::Apart) == Verdict::In);
}

TEST_CASE("the mine's things are never shown while the first map of Mike's hold runs past the list's while, 2026-11-04")
{
    // Session 43 (research/sessions/s43.log): the hold began at 02:52:28.77 beside the security gate;
    // the list settled on its cap at 31.96 while the map ran on to 34.73, and the police tape, the
    // Hannah and Beth talk and the carts, 42 to 82 m off in the mine, were listed for three seconds
    // and then left out as closed off. The security gate was reached at 28.98.
    using rooms::Verdict;
    struct Look
    {
        bool settled, running, proves;
        int reach;
    };
    const Look tape[] = {
        {false, true, false, 0}, // 28.77: the hold begins, the map with it
        {true, true, false, 0},  // 31.96: the list's while has run out, the map has not got there
        {true, true, false, 0},  // 33.00
        {true, false, true, 2},  // 34.73: the map is done: closed off
    };
    Verdict verdict = Verdict::None;
    for (const auto& look : tape)
    {
        rooms::Facts f;
        f.settled = look.settled;
        f.running = look.running;
        f.proves = look.proves;
        f.reach = look.reach;
        verdict = rooms::Judge(f, verdict);
        CHECK(verdict != Verdict::In);
    }
    CHECK(verdict == Verdict::Apart);
    // The gate, reached by the map a fifth of a second in, is in at once.
    rooms::Facts gate;
    gate.running = true;
    gate.reach = 1;
    CHECK(rooms::Judge(gate, Verdict::Waits) == Verdict::In);
}

TEST_CASE("the prologue's airlock: the kitchen's things and the yard's are left out, its two doors listed")
{
    // The map from the airlock between the kitchen and the outside, 2026-10-28 19:35:44
    // (research/dumps/session-user33.log): 671 cells, every cell within reach opened; it reached
    // both doors and none of the rest, a door standing in its ground. The prologue's levels have
    // no reverb volumes, so the map alone judges.
    struct Thing
    {
        const char* name;
        int reach;
    };
    const Thing things[] = {{"Door Into Kitchen", 1}, {"Door To Outside", 1}, {"Camera", 3}, {"Examine Laptop", 3}, {"Window", 3}, {"totem death", 3}};
    std::vector<std::string> listed;
    for (const auto& t : things)
    {
        rooms::Facts f;
        f.settled = true;
        f.proves = true;
        f.reach = t.reach;
        if (rooms::Judge(f, rooms::Verdict::None) == rooms::Verdict::In) listed.push_back(t.name);
    }
    CHECK(listed == std::vector<std::string>{"Door Into Kitchen", "Door To Outside"});
}

TEST_CASE("a way on beyond a door is not offered, one the path runs into is")
{
    CHECK(rooms::WayOffered(false, 1, false));
    CHECK(rooms::WayOffered(false, 0, false));
    CHECK_FALSE(rooms::WayOffered(true, 2, false));
    CHECK_FALSE(rooms::WayOffered(false, 3, false));
    CHECK(rooms::WayOffered(false, 3, true));
    CHECK(rooms::WayOffered(true, 2, true));
}

TEST_CASE("a way found beyond a door stays out while a new map has not got to it: the asylum's hall, 2026-11-07")
{
    // act4_2 after the keycard (research/sessions/s52-full.log, 19:08:38 to 19:08:42): Mike walked off
    // the ground the last map reached, a new map was begun, and the list settled on its while 3.0 s
    // on with 8 of 37 targets judged. The ways to Section_08 and Section_07 (BatesCharacterTriggerVolume_10
    // and _11), the beam's volume outside and the hospital's wolf scare, all found beyond a door by
    // the map before, were offered again ("a way to it is open") and left out once more 0.9 s later,
    // when the map was done; at every new map of the walk, up to fifteen seconds at a time.
    struct Way
    {
        const char* name;
        bool judgedNow;
        int reach; // the map's verdict as the target holds it: this map's once judged, else the last one's
    };
    const Way ways[] = {
        {"BatesCharacterTriggerVolume_10", false, 3},
        {"BatesCharacterTriggerVolume_11", false, 3},
        {"BP_CharacterTriggerVolume_C_1", false, 3},
        {"BatesCharacterTriggerVolume_1", false, 3},
        {"BatesCharacterTriggerVolume_0", true, 1}, // reached by this map, 3184 cm on foot
    };
    std::vector<std::string> offered;
    for (const auto& w : ways)
    {
        if (rooms::WayOffered(false, rooms::WayReach(w.judgedNow, true, false, w.reach), false)) offered.push_back(w.name);
    }
    CHECK(offered == std::vector<std::string>{"BatesCharacterTriggerVolume_0"});
    // A map that has opened too few cells to judge by keeps them out too.
    CHECK_FALSE(rooms::WayOffered(false, rooms::WayReach(false, false, true, 3), false));
    // Done without a verdict on a way (cut short by its bound), the map proves no wall: the way is offered.
    CHECK(rooms::WayOffered(false, rooms::WayReach(false, false, false, 3), false));
    // A way the earlier map reached stays offered meanwhile, and one never judged waits for the map
    // by the list's own rule (ways::Lister), whatever its verdict here.
    CHECK(rooms::WayOffered(false, rooms::WayReach(false, true, false, 1), false));
    CHECK(rooms::WayReach(false, true, false, 0) == 0);
    // This map's verdict is the one, once it has given it.
    CHECK(rooms::WayReach(true, true, false, 3) == 3);
    CHECK(rooms::WayReach(true, false, false, 1) == 1);
}

TEST_CASE("the lodge's yard: the lodge's things are another room's, the yard's stay, and the walk crosses no room")
{
    // Chris in the lodge's yard with the front door frozen shut, 2026-11-02 11:35 to 11:47
    // (research/sessions/s40.log, research/dumps/ground-20261102-113515.tsv cut by trim_record.py
    // to the 5885 cells the yard's maps reached, with the 27 reverb volumes of act2_11's LV_reverb
    // placed by tools/levels/level_triggers.py and the 204 places the character stood at). The
    // list held 24 things; the map, run from the yard, reached four and proved the rest closed off.
    std::ifstream in(std::filesystem::path(DA_REPO_ROOT) / "tests" / "fixtures" / "ground-lodge-yard.tsv");
    REQUIRE(in.good());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ground::Record record;
    REQUIRE(record.Parse(text));
    REQUIRE(!record.anchors.empty());
    const auto& anchor = record.anchors.front();

    std::vector<rooms::Zone> zones;
    for (const auto& [name, shape] : record.volumes)
    {
        const auto wide = str::Utf8ToWide(name);
        if (!rooms::Key(wide).empty()) zones.push_back(rooms::MakeZone(wide, shape));
    }
    REQUIRE(zones.size() == 27);
    const auto zoneNamed = [&](const char* name)
    {
        for (size_t i = 0; i < zones.size(); ++i)
        {
            if (U8(zones[i].name) == name) return static_cast<int>(i);
        }
        return -1;
    };

    // The map over the recorded ground, from where the character stood at the hand-over.
    const grid::Cell start{0, 0, LevelOf(anchor.z - anchor.half)};
    grid::Flood flood(start, 550, 80000);
    const auto passable = record.Passable();
    while (flood.Current() == grid::Flood::State::Running)
        flood.Step(64, passable);
    REQUIRE(flood.Current() == grid::Flood::State::Complete);
    CHECK_FALSE(flood.RadiusReached());
    CHECK(flood.Expanded() == 5885);
    REQUIRE(flood.Order().size() == 5885);

    // The rooms of its ground, by each cell's middle as Exploration's WorldOf has it.
    rooms::Area area;
    area.Reset(zones.size());
    for (const auto& c : flood.Order())
    {
        const auto floor = record.floors.find({c.x, c.y, c.level});
        REQUIRE(floor != record.floors.end());
        area.Add(rooms::ZoneAt(zones, Vec{anchor.x + (c.x + 0.5) * anchor.cell, anchor.y + (c.y + 0.5) * anchor.cell, floor->second + anchor.half}));
    }
    const int yard = zoneNamed("T_Reverb_OutsideLodge");
    REQUIRE(yard >= 0);
    CHECK(area.Covers(yard));
    CHECK(area.Cells(yard) == 5882);
    // The path to the fence, three cells of it: too few to count.
    CHECK(area.Cells(zoneNamed("T_Reverb_PathToBeautySpot")) == 3);
    CHECK_FALSE(area.Covers(zoneNamed("T_Reverb_PathToBeautySpot")));
    // The porch lies in the yard's volume and in the lodge's broad one: the yard's is the narrower.
    CHECK(rooms::ZoneAt(zones, {355, -9482, 3065}) == yard);

    // The things, where the list had them, with the map's verdicts of 11:37:58.
    struct Thing
    {
        const char* name;
        Vec at;
        int reach;
    };
    const Thing things[] = {
        {"chat matt Chris", {563, -11144, 2524}, 1},
        {"intolodge back to entrance Chris", {475, -9240, 3098}, 2},
        {"flame lock no deodorant", {451, -9202, 3011}, 2},
        {"police card", {319, -9018, 3063}, 2},
        {"television", {1018, -9253, 3027}, 2},
        {"sam at fence spoken to ashley Chris", {1034, -11897, 2356}, 1},
        {"garage airlock door", {536, -8140, 3062}, 2},
        {"hannah and beth beach photo", {1818, -8556, 2618}, 2},
        {"Hannah and beth beach photo female", {1818, -8556, 2614}, 2},
        {"bedroom door", {789, -8060, 3437}, 2},
        {"cinema door r", {1941, -8519, 2556}, 2},
        {"hannahs trophy", {2271, -8798, 3440}, 2},
        {"hall lower doors r", {2289, -8672, 3023}, 2},
        {"answerphone", {2659, -9133, 3022}, 2},
        {"family portrait", {-763, -7526, 3027}, 2},
        {"Prom night photo female", {2338, -8066, 2973}, 2},
        {"prom night photo", {2319, -8049, 3028}, 2},
        {"bathroom door", {3295, -8921, 3442}, 2},
        {"deodorant combat", {3633, -9865, 3483}, 2},
        {"totem guidance", {2878, -7967, 3445}, 2},
        {"newspaper edge clipping", {-1677, -6771, 3017}, 2},
        {"missing axe", {-2532, -6085, 3045}, 1},
        {"totem tragedy", {-1481, -5412, 3025}, 2},
        {"grit bin", {-2784, -5539, 3002}, 1},
        // Five use locations the level keeps at the world's origin, said at 100 and 130 m.
        {"finalfight Chris", {70, 0, 0}, 2},
        {"intolodge scene intro mattok Chris", {70, 0, 0}, 2},
        {"intolodge climb inside Chris", {70, 0, 0}, 2},
        {"wolverine to front door cutaway Chris", {70, 0, 0}, 2},
        {"intolodge door open Chris", {70, 0, 0}, 2},
    };
    const int herZone = rooms::ZoneAt(zones, {185, -10227, 2771});
    CHECK(herZone == yard);
    std::vector<std::string> listed;
    std::map<std::string, std::string> roomOf;
    for (const auto& t : things)
    {
        const int zone = rooms::ZoneAt(zones, t.at);
        roomOf[t.name] = zone >= 0 ? U8(zones[static_cast<size_t>(zone)].name) : "";
        rooms::Facts f;
        f.settled = true;
        f.proves = true;
        f.reach = t.reach;
        if (rooms::Judge(f, rooms::Verdict::None) == rooms::Verdict::In) listed.push_back(t.name);
    }
    // The yard's four. The newspaper clipping lies in the yard's own volume, but no walk from the
    // yard gets to it: on 2026-11-04 the map reached it from inside the lodge, 41 m on foot by way of
    // the garage Chris had climbed into (research/sessions/s41.log 00:17:18), so it stays out.
    CHECK(listed == std::vector<std::string>{"chat matt Chris", "sam at fence spoken to ashley Chris", "missing axe", "grit bin"});
    CHECK(roomOf["newspaper edge clipping"] == "T_Reverb_OutsideLodge");
    CHECK(roomOf["flame lock no deodorant"] == "T_Reverb_Hall_Airlock");
    CHECK(roomOf["television"] == "T_Reverb_Hall");
    CHECK(roomOf["bathroom door"] == "T_Reverb_Kitchen_1");
    CHECK(roomOf["totem tragedy"] == "T_Reverb_LodgeInterior_Garage");
    CHECK(roomOf["bedroom door"] == "T_GlobalReverb_LodgeInterior");
    CHECK(roomOf["finalfight Chris"].empty());

    // Every place the character stood at lies in the yard: the walks crossed into no room.
    rooms::Crossing crossing;
    for (auto& z : zones)
        z.spoken = str::Utf8ToWide(U8(rooms::Key(z.name)));
    size_t said = 0;
    REQUIRE(record.positions.size() == 204);
    for (const auto& p : record.positions)
    {
        if (!crossing.Update(zones, Vec{p.x, p.y, p.z}).empty()) ++said;
    }
    CHECK(said == 0);
    CHECK(U8(crossing.Room()) == "T_Reverb_OutsideLodge");
}
