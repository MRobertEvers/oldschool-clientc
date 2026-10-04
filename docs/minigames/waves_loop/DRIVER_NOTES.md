# Driver notes -- the waves loop

What a wave-test author sees when calling the quest driver's wave verbs: what each
verb returns, what it does not do, and what its authors tripped on. The verbs came
from the raid loop's driver seam (raid commit `94f55b306`, copied verbatim or merged
by waves seam pass 1, `matthew-mbp-m4-waves-b1-seam1`; every copied file is a row in
`FORKED_FROM.md`) and were proved here on ordinary Lumbridge npcs (scratch
`build/quest_gate/dp_scratch3`, 31/31 PASS) and by the driver's own conformance file
(`test/quests/_conformance.lua`, 158 verbs and 102 seam rows, all PASS). Seam pass 2
(`matthew-mbp-m4-waves-b1-seam2`) added t.wave.*, the tick-exact prayer verbs,
t.world.los, t.npc.pack, t.player.drink and t.inv.doses (171 verbs). Seam pass 3
(`matthew-mbp-m4-waves-b1-seam3`) added no verb; it changed what the fights underneath
them do (Inferno waves spawn once, the combat logout delay, a cast is a magic hit) and
added one seam row (103). Its eat-delay port and its prayer regeneration and drain fix
moved green quest tests and are held as patches (see "Held as patches" below). The
contract for every other verb is `docs/QUEST_AUTHORING.md`. Add a heading here for
each new verb or fact a later seam pass lands.

## Running a measurement scratch (no build, nothing published)

Put the Lua file outside `test/waves/` (for example
`build/seam_state/<pass>/x.lua`) and run it in the foreground:

    python3 tools/quest_gate/run.py --script <file> --name <unique label> --no-build --no-publish > build/logs/<label>.log 2>&1

Artefacts land in `build/quest_gate/<label>/` (`ledger.tsv`, `shots/`,
`ticklog.tsv`). A run longer than the 10-minute shell cap is started with
`--detach` and waited on with `run.py --wait <label>` (exit 3 means still
running). After any OSRS-Content edit run `make -C src torirsserver-scripts`
first: the embedded server refuses a stale pack. `run.py` refuses a second
concurrent run of one name, so every scratch gets its own `--name`.
`tools/waves_gate/README.md` has the same recipe.

## Wave tests and their wrappers

Wave tests live in `test/waves/<game>_<unit>.lua` (`inferno_*`, `colosseum_*`,
the full run `<game>_full`) and run through `tools/waves_gate/run.py` and
`tools/waves_gate/gate.py`, which take the quest gate's arguments and set
`TORIRS_QUEST_TESTS_DIR` and `TORIRS_QUEST_PUBLISH_DIR` (unset, the quest tools
behave exactly as before). `gate.py` also runs `tools/waves_gate/waves_coverage.py`
on every id it was given; FULL is required. The wrappers exit 2 when a wave id is
also a quest id, or when it does not start with `inferno_` / `colosseum_`. A PASS
publishes to `selftest/minigames/<game>/<unit>/play/`. Fixtures come from
`test/waves/fixtures/`.

## t.tick -- the server's clock

`t.tick()` returns `('ok', srv->tick)`: the embedded server's tick, the clock of
every tick-log row. `api_drive.tick()`, which `t.ticks` and every deadline use, is
the client cycle divided by 30, and `t.npc.await_anim`'s tick is on that client
clock: do not mix the two in one subtraction. It answers `unsupported` on a
socket-server run. It takes no target, so record it with `t.check`, never `t.exec`.

## t.ticklog.start / mark -- the per-tick server log

`t.ticklog.start()` returns ok and `ticklog on at tick T (now N, serial S) -> <run
dir>/ticklog.tsv`. It is idempotent and off until a test starts it. A relog or a
world reset turns it off: start it again after `t.session.login` / `relog`. The
file opens with `ticklog-v1` and the columns `serial tick kind a b c d e f label`.

`t.ticklog.mark(label)` returns ok and `mark '<label>' at tick T (serial S)`. Tabs
and newlines become spaces; a label over 47 characters is cut.

## Tick-log row kinds

`npc_anim` (after the priority gate: what the client is sent, the same thing a
recorder's NPC_ATTACK sees), `npc_spotanim`, `projectile`, `map_spotanim`,
`hit_player` (the splat as shown: after `::god`, absorption and the clamp),
`hit_npc`, `npc_spawn`, `npc_death` (the killing blow's tick), `npc_free`,
`npc_retype`, `npc_face`, `loc_set`, `obj_add`, `player_tile` (every tick, written
after `phase_players`, so it is the tile the next tick's npcs scan: the T-1 rule of
`ENCOUNTER_TIMING.md` section 1), `npc_tile` (on change, within 32 tiles of a
player), `mark`, `start`.

## t.ticklog.rows -- always filter by kind

`t.ticklog.rows(opts)` returns `('ok', list)`. Always pass `kind`: a Lumbridge run
logs about 10,000 `npc_tile` rows in 200 ticks, and iterating them unfiltered in Lua
exhausts the instruction budget. Filters: `since`, `kind` (a name or a list),
`slot`, `npc` (a `t.npc` row), `pid`, `type`, `seq`, `spotanim`, `where(fn)`. An
unknown kind is refused with `no row kind`. Field names are in `QD.ticklog.FIELDS`:
`hit_player {pid, npc_slot, damage, hitsplat, dealer_pid, npc_type}`,
`projectile {src, dst, target, spotanim, start_cycle, end_cycle}`; packed
coordinates are also given unpacked as `x/z/level`, `src_x..`, `dst_x..`.

## t.ticklog.slot -- the log keys npcs by the server slot

`t.ticklog.slot(npc_row)` returns `('ok', world_slot)`. The client slot and the
server slot differ (client 114 was world 1079 in dp_scratch3). Translate before the
npc dies, because it answers `not_found` after the despawn, or pass `{npc=row}` to
`rows` / `gaps`.

## t.ticklog.gaps -- cadences

`t.ticklog.gaps(slot_or_row, kind, opts)` returns ok, a text, the gaps and the
ticks. Use `opts.seq` to keep one attack sequence. Measured on a
`goblin_unarmed_melee_1` (attack seq 6184): `4, 4, ..., 4 (16 gap(s) over 17
row(s), ticks 17..81)`. Each melee `hit_player` row is on the same tick as its
`npc_anim`. A melee npc that starts diagonal adds a step to the first gap: grade a
cadence on the gaps after the first.

## t.prayer.set / read / points

`t.prayer.set(name, on)` returns ok, refused, no_row, not_found, not_visible or
timeout. It presses `prayerbook:prayerN` (op 1, the client's own button) and
settles on the server's varbit, readable +0 drive ticks after. Measured:
protectfrommelee is `prayerbook:prayer15` (varb4118), protectfrommagic is
`prayer13` (varb4116). It never presses twice: a prayer already in the asked state
answers ok with `no press made`. A server refusal (level, points, "You can't use
protection prayers") answers refused with the line. Names are the content's
spelling (`QD.prayer.TABLE`, 29 entries); a leading `prayer_` is stripped. It takes
no target: `t.check`, not `t.exec`. The curses book is not handled.

`t.prayer.read()` returns ok, a detail and a set of 29 booleans by name. The
overhead icon is NOT read; the detail names the icon the varbits imply.
`t.prayer.points()` returns ok, a detail and `{level, base_level, experience}`.

A prayer pressed between server ticks T-1 and T is in force for tick T's npc phase
(the T-1 rule). Measured drain: Protect from Melee at +0 prayer bonus cost 15 points
over 77 ticks (about one per five ticks).

## t.npc.state / state_text

`t.npc.state(selector[, opts])` returns `('ok', row)`, not_found, no_row or
unsupported. The row adds `anim_id` / `anim_frame` (what is drawn), `spotanim_id`,
`seq_id` / `seq_tick` (the newest SEQUENCE op the server sent and the tick it
arrived), `spotanim_sent_id` / `spotanim_tick`, `facing` (an npc slot,
32768 + pid for a player, or -1), `face_x` / `face_z` / `face_tick`, and `size`.
Use `seq_tick`, never "did anim_id change": a re-sent sequence does not restart.
`t.npc.state_text(row)` is the one-line reading for a detail.

## t.npc.await_anim / await_face

`t.npc.await_anim(selector, seq_or_nil, ticks[, opts])` returns
`('ok', detail, tick, seq)` or timeout. It waits for the NEXT `npc_seq` drive event
on that slot, repeats included, with the tick on the client clock. Measured on a
goblin: 16, 20, 24, 28.

`t.npc.await_face(selector, since_row, ticks)` returns ok
`turned to x,z on tick T (edge)`.

## t.player.step_tick

`t.player.step_tick(x, z)` returns ok `step x,z issued at tick T, resolved at tick
T+1 (+1)`. It issues one move and never re-issues it; the resolve tick is read from
the `player_tile` row, and the verb starts the tick log if it is off. A tile that is
not adjacent is refused (`step_tick takes an adjacent tile; use walk_to`). A blocked
tile times out: there is no collision pre-check.

## t.world.spotanims / projectiles / hazard_at

`t.world.spotanims(radius)` returns `('ok', rows)` of `{spotanim_id, x, z, level,
active, cycles_left, element_id}`. `t.world.projectiles(radius)` returns
`('ok', rows)` of `{spotanim_id, src_x, src_z, dst_x, dst_z, level, target,
target_npc_slot, launched, cycles_left, element_id}`, ranked by destination.
`cycles_left` is in CLIENT cycles, 30 a tick. `t.world.hazard_at(x, z[, level])`
returns `('ok', {locs, objs, spotanims, projectiles, count, text})`; an empty tile
is count 0, not an error.

A map graphic lives only as long as its sequence (telegrab 144: 18 cycles), so poll
it inside a `t.await` started right after the verb that triggers it. Ids are
numbers: 91 wind strike in flight, 92 its impact, 85 splash, 144 telegrab impact.

## The fast attack press

`t.player.attack(npc, op, ticks)` with `ticks <= 2`, or `opts.quick`, and the same
for `t.player.cast`. One aim and one press; on `covered`, exactly one re-aim and one
more press. The detail contains `fast path: press 1 at x,y ...: ok; N tick(s)
spent`. `timeout` means pressed with no hit inside the deadline, and the fight is
still engaged. `covered` answers at once and names the copies the menu offered.
`t.npc.await_dead_engaged` re-presses a fast fight the same way
(`N re-engagement(s) (fast path re-presses)`). The default `ticks` keeps the quest
press, unchanged.

## A cast the server refuses

A cast refused by the server reads `the SERVER refused the cast: 'I can't reach
that!'`. Seen diagonally across the Lumbridge goblin-field fence (3233,3238 at a
goblin on 3234,3239); unsettled whether that is right (`CONTENT_BUGS.md` ENG-3).
Pick a clear tile for a proof.

## The conformance harness runs as qdconform

Run through `run.py --script`, the conformance harness must use `--name qdconform`:
its `session.relog` logs in as `conformance.py`'s USER `qdconform`, and under any
other name every row after the relog runs on a fresh tutorial account.
`make -C src test-quest-conformance` does this for you.

## A drain read near the stat_restore timer

`[timer,stat_restore]` (`player/scripts/stat_restore.rs2:34`) moves a drained or
boosted stat one level every 100 ticks counted from login. A row that reads a drain
must account for that window (`CONTENT_BUGS.md` ENG-2). The timer also restores
drained prayer (ENG-10, the fix held as a patch).

## t.wave.state / enter / await_wave / await_clear

`t.wave.state([game])` returns ok, a one-line detail and a table read from the
server's own Inferno varps (they sit above the cache's varp range, so the client has
no copy): `active, wave, alive, practice, paused, logout_requested, saved_wave`,
`pillars.w/s/e = {hp, dead}`, `pool` (wave-credit npcs in the client's pool), `tick`
(server) and `tile`. Nothing is cached.

`t.wave.enter(game, wave[, {restart=true}])` sends `::inferno <wave>` (the content's
debugproc, a bring-along) and settles when the run is active on that wave, alive > 0
and the pool has caught up. The run it starts is a PRACTICE run: one wave, then the
content leaves (`inferno.rs2:413`). It refuses an active or a paused run unless
`opts.restart`; `colosseum` answers unsupported (no content yet). Measured: enter 1 on
server tick 5, wave begun on 13 (+8, `^inferno_wave_delay`), alive 4 = pool 4, pillars
255 (ws2_wave_a2). Wave 69 settles on active + wave only (not proved).

`t.wave.await_wave(n, ticks)` and `t.wave.await_clear(ticks)` answer ok with the
server tick it happened on, or timeout with the last state. `await_clear` is ok when
alive reaches 0 after the wave began, the wave number moves, or the run ends; it
refuses when no run is active. Deadlines are drive ticks; ticks in details are
server ticks. `await_clear` keys on the wave it finds when called: called after the
last kill, when the wave var has already moved, it waits on the NEXT wave. Read a
clear from `t.wave.state()` and the `npc_death` / `npc_free` rows instead.

## t.wave.pause / resume

`t.wave.pause({via="logout"|"exit"})` presses ONCE (a second logout press is the real
game's wave reset) and answers what the server did: ok (pause requested, or paused),
refused (no run, or the run ended: a practice run leaves on the Cave exit),
unsupported (the logout press ended the session: the client is on the title screen,
call `t.session.login()` next) or timeout. On this content the logout button logs out
and clears the run (ENG-8), so only `via="exit"` arms the content's pause.

`t.wave.resume()` after a login presses the Cave entrance op 1 and the row
"Resume the Inferno (wave N).", and is ok once the run is active on the saved wave
and begun (+16 ticks, `^inferno_resume_delay`). Today the entrance is unreachable
from the exit pad (ENG-6, ENG-7): the seam's proof staged a paused run with
`::setvar` and a labelled `::goto 2495,5123` in its scratch, not in the verb. A relog
reboots the embedded server, so the server tick restarts at 0, and the login chat
line arrives a tick after `relog` returns.

`t.wave.pause` settles on the SERVER's run varp; the client may still stand in the
arena on that frame (its own tile still reads 6430,81 and its pool still holds the
wave). Before reading `state().pool` after a leave, await `state().tile.x < 6000`
(at most 3 ticks) or `t.ticks(1)`. That mixed-clock read was all of ENG-19: the
server releases every arena npc on the leave tick and the client drops them on the
next one (seam.wave_pool_after_leave). `via="logout"` in combat is now refused for
16 ticks after the last hit (see "The combat logout delay"), and the client's own 5 s
fallback then ends the session anyway.

## t.prayer.set decides "already so?" from the server

Since seam pass 2, `t.prayer.set` reads the SERVER's varbit (`varbit_content`) before
it presses, not the client's record, which can trail the server by a few frames after
a press; the settle after the press still waits for the client's record. A test that
asserts with `t.var.server` on a prayer varbit right after a tick-exact verb waits
`t.ticks(2)` first.

## t.prayer.set_on_tick / switch / flick -- presses on a named server tick

All three return (result, detail, info) and count in SERVER ticks (`t.tick()`).
A press "on tick T" is issued while `t.tick()` reads T and is in force from tick
T+1's npc phase. So for an attack rolled on tick A: `set_on_tick(name, true, A-1)`
or `flick(name, A)`. `info` carries `issued`, `seen` (the tick the server's varbit
was first seen changed) and `in_force`, so a technique row can assert "the prayer
was up on the tick the hit was rolled".

`set_on_tick(name, on, tick)` never presses late: a passed tick or a late wake is
timeout with no press. A prayer already in the asked state is ok "no press made"
with `info.issued` nil. `switch(list, {tick=})` presses every entry (`"name"` = on,
or `{name, on}`) in one Lua resume, so all land in one tick; with two protections
the later press wins (`~prayer_deactivate_conflicting`), reported in
`info.final` / `info.displaced`. `flick(name, A)` presses ON on A-1 and OFF on A,
refuses when the prayer is already up, and reports `points_before/after`. Read a
cost over a window with `t.prayer.points()` after `t.ticks(2)`: `points_after` is
the client's stat read at once.

Measured: 20 back-to-back flicks of Protect from Melee cost 0 points; holding it
for 20 npc phases cost 4. T-1: a city guard hit 0/6 times with the prayer pressed on
A-1 against 4/6 pressed on A; the Inferno bat 0/3 against 3/3 (pf_a_final,
pf_b_final). Prayer still regenerates on this branch (ENG-10): the fix is held as a
patch (see "Held as patches" below).

## t.world.los -- line of sight as the server computes it

`t.world.los(from, to[, {routine=}])` returns ok, a detail, `seen` and a `reading`.
`from`/`to` are `"player"`, a `t.npc` row, a `t.npc.pack` row, or `{x=, z=[, size=]}`.
`routine` is `approached` (default: the AP rung and every ranged reach),
`line_of_sight` (RuneScript `lineofsight`) or `line_of_walk`. The reading holds all
three, `intersect`, `gap`, `in_scene`, the tile flags, the flagged `blockers` of the
ray's box and the server tick. An npc's ranged check is cast from the PLAYER to the
npc (LostCity does it backwards): ask `los("player", npc)` or read `sees_player`.
Only wall projectile bits and LOC_PROJ_BLOCKER block sight; a `::goto` onto a loc
tile reads blocked from it. Today the Inferno pillars block nothing (ENG-13).
Neither verb clicks: record with `t.check`, not `t.exec`.

## t.npc.pack -- every npc around the player

`t.npc.pack(radius | {x0, z0, x1, z1})` returns ok, a detail, rows (nearest first)
and the raw reading. `slot` is the WORLD slot (the tick log's key); `client_slot` is
the `t.npc` row's slot. A row has `symbol, x, z, size, hitpoints, attackrange, mode,
target_kind/target_text, walk_x/z, anim_seq/anim_tick, sees_player, gap_player,
health_ratio`. `anim_*` need `t.ticklog.start()` first. Attack style is not a server
field: map the symbol to a style from the unit's spec table. "walking to X,Z" in the
target text is a queued waypoint, not an engine target (the nibblers, ENG-14).

## t.player.drink / t.inv.doses

`t.player.drink(family[, opts])` drinks one dose by the item's own Drink op, fewest
doses first. Families: `prayer_potion`, `super_restore`, `saradomin_brew`,
`ranging_potion`, `bastion`, or any content stem. It returns ok, a detail and `info`
(item, slot, `after` (vial_empty after the last dose), stats before/after/base, doses
before/after, presses, pressed/landed tick); not_found when none is carried; no_row
for an unknown family. A drink lands +1 tick after the press; a press inside the last
drink's or eat's delay is dropped by the server, so the verb re-presses every 2 ticks.
`opts.then_attack` (npc symbol, row, or true) re-attacks one tick after landing: never
on the landing tick, the drink's trailing `p_stopaction` wipes it (ENG-18; the fix is
held as a patch). `t.exec`
passes only two returns, so call drink directly and write the row with `t.check`.

`t.inv.doses(family)` returns ok, a text and `{doses, free, capacity, items}`;
carrying none is ok with 0. On this branch an eat holds a projectile hit until its
delay ends (+2/+3 ticks) while melee lands at once (ENG-16; the fix is held as a
patch): plan eats off the projectile-impact tick.

## gate.py in this worktree needs QUEST_HELPER_ROOT

`tools/quest_gate/helper_coverage.py` looks for the Quest Helper checkout beside
the repository, then one level further up. From this worktree
(`build/orchestrator/worktrees/waves`) neither is the real checkout, so every quest
reads "no QUEUE row or Quest Helper guide -- not graded", and a quest whose
cutscene exemption needs a guide (zombiequeen) goes red. Grade with
`QUEST_HELPER_ROOT=/Users/matthewevers/Documents/git_repos/quest-helper python3
tools/quest_gate/gate.py --allow-blocked`. The tool's search path is the quest
loop's to fix, not this loop's.

## Inferno waves spawn once (seam pass 3)

A wave's set now spawns once: `[softtimer,inferno_wave_tick]` clears itself on the tick
it fires (ENG-5 fixed). Measured: wave 1 one set at server tick 19, last kill 72, last
`npc_free` 76, wave 2 one set at 84 (ws3_resp_after2). The next wave starts
`^inferno_wave_delay` = 8 ticks after the last `npc_free`; Blert has 6 (left for the
spec pass to grade). Read spawn sets with `t.ticklog.rows{kind="npc_spawn"}` grouped
by tick: 7691 nibbler, 7692 bat, 7700 jad, 7709 pillar.

`t.wave.enter` is a PRACTICE run: a cleared wave leaves. To reach wave 2 enter a REAL
run: stage `varb5646=2`, labelled `::goto 2495,5123`, `click_loc inferno_entrance 1`,
choose `/^Jump into the Inferno/`. `::inferno` writes `varb5646=1`
(`inferno.rs2:421`), so after a `t.wave.enter` the entrance has no Jump-in until it is
restaged to 2: run a real-run leg first. Wave 67 never spawns its Jad today (ENG-20).

## The combat logout delay (seam pass 3)

`[if_button,logout:logout]` refuses while fewer than 16 server ticks have passed
since the last `hit_player` row (wiki Logout button:9), with the chat line
"You can't log out until 10 seconds after the end of combat." (the text is
unsourced, an open row). From +16 the server closes the session. The client's own
5 s fallback (`app_net.c`) still logs a refused player out about 8 ticks later
(ENG-29), so `t.session.logout` / `relog` within 16 ticks of a hit reads as the
fallback ending it: wait until the last hit +16 for a server close. `t.ui.widget`
takes `"logout:logout"` as one string; a second string argument raises.

## A cast is a magic hit (seam pass 3)

A spell cast or splash now reaches every per-style rule as Magic, whatever is in
hand (Attack types:37): the black mask, salve, Slash Bash, the Nylocas and Zulrah
checks. `hit_npc` rows carry no style, so prove it with a differential (a melee-only
modifier on vs off). Melee never calls `~npc_retaliate`, and `retaliate=no` is still
ignored by the default retaliation (ENG-25, ENG-26, ENG-27): a nibbler still turns on the
player.

## Reading the npc pool (seam pass 3)

`t.wave.state().pool` and `t.npc.tiles` read the client pool's NEAREST 64 rows; far
rows can be ranked out at a crowded tile. `TORIRS_DRIVE_DEBUG`'s `npc_pool=N` is the
high-water count, not the live npcs; a radius-1 `api_drive` read logs every npc
outside it (`build/seam_state/matthew-mbp-m4-waves-b1-seam3/npa_probe_summary.py`).
Near a crowd the server's npc view radius grows from 15 to 24 tiles while fewer than
64 npcs are in view, so a pool can be FULL at Lumbridge.

## Held as patches (seam pass 3)

Two seams of pass 3 reddened green quest tests and are NOT on the branch; the owner
decides. Their patches are `docs/minigames/waves_loop/patches/matthew-mbp-m4-waves-b1-seam3.*.patch`
and their conformance rows wait in `build/seam_state/matthew-mbp-m4-waves-b1-seam3/held/`.
What a test author would see once they land (measured by the fixers with the patches
applied; none of it is true on the branch today):

- `eat_delay_port` (ENG-16, ENG-17, ENG-18): an eat is two clocks, never a park. A food
  sets a 3-tick food timer; an eat while the weapon delay runs adds 3 (karambwan and
  halibut 2); a ready weapon gains nothing; potions have their own 3-tick timer and no
  attack delay. Food then karambwan and food then potion work in one tick; food after
  food, food after karambwan and karambwan after karambwan are refused silently.
  Unarmed swing gaps 4 uneaten, 7 with a shark after the swing; a spell eaten through
  lands +1 like an uneaten one; an Attack pressed during a drink survives. Read-only
  readouts `::eatdelay` and `::eatgate`. Press two items in one Lua resume to put both
  on one server tick.
- `prayer_regen_and_drain` (ENG-10, ENG-11): prayer points do not regenerate; a prayer
  in force for k npc phases costs (k-1) x its drain effect (the activation tick is
  free, so one-tick flicking costs 0); the drain counter survives turning prayers off,
  running out and a potion. Protect from Melee for 20 phases = 228 units (3 points +
  counter 48).
