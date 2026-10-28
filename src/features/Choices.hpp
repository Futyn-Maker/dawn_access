#pragma once
// Choices: a decision the game offers as two options, left and right, each with its words
// and the key or button that takes it. The options are read together once both are in, with
// the clock's sound when the choice is timed; the option taken is told by a tone and, when
// the mod is detailed, by its words once the keys are released; a choice the time ran out on
// is told as time being up.

#include "features/Feature.hpp"

#include <string>
#include <vector>

namespace da::features
{
    class ChoicesFeature : public Feature
    {
    public:
        const wchar_t* Name() const override
        {
            return L"Choices";
        }
        void Install() override;
        void Describe(std::vector<std::wstring>& out) override;
        void Help(std::vector<std::wstring>& out) override;
    };

    // True while a choice is up and not yet settled.
    bool ChoiceInProgress();
}
