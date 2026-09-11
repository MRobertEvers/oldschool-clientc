<#
.SYNOPSIS
    Launch torirs and report the real peak working set of the live process.

.DESCRIPTION
    PeakWorkingSet64 reads back 0 once the process has exited, so
    Start-Process -Wait cannot answer "how big did this get". This polls a live
    handle instead and prints a single machine-readable line at the end:

        PEAK_WS_MB=<float> EXIT=<code> SECONDS=<float>

    Everything the client wrote goes to -Log (stdout+stderr merged), so a
    MEMPROF or d3d9_mem report survives for attribution.
#>
param(
    [Parameter(Mandatory=$true)][string]$Exe,
    [Parameter(Mandatory=$true)][string]$Log,
    [string[]]$ClientArgs = @(),
    [hashtable]$Env = @{},
    [int]$PollMs = 100,
    [int]$TimeoutSec = 900
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$restore = @{}
foreach ($k in $Env.Keys) {
    $restore[$k] = [Environment]::GetEnvironmentVariable($k)
    [Environment]::SetEnvironmentVariable($k, $Env[$k])
}

$errLog = "$Log.err"
$sw = [Diagnostics.Stopwatch]::StartNew()
try {
    $p = Start-Process -FilePath $Exe -ArgumentList $ClientArgs -NoNewWindow -PassThru `
            -RedirectStandardOutput $Log -RedirectStandardError $errLog
    $peak = 0L
    while (-not $p.HasExited) {
        try {
            $p.Refresh()
            if ($p.PeakWorkingSet64 -gt $peak) { $peak = $p.PeakWorkingSet64 }
        } catch {}
        if ($sw.Elapsed.TotalSeconds -gt $TimeoutSec) {
            try { $p.Kill() } catch {}
            break
        }
        Start-Sleep -Milliseconds $PollMs
    }
    try { $p.Refresh(); if ($p.PeakWorkingSet64 -gt $peak) { $peak = $p.PeakWorkingSet64 } } catch {}
    $p.WaitForExit()
    $code = $p.ExitCode
} finally {
    foreach ($k in $restore.Keys) { [Environment]::SetEnvironmentVariable($k, $restore[$k]) }
}

# The client writes its reports to stderr; keep one file the caller can read.
if (Test-Path $errLog) {
    Get-Content $errLog | Add-Content -Encoding utf8 $Log
    Remove-Item $errLog -Force
}

$mb = [math]::Round($peak / 1MB, 2)
"PEAK_WS_MB=$mb EXIT=$code SECONDS=$([math]::Round($sw.Elapsed.TotalSeconds,1))"
