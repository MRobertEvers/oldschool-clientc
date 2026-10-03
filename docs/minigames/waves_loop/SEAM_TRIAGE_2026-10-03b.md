# Waves seam pass 2 triage (2026-10-03): the verbs a wave fight needs

Pass: `matthew-mbp-m4-waves-b1-seam2`. Written by the waves orchestrator after seam
pass 1 (the driver port) landed. `docs/WAVES_ORCHESTRATOR.md` section 5 lists thirteen
driver rows; rows 1 to 7 came over in seam pass 1 (`docs/minigames/waves_loop/DRIVER_NOTES.md`
has their shapes). This pass adds rows 8 to 13 for the Inferno. Four seams, four fixers,
disjoint files. The engine findings (retaliation, the spell damage type, the eat delay)
are seam pass 3: they change shared content and must not be edited while these fixers
measure.

Rules that hold for every seam here:

- **No content edits and no shared rebuild.** These seams are Lua, plus C for one of
  them. Nobody runs `make torirsserver-scripts` or rebuilds the shared test client. The
  one seam with C (`los_and_pack`) builds a PRIVATE client (`PLATFORM_OBJ_BASE` and
  `PLATFORM_TARGET` of its own) and runs with `QUEST_BINARY=<that binary>`; the closer
  builds the shared one.
- **Every verb is proved on an ordinary npc first** (a Lumbridge goblin, a guard), then
  shown once inside the Inferno through the content's own entry.
- **A verb answers `(result, detail)`** from the fixed result set of
  `docs/QUEST_AUTHORING.md`, and its detail names the SERVER tick wherever a tick matters.
- **Colosseum**: it has no content. A verb that takes a game name answers `unsupported`
  for `colosseum` with a detail saying so; nothing is guessed.

Facts the fixers start from (orchestrator, 2026-10-03):

- `[debugproc,inferno](int $wave)` at `minigame_inferno/scripts/inferno.rs2:413` clamps
  the wave to 1..`^inferno_wave_zuk`, leaves a running run, sets
  `%varb5646_inferno_sacrificed_firecape = 1`, clears `%varp6056_inferno_paused` and calls
  `~inferno_enter($wave, 1)`: the second argument is PRACTICE. So `::inferno` enters a
  practice run. `[proc,inferno_enter](int $start_wave, int $practice)` is at `inferno.rs2:216`.
- State the content keeps (`minigame_inferno/configs/inferno.varp`): `varp5889_inferno_active`,
  `varp6040_inferno_wave`, `varp5890_inferno_alive`, `varp6038_inferno_practice`,
  the pillars `varp6037/6035/6033_inferno_pillar_{w,s,e}_hp` and
  `varp6036/6034/6032_inferno_pillar_{w,s,e}_dead`, `varp6055_inferno_logout_req`,
  `varp6056_inferno_paused`, `varp6065_inferno_saved_wave`.
- Pause and logout in the content: `[debugproc,infernopause]` at `inferno.rs2:472`;
  `player/logout.rs2:25` calls `~inferno_on_logout`.
- Seam pass 1's proof found a cast refused with "I can't reach that!" from 3233,3238 at a
  goblin on 3234,3239 (diagonally adjacent), while a cast from 3226,3233 at 3230,3234
  landed (`build/quest_gate/dp_scratch2` row 28, `dp_scratch3` row 29). Unsettled.
- `QD.wave` exists and is empty; `wave` is not in `quest_driver.lua`'s `PARTS` yet.

## driver: wave_enter_state_pause

Units: every Inferno unit; inferno_pause_and_logout; inferno_entry_and_cape.

Files: `script/plugins/quest_driver/waves.lua`, `script/plugins/quest_driver.lua`

Summary. Fill the empty `waves.lua` part and add `wave` to `PARTS`.
(1) `t.wave.enter(game, wave, opts)`: for `inferno`, enter at a wave the way the content's
own debugproc builds it (`::inferno <wave>` through `t.cheat`), wait until the server says
the run is active on that wave and the wave's npcs are in the client's pool, and answer
`ok` with the wave, the alive count, the player's tile and the server tick. It is a
bring-along (setup of a unit), never a skip inside a run: refuse with `refused` when a
run is already active unless `opts.restart` is set, and say in the detail that the run is
a PRACTICE run, because that is what the debugproc starts (a real run is entered by click
through the entrance, which the entry unit's test does itself).
(2) `t.wave.state()`: a read-only table from the server's vars (`t.var.server`): game,
active, wave, alive, practice, paused, logout_requested, saved_wave, and for the Inferno
the three pillars as `{hp=, dead=}` keyed `w`, `s`, `e`. Second return is a one-line detail.
(3) `t.wave.await_wave(n, ticks)` and `t.wave.await_clear(ticks)`: wait on the wave number
or on alive = 0; `ok` with the tick it happened on, `timeout` with the last state.
(4) Pause and logout BY CLICK. Read the content first (`inferno.rs2` around the
`logout_req` and `paused` writes, `~inferno_on_logout`, `[debugproc,infernopause]`) and
the pinned sources for what the real game does (`docs/minigames/inferno/SOURCES.md` and
`sources/wiki/`: logging out mid-wave asks for a second click and logs the player out
when the wave ends; logging back in resumes paused before the next wave). Then
`t.wave.pause()`: press the client's own logout button inside a run and answer what the
server did (`ok` with logout_requested or paused and the tick; the chat line it printed);
`t.wave.resume()`: after `t.session.login()`, do what a player does to resume, and answer
the wave it resumed on. If the content's pause cannot be reached by click (only by
`::infernopause`), do NOT route the verb through the debugproc: answer `unsupported`,
and write the finding in `engine_findings` with the content line, because a pause that
only a cheat can reach is a content bug for the content seams.
Prove: enter at wave 1, read state (three pillars alive, alive count matches the pool),
kill nothing; enter at 3 with `opts.restart`; the pause path as far as the content goes.

Evidence. `docs/WAVES_ORCHESTRATOR.md` section 5, rows "wave.enter / wave.state" and
"pause and logout"; seam pass 1's report (`build/seam_state/matthew-mbp-m4-waves-b1-seam1/fix.driver_port.json`,
open issue "QD.wave is empty and 'wave' is not in quest_driver.lua's PARTS").

## driver: prayer_flick

Units: inferno_blob_and_splits (the blob flick), inferno_single_jad, inferno_triple_jad,
inferno_mixed_late_waves, inferno_zuk_fight; every unit that switches prayer by attack order.

Files: `script/plugins/quest_driver/prayer.lua`

Summary. On top of the ported `t.prayer.set / read / points`:
(1) `t.prayer.set_on_tick(name, on, tick)`: wait for the named SERVER tick and press the
prayer book on it; answer `ok` with the tick the press was issued on and the tick the
server's varbit showed it; `timeout` when the tick has passed; never a silent late press.
(2) `t.prayer.switch(names)`: two or more presses inside ONE tick (protect from magic to
protect from missiles, with or without an offensive prayer), answering the tick each took
effect on; prove both land in one tick, and say what the server does with two protection
prayers pressed in one tick (the last wins, per the content: quote the script line).
(3) `t.prayer.flick(name, at_tick)`: on for the npc's phase of a tick and off again (the
one-tick flick), answering the ticks of on and off AND the prayer points before and after.
Measure the cost of a one-tick flick repeated 20 times against holding the prayer for the
same 20 ticks, and quote the drain rule from the content or engine line that implements it
(the real game's rule: a prayer switched on and off within one tick drains nothing, because
drain is counted at the end of the tick; settle what OUR server does from its own source
line and report a difference as an engine finding, do not change the drain).
(4) The T-1 fact, measured: with the tick log, show on a goblin that a protection prayer
pressed on tick T blocks a hit the npc rolls on T+1 and does not block one rolled on T
(or what our server really does), in tick-log rows. The detail of every verb names both
ticks so a test's technique row can assert "the prayer was up on the tick the hit was rolled".
Prove on a goblin; then once in the Inferno against a bat (wave 1 has none: pick the first
wave the wave table gives one, through `::inferno`; if `t.wave.enter` has not landed in
your tree use `t.cheat("::inferno <n>")`), protect from missiles up on its attack tick.

Evidence. Section 5 row "prayer flick": "switch on a named server tick and read back the
tick it took effect; two switches inside one tick; the prayer-point cost of a one-tick
flick"; `docs/minigames/theater_of_blood/ENCOUNTER_TIMING.md` section 1.

## driver: los_and_pack

Units: inferno_nibblers_and_pillars (the pillar safespots and the stack),
inferno_zuk_glyph_shield, inferno_mixed_late_waves, inferno_mager_resurrection.

Files: `script/plugins/quest_driver/world.lua`, `script/plugins/plugin_api.meta.lua`,
`src/plugin/torirs_plugin_drive.c`, `src/plugin/torirs_plugin_drive.h`,
`src/plugin/torirs_plugin_drive_los.c`, `src/torirsserver/torirs_server.h`,
`src/torirsserver/torirs_server_los_query.c`, `src/makefile`

Summary. (1) `t.world.los(from, to)`: line of sight between two tiles (or the player and
an npc slot, footprints included) AS THE SERVER COMPUTES IT: the answer comes from the
server's own line-of-sight routine through the embedded-server query path the tick log
already uses, never from a client-side reimplementation. Answer `ok` with `true/false`
and which routine and flags decided it. (2) `t.npc.pack(radius_or_area)`: every npc around
the player with slot, type symbol, tile, size from its record, hitpoints ratio where the
client has one, its current target (the server's interaction target: the player, a
pillar npc, another npc), its last animation and the tick of it, and `sees_player`
(the server's line of sight from the npc's footprint to the player this tick). Style is
not a server field: the test maps type to style from its spec table. (3) Settle the
unsettled cast from seam pass 1 (3233,3238 at a goblin on 3234,3239 refused with
"I can't reach that!"): reproduce it, read which check refused it in our server, and
compare with the source for what the real game does (the LostCity 2004 engine's
line-of-sight and reach code under `/Users/matthewevers/Documents/git_repos/LostCity_Content2`
or its engine; a fence or a diagonal corner rule): quote the line. If our server is wrong
it is an `engine_findings` entry with the measurement, not a fix in this seam.
Private build only (see the rules above); `check-drive-abi` must pass; CLAUDE.md's assert
rules apply to the new C. Prove on ordinary ground: two tiles with a wall between them
(false), two open tiles (true), an npc behind a building corner (`sees_player` false);
then in the Inferno at wave 1: the pack lists the nibblers with a PILLAR as target, and
`los` from behind a pillar to a spawn tile is false.

Evidence. Section 5 rows "line of sight" and "pack reads"; seam pass 1's engine finding 3
(`fix.driver_port.json`).

## driver: supplies_by_dose

Units: every Inferno unit that outlasts one prayer potion (all waves from the bat on,
Jad, Zuk); the full-run test.

Files: `script/plugins/quest_driver/combat.lua`

Summary. (1) `t.player.drink(family, opts)`: drink ONE dose of a potion family
(`prayer_potion`, `super_restore`, `saradomin_brew`, `ranging_potion`, `bastion`, the
names the content's obj symbols use), choosing the item with the fewest doses left first,
by the item's own Drink op; answer `ok` with the item before and after (4 to 3 doses, the
vial when empty), the stat it changed (before and after) and the server tick; `not_found`
when the backpack has none. (2) `t.inv.doses(family)`: total doses and free backpack slots,
so a test counts what it carries. (3) `opts.then_attack = <npc>`: re-issue the attack after
the drink, because an eat or drink drops the player's target (the raid loop's first
authoring lesson). (4) Measure and report, with tick-log rows, what a drink and an eat cost
on OUR server today: the ticks the player's next attack is delayed, and whether a queued
incoming hit is held by the eat's delay (the raid loop found it is; `v3` still has that
behaviour). Do not change it: it is seam pass 3's first row; write the measurement in
`engine_findings` so that pass starts from a number.
Prove on a goblin with a prayer potion and a lobster at reduced stats (`::setlevel` and a
drain are setup bring-alongs).

Evidence. Section 5 row "supplies and choices"; section 10 lessons 7, 14 and 15.

## design: colosseum_choices

Units: the Colosseum's modifier choice and reward chest.

Files: none

Summary. The modifier choice and the reward interfaces are read and clicked by symbol,
but the Colosseum has no content and no interface is opened by our server yet. This row is
recorded so it is not lost; it becomes a driver seam when the Colosseum's build seams open
those interfaces.

Evidence. Section 5 row "supplies and choices"; section 8.
