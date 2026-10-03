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
- Maiden (and her crabs and blood spawns) and Verzik stay the Normal ids in every mode;
  their triggers exist only for the Normal ids.
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
