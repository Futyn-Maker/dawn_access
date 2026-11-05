#pragma once
// Whether the game hands the player the character, worked out without the game so that the
// timelines of play can be replayed in the tests. The game's own states say "in the player's
// hands" for moments where no player has anything: between a level's loading and its first
// scene, as a staircase or an interaction lets go of the character, while its own walk has her.
// The hold rides those states with the rules below, and each rule is a timeline from a log.

namespace da::hold
{
    // The game's state as the hold weighs it.
    enum class State
    {
        Other, // booting, the menus, loading, starting, ending, a frozen screen
        Game,
        Paused,
    };

    // What the hold reads at a frame.
    struct Frame
    {
        bool character = false; // the controller has a character of the game's own
        State state = State::Other;
        long long binding = -1;  // its binding state: 0 unbound, 1 a sequence, 2 an interaction, 3 a special locomotion
        bool cameraFree = false; // the camera is one the game explores through (IsExplorationCameraActiveLayer)
        bool inputTaken = false; // the controller takes movement input
        bool gameWalks = false;  // the game's own walk has her (not a staircase's first step, which is hers)
        bool falling = false;    // she is in the air, the game letting her drop (MovementMode Falling)
        bool sceneWaits = false; // the scene that made her the player waits a moment before it goes on (scripts::Switch)
        bool covered = false;    // the loading screen, a film or the fade to black covers the picture
        bool offered = false;    // the game offers a thing to use where she stands: a press is hers to make
    };

    // Why the character was taken back, the first of the reasons that holds.
    enum class Why
    {
        None, // handed over
        NoCharacter,
        Covered,
        NotInPlay,
        Bound,
        Camera,
        GameWalks,
        InputIgnored,
        Falling,
        SceneWaits,
    };

    // The hard reasons take the character back at once: no character, the game out of play, the
    // character bound to a sequence or an interaction, a camera the game does not explore through,
    // the picture covered. The controller ignoring movement input with none of those is soft: the
    // game does it for a third of a second as the character steps onto a stair (binding state 3)
    // and for half a second as an interaction lets go, and a hold that followed it sounded a false
    // taking back and handing over at every stair; so it takes the character back only once it
    // has lasted kSoftTakeBack (a scripted stop on the woods' path lasted two and a half). The
    // game walking the character itself (its auto-walk) is soft as well: the woods' jog to the
    // deer runs for seventeen seconds and leaves nothing to walk to meanwhile. The character is
    // handed over only once everything has stood for kHandOverSettle: the freedom between a
    // level's loading and its first scene (a tenth of a second at the prologue's start on
    // 2026-10-24, the end of the woods, the end of the first session with the doctor) is no hold,
    // nor the new story's opening film still on the screen (three quarters of a second of play
    // under it on 2026-10-28, which sounded a handing over and a taking back). A pause freezes
    // the hold as it stood, so the pause menu plays no tone, and a hold that ends while the game is
    // paused (the player leaves the scene from the pause menu) ends without one.
    constexpr double kHandOverSettle = 0.5;
    constexpr double kSoftTakeBack = 1.0;

    // A level's first moments are no hold, however long they last: the game turns to play a while
    // before its first scene binds the character, and the freedom between can outlast any settle
    // (1.26 s at chapter one's start on 2026-11-01, after its film was skipped, where the hold
    // sounded a handing over and, at the opening scene, a taking back). After the game turns to
    // play from a loading or a frozen screen, the character is handed over only once everything has
    // stood for kLevelSettle; once the level's first scene has bound her, a later hand-over settles
    // as usual.
    constexpr double kLevelSettle = 3.0;

    // The way back from a pause is the pause's own. The collectibles show their pictures through a
    // camera of the menu's, which turns back to the scene for a moment after the game runs again
    // (2026-11-01, act2_1: 0.38 s and 0.54 s), and a hold that stood before the pause ended on it
    // and was handed over anew, with a tone each time the player looked at the collectibles.
    // After a pause, a hold that stood before it stands while only the camera or a soft reason
    // keeps the character from the player, for at most kPauseReturn; a scene that binds her, a
    // cover, no character, or a camera that has not come back by then ends it without a tone, as
    // a hold that ends during the pause does.
    constexpr double kPauseReturn = 1.0;

    // A character in the air is no one's to walk: the game lets go of her while it drops her
    // through empty space between two shots, at the cable car's end of chapter one and as Matt
    // was put in the player's hands at act2_1 (2026-11-01, a metre a tenth of a second, with no
    // ground under her), and the hold sounded a handing over and a taking back each time. Falling
    // is a soft reason like ignored input: no hand-over while it lasts, and a hold ends once it has
    // lasted kSoftTakeBack (a step off a kerb lasts a fraction of that).
    //
    // A character the scene has just made the player's is not handed over while the scene's own
    // script waits before going on with her (scripts::Switch): act2_1's snowball fight makes
    // Jessica the player, waits its SceneTransitionFadeWaitTime of 2 s on the exploration camera
    // and then starts her scene, and the hold sounded a handing over and a taking back in between
    // (2026-11-02). The wait is a soft reason, as falling is; it lasts only as long as the script's.
    //
    // Ignored movement is no reason at all where the game offers a thing she stands in: at the cabin of
    // chapter 3 the game stops the character at Jessica's phone and at the shutters (a volume that
    // ignores movement on entering, BatesSetMoveInputTriggerAction), and only a press there goes on; a
    // hold taken back after kSoftTakeBack would leave that press unread.

    class Hold
    {
    public:
        // Takes the frame at `now`; true when the hold changed at it.
        bool Update(double now, const Frame& frame);
        bool Control() const
        {
            return m_control;
        }
        // Why the character was taken back at the last change.
        Why Reason() const
        {
            return m_why;
        }
        // The hold ended out of a pause, and its taking back is not sounded: true once, then
        // forgotten.
        bool TakeSilentEnd();
        // Whatever the last end was, the next one is sounded.
        void ForgetSilentEnd()
        {
            m_takenSilently = false;
        }

    private:
        bool m_control = false;
        double m_freeSince = -1.0; // since when everything has let the player walk the character
        double m_softSince = -1.0; // since when a soft reason alone has kept the character from the player
        bool m_wasPaused = false;
        bool m_takenSilently = false;
        double m_backSince = -1.0; // since when play has run again after a pause the hold stood through
        bool m_inPlay = false;     // the last frame was in play
        bool m_levelStart = false; // play began a moment ago and the level's first scene has not yet bound her
        Why m_why = Why::None;
    };
}
