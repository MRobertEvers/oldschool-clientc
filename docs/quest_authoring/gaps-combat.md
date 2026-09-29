# Section 8 gaps: fights, vars, shops, budgets and cheats

## What the ledger, the gate and the linter will not tell you

*Origin: section 8 ("Gaps reported by authors").*

What the ledger, the gate and the linter will not tell you. The "grade this row `true` and let the
following `t.blocked()` carry the verdict" shape that `makinghistory.lua` and `pryingtimes.lua` use
is a RULE, not their private habit: a real FAIL immediately before a BLOCKED row is the rejected
shape (trap 15), so the last row before `t.blocked()` is a RECORDING row --
`t.check(name, true, "<what was read>")` -- and the reading lives in its detail.

`gate.py`'s `shooting_row_names` collects row NAMES from the whole source, not from the branch that
ran, so reusing one name across an `if`/`else` where one arm uses `t.exec`/`t.check` (auto-shoots)
and the other `t.expect` (never shoots) is RED for "no shot recorded" on a row that never shot --
give each arm its own name. `lint_quest.py` checks a `t.var.varp`/`varbit` symbol against the UNION
of `all.varp.compack` and `all.varbit.compack`, so the documented "call the broken name right before
`t.blocked()` to print live proof" is lint-refused for a name missing from BOTH; `t.quest.stage()`
is not in lint's verb list and drives the identical resolver.

### Unaddressable varps read through the server; untransmitted base varps; random coords

The never-arriving-varp bullet above has a second root cause, and it needs no workaround any more: a
varp whose id the client's array cannot address at all (`pack/varp.alloc` 6262 against an
`all.varp.compack` topping out at 5704) is read through the server's own copy by
`t.quest.stage()`/`expect_complete()`, which print `[server content]`. The journal cross-check above
remains the answer for the DIFFERENT shape, a varbit whose BASE varp is never transmitted
(`[makinghistory]`, `[kr_varp1]`): that reads a confident `0`, not `not_found`, so nothing can tell
it from a quest that has not progressed.

A varp holding a coord the SERVER rolled at RANDOM is a third case the `^*_coord` decode does not
cover: the client cannot read which of the documented tiles was picked
(`%fluffs_crate = random(6)`), so loop the candidates and let the one that answers be the evidence.

### Three world facts: attack's presence precheck, private drops, prayer-bypass rolls

Finally, three world facts each cost a run: `t.player.attack`'s own presence precheck is
`npc.nearest(sym, 0)` with no caller-facing radius and answers `no_row` for an npc the same click
just `npc_add`ed, so pair it with `t.npc.await_present`; a private ground drop (`obj_add_private`)
lags the zone packet exactly like a backpack grant, so poll `t.world.obj_near` before `click_obj`;
and an npc's attack script can carry a prayer-bypass branch keyed on the player's MAGIC level
independent of melee defence (`roving_mossgiant` killed a 99 hitpoints / 99 defence / 1 magic
character mid-fight), so raise every level that npc's own `.rs2` rolls against, not just the melee
three.

### Retry loops: record the OUTCOME row only

A retry loop that writes a `t.step` row per ATTEMPT fails the every-row-PASS rule over attempts that
were only a walk: `click_loc`/`use_on` routinely closes the distance on one tick and picks or packs
on the next (measured on Karamja's banana trees and Luthas's export crate, ten items in 25 and 14
attempts), and an attempt whose own non-effect is a walk is not a click failure. Record the loop's
OUTCOME row only -- the count reached against the count needed -- which is trap 15's rule read
forwards.

### The `quest.scroll` photograph can be taken before the scroll mounts

And `quest.expect_complete()`'s `quest.scroll` photograph is taken BEFORE `scroll.title()`'s own
eight-tick mount await (`quest.lua`; `read.lua`'s `scroll.title`), so a completion whose scroll
mounts a tick late publishes a frame with no scroll in it (`hunt/102-quest.scroll.png` against
`druid/63-quest.scroll.png`) while `gate.py` only checks that the FILE exists: settle before
`expect_complete()`, and open that shot yourself before calling a run green.

### `quest.journal` can time out after a real completion: hand-roll the rows

And `quest.expect_complete()`'s own `quest.journal` row can time out AFTER a real completion on a
quest whose `t.ui.journal_open(display)` has already answered `ok` five times in the same run for
the stage rows: that row opens a FRESH journal immediately after its own `scroll.close()`, with no
settle between the two, and on Roving Elves it read
`row 72 clicked, but no painted journal within 20 ticks` on three separate attempts -- the bare
verb, a hand-rolled replica with a `t.settle()` added, and again with `t.ticks(3)` between the close
and the press.

Deterministic, not flaky, and nothing a quest file can reach fixes it. Section 7's minimum shape is
the intended way out, and this is the case it is FOR -- `quest.journal` is not itself required -- so
drive the completion through the rows that DO land, each written by hand with `t.check`:
`quest.varp_complete` (a `t.quest.stage()` read against `constants.complete`), `quest.scroll_title`,
`quest.points`, plus the reward rows. Say in the details which channel answered and why
`quest.journal` is absent. Never ship a row you have measured to time out.

### `sscompile` contention; outcome rows for real sentences; `pack/varp.alloc` lints clean (2026-09-26)

Last, a wall-clock fact with no signal attached: `run.py` compiles all ~38k scripts, and several
sessions share this worktree, so a `sscompile` that normally takes a minute can take five or ten
while other batches build (`ps` shows the concurrent runs). That is contention, not a stall -- look
at `ps` before you kill it and re-run. The retry-loop rule covers an attempt that answers a REAL
server sentence too: a maple depleting and regrowing mid-loop answers "Nothing interesting happens."
on an attempt that is not a failure (`misc.lua`'s chop loop) -- outcome row only.

And `lint_quest.py`'s symbol check now also reads `pack/varp.alloc` (2026-09-26), so a var content
allocated above the cache ids (`%twocats_locator_found`, 7166) lints clean: grade a step on the var
that says it HAPPENED, never swap to a cache-native one that only proves a press -- A Tail of Two
Cats was sent back for three locate rows reading `direction 1 -> 4` while the amulet never lit and a
debugproc teleport did the finding.

## `t.player.attack`'s settle can read `timeout` on a fight that is engaged

*Origin: section 8 ("Gaps reported by authors").*

`t.player.attack`'s own settle can read `timeout` on a fight that is genuinely engaged and
eventually won. Its deadline waits for a hitsplat, a health-bar move or the npc leaving the pool
(`combat.lua`), and none of those has to arrive inside any practical window at low accuracy --
measured at 15, 25 and 50 ticks, attack 40 then 70. The press still happened: the verb stamps
`QD._combat_last` PAST the timeout return, so the engagement is real and
`npc.await_dead`/`await_dead_engaged` is what grades the kill.

Grade the attack row on the press being accepted (`ok`-or-`timeout`, with the verb's own detail --
it names the row text pressed and the hp it read), and put the outcome in `npc.await_dead`'s row.
Raising `ticks` is not the fix and neither is more accuracy.

## `use_item_on_item` order: `[opheldu,item_a]`; `last_item` vs `last_useitem`; `sscompile` argument counts (seam20)

*Origin: section 8 ("Gaps reported by authors").*

`use_item_on_item(item_a, item_b)` arms `item_a` and clicks `item_b`, so the trigger that fires is
`[opheldu,item_a]` -- and content declares only ONE direction of most pairs. Current Affairs'
`use_item_on_item("current_affairs_form", "charcoal")` answered `Nothing interesting happens.` for a
whole run because no `[opheldu,charcoal]` exists;
`use_item_on_item("charcoal", "current_affairs_form")` is the same physical act and lands.

Grep the pack for `[opheldu,` on BOTH names before picking the order, and expect the ingredient to
be the armed half and the target to be the clicked one (Murder Mystery's print comparison is the
reverse of its own flour steps for exactly this reason, and says so in a comment). A CATEGORY-bound
handler (`[opheldu,_sacred_oil]`) is reached from either order, but inside it `last_item` is always
the item the script is bound to and `last_useitem` the other one (`opheldu_orient`,
`torirs_server_scripts.c`); a handler that tests `last_item` for the partner, or a sibling category
handler whose `case default` answers instead of `trigger_decline`, reads as
`Nothing interesting happens.` in both orders -- a content seam, sourced from that invariant
(Mort'ton's pyre logs, seam7).

In a plain `[opheldN]` trigger the subject is `last_item`; `last_useitem` is only the use-on half
and reads -1 there (LostCity engine.rs2:151) -- `trail_hotcold.rs2` answered "Nothing interesting
happens." from the master device over it (fixed seam20; `trail_read.rs2`'s clue Read/Check-steps
still has it). And `sscompile` now counts an argument that is one fixed-return command (`coord`,
`npc_coord`, `movecoord(...)`) exactly, so a short call like `obj_add_private(npc_coord, x, 1, 100)`
is refused at compile time (`'<cmd>' takes N argument(s), M given`) instead of underflowing the VM
stack at run time (seam20).

## A fight that lands exactly ONE blow (engine seam, fixed 2026-09-21)

*Origin: section 8 ("Gaps reported by authors").*

A fight that lands exactly ONE blow and then stands still was an engine seam, fixed 2026-09-21, and
should stop being written into quest files as a driver limitation. `p_opnpc(2)` -- the call
content's own `[label,player_melee_attack]` uses to arm every swing after the first -- read the
npc's menu verb through an accessor that gates the whole record on it having a NAME, and every
multinpc shell in this cache is nameless (2,458 of them), so the re-arm was dropped in silence: no
refusal, no message, no log line, the npc still swinging back.

`npc_menu_verb` now resolves the verb child-then-base through the ungated row. If you see the shape
again, run with `TORIRSSERVER_COMBAT_TRACE=1` and read whether the interaction latch survives the
tick after the swing; a `target=-1 interact=kind0/op0` line on the tick after a hitsplat is the
re-arm being refused, not the script waiting.

## A crowded spawn: `await_dead_engaged` `ok` while the slot still fights

*Origin: section 8 ("Gaps reported by authors").*

A crowded spawn is a SECOND shape of "a kill is never proved by an empty pool": with three
`roving_mossgiant` rows a few tiles apart (`m39_153.spawn`) and the player alive the whole time,
`await_dead_engaged` still answered `ok` while `TORIRSSERVER_COMBAT_TRACE=1` showed the engaged slot
trading hits for ~40 more ticks. The player-death fence does not cover same-symbol neighbours --
corroborate an `ok` against the quest's own unambiguous signal (there, the seed's `obj_add_private`
drop) before crediting the kill.

## Shops: only for an item the quest's own script makes you BUY

*Origin: section 8 ("Gaps reported by authors").*

A SHOP is now reachable (`t.shop.*`), and what it is for is narrow: an item the quest's own script
makes you BUY. Shades of Mort'ton is the case that forced it -- `timberbeam` exists in exactly one
place in this content pack, `razmire_builders_merchants.inv`, the store Razmire opens as the quest's
own reward for five shades, and `~add_temple_resources` will not fire without it -- so `::give`ing
the materials would be cheating the quest's own work (trap 16).

An item a shop merely HAPPENS to sell, that Quest Helper lists as brought along, is still a setup
`::give`; COINS are brought along too, and a quest whose setup has no coins row cannot shop. Proved
end to end in `build/quest_gate/shop_mortton_proof2/ledger.tsv`: the same wall at the same stage
answers "To repair the temple you need to increase your material resource pool..." with an empty
pack and, after `shop.open`/three `shop.buy`s/`shop.close`, moves `temple_resources` 0 -> 792 and
`morttonquest` 50 -> 55.

## A varbit your quest writes is invisible unless its carrier varp is declared

*Origin: section 8 ("Gaps reported by authors").*

A VARBIT YOUR QUEST WRITES IS INVISIBLE UNLESS ITS CARRIER VARP IS DECLARED. A varbit is a bit range
inside a varp (`configs/all.varbit` names the `basevar`), and `ToriRSServer_WorldMarkVarp` returns
early on `!def->transmit` -- a varp whose per-quest `configs/*.varp` does not declare it defaults to
transmit OFF, so every varbit packed into it is written server-side and then seen by nobody and
saved nowhere. `t.var.server(<varbit>)` is the CLIENT's record of the last server-confirmed varp, so
it answers `ok / 0` rather than `not_found` and reads exactly like content that did not run: Throne
of Miscellania's 10,000gp reward read 0 from every channel, and `::setvar misc_coffers 4242`
answered ok and read back 0.

Declare the carrier `protect=no / transmit=yes / scope=perm` in the quest's own `configs/*.varp`
(`quest_pryingtimes` is the worked example), then re-drive the quest end to end -- see trap 28, the
declaration is what makes a wrong multinpc table visible too.
`make -C src check-quest-contract-sweeps` now fails when a quest's progress varp (or its varbit's
basevar) has no transmit=yes scope=perm declaration under `server/scripts` -- `configs/all.varp` is
symbol-only and is never loaded as a def (seam20 declared six).

## A run has about 2,000 server ticks; `[debugproc]` grind hooks; `max_frames`

*Origin: section 8 ("Gaps reported by authors").*

A RUN HAS ABOUT 2,000 SERVER TICKS IN IT BY DEFAULT: `TORIRS_MAX_FRAMES` is 60000 unless the quest
declares its own budget (`max_frames`, later in this item) and `net_transport_embed.c` pays 30
frames to one server tick under `TORIRS_EMBED_CLOCK_MS=20`. Mort'ton's 706-tick run is a third of
the budget. Any content wait longer than a few hundred ticks cannot be driven at all and needs a
harness hook -- and the right hook is a real timer PLUS a `[debugproc]` that calls the same advance
body once per step (A Tail of Two Cats' `::twocats_growpotatoes`), never a shortcut that writes the
end state, so the ledger row stays proof of the growth logic rather than proof of the cheat.

Mort'ton corroborates the ceiling from the other end: a real shade hunt (592 ticks) plus a
self-re-arming temple-wall-repair grind that alone measured 900-1100+ ticks for PARTIAL progress
totalled 1810 ticks in one run's own ledger -- most of the budget, with no fast-forward hook for the
wall. A quest whose grind re-arms itself needs that `[debugproc]` written before it can be a tier-1
row at all.

### `::mortton_repairtemple`: materials bind, not ticks

Mort'ton's is `::mortton_repairtemple` (2026-09-22): the wall grind is 150 separate repair actions
-- fifteen wall locs x ten `next_loc_stage` steps -- and no finite raise of the session budget makes
it deterministic, so the budget was deliberately NOT raised. It runs the real build body against a
real wall, so what binds now is MATERIALS, not ticks: 800 pool per 5 swamp paste + 1 limestone brick
+ 1 timber beam, ~9 pool spent per repair at Crafting 20 and ~31 at Crafting 99, and Razmire's store
stocks 5 of each -- buy across several restocks and call the hook after each load, and it resumes
from wherever the walls are. The hook needs no player position -- unlike the click grind it
replaces, it acts on the walls directly, so do not walk to the temple first.

### A quest may declare its own frame budget (`max_frames`)

A QUEST MAY DECLARE ITS OWN FRAME BUDGET as a `max_frames = <n>,` field beside `fixture = "..."` in
its table (`run.py` reads it by regex, applies it to `TORIRS_MAX_FRAMES` for that run only, and
scales the wall-clock `--timeout` by the same ratio; a `--script` copy carries it too). The ceiling
is `quest_list.MAX_FRAMES_CEILING` = 240000 (4x the default, ~8,000 ticks); `lint_quest.py` rejects
more and `run.py` asserts on it. Raise it only for a guide whose REAL legs are long (Sheep Herder's
four-sheep herd plus poison/incinerate/hand-in overran 60000), never to absorb a content wait --
that still needs the `[debugproc]` hook above.

## `::passive <npc_symbol>`; spawned npcs vanishing (UNCONFIRMED); npc wander parity (seam 15)

*Origin: section 8 ("Gaps reported by authors").*

`::passive <npc_symbol>` TAKES A WANDERING AGGRESSIVE NPC OUT OF A TEST'S WAY, and a quest fought on
a street full of them needs it. In a SINGLE-WAY area one npc that aggresses claims the player for
`TORIRSSERVER_SINGLEWAY_COMBAT_TICKS` past each swing, and every Attack on the thing you are
actually hunting is refused by the engine with "I'm already under attack." -- Shades of Mort'ton
landed two kills of five in twenty rounds with the chat pane eight lines of that sentence.

`::passive <sym>` holds a TYPE passive for the session (a type, not an npc: the pool is a window and
the setup line runs in Lumbridge), `::passive off <sym>` / `::passive off` restore, bare `::passive`
lists. A passive npc is still attackable, still talks, still takes damage, still dies and still
drops -- what it stops doing is starting fights and fighting back. `docs/QUEST_SERVER_CHEATS.md`
section F has the whole reading.

### UNCONFIRMED: a `::spawn`ed npc can leave the pool

UNCONFIRMED against engine source: a `::spawn`ed npc can leave the client's pool later in the same
run with no death -- Steve Beanie survived a 255-tile round trip, then vanished after a shorter one
whose only difference was a content `npc_add` nearby. Re-`::spawn` and re-resolve it before each
visit rather than trusting a handle from rows earlier.

### NPC wander parity landed (seam 15)

NPC WANDER PARITY LANDED (seam 15, LostCity `Npc.ts` wanderMode): there is no per-tick go-home any
more -- a pushed or prodded npc stays put until the 1-in-8 roll picks it a tile inside its box
around the spawn (or the 500-tick stuck teleport), and Sheep Herder's diseased sheep now bleat and
wander (`wanderrange=3 timer=25`, LostCity `quest_sheepherder.npc`). The RNG stream moved with it,
so wandering npcs (Lumbridge's Men, sheep, toads) stand on different tiles than any run before seam
15: a row that depended on where one happened to be (a chop count before the tree falls, a Man
within five tiles) has to find its subject, not assume it.

## `::complete` takes a DBROW name

*Origin: section 8 ("Gaps reported by authors").*

`::complete` TAKESA DBROW NAME, not the script directory's. Druidic Ritual is
`::complete quest_druidicritual` (`all.dbrow.compack`); `quest_druid` is the folder and answers
nothing -- a prerequisite cheat that silently does nothing leaves the whole downstream cascade
looking like a content bug (Heroes' Quest lost its entire Herblore leg to it).

## Feldip hunting ground: the damage comes from the wolves; the lent ogre bow is the reward

In Big Chompy Bird Hunting the chompy (level 6) is not what hurts. Wolves roam the swamp bubbles
where the toads are inflated. The committed run was at 10 of 60 hitpoints by `inflateToad.1` (shot
100), began the chompy fight at 11, and ate five lobsters (the `killChompy` detail). An earlier run
without food died after "You start plucking the chompy bird." The `player.died` row came several
rows after the damage and did not name the attacker. Carry food from setup and pass `opts.eat` to
every kill wait here, even a trivial one.

Rantz lends the player his `ogre_bow` (`talkToRantzForBow`), and the test wears it for the kill. The
completion does not add another bow, so `inv.expect_has("ogre_bow")` straight after
`expect_complete` fails. Run `t.player.unequip("ogre_bow")` first, then read the backpack.
