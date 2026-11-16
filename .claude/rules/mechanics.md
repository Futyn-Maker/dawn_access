---
paths:
    - "src/features/{Choices,ChoiceRules,Prompts,PromptRules,Qte,QteRules,DontMove,CalmRules}.*"
---

# Choices, prompts, quick-time events, Don't Move

- **Every mechanic ends the same way:** a tone for what happened, and at the detailed level the word for it: `result.success`, `result.failure`, and `result.timeout` for a clock run out. A clock that runs out is always the Fail cue and "Time's up.", also where doing nothing is an answer of its own. A mechanic the game resolves by itself, with nothing shown, has no verdict.

## Choices

- **Widgets.** Each option is a `BatesDirectionalChoiceWidget` (`WBP_Choice_C`), set up by `BP_Setup(InInputDirection Left 0/Right 1, InText, InSubText, bCanTimeOut)`.
    - Options set up within 1.5 s are one choice, read once both sides are in, or after 2 s.
    - The option widgets hang in the world, in no widget tree, so `obj::IsWidgetShown` is false for them.
    - A choice is over only when the game says so (`ChoiceEnded(EBatesDirectionalChoiceResult: NONE, LEFT, RIGHT, TIMEDOUT, STAGEFINISHED)`, which reaches the native hook through the override's parent call) or when its option widgets die. Never end a choice by a visibility or a timer.
    - The two option widgets are pooled and serve every choice: a widget set up anew is taken from every other choice, and the choice it belongs to is searched newest first. An old choice still listing them once swallowed the next choices' presses.
- **The reading** is "Choice." or "Timed choice." at the detailed level, then each option with its side, its words, the players' share where the game shows one, and the key the game draws beside it (`ui::PromptsIn` on the option's widget).
    - The shares come with `SetupGlobalStats`, a Blueprint event the script hook alone sees, once the game has been finished and Global Stats is on.
    - The "do nothing" share is a HUD widget of its own, said after the options.
    - The Timer cue marks a timed choice once, as it is read.
- **The hold.**
    - `UpdateSelectionProgress` and `UpdateTimeoutProgress` are Blueprint overrides that do not call their parent, so they are hooked as script functions; the natives stay hooked beside them.
    - The stage moves its ring by the input and snaps it back to 0 on release (`UBatesDirectionalChoiceStage`: target 0x2d0, current 0x2cc). A hold begins when the ring fills from empty, or refills after dropping more than 0.1 from its peak. Its option's words (with the share) are said as the hold begins (`choice::HeldWords`).
    - The ring is silent: no blips while it fills.
- **The commit** (`ChoiceEnded` LEFT or RIGHT) plays Confirm and cuts speech at both levels (`speech::Cut`).
    - At the detailed level "X, chosen." comes at once (`speech::Now`), and the game's own lines (the status card) after it.
    - Where a keyboard key is still repeating (NVDA would cut at each repeat), the words wait for its release, at most 2 s (`choice::kChosenWait`), while `speech::Hold(L"choice")` keeps the game's lines.
    - A quick-time event up by then drops the words.
- **A timeout** is the Fail cue and "Time's up." queued.
- The trait card ("Status updated") is the game's own feedback after a choice that changes a trait, read by the HUD.

## Prompts

- **The interaction prompt.** `UBatesInteractionWidget` (`WBP_Interaction_C`, pooled) is set up by `Setup`, which calls the Blueprint event `SetupInteractionWidgets`, and sent away by `HideAndDestroy` (a fade, then `RemoveFromParent` after 0.3 s). Its main and secondary key caps carry the actions.
    - A set-up after a send-away is a new question, even in the same words: a "left stick up" after the move hint's own was once never said, and the player waited.
    - A set-up again while it stays is the same question.
    - The prompt's identity is its words and its actions.
    - While the game is paused or a menu page is up, prompts are frozen, so that a hidden prompt is not read again when the pause ends.
- **Hint first.** The first question of an interaction waits up to 0.6 s for a HUD text that comes with it (`prompt::HintFirst`, `hud::LastSaidAt`), which is said first. The game speaks first, the mod's line after: a line the mod adds to a prompt (a way on, a way back, a side) waits for the game's prompt to be said (up to 6 s), never on a shorter timer that a slow prompt can beat.
- **Glint prompts** come from `UBatesGlintWidget`. `bIsShowingPrompt` (0x348) is set exactly while the glint's intensity is 1, which is when the glint authorises the press.
    - The widget is pooled, so it is read only while a spawned glint (`bIsSpawned`) holds it (`ABatesGlint.GlintWidget`), labelled with that glint's thing, after 0.4 s steady, never during the mod's walk.
    - A glint's key is its `GlintAction` where the cap carries none.
- **A move drawn beside the grab key** (79 interaction stages draw [move, grab]) is said with the grab held: "Hold: R2, left stick right" (`prompt::HeldMoveOf`, the game's "action: key" style). This applies only where the stage itself holds the grab (`UBatesInteractionStageBase.bUsingToggleGrab` false, from `bUseToggleGrabForInteractions` and `bStageShouldAlwaysUseHoldGrab`); F8 explains it.
    - A swipe counts once per push past halfway, so the grab must already be held as the stick is pushed.
- **Staged stages** (`UBatesInteractionLinearStaged`: climbs, shimmies, ladders, diaries, phones).
    - The stage writes its step into `UBatesIntSimpleGrabAnimInst.AnimationStage` (0x5B8) of every such anim instance. Each step sounds a `sounds::Tick` at its height over the stage's last step, rising on and falling back (`prompt::LinearProgress`); the stage's first reading is silent.
    - The last step is `MaxAnimationStage`, or, where the stage clamps nothing, the largest integer the item's graph compares `AnimationStage` with inside a math call (`prompt::LastComparedStep`: `EX_CallMath` with the instance variable and an int constant as its two operands, nowhere else).
    - The same prompt drawn again at each step is said once (`prompt::SayAtStep`). A step's own prompt is said when it differs from the one before.
    - "Onward: key" names the way on where the game names none, and a stage past its first step names its way back too (`prompt::NamesWayBack`). Neither is said where the game itself names it.
    - A run of steps on one input (a shimmy) says its prompt once and sounds each step (`prompt::PlaceInRun`); F8 says to hold.
    - A step's prompts wait while the page that step turns to is coming (`prompt::HoldsForPage`, at most 1.5 s).
- **Timed interaction stages** sound the Timer cue once a drawn question asks something (`prompt::TimedCue`). A stage that asks nothing sounds nothing, and its clock is no time's up.
    - Their prompt is urgent.
    - A clock that runs out is judged by the stage's graph leaving a waiting state after `TimeSpentInStage` passes `SecondsUntilTimeout` (`prompt::TimeoutTaken`). Whether the player holds the thing says nothing: a lever is held while deciding.
- **Activity input stages** (`UBatesActivitySimpleInputStage`) that do not wait are input windows (`prompt::WindowOf`).
    - A Moment is a stage that goes on at its end marker, unless its end leads into a stage waiting for the same action. It plays the QTE's direction cue and says the key urgently as it comes up.
    - A Clock is a stage with `bShouldTimeOut`. It plays the Timer cue and says the key urgently.
    - At `HideAndDestroy` comes Confirm if `bInputComplete` or the stage went on by its override outcome, else Fail with "Time's up.". A window whose end names no stage has no verdict.
    - The QTE settings do not apply to these windows.
- **Gestures.** The half circles are custom keys (`MovementHalfCircleClockwise`/`AntiClockwise`/`Axis`: the stick past 0.8, up then right then down, or up then left then down, each step within 0.5 s); on the keyboard the prompt's key does it at once. F8 explains the half circle, and that one made with a key held lets go of what is held if the key is let go.
- **The phone's unlock swipe** (`ABatesInteractionPhoneUnlock`): progress grows by each swipe and drains after 0.12 s without one. A Tick sounds per tenth risen or fallen, and Confirm comes as it unlocks.
- **The game's own move prompt** ("left stick", `UBatesCharacterMovementComponent::MovePromptWidget`) comes 5 s after a hand-over without movement. It is a prompt like any other.

## Quick-time events

- **Widget and stage.** The prompt is `UBatesQTEWidget` (`WBP_QTEPrompt_C`), set up by `SetUpQTEPrompt(InInputAction, InWarmupDuration)`. Its stage is the `UBatesQTEStage` whose `QTEWidget` is that prompt, found at the set-up.
    - The verdict is the stage's `QTEResult` (PDB: NONE 0, SUCCESS 1, FAILURE 2, TIMEOUT 3), with the script hook on `BP_EndQTEPrompt` as its twin.
    - The prompt hangs on a world-tracking layer, so its visibility proves nothing.
    - A result of 0 (a stage that moved on first) is silent.
    - Any other bound QTE button pressed while it runs is a failure.
- **The direction** is the last direction word of the action's name (`IA_QTELeftStickUp` is up). A face button's direction is its place among the four: top Y/Triangle (W), bottom A/Cross (S), left X/Square (A), right B/Circle (D). On the keyboard, the stick events are mouse moves.
- **The timing.**
    - The cue and the key are said at set-up, urgently (`speech::Urgent`).
    - The warm-up (`QTEState` WARMUP before RUNNING; presses are ignored) is quiet, and the opening is cued again only where the warm-up was long enough to need it (`qte::CuesOpening`).
    - The key is repeated every second while the time runs, never while the game pauses the prompt (`BP_SetQTEPaused`).
    - A stage's slow motion stretches its window in real time.
    - While a prompt is up and running in play with no screen over it, the game's own lines are held (`speech::Hold(L"qte")`) and said after the verdict.
- **The QTE lesson sidebar** is shown before the first QTE of a stretch and taken away on its first frame. The asked key interrupts it.
- **The settings' examples.** The settings' QTE rows set up `WBP_QTEPrompt_C` through reflection, inside a `BMUIPage`, with no warm-up and no end.
    - They are silent unless the demonstration key plays them (`qte::Demo`, `DemoRow`: G on the keyboard, the right stick's click on the pad, both free in the game's menus).
    - Moving off the row or changing its value stops the demonstration.
    - An example set up as the settings close is dropped once gone (`qte::Example`, 0.3 s settle).
    - The "pause on timeout" example ends with no verdict and stands drawn: it stays voiced as a paused prompt would (`qte::Stands`).

## Don't Move

- **The version is the game's own decision** (`UBatesGameUserSettings::GetActiveDontMoveType`; `DontMoveType` at +0x500: Passive 0, Active 1, Dynamic 2, the default).
    - Dynamic is passive only when `UBMUISubsystem::GetCurrentInputType()` is the gamepad and `IsGamepadPS()`; otherwise active. Any keyboard key, the mod's own included, can turn the next encounter active.
    - In an active run, `OnAction` asks the version at every input, so touching a PlayStation pad makes the game ignore the stick and the keys.
    - The widget's Blueprint writes `bIsUsingPassiveVersion` after its parent `Setup`, so the mode is read on the next poll. The live version is logged, and F8 explains Dynamic in the game's own setting words.
- **The active version is judged by the marker.**
    - **The zone and the edge:** `TranslationLimit` 22 px; past the edge at `Threshold` 0.8 of it, by `max(|x|,|y|)/TranslationLimit`.
    - **The centring:** inside the limit the game eases the marker home and resets `GraceTimer`.
    - **The grace:** 0.65 s past the edge before the clock runs, +0.05 on keyboard and mouse.
    - **The pool:** every moment past the edge after the grace adds to `TimeOverThreshold`, which is never reset, up to `TimeOverThresholdLimit` from the set-up.
    - **The pulls:** pulses toward a `TargetDislocation` (±44 px), re-picked by the dislocation list's times; only the four straight ways exist.
    - **The input:** it ramps up at 4/s and decays at 2.4/s, at a speed of sensitivity (350 keys, 400 pad) × 0.4 to 2.0. So one brief tap per signal is right; a held press crosses the zone in a tenth of a second.
- **The active version's sounds** (`features/CalmRules`):
    - the double ping every 0.5 s while the marker is inside;
    - two equal notes in one ear: push to that side now;
    - three rising or falling notes: push up or down;
    - a low thud before the signal: already past the edge, the signal pointing back to the middle;
    - the Timer cue as half and three quarters of the pool are gone ("Time is running out.", "Almost out of time." at the detailed level);
    - each new way to push said once in a word (`calm::Way`);
    - "Stay calm!" as the active version begins (`calm::StartWordsOf`), since it draws no words of its own while the passive draws "Don't Move!".

    The sounds are told apart by their note count, and the double ping means "on target, all fine" everywhere in the mod.

- **The passive version is judged only by motion.** Each frame the game compares `|VSize(Rotation) × CurrentGyroSensitivityMultiplier × dt|` with `|PassiveSettleStrength × dt|`; after one start grace (`GraceTimerLimitPassive` 0.5 s) every frame of motion drains the pool. The marker plays no part.
    - The mod sounds the game's own judgement, `CurrentInstability`: a thud every 0.2 s while it is at least 0.5, the double ping every 0.5 s while still.
    - Without a motion source nothing moves, and the encounter succeeds by itself.
- **The verdict** is polled from the widget's `Stage` (2 failed, 3 succeeded) beside the hooks: `UBatesDontMoveStage::ValidateDontMove` calls `TriggerSuccess` from C++, which no reflection hook sees.
- **The lesson** (`WBP_DontMove_Tutorial_C`) sets its texts from the string table in `FadeInTutorialText` and `ToggleReminderText`, read after the game's call. The designer text in the asset is an English placeholder.
