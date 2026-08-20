<#
.SYNOPSIS
    Build the Windows XP client (raw Win32 + fixed-function D3D9, no SDL) with the embedded server.

.DESCRIPTION
    Produces a standalone torirs.exe for Windows XP:
      * PLATFORM=win32  -> uses classic Direct3D 9 fixed-function rendering by
        default, with no D3D9Ex, D3DX, shaders, SDL, or GL. Pass --soft3d at
        runtime for the top-down DIB + GDI fallback in platform_win32gdi.c.
      * EMBED_SERVER=1  -> links the rev-230 server into the client, so it runs
        standalone (no separate game server needed) -- what you want on XP.

    The binary is built HERE (the XP box has no compiler) with a 32-bit MinGW
    toolchain, then copied to dist\win32\torirs.exe for deployment. Deploy + run
    it on XP with the RemoteProxyDesktopXP build server (rpdxpctl).

.PARAMETER Toolchain
    Path to a MinGW root or 'bin' directory (must contain gcc, mingw32-make,
    and objdump).
    Defaults to the repository-owned lib\mingw32-win32-toolchain.zip, extracted
    on demand under the ignored toolchain\mingw32 directory.

.PARAMETER Opt
    Release build (OPT=1). Default is a debug build.

.EXAMPLE
    .\build_winxp.ps1
    .\build_winxp.ps1 -Opt
    .\build_winxp.ps1 -Toolchain C:\mingw32\bin
#>
[CmdletBinding()]
param(
    [string]$Toolchain = "",
    [switch]$Opt
)

$ErrorActionPreference = "Stop"
$repo = $PSScriptRoot
. (Join-Path $repo "scripts\windows_toolchain.ps1")

# --- repository-owned compiler + POSIX recipe shell -----------------------
$originalPath = $env:PATH
try {
$resolvedToolchain = Resolve-ToriRSWindowsToolchain -RepoRoot $repo -Lane win32 -Override $Toolchain
$buildEnvironment = Enable-ToriRSWindowsBuildEnvironment -ToolchainBin $resolvedToolchain.Bin
$Toolchain = $resolvedToolchain.Bin
$make = $resolvedToolchain.Make
Write-Host "[winxp] toolchain: $Toolchain ($($resolvedToolchain.Triple))"
Write-Host "[winxp] sh: $($buildEnvironment.Sh)"

# --- build ----------------------------------------------------------------
# All build knowledge lives in the makefile's lane targets: `winxp` is
# PLATFORM=win32 OPT=1 and `winxp-debug` is OPT=0, and both run `lane-check`
# first so a wrong-architecture toolchain fails here with one line rather than
# as an unexplained loader error on the XP box. This script only puts a
# toolchain on PATH and stages the result -- if you find yourself adding a
# compiler flag here, it belongs in src/platform/platform.mk instead.
#
# EMBED_SERVER=1 links the rev-230 server in so the exe runs standalone. It is
# a build flavor rather than a platform property, which is why it is set here
# and not in the lane. Passing it on the command line propagates to the
# makefile's own sub-makes through MAKEFLAGS.
$laneTarget = if ($Opt) { "winxp" } else { "winxp-debug" }
Write-Host "[winxp] building: make -C src EMBED_SERVER=1 $laneTarget"
Push-Location $repo
try {
    & $make -C src EMBED_SERVER=1 CC=gcc $laneTarget
    if ($LASTEXITCODE -ne 0) {
        Write-Error "build failed (exit $LASTEXITCODE). See errors above."
        exit $LASTEXITCODE
    }

    # Read the XP contract back off the binary: 32-bit PE, subsystem version
    # 5.01, classic d3d9.dll!Direct3DCreate9, and no Ex/D3DX/shader/SDL imports.
    # These are the failures that otherwise only show up on the XP target.
    & $make -C src --no-print-directory EMBED_SERVER=1 PLATFORM=win32 lane-check-artifact
    if ($LASTEXITCODE -ne 0) {
        Write-Error "lane-check-artifact failed (exit $LASTEXITCODE) - not staging."
        exit $LASTEXITCODE
    }
} finally { Pop-Location }

# --- stage the exe for deployment -----------------------------------------
# PLATFORM_TARGET is a bare 'torirs.exe', so the link lands in src\ next to the
# makefile -- build_win32*\ holds only objects.
$built = Join-Path $repo "src\torirs.exe"
if (-not (Test-Path $built)) {
    Write-Error "build reported success but $built was not found."
    exit 1
}
$dist = Join-Path $repo "dist\win32"
New-Item -ItemType Directory -Force $dist | Out-Null
Copy-Item $built (Join-Path $dist "torirs.exe") -Force
Write-Host ("[winxp] done -> dist\win32\torirs.exe  ({0:N0} KB)" -f ((Get-Item (Join-Path $dist 'torirs.exe')).Length/1KB))
Write-Host "[winxp] local run (nonpacked OSRS239, fixed-function D3D9):"
Write-Host "        .\src\torirs.exe --manifest .\manifests/manifest_osrs239.ini"
Write-Host "[winxp] deploy + run on XP with the RemoteProxyDesktopXP build server, e.g.:"
Write-Host "        rpdxpctl push dist\win32\torirs.exe C:\dev\torirs.exe"
} finally {
    $env:PATH = $originalPath
}
