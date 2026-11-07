#pragma once
// A camera-nudge interaction (a telescope, the selfie) worked out without the game, so that it can
// be tested: where the framing the scene waits for lies from the view, in the game's own measure;
// how the aim sound and the words lead to it; and the moment a light the scene times a press by
// comes on.

#include "features/ShootingRules.hpp"
#include "features/Vec.hpp"

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

    // ---- a totem turned in the hand -----------------------------------------------------------

    // A totem held for its inspection is turned by the stick: the game adds the stick to a pose of
    // two numbers held within the totem's limits (ABatesInteractionTotem::CalculateExamineInput:
    // CumulativeInspectionInput, the stick pushed right raising the first and pushed up the second).
    // It looks along a line of its own, from the camera's eye to a query point that the pose moves
    // across the view (UpdateTotemExamination, 0x465a4f0: CachedTotemInspectionQueryLocation, the
    // focus offset by the pose scaled to PhantomOffset*Limits), and weighs how near the point that
    // line meets the totem at lies to the premonition's (its closeness, from their places on the
    // totem's texture; 0 while the line misses the totem). The aim sound goes by where the
    // premonition lies from that point, or from the line itself while it misses the totem: from the
    // view's centre line instead, the pose stood at its limit at the fortune totem (2026-11-07,
    // chapter 4) and at the danger totem (chapter 5), the point "right" all the while and the stick
    // pushed right for half a minute turning nothing.
    //
    // Which way a push moves that point over the premonition is the stick's way in the game's own
    // frame, but the totem leans in the hands as well, opposite when the player inverts its controls;
    // so the lead weighs, axis by axis, how the premonition's place answers the pose as the player
    // turns it, and leads the other way once a push is seen to take it further off (kTotemSlope).
    // Where a point lies off a line, from `from` through `through`: its part across the line, the
    // line taken along `fallback` where the two ends are one. The premonition placed so against the
    // game's line (from the eye through its query point) says which way the line must move to it.
    features::world::Vec AcrossLine(const features::world::Vec& point, const features::world::Vec& from, const features::world::Vec& through,
                                    const features::world::Vec& fallback);

    struct TotemSample
    {
        bool turnKnown = false;          // the pose is read
        double turnX = 0.0, turnY = 0.0; // the pose the stick has sent the totem to
        bool onTotem = false;            // the game's line meets the totem (closeness not 0)
        double closeness = 0.0;          // the game's own
        double lateral = 0.0;            // cm right of the point the line meets the totem at (or of the line)
        double vertical = 0.0;           // cm up from it
        double depth = 0.0;              // cm further from the view
    };

    struct TotemAim
    {
        double pan = 0.0;    // where the stick goes across: -1 left .. +1 right
        double level = 0.5;  // and up and down: 0 down .. 1 up
        bool behind = false; // the point lies round the back: the blip muffled
    };

    // The pose's change a response is weighed over; how much of what was weighed before each new
    // step keeps; the pose's movement (summed squares of the steps) it takes to judge a way; and the
    // answer, in cm the premonition moves for the pose's whole unit, that settles one: further off
    // the way the push went, the lead turns round, nearer, it goes the push's way.
    constexpr double kTotemStep = 0.05;
    constexpr double kTotemForget = 0.95;
    constexpr double kTotemEvidence = 0.04;
    constexpr double kTotemSlope = 4.0;

    class TotemLead
    {
    public:
        TotemAim Update(const TotemSample& s);
        void Reset()
        {
            *this = TotemLead{};
        }
        // +1 where a push toward the premonition's side brings it nearer, -1 where it was seen to
        // take it further off; across, then up and down.
        int WayX() const
        {
            return m_wayX;
        }
        int WayY() const
        {
            return m_wayY;
        }

    private:
        TotemAim Aim(const TotemSample& s) const;
        bool m_have = false; // a sample to weigh the next against
        bool m_lastOn = false;
        double m_lastX = 0.0, m_lastY = 0.0, m_lastLateral = 0.0, m_lastVertical = 0.0;
        double m_xx = 0.0, m_xLateral = 0.0, m_yy = 0.0, m_yVertical = 0.0;
        int m_wayX = 1, m_wayY = 1;
    };
}
