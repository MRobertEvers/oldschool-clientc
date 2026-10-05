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
~8089): stage each varbit by its own name (`varb2231_sheepherder_sheep_a` .. `varb2234_sheepherder_sheep_d`).

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
3146,9914, combat 40; "Turael is not spawned" is FIXED: b50 put him back, and Mazchna and Duradel
stand since seam pass matthew-mbp-m4-b51-seam1 (c)).

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
screen's copy): seed it with `::setvar varp7185_death_coffer_balance <n>`; `::pohslayerlog` opens the POH
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
`api.php?action=parse&oldid=<id>&prop=wikitext&format=json`. Since 2026-10 every `action=raw` URL
answers curl with a Cloudflare challenge page; the api.php forms still work (seam pass
matthew-mbp-m4-b53-seam2 (d)).

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
count does not rise) -- FIXED seam pass matthew-mbp-m4-b52-seam1 (a): drop grades on the backpack.

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
sets only the stage, so a setup that uses the temple route adds `::setvar varp6733_priestperil_mausoleum
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
temple no longer needs `::setvar varp6733_priestperil_mausoleum 1048576` (seam30 (f) is superseded).

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
druidspirit's setup no longer carries `::setvar varp6733_priestperil_mausoleum` (seam31 (b)).


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
(Seam pass 35 (a)); the Entrana monk's weapon search is FIXED (Seam pass matthew-mbp-m4-b59-seam1 (j)).

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

## Seam pass 37 (2026-10-01)

(a) Var names carry their kind and id (PR #99, OSRS-Content PR #25). A bare `::setvar qp 43` misses
the cheat's exact rung and is refused by its substring rung (`Which qp? varb456_..., ...`); ten
quests failed setup that way right after the merge. `lint_quest.py` now refuses a bare or wrong-id
name in a `varp =`/`varbit =` key, a `::setvar` literal or a `t.var.*` symbol, naming the right
spelling; `new_quest.py` writes the prefixed bind and a `::setvar varp101_qp <N>` for a guide's
`QuestPointRequirement(N)`; the author and leg cards carry the rule. verbs-state-and-vars, "Var
names carry their kind and id".

(b) A climb on a multiloc whose BASE has no op of its own has no maplink row, and from level 0 into a
separate underground region it answers "You can't go any further.". The maplink harvest
(`tools/data/shortest_path/transports/transports.tsv`) names the multiloc base, and the importer
drops a transport whose base carries no matching op: the H.A.M. trapdoor's Climb-down (`5492`,
`ham_multi_trapdoor`) went through `~climb_ladder(-1)` -> `~maplink_try` -> the +/-1-plane
default. Maplink rows are also keyed on the PLAYER's tile, so a harvested row covers only the
approach sides the harvester used (the trapdoor's rows list west and south; the failing run came
from the north). FIXED for the H.A.M. lair: `[oploc1,osf_trapdoor_open]` (quest_losttribe
`losttribe_ham.rs2`) lands on `^lt_ham_trapdoor_in` 3149,9652,0 from any side, the mirror of
`[oploc1,osf_ham_ladder]` -> 3165,3251,0 (transports.tsv:1064-1068; 2009scape
`HamHideoutPlugin.java`). Drive it: Pick-Lock (`osf_trapdoor_closed` op 5), then Climb-down
(`osf_trapdoor_open` op 1). Conformance row `seam.ham_trapdoor_climb_down_lands_in_lair`.
onesmallfavour still `goto_tile`s into the lair (lines 205, 757); it is enterable now.

(c) Regicide's dense forests are crossed by the loc's own geometry, as LostCity does it
(`quest_regicide.rs2:388-480`), not by `maplink_agility.dbrow`. That table is generated from the
wiki's transport list and missed rows per direction: west of the tracker it had o3 2240->2237 and
o1 2231->2234 but nothing for o2, so the player was stranded on the 1x3 strip at 2237 with
"Nothing interesting happens.", and the guard summoned beside him could not be reached ("I can't
reach that!"). FIXED in `regicide_route.rs2` `[label,regicide_cross_dense_forest]`: stand within 1
of the middle square of your side, and the click forcemoves you 3 squares to the far side in the
loc's own anim (Agility 56 both ways). Each passage is three locs (o3 2238 / o2 2235 / o1 2232 on
z=3149), and the level 110 guard spawns once per player (`varb8446_regicide_seen_guard`) when you
land on 2231,3149 at stage 8 (LostCity `spawn_tyras_guard` :602-610). The `click_loc` row's detail
reads `teleport: 2237,3149,0 -> 2234,3149,0 (a jump no walk makes, held 2 tick(s))`: that is the
`p_exactmove` landing, a real crossing. Rule: a quest-owned obstacle whose LostCity source
computes the crossing from `loc_coord`/`loc_angle` must not route through the generated agility
table. docs/quests/ladders/regicide.notes.md has all three passages' tiles.

(d) A `p_delay` inside an npc-triggered script (`[ai_timer]`, `[ai_queue3]`) that has bound a player
is DROPPED while that player's own script is suspended (`dropping [ai_timer,iban], which suspended
while [oplocu,cave_temple_altar] waits`). A `player_lock` before such a delay is then never
released. Underground Pass's Iban bolt did exactly that: a hit while the doll throw at the altar
was running left the player locked in the exit pocket for good. FIXED as LostCity has it: the bolt
hit lands in one tick with no lock and no delay (`lord_iban.rs2`, LostCity `lord_iban.rs2:25-32`),
and Kalrag's death only queues the PLAYER queue `defeat_kalrag`, where the delays run (LostCity
`kalrag.rs2:1-31`; before, `npc_coord` aborted after the delay and the blessed spiders never
turned). Put delays in a player queue, never in the npc script. gaps-world, "Underground Pass: the
finale".

(e) LostCity's `caveguide5` (npc 976, the end-pocket Koftik at 2443,9607) is `caveguide6` (4532,
multivarbit `upass_koftik_end`) in the OSRS cache; OSRS `caveguide5` is the temple Koftik at
2170,4727 level 1. A port that keeps the LostCity symbol finds nobody: `[oploc1,upass_last_out]`
found no Koftik and the pocket had no way out. FIXED: it finds `caveguide6`, whose Talk-to (the one
binding, quest_regicide `koftik.rs2`) opens `@koftik_whereami` while `%upass` is `defeated_iban`.
When a LostCity npc symbol finds nothing, compare the OSRS spawn file's npc at that tile.

## Seam pass matthew-mbp-m4-b48-seam1 (2026-10-01)

(a) Iban's temple doors had no Regicide branch. Regicide's two walks to the Well of Voyage
(`enterTemple` in leg 2 and again in `goThroughUndergroundPassAgain`) go through the doors of
Iban's temple after Underground Pass is complete, and `upass_tomb.rs2` `[label,open_iban_door]`
carried only a "Regicide shortcut deferred" comment: with `%varp161_upass` complete the click
answered "The temple is in ruins... / ...You cannot enter." and the author jumped the door with
`goto_tile 2010,4709,1` (sampler-findings, Sample matthew-mbp-m4-b48 (c)). FIXED (OSRS-Content
2dd52a46a5) as LostCity_Server has it: entering from the east at `%varp328_regicide_quest >=
^regicide_spoken_lathas` (2), before the Zamorak-robe check, opens the doors and teleports you
into the ruined temple's copy of the room (`quest_upass.rs2:576-585`); leaving by that copy's
doors puts you back outside Iban's temple (`:629-631`). The doors are placed twice:
`upass_templedoor_closed_right`/`_left` at 2143,4648/4647 level 1 (Iban's temple, `m33_72`) and at
2015,4712/4711 level 1 (the ruined temple, `m31_73`, with `regicide_voyage_temple_well1` at
2008,4711). The right leaf lands on 2014,4712 level 1 (the left on 2014,4711); the ruined copy's
right leaf lands back on 2145,4648 level 1. A player below Regicide stage 2 gets the old answers.
Conformance row `seam.iban_temple_door_regicide_shortcut`. The route from Iban's door to the
temple doors crosses four collapsed bridges: gaps-world, "Underground Pass: Iban's temple door".

## Seam pass matthew-mbp-m4-b48-seam2 (2026-10-01)

(a) "The ledge pocket is sealed" was a rock bridge never clicked, not a map bug. Regicide's round 4
walked off the Underground Pass ledge (landing 2374,9638) and found every `walk_to` toward the pipe
(`upass_pipe6`, 2417,9605) or `upass_unicorn_doorl` (2375,9611) stalled at 2374,9638, and blamed a
wall line on z 9615 in `m37_150.jl2`. No map, loc, collision or script was wrong: the pocket is left
by the maze's five rock bridges `walkway_upass_narrow_mid_top` (op1 Cross, `blockwalk=1`, level-1
ground decor over the pits; `[oploc1,...]` `upass_obstacles.rs2:360` = LostCity_Server
`upass_obstacles.rs2:291`), at 2380,9634 / 2387,9631 / 2392,9627 / 2399,9632 / 2406,9637, each
clicked from x-1 and landing on x+1 ("...and make it."). The copies at 2396,9636 and 2406,9632 are
dead ends. A fall ("...and fall off it.") drops you one tile south (north under 2399,9632 and
2406,9632, the two `0_37_150_31_32`/`_38_32` copies) and every fall tile walks back. The same seven
copies stand at the same tiles in LostCity's `m37_150`, and every quest loc of the square (doors,
pipes, ledge, mud, railings) matches LostCity. The hop list is in gaps-world, "Underground Pass:
`walk_to` stalls under attack and stops at rock bridges". The Thieving-50 cage `cave_railings5`
2380,9619 is the guide's optional shortcut ("Navigate the maze, or use the shortcut to the south
with 50 Thieving", Regicide.java:517). Proof: `build/seam_state/matthew-mbp-m4-b48-seam2/scratch/maze_walk.lua`
20/20 with no goto after the well (row `repro.walk_to_pipe_stalls` reproduces the stall, then the
five bridges, the pipe and the door), and Regicide leg 2 with its maze marker replaced 26/0 to the
temple marker, 42/42 with seam1's temple route. Hand-off: `test/quests/wip/regicide/relay.md`,
"seam2 (maze)".

## Seam pass vm-b1-seam1 (2026-10-02, batch vm-b1)

(a) `other_floor` is an honest answer, not a driver seam: the game's own client cannot press a loc
on another plane (the pick drops it; an oploc resolves on the player's plane). The detail now names
the level to reach by the guide's route (verbs-pointer, "One named copy ... other floors";
conformance row `seam.click_loc_names_the_other_floor` checks the wording). The triage's two cases:
Cold War's course steps/stones/crushers are level 0 and their water is unwalkable (gaps-world,
"Penguin Agility Course"); Darkness of Hallowvale's shelf `myq3_agil_26_shelf_climb_down` at
3626,3221,2 presses first time from 3625,3221,2 (`teleport: 3625,3221,2 -> 3626,3221,1`), so its
parity miss was not reproducible.

(b) A POH room or furniture name (`poh_dummy_garden`, `poh_crafting_table_3`, ...) in an UNTYPED
position -- a command argument such as `poh_room_add`'s, the right side of `poh_room_get(...) =` --
compiles to the same-named OBJ since 37f8b2a84 gave dbrows their own symbol kind (`poh_dummy_garden`
is obj 8415 and dbrow 4167; dbrow 8415 is a sailing row). Every house was bare grass. FIXED for the
starter rooms and `~poh_restore_decorations` (OSRS-Content `poh_state.rs2` `[proc,poh_row]`, a
dbrow-typed parameter; `poh_construct.rs2`): pass the name through `~poh_row(...)`. NOT fixed: ~690
other POH sites (a room built through the add-room interface still stores an obj id) and ~160 dbrow
sites elsewhere -- `SSCOMPILE_AMBIGUOUS=all make -C src torirsserver-scripts` lists them; the
compiler fix is to hint `def_<type>` initialisers, assignments and `return(...)` from the declared
type (`src/serverscript/ssc_compile.c`). Saves written before carry obj ids; no migration.

(c) Cold War's physical is enforced at the Agility Instructor: `~coldwar_course_leg`
(`coldwar_outpost.rs2`) counts `%varb3305_peng_agility_state` 1 (stone 7), 2 (the last icicle
pillar, x=2662), 3 (the ice) in order at state 100, and the instructor refuses 100 -> 105 below 3
("You haven't finished the course yet, soldier. ..." -- port wording, no source gives the line),
then resets it to 0 (it is the Icelord count from 125). Before, the fence gate dropped a player
from the START into the finish pen and the instructor wrote 105 with no obstacle run. Sources: wiki
Cold War/Quick guide and Transcript:Cold War, wiki Penguin Agility Course, Quest Helper
`ColdWar.java` steps.put(100). A `goto_tile` into the finish (2652..2657 x 4038..4041) no longer
reaches 105: run the obstacles. Proof: `build/seam_state/vm-b1-seam1/agility/` (course 49/2 -- the
two FAILs a first-press icicle flake retried PASS and a scratch hollow walk; refusal 22/1).

(d) Royal Trouble's Giant Sea Snake is fought as the wiki has it: a stat block in
`royaltrouble.npc` (hp 100, att 170, str 90, def 160, ranged 130, +0 bonuses, crush every 4 ticks,
poison 9, 20% earth weakness -- wiki 'Giant Sea Snake', matching the cache's stat1..5), Slayer 40
checked on `[opnpc2]` and its `[apnpc2]` twin ("You need a Slayer level of 40 to attack this
creature."), melee when touching its 5x5 body and ranged otherwise (`[ai_opplayer2]`), and no
respawn once ROYAL_MISC is 120. It lies at the guide's 2615,10280 read as the MIDDLE of its body:
a Quest Helper `NpcStep` WorldPoint for a large npc is the tile under its centre (RuneLite
`Actor.getWorldLocation`), not the SW anchor `npc_add` takes -- subtract `(size-1)/2` per axis (SW
2613,10278). Melee from 2613..2617,10277; the heavy box lands on 2615,10277. Proof: snake 28/0,
the parity driver from leg 8 to quest complete 43/0 (12 lobsters at 70 combat: bring antipoison,
food or Protect from Melee). Open: a spell bypasses the Slayer gate (tree-wide, `~pvm_default_spell`).

## Seam pass vm-b1-seam2 (2026-10-02, batch vm-b1)

(a) Cold War's course water is walked and entered (gaps-world, "Penguin Agility Course"): the engine
splits the sea family into `TERRAIN_OCEAN_SEA` (blocks walking even without BLOCK) and
`TERRAIN_OCEAN_WADE` (overlay 537, the course only, walked by the cache's BLOCK); content (OSRS-Content a0bdfa073c)
climbs into the water from the ledge, out by the Ice steps, onto the first stone, and widens
`~coldwar_on_iceberg` to the level-0 water (before, entering it took the suit off and the
instructor said "Move along, civilian."). Selftests: sailing 575/0 on both sides; the full
selftest has the same 11 FAILs as its HEAD baseline. Proof: `build/seam_state/vm-b1-seam2/cw_water/`
(scratch 18/0 on the shared binary; a copy of coldwar.lua through the course to stage 105, 190/1
with the one FAIL an icicle press flake retried PASS).

(b) helper_coverage's `narrating_writes` flags any stage write whose branch has a `mes()`, then pins
it on whichever guide step shares two words with the branch: Larry's book hand-over and the KGP
debrief made `talkToThing` and `killIcelords` grade CONTENT_GAP although both were real (the Thing's
dialogue; three Icelords killed). Check the flagged file:line against the step before trusting it.
In an OSRS transcript `{{tact|receives=...}}` / `{{tact|gives=...}}` is a silent item transfer, no
game message: the invented "Larry gives you a clockwork book." (two branches) and "You hand over the
three mission reports." are removed (OSRS-Content a0bdfa073c). Open: the Thing's Talk-to is op3 in `all.npc` (op1 is
Shear) but content binds `[opnpc1,sheep_shearer_the_thing]`, so a test talks by pressing Shear (FIXED vm-b1-seam3 (c): Talk-to is op 3).

(c) Darkness of Hallowvale's Burgh inn Broken wall climbs (gaps-world, "Burgh de Rott inn"); the next
blocker is the pub trapdoor's `~climb_ladder(-1)` from plane 0 into an underground basement ("You
can't go any further."), the same shape as Seam pass 37 (b). FIXED vm-b1-seam3 (b).

(d) Not done this pass: ladder legs cut by route order (The Fremennik Isles; `ladder.py` and
relay.md unchanged) -- no fixer reported; the triage row stands for the next pass. FIXED
vm-b1-seam3 (a).

## Seam pass matthew-mbp-m4-b49-seam1 (2026-10-02, batch matthew-mbp-m4-b49)

(a) An IF1 button runs only `[if_button,<if>:<com>]`. A real click on an `if3=no` component sends
the op-less IF_BUTTON and `handle_if_button` runs `ToriRSServer_ScriptsRunIfButton(uid, 0)`, one
rung, as LostCity's `IfButtonHandler.ts:31` does; the numbered `[if_button<N>,...]` is the IF3 op
form. Ratcatchers' snake charm bound its ten buttons `[if_button1,ratcatcher_flute:*]`, so no press
played the tune; the content now binds `[if_button,...]` (`ratcatchers.rs2`), the dispatcher is
unchanged. Proof: scratch `flute_if1` 0 -> 27/0 (stage 105, "procession of rats"); the committed
test with its `t.blocked` replaced by the tune ran to the completion scroll, 211/0; conformance row
`seam.if1_button_unnumbered_trigger`. Still bound the dead way: `mcannon_broken_cannon.rs2:103-159`
(7, green only because content arms op 1 and the test presses op 1) and `grim_witchhouse.rs2:47-89`
(Grim Tales' piano, 15, unarmed); both FIXED in seam pass matthew-mbp-m4-b49-seam2 (a). Detail:
traps-23-33, Trap 33.

(b) Zembo had no spawn, script or stock, so Tai Bwo Wannai Trio's getRum answered `no_row zembo`.
Ported from LostCity_Server (m45_49.jm2 `0 45 7: zambo` = 2925,3143; `zambo.rs2`; `karamja.npc`
`[zambo]` wanderrange 3 and shop params 700/1000/20; `karamja.inv` `[boozeshop]`) into
`quest_tbwt/` (`tbwt_zembo.spawn`, `.inv`, `.rs2`; the barcrawl arm deferred like
`deadmans_bartender.rs2`). Proof: scratch `seam1_zembo_b` 17/17, the parked file with the buy rows
47/0 to the end of leg 1; conformance row `seam.zembo_boozeshop_sells_rum`. Measured on the way:
`use_item_on_item(a, b)` fires `[opheldu,b]` first (gaps-combat, corrected).

(c) Underground Pass's fall pocket is not sealed: five rockslides and a rock pile, no content change
(gaps-world, "the fall pocket"). The b47 file with its leg-4 GUIDE-GAP replaced by the driven block
ran rows 1-109 PASS and `helper_coverage` grades `leaveFallArea` DRIVEN; it stops next at
`navigateMaze`, where the extra slide rolls shift the account-seeded RNG and a fall off bridge
2406,9637 lands in a pit (2406,9635) with no known walk back (FIXED b52-seam3 (a): the way back is
bridge 2406,9632 crossed westward). The woodplank is a ground spawn
(m38_151.spawn:35, 2435,9726), reached after the bridge, not by `::give`.

(d) Troll Romance: Arrg's damage is the OSRS wiki's and the b47 death was test-side (verbs-combat,
Arrg). The sled rides play and `t.cutscene.await` sees 5/5 keyframes each; they never started
because Curse of Arrav's name binding on the Trollweiss cave mouth and crevice shadowed the maplink
(gaps-world, "a name binding shadows the maplink"). FIXED by the closer in `curseofarrav.rs2` (the
fixer proved it on a private pack, before 12/6, after 18/18, control 12/6; running.md). The parked
file with the armour kit ran 97/97 to the scroll on the shared pack (`closer_tl_shared`).

Not run this pass: the design seam "a travel goto across a guide-named obstacle is CHEAT" (Regicide
9c28a4e0c) had no fixer; `helper_coverage.py` is unchanged. FIXED in seam pass
matthew-mbp-m4-b49-seam2 (c).

## Seam pass matthew-mbp-m4-b49-seam2 (2026-10-02, batch matthew-mbp-m4-b49)

(a) Dwarf Cannon's toolkit and Grim Tales' piano take the IF1 press. Both interfaces are `if3=no`,
so a click sends the op-less IF_BUTTON and only `[if_button,...]` runs (LostCity_Server
`IfButtonHandler.ts:31`); the content bound them `[if_button1,...]`. Rebound: 7 in
`mcannon_broken_cannon.rs2`, 15 in `grim_witchhouse.rs2`; `mcannon.lua`'s toolkit loop presses
op 0. Before: an op-0 press set nothing (`seam2_mcannon_before`, `seam2_piano_before`: "no trigger
for [if_button0,grim_piano:ue] or [if_button,grim_piano:ue]"). After: scratch 17/0 and 22/0, and
mcannon 114/0 to the scroll. Conformance row `seam.if1_toolkit_and_piano_op0`. Trap 33.

(b) Tai Bwo Wannai Trio's XP is CLAIMED FROM THE BROTHERS, not awarded at completion. The b49
review read "the quest gives none of its documented XP"; the OSRS wiki ("Tai Bwo Wannai Trio",
oldid 15265886, Rewards: "claimed upon speaking to Tinsay / Tiadeche / Tamayu after quest
completion"), Quest Helper (`TaiBwoWannaiTrio.java` talkToTimfrakuEnd) and LostCity's
`tbwt_{tinsay,tiadeche,tamayu}_final.rs2` all pay it when you talk to each brother in the village
afterwards, and the port already does. So at the scroll every skill delta is +0; assert
`t.skill.expect_gain` after each claim (Tinsay 5000 Cooking, Tiadeche 5000 Fishing, Tamayu 2500
Attack + 2500 Strength and the kp rune spear; the npcs are the `*_multinpc_house` symbols). The
scroll itself now reads the wiki's `File:Tai_Bwo_Wannai_Trio_reward_scroll.png`: `2 Quest Points |
5000 Fishing XP | 5000 Cooking XP | 2500 Attack XP | 2500 Strength XP`, karambwan model, no coins
line (the 2,000 coins are Timfraku's own `inv_add`). That string is pinned by
`tools/check_quest_combat_contract.py`, so it changed in one commit with the pin: a content seam
whose string a contract check pins cannot land from a content-only worker -- name the pin in the
triage's files. Proof: scratch `close2_tbwt_scroll` 36/0 on the shared pack; a copy of the working
`tbwt.lua` with the claim legs 211/0 (relay in `wip/tbwt`). The vessel now loads ONE karambwanji
(content-gaps, Tai Bwo Wannai Trio).

(c) A travel `goto_tile` across a route obstacle the guide names reads CHEAT in `helper_coverage`,
even when another row pressed the same loc elsewhere, and a crossing row that passed on an earlier
attempt's server line reads CHEAT too (coverage-and-gate, "lands at ... without pressing the
rockslide"). The three reverted Regicide runs go FULL/FULL/CONTENT_GAP -> TEST_GAP/TEST_GAP/MIXED on
the hops their samplers named; the green regicide and all 99 committed greens are unchanged step by
step. `--ledger` grades another run's ledger.

## Seam pass matthew-mbp-m4-b50-seam1 (2026-10-02, batch matthew-mbp-m4-b50)

(a) A goto row that names where it left from (`at <landing> from <departure>`) is proved but HELD
BACK: with it, ten committed greens read TEST_GAP on goto hops past guide obstacles
(coverage-and-gate, "The departure tile"). The driver still writes `at <landing>`.

(b) Enlightened Journey's hand-ins all have sources now: willow trees and branches in tree patches,
and Fill on vegetable sacks (content-gaps, "Enlightened Journey: no willow branch source"; relay in
`wip/enlightenedjourney`).

(c) Bear Your Soul's way into the deep Taverley Dungeon was already in the content: the dusty key,
the 70 Agility pipe or the 80 Agility spikes (gaps-world, "Taverley Dungeon deep area"). No content
change; relay in `wip/bearyoursoul`.

(d) A ground spawn a quest needs goes into `tools/gen_spawns.py`, not the generated `.spawn` file:
`OBJ_SPAWN_ADDITIONS` / `OBJ_SPAWN_EXCLUSIONS` / `OBJ_SPAWN_RELOCATIONS`, each with its source.
`m19_57.spawn` is generated from dennisdev/rs-map-viewer (recipe `docs/ITEM_AND_NPCS.md` s7);
Getting Ahead's knife, bucket, dye exclusion and moved upstairs pot are now in those tables, and
the regenerated file is `cmp`-identical to the committed one. Regenerate into a SCRATCH `--out` and
copy only the square you changed: the script unlinks every `.spawn` first, and four other squares
(`m40_51` pigeons, `m42_55` murder weapon, `m45_159` beer glass, `m45_55` cannonballs) are still
hand edits a full regeneration would drop. A spawn counterfactual runs without touching the shared
tree: a symlink-farm content root with one `.spawn` swapped (old mtime) and
`TORIRSSERVER_CONTENT=<farm>` on `run.py --script`.

(e) "`::ejmodels` crashes above model 19729" was the client's interface-model map, not the id. The
`uitree_scene_bridge` cache-id -> scene-id maps had a fixed 256 capacity and never grew, so the 256th
distinct interface model (or npc chathead, or obj model) in one session was a null store in
`UITreeSceneBridge_EnsureModel` (the OPT build drops the assert). They now double at 75% load. An
`if_setmodel` widget whose model is not composited (or not in the cache) draws nothing
(verbs-ui-and-npc: `t.ui.model_pose`). The repro (`::ejmodelsat`, a scratch debugproc) is not in the
pack, so there is no conformance row.

(f) Eagles' Peak's bronze-room Pedestal is not in the real cache's map either; the runtime stand-up
stays (content-gaps, "A quest loc absent from `maps/*.jl2`").

(g) `if_setevents` on an IF1 component (`if3=no`, `buttontype` != 0) does nothing in this engine:
the client makes the button clickable from its buttontype (`src/ui/uitree_input.c:72`) and the
server answers the op-less IF_BUTTON with `[if_button,...]` (`torirs_server_world.c`, op 0). Dwarf
Cannon's seven dead `if_setevents(..., ^if_event_op1)` lines are deleted (mcannon 114/0, identical
row for row). Content should not arm IF1 buttons. Trap 33.

## Seam pass vm-b1-seam4 (2026-10-02, batch vm-b1)

(a) `helper_coverage` grades a `goto`/`walk`/`travel`-prefixed row named after a guide step as that
step's action, unless its source line is itself a teleport (`goToMines`; v3 c458a4d92;
coverage-and-gate, "CONTENT_GAP at an unrelated line on a `goToX` step"). `--all-green` on 100 tests
changed no verdict. Five step counts moved and all stayed FULL: arthur climbDownFaye1/2 and
thefremennikisles travelToNeitiznotAfterDecree went DRIVEN->TRAVEL; fenkenstrain goToHeadGrave,
losttribe goToDukeWithSilverware and viking resetSwensen went to DRIVEN.

(b) `travel_op_conflict` accepts a press on an op that the guide text names in a later clause
(`Grader.clause_verbs`: "Climb up the walls and search the marked floor" -> search). Darkness of
Hallowvale's `kickBoard` Search press grades DRIVEN (v3 54bd5140f; coverage-and-gate, "Gate RED on
the last leg's"). `--all-green` on v3: no step class changed.

(c) The Meiyerditch to Castle Drakan wall shortcut lands on 3595,3312,0 from a real `click_loc` (a
name-bound oploc1 in `doh_castle.rs2`; gaps-world, "Meiyerditch"). A Darkness of Hallowvale copy
with `click_loc` in place of `t.drive.op` ran 394/0 to quest complete.

## Seam pass vm-b1-seam3 (2026-10-02, batch vm-b1)

(a) Relay legs can be cut by route: `docs/quests/ladders/<test_id>.legs` holds one stage range per
leg in walking order, and `ladder.py` (`<id>`, `--leg K`, `--json`, `--write`) and `fail.py --leg K`
follow it (relay.md, "The legs follow the guide's order, and the route does not"; v3 1c64900e9). A
quest without the file is cut exactly as before (byte-identical output). The Fremennik Isles has
`0-60 90-150 160-210 230-275 280-290 300-`; its new leg 1 (talkToMord .. tellSlugReport1) starts
from `::fremennikisles` at stage 0 and drove talkToMord -> stage 5, the Jatizso ferry and King
Gjuki's hall door (12/0). Jatizso route: from the dock 2420,3782 walk to 2412,3796, open
`frisd_outer_city_wall_door_left` (2413,3797), walk to 2407,3807, open `frisd_town_wall_door`
(2407,3806): Gjuki's hall is entered from the NORTH; walking straight north from the gate stalls
at the jester chest (2407,3800).

(b) A generated ladder binding that cannot reach an underground region is replaced, not
overridden: the script compiler keeps ONE body per trigger, so a quest that takes over a trapdoor
bound in `ladders_stairs/scripts/climb_shared.rs2` deletes the generated line there and leaves an
ownership comment (`tools/ladder_import.py`'s `read_other_rs2_triggers` will not re-emit it), as
the Sanguinesti rug trapdoor (ec0ea5f7ce) and now `burgh_inn_trapdoor_open` do. The underworld step
is the reference trapdoor's: your own tile plus or minus 6400 along z
(`~climb_ladder_to(movecoord(coord, 0, 0, 6400), false)`; `general_use/scripts/trapdoors.rs2`).
Burgh de Rott's pub trapdoor and hideout ladder: gaps-world, "Burgh de Rott inn" (OSRS-Content
dca1c3a937; scratch 16/0, a Darkness of Hallowvale copy 27/0 to stage 40).

(c) Cold War's Thing (`sheep_shearer_the_thing`) talks on op 3 (Talk-to, as `all.npc` has it);
op 1 Shear answers "You need a pair of shears to shear this sheep." or, with shears, "The sheep
manages to get away from you!" and gives no wool (OSRS wiki 'Sheep (penguins)'; OSRS-Content
dca1c3a937). A `talk_to(..., 1)` now reads `chat_message: no dialogue in 5 tick(s), content line
'You need a pair of shears...'` and the stage stays 45. Lumbridge Larry (`peng_multi_larry_lumb`)
is only placed while `varb3298_peng_multi_larry` is 1: a scratch that stages state 45 sets it too.

(d) A regression run in a seam pass takes `--no-publish`: two fixers ran `run.py cooks_assistant`
and `druid` without it and republished `selftest/quests/quest_{cook,druid}/play` in OSRS-Content
(restored by hand). Seam pass 17 has the rule; running.md now says it at the run command.

## Seam pass matthew-mbp-m4-b51-seam1 (2026-10-02, batch matthew-mbp-m4-b51)

(a) Two goto hops the grader read FULL now read CHEAT (coverage-and-gate, "outside the building ...
the guide's way in is ..."). `door_entries`: a goto from outside a building into the room a
ConditionalStep's single-door default opens on, with no press of that door (Black Knights'
Fortress's Falador-to-entrance hop past `bkfortressdoor1`, charged to `enterFortress`).
`room_exits`: a goto out of a room an obstacle step's door opens on, to a nearby tile in no zone,
after which the room's own step is driven there (Heroes' Quest's hop out of the secret room to
melee Grip, charged to `killGrip`). Of the committed greens only mourningsendparti changes: row 172
`goto-cookToxin` teleports from Rimmington into the Mourner HQ past the disguise-gated
`mournerstewdoor`, a real cheat, reopened. A room sealed by walls no guide step names is still not
judged: the grader has no reader of the map's walls (known gap, same section).

(b) Rum Deal's pier gate (`deal_gate_closed`, 2120,5098,0) opened in place at its closed angle, so
the wall stayed and the walk north stalled at 2120,5098 after "You open the gate.". From
`^deal_get_water` on, the press now goes through the shared `~door_open_active` (doors.loc pairs it
with `deal_gate_open`): the gate swings off the wall line like every door and the north island is
walkable (Quest Helper RumDeal.java:358 `openGate`, then `northIsland` z >= 5099; OSRS wiki Rum Deal
oldid 15315444). Below that stage it still answers with its lock line. Route and proof:
the green `test/quests/rumdeal.lua` rows `openGate.walk_north`..`lake.fill` (walk in hops; a 33-tile
`walk_to` to the lake is refused).

(c) Npcs whose dump id the cache gave to newer content were dropped by `tools/gen_spawns.py` as
name drift and are back: `NPC_SPAWN_ID_CORRECTIONS` adds Mazchna (3511,3509), Duradel
(2869,2982,1), Harrallak, Ghommal and Sloane (Warriors' Guild), Mac and Patchy, each to the named
symbol content binds (`slayer_master_2_mazchna`, `warguild_ghommal_npc`, ...); `NPC_NAME_ALIASES`
keeps six same-id renames (`warrior_woman` "Warrior", `jungle_savage` "Tormented Warrior",
`feud_desert_snake` "Snake", the two Ethereal Beings, `hosidius_chief_farmer` "Dale"). Each row
cites the OSRS wiki oldid whose infobox holds the id and whose map marker holds the tile. Drift
fell 219 -> 145 rows; 20 squares gained spawns (never `m45_159`). A scratch `npc.by_name` probe
went 12/12 `no_row` -> 55/55 PASS, and Mazchna and Duradel open their slayer pages. Before you call
a drifted name absent, check every `.spawn` under `server/scripts` (quest-local files too:
Traiborn, Hassan, Aris and Zembo stand in `quest_demon.spawn`, `quest_prince.spawn`,
`tbwt_zembo.spawn`) and every `npc_add`, under the cache's name as well as the dump's. Npcs left
out on purpose (quest-spawned by `npc_add`, Kourend favour-era, Sailing fauna, the reworked
Sophanem) are listed in `docs/ITEM_AND_NPCS.md` section 3.

## Seam pass matthew-mbp-m4-b51-seam2 (2026-10-02, batch matthew-mbp-m4-b51)

(a) Heroes' Quest's `killGrip` and `getCandlestick` are the partner's half of a real two-player leg
(Quest Helper HeroesQuest.java:411 "Wait for your partner to lure Grip into the room next to yours,
and kill him with magic/ranged"; :412 "Get your candlestick from your partner."). A solo Phoenix
player cannot do either half: Grip's drinks cabinet (2775,3196), his keyring (dropped on his own
tile) and the treasure room are all inside the mansion behind `garvdoor`, which only a Black Arm
player at `^hero_blackarm_mansion_unlocked` passes (`garv.rs2` `[label,attempt_open_brimhaven_mansion_door]`),
and LostCity's `[oplocu,pete_treasuredoor]` refuses below `^hero_blackarm_id_papers_given`. Two
test affordances now stand in for the partner, beside `::hero_partner` and under the same
2026-09-23 owner ruling (`docs/QUEST_SERVER_CHEATS.md` section D): `::hero_partner_lure` does
exactly `brimhaven_scarface_mansion.rs2` `[label,summon_grip]` case 1 on Grip (walk to 2777,3198,
"Stay out of my drinks cabinet!", 6-tick hold) and nothing else; `::hero_partner_candlestick`
trades one `petecandlestick`, only at `^hero_phoenix_killed_grip` (your own kill credit, written by
`[ai_queue3,grip]`) and never a second. The player still kills Grip for real through the slit
(gaps-combat: Shooting through an arrow slit). Declare the trade `-- PARTNER: getCandlestick
::hero_partner_candlestick <reason>` (helper_coverage verifies it against the cheats table). Rows:
the green `test/quests/hero.lua` `inSecretRoom`..`getCandlestick`; 125/0 to the scroll, FULL 43.

(b) A goto to 2780,3197 for Heroes' Quest's side door lands INSIDE the secret room (QH `secretRoom`
2780..2782 x 3197..3198, HeroesQuest.java:258), past the door; `useKeyOnSideDoor` then walks you
OUT to the garden (ledger `2780,3197,0 -> 2781,3196,0`). Goto the garden side, 2781,3196, and the
key lets you in (`2781,3196,0 -> 2781,3197,0`).

(c) MEASURED AND FIXED in seam pass matthew-mbp-m4-b52-seam1 (b): `npc_setmode(null)` never cut the
walk; it wiped the step Grip had just taken, so the client drew him one tile short (3197 for 3198).
What is left is LostCity behaviour: from a far wander tile the 6-tick hold is too short and the
wander roll replaces the rest of the walk. Original note: one `::hero_partner_lure` does not always
bring Grip into the cabinet room. From a wander tile (2774,3189) the 6-tick hold ended with him at
2777,3194 or 2777,3195; a second search reached 2777,3197, never 3198 in either run. Search again
(up to three times), as a real partner re-searches the open cabinet, and check Grip's tile before
attacking.

## Seam pass matthew-mbp-m4-b52-seam1 (2026-10-02, batch matthew-mbp-m4-b52)

(a) `t.player.drop` grades on the BACKPACK falling, with at least one of the item on the player's own
tile as the supporting half. It no longer grades on a ground count rising. The client keeps ONE ground
row per (tile, obj id), and an OBJ_ADD for an id already on the tile overwrites that row's count
(`App_WorldObjStackAdd`, src/app/app_world_rebuild.c:172-180). So a second identical non-stackable
copy dropped on its twin's tile never raised anything the client shows, and a real drop read
`timeout ... backpack 1 -> 0, ground 1` (legends b51 `makeBowl.drop-spare-bar-2`). The detail is
now `drop <item>: backpack B -> A, ground on the player's tile G0 -> G1 (N row(s))`, and G1 can equal
G0 on a second copy. Conformance `seam.drop_second_copy_on_one_tile`. FIXED in seam pass
matthew-mbp-m4-b53-seam1 (a), which removed the client merge: after two logs land on one tile and you
pick one up, the second is still drawn and takeable, and G1 now rises on a second copy. (Before
that, the client showed NO log after the first pick and a second pick found no menu row,
`dropseam_pickone` rows 4-5.)

(b) `npc_setmode(none|null)` no longer clears `step_dir` (torirs_server_scripts.c SS_OP_NPC_SETMODE).
LostCity never touches the step there either: `null` is `resetDefaults()` (NpcOps.ts:216-218,
Npc.ts:424-436), and that never clears the waypoint `npc_walk` queued (NpcOps.ts:466-469). Phase 4
moves npcs before phase 5 resumes a `p_delay`ed script. So the old clear erased the step an npc had
just taken: NPC_INFO never sent it, and every client drew the npc one tile short for as long as it
stood there (Heroes' Quest's lure: server 2777,3198, client 2777,3197). Selftest stanza
"npc_setmode(null) mid-walk keeps the walk and its step". `step_dir` and `run_dir` record the step
already taken, never a stop switch. A related read trap: an npc tile read right at a script's
`mes()` line can be one tile behind, because the message lands before that tick's NPC_INFO. Wait
`t.ticks(1)` before asserting an npc's tile after a scripted walk.

(c) Below Ice Mountain's `bim_golem_cleanup` and pillar rockfall now pick the Ancient Guardian by
OWNER (`npc_findall` + `npc_owner = uid`, the gauntlet_progress.rs2:97 test), never the copy nearest
the spawn tile; before this fix, an unowned `::spawn bim_golem_boss` in the public hall was deleted by
the player's cleanup. Engine fact: `npc_find`/`npc_findall`/`npc_findnext` already skip an npc
owned by ANOTHER live player (`ToriRSServer_WorldNpcVisibleTo`), but an unowned copy of the same
type is visible to everyone, so content still tests `npc_owner = uid` for owner-private actors. A
driver fact: the client npc row's `slot` (`t.npc.tiles`, `await_present`) is not stable across a
teleport out of view and back (one guardian read slot 55, then 81/112/142), so identify a copy by
its tile, not its slot. OPEN (content): the hall's four structural pillars never rise for the client
after the entrance spawn (`loc_near bim_boss_rock` fails; `screen_position: no loc 41458`), so the
guide's mine-the-pillars alternative cannot be driven. belowicemountain.lua fights the guardian and
is unaffected.

(d) Porcine of Interest's Sourhog spit lands through the shared `[queue,combat_damage_player]`, and
it now uses the wiki's numbers: 20-30 damage and Attack and Defence drained by 90% of current
(OSRS wiki Sourhog oldid 15275486). It also queues `playerhit_n_retaliate`. Worn reinforced goggles
still cancel it. The one-in-four spit rate is an approximation. Two OPEN engine facts were found
proving it, and both bite any scratch row:
- ANY xp gain snaps a drained stat back to its base level (torirs_server_combat.c:1174
  `stat_boosted < stat_level`), which LostCity's `addXp` does not do (Player.ts:1840-1851). An Attack-xp
  hit cancels the Sourhog drain. Keep xp off the stat a drain row asserts (aggressive style pays
  Strength only). FIXED b53-seam2: a drained stat now stays drained through xp (seam pass
  matthew-mbp-m4-b53-seam2 (a)).
- A boss raised with `npc_setowner` + `npc_setmode(opplayer2)` (`porcine_sourhog_second`) does not walk
  to an idle owner. Engage it with `t.player.attack` before waiting on its attacks.
Also: a 1-tick `t.msg.await` loop interleaved with skill reads can miss a line that lands between
calls. Poll the effect each tick, then `t.msg.expect` the line from the ring.

## Seam pass matthew-mbp-m4-b52-seam3 (2026-10-02, batch matthew-mbp-m4-b52)

(a) Every fall off an Underground Pass maze bridge has a walk back. Nothing is sealed and no map
change was needed. LostCity `maps/m37_150.jm2` and our `m37_150.jl2` place the same SEVEN
`walkway_upass_narrow_mid_top`: 2380,9634 2387,9631 2392,9627 2396,9636 2399,9632 2406,9632
2406,9637. The route crosses five of them (all but 2396,9636 and 2406,9632). `0_37_150_38_32` is
2406,9632, not the route's last bridge. A failed `stat_random(agility,160,300)` lands you TWO tiles
off the bridge (LostCity `upass_obstacles.rs2:308-321`): `p_exactmove` already teleports to its end
tile (LostCity engine `Player.ts:2109-2110`), then `p_teleport` steps one more. The fall goes south,
or north for 2399,9632 and 2406,9632. Live landings: 2380,9632 2387,9629 2399,9634 2406,9635
(2406,9634 from 2406,9632).
- Bridges 1-3 land back in the maze start area. Walk 2373,9634 -> 2379,9634 and cross 1, 2, 3 again.
- Bridge 4 (2399,9632) lands between bridges 2 and 3. Cross 3, then 4.
- Bridge 5 (2406,9637) lands in the pit 2406..2410,9632..9635. It is walled in on foot, but it is
  the east side of bridge 2406,9632. Walk to 2407,9632 and cross 2406,9632 west to 2405,9632 (a fall
  there lands back in the pit). Then go 2403,9632 -> 2403,9637 -> 2405,9637 and cross bridge 5 again.
`test/quests/upass.lua` (leg 4; it was `wip/upass/seam3_maze.lua`) drives this as a `walk-navigateMaze-<n>` row per hop and a
`navigateMaze-<n>-bridgeK` / `-pitExit-2406-9632` row per press, judged by tile (crossed or fell).
It has no `goto_tile`. Proof: scratch agility-1 runs 40/40 and 375/375 exercised every recovery;
the full file ran 289/289 to the scroll (relay.md, seam3 section; `wip/upass/` was removed once green, read it at 2a404b410).

(b) To ask "is this pocket sealed?" before any run, build a collision model from the `.jm2`/`.jl2`
the way LostCity's `GameMap.ts:219-281` does: land flag 1, LINK_BELOW 2 lifting level-1 walkways onto
level 0, and shape 22 blocking only when active. Then BFS it. The model for (a) is
`build/seam_state/matthew-mbp-m4-b52-seam3/scratch/maze_graph.py` (`python3 maze_graph.py
lostcity|ours`), with `maze_bfs.py` beside it.

(c) A relay leg that `::give`s food at its start should cap the give at the free slots minus the
leg's pickups. An RNG shift upstream changes how much food an earlier leg eats. Underground Pass
leg 7 then ended on 19 items, leg 8's `::give lobster 15` filled the pack, and Klank's gauntlets were
lost to "Your inventory is full." Count free slots with `t.inv.slot` (an empty slot reads name `""`)
and give `min(15, free - 3)`.

## Seam pass matthew-mbp-m4-b53-seam1 (2026-10-02, batch matthew-mbp-m4-b53)

(a) The client keeps a LIST of ground objs per tile. Every OBJ_ADD is a new row, even for an obj id
the tile already holds, and OBJ_DEL removes the FIRST row of that id. That is what both references
do: LostCity_JavaClient `Client.java:8206-8213` pushes a new ClientObj per OBJ_ADD and `:8222-8228`
unlinks the first one of the id; the rev-239 deob appends a TileItem (`Statics.method1385`) and
unlinks one (`method6879`). `App_WorldObjStackAdd` (src/app/app_world_rebuild.c) used to find the
tile's row of the id and overwrite its count, so two identical drops were one row, and the first
Take removed it while the server still held the second copy. Now two logs dropped on one tile are
two rows with two Takes (`build/quest_gate/b53s1_pick2_before` 4 FAIL -> `b53s1_pick2_after` 7/0;
conformance `seam.two_copies_one_tile_both_takeable`). A count-0 add (rev 239's ObjEnabledOps,
passed through as OBJ_REVEAL) names the existing row and adds nothing. OPEN (client): OBJ_DEL
carries no count, so with two stacks of one STACKABLE id with different counts on one tile, the
deob (which matches id and quantity) and ours can remove different stacks.

(b) The Chaos Altar is a four-level ladder maze, not a misplaced altar. The chaos talisman/tiara
lands you on level 3 (2275,4847) and the altar is on level 0 (2270,4841). LostCity agrees on both
(`runecraft.dbrow:113` `enter_coord,3_35_75_35_47`; `maps/m35_75.jm2` LOC `0 30 41: 2487 10`, 2487 =
chaos_altar), and the OSRS wiki says so (Chaos Altar oldid 15350445: "players must navigate four
levels of a chaotic maze"). Route: three `laddertop` Climb-downs, plain travel:
`click_loc("laddertop", 1, { at = { 2255, 4829, 3 } })` -> 2255,4830,2; `{ 2275, 4834, 2 }` ->
2274,4834,1; `{ 2259, 4845, 1 }` -> 2258,4845,0. From level 3, `click_loc("chaos_altar")` answers
`covered ... menu has no row for it`, and `t.world.loc_near("chaos_altar", r)` answers ok because
it ignores the plane (Trap 29), so assert the floor with `t.world.tile().level`. Proof:
`build/quest_gate/wlb_altar_route_s3` 13/13 (chaos runes crafted) and a What Lies Below copy 57/0
through `useWandOnAltar`. Route rows: `test/quests/wip/whatliesbelow/relay.md`.

(c) (The child-record spawn and the stage-8 gap below are superseded: FIXED b53-seam2, seam pass
matthew-mbp-m4-b53-seam2 (b) -- the rows are the shells now.) The Ribbiting Tale's Marcellus and
frogs (Locus Oasis) are placed by a quest-local
`quest_ribbitingtale/configs/ribbitingtale.spawn`, not by `gen_spawns.py`. The xrsps dump has no
npc anywhere near (x 1660-1720, z 2960-3010), so there was no dump row to correct. Rule: an npc the
dump lacks ENTIRELY goes in a quest-local `<quest>.spawn` with cited tiles (idesofmilk,
bearyoursoul, ribbitingtale); `NPC_SPAWN_ADDITIONS`/`NPC_SPAWN_ID_CORRECTIONS` are for world
npcs whose dump row drifted. The rows name the op-carrying CHILD records, not the cache's multinpc
shells (13401-13405), because ribbitingtale.rs2 binds its ops on the children (Trap 19):
`frog_quest_marcellus_normal` 1683,2973, `frog_quest_gary_unnamed` 1695,2996,
`frog_quest_sue_unnamed` 1695,2995, `frog_quest_dave_named` 1697,2984, `frog_quest_jane_named`
1696,2983 (Quest Helper TheRibbitingTaleOfALilyPadLabourDispute.java :117/:120/:126; wiki
Marcellus oldid 15319067, Gary 15197135, Sue 15197137, Dave 15197136, Jane 15197139). Cuthbert is
not placed. Consequences: the blue frogs always read "Frog" and Dave/Jane always their names (the
shells' stage forms do not show). A copy of the test starts the quest by click and reaches stage 8
(`build/quest_gate/seam1_ribbit_copy` 21/0). OPEN (content): at stage 8 Dave and Jane only say
"Hello there!": `[label,ribbit_yellow_talk]` guards the election talk on `>= ^ribbit_chop` (10)
and nothing but `[debugproc,ribbitrun]` writes 10.

(d) The Queen of Thieves' tent doorway `piscquest_tentdoor` (1765,10149, a wall on the tile's north
edge) is scripted: `[oploc1,piscquest_tentdoor]` in `queenofthieves_locs.rs2` walks you through with
the shared `@door_walkthrough_try`. Going in is refused before Devan's go-ahead (stage <
`^qot_queen` = 8); going out is never refused. Sources for the gate: wiki
Transcript:The_Queen_of_Thieves oldid 14962997 ("You should head on in and speak to her"),
The_Queen_of_Thieves oldid 15352295, Quest Helper TheQueenOfThieves.java:104-107. The refusal line
"You should speak to Devan Rutter before going in there." is port wording (no source records it).
Drive it with `t.exec("enterTent", t.player.click_loc, "piscquest_tentdoor", 1)` from outside, then
assert 1765,10150; the same click from inside lands on 1765,10149. Proof: pre-fix pack
`qot_tentdoor_pre` "I can't reach that!" at the Queen; fixed `qot_tentdoor_post` 14/0; the
reverted test with its tent `goto_tile`s replaced by doorway clicks ran 78/78. Rows:
`test/quests/wip/queenofthieves/relay.md`. This resolves Sample matthew-mbp-m4-b53 (a).

(e) To prove a content failure on the PRE-fix pack without mutating the shared tree: rsync
`server/scripts` to the scratchpad (leave out png/bmp/build/selftest and the lane `.rs2` files,
keep the lane `.constant` files), put HEAD's version of the changed file there, run
`src/build_opt/sscompile --src <copy> --out <dir> --content-root <content> --pack <content>/pack
--pack <content>/configs`, then run the scratch test with `TORIRSSERVER_SCRIPTS=<dir>`.
`client.log` names the pack it loaded.

(f) FIXED b53-seam2 (seam pass matthew-mbp-m4-b53-seam2 (a)). Was NOT LANDED: the xp-grant drain snap (seam pass matthew-mbp-m4-b52-seam1 (d)). A fix that
follows LostCity `Player.ts:1841-1851` (a drained stat stays drained through an xp grant; a
level-up replenishes it by the levels gained) was written and selftested, but it turned committed
green Desert Treasure RED: the Ice Path cold (deserttreasure.rs2:1222-1247) then really drains
Magic to 57/99 before Kamil, under Fire Blast's 59 ("Your Magic level is not high enough for this
spell."). The closer reverted it. It must land in one pass together with a deserttreasure.lua leg-5
restore (restore potions as a bring-along) and a free backpack slot before the child troll. The
patch and the evidence are in `build/seam_state/matthew-mbp-m4-b53-seam1/drain_fix_carried.patch`
and `build/seam_state/next-seam-carry.md` item 4.

## Seam pass matthew-mbp-m4-b53-seam2 (2026-10-02, batch matthew-mbp-m4-b53)

(a) A drained stat stays drained through an xp grant. `ToriRSServer_CombatAddXp`
(src/torirsserver/torirs_server_combat.c) follows LostCity `Player.ts:1841-1851` (addXp): the
current level moves with the base only while the two are equal, a level-up adds the levels gained to
a drained stat (6/60 -> 7/61, never to the base), and a boost survives a grant. Before, the next xp
grant snapped any drained stat back to its base, so one hit cancelled the Ice Path cold, the
Sourhog spit or any `stat_sub`. Proof: `build/quest_gate/b53s2_drain_before` FAIL `attack 60/60` ->
`b53s2_drain_after` 7/0 `attack 6/60`; the world selftest's drain stanza; conformance
`seam.drain_survives_xp_gain`. It landed with Desert Treasure's leg 5 edit (gaps-combat: "Your
Magic level is not high enough" after a draining walk). Supersedes b52-seam1 (d)'s first bullet and
b53-seam1 (f). Still open: `::maxstats` (stat_advance) leaves a drained stat drained by its deficit,
as LostCity's `::maxme` does; and the `[advancestat]` trigger still runs before the current level
is updated (LostCity updates first).

(b) The Ribbiting Tale's npcs are placed as the cache's multinpc SHELLS
(`ribbitingtale.spawn`: `frog_quest_marcellus`/`_gary`/`_sue`/`_dave`/`_jane`, 13401-13405,
configs/all.npc:429457-429520), and Talk-to is bound on the shells (Trap 19). The live child
follows `varb9844_frog_quest` (`varb9845_frog_quest_patch_unlocked` for Marcellus), read by VALUE
with the last rung as the default for every larger value (Trap 28): Gary/Sue are "Frog" before
stage 4, Dave/Jane a Talk-less "Frog" before stage 4, and Marcellus turns into the farmer (npc 12936)
at completion. Drive them by the SHELL symbol: a child symbol stops resolving once the varbit picks
another child (verbs-pointer: `t.player.talk_to`). `[label,ribbit_yellow_talk]` is rebuilt from the
wiki Transcript:The_Ribbiting_Tale_of_a_Lily_Pad_Labour_Dispute (oldid 15005191); stage 8 is the
election talk (Quest Helper talkToYellowFrogs, TheRibbitingTaleOfALilyPadLabourDispute.java:75/:126)
and writes `^ribbit_chop` (10). Proof: `build/quest_gate/seam2_ribbit_copy3` 97/0 to the scroll
(`quest.stage.chop` 10, `names.*` rows per stage). Cuthbert, Lord of Dread kills a fresh 10 hp
character: bring 40 combat stats, a wielded mithril scimitar and lobsters. Rows:
`test/quests/wip/ribbitingtale/relay.md`. OPEN (content, no guide step): the "It's all sorted"
branch at stage 14 is shadowed, the lily pad refusal at stages 8-10 is not authored, and the
farmer's Pay/Trade have no trigger.

(c) The Queen of Thieves' doorway refusal "You should speak to Devan Rutter before going in there."
stays port wording: a search on 2026-10-02 found no source for what the doorway says (transcripts
14962997/14785766, The_Queen_of_Thieves 15352295 and Quick_guide 15013569, Devan_Rutter 15353387,
The_Warrens 15317408, Doorway 15335504, Quest Helper). Devan_Rutter oldid 15353387 is a second
source for the gate itself ("will not let players enter until they prove their loyalty by killing
Conrad King"). The searched list is in the comment above `[oploc1,piscquest_tentdoor]`.

(d) Fetch a cited wiki oldid with
`curl -A '<ua>' 'https://oldschool.runescape.wiki/api.php?action=query&prop=revisions&revids=<oldid>&rvprop=content|ids&rvslots=main&format=json&formatversion=2'`;
`index.php?action=raw` now returns a Cloudflare challenge page to curl.

## Seam pass matthew-mbp-m4-b53-seam3 (2026-10-02, batch matthew-mbp-m4-b53)

(a) Contact!'s Maisa is talked to ACROSS the chasm; nothing walks to her. She stands "at the other
end of a gaping chasm" (wiki Contact! oldid 15292391), and she says Kaleef "wouldn't have been able
to get across this chasm" (Transcript:Contact! oldid 15263370). Quest Helper Contact.java:244 puts
her at 2258,4317, "on the west side of the chasm". The chasm is `maps/m35_67.jm2`'s flag-1 strip at
x2259..2263, unbroken from z4298 to z4338, with no bridge flag on level 1. The map, the maze ladder
landing and the spawn were all right. The port lacked the across-the-gap talk:
`[apnpc1,contact_maisa_multi]`/`[apnpc1,contact_maisa]` are now stacked on her `[opnpc1]`
(OSRS-Content fd1bf2f29f), the shape LostCity uses for Hudon across the river (`quest_waterfall/scripts/hudon.rs2`).
The engine's ap rung fires at range 10 with line of sight (`torirs_server_world.c` ~2087-2099), and
line of sight crosses a floor-blocked chasm. Proof: `build/quest_gate/b53s3_maisa_before`
talkToMaisa FAIL "I can't reach that!" -> `b53s3_maisa_after2` 11/0; the committed contact.lua
copy with Maisa and Osman rows ran 130/0 to `quest.stage.told_osman` (70), with `maisa.across`
"talked from 2264,4317; Maisa 2258,4317 across the chasm". Rows: `test/quests/wip/contact/relay.md`.
OPEN (parity): the port splits Maisa's two questions over two talks and lacks the transcript's
"I don't know." row.

(b) An npc's lines titled with another npc's name and no chathead (What Lies Below: Rat Burgiss
headed "Outlaw"). `npc_find` and `npc_add` both rebind the PRIMARY active npc in this engine
(`torirs_server_scripts.c` SS_OP_NPC_ADD / NPC_FIND call `SSVM_SetActive(..., SSVM_PRIMARY, ...)`),
so a spawn proc called before a `~chatnpc` makes every later line speak as the npc it found or
added. `.npc_add` is not a fix here (it still sets the primary pointer); `.npc_find` is. The
What Lies Below fix (OSRS-Content fd1bf2f29f) calls `~wlb_spawn_outlaws` after the last `~chatnpc` of
the two paths that leave the player collecting pages; the hand-in paths no longer spawn. Proof:
`b53s3_wlb_speaker_before` `name=ok:Outlaw` 10/4 -> `_after` `name=ok:Rat Burgiss` 14/0;
whatliesbelow.lua 89/0, shot 053 headed "Rat Burgiss" with his head. `t.chat.play` matches the
text, not the title: only `t.chat.name()`/`expect_head` or a shot sees this. Unchecked: other
`npc_find` calls inside another npc's handler (`whatliesbelow_surok.rs2:92`,
`whatliesbelow_zaff.rs2:78`, `whatliesbelow_king.rs2:20`).

(c) The Queen of Thieves writes `' - '` for its four em dashes (gaps-dialogue: A gap where an em
dash should be), and the Queen's Hughes line follows Transcript:The_Queen_of_Thieves oldid 14962997
("find proof of her corruption ... northern side of Kingstown ... she's never in"; no door to pick,
no lock), OSRS-Content fd1bf2f29f. A content reword must keep every `chat.play` prefix a green test
asserts, punctuation included: dropping the period after "Kingstown" sent the run to 64/14.

## Seam pass matthew-mbp-m4-b54-seam1 (2026-10-03, batch matthew-mbp-m4-b54)

(a) **`npc_add`ed copies missing from `t.npc.tiles` (Swan Song: one of three ambush trolls).**
`movecoord(coord, dx, dlevel, dz)` takes the LEVEL in its middle argument, not x or z (LostCity
`engine/src/engine/script/handlers/ServerOps.ts:103-107`,
`CoordGrid.packCoord(position.level + y, position.x + x, position.z + z)`; ours,
`torirs_server_scripts.c` `SS_OP_MOVECOORD`, is the same). Swan Song's
`[proc,ssq_spawn_entrance_ambush]` wrote `movecoord(..., 2, 1, 0)` / `(-1, 2, 0)`, so trolls 2 and 3
spawned on levels 1 and 2 above the entrance. Neither engine's `npc_add` checks collision
(ours `npc_spawn`, world.c; LostCity `NpcOps.ts:57-68`), so a missing copy is never a blocked tile.
Fixed in OSRS-Content f2902a94dd (`(2, 0, 1)` / `(-1, 0, 2)`). Proof: the committed swansong.lua's
`kill79Trolls-present` read `1 copy(s)` before and `3 copy(s) -- 2343,3657 / 2342,3659 / 2345,3658 L0`
after; a copy without the `t.blocked` killed all three (varb2107 1/2/3) to `quest.stage.trolls_beaten`
(50). Rows and the next leg (Herman behind `swan_desk`): `test/quests/wip/swansong/relay.md`.
OPEN: the wiki (Swan_Song revid 15359363) has EIGHT level-79 trolls, the port three (`varb2107` is
2 bits); the ambush `npc_add` duration is 50 ticks, so a slower fight softlocks stage 40. UNCHECKED,
same bug class: `skill_hunter/scripts/stymphike.rs2:111` `movecoord(%varp6583_..., 2, 2, 0)`.

(b) **A quest drop added by hand to a generated `wiki_*.rs2` vanishes on regeneration.** A
quest-owned Tertiary drop goes in `tools/wiki_droptable.py` `QUEST_TERTIARY_HOOKS` (obj gameval ->
proc and call line, with a wiki oldid). The generator emits the call at the end of every label whose
npc's OWN Tertiary block lists that obj, and stops with exit 2 when a rule's proc or obj no longer
exists. Never hand-edit a generated `wiki_*.rs2`; regenerate it:
`tools/wiki_droptable.py --regenerate <drop_tables/scripts/wiki_x.rs2> --write --out-dir <scratch>`,
diff, install. Rag and Bone Man I's goblin skull is the one rule (OSRS Wiki 'Goblin' rev 15290833:
Always, in Drop table 1 AND 2; 'Rag and Bone Man I' rev 15292348: "Each bone is a guaranteed drop");
the old hand edit covered table 1 only. A regeneration of any wiki file written before 79d754bf0 also
adds the `~gwd_death_was_npc_kill` early return to every label (115 of 128 lack it): expect that
diff; it does nothing outside the God Wars Dungeon.

(c) **A scroll title that looks like "Rag and Bone Man II" is "Rag and Bone Man I!".** In the p12
scroll font a trailing "I!" draws like "II". Settle a scroll-title complaint from the
`quest.scroll_title` row's `got=` text, never from the pixels: here it read `You have completed Rag
and Bone Man I!` (questscroll.rs2:73 + the `quest_ragandboneman1` displayname), which is the wiki's
own scroll image (Rag and Bone Man I oldid 15292348, Rewards). The quest was renamed from "Rag and
Bone Man" to "Rag and Bone Man I"; the old name is not the title. NO CHANGE.

## Seam pass matthew-mbp-m4-b54-seam2 (2026-10-03, batch matthew-mbp-m4-b54)

(a) **"You can't go any further." on a cellar ladder (`ladder_cellar`, 17384; Swan Song's Wizards'
Guild basement at 2594,3085).** The ladder was bound only through its `climb_down_ladder`
category: `~climb_ladder(-1)` -> `~climb` -> `~maplink_try`, which is keyed on the PLAYER's tile.
The guild's one row, `[maplink_0_40_48_34_13_down]`, is keyed on the ladder's own tile, and a
shape-10 ladder blocks that tile, so a press from 2594,3086 never matched it. The miss fell through
to the plane default (level 0 - 1) and the blocked message. LostCity climbs every copy from the
player's own tile one dungeon mapsquare down: `[oploc1,loc_1754] p_arrivedelay;
~climb_ladder(movecoord(coord(), 0, 0, 6400), false);` (LostCity_Content2
`scripts/ladders+stairs/scripts/ladders.rs2:83-85`; the same tile, `maps/m40_48.jm2:6369`
`0 34 13: 1754 10`). Fixed in OSRS-Content 662de599a5: a name binding `[oploc1,ladder_cellar]` in
`ladders_stairs/scripts/ladders.rs2` animates, lets a verified maplink row answer first, and
otherwise `p_telejump`s to `movecoord(coord, 0, 0, 6400)`. All 33 harvested `ladder_cellar` rows
were already exactly +6400, so the cellar ladders that worked still land on the same tiles (ikov
`enterDungeonForBoots` PASS). Proof: scratch `b54s2_cellar_before2` FAIL "You can't go any
further." at 2594,3086,0 -> `b54s2_cellar_after` 10/0, tile 2594,9486,0, Wizard Frumscone at
2588,9489, `quest.stage.frumscone_done` 95. Rows: `test/quests/wip/swansong/relay.md`.
OPEN: `ladder_from_cellar` (17385) in that basement climbs to 2594,9486 **level 1** by the plane
default; LostCity `loc_1755` is `movecoord(coord(), 0, 0, -6400)` (`ladders.rs2:87-94`). It has
about 50 placements, some on level 1, so it needs its own audit; leave a basement by `goto_tile`.

(b) **`no npc ... in the client's entity pool` on the first `talk_to` after a ladder into a new
mapsquare.** `click_loc` returns on `map_flag` while the player is still walking to the ladder, so
the climb lands later (here at the end of the following `t.ticks(3)`), and the npc pool for the new
mapsquare arrives a tick after the tile does. Put `t.exec("<npc>-present", t.npc.await_present,
"<npc>", 15, 10)` between the ladder's `-tile` row and the `talk_to`. Swan Song's round-2 file went
from row 122 FAIL to 152/12 (`quest.stage.queen_fight` 170) with that one row added
(`b54s2_round2_await`).

## Seam pass matthew-mbp-m4-b54-seam3 (2026-10-03, batch matthew-mbp-m4-b54)

(a) **A level-170 boss dies to one spell; a level-79 troll reads 21/30 after one hit of 3 (Swan Song's
Sea Troll Queen and sea trolls).** Neither npc had a server `.npc` block, so both fought on
`npc_default.npc`'s 10 hitpoints and 1/1/1. Fixed in OSRS-Content 1ef7c1e7b9: the new
`quest_swansong/configs/swansong.npc` comes from wiki Sea_Troll_Queen oldid 15215925 and Sea_troll
oldid 15329222 (version "Level 79", id 4308), and the cache's `stat1..6` in `configs/all.npc` agree.
The Queen has 200 hp, 100/70/100, magic 150, Water Wave max 37, melee max 16 and speed 4. Each troll has
100 hp, 60/60/60, crush, speed 3, max hit 7 and drops Bones. Her swing is
`[ai_opplayer2,swan_seatroll_queen]` in `swansong_finale.rs2`; gaps-combat has how the fight goes
("A boss you cannot reach on foot"). The bars cannot tell 200 from 10, so the content has two server
reads. `::swansong_queen_hp` answers `Sea Troll Queen hitpoints 200/200 attack 100 strength 70 defence
100 magic 150`. `::swansong_troll_hp` answers `Sea trolls 3: 100/100 100/100 100/100`: the count, then
each troll within 25 tiles. Proof: `b54s3_trolls_b` 27/0 (kills of 86/81/106 ticks). The restaged
round-4 copy `b54s3_round4_copy_a` went 211/0 to the scroll, the Queen dead after 128 ticks. Swan Song
is in `docs/bosses/quest_combat_manifest.json` with three pinned oldids and six known_gaps, and
`check_swansong` is in `tools/check_quest_combat_contract.py`.

(b) **"I'm already under attack." on every Attack after the first, against several aggressive npcs
at once.** The port's `maps/multiway.csv` is LostCity's 2004 list, so a later multicombat area is
single-way here, and the npcs that attacked first claim the player. Check the wiki's Multicombat_area
list. If the place is on it, give the npc records `forcemulti=yes` (the per-record escape God Wars and
the Dagannoth lair use). Swan Song's colony is on the list (oldid 15307059). Before the fix,
`b54s3_fights_a` got three refusals. After it, all three trolls died (`b54s3_trolls_b`).

(c) **An `npc_add`ed ambush or boss vanishes mid-fight and the stage never moves.** Swan Song added the
ambush and the fishing troll for 50 ticks and the Queen for 100, behind a one-shot spawn flag. A real
fight outlasted them and soft-locked stages 40 and 170. The wiki has no timer (Swan_Song oldid
15359363). They now last `^ssq_fight_duration` 3000. Re-entering `swan_hole` at stage 40 puts back
the trolls still owed, and Herman at stage 170 puts back a missing Queen (`~ssq_queen_ensure`). In this
engine an `npc_add` duration of 0 or less means the npc never despawns (`torirs_server_scripts.c`
despawn_tick -1), unlike LostCity. Swan Song uses the finite duration and the put-back instead of
relying on that.

(d) **The guide says an npc gives you a tool, but a gate before him demands it (Swan Song's Franklin
and the hammer).** Fixed in OSRS-Content 1ef7c1e7b9. `swan_hole` no longer asks for a hammer: it is
"(obtainable during the quest)" (Swan_Song oldid 15359363). Franklin's talk after the firebox is lit,
before the wall is whole, follows wiki Transcript:Swan_Song oldid 15263341 (`swansong_franklin.rs2`).
If the player holds no hammer: "Also I think I need a hammer." / "Hammer? Not a problem - here's
mine.", and a hammer lands in the pack. With a hammer already held there is no second one. With a full
pack the player gets "You don't have enough inventory space." Proof: closer scratch
`b54s3c_franklin_hammer` 13/0 (`live.hammer`: `hammer count after the live lit-stage talk = 1`).
`b54s3c_full_franklin` entered the colony with no hammer (shot 033). Its `talkToFranklinHammer-hammer`
read `hammer 1 -> 1` (shot 083), and the run went on to `quest.stage.queen_fight` 170. OPEN, same
source: the transcript also has Franklin hand out a tinderbox at the intro, and the wiki says the bars
and logs can be gathered inside the colony. The hole still demands logs, a tinderbox and 5 iron bars.

(e) **Triage: give each file to one seam only.** The hammer seam's two edit sites were in
`swansong_colony.rs2`, which this pass gave to the combat seam. Its fixer could not edit them, and the
closer had to wire them in. When two seams must edit one file, give the file to one seam and list the
other's edits as its work.

## Seam pass matthew-mbp-m4-b55-seam1 (2026-10-03, batch matthew-mbp-m4-b55)

(a) **An artefact or a table offers only Examine, and its `[oploc1]` never fires (Another Slice of
H.A.M.'s dig sites and specimen table).** The cache gives `slice_artifact_hotspot_0N_1`/`_2`
("Artefact"/"Hole") and `slice_table_01` no op at all (`configs/all.loc:256904ff`, `257038`). In the
real game both are USES: wiki Another_Slice_of_H.A.M./Quick_guide oldid 14458352 #Excavation "Dig up
artefacts from the ground with the trowel", "Use artefacts on the specimen table to clean them";
Quest Helper's dig1..dig6 are trowel-highlighted ObjectSteps. Fixed in OSRS-Content f97d2dcd59: the
digs are `[oplocu,slice_artifact_hotspot_0N]` (anything but the trowel: "You need a trowel to
excavate this site."; a dug site: "You've already excavated this site."), and the dead `[oploc1]`
bindings are gone. Hotspot N writes `varb355(0+N)_slice_artifact_N` (the hotspot's own cache
`multivarbit`, so the dug site turns into its Hole) and gives `slice_artifact_N_dirty`; before,
hotspot 2 wrote artefact 5's varbit (turning hotspot 5 into a Hole), 4 wrote 2 and 5 wrote 4. Quest
Helper's `artefact1..6` labels run 1,5,3,2,4,6 only because that is the objs' id order. Proof:
`b55s1_dig_before` "Nothing interesting happens.", 0 items, varbit 0 -> `b55s1_slice_b` 146/0 to
the scroll (six `dig<N>` rows each `gained slice_artifact_<N>_dirty`, `varb355<N> = 1`). A loc
with no op in the cache is a `use_on` target, never a `click_loc`.

(b) **The route into a city needs a quest the guide does not list (Another Slice of H.A.M. needs
The Lost Tribe's varb532).** `::complete` writes only the named quest's own var. Quest Helper lists
Death to the Dorgeshuun, not The Lost Tribe, but the cellar hole (`lost_tribe_cellar_wall`'s
multiloc child `lost_tribe_cavewall_hole_walldecor`, values 4..12), Kazgar (`lost_tribe_guide`,
9..12) and `cave_goblin_city_doorr` (refuses below `^lt_complete`, `lotg_intro.rs2:67`) all read
`varb532_lost_tribe_quest`. Without `::complete quest_losttribe` the three are absent from the
client (`b55s1_route_noLT`: `no loc 6905 ... in the client's entity pool`). The scaffold's
`ROUTE_PREREQS` (`tools/quest_gate/new_quest.py`, batch matthew-mbp-m4-b55) stages it before DTTD.
With it the route clicks end to end (`b55s1_route_b` 18/0): trapdoor, hole (3221,9618,0), Kazgar
"Can you show me the way to the mines?" (lands beside Mistag, 3319,9615,0), the city door
(2704,5365,0). The Lost Tribe is NOT in LostCity (grep of all five LostCity trees is empty).

(c) **helper_coverage "no [op*] trigger on <loc> serves this quest" on a multiloc PARENT.** The
trigger sits on the multiloc CHILD (`[oploc1,lost_tribe_cavewall_hole_walldecor]`,
`losttribe.rs2:245`). The finding clears once a row named after the guide step clicks the child.

(d) **`talk_to dorgesh_urtaq` answers "I can't reach that!" (Dorgesh-Kaan council room).** Ur-tag
(2730,5365,1) stands in a walled room whose only entrance is `dorgesh_inner_door_posh_closed` at
2733,5363,1. A `goto_tile 2729,5365,1` lands inside the room past that door; click the door
(`click_loc ... { at = { 2733, 5363 } }`) and talk.

(e) **OPEN (content): after a REAL Death to the Dorgeshuun completion, Kazgar, Mistag and the
cellar hole vanish.** `dttd_shared.rs2:65` writes `varb532_lost_tribe_quest = 13`; the cache maps 13
to -1 for `lost_tribe_guide`, `lost_tribe_mistag` and `lost_tribe_cellar_wall` (multinpc14 /
multiloc14; multinpc is indexed by value). The value its own comment means is 12, the `_3ops`
Talk-to/Mines/Watermill Kazgar (wiki Kazgar revid 15196282). `b55s1_route_dttd13` loses the hole;
`b55s1_route_dttd12` passes 18/0. A test is not hit: `::complete quest_deathtothedorgeshuun` leaves
532 at the Lost Tribe's 11.

(f) **OPEN (tools): helper_coverage "X never reads %var" on a talk whose trigger header is stacked
over another.** `script_body()` (`helper_coverage.py`) stops at the next line starting `[`, so the
first of `[opnpc1,a]` / `[opnpc1,b]` written back to back has an empty body. Another Slice of
H.A.M.'s generals were graded this way and do advance 5->6 and 7->8; the content now gives each
header its own `@slice_generals_talk` jump. Other quests with stacked headers can be misgraded the
same way.

(g) **Quest Helper's `NpcID.LOTG_OLDAK_CUTSCENE` has no op.** It is the pre-gameval `NpcID.OLDAK`,
renamed. The talkable Oldak is `dorgesh_oldak_there` (2704,5365,0), the id the Land of the Goblins
helper uses; Another Slice of H.A.M. stage 4 goes through it (`lotg_yubiusk.rs2:12`).

(h) **The Eyes of Glouphrie's Evil Creatures hit back, at most 1 a swing; that is the real game.**
Wiki Evil_Creature oldid 15349482's infobox: `max hit = 1`, Crush, attack speed 4, aggressive No,
1 hitpoint, plus an "Attacking" sound effect; nothing on the page says they do not fight back. The
cache ships the swing (`all.seq [eyeglo_fluffie_attack]`). The engine latches retaliation on every
hit (`torirs_server_combat.c` `ToriRSServer_CombatHitNpc`; only `retaliate=no` opts out), so
`~npc_retaliate(0)` in the `[opnpc2]` bindings only starts the fight on the click. Max hit 1 is
`strength=1` + `strengthbonus 0` through `[proc,npc_melee_maxhit]`: (1 + 9) * 64 = 640, (640 + 320)
/ 640 = 1. `check_theeyesofglouphrie` now refuses `retaliate=no` or another strength. Any earlier
note that they "never retaliate" is wrong. Proof: `b55s1_eyeglo_c` `creatures.hit_back` "hp 40 ->
39 over 6 kills ... worst single tick 1".

(i) **One fight's hp delta does not prove an npc never hits.** Each npc has its own seeded
java.util.Random stream (`torirs_server_scripts.c`, `srv->npcs[slot].random`), so a scratch fight
replays the same rolls even when its timing shifts: `b55s1_eyeglo_a`/`_b` read 40 -> 40 over 13
creature swings, all 0. Prove "it hits back" or "it never hits" with several fights or a swing
count (`[ai_opplayer2,<npc>]` lines under `TORIRSSERVER_VERBOSE=1`).

## Seam pass matthew-mbp-m4-b55-seam2 (2026-10-03, batch matthew-mbp-m4-b55)

(a) **A kill's return teleport never moves you, and client.log prints `npc_findhero with no active
npc ... from [ai_queue3,<npc>]` (Another Slice of H.A.M.'s two H.A.M. rangers).** The death
handler bound the hero, then called a proc that showed a `~mesbox`, `p_teleport`ed and wrote the
stage. The page suspended the NPC script on the player: the stage write landed, the teleport did
not move him off the watchtower (2447,5416,2, no way down from that side), and
`~npc_default_death` resumed with no active npc. This is seam pass 21 (b) and seam 37 (d) one step
further: binding the hero first is not enough, a page in an `[ai_queue]` still breaks the script.
Fixed in OSRS-Content 2ca4e77a52 (batch matthew-mbp-m4-b55) (`slice_hammage.rs2`) the way LostCity's
`grandtree_black_demon.rs2` does it (`queue(queue_defeat_blackdemon, 0, 0)` from the death
handler): each handler does only the npc's half (dead flag, line, `~npc_default_death` while the npc
is active) and, once both are dead at stage 6, `queue(slice_ham_rangers_return)`; that player
script shows the mesbox, teleports to 2957,3512,0 and writes stage 7. `slice_ham_combat_login`
queues it again after a logout during the mesbox. The destination is sourced: the second kill
starts the kidnap cutscene, which ends at the generals (wiki Another_Slice_of_H.A.M. oldid
15292360; Transcript oldid 15263379 "Upon defeating the two H.A.M. members"; Quest Helper
`talkToGeneralsAgain` at WorldPoint(2957,3512,0) straight after `killHamMageAndArcher`, no travel
step). The port has no kidnap cutscene; the mesbox narrates it. Sigmund's death handler
(`slice_sigmund.rs2`) had the same shape: it now writes the flag and stage 10 in the npc's half
and queues his parting line, which shows now (round 6 read `none`). Proof: `b55s2_slice_a` (mage
first) and `b55s2_slice_b` (archer first) 148/0, `b55s2_close_b` 148/0 on the closer's pack:
`killHam-box` "1 page(s): mesbox:With both ambushers down", stage 7, `killHam.returned`
"2957,3512,0 (no goto)", `defeatSigmund.chat` "npc:Someday, somehow", no `no active npc` in
client.log; `b55s2_slice_c` 109/0 relogs during the mesbox and lands at the generals.

(b) **OPEN (tools): `tools/check_npc_script_player_suspend.py` passes a page AFTER `npc_findhero`.**
It flags only a suspend with no player bound. Grep for an `[ai_queue<n>,...]` that binds
`npc_findhero` and then reaches `~mesbox`/`~chatnpc*`/`~chatplayer`/`p_delay` before
`~npc_default_death`. A comment-stripped sweep found five more, not fixed and not run:
`quest_deserttreasure/deserttreasure.rs2:706` and `:1308` (`~chatplayer`),
`quest_rumdeal/deal_combat.rs2:56`, `quest_thegreatbrainrobbery/brain_finale.rs2:45`,
`quest_lunardiplomacy/lunardip_dream.rs2:166` (`~mesbox`).

(c) **OPEN (content parity): the H.A.M. watchtower's own ladder down is out of reach.** The cache
places `slice_goblin_ladder_top` (Climb-down) at 2442,5417,2 (`maps/m38_84.jl2:1753`), but the
port lands you at 2447,5417,2 on the far side of the cover crates: `click_loc
"slice_goblin_ladder_top" 1` answers `reach_failed: I can't reach that!` (`b55s2_slice_ladder`).
In the game you arrive at the ladder with the crates as cover (wiki oldid 15292360). No guide step
climbs down, so no test is blocked.

## Seam pass matthew-mbp-m4-b56-seam1 (2026-10-03, batch matthew-mbp-m4-b56)

Content in OSRS-Content 83c8fa8b73; grader in v3 e0d2cdfcc.

(a) **`<p,mood>` tags printed in dialogue** -- FIXED, `~chat_mood` reads one leading tag
(gaps-dialogue: "A `<p,happy>` tag drawn as text").

(b) **Content trap: an rs2 string literal with an unclosed `<` swallows the rest of the file.**
`ssc_lex.c` read_string counts bracket depth to find the closing quote, so `"<p,"` eats every line
after it, and the error surfaces as `no proc named <x>` in some OTHER file (border_gate.rs2:51
`no proc named chatplayer_anim`). Compare against `substring("<p,>", 0, 3)` instead.

(c) **Swan Song: the firebox kept the log** -- FIXED, `[oplocu,swan_firebox]` deletes it (wiki
Transcript:Firebox oldid 14859233, Swan_Song oldid 15359363, Quest Helper SwanSong.java:160-162: only
the tinderbox is not consumed). A test no longer has to drop a spare log. OPEN: the firebox's
lines differ from the transcript ("The firebox of the press is now filled with wood.") and the loc
does not change to its Logs/Lit versions (wiki Firebox oldid 14859274, ids 13594-13596).

(d) **"You have completed Contact!!"** -- FIXED: the shared `~quest_scroll_paint` appends `!` to
the display name (questscroll.rs2:73). The real title is per quest: Contact!'s scroll has one `!`
(wiki File:Contact! reward scroll.png revid 14120361) and `contact_shared.rs2` re-sets it after
`~quest_complete_rewards`; Wanted!'s real scroll IS "Wanted!!" (revid 14226730), so never strip the
bang in the painter. `quest.scroll_title` is a CONTAINS check and cannot see a doubled bang; only
the scroll PNG can. Not checked against their wiki images: H.A.M.!, Between a Rock...!,
Forgettable Tale...!.

(e) **Ghosts Ahoy's giant lobster swung once** -- FIXED (gaps-combat: "A melee quest npc hits once").

(f) **The grader missed a goto past a house door the guide does not name** -- FIXED for gotos INTO a
walled room (coverage-and-gate: `enclosure_entries`; start-and-travel: "The grader now catches the
goto inside").

## Seam pass matthew-mbp-m4-b58-seam1 (2026-10-04, batch matthew-mbp-m4-b58)

Content in OSRS-Content 6369379ada; client pick and `ladder.py` in the parent commit tagged
`[seam:matthew-mbp-m4-b58-seam1]`. b58 re-drove eight tier 1 tests with no goto into or out of a
closed space, and these are the gaps that walking them for real exposed.

(a) **The Wilderness Ditch's Cross said "Nothing interesting happens."** -- FIXED,
`area_wilderness/scripts/wilderness_ditch.rs2`. Every placed copy is a 1x2 segment. At angle 0/2
(the border at z 3521-3522) the jump is z-1 <-> z+2, from 3523 to 3520 and back. At angle 1/3 (the
north-south stretch at 2996-2997,3530-3533) it is x 2998 <-> 2995. The east end (x 3328-3340) is
`ditch_wilderness_cover_members`, which crosses the same way. Drive it as
`click_loc("ditch_wilderness_cover", 1, {at={x,3521}})`. The jump is an exactmove held 2 ticks, so
call `t.ticks(4)` and then read the tile. The first jump north in a session (wilderness level > 0,
`%varp5753_wilderness` unset) opens the strip's three mesbox pages first. Play them with
`chat.play{"mesbox:WARNING! Proceed with caution", "mesbox:The further north you go",
"mesbox:In the wilderness an indicator"}`. The pages are the pack's LostCity strip warning, not
OSRS's `wilderness_warningscreen` interface, which nothing wires yet. A goto across the border is no
longer justified by "the ditch cannot be crossed".

(b) **Phoenix Gang weapon store: locked from inside, open from the street without the key** --
FIXED. LostCity puts `phoenixdoor2` on the store tile (`m50_52.jm2:6944` `0 51 57: 2398 0 1`).
The rev-239 cache encodes the same wall edge from the street tile (`m50_52.jl2:570`
`0 51 58: 2398 0 3`), so LostCity's `~check_axis` "leaving" test was inverted here.
`[label,unlock_weaponstore_door]` now decides inside from the store's row. Trap for any LostCity
door script that reads `~check_axis` as inside/outside: compare the loc row in
`LostCity_Content2/maps/*.jm2` with ours in `maps/*.jl2` before blaming the proc.

(c) **Ernest the Chicken's maze gates answered "I can't reach that!" from their walled side** --
FIXED. The cache rebuilt LostCity's nine shape-0 wall doors as shape-10 gates on a blocked tile, with
a `blankwall_no_blockrange` (loc 44603, blockrange=0) on the 2004 door's edge. That wall blocks
walking but not range, so no op can reach the gate from its side. Each gate now also answers
`[aploc1]` (`[label,ernest_approach_maze_door]`, `p_aprange(1)`), the same pattern as seam27's
`mdaughter_polerocks`. Press a gate from the tile in line with it on its own axis. You land two tiles
across, on the mirror tile. A press from beside the gate in its own wall line does nothing. 26 map
squares carry loc 44603, so another door there may need the same treatment. The maze is walkable
end to end, which supersedes gaps-world's "The `goto_tile` bypass covers a puzzle-gated door".

(d) **Eadgar's Ruse storeroom door unlocked but still blocked both ways** -- FIXED. A content door
whose script `loc_change`s in place to an op-less, blocking `*_open` leaf blocks its own edge.
LostCity's `~open_and_close_door2` instead walks the player through and adds the leaf one tile over,
rotated, for 3 ticks. This pack has no shared copy of that proc, so port it as a quest-local proc
(`viking_door_pass`, `eadgar_storeroomdoor_pass`). The door now follows LostCity
`quest_eadgar.rs2:488-506`: the drawer key unlocks it once at `got_burnt_meat` and is used up
("You unlock the door."). After that it opens with no key.

(e) **Between a Rock: the ferry cave landed in sealed rock; the outer flames said "Nothing
interesting happens."** -- FIXED. `dwarf_cavewall_tunnel` (troll room 2781,10161) lands on the ferry
bank at 2838,10124. The bank's `dwarf_cave_entrance` (2838,10123) goes back to 2778,10161
(shortest-path `transports.tsv:4330-4331`). That file is the source for any cave transition the
maplink importer skipped because a quest trigger already claimed the loc. The realm's outer walls of
flame (`dwarf_firewall_straight`/`_diagonal`, 10 copies) take op1 Jump-through to the far tile with
no damage, since none is sourced: `click_loc("dwarf_firewall_straight", 1, {at={2372,4939}})` from
2372,4938. The centre ring answers op2 Talk-to as well as op1. After the ferryman lands you at
2823,10165, the scene rebuilds and Dondakan is missing from the client pool for 2-3 ticks, so call
`t.npc.await_present("dwarfrock_dondakan", 15, 10)` before each talk or use on him (the INDEX line
for seam b54-seam2 (b) applies). OPEN: `trollromance_stronghold_exit_tunnel` still lands on
2781,10160, a rock tile beside the cave. LostCity and maplink give 2773,10162, and
betweenarock's `enterDwarfCave` row pins the old tile.

(f) **Keldagrim had no way out** -- FIXED, `area_keldagrim/scripts/keldagrim_travel.rs2`:
- The city boatman `dwarf_city_boatman_city` lands at 2838,10127, beside `dwarf_cave_entrance`. On
  op1 Talk-to, answer "Want me to take you back to the mines?" with "Yes, please take me."; op3
  Travel goes straight there.
- The mines boatman (`dwarf_city_boatman_mines` after The Giant Dwarf starts) lands at 2892,10225 on
  the Keldagrim dock. Talk-to: "Hello again, <name>! Want to go back to Keldagrim?". He has op3
  Travel too.
- From the city boatman to the surface: `dwarf_cave_entrance` -> 2778,10161, then
  `trollromance_piste_exit_tunnel_bottom` (2771,10161) -> 2730,3713 east of Rellekka.
- The Dorgesh-Kaan platform's `slice_underground_wall_exit_dwarf` goes back to 2941,10179. There is
  no train ride yet.
- Conductors 1/2/3/5 sell Ice Mountain (150) and White Wolf Mountain (100, after Fishing Contest)
  tickets. The track 3 cart (2923,10171) rides free to the Grand Exchange, 3141,3504. The track 1
  cart (2923,10175) takes the Ice Mountain ticket to 2995,9835.

Wait about 4 ticks after a landing before `talk_to`. `walk_to` answers `refused move_to` when the
target is outside the scene the last landing built, so walk to a tile inside it first.

(g) **`KeyError: '(goto past <door>)'` from `ladder.py` / `fail.py --leg`** -- FIXED (relay: "The
ladder"). `build()` leaves helper_coverage's goto charges out of the numbered table, and
`ladder_pseudo_step_test.py` holds it. 39 of 189 ladders crashed before the fix.

(h) **`covered ... none of N pixels hittested` on an npc the shot shows nobody at** -- FIXED in the
client. Biohazard's Chancy and Da Vinci (`gambler2`, `artist2`) are cache model 25362: a quad whose
two faces are alpha 255, which lighting hides. The per-face pick skipped every face. An
NPC/PLAYER/OBJSTACK whose model has no visible face now picks by its box, as the reference's
`useAABBMouseCheck` does (`ToriDraw_ModelHasVisibleFace`, `torirs_frame.c`). Locs keep the per-face
rule. Conformance row `seam.npc_drawing_no_face_is_pressed` covers it. biohazard's two `t.drive.op`
fallbacks are no longer needed.

(i) **Proving pre-fix content behaviour without mutating the shared tree**: build a symlink farm
that mirrors `osrs239-content` with the one file replaced by its HEAD copy, compile it with
`src/build_opt/sscompile --src <farm>/server/scripts --out <farm>/server/scripts/build`, and run with
`TORIRSSERVER_SCRIPTS=<that build dir>` (the phoenixdoor2 and Keldagrim baselines).

## Seam pass matthew-mbp-m4-b59-seam1 (2026-10-04, batch matthew-mbp-m4-b59)

Content in the OSRS-Content commit tagged `[seam:matthew-mbp-m4-b59-seam1]`; the driver verbs in the
parent commit with the same tag. b59 re-drove ten tier 1 tests with no goto past a closed door, and
four stopped at content nobody could reach on foot. These are the fixes.

(a) **`talk_to(kennith)` answers `I can't reach that!` from inside the cabin** -- FIXED,
`area_fishing_platform/scripts/kennith.rs2`. Kennith (2766,3288,1) stands in a pocket behind
`slug2_crate_stack`. The nearest standable tile is 2766,3286, two tiles back. He now has an
`[apnpc1]` approach trigger at range 2 for both `kennith` and the `kennith_platform` leaf, the shape
of At First Light's Verity. A multinpc needs both the base and the leaf bound. The crates are
`blockrange=0`, so the talk lands across them. A wall still refuses even at range 2: from 2768,3288
the answer is `I can't reach that!`. Talk from 2766,3286,1 (wiki Sea_Slug/Quick_guide oldid
14957910).

(b) **The Sinclair mansion spiral stairs say "It's just a staircase."** -- FIXED,
`quest_kingsransom/scripts/kr_mansion.rs2`. King's Ransom's triggers on `murder_qip_spiralstairs` /
`murder_qip_spiralstairstop` are more specific than `[oploc1,_climb_up]`, so outside its own
`^kr_told_by_gossip` state they now do the plain `~climb(1)` / `~climb(-1)` (LostCity's mansion has
a plain ladder here, `m42_55.jm2`, loc 1747). The stairs land at 2737,3580,1. Murder Mystery's
level-1 evidence is there: barrels c 2733,3580,1, d 2733,3577,1, e 2747,3581,1, f 2747,3577,1, and
the web at 2740,3574,1. Trap for any quest overlay on a shared staircase or ladder: fall through to
the generic climb outside the overlay's own states.

(c) **`walk_to` stalls at 2559,3299 against the West Ardougne wall door** -- FIXED,
`area_ardougne_west/scripts/doors.rs2`. The map places `ardougnedoor_l`/`_r` (8738/8739, a 2x2 block
at 2557-2558,3299-3300). The Biohazard walk-through was bound only to LostCity's
`ardougnewalldoor_left/right`, which no map places. Now it is bound to both. Press a leaf from 2559
(east) or 2556 (west); you are forcemoved two tiles through the block. Before Biohazard is complete
the door answers "...But they will not open." and you stay put.

(d) **The Mourner HQ door and its trapdoor lock out a player who finished Mourning's End Part I** --
FIXED (`doors.rs2` mournerstewdoor, `mend1_disguise.rs2`). With the full mourner disguise worn, both
admit from `^mend1_gathering` on, Part I complete included. Without the disguise, the door gives its
mesbox and the trapdoor says "The trapdoor is bolted on the other side." (wiki
Mourner_Headquarters oldid 15302196, Trapdoor_(Mourner_Headquarters) oldid 14713443). The trapdoor
lands at 2044,4628,0 on Essyllt's spawn tile. Wait for him with `t.npc.await_present` before
`talk_to`: the landing does not wait for the npc pool the way `goto_tile` does.

(e) **Lletya's trees say "Nothing interesting happens."; the teleport crystal does nothing** --
FIXED, new `quest_mourningsendparti/scripts/mend1_lletya_access.rs2`. `elf_village_treegate` op1
Pass at 2305,3191 and 2305,3195 (the west column x=2304 to the east column x=2306, on your own row,
both ways) once `%varp517_mourning_quest >= ^mend1_escorted`. `mourning_teleport_crystal_N` op1
(`t.player.inv_op(<crystal>, 1)`) lands around 2328,3170 and steps the charge 5 -> 4 -> 3 -> 2 -> 1
-> `elf_crystal_tiny`. Arianwyn (2353,3172) is out of entity-pool range from the landing: `walk_to`
2350,3172 first. Never goto into Lletya. Sources: wiki Tree_(Lletya) oldid 14250252, Teleport_crystal
oldid 15261004.

(f) **The Mourner HQ basement ladder lands in a void at 2044,4649,1** -- FIXED, new
`mend1_hideout_ladder.rs2`. `mourner_hideout_ladder1` (2044,4650) climbs to 2542,3326,0 beside the
trapdoor, so the basement is left on foot, with no Camelot Teleport. Trap: a quest ladder whose
destination the shortest-path data lacks is bound by name with `~climb_ladder_to(coord, true)`.
Never hand-add a `maplink.dbrow` row: that file is generated and the importer erases it.

(g) **Mourning's End Part I mints an extra ogre bellows per dye** -- FIXED, `mend1_sheep.rs2`. A
dye now consumes the empty bellows and the toad catch hands it back, so the pack holds exactly one
bellows through the whole sheep task (wiki Red_dye_bellows oldid 15186976). The four extra bellows
had cut the 12-coal give to 9.

(h) **The Temple of Light low wall's Climb-over says "Nothing interesting happens."** -- FIXED,
`quest_mourningsendpartii/scripts/mend2_temple.rs2`. `mourning_temple_wall_jump` (1883,4620,1 and
1883,4658,1) crosses 1882 <-> 1884 on the wall's row, with no level, no XP and no fail roll (wiki
"Low wall (Temple of Light)" oldid 14748714). `click_loc` answers `timeout settle_after_click` for
the short hop, so grade the crossing on `t.world.tile()`.

(i) **No boat to Miscellania; `goto` from Lumbridge to the island dock** -- FIXED
(`quest_viking/scripts/viking_sailor.rs2`, new `area_miscellania/scripts/misc_sailor.rs2`). Rellekka
`viking_sailor` (2629,3693) Talk-to or op3 Miscellania lands at 2581,3845,0. Miscellania
`misc_sailor` (2581,3847) Talk-to or op3 Rellekka lands at 2629,3693,0. Both need The Fremennik
Trials: `::complete quest_fremenniktrials` in setup, a Quest Helper requirement of Throne of
Miscellania. Without it the sailors give the before-trials lines and do not move you. The ride is
`if_close`, `mes`, `p_delay(2)`, telejump, mesbox, so the dialogue CLOSES for two ticks before the
arrival mesbox. End the `chat.play` list at `player:Let's go!`, `t.await` the landing tile, then
`chat.play{"mesbox:The ship arrives at Miscellania.", "end"}`. One list spanning the gap answers
`the dialogue closed after N page(s)`. Royal Trouble's `[opnpc1,misc_sailor]` falls back to the ride.
Sources: wiki Transcript:Sailor oldid 15095248, Sailor oldid 15351110.

(j) **The Port Sarim monk sails you to Entrana armed** -- FIXED,
`areas/port_sarim/scripts/monk_of_entrana.rs2` `~has_entrana_restricted_items` (LostCity's proc).
After "The monk quickly searches you." the monk checks worn AND carried items. Any `weapon_*` obj,
any wearable with an attack or defence bonus (outside the neck and ring slots and the wiki's
exceptions: ice gloves, wizard and other robes, god capes and books, and so on), and cannon parts
are refused. The refusal is two npc pages, "NO WEAPONS OR ARMOUR are permitted on holy Entrana AT
ALL..." and "Do not try and deceive us again...", and you stay on the dock (wiki Entrana oldid
15352889). Bank or drop gear first, as hero.lua does at Draynor. Food, runes, arrows and jewellery
pass. LostCity's "All is satisfactory" page is NOT ported, so the `chat.play` list still ends at
`mesbox:The monk quickly searches you.`. Supersedes "open: the Entrana monk's weapon search is
unported" (Seam pass 34).

(k) **`t.world.loc_near` on level 1 answers the door on level 0 below; a hand-written `pass_door`**
-- FIXED in the driver. `loc_near(sym, r, {level=n|"here"})` or `{at={x,z[,level]}, slack=s}`
reads ONE floor, and a filtered `not_found` names the skipped copies with their levels.
`t.player.pass_door{closed=, open=, at=, near=, far=[, close=true]}` crosses one door and grades it
on the leaf reads and the tiles. See verbs-pointer: Stacked floors and `t.player.pass_door(spec)`.

(l) **A door the player opened draws NEITHER leaf after its 500-tick revert while the player was
away** (Miscellania castle gate 2510,3860,0 on the way back from Leif; the level-1 landing door
2506,3851,1 after a stair climb) -- OPEN, an engine fix waiting to land. The client's
`UPDATE_ZONE_FULL_FOLLOWS` cleared only obj stacks. The reference (Client-TS Client.ts ~7463) also
resets the zone's locs to the map's. On top of that, the server never retired the open leaf's
revert record (`torirs_server_zone.c`, a removed loc compared by angle). The fix and its conformance
row `seam.door_revert_reaches_a_returning_client` are proved in a private worktree but not
committed. Until it lands, close a door behind you when you leave its zone for 500+ ticks
(`pass_door{..., close=true}`). `pass_door` answers `not_found ... neither leaf on level L` when the
bug fires.

## Seam pass matthew-mbp-m4-b60-seam0 (2026-10-04, batch matthew-mbp-m4-b60)

Content in OSRS-Content 4b4cc88be6 (`[seam:matthew-mbp-m4-b60-seam0]`); the driver verbs, the
sample tools and the conformance rows in the parent commit with the same tag. This pass ran BEFORE
b60's door-rule fixers, on the owner's say-so, because every b56-b59 door-rule fixer had hand-written
the same crossing helpers and three b59 tests went back for gotos the reach tool called clean.

(a) **Crossing helpers are verbs now.** `t.player.cross_gate`, `cross_trap`, `walk_route` and
`teleport_cast` sit beside `pass_door`; verbs-pointer.md "The crossing verbs" has the specs.
Conformance `player.cross_gate` (Taverley east gate in and out, after proving a walk alone does not
cross), `player.walk_route` (rovingelves' 26-waypoint chain, 106 tiles), `player.cross_trap` (the
pitfall Jump, `You manage to cross safely.`) and `player.teleport_cast` (Camelot, 5 air + 1 law to
0). The b59 tests (hero, hunt, rovingelves, mourningsendparti, misc) still carry their own copies; a
copy of hunt.lua migrated to `cross_gate` ran 114/0 and graded FULL.

(b) **`reach.py` charges op locs** -- coverage-and-gate.md "`reach.py` says NEEDS-OP". Landing it
moved one committed quest: Monkey Madness (`mm`) is RED under `gate.py` (TEST_GAP: `goto-
talkToZooknock` and `goto-useTalisman` land in a 294-tile pocket past `mm_double_springtrap_trigger`,
`ape_atoll_dungeon.rs2`; the trap is crossed with a plank or walked over for damage). Its queue row
was left green for the orchestrator to reopen.

(c) **The camera "detached after a walk-triggered rebuild" was a slipped pitfall** -- FIXED,
`quest_regicide/scripts/regicide_traps.rs2`. `[label,regicide_jump_pitfall]`'s slip played
`anim(human_death, 0)` and never ended it. The seq's last frame holds 20,000 client cycles
(`configs/all.seq`, and LostCity 225 alike), and while a primary seq with postanim DELAYMOVE plays,
the client holds every walk the server sends (`World_MoverHeldByAnim`, the same as Client-TS
`Client.ts` routeMove). So the player walked on server-side while his model, camera and minimap
stayed at the pit, and every loc press answered `covered`. The client is faithful; no engine change.
The slip now ends the fall a tick later (`p_delay(0); anim(null, 0);`), as LostCity's spike pit does
(LostCity_Content2 `quest_upass/scripts/upass_grid.rs2:87-89`). Conformance
`seam.slip_fall_releases_the_walk` slips at Agility 1, lands, walks 8 tiles and reads the eye 4-10
tiles south of the player at pose 0/383/600; without the fix it read `d -1,-15`. A quick check for
the shape anywhere else: `t.world.camera()` against `t.world.tile()`. Any content that plays
`human_death` on a living player must clear it (unchanged, worth a look:
`quest_viking/scripts/viking_thorvald.rs2:326`).

(d) **The Isafdar woodspring passes both ways, and walking onto it springs it** -- FIXED,
`regicide_traps.rs2` + `skill_agility/configs/maplink_agility.dbrow`. Before, the pass was a
tile-keyed `~maplink_agility` lookup with one row per trap direction, and four reverse rows had been
dropped by `tools/maplink_import.py` (its 2-tile radius misses the far side of a 3-wide loc;
docs/MAPLINKS_REJECTS.md:273-276; a re-import must keep the four hand-added rows). Pressed from the
east, the approach stood the player on the trap's middle tile and it printed `Nothing interesting
happens.` It is now LostCity's handler (LostCity_Server `quest_regicide.rs2:31-134`): the pass is
worked out from the trap's angle and the side the player stands on, a failed roll walks the player
onto the trap, and the walk trigger (`regicide_zones.rs2:1-66`) springs it: `You set off the trap
as you pass.`, 8 damage, thrown to the trap's fixed side (from the WEST that is the east side,
across it). The sprung tiles are the middle two of each spring (2236-2237,3181; 2200-2201,3169;
2275-2276,3163; 2257-2258,3227; 2181,3210-3211; 2295,3214-3215). `walk_to` re-issues into a spring
on every idle tick until the player dies, so never walk across one: press it with `cross_trap`
(op 1; no level gate, the roll is `stat_random(agility, 30, 155)`, so stage Agility to fail less) and grade the tile; a failed roll from the west lands on the east side,
crossed with damage. The committed regicide.lua walks over the 2235,3181 spring in leg 3 and now
dies there (row 119 `walk-climbThroughForest`); its fixer passes the trap before the walk.

## Seam pass matthew-mbp-m4-b60-seam1 (2026-10-04, batch matthew-mbp-m4-b60)

Content in the OSRS-Content commit tagged `[seam:matthew-mbp-m4-b60-seam1]`; the driver verbs and
their conformance rows in the parent commit with the same tag. Five b60 door-rule reopens ended
`content_bug` on a place no walk could reach; each fix below makes the real branch reachable.

(a) **Spirits of the Elid: the Water Ravine Dungeon is reached and left on foot** -- FIXED,
`quest_spiritsoftheelid/scripts/elid_dungeon.rs2`, `.constant`, `doors/configs/doors.loc`. No
LostCity source; the wiki walkthrough has the rope used on the root "on the east bank". Three faults.
(1) `desert_water_cave_root` (3369-3371,3132) stands over the waterfall pool, and every tile beside
it is pool or cliff, so `[oplocu]` answered `I can't reach that!` from every bank tile. It is now
`[aplocu]`, which narrows with `p_aprange(3)` until `distance(coord, loc_coord) <= 4` (the shape of
LostCity `quest_waterfall.rs2:213` and this tree's `[aplocu,mdaughter_polerocks]`). A loc used across
water needs this shape. The engine measures ap range to the loc's rectangle and the script to
`loc_coord`, so the script check allows the loc's width. (2) `^elid_cave_exit_coord` was the pool
(3370,3131), with no walk out; it is now the east bank 3372,3130 (inferred from the wiki's "east
bank"; no source names the tile). (3) The robe door, the three golem doors and the lake door each had
a quest `[oploc1]` that shadows doors.rs2's `[oploc1,_door_closed]` and never swung the leaf. Each
now ends in `~door_open_active` after its checks (before the golem spawns). A door with no `_open`
sibling in the cache can take a nameless, op-less sibling of the same model as its
`next_loc_stage`: the golem doors open to `elid_underground_inactive_door` (model 10174, mirror=1),
which nothing can close; the 500-tick revert shuts it. The pack does not require a reciprocal
pair (`torirs_server_pack.c:1180`). A test crossing back past it names `open=` in `pass_door`.
Proved on a copy of the committed test with 4 test-side edits: 138/0, helper_coverage FULL.

(b) **Draynor Manor's crypt stairs go down and come back up** -- FIXED, new
`quest_vampire/scripts/vampire_crypt_stairs.rs2`, ported from LostCity
`ladders+stairs/scripts/stairs.rs2:408-424`. `cryptstairsdown`/`cryptstairsup` carried only the
climb categories and no maplink row, so the generic `~climb` moved one plane below level 0:
`You can't go any further.` Down now lands on 3077,9771,0 and up on 3115,3356,0. A climb-category
loc whose real destination is in another region is fixed with a name rung `[oploc1,<sym>]` in the
quest's own scripts (it beats the category rung); bind only the ops the record has (`all.loc`).

(c) **Mountain Daughter: the rockslide is two-way and the rock tent has a door** -- FIXED,
`quest_mountaindaughter/scripts/mountaindaughter_camp.rs2` + `.constant`. No LostCity source; wiki
"Rockslide (Mountain Camp)" oldid 15360533 (the camp's entrance, Climb-over), Mountain Camp oldid
15351048, Mountain Daughter oldid 15292291. The rockslide used to teleport to 2768,3668, a cliff tile
walled on four sides, and only inward. Pressed from z < 3658 it now lands on 2760,3660 inside the
camp, otherwise on 2760,3657 outside. The rock tent's double door (`mdaughter_rocktent_door`
2799,3665 and `_doorl` 2800,3665) had no op1; Go-through now walks the player through
(`open_and_close_door2` shape): from outside onto the door tile 2799,3665, which IS inside the tent;
from inside to 2799,3666. The leaf is gone for only 3 ticks, so grade the entry with `pass_door`
`far_ok` (inside the tent, x 2797-2802 z 3661-3665), not on the leaf's absence or a tile deeper in.
Not ported yet: the wiki gates the rockslide on starting the quest (before it, a rope on the boulder
climbs you down); the boulder still only prints a line, so the rockslide stays ungated.

(d) **The Tourist Trap mine door lands on the door tile** -- FIXED,
`quest_desertrescue/scripts/quest_desertrescue.rs2`, from LostCity
`quest_desertrescue.rs2:433-440`. It used to land on 3278,9425, a walled pocket south of the exit
door. It now lands on 3278,9426 for one tick (`p_delay(0)`) and steps through `thttmineexitl` to
3278,9427, the mine side. A test reads the tile after the door with an await on 3278,9427, not at
once. Not ported: LostCity's double-door swing (no `open_and_close_double_door2`, no open stages
for `thttmineexitl/r`).

(e) **One Small Favour: the Seers' roof ladder reaches the roof** -- FIXED,
`quest_onesmallfavour/scripts/onesmallfavour_puzzles.rs2`. `favour_seer_ladder` (2715,3472,1) and
`favour_roof_trapdoor` (2715,3472,3) used the generic one-plane climb categories, landing on plane 2,
which the map blocks. Name rungs now call `~climb_ladder_to`: up to 2714,3472,3, down to
2714,3472,1 (Quest Helper `OneSmallFavour.java:745,753`, roof zone `:365`; placements
`m42_54.jl2:4299-4300`). A copy of the committed test without its block ran 468/0.

(f) **A spell left armed after a fight, and a climb verb** -- driver. A re-cast whose presses all
answered `covered` left Fire Blast armed after Family Crest's Chronozon. After that, every world
press read `covered ... menu rows: <Cancel>`. `t.player.cancel_selection` drops it, and the driver
calls it itself after a cast press that missed on every try and when `npc.await_dead_engaged`'s cast
wrap ends. `t.player.climb` grades a level change on the level and the landing. Both are in
verbs-pointer.md. Conformance `player.climb`, `player.cancel_selection` and
`seam.cast_fight_ends_with_nothing_armed` (153 verbs / 104 seams).

(g) **Two seam-pass traps, again** -- `run.py <quest id> --name X` does NOT rename a run started by
quest id, and without `--no-publish` it rewrites `OSRS-Content/.../selftest/quests/<dir>/play`. Two
fixers in this pass did so for cooks_assistant and druid, and a hand restore then zeroed 72 tracked
files. When you read a file back with `git show HEAD:<path>`, remember that OSRS-Content's git root
is `OSRS-Content/`, not `osrs239-content/`: the path needs the `osrs239-content/` prefix.

## Seam pass matthew-mbp-m4-b61-seam1 (2026-10-05, batch matthew-mbp-m4-b61)

Content in the OSRS-Content commit tagged `[seam:matthew-mbp-m4-b61-seam1]`; the driver verbs, the
two engine commits and the conformance rows in the parent commits with the same tag. Engine rows run
on `build/orchestrator/worktrees/b61-engine/src/torirs_b61engine`, which replaces `torirs_b59door`.

(a) **Fight Arena: the compound door lets the player out at any stage** -- FIXED,
`quest_arena/scripts/arena_locs.rs2` `[oploc1,fightarena_door1]`, ported from LostCity
`quest_arena.rs2:32-35`. A player on the door's own axis (`~check_axis`: the loc tile, which is the
compound side of both leaves, west 2585,3141 and north 2617,3171) goes straight to
`[label,arena_pass_door1]`, silently, before any guard, stage or disguise test. The stage/disguise
rule and the guard's lines apply to ENTERING only. Before, at stage 12 (after `arena_escape` drops
the player in the yard at 2608,3151) the inside press answered "This door appears to be locked." and
the guard attacked, so the player was locked in. Leave on foot by the west leaf 2585,3141 -> 2583,3140.

(b) **The Abyss Law rift applies Entrana's item search** -- FIXED,
`skill_runecraft/scripts/runecraft_abyss.rs2` `[proc,abyss_rift]` calls the Port Sarim monk's
`~has_entrana_restricted_items` (worn and carried). Source: wiki Abyss oldid 15228428 ("entering it
has the same restrictions as entering Entrana") and Entrana oldid 15352889. The Law rift only; the
other rifts have no item rule. A refused player stays in the Abyss with "The power of Saradomin
prevents you from taking weapons or armour through the Law rift." -- that wording is a paraphrase,
no source quotes it. A quest route through the Law rift carries no weapon or armour (a pickaxe or axe
counts). A probe stands on 3052,4838 to reach every rift; 3045,4836 is `reach_failed` on the Law rift.

(c) **Zogre Flesh Eaters: the Brentle Vahn zombie fights with its real stats** -- FIXED,
`quest_zogreflesheaters/configs/zogreflesheaters.npc` `[zogre_human_brentle_vahn]`: 50 hitpoints,
30/30/30, crush, aggressive, undead, 50% fire weakness, `death_drop null` (wiki oldid 15272404; cache
`all.npc` stat1-4 30/30/30/50). Before, it had no block and fought at the engine default 10 hp. Two
facts came with it. (1) Only the FIRST `[gameval]` block across all `.npc` files survives, and a quest
overlay block beats `npc/configs/npc_anims.generated.npc`. A quest block, even one that only adds
params, must restate the generated anims and `attackrate`, or the npc silently loses them. The
existing `[zogre_slash_bash]` block had lost them and now restates them. (2)
`docs/bosses/quest_combat_manifest.json` is GENERATED: fill a row through `AUDITED_OVERRIDES` in
`tools/generate_quest_combat_manifest.py`, never by hand-editing the JSON, or `--check` goes stale.
When a seam raises an npc from 10 hp to its real hitpoints, check the test's `await_dead_engaged`
budget: the real zombie takes about 64 ticks, against the 60 the old test allowed.

(d) **The region-music unlock wrote quest varps 1-27** -- FIXED (engine),
`tools/gen_music_regions.py`, `torirs_server_music_regions.gen.h`, `torirs_server_world.c`
`ToriRSServer_MusicEnterRegion`. DBTable 44's unlock pair is (music VARIABLE 1-27, bit), and the
generator emitted the variable as a varp id. So walking into a mapped square OR'd a bit into a quest
varp: Draynor Village (48,50, "Unknown Land" = variable 5 bit 5) turned `%varp5_grail` spoken_crone
4 into 36, and the Grail whistle then went to the restored realm. The generator now maps variable N
to the `[varp<id>_musicmulti_N]` id from `interface_music/configs/music.varp` (variable 5 is varp
24). Every quest-loop run since 350035439 (2026-08-20) had the bug: 58 of 92 greens wrote some other
quest's varp 1-27, and none wrote its own or a staged prerequisite (audit:
`build/seam_state/matthew-mbp-m4-b61-seam1/music/audit.json`). Draynor's bank is safe on
`torirs_b61engine` or newer. Saves written by an older server may still carry stray bits.
Conformance `seam.region_music_unlock_writes_the_musicmulti_varp`.

(e) **An npc whose `[ai_queue3]` hands its death to a player queue is held until that queue
decides** -- FIXED (engine), `torirs_server_combat.c` `npc_death_step`. LostCity never removes a
dead npc on its own: `NpcOps.ts` NPC_DEL is the only removal, and `[proc,npc_death]` is its only caller
on a death. `[ai_queue3,black_knight_titan]` only `queue()`s `queue_defeat_titan(npc_uid)`. The
engine used to reap the titan on the same tick, so `npc_finduid` in the queue missed and neither
"Maybe you need something more to beat the titan?" (heal, stage 7) nor "Well done! You have
defeated the Black Knight Titan!" ever ran. Now the CORPSE stage records each new queue entry naming
the npc's uid, and REAP holds the npc (still dying) while the entry is queued or the script it
started is parked on the npc. After that, hitpoints > 0 means revived and 0 means reaped. A revived
npc plays its death animation again on its next death. Residual divergence: the death animation
plays before `[ai_queue3]`, so on the no-Excalibur branch the titan animates a death and stands back
up healed. Only the titan and `alomone_hazeel_cultist` pass `npc_uid` to a queue today. Conformance
`seam.npc_death_waits_for_its_queue`. To tell "the same npc survived" from "reaped and respawned"
(the titan respawns in about 4 ticks), compare the `slot N` in `t.npc.await_present`'s detail.

(f) Driver: `cross_gate` takes `chat=` / `chat_optional=` for a guarded walk-through, and the four
crossing verbs take `loc_level=` for a loc on a bridge deck; see verbs-pointer.

## Seam pass matthew-mbp-m4-b62-seam1 (2026-10-05, batch matthew-mbp-m4-b62)

Content in the OSRS-Content commit tagged `[seam:matthew-mbp-m4-b62-seam1]`; the driver, grader,
workflow and conformance changes in the parent commit with the same tag. Every run used
`build/orchestrator/worktrees/b61-engine/src/torirs_b61engine` (no engine change this pass).

(a) **The Karamja glider crash pages abort on `npc_coord with no active npc`** -- FIXED,
`area_gnome/scripts/gnome_glider.rs2`. After "Take me to Karamja please!" the script
`p_teleport`s the player to the wreck (2917,3058) and keeps talking as the op's npc, the pilot on
top of the Grand Tree about 450 tiles away. This engine retires a static spawn beyond
`TORIRSSERVER_STATIC_SPAWN_OUT` (224) of every player (`torirs_server_world.c`
`world_static_npcs_sync`), so the first `~chatnpc_anim` aborted (`chat.rs2:273`). LostCity's
engine keeps every npc live and needs nothing. The port now binds the wreck's pilot with
`npc_find(coord, pilot_grand_tree, 5, 0)` (m45_47.spawn 2918,3057) and `error()`s if it is absent,
and the crash-site test is LostCity's box `inzone(0_45_47_35_42, 0_45_47_45_53, npc_coord)`
(LostCity `gnome_glider.rs2 [opnpc1,gnomepilot]`), not a square round `^gandius`, which matched the
Karamja glider station's pilot instead. The rule for any port script: one that teleports the player
more than 224 tiles and goes on talking must rebind the destination's npc first. Proof: scratch
`seam1_glider_after` 9/9, crash pages "Sorry about that." .. "Take care little man."; grandtree copy
184/184. OPEN (owner, design): should the engine keep a static spawn that a suspended script holds?
Not changed.

(b) **The Keldagrim Consortium's wide stairs land in a pocket sealed by crates** -- FIXED,
`quest_giantdwarf/scripts/gdwarf_consortium.rs2`. Every press of
`dwarf_keldagrim_wide_stairs_upper` used to `p_teleport` to 2895,10210,0, inside the east stairs'
own footprint and boxed by crates, so a walk off it timed out; both tests detoured by Varrock
Teleport and the GE trapdoor. The two name bindings now call `~climb(1)` / `~climb(-1)`, which asks
`maplink.dbrow` for the copy pressed (the rows the binding used to shadow; docs/MAPLINKS.md), so
the player lands beside that staircase. Up: 2863,10209 -> 2862,10209,1; 2863,10188 -> 2862,10188,1;
2894,10209 -> 2896,10209,1; 2894,10188 -> 2896,10188,1; 2930,10180 -> 2930,10179,1. Down lands on the
far side: 2865,10209 / 2865,10188 / 2893,10209 / 2893,10188 / 2930,10182, level 0. All four
Consortium landings are one floor with the Blue Opal secretary (2869,10205) and director
(2867,10203); the west pair is 7 tiles from both. The stage gate, the two `mes` lines and both stage
writes are unchanged. Proof: probes `kel_wsp_s1a` 35/0 and `kel_wsp_s1b` 12/0; copies with the
detour removed, forgettabletale 307/0 and giantdwarf 379/0. OPEN: the gate and the market `mes`
lines also fire on the 2930,10180 pair, a separate building; no source says whether they should.

(c) **A quest's name binding on a shared staircase sent Veldaban's HQ stair into the Laughing
Miner** -- FIXED, `quest_forgettabletale/scripts/forget_brewing.rs2`. `[oploc1,dwarf_keldagrim_stairs_lower/upper]`
were unguarded, and a name binding beats the locs' `category=climb_up/climb_down`, so every copy
(ten lower ones, Veldaban's HQ at 2828,10215 among them) teleported into the pub. Each binding now
guards on the pub copy's `loc_coord` (2915,10196, levels 0 and 1) and every other copy falls
through to `~climb(1)` / `~climb(-1)` (the `warriorsguild_doors.rs2 [oploc1,spiralstairs]` and
`zogre_finish.rs2 [oploc1,ladder]` shape). The rule: a quest file that binds a shared stair or
ladder by name guards on `loc_coord` and falls through to the category's `~climb`. Proof:
`lmstair_before` hqStairUp landed 2916,10193,1 with the pub line; `lmstair_after` 10/10, the HQ
stair lands on its maplink dest 2828,10214,1 and the pub stair still lands 2916,10193,1.

(d) **Brutus's specials: a stationary attacker dodged, and no sidestep could** -- FIXED,
`quest_idesofmilk/scripts/idesofmilk_locs.rs2`. Both specials were measured from `npc_coord`,
which for a size-N npc (and the driver's npc row x,z) is its SOUTH-WEST tile, so a player beside
his east side at z+2 stood outside the charge lane. Both zones are now measured from the 3x3
footprint (`[proc,cowboss_side]`, `nc_size(npc_type)`), the charge lane is the footprint swept 0..4
tiles (wiki Brutus Strategy table: "Dash forward 4 Tiles"), and the special resolves after 3 ticks
instead of 1. Two general facts came with it: measure a big npc's zones with `nc_size`, never from
`npc_coord` alone; and a telegraphed special queued with `queue*(x, 1)` can never be dodged,
because the player phase runs queues before movement (`torirs_server_world.c` `phase_player`),
so use the telegraph sequence's own length (`configs/all.seq` frame cycles / 30; both Brutus
telegraphs are 90 cycles = 3 ticks). Proof: `brutus_after2_b62` 15/15 stationary specials hit,
sidesteps dodged 4/4; idesofmilk copy 92/0 with `killBrutus.margin` below full hp.

(e) **The Crandor ropes are not missing** -- comment fix only, `quest_dragon/scripts/crandor.rs2`.
The header said LostCity's `crandor_rock_opening` / `crandor_climbing_rope` / `elvarg_gate_*`
were absent and "deferred". They are LostCity NAMES the cache does not carry; the OSRS locs exist:
`dragon_slayer_qip_ruin_entrance` (the Hole, a maplink), `dragon_slayer_qip_climbing_rope`
(2833,9657, `category=climb_unqualified` -> `~climb(1)` -> maplink rows -> 2834,3258,0) and
`dragon_slayer_qip_stalagtite_jump`. The walk out of Elvarg's lair: the secret wall from 2836,9600
to 2836,9599, then `walk_route` 2834,9593 / 2834,9585 / 2842,9585 / 2847,9582 / 2851,9578 / 2855,9574
/ 2855,9569, then `climbing_rope2` (volcano.rs2) -> 2856,3166,0 on the volcano rim, "You climb up the
hanging rope..." / "You appear on the volcano rim.". A rope or hole between a cave (z >= 6400) and
the surface keeps level 0: grade it with `t.player.climb` (verbs-pointer: A climb that changes no
level). Before you call a loc missing because a LostCity handler name is absent, look up the OSRS
loc's `maplink.dbrow` rows and its `ladders.loc`/`maplinks.loc` category. Proof: scratch
`b62s1_crandor_ropes` 12/0.

(f) **"I can't reach that!" right after a teleport out of a fight is Auto Retaliate** -- not a
bug (LostCity-faithful). An attacker's hit inside the teleport's `p_delay` queues
`[queue,playerhit_n_retaliate]` (skill_combat/combat.rs2); it runs after the landing and
`p_opnpc(2)`s the npc left behind, whose route fails. Seen in crest's
`returnCrest.varrockTeleport.cast` after a respawned Chronozon (respawnrate 30, aggressive) reached
the player during a 140-tick kill wait. Repro `crest_reach_repro1`: the line in 6 of 8 attempts,
exactly the six with a hit between the press and the landing; with `::setvar varp172_option_nodef 1`
(Auto Retaliate off) 0 of 8. The row still PASSes. A test that must not see it teleports before the
respawn. OPEN (owner, design): should a landing drop a retaliate queue whose attacker is out of
reach? No source says so; not changed.

(g) **Zogre Flesh Eaters' tomb doors teleport to the boss floor from either side** -- FIXED,
`quest_zogreflesheaters/scripts/zogre_finish.rs2` `[proc,zfe_tomb_door]`. Each press used to
`p_teleport` to 2480,9446,0 on the boss floor, so the stairs were skipped and a player on the boss
floor had no walk out (the test left by Camelot Teleport). Each press now walks the player through
the leaf pressed to its far side (the `door_walkthrough_try` shape, `~check_axis_locactive` +
`~door_open`). The gate is 2009scape's (`ZogreFleshEatersListeners.kt:51-73`, from both sides):
stage >= 9 passes silently, the Ogre Tomb Key passes with "You use the Ogre Tomb Key to unlock the
door.", no key gives "The door is locked.". Route (wiki Zogre_Flesh_Eaters oldid 15328252): the
outer pair 2441-2442,9433,2, the inner pair 2440-2441,9426,2, then `ogre_stairs_down` 2443,9417,2 ->
2442,9417,0; out the same way. `^zfe_tomb_past_door` is deleted. Proof: `zfe_doors_after` 19/19;
copy 178/0, helper_coverage FULL.

(h) Driver, grader and tools: `t.player.climb` grades a same-level landing in another map frame
(verbs-pointer: A climb that changes no level); `t.player.walk_to` answers a detail on success
(traps-01-12, verbs-pointer); `helper_coverage.py` no longer credits an NpcStep to a row only
named after the npc (coverage-and-gate); `run.py` takes one quest id per call and honours
`TORIRS_QUEST_NO_PUBLISH` (running).

## Seam pass matthew-mbp-m4-b62-seam2 (2026-10-05, batch matthew-mbp-m4-b62)

Content in the OSRS-Content commit tagged `[seam:matthew-mbp-m4-b62-seam2]`; the grader change in
the parent commit with the same tag. Every run used
`build/orchestrator/worktrees/b61-engine/src/torirs_b61engine` (no engine change this pass).

(a) **The Dorgesh-Kaan station doorway lands on a walled-off train platform; `walk_to` Tegdak
stalls at 2488,5536** -- FIXED, `quest_anothersliceofham/scripts/slice_zanik.rs2`.
`slice_goblin_station_entrance` (2695,5277,1) had one maplink row, `[maplink_1_42_82_8_29]`, dest
2488,5536,0: the FINISHED station's platform, a 296-tile room the static map walls off from the
dig where Tegdak works (2512,5562), with no quest-state test. A new name binding
`[oploc1,slice_goblin_station_entrance]` lands at `^slice_railway_dig_landing_coord` 2520,5607,0
(the tile in front of the dig's own exit doorway, which blocks its own tile) while
`%varb3550_slice_quest < ^slice_complete`, and falls through to `~maplink_transition` after, so the
platform landing is unchanged at state 11; `maplink.dbrow` is untouched. Sources: Quest Helper
`AnotherSliceOfHam.java:197` (the railway zone 2505..2523 x 5527..5630 is the dig) and
:272/:275 (enterRailway -> talkToTegdak 2512,5564); the wiki quick guide (oldid 14458352,
Excavation: "Walk south on the tracks and talk to Tegdak"). The pattern: a maplink row whose
destination is an area's post-quest state needs a quest-state name binding that shadows
`[oploc1,_maplink_transition]` and falls through to it (gaps-world: A cave or tunnel click answers
a chat line; `curseofarrav.rs2:223-235`). Proof: `seam2_dorgesh_before` landed 2488,5536 and the
walk stalled; `seam2_dorgesh_after` 10/10 (lands 2520,5607, walk reaches Tegdak in 23 ticks,
`::complete` then lands 2488,5536); a copy of the fixer's test with the climb dest changed and the
platform block deleted, `seam2_dorgesh_slice_copy`, 140/0. OPEN (not changed): the return doorway
lands on 2695,5277,1, the station doorway's own tile; the reverse maplink row says 2696,5277.

(b) **`helper_coverage` charges a goto OUT of a sealed pocket** (`sealed_exits`,
coverage-and-gate). Before it, Another Slice of H.A.M.'s `goto-talkToTegdak` off the train
platform read FULL. Landing it reopened Holy Grail (`goto-talkToFisherman` out of the 320-tile
pocket east of the Black Knight Titan, where `defeat_titan`'s `p_teleport(movecoord(npc_coord, 1,
0, 0))` lands the player; from the titan's near side the walk reaches the fisherman in 11 ticks,
so the landing side may be the content bug -- check LostCity and the wiki first) and Watchtower
(`goto-leaveGrewIsland` over the water instead of `tree_ropeswing3`). OPEN: the exemption accepts
any press of a pocket op loc in the 500 ticks before, so a test that swings INTO a pocket and gotos
out within 500 ticks is not charged (Watchtower's `goto-leaveGrewIsland2`); a tighter rule needs
to know which op locs leave a passage open.
