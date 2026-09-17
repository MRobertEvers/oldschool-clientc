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
    content/osrs239-content/    the slice of the content tree the server opens
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
CONTENT = REPO / "OSRS-Content" / "osrs239-content"
SCRIPT_PACK = CONTENT / "server" / "scripts" / "build"
PACKAGE_NAME = "torirs-osrs239"
IS_WINDOWS = os.name == "nt"
EXE = ".exe" if IS_WINDOWS else ""

# The content directories torirsserver opens, relative to the content root
# (grep torirs_server_content.c for `%s/`): configs/all.* and the compacks,
# the interface packs, the ported lanes, the pack cache in server/pack, the
# script sources it walks for .npc/.loc/.obj/.inv/.spawn/.dbtable/... and the
# compiled pack under server/scripts/build.
CONTENT_DIRS = ("configs", "interfaces", "ported", "pack", "server/pack")


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
    subprocess.run(argv, cwd=REPO, check=True)


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
    out = stage / "content" / "osrs239-content"
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


def write_manifest(stage: Path, game_port: int, ws_host: str) -> None:
    """manifests/manifest_osrs239.ini, rewritten for the package's layout:
    the cache beside it, a WebSocket transport on the game port, and the
    launcher-only [derived:*] blocks dropped. Comments and order survive."""
    src = REPO / "manifests" / "manifest_osrs239.ini"
    out_lines: list[str] = []
    section = ""
    seen_ws_keys: set[str] = set()

    def close_net_boot() -> None:
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
            line = "dir=../cache.osrs239\n"
        elif section == "net:boot" and key == "transport":
            line = "transport=ws\n"
        elif section == "net:boot" and key == "port":
            line = f"port={game_port}\n"
        elif section == "net:boot" and key == "ws_host":
            line = f"ws_host={ws_host}\n"
            seen_ws_keys.add("ws_host")
        elif section == "net:boot" and key == "ws_port":
            line = f"ws_port={game_port}\n"
            seen_ws_keys.add("ws_port")
        out_lines.append(line)
    if section == "net:boot":
        close_net_boot()
    dst = stage / "manifests" / "manifest_osrs239.ini"
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text("".join(out_lines), encoding="utf-8")


RUN_CMD = r"""@echo off
setlocal
cd /d "%~dp0"
if "%TORIRS_GAME_PORT%"=="" set TORIRS_GAME_PORT={game_port}
if "%TORIRS_WEB_PORT%"=="" set TORIRS_WEB_PORT={web_port}
if "%TORIRSSERVER_BIND%"=="" set TORIRSSERVER_BIND=0.0.0.0
set TORIRSSERVER_CACHE=cache.osrs239
set TORIRSSERVER_CONTENT=content/osrs239-content
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
export TORIRSSERVER_CONTENT=content/osrs239-content
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
$env:TORIRSSERVER_CONTENT = "content/osrs239-content"
$env:TORIRSSERVER_SAVES = "saves"

function Start-Game {{
    Start-Process -FilePath (Join-Path $PSScriptRoot "bin\torirsserver.exe") `
        -ArgumentList @("$GamePort", "--rev", "osrs239") -WorkingDirectory $PSScriptRoot `
        -NoNewWindow -PassThru -RedirectStandardError "logs\torirsserver.log" `
        -RedirectStandardOutput "logs\torirsserver.out.log"
}}
function Start-Web {{
    Start-Process -FilePath (Join-Path $PSScriptRoot "bin\io_server.exe") `
        -ArgumentList @("--manifest", "manifests/manifest_osrs239.ini", "--root", "build-web",
                        "--boot-root", ".", "--port", "$WebPort") `
        -WorkingDirectory $PSScriptRoot -NoNewWindow -PassThru `
        -RedirectStandardError "logs\io_server.log" -RedirectStandardOutput "logs\io_server.out.log"
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

README = """# torirs-osrs239

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

Built from {sha} on {date} (content {content_sha}).
"""


def stage_scripts(stage: Path, game_port: int, web_port: int, ws_host: str,
                  sha: str, content_sha: str, date: str) -> None:
    fmt = dict(game_port=game_port, web_port=web_port, ws_host=ws_host,
               sha=sha, content_sha=content_sha, date=date)
    (stage / "run.cmd").write_text(RUN_CMD.format(**fmt), encoding="utf-8", newline="\r\n")
    (stage / "run.sh").write_text(RUN_SH.format(**fmt), encoding="utf-8", newline="\n")
    (stage / "supervise.ps1").write_text(SUPERVISE_PS1.format(**fmt), encoding="utf-8", newline="\r\n")
    (stage / "install-windows-task.ps1").write_text(INSTALL_PS1.format(**fmt), encoding="utf-8", newline="\r\n")
    (stage / "README.md").write_text(README.format(**fmt), encoding="utf-8")
    (stage / "VERSION.txt").write_text(
        f"torirs-osrs239\ncommit={sha}\ncontent={content_sha}\nbuilt={date}\n"
        f"host={platform.platform()}\n", encoding="utf-8")
    if not IS_WINDOWS:
        os.chmod(stage / "run.sh", 0o755)


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
    args = ap.parse_args()
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
    stage_web(stage)
    cache_bytes = copy_tree(REPO / "cache.osrs239", stage / "cache.osrs239")
    log(f"cache.osrs239: {cache_bytes / 1e6:.0f} MB")
    stage_content(stage)
    write_manifest(stage, args.game_port, args.ws_host)
    copy_tree(REPO / "revconfig" / "osrs239", stage / "revconfig" / "osrs239")
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
