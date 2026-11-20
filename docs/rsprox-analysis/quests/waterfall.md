# Waterfall Quest from RSProx

Source: **index 4349** (rev 241.1, 2026-10-23, activity "Waterfall Quest"): the
whole quest, stage 0 to 10, in 1,281 ticks. It is the only Waterfall session on
the index (TEXT search for Almera, Hudon, Glarial: 4349 only; Golrie's other hit,
1678, is Roving Elves; Hadley and Gerald are in none). Ticks below are 4349's.

Where no recording reaches (other stages' talk, Hadley, Gerald, every failure
path) the text is the OSRS wiki's `Transcript:Waterfall Quest` and the per-npc
transcripts (Almera, Gerald, Hudon, Hadley, Golrie), fetched 2026-11-20. Those
pages' moods and timings are **unverified**. Everything checked against 4349
agreed with the wiki word for word.

Before this port the quest was LostCity's 2004 content: 2004 text on every page,
`mes` chains where OSRS shows boxes, the raft/barrel/tombstone/door/statue as
bare teleports with no fade, the statue's `cam_shake` "deferred", Golrie's
pebble on an invented varp bit, and the River's Swim an invented refusal.

Files: `OSRS-Content/osrs-content/server/scripts/quests/quest_waterfall/scripts/`
(`waterfall_frame.rs2` is new), test `test/quests/waterfall.lua`.

## 1. Action inventory (`rsprox_actions.py`)

| first tick | action | verdict |
|---|---|---|
| 49..245 | bank booth, GE clerk, offers, collect | generic (banking, GE); not this quest |
| 90..114, 426, 616, 876, 1124 | stamina doses, lobster | generic (drinking, eating) |
| 105, 483, 784 | Varrock / Ardougne teleport tablets | generic, captured in `teleport_tablets.lua` |
| 267..302, 836, 975 | glory / games necklace (worn op 3, Rub op 6) | generic, captured in `jewellery_teleports.lua` |
| 344, 351, 432, 1104 | fencegate_r, poshdoor, elfdoor, castledoubledoorr | generic doors |
| 440, 460 | spiral stairs up/down (2518,3431,1 / 2519,3430,0) | generic maplinks; our landings match |
| 354 | Almera Talk-to | **this content**, §5/§6 |
| 372, 1062 | log raft Board | **this content**, §3 (fade) |
| 398 | rope on crossing rock | **this content**, §4 |
| 411, 1089 | rope on dead tree | **this content**, §4 |
| 417 | barrel Get in | **this content**, §3 (fade) |
| 447 | bookcase2 (plain bookcase) | the recorder walked past it to the quest's; no answer recorded |
| 450 | bookcase_waterfall_quest | **this content**, objbox |
| 453, 459 | the book, its close | already ported from 4349 (`baxtorian_book.rs2`) |
| 505, 514 | world map | client UI |
| 699 | prayer15 (Protect from Melee) | the recorder's own |
| 710..718 | roving_golrie_ladder_to_cellar | **this content**; L+1 face, L+2 `human_pickupfloor`, L+3 move: ours (`~climb_ladder_to`) the same |
| 732 | golrie_crate | **this content**, objbox |
| 759..767 | golrie_gate Open (key in pack) | **this content**, walk-through |
| 771 | Golrie Talk-to | **this content**, §6 |
| 908 | pebble on rareflower_yellow | engine default "Nothing interesting happens." |
| 919 | pebble on tombstone | **this content**, §3 (fade) |
| 932 | Glarial's chest Open | **this content** |
| 956 | Glarial's tomb Search | **this content**, objbox |
| 985 | Wear Glarial's amulet | generic equip |
| 1093 | waterfall_ledge_door | **this content**, §3 (fade) |
| 1111 | baxtorian crate | **this content**, objbox |
| 1145, 1152 | baxtorian_door_2 Open (key in pack) | **this content**, walk-through, stage 6 |
| 1171..1228 | 18 runes on the pillars | **this content**, objbox |
| 1238 | Remove amulet | generic |
| 1245 | amulet on the statue | **this content**, §3 (fade + camera) |
| 1259 | urn on the chalice | **this content**, §5 |
| 1266, 1280 | quest scroll close, logout | client UI |

Teleports: every one is either a generic route above (tablets, jewellery, stairs,
ladder) or one of the quest's fade moves in §3, landing on the recorded tile
(raft 2512,3481; tree 2511,3463; barrel 2527,3413; tomb 2555,9844 -- ours was
2554 before; falls 2575,9861; raised floor 2603,9914).

## 2. Stages and vars

| var | value | goes out with | tick | ours |
|---|---|---|---|---|
| varp 65 | 0 -> 1 | Almera's last page, with "You've started a new quest" | [364] | matched |
| varp 65 | 1 -> 2 | Hudon's "I'm fine alone." (raft talk) | [389] | matched |
| varp 65 | 2 -> 3 | Read on the book | [453] | matched (already) |
| varp 65 | 3 -> 4 | the tombstone's move | [921] | matched (C+2) |
| varp 65 | 4 -> 5 | the ledge door's move | [1095] | matched (C+2) |
| varp 65 | 5 -> 6 | the second key door's crossing | [1157] | matched |
| varp 65 | 6 -> 8 | the statue's move | [1248] | matched (C+3) |
| varp 65 | 8 -> 10 | the chalice, L+4 | [1263] | matched |
| varbit 9110 `waterfall_golrie_chat` (varp 667 bit 14) | 0 -> 1 | Golrie's "Oh that. Yes, have it." | [779] | **fixed** (was our varp 6592 bit 0) |
| varbit 9108 `waterfall_gerald_chat` | -- | never in 4349 | -- | used for Gerald's treasure talk, read off its name: unverified |
| varbit 9109 `waterfall_hadley_chat` | -- | never in 4349 | -- | unused (nothing says what it gates) |

Varps 65 and 667 are both `scope=perm, transmit=yes` (quest_deserttreasure.varp,
quest_elena.varp). The pillar runes are server state with no var on the wire: bits
1..18 of our varp 6592 (perm, unsent).

## 3. The fade frame (step 2b: every scene)

No `cutscene_status`, no camera moves: every move is the same frame
(`waterfall_frame.rs2`), traced packet for packet on our wire
(`TORIRSSERVER_TRACE_OUT=2`, the raft): out box = mesbox echo, `fade_overlay:message ""`,
overlay open, `fade_overlay` 948 (0,255,0,0,50), hp bar hidden + `cc_deleteall`,
messagebox open / text / 600 / events none / continue "" / 1508(text); in box the
same with 948 (0,0,0,255,50), 1508 "0" either side, continue armed. Overlay and box
shut on the last click.

| scene | trigger | F (box out) | move | box in | extra | ours (op tick -> move) |
|---|---|---|---|---|---|---|
| raft [372..391] | Board, walked: face L+1 | L+2 | F+3, synth `roof_collapse` 1384 delay 10 | F+4 | stage 1: Hudon's talk under the overlay | +4 from the op tick, as recorded |
| barrel [417..422] | Get in, on the click | C | C+2 -> 2527,3413 | C+3 (same text) | | C -> C+2 |
| tombstone [919..924] | pebble, on the click | C | C+2 + stage 4 | C+3 (same text) | | C -> C+2 |
| ledge door [1093..1097] | Open, worn amulet | C + `big_wooden_door_open` 44 | C+2 + stage 5 | C+3 + `big_wooden_door_close` 43 | | C -> C+2, 43 at C+3 |
| statue [1245..1249] | amulet, face on C | C+1 + `cam_shake` axes 0..2 (random 6) + `rumbling` 1678 | C+3 + stage 8, amulet gone, `cam_reset` | C+4 | | C -> C+3; the test counts the 4 camera packets |
| exit door, swims, tree slip, door flood, statue/chalice wash | -- | -- | -- | -- | not recorded | the same frame, the wiki's boxes: **unverified** |

`docs/quests/CUTSCENES.tsv` has the row.

## 4. Other recorded shapes

| interaction | recorded | ours |
|---|---|---|
| rope on rock [398..408] | O throw (774); O+1 `rope_piece` 67 delay 3 at z+6; O+2 wading stance, roped rock + 7 rope tiles, first step; a tile a tick, each rope tile cleared behind; O+9 diagonal off, rock back; O+10 stance back | matched tick for tick (tick log 117..127). Fixed: rope lifetimes 8-i (were 9-i), rock 7 (was 8), no extra tick before the throw, stance keeps plain back/left/right walks and no run (`[bas]` in the `--bas` transcript) |
| rope on tree [411..413] | face L+1; L+2 box + move, rope kept | matched |
| bookcase / crates / coffin / pebble / runes | objbox of the item at L+1, no line before it | matched (were `mes` chains with p_delays) |
| Glarial's chest [936..940] | L+2 `human_openchest` + `chest_open`; L+3 open chest + amulet objbox | matched (Open now gives the amulet; was a second Search) |
| Golrie's gate [767..770] | L+1: game line "You use the key to unlock the gate.", step, inviswall in the gate's slot + `inactiveprisondoor` on the far tile (angle+1) for 2 ticks, `door_open` 62; key kept | matched (`~waterfall_walkthrough`) |
| key doors [1148..1159] | face L+1; L+2 the same walk-through with `baxtorian_door_2_open`; first door says "You use the key to unlock the door.", second silent + stage 6 | matched |
| pillars [1171..1228] | `smokepuff` 86 map anim at 160, rune gone, `smokepuff` synth 1930, objbox | matched (18/18) |
| chalice [1259..1266] | face L+1; L+2 box, no continue, no fade; L+4 urn swapped, 2 diamonds, 2 bars, 40 seeds, stage 10, line, `quest_complete_3` jingle, scroll (urn icon, 78,128) | matched. Fixed: Attack is advanced before Strength (4349's stat updates and first level-up are Attack's) |

Scroll lines: "1 Quest Point|13,750 Strength XP|13,750 Attack XP|2 Diamonds|2 Gold Bars|40 Mithril Seeds" (were "13750 Attack XP|..."; 2004 order).

## 5. Start and end

Start [356..364]: "Hello." / Almera's line / the combat-level box (lower than 25) /
`Start the Waterfall Quest?` Yes.|No. / "How can I help?" ... / the stage and the
"started" line with "Would you? You are kind." The old 2004 menu ("I'm afraid I'm in
a rush." / "How can I help?") is the No/Yes branch now.

## 6. Pages

Every recorded page (`rsprox_pages.py`: 68 rows) is in our scripts verbatim with
its recorded mood (Almera 4, Hudon 9, Golrie 9, 20 boxes). Golrie's text was
entirely 2004 before ("Hello, is your name Golrie?"). Hudon's raft talk gained
"Hmm... I wonder what this treasure is." Forced `|` breaks are gone from every
file. The recorder walked off on Golrie's "Ah, thanks a lot for the key,
traveller."; the player's "No problem. Take care, Golrie." is the wiki's.

## 7. Ops

Every quest loc op the cache gives has a binding (River Swim, Rock Swim to, Dead
Tree Climb, Barrel Get in, Door Open x3, bookcase/crates/coffin Search, chest
Open/Search/Shut, tombstone Read, chalice Take treasure). Unbound and out of scope:
`waterfall_ledge_door_left/_right`, `baxtorian_chalice_waterfall_quest_ash`,
`stonepillar_small_waterfall_quest_op` (Song of the Elves rungs/variants),
`diary_waterfall_rope_*_op` (Kandarin diary), `pog_cave_wall_entrance_golrie_waterfall`
(Path of Glouphrie).

## 8. Npc info (step 12)

| npc | recorded (lives) | movement | facing | ours |
|---|---|---|---|---|
| Almera | 3 | wanders, 1 tile steps | `player=` on Talk-to, `type=reset` after | matched (tick log 53 / 63) |
| Hudon | 2 | never moves | **none**, through the whole raft talk | fixed: our pages gave him `playerfaceclose` each page and sent 5 resets; his raft pages now use the named page and only the player turns (`facesquare`, as [383..389]). Talk-to at stage 2 still sends one reset (unrecorded) |
| Golrie | 1 | wanders his cell | `player=` on Talk-to, `type=reset` after | matched (261 / 275) |
| Hadley, Gerald | 3 each | wander | none (never talked to) | generated defaults; talk unverified |

No `[say]`, `[sequence]`, `[transformation]`, `enabled_ops` or `[bas]` block on any
cast member in 4349.

## 9. Journal

4349 never opens it (`latest_quest_journal` only reset to -1 at login) and the wiki
has no `Transcript:Waterfall Quest/Journal`. `waterfall_journal.rs2` is LostCity's
2004 text: **UNVERIFIED**, unchanged.

## 10. Ambience

In the quest's areas: none. 4349's `map_projanim` (catapult, iron arrows) and
`catapultimpact` are Tree Gnome Village's battlefield (that port's); its area
sounds are teleports and level-ups.

## Open

- Every unrecorded path (see §3's last row; Hadley, Gerald, Almera/Hudon at other
  stages, Golrie's return visits, the no-room pebble, the locked gate lines) is the
  wiki's text with guessed moods and the recorded frame's timing.
- The tree slip and the door flood keep the 2004 8 damage; the wiki shows "Ouch!" but
  no number.
- The tombstone's Read lines and the chalice's 5-slot refusal are 2004 text; no OSRS
  source has them.
- 4349 sends a bare `loc_del` where the first rope tile leaves (2512,3475); ours puts
  the map's `waterfall_swim_point` back there, as our map has it.
- A walk that closes a fade's last box ends the script, so the (transparent) overlay
  stays mounted until the next fade; Jagex shuts it on the walk ([422]).
- The raised-room door hops after the amulet are the 2004 port's.
- The driver's `use_on` cannot settle a rope thrown from 8 tiles (its walk_near
  clicks toward the unreachable tile by the rock first); the test judges the
  crossing by the far-bank tile instead.
- `tools/quest_gate/lint_quest.py` does not run tree-wide (2026-11-20:
  `ge_newoffer_price` names varps 5753 and 9507), so the NBSP/forced-break lint
  was not run.
