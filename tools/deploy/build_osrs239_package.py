#!/usr/bin/env python3
"""Build the osrs239 world into one deployable zip.

    python tools/deploy/build_osrs239_package.py            # build + stage + zip
    make -C src deploy-osrs239                               # the same

The zip unpacks to a self-contained `torirs-osrs239/` that runs the two
processes a browser needs (docs/WEB_SERVERS.md) with nothing else installed:

    bin/torirsserver[.exe]      the game world, on the game port
    bin/io_server[.exe]         the page, POST /io cache reads, GET /boot/
    build-web/                  the emscripten client and its host page
    cache.osrs239/              the pristine rev-239 cache both servers read
    content/osrs-content/    the slice of the content tree the server opens
    manifests/                  the boot manifest, rewritten for this layout
    revconfig/, script/         what the page fetches through /boot/
    run.cmd, run.sh             start both in the foreground
    supervise.ps1               start both, restart whichever exits (Windows)
    install-windows-task.ps1    register supervise.ps1 as a startup task,
                                open the two ports in Windows Firewall

The servers are always built NATIVE, so run this on the machine that will
host the package (or one of the same OS). On Windows every make call goes
through make.ps1, which puts the repository's MinGW and emsdk toolchains on
PATH; elsewhere it is plain `make -C src`.

What is deliberately NOT in the package: the 11 GB `server/scripts/selftest`
tree, the alternate script lanes (`build_summoning` and friends), and the map
files the server never opens (only `maps/*.jm2` and `multiway.csv` are read --
see load_maps in torirs_server_content.c). Player saves land in `saves/`
beside the binaries at run time, so a redeploy over an old unpack keeps them.
"""
from __future__ import annotations

import argparse
import datetime as _dt
import os
import platform
import shutil
import subprocess
import sys
import time
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SRC = REPO / "src"
CONTENT = REPO / "OSRS-Content" / "osrs-content"
SCRIPT_PACK = CONTENT / "server" / "scripts" / "build"
# The world this package serves: osrs239 unless --world says otherwise
# (osrs241 is the current revision). Every name below that says osrs239 --
# the cache, the manifest, the revconfig, the scripts' text -- is rewritten to
# it, so the templates keep their one spelling.
WORLD = "osrs239"
PACKAGE_NAME = "torirs-osrs239"


def for_world(text: str) -> str:
    return text.replace("osrs239", WORLD).replace("Osrs239", WORLD.capitalize())
IS_WINDOWS = os.name == "nt"
EXE = ".exe" if IS_WINDOWS else ""

# The content directories torirsserver opens, relative to the content root
# (grep torirs_server_content.c for `%s/`): configs/all.* and the compacks,
# the interface packs, the ported lanes, the pack cache in server/pack, the
# script sources it walks for .npc/.loc/.obj/.inv/.spawn/.dbtable/... and the
# compiled pack under server/scripts/build.
CONTENT_DIRS = ("configs", "fields", "interfaces", "ported", "pack", "server/pack")


def log(msg: str) -> None:
    print(f"deploy: {msg}", flush=True)


def die(msg: str) -> None:
    print(f"deploy: {msg}", file=sys.stderr, flush=True)
    sys.exit(1)


def git(*args: str, cwd: Path = REPO) -> str:
    return subprocess.check_output(["git", *args], cwd=cwd, text=True).strip()


# ------------------------------------------------------------------ make
def make(targets: list[str], jobs: int) -> None:
    """One make invocation, through make.ps1 on Windows (toolchains on PATH)."""
    if IS_WINDOWS:
        argv = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
                "-File", str(REPO / "make.ps1")]
        if jobs > 1:
            argv.append(f"-j{jobs}")
        argv += targets
    else:
        argv = ["make", "-C", str(SRC)]
        if jobs > 1:
            argv.append(f"-j{jobs}")
        argv += targets
    log("make " + " ".join(targets))
    # Run from `make -C src deploy-osrs239` (or make.ps1 deploy-osrs239, which
    # adds CC=gcc), the parent make's MAKEFLAGS would reach this sub-make and
    # its CC=gcc would win over the web lane's emcc: every wasm object then
    # fails on -sUSE_SDL=2. This is a fresh top-level make, not a child.
    env = {k: v for k, v in os.environ.items() if k not in ("MAKEFLAGS", "MFLAGS", "MAKELEVEL")}
    subprocess.run(argv, cwd=REPO, check=True, env=env)


def scripts_stale() -> bool:
    """tools/server_scripts_stale.py: exit 1 means current, anything else bake."""
    rc = subprocess.run(
        [sys.executable, str(REPO / "tools" / "server_scripts_stale.py"),
         "--out", str(SCRIPT_PACK)], cwd=REPO).returncode
    return rc != 1


def server_binary(name: str, built_after: float | None) -> Path:
    """The freshest native optimised build of a server, and only a fresh one
    when this run was supposed to build it."""
    candidates = sorted(SRC.glob(f"build*_opt/{name}{EXE}"),
                        key=lambda p: p.stat().st_mtime, reverse=True)
    candidates = [p for p in candidates if "web" not in p.parent.name
                  and "asan" not in p.parent.name]
    if not candidates:
        die(f"no build*_opt/{name}{EXE} under src/ -- did the build run?")
    found = candidates[0]
    if built_after is not None and found.stat().st_mtime < built_after:
        die(f"{found} predates this build; the make target did not produce it")
    return found


# ------------------------------------------------------------------ staging
def stage_runtime_dlls(bin_dir: Path) -> None:
    """Windows: the MinGW runtime DLLs the binaries import, beside them.

    torirsserver links libwinpthread-1.dll dynamically; on the build box it is
    found through the toolchain on PATH, and an unpacked package has no such
    PATH, so the process died before main() with nothing in its log. Every
    import that resolves inside the toolchain's bin/ is copied; system DLLs
    (kernel32, ws2_32, msvcrt) resolve nowhere there and are left alone."""
    if not IS_WINDOWS:
        return
    tc_bin = REPO / "toolchains" / "mingw64" / "bin"
    objdump = tc_bin / "objdump.exe"
    if not objdump.is_file():
        die(f"{objdump} missing -- cannot resolve the binaries' DLL imports")
    for exe in sorted(bin_dir.glob("*.exe")):
        dump = subprocess.check_output([str(objdump), "-p", str(exe)], text=True)
        for line in dump.splitlines():
            line = line.strip()
            if not line.startswith("DLL Name:"):
                continue
            dll = line.split(":", 1)[1].strip()
            src = tc_bin / dll
            if src.is_file() and not (bin_dir / dll).exists():
                shutil.copy2(src, bin_dir / dll)
                log(f"runtime: {dll} (imported by {exe.name})")


def copy_tree(src: Path, dst: Path, keep=None) -> int:
    """Copy src into dst, `keep(relative_path) -> bool` deciding per file.
    Returns the byte count copied."""
    total = 0
    for root, dirs, files in os.walk(src):
        rel_root = Path(root).relative_to(src)
        dirs[:] = [d for d in dirs if keep is None or keep(rel_root / d, True)]
        for f in files:
            rel = rel_root / f
            if keep is not None and not keep(rel, False):
                continue
            out = dst / rel
            out.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(Path(root) / f, out)
            total += out.stat().st_size
    return total


def stage_content(stage: Path) -> None:
    out = stage / "content" / "osrs-content"
    total = 0
    for rel in CONTENT_DIRS:
        src = CONTENT / rel
        if not src.is_dir():
            die(f"content dir missing: {src}")
        total += copy_tree(src, out / rel)

    # server/scripts: the sources the server walks plus the compiled default
    # pack. Not the 11 GB selftest corpus, not the alternate lanes.
    def keep_scripts(rel: Path, is_dir: bool) -> bool:
        top = rel.parts[0] if rel.parts else ""
        if top == "selftest":
            return False
        if top.startswith("build") and top != "build":
            return False
        return True
    total += copy_tree(CONTENT / "server" / "scripts", out / "server" / "scripts",
                       keep_scripts)
    if not (out / "server" / "scripts" / "build").is_dir():
        die("server/scripts/build (the compiled script pack) is missing")

    # maps: load_maps reads m<x>_<z>.jm2 and nothing else; multiway.csv beside.
    def keep_maps(rel: Path, is_dir: bool) -> bool:
        if is_dir:
            return False
        return rel.suffix == ".jm2" or rel.name == "multiway.csv"
    total += copy_tree(CONTENT / "maps", out / "maps", keep_maps)
    log(f"content: {total / 1e6:.0f} MB")


def stage_web(stage: Path) -> None:
    """build-web/ minus the CMake and C leftovers that live beside the page."""
    src = REPO / "build-web"
    if not (src / "torirs.wasm").is_file():
        die("build-web/torirs.wasm missing -- run without --skip-web")
    served = {".html", ".js", ".wasm", ".png", ".ico", ".webmanifest",
              ".map", ".data", ".css", ".svg", ".txt", ".json", ".ttf",
              ".woff", ".woff2"}

    leftovers = {"CMakeCache.txt", "Makefile", "cmake_install.cmake"}

    def keep(rel: Path, is_dir: bool) -> bool:
        if is_dir:
            return rel.name != "CMakeFiles"
        return rel.suffix in served and rel.name not in leftovers
    total = copy_tree(src, stage / "build-web", keep)
    log(f"build-web: {total / 1e6:.0f} MB")


def write_manifest(stage: Path, game_port: int, ws_host: str, auth: str) -> None:
    """manifests/manifest_osrs239.ini, rewritten for the package's layout:
    the cache beside it, a WebSocket transport on the game port, and the
    launcher-only [derived:*] blocks dropped. Comments and order survive."""
    src = REPO / "manifests" / f"manifest_{WORLD}.ini"
    out_lines: list[str] = []
    section = ""
    seen_ws_keys: set[str] = set()

    def close_net_boot() -> None:
        # How players sign in: `oauth` puts the account server's Play Now
        # cards on the title (game/task_accounts_io.h), which needs io_server
        # run with --gate; `password` is the plain form.
        out_lines.append(f"auth={auth}\n")
        if "ws_host" not in seen_ws_keys:
            out_lines.append(f"ws_host={ws_host}\n")
        if "ws_port" not in seen_ws_keys:
            out_lines.append(f"ws_port={game_port}\n")

    for line in src.read_text(encoding="utf-8").splitlines(keepends=True):
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            if section == "net:boot":
                close_net_boot()
            section = stripped[1:-1]
            if section.startswith("derived:"):
                continue
            out_lines.append(line)
            continue
        if section.startswith("derived:"):
            continue
        key = stripped.split("=", 1)[0].strip() if "=" in stripped else ""
        if section == "cache:boot" and key == "dir":
            line = f"dir=../cache.{WORLD}\n"
        elif section == "net:boot" and key == "transport":
            line = "transport=ws\n"
        elif section == "net:boot" and key == "port":
            line = f"port={game_port}\n"
        elif section == "net:boot" and key == "ws_host":
            line = f"ws_host={ws_host}\n"
            seen_ws_keys.add("ws_host")
        elif section == "net:boot" and key == "auth":
            continue
        elif section == "net:boot" and key == "ws_port":
            line = f"ws_port={game_port}\n"
            seen_ws_keys.add("ws_port")
        out_lines.append(line)
    if section == "net:boot":
        close_net_boot()
    dst = stage / "manifests" / f"manifest_{WORLD}.ini"
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text("".join(out_lines), encoding="utf-8")


RUN_CMD = r"""@echo off
setlocal
cd /d "%~dp0"
if "%TORIRS_GAME_PORT%"=="" set TORIRS_GAME_PORT={game_port}
if "%TORIRS_WEB_PORT%"=="" set TORIRS_WEB_PORT={web_port}
if "%TORIRSSERVER_BIND%"=="" set TORIRSSERVER_BIND=0.0.0.0
set TORIRSSERVER_CACHE=cache.osrs239
set TORIRSSERVER_CONTENT=content/osrs-content
set TORIRSSERVER_SAVES=saves
if not exist saves mkdir saves
if not exist logs mkdir logs
echo torirs-osrs239: game port %TORIRS_GAME_PORT% (bind %TORIRSSERVER_BIND%), web port %TORIRS_WEB_PORT%
start "torirsserver" /b bin\torirsserver.exe %TORIRS_GAME_PORT% --rev osrs239 >> logs\torirsserver.log 2>&1
bin\io_server.exe --manifest manifests/manifest_osrs239.ini --root build-web --boot-root . --port %TORIRS_WEB_PORT% >> logs\io_server.log 2>&1
"""

RUN_SH = """#!/bin/sh
# Start the game server and io_server in the foreground; Ctrl-C stops both.
cd "$(dirname "$0")"
: "${{TORIRS_GAME_PORT:={game_port}}}"
: "${{TORIRS_WEB_PORT:={web_port}}}"
: "${{TORIRSSERVER_BIND:=0.0.0.0}}"
export TORIRSSERVER_BIND
export TORIRSSERVER_CACHE=cache.osrs239
export TORIRSSERVER_CONTENT=content/osrs-content
export TORIRSSERVER_SAVES=saves
mkdir -p saves logs
echo "torirs-osrs239: game port $TORIRS_GAME_PORT (bind $TORIRSSERVER_BIND), web port $TORIRS_WEB_PORT"
./bin/torirsserver "$TORIRS_GAME_PORT" --rev osrs239 >> logs/torirsserver.log 2>&1 &
GAME=$!
trap 'kill $GAME 2>/dev/null' EXIT INT TERM
./bin/io_server --manifest manifests/manifest_osrs239.ini --root build-web --boot-root . --port "$TORIRS_WEB_PORT" >> logs/io_server.log 2>&1
"""

SUPERVISE_PS1 = r"""<#
Start torirsserver and io_server from this directory and keep them running:
whichever exits is started again after a short pause. Logs append under
logs\. This is what install-windows-task.ps1 registers as the startup task;
it also runs fine by hand in a console (Ctrl-C stops the supervisor, and the
two children with it).

  -GamePort N   the game/WebSocket port (default {game_port})
  -WebPort N    io_server's HTTP port (default {web_port})
  -Bind ADDR    TORIRSSERVER_BIND (default 0.0.0.0, every interface)
#>
param(
    [int]$GamePort = {game_port},
    [int]$WebPort = {web_port},
    [string]$Bind = "0.0.0.0"
)
$ErrorActionPreference = "Continue"
Set-Location $PSScriptRoot
New-Item -ItemType Directory -Force -Path saves, logs | Out-Null
$env:TORIRSSERVER_BIND = $Bind
$env:TORIRSSERVER_CACHE = "cache.osrs239"
$env:TORIRSSERVER_CONTENT = "content/osrs-content"
$env:TORIRSSERVER_SAVES = "saves"

function Start-Game {{
    $p = Start-Process -FilePath (Join-Path $PSScriptRoot "bin\torirsserver.exe") `
        -ArgumentList @("$GamePort", "--rev", "osrs239") -WorkingDirectory $PSScriptRoot `
        -NoNewWindow -PassThru -RedirectStandardError "logs\torirsserver.log" `
        -RedirectStandardOutput "logs\torirsserver.out.log"
    $null = $p.Handle   # without this ExitCode reads back empty after the exit
    $p
}}
function Start-Web {{
    $p = Start-Process -FilePath (Join-Path $PSScriptRoot "bin\io_server.exe") `
        -ArgumentList @("--manifest", "manifests/manifest_osrs239.ini", "--root", "build-web",
                        "--boot-root", ".", "--port", "$WebPort") `
        -WorkingDirectory $PSScriptRoot -NoNewWindow -PassThru `
        -RedirectStandardError "logs\io_server.log" -RedirectStandardOutput "logs\io_server.out.log"
    $null = $p.Handle
    $p
}}

$game = $null; $web = $null
try {{
    while ($true) {{
        if (-not $game -or $game.HasExited) {{
            if ($game) {{ Add-Content logs\supervise.log "$(Get-Date -Format s) torirsserver exited ($($game.ExitCode)); restarting" }}
            $game = Start-Game
            Add-Content logs\supervise.log "$(Get-Date -Format s) torirsserver pid $($game.Id) on $GamePort"
        }}
        if (-not $web -or $web.HasExited) {{
            if ($web) {{ Add-Content logs\supervise.log "$(Get-Date -Format s) io_server exited ($($web.ExitCode)); restarting" }}
            $web = Start-Web
            Add-Content logs\supervise.log "$(Get-Date -Format s) io_server pid $($web.Id) on $WebPort"
        }}
        Start-Sleep -Seconds 5
    }}
}} finally {{
    foreach ($p in @($game, $web)) {{
        if ($p -and -not $p.HasExited) {{ Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }}
    }}
}}
"""

INSTALL_PS1 = r"""<#
Register supervise.ps1 as a scheduled task that starts at boot (no login
needed) and runs as the current user, and open the two ports in Windows
Firewall for the VPN and local subnets. Run from an elevated PowerShell:

    .\install-windows-task.ps1              # install + start now
    .\install-windows-task.ps1 -Uninstall   # stop, remove task and rules

  -GamePort / -WebPort   as supervise.ps1
  -Remote  "10.0.0.0/24,LocalSubnet"   who may reach the ports
#>
param(
    [int]$GamePort = {game_port},
    [int]$WebPort = {web_port},
    [string]$Remote = "10.0.0.0/24,LocalSubnet",
    [switch]$Uninstall
)
$ErrorActionPreference = "Stop"
$task = "TorirsOsrs239"
$here = $PSScriptRoot
$rules = @(@{{Name="torirs-osrs239 game"; Port=$GamePort}}, @{{Name="torirs-osrs239 web"; Port=$WebPort}})

if ($Uninstall) {{
    if (Get-ScheduledTask -TaskName $task -ErrorAction SilentlyContinue) {{
        Stop-ScheduledTask -TaskName $task -ErrorAction SilentlyContinue
        Unregister-ScheduledTask -TaskName $task -Confirm:$false
        Write-Host "removed task $task"
    }}
    foreach ($r in $rules) {{
        Get-NetFirewallRule -DisplayName $r.Name -ErrorAction SilentlyContinue | Remove-NetFirewallRule
    }}
    Get-Process torirsserver, io_server -ErrorAction SilentlyContinue | Stop-Process -Force
    Write-Host "stopped servers, removed firewall rules"
    exit 0
}}

foreach ($r in $rules) {{
    if (-not (Get-NetFirewallRule -DisplayName $r.Name -ErrorAction SilentlyContinue)) {{
        New-NetFirewallRule -DisplayName $r.Name -Direction Inbound -Action Allow `
            -Protocol TCP -LocalPort $r.Port -RemoteAddress ($Remote -split ",") -Profile Any | Out-Null
        Write-Host "firewall: allow TCP $($r.Port) from $Remote"
    }}
}}

$action = New-ScheduledTaskAction -Execute "powershell.exe" `
    -Argument "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$here\supervise.ps1`" -GamePort $GamePort -WebPort $WebPort" `
    -WorkingDirectory $here
$trigger = New-ScheduledTaskTrigger -AtStartup
$settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit ([TimeSpan]::Zero) `
    -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1) -StartWhenAvailable
$user = "$env:USERDOMAIN\$env:USERNAME"
$principal = New-ScheduledTaskPrincipal -UserId $user -LogonType S4U -RunLevel Limited
if (Get-ScheduledTask -TaskName $task -ErrorAction SilentlyContinue) {{
    Stop-ScheduledTask -TaskName $task -ErrorAction SilentlyContinue
    Unregister-ScheduledTask -TaskName $task -Confirm:$false
}}
Register-ScheduledTask -TaskName $task -Action $action -Trigger $trigger -Settings $settings -Principal $principal | Out-Null
Start-ScheduledTask -TaskName $task
Write-Host "task $task registered (at startup, as $user) and started"
Write-Host "page: http://localhost:$WebPort/?args=--manifest,manifests/manifest_osrs239.ini"
"""

README = r"""# torirs-osrs239

The osrs239 world, packaged: the game server, the page server, the browser
client, the cache and the content the server reads. Unpack anywhere and run.

    run.cmd                          Windows, foreground (Ctrl-C stops both)
    ./run.sh                         macOS / Linux, foreground
    install-windows-task.ps1         Windows: start at boot + firewall (elevated)

Then open

    http://localhost:{web_port}/?args=--manifest,manifests/manifest_osrs239.ini

The page dials the game server over a WebSocket at `ws_host:ws_port` from
`manifests/manifest_osrs239.ini` ({ws_host}:{game_port}). Behind a reverse proxy
that terminates TLS, add `&ws=ws` (or `&ws=wss://host/path`) to the page URL
and have the proxy pipe that path's WebSocket upgrade to the game port.

Environment knobs (all optional): TORIRS_GAME_PORT, TORIRS_WEB_PORT,
TORIRSSERVER_BIND (default 0.0.0.0; 127.0.0.1 keeps it local),
TORIRSSERVER_SAVES (default saves/), TORIRSSERVER_VERBOSE=1.

Logs append under logs/. Player saves live in saves/ -- keep that directory
when unpacking a newer package over this one.

Logins check passwords. A name's first login sets its password, kept as a
salted hash in saves/accounts/<name>.ini; to reset a forgotten one, delete
that file and the next login sets it again. A character saved before
passwords existed is claimed the same way by whoever logs into it first.
TORIRSSERVER_DEV=1 turns the check off -- never on a world others can reach.

Accounts, from this directory, beside the running servers (no restart; a
record is read at each login, and passwords are prompted for, never typed
on the command line):

    ./torirsctl create-admin <name>     new player account that is staff
    ./torirsctl create-player <name>    new player account
    ./torirsctl admin <name> on|off     make an existing player staff, or not
    ./torirsctl password <name>         new password for a player account
    ./torirsctl unlink <name>           release a player from its web login
    ./torirsctl create-login <name>     new web login (the sign-in gate)
    ./torirsctl revoke-login <name>     delete a web login
    ./torirsctl invite [name]           one-time link to make a web login
    ./torirsctl list                    every account

Starting it over SSH on Windows: a process started from an OpenSSH session
dies with the session. Either register the startup task (above) or create
the supervisor outside the session:

    Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{{
      CommandLine = 'powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File C:\torirs\osrs239\supervise.ps1'
      CurrentDirectory = 'C:\torirs\osrs239' }}

The firewall must admit whoever fronts it: install-windows-task.ps1 opens
the two ports for -Remote (default the 10.0.0.0/24 VPN and the local subnet).

Built from {sha} on {date} (content {content_sha}).
"""


TORIRSCTL = r"""#!/usr/bin/env bash
# torirsctl: make and manage this world's accounts, beside the running servers.
#
# Player accounts are what you play as (the admin flag = staff: the jmod
# crown and ::commands). Web logins open the site and the game page (the
# sign-in gate). Changes need no restart: a record is read at each login.
# Passwords are asked for twice and never echoed or put on a command line.
#
#   ./torirsctl create-admin <name>     new PLAYER account that is staff
#   ./torirsctl create-player <name>    new PLAYER account
#   ./torirsctl admin <name> on|off     make an existing player staff, or not
#   ./torirsctl password <name>         new password for a player account
#   ./torirsctl unlink <name>           release a player from its web login
#   ./torirsctl create-login <name>     new web login (site + game page)
#   ./torirsctl revoke-login <name>     delete a web login: it cannot sign in
#   ./torirsctl invite [name]           one-time link to make a web login
#   ./torirsctl list                    every account
#
# Piped stdin (not a terminal) is read as the password, once, for scripts.
set -euo pipefail
cd "$(dirname "$0")"
export TORIRSSERVER_SAVES="${TORIRSSERVER_SAVES:-saves}"
: "${TORIRSSERVER_GATE_URL:=https://@WS_HOST@}"
export TORIRSSERVER_GATE_URL
BIN=./bin/torirsserver

usage() {
    # The comment block at the top of this file, up to the first code line.
    awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"
    exit 2
}

# Read a password into PW: twice and silent at a terminal, one line from a pipe.
ask_password() {
    local again
    if [ -t 0 ]; then
        read -rs -p "Password for $1: " PW; echo >&2
        read -rs -p "Again: " again; echo >&2
        if [ "$PW" != "$again" ]; then
            echo "torirsctl: the passwords do not match" >&2
            exit 1
        fi
    else
        IFS= read -r PW || true
    fi
    if [ -z "$PW" ]; then
        echo "torirsctl: no password given" >&2
        exit 1
    fi
}

# Run an --account command with the password on its stdin (printf is a shell
# builtin, so the password never appears in a process list).
with_password() {
    local name="$1"; shift
    ask_password "$name"
    printf '%s\n' "$PW" | "$BIN" --account "$@"
    PW=
}

[ $# -ge 1 ] || usage
command="$1"; shift
case "$command" in
    create-admin)
        [ $# -eq 1 ] || usage
        with_password "$1" create "$1" admin ;;
    create-player)
        [ $# -eq 1 ] || usage
        with_password "$1" create "$1" ;;
    admin)
        [ $# -eq 2 ] || usage
        "$BIN" --account admin "$1" "$2" ;;
    password)
        [ $# -eq 1 ] || usage
        with_password "$1" password "$1" ;;
    unlink)
        [ $# -eq 1 ] || usage
        "$BIN" --account unlink "$1" ;;
    create-login)
        [ $# -eq 1 ] || usage
        with_password "$1" login "$1" ;;
    revoke-login)
        [ $# -eq 1 ] || usage
        "$BIN" --account revoke "$1" ;;
    invite)
        [ $# -le 1 ] || usage
        "$BIN" --account invite "$@" ;;
    list)
        [ $# -eq 0 ] || usage
        "$BIN" --account list ;;
    -h|--help|help)
        usage ;;
    *)
        echo "torirsctl: unknown command '$command'" >&2
        usage ;;
esac
"""


def stage_scripts(stage: Path, game_port: int, web_port: int, ws_host: str,
                  sha: str, content_sha: str, date: str) -> None:
    fmt = dict(game_port=game_port, web_port=web_port, ws_host=ws_host,
               sha=sha, content_sha=content_sha, date=date)
    (stage / "run.cmd").write_text(for_world(RUN_CMD.format(**fmt)), encoding="utf-8", newline="\r\n")
    (stage / "run.sh").write_text(for_world(RUN_SH.format(**fmt)), encoding="utf-8", newline="\n")
    # Not .format: the shell's own braces would all need doubling. One
    # placeholder, the public host its invite links name.
    (stage / "torirsctl").write_text(TORIRSCTL.replace("@WS_HOST@", ws_host),
                                     encoding="utf-8", newline="\n")
    (stage / "supervise.ps1").write_text(for_world(SUPERVISE_PS1.format(**fmt)), encoding="utf-8", newline="\r\n")
    (stage / "install-windows-task.ps1").write_text(for_world(INSTALL_PS1.format(**fmt)), encoding="utf-8", newline="\r\n")
    (stage / "README.md").write_text(for_world(README.format(**fmt)), encoding="utf-8")
    (stage / "VERSION.txt").write_text(
        f"{PACKAGE_NAME}\ncommit={sha}\ncontent={content_sha}\nbuilt={date}\n"
        f"host={platform.platform()}\n", encoding="utf-8")
    if not IS_WINDOWS:
        os.chmod(stage / "run.sh", 0o755)
        os.chmod(stage / "torirsctl", 0o755)


def zip_stage(stage: Path, zip_path: Path) -> None:
    log(f"zipping to {zip_path}")
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
        for root, _dirs, files in os.walk(stage):
            for f in sorted(files):
                full = Path(root) / f
                arc = Path(PACKAGE_NAME) / full.relative_to(stage)
                zf.write(full, arc.as_posix())
    log(f"zip: {zip_path.stat().st_size / 1e6:.0f} MB")


# ------------------------------------------------------------------ main
def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", type=Path, default=REPO / "build" / "deploy",
                    help="where the staged tree and zip land (default build/deploy)")
    ap.add_argument("--game-port", type=int, default=43594)
    ap.add_argument("--web-port", type=int, default=8088)
    ap.add_argument("--ws-host", default="localhost",
                    help="ws_host written into the manifest: where a browser on the "
                         "LAN reaches the game server when the page URL has no ws=")
    ap.add_argument("--jobs", "-j", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    ap.add_argument("--skip-servers", action="store_true", help="reuse the built servers")
    ap.add_argument("--skip-scripts", action="store_true", help="never rebake the script pack")
    ap.add_argument("--skip-web", action="store_true", help="reuse build-web/")
    ap.add_argument("--skip-build", action="store_true", help="all three of the above")
    ap.add_argument("--no-zip", action="store_true", help="stage only")
    ap.add_argument("--auth", choices=("password", "oauth"), default="password",
                    help="how players sign in: the password form, or the account "
                         "server's Play Now flow (oauth; needs io_server --gate)")
    ap.add_argument("--world", default="osrs239",
                    help="which world: its cache.<world>, manifests/manifest_<world>.ini and "
                         "revconfig/<world> (default osrs239; osrs241 is current)")
    args = ap.parse_args()
    global WORLD, PACKAGE_NAME
    WORLD = args.world
    PACKAGE_NAME = f"torirs-{WORLD}"
    for need in (REPO / f"cache.{WORLD}", REPO / "manifests" / f"manifest_{WORLD}.ini",
                 REPO / "revconfig" / WORLD):
        if not need.exists():
            die(f"--world {WORLD}: {need} does not exist")
    if args.skip_build:
        args.skip_servers = args.skip_scripts = args.skip_web = True

    started = time.time()
    sha = git("rev-parse", "--short=10", "HEAD")
    content_sha = git("rev-parse", "--short=10", "HEAD", cwd=REPO / "OSRS-Content")
    date = _dt.datetime.now().strftime("%Y-%m-%d %H:%M")
    log(f"repo {sha}, content {content_sha}")

    # ---- build
    if not args.skip_scripts:
        if scripts_stale():
            make(["torirsserver-scripts"], jobs=1)   # the bake is not -j safe
        else:
            log("script pack is current")
        # The server pack the world boots from (it reads no config text). It is
        # stamped with its tree's fingerprint and the server refuses a stale one,
        # so it is rebuilt (a no-op when fresh) before content/ is copied.
        make(["torirsserver-servpack"], jobs=1)
    if not args.skip_web:
        make(["web"], args.jobs)                     # also builds io_server
    if not args.skip_servers:
        make(["io-server"], args.jobs)
        make(["OPT=1", "torirsserver"], args.jobs)
    built_after = None if args.skip_servers else started
    game_bin = server_binary("torirsserver", built_after)
    io_bin = (SRC / "build" / f"io_server{EXE}")
    if not io_bin.is_file():
        die(f"{io_bin} missing")
    if built_after is not None and io_bin.stat().st_mtime < built_after:
        die(f"{io_bin} predates this build")

    # ---- stage
    stage = args.out / PACKAGE_NAME
    if stage.exists():
        shutil.rmtree(stage)
    (stage / "bin").mkdir(parents=True)
    shutil.copy2(game_bin, stage / "bin" / f"torirsserver{EXE}")
    shutil.copy2(io_bin, stage / "bin" / f"io_server{EXE}")
    log(f"servers: {game_bin.relative_to(REPO)}, {io_bin.relative_to(REPO)}")
    stage_runtime_dlls(stage / "bin")
    stage_web(stage)
    cache_bytes = copy_tree(REPO / f"cache.{WORLD}", stage / f"cache.{WORLD}")
    log(f"cache.{WORLD}: {cache_bytes / 1e6:.0f} MB")
    stage_content(stage)
    write_manifest(stage, args.game_port, args.ws_host, args.auth)
    copy_tree(REPO / "revconfig" / WORLD, stage / "revconfig" / WORLD)
    copy_tree(REPO / "script", stage / "script")
    stage_scripts(stage, args.game_port, args.web_port, args.ws_host, sha, content_sha, date)
    log(f"staged {stage}")

    # ---- zip
    if not args.no_zip:
        stamp = _dt.datetime.now().strftime("%Y%m%d")
        zip_path = args.out / f"{PACKAGE_NAME}-{stamp}-{sha}.zip"
        if zip_path.exists():
            zip_path.unlink()
        zip_stage(stage, zip_path)
        print(zip_path)
    log(f"done in {time.time() - started:.0f}s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
