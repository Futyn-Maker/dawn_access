---
paths:
    - "tests/**"
    - "scripts/test.cmd"
    - "tools/tests/**"
---

# Tests

- `tests\` is a CMake project of its own (`tests\CMakeLists.txt`), configured and built by `scripts\test.cmd` into `build\tests` with the same MSVC and Ninja as the mod, and run there.
    - It compiles the engine-free sources straight from `src\` against `tests\LogStub.cpp`, which stands in for `core/Log` and keeps the lines for assertions, and links nothing of UE4SS. Compiling them standalone is what keeps them engine-free: a module that cannot be compiled this way touches the game.
    - `src\CMakeLists.txt` globs every source of the mod, but `tests\CMakeLists.txt` lists its sources by hand: a new engine-free module and its test file join it the day they are written.
- **The framework is doctest,** fetched by CMake at the first configure (tag pinned in `tests\CMakeLists.txt`); `tests\Main.cpp` provides the main.
    - One file per module, `<Module>Tests.cpp`. A `TEST_CASE` is named for the behaviour it holds, often with the moment of the game it was taken from.
    - Run a part: `build\tests\tests.exe -tc="*words of the case*"`, `-sf="*ModuleTests*"`, `-ltc` to list.
- **Every decision of a feature lives in an engine-free module, and the feature only reads the game and voices.**
    - The `*Rules` modules: Calm, Qte, Choice, Prompt, Text, Menu, Shooting, Nudge, Credits, Picture, Achievement, Speech, SaveCopy.
    - Exploration's modules: `Hold`, `WayList`, `GridPath`, `GroundFile`, `LookTurn`, `Rooms`, `Shapes`, `SceneScripts`, `WorldStream`, `GroundRecheck`, `ExploreText`.
    - And `KeyChoice`, `Describe`, `Strings`, `Config`, `Symbols`, `Greeting`, `Fault`, `Locale`, `KeySpec`, `HotkeyTable`.

    A decision still inside a feature is moved out before it is fixed, so that the test exercises the code the game runs, not a copy of it.

- **Tests go through a module's public functions as the mod calls them,** never through its internals, so that a rewrite keeps the test. Expected strings are compared as UTF-8 (`U8()` in `tests\Support.hpp`) so that a failure prints readably.
- Files a test needs go under the temp directory in a folder of their own, written in binary with the endings the test chooses and removed at the end (`TempDir`). The settings and the locale keep process-wide state, so a case loads what it needs itself.
- **Every bug fixed gets a test that replays it:**
    - a timeline taken from the session's log, with its real times;
    - a recorded ground cut into a fixture (`tests\fixtures\*.tsv`, cut from `dumps\ground-*.tsv` by `tools\ground\cut_ground.py`, `cut_steps.py` or `cut_world_change.py`; `.claude/rules/ground.md`);
    - a level's real collision;
    - an excerpt of the game's real script export.

    A fix to timing or listing is proven by the replay of the failing moment, not by a rule test fed with assumed inputs.

- **A test is shown to catch the old code.** `python -I tools/tests/mutate.py <spec.json>` puts each known fault back (the exact old text in its file), runs the suite, restores the file byte for byte with a fresh time, and reports the cases that failed; a fault no case caught shows "0 failed".
    - Write a spec for every fix. Keep the specs in `research\tools\mutate-*.json`.
    - A new mechanism's review findings get their own faults.
- **When a fix adds a rule beside another, test the choice between them** with the very state the new rule exists for, and put the old choice back in a mutation run. Two rules each tested alone failed in play because the branch that picks one swallowed the other's case.
- **The hold's false entries are bugs like any other.** `HoldTests.cpp` replays every logged moment where the game's own states looked like exploration while nothing was to be handed over: a level's first moments, an opening film, a loading screen, a stair's first step, an interaction letting go, the game's own walk, a fall, a pause. Any new edge of the hold comes with its replay.
- **`HotkeyTableTests.cpp` is the repository's own check:** every command once in the table, every default spelling parsed, the shipped `mod\DawnAccess.ini` carrying each setting at the table's default, and `README.md` carrying a row `| <key> |` for each key a player gets (`DA_REPO_ROOT`, which CMake defines). A failure there is answered in the ini or the README, not in the test.
- **Two audit tools build beside the tests,** run by hand, no tests themselves:
    - `build\tests\audit.exe <ground> <volumes>` judges trigger volumes over a recorded ground with the mod's own rules;
    - `build\tests\scripts_audit.exe <exported scripts>` lists every question the scenes' scripts ask about a character in a volume, as the parse judges it.

    Run the second over every act after a change to `SceneScripts`; it shows which waits change.

- Run `scripts\test.cmd` before every hand-over and commit; it ends with `TESTS_OK` or `TESTS_FAILED`. In-game behaviour that no module can hold is checked by the launch self-test and a session log, with the log lines to look for named in the hand-over.
