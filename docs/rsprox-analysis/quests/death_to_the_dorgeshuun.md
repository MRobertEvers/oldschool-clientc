# Death to the Dorgeshuun — RSProx port

Recording: RSProx index 4523 (rev 242.1, world 370, 2026-11-11), the whole quest
end to end (ticks 2590–10424), plus the H.A.M. Store-Rooms at stage 6 (7499–7556)
and the Lumbridge tour with the 2026 tutors (Cadmus, Wilfred, Feoras, Captain Harlan).
Transcripts: `rsprox_index.py get 4523` (default and `--bas`).

The previous port (`dttd_*.rs2`, 2026-08/09) was a quest-helper/wiki reconstruction:
442 of the 519 distinct recorded pages had no counterpart and its stage numbers and
store-room/mill mechanics were invented. It was rebuilt from the recording.

Files: `server/scripts/quests/quest_deathtothedorgeshuun/`
- `dttd_shared.rs2` — Zanik the follower (forms, `follower_npc`/`follower_obj`, placement beside the player), no-continue pages, robes.
- `dttd_start.rs2` — Mistag after The Lost Tribe (offer, ore, way out / water mill), Dorgesh-Kaan's door, Zanik in the cellar.
- `dttd_tour.rs2` — her talk menu and remark pool, the goblins, the sun scene, the leash, the trapdoor refusal, the mark flashback.
- `dttd_tour_npcs.rs2` — Zanik with Lumbridge's people (citizens, Duke, Cook, Hans, Donie/Gee, Guide, Bob, Father Aereck, store, Fred, Millie, border guard, Urhney, candle seller).
- `dttd_tour_tutors.rs2` — Zanik with Cadmus, Wilfred, Feoras, Captain Harlan, Monlum, Finlay (the tutors' own talk: `areas/lumbridge/scripts/<npc>.rs2`).
- `dttd_ham.rs2` — the hideout with Zanik: guards, members, Sigmund, Johanhus, the preaching and rubble triggers, the hidden trapdoor.
- `dttd_storerooms.rs2` — the store rooms' copy (guards 1–5, Zanik's shots, capture/jail, listening, the cell door, Jimmy).
- `dttd_savezanik.rs2` — the body, Juna, the twenty tears, the revival and the meeting flashback.
- `dttd_mill.rs2` — the mill (crate, dwarf, members, cellar copy, ladder/tunnel/drill), Sigmund's escape pages, the finale and completion.
- `dttd_mill_fight.rs2` — Sigmund and the guards (separate boss-fight port).
- `dttd_journal.rs2`, `dttd_debug.rs2` (`::dttdstage n`, `::dttdrun`).

Test: `test/quests/deathtothedorgeshuun.lua` — 48/48 PASS (2026-11-11): start, Zanik,
robes, the sun scene (pages in recorded order), Hans with Zanik, the store rooms' copy,
Juna with the body.

## Stages (`dttd_main` 2258), each on the recorded click

| value | name | recorded |
|---|---|---|
| 1 | find_zanik | [2838] Mistag's "Thank you!" clicked; `dttd_zanik_in_cellar` 1, the start line |
| 2 | get_robes | [2922] Zanik's "Pleased to meet you!" clicked |
| 3 | tour | [3436] "Yes, I have two sets of robes!" clicked; 7 robes taken; follower varps |
| 4 | to_ham | [5469] the mark flashback's return |
| 5 | trapdoor_found | [6243] "Over there..." clicked; `dttd_ham_trapdoor_state` 1 |
| 6 | captured | [7415] waking in the cell; `dttd_zanik_corpse` 1 |
| 7 | body_at_juna | [8618] "It's glowing now. Look." clicked |
| 8 | revived | [8906] "Uuuhhh!" clicked |
| 9 | to_mill | [8942] "Death to the Dorgeshuun!" |
| 10 | mill_fight | [9643] Sigmund's "Guards, kill them!" clicked |
| 11 | sigmund_fled | [10132] |
| 12 | drill_smashed | [10292] |
| 13 | complete | [10410]; `lost_tribe_quest` 11 → 12 (not 13 as the old port had) |

Tour varbits: sun [3613], citizens [3528], priest [3835] (first menu), shop [4028], goblins [4090], duke [5284] (menu). HAM: civilians [5756], deacon [5823], johanhus [6177].

## Rules fitted to the recording

| rule | evidence | verdict |
|---|---|---|
| Zanik moves beside the player when another npc is talked to: a free tile within 2 of the player | 19/19 recorded moves [3513..5269] | built (`~dttd_zanik_beside`) |
| Remark pool: one of five sights at random, "before" line until seen, "after" once seen | 13/13 recorded remarks | built; the six unrecorded lines [wiki] |
| Zanik's second menu row: disguise before the sun, "Have you seen enough" after | [3495] vs [3690] | built |
| Castle-cellar opener "What are we waiting for? Let's go!" | [5033] | built |
| Sun: fires on the first tick the player's pre-move tile is outside the keep | 3205,3214 inside, 3203,3214 fires [3594..3595] | built; other keep edges UNVERIFIED |
| Sun scene shape: blank box; T+1 player faces (-2,-5), Zanik on (0,+1), camera 700/300 jump, silence + `first sunshine`; T+2 camera to 3000 rate 2; guard seq on the 2nd page; varbit with the last page; `cam_reset` on its click | [3595..3615] | built; test shows the six pages in order |
| Leash: outside Lumbridge, her page and "Okay./I'm going anyway." | [4437], [4452], [4579], [4882] | built; box 3150..3265 × 3195..3315 and the 15-tick repeat UNVERIFIED |
| Citizens: first one the long piece; then 1 of 6 at random | [3513..3528]; 5 random lines recorded | built; 6th [wiki] |
| Father Aereck: menu = four rows less the last asked; wars → "How did the godwars end?" | [3835]..[3900] (6 menus) | built |
| Preaching trigger: within 6 of the lectern | one tile 3162,3634 fires; 3158,9640 doesn't | built, fitted to one tile |
| Rubble trigger: all three HAM talks done, within 3 of the trapdoor | 3169,9624 fires; 3171,9622 doesn't; repeats until finished [6214]/[6233] | built |
| Store rooms: a fresh copy of 2560,5184 per climb | raw coords differ every entry | built |
| Guard 1 warns within 2; walking past him west after the warning → shot | 5/5 attempts | built |
| Guard 2 sees (6,5..7); talked to from the north → shot | [6351]; 4/4 | built |
| Guard 3 patrols 7..15 on row 11; "Now!" works walking east | 5/6 (fail [7092] walking west) | built |
| Guard 4 lure past a waiting Zanik; with her following, caught | [7213..7246]; [6940] | built (follow-for-20-ticks model UNVERIFIED) |
| Guard 5 by the door, Zanik waiting elsewhere | [7351..7359] | built; failure branch UNVERIFIED |
| Caught: overhead, T+1 fall + fade, T+3 crying, T+4 cell, stand-up, Zanik outside, her pages; open-cell seq on the click, walked out through an inviswall + inactive door | [6351..6369] | built |
| Store-Rooms (minigame) open from stage 6, not only after the quest | pickpocketing/chest/cracks at stage 6 [7522..7546] | fixed in `minigame_ham_storerooms` |
| Store-room guard patrols: full corridors, a tile a tick, ~5-tick rests | all five guards [7499..7556] | fixed (legs + 17-tick timer) |
| Chest keys: SW room (the cracked one, chest 2570,5192) = bronze | [7546] + wiki table | fixed (old table had it silver) |
| Twenty tears: collecting per tick on a blue (+1) / green (−1) stream; 138-tick clock; "You have collected twenty tears." | [8809..8874] | built on the minigame's stream roll |
| Ham robe pickpocket boost during the quest | Mod Ash 2024-05-20 via OSRS Wiki "H.A.M. Member" | stages 1–2: every non-clue pocket is one of the 7 pieces (the page's 2018 footnote, ~1/4, not used) |

## Cutscenes

| scene | recorded | ours |
|---|---|---|
| Sun | [3595..3615] | camera/jingle/pages as recorded; no copy (none recorded) |
| Mark flashback (Tears of Guthix cave, copy of 3200,9472 level 2) | [5345..5470] | copy, transmog `dttd_zanik_unmarked` → `_with_tog_bowl` → `_marked_glowing` with their stances, walks, Juna's loc anims, light creatures, timed pages (5384, 5404, 5410, 5415 have no continue), Zanik's theme |
| Revival (cave copy) | [8872..8918], cut short by the recorder at [8915] | copy, walk, Juna, stand-in npc → glowing Zanik with guard seq, stage on the 2nd page; the scene's remaining pages ("Oh!" .. Juna's "Someday you will know...") and "The plot! ...outside the meeting room!" from the wiki transcript (moods/heads unverified); the recorded cut-short line "I've got to tell you what I heard in the meeting!" ([8921]) is not built; light creatures' wander not copied |
| Meeting flashback (store-rooms copy) | [8922..8942] | cast and every camera line; toast seqs; stage with the last page |
| Listening/capture | [7390..7438] | camera pan, bend seqs, extra guard's slash, cell |
| Finale (mines copy 3264,9600) | [10351..10424] | cast, camera, bows, pages, box, completion, out to 3316,9612 |

## Journal

Every recorded opening (stages 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12 — 20 openings) is in
`dttd_journal.rs2` row for row, with the conditional rows (robe counts, tour sights in the
journal's own order, the cellar line, the mill line). Stage 8 and the completed journal were
never opened: UNVERIFIED (the completed text follows Ides of Milk's form).

## Rewards

Scroll [10410]: "1 Quest Point", "Access to Dorgesh-Kaan", "2,000 Thieving XP", "2,000 Ranged
XP"; model `dttd_dead_zanik`. Mistag gains the water-mill row afterwards ([10448]).

## Action inventory verdicts (quest window 2590–10460)

- **This content:** every Mistag/Kazgar/Zanik/HAM/store-room/Juna/mill/drill/tunnel op and every page above.
- **Generic content exercised, unchanged here:** Lumbridge teleport tablets, varrock tablet, glory, spiral stairs, castle doors, kitchen trapdoor and cellar ladder, Lost Tribe cellar hole/hole_2, swamp-cave stepping stone, Tears of Guthix tunnel/rocks, the HAM hideout trapdoor and ladder, banks, GE, stamina/prayer potions. Not re-audited in this pass.
- **Other npcs' own dialogue seen but out of scope:** Doomsayer, Count Check, Perdu, Gillie/Seth Groats, farming gardener, Adventurer Jon, Barfy Bill, Nardok, the cave goblins, Juna's Tears of Guthix story/start, the Freaky Forester, Cook's post-quest greeting and menu (ours differs), the Lumbridge Guide's own rows (he has no talk script).
- **Recorder's kit:** combat in the mill (weapon swaps, fire strike), alching, drops.

## Open

1. Sigmund's fight (`dttd_mill_fight.rs2`, `configs/dttd_mill.npc`, `configs/dttd_mill_fight.constant`;
   boss-fight audit 2026-11-11, smoke-tested on a private build, not yet in the quest test):
   recorded forms 990..994 and their prayer switches on the player's attack style (11/11 hits on a
   praying form 0), "foul goblin!" form 994 once the guards are down, the ring-of-life vanish; guards
   (max 3, 0×37 on Zanik), Zanik's bolts (every 4 ticks, land 2 ticks later as 0, 77/77), her
   retargeting K+6. The three engine gaps it found are fixed (2026-11-11, checked on a private build
   with a scratch scriptrun of the stage-9 confrontation and the stage-10 climb-in):
   - **Single-way copies.** A copied tile asks its SOURCE square: `maps/instance_singleway.csv`
     lists the source squares whose copies are single-way (`31_79`, the mill cellar: index 4523
     [9561..10142] sends multiway varbit 4605 = 0), and every other copy stays multi-way
     (`ToriRSServer_TileMultiway`; the indicator varbit and `map_multiway` read it too). Now one
     guard swings at the player at a time; the next takes over when that one dies (recorded [9684]
     death, [9686] next swing) or 8 ticks after its last swing (Sigmund [9702] = [9694] + 8);
     attacking Sigmund mid-fight answers "I'm already under attack.".
   - **Splats on Zanik.** A player bound to an npc's ai script (borrowed, or an owned npc's owner)
     is context, not the hit's dealer (`SCRIPT_PTR_CONTEXT_PLAYER`); only an npc queue a player's
     own script armed keeps that player as dealer (`hero_deals`). The guards' hits on her go out as
     13/17, as recorded; the player's own hits on the guards still credit the player.
     `.npc_damage`/`.npc_hitmark` now act on the secondary npc.
   - **Swing on the step tick.** An npc that steps into reach swings in the same turn (player and
     npc targets), as recorded at [9686] (step south + `human_sword_slash`); ours is now the same
     (guard at t50: step and swing).
   Engine selftest: the same failures as HEAD's engine (93 vs 94; the one gone is the silent-death
   check), none new.
   - **One facing per turn.** A guard waiting its turn re-took the player in its timer and was
     refused at the swing, so its face was set and reset every tick and a reset went out each time.
     Facing now goes out once per npc phase and only when it changed (the reference's
     `setFaceEntity` at the end of `Npc.turn`): one reset on the first refusal, as at [9647], then
     nothing while it stays refused ([9648..9650]). In the scratch run the waiting guard's 103 per-tick
     resets became 1, with the fight otherwise unchanged.
   Not a defect: at a stage-10 climb-in the guard Zanik shoots turns to her and stays where it is. The
   recording does the same (51499 faces her at [10053], 5 tiles off, and never steps or swings again
   before it dies at [10067]).
   `varp797_dttd_temp` (the store-room guard varbits and `dttd_mill_guards_dead`) is now transmitted
   (generated block edited: `transmit=yes`), as the recording sends them.
2. No wire audit (tick-log/`TRACE_OUT` diff) of the cutscenes' camera/seq timings yet; the shapes are copied from the transcript but unmeasured on our wire.
3. Npc-info block inventory (`npc_block_inventory.py`) not yet run against our tick log for Zanik's forms, the store-room guards and the cutscene casts.
4. Zanik's per-tick follower stance/facing during the tour unaudited; the "telejump beside" uses `npc_tele` (≤2 glides).
5. Unrecorded branches taken from the wiki are marked `[wiki]` in the code (moods unverified).
6. The Lumbridge Guide, Millie Miller and the candle seller have only Zanik's part (no own talk scripts in this tree); Feoras' shop has no stock file (cache inv `lumbridge_blacksmith` exists).
7. (fixed) Alchemy on Zanik's body refused in `skill_magic/scripts/spells/alchemy.rs2` ([8168]).
8. Body lost at the bank ([8340] incinerated → [8371] back by the hideout): ours restores it at login and on the hideout ladder, not on the incineration itself.
9. Store-room guard 4 and 5 failure branches, and "Make me!"/"What's down here?" answers, unrecorded.
