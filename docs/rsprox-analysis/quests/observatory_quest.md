# Observatory Quest vs RSProx index 4412

Recording: index 4412 (rev 241.1, world 611, 2026-10-29, "Observatory Quest"),
the whole quest: the goblin village [318..364], the reception [371..551], the
three hand-overs [584..662], the dungeon [755..1087], the mould and the lens
[1114..1178], the scene [1317..1482], the telescope and the reward
[1498..1591]. One playthrough: every roll (the sign, the key chest, the
assistant's greetings) is one sample. Ours: `test/quests/itgronigen.lua` (148
rows on scriptrun; the one FAIL is `quest.journal`, which no scriptrun run can
pass: the quest list is built by a clientscript) and `test/quests/_journals.lua`
(stages 2, 3, 4 and 6 row for row; also checked on scriptrun through
`::itgronigenjournal`).

Content: `quests/quest_itgronigen/` (`observatory_professor.rs2`,
`observatory_assistant.rs2`, `observatory_village.rs2` the goblins, the
signpost, the planets, the west door and the ambience, `observatory_stairs.rs2`,
`observatory_dungeon.rs2`, `goblin_guard.rs2`, `observatory_cutscene.rs2`,
`observatory_telescope.rs2`, `book_of_astrology.rs2` both books,
`itgronigen_journal.rs2`), `skill_crafting/scripts/glass/glass.rs2` (the lens),
`interface_book/scripts/book.rs2` (`~book_read_spread_obj`),
`quests/scripts/questscroll.rs2` (the scroll title), `questpoints.rs2` (the
jingle).

The port was LostCity's, i.e. the 2004 RSC-era quest: none of its pages were
the recording's (rsprox-content §16). Every page is now the recording's;
branches the recording never took are the wiki's (`Transcript:Observatory_Quest`,
`Transcript:Observatory_assistant`, `Transcript:Observatory_professor`), marked
`[wiki]` in the scripts with their moods unverified.

## Action inventory (`rsprox_actions.py`)

| first | n | action | verdict |
|---|---|---|---|
| 5 | 1 | welcome_screen:play | engine, login |
| 36 | 1 | Varrock teleport tablet Break | generic, bound (`teleport_tablet.rs2`) |
| 49, 573, 600 | 5 | vial, planks, bronze bar Drop | generic (drop); the planks and the bar were dropped to see the no-item pages |
| 188..252 | ~40 | bankers, bank ops, Grand Exchange, ring of dueling Wear and its teleport | generic, the recorder's errands |
| 312, 734 | 3 | opobj3 1dose2antipoison (village) | generic take of the `m38_49.spawn` obj; back 88 ticks later [405] |
| 318, 711 | 5 | opnpc1 qip_obs_drunken_goblin | this content, **was unbound**: pages [321..327] |
| 328, 721 | 3 | opnpc1 qip_obs_cooking_goblin | this content, **was unbound** [328..331] |
| 334, 726 | 2 | opnpc1 qip_obs_goblin_setting_a_fire | this content, **was unbound** [335..340] |
| 355 | 1 | oploc1 qip_obs_signpost | this content, **was unbound**: L+2 box, its click the player's page |
| 365, 369 | 3 | tree2, oaktree | generic woodcutting, superseded by the next click |
| 371..1203 | 6 | qip_obs_reception_door / _open | generic door (`doors.loc`): open 62, close 60, matched |
| 378, 384 | 2 | observatory_glassblowing_book take, Read | this content: the book was the 2004 text-less stub; now every spread and its item models [384..390] |
| 391, 394 | 2 | book_of_astrology take, Read | this content: LostCity's 2004 text replaced by the recording's [394..400] |
| 405 | 1 | oploc1 qip_obs_planets | this content, **was unbound**: L+2 "Oooooh, bizarre!" + fov_clamp + camera (100/100), reset at +4 |
| 415..1195 | 15 | opnpc1 qip_obs_proffesors_assistant | this content (rebuilt) |
| 417, 427 | 2 | star_notice_board | generic (interface 47); not checked |
| 434 | 1 | clue_noticeboard | generic (interface 808); not checked |
| 488 | 1 | fai_varrock_range | generic: "You haven't got anything to cook." |
| 493..1168 | 13 | opnpc1 observatory_professor_multi | this content (rebuilt) |
| 580, 612 | 4 | woodplank, bronze bar take | generic take |
| 607..1543 | 9 | questlist journal, close | this content: the journal (below) |
| 740, 1088, 1312 | 3 | stamina potion Drink | generic |
| 745, 750 | 2 | elem_crate_3/1 Search | generic: "You search the crate but find nothing." (not checked) |
| 755, 1224 | 2 | oploc1 qip_obs_vstairs2 | this content: the warning (cws_warning_9) **was missing**; landing matched |
| 765, 1229 | 2 | cws_warning_9:warn1 | this content, **was unbound** |
| 796..845 | 5 | the key chests (closed3, open3, closed2, open2) | this content (rebuilt shape) |
| 840, 1036 | 2 | keep_key, lens_mould Destroy | this content: confirmdestroy with the recorded notes |
| 907 | 1 | opnpc1 qip_obs_goblin_guard | this content (rebuilt: delete and add, not a transform) |
| 912..957 | 5 | opnpc2 goblin_guard | this content (fixed: the retaliation re-queue) |
| 922, 1000 | 2 | prayerbook prayer15 | the recorder's kit |
| 923..1056 | 5 | keepgate_closed, key on it | this content (rebuilt: walk-through, unlocked for good) |
| 987, 989 | 2 | combat/sailing side panel switch | engine UI |
| 992 | 1 | bolt take (the guard's drop) | generic |
| 1004 | 1 | macro_countcheck_surface | random event (Count Check), generic |
| 1016..1051 | 4 | qip_obs_dungeon_stove_top_multi, mould on it | this content (rebuilt; "Just a plain stove. Nothing there.") |
| 1067..1319 | 4 | qip_obs_stairs1_dungeon | this content: east to the village, south-west into the scene |
| 1153 | 1 | molten glass on lens mould | this content (`glass.rs2`, rebuilt boxes) |
| 1484 | 1 | hh_medium009 (STASH) | generic: "You need a saw and a hammer..." (not checked) |
| 1491 | 1 | oploc1 qip_obs_door_right | this content, **was unbound**: the line as message and box |
| 1498 | 1 | oploc1 qip_obs_stairs1 | this content: now lands 2442,3159,1 (was ~climb on the same tile) |
| 1505..1548 | 3 | opnpc1 observatory_professor_multi2 | this content (rebuilt) |
| 1525 | 1 | oploc1 qip_obs_tele_gear_upper_multi | this content (rebuilt shape and model) |
| 1539 | 1 | qip_obs_telescope:close_button | this content |
| 1588 | 1 | questscroll close | engine |
| 1597 | 1 | logout | engine |

## Teleports

| tick | from -> to | cause | verdict |
|---|---|---|---|
| 40 | -> 3213,3423 | Varrock teleport tablet | generic |
| 255 | -> 2440,3088 | ring of dueling | generic |
| 766, 1230 | 2458,3185 -> 2355,9394 | down the village stairs, after the warning | matched (L+2 the modal, the teleport the tick after "Proceed regardless") |
| 1087 | 2355,9394 -> 2458,3185 | the dungeon's east stairs | fixed: L+3 (was the op tick) |
| 1326 | 2335,9350 -> copy of 2438,3163 | the south-west stairs, the scene | built (below) |
| 1417 | copy, to the floor above | the scene | built |
| 1478 | copy -> 2438,3163,0 | the scene's end | built, landing checked |
| 1501 | 2444,3162,0 -> 2442,3159,1 | qip_obs_stairs1 | fixed (no maplink row lands there) |

## Stages and vars

| var | value | tick | with |
|---|---|---|---|
| observatory_starsign (3828) | 2 | [550] | the start (was rolled at the telescope: moved) |
| itgronigen (112) | 1 | [550] | the professor's last page before "Okay, I'll be right back." and the start line |
| observatory_chestchoice (3827) | 2 | [550] | the start |
| itgronigen | 2, 3, 4 | [591], [623], [655] | the page after each hand-over |
| observatory_gatelock (3826) | 1 | [996] | the key in the gate |
| observatory_mould_pres (3837) | 1 / 0 / 1 | [1027], [1040], [1047] | the mould taken, destroyed, taken |
| itgronigen | 5 | [1146] | the glass given back |
| itgronigen | 6 | [1176] | the fade's box |
| cws_warning_9 (3859) | 1, 2 | [762], [1227] | each warning |
| observatory_cutscene_seen (3838) | 1 | [1475] | the scene's last fade |
| observatory_scopelooked (3836) | 1 | [1532] | the telescope interface |
| itgronigen | 7 | [1573] | "By Saradomin's earlobes!" (with qp and the items) |

Every quest var sits on `itgronigen` (112) or `itkeepgatelock` (113), both
`transmit=yes, scope=perm` in `quest_itgronigen.varp`; `cws_var` (1045) is the
stronghold's override. Stage 8 (the wine) is the multinpc's, not recorded.

## Pages

Every recorded page (`rsprox_pages.py`) is in the scripts verbatim with its
mood, including the assistant's double space ("There are  some at the
Barbarian Outpost"), the professor's `chatneu_idle` "..." [542], and the
assistant's and professor's pages inside each other's talks with their own
chatheads (5365; the rung 5366 on the assistant's side). Menus in recorded
order: the professor's two-line menu on every visit, the assistant's menu
that drops the topic just asked ([447], [454], [460], [475]). The assistant's
greeting is a random one of four at stages 1..5 (three of them recorded).

## Shapes

| interaction | R | ours | verdict |
|---|---|---|---|
| a hand-over [589..591] (x5) | the page's click: chat shut, player and professor `dream_player_feed_cyrisus_standing`; +1 the stage, the item, the next page | same (tick log: both anims on one tick) | matched |
| the professor gone ahead [1174..1178] | the click: fade out, chat shut; +2 stage 6, lens and mould gone, fade in, box; its click shuts the overlay | same | matched |
| chest open [802..804] | L+1 face; L+2 line, `human_openchest`, `chest_kicked_open`, chest_open (52); L+3 the open chest | same (233/234) | matched |
| chest search [806..811] | O `generic_look_and_search`; O+2 the line / the key's box | same (237/239) | matched |
| guard wake [907..910] | O both wake seqs; O+2 the sleeper deleted, a goblin_guard added facing north, its line, its first swing | the add and the line on O+2, the first swing O+3 | **1 tick late** (the add is in the player phase; the swing is the npc phase's): Open |
| guard fight | swing every 4 ticks through five clicks; spear seqs; goblin_attack/hit/death | every 4 ticks; spear seqs (were the unarmed set: `npc_combat/g/goblin_guard.combat` authored) | fixed |
| gate with the key [994..998] | L+2 gatelock, two lines, the key, the player's page, inviswall x2 + inactive leaves x2 for 2 ticks, iron_door_open (71), the step | same; the second pass [1056..1060] the same with no lines | matched (the second pass's revert needs 103749aee's loc_add revert fix) |
| stove [1016..1027] | L+2 box; its click box 2 + `human_openchest`; its click the overhead; +2 the mould | same | matched |
| lens [1153..1158] | box with no continue row; +2 the lens and the box again | same (`~observatory_objbox_held`) | matched |
| village stairs [755..766] | L+2 count + modal; "Proceed regardless": close, +1 teleport | same | matched |
| telescope [1525..1539] | L+2 held; L+4 face 2441,3161; L+5 line + `look_through_scope`; L+7 scopelooked, model, mainmodal open (0, 0), the interface | same | matched |
| completion [1573..1591] | items, stage, qp on "By Saradomin"; scroll + jingle 152 on the last click; the level-up after the scroll closes | items and stage there; the xp and qp with the scroll; jingle 152 (was 153); level-up after the scroll | xp/qp 8 ticks late, see Open |

## The scene [1317..1482]

Copy-and-lock frame as Black Knights' Fortress's, from the copy's raw tiles
(raw 2438,3163 -> 12806,14107). Per beat, ours against the recording measured
from the lock (the wave is click-paced): scratch head wave+6 / +6, hammer
+15 / +15, "In goes the lens." +24 / +24, `professor_fitting_lens` + 3801
(delay 10) +1 / +1, "And one final adjustment." +18 / +18, the screwdriver +
3800 +10 / +10, "And all our work pays off." +4 / +4, the cheer +3 / +3, the
area's track at the end +17 / +17. The professor's walks: 28 steps, the same
run lengths (`npc_movement_audit.py paths`). Music: Look to the Stars (368,
0/60/60/0) with its unlock line at the lock, the area's 80 at the end. The
camera lines are copied exactly; scriptrun has no camera reader, so the
`t.cutscene.await` rows run on the live client only.

## Ambience

| what | R | ours |
|---|---|---|
| the campfire: camp_fire_burst 2466,3183 + observatory_goblin_spit (3803) from 2465,3183, range 5 | every 76 ticks | every 76 ticks |
| the cook's pan: fry (2577) from 2467,3183, range 5 | on multiples of 75 | once per 75 |
| drunk / cook / kid / fire-setter overheads | the recorded lines (with their ready seqs) | the same lines, picked at their own rates (UNVERIFIED cadence) |
| the dungeon's two women | ten lines in turn, 5 ticks apart | the same ten in the recorded order, off the clock |
| the lost goblins | a line every 10 ticks, routes a tile a tick | the same; routes `patrol1..` from the recorded turns |
| facings | spawnangles; the cooks face each other, the gossip and the guard face a tile | `[ai_spawn]` faces |

## The journal

Recorded at stages 2 [607], 3 [634], 4 [674], [1122] and 6 [1180], [1208],
[1512], [1544]; `_journals.lua` checks 2, 3, 4 and 6 row for row (plain text;
the first rows' tags checked on a TORIRSSERVER_TRACE_OUT=2 run). Stage 4's
wording differs from 2's ("observatory", no commas), as recorded. Stages 1, 5
and 7+ are UNVERIFIED (no recording opens them).

## Open

- The telescope's models: Sagittarius is the recording's 27056; the other
  eleven are the cache's other constellation models in id order, unverified.
- The guard's first swing is one tick after his add (Jagex: the same tick).
- The completion's crafting xp and quest points go out with the scroll, 8
  ticks after Jagex's [1573]: our level-up queue waits behind a main modal
  but not behind chat pages, so granting it on the recorded page put the
  level-up box over the professor's last pages.
- The kitchen gate's second pass relies on the engine's loc_add revert fix
  (103749aee, "loc_add reverts to the map's loc"): without it the second
  revert deletes both leaves.
- Not recorded, kept from the wiki: every no-item branch, the non-normal
  planks, the key's pages, the wrong constellation, a full backpack, the
  assistant's name and wine after the quest. LostCity's Scorpius grave, ghost
  and spirit are 2004 content, untouched and unverified.
- The patrol1 walk at [1294..1311] joins its route somewhere no recording
  shows.
- star_notice_board, clue_noticeboard, the crates, the STASH: generic, not
  checked here.
