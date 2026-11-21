# Garden of Tranquillity vs RSProx index 4635

Recording: index 4635, rev 242.1, world 411, 2026-11-21, "Garden of Tranquility" —
the whole quest, stage 0 → 60, ticks 0..7894 (113,666 transcript lines).
Transcripts: `~/.rsprox-index-cache/home-{default,bas,raw}/.rsprox/binary/archive/idx4635.txt`.
No other session records the quest (`search varbit 961`: 4635 plus three
seasonal sessions with one stray write each).

Before this pass the content was a Quest Helper / wiki reconstruction: no page
of the 2,362 lines matched the recording, the Wise Old Man's quiz was a chain of
chat menus, the trolley came back after every statue, the Falador scene was "not
built here", the finale was eight invented lines with the rewards on the last,
and the palace patches grew on a private 4-minute clock.

Content: `OSRS-Content/osrs-content/server/scripts/quests/quest_gardenoftranquility/`
(`garden_shared.rs2` Ellamaria, the list, growth, Roald, completion;
`garden_wom.rs2`; `garden_elstan.rs2` the gardener menu, Elstan, Dantaera, the
White Tree; `garden_lyra.rs2`; `garden_kragen.rs2`; `garden_althric.rs2`;
`garden_bernald.rs2` Bernald, Alain, the vines, the strong cure;
`garden_finalgarden.rs2` the palace patches; `garden_statues.rs2` the trolley;
`garden_cutscenes.rs2`; `garden_journal.rs2`). Hooks outside it:
`areas/varrock/scripts/king_roald.rs2` (the menu's middle option),
`skill_farming/scripts/farming_growth.rs2` (`~garden_allot_grew` after an
allotment step), `skill_farming/scripts/farming_gardener.rs2` (Lyra/Kragen refuse
to protect their own crop), `quests/quest_fairytale1/scripts/ft1_martin.rs2`
(Lyra's patch question and names).

## 1. Stages and vars (all on garden_varp_1 / garden_varp_2: `perm`, `transmit=yes`)

| var | value | tick | with |
|---|---|---|---|
| garden_quest 961 | 10 | [200] | "Yes." — the line "You've started a new quest", the list, the player's page |
| | 20 | [363] | the Wise Old Man's "Go and get it" page |
| | 30 | [515] | the seventh quiz answer |
| | 40 | [524] | the opened ring's objbox |
| | 50 | [7456] | the player's "Oh, you want me to go and get Roald" page |
| | 60 | [7835] | the click of Ellamaria's last page after the tour |
| elstan 967 | 1 / 2 / 3 / 4 | [810] / [949] / [5545] / [5557] | the deal / marigold planted in farming_flower_patch_1 / picked / the seeds' objbox |
| dantaera 976 | 1 / 2 | [1105] / [1328] | "Do so with my blessing" / the shoot cut |
| lyra 968 | 1 / 2 / 3 | [1826] / [5380] / [5701] | the deal / the farming tick the onions finish / the seeds |
| kragen 971 | 1 / 2 / 3 | [2187] / [5380] / [5468] | the same, cabbages |
| patch_8 / patch_7 / patch_5 | 1 | [1863] / [1919] / [2245] | the quest crop planted in that allotment |
| althric 977 | 1 / 2 | [2353] / [2463] | the evil sensed / his leave (ring not carried) |
| ring_in_well 966 | 1 / 0 | [2425] / [2725] | the splash / fished out |
| bernald 988 | 1..5 | [2971] [3145] [3365] [3664] [3674] | deal, plant cure, Alain's answer, cured, seeds |
| palace patches | 1..3 rake, 4 planted, +1 per farming tick | [3954..] | the shared 5-minute tick: [4385] [4883] [5380] [5878] [6379] [6884] [7384] |
| trolley 965 / statues 963, 964 | load, reset, place | [4173] [4251] [4570] [4756] [4775] [5129] | §6 |
| cutscene_billybob 962 | 1 | [4742] | after the scene |
| white_tree 985 | 9 | [7890] | the first fruit, on the farming tick after completion |

Ours: every value and tick above, from the same triggers. The palace clock is now
the 5-minute boundary (`~garden_next_farm_tick`), shared by the potted shoot.
Lyra's and Kragen's stage 2 is written from `farming_advance_allot` on the step
their crop finishes (was: on talking to them).

## 2. Actions inventory (rsprox_actions.py, every row)

| action(s) | verdict |
|---|---|
| queen_ellamaria op1 ×21 | **this content**: §3 |
| wom_multi op1 ×4, garden_quiz 133 buttons ×7 | **this content**: §3 (if_addresumebutton + p_pausebutton; ours also sends IF_SETEVENTS on the three buttons, Jagex does not) |
| elstan op1 ×11, farming_flower_patch_1 ops/use ×6 | **this content** (Elstan); patch: generic farming, its own audit |
| dantaera op1 ×13, use sapling on her ×1 ("Nothing interesting happens.") | **this content** |
| garden_white_tree_dead op6/use secateurs | **this content** (examine: cache) |
| shoot on plantpot_compost ×2 (no trowel / trowel) | **this content**: both lines recorded |
| watering_can on potted shoot, op10 examine | **this content** |
| lyra op1 ×16, op3/op4 ×3 | **this content**; the pay ops' "nothing planted"/"fully grown" lines are farming_gardener.rs2's |
| kragen op1 ×13, op3 ×3 | **this content** |
| farming_veg_patch_5/7/8 ops/use, leprechaun notes | generic farming (not re-audited) |
| garden_roses_red/pink/white op1 ×14 | **this content** |
| brother_althric op1 ×8 | **this content** |
| well: ring ×1, op6, bucket ×1, fishing_rod ×2 | **this content** (bucket: generic `fill_bucket_well`) |
| bernald op1 ×12, vines + secateurs/plant cure/strong cure | **this content** |
| crate3 op1 [2994] | not this content (Burthorpe prop; nothing sent) |
| farming_gardener_tree_1 (Alain) op1 ×3, op3 ×1 | **this content**; op3 generic |
| hammer/pestle/rune shards/rune dust/plant cure use-ons | **this content** (pestle on essence: "too big to grind", grind_ingredient.rs2) |
| garden_* palace patches/pots ops/uses ×~40 | **this content**: §5 |
| garden_trolley_obj on statues ×6, garden_trolley op1 ×228, op3 ×8, op4 ×15, op5 ×4, op6 | **this content**: §6 |
| garden_list op2 ×10, destroy ×1 | **this content** (interface garden_list 132) |
| king_roald op1 ×7 | **this content** (+ the Shield of Arrav lamp page [7512]: not this content) |
| quest_rating, soa_lamp, Evil Bob random event [6505..6592] | not this content |
| sanfew, duke_of_lumbridge, Olivia, Vanessa, bankers, GE clerk, shops | generic, not re-audited this pass |
| banks, GE, shops, equipping, eating, drinking, dropping, stamina | the recorder's kit / generic |
| teleports: varrock/falador/lumbridge/ardougne spells, Catherby tablet, ectophial, glory (rub/worn), games necklace, dueling ring, spiral stairs | generic (§2b audits elsewhere); not re-audited this pass |
| trolley fades [4424..4431], [4984..4991] | **this content** (§6) |
| copies [4604..4739], [7622..7797] | **this content** (§7) |

## 3. Pages

Every page the recording shows is in the scripts verbatim with its recorded mood
(the recorder took nearly every wrong branch, so each npc's whole charm tree is
recorded): Ellamaria start (32 pages incl. the "No." branch), stage-40 menu (all
four options), the inspection (not grown / grown), stage 50, the after-quest
menu; the Wise Old Man (stage 10, 20, the verdict); Elstan, Dantaera, Lyra and
Kragen (every branch, the hand-outs, the "more seeds" lines); Brother Althric;
Bernald (states 0..5) and Alain (both branches); Roald (both refusals and the
charm); both scenes; the completion pages. `[Charm]` options are offered only
with the ring worn (Elstan [607] vs [680], Dantaera [994]).

From the wiki (Transcript:Garden of Tranquillity, and the quest and Talk pages),
moods UNVERIFIED — no recording other than 4635 talks to any of these npcs:
requirements not met; the ring lost / not on the player; the failed quiz and its
two answers; Althric's "Can I have some rosebush seeds, please?"; "Can I have
another list?"; Elstan's "Have it your own way, mate. No seeds for you then.";
Lyra's and Kragen's "What did you ask me to do?" answers; the "no room" lines of
Elstan, Kragen, Lyra and Bernald; Dantaera's lost-cutting exchange (the cut is
offered again); Althric's "You don't have enough inventory space to do that.";
Saradomin's plinth; the 5th..9th checklist openings. The wiki has no reply to
"That's all, thanks." and none to Lyra's "psychopath"/"withdraw" lines (ours:
none, as recorded). Still ours, no source at all: the checklist names past the
four recorded (vines, white/pink/red roses, White Tree) and Inspect on the five
unrecorded patches (the wiki has no inspect line for them either).

## 4. The quiz

Questions and answers as sent [433..508]. Recorder: A, A, C, C, B, C, C. Scored
against the wiki quest page's table (Q1 colours, Q2 the kebab, Q3 "It's
absolutely...", Q4 the helmet, Q5 "You of course Pkmaster0036...", Q6 "Ask me
nicely...", plus "I'll do whatever you ask - I just love the monarchy!" on Q6
per Talk:Garden of Tranquillity, 2016), with Q7 free (the quest page: "the
seventh question can be answered in any way"): the recorder scored 4 of 6 and
passed "close" ([518]). So the quest page's "answer six correctly" is not the
rule. Ours: pass at 4 of the first 6. UNVERIFIED: where a fail starts. No other
session opens interface 133, and none of the 228 sessions that see any of the
quest's npcs talks to one (searched 2026-11-21). The order is the recorded one
(the wiki says a retake shuffles it).

## 5. The palace garden

| interaction | recorded | ours |
|---|---|---|
| rake | anim every 4 ticks; a weed + stage on the sweeps that roll it (21 of 28 here) [3934..3962] | `~farming_rake_sweep` (§10) |
| plant | dib anim + seeds gone on O, stage + spam "You plant 4 seeds in the <x> patch." O+3 | same |
| White Tree | human_dig O+1, pot back + stage + spam O+2 | same |
| wrong seed | "This seed can't be planted in this patch." | same |
| water | mesbox "These don't really need watering ..." | same |
| compost on a planted pot | "That doesn't work here." | same (every palace patch) |
| orchid pots | compost first (bucket back), plantpot_compost refused, other pot's seed refused, 3 seeds | same, all five lines |
| Inspect (op2) | Ellamaria's chathead: pink/yellow orchids, the king's plinth | same; the rest UNVERIFIED |
| growth | all patches +1 on the shared 5-minute tick | same (`~garden_palace_catchup`) |

## 6. The trolley

| step | recorded | ours |
|---|---|---|
| load | O pickup anim; O+2 item gone, gt_statue, empty trolley added; O+8 push anim; O+9 statue/trolley varbits + hint arrow | same |
| Push / Pull / Big-push | push anim + gt_cart_push (delay 10) O, a tile O+1 (Big-push: 4, one a tick) | same; the player glides after it |
| the king's jump | trolley onto 3243,3226: fade; +5 player 3213,3500, trolley 3214,3500; +7 overlay shut | same |
| Saradomin's jump | trolley onto 2966,3392 | same (x 2962..2970) |
| wrong way out of Falador | player page "I don't think that's the quickest way ..." [4933] | trolley pushed south of z 3379 (fitted to one stop) |
| place | next to the plinth: anim O, gt_statue + varbits O+2, pickup O+3, line + trolley gone O+4; trolley NOT returned | same |
| place elsewhere | garden square: mesbox "You need to move the trolley next to a plinth ..."; Lumbridge: player page "I'm not sure I should be putting this statue here ..." | same |
| teleport away | trolley gone, "The guards have recaptured the statue of <x>, and placed it back in <y>." | softtimer: no trolley within 16 tiles |
| the queen statue | player page "...that looks like a QUEEN to me." | same |
| a second trolley | from Ellamaria's "How am I supposed ..." | same |

## 7. Cutscenes

| scene | session | T..E | start | copy | cast | camera | pages | stage | verdict |
|---|---|---|---|---|---|---|---|---|---|
| Billy and Bob | 4635 | [4601..4756] | trolley on Falador's Saradomin, billybob 0 | yes: 2965,3380 → 10508,11021 (squares 46,52..53) | garden_cutscene_guard_1/2 at (20,68)/(23,68) facing north, garden_cutscene_aggressor at (22,82) | 26 cam lines copied (232 jumps, 5/20 and 10/10 pans) | 21 timed pages + overheads in pieces, 4 timed mesboxes | billybob 1 [4742], then the load | ported (`[queue,garden_billybob]`) |
| The garden | 4635 | [7619..7835] | Roald's last page | yes: 3223,3474 → 11086,13917 (square 50,54) | king_roald_cutscene (30,38), queen_ellamaria (31,25) | 40 cam lines copied | 30 timed pages + overheads, then 4 world pages | 60 on the last click [7835] | ported (`[queue,garden_finale]`) |
| trolley fades | 4635 | [4424..4431], [4984..4991] | the trolley onto the jump tile | no | — | — | — | — | ported (`~garden_trolley_jump`) |

Frame: Misthalin Mystery's (areasounds 100), plus silence and the scene jingle at
the lock. **Open**: the player's walk back to the statue after the first scene
([4742..4746]) is not played (the load runs from where the scene puts them);
the scene npcs' `[facing]`/`[sequence]` per tick not yet diffed against a tick
log (§9).

## 8. Journal

Every recorded opening's rows are in `garden_journal.rs2` row for row: stage 10
[242] = 20 [400]; 40 nothing grown [533], ring in the well [2628], statues placed
[5174], vines/roses/tree grown [6212], delphiniums/snowdrops grown [7352],
everything [7447]; 50 [7463]. Not started is the overview [101]. UNVERIFIED: 30
(10's rows) and 60 (no recording opens it complete).

## 9. Wire audit (gate run got_r6, 2026-11-21: 395/395 PASS)

`npc_block_inventory.py idx4635 --ours ticklog.tsv --match 'garden_|king_roald_cutscene|queen_ellamaria'`:

| npc | kind | recorded | ours | verdict |
|---|---|---|---|---|
| garden_cutscene_guard_1 | say / sequence / facing | 12 / 3 / 1 | 12 / 3 / 1 | matched, text for text |
| garden_cutscene_guard_2 | say / sequence / facing | 20 / 1 / 1 | 20 / 1 / 1 | matched |
| garden_cutscene_aggressor | facing | 1 | 1 | matched |
| king_roald_cutscene | say / sequence / hit / headbar | 20 / 2 / 1 / 1 | 20 / 2 / 1 / 1 | matched (the slap's 1 [7736]) |
| king_roald_cutscene | facing | 10 | 21 (run 7) | held on the player from [7624] and again from [7651] (`npc_setmode(playerface)`, his leash raised to 40 in gardenoftranquility.npc), the coord look [7650] and the reset [7730] as recorded. The count differs: our encoder re-sends the held face on every tick he walks, Jagex only on some (7666-7668, 7684-7685, 7696, with a changing fallback angle). Engine encoder rule, not content: open |
| queen_ellamaria | say / sequence | 17 / 2 | 17 / 2 | matched |
| queen_ellamaria | facing | 22 | 10 | mostly the engine's dialogue faces over 20 recorded lives; the scene's own are sent |
| garden_trolley | facing | 4 (`type=reset` on the first move after each of 4 loads) | 2 (run 7: the test loads twice) | matched per load |
| garden_trolley | movement | 127 one-tile `teleport` glides, 2 telejumps | glides + 2 telejumps | matched in kind |

Player seqs and synths by name (ours / recorded): garden_cut_shoot 1/1,
garden_picking 3/3, garden_statue_push_for_cutscene 1/1, human_fish_onspot 6/6,
human_fishing_casting 1/1, pool_plop 1/1, smash_gem 1/1, grind 1/1,
gt_grind_rune 1/1, herbing grind/vial 1/1, farming_plant_cure + farming_plantcure
2/2. Different only by what the test does: garden_statue_push 37/101 and
gt_cart_push 33/95 (the recorder pushed one tile at a time, the test big-pushes),
human_dig 1/12 (the recorder dug another allotment), pruning_mid_low 0/1 (the test
never prunes the vines), gt_statue 4/6 (two recorded resets reloaded).

Both scenes: `t.cutscene.await` rows pass (Billy and Bob: moveto 600 / lookat 25
first, reset last; the garden: moveto 2625 / lookat 250, reset).

## 10. Generic content this session uses

| content | recorded | ours | verdict |
|---|---|---|---|
| raking (every patch kind) | 4-tick sweep; a weed per sweep on a level roll: 72 sessions, 402 sweeps, 27% at Farming 1, 73% at 64, 96% at 95 | was 5 ticks, always; now 4 ticks and `stat_random(farming, 68, 254)` (MLE fit) in all 8 raking copies | **fixed** |
| Varrock/Falador/Lumbridge/Ardougne Teleport | 18 landings | tele_coord ± 2 (3213,3424 / 2965,3378 / 3221,3218 / 2661,3301) | all inside |
| Catherby lunar tablet | 3 landings 2800..2802, 3449..3450 | 2801,3449 ± 1 | inside |
| ectophial | 3660..3661, 3522; shape O / O+3 / O+4 / O+5 | Ghosts Ahoy's (4513) | matched |
| glory worn Rub ops | op 2 Edgeville 3087,3496, op 4 Draynor 3105,3251 | same | matched |
| games necklace Burthorpe | 34 cached landings fill 2896..2900 x 3551..3555 | was 2899,3553 r3; now 2898,3553 r2 | **fixed** |
| ring of dueling Castle Wars | 2440,3090 | 2440,3090 r2 | inside |
| watering can at a fountain | spam "You fill the watering can from the fountain.", human_pickuptable, tap_fill [1468] | a `_watersource` fountain refused cans; `fountain`/`sink` said "You fill the watering can." | **fixed** (both) |
| allotment Inspect beside a grown protection flower | "...is at state 4/5." then "Your rosemary plant is protecting these crops from disease." [5264] | first line only | **fixed** |
| Lyra/Kragen crop finished | "Perhaps I should check the cabbages/onions that I planted for Kragen/Lyra..." on the farming tick [5380] | nothing | **fixed** |
| Falador noticeboard (Read) | messagescroll, 15 rows [1168..1170] | unbound | **fixed** (`areas/falador/scripts/falador_noticeboard.rs2`) |
| every other farming line (planting, compost, harvest, clearing, leprechaun notes) | 4635 | present | matched by text |
| Draynor market guard op2 [855] | nothing sent before the recorder moved off | unbound | nothing to copy |
| Evil Bob random event [6505..6592] | the whole event | **not implemented** (no `macro_evil_bob_*` content) | open |
| banks, GE, shops | generic | bound; exercised by their own tests | not re-diffed here |

## 11. Open

- Roald's face re-send count (§9): the encoder re-sends a held face per moving tick.
- Quiz fail threshold (§4): needs a recording that fails.
- The wiki-sourced branches' moods (§3).
- Evil Bob's random event (§10): a port of its own, from [6505..6592].
- Camera lines not diffed packet for packet (`TORIRSSERVER_TRACE_OUT=1`); the
  scene bodies copy every recorded `cam_*` line and tick.

Test notes (`test/quests/gardenoftranquility.lua`): the trolley pushes are graded
(the quest driver's `press` now names the only copy in the pool when `world_op`
reports no element: script/plugins/quest_driver/pointer.lua); the allotments are
cured after each growth wait (Lyra and Kragen refuse to protect them, and an
uncured disease killed the onions once); the raking waits allow for the roll.
