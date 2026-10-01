# Seam-pass facts (section 8)

Facts recorded by seam passes, from section 8. Each pass's items keep their original letters; a
citation like "seam pass 21 (d)" names the pass heading below and the letter inside it.

## Seam pass 8 (2026-09-22): three engine facts

*Origin: section 8 ("Gaps reported by authors").*

THREE ENGINE FACTS FROM SEAM PASS 8 (2026-09-22).

(a) The server now loads the pack's authored `server/scripts/**/configs/*.struct` blocks (ids from
`pack/struct.alloc`, 8000 and up; boot prints `N struct param rows`), and an authored row wins over
the cache's; before this `struct_param` on an authored struct answered every param's DEFAULT
(Mort'ton's sacred oil on logs gave an obj named `item`), so a product named `item` or a `null`
param on a run older than that was the engine, not the quest.

(b) `::setvar <varp>` is REFUSED on a carrier varp that packs varbits (`torirs_server_world.c`
~8089): stage each varbit by its own name (`sheepherder_sheep_a` .. `_d`).

(c) LANDED seam pass 9: after `npc_del` the script's bound npc stays READABLE for the rest of the
tick -- `npc_coord`, `npc_type`, `npc_name`, `npc_stat`/`npc_basestat` and `npc_var_get` answer the
removed npc's last state (`active_npc_readable`, `torirs_server_scripts.c`; the slot is
`pending_free` and cannot be reused until the end-of-tick reap), as LostCity's NPC_DEL leaves
`state.activeNpc` bound. So `npc_del; obj_add(npc_coord, ...)` drops the item where the npc stood
(Sheep Herder's bones, `diseased_sheep.rs2:213`; Monkey Madness's guard smokepuff).

WRITES to the removed npc (`npc_anim`, `npc_tele`, a second `npc_del`) still abort, and so does a
read after a `p_delay` that crosses the tick -- `npc_coord with no active npc` on a run older than
this is the engine, not the quest.

## Seam pass 16 (2026-09-25): two engine facts

*Origin: section 8 ("Gaps reported by authors").*

TWO ENGINE FACTS FROM SEAM PASS 16 (2026-09-25).

(a) ABOARD, `t.world.tile()` IS A DECK STAGING TILE hundreds of tiles off the map -- ask
`t.sail.state().hull_x/hull_z` whether a sea leg arrived; content binds
`[zone,<level>_<mx>_<mz>_<lx>_<lz>]` or `[mapzone,0_<mx>_<mz>]` at the sea tile and the engine
queues it on every rider when the HULL crosses in (`torirs_server_vessel.c` `vessel_update_zones`;
the ripple 2835,3418 is zone 0_44_53_16_24), so a sea leg is sailed, never telejumped.

(b) Server script has `if_setangle(component, xan, yan, zoom)` and
`if_setrotatespeed(component, xspeed, yspeed)` (torirs extensions 11114/11115, the rev-230
IF_SETANGLE/IF_SETROTATESPEED; angles 0..2047, zoom <= 0 keeps it) for a type-6 model component --
Between a Rock's schematic rotation can be built on them and read back with `t.ui.model_pose`. A new
server command is proved without a content edit through a scratch pack (a symlink mirror of the
content root plus one `zz_` script dir, `sscompile --src`, `TORIRSSERVER_SCRIPTS=<pack>`):
`build/seam_state/seam16/angle_scratch`.

### Seam pass 17 (2026-09-26): a pose's first projection is not where it settles

A POSE'S FIRST PROJECTION IS NOT WHERE IT SETTLES (seam pass 17, 2026-09-26).
`QD.drive._frame(target, pose, deadline)` answers the first `ok` screen_position, often read in the
SAME pump as the camera write -- the new angles against the old eye, or an orbit anchor still easing
after a walk -- up to 188 px from where the projection then holds (Lumbridge tree, `s17cp_probe1`),
so a row that grades the PIXEL depended on the world's random stream (two spawns 300 tiles away
turned `seam.press_pixel` red).

`_frame(target, pose, deadline, true)` returns the SETTLED pixel (player idle, yaw from the tile he
stopped on, 10 consecutive `ok` frames within 1 px); click_minimenu still uses the unsettled read
because settling every press regressed Mourning's End II's stairs, Sheep Herder's herd and a retry
press landing on the previous press's open menu (pointer.lua's banner over `_frame`). For
seam/regression runs: pass `run.py --no-publish` (otherwise a passing run copies its play/ artifacts
into OSRS-Content), and `TORIRS_SCRIPT_DIR=<copy of script/>` A/Bs a HEAD or instrumented driver
against the shared binary without touching the tree.

## Seam pass 18 (2026-09-26): four facts

*Origin: section 8 ("Gaps reported by authors").*

FOUR FACTS FROM SEAM PASS 18 (2026-09-26).

(a) ABOARD, `t.npc.nearest/tiles/await_present/await_gone/await_dead` and `t.world.obj_near` measure
radius and "nearest" from the rider's HULL-PROJECTED root tile (`torirs_plugin_drive_ui.c`
`drive_ui_search_origin` -> `app_wev_actor_root_fine`), not from `t.world.tile()` (still the
deck-frame tile): radius 0 aboard is no longer needed, and before this every radius>0 read aboard
missed deck npcs and `await_gone` passed HOLLOWLY. `t.world.loc_near` and the pointer's element-id
ranking still measure from the deck-frame tile.

(b) A FLOOR OBJ ON A VESSEL DECK (a rider's drop, the Drink troll's drink) lives in the deck VIEW's
world at deck-local tiles; the client now paints it at its own plane (a deck world draws every
plane, `world_cycle.c`) and the menu builds its Take row through the view (`rs_minimenu_world.c`,
`OPOBJ` at view base + local, like a deck loc) -- take it through the real menu with
`t.sail._press_deck_row({<obj names>}, "Take", 8, 16)` after `t.drive.camera(1024, 383, 1100)`.
`t.world.obj_near` and `t.player.drop`'s ground count read only the ROOT pool and answer `not_found`
/ `ground 0` for it (driver seam, open).

(c) SUPERSEDED by seam19: `run.py --script` now runs a quest file's `setup` table (see the
`--script` line above).

(d) A logout-then-login forces the next REBUILD even on the zone the client already holds
(`app_net.c` `App_NetSessionReset` sets `net_force_rebuild`), or the server waits forever for
MAP_BUILD_COMPLETE and the link watch drops the session after 15 s.

## Seam pass 21 (2026-09-28)

*Origin: section 8 ("Gaps reported by authors").*

FACTS FROM SEAM PASS 21 (2026-09-28).

(a) NPCs WANDER THE LOSTCITY WAY: a 1/8 roll per tick queues a tile inside spawn +- wanderrange and
the npc walks it that tick (Npc.ts:700-721), so a herd or follow loop re-reads the npc's LIVE tile
before each press, never the tile the last press reported (`sheepherder.lua` `resync(slot)` + its
post-walk drift check).

(b) AN NPC SCRIPT MAY NOT TALK UNTIL IT BINDS A PLAYER: a `~mesbox`/`~chatnpc`/`p_delay` from
`[ai_*]` before `npc_findhero`/`p_finduid` is a script error naming the trigger in client.log
(`bind one (npc_findhero/p_finduid) or queue() it on the player`) and the script ends there; bind
the hero and `queue()` the player's half (QUEST_PORTING_FIELD_GUIDE section 4 item 7;
`tools/check_npc_script_player_suspend.py`).

(c) A held-op script reads the clicked obj as `last_item`; `last_useitem` is the other half of a
use-on and -1 otherwise (clue Read/Check answered "Nothing interesting happens." until
`trail_read.rs2` was fixed).

(d) Cache dials the server now holds: an IF1 buttontype=5 SELECT button sends IF_BUTTON AND sets its
cs1 varp client-side (a `5,261,0` cs1 is VARP 261, not a clientscript) -- The Feud's safe (interface
330, `t.ui.invoke` 1,1,2,3,5,8 on `the_feud_safe:feud_over_model1`/`feud_overstate_model2..9` from
3373,2974,1); an IF3 arrow with a client `onop` still sends IF_BUTTON1 -- Tribal Totem's wheels are
`totemquest_combodoor_code1..4` (transmit=yes), never `%if1..%if4` (Slayer's task). A varbit on a
varp with no server `.varp` is transmit=no: read it with `t.quest._read_content(<base varp>)`.

(e) `map_findsquare` answers its ORIGIN when nothing passes, so an `npc_add` there lands on the
player -- widen lineofwalk -> lineofsight -> none; and `::spawn` puts the npc at player+1,+1 with no
collision check, so beside a bridge, wall or table it can land on a parapet where ANY npc looks
undrawn -- seam21's "spawned multinpc shell not drawn" was exactly that; a spawned shell draws and
follows its varbit live (seam22 `s22_mn_side2`), so stand on open ground with
`t.drive.camera(0, 383, 700)` before calling a spawn invisible.

(f) Relicym's balm brews only after Sithik's disease answer (`%thzfe_makecuredisease`) or quest
completion -- ask him option 3, never `::setvar`.

### Seam pass 22 (2026-09-28)

SEAM PASS 22 (2026-09-28):

(g) single-way follows LostCity's `%lastcombat`: only a SWING claims a player (8 ticks); an npc that
has only noticed him blocks nothing, and a second npc that reaches a claimed player gives him up at
its swing -- a latched-but-idle npc no longer refuses the player's own auto-retaliation (the
fresh_lumbridge giant spider stuck behind the castle fence made `_cheats` passive rows flaky).

(h) `%if1..%if6` (varps 261-266) are the cache's per-screen interface scratch: never store state
there. The Slayer task is `%slayer_count`/`%slayer_target` (var394/395) plus server-only
`%slayer_task_id`/`%slayer_stored_*`; a test that needs a task asks Vannaka (`slayer_master_3`,
3146,9914, combat 40; Turael is not spawned).

(i) The Lumbridge Swamp Dark hole refuses without a rope while `swamp_caves_roped_entrance` is 0
(wiki Dark hole): tearsofguthix, wanted and anothersliceofham give a rope; that bit is on the
untransmitted `goblin_caves`, so read it with `t.quest._read_content('goblin_caves')` bit 3.

(j) `t.player.inv_op` on a reward casket answers `timeout` though it opened (no chat, no mount):
grade the backpack.

(k) An npc death handler's whole player half (dialogue, teleport, instance free) goes on the
player's `queue()`, and `make -C src check-npc-script-player-suspend` (in `torirsserver-scripts`)
refuses an unbound one at 0 hits.

### Seam pass 23 (2026-09-28)

> CONFLICT (kept both): (m) below says "this engine accepts an unequip or a `::goto` inside another
> script's `p_delay`"; since seam24 (`gaps-dialogue.md`, "Since seam24: `player.delayed`") a held-item
> op, inventory button or interface-script button made during another script's `p_delay` is REFUSED,
> while a `::goto` is still NOT refused. The later date (seam24) wins for item and button presses.

SEAM PASS 23 (2026-09-28):

(l) an additive quest hub wired into a shared npc's `[opnpc1]` must `return(0)` (fall through)
whenever the player is not qualified for / not in that quest -- Path of Glouphrie's King Bolren hub
swallowed Tree Gnome Village for every fresh character.

(m) A click that answers `map_flag` can return while the loc script is still in `p_delay`: await its
effect (tile, varp, item) before the next action -- this engine accepts an unequip or a `::goto`
inside another script's `p_delay` (Tourist Trap mine exit: unequipping before the teleport set the
guards on the player and orphaned Al Shabim's next page). Likewise a `use_on` whose handler pages
after a `p_delay` needs `t.ticks(delay+1)` before `chat.drain`: an undrained page holds the script
and the server DROPS the next suspending click
(`dropping [<trigger>], which suspended while [<label>] waits` in client.log).

(n) A multinpc shell's trigger binds on the SHELL symbol the spawn row names
(`qip_watchtower_ogre_shaman_0N`), not a Quest Helper child name or a LostCity generic record.
Watchtower's finish is the wizard hand-over, each crystal on its pillar (yellow SW, grey SE, cyan
NW, magenta NE, 2545..2548,3113..3116,2) and the lever at 2543,3115,2 (the wizard's stage-11
hand-over is still open).

(o) Insulting the Tourist Trap's Mercenary Captain is LostCity's four-step ladder on
`%desertrescue_map_mechanisms` bits 0-1: only the 4th insult dumps you in the desert (and takes your
water); stage 0 does `if_close; p_delay(2)` before the guard's page, so await the page before a
second `chat.play`; a direct Attack before the duel is armed sets a mercenary on you.

(p) Death's Coffer's balance is the server-only perm `%death_coffer_balance` (`%if1` is only the
screen's copy): seed it with `::setvar death_coffer_balance <n>`; `::pohslayerlog` opens the POH
Slayer Kill Log page (`t.ui.journal_read()`: 'Current assignment: <task> (<n> remaining)'). A test
cannot press a slot of a non-backpack IF3 inventory (coffer side panel, deposit box).

(q) Uglug Nar's store opens only after `t.player.use_on('relicyms_balm<N>', <zogre_uglug_nar>)` (any
dose, 100/300/650/1000, sets the transmitted varbit `%thzfe_sold_balm`); before it, Trade answers an
npc page and `t.shop.open` times out.

(r) npc-vs-npc melee reads the wall on the shared edge, an npc's retaliation swing
(`npc_setmode(opplayer2)`) stamps the single-way claim on its first swing, and a second npc whose
mode swing reaches a player another holds gives him up.

(s) A wiki transcript's `{{tbox|...}}` line is a `~mesbox`, not a chathead (fetch the raw wikitext
with `action=raw`; curl meets the bot wall).

### Seam 28 (2026-09-29): every entity owns its random stream

SEAM28 (2026-09-29) -- EVERY ENTITY OWNS ITS RANDOM STREAM: an npc's wander, roam stagger and every
`random` in its `[ai_*]`/queue/timer scripts come off a stream seeded from its spawn tile, plane and
type (never its slot); a player's fights and scripted rolls off one seeded from its account name;
`srv->world_random` only for entity-less draws (`struct ToriRSServerRandomStream`, torirs_server.h;
LostCity uses Math.random, determinism is this engine's test affordance).

Adding or moving a spawn no longer moves any other npc or fight. So a wait budget or a single press
that passed on the old single stream was LUCK: loop a mining or fighting wait until the item or kill
lands, and hunt a wanderer (re-resolve, re-position, re-press) -- 'in view' (radius 3) can be
through a wall (atailoftwocats, ball, eadgar, crest, squire, fenkenstrain were rewritten that way).
Also seam28: `npc_find`/`npc_findexact`/`npc_findnext` honour the `.` operand (secondary bind,
LostCity NpcOps.ts); an npc caster is paced by `%npc_action_delay` (a varn,
skill_combat/configs/npc_combat.varn) + `npc_attackdelay`, never `npc_delay`, which made every
caster unhittable while it cast (the engine's attack clock gates only the combat `[ai_opplayer2]`
swing, not an AP mode); Hazeel Cult's Claus stands at 2540,9697 in the m39_151 kitchen (talk from
2542,9697); Plague City's garden Edmond is back after ANY exit from the sewer (`[mapzoneexit]`), so
a `::goto` out of the sewer finds him.

## Seam 24: a postquest shop clobbers the scroll; missing journal banners; script-held players keep moving

> CONFLICT (kept both): the last-but-one sentence here (seam24) says a gray viewport right after a
> goto can be a transient exactmove glide; seam pass 26 (b), FIXED seam27, says the client now drops
> such an exact move and "a gray viewport right after a goto is now a real bug to report". The later
> date (seam27) wins.

*Origin: section 8 ("Gaps reported by authors").*

A POSTQUEST BRANCH THAT OPENS A SECOND MAIN-SCREEN INTERFACE (a shop) right after
`~quest_complete_rewards` clobbers the reward scroll deterministically (the scroll mount is queued
until the dialogue closes, `questscroll.rs2`); `quest.scroll_title` then fails on every run -- a
`content_bug`, never a retry (Recruitment Drive's Sir Tiffy, fixed seam24). About 30 `*_journal.rs2`
done branches also lack the `<col=ff0000>QUEST COMPLETE!` banner `quest.journal`'s `complete` flag
reads (`ball_journal.rs2:67` is the pattern; Recruitment Drive's is one) -- a content_bug too.

A SCRIPT-HELD PLAYER KEEPS MOVING AFTER A `::goto`: the magic carpet (`~carpet_ride` under
`player_lock`, 52 ticks Shantay -> north Pollnivneach 3349,3003) re-hops from wherever a goto left
him, so a blind `t.ticks(20)` then `goto_tile` flew The Feud's rider back out of the 15-tile npc
pool and `feud_ali_the_barman` read 'not in the client's entity pool' (seam24; it was never an
interior bug). `t.await` the ride's landing tile and `t.player.idle()` before any goto;
`goto_tile`'s own `ok` cannot see a script still moving you.

A gray viewport and empty minimap right after a goto can be a same-tick exactmove glide off the
scene (transient), not an unbuilt scene. The Asp & Snake Bar sells beer at 2gp.

## Seam pass 24 (2026-09-28)

> CONFLICT (kept both): (f) below and seam pass 25 (e) say a multinpc shell's chat header reads
> 'Someone'; seam pass 26 (a) says the header is now the resolved child's name (SS_OP_NPC_NAME follows
> the transform). The later pass (26) wins.

*Origin: section 8 ("Gaps reported by authors").*

FACTS FROM SEAM PASS 24 (2026-09-28).

(a) A QUEST BOSS WITH NO `.npc` BLOCK SPAWNS AT `npc_default.npc`'s 10 HP: the loader reads a
block's stats, never the cache's `statN` (Slash Bash seam23, Brutus seam24 --
`quest_idesofmilk/configs/idesofmilk.npc` now carries the wiki infobox's 58 hp); a boss that dies to
one hit is a content_bug naming the missing block.

(b) A `click_loc` on a MULTILOC base runs the resolved CHILD's trigger first, so a portal can send
you somewhere else: Shadow of the Storm's `golem_portal` resolves to `golem_demon_door_always_open`
once The Golem is complete and lands every player in the demon lair (`golem_portal.rs2:205-212`,
open) -- read `t.world.tile()` after every portal/stairs press, never `::goto` past one. SotS's
Denath, Jennifer and Matthew are cast per player into the throne room on entry
(`shadowstorm_ritual.rs2 ~sots_throne_cast`).

(c) QUEST RUNS ARE DETERMINISTIC (VM rng seeded 0, frame-locked clock): re-running an unchanged file
replays the same rolls, so vary the work, not the run count, to sample.

(d) Ghosts Ahoy's flag colours can repeat a dye: setup must supply a second pot.

(e) A Lua search heavier than ~400k instructions must yield (`QD.ticks`) inside the work, or the
plugin raises 'instruction budget exhausted'.

(f) Family Crest's gem trader offers 'I'm in search of a man named Avan Fitzharmon.' only at
`crest_caleb_where` (-> `spoken_gem_trader`); Avan's first page after it is an options page and his
header reads 'Someone', as Captain Bleemadge's does.

(g) Watchtower's wizard sets stage 11 before the hand-over's first page when all six shamans are
down and all four crystals are held; the player keeps the crystals for the pillars.

(h) A wiki transcript whose `index.php?action=raw` 403s is fetchable as
`api.php?action=parse&oldid=<id>&prop=wikitext&format=json`.

### Seam pass 25 (2026-09-28)

FACTS FROM SEAM PASS 25 (2026-09-28).

(a) A QUEST'S `[oploc1,<door>]` NAME BINDING REPLACES the doors/doubledoors category trigger: its
gate must END by calling `~door_open_active` / `~open_double_door_left|right` /
`~door_selfstage_open` itself, or the door narrates and never moves (The Feud's mansion, Family
Crest's gold gate); the same holds for a shared `ladder` -- guard on `loc_coord` and fall through to
`~climb_ladder(1)`.

(b) A LostCity 2004 loc can put an op in a different slot than this cache: a ported `[oplocN]`
follows `all.loc`'s op slot (Gu'Tanoth rock-cake counter is op2 Steal-From; the ogre trader catches
you within 3 tiles by line of walk -- steal from the side he is not on, north 2513,3037 or south
2514,3035, and step away after a 'Grr!').

(c) A multiloc wrapper binding is dead when every child is bound (child-first dispatch): the Uzer
portal is `golem_demon_door_always_open` and lands on `^sots_throne` 2720,4912,2 once
`%agrith_quest >= 30` -- check arrival with `t.world.tile()`, never `::goto` past it.

(d) `make -C src check-quest-journal-banner` refuses a quest journal whose body lacks
`QUEST COMPLETE!` (done branch:
`append($text, ^journal_complete); append($text, "QUEST COMPLETE!")`); `grep -L journal_complete` is
not a banner sweep.

(e) Any xpreward lamp: `t.player.inv_op(<lamp>,1)`, `t.ui.await_open('xpreward')`, `t.ui.invoke` the
`xpreward:<skill>` then `xpreward:confirm` widget, `t.chat.expect_text('You rub')`,
`t.skill.expect_gain`. A multinpc shell's chat header still reads 'Someone' (npc_name of the
nameless base; a `.npc` `name=` is inert) until SS_OP_NPC_NAME resolves the transform.

(f) Traps: the Ghosts Ahoy wreck's Captain's Room is sealed by a closed `ahoy_harbour_door` at
3615,3543,1 -- a `::goto` inside leaves the plank/rocks 'I can't reach that!'; many copies of one
loc close together (`ahoy_rock_invisible` stepping stones) cannot be chained with `click_loc` (the
nearest copy is underfoot); a `~mesbox`-then-`npc_add` spawn is not in the pool while the mesbox is
up -- `chat.play` it, then `t.npc.await_present`; a single-way claim survives a loc teleport for a
few ticks ('I'm already under attack.' -- wait and press again); `t.ui.invoke`/`walk_to` answer bare
`ok`, so under `t.exec` capture the result and `t.check` the effect.

(g) Places: Witchaven ruin is open to all and lands at 2696,9683; pull the north-wall lever from
2722,9709; the famcrest room doors are selfstage (open once); goldrock2 at Mining 40 takes up to
~150 ticks per ore. Sithik's upstairs room is closed by a poordoor (2591,3105) -- open it before
`laddertop2`. Bridge-flagged jl2 rows are driven at level 0 (tanothjump1 from 2530,3024,0).

## Seam pass 26 (2026-09-28)

*Origin: section 8 ("Gaps reported by authors").*

FACTS FROM SEAM PASS 26 (2026-09-28).

(a) A MULTINPC SHELL'S `~chatnpc` HEADER IS NOW ITS RESOLVED CHILD'S NAME (SS_OP_NPC_NAME follows
the player's varbit/varp transform, as the client menu does; before, 318 shell speakers read
'Someone'): assert it with `t.chat.name()` against the Talk-to name; for a shell whose children
disagree (Avan: Man/Avan) the header changes with the stage.

(b) A GRAY VIEWPORT AND EMPTY MINIMAP RIGHT AFTER A FAR `::goto` means a client exact move (carpet
hop, rope swing, agility obstacle) was still in flight when the teleport landed -- not
`PMASK_EXACT_MOVE` (it is always 0 when `t.cheat` runs), and not always one frame (a press 52 ticks
later still projected off-scene). FIXED (seam27): the client now drops an exact move whose actor has
left the scene (rev-239 `class106.method3620`, `world_cycle.c` `World_ActorLeftSceneReset`, pinned
by `test_exact_move_across_far_teleport` in `make -C src test-world`), so no wait is needed after a
rope swing or carpet before a far goto, and a gray viewport right after a goto is now a real bug to
report.

(c) A JUMP OR TELEPORT LOC (`p_teleport(loc_coord)`) settles on its chat line a tick BEFORE the
player lands: poll `t.world.tile()` up to 4 ticks.

(d) `player.delayed` refuses held ops: a use handler that `p_delay`s between its two messages makes
the NEXT `use_item_on_item` refused silently (the row answers `ok` on the previous chat line,
backpack unchanged) -- content prints a use's lines together.

(e) helper_coverage credits a step by a static line naming its target, so a loop over a table of
step names grades UNMATCHED -- unroll step rows.

(f) FIXED (seam27): `t.npc.await_present`/`await_gone`, `t.ui.await_open` and `t.await` answer a
detail on `ok`, and `gate.py` fails any PASS row with an empty detail.

(g) Facts: the Ghosts Ahoy giant lobster (30 hp) takes ~75 ticks at attack/strength 40 with a rune
scimitar -- give `await_dead_engaged` 150; only the temple copy of `ahoy_harbour_door` (3656,3514,1)
is bone-key gated; Superheat Item turns 'perfect' gold ore into a 'perfect' gold bar (LostCity
`ores.obj` smeltsto), arming it auto-opens the backpack, and rune symbols are
`naturerune`/`firerune`; Miss Cheevers' room is gathered from its shelves, crates, chest and
bookshelves (Quest Helper `MissCheeversStep`), and her south shelves tripped the `pre_login`
fingerprint at the default pose (FIXED seam35: the fingerprint needs both canvas probes, Seam pass
35 (b); no camera turn needed); Recruitment Drive's
completion gives the Initiate sallet (`basic_tk_helm`); The Feud's two villager talks (stages 25/26)
are ported (`feud_villagers.rs2`) and gate the Bandit Leader.

### Seam pass 27 (2026-09-28)

FACTS FROM SEAM PASS 27 (2026-09-28).

(h) A cheat that `inv_add`s to `worn` fills slot 0 (the hat) and the first hat worn knocks it into
the pack: cheats put worn items in their wearpos slot with `inv_setslot(worn, ^wearpos_*, ...)`.

(i) A self-continuing skill roll (a fire) that a test polls for N ticks can fail when unrelated rows
above it shift the deterministic rolls: poll generously (40 ticks for a fire).

(j) Port Phasmatys' west Energy Barrier is `ahoy_town_barrier_multi` at 3659,3508 (2x1, facing
south; town SOUTH): press it with `click_loc('ahoy_town_barrier_multi', op, { at = {3659, 3508} })`
from 3660,3510 (in) or 3660,3506 (out) and read `t.world.tile().z` (3507 inside, 3509 outside); op1
is the guard's toll talk, op4 pays silently, and after completion op4 is the only Pass -- a
`goto_tile` into the town is past a gated barrier (trap 32).

(k) A loc worked across BLOCK-flagged water (jm2 f1) has no op-adjacent walked tile in the real map
either: the content fix is an `[aploc]`/`[aplocu]` with LostCity's
`if (distance(coord, loc_coord) > N) { p_aprange(N); return; }`, never `stand_on_square`; a
`p_teleport` landing constant must be an open STAND tile (Quest Helper ObjectStep WorldPoints are
the loc's own tile).

(l) `click_loc` on a chop/skill loc answers on the walk's map_flag before `p_arrivedelay` and the
swing: read the result (`loc_change`) a few ticks later.

(m) `talk_to` waits 5 ticks for an owed page after a bare `mes`, which outlives short content
windows (The Feud's 5-tick blackjack knock-out): use `t.player.press(npc, op, ticks)` for silent or
mes-only ops that must be followed quickly.

(n) An `~objbox` page is matched with `t.chat.expect_text(<text>)` then `chat.play {'*'}` (no
`objbox:` entry); `t.player.drop` of a second copy of the same item nearby answers FAIL (the ground
count does not rise).

(o) Spawn artefacts in a quest room are removed through `tools/gen_spawns.py` `OBJ_SPAWN_EXCLUSIONS`
(symbol,x,z,plane) with a cited source, never by hand-editing a generated `.spawn`.

(p) `helper_coverage.py` has no `--ledger`; grading a scratch-named run needs its `ledger_path`
pointed at it.

## Seam pass 29 (2026-09-29)

(a) npc random streams have a LIFE (`npc_seed_lives`, torirs_server_world.c). A `::spawn`'d or
`npc_add`'d npc on a tile where one of its type was freed is no longer a replay of the last one:
before, every `::spawn imp` at one tile was the same imp and a drop depended only on the fight's
length (104 kills: black 22 red 22 white 26 yellow 0). A key's first life seeds exactly as before,
so boot spawns and respawns are unchanged. A committed hunt or boss retry that used a replayed
stream may roll differently now; every committed test stayed green.

(b) An npc attack's cadence is `%npc_action_delay` (skill_combat/configs/npc_combat.varn) plus
`npc_attackdelay`, never `npc_delay`: a turn spent in `npc_delay` drains no `[ai_queue2,_]`, so no
player hit lands on the npc. Melzar, the thrower troll, KBD, kalphites, MM archers/demon, snails,
leeches, the necromancer and Nezikchened were moved to LostCity's pacing (Melzar's largest gap
between the player's splats 8 -> 5 ticks, the troll's 7 -> 4); battle mages say their line once per
cast. `npc_delay` stays only for LostCity's own scripted pauses (cabbage 1 tick, dragon spear stun).

(c) Proving content against HEAD without mutating the shared tree: `git -C OSRS-Content worktree
add --detach <scratch>/wt HEAD`, copy server/pack into it, build its pack with `make -C src
torirsserver-scripts-lanes TORIRSSERVER_SCRIPT_LANES= TORIRSSERVER_CONTENT_DIR=<wt>/osrs239-content
TORIRSSERVER_SCRIPT_OUT=<wt>/osrs239-content/server/scripts/build`, then
`TORIRSSERVER_CONTENT=<wt>/osrs239-content run.py --script ...`.

(d) The driver's npc row carries `hit_cycle` (the newest splat's start cycle) and
`overhead`/`overhead_timer`: a `hit_cycle` stamp every swing proves the npc drained its queue that
tick; `overhead_timer >= 138` means "said within the last tick".

(e) `::wield <name>` resolves like `::give` and every miss answers FAILED; the setup loop reads the
worn container back and FAILs `setup.::wield <item>` quoting the last chat lines (arthur fought
Mordred unarmed behind a green setup before).

(f) A garbled item text is not automatically an OCR typo: A scruffy note's "Got a bncket of nnilk"
is the real in-game text (wiki Transcript:A_scruffy_note). The Grand Tree cupboard's "you find
nothing" after the journal is LostCity parity (quest_grandtree.rs2:96-107).

## Seam pass 30 (2026-09-29)

(a) Leg checkpoints landed: a test may declare `legs = {...}` plus a top-level `bind`, and
`run.py <id> --from-leg K` resumes from the server's `::checkpoint` save. See relay.md
"Checkpoints". A checkpoint run is never graded, published or set green.

(b) A multinpc switch varbit is drawn only if its BASE varp is declared `transmit=yes` in content. A
cache varp with no content `.varp` is not sent. `sote_tertiary` (the carrier of Eluned's
`roving_female_woodelf` and Islwyn's `roving_bowyer`) is now declared like `sote_primary`. When a
multinpc shell never changes form on the client, check its basevar first.

(c) An npc's continuous fight is `npc->combat_target`, not its mode. `npc_getmode = opplayer2` is
false between swings (world.c phase 4 drops `opplayer*` to none after each one), so a LostCity
"while fighting" mode test ports as "the npc has a fighter" (`npc_findhero` / `npc_hastarget`),
never literally. Dad (Troll Stronghold) now resets when his fighter is gone or outside the arena
zone (LostCity troll_champion.rs2:101-109).

(d) In an npc `[ai_queue3]` death script, every npc op must come before the first `p_delay`: the
corpse leaves the pool during the delay, and the next npc op aborts the script (Nezikchened's first
defeat skipped its last hit this way).

(e) FIXED seam31 ((a) below) -- LostCity-port engine gaps, fixed in content, not in the engine: `.huntnext` ignores the `.`
operand (it walks the PRIMARY pointer), so a `.huntnext/.coord` loop aborts with "COORD requires
an active entity"; walk hunts on the primary pointer. `loc_*` reads after `loc_del` abort with
"the active loc is gone"; capture them before the delete. The `.obj` param reader does not
resolve `^constants`; write literals.

(f) Paterdomus after Priest in Peril (the `::setvar` note is FIXED seam31, (b) below): temple trapdoor (3405,3507) open, then descend to
3405,9906; `pip_underground_door1` needs `%priestperil_mausoleum` bit 20 (the golden-key unlock)
or the golden key; `pip_underground_door2` (3431,9897); Drezel at stage 60 gives his advice
(-> 61); the holy barrier passes only at 61 and puts you out at 3423,3485,0; `pipeastsidetrapdoor`
(3422,3485) open, then descend to 3440,9887,0 beside Drezel. `::complete quest_priestinperil`
sets only the stage, so a setup that uses the temple route adds `::setvar priestperil_mausoleum
1048576`.

(g) A `~mesbox` opened out in Mort Myre can be closed a tick later by a ghast's attack (`chat.play`
answers "no dialogue is open" with a mesbox-p1 shot). Ask for such pages inside the grotto.

(h) The lane's tab name for skills is `stats` (revconfig/osrs239/osrs239_dat2_cache.ini `[tabs]`).

(i) Observatory professor: "Talk about Treasure Trails." appears only while a coordinate clue is in
the pack (the LostCity gate); `chat.play` drives the chart objbox as `*`. The Dwarf Cannon guards:
the OSRS world has exactly the wiki's seven (3 at the Dwarven Mine entrance, 4 south of the Coal
Trucks); LostCity's extra five are 2004 tiles, so do not add spawns. The Coal Truck four
(`mcannonguard1..4`) now answer Talk-to.

(j) Legends' Quest is landed from the LostCity port (parity3c candidate plus seam30 fixes) but has
no test yet. Its ladder and notes are docs/quests/ladders/legends.{ladder.tsv,notes.md}. The
Gujuo bowl blessing keeps LostCity's inverted `stat_random` (a higher Prayer fails more often); it
needs a wiki check before anyone changes it.

(k) LANDED seam31 (coverage-and-gate.md "A promoted sub-step") -- was DEFERRED: a `helper_coverage` grader that grades Quest Helper `addSubSteps`
children as their own steps. It turned three committed greens red on real skips (biohazard
exitBackyardOfHeadquarters, eadgar leaveEadgarsCaveForThistle, mourningsendparti
enterMournerBaseAfterPoison), so it waits until those tests click the crossings. The patch is
build/seam_state/seam30/helper_coverage_substeps_deferred.patch.


## Seam pass 31 (2026-09-30)

(a) FIXED the engine gaps seam30 (e) listed; port LostCity scripts as written. `.huntnext` binds
`active_player2` and leaves the primary player alone (LostCity PlayerOps.ts HUNTNEXT).
`loc_coord` / `loc_type` / `loc_angle` / `loc_shape` answer after `loc_del`, across a suspend too,
while the handle still names the removed loc (LocOps.ts LOC_DEL keeps `activeLoc`); a WRITE after
`loc_del` still aborts. `loc_name` / `loc_param` after it aborted here too (FIXED seam32 (e)). `obj_name` and
`inv_dropitem` are implemented (ObjOps.ts, InvOps.ts: one pile of what came out, owned by the
dropper for 100 ticks, made the active obj). A `.obj` param may be written `^constant` (one level;
Legends' `crystal_bit` is LostCity's spelling again). Selftest stanza "legends port VM gaps".

(b) `::complete quest_priestinperil` also sets `%priestperil_mausoleum` bit 20, the golden-key
unlock of `pip_underground_door1` (`gates.rs2:15`). A setup that goes through the Paterdomus
temple no longer needs `::setvar priestperil_mausoleum 1048576` (seam30 (f) is superseded).

(c) `loc_del(N)` followed by `loc_add` of another loc on the SAME tile and shape (LostCity's
double-door inviswall) loses the original loc for good here: the revert table is keyed by (tile,
shape) and the second timer replaces the first (`ToriRSServer_WorldLocRevertQueue`). Port it as
`loc_change(inviswall, N)`, one timer (east_gate.rs2, quest_haunted.rs2, now legends_procs.rs2 --
the Kharazi cave trial doors and the Legends' Guild doors came back on every later trip). FIXED
seam32 (e): the engine keeps one lifecycle per loc, and legends_procs.rs2 is back in LostCity's
`loc_del` + `loc_add(inviswall)` form.

(d) A checkpoint carries `[clock] map_clock`, and a `--from-leg` login moves the world's clock
forward to it, so every clock-stamped varp (`%action_delay`, `%frozen`, ...) keeps its meaning
(relay.md "The clock"). The last leg of a legs file gets a checkpoint too, and a run that did not
reach `t.quest.expect_complete` is never published.

(e) A ledger that ends in `run.unfinished` names why the client stopped (running.md "A run that
ended unfinished"); a quest over about 2000 ticks needs `max_frames`. A run over about 8 minutes
goes through `run.py --detach` / `--wait` (relay.md "Runs longer than the shell cap").

(f) Obj id 0 (`mcannonremains`) is invisible to the client's inventory: a client defect (about 25
`obj_id <= 0` "empty" tests against a -1 sentinel), not content (verbs-pointer: `click_obj`
answers `timeout`). FIXED seam32 (c).

(g) Selftest fixture: the full-world fixture fills all 4096 ground slots with map spawns; a stanza
that drops an obj must borrow a slot and put its record back.

(h) Dragon Slayer content is at the guide (content-gaps.md and gaps-world.md, FIXED seam31): the
Guildmaster answers "About my quest...", the magic door takes one item per use, the lair wall is
climbable, Elvarg has 80 hitpoints. Gujuo's blessing roll stays LostCity's (unsourced either way;
seam30 (j)).


## Seam pass 32 (2026-09-30)

(a) Cutscenes are asserted, not survived. Every CAM_MOVETO / CAM_LOOKAT / CAM_SHAKE / CAM_RESET
the client executes is stamped into `app->cam_script` (`struct App_CamScript`, a serial and a
64-deep ring in world tiles). `t.world.camera()`, `t.cutscene.await` and `t.cutscene.mark` read it.
`gate.py` `cutscene_row_required` reds a quest whose content frames the camera until its PASS
`cutscene:` rows cover every framing site, and `make -C src check-quest-cutscenes` fails a
DROPPED/PARTIAL port (never WIKI_MISSING). Fight Arena's five LostCity sequences are ported
(quest_arena.rs2, sammy_servil.rs2, general_khazard.rs2; cutscene_sweep MATCH 17/17). See
verbs-cutscene.md.

(b) A hung client dies in about 90 s. The driver rewrites `<session>/heartbeat` every 25 server
ticks, and run.py kills the process group once the file is older than
`TORIRS_QUEST_STALL_SECONDS` (default 90; boot grace 120 s). The ledger ends in `run.unfinished`
`run stalled: no client tick for N s; last row <name> at tick T` (relay.md "Runs longer than the
shell cap"). `while true do end` inside a step is NOT a hang: the step's instruction budget
(400000) ends it as a `script-error`. An await `level` predicate that loops IS one: the per-frame
pump calls it without the budget. Measured wall time of the 81 committed tests' last runs: p50
40 s, p90 116 s, p95 149 s, legends 572 s.

(c) Obj id 0 (`mcannonremains`, Dwarf remains) is a real item in the client. The empty sentinel is
-1 in both domains: `INV_MANAGER_EMPTY_OBJ_ID` for container slots and `UITREE_NO_OBJ` for
`UITreeComponent.item_id`. An obj is present when `id >= 0`. The objtype link fields
(`cert_link`, `placeholder_link`) use -1 for none; `count_obj` uses 0. C authors: never test an obj
id with `> 0` / `<= 0`. A grep for `obj_id` finds only half of the obj-id variables
(`task_obj_model_load.c` `resolved_id` / `render_id` hid the icon). The DRIVER Lua's own
`<= 0` / `> 0` tests are FIXED in seam33 (Seam pass 33 (b)). `food.rs2`'s
`db_getfieldcount` guard stays: the SERVER's dbtable default for an omitted obj column is 0.

(d) A ground obj on a table or other blocking centrepiece's own tile is drawn RAISED onto the loc
(`World_ObjRaiseGet`; rscache defaults `raiseobject` to `blocks_walk`). The driver's obj projector
now aims at that height, so `click_obj` presses the item on the first pose (verbs-pointer:
`t.player.click_obj`).

(e) Loc lifecycle, against LostCity LocOps.ts / World.ts / ScriptState.ts:
- A loc's revert timer is ONE lifecycle. `loc_del(N)` + `loc_add(same tile and shape, M)` brings
  the ORIGINAL back after M (LostCity doubledoors.rs2). A re-statement restarts the clock and keeps
  the target; a "forever" re-statement and a `loc_del` of an added loc end it.
- `.loc_find`, `.loc_findnext`, `.loc_add`, `.loc_change`, `.loc_anim`, `.loc_coord` address the
  SECONDARY active loc. Before, `.loc_find` overwrote the primary and `.loc_*` aborted "requires an
  active entity" (Shilo Village's tomb door, `quest_zombiequeen.rs2:1457`; Fight Arena's pen gates).
- `loc_name` / `loc_param` / `loc_category` answer after `loc_del`, like `loc_coord` / `loc_type`.
- Not ported: LostCity's LOC_ADD matches the same LAYER; here the key is (tile, shape).
- Author hazard (zombiequeen): there are two `thzq_tombrooml1` copies (2892,9480 and 2893,9497).
  Pick the leaf `t.world.loc_near` finds within 3 tiles, not `t.player.by_symbol`.
- Content comments that described the old `.loc_*` abort (`flamtaer_temple.rs2:295-309`,
  `prison_doors.rs2:5`) are rewritten, and the Shilo tomb door is back in LostCity's order
  (`loc_change` then `~set_zqtombdoorstate`): FIXED in seam33.

(f) Content, sourced: Oziach's "second piece of the map" no longer moves `%dragon_oracle` back
(the guard `guild_master.rs2` uses). `oracle.rs2:18` had the same bare write (LostCity too): FIXED
in seam33 (Seam pass 33 (f)). `imbued_heart_ready_tick` and `hunter_falcon_expire` are `scope=temp`
now (LostCity saves no map_clock stamp); the last perm stamp, `zq_rash_timer`, is FIXED in seam33
too. Gujuo's blessing roll stays LostCity's: no source gives its odds (docs/quests/legends_quest.md).

(g) A `--script` run's rolls are seeded by the player NAME (`--name`). "Fails on name A, passes on
name B" is a roll, not a tree regression; reproduce with the SAME `--name`. druidspirit's seam31
137/149 was this: under `s31vm_druidspirit_shared` a second revealed ghast keeps renewing the
player's single-way claim and every press on another ghast answers "I'm already under attack."
druidspirit's setup no longer carries `::setvar priestperil_mausoleum` (seam31 (b)).


## Seam pass 33 (2026-09-30)

(a) An interface press while the player is DELAYED is dropped by the server, silently. The rule is
LostCity's: `IfButtonHandler` runs `[if_button]` with protected access and `Player.runScript`
returns -1 while `delayed` (`torirs_server_world.c` `if_button_refused_while_delayed`; verbose line
`<- IF_BUTTONN 218:29 refused: player is delayed (p_delay)`). Lost City's `teleportAway` was pressed
on the tick the Dramen chop (`leprechaun_tree.rs2` `[oploc1,dramentree]`, ends `p_delay(1)`) still
held the player, and read `the cast never ran`. A self-cast now re-presses after 3 silent ticks, up
to 3 presses (`spell.lua` `SELF_CAST_*`; detail `(press N: ...)`). A HELD-item cast still presses
once. Row: `seam.cast_self_teleport_from_dungeon_after_delay`.

(b) The driver Lua's empty-slot tests follow seam32's -1 sentinel: `state.lua` `QD.inv.slot`
(`obj_id < 0` is empty), `ui.lua` `QD.shop._stocked` and `QD.shop._row`, and `pointer.lua`
`QD.player._inv_contents` (`>= 0` is an item). `t.inv.slot` names `mcannonremains` (obj 0), and
`use_on` / `use_item_on_item` see it leave the pack. In driver Lua test an obj id `< 0` / `>= 0`,
never `<= 0` / `> 0`. Row: `seam.inv_slot_obj_zero`; `mcannon.lua` `getRemainsStep` now asserts
the pickup.

(c) `date_minutes` and `date_runeday` read one world clock, `ToriRSServer_WorldRealtimeMs`
(CLOCK_REALTIME plus `srv->clock_skip_minutes`). `::clockskip <minutes>` moves it forward (1-10080
per call, a year in all, never backward), writes `%date_minutes` at once, and is lost when the
server re-boots (a relog). `t.clock.skip(minutes)` drives it; the quest's own catch-up still does
the work (Forgettable Tale's kelda patch: `forget_farming = 8` 88-98 ticks after a 16-minute skip,
on its `forget_tick` softtimer). Ordinary farming re-arms from NOW, so one skip is one stage.
Server selftest stanza `::clockskip moves the world clock`; gaps-world, "A step that waits real
minutes".

(d) A bare name in a comparison against a LOCAL is typed by namespace sort order alone: sscompile
types `$x = <name>` only from a left side whose kind it knows, and a `dbrow` local or parameter is
not one. `quest_wanted` is both dbrow 156 and varp 571, so `if ($row = quest_wanted)` in
`quest_cheat.rs2` compiled to `$row = 571` and `::complete quest_wanted` answered "has no arm". The
arm now spells it `~quest_cheat_row(quest_wanted)` (a `dbrow`-typed proc argument). The same
collision still miscompiles `interface_questjournal/scripts/quest_journal.rs2:439` and
`poh_quest_status_generated.rs2:747` (open; the compiler fix is `ssc_compile.c`).
`SSCOMPILE_AMBIGUOUS=all` lists every such name. `::complete` gained arms for
`quest_touristtrap` (30), `quest_templeofikov` (80, the Armadyl ending) and
`quest_trollstronghold` (50), each from the quest's own completion queue
(`docs/QUEST_SERVER_CHEATS.md`, "`::complete <quest row>`"). Row: `seam.complete_cheat_arms`.

(e) Trap 19 again, on Desert Treasure's ice bridge: `m44_59.spawn` places the parents as the BASE
symbols `troll_block_1/2` (multivarbit `fd_icewarrior_dadfree/mumfree`), and only
`[opnpc1,fd_troll_*]` existed, so a freed parent answered "map_flag: no dialogue in 5 tick(s)" and
the reunion teleport never ran (the missing child, npc 696, was downstream of it). The base
triggers now hand off by the player's varbit, with the troll family's lines from the OSRS wiki
transcript. When a respawned multinpc form answers nothing, grep for `[opnpc1,<the spawn row's
symbol>]` before blaming the driver.

(f) Content, sourced: `oracle.rs2` no longer moves `%dragon_oracle` backwards on a revisit (2009scape
OracleDialogue.java writes no quest state; Oziach and the Guildmaster guard the same way), and
`zq_rash_timer` is `scope=temp` as LostCity declares it (a saved map_clock stamp outlived a restart
and Rashiliyia never came). A stage write in a REPEATABLE dialogue must be guarded `<`; a revisit
row (stage the later value, ask again, read it back) catches it. A relog re-boots the embedded
server, so a relog row is the proof for a varp scope change. legends' dead gate walk-arounds are
gone; its gate-state rows assert presence (538/0).

(g) Zombie Queen's two caverns misses were the test, not content (the port matches LostCity line
for line): the loose rocks roll `stat_random(agility, 75, 250)` and a miss is the cave-in branch
(loop the search: the page after "slowly move" is objbox for the scroll, mesbox for the cave-in),
and the gallows run `mes()` + `p_delay(2)` before the first page (await `t.chat.kind()` first).
"3 misses in 7 runs" were 7 different `--name`s, i.e. 7 streams (Seam pass 32 (g)): a roll is
looped in the test, never assumed (docs/quests/ladders/zombiequeen.notes.md).

## Seam pass 34 (2026-10-01)

(a) Render skip is ON in every quest run (`TORIRS_RENDER_SKIP=1` from `run.py` and `conformance.py`;
`--render-every-frame` is the A/B). A frame still runs its tick, input, net, plugins, layout and
emit walk; only the draw and present are skipped unless a screenshot, a pushed click or a pickset
read needs the frame, and render-time state is drawn LATE, never waited for, so the timeline is the
skip-off one. Proof: all 88 ledgers identical row for row (step, verdict, ticks) to the skip-off
green run; summed client time over the suite 5,149 s -> 1,802 s, legends 7.8x. Pictures can differ only in the cursor tooltip box,
a never-bind-posed model (druid's suits of armour) and `CAM_SHAKE` jitter. running.md, "Render
skip"; verbs `t.render.skip` / `t.render.frame`; rows `render.skip`, `render.frame`,
`seam.render_skip_pick_read_catches_up`, `seam.render_skip_shot_draws_its_frame`.

(b) `t.player.inv_op(item, 2)` on a Wield/Wear answered `timeout ... -> 0 left
[settle_after_click]` because a landed `~equip` prints, mounts and routes nothing. It now resolves
on the item's worn total rising and tags the row `[WORN <item>: worn 0 -> 1, wear slot N]`
(verbs-inventory-shops). And a stalled `walk_to` names the locs with an op within two tiles of where
it stopped: a rock bridge, stepping stone or log is a WALL in the game until its op is pressed
(Underground Pass `walkway_upass_narrow_mid_top`, `blockwalk=1` `op1=Cross`, LostCity the same), so
the stop is the game's answer and the fix is `click_loc`, never a walk that paths over it
(verbs-pointer, "A walk stops at an obstacle"). Underground Pass's rejected relay ran unchanged to
checkpoint 4 (114/0). Row: `seam.inv_op_wield_reads_worn`.

(c) A cache-declared transmit hook (`onvarptransmit=` / `oninvtransmit=` / `onstattransmit=` with
`varptriggers=` in the `.if`) was dead on a group's FIRST open: `Task_InterfaceOpen` armed the
record's hooks before baking the group, each hook's node ref resolved to nothing, and the var
dispatch drops a hook whose ref does not resolve (a regression of the incarnation refs, 31b64f3f0;
boot-baked groups were fine). Symptom: a modal's counters and models never change while
`t.var.varp(sym)` on the client already equals `t.var.server(sym)`; rows read `[frame unchanged]`
(Forgettable Tale's junction puzzle, interface 248: shots 194-196 byte-identical). FIXED: armed
after the bake (`src/engine/uitree_builder/task_interface_open.c`). Diagnostic recipe: after each
press compare the client and server varp and read the component with `t.ui.text`; equal varps and
stale text is the client hook, not transmission and not render skip. Row:
`seam.cache_transmit_hook_first_open`. Open, content: with the counters drawing, interface 248's
GREEN-icon row shows `forget_num_left` (varbit 861) while the port spends `forget_num_right` for a
green junction (`forget_puzzle.rs2`); needs a sourced colour/varbit check.

(d) `cutscene_row_required` can be met for a site on a route the guide never takes:
`t.cutscene.exempt(site, reason)` writes a claim row, and `gate.py` accepts it only for a site
entered solely from player-op triggers no guide step targets or carries, with the reason naming a
PASSed guide step; everything else is refused (`cutscene_exempt_refused:`). `gate.py --cutscene-as
<test_id> <dirs>` grades scratch runs. verbs-cutscene, "`cutscene_row_required` names a site on a
route you did not take".

(e) `cutscene_sweep.py` reads LostCity_Server too: ten quests exist only there (eadgar horror misc
mm mortton regicide routequest tbwt troll_love viking) and graded WIKI_* before, hiding troll_love's
DROPPED sled-ride camera and mm's PARTIAL sigil-teleport lookat. `make -C src check-quest-cutscenes`
is RED on those two until the cutscene session ports them (verbs-cutscene, the sweep paragraph).

(f) A transmogged player is DRAWN as the npc: the client decoded the appearance block's 0xffff entry
and drew the player's own body (Monkey Madness's greegree wearer, a human in a monkey's stance). The
player entity now keeps `transmog_npc_id`; the body reconcile builds the npc type's model (multinpc
resolved per frame, LostCity `ClientPlayer.getTempModel2`) and puts the human body back when it
ends. Ladder cheat `::transmog <npc>|off` (QUEST_SERVER_CHEATS). gaps-world, "Monkey Madness".
Row: `seam.transmog_draws_the_npc`.

(g) The Entrana monks' ferry lands on the ship's DECK, not the dock (OSRS-Content, LostCity
`~set_sail` coords `1_44_52_18_3` / `1_47_50_40_31` in `monk_of_entrana.rs2`); the old Entrana jump
0_44_52_15_6 put the player at 2831,3334,0, in the sea, and made the guide's `useGangPlank`
undrivable. A ship landing's source is LostCity's `~set_sail` destination; a `p_telejump` that skips
a deck skips a guide step. zanaris, grail and hero (the committed ferry users) stay green. Recipe:
docs/quests/ladders/deviousminds.notes.md. Desert Treasure's guide items are all obtainable in
content (docs/quests/ladders/deserttreasure.notes.md); the Ardougne SILVER shop opening EMPTY in
the client (a content-declared inv, `cell 3 of shopmain:items is not mounted`) is FIXED in seam35
(Seam pass 35 (a)); open: the Entrana monk's weapon search is unported.

## Seam pass 35 (2026-10-01)

(a) A shop whose inv is declared in CONTENT (`pack/inv.alloc` + `size=`: the Ardougne silver stall
2023, Zaff, the Pie Shop, 64 shop invs in all) opened with no grid cells: `shop.open` read `holds K
stocked slot(s) of K` and `shop.buy` answered `cell N of shopmain:items is not mounted`. The server
sends every slot; the client's `INV_SIZE` read only the cache `InvType`, which has no record for a
server-only inv, and `shop_main_init` (1074) builds `inv_size($shop)` cells once at open. FIXED:
`exec_inv_size` (src/game/rs_cs2_host.c) falls back to the capacity UPDATE_INV_FULL gave the
container when the cache has no record; a cache inv still answers from its type. The first open is
deterministic because RUNCLIENTSCRIPT waits for the SERVER_TICK_END fence. Unit test
`test_server_allocated_inv_size_reads_the_container` (make test-cs2-transmit-pump); row
`seam.shop_content_inv_opens_stocked`. Non-shop content invs (mortton coffin, meat pouch, stash)
take the same path, undriven.

(b) `gate.py`'s `pre_login` fingerprint was the shot's top-left corner against solid black, so any
dark in-game corner matched it: 39 of 112,755 shots, all in game (mm's cutscene fade
`163-clickPuzzle-cutscene`, Miss Cheevers' shelves, troll_love's crash keyframes); and the screen a
stuck run really photographs, the TITLE screen, never matched (s18d_pry1 drove six rows on it).
Now a pre-login frame needs BOTH the top-left and bottom-left 64x64 probes of the 765x503 canvas
(centred in the shot) to match: `pre_login` (the loading screen) within 4.0 of black at both, the new
`title_screen` within 8.0 of `TITLE_SCREEN_CELLS` at both. A fixed-mode frame's bottom-left is the
chat-tab stone, 80+ away, so a fade, a void corner or a dark cave no longer trips it. A PASS
`t.session.logout` row's own shot is exempt from `title_screen` (photographing the title is that
step). `gate.py --probe <png>` prints a shot's distances and cell means. Over every shot on disk:
`pre_login` 0 matches (was 39), `title_screen` 27, all logged-out frames. coverage-and-gate,
"Fingerprints".

(c) `helper_coverage.py` credited a guide step that GOES THROUGH a loc (`enterHamLair`, a step whose
leading verb is enter/climb/cross/...) to a press of the loc's GATING op (Lost Tribe's Pick-Lock on
`ham_multi_trapdoor`), while the test then `goto_tile`d into the lair (sampler revert c32508b14).
Now such a step is credited only by a travel-op press (Climb-down/Enter/Cross/Open... in any
multiloc state) or a row whose detail shows the player moved across; a press on a multiloc CHILD
state is held to the step's op like one on the parent; a `goto_tile` landing on a climb's far side
is CHEAT. The 89 green tests regrade identically. coverage-and-gate, "presses op5 'pick-lock' ...";
fixture `python3 tools/quest_gate/helper_coverage_two_op_test.py`.

(d) `t.var.server` / `t.var.await_server` on a VARBIT read only the client's record, which holds only
what the server SENT, and the server sends a varp only when content declares it `transmit=yes`.
Monkey Madness's `mm_daero` / `mm_caranock` / `mm_narnode` sit on `mm_gnomes` (varp 372), which
nothing declares, so they read a confident `ok 0` for the whole quest. Now a varbit whose base varp
is never transmitted is read off the embedded server (`api_drive.varbit_content`), labelled
`server content copy; base varp N is never transmitted`; the channel is chosen from that static
transmit fact, never from the values disagreeing. `var.expect` still needs a client copy and its
refusal now says so. Not covered yet: `quest.bind{varp=<such a varbit>}` grades the client pair.
verbs-state-and-vars, "A quest varbit reads 0"; row `seam.varbit_server_reads_untransmitted_base`.

(e) Three real presses replace Haunted Mine's `t.drive.op` (verbs-combat, "A covered Attack press, a
boss that teleports, a timed walk"): `t.player.attack` re-takes a `covered` first press from a
SETTLED camera (poses 1 and 4), then from a clear tile two squares off, before the old unsettled pose
loop (Treus Dayth, raised one row after a press that walked the player: 64 ticks `covered` and a
dead character before, 14 ticks after); `await_dead` / `await_dead_engaged` follow an npc that
`npc_tele` re-added under a NEW client slot instead of grading the old slot's absence a kill; and
`click_loc` follows a press whose walk outlasts the 20-tick settle for up to 80 ticks (the 60-step
valve-to-lift route at run energy 8-13). Rows `seam.npc_cover_settled_recovery`,
`seam.npc_tele_reslot_followed`. The recovery runs only after a REAL press, not a pixel under the
UI (Troll Stronghold's generals went red when it did). It also shortened Legends' Irvig and Ranalph
fights, so leg 6 now leaves two sharks, leg 7's fixed `::give shark 9` fills the backpack, and
Ungadulu's `inv_add(holyforce)` (ungadulu.rs2:547) lands nowhere: legends was reopened to top the
sharks up to a count. Engine note, not fixed: `npc_tele` re-adding under a new slot is not what the
real client sees (it keeps the index).

(f) `::complete` has an arm for every quest row a completion site passes to
`~quest_complete_rewards`: sixteen new ones (bigchompybirdhunting, eadgarsruse, elementalworkshop1,
familycrest, fightarena, horrorfromthedeep, insearchofthemyreque, lostcity, onesmallfavour,
regicide, scorpioncatcher, seaslug, shadesofmortton, shilovillage, tribaltotem, witchshouse), each
writing what its completion site writes, cited in quest_cheat.rs2 and QUEST_SERVER_CHEATS.md. The 26
rows with no completion anywhere in content still answer `has no arm`. A test that staged one of
these prerequisites with `::setvar` (rovingelves, mourningsendparti, zogreflesheaters, legends,
hero) can use `::complete` now. Rows `seam.complete_cheat_arms.<row>`.

(g) `p_temprun` and `p_animprotect` are engine ops now (LostCity PlayerOps.ts:1276 / :1236), not
stubs, and Troll Romance's sled rides and Monkey Madness's greegree call them again as LostCity does:
`p_temprun` runs the route whatever the orb says until a movement phase finds no route or energy
drops under 1%; `p_animprotect` makes `anim()` play nothing, `anim(null)` included.
`TORIRSSERVER_ANIM_TRACE=1` prints each script anim and its fate. A transmogged local player's
chathead is the npc's (`t.chat.head()` names the npc), and a placement centres a size-N transmog on
its footprint. `fresh_lumbridge.ini` starts with the run orb ON, so a test cannot see walk-vs-run of
a scripted `p_walk`; `::run N` sets run ENERGY, not the orb. Server selftest stanza "p_temprun and
p_animprotect".

## Seam pass 36 (2026-10-01)

(a) A `[mapzone]` / `[mapzoneexit]` subject is ALWAYS level 0. LostCity latches the map square with
the level forced to 0 (`NetworkPlayer.ts:252` `CoordGrid.packCoord(0, ...)`) and dispatches
`[mapzone,0_${x>>6}_${z>>6}]` (`Player.ts:582`; `:589` for the exit), so it fires on entering the
64x64 square on ANY level and does not fire again on a climb inside the square. Only `[zone,...]`
carries the level. Our engine already matched that. The bug was content: of 83 `[mapzone*]` headers
in osrs239, only Underground Pass's `[mapzone,1_33_71]` (`~upass_spawn_demons`) and
`[mapzone,1_33_72]` (`~upass_spawn_temple_actors`) named level 1, which is a name no dispatch forms.
So the three demons and Iban's temple never spawned, and upass parked at leg 7 on `holthion=no_row`.
FIXED: both are now `[mapzone,0_...]` (upass_encounters.rs2; the procs already place level-1 npcs).
Gate on `coord` level inside the script when the level matters. Server selftest stanza "a
[mapzone]/[mapzoneexit] whose subject starts with anything but 0_" fails any pack that carries
another spelling, and checks that the demons' room dispatches. Conformance row
`seam.mapzone_upper_level_square_fires`. Side effect: Iban and 13 Disciples of Iban now stand in
the temple from stage 5. content-gaps, "upass".

(b) `helper_coverage.py` grades every leaf the `steps.put` ConditionalStep tree can show, not only
the `getPanels()` lists. Monkey Madness's Bamboo Gate (`enterGate`, only in
`bringMonkey.addStep(onApeAtollSouth, enterGate)`) was invisible, so a leg-8 `goto_tile` from the
Ape Atoll dock to Garkor read FULL (sampler revert 16e31e41a). Such a BRANCH-ONLY step is placed
before the state it leads to and is graded after every panel step has taken its credit. A
`goto_tile` that leaves its zone for a later sibling's zone without pressing its gated loc is CHEAT
(`zone_crossing`, positions read from the run's own ledger). `ladder.py` shows the kind as
`<Kind><<composite>[<condition>]`. mm's ladder went from 76 to 78 steps (`useWool`, `enterGate`).
All 93 committed tests with a guide regrade with 121 steps added and no class changed; only mm moves
(FULL -> TEST_GAP on its reverted run). coverage-and-gate, "A branch-only step".
