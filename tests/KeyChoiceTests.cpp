// input/KeyChoice: which of an action's keys the device in hand presses.
#include "Support.hpp"
#include "input/KeyChoice.hpp"

#include <set>
#include <string>
#include <vector>

using namespace da;

namespace
{
    // The engine's flag as the game registers its own keys (FBatesGameModule::StartupModule): the
    // touch pad's swipes and the stick's half circles and the controller's shake are a gamepad's, the
    // mouse's moves a mouse button's; the engine's own Gamepad_ keys are a gamepad's too.
    bool GamepadKey(const std::wstring& key)
    {
        static const std::set<std::wstring> pad = {
            L"TouchPadRightCumulativeDelta", L"TouchPadLeftCumulativeDelta", L"TouchPadXAxisCumulativeDelta", L"TouchPadYAxisDelta",
            L"MovementHalfCircleClockwise",  L"MovementHalfCircleAntiClockwise", L"MovementHalfCircleAxis", L"ControllerShake"};
        return key.rfind(L"Gamepad_", 0) == 0 || pad.count(key) > 0;
    }

    std::vector<std::wstring> Pad(const std::vector<std::wstring>& keys)
    {
        return input::KeysForDevice(keys, true, &GamepadKey);
    }

    std::vector<std::wstring> Desk(const std::vector<std::wstring>& keys)
    {
        return input::KeysForDevice(keys, false, &GamepadKey);
    }
}

TEST_CASE("keys: the touch pad's swipe is the pad's key when the game's touch pad setting is on, 2026-11-08")
{
    // act6_3's flare, the stick standing in for the touch pad (the game's setting): the engine's
    // answer for IA_Swipe_Right was [MouseRight, Gamepad_LeftStick_Right].
    CHECK(Pad({L"MouseRight", L"Gamepad_LeftStick_Right"}) == std::vector<std::wstring>{L"Gamepad_LeftStick_Right"});
    CHECK(Desk({L"MouseRight", L"Gamepad_LeftStick_Right"}) == std::vector<std::wstring>{L"MouseRight"});
    // With the touch pad itself (MC_Gameplay's own mapping): the pad's key is the touch pad's swipe,
    // not the mouse's move it was once named by.
    CHECK(Pad({L"MouseRight", L"TouchPadRightCumulativeDelta"}) == std::vector<std::wstring>{L"TouchPadRightCumulativeDelta"});
    CHECK(Desk({L"MouseRight", L"TouchPadRightCumulativeDelta"}) == std::vector<std::wstring>{L"MouseRight"});
    // Where the pad has both, its own stick comes first.
    CHECK(Pad({L"TouchPadXAxisCumulativeDelta", L"Gamepad_LeftX", L"MouseX"}).front() == L"Gamepad_LeftX");
}

TEST_CASE("keys: a half circle of the stick is the pad's, its letter the keyboard's")
{
    // IA_RotateRightGesture: MovementHalfCircleClockwise, D. On the keyboard the half circle came
    // first and was named in place of D.
    CHECK(Pad({L"MovementHalfCircleClockwise", L"D"}) == std::vector<std::wstring>{L"MovementHalfCircleClockwise"});
    CHECK(Desk({L"MovementHalfCircleClockwise", L"D"}) == std::vector<std::wstring>{L"D"});
    CHECK(Desk({L"MovementHalfCircleAxis", L"D", L"A"}) == std::vector<std::wstring>({L"D", L"A"}));
    // The snow globe's shake: the controller's own, W and S on the keyboard.
    CHECK(Desk({L"ControllerShake", L"W", L"S"}) == std::vector<std::wstring>({L"W", L"S"}));
}

TEST_CASE("keys: the pad's buttons and the keyboard's keys as before")
{
    // IA_InteractionMovement_UpDown, as the engine answered it on 2026-11-08.
    const std::vector<std::wstring> upDown{L"Gamepad_LeftY", L"W", L"S", L"None"};
    CHECK(Pad(upDown) == std::vector<std::wstring>{L"Gamepad_LeftY"});
    CHECK(Desk(upDown) == std::vector<std::wstring>({L"W", L"S", L"None"}));
    // IA_GrabInteraction.
    CHECK(Pad({L"Gamepad_RightTrigger", L"LeftMouseButton", L"E"}) == std::vector<std::wstring>{L"Gamepad_RightTrigger"});
    CHECK(Desk({L"Gamepad_RightTrigger", L"LeftMouseButton", L"E"}) == std::vector<std::wstring>({L"LeftMouseButton", L"E"}));
    // A touch screen's gesture is no device's here; an action with none for the device keeps its first.
    CHECK(Pad({L"Gesture_Pinch", L"MouseWheelAxis"}) == std::vector<std::wstring>{L"Gesture_Pinch"});
    CHECK(Desk({L"Gamepad_FaceButton_Bottom"}) == std::vector<std::wstring>{L"Gamepad_FaceButton_Bottom"});
    CHECK(Pad({}).empty());
}
