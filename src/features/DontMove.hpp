#pragma once
// Don't Move: the game asks the player to keep the character still, either by holding the
// controller motionless or by steering a marker back into its zone against a drift. The
// start is marked by a tone and the game's own words, the version by the mod's instruction
// once the game has chosen it, the marker by a tone that follows it while it lasts, the
// game's lesson in the mechanic by its own words as the game sets them, and the outcome by
// the game's own verdict: a tone, and the word when the mod is detailed.
#include "features/Feature.hpp"

#include <string>
#include <vector>

namespace da::features
{
    class DontMoveFeature : public Feature
    {
    public:
        const wchar_t* Name() const override
        {
            return L"DontMove";
        }
        void Install() override;
        void Describe(std::vector<std::wstring>& out) override;
        void Help(std::vector<std::wstring>& out) override;
    };

    // True while a Stay Calm encounter is running.
    bool DontMoveInProgress();
}
