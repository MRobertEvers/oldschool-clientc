<#
.SYNOPSIS
    Build the modern 64-bit Windows client with the embedded server.

.DESCRIPTION
    Produces a standalone x86_64 executable for Windows 10/11:
      * PLATFORM=win64 -> raw Win32 window/input with fixed-function D3D9 by
        default and the same explicit --soft3d GDI fallback as the XP lane.
      * EMBED_SERVER=1 -> links the rev-230 server into the client.

    The committed lib\mingw64-win64-toolchain.zip is extracted on demand to
    the toolchains\mingw64 directory. A pinned official WebView2 SDK is also
    resolved on demand. The standalone folder is staged under dist\win64 with
    torirs.exe, WebView2Loader.dll, and the immutable plugin_chrome bundle.

.PARAMETER Toolchain
    Optional path to an x86_64 MinGW root or bin directory containing gcc,
    mingw32-make, and objdump. By default the repository-owned archive is used.

.PARAMETER Opt
    Release build (OPT=1). Default is a debug build.

.PARAMETER WebView2Sdk
    Optional Microsoft.Web.WebView2 build\native directory (or package root).
    By default version 1.0.4191.47 is downloaded from NuGet and SHA-256 checked.

.EXAMPLE
    .\build_windows.ps1
    .\build_windows.ps1 -Opt
    .\build_windows.ps1 -Toolchain C:\mingw64\bin -Opt
#>
[CmdletBinding()]
param(
    [string]$Toolchain = "",
    [string]$WebView2Sdk = "",
    [switch]$Opt
)

$ErrorActionPreference = "Stop"
$repo = $PSScriptRoot
. (Join-Path $repo "scripts\windows_toolchain.ps1")

# --- repository-owned compiler + POSIX recipe shell -----------------------
$originalPath = $env:PATH
try {
$resolvedToolchain = Resolve-ToriRSWindowsToolchain -RepoRoot $repo -Lane win64 -Override $Toolchain
$webView2 = Resolve-ToriRSWebView2Sdk -RepoRoot $repo -Override $WebView2Sdk
$buildEnvironment = Enable-ToriRSWindowsBuildEnvironment -ToolchainBin $resolvedToolchain.Bin
$make = $resolvedToolchain.Make
Write-Host "[win64] toolchain: $($resolvedToolchain.Bin) ($($resolvedToolchain.Triple))"
Write-Host "[win64] WebView2 SDK: $($webView2.NativeDir) ($($webView2.Version))"
Write-Host "[win64] sh: $($buildEnvironment.Sh)"

# --- build + artifact contract --------------------------------------------
$laneTarget = if ($Opt) { "win64" } else { "win64-debug" }
Write-Host "[win64] building: make -C src EMBED_SERVER=1 $laneTarget"
Push-Location $repo
try {
    & $make -C src EMBED_SERVER=1 CC=gcc "WEBVIEW2_SDK_DIR=$($webView2.NativeDir)" $laneTarget
    if ($LASTEXITCODE -ne 0) {
        throw "build failed (exit $LASTEXITCODE). See errors above."
    }

    & $make -C src --no-print-directory EMBED_SERVER=1 PLATFORM=win64 "WEBVIEW2_SDK_DIR=$($webView2.NativeDir)" lane-check-artifact
    if ($LASTEXITCODE -ne 0) {
        throw "lane-check-artifact failed (exit $LASTEXITCODE) - not staging."
    }
} finally {
    Pop-Location
}

# --- stage the standalone executable --------------------------------------
$built = Join-Path $repo "src\torirs_win64.exe"
if (-not (Test-Path -LiteralPath $built)) {
    throw "build reported success but $built was not found."
}

# Header/import inspection cannot prove that the Windows loader accepts the
# image. Keep this bounded pre-stage launch check: --help exits before window,
# renderer, cache, or embedded-server initialization and intentionally returns
# 1 after printing usage. It caught an invalid PE subsystem stamp that otherwise
# looked correct to both objdump and dumpbin.
$savedErrorActionPreference = $ErrorActionPreference
try {
    # Windows PowerShell 5 promotes native stderr to a terminating
    # NativeCommandError when the script-wide preference is Stop. Usage is
    # intentionally written to stderr, so relax it for this one bounded call.
    $ErrorActionPreference = "Continue"
    $smokeOutput = @(& $built --help 2>&1)
    $smokeExit = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $savedErrorActionPreference
}
if ($smokeExit -ne 1 -or (($smokeOutput -join "`n") -notmatch "(?i)usage")) {
    throw ("win64 launch smoke failed (exit {0}); the PE linked but did not reach main." -f $smokeExit)
}
Write-Host "[win64] launch smoke: ok (--help reached main)"

$dist = Join-Path $repo "dist\win64"
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$staged = Join-Path $dist "torirs.exe"
Copy-Item -LiteralPath $built -Destination $staged -Force
Copy-Item -LiteralPath $webView2.Loader -Destination (Join-Path $dist "WebView2Loader.dll") -Force
Copy-ToriRSPluginChromeBundle -RepoRoot $repo -Destination (Join-Path $dist "plugin_chrome")
Write-Host ("[win64] done -> dist\win64\  (torirs.exe {0:N0} KB + WebView2 loader + local chrome bundle)" -f ((Get-Item -LiteralPath $staged).Length / 1KB))
Write-Host "[win64] local run (fixed-function D3D9):"
Write-Host "        .\dist\win64\torirs.exe --manifest .\manifests/manifest_osrs239.ini"
} finally {
    $env:PATH = $originalPath
}
