<#
.SYNOPSIS
    Build the Windows XP client (win32 + GDI, no SDL) with the embedded server.

.DESCRIPTION
    Produces a standalone torirs.exe for Windows XP:
      * PLATFORM=win32  -> presents through src/platform/platform_win32gdi.c
        (a top-down DIB + BitBlt implementation of the PlatformSDL2 interface),
        no SDL and no GL.
      * EMBED_SERVER=1  -> links the rev-230 server into the client, so it runs
        standalone (no separate game server needed) -- what you want on XP.

    The binary is built HERE (the XP box has no compiler) with a 32-bit MinGW
    toolchain, then copied to dist\win32\torirs.exe for deployment. Deploy + run
    it on XP with the RemoteProxyDesktopXP build server (rpdxpctl).

.PARAMETER Toolchain
    Path to a MinGW 'bin' directory (must contain gcc/g++ and mingw32-make).
    Defaults to the RemoteProxyDesktopXP vendored toolchain if present.

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

# --- locate a MinGW toolchain ---------------------------------------------
if (-not $Toolchain) {
    $vendored = "C:\Users\mrobe\Documents\git_repos\RemoteProxyDesktopXP\toolchain\mingw32\bin"
    if (Test-Path (Join-Path $vendored "gcc.exe")) { $Toolchain = $vendored }
}
if (-not $Toolchain -or -not (Test-Path (Join-Path $Toolchain "gcc.exe"))) {
    Write-Error "No MinGW toolchain found. Pass -Toolchain <path-to-mingw\bin> (needs gcc, g++, mingw32-make)."
    exit 1
}
$env:PATH = "$Toolchain;$env:PATH"
Write-Host "[winxp] toolchain: $Toolchain"
Write-Host "[winxp] gcc: $((& (Join-Path $Toolchain 'gcc.exe') -dumpmachine))"

$make = Join-Path $Toolchain "mingw32-make.exe"
if (-not (Test-Path $make)) { $make = "mingw32-make" }

# --- a POSIX shell for make ------------------------------------------------
# src/makefile's recipes are POSIX sh (rm -rf, mkdir -p, cp, `for` loops). GNU
# make on Windows silently falls back to cmd.exe when there is no sh.exe on
# PATH, and the first recipe then dies with "p was unexpected at this time".
# Git for Windows always ships one; find it rather than making the caller know.
if (-not (Get-Command sh.exe -ErrorAction SilentlyContinue)) {
    # @(...) around the pipeline is load-bearing: Where-Object returns a bare
    # string when exactly one candidate matches, and indexing [0] into a string
    # yields its first character ("C") rather than the path.
    $shDir = @(
        @(
            "C:\Program Files\Git\usr\bin",
            "C:\Program Files (x86)\Git\usr\bin",
            "$env:LOCALAPPDATA\Programs\Git\usr\bin"
        ) | Where-Object { Test-Path (Join-Path $_ "sh.exe") }
    ) | Select-Object -First 1

    if (-not $shDir) {
        Write-Error "No sh.exe on PATH. src/makefile needs a POSIX shell - install Git for Windows, or add a directory containing sh.exe to PATH."
        exit 1
    }
    $env:PATH = "$shDir;$env:PATH"
    Write-Host "[winxp] sh: $shDir\sh.exe"
}

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
    # 5.01, no SDL. These are the failures that otherwise only show up as XP
    # refusing to start the image, with no diagnostic on either side.
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
Write-Host "[winxp] deploy + run on XP with the RemoteProxyDesktopXP build server, e.g.:"
Write-Host "        rpdxpctl push dist\win32\torirs.exe C:\dev\torirs.exe"
