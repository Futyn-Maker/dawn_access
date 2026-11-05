#pragma once
// The game's prompts worked out without the game, so that they can be tested: when a prompt's
// question is new and when it is said, which lines of the action bar are said, and the blips
// that follow the phone's unlock swipe.

#include <set>
#include <string>
#include <vector>

namespace da::prompt
{
    // How long a readout must stay as it is before it is said. A glint's prompt follows the game's
    // verdict on a press frame by frame, and that verdict flickers on for a moment as the
    // character walks through the place or turns past the thing (150 ms in the lodge on
    // 2026-10-24, the press refused a moment later): such a prompt is no question the game asks,
    // so a glint's waits longer than an interaction's.
    constexpr double kPromptStable = 0.1;
    constexpr double kGlintStable = 0.4;

    // One prompt widget's question: its readout at the last poll and whether it was said.
    struct Question
    {
        std::wstring text;
        std::wstring actions; // the actions of its caps
        double since = 0.0;   // when the readout became what it is, or the prompt was set up anew
        bool announced = false;
        bool hidden = false; // an interaction prompt the game sent away and has not set up since
    };

    // The game sets an interaction prompt up for each question it asks, the same pooled widget
    // serving one question after another (the hint to walk, a door's push, a note's grab), and
    // sends it away with HideAndDestroy. A question set up after the last one went away is new,
    // even in the same words; set up again while it stays, it is the same question still.
    void SetUp(Question& q, double now);
    void Hide(Question& q);

    // A poll's readout of the prompt: true when it is to be said now. The same words for another
    // action (the hint to walk, then a note's step closer) are another question.
    bool Read(Question& q, double now, const std::wstring& text, const std::wstring& actions, bool glint);

    // A glint's prompt while the character is out of the player's hands: not read, and one still
    // up stays the same prompt; only its going down makes its next showing a new one.
    void Withheld(Question& q, bool stillUp);

    // Whether a key cap of the game's shows a key: one set up with an input action shows that
    // action's keys; one set up with a key alone, which only the rebinding rows of the controls page
    // do (UBatesKeyRebindSlotWidget::UpdateCurrentKey, UBatesKeyRebindSettingWidget::NativeTick, with
    // UBMUIInputActionWidget::SetupFromKey), shows that key. A cap set up with nothing is an empty
    // slot: the interaction prompt fades it out (WBP_Interaction: TargetMainOpacity and
    // TargetSecOpacity are 1 with an action and 0 without), and its key caps keep the letters of the
    // question before, which are no key of this one: at the mine on 2026-11-05, on the keyboard, the
    // cigar stub's grab was read "D, left mouse button", the D left over from the totem's "W, S, A,
    // D", and "D" alone was read after each clue.
    bool CapShowsKey(bool hasAction, bool setUpFromKey);

    // The first question an interaction asks waits a moment for a hint the game shows with it, so
    // that the hint comes first and its keys after, whichever the game draws first: at the danger
    // totem on 2026-11-04 (on the pad) the inspection's hint came 0.4 s before its stick, at the
    // fortune totem on 2026-11-05 (on the keyboard) 0.3 s after, and its keys were said first. The
    // wait opens at the first look the interaction's prompts have words (its own or the action
    // bar's), and lasts until a text of the HUD has been said since kHintWait before it opened, or
    // for kHintWait; later questions of the interaction do not wait.
    constexpr double kHintWait = 0.6;
    class HintFirst
    {
    public:
        // Whether the character is bound to an interaction, at every look.
        void Interaction(bool in);
        // The interaction's prompts have words at this look.
        void Asked(double now);
        // Whether the prompts wait at this look; `hintSaidAt` is when a text of the HUD was last said.
        bool Waits(double now, double hintSaidAt) const;

    private:
        bool m_in = false;
        bool m_pending = false;
        double m_openedAt = -1.0;
    };

    // The action bar in play: the prompts the game pins to the bottom of the screen while no menu
    // page is up. It answers the player's own press (a film's skip prompt comes up on the first
    // press and the second press skips), so it is read almost at once, after kBarStable. While a
    // page is up the menus own the bar, and the bar as play left it is kept: a lesson's prompt
    // still there when the page goes asks nothing new, one gone meanwhile is gone. A line that
    // left the bar is said again when it comes back.
    constexpr double kBarStable = 0.1;
    class Bar
    {
    public:
        // The bar's lines now, one per prompt; the lines to say now.
        std::vector<std::wstring> Update(double now, bool pageUp, const std::vector<std::wstring>& lines);

    private:
        std::vector<std::wstring> m_lines;
        std::set<std::wstring> m_said; // the lines said while they stayed on the bar
        double m_changedAt = -1.0;
    };

    // The phone in the woods is unlocked by swipes (ABatesInteractionPhoneUnlock): from the moment
    // the game shows the swipe's prompt (its state 2) every swipe it takes adds to a progress
    // towards a target, which slides back once no swipe came for a moment. The blips follow it,
    // rising as it fills and falling as it slides back, each kBlipStep of the way and kBlipApart
    // apart, and the unlock (state 3 after 2) is the challenge won.
    constexpr double kBlipStep = 0.095;
    constexpr double kBlipApart = 0.08;
    class Swipe
    {
    public:
        struct Step
        {
            bool stateChanged = false;
            bool began = false;    // the swipes count from now
            bool unlocked = false; // the challenge won
            bool blip = false;     // a blip at `level`
            double level = 0.0;    // the share of the target, 0 to 1
        };
        // Another phone taken out: its state is not known yet.
        void Reset()
        {
            m_state = -1;
        }
        Step Update(double now, int state, double progress, double target);
        int State() const
        {
            return m_state;
        }

    private:
        int m_state = -1;
        double m_level = 0.0; // the share of the target at the last blip
        double m_blipAt = 0.0;
    };
}
