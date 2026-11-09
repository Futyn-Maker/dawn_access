<#
.SYNOPSIS
  Installs the last build into the game: the loader, UE4SS and the mod with its speech library.
.DESCRIPTION
  Copies the files of the release (scripts\package.ps1), taken from the last build
  (scripts\build.cmd), into the game. UE4SS's files go where the game already has UE4SS
  (Binaries\Win64\ue4ss, or Binaries\Win64 itself for an older install), or into
  Binaries\Win64\ue4ss when it has none. Program files are replaced; settings files that already
  exist are kept, the mod's DawnAccess.ini too unless -ResetConfig is given.
  Refuses to run while the game is running, since its DLLs are locked then.
.PARAMETER GameDir
  The game's folder, the one that holds Windows\Bates.exe. Without it: the DA_GAME_DIR
  environment variable, else the Steam library that has the game.
.PARAMETER ResetConfig
  Replaces an existing DawnAccess.ini with the repository's default.
#>
param(
    [string]$GameDir,
    [switch]$ResetConfig
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$GameDir = Resolve-DawnGameDir $GameDir
if (Test-DawnRunning) {
    throw "Until Dawn is running. Close it before deploying (its DLLs are locked while the game runs)."
}
$win64 = Get-DawnWin64Dir $GameDir
$ue4ssDir = Get-DawnUE4SSDir $win64

$copied = 0
foreach ($file in Get-DaInstallFiles) {
    if ($file.Dest.StartsWith("ue4ss\")) { $dest = Join-Path $ue4ssDir $file.Dest.Substring(6) }
    else { $dest = Join-Path $win64 $file.Dest }
    if ($file.Kind -eq "Settings" -and (Test-Path $dest)) {
        $isModConfig = $file.Dest -eq "ue4ss\Mods\$DaModName\$DaModName.ini"
        if (-not ($ResetConfig -and $isModConfig)) {
            Write-Host "kept     $dest"
            continue
        }
    }
    New-Item -ItemType Directory -Force (Split-Path -Parent $dest) | Out-Null
    Copy-Item $file.Source $dest -Force
    $copied++
}
Write-Host "Deployed $copied files; UE4SS in $ue4ssDir"

# The linker map of the mod's build just installed, kept by the build's timestamp: a crash dump of
# this build names the mod's frames by it later, whatever was built since (tools\exe\crash.py).
$map = Join-Path $DaRoot "build\src\main.map"
if (Test-Path $map) {
    $stamp = [regex]::Match(((Get-Content $map -TotalCount 8) -join "`n"), 'Timestamp is ([0-9a-f]{8})').Groups[1].Value
    if ($stamp) {
        $maps = Join-Path $DaRoot "build\maps"
        New-Item -ItemType Directory -Force $maps | Out-Null
        Copy-Item $map (Join-Path $maps "main-$stamp.map") -Force
        Write-Host "Kept the build's map as build\maps\main-$stamp.map"
    }
}
