# The Restless Ghost vs RSProx index 4341

Source: RSProx index 4341 (rev 241, w337, 2026-10-23, "Restless Ghost"), the only
recording of the quest (searched by npc 922 ghostx, 923 father_urhney, 2812
father_aereck; no local `.bin` newer). Transcripts: default, `--bas`, `--raw`.
The session opens with the end of Rune Mysteries ([3..132]); the quest runs
[147..547]. Our side: `quests/quest_priest/` and `areas/lumbridge/scripts/
father_{aereck,urhney}.rs2` in OSRS-Content, test `test/quests/priest.lua`.

The quest was a LostCity (2004) port (§16): every page, branch and shape below
was unverified until this audit. The wiki's `Transcript:The Restless Ghost`,
`Transcript:Father Aereck` and `Transcript:Father Urhney` fill the branches the
recording does not take; their moods are unrecorded (kept from the port).

## 1. Action inventory (`rsprox_actions.py`)

| tick | action | verdict |
|---|---|---|
| [3] | welcome_screen:play | engine |
| [27] | drink 2dosestamina | generic: bound (`inferno_potions.rs2` `[opheld1,2dosestamina]`) |
| [36], [71], [118], [132] | Aubury, research notes on research notes, head wizard, quest scroll close | Rune Mysteries, not this quest |
| [97] x3 | fai_wiztower_poor_door | generic door: `doors.loc` pairs `fai_wiztower_poor_door_open` |
| [106] x3, [437] | wizards_tower_laddertop | generic ladder: face L+1, `human_pickupfloor` L+2, teleport L+3 (maplink 3104,9576) |
| [114] x6, [192], [452] | poordoor | generic door (`poordooropen`, synth 62) |
| [136], [331], [475] | Lumbridge teleport tablet Break | generic: bound (`teleport_tablet.rs2`), shape as recorded (4069/965 C, 678/4071 C+2, teleport C+4) |
| [147] x2 | poshdoor (church) | generic door (`poshdooropen`, synth 81) |
| [160] | Father Aereck Talk-to | this content, §3 |
| [200], [504] | shutghostcoffin Open | this content, §4 |
| [207], [394] | ghostx Talk-to | this content, §3 |
| [304] | farming_shed_poordoor (Urhney's hut) | generic door: **DEFECT (open)**, §8 |
| [308] | Father Urhney Talk-to | this content, §3 |
| [327] | amulet_of_ghostspeak Wear | this content: varbit 3732 **fixed**, §5 |
| [355] | openghostcoffin Search | this content, §4 |
| [422] | necklace_of_passage_3 Rub | generic: **DEFECT (open)**, no binding, §8 |
| [446] x3 | restless_ghost_altar Search | this content, §6 ([446] "I can't reach that!" is the engine's, a closed door between) |
| [509], [512] | ghostskull Inspect | this content: **was unbound, fixed**, §6 |
| [518] | ghostskull on openghostcoffin | this content, §7 |
| [552..557] | side stones, logout | the recorder's |

Teleports: [93] research notes (Rune Mysteries), [110]/[441] the ladder, [140]/[336]/[479]
tablets, [428] the necklace, [519] into the release copy, [538] out of it (to 3247,3193).

## 2. Stages and vars

| var | value | tick | with | ours |
|---|---|---|---|---|
| prieststart 107 | 0 -> 1 | [173] | the start menu's Yes, with "You've started a new quest" and the player's page | fixed (was set before the confirm existed) |
| | 1 -> 2 | [320] | the objbox "Father Urhney hands you an amulet." + the amulet | matched (objbox fixed) |
| | 2 -> 3 | [410] | the click into "It is so dull being a ghost..." | matched |
| | 3 -> 4 | [463] | the altar search | matched |
| | 4 -> 5 | [519] | the release's lock tick | fixed (was a queue after three invented mesboxes) |
| restless_ghost_altar_var 2130 | 0 -> 1 | [463] | altar search | matched |
| restless_ghost_skeleton_var 2128 | 0 -> 1 | [463] | altar search | matched; the sitting loc is no longer `loc_del`eted for everyone (Jagex sends no loc_del) |
| restless_ghost_coffin_var 2129 | 0 -> 1 | [519] | the release | fixed (was never written; ours swapped the coffin to `_with_head` by loc id) |
| wearing_ghost_speak_amulet 3732 | 0 -> 1 | [328] | the equip | fixed (had no writer) |

All four varbits sit on `varp728_restless_ghost_control` (a loc-transform carrier, transmitted);
`varp_transmit_audit.py` lists no quest var we fail to send. The journal was never opened
(`latest_quest_journal` 3679 only ever -1): **every journal stage is UNVERIFIED** (still the
LostCity text; the wiki has no Journal transcript for this quest).

## 3. Pages (`rsprox_pages.py idx4341.txt 140`)

Every recorded page is now in our scripts verbatim with its mood. Differences found and fixed:

| R | ours before | now |
|---|---|---|
| [170] mesbox "Before starting this quest, be aware that your combat level is lower than the recommended level of 10." + [171] "Start The Restless Ghost quest?" Yes./No. | absent | added (No: the wiki's two pages) |
| [173] "Okay, let me help then." | "Ok, let me help then." | fixed |
| [174] "...in the church graveyard. I would like you to get rid of it." | "graveyard: I would" | fixed |
| [175] "I believe he is currently living as a hermit in Lumbridge swamp. He has a little shack in the far west of the swamps." | the 2004 line | fixed |
| [176] chatneu3 "Exit the graveyard through the south gate to reach the swamp. I'm sure if you told him that I sent you he'd be willing to help." | absent | added |
| [312] "Father Aereck sent me to talk to you." chathap1 | neutral | fixed |
| [319] "...I vowed not to leave this place until I had done..." | "this place. Until" | fixed |
| [320] objbox (193) amulet, zoom 400 | mesbox | fixed |
| [407] "...by the sea south-west from here." | "southwest" | fixed |
| [408] "...back for you, then you can rest in peace." | "so you can" | fixed |
| [513] Inspect objbox, skull zoom 400 | no binding | added |
| [521] the ghost's page: no chathead anim, no continue, no echo | three invented mesboxes ("The ghost vanishes." ...) | rebuilt (§7) |

From the wiki (moods unrecorded): Aereck's Saradomin branch ("...! ...At least", "Yeah... I, uh,
thought you said something else.", "...all... You know. Slime and tentacles.", "This is an online
game!", "Aargh! Avaunt foul creature... Begone in the name of Saradomin!"), his completed greeting,
the stage-3 player line ("If I can find the skull, the ghost should leave."); Urhney's "sheesh, what a
grouch", "mortgage repayments" + "What? What houses?..." + "Never mind.", the four lost-amulet answers
(with you / banked / no space / a spare) under the option "I've lost the Amulet of Ghostspeak."; the
ghost's "...south-west...", "...then I'll be free!", and "Great. The first person I can speak to in
ages...and they're a moron." on one page. Deleted (in neither source): Aereck's "found the skull but
then lost it" exchange (a stage-4 player without the skull now gets the stage-3 talk, unverified).

Npc facing in dialogue: Aereck and Urhney wander and keep walking for a tick or two after the op
([162..164], [305..309]) before the `[facing] player=` one tick after the first page; the ghost
never moves. Ours freezes the speaker (`playerfaceclose`), the shared chat proc's rule.

## 4. The coffin and the ghost

| shape | R | ours |
|---|---|---|
| Open (walked) | L+1 [201] "You open the coffin." + face (2x2); L+2 `human_openchest` + synth `coffin_open` (54); L+3 `loc_add_change openghostcoffin` (15061) + `map_projanim_v2` `restless_ghost_travel_spotanim` 3250,3192 -> 3250,3195 (start 28, end 51/56, angle 15, progress 32, heights 163/0) + synth `rg_ghost_approach` (1743); L+5 ghost `[add]` 3250,3195 facing south with `restless_ghost_appear` + synth `bigghost_appear` (1595). [505..509] the same. | rebuilt to this (was: anim on O, `_no_head` loc id, ghost on 3249,3194 with no graphic or sounds) |
| Search, stage 2 | L+1 [363] the line "You search the coffin and find some human remains." + the travel graphic + synth; L+3 the ghost. | rebuilt (was the stage>=1 mesbox) |
| Search, stage >= 3 / complete | wiki mesboxes | kept; "There's a nice and complete skeleton" typo fixed |
| How long it stays open | [203] open -> [502] shut: 299 ticks | 300 (matched) |
| How long the ghost stays | not recorded (it left view at [234], [428]) | 100 ticks, unverified |
| Close | not recorded | LostCity's line and anim, without its ghost spawn |

Projectile heights are v2 raw: ours 40/0 (163/4 floored), 12/12 in the release.

## 5. The amulet

Equip [327..328]: `wearing_ghost_speak_amulet` 1 with the worn inv and synth 2238. Ours now writes the
varbit from `~priest_refresh_ghostspeak`, called where `equip.rs2` and `containers.rs2` refresh
the other worn-item varbits (the enchanted amulet counts too: unverified).

## 6. The altar and the skeleton

[463], all on L+1: "The skeleton in the corner suddenly comes to life!" (ours said "Out of nowhere a
skeleton appears!", fixed), altar var, stage 4, skeleton var, `skull_skeleton` `[add]` on 3120,9569
facing south with `restless_skeleton_get_up`, the skull, synth `rg_skeleton_awake` (1746, was
absent). [464] it steps south-west, [465] south and faces the player, [467]/[471] `human_sword_stab`
(386) + synth `armed_skeleton` (775) every 4 ticks (ours: `human_unarmedpunch` + 776, fixed in
`quest_priest.npc`; the generated lines removed). Defend/death never recorded.

[468] Search with the skull: "You already have the Ghost's skull." The altar is on its
`no_skull` child then, which our tree bound to a flavour line: both children now run the one proc.
After the quest: the wiki's "Surprisingly, this altar is empty...".

## 7. The release cutscene [518..547]

| tick | R | ours |
|---|---|---|
| O [518] | "You put the skull in the coffin." + face + `human_pickuptable` (832) + synth `rg_place_skull` (1744) | matched |
| O+1 [519] | coffin var 1, stage 5, `hpbar_hud_show_extra_bar` 0, the lock (accept-aid 0), into a copy (raw 13960,1232), the world ghost and the skull gone, fade out, cam_shake x5, xp drops + sides 1-6/11/12 closed, 6021, 5485, minimap_toggle 2 | matched (a copy of square 50_49; the orbs re-open and 3932 not sent) |
| O+3 [521] | player faces north; ghost `[add]` on the tile north (48,57) facing the player, `[say] "Release! Thank you"`; the hut door open in the copy; fade in; `cam_moveto` 52,57 h350 100/100, `cam_lookat` 46,57 h200 100/100; the page "Release! Thank you stranger.." (chat_left, anim -1, no continue, no echo) | matched |
| [525] | say "stranger.." | matched |
| [529] | `restless_ghost_vanish`; travel graphic 48,57 -> 44,57 (0/60, angle 30, heights 50/50); synth `shade_sigh` (748) | matched |
| [531] | graphic 44,57 -> 44,54; `cam_moveto` 40,57 h900, `cam_lookat` 43,55 h25 (100/100) | matched |
| [533] | graphic 44,54 -> 53,45 (0/95); `cam_lookat` 53,45 h25 rate 5/5 | matched |
| E [538] | unlock, teleport to 3247,3193, cast gone, fade out, cam_reset, hud back, cam_unlock false, cam_reset, sides, 5487, fade in, minimap_toggle 0 | matched (sides 8/9/10/13 are re-opened by Jagex and not by ours, as in every other scene) |
| E+3 [541] | qp, count, "Congratulations...", disease -50/0, song 147, jingle 152 `quest_complete_1`, scroll: "You have completed The Restless Ghost!", ghostskull at zoom 400, "1 Quest Point", "1,125 Prayer XP", "A Ghostspeak Amulet"; the ghost's page shut | matched (the icon was the amulet, the lines "1125 Prayer XP|Ghostspeak amulet": fixed) |
| [547] | the scroll's close shuts the fade overlay | ours shuts it (clear by then) at E+3, before the scroll: the scroll is shown from a queue that waits for the scene's script |

A logout inside the scene (`~priest_on_logout`) puts the stage back to 4 with the skull, so the
scene replays and the rewards are paid.

### Wire audit (our tick log, `test/quests/priest.lua` and a release-only run, 2026-10-27)

Aligned on the use (O): skull seq + `rg_place_skull` O; stage 5, lock, copy O+1; ghost
`[add]` + say + door O+3; "stranger.." O+7; `restless_ghost_vanish` + graphic + `shade_sigh` O+11;
graphics O+13, O+15; out of the copy onto 3247,3193 O+20; qp, song 147, jingle 152 O+23. Every
recorded beat on its recorded tick (R: [518], [519], [521], [525], [529], [531], [533], [538], [541]).
The camera keyframes (350/200, 900/25, the pan, reset) pass `t.cutscene.await`. Coffin open:
lid, coffin + graphic one tick on, ghost two ticks after that (R: [202], [203], [205]).

The first attempt copied two squares (50_49 + 50_50, to keep the church in view): its
`REBUILD_REGION_V2` went out exactly 1024 bytes long and the client dropped the connection on it.
One square, as every other scene here, plays cleanly.

`test/quests/priest.lua` (rebuilt to the recorded flow: the start confirm, the objbox, the varbit
on equip, the coffin's timing, the release with its cutscene rows): 77 rows, 0 failures.

## 8. Open

- `farming_shed_poordoor` (Urhney's hut): Jagex swaps it for `fai_barbarian_poordooropen` (11617) on
  3147,3173 rotation 2 with synth 62 ([307]); ours moves it as a `door_selfstage`. Generic doors.
- `necklace_of_passage_*`: Rub has no binding. Recorded [422..428]: menu "Teleport to..."
  `Wizards' Tower|The Outpost|Eagle's Eyrie|Cancel`; [425] "You rub the necklace..." (spam),
  `teleport_casting` (111, h92) + `human_castteleport` (714) + `sound_area` 200; [428] teleport to
  3114,3177, "Your necklace of passage has two uses left." (col 7f007f), the necklace down a charge.
- The journal (never opened in a recording).
- The coffin's Close, the ghost's lifetime, the pre-quest coffin and altar searches: unrecorded.
- Aereck's and Urhney's wandering through the first page (ours freeze them).
- **The altar skeleton never moves on ours.** It spawns on [463]'s tick and shape, then stands on
  3120,9569 with `stuck_counter` climbing every tick (R: steps [464], [465], swings [467], [471]).
  Not the sitting skeleton's collision: deleting that loc as well changed nothing, and our engine
  gates inactive ground decor (the room's `dugupsoil*`) as the reference does. Engine pathing,
  not content; the recorded attack seq/sound are in its config for when it can reach.
- The two-square copy's 1024-byte `REBUILD_REGION_V2` (engine).
