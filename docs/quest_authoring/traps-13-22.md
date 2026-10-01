# Traps 13-22 (section 5)

Section 5 of the manual, traps 13-22.

## Trap 13. Finding an npc: use `*.spawn` rows, not just Quest Helper.

If `talk_to`/an npc lookup still answers `screen_position`/`no_row` at the WorldPoint tile,
`grep -rn --include='*.spawn' "<symbol>" OSRS-Content/osrs239-content/server/scripts/` -- each hit
is `symbol  x  z  level`, whitespace-separated, under an `==== NPC ====` header, one file per map
square. Search the WHOLE of `server/scripts` as above, not just `areas/`: 972 of the 984 `*.spawn`
files are `areas/world/configs`'s map squares but twelve are not, and a quest's own npcs are exactly
what lives in those (`quests/quest_demon/configs`, `minigames/...`). `"location unknown"` is never a
blocker -- the coordinate is in the tree.

## Trap 14. Name stage-check rows `quest.stage.<constant>`.

`quest.expect_complete()`'s own four rows satisfy section 7's ">=1 `quest.*` row" minimum shape
automatically, but `quest.expect_stage` does not name its own row, and a run that ends `t.blocked`
before hand-in never reaches `expect_complete` -- then only a stage row you named yourself counts.
Name it `quest.stage.started`, never `quest_started` or `cook.started`: the gate matches the literal
`quest.` prefix, not the intent.

## Trap 15. Never pad the shot count.

A shot exists because `t.exec`/`t.check` fired after a click that changed something -- never because
the file was short of section 7's row/PNG minimum (8 rows / 4 PNGs for a green run, 4 / 2 for one
that ends `BLOCKED`). Two byte-identical shots fail the gate (trap 4) whether or not they were
padding, but reaching the floor by repeating a no-op read is a rejection on its own.

## Trap 16. Never cheat the quest's own work.

An item, kill, craft, search or fetch the quest's own `.rs2` makes you do must be driven through
clicks (`click_obj`, `use_on`, a real fight, `talk_to` + `chat.play`) --
`::give`/`::kill`/`::setvar` are for `setup` and for prerequisites Quest Helper lists as
brought-along items (section 6's `canBeObtainedDuringQuest()` rule), never for the quest's own
deliverable. The reviewer reads your file against the `.rs2` and rejects a hand-in it can show was
cheated.

## Trap 17. Resolve every `-- CHECK` before the first run, not after.

`lint_quest.py` without `--allow-check` refuses a file that still has one; `--allow-check` is only
for the scaffold's freshly generated output, never for something handed to review. On an npc whose
branches hang off several interdependent runtime guards (Throne of Miscellania's Astrid: `toldking`,
`partner_multivar`, the affection state machine) the scaffold's `chat.play` lists and `choose:`
texts are near-useless -- its static guess takes whichever guarded branch it assumed false -- so
rebuild every such list from the `.rs2` branch by branch before the first run.

How common this is: `new_quest.py`'s static branch-guard resolver, on a guard it "assumed false, not
entered", usually captures a plausible WRONG branch -- the first-meeting or fallback text -- for
every later talk (sonnet-b29 found it on eleven npcs across idesofmilk and onesmallfavour, the
Duke's scaffolded as the unrelated Rune Mysteries tree). Rebuild every list from the branch the
stage you read actually reaches.

Its `setup` list can also DROP an item the guide's `getItemRequirements()` declares (onesmallfavour
lost `hammer`): diff the setup against that list before the first run.

## Trap 18. A branch almost always opens with the PLAYER's line, not the npc's.

An `[opnpc1,...]` handler in these ports opens `~chatplayer_anim`/`~chatplayer_anim2` first
(`brother_omad.rs2`'s `[label,omad_whats_wrong]`, `doctor_orbon.rs2`, `councillor_halgrive.rs2` all
do) -- `chat.play` grades the page that is up and never skips ahead, so a list starting `"npc:..."`
dies on page 1 with `expected kind=npc, got player`, and the red player name in that shot is the
client being RIGHT. Read the handler's branch in its `.rs2` and spell every page in the order it
actually opens; the generator emits the list from the script itself now -- verify it, do not rewrite
it from memory.

## Trap 19. `screen_position` now names a reason after the colon.

A `not_found` reason one tile from an npc used to be the multinpc id bug -- a `multinpc1=` def's
live entity carries a different `npc_id` than the symbol's (2,458 defs in this cache), fixed in
`pointer.lua` -- if the same reason recurs after that fix, it is a real seam: `t.blocked` it, naming
the npc symbol and the tile, not "stand somewhere else".

### The CONTENT shape: a spawn row carries the BASE symbol

The second shape of the same bug is a CONTENT bug and is written up here because three tier-1 quests
were blocked on it: **a spawn row carries the multinpc's BASE symbol, so an
`[opnpc1,<child_symbol>]` trigger is dead code.** The op lookup is keyed on the spawned symbol only.
Unlike locs (trap 20), that is not a gap to be closed in the engine and will not be closed:
`[reldo]` is `multivarbit=twocats_reldo` with `multinpc1=reldo_normal`/`multinpc2=reldo_withbook`,
so a child-first lookup would move script OWNERSHIP mid-quest, the instant A Tail of Two Cats writes
that varbit; `[contact_osman_multi]` resolves to `osman` for `bcs` 0-96 and to `-1` (hidden) at 97,
so child-first deletes Contact!'s own Osman leg outright.

### Why the engine will not do child-then-base (measured 2026-09-21)

A child-then-base npc ladder was written and MEASURED on 2026-09-21 and reverted for exactly this:
1,011 spawned records carry a transform table, 39+ resolve to a child bound to a DIFFERENT script
body, and Mourning's End Part I fell from 72 passing rows to 16 on one of them
(`mourning_arianwyn -> mourning_arianwyn_vis`, Song of the Elves) with a second dispatch defect
still unexplained underneath (`build/quest_gate/mourningsendparti`, same content, same Lua, HEAD
binary vs patched).

The fix is the base symbol's own trigger checking the quest FIRST and handing off with an early
return (`reldo.rs2`, `holgart.rs2`, and since seam24 Captain Bleemadge:
`gnome_glider.rs2 [opnpc1,pilot_white_wolf]` hands One Small Favour's 75..86 and 190 to
`osf_bleemadge_talk`). The engine kept only `ToriRSServer_NpcResolveTransform`, used to read which
record the client built its MENU from -- reading a verb is safe where running a stranger's script is
not.

### The fix idiom: the spawn row's owner hands off

So the fix is always in the script that owns the SPAWN ROW, and the pack has one idiom for it: the
owner checks the guest quest's window FIRST, with an early return, and calls the guest's
`[proc,...]`/`[label,...]` (`gertrude.rs2` ->
`~ratcatch_gertrude_talk`/`~twocats_gertrude_after_bob`, `holgart.rs2` ->
`~slugmenace_holgart_talk`, `dragonslayer2.rs2` -> `@twocats_talk_to_sphinx`). Landed 2026-09-21 for
the three that were blocked: `reldo.rs2` now hands `%twocats_quest` 25..30 to
`[proc,twocats_reldo_talk]` and `%giantdwarf_quest = ^gdwarf_imcando_asked` to `~gdwarf_reldo_talk`;
`contact_osman.rs2` hands `%contact < ^contact_met_maisa` back to `osman.rs2`'s
`[label,osman_talk]`; `slugmenace_pages.rs2`'s `[opnpc1/opnpc3,slug2_holgart_jeb]` hands
`%slug2_npc_track1 = 0` to `holgart.rs2`'s `[label,holgartplatform_talk]`.

Evidence: `build/quest_gate/seam_mn_content1/ledger.tsv`, 33 rows, each fix beside the quest it must
not break. **When you find a new one, target and verify against the BASE symbol, and report it as a
content seam naming both scripts** -- never `t.blocked` it as "the npc side has no child-then-base
fallback".

## Trap 20. The same is true of a LOC, and its detail now names the symbol.

A loc target is resolved by three rules in order, because the scenery pool stores the id the MAP or
a zone packet named while `multilocN=` defs (4,675 in this pack) draw as a child: `exact` (the scene
holds your symbol), `base` (you named a child, the map placed the wrapper -- the Wizards' Tower
altar), `multiloc` (you named the wrapper, a `loc_change` put a child there -- The Restless Ghost's
`openghostcoffin`, which nothing ever places).

A detail reading `click_loc <sym> -> <other sym> (base|multiloc)` is the resolve WORKING, not a
mis-click. `screen_position: no loc <id> (<symbol>)` after all three is a real seam -- `t.blocked`
it. Doors in this pack are mostly not multilocs but `param=next_loc_stage` PAIRS, so
`click_loc("<door>")` on an already-open door is `not_found` by design: name the `_open` half to
close it again. Press what the SCENE names, the wrapper: the server now looks a multiloc's trigger
up on the varbit-resolved child and then on the base, so a `[oploc1,<wrapper>]`/`[oplocu,<wrapper>]`
script is reached either way (2026-09-20 -- before it, 274 wrapper-bound triggers in this pack were
dead code answering `Nothing interesting happens.`, the same sentence content's own `^dm_default`
prints, which is why it read as a content bug from every quest file).

A plain (non-multiloc) symbol can also be PLACED more than once in one scene --
`grim_witch_house_door` sits on two ground-floor tiles of `m45_54` (2902,3467 and 2902,3474):
`click_loc` resolves the NEAREST copy, so one call opens only the nearer door, and a second call
from past it opens the next (`ball.lua`). Decode the `.jl2` rows for the symbol before assuming one
click reaches the room you want.

## Trap 21. `covered` does not mean something is in front of it, and `ui.tab` answering `ok` does not mean the tab is painted.

The client's pick is a per-triangle containment test over the DRAWN model with no depth test in it
(`ToriDraw_ProjectedModelMouseHitTest`), so `covered` means the pressed pixel is not on the target's
own triangles -- which is why rotating the camera never helped: the eye orbits the player and a
rotation carries the pixel with the target. The driver hunts for a pixel once every pose has
answered `covered` -- at the poses it framed, ranked by how many candidate pixels are inside the
world viewport at all, sharing one probe budget -- and the detail then names the hunt
(`pose 5 (reach -1): none of 83 pixels hittested around the projected 382,250 holds it`) and lists
the rows the menu DID offer, which is usually the whole diagnosis.

A hunted press that WORKS says so in the next row
(`click_minimenu: hunted pose 1 (reach 99) -- hovered -16,-64 off the projected 382,253`), so an
`ok` that needed the search can be told from one that landed first time -- a Rockslide in Ardougne
means you are standing in a scene that has not rebuilt (section 2's teleport bullet), not that
something is in the way.

### `ui.tab` answering `ok` does not mean the tab is painted

Same shape one layer down: `t.ui.tab("inventory")` is a BUTTON PRESS that returns as soon as the
click is taken, and the sidebar's own CS2 paints the cells a frame later -- `inv_op`/`equip`/`drop`
wait for them now, nothing else does.

### `press` has no hunt; Sheep Herder's map, pen and prod

> CONFLICT (kept both): the sentence below that LostCity's `wanderrange=3`/`timer=25` for these
> sheep "is NOT in the port yet" is superseded: NPC wander parity LANDED in seam 15 (`gaps-combat.md`,
> `::passive` gap) and seam pass 21 (a) (2026-09-28, `seam-facts.md`). The later date wins: the sheep
> wander the LostCity way.

`t.player.press` is the one click verb with NO such hunt: `QD.player.press` (`pointer.lua`) presses
once through `click_minimenu` and watches the npc pool, with none of the covered-hunt or
walk-to-another-side retry `click_loc`/`use_on` have, so a numbered op that answers from one tile
and not another reports a plain result word, never `covered`. It was read as a pixel-or-navmesh seam
until 2026-09-22 and IS NOT ONE: with the overhead reading landed, Sheep Herder's 168 presses are 0
`timeout`, 117 `ok` (112 of them carrying the npc's own word) and 51 `refused` -- every press that
used to read "the npc went unresponsive" had in fact run, and the obstacle was the MAP refusing the
push (`m40_52.jm2` marks 2598,3345 and 2599,3344 `f1`).

Read the fourth outcome above and try another side; do not re-issue the same press from the same
tile and do not call it content's fault. And before blaming the map, read the trigger's zone against
the map's walls: Sheep Herder's `sheepherder_pen_gate` (2592-2594,3360-3363) is OUTSIDE the pen's
WEST wall, where gate locs 166/167 sit, so a herd pushed due west from the spawn meets the pen's
EAST fence (x=2609) by design -- route round the pen, south of its z=3351 wall, and do not report
the east fence as a seam (reverted by sampler sonnet-b12).

And a pushing loop (Sheep Herder) that forces an immediate `t.npc.tiles` resync on EVERY failed
`t.player.press` starves the slower per-tile fallbacks -- a one-shot perpendicular nudge, a
`stuck_count` retreat -- which were the only escape before `press` took `{ slot = n }` (seam13) -- a
herd loop now presses the slot it stands behind; `sheepherder.lua` gives the nudge streak 1 and the
resync streak 2. Sheep Herder's pocket is the 2004 map's (LostCity m40_52.jm2 boulders loc 444/445 =
port 10790/10791, the same spawn rows) and the prod is one CARDINAL tile with no fallback, as in
LostCity: push east along z=3343-3346 past x=2612, north along x>=2613 above z=3351, then west to
the gate.

LostCity's `wanderrange=3`/`timer=25` for these sheep is NOT in the port yet (held: the port's
wander walks an npc outside its radius home every tick, `torirs_server_world.c` go-home branch,
where LostCity `Npc.ts` wanderMode only rolls 1/8 a tick; the two land together).

### `the world is not picking` (seam11); other floors; camouflaged locs

`the world is not picking` / `the world stopped picking mid-search` (a run of probes no frame ever
hittested) were, in the Temple of Light, an OPEN MINIMENU -- a covered press leaves it up and it
owns the whole canvas until the pointer leaves it +10px -- and pixels under the chatbox/orbs, where
the frame resets the pickset (seam11). The hunt now closes the menu first (`QD.drive._dismiss_menu`)
and skips a pixel `api_drive.world_gate` refuses for free, so the account reads
`(N off-viewport, M under UI (ui 162:60), K never hittested)`; a stale message that survives carries
`(gate at x,y: why)` naming what refused.

A LOC OR STACK ON ANOTHER FLOOR IS NEVER PICKABLE (`torirs_pick.c` keeps only the player's plane),
so `click_loc`/`use_on`/`walk_near`/the reach retry now choose the copy on the player's own plane
when one exists; a covered row whose only copy is on another floor means YOU are on the wrong floor
(`t.world.tile()` level), not a camera problem -- the Temple's wall-support crossing is
`climb_unqualified` in `ladders.loc` and lands 1901,4612,**2**, while the blue chest is 1917,4613,1,
so record the landed tile and do not blame the pick.

A `t.blocked` for a pick must follow a LIVE failed press in the same run, never a remembered one
(reverted by sampler sonnet-b17). `yaw N framed nothing in 5 poses` (`not_visible`) is the other
shape: no pose projects the target on screen at all. A camouflaged loc (a rock-face secret door)
does this even from the right approach tile -- check its model/size in `all.loc` before blaming the
tile.

## Trap 22. A `~mesbox`/`~chatnpc_specific`-style page SUSPENDS the calling script, not decorates it.

Everything the branch does after opening one -- a plain `mes()` line, an `inv_add`, a varp write --
runs only once the page is DISMISSED, so a `t.var.await_server`/`t.inv.count` taken right after the
click reads the OLD state and looks exactly like content that never ran; dismiss first
(`chat.continue_`/`chat.play`), then assert. A page whose script aborts after mounting (a command
that needs an ACTIVE npc, called with nothing bound from an
`[oploc1,...]`/`[opheld*,...]`/`[queue,...]`/`[proc,...]`) still shows and arms its resume button,
then the script is already dead: `chat.continue_` answers `ok` on the client's own ack and every
later `continue_` answers `a resume is already outstanding` -- a hung page, not a wrong one, and the
bug is a whole verb upstream of the row that prints it.

### Which chat procs need an npc (seam23, seam24)

`~chatnpc_specific`/`~chatnpc_specific_anim` need NO npc since seam23 (the port's
`facesquare(npc_coord)` pair is gone, as in LostCity `chat.rs2:393-401`), so they are the named-chat
form for any trigger. A `[softtimer]` holds no protected player, so a page opened from one can never
resume either: queue the dialogue (`queue(x,0,0)` -> `[queue,x]`). The plain
`~chatnpc`/`~chatnpc_anim` (`interface_chat/scripts/chat.rs2:59`) still need an npc: they read
`npc_type`/`npc_name`/`npc_coord` and hang the identical way from a bare
`[oploc*]`/`[opheld*]`/`[queue]`/`[proc]` (Mountain Daughter's Shining Pool until seam23).

The fix for an npc-less trigger is `npc_find(coord, <speaker>, 12, 0)` first (`rd_table_says`,
seam20), or `~chatnpc_specific_anim("<Name>", <npc>, ...)`, or `~mesbox` when the game shows the
line with no speaker (the wiki's `{{tbox}}`: Asleif's spirit);
`make -C src check-chatnpc-without-npc` (in `torirsserver-scripts`) has an EMPTY KNOWN_OPEN since
seam24, so the scripts build fails on any new npc line reached from a loc/held/queue/proc trigger
without a named speaker.

The ten it used to list are fixed with `~chatnpc_specific(_anim)`: Zogre Flesh Eaters' Sithik (a bed
LOC; `talkToSith` is `click_loc('zogre_sithik_bed_entity', 1)` then `npc:who gave you permission`,
`player:Zavistic Rarve said`, `npc:why would he send you to me`,
`choose:Do you mind if I look around?`, `player:`, `npc:actually yes I do mind`,
`player:going to have a look around anyway` -> `zogre = 4`; papyrus on the bed is
`use_on('papyrus', t.player.by_symbol('loc','zogre_sithik_bed_entity'))`), Path of Glouphrie's
spirit tree, the Fremennik Trials ladder, the ToB orator loc and drift-net Annette.

A loc's CLIENT id past roughly 6887 is its `configs/all.loc` header index + 1 -- find a placement
from the client's `no loc <id>` detail or the compack, never an awk count.
