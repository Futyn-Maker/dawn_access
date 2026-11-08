#pragma once
// What the player drives the game with, and how the mod speaks of it: the device in hand
// (keyboard and mouse, or gamepad), the spoken names of keys and of the keys bound to the
// game's input actions, the player's own activity (which alone may interrupt speech), the
// gamepad as the game reads it (so the mod's chords answer in the menus as well as in play)
// and the keys or the stick a walk is driven with.

#include <string>
#include <string_view>
#include <vector>

namespace RC::Unreal
{
    class UObject;
}

namespace da::input
{
    enum class Scheme
    {
        Unknown,
        Gamepad,
        MouseKeyboard,
    };

    // The device the player last touched, or the one their last mod hotkey came from for a
    // moment after the press.
    Scheme CurrentScheme();
    std::wstring SchemeName(Scheme scheme);

    // Spoken name of the key bound to an input action ("IA_UI_Back") for the current scheme,
    // such as "Escape" or "B button". Empty when the action is unknown or nothing is bound.
    std::wstring KeyForAction(std::wstring_view actionName);

    // The spoken names of up to `count` keys bound to an action for the current scheme, in the
    // order of the game's mapping, for an action drawn with a key cap per direction.
    std::vector<std::wstring> KeysForAction(std::wstring_view actionName, int count);

    // The spoken name of one direction of the stick an action is bound to ("left stick up"
    // for a movement prompt drawn with the up cap alone); `direction` is Up, Down, Left or
    // Right. Empty when the action is bound to no stick in the current scheme.
    std::wstring DirectionalKeyForAction(std::wstring_view actionName, std::wstring_view direction);
    // The keys a key-rebind row of the settings draws in its slots, as one spoken line.
    std::wstring KeysOfRebindRow(RC::Unreal::UObject* row);

    // The keys of an action and what is spoken, for the log.
    std::wstring DescribeAction(std::wstring_view actionName);
    // Spoken name of an engine key name ("SpaceBar" -> "Space", "Gamepad_FaceButton_Bottom" ->
    // "A button"), in the words of the pad the game draws its prompts for: on a PlayStation pad
    // the same key is the cross button.
    std::wstring KeyDisplayName(std::wstring_view keyName);

    // Localized name of the pointing device for the scheme ("mouse" / "right stick").
    std::wstring PointerName();

    // Whether the game draws its prompts for a PlayStation pad (the player's controller setting,
    // else the pad connected).
    bool PlayStationPrompts();

    // Whether the game's Motion Sensor Function is on (its controls settings), as the game's own
    // shake and tilt readings ask it (UBatesGameUserSettings::GetUseSixAxis).
    bool MotionSensorOn();

    // Whether the game's "toggle grab" setting is on
    // (UBatesGameUserSettings::GetUseToggleGrabForInteractions): one press takes hold of a thing and
    // the next lets it go; with it off, the grab lasts as long as the key is held.
    bool ToggleGrabOn();

    void InvalidateCache();

    // Watches the keyboard, mouse and gamepad so speech can tell what the player asked for
    // from what the game said on its own: only the former is allowed to interrupt.
    void InstallActivityTracker();

    // Milliseconds since the player last pressed, held or moved anything. A large number
    // when the game does not have the focus or nothing has been touched yet.
    long long MsSinceInput();
    // A key the mod's own hook took before the system saw it counts as the player's input too.
    void NoteKeyboardActivity();

    // True while a key, button or stick is held down.
    bool InputHeld();

    // True while the player is holding the character's movement keys or pushing the left
    // stick. Keys the mod itself is holding do not count.
    bool MovementHeld();

    // The gamepad as the game reads it: the buttons, triggers and sticks of the first pad
    // that answers, taken from the reading the game itself takes, through XInput or through
    // Sony's pad library, so it is read in the menus as well as in play. A PlayStation pad is
    // laid over XInput's layout: its touchpad click is the Back button, Options is Start.
    struct PadReading
    {
        bool valid = false;
        unsigned buttons = 0;
        int leftTrigger = 0;
        int rightTrigger = 0;
        int leftX = 0;
        int leftY = 0;
        int rightX = 0;
        int rightY = 0;
    };
    PadReading ReadPad();

    // Whether the button, trigger or stick direction an engine key name stands for
    // ("Gamepad_FaceButton_Bottom", "Gamepad_RightTrigger", "Gamepad_RightStick_Up") is down
    // in a reading.
    bool PadKeyDown(const PadReading& pad, std::wstring_view keyName);

    // The button the mod's chords are held with. While it is down the game is shown a pad
    // with nothing pressed, and whatever was pressed under it stays hidden until it is let go
    // of, so a chord is never also a press for the game.
    void SetChordHold(std::wstring_view keyName);

    // Buttons answered the moment they go down, on the reading the game itself takes and
    // before it acts on them, as the keyboard hook answers its watched keys. The handler
    // gets the engine key name of the button, on the thread the game reads its pad on. A
    // button hidden from the game under the chord hold is not answered. Only buttons: a
    // stick or a trigger is never watched.
    using PadWatcher = void (*)(std::wstring_view keyName);
    void WatchPadButtons(const std::vector<std::wstring>& keyNames, PadWatcher handler);

    // The device the player's last mod hotkey came from is the device the answer is worded
    // for: for a moment after the press the scheme reads as that device.
    void NoteHotkeyDevice(Scheme scheme);

    // Points the game's own readings of the gamepad, XInput's and Sony's library's, through
    // the mod, so that the chords and the watched buttons are seen before the game acts on
    // them and a walk can push the stick the game already reads instead of pressing keys,
    // which would make the game treat the keyboard as the device in hand and redraw every
    // prompt for it.
    void ShareGamepadReading();

    // Holds down the game's own movement keys for a heading given in the camera's frame,
    // both from -1 to 1, exactly as a player pressing them would. False when the keys cannot
    // be held, which is the case whenever the game is not the window in front.
    bool HoldWalkKeys(double forward, double right);
    // Lets go of whatever HoldWalkKeys is holding.
    void ReleaseWalkKeys();
    // Lets go and gives the game's own gamepad reading back to it, for shutdown.
    void ForgetWalkKeys();
    // The four keys the character walks with, for the log.
    std::wstring DescribeWalkKeys();
}
