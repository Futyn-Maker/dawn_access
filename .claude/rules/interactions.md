---
paths:
    - "src/features/{Interactions,NudgeRules,Totems}.*"
---

# Interactions, leads, totems

- **The gaze beacon rule.** Wherever the game waits for the stick to bring a view or an item to a point (a success condition in its code), the mod leads it with the aim blips and confirms the arrival with the double ping.
    - All these leads answer to one switch, the look beacon (`[Exploration] LookSound`), which T or D-pad down toggles while any of them leads: the look a press needs, a totem's premonition, a framing, an item turned until it reveals, a beam.
    - Free inspection with no point to reach is never led: a model in a menu, a totem whose vision was seen, a free-look telescope, an item whose reveal its graph never reads.
    - Find the success condition in the game's code first, never from member names (a laptop that looked like a threshold to reach opens on a single push), and lead in the game's own measure.
- **The bound interaction** is the character's `BindableCharacterComponent.GetBoundActorAsInteraction()`. Its stage is the `BlueprintCreatedComponents` member that is a stage with `Stage == CurrentStage`. Anim-graph states are asked only at real state-machine indices (`prompt::MachineIndices`), since other indices fault.
- **Framings** (camera-nudge interactions: telescopes, the selfie).
    - The interaction writes `CameraYaw` (+1 is full left) and `CameraPitch` (+1 is full up) into its anim instance.
    - The target box is the template's `YawMin`/`YawMax`/`PitchMin`/`PitchMax` (or `MinYaw`...), inclusive. The limits are the live `BatesNudge_CameraModifier`'s `MaxNudgeAngleDegrees` and `NudgeSensitivity`, and the input is live while `InputBlockers` (+0xc78) is 0.
    - The lead heads for the box's middle, with the sound scaled to the nudge's own reach (pan by the share across, pitch by half a share), and names every axis still outside however little: a framing can lie 0.3° off the resting view.
    - The double ping comes the moment the view lands, since the game may move on within a frame.
    - A framing is led only in the stage whose success it is: a later "look away" stage with a box inside the first is not.
    - The view's key is named where the game's question names none (`nudge::SayLookKey`).
    - A selfie's lead stops at the press, and its verdict (well or badly framed) sounds after the shutter. The countdown ring and the phone's timer sound that follow the press are no clock.
- **The examine reveal** (an item turned in the hands until it shows its other side or text).
    - The stage is a `BatesInteractionStageExamine` with `bUsesReveal`, read by PDB (`bIsFullyHolding`, `bIsRampingIK`, `bIsRevealing`, `NormalisedExamineEvaluation`). The reveal is reached at `RevealThreshold`, in the input axis of `InspectionData`.
    - It is led only where the item's anim graph reads `bIsRevealing`: some stubs set `bUsesReveal` and their graphs never read it. The graph is checked by the property's pointer in its functions' bytecode, or by a property-access path naming it.
    - The way to turn is the sign of stick × `InputMultiplier` × `EvaluationMultiplier`, which can turn an item against the stick.
    - The double ping comes at the reveal, and nothing after it. H says the way and how much is left.
- **Totems** (`ABatesInteractionTotem`).
    - **When the game measures:** only while `bIsFullyHolding`, `!bWitnessedVision`, and the time since `PickupTimestamp` is at least `InputStartDelay`. A totem whose vision was witnessed measures nothing and plays no film: the mod says "Already found" and leads nothing.
    - **What the game measures:** it looks along its own line, from the camera's eye to `CachedTotemInspectionQueryLocation`. The premonition's point is the collider point a line from the mesh's centre socket through the `DiscoveryPoint` arrow meets, traced inward from twice the arrow less the centre (`FindUVFromPremonition`). Closeness is 1 − the UV distance over the scale, 0 exactly when the line misses.
    - **The lead** places the point against that line and leads each axis the way the pose was seen to move it (`nudge::TotemLead`, a slope learnt over small pose steps). The scale near the point is the square root of the offset's share, so that a few centimetres are heard. Any non-zero closeness counts as on the totem.
    - **The vision** is said as it begins (`totem.<film>`, `.claude/rules/descriptions.md`), from the game's events or the progress reaching 1.
    - **The menu:** a found totem's page replays the vision through its own player (`PlayVideo` on `CurrentMoviePath`), said the same way.
    - A totem put down stays put down until its interaction is over: the interaction winds down for seconds with its figures still set, and was once taken for a new pick-up.
- **Moments the game times by a global flag** (`TimerReadyForButtonPress` on a boiler's lamp, `MikeCanGrabWendigoTrap` on a trap) sound the double ping on the flag's rising edge, with the word ("Lamp on.", "Now.") at the detailed level. F8 tells how the press is judged: the boiler's lamp is lit 1.9 s of a 7.4 s loop, and a trigger still held from the grab counts at once and fails.
- **The game's own settings of a mechanic are its truth.**
    - **A lever between two people** says its sides in the game's names (`prompt::SidesOf`) once it takes the stick. Its one clock covers the grab and the pull.
    - **A radio** says its dial's position as it turns (the global int `RadioDialPosition`), and names the way to the station once its stage asks for the turn (`prompt::DialOf`).
    - **A hidden input** the game shows no prompt for (the snow globe's shake, `IA_ControllerShake`, through `bUseOptionalInputAction`) is told in F8 only, per device: W or S, the left stick up or down, or the PlayStation pad shaken while the game's motion sensor is on (`input::MotionSensorOn`). Nothing is announced, as the game shows nothing.
- **Writes of an activity's control parameter** (`SetLocalControlParameterValue`) are logged: stick-tested activities that branch with no writer found go on by their triggers.
- **Pictures shown in an interaction** (looks, photos, cards, books) are planned in `features/PictureRules` and said by Interactions (`.claude/rules/descriptions.md`).
