# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Dawn Access (`DawnAccess`, C++ namespace `da`) is a UE4SS C++ mod that makes Until Dawn playable without sight: the 2024 remake for PC, a Ballistic Moon fork of Unreal Engine 5.3.2, project `Bates`, Steam app 2172010. It speaks menus, HUD texts, subtitles, prompts and mechanics through the player's screen reader or through SAPI, both reached with Prism. It plays sounds where the game relies on the picture: quick-time event directions, Don't Move, the aim, the exploration beacon, the leads that bring a view or an item to a point. It also describes the pictures and films the game shows without words. Windows only, C++23, MSVC. `README.md` is the only user documentation: features, installation, controls, building, translation. This file and `.claude/rules/` are for developers; the rules for each area load when you open its files.

The mod is tested by playing it with a screen reader. Evidence comes from the game's data and code and from the mod's logs, screen dumps and ground records of a play session, never from a look at the screen.

## Rules

- **Equal access.** Say what a sighted player sees or hears, nothing more and nothing less.
    - Never say data the screen does not show: names the game hides, a clue's details before the game shows them, a trigger whose name would spoil a scare.
    - Never withhold or fold what is shown to cut chatter: a clock the game draws ticking is read as it ticks.
    - Pictures and films that carry information without words are described (`.claude/rules/descriptions.md`).
    - The mod adds no announcement the game does not make (no "only forward here", no warning of a scare). Its own words are the ones that go with its sounds, its exploration list and its answers to the player's keys.
- **Mirror the game's own decision.** Before the mod says what the game shows, find the code or the state the game itself decides by, and read that same member or call that same function. Never use a proxy that usually agrees with it. The key names follow the icon set the game's controller setting forces before they follow the pad that is connected, because that is what the game's icon picker reads.
    - Prove how the game works from its data, its code or a log before coding it or claiming it.
    - Guessing is: taking a flag for what its name suggests; reading a cause out of a fade, an opacity or a delay; keying a feature by a name not read from the level; keeping the first explanation that fits when a second one fits too.
    - The way out is never expensive. Read the PDB, the disassembly, the Blueprint bytecode or the asset's JSON, or log the deciding values and get one session.
    - Write the evidence (function, RVA, offset) into the code comment, so that the next change does not guess again.
    - A cleanup path that ends a mechanic is behaviour too: it reads the game's own state, never a visibility or timing proxy, and never shares the verdict's flag with a guess.
- **A reading is proven only by a log line from the very state it exists for.** A door still shut, a picture before its first page, a prompt before its first press. A neighbouring state proves nothing: a door's leaf was once "seen working" on a door already opened and was dead for every shut one. A reading that comes back empty while the game's data names something logs an ERROR, never an info line that reads like an ordinary outcome.
- **Read the game's state, then check it against what the screen shows.** Colour, opacity and animation are how the game draws a state, not the state: while they play they say nothing, and when they finish they keep saying it. Where the game's state and the picture disagree, the reading is wrong even if it sounds right.
- **Fix where the knowledge is.** A rule about one screen or one mechanic belongs to its reading, not to a primitive everything shares. Changing a shared function means justifying it for every caller.
- **Only the player's own input interrupts speech, with three exceptions the game's timing forces.**
    - The keys that move a selection, leave a screen, select, skip or use stop the speech the moment they go down, before the game acts on them: the arrows, Backspace, Escape, Enter and Space; the D-pad, A, B and Start; the tab keys (Q and E, LB and RB) while a menu page is up.
    - Every other key, button or stick only lets the next message interrupt: `speech::Focus` interrupts only after a press, `speech::Now` answers a request, and a stick never stops anything by itself. Everything the mod says on its own is `speech::Announce`.
    - The three exceptions:
        - What the game gives the player a moment to answer is `speech::Urgent`: a quick-time event's key, an input window's key, a timed prompt.
        - A choice's side taken cuts speech (`speech::Cut`).
        - A document's page turned cuts the page that is still being read (`speech::Replace`).
    - While a quick-time event's prompt is up, what the game says by itself is held (`speech::Hold`/`Release`, with owners) and said after the verdict.
    - Subtitles are spoken verbatim, never shortened, paced or queued by the mod.
- **The mod has one switch, `core/Gate`** (F1; the chord with Start). When it is off:
    - `speech::` says nothing and `sounds::` plays nothing;
    - the keys taken only in play, and the walk, go back to the game;
    - the watchers run on.

    Switched on again, it reads the screen as F6 does. The speech and sound modules ask the switch, so a feature never does, and the switch is never saved.

- Every spoken line is cleaned once, in `speech::` (markup stripped, whitespace collapsed, trimmed), before the dedupe and the log: a reader may clean the text it compares, and none has to clean what it speaks.
- **The verbosity level is about the mod's own words and never the game's.** Everything the game shows or prompts is read at either level, its states included (a control the game has switched off is "unavailable" at both).
    - Brief drops only what the mod adds of itself:
        - where a selection stands in a list, and the kind of a control;
        - which option a choice was settled on;
        - whether a mechanic was passed;
        - "reached" at the end of a walk;
        - the words that double a sound the player already hears;
        - the particulars of the greeting.
    - Each of those asks `cfg::Detailed()`; the sounds play at either level.
    - At the detailed level, every cue that marks an event comes with its word: the Timer cue with "Timer.", an outcome with "Success.", "Failure.", "Time's up." or "Hit.", a moment with what the picture shows ("Lamp on.", "Now."). A cue added without its word, in all 20 tables, is a defect; audit every `sounds::Play` and `sounds::Ping` site. Continuous guidance (blips, beacons, steering runs) has no word.
- **A clock that runs out always sounds as a timeout:** the Fail cue and "Time's up.", also where doing nothing is an answer of its own. The mod cannot know whether a refusal to act was good.
- Every new mod string goes into all 20 tables under `mod/lang/` at once, in natural wording for each language. "Dawn Access" is never translated.
- No phases, plans, TODOs or "not yet" in code comments, strings or docs: every version reads as complete.
- **`README.md` is about how to play:** what a player can do, and what a key, a sound or a word means, in a sentence or two per behaviour, for the keyboard and mouse and for the controller.
    - It never says how the mod works a thing out: no rules of thumb, no internal names, no cases the mod handles.
    - The test for a sentence is whether a player would play differently for knowing it. A sound kept quiet where it would mean nothing, a thing kept off a list for a reason of the mod's own, a screen no longer read once it has closed: none of these passes, so such a change leaves the README as it was.
    - The comments of `mod/DawnAccess.ini`, which players read too, follow the same rule.
    - The README's feature list names the areas the mod covers. A behaviour gets its sentence in its area's section, not a line of its own.
    - It writes nothing that does not occur in the game, even where the code would handle it.
- **A key the mod takes is declared once, in the table of `hotkeys/HotkeyTable.cpp`:** its command, the names of its settings, its defaults on the keyboard, under the chord and on the pad, and its scope (`Always`, `Context`, `Demo`, `Dev`, `Switch`).
    - The settings reader, the bindings, the help and the tests all read that table. A feature asks `hotkeys::KeyOf`, `SpokenKey` and the like for the key a command is on, instead of keeping a setting of its own.
    - What the player reads is still written by hand: the key's line with a comment in `mod/DawnAccess.ini`, its row in the README's tables, and its help line. `scripts\test.cmd` fails when the ini or the README lacks a key of the table.
- **Decisions live in engine-free modules, and every bug gets a test that replays it** (`.claude/rules/tests.md`).
    - A rule still inside a feature is moved out to an engine-free module (the `*Rules` modules, `Hold`, `WayList`, `GridPath`, `LookTurn`, `Rooms`, `Shapes`, `SceneScripts`, `WorldStream`, `GroundRecheck`, ...) before it is fixed.
    - The fix carries its test in the same commit. The test replays the logged timeline or a recorded ground cut into a fixture, and is proven by putting the old code back and seeing it fail (`tools/tests/mutate.py`).
    - When a fix adds a rule beside another, the choice between them is tested too, with the very state the new rule exists for: two rules each tested alone still fail when the code that picks one is untested.
    - A rule's test is fed what the engine side really hands it (a dump, the asset), not assumed inputs.
- A problem found in a log is fixed, with its test, or proven to be the game's own behaviour from its data. It is never handed over as a known limit or "to watch". Logging more is never the fix. A spot in the game is hard to reach again, so fix it from the game's data and scripts, and leave a way back to it: the checkpoint copies of the save, or `tools/saves/restart_act.py`.
- Every sizeable new mechanism gets an independent review before it is handed over: a reviewer given the code and the facts, not the author's conclusions.
- When the game lacks what a feature would lean on (a navigation mesh, a reflected member), build the replacement properly. Never ship a fallback that walks blind or guesses "until the real thing is seen".
- **Files and keys.**
    - Files are written with LF endings. Python on Windows writes CRLF in text mode, so a script that rewrites a repository file opens it in binary mode or with `newline="\n"`. `.cmd`, `.bat` and `.ps1` files keep CRLF (`.gitattributes`).
    - Never bind plain F10 (the window menu freezes the game until Escape), F11 (the game's full-screen toggle) or F12 (the Steam screenshot key).
    - Before theorizing about a key, read the game's mapping contexts, `MC_Gameplay` and `MC_Menu`, in the extracted assets.
- **The running game.**
    - Never deploy or launch while `Bates-Win64-Shipping` runs, and close the game after every self-test (`scripts\stop-game.ps1`).
    - A scripted session only navigates and dumps. It never presses Enter on a settings row, never changes a value, and never starts or continues a story: the game asks to confirm changed settings, and a script that keeps sending keys answers for the player.
    - Never touch the saves while the game runs; back them up first (`scripts\save-backup.ps1`).
- Tools run with their command-line arguments only. UAssetGUI or FModel started bare opens a window and the command never returns. RAD Video Tools' converter shows a window even on the command line, so start such a tool hidden and with a time limit.
- **Commits.** Each finished item is committed on its own, and a correction in a commit of its own. The subject is imperative, in the style of `git log`, with no attribution lines.
    - Each commit builds and passes the tests alone. To check that, use a separate worktree with every touched file's time refreshed, since stale times let Ninja skip files.
    - Do not push.
    - Every commit carries the documentation its change needs, without being asked:
        - `README.md` when players see a difference;
        - `.claude/rules/` and this file when a developer would need the lesson;
        - the `mod/DawnAccess.ini` comments for a new or changed setting.
- The repository's code, comments, docs and commits stand on their own and cite no other project.

## Design Reference

The mod is held against the [Graph A11y Kernel specification](https://github.com/alex19EP/WH40KRTAccess/blob/main/docs/graph-a11y-spec.md), an engine-neutral account of how a screen-reader layer inside a game is built. Its axioms are this mod's rules in other words: the game's state over its picture, parity, interruption by provenance, one vocabulary, polling as the authority with construction callbacks that only record, fault isolation, and the log as the evidence. Where the mod departs from it, it does so on purpose, and these departures are kept:

- **A1, the mod-owned navigation tree.** The mod reads the game's own focus and leaves the arrows to the game.
    - Until Dawn's interface (BMUI) is built for a gamepad: every screen is driven by the D-pad with a focus the game keeps in its page reflection system (the active tree's leaf page and its cached focus widget). Reading that focus is parity.
    - Its lists are short and its keys mean the same on every screen.
    - Text the focus never visits is read on arrival and by F6. The dialogue history's lines, which the game lays out in one scroll box with nothing to focus, are stepped through with the mod's own keys.
    - Exploration and shooting are separate layers, gated on no screen being up.
- **A7 by timing.** The mod cannot see whether the game or the player moved the focus. So `speech::Focus` interrupts when the player's input is fresh: within 120 ms, or since the current line began. The stop keys silence speech as they go down, which goes beyond what the specification asks.
- **Interruption beyond provenance.** The specification lets only the player's input interrupt; this game's timing does not allow that everywhere:
    - a quick-time event's key and an input window's key come urgently, and the game's own lines wait behind the event;
    - a choice's commit cuts speech;
    - a turned page cuts the page still being read.

    Each is in the Rules above, and none may spread further without the same kind of reason.

- **A4 with a backstop.** Focus is spoken from one listener, with a 150 ms dedupe of the same text behind it (`[Speech] FocusDedupeMs`), since the game's focus can report one landing twice.
- **§7.9, faults.** A memory fault in a poller or a hook is swallowed and logged, one ERROR per five seconds per site (`SafeInvokeLogged`), rather than left to the crash reporter. The game stays up and the log says where.
- **P8, settings.** Two verbosity levels and an ini file, not per-control toggles.
- **P6, sound.** The mod's tones play one at a time and the blips yield to the cues. The game's own interface sounds come free of using its focus.

## Commands

Run from the repository root, in cmd or PowerShell. The repository path must be at most 78 characters (UE4SS's sources and build reach 181 characters below it).

- `scripts\build.cmd [configure]`: builds `main.dll`, `UE4SS.dll` and `dwmapi.dll` with the CMake preset `shipping` (Ninja, `Game__Shipping__Win64`).
    - It fetches the submodules first when they are missing (`scripts\setup.cmd`), and the Prism release that `third_party\prism.version` pins when `third_party\prism` holds another.
    - `DA_UE4SS_SOURCE_DIR` builds another checkout of the pinned UE4SS commit.
    - Redirect its output to a file: a pipe into a command that stops reading early (`Select-Object -First`) cuts the build short.
- `scripts\package.cmd`: runs `check-lang.ps1` and `test.cmd`, builds, then lays out the release (`powershell -ExecutionPolicy Bypass -File scripts\package.ps1 -SkipChecks` leaves the checks out).
    - `dist\DawnAccess-<VERSION>\`: its contents go into the game folder.
    - `dist\dawn-access-windows.zip`: the release archive, always under this name. Its entries begin at `Windows/`, and the script reads it back against the folder.
    - `dist\DawnAccess-<VERSION>-symbols\`: the PDBs and `main.map`, also kept as `build\symbols\<timestamp>`.

    It warns when the tree has uncommitted changes.

- `scripts\deploy.ps1 [-GameDir <dir>] [-ResetConfig]`: installs the last build into the game, replacing program files and keeping settings files. It keeps the build's linker map as `build\maps\main-<timestamp>.map` for crash dumps.
- `scripts\run-game.ps1 [-GameDir] [-TimeoutSec 180] [-AutoDismissCrash]`: launches the game (through Steam for a Steam copy) and waits for `[DawnAccess] ready`. `scripts\stop-game.ps1` closes it.
- `scripts\tail-log.ps1 [-All] [-Last N]`: the SAY lines and errors of the mod log.
- `scripts\check-lang.ps1`: every table in `mod\lang` and in each folder under it has exactly the keys of its `en_US.ini`.
- `scripts\test.cmd`: builds the tests of the engine-free modules (`tests\`, doctest, fetched on the first run) into `build\tests` and runs them; it ends with `TESTS_OK` or `TESTS_FAILED`.
    - Run some tests from a build that `test.cmd` made: `build\tests\tests.exe -tc="*part of a test case's name*"` or `-sf="*QteRulesTests*"` (one file); `-ltc` lists the cases.
- `scripts\format.cmd [check]`: clang-format over `src\` and `tests\` (the LLVM tools of Visual Studio).
- `python -I tools/tests/mutate.py <spec.json>`: puts known faults back one at a time, runs the tests and restores each file. Keep the specs in `research\tools\`.
- `scripts\save-backup.ps1 [-Name]`, `scripts\save-restore.ps1 [-Backup <name>] [-Checkpoint <name>] [-Settings]`: back up the game's saves and settings into `research\saves`, or put a backup or one of the mod's checkpoint copies back. Both refuse while the game runs.
- `python -I tools/exe/crash.py collect`: right after a crash, keeps every new dump with its logs and the matching linker map under `research\crashes`, and reads it. `crash.py read <dmp>` reads one.

The game folder (the one holding `Windows\Bates.exe`) comes from `-GameDir`, then `DA_GAME_DIR`, then Steam's library records. UE4SS lives in `Windows\Bates\Binaries\Win64\ue4ss`, the mod in `ue4ss\Mods\DawnAccess`. The version is the first line of `VERSION` and is written nowhere else.

## Testing and Diagnosis

- **Self-test:**
    1. `scripts\test.cmd`;
    2. `deploy.ps1`;
    3. `run-game.ps1 -AutoDismissCrash`;
    4. in the last session of the mod log, check for the greeting's `SAY`, `feature <name> installed` for each feature, `ready`, and no `ERROR`;
    5. `stop-game.ps1`.

    When handing a change over, say what to try in the game and which log lines will show it. Keep every reading not yet seen working on a list, each with the exact log line to look for, and check it in the next log.

- **The mod log** is `<mod dir>\DawnAccess.log`, appended per launch (`---- DawnAccess log opened ----`), with one `.old` beside it once it passes 32 MB.
    - Every utterance is `SAY <policy> "<text>"` with a timestamp: `focus`, `focus-after`, `announce`, `prompt`, `now`, `urgent`, `exclusive` or `repeat`.
    - Features log what they read and decide, at the level `[General] LogLevel` sets (Ctrl+F11 and the chord with LT cycle it).
    - `UE4SS.log` beside `UE4SS.dll` holds every mod line of the last launch, whatever the rotation; it is overwritten at the next one. Copy both logs aside right after a session.
- **After a session:**
    - Read the log causally from the timestamps before theorizing.
    - Grep it for `unread:` (the audit's texts drawn for 4 s that no utterance said), `hud: ... first shown` (HUD widgets met for the first time) and `ERROR`.
    - Read the screen dumps (Ctrl+F9; the chord with LB): `<mod dir>\dumps\screen-*.txt`, every drawn widget tree with texts, visibility, opacity and attachment, then the current screen and focus. The dump walks deeper than any reading does.
    - `[Diag] GroundRecords=1` records the ground the mod feels out in `dumps\ground-<time>.tsv`, the newest 20 kept. These are the walk's evidence and the source of replay fixtures.
    - `[Diag] SaveCopies=1` keeps a copy of the save at every checkpoint in `dumps\saves`, so that a spot can be played again.
- **The command file:** lines written to `<mod dir>\command.txt` run in the game, polled twice a second and then deleted:
    - `dump`, `read`, `help`, `onoff`;
    - `trace on|off` logs Blueprint calls of the classes in `[Diag] TraceClassPrefixes`;
    - `loglevel Error|Info|Verbose|Trace`;
    - `say <text>`; `note <text>` (written to the log only);
    - `key <action>` (an input action's keys and where they were read from); `walkkeys`;
    - `props <object path or class name>` (every property of that object, or of the class's first live instance);
    - `objects` and `headers` (UE4SS's object dump and C++ header dump into `dumps\`);
    - `focus` (the focus sources side by side); `explore` (everything exploration reads).
- **Crashes:** the crashing thread decides which handler writes the dump.
    - **UE4SS's crash dumper** (`[CrashDump] EnableDumping = 1`) writes `crash_<date>.dmp` beside `UE4SS.dll` and names it in a "Fatal Error!" box. It catches faults on the mod's own threads and in UE4SS's event loop.
    - **The game's crash reporter** (Sentry's crashpad) writes `Windows\Bates\Binaries\Win64\.crashdata\reports\<id>.dmp` for faults on the game's threads. No window comes up.
    - A fault inside a guarded poller is no crash, only an `ERROR` in the log.
    - `tools/exe/crash.py` names the game's frames by the PDB, and the mod's by the linker map of the very build that ran: a map is used only when its timestamp equals the one the dump records for `main.dll`.
    - Crashpad's `crs-video.exe` fails at every exit of the game, so the Windows event log proves nothing about a crash.

## Architecture

- `src/dllmain.cpp` is the `CppUserModBase`. `on_unreal_init`:
    1. reads `DawnAccess.ini` and opens the log;
    2. starts speech (`prism.dll` beside `main.dll`, loaded by its full path) and the language tables;
    3. installs the game-thread pump, the input activity tracker, the hooks, the freed-object listener, the watchers, the narration logger, the game state, the hotkeys and the diagnostics (command file, frame timer, first-launch reset, unread-text audit, checkpoint copies);
    4. registers the features.

    A startup poller waits for the game's language, greets, and then installs the features (`features::InstallAll`). The language is followed for the session. The mod folder is the parent of `dlls\`.

- **Threads:** UE4SS calls `on_update` and its key events on its own thread. UObjects are touched on the game thread only: in the engine-tick pump (`core/GameThread`: `Post`, `AddPoller`) or in hook callbacks. Object-construction callbacks may come from loading threads and only record. The keyboard hook runs on its own thread, and the speech backends on the speech thread.
- **Detection is hooks plus polling, and polling is the authority.** `hooks/HookDispatcher` hooks in four ways:
    - `OnScript`/`OnScriptPre` route the Blueprint function executions that UE4SS's ProcessLocalScriptFunction callback sees, by declaring class (subclasses match) and name;
    - `OnNative` hooks a native UFunction by full name, and fires only when the game calls it through reflection;
    - `OnNativeResult` hands the post handler the function's return value;
    - construction listeners are filtered by class.

    A C++ call between natives reaches no hook, so every hook has a polling twin and both are deduped.

- **The core modules:**
    - `core/ObjectUtil` (`obj::`) is name-based reflection over UE4SS: properties, struct members, arrays, soft references, `ProcessEvent` calls (`Call`, `CallForBool`), widget trees, visibility (`IsWidgetShown`, `IsLayerWidget`), liveness (`IsLive`, `IsSame`, `KeyOf`) and SEH guards (`SafeInvoke`, `SafeInvokeLogged`).
    - `core/Symbols` (`sym::`) holds the places reflection does not reach: functions, globals and unreflected members by name, with their RVAs and offsets in a table per build of the game the mod knows. A table is handed out only while the running executable names that build's PDB (its GUID and age, `sym::RunningBuild`). On a build no table names, every feature falls back or stands down, the log says so, and the greeting tells the player (`core/Greeting`).
    - `core/AddressCache` keys every cache by address and identity (name and outer), and `core/Freed` forgets objects as the engine deletes them.
    - `core/GameLua` reads the scene scripts' Lua state (`lua::Global`, `Nested`, `Machines`) between frames, under the script's lock.
    - `core/LevelNames` gives an actor's core name, its name in the level's actor database, and its placing level.
- **Readers:** `watch/` holds the class watcher (shown widgets sorted by native class), the current screen (`watch::CurrentScreen`), the game state (`EBatesGameState`, `gamestate::`), and the narration logger, which reads the game's own text-to-speech queue upstream of its language gate. `ui/Widgets` says what a control is (kind, label, value, state, place, heading, tip), and what a screen's title, body and prompts are; `ui/Describe` speaks a description.
- **`features/*` hold one area each.** `Install()` sets hooks and pollers, `Describe()` feeds F6, `Help()` feeds F8, and `[Features]` in the ini can switch each off:
    - Menus;
    - Subtitles;
    - Hud (texts, cards, sidebars, the objective, the loading bar, documents);
    - Prompts (interaction and glint prompts, the action bar, timed stages, input windows, staged steps);
    - Choices;
    - Qte;
    - DontMove;
    - Exploration (the hold, the list, ways on, rooms, the walk, the beacon, the look lead);
    - Totems;
    - Shooting;
    - Interactions (framings, the examine reveal, the boiler, the radio, the lever, picture plans);
    - Credits;
    - Films;
    - Achievements.

    Their decisions live in the engine-free modules beside them (`*Rules`, `Hold`, `WayList`, `GridPath`, `GroundFile`, `LookTurn`, `Rooms`, `Shapes`, `SceneScripts`, `WorldStream`, `GroundRecheck`, `ExploreText`). `WorldRead` and `Vec` hold the world's readers and geometry.

- **Speech, input and the rest:**
    - `speech/` holds the policies (`Speech`) and their engine-free rules (`SpeechRules`), the speech thread that owns the two voices (`Outputs`), the binding to `prism.dll` (`PrismBridge`), the SAPI voices' languages (`SapiVoices`), and the tones (`Sounds`).
    - `locale/` holds the mod's strings (`locale::Mod(key, args)` with an English fallback), and the game's own strings and language (`GameText`, from its string tables).
    - `input/` holds key names and the game's key choice (`InputNames`, `KeyChoice`), the pads shared with the game, and input activity.
    - `hotkeys/` holds the table, the spellings (`KeySpec`), the keyboard hook (`KeyHook`) and the bindings (`Hotkeys`).
    - `diag/` holds dumps, the help, the command file, the unread-text audit and the checkpoint copies.
- **Around the source:**
    - `mod/` ships beside `main.dll`: `DawnAccess.ini` (the defaults with their comments), `enabled.txt`, `lang/` and the description tables under it (`totems/`, `films/`, `pictures/`, `rooms/`).
    - `ue4ss/UE4SS-settings.ini` is UE4SS's configuration for this game. The pinned UE4SS finds every engine address by its own scans, with no custom game config.
    - UE4SS comes from the submodule `third_party/RE-UE4SS`, pinned. Prism is not in the repository: `scripts\fetch-prism.ps1` downloads the release that `third_party/prism.version` pins.
    - `tools/` holds the research tools worth keeping (`.claude/rules/tools.md`); `research/` (git-ignored) holds extracted data, sessions, fixtures in the making and save backups.

## Game Facts That Shape Everything

- **Steam's build ships its full PDB** (`Bates-Win64-Shipping.pdb`, 1.6 GB). Functions, vtables, enums and native layouts are all named, so natives are found by symbol, never by signature guessing. Every RVA, and every offset of a member reflection does not list, comes from `core/Symbols`' table for the running build; only the engine's own containers (a `TMap`'s hash and sparse array) are read by their layout.
    - Steam's table, read from that PDB, is the reference.
    - Another build (a game update, a copy from elsewhere) gets a table of its own: from its PDB where it ships one, else by matching its code against the reference, every place and member checked (`.claude/rules/core.md`).
- **BMUI pools its widgets** (`UBMUIUserWidgetPool`). Glints, interaction prompts, QTE prompts, choice options, credit lines and menu rows are handed out again for other content, with the same address and name. Identity is never the pointer:
    - a menu row is its page, its place and its rendered text;
    - a cached widget is keyed by `obj::ObjectKey` and checked against still being the one drawn;
    - a released widget keeps its flags and must be asked of the pool (`ui::LayerWidgetDrawn`).
- **Many widgets hang in no widget tree.** World-anchored ones (glints, the prompt on the character's wrist, QTE prompts, choice options) sit on `UBMUIWorldTrackingCanvas` layers, and `obj::IsWidgetShown` is false for them; use `obj::IsLayerWidget` and `ui::LayerWidgetDrawn`.
- **Hooks and native calls.**
    - A `BlueprintNativeEvent` override reaches the native only if it calls its parent (`EX_FinalFunction` of the same name in the override's bytecode). Hook the script function where it does not, and read the bytecode before trusting a hook.
    - A Blueprint writes some of its members after its parent call. A post-hook of the native then sees the old value, so such values are read on the next poll.
- **Addresses are reused.** A freed UFunction's address came back as another Blueprint's function, and a verdict hook answered the wrong function. Every cache keyed by an address also keys by identity (`core/AddressCache`) and is pruned by the engine's delete listener (`core/Freed`).
- **Soft references** (`TSoftObjectPtr`, 40 bytes) have their weak half filled only once something looks the object up. Read one by its path when the weak half is empty (`obj::SoftObjectAt`).
- **The pads, keys and icons.**
    - The game reads Xbox pads through its static `XINPUT1_3.dll` import, and PlayStation pads through Sony's `libScePad.dll`, loaded at run time. Each `scePad*` call goes through an executable stub that calls through a pointer the game fills.
    - The mod swaps the import slot and both read pointers (`scePadRead`, `scePadReadState`, at PDB offsets checked by the stub's bytes), so that what it reads, hides and pushes is what the game sees.
    - On a PlayStation pad the chord hold (`Gamepad_Special_Left`) is the touchpad's click: the runtime's library masks Create and the PS button out.
    - Any keyboard key, the mod's own hotkeys included, flips the game's input type to keyboard and mouse, and the game's input-dependent decisions follow it.
    - Prompts draw keys by the controller setting's forced icon vendor, then the connected pad. On the keyboard they draw the key the game's own action-mapping cache picks under the Mouse/Keyboard Icons setting, which follows the player's remapping.
- **Scenes are BMLua scripts, not reflected state machines.**
    - Each act's scripts are SMED state machines of `SF_*` calls (`LuaScriptAsset.Script`, readable at run time), plus trigger and activity event scripts (`gEvents`).
    - A scene asks whether a character stands in a volume through `UBatesGameScriptInterface::IsInTrigger`, which the mod hooks.
    - The Lua state holds `gVariables`, `gEvents` and every machine's current state.
    - What happens next is read from the scripts and the Lua state, never inferred from names (`.claude/rules/exploration.md`).
- **The world.**
    - Each act is its own world, streamed by sections as the character walks into the game's streaming triggers, and a section's levels come and go under her.
    - The game has no navigation mesh for the player's walk: most levels have none, and where one is cooked it covers only the companions' corridors. So the mod feels out the ground with the character's own capsule and collision, everywhere (`.claude/rules/ground.md`).
    - The game's state is `EBatesGameState`, read from `ABatesGameState` at every poll that needs it.
    - The Shipping build writes no engine log.
- **The game's built-in screen reader** (BMTTS, Flite) speaks English only, and in menus only. Its queued lines are a text source (the page names, the labels of wordless controls, locked items), read upstream of its language gate. The mod never lets it speak.
- **NVDA** stops speech at every key it is given, a held key's auto-repeats included, and cannot report whether it is speaking. The mod passes the game's keys on to it, so a line meant to be heard waits for the player's held key to be let go (`keyhook::KeyRepeating`). The mod reckons what the reader can have finished at 20 characters a second (`speech::rules::kReaderPace`).
- **Achievements:** the game draws nothing when one unlocks. Steam's flat API is polled, and an unlock is said in the game's language from the game's own trophy strings: Until Dawn declares English as its only base language to Steam, so Steam's API answers in English.

## Researching the Game

Only the installed game is needed. Keep extracted files, dumps, sessions and save backups in `research\` (git-ignored); the tools that stay useful live in `tools\` (`.claude/rules/tools.md`).

- **Files:** `Windows\Bates\Content\Paks\Bates-Windows.utoc/.ucas` (IoStore, Oodle, unencrypted) and `Bates-Windows.pak` (Wwise banks, the 20 `Game.locres`, `Config\*.ini`). 274 loose Bink films are in `Windows\Bates\Content\Movies`; `_ja` names a censored cut, and `_no_text` a cut without burned-in text.
- **Assets** are unversioned UE5 packages, so they need the mappings (`.usmap`) dumped from the running game by `tools/assets/USMapDump`, a UE4SS Lua mod enabled for one launch only.
    - `tools/assets/extract.py` pulls packages to legacy files with retoc, by a path substring.
    - `tools/assets/tojson.py` converts them with UAssetGUI. The mappings must be registered by name: a path on the command line is silently ignored, and every export comes out raw.
    - A level converted inside retoc's output tree comes out raw too; convert a flat copy.
- **Code:** natives by the PDB (`tools/exe/pdb_symbols.py`, `dasm.py`, `callers.py`); another build's places by `tools/exe/port_build.py`; Blueprint logic by its bytecode in the asset JSON (`tools/assets/bytecode.py`); live classes and members by the `objects` and `headers` commands.
- **Strings:** `tools/text/locres.py` dumps every string table by key. The game's own words come from its string table `ST_Localized` at run time (`gametext::Resolve`).
- **Levels and scripts:** `tools/levels/` lists triggers, their collision, shapes and rooms, dumps the Lua state machines and the `SF_` calls, and reads an activity's stage graph. The scripts of every act stay loaded in the game; the level's own group is `ULuaScriptUpdater.CurrentLevelScript`.
- **Films:** Bink 2 (ffmpeg decodes Bink 1 only); RAD Video Tools converts them, started hidden. Textures are decoded from the cooked assets.
- **Saves:** `%USERPROFILE%\Documents\My Games\Bates\Saved\SaveGames\<steamid>\SaveGame0.sav`, one GVAS slot under Steam Cloud, signed by a CRC32 after the 0xEA byte (`.claude/rules/saves.md`). The settings are in `Saved\Config\UserSettings.dat`.
