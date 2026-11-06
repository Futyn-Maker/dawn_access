#pragma once
// The look a thing's press waits for, worked out without the game so that it can be tested: which
// turn is missing, the character's or the camera's, when the word for it is said, and how the
// walk turns the character toward the thing, as the game's own locomotion lets a player turn her
// on the spot.

#include "features/Vec.hpp"

#include <optional>

namespace da::look
{
    using Vec = features::world::Vec;

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
        double hold = 0.0; // seconds the push is held: 0 for a flick, let go at the next frame
    };

    // A flick that left her where she stood, facing as she faced (moved less than kDeadMove, turned
    // less than kDeadTurn), did nothing; after kDeadFlicks of them the push is held kPulse, past the
    // 0.16 s at full stick Into needs, so that a walk starts and carries her a step. In the library
    // on 2026-11-06 ten flicks toward a spot of the totem's place 30 cm off turned Ashley nowhere and
    // stepped her nowhere, and the walk said "could not get there"; walked to again, one step took her
    // in.
    constexpr double kDeadMove = 3.0;
    constexpr double kDeadTurn = 3.0;
    constexpr int kDeadFlicks = 2;
    constexpr double kPulse = 0.25;

    // Whether the walk's last stretch keeps her: while the game offers the thing, and once it has
    // begun, while she stands within kStretchReach of the spot the game made for her, though out of
    // the place. Let go as the place takes her, she slides on, and out of a small place: at the
    // lodge's family portrait on 2026-11-04 she was in it for half a second, slid out 60 cm on, and
    // the walk took her round it at full stick for fourteen seconds and gave up 70 cm from it;
    // at the missing axe she walked past it twice and "could not get there" beside it. Out of the
    // place, the last stretch steps her back in with flicks; farther off, the walk goes on.
    constexpr double kStretchReach = 150.0;
    // A walk whose way left is shorter than a step begins its last stretch there and then, with
    // single steps, rather than at full stick: at the lodge's patio door on 2026-11-05 a walk began
    // 11 cm from the end of its way, pushed full stick toward it and ran Sam 125 cm past the door.
    constexpr double kStepWay = 60.0;
    bool StretchKeeps(bool offered, bool begun, double fromSpot, double wayLeft = 1e9);

    // Where the last stretch steps her while she is out of the place: a cell of the place the ground
    // was proven to carry her to (the reach map's, `mapped`, else the end of the way felt out to it,
    // `felt`), and only failing both the spot the game starts the interaction from, which may stand
    // past furniture: the lodge's music box has its start arrow behind the dresser's slanted edge, and
    // nine flicks toward it moved Sam nowhere, "could not get there" a metre from it (2026-11-05).
    Vec StepSpot(const std::optional<Vec>& mapped, const std::optional<Vec>& felt, const Vec& spot);

    // The map's spot of a place is the cell where it first touched the place, on its road from
    // wherever it began, and that may lie on the far side of the place from her: in the library on
    // 2026-11-06 the map begun upstairs touched the guidance totem's place where the stairs' road
    // came to it, and Ashley stood 30 cm off on its near side, stepped toward that cell to no effect.
    // A map begun more than kStretchReach from where the last stretch finds her is begun again from
    // there; its first touch is the place's nearest ground to her (walked to again, the new map's spot
    // was 18 cm off and one step took her in).
    bool FreshSpotMap(double mapOriginDistance);

    // The walk ends where the press is taken only once she has come to rest there, still for
    // kRestFor on end: let go, she takes a stopping step, and at the lodge's patio door on 2026-11-05
    // the walk ended 0.4 s after the press would be taken, the moment her speed first read still,
    // and the step carried her out of the door's place. Moving on for kRestMax she is let go as she
    // is. A look at which the press would not be taken starts the rest over (Reset).
    constexpr double kRestFor = 0.5;
    constexpr double kRestMax = 2.0;
    class Rest
    {
    public:
        void Reset()
        {
            m_readySince = -1.0;
            m_stillSince = -1.0;
        }
        // A look at which the press would be taken; `still`, her speed reads still. True: the walk
        // ends.
        bool Update(double now, bool still);

    private:
        double m_readySince = -1.0;
        double m_stillSince = -1.0;
    };

    class Flicker
    {
    public:
        // The last stretch begins, the walk's push let go `now`.
        void Reset(double now);
        // A frame on which she must turn toward `bearing` (degrees right of the stick's forward,
        // the control rotation's) or step that way: the push to give on this frame, if any; a
        // flick's push is let go at the next call, a held one once its time is up. `facing` is her
        // own yaw, `speed` how fast she moves, `here` where she stands.
        std::optional<Push> Update(double now, double bearing, double facing, double speed, const Vec& here);
        // The flicks in a row that did nothing.
        int Dead() const
        {
            return m_dead;
        }
        // A frame on which nothing is to be turned: a push still on is let go.
        void Hold(double now, double facing);
        // Lets a flick's push go on a frame the last stretch is not reached on.
        void Release(double now);
        // A flick's push is on: given on an earlier frame, it must be let go.
        bool Pushing() const
        {
            return m_pushing;
        }
        // A held push is on and its time is not up: not to be let go from outside yet.
        bool Holding(double now) const
        {
            return m_pushing && now - m_pushedAt < m_push.hold;
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
        Push m_push;               // the push on now
        double m_pushedAt = 0.0;   // when it began
        Vec m_flickedFrom;         // where she stood at the last push
        double m_flickedFacing = 0.0;
        int m_dead = 0;
    };
}
