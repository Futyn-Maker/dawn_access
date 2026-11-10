#pragma once
// The mod's keyboard keys are taken by a low-level keyboard hook on a thread of the mod's
// own, ahead of the screen reader and the game. A screen reader, or one of its add-ons, can
// bind a key for itself and take it before the game ever sees it (an NVDA add-on takes F3
// for "find next"); a hook installed after the screen reader's is called before it, so a key
// bound to a mod command is answered every time, and neither the screen reader nor the game
// sees it.

#include <vector>

namespace da::keyhook
{
    struct Binding
    {
        int vk = 0;
        bool ctrl = false;
        bool alt = false;
        bool shift = false;
        int id = 0;
        // A key that is the mod's only in some contexts (the exploration letters) and the
        // game's, or the text field's, elsewhere.
        bool contextual = false;
        // The mod's own switch, taken while the mod is off as well.
        bool switchKey = false;
    };

    // A key the mod watches but never takes: the handler runs on the hook's own thread the
    // moment the key goes down, before the game is given it, and the key goes on to the game
    // all the same. Set before Install; it is read only once the hook runs.
    using Watcher = void (*)(int vk);
    void Watch(std::vector<int> vks, Watcher handler);

    // Starts the hook thread with the keys to take. False when the hook could not be set.
    bool Install(std::vector<Binding> bindings);
    void Uninstall();
    bool Active();
    // Whether the contextual bindings are taken now; set from the game thread as the
    // context comes and goes.
    void SetContext(bool active);
    // Whether the mod is on; off, the hook takes nothing but the switch. Set from the game
    // thread as the mod is switched.
    void SetOn(bool on);
    // Whether the game waits for a key to bind to a row of its controls: meanwhile the hook takes
    // no key at all (hotkeys::TakeOf). Set from the game thread.
    void SetGameWaitsForKey(bool waits);

    // The ids of the bindings pressed since the last call, in order.
    std::vector<int> TakePresses();

    // Whether the key the player last pressed for the game (one the mod let go on to the screen reader
    // and the game) is still held. Windows repeats the key last pressed for as long as it is held, and
    // a screen reader given each repeat may stop what it is saying at every one: NVDA's "speech
    // interrupt for typed characters", on by default, stops speech at any key it is given. Once that
    // key is let go no key repeats, though another may still be down. Asked from any thread.
    bool KeyRepeating();
}
