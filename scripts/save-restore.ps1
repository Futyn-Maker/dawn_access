<#
.SYNOPSIS
  Puts a backup made by scripts\save-backup.ps1 back into the game's saved folder.
.DESCRIPTION
  Replaces Documents\My Games\Bates\Saved\SaveGames (and, with -Settings, Saved\Config) with the
  backup's copy, after backing up what is there now. Refuses to run while the game runs. Steam
  Cloud may ask at the next start which copy of the save to keep; choose the local one.
.PARAMETER Backup
  The backup folder's name under research\saves, or its full path. Without it, the newest backup.
.PARAMETER Settings
  Also restores the settings (Saved\Config).
#>
param(
    [string]$Backup,
    [switch]$Settings
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
if (Test-DawnRunning) { throw "Until Dawn is running. Close it before restoring a save." }
$savesRoot = Join-Path $DaRoot "research\saves"
if (-not $Backup) {
    $newest = Get-ChildItem $savesRoot -Directory -ErrorAction SilentlyContinue | Sort-Object Name | Select-Object -Last 1
    if (-not $newest) { throw "No backups in $savesRoot" }
    $Backup = $newest.FullName
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
