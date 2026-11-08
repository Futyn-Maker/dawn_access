#pragma once
// Which of an action's keys the device in hand presses, worked out without the game, so that it can
// be tested.

#include <functional>
#include <string>
#include <vector>

namespace da::input
{
    // The keys of an action the device in hand presses, in the game's order. Whether a key is a pad's
    // is the engine's own flag (UKismetInputLibrary::Key_IsGamepadKey, `gamepadKey`), which the game
    // sets for its own keys as it registers them (FBatesGameModule::StartupModule: the touch pad's
    // swipes, the stick's half circles and the controller's shake as a gamepad's keys, 0x1; the
    // mouse's moves as a mouse button's, 0x4), never their names: with the game's touch pad setting
    // on, the swipe right is TouchPadRightCumulativeDelta, which a pad was not to name, so the mouse's
    // MouseRight was named in its place; and on the keyboard a half circle's
    // MovementHalfCircleClockwise came before its D. On a pad, its own buttons and sticks (Gamepad_)
    // come first, then its other keys; on the keyboard and the mouse, every key no pad has. A touch
    // screen's gestures (Gesture...) belong to neither. With none for the device, the first key the
    // action has.
    std::vector<std::wstring> KeysForDevice(const std::vector<std::wstring>& keys, bool pad, const std::function<bool(const std::wstring&)>& gamepadKey);
}
