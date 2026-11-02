# Wanted! (RSProx index 4464): the generic interactions

Index 4464 is rev 241, recorded 2026-11-02. This file covers everything the recorder did that is
not the quest itself. The quest is `wanted.md`, owned by another session. The row list comes from
`tools/rsprox/rsprox_actions.py` and each shape from `tools/rsprox/rsprox_interact.py`. Ticks are
`[n]`, L is the tick the walk ended, and C is the click tick.

The checks ran on 2026-11-02 as one scratch scriptrun (`torirsserver --scriptrun`, private packs
built from a mirror that used HEAD's `quest_wanted/` and `player/death.rs2`). They cover the vial
smash order, the super restore lines, petting Sir Burrus, Turael's assignment, menu and slayer
gem, the Barnaby charter landing (2775,3233,1) and its lines, and the Champions' Guild door (the
loc_set and sound rows in the tick log). The black knight death sound was not run: its params
resolve in the servpack with 0 unresolved names.

| row | recorded (4464) | ours before | verdict | file |
|---|---|---|---|---|
| [173..202] Sir Tiffy's shop, Value on 4 `basic_tk_*` | Opened from his Talk-to "Can I buy some armour?" [195..197]: `templeknight_armoury1`, 4 x 30, "Initiate Temple Knight Armoury". The value lines are "Initiate harness m: currently costs 20,000 coins." etc. | Same stock, title and line (`~shop_value_buying`). Note: `[opnpc3,rd_teleporter_guy]` is dead because the cache gives him no op3. | **matched** (the dialogue route is the quest's, see below) | shop/falador_park/ |
| [539] xmas24_corgi_final (Sir Burrus) op1 "Pet" | L+1 [540] "You pet the dog..." + human_pickupfloor. L+3 [542] "He smiles at you.". He keeps wandering. | No binding and no spawn | **fixed** (binding + spawn 2959,3336,2, fitted to 77 recorded positions) | areas/falador/scripts/sir_burrus.rs2, areas/falador/configs/sir_burrus.spawn |
| [626] drop ahoy_bedsheetgreen | `obj_add_specific despawn=300 ownership=self neverturnpublic=true`, synth put_down on C | `~dropslot` | **matched** | player/scripts/drop.rs2 |
| [633] stamina, last dose | Lines: "You drink some of your stamina potion.", "You have finished your potion.", "You quickly smash the empty vial using the trick a Barbarian taught you.", all spam and in that order. The pooled 44xx sessions agree 172/172. | Smash line first, as a gamemessage | **fixed**: `~potion_finished_message`. The slot is deleted silently, and the smash line comes after "finished", spam. | player/scripts/consumption/consume_shared.rs2 + the 12 potion files that said "finished" |
| (same) super restore | 119/119 recorded drinks say "You drink some of your super restore potion.". "You have N doses of potion left." is spam. | "...your restore potion.", and the 2/3-dose lines were `mes` | **fixed** | player/scripts/consumption/prayer_potion.rs2 |
| [2341] lobster | "You eat the lobster." and "It heals some health." (spam), human_eat, synth eat 2393 | `@eat_food` | **matched** | general/scripts/food.rs2 |
| [4721] 4-dose prayer restore | "You drink some of your restore prayer potion.", "You have 3 doses of potion left." (spam), synth 2401 | Same (already ported from 4433) | **matched** | player/scripts/consumption/prayer_potion.rs2 |
| [848] games necklace Rub > Burthorpe (subop 0) | C: "You rub the necklace...", cast, sound_area 200. C+3: "...has three uses left." and the teleport to 2899,3551 | 2899,3553 r3 | **matched** (inside the box) | general/scripts/enchanted_jewellry/necklace_of_minigames.rs2 |
| [872] Turael op3 Assignment | L+1 [874]: slayer vars, then "Your new task is to kill 28 Goblins." chathap1. [877]: menu "Got any tips for me?\|Okay, great!" (Okay, great! is answered "Good luck! Don't forget to come back when you need a new assignment." chathap2: 4033 [821], 109, 204, 213) | Lowercase name, neutral mood, no menu | **fixed**: `name_uppercase`, happy mood, the menu and "Okay, great!" branch. **OPEN**: the tip page. Its text is the server's `slayer_tip` column, which is absent from our cache, so only the player's line is written. | skill_slayer/scripts/slayer_masters.rs2 |
| [883] Turael op4 Trade, [888] slayer gem x2 | "Slayer Equipment", inv `slayershop_pre_porcine`: 26 items (gem 50, ... broad arrows 50,000, boots_of_stone 50). The gem costs 1 coin. | 18 wiki-guessed items with no slayer gem | **fixed**: the recorded 26 in the recorded order and counts. **OPEN**: after Porcine, Jagex's `slayershop` (rsprox 3125/3262) adds slayer_reinforced_goggles. | shop/slayer_equipment/configs/slayer_equipment_shop.inv |
| [1108] fai_varrock_poor_door_flipped | L+1: loc_del rot 2 + open leaf 3256,3388 rot 1, synth 62 | doors.loc door_closed | **matched** | doors/ |
| [2023] fai_falador_castle_crumble_mid | L+1 [2030]: teleport 2936 to 2934, exactmove 30/100, human_walk_crumbledwall delay 30, synth 2453 delay 40 | Same (4421) | **matched** | skill_agility/scripts/agility_shortcuts_osrs.rs2 |
| [2057] deadtree2 op1 x4 | The tree sits beside the ladder. Reached at L [2060], faced at L+1, then the ladder click [2062] took over and nothing else was sent. | The no-axe line at L+1 after `p_arrivedelay` (4421 [939]) | **matched** (interrupted) | skill_woodcutting/scripts/woodcut.rs2 |
| [2062] ladder_outside_to_underground | L+1 face, L+2 human_reachforladder, L+3 to 2884,9796 | maplink row (4421) | **matched** | quests/quest_drunkmonk/scripts/quest_drunkmonk.rs2 |
| [2080] cauldrondoor_l | Wakes the S suit (line), then the N suit, then the walk-through: inviswalls + inactive leaves, synth 71 [2088] | Same (4421) | **matched** | areas/taverly/dungeon/scripts/prison_doors.rs2 |
| [2090] prisondoor_r | Both leaves swing, synth 71 [2093] | doubledoors + door_sound | **matched** | doors/ |
| [2198] castledoubledoorr | Both leaves, synth 62 | doubledoors | **matched** | doors/ |
| [2140] prayer15 x10 | prayer_allactive + protectfrommelee varbits, synth protect_from_melee 2676 | prayer.rs2 | **matched** (not traced further) | skill_prayer/scripts/prayer.rs2 |
| [2197]/[2247] black_knight / aggressive_black_knight | Attack human_sword_slash: synth 2500 for black_knight, 2h_slash 2503 for aggressive. Defend human_sword_def + synth steel 15 on the hit's landing tick (every one, 0s included). Death: human_death + synth human_death 512 on D, gone + drop at D+2 (3/3). | Combat matched. Death sound `null` (generated). | **fixed** (death_sound human_death; the generated null lines deleted) | quests/quest_blackknight/configs/quest_blackknight.npc, areas/taverly/dungeon/configs/taverly_dungeon.npc, _explicit/configs/server_fields.generated.npc |
| [2503] macro_highwayman (Rick Turpentine) | S [2500] add at 2890,9683 facing south. S+1: bow, "Good day to you, milord.", smokepuff + 1930 r7. Op [2503]: "Your reward is: 1 x Tooth half of key." + keyhalf1 + the page "Today is your lucky day, sirrah! ... past actions!" chathap3. The page closed at C+1 with no click (the recorder was in combat). | Committed (29e857e85a). Same lines, mood and table (keyhalf1 is on it). | **matched**; the combat close of the page is engine-wide, not checked | macro_rick_turpentine/ |
| [2658] / [4212] ectophial | C: "You empty the ectoplasm onto the ground around your feet..." (spam), ectophial_pour_spotanim 1273, ahoy_ecto_teleport 878, sound_area 2401 r5, ectophial to empty. C+3: "...and the world changes around you." + teleport to 3659..3660,3522. C+4: face 3659,3520, human_pickupfloor delay 10, synth 1132 delay 30. C+5: "You refill the ectophial from the Ectofuntus.", refilled. | `[opheld1,ectophial]` in ahoy_shared.rs2: "You empty the ectophial.", a spell teleport, a queued refill line. **Wrong.** | **OPEN**: the file is another session's uncommitted Ghosts Ahoy work. The recorded shape is here for them. | quests/quest_ghostsahoy/scripts/ahoy_shared.rs2 |
| [3024]/[3105] captain_barnaby op3 Brimhaven | L+1 [3109]: "You pay Captain Barnaby 30 coins." + fade (0,255 to 0,0, 50). L+4 [3112]: "You board the ship and sail to Brimhaven." + move to **2775,3233,1**. L+5: fade back. L+7: overlay closed. Pooled: Brimhaven 2775,3233 (4464, 4435 x2), Ardougne 2683,3263 (4360), Rimmington **2915,3221** (4372). | No pay line or fade, the sail line on the click, landings 3234 / 3222 | **fixed** | areas/area_ardougne_east/scripts/captain_barnaby_ferry.rs2 (and test/quests/totem.lua reads the sail line after the move) |
| [3116] ardougneshipplank_off | Step onto the plank, then 2772,3234,0 | `~gangplank_disembark` | **matched** | general_use/scripts/gangplank.rs2 |
| [3148] port_task_master_brimhaven Talk-to | The recorder walked off. No answer. | Bound (sailing/port_masters.rs2) | **n/a** (interrupted) | |
| [3446] championdoor | L+1 [3451] on the door's tile: step to 3191,3362, inviswall in the slot (rot 3), championdoor_open on 3191,3362 rot 0 for 2 ticks, synth bigdoor_open 46. Page "Greetings bold adventurer. Welcome to the guild of Champions." **Guildmaster** chathap2. A Varrock diary line comes first. | Step only, no locs or sound, the name "Guild master" | **fixed** (the diary task is not ours: OPEN, diaries) | areas/varrock/scripts/champions_guild.rs2 |
| [3452] fai_varrock_castle_door | Generic door | doors.loc + big_wooden_door sound | **matched** | doors/ |
| [3634]/[3648] goblin_ladder_bottom/top | Up: L+1 face, L+2 anim, L+3 to 2955,3497,**2**. Down from standing: anim on C, move C+1 to 2955,3497,0. | Same (4402) | **matched** | areas/falador/scripts/goblin_village/gobdip_crates.rs2 |
| [3653] goblin_outpost_poordoor | Door swing, synth 62 | doors.loc | **matched** | doors/ |
| [3659]/[3669] 100_goblin_ladder_down / _up_charred | Down into the RFD kitchen copy (L+3). Up: anim on C, C+1 to 2960,3506,0. | `^rfd_kitchen_exit` = 2960,3506 | **matched** | quests/quest_recipefordisaster/scripts/recipefordisaster_goblins.rs2 |
| [3673]/[3675] goblin_outpost_openpoordoor_double(r)_inner | The second click closes **both** leaves [3678], synth nicedoor_close 80. The first click was overridden. | doubledoors close + door_sound | **matched** | doors/ |
| [3957] qip_cook_trapdoor_open | L+1 face, L+2 human_pickupfloor, L+3 to 3210,9616 | maplink row (ladders.loc climb_down_ladder) | **matched** | ladders_stairs/ |
| [3965] spider op2 | The recorder walked away. No combat. | | **n/a** | |
| [3974] lost_tribe_cellar_wall | L+1 "You squeeze through the hole." + face. L+2 human_pipesqueeze. L+3 move 3219 to 3221 + unsqueeze. | Same (4454) | **matched** | quests/quest_losttribe/scripts/losttribe.rs2 |
| [3983] lost_tribe_guide op3 Follow (Kazgar) | L+1 "Kazgar shows you the way through the tunnels." (spam) + fade to 150. Move to 3309,9613 at +3. Fade back. | Same proc, marked unverified | **matched** (comment now cites 4464). One difference: Jagex walks the player 2 steps after Kazgar under the fade, while ours stands still. | quests/quest_losttribe/scripts/losttribe_mistag.rs2 |
| [4592]/[4735] ardounge_wizard op3 | The cast on C matches ours (curse_casting 92, the overhead, sound 127, curse_travel 61..100). Everything after it is Wanted!'s cutscene: fade, hidenpcops/locops, the instance, cameras. | `@teleport_to_essence_mine` | **matched** (cast). The landing is quest content. | skill_runecraft/scripts/essence_mine.rs2 |
| [4723] blankrunestone_exit_portal_2 | "You step through the portal...", curse_impact delay 30, curse_travel 0..30 (heights 62/124 raw). Next tick 2682,3324 + synth 200. | Same. Landing inside 2684,3322 r2. | **matched** | skill_runecraft/scripts/essence_mine.rs2 |
| TELEPORTS: tablets | Falador x7 (2964..2966, 3377..3379), Varrock x3, Ardougne x2 (2661,3302), Lumbridge 3220,3218 | 3x3 boxes in teleport_tablet.rs2 | **matched** (all inside) | skill_magic/scripts/spells/teleport_tablet.rs2 |
| TELEPORTS: ring of dueling worn op3 x3 | 2442,3091 / 2438,3092 / 2442,3091 | Castle Wars 2440,3090 r2 | **matched** (inside) | ring_of_dueling.rs2 |

## Seen in passing (not on my list)

- **Opening a quest journal in combat is refused.** "You can't do that right now." answered
  questlist op2 at [2203], [2204], [2205], [2268], [2500] and [2501]. Each came 3..5 ticks after
  the recorder's last hit. The open at [2209] (9 ticks after) and at [2188] (16 after) worked.
  Our `[if_button2,questlist:list]` never refuses. **OPEN**: the combat timer's length (between 5
  and 8 ticks) needs a second session.

## For the Wanted! session

- **Black Knight deaths in Daquarius's room.** [2324] aggressive_black_knight (2892,9681) and
  [2402] aggressive_black_knight (2892,9680) died with no page and no varbit. Each was
  human_death + synth 512 on D, then the npc deleted with coins 1 + bones at D+2. [2534] was the
  first **black_knight (516)** death (2890,9685). At D+2 [2536], on the same tick as its delete
  and drop, Lord Daquarius's page opened: "Stop! I will tell you what you want! Please... leave my
  men be..." chatsad3, with busy=1 and the player facing 2892,9680. No other var changed around
  it. The trigger is the 516 kill, after the "I could MAKE you tell me" exchange (from [2406]).
- **Sir Tiffy's "Can I buy some armour?"** (menu option 3 of 5, [176]/[195]) is in no script in
  our tree. The recorded flow: player chatneu1, Tiffy chatneu2 "Of course old bean. I can sell you
  up to Initiate level items only I'm afraid.", then the shop opens on that page's continue [197].
  Call `@initiate_temple_knight_armoury_open` from the menu.
- **Cromperty's essence-mine teleport** [4592]/[4735] goes into Wanted!'s cutscene at C+1 (fade,
  hidden ops, a copy at 2910,4830, cameras). Only the cast on C is generic.
