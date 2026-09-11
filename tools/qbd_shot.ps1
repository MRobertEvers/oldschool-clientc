<#
.SYNOPSIS
    Headless visual harness for the RS2012 QBD arena.

.DESCRIPTION
    Boots the client into the arena, parks the follow camera at a chosen angle,
    captures one frame or a film strip, and converts the BMPs to PNG so they can
    be looked at directly.

    Why a script rather than a command: framing the dragon needs the camera
    pinned (TORIRS_ORBIT_CAM), the wake sequence needs a frame budget, and a
    single frame cannot catch an animation whose start jitters with login time —
    so the useful capture is a strip. Doing that by hand each time is how you
    end up screenshotting the same empty platform repeatedly.

    Boot to first arena frame is ~14 s, so a one-frame capture costs about that
    plus the frames you ask for.

.PARAMETER Yaw
    Follow-camera yaw, 0..2047. Sweep this to find the subject.

.PARAMETER Pitch
    Follow-camera pitch, 128..383. Higher looks down from further up.

.PARAMETER Zoom
    Follow distance as a percentage. Below 100 moves the eye closer.

.PARAMETER Frames
    Total frames to run before exiting.

.PARAMETER Strip
    Capture a film strip instead of one frame: "start,step,count".

.PARAMETER Out
    Output directory. Defaults to build/qbd_shots.

.PARAMETER Manifest
    Boot manifest. Defaults to the RS2012 lane.

.PARAMETER TexDebug
    Also print the raster texture counters (kernel usage, texture misses).

.EXAMPLE
    .\tools\qbd_shot.ps1 -Yaw 1024 -Frames 700
    .\tools\qbd_shot.ps1 -Yaw 512 -Pitch 300 -Strip "600,40,8"
    .\tools\qbd_shot.ps1 -Sweep
#>
[CmdletBinding()]
param(
    [int]$Yaw = 0,
    [int]$Pitch = -1,
    [int]$Zoom = 0,
    [int]$Frames = 700,
    [string]$Strip = "",
    [string]$Out = "build/qbd_shots",
    [string]$Manifest = "manifests/manifest_osrs239_rs2012.ini",
    [switch]$TexDebug,
    [switch]$Sweep,
    [string]$Exe = "dist\win64\torirs.exe"
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
Push-Location $repo
try {
    if (-not (Test-Path $Exe)) { throw "no client at $Exe - build it first" }
    New-Item -ItemType Directory -Force -Path $Out | Out-Null

    # A sweep is just the single-shot path at eight yaws; run it and stop.
    if ($Sweep) {
        foreach ($y in 0, 256, 512, 768, 1024, 1280, 1536, 1792) {
            & $PSCommandPath -Yaw $y -Pitch $Pitch -Zoom $Zoom -Frames $Frames `
                -Out (Join-Path $Out "yaw$y") -Manifest $Manifest -Exe $Exe
        }
        return
    }

    $cam = "$Yaw"
    if ($Pitch -ge 0) { $cam += ",$Pitch" } elseif ($Zoom -gt 0) { $cam += ",-1" }
    if ($Zoom -gt 0) { $cam += ",$Zoom" }

    $env:TORIRS_TRANSPORT = "embed"
    $env:TORIRSSERVER_REV = "osrs239"
    $env:TORIRS_ORBIT_CAM = $cam
    $env:TORIRS_MAX_FRAMES = "$Frames"
    if ($TexDebug) { $env:TORIRS_RASTER_TEX_DEBUG = "1" }
    else { Remove-Item Env:TORIRS_RASTER_TEX_DEBUG -ErrorAction SilentlyContinue }

    $bmpDir = Join-Path $Out "bmp"
    New-Item -ItemType Directory -Force -Path $bmpDir | Out-Null
    Get-ChildItem $bmpDir -Filter *.bmp -ErrorAction SilentlyContinue | Remove-Item -Force

    if ($Strip) {
        $env:TORIRS_BMP_SERIES = "$bmpDir,$Strip"
        Remove-Item Env:TORIRS_EXIT_BMP -ErrorAction SilentlyContinue
    } else {
        Remove-Item Env:TORIRS_BMP_SERIES -ErrorAction SilentlyContinue
        $env:TORIRS_EXIT_BMP = (Join-Path $bmpDir "frame.bmp")
    }

    $log = Join-Path $Out "run.log"
    Write-Host "[qbd_shot] cam=$cam frames=$Frames -> $Out"
    $sw = [Diagnostics.Stopwatch]::StartNew()
    # stderr is the client's normal channel; PowerShell would promote it to a
    # terminating error under the script-wide preference.
    $savedPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & ".\$Exe" --manifest ".\$Manifest" --soft3d 2>&1 | Out-File -Encoding utf8 $log
    $code = $LASTEXITCODE
    $ErrorActionPreference = $savedPreference
    $sw.Stop()
    Write-Host ("[qbd_shot] exit={0} in {1:N1}s" -f $code, $sw.Elapsed.TotalSeconds)

    Add-Type -AssemblyName System.Drawing
    $shots = @()
    foreach ($bmp in Get-ChildItem $bmpDir -Filter *.bmp | Sort-Object Name) {
        $png = Join-Path $Out ($bmp.BaseName + ".png")
        $img = [System.Drawing.Bitmap]::FromFile($bmp.FullName)
        $img.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
        $img.Dispose()
        $shots += $png
    }
    if (-not $shots) { Write-Host "[qbd_shot] NO FRAMES CAPTURED - see $log"; return }
    Write-Host "[qbd_shot] $($shots.Count) png(s):"
    $shots | ForEach-Object { Write-Host "  $_" }

    if ($TexDebug) {
        $skips = (Select-String -Path $log -Pattern 'raster_tex_skip' | Measure-Object).Count
        $alpha = Select-String -Path $log -Pattern 'raster_tex_alpha' | Select-Object -Last 1
        Write-Host "[qbd_shot] texture misses: $skips"
        if ($alpha) { Write-Host "[qbd_shot] $($alpha.Line)" }
    }
}
finally { Pop-Location }
