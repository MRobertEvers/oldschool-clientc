<#
.SYNOPSIS
    Run src/makefile targets on Windows without setting up a shell first.

.DESCRIPTION
    `make -C src <target>` needs two things this repository does not assume are
    installed: an x86_64 MinGW toolchain, and a real sh.exe, because the
    makefile's recipes are POSIX. This wrapper supplies both -- the compiler is
    extracted on demand from the committed lib\mingw64-win64-toolchain.zip, and
    Git for Windows' sh.exe is put on PATH -- then hands everything through to
    mingw32-make with -C src and CC=gcc already applied.

    So:

        .\make.ps1 win64                 =  make -C src CC=gcc win64
        .\make.ps1 -j win64              =  ... -j<cores>
        .\make.ps1 -Embed win64          =  ... EMBED_SERVER=1 win64
        .\make.ps1 mock230-cache-rs2012 MOCK230_CONTENT_DIR=/c/tree

    Both defaults are only defaults: an explicit CC= among the arguments is left
    alone, and -Directory aims it somewhere other than src. Anything this script
    does not recognise -- targets, VAR=value assignments, make's own flags like
    -n or -B -- goes through untouched.

    Parallelism is OFF unless asked for. The compile lanes (win64, torirs, all)
    are safe under -j; the content bakes are not -- mock230-cache-rs2012 and
    mock230-cache-summoning have prerequisites that each rebuild the shared
    cachepack binary, and racing those corrupts the tool mid-link.

    web / web-debug / web-idb / web-idb-debug are a different lane, detected by
    target name (or an explicit PLATFORM=web) rather than a flag: the CC=gcc
    this script otherwise always injects is skipped for them, because src's own
    `CC := $(PLATFORM_CC)` (a plain, command-line-overridable assignment) would
    otherwise lose to it and the emcc sub-make would try to link wasm with gcc.
    Instead both the repo's MinGW toolchain (servers: io_server/js5_server are
    always native) and its emsdk toolchain (scripts\windows_emscripten_toolchain.ps1)
    go on PATH together, and CC is left for src/platform/platform.mk to pick per
    sub-make: gcc for the native servers, emcc for the PLATFORM=web half.

    Flags:
      -j            parallel, one job per processor
      -j<N>, -Jobs N   parallel with an explicit job count
      -Embed        add EMBED_SERVER=1 (link the mock server into the client)
      -Directory D  build in D instead of src
      -Toolchain T  a MinGW bin directory (or root) instead of the repo's own;
                    defaults to $env:TORIRS_TOOLCHAIN
      -EmToolchain T  an emsdk-win64-shaped root instead of the repo's own;
                    defaults to $env:TORIRS_EM_TOOLCHAIN. Only resolved for the
                    web targets.

.EXAMPLE
    .\make.ps1 -j win64
    .\make.ps1 -Embed -j win64
    .\make.ps1 mock230-scripts
    .\make.ps1 -Directory 3rd\rscache\tools cachepack
    .\make.ps1 -n mock230-servpack
    .\make.ps1 web
#>

# No param() block at all, deliberately: every token reaches $args verbatim and
# this script picks its own flags out by hand. Declaring parameters loses make's
# flags to PowerShell's binder before the script is entered -- `--version` binds
# as a value to -Jobs and dies on an int conversion, and a `[Parameter()]`
# attribute makes this an advanced function, whose common parameters then eat
# `-d` as a prefix of -Debug. Both produce errors that name nothing real.
$Arguments = @($args)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = $PSScriptRoot
. (Join-Path $repo 'scripts\windows_toolchain.ps1')
. (Join-Path $repo 'scripts\windows_emscripten_toolchain.ps1')

$cores = [int]$env:NUMBER_OF_PROCESSORS
if ($cores -lt 1) { $cores = 4 }

$jobs = 1
$embed = $false
$directory = 'src'
$toolchain = if ($env:TORIRS_TOOLCHAIN) { $env:TORIRS_TOOLCHAIN } else { '' }
$emToolchain = if ($env:TORIRS_EM_TOOLCHAIN) { $env:TORIRS_EM_TOOLCHAIN } else { '' }
$makeArgs = @()

if (-not $Arguments) { $Arguments = @() }
# A flag that takes a value consumes the next argument by advancing $i here, in
# the loop's own scope. Do NOT factor that into a helper scriptblock: `&` runs
# one in a child scope, so the index it advances is a copy -- the value reads
# back correctly and is ALSO left behind, to reach make as a stray target.
for ($i = 0; $i -lt $Arguments.Count; $i++) {
    $arg = $Arguments[$i]
    $needsValue = $arg -match '^-(jobs|d|dir|directory|t|toolchain|emtoolchain)$'
    if ($needsValue -and $i + 1 -ge $Arguments.Count) {
        Write-Host "make.ps1: $arg needs a value" -ForegroundColor Red
        exit 1
    }
    switch -Regex ($arg) {
        '^-(j|parallel)$'        { $jobs = $cores }
        '^-j(?<n>\d+)$'          { $jobs = [int]$Matches['n'] }
        '^-jobs$'                { $jobs = [int]$Arguments[$i + 1] }
        '^-(embed|embedserver)$' { $embed = $true }
        '^-(d|dir|directory)$'   { $directory = $Arguments[$i + 1] }
        '^-(t|toolchain)$'       { $toolchain = $Arguments[$i + 1] }
        '^-emtoolchain$'         { $emToolchain = $Arguments[$i + 1] }
        '^(-h|-\?|-help)$'       { Get-Help -Full $PSCommandPath; exit 0 }
        default                  { $makeArgs += $arg }
    }
    if ($needsValue) { $i++ }
}

# Detected by target name (or an explicit PLATFORM=web escape hatch for callers
# who build the web lane through `all` directly) rather than a flag, so a plain
# `.\make.ps1 web` behaves the same as every other lane's plain target name.
$isWebBuild = [bool]($makeArgs | Where-Object { $_ -match '^web(-debug|-idb|-idb-debug)?$' -or $_ -eq 'PLATFORM=web' })

$originalPath = $env:PATH
try {
    $tc = Resolve-ToriRSWindowsToolchain -RepoRoot $repo -Lane win64 -Override $toolchain
    Enable-ToriRSWindowsBuildEnvironment -ToolchainBin $tc.Bin | Out-Null

    if ($isWebBuild) {
        # `web` builds native servers (gcc, via the same MinGW toolchain just
        # enabled above) before the PLATFORM=web sub-make (emcc) -- both need
        # to be reachable across the one `make -C src web` invocation.
        $tcEm = Resolve-ToriRSEmscriptenToolchain -RepoRoot $repo -Override $emToolchain
        $emConfigPath = Join-Path $tcEm.Root '.emscripten'
        Enable-ToriRSEmscriptenBuildEnvironment -Toolchain $tcEm -EmConfigPath $emConfigPath | Out-Null
    }

    $argv = @('-C', $directory)
    if ($jobs -gt 1) { $argv += "-j$jobs" }
    # An explicit CC= anywhere in the arguments is the caller's decision; the
    # host-tool submakes pass their own and must not be second-guessed either.
    # For a web build, injecting CC=gcc here at all is the bug: src\makefile's
    # `CC := $(PLATFORM_CC)` is a plain assignment, which GNU Make command-line
    # variables always beat, so a command-line CC=gcc would survive into the
    # PLATFORM=web sub-make (via inherited MAKEFLAGS) and force gcc onto wasm
    # sources. Leaving CC unset lets platform.mk choose per sub-make: gcc for
    # PLATFORM=native (its own CC default when CC's origin is unset), emcc for
    # PLATFORM=web (hardcoded there regardless of CC's origin).
    if (-not $isWebBuild -and -not ($makeArgs | Where-Object { $_ -like 'CC=*' })) { $argv += 'CC=gcc' }
    if ($embed) { $argv += 'EMBED_SERVER=1' }
    $argv += $makeArgs

    Write-Host "make.ps1: $($tc.Make) $($argv -join ' ')" -ForegroundColor DarkGray
    Push-Location $repo
    # Windows PowerShell 5.1 wraps a native command's stderr in ErrorRecords the
    # moment its output is redirected or piped, and under -ErrorActionPreference
    # Stop that TERMINATES -- so `.\make.ps1 win64 > build.log` would abort on
    # the first gcc warning while make itself was succeeding. The exit code is
    # the only thing that decides whether the build failed.
    $saved = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $tc.Make @argv
        $rc = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $saved
        Pop-Location
    }
    if ($rc -ne 0) {
        Write-Host "make.ps1: make failed (exit $rc)" -ForegroundColor Red
        exit $rc
    }
    exit 0
} finally {
    $env:PATH = $originalPath
}
