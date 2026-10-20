<#
.SYNOPSIS
  Launches Until Dawn and waits until the mod reports readiness.
  Detects a crash (process gone, UE4SS fatal error, or the engine's crash dialog)
  and, with -AutoDismissCrash, closes the dialog/process so nothing stays blocked.
  A game in a Steam library is started through Steam, any other copy through its launcher.
.PARAMETER GameDir
  The game's folder, the one that holds Windows\Bates.exe. Without it: the DA_GAME_DIR
  environment variable, else the Steam library that has the game.
.PARAMETER TimeoutSec
  Seconds to wait for "[DawnAccess] ready" in UE4SS.log.
#>
param(
    [string]$GameDir,
    [int]$TimeoutSec = 180,
    [switch]$AutoDismissCrash
)
. (Join-Path $PSScriptRoot "common.ps1")
$GameDir = Resolve-DawnGameDir $GameDir

Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Runtime.InteropServices;
using System.Collections.Generic;
public static class DaWin {
    public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lParam);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr hWnd, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
    public static List<string> Titles = new List<string>();
    public static IntPtr Found = IntPtr.Zero;
    public static IntPtr FindContaining(string part) {
        Found = IntPtr.Zero; Titles.Clear();
        EnumWindows((h, l) => {
            if (!IsWindowVisible(h)) return true;
            var sb = new StringBuilder(512); GetWindowText(h, sb, 512);
            var t = sb.ToString();
            if (t.Length > 0) Titles.Add(t);
            if (t.IndexOf(part, StringComparison.OrdinalIgnoreCase) >= 0) { Found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return Found;
    }
}
"@

$win64 = Get-DawnWin64Dir $GameDir
$ue4ssDir = Get-DawnUE4SSDir $win64
$log = Join-Path $ue4ssDir "UE4SS.log"
if (Get-Process -Name $DaExeName -ErrorAction SilentlyContinue) {
    Write-Host "Game already running."
} elseif ($GameDir -match '\\steamapps\\common\\') {
    Start-Process "steam://rungameid/$DaSteamAppId"
} elseif (Test-Path (Join-Path $GameDir "Windows\$DaLauncherName.exe")) {
    Start-Process (Join-Path $GameDir "Windows\$DaLauncherName.exe") -WorkingDirectory (Join-Path $GameDir "Windows")
} else {
    Start-Process (Join-Path $win64 "$DaExeName.exe") -WorkingDirectory $win64
}
$launchedAt = Get-Date
$deadline = $launchedAt.AddSeconds($TimeoutSec)
$sawProcess = $false
$result = "timeout"
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 2
    $proc = Get-Process -Name $DaExeName -ErrorAction SilentlyContinue
    if ($proc) { $sawProcess = $true }
    $crashWnd = [DaWin]::FindContaining("has crashed")
    if ($crashWnd -ne [IntPtr]::Zero) {
        $result = "crash-dialog"
        Write-Host "CRASH: the engine crash dialog is showing."
        if ($AutoDismissCrash) {
            [DaWin]::PostMessage($crashWnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null   # WM_CLOSE
            Start-Sleep -Seconds 2
            Stop-Process -Name $DaExeName -Force -ErrorAction SilentlyContinue
        }
        break
    }
    if ($sawProcess -and -not $proc) { $result = "exited"; Write-Host "CRASH: game process exited."; break }
    # Only trust the log once this launch has rewritten it (UE4SS truncates it at start).
    if ((Test-Path $log) -and ((Get-Item $log).LastWriteTime -gt $launchedAt)) {
        $content = Get-Content $log -Raw -ErrorAction SilentlyContinue
        if ($content -match "\[$DaModName\] ready") { $result = "ready"; break }
        if ($content -match 'Fatal Error') { $result = "ue4ss-fatal"; Write-Host "UE4SS fatal error:"; ($content -split "`n") | Select-String 'Fatal' | Select-Object -First 3; break }
    }
}
switch ($result) {
    "ready"   { Write-Host "$DaModName ready." }
    "timeout" { Write-Host "Timed out waiting for $DaModName ready." }
}
# Last mod log lines help to locate a crash.
$modLog = Join-Path $ue4ssDir "Mods\$DaModName\$DaModName.log"
if (($result -ne "ready") -and (Test-Path $modLog)) {
    Write-Host "--- last mod log lines ---"
    Get-Content $modLog -Tail 8 -Encoding UTF8
}
exit ($(if ($result -eq "ready") { 0 } else { 1 }))
