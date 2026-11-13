# Dragon Slayer I: rebuilt from RSProx 4555..4559

Recorded 2026-11-12 on rev 242.1, one account across five sessions. All of
the quest is on the index:

| session | what | stage |
|---|---|---|
| 4555 | quest list, the not-started overview (`questjournal_overview_setup` string) | 0 |
| 4556 | the Guildmaster's start, Oziach, the briefing, the Duke's shield, the sailors, the ship bought | 0 -> 3 |
| 4557 | the Rusty Anchor: Ahab, Jack Seagull, Longbow Ben | 3 |
| 4559 | Ned's first ask | 3 |
| 4558 | the goblin generals, Wormbrain, Melzar's Maze, the Oracle and Thalzar's door, the hull, Ned, both voyages, Crandor, Elvarg, Oziach's completion | 3 -> 10 |

Our implementation was LostCity's 2004 Dragon Slayer: none of its stage
numbers past 3 were Jagex's, it kept its own state in seven server varps, and
it wrote a 0..3 repair counter over the whole of varp 177, where Jagex's
varbits live. Tools: `rsprox_actions.py`, `rsprox_pages.py`,
`rsprox_interact.py`, `rsprox_cutscene.py` (the voyage draft), plus a
page-text check and a journal extractor (scratch).

## Stages and vars

Every write, with the click it goes out on. Varbits are on varp 177
(`dragonquestvar`), as `configs/all.varbit` declares them.

| var | value | when | ours before | ours now |
|---|---|---|---|---|
| dragonquest | 1 | 4556 [161], the click of "So, what is this quest?" | 1, on the Guildmaster's 2004 line | same click |
| dragonquest | 2 + `instructions_melzar/oracle/wormbrain/ship/shield` = 1 | [417], the click of "But how can I defeat the dragon?" | 2 mid-talk, no varbits | same |
| `champion_explained_elvarg` | 1 | [513], the click of the story's last page | server varps | same |
| `instructions_*` | 0 each | [560]..[597], as each topic is told | n/a | same |
| dragonquest | 3 | [1228], the click of "Yep, sounds good." | 3 | same |
| `asked_generals` | 1 | 4558 [417], the click of "Where would he be?" | missing (no talk) | same |
| `asked_ned` | 1 | 4559 [120], the click of "To the island of Crandor." | server varp | same |
| `secret_told` | 1 | 4558 [3458], the click of the Oracle question | server varp | same |
| `used_wizards_mind_bomb/silk/lobster_pot/bowl_unfired` | 1 each | [3796]..[3829], each offering | server varp bits | same |
| `ship_onethird/twothird/fullyfixed` | 1 each | [4361], [4369], [4375], each plank | a 0..3 counter in the whole varp | same; the hole multiloc reads bit 10 |
| dragonquest | 6 | [4375], the third plank | 7 | 6 |
| `ned_knows_ship_is_fixed` | 1 | [4427], the click of "You did? Excellent!" | missing | same |
| dragonquest | 7 | [4428], the click of "Here you go." | 8 | 7 |
| dragonquest | 8, the hull bits back to 0 | [4681], the beach | none (a server "sailed" varp) | same |
| `sailing_crew_ghost_jenkins_visibility_control` | 3 at [4680], 0 at [5302] | the crash, the completion | missing | same |
| `crandor_found_secret_door` | 1 | [5094], the secret door from the lair side | server varp | same |
| dragonquest | 9 | [5174], 6 ticks after the kill | 10, completed on the kill | 9 |
| dragonquest | 10, qp +2, xp | [5302], the click of Oziach's "Yer a true hero..." | n/a | same |

The cache backs the numbers. Elvarg's multinpc shows her at 1..8, Captain Ned
(`dragonslayer_ned_on_ship`) is aboard at 7..10, and Cabin Boy Jenkins is
aboard at 0..7. The old 8/9 split had Ned at the wrong stage and Elvarg
visible after the kill. Retired: varps 6414..6418, 7861 and 7865's old use;
7865 is now `dragon_scroll_pending`, a temp flag for Oziach's menu after the
scroll.

## Sections

| section | recording | before | now |
|---|---|---|---|
| Guildmaster | 4556 [8]..[618] | 2004 talk, 21 of 22 pages missing | every page, mood, menu, the topic menus that drop the asked topic, the Pandemonium and Murder Mystery lines |
| Oziach | [49]..[433], 4558 [5279]..[5310] | 2004 (map pieces told by Oziach, completion on the kill) | the recorded flow; completion and scroll at Oziach, his menu after the scroll |
| Duke Horacio | [749]..[853] | one 2004 page | every branch, "Well, are you sure?" header, "a heavy orange shield" objbox |
| Klarense | [1156]..[1279], 4558 [4526], [4803] | 2004 | sale, captain menus, after-crash greeting |
| Cabin Boy Jenkins | [1286]..[1300], 4558 [4384] | matched | moods confirmed; wiki stage-7 exchanges added |
| Ned | 4559 [108]..[153], 4558 [4410]..[4429] | 2004 ("There is a wistfull look...") | recorded |
| Goblin generals | 4558 [404]..[438] | no map question at all | added |
| Wormbrain | [668]..[1075] | no greeting, goblin moods, 4-option menu after "kill you" | "Whut you want?", recorded moods, menus, drop line |
| Melzar's Maze | [1321]..[3085] | doors by use only; wardrobes, bookshelves, trapdoor unbound | Open uses the key; 3 wardrobes, 17 notes, trapdoor line |
| Oracle, magic door, Thalzar's chest | [3415]..[3875] | all four offerings at once; no inscription | one at a time on Jagex's bits; the door swing, crossing and temp locs; the 4-page inscription |
| Map pieces | [871], [3085], [3875], [3881], [3884] | mesboxes, old text | object boxes; the map opens `dragon_slayer_qip_map` |
| The hull | [4353]..[4377] | use-only, 3 hull copies, wrong ladder tiles | Repair op, `human_poh_build` + `poh_build_wood`, one hull, recorded landings |
| Voyage 1 | [4539]..[4695] | two game messages | the whole scene: ship_journey, the Crandor close-up map, the copy at sea, cast, camera, storm and Elvarg overlays, fireballs, fire locs, Jenkins's death, CRASH, the beach |
| Voyage 2 | [4968]..[4988] | n/a | the map for 14 ticks, the beach |
| Crandor | [5088]..[5121] | messages | the secret door's line and varbit, "You climb over the low wall." |
| Elvarg's death | [5168]..[5203] | completed on the kill, teleported out | `dragon_death_elvarg`, body locs, head-cutting seqs and sounds, the victory jingle, the head |
| Sailors, pub, charter | 4556 [1031], [1106]; 4557 [88]..[108] | 2004 or invented lines | the recorded Crandor rows and answers |
| Journal | 11 opens | 2004 | every recorded row verbatim (`~quest_journal_rows`); the not-started overview string |

## Wire audit (our tick log vs the recording)

Our side is the quest test's tick log (`build/quest_gate/dragon/ticklog.tsv`, run of 2026-11-12). Dialogue is paced by the reader, so every scripted beat is placed by the click it follows.

### The voyage (4558 [4544]..[4695]; ours S = 662)

| beat | recorded | ours | verdict |
|---|---|---|---|
| lock, crest of a wave (0,60,60,0), silence, jingle 172 | S+2 | S+2 (664) | matched |
| cast added in the copy (Ned, Jenkins) | S+19 | S+19 (681) | matched |
| crest of a wave (0,0,0,0) on the player's page | [4592] | 685, on the same page | matched |
| wet and wild (360), thunder, clouds box | click of "Not long now!" | 687 on that click | matched |
| Jenkins walks 3 east | page+1 | page+1 (693..695) | matched |
| flapwings, Elvarg overlay | click of "Oh, well..." | 695 | matched |
| dragonbreath; 3 fireballs (0/55, 30/85, 60/115; offsets 28,22->17,12 / 25,22->16,12 / 30,22->15,12), 3 fire spotanims and burningfire at 55/85/115 | click [4646] | 699, same fields | matched |
| panic seqs, fire 1x1 x3 on +2, +3, +4 | [4648]..[4650] | 701..703 | matched |
| dragonscream, fly-over | click [4659] | 704 | matched |
| cast re-sent via the deck above, fire 2x2 + 1x1 on level 2 | +1, +2 | 705, 706 | matched |
| 5 fireballs (0..175), 5 burningfire delays, fire 2x2 | +3 | 707 | matched |
| panic flap/panic, jen_scared delay 6, human_death + hit/death sounds, fire row | +4..+8 | 708..712 | matched |
| Jenkins hit splat 10, bar empty | [4666] | missing | **fixed** (`npc_hitmark`) |
| Jenkins gone, fire 2x2, Ned's page | +9 | 713 | matched |
| crash sound, CRASH box, black | click | 717 | matched |
| Ned gone; player on the beach | crash+3 | 720 | matched |
| stage 8, hull bits 0, stand-up seq, black lifts | crash+4 | 721 | matched |
| region music on the beach during the lock | none | 721 | ours only (engine region music during a cutscene); open |

### Magic door, chest, hull

| beat | recorded | ours | verdict |
|---|---|---|---|
| "The door opens...", loc_anim, magicdoor area sound r7 | O+1 | 159 | matched |
| temp leaves + 2 inviswalls, door out, player across | O+7 | 165 (+6 from the anim) | matched |
| door_open loc | O+10 | 168 | matched |
| door back | O+12 | 170 | matched |
| Thalzar chest: openchest + chest_open, then the open loc | page click, +1 | 179, 180 | matched |
| search: chest_close + shut loc + piece on one tick | [3870] | 184 | matched |
| hull Repair: poh_build + poh_build_wood, then varbit + box at +2 (x3) | [4359]/[4361]... | 602/604, 607/609, 612/614 | matched |

### Elvarg's death (D = death seq)

| beat | recorded | ours before | verdict |
|---|---|---|---|
| body loc | D+5 | D+6 | **fixed** (one wait shorter) |
| stage 9 | D+6 | D+7 | fixed with it |
| remove_head_part1 + cutoffhead1 (35) | D+10 | D+11 | fixed with it |
| line, part2, no-head body, cutoffhead2, silence + victory jingle | D+16 | D+17 | fixed with it |
| death seq delay 7, dragon_death area r10 delay 7, dragonstomp3 delay 70 | D | delay 0, r12, no stomp | handed to the Elvarg audit |
| jingle 267's length | 8,333 ms | 4,800 ms | engine length table; open |

### The lair scene (4558 [5033]..[5058]): ported, not in the first audit

| beat | recorded | ours |
|---|---|---|
| lock + black + Attack 2 + empty box | O+1 | O+1 |
| into a copy of square 44,150 at 24,24 | O+4 | O+4 |
| cutscene Elvarg at 38,38 facing west, camera 40,50/32,39 (150/475, jump), black lifts | O+5 | O+5 |
| pan (50,40 150 / 32,39 250, rate 2) | O+8 | O+8 |
| 7 dragonwalk synths 25..175 | O+10 | O+10 |
| 5 steps west | O+11.. | O+11.. (player-phase walk) |
| black; real cave 2833,9658; black lifts; unlock + Dangerous Road | O+14, +17, +18, +21 | the same |

### Npc info and vars

- `npc_block_inventory.py` over 4556..4559 against our tick log: the cast's faces and seqs match (Ned 8/8, Jenkins 5/5 + 4/4). Wormbrain's `[say] "Ow!"` and the missing death were **fixed**: he never dies (`death_script`), heals, says "Ow!" and drops the piece ([834]..[835]). Melzar's lines were **fixed** to the recorded text ("...pet rock." without "!"). Most other MISSING rows are maze monsters the recorder fought and the test does not. The talk npcs' facing counts differ only by how many talks each side made; the engine turns a talked-to npc to the player.
- `npc_movement_audit.py`: the generals walk in the recording (14-20 steps a life) and stand in ours. That is Goblin Diplomacy's content; open.
- `varp_transmit_audit.py`: varps 176 and 177 reach the client. No quest var is unsent.
- Journal: `test/quests/dragon_journal.lua` sets each of the 11 recorded states and reads the journal. **11/11 match row for row.**

## Action inventory (4556, 4558)

| action | verdict |
|---|---|
| championdoor, guildmaster, oziach, poordoor (huts, jail), duke, elfdoor, spiral stairs | this content / generic door, rebuilt or unchanged |
| klarense, dragonshipgangplank_on/off, dragonshipladder(top), dragonslayer_shiphole Repair, cabin_boy_jenkins, ned, dragonslayer_ned_on_ship | this content, rebuilt |
| goblin generals, goblin_outpost doors/crates/sacks, 100_goblin ladders | generals rebuilt; the rest generic, not diffed (open) |
| wormbrain op1/op2, jail_pirate, jail_thief, jail guards, prison ladders and trapdoor | Wormbrain rebuilt; the jail npcs already have wiki talk; ladders generic |
| melzardoor, coloured doors, funexit, ladders, ladder_broken, spookywardrobe, bookshelves, trapdoor, funchest, maze npcs | this content, rebuilt; maze npc fights not audited (open) |
| oracle, fai_dwarf_trapdoor_down, dragon_slayer_qip_magic_door (op1, four uses), oraclechest | this content, rebuilt |
| mappart1-3 study and join, dragonmap read, the map's Close | this content, rebuilt |
| dragon_slayer_qip_ruin_entrance, stalagtite_jump, dragonsecretdoor | this content; the ruin entrance's scene is not ported (open) |
| elvarg (attack) | Elvarg's fight is not audited (open: boss rule) |
| bank booths, GE, tablets (Varrock, Falador, Lumbridge, Kourend), glory rubs, ring of dueling, potions, prayers, sharks | generic or the recorder's kit; not diffed here |
| shipmonk, sarim crates, sailing port npcs, port task board, pest squire, Trader Stan's charter map | sailing/transport content; only the Crandor answers are this quest's |

## Test

`test/quests/dragon.lua` follows the recorded flow; `test/quests/dragon_journal.lua` checks the journal (11/11); `test/combat/elvarg.lua` is the fight (32/32, see `dragon_slayer_i_elvarg.md`).

Last full quest run (2026-11-12, with the audited Elvarg): **254 pass, 3 fail.** Every page passes, along with:
- both cutscene rows (voyage: 30 keyframes; lair: 10, reset seen);
- the hull, Ned, the Duke's shield;
- the shielded fight (lowest hp 44/99), her death beats, the head;
- the completion, scroll, points and XP.

The three failures:
- two teleport-cast timing rows (the same row fails in other quests' ledgers);
- the secret door's settle (it has flaked before).

## Open

- **Elvarg's fight.** The boss rule calls for a full combat audit by one Opus agent (§13, §13b, §9). Not done. One fight is recorded (4558 [5121]..[5168]). Only the death seq and the dragonfire spam lines were changed.
- **Camera and interface packets** (the cutscene cams, overlays, fades) are not in the tick log. The cutscene rows check the camera keyframes; a `TORIRSSERVER_TRACE_OUT=2` diff of the interface packets has not been done.
- **Unrecorded branches**, taken from the wiki and marked in the scripts: Klarense's refusals, Ned's first-meeting greeting and its condition, the no-map hand-over, Captain Ned's "not quite ready", Ned on Crandor, the Guildmaster after the kill, Oziach after the quest. The shield and visage branches are not written. Journal stages 6, 7, 8 and 10 are also unverified.
- **Non-quest pages these sessions recorded** that are still missing: the Jack/Ben "So are you pirates?" alternate, Ahab's beer line, Jobless Jim and the Sailing crew, and the rope/wig lines in Ned's own talk (recorded wording differs from ours).
- ~~The Duke can't be reached in the test.~~ Fixed: his spawn now follows the recordings (3209,3223; `tools/gen_spawns.py` NPC_SPAWN_RELOCATIONS). On 2026-11-12, all 3 full runs had Duke Horacio standing in the bedroom part of his study, behind a wall from the elfdoor side ("I can't reach that!", 4 presses). His dialogue is rebuilt, but the test gets no shield and fights Elvarg without one. The cause is his pathing or spawn, not this port.
- **Engine: a hidden multinpc still hunts.** Elvarg respawned and killed a stage-9 player she is not drawn for. Her AI now drops such targets and respawns in 50 ticks (wiki). The engine itself should skip a -1 child when it picks hunt targets.
- `script7192` on the head's arrival and the two stray synths during the voyage ([4585] clock tick, [4605] homunculus) were not copied.
