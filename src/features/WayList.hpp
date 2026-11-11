#pragma once
// Which of the ways on that a scene offers the exploration list keeps, worked out without the
// game so that it can be replayed over recorded ground in the tests: the road the walk to a
// target takes over the reach map, pulled straight; the row rule, by which of a row of ways
// along one road only the farthest is listed; and when a fresh reach map has settled the list,
// so that no way is listed, or announced, only to be left out a moment later.

#include "features/GridPath.hpp"
#include "features/Vec.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace da::ways
{
    using features::world::Vec;

    // ---- the road over the reach map ---------------------------------------------------------

    // Pulls a path the reach map found straight wherever the map reached the ground all along the
    // straight line between two of its cells: the columns between them, one per cell along the
    // longer side, each reached on a level within one of the last. Those are the corners the
    // game's own straightener keeps where it sweeps the character between the cells, found
    // without a sweep, and the same whichever of two equal ways the map took: its own cells
    // zigzag between such ways, and whether that zigzag brushed a way beside it decided whether
    // the way was listed (the woods' start on 2026-10-28). `maxRun` bounds a straight run, in
    // cells along the path, as Straightener's does.
    std::vector<grid::Cell> StraightOverMap(const grid::Flood& flood, const std::vector<grid::Cell>& path, size_t maxRun);

    // ---- the row rule ------------------------------------------------------------------------

    // A target as the row rule weighs it.
    struct Entry
    {
        bool way = false;         // a way on, a volume the level waits for the character in; else a thing to use
        bool hasBox = false;      // the way's volume is known
        double distance = 0.0;    // as it is said: on foot where the road is known
        bool behind = false;      // behind the character on the path she is held to
        bool unreachable = false; // a way no walk gets to for now
        bool unjudged = false;    // a way whose ground is not known yet
        bool reached = false;     // the character is there
        bool chosen = false;      // the player chose it: listed whatever the rules say
        bool closedOff = false;   // the ground was proven closed off from it, no door between: its road ends short of it
        bool known = true;        // its road is known now: the navigation mesh, the map, the path she is held to
        bool earlier = false;     // an earlier map found it reachable, or beyond a door, and this one has not judged it yet
        // In the list at the last look: a way listed (judged and kept in), a thing whatever its own
        // verdict (one still waiting for the map may yet give a way its road).
        bool shown = false;
        bool streams = false;     // a way of no story that streams in more of the world (StreamsMore)
        // The way the scene's objective leads to: the scene waits for her there to turn its objective
        // off or set the next (scripts::EndsObjective). It is never left out on another's road.
        bool goal = false;
        // Such a trigger given way to a way of the story (StreamsSuperseded), as Lister::Judge finds
        // it at each look: no road of the row rule, and not listed.
        bool superseded = false;
        // The list's verdict, kept from look to look: whether the row rule has judged it since it
        // came, and the index of the target whose road it lies on, or -1.
        bool passed = false;
        int onRoadOf = -1;
        // The stretch of story (Lister::StoryMoved) and the hold (Lister::NewHold) the verdict was
        // given in, and whether it stands as it is at this judgement (Lister::Judge sets it): a
        // verdict of the hold given before the list froze, or a listed way's.
        int epoch = -1;
        int hold = -1;
        bool kept = false;
    };

    // Whether the road the walk to target `other` takes runs through the volume of way `way`
    // (indices into the entries); nothing where that road is not known.
    using Enters = std::function<std::optional<bool>(size_t way, size_t other)>;

    // The row rule: a way on the road the walk to another listed target takes is left out, since
    // that walk sets it off anyway. Ways are judged from the farthest in, each against the roads of
    // every thing to use, of the chosen target, and of the farther ways found to stay listed, so
    // that two ways never leave each other out, nor a road that is itself left out leaves a way
    // out. A way behind, unreachable, unjudged, reached or chosen is not judged; a road counts only
    // from a target listed and not closed off (the road to a closed-off thing ends short of it, a
    // dead end no walk gets through), used before or not: a road that left ways out only while its
    // thing was not chosen offered the shed's way on in act6_4 and took it back the moment Andrey's
    // list came to the scarecrow he had looked at (research/sessions/s61-play.log, 18:48:44 to
    // 18:48:47), and he asked for the rule to go ("let it be what it is"). An entry whose verdict is
    // `kept` keeps it, and counts as a road as it stands, unless the road it lies on is a trigger
    // given way to a way of the story (`superseded`): such a trigger is no road, judged or kept, and
    // is not judged itself.
    //
    // The way the scene's objective leads to (`goal`) is never left out, nor keeps a verdict that left
    // it out, and counts as a listed way: in the lodge on 2026-11-09 the basement's trigger, where
    // "Разыщите остальных" waited for Sam (Scene03_find_our_friends 3: SF_TurnOffCurrentObjective),
    // lay on the road to the beach photo in the basement beyond it, and Andrey looked for a way on for
    // seven minutes (research/sessions/s64-play.log, 11:22:20.209 to 11:29:35.741).
    //
    // A road not known (`enters` gives nothing) proves nothing: the way keeps the verdict it had on that
    // target. The basement's way came at last only because the photo's road had gone unknown as Sam
    // walked off, and was taken for a road that runs through nothing.
    // Returns, for each entry, the index of the target whose road it lies on, or -1.
    std::vector<int> RowRule(const std::vector<Entry>& entries, const Enters& enters);

    // A trigger that only streams in more of the world (StreamsMore) is a way on only while no way
    // of the story leads on: one the list offers, or leaves out on a listed road. The game streams a
    // section in as she walks toward it, so a way of the story leads through as well; the trigger is
    // the way on only where the map stops at ground the game has not loaded and nothing of the story
    // is reachable (the asylum's grounds, 2026-11-07). In the lodge on 2026-11-08 the candlelight's
    // way stood listed with four triggers into the kitchen, the cinema and the cellar beside it; one
    // walked to loaded the cinema and left the list, nothing of the story behind it ("decorative").
    // Returns, for each entry, whether it is such a trigger with a way of the story listed. Lister::Judge
    // weighs it at each look by the ways as that look judges them, before the row rule: weighed
    // apart, with the story's way still unjudged from the look before, the two rules each left the
    // other's way out. After the dead wolf in act7_2 the scene's way on (T_leave_dead_wolf2) was
    // judged to lie on the road to a trigger into another section, which was not given way to yet;
    // a look later the trigger gave way to the scene's way, and neither was listed for the three
    // minutes Andrey looked for one (research/sessions/s62-play.log, 21:54:42.773 to 21:57:17).
    std::vector<bool> StreamsSuperseded(const std::vector<Entry>& entries);

    // A way's volume as a box turned about the upright: its middle, its sides along its own axes,
    // its yaw in degrees, and the box round it on the world's axes (the actor's bounds).
    struct Box
    {
        Vec middle;
        Vec size;
        double yaw = 0.0;
        Vec low;
        Vec high;
    };
    Box TurnedBox(const Vec& middle, const Vec& size, double yaw);

    // Whether a road walked at the character's feet, sampled every `step` cm, comes within
    // `radius` of a box at a height the character's capsule spans (`halfHeight` is half of it):
    // the walk there sets the volume off. Exploration asks the engine for the distance to the
    // volume itself (GetClosestPointOnCollision), which comes out the same for a box.
    bool RoadEntersBox(const Box& box, const std::vector<Vec>& road, double radius, double halfHeight, double step = 25.0);

    // The path the game holds her to, as points sampled along its spline from where she stands, put at
    // her feet: a spline lies where its designer drew it, and a road is walked at her feet
    // (RoadEntersBox). The stream's path in act8_6 runs along the water's surface, 196 cm over Sam's
    // feet, and the climb out, whose box ends 7.5 cm under the path, was left out as beside the path
    // until she stood in it (research/sessions/s64-play.log, 11:15:59.326 to 11:17:28.981). The points
    // keep their rise along the path from `atHer`, the spline's height where she stands.
    std::vector<Vec> PathAtFeet(std::vector<Vec> samples, double atHer, double feet);

    // After the list's verdicts are given anew for a change of the world or the story at `changedAt`, the
    // first map whose verdicts may stand (Lister::Freeze) is the next one begun, or the one running now
    // where it was begun for that change: at it or after it. A change of the world or the ground drops
    // the map, and the next is begun later in the same frame at the frame's own time, the change's;
    // a change of the story is stamped later in its frame than a map begun in it. Returns the
    // generation after which a map counts. A prop the game moved in the lodge on 2026-11-09 began a map
    // 18 ms before the verdicts were given anew, and the list waited 25 s for the map after it.
    int64_t FreezeAfter(uint64_t generation, double startedAt, double changedAt);

    // Whether the character standing at `feet` sets a volume off, over the ground: the game sets a
    // volume off once her capsule, `radius` around her, overlaps it. The cable car's doorway at
    // chapter one is 51 cm deep, and the platform's ground ends at the car's threshold 49 cm from
    // its middle, where her capsule already overlaps it; judged by a cell within the volume's own
    // half width of its middle, the doorway was found closed off and never offered (2026-11-01).
    bool CapsuleOverlaps(const Box& box, const Vec& feet, double radius);

    // A circle about a volume's middle every cell of which sets it off, in cells, for the searches
    // that head for a circle: the volume's own half width (the narrower of its two) and the
    // capsule's radius, and no less than a cell and a half.
    double VolumeGoalRadius(const Box& box, double capsuleRadius, double cell);

    // ---- doors -------------------------------------------------------------------------------

    // Whether a door is one the character only tries: every animation the door would bind her
    // with, one per body (the door interaction's GenericBoundCharacter), is the one that rattles
    // a locked door (ABP_Door_Locked_*). Those doors are of the same blueprint family as the doors
    // that open, and the animation is the one thing that tells them apart: the station's front
    // door at chapter one binds Sam to ABP_Door_Locked_Right_Female and never opens (2026-11-01),
    // the lodge's doors bind ABP_Door_Push_Right_Open_Female and the like. What lies behind a
    // door she only tries is closed off for now. A door naming no animation is not known locked.
    bool OnlyTried(const std::vector<std::wstring>& animations);

    // ---- who may use a use location ------------------------------------------------------------

    // Whether the player's character may use a use location, as the game decides it
    // (ABatesUseLocation::CanCharacterUseUseLocation): one that requires a valid animation for her
    // takes her only when its interaction has one (her own entry among its bound characters, else
    // a generic interaction's for her body); then anyone may, or only the character it names. A
    // photo of the lodge has two use locations at one spot, one for each body
    // (I_hannah_and_beth_beach_photo and its _female twin, 2026-11-02): Chris was offered both.
    struct UseRule
    {
        bool requiresAnimation = false; // bRequireValidCharacterABP
        bool animated = false;          // the interaction has an animation for her
        bool anyone = false;            // bCanAnyoneInteract
        bool hers = false;              // CharacterToInteract is the player's character
    };
    bool UsableBy(const UseRule& rule);

    // ---- what the game offers ------------------------------------------------------------------

    // Which of the use locations on the game's list of those at hand she stands in: the list is her
    // interaction component's AvailableUseLocations, to which a use location adds itself as she walks
    // into its volume and from which it takes itself off as she walks out
    // (ABatesUseLocation::OnCharacterVolumeStateChanged, 0x44e6bb0). The list can keep one she has
    // left: after the vista's telescope on 2026-11-05 it kept the telescope while Mike went through
    // the gate and down to the loss totem 14 m off, and taken as offered, the telescope was said to be
    // where he stood and the walk to the totem stopped 2 m short of it, "near, but the game offers
    // Telescope". The game's own press never takes such a one: it goes to the nearest one on the list
    // she can use (UPlayerInteractionComponent::FindClosestInteractableUseLocation, 0x46132e0), whose
    // test asks first whether she stands in its volume (her trigger component's
    // IsInsideTriggerVolume). `inside` says that of each entry; returns the indices of those she
    // stands in, in the list's order.
    std::vector<size_t> StoodIn(const std::vector<bool>& inside);

    // ---- how far the list reaches ---------------------------------------------------------------

    // A target comes into the list within the range of the settings and leaves it only kRangeMargin
    // beyond: at the mine on 2026-11-05 the vista's telescope, info board and gate stood 99 to 101 m
    // off while Mike walked about below, and came and went from the list six times in fourteen
    // minutes, said as news at each coming.
    constexpr double kRangeMargin = 3000.0;
    bool InRange(double distance, double range, bool listedBefore);

    // ---- settling ----------------------------------------------------------------------------

    // When a fresh reach map has settled which ways the list keeps. The map reaches the near
    // ground first, and the road to a farther target may yet run through a nearer way, so the ways
    // it judges wait until it has judged every target, has run out of cells or of budget, or has
    // run kSettleCap seconds, whichever comes first. The first map of a hold lists no way
    // meanwhile; a map begun while an earlier one's list stands (the character walked on, the
    // story moved on, a short exchange ended) leaves that list as it was until it settles, so
    // that no way goes and comes back.
    constexpr double kSettleCap = 3.0;

    class Settling
    {
    public:
        // A hold begins afresh: no list stands, and none settles before a map has begun.
        void Reset();
        // A map has begun.
        void Begin(double now);
        // No map judges the ways (the navigation mesh gives the roads): the list stands as it is.
        void Clear();
        // Takes the map's state at a look: whether it has run out (`done`), and how many of the
        // targets it has judged. True at the look the list settles, when the row rule must judge
        // it before anything is listed or said.
        bool Update(double now, bool done, size_t judged, size_t targets);
        bool Settled() const
        {
            return m_settled;
        }
        // Settling while an earlier list stands: the targets judged then keep their verdicts.
        bool Keeping() const
        {
            return !m_settled && m_keep;
        }
        // The map began while an earlier list stood: once it has settled, a way it has not judged
        // yet keeps what the earlier map found until it does (a way the list has said does not go
        // for a while because the map begun as she walked on has not reached it yet).
        bool Kept() const
        {
            return m_kept;
        }
        double StartedAt() const
        {
            return m_startedAt;
        }
        // A map has begun since the hold began.
        bool Begun() const
        {
            return m_begun;
        }

    private:
        bool m_settled = false;
        bool m_keep = false;
        bool m_once = false;  // a list has settled since the hold began
        bool m_begun = false; // a map has begun since
        bool m_kept = false;  // the map began while an earlier list stood
        double m_startedAt = 0.0;
    };

    // The list at every look, as Exploration's ChooseListed takes it. A way's `unjudged` follows the
    // settling: no way is judged before the list settles, and none whose road is not known after
    // (but what an earlier map found, under a map begun while its list stood), while a target the
    // rule judged before keeps its verdict as a later map settles.
    //
    // A way not listed yet waits for the map to be done while anything is in the list (`mapDone`;
    // a thing counts while it waits for the map, a way once listed): what
    // the map has not reached yet may lie beyond it, its road running through the way, and a way
    // listed meanwhile was left out a moment later. After the landing door on 2026-11-07 the map of
    // the lodge ran 18 s; the way on to the floating balloon was listed 3.4 s in and left out 1.6 s
    // later for the road to the prom photo, and the cinema's way 3 s in, left out 0.8 s later
    // (research/sessions/s56-full.log). With nothing else listed a way is judged once the list
    // settles, as before; and a way once listed keeps its verdict for the rest of the hold: the list
    // does not take back what it has offered for a road it learns of later. The row rule
    // judges the list at the look it settles, before anything is listed or said (the woods' start
    // on 2026-10-28 listed four ways at the look it settled and left three out at the next), then
    // once a second, and at the first look after Again().
    //
    // Once the first map of a hold has run out its verdicts stand for the rest of the hold (Freeze):
    // the roads it judged by are those from where she stood, and judged again from where she walks
    // they took ways off the list and put them back as she went (the mine on 2026-11-05: two ways on
    // listed, left out and listed again within five seconds as Mike walked to the totem). After the
    // freeze only a target that came since, or one whose road-giver has left the list, is judged; a
    // new hold, or the ground grown, judges every one afresh.
    //
    // The story moving on judges afresh every target but a way listed: what the list has offered
    // stays offered for the hold while the scene still waits for her there. On the tower's hill on
    // 2026-11-08 the security light's way was said as news, and 25 s later a trigger Emily walked
    // through on the way to the totem moved the story on; judged again, the way lay on the road to the
    // death whistle and left the list without a word, and ten seconds later she walked into it and its
    // scene began (research/sessions/s57-full.log, 01:32:54 to 01:33:28).
    //
    // Only a verdict the row rule gave counts: a way it could not weigh at a judgement (unjudged then)
    // waits for the judgement that weighs it, and is not listed meanwhile. On the deer's hill on
    // 2026-11-08 the list settled while the things beside the path still counted as listed, so both
    // ways waited for the map; a third of a second later the things were out, both ways were listed
    // with no verdict, and the next judgement kept the nearer one, though the walk to the farther ran
    // through it (01:28:15).
    class Lister
    {
    public:
        // Whether the row rule judges the list at this look.
        bool Due(const Settling& settle, bool settlesNow, double now) const;
        // Each entry's `unjudged` and `superseded` at this look, and, when `due`, the row rule over
        // the entries: `onRoadOf` and `passed` for every one it weighed. Without `roads` (neither a
        // map nor the navigation mesh judges them: a hand-over before its first map, a search that
        // laid the grid afresh) a list that stands stays as it is.
        void Judge(std::vector<Entry>& entries, const Settling& settle, bool due, bool roads, bool mapDone, double now, const Enters& enters);
        // The next look judges the list: a target was judged, came or went.
        void Again()
        {
            m_judgedAt = -10.0;
        }
        // A hold begun afresh, or the ground grown under the list: every verdict is given anew.
        void NewHold()
        {
            ++m_epoch;
            ++m_hold;
            m_frozen = false;
            Again();
        }
        // The story moved on: every verdict is given anew but a listed way's.
        void StoryMoved()
        {
            ++m_epoch;
            m_frozen = false;
            Again();
        }
        // The hold's first map has run out: its verdicts stand.
        void Freeze()
        {
            m_frozen = true;
        }
        bool Frozen() const
        {
            return m_frozen;
        }

    private:
        // Whether an entry's verdict stands as the rule gave it in this hold: given since the last
        // change of story, or a listed way's of the hold.
        bool Weighed(const Entry& e) const
        {
            return e.passed && (e.epoch == m_epoch || (e.way && e.shown && e.hold == m_hold));
        }

        double m_judgedAt = -10.0;
        int m_epoch = 0;
        int m_hold = 0;
        bool m_frozen = false;
    };

    // ---- the ground changes under a list that stands ------------------------------------------

    // A list stands for the ground its map judged, and that ground changes at doors, at gates and
    // where a scene carries her on: at the lodge on 2026-11-05 Sam opened the landing door in a short
    // exchange, the hold took up the list it had left, and the way on at the head of the stairs, judged
    // while it lay beyond the door, kept its verdict for four minutes, though the road down to the
    // answerphone runs through it; at the end of chapter 2 Matt was dropped down a slope, and the
    // list from the ledge above stood for a minute, the ledge's way on with it.
    //
    // A hold the game hands back takes up the list it left while she stands on the ground the last
    // map reached, however long the scene between took and however far it moved her on that ground:
    // the ground is what the list's verdicts stand on. Carried off it, through a door, down a drop,
    // the hold begins afresh. A pause moves nobody (`paused`). Only a scene of half a minute or less
    // was taken up until 2026-11-06, and on Mike's trail the heart carving's talk (59 s) and the bush
    // throw handed him back where he had stood with every thing behind him gone quiet for the new
    // map to prove again, up to 25 s: "objects appearing and disappearing".
    bool ResumesList(bool paused, bool onMappedGround);

    // Whether a reach map that is done proves a thing it has not reached closed off: it opened
    // every cell within its bound (`complete`, not cut by the bound) and enough of them to judge by
    // (`minCells`). What the game has yet to stream in is judged so too, since no map can tell how
    // the ground there runs: the trigger that streams it in is the way on instead (StreamsMore).
    bool MapProves(bool complete, bool radiusReached, int expanded, int minCells);

    // Whether a trigger that streams in a section of the act (UBatesStreamingLoadTriggerAction's
    // LoadOnEntry) is a way on: the game holds the section not loaded (`sectionLoaded`, from
    // IsLevelLoaded; unknown is no), she does not stand in it already, walking in being what loads
    // it, and the world has not shown the section since the act's levels were first counted
    // (`seenBefore`): the trigger leads into the act's ground still to come. Until then what lies in
    // the section has no floor to any probe. In act4_1's kitchen on 2026-11-07 the porch's stairs,
    // the yard and the path to the hanging shed were Section_02's, loaded only past T_Section2_Load in
    // the airlock's corridor: the map judged every way on beyond closed off and listed none. Listing
    // whatever stood over the unloaded ground instead listed the asylum's morgue, hospital, admin area
    // and Jack's lair from its grounds in act4_2 that evening, fourteen things behind the cellar door
    // and the building's walls, which went at once when Section_03 came in on the way to the totem.
    // A section the world has shown is one she has been through: the asylum's hall is a hub the game
    // streams its wings in and out of as she walks (sections 3 to 7 in act4_2), and with every
    // trigger back into a wing a way on, six of them came and went in the hall as Mike walked about
    // after the keycard, none of them the story's way (the chapel door, listed among them).
    bool StreamsMore(std::optional<bool> sectionLoaded, bool inside, bool seenBefore);

    // The levels the world has shown, by name, as each count of its levels finds them (Exploration's
    // PollWorld), and the sections the streaming manager has said are loaded: what she has been
    // through. A new world (every level new: another act, a checkpoint loaded) forgets the old.
    class LevelsSeen
    {
    public:
        void Count(const std::vector<std::wstring>& levels, bool newWorld);
        void Loaded(const std::wstring& section);
        bool Seen(const std::wstring& section) const;

    private:
        std::vector<std::wstring> m_seen;
    };

    // Whether the volume that puts her on a beam (ABatesSpecialLocoActivityActor's ActivationVolume)
    // is a way on: it is on (`volumeOn`, its collision), the beam does not hold her now, and it has
    // not carried her over already, the beam leading back from the far side. The grid cannot walk a
    // beam: on the asylum's walkway in act4_2 on 2026-11-07 the map stopped at the middle of
    // BP_BeamWalk_C_1's volume, the ledge behind it all it reached, the list stood empty and Mike
    // was stuck on the ledge above the cellar.
    bool BeamWay(bool volumeOn, bool onBeam, bool crossed);
    // Whether the beam carried her over: it let her go `kBeamCrossed` or more from where it took her
    // (turned back on it, she leaves it about where she came on).
    constexpr double kBeamCrossed = 150.0;
    bool BeamCrossed(const Vec& tookAt, const Vec& letGo);

    // What a target keeps of a verdict the reach map gave it once that map is dropped (the ground is
    // felt out anew after an interaction): 1 reached or 3 beyond a door stay until a new map judges
    // it, while she stands on the ground that map reached (`sameGround`); 2 closed off goes at once,
    // since what closed the way may be gone; and a hold begun afresh off that ground keeps none. Mike
    // dropped into the asylum's cellar in act4_2 on 2026-11-07 and the list said "totem danger, 17 m
    // behind, above" at once, reached by the map from the grounds outside, then left it out a second
    // later when the cellar's own map was done.
    int KeptReach(int reach, bool sameGround);

    // Whether a press of next or previous on an empty list says the list is still to come rather than
    // that nothing is near: a reach map runs (`mapRunning`) and things wait for it (`waiting`). At the
    // asylum's grounds on 2026-11-07 the first map took 14 s to reach the nearest thing, 50 m on foot,
    // and six presses meanwhile said there was nothing to walk to.
    bool StillComing(bool mapRunning, size_t waiting);

    // Whether the reach map takes the larger share of a frame: while the first list of a hold settles
    // (not `settled`, and no earlier list `keeping` it), and on for as long as nothing is listed. The
    // grounds' first map got the smaller share once the list's while ran out with nothing judged, and
    // took 16 s to reach what lay 50 m off.
    // And on while things in range still wait for it (`waiting`, ways::StillComing's count): the
    // first list after the hall's checkpoint was loaded settled with 2 of its 34 things judged, the
    // map went on at the smaller share, and the chapel door 39 m off came 29 s later and the map was
    // done after 43 s (2026-11-07, research/sessions/s54-full.log).
    bool MapHurries(bool settled, bool keeping, bool anyListed, size_t waiting);

    // Whether a walk at the end of its way says the game offers a neighbour instead of its target:
    // the game's offer follows her facing as she steps up, so it must stand kOtherOfferHold before it
    // is said. At the map in act4_3's station on 2026-11-07 the graffiti beside it was the game's
    // offer for half a second as Matt stepped up, and the map once he faced it; "map: near, but the
    // game offers graffiti" was said all the same, and again at the map with Emily
    // (research/sessions/s54-full.log).
    constexpr double kOtherOfferHold = 1.5;
    class OfferHold
    {
    public:
        // A look: whether the game offers a neighbour now; true once it has for kOtherOfferHold.
        bool Update(bool otherOffered, double now);
        void Reset()
        {
            m_since = -1.0;
        }

    private:
        double m_since = -1.0;
    };

    // Whether the hand-over's keys and choice wait for a character's turn to be named (`newTurn`: she
    // is another character than at the last hand-over, or the first since a level loaded): the game
    // hands her over a moment before the card that names her (name, place, time) comes up, and her
    // objective is said after the card. The keys and the choice come after both: while the card is up
    // (`cardUp`), for kCardWait from the hand-over while no card has come (`cardSeen`), and while the
    // objective is due once it has gone; never longer than kTurnWait. At the cable car's upper station
    // on 2026-11-07 they were said 2.1 s before "МЭТТ" and 9.3 s before his objective
    // (research/sessions/s54-full.log); Andrey: the character and the objective first, then the rest.
    constexpr double kCardWait = 4.0;
    constexpr double kTurnWait = 15.0;
    bool HintWaitsForTurn(bool newTurn, bool cardUp, bool cardSeen, bool objectiveDue, double sinceHandOver);

    // Which target the hand-over names as the choice once the list has come, by index (-1 for none):
    // the one chosen while the list shows it (`shown`, in the list's order); one the mod chose that
    // the list shows no longer gives way to the first it shows; and with nothing shown nothing is
    // said. In the asylum's hall on 2026-11-07 the hand-over chose the barrel Mike had just climbed
    // up from, listed for one look while the game still counted him in its volume, and a second later
    // named it, 37 m below and left out of the list.
    int HandOverChoice(int chosen, bool autoChosen, const std::vector<bool>& shown);

    // Whether a use location stands where no level placed it: its root at the world's very origin.
    // An activity's own use locations are left there (act4_1's death-machine lever and "joshisdead",
    // owned by activities at the origin and never moved, the lever used through the scene's own
    // binding): there is no place to walk to, and the list said them 95 m off toward the origin.
    bool Unplaced(const Vec& root);

    // Whether a walk goes on with the target chosen now: only with the one it was begun for, or one
    // the player has chosen since (`walkFor` follows the player's N and P), never with one the mod
    // chose after the walk's own left the list: in act4_1 on 2026-11-07 the scarecrow's look-at was
    // put off under Chris's feet, the mod chose the kitchen's door 47 m behind, and the walk turned
    // back to it.
    bool WalkGoesOn(const void* walkFor, const void* chosen);

    // Whether a walk ends because a map begun since it set off proves its thing closed off: the thing
    // the player chose stays listed though a map finds it closed off, so that a doubtful map is not the
    // last word, but a verdict that comes during the walk is a new fact of the world. In act4_3 on
    // 2026-11-11 ten levels streamed in as Matt walked to the danger totem, a new map found it and five
    // other things closed off behind a spline wall, and the walk pushed into that wall for twenty seconds
    // and said it could not get there; the list dropped them all a moment after (s71, 23:28:44 to 23:29:24).
    // `isWay`: a way on, which the rooms do not judge.
    bool WalkLosesTarget(bool isWay, bool closedOffNow, bool closedOffAtStart);

    // Whether a way on waits for her to walk into it anew: its story fires on entering (`firesOnEntry`:
    // its trigger's actions or its script's Enter event), no scene asks whether she stands in it
    // (`polled`), and she stands in it already, so that it has not fired for her. The game left Matt
    // in the snowman's trigger after Emily's prank, and it fired only once he walked out and in again
    // (s71, 20:31:03 to 20:32:24).
    bool WaitsForEntry(bool firesOnEntry, bool polled, bool standingIn);

    // What a walk to a way on goes on to when the way leaves the list for a thing beside it that
    // stands for it now (LeftForThing: `twin`, the way's twin, null for none): that thing, the way's
    // place being its place; else nothing, and the walk ends saying its target is gone. On the stairs
    // down to the old kitchen on 2026-11-08 the walk to the streaming trigger ended "Target gone" as
    // the game turned on the psycho revelation 4 m ahead, which took the trigger's place, and the mod
    // chose the wine cellar's door 24 m back (research/sessions/s58-whole.log, 11:38:55).
    const void* WalkGoesTo(const void* walkFor, const void* chosen, const void* twin);

    // Why a thing is left out of the list now, as a walk going on to it weighs it.
    struct LeftOut
    {
        bool hidden = false;      // not listed
        bool behind = false;      // behind her on the path she is held to
        bool offPath = false;     // beside a path the game holds her to
        bool unreachable = false; // no walk gets there for now
        bool superseded = false;  // a streaming trigger, with a way of the story listed
        bool onRoad = false;      // a way on the road of another listed one
        bool apart = false;       // beyond a door (rooms::Verdict::Apart)
    };

    // Whether the thing that stands for a way on now (LeftForThing) takes the way's place for a walk
    // to the way (WalkGoesTo): listed, or left out only while its first verdict waits (the game has
    // just turned it on: unjudged, or its room's verdict waiting for the map, which the way's own
    // ground has had); never one left out for a reason of its own. On the stairs down to the old
    // kitchen on 2026-11-08 the psycho revelation's room verdict waited for the reach map in the
    // same look as it took the streaming trigger's place (research/sessions/s58-whole.log,
    // 11:38:55.212), and was said in the area 0.3 s later.
    bool TakesWayPlace(const LeftOut& thing);

    // Whether a hold resumed on the ground the last map reached begins a new map: the scene moved her
    // more than kMovedFromMap from where that map began, and its roads (from there) no longer weigh
    // the ways the scene turned on. Short of that the roads hold.
    constexpr double kMovedFromMap = 150.0;
    bool MovedFromMap(double fromOrigin);

    // The other character standing in a walk's way, if any: the nearest of `others` within
    // kInTheWayReach of her and within kInTheWayAngle of the way to `toward`, as the game's own steering
    // stops her short of a companion rather than walking her into him. In the lodge's cinema airlock on
    // 2026-11-08 Chris stood in the middle of the 2.4 m airlock, 91 cm from Ashley, as she turned back
    // west toward the bedroom door the scene waited at, and she stood there pushing, at no speed,
    // whichever way the walk leant (research/sessions/s57-full.log, 02:04:56). Returns the index, or -1.
    constexpr double kInTheWayReach = 150.0;
    constexpr double kInTheWayAngle = 60.0;
    int InTheWay(const Vec& here, const Vec& toward, const std::vector<Vec>& others);

    // The ground about another character standing in a walk's way, kept clear by a way round him: a
    // place within `clearance` of where he stands (her radius, his and a margin) is no place for her
    // middle, nor a straight run that passes him nearer. The ground's sweeps leave the characters out,
    // for they walk on; one who stays put, as Chris did in the cellar for five minutes at the mouth of
    // the corridor to the creaky door on 2026-11-08, stops her by the game's own steering however the
    // walk leans, while the room let a way round him on his far side
    // (research/sessions/s58-whole.log, 11:13:49 to 11:18:27; replay fixture ground-cellar-chris.tsv).
    struct KeepClear
    {
        std::vector<Vec> standing; // where they stand, on the flat
        double clearance = 0.0;
        bool Clear(const Vec& at) const;
        // A step to `to` from `from`: clear, or no nearer any of them than she was (she may stand
        // within the clearance already, pressed against him, and walks out of it).
        bool StepClear(const Vec& from, const Vec& to) const;
        bool RunClear(const Vec& a, const Vec& b) const;
    };
    constexpr double kKeepClearMargin = 10.0; // cm past touching

    // Whether a walk whose target left the list ended in arriving: a way on's volume she stands in
    // (`wayVolume` and `inside`), which the scene it set going took off the list in the same look,
    // is reached and said so; a use location, or a volume she is not in, has gone. In the lodge's
    // cinema on 2026-11-08 the walk into the door slam's volume ended in silence (s57).
    bool WalkEndsArrived(bool wayVolume, bool inside);
    // A target the list stood on as out of reach when its verdicts were frozen (`reachWhenFrozen`:
    // 2 closed off, 3 beyond a door, as Exploration's reach map judges; 0 not judged, 1 reached) that
    // a later map reaches (`reachNow` 1): the ground has grown, a door has opened beside her, and
    // every verdict is given anew (Lister::NewHold).
    bool Reopened(int reachWhenFrozen, int reachNow);

    // ---- news --------------------------------------------------------------------------------

    // A target joining the list is news only where the game has just turned it on: a thing to use
    // it enables, a volume a scene begins to wait on. One that only comes within the list's range as
    // she walks was there all along: on Matt's trail on 2026-11-05 "New: Drop Down Emily, 120 m" and
    // two ways on were said as news as he walked down toward them, though the game had had them on
    // since the level began, and the player took them for things a trigger had just enabled.
    // `turnedOnAt`: when the game turned it on, if it was seen off before; news within kNewsWithin of
    // that (its ground may take a while to be known). A thing the list has shown before (`listedBefore`)
    // is never news again: the game puts some off and on as she walks (act4_1's scarecrow look-at,
    // off inside T_scarecrow_lookat_disable and on again outside it), and it was said "New:" each time
    // it came back (2026-11-07).
    constexpr double kNewsWithin = 30.0;
    bool IsNews(std::optional<double> turnedOnAt, double now, bool listedBefore);

    // The distance said of a target, in cm: on foot, as far as it is known. Its road's length once the
    // way to it is felt out (`road`); before that, what the reach map tells of it (`map`: its cost to
    // the target, or, while it runs and has not got there, how far it has got, which the target lies
    // beyond); the straight line only where neither is known, and never less than it. At the start of
    // a hold in act7_2 on 2026-11-09 the bolt door was said 25 m off, straight up the stairwell, and
    // 11 s later 118 m, the way on foot, once the map got there (research/sessions/s63-play.log,
    // 00:30:54.832 and 00:31:06.122): the map had got 117 m by the first.
    double SpokenDistance(double straight, std::optional<double> road, std::optional<double> map);

    // What the reach map tells of the way on foot to a target from her, in cells, at least (the map's
    // costs from its start: hers, `herCost`, none when she stands off its ground, the target's at
    // least as far less hers): its cost to the target where it judged it reached (`judged`,
    // `reached`, `targetCost`); where it has not judged it, while it runs or once it ended short of
    // every cell within reach (`running`, `cutShort`: its budget or its bound), how far it got
    // (`frontier`); nothing of a target it found out of reach, nor with her off its ground.
    std::optional<double> MapMeasure(bool judged, bool reached, double targetCost, bool running, bool cutShort, double frontier,
                                     std::optional<double> herCost);

    // ---- a path the game holds her to ---------------------------------------------------------

    // How far off a path the game holds her to she can stray: the game takes the stick only within
    // `coneDegrees` of the path's forward, so over the `ahead` cm she has left to walk she comes at
    // most ahead·tan(cone) from its line (the basement path of the lodge took it within 0 degrees;
    // others within 10). A thing whose place lies farther from the path than that and her own width
    // is seen from the path and never reached on it: the lodge's dollhouse and picture beside the
    // basement path were listed, and each walk to one stopped level with it, "could not get there"
    // (2026-11-05). No more than kPathStrayMax.
    constexpr double kPathStrayMax = 300.0;
    double PathStray(double ahead, double coneDegrees);

    // ---- a way on beside a thing --------------------------------------------------------------

    // A way on beside a thing to use (within kTwinAcross over the ground and kTwinUp in height of
    // the thing's spot) is the thing's own way: the player who goes to the thing sets the volume
    // off, and the way is left out. Not when the player cannot go to the thing (`thingGoneTo`
    // false: the path the game holds her to passes it by, it lies behind her on the path, the
    // map found it closed off): then the way is the road there and it stays. In the cabin's
    // bedroom on 2026-11-06 the phone lay inside the volume at the path's end (the phone's camera,
    // which turns Mike to it and holds him there for the press); the phone was left out as beside
    // the path, the volume as the phone's twin, and the list stood empty.
    constexpr double kTwinAcross = 250.0;
    constexpr double kTwinUp = 200.0;
    bool LeftForThing(const Vec& thing, const Vec& way, bool thingGoneTo);

    // ---- following a road -------------------------------------------------------------------

    // The stretch of a road the character is on, by the corner that ends it (0 for the road's first
    // corner itself), or -1 when none lies within `reach` of her over the ground among the first
    // `count` corners. A road can pass over itself (a stair that turns back under its own landing):
    // a stretch at another height than hers, by more than `rise`, is not hers however near it lies
    // over the ground. Walking back from the danger totem on 2026-11-04, Mike came down the stairs
    // onto the walkway under the platform the road had begun on, and was turned back up 391 cm, the
    // platform's first stretch lying nearer over the ground than the walkway's, until the walk gave
    // up. The road's heights and hers are taken alike (the corners at the height of her middle).
    int StretchOn(const std::vector<Vec>& corners, size_t count, const Vec& here, double reach, double rise);

    // The corner the beacon and the spoken bearing lead her to while she walks by herself: the one that
    // ends the stretch she stands on (StretchOn), and past it the next once she is within kCornerPassed
    // of it or has gone beyond it along its stretch; with the length of the road left from her through
    // it. Asked afresh at every look: the walk's own corner is refreshed only now and then (a road is
    // kept 1.5 s while no walk runs), and a corner passed meanwhile was said to lie behind her ("6 m,
    // behind" seven seconds after "9 m, ahead left", walking to the pool by the beacon,
    // research/sessions/s66-play.log, 16:15:17 to 16:15:24); at a sharp turn walked past, the nearest
    // stretch is still the one that ends at the corner behind her. Nothing when she is on no stretch.
    constexpr double kCornerPassed = 60.0;
    struct Lead
    {
        size_t corner = 0;
        double length = 0.0;
    };
    std::optional<Lead> LeadCorner(const std::vector<Vec>& corners, size_t count, const Vec& here, double reach, double rise);
    constexpr double kStretchRise = 150.0;

    // The way left from where she stands along a road the walk follows, as the walk heads for its
    // corners (the one ending the stretch she is on, the next once she is within 60 cm of it), and
    // whether what is left is one straight leg; none when she is on no stretch of it.
    std::optional<double> LeftAlong(const std::vector<Vec>& corners, const Vec& here, bool& lastLeg);

    // ---- the game's passages ----------------------------------------------------------------

    // A guidance passage of the game's (ABatesGuidancePassageVolume: the doorways' BP_Doorway): a
    // line through a doorway from one entry point to the other, along which the game takes the
    // character when she walks into it. The doorway is often too tight for the ground's cells, whose
    // steps are swept from their middles: the generator shed's doorway in Act2_2 lets a capsule of
    // 42 cm through only near its line, and the reach map found the way in when its cells happened to
    // lie on it (a grid laid in the lodge, 2026-11-04 01:35) and never under a grid laid nearer by
    // (01:40 on, and every map of the next session): the generator, the very thing the objective
    // named, was left out as closed off 248 cm short. A passage the character's capsule gets along
    // (PassageWalk), with no door's leaf across the way it took, links the ground at its two ends.
    struct Passage
    {
        std::string name;
        Vec a, b;          // the entry points, on the floor
        bool free = false; // walked from one to the other
    };

    // A sweep of the character's capsule from one point to another, as the engine answers it: whether
    // it met something, how far it got before it did, the normal of the contact pointing back at the
    // capsule, and whether it began already pressed into something.
    struct SweepHit
    {
        bool hit = false;
        double distance = 0.0;
        Vec normal;
        bool penetrating = false;
        double depth = 0.0; // how far in, when it began pressed in
    };
    using Sweep = std::function<SweepHit(const Vec& from, const Vec& to)>;

    // How the capsule is walked through a passage, as the character's movement takes her through a
    // doorway: straight on until she touches something, then along the surface she touched (the rest
    // of the move with its part into the surface taken away), and on for the far end again, at most
    // kPassageTouches times; she has got there within kPassageArrive of it. A straight sweep along the
    // game's line does not do: at the generator shed on 2026-11-05 it stopped 41 cm in, at an invisible
    // wall the line grazes (BP_SplineCollisionActor_C_3), while the cells a grid laid from the totem
    // that day went through 2 to 12 cm to one side of the line. So the line is walked, and lines beside
    // it, kPassageShift apart up to kPassageShiftMax either way, nearest first.
    constexpr int kPassageTouches = 4;
    constexpr double kPassageArrive = 20.0;
    constexpr double kPassageShift = 3.0;
    constexpr double kPassageShiftMax = 18.0;
    struct PassageWay
    {
        bool passed = false;
        double shift = 0.0; // the line walked, to the right of the game's line looking from a to b
        int touches = 0;    // the surfaces she slid along on it
        Vec reached;        // where the walk on the game's own line got to, when nothing passed
    };
    // Walks the capsule from `a` to `b` (its middle, at the sweep's height) as above.
    PassageWay PassageWalk(const Sweep& sweep, const Vec& a, const Vec& b);
    // One walk along a line: where it got, and how many touches it took.
    Vec SlideWalk(const Sweep& sweep, const Vec& from, const Vec& to, int& touches);

    // A step of the ground swept from one cell's middle toward the next's: a sweep begun with her
    // capsule pressed into something is pushed out once along the contact, by the depth the engine
    // gives, and swept again, as her own movement pulls her out of a touch before it moves her.
    struct StepSweep
    {
        double clear = 0.0; // how far the capsule got
        Vec reached;        // where its middle got to, after any pull-back
        bool hit = false;   // it met something short of the end
        bool stuck = false; // begun pressed in, and still pressed in after the push, or met something at once
    };
    constexpr double kStuckClear = 0.5; // cm: a sweep after the push that gets no farther met something at once
    StepSweep SweepOut(const Sweep& sweep, const Vec& from, const Vec& to);

    // A step stuck so is swept again from a spot of its cell where her capsule stands free: the push
    // the engine gives does not always free her. In the mine of act8_5 on 2026-11-09 the way to the
    // scene's trigger (T_scream) ran up a corridor of the level's hard collision (a spline's wall,
    // BP_SplineCollisionActor_C_0) 96 cm wide for her capsule of 84: no cell's middle there let it
    // stand free, the engine's push out of the wall freed it in no direction at all (every step out of
    // such a cell refused 0 cm in), and the reach map stopped 6 m short of the trigger, which was left
    // out as closed off while the game walked her up the corridor (research/sessions/s63-play.log,
    // 01:30:08; dumps/ground-20261109-001825.tsv). The spots are tried nearest the middle first and
    // across the step's way first, at each of kFreeSpotRings of `reach` from the middle, eight ways
    // round; the first where a sweep of a centimetre toward the next cell meets nothing is where she
    // stands, and the step is walked from there as her movement walks her, along what she touches
    // (SlideWalk): free when she gets within `reach` of the next cell's middle, as every step must.
    // None when no spot stands free or the one that does gets no nearer.
    constexpr double kFreeSpotRings[] = {0.5, 1.0};
    std::optional<Vec> SweepFromFreeSpot(const Sweep& sweep, const Vec& from, const Vec& to, double reach);
    // How near an entry point a cell's middle must lie, over the ground and in height, to take the
    // passage: within a cell and a half, on its floor or a step off it.
    constexpr double kPassageReach = 30.0;
    constexpr double kPassageRise = 60.0;
    // The links the passages give a cell whose middle on its floor is `middle`: to the cell of the
    // other end of each passage it lies at, by `cellAt` (false when that end's floor is not known),
    // the cost being the distance between the two middles in cells of `cellSize`.
    void PassageLinks(const std::vector<Passage>& passages, const Vec& middle, double cellSize,
                      const std::function<bool(const Vec& end, grid::Cell& cell, Vec& cellMiddle)>& cellAt, std::vector<grid::Link>& out);
}
