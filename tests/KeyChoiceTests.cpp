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

TEST_CASE("a shoulder button waits a reading or two for the chord's hold before the game has it: the last subtitle, 2026-11-09")
{
    const uint16_t rb = 0x0200, lb = 0x0100, back = 0x0020, a = 0x1000;
    const uint16_t graced = lb | rb;
    // R1 a frame before the touch pad: kept back, then the chord's (HideChord's latch hides it after).
    input::ChordGrace grace;
    CHECK(grace.HeldBack(rb, false, graced) == rb);
    CHECK(grace.HeldBack(rb | back, true, graced) == 0);
    // Let go of the hold with R1 still down: never kept back again while it stays down.
    CHECK(grace.HeldBack(rb, false, graced) == 0);
    CHECK(grace.HeldBack(rb, false, graced) == 0);
    // R1 alone: the game has it at its third reading, and for as long as it stays down.
    input::ChordGrace alone;
    CHECK(alone.HeldBack(rb, false, graced) == rb);
    CHECK(alone.HeldBack(rb, false, graced) == rb);
    CHECK(alone.HeldBack(rb, false, graced) == 0);
    CHECK(alone.HeldBack(rb, false, graced) == 0);
    // Reports the game does not keep count for nothing.
    input::ChordGrace queued;
    CHECK(queued.HeldBack(rb, false, graced, false) == rb);
    CHECK(queued.HeldBack(rb, false, graced, false) == rb);
    CHECK(queued.HeldBack(rb, false, graced, true) == rb);
    CHECK(queued.HeldBack(rb, false, graced, true) == rb);
    CHECK(queued.HeldBack(rb, false, graced, true) == 0);
    // Let go, and pressed again: it waits again.
    CHECK(alone.HeldBack(0, false, graced) == 0);
    CHECK(alone.HeldBack(rb, false, graced) == rb);
    // The other buttons never wait: a face button is on a quick-time event's clock.
    CHECK(alone.HeldBack(a, false, graced) == 0);
    input::ChordGrace both;
    CHECK(both.HeldBack(rb | lb | a, false, graced) == (rb | lb));
    // Two pads read each frame, one idle: each pad's wait is its own (the mod keeps one per pad), and
    // the idle pad's readings never make the other's R1 wait for ever.
    input::ChordGraces pads;
    int given = 0;
    for (int frame = 0; frame < 6; ++frame)
    {
        if (pads.HeldBack(4, rb, false, graced) == 0) ++given;
        CHECK(pads.HeldBack(0, 0, false, graced) == 0);
    }
    CHECK(given == 4);
    CHECK(pads.HeldBack(input::ChordGraces::kPads, rb, false, graced) == 0);
}

namespace
{
    // The keys as the engine tells them: the mouse's buttons are mouse buttons, the mouse's moves are
    // not digital, the pad's are a pad's.
    input::KeyTraits Key(const wchar_t* name)
    {
        const std::wstring n(name);
        input::KeyTraits k;
        k.name = n;
        k.mouseButton = n == L"LeftMouseButton" || n == L"RightMouseButton" || n == L"MiddleMouseButton";
        k.digital = !(n == L"MouseX" || n == L"MouseY" || n == L"Mouse2D" || n.rfind(L"Gamepad_Left", 0) == 0);
        k.gamepad = n.rfind(L"Gamepad_", 0) == 0;
        return k;
    }

    int Shown(std::initializer_list<const wchar_t*> names, input::IconPriority priority)
    {
        std::vector<input::KeyTraits> keys;
        for (const auto* n : names)
            keys.push_back(Key(n));
        return input::ShownKeyIndex(keys, priority);
    }

    std::wstring Join(const std::wstring& chord, const std::wstring& key)
    {
        return chord + L" + " + key;
    }
}

TEST_CASE("keys: the prompt draws the key the game's mouse and keyboard icon setting prefers, 2026-11-10")
{
    using P = input::IconPriority;
    // The prologue's laptop: grabbing the note, IA_GrabInteraction [LeftMouseButton, E] on the keyboard
    // and the mouse. With Andrey's setting at "Prefer Keyboard" the game drew E; the mod said the first.
    CHECK(Shown({L"LeftMouseButton", L"E"}, P::Keyboard) == 1);
    CHECK(Shown({L"LeftMouseButton", L"E"}, P::Standard) == 0);
    CHECK(Shown({L"LeftMouseButton", L"E"}, P::Mouse) == 0);
    // The interaction's press, IA_StartInteraction [E, LeftMouseButton]: the mouse preferred draws its button.
    CHECK(Shown({L"E", L"LeftMouseButton"}, P::Mouse) == 1);
    CHECK(Shown({L"E", L"LeftMouseButton"}, P::Keyboard) == 0);
    CHECK(Shown({L"E", L"LeftMouseButton"}, P::Standard) == 0);
    // The way back, IA_BackoutInteraction [BackSpace, RightMouseButton].
    CHECK(Shown({L"BackSpace", L"RightMouseButton"}, P::Mouse) == 1);
    // A key alone is drawn whatever the setting prefers; a key bound to nothing is passed over.
    CHECK(Shown({L"MouseRight"}, P::Keyboard) == 0);
    CHECK(Shown({L"None", L"W"}, P::Standard) == 1);
    CHECK(Shown({L"W", L"None"}, P::Mouse) == 0);
    // No key of the kind preferred: the first.
    CHECK(Shown({L"Mouse2D", L"LeftMouseButton"}, P::Keyboard) == 0);
    CHECK(Shown({}, P::Standard) == -1);
    CHECK(Shown({L"None"}, P::Standard) == -1);
}

TEST_CASE("keys: a mouse swipe's cap lights the left button it is made with; other chords are not drawn, 2026-11-10")
{
    // The prologue's phone, IA_Swipe_Right: MouseRight chorded with IA_MouseClickChord, the left
    // button (MC_Gameplay); moving the mouse did nothing until the button was held.
    CHECK(input::CapWords(L"mouse right", L"left mouse button", true, &Join) == L"left mouse button + mouse right");
    // A chord whose key is no mouse button is not drawn on the cap, and no chord leaves the key alone.
    CHECK(input::CapWords(L"mouse right", L"E", false, &Join) == L"mouse right");
    CHECK(input::CapWords(L"mouse right", L"", true, &Join) == L"mouse right");
    // A quick-time event's stick direction on the mouse, IA_QTELeftStickRight: MouseRight with no chord.
    CHECK(input::CapWords(L"mouse right", L"", false, &Join) == L"mouse right");
}
