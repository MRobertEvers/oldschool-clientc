<#
.SYNOPSIS
    Run torirs from a boot manifest, natively or in a browser, on Windows.

.DESCRIPTION
    The PowerShell twin of run-live.sh: same arguments, same environment
    variables, same result. Every argument is positional, exactly as
    run-live.sh takes them with $1/shift -- there is no -User/-Pass/-Manifest
    parameter to bind against, on purpose. An earlier version of this script
    declared $User/$Pass as named parameters without an explicit -Position,
    which PowerShell still binds positionally in declaration order; in `web`
    mode that silently ate the real manifest argument (it landed on $User
    instead), which is why run-live.bat has always converted its own
    positional user/pass into `-User`/`-Pass` before calling this script.
    Parsing one raw argument array by hand, the same way the shell script
    shifts $1, removes that whole class of surprise.

    The manifest (manifest_osrs239_rs2012.ini, manifest_osrs230.ini, ...)
    names the cache, rev, transport, host/port and RSA keys, and everything
    this script decides it reads from there:

      * user/pass come from the manifest's [net:boot] when it carries them,
        falling back to asdf/a. An explicit positional user/pass argument
        wins over both.
      * osrs230 / osrs239 without --offline run the in-process server in
        native mode: built with EMBED_SERVER=1, TORIRS_TRANSPORT=embed, and
        MOCK230_REV set from the manifest so the embedded world writes the
        wire the client speaks. A web run cannot do this -- the wasm module
        has no host filesystem for the embedded server's cache, content tree,
        or script pack -- so it starts a native mock230 child instead, and
        the browser reaches it over WebSocket.
      * a manifest naming a composed cache (cache.osrs239.rs2012,
        cache.osrs239.summoning) is a manifest asking for that bake, so the
        overlay is built before the client runs -- but only when it is stale.
        tools\cache_overlay_stale.py owns that decision and is shared with
        run-live.sh, so the two launchers cannot drift.
      * [content:lanes] names the content lanes the script pack is compiled
        from; scripts= says where that pack lands.
      * the OSRS-Content tree is discovered, not demanded: the first checkout
        carrying both ported\ lanes wins, build\ checkouts before the submodule
        (the submodule tracks main, which has the lanes but not their facebake).
        Override it with $env:MOCK230_CONTENT_DIR, the same variable
        run-live.sh honours -- there is no -ContentDir flag here either, for
        the same reason there is no -User/-Pass.

    The server script pack is a SEPARATE build from the binary, and an embedded
    or mock server loads whatever script.dat was compiled last -- not what the
    tree says today. Building the binary and not the pack is how a session ends
    up running content nobody has written for weeks, with nothing anywhere
    reporting the mismatch, so the pack is checked here for every native-embed
    or web-mock230 run and rebuilt when anything sscompile reads is newer than
    it (tools/server_scripts_stale.py).

    `run-live.ps1 web <manifest.ini> [user] [pass] [client args...]` runs the
    emscripten build instead of the native exe, matching
    `run-live.sh web <manifest.ini> [user] [pass] [client args...]` exactly
    now -- including that user/pass are positional in web mode too. The
    client is the same program with the same command line -- it just arrives
    through the URL rather than argv, and its cache reads are answered by the
    IO server this script starts. Every TORIRS_* variable in the environment
    is forwarded the same way, so a run differs from the native one only in
    where the pixels land. See docs/web_build.md.

    Knobs (all also honoured by run-live.sh unless noted):
      TORIRS_NO_BUILD=1        run the existing exe/lane, skip every build
      TORIRS_SKIP_CHECKS=1     use cache and server script pack as they stand
      TORIRS_NO_CACHE_BAKE=1   keep the composed cache as it stands
      TORIRS_FORCE_CACHE_BAKE=1  rebake the composed cache without asking
      TORIRS_FORCE_SCRIPT_BUILD=1  recompile the server script pack without asking
      TORIRS_PRINT_ONLY=1      print what would run and exit (native mode only;
                                ps1-only, not in run-live.sh)
      TORIRS_TOOLCHAIN         MinGW bin directory (ps1-only)
      TORIRS_WEB_PORT          web mode: page server port (default 8088)
      TORIRS_WEB_DEBUG=1       web mode: build web-debug instead of web
      TORIRS_WEB_NO_OPEN=1     web mode: print the URL instead of opening a browser
      TORIRS_NO_MOCK=1         web mode: skip the native mock230 child
      TORIRS_MOCK_BIN          web mode: use this mock230.exe instead of building one
      TORIRS_JAG_CRC           lc254: skip the CRC fetch, use this value
      plus every TORIRS_* the client itself reads (TORIRS_NET_DEBUG=1,
      TORIRS_NET_CHEAT, TORIRS_MAX_FRAMES, TORIRS_EXIT_BMP, ...)

.PARAMETER RawArgs
    `[--skip-checks] [web] <manifest.ini> [user] [pass] [client args...]`,
    exactly as run-live.sh takes $@. The literal first value `web` switches
    this script into web mode. `--skip-checks` skips cache-overlay and
    server-script preparation while retaining the incremental client build.
    Next comes the manifest path (required), then an optional user, then an
    optional pass -- each omitted one falls back to the manifest's own
    user=/pass=, then asdf/a. Everything left over is handed to the client
    verbatim (--soft3d, --opengl3, --offline, --connect host:port, ...). Do NOT
    separate client args with a bare `--`, which PowerShell rejects as an
    ambiguous parameter name before this script is ever entered.

.EXAMPLE
    .\run-live.ps1 manifest_osrs239_rs2012.ini
    .\run-live.ps1 manifest_osrs239_rs2012.ini --opengl3
    .\run-live.ps1 manifest_osrs230.ini qbdrepro test --soft3d
    .\run-live.ps1 --skip-checks manifest_osrs230.ini qbdrepro test
    .\run-live.ps1 web manifest_osrs239_rs2012.ini
    .\run-live.ps1 web manifest_osrs230.ini qbdrepro test --offline
#>
[CmdletBinding()]
param(
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$RawArgs
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = $PSScriptRoot
$exe = Join-Path $repo 'src\torirs_win64.exe'
$make = Join-Path $repo 'make.ps1'

$Usage = 'usage: run-live.ps1 [--skip-checks] [web] <manifest.ini> [user] [pass] [client args...]'

# One raw argument list, shifted by hand -- the direct equivalent of
# run-live.sh's `MANIFEST="${1:?}"; shift`. See the -Description note above
# for why this replaced named -Manifest/-User/-Pass parameters: PowerShell
# binds unpositioned parameters positionally too, which made `web` mode eat
# the real manifest argument into $User.
$argQueue = New-Object System.Collections.Generic.List[string]
if ($RawArgs) { foreach ($a in $RawArgs) { $argQueue.Add($a) } }

function Take-Arg {
    if ($argQueue.Count -eq 0) { return $null }
    $v = $argQueue[0]
    $argQueue.RemoveAt(0)
    return $v
}

$mode = 'native'
$skipChecks = ($env:TORIRS_SKIP_CHECKS -eq '1')
# Launcher options are leading arguments so they can never be confused with a
# client option. Accept `web` and --skip-checks in either order.
while ($argQueue.Count -gt 0) {
    if ($argQueue[0] -eq 'web') {
        $mode = 'web'
        [void](Take-Arg)
    } elseif ($argQueue[0] -eq '--skip-checks') {
        $skipChecks = $true
        [void](Take-Arg)
    } else {
        break
    }
}

$Manifest = Take-Arg
if (-not $Manifest) {
    Write-Error "run-live.ps1: $Usage"
    exit 1
}

# user/pass are positional, but only when actually present -- a client flag
# (--d3d9-zbuffer, --soft3d, ...) must never be swallowed into either slot.
# Every client arg is long-form (--xxx; see the client's own usage string), so
# that prefix is the one thing user/pass can never legitimately start with:
# stop taking positionally the moment the next token looks like a flag, and
# let $ClientArgs (and the manifest/asdf-a fallback below) pick it up instead.
$User = $null
$Pass = $null
if ($argQueue.Count -gt 0 -and $argQueue[0] -notlike '--*') { $User = Take-Arg }
if ($argQueue.Count -gt 0 -and $argQueue[0] -notlike '--*') { $Pass = Take-Arg }
$ClientArgs = @($argQueue)

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

# Which content lanes this profile's script pack is built from --
# `[content:lanes]`, one `lane=` per line (src/serverscript/ssc_lane.h).
#
# Section-scoped, unlike Get-ManifestValue: `lane` is a repeated key, and a
# manifest-wide match would also pick up a `[revconfig:...]` record that happens
# to use the word. run-live.sh reads it with the same rule.
#
# This replaces guessing the lane set from the OUTPUT DIRECTORY'S NAME
# (`*build_summoning`, `*build_curses`), which could only recognise lanes this
# launcher had been taught: a new lane was not launchable until both launchers
# learned its suffix.
function Get-ManifestLanes {
    $lanes = @()
    $inSection = $false
    foreach ($line in $manifestText) {
        if ($line -match '^\s*\[') { $inSection = ($line -match '^\s*\[content:'); continue }
        if ($inSection -and $line -match '^\s*lane\s*=\s*(.*?)\s*$' -and $Matches[1]) {
            $lanes += $Matches[1]
        }
    }
    return $lanes
}

$rev = Get-ManifestValue 'rev'
if (-not $rev) {
    Write-Error "run-live.ps1: no rev= in [net:boot] of '$Manifest'"
    exit 1
}
$cacheDir = Get-ManifestValue 'dir'
$serverScripts = Get-ManifestValue 'scripts'
$manifestLanes = Get-ManifestLanes
$manifestTransport = Get-ManifestValue 'transport'
# Named $manifestHost, never $Host -- that is PowerShell's own automatic
# variable for the hosting application, and assigning it corrupts the console.
$manifestHost = Get-ManifestValue 'host'
$gamePort = Get-ManifestValue 'port'

# ---------------------------------------------------------------- content tree
#
# Every embedded or mock run compiles the server scripts, and mock230-scripts
# reads both ported\ lanes unconditionally -- SUMMONING_CLIENT_LANE and
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

# The submodule first, then checkouts under build\.
#
# This used to be the other way around: build\osrs-content-rs2012, a worktree
# parked on branch rs2012-facebake-v10-m60, carried 596 rebaked .ob3 the
# submodule's main didn't have, so main having the ported/ lane DIRECTORIES
# wasn't proof it had the current bake, and build\ was preferred to avoid
# silently picking pre-facebake models.
#
# That is no longer true. main gained its own rebake (2026-08-13,
# e53b0fd476 "QBD: re-bake the RS2012 lane") and has since moved past
# build\osrs-content-rs2012's merge point on every front that has been
# checked, not just rs2012_qbd_td -- server\scripts\minigames\minigame_inferno
# on the submodule is newer than the same files on the worktree too. A worktree
# frozen on an old branch is exactly the tree this discovery must not pick
# silently: a launch would compile whatever content nobody has written for
# weeks, with nothing reporting the mismatch. So the submodule -- the tree
# everyone actually commits to -- wins by default again, and build\ is the
# fallback for anyone who has no submodule checkout with the lanes at all.
#
# build\ is gitignored, so these never collide with the tree git tracks.
# Reaching for a build\ checkout on purpose (e.g. a fresh facebake worktree
# that hasn't been merged yet) still works via $env:MOCK230_CONTENT_DIR, which
# is obeyed as given, in front of this whole function.
function Get-ContentCandidates {
    $candidates = @(Join-Path $repo 'OSRS-Content\osrs239-content')
    $buildRoot = Join-Path $repo 'build'
    if (Test-Path -LiteralPath $buildRoot) {
        $candidates += Get-ChildItem -LiteralPath $buildRoot -Directory |
            Sort-Object Name |
            ForEach-Object { Join-Path $_.FullName 'osrs239-content' }
    }
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
# No -ContentDir flag: matching run-live.sh exactly, the only override is
# $env:MOCK230_CONTENT_DIR (set it before invoking this script).
$contentChoice = ''
if ($env:MOCK230_CONTENT_DIR) {
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

# Only the builds need a content tree, so this is checked where a build is
# about to run and not at startup: TORIRS_NO_BUILD=1 and --offline are both
# legitimate ways to run without one. Shared by the native-embed and
# web-mock230 branches below -- both compile the server script pack, and
# mock230-scripts is the thing that actually requires both ported\ lanes.
function Assert-ContentTree {
    if (-not $env:MOCK230_CONTENT_DIR) {
        Write-Host "run-live.ps1: no OSRS-Content tree carrying $($ContentLanes -join ' and ')." -ForegroundColor Red
        Write-Host '  Looked at:' -ForegroundColor Red
        foreach ($candidate in Get-ContentCandidates) { Write-Host "    $candidate" -ForegroundColor Red }
        Write-Host '  Set $env:MOCK230_CONTENT_DIR to the checkout that has them.' -ForegroundColor Red
        exit 1
    }
}

# A self-contained manifest may carry development credentials. Preserve the
# historical asdf/a fallback, while letting explicit arguments win.
if (-not $User) { $User = Get-ManifestValue 'user' }
if (-not $User) { $User = 'asdf' }
if (-not $Pass) { $Pass = Get-ManifestValue 'pass' }
if (-not $Pass) { $Pass = 'a' }

function Test-ValidPort([string]$Value) {
    if ($Value -notmatch '^[0-9]+$') { return $false }
    $n = [int]$Value
    return ($n -ge 1 -and $n -le 65535)
}

# ws_host/ws_port: where a browser reaches the same server (the web build's
# sockets are WebSockets). For LostCity that is also where /crc lives, which is
# why the CRC fetch below uses it rather than assuming port 80. TORIRS_WS_* wins
# -- the same override the client itself honours, so the two cannot disagree.
$manifestWsHost = Get-ManifestValue 'ws_host'
$manifestWsPort = Get-ManifestValue 'ws_port'
$wsHost = if ($env:TORIRS_WS_HOST) { $env:TORIRS_WS_HOST }
    elseif ($manifestWsHost) { $manifestWsHost }
    elseif ($manifestHost) { $manifestHost }
    else { 'localhost' }
$wsPort = if ($env:TORIRS_WS_PORT) { $env:TORIRS_WS_PORT }
    elseif ($manifestWsPort) { $manifestWsPort }
    else { '80' }

# This is the endpoint the web client will actually dial. Unlike wsPort above,
# it falls back to the game port: 80 is only the historic LostCity CRC default,
# not the endpoint a manifest without ws_port tells the browser to use. Match
# BootManifest_ApplyWebEndpoint's treatment of a zero/unset ws_port.
$webGameHost = if ($env:TORIRS_WS_HOST) { $env:TORIRS_WS_HOST }
    elseif ($manifestWsHost) { $manifestWsHost }
    elseif ($manifestHost) { $manifestHost }
    else { 'localhost' }
$webGamePort = $gamePort
if ($env:TORIRS_WS_PORT) {
    $webGamePort = $env:TORIRS_WS_PORT
} elseif (Test-ValidPort $manifestWsPort) {
    $webGamePort = $manifestWsPort
}

# The client command line takes precedence over the manifest and web endpoint
# overrides. Track its networking-relevant flags so the mock we own cannot
# boot on a different port or protocol revision than the page will use.
$cliConnect = ''
$cliConnectSet = $false
$cliPort = ''
$cliPortSet = $false
$cliRev = ''
$cliOffline = $false
for ($i = 0; $i -lt $ClientArgs.Count; $i++) {
    switch ($ClientArgs[$i]) {
        '--connect' {
            if ($i + 1 -lt $ClientArgs.Count) { $cliConnect = $ClientArgs[$i + 1] }
            $cliConnectSet = $true
            $i++
        }
        '--port' {
            if ($i + 1 -lt $ClientArgs.Count) { $cliPort = $ClientArgs[$i + 1] }
            $cliPortSet = $true
            $i++
        }
        '--rev' {
            if ($i + 1 -lt $ClientArgs.Count) { $cliRev = $ClientArgs[$i + 1] }
            $i++
        }
        '--offline' { $cliOffline = $true }
    }
}

$webGameInlinePort = $false
if ($cliConnectSet) {
    $webGameHost = $cliConnect
    $colonIdx = $cliConnect.IndexOf(':')
    if ($colonIdx -ge 0) {
        $webGameHost = $cliConnect.Substring(0, $colonIdx)
        $cliConnectPort = $cliConnect.Substring($colonIdx + 1)
        if ($cliConnectPort) {
            $webGamePort = $cliConnectPort
            $webGameInlinePort = $true
        }
    }
}
if ($cliPortSet -and -not $webGameInlinePort) {
    $webGamePort = $cliPort
}

# NetTransport defaults an unset/zero port to 43594. A malformed explicit
# host:port is left invalid below so this launcher does not start a mock the
# client could never reach.
if (-not $webGameInlinePort -and (-not $webGamePort -or $webGamePort -eq '0')) {
    $webGamePort = '43594'
}
$clientRev = if ($cliRev) { $cliRev } else { $rev }

# --offline never logs in, so it never wants the embedded/mock server or the
# CRC handshake below. Passing both --offline and --connect is NOT offline --
# an explicit --connect says the caller wants to reach something.
$offline = $cliOffline -and (-not $cliConnectSet)

# Native osrs230 / osrs239 live runs use the in-process server. The browser
# cannot: the web build intentionally has no local cache/content files for that
# server to open. It instead talks to a native mock230 child over the browser's
# WebSocket-backed socket API.
$useEmbed = $false
$useMock230 = $false

if ($mode -eq 'web' -and (
        $clientRev -eq 'osrs230' -or $clientRev -eq 'osrs239' -or
        $manifestTransport -eq 'embed' -or $env:TORIRS_TRANSPORT -eq 'embed')) {
    # An Emscripten TCP socket is an RFC 6455 WebSocket. Never leave an embed
    # selection in place for a web run: the browser module is intentionally
    # plain and its world belongs in a native process.
    $env:TORIRS_TRANSPORT = 'tcp'
}

if ($mode -eq 'native') {
    if (($rev -eq 'osrs230' -or $rev -eq 'osrs239') -and -not $offline) {
        $useEmbed = $true
        $env:TORIRS_TRANSPORT = 'embed'
        # Embed defaults to osrs230 unless told otherwise; keep server wire = client rev.
        if (-not $env:MOCK230_REV) { $env:MOCK230_REV = $rev }
    }
} elseif (-not $offline) {
    # Own only a reachable IPv4 loopback endpoint. mock230 intentionally binds
    # 127.0.0.1 (not IPv6); a remote or explicit IPv6 endpoint belongs to the
    # caller and must not get an unused local process.
    if (($clientRev -eq 'osrs230' -or $clientRev -eq 'osrs239') -and (Test-ValidPort $webGamePort)) {
        if ($webGameHost -eq 'localhost' -or $webGameHost -eq '127.0.0.1') {
            $useMock230 = $true
        }
    }
}

# mock230 binds IPv4 loopback. Keep the standard manifest's `localhost` URL
# from depending on the browser's IPv6 fallback; an explicit --connect still
# wins later in the client's normal command-line parsing.
if ($useMock230 -and -not $cliConnectSet) {
    $env:TORIRS_WS_HOST = '127.0.0.1'
}

# lc254 live login checks cache CRCs; fetch the 9 big-endian int32s from the
# server's web endpoint (TORIRS_JAG_CRC env wins over the manifest). Not
# mode-gated -- a native lc254 run needs this exactly as much as a web one.
if ($rev -eq 'lc254' -and -not $offline -and -not $env:TORIRS_JAG_CRC) {
    $crcUrl = "http://${wsHost}:${wsPort}/crc"
    $crcBytes = $null
    try {
        $wc = New-Object System.Net.WebClient
        try { $crcBytes = $wc.DownloadData($crcUrl) } finally { $wc.Dispose() }
    } catch {
        $crcBytes = $null
    }
    if (-not $crcBytes -or $crcBytes.Length -eq 0 -or ($crcBytes.Length % 4) -ne 0) {
        Write-Host "run-live.ps1: cannot fetch $crcUrl -- is the server up?" -ForegroundColor Red
        Write-Host '  (the port comes from [net:boot] ws_port; override with TORIRS_WS_PORT)' -ForegroundColor Red
        exit 1
    }
    $crcValues = New-Object System.Collections.Generic.List[string]
    for ($off = 0; $off -lt $crcBytes.Length; $off += 4) {
        $chunk = New-Object byte[] 4
        [Array]::Copy($crcBytes, $off, $chunk, 0, 4)
        [Array]::Reverse($chunk)
        $crcValues.Add([BitConverter]::ToInt32($chunk, 0).ToString())
    }
    $env:TORIRS_JAG_CRC = [string]::Join(',', $crcValues)
}

$clientArgv = @('--manifest', $manifestPath, '--user', $User, '--pass', $Pass) + $ClientArgs

if ($mode -eq 'native' -and $env:TORIRS_PRINT_ONLY -eq '1') {
    Write-Host "manifest        : $manifestPath"
    Write-Host "rev             : $rev"
    Write-Host "cache           : $cacheDir"
    Write-Host "content tree    : $(if ($env:MOCK230_CONTENT_DIR) { "$env:MOCK230_CONTENT_DIR ($contentChoice)" } else { 'none found' })"
    Write-Host "user/pass       : $User / $Pass"
    Write-Host "offline         : $([int]$offline)"
    Write-Host "embedded server : $([int]$useEmbed)"
    if ($useEmbed) { Write-Host "TORIRS_TRANSPORT: $env:TORIRS_TRANSPORT  MOCK230_REV: $env:MOCK230_REV" }
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

    # Captured, not left to flow straight to the pipeline: an uncaptured native
    # call's stdout becomes part of THIS FUNCTION's own return value, and
    # `if (Test-CacheOverlayFresh ...)` was coercing that [print-line, $bool]
    # 2-element array to $true unconditionally -- PowerShell treats any array
    # of more than one element as truthy regardless of what it contains, so the
    # $false case never reached the caller. Assignment is what stops the
    # python process's stdout from leaking into the function's output stream.
    $global:LASTEXITCODE = 0
    $stdout = & $py.Source (Join-Path $repo 'tools\cache_overlay_stale.py') `
        --cache (Resolve-CachePath $cacheDir) --lane $Lane `
        --base (Resolve-RepoPath $Base) @treeArgs `
        --input (Resolve-RepoPath $Stager) `
        --input (Resolve-RepoPath 'src\makefile') `
        --input (Resolve-RepoPath '3rd\rscache\tools\cachepack')
    if ($stdout) { $stdout | ForEach-Object { Write-Host $_ } }
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

# tools\server_scripts_stale.py is the only implementation of this predicate
# and is shared with run-live.sh, so the two launchers cannot drift.
#
# Anything other than exit 1 rebuilds, for the same reason Test-CacheOverlayFresh
# treats a python-less PATH or a thrown exception as "bake": the failure mode of
# a needless sscompile pass is a few seconds, and the failure mode of a skipped
# one is a live session quietly running scripts nobody wrote.
function Test-ServerScriptsFresh {
    param([string]$OutDir)

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
        Write-Host 'run-live.ps1: no python3 on PATH -- rebuilding rather than assuming the script pack is fresh' -ForegroundColor Yellow
        return $false
    }

    $treeArgs = @()
    if ($env:MOCK230_CONTENT_DIR) { $treeArgs = @('--tree', $env:MOCK230_CONTENT_DIR) }

    # Captured, not left to flow straight to the pipeline: see the matching
    # comment on Test-CacheOverlayFresh -- an uncaptured native call's stdout
    # becomes part of THIS FUNCTION's own return value, and the caller's
    # `if (Test-ServerScriptsFresh ...)` was coercing that [print-line, $bool]
    # 2-element array to $true unconditionally regardless of what $bool was.
    $global:LASTEXITCODE = 0
    $stdout = & $py.Source (Join-Path $repo 'tools\server_scripts_stale.py') `
        --out $OutDir @treeArgs `
        --input (Resolve-RepoPath 'src\serverscript') `
        --input (Resolve-RepoPath 'src\makefile') `
        --input (Resolve-RepoPath 'tools\ss_allocate.py')
    if ($stdout) { $stdout | ForEach-Object { Write-Host $_ } }
    return ($LASTEXITCODE -eq 1)
}

function Build-Scripts {
    # Most manifests carry no scripts= at all -- it only needs stating when a
    # lane profile wants its own output instead of mock230-scripts' default,
    # which is $(MOCK230_CONTENT_DIR)/server/scripts/build (src/makefile). Assert-
    # ContentTree has already run by every call site, so $env:MOCK230_CONTENT_DIR
    # is set.
    $outDir = if ($serverScripts) {
        Resolve-CachePath $serverScripts
    } else {
        Join-Path $env:MOCK230_CONTENT_DIR 'server\scripts\build'
    }

    if (Test-ServerScriptsFresh -OutDir $outDir) {
        Write-Host "run-live.ps1: server script pack is up to date (TORIRS_FORCE_SCRIPT_BUILD=1 to rebuild)" -ForegroundColor Yellow
        return
    }

    # A profile that names lanes gets exactly those, compiled where it said. A
    # profile that names none is the pristine one and goes through
    # `mock230-scripts`, the only target carrying the full set of content
    # contracts. Same split as run-live.sh's build_scripts.
    if ($manifestLanes.Count -gt 0 -or $serverScripts) {
        $laneText = if ($manifestLanes.Count) { $manifestLanes -join ' ' } else { '(defaults only)' }
        Write-Host "run-live.ps1: building the server script pack (lanes: $laneText)..." -ForegroundColor Cyan
        Invoke-Make -Targets @(
            'mock230-scripts-lanes',
            "MOCK230_SCRIPT_LANES=$($manifestLanes -join ' ')",
            "MOCK230_SCRIPT_OUT=$outDir")
        return
    }

    Write-Host 'run-live.ps1: building the server script pack (mock230-scripts)...' -ForegroundColor Cyan
    Invoke-Make -Targets @('mock230-scripts')
}

# Cache and scripts are one consistency boundary. Keep the fast path together
# at each native and web call site so a launch cannot prepare only one half.
function Prepare-LiveContent {
    if ($skipChecks) {
        Write-Host 'run-live.ps1: --skip-checks -- using the cache and server script pack as they stand (they may be stale)' -ForegroundColor Yellow
        return
    }
    Build-CacheOverlay
    Build-Scripts
}

# ------------------------------------------------------------------- web lane
#
# Windows PowerShell 5.1 has no [IO.Path]::GetRelativePath (.NET Core-only), so
# this walks the string by hand. The only requirement -- matching run-live.sh's
# os.path.relpath + SystemExit guard -- is that the manifest is nested under
# the repo root; the io_server route it is served from is repo-rooted.
function Get-RepoRelativePath([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    $rootFull = ([IO.Path]::GetFullPath($repo)).TrimEnd('\')
    if ($full -eq $rootFull) { return '' }
    $prefix = $rootFull + '\'
    if (-not $full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        return $null
    }
    $rel = $full.Substring($prefix.Length)
    return ($rel -replace '\\', '/')
}

# The client's argv, and every TORIRS_* variable (except the launcher's own
# TORIRS_WEB_* knobs), become the page's query string. [uri]::EscapeDataString
# is RFC 3986 (spaces -> %20) rather than run-live.sh's quote_plus (spaces ->
# +); every standard query-string parser, including the JS URLSearchParams the
# page uses, accepts both, so this is a cosmetic difference only.
function Get-WebQueryString([string[]]$ArgsList) {
    $pairs = New-Object System.Collections.Generic.List[string]
    foreach ($a in $ArgsList) {
        $pairs.Add('arg=' + [uri]::EscapeDataString($a))
    }
    $envNames = Get-ChildItem Env: | Where-Object {
        $_.Name -like 'TORIRS_*' -and $_.Name -notlike 'TORIRS_WEB_*' -and
        $_.Name -ne 'TORIRS_USER' -and $_.Name -ne 'TORIRS_PASS'
    } | Sort-Object Name
    foreach ($e in $envNames) {
        $pairs.Add('env=' + [uri]::EscapeDataString("$($e.Name)=$($e.Value)"))
    }
    return [string]::Join('&', $pairs)
}

# ProcessStartInfo.ArgumentList (the Collection<string> overload) is a .NET
# Core-only addition -- it doesn't exist on Windows PowerShell 5.1's .NET
# Framework runtime. ProcessStartInfo.Arguments takes one pre-quoted string
# instead, so each argument is quoted here using the same escaping rules
# CommandLineToArgvW expects: wrap in double quotes if it contains
# space/tab/quote, and backslashes are only special immediately before a
# quote (doubled before an embedded quote, doubled at the end before the
# closing quote).
function ConvertTo-WindowsArgumentString([string[]]$ArgList) {
    $sb = New-Object System.Text.StringBuilder
    foreach ($a in $ArgList) {
        if ($sb.Length -gt 0) { [void]$sb.Append(' ') }
        if ($a.Length -gt 0 -and $a.IndexOfAny([char[]]@(' ', "`t", '"')) -lt 0) {
            [void]$sb.Append($a)
            continue
        }
        [void]$sb.Append('"')
        $i = 0
        while ($i -lt $a.Length) {
            $backslashes = 0
            while ($i -lt $a.Length -and $a[$i] -eq '\') { $backslashes++; $i++ }
            if ($i -eq $a.Length) {
                [void]$sb.Append('\' * ($backslashes * 2))
                break
            } elseif ($a[$i] -eq '"') {
                [void]$sb.Append('\' * ($backslashes * 2 + 1))
                [void]$sb.Append('"')
                $i++
            } else {
                [void]$sb.Append('\' * $backslashes)
                [void]$sb.Append($a[$i])
                $i++
            }
        }
        [void]$sb.Append('"')
    }
    return $sb.ToString()
}

# Start-Process (the cmdlet) has no way to set child-only environment overrides
# in Windows PowerShell 5.1, so io_server and mock230 are spawned through raw
# ProcessStartInfo instead. EnvironmentVariables starts as a copy of this
# process's own environment, so overriding just MOCK230_CACHE/SCRIPTS/REV here
# mirrors run-live.sh's subshell-scoped `export` -- the child sees the change,
# this script's own $env: does not. UseShellExecute=$false with no redirection
# lets the child inherit the console's stdout/stderr handles directly.
function Start-BackgroundProcess {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$ArgumentList = @(),
        [string]$WorkingDirectory = $repo,
        [hashtable]$Environment = @{}
    )
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $FilePath
    $psi.Arguments = ConvertTo-WindowsArgumentString $ArgumentList
    $psi.WorkingDirectory = $WorkingDirectory
    $psi.UseShellExecute = $false
    foreach ($key in $Environment.Keys) { $psi.EnvironmentVariables[$key] = $Environment[$key] }
    return [System.Diagnostics.Process]::Start($psi)
}

# kill + wait, tolerating a process that already exited on its own.
function Stop-BackgroundProcess($Proc) {
    if (-not $Proc) { return }
    if (-not $Proc.HasExited) {
        try {
            $Proc.Kill()
            $Proc.WaitForExit(5000) | Out-Null
        } catch {}
    }
}

if ($mode -eq 'native') {
    if ($env:TORIRS_NO_BUILD -ne '1') {
        if ($useEmbed) {
            Assert-ContentTree
            Write-Host "run-live.ps1: content tree ($contentChoice): $env:MOCK230_CONTENT_DIR" -ForegroundColor Cyan
            Write-Host "run-live.ps1: $rev -- building with EMBED_SERVER=1 (in-process server, MOCK230_REV=$env:MOCK230_REV)" -ForegroundColor Cyan
            Prepare-LiveContent
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
}

# ---------------------------------------------------------------------- web

$port = if ($env:TORIRS_WEB_PORT) { $env:TORIRS_WEB_PORT } else { '8088' }
$webTarget = if ($env:TORIRS_WEB_DEBUG -eq '1') { 'web-debug' } else { 'web' }
# TORIRS_NO_MOCK is checked here, once, rather than re-checked at every use
# below -- matching run-live.sh's own start_mock230 gate.
$useMockEffective = $useMock230 -and ($env:TORIRS_NO_MOCK -ne '1')

# io_server's makefile recipe names a bare `io_server`, but MinGW gcc silently
# appends .exe to any linker output missing an extension -- confirmed on disk,
# not assumed. IO_SERVER_OBJ_DIR is a fixed `build` (never platform-suffixed),
# so this path does not vary by platform the way mock230's below does.
$ioServerExe = Join-Path $repo 'src\build\io_server.exe'

if ($env:TORIRS_NO_BUILD -ne '1') {
    # The module contains no manifests: the page fetches whichever one its
    # query string names from the server, so a build never has to be redone
    # for a new manifest. Never link EMBED_SERVER into the web module -- the
    # native mock owns that world and can open the host cache/content
    # filesystem; EMBED_SERVER=0 is passed explicitly so an inherited
    # EMBED_SERVER=1 cannot turn this into an embedded-world module.
    if ($useMockEffective) {
        Assert-ContentTree
        Prepare-LiveContent
    }
    Invoke-Make -Targets @('EMBED_SERVER=0', $webTarget)

    if (-not (Test-Path -LiteralPath $ioServerExe)) {
        Write-Host 'run-live.ps1: building the IO server...' -ForegroundColor Cyan
        Invoke-Make -Targets @('io-server')
    }
}

if (-not (Test-Path -LiteralPath $ioServerExe)) {
    Write-Error "run-live.ps1: $ioServerExe not found"
    exit 1
}

$manifestArg = Get-RepoRelativePath $manifestPath
if ($null -eq $manifestArg) {
    Write-Error 'run-live.ps1: web manifests must be under the repository root'
    exit 1
}
$webArgv = @('--manifest', $manifestArg, '--user', $User, '--pass', $Pass) + $ClientArgs
$url = "http://localhost:$port/?$(Get-WebQueryString $webArgv)"

# The IO server and mock230 are this script's children: Ctrl-C must release
# both the page server and the game port. PowerShell unwinds through this
# try/finally on Ctrl-C the same way run-live.sh's `trap cleanup EXIT` fires
# on every exit path.
$ioProc = $null
$mockProc = $null
try {
    $ioProc = Start-BackgroundProcess -FilePath $ioServerExe -WorkingDirectory $repo `
        -ArgumentList @('--manifest', $manifestPath, '--root', (Join-Path $repo 'build-web'), '--port', $port)

    # Give it a moment to bind, and fail loudly rather than opening a dead page.
    Start-Sleep -Seconds 1
    if ($ioProc.HasExited) {
        Write-Error "run-live.ps1: io_server exited during startup (port $port already in use?)"
        exit 1
    }

    if ($useMockEffective) {
        # Build a deterministic, non-embedded native lane. The outer
        # environment may carry OPT/MEMTRACE/sanitizer variables for a client
        # experiment; it must not make this launcher look for the wrong
        # binary after building a different lane. A custom TORIRS_MOCK_BIN is
        # already the caller's own responsibility.
        #
        # PLATFORM=native resolves to win64 on Windows (src/platform/platform.mk),
        # so OBJ_DIR here is build_win64_opt, not run-live.sh's build_opt --
        # a genuine Windows/Unix divergence, not a typo.
        $mockBin = if ($env:TORIRS_MOCK_BIN) { $env:TORIRS_MOCK_BIN } else { Join-Path $repo 'src\build_win64_opt\mock230.exe' }
        if (-not $env:TORIRS_MOCK_BIN -and $env:TORIRS_NO_BUILD -ne '1') {
            Write-Host 'run-live.ps1: building the native mock server...' -ForegroundColor Cyan
            Invoke-Make -Targets @('PLATFORM=native', 'OPT=1', 'MEMTRACE=0', 'ENABLE_ASAN=0', 'ENABLE_UBSAN=0', `
                    'TORIDRAW_NO_SIMD=0', 'TORIDRAW_OPT=0', 'EMBED_SERVER=0', 'mock230')
        }
        if (-not (Test-Path -LiteralPath $mockBin)) {
            $found = Get-ChildItem -LiteralPath (Join-Path $repo 'src') -Recurse -Filter 'mock230.exe' -ErrorAction SilentlyContinue |
                Select-Object -First 1
            if ($found) {
                Write-Host "run-live.ps1: mock230 not at $mockBin -- using $($found.FullName)" -ForegroundColor Yellow
                $mockBin = $found.FullName
            }
        }
        if (-not (Test-Path -LiteralPath $mockBin)) {
            Write-Error "run-live.ps1: mock binary '$mockBin' not found"
            exit 1
        }

        $mockEnv = @{}
        if (-not $env:MOCK230_CACHE -and $cacheDir) { $mockEnv['MOCK230_CACHE'] = Resolve-CachePath $cacheDir }
        if (-not $env:MOCK230_SCRIPTS -and $serverScripts) { $mockEnv['MOCK230_SCRIPTS'] = Resolve-CachePath $serverScripts }
        $mockEnv['MOCK230_REV'] = $clientRev

        $mockProc = Start-BackgroundProcess -FilePath $mockBin -WorkingDirectory $repo -Environment $mockEnv `
            -ArgumentList @("$webGamePort", '--rev', $clientRev)

        # A stale listener is worse than an early failure: it can make this run
        # appear to work against yesterday's world. Fail and let the finally
        # block release io_server instead of silently attaching to it.
        Start-Sleep -Seconds 1
        if ($mockProc.HasExited) {
            Write-Error "run-live.ps1: mock230 exited during startup (game port $webGamePort unavailable or mock failed to boot; see output above)"
            $mockProc = $null
            exit 1
        }
    }

    # A browser tab has no TCP. emscripten implements the client's sockets over
    # WebSockets, so whatever the page dials must speak RFC 6455 -- the
    # manifest's transport=tcp describes what the *native* client dials, and
    # says nothing about what the page ends up doing. The local mock accepts
    # that upgrade on the game port; other servers name their WebSocket
    # endpoint with ws_port. Raw $useMock230 (not $useMockEffective) and raw
    # $manifestHost/$gamePort (not the computed web endpoint) match
    # run-live.sh's own warning exactly.
    $wsEndpointKnown = [bool]($manifestWsPort -or $env:TORIRS_WS_PORT -or $useMock230)
    if (-not $offline -and -not $wsEndpointKnown) {
        $hostForWarning = if ($manifestHost) { $manifestHost } else { 'localhost' }
        Write-Host 'run-live.ps1: note -- a browser reaches this server over a WebSocket, and' -ForegroundColor Yellow
        Write-Host "  this manifest names no ws_port, so the page will dial ${hostForWarning}:${gamePort}." -ForegroundColor Yellow
        Write-Host '  If that port speaks raw TCP only, add ws_port= to [net:boot], put a' -ForegroundColor Yellow
        Write-Host "  bridge in front (websockify ${hostForWarning}:8443 ${hostForWarning}:${gamePort})," -ForegroundColor Yellow
        Write-Host '  or pass --offline to run against the cache alone.' -ForegroundColor Yellow
    }

    Write-Host "run-live.ps1: $url"
    if ($env:TORIRS_WEB_NO_OPEN -ne '1') {
        Start-Process $url
    }
    Write-Host "run-live.ps1: serving on port $port -- Ctrl-C to stop (child services stop with it)" -ForegroundColor Cyan

    # io_server and mock230 are one live run. Do not leave a page server
    # advertising a dead game endpoint if the mock crashes after its startup
    # check.
    while (-not $ioProc.HasExited) {
        if ($mockProc -and $mockProc.HasExited) {
            Write-Error 'run-live.ps1: mock230 stopped unexpectedly; stopping the web run'
            exit 1
        }
        Start-Sleep -Seconds 1
    }
    exit $ioProc.ExitCode
} finally {
    Stop-BackgroundProcess $mockProc
    Stop-BackgroundProcess $ioProc
}
