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
  every 3.
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
