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
moved green quest tests and were held as patches; seam pass 4
(`matthew-mbp-m4-waves-b1-seam4`) landed both on the owner's decision, closed the
three prayer rows pass 3 left open, made wave 67 spawn its Jad, and made the engine
honour `retaliate=no` (no verb; 108 seam rows; see "Seam pass 4" below). The
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
must account for that window (`CONTENT_BUGS.md` ENG-2). It no longer restores
drained prayer (ENG-10, landed in seam pass 4): prayer points never regenerate.

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
call `t.session.login()` next) or timeout. Since seam pass 5 the logout button's first
press inside a real run is the pause request (ok "requested"); see "Seam pass 5: entry,
pause and death" below. A second press can answer ok "PAUSED" (paused=1 is read before
the socket closes) rather than unsupported: log in next either way.

A paused run is now resumed by the LOGIN itself (`~inferno_login`), so after
`t.session.relog()` `t.wave.resume()` answers refused (the run is already active); the
paragraph below describes the verb as written, for content that resumes at the entrance.
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
16 ticks after the last hit (see "The combat logout delay"); since seam pass 5 the
client no longer ends a refused session itself, so the player stays in the fight.

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
pf_b_final). Prayer no longer regenerates (ENG-10, landed in seam pass 4), and the
cost of a flick or a hold is what "Prayer drain since seam pass 4" below says.

## t.world.los -- line of sight as the server computes it

`t.world.los(from, to[, {routine=}])` returns ok, a detail, `seen` and a `reading`.
`from`/`to` are `"player"`, a `t.npc` row, a `t.npc.pack` row, or `{x=, z=[, size=]}`.
`routine` is `approached` (default: the AP rung and every ranged reach),
`line_of_sight` (RuneScript `lineofsight`) or `line_of_walk`. The reading holds all
three, `intersect`, `gap`, `in_scene`, the tile flags, the flagged `blockers` of the
ray's box and the server tick. An npc's ranged check is cast from the PLAYER to the
npc (LostCity does it backwards): ask `los("player", npc)` or read `sees_player`.
Only wall projectile bits and LOC_PROJ_BLOCKER block sight; a `::goto` onto a loc
tile reads blocked from it. Since seam pass 5 the Inferno pillars are the cache's locs and block sight and
walking (ENG-13 fixed); before, they blocked nothing.
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
on the landing tick before seam pass 4; since the eat delay landed (ENG-18) no drink
ends in a `p_stopaction`, so an Attack pressed during a drink swings, and a re-attack
at once (`attack_after` 0) gives a swing gap of 4. `t.exec`
passes only two returns, so call drink directly and write the row with `t.check`.

`t.inv.doses(family)` returns ok, a text and `{doses, free, capacity, items}`;
carrying none is ok with 0. Since seam pass 4 an eat no longer holds a queued hit
(ENG-16): a spell eaten through lands +1 like an uneaten one, so a tick eat on the
impact tick is a real heal and not a delay.

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
unsourced, an open row). From +16 the server closes the session. Until seam pass 5
the client's own 5 s fallback (`app_net.c`) logged a refused player out about 8 ticks
later (ENG-29); it is gone (the rev-239 client never counts its logoutTimer down), so a
refused player stays in the world indefinitely and `t.session.logout` inside 16 ticks
of a hit answers timeout (its detail text still names the old fallback, ENG-69): press
again after the last hit +16. A logout watch must poll the screen inside one `t.await`:
`t.ticks` stops on the title screen (it counts the client's world cycle). `t.ui.widget`
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

## Held as patches (seam pass 3), landed in seam pass 4

Two seams of pass 3 reddened green quest tests and were held; the owner decided
(2026-10-03, night) that both land, and seam pass 4 landed them. The text below is
kept as it was written; it is now true on the branch. The patches stay in
`docs/minigames/waves_loop/patches/matthew-mbp-m4-waves-b1-seam3.*.patch` as the record;
their conformance rows are in `test/quests/_conformance.lua`. What a test author sees
(measured by the fixers with the patches applied):

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

## Seam pass 4: the eat delay (eat_delay_land)

No verb added or changed. Two seam rows came back:
`seam.eat_does_not_hold_queued_hit` and `seam.eat_delay_clocks`, after
`seam.attack_fast_path`. What a test sees:

- An eat no longer holds a queued hit: a spell eaten through lands +1, as an uneaten
  one does. Food is a 3-tick gap. An eat inside a running weapon delay adds 3 (a combo
  food 2); with the weapon ready it adds nothing.
- On one tick, food then combo food and food then potion both work. Food after food,
  combo after combo and combo then food are refused, and the server says nothing.
- An eat that lands ON the swing tick is applied after that swing (gap 4, then 7).
  No source states this order; it is ENG-44.
- The Gauntlet: the paddlefish and the crystal paddlefish use the shared food timers,
  so a shark straight after a paddlefish is refused. Egniol sips are 3 ticks apart.
- `QD.SUPPLY_REATTACK_AFTER=1` relied on the old `p_stopaction` wipe. A drink with
  `attack_after` 0 now gives a swing gap of 4 (it was 5, and the attack timed out).
- Read-only readouts: `::eatdelay` (the three timers) and `::eatgate` (the gate's
  answers). Press two items in one Lua resume to put both on one server tick.

## Seam pass 4: prayer drain (prayer_land)

No verb. Two seam rows: `seam.prayer_drain_activation_tick` (after `prayer.flick`) and
`seam.prayer_drain_fresh_per_prayer` (at the end of the plan; see "Conformance row
order" below). The drain rule now:

- Each prayer is free on its own activation tick, even one lit over a prayer that is
  already draining. A prayer pressed on tick T is first charged by T+2's drain.
- Prayer points never regenerate. Chivalry drains 12 (1 point per 3 s), not 24.
- The cost of a window is points lost x (60 + 2 x prayer bonus), plus the change in
  `t.var.server('varp6296_prayer_drain_counter')`. The counter survives turning
  prayers off, running out of points and drinking a potion. A death zeroes it.
- Measured with `t.prayer.switch`, `set_on_tick` and `flick` (all equal the wiki):
  hold 20 = 228, 20 flicks = 0, two prayers held 20 = 456, one lit over another = 396,
  3 flicks over a hold = 228, on and off in one tick = 228, Chivalry for 60 = 708.
- A death scratch can die for real (`::setlevel hitpoints 2`, `::spawn city_guard`,
  attack it) and still read vars and messages afterwards. Only click verbs hit the
  death fence.

## Seam pass 4: wave 67 and a real run at wave N (inferno_jad_wave_spawn)

No verb. The collapse at the start of wave 67 now brings down every remaining pillar
and spawns the Jad: 1 Jad on wave 67, 3 on wave 68.

- To start a real (not practice) run at wave N, set up with `::setvar varb5646 2`,
  `varp6056 1`, `varp6065 N`, goto 2495,5123, then `t.wave.resume()`. The pillars
  come back at 255 hitpoints.
- Wave 67 begins 8 ticks after the last `npc_free` of wave 66. The three pillar frees
  and the Jad's spawn are on that tick, and the Jad fires on the next.
- Do not read a pillar's tile from its npc: the pillar npcs wander (ENG-45). Use
  `wave_table.pillar_tiles` and `t.wave.state().pillars`.
- `t.ticklog.rows{kind='sound'}` is refused: the tick log has no sound kind.
- Wave 66 by real attacks took about 350 ticks with a twisted bow, rune arrows and
  Protect from Magic. Walk away when a Jal-Zek's `gap_player` is 1 or less: in
  reach, it melees.

## Seam pass 4: retaliate=no (retaliate_no)

No verb. One seam row, `seam.retaliate_no`, after `wave.resume`. The engine now skips
the default `[ai_queue1,_]` retaliation rung for an npc whose record says
`retaliate=no`. A type's own `[ai_queue1,<type>]` binding still runs. The Jal-Nib
record says `retaliate=no` (wiki Jal-Nib:50).

- `t.ticklog.rows{since=}` takes a SERIAL (ticklog.lua:30), not a tick. Filter on
  `row.tick` yourself.
- A projectile row's target is the world slot + 1: the slug at slot 1079 is target
  1080.
- Count casts from projectile rows (the Wind Strike spotanim is 91). A splash writes
  no `hit_npc` row, but it still provokes.
- The rung still ignores `::passive` and the categories the C latch refuses
  (ENG-49). A goblin hit by a spell takes the player even when its type is passive.
- Inferno wave 1 needs Protect from Missiles for any measurement longer than 60
  ticks: the bat killed an unprayed 99 Hitpoints, 99 Defence player in 40 ticks.

## Conformance row order is part of the harness

The conformance file runs as one session in one world, and its rows read live npcs.
More ticks before a row changes the world's rolls for every row after it. In seam
pass 4, putting `seam.prayer_drain_fresh_per_prayer` next to the other prayer row
made `seam.attack_presses_the_watched_slot` fail: a goblin hit earlier by
`player.cast` was still retaliating and swung at the player, so the Attack answered
"I'm already under attack." The same row order on HEAD's C and content failed the
same way, so the seams did not cause it. Killing that goblin in setup then broke
`seam.npc_facing_read` further on, because Hans had walked out of range. A new row
whose setup does not depend on what came before goes at the end of the plan, before
`finish`, as `seam.retaliate_no` and `seam.prayer_drain_fresh_per_prayer` do (ENG-50).
A C change is measured with a private client (`PLATFORM_OBJ_BASE`,
`PLATFORM_TARGET`, passed as `QUEST_BINARY`) and the scratch recipe at the top.

## Seam pass 5: no verb changed

Seam pass 5 (2026-10-04) was content and one client fix: no driver verb was added,
changed or brought over. It added one seam row (`seam.inferno_entry_by_click`) and
sharpened `seam.eat_does_not_hold_queued_hit` (it now aims the eat to LAND on the cast
tick or the plain hit tick; the old row passed on the pre-port content too, ENG-58).
`run.py` takes one quest per call: `run.py cooks_assistant druid` fails in argparse.

## Seam pass 5: reading the tick log of an Inferno scratch

A scratch whose tick log starts before the `goto` into the arena also logs overworld
`npc_spawn`/`npc_free` rows: filter by the Inferno npc ids 7691-7700 (pillars 7709,
7710). Anchor tiles on the west pillar's `npc_spawn` row (region-local 17,37); npc
rows give the SOUTH-WEST tile, the same frame as Blert and the scouter (C013).
`t.ticklog.rows{since=}` takes a SERIAL (the S of `t.ticklog.mark`), not a tick: a
tick there miscounts. `t.exec(name, t.wave.state)` fails "bad verb/target": pass
`"inferno"` as its first argument.

## Seam pass 5: spawns and the wave clock

Combat npcs draw their tiles per wave from the nine without replacement; nibblers pick
one of the nine tiles of their 3x3 block each, with replacement (two may share). A
wave's last despawn is followed by "Wave completed!" on that tick and the next
"Wave: N" 6 ticks later (`^inferno_wave_delay`); wave 1 begins 8 ticks after Jump-in
(`^inferno_entry_delay`), landing at local 30,36. Each wave prints exactly one
"Wave: N" line now (ENG-37). A blob's bloblets land mage (2,2), range (1,1), melee
(0,0) from its SW tile, on its `npc_free` tick.

## Seam pass 5: monster timing a test author sees

The ranged Inferno monsters swing in `[ai_opplayer2]` on the clock tick (it was one
tick later in `[ai_applayer2]`): ranger first attack spawn +1, blob spawn +4, bloblets
spawn +3 then every 4. The mager still swings a tick after its clock (MAGER-AP-LAG).
Blob flick: the blob READS the prayer 3 ticks before its projectile leaves (wiki
Jal-Ak:49); with the read on A-3, `set_on_tick(Missiles, A-4)` makes it throw magic and
on A-3 ranged; switch to the counter prayer on A-1 (the projectile's damage is blocked
by the prayer up when it leaves). Nibblers: one pillar per wave, each on a ring tile,
a bite every 4; idle if their pillar falls while another stands; they turn on the
player only when no pillar stands (0-4, no miss roll, blocked by Protect from Melee).
Melee dig: 50 ticks after the wave starts, then 40-60 after its last swing (no reset in
the first 30); dig anim 7600, up 7601 +6, swing +12.

## Seam pass 5: auto-retaliate off and run, in setup

Set `::setvar varp172_option_nodef 1` (auto-retaliate off) in a wave scratch's setup:
otherwise the player walks to and fights the first monster that hits, and a safespot
or a measured cadence is lost (a mager hit on T also skips its T+1 swing,
MAGER-HIT-SKIP). Run: `t.ui.invoke(select(2, t.ui.widget('orbs:runbutton')), 1)`; run
energy: `t.ui.text('orbs:runenergy_text')`.

## Seam pass 5: pillars

The pillars are the cache's locs (`inferno_safespot1..3`), so they block `t.world.los`
and walking. Safespot: stand one tile off the pillar on the far side of the npc's
straight line; ranger, mager and bat stood there with `sees_player` 0. A fall shows in
the tick log as `loc_set` loc -1, `npc_spawn` 7710, `npc_anim` 7561, `npc_free` 7709,
then `npc_free` 7710 two ticks later. After wave 66 the pillars fall on the last
despawn tick, for half the player's current hitpoints within one tile; none stand at
67 or 68.

## Seam pass 5: the Jal-Zek revive

Revive reads: `npc_anim` seq 7611 on the mager and an `npc_spawn` row on the SAME tick
(local x30-37 z28-35). The revived hp is taken with `npc_statsub` (no `hit_npc` row):
read it from `t.npc.pack(...).hitpoints` (bat 12, blob 20). Find a revived npc's first
swing by its attack sequence (bat 7578); 7579 is the bat's defend and lands first when
you hit it. `t.drive.camera` before `t.shot` did not move the shot's camera; an attack
press on the target aims it. Waves 37-42 kill a bow-only player at 99s: plan pillars or
Missiles for the bats and bloblets.

## Seam pass 5: entry, pause and death

A real run by click: `talk_to("inferno_master")`, then
`chat.play{"npc:the Inferno awaits", "choose:Sacrifice your fire cape."}` (varb5646
becomes 2), a labelled `::goto 2495,5131` (the entrance pocket is not walkable from
Ket-Keh, ENG-63), `click_loc("inferno_entrance", 1)` and the Jump row. Pause: the logout
button's first press is the request; the run pauses at the end of the wave and stays in
the arena (paused=1, active=1, alive 0); `t.session.relog()` resumes it at login, wave
+16 ticks. The second press (16 ticks unhit) logs out, and the login restarts the same
wave with its full alive count. Never `t.ticks()` on the title screen. A death pays
`~inferno_tokkul_for_wave(wave)`, doubled with the elite Karamja diary (varb4566); a Zuk
win gives the cape and 16,440 Tokkul (doubled), on the ground if the pack is full.

## Seam pass 5: death restores drained stats

A death now restores every DRAINED stat, prayer included, to base; a BOOST survives the
death (ENG-66, unsourced). Before, `death_restore_stats` was a no-op and only the
stat_restore timer's prayer regeneration hid it (ENG-60).

## Seam pass 5: old content on the shared client

`TORIRSSERVER_CONTENT=<throwaway content root>` runs the shared client on another
content pack, which is how a before/after proof of a content change is made without
touching this tree (sf_eat_row_old ran the pre-port eat files from a throwaway
worktree).

## Seam pass 6: no verb changed

Seam pass 6 (2026-10-04) was content only: Jad and the triple Jad, the glyph, Zuk and
the final-wave adds. No driver verb was added, changed or brought over, and no seam
row was added. The wave-69 proofs are scratch scripts kept in the pass state directory
(`build/seam_state/matthew-mbp-m4-waves-b1-seam6/`): `zk_body.lua` plus the
`zk_*.snip` files, generated by `zk_gen.py`; the glyph's `gf_*` and the healers'
`ad_*` follow the same shape. Each runs with `run.py --script <file> --name <own
name>` against the built pack: no build of its own and nothing published.

## Seam pass 6: wave Jad timing

A wave Jad's first swing is spawn+4. Wave 68's three swing first at +4, +7 and +10
(tile b second, tile c third), then every 8 ticks on wave 67 and every 9 on wave 68;
the melee swing is 4. Both the magic and the ranged hit land 3 ticks after the swing
at any distance. The protection prayer is read on the swing tick, so only a prayer
already up when the animation starts blocks the hit; a bot that prays after the
animation is using the wrong technique. The ranged swing makes its sound with the hit,
the magic one at the swing; the tick log has no sound kind, so the tell is not
readable from it.

## Seam pass 6: Jad death and its healers

A Jad killed by blow D plays its death animation at D+1 and is removed at D+7 together
with its living healers; "Wave completed!" prints on D+7 and the next wave starts at
D+13. `t.npc.pack` drops a dying Jad (`dying=true`): wait on `t.wave.state`'s wave or
pool to see the removal, not on the pack. Healers land x -5..5, z -5..9 from the Jad's
CURRENT south-west tile (read the `npc_tile` rows), never within 1 of its 5x5.

## Seam pass 6: resuming at a Jad wave

`t.wave.resume({begin=false})` returns before the wave starts, but the player cannot
walk until it begins: walk right after the spawn. At wave 67 the player stands inside
the Jad's 5x5 until then (ENG-80), so the first swing can be melee.

## Seam pass 6: the glyph walk

To play the shield walk, LEAD the glyph (target `sx + 1 + 2*dir`) and attack only while
it dwells; a player trailing at end-2 on an arrival tick is hit. The glyph is safe for a
player whose x at T-1 lies in glyph x(T)-1..+3 on rows 5356-5358 (local z 44..46; the
fight row is local z 46). At a dwell stand at x-1 (east end) or x+3 (west end) so a
late-read reversal stays inside the window. Blert's shield and player x are south-west
tiles: base = Zuk's `npc_spawn` coord minus local (28,52). A glyph death in the tick log
is `npc_death` D, `7569` once at D+2, then `npc_free` at D+6, and Zuk's next shot hits
the player. A fresh server's first glyph always goes west (ENG-81): enter wave 69 three
times with `{restart=true}` first and the fourth glyph life goes east (gf_a_east1). The
state directory's `gf_an.py <run dir>` prints dwell, gaps, x at Zuk's attacks, Zuk hits
and death ticks. Eating with `inv_op` in a step loop costs about 4 ticks of standing
(ENG-82); the glyph scratches pressed `_inv_press` instead.

## Seam pass 6: Zuk timing a test author sees

The shot that straddles the 240 enrage fires on max(last+7, crossing+1), then every 7.
The set lands Jal-Zek at 2266,5351 (first swing spawn+6) and Jal-Xil at 2275,5351
(spawn+8), then 4; the set comes at Zuk's first shot + 59. The Zuk-wave Jad (7704)
swings first at spawn+7 and targets the glyph. On Zuk's death 7562 plays on ARRIVE, the
minions go at ARRIVE+2, Zuk and the leave at ARRIVE+5; a shot already in flight can
still kill (ZUK-SHOT-AFTER-DEATH). The wiki's safespots 2260/2266/2276/2282 hold before
the enrage. `::zukhp N` goes through Zuk's damage queue, so it spawns the Jad or the
MejJaks if it crosses 480 or 240; one call that crosses both spawns only the Jad
(ENG-83).

## Seam pass 6: a real run at wave 69

Setup: `::setvar varb5646 2`, `varp6056 1`, `varp6065 69`; goto 2495,5123; then
`t.wave.resume()`. `t.var.server('varp1585_total_zuk_kills')` reads 0 until
`inferno.varp` declares it (ENG-76, ZUK-ENG64-CONTENT). `t.prayer.set` of Protect from
Missiles answers timeout inside the arena (ENG-77).

## Seam pass 6: Jal-MejJak

Zuk's hp per tick is in `t.npc.pack(70)`'s zuk row `.hitpoints`, a server read, but the
mark lands one tick after the change. A Jal-MejJak springs up (2864) on spawn, throws
its first heal at spawn+7 (Zuk gains at spawn+8), then every 3. To provoke one, cast
`t.player.cast('wind_strike','inferno_zuk_healer',2,2,{slot=client_slot})` with Magic 99
and recast until a `hit_npc` row: a splash does not provoke (ENG-78). Cast only right
after a Zuk shot and on the follow tile, since the press can take 3 ticks while the glyph
moves. A `step_tick` to a blocked tile (local x 46, the east end) stalls the driver
about 8 ticks: clamp the target to local x 17..45. Lava hits log as `hit_player` with
`npc_slot` -1 (ENG-85).
