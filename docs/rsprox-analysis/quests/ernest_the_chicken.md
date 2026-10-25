# Ernest the Chicken (quest_haunted) against RSProx

Written 2026-10-24. Content: `quests/quest_haunted/scripts/quest_haunted.rs2`,
`areas/draynor/scripts/{veronica,professor_oddenstein,draynor_manor_chairs}.rs2`,
`areas/draynor/configs/draynor_manor.{spawn,npc}`, `general_use/scripts/wardrobes.rs2`;
test `test/quests/haunted.lua`. The manor's two monsters (skeleton, nasty
tree) are audited separately in `ernest_the_chicken_combat.md`.

## Sessions

| session | rev | what it holds |
|---|---|---|
| index 4362 | 241.1, w631, normal, "Sheep Sheerer" | Veronica, both menu answers [88..103]; the fountain unpoisoned [137..142]; the compost without a spade [156]; the front doors from the grounds [193..195] |
| index 4363 | 241.1, w631, normal, "Ernest the Chicken" | the rest, start (stage 1) to the quest scroll [620]: wardrobe, Oddenstein x4, fish food, poison, back door, compost, fountain x3, front doors, closet x2, bookcase, both ladders, 21 lever pulls, 30 gate crossings, the hand-in |
| index 3192 | 232.1, w367, normal | post-quest lab: Oddenstein's two-row menu [10459..10637]; levers (B alone) [10899..11738]; bookcase from both sides [10866], [11742]; lever-up [11746] |

Recorder in 4362/4363 is male, combat 27. Ours: scriptrun of `test/quests/haunted.lua`
(private packs; 129/130 rows, the one failure is `quest.journal`, which no
scriptrun can pass), its `ticklog.tsv` and `TORIRSSERVER_TRACE_OUT` packets.
O = the op tick (the face), L = the last step, P = a page click.

## Dialogue

| page / menu | recorded | ours before | now |
|---|---|---|---|
| Veronica start menu | `chatbox_multi_init "Start the Ernest the Chicken quest?" "Yes.|No."` (4362 [90], [97]) | "Select an option" with the two sentences | `~p_choice2_header` as recorded |
| Veronica, quest started | varp 32 0->1 + "You've started a new quest: @q_mes@Ernest the Chicken</col>" on the click before "Ok, I'll see what I can do." [101] | varp only, no line | both |
| Veronica moods | shock2, neu1/shock1 (No), hap1, neu2 x3, neu1, hap1 | same | matched |
| Oddenstein stage 1 | "Be careful in here, there's lots of dangerous equipment." (neu1) then the 3-row menu, 2/2 ([40..41], [48..49]) | menu only | fixed |
| Oddenstein stage 1/2 pages | 20 pages, moods as recorded | same | matched |
| hand-in lines | "You give a rubber tube, a pressure gauge, " (trailing space) / "and a can of oil to the professor." [605] | "the rubber tube" | fixed |
| "Let's get this fixed then." | chathap1 [605] | neutral | fixed |
| Ernest | "Thank you, sir. It was ..." chathap2; then player shifty1, Ernest neu1, each page a tick after the click before it [616..619] | no comma; same-tick pages | fixed ("m'lady" unverified) |
| post-quest Oddenstein | no warning, menu "What does the portal machine do?|Is this your house?"; quiz1 / hap4 / quiz1 "Huh?" / hap1 "It's a teleport machine."; quiz1 / neu2 (3192) | warning + 3 rows incl. an invented "Can I use the rift?" and invented killerwatt lines | rebuilt as recorded |
| reward scroll | "4 Quest Points", "300 Coins", model `feather` (314) [620] | "300 coins", "Access to the Killerwatt Plane", model coins | fixed |
| `slayer_killerwatt_portal_check` (1766) | never written by the quest or the lab talk (4363 [620]; 3192 talk) | set at completion and on every post-quest talk | removed (the portal itself still sets it) |
| `ernesthandin_complete` (19655) | 0->1 with the parts [605]; sent at every later login (4358/4359) | never | set in `~haunted_take_parts`; varp 5219 declared perm + transmit |

Every quest line our run sends (game messages and page echoes, in order) was
checked against the recording's list; none is sent that the recording lacks.

## Interactions (O-relative shapes)

| interaction | recorded | ours before | now (our tick log) |
|---|---|---|---|
| front doors from the grounds (2/2: 4362 [192..195], 4363 [291..294]) | player on the leaf's tile; O: both leaves `inviswall` r1 + `haunteddoorl_inactive` r0 / `haunteddoorr_inactive` r2 on z+1 for 2 ticks, `bigdoor_open` 46, step north; O+1 "The doors slam shut behind you." + `bigdoor_close` 45 | `loc_change` in the door slot, teleport, extra "You go through the doors." | identical shape [23..25], [488..490] |
| back door (4363 [199..202]) | walk-through: `inviswall` r3 + `hauntedbackdoor_inactive` r0 on 3123,3360 for 2 ticks, `creakydoor_open` 56, one step | door_selfstage swing (stays open 500) | identical [424..426]; category removed from `doors_selfstage.loc` |
| compost, Search with a spade (4363 [229..232]) | O: "You dig through the compost..." + `human_dig` + `digspade` 1470; O+3 "... and find a small key." | op1 always refused; only use-spade dug | op1 digs, O / O+3 |
| compost, no spade (4362 [156]) | player page "I'm not looking through that with my hands!" chatang1 | a mesbox | player page, angry |
| fountain search (3/3) | O face; O+1 `human_pickpocket` + `fish_swim` 2601; O+2 first page. Unpoisoned: on the click "Something in the water bites you.", "Ow!", splat 1, page closed; next page a tick later (chatcon2) | anim at O, no sound, second page same tick | O+1 / O+2, the bite then `p_delay(0)` |
| poisoned food into the fountain (4363 [257..262]) | O line + `human_pickpocket` + `ernest_sprinkle` 1566; O+2 line + `fish_swim`; O+5 line | no sounds | matched [473..478] |
| poison on fish food (4363 [139..140]) | result in the food's slot, no sound | same | matched |
| closet door, in (4363 [309..312]) | O: "You unlock the door." + `unlock` 2419 + step onto the door tile; O+1 `inviswall` r2 + `draynorpanelleddoor_inactive` r3 on 3108,3367 for 2 ticks, `cupboard_open` 58, step in | "You unlock the door." + "You go through the door." + teleports | identical [498..501] |
| closet door, out (4363 [316..318]) | all of it on O | | identical [505] |
| bookcase from the east (4363 [332..335]; 3192 [10870..10873]) | O+1: line, both cases `loc_del` + `hauntedbookcaseopen` at 3097,3357 / 3097,3360 for 2 ticks, `coffin_open` 54, player to 3096,z | line at O, cases to `inviswall` for 3 ticks, no sound | identical [564..566] |
| bookcase from inside (3192 [11743]) | nothing | nothing | matched |
| lever in the secret room (4363 [570..577]; 3192 [11746..]) | O+1 line + `human_leverdown` + `lever` 2400 + `hauntedleverdown` for 3 ticks; O+3 step north; O+5 bookcase open (`bookcase_open_and_close` 42) + 2 east; O+7 restore | step at O+1, bookcase at O+4, no sounds | identical [1292..1299] |
| ladder down (4363 [338..339]) | O face + `human_pickupfloor`; O+1 teleport to 3117,9753 + `ernest_clunk` 1565 delay 5 | arrivedelay first, landing 3116,9754 | identical [568..569] |
| ladder up (4363 [564..565]; 3192 [11737..11738]) | O face + `human_reachforladder`; O+1 line, the six lever bits cleared, teleport 3092,3361; the gates untouched (2/2) | arrivedelay first; gates cleared too | identical [1281..1282] |
| lever pull (21 pulls) | O+1 line, lever bit, "You hear a clunk.", gates, `macro_lever_switch_down` 2140 / `_up` 2139, `lever` + `ernest_clunk` d5 | `human_leverdown` both ways, no sounds | identical |
| lever inspect (3192) | "The lever is up." / "The lever is down." | same | matched |
| wardrobe open (4363 [22..24]) | L+2 `human_openbigcupboard` + `wardrobe_open` 96, L+3 open loc | no sound | sound added |

### The gates

Each gate is a shape-10 loc whose tile belongs to the room on its open side,
with a `blankwall_no_blockrange` on its west (angle 0) or south (angle 3)
edge. 30 crossings in 4363, two shapes, both restored after 2 ticks:

| from | recorded | ours before | now |
|---|---|---|---|
| the gate's room (op; [399..403], [406..410]) | O = L+1: `iron_door_open` 71 delay 15 + step onto the gate tile; O+1 gate `loc_del` + `inactiveprisondoor_l` (angle+1) on its tile; O+2 step out across the wall | a 2-tile teleport, and nothing at all from a tile not in line with the gate (the [400] approach from the north) | identical [642..645] etc.; the op is bound to the open child `ernest_doorajar`, which the engine resolves first |
| across the wall (ap; [376..379]; standing [404..406]) | the sound on the arrival tick L; L+1 swap + step onto the gate; L+2 one tile on; L+3 restore | a 2-tile teleport | swap/step/restore on L+1/L+2/L+3; **the sound at L+1** (our ap answers the tick after the arrival step: engine-generic) |

The lever -> gate rule. The 2004 rule this file carried (LostCity) is wrong
in rev 241: lever A alone opens 4to5 there and nothing in LostCity's. Every
recorded state (4363: 21 pulls, 10 states; 3192: B alone):

| levers down | gates open |
|---|---|
| none / B | - |
| A | 4to5 |
| AB | 4to5 4to7 |
| ABD | 4to5 4to7 5to6 5to8 |
| AD | 3to6 4to5 5to6 5to8 |
| D | 3to6 5to6 5to8 |
| DE | 3to6 |
| DEF | 1to2 2to3 |
| CDEF | 1to2 2to3 8to9 |
| CDF | 2to3 2to5 5to8 8to9 |

The 2004 rule fits 1to2 (!A!B D E F), 2to3 (!B D F), 2to5 (!A!B C D !E F) and
3to6 (!B D !F) in all of them and is kept for those. The other five are
fitted: 4to5 = A, 4to7 = A&B, 5to6 = D&!E&!F, 5to8 = D&!E, 8to9 = C&F.
Unrecorded states could split 5to6 (D&!E&!C fits equally) and 8to9 (C alone).
Every pull re-sends all nine gate bits (clear, then set: 4363 [385]).

## The hand-in scene (4363 [605..620]; ours [1452..1473])

| P+ | recorded | ours |
|---|---|---|
| on "Give 'em here then." | varbit 19655, the three parts gone, two lines, page "Let's get this fixed then." | same |
| 0 (its click) | page closed; Oddenstein's first step, `[facing] type=reset` | same (routed walk: he wanders, so `npc_routeapproach` beside the machine's 3112,3367) |
| +2 | arrives 3111,3367 (three steps from 3109,3368) + `human_pickuptable` + "Oddenstein starts up the machine." | on arrival (four steps from 3108,3369 in our run) |
| +3 | `[facing] coord=(3112, 3367)` | same |
| +4 | "The machine hums and shakes." + `swan_rumble` 1790 | same |
| +7 | `stun_casting` map anim at 3111,3366 h50; `stun_travel` 3111,3366 -> the chicken, 15..45, angle 0, progress 0, heights 50/0 (v2: ours 12/0); `stunned` on the chicken delay 45; `stun_all` 187 x2 | same |
| +9 | varp 32 2->3; the chicken gone, `ernest_multiernest` (shown as `ernest` at stage 3) added on its tile facing south, wandering; `chicken_into_human` 1564; Ernest's page | same (Ernest a 100-tick world npc; his life after the page is unrecorded) |
| last click | coins +300, qp +4, line, scroll, music 147 + jingle 153 | same |

The world chicken is never retyped: its own multinpc hides it at stage 3. It
wanders the lab (4363: 7 steps over 3109..3111 x 3363..3366), which the
generated movement had pinned (`wanderrange=0` and `defaultmode=none`).

## Npc info

| npc | recorded | ours |
|---|---|---|
| ernest_multichicken | wanders, 1 tile a step | was stationary; now wanders indoors |
| professor_oddenstein | wanders indoors; faces the player once per talk (`[facing] player=`) | wanders; **our shared `~chatnpc` re-sends a coord face + the playerfaceclose latch every page** (engine/shared, every quest's dialogue) |
| veronica | wanders (4 steps); faces the player | same |
| draynor_possesed_chair | 4 stationary npcs, 3120,3354 / 3121,3354 / 3122,3353 (0) facing south-west, 3108,3361 (1) facing north-west (28/28 lives) | **were never spawned** (the spawn dump lacks them); now spawned, `defaultmode=none`, `[ai_spawn]` facing (256 / 768 on our wire) |
| house_ghost .. house_ghost5 | first floor, wander | same tiles, wander |
| fish food | obj at 3108,3356,1 (3/3 zone sends) | was 3109,3357,1 (fixed in `m48_52.spawn` and `tools/gen_spawns.py` OBJ_SPAWN_RELOCATIONS) |

## Open

- The ap-side gate sound lands a tick after the recording's (the engine runs
  the ap rung before the arrival step).
- `busy` (varbit 12393) is never sent by our engine, for any content.
- The shared `~chatnpc` face churn (above).
- `farming_tools_leprechaun_draynor` at 3088,3357 (4362/4363) is our generic
  `farming_tools_leprechaun` at 3087,3352: farming, not this quest.
- The crypt under the manor (coffin "The coffin is sealed shut.", "The candles
  on the floor burn your feet!", 4363 [160..168]) is Vampyre Slayer content,
  not audited here.
- Unrecorded and kept from LostCity, labelled in the script: the doors from the
  hall, the closet without a key, a second dig, using other items on the
  fountain, Oddenstein at stage 0, the female "m'lady".
