# Varrock Museum: the Natural History Quiz

Recordings: index **4340** (rev 241, 2026-10-23, "Varrock Museum": stairs, Orlando's
intro, all fourteen Buttons and plaques, two school-party chats, the reward, both stair
pairs back up) and **1707** (rev 227, Leagues V: an independent run of all fourteen
plaques and Orlando), and **4347** (rev 241, 2026-10-23, "Varrock Museum Plaques after
completion": all fourteen plaques clicked after the quiz). 4337 is an earlier cut of
4340 (identical rolls), not a second sample. Ticks below are 4340's unless marked.

Content: `OSRS-Content/osrs-content/server/scripts/areas/varrock/scripts/`
(`vm_natural_history_quiz.rs2`, `vm_natural_historian.rs2`, `vm_basement_stairs.rs2`).
Test: `test/quests/naturalhistoryquiz.lua` (scriptrun 595/595, 2026-10-23). Engine:
`sound_area_loops` (opcode 11284), for the four cases whose sound loops.

Before this pass the quiz was built from the wiki: the Buttons did nothing, the plaque
showed no creature (the model components ship empty and nothing set a model), questions
could repeat, answers lacked their full stops, a finished display opened an invented
"Common name / Scientific name" panel, Orlando said two lines Jagex never sends, ten of
his pages had the wrong mood, the hall's second staircase went to the ground floor, the
stairs teleported on the op tick, and the historians said invented one-liners.

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

## The natural historians (unrecorded)

No session on the index talks to one (npc 6002..6006, eight sessions searched). Built
from the wiki's Transcript:Natural_historian, generated from its text: the introduction
once (`vm_nathis_got_spiel`), then "Hello again, sir/madam" and the wing's menu (north:
lizards, tortoises, dragons, wyverns; east: snails, monkeys, sea slugs, snakes; south:
natural history, terrorbirds, the Kalphite Queen; west: camels, leeches, moles,
penguins), each lecture's pages with the historian playing the creature's pose seq
(`vm_natural_historian_*`, one per creature, cache) on each. Moods are unrecorded
(neutral).

## The school party

- Schoolboy [745]: "Can you find my teacher? I need the toilet!" (`chathap1`). The wiki
  gives nine lines at random for schoolboys and schoolgirls; the other eight are the
  wiki's.
- Female teacher and pupil, basement [795..796]: "Teacher" with the
  `vm_female_school_teacher_dummy` head, then "Pupil" with
  `vm_female_school_teacher_girl_dummy`, both `chathap1`. The ground-floor and
  second-floor pairs are the wiki's.

## Engine-generic, not content

- `busy` (12393) goes 1 on every dialogue and plaque in the recording (Duke, bank,
  trade too); our engine does not drive it.
- Our `[facing] coord=` for a 3x3 stair is the centre tile with no size fields.

## Open (needs a recording)

- The historians' lecture staging: the wiki says the player and historian teleport to "a
  cutscene in front of the display case" with school children watching
  (`vm_natural_historian_cutscene` is in the cache). Tiles, camera, fades and the
  children's placement are unknown; one recorded lecture would settle them. The pets
  refusal ("I'm afraid I don't allow pets in my lectures") is not built either.
- The male teacher and pupil (`vm_school_teacher_male_and_boy`): no recording and no
  wiki line; no dialogue.
- The plaque before Orlando's favour ("This looks like some sort of quiz...") is
  unrecorded (the wiki's message).
- One sample of each Button: the per-display timings are not yet confirmed by a second.
