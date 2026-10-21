# The Gauntlet / Corrupted Gauntlet: recorded behaviour

What Jagex's server sends for the Gauntlet, read out of RSProx transcripts, and
what changed after the recordings (wiki, `sources/`). Our content is
`OSRS-Content/osrs-content/server/scripts/minigames/minigame_gauntlet/`.

## Sessions

Every recording is seasonal. None is a normal world.

| index | rev | activity | runs |
|---|---|---|---|
| 3268 | 234.1 | Grid Master | G [1463] complete (prep 7:57, kill 0:55); CG [2519] complete (6:47 / 2:10) |
| 3259 | 234.1 | Grid Master | G [9771] died at once; G [9818] died to the boss; G [11079] died to the boss |
| 1144 | 237.1 | Leagues VI | G [3299] complete; CG [4480] [5066] [5805] [8725] [9245] complete, [6347] [7053] [8253] died; from [9740] on, ~120 **Echo** Corrupted runs (`crystal_hunllef_crystals_echo_crystals`, prep skipped). These are not used for boss rules. |
| 4114 | 238.1 | Leagues VI | two entries and leaves ([68], [184]) |
| 1139, 3324, 4033, 4034, 4117 | | | lobby or Prifddinas only |

Runs: `python3 tools/rsprox/rsprox_gauntlet.py runs <transcripts>`. The other
analysis scripts are in `tools/rsprox/gauntlet/` (room reveals, template
rotation, floor patterns, the Hunllef timeline, npc sounds, tornadoes). They read
`~/.rsprox-index-cache/home-{default,raw}`. Instance positions come from
`rsprox_index.py get <id> --raw`.

Seasonal effects to ignore: "Untradeable drop" / "received a drop" lines
(notification settings), "Bottomless Brew", "You make an extra N x Paddlefish",
Flask of fervour / Sigil, `league_*` spotanims, and the Echo runs.

## After the recordings: the 22 July 2026 update (wiki, `sources/Update_Summer_Sweep-Up_Gear__26_PvM_Changes.wiki`)

The newest recording is rev 238 (May 2026). This update postdates all of them and
**overrides** them where they differ:

- A guaranteed random demi-boss in the north/east/south/west-most rooms from the
  centre: (3,6), (6,3), (3,0), (0,3). The species is still random.
- Demi-boss attack range 16 -> 10.
- Non-demi-boss enemies have standardised defensive stats (the wiki infoboxes now
  read def 4 for weak and def 18 for strong; HP 12 / 38).
- A weapon frame is guaranteed from the first weak ("low tier") kill.
- Phren bark, crystal ore and linum tirinum are stackable.
- Crystal shards go straight into the inventory, and enemy drops have a narrower range.
- Shard costs: T1 weapon 0 (frame only); T2 weapon 50; T1 armour 50 each; T2
  body 100, T2 helm/legs 50; T3 armour 100 each.
- Combat Achievements: Speed-chaser 4:45, Speed-runner 3:45, CG Speed-chaser
  7:05, CG Speed-runner 6:05.

Also from the wiki: 28 January 2026, the floor mural and tiles render in the
correct order (client only).

## 1. Lobby (index 1144 [3200..3300], 3268 [1448], 4114)

**Portal** `gauntlet_lobby_entrance` (36081, Prifddinas) at op tick O (L+1, face):

- O+1: `minimap_state` 2 + `minimap_toggle 2`, `busy` 1, `if_opensub prif_overlay` (641) in
  `overlay_atmosphere`, clientscript `prif_overlay_portal` (2921), synth 3952.
- O+2: teleport to (3032, 6125, 1).
- O+3: `minimap_state` 0 + `minimap_toggle 0`, clientscript `prif_remove_portal` (2922).
- O+4: `busy` 0, `if_closesub` the overlay, music 647.

The first click in 1144 [3200] got only the face. It is a Leagues relic quirk (unverified).

**Entrance** `gauntlet_entrance` (37340): op1 enters the Gauntlet, op2 enters
the Corrupted Gauntlet (139 op2 clicks in 1144). Before talking to Bryn
(1144 [3241]), op1 answers with a `chat_left` page, `chatang3`:
"Don't think you want to be heading down there without knowing what you're
getting into! Come and see me, if you really want to go down there."

**Bryn** (`gauntlet_instructor`, 9020) first talk, 1144 [3245..3275] (`rsprox_pages.py`):

| page | mood | text |
|---|---|---|
| npc | chatquiz1 | What are you doing down here? |
| menu | | What is this place? / Just passing by. |
| plr | chatquiz1 | What is this place? |
| npc | chatneu3 | This here is the Gauntlet, the finest creation of the Amlodd clan. It was built years ago to help train Prifddinas' finest warriors and survivalists. |
| plr | chatquiz1 | Train? |
| npc | chatneu3 | That's right. The Gauntlet contains a range of deadly creatures formed from crystal. Defeating them is the key to victory, but it is no easy task. |
| npc | chatneu2 | This is because all participants start with nothing at hand beyond a few basic tools. |
| plr | chatquiz1 | But how do people fight without their gear? |
| npc | chatneu3 | Resources can be gathered throughout the dungeon. These can be used to create the weapons, armour, food and potions needed to survive. |
| npc | chatneu3 | Without any armour, those crystalline creatures can be real nasty. Any armour that you make down there will greatly reduce the damage they do to you. |
| plr | chatcon2 | Okay, I think I follow... But I thought this was all about combat, not just gathering my own equipment. |
| npc | chatneu3 | That's right. Once you feel you're ready, or once we decide that you've spent long enough down there, you'll have to face the fearsome Crystalline Hunllef. |
| plr | chatquiz2 | And what makes it so dangerous, if it's just for training? |
| npc | chatneu3 | Once you go down there, there are only two ways out... You can make a break for the exit, or if you're bested in combat we'll send someone in for you. |
| mesbox | | The Gauntlet is `<col=7f0000>`not a safe environment`</col>`, and `<col=7f0000>`teleports are blocked inside`</col>`! If you die in there, any items stored at a gravestone `<col=7f0000>`will be lost`</col>`! |
| npc | chatneu2 | I think that covers it... Everything else, you'll have to work out for yourself while you're in there. |
| plr | chatneu1 | Okay... I think I've got it. |
| npc | chatneu2 | You can always come back and ask me to go over things again. |

Then the first entry (1144 [3281..3296]): a mesbox "The Gauntlet is a dangerous
environment and dying within will not be considered a safe death. Are you sure
you wish to enter?", menu "Are you sure you wish to enter?" Yes. / Yes, and
don't ask again. / No.; then a mesbox "You cannot keep any items whilst you are in
the Gauntlet. You may deposit your items in the nearby bank deposit box, or
store them here.", menu "Would you like to store your items here?" Yes. / Yes,
and don't ask again. / No.

**Completion hint** (3268 [2357], K+9 after the first completion): Bryn `chatneu2` "You
know, if you start getting comfortable down there, things can be a little
unstable at times..."

**Chest** `gauntlet_chest` (37341, 3268 [1448], [2503]):

- L+1: face.
- L+2: "You open the chest.", seq `human_openchest`, synth 2402.
- L+3: `gauntlet_reward_available` 0, "You find some treasure in the chest!", items added
  (noted), clientscript 7192 (loot tracker) per item, with npc `crystal_hunllef_death`.

## 2. Entry (4114 [65..71], 3268 [1463], 3268 [2519])

The op answers on its own tick E (already in reach), with no walk:

- E: `[facing] coord=` the entrance; the fade out (`fade_overlay` 174 in
  `overlay_atmosphere`, clientscript 948 (0,255,0,0,50), `hpbar_hud:hp` hidden,
  `cc_deleteall hpbar_hud:container`).
- E+1: `busy` 1.
- E+2: inventory and worn cleared (held), `holding_inventory_location` 1,
  `player_in_gauntlet` 1, `com_stance`/`combat_weapon_category` reset.
- E+3: `gauntlet_corrupted` (CG), `gauntlet_start` = the start room, the start and
  boss `gauntlet_room_N_found` 1, `gauntlet_current_room_x/z` = the start room,
  music 2749 (`music_current_track`), "You enter the Gauntlet.", teleport into
  the start room (`[facing] type=reset`). On the same tick the Hunllef (`crystal_hunllef_melee[_hm]`) is
  added in the boss room facing the arena centre, and the boss room's lit door
  arches (`prif_gauntlet_wall_arch_blue_lit` / `_red_lit_hm`, `door_wall_lit_01/02`) are added,
  along with the kit: inv axe, pickaxe, harpoon, pestle, teleport crystal; worn sceptre.
  `set_player_op 2 null`.
- E+4: `multiway_indicator` 1, `busy` 0. The Hunllef plays seq `hunllef_spawn`
  delay 120, `level_change`, `enabled_ops`, and transforms to its random
  protection form with headicon 440 (melee 0 / ranged 1 / magic 2). Fade back in
  (948 0,0,0,255,50).
- E+6: fade closed, `gauntlet_overlay` (637) in `overlay_hud`, clientscript
  `gauntlet_timer_update` (2914) (ticks_left, 0). It is sent **every tick**, counting
  1000 (G) / 750 (CG) down.

During prep the Hunllef wanders in its room (walk steps, 4114 [72..87]).

## 3. The room grid (214 room reveals, 13 full runs)

- 7x7 rooms of 16x16 tiles, room index = z*7 + x, with z increasing northward
  (raw instance coords). Boss = 24 (3,3). The start is one of 17/23/25/31 (S/W/E/N
  of the boss), random (13 runs: 17 x5, 23 x3, 25 x2, 31 x3). `gauntlet_current_room_x/z` is
  written **once**, on entry, and never again (3268 [1463..2261]).
- Every room connects to all its neighbours. Doors are on every inner edge, so
  there is no maze.
- Rooms come from the template's 4x4 library (`m29_88` G, `m30_88` CG): column 0 =
  4 doors (inner rooms), 1 = 3 doors (edges), 2 = 2 doors (corners), row = one of
  4 visual variants, rotated so the missing doors face outward. Column 3: (3,2) is
  the start room, (3,3) the boss room. Both are rotated so the start room's
  door and the boss room's barrier face each other (the template has start south
  of boss = start 31, rotation 0).
- **Door op** (`prif_gauntlet_door_wall_unlit_01/02[_hm]`, op1), 3268 [1474..1476]: at L+1,
  `face_angle` to the door, "You light the nodes in the corridor to help guide the
  way." (spam), `gauntlet_room_N_found` 1 for the room behind it, the new room's
  npcs added (random `spawnangle`) and its resource locs added, both door pairs
  of the corridor and their arches changed to `_lit`, then synth 2596. The arch
  pairs are 1-2 tiles beside the doors. Without a sceptre the answer is the mesbox
  "You need something to light the nodes with." (2 recordings).

### Room contents (pre-July generation, from the 214 reveals)

A room's monster tier depends on its ring around the boss (Chebyshev distance):

| ring | none | weak | strong | demi |
|---|---|---|---|---|
| 1 (n=45) | 20 | 25 | 0 | 0 |
| 2 (n=75) | 43 | 20 | 12 | 0 |
| 3 (n=94) | 41 | 2 | 21 | 30 |

- Weak: 1 (12), 2 (8) or 3 (27) of rat/spider/bat, each species independently random.
- Strong: always 2 of unicorn/scorpion/wolf, independently random.
- Demi: 1 of bear (5) / dragon (12) / dark beast (13).
- No room is empty: every room without monsters has resources.

Resources by tier:

| tier | 0 | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|
| none (n=104) | | | | 65 (all one type) | 25 (a triplet + 1) | 14 (random) |
| weak (n=47) | 14 | 8 | 5 | | | 20 (random) |
| strong (n=33) | 9 | 18 | 6 | | | |
| demi (n=30) | 11 | 13 | 6 | | | |

The types are uniform over rock/tree/fibre/herb/pond (counts 109/108/135/97/89).

- Each template room has **5 resource spots** (template-local south-west of a
  2x2 footprint; a 1x1 node sits on the rotated block's south-west tile). Loc
  rotation is random 0-3 (pond always 0).

  | cell | spots |
  |---|---|
  | (0,0) | (2,2) (2,12) (7,5) (12,2) (12,12) |
  | (0,1) | (2,2) (2,12) (8,11) (12,2) (12,12) |
  | (0,2) | (2,2) (2,12) (10,6) (12,2) (12,12) |
  | (0,3) | (2,12) (4,12) (9,5) (12,10) (12,12) |
  | (1,0) | (2,2) (3,10) (11,5) (11,10) (12,2) |
  | (1,1) | (2,12) (3,8) (9,11) (10,3) (11,6) |
  | (1,2) | (5,5) (5,7) (5,9) (10,3) (10,11) |
  | (1,3) | (2,12) (4,8) (7,3) (10,8) (12,12) |
  | (2,0) | (2,3) (2,12) (3,6) (9,2) (12,2) |
  | (2,1) | (2,5) (6,3) (11,12) (12,2) + 1 unseen |
  | (2,2) | (2,12) (4,2) + 3 unseen |
  | (2,3) | (2,2) (6,3) (11,9) + 2 unseen |

- Npc spawn tiles are not fixed. Weak and strong monsters land on scattered
  tiles; demi-bosses near the room centre (south-west at local (4..8, 5..8), 30 spawns).
- The wiki's normal-mode guarantee (inner 3x3: four rooms with at least 3 deposits,
  3 linum, 3 roots, and weak monsters) and the CG one (one inner room with 3 fishing
  spots) are consistent with the "triplet" rooms above.

## 4. Gathering (3268, 3259, 1144: 261 yields)

| node | start (L+2) | yield | last | yields |
|---|---|---|---|---|
| rock | "You swing your pick at the rock." + seq `human_mining_crystal_pickaxe` | "You manage to mine some ore." + synth 3600 | gamemessage "You mine the last of the salvageable ore from the rock.", seq -1, loc -> `gauntlet_rock_depleted` | 3 |
| tree | "You swing your axe at the roots." + seq `human_woodcutting_crystal_axe` | "You get some bark." + `sound_area` 2735 range 1 at the player | "You chop the last of the salvageable bark from the roots.", seq -1, depleted, synth 2734 | 3 |
| pond | "You start harpooning fish." + seq `human_harpoon_crystal` | "You manage to catch a fish." | "You catch the last of the fish.", depleted | 4 |
| fibre | seq `picking_low` | "You pick some fibre from the plant." + synth 2581 + seq | (depleted after 3) | 3 |
| herb | seq `picking_low` | "You pick a herb from the roots." + synth 2581, seq -1, depleted | | 1 |

- The first yield is at L+3 and each later one 3 ticks after the last: rock 34/34,
  tree 36/36, pond 52/52.
- A shard find "You find N crystal shards." / "...corrupted shards." comes with
  ~1/3 of yields (93/261), N uniform 10-30. It never comes from herbs (0/7).
- "You can't carry any more fish." (mesbox) stops the harpoon.

## 5. Monsters

Stats come from the wiki infoboxes (current, post-July). Anims and sounds come from
the recordings. The sounds are synths to the player and are the same for both modes:

| npc | attack seq | death seq | attack | defend | death |
|---|---|---|---|---|---|
| rat | giant_rat_update_attack | giant_rat_update_death_fast | 710 | 713 | 711 |
| spider | sote_gauntlet_spider_attack_melee | sote_gauntlet_spider_death | 537 | 539 | 538 |
| bat | bat_rework_attack | bat_rework_death | 292 | 294 | 293 |
| unicorn | unicorn_rework_attack | unicorn_rework_death | 369 | 371 | 370 |
| scorpion | scorpion_update_attack_tail | scorpion_update_death | 3611 | 3612 | 3610 |
| wolf | dog_update_wolf_attack | dog_update_wolf_death_quick | 909 | 912 | 911 |
| bear | bear_rework_attack | bear_rework_death | 300 | 302 | 301 |
| dragon | dragon_firebreath_all_attack | dragon_death | 408 d10 + 222 d30 (+223 at impact) | 410 | 409 |
| dark beast | dark_beast_update_attack | dark_beast_update_death | 389 + 1965 d22 | 391 | 390 |

- **No Gauntlet npc plays a defend animation** (0 of 600+ hits across three
  sessions). Ours had one for every type.
- Projectiles (v2: heights / 4):
  - dragon `crystal_dragon_magic_travel` start 51, angle 12, progress 128, height 58->104, impact spot `crystal_dragon_magic_impact` height 104 delay = endtime;
  - dark beast `crystal_dark_beast_range_travel` start 36, angle 8, progress 128, height 76->94.
- Attack every 4 ticks (wiki, and consistent in the recordings).

## 6. Boss room entry (3268 [2261])

Barrier `gauntlet_blockade` op2 (Quick-pass) on tick B:

- B: `gauntlet_boss_started` 1, `hpbar_hud_npc`/`hp`/`basehp`/`boss` set
  (600 G / 1000 CG), the player moves 2 tiles through (5695 -> 5693), the boss room arches/doors lit,
  clientscript `gauntlet_timer_end` (2916), `hp_hud_open`.
- B+1: the Hunllef faces the player.
- B+3: the first attack.

## 7. The Hunllef (14 standard fights: G 3268@2261, 1144@3947, 3259@9780/10635/12082; CG 3268@3200, 1144@4918/5557/6185/6732/7378/8598/9101/9589)

- **Cadence:** one action every 5 ticks, the first at B+3 (13/13 fights).
- **Styles:** Ranged first (14/14). After every 4th standard attack (the tornado special
  counts, the stomp does not), seq `hunllef_attack_transition_magic` /
  `_transition_ranged` plays **2 ticks after** that attack, with synth 6994. Both
  styles fire seq `hunllef_attack_ranged`.
- **Prayer disable:** 0 or 1 per magic phase (30 phases with one, 9 with none),
  any of the 4 positions (1: 3, 2: 11, 3: 7, 4: 9). The projectile is
  `crystal_hunllef_prayer_travel`, the impact spot `crystal_hunllef_prayer_impact`; at impact the
  prayers go off and "`<col=ef1020>`Your prayers have been disabled!`</col>`" is sent.
- **Stomp:** a player under the Hunllef when it acts: seq `hunllef_attack_melee`,
  "You're trampled beneath the Hunllef." (6/6), synth 4180 + 3752 d60. It does not
  advance the 4-count.
- **Tornadoes:** seq `hunllef_attack_special`, synth 4180 + 3752 d60 + 975 d65.
  `crystal_hunllef_crystals[_hm]` are added **3 ticks later** on random arena tiles, not
  the player's (Chebyshev 1-11). Each moves 1 tile per tick toward the player (glide)
  from the tick after it is added, and is removed 20-21 ticks after spawn.
  - Count: G 1/2/3 at HP above 2/3, 1/3-2/3, below 1/3; CG 2/3/4.
  - A special comes every 9-14 attacks (first at attack index 5-10).
  - Its hit is spot `crystal_hunllef_crystals_hit` on the player + `sound_area` 4114 range 16.
- **Projectiles** (v2), all start 19, angle 18, progress 0, height 214->124
  (ours: 53 -> 31), endtime = 49 + 5 x Chebyshev(source, target)
  (n=73 ranged, 51 magic, 14 prayer: d1 54, d2 59, d3 64, d4 69, d5 74). Source = the Hunllef's
  centre tile.
- **Sounds:**
  - ranged: synth 4161 d2 + 1965 d6, no impact spot.
  - magic: synth 4154 d2 + 222 d6, spot `crystal_hunllef_magic_impact` height 124 at endtime.
  - prayer: 4154 d2 + 134 d6.
  - boss defend: synth 4166.
  - death part a: synth 4167.
- **Protection switch:** transform to `crystal_hunllef_{melee,ranged,magic}[_hm]`
  with `sound_area` 2675 (magic) / 2677 (ranged) range 16 (melee: 2676, unseen).
  The rule is the wiki's: every 6th off-prayer player attack.
- **Floor:** the 12x12 arena. Each pattern is a warning (`prif_gauntlet_floor_tile_01_warning`) then
  hit (`..._hit`) then back to `prif_gauntlet_floor_tile_01`, with `sound_area` 1315 range 16 on warning
  and on hit. The first warning comes at B+7. The next warning is on the same tick the previous
  pattern clears.

  | phase (HP) | G warn/hit | CG warn/hit |
  |---|---|---|
  | 1 (above 2/3) | 8 / 16 (first warn 9) | 7 / 14 (first 8) |
  | 2 (1/3-2/3) | 6 / 12 | 5 / 10 |
  | 3 (below 1/3) | 4 / 8 | 3 / 6 |

  The durations follow the current phase, so a hit that started in phase 1 ends at the
  phase 2 length. Patterns (template frame, arena-local (x,z,w,h); `tools/rsprox/gauntlet/floor.py`, 19
  distinct, no back-to-back repeat in 140 transitions):
  - phase 1: 6x6 at (0,0) (0,6) (2,2) (2,4) (4,2) (4,4) (6,0) (6,6);
  - phase 2: those + 4x12 bands W/E, 12x4 bands S/N, paired 3x12 W+E, paired 12x3 S+N;
  - phase 3: 6x6 at (3,3); 3x3 corners + 4x4 centre; 4x4 at the corners; 4x4 at (1,1) (7,1) (1,7) (7,7); the 2-deep ring.
- **Death:** seq `hunllef_death_part_a` (synth 4167) on the killing tick K, `_part_b` +
  transform to `crystal_hunllef_death[_hm]` at K+3.

## 8. Completion, death, leave

**Completion** (3268 [2348..2357]):

- K+4: "Challenge duration: `<col=ff0000>`M:SS`</col>` (new personal best)." or "...
  Personal best: M:SS." (CG: "Corrupted challenge duration: ..."), "Preparation time:
  `<col=ff0000>`M:SS`</col>`. Hunllef kill time: `<col=ff0000>`M:SS`</col>`.", "Your Gauntlet
  completion count is: `<col=ff0000>`N`</col>`." (CG: "Your Corrupted Gauntlet completion count is: ...").
  `total_completed_gauntlet[_hm]` +1, `hpbar_hud_npc` -1, `gauntlet_overlay` closed, the fade
  out, music 147, jingle 276.
- K+7: `player_in_gauntlet` 0, `holding_inventory_location` 0, "Your reward awaits you in
  the nearby chest.", `gauntlet_boss_started` 0, teleport to (3032, 6127, 1), the held items
  restored.
- K+8: fade in, music 655.

**Death** (3259 [10967]): `gauntlet_reward_available` 1, "Oh dear, you are dead!",
`player_in_gauntlet` 0, `holding_inventory_location` 0, "Your reward awaits you in the
nearby chest.", teleport to (3032, 6127, 1), the held items restored, `gauntlet_overlay`
closed, `minimap_toggle 0`. There is no fade.

**Leave** (4114 [168..172]): mesbox "Are you sure you wish to exit the Gauntlet? All of
your progress will be lost and you will start again upon re-entering.", then "You leave
the Gauntlet."

## 9. Npc info (`tools/rsprox/rsprox_npcinfo.py --match 'crystal_|gauntlet_instructor'`, 3268 + 3259 + 1144)

| npc | lives | movement | death seq -> `[del]` | other blocks |
|---|---|---|---|---|
| rat | 80 | walk, 1 tile | 3 | headbar 0; `enabled_ops 0b00000` at death (29/29); random `face_angle` on add |
| spider | 38 | walk 1 | 2 | as rat (17/17) |
| bat | 41 | walk 1 | 2 | as rat (16/16) |
| unicorn | 46 | walk 1 | 3 | (6/6) |
| scorpion | 65 | walk 1 | 3 | (4/4) |
| wolf | 43 | walk 1 | 3 | (4/4) |
| bear | 5 | walk 1 | 4 | (4/4) |
| dragon | 13 | walk 1 | 6 | (11/11) |
| dark beast | 13 | walk 1, mostly still (20 walk / 214 idle) | 6 | (13/13) |
| Hunllef | 14 standard | wanders in prep; in the fight 29 single steps in 8 fights | (removed with the instance) | headbar **20** (2954/2954); `enabled_ops 0b11111` + `level_change` with each transform; headicon 440 frame = form |
| tornado | 531 | `teleport` (glide) 1 tile a tick (1066/1066 standard) | - | none |
| Bryn | 137 | wanders the lobby | - | faces the player on talk |

No Gauntlet npc plays a defend seq (0 rows).

## 10. Implementation audit (ours vs recording)

Ours: `test/minigames/gauntlet_tells.lua` and `gauntlet_cg_tells.lua`
(`tools/quest_gate/run.py --script ...`), with the tick log at
`build/quest_gate/<name>/ticklog.tsv`. B = the barrier tick, K = the kill.

| section | rule (recorded) | ours | verdict |
|---|---|---|---|
| entry | E+3 music 650, teleport, Hunllef added; E+4 spawn seq delay 120 + random form | 13 / 13 / 14 (E = 10) | matched |
| entry | start/boss corridor lit at E+3 | E+4 (the instance's locs exist on our side only once the scene is built) | **one tick late, open** |
| door | L+1 line, found varbit, contents, both corridor halves lit, synth 2596 | the same tick, 8 locs + 3 ponds | matched |
| gathering | L+2 start; yields L+3 then every 3; spent: seq -1 + depleted | pond: 36 seq, yields 37/40/43/46, depleted 46 | matched |
| barrier | lights all four boss sides; B+3 first action | 16 locs at B; B+3 (G and CG) | matched |
| Hunllef cadence | every 5; transition +2 after the 4th | 39/39 at 5; 9/9 at +2 (G); same CG | matched |
| special | first at attack 5-10, then 8-13; tornadoes +3 | 9, then 9/11/9; +3 (3/3 G, 3/3 CG) | matched |
| tornado count | G 1/2/3, CG 2/3/4 | G 1/2/3; CG 2/3/4 | matched |
| tornado hit | crystals_hit + area 4114 r16, every tick on the tile | the same | matched |
| floor | first warn B+7; G 9 then 8/16, 6/12, 4/8; CG 8 then 7/14, 5/10, 3/6 | exactly these, both modes | matched |
| floor sound | area 1315 r16 at boss-local (8,8), on warn and hit | the same | matched |
| projectile | start 19, angle 18, endtime 49 + 5/tile; synths per style | d6: 19 -> 79; 4161 d2 + 1965 d6 ranged; 4154 d2 + 222 d6 magic; 4154 + 134 prayer | matched |
| hit landing | tick (endtime + 15) / 30 | the same (queue delay n answers n+1) | matched |
| death | part_a on K; part_b + death form K+3; lines and jingle K+4; lobby K+7; fade back + music 655 K+8 | part_a K+1 (the engine's death seq), rest K+3/K+4/K+7/K+8 | **part_a one tick late (engine), open** |
| chest | L+2 line + openchest + 2402; L+3 line + items | the same | matched |
| CG death | player dies, items back, lobby | a two-tornado death in the CG test landed in the lobby | matched (shape) |

### Not exercised by a scriptrun yet (configured from the recordings, unverified on our wire)

- The monsters' fights (attack/death anims and sounds, the dragon and dark beast projectiles). The configs carry the recorded values; no test fights one.
- The stomp, the protection switch on 6 off-prayer hits (the tests never attack), and the prayer-disable line (our tick log has no message rows).
- The lobby portal, Bryn's talk, the entrance prompts, the leave and the teleport crystal.
- The singing bowl's make menu (mode 22, free-slot ceiling) and the range/pump/potion lines.

### Open gaps

- The start/boss corridor lights one tick late, and the death seq plays one tick late (both above).
- Corner variants (2,1)/(2,2)/(2,3) offer only the spots seen (4/2/3 of 5).
- Monster spawn tiles are random walkable tiles (weak/strong) or near the centre (demi). The recordings show no fixed set.
- The Hunllef stands still in the fight; the recordings show 29 single steps in 8 fights, rule unknown.
- Not recorded, so implemented from the wiki or minimal and labelled unverified in the scripts: the tool storage's lines past "You take a sceptre.", the barrier's op1 (Pass, treated as Quick-pass), the escape crystal, the floor damage on the normal Hunllef (the Corrupted page's 10-20 used), the stomp damage, the singing XP.
- The loot-tracker clientscript 7192 on the chest is not sent (nothing in the tree sends it).
