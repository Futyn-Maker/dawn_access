#pragma once
// The player's achievements as the game unlocks them, worked out without Steam so that it can be
// tested: which ones are new since the mod first read them. The game draws nothing of its own when one
// unlocks: its trophy manager (UBatesTrophyManager::UnlockTrophyInternal) hands it to the online
// subsystem, which sets it in Steam, and Steam's overlay shows a toast with the achievement's name and
// its description (Steam's client since 2023; its name alone before), in Steam's language.

#include <set>
#include <string>
#include <vector>

namespace da::achieve
{
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
