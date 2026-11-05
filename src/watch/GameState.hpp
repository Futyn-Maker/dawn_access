#pragma once
// The state the game says it is in (booting, in a menu, loading, playing, paused), asked of
// its own game state actor a few times a second: what the features in play key on, and what
// the log shows beside every change.

#include <string>

namespace da::gamestate
{
    enum class State
    {
        None = 0,
        Booting = 1,
        Menu = 2,
        Paused = 3,
        Loading = 4,
        Game = 5,
        GameStarting = 6,
        GameEnding = 7,
        LoadingNewLevel = 8,
        FreezeScreen = 9,
        Unknown = 99,
    };

    State Current();
    // Asks the game now, unless it was asked at this frame. The game pauses as the pause menu's
    // fade to black begins (ABatesGameState::ProcessNewGameState pauses and sets the state in one
    // call, and the fade answers the pause), and the hold, which reads the fade at every frame,
    // saw it up to five frames before the pause and took it for a scene's: a taking back as the
    // collectibles opened and a handing over as they closed (2026-11-01, the fade 27 and 48 ms
    // before the pause was read). Game thread only.
    State Refresh();
    std::wstring Name(State state);
    // The player is in the game proper, playing or watching it.
    bool InPlay();
    bool Paused();

    void Install();
    void Reset(); // after a map load
}
