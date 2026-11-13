# Shared by the PowerShell scripts: where the repository, its build and the game are, and which
# files make up an installed copy of the mod. Dot-source it:
#   . (Join-Path $PSScriptRoot "common.ps1")
# Runs in Windows PowerShell 5.1 and in PowerShell 7.

$DaRoot = Split-Path -Parent $PSScriptRoot
$DaSteamAppId = 2172010
# The game's processes: the shipping executable the mod lives in, and the launcher in front of it.
$DaExeName = "Bates-Win64-Shipping"
$DaLauncherName = "Bates"
$DaModName = "DawnAccess"
# The game's binaries folder below the game's folder (the one that holds the Windows folder).
$DaWin64Relative = "Windows\Bates\Binaries\Win64"
# Where the game keeps its settings and saves.
$DaSavedDir = Join-Path ([Environment]::GetFolderPath("MyDocuments")) "My Games\Bates\Saved"

# The mod's version: the first line of the VERSION file.
function Get-DaVersion {
    $version = (Get-Content (Join-Path $DaRoot "VERSION") -TotalCount 1).Trim()
    if (-not $version) { throw "The VERSION file is empty." }
    return $version
}

# The Prism release the mod is built and shipped with: the first line of
# third_party\prism.version. scripts\fetch-prism.ps1 puts that release into third_party\prism,
# which is not part of the repository.
function Get-DaPrismVersion {
    $version = (Get-Content (Join-Path $DaRoot "third_party\prism.version") -TotalCount 1).Trim()
    if (-not $version) { throw "The third_party\prism.version file is empty." }
    return $version
}

# The RE-UE4SS tree the last build used (scripts\build.cmd notes it), else the one it would use.
function Get-DaUE4SSSourceDir {
    $note = Join-Path $DaRoot "build\ue4ss-source.txt"
    if (Test-Path $note) {
        $dir = (Get-Content $note -TotalCount 1).Trim()
        if ($dir) { return $dir.Replace('/', '\') }
    }
    if ($env:DA_UE4SS_SOURCE_DIR) { return $env:DA_UE4SS_SOURCE_DIR }
    return (Join-Path $DaRoot "third_party\RE-UE4SS")
}

# The game's folder in the Steam library that has it, from Steam's own records; $null when Steam
# or the game is not installed.
function Find-DawnSteamDir {
    $steam = $null
    foreach ($key in 'HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam', 'HKLM:\SOFTWARE\Valve\Steam') {
        $values = Get-ItemProperty -Path $key -ErrorAction SilentlyContinue
        if (-not $values) { continue }
        if ($values.PSObject.Properties['SteamPath']) { $steam = $values.SteamPath }
        elseif ($values.PSObject.Properties['InstallPath']) { $steam = $values.InstallPath }
        if ($steam) { break }
    }
    if (-not $steam) { return $null }
    $libraries = @($steam)
    $folders = Join-Path $steam "steamapps\libraryfolders.vdf"
    if (Test-Path $folders) {
        foreach ($match in [regex]::Matches((Get-Content $folders -Raw), '"path"\s+"([^"]+)"')) {
            $libraries += $match.Groups[1].Value.Replace('\\', '\')
        }
    }
    foreach ($library in ($libraries | Select-Object -Unique)) {
        $manifest = Join-Path $library "steamapps\appmanifest_$DaSteamAppId.acf"
        if (-not (Test-Path $manifest)) { continue }
        $installDir = [regex]::Match((Get-Content $manifest -Raw), '"installdir"\s+"([^"]+)"').Groups[1].Value
        if (-not $installDir) { continue }
        $dir = Join-Path $library "steamapps\common\$installDir"
        if (Test-Path $dir) { return (Resolve-Path $dir).Path }
    }
    return $null
}

# The game's folder (the one that holds the Windows folder with Bates.exe): the given one, else
# DA_GAME_DIR, else the Steam library that has the game.
function Resolve-DawnGameDir([string]$GameDir) {
    $from = "-GameDir"
    if (-not $GameDir) { $GameDir = $env:DA_GAME_DIR; $from = "DA_GAME_DIR" }
    if (-not $GameDir) { $GameDir = Find-DawnSteamDir; $from = "Steam" }
    if (-not $GameDir) {
        throw "Until Dawn was not found in Steam's libraries. Pass -GameDir with the game's folder (the one that holds Windows\Bates.exe), or set DA_GAME_DIR to it."
    }
    $exe = Join-Path $GameDir "$DaWin64Relative\$DaExeName.exe"
    if (-not (Test-Path $exe)) { throw "$GameDir (from $from) is not Until Dawn's folder: $exe is missing." }
    return (Resolve-Path $GameDir).Path
}

function Get-DawnWin64Dir([string]$GameDir) {
    return (Join-Path $GameDir $DaWin64Relative)
}

# Where UE4SS lives in the game: Binaries\Win64\ue4ss, as UE4SS's releases and this mod's release
# install it, or Binaries\Win64 itself for an older install. A game without UE4SS gets the
# ue4ss folder.
function Get-DawnUE4SSDir([string]$Win64) {
    $sub = Join-Path $Win64 "ue4ss"
    if (Test-Path (Join-Path $sub "UE4SS.dll")) { return $sub }
    if (Test-Path (Join-Path $Win64 "UE4SS.dll")) { return $Win64 }
    return $sub
}

# The timestamp the linker gave the mod's last build (main.map's "Timestamp is ..."), as eight hex
# digits; empty when there is no map. A crash dump names the main.dll it ran by the same number.
function Get-DaBuildStamp {
    $map = Join-Path $DaRoot "build\src\main.map"
    if (-not (Test-Path $map)) { return "" }
    return [regex]::Match(((Get-Content $map -TotalCount 8) -join "`n"), 'Timestamp is ([0-9a-f]{8})').Groups[1].Value
}

# True while the game runs: its DLLs are locked then, and its saves must not be touched.
function Test-DawnRunning {
    return [bool]((Get-Process -Name $DaExeName -ErrorAction SilentlyContinue) -or (Get-Process -Name $DaLauncherName -ErrorAction SilentlyContinue))
}

# The files of a folder of the repository that a release takes: the ones git tracks there, so that a
# file left in the folder by hand is never shipped (said, when there is one); every file when the
# repository has no git to ask.
function Get-DaShippedFiles([string]$Folder) {
    $ErrorActionPreference = "Continue"
    $root = (Resolve-Path $Folder).Path
    $all = @(Get-ChildItem $root -Recurse -File)
    $git = Get-Command git -ErrorAction SilentlyContinue
    if (-not $git) { return $all }
    $relative = $root.Substring($DaRoot.Length).TrimStart('')
    $tracked = @(& git -C $DaRoot ls-files -- $relative 2>$null)
    if ($LASTEXITCODE -ne 0 -or $tracked.Count -eq 0) { return $all }
    $set = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($path in $tracked) { [void]$set.Add(($path -replace '/', '')) }
    $shipped = New-Object System.Collections.Generic.List[object]
    foreach ($file in $all) {
        $name = $file.FullName.Substring($DaRoot.Length).TrimStart('')
        if ($set.Contains($name)) { $shipped.Add($file) }
        else { Write-Warning "Not shipped, as git does not track it: $name" }
    }
    return $shipped
}

# Every file of an installed copy of the mod: its source, its place under the game's
# Binaries\Win64 folder, UE4SS's files under "ue4ss\", and its kind. Program files are replaced by
# a newer build; Settings files are the player's once installed. prism.dll sits beside the mod's
# main.dll, where the mod loads it from by its full path, so that the library is the mod's own.
function Get-DaInstallFiles {
    $bin = Join-Path $DaRoot "build\Game__Shipping__Win64\bin"
    $ue4ss = Get-DaUE4SSSourceDir
    $modDest = "ue4ss\Mods\$DaModName"
    $files = New-Object System.Collections.Generic.List[object]
    $add = {
        param($Source, $Dest, $Kind)
        $files.Add([pscustomobject]@{ Source = $Source; Dest = $Dest; Kind = $Kind })
    }

    & $add (Join-Path $bin "dwmapi.dll") "dwmapi.dll" Program
    & $add (Join-Path $bin "UE4SS.dll") "ue4ss\UE4SS.dll" Program
    # UE4SS's configuration for this game: its settings and, where the game needs them, the
    # signatures and layouts it cannot find by itself. The repository keeps them in ue4ss\.
    $config = Join-Path $DaRoot "ue4ss"
    if (-not (Test-Path $config)) { throw "UE4SS's configuration for the game is missing: $config" }
    $config = (Resolve-Path $config).Path
    foreach ($file in Get-DaShippedFiles $config) {
        & $add $file.FullName ("ue4ss\" + $file.FullName.Substring($config.Length + 1)) Settings
    }

    & $add (Join-Path $DaRoot "build\src\main.dll") "$modDest\dlls\main.dll" Program
    & $add (Join-Path $DaRoot "third_party\prism\bin\prism.dll") "$modDest\dlls\prism.dll" Program
    $mod = (Resolve-Path (Join-Path $DaRoot "mod")).Path
    foreach ($file in Get-DaShippedFiles $mod) {
        $relative = $file.FullName.Substring($mod.Length + 1)
        $kind = "Program"
        if ($relative -eq "$DaModName.ini") { $kind = "Settings" }
        & $add $file.FullName "$modDest\$relative" $kind
    }

    & $add (Join-Path $DaRoot "LICENSE") "$modDest\licenses\$DaModName.txt" Program
    & $add (Join-Path $ue4ss "LICENSE") "$modDest\licenses\UE4SS.txt" Program
    # Prism's licence and those of the libraries it carries, laid out as its own release has them.
    $prism = Join-Path $DaRoot "third_party\prism\licenses"
    if (-not (Test-Path $prism)) { throw "The Prism library is missing: run scripts\fetch-prism.ps1." }
    $prism = (Resolve-Path $prism).Path
    foreach ($file in Get-ChildItem $prism -Recurse -File) {
        & $add $file.FullName ("$modDest\licenses\prism\" + $file.FullName.Substring($prism.Length + 1)) Program
    }
    # Where Prism's source is, as its licence (MPL-2.0, section 3.2) asks of a release that carries the
    # library: written for the version the build fetched.
    $prismVersion = Get-DaPrismVersion
    $source = Join-Path $DaRoot "build\prism-source.txt"
    New-Item -ItemType Directory -Force (Split-Path -Parent $source) | Out-Null
    $words = "Prism $prismVersion, the speech library in dlls\prism.dll, is licensed under the Mozilla Public License 2.0 (MPL-2.0; its text is prism\mpl-2.0.txt beside this file). Its source code: https://github.com/ethindp/prism/tree/$prismVersion"
    Set-Content -Path $source -Value $words -Encoding ASCII
    & $add $source "$modDest\licenses\prism\SOURCE.txt" Program
    # The licences of the libraries UE4SS.dll is built from (tools\release\collect_licences.py).
    $thirdParty = Join-Path $DaRoot "third_party\ue4ss-licences"
    if (-not (Test-Path $thirdParty)) { throw "UE4SS's third-party licences are missing: run python -I tools\release\collect_licences.py." }
    foreach ($file in Get-DaShippedFiles $thirdParty) {
        & $add $file.FullName ("$modDest\licenses\UE4SS-third-party\" + $file.Name) Program
    }

    $missing = @($files | Where-Object { -not (Test-Path $_.Source) } | ForEach-Object { $_.Source })
    if ($missing.Count -gt 0) {
        throw "Missing (run scripts\build.cmd first): $($missing -join ', ')"
    }
    return $files
}
