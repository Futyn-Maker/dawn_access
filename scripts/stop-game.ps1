# Stops Until Dawn: the shipping executable and the launcher in front of it.
. (Join-Path $PSScriptRoot "common.ps1")
# Asked to close first, as its window's close button asks it, so that a checkpoint being written is
# finished: a kill in the middle of one can damage the one save slot, which Steam Cloud then keeps.
# Killed only if it is still there after the wait.
foreach ($process in @(Get-Process -Name $DaExeName, $DaLauncherName -ErrorAction SilentlyContinue)) {
    [void]$process.CloseMainWindow()
}
$deadline = (Get-Date).AddSeconds(15)
while ((Test-DawnRunning) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
if (Test-DawnRunning) {
    Write-Host "not closed after 15 s: stopped by force"
    Stop-Process -Name $DaExeName -Force -ErrorAction SilentlyContinue
    Stop-Process -Name $DaLauncherName -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
}
if (Test-DawnRunning) { Write-Host "still running" } else { Write-Host "stopped" }
