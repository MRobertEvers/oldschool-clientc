# Cloud environment for Claude Code on the web

The environment itself (network access, setup script, environment variables)
lives in claude.ai, not in this repo: session title bar → cloud environment
menu → **Edit**. This directory records what to put there; the repo's
`SessionStart` hook (`.claude/hooks/session-start.sh`, registered in
`.claude/settings.json`) does the per-checkout work.

## Environment settings ("Oldschool Client")

| Field | Value |
|---|---|
| Network access | **Custom** with `archive.openrs2.org` allowed, or **Full**. The default *Trusted* level denies OpenRS2, so the cache cannot be fetched. |
| Environment variables | none needed |
| Setup script | contents of [`setup-script.sh`](setup-script.sh) |

Environment edits apply to **new** sessions only.

## What a session gets

| Step | Where | Notes |
|---|---|---|
| `libsdl2-dev libgl1-mesa-dev imagemagick xdotool` | setup script, else the hook | Xvfb is already in the base image |
| `Client-TS` submodule | hook | public |
| `OSRS-Content` submodule | hook | **private** — the session must be granted `MRobertEvers/OSRS-Content` first; the hook warns and continues if it cannot clone |
| RuneStar cs2 name tables | hook → `~/Documents/git_repos/cs2` | where `RUNESTAR_CS2_NAMES` looks; the cache bake needs them |
| `cache.osrs239/` | hook → `tools/fetch_cache_osrs239.sh` | OpenRS2 #2644, ~180 MB zip; skipped when already present |

The client build is left to the session (`make -C src release`, ~1 min).

## Booting the cache built from OSRS-Content

```sh
make -C src torirsserver-cache          # -> cache.osrs239.baked, ~2.5 min
sed 's#^dir=../../cache.osrs239$#dir=../../cache.osrs239.baked#' \
    build/manifests/osrs239.ini > build/manifests/osrs239_baked.ini   # after one ./launch run osrs239
DISPLAY=:99 SDL_AUDIODRIVER=dummy TORIRS_TRANSPORT=embed \
    TORIRSSERVER_CACHE=$PWD/cache.osrs239.baked \
    ./src/torirs --manifest build/manifests/osrs239_baked.ini
```

## Running the client headless

```sh
Xvfb :99 -screen 0 1280x900x24 &
DISPLAY=:99 SDL_AUDIODRIVER=dummy ./launch run osrs239      # embedded server, cache.osrs239
DISPLAY=:99 import -window root shot.png                     # screenshot
DISPLAY=:99 xdotool mousemove 718 489 click 1                # "Existing User"
DISPLAY=:99 xdotool type testc; xdotool key Tab; xdotool type test; xdotool key Return
```

`manifests/manifest_osrs239.ini` itself reads `cache.osrs239.sparse` (the
JS5-hydrated lane, profile `osrs239-net-sparse`), not the pristine cache.
