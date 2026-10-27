# Tree Gnome Village from RSProx

Rebuilt 2026-10-27 from the one recording that plays the whole quest:

| Session | Rev | Covers |
|---|---|---|
| index 4394 (local, 2026-10-27, w631) | 241.1 | Bolren [244] to the quest scroll [2134]; the battlefield throughout; the Molly random event [1004..1124] in the middle (docs/rsprox-analysis/molly_evil_twin.md) |

The other sessions naming these npcs only pass by (818, 820, 821, 3262, 3909, 3983:
one row each) or are The Path of Glouphrie (1678). Lines no recording shows come from
the wiki's `Transcript:Tree_Gnome_Village` (read 2026-10-27) and are marked `[wiki]`
in the scripts, their moods unrecorded; lines in neither are LostCity's and say so.

The content was a LostCity (2004) port (skill §16). It passed its quest gate with:
no Start-quest confirm, the trackers speaking their coordinates aloud, a ballista that
asked for three coordinates, no Follow op on Elkoy, a warlord who turned on you in
place and put the orbs in your pack, a ceremony that completed the quest before it
began, and a battlefield where 130 soldiers stood still.

Content: `OSRS-Content/osrs-content/server/scripts/`
- `areas/area_gnome/scripts/king_bolren.rs2` -- the start, the first orb, the ceremony
- `areas/area_gnome/scripts/elkoy.rs2` -- Talk-to and Follow
- `areas/area_gnome/scripts/commander_montai.rs2`, `local_gnome.rs2`, `gnomevillage_fence.rs2`
- `quests/quest_tree/scripts/tree_guide.rs2` -- Elkoy's guided walk, the fade
- `quests/quest_tree/scripts/tracker_gnome.rs2`, `quest_tree_locs.rs2` (ballista, door, wall, chest)
- `quests/quest_tree/scripts/khazard_warlord.rs2`
- `quests/quest_tree/scripts/tree_battlefield.rs2` -- the battle and the catapult
- `quests/quest_tree/configs/quest_tree.{varp,constant,npc}`

Test: `test/quests/tree.lua` (110 of 111 rows on private packs, 2026-10-27; the one
failure, `talkToKingBolren.camelotTeleport.cast`, fails the same way in every run and
is the driver's teleport check window, not this content).

## Stages (`%varp111_treequest`) and varbits, every write recorded

| Write | Goes out with | 4394 | ours |
|---|---|---|---|
| quest 1 | the click of "I would be glad to help.", with "You've started a new quest" | [298] | same |
| quest 2 | "Please be as quick as you can..." | [626] | same |
| quest 3, six logs removed | "I have some here. (You give six loads of logs...)" | [876] | same |
| quest 4, **ballista (602) rolled** | the click of "Thank you, you're braver than most." | [893] | same, `random(4)` |
| gnometracker_h (599) | "The gnome tells you the height coordinate." | [962] | same |
| gnometracker_y (600) | "The gnome tells you the y coordinate." | [1182] | same |
| gnometracker_x (601) | tracker 3's first page, before the riddle | [1207] | same |
| quest 5 | the click of the hit's mesbox | [1275] | same |
| quest 6, the orb | "You search the chest..." | [1328] | same |
| quest 7, the orb removed | the click of "I will safeguard this orb...", with the guide | [1435] | same |
| khazard_warlord_fighting (3460) 1 / 0 | the click after his last page / 30 ticks after his death | [1520] / [1910] | same |
| quest 8, orbs on the ground | his death | [1880] | same |
| bolren_got_orbs (598) 1, the orbs removed | the click of "...pay our respects." | [2096] | same |
| bolren_got_orbs 2 | C+11 of the chant (with `entendseq` on the tree) | [2110] | same |
| quest 9, qp, amulet, 11,450 Attack, scroll | the last page's click | [2134] | same (LostCity queued it at the ceremony's start) |

The varbits 598-602 sit on `varp279_awarded_points`, which the generated block had as
`scope=temp, transmit=no`: none of them would have reached the client or survived a
logout. It is `perm` and transmitted now (`quest_tree.varp`; the generated lines
deleted). The same varp carries the Wise Old Man's bits (596, 597, 960, 6610).

## The pages

`tools/rsprox/rsprox_pages.py` lists 263 pages; every one outside the Molly event is
in our scripts verbatim with its recorded mood (a mechanical check: 0 missing, 0 mood
mismatches). Fixes: every forced `|` line break removed (the layout wraps where the
client does); Jagex's double spaces kept ("shot.  We
need", "hit.  I've"); the two white asides written with Jagex's own `<br><col=ffffff>`
([262], [876]); tracker 3's riddle lines ([1218], [1220]); the menu order of Bolren's
first choice ("Can I help at all?" first).

## King Bolren's start [244..304]

| R | ours | verdict |
|---|---|---|
| the combat warning mesbox (below 45) [292] | `~player_combat_level < 45` | fixed (missing) |
| "Start the Tree Gnome Village quest?" Yes / No [293] | `~p_choice2_header` | fixed (was "I would be glad to help." / "I'm sorry...") |
| "I would be glad to help." for Yes [297] | same | matched |
| the stage + "You've started a new quest" on that click [298] | same | fixed (the line was missing) |
| "My assistant shall guide you out." then the guide [300..304] | `~tree_elkoy_guide` | fixed (was a bare telejump) |

## Elkoy's guided walk (Follow and every "guide you out")

One frame, six times ([300], [308], [314], [1376], [1435], [1964]):

| tick | R | ours |
|---|---|---|
| O | messagebox, continue row empty and no events; fade_overlay 948 (0,255,0,0,50), the hp bar hidden and emptied | `~tree_mesbox_open` + `~tree_fade` |
| O+1 | busy 1 | engine |
| O+3 | the teleport | `p_telejump` after `p_delay(2)` |
| O+4 | fade in (0,0,0,255,50); Elkoy's page, head `elkoy_1op` whichever Elkoy | `p_delay(0)`, `~tree_fade`, `~chatnpc_specific_anim(.., elkoy_1op, ..)` |
| O+6 or the click | `if_closesub overlay_atmosphere` | `queue(tree_overlay_close, 2)` |

Elkoy's Follow (op 3) did not exist in ours. Both Elkoys are multinpcs on the quest
varp: `elkoy_1op` (Talk-to only) at stages 0 and 7 outside, stage 0 inside; ops bind
on the parent (`[opnpc3,elkoy]`). Destinations: in (2515,3159), out (2504,3192), and
Bolren's exits (2504,3191). The pages on arrival: [312] [318] stages 1..5, [1380]
stage 6 with the orb, [1968] stage 8; the rest the wiki's. The talk's "Ok then,
follow me." (LostCity) is gone: the wiki's Yes goes straight into the guide.

## The trackers [952..1224]

| R | ours | verdict |
|---|---|---|
| tracker 1 spoken to from 2 tiles [960] | `[apnpc1,tracker1]` + `p_aprange(2)` | fixed (was opnpc) |
| no coordinate said aloud; a mesbox names which one [962] [1182] | `~mesbox`, the varbit before it | fixed (said "The height coordinate is 4.") |
| tracker 2 across his cell's bars [1178] (player 2524,3255, he 2526,3257) | apnpc range 2 (unchanged) | matched; the test now stands on 2524,3256 (he wanders z 3257-3258, recorded too) |
| tracker 3's riddle = the rolled x; "Dance with me, and Khazard's men are beat." [1218] [1220] | `~tracker3_riddle` from `%varb602_ballista` | fixed (a fixed 2004 riddle) |
| "All day we pray in the hay, hee hee." chatcon1 | `^chat_confused` | fixed (quiz) |

## The ballista [1129..1275]

| R | ours | verdict |
|---|---|---|
| height and y entered for you; only x asked, once tracker 3 is heard: "That tracker gnome was a bit vague..." + "Enter the x-coordinate of the stronghold" 0001-0004 [1249..1252] | same | fixed (three menus of five) |
| "You enter the height (and y) coordinate(s) you got from the tracker gnome(s)." [1131] [1255] | same | fixed |
| on its click: `catabow_launch` (730) on the ballista, `ballista_travel` (182) (2509,3213) -> (2509,3254) 110 -> 375, angle 7, progress 0, heights 95 -> 0 (v2; ours 24 -> 0), synth `treevillage_catabow_launch` (3488) [1133] [1256] [1271] | same, ticklog [304] [314] | fixed (no projectile, no sound) |
| +2: "The huge spear completely misses the Khazard stronghold!" or the hit's text with synth `treevillage_catabow_hit` (3487) [1135] [1258] [1273] | same ([316]) | fixed |
| a miss: "Evidently that wasn't the right x coordinate." [1260]; with the height only: "I'm not going to be able to hit... height coordinate." + "Maybe I should ask Montai's other tracker gnomes..." [1138] [1144] | same | fixed |
| the stage on the hit box's click [1275] | same | matched |

Unrecorded and extrapolated (labelled in the script): the y-only box, the "...height
and y coordinates." line, and no coordinate at all (LostCity's line).

## The stronghold [930..1330]

| R | ours | verdict |
|---|---|---|
| door from outside at stage 4: mesbox "...find<br>another way to get in." [934] | same | fixed (forced break) |
| low wall before the ballista: "The wall is damaged, but not enough to climb." [939] | same | matched |
| after: the box, two steps on its click, `human_walk_crumbledwall` (840) delay 30, exact move (2509,3252) -> (2509,3254) 30..100 north [1303..1309] | same (ticklog 840/30) | matched |
| the commander swings the tick after the crawl [1310], "Don't even think of taking the orb!" [1311] | `npc_setmode(opplayer2)`, `p_delay(0)`, the say | fixed (was a three-line conversation) |
| chest: `human_openchest` + synth 52 [1325]; the open chest + "Oi! You! Get out of there." [1326] | `~open_chest` + say | matched |
| search: stage, orb, box, nobody speaks [1328] | same | fixed (LostCity's "Hands off..." deleted) |

The commander's own fight (two of them, ground floor and upstairs, both attacking on
sight) is a combat encounter of its own: OPEN, see below.

## The ceremony [2085..2134]

| tick | R | ours |
|---|---|---|
| [2096] | 598 = 1, the orbs removed, the mesbox | same |
| C [2099] | Bolren steps to (2542,3169), face reset; the player to (2542,3170); the north pair "Su tana." + `gnome_chanting`, facing (2543,3170) | `p_walk`, `npc_walk`, `npc_facereset`, `~tree_chant(true, ..)` |
| C+1 | the player faces (2543,3170), Bolren (2543,3169) | same |
| C+2, C+4, C+6 | south "En tania.", north, south | `~tree_chant` |
| C+8 [2107] | Bolren `gnome_cast_globes` (199) delay 20, `gnome_globes` (79) height 124 delay 20 | same (LostCity had height and delay swapped) |
| C+11 [2110] | 598 = 2, `entendseq` (333) on the tree | same (missing) |
| C+15 [2114] | "The orbs of protection come to rest..." | same |
| [2134] | the completion on the last page's click: 2 QP, 11,450 Attack, the amulet, jingle 154, the scroll "2 Quest Points / 11,450 Attack XP / Spirit Tree Access / A Gnome Amulet" | `~tree_quest_complete` there (it was queued before the chant) |

The chanters are found by side of Bolren's tile (the wiki's "Northern / Southern Local
Gnomes"); in 4394 two each, (2540,3171) (2542,3172) north, (2542,3168) (2542,3167) south.

## The loose railing [221..228], [1383..1388], [1969..1973]

Face on the op tick (L+1), busy L+2, at L+3 the spam line "You squeeze through the
loose railing.", `railing_squeeze` (3844) with no delay and the exact move 15..90, busy
off two ticks later; all three crossings north. Ours had `human_into_sidestep` delay 30,
one tick early, as a game message. Fixed.

## Every teleport and maplink in the session

Every player `[teleport]` in 4394, with what caused it:

| R | what | ours | verdict |
|---|---|---|---|
| [226] [1386] [1974] | the loose railing | above | fixed |
| [303] [1438] | Bolren's guided exits | `~tree_elkoy_guide` | fixed |
| [311] [317] [1379] [1967] | Elkoy's Follow | `~tree_elkoy_guide` | fixed (missing) |
| [325] | Varrock teleport tablet, broken at [321] | `[opheld1,poh_tablet_varrockteleport]` | **fixed: no standard tablet had a Break** |
| [498] [640] | Ardougne teleport tablet, [494] [636] | the same | fixed |
| [1309] | the crumbled wall | above | matched |
| [1319] / [1335] | the stronghold's ladder up / laddertop down (2503,3252) | `~climb_ladder(±1)` | matched: up from standing, 828 on C, the move C+1 (ours [341] [342]); down after a walk, the face L+1, 827 L+2, the move L+3 (ours [362..364]) |
| [1982..2048] | the village's five house ladders, up (`ladder_directional`) and down (`laddertop`, `laddertop_directional`, `laddertop_norim`), each ±1 plane on its own tile | all bound to `_climb_up_ladder` / `_climb_down_ladder`, no maplink row on any of the six tiles | matched (category and destination; not run) |
| [1014] [1123] | the Molly random event | macro_evil_twin | the other session's |

The tablets (`skill_magic/scripts/spells/teleport_tablet.rs2`, all three breaks the
same): on the click `poh_smash_magic_tablet` (4069) delay 16 and synth
`poh_tablet_break_teleport` (965) delay 15; C+2 `poh_absorb_tablet_magic` (678),
`poh_absorb_tablet_teleport` (4071) and the tablet gone; C+4 the teleport, the seq
cleared, `cam_reset`. Varrock lands on its spell's tile (3213,3424); Ardougne beside
its spell's (2661,3301) at (2662,3301) and (2662,3302). Ours, `test/magic/
teleport_tablets.lua` (6/6): [9] 4069/16 + 965/15, [11] 678 + 4071, [13] at
(3213,3424); [22], [24], [26] at (2661,3301). Bound: Varrock, Lumbridge, Falador,
Camelot, Ardougne, Watchtower, Kourend. Not bound: the house tablet (POH) and Fortis
(no spell row). The Arceuus `teletab_*` above them still use the spell's cast
animation: no recording here shows one broken.

## The battlefield (tree_battlefield.rs2)

Nothing of it existed: the 134 spawned soldiers wandered. Recorded, ~1,400 ticks on
the field: 568 arrows, 43 catapult shots, ~970 npc lives.

| R (4394) | ours (run 4, ~300 ticks) | verdict |
|---|---|---|
| gnomes shoot every 4 (gnome_attackbow, iron_arrow_launch h48, arrowlaunch2) | every 4 (82/86 gaps) | matched |
| Khazard troops every 5 (human_blunt_pound, warhammer_crush), spearmen 6 (human_spear_lunge, staff_stab), terrorbirds 4 | 5 (57/64), 6 (19/21), 4 (32/32) | matched |
| arrow 41 -> 46 + 5d, angle 15, progress 11, homing; d = 1/2/3 in 393/161/14 | same shape, d = 1 always | OPEN: our gnomes close to their attackrange 1 |
| target's block delayed to the arrow's end + gnome_hit at the target | same | matched |
| splats: blade5/blade2/gnome_hit/terrorbird_hit with them; death synths gnome_death/human_death | same | matched (fitted) |
| damage: spearmen 26% nonzero, troops 38%, nearly all 1s; gnomes 58% nonzero, 1-2 | arrows 1 in 3 for 1; Khazard swings 3 in 5 for 1-2 | fitted, not identified (no accuracy roll) |
| gnome deaths 1 per 3.8 splats | 1 per 5 | close |
| gnome shouts 39/317 shots; Khazard shouts 30/356 splats | 1/8, 1/12 | fitted |
| catapult every 10: loc anim 443, catapultload 15 -> 10d + 36, angle 25, progress 64, heights 200 -> 10 (ours 50 -> 3), to x 2519..2529 z 3234..3243; impact (end + 15) / 20 ticks on | same (45 shots in run 4) | matched (7/7 impact fits) |

## Open

- The Khazard warlord fight: its own audit (one agent per encounter), section below.
- The Khazard commanders' fight in the stronghold: not audited yet.
- Battlefield: arrow distances (d = 1 only), the damage model is fitted not identified,
  and the gnome archers further back rarely find a target within 6 tiles.
- Unrecorded branches (Elkoy's talk, Remsai, Kalron, Bolkoy, stage 0 Elkoy) are the
  wiki's or LostCity's.
