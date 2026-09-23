# The osrs239 world in a container, on the Windows box

The container is **only the execution environment**. It holds no game binary, no
cache, no content and no web client — all of that is bind-mounted in at run
time. Shipping a new server build or a content change is a file copy and a
`docker compose restart`; the image is rebuilt only when the *runtime* itself
changes, which is almost never.

Everything here was built and verified against the real box (Windows 11
26200.9445, WSL 2.7.13, Ubuntu 26.04, 32 cores / 31 GB) alongside the live
native deployment, which was never interrupted.

## What persists, and where

Three things live on the real Windows disk and survive anything that happens to
the container — an image rebuild, `docker compose down`, `docker system prune`,
deleting the container outright:

| what | where | who writes it |
|---|---|---|
| player saves | `C:\torirs\osrs239\saves\*.ini` | torirsserver, on logout |
| player settings | inside those same save files | torirsserver |
| server logs | `C:\torirs\osrs239\logs\` | the supervisor |

**Player settings are part of the player save** — `[chat]` filters, run toggle,
client layout mode, appearance, stats and bank are sections of the same INI,
which is hand-editable (the file says so in its own header).

### What is NOT persisted, and why it can't be

Plugin settings (`plugin_prefs.ini`, `plugin_assets/`) and client device
settings (`preferences.ini` — volumes, resolution, HiDPI) are **device-local and
never reach the server**. For a browser player they live in that browser's
storage, on that player's machine.

This is not an oversight in the packaging, it is a deliberate boundary in the
code: `io_server_main.c` refuses `TORIRS_IOK_FILE_READ` and
`TORIRS_IOK_FILE_WRITE` by kind, with the comment that honouring one would let a
remote client name a path this process would then write with its own
privileges. Mounting a directory cannot change that. Making plugin settings
follow an account across browsers is a **server feature** — an account-keyed
store with its own path validation and size limits — not a deployment change.

## Stopping is safe now, and it was not before

**torirsserver writes a player's save only when that player logs out.** There is
no periodic autosave. Before this work every stop — a container restart, a
supervisor swap, Ctrl-C — silently threw away everything every logged-in player
had done since they arrived.

`torirs_server_main.c` now handles SIGTERM and SIGINT: the handler only sets a
flag, and `ToriRSServer_HostRun` logs every connection out at the top of its
next pass, which is what writes the saves. The supervisor here forwards SIGTERM
and then *waits*, and `stop_grace_period: 45s` gives it room. Shortening that
below `TORIRS_STOP_TIMEOUT` (20s) would let Docker's SIGKILL land mid-save.

Verified on the box: a client logged in over the LAN, teleported to Varrock,
then `docker compose stop` — which took 1.8s and logged *"game server exited
cleanly after 1s; saves written"*. The save on the Windows disk holds
`x = 3218, z = 3424`, the teleported position, not the login position. The same
scenario under SIGKILL leaves no save file at all.

So `docker compose down|stop|restart` are safe. `docker kill` is not, and never
was.

## One-time setup

All of it runs on the Windows box. The native deployment under
`C:\torirs\osrs239` keeps running throughout; nothing here stops it.

### 1. `.wslconfig`

Copy `wslconfig.torirs` to `C:\Users\mrobe\.wslconfig`, then `wsl --shutdown`.

It sets `vmIdleTimeout=-1` and, deliberately, does **not** set
`networkingMode=mirrored`. Mirrored mode was tried first and does not work here:
it came up correctly (the distro saw the host's own interfaces, including
`eth1 10.0.0.5/24`, and host loopback reached WSL listeners), but inbound
connections from the LAN to `10.0.0.5:<port>` were refused — with the Hyper-V
firewall's `DefaultInboundAction` set to `Allow` and per-port allow rules
present in both the persistent and active stores. `10.0.0.5` on this machine is
a **WireGuard tunnel adapter**, not a physical NIC, and mirrored mode does not
mirror inbound on it. So the world is reached the classic way: NAT plus
`netsh interface portproxy`.

### 2. Docker Engine in the distro

    wsl -d Ubuntu -- bash /mnt/c/<path>/install-docker-wsl.sh

Ubuntu's own `docker.io` + `docker-compose-v2` (29.1.3 / 2.40.3 on this box),
not Docker's apt repo — that repo is keyed by release codename and has no suite
for 26.04. Not Docker Desktop either: its value is Windows-side integration, and
a server wants a daemon. systemd is already PID 1 here, so `systemctl enable`
means the daemon returns whenever the distro starts.

### 3. Configure

    cp .env.example .env      # then edit

`TORIRS_GAME_PORT` / `TORIRS_WEB_PORT` are the **published** ports only; inside
the container the servers always bind 43594 and 8088. Setting the host port as
the container's port is the obvious mistake and it is silent — the mapping then
points at a port nothing listens on while the log says "listening on
0.0.0.0:43694", and only the healthcheck notices.

### 4. Build the Linux binaries

The deployment's `bin\` holds Windows `.exe`; the container needs ELF binaries,
which go beside them in `bin-linux\`:

    ./build-linux-binaries.sh /path/to/repo /mnt/c/torirs/osrs239

A throwaway builder image does the compiling, so the distro needs no gcc, make
or python. It builds into private objdirs (`src/build_linux*`) so it cannot mix
object files with the MinGW build `make.ps1` puts in `src/build_opt`. It checks
the results are ELF before installing them. **46 seconds** on this box.

Keeping `bin\*.exe` in place is the rollback: stop the container, start
`supervise.ps1`, and the world is back on Windows binaries against the same
saves.

### 5. Stage the payload onto the distro's own disk

    ./sync-payload.sh

This is not optional, and it is the single biggest thing that makes the
deployment usable. `/mnt/c` is a 9p mount, and the world's boot reads a great
many small files. Measured on this box, same binaries, same content:

| payload on | boot to "listening" |
|---|---|
| `/mnt/c` (9p) | still not finished after 8 minutes |
| ext4 (`/var/lib/torirs/osrs239`) | **10 seconds** |

The Windows copy stays canonical — it is where a deploy lands, what the native
lane runs and what gets backed up. This is a cache of it; the sync takes ~100s
and deleting it costs nothing but a re-sync. Saves and logs are excluded and
bind-mounted straight from the Windows disk, which is the whole point of them.

### 6. Up, and forwarded

    docker compose up -d --build
    .\start-torirs.ps1                 # elevated

`start-torirs.ps1` starts the distro, waits for the ports to answer inside it,
points Windows at them with `netsh interface portproxy`, opens the Windows
firewall to `10.0.0.0/24`, and then holds a WSL session open forever.

### 7. Autostart

    .\install-wsl-keepalive.ps1        # elevated, once

Registers `start-torirs.ps1` as a startup task, running as the interactive user
rather than SYSTEM (WSL distros are per-user; SYSTEM has none).

**Edit the task's ports before using it on the live ports** — it defaults to
43594/8088, which the native deployment currently holds.

## The thing that will bite you

**WSL stops a distro when nothing is attached to it, and that stops dockerd,
which stops the container.** It stops it *cleanly* — SIGTERM, everyone logged
out and saved, exit code 0 — so there is no crash and nothing in any log except
the world no longer being there.

Seen repeatedly while building this: the container was stopped and restarted on
every ssh session, and the world never once finished booting because each boot
was cut off by the next teardown. `vmIdleTimeout=-1` stops the idle timer; the
held session in `start-torirs.ps1` stops the "no sessions left" teardown. Both
are needed. If `wsl -l -v` ever reads `Stopped` while the world is supposed to
be up, this is why.

## Updating without rebuilding the image

    # new server build
    ./build-linux-binaries.sh /path/to/repo /mnt/c/torirs/osrs239
    ./sync-payload.sh && docker compose restart

    # new content or cache: copy it into C:\torirs\osrs239\, then
    ./sync-payload.sh && docker compose restart

## Sandboxing

All capabilities dropped, read-only root filesystem (`/tmp` a 64 MB tmpfs),
`no-new-privileges`, non-root user, 512-pid cap, 4 GB / 8 CPU limits, only the
two ports published. Neither server needs privilege: both bind ports above 1024,
read files and talk TCP.

The payload is mounted read-only **except `cache.osrs239`**, which has to be
writable because `RSCache_Dat2DiskNewFromDirectory` opens `main_file_cache.dat2`
`"rb+"`. Nothing writes it; the mode is what the open demands. torirsserver does
not fail loudly on a read-only cache — it logs "no cache at ..." and carries on
with no equipment slots, no npc metadata and no varbit table, which presents as
a broken world rather than a bad mount. A read-only open exists
(`RSCache_Dat2DiskNewReadOnlyFromDirectory`); moving the ~15 metadata loaders
onto it would let that line go back to `:ro`.

## Troubleshooting

- **`exec format error`** — `bin-linux/` has Windows binaries. Re-run
  `build-linux-binaries.sh`; it checks for ELF and refuses otherwise.
- **`GLIBC_2.xx not found`** — `Dockerfile` and `Dockerfile.builder` have drifted
  to different Debian releases. They are pinned to the same one on purpose.
- **Container exits immediately** — the entrypoint preflights every mount and
  says which one is missing.
- **`create mountpoint ...: read-only file system`** — the payload has no
  `saves/` or `logs/` directory for the bind mounts to land on. `sync-payload.sh`
  creates both; re-run it.
- **Port unreachable from 10.0.0.1** — re-run `start-torirs.ps1`. WSL's address
  changes every boot and a stale portproxy rule points at the old one. The
  Windows firewall *drops* rather than refuses, so this presents as a hang.
- **Container healthy but nobody can connect** — check `TORIRS_GAME_PORT` is not
  also being passed as the container's internal port (see step 3).
