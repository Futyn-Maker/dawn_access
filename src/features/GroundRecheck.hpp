#pragma once
// When the ground the mod has felt out stops being the ground she walks on. The ground keeps a step
// found free for as long as the world holds (features/WorldStream): what the game changes between
// two holds opens ways far more often than it closes them, and asking every step anew had every map
// sweep the whole ground again. But the game also closes ways under the player's feet, with no hold
// between: in the lodge's cinema on 2026-11-08 Ashley walked into the volume that slams the cinema's
// exit door, the door shut the doorway to the hall, and every map after it took her through the
// doorway on the steps found free before; the vase, the hall's photo and the hall itself stayed
// listed, and every walk to them stopped against the door (research/sessions/s57-full.log, 02:03:13
// to 02:05:27; the steps re-swept after the stall met something of the persistent level, the
// slammed door, where the free ones of the doorway were never asked again).
//
// Two things say the ground has changed. A walk that gets nowhere: the steps around her and under
// the way ahead are asked again, and any found shut that was free means the ground has changed
// there (ColumnsAhead, StepsIn). And a prop the game moves: the bodies of a door a sequence slams
// stand where they did not, and the steps about it, before and after, are asked again (PropWatch,
// ColumnsInBox). What was found changed is forgotten for a stretch around it (ColumnsAround), and
// the list is judged afresh by a map begun since.

#include "features/Vec.hpp"

#include <climits>
#include <cstdint>
#include <optional>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

namespace da::recheck
{
    using Vec = features::world::Vec;
    using Column = std::pair<int, int>;
    using StepKey = std::tuple<int, int, int, int, int>; // from x, y, level, to x, y

    // The grid's columns, as Exploration lays them: a cell `cell` cm wide, its corner at `origin`.
    struct Grid
    {
        Vec origin;
        double cell = 20.0;
        Column ColumnOf(const Vec& at) const;
    };

    // The columns a walk that gets nowhere asks again: those within `radius` cells of her own and
    // of the way ahead (`ahead`: the corners she walks to, in order), up to `reach` cm along it.
    std::set<Column> ColumnsAhead(const Grid& grid, const Vec& here, const std::vector<Vec>& ahead, double reach, int radius);

    // The columns within `radius` cells of any of `cells`.
    std::set<Column> ColumnsAround(const std::vector<Column>& cells, int radius);

    // The columns under a box grown by `margin` cm on every side.
    std::set<Column> ColumnsInBox(const Grid& grid, const Vec& low, const Vec& high, double margin);

    // The steps of `steps` (keyed as the ground keys them) that start or land in one of `columns`.
    template <class Steps>
    std::vector<StepKey> StepsIn(const Steps& steps, const std::set<Column>& columns)
    {
        std::vector<StepKey> out;
        for (const auto& [key, value] : steps)
        {
            const auto& [ax, ay, alevel, bx, by] = key;
            if (columns.count({ax, ay}) || columns.count({bx, by})) out.push_back(key);
        }
        return out;
    }

    // The answer the ground keeps for a step (Exploration's kNoFloor): refused; any other answer is
    // the level the step lands on.
    constexpr int kRefused = INT_MIN;

    // The steps of `steps` that start or land in `columns`, each with the answer the ground gives it
    // now: a refusal whose step back is known free is free (`back`: grid::MirroredLanding's landing
    // for the step, none when no step back is known free), as every map takes it. With `freeOnly`,
    // only those free. Weighed against the stored refusal instead, a step refused one way in a passage
    // no wider than she is would be found opened when asked again, its step back answering for it:
    // the lodge's cellar kept 460 such of its 7545 refusals on 2026-11-08, one by the cupboard door
    // the ghost scare swung open (research/dumps/ground-20261108-110204.tsv).
    template <class Steps, class Back>
    std::vector<std::pair<StepKey, int>> KeptAnswers(const Steps& steps, const std::set<Column>& columns, bool freeOnly, const Back& back)
    {
        std::vector<std::pair<StepKey, int>> kept;
        for (const auto& key : StepsIn(steps, columns))
        {
            int answer = steps.at(key);
            if (answer == kRefused)
            {
                if (const std::optional<int> landing = back(key)) answer = *landing;
            }
            if (!freeOnly || answer != kRefused) kept.emplace_back(key, answer);
        }
        return kept;
    }

    // A stretch of the ground asked again, its steps' answers weighed against those the ground kept.
    // The steps are forgotten first, then asked a few a frame (Next, Answer): what changed is what
    // answers otherwise now (Changed). A walk that gets nowhere asks again only what was free, for
    // what shut her way; a prop the game has moved, every step about it, for a door slammed shut or
    // swung open. A prop that has moved and stops nobody, Chris's torch carried about the cellar on
    // 2026-11-08, leaves every answer as it was: nothing is forgotten and no map is begun (the
    // session's 338 moves of the companions' torches had each forgotten the ground about them and
    // given the list's verdicts anew, research/sessions/s58-whole.log).
    class Reask
    {
    public:
        struct Step
        {
            StepKey key;
            int kept = kRefused; // the answer before
            int now = kRefused;  // the answer asked again
        };
        void Begin(std::vector<std::pair<StepKey, int>> kept);
        bool Active() const
        {
            return m_active;
        }
        // The step to ask next, none once each has its answer (the asking is then over).
        std::optional<StepKey> Next();
        // The answer of the step Next gave.
        void Answer(int answer);
        // Every step with its answers, for answering again once all are asked (a step found free
        // one way is free the other: a refusal asked before its step back was found free).
        std::vector<Step>& Steps()
        {
            return m_steps;
        }
        const std::vector<Step>& Steps() const
        {
            return m_steps;
        }
        // Each refusal answered again once every step is asked, as the ground answers it then
        // (`again`: the landing, none for a refusal still): a step whose step back was found free
        // after it was asked is free.
        template <class Again>
        void AnswerRefusalsAgain(const Again& again)
        {
            for (auto& step : m_steps)
            {
                if (step.now != kRefused) continue;
                if (const std::optional<int> landing = again(step.key)) step.now = *landing;
            }
        }
        // The columns where the steps start that are refused now where they were free, or free where
        // they were refused. A free step landing on another level is no change: a floor near a level's
        // boundary is keyed on either, by the sweep that found it or by the step back (in the lodge's
        // cellar on 2026-11-08 a column by the cupboard door held its floor on levels 43 and 44, and a
        // step swept onto 44 is answered 43 by the step back once that is asked first).
        std::vector<Column> Changed() const;
        // How many answers differ, shut and opened.
        size_t Shut() const;
        size_t Opened() const;

    private:
        std::vector<Step> m_steps;
        size_t m_next = 0;
        bool m_active = false;
    };

    // Whether a component stops her capsule's sweep: its collision answers queries (ECollisionEnabled
    // QueryOnly 1, QueryAndPhysics 3, QueryAndProbe 5) and it blocks (ECR_Block 2) the pawn's channel
    // (ECC_Pawn), which her capsule's Pawn profile sweeps as. The game's held items stop no one when
    // held (HeldItemActor overlaps a pawn, HeldItemPhysicsActor ignores one: DefaultEngine.ini); a
    // door's PhysicsActor blocks every channel.
    bool StopsPawn(int64_t collisionEnabled, int64_t responseToPawn);

    // Whether a prop the game has moved can have changed the ground: a door's leaf, which the ground
    // takes for a wall whatever its collision (the game turns a sleeping prop's collision off,
    // UBatesSleepComponent::ApplySleepSettings, so a door a sequence swings open has none once it
    // rests), or a prop with a component that stops her (`stopsHer`, StopsPawn).
    bool Weighed(bool doorLeaf, bool stopsHer);

    // A box in the world, low and high corners.
    struct Box
    {
        Vec low, high;
    };

    // Whether two boxes differ by more than `tolerance` cm at any corner.
    bool Moved(const Box& a, const Box& b, double tolerance);

    // The smallest box holding both.
    Box Union(const Box& a, const Box& b);

    constexpr double kPropTolerance = 3.0; // cm a prop's box may shift without moving (animation's breath)
    constexpr double kPropRest = 1.0;      // seconds a prop that moved must stand still before its ground is asked

    // A prop's box (its colliding components, as the engine bounds them) watched as the game moves
    // it: Update gives each reading, or none where the prop has no collision now; it says once, when
    // a prop that moved (or took up or put down its collision) has stood still for kPropRest, the box
    // it stood in before and the one it stands in now. A prop seen for the first time has not moved;
    // one that never comes to rest is never said.
    class PropWatch
    {
    public:
        bool Update(double now, const std::optional<Box>& box, Box& before, Box& after);

    private:
        bool m_seen = false;
        std::optional<Box> m_rest;   // where it stood at rest, as the ground knows it
        std::optional<Box> m_last;   // the last reading
        bool m_moving = false;
        double m_stillSince = 0.0;
    };
}
