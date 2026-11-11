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

    // The game's setting for which key its prompts draw where an action has several on the keyboard
    // and the mouse (the controls tab's "Mouse/Keyboard Icons", UBatesGameUserSettings's
    // KBMIconSwitchBehaviour, EBMUIKBMIconBehaviour): the first key mapped, a mouse button first, or a
    // key of the keyboard first.
    enum class IconPriority
    {
        Standard = 0,
        Mouse = 1,
        Keyboard = 2,
    };

    // A key as the engine tells it (UKismetInputLibrary's Key_IsDigital, Key_IsMouseButton and
    // Key_IsGamepadKey).
    struct KeyTraits
    {
        std::wstring name;
        bool digital = true;
        bool mouseButton = false;
        bool gamepad = false;
    };

    // Which of an action's keys on the keyboard and the mouse its prompt draws, as the game's UI
    // framework picks it (FProcessedActionMapping::ProcessKeyArray, 0x421ef70), which the mod reads
    // where it can and follows here where it cannot: a key alone is drawn; of several, the first in the
    // game's order, unless the setting prefers the mouse (the first mouse button) or the keyboard (the
    // first digital key that is neither a mouse button nor a pad's), and the first again where none is
    // such. Keys bound to nothing ("None") are passed over. -1 for none. With the keyboard preferred,
    // grabbing the prologue's note drew E, of [LeftMouseButton, E], where the mod had said the first
    // (research/sessions/s67-play.log, 18:54:35).
    int ShownKeyIndex(const std::vector<KeyTraits>& keys, IconPriority priority);

    // The way a key turns a two-way action, by the modifiers its mapping gives it in order, as Enhanced
    // Input works its value out: a key's press is +X; UInputModifierSwizzleAxis reorders the axes
    // (EInputAxisSwizzle: YXZ 0, ZYX 1, XZY 2, YZX 3, ZXY 4), UInputModifierNegate flips those it
    // names. The aim's W, S, A and D of MC_Gameplay: W swizzled is up, S swizzled and negated down, A
    // negated left, D alone right. "Up", "Down", "Left" or "Right"; empty where the key turns it no
    // single way.
    struct KeyModifier
    {
        enum class Kind
        {
            Swizzle,
            Negate,
            Other,
        } kind = Kind::Other;
        int order = 0;              // Swizzle: EInputAxisSwizzle
        bool x = true, y = true, z = true; // Negate: the axes it flips
    };
    std::wstring WayOfKey(const std::vector<KeyModifier>& modifiers);

    // The words of a key cap drawn with a chord (FKeyWithChord's ChordAction: the action held for the
    // key to count): the cap's picture lights the chord's key where it is a mouse button, and only
    // then (UBMUIInputActionInternalWidget::Setup sets the mouse picture's button from its
    // MouseButtonIndexMap), so a swipe of the mouse, made with the left button held, is "left mouse
    // button + mouse right" (`join` of the chord's words and the key's, key.chord); a chord the cap
    // does not draw leaves the key's words alone. The prologue's phone swipe was said "mouse right",
    // and moving the mouse did nothing until the left button was held (s67, 19:02:03).
    std::wstring CapWords(const std::wstring& keyWords, const std::wstring& chordWords, bool chordMouseButton,
                          const std::function<std::wstring(const std::wstring&, const std::wstring&)>& join);

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
