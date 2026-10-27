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
| the commander takes the player the tick after the crawl [1310], "Don't even think of taking the orb!" [1311] | his own hunt (see "The Khazard commanders"), the say at C+2 | fixed (was a three-line conversation, then a one-swing errand) |
| chest: `human_openchest` + synth 52 [1325]; the open chest + "Oi! You! Get out of there." [1326] | `~open_chest` + say | matched |
| search: stage, orb, box, nobody speaks [1328] | same | fixed (LostCity's "Hands off..." deleted) |

The commanders' own fight (two of them, ground floor and upstairs, both attacking on
sight) is audited in "The Khazard commanders" below.

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

## Every action in the session (`tools/rsprox/rsprox_actions.py`)

| first R | action (count) | verdict |
|---|---|---|
| [221] | treegnomelooserailing op1 (3) | fixed (below) |
| [244] [259] [613] [952] [1178] [1202] | Bolren, chanters, Montai, trackers 1-3 talk | fixed (pages) |
| [248] | ent (spirit tree) op2 Talk-to: "The tree doesn't feel like talking." | matched (`spirit_tree.rs2`) |
| [308] [314] | Elkoy / elkoy_village op3 Follow | fixed (missing) |
| [826] | logs used on Montai: engine "Nothing interesting happens." | matched (no binding, engine default) |
| [930] [936] [998] [1321] [1328] | stronghold door, low wall, ballista, chest | fixed (below) |
| [1174] | poordoor op1 (the jail): `loc_del` + `poordooropen` on (2524,3255) rot 2, synth 62 | matched in the test (pass_door); shape not diffed |
| [1314] [1332] [1978..2047] | ladders up/down, the house ladders | matched (below) |
| [1504] [1521] | the warlord talk / attack | fixed; the fight's audit: its own section |
| [1887] | orbs_of_protection pickup (private drop) | fixed |
| [321] [494] | teleport tablets (Varrock, Ardougne) | **fixed: no Break existed** |
| [119] [510] [761] [1634] | stamina / prayer potion drinks | generic, NOT checked |
| [264] [696] [706] | tree, willowtree, tree2 chop (logs for Montai) | generic, NOT checked |
| [366..512] [653] | GE clerks, offers, bank booths, banker | generic, NOT checked |
| [400] [461] [652] [1762] | equip earth battlestaff / staff of earth / priest gown, unequip weapon | generic, NOT checked |
| [846] [856] | drop spade, lobster | generic, NOT checked |
| [1885] [1890] | bones pickup, bury | generic, NOT checked |
| [2004] | treevillage_shopkeeper1 Trade | generic, NOT checked |
| [659] [661] [1311] | autocast, Protect from Melee | the recorder's kit |
| [2] [1580] [2139] [2150] | welcome screen, chat filter, scroll close, logout | client / engine |

## The quest journal

Unverified. 4394 never opens it, and no session on the index shows Tree Gnome Village's
(3976 and 311 name the quest only in the spirit trees' menu; 4332 opens other quests'
`questjournal_overview`). `quests/quest_tree/scripts/tree_journal.rs2` is LostCity's 2004
text, unchanged.

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

- The two fights (warlord, commanders): their own sections below, each with its Open list.
- Battlefield: arrow distances (d = 1 only), the damage model is fitted not identified,
  and the gnome archers further back rarely find a target within 6 tiles.
- Unrecorded branches (Elkoy's talk, Remsai, Kalron, Bolkoy, stage 0 Elkoy) are the
  wiki's or LostCity's.
- The journal (no recording) and the reward's spirit trees (the "Spirit Tree Locations"
  menu, recorded in 311 and 3976) are not audited.
- The generic interactions marked NOT checked in the action table.

## The Khazard warlord

One recorded fight: index 4394 [1501..1910]. The shell (4971), its talking rung (7621),
the fight's warlord (7622) and varbit 3460 are in no other indexed session (`search npc
7622 / 4971 / 7621 --any`, `search varbit 3460`: 4394 only), so every count below is one
fight. The recorder (combat 32) cast earth strike from a staff of earth every 5 ticks from
(2457,3301), adjacent south of him, with Protect from Melee up throughout. Ours:
`test/quests/tree.lua` on private packs (`build/quest_gate/tree_warlord/ticklog.tsv`; the
seed is the run name): fire bolt every 9 ticks at Magic 99 with punches between (the
driver), no prayer. Anchors: A = the fight's warlord's add, K = the killing hit, D = his
del. Recorded A [1520], K [1878] (A+358), D [1880]; ours A 461, K 798, D 800.

Content: `quests/quest_tree/scripts/khazard_warlord.rs2`, the `[khazard_warlord_combat]`
block of `quest_tree.npc` (its keys deleted from `server_fields.generated.npc`). Tools:
`rsprox_npcinfo.py`, `npc_block_inventory.py --values`, `multinpc_stance_audit.py`,
`npc_movement_audit.py`, `rsprox_player_hits.py`, `rsprox_hit_causes.py --by-prayer`,
`check_npc_cache_levels.py` (0), and `TORIRSSERVER_TRACE_OUT=1` for the interface and
hint packets. "Before" is the pack this section started from (the 09:17 commit).

### Timeline

| tick | recorded (4394) | ours | verdict |
|---|---|---|---|
| A | the click of the last page [1519], then at [1520]: varbit 3460 0->1, `current_hint_arrow` (14192) 0->7, busy 0; the shell `[del]`; 7622 `[add]` on the shell's tile, spawnangle south, with `level_change` 112, `[facing]` the player, `enabled_ops` 0b11111, `[transformation]` 7622; `if_closesub` chatmodal; `hint_arrow` reset, then the npc | the same on A = click + 1, except the shell stays in our npc list (its rung resolves to -1, the client draws nothing) | fixed: no re-type on the add, the close went out on the click tick, no hint reset, 14192 never set. OPEN: the shell's `[del]` (engine) |
| A+2 | his first swing, before the first spell lands (A+3): he takes the player on himself | A+2 | fixed: `npc_setmode(opplayer2)` swung on A+1 (the errand) and A+2 (the retaliation's errand), then nothing until the first landing latched him (A+7) |
| every 4 | `human_blunt_pound` (401) + synth `warhammer_crush` (2567); the recorder's block + synth 511 delay 20 (88/88 gaps of 4) | the same (83/83) | fixed: 422 and silent |
| each cast | `human_blunt_def` (404) delayed 56, sent on the cast's tick, splash or not, unless his own swing is that tick (54 of 72 casts; the other 18 went out as 401) | 404 delayed 56 on 28 of 38 casts, the other 10 on his swing ticks; 404/0 on the punches | fixed (was 424) |
| each hit's landing | the splat; `[transformation]` + `level_change` 112 + `enabled_ops` 0b11111; synth `blade2` (29) | the same | fixed: no re-type, no sound |
| each splash, cast+1 | `[transformation]` + `level_change` + `enabled_ops` 0b11111, no sound (18/18) | the same (14/14) | fixed |
| K | the splat, headbar 0, `human_death` (836), `[facing] type=reset`, `enabled_ops` 0b00000 with the re-type, synths 29 and `human_death` (512) | the same | fixed: 836 and the reset came on K+1, silent |
| D = K+2 | `[del]`; bones (`reveal=100, despawn=200, ownership=self`); stage 8; orbs (`despawn=200, ownership=self, neverturnpublic=true`); `map_anim smokepuff` (86) height 10; `sound_area smokepuff` (1930) range 5; `sound_area myarm_male_scream` (3200) delay 10 range 5; synth `put_down` (2739); the objbox; `script7192` for the bones only | the same, D = K+2 | fixed: D was K+3, no bones, no vapour, scream or put_down, and 7192 named the orbs. OPEN: our obj ownership fields; our hint clear on D |
| D+30 | varbit 3460 -> 0 [1910] | D+30 | matched (was D+31) |

The vapour and the scream are the objbox's "ghostly vapour" and "shrill scream": in 4394
they come only with the quest's kill; smokepuff and put_down appear with no other npc death
or bones drop in 4394, 4378, 4384, 4375 or 4392.

### Npc info, every block kind

| kind (7622) | recorded | before | ours | verdict |
|---|---|---|---|---|
| `[facing]` | 2: the player on the add, reset on K | 5: resets on A+1 and A+2, the player again on A+7 | 2 | fixed |
| `[transformation]` | 73 ticks: the add, 54 hit landings, 18 splashes at cast+1 | 0 | 76 ticks: the add, 61 landings, 14 splashes | fixed (per landing, 72/72 and 75/75) |
| `level_change` 112 | 73 | 0 | 76 | fixed |
| `enabled_ops` | 72 x 0b11111, 1 x 0b00000 (K) | 1 (K) | 75 + 1 | fixed |
| `[sequence]` | 401 x 89, 404/56 x 54, 836 x 1 | 422 / 424 | 401 x 84, 404/56 x 28, 404/0 x 28, 836 x 1 | fixed |
| `[spotanim]`, `[hit]`, `[headbar]` | the recorder's spells (98 / 85), 54 splats | | fire bolt (128 / 85), 61 splats | the attacker's kit |
| movement | none (198 idle ticks) | none | none | matched |
| `[bas]`, `[body_customisation]`, headicons, `[say]`, `exact_move` | none | none | none | matched |

The block inventory lists no recorded kind we never send. Our tick log writes one
`npc_retype` row per call (100), two on a spell's landing (its two queues); the wire carries
one block per tick (76).

The shell (`khazard_warlord`): `multinpc_stance_audit.py` "same" (shell and rung share the
human stance set). It wanders (2 steps in its 19 ticks in view, [1507] east, [1508] south)
and faces the player with the first npc page [1509]; ours wanders (the generated default)
and faces on the first npc page (455, the page after the player's at 454). The fight's
warlord never left combat in 4394 (0 steps); it keeps the generated wander 5 and maxrange 7
(the shell's recorded wander; the quest page's leash, "head west until the warlord
de-aggros"): unverified.

### Sounds

| recorded | n | ours | verdict |
|---|---|---|---|
| synth 2567 `warhammer_crush`, each swing | 89/89 | 84/84 | fixed (none) |
| synth 511 `human_block_1` delay 20, each 0 on the player | 89/89 | 78/78, and 518..521 delay 20 on the 6 hits | engine, matched |
| synth 29 `blade2`, each hit on him | 54/54, none on the 18 splashes | 61 landings, none on the 14 splashes | fixed (none); a 0-damage landing's sound is unrecorded (no 0 landed in 4394) |
| synth 512 `human_death`, K | 1 | 1 | fixed (none) |
| D: area 1930 r5, area 3200 delay 10 r5, synth 2739 | 1 each | 1 each | fixed (none) |
| 132 / 133 / 227 | 72 / 54 / 18 | | the recorder's earth strike |
| 3120 x3, 3424, 2396+2384 x3, 2238, 2401, 2663, 2738, 909+521 [1907] | | | the stamina warning, level-up fireworks, an equip, a potion, prayer off, burying the bones, a wolf on the way home: not the fight |

### Damage, accuracy and every splat (skill sections 13, 13b)

- **His swings:** 89 splats on the recorder, each on the swing's own tick, all 0 under
  Protect from Melee (89/89; `prayer_allactive` 16384 until [1884]). That rule holds in ours
  by `~playerhit_n_melee_apply`; the max and the accuracy are not identifiable from this
  fight.
- **Max hit:** the wiki's 10 (infobox: +0 strength, max hit 10). `~npc_melee_maxhit` gave 9
  from +0: `strengthbonus` 6 gives 10 (6..13 all do). Ours, unprayed: 84 swings, 78 zeros,
  6 hits 1..9; the attack roll (75+9) x 64 = 5376 against the test player's crush defence
  roll 28034 predicts 7.3 hits.
- **Accuracy against him:** an npc defends magic with its Magic level. The recorder
  (`--bas`: staff of earth +10, amulet of glory +10, Magic 10..13) predicts 13.7 splashes
  of 72 against (1+9) x 64 = 640; recorded 18. A Defence-based roll predicts 61. So Magic 1
  (the cache leaves stat6 at its default). Ours: 14 of 38 against 8.1 predicted from our
  roll 1498 (p ~ 0.02, one seed; the player's side).
- **Levels:** the cache's stat1..4 (75/80/78/170), stat5/6 unstated (1);
  `check_npc_cache_levels.py` 0.
- **Every splat** (`rsprox_hit_causes.py --npc khazard_warlord --by-prayer`): one cause
  on both sides, `npc:khazard_warlord_combat:human_blunt_pound@0:adj`, 89 (0..0) recorded and
  84 (0..9) ours; no `none`, no heal, no content damage on him. Every splat on him is his
  attacker's (54 earth strikes; ours 24 fire bolts and 37 punches).
- **Combat xp:** the recorder's 72 casts paid earth strike's 9.5 Magic each and almost
  nothing for 173 damage. Replaying every Magic and Hitpoints update of the fight
  (`update_stat_v2`, tenths kept, the damage part 2 and 4/3 per point x the multiplier) fits
  a multiplier of 25..28 per 1000 and no other value: `combat_xp_multiplier` 25 (it was
  1000). The wiki's trivia ("only yields the base magic experience") is this. OPEN: our
  `~give_combat_experience` floors the damage part before the x2 / x4/3, so at 2.5% it pays a
  tenth or two less per hit than Jagex.

### Unrecorded, and how ours does it

- **"Bah, enough of you!"** (Transcript:Khazard warlord, "Any time he is aggressive but not
  attacked and cannot attack the player for long enough"; the quest page: he may despawn
  when safespotted too long, not when flinched): the npc timer, re-armed by each of his
  swings and by every landing on him, fires after `^tree_warlord_idle_ticks` (50) with
  neither: the line and `npc_del`. On our wire with a scratch test (stage 7, the talk, then
  the walk to (2458,3284), the recorder's own way home [1891..1899]): his last swing at 19,
  the line and his removal at 69, varbit 3460 back at 100 (removal + 31: `npc_del` keeps
  him findable through his removal tick, where a death resets at +30), the talking warlord
  back and talkable (11/11 rows).
- The re-type on a melee landing (the recorder only cast): the same as a spell's.
- The vapour, scream and put_down on a later kill: none in ours (only the quest's kill
  was recorded).
- The npc_add duration (`^tree_warlord_combat_duration`, 1000 ticks) is a backstop no source
  states: a fight longer than ten minutes would lose him. Unchanged here.

### Open (engine or shared code, not changed here)

1. **The shell's `[del]` on A:** Jagex takes the shell out of the player's npc info when its
   rung resolves to -1; ours keeps it listed and the client hides it.
2. **Our hint clear on D:** `hint_arrow_flush` (torirs_server_encode.c) clears an npc hint
   whose npc left view; 4394 sends no hint packet after [1520] to the logout, and
   `current_hint_arrow` stays 7.
3. **Ground items:** our rev-241 OBJ_ADD sends `time_until_public`, `ownership` and
   `never_becomes_public` as 0 for every obj (the bones: reveal 100, self; the orbs: self,
   never public).
4. **A successful spell's 0:** earth strike landed 0 in 0 of 58 recorded landings (18 ones;
   a uniform 0..max roll gives that with p ~ 1e-4), fire wave 1 of 98, across 80 cached rev-241
   transcripts (idx42xx-43xx); ours rolls 0..max (1 of 24 fire bolts). Shared player magic.
5. An npc added facing a player who is not due south of it re-sends the face on the next
   tick (`face_sent_angle` starts at 0, torirs_server_world.c). Not on this fight's tile.
6. For docs/rsprox-analysis/combat/defend_timing.md's open question: a splash does send the
   defend seq, delayed to its landing (11 of 11 here off his swing ticks; the brown bear in
   index 4384, 3 of 3).

**Test.** `tree.lua`'s `warlord.wire` row checks the fight's shape from the tick log: the
first swing at A+2, every gap 4, 401 with 2567 on each, 836 on the killing blow's own tick,
the del two ticks later with the smokepuff. It passes on the new pack (84 swings) and fails
on the pack this section started from (0 swings of 401, the death seq a tick late, no
smokepuff). The quest gate: 111 of 112, the only failure the `camelotTeleport.cast` row.

## The Khazard commanders

One recorded encounter: index 4394 [1294..1347], the only session that goes into the
stronghold. The ground floor's commander (npc index 14493, spawn 2509,3254) and the first
floor's (14616, spawn 2503,3255); `search npc 4972 --any` adds 3909 (rev 236, two adds at
2511,3254, never near) and 1682 (rev 226, two wander trips); 820 is a false hit. The
recorder (combat 32, staff of earth, auto-retaliate off: never swings back) crawls over
the rubble at C = [1309], climbs the ladder at U = [1319], opens the chest (the "Oi!" at
X = [1326]), searches [1328], goes down at D = [1335] and out the door [1340]; Protect
from Melee from [1312]. Nobody kills either commander. Ours: `test/quests/tree.lua` on
private packs (`build/quest_gate/tree_commander/ticklog.tsv`; C 325, U 339, X 350, D 361)
and an uncommitted scratch test (150 ticks outside the outer wall on the recorder's tile,
24 standing beside him, the recorder's way to the ladder, 20 at the ladder, up, killing
the first-floor one). "Before" is the content this section started from.

Content: the `[khazard_commander]` block of `quest_tree.npc` (huntmode, huntrange,
maxrange, wanderrange, death_sound; those five keys deleted from his
`server_fields.generated.npc` block), `quests/quest_tree/scripts/khazard_commander.rs2`
(new: the hunt's line-of-sight gate), the wall and chest of `quest_tree_locs.rs2`. This
supersedes the stronghold table's commander row above (`npc_setmode(opplayer2)`,
`p_delay(0)`, the say) and the Open bullet "The Khazard commanders' fight in the
stronghold: not audited yet".

### Timeline

| tick | recorded (4394) | before | ours | verdict |
|---|---|---|---|---|
| [938..950] | the player 1..2 tiles off the ground one for 13 ticks across the outer wall, no fight: no face, no step | no hunt at all | not taken (scratch: 77 ticks within 2, 0 faces; the same pack without the gate: taken on the first tick, held 154) | fixed |
| C | `human_walk_crumbledwall` (840) delay 30, exact move 2509,3252 -> 2509,3254 30..100; no `loc_merge` (0 in all of 4394) | the same plus a LOC_MERGE (zone sub-packet 12, TRACE_OUT=2) | the same, no LOC_MERGE | fixed |
| C+1 | the ground one, 2 tiles off: `[facing]` the player, a step and `human_spear_spike` (428) + synth `staff_stab` (2562) on the one tick; the player's block seq | nothing: the wall's opplayer2 latched, the swing at C+2, `[facing]` reset after it | taken on C+1 (face, target); swings C+1 when already beside him (scratch), C+3 in the quest run (he stood on the landing tile) | fixed; the same-tick step and swing OPEN |
| C+2 | the player's walk north onto 2509,3255; "Don't even think of taking the orb!"; the splat (2) + synth 520 delay 20 | the say; the player stayed on 2509,3254 | the walk step and the say | fixed |
| C+3..C+7 | the chase, a face per change of angle; steps 2508,3255 and 2507,3256; `[facing] type=reset` the turn after the player reached 2503,3255 | mode none after the one swing: he never moved again | scratch, the recorder's path: the same two steps and the reset on the same turn ([190] [191] [192]) | fixed |
| C+8, C+9 | two steps straight back to the spawn | | one step, then the wander roll 27 ticks later | OPEN (engine) |
| U+1 | the first-floor one, 2 tiles off on arrival: face, step, swing on one tick | nothing until the chest: taken at X+1 by `~npc_retaliate` | taken on the first turn within 2 (U+5, the bot's way to the chest), the swing a tick later | fixed |
| X | "Oi! You! Get out of there." with the open chest | same | same (`npc_attackplayer`: a no-op, he holds the player) | matched |
| U+6, U+10, U+15 | swings (gaps 5, 4, 5: each 5 a step first) | one swing (U+16), then the face reset and mode none | gaps 4, 4, 7 (the 7 a chase) | fixed |
| [1326] [1311] [1335] | the splat waits for the player's delay (the chest's lid, the crawl, the ladder) | | the splat on the swing | OPEN (shared combat) |
| [1336..1342] | the ground one leaves the player be: in combat ([1335]), single-way | | | engine, not run |

### Npc info, every block kind

| kind | recorded | before | ours | verdict |
|---|---|---|---|---|
| `[facing]` | 13: the player on the hunt and on each change of fallback angle (13/13 fit the npc's tile after its step against the player's before his), 1 reset (the leash) | 6: errand faces, a reset after each errand swing | 8: the hunt, angle re-sends, the leash reset | fixed |
| `[sequence]` | 428 x 5 | 428 x 2 | 428 x 5 | fixed |
| `[say]` | 2 | 2 | 2 | matched |
| movement | walk only, 45 steps, 11 lives | | walk | matched |
| `[hit]` `[headbar]` `[bas]` `[body_customisation]` `[transformation]` `level_change` `enabled_ops` | none (nobody hits them) | none | none | matched |

`npc_block_inventory.py`: no recorded kind we never send; `multinpc_stance_audit.py`: no
multinpc.

### The hunt, the leash and the wander

- **huntmode aggressive, huntrange 2.** Both take the player unprompted on the first npc
  turn he is in their room, 2 tiles off, with no op in between (2/2). 2 is the least both
  need; nothing recorded bounds it above. LostCity has `cowardly`, huntrange 1; the wiki
  infobox "aggressive = No".
- **Line of sight** (`[ai_hunt,khazard_commander]`; LostCity's `cowardly` is
  check_vis=lineofsight; our engine's hunt measures distance only). The south wall blocks
  it (blockrange); the room's west wall (`crumblywall2`, blockrange=0) does not, and upstairs
  the cannons and cannonballs block some 2-tile diagonals (scratch: 2501,3255 to 2503,3253
  declined 14 ticks): unrecorded.
- **maxrange 4.** The release at [1316] fits the leash (past maxrange + 1 = 5 from the spawn)
  with 3 or 4; LostCity's is 4. Ours releases on the same turn on the same path.
- **wanderrange 3.** 14 wander trips (4394, 1682) end at most 3 tiles from the spawn
  (2506..2512, 3254..3257); LostCity's 2 is too small. Ours: the same extents (scratch, 12
  trips).

### Sounds

| recorded | n | ours | verdict |
|---|---|---|---|
| synth 2562 `staff_stab`, each swing | 5/5 | 5/5 (scratch 9/9) | matched (generated) |
| synth 511 `human_block_1` delay 20, each 0 | 4/4 | 5/5 | engine, matched |
| synth 520 `human_hit_3` delay 20, the 2 | 1/1 | no nonzero in ours | engine |
| 52 chest_open, 46, 2676 / 2663 | | | the chest (matched), the door, the recorder's prayer |
| death | unrecorded | `human_death` (512) on the death seq (scratch) | was silent; the warlord's and the battlefield's human death |
| defend | unrecorded | `human_spear_block` (430) + `blade2` (29) (scratch) | generated (LostCity's), unrecorded |

### Damage, accuracy and every splat (skill sections 13, 13b)

- 5 swings, 5 splats: [1311] 2 unprayed (swung [1310], before Protect from Melee); [1320],
  [1326], [1329], [1335] 0 under Protect from Melee (4/4).
- Max hit 5 (the wiki; str 45, +0: floor(0.5 + 54 x 64 / 640)); the recorded 2 (n = 1) tests
  nothing. Accuracy: the recorder's stab defence roll (1 + 8) x (3 + 2 + 64) = 621 against
  (50 + 9) x 64 = 3776, P(hit) 0.92; 1/1. Not identifiable. Levels: the cache's 50/50/45/22;
  `check_npc_cache_levels.py` 0.
- `rsprox_hit_causes.py --npc khazard_commander --by-prayer`: recorded `@1:adj` 3 (the splats
  that waited for the delay), `@0:adj` 1, `@0:near` 1 (the step and swing from 2 tiles);
  ours `@0:adj` 5 (0..0: the bot's defence roll 27216, P(hit) 0.07). No `none`, no heal. The
  two REC-ONLY keys are Open 1 and 2.

### Unrecorded

The death (scratch: the engine's default, seq on K+1, bones, del K+3; the warlord's
recorded K / K+2), the respawn (150, the wiki), the wiki's tertiary orb drop (not built),
and the chest taking a player the hunt has not (`npc_attackplayer`).

### Open (engine or shared code, not changed here)

1. **Step and swing on one tick:** 4/4 recorded ([1310] [1320] [1325] [1334]); ours steps and
   swings a tick later (`ToriRSServer_CombatNpcTick` returns after the approach; LostCity's
   `aiMode` tries the interaction again after moving). The test's row checks the take, not
   the first swing, for this reason.
2. **A hit on a delayed player:** the splat and its synth wait for the delay's end (3/3);
   ours lands on the swing (`~playerhit_n_melee_apply`).
3. **After the leash:** straight back to the spawn (1/1); ours waits for the wander roll.
4. **Wander trips:** recorded 1..4 steps (14, mean 2.4); ours 1..6 (12, 3 of 5+, mean 3.1).
5. **"Too strong" for a hunt** uses our melee-only combat level: the test bot is 82 to us,
   97 by OSRS's formula (over 2 x 48, never hunted there).

**Test.** `tree.lua`'s `commander.wire` row: each commander taken on the first turn the
player is within 2 (the first-floor one by the "Oi!" at the latest), not through the outer
wall, every swing with 2562 and 4+ apart, the ground one let go on the leash's turn, the
first-floor one not before the ladder down, and the step and the line at C+2. It passes on
the new pack and fails on the old (the ground one never taken, the first-floor one taken
after the "Oi!" and let go upstairs, no step at C+2). The quest gate: 112 of 113, the only
failure the `camelotTeleport.cast` row; every row that passed before passes.
