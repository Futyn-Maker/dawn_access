#include "features/Exploration.hpp"

#include "features/Choices.hpp"
#include "features/DontMove.hpp"
#include "features/ExploreText.hpp"
#include "features/GridPath.hpp"
#include "features/GroundFile.hpp"
#include "features/Hold.hpp"
#include "features/Hud.hpp"
#include "features/LookTurn.hpp"
#include "features/Qte.hpp"
#include "features/Rooms.hpp"
#include "features/SceneScripts.hpp"
#include "features/Shapes.hpp"
#include "features/WayList.hpp"
#include "features/WorldRead.hpp"

#include "core/Config.hpp"
#include "core/GameThread.hpp"
#include "core/GameLua.hpp"
#include "core/Gate.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/ParamReader.hpp"
#include "core/Strings.hpp"
#include "core/Symbols.hpp"
#include "diag/Diagnostics.hpp"
#include "hooks/HookDispatcher.hpp"
#include "hotkeys/Hotkeys.hpp"
#include "input/InputNames.hpp"
#include "locale/Locale.hpp"
#include "speech/Sounds.hpp"
#include "speech/Speech.hpp"
#include "watch/GameState.hpp"
#include "watch/Watchers.hpp"

#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/FWeakObjectPtr.hpp>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <numbers>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace da::features
{
    using RC::Unreal::FFrame;
    using RC::Unreal::FProperty;
    using RC::Unreal::UObject;

    namespace
    {
        using namespace world;

        // Something the character can walk to: a use location the level has turned on (the
        // thing itself, named as the level names it), or a place the level is waiting for
        // the character to walk into, offered as a way on.
        struct Target
        {
            UObject* actor = nullptr;
            bool way = false;   // a trigger volume the level waits on, never named
            bool glint = false; // the game marks it with a glint
            std::wstring label;
            std::wstring coreName; // the name the level's scripts call it by
            Vec position;          // where it stands
            Vec approach;          // the spot the character is meant to reach it from
            bool hasApproach = false;
            double distance = 0.0; // straight line from the character, centimetres
            double bearing = 0.0;  // degrees to the right of the camera's forward
            bool reached = false;
            bool hidden = false; // kept but not listed: a way on whose turn has not come, a thing behind on a path
            bool behind = false; // lies behind the character on the path she is held to
            // A way on is a volume: the box its collision fills, whether the character stands
            // inside it, and whether it lies wholly above or below the character.
            bool hasBox = false;
            Vec boxLow;
            Vec boxHigh;
            // A way on's own box, turned as it stands (VolumeBox): the bounds above are square
            // with the world and reach far beyond a long volume set aslant.
            bool shaped = false;
            ways::Box shape;
            // A way on's collision itself, as the game tests her capsule against it
            // (VolumeShape): most trigger meshes carry their turn, or a shape that is no box, in
            // their own points, so neither box above says where the volume is set off.
            bool hasCollision = false;
            shapes::Shape collision;
            Vec collisionLow;
            Vec collisionHigh;
            bool inside = false;
            int storey = 0;
            // The walkable way there, from the navigation mesh where the level has one: how
            // far it is on foot and the corner to head for now.
            bool hasRoute = false;
            double routeAt = -1000.0;
            double routeLength = 0.0;
            Vec routeNext;
            Vec routeFrom;
            Vec routeGoal;
            // The way felt out over the ground with the character's own collision, where the
            // level has no navigation mesh: its corners, and when and whence they were found.
            std::vector<Vec> gridCorners;
            double gridAt = -1000.0;
            Vec gridFrom;
            Vec gridGoal;
            double gridFailedAt = -1000.0;
            // Where the search found no way all through: the way runs as far as the ground was
            // found to carry the character (the first `gridWalkable` corners), then straight on
            // to the target, as the navigation mesh's partial roads do.
            bool gridPartial = false;
            size_t gridWalkable = 0;
            // The search found every cell the character can reach and the target was not
            // among them (not merely out of its budget): no walk gets there for now.
            bool gridEnclosed = false;
            UObject* onRoadOf = nullptr; // a way left out: the listed target whose road runs through it
            bool unreachable = false;    // a way left out: no walk gets there for now
            bool pathReach = false;      // the path the game holds her to runs into this way
            // The reach map's judgement of the target (ReachMap): 0 none yet, 1 its place reached,
            // 2 closed off, 3 not reached but beyond a door the character can open; and which map
            // made it, so that a new map judges every target afresh.
            int reach = 0;
            uint64_t reachGeneration = 0;
            uint64_t reachTried = 0; // the done map that looked for it and could not judge it
            bool unjudged = false;   // a way left out until its ground is known
            // The reach map's cells of the way to it, kept so that its road can be pulled straight
            // once it is chosen, and whether that was asked.
            std::vector<grid::Cell> reachCells;
            bool straightened = false;
            // The map's road to it, pulled straight over the map (ways::StraightOverMap), and how
            // many of its corners the ground was found to carry her to: the road the row rule
            // weighs, the same whether or not the target is chosen.
            std::vector<Vec> mapRoad;
            size_t mapWalkable = 0;
            bool passed = false; // the row rule has judged it since it came (ways::Entry)
            int rowEpoch = -1;   // the hold its verdict was given in (ways::Lister)
            // How the map judged it when the list's verdicts were frozen (ways::Reopened): 2 closed
            // off, 3 beyond a door, 0 not judged then.
            int reachAtFreeze = 0;
            // Beside the path the game holds her to, never reached on it (ways::PathStray).
            bool offPath = false;
            std::wstring kind; // the class of the interaction behind a use location
            // A thing to use by the room rule (rooms::Judge): in the character's area, outside it,
            // or waiting for the reach map, kept from look to look.
            rooms::Verdict room = rooms::Verdict::None;
        };

        // What the navigation mesh answers for one question.
        struct Route
        {
            bool valid = false;
            bool partial = false;
            double length = 0.0;
            Vec next;
            Vec goal;
            Vec end;
        };

        // What the level's script does when the character walks into a trigger volume, read
        // from the script asset the game loads for it (scripts::ParseEnterEvent).
        using LuaEvent = scripts::EnterEvent;

        // The scenes' waits for a character to walk on, read with the level's scripts
        // (scripts::ParseMoveWaits), and the box the way on at the end of a held path is given.
        std::vector<scripts::MoveWait> g_moveWaits;
        constexpr double kPathEndHalf = 40.0;

        // Whether a scene's machine stands now in a state waiting for `character` to walk on, by
        // the machines' own record in the game's Lua state, read at most twice a second.
        bool ScenesWaitToMove(const std::wstring& character)
        {
            if (g_moveWaits.empty()) return false;
            static double s_readAt = -10.0;
            static std::vector<std::pair<std::wstring, int>> s_machines;
            const double now = gamethread::NowSeconds();
            if (now - s_readAt >= 0.5)
            {
                s_readAt = now;
                s_machines.clear();
                std::vector<lua::Machine> machines;
                if (lua::Machines(machines))
                {
                    for (auto& m : machines)
                        s_machines.emplace_back(std::move(m.name), m.state);
                }
            }
            return scripts::WaitsToMove(g_moveWaits, s_machines, character);
        }

        // A number of the scripts' gVariables as the game's Lua state holds it now (lua::Global),
        // read once a frame whatever the number of conditions asking, and logged when it changes.
        scripts::Number ScriptVariable(const std::wstring& name)
        {
            static unsigned long long s_frame = 0;
            static std::map<std::wstring, scripts::Number> s_read;
            static std::map<std::wstring, std::wstring> s_logged;
            if (gamethread::FrameCount() != s_frame)
            {
                s_frame = gamethread::FrameCount();
                s_read.clear();
            }
            if (const auto it = s_read.find(name); it != s_read.end()) return it->second;
            const auto value = lua::Global("gVariables", str::WideToUtf8(name));
            scripts::Number number;
            if (value.kind == lua::Value::Kind::Nil)
                number.kind = scripts::Number::Kind::Nil;
            else if (value.kind == lua::Value::Kind::Number)
                number = scripts::Number{scripts::Number::Kind::Value, value.number};
            s_read[name] = number;
            const std::wstring said = number.kind == scripts::Number::Kind::Value ? std::format(L"{}", number.value)
                                      : number.kind == scripts::Number::Kind::Nil ? L"nil"
                                                                                   : L"not read";
            if (s_logged[name] != said)
            {
                s_logged[name] = said;
                log::Info(L"explore: the scripts' gVariables.{} is {}", name, said);
            }
            return number;
        }

        // The use location's own flags, as last read, for a log line when they change.
        struct UseState
        {
            bool enabled = false;
            bool activity = false;
            bool sleeping = false;
            bool inProgress = false;
            bool listed = false;
            bool known = false;
        };

        constexpr double kScanApart = 2.0;   // between two scans of the level's actors
        constexpr double kWaysApart = 3.0;   // between two readings of the ways on
        constexpr double kRoadStep = 25.0;   // centimetres between two samples of a road
        constexpr double kTwinApart = 150.0; // two things with one name within this are one

        std::vector<Target> g_targets; // in a stable order: the earlier a target came, the earlier it stays
        UObject* g_selected = nullptr;
        UObject* g_pawn = nullptr;
        double g_leftAt = -1000.0;
        Vec g_leftAtPosition;
        bool g_leftForPause = false; // exploration stopped for the pause menu, not for the scene
        bool g_exploring = false;
        bool g_roaming = false;
        std::wstring g_notRoamingWhy;
        // Who has the character, read at every frame.
        UObject* g_controlPawn = nullptr;
        bool g_control = false;
        bool g_controlHeard = false;
        int64_t g_binding = -2;     // the character's binding state as last logged
        int64_t g_cameraLayer = -2; // the camera layer as last logged
        int64_t g_activeLayer = -2; // the camera manager's active layer, as logged
        bool g_cameraFree = false;  // the game's own test: an exploration layer is active
        bool g_falling = false;     // the character is in the air, as last logged
        // The character the scene last made the player's, and the wait its script makes before it
        // goes on with her (SwitchWait), counted on the play clock from the switch.
        UObject* g_switchPawn = nullptr;
        double g_switchWait = 0.0;
        double g_switchedAt = 0.0;
        bool g_inputTaken = false; // the controller takes movement input
        std::wstring g_stateName;  // the game state as last logged
        bool g_navLogged = false;
        double g_hintDueAt = -1.0;
        double g_hintSaidAt = -1000.0;
        bool g_beacon = true;
        bool g_lookSound = true; // the look beacon, apart from the beacon
        double g_beaconAt = -10.0;
        // The level's actors, scanned now and then.
        double g_scannedAt = -10.0;
        std::vector<UObject*> g_useLocations;
        std::map<UObject*, UObject*> g_glintOf; // use location -> its glint
        std::vector<UObject*> g_volumes;
        std::vector<UObject*> g_stairs; // the level's staircases
        // The level's rooms (rooms::Zone): its reverb volumes with their collision, read as its
        // volumes are scanned, counted anew whenever they change; the room each thing lies in, found
        // once for each count; and the room the character is in.
        std::vector<UObject*> g_zoneVolumes;
        std::vector<rooms::Zone> g_zones;
        uint64_t g_zonesGeneration = 0;
        std::map<UObject*, std::pair<uint64_t, int>> g_zoneOf;
        rooms::Crossing g_crossing;
        // The other characters, left out of the ground's sweeps (RunTrace), and the frame they were
        // last checked for being alive in.
        std::vector<UObject*> g_others;
        uint64_t g_othersCheckedAt = 0;
        std::map<UObject*, UseState> g_useStates;
        // The ways on, read now and then.
        double g_waysScannedAt = -10.0;
        std::vector<Target> g_ways;
        std::map<UObject*, std::wstring> g_wayVerdict;
        std::map<UObject*, std::wstring> g_wayNote; // why a way on is left out besides its verdict (a twin, beside a thing), as last logged
        std::vector<UObject*> g_waysAnnounced;
        scripts::SetOff g_setOff;          // the trigger scripts she has set off, while the level's scripts run
        obj::ObjectKey g_levelScriptGroup; // the level's script group they run in
        // The level's trigger scripts, read once per script asset.
        std::map<UObject*, LuaEvent> g_luaByAsset;
        std::map<std::wstring, LuaEvent> g_luaEvents; // by trigger name
        double g_luaScannedAt = -10.0;
        // The scenes' state machines' questions whether a character stands in a volume, read once
        // per script asset, and the game asking them as its scenes run (scripts::Waits).
        std::map<UObject*, std::vector<scripts::Poll>> g_pollsByAsset;
        std::map<UObject*, std::vector<scripts::MoveWait>> g_moveWaitsByAsset;
        scripts::Waits g_waits;
        // The scenes' switches of the player's character and the waits that follow them
        // (scripts::Switch), read from the same scripts with the numbers they keep in gVariables.
        std::map<UObject*, std::wstring> g_scriptByAsset;
        std::vector<scripts::Switch> g_switches;
        // A clock that runs only while the game is in play, when its scripts run: the questions
        // asked just before a pause still stand when play resumes.
        double g_playClock = 0.0;
        double g_playClockAt = -1.0;
        bool g_pollHeard = false;
        // Targets the level took out of the list a moment ago, and whether the player had
        // chosen one: back soon, they are not news and the chosen one is chosen again.
        struct Gone
        {
            double at = 0.0;
            bool selected = false;
        };
        std::map<UObject*, Gone> g_gone;
        // Walking to the target by itself.
        bool g_walking = false;
        double g_walkStartedAt = 0.0;
        double g_walkMovingAt = 0.0;
        double g_walkLean = 0.0;
        double g_walkBestWay = 1e9;
        double g_walkBestWayAt = 0.0;
        std::tuple<int, double> g_walkMeasure{0, 0.0}; // how the way still to go is measured, and the way felt out it is measured on
        double g_walkLeanedAt = 0.0;
        double g_walkLoggedAt = 0.0;
        Vec g_walkLastPosition;
        // Feeling the way: the heading last chosen, when, which side the character is going
        // round an obstacle on, how far the way ahead was clear, and the words for the log.
        double g_cameraYaw = 0.0;
        UObject* g_offered = nullptr; // the use location the game offers right now
        int g_playerCharacter = -1;   // EBatesCharacter of the player's character
        bool g_walkWaitLogged = false;

        // The grid the ground is felt out on where the level has no navigation mesh: cells
        // of 40 cm laid from a fixed origin at the character's height, each step between two
        // neighbours answered once by a sweep of the character's own collision and
        // remembered until the game hands the character over again (a door may have opened).
        constexpr double kCellSize = 20.0;             // fine enough for a doorway to hold a cell's middle whatever the lattice's phase
        constexpr int kGridExpansions = 60000;         // the most cells one search may open
        constexpr int kEnclosedMinCells = 50;          // fewer opened than this is no proof of a closed place (2 m²)
        constexpr double kStepReach = kCellSize / 2.0; // how near a step's sweep must come to the cell's middle
        constexpr double kGridBudgetMs = 2.0;          // of each frame
        constexpr double kGridWalkBudgetMs = 5.0;      // of each frame while the walk waits for the way
        constexpr size_t kCornerRun = 60;              // cells a straight run between corners may span (12 m)
        constexpr double kDoorSidesApart = 200.0;      // two use locations of one kind this close are one door

        constexpr double kLevelSize = 50.0; // cm of height one level of the grid spans
        constexpr int kGoalLevels = 4;      // levels a thing may stand above or below the ground it is reached from
        constexpr int kNoFloor = INT_MIN;

        // A place on the grid: a cell's column, and a level on it.
        using Place = std::tuple<int, int, int>;

        struct GridState
        {
            bool anchored = false;
            Vec origin;
            double floorZ = 0.0; // the capsule's middle where the grid was laid
            double radius = 34.0;
            double capsuleHalf = 88.0;      // of the character's own capsule
            double stepHeight = 45.0;       // the most the character steps up, as its own movement has it
            double walkableZ = 0.71;        // the least a floor's normal may lean from upright for the character to stand on it
            std::map<Place, double> floors; // the floor of a cell on a level
            std::map<Place, int> probes;    // what a probe from a level found at a column: the level of the floor there, or none
            // The level a step from a cell into a neighbouring column lands on, or none, and
            // why a step is none (A/B no floor at a/b, H too high, W swept into something).
            std::map<std::tuple<int, int, int, int, int>, int> steps;
            std::map<std::tuple<int, int, int, int, int>, wchar_t> why;
            // When each step was swept into something: what stood in the way may have gone (a
            // door opened, a character moved, the scene moved on), so such an answer is asked
            // again once it has aged, or once the story has moved on since it was given.
            std::map<std::tuple<int, int, int, int, int>, double> refusedAt;
            double storyMovedAt = -1000.0;
            double refusalsForgottenAt = -1000.0;
            bool partial = false; // the cells being pulled straight end short of the goal
            // The cell the character stands in, and where in it she stands: it is swept from
            // there, since its middle may lie inside the wall she stands against.
            bool startKnown = false;
            grid::Cell startCell;
            Vec startMiddle;
            std::unique_ptr<grid::Search> search;
            std::unique_ptr<grid::Straightener> straightener; // pulls the found cells straight, after the search
            int searchExpanded = 0;
            UObject* searchFor = nullptr;
            Vec searchAim;
            double searchStartedAt = 0.0;
            int searchSweeps = 0;
            uint64_t straightenFor = 0; // the reach map whose road the straightener pulls, 0 for a search's
            int sweeps = 0;
            double sweepMs = 0.0;
            bool unavailableLogged = false;
        };
        GridState g_grid;

        // The reach map: every cell the character can walk to from where she stood when it was
        // begun, nearest first (grid::Flood over the steps the searches take). It judges every
        // target at once and in the same way, which separate searches did not: at the woods'
        // start on 2026-10-24 each press of N or P began the chosen way's search afresh, the far
        // ones never finished in a minute of browsing, and all eleven ways stayed listed. A target
        // whose place the map reaches is reachable, with its road and its distance on foot; one
        // it has not reached once it has run out of cells is closed off, unless a door the
        // character can open stands in the reached ground, behind which anything may lie. A
        // map cut short by its budget or its bound judges only what it reached. It is begun
        // at every hand-over, when the character has left the ground it reached or walked well
        // away from where it began, and when the story has moved on; a new map judges every
        // target afresh, and until it has, a target keeps what the last map found reachable
        // (never what it found closed off, which is what kept the woods' ways hidden after the
        // duck-under on 2026-10-24).
        struct ReachMap
        {
            std::unique_ptr<grid::Flood> flood;
            Vec origin; // the capsule's middle where it was begun
            double startedAt = -1000.0;
            double doneAt = -1.0;    // when it ran out of cells, or of budget
            uint64_t generation = 0; // counts up with every new map
            int sweepsAtStart = 0;
            // The rooms its ground lies in (rooms::Area), counted cell by cell as it reaches them,
            // under one count of the level's rooms.
            rooms::Area area;
            size_t areaCounted = 0;
            uint64_t areaZones = 0;
        };
        ReachMap g_reach;
        // The last map dropped at the end of an interaction (ForgetGround), kept only to tell
        // whether the hold the game hands back finds her on the ground it reached
        // (ways::ResumesList); gone when the grid is laid afresh, its cells then meaning nothing.
        std::unique_ptr<grid::Flood> g_lastFlood;
        constexpr int kFloodExpansions = 120000;  // the most cells a map may open
        constexpr double kFloodBudgetMs = 3.0;    // of each frame while a map runs
        constexpr double kFloodFirstMs = 10.0;    // of each frame while the first list of a hold settles
        constexpr double kRemapDistance = 1500.0; // cm walked from a done map's start before a new one

        // The ground as it is felt out, written to a file of the dumps folder so that the
        // search can be run over it again without the game.
        class GroundRecorder
        {
        public:
            void Line(const std::string& line)
            {
                if (!Open()) return;
                m_buffer += line;
                m_buffer += '\n';
                if (m_buffer.size() > 65536) Flush();
            }
            void Flush()
            {
                if (!m_file.is_open() || m_buffer.empty()) return;
                m_file << m_buffer;
                m_file.flush();
                m_buffer.clear();
            }

        private:
            bool Open()
            {
                if (m_file.is_open()) return true;
                if (m_tried) return false;
                m_tried = true;
                const std::wstring dir = diag::DumpsDirectory();
                if (dir.empty()) return false;
                std::time_t now = std::time(nullptr);
                std::tm local{};
                localtime_s(&local, &now);
                wchar_t stamp[32];
                std::wcsftime(stamp, std::size(stamp), L"%Y%m%d-%H%M%S", &local);
                const std::wstring path = dir + L"\\ground-" + stamp + L".tsv";
                m_file.open(std::filesystem::path(path), std::ios::out | std::ios::trunc);
                if (!m_file.is_open())
                {
                    log::Error(L"explore: the ground record could not be opened at {}", path);
                    return false;
                }
                m_file << ground::HeaderLine() << '\n';
                log::Info(L"explore: the ground felt out is recorded in {}", path);
                return true;
            }
            std::ofstream m_file;
            std::string m_buffer;
            bool m_tried = false;
        };
        GroundRecorder g_record;

        struct LimitedPath
        {
            bool set = false;
            Vec at;      // the point of the path nearest the character
            Vec forward; // the path's forward there
            double angleLeft = 0.0, angleRight = 0.0;
            double lowerDot = 0.0, upperDot = 0.0;
            UObject* spline = nullptr; // the spline actor the game holds her to, as its state names it
            double along = 0.0;        // how far along that spline the game has her
        };
        LimitedPath g_path;
        UObject* g_pathSpline = nullptr; // the spline actor of the path, once found
        // Which way along the spline the game lets the character walk: +1 where its distance
        // grows, -1 where it shrinks, 0 not known. A spline runs whichever way it was drawn.
        int g_pathSign = 0;
        bool AheadOnPath(const Vec& here, const Vec& point);
        double PathAhead(const Vec& here, const Vec& point);
        // A thing within this along the path counts as level with the character: neither
        // behind her nor a step to walk.
        constexpr double kLevelOnPath = 50.0;
        std::vector<Vec> PathRoad(const Vec& here, const Vec& point);
        std::vector<Vec> PathRoadToEnd(const Vec& here);
        UObject* PathSplineComponent();
        bool SplineLength(UObject* component, double& length);
        bool SplineLocation(UObject* component, double distance, Vec& out);
        double PathBearing(double yaw);

        // Facing the thing: the game takes the press only once the character, or the camera,
        // looks at it, and until then the aim sound leads the way to turn.
        UObject* g_facing = nullptr;
        bool g_faced = false;
        double g_facingBlipAt = 0.0;
        double g_facingLoggedAt = 0.0;
        // When the reach map has settled which ways the list keeps, and when the row rule judges
        // the list (features/WayList).
        ways::Settling g_settle;
        ways::Lister g_lister;
        // The map whose end freezes the list's verdicts must be later than this one (ways::Lister::Freeze),
        // and the story's last move the list began its verdicts anew at.
        int64_t g_freezeAfter = -1;
        double g_listerStoryAt = -1000.0;
        bool g_meshRoadsBefore = false; // the navigation mesh gave the roads at the last look
        // The target was chosen by the mod (at a hand-over, or when nothing was chosen), not
        // by the player: it gives way when the list leaves it out.
        bool g_autoChosen = false;
        // The mod's choice is to be said once the reach map has settled the list.
        bool g_choiceDue = false;
        // The list has not settled yet (g_settle), and a press of the next or previous target that
        // found nothing waits for it (its direction, 0 for none).
        bool g_waysSettling = false;
        int g_stepDue = 0;
        // New ways waiting to be announced until the ground has been felt out to them, and
        // since when: a way found closed off, or on the road to another, is never news.
        std::map<UObject*, double> g_pendingNew;
        // When the game turned a thing or a way on, seen off before (ways::IsNews).
        std::map<UObject*, double> g_turnedOnAt;
        std::optional<double> TurnedOnAt(UObject* actor)
        {
            const auto it = g_turnedOnAt.find(actor);
            if (it == g_turnedOnAt.end()) return std::nullopt;
            return it->second;
        }
        // When the list of this hold began: a hold no map ever judges lists its things after a
        // while (ways::kSettleCap) all the same.
        double g_listSince = 0.0;

        using explore::Metres;
        using explore::NormalizeDegrees;
        using explore::SectorKey;

        // ---- the character and the camera -------------------------------------------------

        // The character's binding state: unbound (0) and in a special locomotion (3) are
        // the two states in which the game moves it from the player's input and draws the
        // glints; bound to a sequence (1) or an interaction (2) it is the game's. Read at
        // the place the game keeps it, under the build's canary; -1 when it cannot be read.
        int64_t BindingState(UObject* pawn)
        {
            UObject* bindable = nullptr;
            if (!pawn || !obj::ReadObject(pawn, L"BindableCharacterComponent", bindable) || !obj::IsLive(bindable)) return -1;
            if (void* at = sym::MemberOf(bindable, L"UBatesBindableComponent.BindingState")) return *static_cast<const uint8_t*>(at);
            return -1;
        }

        const wchar_t* BindingName(int64_t state)
        {
            switch (state)
            {
            case 0: return L"unbound";
            case 1: return L"bound to a sequence";
            case 2: return L"bound to an interaction";
            case 3: return L"in a special locomotion";
            default: return L"unknown";
            }
        }

        UObject* CameraManager()
        {
            UObject* controller = Controller();
            UObject* camera = nullptr;
            if (!controller || !obj::ReadObject(controller, L"PlayerCameraManager", camera) || !obj::IsLive(camera)) return nullptr;
            return camera;
        }

        // The camera manager's active layer, the byte the game's own exploration and
        // look-at tests read (an optional: a value and whether it is set); -1 when unset.
        int64_t ActiveCameraLayer()
        {
            UObject* camera = CameraManager();
            if (!camera) return -1;
            const auto* set = static_cast<const uint8_t*>(sym::MemberOf(camera, L"ABatesPlayerCameraManager.ActiveLayer.IsSet"));
            const auto* value = static_cast<const uint8_t*>(sym::MemberOf(camera, L"ABatesPlayerCameraManager.ActiveLayer"));
            if (!set || !value || !*set) return -1;
            return *value;
        }

        // Whether the camera is one the game lets the player explore through, by the game's
        // own test (IsExplorationCameraActiveLayer: the navigation, framing, static and
        // close-up layers, 1 to 8); a cutscene's camera is not. Without the test, the layer
        // byte answers the same way, and without that too the camera is taken as free.
        bool ExplorationCamera()
        {
            UObject* camera = CameraManager();
            if (!camera) return false;
            if (obj::FindFunction(camera, L"IsExplorationCameraActiveLayer")) return obj::CallForBool(camera, L"IsExplorationCameraActiveLayer");
            const int64_t layer = ActiveCameraLayer();
            return layer < 0 || (layer >= 1 && layer <= 8);
        }

        // Whether the controller throws the player's movement input away, as it does in a
        // cutscene (APlayerController::IsMoveInputIgnored).
        bool MoveInputIgnored()
        {
            UObject* controller = Controller();
            return controller && obj::CallForBool(controller, L"IsMoveInputIgnored");
        }

        // The camera layer the controller's navigation camera is on, for the log.
        int64_t CameraLayer()
        {
            UObject* controller = Controller();
            UObject* navigation = nullptr;
            if (!controller || !obj::ReadObject(controller, L"NavigationCameraController", navigation) || !obj::IsLive(navigation)) return -1;
            if (void* at = sym::MemberOf(navigation, L"UBatesNavigationCameraControllerComponent.CameraLayer")) return *static_cast<const uint8_t*>(at);
            return -1;
        }

        std::wstring CameraLayerName(int64_t layer)
        {
            static const wchar_t* const names[] = {L"not set",        L"navigation",       L"navigation high",     L"navigation highest",
                                                   L"framing camera", L"static camera",    L"static camera high",  L"static camera highest",
                                                   L"close-up",       L"activity",         L"activity high",       L"activity highest",
                                                   L"interaction",    L"interaction high", L"interaction highest", L"front end",
                                                   L"debug"};
            if (layer >= 0 && layer < static_cast<int64_t>(std::size(names))) return names[layer];
            return std::format(L"layer {}", layer);
        }

        // Which of the game's characters the pawn is, by the name it carries.
        int CharacterIndex(UObject* pawn)
        {
            static const wchar_t* const names[] = {L"Beth",  L"Hannah", L"Sam",  L"Mike", L"Jessica", L"Emily",        L"Matt",
                                                   L"Chris", L"Ashley", L"Josh", L"Jack", L"Psycho",  L"Psychiatrist", L"Wendigo"};
            std::wstring name;
            if (!pawn || !obj::ReadString(pawn, L"CharacterName", name)) return -1;
            for (size_t i = 0; i < std::size(names); ++i)
            {
                if (str::EqualsNoCase(name, names[i])) return static_cast<int>(i);
            }
            return -1;
        }

        // The component the character overlaps things with.
        UObject* InteractionComponent(UObject* pawn)
        {
            UObject* component = nullptr;
            if (pawn) obj::ReadObject(pawn, L"InteractionComponent", component);
            return obj::IsLive(component) ? component : nullptr;
        }

        bool InsideTriggerVolume(UObject* pawn, UObject* volume);

        // The use locations the game offers the character right now: those on its list of the ones
        // at hand that she stands in (ways::StoodIn). One the list kept after she left it is said
        // once.
        std::set<obj::ObjectKey> g_staleOffers;

        std::vector<UObject*> OfferedUseLocations(UObject* pawn)
        {
            std::vector<UObject*> listed;
            if (UObject* component = InteractionComponent(pawn)) obj::ReadObjectArray(component, L"AvailableUseLocations", listed);
            std::erase_if(listed, [](UObject* actor) { return !obj::IsLive(actor); });
            std::vector<bool> inside;
            inside.reserve(listed.size());
            for (UObject* use : listed)
                inside.push_back(InsideTriggerVolume(pawn, use));
            std::vector<UObject*> out;
            for (const size_t i : ways::StoodIn(inside))
                out.push_back(listed[i]);
            for (size_t i = 0; i < listed.size(); ++i)
            {
                const auto key = obj::KeyOf(listed[i]);
                if (inside[i])
                    g_staleOffers.erase(key);
                else if (g_staleOffers.insert(key).second)
                    log::Info(L"explore: the game's list of things at hand keeps {} ({}), whose volume she is not in: it is no offer",
                              obj::ObjectName(listed[i]), UseLocationLabelOf(listed[i]));
            }
            return out;
        }

        // The interaction the character is in, if any.
        UObject* ActiveInteraction(UObject* pawn)
        {
            UObject* interaction = nullptr;
            if (UObject* component = InteractionComponent(pawn)) obj::ReadObject(component, L"ActiveInteraction", interaction);
            return obj::IsLive(interaction) ? interaction : nullptr;
        }

        // How wide the character is, which is how near a road has to come for the walk to
        // set a trigger off.
        double WalkingRadius(UObject* pawn)
        {
            UObject* capsule = nullptr;
            double radius = 34.0;
            if (pawn && obj::ReadObject(pawn, L"CapsuleComponent", capsule) && obj::IsLive(capsule))
            {
                bool scaled = false;
                obj::CallReturn(capsule, L"GetScaledCapsuleRadius",
                                [&](void* params, FProperty* returnValue) { scaled = obj::ReadFloatAt(params, returnValue, radius); });
                if (!scaled) obj::ReadFloat(capsule, L"CapsuleRadius", radius);
            }
            return radius;
        }

        // Where the character's capsule stands: its middle and how far it reaches up and down.
        bool CapsuleSpan(UObject* pawn, Vec& middle, double& halfHeight)
        {
            UObject* capsule = nullptr;
            if (!pawn || !obj::ReadObject(pawn, L"CapsuleComponent", capsule) || !obj::IsLive(capsule)) return false;
            bool sized = false;
            obj::CallReturn(capsule, L"GetScaledCapsuleHalfHeight",
                            [&](void* params, FProperty* returnValue) { sized = obj::ReadFloatAt(params, returnValue, halfHeight); });
            return ComponentLocation(capsule, middle) && sized;
        }

        // How fast the character is going, as the engine measures it.
        double CharacterSpeed(UObject* pawn)
        {
            Vec velocity;
            if (!VecReturn(pawn, L"GetVelocity", velocity)) return 0.0;
            return std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
        }

        // ---- the level's things ----------------------------------------------------------

        std::wstring CoreName(UObject* actor)
        {
            UObject* core = nullptr;
            std::wstring name;
            if (obj::IsLive(actor) && obj::ReadObject(actor, L"CoreProperties", core) && obj::IsLive(core)) obj::ReadString(core, L"CoreName", name);
            return str::CollapseWhitespace(name);
        }

        // The name the level's own actor database keeps for a thing: the designers' friendly
        // name, which the level's scripts call it by. Asked once per actor, since the answer
        // comes back as a struct whose string and map stay with the pool.
        std::map<obj::ObjectKey, std::wstring> g_friendlyNames;

        std::wstring FriendlyName(UObject* actor)
        {
            if (!obj::IsLive(actor)) return {};
            if (const auto known = g_friendlyNames.find(obj::KeyOf(actor)); known != g_friendlyNames.end()) return known->second;
            std::wstring name;
            UObject* library = obj::FindObject(L"/Script/BMCommonRuntime.Default__LADFunctionLibrary");
            auto* find = library ? obj::FindFunction(library, L"FindLevelDatabaseByActor") : nullptr;
            UObject* database = nullptr;
            if (find)
            {
                obj::Call(
                    library, find,
                    [&](void* params)
                    {
                        for (auto* prop : find->ForEachProperty())
                        {
                            if (prop && prop->GetName() == L"Actor") *static_cast<UObject**>(obj::ValuePtrAt(params, prop)) = actor;
                        }
                    },
                    [&](void* params)
                    {
                        for (auto* prop : find->ForEachProperty())
                        {
                            if (prop && prop->GetName() == L"ReturnValue") obj::ReadObjectAt(params, prop, database);
                        }
                    });
            }
            auto* get = obj::IsLive(database) ? obj::FindFunction(database, L"GetActorData") : nullptr;
            if (get)
            {
                obj::Call(
                    database, get,
                    [&](void* params)
                    {
                        for (auto* prop : get->ForEachProperty())
                        {
                            if (!prop) continue;
                            const auto n = prop->GetName();
                            if (n == L"Actor")
                                *static_cast<UObject**>(obj::ValuePtrAt(params, prop)) = actor;
                            else if (n == L"bCheckSublevels")
                                *static_cast<bool*>(obj::ValuePtrAt(params, prop)) = true;
                        }
                    },
                    [&](void* params)
                    {
                        for (auto* prop : get->ForEachProperty())
                        {
                            if (prop && prop->GetName() == L"OutData")
                                obj::ReadStringAt(obj::ValuePtrAt(params, prop), obj::StructMember(prop, L"FriendlyName"), name);
                        }
                    });
            }
            name = str::CollapseWhitespace(name);
            if (g_friendlyNames.size() > 512) std::erase_if(g_friendlyNames, [](const auto& item) { return !obj::IsSame(item.first); });
            g_friendlyNames[obj::KeyOf(actor)] = name;
            return name;
        }

        // The words of a class name that say only what kind of thing the class is.
        std::wstring StripKindWords(const std::wstring& words)
        {
            std::vector<std::wstring> kept;
            for (const auto& word : str::Split(words, L' '))
            {
                if (word == L"Interaction" || word == L"Bates" || word == L"BP" || word == L"Use" || word == L"Location" || word == L"C") continue;
                kept.push_back(word);
            }
            return str::Join(kept, L" ");
        }

        // The name a use location goes by. The game draws no words over a thing, only a glint
        // and the key, so the level's own English name read as words is all there is to call
        // it by: the actor database's friendly name of the interaction behind it, then of the
        // use location itself, then the core name of either, then the sequence of the story a
        // use location starts, then the kind of interaction it is.
        std::wstring UseLocationLabel(UObject* actor, std::wstring& coreName)
        {
            UObject* interaction = nullptr;
            obj::ReadObject(actor, L"InteractionActor", interaction);
            std::wstring name = FriendlyName(interaction);
            if (name.empty()) name = FriendlyName(actor);
            if (name.empty()) name = CoreName(interaction);
            if (name.empty()) name = CoreName(actor);
            coreName = name;
            std::wstring words = StripKindWords(explore::NameWords(name));
            if (words.empty())
            {
                std::wstring sequence;
                if (obj::ReadString(actor, L"SequenceToStart", sequence) && !sequence.empty())
                {
                    coreName = sequence;
                    words = StripKindWords(explore::NameWords(sequence));
                }
            }
            if (words.empty() && obj::IsLive(interaction)) words = StripKindWords(explore::NameWords(obj::ClassName(interaction)));
            if (words.empty()) words = StripKindWords(explore::NameWords(obj::ClassName(actor)));
            if (words.empty()) words = locale::Mod(L"explore.place");
            return words;
        }

        // The use location's own flags, at the places the game keeps them (the glint is drawn
        // while the first is set, the game's interact test asks the next three as well).
        bool ReadUseState(UObject* actor, UseState& out)
        {
            const uint8_t* enabled = static_cast<const uint8_t*>(sym::MemberOf(actor, L"ABatesUseLocation.bIsUseLocationEnabled"));
            if (!enabled) return false;
            const uint8_t* activity = static_cast<const uint8_t*>(sym::MemberOf(actor, L"ABatesUseLocation.bIsLinkedActivityEnabled"));
            const uint8_t* sleeping = static_cast<const uint8_t*>(sym::MemberOf(actor, L"ABatesUseLocation.bIsLinkedActivitySleeping"));
            const uint8_t* inProgress = static_cast<const uint8_t*>(sym::MemberOf(actor, L"ABatesUseLocation.bInteractionInProgress"));
            out.enabled = *enabled != 0;
            out.activity = activity && *activity != 0;
            out.sleeping = sleeping && *sleeping != 0;
            out.inProgress = inProgress && *inProgress != 0;
            out.known = true;
            return true;
        }

        // Whether an interaction has an animation for the player's character, as the game looks it
        // up (ABatesInteractionBase::GetCharacterBodyInteractionAnimClass): her own entry in
        // BoundCharacters (a map of character to binding data: its elements 32 bytes apart, the
        // character at +0, the animation class at +8, the next of its hash chain at +0x18; laid
        // out as InteractionAngleFor reads its map), else, for a generic interaction, the
        // animation of her body (GenericBoundCharacter: the female one at +0, the male at +8).
        bool AnimatesCharacter(UObject* interaction, UObject* pawn)
        {
            if (!obj::IsLive(interaction) || g_playerCharacter < 0) return false;
            auto* prop = obj::FindProperty(interaction, L"BoundCharacters");
            const auto* map = prop ? static_cast<const uint8_t*>(obj::ValuePtr(interaction, prop)) : nullptr;
            if (map)
            {
                const int32_t count = *reinterpret_cast<const int32_t*>(map + 0x08);
                const int32_t freeCount = *reinterpret_cast<const int32_t*>(map + 0x34);
                const int32_t hashSize = *reinterpret_cast<const int32_t*>(map + 0x48);
                if (count != freeCount && count > 0 && hashSize > 0 && hashSize <= 4096 && (hashSize & (hashSize - 1)) == 0)
                {
                    const auto* hash = *reinterpret_cast<const int32_t* const*>(map + 0x40);
                    if (!hash) hash = reinterpret_cast<const int32_t*>(map + 0x38);
                    const auto* elements = *reinterpret_cast<const uint8_t* const*>(map);
                    int32_t index = hash[g_playerCharacter & (hashSize - 1)];
                    for (int guard = 0; elements && index >= 0 && index < count && guard < 64; ++guard)
                    {
                        const uint8_t* element = elements + static_cast<size_t>(index) * 32;
                        if (element[0] == static_cast<uint8_t>(g_playerCharacter)) return *reinterpret_cast<void* const*>(element + 8) != nullptr;
                        index = *reinterpret_cast<const int32_t*>(element + 0x18);
                    }
                }
            }
            bool generic = false;
            int64_t gender = -1;
            if (!obj::ReadBool(interaction, L"bIsGenericInteraction", generic) || !generic || !obj::ReadInt(pawn, L"Gender", gender)) return false;
            const auto* binding = static_cast<const uint8_t*>(obj::StructPtr(interaction, L"GenericBoundCharacter"));
            return binding && *reinterpret_cast<void* const*>(binding + (gender == 0 ? 0 : 8)) != nullptr;
        }

        // Whether the game lets this character use the use location (ways::UsableBy).
        bool ForThisCharacter(UObject* actor)
        {
            ways::UseRule rule;
            obj::ReadBool(actor, L"bRequireValidCharacterABP", rule.requiresAnimation);
            if (rule.requiresAnimation)
            {
                UObject* interaction = nullptr;
                obj::ReadObject(actor, L"InteractionActor", interaction);
                rule.animated = AnimatesCharacter(interaction, Pawn());
            }
            obj::ReadBool(actor, L"bCanAnyoneInteract", rule.anyone);
            int64_t who = -1;
            obj::ReadInt(actor, L"CharacterToInteract", who);
            rule.hers = g_playerCharacter >= 0 && who == g_playerCharacter;
            return ways::UsableBy(rule);
        }

        // The box the game keeps for a trigger, in the world's own coordinates, from the
        // actor's colliding components.
        bool TriggerBox(UObject* actor, Vec& low, Vec& high)
        {
            auto* fn = obj::IsLive(actor) ? obj::FindFunction(actor, L"GetActorBounds") : nullptr;
            if (!fn) return false;
            Vec origin{}, extent{};
            bool read = false;
            for (const bool collidingOnly : {true, false})
            {
                obj::Call(
                    actor, fn,
                    [&](void* params)
                    {
                        for (auto* prop : fn->ForEachProperty())
                        {
                            if (prop && prop->GetName() == L"bOnlyCollidingComponents") *static_cast<bool*>(obj::ValuePtrAt(params, prop)) = collidingOnly;
                        }
                    },
                    [&](void* params)
                    {
                        for (auto* prop : fn->ForEachProperty())
                        {
                            if (!prop) continue;
                            const auto name = prop->GetName();
                            if (name == L"Origin")
                                read = ReadVec(obj::ValuePtrAt(params, prop), prop, origin);
                            else if (name == L"BoxExtent")
                                ReadVec(obj::ValuePtrAt(params, prop), prop, extent);
                        }
                    });
                if (read && (extent.x > 1.0 || extent.y > 1.0)) break;
            }
            if (!read || (extent.x <= 1.0 && extent.y <= 1.0)) return false;
            low = Vec{origin.x - extent.x, origin.y - extent.y, origin.z - extent.z};
            high = Vec{origin.x + extent.x, origin.y + extent.y, origin.z + extent.z};
            return true;
        }

        // The engine's measure of the distance from a point to a volume's collision, which
        // is zero inside, or -1 where the engine cannot measure it. A trigger's collision is
        // its own mesh component.
        double VolumeDistance(UObject* volume, const Vec& point)
        {
            UObject* body = nullptr;
            obj::ReadObject(volume, L"TriggerVolume", body);
            auto* fn = obj::IsLive(body) ? obj::FindFunction(body, L"GetClosestPointOnCollision") : nullptr;
            double distance = -1.0;
            if (fn)
            {
                obj::Call(
                    body, fn,
                    [&](void* params)
                    {
                        for (auto* prop : fn->ForEachProperty())
                        {
                            if (prop && prop->GetName() == L"Point") WriteVec(obj::ValuePtrAt(params, prop), prop, point);
                        }
                    },
                    [&](void* params)
                    {
                        for (auto* prop : fn->ForEachProperty())
                        {
                            if (prop && prop->GetName() == L"ReturnValue") obj::ReadFloatAt(params, prop, distance);
                        }
                    });
            }
            return distance;
        }

        // A volume's own box, turned as it stands: its body's mesh bounds, scaled and turned with
        // the body. The engine's bounds of the volume are square with the world; at act2_1 the way
        // on the path to the lodge is a wall of a mesh 1.4 m thick, 10 m long, set at -24 degrees,
        // whose bounds reached to Matt's feet 3 m short of it: the map took the way for reached
        // where he stood, and every walk circled there and gave up (2026-11-02). Read once per
        // volume, since volumes do not move.
        std::map<obj::ObjectKey, std::pair<bool, ways::Box>> g_volumeBoxes;

        bool VolumeBox(UObject* volume, ways::Box& out)
        {
            const auto key = obj::KeyOf(volume);
            if (const auto known = g_volumeBoxes.find(key); known != g_volumeBoxes.end())
            {
                out = known->second.second;
                return known->second.first;
            }
            bool read = false;
            UObject* body = nullptr;
            Vec location, scale, min, max;
            double pitch = 0.0, yaw = 0.0;
            if (obj::ReadObject(volume, L"TriggerVolume", body) && obj::IsLive(body) && VecReturn(body, L"K2_GetComponentLocation", location) &&
                VecReturn(body, L"K2_GetComponentScale", scale) && RotationOf(body, L"K2_GetComponentRotation", pitch, yaw))
            {
                bool readMin = false, readMax = false;
                if (auto* fn = obj::FindFunction(body, L"GetLocalBounds"))
                {
                    obj::Call(body, fn, nullptr,
                              [&](void* params)
                              {
                                  for (auto* prop : fn->ForEachProperty())
                                  {
                                      if (!prop) continue;
                                      if (prop->GetName() == L"Min") readMin = ReadVec(obj::ValuePtrAt(params, prop), prop, min);
                                      if (prop->GetName() == L"Max") readMax = ReadVec(obj::ValuePtrAt(params, prop), prop, max);
                                  }
                              });
                }
                const Vec size{(max.x - min.x) * std::fabs(scale.x), (max.y - min.y) * std::fabs(scale.y), (max.z - min.z) * std::fabs(scale.z)};
                if (readMin && readMax && size.x > 1.0 && size.y > 1.0)
                {
                    const Vec offset{(min.x + max.x) / 2.0 * scale.x, (min.y + max.y) / 2.0 * scale.y, (min.z + max.z) / 2.0 * scale.z};
                    const double radians = yaw * std::numbers::pi / 180.0;
                    const Vec middle{location.x + offset.x * std::cos(radians) - offset.y * std::sin(radians),
                                     location.y + offset.x * std::sin(radians) + offset.y * std::cos(radians), location.z + offset.z};
                    out = ways::TurnedBox(middle, size, yaw);
                    read = true;
                }
            }
            g_volumeBoxes[key] = {read, out};
            return read;
        }

        // A placement as the engine keeps one (FTransform), from a property's value.
        bool ReadPose(void* value, FProperty* transform, shapes::Pose& pose)
        {
            if (!value || !transform) return false;
            FProperty* rotation = obj::StructMember(transform, L"Rotation");
            FProperty* translation = obj::StructMember(transform, L"Translation");
            FProperty* scale = obj::StructMember(transform, L"Scale3D");
            if (!rotation || !translation || !scale) return false;
            void* q = obj::ValuePtrAt(value, rotation);
            return obj::ReadFloatAt(q, obj::StructMember(rotation, L"X"), pose.rotation.x) &&
                   obj::ReadFloatAt(q, obj::StructMember(rotation, L"Y"), pose.rotation.y) &&
                   obj::ReadFloatAt(q, obj::StructMember(rotation, L"Z"), pose.rotation.z) &&
                   obj::ReadFloatAt(q, obj::StructMember(rotation, L"W"), pose.rotation.w) &&
                   ReadVec(obj::ValuePtrAt(value, translation), translation, pose.translation) && ReadVec(obj::ValuePtrAt(value, scale), scale, pose.scale);
        }

        bool ReadRotator(void* value, FProperty* rotator, double& pitch, double& yaw, double& roll)
        {
            return value && rotator && obj::ReadFloatAt(value, obj::StructMember(rotator, L"Pitch"), pitch) &&
                   obj::ReadFloatAt(value, obj::StructMember(rotator, L"Yaw"), yaw) && obj::ReadFloatAt(value, obj::StructMember(rotator, L"Roll"), roll);
        }

        // A volume's collision as the game tests the character against it (features/Shapes): the
        // convex pieces, boxes, balls and capsules of its body's setup (UBodySetup::AggGeom: its
        // mesh's, for a trigger's mesh component; its own, for a brush), placed where the body
        // stands. Nearly every trigger mesh of the game carries its shape in its points: of 2232,
        // the box square with the mesh reaches more than 30 cm beyond the pieces for 2100, up to
        // 20 m (research/triggers/trigger_shapes.tsv); at act2_1 the way on to Mike's choice is a
        // slab set aslant in its mesh, and the box around it held the spot 4.5 m short of it
        // where Matt circled (2026-11-02). Read once per volume, since volumes do not move;
        // logged and put in the ground record once, so that the record replays the walk to it.
        // The pieces of a body's collision (UBodySetup::AggGeom: convex pieces, boxes, balls and
        // capsules) placed where the body stands, `pose` being the body's own place in the world.
        void ReadAggGeom(UObject* setup, const shapes::Pose& pose, shapes::Shape& shape)
        {
            FProperty* aggProp = obj::IsLive(setup) ? obj::FindProperty(setup, L"AggGeom") : nullptr;
            void* agg = aggProp ? obj::ValuePtr(setup, aggProp) : nullptr;
            if (!agg) return;
            obj::ForEachArrayElement(agg, obj::StructMember(aggProp, L"ConvexElems"),
                                     [&](void* element, FProperty* inner)
                                     {
                                         // A convex piece's points stand in its own placement within the body.
                                         shapes::Pose local;
                                         if (FProperty* transform = obj::StructMember(inner, L"Transform"))
                                             ReadPose(obj::ValuePtrAt(element, transform), transform, local);
                                         shapes::Hull hull;
                                         obj::ForEachArrayElement(element, obj::StructMember(inner, L"VertexData"),
                                                                  [&](void* point, FProperty* pointProp)
                                                                  {
                                                                      Vec p;
                                                                      if (ReadVec(point, pointProp, p))
                                                                          hull.points.push_back(shapes::Place(pose, shapes::Place(local, p)));
                                                                  });
                                         if (!hull.points.empty()) shape.hulls.push_back(std::move(hull));
                                     });
            obj::ForEachArrayElement(agg, obj::StructMember(aggProp, L"BoxElems"),
                                     [&](void* element, FProperty* inner)
                                     {
                                         // A box's sizes are whole lengths along its own axes.
                                         Vec middle;
                                         double pitch = 0.0, yaw = 0.0, roll = 0.0, x = 0.0, y = 0.0, z = 0.0;
                                         FProperty* centerProp = obj::StructMember(inner, L"Center");
                                         if (!centerProp || !ReadVec(obj::ValuePtrAt(element, centerProp), centerProp, middle)) return;
                                         if (FProperty* rotationProp = obj::StructMember(inner, L"Rotation"))
                                             ReadRotator(obj::ValuePtrAt(element, rotationProp), rotationProp, pitch, yaw, roll);
                                         if (!obj::ReadFloatAt(element, obj::StructMember(inner, L"X"), x) ||
                                             !obj::ReadFloatAt(element, obj::StructMember(inner, L"Y"), y) ||
                                             !obj::ReadFloatAt(element, obj::StructMember(inner, L"Z"), z))
                                             return;
                                         auto hull = shapes::BoxHull(middle, Vec{x / 2.0, y / 2.0, z / 2.0}, shapes::FromRotator(pitch, yaw, roll));
                                         for (auto& p : hull.points)
                                             p = shapes::Place(pose, p);
                                         shape.hulls.push_back(std::move(hull));
                                     });
            const double least = std::min({std::fabs(pose.scale.x), std::fabs(pose.scale.y), std::fabs(pose.scale.z)});
            obj::ForEachArrayElement(agg, obj::StructMember(aggProp, L"SphereElems"),
                                     [&](void* element, FProperty* inner)
                                     {
                                         Vec middle;
                                         double radius = 0.0;
                                         FProperty* centerProp = obj::StructMember(inner, L"Center");
                                         if (!centerProp || !ReadVec(obj::ValuePtrAt(element, centerProp), centerProp, middle) ||
                                             !obj::ReadFloatAt(element, obj::StructMember(inner, L"Radius"), radius))
                                             return;
                                         shape.balls.push_back({shapes::Place(pose, middle), radius * least});
                                     });
            obj::ForEachArrayElement(agg, obj::StructMember(aggProp, L"SphylElems"),
                                     [&](void* element, FProperty* inner)
                                     {
                                         // A capsule's length is that of its segment, along its own up axis.
                                         Vec middle;
                                         double pitch = 0.0, yaw = 0.0, roll = 0.0, radius = 0.0, length = 0.0;
                                         FProperty* centerProp = obj::StructMember(inner, L"Center");
                                         if (!centerProp || !ReadVec(obj::ValuePtrAt(element, centerProp), centerProp, middle) ||
                                             !obj::ReadFloatAt(element, obj::StructMember(inner, L"Radius"), radius) ||
                                             !obj::ReadFloatAt(element, obj::StructMember(inner, L"Length"), length))
                                             return;
                                         if (FProperty* rotationProp = obj::StructMember(inner, L"Rotation"))
                                             ReadRotator(obj::ValuePtrAt(element, rotationProp), rotationProp, pitch, yaw, roll);
                                         const Vec half = shapes::Turn(shapes::FromRotator(pitch, yaw, roll), Vec{0.0, 0.0, length / 2.0});
                                         shape.pills.push_back({shapes::Place(pose, Vec{middle.x - half.x, middle.y - half.y, middle.z - half.z}),
                                                                shapes::Place(pose, Vec{middle.x + half.x, middle.y + half.y, middle.z + half.z}),
                                                                radius * std::min(std::fabs(pose.scale.x), std::fabs(pose.scale.y))});
                                     });
        }

        std::map<obj::ObjectKey, std::pair<bool, shapes::Shape>> g_volumeShapes;

        bool VolumeShape(UObject* volume, const std::wstring& name, shapes::Shape& out)
        {
            const auto key = obj::KeyOf(volume);
            if (const auto known = g_volumeShapes.find(key); known != g_volumeShapes.end())
            {
                out = known->second.second;
                return known->second.first;
            }
            shapes::Shape shape;
            UObject* body = nullptr;
            if (!obj::ReadObject(volume, L"TriggerVolume", body) || !obj::IsLive(body)) obj::ReadObject(volume, L"BrushComponent", body);
            UObject* setup = nullptr;
            UObject* mesh = nullptr;
            if (obj::IsLive(body) && obj::ReadObject(body, L"StaticMesh", mesh) && obj::IsLive(mesh)) obj::ReadObject(mesh, L"BodySetup", setup);
            if (!obj::IsLive(setup) && obj::IsLive(body)) obj::ReadObject(body, L"BrushBodySetup", setup);
            shapes::Pose pose;
            bool placed = false;
            if (obj::IsLive(body))
                obj::CallReturn(body, L"K2_GetComponentToWorld",
                                [&](void* params, FProperty* returnValue) { placed = ReadPose(obj::ValuePtrAt(params, returnValue), returnValue, pose); });
            if (placed) ReadAggGeom(setup, pose, shape);
            const bool read = !shape.Empty();
            g_volumeShapes[key] = {read, shape};
            if (read)
            {
                Vec low, high;
                shape.Bounds(low, high);
                size_t points = 0;
                for (const auto& hull : shape.hulls)
                    points += hull.points.size();
                log::Info(
                    L"explore: the collision of {}: {} convex piece(s) of {} points, {} ball(s), {} capsule(s), within ({:.0f}, {:.0f}, {:.0f}) to ({:.0f}, "
                    L"{:.0f}, {:.0f})",
                    name, shape.hulls.size(), points, shape.balls.size(), shape.pills.size(), low.x, low.y, low.z, high.x, high.y, high.z);
                for (const auto& line : ground::VolumeLines(str::WideToUtf8(name), shape))
                    g_record.Line(line);
            }
            else
            {
                log::Info(L"explore: the collision of {} is not read ({}); the box around it stands for it", name,
                          !obj::IsLive(body)    ? L"no body"
                          : !obj::IsLive(setup) ? L"no body setup"
                          : !placed             ? L"no placement"
                                                : L"no pieces");
            }
            out = shape;
            return read;
        }

        bool PointInVolume(UObject* volume, const Vec& point, const Vec& low, const Vec& high)
        {
            const double distance = VolumeDistance(volume, point);
            if (distance >= 0.0) return distance == 0.0;
            return point.x >= low.x && point.x <= high.x && point.y >= low.y && point.y <= high.y && point.z >= low.z && point.z <= high.z;
        }

        // Whether the character stands inside a volume: the game counts it in once its
        // capsule touches the volume, so the point measured is on the character's own
        // upright line, at the height of the volume's middle kept within the capsule.
        bool InsideVolume(UObject* pawn, UObject* volume, const Vec& low, const Vec& high, int& storey)
        {
            storey = 0;
            Vec middle;
            double halfHeight = 0.0;
            if (!CapsuleSpan(pawn, middle, halfHeight)) return false;
            const double bottom = middle.z - halfHeight;
            const double top = middle.z + halfHeight;
            if (low.z > top)
                storey = 1;
            else if (high.z < bottom)
                storey = -1;
            if (storey != 0) return false;
            const Vec probe{middle.x, middle.y, std::clamp((low.z + high.z) / 2.0, bottom, top)};
            const double distance = VolumeDistance(volume, probe);
            if (distance >= 0.0) return distance <= WalkingRadius(pawn);
            return PointInVolume(volume, probe, low, high);
        }

        // Whether the character overlaps a volume by the engine's own account, which is what
        // sets a trigger volume off (AActor::IsOverlappingActor).
        bool Overlapping(UObject* pawn, UObject* actor)
        {
            if (!obj::IsLive(pawn) || !obj::IsLive(actor)) return false;
            auto* fn = obj::FindFunction(pawn, L"IsOverlappingActor");
            if (!fn) return false;
            bool result = false;
            obj::Call(
                pawn, fn,
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"Other") *static_cast<UObject**>(obj::ValuePtrAt(params, prop)) = actor;
                    }
                },
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"ReturnValue") obj::ReadBoolAt(params, prop, result);
                    }
                });
            return result;
        }

        // Whether a road, walked at the character's height, comes within the character's
        // width of a volume: the walk there sets the volume off.
        bool RoadEnters(UObject* volume, const Vec& low, const Vec& high, const std::vector<Vec>& road, double radius, double halfHeight)
        {
            for (size_t i = 1; i < road.size(); ++i)
            {
                const Vec& a = road[i - 1];
                const Vec& b = road[i];
                const int steps = std::max(1, static_cast<int>(FlatDistance(a, b) / kRoadStep));
                for (int step = 0; step <= steps; ++step)
                {
                    const double f = static_cast<double>(step) / steps;
                    Vec p{a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f};
                    if (p.x < low.x - radius || p.x > high.x + radius || p.y < low.y - radius || p.y > high.y + radius) continue;
                    p.z = std::clamp((low.z + high.z) / 2.0, p.z, p.z + 2.0 * halfHeight);
                    if (p.z < low.z || p.z > high.z) continue;
                    const double distance = VolumeDistance(volume, p);
                    if (distance >= 0.0 ? distance <= radius : PointInVolume(volume, p, low, high)) return true;
                }
            }
            return false;
        }

        // ---- the level's scripts ---------------------------------------------------------

        // The scripts the level runs: its script group's main script and the scripts it imports
        // (the game's Lua updater's CurrentLevelScript). Every act's scripts stay loaded (465
        // trigger scripts of the whole game at chapter one), and their machines share names
        // (each act has its Dialogue_control), so a scene's questions are read from these alone.
        std::vector<UObject*> LevelScripts()
        {
            UObject* updater = obj::FindFirstLive(L"BatesLuaScriptUpdater");
            UObject* group = nullptr;
            if (!updater || !obj::ReadObject(updater, L"CurrentLevelScript", group) || !obj::IsLive(group))
            {
                static bool told = false;
                if (!told)
                    log::Info(L"explore: the level's script group is not found ({}), so no scene's questions are read", updater ? L"none set" : L"no updater");
                told = true;
                return {};
            }
            if (!(obj::KeyOf(group) == g_levelScriptGroup))
            {
                g_levelScriptGroup = obj::KeyOf(group);
                g_setOff.NewScripts();
            }
            std::vector<UObject*> level;
            UObject* main = nullptr;
            if (obj::ReadObject(group, L"MainScript", main) && obj::IsLive(main)) level.push_back(main);
            std::vector<UObject*> imported;
            obj::ReadObjectArray(group, L"ImportingScripts", imported);
            for (auto* script : imported)
            {
                if (obj::IsLive(script) && std::find(level.begin(), level.end(), script) == level.end()) level.push_back(script);
            }
            return level;
        }

        // Reads what the level's scripts do when the character walks, from the assets the game
        // loads for them: a trigger's script (scripts::ParseEnterEvent), each asset once, and the
        // questions of the scenes' state machines in the scripts the level runs
        // (scripts::ParsePolls), each asset once.
        void ScanLuaEvents(double now)
        {
            if (now - g_luaScannedAt < 10.0) return;
            g_luaScannedAt = now;
            const auto assets = obj::FindAllLive(L"LuaScriptAsset");
            bool changed = false;
            for (auto* asset : assets)
            {
                if (g_luaByAsset.count(asset)) continue;
                if (!str::EndsWith(obj::ObjectName(asset), L"-Enter")) continue;
                std::wstring script;
                if (!obj::ReadString(asset, L"Script", script)) continue;
                LuaEvent parsed;
                if (!scripts::ParseEnterEvent(script, parsed)) continue;
                g_luaByAsset[asset] = parsed;
                g_luaEvents[parsed.trigger] = parsed;
                changed = true;
            }
            std::erase_if(g_luaByAsset, [&](const auto& item) { return std::find(assets.begin(), assets.end(), item.first) == assets.end(); });
            const auto level = LevelScripts();
            bool pollsChanged = false;
            for (auto* asset : level)
            {
                if (g_pollsByAsset.count(asset)) continue;
                std::wstring script;
                obj::ReadString(asset, L"Script", script);
                g_pollsByAsset[asset] = scripts::ParsePolls(script, obj::FullName(asset));
                g_moveWaitsByAsset[asset] = scripts::ParseMoveWaits(script, obj::FullName(asset));
                g_scriptByAsset[asset] = std::move(script);
                pollsChanged = true;
            }
            pollsChanged |=
                std::erase_if(g_pollsByAsset, [&](const auto& item) { return std::find(level.begin(), level.end(), item.first) == level.end(); }) > 0;
            std::erase_if(g_moveWaitsByAsset, [&](const auto& item) { return std::find(level.begin(), level.end(), item.first) == level.end(); });
            if (pollsChanged)
            {
                g_moveWaits.clear();
                std::vector<std::wstring> lines;
                for (const auto& [asset, waits] : g_moveWaitsByAsset)
                {
                    for (const auto& w : waits)
                    {
                        lines.push_back(std::format(L"{} {} {}{}{}", w.machine, w.state, w.negated ? L"not " : L"", w.who, w.story ? L"; moves the scene on" : L""));
                        g_moveWaits.push_back(w);
                    }
                }
                log::Info(L"explore: the scenes wait for a character to walk on: {}", lines.empty() ? L"nowhere" : str::Join(lines, L"; "));
            }
            std::erase_if(g_scriptByAsset, [&](const auto& item) { return std::find(level.begin(), level.end(), item.first) == level.end(); });
            if (pollsChanged)
            {
                std::map<std::wstring, double> variables;
                for (const auto& [asset, script] : g_scriptByAsset)
                    variables.merge(scripts::ParseVariables(script));
                g_switches.clear();
                std::vector<std::wstring> lines;
                for (const auto& [asset, script] : g_scriptByAsset)
                {
                    for (auto& sw : scripts::ParseSwitches(script, variables))
                    {
                        lines.push_back(std::format(L"{} by {} {}{}", sw.character, sw.machine, sw.state,
                                                    sw.seconds > 0.0 ? std::format(L", then a wait of {:g} s before the scene goes on", sw.seconds) : L""));
                        g_switches.push_back(std::move(sw));
                    }
                }
                log::Info(L"explore: the scenes make a character the player's: {}", lines.empty() ? L"none" : str::Join(lines, L"; "));
            }
            if (changed)
            {
                std::vector<std::wstring> lines;
                for (const auto& [trigger, event] : g_luaEvents)
                    lines.push_back(std::format(L"{} ({}; {}{})", trigger, event.limit < 0 ? L"every time" : std::format(L"{} time(s)", event.limit),
                                                str::Join(event.calls, L" "), event.story ? L"; moves the scene on" : L""));
                log::Info(L"explore: the level's trigger scripts: {}", lines.empty() ? L"none" : str::Join(lines, L"; "));
            }
            if (pollsChanged)
            {
                std::vector<scripts::Poll> all;
                for (const auto& [asset, polls] : g_pollsByAsset)
                    all.insert(all.end(), polls.begin(), polls.end());
                std::vector<std::wstring> lines;
                for (const auto& p : all)
                    lines.push_back(std::format(L"{} {} {}{} in {} ({}{})", p.machine, p.state, p.negated ? L"not " : L"", p.who, p.trigger,
                                                str::Join(p.calls, L" "), p.story ? L"; moves the scene on" : L""));
                log::Info(L"explore: the scenes ask whether a character stands in a volume: {}", lines.empty() ? L"nowhere" : str::Join(lines, L"; "));
                g_waits.SetPolls(std::move(all));
            }
        }

        // The game's scripts ask whether a character is inside a volume (SF_IsInTrigger), at
        // every update of every waiting state that asks it.
        void OnIsInTrigger(UObject*, FFrame& stack)
        {
            UObject* asked = nullptr;
            std::wstring trigger;
            params::Object(stack, L"inGameObject", asked);
            params::String(stack, L"inTriggerName", trigger);
            if (trigger.empty()) return;
            const bool player = asked && asked == g_controlPawn;
            g_waits.Asked(g_playClock, trigger, player);
            if (!g_pollHeard)
            {
                g_pollHeard = true;
                log::Info(L"explore: the game's scripts ask whether {} stands in {}", asked ? obj::ObjectName(asked) : L"<nothing>", trigger);
            }
        }

        // Runs the play clock: on while the game is in play.
        void TickPlayClock(double now)
        {
            if (g_playClockAt >= 0.0 && gamestate::InPlay()) g_playClock += std::clamp(now - g_playClockAt, 0.0, 0.5);
            g_playClockAt = now;
        }

        // How long the scenes that make a character the player's wait before they go on with her:
        // the longest wait after any of their switches to her (scripts::Switch), none without one.
        double SwitchWait(const std::wstring& character)
        {
            double wait = 0.0;
            for (const auto& sw : g_switches)
            {
                if (str::EqualsNoCase(sw.character, character)) wait = std::max(wait, sw.seconds);
            }
            return wait;
        }

        // ---- what is where -----------------------------------------------------------------

        // The level's guidance passages (ABatesGuidancePassageVolume: the doorways' BP_Doorway), found
        // with its other actors, and as swept for the ground at each map's start (ReadPassages).
        std::vector<UObject*> g_passageActors;
        std::vector<ways::Passage> g_passages;
        std::wstring g_passagesLogged;
        Vec g_passagesRecordedAt{1e30, 1e30, 1e30}; // the grid's origin the record has the passages for

        // The level's actors of interest, found now and then: the use locations, the glints
        // that mark them, and the trigger volumes.
        void ScanRooms();
        void ScanOthers(UObject* pawn);

        void ScanLevel(UObject* pawn, double now)
        {
            if (now - g_scannedAt < kScanApart) return;
            g_scannedAt = now;
            g_useLocations = obj::FindAllLive(L"BatesUseLocation");
            g_glintOf.clear();
            for (auto* glint : obj::FindAllLive(L"BatesGlint"))
            {
                UObject* owner = nullptr;
                obj::CallReturn(glint, L"GetOwningUseLocation", [&](void* params, FProperty* returnValue) { obj::ReadObjectAt(params, returnValue, owner); });
                if (obj::IsLive(owner)) g_glintOf[owner] = glint;
            }
            g_stairs = obj::FindAllLive(L"BatesStairActivityActor");
            g_passageActors = obj::FindAllLive(L"BatesGuidancePassageVolume");
            g_volumes.clear();
            for (auto* volume : obj::FindAllLive(L"BatesCharacterTriggerVolume"))
            {
                if (obj::IsA(volume, L"BatesUseLocation") || obj::IsA(volume, L"BatesGlint") || obj::IsA(volume, L"BatesGuidanceTriggerVolume") ||
                    obj::IsA(volume, L"BatesGuidancePassageVolume"))
                    continue;
                g_volumes.push_back(volume);
            }
            std::erase_if(g_useStates, [](const auto& item) { return !obj::IsLive(item.first); });
            ScanRooms();
            ScanOthers(pawn);
        }

        // A room's name in the player's language, from the table of room names; empty where it names
        // none (the rooms of the chapters not played yet).
        std::wstring RoomName(const std::wstring& key)
        {
            const std::wstring id = L"room." + key;
            return locale::Has(id) ? locale::Mod(id) : std::wstring();
        }

        // The level's rooms: its reverb volumes whose collision is read (VolumeShape), counted anew
        // whenever the volumes change. A volume whose collision is not read is no room: the box
        // square with the world around a room set aslant would hold the next room's things.
        std::map<obj::ObjectKey, bool> g_reverbVolume; // whether a volume is a reverb volume, asked once per volume

        void ScanRooms()
        {
            std::vector<UObject*> volumes;
            for (auto* volume : g_volumes)
            {
                const auto key = obj::KeyOf(volume);
                auto known = g_reverbVolume.find(key);
                if (known == g_reverbVolume.end()) known = g_reverbVolume.emplace(key, !rooms::Key(CoreName(volume)).empty()).first;
                if (known->second) volumes.push_back(volume);
            }
            if (volumes != g_zoneVolumes)
            {
                g_zoneVolumes = volumes;
                g_zones.clear();
                for (auto* volume : volumes)
                {
                    const auto name = CoreName(volume);
                    shapes::Shape shape;
                    if (VolumeShape(volume, name, shape)) g_zones.push_back(rooms::MakeZone(name, std::move(shape)));
                }
                ++g_zonesGeneration;
                g_zoneOf.clear();
                std::vector<std::wstring> named, unnamed;
                for (const auto& z : g_zones)
                {
                    auto& into = RoomName(z.key).empty() ? unnamed : named;
                    const auto word = z.key + (z.broad ? L" (broad)" : L"");
                    if (std::find(into.begin(), into.end(), word) == into.end()) into.push_back(word);
                }
                log::Info(L"explore: the level's rooms: {} reverb volume(s), {} with their collision read; named: {}; with no name in the table: {}",
                          volumes.size(), g_zones.size(), named.empty() ? L"none" : str::Join(named, L", "),
                          unnamed.empty() ? L"none" : str::Join(unnamed, L", "));
            }
            // The names follow the player's language.
            for (auto& z : g_zones)
                z.spoken = RoomName(z.key);
        }

        // The other characters. The companions walk about and stop in doorways, and a sweep that
        // met one took the ground beyond for closed off: at the lodge's yard on 2026-11-02 two steps
        // of the path were refused 3 to 10 cm into the sweep in the first map and found free in the
        // second two minutes later, and the first map ended at 1473 cells of the yard's 5882. The
        // ground is the level's, so they are left out of every sweep (RunTrace); a walk that meets
        // one goes round her as round anything else.
        void ScanOthers(UObject* pawn)
        {
            std::vector<UObject*> others;
            for (auto* character : obj::FindAllLive(L"BatesCharacter"))
            {
                if (character != pawn) others.push_back(character);
            }
            if (others == g_others) return;
            g_others = others;
            std::vector<std::wstring> names;
            for (auto* other : others)
                names.push_back(obj::ObjectName(other));
            log::Info(L"explore: the ground's sweeps leave out {} other character(s): {}", others.size(), names.empty() ? L"none" : str::Join(names, L", "));
        }

        // The other characters still alive, checked once a frame (the engine reads every one the
        // trace is given).
        const std::vector<UObject*>& OthersToIgnore()
        {
            const auto frame = static_cast<uint64_t>(gamethread::FrameCount());
            if (frame != g_othersCheckedAt)
            {
                g_othersCheckedAt = frame;
                std::erase_if(g_others, [](UObject* other) { return !obj::IsLive(other); });
            }
            return g_others;
        }

        // One action the game runs when the character walks into a volume, as it stands now:
        // its class, whether it is of the story's kind, and whether it has fires left by the
        // game's own count (TimesTriggered against TimesCanBeTriggered, which a loaded save
        // keeps).
        struct TriggerAction
        {
            std::wstring cls;
            bool story = false;
            bool firesLeft = true;
        };

        // Whether a condition holds now: yes, no, or not to be known without running it.
        enum class Holds
        {
            Yes,
            No,
            Maybe,
        };

        // The actions behind one condition of a volume.
        struct TriggerGroup
        {
            Holds condition = Holds::Yes;
            std::vector<TriggerAction> actions;
        };

        // The actions that move the scene on: a sequence, an activity, an interaction, the
        // stairs, a walk of the game's own, a message to the level's script, a companion's
        // order, or a line of dialogue the game holds essential. Lessons, looks, items taken
        // in hand, camera, light, locomotion and streaming are not.
        bool StoryAction(UObject* action, const std::wstring& cls)
        {
            static const wchar_t* const kStory[] = {
                L"BatesStartSequenceTriggerAction",       L"BatesStartActivityLocoTriggerAction",         L"BatesStartInteractionTriggerAction",
                L"BatesStartStairsTriggerAction",         L"BatesCharacter_PerformAutoWalkTriggerAction", L"BatesSendMessageTriggerAction",
                L"BatesSetActivityCompleteTriggerAction", L"BatesExecuteCharacterOrder_TriggerAction"};
            if (std::find(std::begin(kStory), std::end(kStory), cls) != std::end(kStory)) return true;
            if (cls == L"BatesPlayDialogueTriggerAction")
            {
                bool essential = false;
                obj::ReadBool(action, L"bIsEssential", essential);
                return essential;
            }
            // A level sequence played on entry is a scene when the sequence actor takes the
            // camera into it (the woods' deer scare, which binds the character and hands her
            // back past the path's wall); a performance loop of the background (bIsPAOSequence)
            // or a sequence that leaves the camera alone is not.
            if (cls == L"PlayLevelSequenceTriggerAction")
            {
                UObject* sequence = nullptr;
                if (!obj::ReadObject(action, L"SequenceToPlayOnEnter", sequence) || !obj::IsLive(sequence)) return false;
                bool background = false, intoCamera = false;
                obj::ReadBool(sequence, L"bIsPAOSequence", background);
                obj::ReadBool(sequence, L"bEnableIntoTransition", intoCamera);
                return !background && intoCamera;
            }
            return false;
        }

        TriggerAction ReadTriggerAction(UObject* action)
        {
            TriggerAction out;
            out.cls = obj::ClassName(action);
            out.story = StoryAction(action, out.cls);
            int64_t can = 0;
            obj::ReadInt(action, L"TimesCanBeTriggered", can);
            int64_t done = 0;
            if (const auto* times = static_cast<const int32_t*>(sym::MemberOf(action, L"UBatesCharacterTriggerAction.TimesTriggered"))) done = *times;
            // The game fires an action with a limit while its count is under the limit, and
            // one without a limit for ever; for the list, once set off is gone either way, as
            // a thing done.
            out.firesLeft = can < 1 ? done == 0 : done < can;
            return out;
        }

        // A value of the game's global state, by the game's own library, as a trigger
        // condition reads it: an int, a float or a bool by the condition's type.
        bool GlobalStateValue(UObject* pawn, int64_t type, const std::wstring& parameter, double& value)
        {
            const wchar_t* getter = type == 1                ? L"GetGlobalStateControlParameterInt"
                                    : type == 2              ? L"GetGlobalStateControlParameterFloat"
                                    : type == 4 || type == 5 ? L"GetGlobalStateControlParameterBool"
                                                             : nullptr;
            if (!getter || parameter.empty()) return false;
            UObject* library = obj::FindObject(L"/Script/Bates.Default__BatesGlobalStateBlueprintLibrary");
            auto* fn = library ? obj::FindFunction(library, getter) : nullptr;
            if (!fn) return false;
            bool read = false;
            obj::Call(
                library, fn,
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (!prop) continue;
                        const auto pname = prop->GetName();
                        if (pname == L"Context")
                            *static_cast<UObject**>(obj::ValuePtrAt(params, prop)) = pawn;
                        else if (pname == L"GlobalStateName")
                            obj::WriteStringAt(params, prop, parameter);
                    }
                },
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (!prop) continue;
                        const auto pname = prop->GetName();
                        if (pname == L"ReturnValue")
                        {
                            bool b = false;
                            int64_t i = 0;
                            double d = 0.0;
                            if (obj::ReadBoolAt(params, prop, b))
                            {
                                value = b ? 1.0 : 0.0;
                                read = true;
                            }
                            else if (obj::ReadIntAt(params, prop, i))
                            {
                                value = static_cast<double>(i);
                                read = true;
                            }
                            else if (obj::ReadFloatAt(params, prop, d))
                            {
                                value = d;
                                read = true;
                            }
                        }
                        else if (pname == L"GlobalStateName")
                        {
                            obj::WriteStringAt(params, prop, L""); // gives the string's memory back
                        }
                    }
                });
            return read;
        }

        // Whether a trigger condition holds now, from the game's own values and without
        // running it: a condition on a global-state parameter is compared as the game
        // compares it, a compound one is folded as the game folds it, and a chance, a
        // facing or a Blueprint condition is left as "maybe", since running it would roll
        // its dice.
        Holds ConditionHolds(UObject* condition, UObject* pawn, int depth)
        {
            if (!obj::IsLive(condition)) return Holds::Yes;
            bool negate = false;
            obj::ReadBool(condition, L"bNot", negate);
            Holds inner = Holds::Maybe;
            if (obj::IsA(condition, L"BatesCharacterTriggerControlParamCondition"))
            {
                int64_t type = 0, op = 0;
                double expected = 0.0;
                std::wstring parameter;
                obj::ReadInt(condition, L"ControlParameterType", type);
                obj::ReadInt(condition, L"Operator", op);
                obj::ReadFloat(condition, L"ExpectedValue", expected);
                obj::ReadString(condition, L"ProcessedControlParameterName", parameter);
                if (parameter.empty()) obj::ReadString(condition, L"ControlParameterName", parameter);
                double value = 0.0;
                if (GlobalStateValue(pawn, type, parameter, value))
                {
                    bool met = false;
                    switch (op)
                    {
                    case 0: met = value == expected; break;
                    case 1: met = value != expected; break;
                    case 2: met = value < expected; break;
                    case 3: met = value <= expected; break;
                    case 4: met = value > expected; break;
                    case 5: met = value >= expected; break;
                    default: break;
                    }
                    inner = met ? Holds::Yes : Holds::No;
                }
            }
            else if (obj::IsA(condition, L"BatesCharacterTriggerCompoundCondition") && depth < 4)
            {
                std::vector<UObject*> children;
                obj::ReadObjectArray(condition, L"Conditions", children);
                int64_t opType = 0;
                obj::ReadInt(condition, L"OpType", opType);
                int yes = 0, no = 0, maybe = 0;
                for (auto* child : children)
                {
                    switch (ConditionHolds(child, pawn, depth + 1))
                    {
                    case Holds::Yes: ++yes; break;
                    case Holds::No: ++no; break;
                    default: ++maybe; break;
                    }
                }
                switch (opType)
                {
                case 0: inner = no > 0 ? Holds::No : maybe > 0 ? Holds::Maybe : Holds::Yes; break;  // all met
                case 1: inner = yes > 0 ? Holds::Yes : maybe > 0 ? Holds::Maybe : Holds::No; break; // any met
                case 2: inner = no > 0 ? Holds::Yes : maybe > 0 ? Holds::Maybe : Holds::No; break;  // any not met
                case 3: inner = yes > 0 ? Holds::No : maybe > 0 ? Holds::Maybe : Holds::Yes; break; // none met
                default: break;
                }
            }
            if (inner == Holds::Maybe) return Holds::Maybe;
            return ((inner == Holds::Yes) != negate) ? Holds::Yes : Holds::No;
        }

        // The actions of a volume as the game would run them now, group by group: the flat
        // list older volumes keep counts as a group without a condition.
        std::vector<TriggerGroup> TriggerGroups(UObject* volume, UObject* pawn)
        {
            std::vector<TriggerGroup> groups;
            {
                TriggerGroup flat;
                std::vector<UObject*> actions;
                obj::ReadObjectArray(volume, L"Actions", actions);
                for (auto* action : actions)
                {
                    if (obj::IsLive(action)) flat.actions.push_back(ReadTriggerAction(action));
                }
                if (!flat.actions.empty()) groups.push_back(std::move(flat));
            }
            if (auto* groupsProp = obj::FindProperty(volume, L"ActionGroups"))
            {
                obj::ForEachArrayElement(volume, groupsProp,
                                         [&](void* element, FProperty* inner)
                                         {
                                             TriggerGroup group;
                                             UObject* condition = nullptr;
                                             if (auto* cond = obj::StructMember(inner, L"Condition")) obj::ReadObjectAt(element, cond, condition);
                                             group.condition = ConditionHolds(condition, pawn, 0);
                                             if (auto* list = obj::StructMember(inner, L"Actions"))
                                             {
                                                 obj::ForEachArrayElement(element, list,
                                                                          [&](void* item, FProperty* itemProp)
                                                                          {
                                                                              UObject* action = nullptr;
                                                                              if (obj::ReadObjectAt(item, itemProp, action) && obj::IsLive(action))
                                                                                  group.actions.push_back(ReadTriggerAction(action));
                                                                          });
                                             }
                                             groups.push_back(std::move(group));
                                         });
            }
            return groups;
        }

        // The actions of a volume spelled out for the log, with their fire counts and conditions.
        std::vector<std::wstring> ActionNotes(UObject* volume, UObject* pawn)
        {
            std::vector<std::wstring> notes;
            for (const auto& group : TriggerGroups(volume, pawn))
            {
                for (const auto& action : group.actions)
                {
                    std::wstring note = action.cls;
                    if (!action.firesLeft) note += L" [spent]";
                    if (group.condition == Holds::No) note += L" [its condition is not met]";
                    if (group.condition == Holds::Maybe) note += L" [condition unknown]";
                    notes.push_back(note);
                }
            }
            return notes;
        }

        // The volumes the scenes have waited on (scripts::HeldWaits), forgotten when the story moves on
        // or the objective changes.
        scripts::HeldWaits g_heldWaits;
        double g_heldForgottenAt = -1000.0;
        std::wstring g_heldObjective;

        // Whether an actor is among the targets now (listed or left out): such a one stays in the list
        // a little beyond the range a new one comes in at (ways::InRange).
        bool IsTarget(UObject* actor)
        {
            return std::any_of(g_targets.begin(), g_targets.end(), [&](const Target& t) { return t.actor == actor; });
        }

        // The ways on the level is waiting for right now: the trigger volumes that are on,
        // answer the character's walking, and move the scene on when entered, by the game's
        // own actions on them, by the trigger's script, or by a scene's machine that asks now
        // whether she stands in it (chapter one's range). They carry no marker and no name on
        // screen, and what a player sees is the ground leading to them, so they are offered
        // by where they are and never by what they are called.
        std::vector<Target> GatherWays(UObject* pawn, const Vec& here, double range)
        {
            // What the scenes waited on before the story moved on, or under another objective, waits
            // no longer (scripts::HeldWaits).
            const auto objective = hud::Objective();
            if (g_grid.storyMovedAt > g_heldForgottenAt || objective != g_heldObjective)
            {
                g_heldWaits.Forget();
                g_heldForgottenAt = g_grid.storyMovedAt;
                g_heldObjective = objective;
            }
            std::vector<Target> ways;
            std::vector<std::wstring> verdicts;
            std::wstring character;
            if (pawn) obj::ReadString(pawn, L"CharacterName", character);
            for (auto* volume : g_volumes)
            {
                if (!obj::IsLive(volume) || !obj::IsA(volume, L"BatesCharacterTriggerVolume")) continue;
                bool collision = false;
                obj::ReadBool(volume, L"bActorEnableCollision", collision);
                int64_t type = 0;
                int64_t activator = 0;
                obj::ReadInt(volume, L"TriggerVolumeType", type);
                obj::ReadInt(volume, L"ActivatorType", activator);
                const auto name = CoreName(volume);
                // Would the story move on if she walked in now? By the game's own actions,
                // their fire counts and their conditions, and by the level's script.
                const auto groups = TriggerGroups(volume, pawn);
                std::vector<std::wstring> actions;
                bool nativeStory = false, nativeLeft = false, nativeNow = false;
                for (const auto& group : groups)
                {
                    for (const auto& action : group.actions)
                    {
                        std::wstring note = action.cls;
                        if (action.story && !action.firesLeft) note += L" [spent]";
                        if (action.story && action.firesLeft && group.condition == Holds::No) note += L" [its condition is not met]";
                        if (action.story && action.firesLeft && group.condition == Holds::Maybe) note += L" [condition unknown]";
                        actions.push_back(note);
                        if (!action.story) continue;
                        nativeStory = true;
                        if (!action.firesLeft) continue;
                        nativeLeft = true;
                        if (group.condition != Holds::No) nativeNow = true;
                    }
                }
                const auto lua = name.empty() ? g_luaEvents.end() : g_luaEvents.find(name);
                const bool luaStory = lua != g_luaEvents.end() && lua->second.story;
                const bool spent = lua != g_luaEvents.end() && g_setOff.Spent(lua->second);
                // What the script does now, its comparisons weighed with the game's own variables
                // (scripts::StoryNow).
                const bool luaNow = luaStory && !spent && scripts::StoryNow(lua->second, ScriptVariable);
                const bool luaLive = luaNow;
                // A scene's machine asking at every update whether she stands in it, and moving
                // on once she does (scripts::Waits).
                using Kind = scripts::Waits::Kind;
                const auto waited = name.empty() ? scripts::Waits::Verdict{} : g_waits.Judge(g_playClock, name, character);
                const bool pollStory = waited.kind == Kind::Waiting || waited.kind == Kind::NotNow;
                // Waited on a while ago and not walked into since: still a way on (scripts::HeldWaits).
                const bool held = !name.empty() && waited.kind == Kind::NotNow && g_heldWaits.Held(name);
                const bool standing = (waited.kind == Kind::Waiting || held) && Overlapping(pawn, volume);
                const bool pollLive = !name.empty() && g_heldWaits.Waiting(name, waited.kind, standing);
                std::wstring why;
                if (!collision)
                    why = L"off";
                else if (type == 1)
                    why = L"for activities only";
                else if (activator != 0)
                    why = L"set off by the camera";
                else if (!nativeStory && !luaStory && !pollStory)
                    why = actions.empty() && lua == g_luaEvents.end() && waited.kind == Kind::None ? L"nothing happens there"
                                                                                                   : L"nothing of the story happens there";
                else if (!nativeNow && !luaLive && !pollLive)
                    why = nativeStory && !nativeLeft ? L"already set off"
                          : nativeStory              ? L"its condition is not met now"
                          : luaStory && !spent       ? L"its script's condition is not met now"
                          : luaStory                 ? L"already walked into"
                                                     : L"the scene does not wait on it now";
                const auto verdict =
                    (name.empty() ? obj::ObjectName(volume) : name) + L": " +
                    (why.empty() ? (held && pollLive ? L"a way on, the scene having waited on it and left off asking for now" : L"a way on") : why);
                if (g_wayVerdict[volume] != verdict)
                {
                    // A trigger the scene went through moves the story on, and what it sets
                    // going may open the ground (a gate, a character leaving the way); so does
                    // one a scene stopped waiting on once she was in it.
                    const bool wasWay = g_wayVerdict[volume].find(L": a way on") != std::wstring::npos;
                    if (((why == L"already set off" || why == L"already walked into") && !g_wayVerdict[volume].empty()) ||
                        (why == L"the scene does not wait on it now" && wasWay))
                        g_grid.storyMovedAt = gamethread::NowSeconds();
                    // Seen otherwise, now a way on: a scene has begun to wait on it (news, ways::IsNews).
                    if (!wasWay && !g_wayVerdict[volume].empty() && verdict.find(L": a way on") != std::wstring::npos)
                        g_turnedOnAt[volume] = gamethread::NowSeconds();
                    g_wayVerdict[volume] = verdict;
                    log::Info(L"explore: trigger {} ({}{}{})", verdict, str::Join(actions, L", "),
                              lua != g_luaEvents.end() ? L"; script " + str::Join(lua->second.calls, L" ") : std::wstring(),
                              waited.poll
                                  ? std::format(L"; asked about by {} {} ({})", waited.poll->machine, waited.poll->state, str::Join(waited.poll->calls, L" "))
                                  : std::wstring());
                }
                if (!why.empty()) continue;
                Target t;
                t.actor = volume;
                t.way = true;
                t.coreName = name;
                if (TriggerBox(volume, t.boxLow, t.boxHigh))
                {
                    t.hasBox = true;
                    t.position = Vec{(t.boxLow.x + t.boxHigh.x) / 2.0, (t.boxLow.y + t.boxHigh.y) / 2.0, (t.boxLow.z + t.boxHigh.z) / 2.0};
                    t.shaped = VolumeBox(volume, t.shape);
                    t.hasCollision = VolumeShape(volume, name.empty() ? obj::ObjectName(volume) : name, t.collision);
                    if (t.hasCollision) t.collision.Bounds(t.collisionLow, t.collisionHigh);
                }
                else if (!ActorLocation(volume, t.position))
                {
                    continue;
                }
                if (!ways::InRange(Distance(here, t.position), range, IsTarget(t.actor))) continue;
                // Two volumes standing at one spot are one way on.
                if (std::any_of(ways.begin(), ways.end(),
                                [&](const Target& w) { return FlatDistance(w.position, t.position) < 50.0 && std::fabs(w.position.z - t.position.z) < 100.0; }))
                {
                    if (g_wayNote[volume] != L"twin")
                    {
                        g_wayNote[volume] = L"twin";
                        log::Info(L"explore: trigger {} stands where another way on stands, and is left out", name.empty() ? obj::ObjectName(volume) : name);
                    }
                    continue;
                }
                t.label = locale::Mod(L"explore.wayon");
                // Inside by the engine's own overlap, which is what sets the volume off; the
                // box only says whether it lies a storey up or down.
                if (t.hasBox) InsideVolume(pawn, volume, t.boxLow, t.boxHigh, t.storey);
                t.inside = Overlapping(pawn, volume);
                ways.push_back(std::move(t));
            }
            // A scene that waits for her to walk on (scripts::WaitsToMove) while the game holds her
            // to a path: the path's end is the way on, walked along the path.
            if (pawn && g_path.set && obj::IsLive(g_pathSpline) && !character.empty() && ScenesWaitToMove(character))
            {
                UObject* component = PathSplineComponent();
                double length = 0.0;
                Vec end;
                if (component && g_pathSign != 0 && SplineLength(component, length) && SplineLocation(component, g_pathSign > 0 ? length : 0.0, end))
                {
                    Target t;
                    t.actor = g_pathSpline;
                    t.way = true;
                    t.hasBox = true;
                    t.boxLow = Vec{end.x - kPathEndHalf, end.y - kPathEndHalf, end.z - 200.0};
                    t.boxHigh = Vec{end.x + kPathEndHalf, end.y + kPathEndHalf, end.z + 200.0};
                    t.position = end;
                    t.label = locale::Mod(L"explore.wayon");
                    if (g_wayVerdict[g_pathSpline].empty())
                    {
                        g_wayVerdict[g_pathSpline] = L"the scene waits for her to walk on";
                        g_turnedOnAt[g_pathSpline] = gamethread::NowSeconds();
                        log::Info(L"explore: a scene waits for {} to walk on: the end of the path the game holds her to, {:.0f} cm off, is the way on", character,
                                  FlatDistance(here, end));
                    }
                    ways.push_back(std::move(t));
                }
            }
            else if (g_pathSpline && !g_wayVerdict[g_pathSpline].empty())
            {
                g_wayVerdict.erase(g_pathSpline);
            }
            return ways;
        }

        Route FindRoute(UObject* pawn, const Vec& here, const Target& target, std::vector<Vec>* road);
        const Vec& Aim(const Target& t);
        bool NavigationAvailable();
        bool ProjectToMesh(UObject* pawn, const Vec& point, Vec& out, const Vec& extent);
        double SweepClear(UObject* pawn, const Vec& from, const Vec& to, double radius, double halfHeight, std::wstring* hitWhat = nullptr,
                          Vec* reached = nullptr, UObject** hitComponent = nullptr);

        bool ReachedFrom(const grid::Flood& flood, const Target& t, grid::Cell& best);

        std::vector<Target> Gather(UObject* pawn, const Vec& here, double yaw, double now)
        {
            std::vector<Target> uses;
            const double range = cfg::Get().exploreRange * 100.0;
            ScanLevel(pawn, now);
            ScanLuaEvents(now);
            const auto offered = OfferedUseLocations(pawn);
            for (auto* actor : g_useLocations)
            {
                // A slot of the object array freed and reused holds something else now.
                if (!obj::IsLive(actor) || !obj::IsA(actor, L"BatesUseLocation")) continue;
                UseState state;
                if (!ReadUseState(actor, state)) continue;
                const bool forThisCharacter = ForThisCharacter(actor);
                // What the glint shows: a use location that is on. The game's own interact
                // test asks the linked activity as well, and the log says when they disagree.
                state.listed = state.enabled && forThisCharacter;
                UseState& known = g_useStates[actor];
                if (!known.known || known.listed != state.listed || known.enabled != state.enabled || known.activity != state.activity ||
                    known.sleeping != state.sleeping)
                {
                    std::wstring coreName;
                    const auto label = UseLocationLabel(actor, coreName);
                    log::Info(L"explore: use location \"{}\" ({}) is {}: enabled {}, activity {}, sleeping {}, in progress {}, for this character {}{}", label,
                              obj::ObjectName(actor), state.listed ? L"listed" : L"not listed", state.enabled, state.activity, state.sleeping, state.inProgress,
                              forThisCharacter, g_glintOf.count(actor) ? L", with a glint" : L", without a glint");
                    // Seen off, now on: the game has turned it on (news, ways::IsNews).
                    if (known.known && !known.listed && state.listed) g_turnedOnAt[actor] = gamethread::NowSeconds();
                    known = state;
                }
                if (!state.listed) continue;
                Target t;
                t.actor = actor;
                t.label = UseLocationLabel(actor, t.coreName);
                UObject* interaction = nullptr;
                if (obj::ReadObject(actor, L"InteractionActor", interaction) && obj::IsLive(interaction)) t.kind = obj::ClassName(interaction);
                t.glint = g_glintOf.count(actor) > 0;
                Vec low, high;
                if (TriggerBox(actor, low, high))
                {
                    t.hasBox = true;
                    t.boxLow = low;
                    t.boxHigh = high;
                    t.position = Vec{(low.x + high.x) / 2.0, (low.y + high.y) / 2.0, (low.z + high.z) / 2.0};
                }
                else if (!ActorLocation(actor, t.position))
                {
                    continue;
                }
                // The game places the character at its start arrow to use the thing.
                UObject* start = nullptr;
                if (obj::ReadObject(actor, L"StartLocation", start) && ComponentLocation(start, t.approach)) t.hasApproach = true;
                if (!ways::InRange(Distance(here, t.position), range, IsTarget(t.actor))) continue;
                // The place the game's press tests her in: her capsule overlapping its trigger mesh
                // (a use location is a character trigger volume), reached as a way on's is (Touches),
                // not about its start arrow: the pinboard of Hannah's room stands past an invisible
                // wall 108 cm from the nearest ground, and was never listed (2026-11-05).
                t.hasCollision = VolumeShape(actor, t.coreName.empty() ? obj::ObjectName(actor) : t.coreName, t.collision);
                if (t.hasCollision) t.collision.Bounds(t.collisionLow, t.collisionHigh);
                t.inside = std::find(offered.begin(), offered.end(), actor) != offered.end();
                uses.push_back(std::move(t));
            }

            // Twins of the same thing are listed once: two use locations with one label at one
            // spot, and the two sides of one door, which the level names by where each leads and
            // sets a step apart. The one the reach map has reached comes first, the nearer of those
            // first: the side of a door she stands on, which the room rule keeps, may lie a little
            // farther than the other side's when its spot is set back from the door.
            const auto twins = [](const Target& a, const Target& b)
            {
                const double apart = FlatDistance(a.position, b.position);
                return (a.label == b.label && apart < kTwinApart) || (!a.kind.empty() && a.kind == b.kind && apart < kDoorSidesApart);
            };
            std::vector<std::pair<bool, double>> keys(uses.size());
            for (size_t i = 0; i < uses.size(); ++i)
            {
                keys[i] = {false, Distance(here, uses[i].position)};
                const bool twinned = std::any_of(uses.begin(), uses.end(), [&](const Target& o) { return &o != &uses[i] && twins(o, uses[i]); });
                grid::Cell cell;
                if (twinned && g_reach.flood) keys[i].first = ReachedFrom(*g_reach.flood, uses[i], cell);
            }
            std::vector<size_t> order(uses.size());
            for (size_t i = 0; i < order.size(); ++i)
                order[i] = i;
            std::stable_sort(order.begin(), order.end(),
                             [&](size_t a, size_t b) { return keys[a].first != keys[b].first ? keys[a].first : keys[a].second < keys[b].second; });
            std::vector<Target> sorted;
            sorted.reserve(uses.size());
            for (const size_t i : order)
                sorted.push_back(std::move(uses[i]));
            uses = std::move(sorted);
            std::vector<Target> out;
            for (auto& u : uses)
            {
                if (std::any_of(out.begin(), out.end(), [&](const Target& o) { return twins(o, u); })) continue;
                out.push_back(std::move(u));
            }

            // The ways on, which are what a player walks toward when there is nothing left
            // to use. Those the walk to another listed target passes through are told apart
            // once the roads are known (ChooseListed).
            if (now - g_waysScannedAt > kWaysApart)
            {
                g_waysScannedAt = now;
                g_ways = GatherWays(pawn, here, range);
                std::erase_if(g_waysAnnounced,
                              [](UObject* way) { return std::none_of(g_ways.begin(), g_ways.end(), [&](const Target& t) { return t.actor == way; }); });
            }
            for (auto& way : g_ways)
            {
                if (!obj::IsLive(way.actor)) continue;
                Target t = way;
                if (t.hasBox) InsideVolume(pawn, t.actor, t.boxLow, t.boxHigh, t.storey);
                t.inside = Overlapping(pawn, t.actor);
                if (t.inside) g_setOff.Walked(t.coreName);
                out.push_back(std::move(t));
            }
            for (auto& t : out)
            {
                t.distance = Distance(here, t.position);
                t.bearing = BearingDegrees(here, t.position, yaw);
            }
            return out;
        }

        // The known targets keep their order and take the fresh readings; the new ones join
        // at the end, nearest first. Returns the new ones.
        std::vector<Target> Reconcile(std::vector<Target>& fresh)
        {
            std::vector<Target> kept;
            for (auto& old : g_targets)
            {
                auto it = std::find_if(fresh.begin(), fresh.end(), [&](const Target& f) { return f.actor == old.actor; });
                if (it == fresh.end()) continue;
                it->reached = old.reached;
                it->hasRoute = old.hasRoute;
                it->routeAt = old.routeAt;
                it->routeLength = old.routeLength;
                it->routeNext = old.routeNext;
                it->routeFrom = old.routeFrom;
                it->routeGoal = old.routeGoal;
                it->gridCorners = old.gridCorners;
                it->gridAt = old.gridAt;
                it->gridFrom = old.gridFrom;
                it->gridGoal = old.gridGoal;
                it->gridFailedAt = old.gridFailedAt;
                it->gridPartial = old.gridPartial;
                it->gridWalkable = old.gridWalkable;
                it->gridEnclosed = old.gridEnclosed;
                it->onRoadOf = old.onRoadOf;
                it->unreachable = old.unreachable;
                it->reach = old.reach;
                it->reachGeneration = old.reachGeneration;
                it->reachTried = old.reachTried;
                it->reachCells = old.reachCells;
                it->straightened = old.straightened;
                it->mapRoad = old.mapRoad;
                it->mapWalkable = old.mapWalkable;
                it->passed = old.passed;
                it->rowEpoch = old.rowEpoch;
                it->reachAtFreeze = old.reachAtFreeze;
                // Found only once a second with the roads (ChooseListed), and read at every look.
                it->pathReach = old.pathReach;
                it->offPath = old.offPath;
                it->unjudged = old.unjudged;
                it->room = old.room;
                kept.push_back(*it);
                it->actor = nullptr;
            }
            std::vector<Target> added;
            for (auto& f : fresh)
            {
                if (f.actor) added.push_back(f);
            }
            std::sort(added.begin(), added.end(), [](const Target& a, const Target& b) { return a.distance < b.distance; });
            g_targets = kept;
            g_targets.insert(g_targets.end(), added.begin(), added.end());
            return added;
        }

        std::vector<Vec> KnownRoad(UObject* pawn, const Vec& here, const Target& t, double halfHeight);
        double SpokenDistance(const Target& t);

        // What is listed: every use location and every way on the game would set off now,
        // but for three kinds kept back. A thing behind the character on the path she is held
        // to, which the game forbids for the moment. A way no walk can reach for now. And a
        // way on the road the walk to another listed target takes: that walk sets it off
        // anyway, so of a row of ways along one road only the farthest is listed. Roads are
        // judged from the farthest target in, and only the roads of targets that stay listed
        // count, since a way left out is not walked to. The chosen target stays listed.
        bool Judged(const Target& t, bool meshRoads);
        bool RoadSettled(const Target& t, bool meshRoads);
        bool JudgedByMap(const Target& t);
        bool ReachRunning();
        bool MeshHere(UObject* pawn, const Vec& here, double now);
        void LogList(const Vec& here);

        // The map ran out of cells within its bound: whatever it has not reached is closed off.
        bool MapProves()
        {
            if (!g_reach.flood || g_reach.doneAt < 0.0) return false;
            const grid::Flood& flood = *g_reach.flood;
            return flood.Current() == grid::Flood::State::Complete && !flood.RadiusReached() && flood.Expanded() >= kEnclosedMinCells;
        }

        // The room a thing lies in (rooms::ZoneAt): at the spot the character is put on to use it,
        // raised to her middle's height, else at its box's middle; found once for each count of the
        // rooms.
        int ZoneOfTarget(const Target& t)
        {
            const auto known = g_zoneOf.find(t.actor);
            if (known != g_zoneOf.end() && known->second.first == g_zonesGeneration) return known->second.second;
            const Vec point = t.hasApproach ? Vec{t.approach.x, t.approach.y, t.approach.z + rooms::kRoomPointRise} : t.position;
            const int zone = rooms::ZoneAt(g_zones, point);
            g_zoneOf[t.actor] = {g_zonesGeneration, zone};
            return zone;
        }

        // The room rule over the things to use (rooms::Judge): one the map has proven no walk gets
        // to without opening a door or climbing is another room's and is left out; the door to it is
        // listed, being reached. The list settles on the map as the ways do (ways::Settling), once the
        // map's cells are all counted into its rooms for the log; with no map at all a hold lists its
        // things after the settle's while, and where the navigation mesh gives the roads nothing is
        // left out.
        void JudgeRooms(bool meshRoads, double now)
        {
            const bool counted = g_reach.flood && g_reach.areaZones == g_zonesGeneration && g_reach.areaCounted >= g_reach.flood->Order().size();
            rooms::Facts base;
            if (meshRoads)
                base.settled = true;
            else if (g_reach.flood)
                base.settled = g_settle.Settled() && counted;
            else
                base.settled = !g_settle.Begun() && now - g_listSince >= ways::kSettleCap;
            base.running = ReachRunning();
            base.proves = !meshRoads && MapProves();
            for (auto& t : g_targets)
            {
                if (t.way) continue;
                rooms::Facts f = base;
                // A thing that came after the map was done is judged by it at its next slice
                // (PollReachMap): it waits that long, so that it is not said as news and taken
                // back a moment later.
                if (g_reach.flood && !ReachRunning() && !JudgedByMap(t) && t.reachTried != g_reach.generation) f.settled = false;
                f.reach = JudgedByMap(t) ? t.reach : 0;
                f.offered = t.inside;
                const int zone = ZoneOfTarget(t);
                const auto before = t.room;
                t.room = rooms::Judge(f, before);
                if (t.room == before) continue;
                const std::wstring where = zone >= 0 ? g_zones[static_cast<size_t>(zone)].name : std::wstring(L"no room");
                if (t.room == rooms::Verdict::Apart)
                    log::Info(L"explore: \"{}\" {} in {} is left out, another room's: the map found it {}", t.label, obj::ObjectName(t.actor), where,
                              f.reach == 3 ? L"beyond a door" : L"closed off");
                else if (before == rooms::Verdict::Apart)
                    log::Info(L"explore: \"{}\" {} in {} is listed again: {}", t.label, obj::ObjectName(t.actor), where,
                              f.offered      ? L"the game offers it"
                              : f.reach == 1 ? L"the map reaches it"
                                             : L"no map proves it closed off");
                else
                    log::Verbose(L"explore: \"{}\" {} in {} {}", t.label, obj::ObjectName(t.actor), where,
                                 t.room == rooms::Verdict::Waits ? L"waits for the reach map" : L"is in the area");
            }
        }

        void ChooseListed(UObject* pawn, const Vec& here, double now)
        {
            for (auto& t : g_targets)
            {
                t.behind = g_path.set && !t.reached && !AheadOnPath(here, Aim(t));
                // Off the path, or let off it: nothing is beside a path any more.
                if (!g_path.set || t.inside || t.reached) t.offPath = false;
            }
            // A way whose ground is not known yet is not listed: neither whether a walk gets there,
            // nor whether the road to another runs through it. And which ways the list keeps waits
            // for the reach map to settle it (ways::Settling): until the map has judged every target,
            // run out, or run its while, a nearer way may yet turn out to lie on the road to a farther
            // one (the woods' bird scare listed at 8 m for a second, then left out for the farther way
            // along its road; a trigger beside the lodge's totem announced as news and left out a
            // moment later for the road back to the lodge's door, the list having taken the roads an
            // earlier map left for this one's). Only this map's verdicts count. The first map of a
            // hold lists no way meanwhile, a later one leaves the list it found as it was, and the
            // things to use are listed at once.
            const bool meshRoads = MeshHere(pawn, here, now);
            if (meshRoads != g_meshRoadsBefore)
            {
                g_meshRoadsBefore = meshRoads;
                g_lister.Again(); // the roads come from elsewhere now
            }
            bool settlesNow = false;
            if (meshRoads)
            {
                g_settle.Clear();
            }
            else if (g_reach.flood)
            {
                const auto judged =
                    static_cast<size_t>(std::count_if(g_targets.begin(), g_targets.end(), [](const Target& t) { return RoadSettled(t, false); }));
                settlesNow = g_settle.Update(now, !ReachRunning(), judged, g_targets.size());
                if (settlesNow)
                    log::Info(L"explore: the reach map has settled the list in {:.1f} s: {} of {} targets judged{}", now - g_settle.StartedAt(), judged,
                              g_targets.size(),
                              !ReachRunning()              ? L", the map done"
                              : judged >= g_targets.size() ? L""
                                                           : L"; the rest wait for the map");
            }
            g_waysSettling = !g_settle.Settled();
            // The story moved on: every verdict is given anew, by a map begun since.
            if (g_grid.storyMovedAt > g_listerStoryAt)
            {
                g_listerStoryAt = g_grid.storyMovedAt;
                g_lister.NewHold();
                g_freezeAfter = static_cast<int64_t>(g_reach.generation);
                log::Verbose(L"explore: the story moved on: the list's verdicts are given anew");
            }
            // Between a hand-over and its first map nothing judges the roads: the list stands.
            const bool due = (meshRoads || g_reach.flood) && g_lister.Due(g_settle, settlesNow, now);
            const double radius = WalkingRadius(pawn);
            Vec middle;
            double halfHeight = 88.0;
            CapsuleSpan(pawn, middle, halfHeight);
            JudgeRooms(meshRoads, now);
            std::map<UObject*, std::vector<Vec>> roads;
            const auto roadOf = [&](const Target& t) -> const std::vector<Vec>&
            {
                auto it = roads.find(t.actor);
                if (it == roads.end()) it = roads.emplace(t.actor, KnownRoad(pawn, here, t, halfHeight)).first;
                return it->second;
            };
            if (due)
            {
                // A way the ground search found closed off (it reached every cell the character
                // can walk to, and none near the way) is not offered: no walk gets there for now,
                // as a way behind the path's walls, or past a wall a scene has yet to carry her
                // over. On a path the game holds her to, a way the path itself runs into is
                // reached by the path. A way not felt out yet is offered meanwhile.
                // The path ahead to its end, once, for every way it may run into.
                std::vector<Vec> pathAhead;
                if (g_path.set) pathAhead = PathRoadToEnd(here);
                // A thing beside the path is reached on it only within her width and the stray the
                // stick's cone allows over what is left of the path (ways::PathStray).
                double pathLeft = 0.0;
                for (size_t i = 1; i < pathAhead.size(); ++i)
                    pathLeft += FlatDistance(pathAhead[i - 1], pathAhead[i]);
                const double stray = ways::PathStray(pathLeft, std::max(std::fabs(g_path.angleLeft), std::fabs(g_path.angleRight)));
                std::map<UObject*, bool> wasUnreachable;
                for (auto& t : g_targets)
                {
                    wasUnreachable[t.actor] = t.unreachable;
                    t.pathReach =
                        t.way && t.hasBox && !t.behind && pathAhead.size() >= 2 && RoadEnters(t.actor, t.boxLow, t.boxHigh, pathAhead, radius, halfHeight);
                    const bool wasOffPath = t.offPath;
                    t.offPath = !t.way && t.hasBox && !t.behind && !t.reached && !t.inside && pathAhead.size() >= 2 &&
                                !RoadEnters(t.actor, t.boxLow, t.boxHigh, pathAhead, radius + stray, halfHeight);
                    if (t.offPath != wasOffPath)
                        log::Info(L"explore: \"{}\" {} {:.0f} cm off is {}", t.label, obj::ObjectName(t.actor), t.distance,
                                  t.offPath ? std::format(L"left out: the path the game holds her to passes it by over the {:.0f} cm of it left to walk", pathLeft)
                                            : std::wstring(L"listed again: the path runs into it, or holds her no longer"));
                    const bool reachable = !t.way || rooms::WayOffered(t.gridEnclosed, JudgedByMap(t) ? t.reach : 0, t.pathReach);
                    t.unreachable = !reachable;
                    if (t.unreachable != wasUnreachable[t.actor])
                        log::Info(L"explore: the way {} {:.0f} cm off is {}", obj::ObjectName(t.actor), t.distance,
                                  !t.unreachable   ? L"offered again: a way to it is open"
                                  : t.gridEnclosed ? L"not offered: the ground found no way to it"
                                                   : L"not offered: it lies beyond a door, another room's");
                }
            }
            // The row rule (ways::RowRule): of a row of ways along one road only the farthest is
            // listed, each judged against the roads of every use location, of the player's chosen
            // target (listed whatever the rules say), and of the ways farther along found to stay
            // listed. A thing the ground was proven closed off from (no door between) stays listed,
            // but its road ends at the ground nearest it, a dead end short of it: no walk gets there
            // through the way, chosen or not (the lodge's totem, chosen in the woods, hid the way on
            // beside the path and left only itself in the list).
            const auto playerChose = [](const Target& t) { return t.actor == g_selected && !g_autoChosen; };
            std::vector<ways::Entry> entries(g_targets.size());
            std::vector<UObject*> before(g_targets.size());
            for (size_t i = 0; i < g_targets.size(); ++i)
            {
                const Target& t = g_targets[i];
                auto& e = entries[i];
                e.way = t.way;
                e.hasBox = t.hasBox;
                e.distance = SpokenDistance(t);
                e.behind = t.behind;
                // A thing of another room, or one waiting for the map, is no road to weigh.
                const bool roomOut = !t.way && (t.room == rooms::Verdict::Apart || t.room == rooms::Verdict::Waits);
                e.unreachable = t.unreachable || roomOut || t.offPath;
                e.unjudged = t.unjudged;
                e.reached = t.reached;
                e.chosen = playerChose(t);
                e.closedOff = t.gridEnclosed || (JudgedByMap(t) && t.reach == 2) || (!t.way && t.room == rooms::Verdict::Apart);
                e.known = Judged(t, meshRoads);
                e.earlier = t.reach == 1 || t.reach == 3;
                e.passed = t.passed;
                e.epoch = t.rowEpoch;
                before[i] = t.onRoadOf;
                for (size_t j = 0; j < g_targets.size() && t.onRoadOf; ++j)
                {
                    if (g_targets[j].actor == t.onRoadOf) e.onRoadOf = static_cast<int>(j);
                }
                // The target whose road it lay on has gone: it stays out until the rule judges it
                // again, rather than coming back now and maybe going at the next judgement.
                if (t.onRoadOf && e.onRoadOf < 0)
                {
                    e.unjudged = true;
                    e.passed = false;
                }
                // The target whose road it lies on is left out now: its road counts no longer, and the
                // way is judged again, frozen list or not.
                if (e.onRoadOf >= 0 && g_targets[static_cast<size_t>(e.onRoadOf)].hidden && !playerChose(g_targets[static_cast<size_t>(e.onRoadOf)]))
                {
                    e.passed = false;
                    g_lister.Again();
                }
            }
            g_lister.Judge(entries, g_settle, due, meshRoads || g_reach.flood, now,
                           [&](size_t way, size_t other)
                           {
                               const auto& road = roadOf(g_targets[other]);
                               const Target& w = g_targets[way];
                               return road.size() >= 2 && RoadEnters(w.actor, w.boxLow, w.boxHigh, road, radius, halfHeight);
                           });
            for (size_t i = 0; i < g_targets.size(); ++i)
            {
                Target& t = g_targets[i];
                t.unjudged = entries[i].unjudged;
                t.passed = entries[i].passed;
                t.rowEpoch = entries[i].epoch;
                t.onRoadOf = entries[i].onRoadOf >= 0 ? g_targets[static_cast<size_t>(entries[i].onRoadOf)].actor : nullptr;
                if (!due || !t.way || before[i] == t.onRoadOf) continue;
                if (t.onRoadOf)
                {
                    const Target& o = g_targets[static_cast<size_t>(entries[i].onRoadOf)];
                    log::Info(L"explore: the way {} {:.0f} cm off is not offered: it lies on the road to \"{}\" {} {:.0f} cm off", obj::ObjectName(t.actor),
                              t.distance, o.label, obj::ObjectName(o.actor), o.distance);
                }
                else
                {
                    log::Info(L"explore: the way {} {:.0f} cm off is offered: no listed road runs through it", obj::ObjectName(t.actor), t.distance);
                }
            }
            for (auto& t : g_targets)
            {
                const bool roomOut = !t.way && (t.room == rooms::Verdict::Apart || t.room == rooms::Verdict::Waits);
                t.hidden =
                    (t.behind || t.offPath || t.unreachable || t.onRoadOf != nullptr || t.unjudged || roomOut) && !(t.actor == g_selected && !g_autoChosen);
            }
            // Its verdicts stand once the map it judges by has run out and the list has settled on it,
            // or the navigation mesh has given every road (ways::Lister::Freeze).
            const bool mapDone = g_reach.flood && !ReachRunning() && static_cast<int64_t>(g_reach.generation) > g_freezeAfter;
            if (!g_lister.Frozen() && g_settle.Settled() && (mapDone || (meshRoads && due)))
            {
                g_lister.Freeze();
                // What the map found out of reach then: reached by a later map, it reopens the list.
                for (auto& t : g_targets)
                    t.reachAtFreeze = JudgedByMap(t) ? t.reach : (t.gridEnclosed ? 2 : 0);
                log::Info(L"explore: the list's verdicts stand for this hold now, the map it judged by done");
            }
            LogList(here);
        }

        std::wstring g_listLogged;

        void LogList(const Vec& here)
        {
            std::vector<std::wstring> listed;
            std::vector<std::wstring> left;
            for (const auto& t : g_targets)
            {
                const auto name = std::format(L"{} {} at ({:.0f}, {:.0f}, {:.0f})", t.way ? L"way" : t.label, obj::ObjectName(t.actor), t.position.x,
                                              t.position.y, t.position.z);
                if (!t.hidden)
                {
                    listed.push_back(name);
                    continue;
                }
                const wchar_t* why = t.behind                          ? L"behind on the path"
                                     : t.offPath                       ? L"beside the path, never reached on it"
                                     : t.unreachable && t.way          ? (t.gridEnclosed ? L"closed off" : L"beyond a door")
                                     : t.onRoadOf                      ? L"on the road to another"
                                     : t.unjudged                      ? L"its ground not known yet"
                                     : t.room == rooms::Verdict::Apart ? L"another room's"
                                     : t.room == rooms::Verdict::Waits ? L"waiting for the reach map"
                                                                       : L"left out";
                left.push_back(name + L": " + why);
            }
            const auto key = str::Join(listed, L"; ") + L" | " + str::Join(left, L"; ");
            if (key == g_listLogged) return;
            g_listLogged = key;
            log::Info(L"explore: listed at ({:.0f}, {:.0f}, {:.0f}): [{}]; left out: [{}]", here.x, here.y, here.z, str::Join(listed, L"; "),
                      str::Join(left, L"; "));
        }

        // A search felt the way to it out, or failed, after this map was done without judging it
        // (a map cut short by its budget or its bound leaves the farther ground to the searches,
        // FeelOutNextTarget). Only a search past this map counts: what searches found under an
        // earlier one is no verdict of now.
        bool SearchedPastMap(const Target& t)
        {
            return g_reach.flood && g_reach.doneAt >= 0.0 && t.reachTried == g_reach.generation && std::max(t.gridAt, t.gridFailedAt) > g_reach.doneAt;
        }

        // Whether a target's way is known well enough to say whether it is listed: a use
        // location always is; a way once the ground has been felt out to it (the reach map, or a
        // search where the map stopped short), once the path the game holds her to is found to
        // run into it, or where the navigation mesh lies under the character.
        bool Judged(const Target& t, bool meshRoads)
        {
            return !t.way || RoadSettled(t, meshRoads) || SearchedPastMap(t);
        }

        // Whether a target's road is known, way or not: the mesh gives it, the reach map now running
        // or done judged it, or the path she is held to runs into it. What an earlier map or a search
        // left is no verdict of this map's: at the lodge's totem on 2026-10-28 those old roads counted
        // as known, and a way was said as news before the map reached the door whose road runs
        // through it.
        bool RoadSettled(const Target& t, bool meshRoads)
        {
            return meshRoads || JudgedByMap(t) || t.pathReach;
        }

        // Being there: a use location once the game offers it (its prompt is up), a way on
        // once the character stands inside its volume.
        bool AtTarget(const Target& t)
        {
            return t.inside;
        }

        Target* Selected()
        {
            for (auto& t : g_targets)
            {
                if (t.actor == g_selected) return &t;
            }
            return nullptr;
        }

        // ---- routes over the navigation mesh -----------------------------------------------

        UObject* NavigationSystem()
        {
            return obj::FindObject(L"/Script/NavigationSystem.Default__NavigationSystemV1");
        }

        // Whether the level has navigation data at all, for the log.
        bool NavigationDataPresent()
        {
            UObject* system = obj::FindFirstLive(L"NavigationSystemV1");
            if (!system) return false;
            std::vector<UObject*> set;
            obj::ReadObjectArray(system, L"NavDataSet", set);
            return std::any_of(set.begin(), set.end(), [](UObject* data) { return obj::IsLive(data); });
        }

        // Whether the navigation mesh gives the roads. It never does in this game: the levels build a
        // mesh only inside narrow bounds laid along the companions' own paths (chapter one's station
        // has eleven such corridors, the lodge's interior none), for the game's companions to walk,
        // not the ground the player walks. Asked for the roads from the station in chapter one
        // (2026-10-25), it answered with a stub of two points and no length for every thing listed,
        // and the list, taking those for roads, would have dropped its reach map wherever a corridor
        // lay under the character. The ground felt out with the character's own collision is the
        // one road everywhere.
        bool NavigationAvailable()
        {
            return false;
        }

        // Whether the navigation mesh gives the roads under the character: never (NavigationAvailable).
        bool g_meshHere = false;

        bool MeshHere(UObject*, const Vec&, double)
        {
            return g_meshHere;
        }

        void LogNavigationData()
        {
            UObject* system = obj::FindFirstLive(L"NavigationSystemV1");
            if (!system)
            {
                log::Info(L"explore: no navigation system in this level; the ways are straight lines");
                return;
            }
            UObject* main = nullptr;
            obj::ReadObject(system, L"MainNavData", main);
            std::vector<UObject*> set;
            obj::ReadObjectArray(system, L"NavDataSet", set);
            std::vector<std::wstring> names;
            for (auto* data : set)
            {
                if (!obj::IsLive(data)) continue;
                int64_t generation = -1;
                obj::ReadInt(data, L"RuntimeGeneration", generation);
                names.push_back(std::format(L"{}{} (runtime generation {})", obj::ObjectName(data), data == main ? L", the main one" : L"", generation));
            }
            const auto bounds = obj::FindAllLive(L"NavMeshBoundsVolume");
            log::Info(L"explore: navigation by {}: {}; {} navigation bounds volume(s)", obj::ObjectName(system),
                      names.empty() ? L"no navigation data" : str::Join(names, L"; "), bounds.size());
            // Whether a companions' corridor lies under the character, for the log only: the roads are
            // the ground felt out in any case (NavigationAvailable).
            UObject* pawn = Pawn();
            Vec here, ground;
            if (pawn && ActorLocation(pawn, here) && NavigationDataPresent())
            {
                if (ProjectToMesh(pawn, here, ground, Vec{150.0, 150.0, 250.0}))
                    log::Info(L"explore: a companions' corridor of the mesh lies under the character, {:.0f} cm below its middle; the ground is felt out all "
                              L"the same",
                              here.z - ground.z);
                else
                    log::Info(L"explore: no mesh within 1.5 m of the character; the ground is felt out");
            }
        }

        bool ProjectToMesh(UObject* pawn, const Vec& point, Vec& out, const Vec& extent)
        {
            UObject* nav = NavigationSystem();
            auto* fn = nav ? obj::FindFunction(nav, L"K2_ProjectPointToNavigation") : nullptr;
            if (!fn) return false;
            bool ok = false;
            obj::Call(
                nav, fn,
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (!prop) continue;
                        const auto name = prop->GetName();
                        if (name == L"WorldContextObject")
                            *static_cast<UObject**>(obj::ValuePtrAt(params, prop)) = pawn;
                        else if (name == L"Point")
                            WriteVec(obj::ValuePtrAt(params, prop), prop, point);
                        else if (name == L"QueryExtent")
                            WriteVec(obj::ValuePtrAt(params, prop), prop, extent);
                    }
                },
                [&](void* params)
                {
                    bool result = false;
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (!prop) continue;
                        const auto name = prop->GetName();
                        if (name == L"ReturnValue")
                            obj::ReadBoolAt(params, prop, result);
                        else if (name == L"ProjectedLocation")
                            ReadVec(obj::ValuePtrAt(params, prop), prop, out);
                    }
                    ok = result;
                });
            return ok;
        }

        // Where a road to a target is aimed: a use location at the spot the game starts the
        // interaction from, anything else where it stands.
        const Vec& Aim(const Target& t)
        {
            return t.hasApproach ? t.approach : t.position;
        }

        Route FindRoute(UObject* pawn, const Vec& here, const Target& target, std::vector<Vec>* road)
        {
            Route route;
            if (!NavigationAvailable()) return route;
            UObject* nav = NavigationSystem();
            auto* fn = nav ? obj::FindFunction(nav, L"FindPathToLocationSynchronously") : nullptr;
            if (!fn) return route;
            Vec end = Aim(target);
            Vec projected;
            const Vec half{(target.boxHigh.x - target.boxLow.x) / 2.0, (target.boxHigh.y - target.boxLow.y) / 2.0, (target.boxHigh.z - target.boxLow.z) / 2.0};
            if ((target.way && target.hasBox && ProjectToMesh(pawn, end, projected, half)) || ProjectToMesh(pawn, end, projected, Vec{250.0, 250.0, 120.0}) ||
                ProjectToMesh(pawn, end, projected, Vec{250.0, 250.0, 400.0}))
                end = projected;
            UObject* path = nullptr;
            obj::Call(
                nav, fn,
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (!prop) continue;
                        const auto name = prop->GetName();
                        if (name == L"WorldContextObject" || name == L"PathfindingContext")
                            *static_cast<UObject**>(obj::ValuePtrAt(params, prop)) = pawn;
                        else if (name == L"PathStart")
                            WriteVec(obj::ValuePtrAt(params, prop), prop, here);
                        else if (name == L"PathEnd")
                            WriteVec(obj::ValuePtrAt(params, prop), prop, end);
                    }
                },
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"ReturnValue") obj::ReadObjectAt(params, prop, path);
                    }
                });
            static std::map<UObject*, double> complained;
            const double nowSeconds = gamethread::NowSeconds();
            const auto complain = [&](const wchar_t* why)
            {
                double& at = complained[target.actor];
                if (nowSeconds - at < 10.0) return;
                at = nowSeconds;
                log::Verbose(L"explore: no road to \"{}\": {} (asked for ({:.0f}, {:.0f}, {:.0f}) from ({:.0f}, {:.0f}, {:.0f}))", target.label, why, end.x,
                             end.y, end.z, here.x, here.y, here.z);
            };
            if (!obj::IsLive(path))
            {
                complain(L"the navigation system gave no path");
                return route;
            }
            std::vector<Vec> points;
            obj::ForEachArrayElement(path, obj::FindProperty(path, L"PathPoints"),
                                     [&](void* element, FProperty* inner)
                                     {
                                         Vec p;
                                         if (ReadVec(obj::ValuePtrAt(element, inner), inner, p)) points.push_back(p);
                                     });
            if (points.size() < 2)
            {
                complain(L"the path has no points");
                return route;
            }
            if (road) *road = points;
            route.valid = true;
            route.goal = end;
            route.end = points.back();
            route.partial = obj::CallForBool(path, L"IsPartial");
            for (size_t i = 1; i < points.size(); ++i)
                route.length += Distance(points[i - 1], points[i]);
            route.next = points.back();
            for (size_t i = 1; i < points.size(); ++i)
            {
                if (FlatDistance(here, points[i]) > 100.0)
                {
                    route.next = points[i];
                    break;
                }
            }
            log::Verbose(L"explore: route to \"{}\": {} points, {:.0f} cm{}", target.label, points.size(), route.length, route.partial ? L", partial" : L"");
            return route;
        }

        // ---- feeling out the ground --------------------------------------------------------

        int LevelOf(double floorZ)
        {
            return static_cast<int>(std::floor(floorZ / kLevelSize));
        }

        // The cell of a spot over the ground, on the level of the floor given.
        grid::Cell CellOf(const Vec& p, double floorZ)
        {
            return {static_cast<int>(std::floor((p.x - g_grid.origin.x) / kCellSize)), static_cast<int>(std::floor((p.y - g_grid.origin.y) / kCellSize)),
                    LevelOf(floorZ)};
        }

        Place PlaceOf(grid::Cell c)
        {
            return {c.x, c.y, c.level};
        }

        // The floor of a cell, known once the cell was stepped onto or stood on.
        bool FloorAt(grid::Cell c, double& floor)
        {
            const auto known = g_grid.floors.find(PlaceOf(c));
            if (known == g_grid.floors.end()) return false;
            floor = known->second;
            return true;
        }

        // A cell's middle, at the height of the character's middle standing on its floor.
        Vec WorldOf(grid::Cell c)
        {
            double floor = g_grid.floorZ - g_grid.capsuleHalf;
            FloorAt(c, floor);
            // The cell the character stands in is where she stands: its middle may lie inside
            // the wall she stands against.
            if (g_grid.startKnown && c == g_grid.startCell) return Vec{g_grid.startMiddle.x, g_grid.startMiddle.y, floor + g_grid.capsuleHalf};
            return Vec{g_grid.origin.x + (c.x + 0.5) * kCellSize, g_grid.origin.y + (c.y + 0.5) * kCellSize, floor + g_grid.capsuleHalf};
        }

        // The capsule swept between cells stands on the floor lifted by a step, since the
        // character steps up anything lower than that, and reaches up to the top of the
        // character's own: its middle over a cell, and its half height.
        Vec SweepMiddleOf(grid::Cell c)
        {
            Vec middle = WorldOf(c);
            middle.z += g_grid.stepHeight / 2.0;
            return middle;
        }

        double SweepHalfHeight()
        {
            return std::max(25.0, g_grid.capsuleHalf - g_grid.stepHeight / 2.0);
        }

        // The steepest floor the character stands on, as its own movement has it: the
        // least z of a floor's normal (cos of the walkable angle).
        double WalkableFloorZ(UObject* pawn)
        {
            UObject* movement = nullptr;
            double z = 0.71;
            if (pawn && obj::ReadObject(pawn, L"CharacterMovement", movement) && obj::IsLive(movement)) obj::ReadFloat(movement, L"WalkableFloorZ", z);
            return std::clamp(z, 0.3, 1.0);
        }

        // The most the character steps up without a climb, as its own movement has it.
        double StepHeight(UObject* pawn)
        {
            UObject* movement = nullptr;
            double step = 45.0;
            if (pawn && obj::ReadObject(pawn, L"CharacterMovement", movement) && obj::IsLive(movement)) obj::ReadFloat(movement, L"MaxStepHeight", step);
            return std::clamp(step, 10.0, 100.0);
        }

        // Whether a component the sweep met is another character's, or a thing she carries: after the
        // walk-and-talk to the guest cabin on 2026-11-04 the scene ordered Jessica to the generator to
        // wait for Mike, and the map stopped 2.5 m short of the generator, at its doorway, though the
        // way had been clear a minute before; the generator, the very thing the next objective named,
        // left the list. Said once for each character met so.
        std::set<std::wstring> g_charactersMet;

        bool OtherCharacter(UObject* pawn, UObject* component)
        {
            if (!obj::IsLive(component)) return false;
            UObject* owner = obj::Outer(component);
            UObject* who = nullptr;
            if (obj::IsLive(owner) && obj::IsA(owner, L"BatesCharacter"))
                who = owner;
            else if (obj::IsLive(owner))
            {
                UObject* parent = nullptr;
                obj::CallReturn(owner, L"GetAttachParentActor", [&](void* params, FProperty* returnValue) { obj::ReadObjectAt(params, returnValue, parent); });
                if (obj::IsLive(parent) && obj::IsA(parent, L"BatesCharacter")) who = parent;
            }
            if (!who || who == pawn) return false;
            const auto name = obj::ObjectName(who);
            if (g_charactersMet.insert(name).second)
                log::Info(L"explore: a sweep met {} ({} of {}), another character, and takes the ground beyond her for free", name, obj::ObjectName(component),
                          obj::ObjectName(owner));
            return true;
        }

        // ---- doors as walls -----------------------------------------------------------------

        // A door's leaf is a wall of the ground. The game passes a door only by its interaction
        // (ABatesInteractionDoor), and the leaf is a prop the interaction binds (BoundObjects) whose
        // collision the game turns off while the prop sleeps (UBatesSleepComponent::ApplySleepSettings
        // calls AActor::SetActorEnableCollision). From the lodge's garage on 2026-11-04 the map swept
        // the door to the front vestibule as free and reached the vestibule and the hall beyond, while
        // Chris, once there, stood at the shut door with the stick pushed until he opened it. The leaf
        // is placed from the prop's physics bodies (its PhysicsAsset: for the lodge's interior door
        // one box of 128 by 7.5 by 236 cm on the hinge's bone) at their bones' places in the world as
        // the map begins, so that a door standing open is out of its doorway and a shut one fills it.
        struct DoorLeaf
        {
            std::wstring name; // the interaction's
            UObject* prop = nullptr;
            shapes::Shape shape;
            Vec low;
            Vec high;
        };
        std::vector<DoorLeaf> g_doorLeaves;
        std::wstring g_doorLeavesLogged;

        // The props an interaction binds: the keys of its BoundObjects, a map of soft pointers to
        // binding data whose elements the map property lays out, the soft pointer's weak half at +0.
        std::vector<UObject*> BoundProps(UObject* interaction)
        {
            std::vector<UObject*> props;
            auto* prop = obj::FindProperty(interaction, L"BoundObjects");
            if (!prop || obj::PropertyTypeName(prop) != L"MapProperty") return props;
            const int32_t stride = static_cast<RC::Unreal::FMapProperty*>(prop)->GetMapLayout().SetLayout.Size;
            const auto* map = static_cast<const uint8_t*>(obj::ValuePtr(interaction, prop));
            if (!map || stride <= 0 || stride > 0x400) return props;
            const auto* elements = *reinterpret_cast<const uint8_t* const*>(map);
            const int32_t count = *reinterpret_cast<const int32_t*>(map + 0x08);
            const int32_t maxBits = *reinterpret_cast<const int32_t*>(map + 0x2c);
            if (!elements || count <= 0 || count > 64) return props;
            const auto* bits = maxBits > 128 ? *reinterpret_cast<const uint32_t* const*>(map + 0x20) : reinterpret_cast<const uint32_t*>(map + 0x10);
            if (!bits) return props;
            for (int32_t i = 0; i < count; ++i)
            {
                if ((bits[i / 32] & (1u << (i % 32))) == 0) continue; // a free slot
                const auto* weak = reinterpret_cast<const RC::Unreal::FWeakObjectPtr*>(elements + static_cast<size_t>(i) * static_cast<size_t>(stride));
                UObject* object = weak->Get();
                if (obj::IsLive(object) && obj::IsA(object, L"BatesBindableProp")) props.push_back(object);
            }
            return props;
        }

        // A bone's place in the world, by the component's own GetSocketTransform.
        bool BonePose(UObject* component, void* boneName, shapes::Pose& pose)
        {
            auto* fn = obj::IsLive(component) ? obj::FindFunction(component, L"GetSocketTransform") : nullptr;
            if (!fn || !boneName) return false;
            bool placed = false;
            obj::Call(
                component, fn,
                [&](void* params)
                {
                    for (auto* p : fn->ForEachProperty())
                    {
                        if (p && p->GetName() == L"InSocketName") std::memcpy(obj::ValuePtrAt(params, p), boneName, sizeof(RC::Unreal::FName));
                    }
                },
                [&](void* params)
                {
                    for (auto* p : fn->ForEachProperty())
                    {
                        if (p && p->GetName() == L"ReturnValue") placed = ReadPose(obj::ValuePtrAt(params, p), p, pose);
                    }
                });
            return placed;
        }

        // A prop's collision as its physics bodies stand now: each body's pieces on its bone.
        shapes::Shape PropBodies(UObject* prop)
        {
            shapes::Shape shape;
            UObject* component = nullptr;
            if (!obj::ReadObject(prop, L"SkeletalMeshComponent", component) || !obj::IsLive(component)) return shape;
            // The component's own physics asset, else its mesh's.
            UObject* physics = nullptr;
            obj::CallReturn(component, L"GetPhysicsAsset", [&](void* params, FProperty* returnValue) { obj::ReadObjectAt(params, returnValue, physics); });
            if (!obj::IsLive(physics)) obj::ReadObject(component, L"PhysicsAssetOverride", physics);
            for (const wchar_t* member : {L"SkinnedAsset", L"SkeletalMeshAsset", L"SkeletalMesh"})
            {
                UObject* mesh = nullptr;
                if (!obj::IsLive(physics) && obj::ReadObject(component, member, mesh) && obj::IsLive(mesh)) obj::ReadObject(mesh, L"PhysicsAsset", physics);
            }
            if (!obj::IsLive(physics)) return shape;
            std::vector<UObject*> bodies;
            obj::ReadObjectArray(physics, L"SkeletalBodySetups", bodies);
            for (UObject* body : bodies)
            {
                if (!obj::IsLive(body)) continue;
                auto* boneProp = obj::FindProperty(body, L"BoneName");
                shapes::Pose pose;
                if (!boneProp || !BonePose(component, obj::ValuePtr(body, boneProp), pose)) continue;
                ReadAggGeom(body, pose, shape);
            }
            return shape;
        }

        // The leaves of every door the level's use locations lead to, read as a map begins.
        void ReadDoorLeaves()
        {
            g_doorLeaves.clear();
            std::set<UObject*> doors;
            std::vector<std::wstring> lines;
            for (auto* use : g_useLocations)
            {
                UObject* interaction = nullptr;
                if (!obj::IsLive(use) || !obj::ReadObject(use, L"InteractionActor", interaction) || !obj::IsLive(interaction) ||
                    !obj::IsA(interaction, L"BatesInteractionDoor") || !doors.insert(interaction).second)
                    continue;
                std::wstring name = CoreName(interaction);
                if (name.empty()) name = obj::ObjectName(interaction);
                const auto props = BoundProps(interaction);
                bool any = false;
                for (UObject* prop : props)
                {
                    DoorLeaf leaf;
                    leaf.name = name;
                    leaf.prop = prop;
                    leaf.shape = PropBodies(prop);
                    if (leaf.shape.Empty()) continue;
                    leaf.shape.Bounds(leaf.low, leaf.high);
                    lines.push_back(std::format(L"{} ({} within ({:.0f}, {:.0f}, {:.0f}) to ({:.0f}, {:.0f}, {:.0f}))", name, obj::ObjectName(prop), leaf.low.x,
                                                leaf.low.y, leaf.low.z, leaf.high.x, leaf.high.y, leaf.high.z));
                    g_doorLeaves.push_back(std::move(leaf));
                    any = true;
                }
                if (!any) lines.push_back(std::format(L"{} (no leaf of its own: {} bound prop(s))", name, props.size()));
            }
            const auto text = lines.empty() ? std::wstring(L"none") : str::Join(lines, L"; ");
            if (text == g_doorLeavesLogged) return;
            g_doorLeavesLogged = text;
            log::Info(L"explore: the doors' leaves stand as walls: {}", text);
            for (const auto& leaf : g_doorLeaves)
            {
                for (const auto& line : ground::VolumeLines(str::WideToUtf8(L"door:" + leaf.name), leaf.shape))
                    g_record.Line(line);
            }
        }

        // Whether the character's capsule, swept between two middles, runs into a door's leaf: at
        // half way or where it ends, so that a step away from a leaf she stands against is free.
        bool DoorLeafBetween(const Vec& from, const Vec& to, double radius, double halfHeight)
        {
            for (const auto& leaf : g_doorLeaves)
            {
                if (std::max(from.x, to.x) < leaf.low.x - radius || std::min(from.x, to.x) > leaf.high.x + radius ||
                    std::max(from.y, to.y) < leaf.low.y - radius || std::min(from.y, to.y) > leaf.high.y + radius ||
                    std::max(from.z, to.z) + halfHeight < leaf.low.z || std::min(from.z, to.z) - halfHeight > leaf.high.z)
                    continue;
                if (shapes::SweepTouches(leaf.shape, from, to, radius, halfHeight)) return true;
            }
            return false;
        }

        // ---- the game's guidance passages --------------------------------------------------

        bool FloorFrom(UObject* pawn, grid::Cell column, int level, double& floor);

        // A passage's entry point on the floor: its arrow's place (EntryPointA or EntryPointB), on the
        // floor of the arrow's column, which the arrow stands on or a little below.
        bool PassageEnd(UObject* pawn, UObject* passage, const wchar_t* member, Vec& end)
        {
            UObject* arrow = nullptr;
            Vec at;
            if (!obj::ReadObject(passage, member, arrow) || !obj::IsLive(arrow) || !ComponentLocation(arrow, at)) return false;
            const grid::Cell column = CellOf(at, at.z);
            double floor = 0.0;
            if (!FloorFrom(pawn, column, LevelOf(at.z), floor)) return false;
            end = Vec{at.x, at.y, floor};
            return true;
        }

        ways::SweepHit PassageSweep(UObject* pawn, const Vec& from, const Vec& to);

        // Where the character's capsule fits in a passage too tight for every way tried, written to the
        // ground record once a session for each such passage, so that the doorway can be worked out
        // from the record: rows every kScanAlong cm from 10 cm before its near end to 10 cm past its far
        // one, each kScanAcross cm across from 40 cm left of its line to 40 right, '1' where the capsule
        // stands free and '0' where it does not.
        constexpr double kScanAlong = 5.0;
        constexpr double kScanAcross = 2.0;
        std::set<std::wstring> g_passagesScanned;

        void ScanPassage(UObject* pawn, const std::wstring& name, const Vec& from, const Vec& to)
        {
            if (!g_passagesScanned.insert(name).second) return;
            const double dx = to.x - from.x;
            const double dy = to.y - from.y;
            const double length = std::hypot(dx, dy);
            if (length < 1.0) return;
            const double ux = dx / length, uy = dy / length;
            const double rx = -uy, ry = ux;
            int rows = 0;
            for (double along = -10.0; along <= length + 10.0; along += kScanAlong)
            {
                std::string bits;
                for (double across = -40.0; across <= 40.0 + 1e-9; across += kScanAcross)
                {
                    const double f = std::clamp(along / length, 0.0, 1.0);
                    const Vec p{from.x + ux * along + rx * across, from.y + uy * along + ry * across, from.z + (to.z - from.z) * f};
                    const Vec q{p.x + ux, p.y + uy, p.z};
                    const auto hit = PassageSweep(pawn, p, q);
                    bits += hit.hit ? '0' : '1';
                }
                g_record.Line(std::format("K {} {:.1f} {:.1f} {:.1f} {:.1f} {:.1f} {:.1f} {:.1f} {}", str::WideToUtf8(name), from.x, from.y, from.z, to.x, to.y,
                                          to.z, along, bits));
                ++rows;
            }
            log::Info(L"explore: the passage {} is too tight for every way tried; a map of where her capsule fits in it ({} rows) is in the ground record",
                      name, rows);
        }

        // Each passage walked with the character's capsule as her movement takes her through a doorway
        // (ways::PassageWalk), its ends at the height of her middle on their floors; free when she gets
        // to the far end and no door's leaf stands across the way she took. Read as a map begins, after
        // the doors' leaves; a passage whose ends, and the leaves, are as they were keeps its verdict.
        struct PassageVerdict
        {
            std::wstring key;
            ways::Passage passage;
            std::wstring line;
        };
        std::map<std::wstring, PassageVerdict> g_passageVerdicts;

        void ReadPassages(UObject* pawn)
        {
            g_passages.clear();
            std::vector<std::wstring> lines;
            for (UObject* actor : g_passageActors)
            {
                if (!obj::IsLive(actor)) continue;
                ways::Passage p;
                std::wstring name = CoreName(actor);
                if (name.empty()) name = obj::ObjectName(actor);
                p.name = str::WideToUtf8(name);
                // The scenes switch a passage off as they do any trigger volume, with its collision
                // (UBatesCharacter_DisableTriggerVolumeTriggerAction: AActor::SetActorEnableCollision),
                // and the game guides no one through one switched off.
                bool on = true;
                obj::ReadBool(actor, L"bActorEnableCollision", on);
                if (!on)
                {
                    lines.push_back(std::format(L"{} (switched off)", name));
                    continue;
                }
                if (!PassageEnd(pawn, actor, L"EntryPointA", p.a) || !PassageEnd(pawn, actor, L"EntryPointB", p.b))
                {
                    lines.push_back(std::format(L"{} (its ends not on a floor known)", name));
                    continue;
                }
                const double rise = g_grid.capsuleHalf + g_grid.stepHeight / 2.0;
                const Vec from{p.a.x, p.a.y, p.a.z + rise};
                const Vec to{p.b.x, p.b.y, p.b.z + rise};
                const auto key = std::format(L"{:.0f} {:.0f} {:.0f} {:.0f} {:.0f} {:.0f} {:.0f} {}", p.a.x, p.a.y, p.a.z, p.b.x, p.b.y, p.b.z, g_grid.radius,
                                             g_doorLeavesLogged);
                const auto known = g_passageVerdicts.find(name);
                if (known != g_passageVerdicts.end() && known->second.key == key)
                {
                    lines.push_back(known->second.line);
                    g_passages.push_back(known->second.passage);
                    continue;
                }
                const auto way = ways::PassageWalk([&](const Vec& f, const Vec& t) { return PassageSweep(pawn, f, t); }, from, to);
                const double length = std::max(1.0, FlatDistance(from, to));
                const double rx = -(to.y - from.y) / length, ry = (to.x - from.x) / length;
                const Vec wayFrom{from.x + rx * way.shift, from.y + ry * way.shift, from.z};
                const Vec wayTo{to.x + rx * way.shift, to.y + ry * way.shift, to.z};
                const bool leaf = way.passed && DoorLeafBetween(wayFrom, wayTo, g_grid.radius, SweepHalfHeight());
                p.free = way.passed && !leaf;
                std::wstring how;
                if (p.free)
                    how = std::format(L"the capsule passes{}{}",
                                      way.shift == 0.0
                                          ? std::wstring(L" on its line")
                                          : std::format(L" {:.0f} cm to the {} of its line", std::fabs(way.shift), way.shift > 0.0 ? L"right" : L"left"),
                                      way.touches > 0 ? std::format(L", sliding along {} surface(s)", way.touches) : std::wstring());
                else if (leaf)
                    how = L"a door's leaf stands across it";
                else
                    how = std::format(L"too tight every way tried: on its line the capsule got to ({:.0f}, {:.0f}), {:.0f} cm short", way.reached.x,
                                      way.reached.y, FlatDistance(way.reached, to));
                const auto line =
                    std::format(L"{} from ({:.0f}, {:.0f}, {:.0f}) to ({:.0f}, {:.0f}, {:.0f}): {}", name, p.a.x, p.a.y, p.a.z, p.b.x, p.b.y, p.b.z, how);
                lines.push_back(line);
                g_passageVerdicts[name] = PassageVerdict{key, p, line};
                if (!way.passed) ScanPassage(pawn, name, from, to);
                g_passages.push_back(std::move(p));
            }
            const auto text = lines.empty() ? std::wstring(L"none") : str::Join(lines, L"; ");
            const bool changed = text != g_passagesLogged;
            if (changed)
            {
                g_passagesLogged = text;
                log::Info(L"explore: the game's guidance passages: {}", text);
            }
            // The record has them again under a new grid's anchor, and whenever they change.
            if (changed || FlatDistance(g_passagesRecordedAt, g_grid.origin) > 1.0)
            {
                g_passagesRecordedAt = g_grid.origin;
                for (const auto& p : g_passages)
                    g_record.Line(ground::PassageLine(p));
            }
        }

        // The links the passages give a cell of the ground (ways::PassageLinks): to the cell of the far
        // end, on its floor.
        void PassageLinksOf(grid::Cell from, std::vector<grid::Link>& out)
        {
            UObject* pawn = Pawn();
            double floor = 0.0;
            if (g_passages.empty() || !pawn || !FloorAt(from, floor)) return;
            const Vec world = WorldOf(from);
            ways::PassageLinks(
                g_passages, Vec{world.x, world.y, floor}, kCellSize,
                [&](const Vec& end, grid::Cell& cell, Vec& cellMiddle)
                {
                    cell = CellOf(end, end.z);
                    double f = 0.0;
                    if (!FloorAt(cell, f))
                    {
                        if (!FloorFrom(pawn, cell, cell.level, f)) return false;
                        cell.level = LevelOf(f);
                    }
                    const Vec w = WorldOf(cell);
                    cellMiddle = Vec{w.x, w.y, f};
                    return true;
                },
                out);
        }

        // ---- the engine's traces, asked many times a frame ---------------------------------

        // A trace of the engine's (KismetSystemLibrary's line and capsule traces by profile) with
        // its function, the places of its parameters and of the hit's members, and the profile's
        // name found once: a call then costs the engine's own work, not a walk of the function's
        // parameters by name each time, which is what most of a step's time went on. Vectors
        // are written and read as the three doubles they are in this engine, which preparing
        // checks.
        struct PreparedTrace
        {
            UObject* library = nullptr;
            RC::Unreal::UFunction* fn = nullptr;
            size_t size = 0;
            int32_t world = -1, start = -1, end = -1, radius = -1, half = -1, profile = -1, ignoreSelf = -1, ret = -1, hit = -1;
            int32_t ignore = -1; // ActorsToIgnore, an array of actors
            int32_t hitDistance = -1, hitPenetrating = -1, hitDepth = -1, hitNormal = -1, hitImpactNormal = -1, hitLocation = -1, hitComponent = -1;
            FProperty* hitProp = nullptr;
            FProperty* penetratingProp = nullptr; // a bit of a byte the hit shares with others
            RC::Unreal::FName pawn;
            std::vector<uint8_t> buffer;
            bool failed = false;
        };
        PreparedTrace g_lineTrace;
        PreparedTrace g_capsuleTrace;

        // Where a member of a parameter struct lies within the parameters, or -1.
        int32_t MemberOffset(FProperty* structProp, int32_t structOffset, const wchar_t* name)
        {
            FProperty* member = structProp ? obj::StructMember(structProp, name) : nullptr;
            return member ? structOffset + member->GetOffset_Internal() : -1;
        }

        bool VectorOfDoubles(FProperty* structProp)
        {
            static const wchar_t* const axes[] = {L"X", L"Y", L"Z"};
            for (int i = 0; i < 3; ++i)
            {
                FProperty* member = structProp ? obj::StructMember(structProp, axes[i]) : nullptr;
                if (!member || obj::PropertyTypeName(member) != L"DoubleProperty" || member->GetOffset_Internal() != i * 8) return false;
            }
            return true;
        }

        bool Prepare(PreparedTrace& t, const wchar_t* function)
        {
            if (t.fn && obj::IsLive(t.library)) return true;
            if (t.failed) return false;
            t = PreparedTrace{};
            t.library = obj::FindObject(L"/Script/Engine.Default__KismetSystemLibrary");
            t.fn = t.library ? obj::FindFunction(t.library, function) : nullptr;
            if (!t.fn) return false;
            const int32_t size = t.fn->GetPropertiesSize();
            if (size <= 0 || size > 4096) return false;
            t.size = static_cast<size_t>(size);
            FProperty* startProp = nullptr;
            FProperty* endProp = nullptr;
            for (auto* prop : t.fn->ForEachProperty())
            {
                if (!prop) continue;
                const auto name = prop->GetName();
                const int32_t at = prop->GetOffset_Internal();
                if (name == L"WorldContextObject")
                    t.world = at;
                else if (name == L"Start")
                {
                    t.start = at;
                    startProp = prop;
                }
                else if (name == L"End")
                {
                    t.end = at;
                    endProp = prop;
                }
                else if (name == L"Radius")
                    t.radius = at;
                else if (name == L"HalfHeight")
                    t.half = at;
                else if (name == L"ProfileName")
                    t.profile = at;
                else if (name == L"bIgnoreSelf")
                    t.ignoreSelf = at;
                else if (name == L"ActorsToIgnore" && obj::PropertyTypeName(prop) == L"ArrayProperty")
                    t.ignore = at;
                else if (name == L"ReturnValue")
                    t.ret = at;
                else if (name == L"OutHit")
                {
                    t.hit = at;
                    t.hitProp = prop;
                }
            }
            t.hitDistance = MemberOffset(t.hitProp, t.hit, L"Distance");
            t.hitPenetrating = MemberOffset(t.hitProp, t.hit, L"bStartPenetrating");
            t.penetratingProp = t.hitProp ? obj::StructMember(t.hitProp, L"bStartPenetrating") : nullptr;
            if (t.penetratingProp && obj::PropertyTypeName(t.penetratingProp) != L"BoolProperty") t.penetratingProp = nullptr;
            t.hitDepth = MemberOffset(t.hitProp, t.hit, L"PenetrationDepth");
            t.hitNormal = MemberOffset(t.hitProp, t.hit, L"Normal");
            t.hitImpactNormal = MemberOffset(t.hitProp, t.hit, L"ImpactNormal");
            t.hitLocation = MemberOffset(t.hitProp, t.hit, L"Location");
            t.hitComponent = MemberOffset(t.hitProp, t.hit, L"Component");
            const bool vectors = VectorOfDoubles(startProp) && VectorOfDoubles(endProp) && t.hitProp &&
                                 VectorOfDoubles(obj::StructMember(t.hitProp, L"Normal")) && VectorOfDoubles(obj::StructMember(t.hitProp, L"Location"));
            if (t.world < 0 || t.start < 0 || t.end < 0 || t.profile < 0 || t.ignoreSelf < 0 || t.ret < 0 || t.hit < 0 || t.hitDistance < 0 ||
                !t.penetratingProp || t.hitNormal < 0 || t.hitLocation < 0 || !vectors)
            {
                log::Error(L"explore: the engine's {} is not laid out as the mod expects, so the ground cannot be felt out", function);
                t.failed = true;
                t.fn = nullptr;
                return false;
            }
            t.pawn = RC::Unreal::FName(L"Pawn", RC::Unreal::FNAME_Add);
            t.buffer.assign(t.size + 32, 0);
            log::Info(L"explore: the engine's {} {}", function,
                      t.ignore >= 0 ? L"takes the other characters to leave out of it (ActorsToIgnore)" : L"has no list of actors to leave out");
            return true;
        }

        void PutVec(uint8_t* params, int32_t at, const Vec& v)
        {
            auto* d = reinterpret_cast<double*>(params + at);
            d[0] = v.x;
            d[1] = v.y;
            d[2] = v.z;
        }

        Vec GetVec(const uint8_t* params, int32_t at)
        {
            const auto* d = reinterpret_cast<const double*>(params + at);
            return Vec{d[0], d[1], d[2]};
        }

        // What a trace met: whether it hit, and the hit's members the ground needs.
        struct TraceHit
        {
            bool hit = false;
            bool penetrating = false;
            double distance = 0.0;
            double depth = 0.0;
            Vec location;
            Vec normal;
            Vec impactNormal{0.0, 0.0, 1.0};
            UObject* component = nullptr;
        };

        // Runs a prepared trace from one point to another, a capsule when `radius` is above 0.
        bool RunTrace(PreparedTrace& t, UObject* pawn, const Vec& from, const Vec& to, double radius, double halfHeight, TraceHit& out)
        {
            uint8_t* params = t.buffer.data();
            std::fill(t.buffer.begin(), t.buffer.end(), uint8_t{0});
            *reinterpret_cast<UObject**>(params + t.world) = pawn;
            PutVec(params, t.start, from);
            PutVec(params, t.end, to);
            if (t.radius >= 0) *reinterpret_cast<float*>(params + t.radius) = static_cast<float>(radius);
            if (t.half >= 0) *reinterpret_cast<float*>(params + t.half) = static_cast<float>(halfHeight);
            *reinterpret_cast<RC::Unreal::FName*>(params + t.profile) = t.pawn;
            *reinterpret_cast<bool*>(params + t.ignoreSelf) = true;
            // The other characters (ScanOthers), as the array the engine takes by reference and only
            // reads: its data, its count and its room.
            struct ActorArray
            {
                UObject* const* data;
                int32_t count;
                int32_t room;
            };
            const auto& others = OthersToIgnore();
            if (t.ignore >= 0 && !others.empty())
                *reinterpret_cast<ActorArray*>(params + t.ignore) =
                    ActorArray{others.data(), static_cast<int32_t>(others.size()), static_cast<int32_t>(others.size())};
            t.library->ProcessEvent(t.fn, params);
            if (t.ignore >= 0) *reinterpret_cast<ActorArray*>(params + t.ignore) = ActorArray{nullptr, 0, 0};
            out = TraceHit{};
            out.hit = *reinterpret_cast<const bool*>(params + t.ret);
            if (!out.hit) return true;
            out.distance = *reinterpret_cast<const float*>(params + t.hitDistance);
            out.penetrating = static_cast<RC::Unreal::FBoolProperty*>(t.penetratingProp)->GetPropertyValue(params + t.hitPenetrating);
            if (t.hitDepth >= 0) out.depth = *reinterpret_cast<const float*>(params + t.hitDepth);
            out.normal = GetVec(params, t.hitNormal);
            out.location = GetVec(params, t.hitLocation);
            if (t.hitImpactNormal >= 0) out.impactNormal = GetVec(params, t.hitImpactNormal);
            if (t.hitComponent >= 0)
            {
                // A weak pointer: the object's index and serial number in the object array.
                const auto* weak = reinterpret_cast<const RC::Unreal::FWeakObjectPtr*>(params + t.hitComponent);
                out.component = weak->Get();
            }
            return true;
        }

        // The floor at a column as a character on `level` would step onto it: the engine's
        // own line down with a pawn's collision, from a step above the top of that level to
        // a step below its bottom, and the first thing it meets. A table top in that span
        // is the floor found, too high to step onto; a drop deeper than a step is no floor.
        // Asked once per column and level; the answer is the level of the floor found.
        bool FloorFrom(UObject* pawn, grid::Cell column, int level, double& floor)
        {
            const Place probe{column.x, column.y, level};
            if (const auto known = g_grid.probes.find(probe); known != g_grid.probes.end())
            {
                if (known->second == kNoFloor) return false;
                return FloorAt(grid::Cell{column.x, column.y, known->second}, floor);
            }
            if (!Prepare(g_lineTrace, L"LineTraceSingleByProfile")) return false;
            const double x = g_grid.origin.x + (column.x + 0.5) * kCellSize;
            const double y = g_grid.origin.y + (column.y + 0.5) * kCellSize;
            const Vec from{x, y, (level + 1) * kLevelSize + g_grid.stepHeight + 5.0};
            const Vec to{x, y, level * kLevelSize - g_grid.stepHeight - 5.0};
            TraceHit trace;
            RunTrace(g_lineTrace, pawn, from, to, 0.0, 0.0, trace);
            bool hit = trace.hit;
            const Vec where = trace.location;
            const Vec normal = trace.hit ? trace.impactNormal : Vec{0.0, 0.0, 1.0};
            ++g_grid.sweeps;
            // Ground steeper than the character stands on is no floor to step onto: she
            // would slide back down it.
            const bool steep = hit && normal.z < g_grid.walkableZ;
            if (steep) hit = false;
            ground::Probe record;
            record.x = column.x;
            record.y = column.y;
            record.level = level;
            record.found = hit;
            record.floor = where.z;
            record.normalZ = normal.z;
            record.why = steep ? 's' : 'n';
            g_record.Line(ground::ProbeLine(record));
            if (!hit)
            {
                g_grid.probes[probe] = kNoFloor;
                return false;
            }
            const int found = LevelOf(where.z);
            g_grid.probes[probe] = found;
            // The first floor found on a level is the one kept: another within the same
            // level is a step the character takes in its stride.
            const auto kept = g_grid.floors.emplace(Place{column.x, column.y, found}, where.z).first;
            floor = kept->second;
            return true;
        }

        // The grid is laid afresh where the character stands when none is laid or when the
        // character has gone far from the origin; the floors found stay with it, a door
        // opening moves no ground.
        // The character's own measures go with the grid: every step on it was swept with them,
        // so another character (another capsule) lays it afresh, and the record's anchor line
        // carries the measures the steps after it were taken with.
        void AnchorGrid(const Vec& middle, double radius, double half, double step, double walkable)
        {
            const bool sameBody = std::fabs(radius - g_grid.radius) < 0.5 && std::fabs(half - g_grid.capsuleHalf) < 0.5 &&
                                  std::fabs(step - g_grid.stepHeight) < 0.5 && std::fabs(walkable - g_grid.walkableZ) < 0.005;
            if (g_grid.anchored && sameBody && FlatDistance(middle, g_grid.origin) < 6000.0) return;
            g_grid = GridState{};
            {
                const uint64_t generation = g_reach.generation;
                g_reach = ReachMap{};
                g_reach.generation = generation;
                g_lastFlood.reset();
            }
            g_grid.anchored = true;
            g_grid.origin = Vec{middle.x, middle.y, middle.z};
            g_grid.floorZ = middle.z;
            g_grid.radius = radius;
            g_grid.capsuleHalf = half;
            g_grid.stepHeight = step;
            g_grid.walkableZ = walkable;
            // The old grid's cells and roads go; a verdict that lists a target stays as an earlier
            // map's (ways::Settling::Kept), judged by no map of now's.
            for (auto& t : g_targets)
            {
                t.gridCorners.clear();
                t.gridPartial = false;
                t.gridWalkable = 0;
                t.gridAt = -1000.0;
                t.gridFailedAt = -1000.0;
                t.gridEnclosed = false;
                if (t.reach != 1 && t.reach != 3) t.reach = 0;
                t.reachGeneration = 0;
                t.reachCells.clear();
                t.mapRoad.clear();
                t.mapWalkable = 0;
            }
            log::Info(L"explore: the ground is felt out on a grid of {:.0f} cm cells from ({:.0f}, {:.0f}) at height {:.0f}, for a capsule of radius {:.0f} "
                      L"and half height {:.0f} stepping {:.0f} cm",
                      kCellSize, middle.x, middle.y, middle.z, radius, half, step);
            ground::Anchor anchor;
            anchor.x = middle.x;
            anchor.y = middle.y;
            anchor.z = middle.z;
            anchor.cell = kCellSize;
            anchor.level = kLevelSize;
            anchor.half = g_grid.capsuleHalf;
            anchor.radius = g_grid.radius;
            anchor.step = g_grid.stepHeight;
            anchor.walkable = g_grid.walkableZ;
            g_record.Line(ground::AnchorLine(anchor));
        }

        // Whether a step from a cell into a neighbouring column is free, and onto which
        // level it lands: the floor there within a step of this one, then the capsule swept
        // from floor to floor. Remembered per step.
        bool GridStepFree(UObject* pawn, grid::Cell a, grid::Cell& b)
        {
            const std::tuple<int, int, int, int, int> key{a.x, a.y, a.level, b.x, b.y};
            if (const auto known = g_grid.steps.find(key); known != g_grid.steps.end())
            {
                if (known->second == kNoFloor) return false;
                b.level = known->second;
                return true;
            }
            const auto started = std::chrono::steady_clock::now();
            double floorA = 0.0;
            double floorB = 0.0;
            char why = 0;
            double clear = 0.0;
            bool free = FloorAt(a, floorA) || FloorFrom(pawn, a, a.level, floorA);
            if (!free) why = 'A';
            if (free && !FloorFrom(pawn, b, LevelOf(floorA), floorB))
            {
                free = false;
                why = 'B';
            }
            if (free && std::fabs(floorB - floorA) > g_grid.stepHeight)
            {
                free = false;
                why = 'H';
                clear = floorB - floorA;
            }
            if (free)
            {
                b.level = LevelOf(floorB);
                const Vec from = SweepMiddleOf(a);
                const Vec to = SweepMiddleOf(b);
                Vec reached = from;
                UObject* blocker = nullptr;
                clear = SweepClear(pawn, from, to, g_grid.radius, SweepHalfHeight(), nullptr, &reached, &blocker);
                ++g_grid.sweeps;
                if (clear < 0.0 && !g_grid.unavailableLogged)
                {
                    g_grid.unavailableLogged = true;
                    log::Error(L"explore: the engine's sweep cannot be asked, so the ground cannot be felt out");
                }
                // The capsule need only come within half a cell of the cell's middle: then it
                // stands somewhere in the cell. A corridor narrower for its middle than a cell
                // (the woods' path walls leave 17 cm) holds no cell's middle, and asking for the
                // middle itself closed such a corridor to the search while the game walked her
                // through. The next step from the cell is swept from its middle and pulled back
                // out of the wall it touches, as the character's own movement does. Nearness is
                // measured from where the capsule got to, after any pull-back, to the cell's own
                // middle, so that cells along a wall cannot creep into it a step at a time.
                free = clear >= 0.0 && FlatDistance(reached, to) <= kStepReach;
                // Another character is no wall, whether or not the engine's list kept her out of the
                // sweep (ScanOthers): she walks on.
                if (!free && clear >= 0.0 && OtherCharacter(pawn, blocker)) free = true;
                if (!free) why = 'W';
                if (free && DoorLeafBetween(from, to, g_grid.radius, SweepHalfHeight()))
                {
                    free = false;
                    why = 'D';
                }
            }
            g_grid.sweepMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            g_grid.steps[key] = free ? b.level : kNoFloor;
            if (!free) g_grid.why[key] = why;
            if (why == 'W' || why == 'D') g_grid.refusedAt[key] = gamethread::NowSeconds();
            ground::Step record;
            record.from = a;
            record.bx = b.x;
            record.by = b.y;
            record.free = free;
            record.land = b.level;
            record.why = why;
            record.clear = clear;
            g_record.Line(ground::StepLine(record));
            return free;
        }

        bool ReachRunning();
        bool JudgedByMap(const Target& t);
        void StartReachMap(UObject* pawn, double now);

        // Whether the ground is being felt out right now, and for which target.
        bool GridBusy()
        {
            return g_grid.search != nullptr || g_grid.straightener != nullptr;
        }

        // Whether a target's way is being felt out: its own search, or the reach map that has not
        // judged it yet.
        bool GridSearching(UObject* actor)
        {
            if (GridBusy() && g_grid.searchFor == actor) return true;
            if (!ReachRunning()) return false;
            return std::any_of(g_targets.begin(), g_targets.end(), [&](const Target& t) { return t.actor == actor && !JudgedByMap(t); });
        }

        // What the game may have changed since the ground was felt out (a door opened at the
        // end of an interaction) is forgotten, and every way is felt out anew.
        void ForgetGround()
        {
            // Every step once refused is asked again: a door that stood closed, or a character
            // that stood in the way, was a wall to a sweep, or a floor too high or none to a
            // probe, and the answer is only right once they have moved, so the probes of the
            // columns such steps led into go too. A step found free stays free: what the game
            // changes between two holds opens ways far more often than it closes them, and
            // asking every step anew had each new reach map sweep the whole ground again.
            std::set<std::pair<int, int>> columns;
            for (const auto& [key, why] : g_grid.why)
            {
                if (why == 'H' || why == 'B') columns.insert({std::get<3>(key), std::get<4>(key)});
            }
            std::erase_if(g_grid.steps, [](const auto& step) { return step.second == kNoFloor; });
            g_grid.why.clear();
            g_grid.refusedAt.clear();
            std::erase_if(g_grid.probes, [&](const auto& probe)
                          { return probe.second == kNoFloor || columns.count({std::get<0>(probe.first), std::get<1>(probe.first)}) > 0; });
            std::erase_if(g_grid.floors, [&](const auto& floor) { return columns.count({std::get<0>(floor.first), std::get<1>(floor.first)}) > 0; });
            g_grid.search.reset();
            g_grid.straightener.reset();
            g_grid.searchFor = nullptr;
            const uint64_t generation = g_reach.generation;
            if (g_reach.flood) g_lastFlood = std::move(g_reach.flood);
            g_reach = ReachMap{};
            g_reach.generation = generation;
            // A verdict that lists a target (its place reached, or beyond a door) stays until a new
            // map judges it; a verdict of no way is dropped at once, since what closed the way may
            // be gone.
            for (auto& t : g_targets)
            {
                if (t.reach == 1 || t.reach == 3) continue;
                t.gridCorners.clear();
                t.gridPartial = false;
                t.gridWalkable = 0;
                t.gridAt = -1000.0;
                t.gridFailedAt = -1000.0;
                t.gridEnclosed = false;
                t.reach = 0;
            }
        }

        // The doors' leaves as they stand now (ReadDoorLeaves), and the steps through a leaf the ground
        // has kept for free, swept while the door stood open or slept, asked again.
        void ForgetDoorways()
        {
            ReadDoorLeaves();
            size_t forgotten = 0;
            for (const auto& leaf : g_doorLeaves)
            {
                const double reach = g_grid.radius + kCellSize;
                const int x0 = static_cast<int>(std::floor((leaf.low.x - reach - g_grid.origin.x) / kCellSize));
                const int x1 = static_cast<int>(std::floor((leaf.high.x + reach - g_grid.origin.x) / kCellSize));
                const int y0 = static_cast<int>(std::floor((leaf.low.y - reach - g_grid.origin.y) / kCellSize));
                const int y1 = static_cast<int>(std::floor((leaf.high.y + reach - g_grid.origin.y) / kCellSize));
                for (int x = x0; x <= x1; ++x)
                {
                    for (auto it = g_grid.steps.lower_bound({x, y0, INT_MIN, INT_MIN, INT_MIN}); it != g_grid.steps.end() && std::get<0>(it->first) == x;)
                    {
                        const auto& [ax, ay, alevel, bx, by] = it->first;
                        if (ay > y1) break;
                        if (it->second == kNoFloor)
                        {
                            ++it;
                            continue;
                        }
                        const Vec from = SweepMiddleOf(grid::Cell{ax, ay, alevel});
                        const Vec to = SweepMiddleOf(grid::Cell{bx, by, it->second});
                        if (DoorLeafBetween(from, to, g_grid.radius, SweepHalfHeight()))
                        {
                            it = g_grid.steps.erase(it);
                            ++forgotten;
                        }
                        else
                        {
                            ++it;
                        }
                    }
                }
            }
            if (forgotten > 0) log::Info(L"explore: {} steps through the doors' leaves, kept for free, are asked again", forgotten);
        }

        // The steps once swept into something are asked again: those older than a while, or
        // all of them once the story has moved on since (a trigger set off may open a gate or
        // send a character away). Ways that ended short of their targets are felt out anew.
        constexpr double kRefusalAge = 30.0;

        void ForgetStaleRefusals(double now)
        {
            const bool moved = g_grid.storyMovedAt > g_grid.refusalsForgottenAt;
            size_t forgotten = 0;
            for (auto it = g_grid.refusedAt.begin(); it != g_grid.refusedAt.end();)
            {
                if (moved || now - it->second > kRefusalAge)
                {
                    g_grid.steps.erase(it->first);
                    g_grid.why.erase(it->first);
                    it = g_grid.refusedAt.erase(it);
                    ++forgotten;
                }
                else
                {
                    ++it;
                }
            }
            if (moved)
            {
                // A gate that stood shut was a floor too high, or no floor, to the probes of
                // its column: those steps go, and the probes and floors of the columns they
                // led into with them, so that the next search probes them afresh.
                std::set<std::pair<int, int>> columns;
                for (auto it = g_grid.why.begin(); it != g_grid.why.end();)
                {
                    const auto& [ax, ay, alevel, bx, by] = it->first;
                    if (it->second == 'H' || it->second == 'B')
                    {
                        columns.insert({bx, by});
                        g_grid.steps.erase(it->first);
                        it = g_grid.why.erase(it);
                        ++forgotten;
                    }
                    else
                    {
                        ++it;
                    }
                }
                std::erase_if(g_grid.probes, [&](const auto& probe) { return columns.count({std::get<0>(probe.first), std::get<1>(probe.first)}) > 0; });
                std::erase_if(g_grid.floors, [&](const auto& floor) { return columns.count({std::get<0>(floor.first), std::get<1>(floor.first)}) > 0; });
                // The story's moment is taken as late as it was stamped, so that it is not
                // taken for a later one at the next look.
                g_grid.refusalsForgottenAt = std::max(now, g_grid.storyMovedAt);
                for (auto& t : g_targets)
                {
                    if (!t.gridPartial || t.reach != 0) continue;
                    t.gridFailedAt = -1000.0;
                    t.gridAt = -1000.0;
                }
            }
            if (forgotten > 0)
                log::Verbose(L"explore: {} steps once swept into something are asked again{}", forgotten, moved ? L", the story having moved on" : L"");
        }

        // Where a target lies on the grid for a search or the reach map: its aim's cell, how many
        // cells from it count as there, and on how many levels above or below it.
        void GoalOf(const Target& t, grid::Cell& goal, double& radius, int& levels)
        {
            const Vec aim = Aim(t);
            goal = CellOf(aim, aim.z);
            radius = 1.5;
            levels = kGoalLevels;
            if (t.hasBox)
            {
                // A way on or a thing to use whose collision is known is set off, or offered, where
                // her capsule touches it: the goal is the middle of its pieces, the circle holds them
                // all, and a cell counts as there only where she touches them (Touches). A way whose
                // own box is known is set off where her capsule overlaps the box (the searches head
                // for the circle within it); any other box, by its middle.
                if (t.hasCollision)
                {
                    const Vec middle{(t.collisionLow.x + t.collisionHigh.x) / 2.0, (t.collisionLow.y + t.collisionHigh.y) / 2.0,
                                     (t.collisionLow.z + t.collisionHigh.z) / 2.0};
                    goal = CellOf(middle, middle.z);
                    radius = (std::hypot(t.collisionHigh.x - t.collisionLow.x, t.collisionHigh.y - t.collisionLow.y) / 2.0 + g_grid.radius) / kCellSize + 1.0;
                    levels = -1;
                    return;
                }
                if (t.way && t.shaped)
                {
                    goal = CellOf(t.shape.middle, t.shape.middle.z);
                    radius = ways::VolumeGoalRadius(t.shape, g_grid.radius, kCellSize);
                }
                else
                {
                    radius = std::max(1.5, std::min(t.boxHigh.x - t.boxLow.x, t.boxHigh.y - t.boxLow.y) / 2.0 / kCellSize);
                }
                // A volume's aim is its middle; a tall one is entered from well below it.
                levels = std::max(kGoalLevels, (LevelOf(t.boxHigh.z) - LevelOf(t.boxLow.z)) / 2 + 2);
            }
        }

        bool Touches(const Target& t, grid::Cell c);

        void RequestGridRoute(UObject* pawn, Target& t, double now)
        {
            Vec middle;
            double halfHeight = 88.0;
            if (!CapsuleSpan(pawn, middle, halfHeight)) return;
            AnchorGrid(middle, WalkingRadius(pawn), halfHeight, StepHeight(pawn), WalkableFloorZ(pawn));
            // The character's own cell stands on the floor its capsule rests on.
            const double floor = middle.z - halfHeight;
            const grid::Cell start = CellOf(middle, floor);
            g_grid.floors[PlaceOf(start)] = floor;
            // The steps into and out of the cell she stands in are swept from where she stands,
            // so those felt out from a middle before are forgotten, for the cell she stood in
            // last time as well as for this one.
            const auto touching = [](const grid::Cell& c)
            {
                return [c](const auto& step)
                {
                    const auto& [ax, ay, alevel, bx, by] = step.first;
                    return (ax == c.x && ay == c.y && alevel == c.level) || (bx == c.x && by == c.y);
                };
            };
            const auto forget = [&](const grid::Cell& c)
            {
                std::erase_if(g_grid.steps, touching(c));
                std::erase_if(g_grid.why, touching(c));
                std::erase_if(g_grid.refusedAt, touching(c));
            };
            if (g_grid.startKnown && g_grid.startCell != start) forget(g_grid.startCell);
            if (!g_grid.startKnown || start != g_grid.startCell || FlatDistance(middle, g_grid.startMiddle) > 5.0) forget(start);
            g_grid.startKnown = true;
            g_grid.startCell = start;
            g_grid.startMiddle = middle;
            ForgetStaleRefusals(now);
            t.gridFrom = middle;
            const Vec aim = Aim(t);
            grid::Cell goal;
            double goalRadius = 1.5;
            int goalLevels = kGoalLevels;
            GoalOf(t, goal, goalRadius, goalLevels);
            // The search reaches as far as the list of targets does.
            const int radiusCells = static_cast<int>(cfg::Get().exploreRange * 100.0 / kCellSize) + 50;
            std::function<bool(grid::Cell)> fits;
            if (t.hasCollision) fits = [collision = t](grid::Cell c) { return Touches(collision, c); };
            g_grid.search = std::make_unique<grid::Search>(start, goal, goalRadius, goalLevels, radiusCells, kGridExpansions, kLevelSize / kCellSize, fits);
            g_grid.search->SetLinks(&PassageLinksOf);
            ground::Search record;
            record.from = start;
            record.to = goal;
            record.radius = goalRadius;
            record.levels = goalLevels;
            g_record.Line(ground::SearchLine(record));
            g_grid.straightener.reset();
            g_grid.searchFor = t.actor;
            g_grid.searchAim = aim;
            g_grid.searchStartedAt = now;
            g_grid.searchSweeps = g_grid.sweeps;
            log::Verbose(L"explore: feeling out the way to \"{}\" from cell ({}, {}) on level {} to ({}, {}) on level {}, within {:.1f} cells of it", t.label,
                         start.x, start.y, start.level, goal.x, goal.y, goal.level, goalRadius);
        }

        // Whether the ground runs on under a straight way between two cells: the floor of
        // every column along it within a step of the one before, by the same probes the
        // steps use. A sweep alone floats a step above the ground and would cross a drop.
        bool GroundRuns(UObject* pawn, grid::Cell a, grid::Cell b)
        {
            double floor = 0.0;
            if (!FloorAt(a, floor)) return false;
            const int steps = std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
            for (int i = 1; i <= steps; ++i)
            {
                const grid::Cell column{a.x + (b.x - a.x) * i / steps, a.y + (b.y - a.y) * i / steps, 0};
                double next = 0.0;
                if (!FloorFrom(pawn, column, LevelOf(floor), next) || std::fabs(next - floor) > g_grid.stepHeight) return false;
                floor = next;
            }
            return true;
        }

        void LogBlockers(UObject* pawn, grid::Cell nearest);

        // Runs the search a slice of a frame at a time, then pulls the cells found straight
        // the same way, and hands the way to its target.
        void PollGrid(UObject* pawn, double now)
        {
            if (!GridBusy() || !pawn) return;
            Target* target = nullptr;
            for (auto& t : g_targets)
            {
                if (t.actor == g_grid.searchFor) target = &t;
            }
            if (!target)
            {
                g_grid.search.reset();
                g_grid.straightener.reset();
                g_grid.searchFor = nullptr;
                return;
            }
            const auto started = std::chrono::steady_clock::now();
            const double budget = g_walking && g_grid.searchFor == g_selected ? kGridWalkBudgetMs : kGridBudgetMs;
            const auto overBudget = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() > budget; };
            if (g_grid.search)
            {
                const auto passable = [&](grid::Cell a, grid::Cell& b) { return GridStepFree(pawn, a, b); };
                auto state = g_grid.search->Current();
                while (state == grid::Search::State::Running)
                {
                    state = g_grid.search->Step(10, passable);
                    if (overBudget()) break;
                }
                if (state == grid::Search::State::Running) return;
                g_grid.searchExpanded = g_grid.search->Expanded();
                if (state == grid::Search::State::Failed)
                {
                    const double elapsed = now - g_grid.searchStartedAt;
                    const int sweeps = g_grid.sweeps - g_grid.searchSweeps;
                    target->gridFailedAt = now;
                    // Closed in: every cell she can reach was opened, not the budget spent. A
                    // handful of cells is her own spot refused rather than a closed place.
                    target->gridEnclosed =
                        g_grid.searchExpanded < kGridExpansions && g_grid.searchExpanded >= kEnclosedMinCells && !g_grid.search->RadiusReached();
                    log::Info(L"explore: no way over the ground to \"{}\": {} cells opened, {} sweeps, {:.1f} s", target->label, g_grid.searchExpanded, sweeps,
                              elapsed);
                    // Where the search got to, and what stopped it there, for the log and the record.
                    const grid::Cell nearest = g_grid.search->Nearest();
                    const Vec nearAt = WorldOf(nearest);
                    std::wstring around;
                    for (int dy = -1; dy <= 1; ++dy)
                    {
                        for (int dx = -1; dx <= 1; ++dx)
                        {
                            if (dx == 0 && dy == 0) continue;
                            const auto w =
                                g_grid.why.find(std::tuple<int, int, int, int, int>{nearest.x, nearest.y, nearest.level, nearest.x + dx, nearest.y + dy});
                            around += std::format(L" {:+}{:+}:{}", dx, dy, w == g_grid.why.end() ? L'?' : w->second);
                        }
                    }
                    log::Info(L"explore: the search got nearest the goal at ({:.0f}, {:.0f}, {:.0f}) on level {}, {:.0f} cm from it; the steps out of that "
                              L"cell:{} (A/B no floor here/there, H too high, W swept into something, ? not tried)",
                              nearAt.x, nearAt.y, nearAt.z - g_grid.capsuleHalf, nearest.level, FlatDistance(nearAt, g_grid.searchAim), around);
                    LogBlockers(pawn, nearest);
                    ground::Search record;
                    record.done = true;
                    record.found = false;
                    record.cells = g_grid.searchExpanded;
                    record.sweeps = sweeps;
                    record.nearest = nearest;
                    g_record.Line(ground::ResultLine(record));
                    g_lister.Again(); // the list is judged again with this verdict
                    // The way runs as far as the ground was found to carry the character, then
                    // straight on, as a partial road of a navigation mesh does: the beacon and
                    // the walk both take it, and a walk that stops getting nearer says so.
                    const auto partial = g_grid.search->PartialPath();
                    g_grid.search.reset();
                    g_grid.partial = true;
                    g_grid.straightener = std::make_unique<grid::Straightener>(partial, kCornerRun);
                    g_grid.straightenFor = 0;
                }
                else
                {
                    ground::Search record;
                    record.done = true;
                    record.found = true;
                    record.cells = g_grid.searchExpanded;
                    record.sweeps = g_grid.sweeps - g_grid.searchSweeps;
                    record.nearest = g_grid.search->Nearest();
                    g_record.Line(ground::ResultLine(record));
                    g_grid.partial = false;
                    g_grid.straightener = std::make_unique<grid::Straightener>(g_grid.search->Path(), kCornerRun);
                    g_grid.straightenFor = 0;
                    g_grid.search.reset();
                }
            }
            // The cells are pulled straight where the ground runs on and the direct sweep is
            // free, a few checks a frame.
            const auto clear = [&](grid::Cell a, grid::Cell b)
            {
                if (!GroundRuns(pawn, a, b)) return false;
                const Vec from = SweepMiddleOf(a);
                const Vec to = SweepMiddleOf(b);
                const double c = SweepClear(pawn, from, to, g_grid.radius, SweepHalfHeight());
                ++g_grid.sweeps;
                return c >= 0.0 && c >= Distance(from, to) - 1.0;
            };
            while (!g_grid.straightener->Step(2, clear))
            {
                if (overBudget()) return;
            }
            if (g_grid.straightenFor != 0 && target->reachGeneration != g_grid.straightenFor)
            {
                log::Verbose(L"explore: the road pulled straight for \"{}\" is an older map's, and is dropped", target->label);
                g_grid.straightener.reset();
                g_grid.partial = false;
                g_grid.searchFor = nullptr;
                g_grid.straightenFor = 0;
                return;
            }
            const double elapsed = now - g_grid.searchStartedAt;
            const int sweeps = g_grid.sweeps - g_grid.searchSweeps;
            const auto& corners = g_grid.straightener->Corners();
            target->gridCorners.clear();
            for (const auto& c : corners)
                target->gridCorners.push_back(WorldOf(c));
            target->gridPartial = g_grid.partial;
            target->gridWalkable = target->gridCorners.size();
            // Within reach of the goal from the start, the way is the step to the thing itself;
            // short of it, the straight leg on from the nearest cell reached.
            if (target->gridCorners.size() == 1 || g_grid.partial)
            {
                const Vec aim = g_grid.searchAim;
                target->gridCorners.push_back(Vec{aim.x, aim.y, target->gridCorners.back().z});
            }
            target->gridAt = now;
            target->gridGoal = g_grid.searchAim;
            if (!g_grid.partial)
            {
                target->gridFailedAt = -1000.0;
                target->gridEnclosed = false;
            }
            double length = 0.0;
            for (size_t i = 1; i < target->gridCorners.size(); ++i)
                length += FlatDistance(target->gridCorners[i - 1], target->gridCorners[i]);
            std::wstring points;
            for (const Vec& p : target->gridCorners)
                points += std::format(L" ({:.0f}, {:.0f}, {:.0f})", p.x, p.y, p.z - g_grid.capsuleHalf);
            if (g_grid.partial)
                log::Info(L"explore: the way to \"{}\" is felt out as far as {:.0f} cm short of it, then straight on: {:.0f} cm in {} corners:{}",
                          target->label, FlatDistance(target->gridCorners[target->gridWalkable - 1], g_grid.searchAim), length, target->gridCorners.size(),
                          points);
            else
                log::Info(
                    L"explore: the way to \"{}\" felt out: {:.0f} cm in {} corners; {} cells opened, {} sweeps in {:.1f} s ({:.0f} ms sweeping so far):{}",
                    target->label, length, target->gridCorners.size(), g_grid.searchExpanded, sweeps, elapsed, g_grid.sweepMs, points);
            g_grid.straightener.reset();
            g_grid.partial = false;
            g_grid.searchFor = nullptr;
            g_grid.straightenFor = 0;
            g_lister.Again(); // the list is judged again with this way
        }

        // ---- the reach map ---------------------------------------------------------------

        bool ReachRunning()
        {
            return g_reach.flood && g_reach.doneAt < 0.0;
        }

        // Maps count from 1; a verdict of generation 0 is an earlier grid's.
        bool JudgedByMap(const Target& t)
        {
            return t.reach != 0 && t.reachGeneration != 0 && t.reachGeneration == g_reach.generation;
        }

        // The cell the character stands in is one the map reached: she is still on its ground.
        bool OnGroundOf(const grid::Flood& flood, const Vec& middle, double halfHeight)
        {
            const grid::Cell here = CellOf(middle, middle.z - halfHeight);
            for (int level = here.level - 1; level <= here.level + 1; ++level)
            {
                if (flood.Reached(grid::Cell{here.x, here.y, level})) return true;
            }
            return false;
        }

        bool OnMappedGround(const Vec& middle, double halfHeight)
        {
            return g_reach.flood && OnGroundOf(*g_reach.flood, middle, halfHeight);
        }

        // On the ground of the map standing, or of the last one an interaction dropped.
        bool OnLastMappedGround(const Vec& middle, double halfHeight)
        {
            if (g_reach.flood) return OnGroundOf(*g_reach.flood, middle, halfHeight);
            return g_lastFlood && OnGroundOf(*g_lastFlood, middle, halfHeight);
        }

        void StartReachMap(UObject* pawn, double now)
        {
            Vec middle;
            double halfHeight = 88.0;
            if (!CapsuleSpan(pawn, middle, halfHeight)) return;
            AnchorGrid(middle, WalkingRadius(pawn), halfHeight, StepHeight(pawn), WalkableFloorZ(pawn));
            const double floor = middle.z - halfHeight;
            const grid::Cell start = CellOf(middle, floor);
            g_grid.floors[PlaceOf(start)] = floor;
            // The steps out of the cell she stands in are swept from where she stands, as a
            // search's are.
            const auto touching = [](const grid::Cell& c)
            {
                return [c](const auto& step)
                {
                    const auto& [ax, ay, alevel, bx, by] = step.first;
                    return (ax == c.x && ay == c.y && alevel == c.level) || (bx == c.x && by == c.y);
                };
            };
            const auto forget = [&](const grid::Cell& c)
            {
                std::erase_if(g_grid.steps, touching(c));
                std::erase_if(g_grid.why, touching(c));
                std::erase_if(g_grid.refusedAt, touching(c));
            };
            if (g_grid.startKnown && g_grid.startCell != start) forget(g_grid.startCell);
            if (!g_grid.startKnown || start != g_grid.startCell || FlatDistance(middle, g_grid.startMiddle) > 5.0) forget(start);
            g_grid.startKnown = true;
            g_grid.startCell = start;
            g_grid.startMiddle = middle;
            ForgetStaleRefusals(now);
            ForgetDoorways();
            ReadPassages(pawn);
            const int radiusCells = static_cast<int>(cfg::Get().exploreRange * 100.0 / kCellSize) + 50;
            const uint64_t generation = g_reach.generation + 1;
            g_reach = ReachMap{};
            g_reach.generation = generation;
            g_reach.flood = std::make_unique<grid::Flood>(start, radiusCells, kFloodExpansions);
            g_reach.flood->SetLinks(&PassageLinksOf);
            g_reach.origin = middle;
            g_reach.startedAt = now;
            g_reach.sweepsAtStart = g_grid.sweeps;
            g_settle.Begin(now);
            log::Info(L"explore: the reach map is begun from cell ({}, {}) on level {} at ({:.0f}, {:.0f}, {:.0f})", start.x, start.y, start.level, middle.x,
                      middle.y, middle.z);
        }

        // The cells of a path as the corners of a road, pulled straight over the ground the map
        // reached (ways::StraightOverMap): the road whose length is said and whose volumes the row
        // rule weighs, close to the one the walk takes. The map's cells themselves zigzag between
        // equal ways: at the woods' start on 2026-10-28 they said 47 m to a way the walk then found
        // 45 m off. The chosen target's road is pulled straight once more with the character's own
        // sweeps, for the walk and the beacon (StraightenRoad).
        std::vector<Vec> RoadOfCells(const std::vector<grid::Cell>& cells)
        {
            std::vector<Vec> road;
            if (!g_reach.flood) return road;
            for (const auto& c : ways::StraightOverMap(*g_reach.flood, cells, kCornerRun))
                road.push_back(WorldOf(c));
            return road;
        }

        // Pulls the chosen target's road straight with the searches' own straightener, so that the
        // walk and the beacon take the same corners.
        void StraightenRoad(Target& t, const std::vector<grid::Cell>& cells, bool partial, double now)
        {
            if (cells.size() < 3)
            {
                t.straightened = true;
                return;
            }
            if (GridBusy()) return;
            t.straightened = true;
            g_grid.straightener = std::make_unique<grid::Straightener>(cells, kCornerRun);
            g_grid.straightenFor = g_reach.generation;
            g_grid.partial = partial;
            g_grid.searchFor = t.actor;
            g_grid.searchAim = Aim(t);
            g_grid.searchStartedAt = now;
            g_grid.searchSweeps = g_grid.sweeps;
            g_grid.searchExpanded = g_reach.flood ? g_reach.flood->Expanded() : 0;
        }

        // A door the character only tries (ways::OnlyTried), by the animations it binds her with.
        bool LockedDoor(UObject* door)
        {
            auto* binding = obj::FindProperty(door, L"GenericBoundCharacter");
            void* container = binding ? obj::ValuePtr(door, binding) : nullptr;
            if (!container) return false;
            std::vector<std::wstring> animations;
            for (const wchar_t* member : {L"FemaleAnimInstance", L"MaleAnimInstance"})
            {
                UObject* anim = nullptr;
                if (obj::ReadObjectAt(container, obj::StructMember(binding, member), anim) && anim) animations.push_back(obj::ObjectName(anim));
            }
            return ways::OnlyTried(animations);
        }

        // A door the character can open.
        bool IsDoor(UObject* useLocation)
        {
            UObject* interaction = nullptr;
            return obj::IsLive(useLocation) && obj::ReadObject(useLocation, L"InteractionActor", interaction) && obj::IsLive(interaction) &&
                   obj::IsA(interaction, L"BatesInteractionDoor") && !LockedDoor(interaction);
        }

        // Whether the character, standing in a cell, touches a way on's collision: her capsule
        // over the cell's floor, by the pieces themselves (shapes::CapsuleTouches), after the box
        // around them has been passed.
        bool Touches(const Target& t, grid::Cell c)
        {
            const Vec middle = WorldOf(c);
            const double r = g_grid.radius;
            if (middle.x < t.collisionLow.x - r || middle.x > t.collisionHigh.x + r || middle.y < t.collisionLow.y - r || middle.y > t.collisionHigh.y + r ||
                middle.z + g_grid.capsuleHalf < t.collisionLow.z || middle.z - g_grid.capsuleHalf > t.collisionHigh.z)
                return false;
            return shapes::CapsuleTouches(t.collision, middle, r, g_grid.capsuleHalf);
        }

        // The reached cell of least cost from which the character gets at a target: for a way on
        // whose collision is known, one where her capsule touches it (Touches); for one whose own
        // box alone is known, one where her capsule overlaps that box (ways::CapsuleOverlaps;
        // never the engine's bounds, square with the world, which reach far beyond a volume set
        // aslant); for anything else, one within its goal's circle.
        bool ReachedFrom(const grid::Flood& flood, const Target& t, grid::Cell& best)
        {
            grid::Cell goal;
            double radius = 1.5;
            int levels = kGoalLevels;
            GoalOf(t, goal, radius, levels);
            if (t.hasCollision)
                return flood.BestWhere(goal, static_cast<int>(std::ceil(radius)), -1, [&](grid::Cell c) { return Touches(t, c); }, best);
            if (!t.way || !t.shaped) return flood.BestIn(goal, radius, levels, best);
            const int reach =
                static_cast<int>(std::ceil((std::max(t.shape.high.x - t.shape.low.x, t.shape.high.y - t.shape.low.y) / 2.0 + g_grid.radius) / kCellSize)) + 1;
            return flood.BestWhere(goal, reach, levels, [&](grid::Cell c) { return ways::CapsuleOverlaps(t.shape, WorldOf(c), g_grid.radius); }, best);
        }

        // What stops the map at the cell it got nearest a thing from: the refused steps out of it that head
        // toward the thing, each swept again for what it meets (a wall, a door's leaf), or a step too high
        // or no floor. The step straight at the thing is often never tried: a diagonal is taken only
        // where both straight steps beside it are free (the generator shed's doorway on 2026-11-04 said
        // nothing so).
        std::wstring StoppedBy(grid::Cell nearest, grid::Cell goal)
        {
            UObject* pawn = Pawn();
            const int gx = goal.x - nearest.x;
            const int gy = goal.y - nearest.y;
            if (!pawn || (gx == 0 && gy == 0)) return {};
            std::vector<std::wstring> seen;
            for (int dy = -1; dy <= 1; ++dy)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    if ((dx == 0 && dy == 0) || dx * gx + dy * gy <= 0) continue;
                    const auto why = g_grid.why.find(std::tuple<int, int, int, int, int>{nearest.x, nearest.y, nearest.level, nearest.x + dx, nearest.y + dy});
                    if (why == g_grid.why.end()) continue;
                    std::wstring what;
                    if (why->second == 'H')
                        what = L"a step too high";
                    else if (why->second == 'D')
                        what = L"a door's leaf";
                    else if (why->second != 'W')
                        what = L"no floor";
                    else
                    {
                        double floor = 0.0;
                        grid::Cell next{nearest.x + dx, nearest.y + dy, nearest.level};
                        if (FloorAt(next, floor)) next.level = LevelOf(floor);
                        SweepClear(pawn, SweepMiddleOf(nearest), SweepMiddleOf(next), g_grid.radius, SweepHalfHeight(), &what);
                        if (what.empty()) what = L"something the sweep no longer meets";
                    }
                    if (std::find(seen.begin(), seen.end(), what) == seen.end()) seen.push_back(what);
                }
            }
            return seen.empty() ? std::wstring(L"no step toward it tried") : str::Join(seen, L"; ");
        }

        // Judges the targets the map can judge now: every target whose place it has reached, and,
        // once it is done, every other one.
        void JudgeByReach(double now, bool done)
        {
            if (!g_reach.flood) return;
            const grid::Flood& flood = *g_reach.flood;
            const bool proves = done && flood.Current() == grid::Flood::State::Complete && !flood.RadiusReached() && flood.Expanded() >= kEnclosedMinCells;
            // A door the character can open, in the ground the map reached: what lies behind it
            // is not closed off.
            bool doors = false;
            if (done)
            {
                for (const auto& t : g_targets)
                {
                    if (t.way || !IsDoor(t.actor)) continue;
                    grid::Cell best;
                    if (ReachedFrom(flood, t, best)) doors = true;
                }
            }
            for (auto& t : g_targets)
            {
                if (JudgedByMap(t) || (done && t.reachTried == g_reach.generation)) continue;
                grid::Cell goal, best;
                double radius = 1.5;
                int levels = kGoalLevels;
                GoalOf(t, goal, radius, levels);
                if (ReachedFrom(flood, t, best))
                {
                    const auto cells = flood.PathTo(best);
                    // Out of reach when the list stood, reached now: the ground has grown (a door
                    // opened beside her), and every verdict is given anew, by this map.
                    if (g_lister.Frozen() && ways::Reopened(t.reachAtFreeze, 1))
                    {
                        log::Info(L"explore: the map reaches \"{}\" {}, which the list stood on as {}: every verdict is given anew", t.label,
                                  obj::ObjectName(t.actor), t.reachAtFreeze == 3 ? L"beyond a door" : L"closed off");
                        g_lister.NewHold();
                        g_freezeAfter = static_cast<int64_t>(g_reach.generation) - 1;
                        for (auto& o : g_targets)
                            o.reachAtFreeze = 0;
                    }
                    t.reach = 1;
                    t.reachGeneration = g_reach.generation;
                    t.reachCells = cells;
                    t.straightened = false;
                    t.gridCorners = RoadOfCells(cells);
                    if (t.gridCorners.size() == 1) t.gridCorners.push_back(Vec{Aim(t).x, Aim(t).y, t.gridCorners.back().z});
                    t.gridWalkable = t.gridCorners.size();
                    t.mapRoad = t.gridCorners;
                    t.mapWalkable = t.gridWalkable;
                    t.gridPartial = false;
                    t.gridEnclosed = false;
                    t.gridAt = now;
                    t.gridFailedAt = -1000.0;
                    t.gridGoal = Aim(t);
                    t.gridFrom = g_reach.origin;
                    g_lister.Again();
                    log::Info(L"explore: the reach map reaches \"{}\" {}: {:.0f} cm on foot", t.label, obj::ObjectName(t.actor), flood.Cost(best) * kCellSize);
                    if (t.actor == g_selected) StraightenRoad(t, cells, false, now);
                    continue;
                }
                if (!done) continue;
                t.reachTried = g_reach.generation;
                if (!proves)
                {
                    // Cut short: the ground beyond is not known, and a search may find a way there.
                    continue;
                }
                const grid::Cell nearest = flood.Nearest(goal, kLevelSize / kCellSize);
                const auto cells = flood.PathTo(nearest);
                const auto stopped = StoppedBy(nearest, goal);
                t.reach = doors ? 3 : 2;
                t.reachGeneration = g_reach.generation;
                t.reachCells = cells;
                t.straightened = false;
                t.gridCorners = RoadOfCells(cells);
                t.gridWalkable = t.gridCorners.size();
                t.mapRoad = t.gridCorners;
                t.mapWalkable = t.gridWalkable;
                t.gridCorners.push_back(Vec{Aim(t).x, Aim(t).y, t.gridCorners.empty() ? Aim(t).z : t.gridCorners.back().z});
                t.gridPartial = true;
                t.gridEnclosed = !doors;
                t.gridAt = now;
                t.gridFailedAt = now;
                t.gridGoal = Aim(t);
                t.gridFrom = g_reach.origin;
                g_lister.Again();
                log::Info(L"explore: the reach map does not reach \"{}\" {}{}; it got nearest {:.0f} cm from it{}", t.label, obj::ObjectName(t.actor),
                          doors ? L", but a door the character can open stands in the ground it reached" : L": closed off",
                          FlatDistance(WorldOf(nearest), Aim(t)), stopped.empty() ? std::wstring() : L", stopped there by " + stopped);
                if (t.actor == g_selected) StraightenRoad(t, cells, true, now);
            }
        }

        // The rooms of the ground the map has reached (rooms::Area), counted as it reaches the cells,
        // and a few thousand cells a frame at most, so that rooms counted anew over a map already run
        // do not take one frame's time.
        void CountArea()
        {
            if (!g_reach.flood) return;
            if (g_reach.areaZones != g_zonesGeneration)
            {
                g_reach.area.Reset(g_zones.size());
                g_reach.areaCounted = 0;
                g_reach.areaZones = g_zonesGeneration;
            }
            const auto& order = g_reach.flood->Order();
            const size_t end = std::min(order.size(), g_reach.areaCounted + 4000);
            for (; g_reach.areaCounted < end; ++g_reach.areaCounted)
                g_reach.area.Add(g_zones.empty() ? -1 : rooms::ZoneAt(g_zones, WorldOf(order[g_reach.areaCounted])));
        }

        // Runs the map a slice of a frame at a time, and judges as it goes.
        void PollReachMap(UObject* pawn, double now)
        {
            if (!g_reach.flood || !pawn) return;
            // A done map judges the targets that came after it, out of what it reached.
            if (!ReachRunning())
            {
                CountArea();
                JudgeByReach(now, true);
                return;
            }
            const auto started = std::chrono::steady_clock::now();
            // While the first list of a hold settles the map has a larger share, so that it settles
            // soon and on what the map has judged rather than on its while; a later map, with a list
            // standing meanwhile, keeps the frame rate.
            const double budget = g_settle.Settled() || g_settle.Keeping() ? kFloodBudgetMs : kFloodFirstMs;
            const auto overBudget = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() > budget; };
            const auto passable = [&](grid::Cell a, grid::Cell& b) { return GridStepFree(pawn, a, b); };
            auto state = g_reach.flood->Current();
            while (state == grid::Flood::State::Running)
            {
                // A few cells at a time: a cell on new ground costs up to eight sweeps.
                state = g_reach.flood->Step(4, passable);
                if (overBudget()) break;
            }
            const bool done = state != grid::Flood::State::Running;
            if (done)
            {
                g_reach.doneAt = now;
                log::Info(L"explore: the reach map is done: {} cells opened, {} sweeps, {:.1f} s, {}", g_reach.flood->Expanded(),
                          g_grid.sweeps - g_reach.sweepsAtStart, now - g_reach.startedAt,
                          state == grid::Flood::State::Budget ? L"cut short by its budget"
                          : g_reach.flood->RadiusReached()    ? L"cut short by its bound"
                                                              : L"every cell within reach opened");
            }
            CountArea();
            if (done && !g_zones.empty())
            {
                std::vector<std::wstring> covered;
                for (size_t i = 0; i < g_zones.size(); ++i)
                {
                    if (g_reach.area.Cells(static_cast<int>(i)) > 0)
                        covered.push_back(std::format(L"{} {}{}", g_zones[i].name, g_reach.area.Cells(static_cast<int>(i)),
                                                      g_reach.area.Covers(static_cast<int>(i)) ? L"" : L" (too few)"));
                }
                log::Info(L"explore: the reach map's ground lies in the rooms {}", covered.empty() ? L"none" : str::Join(covered, L", "));
            }
            JudgeByReach(now, done);
        }

        // Begins a new map when the one there is no longer the character's: none yet, a done one
        // whose ground she has left or whose start she has walked well away from, or one begun
        // before the story last moved on.
        void KeepReachMap(UObject* pawn, double now)
        {
            if (!pawn || ReachRunning()) return;
            // A search begun before any map (the hand-over's first look chose a target) gives way:
            // the map judges every target, the search one.
            if (!g_reach.flood && g_grid.search)
            {
                log::Verbose(L"explore: the search for \"{}\" gives way to the reach map", obj::ObjectName(g_grid.searchFor));
                g_grid.search.reset();
                g_grid.searchFor = nullptr;
            }
            if (g_grid.search) return;
            Vec middle;
            double halfHeight = 88.0;
            if (!CapsuleSpan(pawn, middle, halfHeight)) return;
            const wchar_t* why = nullptr;
            if (!g_reach.flood)
                why = L"none yet";
            else if (g_grid.storyMovedAt > g_reach.startedAt)
                why = L"the story moved on";
            else if (!OnMappedGround(middle, halfHeight))
                why = L"the character left the ground it reached";
            else if (FlatDistance(middle, g_reach.origin) > kRemapDistance)
                why = L"the character walked away from where it began";
            else if (g_reach.flood->Expanded() < kEnclosedMinCells && now - g_reach.doneAt > 5.0)
                why = L"the last one opened too little ground to judge by";
            if (!why) return;
            log::Verbose(L"explore: a new reach map, {}", why);
            StartReachMap(pawn, now);
        }

        // What the sweeps out of the cell a failed search got nearest to ran into, swept again
        // for the log: the game's own names for it tell a wall from a thing that moves.
        void LogBlockers(UObject* pawn, grid::Cell nearest)
        {
            std::vector<std::wstring> seen;
            for (int dy = -1; dy <= 1; ++dy)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    if (dx == 0 && dy == 0) continue;
                    const auto w = g_grid.why.find(std::tuple<int, int, int, int, int>{nearest.x, nearest.y, nearest.level, nearest.x + dx, nearest.y + dy});
                    if (w == g_grid.why.end() || w->second != 'W') continue;
                    double floorA = 0.0, floorB = 0.0;
                    if (!FloorAt(nearest, floorA) || !FloorFrom(pawn, grid::Cell{nearest.x + dx, nearest.y + dy, 0}, LevelOf(floorA), floorB)) continue;
                    std::wstring what;
                    SweepClear(pawn, SweepMiddleOf(nearest), SweepMiddleOf(grid::Cell{nearest.x + dx, nearest.y + dy, LevelOf(floorB)}), g_grid.radius,
                               SweepHalfHeight(), &what);
                    if (!what.empty() && std::find(seen.begin(), seen.end(), what) == seen.end()) seen.push_back(what);
                }
            }
            if (!seen.empty()) log::Info(L"explore: what stopped it there: {}", str::Join(seen, L"; "));
        }

        // Follows the way felt out: the corner that ends the stretch the character stands
        // on is the one to walk to; strayed from it, or without one, it is felt out anew.
        void FollowGrid(UObject* pawn, const Vec& here, Target& t, double now, bool request)
        {
            const bool fresh = !t.gridCorners.empty() && FlatDistance(Aim(t), t.gridGoal) < 50.0;
            bool following = false;
            if (fresh)
            {
                const int on = ways::StretchOn(t.gridCorners, t.gridCorners.size(), here, 150.0, ways::kStretchRise);
                if (on >= 0)
                {
                    size_t next = static_cast<size_t>(on);
                    while (next + 1 < t.gridCorners.size() && FlatDistance(here, t.gridCorners[next]) < 60.0)
                        ++next;
                    t.hasRoute = true;
                    t.routeNext = t.gridCorners[next];
                    double length = FlatDistance(here, t.gridCorners[next]);
                    for (size_t i = next + 1; i < t.gridCorners.size(); ++i)
                        length += FlatDistance(t.gridCorners[i - 1], t.gridCorners[i]);
                    t.routeLength = length;
                    following = true;
                }
            }
            // A way that ends short of its target is felt out again now and then while it is
            // followed, without leaving it: what stood in the way may have gone. The reach map's
            // verdicts are not: a new map judges them when there is a reason to.
            const bool retry = following && t.gridPartial && !JudgedByMap(t) && now - t.gridAt > 8.0;
            if (following && !retry) return;
            if (!request) return;
            if (ReachRunning()) return;
            // No map yet where the ground is felt out: one is about to begin and judges this
            // target with the rest.
            if (!g_reach.flood && !g_meshHere) return;
            if (JudgedByMap(t))
            {
                // Strayed from the road the map gave (it runs from where the map began): a new map
                // from here gives the road from here.
                if (t.reach == 1 && !GridBusy() && !g_meshHere) StartReachMap(pawn, now);
                return;
            }
            if (!retry && now - t.gridFailedAt < 8.0) return;
            if (GridSearching(t.actor)) return;
            // The chosen target's way is felt out again in place of another's in the
            // background, which would otherwise always be running.
            if (retry && GridBusy() && t.actor != g_selected) return;
            // The chosen target's search is not interrupted for another's.
            if (GridBusy() && g_grid.searchFor == g_selected && t.actor != g_selected) return;
            RequestGridRoute(pawn, t, now);
        }

        // While nothing else is felt out, the other targets' ways are, one at a time: then
        // every distance spoken is a distance on foot, and a way on that lies on the way to
        // a thing can be told.
        size_t g_feltOutNext = 0;

        void FeelOutNextTarget(UObject* pawn, double now)
        {
            // Only what the reach map could not judge, once it is done (cut short by its budget or
            // its bound), and only while the list holds no way at all: a search goes on toward one
            // place, past where the map stopped. A way the map did not reach while others are
            // listed lies past them, and a new map judges it once the character has gone on.
            if (GridBusy() || ReachRunning() || !g_reach.flood || !pawn || g_targets.empty()) return;
            if (std::any_of(g_targets.begin(), g_targets.end(), [](const Target& t) { return t.way && !t.hidden; })) return;
            Vec here;
            if (!ActorLocation(pawn, here)) return;
            for (size_t tried = 0; tried < g_targets.size(); ++tried)
            {
                Target& t = g_targets[(g_feltOutNext + tried) % g_targets.size()];
                if (t.actor == g_selected || t.reached || JudgedByMap(t)) continue;
                // A way that ends short of its target ages sooner than a whole one.
                const bool fresh = !t.gridCorners.empty() && FlatDistance(Aim(t), t.gridGoal) < 50.0 && !(t.gridPartial && now - t.gridAt > 30.0);
                if (fresh || now - t.gridFailedAt < 30.0) continue;
                g_feltOutNext = (g_feltOutNext + tried + 1) % g_targets.size();
                RequestGridRoute(pawn, t, now);
                return;
            }
        }

        // Asks the mesh again when the answer has aged or the character has walked on. Where
        // the mesh gives no road and the target is one the player is after (`feelOut`), the
        // ground felt out with the character's own collision stands in.
        void EnsureRoute(UObject* pawn, const Vec& here, Target& t, double now, double maxAge, bool feelOut = false)
        {
            if (now - t.routeAt < maxAge && FlatDistance(here, t.routeFrom) < 200.0 && FlatDistance(Aim(t), t.routeGoal) < 50.0)
            {
                if (!t.hasRoute) FollowGrid(pawn, here, t, now, feelOut);
                return;
            }
            const Route route = FindRoute(pawn, here, t, nullptr);
            t.routeAt = now;
            t.routeFrom = here;
            t.routeGoal = Aim(t);
            t.hasRoute = route.valid && !(route.partial && Distance(route.end, route.goal) > 300.0);
            t.routeLength = route.length;
            t.routeNext = route.next;
            if (!t.hasRoute) FollowGrid(pawn, here, t, now, feelOut);
        }

        // The road the walk to a target takes, as far as it is known to carry the character,
        // at her feet: on a path the game holds her to, the path itself up to the target or to
        // where the path ends; where the level has a navigation mesh, its road; else the way
        // felt out over the ground, from the stretch she stands on up to the cell nearest the
        // target that the search reached. The straight leg on from there is not known to
        // carry her, and a road not felt out yet is not known at all: empty.
        std::vector<Vec> KnownRoad(UObject* pawn, const Vec& here, const Target& t, double halfHeight)
        {
            std::vector<Vec> road;
            if (g_path.set)
            {
                if (AheadOnPath(here, Aim(t))) road = PathRoad(here, Aim(t));
                return road;
            }
            if (NavigationAvailable())
            {
                FindRoute(pawn, here, t, &road);
                if (road.size() >= 2) return road;
                road.clear();
            }
            // The map's road where this map judged the target, pulled straight over the map alike for
            // every target: the chosen one's, swept again for the walk, would otherwise judge the list
            // differently from the rest.
            const bool mapped = JudgedByMap(t) && t.mapRoad.size() >= 2;
            const auto& corners = mapped ? t.mapRoad : t.gridCorners;
            if (corners.size() < 2 || FlatDistance(Aim(t), t.gridGoal) >= 50.0) return road;
            const size_t known = mapped ? t.mapWalkable : (t.gridPartial ? t.gridWalkable : corners.size());
            const size_t walkable = std::min(known, corners.size());
            if (walkable < 2) return road;
            const int on = ways::StretchOn(corners, walkable, here, 150.0, ways::kStretchRise);
            if (on < 0) return road;
            const size_t from = static_cast<size_t>(on);
            road.push_back(Vec{here.x, here.y, here.z - halfHeight});
            for (size_t i = from; i < walkable; ++i)
                road.push_back(Vec{corners[i].x, corners[i].y, corners[i].z - g_grid.capsuleHalf});
            return road;
        }

        double SpokenDistance(const Target& t)
        {
            return t.hasRoute ? std::max(t.distance, t.routeLength) : t.distance;
        }

        double SpokenBearing(const Target& t, const Vec& here, double yaw)
        {
            return t.hasRoute ? BearingDegrees(here, t.routeNext, yaw) : t.bearing;
        }

        // ---- what is said -------------------------------------------------------------------

        std::wstring Bearings(const Target& t, const Vec& here, double yaw)
        {
            std::vector<std::wstring> where{locale::Mod(SectorKey(SpokenBearing(t, here, yaw)))};
            const double rise = t.position.z - here.z;
            const int storey = t.way && t.hasBox ? t.storey : (rise > 150.0 ? 1 : (rise < -150.0 ? -1 : 0));
            if (storey > 0)
                where.push_back(locale::Mod(L"explore.higher"));
            else if (storey < 0)
                where.push_back(locale::Mod(L"explore.lower"));
            return str::Join(where, L", ");
        }

        std::wstring TargetText(const Target& t, const Vec& here, double yaw)
        {
            if (t.reached) return locale::Mod(L"explore.reached", t.label);
            return locale::Mod(L"explore.target", {t.label, Metres(SpokenDistance(t)), Bearings(t, here, yaw)});
        }

        // What the scanner says of the target it lands on: the target as ever, and, at the
        // detailed level, its place among the targets listed ("1 of 5") after where it is.
        std::wstring ScannerText(const Target& t, const Vec& here, double yaw)
        {
            if (!cfg::Detailed()) return TargetText(t, here, yaw);
            size_t position = 0;
            size_t total = 0;
            for (const auto& other : g_targets)
            {
                if (other.hidden) continue;
                ++total;
                if (other.actor == t.actor) position = total;
            }
            if (position == 0) return TargetText(t, here, yaw);
            const auto place = locale::Mod(L"ui.pos", {std::to_wstring(position), std::to_wstring(total)});
            if (t.reached) return locale::Mod(L"explore.reached.pos", {t.label, place});
            return locale::Mod(L"explore.target.pos", {t.label, Metres(SpokenDistance(t)), Bearings(t, here, yaw), place});
        }

        std::wstring ListText()
        {
            if (g_targets.empty()) return locale::Mod(L"explore.none");
            UObject* pawn = Pawn();
            Vec here;
            double yaw = 0.0;
            const bool placed = pawn && ActorLocation(pawn, here) && CameraYaw(yaw);
            const double now = gamethread::NowSeconds();
            std::vector<Target*> ordered;
            for (auto& t : g_targets)
            {
                if (!t.hidden) ordered.push_back(&t);
            }
            if (ordered.empty()) return locale::Mod(L"explore.none");
            if (placed)
            {
                size_t asked = 0;
                for (auto* t : ordered)
                {
                    if (++asked > 10) break;
                    EnsureRoute(pawn, here, *t, now, 3.0);
                }
            }
            std::sort(ordered.begin(), ordered.end(), [](const Target* a, const Target* b) { return SpokenDistance(*a) < SpokenDistance(*b); });
            std::vector<std::wstring> parts{locale::Mod(L"explore.nearby")};
            size_t n = 0;
            for (const auto* t : ordered)
            {
                if (++n > 8) break;
                const double bearing = placed ? SpokenBearing(*t, here, yaw) : t->bearing;
                parts.push_back(locale::Mod(L"explore.target", {t->label, Metres(SpokenDistance(*t)), locale::Mod(SectorKey(bearing))}));
            }
            return str::JoinSentences(parts);
        }

        std::wstring HintText()
        {
            using hotkeys::Command;
            return locale::Mod(L"explore.hint", {hotkeys::SpokenKey(Command::NextTarget), hotkeys::SpokenKey(Command::PreviousTarget),
                                                 hotkeys::SpokenKey(Command::Walk), hotkeys::SpokenKey(Command::Where), hotkeys::SpokenKey(Command::Beacon)});
        }

        void StraightenRoad(Target& t, const std::vector<grid::Cell>& cells, bool partial, double now);

        // The chosen target's road from the reach map is pulled straight, so that the walk and the
        // beacon take the same corners and not the map's cells.
        void StraightenChosen(Target& t, double now)
        {
            if (JudgedByMap(t) && !t.reachCells.empty() && !t.straightened) StraightenRoad(t, t.reachCells, t.gridPartial, now);
        }

        void Select(Target* t, UObject* pawn, const Vec& here, double yaw, bool say)
        {
            g_selected = t ? t->actor : nullptr;
            g_beaconAt = -10.0;
            g_choiceDue = false;
            if (!t) return;
            StraightenChosen(*t, gamethread::NowSeconds());
            if (pawn) EnsureRoute(pawn, here, *t, gamethread::NowSeconds(), 0.0, true);
            if (say) speech::Now(TargetText(*t, here, yaw));
        }

        // The mod's own choice is said at once, or, when it is a way while the list has not settled
        // yet (a way the list kept from before, as a later map settles), once it has: a nearer way
        // said and then taken back for a farther one along its road is a way that appears for a
        // moment and is gone.
        // What the mod says of a target it chose: its road from where she stands now, the same
        // N and H give a moment later (said from an older road or the straight line, the
        // distance and the side changed under the player's next press).
        std::wstring ChoiceText(UObject* pawn, Target& t, const Vec& here, double yaw, double now)
        {
            if (pawn) EnsureRoute(pawn, here, t, now, 0.0, true);
            return TargetText(t, here, yaw);
        }

        void SayChoice(UObject* pawn, Target& t, const Vec& here, double yaw, double now)
        {
            if (t.way && !g_settle.Settled())
            {
                g_choiceDue = true;
                log::Info(L"explore: the choice of \"{}\" {} is said once the reach map has settled the list", t.label, obj::ObjectName(t.actor));
                return;
            }
            speech::Announce(ChoiceText(pawn, t, here, yaw, now));
        }

        // ---- the beacon -------------------------------------------------------------------

        void Beacon(double bearing, double distance, double now)
        {
            if (!g_beacon || now - g_beaconAt < cfg::Get().beaconIntervalMs / 1000.0) return;
            g_beaconAt = now;
            const double radians = bearing * std::numbers::pi / 180.0;
            const double level = 1.0 - std::clamp(distance / 3000.0, 0.0, 1.0);
            sounds::Beacon(std::sin(radians), level, std::cos(radians) < 0.0);
        }

        // ---- the game's own test of a press -----------------------------------------------

        // The game takes the interaction key at a use location while the verdict its tick
        // keeps holds (bOverlappingCharacterCanInteract).
        // The tick asks ABatesUseLocation::CanCharacterInteract of the character overlapping
        // it: the linked activity on, the character the player's, no interaction in progress,
        // the character not blending out of what it was bound to, the character inside the use
        // location's volume by its own trigger component, and, when the use location asks for
        // a look, IsInLookAtAngle. That passes when the use location needs no glint, fails when
        // it has none, passes on every camera layer but the navigation (1) and framing (4)
        // ones, and otherwise asks IsInLookAtAngleCheck. The game's use locations
        // (BP_BatesUseLocation) answer that with the verdict their glint keeps,
        // bGlintAuthorizesInteraction: the glint (BP_GlintActor::GetTargetIntensity, every tick)
        // sets it while its intensity is full, which is also when its widget shows the prompt.
        // Through the exploration camera that takes the character overlapping the use
        // location, the glint's widget on the screen, the character facing the glint
        // (GetCharacterLookIntensity: its billboard within CharacterLookAngle of her forward,
        // 110 degrees) and the billboard inside three quarters of the screen's inscribed ellipse
        // from the middle (GetCameraLookIntensity), with a clear line from the camera where the
        // glint asks for one; through any other camera the same but the ellipse. Each part is
        // asked of the glint's own functions, so that the walk and the guidance know what is
        // missing. A use location without that verdict is judged by the native test: the
        // camera's yaw against the way from the character to the glint over the ground, and the
        // character's forward against the way from the camera to the glint, each within the use
        // location's angle. A use location that starts by itself is never ticked: walking in
        // starts it.
        struct PressTest
        {
            bool known = false;
            bool verdict = false; // the game's own, as its tick left it
            bool enabled = false;
            bool inProgress = false;
            bool blending = false;
            bool inside = false;
            bool autoStart = false;
            bool needCamera = false;
            bool needCharacter = false;
            bool needGlint = false;
            bool glint = false;
            int64_t layer = -1;       // the camera's active layer, -1 when unset
            bool measured = false;    // the look is measured: asked for, a glint, layer 1 or 4
            double limit = 180.0;     // the angle the use location allows on this layer
            double cameraAngle = 0.0; // between the camera's yaw and the way to the glint
            double characterAngle = 0.0;
            double cameraTurn = 0.0;    // how far the camera must turn, right positive
            double cameraRise = 0.0;    // how far it must tilt, up positive
            double characterTurn = 0.0; // how far the character must turn, right positive
            // Where the thing lies from her in the frame the stick works in (the control rotation),
            // right positive: the stick pushed that way turns her toward it, whatever the camera.
            double stickTurn = 0.0;
            bool cameraOk = true;
            bool characterOk = true;
            // The look as the glint judges it.
            bool byGlint = false;         // the use location takes its glint's verdict
            bool glintVerdict = false;    // bGlintAuthorizesInteraction
            bool explorationView = false; // the view is the exploration camera's
            bool overlap = false;         // the use location overlaps the character
            bool onScreen = false;        // the glint's widget is on the screen
            bool insideEllipse = false;   // the billboard lies in the part of the screen the camera's look asks for
            bool facing = false;          // the character faces the billboard
            bool sightBlocked = false;    // the line from the camera to the billboard is blocked
            bool LookOk() const
            {
                return byGlint ? glintVerdict : cameraOk && characterOk;
            }
            // Everything but a look is in place: the character stands where the press is taken.
            bool AtHand() const
            {
                return enabled && !inProgress && !blending && inside;
            }
        };

        // The parts the guidance weighs (features/LookTurn).
        look::Parts PartsOf(const PressTest& p)
        {
            look::Parts parts;
            parts.measured = p.measured;
            parts.atHand = p.AtHand();
            parts.verdict = p.verdict;
            parts.lookOk = p.LookOk();
            parts.byGlint = p.byGlint;
            parts.explorationView = p.explorationView;
            parts.onScreen = p.onScreen;
            parts.cameraOk = p.cameraOk;
            parts.characterOk = p.characterOk;
            parts.needCamera = p.needCamera;
            return parts;
        }

        // Whether the character stands inside a volume by its own trigger component, the
        // test the game's interact test asks.
        bool InsideTriggerVolume(UObject* pawn, UObject* volume)
        {
            UObject* trigger = nullptr;
            if (!obj::ReadObject(pawn, L"TriggerComponent", trigger) || !obj::IsLive(trigger)) return false;
            auto* fn = obj::FindFunction(trigger, L"IsInsideTriggerVolume");
            if (!fn) return false;
            bool inside = false;
            obj::Call(
                trigger, fn,
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"Volume") *static_cast<UObject**>(obj::ValuePtrAt(params, prop)) = volume;
                    }
                },
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"ReturnValue") obj::ReadBoolAt(params, prop, inside);
                    }
                });
            return inside;
        }

        // The angle a use location allows on a camera layer: its override for the layer,
        // looked up as the game looks it up in InteractionAngleOverrides (a map of layer to
        // angle: its elements 16 bytes apart, the layer at +0, the angle at +4, the next of its
        // hash chain at +8; the map's element count at +0x8 and its free count at +0x34, equal
        // when it is empty; its hash at +0x40, or inline at +0x38, and the hash's size at
        // +0x48), else its InteractionAngle.
        double InteractionAngleFor(UObject* useLocation, int64_t layer)
        {
            double angle = 180.0;
            obj::ReadFloat(useLocation, L"InteractionAngle", angle);
            auto* prop = obj::FindProperty(useLocation, L"InteractionAngleOverrides");
            const auto* map = prop ? static_cast<const uint8_t*>(obj::ValuePtr(useLocation, prop)) : nullptr;
            if (!map || layer < 0) return angle;
            const int32_t count = *reinterpret_cast<const int32_t*>(map + 0x08);
            const int32_t freeCount = *reinterpret_cast<const int32_t*>(map + 0x34);
            const int32_t hashSize = *reinterpret_cast<const int32_t*>(map + 0x48);
            if (count == freeCount || count <= 0 || hashSize <= 0 || hashSize > 4096 || (hashSize & (hashSize - 1)) != 0) return angle;
            const auto* hash = *reinterpret_cast<const int32_t* const*>(map + 0x40);
            if (!hash) hash = reinterpret_cast<const int32_t*>(map + 0x38);
            const auto* elements = *reinterpret_cast<const uint8_t* const*>(map);
            int32_t index = hash[layer & (hashSize - 1)];
            for (int guard = 0; elements && index >= 0 && index < count && guard < 64; ++guard)
            {
                const uint8_t* element = elements + static_cast<size_t>(index) * 16;
                if (element[0] == static_cast<uint8_t>(layer)) return *reinterpret_cast<const float*>(element + 4);
                index = *reinterpret_cast<const int32_t*>(element + 8);
            }
            return angle;
        }

        // Where the camera looks from, its yaw and its pitch, as the game's look test takes them
        // (APlayerCameraManager::GetCameraViewPoint: the camera's cached view).
        bool CameraView(Vec& location, double& yaw, double* pitchOut = nullptr)
        {
            UObject* camera = CameraManager();
            double pitch = 0.0;
            const bool known = camera && VecReturn(camera, L"GetCameraLocation", location) && RotationOf(camera, L"GetCameraRotation", pitch, yaw);
            if (pitchOut) *pitchOut = pitch;
            return known;
        }

        // The actor the player's view is through (the controller's view target), and the
        // exploration camera the game compares it with (UExplorationCameraUtils::
        // GetExplorationCamera: the controller's own).
        UObject* ViewTarget()
        {
            UObject* controller = Controller();
            UObject* view = nullptr;
            if (controller)
                obj::CallReturn(controller, L"GetViewTarget", [&](void* params, FProperty* returnValue) { obj::ReadObjectAt(params, returnValue, view); });
            return obj::IsLive(view) ? view : nullptr;
        }

        UObject* ExplorationCameraActor()
        {
            UObject* library = obj::FindObject(L"/Script/Bates.Default__ExplorationCameraUtils");
            auto* fn = library ? obj::FindFunction(library, L"GetExplorationCamera") : nullptr;
            UObject* pawn = Pawn();
            if (!fn || !pawn) return nullptr;
            UObject* camera = nullptr;
            obj::Call(
                library, fn,
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (!prop || prop->GetName() == L"ReturnValue") continue;
                        const auto type = obj::PropertyTypeName(prop);
                        if (type == L"ObjectProperty")
                            *static_cast<UObject**>(obj::ValuePtrAt(params, prop)) = pawn;
                        else if (type == L"BoolProperty")
                            obj::WriteBoolAt(params, prop, true);
                    }
                },
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"ReturnValue") obj::ReadObjectAt(params, prop, camera);
                    }
                });
            return obj::IsLive(camera) ? camera : nullptr;
        }

        // Calls a function of the game's with object and bool arguments set by name, and reads
        // the bools it hands back by name; false when it cannot be called.
        bool CallNamed(UObject* target, const wchar_t* function, std::initializer_list<std::pair<const wchar_t*, UObject*>> objects,
                       std::initializer_list<std::pair<const wchar_t*, bool>> bools, std::initializer_list<std::pair<const wchar_t*, bool*>> results)
        {
            auto* fn = obj::IsLive(target) ? obj::FindFunction(target, function) : nullptr;
            if (!fn) return false;
            return obj::Call(
                target, fn,
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (!prop) continue;
                        const auto name = prop->GetName();
                        for (const auto& [n, value] : objects)
                        {
                            if (name == n) *static_cast<UObject**>(obj::ValuePtrAt(params, prop)) = value;
                        }
                        for (const auto& [n, value] : bools)
                        {
                            if (name == n) obj::WriteBoolAt(params, prop, value);
                        }
                    }
                },
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (!prop) continue;
                        const auto name = prop->GetName();
                        for (const auto& [n, out] : results)
                        {
                            if (name == n) obj::ReadBoolAt(params, prop, *out);
                        }
                    }
                });
        }

        // The look as the glint judges it, part by part (see the test above); false when the use
        // location keeps no glint verdict.
        bool ReadGlintLook(UObject* pawn, UObject* useLocation, UObject* glint, PressTest& p)
        {
            bool verdict = false;
            if (!obj::ReadBool(useLocation, L"bGlintAuthorizesInteraction", verdict)) return false;
            p.byGlint = true;
            p.glintVerdict = verdict;
            UObject* view = ViewTarget();
            p.explorationView = view && view == ExplorationCameraActor();
            CallNamed(useLocation, L"IsOverlappingActor", {{L"Other", pawn}}, {}, {{L"ReturnValue", &p.overlap}});
            UObject* widget = nullptr;
            p.onScreen = obj::ReadObject(glint, L"GlintWidget", widget) && obj::IsLive(widget) && obj::CallForBool(widget, L"GetIsOnScreen");
            CallNamed(glint, L"GetCharacterLookIntensity", {{L"Character", pawn}}, {}, {{L"FacingGlint", &p.facing}});
            p.insideEllipse = true;
            if (p.explorationView)
                CallNamed(glint, L"GetCameraLookIntensity", {{L"ViewTarget", view}}, {{L"Inside UseLocation", p.overlap}},
                          {{L"Inside Angle", &p.insideEllipse}});
            bool lineOfSight = false;
            double intensity = 0.0;
            obj::ReadBool(glint, L"bUseLineOfSightCheck", lineOfSight);
            obj::ReadFloat(glint, L"CurrentIntensity", intensity);
            p.sightBlocked = p.explorationView && lineOfSight && intensity < -0.5;
            p.cameraOk = p.onScreen && p.insideEllipse && !p.sightBlocked;
            p.characterOk = p.facing;
            // Inside by both of the game's tests: the interact test's trigger component, and the
            // glint's overlap.
            p.inside = p.inside && p.overlap;
            return true;
        }

        // The glint of a use location, which the game's look-at test is measured to. Every object
        // is looked through for it at most every few seconds: a use location may have none.
        std::map<UObject*, double> g_glintMissedAt;

        UObject* GlintOf(UObject* useLocation)
        {
            if (const auto known = g_glintOf.find(useLocation); known != g_glintOf.end() && obj::IsLive(known->second)) return known->second;
            const double now = gamethread::NowSeconds();
            if (const auto missed = g_glintMissedAt.find(useLocation); missed != g_glintMissedAt.end() && now - missed->second < 5.0) return nullptr;
            for (auto* glint : obj::FindAllLive(L"BatesGlint"))
            {
                UObject* owner = nullptr;
                obj::CallReturn(glint, L"GetOwningUseLocation", [&](void* params, FProperty* returnValue) { obj::ReadObjectAt(params, returnValue, owner); });
                if (owner) g_glintOf[owner] = glint;
            }
            const auto found = g_glintOf.find(useLocation);
            if (found != g_glintOf.end() && obj::IsLive(found->second)) return found->second;
            g_glintMissedAt[useLocation] = now;
            return nullptr;
        }

        // Where the game's look-at test points: the glint's billboard, else the glint itself,
        // else the use location.
        Vec LookAtPoint(const Target& t)
        {
            Vec at;
            if (UObject* glint = GlintOf(t.actor))
            {
                UObject* billboard = nullptr;
                if (obj::ReadObject(glint, L"GlintBillboardComponent", billboard) && ComponentLocation(billboard, at)) return at;
                if (ActorLocation(glint, at)) return at;
            }
            return t.position;
        }

        PressTest ReadPressTest(UObject* pawn, UObject* useLocation)
        {
            PressTest p;
            if (!pawn || !obj::IsLive(useLocation) || !obj::IsA(useLocation, L"BatesUseLocation")) return p;
            const auto flag = [](UObject* object, const wchar_t* member)
            {
                const auto* at = static_cast<const uint8_t*>(sym::MemberOf(object, member));
                return at && *at != 0;
            };
            p.known = true;
            p.enabled = flag(useLocation, L"ABatesUseLocation.bIsLinkedActivityEnabled");
            // CanOverlappingCharacterInteract (0x44beae0): the activity and the tick's flag together.
            p.verdict = p.enabled && flag(useLocation, L"ABatesUseLocation.bOverlappingCharacterCanInteract");
            p.inProgress = flag(useLocation, L"ABatesUseLocation.bInteractionInProgress");
            obj::ReadBool(useLocation, L"bStartInteractionAutomatically", p.autoStart);
            obj::ReadBool(useLocation, L"bRequireCameraLookAt", p.needCamera);
            obj::ReadBool(useLocation, L"bRequireCharacterLookAt", p.needCharacter);
            obj::ReadBool(useLocation, L"bRequiresGlint", p.needGlint);
            UObject* bindable = nullptr;
            if (obj::ReadObject(pawn, L"BindableCharacterComponent", bindable) && obj::IsLive(bindable))
                p.blending = flag(bindable, L"UBatesBindableCharacterComponent.bIsBlending");
            p.inside = InsideTriggerVolume(pawn, useLocation);
            if ((!p.needCamera && !p.needCharacter) || !p.needGlint) return p;
            UObject* glint = GlintOf(useLocation);
            p.glint = glint != nullptr;
            if (!p.glint)
            {
                p.cameraOk = false;
                p.characterOk = false;
                return p;
            }
            p.layer = ActiveCameraLayer();
            if (p.layer != 1 && p.layer != 4) return p;
            Vec camera, character, mark;
            double cameraYaw = 0.0, cameraPitch = 0.0, pitch = 0.0, yaw = 0.0;
            UObject* billboard = nullptr;
            if (!CameraView(camera, cameraYaw, &cameraPitch) || !ActorLocation(pawn, character) || !RotationOf(pawn, L"K2_GetActorRotation", pitch, yaw) ||
                !obj::ReadObject(glint, L"GlintBillboardComponent", billboard) || !ComponentLocation(billboard, mark))
                return p;
            p.measured = true;
            // The turns that bring the billboard to the middle of the view, and the character's
            // forward onto it.
            p.cameraTurn = BearingDegrees(camera, mark, cameraYaw);
            p.cameraRise = NormalizeDegrees(ElevationDegrees(camera, mark) - cameraPitch);
            p.characterTurn = BearingDegrees(character, mark, yaw);
            double controlYaw = cameraYaw;
            ControlYaw(controlYaw);
            p.stickTurn = BearingDegrees(character, mark, controlYaw);
            if (ReadGlintLook(pawn, useLocation, glint, p)) return p;
            p.limit = InteractionAngleFor(useLocation, p.layer);
            const auto degrees = [](double dot) { return std::acos(std::clamp(dot, -1.0, 1.0)) * 180.0 / std::numbers::pi; };
            // The camera: its yaw's forward against the way from the character over the ground.
            const double gx = mark.x - character.x, gy = mark.y - character.y;
            const double ground = std::sqrt(gx * gx + gy * gy);
            const double cameraRad = cameraYaw * std::numbers::pi / 180.0;
            p.cameraAngle = ground > 1e-4 ? degrees((gx * std::cos(cameraRad) + gy * std::sin(cameraRad)) / ground) : 90.0;
            p.cameraTurn = BearingDegrees(character, mark, cameraYaw);
            // The character: its forward against the way from the camera, within the whole length.
            const double cx = mark.x - camera.x, cy = mark.y - camera.y, cz = mark.z - camera.z;
            const double whole = std::sqrt(cx * cx + cy * cy + cz * cz);
            const double pitchRad = pitch * std::numbers::pi / 180.0, faceRad = yaw * std::numbers::pi / 180.0;
            const double fx = std::cos(pitchRad) * std::cos(faceRad), fy = std::cos(pitchRad) * std::sin(faceRad);
            p.characterAngle = whole > 1e-4 ? degrees((fx * cx + fy * cy) / whole) : 90.0;
            p.characterTurn = BearingDegrees(camera, mark, yaw);
            p.stickTurn = BearingDegrees(camera, mark, controlYaw);
            p.cameraOk = !p.needCamera || p.cameraAngle <= p.limit;
            p.characterOk = !p.needCharacter || p.characterAngle <= p.limit;
            return p;
        }

        // The use location a press would go to now: of those the character overlaps that would take
        // it, the one whose start arrow lies nearest her over the ground, as the press itself picks
        // (UPlayerInteractionComponent::FindClosestInteractableUseLocation, 0x46132e0, by
        // ABatesUseLocation::GetSqDistanceFromTarget, 0x44e30b0); null when none would.
        UObject* PressGoesTo(UObject* pawn)
        {
            Vec here;
            if (!pawn || !ActorLocation(pawn, here)) return nullptr;
            UObject* best = nullptr;
            double bestDistance = 0.0;
            for (UObject* use : OfferedUseLocations(pawn))
            {
                const auto* enabled = static_cast<const uint8_t*>(sym::MemberOf(use, L"ABatesUseLocation.bIsLinkedActivityEnabled"));
                const auto* can = static_cast<const uint8_t*>(sym::MemberOf(use, L"ABatesUseLocation.bOverlappingCharacterCanInteract"));
                if (!enabled || !can || !*enabled || !*can) continue;
                UObject* arrow = nullptr;
                Vec start;
                if (!obj::ReadObject(use, L"StartLocation", arrow) || !ComponentLocation(arrow, start)) continue;
                const double distance = FlatDistance(here, start);
                if (!best || distance < bestDistance)
                {
                    best = use;
                    bestDistance = distance;
                }
            }
            return best;
        }

        // The parts of the test for the log; without the angles (`angles` false) it is the
        // key the log is written on, so that only a change of a part is logged.
        std::wstring PressTestText(const PressTest& p, bool angles = true)
        {
            if (!p.known) return L"not a use location";
            std::wstring text = std::format(L"the game's verdict {}; activity {}, in progress {}, blending {}, inside {}{}", p.verdict ? L"yes" : L"no",
                                            p.enabled, p.inProgress, p.blending, p.inside, p.autoStart ? L", starts by itself" : L"");
            if (!p.needCamera && !p.needCharacter) return text + L"; no look asked";
            if (!p.needGlint) return text + L"; a look asked, but no glint needed, so none measured";
            if (!p.glint) return text + L"; a look asked and no glint: never taken";
            if (!p.measured) return text + std::format(L"; a look asked, not measured on camera layer {}", p.layer);
            if (p.byGlint)
            {
                const auto yes = [](bool b) { return b ? L"yes" : L"no"; };
                text += std::format(L"; the glint's verdict {} on layer {}: {} camera, overlapping {}, on the screen {}, within the ellipse {}, facing {}{}",
                                    yes(p.glintVerdict), p.layer, p.explorationView ? L"the exploration" : L"another", yes(p.overlap), yes(p.onScreen),
                                    yes(p.insideEllipse), yes(p.facing), p.sightBlocked ? L", the line of sight blocked" : L"");
                if (angles)
                    text +=
                        std::format(L"; camera turn {:+.0f} deg, tilt {:+.0f} deg, character turn {:+.0f} deg", p.cameraTurn, p.cameraRise, p.characterTurn);
                return text;
            }
            if (!angles)
                return text + std::format(L"; within {:.0f} deg on layer {}: camera {}, character {}", p.limit, p.layer,
                                          !p.needCamera ? L"not asked"
                                          : p.cameraOk  ? L"on"
                                                        : L"off",
                                          !p.needCharacter ? L"not asked"
                                          : p.characterOk  ? L"on"
                                                           : L"off");
            return text + std::format(L"; within {:.0f} deg on layer {}: camera {} at {:.0f} deg (turn {:+.0f}){}, character {} at {:.0f} deg (turn {:+.0f}){}",
                                      p.limit, p.layer, p.needCamera ? L"asked" : L"not asked", p.cameraAngle, p.cameraTurn, p.cameraOk ? L"" : L" OFF",
                                      p.needCharacter ? L"asked" : L"not asked", p.characterAngle, p.characterTurn, p.characterOk ? L"" : L" OFF");
        }

        // ---- walking there by itself ------------------------------------------------------

        bool MechanicShown()
        {
            return ChoiceInProgress() || QteInProgress() || DontMoveInProgress();
        }

        void StopWalk(const wchar_t* what, bool askedFor)
        {
            if (!g_walking) return;
            g_walking = false;
            log::Info(L"explore: walking ended{}{}", what ? L": " : L"", what ? what : L"");
            input::ReleaseWalkKeys();
            if (!what) return;
            if (askedFor)
                speech::Now(locale::Mod(what));
            else
                speech::Announce(locale::Mod(what));
        }

        // How far the way is clear from one point toward another: a capsule the character's
        // size swept between them, with the collision of a pawn, which is what the game
        // stops the character with. A sweep that starts touching a wall reports a hit at
        // once whichever way it goes; the character's own movement pulls back out of such a
        // touch before it sweeps, and so does this, once, by the depth the engine reports.
        // -1 when the engine cannot be asked.
        // `hitWhat`, when asked for, names what the sweep ran into: the component, its actor
        // and its collision profile, for the log.
        // `reached`, when asked for, is where the capsule's middle got to, after any pull-back:
        // what a step's nearness to its cell is measured by.
        // A sweep of her capsule as a passage's walk asks it (ways::Sweep): another character met is
        // passed by, as on the ground.
        ways::SweepHit PassageSweep(UObject* pawn, const Vec& from, const Vec& to)
        {
            ways::SweepHit out;
            if (!Prepare(g_capsuleTrace, L"CapsuleTraceSingleByProfile"))
            {
                out.hit = true;
                return out;
            }
            TraceHit hit;
            RunTrace(g_capsuleTrace, pawn, from, to, g_grid.radius, SweepHalfHeight(), hit);
            ++g_grid.sweeps;
            if (!hit.hit || OtherCharacter(pawn, hit.component)) return out;
            out.hit = true;
            out.distance = hit.distance;
            out.normal = hit.normal;
            out.penetrating = hit.penetrating;
            out.depth = hit.depth;
            return out;
        }

        double SweepClear(UObject* pawn, const Vec& middle, const Vec& end, double radius, double halfHeight, std::wstring* hitWhat, Vec* reached,
                          UObject** hitComponent)
        {
            if (!Prepare(g_capsuleTrace, L"CapsuleTraceSingleByProfile")) return -1.0;
            if (hitComponent) *hitComponent = nullptr;
            Vec from = middle;
            Vec to = end;
            for (int attempt = 0; attempt < 2; ++attempt)
            {
                const double distance = Distance(from, to);
                TraceHit hit;
                RunTrace(g_capsuleTrace, pawn, from, to, radius, halfHeight, hit);
                if (hitWhat)
                {
                    hitWhat->clear();
                    if (hit.hit && obj::IsLive(hit.component))
                    {
                        std::wstring profile;
                        obj::CallReturn(hit.component, L"GetCollisionProfileName",
                                        [&](void* p, FProperty* returnValue)
                                        {
                                            if (returnValue) obj::ReadStringAt(p, returnValue, profile);
                                        });
                        UObject* owner = obj::Outer(hit.component);
                        *hitWhat = std::format(L"{} {} of {} {}, profile {}", obj::ClassName(hit.component), obj::ObjectName(hit.component),
                                               obj::ClassName(owner), obj::ObjectName(owner), profile.empty() ? L"?" : profile);
                    }
                }
                const auto along = [&](double d)
                {
                    if (!reached) return;
                    const double f = distance > 0.0 ? d / distance : 0.0;
                    *reached = Vec{from.x + (to.x - from.x) * f, from.y + (to.y - from.y) * f, from.z + (to.z - from.z) * f};
                };
                if (!hit.hit)
                {
                    along(distance);
                    return distance;
                }
                if (!hit.penetrating || attempt == 1)
                {
                    const double clear = std::clamp(hit.distance, 0.0, distance);
                    along(clear);
                    if (hitComponent) *hitComponent = hit.component;
                    return clear;
                }
                const double push = hit.depth + 1.0;
                from = Vec{from.x + hit.normal.x * push, from.y + hit.normal.y * push, from.z + hit.normal.z * push};
                to = Vec{to.x + hit.normal.x * push, to.y + hit.normal.y * push, to.z + hit.normal.z * push};
            }
            return 0.0;
        }

        // The walk's last stretch at a use location the game offers. Once the game would take
        // the press the keys are let go and the walk waits for the character to stand still,
        // since she slides on and may slide out of the place: the totem of the lodge on
        // 2026-10-24 had its prompt up for half a second, gone as she came to rest, and the
        // press was refused while the camera pointed at it. A press lost so, or missing all
        // along for want of a step further in or of facing the thing, is won as a player wins
        // it: flicks of the stick, or taps of the keys (look::Flicker), toward the glint once she
        // is inside and must face it, which turn her on the spot, or toward the spot the game made
        // for her, which turn her that way and then step her there. A look the camera must give is
        // the player's, which the aim sound leads once the walk is over.
        double g_arriveSince = -1.0; // since when the walk is on its last stretch
        double g_stretchMapAt = -1.0; // when a map was begun from where the last stretch found her (look::FreshSpotMap)
        constexpr double kStretchMapWait = 1.5; // how long the stretch waits for that map to reach the place
        look::Rest g_rest;     // she comes to rest where the press would be taken, the keys let go
        look::Rest g_lookRest; // she comes to rest where only the camera's look is missing
        look::Flicker g_flicker;
        unsigned long long g_flickFrame = 0; // the frame a flick's push was given on
        std::wstring g_arriveLogged;

        // Her own yaw, for the flicks; 0 when it cannot be read.
        double FacingYaw(UObject* pawn)
        {
            double pitch = 0.0, yaw = 0.0;
            RotationOf(pawn, L"K2_GetActorRotation", pitch, yaw);
            return yaw;
        }

        // A cell of the place the ground was proven to carry her to, for the last stretch's steps
        // (look::StepSpot): the reach map's, else the end of the way felt out to it.
        Vec StepSpotOf(const Target& t)
        {
            std::optional<Vec> mapped, felt;
            if (JudgedByMap(t) && t.reach == 1 && !t.reachCells.empty()) mapped = WorldOf(t.reachCells.back());
            if (!t.gridPartial && t.gridWalkable >= 1 && t.gridWalkable <= t.gridCorners.size() && FlatDistance(Aim(t), t.gridGoal) < 50.0)
                felt = t.gridCorners[t.gridWalkable - 1];
            return look::StepSpot(mapped, felt, Aim(t));
        }

        // Runs the last stretch; false while the walk is still on its way there.
        bool PollArrival(UObject* pawn, Target& t, const Vec& here, double yaw, double now)
        {
            // Once begun, the last stretch keeps her near her spot though she slides out of the place
            // (look::StretchKeeps): flicks step her back in. A way felt out to within a step of its
            // end begins it at once.
            const Vec spot = StepSpotOf(t);
            const double fromSpot = FlatDistance(here, spot);
            const double wayLeft = t.hasRoute ? t.routeLength : 1e9;
            if (t.way || !obj::IsLive(t.actor) || !look::StretchKeeps(t.inside, g_arriveSince >= 0.0, fromSpot, wayLeft))
            {
                if (g_arriveSince >= 0.0 && !t.way)
                    log::Info(L"explore: the walk's last stretch to \"{}\" lets her go {:.0f} cm from her spot", t.label, fromSpot);
                g_arriveSince = -1.0;
                return false;
            }
            const PressTest p = ReadPressTest(pawn, t.actor);
            if (g_arriveSince < 0.0)
            {
                g_arriveSince = now;
                g_stretchMapAt = -1.0;
                g_rest.Reset();
                g_lookRest.Reset();
                g_flicker.Reset(now);
                g_arriveLogged.clear();
                if (!t.inside)
                {
                    log::Info(L"explore: the walk's last stretch to \"{}\" begins {:.0f} cm from a spot of its place the ground carries her to", t.label,
                              fromSpot);
                    // The spot is where the map first touched the place, on its road from wherever it
                    // began; a map begun from where she stands touches it nearest her
                    // (look::FreshSpotMap).
                    if (pawn && g_reach.flood && !ReachRunning() && look::FreshSpotMap(FlatDistance(g_reach.origin, here)))
                    {
                        log::Info(L"explore: the map was begun {:.0f} cm off; a new one from where she stands finds the place's nearest ground",
                                  FlatDistance(g_reach.origin, here));
                        StartReachMap(pawn, now);
                        g_stretchMapAt = now;
                    }
                }
            }
            const double facing = FacingYaw(pawn);
            // The walk's own measures of getting nearer start over while the last stretch has it,
            // so that losing the offer afterwards is not taken for a stall.
            g_walkMeasure = {0, 0.0};
            g_walkMovingAt = now;
            g_walkLeanedAt = now;
            const auto parts = PressTestText(p, false);
            if (parts != g_arriveLogged)
            {
                g_arriveLogged = parts;
                log::Info(L"explore: the walk's last stretch to \"{}\": {}", t.label, PressTestText(p));
            }
            const bool still = CharacterSpeed(pawn) < 10.0;
            // Ready, or starting by itself: she is let go and comes to rest (look::Rest); a
            // stopping step that takes her out of the place again starts the rest over.
            if (p.verdict || p.autoStart)
            {
                g_flicker.Hold(now, facing);
                input::ReleaseWalkKeys();
                if (!g_rest.Update(now, still)) return true;
                // A press goes to the nearest of the things that would take it: a neighbour there
                // takes it instead, and nearer this one she cannot get.
                UObject* goesTo = p.autoStart ? t.actor : PressGoesTo(pawn);
                if (goesTo && goesTo != t.actor)
                {
                    std::wstring other;
                    for (const auto& o : g_targets)
                    {
                        if (o.actor == goesTo) other = o.label;
                    }
                    if (other.empty())
                    {
                        std::wstring coreName;
                        other = UseLocationLabel(goesTo, coreName);
                    }
                    log::Info(L"explore: at \"{}\" a press would go to {}, which lies nearer", t.label, other);
                    StopWalk(nullptr, false);
                    speech::Announce(locale::Mod(L"explore.walk.other", t.label, other));
                    return true;
                }
                log::Info(L"explore: the walk to \"{}\" ends where the press is taken, she at rest", t.label);
                StopWalk(nullptr, false);
                return true;
            }
            g_rest.Reset();
            if (now - g_arriveSince > 8.0)
            {
                log::Info(L"explore: the walk to \"{}\" ends without the press taken: {}", t.label, PressTestText(p));
                // Inside, a look is what is missing, and the guidance says so once she stands still;
                // outside, she could not get in.
                StopWalk(p.inside ? nullptr : L"explore.walk.blocked", false);
                return true;
            }
            // Something of the game's own keeps the press off (the activity, a binding still
            // blending, an interaction in progress), or everything is in place and the game's
            // verdict is a tick behind: she waits where she is.
            if (!p.enabled || p.inProgress || p.blending || (p.inside && p.LookOk()))
            {
                g_flicker.Hold(now, facing);
                input::ReleaseWalkKeys();
                return true;
            }
            // Only the camera's look is missing: that turn is the player's, once she has come to
            // rest in the place (look::Rest); a stopping step that takes her out of it hands her
            // back to the steps below.
            if (p.inside && p.characterOk)
            {
                g_flicker.Hold(now, facing);
                input::ReleaseWalkKeys();
                if (!g_lookRest.Update(now, still)) return true;
                log::Info(L"explore: the walk to \"{}\" ends there, she at rest, the camera to look at it", t.label);
                StopWalk(nullptr, false);
                return true;
            }
            g_lookRest.Reset();
            // The press asks for a glint and the thing has none: no turn of hers wins it.
            if (p.inside && p.needGlint && (p.needCamera || p.needCharacter) && !p.glint)
            {
                g_flicker.Hold(now, facing);
                input::ReleaseWalkKeys();
                return true;
            }
            // Her turn toward the glint, or a step further in toward her spot: flicks, in the frame
            // the stick works in.
            // A map begun for the stretch has yet to reach the place from where she stands: its spot
            // is the one to step toward, and it comes within a moment.
            if (g_stretchMapAt >= 0.0 && !JudgedByMap(t) && ReachRunning() && now - g_stretchMapAt < kStretchMapWait)
            {
                g_flicker.Hold(now, facing);
                input::ReleaseWalkKeys();
                return true;
            }
            const double bearing = BearingDegrees(here, p.inside ? LookAtPoint(t) : spot, yaw);
            const auto push = g_flicker.Update(now, bearing, facing, CharacterSpeed(pawn), here);
            if (g_flicker.Began())
                log::Info(L"explore: the walk's last stretch to \"{}\": {} {} toward {:+.0f} deg, to {} (she faces {:+.0f} deg off the glint; at ({:.0f}, {:.0f}), "
                          L"{:.0f} cm from her spot at ({:.0f}, {:.0f}){})",
                          t.label, push && push->hold > 0.0 ? L"held push" : L"flick", g_flicker.Count(), bearing,
                          p.inside ? L"turn her to the glint" : L"step her toward her spot", p.characterTurn, here.x, here.y, fromSpot, spot.x, spot.y,
                          g_flicker.Dead() > 0 ? std::format(L"; {} flick(s) before it did nothing", g_flicker.Dead()) : std::wstring());
            if (push)
            {
                input::HoldWalkKeys(push->forward, push->right);
                g_flickFrame = gamethread::FrameCount();
            }
            else
            {
                input::ReleaseWalkKeys();
            }
            return true;
        }

        void PollWalk(double now)
        {
            if (!g_walking) return;
            if (!gate::IsOn())
            {
                StopWalk(nullptr, false);
                return;
            }
            UObject* pawn = Pawn();
            Target* target = Selected();
            if (!g_exploring || !pawn || !target)
            {
                StopWalk(nullptr, false);
                return;
            }
            // Arriving at a way on speaks for itself; at a use location the last stretch
            // (PollArrival) goes on until the press would be taken where she stands.
            if (target->way && target->reached)
            {
                StopWalk(nullptr, false);
                return;
            }
            if (gamestate::Paused() || watch::CurrentScreen() != nullptr || MechanicShown() || !g_control)
            {
                StopWalk(nullptr, false);
                return;
            }
            Vec here;
            if (!ActorLocation(pawn, here)) return;
            const double delay = cfg::Get().walkDelayMs / 1000.0;
            if (now - g_walkStartedAt > delay + 0.7 && input::MovementHeld())
            {
                StopWalk(L"explore.walk.stop", true);
                return;
            }
            if (now - g_walkStartedAt > 120.0)
            {
                StopWalk(L"explore.walk.stop", false);
                return;
            }
            if (now - g_walkStartedAt < delay)
            {
                g_walkMovingAt = now;
                g_walkLeanedAt = now;
                return;
            }
            // The game reads the stick in the frame of the control rotation, which the
            // exploration camera keeps at its own.
            double yaw = 0.0;
            if (!ControlYaw(yaw) && !CameraYaw(yaw)) return;
            if (PollArrival(pawn, *target, here, yaw, now)) return;
            EnsureRoute(pawn, here, *target, now, 0.5, true);
            if (target->hasRoute && FlatDistance(here, target->routeNext) < 80.0) EnsureRoute(pawn, here, *target, now, 0.0, true);
            // Where the level has no mesh the way is felt out first, and the character waits
            // for it rather than setting off into the furniture. The walk then takes the way
            // the beacon leads along: the way felt out, as far as it was found to go, and on
            // from there straight to the target; with no way known at all, straight, as the
            // beacon points. Whether the character really cannot get there is for the walk
            // itself to find: it says so when she stops getting nearer.
            const double feltSince = ReachRunning() ? g_reach.startedAt : g_grid.searchStartedAt;
            if (!target->hasRoute && !g_path.set && GridSearching(target->actor) && now - feltSince < 20.0)
            {
                // She stands while she waits: the keys of the last heading are let go.
                input::ReleaseWalkKeys();
                g_walkMovingAt = now;
                g_walkBestWayAt = now;
                if (!g_walkWaitLogged)
                {
                    g_walkWaitLogged = true;
                    log::Info(L"explore: the walk to \"{}\" waits for the way to be felt out", target->label);
                }
                return;
            }
            // The way still to go, measured as the walk goes: along the path the game holds
            // her to, by the way felt out, or straight. Another measure, or another way felt
            // out, starts the test of getting nearer over, since the numbers do not compare.
            double way = target->distance;
            int measure = 3;
            if (g_path.set)
            {
                way = std::max(0.0, PathAhead(here, Aim(*target)));
                measure = 1;
            }
            else if (target->hasRoute)
            {
                way = target->routeLength;
                measure = 2;
            }
            const auto measured = std::make_tuple(measure, target->gridAt);
            if (measured != g_walkMeasure)
            {
                g_walkMeasure = measured;
                g_walkBestWay = way;
                g_walkBestWayAt = now;
            }
            if (way < g_walkBestWay - 30.0)
            {
                g_walkBestWay = way;
                g_walkBestWayAt = now;
            }
            else if (now - g_walkBestWayAt > 6.0)
            {
                log::Info(L"explore: the way to \"{}\" stopped growing shorter at {:.0f} cm{}", target->label, way, target->reached ? L", at the thing" : L"");
                StopWalk(target->reached ? nullptr : L"explore.walk.blocked", false);
                return;
            }
            // At the end of the way with the game offering a neighbour instead of the
            // target there is nowhere nearer to go.
            const auto offered = OfferedUseLocations(pawn);
            if (way < 60.0 && !target->way && !offered.empty() && std::find(offered.begin(), offered.end(), target->actor) == offered.end())
            {
                std::wstring other;
                for (const auto& t : g_targets)
                {
                    if (t.actor == offered.front()) other = t.label;
                }
                if (other.empty())
                {
                    std::wstring coreName;
                    other = UseLocationLabel(offered.front(), coreName);
                }
                log::Info(L"explore: at the end of the way to \"{}\" the game offers {}", target->label, other);
                StopWalk(nullptr, false);
                speech::Announce(locale::Mod(L"explore.walk.other", target->label, other));
                return;
            }
            // The heading is the corner of the way itself, in the frame the game reads the
            // stick in; the way was felt out with the character's own collision, so nothing
            // is steered round on the spot. Stuck for a moment, the heading leans to one
            // side and then the other, the way a player feels around a doorway. Held to a
            // path, the heading is the path's forward, which is all the game takes.
            const Vec goal = target->hasRoute ? target->routeNext : Aim(*target);
            double bearing = BearingDegrees(here, goal, yaw) + g_walkLean;
            if (g_path.set)
            {
                const double ahead = PathAhead(here, Aim(*target));
                if (ahead < -kLevelOnPath)
                {
                    log::Info(L"explore: \"{}\" lies behind on the path the game holds the character to", target->label);
                    StopWalk(L"explore.walk.blocked", false);
                    return;
                }
                // Level with a thing beside the path she is as near as the path goes; the game
                // has not offered it (that is the last stretch's), so this is as far as a walk
                // gets her.
                if (ahead < kLevelOnPath && !target->way)
                {
                    log::Info(L"explore: \"{}\" is level with the character on the path the game holds her to, and not offered", target->label);
                    StopWalk(L"explore.walk.blocked", false);
                    return;
                }
                bearing = PathBearing(yaw);
            }
            const double radians = bearing * std::numbers::pi / 180.0;
            if (!input::HoldWalkKeys(std::cos(radians), std::sin(radians)))
            {
                StopWalk(L"explore.walk.stop", false);
                return;
            }
            const double speed = CharacterSpeed(pawn);
            if (speed > 15.0)
            {
                g_walkMovingAt = now;
                g_walkLean = 0.0;
            }
            if (now - g_walkLoggedAt >= 1.0)
            {
                if (FlatDistance(here, g_walkLastPosition) > 20.0) g_walkMovingAt = now;
                g_record.Line(ground::PositionLine(ground::Position{here.x, here.y, here.z}));
                log::Info(L"explore: walking to \"{}\": {:.0f} cm straight, way {} {:.0f} cm, corner {:.0f} cm at {:.0f} deg and {:+.0f} cm up, heading "
                          L"{:.0f} deg{}, speed {:.0f}, moved {:.0f} cm, at ({:.0f}, {:.0f}, {:.0f})",
                          target->label, target->distance, target->hasRoute ? (target->gridPartial ? L"partly felt out" : L"through") : L"straight", way,
                          FlatDistance(here, goal), BearingDegrees(here, goal, yaw), goal.z - here.z, bearing,
                          g_walkLean != 0.0 ? std::format(L" (leaning {:+.0f})", g_walkLean) : L"", speed, FlatDistance(here, g_walkLastPosition), here.x,
                          here.y, here.z);
                g_walkLoggedAt = now;
                g_walkLastPosition = here;
            }
            // Nothing gained for a moment: try a little to one side, then the other, the way
            // a player feels around a doorway.
            if (now - g_walkMovingAt > 1.2 && now - g_walkLeanedAt > 0.9)
            {
                static const double leans[] = {0.0, 40.0, -40.0, 75.0, -75.0};
                size_t next = 0;
                for (size_t i = 0; i < std::size(leans); ++i)
                {
                    if (std::fabs(leans[i] - g_walkLean) < 1.0) next = (i + 1) % std::size(leans);
                }
                g_walkLean = leans[next];
                g_walkLeanedAt = now;
            }
            if (now - g_walkMovingAt > 8.0) StopWalk(target->reached ? nullptr : L"explore.walk.blocked", false);
        }

        void StartWalk()
        {
            if (!g_exploring) return;
            Target* target = Selected();
            if (!target)
            {
                speech::Now(locale::Mod(L"explore.notarget"));
                return;
            }
            UObject* pawn = Pawn();
            Vec here;
            if (!pawn || !ActorLocation(pawn, here)) return;
            // There already: a way on she stands in, a use location whose press would be taken.
            // Offered but not yet taking the press, the walk nudges her the rest of the way.
            if (target->reached && (target->way || ReadPressTest(pawn, target->actor).verdict))
            {
                speech::Now(locale::Mod(L"explore.reached", target->label));
                return;
            }
            g_walking = true;
            g_arriveSince = -1.0;
            StraightenChosen(*target, gamethread::NowSeconds());
            g_autoChosen = false; // walking to it makes it the player's choice
            g_walkStartedAt = gamethread::NowSeconds();
            g_walkMovingAt = g_walkStartedAt;
            g_walkLeanedAt = g_walkStartedAt;
            g_walkLean = 0.0;
            g_walkBestWay = 1e9;
            g_walkBestWayAt = g_walkStartedAt;
            g_walkMeasure = {0, 0.0};
            g_walkLoggedAt = g_walkStartedAt;
            g_walkLastPosition = here;
            g_walkWaitLogged = false;
            EnsureRoute(pawn, here, *target, g_walkStartedAt, 0.0, true);
            double cameraYaw = 0.0, controlYaw = 0.0;
            CameraYaw(cameraYaw);
            ControlYaw(controlYaw);
            const Route route = FindRoute(pawn, here, *target, nullptr);
            log::Info(
                L"explore: walking to \"{}\" at ({:.0f}, {:.0f}, {:.0f}) from ({:.0f}, {:.0f}, {:.0f}); the way is {}; camera yaw {:.0f}, control yaw {:.0f}",
                target->label, Aim(*target).x, Aim(*target).y, Aim(*target).z, here.x, here.y, here.z,
                route.valid ? std::format(L"{} through the mesh, {:.0f} cm", route.partial ? L"partial" : L"complete", route.length)
                            : L"felt out over the ground",
                cameraYaw, controlYaw);
            speech::Now(locale::Mod(L"explore.walk.start"));
        }

        // ---- who has the character --------------------------------------------------------

        // The widgets the game lays over the whole picture while it changes scenes: the
        // loading screen, a film played full screen, and the fade to black. Under any of them
        // the player sees no scene, whatever the character's state says: at the start of the
        // prologue the game was in play, the character unbound and the camera free for a
        // tenth of a second while the loading screen and the fade still covered everything
        // (2026-10-24), and for three quarters of a second under the new story's opening film
        // (2026-10-28), which the new level's HUD does not hold: the film the level's loading
        // put up is a widget of its own, seen by the screen watcher by its class.
        UObject* g_loadingScreen = nullptr; // the loading screen, once the screen watcher has seen it

        UObject* Hud()
        {
            UObject* controller = Controller();
            UObject* hud = nullptr;
            if (!controller || !obj::ReadObject(controller, L"MyHUD", hud) || !obj::IsLive(hud)) return nullptr;
            return hud;
        }

        // What covers the picture now, for the log; empty when nothing does. A fade counts
        // while it goes to black or holds it; one coming back from black uncovers the scene.
        std::wstring Cover()
        {
            if (g_loadingScreen && obj::IsLive(g_loadingScreen) && obj::IsWidgetShown(g_loadingScreen)) return L"the loading screen";
            // Every full-screen film the screen watcher sees, each asked now: a level's loading can put
            // up more than one, and the last one seen was not the one on the screen at chapter one's
            // start (2026-11-01).
            for (UObject* film : watch::ShownWidgetsOf(L"BatesFullscreenVideoWidget"))
            {
                if (obj::IsWidgetShown(film)) return L"a film";
            }
            UObject* hud = Hud();
            if (!hud) return {};
            UObject* video = nullptr;
            if (obj::ReadObject(hud, L"FullscreenVideoWidget", video) && obj::IsLive(video) && obj::IsWidgetShown(video)) return L"a film";
            UObject* fade = nullptr;
            if (obj::ReadObject(hud, L"UIFadeWidget", fade) && obj::IsLive(fade) && obj::IsWidgetShown(fade))
            {
                const auto* toBlack = static_cast<const uint8_t*>(sym::MemberOf(fade, L"UBatesUIFade.bFadeToBlack"));
                if (!toBlack || *toBlack) return L"the fade to black";
            }
            return {};
        }

        // The hold's reasons are read at every frame and weighed by hold::Hold (features/Hold),
        // whose rules are replayed from the logs in the tests. A staircase walking her onto its
        // first step is not the game holding her at all (StairsWalkHer).
        hold::Hold g_hold;
        bool g_gameWalks = false; // the game's own walk, as last logged

        // Whether the character is in the air: her movement component's own mode is falling
        // (EMovementMode MOVE_Falling).
        bool Falling(UObject* pawn)
        {
            UObject* movement = nullptr;
            int64_t mode = 0;
            return pawn && obj::ReadObject(pawn, L"CharacterMovement", movement) && obj::IsLive(movement) && obj::ReadInt(movement, L"MovementMode", mode) &&
                   mode == 3;
        }

        bool GameWalksCharacter(UObject* pawn)
        {
            UObject* movement = nullptr;
            if (!pawn || !obj::ReadObject(pawn, L"CharacterMovement", movement) || !obj::IsLive(movement)) return false;
            const auto* set = static_cast<const uint8_t*>(sym::MemberOf(movement, L"UBatesCharacterMovementComponent.AutoLocoState.IsSet"));
            return set && *set != 0;
        }

        // Whether a staircase is walking the character onto its first step: the stairs put the
        // game's own walk on her and mark their record of her while it lasts
        // (FStairActivityCharacterData.bIsInAutoLoco), then hand her the stairs to climb
        // (binding state 3). That walk is the stairs', not the game taking her back, and it
        // lasts as long as the way to the step: 0.4 s at the lodge's porch one time, 1.4 s
        // another. The record is a value of the stairs' BoundCharacterData, a map whose
        // elements (24 bytes: the key's object index at +0, the record at +8) are counted at
        // +0x08, with their allocation bits inline at +0x10 while there are at most 128 of them
        // (else behind the pointer at +0x20) and that number of bits at +0x2c.
        bool StairsWalkHer(UObject* pawn)
        {
            const size_t flag = sym::Offset(L"FStairActivityCharacterData.bIsInAutoLoco");
            if (!pawn || flag == 0 || !sym::BuildMatches()) return false;
            const int32_t index = pawn->GetInternalIndex();
            for (UObject* stairs : g_stairs)
            {
                if (!obj::IsLive(stairs) || !obj::IsA(stairs, L"BatesStairActivityActor")) continue;
                const auto* map = static_cast<const uint8_t*>(sym::MemberOf(stairs, L"ABatesSpecialLocoActivityActor.BoundCharacterData"));
                if (!map) continue;
                const auto* elements = *reinterpret_cast<const uint8_t* const*>(map);
                const int32_t count = *reinterpret_cast<const int32_t*>(map + 0x08);
                const int32_t maxBits = *reinterpret_cast<const int32_t*>(map + 0x2c);
                if (!elements || count <= 0 || count > 64) continue;
                const auto* bits = maxBits > 128 ? *reinterpret_cast<const uint32_t* const*>(map + 0x20) : reinterpret_cast<const uint32_t*>(map + 0x10);
                if (!bits) continue;
                for (int32_t i = 0; i < count; ++i)
                {
                    if ((bits[i / 32] & (1u << (i % 32))) == 0) continue; // a free slot
                    const uint8_t* element = elements + static_cast<size_t>(i) * 24;
                    if (*reinterpret_cast<const int32_t*>(element) != index) continue;
                    const auto* record = *reinterpret_cast<const uint8_t* const*>(element + 8);
                    if (record && record[flag] != 0) return true;
                }
            }
            return false;
        }
        bool g_stairStep = false; // the stairs' own step, as last logged
        std::wstring g_cover;     // what covered the picture, as last logged

        // Whether the game hands the player the character: a character of the game's own,
        // unbound or in a special locomotion (the two states its movement code takes the
        // player's input in and its glints are drawn in), while the game is in play, the
        // camera is one it explores through and nothing covers the picture. Returns whether
        // the hold changed.
        bool PollControl(double now)
        {
            UObject* pawn = Pawn();
            const auto gameState = gamestate::Refresh();
            const auto stateName = gamestate::Name(gameState);
            if (stateName != g_stateName)
            {
                g_stateName = stateName;
                log::Info(L"explore: the game state is {}", stateName);
            }
            if (gameState == gamestate::State::Paused)
            {
                hold::Frame paused;
                paused.state = hold::State::Paused;
                g_hold.Update(now, paused);
                return false;
            }
            const int64_t binding = BindingState(pawn);
            // The camera must be one the game lets the player explore through, by its own
            // test, and the controller must take movement input: a cutscene played around an
            // unbound character fails one of them while the binding state says nothing.
            const bool cameraFree = pawn && ExplorationCamera();
            const bool inputTaken = pawn && !MoveInputIgnored();
            const bool autoWalk = GameWalksCharacter(pawn);
            const bool stairStep = autoWalk && StairsWalkHer(pawn);
            const bool gameWalks = autoWalk && !stairStep;
            const std::wstring cover = pawn ? Cover() : std::wstring();
            const bool falling = pawn && Falling(pawn);
            // A character the scene has just made the player's, while its script waits before it
            // goes on with her (scripts::Switch), on the play clock, which stands still in a pause
            // as the script's wait does.
            if (pawn != g_switchPawn)
            {
                g_switchPawn = pawn;
                std::wstring name;
                if (pawn) obj::ReadString(pawn, L"CharacterName", name);
                g_switchWait = pawn ? SwitchWait(name) : 0.0;
                g_switchedAt = g_playClock;
                if (g_switchWait > 0.0) log::Info(L"explore: {} is made the player's, and the scene waits {:g} s before it goes on", name, g_switchWait);
            }
            const bool sceneWaits = pawn && g_playClock - g_switchedAt < g_switchWait;
            if (falling != g_falling)
            {
                g_falling = falling;
                if (pawn) log::Info(L"explore: the character {}", falling ? L"is in the air, falling" : L"stands on the ground again");
            }
            if (gameWalks != g_gameWalks || stairStep != g_stairStep)
            {
                g_gameWalks = gameWalks;
                g_stairStep = stairStep;
                log::Info(L"explore: {}", stairStep   ? L"a staircase walks the character onto its first step"
                                          : gameWalks ? L"the game walks the character itself"
                                                      : L"the game no longer walks the character itself");
            }
            const int64_t active = pawn ? ActiveCameraLayer() : -2;
            if (active != g_activeLayer || cameraFree != g_cameraFree || inputTaken != g_inputTaken)
            {
                g_activeLayer = active;
                g_cameraFree = cameraFree;
                g_inputTaken = inputTaken;
                if (pawn)
                    log::Info(L"explore: the camera's active layer is {} ({}), {}; the controller {} movement input", active, CameraLayerName(active),
                              cameraFree ? L"an exploration layer" : L"not an exploration layer", inputTaken ? L"takes" : L"ignores");
            }
            if (cover != g_cover)
            {
                g_cover = cover;
                log::Info(L"explore: {}", cover.empty() ? std::wstring(L"nothing covers the picture") : cover + L" covers the picture");
            }
            hold::Frame frame;
            frame.character = pawn != nullptr;
            frame.state = gameState == gamestate::State::Game ? hold::State::Game : hold::State::Other;
            frame.binding = binding;
            frame.cameraFree = cameraFree;
            frame.inputTaken = inputTaken;
            frame.gameWalks = gameWalks;
            frame.falling = falling;
            frame.sceneWaits = sceneWaits;
            // A thing the game offers where she stands: the press is hers though it ignores movement.
            frame.offered = pawn && !OfferedUseLocations(pawn).empty();
            frame.covered = !cover.empty();
            const bool changed = g_hold.Update(now, frame);
            const bool control = g_hold.Control();
            if (pawn != g_controlPawn)
            {
                g_controlPawn = pawn;
                g_playerCharacter = CharacterIndex(pawn);
                if (pawn)
                {
                    std::wstring name;
                    obj::ReadString(pawn, L"CharacterName", name);
                    log::Info(L"explore: the player's character is {} ({})", name, obj::ObjectName(pawn));
                }
                else
                {
                    log::Info(L"explore: the player has no character");
                }
            }
            if (binding != g_binding)
            {
                g_binding = binding;
                if (pawn) log::Info(L"explore: {} is {} (binding state {})", obj::ObjectName(pawn), BindingName(binding), binding);
            }
            const int64_t layer = CameraLayer();
            if (layer != g_cameraLayer)
            {
                g_cameraLayer = layer;
                log::Info(L"explore: the camera is on the {} layer", CameraLayerName(layer));
            }
            g_control = control;
            if (changed)
            {
                using hold::Why;
                const Why reason = g_hold.Reason();
                const auto why = reason == Why::None          ? std::wstring()
                                 : reason == Why::NoCharacter ? std::wstring(L": no character")
                                 : reason == Why::Covered     ? L": " + cover + L" covers the picture"
                                 : reason == Why::NotInPlay   ? L": the game is " + stateName
                                 : reason == Why::Bound       ? std::format(L": the character is {}", BindingName(binding))
                                 : reason == Why::Camera      ? std::wstring(L": the camera is not one to explore through")
                                 : reason == Why::GameWalks   ? std::wstring(L": the game has walked the character itself for a second")
                                 : reason == Why::Falling     ? std::wstring(L": the character has fallen through the air for a second")
                                 : reason == Why::SceneWaits  ? std::wstring(L": the scene that made her the player's waits before it goes on")
                                                              : std::wstring(L": the controller has ignored movement input for a second");
                log::Info(L"explore: the game {} the character{}", control ? L"hands the player" : L"takes back", why);
                if (control) ForgetGround();
                if (control && !g_navLogged)
                {
                    g_navLogged = true;
                    LogNavigationData();
                }
                if (!control) g_navLogged = false;
            }
            return changed;
        }

        // The tones of the hand-over and of the taking back, played once the roaming has read
        // the frame: the hand-over as exploration begins, the taking back when the hold ends
        // after a hand-over was heard. Every hold sounds, whatever there is to walk to: the
        // tone says the character is the player's, and open ground is as much the player's.
        void SoundControl()
        {
            if (g_controlHeard)
            {
                if (g_control) return;
                g_controlHeard = false;
                if (g_hold.TakeSilentEnd())
                {
                    log::Info(L"explore: the hold ended while the game was paused, and the taking back is not sounded");
                    return;
                }
                log::Info(L"explore: the taking back sounds");
                sounds::Play(sounds::Cue::ControlLost);
                return;
            }
            g_hold.ForgetSilentEnd();
            if (!g_control || !g_exploring) return;
            g_controlHeard = true;
            const auto listed = std::count_if(g_targets.begin(), g_targets.end(), [](const Target& t) { return !t.hidden; });
            log::Info(L"explore: the hand-over sounds, with {} to walk to", listed);
            sounds::Play(sounds::Cue::ControlGained);
        }

        // ---- the hold to a path -------------------------------------------------------------

        // The game's hold of the character to a path: the spline-limited locomotion the
        // forest's triggers put her in (the movement component's LimitedLocomotionState).
        // While it is set the movement code bends the stick by the path's forward
        // (ApplyLimitedLocomotionLimits): a push within the angle limits goes as pushed, one
        // beyond the grace limit is thrown away, one between is bent onto the limit. The
        // way is then the path itself, and nothing behind on it can be walked to.
        //
        // The state (FBatesLimitedLocomotionState, 0x110 bytes, from the PDB) begins with its
        // parameters, whose first member is the spline actor as a soft pointer (its weak half
        // at 0x0, set once the game has resolved it) and whose angle limits lie at 0x30; then
        // the transform of the character's point on the spline (rotation at 0x80, translation
        // at 0xa0), the distance along the spline (a float at 0xe0) and the dot limits (0xe8,
        // 0xf0). The transform is filled in by the first movement update after the hold
        // begins; until then it is the identity at the world's origin, and the hold is not
        // taken as begun: its forward and its point would both be wrong.

        LimitedPath ReadLimitedPath(UObject* pawn)
        {
            LimitedPath p;
            UObject* movement = nullptr;
            if (!pawn || !obj::ReadObject(pawn, L"CharacterMovement", movement) || !obj::IsLive(movement)) return p;
            const auto* set = static_cast<const uint8_t*>(sym::MemberOf(movement, L"UBatesCharacterMovementComponent.LimitedLocomotionState.IsSet"));
            const auto* state = static_cast<const uint8_t*>(sym::MemberOf(movement, L"UBatesCharacterMovementComponent.LimitedLocomotionState"));
            if (!set || !state || !*set) return p;
            const auto* quat = reinterpret_cast<const double*>(state + 0x80);
            const auto* translation = reinterpret_cast<const double*>(state + 0xa0);
            const double x = quat[0], y = quat[1], z = quat[2], w = quat[3];
            const bool blank = std::fabs(x) < 1e-9 && std::fabs(y) < 1e-9 && std::fabs(z) < 1e-9 && std::fabs(translation[0]) < 1e-3 &&
                               std::fabs(translation[1]) < 1e-3 && std::fabs(translation[2]) < 1e-3;
            if (blank) return p;
            p.set = true;
            p.forward = Vec{1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y + w * z), 2.0 * (x * z - w * y)};
            p.at = Vec{translation[0], translation[1], translation[2]};
            const auto* limits = reinterpret_cast<const double*>(state + 0x30);
            p.angleLeft = limits[0];
            p.angleRight = limits[1];
            p.lowerDot = *reinterpret_cast<const double*>(state + 0xe8);
            p.upperDot = *reinterpret_cast<const double*>(state + 0xf0);
            p.along = *reinterpret_cast<const float*>(state + 0xe0);
            UObject* spline = reinterpret_cast<const RC::Unreal::FWeakObjectPtr*>(state)->Get();
            if (spline && obj::IsLive(spline) && obj::IsA(spline, L"BatesSplineActor")) p.spline = spline;
            return p;
        }

        // How far along a spline a point lies, by the spline actor's own function.
        bool SplineDistance(UObject* spline, const Vec& point, double& distance)
        {
            auto* fn = obj::IsLive(spline) ? obj::FindFunction(spline, L"GetDistanceForWorldLocationAlongSpline") : nullptr;
            if (!fn) return false;
            bool read = false;
            obj::Call(
                spline, fn,
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"WorldLocation") WriteVec(obj::ValuePtrAt(params, prop), prop, point);
                    }
                },
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"ReturnValue") read = obj::ReadFloatAt(params, prop, distance);
                    }
                });
            return read;
        }

        // The spline actor of the path: the one whose nearest point to the character is the
        // point the state holds.
        UObject* FindPathSpline(const LimitedPath& path)
        {
            for (auto* spline : obj::FindAllLive(L"BatesSplineActor"))
            {
                auto* fn = obj::FindFunction(spline, L"GetNearestTransformOnSpline");
                if (!fn) continue;
                Vec nearest;
                bool read = false;
                obj::Call(
                    spline, fn,
                    [&](void* params)
                    {
                        for (auto* prop : fn->ForEachProperty())
                        {
                            if (prop && prop->GetName() == L"WorldLocation") WriteVec(obj::ValuePtrAt(params, prop), prop, path.at);
                        }
                    },
                    [&](void* params)
                    {
                        for (auto* prop : fn->ForEachProperty())
                        {
                            if (!prop || prop->GetName() != L"ReturnValue") continue;
                            auto* translation = obj::StructMember(prop, L"Translation");
                            if (translation) read = ReadVec(obj::ValuePtrAt(obj::ValuePtrAt(params, prop), translation), translation, nearest);
                        }
                    });
                if (read && Distance(nearest, path.at) < 50.0) return spline;
            }
            return nullptr;
        }

        // Which way along the spline the path's forward runs: the spline's distance a step
        // ahead of the path's point against a step behind it. A spline runs whichever way it
        // was drawn, and the game walks some of them backwards.
        int PathSignOf(const LimitedPath& path)
        {
            if (!g_pathSpline) return 0;
            const Vec ahead{path.at.x + path.forward.x * 100.0, path.at.y + path.forward.y * 100.0, path.at.z + path.forward.z * 100.0};
            const Vec back{path.at.x - path.forward.x * 100.0, path.at.y - path.forward.y * 100.0, path.at.z - path.forward.z * 100.0};
            double atAhead = 0.0, atBack = 0.0;
            if (!SplineDistance(g_pathSpline, ahead, atAhead) || !SplineDistance(g_pathSpline, back, atBack)) return 0;
            if (std::fabs(atAhead - atBack) < 20.0) return 0;
            return atAhead > atBack ? 1 : -1;
        }

        // How far ahead of the character a point lies on the path, in centimetres (behind is
        // negative): along its spline in the direction the game lets her walk, or, without
        // the spline or its direction, along the path's forward.
        double PathAhead(const Vec& here, const Vec& point)
        {
            double atHere = 0.0, atPoint = 0.0;
            if (g_pathSpline && g_pathSign != 0 && SplineDistance(g_pathSpline, here, atHere) && SplineDistance(g_pathSpline, point, atPoint))
                return (atPoint - atHere) * g_pathSign;
            return Dot(Minus(point, here), g_path.forward);
        }

        // Whether a point can be walked to on the path: ahead of the character, or level
        // with her.
        bool AheadOnPath(const Vec& here, const Vec& point)
        {
            if (!g_path.set) return true;
            return PathAhead(here, point) >= -kLevelOnPath;
        }

        // The direction the game lets the character walk on the path, in the camera's frame:
        // where the walk pushes the stick, and where the beacon leads.
        double PathBearing(double yaw)
        {
            const double pathYaw = std::atan2(g_path.forward.y, g_path.forward.x) * 180.0 / std::numbers::pi;
            return NormalizeDegrees(pathYaw - yaw);
        }

        UObject* PathSplineComponent()
        {
            if (!g_pathSpline || !obj::IsLive(g_pathSpline)) return nullptr;
            UObject* component = nullptr;
            obj::CallReturn(g_pathSpline, L"GetSplineComponent",
                            [&](void* params, FProperty* returnValue)
                            {
                                if (returnValue) obj::ReadObjectAt(params, returnValue, component);
                            });
            return component && obj::IsLive(component) ? component : nullptr;
        }

        // A number into a parameter of a call, at whichever width the engine declares it.
        void WriteNumberAt(void* params, FProperty* prop, double value)
        {
            const auto type = obj::PropertyTypeName(prop);
            void* at = obj::ValuePtrAt(params, prop);
            if (type == L"FloatProperty")
                *static_cast<float*>(at) = static_cast<float>(value);
            else if (type == L"DoubleProperty")
                *static_cast<double*>(at) = value;
            else if (type == L"IntProperty")
                *static_cast<int32_t*>(at) = static_cast<int32_t>(value);
            else if (type == L"ByteProperty" || type == L"EnumProperty")
                *static_cast<uint8_t*>(at) = static_cast<uint8_t>(value);
        }

        bool SplineLength(UObject* component, double& length)
        {
            bool read = false;
            obj::CallReturn(component, L"GetSplineLength",
                            [&](void* params, FProperty* returnValue)
                            {
                                if (returnValue) read = obj::ReadFloatAt(params, returnValue, length);
                            });
            return read;
        }

        bool SplineLocation(UObject* component, double distance, Vec& out)
        {
            auto* fn = obj::FindFunction(component, L"GetLocationAtDistanceAlongSpline");
            if (!fn) return false;
            bool read = false;
            obj::Call(
                component, fn,
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (!prop) continue;
                        const auto name = prop->GetName();
                        if (name == L"Distance")
                            WriteNumberAt(params, prop, distance);
                        else if (name == L"CoordinateSpace")
                            WriteNumberAt(params, prop, 1.0); // the world's
                    }
                },
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"ReturnValue") read = ReadVec(obj::ValuePtrAt(params, prop), prop, out);
                    }
                });
            return read;
        }

        // The path from the character up to a point ahead on it, or to where the path ends,
        // sampled along the spline every half metre: the road a walk on the path takes.
        std::vector<Vec> PathRoad(const Vec& here, const Vec& point)
        {
            std::vector<Vec> road;
            UObject* component = PathSplineComponent();
            double atHere = 0.0, atPoint = 0.0, length = 0.0;
            if (!component || g_pathSign == 0 || !SplineDistance(g_pathSpline, here, atHere) || !SplineDistance(g_pathSpline, point, atPoint) ||
                !SplineLength(component, length))
                return road;
            const double end = std::clamp(atPoint, 0.0, length);
            if ((end - atHere) * g_pathSign <= 0.0) return road;
            const int steps = std::clamp(static_cast<int>(std::fabs(end - atHere) / 50.0), 1, 200);
            for (int i = 0; i <= steps; ++i)
            {
                Vec p;
                if (SplineLocation(component, atHere + (end - atHere) * i / steps, p)) road.push_back(p);
            }
            return road;
        }

        // The path from the character to its end in the direction she may walk.
        std::vector<Vec> PathRoadToEnd(const Vec& here)
        {
            UObject* component = PathSplineComponent();
            double length = 0.0;
            if (!component || g_pathSign == 0 || !SplineLength(component, length)) return {};
            Vec end;
            if (!SplineLocation(component, g_pathSign > 0 ? length : 0.0, end)) return {};
            return PathRoad(here, end);
        }

        void PollLimitedPath(UObject* pawn, double now)
        {
            const LimitedPath path = ReadLimitedPath(pawn);
            // The spline is the one the game's state names; once the game has resolved it,
            // a guess by the nearest point gives way to it.
            if (path.set && g_path.set && path.spline && path.spline != g_pathSpline)
            {
                g_pathSpline = path.spline;
                g_pathSign = PathSignOf(path);
                log::Info(L"explore: the path's spline is {} by the game's own state, forward {}", obj::ObjectName(g_pathSpline),
                          g_pathSign > 0   ? L"where its distance grows"
                          : g_pathSign < 0 ? L"where its distance shrinks"
                                           : L"by the path's own forward");
            }
            else if (path.set && g_path.set && g_pathSign == 0 && g_pathSpline)
            {
                g_pathSign = PathSignOf(path);
            }
            if (path.set != g_path.set)
            {
                if (path.set)
                {
                    g_pathSpline = path.spline ? path.spline : FindPathSpline(path);
                    g_pathSign = PathSignOf(path);
                    const double yaw = std::atan2(path.forward.y, path.forward.x) * 180.0 / std::numbers::pi;
                    double at = -1.0, length = -1.0;
                    if (g_pathSpline) SplineDistance(g_pathSpline, path.at, at);
                    if (UObject* component = PathSplineComponent()) SplineLength(component, length);
                    log::Info(L"explore: the game holds the character to a path: forward at yaw {:.0f}, the stick taken within {:.0f} to {:+.0f} degrees of it "
                              L"(dot {:.2f} to {:.2f}); its spline {} ({}), {:.0f} cm long, the character {:.0f} cm along it ({:.0f} by the game), forward {}",
                              yaw, path.angleLeft, path.angleRight, path.lowerDot, path.upperDot, g_pathSpline ? obj::ObjectName(g_pathSpline) : L"not found",
                              path.spline ? L"named by the game's state" : L"found by its nearest point", length, at, path.along,
                              g_pathSign > 0   ? L"where its distance grows"
                              : g_pathSign < 0 ? L"where its distance shrinks"
                                               : L"not told by the spline, so by the path's own forward");
                }
                else
                {
                    log::Info(L"explore: the game lets the character off the path");
                    g_pathSpline = nullptr;
                    g_pathSign = 0;
                }
            }
            g_path = path;
            (void)now;
        }

        // ---- facing the thing ------------------------------------------------------------

        // The thing whose press the game waits on: the chosen target once the game offers it,
        // or, when nothing is chosen, the one the game offers where the character stands. A
        // thing offered behind the player's back while they are after another is left to the
        // game's own prompt.
        Target* FacingSubject()
        {
            Target* chosen = Selected();
            if (chosen && !chosen->way && chosen->inside && obj::IsLive(chosen->actor)) return chosen;
            if (chosen || !g_offered || !obj::IsLive(g_offered)) return nullptr;
            for (auto& t : g_targets)
            {
                if (t.actor == g_offered && !t.way) return &t;
            }
            return nullptr;
        }

        PressTest g_press;          // the subject's test, as last read
        std::wstring g_pressLogged; // its parts as last logged
        look::Words g_lookWords;    // the words for the look said for this subject
        bool g_lookOffShot = false; // the subject's glint lies off the fixed shot, as logged

        // Whether a turn the player can make completes the look (look::CanBeLed): not on a fixed
        // shot whose picture leaves the glint out (the lodge's totem after its vision on
        // 2026-10-28: she faced it, the glint off the shot, and was told to turn toward it all the
        // same). A sighted player sees no glint there either.
        bool LookCanBeLed(const PressTest& p)
        {
            return look::CanBeLed(PartsOf(p));
        }

        // Whether the thing at hand would take the press, and, when only a look is missing,
        // who must turn. The word is said once the character stands still at it, a walk past
        // being no arrival, and again whenever the turn missing becomes the other one
        // (look::Words); the aim sound leads the turn and keeps the double ping while the press
        // would be taken, as it does in a look-around.
        void PollFacing(UObject* pawn, double now)
        {
            Target* target = pawn ? FacingSubject() : nullptr;
            if (!target)
            {
                g_facing = nullptr;
                return;
            }
            if (g_facing != target->actor)
            {
                g_facing = target->actor;
                g_faced = false;
                g_lookWords.Reset();
                g_lookOffShot = false;
                g_facingBlipAt = 0.0;
                g_facingLoggedAt = 0.0;
                g_pressLogged.clear();
            }
            g_press = ReadPressTest(pawn, target->actor);
            const auto parts = PressTestText(g_press, false);
            if (parts != g_pressLogged)
            {
                g_pressLogged = parts;
                log::Info(L"explore: the press at \"{}\": {}", target->label, PressTestText(g_press));
            }
            if (g_press.verdict != g_faced)
            {
                g_faced = g_press.verdict;
                log::Info(L"explore: \"{}\" {}", target->label, g_faced ? L"would take the press now" : L"would not take the press now");
            }
            const bool offShot = !g_press.verdict && g_press.AtHand() && g_press.measured && !LookCanBeLed(g_press);
            if (offShot && !g_lookOffShot)
                log::Info(L"explore: \"{}\" is at hand, but its glint lies off the fixed shot, which no turn of hers brings in: no look is led", target->label);
            g_lookOffShot = offShot;
            const look::Lead word = g_lookWords.Update(now, look::Missing(PartsOf(g_press)), !g_walking && CharacterSpeed(pawn) <= 10.0);
            if (word == look::Lead::None) return;
            const bool character = word == look::Lead::Character;
            log::Info(L"explore: \"{}\" is at hand, but the press would not be taken until the {} looks at it", target->label,
                      character ? L"character" : L"camera");
            speech::Announce(locale::Mod(character ? L"explore.face" : L"explore.look"));
        }

        // Leads the turn with the aim sound of a look-around: the camera's turn while the camera
        // is off, measured as the game measures it; else the character's, in the frame the stick
        // works in, so that the stick pushed toward the sound turns her toward the thing (measured
        // from her own forward it sent the player the wrong way on a fixed shot, where the stick
        // follows the camera and not her: she walked off the totem on 2026-10-28). The walk's last
        // stretch nudges her the same way.
        void PollFacingSound(UObject* pawn, double now)
        {
            if (!g_facing || !g_lookSound || !pawn || g_walking) return;
            Target* target = FacingSubject();
            if (!target || target->actor != g_facing) return;
            g_press = ReadPressTest(pawn, target->actor);
            if (!g_press.AtHand() || !g_press.measured || !LookCanBeLed(g_press)) return;
            const bool camera = look::Leads(PartsOf(g_press)) == look::Lead::Camera;
            const double across = camera ? g_press.cameraTurn : g_press.stickTurn;
            // The glint is judged on the screen, up and down as well as across: the blip's pitch
            // follows the tilt the camera needs.
            const double rise = camera && g_press.byGlint ? g_press.cameraRise : 0.0;
            if (now - g_facingLoggedAt >= 1.0)
            {
                g_facingLoggedAt = now;
                log::Info(L"explore: look at \"{}\": {} {:+.0f} deg off across, {:+.0f} up, {}{}", target->label,
                          camera ? L"the camera" : L"the stick to turn her", across, rise, g_press.verdict ? L"looked at" : L"off",
                          camera ? std::wstring() : std::format(L" (she faces {:+.0f} deg off it)", g_press.characterTurn));
            }
            const double off = std::hypot(across, rise);
            const double interval = g_press.verdict ? 0.12 : 0.09 + 0.4 * std::clamp(off / 25.0, 0.0, 1.0);
            if (now - g_facingBlipAt < interval) return;
            g_facingBlipAt = now;
            sounds::Aim(std::clamp(across / 12.0, -1.0, 1.0), std::clamp(0.5 + rise / 12.0, 0.0, 1.0), g_press.verdict, std::fabs(across) > 90.0);
        }

        // ---- free roaming ----------------------------------------------------------------

        void Step(int direction);

        // The room she is in, and a crossing into another said by its name alone (rooms::Crossing):
        // walked through a doorway or a door, or put there by a scene. The first room of a
        // character is taken without a word; F6 says it.
        void PollRoom(UObject* pawn)
        {
            Vec middle;
            double halfHeight = 88.0;
            if (!CapsuleSpan(pawn, middle, halfHeight)) return;
            const std::wstring before = g_crossing.Room();
            const auto said = g_crossing.Update(g_zones, middle);
            if (g_crossing.Room() != before)
                log::Info(L"explore: the character is in {}{}", g_crossing.Room().empty() ? std::wstring(L"no room") : g_crossing.Room(),
                          said.empty() ? std::wstring() : L", said as \"" + said + L"\"");
            if (!said.empty()) speech::Announce(said);
        }

        void PollRoaming(double now)
        {
            UObject* pawn = g_controlPawn;
            const bool paused = gamestate::Paused();
            UObject* screen = watch::CurrentScreen();
            const bool mechanic = MechanicShown();
            const bool exploring = g_control && !paused && !screen && !mechanic;
            const std::wstring why = exploring  ? std::wstring()
                                     : paused   ? std::wstring(L"the game is paused")
                                     : screen   ? L"a screen is up, " + obj::ClassName(screen)
                                     : mechanic ? std::wstring(L"a mechanic is shown")
                                     : pawn     ? std::wstring(L"the game holds the character")
                                                : std::wstring(L"no character");
            if (exploring != g_roaming || why != g_notRoamingWhy)
            {
                g_roaming = exploring;
                g_notRoamingWhy = why;
                log::Info(L"explore: {} {}{}", pawn ? obj::ObjectName(pawn) : L"<no pawn>",
                          exploring ? L"under the player's control" : L"not under the player's control: ", why);
            }
            if (!exploring)
            {
                if (g_exploring)
                {
                    g_exploring = false;
                    g_leftForPause = paused;
                    g_choiceDue = false;
                    g_stepDue = 0;
                    g_pendingNew.clear();
                    g_facing = nullptr;
                    g_leftAt = now;
                    g_hintDueAt = -1.0;
                    StopWalk(nullptr, false);
                    if (pawn) ActorLocation(pawn, g_leftAtPosition);
                }
                return;
            }
            Vec here;
            double yaw = 0.0;
            if (!ActorLocation(pawn, here) || !CameraYaw(yaw)) return;
            // Every direction is given from the camera; a cut to another camera, or a quick
            // turn, turns them all at once.
            if (std::fabs(NormalizeDegrees(yaw - g_cameraYaw)) > 40.0)
            {
                log::Info(L"explore: the camera turned {:.0f} degrees", NormalizeDegrees(yaw - g_cameraYaw));
                g_cameraYaw = yaw;
            }
            else if (std::fabs(NormalizeDegrees(yaw - g_cameraYaw)) > 5.0)
            {
                g_cameraYaw = yaw;
            }
            if (pawn != g_pawn)
            {
                g_pawn = pawn;
                g_crossing.Reset();
                g_targets.clear();
                g_selected = nullptr;
                g_autoChosen = false;
                g_pendingNew.clear();
                g_turnedOnAt.clear();
                g_waysAnnounced.clear();
                g_wayVerdict.clear();
                g_wayNote.clear();
                g_gone.clear();
                g_scannedAt = -10.0;
                g_waysScannedAt = -10.0;
                g_luaScannedAt = -10.0;
                g_settle.Reset();
            }
            // A scene that hands her back on the ground the last map reached, however long it
            // took, leaves everything where it was, so the list is not said again; only what the
            // level has added is. Carried off that ground (through a door, down a drop), she begins
            // afresh (ways::ResumesList).
            else if (!g_exploring && !g_targets.empty())
            {
                Vec middle;
                double halfHeight = 0.0;
                const bool onGround = CapsuleSpan(pawn, middle, halfHeight) && OnLastMappedGround(middle, halfHeight);
                if (ways::ResumesList(g_leftForPause, onGround))
                    g_exploring = true;
                else
                    log::Info(L"explore: the hold begins afresh: the scene left the character off the ground the last map reached ({:.0f} s, {:.0f} cm on)",
                              now - g_leftAt, FlatDistance(here, g_leftAtPosition));
            }
            // A hold begun afresh lists no way before its first map has settled the list; a resumed
            // one keeps the list it left while its map settles.
            if (!g_exploring)
            {
                g_settle.Reset();
                g_lister.NewHold();
                g_freezeAfter = static_cast<int64_t>(g_reach.generation) - 1;
                // A map still standing (the pause left it, and no short exchange resumed the list)
                // is this hold's first: the list settles on it, at once when it is done.
                if (g_reach.flood) g_settle.Begin(now);
                // Every thing is judged anew by the room rule, as the ways are.
                for (auto& t : g_targets)
                    t.room = rooms::Verdict::None;
                g_listSince = now;
            }
            PollRoom(pawn);
            const auto offered = OfferedUseLocations(pawn);
            UObject* first = offered.empty() ? nullptr : offered.front();
            // The targets before this look, and whether the player had chosen each one: a use
            // location the player chose that leaves the list (its interaction in progress) may
            // come back and be chosen again; a way on that leaves it has been set off for good.
            std::vector<std::pair<UObject*, bool>> before;
            for (const auto& t : g_targets)
                before.emplace_back(t.actor, t.actor == g_selected && !g_autoChosen && !t.way);
            auto fresh = Gather(pawn, here, yaw, now);
            // A way on beside a named thing is left out: the thing is the way, and the player who
            // goes to it sets the volume off; not beside a thing she cannot go to, as the list last
            // judged it (ways::LeftForThing). One on the road to a listed target is told apart once
            // the roads are known (ChooseListed).
            std::vector<std::pair<Vec, bool>> things;
            for (const auto& u : fresh)
            {
                if (u.way) continue;
                const auto judged = std::find_if(g_targets.begin(), g_targets.end(), [&](const Target& t) { return t.actor == u.actor; });
                const bool goneTo = judged == g_targets.end() ||
                                    !(judged->offPath || judged->behind || judged->unreachable || judged->room == rooms::Verdict::Apart);
                things.emplace_back(Aim(u), goneTo);
            }
            std::erase_if(fresh,
                          [&](const Target& w)
                          {
                              if (!w.way) return false;
                              const wchar_t* why = nullptr;
                              for (const auto& [thing, goneTo] : things)
                              {
                                  if (ways::LeftForThing(thing, w.position, goneTo)) why = L"beside a thing listed";
                              }
                              if (!why)
                              {
                                  if (g_wayNote.erase(w.actor))
                                      log::Info(L"explore: trigger {} is kept: no thing beside it is one she can go to",
                                                w.coreName.empty() ? obj::ObjectName(w.actor) : w.coreName);
                                  return false;
                              }
                              if (g_wayNote[w.actor] != why)
                              {
                                  g_wayNote[w.actor] = why;
                                  log::Info(L"explore: trigger {} lies {}, and is left out", w.coreName.empty() ? obj::ObjectName(w.actor) : w.coreName, why);
                              }
                              return true;
                          });
            const auto added = Reconcile(fresh);
            const bool removed = std::any_of(before.begin(), before.end(), [](const auto& b)
                                             { return std::none_of(g_targets.begin(), g_targets.end(), [&](const Target& t) { return t.actor == b.first; }); });
            if (!added.empty() || removed) g_lister.Again();
            if (g_grid.storyMovedAt > g_grid.refusalsForgottenAt) ForgetStaleRefusals(now);
            // Every target's way is asked for (the mesh at once, the ground as it gets felt
            // out), so that what is said of it is a distance on foot.
            for (auto& t : g_targets)
                EnsureRoute(pawn, here, t, now, 3.0);
            ChooseListed(pawn, here, now);
            const bool meshRoads = MeshHere(pawn, here, now);
            if (g_exploring && !meshRoads) KeepReachMap(pawn, now);
            UObject* saidNow = nullptr; // a target said as the mod's choice in this look, not to be said again as news
            // A target the mod chose that the list now leaves out gives way to the first one it
            // keeps, the judged ones first; the player hears the new one once the hint is said.
            if (Target* chosen = Selected();
                chosen && g_autoChosen && chosen->hidden && g_exploring && !(chosen->unjudged && !g_settle.Settled()) && chosen->room != rooms::Verdict::Waits)
            {
                // The road to a farther way runs through the chosen one: that way is the same way
                // on, only longer, and it is taken without a word (H says it).
                auto it = std::find_if(g_targets.begin(), g_targets.end(),
                                       [&](const Target& t) { return chosen->way && t.way && t.actor == chosen->onRoadOf && !t.hidden; });
                const bool farther = it != g_targets.end();
                if (!farther)
                    it = std::find_if(g_targets.begin(), g_targets.end(), [&](const Target& t) { return !t.reached && !t.hidden && Judged(t, meshRoads); });
                if (it == g_targets.end()) it = std::find_if(g_targets.begin(), g_targets.end(), [](const Target& t) { return !t.reached && !t.hidden; });
                log::Info(L"explore: the target chosen for the player, \"{}\", is left out now; {}", chosen->label,
                          it == g_targets.end() ? std::wstring(L"nothing else to choose")
                          : farther             ? L"the way on farther along its road is chosen instead, " + obj::ObjectName(it->actor)
                                                : L"\"" + it->label + L"\" is chosen instead");
                if (it != g_targets.end())
                {
                    // A choice held back until the map settled stays to be said, as it stands then.
                    const bool due = g_choiceDue;
                    Select(&*it, pawn, here, yaw, false);
                    g_autoChosen = true;
                    if (g_hintDueAt < 0.0 && !farther)
                    {
                        SayChoice(pawn, *it, here, yaw, now);
                        saidNow = it->actor;
                    }
                    else if (due)
                    {
                        g_choiceDue = true;
                    }
                    if (farther) saidNow = it->actor;
                }
                else
                {
                    Select(nullptr, pawn, here, yaw, false);
                }
            }
            for (const auto& [actor, chosenByPlayer] : before)
            {
                if (std::none_of(g_targets.begin(), g_targets.end(), [&](const Target& t) { return t.actor == actor; }))
                    g_gone[actor] = Gone{now, chosenByPlayer};
            }
            std::erase_if(g_gone, [&](const auto& item) { return now - item.second.at > 30.0; });
            // Nothing chosen (the chosen target set off, or left the list with nothing kept in
            // its place): the mod chooses the first target the list keeps whose ground is known,
            // and says it unless the hint is about to. A target the player chose that left the
            // list a moment ago is waited for: it is chosen again if it comes back.
            const bool chosenAway = std::any_of(g_gone.begin(), g_gone.end(),
                                                [](const auto& item)
                                                {
                                                    if (!item.second.selected || !obj::IsLive(item.first) || !obj::IsA(item.first, L"BatesUseLocation"))
                                                        return false;
                                                    UseState state;
                                                    return ReadUseState(item.first, state) && state.inProgress;
                                                });
            if (g_exploring && cfg::Get().autoTarget && !Selected() && !chosenAway)
            {
                auto it = std::find_if(g_targets.begin(), g_targets.end(), [&](const Target& t) { return !t.reached && !t.hidden && Judged(t, meshRoads); });
                if (it != g_targets.end())
                {
                    log::Info(L"explore: \"{}\" is chosen for the player, nothing being chosen", it->label);
                    Select(&*it, pawn, here, yaw, false);
                    g_autoChosen = true;
                    if (g_hintDueAt < 0.0)
                    {
                        SayChoice(pawn, *it, here, yaw, now);
                        saidNow = it->actor;
                    }
                }
            }
            if (first != g_offered)
            {
                g_offered = first;
                std::wstring label;
                for (const auto& t : g_targets)
                {
                    if (t.actor == first) label = t.label;
                }
                if (first && label.empty())
                {
                    std::wstring coreName;
                    label = UseLocationLabel(first, coreName);
                }
                log::Info(L"explore: the game offers {}{}", first ? label : L"nothing to use",
                          first ? (obj::CallForBool(first, L"CanOverlappingCharacterInteract") ? L", and a press would take it"
                                                                                               : L", but a press would not take it now")
                                : L"");
            }

            PollFacing(pawn, now);

            // Arrival: a way once the character stands in it; a use location once the game
            // offers it and would take the press, or once she stands still in it with only a
            // look missing (a walk past is no arrival, nor is the walk's own last stretch). A
            // target left behind again can be walked to again.
            for (auto& t : g_targets)
            {
                bool there = AtTarget(t);
                if (there && !t.reached && !t.way)
                {
                    if (g_walking && t.actor == g_selected)
                    {
                        there = false;
                    }
                    else
                    {
                        const PressTest p = ReadPressTest(pawn, t.actor);
                        there = p.verdict || p.autoStart || (p.AtHand() && CharacterSpeed(pawn) < 10.0);
                    }
                }
                if (there && !t.reached)
                {
                    t.reached = true;
                    if (t.actor == g_selected && g_exploring)
                    {
                        log::Info(L"explore: reached \"{}\"", t.label);
                        if (cfg::Detailed()) speech::Announce(locale::Mod(L"explore.reached", t.label));
                        sounds::Play(sounds::Cue::Confirm);
                    }
                }
                else if (!there && t.reached && (t.way || t.distance > 350.0))
                {
                    t.reached = false;
                }
            }

            for (const auto& t : added)
                log::Info(L"explore: {} \"{}\" {} ({}) at {:.0f} cm, {:.0f} deg{}", t.way ? L"way" : L"use location", t.label, obj::ObjectName(t.actor),
                          t.coreName, t.distance, t.bearing, t.glint ? L", with a glint" : L"");

            std::vector<UObject*> silent;
            for (const auto& t : g_targets)
            {
                if (!t.way) continue;
                if (std::find(g_waysAnnounced.begin(), g_waysAnnounced.end(), t.actor) != g_waysAnnounced.end())
                    silent.push_back(t.actor);
                else
                    g_waysAnnounced.push_back(t.actor);
            }

            if (!g_exploring)
            {
                g_exploring = true;
                if (cfg::Get().autoTarget)
                {
                    auto it =
                        std::find_if(g_targets.begin(), g_targets.end(), [&](const Target& t) { return !t.reached && !t.hidden && Judged(t, meshRoads); });
                    if (it == g_targets.end()) it = std::find_if(g_targets.begin(), g_targets.end(), [](const Target& t) { return !t.reached && !t.hidden; });
                    if (it != g_targets.end())
                    {
                        Select(&*it, pawn, here, yaw, false);
                        g_autoChosen = true;
                    }
                }
                // The keys are said after the game's own word about the stick, which it
                // shows a moment after handing the character over, then the target the
                // beacon leads to. The rest of what is around is left to F6.
                g_hintDueAt = now + 1.0;
            }
            else
            {
                std::vector<Target*> fresh;
                for (auto& t : g_targets)
                {
                    if (std::none_of(added.begin(), added.end(), [&](const Target& a) { return a.actor == t.actor; })) continue;
                    if (const auto back = g_gone.find(t.actor); back != g_gone.end())
                    {
                        if (back->second.selected && g_selected == nullptr)
                        {
                            Select(&t, pawn, here, yaw, false);
                            g_autoChosen = false;
                        }
                        g_gone.erase(back);
                        continue;
                    }
                    if (std::find(silent.begin(), silent.end(), t.actor) != silent.end()) continue;
                    if (t.actor == saidNow) continue;
                    // A way whose ground is not known yet, or while the list still settles, is news
                    // once it is known and settled, if the list keeps it.
                    EnsureRoute(pawn, here, t, now, 3.0);
                    if ((t.way && (!Judged(t, meshRoads) || g_waysSettling)) || (!t.way && t.room == rooms::Verdict::Waits))
                    {
                        g_pendingNew.emplace(t.actor, now);
                        continue;
                    }
                    if (t.hidden) continue;
                    // Only what the game has just turned on is news; what came within the list's
                    // range as she walked was there all along, and joins without a word.
                    if (!ways::IsNews(TurnedOnAt(t.actor), now))
                    {
                        log::Info(L"explore: \"{}\" {} joins the list unannounced: the game had it on before", t.label, obj::ObjectName(t.actor));
                        continue;
                    }
                    fresh.push_back(&t);
                }
                // Ways waiting for their ground are news once it is felt out (or after a
                // while without), and only if the list keeps them.
                for (auto it = g_pendingNew.begin(); it != g_pendingNew.end();)
                {
                    auto t = std::find_if(g_targets.begin(), g_targets.end(), [&](const Target& x) { return x.actor == it->first; });
                    if (t == g_targets.end())
                    {
                        it = g_pendingNew.erase(it);
                        continue;
                    }
                    if ((!Judged(*t, meshRoads) || g_waysSettling || (!t->way && t->room == rooms::Verdict::Waits)) && now - it->second < 20.0)
                    {
                        ++it;
                        continue;
                    }
                    if (!t->hidden && !t->reached && t->actor != saidNow && ways::IsNews(TurnedOnAt(t->actor), it->second)) fresh.push_back(&*t);
                    it = g_pendingNew.erase(it);
                }
                // The nearest two are named; a crowd arriving at once is counted.
                std::sort(fresh.begin(), fresh.end(), [](const Target* a, const Target* b) { return SpokenDistance(*a) < SpokenDistance(*b); });
                for (size_t i = 0; i < fresh.size() && i < 2; ++i)
                {
                    const Target& t = *fresh[i];
                    speech::Announce(locale::Mod(L"explore.new", {t.label, Metres(SpokenDistance(t)), Bearings(t, here, yaw)}));
                }
                if (fresh.size() > 2) speech::Announce(locale::Mod(L"explore.new.count", std::to_wstring(fresh.size() - 2)));
            }

            if (g_stepDue != 0 && !g_waysSettling)
            {
                const int direction = g_stepDue;
                g_stepDue = 0;
                Step(direction);
            }
            // A choice held back while the list settled is said as it stands now.
            if (g_choiceDue && g_settle.Settled())
            {
                g_choiceDue = false;
                Target* chosen = Selected();
                if (chosen && g_autoChosen && !chosen->hidden && !chosen->reached)
                {
                    log::Info(L"explore: the reach map has settled the list; \"{}\" {} is said as the choice", chosen->label, obj::ObjectName(chosen->actor));
                    speech::Announce(ChoiceText(pawn, *chosen, here, yaw, now));
                }
            }
            // The keys and the choice are said once something is listed and the choice will hold: a
            // thing to use at once, a way once the list has settled.
            const Target* toSay = Selected();
            if (g_hintDueAt > 0.0 && now >= g_hintDueAt && (g_settle.Settled() || (toSay && !toSay->way)) &&
                std::any_of(g_targets.begin(), g_targets.end(), [](const Target& t) { return !t.hidden; }))
            {
                g_hintDueAt = -1.0;
                if (now - g_hintSaidAt > 120.0)
                {
                    g_hintSaidAt = now;
                    speech::Announce(HintText());
                }
                if (Target* chosen = Selected()) speech::Announce(ChoiceText(pawn, *chosen, here, yaw, now));
            }

            Target* target = Selected();
            if (!target)
            {
                g_selected = nullptr;
                return;
            }
            if (target->reached) return;
            StraightenChosen(*target, now);
            EnsureRoute(pawn, here, *target, now, g_walking ? 0.5 : 1.5, true);
            // The beacon leads where the walk would go: along the path the game holds her to,
            // else to the next corner of the way, else straight to the target.
            const bool onPath = g_path.set && AheadOnPath(here, Aim(*target));
            Beacon(onPath ? PathBearing(yaw) : BearingDegrees(here, target->hasRoute ? target->routeNext : Aim(*target), yaw), SpokenDistance(*target), now);
        }

        void PollImpl()
        {
            const double now = gamethread::NowSeconds();
            TickPlayClock(now);
            // A flick is one frame's push: let go here, before anything else, whatever the walk does
            // this frame, since one held longer starts a walk (look::Flicker); a push held on purpose
            // is let go once its time is up, or the walk is over.
            if (g_flicker.Pushing() && gamethread::FrameCount() != g_flickFrame && (!g_flicker.Holding(now) || !g_walking))
            {
                g_flicker.Release(now);
                input::ReleaseWalkKeys();
            }
            const bool controlChanged = PollControl(now);
            if (g_controlPawn && gamethread::FrameCount() % 5 == 2) PollLimitedPath(g_controlPawn, now);
            PollWalk(now);
            if (controlChanged || gamethread::FrameCount() % 10 == 0) PollRoaming(now);
            if (g_exploring)
            {
                if (!g_meshHere)
                {
                    KeepReachMap(g_controlPawn, now);
                    PollReachMap(g_controlPawn, now);
                }
                else if (g_reach.flood)
                {
                    log::Info(L"explore: the navigation mesh gives the roads here, and the reach map is dropped");
                    const uint64_t generation = g_reach.generation;
                    g_reach = ReachMap{};
                    g_reach.generation = generation;
                }
                PollGrid(g_controlPawn, now);
                FeelOutNextTarget(g_controlPawn, now);
                if (gamethread::FrameCount() % 60 == 3) g_record.Flush();
                if (gamethread::FrameCount() % 5 == 1) PollFacingSound(g_controlPawn, now);
            }
            SoundControl();
        }

        void Poll(float)
        {
            obj::SafeInvokeLogged(L"exploration.PollImpl", [](void*) { PollImpl(); }, nullptr);
        }

        void Step(int direction)
        {
            if (!g_exploring) return;
            if (g_targets.empty())
            {
                if (g_waysSettling)
                {
                    g_stepDue = direction;
                    return;
                }
                speech::Now(locale::Mod(L"explore.none"));
                return;
            }
            size_t index = 0;
            bool found = false;
            for (size_t i = 0; i < g_targets.size(); ++i)
            {
                if (g_targets[i].actor == g_selected)
                {
                    index = i;
                    found = true;
                }
            }
            // The next listed target round the ring; the hidden ones are passed over.
            const size_t n = g_targets.size();
            size_t candidate = found ? (index + n + static_cast<size_t>(direction > 0 ? 1 : n - 1)) % n : (direction > 0 ? 0 : n - 1);
            for (size_t tried = 0; tried < n && g_targets[candidate].hidden; ++tried)
                candidate = (candidate + n + static_cast<size_t>(direction > 0 ? 1 : n - 1)) % n;
            if (g_targets[candidate].hidden)
            {
                // Nothing listed while a fresh map still settles which ways stay: the press is
                // answered once it has, rather than with a "nothing" a moment out of date.
                if (g_waysSettling)
                {
                    g_stepDue = direction;
                    log::Info(L"explore: the press waits for the reach map to settle the list");
                    return;
                }
                speech::Now(locale::Mod(L"explore.none"));
                return;
            }
            UObject* pawn = Pawn();
            Vec here;
            double yaw = 0.0;
            if (!pawn || !ActorLocation(pawn, here) || !CameraYaw(yaw)) return;
            Select(&g_targets[candidate], pawn, here, yaw, false);
            speech::Now(ScannerText(g_targets[candidate], here, yaw));
            g_autoChosen = false;
        }

        // ---- the dump ---------------------------------------------------------------------

        void DumpImpl()
        {
            UObject* pawn = Pawn();
            if (!pawn)
            {
                log::Info(L"explore dump: no character");
                return;
            }
            Vec here, eye;
            double pitch = 0.0, camYaw = 0.0, controlYaw = 0.0, pawnYaw = 0.0;
            ActorLocation(pawn, here);
            CameraLine(eye, pitch, camYaw);
            ControlYaw(controlYaw);
            {
                double p = 0.0;
                RotationOf(pawn, L"K2_GetActorRotation", p, pawnYaw);
            }
            std::wstring name;
            obj::ReadString(pawn, L"CharacterName", name);
            log::Info(L"explore dump: the camera's active layer is {} ({}), {}; the controller {} movement input; in play {}", ActiveCameraLayer(),
                      CameraLayerName(ActiveCameraLayer()), ExplorationCamera() ? L"an exploration layer" : L"not an exploration layer",
                      MoveInputIgnored() ? L"ignores" : L"takes", gamestate::InPlay());
            log::Info(
                L"explore dump: {} {} at ({:.0f}, {:.0f}, {:.0f}) yaw {:.0f}; camera at ({:.0f}, {:.0f}, {:.0f}) pitch {:.0f} yaw {:.0f}; control yaw {:.0f}; "
                L"binding state {} ({}); camera layer {}; game state {}; interaction {}; screen {}",
                name, obj::ObjectName(pawn), here.x, here.y, here.z, pawnYaw, eye.x, eye.y, eye.z, pitch, camYaw, controlYaw, BindingState(pawn),
                BindingName(BindingState(pawn)), CameraLayerName(CameraLayer()), gamestate::Name(gamestate::Current()),
                ActiveInteraction(pawn) ? obj::ObjectName(ActiveInteraction(pawn)) : L"<none>",
                watch::CurrentScreen() ? obj::ClassName(watch::CurrentScreen()) : L"<none>");
            LogNavigationData();
            const auto offered = OfferedUseLocations(pawn);
            std::vector<std::wstring> offeredNames;
            for (auto* actor : offered)
                offeredNames.push_back(obj::ObjectName(actor));
            log::Info(L"explore dump: the game offers [{}]", str::Join(offeredNames, L", "));
            g_scannedAt = -10.0;
            g_luaScannedAt = -10.0;
            ScanLevel(pawn, gamethread::NowSeconds());
            ScanLuaEvents(gamethread::NowSeconds());
            log::Info(L"explore dump: use locations in the level ({})", g_useLocations.size());
            for (auto* actor : g_useLocations)
            {
                // A slot of the object array freed and reused holds something else now.
                if (!obj::IsLive(actor) || !obj::IsA(actor, L"BatesUseLocation")) continue;
                UseState state;
                ReadUseState(actor, state);
                std::wstring coreName;
                const auto label = UseLocationLabel(actor, coreName);
                Vec at;
                ActorLocation(actor, at);
                Vec low, high;
                const bool box = TriggerBox(actor, low, high);
                UObject* interaction = nullptr;
                obj::ReadObject(actor, L"InteractionActor", interaction);
                bool anyone = false, requiresGlint = false, guidance = false, autoStart = false;
                int64_t who = -1;
                obj::ReadBool(actor, L"bCanAnyoneInteract", anyone);
                obj::ReadBool(actor, L"bRequiresGlint", requiresGlint);
                obj::ReadBool(actor, L"bUsePlayerGuidance", guidance);
                obj::ReadBool(actor, L"bStartInteractionAutomatically", autoStart);
                obj::ReadInt(actor, L"CharacterToInteract", who);
                std::wstring glintText = L"none";
                if (const auto g = g_glintOf.find(actor); g != g_glintOf.end())
                {
                    double start = 0.0, end = 0.0, sight = 0.0;
                    obj::ReadFloat(g->second, L"StartDistance", start);
                    obj::ReadFloat(g->second, L"EndDistance", end);
                    obj::ReadFloat(g->second, L"SightAngle", sight);
                    glintText = std::format(L"{} (start {:.0f} cm, end {:.0f} cm, sight angle {:.0f})", obj::ObjectName(g->second), start, end, sight);
                }
                log::Info(
                    L"  {} \"{}\" core {} interaction {} ({}) at ({:.0f}, {:.0f}, {:.0f}) {:.0f} cm {:.0f} deg; box {}; enabled {} activity {} sleeping {} in "
                    L"progress {}; anyone {} character {}; requires glint {} glint {}; guidance {} auto start {}; offered {}",
                    obj::ObjectName(actor), label, coreName, interaction ? obj::ObjectName(interaction) : L"<none>",
                    interaction ? obj::ClassName(interaction) : L"", at.x, at.y, at.z, Distance(here, at), BearingDegrees(here, at, camYaw),
                    box ? std::format(L"({:.0f}, {:.0f}, {:.0f}) to ({:.0f}, {:.0f}, {:.0f})", low.x, low.y, low.z, high.x, high.y, high.z) : L"none",
                    state.enabled, state.activity, state.sleeping, state.inProgress, anyone, who, requiresGlint, glintText, guidance, autoStart,
                    std::find(offered.begin(), offered.end(), actor) != offered.end());
            }
            log::Info(L"explore dump: trigger volumes in the level ({})", g_volumes.size());
            for (auto* volume : g_volumes)
            {
                if (!obj::IsLive(volume) || !obj::IsA(volume, L"BatesCharacterTriggerVolume")) continue;
                bool collision = false, startEnabled = false;
                int64_t type = 0, activator = 0, mask = 0;
                obj::ReadBool(volume, L"bActorEnableCollision", collision);
                obj::ReadBool(volume, L"bStartEnabled", startEnabled);
                obj::ReadInt(volume, L"TriggerVolumeType", type);
                obj::ReadInt(volume, L"ActivatorType", activator);
                obj::ReadInt(volume, L"ActivitiesMask", mask);
                Vec low, high;
                const bool box = TriggerBox(volume, low, high);
                const auto coreName = CoreName(volume);
                const auto lua = g_luaEvents.find(coreName);
                log::Info(L"  {} core {} class {} collision {} start enabled {} type {} activator {} mask {} box {}; actions [{}]; script {}",
                          obj::ObjectName(volume), coreName, obj::ClassName(volume), collision, startEnabled, type, activator, mask,
                          box ? std::format(L"({:.0f}, {:.0f}, {:.0f}) to ({:.0f}, {:.0f}, {:.0f}), {:.0f} cm off", low.x, low.y, low.z, high.x, high.y, high.z,
                                            Distance(here, Vec{(low.x + high.x) / 2, (low.y + high.y) / 2, (low.z + high.z) / 2}))
                              : L"none",
                          str::Join(ActionNotes(volume, pawn), L", "),
                          lua == g_luaEvents.end() ? L"none"
                                                   : std::format(L"{} [{}] activities [{}]{}", lua->second.limit, str::Join(lua->second.calls, L" "),
                                                                 str::Join(lua->second.activities, L", "), lua->second.story ? L" story" : L""));
            }
            for (const wchar_t* cls : {L"BatesGuidanceTriggerVolume", L"BatesGuidancePassageVolume", L"BatesPOIObject", L"BatesInteractionBase"})
            {
                const auto actors = obj::FindAllLive(cls);
                log::Info(L"explore dump: {} ({})", cls, actors.size());
                for (auto* actor : actors)
                {
                    Vec at;
                    ActorLocation(actor, at);
                    bool collision = false;
                    obj::ReadBool(actor, L"bActorEnableCollision", collision);
                    log::Info(L"  {} {} core {} at ({:.0f}, {:.0f}, {:.0f}) {:.0f} cm collision {}", obj::ClassName(actor), obj::ObjectName(actor),
                              CoreName(actor), at.x, at.y, at.z, Distance(here, at), collision);
                }
            }
            for (const auto& t : g_targets)
            {
                const Route route = FindRoute(pawn, here, t, nullptr);
                log::Info(L"explore dump: target \"{}\" {:.0f} cm straight {:.0f} deg; way {} {:.0f} cm, corner {:.0f} cm at {:.0f} deg; reached {}", t.label,
                          t.distance, t.bearing, route.valid ? (route.partial ? L"partial" : L"whole") : L"none", route.length, FlatDistance(here, route.next),
                          BearingDegrees(here, route.next, camYaw), t.reached);
            }
        }

        // ---- the keys ---------------------------------------------------------------------

        bool OnNext()
        {
            if (!g_exploring) return false;
            NextTarget();
            return true;
        }

        bool OnPrevious()
        {
            if (!g_exploring) return false;
            PreviousTarget();
            return true;
        }

        bool OnWhere()
        {
            if (!g_exploring) return false;
            WhereIsTarget();
            return true;
        }

        bool OnWalk()
        {
            if (!g_exploring) return false;
            WalkToTarget();
            return true;
        }

        // Whether a look is being led: the thing at hand waits for the camera or the character
        // to look at it, which the look beacon sounds while it is on.
        bool LookLed()
        {
            if (!g_facing || g_walking) return false;
            Target* target = FacingSubject();
            return target && target->actor == g_facing && g_press.AtHand() && g_press.measured && LookCanBeLed(g_press);
        }

        bool OnBeacon()
        {
            if (!g_exploring) return false;
            ToggleBeacon();
            return true;
        }
    }

    void ExplorationFeature::Install()
    {
        g_beacon = cfg::Get().beacon;
        g_lookSound = cfg::Get().lookSound;
        using hotkeys::Command;
        hotkeys::AddContext(&ExplorationActive);
        hotkeys::OnCommand(Command::NextTarget, &OnNext);
        hotkeys::OnCommand(Command::PreviousTarget, &OnPrevious);
        hotkeys::OnCommand(Command::Where, &OnWhere);
        hotkeys::OnCommand(Command::Walk, &OnWalk);
        hotkeys::OnCommand(Command::Beacon, &OnBeacon);
        watch::OnWidgetClass(L"BatesLoadingScreen",
                             [](UObject* widget, bool appeared)
                             {
                                 if (appeared) g_loadingScreen = widget;
                             });
        // The films are watched so that Cover() can ask after each of them.
        watch::OnWidgetClass(L"BatesFullscreenVideoWidget",
                             [](UObject* widget, bool appeared)
                             {
                                 if (appeared) log::Verbose(L"explore: a full-screen film {} came up", obj::ObjectName(widget));
                             });
        // The scenes' scripts call it through the game's reflection (their glue looks the function
        // up by name and calls ProcessEvent), so the function itself is hooked.
        hooks::OnNative(L"/Script/Bates.BatesGameScriptInterface:IsInTrigger", &OnIsInTrigger, nullptr);
        diag::RegisterCommand(L"explore", [](const std::wstring&) { DumpExploration(); });
        gamethread::AddPoller(L"explore", &Poll);
    }

    void ExplorationFeature::Describe(std::vector<std::wstring>& out)
    {
        if (!g_exploring) return;
        if (!g_crossing.Spoken().empty()) out.push_back(g_crossing.Spoken());
        out.push_back(ListText());
    }

    void ExplorationFeature::Help(std::vector<std::wstring>& out)
    {
        if (!g_exploring) return;
        using hotkeys::Command;
        if (input::CurrentScheme() == input::Scheme::Gamepad)
        {
            out.push_back(
                locale::Mod(L"help.explore.pad", {hotkeys::SpokenKey(Command::NextTarget), hotkeys::SpokenKey(Command::PreviousTarget),
                                                  hotkeys::SpokenKey(Command::Where), hotkeys::SpokenKey(Command::Beacon), hotkeys::SpokenKey(Command::Walk)}));
        }
        else
        {
            out.push_back(
                locale::Mod(L"help.explore", {hotkeys::KeyOf(Command::NextTarget), hotkeys::KeyOf(Command::PreviousTarget), hotkeys::KeyOf(Command::Beacon)}));
            out.push_back(locale::Mod(L"help.where", hotkeys::KeyOf(Command::Where)));
            out.push_back(locale::Mod(L"help.walk", hotkeys::KeyOf(Command::Walk)));
        }
        out.push_back(locale::Mod(L"help.looksound", hotkeys::SpokenKey(Command::Beacon)));
        // How a player turns her toward a thing without walking off it: the game's turn on the
        // spot (look::Flicker), which nothing on the screen teaches.
        if (LookLed())
        {
            if (input::CurrentScheme() == input::Scheme::Gamepad)
                out.push_back(locale::Mod(L"help.lookturn.pad", {input::KeyForAction(L"IA_Movement"), input::KeyForAction(L"IA_Looking")}));
            else
                out.push_back(locale::Mod(L"help.lookturn"));
        }
    }

    void NextTarget()
    {
        Step(1);
    }

    void PreviousTarget()
    {
        Step(-1);
    }

    void WhereIsTarget()
    {
        if (!g_exploring) return;
        Target* target = Selected();
        if (!target)
        {
            speech::Now(locale::Mod(L"explore.notarget"));
            return;
        }
        UObject* pawn = Pawn();
        Vec here;
        double yaw = 0.0;
        if (!pawn || !ActorLocation(pawn, here) || !CameraYaw(yaw)) return;
        target->distance = Distance(here, target->position);
        target->bearing = BearingDegrees(here, target->position, yaw);
        EnsureRoute(pawn, here, *target, gamethread::NowSeconds(), 0.0, true);
        speech::Now(TargetText(*target, here, yaw));
    }

    void WalkToTarget()
    {
        if (g_walking)
            StopWalk(L"explore.walk.stop", true);
        else
            StartWalk();
    }

    bool LookBeaconOn()
    {
        return g_lookSound;
    }

    void ToggleLookBeacon()
    {
        g_lookSound = !g_lookSound;
        log::Info(L"explore: look beacon {}", g_lookSound ? L"on" : L"off");
        speech::Now(locale::Mod(g_lookSound ? L"explore.looksound.on" : L"explore.looksound.off"));
        if (!cfg::Persist(L"Exploration", L"LookSound", g_lookSound ? L"1" : L"0")) log::Error(L"explore: the look beacon setting could not be saved");
    }

    bool CharacterInInteraction()
    {
        UObject* pawn = Pawn();
        return pawn && BindingState(pawn) == 2;
    }

    bool ExplorationActive()
    {
        return g_exploring;
    }

    bool WalkInProgress()
    {
        return g_walking;
    }

    UObject* OfferedUseLocation()
    {
        return g_exploring && obj::IsLive(g_offered) ? g_offered : nullptr;
    }

    void ToggleBeacon()
    {
        // While a look is led the key is the look beacon's: the beacon can be off for a walk and
        // the look still led, or the other way round.
        if (LookLed())
        {
            ToggleLookBeacon();
            return;
        }
        g_beacon = !g_beacon;
        log::Info(L"explore: beacon {}", g_beacon ? L"on" : L"off");
        speech::Now(locale::Mod(g_beacon ? L"explore.beacon.on" : L"explore.beacon.off"));
        if (!cfg::Persist(L"Exploration", L"Beacon", g_beacon ? L"1" : L"0")) log::Error(L"explore: the beacon setting could not be saved");
    }

    void DumpExploration()
    {
        obj::SafeInvokeLogged(L"exploration.Dump", [](void*) { DumpImpl(); }, nullptr);
    }

    std::wstring UseLocationLabelOf(UObject* useLocation)
    {
        if (!obj::IsLive(useLocation)) return {};
        std::wstring coreName;
        return UseLocationLabel(useLocation, coreName);
    }

    UObject* UseLocationOfGlintWidget(UObject* glintWidget)
    {
        if (!obj::IsLive(glintWidget)) return nullptr;
        // The widgets are pooled, and a glint gone from the character may still point at the
        // one it had: the glint that holds the widget is the one spawned for her now.
        const auto holds = [&](UObject* glint)
        {
            UObject* widget = nullptr;
            const auto* spawned = static_cast<const uint8_t*>(sym::MemberOf(glint, L"ABatesGlint.bIsSpawned"));
            return obj::IsLive(glint) && (!spawned || *spawned != 0) && obj::ReadObject(glint, L"GlintWidget", widget) && widget == glintWidget;
        };
        for (const auto& [useLocation, glint] : g_glintOf)
        {
            if (obj::IsLive(useLocation) && holds(glint)) return useLocation;
        }
        // Every object is looked through for the widget's glint at most every few seconds.
        static std::map<UObject*, double> missedAt;
        const double now = gamethread::NowSeconds();
        if (const auto missed = missedAt.find(glintWidget); missed != missedAt.end() && now - missed->second < 5.0) return nullptr;
        for (auto* glint : obj::FindAllLive(L"BatesGlint"))
        {
            if (!holds(glint)) continue;
            UObject* owner = nullptr;
            obj::CallReturn(glint, L"GetOwningUseLocation", [&](void* params, FProperty* returnValue) { obj::ReadObjectAt(params, returnValue, owner); });
            if (!obj::IsLive(owner)) break;
            g_glintOf[owner] = glint; // found at once next time
            return owner;
        }
        missedAt[glintWidget] = now;
        return nullptr;
    }
}
