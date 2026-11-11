#pragma once
// The game's prompts worked out without the game, so that they can be tested: when a prompt's
// question is new and when it is said, which lines of the action bar are said, and the blips
// that follow the phone's unlock swipe.

#include <cstddef>
#include <cstdint>
#include <map>
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

    // A stage on a clock asks something of the player only where the game draws its question while
    // the stage stands: its Timer cue sounds then, once a stage, and only such a stage's clock running
    // out is a time's up. The prologue's shack door (BP_ShackScare's LookThroughDoorStage) runs 8 s with
    // nothing drawn, its clock timing Beth's scare: the cue had sounded at its start with no question
    // after it (research/sessions/s67-play.log, 19:10:24.912).
    // Whether an interaction's prompt asks its question now: not a glint's, not sent away, with words,
    // and drawn, in the viewport's widget tree or on the screen layer of its own the game draws a glint
    // and most interactions' prompts on (by the widget's own flags), as the prompt reader reads it. Asked
    // for the viewport's tree alone, no stage on a clock sounded its Timer cue nor said its clock run out
    // since session 66 (s71: Jessica's phone, the death machine's lever, the psycho's door bolt), and the
    // radio's dial never named its way to the station (s57).
    bool Asking(bool glint, bool hidden, bool hasText, bool inTree, bool onLayer);

    class TimedCue
    {
    public:
        // `stage`: the stage on a clock standing (-1 none); `asked`: a question of the interaction is
        // drawn. True as the cue is to sound.
        bool Update(int64_t stage, bool asked);
        // Whether the stage asked anything while it stood, so that its clock running out is said.
        bool Asked(int64_t stage) const;
        void Reset();

    private:
        int64_t m_stage = -1;
        bool m_asked = false;
    };

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
    // (IA_InteractionMovement_LeftRight, _UpDown, IA_SwipeLeftRight, IA_SwipeUpDown), the game's own
    // one-way action for the half that goes on (IA_InteractionMovement_Right, IA_Swipe_Left), whose key
    // the player is told; empty for any other action, whose prompt names its one way
    // already. act4_3's ledge shimmy asks for "the left stick left and right" and only one of them
    // takes Matt and Emily along the ledge: Andrey stepped back and forth until he held right
    // (research/sessions/s55-full.log).
    std::wstring LinearForward(std::wstring_view action, bool invert);

    // Whether a staged stage's way back is named beside its way on (LinearForward): where the stage
    // stands past its first step as its prompt comes up, a step back leads somewhere the player has
    // not been. Jack's journal in act7_1 opens at its second page (InitialAnimationStage 1): a push
    // right turns back to its first page, the foreword, with a clue of its own
    // (FoundJournalIntroduction), and with only the way on said Andrey went on and never saw it
    // (research/sessions/s61-play.log, 18:59:39; research/interactions/analysis/BP_Readjournal__act7_1.txt).
    // The step the graph stands at where it can be read (-1 where not), else the stage's own first
    // (InitialAnimationStage, the journal's alone of the game's staged stages not 0).
    bool NamesWayBack(int64_t step, int64_t initial);

    // The step a staged climb, shimmy or page turn stands at (the AnimationStage the stage writes into
    // its graphs, UBatesIntSimpleGrabAnimInst's) out of its stage's last
    // (UBatesInteractionLinearStaged.MaxAnimationStage), as a pitch: each step
    // on raises it, each step back lowers it, the stage's first reading none (Andrey on 2026-11-08,
    // chapter 5's ledge and chapter 6's tower ladders: "a rising tone from zero to one hundred going
    // forward, and the opposite going back", like the phone's swipe in the woods). Update gives the
    // level (0 to 1) to sound when the step has changed.
    class LinearProgress
    {
    public:
        std::optional<double> Update(int64_t step, int64_t last);
        void Reset()
        {
            m_step = -1;
        }

    private:
        int64_t m_step = -1;
    };

    // The last step of a staged stage that keeps no last of its own. MaxAnimationStage counts only with
    // bClampAnimationStage (UBatesInteractionLinearStaged::OnIncreaseAnimationStage raises the step
    // without limit otherwise), and act6_1's ladders clamp nothing: their stages said "unbounded" and
    // no step sounded up either ladder on 2026-11-08 (research/sessions/s59-full.log 15:47:41,
    // 15:53:05). Where the stage keeps none, the item's anim graph decides the end: its transitions
    // compare the step it is handed with each step's number (the hub ladder's EqualEqual_IntInt
    // against 0 to 14, the last to Step13_to_End). The largest number a graph's compiled script
    // compares the member with is the last step. A comparison is a math library call (EX_CallMath,
    // 0x68, and the function's address) whose two operands are the member, read as
    // EX_InstanceVariable (0x01) and its property's address (UObject::execInstanceVariable takes eight
    // bytes), and a number, EX_IntConst (0x1D) and four bytes or EX_IntConstByte (0x2C) and one, in
    // either order. Only such a call counts: a byte of the function's address may read 0x1D, and an
    // assignment to the member reads the same operands with no call. None where no call compares the
    // member with a number.
    std::optional<int64_t> LastComparedStep(const uint8_t* script, size_t size, const void* property);

    // The last step of a staged stage: its MaxAnimationStage where it clamps its steps there; else its
    // graph's (`graph`, LastComparedStep), the graph being what ends an unclamped stage; else the
    // MaxAnimationStage it names all the same, as the stage was read before the graph was asked;
    // none where neither knows.
    std::optional<int64_t> LastStep(bool clamp, int64_t named, std::optional<int64_t> graph);

    // A staged stage's prompt drawn again at each step: ApplyAnimationStage takes the stage's prompt
    // down at every step and UpdateStage sets it up again once the step has stood UIIdleTime, the same
    // words for the same input while the stage stands. Where the steps sound (LinearProgress) the
    // prompt said at every step only talked over them: fourteen times up act6_1's broken ladder while
    // Andrey held the stick, and at the hub's ladder the prompt, the way on, and the prompt again
    // within 0.6 s ("a huge number of prompts, and they didn't stop even when holding it",
    // research/sessions/s59-full.log 15:47:42, 15:53:05 to 15:53:19). Such a prompt is said when its
    // stage begins and again only when its words change (another device in hand, another input) or
    // another stage stands. `stage` identifies the stage in its interaction, -1 for none whose steps
    // sound (every prompt is then said); `last` keeps what was said.
    // A stage that draws a prompt of its own at some steps (its LinearStageUISetupScreenRules: the
    // office desk's pages of act7_2 draw the way on alone at the first page, "левый стик вправо", and
    // the way back alone at the last) sets its own prompt up again once a step has stood UIIdleTime
    // (UBatesInteractionLinearStaged::UpdateStage): the desk's, left at its default, flipped back to
    // "влево и вправо" half a second after every end, and every flip was said
    // (research/sessions/s63-play.log, 00:23:22 to 00:24:56). The stage's own prompt (`main`, the
    // actions of its InputActionAssigned) is said once while the stage stands, and again only when its
    // words change; a step's own prompt whenever it differs from the prompt drawn before it.
    struct StagePrompt
    {
        int64_t stage = -1;
        std::wstring text;
        std::wstring actions;
        std::wstring mainSaid; // the stage's own prompt's words, as said while it stands
    };
    bool SayAtStep(StagePrompt& last, int64_t stage, const std::wstring& text, const std::wstring& actions, const std::wstring& main = {});

    // When the way on of a staged stage whose input has two ways (Interactions' "Дальше: ...") is
    // said: after the game's own prompt, and not at all where the game names the way on itself with a
    // prompt for it alone (`lastActions`, the actions of the last prompt drawn since the stage began,
    // `prompted`, being `forward`). Where the stage draws prompts of its own at its steps
    // (`stepPrompts`) the way waits for the step's prompt, at most kStepPromptWait from the stage's
    // start: the desk's way on was said 1 s in, and its first page named it 1.3 s later
    // (research/sessions/s63-play.log, 00:23:23.497 and 24.748). `since`: the seconds since the stage
    // began; `main`: the actions of the stage's own prompt. A step's own prompt that names the way
    // back alone (`back`) stands at the stage's last step, where there is no way on to name.
    constexpr double kWayOnAfter = 1.0;      // the game's own prompt comes up first
    constexpr double kStepPromptWait = 3.0;  // a step's own prompt comes once the step stands idle
    enum class WayOn
    {
        Wait,
        Say,
        Skip,
    };
    WayOn WayOnNow(double since, bool prompted, const std::wstring& lastActions, const std::wstring& main, const std::wstring& forward, bool stepPrompts,
                   const std::wstring& back = {});

    // Whether a step's prompts wait for the page coming with it (hud::PageComing): while a staged
    // stage stands (`staged`) and its page is coming, once the stage's step has changed since the page
    // was last drawn (`turned`: a turn; the transcript hidden by its own key turns nothing, and its
    // answer on the bar is not held) and for at most kPageHoldMost since (`sinceTurn`).
    constexpr double kPageHoldMost = 1.5;
    bool HoldsForPage(bool staged, bool pageComing, bool turned, double sinceTurn);

    // An activity's input stage (UBatesActivitySimpleInputStage) sets its prompt up as it begins (its
    // InputWidget, an interaction prompt) and takes it down as it ends, and the input it binds counts
    // only while it stands (bContinueOnInputInitialPress binds ETriggerEvent::Triggered, which a stick
    // held fires from the stage's first frame). How long it stands is the stage's own: a stage on a clock
    // (bShouldTimeOut, SecondsUntilTimeout: chapter 8's bolt door, 3 s) ends with its clock; one that
    // goes on at its end marker (EEndMarkerReachedAction GoToNextStage) ends with its stretch of the
    // activity's sequence; one that loops or pauses there waits for the input. Emily's three swings in
    // act6_1 (A_cut_the_cable_emilyhanging, MSQ_Emilyhanging's markers) stand 0.83, 0.67 and 0.60 s, a
    // swing missed played again 5 s on; read as an interaction's prompt, 0.15 to 0.2 s after it came up
    // and its direction the last word, the key came too late for eleven of fourteen
    // (research/sessions/s59-full.log 15:35:36 to 15:36:28; the swing passed only once Andrey held the
    // stick). A prompt that does not wait is a quick-time event's: its direction's cue at once, its key
    // over anything being said, the stage's own verdict as it goes.
    enum class InputWindow
    {
        Waits,  // looping or pausing at its end marker: the prompt stands until the input
        Moment, // gone at its end marker
        Clock,  // gone when its clock runs out
    };
    // A stage gone at its end marker asks for nothing at a moment where its end leads into a stage that
    // waits for the same input: chapter 4's mine shimmy (act3_2 A_aim_at_killer) steps on in windows
    // of 0.9 to 1.3 s, and a window run out takes it to an idle (TempIdle, looping) that goes on with
    // the same push (research/mechanics/input_stages.tsv, the audit of every such stage). So does an
    // end outcome that is the input's own (UpdateStage writes OverrideStageOutcome over StageOutcome
    // as it takes the input, and a stage played again keeps it).
    struct InputStage
    {
        int endAction = 0;     // EEndMarkerReachedAction: GoToNextStage 0, Loop 1, Pause 2
        bool timesOut = false; // bShouldTimeOut
        double seconds = 0.0;  // SecondsUntilTimeout (5 s in the stage's constructor)
        bool endWaits = false; // its end marker leads where the same input is still taken
    };
    InputWindow WindowOf(const InputStage& stage);

    // How such a stage ended, by its own record as its prompt goes (OnFinished destroys the prompt
    // before the next stage begins): the input taken (bInputComplete, set by ProcessAssignedInput,
    // or the stage going on by the input's own outcome), its clock run out (bHasTimedOut), or its
    // stretch over with neither. A stretch over is a clock run out like any other: the Fail cue and
    // "Time's up" (Andrey, 2026-11-06), where the stage names what a miss leads to (`endNamed`: the
    // swings' StageOutcome, their swing played again). One whose end names nothing (act20_3's page
    // windows, which no play has reached) leaves the activity to go on as the game has it, and the
    // mod says nothing of it (Over).
    enum class InputEnd
    {
        Taken,
        TimedOut,
        Closed,
        Over,
    };
    InputEnd EndOf(bool inputComplete, bool timedOut, bool byInputsOutcome, bool endNamed);

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
        // The bar's lines now, one per prompt; the lines to say now. `scope`, from 0 on, is a staged
        // stage standing (Interactions' SoundingStagedStage): the bar empties at its every step and
        // fills again with the same lines, which were said at every page of the office desk
        // (research/sessions/s63-play.log, 00:23:37 to 00:24:56), so while the stage stands a line is
        // said only where its action's words differ from those said for it before (`actions`, one per
        // line: the transcript's key toggled to "show" and back is said each time).
        std::vector<std::wstring> Update(double now, bool pageUp, const std::vector<std::wstring>& lines, int64_t scope = -1,
                                         const std::vector<std::wstring>& actions = {});

    private:
        std::vector<std::wstring> m_lines;
        std::set<std::wstring> m_said;                      // the lines said while they stayed on the bar
        std::map<std::wstring, std::wstring> m_byAction;   // the words said for each action while the scope stands
        int64_t m_scope = -1;
        double m_changedAt = -1.0;
    };

    // The phone in the woods is unlocked by swipes (ABatesInteractionPhoneUnlock): from the moment
    // the game shows the swipe's prompt (its state 2) every swipe it takes adds to a progress
    // towards a target, which slides back once no swipe came for a moment. The blips follow it,
    // rising as it fills and falling as it slides back, each kBlipStep of the way and kBlipApart
    // apart, and the unlock (state 3 after 2) is the challenge won.
    // The help line that names the snow globe's hidden shake (IA_ControllerShake), by what takes it:
    // the keyboard's W or S; another pad's left stick up or down; a PlayStation pad only shaken, and
    // only while the game's Motion Sensor Function is on (UBatesInputRouter::DoControllerShakeWork asks
    // GetUseSixAxis; BatesControllerVendorInputModifier's PSControllerMultiplier 0 gives such a pad's
    // stick nothing), the keyboard taking it all the same.
    const wchar_t* ShakeHelp(bool pad, bool playStation, bool motionSensor);

    // The help line for a prompt that shows a move of the stick beside the grab: the move is made with
    // the grab held, the item's graph taking it only while both stand. act6_3's flare asks for a swipe
    // right and R2 at once (BP_Lightflareescaped: its stage bStageShouldAlwaysUseHoldGrab, its graph
    // BooleanAND of bIsInputPressed, raised for the moment the swipe is made, and bGrabButtonInput,
    // held with R2); Andrey stood two minutes there pushing the stick and then R2
    // (research/sessions/s60-full.log 17:56:50 to 17:58:29). Chapter 3's lantern makes a half circle
    // with R2 held, and letting go drops it. The key of the line by the prompt's actions, none for any
    // other prompt.
    const wchar_t* HeldMoveHelp(std::wstring_view actions);

    // The bar's lines shown again with a document's page shown again as it is being read
    // (hud::PageResumedAt): those said within `within` seconds are still to be said behind the page,
    // the others are new and said. `saidAt` holds when each line was last said.
    std::vector<std::wstring> NotSaidWithin(const std::vector<std::wstring>& fresh, const std::map<std::wstring, double>& saidAt, double now,
                                            double within);

    // A prompt that draws the grab beside a move of the stick (a push, a swipe, a half circle): the
    // move is made with the grab held. Of the interaction stages of the whole game, 79 draw such a
    // pair, and they say it in the stage's own UI keys: "hold" draws the grab alone, and once it is
    // held "move_left", "swipe_down", "rotate_right" draw the move with the grab beside it, its key
    // lit while it is down; let go, the stage goes back to "hold" (research/interactions/analysis).
    // act6_3's flare draws only the pair ("swipe_right": IA_Swipe_Right and IA_GrabInteraction), and
    // Andrey, reading "left stick right, R2" as two ways to do one thing, stood two minutes there
    // (research/sessions/s60-full.log). Such a prompt is said in the game's own way of naming an
    // action and its key, the grab first: "Hold: R2, left stick right" (Andrey on 2026-11-08: "an
    // in-game style prompt like 'hold R2 and push left stick right', and similarly for the mouse").
    // The places of the grab and of the move among the prompt's actions, in the order the widget
    // draws them; none unless the prompt holds the grab and one move and nothing else.
    struct HeldPair
    {
        size_t grab = 0;
        size_t move = 0;
    };
    std::optional<HeldPair> HeldMoveOf(const std::vector<std::wstring>& actions);

    // Whether the grab is held through such a move, as the stage itself has it: it toggles only where
    // the player's "toggle grab" setting is on and the stage does not insist on holding
    // (UBatesInteractionStageBase::InitialiseStage writes bUsingToggleGrab = the setting
    // bUseToggleGrabForInteractions and not bStageShouldAlwaysUseHoldGrab, and ProcessGrabReleased
    // lets go on release only without it). The stage's own answer where it is known, else the
    // setting: a stage that toggles has the grab on between two presses, and "hold" would be wrong.
    bool GrabHeld(std::optional<bool> stageToggles, bool settingToggles);

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
