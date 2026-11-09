#pragma once
// The interactions that need more than their prompts: the ones that wait for the view to rest on a
// framing (a telescope's sight, the selfie's shot), led by the look beacon in the game's own
// measure of the view; an item in hand turned with the stick until it reveals (a note's other side,
// a clue), led the same way; and the boiler's pressure lamp, which times the press it waits for.

#include "features/Feature.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace RC::Unreal
{
    class UObject;
}

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
    // The staged stage the interaction stands in, where its steps sound (prompt::SayAtStep): a number
    // for that stage of that interaction, another for the next interaction bound; -1 for none.
    int64_t SoundingStagedStage();
    // The action of the staged stage's own prompt (its InputActionAssigned) where the interaction
    // stands in one; empty for none (prompt::SayAtStep).
    std::wstring StagedStageInput();
    // The step the staged stage stands at, where its steps sound; -1 for none.
    int64_t StagedStageStep();
    // Whether the stage of the bound interaction that drew a prompt widget toggles the grab
    // (UBatesInteractionStageBase.bUsingToggleGrab): its current stage's answer where several share
    // the widget, none where no stage of the bound interaction drew it.
    std::optional<bool> GrabTogglesFor(RC::Unreal::UObject* widget);
}
