# Plague City (quest_elena) vs RSProx index 4360

Recording: RSProx index 4360, "Plague City", rev 241.1, 2026-10-24. One recorder, the
whole quest from Edmond's first talk ([514]) to the scroll read ([1181]) and a talk after it
([1185]). Bracketed ticks below are the recording's; "ours" ticks are scriptrun's
(`test/quests/elena.lua`, private pack, `TORIRSSERVER_TRACE_OUT`, tick log).

## Why the LostCity port missed it

The quest was ported line for line from LostCity (`LostCity_Content2/scripts/quests/quest_elena`),
which is the 2004 quest on a rev-254 cache. Three things in this revision's cache do not exist
there at all, so a port of the scripts could not contain them:

- **The dug hole.** `plaguemudpatch2` (2532, the only patch the map places) is a multiloc on
  `%varb1785_plaguecity_dug_mud_pile`: 0 `plague_mud`, 1 `plague_hole` ("Dug hole", op1
  Climb-down). LostCity has no multiloc and no varbit, so nothing wrote 1785 and the hole never
  appeared; the way back down was the spade again.
- **The grill.** `plague_grill` (11422, wall decor over the pipe at 2514,9739, op1 Open) on
  `%varb1787_plaguecity_pipe` (0/1 drawn, 2 gone), plus three floor-rope multilocs on the same
  bit (drawn at 1), and `%varb1786_plaguecity_checked_grill`. LostCity bound the rope to the pipe
  (2541, placed nowhere in this cache), so the port had no grill to open and no rope to show.
- **The pull scene.** Edmond's "gets a good grip on the rope... you both fly backwards" was a
  2004 mesbox. OldSchool plays it as a scene in a copy of the sewer square with three animated
  rope locs (`hanging_rope_anim`, `straight_rope_anim`, `straight_rope_end_anim`), a camera, an
  overhead and timed pages.

And the port never checked the cache's multiloc wiring (`multivarbit=` in `configs/all.loc`)
against the scripts, or any recording; there was none of this quest until 4360. Every
dialogue page was LostCity's 2004 text too: 122 of 122 recorded pages differed.

Also wrong from the port: Edmond's shells (`%varb1783_plaguecity_can_see_edmond_up_top`)
were put back up top on the mud-pile climb and on leaving the sewer. The recording keeps him
in the sewer from the dig ([693]) to Elena's release ([1126]); the garden had no Edmond at
stage 8 ([750]).

## What changed

| area | file |
|---|---|
| dig, pour, the hole's Climb-down (bound on the child `plague_hole`: the generated ladders.loc gives it `category=climb_down`, whose generic rung answered "You can't go any further.") | quest_elena/scripts/mud_patch.rs2 |
| mud pile (ladder shape; no Edmond reset) | mud_pile.rs2 |
| grill Open, rope on grill, pipe Climb-up, the squeeze and the fade into West Ardougne | sewerpipe.rs2 |
| the pull scene in a copy, logout backstop | grill_scene.rs2, player/logout.rs2 |
| every page's text and mood (recording, else the wiki transcript, marked "wiki") | edmond, alrena, jethick, rehnisons, bravek, plaguehouse, elenap, doors, cupboard; area_ardougne_west clerk |
| walk-through doors as recorded (inviswall + leaf for 2 ticks + door sound) | doors.rs2 `~elena_door_cross`; plague house, cell, Rehnisons, Bravek's door |
| Elena leaves under a fade | elenap.rs2 |
| manhole down as recorded (fade, move on O+3) | area_ardougne_west manhole.rs2 |
| completion: scroll on page 1's click, page 2 on its close; rewards text and gas mask model | edmond.rs2 |
| scroll read: one objbox, wom_bless | elena_teleport_scroll.rs2 |
| cure mixing: objboxes | skill_cooking/scripts/cakes.rs2 |
| post-quest Edmond for a player SotE does not admit yet | quest_songoftheelves/scripts/songoftheelves.rs2 |
| saves from before the bits existed | quest_elena.rs2 `~quest_elena_login`, player/login.rs2 |

## Wire audit (ours vs R, offsets from the op tick O / scene click T)

| section | R | ours | verdict |
|---|---|---|---|
| pour water | L+1: stage, line, farming_pour_water, bucket swapped, farming_watering | same | matched |
| dig, hard | O: human_dig, digspade, objbox spade | same | matched |
| dig, soft | O: dig, digspade x2, fade out, box (no click); O+3 stage 8, 1785, 1783, move 2518,9760; O+4 fade in, box; overlay closed on the click ([690..697]) | O 216, O+3 219, O+4 220, overlay 224 on the click | matched |
| mud pile | L+1 face, L+2 reachforladder, L+3 spam + move 2566,3331 | ladder shape | matched |
| hole Climb-down | O face + pickupfloor, O+1 spam + move 2518,9759 ([752..753]) | 233 / 234 | matched |
| grill Open | O face, O+1 1786, pull_on_pipe, irondoor_locked, box ([716..717]) | 249 / 250 | matched |
| rope on grill | O face; O+1 step to 2514,9740; O+2 stage 9, rope_tie, rope gone, plague_attach; O+3 1787=1, objbox ([721..724]) | 274 / 275 / 276 / 277 | matched |
| pipe, grill on | "There is a grill blocking your way." on L+1 / O | p_arrivedelay | matched |
| pipe, no mask | Edmond's page L+1 ([798]) | same | matched |
| pull scene | T fade + empty box; T+3 copy, Edmond (4256) added, ropes in; T+4 camera 375/125 rate 90, fade in; T+7 say, both lift_and_pull, rope anims, plague_pull_grill; T+13 Edmond's timed page, seqs cleared; T+24 player's timed page; T+31 fade; T+33 1787=2, stage 10, back to 2514,9740, cam_reset; T+34 fade in; T+36 overlay | T 290, +3 293, +7 297, +13 303, +33 323 | matched, **except the copy's grill and floor rope are not deleted (open, below)** |
| pipe climb | O: doublepipesqueeze delay 30, exactmove 30/126 south, squeeze_in; O+1 fade, box; O+6 out at 2529,3304, manhole opened, manhole_open, fade in, box ([810..819]) | 340 / 346 | matched |
| Rehnisons' door | Ted L+1; step onto the door tile on the last click; crossing + objbox; door back +2 | same (391 / 393) | matched |
| Milli | gnome_cry with her page ([885]) | 426 | matched |
| Bravek's cure | say "Grruurgh!", bravek_hangover_cure_anim for 3 ticks ([1022..1025]) | fitted: our npc_changetype reverts one tick before its count, so 4 | matched |
| Alrena's mask | mix [543], mask [544], herself [548] | fitted the same way (5) | matched |
| warrant | no box; mourner L+1; step onto the door tile on the click; warrant page; "highly irregular"; chat closed; +2 the two guards face each other and say; +2 the second line, the crossing and the box ([1071..1083]) | 690 / 692 | matched |
| cell door | locked: box + locked synth, Elena; key: crossing + box on O; out: crossing ([1091], [1116], [1128]) | 707 / 731 / 748 | matched |
| Elena leaves | last click T: fade, chat closed; T+3 stage 28, 1784=1, 1783=0, fade in; T+5 overlay ([1123..1128]) | 740 / 743 | matched |
| manhole down | O: box, pickupfloor, fade; O+3 move 2514,9739 facing north; O+4 fade in, box ([1155..1161]) | 842 / 845 | matched |
| pages | 122 pages | every page the test reaches matches text and mood; the 18 it does not reach were checked against the scripts | matched |

## Open

- **Copy loc deletes (engine).** On T+3 Jagex deletes the copy's grill (wall decor, 2514,9739)
  and its three floor-rope locs before adding the animated ropes. Ours sends no LOC_DEL: the
  server's window over a fresh copy is built after the scripts of the tick after the move, and
  `loc_find` searches windows (and the zone records of runtime changes), so it cannot see the
  copy's map locs on the tick the player arrives, or the next. The deletes are written on the
  recorded tick and will go out once the engine can answer for them. Until then the old grill
  and floor rope are drawn under the scene's rope.
- **scriptrun only:** `quest.journal` (no clientscripts build the quest list), and the Ardougne
  Teleport casts after the quest scroll has been shown (the spell's node reads display-hidden
  under scriptrun after the scroll closes; a cheat-completed character casts it, and the
  published live run cast it). The clerk talk is shifted a tick in the test because scriptrun's
  options reader keeps only the last clientscript and the stat-boost timer scripts landed on the
  menu's tick.
- **Unverified (no recording):** the branches marked "wiki" (moods guessed), Jethick's shorter
  greeting for a second talk (no var on the wire records the first; ours always plays the
  intro), a spade on the dug hole, the pipe climbed from a walk, Biohazard filling the hole.
