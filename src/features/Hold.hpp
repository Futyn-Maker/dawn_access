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
        bool covered = false;    // the loading screen, a film or the fade to black covers the picture
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
        Why m_why = Why::None;
    };
}
