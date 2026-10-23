# Varrock Museum: the Natural History Quiz

Recordings: index **4340** (rev 241, 2026-10-23, "Varrock Museum": stairs, Orlando's
intro, all fourteen Buttons and plaques, two school-party chats, the reward, both stair
pairs back up) and **1707** (rev 227, Leagues V: an independent run of all fourteen
plaques and Orlando), and **4347** (rev 241, 2026-10-23, "Varrock Museum Plaques after
completion": all fourteen plaques clicked after the quiz), and **4348** (rev 241,
2026-10-23: the four natural historians and all fourteen lectures, the school party).
4337 is an earlier cut of
4340 (identical rolls), not a second sample. Ticks below are 4340's unless marked.

Content: `OSRS-Content/osrs-content/server/scripts/areas/varrock/scripts/`
(`vm_natural_history_quiz.rs2`, `vm_natural_historian.rs2`, `vm_basement_stairs.rs2`).
Tests: `test/quests/naturalhistoryquiz.lua` (scriptrun 603/603) and
`test/quests/naturalhistorylectures.lua` (all fourteen lectures, 114/114), 2026-10-23. Engine:
`sound_area_loops` (opcode 11284), for the four cases whose sound loops.

Before this pass the quiz was built from the wiki: the Buttons did nothing, the plaque
showed no creature (the model components ship empty and nothing set a model), questions
could repeat, answers lacked their full stops, a finished display opened an invented
"Common name / Scientific name" panel, Orlando said two lines Jagex never sends, ten of
his pages had the wrong mood, the hall's second staircase went to the ground floor, the
stairs teleported on the op tick, and the historians said invented one-liners with no
lecture cutscenes.

## Stairs (`vm_basement_wall_stairs_down` / `_up`)

Two flights share each loc type; the landing goes by the flight. Face on L+1, teleport
on L+2, no anim (`p_arrivedelay`); already adjacent, on the click tick.

| Flight | Recorded | Ours |
|---|---|---|
| ground 3255,3451 down | [409] face, [410] -> 1759,4958 | matched |
| hall 1758,4963 down (level-1 loc) | [1062] face, [1063] -> 1631,4957 | matched (was 1759,4958) |
| 1630,4958 up | [1083]/[1084] -> 1761,4964 | matched (was 3258,3452) |
| hall 1758,4959 up | [1092]/[1093] -> 3258,3452 | matched |

## Buttons (the animatronics)

O = the op tick (the face), P = the push. The button sinks on O (`vm_button_1x1`, or
`_2x1` for the lizard, tortoise, dragon and wyvern); on P (O+1 after a walk, O from a
standing start: the second Kalphite Queen press [647]) `vm_player_button_push` 6462,
`cam_moveto`/`cam_lookat` (rates 10/10) and synth `vm_push_button` 3664; on P+4 the
case's seq, `vm_display_case_cogs` 6427 on its `vm_cogs_*` loc and its area sound
(range 10); the button comes back on O+revert; `cam_reset` on P+hold.

| Display | Case (seq) | Sound (loops) | Camera at / looks at | revert | hold |
|---|---|---|---|---|---|
| Snail [440] | npc 6009 (6430) | vm_snail 3666 (3) | 1778,4960 h425 / 1775,4965 h300 | 11 | 15 |
| Snake [484] | loc `vm_nat_his_snake` (6452; 6453 `_fixed` at P+16) | vm_snake 3667 (3) | 1779,4962 h350 / 1781,4964 h325 | 14 | 16 |
| Sea slug [529] | npc 6008 (6432) | vm_seaslug 3665 (4) | 1782,4959 h350 / 1782,4958 h300 | 11 | 17 |
| Monkey [578] | npc 6010 (6434) | vm_monkey 3662 (2) | 1778,4958 h550 / 1776,4956 h325 | 11 | 13 |
| Kalphite Queen [624], [647] | loc `vm_nat_his_kalphite_queen` (6476; 6477 `_fixed` at P+11) | vm_kalphite 3655 | 1759,4946 h700 / 1764,4939 h100 | 7 | 13 |
| Terrorbird [679] | npc 6014 (6442) | vm_terrorbird 3668 | 1757,4942 h375 / 1756,4941 h300 | 6 | 14 |
| Penguin [726] | npc 6012 (6438) | vm_penguin 3663 | 1749,4959 h500 / 1743,4956 h300 | 6 | 11 |
| Mole [765] | npc 6018 (6450) | vm_mole_burrow_up 3661 | 1733,4959 h375 / 1734,4958 h300 | 6 | 10 |
| Camel [800] | npc 6013 (6440) | vm_camel 3647 | 1732,4958 h500 / 1735,4962 h300 | 9 | 14 |
| Leech [835] | npc 6007 (6428) | vm_leech 3656 | 1743,4956 h650 / 1743,4962 h200 | 9 | 14 |
| Lizard [887] | npc 6011 (6436) | vm_lizard 3657 | 1747,4975 h525 / 1742,4980 h300 | 7 | 12 |
| Tortoise [923] | npc 6017 (6448) | vm_tortoise 3669 | 1756,4976 h425 / 1753,4979 h250 | 8 | 13 |
| Dragon [962] | npc 6015 (6446) | vm_dragon 3649 | 1774,4978 h925 / 1766,4981 h300 | 3 | 11 |
| Wyvern [996] | npc 6016 (6444); cogs re-added at P+6 | vm_wyvern 3671 | 1781,4977 h800 / 1776,4980 h300 | 4 | 9 |

Audit (ours = scriptrun tick log + wire log, every button, aligned on O): 10-11 events
matched per display, 0 recording-only, 0 ours-only, except the wyvern's cog reset, which
Jagex sends as `loc_del` + `loc_add_change` (twice) and ours as one `loc_add_change` of
the same loc: the same client result. One recording each; the second Kalphite Queen
press agrees with the first on every offset from its own P.

## The plaque (interface 533)

Every open (44 in ours, every one in 4340 and 1707): L+1 face; L+2 the player's
`vm_display_case_ponder` 6383, the face again, `vm_nathis_text` = the title,
`vm_nathis_display_num`, `if_setmodel` on the creature's component(s) with
`if_setrotatespeed (0, 5)` (the dragon's three and the sea slugs' four are set
unturned), the question and three answers, `if_sethide quiz_text false`,
`toplevel_mainmodal_open` 2524 (0, 100), the mount. `creature_text` is never touched (it
ships hidden; `vm_nathis_text` lives inside it). The kudos overlay (532, `overlay_hud`)
is closed on each open once there are kudos.

An answer: `anim -1`, synth `vm_correct` 3648 / `vm_incorrect` 3654, the overlay back
(once there are kudos), the modal closed, Orlando's page (`chathap1` / `chatsad1`).
Its continue reopens the plaque on a new question and closes the page. The third right
answer: the counter, `vm_kudos` +2, spam "You have earned Kudos with the Museum!",
synths 3648 + `vm_gain_kudos` 3653, the overlay, "Nice job..."; continue -> synth
`vm_all_correct` 3643 and "Bonza, mate! I think that's all of them."; continue closes.
No reopen, no creature panel.

A finished display's plaque opens nothing: on the op tick (the face, L+1) its title as
a game message, "The Giant Snail (Achatina Acidia Giganteus)" (4347, 14/14 plaques, e.g.
sea slug [88], monkey [94], Kalphite Queen [111]); no anim, no interface. Ours had an
invented creature panel ("Common name: ... Scientific name: ...").

| Rule | Count |
|---|---|
| A wrong answer keeps progress; the plaque reopens on a new question | 2/2 (snail [468], snake [508]) |
| No question repeats within a sitting | 28 sittings, 0 repeats (4340 + 1707) |
| Answer order fixed per question; right row agrees in both sessions | 63/63 |
| The last display adds nothing after "Bonza" (ours had "Well done..." and "Come over...") | 2/2 |

Questions: 63 of the 84 recorded verbatim. Every answer ends in a full stop except "A
musical instrument". Wording fixes over the wiki: "Anti dragon-breath shield.", "What is
special about Morytanian leeches?", "Are kalphites carnivores, herbivores or
omnivores?", "Chilli.", "Sarcopterygii.". The 21 unrecorded ones are the wiki's with the
same full stops (marked `// wiki`).

Wire audit (snail sitting, block by block, toplevel group and question text
normalised): open, right answer, wrong answer, completion, Bonza and close all match
packet for packet. Models checked on our trace: 61 `IF_SETMODEL` and 37
`IF_SETROTATESPEED` = one per creature component per open.

## Orlando Smith

All 23 recorded pages ([416..430], [1039..1051]) match in text, `<br>` and mood
(`chatcon` on his greetings and "come a gutser", `chathap` on the player's "Good
day.<br>Are you alright?..."; the cobber page's explicit `<br> Could you`).
`vm_nathis_agreed` is set with the choice, before "Sure thing." ([428]); Jagex never
sets `vm_nathis_got_spiel` here (0/2: it is the historians'). The reward on the "You
know..." page [1046]: `vm_nathis_reward_given`, "You have gained 1000xp in Slayer and
Hunter.", Slayer then Hunter 1000 xp each. The decline, "not yet", "getting there" and
after-reward branches are unrecorded (wiki).

## The natural historians (4348)

Index **4348** (rev 241, 2026-10-23, "Varrock Museum Historian Cutscenes") talks to all
four: the introduction once (north, [75..84], `vm_nathis_got_spiel`), then straight to
the menu; after it "Hello again, sir, how can I help you on this fine day?" and "I was
hoping you could tell me about something." (`chathap1` both). The chosen line is echoed
`chatneu1` ("Tell me about the tortoises."). Natural history is four pages and ends the
chat ([477..482]). Every other topic is "Ahh ...", "If you just follow me to the display
case I shall explain all about them." and a cutscene.

**The frame** (all fourteen alike; T = the "follow me" page's click):

| tick | packets | ours |
|---|---|---|
| T | the lecture's track (`midi_song_v2`, envelope 0/60/60/0), the kudos overlay shut, fade out (948 `0,255,0,0,50`), the page closed | `midi_hold` + `midi_song_envelope`, `~vm_hc_fade_out` |
| T+3 | the lock (fov clamp, `cutscene_status`, ops hidden, minimap 2, area sounds 25, draw distance); the player into a new copy of the basement (raw x > 10000); the cast added and faced; shakes cleared, xp drops and sides 0-6, 11, 12 closed, 6021, `highlighting_off`, `minimap_toggle 2` | `~vm_hc_lock`, `~vm_hc_copy_enter`, the cast, `~vm_hc_hud_lock` |
| T+4 | the case's Button and plaque replaced by `vm_button_1x1` and `vm_plaque_inactive` (not in the snail scene), `cam_moveto`/`cam_lookat` at rate 100, fade in | |
| T+5 | one watcher turns to the player (held) | `npc_setmode(playerface)` + `npc_faceplayer` |
| T+6.. | the pages: each with the player's face to the historian; on some, his pose seq | `~vm_hc_page` |
| E | (the last click) Looking Back (309, same envelope), fade out, the page closed | |
| E+3 | the player back in the basement beside the case | |
| E+4 | the unlock, `cam_unlock false` + `cam_reset`, the sides back, `highlighting_on`, fade in, `minimap_toggle 0` | `~vm_hc_unlock` (no `cam_unlock`: not an engine command yet) |
| E+6 | the fade overlay closed, the kudos overlay back, Looking Back again | `midi_hold(0)` + the song |

Without the hold the engine's region music started Looking Back as the player entered
the copy (a copy of the museum's square), under the lecture's track.

**The scenes** (cast in the copy, tiles the recording's; P = a page with a pose). The
poses follow no clock (one comes 2 ticks after the last, another is skipped after 6;
the tortoise poses on two pages running, the dragon skips three), so each page carries
its own flag. The snail lecture is staged in the west wing at the penguin case's tile;
the snake scene re-adds `vm_nat_his_snake` on the lock tick; three scenes also add the
neighbouring case.

| Lecture [T] | Track | Cast | Poses by page | Back at |
|---|---|---|---|---|
| Lizards [94] | Desert Voyage (174) | school_boy_blonde, teacher_girl_multi, case:lizard, historian | `P.P.P.P..` | 1741,4976 |
| Tortoises [146] | Attack 1 (24) | school_teacher_male_and_boy, historian, case:battle_tortoise, school_girl_blonde, case:lizard | `P.P.P.PP.` | 1751,4976 |
| Dragons [191] | Scape Wild (332) | school_teacher_male_and_boy, case:wyvern, case:dragon, historian | `.P...P.P.P.P.` | 1767,4977 |
| Wyverns [235] | Woe of the Wyvern (529) | school_girl_blonde, school_teacher_male_and_boy, historian, case:wyvern | `P.P.P.P.P...` | 1776,4976 |
| Snails [291] | Stagnant (241) | school_girl_brunette_var1, school_boy_brunnete, case:snail, historian | `P.P.P.P.P..` | 1775,4961 |
| Monkeys [332] | Tribal (165) | school_boy_blonde, teacher_girl_multi, case:monkey, historian, case:seaslugs | `P.P.P.P..` | 1775,4959 |
| Sea slugs [370] | Fruits de Mer (347) | school_girl_blonde, school_teacher_male_and_boy, case:seaslugs, historian | `P.P.P.P.P..` | 1782,4959 |
| Snakes [425] | Jungle Island (172) | historian, school_girl_blonde, school_teacher_male_and_boy | `P.P.P.P.P.` | 1782,4961 |
| Terrorbirds [495] | Gnome Village (33) | case:terror_bird, historian, school_girl_blonde, school_teacher_male_and_boy | `P.P.P.P.P.P.` | 1757,4939 |
| Kalphite Queen [536] | Insect Queen (260) | school_girl_blonde, school_teacher_male_and_boy, historian | `P.P.P.P.P..P.` | 1760,4939 |
| Camels [598] | The Desert (79) | historian, school_teacher_male_and_boy, case:camel, school_girl_blonde | `P.P.P.P.P.` | 1736,4961 |
| Leeches [640] | Deadlands (288) | case:leech, historian, school_girl_blonde, school_teacher_male_and_boy | `P.P.P..P.P.P.` | 1743,4961 |
| Moles [679] | song 573 (573) | school_teacher_male_and_boy, school_girl_blonde, case:mole, historian | `P.P.P.P.` | 1736,4959 |
| Penguins [721] | Poles Apart (548) | school_teacher_male_and_boy, school_girl_brunette, case:penguin, historian | `..P.P..P.P` | 1743,4959 |

Audit (ours: `test/quests/naturalhistorylectures.lua`, all fourteen; tick log + wire log
against 4348, aligned on each lock): cast and tiles, every face on T+3..T+6 (coord and
entity), props, camera (tile, height, rates), the pose on each page, the track at T and
its envelope, Looking Back at E and E+6, the return on E+3, the unlock on E+4 and the
overlay on E+6: 14/14 scenes, no field different. Every page the quiz test sends from
4348's content (intro, menus, natural history, the terrorbird lecture, the male teacher:
34) matches in text, `<br>` and chathead animation; the lecture test checks every
lecture page's text. Not compared: spawn angles (ours `npc_facedir`, which the tick log
does not carry) and the unlock's `sanity` varbit (15064 0 -> 100).

## The school party

- Schoolchildren in the basement (4348 [52..72], six talks; 4340 [745]), one at random:
  "The monkeys are so funny-looking!", "I want a pet sea slug for my birthday. They are
  so cute!", "Can you find my teacher? I need the toilet!", "Do you know where the
  moles are?" (twice), "We're going to see the dragons! They're my favourite!"
  (`chathap`). Elsewhere the wiki's nine (unrecorded).
- Male teacher and pupil (`vm_school_teacher_male_and_boy`, 4348 [37..46], again
  [581..586]): "Pupil" (`vm_male_school_teacher_boy_chathead_dummy`, `chatshock1`)
  "Teacher! Sir! I need the toilet!", "Teacher" (`vm_male_school_teacher_chathead_dummy`,
  `chatbored1`) "I told you to go before we got here.", the pupil "But sir, I didn't need
  to go then!", the teacher "Alright, come on then."
- Female teacher and pupil, basement (4340 [795..796], 4348 [27..30]): "Teacher" with the
  `vm_female_school_teacher_dummy` head, then "Pupil" with
  `vm_female_school_teacher_girl_dummy`, both `chathap1`. The ground-floor and
  second-floor pairs are the wiki's.

## Engine-generic, not content

- `busy` (12393) goes 1 on every dialogue and plaque in the recording (Duke, bank,
  trade too); our engine does not drive it.
- Our `[facing] coord=` for a 3x3 stair is the centre tile with no size fields.

## Open

- The kudos overlay: Jagex shows it whenever a player with kudos is in the museum
  (opened walking in at 4347 [51], on login in the basement at 4348 [2]). Ours opens it
  only from the quiz and after a lecture. One entry crossing and no exit are recorded,
  too few to fit the area.
- `cam_unlock false` at a lecture's unlock is not sent (not an engine command yet).
- Our dialogue layer turns the historian to the player on every page (a coord face and
  the `playerfaceclose` mode); Jagex sends one entity face per talk. Shared by every npc
  chat; not changed here.
- Unrecorded: the historians' "That's enough education for one day." and their pets
  refusal (not built); the ground-floor school party; the plaque before Orlando's favour
  ("This looks like some sort of quiz..."). One sample of each Button and each lecture.
