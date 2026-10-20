#pragma once
// Menus: every screen the game builds of its pages is read as the player arrives on it (its
// tab, its title, its text, the focused control and the prompts of the action bar), every
// control as the focus lands on it (with the heading above it when that changes), every
// value as it changes under the focus, and the prompts as they change.

#include "features/Feature.hpp"

#include <string>
#include <vector>

namespace da::features
{
    class MenusFeature : public Feature
    {
    public:
        const wchar_t* Name() const override
        {
            return L"Menus";
        }
        void Install() override;
        void Describe(std::vector<std::wstring>& out) override;
        void Help(std::vector<std::wstring>& out) override;
    };

    // Forgets the screen being read (after a map change).
    void ResetMenus();
}
