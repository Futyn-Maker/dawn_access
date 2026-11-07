#pragma once
// A shooting stage's guidance worked out without the game, so that it can be tested: which target
// the aim sound leads to, when the word to fire is said, and the blip's pace, side and pitch. What
// a target is and whether a shot now would take it are the game's, read by features/Shooting.

#include <string>
#include <string_view>
#include <vector>

namespace da::shoot
{
    // What a target is called: the designers' own name for the thing, as the things to walk to are
    // named, since a shooting stage is a choice between things a sighted player tells apart at a
    // glance (the squirrel or the sack, the robin or not). A target found by its actor's name is
    // that name ("mike"; "BASA_Robin" is "Robin"); one marked by a place in the level is named by
    // the stage a hit on it leads to, its verb left out ("Shoot_Squirrel" is "Squirrel",
    // "Shoot_Sack_2" is "Sack"). Empty when neither names anything.
    std::wstring TargetLabel(bool foundByName, std::wstring_view actorName, std::wstring_view nextStage);
    // Where a target stands against the aim.
    struct Sight
    {
        bool placed = false;
        double horizontal = 0.0; // degrees to the right of the aim
        double vertical = 0.0;   // degrees above it
        double angle = 0.0;      // degrees off the aim altogether
        bool onTarget = false;   // a shot fired now would take it, by the game's own rule
        bool solid = false;      // and one fired a moment later would too, however the aim sways meanwhile
    };

    struct Candidate
    {
        int index = 0;
        Sight sight;
    };

    // Which target the sound leads to: the one the player chose with a key, alone, while there is
    // a choice; else the one a shot fired now would take, since that is the target a shot takes;
    // else the one nearest the aim, the one led before kept until another is nearer by a clear
    // margin in proportion, so that two targets side by side do not take the sound in turns (the
    // margin is in proportion because targets can stand two or three degrees apart). -1 for none.
    constexpr double kKeepMargin = 0.6;
    int Lead(const std::vector<Candidate>& candidates, int chosen, int leading);

    // The word to fire: said once a shot has been sure of the target it leads to for kFireAfter,
    // so that an aim swept across a target does not call it; again only after the aim has been
    // off it for kFireAgain; a target newly led earns it afresh. The double ping tells the moment
    // (a shot now takes it); the word tells that one a moment later still would.
    constexpr double kFireAfter = 0.2;
    constexpr double kFireAgain = 1.0;
    class FireWord
    {
    public:
        // A poll: the target led (-1 for none) and whether a shot is sure of it; whether the word
        // is said now.
        bool Update(double now, int led, bool solid);
        void Reset();

    private:
        int m_led = -1;
        bool m_called = false;
        double m_onSince = -1.0;
        double m_onAt = 0.0;
    };

    // How a stage ended, by the game's own record of it. A press with nothing highlighted does
    // nothing, so there is no miss: a stage ends with a target shot, with the aim brought where an
    // aim-only stage asks, or by its clock. The game never clears its chosen option between plays
    // of a stage (the range plays a round again after its time ran out), so a hit is a circle the
    // game took down as shot, or a chosen option other than the one the stage began with. The game
    // clears its timed-out flag as a stage starts; should a round played again ever begin with it
    // still set, its clock tells this play's timeout.
    enum class Verdict
    {
        None,       // ended with the world standing still, or by the story: nothing to tell
        Hit,        // a target was shot
        AimReached, // an aim-only stage got the aim it asked for
        TimedOut,   // the time ran out
    };
    struct Ending
    {
        bool still = false;          // the world stood still as it ended (paused, a screen up, the way out)
        bool shotCircle = false;     // a target's circle went with the game's verdict "shot"
        bool chosenChanged = false;  // the stage's chosen option is not the one it began with
        bool timedOutNow = false;    // its timed-out flag is set now
        bool timedOutBefore = false; // and was set as it began
        bool timed = false;
        double seconds = 0.0; // its clock as it ended
        double limit = 0.0;   // and its time
        bool aimingOnly = false;
        bool aimReached = false;
        // The player saw neither the targets nor the reticle, and the stage was no dilemma played by
        // the stick alone: the game resolved it by itself, and it has no verdict to tell. After the
        // missed shot at the Wendigo in act3_2 (research/sessions/s49-full.log, 00:26:21), the hidden
        // stage Wendigo_Dodge took its only option, "aim timeout", 0.15 s in, and the hit's tone was
        // played for the game's own timeout branch.
        bool unseen = false;
    };
    // A clock within this of its time has run out.
    constexpr double kClockSlack = 0.05;
    Verdict Judge(const Ending& e);

    // The aim sound: a blip on the side of the target, higher when it stands above the aim and
    // lower below, quicker as the aim nears it, a quick double ping while a shot would take it,
    // muffled when it stands behind.
    struct Blip
    {
        double pan = 0.0;   // -1 left .. 1 right
        double level = 0.5; // 0 low .. 1 high
        bool locked = false;
        bool muffled = false;
    };
    constexpr double kOnTargetInterval = 0.12;
    double BlipInterval(const Sight& s);
    Blip BlipOf(const Sight& s);

    // An aim-only stage's completion conditions (FBatesAimOnlyStageCompletionCondition): an axis
    // (EBatesCameraAxis: 0 pitch, 1 yaw), the value, and the operator the stage's aim is held to
    // against it (EBMFloatParameterOperator: 0 ==, 1 !=, 2 <, 3 <=, 4 >, 5 >=). The stage counts its
    // aim from the direction it began in, CachedYaw growing to the right and CachedPitch upward. The
    // chapter 4 rifle scope (act3_2's use_scope) completes once its yaw is 20 or more, with no target
    // and no clock: the scene waits for the turn.
    struct AimCondition
    {
        int axis = 1;
        double value = 0.0;
        int op = 5;
    };
    bool ConditionHolds(const AimCondition& c, double yaw, double pitch);
    // Where the aim must go for every condition to hold, as a sight from the aim now: placed when
    // some condition names a way to turn, past each threshold by kAimGoalMargin so that the sound
    // leads over its edge, and on target once all hold.
    constexpr double kAimGoalMargin = 3.0;
    Sight AimGoal(const std::vector<AimCondition>& conditions, double yaw, double pitch);

    // The aim's blips come at the interval of how near the aim is, and at once when it comes on
    // target: chapter one's telescope took the view in the frame it came to rest on Emily and Mike,
    // 28 ms before the interval let the next blip come, and the double ping was never heard
    // (2026-11-02); a shot can light up as briefly before its clock runs out.
    class Cadence
    {
    public:
        // Whether a blip is due now, the aim on target or not, at `interval` from the last.
        bool Due(double now, bool onTarget, double interval);
        void Reset()
        {
            m_last = -1000.0;
            m_onTarget = false;
        }
        // The next blip waits its interval from now (a stage going on after a pause).
        void Rest(double now)
        {
            m_last = now;
        }

    private:
        double m_last = -1000.0;
        bool m_onTarget = false;
    };

    // ---- the Chris and Ashley dilemma ---------------------------------------------------------

    // act5_3's death room, the game's only stages with bIsChrisAshleyDilemma: the game ignores the
    // targets' places and the vertical axis, and highlights by where the aim's yaw stands between the
    // stage's limits (UBatesShootingStage::UpdateOptionsChrisAshley): t = (CachedYaw - MinYaw) /
    // (MaxYaw - MinYaw), the first option where t < (1 - M) / 2, the second where t > (1 + M) / 2, M the
    // stage's ChrisAshleyMiddleChoiceSize (0.5: the outer quarters), the middle nothing
    // (research/shooting/notes.md, section 9). The stick's right raises the yaw (UpdateInputAngles), so
    // the first option lies to the stick's left unless the aiming's X axis is inverted. The level puts
    // Chris first (the gun on himself: Shoot_Chris, Aim_Chris_Seq_0) and Ashley second
    // (research/mechanics/json/act5_3/.../LV_Activities_deathroom.json). The first turn (StartPose)
    // hides targets and reticle and commits on the highlight (bFinishOnHighlight); each shot after it
    // (ShootChoice_*) waits 25 s for R2 on the side highlighted, the clock running out shooting nobody.
    struct DilemmaPlace
    {
        double t = 0.5;        // where the aim stands between the limits, 0 to 1
        int region = -1;       // 0 the first option's, 1 the second's, -1 the middle
        double toFirst = 0.0;  // degrees of yaw to go into the first option's region (0 within it)
        double toSecond = 0.0; // and into the second's
    };
    DilemmaPlace DilemmaAt(double yaw, double minYaw, double maxYaw, double middle);

    // The aim's lead in the dilemma: toward `option` (0 or 1) when the player has chosen one by key,
    // past the edge of its region by kDilemmaMargin degrees, on target within it; with none chosen, no
    // lead to either side (the side is the player's choice) but the aim's place between the two, as
    // the gun turning between them is seen: the blip's pan from the first's side (-1) to the second's
    // (+1), at a steady pace, on target while either side is highlighted. `invert`: the stick's right
    // lowers the yaw.
    constexpr double kDilemmaMargin = 0.5;
    constexpr double kDilemmaPace = 0.3;
    struct DilemmaLead
    {
        Blip blip;
        double interval = kDilemmaPace;
    };
    DilemmaLead LeadDilemma(const DilemmaPlace& p, int option, bool highlighted, bool invert);
}
