# The Varrock easy diary and the Varrock Museum

Recording: RSProx index **4649** (rev 242, 2026-11-22, "Varrock Easy and Varrock Museum",
ticks 0..1943). The bit map is pooled with 15 more sessions that write
`varrock_achievement_diary` (1176): 4648, 1741, 1707, 3258, 4346, 4423, 4430, 4464, 4489,
208, 213, 1694, 1827, 1828, 3281. The museum's basement (the natural history quiz) is
`docs/rsprox-analysis/quests/natural_history_quiz.md` (4340); this covers the rest.

Content (OSRS-Content/osrs-content/server/scripts):
- `interface_diaries/scripts/diary_varrock.rs2` (new): the claim, the bit map, the tier frame,
  the journal, Toby's reward, the lamp picker; `configs/diary_varrock.varp` (new, varps 9842-9843).
- `areas/varrock/scripts/vm_historian.rs2` (new; Minas moved out of `twocats.rs2`),
  `vm_kudos_overlay.rs2` (new), `vm_museum_map.rs2` (new), `vm_timeline.rs2` (rewritten:
  all 34 displays), `apothecary.rs2` (+ the Potions interface), `curator.rs2`, `gertrude.rs2`,
  `varrock_wiki_npcs.rs2` (Toby, Benny, the stray dog, Torrcs, Marfet), `aubury.rs2`.
- Task hooks: `skill_thieving/scripts/thieving.rs2`, `skill_construction/scripts/poh_sawmill.rs2`,
  `skill_woodcutting/` (the dying tree is now a tree), `skill_fishing/scripts/fishing.rs2`,
  `skill_crafting/scripts/pottery/pottery.rs2`, `skill_agility/scripts/shortcut_varrock_fence.rs2`
  (new), `skill_runecraft/scripts/runecraft.rs2`, `skill_mining/scripts/mining.rs2`,
  `areas/area_stronghold/scripts/stronghold.rs2`, `shop/varrock/scripts/thessalias_fine_clothes.rs2`,
  `skill_magic/scripts/spells/teleport.rs2`, `quests/quest_soulsbane/scripts/soulsbane.rs2`.

Tests (scriptrun, 2026-11-22): `test/varrock/varrock_easy_diary.lua` 97/97,
`test/varrock/varrock_museum.lua` 67/67.

## The claim (every task)

On the tick the task's own line goes out, right after it: the task's bit on varp 1176, the
"Well done!" line, then the tier's count varbit (6319 easy, 6320 medium). 13/13 claims in 4649,
3/3 in 4648, 1/1 in 4346, where the session's first claim also sets `started_varrock_diary`
(4455) 0 -> 1 before the bit. A task already claimed sends nothing (4649: four Varrock teleports,
none credited). Ours: `~varrock_diary_task(tier, task)`, which sets 4455 if 0, the bit if clear,
then `~diary_task_award` (our mask, the line, the count).

### The bit map

| tier | task -> bit | recorded |
|---|---|---|
| easy | N -> N (bits 1..14) | all 14 (4649 x10, 4648 x2, 208, 4346; 1741 and 1707 agree) |
| medium | 1, 2 -> 15, 16; 3..13 -> 18..28 | 1, 2, 4, 5, 6, 7, 8, 9, 10, 13; bit 17 is never set, even in 1827 and 1828 whose diaries hold every other bit 1..31 |
| hard | 1..3 -> 29..31 | 2 (1828 [6493]), 3 (1827 [6465]); 1 by the rule |
| hard 4..10, elite | another varp | no recording names it: `~varrock_diary_bit` errors |

### The tasks

| task | recorded | trigger, the claim's tick | ours |
|---|---|---|---|
| E1 Thessalia's store | 4648 [274..275] | Trade: with the shop's mount | thessalias_fine_clothes.rs2 |
| E2 Aubury's teleport | 208 [855] | Teleport op, before the cast | aubury.rs2 |
| E3 iron, south-east mine | 4648 [197]; 1741 [8226] | after "You manage to mine some iron." | mining.rs2 (zone 3278..3296 x 3358..3375) |
| E4 a normal plank | 4649 [487]; 1707 [11978] | the sawmill answer, after the coins | poh_sawmill.rs2 |
| E5 Stronghold level 2 | 4346 [427]; 3258 [480] | after "You climb down the ladder...", with the move | stronghold.rs2 |
| E6 the fence | 4649 [1002]; 1741 [8274] | jump+1 | new binding (below) |
| E7 the dying tree | 4649 [502]; 1741 [8310] | after "You get some logs." | the tree had no binding: now a woodcutting tree |
| E8 a newspaper | 4649 [552] | the "Sure, here you go..." continue, before the objbox | varrock_wiki_npcs.rs2 (the box was missing) |
| E9 a dog a bone | 4649 [77]; 1707 [11459] | after "You give the dog some nice <bone>..." | new `[opnpcu,dog_stray]` (below) |
| E10 the bowl | 4649 [870], [880]; 1741 [8444] | stage at the spin, claim at the oven before "You remove the bowl" | pottery.rs2 + varp 9842 |
| E11 Haig Halen, 50 kudos | 4649 [1796]; 1694 [350] | the continue of "Welcome to the museum of Varrock." | curator.rs2 |
| E12 earth runes | 4649 [1154]; 1741 [8595] | the craft tick | runecraft.rs2 |
| E13 trout, River Lum | 4649 [842]; 1741 [8540]; 3258 [6940] | after the catch's line, spot `0_48_53_freshfish` | fishing.rs2 |
| E14 tea stall | 4649 [20]; 1707 [10760] | after "You steal a cup of tea." | thieving.rs2 |
| M1 strength potion | 4649 [335]; 1741 [8831] | Buy-1, a tick after the press | apothecary.rs2 (interface 556, below) |
| M6 Tolna dungeon | 4649 [1120]; 1741 [8730] | with the move | soulsbane.rs2 (frame below) |
| M8 Varrock teleport | 4489 [182]; 213 [10507] | the landing tick | teleport.rs2 |
| M10 20 mahogany planks | 4649 [483]; 3258 [31581] | the answer, before the coins | poh_sawmill.rs2 |
| M13 rooftop lap | 4648 [480] | (3draster-46's port) | rooftop_varrock.rs2 |

The bowl's first stage: "<col=0040ff>Achievement Diary Stage Task - Current stage: 1.</col>" and
synth `clock_tick_1` x3 after "You make the clay into a bowl." at Barbarian Village's wheel; no
var went out with it, so ours keeps it in `varp9842_varrock_diary_bowl_stage` (perm, not sent).

### The tier's last task (4649 [1796..1800])

T the claim (with the page the click answered, Haig's menu opened); T+1 the menu shut; T+2
`varrock_diary_easy_complete` (4479) = 1 and the box "Congratulations! You have completed all of
the easy tasks in the Varrock area. Speak to Toby in Varrock square to claim your reward.".
Ours: `strongqueue(varrock_diary_tier_done, 1, tier)` from `~diary_task_award` for Varrock only
(the other areas still promote at once). Our strong queue shuts the menu on T, a tick early (Open).

## The journal (area_task row 8)

Eleven opens, every row read. pagepos (334) counted 0 -> N, busy, the title
"<col=800000>Achievement Diary - Varrock", `journal_scroll_reset` (6844), qj1..qjN, 2524(-1,-1),
the mount, 6845(true, N). N = 69, 71 once the easy reward is claimed. Rows: "<col=ffff00>Varrock
Tasks", " ", then per tier its name (`<col=ffff00>`, `<col=00ff00>` once complete [1853]), one
empty row in `<col=000000>` (elite's is `<str>`), " ", the tasks with Jagex's wraps, each row
`<str>` claimed or `<col=000000>` not; under easy, once its reward is claimed, "If I ever lose my
Varrock armour I can speak to Toby in" / "Varrock square." [1853]; " " between tiers. The task
text is the journal's, not the registry's ("south east", not "south-east"). Ours:
`~varrock_journal_open`; strikes from our task mask. The other eleven areas keep the old summary
journal (unverified; no recording opens theirs).

## Toby (4649 [1814..1823])

| tick | recorded | ours |
|---|---|---|
| [1815] | player chathap2 "I've completed all of the easy tasks in my Varrock Achievement Diary!" | matched |
| [1816] | Toby chathap2 "I can see that, well done! You'll be wanting your reward then!" | matched |
| [1817] | player chathap1 "Yes please!" | matched |
| [1818] | `varrock_easy_reward` (4519) 1, `varrock_armour_easy` and `diary_easy_lamp` in, Toby chatneu4 "This armour is a symbol..." | matched |
| [1819] | Toby chatneu1 "furnace. Zaff will stock 15 Battlestaves per day.", player chathap1 "Wow, thanks!" | matched |
| [1820] | Toby chatneu2 "If you ever lose your armour, come back to me to reclaim it." | matched |
| [1821] | menu "Can I have another Varrock platebody?\|Who are you?\|I have a question about my Achievement Diary\|Bye!" | matched |

The lamp [1825..1832]: if1 30, if2 -1, 2524, xpreward, pause 0..23, the title; on the pick the
box "<col=0000ff>Your wish has been granted!</col><br>You have been awarded 2,500 Agility
experience!", the lamp gone, `found_gem`, the picker shut. Ours: `~xp_lamp_rub(lamp, level, xp)`.

## The museum

### The kudos counter (vm_kudos 532 on overlay_hud)

Opens on the first tick the pre-move tile is inside (from 3253,3448 [1204]), closes on leaving
([1806], a teleport). Fitted to all 602 tiles with it up: x 3253..3267, z 3442..3455, levels 0-2;
the nearest tile with it down is 3251,3447. Every main modal in the museum shuts it first and it
comes back as the modal closes (38 display opens, 4 map opens: 42/42). Every kudos gain mounts it
again. Ours: `[mapzone]` arms a 1-tick soft timer over 50_53/51_53 (pre-move tile), state in
`varp9843_vm_kudos_overlay` (0 away, 1 up, 2 under a modal).

### Historian Minas (4649 [1215..1221], [1570..1686])

"Hello!" (chatneu1), his welcome (chathap2), the menu "Tell me about the Museum.|I have some
information that might be of use in your displays.|No thanks.". The information branch tells
EVERY quest not yet told in one conversation, in this order (ours told one and ended):

| # | quest | bit | the bit + 5 kudos ride | lamp |
|---|---|---|---|---|
| 1 | Temple of Ikov | 3650 | his "That's excellent..." [1588] | |
| 2 | Shield of Arrav | 3656 + `vm_soa_vis` 9679 | the lamp box [1616] | `vm_reward_arrav` |
| 3 | Observatory Quest | 3652 | his answer [1625] | |
| 4 | Merlin's Crystal | 3655 | the lamp box [1639] | `vm_reward_merlin` |
| 5 | Demon Slayer | 3653 | his answer [1643] | |
| 6 | The Grand Tree | 3658 | his answer [1650] | |
| 7 | Making History | 3654 | the lamp box [1660] | `vm_reward_history` |
| 8 | Priest in Peril | 3651 = 1 | his answer [1665] | |
| 9 | In Aid of the Myreque | 3651 = 2 | his answer [1671] | |
| 10 | Hazeel Cult | 3659 | his answer [1679] | |
| 11 | Rune Mysteries | 3661 | his answer [1684] | |

Each gain: the bits, kudos (3637) +5, spam "You have earned Kudos with the Museum!", synth
`vm_gain_kudos`, the counter mounted. A lamp: "You receive an old lamp." as a game line and a
mesbox. All 47 pages verbatim with their moods (the wiki text had five wrong lines: "Display
number 24, 5th Age", the Salve pair, "Zamorkian"). The three museum lamps: if1 20, 1,000 xp
([1690..1715], three rubs).

### The timeline displays (vm_timeline 534)

All 34 displays studied (38 opens), each generated from its recorded frame: on L+1
`vm_display_case_ponder`, the text, the number, the map caption hidden, the model(s), set_spinner
(65) on a lone model, the counter shut, the mount; Close: the seq stops, the counter back. Quirks
kept: display 7 sets its text on its model component [1319]; 21 and 7 send their text twice;
the world map (34) shows its caption [1290]. Told/untold, both recorded: 28 (staff, [1230] /
[1748]), 24 (shield, [1295] / [1744]: model 25350 -> 25611 and the returned-shield text), 23
(helms, [1300] / [1752]); 30 stolen once The Golem is done [1238].

### The wall map (vm_museum_map 527)

Look-at [1454], [1541], [1725]: L+1 the ponder seq, busy, the four layers (basement hidden,
ground shown, first and second hidden), "Empty<br>display" on every exhibit still empty (the four
dig-site exhibits by their varbits 3645..3648, the Historian's displays by theirs, the statuette
once taken), 2524(12632226, 150), the counter shut, the mount. 7 of 7 floor arrows re-send all
four layers. Take [1730]: `human_pickuptable`, the map, `pick`, objbox "You reach and take a map
of the Museum.". Read [1732] the same frame with no seq. Drop [1736] is the generic drop.

### Others

| who | recorded | verdict |
|---|---|---|
| Torrcs (vm_advent_01) | [1266..1282], one of his five talks | moods set on that one; the other four are the wiki's |
| Marfet (vm_advent_02) | [1359..1368] | moods set; text already matched |
| Haig Halen | [1794..1796] | the E11 claim; his menu unchanged |
| dig-site cases | [1789] two Study clicks | no answer: the recorder walked to Haig first (no verdict possible) |

## Action inventory (rsprox_actions.py)

| first tick | action | verdict |
|---|---|---|
| 2 | welcome_screen:play | engine |
| 15 | tea_stall op2 | E14 (above) |
| 34, 196, 717, 755 | bank booths, bankmain/bankside buttons, GE clerks, GE offers | generic, not diffed this pass |
| 69 | bat_bones on dog_stray | E9: was unbound, now bound |
| 81 | area_task:taskbox op1 x12 | the journal (above) |
| 106 | journalscroll:close | engine close |
| 134, 339, 690, 983 | stamina potion drinks | the recorder's own kit |
| 172, 333 | apothecary op3 (Potions) | was unbound: interface 556 bound (above) |
| 331 | fai_varrock_chemisty_shelves2 op1 | clicked in passing, no answer before the next op (no verdict) |
| 332 | apothecary op1 | interrupted by op3 the next tick |
| 334 | apothecary_potions:strength | M1 |
| 479, 486 | poh_sawmill_opp op3 + skillmulti | E4, M10 |
| 490, 505 | gertrudefence op1 (Lumber Yard) | generic shortcut, quest_fluffs.rs2; not diffed |
| 498..500 | fai_varrock_dead_tree op1 x5 | E7 |
| 530, 1172, 1803, 1928 | varrock_teleport | M8 (claimed before this session: no line, as recorded) |
| 540, 565 | drop logs / newspaper | generic drop |
| 544 | Benny op1 | E8 |
| 555 | newspaper read | not diffed (Open) |
| 584..586 | bankside | generic |
| 612 | enchant_jewellery spellbook | the recorder's own (no cast followed) |
| 624, 625 | GE wall, GE clerk | generic |
| 826 | 0_48_53_freshfish op1 | E13 |
| 846 | fai_barbarian_12 op2 | generic (Barbarian Village), not diffed |
| 863, 873 | potterywheel, bowl on the oven | E10 |
| 993, 1004 | lumbridge_sc_fencejump | E6: was unbound, now bound |
| 1095 | fai_varrock_member_gater | generic gate, not diffed |
| 1106..1123 | soulbane rift, rope up | M6 |
| 1142..1156 | earth tiara wear, ruins, altar, portal | E12; the ruins and portal are generic runecraft |
| 1208, 1372, 1531, 1777 | museum stairs | the tests climb by `::goto`; the landings (3266,3455,1; 3254,3446,2; 3254,3442,1; 3266,3451,0) are not yet diffed (Open) |
| 1215, 1570 | Historian Minas | above |
| 1224..1443 | 34 timeline displays | above |
| 1247, 1538 | Torrcs; 1357 Marfet | above |
| 1456..1541, 1730..1736 | wall maps, museum map item | above |
| 1690..1713 | three museum lamps | above |
| 1789 | dig-site cases | no answer recorded |
| 1794 | curator | E11 |
| 1801 | magic tab | engine |
| 1814, 1825 | Toby, the lamp | above |
| 1880, 1883 | bank: grown cat, catspeak amulet | generic |
| 1899 | gertrude op3 (Kitten) | her refusal with a cat owned was missing: added |
| 1943 | logout | engine |

## Teleports

| tick | from -> to | cause | verdict |
|---|---|---|---|
| [495], [507] | 3308,3492 <-> 3308,3494/3491 | gertrudefence | generic (quest_fluffs), not diffed |
| [533], [1176], [1806], [1931] | -> 3213..3215,3422..3423 | Varrock teleport | engine landing r2, matched in the test (3213,3425) |
| [1001], [1006] | 3239,3335 -> 3240,3334; 3240,3334 -> 3240,3335 | the fence | matched (test lands 3240,3334) |
| [1120], [1123] | 3309,3452 -> 3105,5280; -> 3312,3450 | the rift, the rope up | the descent's frame now matches (L+1 face, L+2 face + camera, the climb, +4 the move); the rope up not diffed |
| [1146], [1161] | ruins -> 2657,4830; portal -> 3302,3477 | earth temple | generic runecraft, not diffed |
| [1213], [1375], [1536], [1781] | the museum stairs | maplinks | not diffed (Open) |

No cutscene: the only camera lines are the rift's ([1115]); no `cutscene_status`, no fade.

## Open

- **Hard 4..10 and elite bits**: on a varp no recording names; `~varrock_diary_bit` refuses them.
  A recording that claims one (any hard task but the first three) settles it.
- **Tier-complete menu close one tick early**: ours shuts Haig's menu on T (the strong queue's
  scan), Jagex on T+1.
- **Displays 17, 9, 20 and 3 once told**: no recording studies them after the Historian; they keep
  the recorded empty case. The wiki gives only fragments. A recording that studies the Observatory,
  Excalibur, Robert the Strong or the tome display after telling Minas settles each.
- **Minas's order for A Tail of Two Cats, What Lies Below and Defender of Varrock**, and their
  pages (the wiki's): no recording tells them. Also "Tell me about the Museum." and "No thanks.".
- **The other eleven areas' journals** still use the old summary; their wording is not recorded.
- **Generic interactions not diffed this pass**: the tea stall's "You attempt to steal..." line and
  `marketthief_tea` (170) varbit; woodcutting's "You get some logs." is spam on Jagex, a game line
  in ours; runecraft sends no "You bind the temple's power..." line on Jagex and a 92 (not 96)
  spotanim height; the museum stairs; the newspaper's Read; the Lumber Yard fence; bank and GE.
- **Unverified fallbacks**: the fence's level (wiki 13) and refusal; Apothecary Buy-5/X/All and a
  buy with an ingredient missing; Toby's full-pack line and "another platebody" (wiki); a lamp
  pick under its level; the counter in the basement.
