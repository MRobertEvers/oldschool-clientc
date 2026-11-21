# Desert Treasure I: the Smoke Dungeon (section "smoke")

Sources: RSProx 4623 [1250..2135] (rev 242, normal world, the whole dungeon run), 4627 [900..1800]
(the second visit, after the kill). Content: `quests/quest_deserttreasure/scripts/dt_smoke.rs2`,
constants in `configs/deserttreasure.constant`, one line in `player/logout.rs2`. The fight is `dt_fareed.rs2`
(not touched). Nothing was run: no server, client or quest gate (shared tree); only the private compile.

## 1. Action inventory (every distinct click of the dungeon windows)

| R (session [tick]) | Ours before | Ours now | Verdict |
|---|---|---|---|
| 4623 [1253] x3 oploc1 `sword_haunted_well`; [1260] first answer: 2 player pages, `fd_fw_warningspam` 0->1 on the same tick | a mes line the recording never has | 2 `~chatplayer` pages, varbit written with the first, then return; next click climbs | rebuilt. The click at [1253] (arrival [1257]) answered nothing: a tab click at [1258] intervened; the dialogue is on the click at [1260] |
| 4623 [1273], 4627 [1107], [1589]: climb | `~climb_ladder(-1)` (ladder anim) | `mes("You climb down the well.")` + `~climb(-1)` (maplink to 3206,9379), no anim; teleport on arrival + 1 (4627) or the click tick when already adjacent (4623) | rebuilt |
| 4623 [1269] oploc2 `Smoke`: menu "Do you want to turn the smoke overlay off?" / "Yes, hide the smoke overlay.\|No, leave it on." | not bound | bound, menu verbatim; Yes/No set `%varb298_slayer_smoke_toggle` | menu recorded; **answer unrecorded** (the next click at [1273] closed it): Open |
| 4623 [1274], 4627 [1108], [1590]: `if_opensub overlay_atmosphere smokeoverlay (313)` one tick after landing; closed with the exit teleport (4627 [1252]) | none | `softtimer dt_smoke_overlay` (+1) opens it, `dt_smoke_watch` (1 tick) closes it when out of the dungeon | built; closing on the exact tick is by a 1-tick watcher, not the teleport itself |
| 4623 [1299] oploc6 `4d_standing_torch3_unlit`, [1677] `_lit`: "An old mystical torch." | generic examine | generic | ok |
| 4623 [1305],[1483],[1528],[1618] oploct tinderbox on the torch (parent id `4d_standing_torchN`); effect at arrival + 1: varbit 0->1, "You light the torch.", synth `fire_lit`; the fourth also "The path is lit, now claim the key..." on the same tick; no animation, no delay | "You attempt to light...", `p_delay(2)`, a Firemaking roll, "You fail to light" (none recorded) | one tick, varbit, line, synth, timer; level 50 kept (wiki) with `~displaymessage` as its unrecorded answer | rebuilt; 5/5 recorded attempts succeed |
| burn-out: lit [1306] out [1807]; [1486]->[1987]; [1529]->[2030]; [1623]->[2124] = **501 ticks** each; lines "The South-west / South-east / North-east / North-west torch burns out..." | 250 ticks | `^dt_torch_burn_ticks = 501` | fitted |
| relight [1829] (torch 3 again), "The path is lit..." with the other three still burning | same proc | ok | ok |
| logout [2135]: torch 3 (due [2330]) goes out with its line on the logout tick | none (torches stuck lit across a relog) | `~dt_smoke_on_logout` from `player/logout.rs2`: every lit torch out, same lines | built |
| 4623 [1342]-[1347] oploc1 `fd_firedungeon_shutchest`, torches out: message box "There seems to be no way to open this chest. Engraved where the keyhole should be is a message; 'Light the path, and find the key...'" | box with `\|` and ":" | verbatim | fixed |
| 4623 [1853]->[1854]..[1857]: chest_open synth + `fd_firechest` 0->1 at arrival + 1; at +3 `human_lockedchest` anim, `fd_firedungeon_openchest` (shape 10, rot 3) for one tick, key (slot 12), "You open the chest and take a key." | different order, other lines ("You find another warm key...") | `sound; varbit; p_delay(1); anim; loc_change(open,1); inv_add; mes` | rebuilt |
| 4627 [1128]-[1136]: after the kill, torches all out, `fd_firechest` already 1: the same shape, the **same line**, a key again | "The chest is empty." unless lost | always gives a key once the hunt is open (the box only while the varbit is 0 and a torch is out) | rebuilt; gives another key even if one is held: unrecorded, wiki only says a lost key returns |
| 4623 [1360] tinderbox on the chest: "Nothing interesting happens." (engine) | n/a | default | ok |
| 4623 [1432]-[1437],[1875] tinderbox on shark / glory / hat, key on tinderbox: "Nothing interesting happens." | | generic default | ok |
| 4623 [1423],[1454],[1456] gate, no key: message box "This gate is locked." (type mesbox) | `mes("The gate is locked.")` | `~mesbox("This gate is locked.")` | fixed |
| 4623 [1955] gate with key: key removed (slot 12), `fd_firewargate` 0->1, "You unlock the gate and enter the room.", then the open gate (below) | key and varp 6613 | same, varbit | rebuilt |
| gate opening, every press in 4623 [1955] and 4627 [1223]-[1752]: `inviswall` in both closed tiles (3305,9376) (3305,9375), `inacmetalgateopenr (1574)` at (3304,9376) rot 1, `inacmetalgateopenl (1573)` at (3304,9375) rot 3, synth `gate_open`, both for 2 ticks; the player crosses one tile on the open tick (3304->3305 entering, 3305->3304 leaving) | `p_teleport` pair | `dt_smoke_gate_cross`, same loc set (`^dt_fw_gate_*`, `^dt_fw_open_*`); crossing by `p_teleport` (the recording walks) | rebuilt, crossing shape approximated |
| 4627 [1712]/[1747] drop of `fd_diamond_fire` (inventory op 7) at the gate; [1716] and [1749] on the next entering: obj at (3316,9377) (private, despawn 2000, never public) and "The Diamond of Smoke seems to have mystically found its way back here..." at gate + 1. No second copy on [1724],[1727],[1752] while one lies in the chamber, none when carried ([1744]) | message + obj every pass | on entering, after the kill: only if the diamond is not carried, not banked and not already on `^dt_diamond_return` | rebuilt; this **is** why the diamond was on the floor in 4627: the recorder dropped it on purpose to test the return |
| 4627 [1721],[1730] opobj3 Take the returned diamond; [1736] picked up | generic | generic | ok |
| 4627 [1223]-[1760] repeated gate presses after the kill: every press opens, no line, no key, no spawn | ours spawned Fareed again if `stage=keyed` | no key and no spawn once `fd_killed_firewarrior = 1` | rebuilt |
| 4623 [2096]-[2118] attack, [2117] last hit, [2118] `human_death` + `enabled_ops 0b00000`, [2120] hint arrow off, `fd_killed_firewarrior` 0->1, diamond into slot 19, "You take the Diamond of Smoke from the ashes of the warrior." | diamond dropped, other line | `[queue,dt_smoke_fareed_slain]` (hint_stop, varbit, direct add, the line) for the fight agent to queue | exposed; **not wired**: dt_fareed.rs2 is not mine |
| fight clicks (opnpc2 x27 Fareed, darts, wind/water blast, prayers, staff swaps, food) | | | recorder's own kit / the fight agent |

Generic rows inside the windows (one verdict each): Shantay Pass doorway `shantay_pass_henge_doorway` op1 (4623 [1115], 4627 [978]) bound
(`areas/area_alkharid/scripts/shantay_pass.rs2`); `shantay` op4 shop and the pass purchase "You purchase a Shantay Pass." bound (`shantay.rs2`);
`shantay_guard` op2 Attack (4623 [133]) is the recorder's attack, no content; `thbankchest` op1 (4623 [135], 4627 [899]) bound
(`shantay_chest.rs2`); `banker1_east` op3 and the bank screens bound (`bank.rs2`); `bankdoor_l` op1 (4623 [92]) not individually traced (door category);
`ge_clerk_1` op3 bound (`ge_offers.rs2`); glory / ring of dueling teleports (4623 [87], 4627 [1249]) bound, not traced;
Varrock Teleport (4623 [180]) generic; **magic carpet** `magic_carpet_seller1` op3 (4623 [1135], 4627 [985], [1378]): **defect, Open**: recorded
menu "Carpet rides cost 100 coins." with Uzer / Bedabin camp / Pollnivneach / Cancel; ours opens the talk-style two-option dialogue
(`areas/area_desert/scripts/magic_carpet.rs2`), and that string is in no script; the 20-odd-tile jumps of the ride (3-tile steps, 4623 [1148]..[1198]) are the carpet's own scene.

## 2. Teleports

| R | Cause | Ours | Verdict |
|---|---|---|---|
| 4623 [90] (3190,2986)->(3293,3163), [183] Varrock tele, [889] -> (3317,3233) Duel Arena | glory / spell / ring | generic | not mine |
| 4623 [1148]..[1198] carpet legs | magic carpet | generic carpet | see Open |
| 4623 [1273], 4627 [1107], [1589]: (3310,2961) -> (3206,9379) | the well | `~climb(-1)` through `maplink_0_51_46_46_17_down` | ok |
| 4627 [1252] (3304,9376) -> (3315,3233) | ring of dueling | generic | not mine |
| 4627 [1778] (3303,9376) -> (3214,3423) | Varrock Teleport | generic | not mine |

## 3. Scenes

None. No camera, fade, jingle or `cutscene_status` in the windows. The only interface events of the dungeon are the smoke overlay and the
chat boxes above. 4623 [1274] and 4627 [1108]: `You have unlocked a new music track: Bone Dry` (music unlock on entering the zone, generic).

## 4. Variables

| Varbit (id) | Recorded | Written by (ours) |
|---|---|---|
| `fd_fw_warningspam` (374) | 0->1 4623 [1260], with the first warning | the well |
| `fd_torch_count1..4` (360..363) | 1 on lighting, 0 after 501 ticks or at logout | `dt_light_standing_torch`, the four burn timers, `dt_smoke_on_logout` |
| `fd_firechest` (364) | 0->1 4623 [1854] (key taken) | the chest |
| `fd_firewargate` (386) | 0->1 4623 [1955] (key spent) | the gate |
| `fd_killed_firewarrior` (376) | 0->1 4623 [2120] | `[queue,dt_smoke_fareed_slain]` (fight agent calls it) |
| `current_hint_arrow` (14192) | 0->7 4623 [1956] (hint on Fareed), 7->0 [2120] | `hint_npc` / `hint_stop` |

Base varps 440/441/442: stated `perm`, `transmit=yes` in `deserttreasure.varp`; the generated block states only `wholewrite/wholeread` for them, so no key is stated twice.
Old -> new: `%varp6614_dt_smoke_stage` 0 -> `fd_firechest` 0; 1 (keyed) -> `fd_firechest` 1 and `fd_killed_firewarrior` 0; 100 -> `fd_killed_firewarrior` 1.
`%varp6613_dt_smoke_gate` 0/1 -> `fd_firewargate` 0/1. `%varp6612_dt_fareed_fighting` (temp spawn guard) is kept and still used by dt_fareed.rs2.
Still read by other files (they must migrate, then the varps can be left dead): `dt_journal.rs2` lines 53, 57, 127 (`varp6614`), `dt_fareed.rs2` line 6.

## 5. Pages (4623 [1250..2135] + 4627 [900..1800], Smoke part)

Recorded 5 distinct strings (4 chat/box pages, 1 menu). Before: 0 verbatim (the warning lines were different, the chest box used `|` and ":", the gate line said
"The gate is locked." as a `mes`, the menu was absent). Now: 5 verbatim, 0 wiki. The Shantay/carpet strings of 4627 are generic (see Open).

## 6. What the gate does on the wire up to Fareed's first tick (for the fight agent)

4623, gate pressed with the key from (3304,9376):

- [1955] (T): click; `fd_firewargate` 0->1; "You unlock the gate and enter the room."; key removed; the gate loc swap (section 1); the player steps to (3305,9376); synth `gate_open (67)`.
- [1956] (T+1): `[add]` `firediamond_firewarrior (3456)` at (3309,9377), spawn angle south, with a **`[say]` "You dare trespass in my realm?"** in the same add block; `current_hint_arrow` 0->7 and `hint_arrow` reset then `hint_arrow npc` on him.
- [1957] (T+2): he walks (3309,9377)->(3308,9376) (south-west), faces the player; the closed gate locs return; [1958] (3307,9376), [1959] (3306,9376); the player takes a `failedspell_impact` spotanim at [1959] (his ranged/ash effect, the fight's); [1962] he is idle at (3306,9376) facing the player.
- No other message, no `enabled_ops` or transform at spawn, no level change. Block kinds over his whole life (one spawn, 4623 [1956]..[2118]): add (1, say + spawnangle), walk (91 rows, shared with others in the file), facing (the player), `human_sword_slash` x26 (his swing), `human_death` x1, `enabled_ops 0b00000` at death, `[del]`. His blocks are the fight agent's to compare; ours spawns him exactly as above at T+1 via `npc_add(^dt_fareed_spawn)`, `npc_facedir` south, `npc_say`, `npc_setmode(opplayer2)`, `hint_npc`.
- Kill: last hit [2117] (T), `human_death` and `enabled_ops` [2118], hint arrow off + `fd_killed_firewarrior` + diamond + line at [2120] (T+3 from the last hit, death + 2). Queue `dt_smoke_fareed_slain` on the killer so it lands then.

## 7. Open

1. Smoke damage is **not recorded**: the recorder wore a slayer facemask throughout (the only gap, [1443]..[1447], is 4 ticks), and every splat in the windows is a fire giant or the desert heat. The fume is the wiki's (20 every 20 ticks, never below 1 hitpoint) without the line `The smoke in the dungeon burns your lungs.` (deleted: no source).
2. The oploc2 menu's answers are unrecorded; implemented on `varb298_slayer_smoke_toggle` (a guess from its name; its base varp 1077 `slayer_tasks_completed_1` scope/transmit not checked).
3. Firemaking-too-low message for a torch, full-inventory lines, and "a key when one is already held" are unrecorded; defaults noted in the script.
4. Gate crossing is a `p_teleport`; Jagex walks the step ([1703] etc.). A one-tile `p_walk` through the inviswall is not possible with our routing.
5. A torch lit and a server restart (timers do not survive): the varbit stays 1 until the next logout/burn; no descent reset (the old script reset on descent, unrecorded).
6. `dt_fareed.rs2` must (a) call `queue(dt_smoke_fareed_slain, ...)` and stop writing the diamond/varp itself, (b) read `fd_killed_firewarrior`, not `varp6614`. `dt_journal.rs2` must read the varbits.
7. Magic carpet op3 menu (generic, `areas/area_desert`), see section 1.
8. `test/quests/deserttreasure.lua` rows updated by hand, UNRUN (box text for the locked gate, the direct-to-pack diamond).
9. The dungeon's ambience (fire giants, dust devils, pyrefiends, elementals) and music were not audited; they are not this quest's content.
