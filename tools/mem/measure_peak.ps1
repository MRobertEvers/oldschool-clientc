<#
.SYNOPSIS
    Run a client to a bounded stop and report its peak memory.

.DESCRIPTION
    Answers "how much memory does a session actually cost", which is a
    different question from the one MEMPROF answers. MEMPROF ranks the tracked
    C heap; this measures what the OS charged the process -- the heap plus
    everything that never went through malloc: renderer surfaces, loader
    images, CRT arenas, driver buffers, and (for the Java client) the entire
    JVM. On the d3d9 lane the gap between the two has been over 200 MB, so a
    footprint claim made from either number alone is unfalsifiable.

    The launcher is deliberately generic -- an exe plus an argument list --
    because the C client and the Java client have to be compared, and a
    comparison is only worth stating if both sides were measured by the same
    code. Nothing here knows which client it is running.

    PeakWorkingSet64 is read from a LIVE process. A Process object whose
    process has exited reports 0 for every memory field on Windows, and that
    zero is indistinguishable from a measurement, so the poll loop IS the
    measurement -- there is no after-the-fact read to fall back on.

.PARAMETER Exe
    Executable to run. For the Java client this is java.exe, not gradlew:
    gradle forks the JVM, and polling the launcher measures the launcher.

.PARAMETER ClientArgs
    Argument list passed to Exe.

.PARAMETER Env
    Hashtable of environment variables to set for the child (e.g.
    TORIRS_MAX_FRAMES). Restored afterwards.

.PARAMETER DurationSec
    Stop the process after this many seconds. 0 (default) waits for it to exit
    on its own -- which is what the C client does under TORIRS_MAX_FRAMES. A
    client with no frame bound needs a wall-clock stop instead; keep it long
    enough to cover the load that produces the peak.

.PARAMETER Label
    Name for this run in the output.

.PARAMETER PollMs
    Sampling interval. The peak fields are maintained by the kernel, not by
    the sampler, so this bounds only the resolution of the WorkingSet trace;
    the peak itself is exact regardless.

.PARAMETER Csv
    If set, the per-sample trace is written here.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [string[]]$ClientArgs = @(),
    [hashtable]$Env = @{},
    [int]$DurationSec = 0,
    [string]$Label = "run",
    [int]$PollMs = 50,
    [string]$Csv = ""
)

$ErrorActionPreference = "Stop"

$resolved = (Get-Command $Exe -ErrorAction SilentlyContinue)
if ($resolved) {
    $Exe = $resolved.Source
} elseif (-not (Test-Path -LiteralPath $Exe)) {
    throw "no such executable: $Exe"
}

# Save and set the child's environment. PowerShell has no per-process env for
# Start-Process, so this is the process env and has to be put back.
$saved = @{}
foreach ($k in $Env.Keys) {
    $saved[$k] = [Environment]::GetEnvironmentVariable($k)
    [Environment]::SetEnvironmentVariable($k, [string]$Env[$k])
}

Write-Host "[$Label] $Exe $($ClientArgs -join ' ')"
if ($Env.Count -gt 0) {
    Write-Host "[$Label] env: $(($Env.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ' ')"
}

$samples = New-Object System.Collections.Generic.List[object]
$sw = [System.Diagnostics.Stopwatch]::StartNew()

try {
    if ($ClientArgs.Count -gt 0) {
        $proc = Start-Process -FilePath $Exe -ArgumentList $ClientArgs -PassThru -NoNewWindow
    } else {
        $proc = Start-Process -FilePath $Exe -PassThru -NoNewWindow
    }

    # Last-known-good values. The process can exit between HasExited and the
    # field read, and the fields go to 0 the moment it does; keeping the
    # previous sample means a race loses one sample, not the measurement.
    $peakWs = 0L
    $peakPagefile = 0L
    $maxWs = 0L
    $maxPrivate = 0L
    $stoppedByTimer = $false

    while ($true) {
        try {
            $proc.Refresh()
            if ($proc.HasExited) { break }

            $ws = $proc.WorkingSet64
            $priv = $proc.PrivateMemorySize64
            $pws = $proc.PeakWorkingSet64
            $ppf = $proc.PeakPagedMemorySize64

            # A zero here is the exit race, not a measurement.
            if ($ws -gt 0) {
                if ($ws -gt $maxWs) { $maxWs = $ws }
                if ($priv -gt $maxPrivate) { $maxPrivate = $priv }
                if ($pws -gt $peakWs) { $peakWs = $pws }
                if ($ppf -gt $peakPagefile) { $peakPagefile = $ppf }

                $samples.Add([pscustomobject]@{
                    ms = [int]$sw.ElapsedMilliseconds
                    working_set = $ws
                    private = $priv
                    peak_working_set = $pws
                })
            }
        } catch {
            break
        }

        if ($DurationSec -gt 0 -and $sw.Elapsed.TotalSeconds -ge $DurationSec) {
            $stoppedByTimer = $true
            break
        }

        Start-Sleep -Milliseconds $PollMs
    }

    if ($stoppedByTimer) {
        # The peak is already banked in $peakWs; killing now only ends the run.
        try { $proc.Kill() } catch {}
    }

    $proc.WaitForExit()
    $sw.Stop()
    $exitCode = if ($stoppedByTimer) { "(stopped at ${DurationSec}s)" } else { $proc.ExitCode }
} finally {
    foreach ($k in $saved.Keys) {
        [Environment]::SetEnvironmentVariable($k, $saved[$k])
    }
}

$mb = 1024.0 * 1024.0
Write-Host ""
Write-Host "[$Label] exit=$exitCode  wall=$([math]::Round($sw.Elapsed.TotalSeconds,1))s  samples=$($samples.Count)"
Write-Host ("[{0}] peak working set   : {1,8:N2} MB" -f $Label, ($peakWs / $mb))
Write-Host ("[{0}] peak private bytes : {1,8:N2} MB" -f $Label, ($peakPagefile / $mb))
Write-Host ("[{0}] max sampled WS     : {1,8:N2} MB" -f $Label, ($maxWs / $mb))
Write-Host ("[{0}] max sampled private: {1,8:N2} MB" -f $Label, ($maxPrivate / $mb))

if ($Csv) {
    $samples | Export-Csv -NoTypeInformation -Path $Csv -Encoding utf8
    Write-Host "[$Label] trace -> $Csv"
}
