#pragma once
// Which of an action's keys the device in hand presses, worked out without the game, so that it can
// be tested.

#include <cstdint>
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

    // A chord's second button pressed a moment before its hold reaches the game, which takes it as it
    // goes down: R1 pressed a frame before the touch pad opened the game's status page five times of
    // thirty-six last-subtitle chords on 2026-11-09 (research/sessions/s63-play.log, 01:04:18 and on).
    // The shoulder buttons (`graced`), which the game asks for nothing on a clock (its quick-time
    // events take the face buttons, the triggers and the left stick), are kept from the game for the
    // first kChordGraceReadings of its readings of the pad that find them down while the hold is up:
    // the hold coming meanwhile takes them into the chord, and the game never has them. A press let go
    // within those readings (two frames) is lost to the game, as no reading is given a button the pad
    // does not hold. One for each pad the game reads: the readings of one pad say nothing of another's.
    constexpr int kChordGraceReadings = 2;
    class ChordGrace
    {
    public:
        // The buttons to keep from the game at this reading: `buttons` as the pad holds them, `holdDown`
        // the chord's hold, `kept` whether the game keeps this reading as the pad's state (every
        // reading of an XInput pad; of a PlayStation pad's reports queued since the last frame, the
        // last): only those count.
        uint16_t HeldBack(uint16_t buttons, bool holdDown, uint16_t graced, bool kept = true);

    private:
        uint16_t m_given = 0;      // down and given to the game, or taken by the chord
        uint8_t m_held[16] = {};   // the readings each has been kept from so far
    };

    // The waits of every pad the game reads, one each: XInput's four by their index, the PlayStation
    // handles from 4 on. With one for all, an idle second pad's readings started the first's wait
    // again at every frame, and its shoulder buttons never reached the game.
    class ChordGraces
    {
    public:
        static constexpr uint32_t kPads = 8;
        uint16_t HeldBack(uint32_t pad, uint16_t buttons, bool holdDown, uint16_t graced, bool kept = true)
        {
            return pad < kPads ? m_pads[pad].HeldBack(buttons, holdDown, graced, kept) : 0;
        }

    private:
        ChordGrace m_pads[kPads];
    };
}
