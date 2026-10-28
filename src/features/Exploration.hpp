#pragma once
// Exploration: what the character can walk to while the game lets the player walk it (the
// use locations the level has turned on, each with the name it carries in the level, its
// distance and its direction from the camera's point of view, and the places the level is
// waiting for the character to walk into), a beacon that leads to the chosen target along
// the walkable route where the level has a navigation mesh and straight at it where it has
// none, and a walk that takes the character there by itself.

#include "features/Feature.hpp"

#include <string>
#include <vector>

namespace RC::Unreal
{
    class UObject;
}

namespace da::features
{
    class ExplorationFeature : public Feature
    {
    public:
        const wchar_t* Name() const override
        {
            return L"Exploration";
        }
        void Install() override;
        void Describe(std::vector<std::wstring>& out) override;
        void Help(std::vector<std::wstring>& out) override;
    };

    // Picks the next or the previous thing to walk to and says it. Nothing happens outside
    // exploration.
    void NextTarget();
    void PreviousTarget();
    // Says the target again with its distance along the route and its direction.
    void WhereIsTarget();
    // Walks the character to the chosen target and stops on the next press.
    void WalkToTarget();
    // True while the player is walking the character freely and no screen or mechanic of the
    // game is in the way.
    bool ExplorationActive();
    // True while the mod walks the character to a target by itself.
    bool WalkInProgress();
    // The use location the game offers the character right now, or null.
    RC::Unreal::UObject* OfferedUseLocation();
    // Whether the beacon and the aim sounds are switched on (the one switch serves both).
    bool AimSoundOn();
    // Whether the game has bound the character to an interaction.
    bool CharacterInInteraction();
    // Switches the beacon off and on, and says which.
    void ToggleBeacon();
    // Writes everything the exploration reads (character, camera, navigation data, use
    // locations, glints, trigger volumes, routes) to the log.
    void DumpExploration();
    // The name a use location goes by in the list of things to walk to.
    std::wstring UseLocationLabelOf(RC::Unreal::UObject* useLocation);
    // The use location a glint's widget marks, or null.
    RC::Unreal::UObject* UseLocationOfGlintWidget(RC::Unreal::UObject* glintWidget);
}
