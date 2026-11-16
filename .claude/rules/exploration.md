---
paths:
    - "src/features/{Exploration,Hold,WayList,LookTurn,Rooms,SceneScripts,ExploreText}.*"
---

# Exploration: the hold, the list, ways on, the walk, looks, rooms

## Who has the character (the hold)

- **The hold is the game's own gate for the player's input.** The controller's pawn must be a `BatesCharacter` in the Game state with:
    - its binding state 0 or 3 (`UBatesBindableComponent.BindingState`, PDB 0x160; 1 is a sequence, 2 an interaction; `ABatesCharacter::IsInExploration`);
    - the camera's active layer 1 to 8 (`ABatesPlayerCameraManager::IsExplorationCameraActiveLayer`: the navigation, framing, static and close-up cameras);
    - `IsMoveInputIgnored` false.

    A cutscene played around an unbound character fails on the camera layer.

- **Hard reasons end the hold at once:** no pawn, another state, another binding, the camera's layer, or the picture covered: the loading screen, a full-screen film (any film widget the screen watcher saw, not only the HUD's own), or a fade to black (`ABatesHUD.UIFadeWidget` with `bFadeToBlack`).
- **Soft reasons count only after 1 s:** input ignored, the game's own walk (`UBatesCharacterMovementComponent.AutoLocoState`), a fall (`MovementMode == MOVE_Falling`), a scene waiting after it switched the character. A staircase's own first step (`StartAutoLocoToStartPosition`, `bIsInAutoLoco` of the stairs' record) is exempt.
- **The hand-over comes after 0.5 s of everything.**
    - A level's first moments hand nothing over before its first scene or 3 s (`hold::kLevelSettle`).
    - A pause freezes the hold with no tones, and a hold that stood before a pause stands for 1 s after it while only the camera or a soft reason is in the way (`hold::kPauseReturn`).
- **There are no false entries or exits.**
    - Every hold sounds its tones, ControlGained and ControlLost, whether or not anything is listed.
    - Each logged false entry is a replay in `HoldTests.cpp`, and the decision lives in `features/Hold`.
    - The log names every hand-over, every taking back and every change of reason.

## What is listed

- **The list holds what the character can reach on foot from where she stands without opening a door or climbing,** up to `[Exploration] Range` (200 m) on foot. Doors, windows and climbs reached from this side are listed as the ways out.
    - The list does not change while she walks between open rooms. It changes when she passes a door or a climb, or when the game itself changes what is there.
    - Things are hidden only by the reach map's proof: a complete map of at least 50 cells, not cut by its bound, that reached neither the thing nor a door to it, or reached it only beyond a door (`rooms::Judge`, reach 2 closed off, 3 beyond a door).
    - A map that opens fewer than 50 cells changes no verdict (`rooms::Facts::stuck`).
    - Room volumes never decide what is listed; they only name rooms.
    - A thing proven closed off stays judged so, but its road hides nothing.
- **Use locations** (`ABatesUseLocation`, a character trigger volume) are listed while the game has them on: activity enabled 0x500, sleeping 0x501, enabled 0x502, in progress 0x503 (PDB), plus the rules below.
    - **Usable by this character** (`ways::UsableBy`): `bRequireValidCharacterABP`, then the interaction's bound character classes, or a generic one by gender; then `bCanAnyoneInteract`/`CharacterToInteract`. A photo's two use locations, one per body, are told apart so.
    - **Not at the world's origin** (`ways::Unplaced`: activities' own).
    - **A door only ever tried** (`ways::OnlyTried`: every bound animation `ABP_Door_Locked_*`) opens nothing, and the map counts no way through it.
- **Offered** means in `PlayerInteractionComponent.AvailableUseLocations` and standing in it by the game's live test (`IsInsideTriggerVolume`, `ways::StoodIn`): the game's own press filters that list by `CanCharacterInteract`, and a stale entry stays there after its interaction.
- **Totems** lie in the level like any other thing, and their glint shows only near them. No collectible is hidden until a spot (every scene script and the collectibles' code were checked), so totems are always listed under their own names, with no switch for them.
- **A thing used before has no status of its own:** it is listed, and its road counts, as the game keeps it. The game turns used things off or keeps them examinable itself.
- **No announcement the game does not make**, and no rule of the mod's own that hides a thing the game shows: a list kept "simple" by such a rule hid the way on more than once.
- **Ways on** are the trigger volumes the scene would move on through (below).
    - A way within reach of a listed use location's spot, or on its road, gives way to the thing (`ways::LeftForThing`), but only when she can go to the thing.
    - Twins at one spot are one, and the two sides of a door are one target: the side the map reached first, else the nearer.

## Ways on (the scene's own rules)

- **Enabled is nothing but the actor's collision** (`bActorEnableCollision`; the scripts' `SF_SetEnableTrigger` → `EnableTrigger`/`DisableTrigger` → `SetActorEnableCollision`).
- **A volume answers the character** only with `ActivatorType` 0 (character) and a `TriggerVolumeType` that her binding allows: ExplorationOnly 0 or Both 2 while unbound.
- **Each action fires** only while its `TimesTriggered` is below `TimesCanBeTriggered`. Groups fire when their condition's `OnIsMet` differs from `bNot`.
    - The conditions compare global state through `UBatesGlobalStateBlueprintLibrary::GetGlobalStateControlParameter*`.
    - A chance condition records its roll in `OnIsMet`: never call it.
- **Story actions** move the scene on: StartSequence, StartActivityLoco, StartInteraction, StartStairs, PerformAutoWalk, SendMessage, SetActivityComplete, ExecuteCharacterOrder, PlayDialogue with `bIsEssential`.
    - A sequence counts only when its actor is not a background one (`bIsPAOSequence`) and has `bEnableIntoTransition`.
    - Tutorials, look-ats, held items, audio, cameras, lights, streaming, sleep switches and the like are no story.
    - An activity of the background (`PAO_*`, `bIsBackgroundActivity`) is no way on.
- **The scene scripts are read, not guessed** (`features/SceneScripts`, engine-free, tested on excerpts of the real exports).
    - **Trigger scripts.** `gEvents["EnterTrigger"]["T_x"] = {stateMachine, triggerLimit, delay}` is judged by what its machine reaches from its start, every branch. Only starting calls name story activities (`SF_StartActivity`, `SF_SendMessageToActivity`, `SF_CutToActivity`, `SF_OrderStartActivity`), never `SF_SetEnableActivity`.
        - A trigger is spent by the game's own record: `Event_Start` counts `triggerLimit` down and removes the entry (`scripts::SetOff::Spent` over `lua::Nested("gEvents", "EnterTrigger", name)`), and a failed read keeps the last answer.
        - A comparison on `gVariables` (`SF_Comparison`) is weighed with the game's Lua values (`scripts::StoryNow`).
    - **Polls.** A scene's machine asking every update whether she stands in a volume (`SF_IsInTrigger`) is a way on while it waits there.
        - The asks are seen live by the hook on `BatesGameScriptInterface:IsInTrigger`.
        - The machine's state is read from the Lua state (`lua::Machines`): a poll counts only while its machine stands in that state.
        - A question nested in other conditions is read.
        - A question asked in passing, or a yes that never jumps, is no wait.
        - A wait is met by the game's own yes (`hooks::OnNativeResult`), once the scene stops asking.
        - A wait the scenes held stays a way on until she walks in, the story moves on, or the objective changes (`scripts::HeldWaits`).
    - **What a scene does while she stands in a volume**, turning a thing on included, moves the scene on as its other branches do.
    - **A companion's order** (`SF_OrderLeadTo`/`SF_OrderMoveTo`) counts as moving the scene on only while a scene waits for that companion (`scripts::WeighCompanions`), never in a trigger script.
        - A companion leading the player (her machine standing where every way in ends with `SF_OrderLeadTo` with lead "Player") makes a scene's wait for her the player's way on (`scripts::ParseLeads`, `LeadingNow`).
        - The game's lead styles (`DA_BatesAIStyleData`): Lead_5m waits once the player is 7 m behind and comes back at 10 m.
    - **A poll's way on that reaches `SF_End`** moves the story on where another machine waits for that one to be done (`SF_IsStatemachineDone`) and then runs a machine or a story call.
    - **The scene's goal** (a wait whose way turns the objective off or sets the next, `scripts::EndsObjective`) is never left out, and counts as a listed way.
    - Run `scripts_audit.exe` over every act after a change here.
- **Streaming triggers** (`UBatesStreamingLoadTriggerAction`, LoadOnEntry/HintOnEntry).
    - A streaming trigger the map reaches is a way on only into a section the world has not shown since the act began (`ways::StreamsMore`, `ways::LevelsSeen`), never news.
    - It gives way to any story way listed (`ways::StreamsSuperseded`), inside the judgement, so that neither hides the other.
- **Other ways.**
    - A beam's `ActivationVolume` is a way on (`ways::BeamWay`). A beam crossed (let go of 1.5 m or more from where it took her) is no way on afterwards.
    - A way that fires on entry is left out while she stands in it (`ways::WaitsForEntry`).
    - A way she walks into as it leaves the list has been arrived at (`ways::WalkEndsArrived`).

## The list's order, its settling, its news

- **The row rule:** of the ways on along one road, only the farthest is listed (`ways::RowRule`). The walk there sets the nearer ones off anyway.
    - Only the known roads of listed targets count: never a target proven closed off, never a superseded streaming trigger, never a way that is itself left out.
    - An unknown road keeps a way's verdict.
    - The road from where she stands to a target is the map's own way back to where her path from the map's start parts from it, used only within 6 m (`grid::TreeWay`).
- **Settling** (`ways::Lister`, `Settling`).
    - A map's list settles when the map is done, when every target is judged by it, or at 3 s; the first map of a hold lists no way meanwhile.
    - Things to use are listed and said at once; ways wait for the map.
    - A way not yet listed waits for the map to be done while anything else is listed, and a way once listed keeps its verdict for the hold.
    - After the hold's map is done the verdicts are frozen (`Lister::Freeze`, `ways::FreezeAfter`): only new targets, and ways whose road-giver left or is hidden, are judged again.
    - The story moving on keeps the ways listed (`Lister::StoryMoved`). The ground grown (a world change, a reopened map, the ground re-asked) judges every target afresh (`NewHold`).
- **A hold handed back.**
    - It takes its list up only while she stands on the ground the last map reached (`ways::ResumesList`).
    - A map that reaches what the frozen list held out of reach gives every verdict anew (`ways::Reopened`).
    - A hold begun afresh off the last map's ground drops that map's verdicts (`ways::KeptReach`).
    - A hold resumed after a scene moved her more than 150 cm begins a new map (`ways::MovedFromMap`).
- **News** ("New:", at most two lines and a count) is only what the game turns on: a use location switched on, a verdict turned into a way on (`ways::IsNews`). It never covers what the map merely reaches, what was listed before, or a new turn's first list. News of a way waits with its way.
- **At a new character's turn,** the hand-over's keys and choice wait for the character change card and the objective (`ways::HintWaitsForTurn`, at most 15 s).
    - The hand-over names a choice the list shows (`ways::HandOverChoice`), once the map is done or 4 s old.
    - A neighbour offered at the end of a walk is said only once the offer has stood 1.5 s (`ways::OfferHold`).
- **Distance and bearing.**
    - The distance said is on foot as far as the map knows it (`ways::SpokenDistance`, `MapMeasure`: the map's cost less her own cell's), to where her capsule would first touch the thing (`shapes::Nearest`).
    - Bearings are in the control rotation's frame, the camera's in every logged walk, fixed shots included: "ahead", "behind left" and so on, with "higher up" or "lower down" a storey apart.
    - The beacon and H lead to the road's corner ahead of her, asked afresh every look (`ways::LeadCorner`).
- **An empty list still mapping** says "Looking for targets." (`ways::StillComing`). The map keeps the larger frame share while nothing is listed (`ways::MapHurries`).

## Labels

- **The game draws no words over things:** a glint is an icon, and prompts show keys. A label comes from:
    1. the interaction's name in the level's actor database;
    2. the use location's;
    3. the core names;
    4. the `SequenceToStart`'s words;
    5. the class's words without "Interaction", "Bates", "Use", "Location";
    6. "place".

    The kind words are stripped, so a label reads "Door To Airlock" or "pinuppostcards". These are English internal names, read as they are.

- **Companions and creatures** are named only where the game's speaker names exist ("Chris is in the way"). A creature is never named.

## The walk

- **The walk and the beacon follow one road,** and the walk never refuses for lack of one.
    - A failed search keeps its partial way, then a straight leg.
    - "Could not get there" comes only when she stops getting nearer.
    - The walk stops on the player's movement after 0.7 s, on the walk key, on any screen, mechanic or pause, after 120 s, or with "Target gone" when its own target leaves the list (`ways::WalkGoesOn`). It never follows the mod's next choice.
    - It loses its target when a map after a change of the world finds the thing closed off (`ways::WalkLosesTarget`, `WorldChangedSince`).
- **The walk pushes the game's own input:** the movement keys, or the left stick through the pad share when a pad leads.
    - The game's movement is state-driven (`BP_LocoInputHandler_*`). `Into` starts a walk at an input of 0.8 held 0.16 s, or 0.4 held 1.5 s. `IdlePivot` turns a standing character on the spot toward the largest input of a hold released within 1 s. `OutOf`, just after an interaction, walks on 0.4 held 0.1 s.
    - The pad has a radial dead zone of 0.2, and the game keeps a 0.5 s input buffer.
    - So a turn on the spot is a flick: one frame's full push, let go at the next, the next flick only once the last is 0.75 s gone and she stands (`look::Flicker`). A light held push does nothing, and the model is tested in `LookTurnTests.cpp`.
- **The last stretch** (`PollArrival`) ends only where the game would take the press, or where only a look of the player's is missing (`look::Arrived`).
    - Inside a place is not reached, however near. A thing the game does not let her use from where she ends says "Could not get there".
    - She slides on after a let-go, so the walk lets go where her stride ends at the spot: the slide is measured from the session's own let-goes, on the way's last straight leg (`look::Slide`, `LetGoAt`).
    - Out of the place, it steps toward a cell of the place proven walkable (`look::StepSpot`) and ends only after she comes to rest (`look::Rest`). Toward a spot, only moving counts as progress, not turning.
    - It gives up only once she has settled after its last push (`look::GivesUp`). A last stretch that gives up out of the place backs off 2 m and walks in again, once (`look::BackOffLine`).
- **Companions.** When a stall names someone in the way (within 150 cm and 60° of the way ahead), the walk goes round him once where the ground lets it, keeping clear at her radius + his + 10 cm, then at touching distance (`ways::KeepClear`, `KeepClearances`). Otherwise it ends naming him. A companion in a doorway gets one step back for him (`look::BackOffFrom`).
- **Held paths** (spline-limited locomotion, `UBatesCharacterMovementComponent.LimitedLocomotionState`): the game takes the stick's dot with the path's forward against the side's dot limits.
    - Backwards is impossible, so things behind on the path are not offered, and things beside it are left out (`ways::PathStray`).
    - The path's spline lies where its designer drew it, so it is measured at her feet (`ways::PathAtFeet`).
    - The walk pushes along the path's forward.
- **Stairs** (binding 3) take only the input's component along their rise, so the walk pushes up or down them (`look::StairPush`).
- **A beam** moves her only the way she faces, and pushing back turns her round. The beacon leads along it, and the walk pushes along her facing until the beam lets her go ("On a beam.", "End of the beam.").

## Looks

- **The press is the game's test, mirrored part by part** (`ReadPressTest`):
    - the use location's tick verdict (0x505), which judges the character it cached last (`CachedCharacter`, so another who walked in may hold it);
    - activity enabled, not in progress, not blending (`bIsBlending`);
    - `IsInsideTriggerVolume`;
    - the look.
- **The look.** A use location with `bGlintAuthorizesInteraction` takes the press only while its glint's intensity is 1 (`BP_GlintActor::GetTargetIntensity`). That needs:
    - the overlap;
    - the glint's widget on the screen;
    - her facing within 110° of the billboard;
    - through the exploration camera, the billboard inside 0.75 of the screen's ellipse;
    - a clear line of sight from the view to the billboard (blocked gives −1, said as no glint).

    Use locations without that variable use the native angle test (`IsInLookAtAngleCheck`).

- **A glint exists only while she stands in the glint's own volume** (`ABatesGlint.bIsSpawned`), which is not the use location's. Outside it, the walk steps her in (`look::GlintStepPoint`), never asks for a look. A glint is said missing only when its billboard lies in the picture (`world::InView`) and the game still draws none.
- **The words** "Turn toward it." (her facing) or "Look at it." (the camera) come once she stands, then again only for a change, and each time a walk ends for want of the player's look.
    - The look sound leads the turn: pan by the stick's frame bearing to the glint, over 45° pan, 30° pitch, 90° pace (`look::LookSound`).
    - On a fixed shot the character's turn is led, and nothing is led where the picture leaves the glint out.
    - A framing camera that the camera's movement ends is the player's to turn. One that ends by distance is walked out of (`look::FramingExit`).
    - The thing just used is not led to again until it is chosen or she leaves its place (`look::JustUsed`).
- **The walk key at a reached thing** answers where she stands (`look::WalkKeyAt`): reached, or the look asked for, or a walk.

## Rooms

- **Rooms are the game's reverb volumes** (`LV_reverb` sublevels, `T_Reverb_*`; the broad ones `T_GlobalReverb_*` and the like), keyed by `rooms::Key`.
    - A point's room is the narrowest volume whose collision holds it (`ZoneAt`).
    - Crossing (`rooms::Crossing`): a narrower room takes over at once, leaving needs 30 cm beyond the old room, a broad volume takes over only 100 cm beyond a room, the same name is silent, and a character's first room is silent.
    - F6 says the room first.
    - Acts without a reverb level have no rooms, and the reach map alone judges there.
- **The names** come from the game's own place words in each language (`.claude/rules/lang.md`).
