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
    std::wstring Name(State state);
    // The player is in the game proper, playing or watching it.
    bool InPlay();
    bool Paused();

    void Install();
    void Reset(); // after a map load
}
