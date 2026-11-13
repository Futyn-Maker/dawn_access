#pragma once
// Mod hotkeys: the keys of hotkeys/HotkeyTable on the keyboard (F5 repeat, F6 read screen,
// F7 stop, F8 help, F9 subtitle reading on/off, F4 last subtitle line, F3 speech through
// the screen reader or SAPI, F2 verbosity, F1 the mod off and on, N/P next and previous
// exploration target, H where it is, G walk, T beacon, dev keys) and as gamepad chords
// (hold Back and press a face button, a bumper, a trigger, Start or a stick).
// Keyboard keys are taken by the mod's own low-level keyboard hook ahead of the screen
// reader and the game (KeyHook.hpp), and polled with GetAsyncKeyState only when the hook
// could not be set; the gamepad is read through the same XInput the game reads, so the
// chords answer in the menus as well as in play. While the mod is switched off (core/Gate)
// only its switch answers.

#include "hotkeys/HotkeyTable.hpp"

#include <string>

namespace da::hotkeys
{
    // Where a command came from, which is the device its answer is worded for.
    enum class Source
    {
        Keyboard,
        Gamepad,
        Other,
    };

    // Reads the bindings of the table from the ini and registers the pollers.
    void Install();
    // Takes the keyboard hook down, for shutdown.
    void Uninstall();

    // Runs a command (game thread).
    void Run(Command command, Source source = Source::Other);

    // The situations in which the mod's own letters are the mod's, such as the player walking
    // the character: a feature registers the check that says when its situation is on, and the
    // letters answer while any check says yes. Elsewhere they are the game's, or a text
    // field's.
    using Context = bool (*)();
    void AddContext(Context inContext);
    // The situations in which the demonstration's key is the mod's (Scope::Demo): a settings row
    // with an example has the focus.
    void AddDemoContext(Context inContext);

    // A command a feature answers. The handler says whether it took the command; the handlers
    // of a command are asked in the order they were registered until one does. A command with
    // no handler does nothing.
    using Handler = bool (*)();
    void OnCommand(Command command, Handler handler);

    // The keys a command is on, as the settings spell them: the keyboard key ("F5",
    // "Ctrl+F9"), the button pressed under the chord hold and the pad button answered
    // without the chord in the command's own context (engine key names); empty for none.
    std::wstring KeyOf(Command command);
    std::wstring ChordOf(Command command);
    std::wstring PadOf(Command command);
    // The button held for the chords; empty when the chords are off.
    std::wstring ChordHold();
    // True when the help is worded for the gamepad: the player's last device, with the chords on.
    bool ChordsInHand();
    // The spoken name of a command's key for the device in the player's hands: the pad
    // button's name after a gamepad press where the command has one, else the keyboard key as
    // spelled.
    std::wstring SpokenKey(Command command);
}
