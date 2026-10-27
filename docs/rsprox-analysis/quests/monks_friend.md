# Monk's Friend vs RSProx index 4393

Recording: index 4393 (rev 241.1, world 631, 2026-10-27, "Monk's Friend"), the
whole quest from first talk [322] to the scroll [643]. One playthrough, so every
random (balloons) is one roll. Ours: `test/quests/drunkmonk.lua` on scriptrun,
tick log with `leg.*` marks.

Content: `quests/quest_drunkmonk/` (ladder, party, completion),
`areas/ardougne_east/scripts/brother_omad.rs2`, `brother_cedric.rs2`.
First ported from LostCity (2004), so per the rsprox-content skill §16 every
page was rebuilt from the recording; the branches the recording never took come
from the OSRS wiki's `Transcript:Monk's_Friend` and are marked `[wiki]` in the
scripts (moods unverified).

## What was wrong (LostCity) and what the recording says

| item | LostCity port | recording | cite |
|---|---|---|---|
| quest-start menu | `p_choice2("Can I help at all?", "I'm sorry to hear that!...")` | `Start the Monk's Friend quest?` Yes./No., then the player says the line | [342], [355] |
| "You've started a new quest" | missing | with the stage write, on the "Where are they?" click | [359] |
| cave ladder loc | `wildymirrorladdertop1` (18987) | `ladder_outside_to_underground` (16680), rot 1 | [392] |
| ladder watcher | timer armed once on the start page; lost on relog | (fires from any later visit) | [392] |
| ladder-down seq | `human_pickupfloor` (827) | `human_reachforladder` (828) | [397] |
| Omad's last blanket page | "I'm off to bed! ..." | "*yawn*..I'm off to bed! Farewell brave traveller!" | [465] |
| blanket taken / stage 20 | after the last page | on the click before it | [465] |
| Omad at stage 40 | player opens "Hello brother Omad." | Omad speaks first ([wiki]) | wiki |
| Cedric stage 40 | after the last page | on the click before it | [531] |
| Cedric water | say before objbox, "Gulp...gulp!", neutral "Aah!" | objbox; on its click jug gone + stage + "Gulp... gulp!" + `liquid` 2401; "Aah! That's better!" happy | [552]..[553] |
| Cedric wood box | mesbox | objbox (logs), wood gone with stage on the "Here you go!" click | [576]..[578] |
| Cedric after the quest | "Brother Omad sends you his thanks!" (in neither source) | deleted; falls to his drunk default (unverified) | - |
| party seq | `emote_dance` (866) | `human_dancing` (818), Omad and player | [613].. |
| party Omad facing | `npc_setmode(playerface)` (entity) | `[facing] coord=` player's tile on K, reset on K+23 | [613], [636] |
| party monks | npc_find a monk within 5, "GO!/Dance!/You go!" | none: a monk stood 4 tiles away [628]..[643], silent | [628]..[643] |
| party timing | Party, drops, Woop, then rounds of +3/+3/+3 | K, drops K+1..K+5, Woop K+3, lines K+6/9/12/15/21/24, complete K+30 | [613]..[643] |
| balloon types | 7 incl. yellow, no balloon3 | red/blue/green/purple/white/balloon2/balloon3; yellow 0 of 18 | [614]..[618] |
| balloon tiles | (2603..2610,3207..3211) 3:1 (2605..2607,3212..3218) | (2602..2609,3207..3211) 9 : (2605..2607,3213..3217) 9; P(3:1) ~ 1.4% | [614]..[618] |
| balloon on a balloon | allowed (map_blocked only) | never (0 of 18; 2 of 20 tries skipped) | [618] |
| completion | queue; scroll "2000 Woodcutting XP / 8 Law runes / parties", lawrune icon | inline at K+30; "2,000 Woodcutting XP", "8 Law Runes", child's blanket icon | [643] |
| jingle | difficulty default (154) | 152 `quest_complete_1` | [643] |
| table pickup (shared) | `human_pickuptable` 832, no sound | `human_pickuptable_walkmerge` 9401 + `pick2` 2582 (also index 4352 [753], [760]) | [417] |

## Wire audit (ours = scriptrun tick log, 2026-10-27)

| section | recorded | ours | verdict |
|---|---|---|---|
| start | stage 10 + message + last page on the "Where are they?" click [359] | same click (t27) | matched |
| ladder | message + loc on the tick after the player's tile was within 2 [392] | t32 (bot teleported beside it at t30; the zone fires t31, the timer t32) | matched for a walker; a teleport-in costs a tick |
| ladder down | face L+1, 828 L+2, teleport L+3 [396]..[398] | bot already adjacent: 828 on C, teleport C+1 | matched (no-walk shape) |
| blanket | face + seq -1, then 9401 + obj_del + 2582 [416]..[417] | -1 then 9401 + pick2 (after the pickup.rs2 fix) | fixed |
| ladder up | 828 then teleport to 2561,3221 [430]..[431] | t64..t65 | matched |
| blanket hand-in | stage 20 on the click after "Really..." [465] | t75 | matched |
| Cedric 30/40/50/60/70 | [501], [531], [553], [565], [578] | t94, t102, t110, t115, t122, each on the same click | matched |
| water | Gulp + 2401 + stage on the objbox click [553] | t110 | matched |
| party | K=613: face coord, 818, "Party!"; balloons K+1..K+5; ... complete K+30 | K=133: all on the same offsets, reset K+23 | matched |
| completion | music 147, jingle 152 (6,325 ms), stage, qp, runes, xp [643] | t163: 147, 152 (6,325) | fixed (was 154) |

## Open

- The obj respawn: the blanket came back 8 ticks after the take ([417] -> [425]).
  Ours uses `^lootdrop_duration` for every map spawn (no per-obj `respawnrate`
  decoder, torirs_server_world.c `ToriRSServer_WorldGroundTake`). Engine-wide gap.
- The balloon duration (still up at logout, 48+ ticks) and the ladder's (39+)
  are LostCity's 200, unverified.
- Omad's extra `[facing] coord=` on K+1 [614] is not sent by ours.
- The wiki-only pages' moods.
- Under scriptrun, `quest.journal` fails (no clientscripts) and the Camelot cast
  row in the setup leg fails before any quest content; neither is this content.
