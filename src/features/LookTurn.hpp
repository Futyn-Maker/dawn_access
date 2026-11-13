#pragma once
// The look a thing's press waits for, worked out without the game so that it can be tested: which
// turn is missing, the character's or the camera's, when the word for it is said, and how the
// walk turns the character toward the thing, as the game's own locomotion lets a player turn her
// on the spot.

#include "features/Vec.hpp"

#include <array>
#include <deque>
#include <optional>
#include <utility>
#include <vector>

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
        // The view is a framing camera the camera's own movement ends (ABatesInitialFramingCamera
        // with bCameraMovementEndsFramingCamera): the right stick turned brings the exploration
        // camera back, so the camera's look is the player's to give there too.
        bool framingEndsOnLook = false;
        // The use location takes its glint's verdict, and the game has not put the glint up for her:
        // it does so only while she stands in the glint's own volume (ABatesGlint is a character
        // trigger volume; OnCharacterVolumeStateChanged adds it as she walks in and takes it away as
        // she walks out), which need not be where the press's own volume is. No turn brings it: she
        // must step into it. Sam's drop-down in act8_6 stood at the glint volume's edge: in the first
        // playthrough she stopped inside it, in the second a few centimetres off, told to look at a glint
        // the game never drew (research/sessions/s65-play.log, 14:57:20 to 15:01:54).
        bool glintAway = false;
        // The exploration camera's line to the glint's billboard is blocked (the glint's own line of
        // sight check: CurrentIntensity -1): the glint is not drawn, though the camera may look at it.
        bool sightBlocked = false;
    };

    // Whether the game takes a press at a use location from the player's character, as its
    // ABatesUseLocation::CanCharacterInteract (0x44d9a30) asks it of the pressing character: everything
    // but a look in place (`atHand`: the linked activity on, no interaction in progress, she not
    // blending out of what she was bound to, inside the volume), and the look, where one is asked
    // (`lookAsked`), as IsInLookAtAngle judges it: passed where no glint is needed (`needGlint`), never
    // where one is and the use location has none (`glint`), passed on every camera layer but the
    // navigation (1) and framing (4) ones, and judged there as the glint or the angles judge it
    // (`lookOk`, once `measured`). The use location's own verdict (bOverlappingCharacterCanInteract)
    // is its tick's of the character it cached, the last to walk into it who may use it: where that is
    // a companion, it says nothing of hers. At the pool of Jessica's lair on 2026-11-09 Mike walked
    // in after Sam, and the double ping waited 2.8 s while the game drew its prompt for her and took
    // her press (research/sessions/s63-play.log, 01:12:34.829 to 37.943).
    bool PressTaken(bool atHand, bool lookAsked, bool needGlint, bool glint, int64_t layer, bool measured, bool lookOk);

    enum class Lead
    {
        None,
        Character, // she must turn: the left stick, the movement keys
        Camera,    // the camera must: the right stick, the mouse
    };

    // Whether a turn the player can make completes the look. On a fixed shot (any camera but the
    // exploration one) the camera is not the player's: a glint the picture leaves out stays out
    // whichever way she faces, and the press is not taken there. A framing camera that the camera's
    // movement ends is the player's all the same: in the old hotel's kitchen on 2026-11-08 the shot
    // after the door slam left the chains and knives out, the walk stepped her about and said it
    // could not get there, while a turn of the right stick would have ended the shot
    // (research/sessions/s58-whole.log, 11:43:17; LV_Cameras_oldhotel's BP_InitialFramingCamera_Mover).
    bool CanBeLed(const Parts& p);

    // What the walk's last stretch steps her toward once she is inside the place, as the press's parts
    // stand: into the glint's own volume where the game has not put the glint up for her (`glintAway`: no
    // shot leaves out a glint that is not there); toward the spot the thing is used from where a fixed
    // shot leaves the glint out, which no turn of hers brings in (OffShot); else her turn to the glint.
    // Outside the place, a step toward her spot. The two came from the one rule (CanBeLed), which counts a
    // glint not put up for her as no turn to lead: the walk took every glint not put up for her for one
    // off the shot, and the step into the glint's volume made for Sam's drop-down in act8_6 never ran. In
    // the second playthrough the walk to the drop-down ended "could not get there" five times, 1 m from it,
    // until she walked to the totem and back through the glint's volume, as in the first (research/
    // sessions/s73-play.log, 16:32:17 to 16:33:43; s65-play.log, 14:57:20).
    enum class StretchStep
    {
        IntoGlint,   // into the glint's own volume
        ToUseSpot,   // toward the spot it is used from: the shot leaves the glint out
        TurnToGlint, // her turn to the glint, inside
        ToSpot,      // toward her spot, outside the place
    };
    StretchStep StretchStepOf(const Parts& p, bool inside);
    // A fixed shot leaves the glint out, the glint up for her: no turn of hers brings it (inside the place,
    // its look measured).
    bool OffShot(const Parts& p, bool inside);

    // The turn the look beacon follows: the camera's while its part is not in place, else the
    // character's; on a fixed shot only the character's.
    Lead Leads(const Parts& p);

    // Whether the game shows no glint for a thing at hand though the camera looks at it: on the
    // exploration camera, its billboard inside the part of the screen the camera's look asks for
    // (`insideEllipse`, the glint's own GetCameraLookIntensity) and inside the picture
    // (`billboardInView`, where the game's own projection puts it), and its widget not on the screen
    // (GetIsOnScreen: the canvas entry's place within the screen, give or take the widget's own size,
    // FCanvasEntry::IsOnScreen). A widget the game tracks lies on the screen wherever its billboard
    // does, so only a glint whose widget the game has not put up (or has hidden) is off it there: no
    // turn of the camera brings it in. The look's ellipse reaches past the picture's edge: at the
    // guidance totem by the cable car on 2026-11-11 the billboard lay inside it, 24 degrees off across
    // and off the screen, and "the game shows no glint here" was said; the camera turned 9 degrees
    // further, the press was taken (research/sessions/s68-play.log, 01:17:25 to 01:17:48). Said after
    // kGlintMissingAfter of it, she standing still: a glint coming up takes a moment (the library's
    // guidance totem in s46 was off the screen inside the ellipse for 2.5 s before it showed), the
    // dollhouse's stayed so for 5 to 14 s at a time over 93 s.
    // The same where the widget is on the screen but the glint's own line of sight from the camera to
    // it is blocked (`sightBlocked`): the wine rack of the lodge's cellar on 2026-11-12 drew no glint
    // for two minutes, its line of sight blocked whichever way the camera turned, and the mod said
    // only "look at it" (research/sessions/s73-play.log, 17:18:35 to 17:22:26).
    bool GlintMissing(const Parts& p, bool insideEllipse, bool billboardInView);
    constexpr double kGlintMissingAfter = 5.0;

    // The turn the press still waits for, or None when the look is in place (or the game's
    // verdict is only a tick behind it), when nothing is at hand, or when no turn of the
    // player's completes it.
    Lead Missing(const Parts& p);

    // What the walk key does at a thing: one reached whose press the game takes, or a way on she
    // stands in, is said reached; one reached where only the camera's look is missing has that
    // look asked for, as a walk would end at once saying so; anything else is walked to, a way on
    // not reached yet or a press that wants a step (into the glint's volume). At Sam's drop-down on
    // 2026-11-09 the key said "Иду" every time while she stood at it, and nothing came of it
    // (research/sessions/s65-play.log).
    enum class WalkKey
    {
        Reached,
        Look,
        Walk,
    };
    WalkKey WalkKeyAt(bool reached, bool way, bool pressTaken, Lead missing);

    // Whether a thing to use counts as reached, "arrived" in the list: the game would take the press
    // now, or the thing starts by itself (`autoStart`), or everything but a look is in place and a
    // turn of the player's completes it (CanBeLed). Anything else is not reached however near, and
    // the list says how far, a metre at the least (Andrey's rule of 2026-11-07): after the kitchen's
    // blood in act4_1 Chris stood inside its volume on a fixed shot whose picture left the glint out,
    // and the list said "arrived" while no press was taken and no turn brought it. Becoming reached
    // waits for her to stand still (`still`: a walk past is no arrival); staying so (`before`) does not.
    bool Arrived(const Parts& p, bool autoStart, bool still, bool before);

    // Where the last stretch steps her while the glint the press needs is away (Parts::glintAway): the
    // middle of where the boxes of the use location's volume and of the glint's overlap, at her own
    // height; the glint box's middle where they do not overlap.
    Vec GlintStepPoint(const Vec& useLow, const Vec& useHigh, const Vec& glintLow, const Vec& glintHigh, double atZ);

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
    // in. A flick to step her somewhere did nothing toward it unless it moved her, however it turned
    // her: at the guidance totem by the cable car on 2026-11-11 six flicks toward a spot 66 cm off
    // turned Sam by 45 to 90 degrees four times and nowhere twice, never stepped her, and as each turn
    // counted as something done the held push never came; "could not get there" 2 m from the totem,
    // and walked to again, the walk's own push took her in (research/sessions/s68-play.log, 01:16:49 to
    // 01:17:22). Over every session's last stretches flicks toward a spot moved her 3 times of 100, and
    // the held push after two dead flicks was the last push of 19 that ended well.
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
    // `letGoAt`: the way left at which the walk lets go, a step's (kStepWay) or her slide's (Slide).
    // `steppingBack`: the walk is on its way back to walk in again (BackOffLine): no last stretch
    // begins on the way, which ends a step from the point stepped back to, kStretchReach from the spot
    // or nearer, unless the thing is offered where she stands.
    bool StretchKeeps(bool offered, bool begun, double fromSpot, double wayLeft = 1e9, double letGoAt = kStepWay, bool steppingBack = false);

    // The last stretch's own time: kArriveMost to get the press taken, then the walk gives up on it.
    // It gives up only once she is at rest and kSettleAfterPush has passed since its last push, or
    // kGiveUpWaitMost after its time at most: a flick that turns her on the spot sets her down a
    // moment after it, inside the place as often as not. At Hannah's diary the walk said it could not
    // get there 0.36 s after its eighth flick, and 0.55 s later the game offered the diary, only the
    // camera's look missing (research/sessions/s66-play.log, 16:20:07.707 to 16:20:08.619).
    constexpr double kArriveMost = 8.0;
    constexpr double kSettleAfterPush = 1.2;
    constexpr double kGiveUpWaitMost = 3.0;
    // `sinceBegun`: the last stretch's time so far; `sinceLastPush`: the time since its last push;
    // `still`: she reads still (Motion).
    bool GivesUp(double sinceBegun, double sinceLastPush, bool still);

    // A last stretch that gives up out of the place is tried once more from farther off, as a player
    // tries: the walk steps back kBackOff along the way it came (BackOffPoint), then walks in again
    // with its own speed, letting go where her slide ends. Close in, no push of the stick steps her a
    // little: the game's walk start (BP_LocoInputHandler_Into) wants 0.16 s of a full push and then
    // runs a metre, and it is refused, and only a single step offered, where something stands within
    // a metre ahead, as by the furniture a thing is used at; a shorter push only turns her on the spot,
    // and only where the way lies more than 23 degrees off her facing. Over every session's last
    // stretches one-frame flicks moved her 18 times of 426 and held pushes 6 times of 38. In session
    // 71 Sam stood 36 cm from the bathroom door's place, a held push ran her 145 cm past it, and the
    // walk said it could not get there; walked to again from 3 m, she was in it at once (23:35:32 to
    // 23:35:57).
    constexpr double kBackOff = 200.0;
    constexpr double kBackOffReached = 50.0; // that near the point stepped back to, the walk turns in again
    // The point kBack along `route` (the way the walk came, its end at the place) from its end; its
    // start where the way is shorter. Nothing for no way.
    std::optional<Vec> BackOffPoint(const std::vector<Vec>& route, double back = kBackOff);
    // The way back the walk follows: from where she gave up to the point stepped back to. A walk keeps
    // to a way round only while she stands within 150 cm of it (ways::StretchOn), and the point lies
    // kBackOff off: a way round of the point alone was dropped the moment it was set, and the walk
    // turned in again from where it had given up.
    std::vector<Vec> BackOffLine(const Vec& from, const Vec& back);

    // A framing camera (ABatesInitialFramingCamera: put up as a scene or an interaction ends, or as she
    // comes into an area) holds the view until the game ends it (its Tick, ProcessCharacterMovementInput,
    // ProcessCameraInput and ClearCameraLayer): never before MinTimeBeforeExit of it; then by a turn of
    // the camera where bCameraMovementEndsFramingCamera is set; by any push of the stick, unless
    // bMustWaitForDistanceToBeTravelled is, and then only once she stands farther than DistanceToTravel
    // from where she stood as it began (InitialPlayerLocation, measured in three dimensions). A glint off
    // its shot is no glint a turn brings in, and no step about the place either: in the old hotel's
    // kitchen on 2026-11-13 the chains and knives' glint lay off the shot Ashley's scene had left, four
    // walks to it ended "could not get there", and the glint came on screen once she had walked to the
    // photos 4 m away and come back (research/sessions/s73-play.log, 11:35:35 to 11:37:34). So a walk
    // that finds a glint off such a shot steps her out past its distance, as that player did, and walks
    // in again.
    struct Framing
    {
        bool waitsForDistance = false; // bMustWaitForDistanceToBeTravelled
        double distance = 0.0;         // DistanceToTravel
        Vec start;                     // InitialPlayerLocation
    };
    constexpr double kFramingMargin = 60.0;
    // How far she must still go from `here` for the framing to end by her going: 0 when it ends by a
    // push of the stick, or she has gone far enough.
    double FramingLeft(const Framing& f, const Vec& here);
    // A point `reach` cm from `center` on the line from it through `here`, turned by `turn` degrees
    // about `center`, at her height; straight on from her forward (`facingYaw`, degrees) where she stands
    // at the center.
    Vec AwayFrom(const Vec& center, const Vec& here, double reach, double facingYaw, double turn);
    // The point to step out to: on the line from where the framing began through her, kFramingMargin
    // past its distance (AwayFrom).
    Vec FramingExit(const Framing& f, const Vec& here, double facingYaw, double turn);
    // The point a last stretch out of the place steps back to where the walk's way gives none (the way
    // felt out for another spot than the place's, or none): kBackOff farther from the spot than she
    // stands, on the line from the spot through her (AwayFrom). The cart in the mine on 2026-11-13: the
    // stretch's pushes moved her nowhere 37 cm from her spot, no way to step back along was known, the
    // walk said it could not get there, and walked to again from 1.7 m off she was in the place at once
    // (research/sessions/s73-play.log, 13:22:23 to 13:22:41); Ashley's place to rejoin the group the
    // same at 15:54:42.
    Vec BackOffFrom(const Vec& spot, const Vec& here, double facingYaw, double turn);
    // The turns tried, in order, where the straight way out is walled: the nearest first, both ways.
    constexpr std::array<double, 10> kFramingTurns = {0.0, 35.0, -35.0, 70.0, -70.0, 105.0, -105.0, 140.0, -140.0, 180.0};

    // Held to a path, level with a thing the game has not offered: the walk lets the stick go and
    // gives up only once she has come to rest, at least kPathLevelSettle after it let go, or
    // kPathLevelWaitMost after at most. She slides on along the path, and the game's offer comes as
    // she does: the climb out of the stream was offered 0.47 s after the walk had said it could not
    // get there (research/sessions/s66-play.log, 16:42:17.284 to 16:42:17.753).
    constexpr double kPathLevelSettle = 0.5;
    constexpr double kPathLevelWaitMost = 1.5;
    bool PathLevelGivesUp(double sinceLevel, bool still);

    // How far she slides on once the walk lets go at speed: the game's locomotion ends a walk with a
    // stopping stride, about a second's walk on. At the damaged doors of act7_2 on 2026-11-08 the
    // walk let go at 138 cm/s as the place took her, 62 cm short of the spot found well inside it;
    // she came to rest 139 cm on, 77 cm past the spot and out of the place, and its flicks stepped her
    // through it and out again until the walk gave up beside it (research/sessions/s62-play.log,
    // 21:56:47 to 21:56:56); at Sam's sock and the bathroom door in s55, the family portrait and the
    // patio door before, the same. The walk lets go that far before the end of its way, so that the
    // stride ends there: the seconds of walk she slides on, measured from her own let-goes in the
    // session (the median of the last kSlideKept), times her speed, at most kStretchReach; nothing
    // until one is measured.
    // Her slide goes on the way she is heading: the walk lets go early only on the way's last
    // straight leg, heading along it within kSlideAlong (a corner within the slide, the way into a
    // doorway, is walked round first, and let go a step before the end as ever).
    constexpr double kSlideAlong = 20.0; // degrees
    double LetGoAt(double slide, bool lastLeg, std::optional<double> headingOff);
    constexpr double kSlideMinSpeed = 60.0; // cm/s: a slower let-go says nothing of the stride
    constexpr double kSlideWithin = 3.0;    // s: at rest later than this after the let-go, no slide is measured
    constexpr size_t kSlideKept = 5;
    class Slide
    {
    public:
        // The walk lets go at `here`, she moving at `speed` cm/s.
        void LetGo(double now, const Vec& here, double speed);
        // A push of the mod's own since (a flick): this slide is not measured.
        void Spoiled()
        {
            m_open = false;
        }
        // A frame: `still` once she has come to rest, at `here`. True on the frame a slide is measured.
        bool Update(double now, const Vec& here, bool still);
        // How far she slides on from `speed`.
        double Distance(double speed) const;
        // The seconds of walk she slid on, as last measured; 0 before any.
        double LastSeconds() const
        {
            return m_seconds.empty() ? 0.0 : m_seconds.back();
        }

    private:
        bool m_open = false;
        Vec m_from;
        double m_speed = 0.0;
        double m_at = -1.0;
        std::deque<double> m_seconds;
    };

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

    // A beam the game walks her along, a special locomotion of its own (chapter 5's BP_BeamWalk,
    // ABP_BeamTemplate): it moves her only while the stick, past half (InputSizeThreshold 0.5), points
    // in the world within kBeamCone of where she faces (InputForwardThresholdDegrees 46), and a push
    // beyond 134 degrees turns her round; under the fixed camera above the beam that way is seen, not
    // known. The lead sounds where she faces in the stick's frame (`facing`, degrees right of the
    // stick's forward; behind, muffled), with the double ping while the stick (its x right, y up, -1
    // to 1) points within the cone.
    constexpr double kBeamCone = 46.0;
    constexpr double kBeamPush = 0.5;
    struct BeamLead
    {
        double pan = 0.0;
        bool locked = false;
        bool muffled = false;
    };
    BeamLead LeadAlongBeam(double facing, double stickX, double stickY);

    // A staircase the game walks her on, a special locomotion of its own (ABatesStairActivityActor):
    // it takes the movement input only along the stairs, the input dotted with the stairs' rise saying
    // up above nought and down else once she moves, and from a standstill only past the stairs'
    // InputThresholdAmountNormalised (UpdateInputForCharacters, from 0x44f8e68). A walk pushing toward
    // a corner of its way beside the stairs pushes near square to them: on the lodge's stairs down to
    // the lower hall on 2026-11-12 the corner lay 67 cm aside, the push swung about the square as she
    // went, and Ashley went down and up and down for six seconds until the walk said it could not get
    // there, 13 m short of the smashed vase (research/sessions/s72-play.log, 11:44:03 to 11:44:16). On
    // the stairs the walk pushes along them, the way its way first moves her along them: the first
    // corner of the way ahead (`ahead`) more than kStairDecides up or down the stairs from her says
    // which (a corner beside her says nothing, and the way cannot leave the stairs but by an end);
    // with none, the target (`aim`) above or below her by more than kStairDecidesRise, else where it
    // lies along them; with nothing to go by, the way last pushed (`last`, 0 for none: up). `bottom`
    // and `top`: the ends of the track she climbs. Returns the flat direction to push, of unit
    // length, and the way: 1 up, -1 down.
    constexpr double kStairDecides = 30.0;     // cm along the stairs
    constexpr double kStairDecidesRise = 50.0; // cm up or down
    struct StairWay
    {
        Vec push;
        int way = 0;
    };
    StairWay StairPush(const Vec& here, const Vec& bottom, const Vec& top, const std::vector<Vec>& ahead, const Vec& aim, int last);

    // The walk ends where the press is taken only once she has come to rest there, still for
    // kRestFor on end: let go, she takes a stopping step, and at the lodge's patio door on 2026-11-05
    // the walk ended 0.4 s after the press would be taken, the moment her speed first read still,
    // and the step carried her out of the door's place. Moving on for kRestMax she is let go as she
    // Whether she moves, by where she stands rather than by the game's velocity: the velocity follows
    // the animation's own motion, which goes on while her capsule pushes against something. At the
    // cable car's upper station on 2026-11-07 Matt pushed against something for five seconds at 111 to
    // 140 cm/s without moving a centimetre, and the walk, taking the speed for progress, never leaned
    // another way; the same push from the same spot half a minute later walked him through
    // (research/sessions/s55-full.log). Moving: kMovingCm within kMovingWindow; still: less than
    // kStillCm within kStillWindow (10 cm/s, the speed a turn on the spot stays under).
    constexpr double kMovingWindow = 0.5;
    constexpr double kMovingCm = 8.0;
    constexpr double kStillWindow = 0.25;
    constexpr double kStillCm = 2.5;
    class Motion
    {
    public:
        void Reset()
        {
            m_samples.clear();
        }
        // Where she stands on a frame.
        void Update(double now, const Vec& here);
        // How far she has gone, over the ground, within the last `seconds`; nothing known is nothing gone.
        double MovedWithin(double now, double seconds) const;
        bool Moving(double now) const
        {
            return MovedWithin(now, kMovingWindow) >= kMovingCm;
        }
        // The way she goes, the world's yaw in degrees, over the last kMovingWindow: none while she
        // is not moving.
        std::optional<double> Heading(double now) const;
        // Still: watched for the whole window, and gone less than kStillCm in it.
        bool Still(double now) const;
        // Her speed over the ground, from the samples within the window a still is judged by, in cm/s.
        double Speed(double now) const;

    private:
        std::deque<std::pair<double, Vec>> m_samples;
    };

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
        // own yaw, `speed` how fast she moves, `here` where she stands; `step`, the push is to step her
        // toward a spot, which only moving her brings nearer, not to turn her.
        std::optional<Push> Update(double now, double bearing, double facing, double speed, const Vec& here, bool step = false);
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
        Push m_push;             // the push on now
        double m_pushedAt = 0.0; // when it began
        Vec m_flickedFrom;       // where she stood at the last push
        double m_flickedFacing = 0.0;
        int m_dead = 0;
    };

    // The aim sound of a look's lead, the camera's turn or hers by the stick: on the side of the
    // thing, higher above and lower below, quicker as the turn left to make shrinks, the double ping
    // while the game would take the press. The game takes the look wherever its glint lies within
    // the screen's ellipse, and the sessions' logs have it taken as far as 80 degrees across and 24 up
    // or down of where the glint stands; with the shooting aim's scales (12 degrees across, 6 up and
    // down, 25 for the pace) three in four of the leads' blips sounded alike, telling the side and
    // never nearer or further: in s47 the look went 46 and 91 degrees off and came back with no change
    // heard (research/sessions/s46-full.log to s56-full.log, the audit of 2026-11-08). The pan follows
    // the turn over kLookPanSpan, the pitch over kLookRiseSpan, the pace over kLookPaceSpan.
    constexpr double kLookPanSpan = 45.0;
    constexpr double kLookRiseSpan = 30.0;
    constexpr double kLookPaceSpan = 90.0;
    struct LookBlip
    {
        double pan = 0.0;   // -1 left .. 1 right
        double level = 0.5; // 0 low .. 1 high
        double interval = 0.12;
        bool locked = false;
        bool muffled = false;
    };
    LookBlip LookSound(double across, double rise, bool lookedAt);

    // The thing she has just used is led to no more until the player chooses it again or she leaves
    // its place: handed back after a door tried or a photo seen, she stands in the thing's place
    // facing away from it, and "turn to it" was said for the thing just used 23 times in sessions 46
    // to 56, 140 to 176 degrees behind her, and never followed (the audit of 2026-11-08; s56 23:51:48
    // after the lodge's patio door was tried).
    class JustUsed
    {
    public:
        // Bound to the interaction of `thing`.
        void Used(const void* thing)
        {
            m_thing = thing;
        }
        // The player chose a target.
        void Chosen()
        {
            m_thing = nullptr;
        }
        // The look's subject at this look (none when she is in no thing's place): whether its lead
        // waits. A subject other than the thing used, or none, lets the thing go.
        bool Waits(const void* subject)
        {
            if (m_thing && subject != m_thing) m_thing = nullptr;
            return subject != nullptr && subject == m_thing;
        }

    private:
        const void* m_thing = nullptr;
    };
}
