#include "features/PictureRules.hpp"

#include "core/Strings.hpp"

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
        };

        // The interactions whose pictures the mod describes, and when; the moments are the game's own
        // states, as the logs of chapters 5 and 6 show them.
        constexpr std::array<Planned, 28> kPlans = {
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
            // things in the car at the lower station (act1_3, no core name), the hotel poster and the torn
            // wanted poster (act1_3), the empty axe case (act2_11, act2_3), the prom and beach photos, whose
            // photo side the game leaves unsaid as it reads their backs (act2_11; the prom photo's copy of
            // chapter 5 too), the mine's danger map, the cigar stub's band and the painted symbol (Act2_2),
            // the postcard's other side, Dr. Hill's painting (act2_4), the bench's carved heart on its second
            // wipe and the pig's head note's back (act2_3).
            Planned{L"i_car_lookat", L"bp_car_lookat_c", Moment::State, L"examine_idle"},
            Planned{L"i_hotel_poster", L"bp_hotel_poster_c", Moment::State, L"examine_idle"},
            Planned{L"i_wanted_poster_lookat_new", L"bp_wanted_poster_lookat_new_c", Moment::State, L"examine_idle"},
            Planned{L"i_missing_axe", L"bp_missing_axe_c", Moment::State, L"examine_idle"},
            Planned{L"i_prom_night_photo", L"bp_prom_night_photo_c", Moment::State, L"ik_animation"},
            Planned{L"i_prom_night_photo_female", L"bp_prom_night_photo_female_c", Moment::State, L"ik_animation"},
            Planned{L"i_hannah_and_beth_beach_photo", L"bp_hannah_and_beth_beach_photo_c", Moment::State, L"ik_animation"},
            Planned{L"i_minemap", L"bp_minemap_c", Moment::State, L"look_idle1"},
            Planned{L"i_cigarstub", L"bp_cigarstub_c", Moment::State, L"ik_animation_run"},
            Planned{L"i_mysticalsymbols", L"bp_mysticalsymbols_c", Moment::State, L"examine_idle"},
            Planned{L"i_childsdrawing", L"bp_childsdrawing_c", Moment::Reveals, L""},
            Planned{L"i_snowybench", L"bp_snowybench_c", Moment::State, L"BenchWipe2"},
            Planned{L"i_readnote", L"bp_readnote_c", Moment::Reveals, L""},
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
                if (p.key == lower || (!p.className.empty() && p.className == lower)) return LookPlan{p.moment, std::wstring(p.state), std::wstring(p.key)};
            }
        }
        return std::nullopt;
    }

    std::wstring LookKey(std::wstring_view planKey)
    {
        return L"look." + str::ToLower(planKey);
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

    void Due::Set(double now, bool awaitPrompt)
    {
        m_pending = true;
        m_awaitPrompt = awaitPrompt;
        m_at = now;
    }

    bool Due::Ready(double now, double promptAt, bool keyHeld)
    {
        if (!m_pending) return false;
        if (m_awaitPrompt)
        {
            const bool prompted = promptAt >= m_at && now - promptAt >= kPromptSettle;
            if (!prompted && now - m_at < kLookPromptWaitMost) return false;
        }
        if (WaitsForKey(now, keyHeld)) return false;
        m_pending = false;
        return true;
    }

    bool Due::WaitsForKey(double now, bool keyHeld) const
    {
        return m_pending && keyHeld && now - m_at < kKeyWaitMost;
    }

    void Due::Reset()
    {
        m_pending = false;
        m_awaitPrompt = false;
        m_at = 0.0;
    }
}
