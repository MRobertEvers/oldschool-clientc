# Biohazard vs RSProx index 4372

Recording: RSProx index 4372 (owner's upload, rev 241.1, world 631, 2026-10-24). One
recorder, the whole quest: Elena's first talk [186] (a "No", then the start [220..229]),
through King Lathas's scroll [1413], and on into the Underground Pass start [1420..1437].
Bracketed ticks are the recording's; "ours" ticks are scriptrun's
(`test/quests/biohazard.lua`, `TORIRS_SCRIPTRUN_WIRELOG`, tick log).

Tools: `build/biohazard_private/condense.py` (a per-tick view of the transcript),
`pagediff.py` (every page's chathead seq and wrapped text against ours), `msgdiff.py` (every
message in order), `sections.py` (`tools/rsprox/wirelog_tells.py` per section, anchored on a
message both sides send), `tools/rsprox/npc_block_inventory.py`,
`tools/rsprox/npc_movement_audit.py`. Unrecorded branches are the OSRS wiki's
`Transcript:Biohazard` and the npcs' own transcripts (fetched 2026-10-24), marked `// wiki`
in the scripts; their moods are unverified.

## Why the port missed it

Every Biohazard script was ported from LostCity's 2004 quest. OSRS rewrote the quest's
dialogue since; none of the recording's 203 pages matched the port. Beyond the text:

- **The pigeons fly.** `biopigeon_launch` (spotanim 72) was unused: three map projectiles
  from the player at the tower ([396]).
- **The rope ladder** was mesboxes and an instant move. Jagex lays `bioladder` over both
  halves of the wall for five ticks, faces the player at it, plays
  `human_reachforladder`, then moves them ([426..432], [693..699]).
- **The fence squeeze** walked with no line; Jagex sends the line, a face, `regicide_tightfit`
  and a 76-cycle exact move, the player held three ticks ([478..483]).
- **The Mourner Headquarters' guard** was LostCity's ("In you go doc."); OSRS has the mourner
  inside answer through the door, with `mournerstew1`'s head.
- **The sick mourner** was silent (null sounds), defended with the wrong seq, and dropped the
  key as a game message on a search; Jagex puts it in the pack with a box two ticks after the
  death seq ([616..618]).
- **Asyff** gave the gown AND robe; he gives the gown (`varb9106` set) and nothing else
  ([1183]). His menu is five rows ([1137]).
- **The Chemist** opens a three-topic menu (Lamps / Your quest / Impling jars) before any
  quest talk ([880]); the port went straight to the quest.
- **King Lathas** completed on a queue after a LostCity monologue; OSRS completes on the click
  of his last page and, after the scroll, offers the Underground Pass ([1413..1437]).

## What changed

| area | file |
|---|---|
| shared helpers: the held (no-continue) box, the two-item box, the wall crossing | quest_biohazard/scripts/quest_biohazard.rs2 |
| Jerico, every stage | quest_biohazard/scripts/jerico.rs2 |
| cupboards, bird feed and the watchtower, pigeons, Omart and Kilron, the cauldron, Nurse Sarah, the HQ gate, the crate, keeping the gown and mask on | quest_biohazard/scripts/quest_biohazard_locs.rs2, player/scripts/equip.rs2 (`~biohazard_keep_worn`) |
| the yard fence | general_use/scripts/fence.rs2 |
| the HQ front door and the upstairs door | area_ardougne_west/scripts/doors.rs2 |
| the HQ mourners, the sick mourner's talk and key, the door mourner's line | area_ardougne_west/scripts/mourner.rs2; configs/mourner.npc (sounds, defend seq, the door mourner standing still) |
| Elena, every stage | area_ardougne_east/scripts/elena.rs2 |
| the Chemist (lamps, quest, impling jars) | quest_biohazard/scripts/chemist.rs2 |
| the six errand boys | quest_biohazard/scripts/errand_boys.rs2 |
| the Varrock gate's search | areas/varrock/scripts/east_gate.rs2 |
| Asyff's menu and the gown | quest_eaglepeak/scripts/asyff.rs2 |
| Julie at the door, Julie, Guidor | quest_biohazard/scripts/guidors_wife.rs2, guidor.rs2 |
| King Lathas, the scroll, the Underground Pass offer | area_ardougne_east/scripts/king_lathas.rs2; quests/scripts/questpoints.rs2 (quest_complete_1) |
| **every messagebox wraps at 470, not 479** | interface_chat/scripts/chat.rs2 `~mesbox`, `^mesbox_wrap_width`, and the six quests' own held boxes |

The messagebox width: across 2,156 messagebox pages in 393 transcripts, no row is wider than
470 px (q8_full) and the soft wraps fall at 471..473. At 479, "...The mourners frantically
try" [396] and "...dissolves into the stew." [496] stayed on one row.

## Wire audit (ours vs R)

Stage writes: 0 -> 1 -> 2 -> 3 -> 4 -> 5 -> 6 -> 7 -> 10 -> 12 -> 14 -> 15 -> 16, each on the
recorded page's click (12/12). Pages: **201 of 203 identical** (text, `<br>` wrap,
chathead, seq by rows); the two not reached are Asyff's full-pack box [1144] and the
Underground Pass warning's combat sentence [1430] (the recorder was combat 28; the test
character is over 60 and gets the skills-only line). Messages in order: 216 of 228 paired;
the rest are branches the test does not take (the full-pack Asyff visit) or the bot's
own lines (teleport cheats, music unlocks).

| section | R | ours | verdict |
|---|---|---|---|
| Elena start | stage 1, `varb1785`=0, the start line on the click of "Any ideas?" ([228]) | same | matched |
| Jerico | stage 2 on the click of the pigeons page ([294]) | same | matched |
| cupboards | O line, O+1 `human_openchest` + `cupboard_open`, O+2 open loc ([305..307], [538..540]); search: box, then feed + line ([309..310]); gown + box on the op ([541]) | same | matched |
| feed | stage 3, feed gone, box on the op ([388]) | same | matched |
| pigeons | held box, stage 4, 3 projectiles (16/125..130, 5..25 -> 106..126, 20, 10), cage swapped; the box with its continue at +5 ([396..401]) | same, tick for tick | matched |
| Omart's ladder | T held box; T+1 ladders; T+2 face; T+3 held box + reachforladder; T+4 stage 5 + move to 2556,3267; T+6 wall back + box; Kilron's pages on its click ([426..441]) | same | matched |
| fence | O face; O+1 line, face 1536/512, tightfit, exact move 0..76; held to O+4 ([478..484], [499..504]) | same | matched |
| cauldron | stage 6, apple gone, box on the op; line on its click ([496..498]) | same | matched |
| HQ door | without the gown: three pages ([571..573]); with it: three pages, the crossing on the last click (inviswall + leaf for 2, `door_open`) ([577..582]) | same | matched |
| upstairs door | the crossing on the op ([593..595]) | same | matched |
| the sick mourner | defend `human_unarmed_def` + `human_hit`, swing `unarmed_punch`, death `human_death` + seq, `enabled_ops` 0 ([594..616]); key + box at death seq +2 ([618]) | same | matched (hits are the recorder's kit) |
| HQ gate | in: box, one tile through, both leaves + inviswalls, `grate_open`, back at +2; "The gate is locked. You need a key." on the box's click ([623..625]); out the same with no box ([632..634]) | same | matched |
| crate | stage 7, distillator + box on the op ([627]) | same | matched |
| Kilron's ladder | as Omart's, landing 2559,3267 ([693..699]) | same | matched |
| Elena's vials | box; distillator out, four in, stage 10, two-item box on the click of "I'm not sure..." ([759..763]) | same | matched |
| Chemist | menu on the op; lamps; touch paper + stage 12 + box ([880..913]) | same | matched |
| errand boys | each vial gone with its box; each back with its box ([958], [977], [988], [1081], [1113], [1122]) | same | matched |
| Varrock gate | page on the op, held search 4 ticks, page, crossing on its click (inviswalls, leaves, `gate_open`, +2) ([1057..1066]) | same (+4) | matched |
| Asyff | `varb9106`, gown, box ([1183]) | same | matched |
| Julie at the door | four pages, `varb9105` on the second click ([1210..1213]); crossing in priest's clothes ([1218..1220]) | same | matched |
| Guidor | stage 14 and the five items on the click of "So what about the plague?" ([1242]) | same | matched |
| Elena's report | stage 15 on the click of "The king?" ([1336]) | same | matched |
| completion | key gone, stage 16, 3 QP, line, disease -50/0, song 147, `quest_complete_1` (152), scroll (distillator at 71,128; "1,250 Thieving XP / Full West Ardougne Access / Combat Training Camp Access") on the click of the last page; "Do you need anything else" on the scroll's close ([1413..1420]) | same | matched |

npc info: `npc_block_inventory.py` lists only `facing` for the cast and, for the sick
mourner, `enabled_ops` (0b00000 both sides), `sequence`, `headbar`, `hit`. Movement
(`npc_movement_audit.py`, steps per visible tick): the door mourner (`mourner_armed_guard`)
never moves in 5 recorded lives (105 ticks) and wandered in ours; now `defaultmode=none`.
Every other type moves at a comparable rate on both sides (Omart, Kilron, Guidor, the
Chancys and the Varrock guard stand still on both).

Engine-wide, not Biohazard's: continue rows armed 0..28 (`if_addresumebutton`; Jagex -1..-1,
documented in the Guthixian Archive and X Marks the Spot audits); a loc face sent as the
centre tile where Jagex sends the loc tile with `sizex`.

## Open

- **Two npcs never turn to the player** (engine, the npc turn loop): Chancy in Rimmington
  (`gambler1`, recorded [972]) and Guidor ([1223]) enter `playerfaceclose` (mode 6 in the
  tick log) but their face latch is never set; Chancy in Varrock and Kilron, with the same
  config, do turn.
- **The player's face is not reset by a scripted move** (engine): Jagex sends `[facing]
  type=reset` with every crossing's move ([430], [580], [623], [697]) and on the first step
  after the squeeze ([484]); ours sends none.
- **Omart and Kilron go back to their spawn angle** a tick after their face resets (3/3:
  [431..432], [446..447], [698..699]); ours resets a tick later (teleport +2, Jagex +1) and
  sends no angle. The Chancys and Da Vinci only reset (0/3).
- **The face release on the target's death** (engine): Jagex turns the player to an angle on
  the death tick ([616]), ours resets a tick earlier.
- **Underground Pass's start state**: the recording writes `%varp161_upass` = 1 and Koftik's
  five varbits (9139..9143) on the Yes ([1435]); this tree's Underground Pass keeps its start
  in `%varb9125_upass_lathas_met` until Koftik is met, so that is what is written. Moving it
  to the recorded vars is Underground Pass work.
- **Unrecorded, from the wiki only** (moods unverified): every branch the recorder did not
  take; Investigate on the watchtower without feed does nothing (no source has it); the HQ
  "back door" and trapdoor lines are not wired (no loc in this cache is named for them).
