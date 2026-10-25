# Black Knights' Fortress against RSProx

**Recording:** index 4368, recorded 2026-10-24, rev 241.1, world 631 (a normal world). It covers the whole quest, from Sir Amik [378] to the scroll [861]. The recorder was combat 27, with Defence 1 and 10 Hitpoints, wearing the disguise (bronze med helm and iron chainbody) plus an adamant scimitar and a glory.

**Our run:** `test/quests/blackknight.lua`, scriptrun, 90/90 PASS. It keeps a tick log (`t.ticklog.start()`). The tick numbers in "ours" below come from the run on 2026-10-24 (`build/bkf_private/ticklog_run*.tsv`). Bracketed ticks are from the recording.

**Tools:** `rsprox_interact.py`, `rsprox_pages.py`, `rsprox_cutscene.py`, `rsprox_npcinfo.py`, `npc_block_inventory.py --ours`, `rsprox_player_hits.py`, `ticklog_tells.py`, and the `TORIRSSERVER_TRACE_OUT=2` packet trace.

## Code

| file | what |
|---|---|
| `quest_blackknight/scripts/quest_blackknight.rs2` | doors, secret walls, ladders, grill, hole, reward, the meeting-room aggro, fortress music |
| `quest_blackknight/scripts/bkf_cutscene.rs2` | the cabbage scene, played in a copy of square 47,54 |
| `quest_blackknight/configs/quest_blackknight.npc` | combat for the three knight types; the witch and Greldo wander |
| `areas/falador/scripts/sir_amik_varze.rs2` | page text, moods and ordering |
| `areas/world/configs/m47_54.spawn` + `tools/gen_spawns.py` `NPC_SPAWN_ID_CORRECTIONS` | six `aggressive_black_knight_f` |
| `npc_movement/{f/fortwitch,g/greldo}.move` (authored), `npc_movement.generated.npc`, `server_fields.generated.npc` | lines that conflicted with the content's own `.npc` were removed |
| `quests/scripts/questpoints.rs2` | quest_complete_1 for this quest [861] |
| `skill_combat/combat.rs2` | block sound for the iron chainbody |
| `src/torirsserver/torirs_server_world.c`, `_scripts.c`, `.h` | region fades 0/60/60/0; the track returns at a jingle's end |

## Sections

### Sir Amik [378..406], [854..863]

| recorded | ours before | now |
|---|---|---|
| "Well, I need some spy work doing but it's quite dangerous. It will involve going into the Black Knights' fortress." | LostCity wording | copied |
| "I laugh in the face of danger!" | "...danger." | `!` |
| "Temple Knights? Who are they?" / "That information is classified. I am forbidden to share it with outsiders. " (quiz / neu) | missing | added; offer drain: 13 pages, recorded order |
| [401] on the "Ok, I'll do my best." click: stage 1, "You've started a new quest: @q_mes@Black Knights' Fortress</col>", the dossier with no line, then "Good luck!..." (chatneu2) | page first, then dossier + "Sir Amik Varze hands you a dossier.", mood happy, no start line | recorded order, no hands-you line, neutral |
| [406] carrying the dossier: "Don't forget to read that dossier of information I gave you." | missing | added |
| [860] mesbox on the "Please accept" click; [861] its click: coins, stage 4, the completion, jingle 152 (quest_complete_1), scroll icon `bkfortress_cauldron_broken`, "2,500 Coins" | a queue: coins before the box, icon coins, "2500 coins", jingle by difficulty | recorded order, icon, line and jingle |

### Doors and secret walls (walk-throughs, never swung)

Every pass is the same on the op tick O. An `inviswall` goes in the door's slot (the door's rotation), and the inactive leaf goes on the tile across the wall (rotation + 1) with the player stepped across. Both come back at O+2. The fortress doors use `bigdoor_open` (46). The walls use `scrape` (2410), after "You push against the wall. You find a secret passage." on O. A player who reaches the wall from a tile beside its own tile is stepped onto the wall tile on O, and crosses on O+1.

| pass | recorded | ours now |
|---|---|---|
| bkfortressdoor1 in | [574] side step, [575] pass, [577] back | [89] pass, [91] back |
| bksecretdoor | [578] / [672..673] (side) / [708] | [91], [259], [287] |
| bkfortressdoor3 | [609] / [643..644] (side) | [149]; [202..203] (side) |
| bkfortressdoor2 | [694] on the "going in anyway" page's click, plus "Die, intruder!" | [265] pass + say |

Before, these were LostCity teleports: no locs, no sound, and a `p_delay(1)` side step. Door3 also called `~black_knights_aggro`, which the recording never shows (no say, no burst at [609] or [644]); that call is removed.

### Ladders (15/15 recorded)

`dk_ladder`, `dk_laddertop` and `dk_meeting_ladder` all face the ladder on L+1 and teleport on L+2, with `[sequence] -1` and no climb seq, to the same tile one plane up or down. Ours used to play `human_reachforladder`/`human_pickupfloor` and teleport on L+3. They are now bound by name (`~blackknight_climb`). `dk_meeting_laddertop` gets the same treatment, unrecorded.

### The meeting door's guard [680..694]

The pages carry the head of the nearest guard: `fortressguard_03` two tiles away, not the first guard type found. The player faces that guard's tile on the first page only, and the guard never turns. Ours now does the same (`~blackknight_find_guard` picks the nearest). [684] and [689] closed on the recorder's auto-retaliate; that is the recorder's kit.

### The grill [615..629]

| recorded | ours now |
|---|---|
| L+1 face; [619] `dttd_bend_to_listen_at_door`; [621] `bkf_bent_to_listen_at_door_idle` + page 1 | [179] op, [180] bend, [182] idle |
| pages with the speakers' heads (`grillknight`, `fortwitch`, `greldo`), no face either way | `~chatnpc_specific_anim` (the old proc faced each page) |
| "...not to mention the trees!", "Yeth, Mithtreth.", trailing space after "properties.... ", "Now remember Greldo" chatneu3, Greldo chatneu1 | copied (was "merntion", "Mithreth", angry, goblin mood) |
| [629] the last click: stage 2, `dttd_standing_after_listen_at_door` | [191] |

### The cabbage scene [716..812]

The old version was seven chat pages with no scene. It is now a cutscene in a copy of square 47,54. Before the witch's first page, events are timed from O; after it, from each click (C1..C7, ours 313/319/320/327/329/330/339).

| recorded | ours |
|---|---|
| O [716]: lock (fov_clamp, cutscene_status, ops hidden, minimap 2, gravestone, area sounds 25, draw distance), `bkf_throw_cabbage`, spotanim 771 h96, projectile 772 player→hole 96/196/55/60, cabbage gone, hud lock (5 cam shakes, xp drops and sides closed, 6021, 5485), synth 1420, minimap_toggle 2 | [289] all of it |
| O+5 fade out; O+7 into the copy at (9,41,0), song 147 cut + jingle 5 | [294], [296]; region track held off (`midi_hold`) |
| O+9 cat (19,51), witch (22,53), captain (20,53), all facing south; cam_moveto (27,43) 1500, cam_lookat (24,49) 300, rate 100/100 | [298] |
| O+13 fade in; O+18 witch reset + 2 S, O+21 2 N; O+23 witch faces the cauldron (23,51), player faces witch, page (neutral) | [302], [307..308], [310..311], [312] |
| witch paces while the page waits: P+2 N (reset), P+16 SW, P+26 E, P+27 5 S, P+39 N, P+40 E | npc_queue path from P (314, 328, ...) |
| C1+2 synth 1414; C1+5 captain faces witch, player faces captain, "What's that noise?" (quiz) | [315], [318] |
| C2: player faces witch, "Hopefully...NOOOOOoooo!" | [319] |
| C3: captain reset + 1 S; C3+1 map anim 773 at the cauldron (delay 100), projectile 772 (24,51)→(23,51) 0/100/192/128 h500/50 raw, synth 1416; C3+3..4 captain 2 N; C3+6 cauldron varbit 2494 = 1, "My potion!" (shock) | [320], [321], [323..324], [326] |
| C4 witch `emote_cry`; C4+1 "Oh boy, this doesn't look good!" (scared) | [327], [328] |
| C5 player faces cat, captain `emote_panic`, "Meow!" | [329] |
| C6 sound_area cat_attack (333) at the cat, range 5; chat closed; C6+3 fade out; C6+5 back on the throw tile, cast gone, cam_reset, fade in, minimap_state 0, minimap_toggle 0; C6+8 unlock, stage 3, hud unlock, "Looks like my work here is done..." (happy) | [330] close (area sound: **open**, below), [333], [335], [338] |
| C7: fade overlay closed, Knightmare sent again | [339], [340] |
| jingle end (O+7+58) [781]: Knightmare cut 0/0/0/0 | [355] (the clock; our reader was faster) |

Each click closes the chat once, as recorded ([313], [320], [327], [330]).

### Music

| recorded | before | now |
|---|---|---|
| region changes 0/60/60/0 (9/9 here; 4367, 4363, 4336, 4289 too) | 0/30/0/30 | 0/60/60/0 (engine) |
| [505] Alone in square 47,54; [575] stepping in at the front door: Knightmare unlocked (musicmulti_16 bit 18, "@mes_hl_red@Knightmare") and played | Alone throughout (region music is per square) | the front door's pass plays Knightmare, and Alone on the way out (unverified) |
| a jingle's end re-sends the area track cut: [230]/[235] (Doric's), [781], [871] | never | `ToriRSServer_MusicJingleStarted` + the music clock, after floor(length/600) ticks |

### Knights (combat)

Every swing is `human_sword_slash` for all three types (4 / 4 / 5 swings) with synth `2h_slash` (2503). The kr knight the recorder hit answered `human_sword_def` + `steel` (15). All three types hunt. The generated blocks had `aggressive_black_knight_f` with huntmode none and punches, `kr_aggressive_black_knight` punching, and every attack sound null. Our spawns had no `_f` knights, where the recording has 3 of 8 on the ground floor, 2 of 4 on the first and 1 of 3 on the second.

Damage, 11 splats on the recorder: 1 zero, max 4, values 1..4 with a mean of 2.3. The cache's max is floor(0.5 + 33 × 80 / 640) = 4. For a Defence-1 recorder in this kit the formula predicts 32% zeros, and 1 of 11 has p ≈ 0.09. Ours, with Defence 30: 9 zeros in 14, predicted 67%. Levels are unchanged from the cache (`check_npc_cache_levels.py`: 0). Block sound: the recorder's one 0 in an iron chainbody came with `blade3_mail_2` (2802). Ours gave `human_block_1`; the family is now in `~combat_player_block_sound`. Facing: both sides re-send the player face on chase steps.

### Npc movement

The witch and Greldo walk in the grill room (37 and 23 steps); ours had them stationary. Both are now `wanders` with `source = authored`. The guards, the cat and the captain already wandered.

## Open

1. **Area sound range.** The cat's `cat_attack` (range 5) reached the recorder 10 tiles away in the copy. Jagex sends a zone's area sounds to everyone observing it, while our `ToriRSServer_WorldSoundArea` sends only within range, so ours sends nothing. This is engine-wide; not changed here.
2. `hpbar_hud_show_extra_bar` (varp 5991) −1→0 with the first fade [721]: not sent (HP HUD).
3. Projectile heights: v2 raw heights 130 and 50 become 32 and 12 in our byte (128 and 48).
4. The witch's pacing is timed from her page opening, as the recorder waited 41 ticks there. A faster reader moves on while she still paces.
5. Only one jingle comeback is tracked, so after Doric's two jingles ours sends only [235]'s, not [230]'s.
6. Knightmare is played only by the front door. Logging in inside the fortress, or another way in, gets the square's Alone.
7. Fitted to this one session: which dump tiles hold `_f` knights, and the iron chainbody's five takes.
8. Unrecorded and kept as LostCity's: the guard's challenge without the disguise, the front door outward, the meeting door from inside, the magic cabbage, the grill after sabotage, Sir Amik with no dossier or no room. Also unrecorded: the knights' death seq and sound.
