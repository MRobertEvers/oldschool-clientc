# Enlightened Journey, from RSProx

Rebuilt 2026-11-22 from four sessions, all rev 242.1, one recorder:

| index | what it holds | ticks |
|---|---|---|
| 4637 | Forgettable Tale first, then EJ from the start to stage 90, the first five flights (1 bail, 1 sea crash, 2 woodland crashes, 1 bail), 6 journal openings | [2717..4457] |
| 4641 | one flight at stage 90 (bail over Entrana) | [29..75] |
| 4643 | after the quest: the completed journal | [661] |
| 4644 | 13 flights at stage 90 (6 bails, 4 sea, 2 woodland, the landing), stage 100, the completion, 2 journal openings | [1449..3717] |

The content before this was written from the wiki and Quest Helper. Every page was
"original wording", the flight's obstacles were made up from Quest Helper's
corridor tables, a crash was a mesbox, and the three scenes were missing. Everything
below is now from the recordings unless it says UNVERIFIED.

Files: `quest_enlightenedjourney/scripts/ej_shared.rs2` (Auguste, the journal, the
completion), `ej_cutscenes.rs2` (the frame and the three scenes), `ej_flight.rs2`
(the flight), `ej_crashsite.rs2` (both crash sites), `ej_crafting.rs2` (origami,
sandbags, the basket); `configs/zep_flight.enum` (the three screens). Tests:
`test/quests/enlightenedjourney.lua` (208/208 on scriptrun, two seeds) and
`test/quests/_ej_crashes.lua` (bail, sea, woodland; all pass).

## Stages (zep_quest, varbit 2866 on varp 912: perm, transmit)

| value | written | with |
|---|---|---|
| 5 | 4637 [2788] | "Yes! Sign me up."'s click; the "started" line with the next page |
| 6 | [2799] | "Let me show you!"'s click; the design (interface 472) opens |
| 10 | [2854] | the last page of the hot-air explanation |
| 20 | [2932] | "Good, you have everything!..." |
| 40 | [3042] | scene 1's E+3, the origami balloon taken |
| 60 | [3108] | scene 2, the shrug; 2 papyrus and the sack taken |
| 70 | [3203] | "Here you go..."'s click; sapling and apples given |
| 80 | [3329] | "That's the last of it!" |
| 90 | [3511] | 2 ticks after "Great! Let me just put it all together..."; 12 branches taken |
| 100 | 4644 [3567] | the landing's T+1 |
| 200 | [3701] | the last completion page's click |

The other words on varp 912, every write recorded: zep_bowl [3273], zep_silk
[3319], zep_ydye [3324], zep_rdye + zep_alldye [3326], zep_sandbags [3329]: each
written 1 (silk and sandbags were counts before), all six back to 0 at 200.
zep_multi_basket 0 -> 2 at [3516] (the basket scene; ours wrote 3 at take-off
before). zep_multi_piccard 0 -> 1 at the landing, 1 -> 2 when the player left
Taverley (4644 [3606], a glory teleport).

Varp 913 (the flight's counters) was `scope=temp`; the recordings show it saved:
4641 logs in with sandbags 7 / logs 9, what 4637's last flight left. Now perm.

## Pages

Every page in the four sessions (~190, `rsprox_pages.py`) is in ej_shared.rs2,
ej_cutscenes.rs2 or ej_crashsite.rs2 verbatim, with its mood, Jagex's own `<br>`s
and stray spaces kept. Menus as recorded, including the materials menu's two- or
three-answer form ([2858]/[2899]) and the give menu shrinking as items are handed
over ([3267], [3273], [3319], [3326]). From the wiki, marked UNVERIFIED: the
requirements refusal, the bowl hint, buying a sapling with the money, the weight
refusal, the full-inventory refusal at the end, the talk after the quest, the
two missing-papyrus lines at stage 40.

## Scenes

| scene | session, ticks | copy | frame |
|---|---|---|---|
| first experiment | 4637 [2997..3046] | Entrana square (43,52), new each time | C fade, C+3 lock + copy + cutscene Auguste, C+4 cam (90/90) + launch + jingle, C+7 burning projectile + pan (5/10), C+14 pages, E fade, E+3 unlock + stage + back, E+4 lift, E+7 overlay shut |
| second experiment, flash mob | 4637 [3085..3129] | the same | C+3 cast of 5 (peasants 1-4 face south), C+4 cam, C+6 launch, C+8..11 the peasants walk west, C+9 "Burn, stab, burn!", C+17 "Destroy the floating thing!" + the fire, C+21 "We are peasants against science!" + cam, C+23 stage 60 + shrug + page |
| basket | 4637 [3509..3530] | none: the island | C+2 stage + fade, C+7 lock + balloon (multi_basket 2) + cam 1050, C+10 jingle + crane pan to 1950, C+16 fade, C+19 unlock |

Our wire (TORIRSSERVER_TRACE_OUT=2, run5) puts every one of these on the same tick
offset as the recording. Jingle 116 is `burning_experiments_enlightened_journey`
in our cache (the recording names it "balloon journey test").

## The flight

Fitted on all 19 recorded flights. Replaying each one's presses through these rules
gives the recorded outcome every time, and every press before the last survives:

- interface 470, 13 rows by 20 columns, row 0 at the bottom; the balloon is two
  cells, envelope on row h, basket on row h-1, at column zep_if_screen_dist;
- each press moves it one column: Sandbags +2, Logs +1, Relax 0, Tug -1,
  Emergency -2; sounds 3249, 3251, 3242, 3241, 3239, then 3246 after the move;
- off column 19 is column 1 of the next screen ([3024]: 19 -> 1); rows 0..10
  are emptied (220 `if_setmodel -1`), their angles reset, then the new screen;
- the move crashes when either cell holds a model (row 1's scenery strip aside),
  or when h leaves 4..10;
- column 19 of screen 3 lands at h 5 and crashes at any other height ([3361]);
- each screen's models are fixed: one grid per screen in all 19 flights, now
  `zep_flight_screen1..3`, in the order Jagex sends them. Birds (19779, 19780)
  get angle 201,318,2576 + `bird_ready`, the killerwatt (19781) 99,333,3472 +
  `killerwatt_biped_ready`;
- take-off uses ten normal logs ("You used the last of your Logs.", spam);
  counters 8 and 10.

The landing route (4644 [3496..3566], 57 presses) is the test's route and lands
on 2941,3420.

### Crashes: where the balloon was decides it

| where (the column before the crash) | outcome | recorded |
|---|---|---|
| screen 1, columns 0..13 | "You bail, but land safely on Entrana.", 2809,3355 | 7/7 (4641 [64]; 4644 [1655], [1676], [2064], [2101], [2209], [2245]; 4637 [3632], [4135]) |
| screen 1, column 14 | the woodland | 4637 [3939], [4283] |
| screen 1, columns 15..19 | the sea | 4644 [2435]; 4637 [3818] |
| screen 2, columns 0..11 | the sea | 4644 [2598] (Bail), [2757], [2873] |
| screen 2, columns 12..19 | the woodland: UNVERIFIED (no crash recorded there; the screen's houses start at column 12) | - |
| screen 3 | the woodland | 4644 [3062], [3361] |

The wiki agrees: "If the collision took place while floating above the sea... a
stretch of ocean surrounded by sharks", "above land, and too much distance has
already been crossed... a small patch of woodland". A crashing press changes no
counter.

| site | frame |
|---|---|
| over Entrana | T panels shut under an instant black; T+1 unlock + line + move + lift; the overlay shut a tick after the continue |
| sea (the real map, 1803,4890; Auguste and 3 sharks are world spawns) | T+1 unlock + panels; T+3 move, side panels shut again, `zep_balloon_crash`, lift, line. Auguste's 7 pages ([2478..2485]). The plank: a line on the op tick, then on its continue the shore (2867,3368), panels back, second line |
| woodland (a copy of m28_76 per crash) | T+1 unlock; T+3 copy, facing Auguste (`zep_piccard_crashland`), lift; T+4 line. His 3 pages. The mound: a line, then on its continue a fade; 4 ticks on, 3068,3358, lift, Floating Free, "The crash must have really sent us off course; I'm near Falador!" |

The sea's Auguste wandered on the generated default (`defaultmode=wander`): he now
stands (`configs/enlightenedjourney.npc`, the two lines deleted from
`server_fields.generated.npc`).

### The landing (4644 [3566..3574])

T the panels shut; T+1 unlock, multi_piccard 1, stage 100; T+4 "You arrive safely
in Taverley." + 2941,3420; T+5 panels + lift; T+8 overlay shut.

## Completion (4644 [3685..3701])

Three pages (the second names the player: `<displayname>`), then on the last click
stage 200, the six given-item flags cleared, cap and jacket, the scroll with the
cap at 76,128: "4,000 Firemaking XP|3,000 Farming XP|2,000 Crafting XP|1,500
Woodcutting XP|Balloon Transport Access".

## Journal

Every row as sent at 6 (4637 [2809]), 20 ([2968]), 40 ([3050]), 60 ([3134]), 80
([3488]), 90 ([3656]), 100 (4644 [3610]) and 200 (4643 [661]).
`quest_journal_rows`, so no rewrap. UNVERIFIED: 5, 10 and 70 (no recording opens
them; their current step is ours).

## Other interactions the sessions exercise

| action | verdict |
|---|---|
| papyrus on wool, candle on the frame ([2951], [2955]) | fixed: Jagex's lines, `zep_making_balloon`, `zep_construct` |
| a sack on a sand pit ×8 ([3289..3306]) | fixed: "You fill the bag with sand." (spam), `zep_fill_sandbag`, `sand_bucket` |
| branches on the basket ([3500], [3506]) | fixed: "You do not have enough willow branches.", Auguste's page, the scene |
| Fly on Auguste before the end ([2975], [3079]; 4641 [29]) | fixed: "You should speak to Auguste first." |
| Fly on either crashed Auguste (4637 [3956]) | default ("Nothing interesting happens.") |
| the Port Sarim monk, Talk ([2692] etc.) | ours already matched; the test now clicks "All is satisfactory." |
| the willow at a tree patch, the gardener paid in apples (4644 farm run) | generic farming; the test now pays the gardener, as Auguste says |
| `zep_multi_storage_entrana`, the logs-preference prompt (4637 [3611]; 4644 [2386], [3268]) | OPEN: no storage crate in ours |
| glory, tablets, spells, banks, GE, trade, trees, the farm run | generic, not this quest |

## Forgettable Tale: the Kelda hops (4637)

Fixed with this port. Planting ([2212..2217]): the op on L+1, the dibbing + seeds
gone + synth on L+2, then on L+5 "You plant 4 seeds in the hops patch." (spam),
the stage and 9 xp. Growth on the farming clock: a stage on every five-minute
mark after planting ([2392], [2879], [3403], [3884]: ~500 ticks apart, the first
105 s on), and with the last one "Perhaps I should take a look and see if my
Kelda hops have grown..." wherever the player is (Entrana, there). Was: four
minutes a stage from the planting, no line, the plant line our own. Leg 2 passes
27/27 with a check for the line.

## Open

- Log storage crates and the "which logs first" prompt: not built; flights take
  inventory logs only.
- Npc info not audited block by block (`npc_block_inventory.py`): the Entrana
  Auguste's walks, the cast's facings beyond what the scene draft gave, the
  sharks' movement.
- Area music after the jingle comes back 11 ticks after the launch in ours, 22 in
  4637 [3023] (the engine's jingle length for 116).
- "You have unlocked a new music track: Floating Free" (4637 [3619]) is not sent.
- zep_login_bool, zep_return_check, zep_sapling_check, zep_sapling_patch are
  never written.
- Taverley's Auguste at stage 100 is talked to by the test; the recorder went
  back to Entrana. The Taverley talk is unrecorded.
- The completion still opens Castle Wars, the Gnome Stronghold, the Crafting
  Guild and Varrock (multi_cast/gno/craft/varr = 1). 4644's completion writes none
  of these: each route opens by flying it. The network flights are still
  teleports.
- A press with no sandbags or logs left does nothing: UNVERIFIED.
- The overlay's close after the basket scene and after the woodland exit is past
  the windows read.
