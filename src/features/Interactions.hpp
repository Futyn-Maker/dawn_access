#pragma once
// The interactions that need more than their prompts: the ones that wait for the view to rest on a
// framing (a telescope's sight, the selfie's shot), led by the look beacon in the game's own
// measure of the view; an item in hand turned with the stick until it reveals (a note's other side,
// a clue), led the same way; and the boiler's pressure lamp, which times the press it waits for.

#include "features/Feature.hpp"

#include <string>
#include <vector>

namespace da::features
{
    class InteractionsFeature : public Feature
    {
    public:
        const wchar_t* Name() const override
        {
            return L"Interactions";
        }
        void Install() override;
        void Describe(std::vector<std::wstring>& out) override;
        void Help(std::vector<std::wstring>& out) override;
    };

    // True while the look beacon leads a framing an interaction waits for.
    bool FramingLed();
    // True while the look beacon leads the turn of an item in hand to its reveal.
    bool RevealLed();
    // True while the interaction the character is bound to is in a stage that runs on a clock.
    bool InteractionStageTimed();
}
