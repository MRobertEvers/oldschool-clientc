# The Forsaken Tower — RSProx index 4424

Rev 241.1, world 369, recorded 2026-10-29. The whole quest end to end: Lady Vulcana
[258], Undor and Ignisia [353..422], the tower [570..1331], the hand-in [1352..1366],
the completion [1487..1522], then the reward page into Kharedst's Memoirs [1541..1557].
Cites are `[tick]` in idx4424.txt.

What was there before: a quest-helper/wiki build with invented dialogue (none of the
189 recorded pages matched), invented puzzle messages, a deterministic grid, a fixed
fluid riddle in its own words, and two "leftover" mesboxes. Rebuilt:
`quest_forsakentower/scripts/forsakentower.rs2`, `ft_puzzles.rs2`, `configs/*`
(`ft_bmp.rs2` deleted: its debugprocs drove the invented pages). Test:
`test/quests/forsakentower.lua` (scriptrun: 186/187; the one failure is `quest.journal`,
which fails on every quest under scriptrun), and `test/quests/_journals.lua`
`journal.forsakentower.6`.

## Stages and vars (all on varp 2066..2069, perm, transmitted)

| var | value | goes out with | tick |
|---|---|---|---|
| lovaquest 7796 | 0 -> 1, start line, pylon_1=15, pylon_setup=1, fluid_setup=1 | "Ah yes, the urgent matter..." | [269] |
| | 1 -> 2 | "Could you go to him..." | [273] |
| | 2 -> 3 | "The Forsaken Tower, as it's now known..." | [421] |
| | 3 -> 4 | the entry door's step | [573] |
| | +1 per finished puzzle (furnace 5, power 6, refinery 7, altar 8) | that puzzle's last box | [785] [906] [1040] [1309] |
| | 8 -> 9 | Search the display case | [1329] |
| | 9 -> 10 | "Give Dinh's Hammer to Undor?" Yes | [1361] |
| | 10 -> 11, lovaquest_reward 1 | "Well here you go, have this then."'s click | [1500] |
| foundhammer 7804 | 1 | Inspect the case | [580] |
| furnace 7798 | 1 inspect, 2 first jug at the container, 3 four gallons in, 4 lit | | [613] [753] [771] [785] |
| electricity 7797 | 1 generator inspected, 2 started, 3 grid inspected, 4 aligned | | [838] [845] [851] [906] |
| refinery 7799 | 1 inspected, 2 tried, 3 cleansed, 4 active | | [939] [943] [1037] [1040] |
| altar 7800 | 1 first disk taken, 2 solved (all pylons 16) | | [1206] [1309] |
| grid a1..f6 7811..7846, shuffle 7803 | rolled on the first "Align" Yes | | [854] |

Ours writes every one of these on the same click. Server-only: the jugs (varp 7893)
and the fluid order (varp 7894, rolled at the start).

## Action inventory (`rsprox_actions.py`), a verdict per row

| first tick | action | verdict |
|---|---|---|
| [13] [251] [1482] | doors: fai_varrock_poor_door_flipped, lova_wall_door_lower(_m) | generic. The Assembly door was WRONG: ours swung one leaf onto 1484,3742; Jagex swings both leaves in place with area `nicedoor_open` range 1. Fixed: `doors/scripts/lova_assembly_door.rs2` (this placement only) |
| [39..76] [1002] [1005] | quest list, overview (782), journal | generic; the stage-6 journal checked row for row (below) |
| [77..142] [311..342] [470..485] [1079] | banks | generic, not audited here |
| [153] [156] [1173] [1084] [1085] [192] [506] [1169] | potions combined, vials dropped, antipoison, lobsters, stamina | recorder's kit (the stamina's `clock_tick_1` / `tol_homonculus_crying` are its expiry warning, as the Grand Tree doc found) |
| [162] [430] [458] [490] [1049] [1095] [1374] | Kourend, Varrock and Battlefront tablets | generic, covered by `test/magic/teleport_tablets.lua` (2026-10-29 audit of 4404..4430) |
| [347] [1344] | games necklace Rub > Wintertodt | generic, covered by `test/magic/jewellery_teleports.lua` |
| [171] [53] | world map | client |
| [258] [1487] | opnpc1 Lady Vulcana | **this content**, rebuilt |
| [352..] | opnpc1 Undor (wint_master_smith) | **this content**, rebuilt |
| [375] [378] | Ignisia op3 (walked off) then Talk-to | generic Wintertodt; her first talk was wrong (5 pages reworded, moods, "How else may I be of assistance?" and the four-row low-Firemaking menu missing). Fixed in `wintertodt_npc.rs2` |
| [570] [1185] | lovaquest_tower_entry_door | **this content** |
| [578] [588] [1325] | display case, lovaquest_locks close | **this content** |
| [595] [814] [911] [1198] [1313] | the four ladders | **this content** |
| [604] [788] [807] [931] | lovaquest_inner_door / _open | generic door; sound was `door_open` 62, Jagex `iron_door_open` 71 / `iron_door_close` 70. Fixed in `door_procs.rs2` |
| [795] [803] [920] | spiral stairs | **this content**; landings and timing fixed (below) |
| [611..785] | furnace, shelves, jugs, tinderbox, coolant, container, jug use/check | **this content** |
| [821] | prisondoor (basement) | generic door; sound fixed to 71 as above |
| [828..906] | crate, generator, grid (51 presses) | **this content** |
| [934..1041] | refinery, notes, fluid table, refinery use | **this content** |
| [961] | run toggle | client |
| [1205..1309] | pylons 1..3 (41 clicks) | **this content** |
| [1520] [1522] | scroll close, the post-quest menu | **this content** |
| [1538..1557] | wield/read Kharedst's Memoirs, page used on it | generic (`kharedst_memoirs.rs2`); the box's zoom was 250, Jagex 400. Fixed |
| [1562] | logout | client |

## Teleports

| tick | cause | verdict |
|---|---|---|
| [166] [435] [462] [494] [1053] [1099] [1378] | tablets | covered (teleport_tablets.lua) |
| [350] [1347] | games necklace | covered (jewellery_teleports.lua) |
| [798] | east stair up, 1385,3824,0 -> 1387,3826,1 | was 1386,3826,1: fixed (`forsakentower_maplinks.dbrow`) |
| [812] [1323] | west top down -> 1378,3823,0 | matched |
| [818] [918] | basement ladder -> 1382,10228,0 / 1382,3824,0 | matched |
| [925] [1196] | west stair up -> 1377,3826,1 (from 1379,3824 and 1378,3823) | was 1378,3826,1: fixed |
| [1203] [1316] | top ladder -> 1382,3826,2 / 1382,3828,1 | matched |

The stairs hold a tick longer than the generic staircase (face L+1, busy L+2, move L+3,
4/4). Ours moved on L+2; now `~ft_stairs` (arrive, a tick, the move). Ladders: face L+1,
anim L+2 (827 down, 828 up), move L+3, matched; from standing ours animates on C and
moves C+1.

## Sections (R = recording)

### Lady Vulcana, start [259..289]

Every page verbatim with its mood; Unkar and Jorra speak inside Vulcana's
conversation with their own heads (`councillor_unkar`, `lovakengj_assistant`). The
start offer is `"Start The Forsaken Tower quest?" Yes.|No.` after "What do you want
human?"; the start line with the stage. After the last page Unkar's three-topic menu
loops, dropping the topic just asked ([281], [288]). 33/33 pages in the test.
Unrecorded (wiki, UNVERIFIED): the unqualified branch, "No." ("Nothing."), the
mid-quest "Human, we have given you a job to do...", Unkar's and Jorra's own Talk-to
(neither was bound before), the post-quest Vulcana menu answers.

### Undor [353..422], [1352..1366]

Before Ignisia: "Hello." / "Well hello there, laddie." / "I've been sent to help you." /
"I just need to finish this little job..." After: "Hello there, laddie." and the
five-row menu; row 4 is "I've been sent to help you." at stage 2 and "Let's talk about
my quest." from stage 3. The hand-in: menu "Give Dinh's Hammer to Undor?", the objbox
with the hammer, four pages. All recorded pages matched (4 + 26 + 13). The three
lore topics now return to the menu (wiki `{{tact|other}}`); "lassie" for a female
player is the wiki's.

### The tower door and the display case [570..588], [1185..1189], [1325..1331]

| R | ours | verdict |
|---|---|---|
| L+1: inviswall r3 on 1382,3817, lovaquest_tower_entry_door_open r0 on 1382,3816 for 2 ticks, synth 71, the step onto 1382,3817; stage 4 first time | same (tick log 115/117) | matched |
| Inspect: L+2 objbox (hammer, zoom 400) "The display case is sealed..."; a tick after its continue, mainmodal_open + lovaquest_locks | same | matched |
| Search at stage 8: L+2 stage 9, the hammer, synth `pick` 2581, objbox | same | matched |

### Furnace [604..785]

| R | ours | verdict |
|---|---|---|
| Inspect: mesbox "It's an ancient furnace...", without a tinderbox "You do not have anything suitable to light the furnace with." | same | matched |
| lovaquest_tower_shelves: "You search the cupboard but find nothing of interest." on the face tick | was UNBOUND | fixed |
| jugs cupboard: menu "There are some coolant jugs in the cupboard." 4 rows; Take both: objectbox_double | same | matched |
| tinderbox cupboard: objbox "There's a tinderbox in the cupboard. You take it." | same | matched |
| jug on dispenser: "Would you like to fill your 5-gallon jug with coolant?" -> human_pickuptable 832 + tap_fill 2609 + objbox | same | matched |
| Fill-jug with both: "Would you like to fill a jug with coolant?" 5-gallon jug.\|8-gallon jug.\|No. | same | matched |
| jug on jug: "Add N gallon(s) of coolant to your 8-gallon jug?", `liquid` 2401, objbox; "It now contains" only when the jug poured into was not empty; an empty jug poured: objbox "The 8-gallon jug is empty." | same, all five recorded texts in the test | matched |
| Check: "This 8-gallon jug contains 8 gallons of coolant." Empty the jug.\|Do nothing. -> liquid + objbox | same | matched |
| container: first use moves furnace to 2; 8 gallons refused (mesbox); 4 gallons: anim, liquid, both jugs gone, objbox | same | matched |
| Light: "Light the furnace?"; no-continue mesbox "You attempt to light the furnace..." + human_createfire 733; +4 ticks seq -1, fire_lit 2596, stage, mesbox | same (236 -> 240) | matched |

### Power [814..918]

Crate objbox (crank), generator mesbox then the crank menu then objbox, grid mesbox +
"Align the power grid?" -> rolled tiles, mainmodal_open, grid armed OP1|OP2, each OP1
press a quarter turn; solved: the grid closes, `found_gem` 2655, mesbox "You
successfully align the power grid." All matched. Our solved table equals the
recorded final state tile for tile; the start is rolled per tile (the recording's had
solved tiles in it, so it is not a fixed offset as before).

### Refinery [920..1041]

| R | ours | verdict |
|---|---|---|
| Inspect: mesbox, "Activate the refinery?", the warning mesbox, "Cleanse the refinery?", no fluid: "You don't have anything to cleanse the refinery with." | same | matched |
| notes cupboard: objbox "There are some old notes in the cupboard. You take them." | same | matched |
| Read: note (291) rows 1..4 and 9..12 blank, the four clues on 5..8, then the mount | same; the clue templates are fixed, the five names (Cleansing fluid, Toadflax blend, Sulphuric acid, Hydrogen peroxide, Sodium hydroxide) shuffled at the start | matched |
| Table: menu_new (947) "Which would you like to take?" 5 places + (Cancel); objbox "You take the fluid from the table." / "...and return your other fluid." | same | matched |
| wrong fluid [994..999]: Yes -> "You pour the fluid into the refinery...", liquid; +3 "...but a violent reaction occurs!", gas_hiss 2300, an 11, poison 18 | same (scratch run: 31 -> 34, 11, gas_hiss, human_hit at delay 20) | matched; see Open for the first poison tick |
| right fluid [1037..1040]: refinery 3, the vial AND the notes gone, objbox, "Activate the refinery?" -> 4, stage, found_gem, mesbox | same | matched |

### Altar [1205..1309]

The answer on the face's tick. Take: altar 0 -> 1 the first time, the pylon's lowest
disk, `pick2` 2582, objbox "You take the level N energy disk from the pylon."; add:
`put_down` 2739, objbox "You add the level N energy disk to the pylon."; a bigger disk
on a smaller: the recorded refusal objbox; the 15th move: "...successfully rebalancing
the runic altar.", altar 2, stage 8, all pylons 16. Each pylon is bound on its own base
loc (the multiloc rungs are shared by all three; the old rung bindings ran first and
told the pylons apart by tile). Matched.

### Completion [1487..1522]

19 pages; on the last click: stage 11, reward varbit, coins 6,000 and the memoir page,
the line, song 147 and jingle 154 (`quest_complete_3`, ours played 153 before: fixed
in questpoints.rs2), the scroll rows "1 Quest Point / Free use of the Lovakengj /
Minecarts / 6,000 Coins / 500 Mining XP / 500 Smithing XP / A Memoir Page", the hammer
model; the xp behind the scroll; "Oh. The human is still here." when it closes, then
her menu.

### Journal (index 4424 [1002], stage 6)

27 rows, checked row for row (scratch scriptrun of `::ftjournal`, and
`_journals.lua journal.forsakentower.6` for the live lane). Every other state is
UNVERIFIED: its rows are ours, built the same way (a puzzle met but unfinished is its
first sentence unstruck).

### Npcs

| type | R | ours before | now |
|---|---|---|---|
| vulcana_lovakengj | 9 steps, every tile within 1 of 1483,3748 | wander 5 | wander 1 |
| councillor_unkar | 11 steps, within 1 of 1482,3746 | wander 5 | wander 1 |
| lovakengj_assistant (Jorra) | 0 steps in two lives | wander 5 | none |
| wint_master_smith (Undor) | 3 steps, within 1 of 1625,3943 | wander 5 | wander 1 |
| wint_master_pyromancer | 0 steps | none | none |

(`quest_forsakentower/configs/forsakentower.npc`; the four `defaultmode`/`wanderrange`
lines deleted from `server_fields.generated.npc`.)

No cutscene: the recording's `cam_reset`/`minimap_toggle` lines are all teleports.
No ambience in the tower; the Assembly's `longbow` area sounds are someone else's.

## Open

- The first poison tick: Jagex hits ~31 ticks after the reaction ([999] -> [1031]);
  ours takes the poison timer already running from login (`~set_poison_login`), so the
  first hit came the next tick in the scratch run. Generic poison, one sample.
- Unrecorded and UNVERIFIED (ours): lighting without coolant (wiki: 40% hitpoints; our
  line), the container's Inspect, an unpowered grid, the Start/Align rungs going
  straight to their menus, the pylons' Reset (silent), the altar's Inspect, a second
  search of any cupboard or crate, the door before stage 3 ("The door is locked."),
  leaving by the door (the leaf mirrored), the post-quest Vulcana/Unkar answers' moods.
- The east stair top's Climb-down landing is the model reading (no recording).
- The tower's music: Jagex starts 624 "The Forsaken Tower" (with its unlock line) on
  the step through the entry door ([574], [1188]) and keeps it on every floor ([918]).
  Ours plays square 21_59's 445 on the ground and upper floors and 624 only in the
  basement's square (21_159): `docs/audio/music_regions.tsv` maps whole squares. Needs
  the engine's sub-square music areas (the same OPEN item as Draynor's).
