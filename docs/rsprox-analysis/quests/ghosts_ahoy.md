# Ghosts Ahoy — RSProx index 4462 (with 4460, 4461)

Recorded 2026-11-02, rev 241.1, by the owner, labelled "Ghost's Ahoy Part 1" (index 4461,
world 495, 870 ticks) and "Ghost's Ahoy Part 2 and Droalak from Making History partial"
(index 4462, world 438, 6,560 ticks). 4461 is the trip in: Grand Exchange buying, cooking on a
forestry campfire, the Varrock tea stall, the lumberyard scroll, Paterdomus and Priest in
Peril's passage, Canifis. 4462 plays the whole quest, start [997] to completion [6547], plus the
Ectofuntus, an Evil Bob random, Making History's Droalak and Melina, and the nettle tea round
Edgeville. The journal was opened at 13 Ghosts Ahoy states. It is the only recording of the
quest on the index (`search npc 2985`/`2994`: 4462 alone).

Before this port the quest was a wiki/Quest Helper paraphrase: of 473 distinct recorded page
texts in 4462, 402 were missing from our tree. Runedraw was a chat-menu game instead of
interface 9, the old man and the chests gave the wrong scraps, the Ectofuntus was a 2009scape
port that never sent the cache's grinder and token varbits, and the journal was invented.

**Ours:** `quests/quest_ghostsahoy/` — `ahoy_shared.rs2` (helpers, the recorded journal, the
completion, the ectophial), `ahoy_hub.rs2` (the barrier and guards, Velorina, Necrovarus, the
town's history, nettle tea), `ahoy_crone.rs2` (new: the Old crone, called from
`quest_animalmagnetism/scripts/anma_farm.rs2`), `ahoy_manual.rs2` (Ak-Haranu, Robin, Runedraw
on interface 9), `ahoy_robes.rs2` (the innkeeper, the bedsheet, Gravingas, the villagers, the
temple door and coffin, Sarah, the ghost sailor), `ahoy_book.rs2` (the toy boat and flag, the
mast and wind, the old man, the chests, the lobster, the plank and rocks, the map, the captain,
the dig), `configs/ghostsahoy.constant`, `configs/ghostsahoy.varp`. Shared:
`skill_prayer/scripts/ectofuntus.rs2` (rebuilt), `doors/scripts/door_procs.rs2` (the harbour
door's sound), `skill_cooking/scripts/cooking.rs2` (nettle-water on a range and on the
Edgeville stove), `quests/quest_onesmallfavour/scripts/onesmallfavour_puzzles.rs2` (a branch
into the tea pour), `interface_questjournal/scripts/quest_overview_start.rs2`. Tests:
`test/quests/ghostsahoy.lua` (re-driven along 4462), `test/prayer/ectofuntus.lua` (new).

## Actions (rsprox_actions.py) — a verdict per row

4462 (the quest session; 4461's and 4460's rows are at the end):

| first tick | action | verdict |
|---|---|---|
| 76..126, 1346..1355, 1439..1550, 4494..4520 | welcome screen, shops (Canifis general store), Canifis/Edgeville/GE banks, the GE | generic |
| 87, 1297, 1303, 1563 | glory (worn) Edgeville x2 | generic jewellery, audited 2026-10-29 |
| 93.., 535, 1621.. | stamina potions | the recorder's kit |
| 244, 384..396, 684, 5935, 6001, 6280 | Ghost disciples | **rebuilt** (ectofuntus.rs2): the fountain's five questions, the grinder page upstairs, the tokens |
| 252, 1023, 5931..5984, 6412..6499 | Necrovarus | **this content** |
| 260, 2342, 2362, 2666, 6410, 6417 | ghostspeak worn/dropped/picked | generic equip/drop; drives the wail (below) |
| 328, 343 | Ghost guard | **this content** (no tokens: recorded; with tokens: wiki) |
| 341..6503 | the energy barrier (op4 x17, op1 x1) | **this content**: the recorded crossing shape |
| 353, 521, 690 | ahoy_trapdoor / _open | generic maplink (climb_shared.rs2); shape not re-audited (Open) |
| 358..825 | ahoy_ectofuntus (worship x32) | **rebuilt** |
| 365, 514, 827, 908, 5986, 6038 | ahoy_tower_stairs_lv1 / _top | generic maplink (maplink.dbrow); landings match (3666,3522,1 / 3666,3517,0) in our test |
| 377, 6018..6023 | ahoy_coffin / _open | **this content** |
| 378, 6005..6028 | ahoy_harbour_door (temple door) | **this content**: the disciple's stop, the key, the walk-through |
| 1110..6330 (others) | ahoy_harbour_door (inn, shacks, the wreck) | generic door; **fixed** its sound: creakydoor_open (56), 12/12 |
| 406..895 | grinder loader / handle / chute | **rebuilt** (ectofuntus.rs2) |
| 527 | ectopool_sc_raildown | agility shortcut (maplink_agility.dbrow); "You need an agility level of at least 58 to climb down this wall." not checked against ours (Open) |
| 539..4159 | ahoy_cavern_stairs / _top | generic maplink; the landings are in maplink.dbrow, not re-audited (Open) |
| 585..765 | bucket on ahoy_new_green_floor | **rebuilt** (ectofuntus.rs2) |
| 650, 809, 4176 | ahoy_ladder_from_cellar | generic maplink (human_reachforladder, move a tick later) |
| 954..1090, 6354..6547 | Velorina | **this content** |
| 1066..6518 | quest list / journal (38 opens) | **this content**: the journal (below) |
| 1113..1278, 2770..3093, 6332 | Robin | **this content** |
| 1174..1236, 3252..3271, 3500 | Ghost innkeeper | **this content** |
| 1240..5920 | bedsheet wear/remove (op3 x10, worn op x12) | generic equip; the barrier and the captain read it |
| 1253 | bedsheet on Robin | "Nothing interesting happens." (default) as recorded |
| 1282..6510 | Ghost villagers (x46) | **this content** |
| 1319..1719 | nettles | **this content**: gloves / sting / regrowth |
| 1382 | varrock_sc_tunnel_west | audited by Troll Romance (4456) |
| 1464, 1470, 1475, 1492, 1519, 1558 | cup of tea, hot water, nettles on the wrong vessel | **fixed**: cup of tea's Drink, hot water's Empty (tea.rs2); the wrong uses answer the default |
| 1571, 1576 | fencegate_r, poordoor | generic doors (synth 67 / 62) |
| 1585..1597, 2125..2194 | nettles into water, the stove, pouring, milk | **this content** |
| 1592..1796 | 100_dave_stove | **fixed**: the Edgeville stove had no cooking binding (no category in the cache) |
| 1826 | lumberyard scroll | generic (teleport_scroll.rs2) |
| 1852 | fai_varrock_member_gater | generic double gate (synth 67) |
| 1900..1947 | Paterdomus trapdoor, the mausoleum gates, Drezel, the holy barrier | Priest in Peril's (committed 6382aa3302 from 4459) |
| 1998..2391, 4306..4422, 6154..6163 | Old crone | **this content** |
| 2007 | crate2 | generic: "You search the crate but find nothing." (crates.rs2) as recorded |
| 2668..2703 | Alice, Malcolm | **fixed**: Alice's greeting menu and Malcolm's pre-Animal Magnetism pages (anma_farm.rs2) |
| 2798..3203, 6341..6376 | Ak-Haranu (talk and trade) | **this content**; the shop is the existing one ("I can't reach that!" across the counter, engine) |
| 2901..3086 | ahoy_runedraw buttons | **this content**: interface 9 |
| 3107 | wield the signed bow | generic: the Ranged 5 requirement line |
| 3114, 3249 | Bill Teach (fever_teach) | Cabin Fever's; no page recorded |
| 3239, 4985 | ahoy_boxes_1 | **this content**: "There is nothing interesting in these boxes." |
| 3401..3890, 5756, 6304, 6520 | Gravingas (protester multinpc) | **this content** |
| 3501..3660 | Evil Bob, Prison Pete, the lever, the balloons | **new**: `macro_prison_pete/` (docs/rsprox-analysis/randoms/prison_pete.md) |
| 3705..3829 | Melina, Sarah, Droalak | Making History's (making_history.md); Sarah **new** binding |
| 3845..3858 | Ghost shopkeeper | generic shop; its page and menu are in our tree |
| 4369..5220 | toy boat (inspect, mend, dye) | **this content** |
| 4734..5280 | the wreck's chests | **this content** |
| 4748..5406 | ghostship ladders | **this content** (bound for the wind overlay) |
| 4775..4819 | giant lobster | **this content** (a regular mob; light audit below) |
| 4938..5206 | ahoy_mast | **this content** |
| 4943 | ahoy_skull_captain | **this content** |
| 5283, 5341 | map scraps combined | **this content** |
| 5291..5400 | gangplank, ahoy_rock_invisible | **this content** |
| 5348..5612 | the map: Read (interface 8), Walk, drop/pick up | **this content** |
| 5468..5486 | Ghost sailor | **new** binding |
| 5514..5669 | Ghost captain | **this content** |
| 5558..5629 | spade | **this content** at the spot; elsewhere generic "Nothing interesting happens." |
| 5718..5751 | dyes / ectoplasm on the bedsheet | **this content** |
| 6076, 6176, 4199, 5811 | examines | engine |

**4461:** banks, GE, forestry campfire (cooking, category 152: generic), skill guide — generic;
tea stall steal and the tea seller's shop — generic (thieving.rs2, tea_seller.rs2); cup of tea
— **fixed** (above); Kharyll tablet — **fixed**: "You must complete the Desert Treasure I quest
before you can use the tablet." (the break itself unrecorded); lumberyard scroll, the members'
gate, Paterdomus — as above; Hiylik Myna (ramble_priest) — **new** binding
(`areas/area_paterdomus/scripts/hiylik_myna.rs2`); Malak (fourdiamonds_vampire_lord) — **fixed**
his line ("barkeep", not "barkeeper"); the Canifis general store and its pack of buckets —
**fixed**: the pack's Open (`general_use/scripts/packs.rs2`, 100 noted buckets, synth 3616).

**4460** (Making History's start): see making_history.md; its necklace of passage refused
Wyrmscraig — **fixed** (the recorded objbox); the Eagles' Eyrie desert cave — **fixed**: Enter
had no binding at all, Exit said "You leave the eyrie." (both now the recorded spam lines).

**Teleports (4462):** the tower stairs, trapdoor, cavern stairs and cellar ladder (maplinks),
glory x2, the Varrock tunnel's three legs, the lumberyard scroll, Paterdomus and the holy
barrier (Priest in Peril's), Evil Bob's prison and back (the random), the wreck's ladders and
plank, the rocks (each a jump), the map's Walk (one step back), the captain both ways.

## Stages (%varb217_ahoy_questvar, varp408 transmitted)

| value | recorded | goes out with | ours (scriptrun ga1) |
|---|---|---|---|
| 1 | [997] | the start confirm's Yes., with "You've started a new quest" and the player's page | same |
| 2 | [1033] | Necrovarus's last page | same |
| 3 | [1073] | Velorina's "She fled..." page | same |
| 4 | [2316] | the milky tea handed over (tea var 3) | same |
| 5 | [6157] | the last item handed in ("Wonderful; that's everything I need.") | same |
| 6 | [6161] | the ritual's box (the worn amulet enchanted in its slot) | same |
| 7 | [6474] | the command's beam box clicked (the amulet back to a plain one) | same |
| 8 | [6547] | Velorina's last page, with the scroll | same |

Sub-states, all on varp 408 and all recorded: the tea 211 (1 [2020], 2 [2074], 3 [2316]), the
bow 212 (1 [2852], 2 the first game [2900], +1 a win, 7 signed [3097], 8 traded [3201]), the
sheet 216 [1132], the petition 209 (1 [3466], 1 + n signatures, 31 burnt [5949]), the boat 214
(1 [4351], 2 [5248], 3 [5264]), the lobster 215 [4819], the door 213 [6013], the items 206
[4306], 207 [6155], 208 [6157]; the Ectofuntus' 210 and 4769 (below), the wind 205. Ours writes
every one on the same page or click. The flag colours are the server's alone (no packet).

## The start and the end

- Start [954..1002]: Velorina's greeting menu; the history (the same 13 pages the innkeeper
  tells, [1201..1210]) or "Yes, I do. It is a very sad story."; "Start the Ghosts Ahoy quest?"
  Yes./No. No combat warning (the quest has no combat level). "No." and "Sorry, I'm scared of
  ghosts." are unverified (never chosen).
- End [6532..6554]: Velorina's five pages and the objbox with the ectophial's model; on the
  last page's click stage 8, quest points 114 -> 116, the line, disease -50/0, the ectophial into
  the pack, song 147 and jingle 152, the scroll "2 Quest Points | 2,400 Prayer XP | An
  Ectophial | Free Port Phasmatys Entry" with the ectophial at 78,128. Ours: the same.

## Pages

`rsprox_pages.py`: 875 rows in 4462. Every Ghosts Ahoy page is in our scripts verbatim with its
mood, the Ectofuntus' and the town's other ghosts' too (scratchpad pagecheck: 473 distinct
texts, the remaining 24 "misses" are our interpolated strings — "You owe me <n> gold coins!",
"The ghost signs your petition. You have obtained <n> signatures so far.", the flag's colours,
"Here's <n> ectotokens." — which match when filled). Jagex's quirks kept: the player says "Well
done, you beat me. But you still owe me 50 gold pieces." after a LOSS; Velorina's
"<displayname>- I sensed"; Necrovarus's " ... any ... " with its spaces; the crone's son story
ending "the sea. "; the villagers' three refusals with no chathead animation (-1); the objbox for
the crone's toy boat showing the mended boat's model while the plain boat goes in the pack.

## The journal

Recorded at stages 2, 3 (three tea states), 4 (seven sub-states), 6 and 7 — every row is in
`~ahoy_journal` as sent (old format: struck paragraphs, blank rows between, the current step
blue with red names; at the cup-given tea state Jagex ends on a blank row and so do we). The
paragraphs at stage 4 come in Jagex's order: the item list (each struck once given), the toy
boat, the bow, the petition. UNVERIFIED: stage 1, stage 5, a part-signed petition, the bow
signed but not traded, and the completed journal (no recording opens them).

The not-started overview: "talking to Velorina at her home in Port Phasmatys." (4461 [746]),
now in `quest_overview_start.rs2` with six other overviews these sessions opened.

## Sections

**The barrier** [1018..1054]: L+1 the face (sizex=2), L+2 the toll line (spam) and the tokens
and a step onto the barrier's row, L+3 the step off. Leaving is free with no line; from stage 7
entering is free with no line [6503]. No tokens: "You don't have enough tokens to pay the toll."
and the guard's page [341..342]. In a sheet: the mesbox [3393]. Ours: the same shape
(scriptrun: 12 crossings). The guard's own talk with tokens enough is the wiki's (unverified).

**Velorina, Necrovarus, the guard, the innkeeper, Gravingas, Ak-Haranu, Robin, the crone, the
old man** — page for page as recorded (above). The ghosts wail without a worn ghostspeak amulet:
"Woooo wooo wooooo woooo" and the mesbox "You cannot understand the ghost." (3/3).

**Nettle tea** [1319..2194]: with gloves, the pick anim and synth 2581 on the op, two ticks
later the line, the nettles, synth 2581 and the patch gone for 13 ticks (7/7); bare-handed the
sting, a 2 and synth 1138 (2/2). Nettles into a bowl of water (a make-X with more: Open); on a
range or the Edgeville stove, human_cooking and synth 2577 with "You boil the water and make
nettle tea." or "You accidentally let the water boil over." (2 of 4 at Cooking 20: our odds are a
guess fitted to those four). Pours into the vessel's own slot with synth 2401.

**Runedraw** (interface 9, [2900..3086], seven games): the stake and the bow var with the
"Yes, I'll give you a game." page; the board on its click (20 slots cleared, both buttons); a
draw goes out on its click (rune, total, buttons hidden, "Thinking..."); Robin draws 2..4 ticks
later (2 x3, 3 x4, 4 x6 of 13), buttons back, "Your turn"; Hold: Robin draws every 2 ticks until
he passes the player's total or draws the Death rune; the board stays 4 ticks after the end (3
once), then closes with the player's page. Ours: the same messages, texts and timings;
scriptrun: 9 games to four wins.

**The wreck**: the toy boat mended by Inspect with silk, needle, thread and knife [4706]; dyes
by the "Which part of the flag do you want to dye?" menu. The mast: the wind overlay (interface
10) opens with the climb to the top deck and closes with the climb down; varbit 205 = 1 is a
lull (13/13 looks in a lull named a part, 11/11 in the wind said "too hard"); lulls 2..12 ticks,
wind 2..46: modelled as a lull starting at 1/13 a tick and ending at 1/6 (fitted, n 12 each).
The old man as recorded (the boat becomes the key in its slot). The chests: locked ones say so
(9/9); the Captain's chest takes the key and gives scrap 1 with the `_active` flash and synth 62;
the lower deck's chest (3618,3542,0) holds the lobster and scrap 2, the rock's (3606,3564) scrap
3 — ours gave scrap 3 and 2 the other way round. The plank: L+1 face, L+2 the line and the move.
The rocks: five pairs, each jumped from its own tile three ticks after the click with
human_jump_stones (26), exactmove (47, 59), synth 2461, 10 Agility xp; 1 of 11 fell
(human_falling_end, synth 2455, a 2, synth 518).

**The giant lobster** (a regular mob, light audit): added at 3616,3543 facing south with a hint
arrow [4774], faces the player, swings every 4 ticks (splats on the player at 4777, 4781, ...:
3, 0, 0, 0, 0, 0, 3, 0, 0, 0), defends every hit, dies with lobster_update_death and is gone 3
ticks later. Jagex sends its attack seq once ([4777]); afterwards the defend seq started a tick
earlier by the player's swing wins. Ours sends the attack seq on every swing (15/15): engine seq
priority, Open.

**The captain and the dig** [5514..5651]: the sheet warning, the 25-token fare, the fade
"After a long boat trip you arrive at Dragontooth Island." on the page's click, the move four
ticks later, the fade in, closed two ticks on; back by Travel. The book at 3803,3530 three ticks
after the spade, the map kept.

**The Ectofuntus** (test/prayer/ectofuntus.lua, 24/24): worship on the op — the spam line,
ahoy_prayer, synth 2671, the bucket and pot emptied in their slots, 5 tokens owed on varbit 4769
(5..25 recorded), four times the bones' Prayer xp. The grinder: varbit 210 (0, 1 loaded, 2
ground); "You put some dragon bones in the grinder's hopper." (ahoy_bone_dump 5, synth 1133 5),
"You wind the grinder handle." (ahoy_bone_grind 10, synth 1131 10), "Some crushed dragon bones
pour into the bin." three ticks on, "You fill a pot with crushed dragon bones."
(ahoy_fillbucket_bonedust 5, synth 1136 5), "The grinder is already empty.", the handle's Check;
used on the hopper it goes on by itself (load T, wind T+5, pour T+8, fill T+13, next load T+19).
The pool: the anim, two ticks on the line, the bucket filled in place, synth 1132, every four
ticks while empty buckets last. The disciples pay out with "Certainly, mortal. Here's 25
ectotokens."

## Wire audit (scriptrun ga1 tick log against 4462)

| kind | recorded | ours | verdict |
|---|---|---|---|
| harbour doors | creakydoor_open 56, 12/12 | 62 before; **56** now | fixed |
| chests | human_openchest + 52, the open form the tick after | same | matched |
| captain's chest | `_active` for 1 tick + 62 | same | matched |
| coffin | 54 + the open form | same | matched |
| crone's ritual | human_dancing on the crone with the page | same | matched |
| Ak-Haranu | emote_bow with his last page | same | matched |
| rocks | human_jump_stones 26, exactmove 47/59, 2461 | same (10/10) | matched |
| dig | human_dig + 1470 | same | matched |
| tea | pick 2581 x2, fry 2577, liquid 2401 | same | matched |
| lobster | attack seq once, defend x11, death | attack x15, defend x15 | **Open** (engine seq priority) |
| hint arrow | on the lobster's add | added | fixed |

`varp_transmit_audit.py` on 4460..4462: nothing of this quest's is unsent (varp 408 and 604 are
transmitted). varp 531 (Prison Pete, Drill Demon, the Pirate's combination lock) was not sent:
fixed by the Prison Pete port.

## Open

- The bone key: Jagex drops it on Necrovarus's own tile [5956]; our engine cannot take an item
  from under a standing npc, so ours drops it at the player's feet.
- The lobster's attack seq (above).
- Nettles into water with several bowls is a make-X ([1645]); ours makes one per use.
- The boil-over odds (a guess from 4 rolls), the rock-fall odds (1 of 11), the flag's colour
  distribution and the villagers' answer weights (22 asks) are fits to one session.
- Unverified (no recording): every page before or between the recorded stages that the doc
  names above; the 500-token captain's pass; the ectophial's teleport and refill; the token hold;
  Robin with too few coins; a draw tie; losing the key, the petition, the manual or the bedsheet.
- Not re-audited this pass: the Port Phasmatys maplinks' shapes (trapdoor, cavern stairs,
  cellar ladder: the landings pass in our test), the ectopool shortcut's level line, the
  Ectofuntus' own tick timings against the recording beyond the test's waits, and the mast's
  overlay on the live client (scriptrun draws no interface).
- `quest.journal` fails on scriptrun as for every quest (no clientscript builds the quest list).
