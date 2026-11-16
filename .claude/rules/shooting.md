---
paths:
    - "src/features/{Shooting,ShootingRules}.*"
---

# Shooting

- **The stage is the authority.** `UBatesShootingStage` runs while `bShootingStageStarted` (+0x450).
    - Every frame the game highlights the target nearest the screen-centre aim within `InitialRadius` pixels, and keeps it while within `SelectedRadius`. The radii are raw viewport pixels: 30/50 for guns, 30/35 for snowballs.
    - A press of the shoot key (`ConfirmShootInput`) takes `HighlightedOption` (+0x278) into `ChosenOption` (+0x280) and ends the stage.
    - A press with nothing highlighted does nothing: no miss, no limit of shots, no projectile, no chance.
- **The double ping plays exactly while the game would take a shot,** that is while `HighlightedOption` is set, never by a geometry of the mod's own. "Fire!" follows at the detailed level after 0.2 s of it, and a blip comes at once when the aim lands on target (`shoot::Cadence`).
- **The stage's members are never reset between plays of a stage** (`ChosenOption`, `HighlightedOption`, `CachedYaw`): take a snapshot at its start. A shooting range replays its rounds after a timeout with the same stage object, and the stale flag of the round before must not count.
- **The clock** is `SecondsInCurrentStage` (+0x90) against `SecondsUntilTimeout`, with `bHasTimedOut` (+0x94), in real seconds; it runs in a pause too. A timed stage plays the Timer cue at its start ("Timer." at the detailed level), and a timeout is the Fail cue with "Time's up.".
- **The widgets record.** `WBP_ShootingTarget` (`Setup`, `SetHighlighted`, `SetTimePercentage`, `Destroy(0 shot / 1 not shot)`) and the reticle (`Show`, `SetIsHoveringAnyTarget`) are hooked to find the stage at once, also while the previous one is closing. The stage is polled as the authority.
- **Targets are named by the designers' names of the things,** because these stages are choices between visible things (an animal or a sack, a bird or nothing): an outcome's `NextStageName` without its verb, or the actor's name. A target is never named by anything the screen does not show; review new names for spoilers. Among several targets, the place ("2 of 3") is said at the detailed level only.
- **The aim sound** leads to the target a shot would take, else the one chosen with N and P, else the nearest.
    - Its pan is the horizontal angle over 12°, its pitch 0.5 + the vertical over 12°.
    - Its interval is 0.12 s on target, else 0.09 + 0.4 × the angle over 25°.
    - It drops an octave when the target is behind.
    - H says where the target is, and T switches the sound (`[Shooting] AimSound`).
- **A DualSense with adaptive triggers on** counts R2 only past the weapon's resistance (`IsTriggerEffectInState`); F8 says so on that pad.
- **The game's Aim Assist** (`bAccessibilityAimAssist`) keeps the aim inside the targets' box and pulls it onto a highlighted target's centre when the stick is let go. It changes no hit radius.
- **Auto-aim:** the walk key (G, X/Square) puts the aim on the chosen target, else on the one the sound leads to, and holds it there until the player aims.
    - It writes the stage's `CachedYaw`/`CachedPitch` (+0x390/+0x394) to the target's own direction less the start's (`shoot::AimOnto`; `InitialTargetWorldPosition` +0x330), within the camera settings' limits, every frame.
    - It lets go when the stage ends, the target goes, or the player works the aim's keys (`input::ActionWorked` on `IA_SequenceAim`, the stick's dead zone 0.1).
    - It never shoots, and says nothing.
    - It is never used in an aim-only stage, a stage whose targets are hidden, a stage that ends on a highlight, or the dilemma below.
- **An aim-only stage** (the rifle scope: `AimOnlyStageCompletionConditions`, done once the yaw passes 20°) has no target to shoot. The aim sound leads it past its own completion by a margin of 3°, with the double ping once it holds, and H names the key and the way.
- **A stage with its targets and reticle hidden that the game resolves by itself** has no verdict and plays no tone (`shoot::Ending`). A stage with no targets at all says nothing (`shoot::Asks`).
- **The dilemma** (`bIsChrisAshleyDilemma`): the aim's place `t` between the stage's `MinYawAngle` and `MaxYawAngle` (its camera settings), with `ChrisAshleyMiddleChoiceSize` the neutral middle, chooses a side.
    - The sides are said in the game's names, with the aim's keys; the aiming's X inversion is followed.
    - The sound is the aim's place (pan 2t−1), with the double ping while a side is highlighted.
    - There is no lead to a side the player has not chosen with N or P.
    - The first turn's commit is confirmed at the detailed level. Doing nothing lets the clock run out, which is said as a timeout.
- **A pause stands the stage still:** nothing is judged, sounded or timed out, and the stage's keys are the game's.
