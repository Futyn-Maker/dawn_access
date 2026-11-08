#pragma once
// HUD texts: what the game lays over the view in play and expects the player to read on
// their own: the chapter and character introductions, the cards about a collectible or a
// change of status, the side bars with a hint and its key, a note held up to be read, the
// loading screen. Each is read once it has settled, and again when its words change. The
// elements the player acts on (subtitles, prompts, choices, quick-time events, Don't Move)
// belong to their own features; the pages of a menu to the menus.

#include "features/Feature.hpp"

#include <string>
#include <vector>

namespace da::features
{
    class HudFeature : public Feature
    {
    public:
        const wchar_t* Name() const override
        {
            return L"Hud";
        }
        void Install() override;
        void Describe(std::vector<std::wstring>& out) override;
    };

    namespace hud
    {
        // A HUD text shown a moment ago is still to be said: a control's prompt that comes up with
        // it waits for it, so that the hint comes first and its keys after. At the lodge's two
        // totems on 2026-11-04 the inspection's hint and its stick came up within a frame of each
        // other and were said in one order at the first totem and in the other at the second.
        // Waits at most kHintFirst from the text's showing. A hint the game draws a moment after
        // the prompt is waited for by the prompt itself (prompt::HintFirst).
        constexpr double kHintFirst = 0.6;
        bool TextSettling(double now);
        // When an element's words were last said; long ago when none was.
        double LastSaidAt();
        // The objective the game keeps for the player now, as last read; empty when none is known.
        std::wstring Objective();
        // When a document's transcript (a BatesReadingWidget) was last seen drawn; long ago when never.
        double DocumentDrawnAt();
        // When a document's page was last shown again, unchanged, as it was being read, and left to
        // be read on; long ago when never. The prompts the bar shows again with it are not said again.
        double PageResumedAt();
        // The card that changes the character (name, place, time) is up now.
        bool CharacterCardShown();
        // An objective is set and waits to be said (for a title card to go, or for play to run).
        bool ObjectiveDue();
    }
}
