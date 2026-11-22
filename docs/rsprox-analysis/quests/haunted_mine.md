# Haunted Mine, from RSProx

Rebuilt 2026-11-22 from the owner's recording, index 4647 (rev 242.1, 7,174 ticks, recorder
LoveTori): the whole quest, stage 0 to 11, in one session. Index 4645 (another account, Das
Killers) only logs in with the quest complete; its login varbits show what Jagex keeps (below).
Before this the quest was a Quest Helper reconstruction: invented stages (0/1/9/10/11),
"original wording" pages, a 48-second invented valve timer, no points panel, no dark rooms, no
cart run, no shuttle carts, no hazes. Everything here is from 4647 unless marked UNVERIFIED.

Files (OSRS-Content `quests/quest_hauntedmine/`): `hauntedmine_zealot.rs2` (the Zealot),
`hauntedmine_dungeon.rs2` (entrances, ladders, lift and valve, fungus, cart puzzle, shuttle carts,
hazes, possessed pickaxes), `hauntedmine_dayth.rs2` (flooded stairs, Dayth's room up to the fight,
crystal doors, completion), `hauntedmine_journal.rs2`, `hauntedmine_dayth_fight.rs2` (the fight:
[haunted_mine_dayth.md](haunted_mine_dayth.md)), `configs/quest_hauntedmine.constant`,
`.varp`, `hauntedmine_mine.npc`; `maps/multiway.csv` (square 43,69 is multi-combat, as recorded).
Tests: `test/quests/hauntedmine.lua` (the quest, recorded order plus every recorded failure;
221/221, 221/221, 217/217 on scriptrun under three run names, the count varying with the fight's
length) and `test/quests/_hauntedmine_dayth.lua` (the fight, 17/17).

## Stages (varp 382, perm, transmit)

| value | written | with |
|---|---|---|
| 1 | [633] | the topic menu after the history; "You've started a new quest" |
| 3 | [859] | the first crawl through hauntedmine_secondary_entrance, on its op tick |
| 4 | [1030] | the first try of the locked valve, with its line |
| 5 | [2703] | the first powered ride down ("You get in the lift. Now powered...") |
| 6 | [2740] | the first flooded stair taken without a fungus (a dark room) |
| 7 | [5330] | the first east stair taken with a fungus (Dayth's room) |
| 8 | [5543] | the continue of the key's mesbox (Dayth rises) |
| 9 | [6944] | the killing strike |
| 10 | [7053] | the first pass through the crystal doors |
| 11 | [7080] | the first shard cut: the completion |

There is no 2. The bit word, varp 383 `hauntedmine_bits`, was `temp, transmit=no` in the
generated block: no lever, cart, lift or key bit reached the client (the points panel draws
every lever and the cart run from it: clientscript `hauntedmine_controls_update_3820`, onvartransmit
of varp 383) and all of it was lost at logout. 4647 sends every write; 4645 logs in with
pointspuzzlestarted, levers a b e i, liftpoweredonce and heardaboutkey set. Now perm + transmit
(the generated lines deleted).

## The Zealot ([531..741])

Every recorded page is in `hauntedmine_zealot.rs2` verbatim with its mood (Jagex's double space
after "Then begone." kept, "endeavoring" as sent). The topic menus follow one rule, 9/9 recorded
menus: the topics he can still answer without the one just asked, then "I must be going now."; the
menu opening a later talk has "Can you explain the history of these mines again?" first and no
farewell; "Is there any other way into the mines?" stands for the key's three topics until he has
told of the key (heardaboutkey, written with the menu after its second page, [676]); after it a
five-row menu (where / discreet / borrow / artefact / slaughter, [676]).

| branch | source |
|---|---|
| Hello there; allegiance; Saradomin -> "I could ask the same of you." -> Yes -> history -> topics | recorded [542..633] |
| Zamorak -> "Fine, I didn't want to talk to you either." | recorded [549..557] |
| Guthix -> "I can make my own choices" | recorded [566..572] |
| No allegiance -> "Why, what are you doing?" -> the start menu | recorded [579..587] |
| other way, artefact, slaughter, where, discreet -> "I can't see anyone behind you.", borrow -> please -> trade, farewell | recorded [648..730] |
| challenges, Ok I lied, purpose, leave you, No to the start, consolation, "And I would be one of them", right, nuts, didn't want, safer, pretty please, the history again, "What is this quest", after Dayth | UNVERIFIED: the wiki transcript (moods guessed) |

Pickpocket: before heardaboutkey "I doubt he's got much of value on him." on the op tick, nothing
else ([537], [661]); after it `human_pickpocket` + `pick` (2581), "You pick the zealot's pocket and
retrieve a small silvery key.", the key, no xp, no stun ([741]). UNVERIFIED: a second key while one
is held (refused the same way).

## Moves (every one recorded)

Ladders: the landing is the player's own tile shifted by a fixed offset per ladder name, 62 of 62
climbs: `laddertop` (-640,-5056), `ladder` (+640,+5056), `_1sw` (-64,-64), `_1ne` (+64,+64), `_1e`
(+64,0), `_1w` (-64,0). Shape: face O, `human_reachforladder` O+1, move O+2 ([908..913],
[1631..1634]). Crawls the same with `human_crawling` at 20 and the seq cleared on the move tick.

| loc | to | cites |
|---|---|---|
| hauntedmine_main_entrance 3452,3243 / _inside 2790,4589 | 2792,4592 / 3454,3242, no anim | [505], [518] |
| hauntedmine_obstacle_cart 3445,3236 | exactmove 3446->3444,3236 (30..100, west), `human_walk_crumbledwall` at 30, `climb_wall` at 40, O+1 | [851], [2467], [3013], [6192]; the east-bound climb UNVERIFIED |
| hauntedmine_secondary_entrance 3440,3232 | 3436,9637 (stage 3 on its op tick) | [861] x4 |
| hauntedmine_back_entrance1 3430,3233 / its _inside 3404,9631 | 3405,9631 / 3429,3233 | [3092] x3 / [1611] x4 |
| hauntedmine_back_entrance2 3429,3225 / lotr_back_entrance1_inside 3408,9623 | 3409,9623 / 3428,3225 | [1622] x6 / [2019] x4 |
| lift_side_l / _r 2807,4494 / 4492 | 2725,4456, then the wade: four steps south T+2..T+5 | [2703], [3210], [5283], [6667] (4/4) |
| lift_flooded 2726,4456 | answered from the shore (Chebyshev 4), the wade back C+1..C+5, `watersplash` + `lift_fall` C+3, 2807,4493 at C+6 | [3252..3258] |
| hauntedmine_dark_stairs_top west 2692,4436 | no fungus: 2712,4593 (dark); fungus: 2758,4454 | [2740]; [5434], [7031] |
| hauntedmine_dark_stairs_top east 2746,4436 | no fungus: 2730,4563 (dark); fungus: 2811,4454 | [2801], [2813]; [5330], [5501], [6696] |
| hauntedmine_dark_stairs_bottom west / east | 2691,4437 / 2750,4437, cam_reset | [2767] / [2811], [2817] |
| hauntedmine_light_stairs_bottom 2755,4452 / 2812,4452 | 2691,4437 / 2750,4437 | [5467] / [5400], [6998] |

Dark room: the move on the op tick, then the player's page "It's kind of dark down here. I should
probably find some form of light before going further." (chatneu2) the tick after ([2741], [2802],
[2814]). UNVERIFIED (wiki): "Hey - my light source isn't working!..." for a lit light (a soaked
one can't be: below).

## The lift and the valve

- The valve answers on the op tick. Locked: "The valve seems to be locked in position. There is a
  small keyhole in the side." (+ stage 4 the first time, [1030], [2625]). The key on it: "The key
  unlocks the valve." then "You open the valve. Water begins to flow through the lift mechanism."
  and `squeaky_valve`, the key kept ([2631]). Open: "The valve is already open." ([3291]).
- The race (stage 4 only): the cheeky ghost is added 49 ticks after the valve at 2802,4516 with
  `ghost_summon2` and "Ooooo Wooooo Woo", glides a tile a tick south and south-east to the valve
  (20 ticks), and the valve is shut 8 ticks later ([2680..2708]) whether or not the player has
  gone down. From stage 5 a turned valve stays open, with no ghost ([3177] open, still open at
  [3291], [5283], [6667]). A valve reopened at stage 4 starts a new race (the test walks it).
  UNVERIFIED (wiki): "You hear the sound of a valve being turned." to a player still in the room.
- The lift: unpowered, a mesbox "The lift doesn't respond, it needs to be powered in some way."
  ([1076]); powered, the line and `lift_fall` on the op tick, "...plunging you straight into the
  middle of a chamber flooded with water." two ticks on, and at T+5 "The water soaks your
  tinderbox!" with `sizzle`: the tinderbox damp, every lit light put out ([3215]: a lit bullseye
  lantern and two candles).
- The chisel is a ground spawn (2800,4500 and 2803,4491; `opobj3`, `human_pickuptable_walkmerge`,
  `pick2`, [1062..1066]), not the crate. The crate has no recorded op.

## The fungus

Pulled from the pools on the op tick, no anim: "You pull the fungus from the water. It is very cold
to the touch." + `pick`; a full pack: "You don't have enough inventory space to hold the fungus."
([1736], [1779]). Dropped: ashes on the floor, "When you drop the fungus it crumbles mysteriously to
dust.", `put_down` ([1739], [2648]). Carried out of a crawl: the mesbox "As you crawl out into the
sunlight, the strange fungus you are carrying crumbles to dust.", the fungus ashes in the pack
([2019]); by teleport: "The strange fungus you are carrying crumbles to dust." ([2355], [4683],
[6053], [7117]; ours on a 1-tick timer, so up to a tick late).

## The cart puzzle

- The points panel is interface `hauntedmine_controls` (144) in the main modal
  (`toplevel_mainmodal_open(-1,-1)`). The first look writes pointspuzzlestarted and sets the
  starting levers ([1820]: a and d up). UNVERIFIED: the roll (each lever a coin, never the
  solution; the wiki: "The starting setup differs per player").
- Levers 1..8 -> bits b a c d e i j k (32 pulls): the bit, "You pull the lever. The old points
  creak into place.", `points_change`, `hauntedmine_point_lever_inactive` for one tick. The flooded
  three: "You can't reach across the flooded track to pull that lever."
- Carts: the fungus on the start cart (2778,4506): "You place the glowing fungus in the mine cart.";
  searching either cart: "The cart is empty.", or "There's a glowing fungus I placed in here
  earlier." Take it / Leave it; a full pack gets it at its feet (private, 200 ticks, [2547]).
  UNVERIFIED (wiki): the fungus on the far cart ("I just went to all the effort...").
- The route, fitted to all 14 Start presses (every one matched): a=0 returns; b=0: e=1 sinks at
  38_42, else i=0 39_50, i=1 37_47; b=1: c=1 38_44, j=1 35_59, else the solution ABcdEIjk (4/4
  success). UNVERIFIED: d or k up on the success branch (taken to sink at 35_59), and the 35_59_v2
  figure (never shown).
- The run's frame, from the press P (14 presses, identical): P animate_start + every figure hidden
  + the camera on the start cart (a jump); P+1 panel shut, `cart_away`, the camera follows (rate
  1/10); P+4 the outcome's bit, the panel back with its figure, the camera on the outcome; P+6 panel
  shut, `cart_away` or `pool_plop`; P+9 a sink: "Oh dear, the mine cart seems to have sunk.", the
  fungus lost, cam_reset, panel back; P+11 returned ("How useful, it's come right back to where it
  started.") or through (the page "I wonder if that cart is anywhere useful now?" when a fungus
  went), cam_reset, panel back.

## The mine's ambience

| type | recorded | ours |
|---|---|---|
| hauntedmine_cart1..4 | straight tracks, a tile a tick, idle 2 ticks at each end (cart4: 3 at its south end); 451/87/294/118 steps | scripted shuttle, same tracks and idles |
| the shove | a cart stepping into the player pushes them a tile a tick ahead, exactmove 25..45 facing the cart, `human_stumble_back_continuous` at 25, from the 2nd tick a 0..2 splat with `cart_hit` until the cart stops ([1207..1218], [1274..1281]) | same; UNVERIFIED: the 2 of 11 / 2 of 8 ticks with no splat are not reproduced |
| hauntedmine_suprise_ghost_hidden (Haze) | wanders (656 steps, 114 lives); 2 rose in 137 recorder-ticks within 2 tiles: `bossghost_fade_in` facing the player, 2 ticks on the level-29 Ghost; fades (`bossghost_fade_out`) and is a haze 2 ticks on when left ([1515..1548]) | the same, a 1-in-64 roll a tick within 2 tiles (fitted); swing `ghost_update_tendrill_attack` + `ghost_attack`, defend `ghost_hit` |
| possessed_pickaxe_stationary ("Iron pickaxe", Take) | Take: `pickaxe_summon` on the op tick, 2 ticks on the level-50 possessed pickaxe facing and fighting ([5308..5314]); back when nobody is near ([5476]) | the same; swing `magicaxe_attack` + `baxe_slash` |
| skeletal_miner_sitting (Corpse) | 506 lives, never moved or changed | nothing (the wiki's ambush not recorded) |

## The end

- Dayth's room door (2799,4453): a walk-through, nothing stays open: an inviswall in its slot,
  `inactivepoordoor` on 2798,4453 rot 1 for two ticks, `door_open`, the step across ([5354],
  [5393], [5518], [6985]).
- The key: at stage 7/8 with no Dayth up, the reach (`human_pickuptable` at 10), the key floats a
  tile north and back, the camera on it (eye 2792,4456 h400, look 2788,4455 h80, jumps),
  `ghast_appear`; C+4 cam_reset and "What the... ?" (chatcon1); the voice's mesbox; its continue
  writes stage 8 and Dayth rises ([5534..5543], again at stage 8 [6713..6720]). The fight is
  [haunted_mine_dayth.md](haunted_mine_dayth.md). After the kill: the reach, the key, its objbox
  "You take the key. It doesn't resist anymore." the tick after ([6966..6968]); a second: "You
  already have a key like that." ([6972]). UNVERIFIED (wiki): "I should deal with that ghost before
  trying to take the key again." while he is up.
- The crystal doors (2772..2773,4450): without the key "This door appears to be locked." +
  `locked` ([5445]); with it both leaves walk through: inviswalls, `inaccastledoubledoorr` rot 0 /
  `l` rot 2 on z-1 for two ticks, `nicedoor_open`, the step south, stage 10 the first time
  ([7053]). The front-entrance pair is locked to everyone recorded ([510], [515], [961]).
- The crystal: the chisel on `crystalcorner`: "You cut a shard from the crystal.",
  `human_pickuptable_walkmerge` at 5, the salve shard; the first is the completion: stage 11, the
  line, Quest Complete 1 (jingle 152, a new row in `quest_complete_jingle_recorded`: difficulty 2
  would have played Quest Complete 2), the scroll with the salve amulet (`crystalshard_necklace`)
  and "22,000 Strength XP" / "Access to Tarn's Lair", the rating ([7080]).

## The journal

Recorded at stage 0 (the overview, [248]; `quest_overview_start.rs2` already had the recorded
line) and stage 10 ([7065]: 17 rows, eight struck paragraphs + the current step). The test reads it
row for row at stage 10: 17/17. Every other stage is built from the struck paragraphs, the last as
the current step in navy: UNVERIFIED wording.

## Action inventory (`rsprox_actions.py`, 4647) -- a verdict per row

| action | verdict |
|---|---|
| Keldagrim doors, rowdy dwarf, boatmen, ferryman, banks, GE, Varrock bank booths | generic content the session exercises; not Haunted Mine (the Keldagrim lines are Forgettable Tale's session, 3draster-09) |
| book of scrolls -> Mort'ton teleport; Varrock teleport; ring of dueling (worn) -> Castle Wars | generic teleports (their own ports); not checked here |
| burgh fence gates, burgh agility shortcut, the Burgh woman | Burgh de Rott generic; not checked here |
| stamina/ranging/prayer potions, sharks, combine/drop doses, eat | the recorder's kit / generic consumption |
| quest list / journal / overview | journal: checked (above) |
| hauntedmine_main_entrance, _inside, rewarddoor_l/_r (front) | this content (Moves; The end) |
| saradominist_zealot op1 / op3 | this content (The Zealot) |
| hauntedmine_obstacle_cart, _secondary_entrance, back_entrance1/2 (+ insides), lotr_back_entrance1_inside | this content (Moves) |
| small_bat, bat, rat attacks; telegrab on npcs ("You can't use Telekinetic Grab on them.") | the mine's generic monsters / the engine's telegrab refusal |
| lotr_mine_wall_clay/tin/copper/coal, drop clay/ashes | generic mining (the full-pack mesboxes are generic) |
| every hauntedmine ladder | this content (Moves) |
| lift_side_l/_r, lift_valve (op1 and key), lift_flooded | this content (The lift and the valve) |
| chisel (ground) | this content (spawn) |
| skeletal_miner_sitting examine | cache examine |
| puzzle_cart, glowing_mushroom/2, points_info, levers 1..8 and flooded 9..11, hauntedmine_controls:start | this content (The cart puzzle) |
| tinderbox on lantern, damp tinderbox on candles/lantern/itself ("Nothing interesting happens."), drop it | the soak (ours) / generic lighting; the damp tinderbox's refusals are the generic default |
| possessed_pickaxe_stationary Take | this content (ambience) |
| hauntedmine_boss_door, boss_key (op1, examine), boss_ghost attacks, prayers, spec | this content; the fight is haunted_mine_dayth.md |
| macro_niles (random event) | its own port |
| hauntedmine_light_stairs_bottom, dark_stairs_top/_bottom | this content (Moves) |
| lift key on the crystal door ("Nothing interesting happens.") | the generic default, as recorded |
| crystalcorner (op1, chisel), quest rating | this content (The end); UNVERIFIED: the Cut op alone ([7079] sent nothing) |
| ball of wool on the salve shard, bank | generic crafting (stringing the salve amulet) |

Every TELEPORTS row of the session is one of the moves above or a generic teleport, except the
player "teleports" during the fight: cart shoves (haunted_mine_dayth.md).

## Wire audit (scriptrun, `test/quests/hauntedmine.lua` tick log)

Matched on our tick log against the recording's frames: the obstacle cart (840 at 30, `climb_wall`
at 40); stage 3 on the crawl's op tick, `human_crawling` O+1, the move and seq clear O+2; ladders
(`human_reachforladder` O+1); the haze's swing (`ghost_attack` + the player's block); the valve
(`squeaky_valve`) and the race's shut at +77; the ride (`lift_fall`, the soak at T+5 with
`sizzle`); the ride up (`watersplash` + `lift_fall` C+3); the fungus (`pick`, `put_down`); every
lever (the inactive model one tick, `points_change`); both run shapes (P+1 `cart_away`, P+4 the
outcome bit, P+6 `pool_plop` / `cart_away`, P+9 the fungus lost). The tick log has no camera or
interface rows; on `TORIRSSERVER_TRACE_OUT=1` a run's P, P+1 and P+4 carry the recorded packet
kinds and counts (P: varp 383, 8 IF_SETHIDE, CAM_MOVETO_V3, CAM_LOOKAT_V3; P+1: varp, IF_CLOSESUB,
8 IF_SETHIDE, SYNTH_SOUND, both CAM; P+4: varp, RUNCLIENTSCRIPT 2524, IF_OPENSUB, 1 IF_SETHIDE, both
CAM). Bodies (`=2`) not diffed.

## Open

- The fight's own open list: [haunted_mine_dayth.md](haunted_mine_dayth.md) (the player leaving
  while he is visible, logout/death, the pickaxe source-distance fit, facing re-sends, the two
  boss carts the generated spawn places outside their recorded stretches).
- `test/quests/_conformance.lua` seam.npc_cover_settled_recovery now raises Dayth the recorded way
  (stage 7, the two pages, the rise) -- not run. seam.npc_tele_reslot_followed tests a 3+-tile
  npc_tele re-slot that Dayth no longer makes (he glides 1..2 tiles and changes form): it needs a
  new subject.
- Camera and interface packet BODIES of the cart run and the key's float: kinds and counts only.
- Stages 1..9 of the journal: UNVERIFIED wording; the completed journal unrecorded.
- The lever roll; the success branch with d or k up; the 35_59_v2 figure.
- The shove's missing splats; the haze rise rule is a fit on two rises.
- The front crystal-hall doors after the quest (the wiki's shortcut) stay locked: unrecorded.
- Skeletal miners never ambush (unrecorded); the chisel crate has no op (unrecorded).
- Our map blocks the pool's south rows (z 4453..4455), which Jagex's player walks; both wades are
  scripted steps, and lift_flooded answers from the shore (as recorded), so nothing depends on it.
