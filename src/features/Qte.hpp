#pragma once
// Quick-time events: the key or button the game asks for is said and played as a direction
// the moment the game sets the prompt up, played again when the game starts taking the
// press, repeated every second while it waits, and its outcome told by the game's own
// verdict: a tone, and the word when the mod is detailed.

#include "features/Feature.hpp"

#include <string>
#include <vector>

namespace da::features
{
    class QteFeature : public Feature
    {
    public:
        const wchar_t* Name() const override
        {
            return L"Qte";
        }
        void Install() override;
        void Describe(std::vector<std::wstring>& out) override;
        void Help(std::vector<std::wstring>& out) override;
    };
}
