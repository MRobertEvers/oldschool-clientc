# The Shades of Mort'ton — RSProx index 4467..4472

Recorded 2026-11-06 by the owner (display name in the transcripts: LoveTori), rev 242.1:

| index | world | ticks | label | content |
|---|---|---|---|---|
| 4471 | 329 | 2344 | "Shades of Morton start" | bank/GE buying, Mort'ton scroll, stage 0 -> 50: the diary, the start, the table, the serum, both cures, five Loar kills, Razmire's and Ulsquire's talks, the shops, the journal at 0/5/15/45/50 |
| 4469 | 377 | 1117 | "Finishing Shades of Morton - Serum 208 on npcs" | stage 50 -> 85: wall repair, the altar, sacred oil, pyre logs, the pyre, the completion, Serum 208 on Ulsquire, Razmire and two locals, the journal at 85, the shelf again, glory + Varrock tablet, the Apothecary's diary trade |
| 4470 | 377 | 113 | "Shades of Morton temple repair" | stage 85: 22 wall repairs, the altar refused at 1% sanctity |
| 4467 | 378 | 97 | "Morton Template repair briefly straight wall" | stage 85: the scroll, a broken wall taken to stage 1 and knocked down by a shade |
| 4472 | 377 | 954 | "Nothing" | Varrock tablet out of Mort'ton, the bank, two other quests' overviews |
| 4468 | 377 | 25 | "Nothing" | login, world switch |

These are the only recordings: `search varp 339` finds 4469 and 4471 (and rsprox.net 1846, a
Leagues V login at stage 0).

**Transcribing rev 242.** The index's stock transcriber (`~/rsprox-index-data/rsprox.classpath`,
upstream 323d3229) has no `osrs-242` decoder and fails "RevisionKey(revision=242...) is missing".
Upstream f1081e91 (RSProx 8c514346 "Revision 242") plus the two `local-indexer-fixes` commits is
built in `~/rsprox-index-data/rsprox-upstream-builds/w242` (worktree of rsprox-upstream, branch
`local-indexer-fixes-242`, JDK 21); its classpath is `~/rsprox-index-data/rsprox-242.classpath`.
The rev-242 cache A5AB2044... was in `~/rsprox-index-data/caches` only and is now linked into
`~/.rsprox/caches` (dictionary entry added). `RSPROX_CLASSPATH=~/rsprox-index-data/rsprox-242.classpath
python3 tools/rsprox/rsprox_index.py get <id> [--bas]`.

**Ours:** a LostCity port (§16): `quests/quest_mortton/` (Razmire, Ulsquire, the afflicted, the
diary, the journal) and `minigames/game_mortton/` (the temple, the pyre, the shades, the
catacombs). Its pages were already OSRS text (all but six matched verbatim); what was 2004 or
guessed was the boxes, the start, the bits' varp, the flame, the lighting, the repair cadence,
the completion and the journal.

## Actions (rsprox_actions.py) — a verdict per row

4471:

| first tick | action | verdict |
|---|---|---|
| 3, 8..247 | welcome screen, bankers, bank tabs/withdraws, GE clerk + offer for tarrominvial, skill guide | generic / the recorder's errands |
| 131, 148, 308, 432, 478, 482 | take ashes, mixed hide set, smashed glass, empty vials, unidentified guam, bones | generic ground items |
| 274 | teleportscroll_mortton Read | **fixed**: no binding existed; added to `teleport_scroll.rs2` (shape 4471 [274..277], 4467 [8..11]) |
| 280, 1042 | prayer potion / stamina combine | generic decanting |
| 284.. | Protect from Melee | generic prayer (the shades' hits read it: 10/10 prayed zero) |
| 291.., 335.. | poordoor / poordooropen (x20 / x13) | generic door (`door_close` 60 recorded); not re-diffed |
| 300.., 1537.. | attack shade_level1 / shadeshadow_level1 | **this content**: the shades (below) |
| 317, 449, 459 | attack / talk Afflicted villagers | this content: gibberish talk (pages); attacking them generic combat |
| 317..423 | dead trees (deadtree1..4) op1 x21 | walk clicks: no answer on the wire to any of them |
| 322, 1031 | stamina potion, lobsters | the recorder's kit |
| 331, 386, 1983 | talk to afflicted Ulsquire / Razmire | **this content**: gibberish + the blue line (pages) |
| 355, 379 | crate3 ("I can't reach that!"), crate_old ("You search the crate but find nothing.") | generic search; no Mort'ton content |
| 401, 638, 408 | quest list op / journal / overview close | **this content**: the journal (below) |
| 441, 1044, 2296..2309 | drops | generic |
| 481, 485, 1093, 1094 | identify guam, bury bones, identify purse and tarromin | generic |
| 486, 992 | shades_experimentshelf | **fixed**: objbox with the book (was a mesbox) |
| 494, 946 | serum_book Read, book close | **fixed**: the start menu on the first Read (below) |
| 958 | tarrominvial on afflicted Razmire | "Nothing interesting happens." — ours falls to the default (matched by shape, not run) |
| 1046, 1082 | experiment table op5 (full pack), op6 | **fixed**: objbox (tarromin) / doubleobjbox (tarromin, rogue's purse) |
| 1314 | ashes on tarrominvial | **fixed**: the brew's "You make serum 207." only; our extra line deleted |
| 1372, 1736, 1878, 1996, 2227 | serums on Razmire / Ulsquire | **fixed**: mood, the oil_pour synth, the bits on varp 340 |
| 1498, 1904, 2241, 2277 | talk to Razmire, his two stores | this content (pages). General store: all 15 stocks and counts match [2241]. Builders' store: **fixed**, limestone was missing and the order differed; the 10 wooden planks [2277] are added, labelled (base stock or a player's sale) |
| 1564 | take shade_bones1 x6 | generic take of the shade's drop |
| 2181, 2186 | vial on mortton_sink, tarromin on vial of water | generic water source (`water_sources.rs2`) and herblore |
| 2254..2335 | shop buys: olive oil, Flamtaer hammer, timber beam, limestone brick; swamp paste sold | generic shop |
| 2343 | logout | — |

4469:

| first tick | action | verdict |
|---|---|---|
| 7, 210, 839 | talk to afflicted Razmire / Ulsquire | this content (pages) |
| 36, 680, 709 | templefire_altar_nofire, templewallcorner_base / _6 | **fixed**: the lighting (below) |
| 45 | attack shade_level1 x10 | this content: the shades |
| 74.. | templewall_10 op1 x12 | **fixed**: the repair cadence (below) |
| 113, 160, 746, 763 | templefire_altar op1 x5 | **fixed**: op1 had NO binding; now sanctifies oil, then serum |
| 216, 799, 849 | Serum 207 / 208 on Ulsquire, Razmire | **fixed**: objbox coins (Razmire's line has no full stop) |
| 245 | sacred oil on oak logs | matched (line, oil_pour, stage 70; not run on our side) |
| 279, 208 | doors | generic |
| 296, 303, 306 | pyre: logs, remains, light | **fixed**: shade_sigh, the shade's rise tick, no smokepuff synth (below) |
| 316 | take shadekey_bronze_bloodred | generic take of the private drop (despawn 500, neverturnpublic) |
| 333, 461 | quest list / journal | the completed journal (below) |
| 708, 781 | prayer, prayer potion | the recorder's kit |
| 899, 948 | Serum 208 on mort_afflicted_man2 x2 | **fixed**: the gift as an objbox of the item on the page's click |
| 905 | talk to mort_man2 | matched page |
| 992, 1003 | shelf (already have the book: objbox), Read | matched |
| 1017 | glory (worn) Edgeville | generic jewellery, audited 2026-10-29 |
| 1025..1035 | bank | generic |
| 1038 | Varrock tablet | generic tablet, audited 2026-10-29 |
| 1067, 1086 | Apothecary talk, serum book on him | **fixed**: the two objboxes (book) |

4470, 4467, 4472, 4468: wall repairs and the altar (as 4469), the scroll (as 4471), the Varrock
tablet, the bank, world switches, other quests' overviews.

**Teleports:** 4471 [277] scroll 3162,3487 -> 3488,3288; 4467 [11] 3191,3411 -> 3489,3288; 4469
[1020] glory -> 3087,3496, [1042] Varrock tablet -> 3212,3425; 4472 [21] tablet -> 3213,3423. The
scroll lands in the 3x3 round 3488,3288 (two landings, radius 1 as the other scrolls).

## Stages and vars

| stage | goes out | tick | ours |
|---|---|---|---|
| 0 -> 5 | the start menu's "Yes." | 4471 [497] | **fixed** (was: the diary's last spread) |
| 5 -> 10 | the first serum brewed | [1315] | matched |
| 10 -> 15 | "Yes, I'll dispatch those dark and evil creatures." | [1516] | matched |
| 15 -> 20..40 | each Loar kill, death + 3, with "That's one Shade!" .. | [1563], [1608], [1646], [1676], [1725] | matched (message text per kill not all diffed) |
| 40 -> 45 | "Yes I have actually!"'s click, with "Razmire takes two Shade remains." | [1886] | matched |
| 45 -> 47 | Ulsquire's "Oh yes, that would be interesting!" click, with the remains objbox | [2015] | matched |
| 47 -> 50 | Ulsquire's "rebuild" page | [2117] | matched |
| 50 -> 55 -> 60 | the first repair (both in one tick: the shared temple was already 100%) | 4469 [78] | matched |
| 60 -> 65 | the oil sanctified | [160] | matched |
| 65 -> 70 | sacred oil on logs | [245] | matched |
| 70 -> 75 | logs on the pyre | [297] | matched |
| 75 -> 80 | the pyre burns (O+3) | [309] | matched |
| 80 -> 85 | the click after "Great! Well done my friend!" | [355] | **fixed**: was a queue after the page |

**varp 340 `morttonmulti`.** The quest's gap bits go out on varp 340, not on a server-only varp:

| bit | meaning | recorded |
|---|---|---|
| 0 | serum used on Ulsquire | 4471 [1736] 262 -> 263 |
| 1 | Ulsquire cured | [1736] 260 -> 262; cleared [1951], [2197] (~200 ticks) |
| 2 | serum used on Razmire | [1372] 280 -> 284 |
| 3 | Razmire cured | [1372] 272 -> 280; cleared [1573] (201 ticks), [2144] |
| 4 | `shadeattack` (varbit 201) | 278 / 393 / 419 / 528 ... |
| 5 | Serum 208 on Ulsquire | 4469 [805] +32 |
| 6 | Serum 208 on Razmire | [852] +64 |
| 7 | `made_perm_serum` (varbit 5685) | [764] |
| 8 | the table searched | 4471 [1084] 16 -> 272 |
| 9..13, 14..27 | `pyre_loc`, `temple_resources` | throughout |

Ours kept bits 0..8 and 31 in `%varp7427_mortton_runtime` (`transmit=no`): **fixed**, they are
on `%varp340_morttonmulti` (declared `wholewrite=allow`; every write is a setbit/clearbit), bit
7 as `%varb5685_made_perm_serum`. Bits 28..30 (oil given, lair unlocked, diary traded) stay in
7427: no recording shows them, and 28 is the cache's `shades_dampe_intro`. A save made before
this change keeps its cures and flags in 7427 and loses them (no migration).

Also recorded and not ours: varbits `last_quest_id` (16741), `last_started_quest_id` (16760),
`last_completed_quest_id` (16767) toggled on every stage write (engine-level, no quest sends
them yet; Open, all quests).

## The start (4471 [486..497])

The shelf: L+1 objbox "You find an interesting looking book on the shelf." with the book (193).
The first Read: on the click `chatmenu` "Start the Shades of Mort'ton quest?" "Yes.|No."; on
"Yes." stage 5, "You've started a new quest: @q_mes@Shades of Mort'ton</col>", `human_readbook`
and the book's first spread ("Diary of Herbi Flax", page 11). Every spread of 4471 [497..908]
matches `serum_book.rs2` row for row (rsprox_book.py).

## Pages (rsprox_pages.py, all six sessions)

Checked mechanically: every recorded page's text (`<br>` -> space) against our string literals,
and the box kind and mood against the call.

| recorded | ours before | fix |
|---|---|---|
| "Ah, excellent, you've made your own serum..." chatneu3 (Razmire [1372], Ulsquire [1736]) | happy | neutral |
| "Great, that's what I wanted to hear. When you've done it, bring back all the remains, I want..." chatneu2 | happy, two spaces after "remains," | neutral, one space |
| "Yes I have actually!" | "Yes, I have actually!" | fixed |
| "How can we do that? You can't even bury the remains!" | "How do we do that?" | fixed |
| "...limestone bricks, swamp paste and timber beams." | "wooden planks" (a LostCity-era guess) | fixed |
| "Razmire takes two Shade remains." objbox (shade_bones1) | mesbox | objbox |
| "You show the shade remains to the priest. He wanders off to study the bones." objbox | mesbox | objbox |
| "Razmire gives you 186 gold coins" (no stop) / "Ulsquire gives you 183 gold coins." objbox (coins) | mesbox, stop on both | fixed |
| "The local gives you some herbs." / "...some olive oil." objbox of the gift, on the page's click | mesbox, queued | objbox, inline (every gift line) |
| "You find a selection of herbs." doubleobjbox | mesbox | fixed |
| "You see three herbs inside, but you don't have enough inventory space to take them." objbox | mesbox | fixed |
| "You show the Serum book to the Apothecary." / "The apothecary trains you in Herblore." objbox (book) | mesbox | fixed |
| "You sanctify serum 207 in the sacred flame and rename it to serum 208." objbox (`mort_serum3`, whatever the dose) | "...and turn it into Serum 208." mesbox | fixed |
| "You haven't sanctified enough of the temple to allow you light the fire. ... 10% sanctity ..." | "to light", "Sanctity" | fixed |
| "You need to increase your sanctity to at least 20% before you can use the sacred flame for this purpose. ..." | "to %20 ..." | fixed |
| "The holy fire altar appears sanctified. It can now be lit!" | "The holy fire appears sanctified, it can now be lit!" | fixed |

The afflicted's gibberish is generated (`~generate_afflicted_speech`, the recorded words gurghl,
pirsl, sfriess, ... are its vocabulary) followed by the blue "~ This person doesn't make any sense
at all. ~" (chatshock1). The Apothecary's own menu (the Romeo & Juliet npc) is not this quest's.

Pages no recording reaches stay LostCity's, UNVERIFIED: Razmire's "Great, I'll experiment on
these!..." after the remains (the recorder closed that objbox by opening the journal), the
"No, not yet!" branch, every stage-55..80 reply, Ulsquire's olive oil.

## The journal

Recorded at stage 0 (overview, 4471 [401]: start text "searching a house in
<col=800000>Mort'ton<col=000080>."), 5 ([633], [971]), 15 ([1524]), 45 ([1896], [1958]), 50
([2150]) and 85 (4469 [453]). `mortton_journal.rs2` is rewritten from them: struck paragraphs
with no blank rows, the current step in 000080/800000, the completed journal row for row ("I  was"
with Jagex's double space, "QUEST COMPLETE" with no "!"). Stages 10, 20..40, 47 and 55..80 keep
LostCity's wording, UNVERIFIED.

## The shades (4471 [1537..1725], 4469)

`rsprox_npcinfo.py --match '^shade'`: shadeshadow_level1 478 lives in 4471 (they wander in and
out of view, 10219 walk steps); on an attack it rises: `[transformation] shade_level1`,
`level_change 40`, `shade_rise`, and (fixed) `sound_area shade_appear` range 10 at its tile
([1538], [1569]; 15/15 rises in 4471, 13 of 24 in 4469's busy temple, where the other
rises' distance from the recorder was not checked against the range). The shade swings `shade_attack` + synth 745 two ticks after the
rise and then every 4 ticks; blocks `shade_block` + 747; dies `shade_sink` + 746, `[del]` 2 ticks
later, the remains (`obj_add_specific` despawn 200, private) and the kill line a tick after
that.

| | recorded | ours |
|---|---|---|
| Loar max | 4 (28 unprayed swings, 8 zeros) | 4 (melee formula, str 30) |
| prayed | 10/10 zero | zero |
| player seq on a landed hit | `human_shade_defence` 19/19 (where the player was not swinging or repairing) | **fixed** (was the generic defend) |
| player seq on a miss | none, 10 of 11 | **fixed** (was the generic defend) |
| spotanim on a landed hit | `shade_attack` | matched |

`shadeattack` (varbit 201) goes 1 on a rise near the player and back to 0 100..170 ticks later:
not built (Open).

## The temple (4469, 4470, 4467)

The walls and the altar are shared world locs; resources (varbit 11), sanctity and the overlay's
three percentages (varps 343..345) are the player's.

**A repair** (4470 [5..8] standing, 4467 [50..53], 4469 [74..78] after a walk): on the op the
`flamtaer_status` overlay and `human_pickuptable` delay 30, the anim re-sent every tick, the
percentages a tick later, the first spend at O+3 and then every 4 ticks (71 recorded spends, none
skipped). A set of materials (1 limestone brick, 5 swamp paste, 1 timber beam) is +800 to the pool
on a spend tick, then the spend. **Fixed**: ours was a 3-tick cadence with a crafting roll that
could skip a spend, and the anim only on spends.

**What a spend costs:** 7 on 20 of 71, otherwise 8..37 (8:1 9:1 11-37:49), at Crafting 34-35 with
the Flamtaer hammer; xp is half the spend. **Fixed**: ours rolled 7..9 / 10..35 / 35..59 (Crafting
50+) bands; now 7, or 8..37 on a roll fitted to 72% at that one level band; the 60..70 band at
99 is the wiki's, UNVERIFIED.

**The wall:** a stage up sends `brick_build` (1768) to the builder (4467 [73]; 4469 [684..724],
where one corner climbed a stage per spend from 1 to 10); a shade knocking it down sends
`wall_destroy` (1769) range 4 ([83]). **Fixed** (both were missing). When a stage goes up is
not identified: 4467 took six spends (125 resources) for stage 1, 4469's corner one spend a
stage, [616..648] seven spends moved nothing. Ours keeps the LostCity build-pool thresholds
(Open).

**The shades attack the walls** as npcs: `shade_attack_wall` seqs and `demolish_1` (2097) range
5 at the walls, all through 4469. Ours simulates it with a player timer. Its invented line "A
Loar shade lashes out at the temple walls!" is deleted (no recording has it). The npc attack
itself is Open.

**The altar:** lit by op1 at L+1 (4469 [151..153]): `templefire_altar` for 99 ticks (four lights,
four 99s) and `strike_and_light` range 2, no roll, no line, no xp. **Fixed** (ours rolled
Firemaking, printed two lines and gave 1600 xp). Below 10% sanctity, the mesbox; below stage 60,
"You need to have repaired or reinforced the walls of the temple before you can light the sacred
flame." ([37]).

**The flame:** op1 (unbound in ours: **fixed**) sanctifies the pack's oil, then its serum, one a
go, four ticks apart ([160] oil, [164] the serum's 20% box), the HIGHEST dose of each first: at
[763] the pack held a 2-dose Serum 207 in slot 2 and a 4-dose in slot 12 (the full inventory at
[1]; slot 2's 3-dose cured Ulsquire at [216]), and slot 12 went. Oil costs 0.9% a dose (11% -> 7%
for 4, ours matched; the highest-dose pick for oil is UNVERIFIED, the recorder held one vial).
Serum 207 (n) becomes Serum 208 (n), 5% a dose, needing 20% (the wiki's Serum 208 page and its
changelog: "5% sanctity per dose, rather than a flat 20%"; [764] the 4-dose, 20% -> 0%).
**Fixed** twice: the LostCity port made every vial perm3 at 15%, and my first pass read [764] as
"3-dose -> perm4" from the objbox's obj and made every vial perm4. The objbox always shows
`mort_serum3` ([764], although the vial was the 4-dose), with `made_perm_serum`, the first time
only. Varp 345 (the overlay's sanctity %) follows the spend through a normal queue: [160] -> [162]
for oil, and for the serum it waits out the objbox, 20 -> 0 on the resume [770]; ours queues
`update_flamtaer_overlay` (delay 1) after each spend (**added**; it waited for the drain timer).
The recorder ran off at [772], so whether the loop goes on to the 2-dose after the box is not
shown; ours does.

## The pyre (4469 [296..320])

Logs at L+1 (stage 75, `pyre_loc` 21, "You put some logs on the pyre.", `human_pickuptable`,
`log_down`); remains on C ("You place the shade's remains on the logs.", `pick`); light on C
("You attempt to light the funeral pyre."); O+3 the fire (`temple_pyre_fire` delay 30 at the
pyre's middle tile), `shade_sigh` range 5 there (**added**), `fire_lit`, stage 80, `pyre_loc` 0,
the seq cleared; O+5 `shade_heaven` added there facing the pillar with `shade_sigh` again
(**fixed**: ours added it at O+4 with no sound); O+7 the key (`obj_add_specific` despawn 500,
private, neverturnpublic) at the pillar, `smokepuff_large` height 96, the pyre back; O+14 the
shade gone by its own lifetime. Ours sent a `smokepuff` synth (not recorded: **deleted**) and
deleted the shade at O+7 (**fixed**: it lives its 9 ticks).

## The completion (4469 [353..355])

"I've put the Shade's spirit to rest!" (neu1), "Great! Well done my friend!" (sad1), and on its
click: stage 85, the quest points, "Congratulations, you've completed a quest: @q_mes@Shades of
Mort'ton</col>", `quest complete 3`, the scroll with `shadekey_silver_purple` at 78,128 and
"3 Quest Points", "2,000 Crafting XP", "2,000 Herblore XP". **Fixed**: the scroll had "2000
Herblore XP|2000 Crafting XP|Access to the Shade Catacombs", and the completion was queued.

## Our wire (test/quests/mortton.lua, 185/185 PASS on a private pack, 2026-11-06)

The pyre from our tick log (`ticklog_tells.py`), aligned on the fire:

| R (4469) | ours | verdict |
|---|---|---|
| [297] logs: human_pickuptable, log_down, the logs loc | 719: the same | matched |
| [303] remains: human_pickuptable, pick, the bones loc | 722: the same | matched |
| [309] temple_pyre_fire 3507,3277 h0 d30, shade_sigh r5 there, fire_lit | 729: the same | matched |
| [311] shade_heaven add at 3507,3277, shade_sigh again | 731 (+2) | matched |
| [313] smokepuff_large 3507,3279 h96, temple_pyre back | 733 (+4) | matched |
| [320] shade_heaven del | 740 (+11) | matched |
| -- | 734: temple_pyre set a second time (the reward's 1-tick `loc_change` reverting to the same loc) | ours only, harmless; Open |
| [44]/[153] strike_and_light r2 at 3506,3316 | 687 | matched |
| [160] oil: human_pickuptable (no delay), oil_pour | 515, 548 | matched |

The wall repairs in the test go through `::mortton_repairtemple` (every spend in one tick), so
the 4-tick cadence and the per-tick anim are not on our tick log; the code path is the op's.

## Open

- The shades' wall attacks as npcs (`shade_attack_wall`, `demolish_1` range 5) and when a wall
  stage goes up (the build model is LostCity's).
- `shadeattack` (varbit 201).
- Razmire's store prices (stock matched / fixed; prices not diffed).
- `last_quest_id` / `last_started_quest_id` / `last_completed_quest_id` (all quests).
- The pages and journal rows listed UNVERIFIED above.
- A test row that repairs a wall through the op (not the cheat) to put the cadence on our wire.
- The shades' fight against our tick log (seqs, sounds, `human_shade_defence`): not diffed.
