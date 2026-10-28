#include "features/Exploration.hpp"

#include "features/Choices.hpp"
#include "features/DontMove.hpp"
#include "features/ExploreText.hpp"
#include "features/GridPath.hpp"
#include "features/GroundFile.hpp"
#include "features/Hold.hpp"
#include "features/LookTurn.hpp"
#include "features/Qte.hpp"
#include "features/WayList.hpp"
#include "features/WorldRead.hpp"

#include "core/Config.hpp"
#include "core/GameThread.hpp"
#include "core/Gate.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "core/Symbols.hpp"
#include "diag/Diagnostics.hpp"
#include "hotkeys/Hotkeys.hpp"
#include "input/InputNames.hpp"
#include "locale/Locale.hpp"
#include "speech/Sounds.hpp"
#include "speech/Speech.hpp"
#include "watch/GameState.hpp"
#include "watch/Watchers.hpp"

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
            std::wstring kind;   // the class of the interaction behind a use location
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
        // from the script asset the game loads for it.
        struct LuaEvent
        {
            std::wstring trigger;
            int limit = -1; // -1 every time, else so many times
            std::vector<std::wstring> calls;
            std::vector<std::wstring> activities;
            bool story = false; // it moves the scene on: a sequence, a movie, a level
        };

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
        bool g_inputTaken = false;  // the controller takes movement input
        std::wstring g_stateName;   // the game state as last logged
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
        std::map<UObject*, UseState> g_useStates;
        // The ways on, read now and then.
        double g_waysScannedAt = -10.0;
        std::vector<Target> g_ways;
        std::map<UObject*, std::wstring> g_wayVerdict;
        std::map<UObject*, std::wstring> g_wayNote; // why a way on is left out besides its verdict (a twin, beside a thing), as last logged
        std::vector<UObject*> g_waysAnnounced;
        std::set<UObject*> g_waysEntered; // volumes the character has stood in, this level
        // The level's trigger scripts, read once per script asset.
        std::map<UObject*, LuaEvent> g_luaByAsset;
        std::map<std::wstring, LuaEvent> g_luaEvents; // by trigger name
        double g_luaScannedAt = -10.0;
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
        };
        ReachMap g_reach;
        constexpr int kFloodExpansions = 80000;   // the most cells a map may open
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

        // The use locations the game offers the character right now: those its prompt is up
        // for. Pressing the interaction key uses the first of them.
        std::vector<UObject*> OfferedUseLocations(UObject* pawn)
        {
            std::vector<UObject*> out;
            if (UObject* component = InteractionComponent(pawn)) obj::ReadObjectArray(component, L"AvailableUseLocations", out);
            std::erase_if(out, [](UObject* actor) { return !obj::IsLive(actor); });
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

        // Whether the game lets this character use the use location: anyone may, or the one
        // character it names.
        bool ForThisCharacter(UObject* actor)
        {
            bool anyone = false;
            obj::ReadBool(actor, L"bCanAnyoneInteract", anyone);
            if (anyone) return true;
            int64_t who = -1;
            obj::ReadInt(actor, L"CharacterToInteract", who);
            return g_playerCharacter >= 0 && who == g_playerCharacter;
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

        // Reads what a trigger's script does from the asset the game loads for it: the
        // trigger it answers, how many times, and the calls it makes. A script that starts
        // a sequence, an activity of the story, a movie or the next level moves the scene
        // on; one that only shows a lesson, streams a section, wakes a thing up or plays an
        // ambient animation does not.
        bool ParseLuaEvent(const std::wstring& script, LuaEvent& out)
        {
            static const std::wregex header(LR"re(gEvents\["EnterTrigger"\]\["([^"]+)"\])re");
            static const std::wregex limit(LR"re(triggerLimit\s*=\s*(-?\d+))re");
            static const std::wregex call(LR"re((SF_[A-Za-z0-9_]+)\s*\()re");
            static const std::wregex activity(LR"re(activit(?:y|ies)\s*=\s*"([^"]*)")re");
            std::wsmatch m;
            if (!std::regex_search(script, m, header)) return false;
            out.trigger = m[1].str();
            if (std::regex_search(script, m, limit)) out.limit = std::stoi(m[1].str());
            for (auto it = std::wsregex_iterator(script.begin(), script.end(), call); it != std::wsregex_iterator(); ++it)
            {
                const auto name = (*it)[1].str();
                if (name == L"SF_Start" || name == L"SF_End") continue;
                if (std::find(out.calls.begin(), out.calls.end(), name) == out.calls.end()) out.calls.push_back(name);
            }
            for (auto it = std::wsregex_iterator(script.begin(), script.end(), activity); it != std::wsregex_iterator(); ++it)
            {
                for (const auto& one : str::Split((*it)[1].str(), L','))
                {
                    if (!one.empty()) out.activities.push_back(one);
                }
            }
            const bool storyActivity = std::any_of(out.activities.begin(), out.activities.end(),
                                                   [](const std::wstring& a) { return !str::StartsWith(a, L"PAO_") && !str::StartsWith(a, L"pao_"); });
            static const wchar_t* const kStory[] = {L"SF_PlayMovie",          L"SF_LoadNextLevel",         L"SF_Teleport",
                                                    L"SF_SetPlayerCharacter", L"SF_CutToActivity",         L"SF_StartSequence",
                                                    L"SF_OrderStartActivity", L"SF_StartTotemFlowCommand", L"SF_SetAllowLocomotionInTutorials",
                                                    L"SF_SetCurrentObjective"};
            out.story = storyActivity;
            for (const auto& name : out.calls)
            {
                if (std::find(std::begin(kStory), std::end(kStory), name) != std::end(kStory)) out.story = true;
            }
            return true;
        }

        void ScanLuaEvents(double now)
        {
            if (now - g_luaScannedAt < 10.0) return;
            g_luaScannedAt = now;
            const auto assets = obj::FindAllLive(L"LuaScriptAsset");
            bool changed = false;
            for (auto* asset : assets)
            {
                if (g_luaByAsset.count(asset)) continue;
                const auto name = obj::ObjectName(asset);
                if (!str::EndsWith(name, L"-Enter")) continue;
                std::wstring script;
                if (!obj::ReadString(asset, L"Script", script)) continue;
                LuaEvent event;
                if (!ParseLuaEvent(script, event)) continue;
                g_luaByAsset[asset] = event;
                g_luaEvents[event.trigger] = event;
                changed = true;
            }
            std::erase_if(g_luaByAsset, [&](const auto& item) { return std::find(assets.begin(), assets.end(), item.first) == assets.end(); });
            if (changed)
            {
                std::vector<std::wstring> lines;
                for (const auto& [trigger, event] : g_luaEvents)
                    lines.push_back(std::format(L"{} ({}; {}{})", trigger, event.limit < 0 ? L"every time" : std::format(L"{} time(s)", event.limit),
                                                str::Join(event.calls, L" "), event.story ? L"; moves the scene on" : L""));
                log::Info(L"explore: the level's trigger scripts: {}", lines.empty() ? L"none" : str::Join(lines, L"; "));
            }
        }

        // ---- what is where -----------------------------------------------------------------

        // The level's actors of interest, found now and then: the use locations, the glints
        // that mark them, and the trigger volumes.
        void ScanLevel(double now)
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
            g_volumes.clear();
            for (auto* volume : obj::FindAllLive(L"BatesCharacterTriggerVolume"))
            {
                if (obj::IsA(volume, L"BatesUseLocation") || obj::IsA(volume, L"BatesGlint") || obj::IsA(volume, L"BatesGuidanceTriggerVolume") ||
                    obj::IsA(volume, L"BatesGuidancePassageVolume"))
                    continue;
                g_volumes.push_back(volume);
            }
            std::erase_if(g_useStates, [](const auto& item) { return !obj::IsLive(item.first); });
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

        // The ways on the level is waiting for right now: the trigger volumes that are on,
        // answer the character's walking, and move the scene on when entered, by the game's
        // own actions on them or by the level's script. They carry no marker and no name on
        // screen, and what a player sees is the ground leading to them, so they are offered
        // by where they are and never by what they are called.
        std::vector<Target> GatherWays(UObject* pawn, const Vec& here, double range)
        {
            std::vector<Target> ways;
            std::vector<std::wstring> verdicts;
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
                const bool spent = lua != g_luaEvents.end() && lua->second.limit > 0 && g_waysEntered.count(volume) > 0;
                const bool luaLive = luaStory && !spent;
                std::wstring why;
                if (!collision)
                    why = L"off";
                else if (type == 1)
                    why = L"for activities only";
                else if (activator != 0)
                    why = L"set off by the camera";
                else if (!nativeStory && !luaStory)
                    why = actions.empty() && lua == g_luaEvents.end() ? L"nothing happens there" : L"nothing of the story happens there";
                else if (!nativeNow && !luaLive)
                    why = nativeStory && !nativeLeft ? L"already set off" : nativeStory ? L"its condition is not met now" : L"already walked into";
                const auto verdict = (name.empty() ? obj::ObjectName(volume) : name) + L": " + (why.empty() ? L"a way on" : why);
                if (g_wayVerdict[volume] != verdict)
                {
                    // A trigger the scene went through moves the story on, and what it sets
                    // going may open the ground (a gate, a character leaving the way).
                    if ((why == L"already set off" || why == L"already walked into") && !g_wayVerdict[volume].empty())
                        g_grid.storyMovedAt = gamethread::NowSeconds();
                    g_wayVerdict[volume] = verdict;
                    log::Info(L"explore: trigger {} ({}{})", verdict, str::Join(actions, L", "),
                              lua != g_luaEvents.end() ? L"; script " + str::Join(lua->second.calls, L" ") : std::wstring());
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
                }
                else if (!ActorLocation(volume, t.position))
                {
                    continue;
                }
                if (Distance(here, t.position) > range) continue;
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
            return ways;
        }

        Route FindRoute(UObject* pawn, const Vec& here, const Target& target, std::vector<Vec>* road);
        const Vec& Aim(const Target& t);
        bool NavigationAvailable();
        bool ProjectToMesh(UObject* pawn, const Vec& point, Vec& out, const Vec& extent);
        double SweepClear(UObject* pawn, const Vec& from, const Vec& to, double radius, double halfHeight, std::wstring* hitWhat = nullptr,
                          Vec* reached = nullptr);

        std::vector<Target> Gather(UObject* pawn, const Vec& here, double yaw, double now)
        {
            std::vector<Target> uses;
            const double range = cfg::Get().exploreRange * 100.0;
            ScanLevel(now);
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
                if (Distance(here, t.position) > range) continue;
                t.inside = std::find(offered.begin(), offered.end(), actor) != offered.end();
                uses.push_back(std::move(t));
            }

            // Twins of the same thing are listed once, the nearer first: two use locations
            // with one label at one spot, and the two sides of one door, which the level
            // names by where each leads and sets a step apart.
            std::sort(uses.begin(), uses.end(), [&](const Target& a, const Target& b) { return Distance(here, a.position) < Distance(here, b.position); });
            std::vector<Target> out;
            for (auto& u : uses)
            {
                if (std::any_of(out.begin(), out.end(),
                                [&](const Target& o)
                                {
                                    const double apart = FlatDistance(o.position, u.position);
                                    return (o.label == u.label && apart < kTwinApart) || (!u.kind.empty() && o.kind == u.kind && apart < kDoorSidesApart);
                                }))
                    continue;
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
                if (t.inside) g_waysEntered.insert(t.actor);
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
                // Found only once a second with the roads (ChooseListed), and read at every look.
                it->pathReach = old.pathReach;
                it->unjudged = old.unjudged;
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

        void ChooseListed(UObject* pawn, const Vec& here, double now)
        {
            for (auto& t : g_targets)
                t.behind = g_path.set && !t.reached && !AheadOnPath(here, Aim(t));
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
            // Between a hand-over and its first map nothing judges the roads: the list stands.
            const bool due = (meshRoads || g_reach.flood) && g_lister.Due(g_settle, settlesNow, now);
            const double radius = WalkingRadius(pawn);
            Vec middle;
            double halfHeight = 88.0;
            CapsuleSpan(pawn, middle, halfHeight);
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
                std::map<UObject*, bool> wasUnreachable;
                for (auto& t : g_targets)
                {
                    wasUnreachable[t.actor] = t.unreachable;
                    t.pathReach =
                        t.way && t.hasBox && !t.behind && pathAhead.size() >= 2 && RoadEnters(t.actor, t.boxLow, t.boxHigh, pathAhead, radius, halfHeight);
                    const bool reachable = !t.way || !t.gridEnclosed || t.pathReach;
                    t.unreachable = !reachable;
                    if (t.unreachable != wasUnreachable[t.actor])
                        log::Info(L"explore: the way {} {:.0f} cm off is {}", obj::ObjectName(t.actor), t.distance,
                                  t.unreachable ? L"not offered: the ground found no way to it" : L"offered again: a way to it is open");
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
                e.unreachable = t.unreachable;
                e.unjudged = t.unjudged;
                e.reached = t.reached;
                e.chosen = playerChose(t);
                e.closedOff = t.gridEnclosed || (JudgedByMap(t) && t.reach == 2);
                e.known = Judged(t, meshRoads);
                e.earlier = t.reach == 1 || t.reach == 3;
                e.passed = t.passed;
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
                t.hidden = (t.behind || t.unreachable || t.onRoadOf != nullptr || t.unjudged) && !(t.actor == g_selected && !g_autoChosen);
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
                const wchar_t* why = t.behind        ? L"behind on the path"
                                     : t.unreachable ? L"closed off"
                                     : t.onRoadOf    ? L"on the road to another"
                                     : t.unjudged    ? L"its ground not known yet"
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

        // Whether the level has navigation data to ask for roads. The prologue's levels ship
        // none, and there the roads are straight lines.
        bool NavigationAvailable()
        {
            UObject* system = obj::FindFirstLive(L"NavigationSystemV1");
            if (!system) return false;
            std::vector<UObject*> set;
            obj::ReadObjectArray(system, L"NavDataSet", set);
            return std::any_of(set.begin(), set.end(), [](UObject* data) { return obj::IsLive(data); });
        }

        // Whether the navigation mesh lies under the character, asked now and then: only then does
        // it give roads, and the ground is felt out instead where it does not. The prologue's levels
        // hold a mesh with no tiles near the character, and its mere presence once had every way
        // on taken as known.
        bool g_meshHere = false;
        double g_meshAskedAt = -100.0;

        bool MeshHere(UObject* pawn, const Vec& here, double now)
        {
            if (now - g_meshAskedAt < 3.0) return g_meshHere;
            g_meshAskedAt = now;
            Vec ground;
            const bool mesh = NavigationAvailable() && ProjectToMesh(pawn, here, ground, Vec{150.0, 150.0, 250.0});
            if (mesh != g_meshHere) log::Info(L"explore: the navigation mesh {} under the character", mesh ? L"lies" : L"no longer lies");
            g_meshHere = mesh;
            return mesh;
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
                      names.empty() ? L"no navigation data, so the ways are straight lines" : str::Join(names, L"; "), bounds.size());
            // Whether the mesh reaches the character at all: without it under the character's
            // feet, no road can be asked for, and the ways are straight lines.
            UObject* pawn = Pawn();
            Vec here, ground;
            if (pawn && ActorLocation(pawn, here))
            {
                g_meshHere = ProjectToMesh(pawn, here, ground, Vec{150.0, 150.0, 250.0});
                g_meshAskedAt = gamethread::NowSeconds();
                if (g_meshHere)
                    log::Info(L"explore: the mesh lies under the character, {:.0f} cm below its middle", here.z - ground.z);
                else
                    log::Info(L"explore: no mesh within 1.5 m of the character, so the ground is felt out instead");
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

        double DistanceToSegment(const Vec& p, const Vec& a, const Vec& b)
        {
            const double abx = b.x - a.x;
            const double aby = b.y - a.y;
            const double length = abx * abx + aby * aby;
            double along = 0.0;
            if (length > 1.0) along = std::clamp(((p.x - a.x) * abx + (p.y - a.y) * aby) / length, 0.0, 1.0);
            return FlatDistance(p, Vec{a.x + abx * along, a.y + aby * along, 0.0});
        }

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
            t.library->ProcessEvent(t.fn, params);
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
                clear = SweepClear(pawn, from, to, g_grid.radius, SweepHalfHeight(), nullptr, &reached);
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
                if (!free) why = 'W';
            }
            g_grid.sweepMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            g_grid.steps[key] = free ? b.level : kNoFloor;
            if (!free) g_grid.why[key] = why;
            if (why == 'W') g_grid.refusedAt[key] = gamethread::NowSeconds();
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
                radius = std::max(1.5, std::min(t.boxHigh.x - t.boxLow.x, t.boxHigh.y - t.boxLow.y) / 2.0 / kCellSize);
                // A volume's aim is its middle; a tall one is entered from well below it.
                levels = std::max(kGoalLevels, (LevelOf(t.boxHigh.z) - LevelOf(t.boxLow.z)) / 2 + 2);
            }
        }

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
            g_grid.search = std::make_unique<grid::Search>(start, goal, goalRadius, goalLevels, radiusCells, kGridExpansions, kLevelSize / kCellSize);
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
        bool OnMappedGround(const Vec& middle, double halfHeight)
        {
            if (!g_reach.flood) return false;
            const grid::Cell here = CellOf(middle, middle.z - halfHeight);
            for (int level = here.level - 1; level <= here.level + 1; ++level)
            {
                if (g_reach.flood->Reached(grid::Cell{here.x, here.y, level})) return true;
            }
            return false;
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
            const int radiusCells = static_cast<int>(cfg::Get().exploreRange * 100.0 / kCellSize) + 50;
            const uint64_t generation = g_reach.generation + 1;
            g_reach = ReachMap{};
            g_reach.generation = generation;
            g_reach.flood = std::make_unique<grid::Flood>(start, radiusCells, kFloodExpansions);
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

        bool IsDoor(UObject* useLocation)
        {
            UObject* interaction = nullptr;
            return obj::IsLive(useLocation) && obj::ReadObject(useLocation, L"InteractionActor", interaction) && obj::IsLive(interaction) &&
                   obj::IsA(interaction, L"BatesInteractionDoor");
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
                    grid::Cell goal, best;
                    double radius = 1.5;
                    int levels = kGoalLevels;
                    GoalOf(t, goal, radius, levels);
                    if (flood.BestIn(goal, radius, levels, best)) doors = true;
                }
            }
            for (auto& t : g_targets)
            {
                if (JudgedByMap(t) || (done && t.reachTried == g_reach.generation)) continue;
                grid::Cell goal, best;
                double radius = 1.5;
                int levels = kGoalLevels;
                GoalOf(t, goal, radius, levels);
                if (flood.BestIn(goal, radius, levels, best))
                {
                    const auto cells = flood.PathTo(best);
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
                log::Info(L"explore: the reach map does not reach \"{}\" {}{}; it got nearest {:.0f} cm from it", t.label, obj::ObjectName(t.actor),
                          doors ? L", but a door the character can open stands in the ground it reached" : L": closed off",
                          FlatDistance(WorldOf(nearest), Aim(t)));
                if (t.actor == g_selected) StraightenRoad(t, cells, true, now);
            }
        }

        // Runs the map a slice of a frame at a time, and judges as it goes.
        void PollReachMap(UObject* pawn, double now)
        {
            if (!g_reach.flood || !pawn) return;
            // A done map judges the targets that came after it, out of what it reached.
            if (!ReachRunning())
            {
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
                size_t nearest = 0;
                double best = FlatDistance(here, t.gridCorners[0]);
                for (size_t i = 1; i < t.gridCorners.size(); ++i)
                {
                    const double d = DistanceToSegment(here, t.gridCorners[i - 1], t.gridCorners[i]);
                    if (d < best)
                    {
                        best = d;
                        nearest = i;
                    }
                }
                if (best < 150.0)
                {
                    size_t next = nearest;
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
            size_t from = 0;
            double best = FlatDistance(here, corners[0]);
            for (size_t i = 1; i < walkable; ++i)
            {
                const double d = DistanceToSegment(here, corners[i - 1], corners[i]);
                if (d < best)
                {
                    best = d;
                    from = i;
                }
            }
            if (best > 150.0) return road;
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
        double SweepClear(UObject* pawn, const Vec& middle, const Vec& end, double radius, double halfHeight, std::wstring* hitWhat, Vec* reached)
        {
            if (!Prepare(g_capsuleTrace, L"CapsuleTraceSingleByProfile")) return -1.0;
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
        double g_readySince = -1.0;  // since when the press would be taken, the keys let go
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

        // Runs the last stretch; false while the walk is still on its way there.
        bool PollArrival(UObject* pawn, Target& t, const Vec& here, double yaw, double now)
        {
            if (t.way || !t.inside || !obj::IsLive(t.actor))
            {
                g_arriveSince = -1.0;
                return false;
            }
            const PressTest p = ReadPressTest(pawn, t.actor);
            if (g_arriveSince < 0.0)
            {
                g_arriveSince = now;
                g_readySince = -1.0;
                g_flicker.Reset(now);
                g_arriveLogged.clear();
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
            // Ready, or starting by itself: she is let go and comes to rest.
            if (p.verdict || p.autoStart)
            {
                g_flicker.Hold(now, facing);
                input::ReleaseWalkKeys();
                if (g_readySince < 0.0) g_readySince = now;
                if (!still && now - g_readySince < 2.0) return true;
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
                log::Info(L"explore: the walk to \"{}\" ends where the press is taken", t.label);
                StopWalk(nullptr, false);
                return true;
            }
            g_readySince = -1.0;
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
            // Only the camera's look is missing: that turn is the player's.
            if (p.inside && p.characterOk)
            {
                g_flicker.Hold(now, facing);
                input::ReleaseWalkKeys();
                if (!still) return true;
                log::Info(L"explore: the walk to \"{}\" ends there, the camera to look at it", t.label);
                StopWalk(nullptr, false);
                return true;
            }
            // The press asks for a glint and the thing has none: no turn of hers wins it.
            if (p.inside && p.needGlint && (p.needCamera || p.needCharacter) && !p.glint)
            {
                g_flicker.Hold(now, facing);
                input::ReleaseWalkKeys();
                return true;
            }
            // Her turn toward the glint, or a step further in toward her spot: flicks, in the frame
            // the stick works in.
            const double bearing = BearingDegrees(here, p.inside ? LookAtPoint(t) : Aim(t), yaw);
            const auto push = g_flicker.Update(now, bearing, facing, CharacterSpeed(pawn));
            if (g_flicker.Began())
                log::Info(L"explore: the walk's last stretch to \"{}\": flick {} toward {:+.0f} deg, to {} (she faces {:+.0f} deg off the glint)", t.label,
                          g_flicker.Count(), bearing, p.inside ? L"turn her to the glint" : L"step her toward her spot", p.characterTurn);
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
        UObject* g_film = nullptr;          // the last full-screen film the screen watcher saw

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
            if (g_film && obj::IsLive(g_film) && obj::IsA(g_film, L"BatesFullscreenVideoWidget") && obj::IsWidgetShown(g_film)) return L"a film";
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
            const auto gameState = gamestate::Current();
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
                g_targets.clear();
                g_selected = nullptr;
                g_autoChosen = false;
                g_pendingNew.clear();
                g_waysAnnounced.clear();
                g_waysEntered.clear();
                g_wayVerdict.clear();
                g_wayNote.clear();
                g_gone.clear();
                g_scannedAt = -10.0;
                g_waysScannedAt = -10.0;
                g_luaScannedAt = -10.0;
                g_settle.Reset();
            }
            // A short exchange in the middle of a scene leaves everything where it was, so
            // the list is not said again; only what the level has added is.
            else if (!g_exploring && !g_targets.empty() && FlatDistance(here, g_leftAtPosition) < 2000.0 && (g_leftForPause || now - g_leftAt < 30.0))
            {
                g_exploring = true;
            }
            // A hold begun afresh lists no way before its first map has settled the list; a resumed
            // one keeps the list it left while its map settles.
            if (!g_exploring)
            {
                g_settle.Reset();
                // A map still standing (the pause left it, and no short exchange resumed the list)
                // is this hold's first: the list settles on it, at once when it is done.
                if (g_reach.flood) g_settle.Begin(now);
            }
            const auto offered = OfferedUseLocations(pawn);
            UObject* first = offered.empty() ? nullptr : offered.front();
            // The targets before this look, and whether the player had chosen each one: a use
            // location the player chose that leaves the list (its interaction in progress) may
            // come back and be chosen again; a way on that leaves it has been set off for good.
            std::vector<std::pair<UObject*, bool>> before;
            for (const auto& t : g_targets)
                before.emplace_back(t.actor, t.actor == g_selected && !g_autoChosen && !t.way);
            auto fresh = Gather(pawn, here, yaw, now);
            // A way on beside a named thing (within 250 cm of its spot) is left out: the thing
            // is the way, and the player who goes to it sets the volume off. One on the road
            // to a listed target is told apart once the roads are known (ChooseListed).
            std::vector<Vec> things;
            for (const auto& u : fresh)
            {
                if (!u.way) things.push_back(Aim(u));
            }
            std::erase_if(fresh,
                          [&](const Target& w)
                          {
                              if (!w.way) return false;
                              const wchar_t* why = nullptr;
                              for (const Vec& thing : things)
                              {
                                  if (FlatDistance(thing, w.position) < 250.0 && std::fabs(thing.z - w.position.z) < 200.0) why = L"beside a thing listed";
                              }
                              if (!why)
                              {
                                  g_wayNote.erase(w.actor);
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
            if (Target* chosen = Selected(); chosen && g_autoChosen && chosen->hidden && g_exploring && !(chosen->unjudged && !g_settle.Settled()))
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
                    if (t.way && (!Judged(t, meshRoads) || g_waysSettling))
                    {
                        g_pendingNew.emplace(t.actor, now);
                        continue;
                    }
                    if (t.hidden) continue;
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
                    if ((!Judged(*t, meshRoads) || g_waysSettling) && now - it->second < 20.0)
                    {
                        ++it;
                        continue;
                    }
                    if (!t->hidden && !t->reached && t->actor != saidNow) fresh.push_back(&*t);
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
            // A flick is one frame's push: let go here, before anything else, whatever the walk does
            // this frame, since one held longer starts a walk (look::Flicker).
            if (g_flicker.Pushing() && gamethread::FrameCount() != g_flickFrame)
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
            ScanLevel(gamethread::NowSeconds());
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
        watch::OnWidgetClass(L"BatesFullscreenVideoWidget",
                             [](UObject* widget, bool appeared)
                             {
                                 if (appeared) g_film = widget;
                             });
        diag::RegisterCommand(L"explore", [](const std::wstring&) { DumpExploration(); });
        gamethread::AddPoller(L"explore", &Poll);
    }

    void ExplorationFeature::Describe(std::vector<std::wstring>& out)
    {
        if (!g_exploring) return;
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

    bool AimSoundOn()
    {
        return g_lookSound;
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
            g_lookSound = !g_lookSound;
            log::Info(L"explore: look beacon {}", g_lookSound ? L"on" : L"off");
            speech::Now(locale::Mod(g_lookSound ? L"explore.looksound.on" : L"explore.looksound.off"));
            if (!cfg::Persist(L"Exploration", L"LookSound", g_lookSound ? L"1" : L"0")) log::Error(L"explore: the look beacon setting could not be saved");
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
