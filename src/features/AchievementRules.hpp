#pragma once
// The player's achievements as the game unlocks them, worked out without Steam so that it can be
// tested: which ones are new since the mod first read them, and the words they are said in. The game
// draws nothing of its own when one unlocks: its trophy manager (UBatesTrophyManager::UnlockTrophyInternal)
// hands it to the online subsystem, which sets it in Steam, and Steam's overlay shows a toast with the
// achievement's name and its description (Steam's client since 2023; its name alone before), in the
// game's Steam language. That is English for every player: the game declares English as its only base
// language to Steam (`baselanguages`), so its properties offer no language and Steam's API answers in
// English, though Steam holds the other languages too. The mod says achievements in the game's own
// language instead, the one the mod speaks (Andrey's decision of 2026-11-11), from the game's trophy
// strings.

#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace da::achieve
{
    enum class Field
    {
        Name,
        Description
    };

    // The keys of the game's string tables that hold an achievement's words, in the order to try:
    // the trophy strings are keyed by the achievement's Steam name (TRP_SELFIE: BATES_TROPHIES_TRP_SELFIE_N
    // for the name, _D for the description; all 29 of Steam's names have both), and where the game also
    // has a PC wording, "ACH_" for "TRP_" (BATES_TROPHIES_ACH_SYMPHONY_N "A Symphony of Horror
    // Achievements" beside the console's "... Trophies", which Steam's own words follow), that first.
    std::vector<std::wstring> TextKeys(std::string_view id, Field field);

    // The first that is not empty of the game's words, Steam's words and a last resort (the
    // achievement's Steam name, for a name that has no words at all).
    std::wstring Pick(std::wstring_view game, std::wstring_view steam, std::wstring_view last = {});

    // One achievement as Steam reports it: its API name, whether the player has it.
    struct State
    {
        std::string id;
        bool achieved = false;
    };

    // The achievements read at each look. The first complete reading is what the player had already:
    // Steam's toast shows only one unlocked now, and so is said only one unlocked after it. Each one read
    // as achieved that was not is new, once. A reading that is not complete (Steam not ready, an
    // achievement it could not answer for) seeds nothing and loses nothing: what it read as achieved
    // is new all the same. An achievement Steam takes back (its stats reset) can be new again.
    class Watch
    {
    public:
        // `complete`: every achievement Steam lists was read. Returns the ids newly unlocked, in
        // Steam's order.
        std::vector<std::string> Update(const std::vector<State>& states, bool complete);
        bool Seeded() const
        {
            return m_seeded;
        }
        size_t Achieved() const
        {
            return m_achieved.size();
        }

    private:
        bool m_seeded = false;
        std::set<std::string> m_achieved;
    };
}
