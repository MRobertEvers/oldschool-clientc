<#
.SYNOPSIS
    Run torirs from a boot manifest, on Windows.

.DESCRIPTION
    The PowerShell half of run-live.sh: same arguments, same environment
    variables, same result. The manifest (manifest_osrs239_rs2012.ini,
    manifest_osrs230.ini, ...) names the cache, rev, transport, host/port and
    RSA keys, and everything this script decides it reads from there:

      * user/pass come from the manifest's [net:boot] when it carries them,
        falling back to asdf/a. Explicit -User/-Pass arguments win over both.
      * osrs230 / osrs239 without --offline run the in-process server: built
        with EMBED_SERVER=1, TORIRS_TRANSPORT=embed, and MOCK230_REV set from
        the manifest so the embedded world writes the wire the client speaks.
      * a manifest naming a composed cache (cache.osrs239.rs2012,
        cache.osrs239.summoning) is a manifest asking for that bake, so the
        overlay is built before the client runs -- but only when it is stale.
        tools\cache_overlay_stale.py owns that decision and is shared with
        run-live.sh, so the two launchers cannot drift.
      * scripts=...build_summoning selects the Summoning script pack.
      * the OSRS-Content tree is discovered, not demanded: the first checkout
        carrying both ported\ lanes wins, build\ checkouts before the submodule
        (the submodule tracks main, which has the lanes but not their facebake).

    The server script pack is a SEPARATE build from the binary, and an embedded
    server loads whatever script.dat was compiled last -- not what the tree says
    today. Building the binary and not the pack is how a session ends up running
    content nobody has written for weeks, with nothing anywhere reporting the
    mismatch, so the pack is rebuilt here for every embedded run.

    Knobs (all also honoured by run-live.sh):
      TORIRS_NO_BUILD=1        run the existing exe, skip every build
      TORIRS_NO_CACHE_BAKE=1   keep the composed cache as it stands
      TORIRS_FORCE_CACHE_BAKE=1  rebake the composed cache without asking
      TORIRS_PRINT_ONLY=1      print what would run and exit
      TORIRS_TOOLCHAIN         MinGW bin directory
      plus every TORIRS_* the client itself reads (TORIRS_NET_DEBUG=1,
      TORIRS_NET_CHEAT, TORIRS_MAX_FRAMES, TORIRS_EXIT_BMP, ...)

    `run-live.sh web` has no equivalent here: the web lane needs emscripten and
    a POSIX shell throughout. Use Git Bash and run-live.sh for that.

.PARAMETER Manifest
    Path to the boot manifest.

.PARAMETER User
    Login name. Defaults to the manifest's user=, then asdf.

.PARAMETER Pass
    Password. Defaults to the manifest's pass=, then a.

.PARAMETER ContentDir
    The OSRS-Content tree the bakes read. Not normally needed -- when it is
    omitted the tree is discovered: the submodule at OSRS-Content\osrs239-content
    is preferred, then each build\*\osrs239-content clone, and the first one
    carrying both ported\ lanes is taken. Supply this only to override that,
    which is obeyed even when the tree looks wrong (warned about, not
    second-guessed -- mid-port, the caller knows better).

    Why the lanes are the test: mock230-scripts feeds both
    ported\scape2009_summoning and ported\rs2012_qbd_td to sscompile
    unconditionally, so a checkout without them fails on a missing
    all.varbit.compack -- which names nothing about the real problem.

    Sets MOCK230_CONTENT_DIR for the build steps; $env:MOCK230_CONTENT_DIR does
    the same thing and this wins over it.

.PARAMETER ClientArgs
    Everything else, handed to the client verbatim (--soft3d, --opengl3,
    --offline, ...). Just trail them; do NOT separate them with a bare `--`,
    which PowerShell rejects as an ambiguous parameter name before this script
    is ever entered.

.EXAMPLE
    .\run-live.ps1 manifest_osrs239_rs2012.ini
    .\run-live.ps1 manifest_osrs239_rs2012.ini --opengl3
    .\run-live.ps1 manifest_osrs230.ini -User qbdrepro -Pass test --soft3d
    .\run-live.ps1 manifest_osrs239_rs2012.ini -ContentDir some\other\osrs239-content
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)][string]$Manifest,
    [string]$User = '',
    [string]$Pass = '',
    [string]$ContentDir = '',
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$ClientArgs
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = $PSScriptRoot
$exe = Join-Path $repo 'src\torirs_win64.exe'
$make = Join-Path $repo 'make.ps1'

if ($Manifest -eq 'web') {
    Write-Host 'run-live.ps1: the web lane needs emscripten and a POSIX shell throughout.'
    Write-Host '  Use Git Bash:  ./run-live.sh web <manifest.ini> [user] [pass]'
    exit 1
}

if (-not $ClientArgs) { $ClientArgs = @() }

$manifestPath = if ([IO.Path]::IsPathRooted($Manifest)) { $Manifest } else { Join-Path $repo $Manifest }
if (-not (Test-Path -LiteralPath $manifestPath)) {
    Write-Error "run-live.ps1: manifest '$Manifest' not found"
    exit 1
}
$manifestText = Get-Content -LiteralPath $manifestPath

# The manifest is read the same way run-live.sh reads it: first assignment of a
# key wins, whitespace around the `=` is not significant, and a key that is
# absent simply yields nothing.
function Get-ManifestValue([string]$Key) {
    foreach ($line in $manifestText) {
        if ($line -match "^\s*$([regex]::Escape($Key))\s*=\s*(.*?)\s*$") { return $Matches[1] }
    }
    return ''
}

$rev = Get-ManifestValue 'rev'
if (-not $rev) {
    Write-Error "run-live.ps1: no rev= in [net:boot] of '$Manifest'"
    exit 1
}
$cacheDir = Get-ManifestValue 'dir'
$serverScripts = Get-ManifestValue 'scripts'

# ---------------------------------------------------------------- content tree
#
# Every embedded run compiles the server scripts, and mock230-scripts reads both
# ported\ lanes unconditionally -- SUMMONING_CLIENT_LANE and
# RS2012_QBD_TD_CLIENT_LANE in src/makefile, as --pack and --component-root
# arguments to sscompile. A checkout without them does not fail anywhere near the
# tree: it dies on a missing all.varbit.compack several targets deep, which reads
# as a broken working copy rather than the wrong content root.
#
# So the tree is found rather than demanded. The caller should not have to know
# which of several checkouts is the one parked on the porting branch.
$ContentLanes = @('ported\scape2009_summoning', 'ported\rs2012_qbd_td')

function Test-ContentTree([string]$Dir) {
    if (-not (Test-Path -LiteralPath $Dir -PathType Container)) { return $false }
    foreach ($lane in $ContentLanes) {
        if (-not (Test-Path -LiteralPath (Join-Path $Dir $lane) -PathType Container)) { return $false }
    }
    return $true
}

# Checkouts under build\ first, the submodule last.
#
# Carrying the lane DIRECTORIES is not the same as carrying the lane's current
# bake, and this is the order that tells them apart. A tree under build\ is a
# deliberately provisioned lane checkout -- typically a worktree parked on a
# facebake branch (build\osrs-content-rs2012 sits on rs2012-facebake-v10-m60).
# The submodule tracks main, which receives the ported/ lane directories but not
# the rebaked models: on 2026-08-12 upstream main gained ported/rs2012_qbd_td
# while the 596 rebaked .ob3 stayed on the facebake branch. Preferring the
# submodule there picked lane-shaped content with pre-facebake models and said
# nothing, which is the one failure this discovery must not produce.
#
# build\ is gitignored, so these never collide with the tree git tracks, and the
# submodule remains the fallback for anyone who has no build\ checkout at all.
function Get-ContentCandidates {
    $candidates = @()
    $buildRoot = Join-Path $repo 'build'
    if (Test-Path -LiteralPath $buildRoot) {
        $candidates += Get-ChildItem -LiteralPath $buildRoot -Directory |
            Sort-Object Name |
            ForEach-Object { Join-Path $_.FullName 'osrs239-content' }
    }
    $candidates += Join-Path $repo 'OSRS-Content\osrs239-content'
    return $candidates
}

function Set-ContentTree([string]$Dir) {
    # Absolute and forward-slashed: the recipes hand this to sh and to python,
    # neither of which wants a Windows-relative path.
    $resolved = ([IO.Path]::GetFullPath($Dir)) -replace '\\', '/'

    # Two variables, one tree, and they must not disagree.
    #
    # MOCK230_CONTENT_DIR is the BUILD side: src/makefile reads it to decide
    # which tree to compile the server script pack from and to stage the cache
    # overlay out of. MOCK230_CONTENT is the RUNTIME side: mock230_boot.c's
    # resolve_content_dir() reads it, and with it unset falls back to a
    # hardcoded "OSRS-Content/osrs239-content" -- the submodule, whatever tree
    # the build just used.
    #
    # Setting only the first is how a run compiles script.dat into the build\
    # worktree and then boots the submodule's months-old copy. That is not
    # hypothetical: it is why `::rs2012qbdmanifest` answered "Unknown command"
    # -- the debugproc was in the pack that had just been built (7.2MB, today)
    # and absent from the pack that was loaded (6.7MB, a week older). Nothing
    # reported the mismatch, because from the build's point of view everything
    # succeeded. content_dir also resolves the world's server/pack reads, so
    # the same split silently feeds the world pre-bake npc/loc records.
    $env:MOCK230_CONTENT_DIR = $resolved
    $env:MOCK230_CONTENT = $resolved
}

# An explicit choice is obeyed even when it looks wrong -- warned about, not
# overridden, because the caller may be mid-port and know better than this check.
$contentChoice = ''
if ($ContentDir) {
    $resolved = if ([IO.Path]::IsPathRooted($ContentDir)) { $ContentDir } else { Join-Path $repo $ContentDir }
    if (-not (Test-Path -LiteralPath $resolved -PathType Container)) {
        Write-Error "run-live.ps1: -ContentDir '$ContentDir' does not exist"
        exit 1
    }
    Set-ContentTree $resolved
    $contentChoice = '-ContentDir'
} elseif ($env:MOCK230_CONTENT_DIR) {
    Set-ContentTree $env:MOCK230_CONTENT_DIR
    $contentChoice = 'MOCK230_CONTENT_DIR'
} else {
    foreach ($candidate in Get-ContentCandidates) {
        if (Test-ContentTree $candidate) {
            Set-ContentTree $candidate
            $contentChoice = 'auto'
            break
        }
    }
}
if ($contentChoice -ne 'auto' -and $env:MOCK230_CONTENT_DIR -and
    -not (Test-ContentTree $env:MOCK230_CONTENT_DIR)) {
    Write-Host ("run-live.ps1: $contentChoice tree $env:MOCK230_CONTENT_DIR is missing " +
        "$($ContentLanes -join ' / ') -- the bakes will likely fail") -ForegroundColor Yellow
}

# A self-contained manifest may carry development credentials. Preserve the
# historical asdf/a fallback, while letting explicit arguments win.
if (-not $User) { $User = Get-ManifestValue 'user' }
if (-not $User) { $User = 'asdf' }
if (-not $Pass) { $Pass = Get-ManifestValue 'pass' }
if (-not $Pass) { $Pass = 'a' }

# --offline never logs in, so it never wants the embedded server.
$offline = [bool]($ClientArgs -contains '--offline')
$embed = (-not $offline) -and ($rev -in @('osrs230', 'osrs239'))
if ($embed) {
    $env:TORIRS_TRANSPORT = 'embed'
    # Embed defaults to osrs230 unless told otherwise; keep server wire = client rev.
    if (-not $env:MOCK230_REV) { $env:MOCK230_REV = $rev }
}

$clientArgv = @('--manifest', $manifestPath, '--user', $User, '--pass', $Pass) + $ClientArgs

if ($env:TORIRS_PRINT_ONLY -eq '1') {
    Write-Host "manifest        : $manifestPath"
    Write-Host "rev             : $rev"
    Write-Host "cache           : $cacheDir"
    Write-Host "content tree    : $(if ($env:MOCK230_CONTENT_DIR) { "$env:MOCK230_CONTENT_DIR ($contentChoice)" } else { 'none found' })"
    Write-Host "user/pass       : $User / $Pass"
    Write-Host "offline         : $([int]$offline)"
    Write-Host "embedded server : $([int]$embed)"
    if ($embed) { Write-Host "TORIRS_TRANSPORT: $env:TORIRS_TRANSPORT  MOCK230_REV: $env:MOCK230_REV" }
    Write-Host "argv            : $exe $($clientArgv -join ' ')"
    exit 0
}

# make.ps1 parses its own flags out of a raw argument list rather than declaring
# them, so they are passed positionally, ahead of the targets.
function Invoke-Make {
    param([string[]]$Targets, [switch]$Parallel, [switch]$EmbedServer)
    $argv = @()
    if ($Parallel) { $argv += '-j' }
    if ($EmbedServer) { $argv += '-Embed' }
    $argv += $Targets
    & $make @argv
    if ($LASTEXITCODE -ne 0) {
        Write-Error "run-live.ps1: 'make $($Targets -join ' ')' failed (exit $LASTEXITCODE)"
        exit $LASTEXITCODE
    }
}

# Build-CacheOverlay runs before the Push-Location further down, so the cwd is
# still the caller's. Every path handed to the predicate is absolutised here
# rather than relying on one.
function Resolve-RepoPath([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return $Path }
    return (Join-Path $repo $Path)
}

# BootManifest_LoadFile resolves a manifest's cache path against the MANIFEST's
# directory, not the repo root -- the same rule run-live.sh's manifest_path()
# applies. The predicate has to stat the cache the client will actually boot.
function Resolve-CachePath([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return $Path }
    return (Join-Path (Split-Path -Parent $manifestPath) $Path)
}

# tools\cache_overlay_stale.py is the only implementation of this predicate and
# is shared with run-live.sh, so the two launchers cannot drift.
#
# Anything other than exit 1 bakes. A predicate that could not answer -- no
# interpreter on PATH, an exception inside it -- must never be read as "up to
# date": the failure mode of a needless bake is two minutes, and the failure
# mode of a skipped one is a session spent debugging content that was never in
# the cache. TORIRS_FORCE_CACHE_BAKE=1 is read by the script itself, so there is
# no branch for it here.
function Test-CacheOverlayFresh {
    param([string]$Lane, [string]$Base, [string]$Stager)

    # Function-scoped, and deliberate: a non-zero exit is this predicate's
    # ANSWER, not a failure, and PowerShell 7.4+ raises native non-zero exits as
    # terminating errors under $ErrorActionPreference = 'Stop'.
    $ErrorActionPreference = 'Continue'

    $py = Get-Command python3 -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $py) {
        $py = Get-Command python -CommandType Application -ErrorAction SilentlyContinue |
            Select-Object -First 1
    }
    if (-not $py) {
        Write-Host 'run-live.ps1: no python3 on PATH -- baking rather than assuming the cache is fresh' -ForegroundColor Yellow
        return $false
    }

    # --tree, which run-live.sh also passes: the script's own default is the
    # OSRS-Content submodule, and the whole point of the discovery above is that
    # the tree in use is often a build\ checkout instead. Left to the default the
    # predicate watches a tree nobody is building from and answers "fresh" for a
    # lane that moved.
    $treeArgs = @()
    if ($env:MOCK230_CONTENT_DIR) { $treeArgs = @('--tree', $env:MOCK230_CONTENT_DIR) }

    $global:LASTEXITCODE = 0
    & $py.Source (Join-Path $repo 'tools\cache_overlay_stale.py') `
        --cache (Resolve-CachePath $cacheDir) --lane $Lane `
        --base (Resolve-RepoPath $Base) @treeArgs `
        --input (Resolve-RepoPath $Stager) `
        --input (Resolve-RepoPath 'src\makefile') `
        --input (Resolve-RepoPath '3rd\rscache\tools\cachepack')
    return ($LASTEXITCODE -eq 1)
}

# A composed cache is deleted and repacked from scratch, which takes minutes and
# tears the cache out from under anything else reading it (a second client, an
# osrsify search wave). TORIRS_NO_CACHE_BAKE=1 runs against the cache as it
# already stands -- the right choice while iterating on C or on scripts, and the
# wrong one the moment the content tree changed.
#
# Those targets are .PHONY, though, so asking unconditionally spends the full
# copy-repack-verify on every single launch. Ask the predicate `make` would
# apply if the target were a real file instead.
#
# The bases match the makefile's `?=` defaults and honour the same overrides: a
# caller that moves SUMMONING_CACHE_BASE must not leave the freshness check
# watching a cache the bake no longer reads.
function Build-CacheOverlay {
    $summoningBase = if ($env:SUMMONING_CACHE_BASE) { $env:SUMMONING_CACHE_BASE } else { 'cache.osrs239.baked' }
    $rs2012Base = if ($env:RS2012_CACHE_BASE) { $env:RS2012_CACHE_BASE } else { 'cache.osrs239' }

    $lane = switch -Wildcard ($cacheDir) {
        '*cache.osrs239.summoning' {
            @{
                Label   = 'Summoning'
                Lane    = 'scape2009_summoning'
                Base    = $summoningBase
                Stager  = 'tools\stage_summoning_overlay.py'
                Targets = @('mock230-cache-summoning')
            }
            break
        }
        # The QBD/TD lane. mock230-servpack is the server half of the same tree
        # (the npc/loc server fields the boot reads out of <content>/server/pack);
        # without it the world falls back to a text parse of content the bake has
        # already moved.
        '*cache.osrs239.rs2012' {
            @{
                Label   = 'RS2012 QBD/TD'
                Lane    = 'rs2012_qbd_td'
                Base    = $rs2012Base
                Stager  = 'tools\stage_rs2012_overlay.py'
                Targets = @('mock230-cache-rs2012', 'mock230-servpack')
            }
            break
        }
        default { $null }
    }
    if (-not $lane) { return }

    if ($env:TORIRS_NO_CACHE_BAKE -eq '1') {
        Write-Host "run-live.ps1: TORIRS_NO_CACHE_BAKE=1 -- using $cacheDir as it stands" -ForegroundColor Yellow
        return
    }

    if (Test-CacheOverlayFresh -Lane $lane.Lane -Base $lane.Base -Stager $lane.Stager) {
        Write-Host "run-live.ps1: $($lane.Label) cache overlay is up to date (TORIRS_FORCE_CACHE_BAKE=1 to rebake)" -ForegroundColor Yellow
        return
    }

    Write-Host "run-live.ps1: baking $cacheDir ($($lane.Targets -join ' '))..." -ForegroundColor Cyan
    Invoke-Make -Targets $lane.Targets
}

function Build-Scripts {
    $target = if ($serverScripts -like '*build_summoning') { 'mock230-scripts-summoning' } else { 'mock230-scripts' }
    Write-Host "run-live.ps1: building the server script pack ($target)..." -ForegroundColor Cyan
    Invoke-Make -Targets @($target)
}

if ($env:TORIRS_NO_BUILD -ne '1') {
    if ($embed) {
        # Only the builds need a content tree, so this is checked here and not at
        # startup: TORIRS_NO_BUILD=1 and --offline are both legitimate ways to run
        # without one.
        if (-not $env:MOCK230_CONTENT_DIR) {
            Write-Host "run-live.ps1: no OSRS-Content tree carrying $($ContentLanes -join ' and ')." -ForegroundColor Red
            Write-Host '  Looked at:' -ForegroundColor Red
            foreach ($candidate in Get-ContentCandidates) { Write-Host "    $candidate" -ForegroundColor Red }
            Write-Host '  Point -ContentDir (or MOCK230_CONTENT_DIR) at the checkout that has them.' -ForegroundColor Red
            exit 1
        }
        Write-Host "run-live.ps1: content tree ($contentChoice): $env:MOCK230_CONTENT_DIR" -ForegroundColor Cyan
        Write-Host "run-live.ps1: $rev -- building with EMBED_SERVER=1 (in-process server, MOCK230_REV=$env:MOCK230_REV)" -ForegroundColor Cyan
        Build-CacheOverlay
        Build-Scripts
        Invoke-Make -Targets @('win64') -Parallel -EmbedServer
    } elseif (-not (Test-Path -LiteralPath $exe)) {
        Write-Host "run-live.ps1: building $exe..." -ForegroundColor Cyan
        Invoke-Make -Targets @('win64') -Parallel
    }
}

if (-not (Test-Path -LiteralPath $exe)) {
    Write-Error "run-live.ps1: $exe not found -- build it with .\build_windows.ps1 -Opt"
    exit 1
}

Push-Location $repo
try {
    & $exe @clientArgv
    $rc = $LASTEXITCODE
} finally {
    Pop-Location
}
exit $rc
