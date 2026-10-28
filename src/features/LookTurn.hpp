#pragma once
// The look a thing's press waits for, worked out without the game so that it can be tested: which
// turn is missing, the character's or the camera's, when the word for it is said, and how the
// walk turns the character toward the thing, as the game's own locomotion lets a player turn her
// on the spot.

#include <optional>

namespace da::look
{
    // ---- what is missing ---------------------------------------------------------------------

    // The press test's parts the guidance weighs (Exploration's PressTest).
    struct Parts
    {
        bool measured = false;        // the look is measured: asked for, a glint, a layer it is judged on
        bool atHand = false;          // everything but a look is in place
        bool verdict = false;         // the game would take the press
        bool lookOk = false;          // the look the press asks for is in place
        bool byGlint = false;         // the use location takes its glint's verdict
        bool explorationView = false; // the view is the exploration camera's
        bool onScreen = false;        // the glint's widget is on the screen
        bool cameraOk = false;        // the camera's part of the look is in place
        bool characterOk = false;     // the character's part of the look is in place
        bool needCamera = false;      // the use location asks for the camera's look
    };

    enum class Lead
    {
        None,
        Character, // she must turn: the left stick, the movement keys
        Camera,    // the camera must: the right stick, the mouse
    };

    // Whether a turn the player can make completes the look. On a fixed shot (any camera but the
    // exploration one) the camera is not the player's: a glint the picture leaves out stays out
    // whichever way she faces, and the press is not taken there.
    bool CanBeLed(const Parts& p);

    // The turn the look beacon follows: the camera's while its part is not in place, else the
    // character's; on a fixed shot only the character's.
    Lead Leads(const Parts& p);

    // The turn the press still waits for, or None when the look is in place (or the game's
    // verdict is only a tick behind it), when nothing is at hand, or when no turn of the
    // player's completes it.
    Lead Missing(const Parts& p);

    // ---- the words ---------------------------------------------------------------------------

    // A change of what is missing is said once it has lasted this long, and no sooner than this
    // after the last word: a turn passing the edge of the look says nothing.
    constexpr double kSwitchSteady = 0.5;
    constexpr double kWordGap = 1.5;

    // When "turn toward it" and "look at it" are said for one thing at hand: the first as soon as
    // the character stands at it, then again whenever what is missing changes to the other turn
    // and stays so. A fixed shot asks for her turn alone, and the first push of the stick gives
    // the exploration camera back, which then asks for its own (the doors of the lodge on
    // 2026-10-28: told to turn toward the door, the player turned her, and the camera's turn
    // the beacon led next went unsaid).
    class Words
    {
    public:
        // Another thing at hand: nothing is said for it yet.
        void Reset();
        // The word due at this look for the turn that is `missing`, the character standing still
        // or not (no word while she moves); None when there is nothing to say.
        Lead Update(double now, Lead missing, bool still);

    private:
        Lead m_said = Lead::None;
        Lead m_missing = Lead::None;
        double m_since = 0.0;
        double m_saidAt = -1000.0;
    };

    // ---- the walk's turn on the spot ---------------------------------------------------------

    // The game's locomotion (UBatesStateDrivenCharacterMovementComponent and its input handlers,
    // Gameplay/locomotion/BP_LocoInputHandler_*) judges each frame's stick with the time the
    // stick had been off its middle before that frame (TimeHadInput: TickComponent grows
    // TimeHadMovementInput while the input's square is at least 1e-4 and zeroes it on the first
    // frame it is not, after the frame's input took it):
    // - Into starts a walk once that time is 0.16 s with the input at 0.8 or more, or over 1.5 s at
    //   0.4, and over 0.1 s at 0.4 just after an interaction (the OutOf state,
    //   OutOfInterruptHoldDuration);
    // - IdlePivot turns a standing character on the spot on the first frame the stick is back
    //   within 0.05 of its middle after more than none and less than a second of input, toward
    //   the largest input of the last half second (the input buffer, kInputWindow; an older entry
    //   wins only when strictly larger), or takes a single step that way when it lies within 23
    //   degrees of where she faces (ForwardSingleStepAngleThreshold).
    // The walk's old nudge, a held push of 45% (0.41 of the stick, 0.26 after IA_Movement's dead
    // zone of 0.2), was neither, and she never turned. So the walk flicks: one frame's full push,
    // let go at the next, which carries no time of its own (so no walk starts) and leaves the
    // release frame the time of that one frame (so she turns). A flick waits until the walk's push
    // and the last flick have left the input buffer, and until she stands and has stopped
    // turning. A flick toward her spot once she faces it is the single step that brings her in.
    constexpr double kInputWindow = 0.5;               // the input buffer's age (+0x15f8, set to 0.5 in the constructor)
    constexpr double kAfterPush = kInputWindow + 0.25; // from the last push, the walk's or a flick's, to a flick
    constexpr double kSteady = 0.25;                   // her facing unchanged this long: a turn is over
    constexpr double kSteadyDegrees = 1.5;
    constexpr double kStillSpeed = 10.0; // cm/s
    constexpr double kSettleMax = 2.0;   // a turn not over by then is not waited for longer

    struct Push
    {
        double forward = 0.0;
        double right = 0.0;
    };

    class Flicker
    {
    public:
        // The last stretch begins, the walk's push let go `now`.
        void Reset(double now);
        // A frame on which she must turn toward `bearing` (degrees right of the stick's forward,
        // the control rotation's) or step that way: the push to give on this frame, if any; a
        // flick's push is let go at the next call. `facing` is her own yaw, `speed` how fast she
        // moves.
        std::optional<Push> Update(double now, double bearing, double facing, double speed);
        // A frame on which nothing is to be turned: a push still on is let go.
        void Hold(double now, double facing);
        // Lets a flick's push go on a frame the last stretch is not reached on.
        void Release(double now);
        // A flick's push is on: given on an earlier frame, it must be let go.
        bool Pushing() const
        {
            return m_pushing;
        }
        // The flicks given since Reset.
        int Count() const
        {
            return m_count;
        }
        // A flick began at the last Update.
        bool Began() const
        {
            return m_began;
        }

    private:
        void Watch(double now, double facing);
        double m_releasedAt = 0.0; // when the last push, the walk's or a flick's, was let go
        double m_steadyYaw = 0.0;
        double m_steadySince = -1.0;
        int m_count = 0;
        bool m_pushing = false;
        bool m_began = false;
    };
}
