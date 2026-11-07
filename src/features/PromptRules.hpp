#pragma once
// The game's prompts worked out without the game, so that they can be tested: when a prompt's
// question is new and when it is said, which lines of the action bar are said, and the blips
// that follow the phone's unlock swipe.

#include <optional>
#include <set>
#include <string>
#include <string_view>
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

    // Whether an interaction's anim state is an idle it comes back to after a step, where the game
    // takes the stage's input again: the states are named for the step they follow
    // (bash_door_down_idle_1 after the first barge at the kitchen door of chapter 3, _idle_2 after
    // the second; the first, _idle_0, comes before any step). The game hides the push's prompt at
    // each barge and never draws it again, and the door waits for two more pushes
    // (research/mechanics/gaps.md), so where no prompt has been drawn since such a state began, its
    // interaction's last prompt is said again.
    bool IdleAfterStep(std::wstring_view state);

    // The indices UAnimInstance::GetCurrentStateName takes for the state machines of an anim graph,
    // given which of its class's nodes are machines, in the class's order (AnimNodeProperties). The
    // index counts the nodes from the last (FAnimInstanceProxy::GetStateMachineInstance reads node
    // Num - 1 - index as a machine without asking), so only these may be passed: asking 0 to 3 of
    // every graph read other nodes as machines and faulted at every look of the interactions on
    // 2026-11-06 (FAnimNode_StateMachine::GetCurrentStateName, 128 times in a session), and the
    // states that read nothing.
    std::vector<int> MachineIndices(const std::vector<bool>& nodeIsMachine);

    // Whether a stage on a clock has run out as its answer. The stage's own count passes its seconds
    // (UBatesInteractionStageBase::UpdateStage raises bHasTimedOut on its anim graphs then, every
    // frame, whatever she does), and a graph takes it only from a state that waits for her (its
    // name says idle or loop: the death machine's grab_idle goes to grab_timeout_end, the psycho
    // door's lifted_bolt_idle to lifted_bolt_fail, Emily's GrabLoop to emily_pullsupself). So the
    // clock has run out as the answer where a graph stood waiting as the count passed and the graph
    // has moved on since (or the interaction ended at once). Jessica's phone, picked up at 4.9 of its
    // 5 s, stood in ik_animation as its count went on: no time's up.
    bool WaitingState(std::wstring_view state);
    bool TimeoutTaken(const std::vector<std::wstring>& atLimit, const std::vector<std::wstring>& now);

    // An interaction whose stick's side is the choice, with no word for either side on the screen:
    // chapter 4's death machine (act4_1's BP_Deathmachinelever). Its graph pulls the lever left once
    // the stick passes its threshold that way with R2 held (pull_lever_left_to_end, which sets
    // ChrisMovedLeverAsh), right the other way (ChrisMovedLeverJosh), and its stage's 30 s running out
    // spares neither (ChrisChoseNobody). A sighted player sees whom each side's blade hangs over. The
    // sides are said in the game's own names for the two (the string-table keys of their characters'
    // LocalisedName), as the graph comes to the state that takes the stick (`state`).
    struct Sides
    {
        std::wstring_view left;
        std::wstring_view right;
        std::wstring_view state;
    };
    std::optional<Sides> SidesOf(std::wstring_view interactionClass);

    // A dial the stick turns whose place the game keeps as one of its global states: chapter 6's tower
    // radio (act5_2's BP_Radio keeps it in the global int RadioDialPosition, 0 to 3; at 3 the ranger
    // station answers). Its stage takes IA_RotateAxisGesture, whose positive half (a clockwise half
    // circle, D on the keyboard: MC_Gameplay) turns the needle up the scale toward the station, the
    // game's own one-way action for that half being IA_RotateRightGesture, bound to the same keys. A
    // sighted player sees the needle move: its place is said as it changes, and the way to the
    // station once the stage asks for the turn.
    struct Dial
    {
        std::wstring_view state;      // the global int it keeps its place in
        int station = 0;              // the place the stage waits for
        std::wstring_view turnAction; // the stage's own input
        std::wstring_view forward;    // the game's one-way action that turns it toward the station
    };
    std::optional<Dial> DialOf(std::wstring_view interactionClass);

    // The way a staged climb or shimmy goes on (UBatesInteractionLinearStaged: the positive half of its
    // input raises its stage, the negative half lowers it, unless bInvertInput): for an axis action
    // (IA_InteractionMovement_LeftRight, _UpDown), the game's own one-way action for the half that goes
    // on, whose key the player is told; empty for any other action, whose prompt names its one way
    // already. act4_3's ledge shimmy asks for "the left stick left and right" and only one of them
    // takes Matt and Emily along the ledge: Andrey stepped back and forth until he held right
    // (research/sessions/s55-full.log).
    std::wstring LinearForward(std::wstring_view action, bool invert);

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
