---
paths:
    - "src/core/**"
    - "src/hooks/**"
    - "src/watch/**"
    - "src/dllmain.cpp"
---

# Core: reflection, hooks, symbols, pollers

- **UE4SS API pitfalls:**
    - `FindAllOf` returns subclasses too.
    - `obj::FindObject` is `StaticFindObject`, which never loads anything.
    - `obj::StructMember` walks the property chain, since members often live in a base struct.
    - `obj::ForEachArrayElement` takes the object that owns the array, never the array's own pointer. Passing the value pointer faulted every pass of a scan, and the feature it fed judged nothing for a whole session.
    - An array a game function returns is destroyed after reading (`FProperty::DestroyValue`). `obj::Call` frees the strings and texts of its parameters itself. A struct or an array a caller puts into a parameter stays the caller's.
    - An engine call that takes an `FKey` gets it back destroyed.
- **What a hook sees.**
    - `hooks::OnScript` sees what UE4SS's ProcessLocalScriptFunction callback sees: functions a Blueprint executes, the `BP_*` events and C++ events a Blueprint implements included.
    - `hooks::OnNative` fires only when the game calls the UFunction through reflection; a direct C++ call bypasses it. `UBatesQTEStage::FinishQTE` calls the prompt's `EndQTEPrompt` through the vtable, so the native hook never fires and the script hook on `BP_EndQTEPrompt` does.
    - So every hook has a polling twin, and the twins are deduped.
    - `hooks::OnNativeResult` hands the post handler the function's own return value: the game's answer to a scene's `IsInTrigger` ask is read so.
- **Before trusting a hook, read the Blueprint's bytecode.**
    - A `BlueprintNativeEvent` override reaches the native only if it calls its parent (`EX_FinalFunction` of the same name inside the override); hook the script function where it does not. `UpdateSelectionProgress` and `UpdateTimeoutProgress` on the choice widgets are such overrides.
    - A Blueprint can write a member after its parent call, so a post-hook of the native sees the old value: Don't Move's `bIsUsingPassiveVersion` is read on the poll after `Setup`, never inside its hook.
- **`obj::IsLive` and identity.**
    - `obj::IsLive` compares the object-array item back to the pointer, because freed and reused memory passes a plain validity check. `obj::Call` refuses targets that are not live.
    - Live is not "still the one drawn". BMUI hands pooled widgets out again for other content with the same address and name, and a released widget keeps its flags. A memory of a widget that outlives its frame is keyed by `obj::ObjectKey` (`obj::KeyOf`) and pruned with `obj::IsSame`.
    - A cache keyed by any address also keys by the object's identity, its name and outer (`core/AddressCache`), and the engine's delete listener (`core/Freed`) forgets objects as they go. A freed UFunction's address once came back as another Blueprint's function, and Stay Calm's verdict hook answered the wrong function in two scenes.
- **`core/Symbols` (`sym::`).**
    - It holds every RVA and every offset of a member reflection does not list, in a table per build of the game (`sym::Tables`). The first is the reference, Steam's build, read from the PDB it ships; the others were found from it.
    - A table belongs to the PDB its executable names in the CodeView record (GUID and age, `sym::RunningBuild`). `sym::Running` picks the running build's once and logs it. `sym::Address`, `sym::MemberOf`, `sym::Rva` and `sym::Offset` answer from that table, and null or 0 on a build no table names.
    - On such a build every feature that needs a place falls back or stands down. The greeting says so at either level, even with the greeting switched off, and asks for a report with the log (`core/Greeting`); the log names the build's GUID and age.
    - Every table names exactly the reference's places and members, each place at an address of its own, and keeps each optional's flag right after its value (`SymbolsTests`). The relations a table was found by (the Lua functions in the order of their names) are tested for that build alone.
    - A code pointer read through a stub is trusted only after the stub's bytes are checked: the pad library's read pointers are found so.
    - The PDB's layouts are the native layout: a member is reflected only if it is a `UPROPERTY`. Read reflected members by name, and take an offset only for the rest.
- **A new build** (a game update, or a copy whose greeting reported it) gets a table of its own.
    1. Get its `Bates-Win64-Shipping.exe` and, if it ships one, its `Bates-Win64-Shipping.pdb`.
    2. With its PDB, dump it (`tools/exe/pdb_symbols.py`, `DA_SYMBOLS` for another folder) and read every place and member by name, as the reference's were read.
    3. Without one, match it against the reference with `tools/exe/port_build.py`. A wrong place crashes the game or misleads a feature, so nothing goes in on a likeness score alone.
        - `places` matches each function by its code with its addresses masked. "Exact" means the masked code is that function's alone in the reference and begins exactly one function of the new build, naming the same strings: the pad library's 27 stubs share one shape.
        - A "near" candidate is taken only once `side` shows it is the same function: the same calls, constants and offsets, and calls that land on the other places found. A compiler of its own reorders registers and branches.
        - A place of data is read at the same instruction of every matched function that uses it, and all must agree. A place only one function uses (a stub's pointer) is checked with `side`.
        - `members` shows each member at its offset in a function of the new build that uses it, with the instruction. An exact match proves only what that instruction touches, so a small offset, or another class's function, is read before it counts: a destructor freeing the string at that offset counts, a local of the frame does not. `callee` reaches a function whose own code changed through a caller that did not.
        - `layout` compares every reflected property's offset: the unreflected members of a class that moved must be found anew. Bools carry no offset there, and a property added at the end of a class shows among the records of one build only, which are listed to be read.
    4. Add the table to `kTables` in `core/Symbols.cpp`: its GUID, age and the version its executable states, its places, each with how it was found, and its members (the reference's array where every one held, else an array of its own). Add its own places to `SymbolsTests`.
    5. Build, test, and ask the player for a session log at `LogLevel=Verbose`. It shows "which the mod knows", then the readings exploration and aiming depend on.
- **Soft references.** A `TSoftObjectPtr` (40 bytes) has its weak half filled only once something looks the object up. `obj::SoftObjectAt` reads the weak half, else `StaticFindObject` by the path, else the live object of that full name.
    - A door's leaves are the keys of its interaction's `BoundObjects` map, so reading only the weak half found no leaf for any door not used yet. For days no shut door stood as a wall.
    - Every new read of a soft reference goes this way.
- **Engine containers** are read by their layout: a `TMap`'s element stride comes from `FMapProperty::GetMapLayout().SetLayout.Size`, its hash and sparse array from the map itself.
- **UObjects are touched on the game thread only:** in the engine-tick pump (`gamethread::Post`, `AddPoller`) or in hook callbacks. Construction callbacks may come from loading threads and only record. Every poller, hook and command runs under `obj::SafeInvokeLogged`, which writes at most one ERROR per five seconds per site; an ERROR in a session log is a defect to find.
- **A poller that must not miss an event reads its state at every tick.**
    - Rate-limit the scan that finds the object, never the reading of its state.
    - A costly call passed as an argument to a rule runs at every poll: ask the game only when the rule needs the answer (a QTE stage's `bHasStarted` only while it has no verdict and drives the prompt).
- **The game state** (`watch/GameState`, `EBatesGameState`: None, Booting, Menu, Paused, Loading, Game, GameStarting, GameEnding, LoadingNewLevel, FreezeScreen) is asked of `ABatesGameState` at every poll that needs it (`gamestate::Refresh`). `ProcessNewGameState` sets the pause and the state in one call, and a value read only every few frames lets a fade and a pause arrive in the wrong order.
- **The Lua state** (`core/GameLua`) is the scene scripts' Lua 5.1 state (PUC, floats): `ULuaScriptUpdater::RunningScript` → `UScript::mScriptData` (a `lua_State*`) behind `mScriptAccessMutex`.
    - It is read between frames, when no script runs, under the lock, with the stack restored and SEH guarded. The Lua functions are linked into the executable, and their RVAs are in `core/Symbols`' table for each build: the mod calls them directly, so each is shown to be that function before it goes in.
    - `lua::Global` reads a global's field, `lua::Nested` a nested table's, and `lua::Machines` every running state machine's current state.
    - A failed read keeps the last answer: a read taken for "nil" once made a spent trigger new again.
- **Names of things** (`core/LevelNames`).
    - `CoreProperties.CoreName` is empty on most actors.
    - The designers' name is in the level's actor database: `ULADFunctionLibrary::FindLevelDatabaseByActor`, then `ULADDatabase::GetActorData` → `FLADActorData.FriendlyName`. Those are the names the scene scripts call things by.
    - `PlacingLevel` is the world of the actor's outer level; one class placed by several levels may show different things.
    - Key a feature by a name read in the level's data, or logged at the moment of binding, never by a name taken from an event payload or a class asset.
- **The watchers.**
    - The class watcher (`watch/Watchers`) sorts the shown widgets by native class every 30 frames, one `FindAllLive` of user widgets. Features also hook their widgets' events for an instant reaction.
    - The current screen (`watch::CurrentScreen`) is the leaf page of BMUI's page reflection system's active tree (`.claude/rules/menus-hud.md`).
    - `watch::DisplayGeneration` counts page displays and activations, so that a page shown anew in the same pooled widget is noticed.
- **Each act is its own world:** a level loaded anew clears the caches that belong to a world. The mod's greeting comes while the game still boots. The features are installed after it, once the game's language has settled.
- **The mod falls silent the moment the game goes away** (`speech::rules::Closing`: `GIsRequestingExit`, `GIsCriticalError`, the crash reporter's count), so nothing is said over a crash or a quit.
