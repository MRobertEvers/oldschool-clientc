# Horror from the Deep from RSProx

Recording: **index 4474**, rev 242.1, world 377, 2026-11-06, recorder LoveTori (combat 48,
Agility 35), "Horror from the Deep": the whole quest, start to the Unholy Book, 2,374 ticks.
No other recording reaches the quest (index search on Larrissa 4425/4426, Jossik 4423/4424,
the Dagannoths 976..988: 4474 [86 rows]; seven Leagues/normal sessions only pass the npcs).

Transcribing: the stock transcriber has no osrs-242 decoder and rsprox-c HEAD (5959be87)
aborted on RSProt's ToriRS-only `CACHE_INVALIDATE` prot. The transcripts here were made with an
rsprox-c worktree that skips that prot in `DecoderLoader.validateProtNames` (since fixed in the
rsprox-c checkout by 3draster-fe, uncommitted). `update_stat_v2`'s `experience=` in the
transcript is the transcriber's DELTA of whole xp, not the total.

Our side before this pass: `quest_horror/` was a LostCity (2004) port (§16): the start had no
combat warning and no "Start the Horror from the Deep quest?" menu, stage 3 was never written,
the three books were LostCity's text on LostCity's page breaks (14/14/11 spreads, Jagex's are
8/11/10), the basalt jumps and the bridge had LostCity's timing and an extra tile of travel, the
completion scroll had invented rows, and the mother dropped bones the recording never shows.

## Stages and vars (varbit horrorquest 34 on varp deephorror 351, transmitted)

| var | value | written by | 4474 [tick] | ours |
|---|---|---|---|---|
| horrorquest | 1 | the start menu's "Yes.", with "Okay, I'll help!" | [873] | fixed (was the "Okay, I'll help!" option) |
| horrorbridgeleft 36 | 1 | plank on the left spot | [1008] | matched |
| horrorbridgeright 37 | 1 | plank on the right spot | [1019] | matched |
| horroragilitykey 38 | 1 | Gunnjorn's last page, with the key | [1117] | matched |
| horrorquest | 2 | Larrissa's last key page, on its click; the key is KEPT | [1218] | matched (mood fixed) |
| horrorquest | 3 | the inner Larrissa's first page | [1245] | fixed (never written before) |
| horrortar 46 / horrorlight 48 / horrorglass 47 | 1 | tar, tinderbox, glass on the cog | [1481] [1486] [1492] | matched |
| horrorquest | 4 | the glass, with the teleport to the real top floor | [1492] | matched |
| horrorsword 44 .. horrorfire 40 | 1 | each offering's "Yes" | [1568..1612] | matched |
| horrordoor 35 | 1 | with the sixth offering | [1612] | fixed (never written) |
| horrorquest | 5 | the juvenile's [del] tick, death seq +2, with the first page | [1865] | matched (death seq +2 on our tick log) |
| horrorquest | 10 | the mother's [del] tick, death seq +2, with the scroll | [2199] | matched (death seq +2) |
| horrorlighthouseentrance 39 | -- | never written in the recording | | LostCity's key-on-door path kept, unverified |

`last_quest_id`, `last_started_quest_id`, `last_completed_quest_*` (16741, 16760, 16767..) and
`quest_rating_current_quest` go out with every stage write: engine-generic quest tracking we
never send (varp_transmit_audit), wider than this quest.

## Action inventory (rsprox_actions.py), a verdict per row

| first tick | action | verdict |
|---|---|---|
| 4..24 | welcome screen, quest list, overview close | journal section below |
| 126, 351 | glory Rub (worn slot2 op2) -> 3087,3496 | generic jewellery (amulet_of_glory.rs2), not re-audited here |
| 131, 331, 416, 417 | bank booth / BA bank chest / GE banker / GE bank wall | generic banking |
| 136..190, 510..670 | bank and GE item ops, equips (xeric's, glory, staves) | generic bank/GE/equip |
| 214, 397 | varrock_sc_tunnel_west (3 one-tile hops) | generic agility shortcut (agility_shortcuts_osrs.rs2) |
| 228 | Nigel (deadman_nigel_varrock 16812) op4 -> omnishop | **unbound**: the Deadman omnishop (shop data 929) has no system in our tree; Open |
| 241..614 | GE clerk, offers, collect (nails, xeric's, runes) | generic GE |
| 278, 683, 694, 1050 | games necklace submenu (Barbarian Outpost x3, Burthorpe x1) | generic jewellery (§2b) |
| 283 | willow tree | generic woodcutting |
| 293..1197 | basalt rocks 1..9 | **this content**: Basalt rocks |
| 357..380 | skill guide | generic |
| 786..1239 | lighthouse doorway x5 | **this content**: the doors |
| 796, 1211, 1220, 2267 | Larrissa outside | **this content**: Larrissa |
| 980..1037 | broken bridge spots, planks | **this content**: the bridge |
| 1004 | lobster | generic eating |
| 1062 | stamina potion | generic drinking |
| 1067, 1137 | Barbarian Outpost gates | generic double gate |
| 1076, 1129 | the agility pipe | generic agility shortcut |
| 1089 | Gunnjorn | **this content**: Gunnjorn |
| 1136 | fai_barbarian_1 op2 | a stray Attack click, no answer before the walk-away |
| 1245, 1528 | Larrissa inside | **this content**: Larrissa |
| 1261..2363 | lighthouse spiral stairs, all ops | **this content**: the stairs |
| 1267..1509 | bookcase; the three books read | **this content**: the books |
| 1381, 1572, 1582 | bronze arrows wield/unwield | recorder's kit |
| 1420, 1431 | drawers (locked; open; search) | generic drawers, fixed (drawer_open synth; the locked one) |
| 1443..1492 | the broken cog: examine, tar, tinderbox, glass | **this content**: the light |
| 1540 | crate2 search | generic ("You search the crate but find nothing.") |
| 1544..1650 | horror ladders (top, top2, base2) | **this content**: ladders |
| 1552..1612 | the strange wall, its interface, six offerings | **this content**: the wall |
| 1616..1640 | far left/right doors | **this content**: the doors |
| 1659..1890 | Jossik injured | **this content**: Jossik |
| 1719..2155 | Dagannoth fights, prayers, autocast, restores | **this content** (fights) + recorder's kit (spells, prayers) |
| 2212, 2235 | quest scroll close; quest_rating close | completion; quest_rating is rev 242 only (Open) |
| 2244 | casket used on itself | unanswered in the recording |
| 2249 | horror_ladder_base (cave exit) | **this content**: ladders, landing 2510,3644 matched |
| 2287 | Jossik (well) | **this content**: the casket |
| 2374 | logout | generic |

## Teleport inventory

| tick | from -> to | cause | verdict |
|---|---|---|---|
| 129, 354 | -> 3087,3496 | glory Rub | generic |
| 220..222, 403..405 | 3137,3516 -> 3142,3513 | Varrock tunnel shortcut | generic |
| 281, 697, 1053 | -> 2518..2521,3570..3571 | games necklace, Barbarian Outpost | generic (§2b) |
| 686 | -> 2899,3551 | games necklace, Burthorpe | generic |
| 713..776, 1163..1197 | two tiles per rock | basalt jumps | rebuilt (Basalt rocks) |
| 723 | 2522,3600 -> 2521,3600 | the slip | rebuilt |
| 982..1014 | 2596 <-> 2598 | bridge jumps/dives | rebuilt (the bridge) |
| 1079, 1132 | 2552,3561 <-> 2552,3558 | the pipe | generic |
| 1241 | 2509,3635 -> 2445,4595 | the door, into the lighthouse copy | matched |
| 1263, 1439 | -> 2441,4601,1 / ,2 | copy stairs | matched |
| 1492 | 2444,4603,2 -> 2508,3643,2 | the glass, to the real top floor | matched |
| 1502..1527 | copy <-> real lighthouse | the stairs at stage 4 | matched; Top-floor/Bottom-floor fixed |
| 1548 | -> 2518,4618 | horror_ladder_top | matched |
| 1644..1650 | 2515,4629 <-> 4632 | the cavern deck ladders | matched |
| 2199 | 2528,4647 -> 2515,10001 | completion | matched |
| 2252 | 2519,9995 -> 2510,3644 | cave ladder out | matched |
| 2279, 2363 | real lighthouse stairs | matched |

## Basalt rocks (basalt_rocks.rs2)

| | R (4474) | ours before | now |
|---|---|---|---|
| step onto the rock | the tick after the op's face, 9/10 | arrivedelay-shaped, a 1-tick pause | matched |
| the jump | step+2: seq 769 delay 19, exactmove 47..59, teleport 2 tiles | then a forced extra tile | matched |
| landing | step+4: synth `jump` (2461) | synth `jump_no_land` at the start | matched |
| xp | 2.0 for spots 3, 5, 7 (6/6); 0 for 1, 9 (4/4) | 2.0 for 3, 4, 7, 8 | 3..8 (4, 6, 8 unrecorded) |
| slip | [721..731]: see the file header | wrong ticks, an extra tile, `jump_no_land` | matched |
| slip damage | 3 (one roll) | 1..10 | 1..10 kept (unverified range) |

## The bridge (horror_interactions.rs2)

| | R | now |
|---|---|---|
| unfixed fail | dive 1115 delay 20, exactmove 30..49; synth jump +1; hit 2..5 +3; left on the far spot ([980..985], [993..998], [999..1002]) | matched |
| unfixed success | jump 769 delay 30, exactmove 58..70; step off + synth jump +2; xp +4 ([987..991], [1014..1018]) | matched |
| both planked | step, face +2, sidestep +3 with balancing_ledge x3 delay 10, a tile per 2 ticks, outof_sidestep (west only) +7, off +8 ([1027..1035], [1038..1046]) | matched; stances from the --bas transcript |
| xp | the 7 deltas fit 0.67..0.86 per crossing | 0.7 |
| plank | human_smithing + mesbox on the op, no sound | `anvil_4` removed |

## Pages (rsprox_pages.py, 157 rows)

Every recorded page's text and mood is in our scripts. Fixed: the start menu (was "Okay, I'll
help!" / "Sorry, just passing through"), the combat-level warning, the bridge page's "a hammer,
and thirty steel nails for each plank you use as well", "Quickly, we must go inside..." (sad),
Gunnjorn's greeting (forced breaks removed), "Climb up." / "Climb down.", Jossik's "I can
indeed! ...|Yes! There is something written on it!" as one page, "It is very faint, however...",
the casket's fourth option and its three pages, "Wow! It's ..." (happy), and Jossik's three
closing pages, which play in the Dagannoth cave with his injured chathead after he is gone
(ours skipped them). A re-talk to Jossik while a Dagannoth is up replays the same pages and
spawns nothing ([1829..1843], [1887..1890]); ours said "Look out! The creature is still here!".

Unrecorded and kept as LostCity/wiki (unverified): "No." at the start, every Larrissa re-talk
at stages 1, 3, 5, the copy-exit door, the lost-key arm, Saradomin and Guthix reveals.

## The books (horror_diary.rs2)

All three were read to the last spread: Manual 8, Diary 11, Journal 10 spreads ([1279..1415]).
Every row is now the recorded row (`|` per row, `~book_read_spread`), generated from the
transcript; LostCity's text and breaks are gone.

## The light, the wall, the doors

Matched as they were: the tar/tinderbox/glass lines and order, the landing on the real top
floor, the wall interface's six models, every offering's two pages and line. Fixed: varbit
horrordoor with the last offering; "This door cannot be opened from this side." (was "The
door"); the leaves of every door (front, far left, far right) for 2 ticks, not 3, and
strangedoor_close 2 ticks after the open. The copy entry also plays `clock_tick_1` x3
([1242], one occurrence, copied). `tol_homonculus_crying` (3424) at [1262] beside the first
stair climb has no cause in the recording: not built (Open).

## The journal

Not started ([732]): the overview, start line "talking to Larrissa outside the Lighthouse west
of Rellekka." (and Zogre Flesh Eaters' at [1558], same recording); both added to
`quest_overview_start.rs2`. Stage 2 ([1227]): the three rows, copied with Jagex's stray `<str>`.
Every other stage is LostCity's text: UNVERIFIED.

## The fights

The juvenile (120 hp, 1 hp per 20 ticks: 127 damage over 140 ticks, 7 healed), 35 swings every
4 ticks, all 0 under Protect from Melee; arrival [1708..1717] rebuilt tick for tick (the rise
tile, the forms every 2 ticks, the camera, Jossik's turn, the jump to 2518,4637). The mother's
arrival [1871..1881] rebuilt likewise (2506,4642, the area chitters, the jump to 2523,4646),
her colour on a 30-tick clock from the Air form (12/12 changes). Completion [2199]: no drop, no
casket line, the scroll "You have survived the Horror from the Deep!" with "2 Quest Points",
"4,662 Strength XP", "4,662 Ranged XP", "4,662 Magic XP", "A Damaged Book".

The mother's attacks, damage and prayer rules are the boss audit's (below).

## Npc movement

`npc_movement_audit.py defaults quests/quest_horror`: all 20 types wandered on the generated
default. Recorded: injured Jossik never moves (27 idle ticks, no step) and ours walked off his
tile, so `npc_findexact` missed him and his post-juvenile pages were skipped; the 13 Dagannoth
forms move only by script. Both now `defaultmode=none, wanderrange=0`. Larrissa (both),
well Jossik and Gunnjorn walk in the recording: left wandering. The arrivals' steps are over
water, which `npc_walk` will not route: one-tile `npc_tele` glides on the recorded ticks (the
wire calls them teleport steps where Jagex's are walks; same tile, same tick).

## Wire audit (scriptrun, build/quest_gate/horror/ticklog.tsv, 132/132 PASS)

| section | R (4474) | ours | verdict |
|---|---|---|---|
| jump | step S; seq 769 d19 S+2; synth 2461 S+4 | same | matched |
| slip | fall 770 d10 F; synth jump F+1; splash 68 d3 + pool_plop F+2; seq -1 F+7; hit F+8 | same | matched |
| bridge dive | step S; 1115 d20 S+1; synth S+2; hit S+4 | same | matched |
| bridge walk | step S; face S+2; first sidestep + 2451 x3 d10 S+3; S+5; 758 d20 S+7; off S+8 | same | matched |
| copy entry | teleport L+2; leaves + door_open + clock_tick_1 x3 L+3; back +2 | same ([761], [763]) | fixed (the copy door was never swapped: the active loc was the real door) |
| far doors | leaves + strangedoor_open O; back + strangedoor_close O+2 | same | fixed (close was O+3) |
| juvenile | rise = last click +1; forms +2/+4/+6; jump +8; camera reset +9 | same | fixed (rose on the click) |
| mother | camera = last click +1; rise +2; forms every 2; jump; camera reset | same | fixed (rose mid-dialogue and closed it) |
| stage 5 / 10 | death seq +2 | death seq +2 | matched |

The test (test/quests/horror.lua) now follows the recording: the start menu's "Yes.", the key
handed to Larrissa before one door click, the inner Larrissa (stage 3), the bookcase, "Climb
up.", the scroll before Jossik's closing pages and the level-up boxes, the cave ladder out, and
Jossik's casket with the fourth option once. Setup adds Barcrawl (the quest's prerequisite and
the Outpost guard's).

## Open

- Nigel's Deadman omnishop ([228]) is unbound; no omnishop system.
- `quest_rating` (983, [2232]) after the closing pages: a rev-242 interface, absent from our
  rev-241 cache.
- The generic quest-tracking varbits (16741, 16760, 16767, ...) are never sent.
- `tol_homonculus_crying` at [1262]: no cause found.
- The reverse basalt spots' xp, spot 8's slip tiles, the bridge xp (0.7 of a fitted range).
- The door at stage 1 with the key (LostCity's unlock arm) is unrecorded.

## The Dagannoth mother: combat audit

Recording: index 4474 [1872..2199], one fight, one roll. The recorder (combat 48, Defence 31,
Hitpoints 29, Magic 26..27; Xerician robes, glory, staff of earth, elemental shield) autocast
the four strike spells, prayed Protect from Melee to [1884], Protect from Missiles after it,
and none [2150..2155]; from [1916] they stood on 2528,4647, two tiles south of her footprint,
where she cannot close in. Ours: `test/quests/horror.lua` (bot at 2517,4633 with Magic 60 and
the four blasts, Protect from Missiles, eight ticks without it once three casts have landed),
tick logs before (tick 1075 = [1872]) and after the fixes (`killMother.*`, 134/134). Ids are
named in our rev-241 cache; they are the recording's names.

### Her record (cache, all nine forms) and the fight's stream

| | R | ours | verdict |
|---|---|---|---|
| levels | cache stat1..6: attack 78, defence 81, strength 78, hitpoints 120, ranged 50, magic default 1 | same | matched |
| defences | cache: stab/slash/crush 150, magic 50; this cache's npc records carry no ranged defence (38 of 3,252 state one) | stab/slash/crush 150, magic 50, ranged 50 | matched; ranged 50 unverifiable here (Open) |
| strength bonus | cache states none (2,023 records state one) | 9 (pinned by `tools/check_quest_combat_contract.py`) | Open: melee max 10 with it, 9 without; recorded max 6 |
| elemental weakness | earth: Earth Strike tops at 10 (max splat 43 at [1978]; the juvenile 4x), wind/water/fire at 8 (43 at [2075] [2085] [2105] [2019]); 8 + floor(8 x p) = 10 -> p in [25, 37] | earth 35% on the level-scaled strike max 8 (Magic 13+) | matched (cache `param_50` = 35) |
| combat xp | 701 Magic, 188 Hitpoints over 49 casts and 134 damage: 700.9 / 187.6 at x1.05 (687.5 / 178.7 at x1.00) | `combat_xp_multiplier,1050` | matched |
| regeneration | 1 hp every 20 turns, silent (no heal splat), on the clock from her add: all 42 fills fit heals at 1893, 1913, 1933, ..., first undoing damage [1933], 14 in all (134 splat damage kills 120) | was 1 per 100 (2 heals); now `param=regenrate,20` on all nine forms: heals at 1116, 1136, .. 1296 (add + 41 + 20k) | **fixed** |
| headbar | `healthbar` 0, fill = 1 + hp x 29 / 120 (42/42 under the heal model; floor(hp / 4) fits none) | the engine's rule (`ToriRSServer_HealthbarFill`) | matched |
| block kinds | enabled_ops, facing, headbar, hit, level_change, say, sequence, spotanim, transformation; never bas, body, exact_move, transparency, headicon, her own spotanim | same kinds; `say` was missing | **fixed** (say) |
| facing | `[facing] player=` on 28 of 31 walking ticks (fallback angle each time) and 3 idle ones: [1893] retarget, [1894], [2197] `type=reset` | a face row on 11 of 12 steps, the retarget (1120), the reset with the death seq (1311) | matched |
| defend seq | `horror_dagannoth_defend` (1340) on every cast at her, splashes too, delay = the spell's endtime (56, 66, 86): 49/49 | same, engine (ours 56/66 for the blasts) | matched |
| hit sound | synth `dagganoth_hit` (1622) with every landed splat, zeros too: 41/41; none on a splash | the engine's synth from `defend_sound` | matched |

### The form changes ([ai_timer], 10 recorded)

Every 30 ticks from the Air form at [1878] (12/12 with the arrival's). Each change, 10/10:
`transformation` + `level_change` 100 + `enabled_ops 0b11111`, the destination form's `[say]`,
the line "The Dagannoth changes to <colour>..." (gamemessage), and `sound_area dagganoth_changes`
(1617), loops 1, delay 0, range 10, at her tile before that tick's step ([1908]).

| destination | say (R) | line (R) | ticks | ours before | now |
|---|---|---|---|---|---|
| water | Krrrrrrk | blue | [1908] [2088] | transformation only | all four parts |
| melee | Chkhkhkhkhk | orange | [1938] [2118] | " | " |
| earth | Krrrrrrssssssss | brown | [1968] [2148] | " | " |
| fire | Sssssrrrkkkkkk | red | [1998] [2178] | " | " |
| ranged | Krkrkrkrkrkrkrkr | green | [2028] | " | " |
| air | Tktktktktktkt | white | [2058] | " | " |

Ours now: say + area sound on all 7 changes of the run (1111 .. 1291); the lines read back in
the recorded order (`killMother.colour_lines`: blue orange brown red green white). The change
also engages her if nothing has (`npc_attackplayer`), replacing a `npc_setmode(opplayer2)`.

### Her style: which attack, by the player's protection prayer

| player | R | ours before | now |
|---|---|---|---|
| Protect from Melee, 10 tiles | spines [1883] (she fired the tick her footprint reached 10, not at 12 or 11) | melee errand only: walks in, never throws | spines inside `attackrange` 10 (was 8); see Open 1 for the opening |
| Protect from Missiles, flush | melee, every 4 ticks in reach: [1894] [1902] [1906] [1911] [1915] | melee | melee |
| Protect from Missiles, out of reach | closes in a tile a tick ([1885..1892]); where she cannot, nothing at all: [1916..2150], 235 ticks, 0 attacks | melee from anywhere inside attackrange 8 (one swing from 2 tiles, baseline 1115); never closed in under the combat clock | closes in (`npc_approach`, clock re-armed each tick); idle when blocked (1111..1116) |
| none, out of reach | spines [2152], 2 ticks after Protect from Missiles went off at [2150] | never | spines (1118) |
| none, flush | not recorded | melee | melee (UNVERIFIED) |
| style change | one tick ([2150] off -> [2151] -> [2152]), as LostCity's op -> ap hand-over | `npc_setmode` swap, which the combat clock never ran | npc var 0 + `npc_attackdelay(1)`: switch 1117, throw 1118 |

### Spines

| field | R ([1883], [2152]) | ours before | now (1118) |
|---|---|---|---|
| seq | `horror_dagannoth_rangeattack` (1343), delay 0, 2/2 | same | same |
| sound | `sound_area dagganoth_spines` (1623) range 10 at her south-west tile, 2/2 | none | same |
| projectile | `horror_spines_travel` (294) | `dagannoth_spine_spotanim_travel` (475) | 294 |
| source | her middle tile (sw + 1), 2/2 | her south-west tile | middle (2518,4636) |
| starttime / endtime | 10 / 41 + 5 per tile from her middle: 96 at 11, 56 at 3 | 32 / 32 + 5 per tile from the corner | 10 / 56 at 3 (the tick log) |
| angle / progress / heights | 25 / 50 / 210 -> 110 raw (v2 packet) | 15 / 11 / 160 -> 144 | 25 / 50 / 210 -> 110 through `projanim_pl_raw` (not tick-log fields) |
| player's block | `human_shield_defence` sent on the throw, delay = endtime (96, 56) | same rule | `human_unarmedblock` delay 56 (the bot's kit) |
| splats | 2, landing throw + floor(end / 30): +3 [1886], +1 [2153] | same rule | 2 at +1 (1119) |
| damage | unprayed 7, 7; 0, 0 under Missiles | 0..12 each after an accuracy roll | same: 0, 5 unprayed |

### Melee

| | R (5 swings, all under Protect from Missiles) | ours | verdict |
|---|---|---|---|
| seq / sound | `horror_dagannoth_attack` (1341) delay 0; synth `dagganoth_attack` (1615) delay 0: 5/5 | same (the id now by name) | matched |
| reach | flush after her step that tick: 5/5 ([1902] [1911] the player 2..3 away at the end of the tick before) | was anywhere inside attackrange 8; now flush only | **fixed** |
| landing | the hit and the player's block on the swing tick: 5/5 | same (`~playerhit_n_melee`) | matched |
| cadence | 4 ticks in reach ([1902] -> [1906], [1911] -> [1915]) | attackrate 4 (1123, 1127, 1131, ...) | matched |
| damage | 2, 5, 0, 4, 6: 1 zero (1.6 predicted: her stab roll 5,568 against the recorder's 2,691), max 6 | formula max 10; 46 swings at the bot, 0..10 | consistent; max not identified |

### Hits on her: the colour rule

| | R | ours | verdict |
|---|---|---|---|
| matching element | a damage splat: 31 casts, values 1..8 (10 with Earth) | the element latch through `~horror_mother_prepare_hit` | matched |
| wrong element (or a spell on the melee/ranged forms) | the impact spotanim and a blue 0 (`hit id=12`) + `dagganoth_hit`: 11/11 | a 0 queued, `hitsplat_block` | matched |
| splash | `failedspell_impact` + area `spellfail` and NO splat, no hit synth: 7/7 (accuracy 42 of 49, fits magic level 1 / magic 50) | `~pvm_spell_fail` queues nothing | matched |
| when the colour is read | at the CAST: fire at Fire landing on Ranged hit 1 ([2026] -> [2029]), wind at Air landing on Water 1 ([2087] -> [2090]); wind at Ranged landing on Air 0 ([2057] -> [2060]), earth at Melee landing on Earth 0 ([2146] -> [2149]): 4/4 | prepared in the cast's funnel; our 1110 wind blast at Air landed 16 on Water at 1113 | matched |
| melee / ranged on their forms | not recorded (the recorder only cast) | melee and ranged damage types only | unverified |

### Death

| | R | ours | verdict |
|---|---|---|---|
| killing splat | [2196] 2, fill 0 | 1310 | |
| death | [2197]: `horror_dagannoth_death` (1342) delay 0, `[facing] type=reset`, `enabled_ops 0b00000`, synth `dagganoth_death` (1621) | 1311: seq, reset, synth; ops cleared at 1310 with the splat | matched; ops a tick early (engine, Open) |
| despawn | [2199], with the stage, scroll and teleport | 1313, the completion on the same tick | matched |

### Every splat and heal (rsprox_hit_causes.py, rsprox_player_hits.py)

| cause | R | ours (after) | verdict |
|---|---|---|---|
| melee swing, flush | 3: 0..6 (Missiles) | 46: 0..10 (Missiles), 1 unprayed 0 | matched (the bot stands flush after its auto-retaliate walk) |
| melee swing, near (2..3 at the end of the tick before) | 2: 4, 5 (step and swing on one tick) | 0 (before: 1, from 2 tiles) | engine order (Open 4) |
| spines | 2 + 2: 0, 0 (Missiles), 7, 7 (none) | 2: 0, 5 (none) | matched |
| none / unexplained | 0 | 0 | |
| heals on her | 14, silent | 10 in the run, silent (`npc_heal regen`) | matched |

### Open

1. **The opening spines at 10 tiles ([1883]) need the arrival.** `[queue,horror_spawn_mother]` hands
   her `npc_setmode(opplayer2)`, an errand that walks to melee before any decision, so ours
   cannot throw until she is hit, reaches the player, or changes colour. With
   `npc_attackplayer` there (in place of the errand), `[ai_opplayer2]` would throw from 10 tiles.
   Not changed: the arrival is outside this audit's scope.
2. **Protect from Missiles read at the throw or at the landing.** The [1883] spines were thrown
   under Protect from Melee and landed 0, 0 under Missiles: P = 0.16 if read at the throw (her
   ranged roll 3,776 against the recorder's 2,613: 35% misses, plus 1 in 13 zero rolls), 1 if
   read at the landing. Ours reads it at the throw, as `~playerhit_n_ranged` and LostCity do.
3. **The switch tick lands a tick earlier in ours.** Our engine let her turn see the prayer
   dropped by input in the same tick (1117 -> 1118); Jagex's turn came first ([2150] -> [2152]).
   The content's one-tick switch is the recorded one; the phase order is the engine's.
4. **A step and an attack on one tick.** Jagex steps and attacks together ([1883] [1902] [1906]
   [1911]); our combat turn tests reach before the step (`torirs_server_combat.c`), so ours
   attacks the tick after closing in. Engine-generic.
5. Melee against an unprotected player standing flush: never recorded (ours melee).
6. The spines' max 12 and accuracy roll (LostCity's 12, with no roll): 7, 7 cannot identify
   either; the formula max from ranged 50 and no bonus is 6, below the recorded 7.
7. Strength bonus 9 (ours) against the cache's none; ranged defence 50 (no ranged defence in
   this cache's npc records); `huntmode=aggressive` + huntrange 8 on the forms is inert (the
   engine seeds huntmode at the add, from `horror_dagganoth_aira`).
8. **No 0 on an accurate strike: 0 of 31 on her, 0 of 28 on the juvenile** (expected 5.9 under
   our 0..max roll, p ~ 0.003), and 1s are frequent on her (10 of 31). The player's magic damage
   roll is generic: a cross-recording check, not this fight's rule.
9. `enabled_ops 0b00000` goes out with the killing splat in ours, with the death seq in the
   recording (engine death).
10. During the rise ours faces the player from T+2 (1076 on); the recording sends
    `[facing] type=reset` at [1874] and its first player face at [1881] (arrival / engine).
