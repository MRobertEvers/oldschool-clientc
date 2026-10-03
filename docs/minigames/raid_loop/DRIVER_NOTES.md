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
- The maze waits for its runner: an eat on proc+2 holds the teleport (`p_delay`) to
  proc+5 and the maze is not ended under it. Do not eat on proc+1..proc+2 if you assert
  `maze_teleport_delay` 3 (the late landing is an open row).
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
- Re-attack after every eat, dodge or step (README lessons); an eat drops the engagement.

## Rows inside a timed loop

- A `t.check` row costs server ticks (its screenshot settles). Inside a per-tick dodge or
  attack loop, collect the readings in a Lua table and emit the `t.check` rows after the
  loop ends.
- A player death aborts the run with `player.died`; every row not yet written is lost.
  Break the loop on low hitpoints (eat first, then leave the loop) and write the rows.
- `t.player.alive()` returns `("ok", detail)` or `("refused", detail)`, both strings: it
  is never false, so `if t.player.alive() then` is always taken. Compare to `"ok"`.

## Specials and stats the driver cannot read

- A special attack is pressed as the player would: `t.ui.tab('combat')` then
  `t.ui.widget('combat_interface:special_attack')`, then the attack press (Dragon
  warhammer, seen landing its floor). On Entry Bloat two warhammer specials and Curse
  drained nothing (Defence read 80 of 80; mage defence 600), so `bloat.stomp_defence`
  could not be driven through a drain there: report it open, not as a pass.
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
- Do not eat while a death ball is in flight if you classify splats by launch+16. The
  impact is a player queue, the eat's `p_delay` holds it, and the splat lands late and
  reads as an ordinary ball (2 of 5 moved in s4_sote_ball_before).
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
- An eat's `p_delay` holds every queued hit on the player, not only the Sotetseg death
  ball: a Sotetseg melee swung on tick 531 landed on 535 instead of 532. A row that
  excuses a late splat by an eat must look for the eat in the ticks AFTER the swing
  (swing+1 .. splat-1), not only before it, and must `t.check` every instance; an
  unexplained late splat is a FAIL (or a content_bug), never a PASS.
- Seam5 found the cause: our eat calls `p_delay`, LostCity's does not (seam5 section,
  "An eat holds every queued npc hit"). It is still open after seam6: the port was made
  and proved, but it moved two quest tests from green to RED and was not landed (seam6
  section, "The eat-delay port did not land"). Until it lands, count eaten-through hits
  separately.

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

## An eat holds every queued npc hit

- general/scripts/food.rs2 eats with `p_delay(^eat_delay)` (2 ticks). A delayed player
  runs no queue of any kind (torirs_server_scripts.c:938 and :1540, the same canAccess
  gate as LostCity).
- So a hit due at +1 lands at +3 when the eat lands on the swing tick, and at +4 when the
  eat lands the tick after. This covers Sotetseg's melee and balls, every raid projectile
  impact, and an ordinary dark wizard's spell.
- An ordinary npc's melee (a goblin) is not held, because it lands in the npc's own turn.
- LostCity's eat (consume.rs2:101-110) never calls `p_delay`: it sets an eat-delay clock
  and adds to the action delay. The fix is a port of that into food.rs2 (and the other
  consumption scripts that `p_delay`). Seam6 made and proved that port but did not land
  it (seam6 section); it is still open.
- Until it lands, count eaten-through hits separately in any hit-delay row.

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
made and proved but not landed, because it moved two committed quest tests from green to
RED. The fixers' reports are under `build/seam_state/matthew-mbp-m4-raid-b1-seam6/`.

## The eat-delay port did not land: an eat still holds every queued hit

- An eat still parks the player for 2 ticks, and a potion for 1, so a scripted npc hit
  queued on the player still lands late when the player eats on or after the swing tick.
  The seam5 note "An eat holds every queued npc hit" still holds. Count eaten-through hits
  separately in every hit-delay row.
- The port was made and proved: LostCity's consume.rs2:96-110 clock shape in a new
  consume_shared.rs2, no `p_delay` and no `p_stopaction` in any consume script, and an
  eat adding 3 to a running weapon delay (wiki Food/Fast foods). It is saved as a patch
  with its two seam rows under `build/seam_state/matthew-mbp-m4-raid-b1-seam6/close/`.
- The closer's full suite moved `troll` and `regicide` from green to RED with the port in,
  and both were green again without it. Both deaths come from food tuned to the old
  mechanics. The port has to land together with a quest-loop retune of those two tests.
  See CONTENT_BUGS.md, "From seam6".

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
