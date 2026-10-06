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

Seam3: a cast did not recompute the combat varps, so after `::setlevel magic` every
spell rolled attack 0 and splashed; that is fixed ("A cast lands at the level ::setlevel
set", below) and is a likely part of this. A splash still calls `~npc_retaliate`, so it
is not the whole story: the man was not re-measured, and the row stays open.

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

# Seam pass 2: the Theatre of Blood content as the server now plays it

Seam pass 2 (`matthew-mbp-m4-raid-b1-seam2`) added no verbs. It fixed the ToB content
rows the spec pass found (CONTENT_BUGS.md), so the facts below are what a room test
asserts after it. Each was measured with a scratch script that entered the room with
`t.raid.enter` and read `ticklog.tsv`; the run directories are named in
`SEAM_LEDGER.md` and the fixers' reports.

## Measuring a room while other workers edit OSRS-Content

- `run.py` checks and rebuilds the SHARED pack first, so another worker's half-written
  file breaks your run. To isolate a before/after, build a private content root:
  `git archive HEAD -- osrs239-content/server/scripts` (leave out `selftest` and
  `build*`), symlink the other content directories, compile it with
  `src/build_opt/sscompile --src <copy>/server/scripts --out <copy>/server/scripts/build
  --content-root <copy>`, and run with `TORIRSSERVER_CONTENT=<copy>` and
  `TORIRSSERVER_SCRIPTS=<copy>/server/scripts/build`. The embedded server also reads the
  `.npc` configs from that root.
- Keep that pack inside its own worktree (`<copy>/osrs239-content/server/scripts/build`).
  The server's stale check walks the pack's parent directory, so a pack in a shared
  scratch directory picks up other workers' files.
- The C `--selftest` loads the repo's shared pack unless `TORIRSSERVER_SCRIPTS` is set.

## Reading a debugproc's reply

- `t.cheat` returns no reply text for a debugproc. Read it with `t.msg.last(n)`, which
  returns the ring NEWEST FIRST and includes older lines. Filter on a serial greater
  than the newest one you read before the cheat; otherwise you can read the previous
  call's line when the new one has not arrived yet.
- `t.cheat('::tobwhy')` runs synchronously in the embedded server's pump, so
  `map_clock` does not advance between two cheats: put `t.ticks(1)` between readings.
  The reply carries `map_clock=N` (the tick log's clock) and ` npc <name> hp=X` lines.
  Take the block that ends `npcs in instance=K` and the K npc lines before it, or two
  calls' lines merge.
- `t.cheat('::kill <symbol> <radius>')` logs `debugproc not found` in client.log and
  then the engine ladder handles it. It works; that line is not a failure.
- `t.cheat('::tobbloatlos <local x> <local z>')` (read-only, tob_selftest.rs2
  `[debugproc,tobbloatlos]`) asks Bloat's OWN sight test (`~tob_bloat_sees`, the near-side
  rule, not `lineofsight`) about an instance-local tile and prints `tobbloatlos 1` (he sees
  it), `tobbloatlos 0` (hidden) or `tobbloatlos -1` (no Bloat in this instance). It changes
  nothing; use it to confirm a hiding tile before a room test stands on it (the Normal
  Bloat author did; seam22 recorded it here).

## Tick-log details that bite

- `npc_tile` rows are written only when the tile changes, while blert has a position
  every tick. Fill positions forward per tick before you detect direction flips.
- For an hp gate, take the npc's hitpoints AS SEEN on tick t: start hp minus the
  `hit_npc` rows on EARLIER ticks. The npc phase runs before the player phase, so a hit
  logged on tick t lands after the boss's own turn that tick.
- Under `::god`/`::godmode`, `hit_player` is written absorbed (hitsplat 26, damage 0);
  real damage is hitsplat 28. A max-hit row needs god mode off.
- The tick log has no room-start row. A `t.ticklog.mark` written right after
  `t.chat.play({'options','choose:Yes, begin the fight.'})` lands one tick after the
  Maiden's room start (mark 10, `::tobwhy` clock 18 = start 9 + 9). Read the anchor from
  the server, or state the offset in the ledger row.
- A content `npc_queue` armed from inside the npc's own `[ai_timer]` drains in the SAME
  npc turn, so `npc_queue(q, arg, N)` from a timer fires on T+N-1, and one armed
  elsewhere fires on T+N. Verzik's P1 bolt, yellows and webs ask for N+1 for this
  reason.
- Per-room rolls replay: a boss's rolls are seeded from its spawn key and a lives
  counter, so two runs with the same history replay identical rooms. Offset runs by
  entering and leaving rooms, not with a new `--name`. About 16,000 server ticks is the
  480000-frame ceiling.

## tools/verify_tob_timings.py --ticklog: our run against the blert checks

- `python3 tools/verify_tob_timings.py --ticklog <run>/ticklog.tsv` measures a run on our
  server against the same tob.constant figures and the same checks the blert cache
  gets. It refuses a header other than `ticklog-v1`.
- Rooms are found by cache ids. Maiden: attack seqs 8091/8092 and pool loc 32984 (splat
  when no blood spawn stood on the tile, trail when one did). Bloat: down seq 8082, up =
  the slot's next `npc_tile`, stomp = the Bloat's `hit_player` inside the down (flies in
  flight and hand ticks excluded), hands = `map_spotanim` 1570-1573 (shadow) and 1576
  (drop). Sotetseg 8138/8139. Xarpus spit 8059 and exhumed loc 32743. Verzik phase = npc
  type (8370/8372/8374, story 10831/10833/10835, hard 10848/10850/10852), attack seqs
  only. Nylocas waves are not measured (a spawn row has no wave number).
- Room tick 0 is not in the log: give `--start-tick N` (repeatable) or
  `--start-mark REGEX` plus `--anchor-offset`. Without an anchor, Maiden's first attack,
  Bloat's first walk and Xarpus' first exhumed print as notes, not checks. The Xarpus
  spawn gap needs `--scale` (and `--mode`).
- Bloat walks use blert's definition, walkTime = down - up - 1, where up is the first
  step after the down. The first walk is down - room tick 0 - 1.
- blert sees a loc's despawn one tick early (M5), so the tool adds 1 to a recorded pool
  or trail run: trail 29 -> 30, splat 13 -> 14 (against our 11, a named mismatch).
- blert event 141 (xarpusExhumed) is stamped on the tick the exhumed is gone; lifetime
  = tick - spawnTick, 11 x98 regular.

## Entry Mode hitpoints stack, and the party affordances

- Every Entry npc's hitpoints are `~tob_entry_scale(unit, players)` = unit x 1 / 1.9 /
  2.7 / 3.4 / 4.0 (Jagex, Entry Mode Improvements, 1 Mar 2023): Maiden 500 / 950 / 1350
  / 1700 / 2000, a Matomenos 16 / 30 / 43 / 54 / 64. The wiki's Entry infobox figure is
  the FIVE-player one.
- `::tobscale <n>` re-states the current room's boss for a party of n and prints
  `tobscale n hp=H of M`. It is the only way a one-player driver reads a 2-5 player
  pool. It does not move Vasilias (not the room's registered boss). Never use it in a
  "fought for real" room test.
- `::tobboss` prints `tobboss record=entry|normal|hard room_mode=M hp=H of M base=B
  def=D of D0 name=...` for the room's boss: assert the mode's cache record and its
  Defence with it.

## Which npc id fights in each mode

- Bloat, Sotetseg and Xarpus spawn their mode's cache record in Entry and Hard
  (`tob_bloat_story`/`_hard`, `tob_sotetseg_combat_story`/`_hard` and their maze
  forms 10865/10864 and 10868/10867, `tob_xarpus_*_story`/`_hard`). Filter tick-log
  `npc_retype`/`npc_anim` rows on all three families.
- Maiden, her crabs and her blood spawns are their mode's records from seam5 (see
  "Entry and Hard Maiden are their mode's records" in the seam5 section).
- Verzik stays the Normal ids in every mode; seam5 sets her Entry levels on those ids.
- `t.raid.enter('tob', 'sotetseg', {mode='entry'|'hard'})` lists the mode records since
  seam2; before, its detail read `boss unspawned` there while answering ok.

## The room watchdog runs every tick

- The per-player room watch is a 1-tick queue since seam2. Standing in Maiden's blood is
  hit every tick (a pool landing on T hits T..T+10, plus the extras a tick later; a
  99-hp player standing still dies in about 7 ticks). Sotetseg's realm chip lands every
  7 ticks and the maze rag every tick. A room reads cleared about 2 ticks after its boss
  is gone.
- The boss is looked up from the room's fight tile, not from the player, so a solo
  Sotetseg runner in the shadow realm no longer clears the room.

## Maiden after seam2

- Her 70/50/30 transmog keeps her hitpoints and pool: assert "hp after the retype <= hp
  before" on the transmog tick (`::tobwhy`). Measured 1444 -> 1444 solo Normal.
- Hard period: the gap after an attack is 10 - ceil(c/2), floor 5, where c counts LEAKS
  (arrivals) up to that attack; kills do not count. In the tick log a crab
  `npc_death` is a leak only when it was not killed. Measured c1 9, c2 9, c3 8, c4 8,
  c6 7, c7 6, c8 6, c>=10 5; c=5 and c>=9 are formula only (E, M20).
- Hard crab sets are fixed: all ten at 2-5 players; solo seven at local (45,40) N3,
  (49,38) N4i, (49,40) N4o, (41,20) S2, (45,20) S3, (49,22) S4i, (49,20) S4o. Normal and
  Entry draw a uniform subset of 2 x party. A crab's hitpoints are its POOL (75 of 75).
- Blood spawns step one tile every tick while free (0.968 of slug-ticks moved, 0
  two-tile steps). Take out 32-tick freezes, the last ~5 ticks while she dies, and the
  east entrance pocket (local 50-51, 27-34) before computing a move rate.
- Blood-spawn chance per expiring pool: 20 if stood in, 10 if another pool of the throw
  was, 5 when the whole team dodged. No Hard ramp (focus) damage in a solo raid.
- Death, K = her `npc_death`: `npc_anim` 8093 at K+1, retype to 8364 at K+3, 8365 at
  K+5, `npc_free` at K+9 (measured seam2 closer; blert K+9). Assert on `npc_free`, not
  on the 8364 retype tick (the retype lands at the engine's corpse stage, 2 late).
- Measurement debugprocs (never in a room test): `::tobmaidenpct <pct>` takes her
  current body to pct of the scaled pool with `npc_damage`; `::tobmaidenleave <n>`
  kills every walking Matomenos but n; `::tobmaidenunit` prints the room's pure
  functions.

## Bloat after seam2

- The first step, first fly and first falling-flesh volley come ON down+33. Bloat does
  not move on the down tick. A visible reversal is never closer than 5 ticks to the
  next down.
- Falling flesh never lands on local x29..34, z29..34 (the tank). Regular mode's first
  volley after a rise falls on the rise tick; Hard keeps a strict 6/4 cadence through
  downs.
- The run, hands and hurry gates are strictly below 60 / 90 / 40 % of the scaled
  maximum. Entry flies hit 4-8 and the Entry stomp 20-40 (minimum an approximation,
  M62).
- Turn rate as a per-tick hazard: 1 in 17 Regular, 1 in 7 Hard (blert's
  analyze_bloat.py "turn rate" is odds over non-turn ticks and reads 1 in 15 / 1 in 6).
  The down chance is 25 % Regular and 28 % Hard per eligible tick, fitted against
  blert's first walks [M17b].

## Nylocas after seam2

- Lifetime tick 1 is the spawn tick. A small's explosion animation is on lifetime tick
  52 and it despawns 52 ticks after spawning; a big's on 53 and 55. An exploding big
  leaves two smalls on its despawn tick at its SW tile (0,0) and (+1,+1). A big killed
  by a player despawns hp0+6 standing / +7 walking with the same two splits. A nylocas
  killed during its explosion animation dies through the death path (no extra split).
- Nylocas do not step on their spawn tick. A flicker's first colour change is +5 after
  spawn (then a 2-tick hold). An aggro turns incoming -> fighting on its first turn with
  its SW tile inside the arena box (local x 26..37, z 19..30) and does not step that
  turn: +10 west/east, +11 south, +9 east-lane big. Read them as `npc_retype` rows.
- Vasilias lands with her SW tile on 3294,4247 (local 30,23), holds the spawning form 2
  ticks, then melee for 9 and every later form for 10. She attacks exactly twice per
  form: +2 or +3 after the change, then +4 (opening form +1 or +4). Grade a window as two
  attack `npc_anim` rows inside one `npc_retype` interval.
- The Hard Prinkipas does not self-destruct: a room test must kill all three or
  Vasilias never lands.
- A passive solo room now loses all four supports (about room tick 540 with `::god` and
  no kills), because exploding bigs split. A measurement scratch can keep the room alive
  with `::kill tob_nylocas_<incoming|fighting|big_incoming|big_fighting>_<style>[_hard|_story] 40`;
  never in a room test.
- Known gap: a small killed by a player despawns hp0+3 standing / +4..+5 walking against
  blert's +2; assert it as "measured N, open" (CONTENT_BUGS).

## Sotetseg after seam2

- Attacks every 5. Ten ordinary 1606 balls, then a 1604 death ball INSTEAD of the 11th
  (one `npc_anim` 8139, no 1606 that tick), next attack +10. The death ball's
  `hit_player` row is at D+16 at any distance (projectile end_cycle 450). Melee (8138)
  `hit_player` at +1.
- At a maze proc (`npc_retype` 8388 -> 8387, or the mode's pair) every raider is stunned
  5 ticks and teleported at proc+3 (`player_tile` level 3); the first step resolves at
  proc+5 (`step_tick` before that answers timeout). The maze ends only on world ticks
  that are multiples of 4; his first attack is re-activation+1 and never a death ball.
- `::tobgo` teleports to the fight tile (6415,84), so warp AFTER it (adjacent =
  `::tobwarp 15 39`, 3 tiles = `::tobwarp 15 37`).
- `::tobsotedrain <n>` drains his Defence through `npc_statsub`; `::tobsotestate` a tick
  later shows the floor (def=100 of 200).

## Xarpus after seam2

- Signatures: exhumed = `loc_set` loc 32743 (despawn `loc_set` -1 on the same coord),
  heal orb = projectile spotanim 1550 (one per uncovered exhumed per tick from rise+3),
  acid pool = `loc_set` 32744, wake = the first `npc_retype` (static -> feeding),
  stand-up = the second (8339->8340 Normal, 10771->10772 Hard, 10767->10768 Entry).
  Fight tick 0 = the wake retype.
- Phase 1, solo: opens at 75 % of his pool (Normal 2812/3750, Hard 3375/4500, Entry
  390/520). Normal/Entry 7 exhumed rising at fight ticks 9, 21, ..., 81, lifetime 11,
  stand-up 9 after the last despawn; Hard 9 exhumed at 9, 21, ..., 105, lifetime 9,
  stand-up 7 after (fight tick 121). Heal per orb Normal 20, Hard 21, Entry 6. Hard lays
  its 98-pool ring on fight tick 3 and it hurts in phase 1.
- Hard phase 3 has no timed turn: he faces the quadrant of the last LANDED hit with
  damage > 0. The gaze retaliation is a `hit_player` row with NO `npc_anim` on its
  tick. Use `::tobxarpusarm` right after the stand-up to reach phase 3 early.
- Melee tiles around a standing Xarpus (instance handle 1): his 5x5 is 6432..6436 x
  97..101, centre 6434,99; south-middle 6434,96 is the SW quadrant, east-middle 6437,99
  the SE quadrant. In Hard the ring's reach leaves 6433-6435, 91-92 (the entrance gap)
  as the only safe barrier tiles; the fight tile 6434,92 is in it.

## Verzik after seam2

- Her pool survives every change of form (`~tob_verzik_retype`): a solo Normal room is
  6750 = 1500 + 2 x 2625, and the P1 shield is 1500 (`::tobvzskip` spent=1500; it was
  3750 before the seam2 closer). Entry solo is 1100 = 300 + 2 x 400, Defence 10 / 120 /
  120 by phase (`::tobboss`). P3 still uses the P2 figure in Entry (open).
- P1: wind-up 8109 on T, bolt 1580 on T+3.
- P2: count attacks as `npc_anim` 8114/8116 rows while she is 8372. An Athanatos cast is
  projectile 1586 on that tick and the Athanatos (npc 8384) appears 6 ticks later; the
  first cast can come on attack 0 (a 25 % roll), then 20+ attacks between casts. Reds
  are npc 8385: two, one when one raider is left, sized as their pool at the party's
  scale (solo Normal 150, Entry 20; they were the record's 200 before seam2).
- P2 -> P3: 8118 plays on the P2 form at the phase event E, the P3 id 8374 arrives at
  E+6 with 8119, the first P3 attack is at E+12. The cache's 8373 form is not worn yet.
- P3: yellows blast on pool tick +14 (Hard +20) with the next auto 7 later; webs special
  42 ticks to the next auto; green ball flight 221 cycles; auto max 33, 34 once enraged.
- `::tobvzpct <pct>` spends her current phase to pct % of its own pool (a debugproc
  that does damage: scratches only). `::tobvzskip` spends a whole phase.

# Seam pass 3: what the first ToB room pass exposed

Seam pass 3 (`matthew-mbp-m4-raid-b1-seam3`, triage `SEAM_TRIAGE_2026-10-03.md`) added
one verb (`t.npc.await_face`), three fields on `t.npc.state` rows, one tick-log kind
(`npc_face`), the engagement-stamp rule for an attack on another copy, three tree-wide
combat fixes, the Entry-mode damage figures of every room, the supply chests, and the
six scope sidecars `raid_coverage.py` reads. The facts below are what a room test sees
after it; the run directories are named in `SEAM_LEDGER.md` and the fixers' reports
under `build/seam_state/matthew-mbp-m4-raid-b1-seam3/`.

## Small API facts the first room authors tripped on

- `t.msg.last(n)` returns `(status, list)`, not the list.
- `t.ticklog.rows(opts)` returns `(ok, list)`.
- Instance coordinates are the template region moved by whole 64-tile blocks. In the
  first ToB instance slot (region 13122) an instance tile is template x - 3136,
  z + 4160; the Nylocas fight tile 6431,94 is template 3295,4254.
- An npc's footprint is its south-west tile plus the cache record's `size`. Verzik P2
  (8372) is size 3 with her south-west tile at 6431,89, so she covers 6431..6433 x
  89..91.

## t.npc.state rows: face_x, face_z, face_tick (the square an npc was turned to)

- The newest FACE_COORD op the server sent that npc (`npc_facesquare`): the absolute
  tile, and for a sized square its centre tile (the wire's half-tiles are halved). All
  three read -1 before the first op.
- They outlive the turn. The entity's own pending square is cleared the cycle the turn
  is consumed, and a later face-entity lock does not clear them; `facing` says whether a
  lock is held now.
- `face_tick` is on `api_drive.tick()`'s clock, like `seq_tick`, not on `t.tick()`'s.
- `t.npc.state_text(row)` adds `last face square X,Z on tick T` (or `none`). On a binary
  older than seam3 it says `last face square unread`.
- Ordinary-npc subject: Hans in Lumbridge. Every `~chatnpc` page runs
  `npc_facesquare(coord)` (interface_chat/scripts/chat.rs2:192), so talking to him turns
  him to the player's tile. Conformance row `seam.npc_facing_read`.

## t.npc.await_face(selector, since_row, ticks[, opts]): "the boss turned on tick T"

- Returns `('ok', detail, tick, x, z)`, `timeout`, `not_found`, `no_row` or
  `unsupported` (a binary without the face fields).
- Two ways to meet it, and the detail names which. EDGE: the next `npc_face` drive event
  on that copy; a turn to the square it already faces still counts. SINCE: pass an
  `npc.state` row read BEFORE the action; a turn newer than that reading (later
  `face_tick` or a different square) also counts.
- Use the SINCE path when the cause is a verb that has already returned
  (`chat.continue_`, an attack): the op can go past while that verb waits for its own
  edge. The copy is resolved once, before the wait, as in `await_anim`. Default 10 ticks.
- Conformance row `npc.await_face` (Hans's second page turns him again).

## The tick log's npc_face row

- Kind `npc_face` {slot, type, x, z}: written in `SS_OP_NPC_FACESQUARE`, the only writer
  of an npc's FACE_COORD mask (no C movement or combat path turns an npc to a square).
  x and z are tiles, not a packed coord.
- Filter by kind and by the WORLD slot (`t.ticklog.slot(row)`), as with every log row.
  The log's tick and the client's `face_tick` are different clocks; never compare them
  (measured log 16 vs client 15, log 173 vs client 172).

## Xarpus's gaze, read (Entry, measured with ::god in face_xarpus2)

- Every P2 spit is an `npc_face` row on the target's tile, on the spit's own tick, every
  4 ticks (120, 124, 128, 132 -> 6433,92 while the player stood there).
- After the screech, P3 turns are `npc_face` rows every `^tob_xarpus_stare_ticks` = 8
  ticks (141..173) to quadrant tiles at centre +-5 (6429,94 SW, 6439,104 NE, 6429,104
  NW), never the same square twice in a row. Read the quadrant from the row's x/z against
  his centre 6434,99.
- `raid.state`'s `boss_slot` names the static form. The fighting form has its own client
  slot: select it by symbol (`tob_xarpus_combat_story` / `_hard` / `tob_xarpus_combat`).

## A press names its copy (the stale-menu half: see seam4 below)

- `t.player.attack`, `talk_to` and `press` with `{slot=}` or `{at=}` press only a menu
  row carrying that copy's element id. When the menu has no row for it the verb answers
  `covered`, and the detail names the copies the menu did offer for the same op: "menu
  has no row for it; the menu offered this op on 1 other copy(ies) -- element 1073760392
  'Attack Nylocas Hagios (level-46)' -- none pressed". That is the asked copy hidden or
  gone, not a wrong press: re-pick a target and attack again.
- FIXED IN PART by seam4 ("A press no longer takes another copy's row from a stale
  menu", below). The seam3 text follows. A menu a `covered` press left open was NOT closed before the next press. In this
  client a press of either mouse button on an open menu's row SELECTS that row
  (uitree_interact.c `interact_minimenu`), so a retry whose pixel falls inside the old
  menu can attack another copy while every press still answers `covered`
  (s3ec_stale_before1: 5 hits on the copy never asked for). The seam3 fixer closed the
  menu first; the closer reverted that half because the tick it cost moved three green
  quests to RED (CONTENT_BUGS.md, seam3). Until it lands, grade which copy was hit from
  the tick log (`hit_npc` rows on the asked world slot), not from the verb's answer.
- A `t.player.attack` that names a different slot than the current engagement and does
  not land (covered, no_row, refused, press timeout) drops the old stamp. The next
  `t.npc.await_dead_engaged` answers `no_row` "nothing is engaged" instead of re-pressing
  the previous copy, and the failing attack's detail says "the engagement stamp on slot
  N ... was dropped". A failed re-attack of the SAME slot keeps the stamp.
- Two copies on one tile: `::spawn <npc>` twice from one player tile lands both on
  player.x+1, player.z+1. `t.npc.tiles(sym, r)` lists both with slot and element.
  Conformance row `seam.attack_exact_copy_on_one_tile` (goblins).
- With auto-retaliate on, the SERVER swings back at whichever npc hits the player, which
  is not a press. A test that grades which copy was hit turns it off:
  `::setvar varp172_option_nodef 1`.
- In the Nylocas room filter `hit_npc` rows by nylocas type: the four supports take a
  `hit_npc` row every chew (about 400 rows in 160 ticks).

## Bosses never retaliate through content any more (retaliate=no honoured)

- `minigame_tob/scripts/tob_retaliate.rs2` binds a no-op `[ai_queue1,<record>]` for each
  of the 93 `retaliate=no` ToB records, replacing the default `[ai_queue1,_]`
  (`npc_setmode(opplayer2)`). A boss now attacks only on its own `[ai_timer]` clock, and
  Maiden's and Verzik's `playerface` latch survives a hit.
- Before: a ranged or magic hit left Sotetseg in opplayer2, and stepping adjacent drew an
  extra 8138 off his 5-tick clock that landed +0. After: 21 attack anims all on one
  residue mod 5, every 8138 lands +1 (sc3_sote_retal_after).
- Melee never calls `~npc_retaliate` (only ranged, magic and specials do); an ordinary
  npc's melee retaliation is the engine's latch (torirs_server_combat.c:1427). A
  retaliation proof must hit with ranged or magic.
- CoX, ToA and Zulrah `retaliate=no` npcs still have the defect until the engine row
  lands (CONTENT_BUGS.md); a new `retaliate=no` record in tob.npc needs a line in
  tob_retaliate.rs2 until then.

## A cast lands at the level ::setlevel set, and is a magic hit

- `[proc,pvm_spell_cast]` recomputes the combat varps first (`~player_combat_stat`, as
  LostCity's `[changestat,_]` does). Before, a cast with nothing equipped after
  `::setlevel magic 99` rolled attack 0 and splashed 12 of 12 Fire Bolts at a Hagios.
- A cast is a MAGIC hit in the damage funnel (`%varp6295_damagetype = ^magic_style`
  around `~player_hit_npc_prepare`, restored after). A spell damages a magic nylocas:
  10 damaging hits and 6 kills from 12 Fire Bolts (sc3_nylo_cast_after2).
- Powered staves (trident, sanguinesti, shadow) and the magic specials still arrive as
  the weapon's melee style and are nulled by a Hagios: use a spellbook spell for the
  magic nylocas (open row).
- Other combat varps (`%varp6285_com_magicattack` and friends) are still not recomputed
  on a stat change outside a cast or a swing.

## A hit in flight at a player who dies does not land after the respawn

- The death script clears the raids' personal delayed-hit queues
  (`~raid_death_clear_hits`, death.rs2) in the same turn as the respawn teleport: ToB
  Maiden's blackstorm, Sotetseg's melee/ball/impact, Xarpus's delayed poison and stomp,
  Verzik's P1 bolt, acid and ball; the CoX Olm and Vasa and ToA hit queues likewise.
- Room-wide queues (urnbomb pools, webs, the Athanatos landing, Xarpus splats) are not
  cleared; they check the victim's tile at impact.
- "Oh dear, you are dead!" prints twice per death, so `player.died`'s detail reads
  "2 time(s)" (open row).
- Measurement trap: a heal cheat (`::setlevel hitpoints N`) during the 7-tick death
  sequence revives the corpse; never heal inside a death measurement.

## Maiden after seam3

- Her blackstorm's `hit_player` rows name her WORLD slot (`t.ticklog.slot(boss row)`;
  1079 in a solo Entry room) with `npc_type` her current form (8360..8363) and
  `dealer_pid` -1. Pool and blood-spawn trail hits stay `npc_slot` -1, so split autos
  from pools with `npc_slot == boss_slot` against `npc_slot == -1`.
- A raider at 0 hitpoints is not a blackstorm target, and a shot whose target is at 0
  hitpoints or outside the launch instance at impact does not land. Solo Entry: death at
  93, respawn in Lumbridge at 100 (7 ticks), blackstorm flight 5 from the fight tile.
- Entry pools deal the Normal 10 + 2c (measured 10/14/18 at c 0/2/4): grade E [M121], no
  source gives an Entry figure (`^tob_maiden_pool_entry_divisor` = 1,
  tob_maiden.constant). The Entry blackstorm is the halved auto: 18/20/21/25 at c
  0/1/2/4.
- Timing a death against an attack: poll `t.ticklog.rows({kind='npc_anim', seq=8092,
  since=serial})` one `t.ticks(1)` at a time, then act on T+k.

## Bloat after seam3

- Every later walk is 34..42 in blert's numbers (walkTime = next down - first step - 1;
  the first step is the rise tick, down + 33), plus the lockout extensions. The earliest
  down is rise + 35; down-to-down is never below 68. The first walk is unchanged
  (counted from room tick 0).
- Entry falling flesh hits 20-25 (grade E, M62, one narrator); Normal and Hard 30-50.
- Measuring a hand hit: on a fresh shadow volley (`map_spotanim` 1570-1573 on tick S),
  `::tobwarp` onto a shadow tile and repeat the warp every tick until S+3 (a running
  attack engagement walks the player off between ticks); keep `::god` off through S+5.
  The splat judges the tile held at the end of S+2 (T-1).

## The supply chests (after Bloat and after Sotetseg)

- `tob_midway_chest_closed` appears when the room reads cleared (`~tob_room_cleared`).
  After Bloat it stands at room-local (5,33) (the wiki infobox's map pin), e.g. 6405,97;
  `t.world.loc_near` reports it at level 1 because the corridor is a bridge deck, and it
  is played at plane 0. After Sotetseg it stands at room-local (17,5), the east flank of
  the exit (the flank is not sourced).
- To open it: cross the exit barrier first (Bloat `tob_arena_barrier` at local 23,31;
  Sotetseg local 15,19), walk to Bloat (9,32) or Sotetseg (17,8), then
  `t.player.click_loc('tob_midway_chest_closed', 1)`. From the arena `click_loc`
  answers `not_visible`. Never stand within 1 tile of the exit passage (Bloat 5,31;
  Sotetseg 15,5): `~tob_exit_walked` moves the party into the next room.
- Messages. Entry: "You take N bandages from the chest." (N = 10, or the free slots if
  fewer), then "The chest is empty.". Normal and Hard: "You have N points to spend."
  (10-13 with no deaths in the two rooms, 8-11 with one, 6-9 with two or more), then a
  5-row options menu; a second Open goes straight to the menu and awards nothing; a
  second stamina from one chest answers "You can only take one stamina potion from each
  chest.". Died in both rooms: "The chest contains a single onion.", then "The chest is
  empty.".
- The Entry bandages have no Heal script yet (open row): carrying them does nothing.

## Nylocas after seam3

- Entry explosions roll 1-8 for small and big (wiki Entry page "about 8"); Normal and
  Hard keep 18/21. Measured 14 explosion hits 1-8 in a solo Entry room.
- An explosion's `hit_player` row lands on the detonation tick T with `npc_slot` = the
  exploding nylocas (deleted a tick or three later). Grade explosions as: the dealer slot
  has a detonate `npc_anim` (7992/8000/8006) on the same tick (`map_spotanim` 1565-1567
  marks the tile). A `hit_player` with `npc_slot` -1 is a projectile whose thrower
  despawned in flight, not an explosion.
- Magic (Hagios) aggros stand about 6 tiles off and cast, so their explosions never
  reach a player standing still; explosions are met next to a pillar or beside a
  melee/ranged aggro.
- A small killed by a player despawns hp0 + 2 standing (blert +2). It is +3 if it stepped
  the tick before the kill and +4 if it stepped on the kill tick: the engine's arrive
  delay, an open engine row. Assert standing smalls exactly 2 and walking ones as
  "measured N, open (engine arrive delay)".
- Wave count from the tick log: a wave is a lane-tile `npc_spawn` on a room cycle tick
  ((tick - support spawn tick) % 4 == 0); a big killed on its lane tile splits there on
  other ticks, which are not waves. Counted that way the first flicker is wave 16 of 31
  (about room tick 136, so a fight capped at 115 ticks never sees it).
- Vasilias in Entry: lands on the first cycle tick at or after the last nylocas despawn
  + 16 (measured +17), south-west 3294,4247, spawning form 2 ticks, melee 9, then each
  form 10, exactly 2 attacks per form (first +1 in the opening melee, then +2
  melee/ranged, +3 magic). The Entry spec rows `vasilias_switch_entry` 15 /
  `vasilias_attacks_entry` 3-4 are not what the server plays (open).
- Measuring a whole room solo without dying (never a room test): every 3 ticks
  `::kill tob_nylocas_<incoming|big_incoming|fighting|big_fighting>_<melee|ranged>_story
  40` (twice each), magic left alive; `::goto 6428 85 0` (the SW support's north-east
  corner); `::setlevel hitpoints 99` every tick. Vasilias lands about room tick 364.
- A loop around `t.player.attack` without its own hitpoint check can die here: one
  attack call spent 30+ ticks in covered/re-press handling while magic aggros hit for
  100 in 50 ticks, and only verbs check alive.

## Sotetseg after seam3

- First attack: room start + 6 in every mode (blert B 6; Entry D 7 +-1). Assert 6 from
  the barrier mark - 1.
- Entry max hits: 20 melee and 22 ball/ricochet (wiki infobox); Normal and Hard keep 45
  and 50. Entry Protect from Melee caps at 10, a derived figure.
- He keeps his hitpoints and pool across both maze retypes: `::tobboss` reads the same
  hp (of 560 Entry, of 3000 Normal) before the arm, after the proc and after the
  re-activation.
- The shadow-realm path is lit on proc+4, the runner's first tick in the realm:
  `loc_set` rows with loc 33035 (`tob_sotetseg_lighttile`) at level 3, one per path
  tile (25-30 per maze). `t.world.hazard_at` reads 33035 on the start tile and 33034
  (darktile) off the path. In a solo raid the arena mirror lights nothing (nobody stands
  there); a party test should assert the mirror's rows.
- Off on 3: a step off the grid resolving on a tick = 3 mod 4 re-activates him on the
  next tick. The despawn check scans players' tiles in the NPC phase.
- The maze waits for its runner. Before the eat-delay port (seam6, landed in OSRS-Content
  7936c59bf9) an eat on proc+2 held the teleport (`p_delay`) to proc+5. An eat no longer parks
  the player, so it no longer holds the teleport (not re-measured: assert
  `maze_teleport_delay` 3 and report a late landing as a finding).
- Rag: a wrong tile resolved on tick N is hit on N+1 .. the tick the step back resolves;
  Entry 11 + 6.67 % of current hp.
- `::tobsotepct <permille>` (scratches only) takes him to that share of the scaled
  pool; `::tobsotepct 330` opens the second maze.
- Loc ops in an instance nobody stands in: `loc_find` (and so `loc_change`) finds a
  map-placed loc only through a scene window around a player.

## Xarpus after seam3

- Every poison hit (splash, standing in a pool, the delayed hit for crossing one) is the
  4-8 base x (100 + absorbed %)/100, capped at 11 in Normal and Hard. Entry halves it,
  rounding up, capped at 6. With all 7 exhumed through: Normal {8,10,11}, Entry {4,5,6}.
- Stomp: two splats on the tick after you stand in his 5x5, each 2-8, the pair never
  past 9 (the second is cut). Entry halves each, so its tick is at most 5. The splats
  are slotless `hit_player` rows (`npc_slot` -1): pick them out by tile (player inside
  6432..6436 x 97..101 on the previous tick), not by dealer.
- In an Entry test `spec.xarpus.p2.max_hit.entry` is the largest npc-dealt poison hit
  (<= 6). Do not derive `poison_base` from Entry hits: it is a Normal row.

## Verzik after seam3

- P2 bounce, measured with `t.player.step_tick` in solo Entry: the tile held at the END
  of T-1 decides the attack on T. Adjacent, e.g. 6430,90 (local 30,26): body slam
  (`npc_anim` 8116, rolled against crush, knockback to 6427,93, 5-tick stun) on 10 of 15
  attacks (75 % [M48]). Under her, e.g. 6431,90 (local 31,26): the STOMP (8116 plus
  "There's nothing for you there!") 5 of 5, knockback to 6429,88, 7-tick stun -- under
  her is NOT safe in P2 (walking under is P3's melee rule). Two out, 6429,90: bomb or
  zap. A step onto 6430,90 issued at T-1 (resolving on T) drew no slam 4 of 4: step off
  on T-2 so the step lands by T-1.
- Entry damage (wiki Entry infobox, tob_verzik.constant): P1 bolt up to 60 (30 under
  Protect from Magic); P2 urnbomb 16 (8 prayed); body slam 16; stomp ~34; P3 melee 36;
  P3 magic/ranged 20 (10 prayed), also after the enrage. Still the Normal figures in
  Entry (no source): lightning 48, exploding nylocas 63/26/8, Athanatos landing 78,
  blood spell 45, power blast 80, web snap 40.
- The P2 stomp is now rolled 1..max in every mode ("up to 82 ... always a successful
  hit", Strategies:909); it dealt a flat 82 before.
- Attribution: P1 bolt `hit_player` rows have `npc_slot` -1 and `npc_type` -1 (a
  player-side landing queue): match them to projectile 1580 by tick (+3). P2 and P3 hits
  carry her slot and type 8372 / 8374. Exploding-nylocas blasts show `npc_slot` -1.
  The P3 green ball is projectile 1598 with no `npc_anim` row of its own and lands for
  75 % of the Hitpoints level (74 at 99).

## Measurement recipes from seam3

- Healing without god-mode rows: `t.cheat('::god 1', false); t.cheat('::god 0', false)`
  in the same pump tops hitpoints up (the god branch heals on the way in); the
  `hit_player` rows outside that window keep their real damage. Never in a room test.
- A private content root, for measuring while other workers edit OSRS-Content:
  `git archive` OSRS-Content HEAD `server/scripts` with the pathspecs
  `':(exclude)osrs239-content/server/scripts/selftest'` and
  `':(exclude)osrs239-content/server/scripts/build*'` (or the archive is 16 GB), symlink
  every other entry of osrs239-content and osrs239-content/server, `mkdir
  server/scripts/build` (or sscompile fails "cannot write .../build/script.dat"), run
  `tools/ss_allocate.py --tree` and `src/build_opt/sscompile --src/--out/--content-root`,
  then run with `TORIRSSERVER_CONTENT` / `TORIRSSERVER_SCRIPTS` set
  (build/seam_state/matthew-mbp-m4-raid-b1-seam3/vz3/pack.sh and prun.py).
- A `::tele` on a run's very first tick can crash the client in
  `app_wev_actor_root_fine` (seen on HEAD and on the seam binary). Wait a few ticks
  (`t.ticks(5)`) before the first teleport.

## Coverage scope sidecars: what a test is held to

- `docs/minigames/theater_of_blood/encounters/<room>.scope.tsv` (mechanic_id, scope,
  note) classifies every row of the room table for `tools/raid_gate/raid_coverage.py`:
  `all`, `entry`, `normal`, `hard`, `party` (two or more players or `::tobscale`), or
  `stat` (a distribution one room cannot settle; `verify_tob_timings.py` over Blert and
  many of our rooms settle those, a room test does not write them).
- A solo Entry test (spec.scope `mode=entry party=1`) is held to maiden 39, bloat 32,
  nylocas 44, sotetseg 43, xarpus 34 and verzik 85 rows. Two-mode rows ("Normal and
  Hard") are `# heuristic` comment lines scoped by the name heuristic. To see the list:
  `python3 tools/raid_gate/raid_coverage.py tob_<room> --mode entry --party 1` (the first
  line names every skipped row and why).
- The grader checks only the FIRST element of a measured comma list (`parse_row`, open
  row). Check every instance in the test itself with `t.check`.

## Text rows: the exact ledger detail

A text row (unit `text`) is graded by equality: the measured text is everything between
`measured ` and the FIRST `;`, the spec text everything between `(spec ` and `, grade`
with one trailing ` text` dropped, both trimmed and case-folded. Write the table's
spec_value verbatim after `measured `, then `;`, then the evidence. A comma does not end
the measured text (`measured never text, 19 pools` failed), and never put the word
`text` or a unit after the measured value. `tol` is the table's tolerance even on a text
row, and a grade E text row still carries `approximation, M<n>`. A run that saw
something else writes what it saw as the measured text. The evidence clauses below are
examples; the measured text, the `(spec ...)` group, grade and tol are exact.

- maiden (party) `spec.maiden.blood_extra_target`: `measured furthest; the two extras of
  4 throws landed round the player furthest from her hitbox (2 players, distances 3 and
  7) (spec furthest text, grade C, tol exact)`
- maiden (all) `spec.maiden.drain_stat`: `measured target_time; bow equipped on her
  target tick, swapped to a melee weapon before impact: Ranged drained, Attack/Strength
  untouched, 3 of 3 blackstorms (spec target_time text, grade C, tol exact)`
- maiden (party) `spec.maiden.target_rule`: `measured closest_then_orb; every blackstorm
  went to the player closest to her centre, the tie went by orb order (2 players) (spec
  closest_then_orb text, grade D, tol exact)`
- maiden (hard) `spec.maiden.trail_life_hard`: `measured never; Hard room: 6 trail tiles
  laid, 0 removed by the room's end (loc_set add, no del) (spec never text, grade B, tol
  exact)`
- maiden (hard) `spec.maiden.hard_spawn_invulnerable`: `measured yes; Hard room: 5 hits
  on a blood spawn, every hit_npc 0 and its hitpoints unchanged (spec yes text, grade D,
  tol exact)`
- bloat (all) `spec.bloat.fly_los`: `measured nearest-side-any-tile; flies hit on every
  walking tick with a clear tile on the nearest 5x5 side; 0 flies on 12 walking ticks
  stood directly behind the tank (spec nearest-side-any-tile text, grade A, tol exact)`
- bloat (party) `spec.bloat.fly_spread`: `measured ?; what the run saw (a solo room
  cannot drive the spread); approximation, M64 (spec ? text, grade E, tol approx)`
- bloat (all) `spec.bloat.stomp_defence`: `measured full; Defence drained to 60 before
  the down, read 99 on the stomp tick, 3 of 3 downs (spec full text, grade D, tol
  exact)`
- bloat (all) `spec.bloat.speed_alt`: `measured flip-per-attack; below 40 %: speed
  1->2->1 on 6 consecutive attacks, hit or miss, from npc_tile steps (spec
  flip-per-attack text, grade D, tol exact)`
- bloat (hard) `spec.bloat.hand_hard_clock`: `measured continuous; Hard room: drop gaps 4
  or 6 only across 3 downs and rises (spec continuous text, grade B, tol exact)`
- bloat (hard) `spec.bloat.hand_hard_down`: `measured text-yes; Hard room: hands landed
  on ticks Bloat was down (3 downs) (spec text-yes text, grade B, tol exact)`
- nylocas (all) `spec.nylocas.vasilias_first_form`: `measured melee; her first npc id on
  landing was the melee form (from npc_spawn) (spec melee text, grade B, tol exact)`
- nylocas (hard) `spec.nylocas.prince_min_life`: `measured never; Hard room: an
  unattacked Prinkipas still alive at +52 and +80 ticks, despawned only at hp 0 (spec
  never text, grade C, tol exact)`
- sotetseg (all) `spec.sotetseg.maze_cycle_phase`: `measured global; re-activation tick
  mod 4 equal for both mazes (2 and 2) (spec global text, grade B, tol +-1)`
- xarpus (all) `spec.xarpus.p2.splat_lifetime`: `measured never; 19 pools laid, 0
  removed within the observed 45 ticks (spec never text, grade D, tol exact)`
- verzik (all) `spec.verzik.p2_scan_rule`: `measured adjacent or inside on T-1; bounce on
  4 of 4 attacks with the player adjacent on T-1 and 0 of 4 at distance 2, step_tick rows
  (spec adjacent or inside on T-1 text, grade C, tol exact)`
- verzik (all) `spec.verzik.p2_bomb_judged_tile`: `measured previous tick tile; hit on 3
  of 3 bombs stood on at T-1 and stepped off on T, 0 of 3 stepped onto on T (spec
  previous tick tile text, grade C, tol exact)`
- verzik (all) `spec.verzik.p2_purple_gate`: `measured suppressed while alive; no
  Athanatos cast while one was alive (2 casts, 1 live window of 30 ticks) (spec
  suppressed while alive text, grade D, tol exact)`
- verzik (all) `spec.verzik.p3_special_order`: `measured crabs,webs,yellows,ball;
  specials in order crabs, webs, yellows, ball, crabs (5 specials) (spec
  crabs,webs,yellows,ball text, grade C, tol exact)`
- verzik (all) `spec.verzik.p3_melee_predicate`: `measured adjacent not overlapping on
  T-1; melee only with the tank adjacent and not under her on T-1: 3 melees adjacent, 0
  with the tank under her, 0 on her first P3 attack (spec adjacent not overlapping on
  T-1 text, grade C, tol exact)`

The ledger detail is one line: the wrapping above is this page's, not the string's.

# What the second ToB room pass tripped on (matthew-mbp-m4-raid-b1-rooms-tob)

Folded from the reviewers' doc gaps of the second launch of the ToB room pass. Each line
is a fact about a verb, the tick log, the grader or a room that the sections above lack.

## In a boss fight, press attack with ticks=1

- `t.player.attack(sym, op, ticks)`: `ticks` is the settle deadline (default 10). With a
  deadline above 1 the verb keeps re-aiming at the boss's pixels until a hit lands, and on
  a covered or moving boss that hunted up to 20 ticks (tob_maiden), long enough to miss
  every dodge in the loop. Press with `ticks=1` and read the fight from the tick log's
  `hit_npc` rows, not from the verb's answer.
- A pool decal (Xarpus) lying over the boss pixel can make the press answer covered and
  then spend 3 to 20 ticks of camera settling. Step so the boss is clear of the decal, or
  press with `{ slot = n }` after the step.
- `t.player.inv_op` (eating) costs about 3 server ticks from press to settle. Budget it
  in a dodge loop: an eat started on T-2 of a hit is not finished on T.
- Re-attack after every dodge or step (README lessons). Since the eat-delay port an eat
  no longer drops the engagement (no consume script calls `p_stopaction`); a re-press
  after an eat is redundant but harmless.

## Rows inside a timed loop

- A `t.check` row costs server ticks (its screenshot settles). Inside a per-tick dodge or
  attack loop, collect the readings in a Lua table and emit the `t.check` rows after the
  loop ends.
- A player death aborts the run with `player.died`; every row not yet written is lost.
  Break the loop on low hitpoints (eat first, then leave the loop) and write the rows.
- `t.player.alive()` returns `("ok", detail)` or `("refused", detail)`, both strings: it
  is never false, so `if t.player.alive() then` is always taken. Compare to `"ok"`.

## Specials and stats the driver cannot read

- A special attack is armed with `t.ui.widget('orbs:specbutton')` (the minimap orb;
  `varp301_sa_attack` reads back 1), then the attack press. Pressing
  `combat_interface:special_attack` does NOT arm it (sixth room pass, Xarpus claws). On
  Entry Bloat warhammer specials and Curse usually drained nothing (Defence read 80 of
  80; mage defence 600), but one run read Defence 80 -> 56. STALE since seam10: the drain
  is deterministic and the misses were the probe's (combat-tab press, a 1-tick attack
  timeout, an unguarded read). The recipe is in the seam10 section, "Bloat: Defence reads
  80 of 80 after a Dragon warhammer special".
- `maiden.freeze_full_bonus` is reachable since seam5: Ancients as a setup bring-along
  and a +140 magic set (seam5 section, "Maiden: the freeze curve").
- `::tobboss` now prints att/str/rng/mag and `size=` (seam4, "::tobboss levels and
  size" below); `sotetseg.attack_level` reads from it.
- Every npc pool row now carries `size` (seam4, "t.npc rows carry size" below), so
  `xarpus.size.p1_p2` is measured from the player's side.

## Bloat walks before the barrier click

- In tob_bloat's tick log Bloat's `npc_tile` rows start on server tick 15, the room
  landing, while the author's `room start` mark (the barrier click) was tick 59. So
  `bloat.fly_first` / `bloat.first_walk` measured from a mark at the click are short by
  the gap: measure them from the room's own start (the first `npc_tile` row of the boss),
  and state which origin the row used.

## The grader's number parsing

- `raid_coverage.py` reads a measured range only as `lo-hi`. `38.4..38.4` passes the
  number test and then crashes `float()`; write one value, or `lo-hi`.
- Tolerance `range` against a single spec value demands equality (lo = hi), so a table
  row whose prose means a ceiling or floor (xarpus `max_hit.entry` 6, `stomp_max` 9,
  `retaliate_min_entry` 38) is graded exact. Report the measured figure honestly; a
  ceiling met below the spec reads as a failure until the table says `0-6` (open grader
  or table row).

## Xarpus retaliation solo

- The P3 retaliation scales with the share of exhumeds absorbed, not orbs: a solo run
  always reaches 100 percent (a late cover still lets one orb through), and the hit
  reached 103-105. Survive the probe on a Saradomin brew's boost (hp 115).
- The room-exit item `tob_skeleton_with_weapon` at 6435,109 answered "I can't reach
  that!" to `click_loc` from 6434..6436,106 (the arena's north edge): it stands past the
  exit gate. The route is in seam4's "Xarpus room exit" below.

# Seam pass 4: the second room pass's residue

Seam pass 4 (`matthew-mbp-m4-raid-b1-seam4`, triage `SEAM_TRIAGE_2026-10-03b.md`) added
one field to every npc pool row (`size`), one internal driver call
(`api_drive.menu_rect`, used by the press), two read-only debugproc readouts, and the
content fixes of OSRS-Content a44e3d97bf: the Entry Athanatos, Maiden's blood spawns,
extras and Entry trail, Sotetseg's solo Entry death ball, and Xarpus's Entry death,
retaliation floor and exit. The fixers' reports are under
`build/seam_state/matthew-mbp-m4-raid-b1-seam4/`.

## t.npc rows carry size (the footprint)

- Every pool row (`t.npc.state`, `t.npc.nearest`, `t.npc.by_symbol`, `t.npc.tiles`) has
  `size`: the footprint in tiles from the npc config, 1 when the config states none.
- `x,z` is the footprint's south-west corner. The npc covers `x .. x+size-1` by
  `z .. z+size-1`.
- A transmog rewrites it on the tick the client applies the new type. Entry Xarpus reads
  3 in his static form (`tob_xarpus_static_story`) and 5 in his fighting form (npc 10768)
  from the stand-up (scratch s4xsize_after1).
- `t.npc.state_text(row)` names it as `size N`. On a binary built before seam4 it says
  `size unread` and `row.size` is nil.
- Conformance rows: `npc.state_text` grades the man at size 1; `seam.npc_state_size`
  grades `goblin_unarmed_melee_1` at 1 and `cow` at 2.

## Ordinary npcs for a footprint check

- `goblin_unarmed_melee_1` 1, `man` 1, `cow` 2 (configs/all.npc `[cow] size=2`).
- `::spawn cow` in the Lumbridge goblin field gives a size-2 subject.

## A press no longer takes another copy's row from a stale menu

- Before it presses, `QD.drive._press_row` asks the client's own hit test
  (`api_drive.menu_rect(x, y)`, which is `UIMinimenu_HitOption`). If the press would
  SELECT a row of an open menu that offers this press's op on a different element (another
  copy), it dismisses the menu first.
- Each dismissal prints `QUEST stale-menu N: ...` to `client.log`.
- Before: 5 `hit_npc` rows on the copy never asked for (s4stale_before2). After: 0
  (s4stale_after3, two dismissals, each on row 6 `Attack Goblin`).
- So for `attack`, `talk_to` and `press` with `{slot=}` or `{at=}`, the verb's answer
  tells you which copy was hit again. The tick log's `hit_npc` rows on the asked world
  slot are still the stronger proof.
- Unchanged on purpose: a press on a stale menu's `Walk here`, `Examine` or `Cancel` row,
  on the asked copy's own row, or on its title bar behaves as before seam4. A stale
  `Walk here` row can still walk the player, and a title-bar press is swallowed.
  hauntedmine, childrenofthesun and thefeud have green timelines that depend on those
  presses: dismissing them made the presses faster, but every later tick moved and all
  three went RED. If a room test sees an unexplained step after a `covered` press, look
  for this (open row in CONTENT_BUGS.md).
- A dismissal saves ticks; it does not cost them (tryToPickUpKey 24 -> 20, catchSnake
  26 -> 10). The seam3 regression came from moving a timeline the quest tests were
  written against.

## ::tobboss levels and size; ::tobpurple

- `::tobboss` (tob_selftest.rs2) prints `tobboss record=R room_mode=M hp=H of P base=B
  def=D of Db att=A of Ab str=S of Sb rng=G of Gb mag=K of Kb size=Z name=N`, each level as
  current of base.
- The new fields sit before `name=`, so readers matching `hp=(%d+) of`, `def=(%d+) of` or
  `record=%a+ room_mode=%d+ hp=(%d+)` still work.
- Sotetseg reads att 180 (Entry), 250 (Normal), 350 (Hard), all size 5 (spec
  `sotetseg.attack_level`). Xarpus reads size 3 static and feeding, 5 in combat (spec
  `xarpus.size.p1_p2`). Run s4e_readouts_a, s4e_xarpus_a.
- `::tobpurple` (read-only) prints `tobpurple n=N mode=M`, then ` hp=H of P def=D of Db
  mag=K of Kb size=3` for each Nylocas Athanatos in the instance. In Entry it reads
  `hp=30 of 30 def=40 of 50 mag=40 of 50 size=3`.
- `encounters/sotetseg.tsv`'s `attack_level` note ("ours uses the Normal record in every
  mode") is stale: the readout shows the mode records.

## Entry Verzik: the Athanatos is 30 hp

- The room spawns one Athanatos type in every mode. In Entry, `~tob_verzik_athanatos_land`
  gives it record 10844's figures: pool 30 (stacked for the party like the reds and the
  crabs), Defence 40, Magic 40 (cache_npc_verzik.txt:699-701).
- Solo Entry: a whip killed it in 4 hits summing 30, and P3 followed at tick 128
  (s4e_verzik_entry_c). Before, the bar held at 21-28 of 30 for the whole P2.
- The Normal and Hard Athanatos now have the cache's Defence and Magic 50; before they
  stood at the engine default 1.
- Its Defence and Magic are lowered with `npc_statsub`, so the base stays 50. If the
  engine restores npc stats over time they could climb back during a long P2 (not
  measured).
- Seam5 fixed the levels of Entry Verzik and Entry Maiden and the Entry P3 pool (600).
  The Entry P2 lightning has no Entry figure in any source (seam5 section).

## Maiden after seam4

- Blood spawns step on every free tick: 1000 permille, 5538 of 5538 free pairs over 8
  slugs, no still runs of 1-4 (s4m3_after2). Any still run in a room test is a freeze or
  her death, so measure `maiden.blood_spawn_step` over all pairs outside runs of 5+.
- Every blood throw carries exactly two extras (30 of 30 throws, solo Entry).
- In the tick log, a projectile row with spotanim 1578 and target -1 is the pool under
  the player (`projanim_pl`); target 0 is an extra (`projanim_map`), flying 25 cycles
  longer.
- Entry trail damage is a 2-5 roll (`^tob_maiden_trail_damage_entry_min/max`, wiki
  Blood_spawn:48). It shows as `hit_player` rows with npc_slot -1 and hitsplat 28. A pool
  never lands on blood, so while you stand on a trail tile every npc_slot -1 hit is a trail
  hit (18 and 17 hits in two runs, all 2-5). Normal and Hard trails keep `10 + 2c`.
- Trail recipe: from `t.npc.tiles('maiden_blood_slug', 40)` pick a slug at x <= 6447 (solo
  instance handle 1), `t.player.walk_to` its tile, and stand 3-4 ticks. Slugs in the east
  pocket at 6450-6451 cannot be reached.
- Death timeline on our server: 8093 at K+1 (engine death seq), dying_a retype at K+3
  (`[ai_queue3]` at the engine's corpse stage), dying_b and 8094 at K+5, `npc_free` at K+9.
  Blert has 8364 at K+1, 8365 at K+5 and the despawn at K+9 (raids 147ff143, db36efc7,
  e2fbcc68, a40d3d9c Hard; seam2 saw 13 of 13). The spec rows `maiden.death_a_len` 3 and
  `death_total` 7 are animation lengths and AdvancedRaidTracker's count from K+1; they need
  restating (open). Until then, assert 8093 -> 8094 = 4 and killing blow -> `npc_free` = 9
  and say which rows they are.
- A seam that changes how many `random()` calls she makes per tick changes the whole
  room's roll stream. The committed tob_maiden test relied on accidents (standing in a pool,
  a slug spawning) and lost its pool rows after seam4 (copy run s4m3_maiden_copy: 60 PASS,
  1 FAIL at `pool_heal_ratio`, coverage 32 of 39). Drive the mechanic on purpose.

## Sotetseg after seam4

- The death ball in solo Entry (scale 1) is a flat 15 at launch+16 (`~tob_sote_ball_flat`),
  not a roll: 85:15, 145:15, 205:15 (s4_sote_ball_after). In groups (Entry 80, Normal and
  Hard 121/155/188) it is still a roll under the ceiling, split over the 3x3.
- Sources: wiki_Sotetseg.wikitext:105, wiki_Theatre_of_Blood_Entry_Mode.wikitext:191,
  transcripts/yt_B_gjVdmfOrY.md:93. Gear reduction still applies (`~gear_reduce_damage`);
  no source says whether it should.
- The ordinary Entry ball rolls 1..22 unprayed: over 40 balls the largest was 22 (three
  times), none above. Solo there are no ricochets.
- An eat no longer holds a death ball's impact (the eat-delay port, OSRS-Content 7936c59bf9):
  the impact is a player queue and lands on launch+16 whether or not the player eats.
  Before the port the eat's `p_delay` held it and the splat read as an ordinary ball
  (2 of 5 moved in s4_sote_ball_before).
- Long measurement without a maze: `::tobwarp 15 37` (3 tiles out, no melee), no prayer,
  no attacks (he stays above 66.6 %), 28 sharks with an hp < 55 threshold covers about 45
  attacks.

## Xarpus after seam4

- Room exit (Entry/Normal, instance handle 1): the Dawnbringer skeleton
  `tob_skeleton_with_weapon` at 6435,109 stands two tiles past the exit gate
  `tob_arena_barrier` at 6434,107. After the kill: `t.player.walk_to(6434, 106, 12)`;
  `t.player.click_loc('tob_arena_barrier', 1, {at={6434,107}})`, which puts you on
  6434,108; then `t.player.click_loc('tob_skeleton_with_weapon', 1)`,
  `t.chat.continue_()` on the objbox, and `t.inv.await('verzik_special_weapon', 1, 5)`.
- The gate's `click_loc` answers `timeout` (detail `settle_after_click from 6434,106 ->
  6434,108`) even when you crossed, so assert `t.world.tile()` z == 108, not the result
  word.
- The exit gate opens while he is still alive (`~tob_barrier_step` runs once the room
  starts; open row). Do not take a crossing as proof of the kill.
- Entry death: killing blow K = `npc_death`, 8062 at K+1, then at K+3 the book
  `obj_add`, `npc_retype` 10768 -> 10769 (`xarpus_death_story`) and `npc_anim` 8063
  together, `npc_free` at K+5. Measure `xarpus.death.collapse` as `npc_anim` 8063 ->
  `npc_free` (2). Normal and Hard: 8340 -> 8341 and 10772 -> 10773, same shape.
- P3 retaliation in Entry is the 50-75 base scaled by 38/50 (38-57: floor [wiki] Entry
  infobox "38+ recoil", ceiling [M123]), then times (100 + 40 % of absorbed %) / 100. Solo
  is always 100 % absorbed, so Entry reads 53-79 and Normal/Hard 70-105. To check the
  uplift in Entry, invert `floor(b*38/50)` for b in 50..75.
- The retaliation fires once per HITSPLAT: a scythe swing from the faced quadrant takes
  three (77 + 22 killed a 99-hp player in one tick in Entry before the floor change).
  Probe with a one-hitsplat weapon.
- Stomp re-measured standing in 6432..6436 x 97..101 for 36 ticks: Entry per-tick sum
  max 5 ({5:24, 4:7, 3:4, 2:1}), Normal max 9. The splats are slotless `hit_player` rows.
  A spit splash launched before you stepped under can land on the same tick (stomp 3 +
  poison 6), so count slotless rows only.

## Tooling facts from seam4

- `tools/wiki_droptable.py` `MINIGAME_DEATH_QUEUES`: a minigame can own a wiki-tabled
  npc's `[ai_queue3]`. The generator then writes `[proc,wiki_<slug>_drop]` with no binding
  and refuses to write unless the owner file binds the trigger and calls the proc. Call the
  proc BEFORE any `npc_changetype`, because `npc_param(death_drop)` is read off the current
  type (dropping after the retype put a pile of bones under Xarpus).
- Never call a driver verb (`t.player.attack`, `t.npc.nearest` and the like) inside a
  `t.await` level function. A nested verb's own await appears to resolve the outer await
  at once (an await on an `npc_death` row answered ok after 1 tick with no death). Poll
  with a loop of `t.ticks(1)` instead.
- `t.ticklog.rows` has no from_type/to_type filter for `npc_retype`; filter with
  `where=function(r) return r.to_type == X end`.
- `run.py <quest>` without `--no-publish` copies that run's shots and ledger over the
  quest's committed evidence in OSRS-Content (`selftest/quests/<dir>/play`). Seam
  regression runs pass `--no-publish`, and `--name <label>` when another worker holds
  the quest id's lock.
- Entering a raid a fourth time in one session stood an Entry Xarpus up at 1768 of 1768
  (520 x 3.4, the four-player stack); the same room entered fresh stood at 520.
  `^tob_var_scale` appears to count earlier `t.raid.enter` re-entries (open). Enter one
  raid per run.

# What the third ToB room pass tripped on (matthew-mbp-m4-raid-b1-rooms-tob)

Folded from the reviewers' doc gaps and the sampler's send-backs of the third launch.

## A covered first press still costs ticks

- `t.player.attack(sym, op, 1)` settles in one tick only when the first press lands.
  When it is covered (a pool decal, another npc, the boss moving), the verb's cover
  recovery and `walk_near` spent 22 to 53 ticks before it answered. In a timed loop
  step to a tile with a clear line to the boss first, or press with `{ slot = n }`.
- Seam5: `ticks` <= 2 (or `opts.quick = true`) now takes the fast press, which answers
  `covered` within a tick or two instead (seam5 section, "The fast press").

## Hitpoints after an eat, and what an eat holds

- `t.skill.read('hitpoints')` lags an eat by 1 to 3 ticks. Re-eating on the stale read
  wastes food: after an eat, wait for the reading to rise (or count the eat) before the
  next threshold test.
- An eat holds no hit any more. Until the eat-delay port (OSRS-Content 7936c59bf9) an eat's
  `p_delay` held every queued hit on the player (a Sotetseg melee swung on tick 531
  landed on 535 instead of 532). Now an incoming hit lands on its own tick while the
  player eats, so a late splat cannot be excused by an eat: it is a FAIL (or a
  content_bug), never a PASS. See the seam6 section, "The eat-delay port".

## Exact-tick actions

- `t.ticks(n)` waits on the CLIENT clock, not on server ticks.
  For an action that must resolve on a given server tick, `t.await` on `t.tick()`
  reaching the tick before it, then `t.player.step_tick`.

## Verzik phase 1: pillars, bombs, the attack spot

- The hide tile behind the west pillar is 6426,93 (and 87, 81), but attacking from it
  walks you out of cover, because the pillar blocks the line. Attack from 6428,93.
- Bolt hits on a pillar are `npc_hitmark` events only, never `hit_npc` rows. Read pillar
  health with `::tobpillars 0`.
- A P2 bomb's `hit_player` row lands on the landing tick + 1. The landing tick is the
  1584 ground graphic's `map_spotanim` row, and that row's `delay` field carries the
  graphic's duration, not an offset.

## Maiden: the Ancients spellbook

- Seam5: bring Ancients in `setup` with `::setvar varb4070_spellbook 1` and cast Ice
  Barrage or Ice Burst with `t.player.cast` (seam5 section, "Maiden: the freeze curve").

## A measured value must come from the log, not from the spec

- A spec row whose test writes the SPEC value as "measured" when a consistency check
  holds is not measured, and the sampler sends the room back. Two examples from this
  pass: `xarpus.p3.retaliate_uplift` wrote 40 because one retaliation of 78 fit
  `base * 140 / 100`, but in Entry (base 50-75 scaled by 38/50 first, seam4) a 78 fits
  any uplift from 37 to 105 percent; `xarpus.p3.screech_pct_entry` wrote 22.5 while the
  log bracketed the threshold at 19.6-23.1 percent (120 -> 102 on the first splat of a
  scythe swing), wider than its tolerance of +-1.
- Narrow a threshold with small hits near it (a weak weapon, one hitsplat a swing), and
  pin a scaling factor with several samples (the largest seen hit caps it: in Entry a 79
  needs an uplift of at least 39 percent). Write what the log brackets; if the bracket is
  wider than the tolerance, the row is open, not a pass.
- The grader still reads only the first element of a measured list (above), so a list
  row such as `measured 1,4 ticks` against an exact 1 is graded as 1. Grade every
  instance in the test.

# Seam pass 5: the third room pass's residue

Seam pass 5 (`matthew-mbp-m4-raid-b1-seam5`, triage `SEAM_TRIAGE_2026-10-03c.md`) added a
fast press to `t.player.attack`, `t.player.cast` and `t.npc.await_dead_engaged` (seam row
`seam.attack_fast_path`), made Entry and Hard Maiden fight as their mode's records, set
Entry Verzik's levels, pillar and P3 pool, and gave three Normal add records their cache
levels (OSRS-Content d2134f89f5). The Sotetseg hit-delay seam found its cause in the eat,
outside its files, and changed nothing. The fixers' reports are under
`build/seam_state/matthew-mbp-m4-raid-b1-seam5/`.

## The fast press: t.player.attack / t.player.cast with ticks <= 2 or opts.quick

- `t.player.attack(sym, op, ticks, opts)` and `t.player.cast(spell, sym, ticks, op, opts)`
  take the fast press when `ticks` <= 2 or `opts.quick = true`. The press aims once at the
  named copy and presses once. On `covered` it makes exactly one re-aim and presses again.
  The re-aim is the copy's new tile if it stepped, else a line hunt through the missed
  pixel, else a camera nudge (pose 4 within 3 tiles, pose 1 beyond).
- It never runs the cover recovery, never `walk_near`, and never hunts longer than one
  tick per aim. Every goblin press took 0-1 tick. The worst Nylocas press took 3 ticks
  over 76 presses, where the quest press took 19 and 22.
- `opts` carries `quick` beside the selector: `{ slot = n }` (fast because ticks <= 2),
  `{ slot = n, quick = true }`, `{ quick = true }` (the nearest copy), and
  `{ slot = n, quick = false }` (the quest press even at ticks=1).
- The detail of a fast press contains `fast path: aim ...; press 1 at x,y (...): covered;
  re-aim: ...; press 2 ...; N tick(s) spent`.
- Default ticks (10), or ticks >= 3 without `quick`, take the quest press exactly as
  before. cooks_assistant, druid, hauntedmine, childrenofthesun and thefeud were
  tick-identical. chompybird's three `attack(..., 5, 1)` refusal probes now press fast
  and stay green (600 -> 586 ticks).

## A fast `covered` answers at once and names other copies

- The detail ends `... N tick(s) spent; the menus offered this op on slot S (element E
  'Attack Nylocas ...') -- press one of those instead`, or `on no other copy`.
- Pick another target from those slots rather than pressing the same copy again.

## The kill wait remembers a fast fight

- The engagement stamp records a fast attack or cast, so `t.npc.await_dead_engaged`
  re-presses (or re-casts) that fight with the fast press too.
- Its detail then says `N re-engagement(s) (fast path re-presses)`, and each
  re-engagement note carries that press's own `fast path: ...` account.
- Pass `opts.quick` to the wait to choose otherwise.

## A fast cast and its settle

- The cast still settles for `ticks`. With ticks=1, a cast at a copy several tiles away
  answers `timeout` before the Magic XP lands.
- When the XP was paid, the detail says `CAST (Magic XP paid; the stamp is written), but
  the 6-tick flight window outlasts ticks=1`. When it was not paid yet, read Magic XP
  yourself over the next few ticks.
- The quest cast's timeout still says `the cast never ran` even when XP was paid (left
  as it was, so the quest path stays byte-identical).
- One far named copy (8 tiles across the Lumbridge goblin field) was pressed fast and the
  server never cast on it: no XP and no refusal line for 10 ticks. The cause is not known.
  The seam row stands two tiles off first. Watch for a raid cast at range that is silently
  dropped.

## The kill rate now follows the loop, not the verb

- In the Nylocas copy, press ticks fell from 103 to 81 over 300 ticks, but wait ticks
  rose from 130 to 149, because the loop held each target 3-5 ticks after a landed
  press. A raid loop's hold and eat policy now sets its kill rate.
- tob_nylocas with the fast press: 0 presses took six ticks or more, and 37 of 96
  nylocas were killed in 368 ticks. With the 300-tick cap lifted it ate all 22 sharks and
  left at hp 4 at click+448. Vasilias and the collapse rows were not reached. The aggro
  forms swing unconditionally (tob_nylocas.rs2:1072), so the next step is tactics.

## Entry and Hard Maiden are their mode's records

- From her first tick, Maiden in Entry is `tob_maiden_100_story` / `_70_story` /
  `_50_story` / `_30_story` (ids 10814-10817), and in Hard the `_hard` forms (10822-10825).
  `~tob_maiden_mode_form` in tob_maiden.rs2 retypes her before the barrier and keeps her
  pool.
- Her crabs are `maiden_elemental_story` (10820; Hard `_hard` 10828), and her blood
  spawns are `maiden_blood_slug_story` (10821; Hard 10829).
- In Entry, `t.npc.nearest('tob_maiden_100')` and the Normal crab and slug symbols answer
  `no_row`. Address her by her mode symbols. `t.raid.enter` lists all twelve bodies (the
  closer added them to raid.lua).
- Entry levels: `::tobboss` before the barrier reads `record=entry ... hp=500 of 500 def=80
  of 80 att=140 str=140 rng=140 mag=140` (was `record=normal def=200 att=350`). Defence
  stays 80 through the 70 and 30 bodies.
- After a transmog, `::tobboss` says `record=normal`, because its classifier knows only
  the 100% bodies. The levels it prints are still Entry's.
- Her death in Entry is 10817 -> 10818 (dying_a, K+3) -> 10819 (dying_b, K+5), then
  `npc_free` at K+9. The room clears.
- The `::tobcrab*` debugprocs and the slug debug readout match only the Normal records,
  so in Entry they find nothing (open, tob_selftest.rs2).
- Every Normal Maiden past 70% used to fight at Defence 1: `tob_maiden_70/50/30` had no
  levels. They now have the cache's 350/200/350/350/350. When a Maiden record changes,
  check `::tobboss` after `::tobmaidenpct 60`.

## maiden.blood_spawn_dodged_cap is a cap

- Measure the most blood spawns from any one throw that no player stood in. "Stood in"
  means the player's previous-tick tile was a live pool tile. Pass when the maximum is
  <= 1, and state N.
- A solo throw has 3 pools at 5 % each (the 10 % halved), so the chance of no spawn in N
  dodged throws is 0.857^N (20 throws: 4.6 %; 30 throws: 1 %). A maximum of 0 over a small
  N is consistent with the cap, not a failure.
- Measured on our server: 61 dodged throws gave {0: 51, 1: 10}, max 1 (s5m4_after2). The
  content was right and was not changed.
- To dodge every throw, move only on the 8091 edge (`t.npc.state` seq_tick), between two
  tiles at least 5 apart. Moving on every attack walks back onto the last throw's pools:
  42 of 47 throws stood, max 2 (the 1 + stood cap).

## Maiden: the freeze curve

- The curve is in tob_maiden.rs2: `~tob_matomenos_hit_roll` and
  `~tob_matomenos_freeze_chance`, with `^tob_maiden_freeze_bonus_full` 140
  (tob.constant:238). The chance is `(lvl_now * (bonus + 64) - lvl_base * 64) /
  (lvl_base * 140)`, which is 100 % at +140 unboosted.
- Only Ice BARRAGE and Ice BURST use it (`~pvm_barrage_spell` -> `~player_npc_hit_roll`).
  Ice Rush and Ice Blitz roll ordinary accuracy. The curve counts equipment magic attack
  only; prayer and Void are not counted (open).
- Bring Ancients in `setup`: `::setvar varb4070_spellbook 1`.
- A +152 set in the pack: ancestral_hat, ancestral_robe_top, ancestral_robe_bottom,
  kodai_wand, occult_necklace, eternal_boots, magus_ring, arcane (spirit shield). With it
  3 of 3 barrages hit (spotanim 369) and the crab stood still until killed. At +0 a
  barrage splashed (spotanim 85) (s5m4_freeze1).

## Entry Verzik: levels, pillars, P3 pool

- `::tobboss` levels per phase in Entry: P1 att 180, str 150, rng 180, mag 180, def 10;
  P2 200/150/180/180, def 120; P3 180/200/180/180, def 120. `of 400` is the Normal
  record's base, which the content lowers (`~tob_verzik_entry_levels`, constants from the
  cache `_story` records in tob_verzik.constant).
- Entry pillars read `tobpillars N hp=200` (`::tobpillars 0`). Normal and Hard stay 185.
- SUPERSEDED by seam6 ("Verzik's three pools" below): the bar is now P1 + P2 + P3 (1300
  solo Entry) from the barrier, each phase opens on its own full pool, and nothing carries.
  Measure a pool from `::tobboss phase_hp=X of Y`, not from the pool growth.
- The Entry P2 lightning has no Entry figure in any source. Keep `verzik.p2_zap_max`
  scoped to normal. A solo Entry zap measured 8 and 3 under Protect from Magic.
- An Entry red reads `red hp=20 def=30 mag=30`.

## ::tobaddlevels

- Read-only (tob_verzik.rs2). It prints hp/att/str/def/rng/mag for every Verzik red and
  every Maiden crab or blood spawn in the room, in every mode since seam6.
- Since seam6 the reply prints at most four groups after `n=<count> shown=<groups>`, so it
  stays under the 252 characters that desync a session (trap 27).

## An eat held every queued npc hit (fixed by the eat-delay port)

- HISTORY. Until OSRS-Content 7936c59bf9 general/scripts/food.rs2 ate with `p_delay(^eat_delay)`
  (2 ticks), and a delayed player runs no queue of any kind (torirs_server_scripts.c:938
  and :1540, the same canAccess gate as LostCity). A hit due at +1 landed at +3 when the
  eat landed on the swing tick and at +4 the tick after (Sotetseg's melee and balls,
  every raid projectile impact, an ordinary dark wizard's spell).
- LostCity's eat (consume.rs2:101-110) never calls `p_delay`; the port of that landed in
  seam6 (section "The eat-delay port"). A hit queued on the player is a delayed-player
  queue still, so any OTHER script that `p_delay`s (the ToA supply drinks,
  toa_supplies.rs2, are still `p_delay(1)`) holds it the same way.

## Measuring

- Ticklog pairing for an npc's hit delay: learn the cast seq as the latest `npc_anim` of
  the npc's slot before its first `hit_player` row. Then pair each cast with the first
  `hit_player` of that slot within 8 ticks.
- A cast that deals no splat of its own (a dark wizard's weaken or confuse) shows up as a
  +5 that is really the next cast's hit.
- Filter rows by slot in the call (`slot = ws`). A `where` function over unfiltered
  `npc_anim` rows ran out of the 400,000-instruction budget in 200 ticks.
- A Dragon warhammer special drains 30 % Defence and an Elder maul 35 %. Name the weapon
  the test really swings.

## Proving a content change without the shared tree

- Make a scratchpad copy of osrs239-content as a symlink farm, with configs/, pack/,
  server/pack and server/scripts (minus build* and selftest) copied, so ss_allocate's
  writes stay private.
- Build its pack with `make -C src torirsserver-scripts-lanes TORIRSSERVER_SCRIPT_LANES=
  TORIRSSERVER_CONTENT_DIR=<copy> TORIRSSERVER_SCRIPT_OUT=<copy>/server/scripts/build`.
- Run with `TORIRSSERVER_CONTENT=<copy> python3 tools/quest_gate/run.py ...
  --no-publish`. Damage rolls are the same in both trees, so a before/after diff shows
  only the timing.
- Run seam-pass regression quests with `--no-publish`. Without it, run.py rewrites
  OSRS-Content's tracked selftest/quests/<quest>/play evidence.

# What the fourth ToB room pass tripped on (matthew-mbp-m4-raid-b1-rooms-tob)

Three facts the room authors and reviewers of the fourth launch asked for.

## Protection prayers against nylocas hits (fixed in seam6)

- Before seam6 no nylocas swing read the overhead prayer. Since seam6 (OSRS-Content
  2cddff56d5) a wave nylocas's swing is blocked to 0 by the protection prayer of its style.
  See "Protection prayers block a wave nylocas's hit" in the seam6 section below.
- Explosions (`~tob_nylo_detonate`) and support collapses still ignore prayer.
- The gear reduction still always uses the magic style, whatever the nylocas's style.

## Small controlled hits on a boss, for a threshold row

- Bring the weak weapon as a setup bring-along (`::give`), and swap to it inside `run`
  with `t.player.equip`. Wielding gear is a player action, not a cheat. Then hit near the
  threshold one splat at a time.
- Levels go down only in setup. Any `::setlevel` inside `run` is a world-changing cheat
  and a rejection (test/raids/README.md "Cheats inside `run`"). If you need a low
  Strength for small hits, set it in setup and plan the whole fight around it. You can
  also use the weak weapon only near the threshold.
- Report what the log brackets. See "A measured value must come from the log, not from
  the spec" above.

## Verzik P3 webs bind the tile you stood on when she threw

- `~tob_verzik_webs` throws three webs at the tile each player stood on at the throw
  (tob_verzik.rs2:2703-2713). Each lands after its flight
  (`[queue,tob_verzik_web_land]`, :2745).
- If you are still on that tile when it lands, you are frozen
  (`%varp5754_frozen` = map_clock + `^tob_verzik_web_lifetime` 20 + 1, :2752-2756) and see
  "You are bound by a web!". An unfreed web snaps after 20 ticks for 1..40
  (`^tob_verzik_p3_web_break_max`, :2760-2770). Killing the 10-hp web frees you at no
  cost (:2777).
- The verzik author saw that a bound player's press at the web never landed ("no hit
  landed" every 2 ticks, so the web was never killed). Walk off the throw tile during the
  flight, about two ticks before her web special lands. Then press the web from the
  next tile with a ranged or magic attack.
- Solo, there is one web per cast (one per player), so `p3_webs_per_cast` 3 cannot be
  measured in a solo run.

# Seam pass 6: the fourth room pass's residue

Seam pass 6 (`matthew-mbp-m4-raid-b1-seam6`, triage `SEAM_TRIAGE_2026-10-03d.md`) changed
no driver verb. It landed two content seams in OSRS-Content 2cddff56d5. The first makes
protection prayers block wave nylocas hits. The second gives Verzik three separate pools
and an enrage threshold compared without truncation, and makes the readouts know every
mode's Maiden and Verzik records. One seam row proves the first:
`seam.nylocas_protect_blocks_wave_hit`. The third seam, the tree-wide eat-delay port, was
held back by the closer because it moved two committed quest tests from green to RED; it
landed afterwards on the owner's decision (2026-10-03) in OSRS-Content 7936c59bf9, with the seam
rows `seam.eat_does_not_hold_queued_hit` and `seam.eat_delay_clocks`. The fixers'
reports are under `build/seam_state/matthew-mbp-m4-raid-b1-seam6/`.

## The eat-delay port: incoming hits land on their tick

- Landed 2026-10-03 on the owner's decision (OSRS-Content 7936c59bf9). An eat or a sip is a pair
  of clocks in player/scripts/consumption/consume_shared.rs2 (`varp7219_consume_food_delay`,
  `varp7218_consume_combo_delay`, `varp7220_consume_potion_delay`; LostCity
  consume.rs2:96-110's shape), never a `p_delay`. Every food and potion script in the
  tree uses it except the ToA supply drinks (toa_supplies.rs2, still `p_delay(1)`).
- Incoming hits land on their tick. A scripted npc hit queued on the player (Sotetseg's
  melee and balls, a raid projectile impact, a dark wizard's spell) lands where it would
  without the eat: Sotetseg melee +1 x8 with an eat on the swing tick (landeat_sote_eat),
  a young dark wizard's spell +1 eaten as plain. Eat around a timed hit freely; do not
  count eaten-through hits separately any more.
- An eat costs its delay on the eater's NEXT EAT and NEXT ATTACK only. Food every 3 ticks
  (pies 1/2, cakes 2/2/3, pizzas 1/2); a combo food (`tbwt_cooked_karambwan`, halibut) may
  follow a food on the same tick but not another combo food; potions have their own 3-tick
  timer (a sip can share a tick with a food); barbarian mixes use the food timer. A
  refused press is silent, as in LostCity.
- The attack: an eat adds 3 ticks to a weapon delay that is still running (2 for
  karambwan/halibut, stacked: shark + karambwan = 5) and nothing to a ready weapon. An
  unarmed goblin fight swings 4,4 and 7,7 after an eaten swing.
- An eat no longer stops the player's attack or walk (no `p_stopaction` in any consume
  script).
- `t.player.inv_op` settles for 3 ticks, so no test can press twice inside one food delay.
  Read the clocks instead: `::eatdelay` prints `eatdelay: clock C food F combo K potion P
  action A` (each the last tick still refused; food = the eat tick + 2), and `::eatgate`
  runs the gate procs in one tick and prints two lines (`eatgate:` and `eatgate+:`).
- The driver eater (`opts.eat`) still waits `QD.COMBAT_EAT_DELAY_TICKS` = 3 after
  inv_op's own 3-tick settle, so it eats at most every 6 ticks; the server would take one
  every 3. Seam27 added `opts.eat.quick = true`, which bites at the server's
  delay (every 3 ticks, `@tick` on each bite in the detail); the default is unchanged
  until the 36 kept quests that pass `opts.eat` are re-run (see "Several inputs in one
  tick" below).
- Quest-loop impact: `troll` and `regicide` went green -> RED with the port (the player
  dies without the held hits); the quest loop re-authors them with more food or prayer.
  troll: first failing row 31 player.died at tick 465, in killGeneral's wait (2824,10077 level 2): the Troll general's hits killed the player after all 26 sharks were eaten (the general at 1/30). regicide: first failing row 205 goKillGuardAtSecondForest-walk-toForests, run from the Lumbridge respawn; the death came at the end of leg 4 (about tick 4663): the Tyras guard fight ate all 12 sharks (lowest 20/70, OUT OF shark), then the tripwire snag and its poison took the player to 0. See CONTENT_BUGS.md, "Quest loop impact of the eat-delay port".

## Protection prayers block a wave nylocas's hit

- A wave nylocas's swing is blocked to 0 by the protection prayer of its style: Ischyros
  melee, Toxobolos ranged (Protect from Missiles), Hagios magic. The block shows as the
  block splat, a `hit_player` row with damage 0, hitsplat 26.
- The prayer is read on the swing tick (`~tob_nylo_swing` -> `~tob_nylo_wave_damage`,
  tob_damage.rs2). Pray before the swing, not before the hit lands.
- Measured: 0 of 52 matched swings landed, while the other two styles landed as before.
  The seam row `seam.nylocas_protect_blocks_wave_hit` reads three melee hits at 0 under
  Protect from Melee, with 11 thrown hits landed as the control.
- Explosions (`~tob_nylo_detonate`) and support collapses still ignore prayer.
- Source: wiki Protection prayers ("block all damage in most circumstances against
  NPCs"), and the nylocas styles from wiki Theatre of Blood/Entry Mode:157 and the
  infoboxes.

## Vasilias and prayer

- A Vasilias melee swing into Protect from Melee is now a 0 with its own `hit_player`
  row. Before seam6 it wrote no row at all.
- Her ranged and magic forms into the matching prayer roll 1-17
  (`^tob_vasilias_prayed_max`). That figure is sourced for Normal only but applies in
  every mode. Entry off-prayer is 1-24.
- Her prayer test and the waves' are one proc, `~tob_nylo_prayed_against` over
  `~check_protect_prayer`.

## Praying in the Nylocas room

- Pray the style of the aggro majority. A big counts two, and a melee copy counts only
  once it is within 2 tiles.
- A test-copy variant that did this switched 21 times in 395 ticks and blocked 57 of 100
  nylocas hits, with no spec row moved. It survived past click+410 to tick 482 (28 waves);
  the unprayed test always stopped earlier.

## Classifying a nylocas hit by the prayer in force

- Use every attack animation of the hit's slot in the 6 ticks before the hit, not only
  the last one. A thrown hit lands 1 or more ticks after its swing and the slot swings
  every 3, so "the last anim before the hit" attributes it to the next swing.
- Read hits only up to the end of the measured window. A later hit's swing may postdate
  the animation read.

## ::tobnyloskip

- Typed in a started Nylocas room, before or during the waves, it marks all 31 waves out.
  Vasilias then lands through the room's own clear and boss-tick path, 16 or more ticks
  after the last nylocas despawns.
- It skips a phase of the room, so it is a measurement cheat only: never in a room test.

## Verzik's three pools

- Her three phases are three pools end to end on one npc (`~tob_verzik_fresh_pool`). The
  npc's hitpoints and max are what is left of the current phase plus the whole pools
  still to come.
- The barrier gives P1 + P2 + P3: solo Entry 1300 = 300 + 400 + 600 (cache_npc_verzik.txt
  :430, :491, :552), solo Normal 6750. `::tobboss hp=N of M` reads 1300, then 1000, then
  600 as the phases fall.
- A P1 or P2 overkill is dropped at the phase change. Every heal (the Athanatos, the reds'
  absorb, the tornado, Hard's last heal) stops at the phase's own pool.
- Measure a phase pool from `::tobboss phase=N phase_hp=X of Y`. Never derive it from
  (total - P1) / 2 or from "the 400 floor".

## Verzik's thresholds are "at or below", compared whole

- An in-phase threshold compares left x 100 <= pool x pct, with no integer percent. The
  reds come at 140 of 400 and not at 141. The enrage comes at 120 of 600 and not at 121
  (before seam6, 125 enraged).
- For a threshold row, read `::tobboss phase_hp` right after the event. The hit before
  it must leave her above the threshold.

## ::tobboss in Verzik's room

- It adds `phase=` (0 P1, 1 P2, 2 P3, 3 the fall off the throne) and `phase_hp=X of Y`.
- `record=` stays normal in Entry because she wears the Normal forms. `room_mode` and the
  levels give her mode.

## Readouts that know every mode

- `::tobboss`, `::tobcrabdrop`, `::tobcrabkill`, `::tobcrabweaken`, `::tobcrabkillreal`,
  `::tobgates slugs=` and `::tobaddlevels` recognise every mode's Maiden bodies, crabs,
  slugs and reds. An Entry Maiden reads `record=entry` on her 100, 70, 50 and 30 bodies.
- `::tobaddlevels` prints at most four groups, followed by `shown=`.

## ::tobvzleft and the enr= field

- `::tobvzleft <n>` sets Verzik's current phase to exactly n hitpoints. A negative n is
  an overkill. A value above her current hp heals by `npc_statheal`, so it stops where
  her pool clamps. It performs damage, so it is for scratches only and is rejected in a
  room test.
- `::tobvz` gains `enr=`, the P3 enrage latch.

# Seam pass 7: presentation

Seam pass 7 (`matthew-mbp-m4-raid-b1-seam7`, triage `SEAM_TRIAGE_2026-10-03e.md`) is the
presentation pass of the Theatre of Blood, built from the asset inventory
(`docs/minigames/theater_of_blood/AV_INVENTORY.md`). No driver verb changed. The engine
seam adds three tick log row kinds (`sound`, `music`, `jingle`). Eleven content seams
landed in OSRS-Content a006110486: the per-room animations, graphics and doubled sounds, the
boss hit sounds, the treasure vault, the death and spectate flow with the lobby services,
the party board and scoreboard interfaces, and the music unlocks and title-card sound.
Eight seam rows prove them (`seam.ticklog_sound_rows`, `seam.ticklog_music_row`,
`seam.maiden_blackstorm_sound_once`, `seam.bloat_down_sounds_in_band`,
`seam.nylocas_presentation`, `seam.tob_boss_hit_sound`,
`seam.tob_death_cage_then_entry_restart`, `seam.tob_party_board_forms_a_party`). No tick,
damage, hitpoint or spawn rule changed. The fixers' reports and scratches are under
`build/seam_state/matthew-mbp-m4-raid-b1-seam7/`.

## The tick log's audio rows: sound, music, jingle

- The server writes one row per packet per player, at the place the packet is sent.
  `sound` is {pid, sound, loops, delay, coord, radius} plus `source`. `music` is {pid,
  track} plus `source`; track -1 means stop. `jingle` is {pid, jingle, length_ms}. The
  source rides in the row's label, and ticklog.lua names it `row.source`.
- Sound sources. `synth` is a plain `sound_synth` to the active player; coord and radius
  are -1. `area` is `~sound_area` / `.sound_area`: coord and radius are the proc's own
  tile and distance, and there is one row per player in range. `distance` is
  `~sound_within_distance`. `npc` is the engine's own npc defend or death noise
  (torirs_server_combat.c `npc_sound_nearby`): `row.npc_slot` and `row.npc_type` name the
  npc, coord is its tile and radius is 12. An npc's ATTACK sound is a script synth
  (combat.rs2 plays it to the target), so it is a `synth` row.
- Music sources: `script` (`midi_song`: tob_music.rs2, cutscenes), `region` (entering a
  mapped map square, torirs_server_music_regions.gen.h) and `login`.
- Filters: `t.ticklog.rows{kind = "sound", sound = <id>, source = "area", slot = <npc
  world slot>, pid = ...}`, `{kind = "music", track = <id>, source = "region"}`,
  `{kind = "jingle", jingle = <id>}`. `slot=` matches an npc sound row by its npc slot and
  is filtered in C, as for `hit_player`.
- Not driven by a conformance row: the `jingle` row (no ordinary action plays one; a ToB
  boss death and a quest completion do) and the `distance` source (no content in reach
  calls it; it shares the area code).

## A seq's frame sounds are not rows

- The client plays a seq's frame sounds itself, as each frame is crossed
  (src/world/world_cycle.c `World_EmitAnimFrameSound`), for players and npcs only (a
  spotanim's seq plays none). Assert a frame sound through the `npc_anim` row of its seq;
  assert a script sound through the `sound` row.
- `python3 tools/raid_gate/seq_frame_sounds.py <seq id or name> [--tsv]` prints a seq's
  frame sounds: the frame, the cycle and tick offset after the animation starts (30 client
  cycles per tick), the sound id and name, and loops/location/retain/weight. Example:
  `maiden_spawn` 14399 plays 11863 at frame 14, cycle 90 = the npc_anim tick + 3.
- A content `sound_synth` of a sound the seq already carries is the sound twice. Seam 7
  removed every such double in the rooms: Maiden's blackstorm 3293, Bloat's snore 3976 and
  shout 3545, every nylocas swing and detonation shriek, Sotetseg's melee 3540, and
  Verzik's pillar collapse, P2 melee, P3 autos, webs and powerblast cast. In those places a
  `sound` row of that id is now a regression.

## loops 0 is silent, and an area sound has a radius

- Fixed in seam8: `~sound_area` and `~sound_within_distance` send loops 1 now, so area
  sounds (Sotetseg's death-ball cast 3994 and tornado hum 4001 among them) are heard. A
  ported direct `sound_synth(x, 0, y)` is still silent; see seam8's "Area sounds are
  heard". Assert loops >= 1 where a test claims a sound is heard.
- An area sound reaches only players within its radius of its coord. Sotetseg's death
  ball (radius 10 around the projectile source) is not heard from the room's start tile,
  20 tiles south.

## Boss hit sounds

- Every boss form the player fights carries `param=defend_sound,<cache name>` in
  minigame_tob/configs/tob.npc: `tob_maiden_hit` 3999, `tob_nylocas_hit` 4020 (every
  Ischyros, Toxobolos and Hagios, and Vasilias), `tob_sotetseg_hit` 4019, `tob_xarpus_hit`
  4018 (standing form), `tob_verzik_human_hit` 4009 / `_vampire_hit` 4021 / `_spider_hit`
  4022 (P1/P2/P3). Bloat's `tob_bloat_hit` 3971 since seam8. The source is the cache name and the 281-row `<npc>_hit`
  defend_sound convention in LostCity's configs; no video audio was checked.
- The engine plays defend_sound on every hit the npc takes, a blocked 0 included
  (`ToriRSServer_CombatHitNpc`), to players within 12 tiles of the npc's SOUTH-WEST tile,
  not its footprint. A ranged player 9 tiles east of Maiden's 6x6 (6440,93) hears nothing;
  6436,93 does.
- Assert it through the tick log: `t.ticklog.rows{kind = "sound", source = "npc", slot =
  <world slot from t.ticklog.slot>, since = <a mark>}` gives one row per hit, on the
  hit_npc row's tick. Use a `since` mark: world slots are reused, so a whole-log read can
  find another npc's hit on the same slot (the first conformance run of
  `seam.tob_boss_hit_sound` did).
- `npc_combat/*.combat` is generated, and for any npc with a block in tob.npc the generator
  skips the ledger ("NOT COMPILED"). Put a ToB npc's combat sound in tob.npc. Only
  `verzik_phase{1,2,3}_{hard,story}` have no tob.npc block: their pin is in the ledger
  (source = authored) and in npc_anims.generated.npc. Do not run `gen_npc_combat.py --write`
  to refresh it: today that drifts 516 ledgers and drops 420 config blocks.
- Entry and Hard Verzik fight on the base records 8370/8372/8374, not on
  `verzik_phase*_story/_hard`.
- A private client build that prints every SYNTH_SOUND without touching source: `make -C
  src OPT=1 EMBED_SERVER=1 PLATFORM_OBJ_BASE=build_qd_<you> PLATFORM_TARGET=torirs_qd_<you>
  SDL_CFLAGS="$(pkg-config --cflags sdl2) -DTORIRS_LOG_ENABLED=1" SDL_LIBS="$(pkg-config
  --libs sdl2)" torirs_qd_<you>`, then run with `TORIRS_SOUND_DEBUG=1`. The shared OPT
  binary prints nothing for it; use the tick log.

## Maiden

- The blackstorm sends no script sound: `maiden_attack_special` carries 3293 (frame 3) and
  3234 (frame 17) in band. The blood throw still sends 3981 once per throw (its seq carries
  only 4002). `seam.maiden_blackstorm_sound_once` counts both.
- Left unplaced, each for its reason: `maiden_spawn` 14399, the rotated pools 3982-3984 and
  `maiden_transmog` 11958 came in with a later quest's `myq6` assets (id neighbourhood),
  and a video shows no entrance animation; `tob_maiden_blood_hit` 3989 has no binding;
  `tob_maiden_initial` 32972 and `tob_maiden_dead_remains` 32973 have no source (the map's
  36 pool-well pieces already show the flat pool the video shows after she fades). Live
  pools are graphic 1579.

## Bloat

- A falling-flesh hit shows `tob_bloat_stunned` 1575 on the player (254's model and
  colours with its own 3-tick seq 8089), not `stunned_shove` 254. The stun is still
  `^tob_bloat_hand_stun_ticks`. No tick log row or driver read sees a player's graphic: a
  `player_spotanim` kind would be needed (open).
- A Bloat down sends no server sound: `tob_bloat_sleep` 8082 carries the snore (frames
  26-66), the shout (frame 101, the stomp) and the footsteps. The room's server sounds are
  the flies (3945/3954/4016, with the projectile delay) and the hand's `tob_bloat_hit` 3971
  per player hit. `seam.bloat_down_sounds_in_band` pins it.
- The ambience 3288 is the soundid of the map-placed loc `tob_bloat_chamber`, played by the
  client's area sounds; a server row cannot assert it.
- Hard Bloat measurement recipe: `::god 1`, `t.raid.enter('tob', 'bloat', {mode =
  'hard'})`, the barrier and its confirm, stand on the room's first tile. Under `::god`
  every hit_player row reads 0, so a hand that hit the player is a `map_spotanim` 1576 on
  the player's tile that tick.

## Nylocas

- A small nylocas dies in two halves. The npc plays its one tick and despawns; on the
  despawn tick and tile a 43-scale graphic of its colour plays the rest:
  `tob_nylocas_death_<style>_standard` (1562-1564) for a kill, from the new
  `[ai_queue3,<small type>]` hooks (which still gosub `npc_default_death`), and
  `_detonate` (1565-1567) for a self-destruct. Bigs leave no graphic. Tick log check: a
  `map_spotanim` with the same tick and x,z as the small's `npc_free`.
- A big thrower's projectile is `tob_nylocas_rangedprojectile_sizemid` 1560; smalls throw
  1559 and Vasilias 1561. A projectile row's spotanim tells a big's throw from a small's.
- Vasilias' and Prinkipas' spawning form plays `top_spider_melee_spawn_noloop` 9030 on its
  spawn tick (`~tob_nylo_drop_in`): the tick log has `npc_anim` 9030 at the `npc_spawn` of
  type 10786 (Entry); `t.npc.state` reads 'anim 9030 frame 0' on the next client tick.
  `seam.nylocas_presentation` proves both halves.
- No Nylocas script calls `sound_synth` for a swing or a detonation any more; only the
  pillar bite and collapse sounds are script sounds.
- In a room, read the tick log one kind per call and with a `since` mark. The first
  conformance run of `seam.nylocas_presentation` read whole-log `npc_free`,
  `map_spotanim` and `npc_death` lists and ran out the 400000-instruction budget.

## Sotetseg

- The arena grid is `tob_sotetseg_plaintile` 33033 outside a maze and `darktile` 33034
  during one: 210 `loc_set` rows of 33033 on level 0 at the fight's first tick, 210 of
  33034 at each proc tick and 210 of 33033 on re-activation + 1. The realm stays 33034
  with path tiles 33035; filter a maze test's path rows by level 3. The floor turns plain
  at fight start, not on room entry (open: `~tob_watch_room` calls the sync only once the
  fight has started).
- Each rag adds `map_spotanim` 505 (`devious_explosion`, blert's rag marker) on the wrong
  tile and a script sound 3985 to everyone hit.
- The death ball's message is Jagex's: `<col=bf0000>A large ball of energy is shot your
  way...</col>`. A test that matched '<name> has discovered a large ball of energy' must
  match this.
- Script sounds a Sotetseg test can assert: 3994 at the death-ball cast (area, radius 10,
  silent today, see loops 0), 3947 per sharer when the death ball lands, 3996/4015 when an
  ordinary ball or ricochet lands, 3985 per rag, 4001 when a tornado spawns (area), 3233 per
  tornado hit, 3963 at the proc, 3286 at his death. 3540 (melee) is a frame sound of seq
  8138. The tornado's two sounds are placed from the cache's names ("red spiral", the
  wiki's "red vortex") only.
- Every raider plays seq 1816 `human_teleport_other_impact` at the maze proc (blert's and
  tob-qol's proc marker). No tick log kind reads a player's animation.
- A locked-clock A/B against HEAD is not valid after a content edit that adds procs: a
  recompile with more scripts reorders the rolls (HEAD plus two unused procs moved the
  first roll as the real change did). Compare against a HEAD + dummy-proc pack, or
  compare spec rows and distributions.

## Xarpus

- The acid pools are permanent for the fight, not the room. When he dies,
  `~tob_xarpus_dissolve_acid` removes every pool over four waves, collapse +4 to +7 (video
  B_gjVdmfOrY), each with a `loc_set -1` and, on the same tile and tick, `map_spotanim`
  1551 + its loc angle. Which pool goes on which tick is [M124]. Bound any 'pools never
  removed' spec row to ticks before the `npc_retype` to `xarpus_death*`.
- Player queue timing: a queue re-armed from inside the same player's queue script with
  delay 0 runs on the NEXT tick, delay 1 two ticks later. A queue armed from an npc's turn
  with delay N runs N ticks later.

## Verzik

- The P2 zap's final player gets spotanim 560 and seq 3170 (the killerwatt shock),
  delayed by the drawn ball's flight so it shows on arrival. The damage still lands on
  the cast tick (CONTENT_BUGS.md).
- The death bat is a new npc: `npc_add(verzik_death_bat)` at her tile on the death tick
  (`npc_changetype` on a dead npc was freed with her on the same tick, so 8129 never
  reached a client). It stands 5 ticks. The room clears on the tick it always did.
- The throne opens on her death: `tob_dungeon_verzik_throne_transforming` 32737 at local
  (31,36) with `loc_anim` 8108 on the death tick (the seat 32686 deleted, varbit 6400 to
  0), and the trapdoor `tob_dungeon_verzik_throne_door_opened` 32738 on the bat's last tick
  (+5). 8053 and 8108 are on the throne loc's framemap group 10275, not Verzik's, which is
  why 8108 looped when it was played on her. Read a seq's frame group before deciding
  whose it is. The vault's own trapdoor fallback (`~tob_vault_trapdoor`) waits while 32737
  stands and lays the door 6 ticks later only if it is still missing; on the final tree
  there is exactly one door, at +5 (closer run s7close_verzik_av, 27/27).
- The Athanatos hatches with 8079 `tob_spider_tank_spawn`; its heal is drawn as projectile
  1587 from it to Verzik (1588 when it was poisoned), and still lands on its timer tick.
- Yellows: a player alone on a pool gets 1597 and "The power resonating here protects you
  from the blast."; a struck player gets 1600 at height 96.
- The P3 death plays no script sound (3542 is the P2 death seq's). 4008, 1599, 3028, 3988
  and the Hard Mode pillar debris graphic stay unsourced; the debris keeps Bloat's 1570 as
  a commented stand-in.
- A dead npc cannot be retyped into its death form: `npc_changetype` in its
  `[ai_queue3]` is freed with it on the same tick. Spawn the death form as a new npc.

## The treasure vault

- Verzik's death no longer teleports out. The raid stays active (`varp5893_tob_active` 1),
  the loot sits server-side in the player's `tob_chests` inv (612) with non-stackables
  noted, and `varb11958_tob_should_have_loot` reads 1 (read it with `t.var.server`). The
  roll is unchanged.
- The trapdoor may appear up to 8 ticks after 'The throne collapses' (the throne
  transforms first): await `t.world.loc_near('tob_dungeon_verzik_throne_door_opened', 30)`
  before clicking it. It is an `[aploc1]` trigger, so `t.player.click_loc` works from the
  carpet.
- In the vault, `t.var.server('varb6450_tob_treasureroom_chest_0') == 2` is 'my chest,
  standard' (3 = mine with a unique, 0/1 = a teammate's). `t.player.click_loc(
  'tob_treasureroom_chest_loc0', 1)` opens `tob_chests` (`t.ui.await_open`). The grid is
  cc-created by clientscript 149 on `tob_chests:items`: `t.ui.invoke(<items sub>, 1)` takes
  one slot; `tob_chests:inventory` op1 is Take-all, `:bank` op6 Bank-all, `:discard` op6
  Discard-all. The opened chest becomes `tob_treasureroom_chest_open` (Search reopens it).
- `::tobvault` reads the chest inv, the loot flag, the five chest varbits, the party slot,
  the rare mask, the room (7 = vault) and the handle. `::tobvaultall` places all five
  chests for a visual check (measurement only).
- Leave by `t.player.click_loc('tob_treasureroom_teleportout', 1)` (lands at 3677,3219,0).
  Unclaimed loot is claimed at `tob_rewards_chest_lobby_multi` in Ver Sinhaza (same
  interface); a logout forfeits it.
- The war table in the vault stands inside the spectator enclosure and a raider cannot
  reach it; the strategy table is the raiders' board. Splits are carried room to room
  now, so the board shows every room's split.
- Every boss death plays the jingle `verzik_s_defeat` 250 (`~tob_room_cleared`, the one
  raider whose watchdog cleared the room hears it).

## Dying in the Theatre

- `[queue,player_death]` asks `~tob_death_begin` (tob_spectate.rs2) before the corpse
  falls: 0 not a Theatre death, 1 a death in a room still being fought (the cage), 2 a
  death in a won room (get up in place), 3 killed again while caged (re-caged, not
  counted). Every Theatre kind is a safe death: no gravestone, no Lumbridge.
- The cage: chat 'You have died. Death count: N.' (not 'Oh dear, you are dead!'), the
  room's cage tile, `tob_purgatory_stance` 8070, a walktrigger hold, the raider's orb at
  30 and the overlay text 'If your party survives the wave, you will respawn.'. State is
  bit 0 of `%varp6840_tob_died_in`; `::tobjail` prints it.
- Three ticks after a raider is caged, if every party slot reads 30 (caged) or 31 (left),
  Entry restarts the room ('You have failed.', 3 `tob_bandages` each, items kept) and
  Normal/Hard ends the raid ('Your party has failed.', keep-3/4 in the pack, the rest in
  retrieval service 11 at 100,000 coins, out to 3677,3219, 'A magical chest has retrieved
  ...'). If the room is won while a raider is caged, the cage lets go on that raider's
  next watchdog tick, at the room entry. `seam.tob_death_cage_then_entry_restart` proves
  the Entry path.
- The driver's death fence still matches only 'Oh dear, you are dead!'
  (`QD.player.DEATH_LINE`), so a Theatre death is caught only by the hitpoints-0 reading.
  Read `::tobjail` or the 'You have died. Death count:' line to assert a death.
- Obsolete since seam8: no room targets a caged raider any more (seam8's "The spectator
  cage").
- `::tobmate` adds a non-player party member with a full orb so a solo client can reach
  the cage-then-rejoin path (measurement only, never in a test).
- A leaver's orb now reads 31 ('left the raid').

## Lobby services and the supply chest

- The Normal/Hard supply chest opens the cache's `tob_midway_stores` (405); stock and
  prices are enums 1952/1953 (the mushroom potato is back; the regular potions replace
  the `br_` copies). `t.ui.await_open('tob_midway_stores')`. A test cannot click a stock
  slot yet (no comsubid in `t.ui.invoke`); `::tobstores` / `::tobstorebuy` measure the
  server half. The points label reads the balance since seam8 declared varp 1746
  transmit. Entry still hands out bandages.
- The onion rule is per player now (the raider's own died-in bits), not the team's count.
- Ver Sinhaza: `tob_surface_gravestone_chest` (Claim) unlocks the retrieval service (fee
  from the pack then the bank) and reclaims everything or nothing;
  `tob_surface_deposit_box` opens the deposit box. The in-room Vyre Orators' Resign goes
  through `~tob_leave`.
- A Theatre death in part of the Maiden's square used to be claimed as a Nex death
  (`gwd_in_bounds` maps the Nex box into any instance) and woke at 2900,5203; death.rs2
  skips the Nex hook for Theatre deaths.

## The party board and the scoreboard

- Read the board with `t.player.click_loc('tob_surface_notice_board', 1)`. The first
  reading is three pages (mesbox, the Entry/Normal choice, mesbox); later readings open
  `tob_partylist` directly. 'Make party' is `t.ui.invoke(t.ui.widget(
  'tob_partylist:myparty'), 1)` and opens `tob_partydetails`.
- Panel text: `tob_partydetails:frame` sub 1 = 'Party of <name>', `:mode` / `:size` /
  `:level` sub 0, `:action` sub 9 = Disband. List: `tob_partylist:frame` sub 1 =
  'Performers for the Theatre'; row k's cells are `tob_partylist:<kk>` subs 1, 3, 5, 6.
- The panel's first push draws only its last row (a client defect: a RUNCLIENTSCRIPT that
  GOSUBs an unloaded proc never finishes when more follow). Press the panel's Refresh
  twice before reading the member row.
- Obsolete since seam8: the server latches sub 0, so list Refresh, the list's first row
  and panel Back act on their own row.
- Mode: invoke `tob_partydetails:mode`, then `t.chat.play({'options', 'choose:Normal
  Mode.'})`. Preferred Size/Level open a count prompt `t.chat.count` cannot answer while
  the panel is open.
- The door by click needs a party: without one it says 'You need to be in a party to
  enter the Theatre...'. The leader gets the ready check 'Is your party ready? Members: N.
  Mode: X.' and `t.chat.choose("Yes, let's go!")`. `::tobmode` and `t.raid.enter` are
  unchanged.
- `::tobparty`, `::tobscoreboard` and `::tobperformance` are readouts and openers. A lobby
  party takes one of the 32 instance slots, so the raid built after a party exists gets
  handle 2.
- `seam.tob_party_board_forms_a_party` proves the board, the list, Make party and the
  leader record.

## Music and the title card

- `~tob_music_play` unlocks, names and plays: it reads the track's cache music row,
  calls `~music_unlock`, sets `music:now_playing_text`, then `midi_song`. A test reads an
  unlock as `t.var.varp('varp1681_musicmulti_18')` (bits 8, 10..22) and the name as
  `t.ui.text('music:now_playing_text')`.
- `~tob_music_vault` (582, The Curtain Closes) and `~tob_music_boss_defeated` (jingle 250).
  `::tobmusic 1|2` plays them (scratch only).
- The room title card plays `tob_transition_card` 3952 on the tick the blood opens
  (`~tob_title_show`): one `sound` row per card, none on `~tob_title_spread`.
- 'Welcome to the Theatre' 556 is the engine's region track for the lobby square. Since
  seam8 the engine's region unlock writes the musicmulti word, so the track unlocks on
  entry and the vault prints one unlock line.

## Proving a presentation change without the shared tree

- A before/after on content: copy server/scripts (minus build*, png, bmp) into a mirror
  root whose other entries are symlinks to osrs239-content, compile one pack with the HEAD
  version of the file and one with yours (`build_opt/sscompile --src <root>/server/scripts
  --out <pack> --content-root <root>`), and run with `TORIRSSERVER_SCRIPTS=<pack>
  TORIRSSERVER_ALLOW_STALE_SCRIPTS=1`. Both packs carry the same snapshot of other edits.
  Run `ss_allocate` with `--check` there, or it writes .alloc files into the shared pack/.
- A regression run of a quest must pass `--no-publish`, or it overwrites OSRS-Content's
  selftest/quests/<dir>/play.
- Tick log proof of a presentation fix: npc_spawn + npc_anim on one slot and tick (a spawn
  animation), projectile rows by spotanim, loc_set rows (a loc chain and its timing),
  sound rows with a control synth that must be present. A player's own spotanim or
  animation and a loc_anim are rows since seam8 (`player_spotanim`, `player_anim`,
  `loc_anim`).
- The corpus videos under build/frames/<id>/full.mp4 are video only (no audio), so a
  sound cannot be sourced from them as downloaded.

## The run name decides every roll (reported by the quest loop, 2026-10-03)

The player's random stream is seeded from the account name
(`src/torirsserver/torirs_server_save.c:268`), and the account is the run's name: with
`run.py --name X` (or a test id) every roll of that run is fixed. Two runs under one name
are ONE sample, not two. A statistical measurement (a turn rate, a spawn chance, a dodged
throw's cap) needs runs under different names, and a row that depends on a rare roll can
be searched for by changing the name, never by a cheat. Say in the row's detail how many
names the figure covers.

# Seam pass 8: what the presentation spec pass found

Seam pass 8 (`matthew-mbp-m4-raid-b1-seam8`, triage `SEAM_TRIAGE_2026-10-03f.md`) fixes what
the Theatre of Blood presentation spec pass measured. No driver verb changed. The engine
seam adds four tick log row kinds (`player_anim`, `player_spotanim`, `loc_anim`,
`npc_say`), fixes the region music unlock and latches a sub-0 pause button. Ten content
seams landed in OSRS-Content 26ba1bd604: the spectator cage in every room's hunts, the
death forms and doubled death sounds, Bloat's defend and hand sounds, the Sotetseg tornado
and Verzik's Entry/Hard records, the reward tables and combat achievements, the HUD
domains and the recorders' chat lines, the lobby shop and supply chest, and the area
sounds. Nineteen seam rows prove them. The fixers' reports and scratches are under
`build/seam_state/matthew-mbp-m4-raid-b1-seam8/`.

## The tick log's presentation rows: player_anim, player_spotanim, loc_anim, npc_say

- `player_anim` is {pid, seq, delay}. It is written only when the seq wins the priority
  gate. `anim(null)` is seq -1, and a `p_animprotect` refusal writes no row.
- `player_spotanim` is {pid, spotanim, height, delay}, from `spotanim_pl`.
- `loc_anim` is {coord, loc, shape, angle, seq}. The coord is the active loc's south-west
  tile.
- `npc_say` is {slot, type, coord}, and `row.text` holds the whole line (up to 79
  characters).
- Filters: `loc=` (a loc_set or loc_anim row's loc id) and `text=` (an npc_say whose text
  contains it, plain match). `seq=` and `spotanim=` now match the player and loc kinds
  too. `slot=` matches npc_say in C.
- Raid figures now in the log: Bloat's fly impact is player_spotanim 1569 and its hand
  stun is 1575 at height 92. Sotetseg's are player_spotanim 1605 and 1608, plus
  player_anim 1816 on the maze proc. Xarpus's loops are loc_anim 8065 on loc 32743
  (exhumed) and 8068 on loc 32744 (acid).
- No door animates in either content tree: a door is a loc_change, which writes a
  `loc_set` row. Every ordinary `loc_anim` caller is an obstacle. The conformance row
  uses the Brimhaven rope swing (loc 23568 at 2705,3209: loc seq 497 and player seq 751
  on the same tick).
- Ambient npc lines (cows' Moo, Al Kharid warriors' call for help) never fire in this
  engine. Hans's 'Help! Help!' is the deterministic subject (hans.lua's approach, then
  'I have come to kill everyone in this castle!').
- Rows: `seam.ticklog_player_presentation_rows`, `seam.ticklog_loc_anim_row`,
  `seam.ticklog_npc_say_row`.

## Region music unlocks its musicmulti word

- A music row's (variable, bit) pair names a musicmulti WORD by index, 1..27, never a
  varp id. `ToriRSServer_MusicVariableVarp` holds clientscript 7305's table (18 -> varp
  1681). Ver Sinhaza unlocks 'Welcome to the Theatre' as bit 8 of `varp1681_musicmulti_18`.
  `varp18_musicplay` no longer reads 256 in the lobby, and the vault prints one unlock
  line, not two. Row: `seam.music_region_unlocks_its_musicmulti`; C selftest stanza
  'region music unlocks the musicmulti word'.

## A sub-0 pause button acts on its own row

- RESUME_PAUSEBUTTON now latches sub 0 (the client sends 0xffff, read as -1, for 'no
  sub'). The party list's Refresh and first row and the party panel's Back act on their
  own row: press them and assert as with any other button. The seam7 note 'never press a
  sub-0 button' is obsolete. Row: `seam.pausebutton_sub_zero_latches`.

## Area sounds are heard

- `~sound_area`, `~.sound_area` and `~sound_within_distance` send loops 1. An OSRS client
  drops an effect with loops 0 (Kronos184 Message.java:175; the mixer gets loops - 1;
  ours is rs_audio.c:124). LostCity's 0 was right only for its 2004 client.
- About 104 ported direct `sound_synth(x, 0, y)` calls are still silent: 99 under
  quests/, plus prayer.rs2:395 (every prayer's activation sound) and gnome_gate.rs2:66.
  Where a test claims a sound is heard, assert `row.loops >= 1`.
- An ordinary area sound for a test: `::spawn eadgar_storeroom_guard` snores
  troll_snore 869 (radius 5) every 8-16 ticks.
- Hearing a sound in the client needs a private binary built with
  `BUILD_DIAGNOSTIC_CFLAGS='-fomit-frame-pointer -DTORIRS_LOG_ENABLED=1'` (the shared OPT
  binary compiles TORIRS_LOG out), run with `TORIRS_AUDIO_TRACE=1`. Build it cold:
  objverify hashes sources, not flags. Row: `seam.area_sound_plays_once`.

## The spectator cage: no room targets a caged raider

- Every room's hunts skip a raider in the cage (`~tob_jailed`, bit 0 of
  `%varp6840_tob_died_in`): Maiden's blackstorm, blood throw, facing and sweep; Bloat's
  flies, spread, falling flesh, stomp and camera shake (`~tob_bloat_target`); every
  nylocas swing, detonation and pillar collapse, and Vasilias (who swings nothing while
  only a caged raider is in range); Sotetseg's autos, death balls, ricochets, hits in
  flight, the maze proc's stun, teleport and runner choice, the rag and the tornado
  (`~tob_sote_targetable`); Xarpus's spit, bounce, sweep, splash, delayed poison and stomp
  (he draws only from raiders in the game and not caged); every Verzik hunt
  (`~tob_verzik_target`). The seam7 note 'Bosses still target a caged raider' is
  obsolete.
- A Sotetseg runner ALIVE in the shadow realm is not caged. One who dies there is caged in
  the arena, and the realm is let go without walking them out of the cage.
- Prove a room's cage with `::tobmate` (a party member who is not a player) before the
  barrier, then `::die` (or `::setlevel hitpoints 10`, `defence 1` and stand), then poll
  `::tobjail` until `jailed=1` and count rows from that tick. A scratch can force the
  flag with `::setvar varp6840_tob_died_in 1` plus `::tobmate`. All of these are
  measurement cheats, never in a room test.
- Since seam9 every room releases a caged raider when the room is won (seam pass 9, "A
  caged raider comes out when the room is won").
- Rows: `seam.tob_maiden_cage_and_one_death_anim`, `seam.bloat_hand_sound_and_cage`,
  `seam.nylocas_cage_skipped_support_unanimated`, `seam.tob_sotetseg_cage_not_targeted`,
  `seam.verzik_entry_forms_cage_and_death`.

## Maiden: one death animation, and the death timeline

- The killing blow is K. `maiden_death_a` 8093 plays ONCE at K+1 (the engine's
  `npc_death_step` plays the body's death_anim), and it carries sounds 3942/3955/3958 in
  band. Since seam9 the dying_a retype is at K+1 (it was K+3), dying_b with 8094 at K+5,
  and npc_free at K+9 (seam pass 9, "Maiden's death forms are on the recorded ticks").
- A solo bow cannot finish her from 1%: the leaks out-heal it. Use `::tobmaidenpct 0` for
  a measurement; a room test kills her for real.
- Thrown pools vs trails: since seam9 a thrown pool is graphic 1579 alone and loc 32984 is
  a blood spawn's trail only (seam pass 9, "Maiden's thrown pools are a graphic").
- `test/raids/tob_maiden.lua` must name the Entry records (`tob_maiden_100_story` ...
  `_30_story`) since seam5. With those four symbols the committed file drives the whole
  Entry room 64/64.

## Bloat: defend sound, hand sound

- `tob_bloat_hit` 3971 is Bloat's defend sound (tob.npc), played on every hit he takes.
  Bloat's `npc_combat/t/tob_bloat*.combat` ledgers are not compiled: tob.npc states the
  three Bloat blocks and `tools/gen_npc_combat.py` skips an npc with an authored block, so
  a Bloat sound or animation goes in tob.npc.
- The falling flesh plays one `barbassault_splat` 3308 per volley to every raider not in
  the cage, on the landing tick (source Near-Reality PestilentBloat.kt:227-228, grade E
  M163). Read it as `sound` rows with sound=3308 on the map_spotanim 1576 tick.
- Bloat's cage tiles are out of fly line of sight, stomp reach and spread reach, so the
  natural cage hit does not happen solo; the forced flag is the reproduction.
- Rows: `seam.bloat_hand_sound_and_cage`, `seam.bloat_defend_sound`.

## Nylocas: death timings and the support npc

- Measure a death offset with REAL hits, never `::kill`: `::kill` lands on the npc's own
  step tick (arrive delay 2). With real hits bigs read +1/+6 standing and +2/+7 walking.
  Since seam9 smalls read +1/+2 walking or standing (seam pass 9, "A small Nylocas dies on
  a fixed two ticks").
- Sound 4020 `tob_nylocas_hit` is the defend sound on every hit, lethal or not. A `::kill`
  log shows it only on death ticks because the kill is the only hit: read sound rows
  against `hit_npc`, not `npc_death`.
- The support npc (8358/10790/10811) is an invisible health carrier with no animation of
  its own (tob.npc `param=defend_anim,null`, `death_anim,null`): a test expects no npc_anim
  rows on it. The visible collapse is loc 32863 playing 8074 (a `loc_anim` row) between
  loc_set 32863 at collapse +3 and the rubble 32864 at +7.
- `::tobnylobreak` collapses a support with two latched chewers on demand (measurement
  only).

## Sotetseg: the tornado is the mode's record

- The maze tornado is `tob_sotetseg_creeper` 8389 (Normal), `_story` 10866 (Entry) or
  `_hard` 10869 (Hard), picked by `~tob_sote_tornado_type`. Assert npc_spawn/npc_tile by
  the mode's type, not 8389. Row: `seam.tob_sotetseg_tornado_mode_record`.
- Tick log rows read with `since=<row serial>` EXCLUDE that row: to count a proc and a
  re-activation, take a `ticklog.mark` before the proc and read since the mark's serial.

## Xarpus: the death screech, the spit landing

- The death screech 3549 is now only the frame sound of seq 8063. Assert it through the
  npc_anim 8063 row on the collapse tick, never through a sound row.
- A spit lands at spit + floor(end_cycle / 30) (3 or 4 by distance; the grade E spec says
  2, M71). Since seam9 the landing is Xarpus's own queue and does not wait on a dying
  target (seam pass 9, "Xarpus's spits and orbs land on their own tick").
- With a `::tobmate` stand-in a solo raider takes every spit (a stand-in is never a
  target); before, about half were lost on the stand-in's slot.

## Verzik: Entry and Hard records

- In Entry and Hard she wears her mode's records: seated 10830/10847
  (`verzik_initial_story` / `verzik_initial_hard`), P1 10831/10848, P1->P2 10832/10849, P2
  10833/10850, P2->P3 10834/10851, P3 10835/10852, bat 10836/10853; crabs 10841-10843 /
  10858-10860, Athanatos 10844/10861, Matomenos 10845/10862, tornado 10846/10863. Webs and
  pillars keep the Normal ids. A test talks to and attacks the mode's symbol and matches
  tick log types by the mode's ids. `t.raid.enter` knows the mode forms (raid.lua).
- The seated shell becomes the mode record on its first timer tick, so
  `t.npc.nearest("verzik_initial")` answers no_row in Entry and Hard.
- Entry levels per phase are the records' own and `::tobboss` shows them. P1 is att 180,
  def 10, str 150, rng 180, mag 180. P1->P2 and P2 are 200/120/150/180/180, and P2->P3 is
  the same. P3 is 180/120/200/180/180. Pools are unchanged (solo 1300 = 300 + 400 + 600).
- P2 -> P3: npc_anim 8118 on the P2 body at the phase event E; npc_retype to the 8373
  family at E+2 with 8119 on the same tick; the P3 id at E+6; the first P3 attack at E+12.
- The P3 crab special is npc_anim 14406 plus a regular attack under it (a 1593/1594
  projectile on the same tick, or the melee hit). The green ball is npc_anim 8124 or 8125
  plus projectile 1598 on the same tick, with no second projectile.
- Yellows: no `sound` row for 4000 (seq 8126 frame 59 carries it). Each pool's graphic
  1595 is several map_spotanim rows on the cast tick, delays 0,183,237 (Normal/Entry, 14
  ticks) or 0,183,366,417 (Hard, 20 ticks): count pools by distinct coord or by delay 0.
- The P3 death plays 8128 once, at death +1; the bat spawns at death +3 playing 8129.
- The P2 lightning's `hit_player` lands at cast + end_cycle/30 (+3 at the usual distance),
  with its ball and shock, not on the cast tick.
- `::tobvz` 'form' and `::tobmelee` (tob_selftest.rs2) still compare Normal types only:
  they read form 0 / no boss for an Entry or Hard Verzik.

## The vault: one team roll, the real tables, `::tobrewards`

- The unique roll is one per raid for the team (10/91 Normal, 10/77 Hard, none in Entry),
  latched on Verzik's room (instance regs 126/127: 0 not rolled, 1 missed, 2 hit) and
  given to one raider owed a chest by a draw weighted by `~tob_board_score`.
- The common table is the wiki's 29 rows over 30 slots, rolled three times, paid at the
  mode's share: Entry 20, Normal 100, Hard 115, 130 inside the overall-time target, floored
  at 1. Tertiaries: the elite clue (2/6/7 of 50), Lil' Zik (1/650 Normal, 1/500 Hard, none
  in Entry), and the Hard kits and dust inside the target.
- `::tobrewards` is a read-only readout of every table through the procs the vault rolls
  with, and inside a raid the latch, a dry allocation and the chest checked against its
  rows ('tobrewards chest <name> xN [noted] want a-b'). A table proof is this readout,
  never thousands of raids. 'want a-b' is ONE roll's range: three rolls can stack on one
  row. Row: `seam.tob_reward_table`.
- `%varp6826_tob_completions` counts Normal and Hard only (Entry reads 0 -> 0). An Entry
  completion awards CA 397 (and 396 under 1700 ticks of challenge time); Hard awards 381
  inside the target.
- The perfect-room tasks (243-249) are awarded on a raid resumed at Verzik (`t.raid.enter`
  / `::tob 6`): their bits start set and rooms 1-5 never ran.
- A `t.await` level function that calls `t.world.loc_near` raises 'attempt to yield across
  a C-call boundary': poll with `t.ticks(1)` in a loop instead.
- In rs2 content a '<' inside a string literal opens an interpolation and runs the parser
  to EOF ('expected ')' after arguments to 'append'' at the last line).

## The HUD and the chat lines the recorders parse

- Varbit 6448 is the boss's hitpoints as a permille of its pool (0..1000, rounded up), and
  6449 is always 1000. A full pool reads exactly 1000 at the fight start. 6440 reads 2 for
  the whole stay in the raid from the first arrival, and 1 in the lobby with a party.
  6447/6448/6449 read 0 in a room whose barrier is uncrossed. Read them with
  `t.var.varbit('varb6440_tob_client_partystatus')` and the like.
- The chat lines, raw as `t.msg` sees them, tags included:
  - door: 'You enter the Theatre of Blood (Entry Mode)...';
  - lobby member: '<leader> has entered the Theatre of Blood (Entry Mode). Step inside to
    join him...';
  - room end: "Wave 'The Maiden of Sugadinti' (Entry Mode) complete!<br>Duration:
    <col=ff0000>0:13</col>";
  - Verzik: "Wave 'The Final Challenge' (Normal Mode) complete!<br>Duration:
    <col=ff0000>m:ss</col><br>Theatre of Blood completion time: <col=ff0000>m:ss</col>",
    then 'Theatre of Blood total completion time: <col=ff0000>m:ss</col>'.
  Strip `<[^>]*>` to compare with a recorder regex. Gone: 'The way onward is open', 'You
  enter the chamber of', 'You follow the party', 'You join the party already inside' and
  'You have completed the Theatre of Blood.'. 'The fight begins: <room>' stays.
- The boss-defeated jingle 250 and the wave line reach every raider once.
- A logout with the room started and not cleared is a death (team deaths +1, orb 30). On
  return the raider is caged if anyone is standing, otherwise the mode's wipe runs. A
  hallway logout keeps the room resume. A driver relog re-boots the embedded server, so
  this is measured with `::toblogoutdrill`, which prints 'toblogoutdrill logout ...' and
  'toblogoutdrill reenter ...' lines (measurement only).
- `::tobmode` (`t.raid.enter`) without `::tobout` (`t.raid.leave`) first carries the last
  room's party into the new build and joins the raider again (party [me, me]). A scratch
  that lands twice must call `t.raid.leave` between the landings.
- Sotetseg's arena floor is plaintile 33033 from room entry: 210 loc_set rows two ticks
  after the arrival, barrier uncrossed. Read it with
  `t.world.loc_near('tob_sotetseg_plaintile', 40)` before the fight.
- Rows: `seam.tob_hud_status_and_wave_line`, `seam.tob_logout_in_fight_is_death`.

## The lobby: the Stranger's shop, the escape crystal, the supply chest

- A chest band read must be DEATHLESS: a fresh 10-hp character dies at Bloat's fight tile
  inside the `::kill` window, and a death moves the band. Set `::setlevel hitpoints 99`
  and assert no 'You have died' line since the room was built. A Hard chest pays the
  Normal band less 4 (deathless 6..9).
- The supply chest after Bloat (loc_add at 6405,97) is not seen by `t.world.loc_near` from
  the fight tile 6439,95: `goto_tile 6406,97` first, then `click_loc
  tob_midway_chest_closed`.
- The store label `t.ui.text('tob_midway_stores:points_text')` reads 'Points Available: N'
  and equals `::tobstores points=` (varbit 6460 on varp 1746, now declared transmit).
- The party list's 'Make party' / 'My party' label is `t.ui.text('tob_partylist:myparty',
  9)`. It reads 'Make party' once varp 1740 is -1, which the board writes the first time
  it asks.
- The Mysterious Stranger: stage the Trade form with `::setvar
  varb15607_tobquest_stranger_vis 1` (A Night at the Theatre's talk that sets it is not
  wired). Then `t.shop.open('tob_stranger', 3, 'tob_stranger_shop')` and
  `t.shop.buy('tob_teleport', 1)` cost 75,000. The crystal's Teleport is
  `t.player.inv_op('tob_teleport', 1)`: inside the Theatre it lands on 3677,3219 with
  tobstate active=0; outside it answers 'Nothing interesting happens.' and keeps the
  crystal.
- `::tobstrangershroud` opens the Sinhaza shroud reward page (needs 100 completions and no
  shroud held). It is a test affordance until the talk offers 'What do you have for me?'.
  The cape page is an objbox: use '*' in `chat.play`.
- A content-allocated shop inv (an id in pack/inv.alloc, not in all.inv.compack) needs
  `size=` in its .inv, or `shop.open` answers 'shopmain is up but <inv> carried no stock'.
- Rows: `seam.tob_partylist_button_reads_mycontroller`,
  `seam.tob_stranger_sells_escape_crystal_and_it_leaves`,
  `seam.tob_hard_chest_pays_fewer_and_label_reads_it`.

## Proving a change to a file another seam owns

- Build a private content root: HEAD server/scripts plus only your own block (seam8
  `lobby/mk_tob_rs2.py`, `bloat8/pack.sh`), and keep a second private root at HEAD for the
  before runs; run with `TORIRSSERVER_CONTENT` / `TORIRSSERVER_SCRIPTS`. Strip
  server/scripts/selftest down to its sources (the evidence PNGs are 15 GB). run.py still
  rebuilds the shared pack first, so a concurrent fixer's broken edit can refuse the run:
  retry once it lands.
- `make torirsserver` compiles the server sources on every call; check the binary carries
  the change (`strings <bin> | grep player_anim`) before trusting a selftest count.
- Every regression run of a quest passes `--no-publish`. Two seam8 runs without it
  rewrote OSRS-Content's selftest/quests/quest_cook and quest_druid evidence.

# Seam pass 9: what the client draws, the death stages, the cage on a win

Seam pass 9 (`matthew-mbp-m4-raid-b1-seam9`, triage `SEAM_TRIAGE_2026-10-03g.md`) adds the
reads the fifth room pass could not take: the pose an npc draws while it stands or walks,
the sequence a graphic or a loc plays, and a loc's looping sound. The engine seam fixes
Maiden's death forms, the small Nylocas death, and the pool loc, and adds an `npc_heal`
tick log row. The content seam releases a caged raider when any room is won and lands
Xarpus's spits on his own clock (OSRS-Content 24198b54ed). No driver verb changed: the new reads are fields on rows
the verbs already return. Nine seam rows prove them. The fixers' reports and scratches are
under `build/seam_state/matthew-mbp-m4-raid-b1-seam9/`.

## An npc standing or walking reads anim_id -1: read pose_anim

- Every npc row (`t.npc.state`, `t.npc.nearest`, raid.lua's boss read) carries
  `pose_anim` / `pose_frame`. That is the client's secondary (locomotion) track as it is
  drawn. `pose_kind` says which slot of the npc's idle set it is: `ready`, `walk`,
  `walk_back`, `walk_left`, `walk_right`, `run`, `turn`, `other` or `none`.
- The row also carries `ready_anim`, `walk_anim`, `turn_anim` and `run_anim`: the idle set
  the client holds for that entity now. -1 means an empty track or slot. `pose_kind`
  `none` means `pose_anim` is -1.
- Trap: `anim_id` is the ACTION track only. It reads -1 the whole time an npc stands or
  walks, because the server sends no SEQUENCE op for a ready or walk loop. A test that
  reads `anim_id` for an idle or walk row measures nothing.
- Filter on `pose_kind`, not on "the tile did not change". Between ticks a walking npc's
  tile does not change either. Bloat walks almost all the time: ready 8080 was drawn on 1
  of about 770 reads, and a tile-unchanged filter read his walk 8081.
- After `npc_changetype` the idle set is the NEW type's (`World_NpcSetType`). A cache
  lookup of the spawn symbol answers the old type forever. Vasilias reads ready 8002 /
  walk 8003 in her first form and ready 7988 / walk 7987 after the retype to 10788.
- A standing npc with no turnanim turns in its walkanim. That reads `pose_kind` `turn`
  (canafis_man1: turn = walk 819).
- The pose track keeps stepping under an action seq. Whether it shows depends on the
  action seq's walkmerge and priority, so `pose_anim` with `anim_id` >= 0 does not prove
  the pose is visible.
- Rows: `seam.npc_pose_reads_the_drawn_track`, `seam.npc_pose_follows_a_retype`.

## A graphic's own sequence: seq on projectile and spotanim rows

- `t.world.projectiles` and `t.world.spotanims` rows carry `seq` / `seq_frame`: the
  sequence the graphic's scene element is playing. Wind strike projectile 91 plays 659.
  Bloat's falling flesh 1570-1573 plays 8088.
- They read -1 / -1 while the seq is loading and before the element exists. Wait for
  `seq >= 0` instead of reading the first row.
- Row: `seam.element_seq_projectile_and_static_loc`.

## A loc's animation and looping sound: seq and ambient_* on loc rows

- Every loc row (`api_drive.locs`, and `t.world.hazard_at(x, z, level).locs`) carries
  `seq` / `seq_frame`: the sequence the loc's element plays. It reads the same for a
  map-placed loc whose record has an anim and for a server `loc_anim`. It is -1 for a
  static loc, and -1 again once a one-shot loc anim has run out.
- The row also carries `ambient_sound`, `ambient_range` (inaudible distance in tiles),
  `ambient_inner` (full-volume radius) and `ambient_random` (number of random
  alternatives): the looping area sound the client REGISTERED for that placement. They
  read -1 (random 0) when the client registered nothing.
- Bloat's room reads: tob_bloat_chamber seq 8086, ambient 3288 range 5; tob_arena_barrier
  seq 7929, ambient 3139 range 1; the chain hooks seq 8087 and silent. A Lumbridge tree
  reads seq -1 and no ambient.
- Trap: `t.world.loc_near` returns the pointer's own row (id, tile, level) without these
  fields. Take the tile from it, read `hazard_at(tile).locs` and match on `loc_id`.
- Row: `seam.loc_and_graphic_seq_in_bloat_room`.

## npc rows do carry size

- `t.npc.state` rows carry `size` (raid seam4, still true). The Xarpus author's report
  that it is missing came from reading the wrong row. Xarpus reads 3 in his static form
  and 5 in his fighting form.

## The tick log's npc_heal row

- `t.ticklog.rows({ kind = "npc_heal" })` returns { slot, type, amount, hitpoints, base,
  source }. A row is written by `npc_statheal` or `npc_statadd` on hitpoints, and only
  when the level actually rose. `source` is the healing script's name.
- ToB sources: `[proc,tob_maiden_heal_found]` (+10..+22 a tick as blood spawns reach her),
  `[proc,tob_verzik_athanatos_tick]` (+10 every 5 ticks while an Athanatos stands, for
  example spawn t239 then heals t244/249/254), `[proc,tob_verzik_blood_spell]` and
  `[proc,tob_verzik_absorb_reds]`.
- Room setup writes heal rows too (`[proc,tob_boss_set_hp]`,
  `[proc,tob_verzik_spawn_pillars]`). Filter by `source`, or by slot and the window you
  measure.
- Ordinary npc: a Slayer Tower banshee writes +1 `[proc,slayer_after_player_hit]` on
  every landed hit, and takes damage only while slayer_earmuffs are worn.
- Row: `seam.ticklog_npc_heal_row`.

## Maiden's death forms are on the recorded ticks

- K is her `npc_death` row. The retype to dying_a (Entry 10818) is at K+1, the fade
  (10819) at K+5, and `npc_free` at K+9. `maiden_death_a` 8093 is sent once, at K+1. A
  ledger row for `maiden.av.death.dying_a_form_ticks` reads 4 (it read 2 before seam9).
- How: her living records state `death_delay=0`, and the engine runs `[ai_queue3]` on the
  death animation's tick for that value. No other npc record states 0.
- To get a death on cue in a measurement scratch, use `::tobmaidenpct 0`. With
  `::tobmaidenpct 2` and arrows, her blood spawns heal an Entry Maiden back faster than the
  arrows chip. A room test still kills her with its own hits.
- Row: `seam.maiden_death_forms_on_recorded_ticks`.

## Maiden's thrown pools are a graphic, not a loc

- A thrown blood pool is a `map_spotanim` row with spotanim 1579 on its landing tick. No
  loc 32984 is placed under it. A test that finds pools through `loc_set` 32984 or
  `hazard_at`'s loc rows sees none ("0 splats from loc add to loc removal").
- Key pool rows on map_spotanim 1579. Loc 32984 rows are blood-spawn trails only.
- `test/raids/tob_maiden.lua`'s `spec.maiden.pool_life` row still keys on loc 32984; the
  next maiden author re-keys it.

## A small Nylocas dies on a fixed two ticks

- A small the player kills plays its death animation at `npc_death` + 1 and is freed at
  + 2, walking or standing (blert's table). A ledger no longer sets walking smalls aside.
- A big that was walking still stops first: anim + 2, free + 7 (standing + 1 / + 6).
- How: the engine skips the arrive delay for a record stating `death_delay` under 2. Only
  the 18 small records (1) and Maiden's living records (0) do.
- Row: `seam.small_nylocas_dies_without_arrive_wait`.

## A caged raider comes out when the room is won, in every room

- A raider who dies with a living party member (`::tobmate` in a scratch) is caged. When
  the boss dies they are back at the room entry within about 6 ticks, and `::tobjail`
  reads `jailed=0 ... cleared=1 tile=<room entry>`. Before seam9 only the Maiden's room
  did this. Source: wiki Theatre of Blood/Strategies, "If the room is successfully cleared
  by the remaining players, those who have died will be reunited with the rest of their
  team."
- Row: `seam.tob_cage_released_on_room_win`.

## Xarpus's spits and orbs land on their own tick

- A spit lands (map_spotanim 1556, loc 32744 and sound 4005 on its tile) on spit tick +
  floor(end_cycle / 30). A thrown orb lands on throw tick + floor(end_cycle / 30). Read
  end_cycle from the projectile tick log row: do not assume +3. A spit's end_cycle depends
  on distance (107 is 3 ticks, 122 is 4).
- A target who is dying or caged no longer delays a landing. Landings still in flight when
  Xarpus dies are dropped.
- Orb timing moved from mostly throw + f + 1 to throw + f (the ENCOUNTER_TIMING 1.2/1.4
  rule). A spec row written against the old orb timing moves by one tick.
- To pair a splat with its spit, match the exact due tick. For a stationary target every
  spit hits the same tile, so "the first 1556 on the aimed tile" misattributes.
- Tell a spit from an orb by its source tile (his mouth is the source of the fight's first
  spit), not by "a 1555 projectile on an npc_anim 8059 tick". Orbs are often thrown on a
  spit tick.
- The landings now run in the npc phase, so a deterministic run's random stream differs
  from earlier ledgers from the first landing on. Re-measure instead of comparing with a
  pre-seam9 tick log.
- Row: `seam.tob_xarpus_landing_ignores_a_dying_target`.

# What the sixth ToB room pass tripped on (matthew-mbp-m4-raid-b1-rooms-tob)

The sampler sent back tob_maiden and tob_sotetseg (both "green, coverage FULL"). The
reasons, and the small facts the reviewers reported, follow.

## "Record-driven, so no row" is not a measurement: read pose_anim

- `tob_maiden.lua` wrote `measured 8090` for `maiden.av.idle.seq` because `anim_id` read
  -1 and no npc_anim row carried 8090. It wrote `measured 8101,8102` for the blood
  spawn's walk/idle the same way. Both are the spec value written from an absence.
- Since seam9 the npc row carries `pose_anim` / `pose_kind` / `ready_anim` / `walk_anim`
  (seam9, "An npc standing or walking reads anim_id -1"). Measure a stand from a read
  with `pose_kind == 'ready'` and a walk from `pose_kind == 'walk'`. Write the value the
  row returned, even when it disagrees.

## Two rows that each assume the other's spec value measure neither

- Sotetseg's wrong-tile damage is `floor(p * hp) + f`. `rag_flat_entry` computed f by
  fixing p at the spec's 6.7 percent, and `rag_percent` bracketed p by fixing f at the
  spec's 11, then wrote "66.7" because the bracket held it. Two splats (16 and 15, one
  tick apart) also fit f = 10 with p = 8 percent and f = 13 with p = 4 percent.
- To pin both, collect splats at clearly different hitpoints (for example a stay on a
  wrong tile at high hitpoints and another at low), solve for every (p, f) pair that
  fits all of them, and write that set. If more than one pair fits, the row is open.
- The same goes for a max-hit row from one sample. `sotetseg.ball_max_entry` passed on a
  single unprayed 4 against a 22 max. A max row needs enough samples to reach the max,
  or a stated reason that it cannot reach it.

## A technique shot is taken at the technique, not at the end

- Maiden's `tech.*` shots (104-107) were all the corridor after the exit, and
  Sotetseg's `fight.over` plus five `technique.*` shots (056-061) were one frame with the
  boss alive at 36 of 560. The rows were evaluated from the log after the fight, so the
  shot was whatever the screen showed then.
- Take the frame at the moment (`t.shot` inside the loop on the tick it happens: the
  sidestep, the prayer lighting, the off-on-3 step, the tornado chase, the kill), keep
  its name, and let the end-of-test row reference it. The kill shot must show the death
  or the wave-complete line, not a living boss.

## loc_near rows use tile_x / tile_z

- `t.world.loc_near` (and its obj answer) returns `tile_x`, `tile_z`, `level`, not `x` /
  `z` (`script/plugins/quest_driver/world.lua`). Reading `.x` gives nil, and a
  comparison with nil fails silently in a filter.

## Nylocas Entry figures that differ from grade D spec rows

STALE since seam10 (OSRS-Content 0bc66408fc): all four now match their rows. See the seam10
section, "Nylocas Vasilias in Entry". What follows is the content as it was.

- Measured on our server, solo Entry: `nylocas.pillar_collapse_entry_min` 3, 15 and 27 hp
  (spec 30+); `nylocas.explosion_radius` the farthest hurt tile was 1 (spec 2; no sample
  at distance 2 yet); `nylocas.vasilias_attacks_entry` 2 (spec 3-4);
  `nylocas.vasilias_switch_entry` 9 and 10 ticks (spec 15, in CONTENT_BUGS).
- Report these as doc_gap or content_bug with both figures. Never bend the measured value
  to the spec. Get a distance-2 sample before calling the explosion radius 1.

# Seam pass 10: the four stuck rooms (Bloat, Nylocas, Xarpus, Verzik)

Seam pass 10 (`matthew-mbp-m4-raid-b1-seam10`, triage `SEAM_TRIAGE_2026-10-03h.md`) settles
the rows the sixth ToB room pass could not finish. Three content fixes landed in OSRS-Content
0bc66408fc (Nylocas, Verzik, Xarpus); the Bloat row was not a content bug. No driver verb
changed. Five seam rows prove the fixes and one recipe: `seam.special_attack_spent`,
`seam.vasilias_entry_window_and_reflect`, `seam.verzik_athanatos_poison_bursts`,
`seam.verzik_yellow_pool_walkable` and `seam.tob_xarpus_acid_is_the_puddle_tile`. The fixers'
reports, scratch scripts and kept tick logs are under
`build/seam_state/matthew-mbp-m4-raid-b1-seam10/`.

## Bloat: Defence reads 80 of 80 after a Dragon warhammer special (bloat.stomp_defence)

- The drain is deterministic. If Defence reads undrained, the special never fired or it
  dealt 0. `specs/pvm_dragon_warhammer.rs2` drains 30 % of CURRENT Defence only when the
  prepared damage is above 0. On Entry Bloat (Defence 80) one special gives 56 of 80. A 0
  splat drains nothing: try again in the next down while energy is 500 or more.
- Arm it from the minimap orb: `t.ui.widget('orbs:specbutton')`, `t.ui.invoke(wid, 1)`,
  `t.ticks(1)`, then `varp301_sa_attack` reads 1. Never press
  `combat_interface:special_attack`. Pressed while idle it does nothing. Pressed while
  engaged it arms and fires inside one tick, so varp301 reads 0 either way.
- Prove a special by the energy it SPENDS. Do not use varp301 (the firing swing clears it)
  or the first hitsplat (the first swing after the press can be a plain one: seam10 probe2,
  a plain 21 on t131 and the special on t137). Read `varp300_sa_energy` before the press,
  call `t.player.attack(boss, 2, 8)`, then poll varp300 once a tick until it falls by 500
  (DWH). That tick is the special swing. The `hit_npc` row on the boss's world slot within
  +-1 tick of it is the special's splat. Keep the hammer worn until then: equipping the
  scythe earlier throws the special away. Generic row: `seam.special_attack_spent` (it
  spawns a passive Man beside the player, because Lumbridge's own Men wander off).
- Swing in the down (age 1-6), where damage is not halved. A walking Bloat halves a 1 to
  0, and a 0 drains nothing.
- Read Defence with `::tobboss` guarded by the message serial. Take
  `m0 = t.msg.last(1)[1].serial`, send the cheat, `t.ticks(1)`, then accept only a
  `t.msg.last(8)` line whose serial is above m0 and that matches `def=(%d+) of (%d+)`. An
  unguarded `t.msg.last(4)` can return the previous read.
- The stomp restores Defence on exactly T+29, where T is the boss's `npc_anim` 8082 row
  (server tick). A read sent on T+28 reads drained and one sent on T+29 reads full. The
  stomp's own `hit_player` row (10 or more damage inside the down) is on T+29 too (probe3:
  T=59, t87 56/80, t88 80/80, stomp hit t88; T=127, t155 56, t156 80, stomp hit t156). For
  the row, read once at age 21-28 and once at age 30-44.
- The Dragon warhammer needs Attack 60 AND Strength 60 here. The two are refused one at a
  time ('You need to have a Strength level of 60.', then Attack).
- Still open for the room (author's, not a seam): `spec.bloat.fly_first` measures 3 against
  1 in the unmodified committed file too (the room start mark moved from tick 60 to 81), and
  `tech_flinch_tiles` comes and goes between runs.

## Nylocas Vasilias in Entry: 15-tick colours, a reflect that heals, collapses of 30+

- Since seam10 her windows are 14 ticks, then 15 every time (blert m10; wiki Vasilias:92).
  She attacks 3 or 4 times per window at gap 4: (2,6,10,14) or (3,7,11) after a turn, and
  (1,5,9,13) or (4,8,12) in the opening melee form. She never attacks on the turn tick.
- Measure `spec.nylocas.vasilias_switch_entry` from `npc_retype` rows on her world slot,
  counting from the spawning->melee retype (`to_type` 10787/10788/10789). The first gap is
  14, inside +-1. Measure `vasilias_attacks_entry` from `npc_anim` seq 8004/7989/7999
  between consecutive retypes.
- Read her form from the tick log, not from `t.npc.nearest`. The three story records share
  one name, so `nearest()` by symbol can answer ok for a form she is not in. Read the last
  `npc_retype` row's `to_type` on her slot.
- Reflect recipe (`spec.nylocas.vasilias_reflect`): in a magic (10788) or ranged (10789)
  form, press the melee weapon (abyssal whip) once, then step one tile to drop the auto
  attack. Probe only after she has taken damage, because `npc_heal` is written only when
  her level rises. A missed roll reflects 0 and heals nothing, so press again until an
  `npc_heal` row appears. The proof is three rows: `npc_heal` X on her slot on tick T,
  `hit_player` X on the raider on T with `npc_slot` = her slot, and her 0 `hit_npc` splat
  on T+1. Detail: 'measured 100 percent, N wrong-style hits: tick T heal X = reflect X ...
  (spec 100 percent, grade A, tol exact)'. Under `::god` the reflect is absorbed (hitsplat
  26, damage 0), so a room test probes without it.
- One wrong-style hit no longer nulls you on her (tob_damage.rs2; the waves still null).
  Switch back to the right style and keep killing.
- Surviving her solo in Entry: Protect from Melee makes her melee 0, but magic and ranged
  still hit 1-17 under the right prayer every 4 ticks. Eat at 75 or under (25 sharks killed
  her in s10ny_vas4, 24 of them eaten). Press attack again after every eat and on every
  retype, because she cancels your attack on the turn.
- `spec.nylocas.entry_recoil_cap`: write 'measured <largest reflected X> hp, <n>
  wrong-style hits (spec ? hp, grade E, tol approx); approximation, M97'. The content
  reflects the full rolled hit, capped at her hitpoints. Jagex's story-mode reduction has no
  figure.
- Support collapse (`spec.nylocas.pillar_collapse_entry_min` 30): each fall hits the raider
  for 30-50. It is a `hit_player` row with `npc_slot` -1 and `npc_type` -1, three ticks after
  the support's `npc_death` (type 10790), so on D+3. A dead thrower's projectile can land
  slotless on the same tick (a 5 on t501 in s10ny_room1): drop any slotless hit that a
  projectile row from an npc freed before it lands explains. Three falls deal 90-150, so eat
  to full before a support's hitpoints run out. The room copy died at t504 after three
  supports fell; keeping supports standing is the author's job.
- Explosion reach (`spec.nylocas.explosion_radius` 2): measure footprint distance, the
  Chebyshev distance from the raider's T-1 tile to the nearest tile of the nylocas' body (a
  big is 2x2 from its south-west `npc_tile`). The content now uses the same measure. For a
  distance-2 sample, stand still two tiles off a support's chew tiles. In Entry solo the
  chewers on the south-west support stand at 6426-6427,85 and 6428,83-84, so standing at
  6429,86 gives samples at 2 and 3. Leave out detonations of a fighting nylocas
  (10780-10785) that swung in the 6 ticks before, because its own hit can land on T.
  Incoming chewers (10774-10779) bite only the support.
- A text spec row's measured text runs to the first ';', not the first comma
  (`raid_coverage.py` `parse_row`). Write 'measured melee; 1 instance, the type of her
  npc_spawn row (spec melee, grade B, tol exact)'. 'measured melee, 1 instances, ...' is
  graded as the whole string and mismatches.
- `spec.nylocas.av.vasilias_death.seq` is per form (8005 melee, 7991 magic, 7998 ranged;
  npc_anims.generated.npc). It is the `npc_anim` on her slot within 3 ticks of her
  `npc_death` row, and a run sees only the form she dies in. Its scope is now `stat`
  (nylocas.scope.tsv), so one room is not required to show all three.
- The section "Nylocas Entry figures that differ from grade D spec rows" above described the
  content before this pass. All four figures now match their rows.

## Xarpus phase 2: you die standing BESIDE acid, or the arena fills with poison by the tenth spit

- Since seam10 a puddle (loc 32744) hurts only a player standing on its own tile: a hit
  every tick you stay on it, and a delayed hit on the tick after you step onto it. The 3x3
  around it is the LANDING hit only, once, when the 1556 graphic lands. New spec row
  `xarpus.p2.pool_reach` (0 tiles, grade D).
- Track puddles as one tile each, from `loc_set` rows. Track landings in flight from
  projectile 1555 rows (land = tick + end_cycle // 30: spits 3 ticks, orbs 2 or 3). A
  planner that marks the 3x3 around each puddle as bad finds no clean tile by about spit 9
  and ends up standing on a puddle. That was tob_xarpus.lua's death (copy run
  s10x_copy_xarpus, tick 195). With only the puddle's own tile marked, the same copy kills
  him (collapse 255, 110 of 112 rows).
- Proof row: `seam.tob_xarpus_acid_is_the_puddle_tile` (Hard phase 1 ring: 0 hits one tile
  from it in 6 ticks, 4 hits in 5 ticks on it).
- Crossing a puddle in the middle of a 2-tile run step still costs nothing: the ground sweep
  sees only the tile at the end of each tick. The wiki's 'run over it' is not modelled
  (unsourced as to how).

## RECIPE: Entry solo Xarpus, melee

Scratch `build/seam_state/matthew-mbp-m4-raid-b1-seam10/x/s10x_wiki.lua` killed him under
four run names (collapse 214, 232, 256, 242).

- Kit: 99 Attack, Strength, Defence, Hitpoints and Prayer; scythe_of_vitur, Torva helm,
  chest and legs, ferocious gloves, primordial boots, infernal cape, berserker ring,
  zenyte_amulet_enchanted, 18 sharks and a 4-dose super combat (28 slots before you equip).
  In phase 1: the inventory tab and `t.drive.camera(0,383,1100)` on the 2nd cover, Piety on
  the 3rd, the potion on the 4th.
- Spit tick S: the stand-up (`npc_retype` to 10768) + 7, then each `npc_anim` 8059 row + 4.
  His 5x5 is 6432..6436 x 97..101 for all of phases 2 and 3.
- Each spit: at server tick S-2, `step_tick` OUT to a ring-2 tile next to you (prefer one
  with no puddle), then `step_tick` back IN to a side melee tile next to it. Pick an IN
  tile with no puddle and no landing in flight on it. The steps resolve on S-1 and S, so the
  scan reads the OUT tile. Then `attack(sym, 2, 2)`. The puddle lands on the OUT tile. Its
  splash (at most 6 in Entry) reaches you, and you accept it, as the wiki does.
- Eat (an `inv_op` shark costs about 3 ticks) right after the step back in, when hitpoints
  are under 62. The eat costs you the next S-2 step. So at S-1 of that spit, with no step
  made, step twice along the ring to a tile 2 from where you stand (they resolve on S and
  S+1). The scan reads your old tile, the landing at S+3 misses you, and the old melee tile
  becomes a puddle.
- Any other landing in flight within 1 of you with 2 or more ticks left: leave its 3x3 (one
  step to a side tile 2 from its centre, else two steps). If your tile has a puddle and it
  is S-3 or earlier, step to the nearest side tile with no puddle.
- Phase 3 (`npc_say` 'Screeeech!'): stand on a side tile with no puddle and read `npc_face`
  rows after the screech (quadrant: z>99 north, x>6434 east). Press attack once when he
  faces another quadrant, then `step_tick` to a neighbouring clean side tile in your
  quadrant to stop. If the press answers `covered` (from 6434,96 the boss pixel sits under
  the HUD), never use that tile again.
- Measured (j2): phase 2 from tick 133 to 215, 13 step-backs, 2 lost to eats, 3 sharks.
  Damage: 12 splash hits (sum 62), 5 delayed puddle hits (sum 27), stomp 0. Every hit was at
  or under `xarpus.p2.max_hit.entry` 6.
- Still the author's in tob_xarpus.lua: `spec.xarpus.p3.screech_pct_entry` brackets
  20.8-27.3, wider than its `bracket<=5`; `technique.spit_dodge` counts 1 landing within one
  tile. `xarpus.p2.spit_landing` stays grade E at measured 3 (M71).

## Two runs of one room test disagree: the run name seeds the player's rolls

- Under ONE `--name` a run is byte-identical (s10x_wiki_j2 twice: 943 tick log rows equal).
  The account name is the run name, and it seeds the PLAYER's rolls. Different names first
  diverge at the player's first hit on the boss (tick 135: 0, 11 or 15 damage), while the
  boss and orb rolls match until then. A different name changes how long phase 2 lasts,
  and so the spit count. Prove a room under two names.
- tob_xarpus's 60/61 author pass and the reviewer's death at tick 205 ran on different
  content: seam9 (OSRS-Content 24198b54ed, 16:24) moved the landings onto Xarpus's own
  queues between them, which changes the random stream from the first landing on.
- Verzik runs likewise split early on a player hit roll (vz10_after and vz10_after2 on
  t42, 0 against 8). A pass or fail of a late P3 row in one run proves little: loop on
  state, or run it under several names.

## Verzik P2: no 1588 projectile after an Athanatos landing (verzik.av.p2_purple.poison_globule)

- The globule is not a landing event. It flies only when the Athanatos is hit with poison:
  a poisoned weapon or ammo (rune_arrow_p in a twisted bow's quiver, any `*_p` dagger,
  spear or dart, poison_bolt) or a charged serpentine helm (tob_damage.rs2
  `~tob_hit_is_poisonous`).
- On that hit tick the log shows `npc_spotanim` 1590 (height 92) on the Athanatos slot
  (Entry type 10844), projectile 1588 (from the Athanatos to her), `hit_npc` on her of up to
  70, and `npc_death` of the Athanatos. Its death anim 8078 plays one tick later. Since
  seam10 the poisoned hit bursts it (wiki_Nylocas_Athanatos:54), so a burst Athanatos sends
  no more 1587 heals. Proof row: `seam.verzik_athanatos_poison_bursts`.
- Anchor the row on your `hit_npc` on type 10844, not on the 1589 landing. Read
  `t.ticklog.rows{since=<the Athanatos npc_spawn serial>, kind={'npc_spotanim',
  'projectile','npc_death'}}`.
- The heal rows (p2_purple_heal, heal_period, heal_proj) need an Athanatos that lives at
  least two beats. Let it heal twice (`npc_heal` rows with source
  `[proc,tob_verzik_athanatos_tick]`, +5 and +10 after spawn), then shoot it.
- Rune arrows hit weaker than dragon arrows in a food-limited fight. The other choice is to
  keep dragon arrows and wear serpentine_helm_charged in place of the Armadyl helmet, which
  makes every hit poisonous.

## Verzik P3: the yellow pool is on a wall, or 1600 while standing next to the pool (verzik.av.p3_yellows.gfx_blast)

- The pool was never under her body. It was drawn on the wall row z=99 next to the throne
  (x<=6417 on the west edge is wall too). Since seam10 every pool is on a tile you can stand
  on, within 2 tiles of you and never your own tile. Proof row:
  `seam.verzik_yellow_pool_walkable`.
- Recipe: read `map_spotanim` 1595 (`t.ticklog.rows{kind='map_spotanim', spotanim=1595}`:
  `row.x` / `row.z`), walk to it at once (from 2 tiles away you reach it the next tick) and
  stay on it 14 ticks.
- At pool tick +14 every raider who is a target gets `player_spotanim` 1596. A raider ALONE
  on a pool then gets 1597 and 'The power resonating here protects you from the blast.' A
  raider off the pool (or sharing it) gets 1600 at height 96 plus the hit (up to 80).
  Nothing plays on an empty pool: its 1595 copies just end at the blast. Walking under her
  to reach a pool is fine (players stand under her in P3).

## Verzik P1: a shield hit over the cap right after a weapon swap (tech.p1_cap_melee_ranged)

- Match each `hit_npc` on the P1 form (Entry 10831) to the `player_anim` that launched it,
  not to the swap tick. Bare fists (422) land +1 tick. Dawnbringer (1167) lands +4 (68->72
  ... 84->88 in vz10_base). The twisted bow (426) landed +3 at that range (88->91); the
  bow's delay grows with distance.
- The tick-88 14 was the Dawnbringer cast from t84; the author's 'two ticks after the swap'
  rule called it a bow hit. Safe rule: after a Dawnbringer to bow swap, a hit belongs to the
  bow only if it lands 5 or more ticks after the last 1167 anim (2 or more after the last
  422 punch). The Dawnbringer is exempt from the cap (tob_damage.rs2:299).
- A fist cap of 10 shows only if a fist roll reaches 10: four punches maxed at 6 in
  vz10_base.

## Verzik room rows the author left unmeasured: the exact reads

Checked in scratch runs vz10_room3, vz10_room4 and vz10_room5.

- throne_seq: tick log `loc_anim` with loc 32737 and seq 8108, three ticks after her
  `npc_death` (t121 -> t124), on the same tick as `loc_set` 32737. The client row reads it
  too: `t.world.loc_near('tob_dungeon_verzik_throne_transforming', 60)`, then
  `t.world.hazard_at(tile)` loc row seq 8108 at 6431,100. 8053 is that loc's cache `anim=`
  and has no row.
- jingle: tick log kind `jingle` with jingle 250 at throne +1 (t125). The spec row's quantity
  now says one tick after the throne (it said two; no source states an offset).
- barrier: `t.world.loc_near('tob_walkway_verzik_barrier', 60)` gives id 33028 at 6432,78,
  and the `hazard_at` loc row seq is 7929 (ds2_lithkren_barrier_glow, the same seq Bloat's
  `tob_arena_barrier` plays).
- door: 32738 by `loc_set` at throne +5 (t129).
- map_locs: `loc_near(sym, 0)` finds 41 of the 42. `tob_dungeon_verzik_throne_empty` is a
  varbit-6400 multiloc that stays hidden until `loc_set` 32686 at fight start (t42 in
  vz10_room3); count it from that row.
- death_cage: STALE since seam11, use `t.world.loc_copies` (seam11 section below, "Counting
  every copy of a loc"). Before it: `hazard_at` over the 24 map tiles of
  m49_67.jl2:255-278 finds 32717 on 10; the other 14 show other wall locs. The template 49_67's instance origin is (6401,64): local (lx,lz) -> (6401+lx, 64+lz).
- Scratch only, never in a test: in P2, `::tobvzleft 0` loses to a live Athanatos heal
  whenever she is the higher npc slot (a second instance in one session). She then reads
  about 10, crosses 35 % and summons the reds. `::tobvzleft -60` (an overkill past the
  floor) reaches P3 every time.

## The bracket tolerance now parses

- `raid_coverage.py`'s `SPEC_RE` accepted only exact, +-N, range and approx, so a row
  written `tol bracket<=N` (4d4331ae2) read as malformed although `within()` already graded
  it. It now parses (seam10 closer).

## A solved bracket under `tol range` with one spec value never passes

- `raid_coverage.py`'s `within()` reads a single spec value under `tol range` as a bound
  only when the row's id or quantity says max/cap/ceiling/upper or min/floor/lower;
  otherwise it is equality. `sotetseg.rag_percent` (66.7, range) and
  `sotetseg.rag_flat_entry` (11, range) are neither, so a two-sample solve that writes
  `measured 5.9-8.1` can never be graded FULL, however honest it is.
- The row needs `bracket<=N` in `sotetseg.tsv` (a spec pass decides N). Until then write
  the solved set, let the row stay open, and report it in doc_gaps. Never narrow the set
  to the spec value to make it pass. (Reported by the sotetseg reviewer of
  matthew-mbp-m4-raid-b1-rooms-tob.)

## `hit_player` is after the prayer: a blocked hit's size is not in the log

- `hit_player` carries the splat as shown, so a spell blocked by a protection prayer is a
  row with damage 0 (hitsplat 26), not the rolled damage. A row that wants "the share of
  the damage he heals", like `verzik.p2_heal_spell_fraction`, cannot be measured from
  prayed hits. Take the samples unprayed (eat for it), or read the heal against the
  unprayed hits only, and say which. (Reported by the verzik reviewer of
  matthew-mbp-m4-raid-b1-rooms-tob.)
- Since seam11 every hit row also carries `raw`, the hit as the caller dealt it (seam11
  section below, "A hit's real size"). A hit content zeroed for prayer still reads raw 0:
  the engine never sees the script's roll.

## Sotetseg: a ball cast on the maze proc tick (SETTLED by seam11, see below)

- STALE: seam11 settled it. He plays no 8142 at the proc, and a proc-tick ball is thrown
  but not counted (the seam11 Sotetseg headings below). The old report, for the record:
- The sotetseg reviewer saw the attack on a maze proc tick go uncounted in the ten-ball
  counter, or log `npc_anim` 8139 (attack_ranged) where `av.maze.boss_seq` expects 8142
  (shadow_portal); the counter is `tob_sotetseg.rs2` around lines 226-250. This is
  unconfirmed. If your rows `magic_per_ball`, `maze_boss_idle_at_proc` or
  `av.maze.boss_seq` disagree near a proc, put both figures in doc_gaps for the seam
  pass. Do not change how you count to make them agree.

## A reflect that equals the heal does not prove 100 percent (nylocas.vasilias_reflect)

- Under the old rule a wrong-style hit on Vasilias reflected 50 percent and healed 50
  percent. Under the current rule it reflects 100 and heals 100. In both cases the
  `hit_player` reflect equals the `npc_heal` amount, so "heal == reflect" holds under
  either rule. `tob_nylocas.lua` wrote "measured 100" from that equality on one hit (18).
- The rolled hit never shows: her splat is 0. To tell 100 from 50, compare the reflects
  with your weapon's max hit against her. A reflect above half the max is possible only
  at 100 percent. Take several wrong-style hits, write the largest reflect and the max hit
  you used, and leave the row open if no reflect goes above half the max.

## Every technique row has its own frame, and so does every prayer switch

- The nylocas file of rooms-tob launch 7 published no frame of the fight. 011 is the
  empty arena after the kill, and every later shot is outside the Theatre after
  `t.raid.leave`. None of its 19 prayer switches, techniques or kill was shot at the
  moment. That alone sends a room back.
- Xarpus shot its spit dodge, screech, gaze retaliation and kill at the moment, but not
  `technique.exhumed_cover` (standing on an exhumed when it opens) or
  `technique.stomp_skip` (standing under him on a scan tick). Shoot each technique row
  on the tick it happens and give the frame the row's name. A log-proved row with no
  frame is a gap that the sampler records.

<!-- seam11 (matthew-mbp-m4-raid-b1-seam11): the headings below, one per fact. -->

## A hit's real size: `raw` on hit_player and hit_npc rows (seam11)

- `damage` is the splat as shown: after `::god`, the absorption pool and the clamp to the
  hitpoints left. `raw` is the hit the caller dealt, before those three.
- Anything content does before it calls `damage` is already inside `raw`: a protection
  prayer zeroing or halving the hit, Justiciar, slayer caps. A prayed-off hit reads raw 0.
- `raw` is read before `::god`, so a scratch can keep the player alive with `::god 1` and
  still read every hit's size (damage 0, raw 40).
- A killing blow that overkills reads damage = the hitpoints left, raw = the hit. On a
  player's own swings content clamps at preparation (player_hit_npc_prepare.rs2:230), so
  expect raw = damage there.
- In ticklog.tsv, hit_player's raw is the column AFTER the label (`g`), so readers that
  index the label as column 9 are unchanged. hit_npc's raw is column `e`.
- Conformance: `seam.ticklog_hit_raw`.

## Measuring Verzik's blood-spell heal (verzik.p2_heal_spell_fraction)

- Each cast is an npc_heal row on her slot with source `[proc,tob_verzik_blood_spell]` on
  the cast tick T, beside a projectile row (spotanim 1591, start_cycle 20, end_cycle 120).
- The hit lands as a hit_player row with npc_slot = her slot on
  T + (end_cycle - start_cycle) // 30 + 1, which is T+4. Pair on that landing tick, not on
  "the next hit", because her urnbomb also hits from her slot.
- Unprayed, heal = floor(raw * 50 / 100): 11 of 11 casts in build/quest_gate/s11_heal2
  (raw 7..44). Assert the fraction on unprayed casts.
- Under Protect from Magic you take 0 (raw 0) and, since seam12, the heal counts half the
  roll: floor(floor(roll / 2) / 2), at most 11 for one raider. The Blert pull settled it
  (see "Verzik P2: the blood spell off a prayed raider heals her half the HALVED roll"
  in the seam 12 section). Assert prayed casts at 11 or less.

## Counting every copy of a loc: t.world.loc_copies(sym, radius) (seam11)

- `t.world.loc_near` answers the nearest copy. `t.world.loc_copies(sym, radius)` answers
  (result, summary, rows) like `t.npc.tiles`: `rows.total` is the count, rows are nearest
  first ({loc_id, resolved_loc_id, x, z, level, element_id, shape, seq, ...}).
- radius 0 is the whole loaded scene, map-placed and server-placed alike. `no_row` when
  none is placed.
- It matches the PLACED id (for a multiloc, the wrapper the map names), with no multiloc
  swap, because that swap walks the whole pool.
- Do not count with api-level loc lists: they keep the nearest 8192 placements of every
  id, and a Lumbridge scene has 8,515.
- The Verzik room's spectator cages: `t.world.loc_copies("tob_dungeon_verzik_death_cage", 0)`
  gives 24 copies on the m49_67.jl2:255-278 pattern (south-west copy 6421,100 in the
  Entry instance). Conformance: `world.loc_copies`, `seam.loc_copies_verzik_death_cage`.

## An npc whose generated record names an attack it never makes (seam11)

- Author `param=attack_anim,null` in the area's .npc. The server applies every
  `*.generated.npc` block first and every authored .npc block second
  (torirs_server_content.c `load_npc_generated_config`, then `load_npc_authored_config`),
  and cachepack ranks generated 1 and authored 2, so an authored value, null included,
  always wins.
- RIG_AUDIT.md's "directory order" claim was stale: the Athanatos' authored null held in
  all three modes before any seam11 edit.
- Seam11 authored the null for the blood spawn, both Matomenos families, Bloat, both
  tornado families and the Verzik death bat (which also gets death null), all modes.
  Conformance: `seam.authored_null_attack_anim_holds`.

## ::tobnpcanim <npc>: what attack_anim / death_anim the running server holds

- It adds a copy of the type on your tile, reads `npc_param` (the call
  skill_combat/combat.rs2 swings with), deletes it, and prints
  `tobnpcanim <npc> attack_anim=null|set death_anim=null|set cache_attack=null|set`.
- `set` means some seq: the script language has no seq-to-int. Read it with
  `t.msg.last` after `t.ticks(1)`.
- `cache_attack=` is `nc_param`, which reads only the cache's param table and never the
  overlay, so it says `set` even where the record is null. Never use `nc_param` to read an
  overlay param (an engine row, CONTENT_BUGS.md seam11).
- A measurement debugproc: fine in scratch and conformance, never in a room test.

## Adding the first authored block for an npc: restate its generated rows

- `gen_npc_combat.py --write` stops compiling any npc that an authored block names
  (`load_authored_blocks`), so on the next regeneration that npc's generated death_anim,
  defend_anim and attackrate disappear.
- The seam11 blocks for tob_sotetseg_creeper_story/_hard and the three death bats state all
  three anim rows for this reason. Before any `--write`, every ToB npc whose death_anim
  lives only in a generated file (tob_bloat, the Matomenos, the blood spawn) needs it
  restated in tob.npc. The generator was NOT re-run by seam11.

## gen_npc_combat.py refuses spawn, sleep and stance sequences as attacks (seam11)

- a4 ("the rig's only forcedpriority 6-8 seq") now refuses a seq whose name states a role
  (spawn, despawn, emerge, sleep, ready, idle, walk, death, transform, ...), and any
  inferred slot equal to the npc's own readyanim is refused (`drop_own_stance`). Both
  refusals are written into the ledger note.
- `--validate` grades are unchanged. The 54 slots the next `--write` will change are in
  build/seam_state/matthew-mbp-m4-raid-b1-seam11/rigledger/decide_diff.txt.

## A Matomenos plays elemental_spawn 8098 on the tick it spawns (seam11)

- The tick log has an npc_anim 8098 row on the same slot and tick as each Matomenos'
  npc_spawn row (2 of 2 in rigledger_crab; 6 of 6 in a full tob_maiden run). Pair the rows
  with `{ kind = 'npc_anim', type = 10820 }` (Entry Maiden) and `type = 10845` (Entry
  Verzik). Spec rows `maiden.av.crab_spawn.seq`, `verzik.av.reds.spawn_seq`.
- It changed none of tob_maiden's measured numbers. Conformance:
  `seam.matomenos_spawn_seq_on_spawn_tick`.

## Xarpus dies in two animations: 8062 then 8063

- On the killing blow (npc_death tick K) the combat form 10768 plays tob_xarpus_death_a 8062
  at K+1; its frame-1 sound 4014 tob_xarpus_death_wingflap is the client's and has no row.
- At K+3 the retype to the dead form 10769 plays death_b 8063.
- Measured 3 of 3 (K=205, 174, 281). Spec row `xarpus.av.death_a.seq` (new in seam11: the
  committed tob_xarpus test does not measure it yet; SEAM_LEDGER.md).

## Sotetseg does not animate at the maze proc (no 8142 on him)

- On a proc tick he plays nothing of his own. If his attack fell due on that tick, you see
  that attack's seq (8139 or 8138): he attacks first, then the maze opens.
- Detect the proc with the npc_retype row on his slot (combat -> noncombat form), or with
  the runner's player_anim 1816 row. Both are on the same tick
  (`sotetseg.maze_boss_idle_at_proc` = 0).
- Never look for 8142 on him: it belongs to the exit portal loc 33037
  (`sotetseg.av.maze.exit_portal`). Spec row `sotetseg.av.maze.boss_seq` is now 0.

## A ball he throws on the proc tick does not count toward the death ball

- It is thrown, logged (npc_anim 8139 plus projectile 1606 on the retype tick) and seen,
  but a death-ball run that holds it has 11 ordinary balls, not 10 (blert 2 of 2;
  sote11_a/b 4 of 4).
- Mark a run as proc-tick if any ball tick equals an npc_retype tick of his slot. Assert
  `sotetseg.magic_per_ball` (10) on the other runs and `sotetseg.magic_per_ball_proc_tick`
  (11) on these.
- A run where the counter was already at 10 when the maze opened also measures 11 (the
  post-maze hold): that is `sotetseg.death_ball_after_maze`.

## The Sotetseg tornado rises and sinks (npc_anim 9004 / 9005 on its slot)

- On its npc_spawn tick the tornado plays 9004; its first npc_tile step comes on a later
  tick.
- When it leaves (you step back to row 3, it runs out of path, or the maze ends) it plays
  9005 on tick D, takes no more steps, deals no more hits, and its npc_free row is on D+1
  (`sotetseg.tornado_despawn_ticks` = 1).
- The step back is seen one tick late, like every maze rule, because the hook reads the
  tile you stood on at the end of the previous tick: back on row 3 at T, 9005 at T+1,
  free at T+2 (sote11_a: 126, 127, 128).
- A realm tornado still alive when the runner leaves goes with the realm (npc_free on the
  re-activation tick, no 9005).
- Known stall (open, CONTENT_BUGS.md seam11): when the path starts at column 12 or 13 the
  size-3 tornado never moves. If your runner walks back to it, that is the stall.

## Sotetseg: taking two rag samples safely, solo Entry (rag_percent, rag_flat_entry)

- In maze 1, step one tile north from the start (row 2, below the tornado's row 4). Eat
  to 99 first and wait 2 ticks so `t.skill.read('hitpoints')` is current, then note the
  tick.
- `t.player.step_tick` onto an off-path tile beside you that has a darktile
  (`t.world.hazard_at(x, z, 3).locs` non-empty and not in the lit path). Wait
  `t.ticks(5)`, then `step_tick` back onto the path tile.
- That stay lands 5 splats on consecutive ticks: issue tick I, splats at I+2..I+6.
  Measured from 99: 17, 16, 15, 14, 13 at 99, 79, 63, 48, 34, ending at 21 (sote11_b). The
  boss deals no damage in the maze and there is no tornado below row 4, so 21 is safe.
  Eat back above 80 before stepping onto row 4.
- The solve gives f 11-12 and p 50.6-70.7, inside bracket<=6 and bracket<=60. Three or four
  splats pass too; five meets the table's 60-apart note.
- The splats are hit_player rows on your pid with npc_slot -1, damage 8..29 and hitsplat
  not 26, one per tick of the stay; each tick also has a map_spotanim 505 and a sound
  3985 row.
- Work out the hitpoints before each splat from the reading minus EVERY hit_player row on
  your pid since the reading's tick, in serial order. That includes the realm chip
  (damage 1-3, npc_slot -1, player_spotanim 1608 on its tick), every 7 ticks, which can
  fall inside the stay (sote11_b: chip of 3 at 104). The chip comes before the rag within
  a tick, so subtract it first.

## Verzik P3 webs: a solo raider gets one web per cast

- Strategies:953 "three at a time if they are not on the same tile": the webs aim at
  TILES. Solo you are one tile, so each 8127 sends one 1601 projectile and one web npc
  8376 lands on the tile you stood on, +3 ticks. Count with
  `t.ticklog.rows({kind='projectile', spotanim=1601})` on the 8127 tick.
  `verzik.p3_webs_per_cast` is scoped party since seam11.
- Web lifetime: npc_spawn 8376 to npc_free is 20 (M81, our constant). Stepping off does
  not change it. To avoid the bind, walk 3 tiles off on the 8127 tick (the walk must
  start by cast +2).

## Verzik enrage: the tornado touches you where you stand (seam11)

- Contact is the tile next to you (npc_range 1, tob_verzik.rs2 ~tob_verzik_tornado_tick).
  Before seam11 a standing raider was never touched. This is an open approximation: no
  source says whether contact is the shared tile or the one beside it.
- The tornado walks 1 tile a tick from her south-west tile and spawns with npc_anim 9004.
  On the touch tick: hit_player with npc_type 10846 (Entry), player_spotanim 1602 on you,
  npc_anim 9005 on it, and npc_heal source `[proc,tob_verzik_tornado_heal]` of exactly 3x
  the touch. npc_free follows the next tick, and npc_spawn 10846 comes again 16 ticks
  after the touch. Spec row `verzik.av.tornado.seqs`; conformance
  `seam.verzik_tornado_touches_a_standing_raider`.
- `t.npc.nearest` never sees the tornado (cache interactable=no). Read its tile from
  t.ticklog npc_tile rows (type 10846).
- Recipe for pct, heal_mult, respawn and av.p3_enrage.tornado: stand on open floor south
  of her (6430,84 in Entry), let the first tornado touch you, then outrun the rest. When
  an npc_tile row puts it 4 tiles away or closer, walk 6 tiles directly away, clamped to
  x 6421..6443 and z 81..97.
- Do not eat in the tick before the touch: the pct row reads your hitpoints the tick
  before. A brew in that tick made a 47 look like half of 79 (kill5). Safer:
  hp_after = before - floor(before/2), and check the touch equals hp_after or
  hp_after - 1, using the hp read the tick after.
- Never take a touch while the green ball (projectile 1598) is in flight: the ball (66)
  landing on a 49-hp touch killed kill6. Keep hp at 90 or more from the yellows until
  launch + 13.
- The floor of 5 (`p3_tornado_min`) needs 9 hitpoints or fewer at the touch: scratch only
  (`::setlevel hitpoints 8` gives a touch of 5 and a heal of 15); scoped stat.

## Verzik P1: measuring the shield cap per style (p1_cap 10,3,3)

- Classify each hit_npc on the P1 form (Entry 10831) by your last player_anim before it:
  1658 whip (melee), 426 bow (ranged), 1162 a cast (magic). Dawnbringer hits (1167) are
  uncapped in our content, so leave them out.
- Recipe (vz11_kill7): one Dawnbringer special first (combat_interface:special_attack,
  then attack: about 100-145). Then abyssal_whip x6 (most hits are 10), twisted_bow x3
  (3 each), then staff_of_fire with `t.player.cast('fire_blast', 'verzik_phase1_story', 1)`
  until one cast lands DAMAGE over 0 (stop on damage, not on a hit row). Finish with the
  whip.
- In armadyl, fire blast splashes often (0 of 8 in kill5). Bring runes for 10 or more
  casts (airrune and deathrune), or free two slots so armour can come off.
- Tank the bolts under Protect from Magic (Entry prayed max 30), eat below 55; P1 costs
  about 150 hp over about 150 ticks.

## Verzik P2: the first crab kited until it dies of age (p2_crab_lifetime 25)

- The crab spawns 6 tiles out from you, on the side AWAY from her
  (~tob_verzik_crab_tile). Stand south-west at 6427,86 when she casts (no crab if the wall
  is behind you, e.g. north of her at 6432,98).
- Kite on a ring round her 3x3 body with corners 6427,85 / 6427,95 / 6437,95 / 6437,85.
  Head for the corner opposite the crab's nearest corner, one corner at a time, and keep
  the corner you chose until you reach it (re-picking each tick walked into the crab).
- Never path next to her body: a body slam knocks you 3 tiles, into the crab. Her body
  blocks the crab, which gets stuck on her face.
- Read npc_spawn of 10841/10842/10843 to its npc_death: 25 when you are 4 or more tiles
  away.
- `p2_zap_bounces` and `p2_zap_self_damage` need a second raider (scoped party, seam11).

## Verzik P2: dodge the Athanatos landing; the bombs are cheap under Protect from Missiles

- Projectile 1586 (the Athanatos) lands on its dst tile 6 ticks later for up to 78 (the
  Normal figure, in Entry). Step 2 tiles off any 1586 whose dst is your tile, and let this
  dodge win over shooting crabs.
- Urnbomb 1583: prayed max 8. Stepping to the next tile when dst is yours works, but
  tanking is fine. The zap (1585, up to 48, every 5th attack) cannot be dodged solo; eat
  at 62 or below.

## Verzik reds: measuring the absorb window (reds_absorb_window 5)

- The heal is judged at your ATTACK tick, not the landing. An attack on summon s+0..s+4
  (s is the npc_anim 8117 tick) writes npc_heal source `[proc,tob_prepare_player_hit]` on
  the attack tick and lands 0 (hitsplat 26). An attack on s+5 lands as damage. A roll of
  0 heals nothing and proves nothing, so retry on the next summon.
- Nothing may be in flight at the summon. Near 35% (740 of the 1000 bar; track it from
  npc_heal `hitpoints` minus hit_npc), fire single shots and cancel each with a one-tile
  walk. Hold once she reads 742 or below, and poll the log every tick.
- Summon 1: press attack at s+4. Summons come every 36 ticks: hold from s+30 and press at
  s2+5. After both probes, hold fire on s..s+4 of every later summon (otherwise your
  auto-attacks heal her).

## Verzik P3 that a solo bow survives (vz11_kill7, no god)

- Kit: armadyl worn, Dawnbringer wielded at the start; twisted_bow, abyssal_whip,
  staff_of_fire, airrune 60, deathrune 15, ranging potion, 6 saradomin brews
  (br_4dosepotionofsaradomin), 4 restores (br_4dose2restore), 13 anglerfish. Eat brew,
  brew, restore. P2 starts with rigour and Protect from Missiles; switch to Protect from
  Magic on projectile 1591.
- In P3 switch prayer on her npc_anim (8124 magic, 8125 ranged) or on the projectile
  (1594 magic, 1593 ranged); since seam12 the prayer counts when the projectile lands, so
  the switch is in time. The seam 12 recipe below replaces this one for the tornado
  (this one has no flee). Keep at least 2 tiles from
  her 7x7 body. Shoot P3 crabs (3 hp) at once. For the yellows, walk onto the
  map_spotanim 1595 tile and hold until pool tick + 15. For the webs, walk 3 tiles off on
  the 8127 tick.
- Two kills under two run names: t741 (kill7, 15 of 15 rows) and t748 (kill7b). The plan
  is run-name fragile (the run name seeds the player's rolls).

<!-- rooms-tob sampler, eighth launch (2026-10-03): the headings below. -->

## Bloat: a 7 among your "protected" fly hits means the prayer was off when it was thrown

- The server rolls a fly's damage when it launches the fly (`queue*(combat_damage_player,
  flight)(npc_uid, ~tob_bloat_fly_damage)`, tob_bloat.rs2:431). It reads the target's
  prayer at that moment, not at the splat.
- Entry flies roll 4-8 (`~tob_bloat_entry_hit`, tob_bloat.rs2:792-794: max/2 to max).
  Protect from Missiles keeps 75 percent, rounded down (tob_bloat.rs2:487-489,
  tob.constant:741-742). So a prayed fly is 3, 3, 4, 5 or 6, and never more than 6.
- The rooms-tob launch-8 tob_bloat run agrees: 95 prayed hits, 3-6, of which 46 are 3s
  (4 and 5 both round to 3). The 5 hits before the prayer was first lit were 4,6,7,7,4.
- So if a 7 shows up among hits you count as protected, your classification is wrong:
  the prayer was off on the server when that fly launched. The usual cause is the
  piety/protect swap on a down or rise, or prayer points at 0. It is not a content bug.
  Do not raise the 6 to let the 7 through. Count a hit as protected only when the
  prayer was on from the launch tick (splat tick minus the flight) through the splat.

## Sotetseg: putting a ball on the maze proc tick (magic_per_ball_proc_tick)

- His timer attacks first and checks the maze second, on the same tick
  (tob_sotetseg.rs2:104-109). The check reads his hitpoints against the scaled pool in
  tenths of a percent (tob_sotetseg.rs2:959-985, tob.constant:1530-1531). In solo Entry
  (560) the first maze procs at 373 or below and the second at 187 or below.
- In the log the proc (his npc_retype) is the tick AFTER the crossing hit_npc. The
  rooms-tob launch-8 tob_sotetseg run shows this twice: hit t103, retype t104; hit
  t359, retype t360. His balls come every 5 ticks (npc_anim 8139: t91, 96, 101, ...).
- To get a proc-tick ball, bring him to a few points above the threshold and stop
  attacking. Read his next ball tick B from the 8139 rows. Then deliver one hit whose
  hit_npc lands on B-1 and is big enough to cross. A 0 or a short hit just means you
  wait 5 ticks and try again. Measure your own weapon's swing-to-splat delay first.
- Do not try to get there by shifting the start tick (ticks 2..9). The proc tick follows
  whichever hit happens to cross, so a tick shift only moves the gamble.
- Seam12 proved a recipe that works (5-tick bow, phase alignment, a hold past the slot
  after a death ball): "Sotetseg: putting a ball on the maze proc tick, a recipe that
  works" in the seam 12 section.

## Verzik P3: she picks the style and reads your prayer on the tick she animates (SUPERSEDED by seam12)

- Fixed in seam12. The prayer is now read when the projectile lands, and the green ball is
  always on 8125. See "Verzik P3: switch the protection prayer on what she shows; it counts
  when the projectile lands" in the seam 12 section.

## Verzik P3: after a tornado heal she attacks every 7 ticks again (FIXED by seam12)

- Fixed in seam12: `~tob_verzik_enraged` now returns true once bit 70 is set, so a tornado
  heal no longer resets her cadence to 7. See "Verzik P3: the enrage is permanent, even after
  a tornado heal" in the seam 12 section. A 7 after the enrage line is now a real failure.

# Seam pass 12: the Sotetseg tornado and proc-tick ball, Verzik P3 against the sources

Both seams were content (tob_sotetseg.rs2, tob.npc, tob_verzik.rs2). No driver verb was
added or changed, so the conformance PLAN did not move. The scratch scripts that prove each
recipe are under build/seam_state/matthew-mbp-m4-raid-b1-seam12/ (sote12/procball_scratch.lua,
sote12/tornado_scratch*.lua, vz12_kill.lua).

## Sotetseg: the maze tornado is centred on the path now

- It spawns with its 3x3 body around the path's start tile. It walks one path tile a tick
  (an npc_tile row every tick, diagonal where the body shifts), and it walks through the
  runner (`moverestrict=passthru` on all three creeper records). The old stall at start
  column 12 or 13 is gone: sote12_after_a and _b followed and hit from start columns 13, 12,
  1 and 7.
- Its npc_spawn / npc_tile coord is the body's SW corner. The path tile it is on is
  (x + 1, z + 1). On a row whose lit path touches column 0 it is (x, z + 1), and on a row
  touching column 13 it is (x + 2, z + 1). The realm's walls stand one tile outside the grid,
  and this engine's npc walk always respects walls, so the body is kept inside the grid there.
- It hits only on the path tile under its centre, on the tick AFTER its npc_tile row reaches
  your tile (its timer hunts first, then steps). sote12_after_a: npc_tile t69 onto the
  runner's tile, hit_player t70. A runner one tile a tick ahead of it is never hit.
- Scratch only: `::tobsotemaze <start> <toward>`, sent before the threshold hit, parks the
  next maze's path. Seed 0 is `<start>` (1..13). Later seeds step toward `<toward>` by up to
  5, or roll normally with -1. Never use it in a room test.

## Sotetseg: putting a ball on the maze proc tick, a recipe that works

- His attacks are npc_anim rows on his slot: 8139 with projectile 1606 is an ordinary ball,
  8139 with 1604 is the death ball, and 8138 is melee (only within 1 tile, so stand further).
  The next attack is the last + 5, or + 10 after a death ball. The attack after ten ordinary
  balls is the death ball.
- The maze procs on the tick AFTER the hit_npc that takes him to 373 or below (maze 1) or
  186 or below (maze 2) in solo Entry. Track his hitpoints as 560 minus every hit_npc on his
  slot. His attack on the proc tick comes first.
- Wield a 5-tick weapon and keep it firing: bow_of_faerdhinen (attackrate 5, range 10, no
  ammo; `::setlevel agility 99` to wield it). Every hit then lands at the same phase of his
  5-tick cycle. At bx+2, bz-5 with the fast press `t.player.attack(sym, 2, 2)`, swing
  (player_anim 426) to hit_npc is d = 3 ticks, and press to swing is L = 0.
- Align once, early. phase = (your hit_npc tick - any attack tick of his) mod 5, and you
  want 4. If it is not 4, cancel with a one-tile step and press again at H - d - L, where H
  is the first tick at least (last swing + 5 + d) with (H - his attack tick) mod 5 = 4.
- Inside the last ~70 hitpoints, on each swing S work out the attack at Bn = S + 6 + d (the
  tick after the NEXT swing's hit). If Bn is not an ordinary ball (it is the empty slot
  D + 5 after a death ball D, or the death ball itself), step off and press at
  S + 5(k + 1) - L for the smallest k that makes Bn + 5k a ball. sote12_procball_a shows why:
  an aligned hit landed on 144 before the empty 145 (death ball 140), the proc had no attack,
  and the run was 10.
- Do not eat inside the window: an eat delays the next swing and breaks the phase. Re-align
  after one.
- Mark the run as proc-tick by a ball tick equal to the npc_retype tick and assert 11;
  assert sotetseg.magic_per_ball (10) on the other runs.
- Proved twice. sote12_procball_c: hit 149 (383 -> 370), npc_retype 150 with 8139 + 1606
  on 150, 11 balls between death balls 140 and 235; hit 324 -> proc 325, 11 between 295 and
  403. The closer's re-run under a new name (close12_sote): hit 114 -> proc 115 with a ball,
  11 between 80 and 178; hit 262 -> proc 263, 11 between 238 and 336.
- The scratches used `::god 1` (incoming damage only). The hit timing, procs and ball counts
  are the player's own, but a room test must survive on food and prayer, and an eat inside
  the hold breaks the phase.

## Verzik P3: switch the protection prayer on what she shows; it counts when the projectile lands

- RULED by the owner, 2026-10-04: "You are correct about p3 Verzik on hit". The landing read
  is the sourced exception to the send-tick default (CONTENT_BUGS.md, Owner rulings); seam20
  left `[queue,tob_verzik_p3_auto_land]` as it is and added only a comment line citing this.
- The style shows on her attack tick T: npc_anim 8125 with projectile 1593 is ranged
  (Protect from Missiles), 8124 with 1594 is magic (Protect from Magic). During the crab
  summon (14406) the pose shows nothing, so key on the projectile there.
- The server reads your prayer on the landing tick (`[queue,tob_verzik_p3_auto_land]`,
  T + end_cycle // 30, 2-3 ticks). A `t.prayer.set` issued when you read the row (T or
  T+1) is in time. The splat comes one tick after that read, so pair an auto with its
  hit_player on T+3..T+5.
- The green ball is always on 8125 with projectile 1598 and no auto hit. A Missiles switch
  on its tick costs nothing.
- Prove it like `seam.verzik_p3_prayer_at_impact`: an auto whose prayer was off on T and on
  at the landing must splat at most half the max (Entry 10). vz12_kill_a: 7 such autos,
  max 8. close12_vz: 4, max 9. The old pack gave 5 of 11 over 10.
- Source: "You need prayer up as the projectile hits your character, otherwise it's
  considered off prayer" (transcripts/yt_oGPT3sZMnd8.md:51); spec row verzik.p3_prayer_read.

## Verzik P3: the enrage is permanent, even after a tornado heal

- After "I'm not finished with you just yet!" she attacks every 5 ticks for the rest of the
  phase, and the max stays 34 (Entry 20). A tornado heal that puts her back above 20 percent
  does not reset it (the bit 70 latch in `~tob_verzik_enraged`).
- Row: every plain-auto gap after the npc_say is 5. Record her hitpoints as npc_heal minus
  hit_npc damage, and show that some gaps start above 20 percent
  (`seam.verzik_enrage_latches`: vz12_kill_b had 12, close12_vz had 9).
- When the enrage lands inside the green ball's window, her next attack is the enrage tick
  + 5 (Blert models it the same way). Measure p3_ball_delay only on a ball window with no
  enrage in it.
- Spec row verzik.p3_enrage_latch (grade C: the Blert pull, the blert plugin's one-way
  flag, OpenOSRS's `tornados`).

## Verzik P2: the blood spell off a prayed raider heals her half the HALVED roll

- Under Protect from Magic you take 0 (raw 0 on the T+4 hit_player row). The heal (npc_heal
  `[proc,tob_verzik_blood_spell]` on T) is floor(floor(roll / 2) / 2), at most 11 for one
  raider. Unprayed, the heal is floor(raw * 50 / 100).
- Pair a cast with the hit on T + (1591 end_cycle // 30), never with "the next hit": her
  urnbomb hits from the same slot.
- Source: the Blert pull (sources/blert_api/spec_pass_verzik/verzik_seam12_2026-10-03.txt).
  Praying raiders lost 0 hitpoints in 370 of 370 casts, and the heal was 11 or less in 73
  of 78 clean readings. Spec row verzik.p2_heal_spell_fraction, now grade B.
- The heal still lands on the cast tick and reads the prayer at the cast, while Blert's
  hitpoint stream shows it at T+2 (health-bar latency unknown). Not changed by seam12.

## RECIPE: an Entry solo Verzik kill a driven player survives

- Script: build/seam_state/matthew-mbp-m4-raid-b1-seam12/vz12_kill.lua, built on seam11's
  vz11_kill7. Setup gear only, no `::god`, no boss cheat. It killed her as vz12_kill_a
  (t724) and vz12_kill_b (t824).
- Kit: armadyl worn, Dawnbringer wielded. Twisted bow, whip, staff of fire with 60 air and
  15 death runes, 6 saradomin brews, 4 restores, 1 ranging potion, 13 anglerfish.
- P1: Dawnbringer specs, then whip, then bow. P2: rigour and Protect from Missiles, Protect
  from Magic on 1591. P3: the prayer rule above.
- Read your prayer each loop with `t.prayer.read()` and press `t.prayer.set` only when the
  wanted one is off. That way the record shows which autos were reactive.
- Webs: step 3 tiles off on the 8127 tick. Yellows: the pool is the FIRST 1595 copy (more
  copies follow on later ticks). The blast is the 8126 tick + 14; be on the pool from
  8126 + 10.
- Tornado: let the first one touch if you want its rows. Then keep moving, away from it but
  toward the boss: "think in rectangles" (transcripts/yt_sDaQ2qsU8AQ.md:28).
- Track the freshest npc_tile row of the tornado type (10846), not a slot in `live`: a
  touched tornado is npc_del'd without a free row, and a stale slot hid it in vz12_kill_b's
  first try (5 touches, death).
- When it is 6 tiles away or closer, walk to the ring tile 3 off her body that is farthest
  from it. Skip any tile whose midpoint with you is within 2 of the tornado. Score 12 points
  per tile of wall clearance, up to 3, and 40 off for a tile beside the wall: a corner is a
  trap (6421,81 killed two tries). With a yellow pool down, stay within 4 tiles of the pool.
- IT IS NOT YET ROBUST. The run name seeds your rolls, and the flee is tuned on two names.
  The closer's re-run under a third name (close12_vz, final pack) died in P3 after 8 tornado
  touches, while every seam12 row in it passed. Prove a room test's plan under at least
  three names, and expect to tune the flee further.

## Vasilias reflect: `raw` on the reflect row does not tell 100 from 50 percent

- The reflect is its own `damage` call (tob_damage.rs2:293-300: `$reflect` = your rolled
  hit, clamped to her hitpoints, then `damage` on you and `npc_statheal` on her). The
  `hit_player` row's `raw` is the figure THAT call passed, so `damage == raw == npc_heal`
  holds under the old 50/50 rule as well: content would pass the halved roll to both.
  Your rolled hit is not in the log anywhere (her `hit_npc` that tick is 0, raw 0).
- The only discriminator in the log is size: a reflect above floor(max / 2), where max is
  your weapon's max hit at that press. An abyssal whip alone at 99 Strength, aggressive,
  no prayer or boost: floor(0.5 + 110 * 146 / 640) = 25, so any reflect over 12 is
  possible only at 100 percent (the ninth-launch run had 21 and 14). Compute the max from
  the player's Strength level and bonus at the press (brews drain Strength), assert the
  largest reflect against it, and write both numbers in the row; with no reflect over half
  the max, leave the row open.
- A reflect landing while she is near full hitpoints heals less than it reflects (heal cut
  at her base: 668 reflect 21, heal 13). Pair such a row on the reflect, not the heal.

## Sotetseg tornado at the end of the path: it catches you there and sinks the same tick

- A runner one tile a tick ahead of the tornado is not hit on the way, but the path ends
  and you stop; the tornado reaches your tile and hits (37 and 43 in the ninth-launch
  run, the realm runner standing on the end tile).
- A tornado that leaves by running out of path plays 9005 and is freed ON the same tick
  (9005@142 free@142, 9005@372 free@372), not D+1; the D+1 free is the step-back case
  (9005@333 free@334). `sotetseg.tornado_despawn_ticks` counts only the step-back case.

## A level read is a TABLE: `tonumber(t.skill.read(...))` is nil, and a `or 99` hides it

- `local _, reading = t.skill.read("strength")` gives a reading table; the current
  (drained or boosted) level is `reading.level`. `tonumber(reading)` is nil, so
  `tonumber(reading) or 99` reads 99 every time and a row built on it writes a constant
  as a measurement (the tenth-launch Vasilias reflect row: "max hit 25" at every press while
  `fight.levels661` read Strength 86 and shot 020 shows the 86 indicator). Read `.level`,
  and let a missing reading fail the row rather than fall back to a level.
- The whip has no aggressive style. The combat-tab slot pressed for the bow's rapid
  (`varp43_com_mode` 1) is the whip's Lash: controlled, +1 Strength, not +3. Standard melee
  max: floor(0.5 + (level * prayer + style + 8) * (bonus + 64) / 640); whip alone, Lash,
  no prayer: Strength 99 -> 25, 86 -> 22, 81 -> 21, 76 -> 20. A brew-drained Strength
  can put the max under a reflect you saw; the `<= max` leg only means something with the
  real level.

# Seam pass 13: the whole raid, played in two halves (2026-10-04)

The relay `tob_entry` is not authored yet. This pass built it as two scratch halves, each played
by click with one kit: Ver Sinhaza to Sotetseg's entrance, then Sotetseg's entrance to Ver
Sinhaza. Neither half is a committed test, and test/raids/fixtures/ holds no scratch examples,
so both scratches stay in the pass's state dir (paths in each recipe below). Both halves
survive with the kit they were tuned on, but only some of the time. They do not join: the kit
the first half measured at Sotetseg's entrance dies in Verzik P2 when the second half carries
it (see "the two halves' kits do not join" below). The five kept room tests and tob_verzik
re-ran unchanged on the final tree.

## The whole raid: the two halves' kits do not join (closer's reconciliation)

The second half assumed one kit at Sotetseg's entrance. The first half measured a different one
(kit_at_sotetseg.md in the state dir, run s13_relay_a9). The differences, slot by slot:

- Worn. Assumed: armadyl helm/chest/skirt, twisted bow, dragon arrows, insulated boots,
  ferocious gloves, infernal cape, berserker ring, amulet of torture. Measured: void ranger helm,
  elite void top/robes, void gloves, magic shortbow (i) with rune arrows, dragon boots, glory,
  imbued Saradomin cape, berserker ring (i). The insulated boots are carried, not worn.
- Melee for Xarpus. Assumed: scythe plus torva chest/legs. Measured: scythe plus the void melee
  helm, staff of fire and runes as well.
- Supplies. Assumed: 4 bandages, 6 sharks, 2 full brews and 2 full restores (8 brew doses, 8
  restore doses). Measured: 0 bandages, 0 sharks, 11 brew doses (4, 4, 3) and 5 restore doses
  (4, 1), and 16 free slots.

Run once with the measured kit. The scratch is
build/seam_state/matthew-mbp-m4-raid-b1-seam13/close/relay_second_half_measured_kit.lua. It
swaps torva for the void melee helm at Xarpus, and the twisted bow and armadyl for the shortbow,
the ranger helm and the insulated boots at Verzik. In close13_kit3, Sotetseg died at tick 523,
460 ticks after the begin (the fixer's twisted bow took 250-315). The chest paid 10 bandages.
Xarpus died at tick 862 after 8 bandages. Then the raider DIED in Verzik P2 at tick 1253, with
every bandage, all 11 brew doses and all 5 restore doses spent: 32 PASS, 1 FAIL (verzik.kill),
BLOCKED. Both halves therefore starve, the first in the Nylocas (2 of 10 runs survive) and the
second at Verzik. The levers are the ones the halves name: the Nylocas freeze rule and Ancients,
a fight-speed press verb, and the P2 lightning's missing Entry figure. A whole-raid author
starts with a kit decision that serves both halves, and that decision is still open.

## The whole raid: t.raid.enter gives you eight free potions unless you hold a br_ brew

`t.raid.enter('tob', <room>)` runs `~tob_debug_kit` (tob.rs2:764). It heals you, fills your
prayer and special energy and, if you hold no `br_4dosepotionofsaradomin`, adds four of them
and four `br_4dose2restore`. That is 32 free doses and eight slots. A raider who walks into the
room has none of them. The closer's first measured-kit run (close13_kit) held plain brews, so it
received the gift. Its chest paid 8 bandages instead of 10, because the gift filled 8 slots. A
relay leg that stands in for a walked-in raider must drop the eight (`t.player.drop`) or hold a
br_ brew from the start, and it must say which it did. The second half's scratch held br_ brews,
so it never received the gift.

## The whole raid: the first half, Ver Sinhaza to Sotetseg's entrance (RECIPE, seam13)

Scratch: build/seam_state/matthew-mbp-m4-raid-b1-seam13/relay1/relay_lobby_to_sotetseg.lua
(assembled by build.sh from relay_a_*.lua; dev copies dev_maiden.lua, dev_bloat.lua,
dev_nylo.lua enter a room with t.raid.enter for tuning only). test/raids/fixtures/ holds no
scratch examples, so nothing was copied there. Green once (s13_relay_a9 92/92), survived twice
(a7), died in the Nylocas eight times of ten: the half is playable by click and starves in the
Nylocas.

KIT (one, from the wiki Entry Mode tabber and the Melee setup; bring-alongs): 99 in every combat
stat. Worn: void ranger helm (game_pest_archer_helm), amulet_of_glory, ma2_saradomin_cape,
elite_void_knight_top/robes, pest_void_knight_gloves, dragon_boots, nzone_berzerker_ring,
magic_shortbow_i, rune_arrow 1000. Carried (28): ::fullscythe (scythe_of_vitur),
game_pest_melee_helm, staff_of_fire, airrune 1000, mindrune 500, dragon_dagger_p++ and
slayer_boots (for Verzik), 4dose2combat, 4dosestamina, 4dose2restore x6, 4dosepotionofsaradomin
x13. Measured choices: a twisted bow killed the Maiden no faster than the shortbow (343 vs 341
ticks); the whip dealt 265 to Bloat in eight downs where the scythe killed it in four; the
trident of the seas killed fewer nylocas than Fire Strike (46 vs 72 by tick ~500); brews beat
sharks per slot (64 vs 20), and six sharks plus seven brews died in the Nylocas at 472.

MAIDEN (ten lines): Protect from Magic for the fight; equip the scythe and melee helm in the
corridor; super combat; click tob_arena_barrier op 1, 'Yes, begin the fight.'; target
maiden_elemental_story within 7 tiles first, then a blood slug adjacent, then her; track blood
throws (projectile 1578, landing = tick + ceil((end-start)/30)) and pools (map_spotanim 1579,
live 11 ticks: NOT a loc, hazard_at loc rows miss them); on a bad tile walk to the nearest clean
tile adjacent to her 6x6 footprint; eat (food, then a brew) under 55; restore when prayer < 20
or Strength < 85 (her drain lands on the stat behind the highest attack bonus: Strength with the
scythe). Kill: 200-270 ticks, 4-10 eats. After: HP/prayer restored, drained stats are not.

MAIDEN EXIT: tob_arena_barrier op 1 again (her arena has one opening; the corridor loops round
it), then tob_dungeon_walkway_exit_clickbox op 1 (local 40,6); room 2 is built under the blood
card.

BLOAT (ten lines): restore if the Maiden left Attack/Strength under 95; stamina; walk to local
(41,31); wait until his centre is west of the tank (x <= 28 local); super combat; barrier, 'Yes,
begin the fight.'; Protect from Missiles; every tick, unless inside a down window, walk to the
ring point half a loop from his centre (ring = the centre line 26..37 round the tank, 44 tiles,
led by two ticks of his speed along it, skipping shadow tiles: map_spotanim 1570-1573 live 5
ticks); on his down (npc_anim 8082 on his slot, read since your own mark: slots are reused
across rooms) attack with the scythe until T+23, then run back opposite (stomp T+29, hunt range
6 from his SW tile); restore when Attack/Strength < 90. Kill: 4-6 downs, 209-426 ticks, 23-299
taken.

BLOAT EXIT AND CHEST: tob_arena_barrier with at = local (23,31),(24,31),(23,30),(22,31), retried
until the tile is west of local 23 (one run's click landed and did not cross); walk to local
(9,32); drink out the super combat and stamina, drop vial_empty; click tob_midway_chest_closed
op 1: 'You take N bandages from the chest.' with N = min(10, free slots), a second Open 'The
chest is empty.'; then tob_dungeon_walkway_exit_clickbox op 1 (local 5,31).

NYLOCAS (ten lines): equip the ranger helm and shortbow; combat tab style slot 1 (rapid,
varp43_com_mode 1); auto-retaliate off (combat_interface:retaliate, varp172_option_nodef 1);
walk to fight.x+1; barrier, 'Yes, begin the fight.'; each tick read every
tob_nylocas_{big_fighting,fighting}_{melee,ranged,magic}_story within 16, pray against the
weighted majority (a big counts two, melee only within 4); press the unprotected aggros (bigs
first) with their own style's weapon (scythe / shortbow / staff_of_fire + Fire Strike), else
chewers (tob_nylocas_incoming_*_story within 16), a chewer near a support under 75 percent
before any aggro, any chewer before a small aggro while HP > 70; Vasilias by her form's weapon,
prayer to her form; eat one food plus one brew under 72 (84 with two aggros, 92 with four);
every third brew dose a super restore instead (a restore pressed behind a brew is refused by the
potion delay); stop on 'boss gone'. Kill: ~740-790 ticks, 880-1179 taken.

NYLOCAS EXIT: tob_arena_barrier op 1, tob_dungeon_walkway_exit_clickbox op 1 (local 39,51): room
4, Sotetseg unstarted, track 584 on arrival. What the raider has there: kit_at_sotetseg.md in
the seam13 state dir.

PRESSES: t.player.inv_op / equip block 3-17 ticks per press in a raid room (the settle never
settles while waves move); the scratch presses through t.player._inv_press until a fight-speed
verb exists.

## The whole raid: raid-wide rows the first half measures (seam13, s13_relay_a9 and s13_relay_d1)

lobby.regions 14642 (region of t.world.tile in Ver Sinhaza); lobby.notice_board_loc 32655,
entrance_loc 32653, scoreboard_loc 32987, gravestone_chest_loc 32656, deposit_box_loc 32665
(t.world.loc_near <symbol> -> row.id); lobby.stranger_present 1 (t.npc.nearest tob_stranger);
board.first_read_prompt 1 (chat.play: mesbox, options 'Not very experienced. I'll start with
Entry Mode.', mesbox); party.list_title 'Performers for the Theatre' (t.ui.text
tob_partylist:frame 1); party.list_empty 'No parties are currently listed.' (tob_partylist:list
45); party.default_mode 'Mode: Entry' (tob_partydetails:mode 0 after two Refresh presses);
hud.party_status_lobby_party 1 (varb6440 after Make party); door.ready_check 'Is your party
ready? Members: 1. Mode: Entry.' (t.chat.options_title; the row's text is the solo Normal one);
chat.enter_line 'You enter the Theatre of Blood (Entry Mode)...'; hud.room_status_inactive 0
(varb6447 before the Maiden's barrier); hud.party_status_in_raid 2 (varb6440, door path);
hud.room_status_boss 1 and hud.party_status_fight 2 (two ticks after the begin);
hud.boss_hp_full 1000 (varb6448); hud.orb_full 27 (varb6442); music.maiden_sorrow 570 (arrival),
maiden_anger 569 (begin), bloat_nightmare 578 / bloat_continues 571, nylocas_dance 580 /
nylocas_arachnids 579, sotetseg_room 584 (tick-log music rows, source script, read since a mark
at the door or passage); card.sound 3952 at each room's arrival (4 rows: door, two passages,
Sotetseg); music.boss_defeated_jingle 250 one row per kill; chat.wave_complete_line per room:
'Wave '<room>' (Entry Mode) complete!<br>Duration: <col=ff0000>m:ss</col>' (read by the room's
name right after each kill; the message ring does not keep the Maiden's line until the Nylocas);
restore.on_clear 99/99 hitpoints and prayer 12 ticks after the Maiden's death; chest.supply_loc
32758 (loc_near tob_midway_chest_closed from local 9,32); chest.entry_bandages = min(10, free
slots) ('You take N bandages from the chest.'; 10 only with ten free slots);
chest.entry_bandages_no_carry (a second Open: 'The chest is empty.').

## The whole raid, second half: Sotetseg's entrance to Ver Sinhaza (RECIPE, seam13)

Scratch: build/seam_state/matthew-mbp-m4-raid-b1-seam13/s13b/relay_second_half.lua, green as
s13b_full_c (55/55) and s13b_full_e (58/58). test/raids/fixtures/ holds no scratch examples, so
the scratch stays in the state dir.

Kit (the assumption, slot by slot). Worn: armadyl helm/chest/skirt, twisted bow, dragon arrows,
insulated boots (`slayer_boots`; `::setlevel slayer 37` first, or ::wield refuses), ferocious
gloves, infernal cape, berserker ring, amulet of torture. Pack: `::fullscythe`, torva chest and
legs, 2 br_4dosepotionofsaradomin, 2 br_4dose2restore, 4 tob_bandages, 6 sharks. That leaves 11
free slots: the chest pays min(10, free), and the Dawnbringer needs 1. Hold a
br_4dosepotionofsaradomin, or `::tobmode` adds 8 free potions (tob.rs2 ~tob_debug_kit).

Eat order: a bandage first (heals 20 and re-boosts), then sharks, then brews with a restore
after every second dose; restore when prayer is under 25.

Sotetseg (about 250-315 ticks, 1-2 bandages). Click `tob_arena_barrier`, then 'Yes, begin the
fight.'. Protect from Magic. Bow from his SW corner +2,-6, re-pressed after any eat or step and
every 12 ticks. A maze is an npc_retype on his slot: wait for level 3, wait to proc+4, take the
lit path from the level-3 loc_set rows since the proc, and step_tick one tile a tick (north
first, else west, else east). Step north off the grid AT ONCE at the end, so the tornado (one
tile behind) never reaches you. Wait for level 0, then swallow the re-activation retype so it is
not read as a third maze.

After Sotetseg: click `tob_arena_barrier` (the exit gate). `loc_near('tob_midway_chest_closed')`
is at 6417,69. Walk to z+3 and click it: 'You take 10 bandages from the chest.'; a second open
says 'The chest is empty.'. Then click `tob_dungeon_walkway_exit_clickbox` and poll
`t.raid.state().room == 'xarpus'`.

Xarpus (about 230-280 ticks, 6-9 bandages). Equip the scythe, torva chest and torva legs. Click
the barrier from start+(0,-3). Phase 1: walk onto every exhumed (loc_set 32743); Piety on the
3rd. HIS BODY: the static form's row (6433,162 here) is NOT the standing body. Read the npc_tile
row of type 10768 on the stand-up tick (6432,161; 5x5); standing on the static corner is
standing under him. Phase 2 is seam10's s10x_wiki recipe on that body. Add one override: a
puddle under you (loc_set 32744 on your tile) means step off before anything else, and eat at
any time under 45. Phase 3: wait on a ring-2 tile in your quadrant. On each new npc_face row
away from it, press once, wait for the swing (player_anim 8056), and walk back to the base.
Never stand adjacent between swings: a run that kept swinging was retaliated for 67+8 and died.

After Xarpus: the wave line comes 3 ticks after the dead-form retype. The exit is the NORTH
barrier, m49_68 local (33..35,43) = start tile +15: `click_loc('tob_arena_barrier', 1, {at =
{start.x, start.z + 15, 1}})`, because the south one is as near. Then
`click_loc('tob_skeleton_with_weapon', 1)`: an objbox, one `continue_`, and
verzik_special_weapon is in the pack (it needs a free slot). Then
`click_loc('tob_dungeon_xarpus_arena_door_exit', 1)` lands you at 6432,78 in Verzik's room, the
same coordinates as `t.raid.enter`'s handle-1 room (her P2 body SW 6431,89).

Verzik (about 490-550 ticks; it eats everything left). Equip the armadyl chest/skirt back and
wield the Dawnbringer. `talk_to('verzik_initial_story')`, then chat.play {'npc:So, you wish to
entertain me', 'options', 'choose:Yes, begin the fight.'}. P1: the Dawnbringer alone. Special
(combat tab special_attack, then attack) whenever varp300 >= 350, autos otherwise; 2-3 specs
plus autos end it in about 60 ticks. T12: bow, Rigour, Protect from Missiles, home 6427,86. P2:
a crab within 5 tiles first, then the Athanatos (it heals her about 10 every 5 ticks), then her.
Hold off her from the 8117 summon to +4. Protect from Magic on a 1591 projectile, back to
Missiles on 1583. P3: vz12's rules (prayer on 8124/8125, webs, pool, flee every tornado, never
adjacent).

Vault: poll `loc_near('tob_dungeon_verzik_throne_door_opened', 30)` with t.ticks(1); never
inside t.await. It appears 8 ticks after her death (throne 32737 +5). Click it. Then check
varb6450 == 2 and that loc_near tob_treasureroom_chest_loc0 is ok and loc1 is not_found (solo).
Click chest_loc0; `t.ui.await_open('tob_chests')`; the opened loc is 32994.
`t.ui.invoke(t.ui.widget('tob_chests:inventory'), 1)` is Take-all (::tobvault items=[] flag=0).
Then escape, and `click_loc('tob_treasureroom_teleportout', 1)` lands at 3677,3219 with varp5893
0 and varp6826 0 -> 0 (Entry).

## The whole raid: raid-wide rows the second half measures (s13b_full_e)

The raid-wide rows this half reads (s13b_full_e): music 584@36, 581@41, 567@403, 564@413,
566@671, 572@681, 582 on the vault teleport tick (0 ticks; the first player_tile row in the
vault is one tick later); jingle 250 x3 (one per boss death, +4 ticks); card sound 3952 on each
room arrival plus the vault; trapdoor_after_throne 5; hud 6440 2/2, 6447 0 then 1, 6448 1000 at
the start, orb p0 27; chest supply_loc 32758 and entry_bandages 10; restore.on_clear (hp and
prayer back after each kill); vault varbit 2, open loc 32994, crystal 32996, interface
tob_chests; completion.entry_counts 0 -> 0; the wave lines and 'Theatre of Blood total
completion time'. Not reached: the death, wipe and onion rows (a death costs the run), and
unlock_bits (the first half's tracks are missing).

## Bandages heal now: 20 hitpoints, the combat boosts and a stamina dose, no prayer

Bandages heal now (seam13): 20 hitpoints, att/str/def +floor(L*15/100)+4, ranged +floor(L/10)+4,
magic +4, cure poison, and the stamina effect, as food (combo-eat with potions). They do not
restore prayer: no source gives a figure.

## Insulated boots take 40 percent off Verzik's P2 lightning

Insulated boots (slayer_boots) take 40% off Verzik's P2 lightning (wiki item page); the Entry
page makes them mandatory solo. Without them a solo P2 took 42, 43, 34 and 77 and died.

## A wave-complete line: match the room's own name, never the last match

Reading wave lines: do not take the last match of t.msg.last(n). The ring came back newest
first, so the 'last' match was the oldest (Sotetseg's line read as Xarpus's). Match the room's
own name.

## t.ticklog.rows({}) over a whole raid runs out of instruction budget

A whole-log `t.ticklog.rows({})` with a Lua filter, run at the end of a 1300-tick relay, raised
'instruction budget exhausted (400000)'. Query one kind at a time (`{kind = 'music'}`).

## An arrival after a teleport: the player_tile row is one tick late

A player_tile row lands on the tick AFTER a teleport resolves. To date an arrival, use the jump
row's tick - 1.

# Seam pass 14: why one Entry solo raider could not carry the Theatre (2026-10-04)

Seam13 found three reasons the whole raid could not be authored: the driver's held presses cost
3-17 ticks each in a fight, the Nylocas took more hitpoints than the kit holds, and Verzik left
no supply margin. This pass gave the tests fight-speed presses, made the Nylocas' swings roll
accuracy and added the Entry freeze rule, and found that Verzik's damage sources are all at or
under their figures. The first half of the raid now survives 3 of 3; the second half still dies,
and what binds it now is its own recipe (prayer at Verzik, brews never drunk at Xarpus), not the
content. Scratches and run names are under build/seam_state/matthew-mbp-m4-raid-b1-seam14/.

## A held press costs 3 ticks, up to 17 in a wave room: use opts.quick, t.player.eat, t.player.drink

`t.player.inv_op(item, op)` and `t.player.equip(item)` wait for the world to go quiet after the
click: a settle, three equal backpack reads, then a chat-line wait. That costs 3 server ticks a
press in Lumbridge and up to 17 in the Nylocas room, where the frame never settles. In a fight,
call one of these instead:

- `t.player.eat(item_or_list[, {op = n}])`
- `t.player.drink(item_or_list[, {op = n}])`
- `t.player.inv_op(item, op, {quick = true})`
- `t.player.equip(item, {quick = true})`

Each one is the same real click on the backpack cell (never a server op). It returns on the tick
its effect can be read, one server tick after the press. On ok the detail reads
`<item> slot S op O: <before> -> <after> [WORN ...], read on tick N (pressed on N-1, +1)`, and
eat and drink add `hitpoints before -> after` as read (in a fight that includes any hit landing
on the same tick). A list for eat or drink means the first symbol held, so pass a potion's doses
in the order to drink them. Measured: the committed tob_verzik test with only its presses made
fast kills Verzik at tick 714, where the slow presses die in P3 at tick 854. Conformance:
player.eat, player.drink and seam.held_press_fight_speed (equip 1, eat 1, drink 1, quick inv_op 1,
slow inv_op 3-4 ticks).

## A fast eat or drink answered timeout "... not re-pressed": the food or potion timer refused it

A fast press is ONE click. Inside the food timer (food after food, 3 ticks) or the potion timer
(potion after potion), the server ignores the click without a word. The verb answers timeout:
`pressed on tick P, slot still <item> on tick P+2 -- no new chat line; not re-pressed (a fast
press is one click)`. A test that counts every press as a heal counts too many: a tob_nylocas copy
counted 39 presses and 36 landed. Read the result, and click again on the next tick if you need
the heal. A level requirement answers refused with the server's own sentence ("You need to have
an Attack level of 70."). On equip, ok means the worn total rose; on inv_op {quick}, ok means the
cell changed. An op that changes nothing (Check, Read) has nothing to read back, so use the slow
inv_op for it.

## ::give dragon_dagger_p++ gives the plain dragon_dagger_p (t.inv.count is right)

The server's cheat name match underscores its argument before the exact lookup
(torirs_server_world.c cheat_id_from_name), so "p++" and "p+" lose their "+" signs and resolve
to dragon_dagger_p. `t.inv.count("dragon_dagger_p++")` correctly reads 0, because the backpack
really holds dragon_dagger_p. Until the cheat looks up the exact symbol first, a kit with a p++
dagger holds the plain one: count it as dragon_dagger_p. (A conformance row for the fix,
seam.give_takes_the_exact_symbol, waits in the pass's state dir, unmerged.)

## How an eat is aimed into a queued hit's delay window (seam.eat_does_not_hold_queued_hit)

A cast is in the tick log at the reading of its own server tick R (`t.tick()`). A press made at
reading R is processed in tick R after the npc, so the eat's player_anim row is stamped R. Eat the
moment a cast row with tick == t.tick() appears, and the eat lands on the cast tick, between the
hit being queued and the hit landing. Read the eat's tick from player_anim (seq 829), not from
when the verb returned. The pre-port food.rs2 (p_delay(^eat_delay)) holds a young dark wizard's
+1 hit to +3; the ported content lands it at +1. A bow fight puts the wizard at range, where the
plain delay is already +2..+4: take the bow off, because an eat's hold can only be seen when the
plain delay is at most +2. The row was rewritten this way in seam14 and fails on a scratchpad
copy with the pre-port food.rs2 (+3), so it now discriminates.

## The press costs in the kept rooms, slow against fast

Same tree, copies of the kept tests: tob_nylocas equip 94 -> 29 ticks, eat 174 -> 36. tob_verzik:
killed at tick 714 with 37 eat/drink animations, against a P3 death at tick 854 with 41. A room
re-authored to use fast presses will move its damage-taken, supply and kill-tick rows and roll
differently (the verzik p1_cap bare-fist max went 10 -> 7). Record a moved number with both
figures; it does not mean the room broke. npc.await_dead_engaged's opts.eat still eats through
the slow inv_op.

## A nylocas swing at 0 with no prayer up: the waves and Vasilias now roll accuracy

A wave nylocas's swing and Vasilias' swing roll the npc's attack roll against your defence roll
(Ischyros stab, Toxobolos ranged, Hagios magic), then 0..max (tob_nylocas.rs2 ~tob_nylo_hit_roll;
LostCity npc_combat_melee.rs2:27-28; cache_npc_nylocas.txt stats). Before seam14 every swing
landed for 1..max. A 0 hit_player row with splat 26 is a miss OR a prayer block, and the two look
the same, so never infer the overhead from a 0: a row that counts prayer blocks must count only
swings of the prayed style. Measured with Defence 99 and no armour: 5-8 of 14 unprayed aggro
swings landed (conformance seam.nylocas_swing_rolls_accuracy).

## A frozen chewer stops biting (Entry only)

An Ice Burst or Barrage on a nylocas chewing a support freezes it, and in Entry Mode it does not
bite until it thaws (Entry Mode page :166; ~tob_nylo_pillar_tick). Measured: bites at t65 and t68,
the Ice Burst lands at t69, the next bite at t85 (+16). A wrong-colour spell still nulls the
nylocas (0, splat 26), so a frozen grey or green one is out of the fight until it detonates at
52 ticks. In Normal and Hard it keeps biting every 3 ticks (the Strategies page says only
"usually"). Measure the freeze with a bite-animation count on the slot (seqs 7989/7999/8004), not
with t.player.cast's result: the cast answers timeout on a nulled target because no Magic XP is
given. Conformance: seam.nylocas_frozen_no_bite_entry.

## A hit_player row with dealer -1 that is not a support collapse

A nylocas killed while its projectile is in flight is freed before the impact, so the impact is
written with npc_slot -1 and npc_type -1, exactly like a collapse row. Now that a swing can miss,
such a row can be a 0 on a collapse tick, and a row that takes every dealerless hit on a collapse
tick counts it (a tob_nylocas copy read 0,31,36,41). Filter collapses by damage >= 30 (Entry) or
by the support's npc_free tick.

## The Nylocas within one kit (RECIPE)

With the accuracy roll in, seam13's plan (aggro swingers first, the protection prayer of the
aggro majority, then the chewers of a support under 75 percent, Vasilias by form) takes 361-441
in the room scratch and keeps 9-10 full brews (nylo/nylo_room.lua). In the relay first half on
the final tree it took 392, 279 and 330 (close14_r1a/b/c, all three green) and left 6-8 full
brews and 2-3 full restores. That is roughly 175 from wave swings, 36-84 from 1-2 collapses,
15-58 from explosions and about 112 from Vasilias. What a human adds when a kit is tighter (Entry
Mode page :160-166): burst or barrage clumps you will not deal with soon, since in Entry a frozen
nylocas cannot chew; kill greens first with the fastest ranged weapon; stay near the centre
unless you are clearing greys; let a support that is about to fall go and keep the other three
up, because collapses stack at 30+ each. To split a run's damage by source:
`python3 build/seam_state/matthew-mbp-m4-raid-b1-seam14/nylo/an_hits.py <run dir>`.

## Verzik's P2 prayer: hold Protect from Magic from the first reds summon

The blood spell is checked against your prayer at the cast (~tob_verzik_blood_spell;
Verzik_Vitur:397 "calculated during her attack animation"), so switching on the 1591 projectile
row is always one tick late. The seam13 second-half scratch switches to missiles on every 1583
urnbomb and took 33 blood hits (241 damage) while Verzik healed 404 against a 400 P2 pool. Holding
magic from the first 8117 seq (Entry Mode :231 "swap to Protect from Magic") cut the blood
damage to 0-3 and moved the deaths into P3 (s14v_kit_a/b, verz/relay_second_half_magic_after_reds.lua).

## The second half starves on prayer once the blood spell is prayed

With 5 restore doses, s14v_kit_a/b reached P3 at 24-26 prayer and died at 0-6 ("You have run out
of Prayer points" seven times). The bandages restore no prayer in this tree (no source gives a
figure). Budget restores for P2 and P3. The first half now leaves 11-14 restore doses, not 5.

## Where Verzik's damage comes from (classify a run)

`python3 build/seam_state/matthew-mbp-m4-raid-b1-seam14/verz/classify.py <ticklog.tsv>` groups
hit_player rows by phase and source: zap 1585, bomb 1583, blood 1591, autos 1593/1594, ball 1598,
tornado npc 10846, crab explosion map_spotanim 1565. In five Entry runs every source was at or
under its spec max: P1 bolt 15 (60, 30 prayed), P2 bomb 8 (16), zap with boots 25 (28), blood 44
(45), crab explosion 60 (63), P3 autos 10 prayed (20), ball 74 (75 percent of the HP level). The
lightning is the long sink: 8-12 zaps per P2 (110-146 damage), because a long P2 means more of
them.

## Entry chest bandages: min(10, free slots) on the first Open, then "The chest is empty."

Superseded by seam15: the chest now keeps what you could not carry (see "An Entry supply
chest keeps what you could not carry" below). Kept for the runs before it.

An Entry supply chest hands over as many bandages as you have free slots, up to 10, and a
second Open says "The chest is empty." Free 10 slots BEFORE opening. The wiki says the leftovers
stay until the next chest (CONTENT_BUGS.md, seam14; a patch is proposed, not applied).

## Insulated boots: 60 percent of the P2 lightning roll

`^tob_verzik_p2_zap_boots_pct` (tob_verzik.constant) is 60, from the item page's "by 40%". The
Strategies page says "25 if wearing insulated boots" (52 percent of 48); both are quoted there.
With boots one zap lands 0-28, every fifth P2 attack.

## The whole raid after seam14: the first half survives, the second half does not yet

First half (seam13 relay1/relay_lobby_to_sotetseg.lua) on the final tree: 3 of 3 green
(close14_r1a 99/99, r1b 97/97, r1c 98/98). Seam13 had 2 of 10. The Nylocas took 392, 279 and
330, and the kit at Sotetseg's entrance was brew(4) 6-8 and restore(4) 2-3: 24-34 brew doses
and 11-14 restore doses, where seam13 measured 11 and 5.

Second half with seam13's measured kit (close/relay_second_half_measured_kit.lua): 0 of 3
(close14_r2a died in Xarpus at tick 908; r2b and r2c died in Verzik P2 at 1290 and 1273), every
supply spent. With the kit the first half now leaves (the least of the three:
close/relay_second_half_seam14_kit.lua): 0 of 3, all three identical, dead in Verzik P2 at tick
1680 with 24 brew doses and 11 restore doses spent. With that kit and Protect from Magic held from
the first reds (close/relay_second_half_seam14_kit_magic_after_reds.lua): 0 of 3, all identical,
dead in Xarpus at tick 850 having drunk only 4 brew doses, because the scratch's Xarpus loop heals
from bandages alone. The two halves now differ by the second half's recipe, not by the content:
the Xarpus loop must drink brews once the bandages run out, and Verzik's P2 must hold magic and
restore prayer. These second-half scripts gave the same result under all three run names, so
three names do not sample three outcomes.

## The raider dies three ticks after Verzik: her in-flight hit lands after her npc_death

- Her P3 auto is a projectile; one launched before her last hitpoint goes still lands after
  the npc_death row. A tob_verzik run (rooms-tob launch 12) killed her on tick 824 and took
  her 825 hit (13, raw 16) on 13 hitpoints: death anim 836 on 827, chat 'You have died.
  Death count: 1.' The room counts as won (a death in a won room gets up in place, see
  "The cage" above), so every row after it passed, the vault readout included.
- A room is not green on a death. Assert no 'You have died' line from the room's start to
  the boss npc_death row PLUS the flight of her last launch (6 ticks), and end the fight
  with hitpoints above her prayed max (10 Entry) plus the unprayed max when prayer is out.
- That run had no anglerfish from tick 342 and drank last on 704; the tornado touch on
  681 healed her 26 -> 173 (147 = 3 x 49) and the extra 140 ticks starved the kit. Take the
  touch with her pool high enough that 3 x your half-hitpoints does not undo P3.

## Her tornado outlives her: a tornado row can pick a hit after the kill

Superseded by seam15: no tornado touches or respawns after her last hitpoint (see "Her tornado
goes with her" below). Kept for the runs before it.

- Verzik's npc_death does not free her tornado (10846). In that run the one spawned on 697
  chased the raider (who had died and got up at 99) and touched him on 841, 17 ticks after
  her death, then npc_free 842. A row that keeps "the first tornado hit that verifies"
  took that one: spec.verzik.p3_tornado_pct read 49 of 99 on 841 because hp_by_tick had no
  reading at 679/680 for the in-fight touch on 681.
- Filter every tornado row (pct, heal, respawn, av.tornado.seqs, av.p3_enrage.tornado) to
  hits before her npc_death, and read hitpoints on the tick before the touch (poll every
  tick while a tornado is within 2), or rebuild it: 681's 49 + her 2 that tick on a reading
  of 48 after = 99 before.
- Whether the tornado should vanish with her is unsourced in sources/ (the blert plugin
  only "fully despawns tornadoes at the end", VerzikDataTracker.java:405-407): triage it.

## A super restore heals 32 hitpoints here (content defect)

Fixed in seam15 (see "A super restore no longer heals Hitpoints" below). Kept for the runs
before it.

- `[proc,super_restore_effect]` (prayer_potion.rs2:119-143) runs stat_heal(hitpoints, 8, 25)
  with every other stat (line 125), and br_4dose2restore calls the same proc
  (br_potion.rs2:74). Below base, a dose heals 8 + 25% of base, 32 at 99, capped at base.
  OSRS's super restore restores every stat except Hitpoints. Until it is fixed a restore is
  a heal in every raid kit (the nylocas and verzik runs of rooms-tob launch 12 both lean on
  it): do not plan a kit on it, and do not measure a hit against a hitpoints reading taken
  across a restore.

## Strength at a Vasilias swing: rebuild it from the drink animations

- The ticklog has no stat row. A drink is player_anim 829 (brew and restore alike); tell
  them apart by the test's own brew/restore counters on the trace rows. Observed at base 99:
  a Saradomin brew takes 11 from Strength (77 -> 66, 99 -> 88), a super restore adds 32
  capped at 99 (66 -> 98), and both run ~player_combat_stat, so the next swing rolls with
  the new level. The whip swing lands on its own anim tick (1658 on the heal tick). A
  reflect's max is the level after the last drink before that tick: tob_nylocas
  (d87427d41) 22 on 729 after a brew on 713 and a restore on 718 = 98, max 24.

## A super restore no longer heals Hitpoints (seam15)

- `[proc,super_restore_effect]` (prayer_potion.rs2) restores every stat except Hitpoints by
  8 + 25% of base. That covers `4dose2restore`, `br_4dose2restore` (the Tombs' supply copy) and
  the Castlewars brew. Before seam15 every dose also healed 32 hitpoints at 99.
- Source: wiki [Super restore] oldid 15183989, line 53 ("restores all player stats that have
  been lowered, including Prayer, but not Hitpoints"), pinned under
  docs/minigames/theater_of_blood/sources/. Row: seam.super_restore_no_hitpoints.
- A kit sized against the old heal needs brew doses or food for those hitpoints. A restore now
  buys back Prayer and the brews' stat drain, nothing more. tob_maiden and tob_verzik moved
  because of this (SEAM_LEDGER.md, seam15).

## Taking a raider under base Hitpoints without a cheat

- There is no `::damage` cheat. Cave nightshade (`t.player.eat("nightshade", { op = 4 })`) hits
  15 a bite (nightshade.rs2) and can be eaten every 2 ticks.
- A Saradomin brew dose drains Attack, Strength, Ranged and Magic by 2 + 10% of base
  (99 -> 88 -> 77 -> 66 -> 55), raises Defence to base + 21 (since seam18; it drained Defence
  before) and lifts Hitpoints to base + 16. Drink before you bite. A second drink 2 ticks after
  the first is refused by the potion delay: wait 3.
- Recipe: build/seam_state/matthew-mbp-m4-raid-b1-seam15/srh/srh_scratch.lua.

## Her tornado goes with her (seam15)

- While she lies dying (npc_death to the bat's npc_spawn, 3 ticks), each tornado keeps walking
  (npc_tile rows) but cannot touch or heal. On the bat's spawn tick every tornado plays 9005,
  and its npc_free comes one tick later. No tornado spawns after her npc_death.
- Row: seam.verzik_tornado_gone_with_her (her death 2454, bat 2457, 9005 2457, npc_free 2458,
  no touch). Spec row verzik.p3_tornado_end (0 ticks, grade D).
- Sources: blert VerzikDataTracker.java:446-456 (every tornado despawned at the death form) and
  Near Reality PurpleTornado.kt:34-37,54 (no contact or respawn once she isDead). In 62 Blert P3
  rooms the tornadoes move between her last hitpoint and the death form (441 rows) and none is
  listed after it (sources/blert_api/spec_pass_verzik/verzik_seam15_death_2026-10-04.txt).
- A tornado row no longer has to filter out touches after the kill. Still read hitpoints on
  the tick before a touch.

## Her last auto lands after she dies (sourced, not a bug)

- An auto she launched before her last hitpoint lands 5-6 ticks later, after her npc_death.
  Once she is freed the hit_player row carries dealer -1 (scratch vzt_kill_b: launch 68, hit 3
  on 74).
- In 15 of 62 Blert P3 rooms a raider's hitpoints drop after her zero tick, 3-6 ticks after her
  last launch, and one raider died from such a hit. Near Reality's blockIncomingHits(15) is not
  OSRS. Spec row verzik.p3_inflight_after_death (3-6 ticks, grade B).
- Keep her prayed max plus one unprayed auto in hand at the kill, and assert no
  'You have died' through npc_death + 6.

## ::give takes the symbol you typed (seam15)

- `::give`, `::wield`, `::spawn` and `::setvar` look up the argument exactly as typed first,
  then its underscored form (the display-name spelling), then a unique substring.
- So `::give dragon_dagger_p++` gives dragon_dagger_p++ (5698), not dragon_dagger_p (1231).
  Before seam15 any symbol with `+` (the poison tiers p+ and p++) silently gave the plain item.
  A kit that named p++ before seam15 carried the plain (p) dagger: compare old relay counts
  with that in mind.
- Read the item back with `t.inv.count(<symbol>)`; it takes the gameval symbol, not the
  display name. Row: seam.give_takes_the_exact_symbol.

## An Entry supply chest keeps what you could not carry (seam15)

- tob_chest.rs2: an Open gives min(left, free slots); the rest stay in the chest until ten have
  been taken; "The chest is empty." comes only after ten.
- Source: wiki Theatre of Blood/Entry Mode:7 ("Leftover bandages in the supply chest do not
  carry over from each boss") and Bandages (Theatre of Blood) ("The chest can only contain a
  maximum of 10 bandages"). Row seam.tob_entry_chest_keeps_leftovers (3 -> 8 -> 10, then empty).

## The whole raid in one run

The recipe seam15 measured: one raider, one kit, from the notice board in Ver Sinhaza to the
vault and back out, with no `t.raid.enter`. It is green under ONE run name of five; it is not
yet a test (SEAM_LEDGER.md, seam15).

- Scratch: build/seam_state/matthew-mbp-m4-raid-b1-seam15/trj/joined.lua. It is generated:
  assemble.py joins seam13's first half, `R.between_halves` and seam14's closer second half
  into joined_base.lua, and plan.py applies plans 1-12 and 14-16 (each commented with its run).
  Run it with `run.py --script <joined.lua> --name <name> --no-build --no-publish`; a run takes
  about three minutes.
- Kit (`R.KIT`, given once at the notice board). Worn: void ranger helm, glory, imbued
  Saradomin cape, elite void top and robe, void gloves, dragon boots, berserker ring, magic
  shortbow (i), 1000 rune arrows. Carried, 28 slots: scythe of vitur, void melee helm, staff of
  fire, 1000 air, 500 mind, dragon dagger(p), insulated boots (`slayer_boots`, needs
  `::setlevel slayer 37` or it silently stays in the pack), super combat(4), stamina(4), 6 super
  restore(4), 13 saradomin brew(4).
- Between the halves (Sotetseg's entrance): drop the staff, the runes and the vials, wear the
  bow and the ranger helm. Take each chest's ten with ten slots free.
- Verzik P2: Rigour and Protect from Missiles, Protect from Magic from the first reds summon.
  Step one tile off every bomb (1583, 4 ticks) and every Athanatos aim (1586, 7 ticks) inside
  the box. Hit the Athanatos with the dragon dagger(p): one poisoned hit bursts it, for up to
  70 to her (Entry Mode:93 "must be poisoned ... not optional"). Then the bow goes back on. Run
  from any exploding nylocas whose 2x2 is within 5 tiles, to the tile 2-4 off that is farthest
  from it (Entry Mode:228 "or just run away").
- Verzik P3: the seam12 prayer rule. Flee a tornado within 6 to a ring tile whose diagonal-first
  route stays 2 clear of it (it moves 1 tile per 2 of yours). Flee before you eat when it is
  within 4. The green ball is 75% of your Hitpoints level (74 at 99): be above 75 when it lands.
- Supplies at each room's exit, the green run s15k1 (re-run by the closer on the final tree,
  160/160, the same ticks): Sotetseg's entrance 18 brew and 8 restore doses, 0 bandages; after
  Sotetseg 2 brew + 1 restore used, chest 10; after Xarpus all 10 bandages + 3 brew + 1 restore
  used; Verzik dead on tick 2919, everything drunk, the raider at 8 hitpoints.
- The other names: s15j7 died in Verzik P3 at 2972 (food out, her pool ~163 short); s15j5b died
  in P3 at 2986 (prayer out at 2880, ~338 short); c15alpha died in P3 at 2921 (16 brew and 7
  restore doses at Sotetseg's entrance); c15bravo died at Xarpus at 2410 (missed 39, 16 brew and
  6 restore doses); c15charlie never killed Xarpus (no food from about 2700, 12 brew and 4
  restore doses).
- What decides it: the first half's Nylocas leaves 12 to 21 brew doses depending on the name,
  and the second half needs about 18. The sourced bounds are two chests of exactly 10 bandages
  (Entry Mode:33, :151, :197), the 28-slot kit, the P3 pool 600 (cache_npc_verzik.txt:552) and
  the P3 green ball at 75% of the Hitpoints level.
- Raid-wide spec rows: `raid_coverage.py tob_entry` grades only `spec.<id>` steps, so it reads
  0 of 75. The green ledger has a `raidwide.<id>` step for 42 of the 75 (41 PASS; the one FAIL
  was the duplicate music row plan 16 drops). The other 33 are death, wipe, logout, party,
  reward-roll and vault rows one surviving solo run cannot emit. The mapping, row by row:
  build/seam_state/matthew-mbp-m4-raid-b1-seam15/raidwide_rows.tob_relay_joined.tsv.

## The run name seeds every roll, but only its first 12 characters, case-folded

- strtobase37 (src/net/jbase37.c:29) stops at 12 characters and maps A-Z like a-z. So
  close14_r2k_a/b/c were one seed (prefix close14_r2k_), which is why seam14's three second
  halves were identical.
- Name runs 12 characters or fewer and make them differ early (s15k1, s15j7). To A/B a plan
  change, rerun the SAME name: the run is byte-identical until the change acts.

## A death ends the run before the drive notes flush: read the tick log

- player.died aborts run(), and the drive notes after the last flush are lost.
- trj/hits.py (damage by dealer and the nearest projectile per tick window), trj/vtl.py (a
  Verzik timeline) and trj/summ.py (a ledger digest) rebuild the fight from ticklog.tsv.
- npc_tile columns are slot, x, z, level, type; npc_heal is slot, type, amount, hp, max.

## The joined second half must not re-measure the first half's tracks

- The second half's base is the `second_half` mark, set after Sotetseg's room track (584) has
  played, so its copy of raidwide.music.sotetseg_room read nil. The first half reads that track
  from the Nylocas passage. Plan 16 drops the duplicate.

## A hit on the tick you step into range is sounded against your old tile

- tob_maiden's spec.maiden.av.hit_sound (seam15 close run) counted a hit on tick 355 as
  unsounded: the raider ended that tick 12 tiles from her south-west tile (6438,100) but began
  it 13 away (6439,101). The likely reading (not traced in the C): the engine judged the range
  against the tile he stood on when the hit resolved, and the row read the tile he ended on.
  It is the T-1 rule again (ENCOUNTER_TIMING.md section 1).
- Count a hit whose two tiles straddle the 12-tile edge as neither near nor far, or judge it from
  the tile at the end of tick - 1.

## Verzik's Entry solo table is 144 rows since seam15, and its P3 frames are the kill screen

- `verzik.tsv` has 212 rows. `verzik.scope.tsv` drops 68 of them for Entry solo (party and
  Normal-only figures), which leaves 144 in scope. Seam15 added two of them:
  `verzik.p3_tornado_end` and `verzik.p3_inflight_after_death`. An older brief that says 142
  predates seam15. Coverage FULL means 144 of 144.
- Rows whose ledger lines come after `fight.loop_end` are graded from the tick log after her
  npc_death. That covers the enrage, tornado and P3 death rows, plus the `drive.*` notes. Their
  shots show the kill screen ("Verzik Vitur has fallen"), not the moment they name. For
  example, `p3_tornado_pct` names a touch on tick 634 of a 772-tick fight. To check one of
  these rows, read the tick-log rows (hit_player, npc_heal, npc_spawn 10846) at the tick its
  detail quotes. Do not look for the moment in the PNG. Rows before `fight.loop_end` are still
  frames from the fight: P1 and P2 at their phase's end, and `p3_proj_flight` mid-P3.
- Sampled at launch 13 (tob_verzik d1b4ccf00).

## Maiden's blackstorm always lands; a prayed Entry hit above 9 means crabs leaked (seam16)

- The blackstorm has no accuracy roll: "The attack always lands as a [[successful hit]]"
  (wiki Theatre_of_Blood/Strategies:590). Protect from Magic halves it. Under the prayer in
  Entry a hit is floor(floor(floor(36.5 + 3.5c) / 2) / 2): 9, 10, 10, 11, 12, 13, 14 for
  c = 0..6 Matomenos that reached her.
- A prayed hit above 9 does not mean the prayer failed. Measure c from the tick log: an
  npc_heal on her slot on the same tick as a crab's npc_death is a leak.
- Spec rows maiden.auto_land_rate_entry and maiden.auto_prayed_entry (D). Row:
  seam.maiden_blackstorm_always_lands_entry.

## Maiden's blood: a hit with npc_slot -1 and dealer 0 is a pool, and it costs prayer (seam16)

- In the Maiden room a hit_player row with npc_slot -1 and dealer 0 is a blood pool or trail.
  A blackstorm carries her slot. s15k1's "18 before the prayer" was a pool at c = 4 (10 + 2c).
- Every pool hit also takes half its damage from your prayer (tob_maiden.rs2
  `stat_sub(prayer, ...)`). A raider left standing in a pool takes 10 a tick and loses 5
  prayer a tick, so Protect from Magic goes out within about 20 ticks. The conformance row
  tops prayer up every 5 ticks for that reason; a room test must step off the pool.
- Entry Maiden's own prayer cost is Protect from Magic's drain: 0.153-0.169 points a tick
  measured, prayer never below 80 over 230-250 ticks when the raider is off the blood.

## Restore presses are not doses

- A press inside the potion delay is refused, so a "restores 13" counter can mean 6 doses.
  Count doses from the backpack (the sum of dose x count) before and after a fight, as
  build/seam_state/matthew-mbp-m4-raid-b1-seam16/m16/analysis.lua does.
- On a brews-only kit, a relay that restores on Strength < 85 spends its restore doses on the
  brews' drain, not on prayer (6 doses at Maiden in m16a-c, prayer never below 80).

## Bloat's stomp needs line of sight (seam16)

- The stomp hits only a raider within 6 tiles of Bloat's south-west tile whom `~tob_bloat_sees`
  admits (the flies' near-side test). Standing behind the tank during a down is safe from it.
  Source: Entry Mode :134 and :145 ("out of his line of sight"). Row:
  seam.bloat_stomp_needs_sight.
- `::tobbloatlos <lx> <lz>` prints Bloat's own answer for a local tile (1 seen, 0 hidden, -1 no
  Bloat), and `::tobwarp <lx> <lz>` stands you there. Both are for scratches and conformance
  only, never inside a room test. The fight square is local 24..39 and the tank 29..34; the
  instance is the template moved by whole 64-tile blocks, so local = world - floor(world/64)*64.

## Bloat's hands: 14-16 per volley, one per tile (seam16)

- A volley rolls 16 tiles. A tile rolled twice is drawn once (one 1570-1573 shadow) and lands
  once (one 1576 splat), so a volley shows 14-16 graphics, as blert's drops do. Count hands by
  distinct tiles per tick, never by 16.
- `~tob_bloat_show_hands` still rolls the shadow look for every one of the 16, so a room run
  under its own id keeps its old tick log.
- Spec bloat.hand_tiles is 14-16 range. tob_bloat's own spec row still carries "16 ... exact",
  so the raid gate reports a spec and tolerance mismatch until it is re-authored.

## Verzik's P3 attacks can miss (seam16)

- Her ranged and magic autos and her melee are accuracy-rolled. The autos take her
  (level + 9) x (bonus + 64) roll (Entry 180, +20) against your defence roll at the landing
  ("damage is calculated upon impact", Entry Mode :243). The melee rolls against crush (:245).
- A raw 0 hit_player row from her slot is a miss. Under the matching prayer a landed 1 is
  halved to 0 too, so test misses unprayed.
- Measured: a void raider with Rigour is hit by roughly 65-80% of autos (tob_verzik copies:
  23 of 35, 14 of 24); the conformance row in rune armour at Defence 99: 8 of 17 autos and 2 of
  3 melee at 0. Spec verzik.p3_auto_miss_entry (C); verzik.tsv is 145 rows in Entry solo scope
  now, not 144. Row: seam.verzik_p3_attacks_roll_accuracy.

## The tick log is one log per session

- `t.ticklog.start()` is idempotent: a second call keeps the rows and answers the same start.
  A row that reads `t.ticklog.rows()` after an earlier row also fought the same boss sees that
  boss's old anims, retypes and sounds too (and the boss slot can be reused).
- Take `local _, from = t.tick()` right after the start and keep only rows with
  `tick >= from` (`since` takes a log serial, not a tick). seam16's new Maiden and Verzik
  conformance rows failed in the full harness until they did.

## Content A/B between two packs

- Build a private HEAD pack: `git -C OSRS-Content archive HEAD -- osrs239-content/server/scripts
  | tar -x -C <dir> --exclude='osrs239-content/server/scripts/build*' --exclude='*/selftest/*'`.
  Symlink every other osrs239-content entry and server/pack into it, then run
  `src/build_opt/sscompile --src <dir>/osrs239-content/server/scripts --out
  <dir>/osrs239-content/server/scripts/build --content-root <dir>/osrs239-content`. Do NOT run
  ss_allocate.py on it: it writes through the symlinks.
- Run with TORIRSSERVER_CONTENT and TORIRSSERVER_SCRIPTS set. Compare before and after under
  the SAME --name: the name is the seed, so a different name is a different fight.

## A fix that adds or skips a roll moves the whole room

- A content fix that adds or skips a `random()` call shifts every later roll of that entity's
  stream, and the room diverges after it. In seam16 a skipped random(4) shadow changed tob_bloat
  from tick 138 and killed its raider.
- Where the skipped call has no gameplay meaning, keep consuming it. Where it does (an accuracy
  roll), expect the kept room to need re-authoring.

## Reading a room's cost from its tick log

- Group hit_player rows by npc_type (column f), then by the seq that npc_slot played within 4
  ticks before the hit (npc_anim rows).
- Nylocas swing seqs are 8004 melee, 7999 ranged and 7989 magic; their detonations are 8006,
  8000 and 7992. Verzik's P3 regular attacks are 8123 melee and 8124/8125 autos. Bloat's stomp
  is the hit on down (8082) + 29.
- In the Nylocas room a dealer -1 row on a tick with loc_set 32864/32863 is a support collapse.
  In the Xarpus room, dealer pid 0 with npc_type -1 is the delayed crossing-acid hit, and
  npc_type 10768 is the splash or standing on acid.
- Scripts: build/seam_state/matthew-mbp-m4-raid-b1-seam16/s16r/hits.py, nylo.py, bloat.py,
  vzsum.py.

## Xarpus's absorbed share decides his poison

- Count npc_heal rows on him in phase 1 by the exhumed that sent them (rise tick from loc_set
  32743). An exhumed whose first orb (rise + 3) gets through counts as fully absorbed, and every
  later poison hit scales by (100 + absorbed%) / 100 before the Entry halving.
- Standing on each exhumed within 3 ticks of its rise keeps P2 and P3 poison at 2-4 in Entry.
  s15k1's late covering made it 4-6.

## The whole raid with the wiki's kit: 0 of 5 survive (seam16)

- Plan 17 in build/seam_state/matthew-mbp-m4-raid-b1-seam16/trj/plan.py is the wiki's kit
  (`--seam15-kit` gives R.KIT back byte for byte). Carried: scythe, void melee helm, staff of
  fire, air and mind runes, dragon dagger(p), insulated boots, super combat(4), ranging(4),
  stamina(4), 2 super restore(4), 6 Saradomin brew(4) and 10 sharks (Entry Mode :29-31, :33,
  inventory table :42-69).
- 0 of 5 names survive (w16alpha to w16echo): three die in Xarpus phase 2 before the screech
  and two in the Nylocas. Raiders who reach Sotetseg's entrance hold 9-11 brew doses and 0-1
  restore doses.
- `trj/digest.py <names...>` prints, per run name, the death room and tick, damage by room
  (hit_player rows split at the boss-death jingles) and the kill rows' eats and restores. The
  second half's used counts in sote.kill, xarpus.kill and verzik.kill are cumulative over the
  half: subtract to get one room.

## Brews drain Magic and Ranged: count restores against brews room by room

- A brew drains Magic and Ranged by 10% + 2 of the current level (wiki Saradomin_brew :56). With
  no restores left, about 12 brew doses put Magic under Fire Strike's 13: w16delta's Nylocas
  boss phase ended in eight "Your Magic level is not high enough for this spell" and a death
  with Vasilias at 17%.
- Defence is no longer drained: seam18 made sara_brew.rs2 raise it by 20% + 2 of base, as the
  wiki says (see "The Saradomin brew raises Defence (seam18)" below).

## The Saradomin brew raises Defence (seam18)

- sara_brew.rs2 is now `stat_boost(hitpoints, 2, 15)`, `stat_boost(defence, 2, 20)` and
  `stat_drain(attack/strength/magic/ranged, 2, 10)` (wiki Saradomin brew :56, :89; grade C).
  At 99 one dose reads Defence 120, Strength 88 and Hitpoints 115 on the next tick. A second
  dose leaves Defence at 120: `stat_boost` caps at base plus one dose.
- Rows: seam.brew_raises_defence and seam.brew_defence_no_stack.
- A room that brews now takes fewer accuracy-rolled hits. In the Nylocas waves the landed rate
  fell from about 19% to about 17%, so the roll sequence after the first brew diverges: a room
  tuned on the old brew must be re-run, not trusted.
- Brew drains step by the BASE level. The wiki's "current level" (:56, :114) conflicts with its
  own super-restore paragraph (:58), so it is an open row in CONTENT_BUGS.md (seam18). Do not
  change the drain without a second source: seam15's seam.super_restore_no_hitpoints staging
  assumes 3 doses take Attack 99 -> 66.
- The ToA supply brew (br_potion.rs2, `br_*dosepotionofsaradomin`) still drains Defence: open
  row in CONTENT_BUGS.md.

## Engine arithmetic for consumables

- `stat_boost`, `stat_drain`, `stat_sub` and `stat_heal` all step by
  `constant + BASE * percent / 100` (the integer divide is the wiki's "rounded down").
- `stat_boost` gives `max(min(cur + d, base + d), cur)` (no stacking); `stat_heal` gives
  `max(min(cur + d, base), cur)`; `stat_add` is the unclamped one.
- A wiki "X% of the current level" drain cannot be written with `stat_drain`'s percent. It
  needs `stat_drain(x, calc(stat(x) / 10 + 2), 0)` (`stat()` is the boosted level).

## A RED pack without touching the shared tree

- Build a symlink shadow of the content root: symlink every top-level entry, recreate the path
  down to the one changed file as real directories, and write the old file from
  `git show HEAD:./server/scripts/...`.
- Compile it with `src/build_opt/sscompile --src <shadow>/server/scripts --out
  <shadow>/server/scripts/build --content-root <shadow>`. The content root must be the shadow,
  or the lane.ini path match fails.
- Run with `TORIRSSERVER_SCRIPTS=<shadow>/server/scripts/build`. The server only warns on a
  stale pack. The script count is the same as the real pack's (42432).

## Maiden's Matomenos: meet each crab with the weapon in hand (seam18)

- Relay plan 18b: attack each crab by its own slot from the tick `t.npc.nearest` sees it, at
  any distance: `t.player.attack(sym, 2, 1, { slot = crab.slot, quick = true })`.
- The bow (MSB(i), rune arrows) let all 6 of 6 reach her (c = 6). The scythe intercept let 3
  of 6 reach her and halved her damage (261 -> 144) and the food (10 sharks -> 5).
- Count leaks from the tick log: a Matomenos (10820) `npc_death` on the same tick as an
  `npc_heal` on her slot. The heal is twice the crab's remaining hitpoints, so a wounded leak
  still counts in c.

## Xarpus phase 2: where the splash lands (seam18)

- Measured with trj/xspit.py: a spit hits the raider if and only if the raider's tile at
  land-1 is within 1 of the aim tile (w16bravo: distance 0 14 of 14, distance 1 32 of 32,
  distance 2 0 of 15). The aim is the raider's tile at S-1 (the T-1 rule).
- So the melee dance is OUT to a clean ring-2 tile (it takes the puddle), then TWO clean steps
  to a side tile two away from it. `step_tick` blocks one tick each (issued S-2, S-1, S;
  resolved S-1, S, S+1). Stepping back to the side tile next to the out tile is inside the
  3x3 every time.
- Phase 1 has no recipe lever on exhumed leaks: the click lands on the spawn tick and the run
  is 2 tiles a tick. An exhumed fires every tick from spawn+3 unless covered, stays open 11
  ticks, and the next comes 12 ticks later, so one more than about 5 tiles away leaks.

## Nylocas: a dealer -1 hit over 8 is a support collapse (seam18)

- A support collapse deals 37-43 in Entry and hits everyone in the room wherever they stand
  (tob_nylocas.rs2:1680). Seam16's "117 from detonations" was three collapses.
- Aggro explosions (max 8 in Entry) cost 0-9 a room over ten runs. Stepping away from them
  (relay plan 20) cost a support and 44 hitpoints: do not spend presses on them.

## Count the kit by doses, never by presses (seam18)

- `R.used.restore` counted every press, and the potion delay refuses most of them: seam16's
  "restores 13" was 6 doses.
- The relay's `R.kit(t)` / `R.kit_used(t, k0)` read shark, bandage, brew-dose and restore-dose
  counts and print the difference per room (`<room>.kit_used` rows).
- Entry Mode restores hitpoints and prayer after every boss (wiki Entry Mode :22). Food eaten to
  top up at the end of a fight is wasted, and a brew in the Maiden drains the scythe's Strength
  for the rest of the fight.

## The whole raid after the brew fix: still 0 of 5 (seam18)

- With the wiki's kit plus the Bloat chest's 10 bandages, the seam18 relay reaches Sotetseg
  with 0-2 restore doses, 3-14 brew doses and no food in every name. Nothing after the Nylocas
  recovers that: the Sotetseg chest's 10 bandages are all the second half gets.
- The Nylocas is the supply sink: 285-377 damage, 29-33 eats, 10-15 brew doses and 4-7
  restore doses per name. The relay fights it with single-target Fire Strike, where the wiki
  recommends Ancient Magicks (Entry :157, :166, rune pouch :92). That is the next lever.
- The brew fix alone (one run of seam16's plan-17 relay under w16alpha): Xarpus is killed
  instead of the phase 2 death (missed 23 -> 1, damage 383 -> 215) and the death moves to
  Verzik P2 at t2759. bravo and charlie died on the same tick as before.

## Three raiders in one run

Seam17 added this (three_clients_one_world, party_run_and_verbs). Normal and Hard need a
party ("For normal and hard mode, you will need 3 players", owner 2026-10-04). A test file
that declares `party = 3,`, or a run given `--party 3`, starts three client processes. All
three play in ONE world, ticking in lock step. The worked example is
`test/raids/_party_smoke.lua`. Its knobs and directories are in `test/raids/README.md`
("A party run").

- Run it: `python3 tools/raid_gate/run.py _party_smoke --no-build --no-publish`, then
  `python3 tools/raid_gate/gate.py _party_smoke`. Closer runs on the final tree went 99/99
  PASS. They took 9.6 s of the leader's wall clock for 165 world ticks: two raid entries,
  the second at Normal with the Maiden's fight started.
- Do the tick logs agree? Yes. There is one log, the world's, written in the leader's `p1/`
  and copied to `p2/`, `p3/` and the run directory. Each member also gets the server tick
  in every TICK frame. With `TORIRS_EMBED_PARTY_TRACE=1`, p2's and p3's client.log read
  `net: party: boundary 150 -> server tick 149`. That is the tick of the Normal Maiden's
  row, `2948 149 npc_spawn 1570 8360 105283740`. Two consecutive smoke runs gave
  byte-identical tick logs.

### Where the world lives: the leader's process (option A)

The world stays embedded in the LEADER's client. Members are clients 1..3 of that embed,
reached over a loopback "party link" (`src/torirsserver/torirs_server_embed.h`). The other
option, B, was the standalone torirsserver with `transport=tcp`. It was rejected for two
reasons. First, every server-side driver verb reads the world in its own process:
`t.cheat`, `t.ticklog`, `t.tick`, the server varps and `drive_world_ready`
(`src/plugin/torirs_plugin_drive.c`). Under B, all three clients would lose them, the
leader included. Second, B's 600 ms wall clock cannot follow content_test's virtual clock.
`TORIRSSERVER_EMBED_CLIENT_MAX` is 4 (leader + 3), which is enough for a party of three. A
five-raider party needs it raised (`TORIRSSERVER_PLAYER_MAX` is 8). The link exists only on
native POSIX hosts. A Windows or web build given a party knob aborts with a message.

### Lock step: READY, then TICK

The link is framed: a 1-byte type and a 4-byte big-endian length. 'D' frames carry the
game's bytes untouched. A member sends 'R' (READY) at each of its own 600 ms boundaries
and then blocks until the leader's 'T' (TICK, which carries `srv->tick`). The leader runs
a boundary only after every member has said READY. At that boundary it feeds each
member's input to the world in seat order, before the tick. So one world tick is one
boundary on every client, and every trace reads `boundary k -> tick k-1 digest d`
("Lockstep, pinned" below).
'S' (SEAT) is a member's first frame: seat n is client id n-1, and so login order and
pid. `TORIRS_EMBED_PARTY_SEAT` makes "who is pid 2" a fact of the command line, not a
race between processes.

The smoke's leader row `seam.three_clients_one_world` checks the whole run. From the
first tick with all three logged in, every world tick carries exactly three `player_tile`
rows. Measured: `ticks 4..166 (163 ticks): 3 player_tile rows on every tick` (seam17);
`ticks 4..191 (188 ticks)` since seam21.

### Lockstep, pinned: F frames per tick, READY carries the count, TICK carries the digest

Seam21 (party_lockstep_frames, party_determinism_gate). The owner, 2026-10-04: "I don't
want to introduce nondeterminism." A party run is now byte-identical run to run under the
same run name: the tick log, every raider's ledger and every boundary trace.

**The cause of the old one-tick shift.** It was `t.party.barrier`'s files, not the link.
Between two boundaries the three clients run their frames at the same time, in three
processes. So whether raider A's frame saw raider B's barrier file in that interval
depended on the wall clock. Measured 2026-10-04 on the unmodified binary, five smokes:
identical tick logs and member traces, but the leader's ledger came out three ways
(`party.barrier.normal_out ... p1 waited 1 tick(s)` in one run and `0` in the next;
`raid.enter.bloat_normal` 6 vs 7 ticks; SUMMARY 182 vs 183). A barrier passed one tick
later puts the next typed command one boundary later. Everything else on a member was
already a function of the frame sequence.

**The fix: a mark is stamped with the lockstep tick.** `api_drive.barrier_mark` writes
`lockstep=<t>` into the file, where t is `ToriRSServer_EmbedLockstepTick()`: on the
leader, the tick its last party boundary ran; on a member, the tick of the last TICK it
received. `barrier_present` counts a mark only when the reader's own lockstep tick is
later than the stamp. A mark written before boundary t+1 was written before its writer's
READY (or before the leader ran t+1), and every reader past t+1 got its TICK after that.
So every raider passes a barrier on the same tick, in every run. The cost: a barrier now
takes at least one boundary after the last raider's mark (the smoke went from 183 to 191
ticks, 5561 to 5617 tick-log lines). Outside a party there is no lockstep tick, and a
mark counts as soon as the file exists, as before.

**The link protocol, version 2.**
- A client frame pays k logic cycles (`TORIRS_LOGIC_CYCLES_PER_FRAME`, default 1) and moves
  the link's clock k x 20 ms. A 600 ms tick is therefore F = 30/k clock frames on the
  leader and on every member. A frame that the content-test clock holds (a screenshot in
  flight, async IO) moves neither clock and is not counted. That is still a function of
  the frame sequence.
- SEAT carries (protocol version, seat, k). A member whose version or k differs from the
  leader's aborts the run with a message naming both values. Build every client of a
  party from one tree; run.py already uses one binary.
- READY carries the member's clock frames since its last TICK. From a member's second
  READY on, the leader requires exactly F. The first READY follows the join, which ticks
  at once. The leader holds its own boundaries to the same F. A mismatch prints
  `torirsserver: party: seat n ran k frames, expected F` and aborts. A member that drifts
  is a loud failure, never a silent one-tick shift.
- TICK carries the tick and `ToriRSServer_EmbedWorldDigest`. The digest is FNV-1a over the
  tick, then pid, x, z, level and hitpoints of each active player in pid order, then the
  active npc count.
- With `TORIRS_EMBED_PARTY_TRACE=1` (run.py sets it on every party client) the leader and
  every member print one line per boundary in one format:
  `net: party: boundary k -> tick t digest d`. The old member line
  `boundary k -> server tick t (pending n bytes)` is gone.
- A frame audit aborts a party client when other than exactly one `App_RunOnce` runs
  between two transport polls, or when a frame pays other than k cycles. A party client
  needs a frame clock (`TORIRS_MAX_FRAMES` with the quest driver, or
  `TORIRS_EMBED_CLOCK_MS`); a party on the wall clock is refused.
- In a quest run the embed transport is clocked by content_test.c's virtual clock
  (`NetTransport_TestClock`), not by `TORIRS_EMBED_CLOCK_MS`. That knob matters only for
  headless runs outside the quest driver.

`make -C src test-embed-party-link` covers the payloads, F accepted and F-1 refused (at
k=1 and k=10), and the digest across two members. Three live mutants were built in a
throwaway worktree (a READY short by one frame, a 2-cycle frame, a doubled `App_RunOnce`).
Each one ended the party run with its message and signal 6.

**The driver half: every wait is counted in frames.** `api_drive.await` evaluates its level
once when it is armed and once per frame (`drive_pump_once`, called from on_frame_start).
Its deadline is the client world cycle (frames x k), never wall time. `t.party.barrier`
waits through `QD.party._await_counted` and reports what it counted:
`party.barrier applied: all 3 raiders, p1 waited 1050 frame(s) (35 tick(s))`. That number
is the same in every run. A party of one reads
`a party of one, p1 waited 0 frame(s) (0 tick(s))`. Audit, 2026-10-04: raid.lua and
core.lua contain no `os.*`, `io.*` or `math.random`. The heartbeat comes from the per-frame
pump every 25 client ticks, and run.py only watches its mtime. `t.party.see` is a level
await. Shots are requested by frame. Conformance row:
`seam.party_barrier_frame_counted` (a two-tick deadline times out after exactly 60 frames
at k=1).

**The checks.**
- Inside a run: `gate.party_lockstep` compares each member's boundary trace with the
  leader's, line for line. The union ledger gains a `party.lockstep` row. It is PASS when
  the traces are equal, and FAIL naming the first boundary and tick that differs (also
  when a member has fewer boundaries, or a trace is missing). run.py fails the run on a
  FAIL. Measured: `party.lockstep PASS p1 p2 p3: 193 boundaries each, 1 -> tick -1 ..
  193 -> tick 192, the same tick and digest on every raider at every boundary`.
- Across runs: `python3 tools/raid_gate/party_repeat.py <id> --runs 3 --load` runs the test
  three times under one run name and compares the tick log, every ledger and every trace
  byte for byte. Only a `run.unfinished` row's seconds are stripped. `--load` runs the
  last of the three with p2 at nice 19 and one busy loop per CPU. This is the gate a
  party room author runs before review (test/raids/README.md "Determinism").
- Measured by the seam21 closer: `_party_smoke --runs 3 --load` AGREE (tick log sha
  2054478152d0, 5617 lines, 193 boundaries, p1 SUMMARY 55 PASS 191); `--cycles default,1
  --runs 3 --load` AGREE (six runs, one sha); a long Normal Bloat scratch (all three
  attacking, eating and praying, 406 ticks in the fight) 3/3 AGREE under load (sha
  3a6ea401b7a8, 548 boundaries). The long scratch runs under `::god 1`; without it Normal
  Bloat's flies kill an unhidden raider in about 10 ticks.
- The run name seeds the run (its accounts). A renamed run (`run.py --name` with a party
  test id, `--no-publish` only) is a different run, and equally repeatable.

**The frames-per-tick knob: `TORIRS_LOGIC_CYCLES_PER_FRAME=k`.** The owner, 2026-10-04: "30
frames per tick seems like a lot of wasted compute per tick ... especially since you're
running headlessly". k must divide 30 (1, 2, 3, 5, 6, 10, 15 or 30) and needs
`TORIRS_MAX_FRAMES`; without it the client aborts. A headless frame then pays k logic
cycles in a row, and the transport clock and the frame's ms clock stretch by k. Every
clientscript clock, animation and timer still sees 50 Hz. A cycle the settle fence holds
back is carried to the next frame. At k>1 a shot is of the frame's last cycle, and
`TORIRS_SHOT_FRAME=N` is frame N, which is cycle N x k. k=1 is the default and changes
nothing: cooks_assistant, druid and the six Entry rooms are byte-identical.

**The party default stays k=1.** Each k is deterministic on its own, but k>1 is not the
same run as k=1. The driver's verbs are written in frames: a pointer step, a chat page and
a per-frame await each cost k cycles. So at k=10 the smoke takes 303 boundaries instead of
193, and at k=30 it takes 631, with different ledgers: at k=10 and k=30 p2/p3
`raidwide.chat.party_enter_line` FAIL, and at k=30 p1 `raid.left.bloat` too. k=2 keeps all three ledgers but moves three
`npc_free` rows at tick 112 by a tile; the cause was not found. Wall time on `_party_smoke`
(run.py total, closer's run): k=1 10.9 s, k=10 10.5 s, k=30 11.0 s. A tick costs about
3x less at k=30, but the frame-granular driver spends that many more ticks on the same
script. A room test keeps the default. Making k irrelevant means stepping the driver per
cycle or rewriting those verbs in ticks; that is a later driver seam.

### Stalls and exits

The slowest client sets the pace. If a member sends no READY within
`TORIRS_EMBED_PARTY_WAIT_S` (default 60), the leader logs it out by name and keeps going:
`client 2 <name> sent no READY within TORIRS_EMBED_PARTY_WAIT_S -- logged out`. A member
that exits is logged out the same way (`closed its link`), so the leader's rows show the
departure. If the leader stalls, every member blocks and the leader's heartbeat ends the
run. When the leader exits, run.py gives each member 20 s to finish its own script and
then kills it. An unfinished member ledger gets its SUMMARY like any unfinished run.
Closed (seam22; it was seam21's open item): a member stops when its leader's link is
gone. In the long Bloat scratch without `::god` the leader died and the members ran about
1000 more client ticks on their own clocks, then timed out their `done` barrier. Now
`party_member_lost` (net_transport_embed.c) prints
`net: party: abort: this member's world is gone (the leader's link closed after boundary k,
tick t) -- a member never runs past its leader` and exits 1 at the member's next boundary,
whatever closed the link (the leader's exit or death, a seat the leader dropped, a leader
still past `TORIRS_EMBED_PARTY_WAIT_S`, a protocol break). run.py writes the member's
`run.unfinished` quoting the line. Scratch `s22_leader_gone` (the leader finishes after one
barrier, the members wait on one it never marks): both members exit 1 after boundary 4, tick
3, and `party.lockstep` PASS 4 boundaries each. A normal end never reaches it: every raider
passes the last barrier on the same lockstep tick and finishes in that frame.

### Seats, accounts and directories

Accounts are `<base>_p1` .. `<base>_p3`, where base is the run name sanitised and cut to
9 characters, so no account is longer than 12 (only 12 characters seed a run). The
password is `test`. Raider n's session is `build/quest_gate/<run>/p<n>/`: ledger.tsv,
shots/, heartbeat, client.log and script/. The world's saves are `<run>/saves/`. Every
raider's fixture is written there before any client starts. `<run>/party.tsv` names the
seats.

### Grading: the union ledger

`gate.py` grades the union at `<run>/ledger.tsv`. The leader's rows keep their names, so
its `spec.*` rows are the ones `raid_coverage.py` grades, unchanged. A member's rows are
`p<n>:<step>` and its shots `p<n>-<shot>`. A raider with no ledger becomes a FAIL row,
`p<n>:run.no_ledger`. Duplicate-MD5 shots are judged per raider, because two raiders side
by side can photograph the same dialogue page. A test id starting with `_` skips
`raid_coverage` (raid_gate/gate.py): it has no encounter table.

### What a member's script looks like

It is the SAME file. `local role = t.party.role()`, then branch with if/else:

```lua
if role == 1 then t.exec("party.form", t.party.form, "normal") end
t.expect("party.barrier.formed", t.party.barrier("formed", 300))
if role ~= 1 then t.exec("party.apply", t.party.apply, t.party.name(1)) end
-- leader: t.party.accept(t.party.name(n)) for each n, then t.party.ready()
-- member: t.msg.await("has entered the Theatre of Blood", 20); t.party.follow_in()
```

A member reads and clicks everything a client can: ui, chat, msg, inv, npcs, locs,
`t.party.players`, its own tile and stats. Its `t.cheat` goes out as the client's typed
`::command` packet (App_SendCommand) and is handled for that member at the world's next
boundary. The verdict is "sent", not "ran", so read the effect back. In the smoke,
`party.setup_landed` reads hitpoints 99 and defence 99 on all three raiders. A member
loads the content symbol tables itself (ToriRSServer_BootLoad, no world). Without them,
every component, loc and varbit name it spelled answered `no_row`.

What a member cannot do: the SERVER readers. `t.ticklog`, `t.var.server`,
`t.raid.state`, `t.raid.enter` and `t.raid.leave` all answer `unsupported`. `t.tick` answers
since seam22: the tick of the member's last TICK frame ("Seam pass 22" below). Spec rows and
tick-ledger rows are the leader's to write. A member leaves the raid with
`t.cheat("::tobout")` and a tile read. `t.session.relog` on a member would type `p<n>` as
its user, because session.lua derives the user from the session directory. For the same
reason, a solo `t.party.names()` answers the session directory's name; under run.py that
is the account, except in the conformance harness (`attempt-01`).

On the leader, client 0's player is made the world's active player before every server
read and cheat (`drive_embed_world_as_leader`). With three raiders, the active player
between ticks is whoever the world last acted for. Solo, it is already client 0's and
nothing changes: cooks_assistant and druid ledgers are byte-identical to the branch base.

### What a Normal room test author writes

Start with `party = 3,` and the lobby sequence. The leader forms the party with
`t.party.form("normal")`, accepts both members, then calls `t.party.ready()`, which reads
`Is your party ready? Members: 3. Mode: Normal.`. The members call `t.party.follow_in()`
after the call-in line. The leader crosses the barrier ("Yes, begin the fight."). The
scale is the party that walked in: `~tob_start_room` sets `^tob_var_scale` to
`~tob_party_size`. Measured in the smoke:

- Normal Maiden at 2625 = 3500 x 750 / 1000 (`spec.maiden.hp_3`).
- `spec.raidwide.scale.party_3_or_fewer` at 750. Source: "Players in groups of three will
  find that the bosses have 75% of their original hitpoints."
  (wiki_Update_Theatre_of_Blood_Changes_Deadman_Summer_Finals.wikitext:28).
- HUD varbit 6448 at 1000 (`spec.raidwide.hud.boss_hp_full`).

A member's part in a fight is its own clicks plus barrier sync with the leader.

### t.party.barrier: sync without a cheat

`t.party.barrier(name, ticks)` writes `<run dir>/barrier.<name>.p<n>` (api_drive.barrier_mark)
and waits until all N marks count. These files are driver state; the world never sees
them. Since seam21 a mark is stamped with the lockstep tick and counts from the next
boundary on, and the wait is counted in frames: the detail reads
`p1 waited 1050 frame(s) (35 tick(s))`, the same in every run ("Lockstep, pinned").
A barrier typically waits 0-35 ticks. A party of one answers ok at once.
`QD.await` predicates cannot yield, so a predicate cannot click. A loop that presses
Refresh while it waits has to be written out.

### The lobby verbs: form, apply, accept, ready, follow_in

Each verb is a real click sequence on `tob_partylist`, `tob_partydetails` and the door,
read back on the next tick:

- `form(mode)`: the notice board (a first reading's three pages are answered with the
  mode's experience line), then Make party, then Mode if needed. Read back: `Mode: X` and
  the `Party of` title.
- `apply(leader)`: the party list row whose sub 3 is the leader's name, then View party,
  then Apply. Read back: the action button offers Withdraw.
- `accept(name)`: on the leader's open panel. Accept is applicant k's sub `20k`. A
  just-pressed row has no ops for 80 client cycles (`torirs_tob_party_ack.cs2`), so accept
  re-reads the index and presses again, up to 3 times. Read back: a member row.
- `ready()`: the door, the death warning, then the ready check. The detail carries
  `Is your party ready? Members: N. Mode: X.` verbatim. Then "Yes, let's go!" and the
  entry line.
- `follow_in()`: a member's door click after the leader. A member who is too early reads
  "Your party leader has not entered the Theatre yet" and gets `refused`.

The panel's first push draws only its last row, so every read presses Refresh first.
After the board's first reading, its last mesbox page ("When you form a raiding party
...") can stay on screen under the party panel. It is cosmetic: no press failed because
of it in any run.

### t.party.players / t.party.see: who this client can see

`api_drive.players()` lists every player in THIS client's entity pool: name, world tile,
level, pid and `me`. `t.party.players(radius)` returns the OTHER players within radius.
`t.party.see(names, radius, ticks)` waits until every named raider is among them. The
names can be account or display spellings; the default is every other raider. The solo
pool fills about 2 ticks after a script starts, so settle before the first read. In the
smoke, every raider reads the other two in the lobby and in the Maiden's room:
`_party_sm_p2 at 3663,3216,0 sees _party_sm_p3 pid 3 at 3664,3216,0; _party_sm_p1 pid 1 at
3662,3216,0`.

### HUD orbs in a party (content finding)

Inside the raid, a member's orbs for raiders who arrived after it stay 0 until its own
`~tob_hud_orbs` runs again. The fight watchdog that refreshes them is queued only on the
raider who crossed the barrier (tob_raid.rs2:1572-1573, `queue(tob_room_watchdog, 0, 0)`;
tob_party.rs2:697). Measured with the fight running: the leader reads
`p0=27 p1=27 p2=27`, and p2 reads `p0=27 p1=27 p2=0`. Grade the leader's three orbs and
each raider's own orb. The finding is in CONTENT_BUGS.md (seam17).

**Fixed in seam19.** Every raider's column is now current: whoever runs `~tob_hud_orbs` (an
arrival, the starter's per-tick watchdog) refreshes the whole party. The seam19 smoke reads
`p0=27 p1=27 p2=27` on all three clients at Bloat's entrance and with the fight running.
See "A party room test" below.

## A party room test (seam19)

Seam19 added this (tob_party_room_bring_along, tob_party_room_test_shape). A Normal or Hard
room test is a party of three at the room's entrance. The worked example is
`test/raids/_party_smoke.lua` phase C: Normal Bloat, `spec.bloat.hp_3` 1500, `scale=3`, and
the three orbs at 27 on every raider.

- **The id and the file.** The id is `tob_<room>_<mode>` (`tob_maiden_normal`,
  `tob_maiden_hard`). `raid_coverage.py` grades it against `<room>.tsv`. `run.py` publishes
  it to `selftest/minigames/tob/<room>_<mode>/play/` (the id is split at its first `_`),
  with the members' `p<n>-` shots. The file declares `party = 3,`. A Hard test also declares
  `fixture = "tob_normal_done.ini"`: that is `fresh_lumbridge.ini` plus
  `varp6826_tob_completions = 1`, because the door refuses Hard to a raider with no
  completion. `::tobmode` and `::tobjoinroom` do not check it.
- **Every raider calls `t.raid.enter("tob", "<room>", {mode = "<mode>"})`**, with the same
  arguments and the same number of times. The barrier names count the calls, so a test that
  enters on one raider only desyncs them (they time out after 300 ticks).
  - The leader lands with `::tobmode`, as solo. It then passes the barriers
    `raid_enter_<k>` and `raid_joined_<k>` and re-reads `::tobstate`. ok ends
    `party 3 of 3 in the instance (tobstate party=3 scale=1)`; refused if the instance's
    party is short.
  - A member waits for `raid_enter_<k>`, types `::tobjoinroom <mode>`, and reads back the
    room line and its tile leaving within 5 ticks (measured 1-2), then its tile equal to the
    leader's. ok reads `in tob bloat (normal) at 6452,159,0; boss present (...); p2 joined by
    ::tobjoinroom 1 (landed 2 tick(s) after it was typed) on the leader <name>'s tile ...`.
  - Measured on all six rooms at Normal and on the Maiden at Hard: all three raiders on one
    tile at the entrance.
- **`::tobjoinroom [0|1|2]`** (tob.rs2) is a member's half of `::tobmode`. No argument means
  the raid's own mode. It runs the door's join (`~tob_join_raid`: the handle, the chest
  flags, the arrival on the leader's tile, the room card that mounts the HUD) and
  `~tob_party_join`, so the seats fill in the order the members type it. Its refusals, each
  answered `refused` with the line:
  - "You're already inside the Theatre." (leave first with `t.cheat("::tobout")`)
  - "Your party leader has not entered the Theatre yet."
  - "That party is running a different mode."
  - "That party is already fighting. Wait for the room to finish."
  - "That party is full."
  The older `::tobjoin` (tob_selftest.rs2) is the C selftest's: Normal only, no refusals of
  its own. Never use it in a test.
- **`::tobstate` ends ` party=N scale=K`**: the instance's party count and the scale
  register. Scale reads 1 (the build's seed) until the barrier is crossed, then the party
  that walked in. Normal and Hard boss hitpoints are the same at scale 1, 2 and 3 (the
  3-or-fewer pool), so a party test proves its scale from this field, not from hitpoints.
- **The leader/member split.** The leader writes `spec.scope` (`mode=<mode> party=3`), every
  `spec.*` row and every tick-ledger row. On a member, `t.raid.state`, `t.raid.leave` and
  `t.raid.start_tile` answer `unsupported` ("p2 is a party member and holds no world"), as
  do `t.ticklog` and `t.var.server` (`t.tick` answers on a member since seam22). The one tick log carries every raider's tiles
  and hits; a member's are told apart by pid: in the tick log seat n is pid n-1 (the leader is
pid 0) and a projectile aimed at a player carries target -(pid+1) (corrected in seam22; this
line used to say seat n is pid n). A member's part is its own
  clicks, prayers, steps and eats, plus a `t.party.barrier` at each phase the leader also
  passes (name each barrier once per run).
- **The LEADER crosses the barrier.** Since seam22 every raider of the party who is in the
  raid is judged by the room's per-tick rules, not only the crosser (tob_raid.rs2
  `~tob_arm_party_watchdogs`; "Every raider is judged by the room's per-tick rules" in the
  seam22 section below): a member standing in Maiden's blood takes it, and a member on
  Sotetseg's arena grid is ragged off the path and spawns the tornado on the fourth row.
  Grade a member's per-tick room damage from the leader's tick log (`hit_player pid n-1`).
- **Roles come from the sources, never invention.** Use the trio transcripts under
  `docs/minigames/theater_of_blood/sources/transcripts/` (`grep -l -i trio`:
  yt_4i4lv-srJkw.md, yt_6soXuRA77JU.md, yt__QXdNAZh7Yo.md, yt_D1b4eWwnOHU.md) and the Blert
  files `sources/blert_guides/tob_nylocas_trio_content.mdx`, `tob_bloat_humid_content.mdx`
  and `sources/blert_nylo_pillar_assignment.json`. Cite the line beside each role's branch.
- **Grading a party scope.** `raid_coverage.py` keeps a sidecar `party` row only in the run's
  mode. It drops a party/normal/hard row whose id names another party size (`_5`, `_4`,
  `_5p`, `hard_5`). `_3` counts at 3, and at 2 when the quantity says "or fewer". In-scope
  counts on the current tables: Normal/3 maiden 70, bloat 57, nylocas 85, sotetseg 89,
  xarpus 63, verzik 170; Hard/3 74, 54, 80, 89, 66, 158. `--mode` and `--party` override the
  scope row each on its own. A `tob_<room>_<mode>` id whose scope row names another mode is a
  finding.
- **Party orbs.** Varbits 6442..6446 are current on every raider's client, so a member can
  grade `raidwide.hud.orb_*` rows on its own client. With the fight running, a member's `p0`
  tracks the leader's hits (measured p0=10 on all three).
- **The orbs wait for an open chat page.** The arrival's orb write is a normal queue
  (`[queue,tob_room_settle]`, tob_raid.rs2), so it does not run while a modal is open. The
  seam19 closer measured partyslot 0 / p0 0 for six ticks at an entrance behind a nightshade
  "player" page, and 1 / 27 once it was closed. Close any chat page before `t.raid.enter`
  when a row reads the orbs.
- **A member's entrance shot is dark.** It is taken while the room's title card is still
  fading (dark red with the card): the settle does not wait for the card. This is cosmetic.
- **Time and determinism.** A three-client run costs about 35 ms of the leader's wall clock
  per world tick at the default k=1. The smoke runs 191 ticks in about 11 s of run.py with
  `--no-build`. A full Normal room of ~700 ticks should take well under a minute; set
  `max_frames` from a measured run. Since seam21 a party run is byte-identical run to run
  under the same run name: tick log, every ledger, every boundary trace. The old one-tick
  shift (one run in three, from tick 114-116) was `t.party.barrier`'s cross-process file
  race, now pinned ("Lockstep, pinned" under "Three raiders in one run"). Compare runs
  with `cmp` or `tools/raid_gate/party_repeat.py`; no tolerance.

## A burst's freeze is in the tick log: one npc_spotanim per slot it hit

There is no `npc_frozen` kind, but an Ice Burst writes `npc_spotanim` `ice_burst_impact` (367 in
this cache, height 124; `magic_combat_spells.dbrow` `spotanim_target,ice_burst_impact,124`) on
every nylocas the 3x3 hit, wrong colour included; a splash is spotanim 85 instead. In the launch-14
tob_nylocas log every one of the 33 chewer bite gaps that was not 3 ticks (17-19, the freeze) held
a 367 row on that slot, and no 3-tick gap did. Key frozen gaps on that row by slot rather than
guessing the burst's neighbours from `t.npc.tiles` at cast time (what tob_nylocas does now; it
gives the same answer here, but a nylocas that walked into or out of the 3x3 in the press's tick
would be misread).

## "Unprayed" is the prayer you read, not the first switch you made (Verzik P3)

A protection prayer turned on in an earlier phase stays lit through the phase change: the
launch-14 tob_verzik raider entered P3 at tick 415 with Protect from Magic still on from P2, and
`t.prayer.read()` reported `protectfrommagic` true at every sample from 426 to 576. The run's
spec.verzik.p3_auto_miss_entry counted the autos launched before its own first P3
`t.prayer.set` (tick 585) as unprayed, so five magic autos (projectile 1594) that landed under
Protect from Magic (0, 6, 10, 0, 0, all within the prayed max of 10) were counted with the seven
ranged ones (1593: 10, 0, 9, 15, 10, 0, 0). The truly unprayed sample was 7, not 12.

- Decide "unprayed" per hit: read `t.prayer.read()` on the landing tick (Verzik P3's autos
  are judged at impact, Entry Mode :243) and keep the hit only when the prayer for ITS style
  (1593 ranged -> `protectfrommissiles`, 1594 magic -> `protectfrommagic`) was off.
- Or turn every protection prayer off at the phase change and say so in the row.
- `raw` on hit_player already includes the prayer's cut, so a raw 0 under the matching prayer
  is not a miss you can count; and a technique row that proves "prayed at landing" with four
  raw 0 hits cannot tell the prayer from a miss when the unprayed miss rate is about 3 in 7.
  Show a prayed sample whose largest hit is under the unprayed ones', or a halved nonzero hit.

## The tick log has no prayer kind: prove "prayer off" by a hit over the prayed ceiling

There is no prayer row in ticklog.tsv (kinds are player_tile, hit_player, projectile, npc_anim
and the rest; no prayer or varbit kind), so a row that claims an attack was unprayed has only
two witnesses: a `t.prayer.read()` readout in a drive row, and the damage. The readout can lie
by construction: tob_verzik's P3 loop drops every lit protection prayer each tick until its first
switch and then writes `pset3[pn] = false` into the table it just read, and drive.p3stateN prints
that table, so a `t.prayer.set(pn, false)` that failed would still print `pm false pg false`.

- Write the readout from a fresh `t.prayer.read()` after the set, never from the table you edited.
- The damage is the witness a sampler trusts: Verzik P3 halves a prayed auto at the landing
  (tob_verzik.rs2 `[queue,tob_verzik_p3_auto_land]`, `divide($hit, 2)`), so an Entry auto over 10
  of the matching style proves that protection was off. The launch-14c tob_verzik sample had magic
  13 and 20 and ranged 15 inside its unprayed window; name such hits in the row's detail.

# Seam pass 20: when a protection prayer is read

The owner's default (2026-10-04): an npc attack reads the target's protection prayer on its
animation tick, when the projectile is sent. A landing read is the exception and needs a
pinned source naming the npc. Seam20 audited every read in minigame_tob and changed no
behaviour: the three landing reads are each sourced or ruled.

## Sotetseg's ball: switch so the prayer is up when the ball LANDS (seam20)

- He reads Protect from Magic on throw + end_cycle // 30 (7 ticks from the barrier,
  end_cycle 232), not on the throw. A prayer raised after the throw counts; one dropped
  after the throw does not.
- A prayed ball is a block splat ON the landing tick: `hit_player` damage 0, hitsplat 26,
  npc_slot -1. An unprayed one is his own hit (npc_slot = his slot) one tick later, and
  protection presses are refused for 5 ticks after it (`t.prayer.set` answers refused).
- Match a splat to its ball by the landing tick, never "the next splat": he throws every 5
  ticks and the ball flies 7, so two are in the air at once.
- The ricochets (a party of two or more) read theirs at their own landing, by the wiki's
  "similar projectiles"; nothing has measured them apart.
- Source: blert, 21 Normal and Hard rooms (sources/blert_api/spec_pass_sotetseg/
  seam20_prayer_read.txt): of 30 balls thrown with Protect from Magic off and switched on
  within four ticks, 23 cost nothing. Spec row `sotetseg.ball_prayer_read_tick` (B). Proved
  by `seam.tob_sotetseg_ball_prayer_read_at_landing` (throw 2345, landing 2352 blocked;
  throw 2355, landing 2362 unprayed 10 at 2363; the next press refused).
- tob_sotetseg measures this row since launch 15 (1c86bf153, FULL 84/84): one ball thrown
  with the prayer off and raised after the projectile row was seen (throw 33, landing 40,
  block splat), and the tenth ball of phase 2 thrown prayed and dropped after its row (throw
  279, landing 282, unprayed 22 at 283, presses refused to 287).

## Verzik P2's urnbomb: Protect from Missiles counts if it is up when the bomb LANDS (seam20)

- Read on throw + end_cycle // 30 (about 4 ticks), and only if you are still on the tile.
  The blood spell is the opposite: read on her cast.
- Under `::god`, read `hit_player.raw`; the prayer's halving is already inside it (Entry: at
  most 8 prayed, 16 unprayed).
- Source: wiki_Verzik_Vitur.wikitext:394 "when the urnbombs land", :397 the blood spell "is
  calculated during her attack animation unlike the urnbombs", Strategies :907. Spec row
  `verzik.p2_bomb_prayer_read_tick` (D). Proved by
  `seam.verzik_p2_urnbomb_prayer_read_at_landing` (prayed only in flight: raw 3,0,2,3,5,1;
  prayed only at the throw: 14,10,13,1,11,3).
- tob_verzik does not measure this row yet: the coverage gate reads 145 of 146 until the
  room is re-authored with a `spec.verzik.p2_bomb_prayer_read_tick` row.

## Which ToB attacks read the prayer on the send tick, and which at the landing (seam20)

- Send tick (switch before the npc animates): Maiden's auto, Bloat's flies, the Nylocas
  waves, Vasilias, Sotetseg's melee (read on the swing tick; the splat is a tick later),
  Verzik P1 and the P2 blood spell.
- Landing (switch before it lands): Sotetseg's ball and ricochets, the P2 urnbomb, the P3
  ranged and magic autos (ruled).
- No prayer read at all: everything else, including Sotetseg's death ball, Xarpus, the P3
  green ball, webs and melee. The full table with file:line is in CONTENT_BUGS.md, "From
  seam20".

## Sotetseg's ball trial: "dropped at the throw tick" is still after the throw, the flight is range-dependent, and an unprayed ball can lock you out for good (launch 15)

- A press made after `t.ticklog.rows({ kind = "projectile" })` returned the ball is causally after
  the throw even when both carry the same tick (tob_sotetseg: projectile serial 1401 at 279, the
  drop pressed after it, `varb4116_prayer_protectfrommagic 1 -> 0 (server) read on drive tick 279`).
  Prove the order by the row you waited for, not by the tick numbers.
- The flight is not always 7: compute each ball's landing as `tick + end_cycle // 30` from its own
  row. From the barrier it is end_cycle 232 (7 ticks); three tiles out in phase 2 it is 96 (3).
- Dropping Protect from Magic while he keeps throwing every 5 ticks is a trap: each unprayed
  ball refuses protection presses for 5 ticks, and the next ball lands inside that window, so
  the lock renews itself until a death ball's gap lets a press through (the launch-15 author lost
  runs 1-3 to this; its kept sample is the ball at 279, whose successor at 284 is a death
  ball). Take the "dropped in flight" sample on a ball whose successor is a death ball, or
  right before a maze.

## Verzik's urnbomb read: one bomb under 8 does not tell the landing from the throw (launch 15b)

- An Entry urnbomb is a successful hit of 0..16, halved to 0..8 by Protect from Missiles. One
  forward trial (off at the throw, on before the landing) that takes 1..8 is what a THROW read
  would also give about 8 times in 17 (seam20's "prayed only at the throw" sample has 2, 1, 3
  among 14, 10, 13, 11). The sampled launch-15 room passed `spec.verzik.p2_bomb_prayer_read_tick`
  on one such hit (throw 277 at 6427,91, landing 280, 4 at 281) and was reverted.
- Grade on a sample the other rule cannot produce. The strong one is the reverse trial: a bomb
  thrown at your tile while Protect from Missiles is up, the prayer dropped before it lands, and
  a hit over 8 (unprayed at the landing, impossible under a throw read). The same run had one by
  accident and the row ignored it: thrown 257 under the prayer, drop pressed 259, landing 260,
  9 at 261. Otherwise repeat the forward trial; five hits all at most 8 is (9/17)^5, about 4%,
  under a throw read.
- Prove each set, do not narrate it: keep the detail `t.prayer.set` returns (the
  `varb..._prayer_protectfrommissiles 1 -> 0 (server) read on drive tick N` line), or print a
  fresh `t.prayer.read()` after the press. A drive line saying "dropped" is not a witness.
- `hit_player.raw` equals the damage for a bomb (4 = 4, 9 = 9): the halving is already inside
  it, so the raw column cannot show the prayer either. Only the size can.
- "No bomb in the air" is not `new_atk == nil`: that is only "no new throw this tick". A bomb
  thrown a tick or two earlier is still in flight; label every bomb hit by the prayer at its
  own landing (`spec.verzik.entry_p2_bomb_max` called the 9 above "Protect from Missiles on").

## Verzik's urnbomb reverse trial: one hit over 8 settles the read, if the prayed bombs show the halving (launch 15c)

- The kept room (d3ff92400) arms the trial on the locked tile 6427,91 with Protect from Missiles
  up and read back (`t.prayer.read()` true on tick 259), waits for the next `projectile` 1583 row
  at its own tile (thrown 261), drops the prayer on that same drive tick and reads it back false,
  then holds the tile until land+2. The `map_spotanim` 1584 lands on 264 at 105300059
  (6427 << 14 | 91 = the raider's tile) and `hit_player` 12 on 265: over the prayed 8, so not a
  throw read. A press made on the drive tick that FIRST shows the projectile row is after the
  throw (the row is the server's finished tick), as for Sotetseg's ball.
- One reverse hit over 8 alone would also pass if the prayer never touched the bomb at all. The
  witness for "the prayer halves it" is the rest of the log: the room keeps Protect from Missiles
  up through P2, and its 8 other clean bomb hits (hit_player one tick after a 1584 on the
  raider's tile, no zap that tick) were 7, 4, 2, 6, 6, 0, 4, 5: all within the halved 8. A grader that wants both halves in its own row counts those.
- A bomb hit is the `hit_player` on the tick after a 1584 landing on your packed tile; the
  lightning ball's hits (projectile 1585 plus `player_spotanim` 560 on the same tick) are the
  other P2 hits over 8 (10, 18, 14, 9 in this run) and are not bombs.

# Seam pass 22: a dead raider stays in lock step, and the member readers

Seam22 (party_death_and_member_readers; SEAM_TRIAGE_2026-10-05a.md). The first Normal
trio pass lost Maiden and Sotetseg to a harness rule, not to the fight: a member who died
ended its script, its boundary trace stopped, and `party.lockstep` failed the run
(`first difference at boundary 653 (tick 652): p2 stops after 652 boundaries, p1 ran 875`,
tob_maiden_normal; `p2 stops after 213, p1 ran 302`, tob_bloat_normal).

## A member's death is a row, not the end of its run

- On a MEMBER (`t.party.role() > 1`) raid.lua wraps combat.lua's `QD.player._death_fence`:
  the first fenced verb that reads the death (a click settle, `t.player.attack`/`cast`,
  `t.npc.await_dead*`, a quick held press) writes `player.died` with its kept shot and
  returns true, so that verb answers `refused` with the death text, and every fenced verb
  after it does too. It does NOT call `t.finish`: the script runs on, caged, sending READY
  every F frames like any member. `t.party.barrier` runs the fence first on a member, so
  the row is written at the latest at the next barrier, on the same frame in every run.
- The row is FAIL unless the member called `t.party.allow_death(reason)` beforehand; then
  PASS, quoting the reason. The leader's `allow_death` answers `refused` ("the leader, whose
  death ends the world"). The leader's death is unchanged: row FAIL, `t.finish(0)`, and its
  members then stop at their next boundary (net_transport_embed.c, "Stalls and exits").
- The Theatre prints "You have died. Death count: N." IN PLACE OF "Oh dear, you are dead!"
  (tob_spectate.rs2 `~tob_death_message`, :223-232), so state.lua's ring reading never saw a
  Theatre death; only the few ticks its hitpoints read 0 did. raid.lua now latches that line
  too, on every raider of a PARTY (`QD.raid.TOB_DEATH_PREFIX`; `t.party.size() > 1`):
  `t.player.alive()` stays `refused` after the restore refills the hitpoints, and a party
  leader's Theatre death ends its run reliably (tob_bloat_normal's copy: the leader's
  `player.died` quotes the line). NOT in a solo run (seam22 closer): the conformance harness
  dies in a solo Entry room on purpose (`seam.tob_death_cage_then_entry_restart`, `::die`, the
  cage, the wipe's restart) and drives on; latched there, it ended at `player.died` on the very
  next click settle, eleven attempts in a row. A solo room test's death is read only through
  its hitpoints-0 ticks, as before seam22.
- After a death, carry the member through the remaining barriers to the common end and
  branch its fight on `t.player.alive()`. The member's own clicks are meaningless while
  caged; reads, `t.cheat` (`::tobout` leaves the cage) and barriers work.
- `gate.party_lockstep` compares every member's trace with the leader's up to the
  leader's last boundary; a member must reach it (fewer boundaries is FAIL), nothing after it
  is compared, and the PASS detail names the members whose ledger has a `player.died` row.
- Proof: `_party_smoke` phase D. The leader steps back out through Bloat's barrier (a
  started fight's barrier is a gate) and walks to the entrance; p3 calls `allow_death`,
  crosses alone and stands unhidden. The leader's tick-log reading: `hit_player pid 2 since
  tick 199: 8 hit(s), ticks 212..220, 99 damage; p3's 99 hitpoints ran out on tick 220`; p3's
  `party.death.p3_caged` reads `You have died. Death count: 1.` at 6431,171 (the cage) on tick
  226; `p3:player.died PASS ... allowed`; then `party.lockstep PASS p1 p2 p3: 230 boundaries
  each ... p3 died and stayed in step to the leader's end`. `party_repeat.py _party_smoke
  --runs 3 --load`: AGREE, tick log sha 6cf6d25dd6a6, 5817 lines, 230 boundaries, p1 SUMMARY
  64 PASS: the death lands on tick 220 in every run.
- The four Normal attempts, copied and driven on this tree: tob_maiden_normal lockstep PASS
  559 boundaries (`p2 died and stayed in step`), tob_sotetseg_normal PASS 643 (`p2, p3 died
  and stayed in step`), tob_bloat_normal PASS 125, tob_verzik_normal PASS 117. Each now stops
  at the LEADER's death (maiden tick 557, sotetseg 642, bloat 124, verzik 115): strategy and
  content, not the harness.

## Member readers

- `t.tick()` on a member answers the tick of its last TICK frame
  (`api_drive.session().lockstep_tick`, ToriRSServer_EmbedLockstepTick; nil outside a party).
  The leader stamps the TICK from srv->tick right after the boundary's world tick, so between
  two boundaries a member reads what the leader's `t.tick()` reads: the smoke's
  `party.tick.bloat_read/death_clear/death_done` rows read 190, 199, 227 on all three raiders.
  The leader and a solo run still read srv->tick (`seam.party_member_readers_solo`). The quick
  presses' "read on tick N (pressed on N-1)" is on this clock on a member now.
- `t.prayer.points()` answers `("ok", reading, detail)`, the shape of
  `t.skill.read("prayer")`, with `reading.points` (= level, points left) and `reading.text`.
  It used to answer `("ok", detail, reading)`, and every author who wrote it like
  `local _, pp = t.skill.read("prayer")` got the string (the Normal Sotetseg review).
- Pids. The tick log's `pid` is 0-based by seat: the leader p1 is pid 0, seat n is pid n-1,
  and a projectile aimed at a player has `target = -(pid+1)` (phase D: Bloat's flies at p3 are
  `projectile ... target -3 spotanim 1568`, the hits `hit_player pid 2`). The client's
  `t.party.players` rows carry the client's player index, which IS the seat (`_party_sm_p3 pid
  3`). README.md used to say seat n is pid n for the tick log; corrected.

## The budget, and `t.ticklog.rows` by area

- The driver's Lua runs under `PLUGIN_LUA_STEP_BUDGET` = 400000 VM instructions
  (src/plugin/torirs_plugin_lua.c:38; not in torirs_plugin_drive.c), re-armed on the test's
  coroutine at every resume (`PluginLua_ThreadResume`), so it bounds the code between two
  yields; an await predicate runs in the frame callback under its own. Past it: `instruction
  budget exhausted (400000)` and a `script-error` row. The Normal Maiden author hit it twice in
  a post-fight analysis (runs 10-12): whole-log `npc_spawn` rows (506, every region npc) and
  `npc_tile` scanned per projectile. Filter in C (`kind`, `slot`), add `area` or `since`,
  index by tick once, and `t.ticks(1)` between big passes.
- `t.ticklog.rows({kind = ..., area = {x0, z0, x1, z1[, level]}})`: world tiles, inclusive,
  either corner first. Keeps a row whose `x, z` (any unpacked `coord`, npc_tile's tile), else
  `dst_x, dst_z`, is inside; drops a row with no tile; refuses a malformed box. raid.lua wraps
  ticklog.lua's `rows` (DRIVE_SCRIPT_PARTS now loads ticklog.lua before raid.lua, and raid.lua
  asserts it). Smoke: `npc_spawn rows: 1519 in the whole log, 3 with area
  {6400,128,6463,191} (Bloat's map square)`; conformance `seam.ticklog_rows_area`.
- `t.world.spotanims(radius)` / `t.world.projectiles(radius)`: the radius is in TILES, a
  square (|dx| and |dz| both within it; torirs_plugin_drive_ui.c drive_ui_within_radius), 0 =
  all. Radius 1 drops a shadow two tiles off (the Normal Bloat review).
- In a fight press fast: `t.player.eat`, `t.player.drink`, `t.player.inv_op(item, op,
  {quick = true})`, `t.player.equip(item, {quick = true})` ("The fast press" above). The
  plain `inv_op` costs 3 ticks a press (the Normal Maiden author found it in run 11 and
  switched five eat sites to `{quick = true}`).
- `t.npc.state_text(row)` ends `(now tick N)`, and N is `api_drive.tick()`, the client's api
  clock that the row's `seq_tick` and `spotanim_tick` use (ui.lua `QD.npc.state_text`), not
  `t.tick()`. Compare a seq tick with that N; compare a tick-log row with `t.tick()` (the
  Normal Bloat review read the two as one clock).
- `t.player.step_tick` on a MEMBER answers `ticklog_start answered unsupported`: it needs the
  tick log, which only the leader holds. A member walks with `t.player.walk_to` and times its
  step on `t.tick()` (the lockstep tick above).

# Seam pass 22: what the first Normal trio pass found in the rooms

Seam22's content half (tob_normal_trio_findings; SEAM_TRIAGE_2026-10-05a.md). The
analyses, the Blert harvest scripts, the scratch scripts and the before/after pack script are
pinned under `docs/minigames/theater_of_blood/sources/blert_api/spec_pass_seam22/` (README.md
there names each); CONTENT_BUGS.md seam22 rows cite them.

## Every raider is judged by the room's per-tick rules

- The room watchdog now runs for every raider of the party who is in the raid, not only the
  raider who crossed the barrier (tob_raid.rs2 `~tob_arm_party_watchdogs`; a solo raid
  returns before any `p_finduid`). `~tob_arm_watchdog` is idempotent (`getqueue`), and
  `^tob_var_boss_misses` holds the first-miss tick, so N watchdogs confirm a death on the
  same second tick.
- So a member standing in Maiden's blood takes it every tick, and a member on Sotetseg's
  arena grid off the path is ragged (6.67% of current hitpoints + 15, every tick; range 1 also
  hits a neighbour) and spawns the arena tornado on the fourth row. Grade a member's room
  damage from the leader's tick log: `hit_player pid n-1`.
- Source: Strategies :803 and wiki_Sotetseg :97 (the tornado, below). Stale comments that
  still say only the starter's watchdog runs: tob_hud.rs2:250-260, tob_spectate.rs2:295-297
  (comment-only; not this seam's files).

## Sotetseg's maze with three raiders

- The leader (slot order first) is the runner. In a party the runner never sees a tornado:
  "This tornado will not appear for the maze runner (unless they are the only player)"
  (Strategies :803). The other two land on the far tile 6415,84 (room-local 15,20, arena
  column 6, row -2).
- Find the path start from the arena side with `t.world.loc_near('tob_sotetseg_lighttile',
  30)`: it mirrors the runner's tile, and the runner stands on the start tile after landing.
  Rows count from 0 at z 86. The first arena step onto row 3 (start z + 3, "the fourth row")
  spawns the tornado (npc 8389) at the path start; it walks the path and hits 35-45 on its
  tile. A raider still on rows 0-2 does not despawn it; it goes only when nobody in that world
  is past row three.
- The source's recipe: "The maze runner should stop on the third row and wait a few seconds
  to let their teammates position themselves" (:803), and the arena raiders walk the path
  behind the runner. Members who wait off the grid (the rejected tob_sotetseg_normal) never
  spawn the tornado and are not what the room tests. Scratch: `s22_sote_trio.lua` in the
  pinned dir (Normal trio, `::tobmazearm`: one arena tornado on tick 39, p2 ragged off the
  path every tick, p3 hit 35 by the tornado).
- An unprayed Sotetseg ball blocks all three protection prayers for 5 ticks
  (`~prayer_block_protection(^tob_sote_prayer_disable_ticks)`, tob_sotetseg.rs2:394; spec
  row `sotetseg.prayer_disable`, [wiki][guide][jagex] C; Retribution, Smite and Redemption are
  untouched). A raider who misses one ball cannot pray the next for 5 ticks: plan the eat.

## Prayer drain is the source's

- Protect from Magic (12) + Rigour, Piety or Augury (24) drain 36 a tick against a
  resistance of 60 + 2 x the prayer bonus: 99 points last 165 ticks at bonus 0 (wiki Prayer
  :459-463, :301-304, :405-409, pinned as `sources/wiki_Prayer.wikitext`; LostCity
  prayer.rs2:164-173). Measured 99 -> 63 in 60 ticks, 0 after 167 (`s22_prayer_drain.lua`).
- Protect alone is a point per 5 ticks. A Normal room that prays offence needs restores, or
  flicks the offensive prayer.

## Xarpus phase 2 with three raiders: every spit chains to the others

- A spit's splat now chains to the next raiders in orb order after its target: the phase's
  first spit throws 1 orb, every later spit 2, each at that raider's tile. The orb goes to a
  random uncovered tile instead only when the walk comes back to the target, the raider is
  caged or gone, or already stands on the landing tile. Chained orbs do not chain again
  (Strategies :836/:838). Blert: 115 of 117 trio chained splats (40 of 40 at four) land on a
  raider's tile, which refutes the old 50/50 coin's random-tile splash at party scale
  (`an_blert_xarpus*.txt` in the pinned dir).
- So every raider gets an orb on its own tile shortly after any teammate's spit lands: step
  off your tile after a teammate's landing as well as your own. The rejected
  tob_xarpus_normal's dodge recipe was fitted to the coin and must be re-authored.
- Measure `chain_count` as the orbs thrown from each spit's landing tile within 12 ticks;
  expect 1 then 2s (`s22_xarpus_trio.lua`: 1,2,2,... over 13 spits, all 25 orbs on raider
  tiles). Solo is unchanged (the coin's 1-2 random tiles; an Open content row).

## Verzik phase 1 with three raiders: the Dawnbringer passed by special attack

The owner, 2026-10-05: "the players need to take the dawnbringer from the skeleton on the
ground after xarpus. That weapon does not have the shield penalty and the players should
share it using their special attack".

- Our P1 matches Blert's 10 Normal trio rooms (`an_blert_verzik_p1*.txt` in the pinned dir).
  On the auto ticks the raiders stand on room-local (26,29) = 6426,93 (the hide tile behind
  the south-west pillar at 6425,94) 90 times and on (30,34) = 6430,98 (the melee tile, south
  of her south-west tile 6430,99) 26 times. The pillars are at x 6425 and 6437, z 94, 88
  and 82; the west hide tiles are 6426,93, 6426,87 and 6426,81. Attack from 6428,93, not the
  hide tile (the pillar blocks the line; "Verzik phase 1: pillars, bombs" above).
- Autos come every 14 ticks (the first wind-up about tick 19 of the fight,
  `verzik.p1_first_windup`). `t.npc.await_anim(boss, 8109, 14)` returns on the wind-up tick
  W and the bolt launches on W+3, so a raider on the melee tile has about 3 ticks to reach
  the hide tile, 5 tiles away: run. A prayed bolt does 0-68 ("reduce the damage by 50%, or
  68 damage", Strategies :877); Blert's prayed drops were 0 hidden or 1-57. A raider who
  tanks prays Magic and eats.
- Real trios end P1 in 58-116 ticks (median about 70, 3-7 autos) with 10-16 Dawnbringer
  specials (75-150 each, exempt from the damage cap, tob_damage.rs2) between the autos. The
  raid has ONE Dawnbringer (the skeleton `tob_skeleton_with_weapon` at 6435,109 empties for
  the rest of the raid, tob_xarpus.rs2 `~tob_dawnbringer_take`), so it is passed: "requiring
  players to drop the Dawnbringer for the next player (in orb order) to use" (Strategies
  :875). Between autos the others "safely attack four times with a 4-tick weapon ...
  Afterwards, hide behind one pillar together ... allows for two hits by any 4 or 5-tick
  weapon before requiring to hide" (:883).
- The recipe in verbs (not yet driven end to end on this tree; the rejected
  tob_verzik_normal's leader still died on tick 115 holding the sword alone):
  1. After Xarpus one raider takes the sword: the gate, then
     `t.player.click_loc('tob_skeleton_with_weapon', 1)`, `t.chat.continue_()` and
     `t.inv.await('verzik_special_weapon', 1, 5)` ("Xarpus after seam4" above). It needs a
     free backpack slot.
  2. In P1 the holder wields it, fires its special (`combat_interface:special_attack`, the
     solo recipe "vz11_kill7" above) until the energy is spent, then
     `t.player.drop('verzik_special_weapon')` on its tile and goes back to its own weapon.
  3. The next raider in orb order steps onto that tile at a `t.party.barrier` both pass,
     `t.player.click_obj('verzik_special_weapon')` (op 3, Take; completes on the backpack
     count rising), wields it and specs. No driver verb reads special energy yet: count the
     presses against the weapon's `sa_energy` param (pvm_verzik_special_weapon.rs2:48).
  4. Everyone hides at 6426,93 on each wind-up (W) and is behind the pillar by W+3.
- A kill proves the recipe only with the room's spec rows: P1's hitpoints at three are the
  party sidecar row `verzik.p1_hp_3`.

## Nylocas: spawn_aggro counts the table's aggros, not swaps

- The wave table holds 35 aggro rows, fixed per encounter (Blert mechanics page :177 "aggros
  are fixed"), and every one spawns. An aggro swaps incoming -> fighting only at the box
  edge, so one killed in its lane never swaps but is still an aggro. Count the table's aggro
  rows by wave, lane and size (nylocas_waves.md `*` rows), or add lane-killed table aggros to
  the swaps; the rejected tob_nylocas_normal counted swaps and read 34.

## Comparing a run before and after a change

- A scratch replays a test's trajectory only under the same first 9 characters of the run
  name, because the accounts (and so the seeds) are the run name cut to 9 characters:
  `--name tob_sotetseg_s22c` replays tob_sotetseg_normal (maze 1 at proc 187),
  `--name s22c_sotetseg_normal` diverges (the leader dead on tick 74). An Entry room run
  under another name carries FAIL rows on both packs; compare before and after only under
  one name, and grade a room only under its own id.
- A content before/after without touching the tree: `mk_before_pack.sh` in the pinned dir
  (the recipe "A before/after on content" above), then run with
  `TORIRSSERVER_SCRIPTS=<out> TORIRSSERVER_ALLOW_STALE_SCRIPTS=1` under the same `--name`.
  client.log's `N scripts loaded from <dir>` line proves which pack ran.

# Seam pass 23: watching a test

## Watching a test: the Scripts tab

### Two views: what you see while a script plays (camera seam2, 2026-10-05)

The owner: "I want to be able to watch the runner run its test, but I don't want the
watcher's camera to be affected." Since camera seam2 a Play in a client that presents has
TWO world views (struct App_ViewSplit, src/app.h; runner_view_split):

- **AutomationRunner** (`views[0]`): what every driver verb reads and writes -- its camera
  (the pose ladder, `t.camera`, a shot's re-aim), its pointer, its pick results, its
  right-click menu and its photographs (drawn offscreen on the software lane, through its
  own camera).
- **PlayerClient** (`views[1]`): what is presented, steered by YOUR mouse and keys -- arrows,
  middle-drag and wheel orbit and zoom it; your hover picks through it.

`QD.core_run_test` attaches at Play (`api.drive.view_attach("AutomationRunner")`) and
`QD.finish` / Stop detach; the second view is created as a copy of the first (no jump) and
copied back at the end. A cutscene, a `CAM_*` packet and `CAM_FORCEANGLE` take BOTH views
(owner decision 2). A test run (run.py, SDL dummy, render skip) presents nothing: attach
answers `attached=false` with a reason and nothing is created.

**The Interact switch** (owner decision 1). A toggle on the Scripts page, "Interact (play
while the script runs)", OFF when a script starts. OFF: your mouse and keys are a
spectator's -- orbit, zoom, hover-inspect, the plugin chrome -- and game clicks and typing
are dropped. ON: they play the game through YOUR view (your pointer, your pick, your menu),
never inside one of the runner's gestures (a press waits), and every action becomes a
`watcher.<click|right_click|key|wheel>` PASS row with its tick, written before the script's
next row. The switch is `api.drive.view_interact(on)`; without an attached view it is
refused and the toggle falls back to off.

**What the presented frame shows** (watch_debug_aids, src/app/app_overlay.c "THE WATCHER'S
AIDS"; drawn by the client, not a plugin, so nothing costs anything with one view and no
plugin can cover it):

- THE BADGE, top-centre of the world viewport, on the canvas overlay list (above every
  interface, every lane, every gameframe): "Runner has control" (white on a dark wash) or
  "You can interact (Scripts: Interact is on)" (black on the game's orange `0xFF981F`, a
  yellow edge).
- THE GHOST CURSOR at the runner's pointer (screen space, which the views share): four cyan
  ticks round a dot with a black shadow; a red X while a runner button is held and for
  400 ms after a runner press; under it what the runner's OWN pick holds -- `runner: npc 3105
  at 3212,3230 (2 hits)` (kind, config id, absolute tile, hit count; recomputed only when the
  first hit changes). The game's own yellow/red click cross still lands at the runner's
  press pixel too.
- THE PRESS OUTLINE. The runner's pixel means nothing in your picture; the thing it pressed
  does. When the runner presses a menu row on the world, the npc, loc, obj or player that
  row names is outlined in cyan through YOUR camera for 1.5 s (the hover outline's model
  path, `app_overlay_outline_element_model`); a Walk-here row outlines its tile. HOW IT
  KNOWS: while the runner's menu is open, each presented frame records its rows' y bands
  and picks (the driver's row press closes the menu in the same input step, so no frame
  ever sees the menu and the press together); a NEW click cross while the runner moved last
  is the press, and the recorded row under the runner's pointer is what it pressed. A
  `drive.op` bypass (no pointer, no menu) and a minimap walk draw no outline.
- MENUS (runner_view_split): the menu of the view that is not presented is drawn under
  yours in `0x474745` and takes no physical click; yours draws as usual on top.
- None of this reaches a script's photograph: the world items are built only while the
  frame's view is yours, and the runner's re-walk of the emit for a shot gets the canvas
  list cut back to the plugins' items.

**The page's readings** (script/plugins/script_runner.lua; every 10 frames, rows never
added or removed, each under 192 characters): `Control` (the badge's words, or "no script
attached: your mouse and keys play the game"); `Under your pointer` (YOUR pick:
`api.input.hover_entity` / `hover_tile`, which answer for the presented view -- `npc Hans
(id 3105) at 3221,3219 level 0`, `tile 3222,3218 level 0, nothing on it`; the name comes from
one walk of that kind's pool, only when the hovered thing changes); `Runner pointer`
(`api.drive.view_status().runner`: `386,129  picked 3  menu open  camera yaw 1024 pitch 300
zoom 600`). WHAT THE PAGE CANNOT SAY: what the runner's pick holds -- no plugin read of
`views[0].world_pickset` exists (`drive_push_view`, torirs_plugin_drive.c, would carry it),
so the page gives its count and the ghost cursor's label gives its contents.

**What is and is not deterministic.** The runner's picks are drawn on demand exactly as
render skip draws them (owed by a push, or late on a read), so a watched run's timeline
equals a render-skip run's; orbiting and zooming your own camera the whole run changes
nothing (cooks_assistant and tob_maiden ledgers identical, ticks and shots included, with
and without a watcher orbiting). NOT deterministic: anything you do with Interact ON (it is
real input, recorded as `watcher.*` rows), the moment you press Play, and the account
NAME (each name has its own random stream, so a combat room's watched ledger can differ
from its kept one -- not because of the views).

**Cost.**

| state | per frame |
|---|---|
| headless test run (run.py) | one view; nothing new; attach refused with a reason |
| client, no script attached | one view; the aids are one branch on `view_split.attached` in two overlay builders |
| client, script attached, nothing pushed | the presented frame + badge and ghost cursor (about 15 canvas items) |
| client, script attached, runner picks/shots | + an offscreen software frame through the runner's view when a push owes one or a read needs one: 181 for 1691 presented in cooks_assistant (~5/s at 50 fps; 110 for reads, 46 for shots) |
| a runner press on the world | + the outline (14 world items in the Hans proof) for 1.5 s |

`./launch bench osrs239-bench --renderer soft3d`, no script attached, interleaved A/B: the
split vs HEAD +0.6% summed frame p50 (noise); the aids vs the split alone 28.38 vs 29.12 ms
(two runs each, -2.5%, noise).

**Proof knobs.** `TORIRS_VIEW_SPLIT_FORCE=1` (+ `--render-every-frame`) forces the two
views headless; `TORIRS_VIEW_TRACE=<file>` both views' poses per frame;
`TORIRS_VIEW_SIM_ORBIT=start,end` and `TORIRS_VIEW_SIM_EVENTS='frame:op:a:b:c;...'` physical
input; `TORIRS_WATCH_TRACE=<file>` one line per presented frame while attached: interact,
badge words and box, ghost x/y, buttons, click cross, runner menu open and rows recorded,
presses, the press mark (pick kind, element, ids, tile, items pushed, row text), the ghost
label, and `cuts` (photograph walks the canvas was cut for). Measured
(build/watch_aids/wa_aids.lua, `--name wa_aids3`, all 5 rows PASS): 449 presented frames,
the badge "Runner has control" on 359 and "You can interact" on the 90 between
`t.view.interact(true)` and `(false)`; the ghost at the runner's pointer on every frame;
`t.player.talk_to("hans")` -> press at presented frame 187, kind NPC, element 1073761432,
npc id 3105, row "Talk-to Hans", 14 outline items on each of the next 75 frames (1.5 s at
20 ms); `cuts=1` for the shot `001-aids.talk`.

### t.view.attach / detach / status / interact / watcher (camera seam2)

No room test calls these: `QD.core_run_test` attaches for a Play and `QD.finish` (or a
Stop, in C) detaches. They exist for the conformance rows and for probes. Every one
answers `"ok", status` where status is `{attached, views, presentable, interact,
reason?, lane_refusal?, runner = {yaw, pitch, zoom, pointer_x, pointer_y, menu_open,
picked, eye_*}, watcher = {...} (attached only), offscreen, offscreen_reads,
offscreen_shots, presented, delivered, dropped, held, watcher_serial}`.

- On a test run (run.py: SDL dummy, render skip) `t.view.attach("AutomationRunner")`
  answers `attached=false, views=1` with a reason: nothing is created and nothing new runs
  per frame. Any other role name is `refused`.
- `t.view.interact(true)` is `refused` while nothing is attached; with no argument it is
  a read.
- `t.view.watcher(after)` lists the watcher's actions while Interact was on, after serial
  `after` (the last 32 are kept); `core.lua` turns each into a `watcher.<what>` row.
- Lanes: the software and OpenGL3 lanes carry the second view. D3D9, GLES and WebGL
  answer `attached=false` with `lane_refusal` set, and a lane switch while attached
  detaches. On those lanes a Play still moves the presented camera, as before.

### The Scripts tab itself (seam23, seam24)

Seam23 built the tab for the six solo raid rooms off a prepared index; seam24 (2026-10-05,
scripts_tab_every_script) replaced that plumbing. What it is now:

- The owner's ONE command: `./launch run osrs239-scripts`. Log in with any name, open the
  Scripts tab on the plugin rail (the play-triangle icon), pick a Suite (All, Quests,
  Raids), type in Search, click a row, Play. Play logs the watcher out, makes a fresh
  account, logs it in and plays the test at real speed. Stop ends it at its next step.
- THE LIST IS ASKED FOR, like the plugins are. The client asks for the scripts manifest
  `tests/tests.ini` (`api.drive.tests`, torirs_plugin_drive.c) as ONE script item through
  the IO layer (task_plugin_io.c `CreateTask_PluginScriptRead`, the SCRIPT kind
  `plugins/plugins.ini` is): natively a file under `script/`, on the browser lane the served
  script directory. `[test:<id>]` sections: `suite`, `title`, `source`, `fixture`, `legs`,
  `party`, `max_frames`, `available`, `reason`. `TORIRS_TESTS_MANIFEST` moves it (a
  script-dir-relative path, the `TORIRS_PLUGIN_MANIFEST` convention); the profile does not
  set it. There is no `TORIRS_SCRIPTS_INDEX`, no `index.tsv` and no prepare step.
- HOW IT COMES TO EXIST. The profile's `[derived:tests]` block (`out=script/tests/tests.ini`,
  `command=tools/raid_gate/prepare_scripts.py --out {out}`) is run by the launcher on every
  launch (tools/launcher/profiles.py `run_profile_derived`, called from
  `generate_resolved_manifest`, which `cli.build_plan` calls before anything starts). Not a
  world-manifest `[derived:*]` block: staleness.py rebuilds those with a make target, and a
  50 ms file is cheaper to write than to check. `prepare_scripts.py` reads ONE table of
  tests directories (`TEST_SUITES`: quest test/quests, raid test/raids; a new suite is one
  line plus its link), skips `_` files, and lists a party test (`party = N`) as unavailable
  ("needs N clients"). `script/tests/.gitignore` keeps the generated file out of git.
- HOW A SOURCE IS READABLE. io_server refuses a path containing `..`, so the tests are
  reached through two committed directory links, `script/tests/quests -> ../../test/quests`
  and `script/tests/raids -> ../../test/raids`: `source=tests/quests/cooks_assistant.lua`
  IS the file in the tree (no mirror to go stale), and io_server's fopen follows the link
  the same way. A Windows checkout without `core.symlinks` gets text files instead.
- HOT RELOAD. Play reads the source (and the fixture) again, every time, through the same
  IO path; nothing caches a test. Edit `test/quests/<id>.lua` or `test/raids/<id>.lua`,
  Stop, Play: the edit runs. Refresh re-asks for the manifest; a NEW test file needs the
  manifest re-derived first (`python3 tools/raid_gate/prepare_scripts.py`, or a relaunch).
  NOT hot-reloaded: a running script is never swapped mid-run, and the driver's own Lua
  (script/plugins/quest_driver/*.lua) is read at client start. The driver plugin IS
  reloaded after every run (a fresh Lua state, `drive_demand_release` ->
  `PluginHost_Reload`), but from the bytes the boot read: the host retains the source for
  reload (torirs_plugin_lua.h), it does not re-read the file, so a verb edit needs a
  restart. No "Reload driver" button was wired.
- A FRESH ACCOUNT PER PLAY (`api.drive.play`). C picks the account: up to eight of the
  id's letters and digits plus the first number whose save file
  (`ToriRSServer_SavePath`) and session dir do not exist yet (`cooksass1`, `tobmaide2`; at
  most 12 characters, never reused across Plays or restarts). The fixture, read as a script
  item, is written as that account's save with its `name = ` line rewritten: run.py's
  `write_session_fixture`, done in C because the sandbox has no io and C holds the bytes.
  Ledger and shots: `build/quest_gate/watch/<account>/` (ABSOLUTE: a relative capture dir
  is put under the plugin prefs' asset directory by App_RequestScreenshot, measured). The
  session dir's basename is the account, so `t.session.relog` inside a test logs back in
  as it.
- THE WRAPPING IS LUA. The sandbox has no `load`, so C compiles the raw test
  (`PluginLua_TestThreadCreate`, a bootstrap that calls `t.core_run_test(loader, options)`
  instead of `loader().run(t)`). `QD.core_run_test` (quest_driver/core.lua) builds the
  test's table exactly as run.py's wrapper does, settles the shot latch, logs out
  (`t.session.logout`), logs in as the account (`t.session.login`), then runs
  `core_run_test_wrapped`: run.py `write_wrapper_script`'s QUEST.run copied from the
  generated Lua (the login-grant wait, the setup list with its give/wield/setlevel/clearinv
  read-backs). KEEP THE TWO IN STEP (run.py says so beside it). A failed log-out or log-in
  is one FAIL row, `watch.account`, and the end.
- LEGS. run.py's FULL run of a legs file is one process: `QD.core_legs_drive` runs every leg
  in order with a `::checkpoint k` after each all-PASS leg, no relog. A Play does the same
  (no `from`, no `only`), so a legs file plays in one sitting; the status reads "leg k of n"
  off the `leg.<k>.` rows. No legs file is unavailable for this reason.
- THE CLIENT'S VARPS OUTLIVE A LOGOUT. Nothing in this engine clears them, and the
  embedded server's login sends only the new account's non-zero varps, so the second
  account of a session read the first one's values for every varp it holds at 0. Measured
  on the first build: seaslug1 after cooksass2 and hetty1 after doric1 each failed
  quest.points with "qp (varp) 1 -> 1" (PASS in the suite). `core_run_test` now calls
  `api.drive.forget_varps()` between the log-out and the log-in (VarPManager_ResetAll,
  what a VARP_RESET packet does; refused unless a Play runs and the client is on the
  title screen). A test run never meets it: one account per process. ENGINE QUESTION, not
  settled here: does the real client reset varps on logout, or the server send VARP_RESET at
  login? Either would make the driver's reset redundant.
- THE CAMERA OUTLIVES A RUN too. A test's camera verbs leave the pose where they put it and
  a logout does not reset it: in seam24's fin3, tob_verzik played after five rooms started
  top-down and its click on Verzik found nothing (verzik.talk FAIL), while the same Play as
  a client's first passed. The C side saves the camera pose at the FIRST Play of a client
  (`DrivePointer_CameraPose`; headless it read yaw=0 pitch=128 zoom=600) and
  `core_run_test` puts it back after every log-in (`api_drive.camera`); proved by xarpus
  then verzik in one client (xv1: verzik.talk and verzik.begin PASS).
- THE SHOT LATCH (seam23 open item). One capture outstanding at a time is C state in
  torirs_plugin_drive_ui.c; a run that ended with a capture in flight left it for the next
  run's first `t.shot`. `core_run_test` takes one capture, `watch-start`, before anything
  else: it collects a stale request (its file lands in the OLD session's shots/) or
  photographs the world as Play found it. Either way the latch is empty when the test's
  first `t.shot` asks.
- THE PAGE (`script/plugins/script_runner.lua`). Rows, never added or removed: Suite (a
  select), Search (node kind 5), the match count, Selected, Play, Stop, Interact (a toggle),
  Control, Under your pointer, Runner pointer (camera seam2, above), Driver, Test (suite,
  id, account), Leg, Step, Rows, a note, 12 list slots (action rows: the id, then the title
  or `[n legs]`, `selected:` on the chosen one; an unavailable test reads
  `<id>  (unavailable)` with its reason and answers its reason in the note when clicked),
  "n more: refine the search", Refresh, Summary, Session. Sorted by suite then id. A filter,
  a keystroke, a selection or a status change is `set_text` / `set_label` / `set_value` on
  those rows; `on_ui_build` produces the same row set every time. That is what keeps the
  search box's keyboard: the box is never re-created (seam23's 600 ms settle and its lost
  focus are gone), and the list follows every key.
- NOT DONE, and why. Unavailable rows are not GREY: the host draws an action row with no
  disabled state (torirs_plugin_panel.u.c builds ACTION_ROW without
  `ToriRSChrome_SetDisabled`; a button is the only row it greys, and a button clips its
  caption to the label column). Needs one line in the host: apply `model->value` to an
  ACTION_ROW the way BUTTON does. Button captions follow the host's convention: a disabled
  caption is drawn in `text_dim`, which the OSRS theme sets to the label colour (orange),
  and an enabled one in `text` (white); that reads backwards in this theme and is the
  theme's to change, not the plugin's.
- THE WINDOW (seam25, watched_client_mouse_mapping, 2026-10-05). The owner: "the mouse
  coords are WAYYY OFF" at the login screen of the default window on a Retina Mac; fixed
  for him by `TORIRS_HIDPI=0 ... -- --soft3d --window 765x503`. The profile now pins
  exactly that (`[args]`, `[env] TORIRS_HIDPI=0`): it is the window every test is graded
  in, so a watched run looks like what the test saw, and the one command needs no
  arguments. What was measured about the cause: the SDL mapping is ONE pure function now
  (`platform/platform_pointer_map.h`; `PlatformWindow_MapMouse` and the present's game
  area both read it), and `make -C src test-sdl-pointer-map` proves a press lands on the
  layout pixel the present draws there across 1x/2x, pane closed/rail/page+rail (grown
  and carved), the title's 765x503 layout and the in-game resizable one, and the real
  `platform_sdl2.c` MapMouse under the dummy driver at 1x and a claimed 2x (698 checks
  each, 0 failures). The two candidates' sizes, measured: a density-blind read puts
  Existing User (462,291) at 231,145; a pane-blind one at 442,291 (rail) or 244,272
  (carved page). Candidate (b) does not arise: at the title the chrome reports
  `rail_hidden=1 column=-1`, so `api.panel.request` at plugin start opens no pane there
  (script_runner.lua unchanged). NOT reproduced headless: the default GPU lane (GL3 is
  unavailable under the dummy driver) and the real Cocoa WKWebView pane; the factor-2
  error a density-blind read gives is the size of "way off", and it can only live there.
  Open item, for the owner's eyes: the profile with its `[args]` and `TORIRS_HIDPI=0`
  lines removed (the GPU lane, HighDPI drawable) and the pointer on Existing User.
- REAL-EVENT KNOBS (platform_sdl2.c, headless only in effect):
  `TORIRS_SIM_SDL_CLICK_AT=frame,x,y[,right][;...]` pushes SDL_MOUSEMOTION, then
  SDL_MOUSEBUTTONDOWN/UP three and four frames later, in WINDOW POINTS, through
  SDL_PushEvent, so the press travels the pane routing and MapMouse a person's does
  (`TORIRS_SIM_CLICK_AT` puts layout coordinates on the bus and cannot see a mapping
  bug); each release logs `released x,y points -> layout x,y`. `frame` counts
  PollCommands calls. `TORIRS_SIM_PIXEL_DENSITY=2` makes a `SDL_VIDEODRIVER=dummy` window
  claim a drawable twice its points (ignored on any real driver). Measured with the
  profile's env: Existing User (462,291) then name, password and Login (302,321) through
  the SDL path -> `login user='probeone' session=ok` at 1x and `'probetwo'` at 2x
  (canvas 765x503 in a 1530x1006 drawable), and under the pinned args; the Scripts
  page's Search (210,173 in the 807x503 floating pane) -> `search 'cook' -> 1 rows`.
- Headless drive of the tab (profile env + `SDL_VIDEODRIVER=dummy`; harness
  `build/seam_state/matthew-mbp-m4-raid-b1-seam24/panel/run_watch.sh`):
  `TORIRS_SIM_PLUGIN_PANEL=<tick>,script-runner,page`;
  `TORIRS_SIM_PANEL_PICK=<tick>,script-runner,suite,quest` (or `raid`, `all`),
  `...,slot<k>,!activate`, `...,play,!activate`, `...,stop,!activate`;
  `TORIRS_SIM_CLICK_AT=<frame>,210,173` focuses Search in the 807x503 floating pane, then
  `TORIRS_SIM_TYPE=<frame>,c99;<frame>,c111;...` types (one burst per key gives pauses;
  `k85` is backspace). Panel ticks and frames advance together.
- MEASURED headless (2026-10-05, build/seam_state/matthew-mbp-m4-raid-b1-seam24/panel/,
  the profile's env + dummy video + `TORIRS_EMBED_CLOCK_MS=20`; ledgers in
  build/quest_gate/watch/<account>/):
  - the manifest arrived as ONE script item ("tests manifest: asking for script item
    tests/tests.ini (read 1)", 25900 bytes): 131 tests, quest 119 (all playable), raid 12
    (6 playable, the six `*_normal` party tests "needs 3 clients"). The tree has 123
    `test/quests/*.lua`, four of them `_` harnesses: 119 quests, not the 123 the triage
    counted from `ls test/quests` (which counts README, QUEUE and the directories). 18 legs
    files by `lint_quest.legs_layout`.
  - one client, three fresh accounts in a row (fin3): Quests, search `cook`, cooks_assistant
    on cooksass5 to `SUMMARY 48 PASS` through leg 3 of 3; search `theslug`, theslugmenace (a
    legs file) on theslugm1 to `SUMMARY 146 PASS`, leg 3 of 3; then the raid rooms. Both
    quests' steps and verdicts are IDENTICAL to their suite ledgers.
  - five more quests across the alphabet in one client (smp2): doric, hetty, priest, sheep,
    xmarksthespot -> 21, 29, 29, 14, 47 PASS, every one's steps and verdicts IDENTICAL to its
    suite ledger (hetty after doric only once the varps were forgotten, above).
  - the rooms (fin3, one client, after the two quests): tob_maiden to its end `116 FAIL`
    (pass=111; auto_prayed_entry, scan_lead, drain_stat, tech.sidestep_scan, tech.bow_flick:
    timing rows), tob_bloat died at row 14, tob_nylocas `171 FAIL` (pass=170,
    explosion_radius), tob_sotetseg `160 PASS`, tob_xarpus died at row 30, tob_verzik's
    setup PASSED (seam23's `setup.::give serpentine_helm_charged` failure after three rooms
    is gone: a fresh account, no `::clearworn` needed) and then failed verzik.talk on the
    leaked camera (fixed, above). tob_bloat as a client's FIRST Play died too, and raid
    run.py on a copy of tob_bloat under account `tobbloat1` (virtual clock, no tab) ended
    `87 FAIL` (pass=85) against the kept `94 PASS` under `tob_bloat`: a room's outcome
    follows the account name's random stream, so a watched room cannot reproduce its kept
    ledger. On the final build (rooms1, one client): tob_maiden `113 FAIL` (pass=108),
    tob_bloat `91 FAIL` (pass=79; it overran its slot in the fixed headless schedule, so
    the scheduled nylocas Play met a disabled button), tob_sotetseg `158 PASS`, tob_xarpus
    died at row 30, tob_verzik as the fifth room: setup PASS, verzik.talk and verzik.begin
    PASS, `256 FAIL` (pass=240: the p3_death, trapdoor, loot and chest rows).
  - hot reload (h2): a scratch tests root (`prepare_scripts.py --suite raid=<dir> --out
    <dir>/tests.ini`, `TORIRS_TESTS_MANIFEST=../build/...`), Play tob_maiden to rows 1-14
    PASS (barrier.click), Stop, the scratch copy edited, Play: the source read 112702 bytes
    instead of 112690 and row 1 read `mode=entry party=1 HOTRELOAD-2`.
  - search keeps the keyboard: eight characters typed one burst per key ~1.3 s apart
    (`t`,`o`,`b`,`_`,`m`,`a`,`i`,`d`) each reached the box ("search 't'" ... "search
    'tob_maid'"), with no click between them.
  - crops: the floating pane with Suite Quests, the focused box holding `cook`, "1 of 119
    quests match 'cook' (1 playable)"; the fullscreen pane with Suite Raids, "12 raids, 6
    playable.", Selected raid tob_bloat, Play white (enabled), Stop orange (disabled), and the
    rows "tob_bloat / selected: Theatre of Blood, Bloat, Entry solo" and "tob_bloat_normal
    (unavailable) / needs 3 clients ...".
  - strict scan meter on every long run: no `uitree:` line.
  - the closer, on the final tree (./src/torirs, panel/close_a and close_b): the manifest
    landed as one script item (25900 bytes, 131 tests: quest 119 all playable, raid 12 with 6
    playable); `cooks_as` typed one key per 80 frames after one click, every key reached the
    box; cooks_assistant on cooksass6 to `SUMMARY 48 PASS`, leg 3 of 3, steps and verdicts
    identical to build/merge17_check/cooks_before.tsv. Hot reload again on a scratch copy of
    tob_maiden: Play read 112690 bytes and reached barrier.click, Stop, the copy was edited,
    the next Play read 112702 bytes and its row 1 read `mode=entry party=1 HOTRELOAD-2`. The
    frame at 2300 was read through OCR (the editor's image hook timed out): Suite Quests,
    Search `cooks_as` with a yellow focus border, "1 of 119 quests match 'cooks_as' (1
    playable)", Selected "none (pick a row)", Play, Stop, Driver idle.
- A watched run is NOT a test run and grades nothing: the wall clock (or
  `TORIRS_EMBED_CLOCK_MS` headless), a Play at a moment of the watcher's choosing, and an
  account whose random stream is seeded from its name. A quest whose path does not roll
  matched its kept ledger row for row; a combat room can differ (seam23: tob_maiden left the
  kept ledger at row 29). The native plugins do not touch the play (seam23: byte-identical
  ledgers with them on and off). Party tests stay unavailable: three clients in lock step.
- Test runs are unchanged: run.py keeps its own wrapper and never reads a profile; every
  new path is behind `TORIRS_DRIVE_ON_DEMAND=1` (cooks_assistant and druid ledgers
  byte-identical to build/merge17_check/ after the change).

## On demand: t.drive.start, t.drive.stop, t.drive.status

- `TORIRS_DRIVE_ON_DEMAND=1` installs `api.drive` in an ordinary client (no
  `TORIRS_CONTENT_TEST`), and nothing starts at world-ready. `api.drive.start(path,
  session_dir)` (`t.drive.start`) checks the request, creates `session_dir/shots/`, and the
  coroutine begins on the next pump the world is ready for, exactly where a test run's
  starts. It answers `refused` with a reason when a script is running or still ending, when
  the quest-driver plugin is not running, when the world is not ready ("log in first"), or
  when the file is missing. A reused session directory loses its old `ledger.tsv`,
  `ticklog.tsv` and `heartbeat`; old shots past the new run's count stay.
- `api.drive.stop()` (`t.drive.stop`) ends the run at its next yield that is not a `t.shot`
  (a shot in flight is let finish, or its PNG would land in the next run). The ledger gets
  the two lines run.py writes for an unfinished run: `run.unfinished FAIL ... last row
  written: <step>` and `SUMMARY ... exit=none`.
- `api.drive.status()` (`t.drive.status`) answers `ok` and `{state = idle|running|finished,
  script, session, step, verdict, rows, pass, fail, blocked, summary, exit, on_demand, runs,
  starting, stopping}`; `step`/`verdict` are the last ledger row, `summary` the SUMMARY line
  once finished.
- On a TEST run start and stop are refused ("not an on-demand client") and status reads the
  run itself (`state=running`, its own script, `on_demand=false`). None of the three is ever
  `unsupported`. Conformance rows `drive.status`, `drive.start`, `drive.stop` prove exactly
  that; the on-demand path cannot be set per row and is proved headless (the Scripts tab
  section above, and the seam23 closer's run `close1` in SEAM_LEDGER.md).
- A finished, stopped or erroring script never ends a watched client: `PluginDrive_Finished`
  answers 0 under the knob. At the next frame boundary in main.c (outside every plugin
  callback) the driver destroys the coroutine, drops the tick log and RELOADS the
  quest-driver plugin, so no Lua part's per-script state (shot counter, row tallies, party
  counters) reaches the next run. A plugin a script error disabled is switched back on first.
- A watched client's world clock is the transport's own: wall time, or
  `TORIRS_EMBED_CLOCK_MS` per frame for a frame-locked headless proof (main.c hands the
  driver the embed through `NetTransport_TestClock` with that clock). Never set
  `TORIRS_CONTENT_TEST` in a watched profile: its mailbox clock stays paused.
- (seam23, superseded by seam24's api.drive.play) Consecutive api.drive.start runs share one
  account's world: Verzik after three other rooms on one account failed `setup.::give
  serpentine_helm_charged`. A Play gives every run a fresh account from its fixture instead,
  so no `::clearworn` is needed ("Watching a test: the Scripts tab").
- `t.session.login` (the relog verb) types the session directory's last component as the
  account name. A Play's session dir is `build/quest_gate/watch/<account>/`, so a test that
  relogs logs back in as its own fresh account.
- (seam23, REPLACED in seam24) `prepare_scripts.py` no longer writes wrapped copies or an
  `index.tsv`: it writes the scripts manifest the tab asks for, and a Play reads the test
  file itself.

## Several inputs in one tick: `t.together` (seam27)

The owner, 2026-10-05: "update the script runner so that it can do multiple things at
once ... it's really slow to equip, eat move around". Before this seam every fast verb
pressed and then waited for its own effect one tick later, so N presses took N ticks.

**The rule from the source.** One OSRS tick runs client input, then npcs, then players
(ENCOUNTER_TIMING.md 1.1). The input phase runs every packet the client sent since the
last tick, in the order sent. LostCity's engine caps it: `NetworkPlayer.decodeIn`
(Engine-TS `src/engine/entity/NetworkPlayer.ts:55-74`) reads while fewer than
`USER_EVENT.limit = 5` user events have succeeded this tick
(`ClientGameProtCategory.ts:6`; IfButton, OpHeld, InvButton and MoveClick are all user
events), and the rest wait in the buffer for the next tick. They are not dropped. Food:
"The 3 tick Eat delay"; "Potions do not incur the standard 3 tick ... delay"; "consuming
a marlin, Saradomin brew, and halibut - in that order - allows 60 hitpoints to be healed
at once" (wiki Food/Fast foods, Combo eating). No source quoted here gives OSRS's own cap.

**What our server does.** `ToriRSServer_SessionPump` (torirs_server_session.c:1066)
dispatches each packet when it arrives, between world ticks. `phase_clients_in`
(torirs_server_world.c:15081) is empty. A press made between ticks T-1 and T is in force
for tick T, in the order sent, and there is **no per-tick cap**. The order matches the
source, but the missing cap does not. Two driver comments disagreed about this:
prayer.lua's "packets run as they arrive" is right. step_tick's "read by the next tick's
phase_clients_in" gets the tick right for the wrong reason. Measured in
build/quest_gate/siot_probe1: a prayer's varbit reads back on the press tick (+0), and a
held press or a step reads back on the next tick (+1).

**OPEN ROW: the per-tick input cap.** LostCity holds a sixth user event for the next
tick. Our server lands every one. This is not fixed here because the dispatch is not in
the mock239 intake files, and a buffered intake is an engine change. Until it is fixed,
`t.together` reports any block of more than five inputs (`6 is over LostCity's 5 user
events a tick (our server has no cap)`) and refuses an eleventh. The number OSRS uses is
the evidence needed to close this row.

**The form.**

```lua
local result, detail = t.together(function()
    t.prayer.set("protectfrommelee", true)     -- prayers first (prayer tab)
    t.prayer.set("piety", true)
    t.player.equip("abyssal_whip")              -- then held items (inventory tab)
    t.player.equip("dragon_defender")
    t.player.eat("shark")
    t.player.eat("tbwt_cooked_karambwan")       -- a combo food: its own timer
    t.player.step_tick(x + 1, z)                -- movement last, one per block
end)
t.check("swap.melee", result == "ok", detail)
```

Inside the body, `eat`, `drink`, `equip` (always the quick press), `inv_op` (the same),
`prayer.set`, `step_tick` and `walk_to` each press and return `"pending"` without
waiting. The block then waits up to `QD.TOGETHER_CONFIRM_TICKS = 2` and confirms every
effect: the pressed cell changed (eat, drink), the worn total rose (equip), the varbit
reached its new value (prayer), and the player reached the tile (step) or moved off the
start tile (walk). Its one detail names each input with the tick it was pressed and the
tick it was confirmed. The block answers:
`ok` when everything was pressed on one tick and confirmed.
`split` when everything was confirmed but the tick rolled over between presses; it names
the late inputs and presses nothing again.
`refused` when an input never left or the server refused it. The detail names it: a
second press of one cell or one prayer, a second movement, a missing item, a prayer level.
`timeout` when an input left but its effect never appeared.

**The cost of a tab change** is frames, not ticks. Measured: six inputs across the
prayer and inventory tabs waited 4 frames (3 for the inventory cell to repaint after the
prayer tab, 1 for the prayer button). A tick is 30 frames, so the client could press far
more than any source shows a player landing. The bound is the cap above, not the client.

**Measured** (build/quest_gate/siot_probe1): two prayers, a whip and a kiteshield, a
shark and a step were pressed on server tick 5 and all confirmed by tick 6. The tick
log's `player_tile` row for the step is on tick 6. The same six inputs through the verbs
one at a time took 4 ticks (9 -> 13).

**Combo eating** (build/quest_gate/siot_probe5, row 1): a shark, a combat potion dose
and a `tbwt_cooked_karambwan` were pressed on tick 3, and all three were confirmed on
tick 4. **One item, one cell:** a block presses the first cell that holds an item, so
`eat("shark")` twice in one block is refused at the second press. That press would land
on the same cell (row 2), and the food gate would refuse a second shark in one tick
anyway.

**Do not**: call `t.exec` or `t.check` inside the body (each would write a row for
`pending`), or put a slow verb inside it (attack, talk, click_loc, a non-quick
`inv_op` outside a block). Those wait, the tick rolls over, and the block answers `split`.

### How a fight loop is written now

1. Each pass, decide the WHOLE intent for the tick from what the boss is doing
   (`npc.state`, the tick log), then send it in one `t.together`. Never write one
   action per pass: an `acted = true` guard that ends the pass after a swap is the
   pattern this replaces (tob_verzik.lua has 34).
2. Do not take a photograph or press a slow verb inside a fight.
   `t.player.inv_op(food, 1)` without `{quick = true}` settles for 3 ticks in a calm
   town and up to 17 in the Nylocas room (seam14).
3. Eat at the food delay, not after it. Use `opts.eat.quick = true` on
   `await_dead_engaged`, or `t.player.eat` inside the block with the combo food
   alongside. Space your own eats by 3 ticks from the PRESS tick
   (food.constant:29 `^eat_delay = 2`, consume_shared.rs2:80/:89).
4. Put movement last in the block, and use one movement per tick.

### The eater: `opts.eat.quick` (seam27)

`opts.eat = { item =, below =, quick = true [, delay = n] [, combo = "<symbol>"] }`
presses with `t.player.eat` and waits `delay` (default `QD.COMBAT_FOOD_DELAY_TICKS = 3`)
from the PRESS tick. `combo` eats a combo food in the same tick through `t.together`.
Each bite in the detail carries `@<drive tick>`. Measured under Chronozon
(build/quest_gate/siot_probe4): bites at 101, 104, 107, 110 and 113, then 117 and 120 once
hitpoints reached the threshold. That is 7 bites in 20 ticks; the default eater managed 4
in the same fight. The default is unchanged. **OPEN ROW:** switching the default to quick
moves every ledger of the 36 kept quests that pass `opts.eat` (arthur belowicemountain
arena chompybird coldwar crest depthsofdespair deserttreasure dragon druidspirit
gettingahead grandtree hauntedmine horror icthlarin insearchofknowledge ikov legends
itgronigen mm porcineofinterest priestperil routequest regicide redreef royaltrouble tbwt
thefremennikisles theslugmenace troll troll_love viking upass zanaris zombiequeen, plus
_conformance). Re-run all of them first.

### The slow settle, measured but not changed (seam27)

A photograph costs about 3.2 frames: `shot-aim ... answered at poll N` counts frames.
- cooks_assistant: 46 shots, 147 frames (about 4.9 ticks) against a ledger total of 116
  ticks, so roughly 4%.
- druid: 67 shots, 208 frames (about 7 ticks).
- legends: 532 shots, 1687 frames (about 56 ticks).
- regicide: 294 shots, 908 frames (about 30 ticks).
These are small next to the slow held press. seam14 measured `inv_op` without `quick` at
3 ticks a press in Lumbridge and up to 17 in the Nylocas room, and tob_nylocas spent
`equip 83 eat 167` ticks on 54 eats and drinks. The share of `QD.settle` and the slow
`inv_op` in a quest run cannot be read from the ledgers and client logs as they stand,
because no line names the verb that waited. **OPEN ROW:** a `QUEST progress` line for each
settle, naming its verb, then a suite run before any default changes. No default was
changed in this pass.

## The play library: `t.raid.play` (seam27)

The owner, 2026-10-05: "the driver is not very fast or good. That is not going to work in
normal mode. You will need to code up the agents a lot smarter using the actual
strategies." Before this seam every room test carried its own fight loop, one action per
pass, reacting after the fact.

```lua
local result, detail, rec = t.raid.play("tob_bloat", { mode = "entry" })
```

`t.raid.play(plan_id, { mode, weapon, max_ticks })` answers `ok`, `died`, `timeout` or
`unsupported`, a one-line detail, and the record (`rec`: downs, flinches, swings, eats,
drinks, `prayer_at`, `hp_at`, `tile_at`, inputs per tick). The code is the banner block
`SEAM raid_play_by_tick_intent` at the end of raid.lua. Each server tick it:
- SEES what a player sees: the boss's animation and tile, the floor markers, its own
  hitpoints, prayer points, lit prayers, tile, and its own swings (`player_anim` rows for
  its own pid; a member, which has no tick log, counts swings from its presses);
- lets the room's PLAN DECIDE the whole intent for the tick;
- SENDS prayers, potion, food and the step in ONE `t.together`, with the attack press
  after the block. A walk is re-issued only when its target moves, never waited on.

It never reads the server's registers, `::tob*` readouts, the seed, or the tick log's
hidden columns. The skills and every plan line, with sources, are in
docs/minigames/raid_loop/PLAY_NOTES.md. The worked example is test/raids/_play_smoke.lua.

**Attack on cooldown.** A click only starts a fight; after that the weapon swings on its
own. Press Attack only when not engaged (after a step), or when no swing was seen for
`speed + 2` ticks. Eat on the swing tick: "If your weapon is ready ... eating does not add
any new delay" (consume_shared.rs2:34-38).

**Supplies.** Eat when hitpoints are at or under the largest damage that can land before
the next chance to eat. On a free tick the horizon is the gap to the next free tick plus
`TOGETHER_CONFIRM_TICKS + 1`. That margin is the seam's own measured choice: a bite on the
swing two ticks before Bloat's stomp was not yet read back one tick later. No source gives
a margin. A brew rides along when the food alone is short (combo eating); a restore is
drunk when a dose's worth of prayer is missing or prayer is about to run out.

**Which plans play.** `tob_bloat` has a decide function (Entry stays and tick-eats the
stomp, then clicks back on the rise; Normal and Hard leave after the last swing that
fits). `tob_maiden` is a strategy table only and answers `unsupported ... no decide
function`, as does any unknown plan id. Conformance row `raid.play` proves both answers.

**Measured, Entry Bloat through the library, five names** (svabloat, svbbloat, svcbloat,
svdbloat, playbloat; 13/13 PASS each): 196-267 room ticks, 66-160 damage taken, 3-8 eats
and drinks, 0 attacks lost to late presses. The kept tob_bloat.lua run took 329 ticks, 724
damage and 41 eats and drinks; under svabloat and svbbloat the kept test never killed
Bloat in about 1,200 ticks.

**OPEN ROWS.**
- The Normal trio (`--party 3`, build/quest_gate/playn3) died: the plan has no Defence
  drain run-by (wiki :687), so Bloat took 666 of 1500 in six downs, and the trio ran out
  of food. The hazard skill is applied to walks only, not to the attack press's own path,
  so p3 died to two hands on the third tick of a down.
- A member has no client read of its own animation (`api_drive.players` has no anim
  field), so its swings are counted, not seen.
- The library is a block in raid.lua, not its own raid_play.lua: the driver is one chunk
  built from DRIVE_SCRIPT_PARTS (src/plugin/torirs_plugin_drive.c), and the sandbox has no
  dofile/loadfile. A separate file needs a one-line C list edit and a rebuild of every
  binary.
- Only Bloat is played through the library, in _play_smoke.lua. The kept tob_*.lua room
  tests still use their own hand-written plays on one seed each, and are re-authored onto
  the library in a later pass (test/raids/README.md states the rule).

## Reading a run's mistakes from its log: the raider rows (seam29)

The owner, 2026-10-05: "use something like what blert does and just look at the log and see
where you went wrong." A failed run is read, never replayed or watched:

    python3 tools/raid_gate/raid_report.py build/quest_gate/<run>            # summary + MISTAKES
    python3 tools/raid_gate/raid_report.py build/quest_gate/<run> --mistakes 40 --timeline 180-195
    python3 tools/raid_gate/seed_survey.py <test id>   # five names; a red one prints its first three mistakes

**The raider's side of `ticklog.tsv`.** Three kinds the server writes for every logged-in
player (so a party's leader log carries every raider, by pid) while `t.ticklog.start()` is on:

| kind | a..g | label |
|---|---|---|
| `raider` (every tick, after the tick's real rows) | pid, hitpoints, prayer points, `varp83_prayer0` (every prayer lit, one bit each, `configs/all.varbit` startbit), weapon obj or -1, `com_mode`, special energy (varp300, 0..1000) | `hpmax H prmax P head I input N tgt S`: input 1 = a client packet (walks included) arrived since the previous raider row; tgt = the npc slot interacted with, or -1 |
| `input` | pid, trigger, subject type, npc slot | the trigger a script ran for, `[opheld1,shark]` (an `[apnpc*]` re-runs every tick of an approach) |
| `consume` | pid, obj, op, hp before, hp after, prayer before, prayer after | the `[opheld*]` that took the obj out of the backpack |

They are **file-only**: never in the row array `t.ticklog.rows()` reads, so they take no
serial (their serial column repeats the last real row's) and no ledger moves -- ledgers print
serials ("mark 'room start' at tick 60 (serial 99)"). `t.ticklog.rows({kind = "raider"})` is
refused, naming the file. `npc_tile` rows now carry the npc's footprint in `f` (file only;
the Lua row does not name it). An in-process client's packets are handled BETWEEN ticks, so
an `input`/`consume` row carries the tick that had just ended and acts on the next one.

**The MISTAKES block** (`raid_report.py`), each with its tick and raider:

- `missed_attack`: standing still, cooldown over (the weapon's cadence = its shortest swing
  gap; food adds three ticks, a potion none), an npc it hits within the reach most of its
  swings were sent from on the tick before AND this tick (melee never from under, never
  across a diagonal), and no swing.
- `prayer`: a hit taken with no protection lit -- or through one -- on the tick its npc's
  attack animation started (in the ten ticks before). A hit with no attack row is not judged
  (counted on a note line). `PINNED_PRAYER` holds the owner's exceptions: Bloat Entry
  (10812) flies read the prayer at launch, up to six ticks before the hit, and Protect from
  Missiles only cuts them 25% (`tob_bloat.rs2 ~tob_bloat_fly_damage`).
- `hazard`: stood on a live hazard tile (`HAZARD_SPOTANIMS`: the Bloat hand landing 1576;
  `--hazard ID[:TICKS]` adds one for a run).
- `food`: food (not a potion) eaten above the most any raider took in one tick of the run.
- `stall`: no input, swing or food for 10 ticks while an npc it hits lived.
- `death`: hitpoints 0 on the raider row (a log from before seam29: the death animation
  836), with the raider's last ten ticks.

A log from before seam29 has no raider rows: prayer and food are not judged, a stall is read
from swings, hits and the first step of each walk (`build/seed_survey_2026-10-05/svabloat`:
"t437-454 stall: no input for 17 ticks", then "t458 died").

**`tools/raid_gate/run.py <test id> --name X`** now runs the test under X (it is rewritten to
`--script test/raids/<id>.lua --name X --fixture <its fixture>`: same file, setup and frame
budget) and needs `--no-publish`; before seam29 the quest gate ignored the name for a solo
test and ran it under its own id.

**The sampler keeps a room only on a green seed survey** (`raid_author.workflow.js`, step 0):
`seed_survey.py <id>` after the room is green under its own name; a non-zero exit rejects the
room with the tool's lines as the finding.

## The play library lives in its own parts (seam29)

`script/plugins/quest_driver/raid_play.lua` holds the loop `t.raid.play` and the shared
skills. Each room's plan is its own part, `raid_play_tob_<room>.lua` (maiden, bloat,
nylocas, sotetseg, xarpus, verzik), listed in `DRIVE_SCRIPT_PARTS`
(`src/plugin/torirs_plugin_drive.c`) after `raid.lua` and registered with
`QD.raid._play_plan(id, plan)`, which asserts when one id is registered twice. A plan
with no `decide` answers `unsupported` with "has no decide function yet: <the plan's
`unsupported` line>"; the five rooms other than Bloat answer that today, each naming the
seam30 row that will write it.

A NEW part needs the C list edit and a rebuild of both binaries, `src/torirs_questtest`
(run.py without `--no-build`) and the profile binary `src/torirs`
(`make -C src -j EMBED_SERVER=1 torirs`), BEFORE any code leaves an existing part: the Lua
is read live, but the parts list is compiled in.

## Every move goes through the hazard skills (seam29)

`_play_hazard` picks the safe destination. `_play_safe_step` makes every tick-end of the
walk safe: the server's route on open floor was measured both diagonal-first (playn3) and
straight-along-the-longer-axis-first (svaplaysmoke t164-166), so the first two tiles of both
shapes are checked. `_play_reach` replaces an attack press with a walk to an unmarked
edge-adjacent tile while any marker is on the floor (the attack press's own server path
goes through no skill), and holds the press when every reach tile is marked; it leaves the
footprint's diagonal corners out (a conservative choice; no source in the tree states it).
A plan sends its own walk, the approach, and a step off a marker under a standing raider
through them.

Not yet proved: the Normal trio (`--party 3`, run s29n3e) still took 5 hands, each on a
tile entered exactly 2 ticks after its shadow was drawn, under three different safe-step
shapes. The suspected cause is a visibility lag (the shadow not yet in
`QD.world.spotanims` when the loop decides); the loop's decisions are not logged, so it
is not shown.

## Classifying a hand from the tick log (seam29)

A 1576 `map_spotanim` at tick T on tile X hits a raider whose `player_tile` at T-1 is X.
If the raider entered X at or after the 1570-1573 shadow's tick (T-3), it pathed onto a
visible marker; otherwise the marker appeared under it (a stunned raider cannot move).
The arrival tick is the start of the run of unchanged `player_tile` rows.
`raid_report.py`'s `hazard` mistake lists the hand; this rule says whose fault it was.

## A technique row needs evidence the play may never give (seam29)

`tech.protect_from_missiles` and `tech.step_off_shadow` both need an event (a fly landing,
a shadow on the raider's own tile) that better play makes rarer. Seam29 saw both rows fail
on evidence alone, not on play. Read the row's numerator before reading a FAIL as a play
fault. `_play_smoke`'s `tech.protect_from_missiles` window is now the fly's own flight
(the earliest launch within six ticks of the hit, to the hit), because
`tob_bloat.rs2 ~tob_bloat_fly_damage` reads the prayer at launch; the kept
`test/raids/tob_bloat.lua` row keeps the old six-ticks-back window.

## A pinned prayer mistake on Bloat's rise tick may be the stomp (seam29 closer)

`raid_report.py`'s `PINNED_PRAYER` judges every hit by npc 10812 (Bloat Entry) as a fly.
On `_play_smoke` it lists "t148 took 38" and "t224 took 21" as prayer mistakes, but an
Entry fly does at most 8 unprotected, so those hits are larger than any fly (most likely
the stomp). Until the rule bounds the damage, read a pinned prayer mistake above the fly
maximum as a different attack, not a missing prayer.
## Starting state: `::resetcharacter`, `t.session.reset`, `t.session.held` (seam25)

The Scripts tab used to start every Play on a fresh account (seam24), and a run
on one account carried the last script's gear into the next one: seam23's
Verzik setup ran out of backpack after three rooms. `::resetcharacter` cleans the
live character without a relog.

- **`::resetcharacter`** (torirs_server_world.c, beside `::clearinv`) empties the
  backpack and every worn slot (removed, not dropped), leaves an active
  Theatre/Chambers/Tombs through content's own `~tob_leave`/`~cox_leave`/`~toa_leave`
  (this frees the room and moves the player outside), drops the current action,
  runs the clearers death.rs2 runs on a respawn (poison, venom, disease, antifire,
  prayers off, skull, imbued heart, the personal hit queues, special attack back
  to 100%), clears the timed potion and spell effects death leaves behind (stamina,
  overloads, divine potions, prayer regen/enhance, Menaphite remedy, hunter meat,
  freeze, teleblock, vengeance, the Theatre's per-raid registers, and their timers
  and queues), puts every stat back to its base level, fills Hitpoints, Prayer and
  run energy, and clears a stun. It leaves the bank, quest progress, base levels
  and xp, appearance and the tile. It answers in one line ("Reset character: 28
  backpack, 8 worn emptied; ... 14 proc(s); left Theatre") and names any clearer
  this pack does not declare.
- **`::resetcharacter home`** also moves the player to the new-character home tile.
- **`::resetcharacter fixture <name>`** applies a fixture to the live character
  (`test/quests/fixtures`, `test/raids/fixtures`, or `$TORIRSSERVER_FIXTURES`): the bank
  emptied, every perm varp zeroed, every stat set to 1, then the fixture's tile, varps,
  stats and items, then `~newplayer_setup`. This is a fresh login's state except for
  temp-scope varps, appearance, name, POH, sailing and the other `[login]` steps,
  which stay as the live session has them.
- **`t.session.reset([fixture])`** runs the cheat and then waits until the client
  shows empty worn slots, and an empty backpack too for a plain reset. Its detail
  carries the server's line. **`t.session.held()`** returns the backpack and worn
  item symbols the client holds.
- **Test runs are unchanged.** run.py still starts every test on a fresh account
  from its fixture and never calls the reset. The Scripts tab's "Start from"
  select (Reset character / Fresh character / As it is) goes through
  `QD.session._start(options)`. Until the core.lua, torirs_plugin_drive.c and
  script_runner.lua edits are merged, `options.start` is nil and the tab starts
  every Play on a fresh account, as before.

  Not merged by the seam25 closer: the tab wiring needs a headless Play through the
  tab to prove it, and snippet 3(f) would pass the manifest's script-item path
  (`tests/quests/fixtures/fresh_lumbridge.ini`) as the fixture NAME, which
  `::resetcharacter fixture` looks up under `test/quests/fixtures/` and would refuse.
  Pass the basename (`fresh_lumbridge`) when it lands.

## One camera call: `QD.drive.camera_aim` (seam25)

- `QD.drive.camera_aim(want)` is the only caller of `api_drive.camera` (the snap) and
  of `api_drive.camera_turn_toward` / `api_drive.camera_turn_release` (the turn).
  `want = { yaw, pitch, zoom, purpose = "press" | "pose" | "photograph",
  projects = fn, snap_await = ticks, note }`; it answers `result, detail` with
  `detail.mode` one of `snap`, `turn`, `turn->snap`, `refused`.
- A TEST RUN SNAPS: the same write and the same await in the same frames as before
  the call existed, so headless ledgers are byte-identical (cooks_assistant and druid
  against build/merge17_check; the six ToB rooms stay green).
- A WATCHED CLIENT (`api_drive.status().on_demand`, read only in
  `QD.drive._camera_watched`) TURNS: once a frame the C verb holds the arrow keys
  through `CmdBus_PushKey`, the way a person's key does, the shortest way round, and
  rolls the wheel one notch at a time (only while the pointer is over the world).
  A `press` stops when the target projects; if the target already projected when the
  turn began, it turns to the pose (a new view). A `pose` finishes with an exact
  write. A `photograph` is refused (63ea41d39). A refused turn (cutscene, unlocked
  camera, no bus) or one past `QD.drive._camera_turn_ticks` (12) falls back to the
  snap, so a watched run reaches the same verdicts.
- `t.drive.camera(yaw, pitch, zoom)` is a `pose` through this call: it blocks until
  the pose is there in both modes. No test calls `api_drive` itself (the sandbox has
  no `api_drive`).
- A fast press that re-aimed by a turn counts `_quick_ticks` from the end of the turn
  (`QD.drive._quick_rebase`); a test run is unchanged.
- `api_drive.camera_turn_toward(yaw, pitch, zoom)` -> `"ok", {yaw, pitch, zoom,
  arrived, yaw_arrived, pitch_arrived, zoom_arrived}`: one poll. It refuses the pitch
  and zoom `api_drive.camera` refuses. `api_drive.camera_turn_release()` lets every
  arrow go; a driver reload (Stop, script error) also releases them
  (`PluginDrivePointer_RegisterLua`). The turn state is one static per process: two
  `t.together` branches turning at once would share it (not seen, not guarded).
- `QD.drive._camera_turn_forced = true` drives the turn path headless (the
  conformance row `drive.camera_aim` only). `QD.drive._camera_turn_stats` counts
  aims, snaps, turns, the longest turn, presses that waited, fallbacks, finish writes
  and refused photographs.
- Conformance: `drive.camera_aim` and `seam.camera_aim_photograph_refused_when_watched`
  sit at the END of the plan, behind a stage that goes back to the Man. Placed early
  in phase 3, the forced turn's ticks moved every wandering npc after it and
  `seam.attack_presses_the_watched_slot` went red on the goblins' new tiles.
- Not proved: a watched Play through the Scripts tab turning (the forced flag stood in
  for it), the flicker rate before and after, and the Screenshots toggle (off by
  default in a watched client), which needs `script_runner.lua`.

## The five room plans and their harnesses (seam30)

Every ToB room now has a decide function in its own part,
`script/plugins/quest_driver/raid_play_tob_<room>.lua`, and a harness,
`test/raids/_play_<room>.lua`. Each harness is the kept room's kit and entry, then one
`t.raid.play`, then the kept room's technique and room-complete rows copied unchanged.
Status on the closer's tree (`seed_survey.py _play_<room>`): maiden, sotetseg and xarpus
are 5 of 5 names; verzik is 4 of 5 (svd dies in P3 after a long reds phase); nylocas is
1 of 5 (the red names run out of supplies at Vasilias). The `raid.play` conformance row no
longer calls `t.raid.play` on a room, because with a decide it would play. It only asserts
that the five decides are registered.

## A plan may send its own block (seam30)

The library's SEND has no gear list. A plan that swaps a loadout sends its own
`QD.together` block from decide: equips, plus a drink or a prayer if needed. It counts the
inputs into `st.inputs[v.tick]` and the result into `st.blocks`, then sets
`st.engaged = false` so the library presses the boss again. A press on an add is the same:
`QD.player.attack(sym, 2, 1, {quick=true, slot=n})` from decide. See `_play_maiden_block`,
`_play_nylocas_wear` and `_verzik_block`.

## Protection prayers: keep one in `walk_prayers` (seam30)

`QD.prayer.set` is a toggle, and the three protections exclude each other on the server.
The library's `_play_pray` sends "on new" and then "off old". The off press lights the old
one again (ny30d: Missiles stayed lit t67-362 while the plan asked for Magic seven times).
The nylocas, sotetseg and verzik plans keep exactly one protection in
`P.walk_prayers[1]`, rewrite it each tick, and let the server put the old one out. A press
reads lit one to three ticks later, so a plan treats its own press as lit for two ticks
rather than pressing again. The library is not fixed yet: the fix is to skip the off for a
member of the group being lit.

## `death_serial` starts at 0 (seam30, open)

The loop stops on the first `npc_death` row of the boss slot after `st.death_serial`, and
that serial starts at 0. A boss that takes a slot a dead npc used ends the play on its spawn
tick (ny30h: Vasilias landed in slot 1079, where a wave nylocas had died). The nylocas plan
moves `st.death_serial` forward itself until `st.boss_slot` is known. The library fix is to
seed the serial with the newest one when `boss_slot` is first set.

## A boss that changes type (seam30)

Maiden (100/70/50/30), Nylocas Vasilias (her forms) and Verzik (her phases) change npc type.
The plan follows the new type from one `api_drive.npcs(0)` read and sets
`st.boss_symbol`. Verzik's client gives each new form a NEW row (P1 was slot 68 and P2 was
slot 78 while the server slot stayed 1079). So follow her by form id, never by the client
slot. The library's `npc_death` check on the server slot still ends the fight. Sotetseg's
maze form leaves `t.npc.state(boss_symbol)` empty, so his plan holds `st.boss_gone` at 0
while the tick log is on. The ticklog's `npc_retype` row carries the type BEFORE the
change.

## `st.teleport_until`: a room teleport is not a death (seam30)

`_play_tick` reads a jump of more than 20 tiles between two ticks as a death. It now skips
that test while `v.tick <= st.teleport_until`. A plan sets that field when it sees a room
teleport coming: Sotetseg's plan sets it from the portal animation and while in the realm.
A plan that never sets it is judged as before. Row: `seam.raid_play_teleport_until`.

## Fresh npc rows read health -1/-1 (seam30)

A freshly spawned npc's `api_drive.npcs` row reads `health_ratio -1` and
`health_scale -1` until its bar is drawn. Test "alive" as `health_ratio ~= 0`, never
`> 0`. The `> 0` test hid every Matomenos in mz30a/b.

## Quick presses answer `timeout` and still land (seam30)

`t.player.cast(..., {slot=, quick=true})` and the quick attack press answer `timeout` on
most presses: 121-130 a Nylocas run, and every Maiden barrage. The tick log shows them
landing (hit rows, apnpc input rows). Count casts, not confirmations, and judge the result
from the tick log. `covered` means another npc stands on its pixels: pass that one over for
a few ticks.

## Walks, presses and footprints (seam30)

- An attack press stops a walk on its first tick-end tile. A dodge that must arrive (Maiden's
  3-tile run out of the 5x5) gets no press until it lands.
- Players walk through npcs. A route that ends a tick inside a large boss's footprint
  triggers its footprint mechanic (Xarpus: the pebble stomp and a skipped spit, xa30d t194).
  Before walking around him, check both route shapes the server uses (diagonal first, and
  straight first).
- Room presses need the kept test's camera (`t.drive.camera(0, 512, 1100)`). Without it
  nearly every Nylocas press answered `covered` or `not_visible` (ny30c).

## Client tick vs server tick (seam30)

An npc row's `seq_tick` and `face_tick` are client ticks, and `v.tick - v.api_now` drifts
by one against the server tick (xa30a saw spit 147 as 146). For a rhythm with a published
cadence (Xarpus, 4), anchor a grid on one sighting and accept a sighting up to 2 ticks
late.

## Projectiles and floor reads (seam30)

- `QD.world.projectiles` rows can include a projectile that has already landed. Keep only
  `cycles_left > 0` before treating a destination as a hazard (Verzik).
- Each projectile's `dst_x/dst_z` is the tile a person sees it falling on. Maiden's 1578
  splats, Xarpus's 1555 acid, and Verzik's urnbombs, Athanatos and webs are all dodged
  from it.
- Sotetseg's shadow-realm path is `t.world.loc_copies('tob_sotetseg_lighttile', 40)` on
  level 3. Walking it one straight run at a time, corner to corner, left 0 of 40-55 realm
  ticks off the path in every survey run.

## A technique row keyed on the plan's own timing passes vacuously (seam30)

The kept `tob_xarpus.lua` row `technique.spit_dodge` looks up the acid by the plan's own
dodge tick (`acid[j].tick == rec.S`). It passed while 9 of 16 dodges were a tick early,
because a dodge whose S had no spit was skipped. Pair such a row with a guard that every
timed event exists in the tick log (`play.dodge_on_spit` in `_play_xarpus.lua`).

## Verzik's nylocas blast on arrival and on death (seam30)

The server's Verzik nylocas blast (`~tob_verzik_crab_blast` on `[ai_queue3]`: 63/26/8 by
band, range 3) fires when it arrives and also when it dies. Killing one beside the raider
costs up to 63, so shoot from 4 or more tiles.

## Raid runs are cheap on the virtual clock (seam30)

A Maiden room is about 10 s of wall time headless, so a five-name survey takes about a
minute. Iterate from `raid_report.py --mistakes`, not from replays. Note: `raid_report.py`
counts every Xarpus poison hit and every Sotetseg melee through Protect from Melee as a
`prayer` mistake. In both rooms that is the content's rule (protection has no effect at
Xarpus, E:201; Sotetseg's prayed melee max is 10), not a play error.

## Lighting a prayer puts out its group: never send the "off" (seam31)

A prayer press is a toggle. When the server lights prayer X it first puts out every prayer
that shares an exclusion group with X (`prayers.dbrow` `data=group` lines;
`~prayer_deactivate_conflicting`, prayer.rs2:309-318). So an "off" for such a prayer, sent in
the same block, lights it again and puts X out (seam30 ny30d: Protect from Missiles held
t67-362 while Magic was asked seven times). `QD.raid._play_pray` keeps those offs back for you
(`st.pray_skips` counts them; the summary says "prayer offs kept back N").
`t.prayer.conflicts(a, b)` answers the question, and `QD.prayer.GROUPS` is the table. The
three protections, retribution, redemption and smite share `overhead`; piety, chivalry,
rigour and augury share every combat lane; protect item and the two restores conflict with
nothing.

## The play ends on the boss's death row, not on "boss gone" (seam31)

`t.raid.play` answers `ok` only on an `npc_death` row for the boss's world slot that came
after the boss was first seen, or on the plan's optional `room_cleared` hook
(`plan.room_cleared = "<QD.raid function name>"`, called as `f(st, v) -> boolean`). A server
slot is reused, so `st.death_serial` is seeded past that slot's earlier deaths when the slot
first resolves (seam30 ny30h: Vasilias took a wave nylocas' slot and the play ended on her
spawn tick). With the tick log on, a boss the SEE step cannot find (a retype changes its
symbol) is counted as "boss gone with no death row" and the play goes on. Only a member with
no tick log still stops after 3 ticks gone, and its stop says "not proved dead". The record
carries `st.stop`.

## A boss's health bar is no death sign (seam31)

Vasilias at 4 of 360 hitpoints reads ratio 0 of 30. A plan that took ratio 0 as dead
dropped a live boss and died to her (svhplaynyloc t861-1237). Fight a row while it is there,
and end on the `npc_death` row (the library does).

## The true-answer press: QD.raid._play_press (seam31)

`QD.raid._play_press(st, v, {symbol=, slot=, op=2, spell=})` is the library's quick press.
It answers:

- `ok`: the hit showed inside the settle.
- `pressed`: the row landed. The SEE step turns it into `ok` on the tick a new hitsplat shows
  on that copy (or the copy leaves the pool), or into `unconfirmed` after
  `QD.RAID_PLAY_PRESS_CONFIRM` (10) ticks.
- `refused` with a one-line reason: the server's own sentence ("server refused the cast:
  That target is already frozen."), a pressed row that was not Attack, or "the spell was not
  armed when the menu opened".
- the verb's own `covered`, `not_visible`, `no_row` or `no_runes`, with its first clause.

A raw quick-press `timeout` from `t.player.attack` or `t.player.cast` with a one-tick settle
means "pressed, no hit inside one tick", never "missed". Measured effect lag on goblins: Wind
Strike +2..+6, bow +3. The summary clause reads
`presses [ok N, unconfirmed M] effect lag [+3xK ...] pending P reasons {...}`. The room
plans (Nylocas, Verzik, the Maiden adds) still call `t.player.attack` and `t.player.cast`
directly, so their own histograms still say `timeout`. `covered` presses are not fixed at
the pointer level.

## A Saradomin brew's overheal does not hold on this server (seam31)

The consume row reads 99 -> 115 and the raider row reads 99 again the same tick
(svaplaynyloc t532-538; CONTENT_BUGS seam31). A plan whose threat stays at or above the base
otherwise drinks every 3 ticks for nothing. The library's `_play_send` also sends the drink
before the eat, so in a combo the food lands after the brew: judge the food on hp + brew,
never on hp. The Nylocas plan's `_play_nylocas_supplies` does both. The library's
`_play_supplies`, which the other rooms use, still counts the overheal.

## The client's npc row can lag the server's walk (seam31)

For Verzik's tornado (10846), `api_drive.npcs` gives the spawn tile on every tick while the
server's `npc_tile` rows move it one tile a tick (CONTENT_BUGS seam31). Before trusting a
row's x,z for something that chases, compare it once against the tick log's `npc_tile` rows
(`raid_report.py --timeline`). A plan that cannot see the walk dead-reckons from the
content's own walk rule, and still believes the row whenever the row moves.

## A map spotanim is listed after it has played (seam31)

The yellow pool graphic 1595 stayed in `QD.world.spotanims` 51 ticks after its blast. Filter
on `cycles_left > 0` and on the hazard's sourced lifetime from its first sight. Never treat
"listed" as "on the floor".

## An auto-attack keeps rolling after the plan stops wanting it (seam31)

An engaged weapon repeats on its own. A damage-to-heal window (Verzik's reds summon;
tob_damage.rs2 rolls at the swing) needs the repeat cut by a one-tile step before the window,
not just no new press. Predict the window from what a player can count on screen (her
attacks since the last summon), not from a timer.

## Entry kit after Bloat: bandages and the Entry set (seam31)

The Bloat chest's `tob_bandages` (Entry page :151, "will always contain 10 bandages"; heal 20
plus a boost, tob_spectate.rs2) replace sharks, and the Entry page's recommended Entry
equipment (:78-91) is worn. In the Nylocas harness this cut wave damage from 227-424 to
94-238 and took the room from 1-3 of 5 to 5 of 5. Vasilias' prayed max is still Normal's 17
in every mode: only the Normal figure is sourced (Strategies :752; CONTENT_BUGS seam31).
The Entry blert stream has no player hitpoints or hitsplats, so blert cannot measure damage
taken in Entry.

## Scratch and regression runs: pass --no-publish (seam31)

`tools/raid_gate/run.py` and `tools/quest_gate/run.py` publish a passing non-underscore
run's ledger and shots into OSRS-Content's selftest directories unless `--no-publish` is
passed (`seed_survey.py` passes it). A regression run of a kept room or a quest inside a
seam pass dirties the content submodule that way. Iterating from the log: a small
summariser over `ticklog.tsv` (consume rows' hp_before and hp_after per item, hit_player by
npc type, the boss forms' npc_spawn and npc_death) found every seam31 fix without a replay.
Realised heal, the sum of max(0, hp_after - hp_before) against the item's nominal heal, is
the waste figure.

## A loadout swap and a special ride the play's send: intent.gear, intent.spec (seam32)

`QD.raid._play_send` takes `intent.gear = { item, ... }`: each item is equipped inside the
tick's one `t.together` block, after the food and before the step, so a swap rides the same
tick as the walk that leaves ("the scythe back the same tick"). `intent.spec = true` arms
the special from the minimap orb (`orbs:specbutton`), never the combat tab's bar, before the
attack press of the same tick. The send records `st.gear_swaps` and `st.spec_arms`. A plan
that sets neither sends exactly what it sent before. The orb press is a toggle: a plan that
re-arms reads `varp301_sa_attack` first and presses only when it reads 0. Prove a special
fired by the energy it spends (`varp300_sa_energy` falls by the weapon's cost, 500 for the
Dragon warhammer), not by the first splat. Row: `seam.raid_play_loadout_spec`.

## Bloat's tank block is local x 29..34 (seam32)

The LOS and collision block in Bloat's room is local x 29..34, z 29..34 of the map square
(6429..6434 x 93..98 in the instance). Every Bloat tick log has raiders on 6428,93..98 and
none inside that box. The old plan box (28..33) put a hide tile at x 6434, which nobody can
reach, and the server's route to it ran under a shadow (svcplaybloat t309-311: the walk,
the hand on the route, the stun, a second hand).

## A supplies threat counts the prayed fly and not a shadow the step leaves (seam32)

With Protect from Missiles lit, one of Bloat's flies lands at most 15 in Normal (W:673
"reduced by 25%"). A threat that counts 20 for every unhidden tick ahead reads "eat" at 120
hitpoints, and every Saradomin brew dose drains Attack and Strength by 2 + 10%
(br_potion.rs2:78-79): downs 2 to 5 dealt a fifth of down 1 (b32n3a). A shadow the tick's
own step leaves is not a hand that lands (ET 3.4), so it is not counted either. The Bloat
plan also drinks a super restore when Attack is under its base and re-sips the super
combat on a walk.

## Party kits need run energy for a 300-tick room (seam32)

Fresh characters run out of run energy by Bloat's fourth walk (svbplaysmoke t346-361), and
a raider who walks beside a running Bloat takes a fly every tick. Party kits carry
`::setlevel agility 99` (yt_4i4lv-srJkw.md 0:12:09).

## A party harness declares its party (seam32)

`party_repeat.py` reads the party size from the test file (`party = N,`), and takes no
`--party`. `seed_survey.py <id> --party 3` works on a file without one. Bloat's Normal trio
moved to `test/raids/_play_bloat.lua` (`party = 3`); `_play_smoke.lua` is the Entry solo
harness and asserts a party of one. The Maiden and Nylocas trios run from `_play_maiden.lua`
and `_play_nylocas.lua` under `--party 3`; their repeats ran on copies that add `party = 3,`
(build/seam_state/matthew-mbp-m4-raid-b1-seam32/_play_<room>_trio.lua). A red name answers
DIFFER (exit 1) even when the tick-log shas agree; compare a green name or pass --allow-red.

## api_drive.players' pid is the tick log's pid + 1 (seam32)

`api_drive.players()` counts the `me` pid from 1; the tick log counts from 0. In a party the
library's `st.my_pid` (`QD.raid._play_state`) therefore names the next raider, and the
leader counts another raider's swings. The Maiden and Nylocas plans re-read their own log
pid once, from the `player_tile` row on their own tile. The library is not fixed yet.

## A party member holds no tick log: read its own swings from the experience paid (seam32)

`QD.raid._play_see` records swings only from the leader's tick log, so every member's plan
is blind to its own swings (s32ny2: p2 and p3 "0 swings" in 933 ticks). The Nylocas plan
reads a member's swings from Hitpoints or Magic experience rising, checked every decide. On
the leader the read matched its `player_anim` swings at a lag of 0 or 1 tick. Proved on a
goblin: three Wind Strikes, each shown by the first Magic experience read at or after it.
Row: `seam.raid_play_member_swing_xp`. It belongs in `_play_see` when there is no log.
A manual cast is followed by the player's own melee five ticks later (s32xpswing: seq 422
at 13 and 21), so with a staff on, a bash on a blue is a wrong-style hit unless another
press comes inside five ticks.

## Maiden's blackstorm is an overhit settled on her launch tick (seam32)

`tob_maiden.rs2` (`~tob_maiden_blackstorm`) makes the hit lethal when it is at least the
target's hitpoints at launch (W:590 "cannot be tick-eaten"). A plan holds hitpoints above
the storm on her attack tick; a bite sent on T-1 is eaten after her scan. The client sees
her animation a tick late, so judge the launch a tick early. A tank who steps in late does
not know the current storm (it grows 3.5 per Matomenos that reached her): a person reads
the old tank's hitsplat, the plan does not yet.

## Matomenos: two lanes, the freeze lands two ticks after the cast (seam32)

Ice Barrage on a Matomenos lands two ticks after the cast (npc_spotanim 369 at cast + 2). A
walking crab nearer her than gap 4 at the cast reaches her before the ice. The north spawns
converge on her top row z=97 and the south ones on z=92, four tiles apart in x (spawns x
6436/6440/6444/6448, z 85/87 and 101/103). They meet only when the lead crab is frozen and
the followers walk into it, so a barrage on a frozen anchor catches them (W:637). In the log
a freeze is a crab's `npc_tile` rows stopping (they step every tick); a leak is a crab dying
at gap 1 or less from her footprint, its absorb blow its remaining hitpoints, healing her
twice that.

## Vasilias' colour is judged at the swing, not the landing (seam32)

Every `npc_heal` row in s32ny2 sat on a tick a raider swung that was also her `npc_retype`
tick. Keep every style's swing off the turn tick: hold the press, or step the tick before
an auto-swing falls there. Projectiles judged by their landing are not enough.

## A copy's age must survive the plan's own long blocks (seam32)

Seam30's "unseen for 2 ticks = new copy" rule reset every age whenever a swap and press
block ran 3 ticks, so in a party every copy stayed under the flicker settle age and was
never picked (s32ny6 p2: pick=none with 22-33 present). A copy is new only on a new slot,
8 unseen ticks, a size change, or a move further than it could walk.

## A weapon swap while engaged swings the old target with the new weapon (seam32)

The player keeps swinging at the old copy with the NEW weapon until the next press lands
(svcplaynyloc p2 t163-t167: whip pressed on a grey, blowpipe worn, darts on the grey for 0
and 0). In the Nylocas that is a wrong-style hit. A one-tile step with the swap ends the
engagement at a tempo cost (the plan's P.swap_stop, off: survey 3 of 5 with it).
"Your attack has no effect on this Nylocas." (tob_damage.rs2:308-310) is the sign a raider
is nulled on a copy; the plan strikes its last-swung copy off on that line.

## Seq ids and the blowpipe (seam32)

`drive.symbol` has no `seq` kind. Measure a seq id from `player_anim` rows: the toxic
blowpipe swing is 5061 (s32ny5). `all.seq`'s header order is not the id (index 5055 is
snakeboss_blowpipe_attack, id 5061). A blowpipe is loaded the way a player does it:
`t.player.use_item_on_item('dragon_dart', 'toxic_blowpipe')`, then
`('snakeboss_scale', 'toxic_blowpipe_loaded')`, then equip `toxic_blowpipe_loaded`. The
scales need a free backpack slot at `::give` time.

## Iterating without a replay (seam32)

A scratch copy of a harness that wraps the plan's decide function inside `run(t)` and
writes each leader decision as a `t.ticklog.mark` shows why a tick went the way it did
(QD is not a global when a test file loads; `t.raid` is QD.raid). Example:
build/seam_state/matthew-mbp-m4-raid-b1-seam32/scratch_bloat_trace.lua.

## Conformance rows share one tick log and one fight (closer seam32)

The conformance run's tick log carries every earlier row and the server tick restarts
inside the run, so a row reads its rows `since` its own mark's serial, never "tick >= now".
A row that fights must end the engagement the row before it left (a one-tile walk, then a
wait for a dying npc to leave). A boosted stat drops one point on the player's stat restore
tick, so read a boost on the tick the item is consumed, not a few ticks on.

## A magic hit that is not a cast goes through the funnel's magic entry (seam33)

`%varp6295_damagetype` is the weapon's combat-style row, and a powered staff has no magic
row. A hit that is MAGIC damage but is not a cast calls `[proc,player_hit_npc_prepare_magic]`
(player_hit_npc_prepare.rs2), which sets the magic style, calls `~player_hit_npc_prepare`
and restores the varp. Callers today: the powered-staff auto (trident, sanguinesti, shadow,
Ayak, Dawnbringer), the Dawnbringer, purging staff and Eye of Ayak specials. Before seam33 a
Nylocas Hagios nulled a trident for good. Voidwaker and the accursed sceptre still go
through the weapon's row (CONTENT_BUGS.md, seam33).

## A nylocas stand-in outside the room (seam33)

`::spawn tob_nylocas_fighting_magic` in Lumbridge gives an 11-hitpoint Hagios whose style
and null rule are live, because `~tob_is_nylocas` reads npc_category, not the instance. It
does not retaliate. `::kill <sym> 8` kills it through the ordinary death path. The world
slot is reused by the next spawn, so filter `t.ticklog.rows` by slot AND by serial since a
mark taken before the attack, and pass the `t.npc` row through `t.ticklog.slot` first:
client and world slots differ.

## A byte-identical check runs under the room's own name (seam33)

The account name's first 12 characters seed the server, so a renamed run (`--name`) is a
different seed. A kept room's byte-identical check is `run.py --script
test/raids/<room>.lua --no-build` (it does not publish). A seed A/B needs two names that
share their first 12 characters (s33nyseedoneA vs s33nyseedoneB).

## Chinchompas hit the 3x3, in a multi-way area only (seam33)

`[proc,player_chinchompa_splash]` (player_ranged.rs2): the primary's accuracy roll decides
every secondary, each npc gets its own damage roll, the cap is 11 targets (12 for black)
and the splash happens only in maps/multiway.csv or a map instance. ToB rooms are
instances, so a ranger's chins hit a clump there; in Lumbridge a chin hits the primary
only. Every hit_npc row of one throw shares the primary's landing tick.

Multi-way stand-in: the wilderness zone 0_50_57 (about 3200-3215, 3648-3670) is in
multiway.csv. `::goto 3203 3655 0; ::spawn tob_nylocas_fighting_ranged 3` puts three
Toxobolos at 3204..3206,3656 (`::spawn` lands at the player's tile +1+i, +1). In-instance
stand-in: `t.raid.enter('tob','nylocas',{mode='entry'})` lands at 6431,113 with the fight
not started, and a `::spawn` there is inside the instance. `t.npc.tiles` returns
(result, summary, rows): take the third value.

## A loaded toxic blowpipe is one kit line; t.inv.blowpipe reads it (seam33)

`::blowpipe <dart> <dart_count> <scale_count>` then `::wield toxic_blowpipe_loaded`. The
cheat gives the three items and loads them through the content's own use-on (darts, then
scales), as a player's Use click does, then reads the slot back. It answers FAILED (the
setup row stops the run) when the counts are not exactly met, when the backpack already
holds a toxic blowpipe, that dart or scales, or when fewer than 3 slots are free: put it
early in the kit. Once loaded it is one slot.

`t.inv.blowpipe()` -> `("ok", {where = "worn"|"inv", pipe, dart, darts, scales, line})`
reads the load through the read-only `::blowpipe` readout (item vars are server-only). One
shot spends one dart and, two times in three, one scale. A plan can count darts down to
prove the shots were the pipe's. Only toxic_blowpipe is supported.

Ranged stand-in: `::spawn giant 1; ::passive giant` (Hill Giant, 35 hitpoints) next to the
fresh_lumbridge tile; read `t.ticklog.rows({kind='hit_npc'})` filtered by the
`t.ticklog.slot` of the `t.npc.nearest` row.

## A powered staff is charged by its own op and swings on by itself (seam33)

One `t.player.attack` at a goblin with an Eye of Ayak gives seq 12397 every 3 ticks
(conformance `seam.raid_play_powered_staff_cadence`). Set it up the way a player does:
`::give <staff>_uncharged` plus its charge material, then
`t.player.inv_op('<staff>_uncharged', 3)` (the Charge op). Demon tears for the Ayak (one a
charge), blood runes for the Sanguinesti staff (three a charge); the material leaves the
backpack. The staff's max hit is floor(Magic / 3) - 6 for the Ayak
(~powered_staff_maxhit): at Magic 99 a 5-hitpoint goblin dies on the second swing, so a
cadence check drops Magic after the wield (the closer set 21: max hit 1).

## Seq ids for a loadout (seam33)

Seq ids come from all.seq.compack's `id=name` lines (12397 human_eye_of_ayak_normal, 1167
human_castwave_staff, 5061 snakeboss_blowpipe_attack). Grep the exact name with an
anchored `=name$`.

## A weapon swap does not end an attack; time it (seam33)

A raider whose bow is on the boss and who swaps to a staff or wand walks to melee range and
swings it there (the Maiden freezer stood next to her for 27 ticks); the Maiden plan sends
a one-tile walk in the swap's tick. On a nylocas copy that still stands, the cheap fix is
timing: send the swap and its press only when the old weapon's next swing (own last swing +
worn speed) is two or more ticks off, else wait a tick. Wrong-style wave swings fell from 11
to 3 on one seed with no tempo cost.

## Piety and Rigour light only when the plan lists them (seam33)

`_play_pray` manages only the prayers in the plan's walk_prayers or down_prayers, so
`intent.want.piety` on a plan that does not list it lights nothing. The raider row's
prayer mask shows it: bit 12 Protect from Magic, bit 24 Rigour.

## A ranged or magic press answers "timeout" when it landed (seam33)

`QD.player.attack`'s timeout means pressed, no hit inside the one-tick settle; a dart or an
arrow cannot land in one tick. `QD.raid._play_press` gives a true answer (pressed, then ok
when the hitsplat shows).

## The supplies horizon and an overhit settled at the launch (seam33)

Between swings `_play_supplies` looks only 2 ticks ahead. For Maiden's blackstorm, settled
at the LAUNCH, a raider at low hitpoints with only potions needs two doses 3 ticks apart,
so the Maiden plan's threat function counts a launch up to 6 ticks out (reach_h).

## A seed survey overwrites its runs' directories (seam33)

A survey of one id writes build/quest_gate/<sv?name>/ for every mode, so the Entry solo
survey after a party survey destroys the party logs: read or copy the party runs first.
`build/seed_survey/<id>/results.tsv` is also rewritten per survey.

## Sotetseg trio: the room picks the runner, the others read the glow (seam33)

`QD.raid._play_sotetseg_trio` runs when st.party > 1. The room picks the maze runner (S
tob_sote_send_party), so whoever lands in the realm runs the Entry path without the row-3
wait (W:803), and the others follow the glow (`_play_sotetseg_follow`). The arena's glow is
ONE tile, the runner's current tile (S tob_sote_mirror): a follower remembers every glow in
order and joins them with straight runs, which works because the runner walks corner to
corner. A path that fails the maze-shape check (even row one tile, odd row a run) is never
walked. The maze ends on a 4-tick check that finds BOTH grids empty (a raider off the south
edge counts as empty), so the runner holds the last tile 6 ticks (party_end_hold).

A homing projectile's dst (`world.projectiles`) is the target's tile as last drawn, one
tick behind a walking raider: "aimed at me" is dst within one tile, safe with seats 3+
apart. Ball flight: 5 + 36 + 8 per tile cycles for a ricochet, 20 + 36 + 8 per tile from
his centre, 30 cycles a tick; adjacent raiders get a 1-tick ricochet. The death-ball share
splats land over two ticks and can be 0 or 1, so judge the stack by tiles at the landing.
The conformance row `seam.raid_play_sotetseg_trio_seats` pins the seat geometry.

## A party harness that keeps its solo id needs a party = 3 copy to repeat (seam33)

party_repeat.py has no --party option. A harness that reads its party from QD_PARTY
(_play_sotetseg, _play_nylocas, _play_maiden) is repeated through a copy with `party = 3,`
after its id line: `party_repeat.py --script <copy> --name <run> --runs 3`.

## The Nylocas trio's pillar bar is decided by copies never killed (seam33)

Count kills against pops from npc_spawn/npc_death per slot before tuning a target score:
the 34-44 copies that chew their whole 51 ticks and pop are about half the support damage.
## Xarpus phase 2 timing with a party: spit S+3, chains S+4/S+5 (seam34x)

Measured in our server (xn34c): the spit on tick S aims at the target's tile as of the end
of S-1 and lands on S+3 (projectile end cycle 102-112). The landing throws its chains (one
for the phase's first spit, two after) at the other raiders' tiles as of the end of S+2, and
they land 2-3 ticks later (end cycle 65-95). A landing is judged on S+2 (the spit) and on
S+4/S+5 (the chains). A one-tick step back cannot keep every chain off the melee ring under
this timing; the trio plan steps back for two ticks (the ends of S+2 and S+3).

## A party that stacks: one rule, one view, ties by tile (seam34x)

Three raiders that run the same decide on the same view, with every tie broken by the tile
key and nothing by role, stay on one tile without talking (Xarpus phase 2: 109-123 ticks
together, 0-10 apart). Every landing of a spit cycle then falls on the stack's one step-back
tile. `trio.stacked` in `test/raids/_play_xarpus.lua` counts the ticks they were apart.

## A covered press: re-press on the next tick (seam34x)

About once a raider a fight, a press onto the boss is "covered" by the stack's own models
and does not land, leaving that raider on the step-back tile. Re-pressing on the next tick
puts it back on the stack's tile, because the server's run in takes the same path.

## Never eat on a press tick in a tick-exact pattern (seam34x)

A press with a bite in the same tick took two ticks (xn34b), and the step-back pattern
slipped by one for the rest of the phase.

## Run energy for a step-back pattern (seam34x)

With Agility 99 and no stamina potion, run energy held for a four-tile-per-four-tick
pattern over about 140 ticks.

## party_repeat.py has no --party (seam34x)

`party_repeat.py _play_xarpus --runs 3` runs the harness solo and then fails on the missing
party.tsv. The trio's repeat proof ran on a copy of the harness with `party = 3,` added
(`build/xn34_repeat/_play_xarpus_trio.lua`, `--script ... --name _play_xarpus --runs 3`).
## Verzik Normal trio: the Dawnbringer shared by drop and take (seam34v)

One Dawnbringer per raid (tob_xarpus.rs2 `[proc,tob_dawnbringer_take]`). The Xarpus room is
not played before `_play_verzik`, so the harness hands p1 the one copy with `::give`; p2
and p3 keep a backpack slot free. The holder arms the special from the orb (`intent.spec`
with `intent.attack`) and sees it as varp300 falling by the 350 it costs (special_attack.obj
`sa_energy` 350): two specials per raider at 1000 energy. Spent: the scythe back on
(`intent.gear`), then `QD.player.drop` on the cover tile (W:887 "pillar drop"). The next
raider takes it with `QD.player.click_obj(..., 3)` the (role-1)th time the obj appears in
`api_drive.objs(0)`, so the orb order (W:875) is seen, never told. Proved: 6 specials on
five names; the last holder keeps it (the shield's break destroys it).

## Verzik P1 cover is the content's box, and a pillar's bar fades (seam34v)

The cover is `~tob_verzik_behind_pillar`'s box (tob_verzik.rs2), not "behind" by eye. A
pillar's health bar shows only for a while after a hit, so the plan keeps the LOWEST bar
seen per npc slot: a faded bar read as a whole pillar split the trio over both near pillars
and both fell on them (t119). A pillar with 60 or less left is hidden behind only from 3+
tiles from its centre (`^tob_verzik_pillar_collapse_range` 2). A shadow more than 7 from
her is no cover: past the near row the bolts are tanked under Protect from Magic (W:887).

## A route round a pillar is a tick longer than Chebyshev / 2 (seam34v)

Hide from L-2-travel, not L-1-travel, where L is the bolt's launch tick and travel is
ceil(Chebyshev / 2): the leader at 6430,98 needed 4 ticks to reach 6426,93, not 3.

## Yellow pools are listed three times per tile (seam34v)

The client lists each yellow pool's graphic (1595) three times on one tile. Deduplicate
before assigning one pool per role (p(r) takes the r-th in x, then z order), and lock the
pick for the charge: a pick re-read every tick flipped as the list changed. A bow repeat
swing paths a raider off its pool: click its own tile to end the repeat, and press nothing
while she charges (W:968, she is invulnerable).

## A raider knocked off the Verzik floor never swings (seam34v)

In P3 a raider can stand off the floor (6421,84). There it pressed Attack every tick for 20+
ticks with no swing. Step back onto the floor first.

## Enrage with three tornadoes: power through (seam34v)

Running from all three (or from one's own, dead-reckoned) cost more than it saved: 16-22
touches a room. The trio keeps shooting at the 45 band (W:983) and runs only while the
green ball is in the air or above the band + 15.

## party_repeat.py reads only a declared party (closer seam34v)

`_play_verzik` must stay the Entry solo, so it declares no `party`; a `--party 3` run of it
plays Normal. `party_repeat.py` has no `--party` option, so its gate runs a scratch copy
with `party = 3,` added, through `--script` (build/seam34v_scratch/_play_verzik_party3c.lua).

## Verzik P2 reds: count casts between spawns, not attacks to the summon (seam36)

A summon is never one of the seven attacks. Count her casts between the npc_spawn rows of
10845/8385/10862: 7 casts, the last cast to the next spawn 8 ticks (one empty slot), spawn
to spawn 44 (Blert 84 of 84 cycles). The first set comes on the attack slot after she
crosses 35%, 4 ticks after the attack before it (62 of 62). The kept tob_verzik row
`spec.verzik.reds_attacks_between` counts casts plus the summon and now reads 8: it asserted
the old one-short cycle and is a note for the re-author.

## The account name seeds the fight: compare under the same name (seam36)

`run.py <test> --name X --no-publish` plays a different fight from the test's own name
(s36tobvz died in P3 where tob_verzik under its own name is green). Compare before and
after under the SAME name. `run.py <test> --no-publish` without `--name` still writes
build/quest_gate/<test>/, so copy the old ledger first.

## A decisive prayer-read-tick trial is a reverse trial above the prayed ceiling (seam36)

For a damage-on-landing attack, a trial decides only when the prayer is on at the send,
off at landing, and the hit exceeds the prayed ceiling. A forward trial, or any hit at or
under the ceiling, proves nothing (P 1/2 a trial for a halving prayer).

## A Saradomin brew's overheal holds through hits (seam36)

Engine fix in `ToriRSServer_CombatSyncHitpoints`: a raider at 115 hit for 15 reads 100, not
99 (it used to clamp to the base on every hit, a 0 included). Plans that cap a brew at the
base (`_play_nylocas_supplies`) can count the overheal again. The overheal does not decay
yet: stat_restore.rs2 skips hitpoints (open). On a player's hit_player row, max_hitpoints in
the DAMAGE mask equals the current hitpoints while overhealed (a full bar); the raider
row's `hpmax` label is still the base level.

## The br_ Saradomin brew raises Defence (seam36)

`br_` is the cache's Last Man Standing supply family (br_bloody_key, br_token); the ToB chest
and the ToA bundles hand out its potions. Its Saradomin brew now raises Defence to base + 2
+ 20% (99 -> 120), like the tradeable brew; it used to drain it.

## A prayer technique row reads the lit prayer, never prayer points (seam36)

Read `t.prayer.read` or the raider row's prayers bit. tob_bloat.lua :185 counts a walking
tick with prayer POINTS as shielded, so under other names it counts flies that landed before
the first press (svdbloat t87/t89 7s; from the first press on the flies are 3..6, the wiki's
25% off the Entry 4..8).

## Salve amulet against the undead (seam36)

gear/salve_amulet.rs2 `~salve_or_black_mask_scale_target(value, style)` is the one
target-bound call at both funnels, the attack roll (combat_stats.rs2) and
`~player_hit_npc_prepare`: the salve when it applies to that style against an
`npc_param(undead)` npc, else the black mask or slayer helmet, never both. Pestilent Bloat is
undead. POH dummies keep their own path. `::salvemax` (debugproc) prints the content's own
melee/ranged max hit; read it with `t.chat._choose_new_line(0)`. A POH dummy cannot name M
(its hit is capped at its 10 hitpoints), and `varp6287_com_maxhit` through `t.var.server`
answers 0.

## Barrage and chinchompa: multi-way areas and instances only, fuse accuracy (seam36)

A barrage or burst reaches the 3x3 only where `map_multiway` or `map_instance_find` says so,
the same test as the chinchompa splash; a raid room is an instance, so multi. A chinchompa's
attack roll is scaled by the fuse table (short/medium/long = accurate/rapid/longrange) at
the Chebyshev distance to the npc's south-west tile. Both compiled and gated by the suite,
not driven by a scratch.

## Scratch scripts run without pcall/api_drive globals (seam36)

A scratch script passed with `--script` sees only the `t.*` verbs.

## Vasilias' spawn delay counts the split smalls too (closer seam36)

The kept tob_nylocas row `spec.nylocas.vasilias_spawn_delay` takes the last free of a WAVE
nylocas. When a big dies late its split smalls outlive the wave (tick 613 split, 665 free),
and she spawns 17 ticks after them (682), inside the spec's 16-19; the row then reads 41.
Under its own name the room now plays that roll (the brew overheal holds, seam36), so the
row is red on a measurement, not on the content: a note for the re-author.

## A play's tick-log reads start at the play, not at serial 0 (seam35e)

A room test's tick log starts at its room; the whole-raid relay's starts in the lobby.
Anything the play library reads "since serial 0" must start at the play.
`QD.raid._play_state` now seeds `st.anim_serial` at the newest `player_anim` row, so a
room's swing count no longer includes the earlier rooms' swings. Before this, Verzik's
plan read 60 scythe swings from Bloat, Sotetseg and Xarpus as P1 punches and switched to
the bow 15 ticks in. Row: `seam.raid_play_swings_start_at_play`.

## A plan with absolute tiles only works in the room test's instance (seam35e)

When the Theatre is entered by its door, each room is built in the next free instance.
On the relay's first run Maiden stood at 6426,156, not at the room test's 6426,92. Base
every tile on the boss's tile or on `st.origin`, for a solo plan as well as a party plan.
`raid_play_tob_maiden.lua` now does this for solo; the offset is 0 in the room test.

## Turn prayers off after each kill (seam35e)

Plans turn prayers on and never turn them off. A relay turns off every active prayer
after each kill (`t.prayer.read`, then `t.prayer.set` off for each one). Otherwise Piety
drains prayer through the corridor and into the next room.

## Verzik P1: eat for the bolt's launch tick (seam35e)

Verzik's P1 bolt damage and its lethal verdict are settled on the launch tick
(`tob_verzik.rs2` ~tob_verzik_p1_attack), not on the landing. Eat for the launch tick.
`raid_play_tob_verzik.lua` `bolt_lands` now counts verdicts.

## Leaving a ToB room (seam35e)

The cleared barrier is a gate: press it once, then check the tile, because a second press
steps back. A support or the chamber can cover the passage clickbox. Walking to within
one tile of the passage takes the passage (`tob_raid.rs2` ~tob_exit_walked), so a walk
into it is the fallback.

## Bandages are play-library food (seam35e)

`QD.RAID_PLAY_FOOD` lists `tob_bandages` last (heal 20, Entry Mode page :151). A pack that
still holds fish eats the fish first. Content bandages restore no prayer (CONTENT_BUGS
"From seam13", open), so in a whole raid the prayer potions are the limit. Row:
`seam.raid_play_supplies_bandages`.

## Rune symbols in the backpack have no underscore (seam35e)

The backpack symbols are `bloodrune`, `chaosrune`, `waterrune` and `deathrune`.
`::give water_rune` is accepted, but `t.player.drop` and `t.inv.count` need the backpack
symbol.
