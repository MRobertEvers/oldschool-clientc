# Recipe for Disaster: Freeing the Goblin generals (RSProx index 4402)

Recording: index 4402, rev 241, "Scrubfoot's Cave and RFD 1 - Wartface and
Bentnoze" (default transcript `~/.rsprox-index-cache/home-default/.rsprox/binary/archive/idx4402.txt`,
raw under `home-raw`). Every bracketed tick below is 4402's. Ported 2026-10-29.

Ours: `OSRS-Content/osrs-content/server/scripts/quests/quest_recipefordisaster/scripts/recipefordisaster_goblins.rs2`
(the whole subquest), `recipefordisaster_journal.rs2` (`~rfd_goblins_journal`),
`recipefordisaster_intro.rs2` (the completion procs split, the empty chair in a
fresh frozen-room copy, the save migration), `quests/quest_gobdip/scripts/quest_gobdip.rs2`
(the generals' RFD branch), the ingredient arms in `belowicemountain.rs2`
(`[opheldu,bread]`), `gnome_seasoning.rs2` (`[opheldu,orange_slices]`,
`[opheldu,gnome_spice]`), `dye_cape.rs2` (`[opheldu,bluedye]`), and
`configs/recipefordisaster.npc` (the three cooks). Test: `test/quests/rfd_goblins.lua`.

The old file was a Quest Helper port with invented text (a mesbox-narrated
explosion, a one-visit ingredient talk, its own stage varp 6944 at 1..5).
None of it survived.

## Stages and vars

| var | value | when (4402) | ours |
|---|---|---|---|
| `100goblin` 1867 (varp 679 bits 9-14, perm, sent) | 0 -> 5 | Aris's last page after the inspect [153] | `~rfd_goblins_inspect`, set before that page |
| | 5 -> 10 | Mudknuckles' "Is called charcoal..." page [1107] | set before the page |
| | 10 -> 15 | the click closing "Must be moving. Follow me." [1165] | `~rfd_goblins_explosion`, first line after the close |
| | 15 -> 30 | "Yep, 'bout sums it up..." [1341] | set before the page |
| | 30 -> 35 | the slop's objbox [1510] | set before the objbox |
| | 35 -> 40 | the slop used on a general [1750] | `~rfd_goblins_use` |
| `100goblin_explosion` 1868 (varp 679 bits 15-16) | 0 -> 2 | the village shot of the scene [1220] | same tick of the scene (ours tick 234 = 1220) |
| `100goblinsvisible` 1859 | 1 -> 0 | [1750] | `~rfd_goblins_use` |
| `hundred_subquest_tally` 1852 (varp 678) | 0 -> 1 | the scroll's close [1758] | `[queue,rfd_goblins_scroll]` after the close |
| qp 101, `subquests_completed_count` 13987 | +1, +1 | [1750] | `~rfd_subquest_points` on the use-on tick |
| disease 456 | -50, 0 | [1751] | the scroll queue |

Varp 679 and 678 are already `perm`, `transmit=yes` in `recipefordisaster.varp`
with the generated scope/transmit lines deleted. Ours on the test's wire (tick
log varp 679): 5 [36], 10 [159], 15 [179], explosion [234] (55 ticks after 15,
as 1165 -> 1220), 30 [340], 35 [409], 40 and visible 0 [469], tally [471].

The private stage `%varp6944_rfd_goblins` is retired (`transmit=no`) and read
once at login by `[proc,rfd_goblins_login_migrate]`: 1 -> 5, 2 -> 10, 3 -> 30
(our old 3 covered the explosion and the demands), 4 -> 35, 5 -> 40 with
visible 0 and the tally +1; 15 and up also set the explosion varbit. Every
reader moved to 1867 with Jagex's values (`quest_cheat.rs2`, also writes 1868 and
1859; `poh_quest_status_generated.rs2`, hand-edited because the generator stops
on another quest's arm, `quest_demonslayer`; `recipefordisaster_finale.rs2`;
`recipefordisaster_selftest.rs2`, whose goblin bmp procs now show recorded
text; `tools/quest_gate/quest_inventory.tsv`). The dbrow's `endstate` is 40, so
the quest list and 3757's goblin row now read our stage.

## Action inventory (`rsprox_actions.py`), every row

| tick | action | verdict |
|---|---|---|
| [20] | welcome screen play | generic login |
| [50], [61]..[89], [233]..[346] | bank booth, bank deposits/withdraws, GE buys (gnome spice, blue dye, bread, orange, bait, bucket of water, charcoal) | generic, not this content (banking, GE: other owners); the test gives the same items in setup |
| [53] x14, [99] x11 | quest list op, journal close | this content: the journal (below) |
| [74], [79], [80] | stamina potion combine, drop vials | recorder's own kit |
| [89], [103], [178], [192], [364], [1581], [1587], [1681] | teleport tablets | generic, other owner (tablets) |
| [112] | `hundred_lumbridge_doubledoorl` | rfd_start (done), passed in the test as the Cook's door |
| [120] x3, [1720] | Aris Talk-to | rfd_start (done) |
| [135] x2 | `hundred_goblin2_base` Inspect | this content: matched |
| [436]..[674] | goblin soldiers, catwalk goblin (Grubfoot), poor doors, `goblin_ladder_*` | other agent's scope |
| [461] x5, [535] | General Bentnoze / General Wartface Talk-to | this content: matched (all four answers) |
| [512] | kitten pick-up | recorder's own pet |
| [681]..[1028] | xmas20 Scrubfoot's cave, sled, world map | other agent's scope |
| [1040] x3 | `100_goblin_ladder_down` | this content: matched |
| [1054] x6 | `goblin_cook` Talk-to | this content: matched |
| [1118], [1147] | drop charcoal, take it back | generic drop/take (not diffed; the recorder's test of the menu) |
| [1270] x11 | `goblin_cook_charred` Talk-to | this content: matched (all six answers) |
| [1362] | bucket of water on bread | this content: matched (line, slots) |
| [1375] | knife on orange, "Slice it" | generic `cutting_fruit.rs2`: matched (skillmulti, "You cut the orange into slices." at the choice + 1) |
| [1447], [1455] | soggy bread / orange slices on Mudknuckles | engine default "Nothing interesting happens.": matched (no binding) |
| [1459] | blue dye on blue dye | generic: Jagex sends nothing; ours says "Nothing interesting happens." (OPEN, engine/dye script, not this content) |
| [1474] | gnome spice on fishing bait | this content: matched (both lines, spice kept) |
| [1496] | blue dye on orange slices | this content: matched (line, dyed slices in the dye's slot) |
| [1524] x3 | `100_goblin_ladder_up_charred` | this content: matched |
| [1541], [1569] | `percy_sacks` | other agent's scope |
| [1701] x5, [1793] | the Cook's door, barrier 1 | rfd_start (done) |
| [1731] | `hundred_goblin1_base` Inspect (stage 35) | this content: matched (same pages) |
| [1750] | slop on `hundred_goblin1_base` | this content: matched |
| [1758] | quest scroll close | this content: matched on the server (client OPEN, below) |
| [1796] | `qip_cook_trapdoor_open` | generic, not this content |
| [1809]..[1827] | `hundred_goodchest` Bank / Collect / Buy-items / Buy-food, the chest's shop (gauntlets 1-2, spoon, whisk) | OPEN: the scroll's "Increased Culinaromancer's Chest Access" reward; our tree binds only op1 (bank) on `hundred_goodchest_base` |
| [1833] | logout | generic |

## Teleport inventory

| tick | cause | shape (4402) | ours |
|---|---|---|---|
| [114], [1701], [1794] | Cook's door / double doors / barrier 1 | rfd_start | rfd_start (test passes both ways) |
| [1044] | `100_goblin_ladder_down` | face L+1 [1042], `human_reachforladder` + busy L+2 [1043], into a fresh copy of square 46_154 at (2981,9915) L+3, cook added at (2982,9909) facing south | same shape; from a standing start anim on C, move C+1 (test [110]/[111]) |
| [1195] | the scene's lock | into a copy of the village (46_54) at (2968,3506), the kitchen copy and its cook gone | same tick (ours 209) |
| [1230] | the scene | into a new copy of 46_154 at (2987,9871), the village copy and soldiers gone | same tick (ours 244) |
| [1527], [1539], [1567] | `100_goblin_ladder_up_charred` | same three-tick shape, to (2960,3506) | matched (test lands 2960,3506 twice) |
| [1535], [1549] | ladder down after the explosion | a new copy of the CHARRED kitchen at (2981,9875), charred cook at (2980,9871) | matched (copy 37,19) |
| others | tablets, goblin ladder, xmas20 cave | not this content | |

`100_goblin_ladder_up` (the unburnt kitchen's ladder) is never climbed in 4402:
same shape assumed, UNVERIFIED. With the instance pool full the ladder puts the
player in the world's own kitchen (the m46_154 spawns), a fallback no recording
shows.

## The frozen generals [135]..[156], [1731]..[1762]

| R | ours | verdict |
|---|---|---|
| op at the arrival [135]/[137]: player chatquiz1 "Which one of these am I actually meant to be saving?", three Aris pages (chatneu2/3/2), player chatneu2, Aris chatneu4 with the stage 5 | `[oploc1,hundred_goblin1_base]`/`2_base` -> `~rfd_goblins_inspect` | matched (test `inspect.pages`) |
| stage 35 [1734]: the same pages, no stage | same proc, stage written only below 5 | matched |
| [1750] slop on a general: stage 40, visible 0, qp, count, face, `loc_del` (6,15), (6,13), (6,14), `empty_chair` rot 3 at (6,14), the mush gone | `~rfd_goblins_use` | matched (tick log [469]: two dels, the empty chair) ; Jagex's third `loc_del` of the chair slot is ours' `loc_add` replacing it |
| [1751] disease, song 147 (zero envelope), jingle 152 `quest_complete_1`, the scroll "You have freed the Goblin Generals!", the mush at zoom 400 (78,128), "1 Quest Point / 1,000 Cooking XP / 1,000 Crafting XP / 1,000 Farming XP / Increased Culinaromancer's / Chest Access" | `[queue,rfd_goblins_scroll]` | matched (ours [470] song + jingle) |
| [1758] the close: tally 1, Aris chatneu3 "Good work adventurer, I've teleported them away to safety! You only have 7 council members left to protect!" | same; "7" is `8 - tally` (other tallies UNVERIFIED) | matched on the server (the echo is in the test's chat) |
| [1762] the page closed: the xp (Cooking, Farming, Crafting), level-ups [1763], [1766] | `[queue,rfd_goblins_xp]`, a normal queue that waits for the page | matched in order; ours [472] |
| a frozen room entered after [1750] | `~rfd_frozen_cast` runs `~rfd_goblins_empty_chair` when freed | UNVERIFIED (4402 never re-enters) |

## The generals in the village [461]..[565]

Recorded at stage 5 only, six talks, either general. No argument first: the
menu on the op tick "Can I ask about what food you like? / So how is life for the
goblins?". Ours: `~rfd_goblins_generals_talk`, asked by
`[label,gobdip_talk_current_general]` once Goblin Diplomacy is complete, for
stages 5..35 (10..35 UNVERIFIED). The player faces each speaking general's
tile per page (`~gobdip_says`), as recorded.

| answer | pages (moods) | verdict |
|---|---|---|
| food -> "Market research." [470] | hap1, Wartface con1, hap2, Bentnoze neu1, hap1, Bentnoze ang1 | matched |
| food -> "Erm... no particular reason." [502] | shifty1, Wartface laugh1 | matched |
| food -> "I need to save you..." [521], [541] | neu1, Bentnoze quiz1, con2, Wartface neu1, neu2, Bentnoze ang1, quiz1, Bentnoze ang1, Wartface ang1, shock1, Bentnoze ang1, bored1 | matched |
| "So how is life for the goblins?" [490] | con1, Wartface bored2 "Now you've solved our argument, we gotta think...", Bentnoze bored1 | matched. Goblin Diplomacy's own completed talk (`goblin_diplomacy_completed`, "...argument we gotta...", neutral) is left as it was (not this content) |

Bentnoze turns to the player once [473] (`[facing] player=`); ours does not set a
face on the generals (OPEN, minor).

## Mudknuckles before the charcoal [1054]..[1110], [1134]..[1164]

Every talk opens "Go away human, me's busy here!" (ang1), "Busy doing what?"
(quiz1), "Me's working on new way of cooking..." (hap2), then the menu. All
recorded branches matched in the test (`cook.newway`, `cook.leave`,
`cook.later`, `cook.charcoal`, `cook.explosion`):

| choice | R | ours |
|---|---|---|
| "What is this new way of cooking?" [1061] | hap3, hap4, then "I need your help... / Well, good luck with that." -> neu1, hap2 | matched; "I need your help..." from that menu UNVERIFIED (assumed the same help branch) |
| "I need your help..." [1087] | shock2, shifty2, shifty3, "What do you need...? / I guess I'll come back later then." | matched |
| -> "What do you need?" [1106] | quiz1, stage 10 with neu3 "Is called charcoal...", neu3, neu1 | matched |
| -> "I guess I'll come back later then." [1096] | the player's page with NO head animation (`if_setanim -1`) | matched (`~chatplayer_anim(-1, ..)`) |
| "Well, I guess I'll leave you to it." [1077] | neu1, neu1 "Yes, Mudknuckles too busy to talk." | matched |
| stage 10, charcoal carried [1152] | "I've got the charcoal you were after." leads a 4-row menu; hap1, hap2 "...Sulphur 'n stuff.", shock1, neu1 "Must be moving. Follow me." | matched |
| charcoal carried at stage 5 [1059] | not offered | matched |

Stage 0 (never recorded): the one "Go away human" line, UNVERIFIED.

## The explosion [1165]..[1259]

Recorded tick by tick from the raw transcript (`rsprox_cutscene.py` drafted it);
ours runs from the click, so ours = R - 986 in run 6's tick log. Every row:

| R | event | ours | verdict |
|---|---|---|---|
| 1165 | stage 15, charcoal gone, player one step north (walk), cook W then NW [1165], [1166], `[facing] type=reset` | 179: stage, charcoal, step, face reset; the cook covers both steps on 179 | OPEN: the cook arrives a tick early (two tiles on the click tick) |
| 1167 | cook faces (2981,9910) | 181 | matched |
| 1168 | player faces (2982,9910); `cam_moveto` (2979,9914) 325 0/100, `cam_lookat` (2981,9911) 325 | same tick | matched (camera from the script, not in the tick log) |
| 1169, 1178, 1184 | player says | `say` | matched |
| 1174 | cook says "Absolutely, best idea Mudknuckles ever had." | 188 | matched |
| 1182 | `100_goblin_cauldren_shake` at (2980,9910) shape 10 rot 0, synth 1024 `100_cauldron_shake` | 196 | matched |
| 1187 | `cam_shake 3,1,0,0` | same | matched |
| 1188 | `emote_panic` | 202 | matched |
| 1191..1192 | player runs 2 tiles a tick east to (2987,9910) | 205, 206 (`p_temprun`) | matched |
| 1193 | cook `100_goblin_thrown_back` delay 12, says "It fine, it...", `100_goblin_cauldren_blast`, synth 1021 | 207 | matched |
| 1194 | white fade out (16777215,255 -> 0, 15) | same | matched |
| 1195 | the lock (`~mist_cutscene_lock` + hud lock), village copy, cook `[del]` | 209 | matched |
| 1196 | `goblin_red_soldier_7` at (2954,3502) south, fade in 50, synth 2425 `whiteout`, `cam_reset`, cam (2959,3508) 475 / (2955,3505) 150 | 210 | matched |
| 1197..1198 | soldier 7 two steps north, reset on the first | 211, 212 | matched |
| 1200 | soldier 7 faces the player, says "What that noise?"; `goblin_green_soldier_3` at (2954,3509); synth 1020 | 214 | matched; ours sends a face reset on 215 that Jagex does not (OPEN, below) |
| 1201..1204 | soldier 3 four steps south | 215..218 | matched |
| 1205, 1209, 1213, 1222 | the soldiers' says | 219, 223, 227, 236 | matched |
| 1207 | soldier 3 faces the player | 221 (+ reset 222, OPEN) | matched |
| 1214 | `cam_shake 3,1`, synth 1023 | 228 | matched |
| 1217 | green 2 (2957,3508), green 6 (2956,3500), red 4 (2959,3499), all south; cam (2960,3519) 1000 / (2955,3505) 250 | 231 | matched |
| 1219 | `map_anim 100_goblin_cauldren_roof_flames` (2960,3504) delay 20; `map_projanim_v2 100_goblin_cauldren_tumble` (2961,3505) -> (2943,3503) 25/115 angle 25 heights 150; `cam_shake 3,3` | 233 (raw heights via `projanim_map_raw`; the target is a tile past the copy's west edge, kept) | matched |
| 1220 | explosion varbit 2; `cam_shake 3,5` | 234 | matched |
| 1221 | `cam_shake 3,1` | 235 | matched |
| 1222 | `cam_reset` + the same camera | 236 | matched |
| 1228 | white fade out 35 | 242 | matched |
| 1230 | ops hidden, the charred-kitchen copy at (2987,9871), soldiers `[del]`, cam_shake x5, 6021, 5485, minimap 2 | 244 | matched |
| 1231 | player faces (2980,9871), `terrified_emote`; `goblin_cook_on_wall` (2981,9866) north, faces (2981,9874) | 245 | matched |
| 1232 | fade in 50, `whiteout`, cam (2980,9871) 600 / (2986,9871) 175 | 246 | matched |
| 1236 | `cam_moveto` (2972,9871) 600 rate 1/20 | 250 | matched |
| 1241 | "Mudknuckles?" | 255 | matched |
| 1244 | cam (2981,9872) 525 / (2981,9866) 75 | 258 | matched |
| 1246 | `100_goblin_fall_from_wall`, synth 1028 | 260 | matched |
| 1251 | transformation to `goblin_cook_charred`, level, ops | 265 (`npc_changetype`) | matched |
| 1252, 1254 | "It...", "It worked!" | 266, 268 | matched |
| 1257 | `[sequence] id=-1` | 271 | matched |
| 1258..1260 | steps N, N, NE to (2982,9869), reset on the first | 272..274 | matched |
| 1259 | the unlock, "It did... ?!", `cam_reset`, hud unlock (xp drops, 6021, `cam_unlock false`, `cam_reset`, sides, 5487, minimap 0) | 273 | matched; the fade overlay stays mounted after, as recorded |
| music | Jagex keeps "Too Many Cooks..." (386) through both copies | ours re-asks the region at each teleport: 313 at 209, 386 at 244 | OPEN (engine music on a cutscene's copy moves) |

The test checks the camera ops and heights (`t.cutscene.await`: moveto 325,
475, 1000, 600, 525, reset).

## Mudknuckles after the explosion [1270]..[1558]

Every talk: hap4 "You the human who helped me...", hap2 "I thinking of calling it
fast food...", then "That sounds like a great idea. / Do you think that will
sell? / <third> / I'll leave you to your experimentation then." Each answer ends
the talk. All matched in the test.

| choice | stage | R | verdict |
|---|---|---|---|
| great idea [1277] | any | hap1, quiz3, quiz2, con2 | matched |
| will it sell [1297] | any | quiz1 "...Like you said, it's burnt.", neu3 | matched |
| leave [1290] | any | neu1 | matched |
| "I need your help..." [1318] | 15 | the demands, 24 pages, stage 30 with "Yep, 'bout sums it up" | matched |
| "I need your help..." [1405] | 30 | neu2 "...About that dish...", then a menu of the three problems, one answer (sad3 / sad2 / sad2) | matched, all three |
| "I've got the ingredients we need..." [1502] | 30, all three held | hap2, con3, neu2, stage 35 + objbox (mush) "The goblin hands you a bowl containing some kind of indescribable grey mush.", hap1 "Great, thanks!"; the three in, the mush in the dyed slices' slot | matched |
| "I need your help..." [1557] | 35 | quiz2, sad2 "Highly unlikely..." | matched; stage 40 UNVERIFIED (assumed the same) |

The cook never wanders in 4402 (every copy's cook stands where spawned; one step
NW two ticks after the [1549] spawn is the only other move, one sample, not
built). `recipefordisaster.npc` gives all three forms `wanderrange=0` and
`moverestrict=normal` (they must walk in the scene; the old `nomove` stopped
`npc_walk`), the charred and on-wall forms `defaultmode=none`; the generated
block's `nomove`, `defaultmode=wander`, `wanderrange=5`, `moverestrict`,
`nomove=0` lines for them are deleted. The goblin_cook block moved out of
`lumbridge.npc`.

## The ingredients

| R | ours | verdict |
|---|---|---|
| [1362] water on bread: "You soak the nice crispy bread until it becomes slimy to the touch.", soggy bread in the bread's slot, empty bucket in the bucket's | `~rfd_goblins_soak_bread` from `[opheldu,bread]` | matched |
| [1474] gnome spice on bait: "Your reward is: <col=ff0000>1</col> x <col=ff0000>Spicy maggots</col>." then "You add a light dusting of spices to the maggots.", maggots in the bait's slot, the spice kept | `~rfd_goblins_spice_bait` from `[opheldu,fishing_bait]` and `[opheldu,gnome_spice]` | matched; `spicespot` UNVERIFIED (assumed the same lines, used up) |
| [1496] blue dye on slices: "You add the dye to the oranges. They turn an appealing greyish brown colour.", dyed slices in the dye's slot | `~rfd_goblins_dye_oranges` from both click orders | matched; other dyes no longer work (not recorded) |

## The journal (every stage recorded)

4402 opens it at 0 [56], 5 [166], 10 [1122], 15 [1303], 30 [1348] [1369], 35
[1520], 40 [1766]. Extracted per open with a small script over the transcript
(qj rows with the 100goblin value). Current journal format: no blank rows,
every finished row `<str>`, the current step blue with names red, the first row
blue until 40. At 30 each ingredient row is struck while its product is carried
([1369]: the soggy bread). Some steps change wording when finished: 10's "I met
a cook named Mudknuckles..." becomes "I met a cook in the village...", 35's
"...use to protect Wartface and Bentnoze from the culinaromancer's spell."
becomes "...use to free Wartface and Bentnoze back in Lumbridge castle.".

Ours: `~rfd_goblins_journal`. The test reads every stage row for row (plain
text): 5, 9, 12, 15, 21, 25, 29 rows, all match. The tags were checked on a
`TORIRSSERVER_TRACE_OUT=2` run: every struck row starts bare `<str>` as Jagex's
do. The engine's split carries an open colour across a `|`, which had put
`<col=000080><str>` (struck rows drawn navy) and `<col=000080>` on the blank row;
each coloured paragraph now ends `</col>` (also in the shared
`~rfd_subquest_unstarted` / `~rfd_subquest_req`, so every guest's unstarted
page starts its rows as recorded). Our rows carry a trailing `</col>` Jagex's do
not (same drawing).

## Tests

`python3 tools/quest_gate/run.py rfd_goblins --timeout 1200` -- in this session
the shared script pack would not build (another session's
`goblin_village/goblin_soldiers.rs2` calls `npc_huntnext`,
`game_sledracing/sledracing.rs2` and `player/logout.rs2`'s `sled_on_logout` do
not compile), so it ran from a private pack: a symlink mirror of osrs-content
without those two new files and with HEAD's logout.rs2, `sscompile` into a
scratch pack, `cachepack pack --server-only` for the server pack, and run.py
imported with its pack ensures stubbed, `TORIRSSERVER_SCRIPTS`/
`TORIRSSERVER_PACK_DIR` set. Result: 91/91 PASS (final run, the GE items given
in setup as the recorder bought them; `lint_quest.py` clean). The tick log of
the run before it, with `TORIRSSERVER_TRACE_OUT=2`, is the one audited above.
A PASS publishes its shots to
`server/scripts/selftest/quests/quest_recipefordisaster_goblins/play`.

## Open

- The cook's two steps to the cauldron land on the click tick (Jagex: one a
  tick, [1165], [1166]).
- The soldiers' faces at the player get a reset a tick later on our wire
  ([1200], [1207]: Jagex sends none). `playerface` would hold it, but it drops
  at the player 14 tiles from their spawn (maxrange 7) and hands them to their
  default wander (seen on run 4: soldier 3 walked off).
- Music: Jagex keeps track 386 through the scene's two copy moves; ours
  re-asks the region at each (313 for 35 ticks). Engine.
- The scroll's close: our client sends CLOSE_MODAL after the op and Aris's page
  goes with it (the same open as rfd_start.md). The server sends it; the xp no
  longer depends on it (a normal queue).
- Level-ups: Jagex shows each skill's level-up box in turn ([1763], [1766]);
  ours plays one jingle for the batch. Engine-generic.
- The Culinaromancer's Chest ([1809]..[1827]): Bank, Collect, Buy-items,
  Buy-food and its tiered shop are the reward's feature ("Increased
  Culinaromancer's Chest Access"); our tree binds only op1 Bank on
  `hundred_goodchest_base`. Not built.
- Blue dye on blue dye [1459]: Jagex says nothing, ours "Nothing interesting
  happens." (dye_cape.rs2 default; not this content).
- Bentnoze turns to the player once in the generals' talk [473]; ours sends no
  face for the generals.
- Unverified (no recording): stage 0 Talk-to for Mudknuckles, the generals'
  menu at 10..35, "I need your help..." from the cooking sub-menu, stage 40
  talks, `spicespot`, the ladder up from the unburnt kitchen, the frozen room
  re-entered after the generals are freed, Aris's count at other tallies, the
  Goblin Diplomacy requirement not met (no gate built).
