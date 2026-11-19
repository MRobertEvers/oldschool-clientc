# Creature of Fenkenstrain: RSProx index 4606

One session, rev 242.1, world 442, 2026-11-18: the whole quest from the signpost
(stage 0) to the scroll ([3575]), 3,600 ticks. Transcripts: `rsprox_index.py get
4606` (default, `--bas`, `--raw`). Ticks below are this session's.

The content it replaces was a 2009scape port (`DrFenkenstrainDialogueFile`,
Quest Helper steps). None of it was verified, and most of it differed from the
recording: the stage numbering, every menu's shape, the bookcases, the hand-in,
the digs, the doors, the brain's source, the rod's casting and the completion.
Branches no recording reaches are taken from the OSRS wiki's transcript
(`Transcript:Creature_of_Fenkenstrain`, fetched 2026-11-18) and marked `[wiki]`
in the scripts.

Files: `OSRS-Content/osrs-content/server/scripts/quests/quest_fenkenstrain/`
(`fenkenstrain.rs2`, `_parts`, `_lightning`, `_finish`, `configs/*.constant`,
`.varp`, `.npc`), plus hooks in `general_use/scripts/spade.rs2`,
`skill_crafting/scripts/jewellery/jewellery{,_if}.rs2`,
`skill_smithing/scripts/smelting/smelting.rs2`,
`areas/area_canifis/scripts/werewolfinnkeeper.rs2`,
`quests/scripts/quest_cheat.rs2` and
`interface_questjournal/scripts/quest_overview_start.rs2`. Test:
`test/quests/fenkenstrain.lua`.

## 1. Stages and vars

The quest has two stage words, and they do not agree:

| tick | varbit 13715 `creatureoffenkenstrain` (quest list, endstate 9) | varp 399 `fenk_quest` (multinpc switch) | goes out with |
|---|---|---|---|
| 604 | 0 -> 1 | (stays 0) | the confirm's "Yes.", with the note's box |
| 1121 | 1 -> 2 | 0 -> 1 | the "Mmmm, I see." after the right second answer |
| 2536 | 2 -> 3 | 1 -> 2 | "Superb!! Those are all the parts I need..." |
| 2772 | 3 -> 4 | 2 -> 3 | the sewing mesbox |
| 3328 | 4 -> 5 | 3 -> 4 | the rod used on the conductor (the lightning box) |
| 3396 | 5 -> 6 | 4 -> 5 | "Destroy it!!! Take the key to the Tower..." |
| 3504 | 6 -> 7 | 5 -> 6 | the creature's "Only one - please stop Fenkenstrain..." |
| 3575 | 7 -> 9 | 6 -> 8 | the pickpocket (op tick) |

Ours before: one constant written to both, so varp 399 ran one ahead from hire
on. At completion it read 9 instead of 8, and the cheat wrote only the varp.
Now `~fenk_set_stage` writes both from the varbit's value.

Flags on varp 400 (`perm`, `transmit=yes` in the quest's own `.varp`; every
quest varbit is on it):

| varbit | written | ours |
|---|---|---|
| 203 read_signpost | 1 at [604] | matched |
| 195 wound_clock | 1 at [695] (the clock) | was never written; written now |
| 193 spoken_to_gardener | 1 at [1407] (his first page through the amulet) | was written on any head talk; now on the amulet's first use |
| 192 coffin | 1 at [1655] | matched |
| 199 unlocked_cavern, 4441/5023 built_bridge_north/south | 1 together at [1825] (key on the door) | the bridges were never written; now written |
| 197 digresult | 3 arms [1903], 2 torso [1940], 4 legs [1958], 1 head [2297] | was never written; written on the dig's click |
| 186/187/188 arms/legs/torso | 1 when HANDED IN [2085], [2118], [2123] | were set when dug; now set on the hand-in |
| 185 gardener_directions | 1 at [2161] (the guide), 0 at [2303] (the head dug) | was never written; written now |
| 189 head | 1 at [2535] (handed in) | as 186 |
| 190 needle, 191 threads_given | 1 [2770], 5 [2771] | matched |
| 200 unlocked_shed | 1 at [2964] (Open with the key held; key spent) | the key was kept; now spent |
| 198 unlocked_tower | 1 at [3470] (Open with the key held; key spent) | the key was kept; now spent |
| all of the above | 0 at [3575] | were never cleared; cleared now (`~fenk_clear_flags`) |

## 2. Action inventory (`rsprox_actions.py`)

| first tick | count | action | verdict |
|---|---|---|---|
| 7, 35, 43, 481.. | | welcome screen, quest list, journal overview, world map | generic UI: not this content |
| 44, 49, 82 | | Fairy Godfather, henchman, Martin the Master Gardener | Fairy Tale I/II (session 3draster-c8's port from 4605): not this content |
| 57 | 1 | worn slot2 op4 (glory to Draynor) | generic jewellery; not audited here |
| 190, 3157 | | Varrock / Falador Teleport | generic spells; not audited here |
| 243..263 | | GE clerk, offers (bronze wire) | generic GE; not audited here |
| 268..300 | | bank | generic; not audited here |
| 313, 3185 | 2 | ectophial | generic; not audited here |
| 477 | 1 | Vanstrom (Canifis) | not this content |
| 496, 510 | | Canifis man/woman | Canifis npcs; not this content |
| 534 | 1 | opobj3 fenk_brain (the jar on the bar) | **this content**: Roavar's sale (§4). Ours had no binding; the brain was a 2009scape menu row on Roavar. |
| 595 | 2 | signpost | **this content**: §3 |
| 665.. | 15 | fenk_door_mirror, fenk_door, poordoor | castle doors: generic double doors and poordoors, except the first-floor conductor door (§6) |
| 686, 875, 3314, 3338, 3472, 3522 | | ladders 16683/16679 | generic climbs; L+1 face, reachforladder / pickupfloor, teleport the next tick |
| 694 | 1 | fenk_clock | **this content**; ours had no binding |
| 704, 868 | | the letter's Read, close | **this content**; ours had no binding |
| 930.. | 14 | gardener ghost | **this content**: §5 |
| 940 | 4 | cane pile | **this content** |
| 944 | 6 | shed door | **this content** |
| 967, 1039 | 20 | fenk_stairs_lv1(_top) | generic maplinks; landings match `maplink.dbrow` |
| 980 | 18 | bookcases | **this content**: §4 |
| 1005 | 5 | tower door | **this content** |
| 1044.. | | stamina, attack and strength potions, eggs dropped | generic; not audited here |
| 1059 | 5 | Fenkenstrain op3 (pickpocket) | **this content** |
| 1068 | 24 | Fenkenstrain op1 | **this content**: §3 |
| 1205, 1359, 2076 | | open doors closed | generic |
| 1246, 2857 | 2 | book close | **this content** (the chimney book) |
| 1353, 1952, 2524, 2910.. | | item on item | **this content**: amulets, head and brain, canes and brushes; the arms on the torso is "Nothing interesting happens." (engine) |
| 1399, 1748, 3583, 3585 | | equip ghostspeak, dramen staff, rings | generic equip |
| 1596 | 10 | spade Dig | **this content** beside a quest grave; generic elsewhere |
| 1601, 1629, 2291 | | grave Read/Dig | **this content** |
| 1608.. | 8 | memorial Push/Search, star on it | **this content** |
| 1678 | 15 | experiment_1 attack | **this content**: §7 (the recorder's own swings are their kit) |
| 1721, 1824 | 4 | mausoleum door, key on it | **this content** |
| 1756, 2359 | | prayers | the recorder's own |
| 1808 | 1 | the key picked up | generic take |
| 1838, 1849, 2047 | 4 | cellar ladders | generic maplinks; the east one lands 3578,3526 in ours, 3577,3527 recorded (Open) |
| 1840 | 2 | chest | **this content** |
| 1873 | 2 | the island bridge (north mirror) | **this content** (§6); ours had no binding |
| 2083.. | 8 | items used on Fenkenstrain | **this content** |
| 2165 | 7 | the guide gardener | **this content**: §5 |
| 2214 | 7 | call follower | generic ("You do not have a follower."): the guide is not a follower |
| 2308, 2325, 3437 | | head and tower key dropped / taken | generic drop/take |
| 2422, 2992 | 2 | head, brush on the gardener | "Nothing interesting happens." (engine) |
| 2669.. | | Canifis general store: needle, thread | generic shop; not audited here |
| 2754 | 2 | run toggle | generic |
| 2967, 2971 | 2 | cupboard Open / Search | **this content** |
| 3059 | 4 | brush on fireplaces | **this content** |
| 3130 | 1 | glory submenu (Draynor) | generic jewellery |
| 3141.. | | bank | generic |
| 3170, 3173 | | furnace, silver_crafting:lightning_rod | **this content** through generic crafting (§6) |
| 3180, 3324 | | conductor on brush, brush on conductor | "Nothing interesting happens." |
| 3319, 3328 | 3 | the broken conductor | **this content** |
| 3477 | 2 | the creature | **this content** |
| 3546, 3579 | | journal switch, quest rating | generic |
| 3591 | 2 | logout | generic |

Teleports: every one follows its row above. The memorial pushes land 3577,9927
[1663] and 3504,9969 [1863], as ours do. The stairs land 3559,3554,1 /
3537,3554,1 up and 3537,3549 / 3559,3549 down, matching `maplink.dbrow`.

## 3. Pages and shapes (`rsprox_pages.py`: 340 pages)

Every recorded page's text is in the scripts verbatim, with the base of its
recorded mood: `chatneu`, `chatquiz`, `chatshifty`, `chatscared`, `chatbored`,
`chatang`, `chatshock` (the row count picks the variant). The recorder's name
is `<displayname>` (it appears in three of Fenkenstrain's lines).

| section | recorded | ours before -> now |
|---|---|---|
| signpost | confirm on the op; "No." does nothing; "Yes." writes the stage and the start line and opens the note's box, with Jagex's four rows | no confirm, no start line, "Hunchback" row broken differently -> as recorded |
| interview | "Yes.\|No." menu; "Stunning\|Awe-inspiring\|Breathtaking\|Braindead" and "Combat\|Magic\|Cooking\|Grave-digging" with no periods and no echo; a wrong first answer ends on "Mmmm, I see." + "I'm sorry, but I don't think you have the aptitude for this position." [1078], a wrong second the same [1101] | options with periods, every answer echoed, wrong answers asked both questions and said "not the man for the job" -> as recorded |
| hired menu | five options, no greeting, one answer a talk; row 1 "I have some body parts for you." while a part is held, else "Do you know where I could find body parts?"; the player's reworded lines ("What do you want me to do, again?", "Why do you want to make a creature?") | four options, wrong rewordings, no creature/job rows -> as recorded |
| hand-in | use-on per part, a line each ([2085] "Great, you've brought me some arms.", [2118] legs, [2123] torso); the menu's row hands in one ([2535] the head); the fourth writes the stage with "Superb!!", "Oh bother!", "Go and get me a needle..." | all four at once with invented lines -> as recorded |
| head/brain on him | "I can't use this head, <name>. It's missing..." [2385]; "A pickled brain? ... crisps?!?" [2396] | none -> as recorded |
| needle and thread | "Where are my needle and thread, <name>?", "Ah, a needle. Wonderful.", "Some thread, excellent.", the sewing box with the stage, eight pages | pages missing and reworded -> as recorded |
| conductor on him | the page, then "Dr Fenkenstrain doesn't want this." [3272..3278]; any other item the line alone [3283] | none -> as recorded |
| creature lives | eleven pages; the stage with "Destroy it!!!", the key on its click [3401] | four of them -> as recorded |
| tower key lost | "I seem to have lost the Tower key." / "Oh, for goodness sake. Here's a copy I had made." with the key [3444] | silent re-give -> as recorded |
| the creature | the drunk lines (`*hic*`), the two mirror mesboxes, the stage on his last page; spoken to again, the ring's five pages [3508..3514] | sober 2009scape text -> as recorded |
| stage 7 Fenkenstrain | six pages, no stage [3566..3573] | none -> as recorded |
| pickpocket before 7 | "What do you think you're doing???" page, no anim [1061], [2764], [3368], [3419] | "You need a Thieving level..." / an anim -> as recorded |
| completion | the op: "You steal the Ring of Charos from Fenkenstrain.", the stage, the flags cleared, the ring, the scroll ("2 Quest Points", "1,000 Thieving XP", "The Ring of Charos"); **no anim** | anim + delay, "1000 Thieving XP\|Ring of charos" -> as recorded |
| gardener before the job | "Who are you?" then the twitching-neck mesbox [930..935] | "..." -> as recorded |
| gardener menu (2..3) | "Tell me about Fenkenstrain.\|What happened to your head?\|What's your name?"; without the amulet each answer is the player's line then the twitch [1381]; the first answer with it opens the amulet's objbox [1403] | a single line -> as recorded |
| gardener menu (4) | "Tell me about Fenkenstrain.\|Do you know where the key to the shed is?\|Do you know where a conductor mould is?\|What happened to your head?"; the key's row gone once given [2954] | three rows -> as recorded |
| the head's tale | four pages, then "Well, oi s'pose oi've got 10 minutes to spare." (the guide) or "No mate, you've found my head already, oi think." [2439] | invented "Dig at my grave southeast of the fairy ring" -> as recorded |
| Roavar | taking the jar off the bar: six pages and "I'll buy one.\|I'm not hungry." [534..575], with no quest gate | a "Do you sell pickled brains?" row on his talk -> the take; the row is deleted |
| bookcases | §4 | |

## 4. Interactions

| interaction | recorded (O = the op tick) | ours |
|---|---|---|
| bookcase, before the job / any other | "It is a bookcase full of books." [983], [3021] | matched |
| east bookcase (3555,3558,1) | "Which book would you like to read?": "Chimney Sweeping on a Budget" (human_readbook + the book 392 with its 19 rows [1227]), "Handy Maggot Avoidance Techniques" (objbox `fenk_journal` latch + loc_anim `fenk_bookcase_open` + `bookcasedoor` on the click, then objbox obsidian [1256..1259], or "The secret compartment is empty." mesbox [2864]), "My Family and Other Zombies" (objbox `fenk_journal`, one of "appallingly dull" / "a mediocre read" / "mildly amusing" [1249], [1266], [2869]) | rebuilt; the roll is 1/3 each (UNVERIFIED odds) |
| west bookcase (3542,3558,1) | the four books [1297..1346]; the latch is a plain mesbox, the marble amulet an objbox | rebuilt |
| amulets | objbox star, the swap on the click [1353] | was a mesbox |
| clock | objbox letter, varbit 195, the letter [695] | new |
| letter | "You read the letter.", title "Rologarric, ", 46 rows with pagepos counting to 100 [705] | new |
| brain + head | objbox [2524] | was a mesbox |
| memorial Push | not open: "The coffin is incredibly heavy, and does not budge. " (trailing space) [1609]; open: loc_anim `fenk_capstone_moves` + `scrape` on O, teleport O+3 [1660..1663], the island's the same [1860..1863] | was an instant teleport and a message |
| memorial Search | the star memorial: "You find a depression in the memorial stone in the shape of a six-pointed star." [1649]; others "You find nothing remarkable about the memorial stone. " [1622] | new |
| star on memorial | objbox star [1655] | was a mesbox |
| grave Read | "The grave says:" + "   'Here lies  <name> - REST IN PEACE'" (three spaces, then two) | one space each, wrong names |
| grave Dig / spade beside it | O: varbit 197, "You start digging...", `human_dig_long`, `digspade` (spade route also `digspade` 1,0), `digspade` 3,25; O+6: seq -1, objbox find, the same line in chat on its close; an empty grave "... but the grave is empty." [1635] | was human_dig, p_delay(2), a mesbox; the spade did nothing at a grave |
| experiment key | private obj on the kill tick, despawn 200, "Untradeable drop: Cavern key" [1802] | the line was missing |
| mausoleum door | locked: "The door is locked." [1721]; the key on it: varbits, key spent, inviswall + `fenk_mausoleum_door_open` (far tile, turned) for 2 ticks, `iron_door_open` + `locked`, the step through [1825..1827] | was a swing door that kept no key |
| chest | objbox key whenever none is held [1841]; "The chest is empty." [1847] | gated on stage |
| shed | "The door is locked." [948]; Open with the key held spends it; inviswall + open leaf 2 ticks, `door_open` + `locked` [2965], [2977] | key kept, swing door |
| cupboard | Open: `human_openhighcupboard` + `cupboard_open` on O, the open loc O+1 [2969..2970]; Search: objbox brush [2971] | no anim/sound; a plain line |
| canes | "You take a garden cane from the pile.", no gate [940] | gated on stage 4 |
| cane on brush | "You attach the cane to the brush.", a wire spent [2980] | matched |
| chimney | `fenk_human_poke` + `brushing` on O, "You give the chimney a jolly good clean out." O+5; the west first-floor one an objbox mould with it [3108] | instant, a mesbox |
| furnace | silver bar on it opens `silver_crafting`; `lightning_rod` cell: `human_furnace` + `furnace`, the rod O+2, Crafting +50 [3170..3175] | bypassed the panel with its own line |
| conductor door (3549,3543,1) | locked before stage 4: "The door to the lightning conductor is locked." + `locked` [1013]; after, both leaves inviswalls with `fenk_door_open`/`_mirror` on the south tiles for 2 ticks, `nicedoor_open` [3312..3314] | was the generic double door, never locked |
| conductor | Repair: nothing with the rod held [3319]; rod on it: stage, `fenk_conductor_repaired` 4 ticks + loc_anim `fenk_lightning` + `lightning`, the box [3328..3332]; after: "The lightning conductor is now beyond repair." [3336] | was op1 Repair with a game line |
| tower door | locked [1007]; Open with the key held spends it, inviswall + open leaf, `iron_door_open` + `locked` [3470] | key kept |
| bridge (island) | below Construction 59: cam_lookat 3505,3566 h50 / cam_moveto 3491,3556 h1800 (rate 100), the line and box a tick on, cam_reset 4 ticks after the close [1878..1885] | new; the build itself is UNVERIFIED (not ported) |

## 5. The gardener's guide (npc info)

[2161]: on "10 minutes to spare" the garden ghost (`fenk_gardener_multi_2`) is
`[del]`eted and a new `fenk_gardener_multi` is `[add]`ed beside him, facing the
player (varbit 185 flips which multinpc shows). It follows the player,
walking a tile a tick and telejumping beside them when it falls about 17 tiles
behind ([2215], [2244], [2276]). Talked to, it says a line overhead, with no
page: "You need to head off to them Haunted Woods, mate." in the castle's
square ([2165..2228]), "Go southeast, mate." in the woods ([2248]). It is
`[del]`eted when the head is dug ([2303]). It is NOT a follower: "call follower"
answers "You do not have a follower." [2214]. Ours: `npc_add` + `npc_setowner`
+ `playerfollow`, the says by area, deleted on the head's dig. Open: our
playerfollow moves two tiles a tick and never telejumps (engine-generic).

## 6. Npc-info notes

- **Hidden multinpcs are removed from npc info.** Jagex `[del]`s an npc whose
  multinpc resolves hidden for this player and `[add]`s one that becomes
  visible: Fenkenstrain and the released Rologarth at [3575], the two
  gardeners at [2161]. Our server keeps sending the hidden one and the client
  hides it. Engine-generic; Open.
- Fenkenstrain and the gardener wander (walk steps through the whole
  session); the experiments' sequences are `homolupus_attack`/`_defend`/`_death`
  (cache params, matched).

## 7. The experiment fight (regular mob, no §13 audit)

`fenk_experiment_1`: every swing `wolf_attack` (25/25), every hit on it
`female_hit` (25/25), death `homolupus_death` + `wolf_death`, `[del]` 2 ticks on
(death_delay 2). Ours had all three sounds `null` (generated block); they are
now in `configs/fenkenstrain.npc`, and the generated lines are deleted.
Experiments 2 and 3 were never fought: UNVERIFIED.

## 8. Journal

Rows verbatim at stage 2 ([1160], no part; [2180], three parts struck: a given
part's row is `<str>a pair of arms`), 4 ([2837], [3207]), 6 ([3413]) and 7
([3518], [3542]). Current format: struck paragraphs, no blank rows. Stages 1,
3, 5 and 9 are UNVERIFIED (no recording opens them; the wiki has no journal
transcript). The overview's start text: "reading the
<col=800000>Signpost<col=000080> in <col=800000>Canifis<col=000080>." [481].

## 9. Cutscenes

None: no `cutscene_status`, fade, `minimap_toggle` or scene camera in the
session. The only camera is the bridge refusal's (§4). `docs/quests/CUTSCENES.tsv`
row updated.

## 10. Test and wire audit

`python3 tools/quest_gate/run.py fenkenstrain --no-publish`: **226/226 PASS**
(2026-11-18, last run after every change below). The test walks the recorded
flow: the confirm, the brain taken off the bar, the interview with no echoes,
both bookcases, the gardener's tale and the guide (its overhead line read from
the tick log), the spade at each grave, three parts used on Fenkenstrain and
the head by the menu, the needle and thread, the shed opened with the key held,
the cupboard, the canes, the west chimney, the furnace's lightning rod cell, the
conductor door walked through, the rod on the conductor, the Tower door opened
with the key held, the creature, the stage-7 refusal, and the pickpocket.

Mechanical page check (every recorded page and menu row from [534] on, `<br>`
read as a space, the recorder's name as `<displayname>`): 424 checked, 421
literal. The other three are the signpost (Jagex's own `<br>` rows) and two
`text_gender` lines ("man", "sir": the recorder is male).

Npc-info block inventory (`npc_block_inventory.py idx4606.txt --ours
ticklog.tsv --match fenk`): every recorded kind is now sent ("kinds recorded and
never sent: 0"). Before the guide's talk row, `[say]` on `fenk_gardener_multi`
was the one missing kind. The counts differ by route: the test talks less and
fights one experiment for fewer swings.

Sounds and loc anims (ids and loops/delay from [480] on, recorded -> ours):
`bookcasedoor`, `brushing`, `digspade` (1,0 and 3,25), `lightning`, `locked`,
`scrape`, `pick2`, `furnace`, `cupboard_open`, `iron_door_open`,
`wolf_attack`/`female_hit`/`wolf_death`, `fenk_bookcase_open`,
`fenk_capstone_moves` and `fenk_lightning` all go out with the same fields.
Counts follow the route. Found and fixed: the castle's double doors played
`door_open` (62); they now play `nicedoor_open` (81, 16/16 recorded; the close
is unrecorded) through `doors/scripts/door_procs.rs2`.

Var transmit (`varp_transmit_audit.py`): `pagepos` (334), the letter scroll's
counter, was `transmit=no`, and the recording sends 1..100. It is stated
`transmit=yes` in `fenkenstrain.varp` and its generated line is deleted. It has
not yet been observed on a wirelog.

## Open

- Engine: hidden multinpcs should be `[del]`eted from npc info (§6).
- Engine: `playerfollow` should walk a tile a tick and telejump after the owner
  when far (§5).
- The east cellar ladder (`ladder_from_cellar_directional` 3578,9927) lands
  3578,3526 in `maplink.dbrow`; recorded 3577,3527 [2054]. The file is
  generated and a second row's precedence is undefined.
- The island bridge's build at Construction 59+ (no recording).
- The guide's compass word thresholds and its line at the grave ([wiki]).
- Stages 1, 3, 5, 9 of the journal; experiments 2 and 3; the "My Family and
  Other Zombies" odds.
- Generic content the session exercised and this port did not diff: the GE,
  the bank, glory and ectophial teleports, Varrock/Falador Teleport, potions,
  the Canifis general store.
