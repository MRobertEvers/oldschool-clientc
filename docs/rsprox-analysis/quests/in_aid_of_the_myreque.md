# In Aid of the Myreque -- from RSProx index 4634

Recording (rev 242.1, the owner's, 2026-11-21): **4634** "In Aid of the Myreque", the whole quest
in one 8,409-tick session, stage 0 to 430. Transcripts: `rsprox_index.py get 4634` (default,
`--raw`, `--bas`). Ticks below are `[tick]` of 4634.

Our content (`OSRS-Content/osrs-content/server/scripts/quests/quest_inaidofthemyreque/`) was a
quest-helper/wiki "compatibility port": invented dialogue, op2 "Repair" overlays the cache does not
have, the cellar and the crate collapsed to one click, invented stage values, the completion icon
the Gadderhammer. None of the recording's pages matched it. It is rebuilt here from the recording;
a branch no session takes is the wiki transcript's (`Transcript:In_Aid_of_the_Myreque`, fetched
2026-11-21), marked `[wiki]` in the scripts, mood unverified.

Files: `scripts/myreque2_hideout.rs2` (the Hollows: Veliaf's offer and briefing, Polmafi, Radigad,
Ivan, the garlic chest), `myreque2_burgh.rs2` (the gate, Florin's tomatoes, the food chest, every
citizen by stage, the children, Marius), `myreque2_inn.rs2` (the wall, the trapdoor, the cellar copy
and its rubble, buckets, finds, plaque, plaster, scroll), `myreque2_works.rs2` (store roof and wall,
the crate, the bank, Cornelius, the furnace and the Castle Drakan scene, the poll booth),
`myreque2_tithe.rs2` (the blood tithe, the fight copy, Veliaf's rescue, Gadderanks' death),
`myreque2_trek.rs2` (the Burgh map, the ambush rooms, Ivan at Paterdomus, Ivan's food),
`myreque2_temple.rs2` (Drezel, the keyhole, the library, the three books), `myreque2_rod.rs2`
(boards, tomb, coffin, mould, the silvthrill rod, enchant, the well), `myreque2_shared.rs2`
(requirements, the Burgh-hideout Veliaf/Polmafi/Radigad, completion, scene frames),
`myreque2_journal.rs2`. Hooks outside the quest: Drezel (`quest_priestperil/mausoleum_drezel.rs2`),
the Burgh Veliaf (`quest_sinsofthefather/sinsofthefather.rs2`, now from stage 315), Aurel's Trade
(`shop/burgh_de_rott/aurels_supplies.rs2`), the booth's Bank (`interface_bank/bank_booths.rs2`),
Lvl-1 Enchant (`skill_magic/spells/enchant.rs2`), logout/death (the copies), and the wall and
trapdoor moved here from `quest_darknessofhallowvale/doh_burgh.rs2`, the keyhole from
`areas/area_mausoleum/paterdomus.rs2`. Test: `test/quests/inaidofthemyreque.lua`.

## Stages (every recorded write; `myreque2.constant` names them)

| value | tick | goes out with |
|---|---|---|
| 20 | [195] | Veliaf "Exactly. Head down to Burgh de Rott..." + "You've started a new quest" |
| 30 | [481] | Florin "Hey! Don't try to sweet talk me..." |
| 40 | [549] | food on the chest (objbox, then "Nothing interesting happens." on its continue) |
| 60 | [605] | a citizen's "Well, there's the inn..." |
| 70 | [1135] | the first climb over the inn's wall |
| 80 | [1334] | the trapdoor's rubble broken (`burgh_inn_colapsed_wall` 1) |
| 90 | [1338] | the trapdoor opened (`burgh_inn_trapdoor` 1) |
| 100 | [1340] | the first climb down (into a copy) |
| 110 | [2151] | the fifteenth pile scooped (`burgh_inn_rubble_pile` 15) |
| 140 | [2492] | Aurel "Please can you fix the roof, then the walls..." |
| 150, 160 | [2773], [2793] | the roof, the store's wall |
| 165 | [2857] | "Aurel gives you a crate." (`burgh_food_type` 1 at [2853]) |
| 170 | [3342] | "Aurel quickly puts the stock out around the shop." (`burgh_store_stocked`) |
| 180, 190 | [3538], [3597] | the booth, the bank's wall |
| 200 | [3628] | Cornelius behind the booth (`burgh_bank_teller`), inside a fade |
| 205 | [3645] | a citizen's furnace hint ("What should I do now?") |
| 210 | [3819] | the chimney (`burgh_furnace_fix` 1; coal 2 at [3960], no stage) |
| 220 | [3983] | the furnace lit (`burgh_furnace_fix` 3), the Castle Drakan scene's fade |
| 230 | [4023] | Vanstrom's last page (`blood_tithe_visible` 1) |
| 240 | [4152] | inside the fight copy, Gadderanks' page again |
| 250 | [4472] | Veliaf comes through the door |
| 260 | [4524] | the third death (`juvinate_deaths` 3), Gadderanks' "*cough*" |
| 280 | [4552] | out of the copy (`blood_tithe_visible` 2) |
| 290 | [4990] | Veliaf "Before you go, do you mind telling Polmafi..." |
| 300 | [5055] | the player's page to Polmafi |
| 310 | [5069] | Ivan "Very well. I'm ready, lead the way." |
| 315 | [6095] | Ivan's arrival pages (`route_hideout_npcs` 1) |
| 320, 330, 340, 350 | [6131], [6139], [6187], [6216] | Drezel's pages; the key |
| 360 | [6255] | the library trapdoor's move |
| 370, 375 | [6265], [6268] | the Sleeping Seven found; opened |
| 380 | [7073] | the tomb's cave mouth |
| 390, 400 | [7270], [7502] | the mould; the silvthrill rod |
| 410 | [7966] | Lvl-1 Enchant cast on the rod |
| 420 | [8281] | the rod lowered into the Salve (a rope held) |
| 430 | [8374] | "You give the Rod of Ivandis to Veliaf." (`templetrek_npcs_visible` 1); the scroll on the click two pages on, [8378] |

All of varp 704/705/706 are `scope=perm, transmit=yes` (`myreque2.varp`). Two server-only varps
were added: 9833 (Florin's throw clock) and 9834 (the player is in one of this quest's copies).

## Action inventory (`rsprox_actions.py`), a verdict per row

| first tick | action | verdict |
|---|---|---|
| 9, 53, 76, 229 | welcome screen, quest list (31), world map, journal close (23) | client UI; the journal: see Journal |
| 17 | Vanstrom/Stranger | In Search of the Myreque's post-quest line (not this quest) |
| 93, 112, 127, 4871.. | Hair of the Dog trapdoor, false wall, cave wall tunnel | In Search of the Myreque's routes (the twin's) |
| 102, 105, 5794, 6953 | crates in the tavern basement | generic crates, not audited |
| 118, 6939, 7065, 7073 | the tomb's cave mouth | **this content** (`myreque2_rod.rs2`) |
| 140, 146, 4908, 5055, 5064 | Radigad, Veliaf, Polmafi, Ivan in the Hollows | **this content** (`myreque2_hideout.rs2`; Radigad via a hook in `routequest_hideout.rs2`) |
| 242.. (x11) | Kharyll teleport tablet | generic tablet, not audited here |
| 269.., 2946.., 7464 | bank booths, bankers, deposit box | generic bank |
| 286, 294 | book of scrolls, Mort'ton teleport scroll | generic teleport scroll, not audited here |
| 314, 2220, 2263 | Burgh gate (both leaves) | **this content**: locked before the food; walked through after |
| 324..1083 | Florin and every citizen | **this content** (`myreque2_burgh.rs2`) |
| 336.., 549 | food chest (search, food used) | **this content** |
| 346, 356, 435 | food table (search, food used) | **this content** |
| 367 | food on Florin | engine "I can't reach that!" |
| 1008, 3529, 3550 | bank booth (inspect, fix, collect) | **this content** |
| 1018, 3560 | poll booth | **this content** (refusal line) |
| 1115 | Mort'ton harbour door | generic door, not audited |
| 1127.. (x21) | the inn's broken wall | **this content** |
| 1138.. (x26) | the inn's trapdoor | **this content** |
| 1149, 2279 | the outside rubble pile | **this content** |
| 1167, 1609, 1632, 1662, 3975 | picking up a bucket, pickaxe, spade, nails, tinderbox | generic ground items |
| 1191 | bucket on the collapsed wall | "Nothing interesting happens." (the default) |
| 1224, 8199 | Canifis men | generic, not audited |
| 1294, 1305 | Burgh agility fence, hatch ladder | generic shortcut/ladder, not audited |
| 1352.., 2184, 7927 | the cellar's ladder up | **this content** |
| 1371.. | stamina, prayer, attack, strength potions; shark, trout | generic drinking/eating |
| 1396, 1405, 1444, 1456, 1467, 1470 | Mort'ton boat, Cyreg, the bridge tree, snail, Curpile, the hideout door | In Search of the Myreque / Mort Myre generic |
| 1567.. | rubble a_1..a_4 (mine, remove, bucket on rubble) | **this content** |
| 1603.., 1651.., 1812 | drops (spade, pickaxe, rock, glass, nails, plaster destroy) | generic drop; the plaster's destroy generic |
| 1666, 1673, 2191..2256 | bucket drop/empty | **this content** |
| 1697 | spade dig | generic ("Nothing interesting happens." + dig) |
| 1713, 1644 | examine | client |
| 1751, 1958 | plaster Read, scroll Read (long scroll 625) | **this content** |
| 2169, 7649 | the cellar plaque | **this content** |
| 2505, 2778 | the store's ladders (plane 0 <-> 2) | generic ladder bindings, not re-checked |
| 2588, 2785, 3584 | roof, store wall, bank wall | **this content** |
| 2871, 4652, 5676, 7288, 7366 | Varrock / Falador Teleport | generic spells |
| 2920..3183 | GE clerk and offers, Varrock West bankers | generic GE/bank |
| 2987..3202 | items on the crate, crate Search | **this content** |
| 3337, 3340, 4091, 4604 | Aurel (Trade, hand-in, tithe, hero) | **this content** |
| 3640, 3882, 3898, 3878 | Cornelius the banker, Gabriela, Valeria, Marius's bed | **this content** |
| 3753.., 3978, 4042 | the furnace (chimney, coal, light, Smelt) | **this content**; Smelt with no ore is generic (the line "You don't have any ore to smelt." is NOT checked) |
| 4070..4152 | the juvinates, Wiskit, Gadderanks in the store | **this content** (`myreque2_tithe.rs2`) |
| 4163.., 4248.., 4258, 4303.. | blessed sickle, bloom, prayers, magic shortbow, orbs | the recorder's own kit |
| 4168, 4470, 4500 | attacking the juvinates, Gadderanks | **this content** (the fight) |
| 4292 | the escape door (Quick-escape) | **this content** |
| 4520 | wounded Gadderanks during the fight | **this content** ("Now is not the time for talking.") |
| 4777..4817 | harralander/garlic/silver potions | Guthix balance (a later quest's/herblore), not this quest |
| 4888, 4892 | rats | generic |
| 5054, 7657 | the garlic chest | **this content** (the twin's block, kept) |
| 5083..5848 | the Burgh map | **this content** |
| 5119, 5480 | the ambush juvinates | **this content** |
| 5120, 5207 | the swamp exit path | **this content** |
| 5169, 5450, 5616, 5824 | Ivan in the ambush and before it (food) | **this content** |
| 5771 | Canifis innkeeper | generic |
| 6110, 6116, 6884, 8146 | Paterdomus trapdoor, underground wall, door | Priest in Peril's (generic) |
| 6122, 6244 | the library keyhole | **this content** |
| 6127.. | Drezel | **this content** (hooked from `mausoleum_drezel.rs2`) |
| 6232 | drop the library key | generic drop; the lost-key line is this content |
| 6251, 6873 | library trapdoor, ladder | **this content** |
| 6262, 6467 | the bookcases | **this content** |
| 6268, 6476, 6706, 6453 | the three books | **this content** (`book` 392, recorded spreads) |
| 6961 | tavern basement ladder | In Search of the Myreque / generic |
| 7034 | a hunting trap | not this quest |
| 7102, 7270 | the coffin (inspect, soft clay) | **this content** |
| 7386..7500 | the mould on Falador's furnace, Smelt, the silver panel's rod | **this content** (`[if_button1,silver_crafting:ivandis]`) |
| 7475 | cutting a sapphire | generic crafting |
| 7506, 7986, 8153 | Inspect the rod | **this content** |
| 7668..8371 | Polmafi, Radigad, Veliaf in the Burgh hideout | **this content** (`myreque2_shared.rs2`) |
| 7674..7683 | books on Polmafi | "Nothing interesting happens." (the default) |
| 7964..7970 | Lvl-1 Enchant on the rod | **this content** |
| 8158, 8276 | the rod on the well | **this content** (needs a rope) |
| 8263 | a grave in the mausoleum | Priest in Peril's |
| 8286, 8369 | wield/unwield the Rod of Ivandis | the rod's own equip (not audited: `--bas` not read) |
| 8385 | quest rating | client |

## Teleports (from the list)

| tick | what | ours |
|---|---|---|
| [1135] etc. | the wall: exact move 60 cycles + human_walk_crumbledwall + climb_wall | same |
| [1341], [1561] | trapdoor -> a copy of the cellar (plane 1 of 54_150 as plane 0) | same (a fresh copy each time) |
| [1355], [2188] | the cellar ladder -> 3491,3232 | same |
| [3986], [4033] | into/out of the Castle Drakan copy (square 55_52) | same |
| [4151], [4295], [4350], [4552] | into/out of the store copy (square 54_50) | same |
| [5111], [5476], [5854] | the Burgh map -> the ambush rooms (square 44_71) | same |
| [5208] | the swamp exit -> 3429,3401 | same |
| [6090] | the ambush won -> Paterdomus 3432,3484 | same |
| [6255], [6876] | library trapdoor 3360,9904; ladder -> 3440,9899 | same |
| [7075] | tomb crawl -> 3491,9876 | same |
| [7638], [7931], [8016] | trapdoor -> hideout plane 0 3490,9631 (after the cellar is clear) | same |

## Scenes and frames

| scene | ticks | ours |
|---|---|---|
| Florin's camera on the food | [550..561] | moveto 3487,3250 (500), lookat 3483,3242 (100), 100/100; reset on the page's close |
| the cellar plaque | [2151..2163] | the no-continue box, two camera steps, the box again 7 ticks on, reset on its click |
| Cornelius changes | [3626..3629] | fade out, box, teller varbit + stage, fade in, box |
| Castle Drakan | [3976..4038] | the copy, the lock (fov 1), cast, camera 1900/450 jump, Gadderanks' walk, Vanstrom's turn, unlock, Gabriela's line |
| the fight copy | [4147..4159], [4292..4299], [4548..4553] | fade/minimap frames in, out by the door, out after the death |
| Veliaf leaves | [4578..4585] | box + fade, varbit 3, fade in |
| the tomb crawl | [7073..7076] | human_longcrawl, fade, climbing_loop, move, fade in |

## Journal

Rebuilt from all 17 recorded opens (`myreque2_journal.rs2`, rows verbatim, sent by
`~quest_journal_rows`). Unverified (no open there): the current paragraph at 30..90, 170..205,
220, 240..270, 290..310, 320..340, 360..370, 380, 400, and the whole of 430.

## Verified

`run.py inaidofthemyreque --no-build --no-publish` from a fresh fixture: **194/194 PASS** over five
legs (`test/quests/inaidofthemyreque.lua`):
1. `veliaf_to_cellar`: the offer, Florin, the food chest, the inn hint, the gate, the wall, the
   trapdoor's rubble, the trapdoor, the cellar copy, one pile.
2. `cellar_to_store`: the other fourteen piles with their finds, the plaque camera, the dump, Aurel,
   the roof and wall, the crate (the food rolled), the hand-in.
3. `bank_to_tithe`: the booth, the wall, Cornelius changing, his furnace hint across the booth
   from the recorder's tile 3495,3211 ([3640]), the
   chimney, the coal, the light, **Castle Drakan** (`t.cutscene.await`: moveto h1900, lookat h450,
   reset), the three in the store, the fight copy, Veliaf's rescue, Gadderanks' words and death, the
   warhammer, Veliaf leaving.
4. `hollows_to_library`: Veliaf's briefing, Polmafi, Ivan; **route 2 lost** (the juvinates left on
   Ivan until he flees, the exit path); route 1 won; Ivan at Paterdomus; Drezel's two menus; the
   key, keyhole, trapdoor, bookcase, the book.
5. `rod_to_end`: the boards, the tomb, the mould, the silver panel's rod, Lvl-1 Enchant (with its
   17.5 xp), the well with a rope, Veliaf, the scroll, the points, the journal.

## Wire comparison (ours: that run's tick log; the recording: 4634)

Per interaction, events from the first one after the click (`sound`, player seq, quest var):

| interaction | recording | ours | verdict |
|---|---|---|---|
| inn wall | +0 seq 840, climb_wall 2453 | same | matched |
| rubble mine | +0 628, +3 628, +4 mine_ore, +7 628, +8 mine_ore | same (after the fix: the seq a tick before each step) | fixed |
| rubble scoop | +0 830 + digspade, +2 again, +4 the pile | same | matched |
| store/bank wall, roof, booth | +0 pest_repair 3931 + hammer x3 (0/20/40), +2 the varbits + m2_build | same | matched |
| library trapdoor | +0 827, +1 the stage | same | matched |
| tomb crawl | human_longcrawl 2796 + climbing_loop x3 | same | matched |
| Castle Drakan camera | moveto (3553,3328) h1900 100/100, lookat (3553,3358) h450, reset | same (cutscene await) | matched |

Sounds by id (whole session): the fights were silent before (`attack_sound`/`defend_sound` null in
the generated block); now vampire_attack 879 and vampire_hit 884 (juvinates, both rooms), human_hit
513 + human_death 512 (Gadderanks), unarmed_kick 2565 + vampire_hit as area sounds (Veliaf),
vampire_attack / wood_armour_2 2571 / stabsword_stab 2549 as area sounds (the juvinates on Ivan,
Ivan's punch). The juvinates' defend seq was the generated `human_unarmedblock`; recorded
`human_unarmed_def` (425). Moved into `configs/myreque2.npc`, the generated lines deleted.

Npc block kinds (`npc_block_inventory.py`): Ivan's hits/seqs/headbar, the Mist's face and
`mist_disappear` (1262, 9 ticks after it appears, 5/5), Veliaf's slashes and say, Gadderanks'
transformation all present. Added from this audit: the Mist (`timer=9`, its face and reset), the four
sitting villagers' `sitting_eating` every 10..14 ticks (322 recorded; `timer=12` + `[ai_timer]`),
Gadderanks' `enabled_ops 31` + `level 0` with his change. He does not die ([4510]: blow, fall,
transformation, ops and level on one tick; talked to at [4520]): `param=death_script,yes`, so the
engine's own death no longer runs under the change (it hid his menu a tick on and freed him two
ticks after that). Still MISSING only where the test does
not go: route 2's juvinates are never hit by the player; several villagers are not talked to.

Movement (the generated `wander 5` default on every Burgh npc, measured against 4634's npc info):
most villagers do wander (100..360 steps, up to 3..8 tiles), so the default stays. Set in
`myreque2.npc` from the recording: Cornelius the banker (`burgh_actual_bank_teller_multinpc` and
its rung) and the fight's Gadderanks never move (0 steps; `none`); the three in the store, Aurel,
Gabriela and Ileana stay within 2..3 tiles (`wander 2`). The banker's wander was why the player
could not talk to him: he drifted to 3492,3211, three tiles across the booth, outside the recorded
2-tile reach, and the route around the counter fails.

## Open

- Ivan's flight is fitted to two lost runs (flees the tick after his bar is 8/30; juvinates go for him
  until the player engages them); his punches deal no damage (none recorded).
- The two post-Gadderanks answers ("How do you like your town?", "What's happening around here?")
  are from index 3043 (rev 231, one citizen, one sample each).
- Florin's tomato cadence is fitted (one throw per 10 ticks on the gate/talk/chest); the varbit's
  decay ([746], [967]) is not modelled.
- The cellar's finds are fixed at piles 2, 5, 8, 10 (one session); the rubble tiles are a random
  pick from the 22 two copies used.
- The silver panel's Ivandis cell is not shown/hidden by the server; Modern day Morytania's spreads
  after the eighth are unrecorded; the tomb's exit says "too small" (recorded) with no other way out.
- Commit: `quest_routequest/scripts/routequest_hideout.rs2` (the Radigad call, added by the In
  Search session, 6 lines) goes in the same commit as these files.
