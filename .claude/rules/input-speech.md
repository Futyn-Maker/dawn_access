---
paths:
    - "src/input/**"
    - "src/hotkeys/**"
    - "src/speech/**"
---

# Input, hotkeys, speech, sounds

## Keys and pads

- **Keyboard hotkeys go through a `WH_KEYBOARD_LL` hook on the mod's own thread.**
    - The hook is installed anew every 5 seconds while none of the mod's keys is down. It then stays the newest hook, ahead of a screen reader that started or restarted after it.
    - It takes the mod's keys only while the game's window is in front (the change is logged), ignores injected events, and counts a held key once.
    - The exploration letters (N, P, H, G, T) and the demonstration key are taken only in their contexts (`hotkeys::AddContext`, `Scope::Context`/`Demo`); otherwise they reach the game.
    - While the game waits for a key to bind on its controls page (`UBatesKeyRebindSettingWidget.bInSelectingKeyState`), every key is the game's, the mod's own too (`hotkeys::TakeOf`). A key the mod took down before the wait keeps its release. The game itself bans only Escape, Tab, Start, Back, RB and the touchpad there.
- **The stop keys are watched, not taken** (`KeyboardStopKeys`, `PadStopKeys` in `hotkeys/HotkeyTable`, each with its menu-only flag): the arrows, Backspace, Escape, Enter, Space; the D-pad, A, B, Start; Q and E and the bumpers while a menu page is up.
    - The press stops what is being said (`speech::StopForKey`) and the game still gets the key.
    - It has to be the hook, and on the pad the reading the game takes, not a poller: the stop must land before the game announces what the key brings up.
    - No stop key is one of the mod's own keys or the chord hold, and a stick is never a stop key: a walk holds one for seconds.
- **Every other input only licenses the next `speech::Focus` to interrupt** (`input::MsSinceInput`): keys, mouse moves, buttons, triggers and sticks alike. `input::InputHeld` says whether anything is still held.
- **Xbox pads** come through the game's static `XINPUT1_3.dll` import. The mod swaps the import slot, so that it reads the pad on the reading the game takes, hides every button pressed while the chord hold (`Gamepad_Special_Left`, Back/View) is down until its release, and pushes the left stick for a walk.
- **PlayStation pads** come through Sony's `libScePad.dll` from the PlayStation PC SDK runtime, which the game loads at run time.
    - Every `scePad*` call goes through an executable stub that calls through a pointer the game fills. The mod swaps both read pointers, `scePadRead` (queued reports, the last one kept as the pad's state) and `scePadReadState` (the fallback), once they are filled and point into the library. The pointers are found at PDB offsets, and the stubs' bytes are checked first.
    - A report is 120 bytes: the buttons in the first word, the sticks in bytes 4 to 7 (y counted from the top), L2 and R2 in bytes 8 and 9, the connection at 76. It is laid over XInput's layout, so the chords, the watched buttons, the walk's push and the activity tracking are one code path for both kinds of pad.
    - The write-back into the game's data only clears bits, never sets them.
    - The touchpad's click is `Gamepad_Special_Left`, so it is the chord hold on such a pad. The runtime's library masks Create and the PS button out of every report and has no mode to keep them, so neither can serve.
    - Options is `Gamepad_Special_Right`.
- **L1 and R1 pressed with the chord hold already up** are kept from the game's next two readings of that pad (`input::ChordGrace`, one per pad in `ChordGraces`): the hold coming meanwhile takes them into the chord. Without that, the last-subtitle chord opened the status page now and then. The grace is kept per pad, by the readings the game itself keeps.
- **The device decides the game's prompts and some of its mechanics:** any keyboard key flips the game's input type to keyboard and mouse, the mod's own hotkeys included. The help is worded for the device of the last input (`input::CurrentScheme`).
- **A key's spoken name is the one the game draws.**
    - An action's keys for the device in use come from the live bindings (`input::KeyForAction`, through Enhanced Input's `QueryKeysMappedToAction`), filtered by the engine's own `Key_IsGamepadKey` (`input::KeysForDevice`). The game registers its touchpad, half-circle and shake keys as a gamepad's, and its mouse moves as a mouse button's.
    - On the keyboard and mouse, the key a prompt draws is the game's own pick from `UBMUISubsystem.ActionMappingCache` (`ProcessedActionMappings`). The rule behind it is mirrored as the fallback (`input::ShownKeyIndex`): under "Mouse/Keyboard Icons" (Standard, Prefer Mouse, Prefer Keyboard), the first mapping, the first mouse button, or the first plain keyboard key.
    - A mouse swipe's cap lights its chord's mouse button (`IA_MouseClickChord`), said "{button} + {move}".
    - A stick cap names a direction only where its picture shows one (`..Stick_Up`/`_Down`/`_Left`/`_Right`; `_All`, `_LeftRight`, `_UpDown` and the half circles name none; `StickDirectionOfCap` in `ui/Widgets.cpp`).
    - A cap set up with no action shows no key, whatever letters it still holds (`prompt::CapShowsKey`).
    - The way of a key on the keyboard (which key moves left, which up) is read from the action's mappings and their modifiers (`input::WayOfKey`, `KeyForActionWay`).
- **Which icons the game draws:** `UBMUISubsystem::ForceGamepadVendorIcons`, set by the controller setting (Automatic, Xbox, PlayStation), comes first, then `GetCurrentGamepadVendor()`. The icon picker (`UBMUIActionIcons::GetBestIconForKey`) reads exactly these. PlayStation names (`key.ps.*`) follow the vendor, never the kind of pad connected.
- **A held key and NVDA.** NVDA stops speech at every key it is given, a held key's auto-repeats included, and the mod's hook passes the game's keys on to it. A line that must be heard (a picture's description, a choice's chosen word) waits while the key last passed on is still down (`keyhook::KeyRepeating`; Windows repeats only the last key pressed), within a bound.

## Speech

- **Prism.** Speech goes through Prism (`prism.dll`, loaded from the mod's `dlls\` by its full path). `speech/Outputs` keeps one thread that owns both backends and every call into the library: a backend is not thread-safe, and the COM proxies behind the Windows backends answer only in the apartment that made them. The rest of the mod queues lines and never waits.
- **Which voice speaks.**
    - The screen reader is the first backend in Prism's registry that is both a reader and running; SAPI and OneCore are not readers.
    - The fallback voice is SAPI, chosen by name, with its voice picked per language by LANGID.
    - SAPI speaks when no reader runs, or with `[Speech] PreferSapi=1` (toggled with F3); the reader still gets the braille while SAPI speaks.
    - Prism never reconnects a backend whose reader has closed, so the mod picks anew on a lost backend and when Prism reports one coming or going. Never probe the readers per line.
- Prism reports every SAPI voice as `en-us`. `speech/SapiVoices` reads each token's own `Attributes\Language` and matches the two lists by name.
- **No reader reports whether it is speaking** (NVDA's backend has no such call). So the reader does the queueing, and where the mod must know what the reader has finished (a page turned), it reckons at 20 characters a second (`speech::rules::kReaderPace`), each line begun once the one before is done.
- **The policies (`speech/Speech.hpp`):**
    - `Focus` interrupts only after fresh input, with the 150 ms same-text dedupe; `focus-after` in the log means it waited.
    - `Announce` is queued: everything the mod says by itself.
    - `Prompt` is a prompt the game draws, queued, and dropped from behind a page turned before the reader got to it.
    - `Now` hard-interrupts for what the player asked for, and is not remembered as the last message.
    - `Urgent` hard-interrupts for what the game gives a moment to answer.
    - `Replace(previous, text)` is a document's page turned.
    - `Hold(owner)`/`Release(owner)` keep the game's own lines while a moment lasts; holders are counted by owner (`rules::Holders`: "qte", "choice"), so one release never ends another's hold. Focus is never kept, and Now, Urgent and Exclusive are said at once.
    - `Exclusive` is the held moment's own words.
    - `Cut(why)` silences speech for something the player did.
    - `Withdraw` is a line that no longer stands.
    - `TakeDropped` says whether a prompt was dropped unheard behind a turned page, so that it is said again once.
- **A page turned** (`speech::rules::Turn`, `TurnOrder`, `KeptAcrossTurn`, `StillOn`, `Dropped`):
    - The new page cuts the old one only while the reader may still be on it; once the reader is past it, the new page waits behind what is being said, so a clue's card is cut neither with the page nor by it.
    - The lines queued behind the old page are each said again once, after the new page, unless they were taken down since.
    - A page the reader cannot be on (a stop or a key's answer came between) waits behind what is said.
- Every line is cleaned once here before the dedupe and the `SAY` line. While the switch is off nothing is said, and a dropped line goes to the log at Verbose.
- **Repeat (F5)** says the last utterance again, and F4 the last subtitle line.

## Sounds

- **Tones are synthesized** (44.1 kHz, 16-bit sine notes, 4 ms attack, 30 ms release, base amplitude 0.5 × `[Sounds] Volume`/100, 80 by default; quieter was lost under the game). They play through one `PlaySound` channel for the whole process, and a new sound cuts off the one playing.
- **The cues tell of something that happened once:**
    - **Confirm:** 660 then 990 Hz, 55 and 75 ms.
    - **Fail:** 660 then 440 Hz, 60 and 110 ms.
    - **Up:** 440, 554 and 659 Hz, 40/40/60 ms. **Down** is the same, falling.
    - **Left and Right:** two notes of 523 Hz, 50 and 80 ms, in one ear.
    - **ControlGained:** a glide from 392 to 587 Hz, 180 ms, at 0.6. **ControlLost** is the same glide falling.
    - **Interrupt:** three taps of 988 Hz.
    - **Timer:** 2093 Hz for 20 ms, a 100 ms rest, then 1568 Hz for 25 ms.
- **The blips come again a moment later,** so they wait while a cue sounds, and are dropped rather than queued:
    - `Tick(level)`: 12 steps, 330 × 4^(i/11) Hz, 30 ms. It carries the phone's unlock slider and the staged steps' height.
    - `Beacon(pan, level, muffled)`: level = 1 − distance/30 m, pan = sin(bearing), an octave lower when behind, every `[Exploration] BeaconIntervalMs`.
    - `Aim(pan, level, locked, muffled)`: the aim and look leads. Locked is the double ping, 1047 then 1568 Hz.
    - `Steer` and `Marker`: Don't Move's runs and marker.
- **`Ping()`, the double ping,** plays at once whatever else sounds. Nothing else sounds for a moment after it, so that it is heard.
- **The vocabulary is fixed, and players learn it.** Two rising notes mean done or won, two falling mean lost or out of time. The double ping means on target, in frame, in the zone or "now": a lit lamp, a trap's moment. A tick-tock is a clock. A rising or falling three-note run is a direction, and a note in one ear a side.
    - A new sound reuses this vocabulary.
    - A mechanic the game gives no sound feedback for stays silent where nothing is to be done: a choice's hold-to-fill is silent until the commit's tone.
    - A new sound is added only with a reason rooted in the game: a picture that tells the player something.
- **The cue words:** every cue that marks an event has its word at the detailed level, said with it (`.claude/rules/mechanics.md`). Continuous guidance has none.
