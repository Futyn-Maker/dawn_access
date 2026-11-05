#pragma once
// A camera-nudge interaction (a telescope, the selfie) worked out without the game, so that it can
// be tested: where the framing the scene waits for lies from the view, in the game's own measure;
// how the aim sound and the words lead to it; and the moment a light the scene times a press by
// comes on.

#include "features/ShootingRules.hpp"

namespace da::nudge
{
    // The game's measure of a nudged view (ABatesInteractionComplexCameraNudge::UpdateInteraction):
    // the nudge off the scene's camera as a share of its limits, CameraYaw +1 fully left and
    // CameraPitch +1 fully up; and the limits themselves in degrees (MaxNudgeAngleDegrees over
    // NudgeSensitivity).
    struct View
    {
        double yaw = 0.0;
        double pitch = 0.0;
        double maxYaw = 0.0;
        double maxPitch = 0.0;
    };

    // The framing the scene waits for: the ranges its anim graph tests both shares against,
    // inclusive (the telescope's Emily and Mike, the selfie's good shot).
    struct Box
    {
        double yawMin = 0.0;
        double yawMax = 0.0;
        double pitchMin = 0.0;
        double pitchMax = 0.0;
    };
    // A box the anim graph can be in: ordered ranges within the shares' reach, not every bound zero.
    bool Valid(const Box& b);
    bool Inside(const View& v, const Box& b);

    // Where the framing lies from the view. The lead heads for the middle of the box, not its
    // nearest edge: a view resting on an edge is a hair from leaving it, and the telescope's
    // framing lies a mere 0.3 degrees below its resting view, too little to hear. Every axis still
    // outside the box is named however little it is out, since the game waits for both at once.
    struct Lead
    {
        shoot::Sight sight; // degrees to the middle, right and up; on target once the view is in the box
        int turnRight = 0;  // the yaw outside the box: +1 the view must turn right, -1 left, 0 in
        int turnUp = 0;     // the pitch outside the box: +1 up, -1 down, 0 in
        double right = 0.0; // the middle's offset in shares of the nudge's reach, + right
        double up = 0.0;    // and + up
    };
    Lead Toward(const View& v, const Box& b);

    // The aim sound for a framing, scaled to the view's own reach (a telescope's nudge spans a few
    // degrees, far less than a gun's aim): the blip on the side of the middle, higher when it lies
    // above, quicker as the view nears it, the double ping while the view is in the box.
    shoot::Blip BlipOf(const Lead& l);
    double BlipInterval(const Lead& l);

    // An item in hand turned along one axis of the stick until the game's own measure of the turn
    // reaches the point where it reveals (an examine stage: NormalisedExamineEvaluation, the share
    // of the turn from 0 to 1, against RevealThreshold; the item's anim graph moves to its reveal
    // on bIsRevealing). Pushing the stick the positive way along the stage's axis (right for X, up
    // for Y) raises the share, unless the player's setting turns that axis round
    // (bInvertInteractionInputX/Y, laid over the input itself); the other way lowers it.
    struct Turn
    {
        double share = 0.0;
        double threshold = 0.95;
        bool vertical = false; // along the stick's Y axis rather than its X
        bool inverted = false; // the player's setting turns the axis round
    };

    // The way to push and how much is left. The way does not change on the way there, so the
    // blip keeps to its side all the way and only comes quicker as the point nears.
    struct TurnLead
    {
        int right = 0;          // +1 push right, -1 left; 0 for a turn up or down
        int up = 0;             // +1 push up, -1 down; 0 for a turn to a side
        double remaining = 0.0; // the share still to turn
        bool reached = false;   // at the point or past it, as the game's own test has it
    };
    TurnLead Toward(const Turn& t);
    shoot::Blip BlipOf(const TurnLead& l);
    double BlipInterval(const TurnLead& l);
    // What is left of the way from no turn to the point, in whole per cent: at least 1 until the
    // point is reached, so that a turn still to make is never said to be done.
    int PercentLeft(const Turn& t);

    // What decides whether a turn is led: only where the game has a point to reach, and only
    // while the stick moves the item towards it.
    struct TurnState
    {
        bool usesReveal = false;       // the stage reveals (bUsesReveal)
        bool graphReadsReveal = false; // the item's anim graph reads bIsRevealing at all
        bool fullyHeld = false;        // the stage measures the turn: the item fully in hand
        bool ramping = false;          // a ramp of the stage's own moves the item and the stick counts for nothing
        bool revealing = false;        // the game's flag: the item is revealed
    };

    // A turn's course through one interaction. A stage can measure a reveal its item's graph
    // never looks at (the cigar stubs': their clue comes with the pick-up), and turning such a
    // thing round has no end, so nothing is led there. The reveal is told once, at the poll the
    // game's flag is seen up with the item in hand; after it nothing more is led in the
    // interaction (a note turned back to its front is the player's own look).
    class TurnCourse
    {
    public:
        enum class Event
        {
            None,
            Begins,  // the lead starts (again, once the item is back in hand)
            Reveals, // the game reveals the item
            Ends,    // the lead stops without a reveal: the item left the hand, a ramp took it
        };
        Event Update(const TurnState& s);
        bool Led() const
        {
            return m_led;
        }
        bool Revealed() const
        {
            return m_revealed;
        }
        void Reset()
        {
            m_led = false;
            m_revealed = false;
        }

    private:
        bool m_led = false;
        bool m_revealed = false;
    };

    // A light the scene times a press by (the boiler's pressure lamp, which the game keeps as a
    // global flag the press is judged by): true at the poll it comes on.
    class Light
    {
    public:
        bool Update(bool on)
        {
            const bool rose = on && !m_on;
            m_on = on;
            return rose;
        }
        bool On() const
        {
            return m_on;
        }
        void Reset()
        {
            m_on = false;
        }

    private:
        bool m_on = false;
    };
}
