# Live LostCity Server — Debug Session Handoff

> **`::tele` takes underscores, not commas.** `::tele 0_50_50_21_21`. The comma
> form in some older recipes below fails with "nowhere called 0,50,50,21,21" and
> the run then CONTINUES from wherever the player already was — so a harness
> using it has been measuring the login tile, silently. `~tele_resolve` reads one
> word and decides name-or-coord by its first character (cheat_tele.rs2); a comma
> literal is neither.


Status and map of the work that made `src/torirs` play against a real
LostCity_Server (Engine-TS rev 254). Written for an agent picking this up cold.
Prior context: the v3 branch already had the full protocol port — canonical
packet names, lc254 rev tables, fully-async packet exec (two TaskRunners, no
blocking Drain outside `*/test/`), PLAYER/NPC_INFO bit decode → World,
walkmerge, outbound builders, and minimenu/inventory/chat interaction wiring.
This session debugged that stack against the live server and fixed what broke.

## Current state (verified live)

Login, REBUILD_NORMAL → async world load, local player + NPC spawn/movement,
click-to-walk (MOVE_GAMECLICK → server path → PLAYER_INFO walkdirs), a camera
that follows the local player, chat MESSAGE_GAME, all 14 IF_SETTAB side-tab
mounts with live UPDATE_STAT data, VARP sync. All 16 test targets green
(`make -C src <target>`; see `check-no-drain` too). Offline (`src/torirs
cache254` with no `--connect`) is unchanged.

How to run: see "Running against a LostCity server" at the bottom of the root
`readme.md`, or `./run-live.sh [host] [user] [pass]`.
Reference server: `/Users/matthewevers/Documents/git_repos/LostCity_Server`
(authoritative for every wire question). Reference client:
`Client-TS/src/client/Client.ts` in this repo.

## The six bugs fixed (why the connection "hung silently")

Each of these alone bricked the session; they were stacked:

1. **Dropped login hello** — `src/platform/platform_socket.c`:
   `ToriRS_Network_ConnectLogin` pushes CONNECT + the first login bytes into
   the out-ring in one tick; the poll drained both but discarded SEND_DATA
   while the non-blocking connect was in flight. Fix: `out_pending` buffer,
   flushed on connect-success / each poll (also covers would-block partials).
2. **No hostname resolution** — `src/platform/sockstream.c` only took IPv4
   literals ("localhost" → "Invalid address" → connect to 0.0.0.0). Fix:
   `getaddrinfo` fallback.
3. **Login success is 3 bytes** — server writes `[2, staffmodlevel,
   mouseTracked]` (engine/src/engine/World.ts:894). We consumed only the `2`;
   the two leftover bytes shifted the inbound stream so every ISAAC-decoded
   opcode was garbage forever. Fix: `LOGINPROTO_LOGIN_SUCCESS_TAIL` state in
   `src/net/loginproto.c`. Reply 15 = reconnect handoff (single byte, no
   tail); 18/19 are NOT success. Reply 5 = already-logged-in (see gotchas).
   The mock server (`src/torirsserver/mock_server.c`) now emits the 3-byte form.
   *Symptom signature for this class of bug*: `net: dropping unknown wire
   opcode N` runs with size=0 interleaved with occasional plausible packets.
4. **NPC_INFO slot width** — rev 254 new-NPC block is `gbits(14)` slot,
   terminator 16383 (Client.ts `getNpcPosNewVis`), not v0-245_2's 13/8191.
   This is the client's local slot for one nearby NPC instance, not the cache
   NPC definition/type id; the type is a separate field in the add record.
   One bit off → garbage npc type ids + all NPCs on one tile. Fixed in
   `src/net/rev/pkt_npc_info.c` (+ `src/net/test/entity_decode_test.c`).
5. **Scene coordinate spaces** — ~~server "local" coords are relative to the
   classic 104×104 scene origin, but our scene was map-square aligned~~ —
   **unified 2026-08-03.** `REBUILD_NORMAL` now builds a classic 104×104
   scene via `WorldBuilder_RebuildCenterzone(zone, 104)` with
   `_base_tile = (zone-6)*8` (Client-TS / deob `method3310`). `scene_off`
   is gone; server-local tiles are our-scene tiles. FACE_COORD wire is
   still absolute half-tiles `(tile<<1)+1`. Outbound MOVE_GAMECLICK /
   OPLOC / OPOBJ send ABSOLUTE tiles (scene + `_base_tile`); OPNPC/OPPLAYER
   send server slots.
6. **Hover text ate world clicks** — `UIELEM_BUILTIN_HOVERTEXT` (full-canvas
   overlay, com id 0x7ffe0002) was missing from
   `UITree_ComponentIsPassThrough` (`src/ui/uitree_input.c`), so every world
   left-click hit it and no MOVE_GAMECLICK was ever sent. Latent offline too.

Also hardened: `read_string` in `3rd/rscache/src/rsbuffer.c` looped forever at
end-of-buffer (G1 past end returns 0 *without advancing*) — a truncated/
misframed packet hung the client inside `gameproto_parse`. Now bounded.

## New behavior added

- **Camera follow** — `app_world_camera_follow` in `src/app.c`, called from
  `app_world_frame` after `app_world_sync_positions`. Exact port of Client-TS
  `camFollow` (16.16 sin/cos via `ToriDraw_Sin/Cos`, distance = pitch*3+600).
  Gated off when `cam_script.scripted` or offline (`!app->net`). Pitch is
  pinned at 383 (upper orbit clamp) instead of the reference default 128
  because there is **no roof-hiding** yet — at 128 an indoor player is hidden
  behind the roof. See follow-ons.
- **Harness envs** (all in `src/main.c` / `src/app.c`):
  - `TORIRS_EXIT_BMP=path` — render+dump the final frame after
    `TORIRS_MAX_FRAMES` (the pre-existing `TORIRS_WORLD_BMP` path exits
    *before* the network loop, useless for live tests).
  - `TORIRS_SIM_CLICK_AT="frame,x,y[,right][;frame,x,y…]"` — inject clicks in
    the live main loop. Mouse-move lands at frame N, press N+3, release N+4:
    the world pickset is built during render from the hover position, so the
    move must precede the press by a frame or the click hits nothing.
  - `TORIRS_NET_CHEAT="tele 0,50,50,21,21;give coins"` — ';'-separated `::`
    commands sent once on entering GAME state (one-shot latch
    `app->net_cheat_sent`).
  - `TORIRS_NET_DEBUG=1` now also logs: per-packet `wire=/name=/size=`
    (net.c), outbound byte counts (`net: ->`), settab mounts, and click
    classification (`click: miss=… com=… gate=… picks=…`).

## Operational gotchas (will bite you)

- **Login reply 5** right after a previous run: the server still holds the
  killed session. Wait ~8s between live runs. When login is rejected the
  client silently continues in OFFLINE mode — check the log for
  `loginproto: login rejected, reply=N` before trusting a run's results
  (an offline run still shows a world and working tabs and once burned half
  an hour of analysis).
- **Cheat syntax**: tele is comma-separated `tele level,mx,mz,lx,lz`
  (underscores fail silently). Cheat gates: tele needs staffmod ≥2, `give`
  needs ≥3 — against this server instance give was silently ignored
  (behaved as level 2), so inventory couldn't be item-tested live.
- Fresh accounts spawn on Tutorial Island **indoors** — without roof-hiding
  you mostly see roof. `TORIRS_NET_CHEAT="tele 0,50,50,21,21"` → Lumbridge
  courtyard.
- stdout is block-buffered through pipes while stderr isn't; interleaved log
  order lies. Don't infer event ordering across the two streams.
- Debug BMPs → `sips -s format png x.bmp --out x.png` (no PIL on this box).
- CRCs: `TORIRS_JAG_CRC` = 9 comma-separated int32s from
  `curl http://localhost/crc` (big-endian); only changes when the server
  repacks. `run-live.sh` fetches it automatically.

## Verification pattern that worked

Headless live run + screenshot + grep, e.g.:

```bash
SDL_VIDEODRIVER=dummy TORIRS_NET_DEBUG=1 TORIRS_MAX_FRAMES=2600 \
TORIRS_NET_CHEAT="tele 0,50,50,21,21" \
TORIRS_SIM_CLICK_AT="1300,583,188;1700,390,95" \
TORIRS_EXIT_BMP=/tmp/live.bmp TORIRS_JAG_CRC=… \
src/torirs cache254 --connect localhost --user debugcc --pass test 2>&1 | tee run.log
```

Then: `grep reply= run.log` (really logged in?), `grep -c wire=209` (REBUILD),
`grep walk-click` (outbound), inspect the BMP. Click coords used: 583,188 =
stats tab icon; viewport is roughly x 8..512, y 8..340.

## Known follow-ons (not started unless noted)

- **Roof-hiding** — the big one; blocks indoor play at reference camera pitch.
  Needs scene-level support: `ToriDraw_SceneElement` has no level/hide field,
  so the world builder must tag elements with their plane and the renderer
  cull planes above the player (reference: Client.ts `roofCheck`).
- Minimap click-to-walk: `net_out_move_minimapclick` exists (incl. 14-byte
  anticheat trailer) but nothing in app.c calls it.
- Skill *levels* text in the stats tab renders "/" with blank numbers.
- OPNPC/OPLOC live round-trip never observed end-to-end (wire formats verified
  against server decoders; the sim clicks kept resolving to "Walk here").
  A targeted test: click directly on an NPC → expect OPNPC1 + dialogue.
- `Socket recv error: invalid stream` prints once at startup — cosmetic.
- Earlier follow-ons still open: entity spotanim visuals, overhead chat text,
  audio playback, hint-arrow/minimap-flag drawing, LOC_ADD_CHANGE builder
  spawn hook.
- `run-live.sh` needs `chmod +x` (tooling outage blocked it; `sh run-live.sh`
  works regardless).

## Memory files

Persistent memory (`~/.claude/projects/...3draster/memory/`) has the same
content in compressed form: `live-server-debug.md` (this session) on top of
`entity-sync-async-protocol.md` and `command-bus-and-net-stack.md` (the two
build-out sessions this debugging sits on).
