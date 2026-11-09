#pragma once
// Prompts in play: the key caps the game floats over the world or pins to the screen while
// the character can do something (take, open, continue, back out), the prompt a glint
// shows once the character is close enough, and the action bar at the foot of the screen
// while no menu page is up (continue a lesson, hide a transcript). Each is read as its
// words and key, once it has settled, and again whenever the game asks anew. The phone's
// unlock swipe in the woods is followed here too, as the prompt it is.

#include "features/Feature.hpp"

#include <string>
#include <vector>

namespace da::features
{
    class PromptsFeature : public Feature
    {
    public:
        const wchar_t* Name() const override
        {
            return L"Prompts";
        }
        void Install() override;
        void Describe(std::vector<std::wstring>& out) override;
        void Help(std::vector<std::wstring>& out) override;
    };

    // The last question an interaction asked that was read (its words as said), and when.
    std::wstring LastInteractionPrompt();
    double LastInteractionPromptAt();
    // The actions of that question.
    std::wstring LastInteractionPromptActions();
    // Whether an interaction's question is drawn now.
    bool InteractionPromptShown();
    // Whether a question an interaction draws now asks for the action.
    bool InteractionAsks(std::wstring_view action);
}
