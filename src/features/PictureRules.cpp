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
        constexpr std::array<Planned, 6> kPlans = {
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
        };
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
