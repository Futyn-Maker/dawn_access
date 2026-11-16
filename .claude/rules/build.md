---
paths:
    - "scripts/**"
    - "CMakeLists.txt"
    - "src/CMakeLists.txt"
    - "CMakePresets.json"
    - "ue4ss/**"
    - "third_party/prism.version"
    - ".gitattributes"
---

# Build, package, deploy

- **One configuration and two targets.** The configuration is `Game__Shipping__Win64` with Ninja (preset `shipping`). The targets are `DawnAccess` (`main.dll`, which links UE4SS) and `proxy` (`dwmapi.dll`). UE4SS cannot build as Debug.
    - `build.cmd` configures again when the cache's build type, the UE4SS tree or the preset differ from the last configure. That also undoes an IDE that configured `build\` its own way.
    - `FETCHCONTENT_UPDATES_DISCONNECTED` is on: the libraries UE4SS downloads are fetched once per build folder. After moving the submodule to a UE4SS commit that changes their versions, delete `build\`.
- **No third-party binary is in the repository.**
    - The Prism speech library is pinned by one line in `third_party\prism.version`. `fetch-prism.ps1` downloads it into the git-ignored `third_party\prism`: its header to build against, `prism.dll` to ship, its licenses.
    - `build.cmd` compares the pin with `third_party\prism\VERSION` and fetches when they differ, so bumping the pin is the whole of an upgrade.
    - The mod loads `prism.dll` at run time from its own `dlls\` folder, by its full path, and links no import library. A missing library is then an error the mod reports, not a game that will not start.
- **Setup.**
    - `setup.cmd` fetches the submodules over HTTPS, since RE-UE4SS names its own by SSH address, and then Prism. UEPseudo, a submodule of RE-UE4SS, is visible only to a GitHub account linked to an Epic Games account.
    - `env.cmd` finds Visual Studio 2022 (17.13 or newer) through vswhere, puts vswhere's folder on PATH (vcvarsall calls it by name), takes CMake and Ninja from Visual Studio or PATH and cargo from `CARGO_HOME`, and refuses a repository path over 78 characters.
- **The release's file list lives once,** in `Get-DaInstallFiles` (`scripts\common.ps1`), used by both `package.ps1` and `deploy.ps1`:
    - `dwmapi.dll` beside the game's shipping executable;
    - `UE4SS.dll` and the repository's `ue4ss\` configuration in `Windows\Bates\Binaries\Win64\ue4ss\`;
    - the mod (`main.dll` and `prism.dll` in `dlls\`, the `mod\` folder, the licenses of the mod, UE4SS and its third-party code, and Prism's licenses with the note where its source is) in `ue4ss\Mods\DawnAccess\`.

    `enabled.txt` starts the mod, and UE4SS starts any mod folder that has one; there is no `mods.txt` and no Lua mod.

- **The settings files are kept by `deploy.ps1`:** `DawnAccess.ini` and `UE4SS-settings.ini` are replaced only with `-ResetConfig`. A new setting therefore reaches an installed copy only through its default in the code, so every setting reads its default when the ini lacks it.
- **`package.ps1` builds the release from a checked tree.** It checks the language tables and runs the tests first (`-SkipChecks` leaves them out), and warns when the tree holds uncommitted changes, since the release is then no commit's.
    - It lays out `dist\DawnAccess-<VERSION>` and writes `dist\dawn-access-windows.zip`. The archive's name never changes, because the README links to it on the latest release. Its entries begin at `Windows/`, and it is read back and held to the folder's file list.
    - The symbols and the linker map go to `dist\DawnAccess-<VERSION>-symbols` and to `build\symbols\<timestamp>`.
    - `deploy.ps1` keeps each installed build's map as `build\maps\main-<timestamp>.map`. A crash dump is read only with the map whose timestamp equals that of the `main.dll` in the dump.
- **UE4SS** comes from the pinned submodule. The game needs no custom game config: the pinned build finds every engine address by its own scans. `ue4ss\UE4SS-settings.ini` keeps the console off, the object-array cache on, the crash dumper on (`[CrashDump] EnableDumping = 1`), and the game-thread method on the engine tick.
- The DLLs record no path of the building machine.
- UE4SS and `main.dll` use the dynamic CRT, so players need the Visual C++ 2015-2022 x64 runtime. "Failed to load UE4SS.dll" at the game's start means it is missing.
- **Scripts and shells.**
    - `.gitattributes` checks `.cmd`, `.bat` and `.ps1` files out with CRLF, because cmd.exe misreads labels in LF batch files; keep CRLF when editing them.
    - Scripts must run in Windows PowerShell 5.1: no `??`, no ternaries, no three-argument `Join-Path`. There, `$ErrorActionPreference = "Stop"` turns a native tool's stderr into a terminating error, so set it only after the build and git have run.
    - Shell commands can collapse doubled backslashes, and a heredoc's escapes reach Python as real characters: write backslash-heavy content with the file tools, then check a scripted edit with `grep -c $'\r'`.
- **Every script refuses to run against a game that is running** where that matters: deploy, the save scripts. `stop-game.ps1` closes the game gently. The game's folder comes from `-GameDir`, `DA_GAME_DIR`, then Steam's library records, and a failure names `DA_GAME_DIR`.
