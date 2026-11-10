#pragma once
// A totem in the character's hands: the game asks the player to turn it until the view rests
// on the premonition hidden on its surface, and then plays the vision. The game measures the
// view's closeness to that point itself; the mod leads the turn with the aim sound, says
// when the vision begins, and gives its words when the mod has them for the film.
#include "features/Feature.hpp"

#include <string>
#include <vector>

namespace da::features
{
    class TotemsFeature : public Feature
    {
    public:
        const wchar_t* Name() const override
        {
            return L"Totems";
        }
        void Install() override;
        void Describe(std::vector<std::wstring>& out) override;
        void Help(std::vector<std::wstring>& out) override;
    };

    // True while the character holds a totem.
    bool TotemInHand();
}
