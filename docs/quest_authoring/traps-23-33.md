# Traps 23-33 (section 5)

Section 5 of the manual, traps 23-33.

## Trap 23. A `setup` `::give` has reached the backpack before `run()`'s first row, and one that reaches nothing is a FAIL row.

It did not use to: a cheat's reply outruns its own effect by one server tick (the ladder `say()`s
from inside its branch, while the backpack arrives in the container listener's end-of-tick
`UPDATE_INV`), so `run()`'s first `t.inv.count` read an empty pack from a setup that had answered
`ok`. `run.py`'s wrapper now counts the item client-side before each `::give`, waits for that count
to RISE (10 ticks), awaits an empty pack after `::clearinv`, and ends the setup loop one tick later
-- so read your setup items in `run()`'s first rows with no leading `t.ticks(...)`.

The check also catches what `ok` never could: `::give nosuchitem` and an ambiguous name both `say()`
their complaint and answer `ok` having given nothing, and each is now `setup.<the cheat>` FAIL,
ending the run the way a `no_row` setup cheat does. Stage items in `setup`; the `run()` workaround
is retired.

## Trap 24. A CLICK VERB'S `ok` IS THE SERVER'S SENTENCE, NOT THE CONTAINER UPDATE.

The engine writes a backpack/varp delta into the NEXT tick's player update, so `_settle_after_click`
resolves one server tick before the client has been shown what the press did. Only `use_on` waits
for it (its `ok` now carries `[backpack: gained X 0->1; lost Y 1->0]` when the press moved an item,
after up to 2 ticks). After any OTHER click verb, read a container with
`t.inv.await`/`t.var.await_server`, or spend a `t.ticks(1)` -- never a bare `t.inv.count` on the
line below. Measured in `build/quest_gate/scorp_probe2` rows 8-9 and `scorp_probe3` rows 6-8:
`t.settle()` does NOT close it, one server tick does.

## Trap 25. A DIALOGUE'S EFFECTS ARE NOT READABLE IN THE TICK THEY WERE WRITTEN.

The server writes an `inv_add`/varp/xp grant DURING a tick and transmits it at the END of it, and
several chat pages are clicked inside one tick -- so a read taken the instant `chat.play` returns is
a read of the world from before the conversation. As of 2026-09-21 `chat.play` waits that boundary
out itself (one extra tick per call), so the hand-written `t.ticks(1)` `sheepherder.lua` carries
after `talkToOrbon-dialog` is now the verb's job. `t.chat.drain` does NOT do this yet -- after a
drain, read a container the trap-24 way. Five consecutive readings:
`build/quest_gate/sh_feed_probe/ledger.tsv`.

## Trap 26. `talk_to` waits for the page it is owed; a press whose only effect is in the backpack still answers `ok`.

A talk settles on the first edge the click made -- the route running out, or the content script's
opening `mes` line -- and the dialogue can be up to four ticks behind it, so `talk_to` now waits up
to 5 ticks for a page and its detail says which happened (`...: dialogue npc is up`, or
`...: no dialogue in 5 tick(s)`). A `use_on` whose branch only swaps an item (no page, no chat line,
no route -- `[opnpcu,gertrudescat]`'s milk) used to time out at the full deadline; it now answers
`ok` with the backpack diff as its evidence, AFTER the timeout and never instead of an arm.

Neither changes when anything else resolves. That allowance also grades a REFUSED press `ok`: when
the click walked into "I can't reach that!" (a fence between you and a wandered npc -- `ball.lua`
run 6, shot `58-returnToBoy`), the row reads PASS and the failure surfaces one row later as "no
dialogue is open". Read the chat log in the talk's shot before trusting a bare ok, and goto the
npc's LIVE tile (`t.npc.nearest`) after a long leg.

FIXED seam29 for the reach case: when no page opened, `talk_to` lets a still-running walk finish
(up to 15 ticks), waits for the page again, and answers `refused` quoting "I can't reach that!" --
even when a zone or ambient `mes` resolved the settle first (s29t_reach1). A no-page talk the
client did NOT refuse still answers `ok` with the line quoted.

## Trap 27. A single `mes()` string has two hard byte budgets, and both fail silently.

`mes()` rides MESSAGE_GAME, which is a var-u8 packet at rev 239 (payload = `strlen` + 3), so a
string over **252 characters** writes its own length modulo 256 and the client reads the rest of the
narration as opcodes: the session desyncs and dies, several packets later, with nothing pointing
back at the line that did it. The server refuses that loudly now (2026-09-21, `ToriRSServer_Send`
prints `dropped ... N bytes cannot be declared in a var-u8 length` and drops the message) rather
than taking the connection, so a run whose chat is missing a sentence should be grepped for that
line in `client.log`.

A second, softer cap sits under it: `RS_CHAT_TEXT_LEN` is 200, so anything over **199 characters**
frames correctly and still RENDERS cut mid-word -- ten strings pack-wide are in that band today.
`~chatnpc*`/`~chatplayer*`/`~mesbox`/`~objbox` are subject to NEITHER: they ride IF_SETTEXT, which
is var-u16. Measure the RESOLVED string, never the source line -- `<tostring(...)>` compiles to a
join and resolves to a few digits, so a 473-character source line can be an 87-byte message, and an
audit that greps line length reports four quests that have no problem at all.

## Trap 28. A `multinpc<N>` table is indexed by VALUE, and `multinpc1` is value 0.

The npc twin of trap 20's `multiloc` rule, with one extra edge: a `-1` in a positional slot means
the npc is HIDDEN at that value, not "fall back". So a quest bit named `_pres`/`_visible` may well
mean the opposite of its name -- Making History's two ghosts were written `= ^true` for "standing
there" and vanished the moment their carrier began transmitting. Read the shell's own table in
`configs/all.npc` (a cachepack EXPORT -- it is the cache's data, not something content may rewrite)
before writing the bit, and after DECLARING a carrier `transmit=yes` for the first time, re-drive
the quest end to end: the declaration is what makes a wrong table visible, so the varp fix and the
polarity bug arrive in the same run.

`quest_deviousminds`' `%devious_monk` (0 = hooded and visible, 1 = dead) is the correct precedent. A
shell whose value selects a -1 rung is NOT DRAWN, and `talk_to` now answers `not_visible` naming it
(`resolved to NO child`, seam28: Goblin Diplomacy's constants were 1-based, so blue Grubfoot was
undrawn -- fixed to brown 0 / orange 1 / blue 2 / hidden 3). Read the table before blaming a wander
or the camera. `tools/data/multinpc_shells.csv` labels rungs by VALUE since seam pass 29 (column
`variants_by_value`: `0=` is multinpc1, and the last rung prints `N+=` because every out-of-range
value falls to it); before that it numbered them 1-based from the config text (FIXED seam29).
Roving Elves' Eluned carried the same off-by-one (fixed to 0/1/2), but its base varp
`sote_tertiary` is not transmitted, so the client draws the one-option Eluned whatever is written.

## Trap 29. A `loc_near(sym, R) -> not_found` SWEEP IS NOT EVIDENCE THAT A LOC IS UNPLACED.

Grep the map text first: `grep -rn ': <id> ' OSRS-Content/osrs239-content/maps/*.jl2`, and decode
the hit as `abs_x = mapx*64 + localx`, `abs_z = mapz*64 + localz` off its
`level localx localz: id shape rot` line. Six anchors at radius 60 sound exhaustive and are
worthless when the only copy is in the UNDERGROUND band, which is `z + 6400` -- Mourning's End Part
I was filed as a content_bug reading "`carnilleanrange` (2859) has zero live placements anywhere in
the loaded world", and the range is live at (2538,9699,0), cooks exactly as `mend1_poison.rs2:174`
is written, and carried the quest to 151/151 the moment the test's own `goto_tile` anchor was
corrected. One false content_bug cost a rejected author batch and a seam slot.

### A map edit needs `torirsserver-cache`; naming a blocker from jm2/jl2

The converse holds as well: a MAP edit is not testable in this tree at all without
`make -C src torirsserver-cache`, which deletes and repacks `cache.osrs239` (the client reads map
squares from the cache; only the editor reads `.jl2` text), so "restore the 2004 placement" is never
the cheap option it looks like. The same decode names a BLOCKER without a client run: the square's
`*.jm2` line (`level lx lz: h.. [o..] f<flags>`, f1 = blocked, f4 = remove-roof, so f5 = blocked, no
roof) plus every `*.jl2` loc on it (ids through `configs/all.loc.compack`) -- that is how seam12
found Mourning's End II's "wall" was an unlit Door of Light (`mourning_door_2_16_west`), not
collision.

### Decoding a wall's `rot`

A WALL's `rot` names the EDGE of its own square it sits on (`docs/COLLISION_MAP.md` section 1,
`collision_map.c` `collision_test_wall`): shape 0 (straight wall, and every door) `0`=west `1`=north
`2`=east `3`=south edge, stamping the neighbour across that edge too; shape 2 (L-wall) is that edge
plus the next one clockwise (`0`=west+north, `1`=north+east, `2`=east+south, `3`=south+west); shapes
1 and 3 (corner posts) sit on the corner between them (`0`=NW, `1`=NE, `2`=SE, `3`=SW); shape 9 is a
diagonal that blocks the whole square. So `0 12 40: <id> 0 2` (a door id) is a door on the EAST edge
of local (12,40) -- a player on the tile east of it is outside, not inside.

## Trap 30. A `choose:` WHOSE ROW OPENS A SCREEN IS THE END OF THAT `chat.play` LIST.

Nothing in this pack closes the chatmenu on a `~openshop`/`if_openmain*` path, so the option rows
stay on screen UNDER the new interface. `chat.choose` reads that correctly now -- `ok`, with the
interface named (`...but shopmain (300) opened while the click was served`) -- but the next list
entry has no page to read and fails `expected kind=<x>, got options`. End the list at the `choose:`
and bind the screen with `t.shop.attach`.

And `refused -- stale reopen` now means what it says and only what it says: the server REPLAYED the
same menu (`chat.rs2`'s `~p_choice_open` loop, reached when the resumed slot lands outside 1..N),
printed nothing and opened nothing. It is no longer the answer a working click gets. A `chat.play`
that HANGS -- no timeout, and the run stops on the byte-identical page whatever `--timeout` you give
`run.py` -- is a client/engine freeze on that page transition, not a slow run.

Varying the timeout once is the test; after that, end the list one entry short of the freezing
transition and `t.blocked` at once rather than reading past it.

## Trap 31. AN `[opnpc2,<npc>]` BINDING REPLACES THE ENGINE'S WILDCARD, so a quest npc whose handler does not end in `@player_combat_start` CANNOT BE HIT BY ANYONE.

`skill_combat/combat.rs2:16-21` states the rule in the pack's own words, and `~npc_retaliate(0)`
alone is only the npc's half (`npc_queue(1,...)` -> `[ai_queue1,_] npc_setmode(opplayer2)`): it
tells the npc to come and hit you and says nothing about you swinging back. The swing LOOP needs the
jump too -- `[label,player_melee_attack]` ends in `p_opnpc(2)`, which re-enters the same binding
every attackrate. The ledger tell is exact: the Attack row presses first time and the TARGET'S
HEALTH BAR NEVER APPEARS (`hp no bar -> no bar`) while hitsplats land on the PLAYER.

Zero hits is a binding; small hits are defences -- Between a Rock's Avatar was rewritten as "immune
to melee" on that confusion, and it dies in one hit with a rune scimitar. The reproduction is ten
rows and twenty ticks (`::spawn <sym>` + `::setlevel` + a GOBLIN control pressed identically), never
the quest's own 100-row route.

### `.loc_find` does not exist as a secondary-writing find; Prying Times FIXED

Beside it, the other latent shape a content reviewer should know: `.loc_find` DOES NOT EXIST as a
secondary-writing find -- `[command,loc_find]` always writes the PRIMARY active loc, so a
`.loc_find` + `.loc_change`/`.loc_anim` pair aborts the script with "requires an active entity the
script does not have" and is dead code until something first reaches it (`flamtaer_temple.rs2` had
one; `quest_regicide/scripts/regicide_bombcraft.rs2:228,239` still does).

A second instance, Prying Times' `[opnpc2,sailing_charting_drink_crate_prying_times_effect_troll]`,
is FIXED (OSRS-Content 3b41349334, `pryingtimes_locs.rs2:199-201` now ends
`~npc_retaliate(0); @player_combat_start;`; sonnet-b26's run shows the troll's bar drain to 0 and
its drop) -- any other quest npc still `~npc_retaliate(0);` alone is a content bug to `t.blocked`,
never a driver seam.

### The sweep (seam20) and the AP half (seam21)

Since seam20 the whole pack is swept: `make -C src check-quest-contract-sweeps` (also run by
`check-quest-combat-contract`, so by `torirsserver-scripts`) fails on any name-specific
`[opnpc2]`/`[apnpc2]` that reaches `~npc_retaliate` without `@player_combat_start`/`_ap`; a
deliberate refusal needs a `COMBAT_START_EXEMPT` entry with its reason
(`tools/check_quest_combat_contract.py`). THE AP HALF (seam21):
`[apnpc2,_] @player_combat_start_ap;` claims every Attack made from range (bow, staff, reach
weapon), so a gate written only in `[opnpc2,X]` is a MELEE gate -- give X an `[apnpc2,X]` with the
same `if` ending `@player_combat_start_ap` (LostCity `grandtree_black_demon.rs2:1-13`); the sweep
fails on a gated `[opnpc2]` without that twin, and a melee-only refusal goes in `APNPC2_TWIN_EXEMPT`
with its reason.

## Trap 32. THE GUIDE IS THE SPEC

(a step the guide does with a SPELL or tool -- Telekinetic Grab -- done instead by walking up and
taking it, with the ::given runes never cast, is sent back even where `helper_coverage` grades the
other route DRIVEN; sampler sonnet-b27). **`python3 tools/quest_gate/helper_coverage.py <id>` must
read FULL, or every gap must be declared with `.rs2` evidence.** It grades each Quest Helper step
(the guide's `getPanels()` list) DRIVEN, CHEAT, CONTENT_GAP or UNMATCHED against your rows, cheats
and the content, and `gate.py` calls a would-be-green run RED on any CHEAT or UNMATCHED step and on
any CONTENT_GAP your file does not declare.

A test that `::goto`s past a door the guide has you open, `::give`s an item the guide has you
gather, or lets a debugproc do a leg is a TEST gap even when trap 16's `.rs2` reading passes it; a
leg the port only narrates in a `mes()` (Mourning's End II's Temple of Light) is a CONTENT gap --
declare it on its own line as `-- GUIDE-GAP: <guideStepVar> <reason citing file.rs2:line>` (e.g.
`-- GUIDE-GAP: doAllPuzzles narrated at mend2_shared.rs2:111`); `lint_quest.py` refuses a marker
whose citation does not resolve, and the coverage tool ignores it.

`--json` prints the per-step table. A real driving row wins over the content's own soft-skip prose,
and your `-- GUIDE-GAP:` marker is read before it too, so a step the content's comment calls
collapsed is DRIVEN when you drive it and a declared gap when you mark it; a Lua table field
(`SUS[n].npc`) resolves from `key = value` pairs anywhere in a constructor, several per line
included. A CHEAT's `goto_tile` evidence is the goto that lands CLOSEST to the step's tile.

### Sanctioned grind debugprocs

One of `docs/QUEST_SERVER_CHEATS.md` section A's SANCTIONED grind debugprocs (the list is read from
that doc's "GRIND fast-forward" bullet: `::twocats_growpotatoes`, `::mortton_repairtemple`,
`::misc_earnapproval`) grades its step DRIVEN only when a
`t.check`/`t.expect`/`t.msg.expect`/`t.var`/`t.inv`/`t.quest.stage` read of its effect follows
within 12 lines (and a named `t.check` row is PASS); without the read-back it is CHEAT.

### `stand_on_square` needs a GUIDE-GAP marker (seam10)

The driver's reach retry stands on a loc's own square with `::goto` ONLY for a call carrying
`{ stand_on_square = true }` (since seam10; without it the row fails `reach_failed`) -- the player
never walked there, so walk a real route (a door, a crossing) first; when the square truly is the
only one that serves the loc, put `-- GUIDE-GAP: <step> <reason>` citing the `.rs2` line or the map
square (`m41_53.jm2`) within 8 lines ABOVE the opted-in call, which grades the step a declared
CONTENT_GAP (gate-accepted).

A bare opt-in, or a `stood on with ::goto` row with no opt-in in the Lua, grades CHEAT; an opt-in
with no marker beside it and a stand-on no guide step claims are gate findings too. Two reaches that
used to need a stand-on are real since seam10 (collision_map.c): a STRAIGHT wall decoration whose
own square is floor-blocked is reached from the square in front of it (fishingcompo's garlicpipe
from 2638,3445), and a floor-decoration stepping stone with chasm on every side is reached from the
bank across one gap square (tearsofguthix's swamp_cave_steppingstone_a/b from 3204,9572 / 3221,9556)
-- neither takes the opt-in or a marker.

### Steps with no target: name the row after the step variable

A step with NO npc/loc target -- an `EmoteStep`, a `PortTaskStep`/`SailStep` (Prying Times), a
`DetailedQuestStep` a sanctioned grind cheat finishes -- can only be credited by the top of
`driven()`: a ledger row whose name equals or starts with the guide's own step VARIABLE, normalised
(`blowKissToBrand`, not `kissForBrand`; `get75Support` for the row that reads `misc_approval` back).
`named_cheat()` also needs the step name to be a substring of the debugproc's (`repairTemple` in
`mortton_repairtemple` matches; `get75Support` in `misc_earnapproval` does not), so name the
read-back row after the step variable, verbatim.

### Verified markers: BRANCH-IN, PARTNER, NOT-A-STEP, OBSOLETE, ANY-OF (seam18)

**A guide step that is not a gap gets a verified marker, not a GUIDE-GAP (seam18, 2026-09-26).** A
`-- GUIDE-GAP:` says the CONTENT lacks a leg; five other markers, each on its own comment line, say
the step needs no driving here, and each grades the step `EQUIVALENT` (a neutral class, so the
verdict can read FULL) only when `helper_coverage.py` checks its evidence:
`-- BRANCH-IN: <sibling test_id> <step> [reason]` -- a mutually exclusive guide branch driven by a
SIBLING test (misc courts Brand, misc_astrid courts Astrid): the sibling must be a committed, green
QUEUE row graded against the same Quest Helper file, and its own grading, with its BRANCH-IN markers
switched off so two siblings cannot vouch for each other, must read `<step>` DRIVEN;
`-- PARTNER: <step> ::<cheat> [reason]` -- a two-player step (the guide's text names another player)
done by a cheat in `docs/QUEST_SERVER_CHEATS.md`'s "Two-player partner affordances" table, which the
test calls and a PASS ledger row reports (`::blackarmgang_partner` for Shield of Arrav's
getWeaponStoreKey/tradeCertificateHalf); `-- NOT-A-STEP: <step> <reason>` -- ONLY a Quest Helper
plugin-state step: a `QuestSyncStep`, or a target-less `DetailedQuestStep` whose text asks you to
open the journal to sync the plugin's state (Clock Tower's `syncStep`); on anything else it is
refused; `-- OBSOLETE: <step> <reason>` -- a step the live game removed, or one naming an object
with no interaction; the reason must pin the wiki as
`https://oldschool.runescape.wiki/w/<Page>?oldid=<n>` or `...#<Section>` (a bare page URL is
refused: the page moves under it), e.g. Mourning's End I's talkToIslwyn
(`Mourning%27s_End_Part_I?oldid=15292327#Starting_the_quest`: "Talk to Eluned") and Fishing
Contest's getGarlic (`Fishing_Contest?oldid=15302643#Help_from_the_champion`: "pick up a piece off
the table"); `-- ANY-OF: <step> <driven step> <reason>` -- a step any of several ways satisfies,
done another way: `<driven step>` is a PASS action row of this run (exact, or
`<name>.<check>`/`<name>-<n>`) or a guide step graded DRIVEN, and the reason cites the `.rs2` line,
map square or pinned wiki page saying any way serves (Mourning's End I's cookNaphtha on any range:
`-- ANY-OF: cookNaphtha cookToxin ... skill_cooking/scripts/cooking.rs2:11`).

### Refused markers; grading a proof copy

A malformed, bare or uncited marker, or one whose evidence does not check out, is refused by
`lint_quest.py`, printed `UNVERIFIED` by the coverage tool and made a finding by `gate.py`; its step
grades as if the marker were not there (a declared CONTENT_GAP when its reason also cites a real
`.rs2` line, as a GUIDE-GAP would). Never write one to make a real content gap disappear: a leg the
port lacks is still a GUIDE-GAP. `helper_coverage.py <id> --lua <copy>` grades a proof copy of the
test against the id's QUEUE row, guide and ledger.

### What helper_coverage could not see (sonnet-b28): (a) open, (b) FIXED seam24, (c) FIXED seam27

TWO THINGS IT CANNOT SEE YET (sample sonnet-b28; (b) fixed by seam24). (a) A `stand_on_square`
crossing writes its `reach_failed:`/`stood on with ::goto` text into THAT call's own row detail (not
through `t.note`, whose text folds into the NEXT row), so grading the following row differently
never hides it: the marker above the call is the only thing that claims it. (b) FIXED (seam24): a
loc the quest's own stage gates make you cross more than once (Mountain Daughter's pole-vault and
plank rocks to the lake island, three trips: rows `poleVaultRocks2`, `plankRocks2`,
`returnToShore2`...) is claimed crossing by crossing -- the guide step's `stand_on()` still claims
the first row, and `Grader.claim_marked_crossings` claims every later one whose OWN
`stand_on_square = true` call (the call whose line or three lines above name the row, e.g.
`t.exec("poleVaultRocks2", ...)`) has a cited `-- GUIDE-GAP:` marker naming a step the guide DEFINES
on that same loc: a panel step (`poleVaultRocks`), or a ConditionalStep-only one no panel lists
(`plankRocksReturn`, `noPlankRocksReturn` on `mdaughter_flatstone2`).

`helper_coverage.py <id>` prints each as
`declared crossing: ledger row N ... GUIDE-GAP marker line L (<step>, cites <file:line>)`. So: give
EVERY crossing its own marker, name the row after the step plus a repeat number, and name the guide
step on that loc in the marker; a repeat under a bare opt-in, a marker naming another loc's step or
no guide step at all, or an uncited marker stays an `unclaimed stand-on` gate finding.

Never reroute the content to dodge a repeat. (c) FIXED (seam27): a guide step done by a SPELL ON A
GROUND OBJ (Spirits of the Elid's `telegrabKey`, guide target `elid_wooden_table`) is DRIVEN by its
own row, `telegrabKey.cast` -- the coverage tool's travel filter used to drop every row whose name
merely STARTED with `tele`. A step the test drives another way on the same target is not a content
gap: mark it `-- ANY-OF: telegrabKey telegrabKey.cast elid_house.rs2:117 ...` (verified, printed
`verified ANY-OF marker`), never a GUIDE-GAP.

## Trap 33. AN IF1 GRAPHIC BUTTON (`type=5` with a `buttontype`) IS NOT AN IF3 OP, and its CLOSE icon does not close.

`t.ui.invoke(widget, op)` with `op >= 1` routes the press down the IF3 numbered-op path
(`if_button_action_for_type` in `src/game/rs_minimenu_build.c`, `app_plugin_click_node` in
`src/plugin/torirs_plugin_bridge.u.c`), which an IF1 button never answers -- pass `op=0` for an
ordinary IF1 button (Mourning's End Part I's fractionalising still: tar, pressure and coal all moved
on `op=0`). A `buttontype=3` (`REVCONFIG_BUTTON_TYPE_CLOSE`) icon is the exception `op=0` does NOT
cover: its `if_click` answers `ok` every time and the interface never unmounts.

Close it with `t.key("escape")` (`src/app/app_hotkeys.c`:119 sets `app->host.close_modal_requested`,
the same flag `t.shop.close` uses) and grade the row on `t.ui.await_close`. `t.ui.invoke` is hollow
either way (trap 12): read the varp/varbit or the interface's presence back. Regicide uses the same
still. An IF1 cache interface with no onop and no varptriggers (`rd_combolock`,
`dwarf_rock_schematics_control`, `grim_piano`) sends every press to `[if_button,<if>:<com>]` and the
server owns its state, shown back through `if_settext`: drive it with `t.ui.invoke` and read the
component text (Sir Ren's lock, seam20).

`%if1..%if6` are `scope=temp` screen scratch since seam24 (every open screen refills them; Death's
Coffer's balance is `%death_coffer_balance`, and `~death_coffer_login_migrate` moves a pre-seam23
save's `%if1` balance once) -- never read one as a quest's state.
