# Children of the Sun (and Travel to Varlamore) vs RSProx index 4408

Recording: index 4408 (rev 241.1, world 611, 2026-10-29, "Children of the Sun
and Travel to Varlamore"): the whole quest from Alina's first page [178] to the
scroll [989], Tobyn afterwards [998], Regulus Cento's first flight to Fortis
[1048]..[1081] and the flight home [1085]..[1098], then logout [1175]. One
playthrough. A second, index 1621 (rsprox.net 1637, rev 226, 2024-10-20),
has the tail that got through: the same route, the same stop timings and the
recorder in the same five hiding places; its text is older ("You failed to
stay close enough to Carl."). Ours: `test/quests/childrenofthesun.lua` (82 rows on scriptrun; 81
pass, the one FAIL is `quest.journal`, which fails on every quest under
scriptrun because the quest list is built by a clientscript).

Content: `quests/quest_childrenofthesun/` (`childrenofthesun.rs2` the talks,
the marks, the roof, the journal; `cots_scenes.rs2` the three scenes and the
tail; `childrenofthesun.constant`), `quests/quest_twilightspromise/`
(Regulus Cento's first flight and the flight home), `areas/varrock/`
(`nevet.rs2`, `varrock.spawn`), `general_use/scripts/bookcases.rs2` (the
Varrock shelf), `skill_cooking/scripts/cooking.rs2` (a range with nothing to
cook), `interface_questjournal/scripts/quest_overview_start.rs2`,
`quests/scripts/questpoints.rs2` (the jingle), `player/logout.rs2`.

The tree's earlier version was built from the wiki and Quest Helper with every
scene "spec-pending" and played as chat: no copy, no camera, the tail in the
open world with an invented route and timings, guard varbits 0/1/2 (the cache
has 0 hidden / 1 unmarked / 2 marked / 3 noop), stage 14 written when the
fourth mark landed (Jagex writes it at the arrest), the wiki's text (31 pages
differed: "Kuaini"/"Kuani", "Western Ocean", missing commas, wrong speakers),
the scroll's reward line and model wrong, a locked bandit door, no Nevet. All
rebuilt from the recording.

## Action inventory (`rsprox_actions.py`)

| first | n | action | verdict |
|---|---|---|---|
| [2] | 1 | welcome_screen:play | login, generic |
| [112] | 4 | poh_tablet_varrockteleport op2 (Break) | generic: bound, `teleport_tablet.rs2` (TGV audit 2026-10-27); not re-diffed here |
| [147], [1107] | 2 | 2dose / 1dose stamina Drink | generic: bound, `inferno_potions.rs2`; not re-diffed |
| [149] | 1 | fai_varrock_bankbooth op2 (Bank) | generic: bound, `bank_booths.rs2` |
| [152]..[167] | 13 | bankside withdraw/deposit | the recorder's banking, generic |
| [178] | 6 | vmq1_alina Talk-to | **this content**: the start, the topics, the tail restarts |
| [209]..[902] | 13 | questlist / questjournal / overview | **this content**: the journal (below) |
| [386], [395], [461] | 3 | fai_varrock_door_open / fai_varrock_door (3233,3427/8) | generic doors (`doors.loc` category 167); used in the tail's copy, works there |
| [436], [657] | 2 | fai_varrock_ladder | generic (`ladders.loc`); [443] the climb out of the tail fails it (ours: off his level = too far) |
| [521], [614] | 2 | fai_varrock_poor_door(_flipped) | generic doors, in the tail's copy |
| [531] | 1 | rune_scimitar Wield | the recorder's kit |
| [602] | 1 | fai_varrock_range Cook | generic: **fixed**, "You haven't got anything to cook." in a box (ours said "You have nothing to cook." as a line) |
| [651], [679] | 2 | vmq1_bandit_door Open | **this content**: a walk-through door at stage 10 (was "The door is locked.") |
| [664] | 1 | fai_varrock_shelf_books Search | generic: **fixed**, had no binding; L+2 spam "You search the books..." + "None of them look very interesting." |
| [674] | 1 | fai_varrock_laddertop | generic |
| [712]..[1163] | 10 | vmq1_guard_sergeant / _roof Talk-to | **this content** |
| [719], [721] | 2 | vmq1_guard_8/10_noop op6 (Examine) | engine examine |
| [722] | 1 | events_shop (Nevet) Talk-to | generic: **fixed**, had no binding and no spawn; the first meeting and the shop interface |
| [766]..[886] | 14 | vmq1_guard_1..10 Mark / Unmark | **this content** |
| [992] | 1 | questscroll close | the scroll |
| [1048] | 1 | vmq2_quetzal_keeper_varrock Talk-to | **Travel to Varlamore**: the first flight |
| [1085] | 1 | vmq2_quetzal_keeper_fortis Talk-to | **Travel to Varlamore**: the flight home |
| [1134] | 5 | fai_varrock_castle_door | generic doors |
| [1141], [1146] | 2 | varrock_spiralstairs_taller / middle_taller | generic (`ladders.loc`) |
| [1175] | 1 | logout | generic |

## Teleports

| tick | cause | ours |
|---|---|---|
| [116], [174], [1014], [1104] | Varrock teleport tablet | generic tablet |
| [241] | the delegation scene: a copy (raw 13817,515) | `~cots_copy_enter`, 3225,3427 in the copy |
| [318], [347], [373], [409], [456] | a new copy per try at the tail, 3226,3428 | `~cots_tail_enter` |
| [336], [360], [400], [446] | a failed tail: back to 3225,3427 facing 3222,3427 | `[queue,cots_tail_failed]` |
| [443] | the ladder in the tail's copy: up a level | generic ladder; the guard fails it at once ([443] del, [444] the box) |
| [618] | the house scene: a new copy, 3254,3397 | `[queue,cots_house_scene]` |
| [646] | out of the house scene: 3259,3398 facing 3262,3398 | matched |
| [660], [677] | the empty house's ladder | generic |
| [924] | the escort to the palace roof: 3203,3474,2 facing 3203,3469 | matched (`^cots_roof_coord`) |
| [939], [967] | the cell scene: a copy of 50_54, 3204,3473,2; back to 3203,3474,2 | matched |
| [1072] | Primio to Fortis: 1700,3141 facing 1700,3142 | **fixed** (`^tp_fortis_arrive` was 1697,3140) |
| [1095] | Primio home: 3280,3412 | **fixed** (`^tp_regulus_varrock_coord` was 3282,3413) |
| [1144], [1146] | the castle's spiral stairs | generic |

## Stages and vars

`%varb9632_vmq1` on varp 4075 (`transmit=yes, perm`); our stage numbers are
Jagex's. Every write below is on the recorded click (ticks from 4408, ours
from the scriptrun tick log, all on the same click/page).

| value | 4408 | written with |
|---|---|---|
| 2 | [203] | the "Yes." to "Start the Children of the Sun quest?", with "You've started a new quest" |
| 4 | [239] | the click of "Wait! That's them! They're here!" (fade on that tick) |
| 6 | [315] | the tick after "Hmm... Now where might you be going with that...?" was clicked |
| 8 | [616] | the guard at his door +3 (ours: his timer, A+3) |
| 10 + all ten guards 3 | [644] | the click of "This doesn't sound good..." |
| 12 + all ten guards 1 | [756] | the click of "Return to me once you've found all four..." |
| guard N 1<->2 | [766]..[886] | the mark/unmark op, with "You mark the guard." / "You unmark the guard." |
| 14 + all 3, then 1-4 0 | [910] | arrest click +3, under the fade |
| 16 + guards 5-10 0 | [924] | the escort's teleport |
| 18 | [936] | the click of Tobyn's "Not at all, but..." |
| 20 | [965] | the click of "Well, I think we're done here. Come, <name>." |
| 22 | [981] | "Itzla departs." +3, under the fade |
| 24 + questcomplete_type 2 + vmq2_first_travel 1 | [989] | the click of Tobyn's last page; the scroll |
| `vmq1_met_alina` 1 | [197] | the click of Noah's "Adventure? Sounds dangerous..." |
| `vmq2_first_travel` 2 / 3 / 4 | [1060] / [1073] / [1080] | "So, ready for your first journey...", the landing, "Yes, the Tullus twins..." |
| `varlamore_visited` 1 | [1073] | the landing |
| `events_shop_owner_met` 1 | [727] | Nevet's box |

The guard varbits pick the multinpc rung (`tools/data/multinpc_shells.csv`):
0 hidden, 1 `_unmarked` (Mark), 2 `_marked` (Unmark), 3 `_noop`. Twilight's
Promise's own writes of `vmq2_first_travel` (2 at Renu's feed, 3 at its
completion) no longer lower it below the first flight's 4.

## The start and the end

- No combat warning (no recommended level). The menu, then the stage and the
  line with the player's page, as recorded.
- The scroll [989]: "Congratulations, you've completed a quest", disease
  -50/0, song 147 zeroed, jingle 152 (`quest_complete_1`, now in the
  recorded list), title, `cooked_chicken` zoom 400 at 78,128, rows "1 Quest
  Point", "Access to Varlamore". Ours: matched (was coins and a long line).

## Sections

### Alina and Noah [178]..[238], [343]..[456]

Every page verbatim with its mood and chathead (`vmq1_alina`/`vmq1_noah`,
the cutscene types inside the scene). The topic menu drops the topic just
asked and brings back the one before ([227], [232]). Stage 6: talking to
either plays Noah's "That was quite a large bag..." and on its click the
restart frame: C fade + blank box, C+1 minimap 2, C+2 `cam_forceangle(200,
1536)`, C+3 the new copy, C+5 the black lifts ([344]..[349]). Matched.
Unverified (no recording): "No." to the start, "Sorry, but I need to head
off.", talking before starting with `met_alina` already 1.

### The delegation [239]..[320]

Drafted with `rsprox_cutscene.py`; the frame as Misthalin Mystery's with
`fov_clamp` 3 and roofs hidden 1. Ours against 4408 from T (the stage-4 click):

| event | 4408 | ours |
|---|---|---|
| the copy, the cast of 16 (angles instant) | T+2 | T+2 |
| song 776 (`midi_hold` keeps the copy's region music off) | T+2 | T+2 |
| the procession's first step | T+3 | T+3 |
| camera 13,41 h460 / 25,35 h260 (rate 232/100) | T+3 | T+3 |
| the black lifts | T+4 | T+4 |
| player, Alina, Noah face the gate | T+13 | T+13 |
| knights 1/2 step aside, face out | T+15, T+16 | T+15, T+16 |
| knights 3/4 step, face out | T+17, T+18 | T+17, **T+17** (the page blocks; one tick early) |
| the bag guard + Tobyn (cutscene types), camera 16,42 / 11,46 | "Huh..." click | same |
| their faces | +6 | +6 |
| the second bag guard | Tobyn's click +3 | +3 |
| the stage, the fade | "Hmm..." click +1 | +1 |
| the unlock, `cam_forceangle`, the panels | +3 | +3 |
| the tail's copy | +4 | +4 |

Camera lines in the run: 7 `cam_moveto_v3` / 7 `cam_lookat_v3`, 3
`cam_unlock`, 15 `cam_shake`, 3 scene `cam_reset` (4408 the same, the other 5
resets being tablets and login).

### The tail [318]..[616]

The guard (`vmq1_bag_guard`) in a private copy with Noah, Alina, Tobyn and the
four bandits in their house. His walk, corner by corner, and the five stops,
from the successful try [456]..[613]; ours from the scriptrun tick log (spawn
113):

| | 4408 (spawn 456) | ours (spawn 106) |
|---|---|---|
| spawn: facing coord 3238,3430 | A | A |
| first step, face reset | A+1 | A+1 |
| stop 1 3239,3429: arrive / look west (512) / walk on | 467 / 474 / 482 | 117 / 124 / 132 |
| stop 2 3243,3412: arrive / look north (1024) / on | 500 / 507 / 515 | 150 / 157 / 165 |
| stop 3 3239,3401: arrive / look east (1536) / on | 527 / 534 / 542 | 177 / 184 / 192 |
| stop 4 3243,3389: arrive / look west / on | 562 / 569 / 577 | 212 / 219 / 227 |
| stop 5 3253,3397: arrive / look west / on | 593 / 600 / 608 | 243 / 250 / 258 |
| his door 3259,3400: arrive / face 3269,3400 / the house | 613 / 614 / 616 | 263 / 264 / 266 |

Every stop is arrive, +7 look back the way he came, +15 walk on (5/5 here,
5/5 in index 1621, whose arrival gaps 33/27/35/31 are 4408's). Both lose one
step on 3250,3397 ([590]; 1621 [7940]): ours pauses there too
(`^cots_tail_pause_step`).

**Spotted** (3/3 recorded): on his tile or beside him at any time ([333]:
the recorder ran onto him; [357]: beside him), or during a look within 5+
tiles straight in front ([397]: 5 west, on the street). S: he stops and faces
the player's tile; S+1 "Hey! What are you doing?" overhead, the box with no
continue "You failed to stay hidden from the guard.", the fade; S+2 minimap
2; S+3 out to 3225,3427; S+5 the black lifts and the box asks for its click.
Ours: matched on the first try (S 94, minimap 96, teleport 97, box 99). The look rule
is fitted: within 6, in front, a clear line, and not on that stop's hiding
place. The five places the recorder stood through every look that got
through are the five Quest Helper marks, and our map has a clear line to
3240,3417 from stop 2 (five tiles off his front), so the line alone cannot be
Jagex's rule. Index 1621's recorder stood on the same five tiles through the
same five looks. **Too far**: [443] a climb (off his level): he is deleted that
tick, the box comes the next, "You failed to stay close enough to the guard."
Ours: off his level, or past 16 tiles (unverified: 14 is the furthest the
recording went without a fail). Unverified: leaving the copy by teleport (ours:
he goes quietly).

The copy is one square's worth of zones from 3200,3384 (the tail reaches z
3389, the palace road 3442). A copy of the two whole squares 50_52 + 50_53
(`~map_instance_from_square_block(.., 1, 2)`) put the scriptrun client's
tiles out by two squares east and three north and nothing could walk in it:
an engine/client fault in tall block copies, left open below. The copy's
noop locs (6 bank booths, the tomato crate, the poll booth, the junk source,
the deep ladder) are added as recorded.

### The bandits' house [616]..[650]

| event | 4408 | ours |
|---|---|---|
| stage 8, fade + blank box, Noah/Alina gone | A+3 | A+3 |
| the lock, a new copy, the five actors (angles instant), the noop door/ladder/tree | +2 | +2 |
| camera 52,4 h490 / 60,8 h290; the player walks five tiles, a tile a tick | +3..+7 | +3..+7 |
| the black lifts | +4 | +4 |
| the player's page | +8 | +8 |
| camera 63,3 h340 / 60,11 h300, roofs 2 | the page's click | same |
| song 147 envelope 0,20,0,0 | "Good luck" click +1 | same |
| camera back, roofs 1 | the bandit's last click | same |
| stage 10, guards 3, fade | the player's click | same |
| out to 3259,3398, unlock | +2 | +2 |
| black lifts / overlay closed | +4 / +6 | +4 / +6 |

Speakers by chathead: bandit 1 "You're late." and "Mostly?", bandit 2
"Enough!", bandit 4 "We're just going to have to make do..." and "You lot
panic too much...", bandit 3 "Indeed..." (the wiki had them shuffled).

### Tobyn and the marks [712]..[905]

All pages verbatim. The mark is an ap op: answered from 3 tiles (14/14
recorded at 1..3, mostly 3), with the line and the rung change only. With
exactly four marked the player says "Alright, I've pointed out all the
bandits." and Tobyn either arrests or says "Are you sure? I definitely
recognise..." ([825]); fewer than four: his reminder ([771], [776], [799]).
Unverified: a fifth mark (ours refuses with a line).

### The arrest and the escort [906]..[928]

| event | 4408 | ours |
|---|---|---|
| "Good work..." clicked: box (no continue) + fade | C | C |
| minimap 2 | C+1 | C+1 |
| stage 14, guards 3 then 1-4 0 | C+3 | C+3 |
| black lifts, the box with its continue | C+4 | C+4 |
| "Come with me..." clicked: box + fade | C | C |
| stage 16, guards 5-10 0, onto the roof | C+3 | C+3 |
| black lifts, the box | C+4 | C+4 |

### The roof and the cell [928]..[989]

The roof conversation runs straight on from the escort (`@cots_roof_intro`),
every page with the roof types' heads (`vmq1_itzla`,
`vmq1_guard_sergeant_roof`). The cell scene [937]..[969]: a copy of 50_54,
the three actors, the prison door noop and the castle wall, song 775, camera
7,15 h311 / -3,13 h300 at T+3, black lifts T+4, Itzla opens the door (iron
door synths) and the player follows him in a tile a tick, the door shut, 28
pages with the cutscene heads, stage 20 and the fade on the last click, back
on the roof at +2. Itzla departs [978]..[982] like the arrest. Stage 24 and
the scroll on the click of Tobyn's last page. Matched.

### Tobyn afterwards [998]..[1003], [1163]..[1167]

Before the first flight the long form ("Did you end up travelling to
Varlamore?" / "Not yet."); after it, "Glad to hear it." and done. Matched
(keyed on `varlamore_visited`).

### Travel to Varlamore [1048]..[1098]

Regulus Cento's first-flight talk (13 pages, `vmq2_quetzal_keeper_varrock`
head), then the flight: C fade + chat closed, C+1 minimap 2, C+2 wing flaps
(8501, 3 loops, delay 25), C+6 the landing facing Regulus, C+7 the black
lifts, and five pages in Fortis (`vmq2_quetzal_keeper_fortis`). Home: "Do you
wish to return to Varrock?" / "Yes please." / "Then we will depart at once.",
on its click the wing flaps (delay 55) and the fade, C+4 the landing at
3280,3412, C+5 the black lifts. Both matched (were a bare telejump with an
invented "You fly to..." line). After the first flight the Varrock keeper is
his 2op form; his Travel op now needs the first flight, not Twilight's
Promise. Unverified: later talks, "There's something I need to do first.".

## The journal

Opened at stages 2 [209], 6 [~370] and 12 [824], [889], [899]; a scratch
scriptrun read of ours (`ui.journal_read` after the journal proc) matches all
three row for row (3, 8 and 14 rows, the same wraps). Stages 8, 10, 14..22
and the completed journal keep the recorded layout with unverified wording.
The overview's start text [901] ("talking to Alina and Noah in Varrock
Square.") is in `quest_overview_start.rs2`. `test/quests/_journals.lua` was
being edited by another session and has no Children of the Sun rows yet.

## Every page

`pages.rs2` (all 178 recorded pages, heads and moods, from `rsprox_pages.py`)
checked mechanically against our scripts: 0 missing, 0 with a different mood.

## Open

- **Tall block copies.** `~map_instance_from_square_block(square, 1, 2)` gives
  the scriptrun client tiles two squares east and three north of the real ones
  and the player cannot walk in it (Ides of Milk and ToB use 2x2/2x1 and
  never walk by hand). Worked round with a one-square zone copy; the engine or
  client fault is not fixed.
- **The copies carry no ambient npcs.** Jagex's copies have the square's own
  people (the shopkeepers, the bankers, Romeo, the wandering men); ours only
  the cast. Not built: our copy has no npc copying.
- **Music.** `midi_song_withsecondary` 774/810 in the tail has no command
  (774 alone plays); the "You have unlocked a new music track" lines for
  Children of the Sun / Plots and Plans / The City of Sun / Prospering Fortune
  are not sent. Back in Varrock square our region music is `garden` (125)
  where 4408 plays `medieval` (157): the sub-square music areas issue, not
  this quest's.
- **Nevet wanders** (the generated block's `defaultmode=wander`, range 5);
  4408 has her on 3209,3435 at every add. The override belongs in the npc
  configs, which another session has open.
- **The guard's look rule is fitted** (above): two recordings, both hiding
  on the five marked tiles; neither tried another hiding place.
- **Not wire-diffed here:** the tablets, the stamina drink, the bank, the
  generic doors and ladders and the castle stairs: each is bound in our tree.
- **The invisible roof-hiding walls** the house and cell scenes add are not
  copied (they draw nothing).
- **Live-client lane not run:** the cutscene rows (`t.cutscene.await`, camera
  heights) only run there; on scriptrun they are skipped.
