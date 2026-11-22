#!/usr/bin/env python3
"""Build the osrs239 world into one deployable zip.

    python tools/deploy/build_osrs239_package.py            # build + stage + zip
    make -C src deploy-osrs239                               # the same

The zip unpacks to a self-contained `torirs-osrs239/` that carries the five
servers (docs/WEB_SERVERS.md) with nothing else installed:

    bin/torirsserver[.exe]      the game world, on the game port; hosts the
                                account server unless TORIRS_ACCOUNT_SERVER
                                names one
    bin/account_server[.exe]    the login server alone: player accounts, saves,
                                the lock (for worlds sharing one saves dir)
    bin/hiscores_server[.exe]   hiscores, history, the adventure log: the one
                                process with saves/hiscores.sqlite open
    bin/website_server[.exe]    the public front door: the sign-in gate, user
                                accounts, OAuth, the account site; forwards
                                the rest to the IO server
    bin/io_server[.exe]         the page, POST /io cache reads, GET /boot/, JS5
    build-web/                  the emscripten client and its host page
    cache.osrs239/              the pristine rev-239 cache both servers read
    content/osrs-content/    the slice of the content tree the server opens
    manifests/                  the boot manifest, rewritten for this layout
    revconfig/, script/         what the page fetches through /boot/
    script/tests/               the quest tests and their manifest, played by
                                the staff Tests page (stage_quest_tests)
    build-web/android/          with --android: the prod Android app's game
                                pack (stage_android), served at /android/
    bin/torirsd[.exe]           starts all five from torirsd.ini, in order, and
                                stops them together
    torirsd.ini                 the five servers, their order and environment
    run.cmd, run.sh             torirsd in the foreground
    supervise.ps1               torirsd, started again whenever it exits
                                (Windows)
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
import hashlib
import json
import re
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
# A cache lane (--lane): ported/<lane>/cache baked over cache.<world> by
# `make torirsserver-cache-<lane>` and shipped under the world's own cache name,
# so the manifest and the servers' environment are the same with or without it.
LANE = ""


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


def make_obj_dir(variables: list[str]) -> Path:
    """The object directory a `make <variables> <target>` builds into, as
    make itself names it (src/Makefile print-obj-dir)."""
    if IS_WINDOWS:
        argv = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
                "-File", str(REPO / "make.ps1"), *variables, "print-obj-dir"]
    else:
        argv = ["make", "-C", str(SRC), *variables, "print-obj-dir"]
    env = {k: v for k, v in os.environ.items() if k not in ("MAKEFLAGS", "MFLAGS", "MAKELEVEL")}
    out = subprocess.run(argv, cwd=REPO, check=True, env=env,
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        if line.startswith("OBJ_DIR="):
            return SRC / line[len("OBJ_DIR="):].strip()
    die(f"make print-obj-dir named no OBJ_DIR: {out!r}")


def scripts_stale() -> bool:
    """tools/server_scripts_stale.py: exit 1 means current, anything else bake."""
    rc = subprocess.run(
        [sys.executable, str(REPO / "tools" / "server_scripts_stale.py"),
         "--out", str(SCRIPT_PACK)], cwd=REPO).returncode
    return rc != 1


def built_server(name: str, variables: list[str]) -> Path:
    """The server this run's make built, where make keeps it.

    Not "a binary newer than the run": make that finds the binary current
    rightly does nothing, which is every deploy whose change is the client's
    alone -- and a test of the binary's age refused all of them. make
    exiting 0 for the target (make() checks) is what says it is current."""
    path = make_obj_dir(variables) / f"{name}{EXE}"
    if not path.is_file():
        die(f"{path} missing after its make target")
    return path


def server_binary(name: str) -> Path:
    """--skip-servers: the freshest native optimised build of a server."""
    candidates = sorted(SRC.glob(f"build*_opt/{name}{EXE}"),
                        key=lambda p: p.stat().st_mtime, reverse=True)
    candidates = [p for p in candidates if "web" not in p.parent.name
                  and "asan" not in p.parent.name]
    if not candidates:
        die(f"no build*_opt/{name}{EXE} under src/ -- did the build run?")
    return candidates[0]


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


def stage_admin_art(stage: Path, jobs: int) -> None:
    """build-web/admin-art/: the account site's staff player editor
    (src/websiteserver/website_site_editor.c) -- its page script and style, and the art
    tools/entity_viewer/ev_icons renders from the staged cache: every obj's
    inventory icon in atlas sheets with an index, the skill and equipment-slot
    icons, the varp and inv names. Seconds; redone every package because the
    icons are the cache's. A failure ships the editor without art (it shows
    ids instead of icons) and says so loudly rather than stopping the deploy."""
    out = stage / "build-web" / "admin-art"
    out.mkdir(parents=True, exist_ok=True)
    for name in ("player_editor.js", "player_editor.css"):
        shutil.copy2(SRC / "websiteserver" / "site" / name, out / name)
    tool_dir = REPO / "tools" / "entity_viewer"
    tool = tool_dir / f"ev_icons{EXE}"
    env = {k: v for k, v in os.environ.items() if k not in ("MAKEFLAGS", "MFLAGS", "MAKELEVEL")}
    try:
        subprocess.run(["make", "-C", str(tool_dir), f"-j{max(1, jobs)}", "ev_icons"],
                       cwd=REPO, check=True, env=env)
        subprocess.run([str(tool), "--rev", WORLD, str(stage / f"cache.{WORLD}"),
                        "--names", str(REPO / "OSRS-Content" / "osrs-content"), "--out", str(out)],
                       cwd=REPO, check=True, env=env)
    except (subprocess.CalledProcessError, OSError) as e:
        log(f"WARNING: admin art not rendered ({e}); the player editor will show ids, not icons")
        return
    total = sum(f.stat().st_size for f in out.iterdir() if f.is_file())
    log(f"admin-art: {total / 1e6:.1f} MB")


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


def stage_quest_tests(stage: Path) -> None:
    """The quest tests the staff Tests page plays (src/websiteserver/
    website_site_tests.c): test/quests/*.lua and its fixtures under
    script/tests/quests/ -- where the scripts manifest names them (the checkout
    reaches them through the script/tests/quests link, which copy_tree does not
    follow) -- and the manifest itself, script/tests/tests.ini."""
    src = REPO / "test" / "quests"
    out = stage / "script" / "tests" / "quests"

    def keep(rel: Path, is_dir: bool) -> bool:
        if is_dir:
            return rel.parts[0] == "fixtures"
        return rel.suffix in (".lua", ".ini")

    total = copy_tree(src, out, keep)
    manifest = stage / "script" / "tests" / "tests.ini"
    subprocess.run([sys.executable, str(REPO / "tools" / "raid_gate" / "prepare_scripts.py"),
                    "--out", str(manifest)], cwd=REPO, check=True, capture_output=True)
    log(f"quest tests: {total / 1e6:.1f} MB, manifest {manifest.relative_to(stage)}")


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
    # manifest_latest.ini: the same bytes under a name that never changes, so
    # the gate's landing and a phone's home-screen icon keep working when the
    # world moves to the next revision. The page keys its cache on [cache:boot]
    # dir, not on this file's name, so a new revision is a new cache.
    latest = stage / "manifests" / "manifest_latest.ini"
    latest.write_text(f"; manifest_latest.ini = manifest_{WORLD}.ini (this package's world)\n"
                      + "".join(out_lines), encoding="utf-8")


def write_replay_manifest(stage: Path) -> None:
    """manifests/manifest_replay.ini: manifests/manifest_<world>_replay.ini with
    this package's cache, for the site's /admin/recordings, which boots the web
    page with `?replay=<recording>` (src/web/torirs_host.js) on it. The cache
    dir is the live manifest's, so a replay reads the groups the browser already
    holds for the game, and JS5 rides the game socket (`ws=ws`) as play does."""
    src = REPO / "manifests" / f"manifest_{WORLD}_replay.ini"
    if not src.exists():
        log(f"no {src.name}: the package has no replay manifest; /admin/recordings cannot play")
        return
    out_lines: list[str] = []
    section = ""
    for line in src.read_text(encoding="utf-8").splitlines(keepends=True):
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
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
        out_lines.append(line)
    dst = stage / "manifests" / "manifest_replay.ini"
    dst.write_text(f"; manifest_replay.ini = manifest_{WORLD}_replay.ini (session recordings)\n"
                   + "".join(out_lines), encoding="utf-8")


RUN_CMD = r"""@echo off
rem Start every server (torirsd.ini); Ctrl-C stops them all, the game first.
setlocal
cd /d "%~dp0"
bin\torirsd.exe torirsd.ini
"""

RUN_SH = """#!/bin/sh
# Start every server (torirsd.ini); Ctrl-C stops them all, the game first.
cd "$(dirname "$0")"
exec ./bin/torirsd torirsd.ini
"""

SUPERVISE_PS1 = r"""<#
Run torirsd (every server in torirsd.ini, up together and down together) from
this directory and start it again whenever it exits -- which it does when any
one server stops. Its own lines append to logs\torirsd.log; each server's to
logs\<name>.log. This is what install-windows-task.ps1 registers as the
startup task; it also runs fine by hand in a console (Ctrl-C stops it, and
torirsd stops the servers in order).
#>
$ErrorActionPreference = "Continue"
Set-Location $PSScriptRoot
New-Item -ItemType Directory -Force -Path saves, logs | Out-Null
$daemon = $null
try {{
    while ($true) {{
        $daemon = Start-Process -FilePath (Join-Path $PSScriptRoot "bin\torirsd.exe") `
            -ArgumentList @("torirsd.ini") -WorkingDirectory $PSScriptRoot -NoNewWindow `
            -PassThru -RedirectStandardError "logs\torirsd.log" `
            -RedirectStandardOutput "logs\torirsd.out.log"
        $null = $daemon.Handle   # without this ExitCode reads back empty after the exit
        Add-Content logs\supervise.log "$(Get-Date -Format s) torirsd pid $($daemon.Id)"
        $daemon.WaitForExit()
        Add-Content logs\supervise.log "$(Get-Date -Format s) torirsd exited ($($daemon.ExitCode)); starting again"
        Start-Sleep -Seconds 5
    }}
}} finally {{
    if ($daemon -and -not $daemon.HasExited) {{ Stop-Process -Id $daemon.Id -Force -ErrorAction SilentlyContinue }}
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
    Get-Process torirsserver, io_server, hiscores_server -ErrorAction SilentlyContinue | Stop-Process -Force
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
Write-Host "page: http://localhost:$WebPort/?args=--manifest,manifests/manifest_latest.ini"
"""

README = r"""# torirs-osrs239

The osrs239 world, packaged: its five servers, the browser client, the cache
and the content the game reads. Unpack anywhere and run.

    ./run.sh                         macOS / Linux, foreground
    run.cmd                          Windows, foreground
    install-windows-task.ps1         Windows: start at boot + firewall (elevated)

Each of those is one call: `bin/torirsd torirsd.ini`. Ctrl-C (or a stop of the
unit or task) stops every server, the game first. Then open

    http://localhost:{web_port}/?args=--manifest,manifests/manifest_latest.ini

`manifest_latest.ini` is a copy of `manifests/manifest_osrs239.ini` under a
name the next revision keeps: link to it, not to the revision. The page dials
the game server over a WebSocket at `ws_host:ws_port` from it
({ws_host}:{game_port}). Behind a reverse proxy
that terminates TLS, add `&ws=ws` (or `&ws=wss://host/path`) to the page URL
and have the proxy pipe that path's WebSocket upgrade to the game port.

## The servers

Five processes, one deployment. torirsd starts them in this order and stops
them in the reverse:

| server | binary | what it is | listens |
| --- | --- | --- | --- |
| account | `bin/account_server` | the login server: player accounts (passwords, admin and record flags, the web login each is linked to), every character's save, display names, and the lock that says which world has each character | 127.0.0.1:8091 |
| hiscores | `bin/hiscores_server` | the characters' records: hiscores, experience history, sessions, the adventure log | 127.0.0.1:8093 |
| io | `bin/io_server` | the client page and its assets, manifests, cache reads, the page's black box | 127.0.0.1:8089 |
| website | `bin/website_server` | the public front door: the sign-in gate and the user accounts behind it (web logins, invites), OAuth, the account site and staff pages | 0.0.0.0:{web_port} |
| game | `bin/torirsserver` | the world | 0.0.0.0:{game_port} |

How they talk:

```
  browser ──HTTP──▶ website ──(signed)──▶ io            the page, its assets, the cache
     │                 │
     │                 ├──(signed)──▶ account          sign-in links, the player editor
     │                 └──(signed)──▶ hiscores         the hiscores and player pages
     │
     └──WebSocket──▶ game ──(signed)──▶ account         every login takes the character and
                       │                                its save; every logout hands the
                       │                                save back
                       └──(signed, never waited on)──▶ hiscores   stats, sessions, the log
```

- **The website is the only door a browser uses**, beside the game's
  WebSocket. It answers the gate, the account pages and OAuth itself and
  forwards everything else -- the client page, its files, the cache -- to
  the IO server.
- **A user account is not a player account.** A web login (the website's,
  `saves/website.sqlite`) opens the site and holds player accounts linked to
  it; a player account (the account server's, `saves/accounts/`) is what you
  play as.
- **A character's save belongs to one world at a time.** A login takes it from
  the account server; while it plays the world keeps it in
  `saves/worlds/<world>/`; the logout hands it back to `saves/<name>.ini`.
  A world that crashes hands its characters back when it starts again;
  until then they cannot log in elsewhere. `./torirsctl release <name>` takes
  one back from a world that is not coming back.
- **The hiscores never hold anything up.** The game spools its records to
  `saves/hiscores_spool.tsv` and sends them when the hiscores server answers.
- **Every request between servers is signed** with `saves/service.key` (made at
  the first start): the internal servers refuse anything else. A server on
  another machine needs the same key.

If any one server exits, torirsd stops the others and exits too, so the set
always runs whole; supervise.ps1 (or a systemd unit with Restart=always and
KillMode=mixed) starts it again. Each server logs to `logs/<name>.log`.

Ports, the world's environment and every server's arguments are in
`torirsd.ini`; edit it to change them (`bin/torirsd --check torirsd.ini`
prints what it would run).

## Saves and accounts

Logs append under logs/. Everything that persists lives in saves/ -- keep
that directory when unpacking a newer package over this one.

Logins check passwords. A name's first login sets its password, kept as a
salted hash in saves/accounts/<name>.ini; to reset a forgotten one, delete
that file and the next login sets it again. A character saved before
passwords existed is claimed the same way by whoever logs into it first.

Accounts, from this directory, while the servers run (no restart; a record
is read at each login, and passwords are prompted for, never typed on the
command line):

    ./torirsctl create-admin <name>     new player account that is staff
    ./torirsctl create-player <name>    new player account
    ./torirsctl admin <name> on|off     make an existing player staff, or not
    ./torirsctl record <name> on|off    record every session's packets (replays
                                        at /admin/recordings, staff only)
    ./torirsctl password <name>         new password for a player account
    ./torirsctl unlink <name>           release a player from its web login
    ./torirsctl release <name>          take a character back from a world
                                        that is not coming back
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
the two public ports for -Remote (default the 10.0.0.0/24 VPN and the local
subnet).

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
#   ./torirsctl record <name> on|off    record every session of a player (both
#                                       directions, saves/recordings/<name>/);
#                                       staff watch them at /admin/recordings
#   ./torirsctl password <name>         new password for a player account
#   ./torirsctl unlink <name>           release a player from its web login
#   ./torirsctl release <name>          take a character back from a world that
#                                       is not coming back (its copy is refused)
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
# Where the servers are (torirsd.ini): --account asks them, signed with
# saves/service.key.
export TORIRS_ACCOUNT_SERVER="${TORIRS_ACCOUNT_SERVER:-127.0.0.1:8091}"
export TORIRS_HISCORES_SERVER="${TORIRS_HISCORES_SERVER:-127.0.0.1:8093}"
export TORIRS_WEBSITE_SERVER="${TORIRS_WEBSITE_SERVER:-127.0.0.1:@WEB_PORT@}"
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
    record)
        [ $# -eq 2 ] || usage
        "$BIN" --account record "$1" "$2" ;;
    password)
        [ $# -eq 1 ] || usage
        with_password "$1" password "$1" ;;
    unlink)
        [ $# -eq 1 ] || usage
        "$BIN" --account unlink "$1" ;;
    release)
        [ $# -eq 1 ] || usage
        "$BIN" --account release "$1" ;;
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


# ------------------------------------------------------------------ android
# The prod Android app (android/, flavor `prod`) carries no native library: it
# downloads this, the "game pack", built from the same commit as the world it
# talks to. Served by io_server from build-web/, so at /android/ behind both
# sign-ins. android/src/prod/java/com/torirs/client/GamePack.java reads it.
ANDROID_ABIS = ("armeabi-v7a", "arm64-v8a")


def android_jni_abi() -> int:
    """TORIRS_ANDROID_JNI_ABI: the Java<->C contract the pack was built for."""
    text = (SRC / "platform" / "platform_android.h").read_text(encoding="utf-8")
    m = re.search(r"#define\s+TORIRS_ANDROID_JNI_ABI\s+(\d+)", text)
    if not m:
        die("platform_android.h defines no TORIRS_ANDROID_JNI_ABI")
    return int(m.group(1))


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def stage_android(stage: Path, jobs: int, sha: str, content_sha: str, date: str,
                  key: Path, apk: Path | None) -> None:
    """build-web/android/: the client for every ABI, the files it fopens, and
    latest.json naming them, signed with `key` (an EC P-256 private key that
    never leaves the server; the app holds the public half)."""
    if not key.is_file():
        die(f"--android-key {key} does not exist")
    out = stage / "build-web" / "android"
    out.mkdir(parents=True, exist_ok=True)
    libs = {}
    for abi in ANDROID_ABIS:
        # A private object dir and target per ABI: `make all` on this lane
        # otherwise writes android/src/main/jniLibs, the dev app's copy.
        target = REPO / "build" / "android-gamepack" / abi / "libtorirs.so"
        make(["PLATFORM=android", f"ANDROID_ABI={abi}", "OPT=1",
              f"PLATFORM_OBJ_BASE=build_pkg_android_{abi}", f"PLATFORM_TARGET={target}", "all"], jobs)
        digest = sha256_file(target)
        rel = f"{abi}/libtorirs-{digest[:16]}.so"
        (out / abi).mkdir(exist_ok=True)
        shutil.copy2(target, out / rel)
        libs[abi] = {"path": rel, "sha256": digest, "size": target.stat().st_size}
        log(f"android {abi}: {target.stat().st_size / 1e6:.1f} MB")

    # What the client opens with fopen rather than asking io_server for: the
    # manifest and the RevConfig INIs it names. Fixed timestamps, so the same
    # files make the same zip and the phone fetches it only when they change.
    data_files = [(stage / "manifests" / "manifest_latest.ini", "manifests/manifest_latest.ini"),
                  (REPO / "plugin_prefs.mobile.ini", "plugin_prefs.mobile.ini")]
    for f in sorted((stage / "revconfig" / WORLD).rglob("*")):
        if f.is_file():
            data_files.append((f, f.relative_to(stage).as_posix()))
    tmp = out / "data.zip.part"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for src, arc in data_files:
            info = zipfile.ZipInfo(arc, date_time=(2020, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            zf.writestr(info, src.read_bytes())
    data_sha = sha256_file(tmp)
    data_rel = f"data-{data_sha[:16]}.zip"
    tmp.rename(out / data_rel)

    latest = {
        "format": 1,
        "jni_abi": android_jni_abi(),
        "commit": sha,
        "content": content_sha,
        "built": date,
        "world": WORLD,
        "manifest": "manifests/manifest_latest.ini",
        "data": {"path": data_rel, "sha256": data_sha, "size": (out / data_rel).stat().st_size},
        "libs": libs,
        "apk": "",
    }
    if apk:
        if not apk.is_file():
            die(f"--android-apk {apk} does not exist")
        shutil.copy2(apk, out / "torirs.apk")
        latest["apk"] = "torirs.apk"
    body = (json.dumps(latest, indent=1, sort_keys=True) + "\n").encode("utf-8")
    (out / "latest.json").write_bytes(body)
    subprocess.run(["openssl", "dgst", "-sha256", "-sign", str(key),
                    "-out", str(out / "latest.json.sig"), str(out / "latest.json")], check=True)
    log(f"android game pack: jni {latest['jni_abi']}, {', '.join(ANDROID_ABIS)}, "
        f"data {data_rel}{', apk' if apk else ''}; signed")


TORIRSD_INI = """; torirsd.ini: every server of this world, started by ONE call --
; ./run.sh, run.cmd, supervise.ps1, or a single systemd unit running
; `bin/torirsd torirsd.ini` with KillMode=mixed. Written by the package
; (tools/deploy/build_osrs239_package.py); see README.md "The servers".
;
; Started in this order, each answering on its `ready` port before the next;
; stopped in the reverse order (the game first, so it hands every character's
; save back to the account server while that is still up); and when any one
; exits, all are stopped and torirsd exits, for its supervisor to start the
; whole set again.

[torirsd]
logs = logs
start_timeout_ms = 180000
stop_timeout_ms = 90000

[env]
TORIRSSERVER_SAVES = saves
TORIRSSERVER_CACHE = cache.{world}
TORIRSSERVER_CONTENT = content/osrs-content
TORIRSSERVER_BIND = 0.0.0.0
TORIRS_ACCOUNT_SERVER = 127.0.0.1:8091
TORIRS_HISCORES_SERVER = 127.0.0.1:8093
TORIRS_IO_SERVER = 127.0.0.1:8089
TORIRS_WEBSITE_SERVER = 127.0.0.1:{web_port}
{gate_env}
; The login server: player accounts, their saves, the lock on each character.
[server.account]
run = bin/account_server{exe}
args = --port 8091
ready = 8091

; Hiscores, experience history, sessions, the adventure log.
[server.hiscores]
run = bin/hiscores_server{exe}
args = --port 8093
ready = 8093

; The client page and its assets, the web client's IO, the black box.
[server.io]
run = bin/io_server{exe}
args = --behind-website --port 8089 --manifest manifests/manifest_{world}.ini --root build-web --boot-root .
ready = 8089

; The public front door: the sign-in gate, user accounts, OAuth, the site.
[server.website]
run = bin/website_server{exe}
args = --port {web_port} {website_gate}--io 127.0.0.1:8089
ready = {web_port}

; The game.
[server.game]
run = bin/torirsserver{exe}
args = {game_port} --rev {world}{game_gate}
ready = {game_port}
"""


def write_daemon_config(stage: Path, game_port: int, web_port: int, gate_url: str) -> None:
    """torirsd.ini: the five servers, their order and their environment."""
    landing = "/?args=--manifest,manifests/manifest_latest.ini&ws=ws"
    text = TORIRSD_INI.format(
        world=WORLD, exe=EXE, game_port=game_port, web_port=web_port,
        gate_env=f"TORIRSSERVER_GATE_URL = {gate_url}\n" if gate_url else "",
        website_gate=f'--gate "{landing}" ' if gate_url else "",
        game_gate=" --gate" if gate_url else "")
    (stage / "torirsd.ini").write_text(text, encoding="utf-8")


def stage_scripts(stage: Path, game_port: int, web_port: int, ws_host: str,
                  sha: str, content_sha: str, date: str) -> None:
    fmt = dict(game_port=game_port, web_port=web_port, ws_host=ws_host,
               sha=sha, content_sha=content_sha, date=date)
    (stage / "run.cmd").write_text(for_world(RUN_CMD.format(**fmt)), encoding="utf-8", newline="\r\n")
    (stage / "run.sh").write_text(for_world(RUN_SH.format(**fmt)), encoding="utf-8", newline="\n")
    # Not .format: the shell's own braces would all need doubling. One
    # placeholder, the public host its invite links name.
    (stage / "torirsctl").write_text(
        TORIRSCTL.replace("@WS_HOST@", ws_host).replace("@WEB_PORT@", str(web_port)),
        encoding="utf-8", newline="\n")
    (stage / "supervise.ps1").write_text(for_world(SUPERVISE_PS1.format(**fmt)), encoding="utf-8", newline="\r\n")
    (stage / "install-windows-task.ps1").write_text(for_world(INSTALL_PS1.format(**fmt)), encoding="utf-8", newline="\r\n")
    (stage / "README.md").write_text(for_world(README.format(**fmt)), encoding="utf-8")
    (stage / "VERSION.txt").write_text(
        f"{PACKAGE_NAME}\ncommit={sha}\ncontent={content_sha}\nlane={LANE or '-'}\n"
        f"built={date}\nhost={platform.platform()}\n", encoding="utf-8")
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
                    help="how players sign in: the password form, or the website's "
                         "Play Now flow (oauth; needs --gate)")
    ap.add_argument("--gate", default="", metavar="PUBLIC_URL",
                    help="put the world behind the sign-in gate (torirsd.ini runs the "
                         "website and the game with --gate); PUBLIC_URL, e.g. "
                         "https://torirs.example.com, is what invite links name")
    ap.add_argument("--android", action="store_true",
                    help="also build the prod Android app's game pack into "
                         "build-web/android/ (needs the NDK: ANDROID_NDK_HOME, "
                         "and --android-key)")
    ap.add_argument("--android-key", type=Path,
                    default=Path.home() / "torirs" / "android" / "gamepack-signing.key",
                    help="EC P-256 private key that signs latest.json")
    ap.add_argument("--android-apk", type=Path, default=None,
                    help="the prod APK to serve as /android/torirs.apk, for phones "
                         "whose app is too old for this game pack")
    ap.add_argument("--world", default="osrs239",
                    help="which world: its cache.<world>, manifests/manifest_<world>.ini and "
                         "revconfig/<world> (default osrs239; osrs242 is current)")
    ap.add_argument("--lane", default="",
                    help="ship cache.<world>.<lane> (make torirsserver-cache-<lane>, baked "
                         "from ported/<lane>/cache) as cache.<world>; torirslive is the "
                         "live world's")
    args = ap.parse_args()
    global WORLD, PACKAGE_NAME, LANE
    WORLD = args.world
    LANE = args.lane
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
        make(["website-server"], args.jobs)
        make(["OPT=1", "torirsserver"], args.jobs)
        make(["OPT=1", "account-server"], args.jobs)
        make(["OPT=1", "hiscores-server"], args.jobs)
        make(["OPT=1", "torirsd"], args.jobs)
    if args.skip_servers:
        game_bin = server_binary("torirsserver")
        accounts_bin = server_binary("account_server")
        hiscores_bin = server_binary("hiscores_server")
        daemon_bin = server_binary("torirsd")
    else:
        game_bin = built_server("torirsserver", ["OPT=1"])
        accounts_bin = built_server("account_server", ["OPT=1"])
        hiscores_bin = built_server("hiscores_server", ["OPT=1"])
        daemon_bin = built_server("torirsd", ["OPT=1"])
    # io-server and website-server always build into src/build
    # (IO_SERVER_OBJ_DIR), whatever the variables; their make targets above
    # said whether they are current.
    io_bin = (SRC / "build" / f"io_server{EXE}")
    website_bin = (SRC / "build" / f"website_server{EXE}")
    for built in (io_bin, website_bin):
        if not built.is_file():
            die(f"{built} missing")

    # ---- stage
    stage = args.out / PACKAGE_NAME
    if stage.exists():
        shutil.rmtree(stage)
    (stage / "bin").mkdir(parents=True)
    shutil.copy2(game_bin, stage / "bin" / f"torirsserver{EXE}")
    shutil.copy2(io_bin, stage / "bin" / f"io_server{EXE}")
    shutil.copy2(website_bin, stage / "bin" / f"website_server{EXE}")
    shutil.copy2(accounts_bin, stage / "bin" / f"account_server{EXE}")
    shutil.copy2(hiscores_bin, stage / "bin" / f"hiscores_server{EXE}")
    shutil.copy2(daemon_bin, stage / "bin" / f"torirsd{EXE}")
    log(f"servers: {game_bin.relative_to(REPO)}, {io_bin.relative_to(REPO)}, "
        f"{website_bin.relative_to(REPO)}, {accounts_bin.relative_to(REPO)}, "
        f"{hiscores_bin.relative_to(REPO)}")
    stage_runtime_dlls(stage / "bin")
    stage_web(stage)
    if LANE:
        # Rebaked every time: a copy of the base and a dozen archives, and a
        # stale lane cache would ship last week's base under this week's name.
        make([f"torirsserver-cache-{LANE}"], jobs=1)
        cache_src = REPO / f"cache.{WORLD}.{LANE}"
    else:
        cache_src = REPO / f"cache.{WORLD}"
    cache_bytes = copy_tree(cache_src, stage / f"cache.{WORLD}")
    log(f"cache.{WORLD}: {cache_bytes / 1e6:.0f} MB from {cache_src.name}")
    stage_content(stage)
    stage_admin_art(stage, args.jobs)
    write_manifest(stage, args.game_port, args.ws_host, args.auth)
    write_replay_manifest(stage)
    copy_tree(REPO / "revconfig" / WORLD, stage / "revconfig" / WORLD)
    copy_tree(REPO / "script", stage / "script")
    stage_quest_tests(stage)
    stage_scripts(stage, args.game_port, args.web_port, args.ws_host, sha, content_sha, date)
    write_daemon_config(stage, args.game_port, args.web_port, args.gate)
    # The staged torirsd reads the staged file: a config it refuses fails the
    # package here, not the deployment's first start.
    check = subprocess.run([str(stage / "bin" / f"torirsd{EXE}"), "--check", "torirsd.ini"],
                           cwd=stage, capture_output=True, text=True)
    if check.returncode != 0:
        die(f"torirsd refuses the staged torirsd.ini:\n{check.stdout}{check.stderr}")
    if args.android:
        stage_android(stage, args.jobs, sha, content_sha, date, args.android_key, args.android_apk)
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
