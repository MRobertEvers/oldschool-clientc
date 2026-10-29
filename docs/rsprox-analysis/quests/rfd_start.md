# Recipe for Disaster: the start, the banquet, Aris, the journal

Recording: RSProx index 4400 "RFD Start" (2026-10-28, rev 241.1, w495), the
whole start from the Cook's first talk to the portal door out of the frozen
room. Index 4402 ("Scrubfoot's Cave and RFD 1 - Wartface and Bentnoze") is the
next session: it supplies the frozen room's two entries and the second portal
door, and every stage of the goblins' journal. Ticks below are 4400's unless
marked 4402.

Ours before this port: QuestHelper-shaped (`steps.put` numbering), a Cooking-10
start gate, invented pages, the banquet "condensed into one narrated ~mesbox
sequence", no frozen-room entry or exit at all (every subquest's frozen guest
was unreachable), the stage in a private varp (6942) the client never read, and
a main journal that was invented prose with no way to reach the subquests.

Generic content the session also exercises (gnome cocktails, the tomato crate,
tablets, the GE, the forestry fire, banks, ...) is in
[rfd_start_generic.md](rfd_start_generic.md).

## Stages and vars

| var | recorded | ours now |
|---|---|---|
| `hundred_main_quest_var` (varbit 1850, varp 678 bits 0-6) | 0->1 [370] with the coins page; 1->2 [1614] on the hand-in click; 2->3 [1660] at the lock | the same varbit, same values (`^rfd_intro_started/_ingredients_given/_complete` were already 1/2/3). Was `%varp6942_recipefordisaster`, never sent: the quest list showed RFD not started |
| varp 678 `100intro`, varp 679 `100council` | sent at every change, kept across logout | stated `perm, transmit=yes` in `recipefordisaster.varp`; the generated block's `scope=temp, transmit=no` lines deleted |
| `100{dwarf,goblins,pirate,guide,dave,ogre,amik,monkey,culinaromancer}visible` (1858-1866) | 0->1 at [1660], the same tick as the stage | written there |
| `qp` (101) | 62->63 [1614] | `subquest_rfd_intro` row's questpoints |
| `subquests_completed_count` (13987) | 0->1 [1614]; `quests_completed_count` untouched | `~rfd_subquest_complete` bumps 13987, not 6347 |
| `disease` (456) | -50 and 0 [1614] | the same |

Old saves: `~rfd_login_migrate` (from `[login,_]`) copies a non-zero varp 6942
into varbit 1850 once, sets the visible varbits at stage 3, and zeroes 6942.

Every reference to varp 6942 in the tree (22 files, among them quest_cook,
quest_cheat, poh_quest_status_generated, the eight subquests, Murphy, Below Ice
Mountain, the black dragon drop, Making Friends with My Arm, One Small Favour,
Monkey Madness) now reads varbit 1850.

## The start (the Cook)

| step | recorded | ours |
|---|---|---|
| gate | Cook's Assistant done; no level checked (Cooking 4 here) | gate on Cook's Assistant only (the `stat_base(cooking) >= 10` gate deleted) |
| greeting menu [167] | "Do you have any other quests for me?" / strong and mighty / dying / range: the quest replaces "Nice hat!" | the same, `~rfd_cook_offer` |
| "How does that make you feel?" [198] | Angry chosen | recorded; "I don't really care" branch from the wiki, UNVERIFIED |
| 3-option menu [221] | "How do you still have a job?" chosen | recorded; the other two from the wiki, UNVERIFIED |
| story [243]..[327] | 27 pages | every page, text and mood, from the transcript |
| warning [331] | mesbox, Cooking below the row's 10 | `stat(cooking) < 10` (the row is boostable) |
| confirm [340] | `Start the Recipe for Disaster quest?` Yes./No. | the same; "No." pages from the wiki, UNVERIFIED |
| start [370] | stage 1, 100 coins, "You've started a new quest" with the coins page | the same, before that page |

Pages: generated from the transcript (`rsprox_pages.py`), every recorded `<br>`
kept as a hard break; a 4-row page that Jagex continued onto a second page is
one string (our pager splits it at the same row).

## The ingredients and the hand-in

| step | recorded | ours |
|---|---|---|
| ashes on fruit blast [1580] | "That looks disgusting, but it's what the cook asked for..." | the same, `chatcon1` |
| stage-1 talk [1609]..[1612] | all four held: the hand-in | the same; partial/none answers and the "where do I get" menu from the wiki, UNVERIFIED |
| hand-in [1614] | the four items gone, the vars above, the scroll titled "You have assisted the Lumbridge Cook... again!", `chefs_hat` at 78,128, "1 Quest Point / An invitation to the most / amazing feast you will ever / see!", song 147 then jingle 152 (`quest_complete_1`); no xp, no "Congratulations" line | the same; the scroll mounts a tick later (the engine's rule, questscroll.rs2) |
| scroll closed [1621] | the line "Honestly, you must go and look at the amazing feast that's been prepared!", then the Cook's page | `[queue,rfd_intro_scroll]` parks on the close button and answers it the same way; the wiki's second page ("I simply won't take no for an answer!") UNVERIFIED (4400 opened the journal over it) |
| stage-2 talk [1631] | five pages, the same each time ([1641], [1647]) | the same |

## The banquet [1656]..[2003]

Drafted with `tools/rsprox/rsprox_cutscene.py 4400 1654 2010 --origin 1856,5312`
and rebuilt by a generator (scratch `gen/scene.py`) that puts every statement on
its recorded tick and adds the pages the draft did not list.

- Frame: the black on the op tick [1657] (O = arrival + 1), the lock and the
  copy at O+3 [1660] (the same lock and HUD as Misthalin Mystery, packet for
  packet: `~mist_cutscene_lock` / `_hud_lock`), the cast [1661], the camera and
  the black lifted [1662], song 147 + jingle `the_banquet_recipe_for_disaster`.
- The player plays Awowogei: appearance `npc=mm_awowogei_cutscene` with monkey
  stances from [1661] to [1828] (`p_transmogrify`), kicked off his chair by
  the Duke at [1819] (`m_monkey_block_scimitar` + exactmove).
- Pages: none waits for a click. The Duke's nine roll-call pages carry the chat
  echo and Jagex's two-row breaks (line height 28); every other line of the
  scene is a page with no echo and no continue, beside the speaker's overhead
  chunks (`~rfd_scene_npc_page`, `~rfd_scene_player_page`).
- Npc walks are issued a tick before the recorded first step (a script resumed
  by p_delay runs after the npc phase).
- Projectiles: `windwave_travel` [1934], [1946], `map_projanim_v2` heights 114
  -> 28 (the old packet's byte, x4 on the client).
- [1999] the player into the frozen part of the same copy, the cast gone, Aris
  (`hundred_aris`) and `culinaromancer_froze` added; [2003] the unlock, the
  last page and the fade closed, song 163. Ours keeps the one copy where Jagex's
  raw coordinates suggest a second one; both rooms are in the one square.

## The frozen room

| | recorded | ours |
|---|---|---|
| double doors, stage >= 3 (4402 [112]..[117]) | arrival + 1: into a copy at (13,11), Aris and the frozen Culinaromancer added; next tick the door's slot is an inviswall + `inaccastledoubledoorl` for 2 ticks, `door_open`, a step in | the same (`~rfd_doubledoor`); stage < 3 declines to the ordinary door |
| the Cook's door, stage >= 3 (4402 [1701]) | on the op tick into a copy at (5,3) | the same; its twin inside the copy does nothing (4402 [1787]..[1790]) |
| `hundred_portal_door2` ([2209]..[2213]) | arrival + 1 face, a step east, out to 3213,3222 a tick later | the same |
| `hundred_portal_door1` (4402 [1793]) | already there: a 2-tile step on the op tick, out to 3207,3217 next tick | the same |

## Aris [2008]..[2119]

First menu: the five topics. After each, the four not just asked and "Nothing
thanks." ([2021], [2062], [2087], [2106], [2119]). All five topics recorded,
page for page. "Nothing thanks." is the wiki's two pages, UNVERIFIED (4400 left
the menu open and opened the journal).

## The journal

| state | recorded | ours |
|---|---|---|
| not started | the overview (782), as every quest (4332) | `~quest_journal_overview_open` |
| stage 1 | 4400 [926], [1608]: the struck opening, then each ingredient struck when carried ("I have a ... with me") or red when not ("A 'Dirty Blast'") | the same; the three unseen red forms UNVERIFIED |
| stage 2 | 4400 [1624], 9 rows | row for row (test `journal.stage2`) |
| stage 3 on | no text: title, `quest_journal_reset` (5240), qj_lines 0, the mount, `recipe_for_disaster_subquests` (3757), `if_setevents textlayer 0..7 PAUSEBUTTON` (4402 [53]) | the same (`~rfd_subquest_menu`) |
| a guest's row | RESUME_PAUSEBUTTON on `questjournal:textlayer`, sub = 3757's member index: 0 dwarf, 1 goblins, 2 pirate, 3 guide, 4 Dave, 5 ogre, 6 Amik, 7 Awowogei; answered on its tick with "Recipe for Disaster - <guest>" | `[if_button,questjournal:textlayer]` |
| a subquest not begun | 4400 [2130]..[2193], all eight: "I have to free ...! I can start by examining his/their frozen self/selves in Lumbridge Castle.", a blank row, the requirements struck when met | the seven non-goblin pages rebuilt; the goblins' journal belongs to the session porting that subquest (4402 has every stage) |

## Wire audit: the banquet

Ours: the test's tick log (lock at our tick 102 = [1660], offset 1558), through
`ticklog_tells.py`; recorded: `rsprox_tells.py 4400 --from 1657 --to 2004
--timeline`. Every row below landed on the recorded tick:

| kind | rows |
|---|---|
| cast adds and dels | the 11 guests [1661], the wanderer's del [1741], the invisible monkey [1816]/[1819], Awowogei [1823], the old man [1846]/[1853], the Duke's exit [1864], Evil Bob [1879]/[1885], Osman [1887], the Culinaromancer [1916], Aris [1956], the cast gone and Aris + the frozen Culinaromancer [1999] |
| seqs | the kick and block [1819], the bow [1846], Osman's teleport [1884], the Culinaromancer's three casts [1916], [1934], [1946], [1972] (delay 30), the nine freezes [1935]..[1954] (the pirate's delay 10), the point [1963], Aris's panic and entangle [1968], [1974], the Culinaromancer's freeze [1981] |
| sounds | monkey 630/2565/634 (delay 15)/632 x3, smokepuff synths and the three `sound_area` r7, door 62/60/45, kittens 339, teleport 200/201, bigghost 1595, windwave 222 x2, the eight hit synths with their delays, entangle 1036, whiteout 1039 |
| spotanims and projectiles | smokepuff x8 (height 124), rings (30), teleport_casting, windwave_casting x3, `windwave_travel` x2 (30 -> 500, 12 tiles) |
| locs | the gnome chair [1661] and its del [1823], the open doors [1874], Osman's chair [1884], the elf door [1893]/[1895], the goblin chair [1953], the closed doors [1958], the 84 bubble locs [1979]..[1983] |
| messages | the Duke's nine echoed pages, MUAHAHAHA, Aris's line |

Ours only: region music on entering the copy (song 583 at [1660]) and again
when the banquet jingle ends ([1921] 147, [1923] 583); see Open. Found and fixed
by this audit: the unlock went out at [2000], three ticks early.

## Open

- The subquest menu's row colours read Jagex's subquest varbits (`100goblin`
  1867, `hundred_dave_main` 1878, ...). Our subquests still keep their stages
  in varps 6943..6951 with their own numbering, so every row reads "not
  started" until each subquest moves onto its varbit.
- Requirement rows for quests this world does not run (Big Chompy Bird
  Hunting, Biohazard, Witch's House, Lost City) can never strike.
- The Selim variant of the roll call (after Beneath Cursed Sands, per the wiki)
  is not built: the recorder had not done it.
- In-progress journal pages of the seven other subquests are the old port's
  prose, UNVERIFIED (no recording opens them).
- Aris talked to again later, and the Cook at stage 3 and on, are not recorded.
- Music: our square table (`torirs_server_music_regions.gen.h`) gives the
  dining room square (7507) "Chef Surprise" = track 583 and plays it on the
  copy's entry; Jagex sends none there, plays the scene's 147 by script, and
  163 in the frozen room after the unlock ([2003]). Music areas are sub-square
  here (see the music-areas memory). Our banquet jingle also ends at [1921]
  where Jagex's 205 s one runs past the scene, so ours resumes region music
  mid-scene.
- The client: clicking the scroll's close (op1 "Close", no onop in the cache)
  makes our client send CLOSE_MODAL after the IF_BUTTON, which closes the
  Cook's page the server has just opened in answer ([1621]). The server side
  is right on a `TORIRSSERVER_TRACE_OUT=1` run (the line, the echo, the page);
  the test checks the line.

## Tests

`test/quests/rfd_start.lua` (scriptrun): both Cook talks, the hand-in and
scroll, the stage-2 talk, the stage-2 journal row for row, the door and the
banquet, Aris's five topics, the subquest menu and the goblins' row, the
portal out.
