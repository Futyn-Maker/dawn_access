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
}
