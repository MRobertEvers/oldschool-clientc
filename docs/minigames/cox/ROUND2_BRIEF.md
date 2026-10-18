# CoX round 2: the room solver and its test

Read with `COX_FINISH_PLAN.md` (rules, cross-cutting items) and
`docs/CONTENT_SOLVER_MANUAL.md` Parts 2 (phases 5-9) and 3. Your round-1 spec
and solver plan (`rooms/<room>_spec.md`, `rooms/<room>_solver.md`) are the design.

## What you deliver

1. `script/plugins/quest_driver/cox2_<room>.lua` (already listed in the parts
   header and loaded by every binary; you own it): a `QD.T2` room registered as
   `cox_<room>` (`QD.T2.register("cox_<room>", {...})`, which gives
   `t.raid.cox_<room>_solve(opts)`). The finished example is `cox2_olm.lua`
   (Olm, 96/96): PREDICT + STRATEGY in the room file; plan (`api_drive.aplan`),
   executive, monitor and supplies shared (`tob2_*.lua`).
2. `test/raids/cox_<room>.lua`: kit (start from `QD.COX.kit_cheats`; the Olm
   test's own copy is in `test/raids/cox_olm_solo.lua`), then
   `t.raid.cox_test_enter("<room>", n)` with n taken from the run name's seed
   (so a sweep sees every variant and turn), then the solver, then the PASS
   checks. GREEN, decided before the sweep: the room done (its exit open), the
   player alive, and the zeros your plan names (no damage from a dodgeable
   mechanic, no wrong-prayer hits, ...).
3. Probe 1 seed, read the whole tick log and stdout (`abort`, `script error`),
   batch the fixes; then 8 seeds; then 48 (`--name <room>N` for N = 0..47, n =
   N). Report pass counts and every failure's cause.
4. Fix the selftests in `cox_selftest.rs2` that pin your room's old behaviour
   (small Edit calls; only your room's procs).
5. The room's EXIT BLOCKER, if it is not in yet: OSRS blocks a combat room's
   way out until it is done. Near-Reality: a crystal (loc 30017/30018, per
   variant tile; TektonRoom, VasaNistirioRoom, VanguardRoom, MuttadileRoom)
   that shatters when cleared (`Raid.shatterCrystal`: the loc's disappear
   animation, the explosion and shatter sounds, removed 2 ticks later);
   Vespula's boils; the Mystics' mark of power; the Shamans' blocking spawns;
   the Guardians themselves. Port it with its tiles turned through the room
   funnels, and have the solver see it go.
6. If the room's entrance has tendrils (Muttadiles: noxious, Vespula: septic,
   Shamans: spirit), implement their op (Near-Reality
   `plugins/object/{Noxious,Septic,Spirit}Tendrils.java`, refined by the wiki)
   in your room file, and have the solver take them.

## What the foundation gives you (cox2_raid.lua, cox.rs2)

- `t.raid.cox_test_enter(room, n)` -> "ok", { seed, cx, cz }: the n-th raid
  holding the room, started; the room before cleared; you past its Passage, or
  on its way-out edge when it is an open room. Walk in yourself.
- `QD.COX.here()` / `QD.COX.room_at(x, z, level)`: { name, variant, turn, ox, oz,
  level } from `api_drive.instance_zone` (the template grid a client is sent;
  both lanes). `QD.COX.world(room, lx, lz)` / `QD.COX.template(room, x, z)` turn
  template room-local tiles (your spec's tables) to world and back.
  `QD.COX.exit_passage(room)`, `QD.COX.enter_passage(room)`,
  `QD.raid.cox_walk_near(x, z)`.
- npc rows now carry `splats`: every hitsplat showing, { damage, type, start }.
- A room's rule on a player's hit: add your npc types to `~cox_prepares_hit` and
  one dispatch line to `~cox_prepare_player_hit` in `cox_combat.rs2` (Edit tool,
  one line each), calling a proc in your own file.

## Rules (unchanged, plus)

- Perception only through `api_drive` (what a client is sent). No server varps,
  no trace channels, no cheats after the room starts.
- A missing drive verb: add it to BOTH lanes (scriptrun
  `src/torirsserver/torirs_server_scriptrun.c` and the live
  `src/plugin/torirs_plugin_drive_ui.c`), answering identically, with the Edit
  tool, in your own new function; say so in your report. Build your private
  server (`PLATFORM_OBJ_BASE=build_cox_<unit>`). Do not change engine behaviour
  (tick order, combat rules) yourself: report it.
- Your Lua file is part of one chunk: write to a scratch copy, `luac -p` it,
  then copy it in; keep top-level `local`s to one or two.
- Shared configs only by small Edit calls; `python3 tools/cox/locked_packs.py
  --retry 15` for packs; never stash/checkout/reset/clean; never commit.
- Do not thrash: plan, code in full, probe one seed, read everything, fix in a
  batch.

## The whole-raid contract (2026-10-18, `test/raids/cox_raid_solo.lua`)

The whole raid calls `t.raid.cox_<room>_solve({ max_ticks = N })` with NOTHING
else from where the room before left the player: past its Passage, or on an
open room's way-out edge (`t.raid.cox_leave_room`), exactly where `::coxtest`
leaves a room test. So a solver:
- finds its own room (`QD.COX.room_at` on the room ahead / `QD.COX.here()`),
  never `::coxtest`'s answer (`opts.centre` etc.) -- the test may pass extras,
  the solver must not need them;
- takes its own entrance (tendrils) and returns "ok" once the room is done and
  its exit is open, standing anywhere in the room;
- carries no state between calls (a raid has each room once), and uses only
  the kit `cox_raid_solo.lua` gives (the Olm kit + the raid's potions); if your
  room needs an item beyond it (a pickaxe, an axe ...), say so -- in OSRS it
  comes from the room or the scavengers, and the relay will fetch it.
