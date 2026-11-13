<#
.SYNOPSIS
  Builds the mod, checks it, and lays the release out in dist\DawnAccess-<version> and its zip.
.DESCRIPTION
  The folder holds everything the game needs to run the mod, laid out from the game's own folder
  (the one that holds Windows\Bates.exe): copying its contents there installs the mod.
    Windows\Bates\Binaries\Win64\dwmapi.dll               the loader that starts UE4SS with the game
    Windows\Bates\Binaries\Win64\ue4ss\                   UE4SS with its configuration for the game
    Windows\Bates\Binaries\Win64\ue4ss\Mods\DawnAccess\   the mod with the Prism speech library, its settings, language tables and licenses
  dist\DawnAccess-<version>.zip is the release archive: its entries begin at Windows/, so that
  unpacked into the game's folder it installs the mod. It is read back and held to the folder's
  file list before the script reports success.
  Before anything is laid out the language tables are checked (scripts\check-lang.ps1) and the
  tests are run (scripts\test.cmd); -SkipChecks leaves them out. A tree with changes not
  committed is warned about: the release then is no commit's.
  The symbol files and the linker map of the same build, which crash dumps from players of this
  version are read with, go to dist\DawnAccess-<version>-symbols and are kept as well, by the
  build's timestamp, in build\symbols\<timestamp>, which later packages do not wipe
  (tools\exe\crash.py reads both). They are not for players. build.txt beside them records the
  commit, the version and the timestamp.
  The version is the first line of the VERSION file.
.PARAMETER SkipChecks
  Lays the release out without checking the language tables or running the tests.
.EXAMPLE
  scripts\package.cmd
#>
param(
    [switch]$SkipChecks
)
. (Join-Path $PSScriptRoot "common.ps1")

function Remove-DaPath([string]$Path) {
    if (Test-Path $Path) { Remove-Item $Path -Recurse -Force -ErrorAction Stop }
}

if (-not $SkipChecks) {
    & (Join-Path $PSScriptRoot "check-lang.ps1")
    if ($LASTEXITCODE -ne 0) { throw "The language tables do not check out (scripts\check-lang.ps1)." }
    & (Join-Path $PSScriptRoot "test.cmd")
    if ($LASTEXITCODE -ne 0) { throw "The tests failed (scripts\test.cmd)." }
}
& (Join-Path $PSScriptRoot "build.cmd")
if ($LASTEXITCODE -ne 0) { throw "The build failed." }

# The commit the release is made of, and whether the tree holds changes besides it; asked before the
# errors stop the script, since git can say things on stderr that are no failure.
$commit = ""
$dirty = $false
$git = Get-Command git -ErrorAction SilentlyContinue
if ($git) {
    $commit = (& git -C $DaRoot rev-parse HEAD 2>$null | Out-String).Trim()
    $status = (& git -C $DaRoot status --porcelain 2>$null | Out-String).Trim()
    $dirty = [bool]$status
    if ($dirty) { Write-Warning "The tree has changes not committed: this release is no commit's ($commit plus changes)." }
}

# Only now: the build's tools and git write their progress to stderr, which Windows PowerShell can
# take for errors.
$ErrorActionPreference = "Stop"

$version = Get-DaVersion
$name = "$DaModName-$version"
$dist = Join-Path $DaRoot "dist"
$release = Join-Path $dist $name
$symbols = Join-Path $dist "$name-symbols"
$zip = Join-Path $dist "$name.zip"
Remove-DaPath $release
Remove-DaPath $symbols
Remove-DaPath $zip

$files = Get-DaInstallFiles
$win64 = Join-Path $release $DaWin64Relative
foreach ($file in $files) {
    $dest = Join-Path $win64 $file.Dest
    New-Item -ItemType Directory -Force (Split-Path -Parent $dest) | Out-Null
    Copy-Item $file.Source $dest
}

# The archive: every file under Windows/, with forward slashes (Compress-Archive of Windows PowerShell
# 5.1 writes backslashes), then read back and held to the list.
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$entries = New-Object System.Collections.Generic.List[string]
$archive = [System.IO.Compression.ZipFile]::Open($zip, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in ($files | Sort-Object Dest)) {
        $entry = (($DaWin64Relative + "\" + $file.Dest) -replace '\\', '/')
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.Source, $entry, [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
        $entries.Add($entry)
    }
}
finally {
    $archive.Dispose()
}
$readBack = [System.IO.Compression.ZipFile]::OpenRead($zip)
try {
    $inZip = @($readBack.Entries | ForEach-Object { $_.FullName } | Sort-Object)
}
finally {
    $readBack.Dispose()
}
$wanted = @($entries | Sort-Object)
if (Compare-Object $inZip $wanted) { throw "The archive $zip does not hold exactly the release's files." }
if (@($inZip | Where-Object { -not $_.StartsWith("Windows/") -or $_.Contains("\") }).Count -gt 0) {
    throw "The archive $zip holds an entry outside Windows/ or with a backslash."
}

# The symbols of this build: in the release's symbols folder, and kept by the build's timestamp.
$build = Join-Path $DaRoot "build"
New-Item -ItemType Directory -Force $symbols | Out-Null
foreach ($symbol in "src\main.pdb", "src\main.map", "Game__Shipping__Win64\bin\UE4SS.pdb", "Game__Shipping__Win64\bin\dwmapi.pdb") {
    $path = Join-Path $build $symbol
    if (Test-Path $path) { Copy-Item $path $symbols }
    else { Write-Warning "No $symbol in the build; the symbols folder goes without it." }
}
$stamp = Get-DaBuildStamp
$lines = @("version $version", "commit $commit$(if ($dirty) { ' (with changes not committed)' })", "main.dll timestamp $stamp")
Set-Content -Path (Join-Path $symbols "build.txt") -Value $lines -Encoding ASCII
if ($stamp) {
    $kept = Join-Path $build "symbols\$stamp"
    New-Item -ItemType Directory -Force $kept | Out-Null
    Copy-Item (Join-Path $symbols "*") $kept -Force
}

$size = ($files | ForEach-Object { (Get-Item $_.Source).Length } | Measure-Object -Sum).Sum
Write-Host ""
Write-Host ("{0} files, {1:N1} MB:" -f @($files).Count, ($size / 1MB))
foreach ($file in ($files | Sort-Object Dest)) { Write-Host ("  $DaWin64Relative\" + $file.Dest) }
Write-Host ""
Write-Host "Release: $release"
Write-Host "Archive (unpack into the game's folder): $zip"
Write-Host "Symbols of this build, for its crash dumps (not for players): $symbols$(if ($stamp) { ", kept as build\symbols\$stamp" })"
Write-Host "PACKAGE_OK"
