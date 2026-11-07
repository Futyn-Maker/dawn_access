#pragma once
// The rooms of a level as the game itself lays them out, and what the exploration list keeps of a
// level's things by them, worked out without the game so that the tests can replay them. The
// game's sound designers laid a reverb volume over every room and path of the levels where the
// echo changes (each act's LV_reverb: T_Reverb_Kitchen_1, T_Reverb_Hall_Airlock,
// T_Reverb_PathToBeautySpot, and broad ones under them, T_GlobalReverb_Exterior,
// T_GlobalReverb_LodgeInterior): 278 volumes in 21 acts (research/triggers/audio). The list keeps
// to the area the character can walk about in without opening a door or climbing (Judge), as the
// reach map finds it; the rooms do not decide what is listed, so that the list stays as it is while
// she walks from one open room into the next and changes as she passes a door (Andrey's rule,
// 2026-11-04). The character crossing from one room into another is told the room's name
// (Crossing), from the table of room names (lang/rooms), which names only the rooms of the
// chapters played so far.
//
// At the lodge on 2026-11-02 the list held 24 things, most of them inside the lodge while Chris
// stood in its yard with the front door frozen shut; the reach map had proven every one of them
// closed off and the list kept them all.

#include "features/Shapes.hpp"
#include "features/Vec.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace da::rooms
{
    using features::world::Vec;

    // The key of a reverb volume's room in the table of room names: the volume's name without its
    // prefix, its words in lower case joined by underscores, a number or a lone letter at its end
    // dropped, so that the parts of one room are one room (T_Reverb_Kitchen_1 and
    // T_Reverb_Kitchen3 are "kitchen", T_Reverb_TowerToSanatoriumview_A is
    // "tower_to_sanatoriumview"); empty for a volume that is no reverb volume. `broad`, when given,
    // says whether it is one of the broad volumes laid under the rooms (T_GlobalReverb_*,
    // T_ReverbGlobal_*, T_Reverb_Global_*).
    std::wstring Key(std::wstring_view name, bool* broad = nullptr);

    // One reverb volume: its name, its room's key, the room's name in the player's language
    // (empty where the table names none), and its collision.
    struct Zone
    {
        std::wstring name;
        std::wstring key;
        std::wstring spoken;
        bool broad = false;
        shapes::Shape shape;
        Vec low;
        Vec high;
        double size = 0.0; // of the box around it: of two zones holding a point, the narrower is its room
    };
    Zone MakeZone(std::wstring name, shapes::Shape shape);

    // The zone a point lies in: the narrowest of those whose collision holds it, -1 for none.
    int ZoneAt(const std::vector<Zone>& zones, const Vec& point);
    // How far a point lies outside a zone's collision, zero within it.
    double Outside(const Zone& zone, const Vec& point);

    // The room the character is in, from look to look. She is in a room once her middle is within
    // its collision and no narrower room holds it; she has left it once her middle is kCrossMargin
    // beyond it, so that two rooms whose volumes meet at a doorway do not take turns while she
    // stands in it. Crossing into a room whose name differs from the last one's is said, in that
    // name alone; the first room a character is seen in is not (Reset; the level's rooms may come
    // a look after her), nor one the table does not name, and the parts of one room are crossed in
    // silence. A broad volume takes over from a room only once she is kBroadMargin beyond the room:
    // the broad volumes fill the gaps between rooms, and walking from the lodge's front vestibule
    // into its hall on 2026-11-04 said "vestibule, house, hall" within five seconds, the house
    // being the strip of the broad volume between the two rooms' volumes.
    constexpr double kCrossMargin = 30.0;
    constexpr double kBroadMargin = 100.0;

    class Crossing
    {
    public:
        // A new character: the first room she is seen in is taken without a word.
        void Reset();
        // Takes where she stands at a look; returns the name to say when she has crossed into a
        // room, else nothing. With no rooms known yet nothing is taken.
        std::wstring Update(const std::vector<Zone>& zones, const Vec& middle);
        // The room she is in (its volume's name) and its name in the player's language; empty for
        // none.
        const std::wstring& Room() const
        {
            return m_room;
        }
        const std::wstring& Spoken() const
        {
            return m_spoken;
        }

    private:
        bool m_started = false;
        std::wstring m_room;
        std::wstring m_spoken;
    };

    // The rooms the reach map's ground lies in, for the log: a room counts once kAreaCells of the
    // cells the map reached have their middles in it (the narrowest zone holding a middle). No rule
    // takes a thing for one of the area's rooms by it (Judge).
    constexpr size_t kAreaCells = 25; // a square metre of the grid's 20 cm cells

    class Area
    {
    public:
        // A map begins, over `zones` zones.
        void Reset(size_t zones);
        // A cell the map reached, in zone `zone` (-1 for none).
        void Add(int zone);
        bool Covers(int zone) const;
        size_t Cells(int zone) const;

    private:
        std::vector<size_t> m_cells;
    };

    // Where a thing to use stands for the list. In the area: listed. Apart: the reach map has
    // proven no walk gets there without opening a door or climbing (closed off, or not reached with
    // a door in the ground it reached); left out, as the other side of a door is another room's. Waits: not judged yet while the list settles, left out for the
    // moment so that a thing is not listed and said, then taken back a second later.
    enum class Verdict
    {
        None,
        In,
        Waits,
        Apart,
    };

    struct Facts
    {
        bool settled = false; // the list has settled on the map (ways::Settling)
        bool running = false; // the map is still running
        bool proves = false;  // the map ran out of cells within its bound: whatever it has not reached is closed off
        int reach = 0;        // the map's verdict: 0 none yet, 1 reached, 2 closed off, 3 not reached with a door in what it reached
        bool offered = false; // the game offers it to her now
        bool stuck = false;   // the map, done, opened too few cells to judge by: it began where she stands boxed in for a moment
    };

    // Judges a thing at a look, with its verdict at the last one. Reached or offered, it is in. No
    // room keeps a thing the map has not reached: a room's volume reaches through a doorway into the
    // next (at the lodge on 2026-11-04 the hall's reached 57 cells into the ground the map found from
    // the garage, which kept the hall's television listed behind a closed door), and a rule by the
    // room she stands in would change the list as she walked between open rooms. While the list
    // settles, a verdict an earlier map gave stands and a thing never judged waits; and so they do
    // while a map runs that has not got to the thing yet, the list settled or not: a thing shown when
    // the list's while ran out and taken back once the map proved it closed off is a list changing by
    // itself (the mine's police tape and carts, listed for three seconds when Mike's hold began on
    // 2026-11-04, the first map of the hold taking six). Once the map is done, the thing is apart only
    // by its proof: whatever it could not prove (cut short by its budget or its bound) is in, since only
    // a proven wall may take a thing off the list. A map that opened too few cells to judge by
    // (`stuck`) says nothing of the ground either way, and the verdicts before it stand: on the
    // asylum's walkway on 2026-11-07 Mike's capsule stood for a moment in a cell boxed in at the
    // ledge's edge, maps of one cell came between maps of 108, and with each the forty things of the
    // building came back on the list ("hospital writing on wall, 46 m") and went again.
    Verdict Judge(const Facts& facts, Verdict before);

    // Whether a way on is offered by the reach map's verdict: not one the ground was proven
    // closed off from (`enclosed`), nor one beyond a door (`reach` 3: the door is listed, and
    // what lies behind it is another room's), but one the path she is held to runs into.
    bool WayOffered(bool enclosed, int reach, bool pathReach);

    // The reach map's verdict a way on is offered by (WayOffered), as Judge gives a thing its room:
    // this map's once it has judged the way (`judgedNow`); while the map runs without having got to
    // the way yet, or is stuck, the verdict an earlier map gave it and the ground kept (`reach`,
    // ways::KeptReach); none once the map is done without one, since only a proven wall takes a way
    // off the list. Every map begun as she walked off the ground the last one reached offered every
    // way found beyond a door again until it got to it: in the asylum's hall on 2026-11-07 the ways
    // to the morgue, the church and the grounds were listed for one to fifteen seconds at each new
    // map, "Путь дальше" said six times over, and walks to them could not get there.
    int WayReach(bool judgedNow, bool running, bool stuck, int reach);

    // The point a thing's room is told by: the spot the character is put on to use it, raised to
    // the height of her middle, where the rooms' volumes reach whatever the floor's height.
    constexpr double kRoomPointRise = 50.0;
}
