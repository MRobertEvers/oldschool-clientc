# Fight Arena — rebuilt from RSProx index 4395 and 4396

Recordings: index **4395** ("Fight Arena Part 1", rev 241.1, 2026-10-27: start to the
Bouncer fight, logged out in it) and **4396** ("Part 2 - Killing Khazard": Bouncer, the
Servils freed, the General fought and beaten, his return, Lady Servil). Together they
cover every stage write: 0→1 [125], 2 [188], 3 [254], 5 [499], 6 [617], 8 [675],
9 [700], 10 [810]; 4396: 10→11 [175], 12 [201], 13 [477], 15 [597]. Stage 14 (the
General left alive) is in no recording; it is built from the OSRS Wiki's
Transcript:Fight_Arena (retrieved 2026-10-27).

The old content was a LostCity (2004) port (§16): every page, the keys flow, the
"drunk guard", the cell escort, and fights in the open world with owner-private
actors. None of the 2004 text survived: the recordings have 177 pages and none
matched it. The cache carried the rework unused (`content_asset_inventory.py`): the
Head Guard, the Door Guards, `arena_guard5..7`, every `*_cutscene` actor, the
`*_cage` pens, `fightarena_door2_escape`, `arena_prisondoor_noop`, and varbits
14717..14720 on `varp3741_arenaquest_secondary`.

Files: `quests/quest_arena/scripts/arena_cutscenes.rs2` (the frame, the copies, every
scene and round), `lady_servil.rs2`, `khazard_guard.rs2` (the guards, the Head Guard, the
Door Guards), `sammy_servil.rs2`, `hengrad.rs2`, `fightslave.rs2`, `arena_spectator.rs2`,
`khazard_barman.rs2`, `arena_locs.rs2`, `arena_journal.rs2`; configs `.constant`,
`.npc`, `.spawn`. Tests: `test/quests/arena.lua` (the recorded route, the General
beaten), `test/quests/arena_escape.lua` (the other ending).

## The structure, as recorded

| section | recording | ours |
|---|---|---|
| start | Lady Servil, combat-level warning mesbox below 50, menu "Start the Fight Arena quest?" [111..127] | `[label,lady_servil_start]` |
| armour | the shut chest, L+2 `human_openchest` delay 10 + `chest_open`, L+3 the stage, chest swung open 2 ticks, both pieces, `chest_close` d45 + `pick2`, objectbox_double [182..190] | `[oploc1,arena_guard_chest_shut]` |
| compound doors | walk-through: inviswall + `inactivepoordoor` on the outside tile 2 ticks, `door_open`, one step across; in only with helmet+platebody worn, out always (4396 [549]: helmet only) [219], [397], [471] | `[oploc1,fightarena_door1]` via `~tut_door_swing` / `~tut_walk_through` |
| Sammy | "Hello. Are you Sammy?" … met varbit 14717 with "The Head Guard has them" [222..226] | `[apnpc1,sammy_servil]` |
| Head Guard | "Long live General Khazard!" set, stage 3 with "Oh, nothing beats them!" [249..255]; the drink set, stage 5 + keys in the brew's slot with the keys' box [487..500] | `[opnpc1,arena_guard2]` |
| bar | 4-option menu; "Got any news?" then "Now, can I get you something?" without it; beer with no room; Khali brew with the objbox [416..450] | `khazard_barman.rs2` |
| prison gates | `locked` synth + "The gate is securely locked." [376], [514], [521]; keys on other gates "Nothing interesting happens." [524], [532] | `[oploc1,arena_prisondoor]` |
| Sammy's release | the keys on his gate [576]: 3 pages, fade, copy, the gate, Sammy out, "Sammy, wait!", the arena, "Quick, help him!", the ogre's pen, stage 6 + keys taken [617], into the round's copy [619..621] | `[label,arena_release_sammy]` |
| ogre round | fresh copy, fighter on the player | `~arena_round_enter` (see "The ogre round") |
| the General | ogre del → fade, copy, 11 pages, stage 9, Hengrad's REAL cell [702] | `[label,arena_general_scene]` |
| Hengrad | he speaks first, 8 pages [706..720] | `[label,arena_hengrad_scene]` |
| scorpion's pen | copy, guard marches the player in, 2 pages, the pen, varbit 14718 [725..747] | `[label,arena_scorpion_scene]` |
| Bouncer's pen | 1 page, the pen, varbit 14719 [810..828] | `[label,arena_bouncer_scene]` |
| logout in a round | back in the yard 2606,3151 at the same stage (4396 [0]) | `~arena_on_logout` (player/logout.rs2) |
| return | door2 from the yard: "Return to the arena?" Yes → fade, T+2 the copy, T+4 lifted (4396 [8..16]) | `[oploc1,fightarena_door2]` → `~arena_round_enter(false)` |
| Servils freed | Bouncer del → 6 pages, the Servils led out, stage 12 [201], the General's fight copy (4396 [175..205]) | `[label,arena_freed_scene]` |
| the General's return | his death is a Mahjarrat teleport (4396 [474]); stage 13 + scene, 6 pages, he teleports away, varbit 14720, out to the yard 2607,3151 [477..525] | `[label,arena_return_scene]` |
| end | Lady Servil, 9 pages; stage 15, 1,000 coins, scroll "2 Quest Points / 12,175 Attack XP / 2,175 Thieving XP / 1,000 Coins" with `khazard_platemail` [579..602] | `[label,lady_servil_defeated_khazard]` |

## Wire audit: the scenes (scriptrun tick log vs the recording)

Aligned on each scene's lock (T+2) and, past a page, on the click that releases it
(a dialogue scene is paced by its reader). Our run: `test/quests/arena.lua` on
scriptrun, 92/95 rows (the two camera rows need a live run; `quest.journal` never
passes on scriptrun).

| scene | event | recorded | ours | verdict |
|---|---|---|---|---|
| Sammy | the cast (bouncer, ogre, Sammy, scorpion cutscene forms) + 9 op-less locs | T+2 [581] | T+2 | matched |
| Sammy | `human_opendoor_weapon`, gate leaf + inviswall, `iron_door_open` | [587] | +6 | matched |
| Sammy | gate shut, `iron_door_close` | [590] | +9 | matched |
| Sammy | Justin's cutscene form added, camera on the pen | the click [605] | the click | matched |
| Sammy | the ogre: tele 2, gate open / tele 2 / gate shut / tele 2 | click+1/+3/+4/+5 | same | matched |
| Sammy | ogre swings at Justin, `giant_attack`; `human_hit` | click+8, +9 | same | matched |
| Sammy | E: stage 6, keys taken | click+12 [617] | same | matched |
| Sammy | `fightarena_door2_escape`; the round's cast | E+2, E+3 | same | matched |
| General | the lock and seven actors on the recorded tiles | ogre death seq +4 [677] | same | matched |
| General | "Guards, take him to the cells." | [698] | `text_gender("him","her")` | matched (gendered: the wiki's [him/her/them]) |
| General | stage 9, Hengrad's real cell 2598,3143 | [700], [702] | same shape | matched |
| scorpion | guard5 cutscene glides | T+2, T+4 [729], [731] | same | matched |
| scorpion | pen: open/tele, tele, shut, tele + varbit 14718 + E | click+1..+5 [739..743] | same | matched |
| scorpion | escape door, the round's cast | E+2, E+3 [745], [746] | same | matched |

Every page's text and mood is the recording's (`rsprox_pages.py`); every recorded
page appears in our run in order (the scene rows of the test check each one).

## Spawns and movement

The compound's npcs are the copy the scorpion round is fought in (4395 [746]): a
fresh copy puts every npc on its spawn tile. `quest_arena.spawn` carries them; the
square's generated list (`areas/world/configs/m40_49.spawn`, a pre-rework dump: an
`arena_guard1` on the west door where rev 241 has the Door Guard, no Head Guard, no
watchers, no cages, no off-duty guard) lost its 34 arena rows, and
`tools/gen_spawns.py` `NPC_SPAWN_EXCLUSIONS` keeps them out. Our copies do not copy
spawns, so each round adds the crowd itself (`~arena_copy_crowd`), the watchers the
stage shows (`~arena_copy_watchers`: Sammy alone in the ogre round, all four in the
scorpion and Bouncer rounds, none in the General's — 4395 [620], [746], [827],
4396 [204]) and the unfought pens' cutscene forms.

Movement (`npc_movement_audit.py defaults`, then each type's recorded steps): every
watcher, cage, cutscene actor and fighter is stationary (no step in any life, or set
on the player at once); Justin wanders (16 steps; was pinned still); Sammy wanders
one tile (13 steps, all within one tile of 2616,3168: `wanderrange=1`; at the default
5 he reached 2616,3166, where the gate cannot see him and the talk failed).

## Variables

`varp3741_arenaquest_secondary` (varbits 14717 met Sammy, 14718..14720 the scorpion,
Bouncer and General scenes) was generated `scope=temp, transmit=no`; 4396 [0] re-sends
all three at login: now `perm`, `transmit=yes`. `varp_transmit_audit.py` on both
transcripts lists no other arena variable.

## Not from a recording (labelled UNVERIFIED in the scripts)

- The journal: the OSRS Wiki has none for this quest (no transcript, no screenshot)
  and neither recording opens it. Rewritten to the rework's story with both endings;
  the 2009 text it replaces told the pre-rework quest.
- From the wiki's transcript, moods unverified: Lady Servil after the armour, after
  the arena, after Sammy, the escape ending ("He tried, but I escaped."); the off-duty
  guard before the theft; the disguised guards' four lines; the drunk Head Guard's
  three branches; Sammy's second talk; Hengrad before Sammy and after an escape; the
  slaves' and Joe/Kelvin's other lines; the spectator; the door's "Return to the
  arena?" outside the General's stage; the escape door's "Are you sure you want to
  leave?".
- No source at all: Lady Servil after the quest (a LostCity line kept so Talk-to
  answers); the beer bought ("The barman gives you a beer in exchange for two
  coins."); the Door Guard undisguised; where the escape door lands (the yard, as every
  other way out does); the Door Guard sending the player back into the round at
  stages 6/10/11/12 (the wiki says so only for the jail).

## Open

- Cameras: checked on the copy's ops/heights in the test, but scriptrun has no camera
  state; the two `cutscene` rows need a live run.
- Music: the copies play whatever our music areas give a copy's coordinates; the
  recording plays fanfare3 in the compound and attack4 in the arena. Not compared.
- The fights: see the per-round sections below (one audit per encounter).
