<#
.SYNOPSIS
  Puts a backup made by scripts\save-backup.ps1, or one of the mod's checkpoint copies, back into the
  game's saved folder.
.DESCRIPTION
  Replaces Documents\My Games\Bates\Saved\SaveGames (and, with -Settings, Saved\Config) with the
  backup's copy, after backing up what is there now. With -Checkpoint, puts one of the copies the mod
  keeps of every checkpoint the game saves (the game's Mods\DawnAccess\dumps\saves folder, named
  <date>-<time>-<act>.sav) in place of the game's one save, after the same backup. Refuses to run while
  the game runs. Steam Cloud may ask at the next start which copy of the save to keep; choose the local
  one.
.PARAMETER Backup
  The backup folder's name under research\saves, or its full path. Without it, the newest backup.
.PARAMETER Checkpoint
  A checkpoint copy's file name in the mod's dumps\saves folder (or the start of one, as its date and
  time), or its full path.
.PARAMETER Settings
  Also restores the settings (Saved\Config).
.PARAMETER GameDir
  The game's folder, where the mod's checkpoint copies are looked for; found through Steam without it.
#>
param(
    [string]$Backup,
    [string]$Checkpoint,
    [switch]$Settings,
    [string]$GameDir
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
if (Test-DawnRunning) { throw "Until Dawn is running. Close it before restoring a save." }

if ($Checkpoint) {
    $file = $Checkpoint
    if (-not (Test-Path $file)) {
        $game = Resolve-DawnGameDir $GameDir
        $copies = Join-Path (Get-DawnUE4SSDir (Get-DawnWin64Dir $game)) "Mods\$DaModName\dumps\saves"
        $found = @(Get-ChildItem $copies -Filter "*.sav" -ErrorAction SilentlyContinue | Where-Object { $_.Name -like "$Checkpoint*" } | Sort-Object Name)
        if ($found.Count -eq 0) { throw "No checkpoint copy named $Checkpoint in $copies" }
        if ($found.Count -gt 1) { throw "$Checkpoint names $($found.Count) copies in ${copies}: $(($found | ForEach-Object Name) -join ', ')" }
        $file = $found[0].FullName
    }
    $slots = @(Get-ChildItem (Join-Path $DaSavedDir "SaveGames") -Directory -ErrorAction SilentlyContinue |
        Where-Object { Test-Path (Join-Path $_.FullName "SaveGame0.sav") } | Sort-Object LastWriteTime)
    if ($slots.Count -eq 0) { throw "The game's save slot was not found under $DaSavedDir\SaveGames" }
    # What is there now is kept too, so a restore is never a loss.
    & (Join-Path $PSScriptRoot "save-backup.ps1") -Name "before-restore" | Out-Null
    $target = Join-Path $slots[-1].FullName "SaveGame0.sav"
    Copy-Item $file $target -Force
    Write-Host "Restored the checkpoint $file as $target"
    return
}

$savesRoot = Join-Path $DaRoot "research\saves"
if (-not $Backup) {
    # The newest backup save-backup.ps1 made (its folders are named by the time, yyyyMMdd-HHmmss-...),
    # never one a restore made of what it replaced: a second plain restore repeats the first.
    $newest = Get-ChildItem $savesRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^\d{8}-\d{6}' -and $_.Name -notlike '*-before-restore' -and (Test-Path (Join-Path $_.FullName "SaveGames")) } |
        Sort-Object Name | Select-Object -Last 1
    if (-not $newest) { throw "No backups in $savesRoot" }
    $Backup = $newest.FullName
    Write-Host "The newest backup: $Backup"
}
elseif (-not (Test-Path $Backup)) { $Backup = Join-Path $savesRoot $Backup }
if (-not (Test-Path (Join-Path $Backup "SaveGames"))) { throw "Not a backup folder (no SaveGames in it): $Backup" }

# What is there now is kept too, so a restore is never a loss.
& (Join-Path $PSScriptRoot "save-backup.ps1") -Name "before-restore" | Out-Null

$parts = @("SaveGames")
if ($Settings) { $parts += "Config" }
foreach ($part in $parts) {
    $source = Join-Path $Backup $part
    if (-not (Test-Path $source)) { continue }
    $dest = Join-Path $DaSavedDir $part
    if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
    Copy-Item $source $dest -Recurse -Force
}
Write-Host "Restored $($parts -join ' and ') from $Backup"
