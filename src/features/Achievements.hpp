#pragma once
// The achievements the player unlocks, said as Steam's toast shows them: "Achievement unlocked:" and
// the achievement's name and description in Steam's own words and language, which Steam answers for
// through its API (steam_api64.dll, loaded by the game's online subsystem). The game draws nothing of
// its own for them (features/AchievementRules). A game without Steam's API says nothing.
#include "features/Feature.hpp"

namespace da::features
{
    class AchievementsFeature : public Feature
    {
    public:
        const wchar_t* Name() const override
        {
            return L"Achievements";
        }
        void Install() override;
    };
}
