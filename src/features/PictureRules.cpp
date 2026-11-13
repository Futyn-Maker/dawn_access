#include "features/PictureRules.hpp"

#include "core/Strings.hpp"

#include <algorithm>
#include <array>
#include <cwctype>

namespace da::picture
{
    namespace
    {
        // The suffixes of cuts the descriptions leave alike: what Jessica wears, and Mike's hand.
        constexpr std::array<std::wstring_view, 4> kVariantSuffixes = {L"_jacket", L"_tshirt", L"_naked", L"_lostfingers"};

        // The film a censored cut censors ("totem_death_5_ja" is totem_death_5), or nothing.
        std::optional<std::wstring> Uncensored(std::wstring_view name)
        {
            if (name.size() <= 3 || !name.ends_with(L"_ja")) return std::nullopt;
            return std::wstring(name.substr(0, name.size() - 3));
        }

        // The name without a trailing "_<digits>", or nothing when it has none.
        std::optional<std::wstring> WithoutNumber(std::wstring_view name)
        {
            size_t end = name.size();
            while (end > 0 && std::iswdigit(name[end - 1])) --end;
            if (end == name.size() || end < 2 || name[end - 1] != L'_') return std::nullopt;
            return std::wstring(name.substr(0, end - 1));
        }

        void AddKey(std::vector<std::wstring>& keys, std::wstring key)
        {
            for (const auto& k : keys)
            {
                if (k == key) return;
            }
            keys.push_back(std::move(key));
        }

        struct Planned
        {
            std::wstring_view key;       // its description's name (lower case), and a name the game gives it
            std::wstring_view className; // its class, lower case, where its class is its own
            Moment moment;
            std::wstring_view state;
            bool afterHud = false; // shown with words of the HUD's own, said after them (LookPlan::afterHud)
        };

        // The interactions whose pictures the mod describes, and when; the moments are the game's own
        // states, as the logs of chapters 5 and 6 show them.
        constexpr std::array<Planned, 55> kPlans = {
            // Dr. Hill's card is picked up face down and turned over with the stick (act20_1,
            // BP_PsychSession_PickUp, which no other level places: its examine stage reveals). It has no
            // core name; its activity starts it as I_psychsession_1_Interaction.
            Planned{L"i_psychsession_1_interaction", L"bp_psychsession_pickup_c", Moment::Reveals, L""},
            // Sam's sketchbook is held shut, then opened on its second spread (act5_3, BP_sam_sketchbook,
            // in the old hotel alone: closed_ik_idle, then page2_idle_right); the swipes turn its pages,
            // each spread two pencil portraits (T_Sams_Sketchpad_Paper_CLR_BM, laid on the mesh's three
            // sheets and two covers). It has no core name either; the level calls it I_sams_sketchbook.
            Planned{L"i_sams_sketchbook", L"bp_sam_sketchbook_c", Moment::Pages, L""},
            // The station's map is taken down from the wall and held up to look at (act4_3, BP_Map:
            // grab_idle, map_adjust, then map_inspect1), and looked at again with Emily (stand_to_inspect,
            // then inspect_idle 1.1 s on, for 3.3 s) and without her (stand_to_inspect1, then inspect_idle1
            // 0.9 s on; research/sessions/s54-full.log, 22:12:19 to 22:21:14). Their core names are set in
            // the level.
            Planned{L"i_map", L"", Moment::State, L"map_inspect1"},
            Planned{L"i_lookatmap_with_emily", L"", Moment::State, L"inspect_idle"},
            Planned{L"i_lookatmap_without_emily", L"", Moment::State, L"inspect_idle1"},
            // The words painted over the station's walls (act4_3, BP_Graffiti, whose class sets its core
            // name: a look at the wall, examine_into, then examine_idle 3 s on as the look stands).
            Planned{L"i_graffiti", L"", Moment::State, L"examine_idle"},
            // The security monitor in the lower ski-lift station, looked at by Chris (act1_3,
            // BP_Security_monitor, core name set in the level: examine_into, then examine_idle). Its screen
            // (M_FlickeringMonitor's texture array TA_monitor_image, by the scalar Image) rests on a dark
            // bathroom (0); 0.27 s into examine_idle it cuts to a camera above a snowy building with a dark
            // figure in the snow (1), and at 1.73 s, through static, to the same without the figure (2),
            // none of its events repeating (research/sessions/s68-play.log, 01:28:04 to 01:28:36).
            Planned{L"i_security_monitor", L"bp_security_monitor_c", Moment::State, L"examine_idle"},
            // The Washingtons' portrait in the lodge's garage vestibule, looked at by Chris (act2_11,
            // BP_Family_portrait, core name set in the level: examine_into, then examine_idle as his look
            // stands, "Назад" on the bar 0.7 s on; research/sessions/s70-play.log, 11:44:26 to 11:44:29).
            // A painting on a static mesh (SM_family_portrait, its one colour texture
            // T_family_portrait_clr), the same at every look.
            Planned{L"i_family_portrait", L"bp_family_portrait_c", Moment::State, L"examine_idle"},
            // The silent things of chapters 1 and 2, found by the audit of sessions 67 to 71 (a picture the
            // game showed and said nothing of; research by textures decoded from the game and its own clue
            // images), each when its look stands or its item is held to the face that shows it: Emily's
            // things in the car at the lower station (act1_3, no core name), the hotel poster (act1_3) and the
            // bench's carved heart on its second wipe (act2_3). A clue is described by the game in its menu
            // (DT_ClueData's text and details), so a clue's picture is said only for what it shows of the
            // story that those words leave out (Andrey, 2026-11-09 and 2026-11-13): of the prom and beach
            // photos (act2_11; the prom photo's copy in chapter 5, the beach photo Ashley picks up in chapter 6
            // too) Hannah's butterfly tattoo, which the clue's words name only after the wendigo is shown to
            // be her; the painted symbol of the mine (Act2_2), which the words leave "not clear"; the
            // postcard's other side (act2_4), Dr. Hill's own painting, said in the card's words. The
            // Washingtons' portrait above is a clue too, described at Andrey's request (2026-11-11). Their
            // wanted poster, axe holder, mine danger map, cigar stub and pig's head note show nothing the
            // game's words leave out.
            Planned{L"i_car_lookat", L"bp_car_lookat_c", Moment::State, L"examine_idle"},
            Planned{L"i_hotel_poster", L"bp_hotel_poster_c", Moment::State, L"examine_idle"},
            Planned{L"i_prom_night_photo", L"bp_prom_night_photo_c", Moment::State, L"ik_animation"},
            Planned{L"i_prom_night_photo_female", L"bp_prom_night_photo_female_c", Moment::State, L"ik_animation"},
            Planned{L"i_hannah_and_beth_beach_photo", L"bp_hannah_and_beth_beach_photo_c", Moment::State, L"ik_animation"},
            Planned{L"i_mysticalsymbols", L"bp_mysticalsymbols_c", Moment::State, L"examine_idle"},
            Planned{L"i_childsdrawing", L"bp_childsdrawing_c", Moment::Reveals, L""},
            Planned{L"i_snowybench", L"bp_snowybench_c", Moment::State, L"BenchWipe2"},
            // Andrey's four of session 71. Hannah's pinboard (act2_4): the invitation read, Sam lifts it and
            // a photo of Mike shows under it (lift_paper, lift_paper_idle; "О, Ханна... любовь-любовь..."),
            // said then with the board around it, after the invitation's text as he asked. The dollhouse in
            // the lodge's cellar (act2_4; its class is also I_look_at_picture's, so by its name alone), the
            // headless doll behind the swing (act2_3, no core name, turned to its reveal at 0.85), and the
            // upper cable car station's control desk (act4_3), a look with no words at all.
            Planned{L"i_pinboard_hannah_room", L"bp_pinboard_hannah_room_c", Moment::State, L"lift_paper_idle"},
            Planned{L"i_look_at_dollshouse", L"", Moment::State, L"examine_idle"},
            Planned{L"i_headlessdoll", L"bp_headlessdoll_c", Moment::Reveals, L""},
            Planned{L"i_controls_lookat", L"bp_controls_lookat_c", Moment::State, L"examine_idle"},
            // The views through the telescopes, one picture each whichever way the stick turns them (Andrey,
            // session 71), from walkthroughs of the remake, frame by frame, and the levels' data: Ashley's at
            // the scenic view (act2_1, its own core name A_telescope; Emily and Mike are what its framing
            // waits for), Mike's at the vista (Act2_2) and Matt's on the observation deck (act2_3). The last two
            // share their core name and their class's name, and are told apart by the class's package. Said as
            // the view comes up, after the game's prompt; the scares that cut in (Matt's face, the gaunt face at
            // the vista) are not described.
            Planned{L"a_telescope", L"", Moment::Begins, L""},
            Planned{L"i_telescope.act2_2", L"/game/gameplay/gameobjects/interactions/act2_2/telescope/bp_telescope", Moment::Begins, L""},
            Planned{L"i_telescope.act2_3", L"/game/gameplay/gameobjects/interactions/act2_3/telescope/bp_telescope", Moment::Begins, L""},
            // The silent things of chapter 6, found in session 72 and the first playthrough's s57 to s58
            // (research by the levels' placements, the interactions' own cameras and the props' decoded
            // meshes, textures and animations). The tower's death whistle turned to its face (act5_2); in the
            // lodge (act5_3) the smashed vase, Sam's bracelet wherever it lies (one class), the cellar's
            // painting before it falls, the dollhouse looked into at its first use and opened with the key at
            // its second (the dolls with the friends' hair: Chris's "Это мы"), its keyhole, the key behind
            // the painting, the wine rack and the wine cellar's door, the doll's head turned to its face and
            // the cockroaches it lets out, Sam under the sack; the board of the friends' photos, whose
            // crossed-out one alone is said (a clue the game describes in its menu, as every clue: its card
            // and nothing more, Andrey 2026-11-13, but the photo crossed out is shown on the board alone; the
            // menu's picture of it crosses out none), after the card; the beach photo Ashley picks up. Hannah's
            // missing poster and the hidden camera are clues whose pictures show nothing the game's words
            // leave out.
            Planned{L"i_death_whistle", L"bp_death_whistle_c", Moment::Reveals, L""},
            Planned{L"i_smashed_vase", L"bp_smashed_vase_v2_c", Moment::State, L"ik_idle"},
            Planned{L"i_sams_bracelet", L"bp_bracelet_c", Moment::State, L"ik_idle"},
            Planned{L"i_picture_lookat", L"bp_picture_lookat_c", Moment::State, L"examine_idle"},
            Planned{L"i_dollhouse_complete", L"bp_doll_house_open_c", Moment::State, L"LookinSequence"},
            Planned{L"i_dollhouse_complete.open", L"bp_doll_house_open_c", Moment::State, L"Reach_to_open_front|Open_Front"},
            Planned{L"i_doll_house_lookat_keyhole", L"bp_doll_house_lookat_keyhole_c", Moment::State, L"examine_idle"},
            Planned{L"i_doll_house_key", L"bp_doll_house_key_c", Moment::State, L"ik_animation"},
            Planned{L"i_wine_rack", L"bp_wine_rack_c", Moment::State, L"examine_idle"},
            Planned{L"i_wine_cellar_door", L"bp_wine_cellar_door_lookat_c", Moment::State, L"examine_idle"},
            Planned{L"i_doll_head", L"bp_dolls_head_c", Moment::Reveals, L""},
            Planned{L"i_doll_head.roaches", L"bp_dolls_head_c", Moment::State, L"cockroach_panic"},
            Planned{L"i_sams_sack", L"bp_samsack_c", Moment::State, L"EndPose"},
            Planned{L"i_photos_of_friends_lookat_first", L"bp_photos_lookat_first_c", Moment::State, L"examine_idle", true},
            Planned{L"i_photos_of_friends_lookat", L"bp_photos_lookat_c", Moment::State, L"examine_idle", true},
            Planned{L"i_hannah_and_beth_beach_photo_female", L"bp_hannah_and_beth_beach_photo_female_c", Moment::State, L"ik_animation"},
            // The silent looks of chapters 7 to 10, found in session 73 (none a clue: none shows a clue's
            // card or sets its flag; research from the levels' placements, the looks' own cameras, the props'
            // decoded meshes and textures, and walkthroughs of the remake), each when its look stands, the
            // game's "Назад" some 0.7 s on. Emily in the mines (act6_1): the tunnel floor seen through the
            // mesh, the miner's helmet on the stones (the look is called the torn clothes) and the broken
            // ladder beside it (a generic floor camera the walkthroughs show filled by the ladder), the broken
            // piece of a danger sign in Hannah's lair, and the views through the mesh where the look is called
            // the head and the wheel. The lodge's path (act4_1 and act6_4, one look in the levels both stream):
            // the scarecrow, whose head is the one Dr. Hill's fear question chose (kVariants). Chris at the
            // hanging shed (act6_4): the dead crows (no core name; the level calls them I_Dead_crow_lookat),
            // the chair and blood, the fake torso, the saw blade. Mike in the sanatorium (act7_2): the dead
            // wolf and the clawed cell door. Sam at the sanatorium's back (act8_1): the fence by its gate, the
            // oil can held up (its prompt with it) and the cockroaches it lets out.
            Planned{L"i_peer_at_cart", L"bp_peeratcart_c", Moment::State, L"look_idle"},
            Planned{L"i_torn_miner_clothes", L"bp_tornminerclothes_lookat_c", Moment::State, L"examine_idle"},
            Planned{L"i_broken_ladder", L"bp_broken_ladder_lookat_c", Moment::State, L"examine_idle"},
            Planned{L"i_sign", L"bp_sign_c", Moment::State, L"examine_idle"},
            Planned{L"i_peer_at_head", L"bp_peerathead_c", Moment::State, L"look_idle"},
            Planned{L"i_peer_at_wheel", L"bp_peeratwheel_c", Moment::State, L"look_idle"},
            Planned{L"i_scarecrow_lookat", L"bp_scarecrow_lookat_c", Moment::State, L"examine_idle"},
            Planned{L"i_dead_crow_lookat", L"bp_dead_crow_lookat_c", Moment::State, L"examine_idle"},
            Planned{L"i_chair_and_blood", L"bp_interaction_chairandblood_c", Moment::State, L"examine_idle"},
            Planned{L"i_fake_torso", L"bp_interaction_faketorso_c", Moment::State, L"examine_idle"},
            Planned{L"i_saw_blades", L"bp_interaction_sawblades_c", Moment::State, L"examine_idle"},
            Planned{L"i_dead_wolf_inspect", L"bp_dead_wolf_inspect_c", Moment::State, L"examine_idle"},
            Planned{L"i_damaged_doors", L"bp_damaged_doors_c", Moment::State, L"examine_idle"},
            Planned{L"i_sanlookat", L"bp_lookat_san_entrance_gate_c", Moment::State, L"examine_idle"},
            Planned{L"i_oilflask", L"bp_oilflask_c", Moment::State, L"IKswing"},
            Planned{L"i_oilflask.roaches", L"bp_oilflask_c", Moment::State, L"LookCloserThrowAwayPickUpWeapon"},
        };

        // A picture the game draws one of several ways by the player's earlier answers: the callforward its
        // scene reads (the global state's name, as SF_IsCallforwardEqualTo reads it), in the order it reads
        // them, and the suffix of that drawing's description.
        struct Variant
        {
            std::wstring_view key;
            std::wstring_view callforward;
            std::wstring_view suffix;
        };
        constexpr std::array<Variant, 2> kVariants = {
            // The scarecrow by the lodge's path wears a hat and a sack for a head unless Dr. Hill's fear
            // question chose the clown or the zombie (act20_2 Activity_Session2_Part2-Custom sets ChoseClown
            // or ChoseZombie); the scenes swap its head by them, the clown asked first (act4_1
            // scene01_scarecrow state 9, act6_4 scene02_scarecrow state 8).
            Variant{L"i_scarecrow_lookat", L"CallforwardChoseClown", L".clown"},
            Variant{L"i_scarecrow_lookat", L"CallforwardChoseZombie", L".zombie"},
        };
    }

    Screen ScreenOf(bool everFound, size_t live, bool anyDrawn)
    {
        if (live == 0) return everFound ? Screen::Hidden : Screen::Unknown;
        return anyDrawn ? Screen::Drawn : Screen::Hidden;
    }

    std::wstring FilmName(std::wstring_view url)
    {
        std::wstring name(url);
        if (const auto slash = name.find_last_of(L"/\\"); slash != std::wstring::npos) name = name.substr(slash + 1);
        if (const auto dot = name.rfind(L'.'); dot != std::wstring::npos) name.resize(dot);
        return str::ToLower(name);
    }

    FilmOwner OwnerOf(std::wstring_view name)
    {
        if (name.starts_with(L"death_")) return FilmOwner::Credits;
        // The events of the past play in the totems' menu over a player of their own.
        if (name.starts_with(L"totem_") && name != L"totem_events_of_the_past") return FilmOwner::Totems;
        return FilmOwner::Films;
    }

    std::vector<std::wstring> FilmKeys(std::wstring_view name)
    {
        std::vector<std::wstring> keys;
        if (name.empty()) return keys;
        const std::wstring lower = str::ToLower(name);
        if (const auto film = Uncensored(lower))
        {
            const auto plain = FilmKeys(*film);
            for (const auto& key : plain) AddKey(keys, key + L"_ja");
            for (const auto& key : plain) AddKey(keys, key);
            return keys;
        }
        AddKey(keys, L"film." + lower);
        std::wstring base = lower;
        for (const auto suffix : kVariantSuffixes)
        {
            if (base.size() > suffix.size() && base.ends_with(suffix))
            {
                base.resize(base.size() - suffix.size());
                AddKey(keys, L"film." + base);
                break;
            }
        }
        if (const auto bare = WithoutNumber(base)) AddKey(keys, L"film." + *bare);
        return keys;
    }

    std::vector<double> PartsOf(std::wstring_view name)
    {
        const std::wstring lower = str::ToLower(name);
        // The events of the past (125 s): its archive photographs and drawings, then the night of the
        // prologue rendered anew. The parts begin where what they show begins, measured on the film's
        // own frames (research/films/review_world.md): the photograph and the sketches, the miners'
        // photograph (a hard cut at 20.5 s), the flashes of gore (34.9 s), the handwritten list (51 s),
        // the forest (80 s), the rope fence at the cliff (97.7 s), the man with the flamethrower
        // (107.5 s).
        if (lower == L"totem_events_of_the_past") return {0.0, 20.5, 34.9, 51.0, 80.0, 97.7, 107.5};
        // The epilogue's last film (89.5 s), between its spoken captions (DA_sam_end_credits): after the
        // first (0.6 to 5.1 s), the pill bottle falling (37.9 s), the tissue on her arm (60.5 s), the
        // title (77 s).
        if (lower == L"sam_end_credits") return {5.5, 37.9, 60.5, 77.0};
        // The film on the lodge's cinema screen (133 s): a blank, scratched leader, Sam in her bath
        // (36.9 s), the saw and Josh (63.9 s), the leader again (69.9 s).
        if (lower == L"smg_sting") return {0.0, 36.9, 63.9, 69.9};
        // Chapter 1's opening film (255 s), its first 102 s with no speech and no subtitle: the title
        // (5 s), the cast's names over close-ups of objects (13 s), the crew and the song (72 s), then,
        // under the radio's talk, the bus on the mountain road (104.5 s), Sam on the bus with the radio
        // on her phone (137 s) and Josh's video on it (157.5 s), each where the film's frames show it
        // (research/films/Intro_BusRide).
        if (lower == L"intro_busride") return {4.5, 13.0, 72.0, 104.5, 137.0, 157.5};
        // The sisters at the cliff (9 s): its first five seconds are said with the film; the rest, where
        // Hannah lies at the foot of the cliff, only where the credits show it whole (scene01's queue, 9 s;
        // all_survivor_accounts shows 5 s).
        if (lower == L"death_act1_1_sisters_cliff") return {5.5};
        return {};
    }

    std::wstring PartKey(std::wstring_view name, size_t part)
    {
        return L"film." + str::ToLower(name) + L"." + std::to_wstring(part + 1);
    }

    bool OnKey(std::wstring_view name)
    {
        return str::ToLower(name) == L"totem_events_of_the_past";
    }

    std::optional<size_t> PartAt(const std::vector<double>& parts, double position, bool shown)
    {
        if (!shown || parts.empty() || position < parts.front()) return std::nullopt;
        size_t part = 0;
        while (part + 1 < parts.size() && parts[part + 1] <= position) ++part;
        return part;
    }

    std::optional<size_t> PartTracker::Update(const std::vector<double>& parts, double position, bool shown)
    {
        // Played again from an earlier point: its parts are said again.
        if (m_position >= 0.0 && position < m_position - 1.0) m_said.reset();
        m_position = position;
        const auto part = PartAt(parts, position, shown);
        if (!part || (m_said && *m_said == *part)) return std::nullopt;
        m_said = part;
        return part;
    }

    void PartTracker::Reset()
    {
        m_said.reset();
        m_position = -1.0;
    }

    bool Resumes(const std::wstring& lastFilm, const std::wstring& film, double lastPosition, double position)
    {
        return !film.empty() && film == lastFilm && lastPosition >= 0.5 && position >= lastPosition - 1.0;
    }

    std::vector<std::wstring> VisionKeys(std::wstring_view name)
    {
        std::vector<std::wstring> keys;
        if (name.empty()) return keys;
        const std::wstring lower = str::ToLower(name);
        if (const auto vision = Uncensored(lower))
        {
            const auto plain = VisionKeys(*vision);
            for (const auto& key : plain) AddKey(keys, key + L"_ja");
            for (const auto& key : plain) AddKey(keys, key);
            return keys;
        }
        AddKey(keys, L"totem." + lower);
        // totem_<group>_<n>_<cut>: the cut's number is dropped, the totem's kept.
        size_t parts = 0;
        for (const wchar_t c : lower)
        {
            if (c == L'_') ++parts;
        }
        if (parts >= 3)
        {
            if (const auto bare = WithoutNumber(lower)) AddKey(keys, L"totem." + *bare);
        }
        return keys;
    }

    std::wstring PictureKey(std::wstring_view texture)
    {
        return L"picture." + str::ToLower(texture);
    }

    std::optional<LookPlan> PlanFor(const std::vector<std::wstring>& names)
    {
        for (const auto& name : names)
        {
            if (name.empty()) continue;
            const std::wstring lower = str::ToLower(name);
            for (const auto& p : kPlans)
            {
                if (p.key == lower || (!p.className.empty() && p.className == lower)) return LookPlan{p.moment, std::wstring(p.state), std::wstring(p.key), p.afterHud};
            }
        }
        return std::nullopt;
    }

    std::vector<LookPlan> PlansFor(const std::vector<std::wstring>& names)
    {
        std::vector<LookPlan> plans;
        for (const auto& p : kPlans)
        {
            const bool named = std::any_of(names.begin(), names.end(),
                                           [&](const std::wstring& name)
                                           {
                                               if (name.empty()) return false;
                                               const std::wstring lower = str::ToLower(name);
                                               return p.key == lower || (!p.className.empty() && p.className == lower);
                                           });
            if (named) plans.push_back(LookPlan{p.moment, std::wstring(p.state), std::wstring(p.key), p.afterHud});
        }
        return plans;
    }

    std::wstring VariantKey(std::wstring_view planKey, const std::function<bool(std::wstring_view)>& isSet)
    {
        for (const auto& v : kVariants)
        {
            if (v.key == planKey && isSet(v.callforward)) return std::wstring(planKey) + std::wstring(v.suffix);
        }
        return std::wstring(planKey);
    }

    int CrossedOut(const std::array<bool, 8>& dislikes)
    {
        for (size_t i = 0; i < dislikes.size(); ++i)
        {
            if (dislikes[i]) return static_cast<int>(i);
        }
        return -1;
    }

    bool IsFriendsBoard(std::wstring_view planKey)
    {
        return planKey == L"i_photos_of_friends_lookat_first" || planKey == L"i_photos_of_friends_lookat";
    }

    std::wstring LookKey(std::wstring_view planKey)
    {
        return L"look." + str::ToLower(planKey);
    }

    std::wstring ResolveWords(std::wstring_view key, const std::function<std::wstring(std::wstring_view)>& mod,
                              const std::function<std::wstring(std::wstring_view)>& game)
    {
        constexpr int kHopsMost = 8;
        std::set<std::wstring> seen;
        std::wstring at(key);
        for (int hop = 0; hop < kHopsMost; ++hop)
        {
            if (!seen.insert(str::ToLower(at)).second) return {};
            const std::wstring words = mod(at);
            if (words.starts_with(L"@")) return words.size() > 1 ? game(std::wstring_view(words).substr(1)) : std::wstring();
            if (!words.starts_with(L"=")) return words;
            if (words.size() < 2) return {};
            at = words.substr(1);
        }
        return {};
    }

    std::optional<int> PageOf(std::wstring_view state)
    {
        if (!state.starts_with(L"page")) return std::nullopt;
        size_t at = 4;
        int page = 0;
        while (at < state.size() && std::iswdigit(state[at]) && page < 1000) page = page * 10 + static_cast<int>(state[at++] - L'0');
        if (at == 4 || page <= 0) return std::nullopt;
        const auto rest = state.substr(at);
        if (!rest.starts_with(L"_idle") || rest.find(L"_to_") != std::wstring_view::npos) return std::nullopt;
        return page;
    }

    std::wstring PageKey(std::wstring_view coreName, int page)
    {
        return LookKey(coreName) + L"." + std::to_wstring(page);
    }

    std::vector<std::wstring> DescribedKeys()
    {
        std::vector<std::wstring> keys;
        for (const auto& p : kPlans)
        {
            if (IsFriendsBoard(p.key)) continue;
            AddKey(keys, p.moment == Moment::Pages ? PageKey(p.key, 1) : LookKey(p.key));
        }
        for (const auto& v : kVariants) AddKey(keys, LookKey(std::wstring(v.key) + std::wstring(v.suffix)));
        AddKey(keys, L"look.crossedout");
        return keys;
    }

    std::optional<int> PhotoPager::Update(bool holding, int index, double now, double promptAt, bool keyHeld)
    {
        if (!holding)
        {
            m_holding = false;
            m_said = -1;
            m_index = -1;
            return std::nullopt;
        }
        if (!m_holding)
        {
            m_holding = true;
            m_heldAt = now;
        }
        if (index != m_index)
        {
            m_index = index;
            m_indexAt = now;
        }
        if (index < 0 || index == m_said) return std::nullopt;
        // The first photo after the prompt said as the camera came up, its line on the bar with it.
        if (m_said < 0)
        {
            const bool prompted = promptAt >= m_heldAt && now - promptAt >= kPromptSettle;
            if (!prompted && now - m_heldAt < kPromptWaitMost) return std::nullopt;
        }
        // The key that paged to it, let go: the photo it lands on.
        if (keyHeld && now - m_indexAt < kKeyWaitMost) return std::nullopt;
        m_said = index;
        return index;
    }

    void PhotoPager::Reset()
    {
        m_holding = false;
        m_heldAt = 0.0;
        m_said = -1;
        m_index = -1;
        m_indexAt = 0.0;
    }

    void Due::Set(double now, bool awaitPrompt, bool afterHud)
    {
        m_pending = true;
        m_awaitPrompt = awaitPrompt;
        m_afterHud = afterHud;
        m_at = now;
    }

    bool Due::Ready(double now, double promptAt, bool keyHeld, double hudSaidAt)
    {
        if (!m_pending) return false;
        if (m_awaitPrompt)
        {
            const bool prompted = promptAt >= m_at && now - promptAt >= kPromptSettle;
            if (!prompted && now - m_at < kLookPromptWaitMost) return false;
        }
        if (m_afterHud && hudSaidAt < m_at && now - m_at < kHudWaitMost) return false;
        if (WaitsForKey(now, keyHeld)) return false;
        m_pending = false;
        return true;
    }

    bool Due::WaitsForKey(double now, bool keyHeld) const
    {
        return m_pending && keyHeld && now - m_at < kKeyWaitMost;
    }

    bool Due::AtEnd(double now)
    {
        const bool says = m_pending && now - m_at <= kAtEndMost;
        m_pending = false;
        return says;
    }

    void Due::Reset()
    {
        m_pending = false;
        m_awaitPrompt = false;
        m_afterHud = false;
        m_at = 0.0;
    }
}
