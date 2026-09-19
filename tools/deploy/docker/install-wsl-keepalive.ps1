# Keep the WSL2 distro -- and therefore dockerd and the world -- running.
#
# Run once, from an ELEVATED PowerShell:
#
#     .\install-wsl-keepalive.ps1
#
# Why this is needed at all: WSL stops a distro when nothing is attached to it,
# and stopping the distro stops the Docker daemon, which stops the container.
# It stops it *cleanly* -- SIGTERM, every player logged out and saved -- so there
# is no crash and no error anywhere; the world is simply gone. Without this the
# world dies the moment whoever started it closes their session, and a reboot
# never brings it back.
#
# The task does two things at boot: starts the distro, then holds a session
# open forever. systemd inside the distro starts dockerd, and the container's
# `restart: unless-stopped` brings the world up behind it.
#
# It runs as the interactive user, not SYSTEM: WSL distros are per-user, and
# SYSTEM has none installed, so a task running as SYSTEM starts nothing.

$ErrorActionPreference = 'Stop'

$taskName = 'torirs-wsl-keepalive'

# NOT "$env:USERDOMAIN\$env:USERNAME". On a machine that is not domain-joined
# USERDOMAIN is "WORKGROUP", which is not a principal, and Register-ScheduledTask
# fails with "No mapping between account names and security IDs was done" --
# which reads like a permissions problem and is not one. GetCurrent().Name
# gives the real MACHINE\user.
$user = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name

# `sleep infinity` under -u root, with no shell profile to fail on. This is the
# session WSL counts; while it exists the distro stays up.
$script = Join-Path $PSScriptRoot 'start-torirs.ps1'
$action = New-ScheduledTaskAction -Execute 'powershell.exe' `
    -Argument "-NoProfile -ExecutionPolicy Bypass -File `"$script`""

$trigger = New-ScheduledTaskTrigger -AtStartup

# RunLevel Highest so it does not wait for an interactive logon, and
# ExecutionTimeLimit 0 because the whole point is that it never finishes.
$principal = New-ScheduledTaskPrincipal -UserId $user -LogonType S4U -RunLevel Highest
$settings  = New-ScheduledTaskSettingsSet `
    -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -ExecutionTimeLimit ([TimeSpan]::Zero) `
    -RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 1)

Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger `
    -Principal $principal -Settings $settings -Force | Out-Null

Write-Host "registered scheduled task '$taskName' (runs start-torirs.ps1 as $user at startup)"

Start-ScheduledTask -TaskName $taskName
Start-Sleep -Seconds 5
Get-ScheduledTask -TaskName $taskName | Select-Object TaskName, State
Write-Host "distro state:"
wsl.exe -l -v
