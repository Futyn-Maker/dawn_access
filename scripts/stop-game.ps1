# Stops Until Dawn: the shipping executable and the launcher in front of it.
. (Join-Path $PSScriptRoot "common.ps1")
Stop-Process -Name $DaExeName -Force -ErrorAction SilentlyContinue
Stop-Process -Name $DaLauncherName -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
if (Test-DawnRunning) { Write-Host "still running" } else { Write-Host "stopped" }
