#pragma once
// The world as the player finds it, whatever part of it the game holds now, worked out without the
// game so that the tests can replay it. The game streams an act's sections in and out as she walks
// (UBatesLevelStreamingManager: a trigger's LoadOnEntry shows a section, a HintOnEntry loads one
// ahead and lets another go), and a sighted player never sees it happen: what is let go lies where
// she cannot see it, and the game brings it back before she can get there. The ground the mod felt
// out stands for the world as she will find it: what a section's levels held is kept while they
// are away, and what was felt out while a level was away is asked again once it is back.
//
// In the asylum's hall in act4_2 on 2026-11-07 Section_03 (the entry hall's walls, the church's
// corridors, the porch: 33 levels) went as Mike walked to the basement's trigger and came back as
// he walked to the chapel door. The mod's ground had forgotten every wall it knew when the levels
// went, kept every step it found free, and the steps it then found free through the walls that had
// gone stayed free once the walls were back: some forty steps through a corner of the hall and the
// porch's wall by the front door, which every walk after ran into ("Пройти не удалось", six times;
// the two stops in the hall on the evening before were the same).

#include <set>
#include <string>
#include <vector>

namespace da::stream
{
    // What an answer about the ground rests on: a thing a trace met (`hit`), in a level of the
    // world (its id in World, -1 when not known), or nothing being in the way (a step found free,
    // no floor found, a floor beyond a step's reach).
    struct Basis
    {
        bool hit = false;
        int level = -1;
    };

    // Whether an answer stands once the world's levels have changed (`cameIn`: some came in;
    // otherwise some only went). One that rests on nothing in the way stands while nothing came,
    // since what came may stand in the way. A thing of a level the world holds stands as a wall
    // (`wall`: a step swept into it, more of the world blocking no less), and as a floor while
    // nothing came, since a level come in may lay one above it, a stair over a ledge. A thing of a
    // level away (`present` false) stands if that level is one the streaming brings back
    // (`streamed`), as she will find it, and goes if not: a scene's own level, gone for good.
    bool Kept(const Basis& basis, bool wall, bool cameIn, bool present, bool streamed);

    // Whether an answer may be asked again (it has aged, the story moved on, an interaction ended):
    // never one that a streamed level away now holds, since the world without it answers for the
    // hole the game leaves where she cannot see.
    bool Askable(const Basis& basis, bool present, bool streamed);

    // Whether the world may be felt out: no level of it being added or removed (UWorld's
    // CurrentLevelPendingVisibility and CurrentLevelPendingInvisibility), every level it lists
    // fully added (ULevel::bIsVisible: the engine lists a level while its parts are still being
    // registered, a few each frame, and removes it once their collision is gone), and its list
    // unchanged for kSettle.
    constexpr double kSettle = 1.5;
    bool Settled(bool adding, bool removing, bool allShown, double sinceChange);

    // Whether the ground is laid afresh where she stands: none laid yet, another body (another
    // character's capsule and steps), or farther than kGridReach from where it was laid. Laid afresh,
    // everything felt out is forgotten, what the streamed levels away held with it: on the trail above
    // the cable car's station on 2026-11-07 the grid was laid anew 66 m from its first spot after the
    // station's levels had gone, and the new map walked through the station's walls the game had let
    // go, listing its things again 100 m behind (research/sessions/s55-full.log). An act's ground is
    // kept whole.
    constexpr double kGridReach = 100000.0;
    bool Reanchors(bool anchored, bool sameBody, double fromOrigin);

    // The levels of the world by name: an id for each, the ones the world holds, and the ones the
    // streaming brings back, which came or went with a level the streaming triggers name (the
    // section's own level, its parts with it) or one of the streaming manager's levels.
    class World
    {
    public:
        // A count's change: the levels that came in and went out, and whether it is another world
        // (`fresh`: the first count, or another world: every level held now is all in).
        struct Change
        {
            std::vector<int> in;
            std::vector<int> out;
            bool fresh = false;
        };

        int Id(const std::wstring& name);
        const std::wstring& Name(int id) const;
        // A settled count of the world's levels, by name; `sections` the names of the levels the
        // game streams (the triggers' LoadOnEntry and HintOnEntry, the streaming manager's own);
        // `fresh` when the levels are another world's, though their names be the same (a checkpoint
        // loaded anew: the engine's levels are all new ones).
        Change Count(const std::vector<std::wstring>& levels, const std::set<std::wstring>& sections, bool fresh);
        // Whether the world holds a level; one not known (-1) counts as held.
        bool Present(int id) const;
        bool Streamed(int id) const;

    private:
        std::vector<std::wstring> m_names;
        std::vector<bool> m_present;
        std::vector<bool> m_streamed;
        bool m_counted = false;
    };
}
