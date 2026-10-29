# Tale of the Righteous, the Quiz Master and the Mountain Guide vs RSProx index 4425

Recording: index 4425 (rev 241.1, world 369, 2026-10-29, "Tale of the Rightous -
Quiz master minigame - Mountain Guide (Quidamortem) unlock"). It has the whole
quest, stages 0 to 17 [217..2136], the Quiz Master random event [1500..1582] and
the Mountain Guide's unlock and first trip [1601..1621]. It is one playthrough,
so every roll in it is a single sample.

**Tests** (scriptrun, private packs):

- `test/quests/taleoftherighteous.lua`: 149 of 150 rows pass. The one failure is
  `quest.journal`, which no scriptrun can pass.
- `test/quests/tor_boss.lua`: the Corrupt Lizardman fight, 14 of 14 rows.
- `test/randoms/quizmaster.lua`: the Quiz Master event end to end, 19 of 19 rows.
- `test/quests/_journals.lua`: stages 5, 6, 14 and 16 row for row. These were
  also checked headless through `::torjournal`, and all four match.
- `test/raids/cox_surface.lua`: the Mountain Guide rows pass.

**Content:**

- `quests/quest_taleoftherighteous/`:
  - `taleoftherighteous.rs2`: Phileas, Lord Shiro, Duffy at the temple, the
    crevice, the house, the journal and the reward.
  - `tor_archive.rs2`: Archeio, Istoria, Pagida and the prison.
  - `tor_cave.rs2`: the cave and the boss's entrance.
  - `tor_boss.rs2`: the fight.
  - `tor_bmp.rs2`: `::torjournal`.
- `macro_quiz_master/` (new): the event, the Mystery box, `::quizmaster`,
  `::quizodd` and `::quizanswer`.
- `areas/area_quidamortem/`: the guides, and Gnosi's and the historian's talks.
- `areas/area_shayzien/scripts/shayzien_bosstent.rs2` (new): General Salara.
- `skill_fishing/.../memberfish.rs2`: the casket's sound.

Before this port the quest was built from the wiki and Quest Helper:

- Every page's text matched, but every head was neutral.
- The prison puzzle was the wiki's reading. It had the wrong crystals in the
  wrong corners, the wrong device tile, and no projectile.
- The journal was this tree's own wording.
- Every stage was written a page or more late, and stage 3 was never written.
- The boss swung crush only.

## Action inventory (`rsprox_actions.py`)

| first | n | action | verdict |
|---|---|---|---|
| 2 | 1 | welcome_screen:play | engine |
| 13, 18 | 6 | questlist op2 / questjournal_overview close | the Ascent of Arceuus overview [14] ("talking to Mori in Arceuus"). That is another quest's row in `quest_overview_start.rs2`, which another session is editing, so it was left alone (Open) |
| 119, 124 | 18 | world map orb / close | engine UI |
| 126, 214, 533, 539 | 7 | Lovakengj, Shayzien and Arceuus doors | generic doors (`doors.loc`, `doubledoors.loc`), bound |
| 148, 781, 1087 | 6 | Lovakengj minecarts | generic (`transport_lovakengj_minecart`), bound. The teleports land where the route says |
| 204, 473, 808, 1455 | 6 | stamina potion Drink | generic |
| 217..912 | 3 | opnpc1 phileas_rimor | this content: stages 1 and 6 moved onto their recorded pages, moods recorded |
| 244 | 1 | opnpc2 kourend_guard_f3 | superseded by a walk |
| 271..499 | ~25 | bank booths and bank interface | generic (`bank_booths.rs2` binds `archeeus_bank_booth_*` by category) |
| 415, 745, 1831 | 3 | Kourend teleport tablet Break | generic (`teleport_tablet.rs2`) |
| 540 | 1 | opnpc1 raidquest_library_archive_guardian | this content: the menu order and the teleport are now the recording's |
| 556, 583 | 2 | opnpc1 raidquest_library_traphunter | this content: Pagida's recorded pages, and stage 3 written with "Well the prison is abandoned" [597] |
| 560 | 1 | opnpc1 raidquest_library_historian | this content: the greeting and the menus, rebuilt |
| 610..716 | 27 | shayzienquest_puzzle_piece Push / Attack / Fire Strike on it | this content, rebuilt (below) |
| 662..715 | 6 | bow, arrows, scimitar Wield | the recorder's kit |
| 724 | 2 | shayzienquest_prison_skeleton_main | this content: stage 5 with the box, Jagex's row break kept |
| 863, 1863 | 2 | tree Chop | generic woodcutting |
| 884..2123 | 7 | shayzien_ladder / laddertop | generic (`ladders.loc`; same tile, plane ±1, as `maplink.dbrow` gives) |
| 895..2139 | 5 | questjournal close | the journal (below) |
| 977..2126 | 4 | opnpc1 shiro_shayzien | this content: stages 7 and 15 moved, the post-scroll page and menu added |
| 1065, 2158 | 2 | kitten feed / Interact | the recorder's pet, generic |
| 1115, 1133 | 3 | Gnosi and Duffy at the temple | this content (`quidamortem.rs2`, `taleoftherighteous.rs2`): moods, and Gnosi's menus now drop the question just asked |
| 1185..1695 | 10 | oploc1 shayzienquest_cave | this content: Inspect with a rope ties it on (rope used up, objbox); Enter is the prompt; anim on the click, in a tick later |
| 1201, 1462 | 2 | shayzienquest_lab_exit | this content: face, then the reach a tick later, out a tick after that, on 1214,3557 |
| 1212 | 2 | shayzienquest_blockage | this content: 4 fixed ticks, the recorded lines and mine_ore (one sample) |
| 1234 | 1 | shayzienquest_boulder | this content: the npc glides a tile a tick for 6 ticks with rumbling (range 6), then the no-op boulder on 1200,9953 |
| 1272..1377 | ~40 | prayers, lobster, antipoison, attacks on shayzienquest_lizardman_boss | the fight (below); the prayers and food are the recorder's |
| 1381 | 3 | xeric_fabric take | generic take; three separate drops |
| 1388..1732 | 5 | shayzienquest_cave_door | this content: three one-tile steps through the gate, busy |
| 1401 | 1 | shayzienquest_lab_altar | this content: stage 11 with the last page; the voice is `shayzienquest_ghost`'s head, chatshock |
| 1503..1582 | 10 | the Quiz Master, its panel, the Mystery box, the casket | the Quiz Master (below). The casket is generic: the sounds are fixed (chest_open only) |
| 1577 | 2 | xeric_fabric Drop | generic drop |
| 1601, 1611 | 2 | raids_mountainguide_temple_multi Talk / Travel | the Mountain Guide (below) |
| 1743..1809 | 6 | shayzienquest_historian / gnosi / duffy | this content (the cave): stages 13 and 14 moved, Gnosi's stage-12 talk added, the historian's five lines |
| 1946 | 1 | shayzien_bosstent_strategist | **was unbound**: General Salara, recorded pages plus the wiki's second branch |
| 2145, 2149, 2155 | 3 | Kharedst's memoirs Read / page on memoirs / op3 | another session's port (`kharedst_memoirs.rs2`, uncommitted, with `test/magic/kharedst_memoirs.lua`): not touched |
| 2170 | 2 | logout | engine |

## Teleports

| tick | from -> to | cause | verdict |
|---|---|---|---|
| 160, 794, 1103, 1669, 1880 | minecart routes | Lovakengj minecarts | generic |
| 419, 750, 1835 | -> 1642,3672 | Kourend tablet | generic |
| 554 | 1625,3808 -> 1550,10221 | Archeio | fixed: it landed on 1551,10211. Cast on the click, landing 3 ticks later |
| 605 | 1553,10221 -> copy 1588,10199 | Pagida | same cast; the copy and its contents as recorded |
| 974, 1020, 1956, 2007, 2123 | Shiro's ladder | ladder | generic |
| 1198, 1208, 1699 | 1215,3557 -> copy 1169,9973 | the crevice | fixed: it landed on 1170,9972 |
| 1204, 1469 | copy -> 1214,3557 | the rope | fixed: it landed on 1213,3559 and printed a line that isn't recorded |
| 1257 | 1175,9951 -> 1172,9953 | the boss's entrance | built (below) |
| 1508, 1569 | 1265,3547 <-> 1952,4764,1 | the Quiz Master | built |
| 1618 | 1276,3560 -> 1401,3536 | the Mountain Guide | fixed: the trip's shape and an exact tile |

## Stages and vars

| var | value | tick | with |
|---|---|---|---|
| shayzienquest (6358) | 1 | [240] | the page after "What do you need?", with "You've started a new quest" (moved from the end of the talk) |
| | 3 | [597] | Pagida's "Well the prison is abandoned..." (never written before; 2 is never written) |
| | 4 | [718] | the last crystal, "The cell gates spring open.", the gates, iron_door_open |
| | 5 | [729] | the skeleton's box |
| | 6 | [911] | "First things first..." (moved) |
| | 7 | [991] | "Excellent. Consider your research approved..." (moved) |
| | 8 | [1147] | Duffy's "We recently found a small crevice..." (moved) |
| | 9 | [1192] | the rope's objbox |
| | 10 | [1377] | the boss's despawn |
| | 11 | [1412] | "Oh well, I may as well go..." (moved) |
| | 12 | [1595] | two ticks into the fade after Duffy's last page (was the click) |
| | 13 | [1787] | Duffy's "Anyway, I'm going to carry on..." (moved) |
| | 14 | [1808] | Gnosi's "Hmm, I can see why..." (moved) |
| | 15 | [1997] | Shiro's "Well you need to find out..." (moved) |
| | 16 | [2064] | the house: the tick after the step onto 1541,3570 (the doorway, 1540, fires nothing) |
| | 17 | [2136] | the click of Shiro's last page, with shayzienquest_reward (6359) = 1 |
| raids_guide_travel_unlock (5421) | 1 | [1606] | "I don't see why not..." (moved from after "Thanks!") |
| macro_last (10056) | 3 | [1569] | the Quiz Master's prize |
| chatmodal_unclamp (10670) | 0 | [1524] | the quiz panel (set in content; the engine's table doesn't know interface 191) |

## Pages

All 292 recorded pages were checked mechanically (text, `<br>` read as a space,
and mood). Of the quest's pages, 250 had the right text but a neutral head;
their moods are now the recording's.

These pages were missing, and are now written as recorded:

- Istoria's greeting.
- Six of Pagida's prison pages.
- Gnosi's stage-12 greeting in the cave, and her two cave lines about the altar.
- Two of the Mountain Guide's moods.
- General Salara.
- Every Quiz Master page.

Menus are now in the recorded order and with the recorded titles:

- Archeio.
- Istoria's subjects, with the King's question first.
- "Do you want to enter the crevice?"
- "Where would you like to travel to?"
- "Select Your Prize".
- Istoria's and Gnosi's menus leave out the question just asked, as recorded.

Branches the recording never takes are the wiki's, and their moods are
unverified (marked in the scripts):

- Shiro's post-quest menu and his line before the quest.
- Archeio's "Who are you?".
- Istoria's other subjects.
- Salara's second option.
- The Quiz Master's other verdict lines.

## The prison puzzle (index 4425 [605..718])

| | R | ours |
|---|---|---|
| copy | 1588,10199 in a fresh copy; crystals magenta NE (1581,10203) rot 3, white SE (1581,10196), cyan NW (1574,10203) rot 1, yellow SW (1574,10196) rot 2; gates r/l on 1577/1578,10209 rot 3; device 2x2 on 1577,10199 facing south; invisible_type8_blocking under it | same (was cyan SW, magenta NW, yellow NE, device on 1578,10200) |
| push | from the east or west side only (from north/south: "The device cannot be pushed there." [630], [689]); O: face its SW tile, garden_statue_push, pyramid_scrape delay 20; O+1 one step; O+2 the blocking moved; x 1574..1580 | same |
| strike | the device fires away from the player: E/W three tiles from its SW tile along its row; N/S up the player's column to z 10203 / 10196. Melee fire, magic water, ranged earth; 20/60 cycles, angle 50, height 100 -> 20 on a crystal, 0 on the floor | same |
| hit | the right element on a crystal: its strike impact and hit sound, the crystal white 2 ticks later (rotation kept). Anything else: failedspell_impact + spellfail | same (8 of 8 recorded strikes reproduce) |
| last crystal | stage 4, "The cell gates spring open.", closed gates deleted, open_r 1577,10208 rot 0, open_l 1578,10208 rot 2, iron_door_open | same |

## The cave

| | R | ours |
|---|---|---|
| boss entrance | an area trigger: (1177,9953) nothing, (1175,9951) fires a tick later [1255]. T fade 0,255,0,0,15. T+2 player on 1172,9953 facing 1172,9952. T+3 boss on 1172,9948 facing north, cam_moveto 1172,9956 h300 232/232, cam_lookat 1172,9947 h50. T+4 shayzien_lizard_boss_land, moon_fall_land range 8, fade in. T+7 emote_panic, cam_lookat h150 rate 1/15. T+10 cam_reset, overlay closed. T+11 the boss turns on the player | same (`[timer,tor_boss_watch]` fitted to x 1166..1176, z 9948..9952; `[queue,tor_boss_scene]`) |
| gate | three one-tile steps through it, busy, both ways | same (was a telejump with a line) |
| team (stage 12+) | Duffy 1174,9938 east, Gnosi 1167,9937 west, historian 1174,9936 east; all wander | same |

## The Corrupt Lizardman (`tor_boss.rs2`; audited by one Opus agent)

| attack | recorded n / zero % / max | ours (3 runs) |
|---|---|---|
| spit, unprayed | 2 / 0% / 5 | 20 / 15% / 6 |
| spit, under Protect from Missiles | 21 / 100% / 0 | 34 / 100% / 0 |
| melee | 3 / 0% / 6 | 26 / 19% / 6 |

These match the recording:

- **Spit:** seq 7193, creeper_spit range 10, `lizardman_spit` with start 61,
  end 66 + 5 per tile, angle 5 and progress 20. It lands at end / 30 ticks.
- **Melee:** seq 7192 and stabsword_slash. The hit lands at the swing + 2, and
  the player's block has delay 60.
- **Poison:** severity 15, with the message on the throw.
- **Defend:** seq 7194 on the player's swing, absent when the boss swings on the
  same tick. crocodile_hit plays on every landing.
- **Death:** seq 7196 and crocodile_death. The despawn comes 3 ticks after the
  death seq, with bones and three separate xeric_fabric.

The style rule comes from 211 attacks: 4425 plus the Lizardman Battlefront
sessions 724/725, which share AI category 929.

- **Not adjacent:** always a spit.
- **Adjacent, coming from ranged:** 4/6 spit, 1/6 melee, 1/6 a spit one tick
  late.
- **Coming from melee:** a 1/2 chance to switch back, at a one-tick cost.

The adjacent melee chance (1/3 or 1/2) is not settled and is marked unverified.

Changes to the boss's npc block:

- `huntmode=aggressive` was removed, so the entrance can hold the boss.
- `crushattack` 22 -> 0: the cache has no melee bonus.
- `rangebonus` 20 -> 0: it doubled the ammo bonus, making the max 8.

## The Quiz Master (index 4425 [1500..1582])

| tick | R | ours |
|---|---|---|
| S [1500] | macro_magneson_invitation added 4 tiles away (-4,-3), facing south | `map_findsquare(coord, 1, 4)`; follows the player |
| S+1 | "Hey <name>! It's your lucky day!", smokepuff (h124) and smokepuff sound (range 7) on the add tile | same |
| talk | page chathap; menu "What would you like to say?"; the answer said | same |
| A (the click) | player to 1952,4764,1, the invitation teleported with them (gone at A+2), smokepuff synth, cam_reset | same |
| A+1 | panels 0..6, 11, 12 closed, friends tab, quiz_sit facing north | same; quiz_sit again every 10 ticks |
| A+2 | "Answer four questions correctly in a row to be teleported back where you came from." | same |
| A+5, +8, +11 | the host's three pages, no continue row (chathap2/3/4) | same |
| A+16 | unclamp 0, `macro_quizshow` (191) in the chatmodal, quiz_init(buttons, model, zoom ×3), events 0..2 OP1 | same: two models of one category plus one of another (the cache names the ten models weapon/food/jewel/armour/tool; all six recorded questions decode) |
| answer C | the verdict page (no continue) + quiz_correct / pillory_wrong; C+3 the next question | same (a soft timer: a queue waits for the page to close) |
| 4th right | CONGRATULATIONS page, song 147 envelope 0, jingle victory_burthorpe_games_room; C+3 "Select Your Prize" (1,000 Coins / Mystery Box) | same |
| prize P | macro_last 3, home, panels back; P+1 "Your reward is: ... Mystery box." and the box | same |
| box | "Inside the box you find a casket! Excellent!", the reward line, the prize in the box's slot | the wiki's members table (Mystery_box); the line's article from the name |

The scriptrun test answers through `::quizanswer`. The panel's models are
clientscript children, which a headless run never builds.

## The Mountain Guide (index 4425 [1601..1621])

| | R | ours |
|---|---|---|
| unlock | the varbit with "I don't see why not..." [1606]; chathap on two of the guide's pages | same (it was set after "Thanks!", and the heads were neutral) |
| Travel | menu "Where would you like to travel to?"; the answer not said. Click C: fade out, chat closed. C+1 minimap off. C+2 "You carefully follow the mountain guide to the Shayzien Outpost." (spam) and the player on 1401,3536. C+3 minimap on, fade in. C+5 overlay closed | same. The other two destinations use the wiki's pins and lines by analogy (unverified) |
| summit guide | wanders inside 1275..1277 x 3558..3560 | `wanderrange=1` |

## Journal

Four recorded views: stage 5 [894], 6 [914], 14 [1975] and 16 [2074]. They are
in Jagex's current format: struck finished steps, the current step in blue with
names in red, and no blank rows between them. They give the struck form of the
first five steps. All four views match row for row
(`test/quests/_journals.lua`, and `::torjournal` headless).

Stages 1-4, 7-13, 15 and 17 are this tree's wording, marked unverified.

## Open

- **The Ascent of Arceuus overview row** [14] is another quest's, in a file
  another session is editing.
- **Kharedst's memoirs** [2145..2155] are another session's work.
- **The fight's generic gaps** (poison and the engine; named in the fight
  section):
  - `enabled_ops 0` goes out a tick early.
  - The loot-tracker clientscript is one tick early and has the wrong id.
  - `poison_type` 6251 is never sent.
  - The poison varp is re-sent on every landed spit.
  - The first poison splat follows the login clock.
  - The boss doesn't re-face when the player steps out and back [1296].
- **Not a bug: the VM's random draws.** The fight agent saw two draws in one
  trigger run always agree (7 of 7) and suspected a VM correlation. A probe
  (2026-10-29) found none. It drew random(3) then random(2) 3000 times in one
  player script run, 3000 times in one npc `ai_queue` run, and once in each of
  600 npc runs. The six (a,b) cells gave chi-square 3.7, 8.4 and 7.6 on 5 df,
  all under the 11.07 cutoff, and consecutive runs repeated their first draw
  180 times in 599 (about 1/3 expected). Every run loads and saves its subject's
  Java-Random state (`script_execute`), and the streams are seeded through
  splitmix. The 7 of 7 was a 1-in-128 chance in a small sample.
- **Quiz Master branches no recording reaches:**
  - Saying no, ignoring him (the wiki's lines on the Evil Twin's timing), the
    coins, another player's invitation.
  - Logging out in the room: the varps are perm, but the event doesn't resume.
- **Device splash:** a magic strike can splash (the wiki). The recorded cast
  landed.
- **Unrecorded talks:** Phileas at stages 1-4 and 6+, Duffy at the temple at
  stages 9-10 and 12+, the gate before the boss is dead, and the prison portal
  out (the recorder left by tablet).
