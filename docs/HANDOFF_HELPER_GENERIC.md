# Handoff: the `helper_generic` panel — settings 163, 184, 275, 268

A self-contained server-side task. Everything it depends on has landed; nothing
else in flight touches the files it needs.

## What you are building

Four All Settings > Activities rows whose display is **already fully built in
the cache** and whose only missing piece is a server that opens the panel and
runs the cache's own builder:

| id | row | varbit | entry point |
|---:|-----|--------|-------------|
| 163 | Agility helper | 12379 `agility_helper_disabled` | clientscript **5170** |
| 184 | Slayer helper | 13082 `slayer_helper_disabled` | clientscript **5317** |
| 275 | Clue scroll helper - Infobox | 14187 `option_cluehelper_infobox_enabled` | clientscript **6631**(clue db row) |
| 268 | Blast Furnace helper | 14180 `blast_furnace_helper_disabled` | interface **474** `blast_furnace_hud`, server-driven, no clientscript |

All four varbits are **inverted** except 14187 — the cache states the sense in
the gameval name (`_disabled` vs `_enabled`). Read an inverted row the plain way
and the helper appears for exactly the players who switched it off.

## Why this is the shape it is

`5170`, `5317` and `6631` have **zero callers anywhere in `cache.osrs239`**.
That is the signature of an entry point the server is expected to call — the
same signature `NXT_CLIENT_PLUGINS.md` already identified for the respawn timers
(5471 / 5475 / 5478). Verify it yourself before you start:

```sh
3rd/rscache/tools/cs2/cs2 decompile --cache cache.osrs239 --rev osrs239 --out /tmp/cs2all
grep -rl 'script5170\b' /tmp/cs2all | grep -v 'script5170\]'   # empty
```

The *content* of each helper is the cache's, not yours to author:

- **5182** (Agility, reached from 5170) reads `enum_3507` keyed on
  `%varbit12633 helper_agility_current_course` for the course name, and the lap
  counters out of varcs.
- **5318** (Slayer, from 5317) builds "Slayer Info" and re-arms itself on
  `var394 slayer_count`, `var395 slayer_target`, `var2096 slayer_area`,
  `var1077`/`var1565 slayer_tasks_completed_*`, `var661`.
- **6633** (clue, from 6631) switches on `db_getrowtable(%var3546)` and
  dispatches to 6634..6644, one per clue step kind.

So there is no per-course, per-task or per-clue table to write. If you find
yourself authoring one, stop — you have the wrong seam.

## Where it mounts

`toplevel_osrs_stretch` component **4 = `helper`**, from
`OSRS-Content/osrs239-content/interfaces/toplevel_osrs_stretch.compack`. Not the
floater (component 18) — the world map lives there — and not `hpbar_hud`
(component 2), which is the enemy health overlay.

Register it the way the gameframe slots already are, in
`src/torirsserver/torirs_server_ids.c`:

```c
{ TORIRSSERVER_PACK_COMPONENT, "toplevel_osrs_stretch:helper", &g_ids.com_gameframe_helper },
{ TORIRSSERVER_PACK_INTERFACE, "helper_generic", &g_ids.iface_helper_generic },
{ TORIRSSERVER_PACK_INTERFACE, "blast_furnace_hud", &g_ids.iface_blast_furnace_hud },
```

The builders call `cc_create(interface_711:5, ...)`, so **the interface must be
mounted before the builder runs**. That ordering is the whole trap, and the
enemy health overlay next door has already been through it — read the "the open
comes FIRST, and the data one tick later" comment in
`src/torirsserver/torirs_server_hpbar.c` before you decide your own tick order.
`helper_generic` differs from `hpbar_hud` in one way that matters: it paints
from a `RUNCLIENTSCRIPT` you send, not from a var-transmit hook, so you control
the ordering directly rather than having to leave a tick's gap.

## The template

`src/torirsserver/torirs_server_hpbar.c` is the model, end to end: a per-tick
function called from the world tick, a `*_open` latch on the player, a linger
counter, `SendIfOpensub` / `SendIfClosesub`, and the setting read every tick
rather than once at the open so switching it off mid-session closes the panel.

Copy the shape; do not copy the file into it. `ToriRSServer_HpBarTick` is being
actively edited in another session — **do not modify
`torirs_server_hpbar.c`**. Write `src/torirsserver/torirs_server_helper.c`.

Register the tick beside the hpbar's in the world tick, and add the sources to
`TORIRSSERVER_CORE_SRCS` in `src/makefile`.

## What has already landed for you

**The settings reach the server.** This was the blocker for all ten server-side
rows and it is done. The panel's varbit write is client-local
(`VarPManager_SetVarbitOptimistic`) and revision 239 has no client packet that
carries it — verified against the whole prot table in
`3rd/rsprot/gen/rev239_prot.h`, against NXT's `ClientVarCache::SetVarbit`, and
against interface 134's script family. So the client now mirrors settings writes
over `CLIENT_CHEAT` as `::setting <varbit> <value>`; see `settings_mirror_varbit`
in `src/game/rs_cs2_host.h` for the full reasoning, including why it is
CLIENT_CHEAT and not a new opcode.

Which means you can just read the varbit:

```c
if( ToriRSServer_VarbitGet(player, ids->varbit_agility_helper_off) )
    return;                 /* inverted: 1 is OFF */
```

and you can drive it from a headless run without a panel click:

```sh
SDL_VIDEODRIVER=dummy TORIRS_SETTINGS_DEBUG=1 TORIRSSERVER_SETTINGS_DEBUG=1 \
  TORIRS_SIM_VARBIT="450,12379,0" TORIRS_MAX_FRAMES=700 \
  ./run-live.sh manifests/manifest_osrs239.ini testc test
```

which prints, today:

```
sim_varbit: 12379 = 0 (base varp 3075, reads back 0)
settings: mirror varbit 12379 = 0 -> server
setting: varbit 12379 = 0 (player testc)
```

**`ToriRSServer_SendRunClientscript(player, id, args, argc)`** already exists and
is already used at login for 5487 — see the comment block around
`torirs_server_world.c:10583`, which is also the precedent for "the cache
expects the server to call this and nothing does".

## What still needs finding

Two inputs are not obviously written anywhere yet, and finding out is part of
the task rather than a blocker on it:

- **`%varbit12633 helper_agility_current_course`** — nothing in
  `OSRS-Content/.../server/scripts/` writes it. The agility lane does track laps
  (`agility_lap_course` / `agility_lap_step` in
  `skill_agility/configs/agility.varp`), so the course id is derivable; whether
  the mapping from lane course to the cache's course id already exists is
  unchecked.
- **The slayer task varps** (394, 395, 2096) — `skill_slayer/` exists; whether it
  writes those specific varps is unchecked.

If either is genuinely absent, write it in the content lane rather than in C —
the helper reads varps precisely so that the task state stays content's.

## How to know it worked

Not by screenshot. An empty helper panel and a helper that never opened are the
same picture, and so is a helper drawn off-viewport. Pin all three separately:

1. `TORIRSSERVER_VERBOSE=1` plus your own debug channel (follow
   `TORIRS_HPBAR_DEBUG`'s example) for "the open went out" and "the builder ran".
2. `TORIRS_DUMP_TREE=1` on the client for "the layer exists under the `helper`
   slot with children".
3. A stanza in the C selftest for the decision — which helper the tick chooses
   for a given player state, and that an inverted row switched off chooses none.
   `src/torirsserver/torirs_server_world_selftest.c` has a worked example added
   for the hitsplat promoter; copy its shape, and **mutate your own function and
   watch the stanza go red before you believe it**. The suite currently reports
   28 pre-existing failures (ToB, Inferno, quests, QBD) — record the count before
   you start so you can tell yours apart.

One warning about that suite: `make -C src test-torirsserver` did not rebuild the
server binary until this change, because it depended on a `.PHONY` target named
`ToriRSServer` that has no rule. It is fixed, but if you see a result that
cannot be right, check the binary's timestamp first.

## Out of scope for this slice

Being worked on or deliberately parked elsewhere: the hitsplat settings (5, 279,
280 — done), the boss health overlay veto (10 — done), the enemy health overlay
(111 / 299 / 300 / 301 — another session), the clue helper's worldmap marker and
world arrows (272 / 273 — client half done, server half open), and the iron loot
warnings (182 / 183 — open).
