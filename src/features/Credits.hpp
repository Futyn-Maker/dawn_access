#pragma once
// The epilogue: the credits manager (UBatesCreditsManager) runs the films of the dead, the survivors'
// police interviews and the roll of the credits over them. Read as the HUD read them, every credit
// came three or four times, the interviews' clock at every tick, and the interviews themselves were
// lost among them (research/sessions/s64-play.log, 11:37:09 to 11:49:22). Here each has its own rule:
// - the roll is kept, each line once in its order, and read on the player's key: what came since the
//   last reading, all of it the first time (credits::Roll); the key is said once as the roll begins,
//   and the help names it while the roll is up;
// - the overlay over an interview says its room and its clock as the interview begins, then the clock
//   as its hours or minutes change (credits::Overlay); the tape's number is read with the screen;
// - a film of the dead says whose death it shows, by the name the scene's script gives it (the film's
//   widget draws no text of its own, and a sighted player knows the face), then what the film shows
//   (lang/films, by the film's name).
#include "features/Feature.hpp"

#include <string>
#include <vector>

namespace da::features
{
    class CreditsFeature : public Feature
    {
    public:
        const wchar_t* Name() const override
        {
            return L"Credits";
        }
        void Install() override;
        void Describe(std::vector<std::wstring>& out) override;
        void Help(std::vector<std::wstring>& out) override;
    };
}
