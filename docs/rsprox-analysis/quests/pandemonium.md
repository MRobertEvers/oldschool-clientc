# Pandemonium vs RSProx index 4407

RSProx index 4407: the owner's own rev-241 playthrough on 2026-10-29, world 611, 1,887 ticks from login to the
scroll. It is the only Pandemonium recording on the index. Transcripts:
`~/.rsprox-index-cache/home-{default,bas,raw}/.rsprox/binary/archive/idx4407.txt`.

Before this port, our quest was a quest-helper placeholder. Its "soft-skip" boxes and invented
dialogue reached the end through stage numbers that meant something else; none of its pages were
OSRS text. The port replaces it whole:

- `quest_pandemonium/scripts/pandemonium.rs2`: Port Sarim, the journal, the scroll.
- `pandemonium_boat.rs2`: Will and Anne's boat, the wreck, the overboard scene.
- `pandemonium_island.rs2`: the island, the Shipyard, the raft legs, Old Grog.
- `configs/pandemonium.{constant,spawn,npc}`.

Branches 4407 never took come from the OSRS wiki's `Transcript:Pandemonium` and
`Transcript:Pandemonium/Journal`, read 2026-10-29. Those are the refusals, Ribs' full tale, the
lost cup, and the re-talk lines. Their moods are unverified.

## 1. Stages (`%varb18314_sailing_intro`, on `sailing_intro_primary`, perm, transmit)

| stage | R tick | written by (R) | ours |
|---|---|---|---|
| 2 | [227] | "Yes." at the start menu; "You've started a new quest" goes out with the next page | same, `@pan_offer` |
| 4 | [252] | Anne's "Just let us know when you're ready..." | same page |
| 6 | [325] | first boarding of Will and Anne's boat | `@pan_board_again` |
| 8 | [378] | Anne's "Over to you, <name>! Take the helm when ready." | same page; the helm hint goes out with it (R: one tick later) |
| 10 | [473] | the helm's op | same op |
| 12 | [826] | the sail-down click inside the wreck's water (+ `reached_wreck` 1) | the boat watcher, on the tick it sees sail mode 0 (one tick after the click) |
| 14 | [835] | the "*sigh*" page | same |
| 16 | [905] | the beach walk, with the cup | same |
| 18 | [920] | Steve's "Aye! To be sure!..." | same |
| 20 | [929] | Steve's "Not a lot, I'm afraid..." | same |
| 22 | [1082] | Ribs' "You should talk to Steve..." | same |
| 24 | [1122] | Steve's "Jolly Roger!" | same |
| 26 | [1147] | "Er... I do have this cup I found." | same |
| 28 | [1165] | "You give the cup to Junior Jim." (+ `sailing_boat_1_*`: the raft is owned) | same; the raft via `~sailing_new_hull` |
| 30 | [1264] | the cargo hold built | `boat_customisation.rs2 ~sailing_custom_build` -> `~pan_cargo_hold_built` |
| 32 | [1283] | "Junior Jim gives you a log book." | same |
| 34 | [1287] | Jim's request in the log (`port_task_slot_0_id` 599) | same |
| 36 | [1313] | the raft boarded at the Pandemonium | `[timer,pan_raft_leg]`, the tick after boarding |
| 38 | [1536] | docked at Port Sarim | the raft watcher, the tick after |
| 40 | [1604] | boarded at Port Sarim with the crate | the raft watcher |
| 42 | [1768] | docked at the Pandemonium | the raft watcher |
| 44 | [1783] | "You deliver the crate of ship parts." | `port_tasks.rs2` delivery hook -> `~pan_cargo_delivered` |
| 46 | [1823] | Jim's "Don't mention it, mate." (log -> `sailing_log`) | same |
| 48 | [1856] | Old Grog departs | same |
| 50 | [1869] | the scroll (qp 67->68, quests completed +1, 25 sawmill coupons, Sailing 100->300 xp) | `~pan_quest_complete` |

Other recorded vars: `sailing_intro_reached_wreck` (18315) [826] and
`sailing_intro_received_repairkits` (18316) [1815]. Both are written by ours.

## 2. Actions (`rsprox_actions.py`), a verdict per row

| R tick | action | verdict |
|---|---|---|
| [2] | welcome screen Play | engine |
| [98], [108] | glory: rub, teleport to Draynor | generic (jewellery), not this content |
| [110]..[131] | bank booth, withdraw glory, wear | generic (bank) |
| [200] x3 | Will (Port Sarim) Talk-to | **this content**: the offer, the "No." refusal [215], then "Yes." [227] |
| [386] | Will aboard | **this content**: stage 8 talk |
| [392] x15, [398] x11 | the quest list, Pandemonium's journal | **this content**: journal (section 4) |
| [399] x4 | Port Sarim gangplank | **this content**: aboard their boat, "Now's not the time to be disembarking." [400]; from the dock, "You should speak to Will and Anne if you wish to reboard their boat." [428] |
| [404] x2 | the salvaging hook | **this content**: before the wreck [405]; the scene [854] |
| [409], [591] | Anne aboard | **this content**: stage 8 / 10 talks, "Actually, I want to go back to Port Sarim." |
| [453], [454] | sarim_crate3 / sarim_crate | generic (Port Sarim scenery), not checked |
| [455] | Anne (Port Sarim) | **this content**: "Hello there, <name>. Shall we hop back aboard our ship?" |
| [473] x9 | the intro helm | **this content**: stage 10, the helm hint; the take itself is generic (`~sailing_op_helm`) |
| [481] x40 | sidepanel facilities click layer (sails) | generic sailing controls: sailing agent; the intro reacts in its watcher (hints 3/4, the stop) |
| [530]..[549] | sidepanel tabs, switch to combat tab and back | generic: sailing agent |
| [550] x3 | the intro sails (Trim) | **this content** (trim cheer, hint 6); the trim is generic |
| [748] | sailing_tern op2 | generic sea fauna, not checked |
| [841], [850] | Anne / Will at the wreck | **this content**: "Time for some salvaging..." |
| [937], [1061] | Ribs | **this content** |
| [939] x6 | Polly | **this content**: two recorded overheads + `lore_macaw` area sound |
| [948] x2 | Steve Trade | Prying Times' `~pry_steve_shop` (pub_pandemonium) |
| [950] x3 | Steve Talk-to | **this content** |
| [972] | Adventurer Ada (sailing_crew_generic_2) | **this content** (her pages) |
| [984], [992] | Rosalee / Dawson | **this content**: the argument |
| [1001] | Fiora | **this content** |
| [1040], [1050] | trader Stan crew op4, sailing_menu 72 | generic (charter): sailing agent |
| [1132] x4 | Junior Jim | **this content** |
| [1186] x3 | Junior Jim at the Shipyard | **this content** |
| [1205], [1209] | crate of saws / hammers | **this content** |
| [1216], [1227] | Shipyard workers 1 / 4 | **this content**: the two recorded exchanges |
| [1244] x2 | the Shipyard's gangplank | **this content** (on board at the pier; off to the pier, `~pan_shipyard_plank`) |
| [1249] | raft facility hotspot | generic customisation (939); Jim pays at stage 28 |
| [1276] | Shipyard exit portal | generic `~sailing_shipyard_exit` |
| [1295] | seagull | generic |
| [1298] | Pandemonium port master | generic `port_masters.rs2`: the pre-quest pages match [1298]..[1302] |
| [1310] | Pandemonium gangplank (op2, Board) | generic boarding; stage 36 by the watcher |
| [1319] x5 | raft helm | generic |
| [1362], [1367] | world map | generic |
| [1369] | charting spot (mudskipper) | generic: sailing agent ("You don't have any reason to try to chart that. You need to complete the Pandemonium quest first.") |
| [1390] x3, [1764] | raft cargo hold, withdraw the crate | generic: sailing agent |
| [1398], [1483] x3 | raft sails op2 / op1 | generic |
| [1555] x5 | Port Sarim ledger | generic `port_tasks.rs2`: sailing agent (hands-full box, pick-up box, the discard confirm, "S.W.A.T." line) |
| [1566], [1577] | unequip the shield, wield with cargo held | generic (cargo held in hands) |
| [1778] | Pandemonium ledger (deliver) | generic + the intro hook |
| [1838] | Old Grog | **this content** |
| [1875] | quest scroll close | engine |
| [1886] | logout | engine |

## 3. Teleports (a verdict per row)

| R | from -> to | cause | ours |
|---|---|---|---|
| [102] | 2717,3303 -> 3105,3251 | glory to Draynor | generic |
| [325], [465], [642] | Port Sarim dock -> boat deck | boarding Will and Anne's boat | `vessel_board` onto a config-4 hull spawned at Port Sarim's anchor |
| [423], [611] | deck -> 3050,3193 | "fine, we'll go back" | `~pan_leave_boat` + `p_teleport(^pan_sarim_landing)` |
| [857] | deck -> scene copy 1498,4510,1 | the hook scene | copy of square 23_70, deck tile (26,30) |
| [892] | copy -> beach copy 3034,2995 | overboard | copy of square 47_46, (26,51) |
| [907] | beach copy -> 3049,2970 | the scene ends | `^pan_arrival` |
| [1169] | 3059,2978 -> 2084,2730 | Jim to the Shipyard | `~sailing_shipyard_go` + the copy's (36,42) |
| [1245], [1273] | pier <-> raft | the Shipyard gangplank | `[oploc1,sailing_gangplank_shipyard]`, `~pan_shipyard_plank` |
| [1278] | Shipyard -> 3058,2978 | Jim back home | `~sailing_shipyard_exit` + `^pan_jim_return` |
| [1313], [1536], [1604], [1768] | the raft's boardings and dockings | generic gangplank | generic |

## 4. The journal (opened at stages 8, 10, 14, 20, 38, 42, 44 and 46)

Rows [392], [431], [572], [615], [846], [932], [1544], [1771], [1800] and [1831] are copied verbatim into
`[proc,pandemonium_journal]`:

- current journal format: each finished step is a paragraph struck row by row with `<str>`, no
  blank rows between paragraphs;
- the current step is in `<col=000080>`, with names in `<col=800000>`;
- at stage 8 and 10, a player ashore gets the extra sentence "I should speak to them in Port
  Sarim when I'm ready to reboard their Boat." ([431], [615]).

Every other stage's current step is the wiki's Journal transcript. Those are **unverified**,
wiki typos ("a a job") included:

- 2/4 (before setting sail);
- 6 (aboard);
- 12, 16, 18, 22, 24;
- 26/28, 30, 32, 34/36 (before retrieving the cargo);
- 48 (shows 46's text);
- the completed journal's ninth paragraph.

Wraps: the rows are produced by `~journal_struck` and the engine split at 350 px. They have not
yet been diffed row for row against the recording in `test/quests/_journals.lua`. That file is
being edited by another session right now, so it is open.

## 5. Pages

`rsprox_pages.py` over 4407 gives 324 rows. Every recorded page text, `<br>` read as a space and
the recorder's name as `<displayname>`, is in our scripts. There are 11 exceptions, each assigned:

- the two Shipyard workers, added;
- the ledger's four boxes and the discard menu (generic port tasks, sailing agent).

Moods are copied from each page's `if_setanim` (`chathap*` -> `^chat_happy`, `chatcon*` ->
`^chat_confused`, ...). Menus are copied in recorded order. The start menu's header is "Start the
Pandemonium quest?".

## 6. The boat leg ([325]..[835])

| R | ours | verdict |
|---|---|---|
| boarding frame: fade 948 (0,255,0,0,15) + minimap_state 2 + minimap_toggle 2 on T; the teleport, "You board the boat.", synth 10754 and the sidepanel on T+1; fade in on T+2; the intro HUD (345) on T+3 | `@pan_board_again` | shape matched; the sidepanel frame itself is the sailing agent's |
| the deck: keel 2x5 bronze (2,1,p0), skiff trim (1,1,p0), `sailing_intro_navigating` (4,6), `sailing_boat_mast_intro` (4,4), the hook (3,2) rot 1, the sails (4,5), three ambience locs, Will (3,2), Anne (4,3) | `~pan_deck_built` (config 4, template 3840,6400) | matched by tile |
| `sailing_intro_hint` 627 steps 1/2/3/4/5/6/7/12/0/-1 + the hint arrows (helm W 70, sails S 250, hook W 90) | `~pan_hint`, `hint_coord` | matched by step; arrow tiles are deck offsets |
| npc overheads while underway (Will/Anne headings, "This isn't right!" + the red warning, gust calls, trim cheers, "I think this is it!", "Stop here...") | `[timer,pan_intro]` | lines verbatim (wiki lists all 19 headings); **cadence fitted** (35-tick heading lines, 68-tick "Stop here") |
| the sails' loc_anims per state | `~pan_sail_visual` (steady states) | **the transition seqs (down_to_half, half_to_full, ...) are not sent** |
| the sails loc's opflags (0b10 stopped, 0b10000 under sail, 0b10001 in a gust) | plain `loc_add` (all cache ops) | **open** |
| the wreck: R stopped at 3038.25,3029.25 ([826]) after "I think this is it!" at [750] | `^pan_wreck_centre` 3038,3029, radius 12 | **fitted** to the two readings |
| "forced to take control of the boat" | the warning only | **open**: 4407 never triggers the takeover |

## 7. The overboard scene ([855]..[911])

Copied tick by tick in `@pan_overboard`. The frame:

- the lock at T+2 with `fov_clamp` 3 and the rooftops hidden (17775);
- camera rate 232/100, two copies (the boat template square, then the beach), 50-cycle fades;
- the hook drop/reset seqs on the player and the loc;
- the double objbox (medallion, cup), the step towards Will, the push (`sailing_intro_human_overboard` + `sailing_intro_anne_push`);
- `human_onfloor`, `human_getup_priority_10`, the 80-cycle fade in, the beach walk;
- the stage with the cup, the unlock and Steve's first page.

Not yet checked against a trace: the camera on our wire (`TORIRSSERVER_TRACE_OUT`), and whether
the template square 23_70 actually holds the hook loc at (26,30). The drop/reset `loc_anim` needs
that loc.

## 8. Ambience and npcs

`configs/pandemonium.spawn` adds the cast our world lacked: Will and Anne at Port Sarim; Ribs,
Polly, the crew registrar, Fiora, Rosalee, Dawson, Ada, Brass Hand Harry and Old Grog on the
Pandemonium. Tiles are the first `[add]` in 4407.

The six that never step in 4407 get `wanderrange=0`, and their generated `wanderrange=5` lines
are deleted: Will, Anne, Polly, Rosalee, Dawson, Grog.

The spawn format has no facing, so 4407's spawn angles are not reproduced.

## 9. Engine and generic hooks

- `torirs_server_world.c`: vessel config 4 builds from template 3840,6400 and never takes an owned-boat slot.
- `boat_customisation.rs2`: `~sailing_boat_row(4)` = `sailing_boat_will_anne`; the stage-28 build is free (Jim's materials).
- `boat_facilities.rs2`: `sailing_deck_built` hands config 4 to `~pan_deck_built`.
- `boat_gangplank.rs2`: `@sailing_gangplank_run` asks `~pan_gangplank_intercept` / `~pan_shipyard_plank` first.
- `port_tasks.rs2`: task 599's delivery calls `~pan_cargo_delivered`.
- `player/login.rs2`: `~pan_login` re-arms the raft watcher.

## 10. The test

`test/quests/pandemonium.lua` plays 4407's flow end to end on scriptrun:

- the interview, the briefing, the helm, sailing Will and Anne's boat to the wreck;
- the overboard scene and the island talks;
- Jim, the Shipyard tools and the cargo-hold build;
- the raft to Port Sarim and back with the crate, the delivery;
- Jim, Old Grog, and the scroll.

It checks every stage value on its recorded step. The private OPT binary `src/torirs_pan` was
built from this tree.

Run `pandemonium_r15` (2026-10-29): 106 rows passed, stage 50, the scroll and 25 sawmill coupons.
Two rows failed, both in the test, not the content: a hull field name, and a first leg that
clipped the Port Sarim pier post. Both were fixed for the next run.

Found and fixed on the way:

- Combined navigation (one player works helm and sails) and the sidepanel's boat row now include
  config 4. The cache row says `combined_navigation=true`.
- Pages opened from a timer or an interface button now come from queues: the wreck arrival, and
  Jim's page after the build.
- The cargo-hold interface closes with the build ([1263]).
- The raft's hotspot marker is placed.
- The cargo hold's op1 with a crate in hand ("Deposit-held") and the ledgers' op1 take/deliver
  sat in the old quest file; they are restored as generic bindings in `boat_cargo.rs2` and
  `port_tasks.rs2`.

## 11. Open

- **Sidepanel, config 4:** the Facilities tab lists no sail rows on Will and Anne's boat. 4407
  shows them, so the test sets the sails with the deck loc's own "Set"/"Un-set". This is with the
  sailing agent.
- **Client, multiloc ops:** our client sent the hook's multiloc rung id (60482) in OPLOC1, where
  4407 sends the base (60480). The quest binds both. This is with the sailing agent.

- Stage 12 and the raft legs' stages (36..42) are written by watchers one tick after the recorded click.
- The sail transition seqs and the sails' per-state opflags (section 6).
- The wreck's stop zone is fitted to two readings, not a recorded boundary.
- No tick log has been compared row for row yet (sounds, seqs, the scene's camera); the quest test
  (`test/quests/pandemonium.lua`) is the first run.
- The journal's wrapped rows are not yet diffed against the recording in `_journals.lua`.
- Steve's, Ribs' and Jim's lines at stages 4407 never visited (wiki, moods unverified); Ribs has no
  line outside stages 20 and 22.
- The port master's quest-time lines (wiki: "I'm here to pick up some cargo for Junior Jim...")
  are not ported; ours gives the pre-quest talk recorded at [1298].
- The repair kits go into the raft's hold when the raft is materialised. Otherwise they fall back
  to the backpack. The spyglass is the hold's tool flag (unlocked at stage 50).
- 4407 has no world-entity info. The boat's motion, and the replay's client bugs (the player drawn
  as a boat after disembarking, the hitching), are with the sailing agent.
