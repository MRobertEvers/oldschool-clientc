# Raid driver notes

What a raid room-test author (`test/raids/<raid>_<room>.lua`) needs to know about the
verbs the raid seam passes added to the quest driver, and the engine facts those passes
measured. Each heading names what you see in a ledger row or a verb's detail. The verbs
are proved on ordinary npcs (a Lumbridge goblin or man) by their rows in
`test/quests/_conformance.lua`; the measurements quoted here come from those rows and
from the fixers' scratch runs under `build/quest_gate/`. Seam pass 1
(`matthew-mbp-m4-raid-b1-seam1`) wrote the first version of this file. The general
driver rules are in `docs/QUEST_AUTHORING.md`; this file covers only what is specific to
raids.

## Where a raid test lives and how it runs

- Raid tests live in `test/raids/<raid>_<room>.lua`. Never put one in `test/quests/`,
  because the quest loop's `run.py --all`, `gate.py --all` and `make test-quests` would
  pick it up.
- Run and grade them with `tools/raid_gate/run.py` and `tools/raid_gate/gate.py`. They
  take the same arguments as the quest gate scripts (`--no-build`, `--no-publish`,
  `--from-leg K`, `--script`/`--name`, `QUEST_BINARY=`).
- The wrappers set two environment variables that `tools/quest_gate/quest_list.py`
  reads. `TORIRS_QUEST_TESTS_DIR` sets the suite directory, and fixtures come from
  `<dir>/fixtures/`. `TORIRS_QUEST_PUBLISH_DIR` sets the publish root, and the publish
  subdirectory is the id split at its first `_`, so `tob_maiden` goes to `tob/maiden`.
  QUEUE.tsv is not read. With both variables unset, the quest gate behaves exactly as
  before.
- A passing room publishes to
  `OSRS-Content/osrs239-content/server/scripts/selftest/minigames/<raid>/<room>/play/`.
  Use `--no-publish` for scratch runs.
- A raid id must never equal a quest id. `build/quest_gate/<id>/`, the session lock and
  the relay checkpoints are keyed by id alone, so the wrappers exit 2 when a
  `test/raids/<id>.lua` matches a `test/quests` file or a QUEUE.tsv `test_id`.
- Fixtures are `test/raids/fixtures/*.ini`. `fresh_lumbridge.ini` is a copy of the quest
  fixture. Give any new state its own file there.
- `gate.py` grades a raid id as "coverage: not graded", because it has no QUEUE row or
  guide. Raid coverage from the encounter spec tables is a later seam.

## t.tick() is the server's tick; api_drive.tick() is not

`t.tick()` returns `(ok, srv->tick)`, the embedded server's own tick. Every
`t.ticklog` row is stamped on this clock. `api_drive.tick()`, which `t.ticks` and every
deadline use, is the client's world cycle divided by 30. It runs at the client's frame
pace and starts wherever the client started. On a socket-server run, or on a binary
built before the seam, `t.tick()` answers `unsupported`.

## The tick log: t.ticklog.start / rows / gaps / mark / slot

- `t.ticklog.start()` is idempotent. Its detail reads
  `ticklog on at tick T (now N, serial S) -> <run dir>/ticklog.tsv`. The log is off until
  a test starts it, so ordinary quest runs pay one branch per hook and write no file.
- `ticklog.tsv` starts with the header `ticklog-v1` and has the columns
  `serial tick kind a b c d e f label`. Field names per kind are in `QD.ticklog.FIELDS`
  (`script/plugins/quest_driver/ticklog.lua`). Coordinates are `ToriRSServer_CoordPack`
  values (`level<<28|x<<14|z`), and rows also give them unpacked as `x, z, level`,
  `src_x ...` and `dst_x ...`.
- Row kinds:
  - `npc_anim`: recorded after the priority gate, so it is what the client was sent.
  - `npc_spotanim`, `projectile`, `map_spotanim`.
  - `hit_player`: the splat as shown, after `::god`, absorption and the clamp. The
    dealing npc is the one bound to the script that called `damage`/`p_overhit`.
  - `hit_npc`.
  - `npc_spawn`, `npc_death` (the killing blow's tick), `npc_free` (the tick the npc
    really left, after any held free), `npc_retype`.
  - `loc_set`, `obj_add`.
  - `player_tile`: every tick, written after `phase_players`.
  - `npc_tile`: only when the tile changed, and only for npcs within 32 tiles of a
    player.
  - `mark`, `start`.
- `t.ticklog.rows(opts)` filters on `since`, `kind` (a name or a list), `slot`, `npc` (a
  `t.npc` row), `pid`, `type`, `seq`, `spotanim` and `where(fn)`. One kind and the slot
  are filtered in C. Always pass a kind: a Lumbridge run logs about 10,000 `npc_tile`
  rows in 200 ticks, and unfiltered Lua iteration ran out of the instruction budget
  (400,000).
- `t.ticklog.gaps(slot_or_row, kind, opts)` returns `ok, text, gaps, ticks`. The text
  reads `4, 4, 4 (3 gap(s) over 4 row(s), ticks 8..20)`. Use `opts.seq` to separate the
  attack animation from the block and death animations.
- `t.ticklog.mark(label)` writes a `mark` row at the current server tick. Tabs and
  newlines become spaces, and labels over 47 characters are cut.
- A relog or a world reset turns the log off (`ToriRSServer_WorldReset`). In the
  conformance harness the log stops at the `session.login` row. Start it again after a
  relog.

## Tick-log slots are the server's, not the client's

A `t.npc.nearest` row's `slot` is the client's per-client NPC_INFO name, not the world
slot the log uses. On a Lumbridge goblin, client slot 58 was world slot 632, and on a
man, client 6 was world 578. Translate with `t.ticklog.slot(row)` before the fight,
because after the npc despawns it answers `not_found`. You can also pass `opts.npc = row`
or `gaps(row, ...)`.

## A goblin's measured cadence

A goblin_unarmed_melee_1 has attackrate 4, attack seq 6184, defend seq 6183 and death
seq 6182. Its first swing comes 2 ticks after the first hit (the flinch, attackrate/2),
and then it swings exactly every 4 ticks. The melee `hit_player` row is on the same tick
as the attack animation. Measured in `tickseam_after4`:
`4, 4, 4, 4, 4, 4, 4 (7 gap(s) over 8 row(s), ticks 8..36)`, with 8 `hit_player` rows
for 8 swings and one `npc_death`.

## t.player.step_tick(x, z): one step, and the tick it landed

- The detail reads `step x,z issued at tick T, resolved at tick T+n (+n)`. The verb sends
  one `move_to` and never re-issues it, and it reads the resolve tick from the
  `player_tile` row. It starts the tick log if the log is off.
- T is the last tick the server finished. `+1` means the step landed on the first tick
  it could: the next tick's `phase_clients_in` reads the packet, and the step resolves in
  that tick's `phase_players`.
- ENCOUNTER_TIMING.md section 1: an npc acting on tick T+1 still scans the old tile,
  because `phase_npcs` runs first. Measured: `step 3222,3218 issued at tick 43, resolved
  at tick 44 (+1)`.
- A non-adjacent tile is refused (`refused: step_tick takes an adjacent tile; use
  walk_to`). A blocked tile times out (`no player_tile row reached it`), because there is
  no collision pre-check. For example, 3240,3245 beside the goblin field is not walkable.

## t.prayer.set(name, on): pressed by the prayer book, settled on the varbit

- `t.prayer.set` answers `ok`, `refused`, `no_row`, `not_found`, `not_visible` or
  `timeout`.
- Names use the content spelling: `protectfrommelee`, `protectfrommissiles`,
  `protectfrommagic`, `piety`, `rigour` and so on. A leading `prayer_` is stripped, and
  `QD.prayer.TABLE` lists all 29.
- The verb opens the prayer tab and presses `prayerbook:prayerN` as op 1 through
  `api_drive.if_click`, the IF_BUTTONX path a real click takes. It then waits for the
  prayer's varbit as the server sent it. Use it through `t.exec`.
- It never presses twice. If the varbit already holds the asked state, it answers `ok`
  with `no press made`, because another press would put a lit prayer out. A server
  refusal ("You need a Prayer level of", "You have run out of Prayer points", "You can't
  use protection prayers") answers `refused` with the server's line.
- Button order is not the on-screen order. `prayerbook:prayerN` is component 541:8+N, so
  prayer20 is Hawk Eye and prayer22 is Mystic Will (prayer.rs2:427-433).
- Turning on Protect from Missiles turns Protect from Melee off
  (`prayer_deactivate_conflicting`).
- The curses book is not handled.

## t.prayer.read() / t.prayer.points(): the overhead is not read

- `read()` returns `ok, detail, set`, where `set[name]` is a boolean for all 29 prayers,
  read from the varbits.
- The overhead icon is NOT read. The server keeps `headicons` as an engine int that no
  var or plugin reader carries. The detail names the icon the lit varbits imply (melee
  0, missiles 1, magic 2, retribution 3, smite 4, redemption 5).
- `points()` returns `skill.read("prayer")` with a detail.
- Neither verb takes a target, so `t.exec` writes FAIL `bad verb/target` for them.
  Record them with `t.expect` or `t.check`.

## A prayer press is in force for the next npc phase

The embedded server handles client packets as they arrive (`ToriRSServer_EmbedPump` ->
`pump_client`) and only then runs `ToriRSServer_WorldTick`. A prayer set between server
ticks T-1 and T is therefore in force for tick T's npc phase, which is the OSRS order of
input, then npcs, then players. The varbit reads back +0 drive ticks.

Protection works on an ordinary npc. Against a goblin_unarmed_melee_1, Protect from Melee
gave 24 engaged ticks with 0 damage, and it drained 5 prayer points from 43, one point
per 5 ticks.

## t.npc.state(selector): what the npc is doing

- `t.npc.state` returns `('ok', row)`, `not_found`, `no_row` or `unsupported`.
- `selector` is an npc symbol, which takes the nearest copy, optionally narrowed by
  `opts {slot=n}` or `{at={x,z[,level]}}`. A table alone selects from any npc.
- The row adds these fields:
  - `anim_id` and `anim_frame`: the action seq being drawn, -1 for none.
  - `spotanim_id`: the attached graphic being drawn.
  - `seq_id` and `seq_tick`: the newest SEQUENCE op the server sent, and the server tick
    it arrived on.
  - `spotanim_sent_id` and `spotanim_tick`.
  - `facing`: an npc slot, 32768 + pid for a player, or -1.
- Use `seq_tick`, never "did anim_id change". The client does not restart a seq that is
  re-sent while it is still playing (`world_apply_primary_animation`), so a boss
  repeating one attack seq never changes its drawn `anim_id`.
- `t.npc.state_text(row)` is the one-line reading for a ledger detail. Record it with
  `t.check(name, r == "ok", t.npc.state_text(row))`.

## t.npc.await_anim(selector, seq_or_nil, ticks): "the boss attacked on tick T"

- `t.npc.await_anim` returns `('ok', detail, tick, seq)` or `timeout`.
- It resolves the copy once and then waits for the next `npc_seq` drive event on that
  slot. A stop (seq -1) never matches. The tick is on `api_drive.tick()`'s clock.
- Measured on a Lumbridge goblin: 60, 64, 68, 72, 76 (attackrate 4).
- A melee npc that starts diagonal adds one step to the first gap (man: 23, 28, 32, 36,
  40). Grade cadence on the gaps after the first.

## An ordinary npc hit by magic may never retaliate (engine finding, open)

Several ordinary npcs did not fight back in the expected way:

- A man struck by Wind Strike swung once and then stopped (`npcst_after2`).
- In the full conformance harness, three runs in a row, the man never swung at all and
  `facing` stayed -1.
- A Lumbridge cow did not retaliate within 18 ticks.

In OSRS an npc keeps attacking. The likely cause is the npc mode machine resolving
`opplayer` against `srv->active_player` (player_magic.rs2:700-704). The `npc.await_anim`
conformance row works around it by having the player punch the man as well, at Attack
and Strength 1. Raid bosses run their own AI scripts. Any ordinary-npc proof that
relies on retaliation to magic alone will be flaky. `::passive` applies to an npc type,
so a spawned copy of a passive type is passive too.

## t.world.spotanims / t.world.projectiles / t.world.hazard_at: the tile hazards

- `spotanims(radius)` and `projectiles(radius)` return rows nearest first, and
  projectiles are ranked by destination.
- Spotanim rows: `{spotanim_id, x, z, level, active, cycles_left, element_id}`.
- Projectile rows: `{spotanim_id, src_x, src_z, dst_x, dst_z, level, target,
  target_npc_slot, launched, cycles_left, element_id}`.
- `cycles_left` is in CLIENT cycles, 30 to a tick. A homing projectile's `dst` is its
  target's live tile.
- Ids are numbers, because there is no spotanim symbol kind yet: windstrike_travel 91,
  windstrike_impact 92, failedspell_impact 85, telegrab_impact 144.
- `hazard_at(x, z[, level])` returns `('ok', {locs, objs, spotanims, projectiles, count,
  text})`. A projectile counts on the tile it is aimed at, and an empty tile is
  `count 0`, not an error.
- A map graphic lives only as long as its seq. For example, 144 lasts about 18 cycles.
  Poll it inside a `t.await` level that starts right after the verb that triggers it.
  Proved with Telekinetic Grab:
  `tile 3241,3247,0: loc 324; obj 1205 x1; spotanim 144 (active, 15 cycle(s) left)`.

## A drop pressed while a cast still holds the player does nothing

In the full conformance harness, a `drop` pressed right after a Telekinetic Grab (and a
`::goto`) answered `backpack 2 -> 2, ground 0 -> 0` twice, with no server line. Waiting
4 ticks after the grab before teleporting and dropping fixed it. When a hazard proof
places its own subject, let the previous action's delay run out first.

## Ground items: two identical drops are two rows

Every OBJ_ADD is now one ground row, as in Client-TS.

- Two identical non-stackable drops on one tile are TWO rows in `drive.objs`. One Take
  (OBJ_DEL, pos+id) removes the OLDEST matching row, and the other stays visible and
  takeable.
- `t.world.obj_near` returns the first row only. Count rows with
  `t.player._ground_on_tile(id)`.
- A public stackable dropped on its twin is still ONE row with the summed count, because
  the server sends OBJ_COUNT. For example, coins 10 + 5 make one row of 15. A pile of
  non-stackables (a loot pile, two identical potions) is one row per item, so walk them
  with repeated `click_obj`.
- Known gap: a second pile whose count variant is still loading waits for the next
  world-load sweep (`app_placeholder.c` looks up the oldest row).
- Known gap: OBJ_COUNT carries no old count on the client path, so two private piles of
  one stackable can have the wrong pile retargeted.

## A drained stat stays drained through xp

`ToriRSServer_CombatAddXp` now follows LostCity's `Player.addXp`
(engine/entity/Player.ts:1821-1851):

- An unboosted stat follows the base.
- A drained stat stays drained, and a level-up lifts it by the number of levels gained.
- A boost is left alone.
- Hitpoints and summoning are exempt.

Content drains (Verzik, Olm, Sourhog) now survive the player's own hits. As a side
effect, `::maxstats` leaves a drained stat drained by the same amount, as LostCity's
`::maxme` does.

## t.raid.enter(raid, room, opts): landing in a room unstarted

- `t.raid.enter` uses the raid's own landing debugproc: ToB `::tobmode <1-6> <0-2>`, ToA
  `::toa <1-12>`, CoX `::coxseed <seed>` then `::coxgoto <room> <floor>`. It never starts
  a room.
- The `ok` detail reads `in tob maiden (normal) at x,z,l; boss present (tob_maiden_100
  slot S at x,z); handle H, started 0, fight x,z,l`.
- Rooms:
  - ToB: maiden, bloat, nylocas, sotetseg, xarpus, verzik. `opts.mode` is entry, normal
    or hard.
  - ToA: nexus, crondis, zebak, scabaras, kephri, het, akkha, apmeken, baba, wardens,
    wardens_p2, vault.
  - CoX: the grid rooms and olm. `opts.seed` defaults to 1, and `opts.floor` is also
    accepted.
- `enter` refuses a landing whose read-back disagrees: the wrong room or mode, or a room
  already started.
- `::tobmode` runs `~tob_debug_kit`, which heals and adds 4 `br_4dosepotionofsaradomin`
  and 4 `br_4dose2restore` to the backpack when it holds none (tob.rs2:296-304). Plan the backpack around that. In the
  conformance harness those potions moved every later slot and tick, so the raid rows
  run last there.
- `::tobmode 2` (Hard) skips the door's `varp6826_tob_completions >= 1` requirement. That
  is deliberate for a room resume. The whole-raid `tob_entry` test must use the real
  door.

## The fight begins with the player's own click

Never use `::tobgo` or `::toago`.

- ToB: `t.player.click_loc('tob_arena_barrier', 1)`, then
  `t.chat.play({'options', 'choose:Yes, begin the fight.'})`, then
  `t.msg.expect('The fight begins')`. The line lands during `chat.play`, so `msg.await`
  misses it. In the proof run, Maiden's first attack came 7 ticks later.
- ToA: `click_loc('toa_path_barrier', 1)`. This works for map-placed barriers such as
  Het's. Spawned barriers are a known content defect (see below).
- CoX: encounters wake on approach. Olm's chamber is entered by
  `click_loc('raids_bossentrance', 1)` and "Step through the mystical barrier.".

## t.raid.state() / t.raid.leave() / t.raid.start_tile()

- `state()` returns `('ok', {raid, room, room_id, mode, started, handle, boss_symbol,
  boss_slot, fight, line})`, or `not_found` when no raid is active. Room, mode and the
  started flag are map-instance registers, so `state()` parses the one-line `::tobstate`,
  `::toastate` or `::coxstate` reply (read-only debugprocs). ToA mode is `level N` and
  CoX mode is `seed N`.
- `leave()` and `state()` take no argument, so call them directly and record them with
  `t.check`. `leave()` waits for the active varp to read 0 and for the walk-out to
  settle. ToB leaves at Ver Sinhaza (3677,3219), ToA at 3358,9113 and CoX at 1234,3572.
- `start_tile()` returns `('ok', {x,z,level}, 'x,z,l')`, the room's first tile inside its
  barrier. It answers `unsupported` for CoX.

## Bosses on landing: ToB present, ToA unspawned, Wardens started

- A ToB boss is placed at build time and is present on landing. Nylocas has none until
  its waves.
- A ToA boss is placed only when its room STARTS, and Zebak stands 37 tiles from the
  corridor, outside the client's npc view. "Boss unspawned" is the expected landing
  answer for ToA.
- Wardens P1 lands STARTED. Its entry tile is inside `^toa_room_arena_radius` of its
  fight tile, so the proximity watchdog fires on arrival.
  `t.raid.enter('toa', 'wardens')` answers `refused` until that is fixed.
- ToA barriers that `~toa_add_barrier` spawns do not cross into the arena: the direction
  can be diagonal (Zebak's gate sits NW of the entry row). This blocks toa_zebak,
  toa_wardens, kephri, akkha and baba until it is fixed from a source.
- CoX `::coxgoto` lands at the room centre, and Tekton wakes and attacks on landing (99
  -> 65 measured), so arm the character before entering. The same seed gives the same
  layout every run: seed 1 puts Tekton in cell 1,1 on floor index 1.
