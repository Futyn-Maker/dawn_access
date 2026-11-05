#pragma once
// Shooting: a stage of an activity in which the player aims and fires (a rifle at the range, a
// snowball, later pistols). The game highlights the target nearest its aim within a few pixels of
// the screen's centre, and a shot takes the highlighted target and nothing else; the mod leads the
// aim to the targets with the aim sound, plays the double ping while the game would take a shot,
// says the word to fire once it has held, and tells how the stage ended from the game's own verdict.

#include "features/Feature.hpp"

#include <string>
#include <vector>

namespace da::features
{
    class ShootingFeature : public Feature
    {
    public:
        const wchar_t* Name() const override
        {
            return L"Shooting";
        }
        void Install() override;
        void Describe(std::vector<std::wstring>& out) override;
        void Help(std::vector<std::wstring>& out) override;
    };

    // True while a shooting stage runs and the world is not standing still.
    bool ShootingActive();
    // The aim sound's switch ([Shooting] AimSound), which the beacon key turns while aiming and
    // while a look is led to a framing; toggling says which and keeps it in the ini.
    bool ShootingSoundOn();
    void ToggleShootingSound();
}
