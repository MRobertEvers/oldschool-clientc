# Lost City — RSProx index 4591

Recorded 2026-11-17, rev 242.1, world 407, one session end to end (1,353 ticks), at combat
level 71 (Woodcutting 56, Crafting 38, Prayer 70, Magic for Crumble Undead and Water Blast).
The session covers:

- the warrior's whole tree, four times over [87..246], and the start [229..239];
- the other three adventurers at stages 1 and 2 [252..466];
- the leprechaun tree and Shamus, four lives [149..484] (the first another player's);
- a Lumbridge bank trip [497..602], the boat to Entrana [635..666], the island [663..733];
- the cave monk and the one-way ladder [733..761];
- the Entrana dungeon: zombies by spell [780..1038], the axe they drop [1019..1047];
- the Dramen tree and the tree spirit [1044..1139];
- the magic door to the Wilderness [1161..1162] and a Lumbridge Teleport out [1216..1219];
- a knife, the staff [1231..1246];
- the shed door, five times, and Zanaris [1319..1339]; the scroll and the rating [1339..1346].

The journal was opened at stages 0 (the overview) [56], 1 [260], 2 [407] [643] [684], 4 [1133]
and 5 [1294].

Before this port the quest was a LostCity port (2004). Its pages were nearly all Jagex's text
already; what was wrong was everything around them:

- the warrior's menus ("camped here", "Please tell me?", one "There's no such thing!" too many), no start confirm, no "You've started a new quest" line;
- forced `|` breaks in Shamus's and the warrior's pages;
- Shamus walking a fixed ten tiles east on every spawn;
- the tree spirit's death writing stage 3 and its mesbox at once; the Dramen tree's chop with a sound and the wrong no-axe line;
- the corpse gone 2 ticks after its death seq (4 recorded); five zombie types swinging the wrong seqs, four of them silent, gone 5 ticks after their death seq (7) and back after 30 (34);
- the magic door facing the player north after the jump, with no sound;
- the shed door a plain teleport (a cast, `teleport_casting`) to 2452,4472, the completion queued, a scroll with a coin and no rewards past the points;
- a 2004 journal, and no not-started overview text.

**Ours:**

- `quests/quest_zanaris/`:
  - `scripts/leprechaun_tree.rs2`: the leprechaun tree, the Dramen tree, the staff, the shed door and the completion;
  - `scripts/shamus.rs2`, `scripts/tree_spirit.rs2`, `scripts/zanaris_journal.rs2`;
  - `configs/quest_zanaris.npc` (the spirit's death delay, the zombies), `configs/quest_zanaris.varp` (the spirit-killed mark).
- `areas/lumbridge/scripts/zanaris_camp.rs2` (the four adventurers).
- `areas/entrana/scripts/{cave_monk,entrana_dungeon}.rs2` (the monk, the ladder, the magic door).
- `interface_questjournal/scripts/quest_overview_start.rs2` (the overview's start text).

**Test:** `test/quests/zanaris.lua` follows the recorded flow (the warrior's recorded path with the confirm,
Shamus's "How does it fit in a shed then?" branch, Take-boat, the tree click after the kill that
writes stage 3, the fairy shed door). Results in §Tests.

## 1. Every action in the recording

`rsprox_actions.py`: 82 distinct actions.

| tick | action | verdict |
|---|---|---|
| 2, 12, 56, 77, 262, 315, 1029, 1120.., 1224, 1317, 1344..1352 | welcome screen, worn tab, quest list, overview/journal close, store orb, prayer tab/prayers, side stones, rating, logout | engine/UI. The overview's start text **added** (§7) |
| 19, 29 (x12), 1216 | Varrock and Lumbridge Teleport | generic spell (`teleport.rs2`); the teleport landing frame (sides re-opened) is not sent by ours: **Open** |
| 87 (x8) | Warrior | **this content** (§2) |
| 251 (x3), 314 | tree2 chop, "You get some logs." | generic woodcutting, the recorder's own |
| 274, 285, 292 | Monk, Archer, Wizard | **this content** (§2) |
| 316 (x7) | leprechaun tree | **this content** (§3) |
| 338 | pot_empty pickup | generic pickup |
| 339 (x5) | Shamus | **this content** (§3) |
| 497..602 | bank booth, deposits, withdrawals, tabs, search, rune pouch | generic bank, the recorder's kit |
| 599, 602, 729, 771, 1249, 1257 | stamina, rune pouch, bury bones, prayer restore | generic consumables, the recorder's kit |
| 635 (x3) | Take-boat (shipmonk1_c) | generic: `monk_of_entrana.rs2` + `~ship_journey`, already from 4257/4435: line and boarding [644], deck and mesbox [658] (14 ticks). Matched |
| 663 | ship_from_entrana_off | generic gangplank (`gangplank.rs2`, timed from RSProx f00bdacb8^): [666] |
| 702, 722, 724 | fire_bird attack, hot_feather, bones | Entrana wildlife and pickups, generic |
| 733 (x3), 734 | cave monk, entranaladdertop | **this content** (§4) |
| 780..1061 | Crumble Undead / Water Blast on zombies and the spirit | the recorder's spells; the zombies are **this content** (§5) |
| 1021 (x2), 1043, 1047 | bronze_axe pickup / drop / pickup | generic ground items; the axe is the zombies' drop (`drop_tables`) |
| 1044 (x9) | dramentree | **this content** (§6) |
| 1052..1069 | tree spirit attack | **this content** (§6) |
| 1161 | zanarismagicdoor | **this content** (§4) |
| 1227 | spiralstairsbottom_3 (Lumbridge castle) | generic stairs |
| 1231 | knife pickup (3205,3212: an `m50_50.spawn` row) | generic, matched |
| 1241 | knife on dramen_branch | **this content** (§6) |
| 1246, 1318, 1329 | Wield/remove dramen_staff | generic equip: `combat_weapon_category` 2, "Category: Blunt", `equip_staff` |
| 1300 (x6) | giant rats | swamp wildlife, generic |
| 1319 (x5) | zanarisdoor | **this content** (§8) |

**Teleports:**

| tick | from -> to | cause | verdict |
|---|---|---|---|
| 22, 33, 1219 | spell landings | Varrock / Lumbridge Teleport | generic |
| 644, 658 | Port Sarim -> boat -> Entrana deck 2834,3331,1 | Take-boat | generic, matched |
| 666 | deck -> 2834,3335,0 | gangplank | generic |
| 761 | 2823,3374 -> 2822,9774 | the cave monk's "risk I will have to take" | **this content**, matched (§4) |
| 1162 | 2873,9750 -> 3250,3772 | zanarismagicdoor | **this content**, fixed (§4) |
| 1338 | 3202,3169 -> 2452,4473 | the shed door with the staff | **this content**, rebuilt (§8) |

## 1b. Cutscenes

`grep cutscene_status|cam_*|fade_overlay|minimap_toggle|midi_jingle|hidenpcops`: no scene. The
`cam_reset` + `minimap_toggle 0` lines at [22], [33], [1219] are the spell landings' frame; at
[1334] the same frame goes out with the shed's shimmer line (§8). `docs/quests/CUTSCENES.tsv`
row: none (no scene).

## 2. The adventurers (4591 [87..466])

Every recorded page was checked mechanically (text with `<br>` as a space, and the mood from the
`if_setanim`) against the scripts: every distinct page is in ours verbatim with its mood.
The menus were not:

| R | ours before | now |
|---|---|---|
| [89] "What are you camped out here for?" (the line after it: "What are you camped here for?" [129]) | "What are you camped here for?" | fixed |
| [141] Who's Zanaris -> "There's no such thing." | "!" | fixed (the out-here branch keeps "!" [211]) |
| [96], [221] "Please tell me." | "Please tell me?" | fixed (the player still says "Please tell me?" [99]) |
| [229] after "Of course we know!": `Start the Lost City quest?` Yes./No. | none | **added** (`~p_choice2_header`) |
| [239] stage 1 and "You've started a new quest: @q_mes@Lost City</col>" with the click of "So a leprechaun knows..." | stage only | line **added** |

Forced `|` breaks deleted from every chat line (the layout wraps at 380 px as Jagex does).
The stage-1 and stage-2 branches of all four (monk "I already told you." at 2 [399], archer's
"novice like YOU" at 2 [425], wizard "cabbage" at 2 [462], warrior "anything yet?" at 2 [446])
match. Unrecorded and kept as LostCity's: the non-member lines, "No." at the confirm, the
warrior at stage 6.

Movement: the four wander inside their spawns ±5 (warrior 3144..3152 x 3203..3212 on 3149,3207;
archer 3145..3154 x 3200..3210 on 3149,3205; monk 3146..3155 x 3204..3211 on 3151,3207; wizard
3148..3157 x 3200..3209 on 3152,3205): the generated `wander 5`, matched.

## 3. The leprechaun tree and Shamus (4591 [149..484], index 3158 [193..406])

| | R | ours before | now |
|---|---|---|---|
| op, no Shamus | L+1: Shamus `[add]` on 3139,3211 facing south, no line, no seq ([323], [406], [420]; 3158 [195], [399]) | add + a fixed 10-tile walk east | add, then a walk to a random tile within 15 of the spawn |
| his first move | the tick after the add, in 6/6 lives: 15 tiles east to 3154,3211 twice, to 3150,3217, to 3136,3217, short wanders twice | always 3149,3211 | fitted |
| op, Shamus out | the page "Hey! Yer big elephant!..." (chatang2) at C+1 [328], [336] | same | matched |
| talk | pages [350..386], stage 2 with "The monks are running a ship..." click ([386]) | same | matched; "...How does it fit" fixed, `|` breaks deleted |
| leaving | the mesbox's continue: `[del]` [397], [419]; an op on him before it leaves him standing ([386] -> [388]) | `~mesbox` then `npc_del` | matched |
| no talk | gone after 100 ticks (3158 [195] -> [295]); 64 in 4591 [420] -> [484] with the player walking off | `npc_add(.., 100)` | 100 kept; the 64 is **Open** |

Lives' paths (4591): [149] another player's (Amb86 on 3139,3211), N/W to 3136,3217;
[323] east x15; [406] W, W, N, S, E; [420] east x15, wander back west to 3141,3211.

## 4. The cave monk, the ladder and the magic door

| | R | ours | verdict |
|---|---|---|---|
| ladder op | the monk's three pages, the menu ([739..745]); the monk faces the player [740] | `@cave_monk_chat` | matched |
| "I don't think I'm strong enough" | page, close ([749..750]) | same | matched |
| "risk I will have to take" | page [760]; its click: "You climb down the ladder.", teleport 2822,9774, Prayer 70 -> 4 (`stat_sub(prayer, 0, 95)`) [761] | same | matched |
| magic door | L+1: "You feel the world around you dissolve...", teleport 3250,3772, `synth door_open` [1162]; no face after | the line, the jump, a face north, no sound | **fixed** |

## 5. The Entrana zombies (4591 [769..1156], 15 lives over the 5 types)

| | R | ours before | now |
|---|---|---|---|
| attack seq | `zombie_update_attack_weapon` (24) | `zombie_update_attack_normal` | fixed |
| defend seq | `zombie_update_defend_weapon` (impact delay) | `_defend_normal` | fixed |
| death seq | `zombie_update_death_weapon` (8) | `_death_normal` | fixed |
| sounds | `zombie_attack` 918 x30, `zombie_hit` 923 x44, `zombie_death` 922 x8, all types | 918/923/922 on zombie_entranan, **null on 2..5** | fixed (all five) |
| death seq -> del | 7 ticks, 8/8 | `death_delay=5` | 7 |
| respawn | 34 ticks after the del, 5/5 ([842]->[876], [891]->[925], [910]->[944], [967]->[1001], [989]->[1023]) | 30 | 34 |
| movement | wander, 1 tile a tick | generated wander 5 | matched by kind |
| damage | 23 swings on the recorder, 21 zeros, max 2 (level 71, prayer up for some) | cache stats | too few to fit: **Open** |

## 6. The Dramen tree, the spirit and the staff (4591 [1044..1246])

| tick | R | ours before | now |
|---|---|---|---|
| [1044] | no usable axe: "You need an axe to chop down this tree.<br>You do not have an axe that you have the Woodcutting level to use." | the skill's "You do not have an axe which you have the level to use." | **fixed** (the tree's own line; the skill's generic line is a third text, see Open) |
| [1049] | L+1: `tree_spirit` added on 2860,9737 facing south, `[say]` "You must defeat me before touching the tree!" in the add; no player seq | same | matched |
| [1050..1052] | the spirit steps and faces the player; first swing [1052] (`ghost_update_normal_attack`, `ghost_attack`; `ghost_hit` on its hits; `ghost_update_normal_defend` with the impact delay) | npc_anims.generated | matched by config (Tests) |
| [1117] -> [1118] -> [1122] | the killing hit; the death seq + `ghost_death` + `enabled_ops 0`; gone | death_delay 2 | **4** |
| [1121] -> [1122] | the next tree op (L+1): stage 3 and the mesbox "With the Tree Spirit defeated you can now chop the tree." | stage 3 and the mesbox at the kill, from the death trigger | **rebuilt**: the death leaves a server-side mark (`%varp6998_zanaris_spirit_killed`), the op writes the stage |
| [1125] | the mesbox's continue: `human_woodcutting_bronze_axe` | — | the script runs on into the chop |
| [1127] | "You cut a branch from the Dramen tree.", stage 4, the branch, `[sequence] -1`; no sound, no xp | a `woodchop_4` synth, the branch on O | rebuilt: seq on O, the rest on O+2 ([1130]->[1132], [1137]->[1139], 3/3) |
| [1241] | knife on branch, on the click while running: "You carve the branch into a staff.", stage 5, the swap; no seq, sound or xp | same | matched |

Damage (§13): the spirit swung 15 times and every splat was 0 (13 under Protect from Melee, 2
misses unprayed). Nothing to fit; its stats are the cache's (`quest_zanaris.npc`): **Open**.

## 7. The journal

Not started: the overview, clientscript 6821 with "talking to the <col=800000>Adventurers<col=000080> in their camp in <col=800000>Lumbridge Swamp<col=000080>." [56] — ours had no row in
`quest_overview_start.rs2` (it fell back to "speaking to ...") and `~zanaris_journal` drew a 2004
page instead of the overview: both fixed.

Started: rows sent exactly as Jagex's (`~quest_journal_rows`), stray tags included (`<str><str>Lumbridge`, `nearby <col=000080>who`, `<col=800000><col=800000>Swamp`):

| stage | recorded | ours |
|---|---|---|
| 1 | [260], 4 rows | verbatim |
| 2 | [407], [643], [684], 6 rows | verbatim |
| 3 | not opened | the stage-4 rows 1..6 + LostCity's current step: UNVERIFIED |
| 4 | [1133], 7 rows | verbatim (LostCity had an extra "I cut a branch" row) |
| 5 | [1294], 9 rows | verbatim |
| 6 | not opened | rows 1..7 + LostCity's completed paragraph struck + QUEST COMPLETE!: UNVERIFIED |

## 8. The shed door and the completion (4591 [1319..1339])

The door never swings, staff or not, both ways ([1320], [1324], [1328], [1331]): on the op
tick the door's slot becomes `inviswall` (rot 0), `zanarisdoor_open` stands on the tile outside
(3201,3169, rot 1), `synth door_open`, and the player is stepped across; both back two ticks
later. Ours was a straight `p_teleport` with no locs: **rebuilt** (`~zanaris_walk_door`, the
Holy Grail shape).

With the staff wielded, from outside ([1334]):

| tick | R | ours before | now |
|---|---|---|---|
| O [1334] | "The world starts to shimmer...", the step onto the door's tile (no door), `cam_reset`, side panels 1, 2, 4, 5, 6, 9, 8, 10..13 re-opened, `minimap_toggle 0` | the line | all of it (`~zanaris_shed_hud`) |
| O+1 [1335] | `human_fairy_vanish` delay 30, `fairy_flower_ring`, `synth ft_fairy_teleport` | a teleport cast (`human_castteleport`, `teleport_casting`, `teleport_all`) | rebuilt |
| O+4 [1338] | teleport 2452,4473 + `human_fairy_appear` | 2452,4472 | rebuilt |
| O+5 [1339] | stage 6, qp +3, the line, disease -50/0, the jingle `quest complete 2`, the scroll: dramen staff at 78,128, "3 Quest Points", "Access to Zanaris", "1,500 Woodcutting XP", "1,500 Crafting XP"; 1,500 xp in each | a queued completion with a coin, the points only, no xp | rebuilt |

## 9. Vars

| var | R | ours |
|---|---|---|
| varp 147 `zanaris` 0..6 | [239] 1, [386] 2, [1122] 3, [1127] 4, [1241] 5, [1339] 6 | `perm`, `transmit=yes`; same ticks (Tests) |
| `last_quest_id`, `last_started_quest_*`, `last_completed_quest_*` (16741.., 16760.., 16767..) | with every stage write | not sent by any quest here: **Open** (shared) |
| `%varp6998_zanaris_spirit_killed` | — | authored, `perm`, `transmit=no` |

## Tests

`python3 tools/quest_gate/run.py zanaris` (its own `build_questtest` binary, the shared packs
rebuilt from this tree), `TORIRSSERVER_TRACE_OUT=2`: **52 of 52 rows pass** (2026-11-17). The
first run of the rebuilt quest failed one row: the test read stage 3 after the mesbox's continue,
when the branch had already written stage 4. That is the recorded behaviour, so the test now
reads it with the mesbox up.

The wire audit, on that run's tick log and packet trace:

| check | result |
|---|---|
| pages, menus, mesboxes, journal rows, the scroll (`IF_SETTEXT` text prefix + byte length vs the recording, so the `<br>` count is checked) | 65 of 65 that the test path reaches match; the other 46 are branches and journal stages the path never opens (the warrior's other loops, the stage-2 lines, the journal at 1/2/4/5) |
| stage writes (varp 147) | 1 with the "So a leprechaun knows" click, 2 with Shamus's last page, 3 with the tree op after the kill (the mark written on the corpse tick, gone in 5 from the hit like [1117]->[1122]), 4 two ticks after the mesbox's continue, 5 on the knife, 6 at O+5 of the shed op |
| Shamus | added on the op, first step the next tick, one tile a tick, gone on the mesbox's continue |
| tree spirit | the say in the add; swings every 4 ticks (R: [1052], [1056] .. [1116]); seqs 5532/5533/5534 and sounds 436/439/438 as recorded; hit D, death seq and `ghost_death` D+1, gone D+5 (R 1117/1118/1122); `enabled_ops 0` at death |
| Entrana zombie (the one the test kills) | seqs 5571/5574/5575 (R the same), sounds 918/923/922, hit D -> gone D+8 (R: death seq +7) |
| the chop | `human_woodcutting_bronze_axe` on the continue, the branch and `[sequence] -1` two ticks later |
| the shed | op O: step onto 3202,3169; O+1 3265 delay 30, spotanim 569, synth 1098; O+4 2452,4473 + 3266; O+5 stage 6, jingle 153 (silence before it) |
| npc block kinds (`npc_block_inventory.py`) | every recorded kind sent except the spotanims on the spirit and zombies, which are the recorder's Water Blast / Crumble Undead impacts (our bot fights in melee) |

The test stands where the recorder stood for the tree (north of it, 2861,9736: [1049]); from
the south the spirit had to walk round and swung 6 ticks after its add instead of 3.

The journal is checked headless only at stage 6, whose shared rows (1..7) match the recording;
the recorded stages 1, 2, 4 and 5 need the live client (`test/quests/_journals.lua` has no Lost
City rows yet).

## Open

- **The teleport frame.** Jagex re-opens side panels 1, 2 (with the quest list), 4, 5, 6, 8..13, sends `cam_reset` and `minimap_toggle 0` with every spell landing ([22], [33], [1219]) and with the shed's shimmer ([1334]). Ours sends it only at the shed (`~zanaris_shed_hud`); `~p_telejump_safe` sends `cam_reset` alone. Shared teleport code.
- **Region music waits for the player.** At Zanaris Jagex plays silence + the jingle [1339] and starts Crystal Cave only when the player is released after the scroll and rating ([1346]); ours starts the region track on the arrival tick, over the jingle. Shared music engine.
- **`last_quest_id` / `last_started_quest_*` / `last_completed_quest_*` varbits** (16741, 16760..16763, 16767..16773) go out with every stage write, start and completion; no quest in our tree sends them.
- **The skill's no-axe line.** Index 4471 (rev 242) sends "You need an axe to chop down this tree.<br>You do not have an axe which you have the woodcutting level to use." for an ordinary tree; `~woodcutting_axe_checker` prints the older "You do not have an axe which you have the level to use." (one rev-232 session, 3157). The Dramen tree has its own third wording, now in its script.
- **Shamus with no talk** leaves after 100 ticks in index 3158; in 4591 [420] -> [484] he left after 64 with the recorder walking off. One more recording would decide whether distance or a timer removes him.
- **Damage.** The spirit's 15 swings all hit 0 (13 prayed) and the zombies' 23 hit 0..2 on a level-71 recorder: nothing to fit a max or accuracy from. Stats are the cache's.
- **Unrecorded and kept from LostCity:** the aura line before Shamus, the Woodcutting 36 and Crafting 31 refusals, a full backpack at the tree, a herring on the leprechaun tree, "No." at the start confirm, the non-member lines, "I've been in that shed", the warrior at stage 6, the journal at stages 3 and 6, the spirit clicked while still dying (it says its line again).
- `tools/quest_gate/lint_quest.py` currently aborts on any file: `ge_newoffer_price` is bound to both varp 5753 and 9507.
