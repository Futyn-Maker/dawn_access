<#
.SYNOPSIS
  Copies the game's saves and settings into a dated backup folder.
.DESCRIPTION
  Until Dawn keeps one save slot per Steam account under Documents\My Games\Bates\Saved\SaveGames
  and its settings in Documents\My Games\Bates\Saved\Config. This copies both into
  research\saves\<date-time>[-<name>]\ so that a chapter can be returned to later with
  scripts\save-restore.ps1. Refuses to run while the game runs, since the game writes its save as
  it goes. Steam Cloud keeps its own copy of the save: after a restore, Steam may ask which copy
  to keep at the next start of the game; choose the local one.
.PARAMETER Name
  A word added to the backup folder's name, such as chapter3.
#>
param([string]$Name)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
if (Test-DawnRunning) { throw "Until Dawn is running. Close it before backing up its saves." }
if (-not (Test-Path $DaSavedDir)) { throw "The game's saved folder was not found: $DaSavedDir" }
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
if ($Name) { $stamp = "$stamp-$Name" }
$dest = Join-Path $DaRoot "research\saves\$stamp"
New-Item -ItemType Directory -Force $dest | Out-Null
$count = 0
foreach ($part in "SaveGames", "Config") {
    $source = Join-Path $DaSavedDir $part
    if (-not (Test-Path $source)) { continue }
    Copy-Item $source (Join-Path $dest $part) -Recurse -Force
    $count += @(Get-ChildItem $source -Recurse -File).Count
}
Write-Host "Backed up $count files to $dest"
