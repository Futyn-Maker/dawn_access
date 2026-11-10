#include "features/AchievementRules.hpp"

namespace da::achieve
{
    std::vector<std::string> Watch::Update(const std::vector<State>& states, bool complete)
    {
        std::vector<std::string> fresh;
        if (!m_seeded)
        {
            // Only a whole reading tells what the player had before: a part of it would leave the rest
            // to be taken for new.
            if (!complete || states.empty()) return fresh;
            for (const auto& state : states)
            {
                if (state.achieved) m_achieved.insert(state.id);
            }
            m_seeded = true;
            return fresh;
        }
        for (const auto& state : states)
        {
            if (state.achieved && m_achieved.insert(state.id).second) fresh.push_back(state.id);
        }
        // A whole reading also tells what is no longer had, so that it can be new again.
        if (complete)
        {
            std::set<std::string> now;
            for (const auto& state : states)
            {
                if (state.achieved) now.insert(state.id);
            }
            m_achieved.swap(now);
        }
        return fresh;
    }
}
