#include "features/AchievementRules.hpp"

namespace da::achieve
{
    std::vector<std::wstring> TextKeys(std::string_view id, Field field)
    {
        if (id.empty()) return {};
        const std::wstring wide(id.begin(), id.end()); // Steam's API names are ASCII
        const std::wstring suffix = field == Field::Name ? L"_N" : L"_D";
        std::vector<std::wstring> keys;
        if (wide.starts_with(L"TRP_")) keys.push_back(L"BATES_TROPHIES_ACH_" + wide.substr(4) + suffix);
        keys.push_back(L"BATES_TROPHIES_" + wide + suffix);
        return keys;
    }

    std::wstring Pick(std::wstring_view game, std::wstring_view steam, std::wstring_view last)
    {
        if (!game.empty()) return std::wstring(game);
        if (!steam.empty()) return std::wstring(steam);
        return std::wstring(last);
    }

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
