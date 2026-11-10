# The Giant Dwarf and the other content in RSProx index 4488-4502

Recordings: index 4489 (Ratcatchers and the Varrock sewers, before the start), 4491 (start, ride, arrest, Veldaban,
town), 4492 (Blasidar, boots, library), 4493 (town, palace), 4494 (clothes, boots, axe, Thurgo, the head), 4495, 4496
and 4500 (Consortium deliveries, every company's refusals), 4501 (joining, the meeting, completion), 4502 and the
first ticks of 4503 (after the quest). 4488 and 4490 are short sessions with no quest content. All rev 242.1, `--bas`
transcripts. 4498 (Yama) belongs to session 3draster-6a; 4503's Big Chompy Bird Hunting and farming to 3draster-a4.

Sources beyond the recordings: `Transcript:The_Giant_Dwarf` and `Transcript:Kiss_the_frog`
(oldschool.runescape.wiki, `?action=raw`, 2026-11-09); `The_Giant_Dwarf` for the points rules.

The quest was built from the wiki: stages 0..28 and 50, one points value, a hidden 10-stage clothes machine, Riki
taking everything at once, and a teleport in place of the ride. It has been rebuilt on Jagex's values.

Tests: `test/quests/giantdwarf.lua` (the whole quest on foot, Knight's Sword not done), and
`test/quests/_giantdwarf_recorded.lua` (the recorded flow by teleport, with a tick log, and both rides).

## Stages and vars

| value | R | ours | verdict |
|---|---|---|---|
| quest 5 | 4491 [331], on the continue of "Well, let's not waste any more time then!" | same | matched |
| quest 10 | 4491 [574], on the continue of "You are a good human" | same | matched |
| quest 20 | 4492 [526], the last page of Blasidar's list | same | matched |
| quest 30 | 4494 [4717], the last page of the head talk, with `statue_invis` 0 | same | matched |
| quest 40 | 4501 [382], "Yes! Long live ..." | same | matched |
| quest 50 | 4501 [1099], with `red_traders_gone`, `red_axe_gone` 1, `model_state`, axe and pair bits 0 | same | matched |
| `original_company`, `current_company` | 9 at [548] (none); current at the join, original at the meeting | same | matched |
| `veldaban_introduced`, `sculptor_introduced`, `cousin_introduced`, `pie_given`, `vermundi_givenbook`, `gotpair` | on their recorded pages | same | matched |
| `model_state` | bit 0 boots, 1 clothes, 2 axe (1, 3, 7) | same | fixed: was clothes first |
| `statue_invis`, `cutscene_guard_visible` | 4491 [514], [544] | same | matched |

Server-only state is varps 9540..9558 (`transmit=no`): points per company, the one task in hand, and the steps Jagex's
client does not need (Saro asked, Dromund asked, machine, book hint, Santiri's axe, Thurgo asked).

## The Consortium's hidden score

Wiki rules, checked against the Green Gemstone's run (index 4495..4501): an ore delivery +20 capped at the secretary's
75, a bar delivery +12, a refused or lapsed task -2. 75 opens the director, 100 lets the player join. Four deliveries
with three penalties read 74 ("impressed", 4500 [642], refused); the fifth reads 94 and the cap holds it at 75
("Why, naturally", [851]); three bar deliveries join, one lapsed task between ([4011]). Below 75 the director brushes
the player off, so a refusal at exactly 75 needs another ore delivery.

Task clock, from every recorded offer: ores 10 + n minutes, bars 10 + 2n. Unverified: the "impressed" boundary (set at
50; recorded only that 74 is impressed and one delivery is not).

## Dromund

He is not a gaze timer. He paces a rectangle (2835..2840, 10221..10225), a tile a tick with a tick's pause at each
corner, 630 steps in 4492. He sees what is ahead of him: unseen when his heading dotted with the way to the player is
-2 or less. 13 of 13 recorded grabs and attempts fit, left boot and right. The window needs the player on 2836,10227;
2836,10228 is "try moving a little closer"; casting from inside the room is "casting spells in my house".

## Rides

The trapdoor and the Grand Exchange cart are forced rides, built from the recorded exact moves (4494 [2816..2838],
4495 [1294..1316]). Only the front cart of a track rides; the others say "You should take the cart at the front of
the queue."; track 2's say "You don't have an appropriate ticket". The trapdoor lands at the station 2909,10174, not
Veldaban's headquarters.

## Dialogue pages

973 distinct recorded pages across the sessions. Every page outside the two exclusions below is in the scripts,
counting company and task substitutions. Excluded: 4503's chompy pages (3draster-a4's), and pages whose speaker is the
gardeners' Pay reply, which is in `farming_gardener.rs2` with `<~sir_madam>`.

Rows no recording chose, taken from `Transcript:The_Giant_Dwarf` with moods unverified: refusing Veldaban and Blasidar,
Santiri's held-axe answers and the "lost interest" reply, the not-Knight's-Sword route (Santiri, the Librarian, Reldo),
smithing tiers, the directors' and secretaries' refusals and "Thank you for your time", "No, I need more time", and
"I do NOT want to attend" with its minutes route.

## Scenes

Both scenes' pages advance by themselves (`continue` is empty on every recorded page: 4491 [478..532], 4501
[775..1042]). The boat's camera, fades and the crash overlay are copied; the traders the shots pass are the world's.
The meeting is the map's own room at 1984,4480 with a 27-actor cast (the raw transcript has no instance tiles), and the
player's chosen company fills the two lines that name it.

## Every action

| action | count | first | verdict |
|---|---|---|---|
| `[if_buttont] selectcom=inventory:items (149:0), selectedselectedobj=ch` | 1 | 4494[4584] | client UI, the recorder's own |
| `[if_buttont] selectcom=inventory:items (149:0), selectedselectedobj=dw` | 1 | 4494[1331] | client UI, the recorder's own |
| `[if_buttont] selectcom=inventory:items (149:0), selectedselectedobj=sa` | 1 | 4494[3684] | client UI, the recorder's own |
| `[if_buttont] selectcom=inventory:items (149:0), selectedselectedobj=sa` | 2 | 4494[4580] | client UI, the recorder's own |
| `area_task:taskbox (259:2), op=1` | 1 | 4489[194] | client UI, the recorder's own |
| `bankmain:depositinv (12:47), op=1` | 4 | 4500[3470] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:depositworn (12:49), op=1` | 1 | 4502[169] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=2dosestamina (12629), op=1` | 1 | 4500[1972] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=4dosestamina (12625), op=1` | 11 | 4489[154] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=4dosestamina (12625), op=3` | 1 | 4501[525] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=basket_banana_5 (5416), op=1` | 1 | 4502[189] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=basket_orange_5 (5396), op=1` | 1 | 4502[189] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=bronze_bar (2349), op=1` | 5 | 4501[149] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=chisel (1755), op=1` | 1 | 4494[4568] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=coal (453), op=1` | 5 | 4494[1566] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=coal (453), op=4` | 1 | 4496[386] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=dibber (5343), op=1` | 1 | 4502[181] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=gold_bar (2357), op=1` | 1 | 4501[180] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=gold_bar (2357), op=3` | 1 | 4500[1642] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=iron_bar (2351), op=1` | 1 | 4494[3908] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=iron_bar (2351), op=3` | 1 | 4501[153] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=lawrune (563), op=1` | 1 | 4489[139] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=logs (1511), op=1` | 1 | 4494[1567] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=lunar_tablet_catherby_teleport (24961), op` | 6 | 4502[393] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=mithril_ore (447), op=3` | 1 | 4500[1456] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=mithril_ore (447), op=5` | 1 | 4500[1448] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=plantpot_curry_sapling (5499), op=1` | 5 | 4502[186] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=plantpot_maple_sapling (5372), op=1` | 4 | 4502[198] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=poh_tablet_ardougneteleport (8011), op=4` | 1 | 4489[397] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=poh_tablet_camelotteleport (8010), op=3` | 1 | 4491[77] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=poh_tablet_faladorteleport (8009), op=1` | 10 | 4494[2677] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=poh_tablet_lumbridgeteleport (8008), op=1` | 3 | 4488[36] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=poh_tablet_lumbridgeteleport (8008), op=4` | 1 | 4502[537] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=poh_tablet_varrockteleport (8007), op=1` | 6 | 4500[3923] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=poh_tablet_varrockteleport (8007), op=4` | 2 | 4489[135] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=rake (5341), op=1` | 1 | 4502[180] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=raw_trout (335), op=1` | 3 | 4500[3631] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=raw_trout (335), op=4` | 1 | 4489[171] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=raw_trout (335), op=7` | 1 | 4500[3483] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=sapphire (1607), op=1` | 4 | 4494[3577] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=silver_ore (442), op=1` | 4 | 4495[1359] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=silver_ore (442), op=3` | 1 | 4500[424] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=spade (952), op=1` | 1 | 4502[180] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=steel_bar (2353), op=1` | 2 | 4501[166] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=steel_bar (2353), op=3` | 1 | 4501[155] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:items (12:12), obj=uncut_sapphire (1623), op=1` | 2 | 4494[4573] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:note (12:25), op=1` | 2 | 4502[184] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:quantity1 (12:29), op=1` | 7 | 4489[152] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:quantity5 (12:31), op=1` | 1 | 4501[178] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:quantityall (12:37), op=1` | 6 | 4489[137] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:search (12:42), op=1` | 6 | 4494[3542] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:tabs (12:10), obj=4doseprayerrestore (2434), op=1` | 7 | 4489[151] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:tabs (12:10), obj=dibber (5343), op=1` | 1 | 4502[177] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:tabs (12:10), obj=iron_ore (440), op=1` | 13 | 4494[1564] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:tabs (12:10), obj=lobster (379), op=1` | 2 | 4489[164] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankmain:tabs (12:10), obj=poh_tablet_kourendteleport (28790), op=1` | 8 | 4488[34] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=2dose1strength (117), op=2` | 1 | 4496[393] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=4dose1defense (2432), op=2` | 1 | 4489[158] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=4dosestamina (12625), op=2` | 1 | 4500[1975] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=airrune (556), op=2` | 1 | 4501[176] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=amulet_of_defence (1729), op=2` | 1 | 4501[129] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=amulet_of_ghostspeak (552), op=2` | 1 | 4489[146] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=bronze_bar (2349), op=2` | 1 | 4501[518] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=bucket_empty (1925), op=2` | 1 | 4496[394] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=cert_bronze_bar (2350), op=2` | 1 | 4500[410] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=cert_gold_bar (2358), op=2` | 1 | 4500[1951] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=cert_iron_bar (2352), op=2` | 1 | 4500[408] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=cert_iron_bar (2352), op=6` | 1 | 4494[3907] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=cert_mithril_bar (2360), op=2` | 1 | 4500[407] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=cert_mithril_ore (448), op=5` | 2 | 4500[1444] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=cert_plantpot_curry_sapling (12949), op=2` | 1 | 4502[188] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=cert_sapphire (1608), op=6` | 1 | 4494[3575] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=cert_silver_bar (2356), op=2` | 1 | 4500[1950] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=cert_silver_ore (443), op=2` | 1 | 4500[406] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=cert_silver_ore (443), op=6` | 1 | 4495[1358] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=cert_steel_bar (2354), op=2` | 1 | 4500[410] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=chisel (1755), op=2` | 1 | 4501[170] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=coal (453), op=2` | 2 | 4500[1450] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=copper_ore (436), op=2` | 1 | 4500[1453] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=desert_boots (1837), op=2` | 1 | 4489[128] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=desert_robe (1835), op=2` | 1 | 4489[128] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=desert_shirt (1833), op=2` | 1 | 4489[129] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=dwarf_meeting_notes1 (5066), op=2` | 1 | 4502[164] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=dwarf_trousers1 (5036), op=2` | 1 | 4501[157] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=earth_battlestaff (1399), op=2` | 1 | 4501[171] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=firerune (554), op=2` | 1 | 4501[175] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=gold_bar (2357), op=2` | 2 | 4500[1952] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=gold_ore (444), op=2` | 1 | 4500[1452] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=growncatobject (1561), op=2` | 1 | 4490[110] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=ics_little_amulet_of_catspeak (4677), op=9` | 1 | 4501[126] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=ics_little_bookofembalming (4686), op=2` | 1 | 4489[150] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=iron_bar (2351), op=2` | 1 | 4501[517] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=jug_wine (1993), op=2` | 1 | 4496[394] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=knife (946), op=2` | 1 | 4496[397] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=lawrune (563), op=2` | 1 | 4501[174] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=lobster (379), op=4` | 1 | 4489[161] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=macro_frog_token (6183), op=2` | 1 | 4500[1955] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=mindrune (558), op=2` | 1 | 4501[175] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=mithril_ore (447), op=2` | 1 | 4501[123] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=mithril_ore (447), op=5` | 1 | 4500[1640] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=poh_tablet_camelotteleport (8010), op=5` | 1 | 4501[163] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=poh_tablet_lumbridgeteleport (8008), op=2` | 1 | 4500[409] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=raw_trout (335), op=5` | 1 | 4490[113] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=ring_of_dueling_5 (2558), op=2` | 1 | 4501[158] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=silver_ore (442), op=2` | 1 | 4500[1454] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=softclay (1761), op=2` | 1 | 4489[141] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=steel_bar (2353), op=2` | 1 | 4502[165] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=strung_emerald_amulet (1696), op=2` | 1 | 4501[128] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=tinderbox (590), op=2` | 1 | 4496[396] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=water_skin1 (1829), op=2` | 1 | 4496[399] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `bankside:items (15:3), obj=water_skin4 (1823), op=2` | 1 | 4489[143] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `book:close_button (392:7), op=1` | 1 | 4492[1289] | client UI, the recorder's own |
| `ge_offers:back (465:4), op=1` | 5 | 4500[382] | generic: Grand Exchange (interface_grandexchange) |
| `ge_offers:collectall (465:6), op=1` | 5 | 4489[236] | generic: Grand Exchange (interface_grandexchange) |
| `ge_offers:details_collect (465:24), obj=amulet_of_defence (1729), op=1` | 1 | 4495[1120] | generic: Grand Exchange (interface_grandexchange) |
| `ge_offers:details_collect (465:24), obj=coins (995), op=2` | 6 | 4489[270] | generic: Grand Exchange (interface_grandexchange) |
| `ge_offers:details_collect (465:24), obj=iron_bar (2351), op=1` | 1 | 4494[3900] | generic: Grand Exchange (interface_grandexchange) |
| `ge_offers:details_collect (465:24), obj=lawrune (563), op=2` | 1 | 4489[269] | generic: Grand Exchange (interface_grandexchange) |
| `ge_offers:details_collect (465:24), obj=mithril_ore (447), op=1` | 1 | 4500[1436] | generic: Grand Exchange (interface_grandexchange) |
| `ge_offers:details_collect (465:24), obj=sapphire (1607), op=1` | 1 | 4494[3567] | generic: Grand Exchange (interface_grandexchange) |
| `ge_offers:details_collect (465:24), obj=silver_ore (442), op=1` | 1 | 4495[1349] | generic: Grand Exchange (interface_grandexchange) |
| `ge_offers:details_status (465:23), op=1` | 7 | 4500[386] | generic: Grand Exchange (interface_grandexchange) |
| `ge_offers:index_1 (465:8), op=1` | 1 | 4500[366] | generic: Grand Exchange (interface_grandexchange) |
| `ge_offers:index_2 (465:9), op=1` | 6 | 4494[3471] | generic: Grand Exchange (interface_grandexchange) |
| `ge_offers:index_3 (465:10), op=1` | 23 | 4489[237] | generic: Grand Exchange (interface_grandexchange) |
| `ge_offers:index_6 (465:13), op=1` | 1 | 4500[357] | generic: Grand Exchange (interface_grandexchange) |
| `ge_offers:index_7 (465:14), op=1` | 5 | 4494[3494] | generic: Grand Exchange (interface_grandexchange) |
| `ge_offers:setup (465:26), op=1` | 99 | 4489[260] | generic: Grand Exchange (interface_grandexchange) |
| `ge_offers:setup_confirm (465:30), op=1` | 22 | 4489[267] | generic: Grand Exchange (interface_grandexchange) |
| `inventory:items (149:0), obj=1doseprayerrestore (143), op=2` | 1 | 4489[211] | client UI, the recorder's own |
| `inventory:items (149:0), obj=1dosestamina (12631), op=2` | 7 | 4492[567] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=2dosestamina (12629), op=2` | 7 | 4491[120] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=3dosestamina (12627), op=2` | 6 | 4489[620] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=4doseprayerrestore (2434), op=2` | 1 | 4489[215] | client UI, the recorder's own |
| `inventory:items (149:0), obj=4dosestamina (12625), op=2` | 6 | 4489[314] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=amulet_of_defence (1729), op=3` | 2 | 4495[1125] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=amulet_of_glory_1 (1706), op=3` | 1 | 4491[54] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=amulet_of_glory_2 (1708), op=3` | 2 | 4488[46] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=beer_glass (1919), op=7` | 1 | 4492[360] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=coal (453), op=7` | 1 | 4494[1610] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=dwarf_battleaxe_new (5059), op=10` | 1 | 4494[4160] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=dwarf_battleaxe_new (5059), op=2` | 2 | 4494[4148] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=dwarf_battleaxe_new (5059), op=7` | 3 | 4494[4111] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=dwarf_battleaxe_old (5056), op=10` | 1 | 4494[3581] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=dwarf_battleaxe_old (5056), op=2` | 3 | 4494[2619] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=dwarf_battleaxe_old (5056), op=7` | 2 | 4494[2631] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=dwarf_battleaxe_sapphires (5058), op=10` | 2 | 4494[3691] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=dwarf_battleaxe_sharpened (5057), op=2` | 1 | 4494[4504] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=dwarf_clothes (5067), op=2` | 1 | 4494[1859] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=dwarf_clothes (5067), op=7` | 2 | 4494[1865] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=dwarf_library_book (5065), op=7` | 4 | 4494[1306] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=dwarf_meeting_notes1 (5066), op=2` | 1 | 4501[1114] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=dwarf_perfect_left_boot (5062), op=2` | 2 | 4492[1577] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=dwarf_perfect_left_boot (5062), op=7` | 1 | 4492[1828] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=dwarf_perfect_pair_of_boots (5064), op=2` | 1 | 4492[1731] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=dwarf_perfect_pair_of_boots (5064), op=7` | 2 | 4492[1770] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=dwarf_trousers1 (5036), op=3` | 1 | 4494[1427] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=emerald_ring (1639), op=3` | 1 | 4496[169] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=gnome_boots_green (628), op=3` | 1 | 4495[853] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=gnome_hat_green (658), op=3` | 1 | 4495[855] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=gnome_robebottoms_green (648), op=3` | 1 | 4495[854] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=gnome_robetop_green (638), op=3` | 1 | 4495[853] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=godwars_dwarven_book (11677), op=2` | 1 | 4492[1121] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=godwars_dwarven_book (11677), op=7` | 1 | 4492[1302] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=growncatobject (1561), op=7` | 2 | 4489[70] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=ics_little_amulet_of_catspeak (4677), op=` | 3 | 4489[64] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=iron_bar (2351), op=7` | 1 | 4494[4058] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=jewl_emerald_bracelet (11076), op=3` | 1 | 4496[170] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=lunar_tablet_catherby_teleport (24961), o` | 1 | 4502[396] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=plantpot_empty (5350), op=7` | 8 | 4502[269] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=poh_tablet_ardougneteleport (8011), op=2` | 2 | 4489[412] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=poh_tablet_camelotteleport (8010), op=2` | 1 | 4491[86] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=poh_tablet_faladorteleport (8009), op=2` | 5 | 4494[3248] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=poh_tablet_lumbridgeteleport (8008), op=2` | 1 | 4488[38] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=poh_tablet_varrockteleport (8007), op=2` | 12 | 4489[590] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=ring_of_dueling_5 (2558), op=3` | 1 | 4495[1021] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `inventory:items (149:0), obj=strung_emerald_amulet (1696), op=3` | 2 | 4496[171] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `letterscroll:com_104 (70:104), op=1` | 1 | 4501[1133] | quest list, journal, reward scroll, rating, minutes (gdwarf_journal, gdwarf_meeting) |
| `logout:logout (182:8), op=1` | 14 | 4488[77] | client UI, the recorder's own |
| `magic_spellbook:varrock_teleport (218:26), op=1` | 1 | 4489[179] | client UI, the recorder's own |
| `orbs:runbutton (160:28), op=1` | 2 | 4492[2565] | client UI, the recorder's own |
| `orbs:worldmap (160:55), op=2` | 4 | 4491[20] | client UI, the recorder's own |
| `orbs:xp_drops (160:6), op=1` | 2 | 4502[424] | client UI, the recorder's own |
| `quest_rating:5 (983:10), op=1` | 1 | 4501[1112] | quest list, journal, reward scroll, rating, minutes (gdwarf_journal, gdwarf_meeting) |
| `questjournal:close (119:8), op=1` | 59 | 4491[714] | quest list, journal, reward scroll, rating, minutes (gdwarf_journal, gdwarf_meeting) |
| `questjournal_overview:close (782:9), op=1` | 26 | 4489[49] | quest list, journal, reward scroll, rating, minutes (gdwarf_journal, gdwarf_meeting) |
| `questjournal_overview:content_inner (782:7), op=1` | 2 | 4489[90] | quest list, journal, reward scroll, rating, minutes (gdwarf_journal, gdwarf_meeting) |
| `questlist:list (399:7), op=2` | 99 | 4489[46] | quest list, journal, reward scroll, rating, minutes (gdwarf_journal, gdwarf_meeting) |
| `questscroll:close_button (153:16), op=1` | 1 | 4501[1108] | quest list, journal, reward scroll, rating, minutes (gdwarf_journal, gdwarf_meeting) |
| `shopmain:items (300:16), obj=adamant_arrow (890), op=1` | 1 | 4491[952] | generic: shop (shop/keldagrim*, `~openshop`) |
| `shopmain:items (300:16), obj=adamant_med_helm (1145), op=1` | 1 | 4491[999] | generic: shop (shop/keldagrim*, `~openshop`) |
| `shopmain:items (300:16), obj=adamnt_warhammer (1345), op=1` | 1 | 4494[2339] | generic: shop (shop/keldagrim*, `~openshop`) |
| `shopmain:items (300:16), obj=black_longsword (1297), op=1` | 1 | 4491[951] | generic: shop (shop/keldagrim*, `~openshop`) |
| `shopmain:items (300:16), obj=copper_ore (436), op=3` | 1 | 4500[560] | generic: shop (shop/keldagrim*, `~openshop`) |
| `shopmain:items (300:16), obj=dwarf_jumper1 (5024), op=1` | 1 | 4494[1421] | generic: shop (shop/keldagrim*, `~openshop`) |
| `shopmain:items (300:16), obj=dwarf_shirt1 (5030), op=1` | 1 | 4494[1420] | generic: shop (shop/keldagrim*, `~openshop`) |
| `shopmain:items (300:16), obj=dwarf_trousers1 (5036), op=2` | 1 | 4494[1425] | generic: shop (shop/keldagrim*, `~openshop`) |
| `shopmain:items (300:16), obj=gnome_boots_green (628), op=2` | 1 | 4495[841] | generic: shop (shop/keldagrim*, `~openshop`) |
| `shopmain:items (300:16), obj=gnome_hat_green (658), op=2` | 1 | 4495[836] | generic: shop (shop/keldagrim*, `~openshop`) |
| `shopmain:items (300:16), obj=gnome_robebottoms_green (648), op=2` | 1 | 4495[843] | generic: shop (shop/keldagrim*, `~openshop`) |
| `shopmain:items (300:16), obj=gnome_robetop_green (638), op=2` | 1 | 4495[838] | generic: shop (shop/keldagrim*, `~openshop`) |
| `shopmain:items (300:16), obj=gold_ore (444), op=3` | 1 | 4500[742] | generic: shop (shop/keldagrim*, `~openshop`) |
| `shopmain:items (300:16), obj=maple_longbow (851), op=1` | 1 | 4491[953] | generic: shop (shop/keldagrim*, `~openshop`) |
| `shopmain:items (300:16), obj=maple_shortbow (853), op=1` | 1 | 4491[955] | generic: shop (shop/keldagrim*, `~openshop`) |
| `shopmain:items (300:16), obj=mithril_arrow (888), op=1` | 1 | 4491[953] | generic: shop (shop/keldagrim*, `~openshop`) |
| `shopmain:items (300:16), obj=poh_condensed_gold (26266), op=1` | 2 | 4493[240] | generic: shop (shop/keldagrim*, `~openshop`) |
| `shopmain:items (300:16), obj=poh_magic_crystal (8788), op=1` | 2 | 4493[244] | generic: shop (shop/keldagrim*, `~openshop`) |
| `shopmain:items (300:16), obj=rune_longsword (1303), op=1` | 1 | 4491[947] | generic: shop (shop/keldagrim*, `~openshop`) |
| `side_journal:quest_list (629:10), op=1` | 1 | 4489[221] | client UI, the recorder's own |
| `side_journal:task_list (629:18), op=1` | 1 | 4489[188] | client UI, the recorder's own |
| `skill_guide_v2:close (860:4), op=1` | 3 | 4500[3332] | client UI, the recorder's own |
| `skill_guide_v2:tabs (860:7), op=1` | 4 | 4500[3324] | client UI, the recorder's own |
| `stats:cooking (320:20), op=2` | 1 | 4500[3313] | client UI, the recorder's own |
| `stats:farming (320:23), op=2` | 2 | 4502[302] | client UI, the recorder's own |
| `welcome_screen:play (378:72), op=1` | 12 | 4488[3] | client UI, the recorder's own |
| `worldmap:close (595:38), op=1` | 6 | 4489[96] | client UI, the recorder's own |
| `wornitems:slot10 (387:23), op=1` | 1 | 4489[120] | client UI, the recorder's own |
| `wornitems:slot12 (387:24), op=1` | 1 | 4495[1015] | client UI, the recorder's own |
| `wornitems:slot12 (387:24), op=3` | 3 | 4488[19] | client UI, the recorder's own |
| `wornitems:slot2 (387:17), op=1` | 1 | 4495[1017] | client UI, the recorder's own |
| `wornitems:slot2 (387:17), op=4` | 1 | 4489[708] | client UI, the recorder's own |
| `wornitems:slot3 (387:18), op=1` | 1 | 4495[1017] | client UI, the recorder's own |
| `wornitems:slot4 (387:19), op=1` | 1 | 4489[119] | client UI, the recorder's own |
| `wornitems:slot7 (387:21), op=1` | 1 | 4489[120] | client UI, the recorder's own |
| `oploc1 id=anvil (2097)` | 1 | 4494[4358] | generic or other quest content the session passes (bound; see Open for any unverified row) |
| `oploc1 id=ardougneshipplank_off (2088)` | 2 | 4502[478] | generic: farming and travel; 4502 Pay exists in farming_gardener.rs2 (a4 owns 4503) |
| `oploc1 id=blast_bank_chest (26707)` | 7 | 4494[1558] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `oploc1 id=blast_furnace_pipes2_broken (9117)` | 1 | 4500[719] | generic or other quest content the session passes (bound; see Open for any unverified row) |
| `oploc1 id=blast_furnace_stairs_up (9138)` | 7 | 4494[1573] | generic: stairs, ladders, spirit trees (ladders_stairs, area_gnome/spirit_tree) |
| `oploc1 id=castlewars_bankchest (4483)` | 3 | 4488[29] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `oploc1 id=dwarf_cavewall_tunnel (5973)` | 1 | 4491[278] | Keldagrim travel: rides from 4494/4495 (keldagrim_travel.rs2) |
| `oploc1 id=dwarf_keldagrim_bookcase (6091)` | 11 | 4492[1005] | the quest (gdwarf_*) |
| `oploc1 id=dwarf_keldagrim_bookcase_ladder (6092)` | 9 | 4492[1023] | generic: stairs, ladders, spirit trees (ladders_stairs, area_gnome/spirit_tree) |
| `oploc1 id=dwarf_keldagrim_door (6106)` | 19 | 4491[716] | generic: doors (doors.loc categories) |
| `oploc1 id=dwarf_keldagrim_door_interior (6104)` | 6 | 4491[825] | generic: doors (doors.loc categories) |
| `oploc1 id=dwarf_keldagrim_door_interior_factory (6975)` | 24 | 4494[1547] | generic: doors (doors.loc categories) |
| `oploc1 id=dwarf_keldagrim_door_interior_factory_open (6976)` | 1 | 4494[1577] | generic: doors (doors.loc categories) |
| `oploc1 id=dwarf_keldagrim_door_interior_open (6105)` | 1 | 4491[832] | generic: doors (doors.loc categories) |
| `oploc1 id=dwarf_keldagrim_door_interior_palace (6100)` | 16 | 4494[688] | generic: doors (doors.loc categories) |
| `oploc1 id=dwarf_keldagrim_door_interior_palace_open (6101)` | 4 | 4493[431] | generic: doors (doors.loc categories) |
| `oploc1 id=dwarf_keldagrim_door_interior_poor (6102)` | 12 | 4494[1544] | generic: doors (doors.loc categories) |
| `oploc1 id=dwarf_keldagrim_door_interior_poor_open (6103)` | 2 | 4500[1846] | generic: doors (doors.loc categories) |
| `oploc1 id=dwarf_keldagrim_door_open (6107)` | 2 | 4491[761] | generic: doors (doors.loc categories) |
| `oploc1 id=dwarf_keldagrim_door_ornate (6110)` | 22 | 4491[784] | generic: doors (doors.loc categories) |
| `oploc1 id=dwarf_keldagrim_door_ornate_open (6111)` | 5 | 4491[855] | generic: doors (doors.loc categories) |
| `oploc1 id=dwarf_keldagrim_door_palace (6114)` | 2 | 4494[1058] | generic: doors (doors.loc categories) |
| `oploc1 id=dwarf_keldagrim_door_poor (6108)` | 7 | 4492[368] | generic: doors (doors.loc categories) |
| `oploc1 id=dwarf_keldagrim_factory_door (6977)` | 23 | 4494[1542] | generic: doors (doors.loc categories) |
| `oploc1 id=dwarf_keldagrim_factory_stairs (9084)` | 13 | 4494[1552] | generic: stairs, ladders, spirit trees (ladders_stairs, area_gnome/spirit_tree) |
| `oploc1 id=dwarf_keldagrim_stairs_lower (6085)` | 14 | 4491[807] | generic: stairs, ladders, spirit trees (ladders_stairs, area_gnome/spirit_tree) |
| `oploc1 id=dwarf_keldagrim_stairs_upper (6086)` | 9 | 4491[843] | generic: stairs, ladders, spirit trees (ladders_stairs, area_gnome/spirit_tree) |
| `oploc1 id=dwarf_keldagrim_wide_stairs_3_lower (6089)` | 2 | 4494[1047] | generic: stairs, ladders, spirit trees (ladders_stairs, area_gnome/spirit_tree) |
| `oploc1 id=dwarf_keldagrim_wide_stairs_3_upper (6090)` | 1 | 4494[1074] | generic: stairs, ladders, spirit trees (ladders_stairs, area_gnome/spirit_tree) |
| `oploc1 id=dwarf_keldagrim_wide_stairs_lower (6087)` | 33 | 4493[455] | generic: stairs, ladders, spirit trees (ladders_stairs, area_gnome/spirit_tree) |
| `oploc1 id=dwarf_keldagrim_wide_stairs_upper (6088)` | 25 | 4494[1123] | generic: stairs, ladders, spirit trees (ladders_stairs, area_gnome/spirit_tree) |
| `oploc1 id=dwarf_keldagrim_wooden_boxes (6176)` | 4 | 4491[763] | the quest (gdwarf_*) |
| `oploc1 id=exchange_bank_wall_exchange (10061)` | 4 | 4494[3881] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `oploc1 id=fai_varrock_ladder (11794)` | 1 | 4489[657] | generic: stairs, ladders, spirit trees (ladders_stairs, area_gnome/spirit_tree) |
| `oploc1 id=fai_varrock_laddertop (11795)` | 1 | 4489[670] | generic: stairs, ladders, spirit trees (ladders_stairs, area_gnome/spirit_tree) |
| `oploc1 id=fai_varrock_poor_door (11780)` | 2 | 4489[651] | generic: doors (doors.loc categories) |
| `oploc1 id=fai_varrock_poor_door_open (11778)` | 1 | 4489[684] | generic: doors (doors.loc categories) |
| `oploc1 id=farming_fruit_tree_patch_1 (7962)` | 3 | 4502[297] | generic: farming and travel; 4502 Pay exists in farming_gardener.rs2 (a4 owns 4503) |
| `oploc1 id=farming_fruit_tree_patch_2 (7963)` | 1 | 4502[758] | generic: farming and travel; 4502 Pay exists in farming_gardener.rs2 (a4 owns 4503) |
| `oploc1 id=farming_fruit_tree_patch_3 (7964)` | 1 | 4502[487] | generic: farming and travel; 4502 Pay exists in farming_gardener.rs2 (a4 owns 4503) |
| `oploc1 id=farming_fruit_tree_patch_4 (7965)` | 1 | 4502[431] | generic: farming and travel; 4502 Pay exists in farming_gardener.rs2 (a4 owns 4503) |
| `oploc1 id=farming_tree_patch_1 (8388)` | 1 | 4502[646] | generic: farming and travel; 4502 Pay exists in farming_gardener.rs2 (a4 owns 4503) |
| `oploc1 id=farming_tree_patch_2 (8389)` | 1 | 4502[567] | generic: farming and travel; 4502 Pay exists in farming_gardener.rs2 (a4 owns 4503) |
| `oploc1 id=farming_tree_patch_3 (8390)` | 1 | 4502[230] | generic: farming and travel; 4502 Pay exists in farming_gardener.rs2 (a4 owns 4503) |
| `oploc1 id=farming_tree_patch_5 (19147)` | 2 | 4502[346] | generic: farming and travel; 4502 Pay exists in farming_gardener.rs2 (a4 owns 4503) |
| `oploc1 id=ge_keldagrim_trapdoor (16168)` | 2 | 4489[871] | Keldagrim travel: rides from 4494/4495 (keldagrim_travel.rs2) |
| `oploc1 id=grandtree_ladderbottom (4458)` | 2 | 4495[796] | generic: stairs, ladders, spirit trees (ladders_stairs, area_gnome/spirit_tree) |
| `oploc1 id=guidorgaterclosed (2051)` | 1 | 4489[622] | generic: doors (doors.loc categories) |
| `oploc1 id=keldagrim_train_cart (7028)` | 26 | 4494[2753] | Keldagrim travel: rides from 4494/4495 (keldagrim_travel.rs2) |
| `oploc1 id=manholeclosed (881)` | 1 | 4489[335] | generic: stairs, ladders, spirit trees (ladders_stairs, area_gnome/spirit_tree) |
| `oploc1 id=manholeopen (882)` | 1 | 4489[341] | generic: stairs, ladders, spirit trees (ladders_stairs, area_gnome/spirit_tree) |
| `oploc1 id=poshdoor (1540)` | 2 | 4489[552] | generic: doors (doors.loc categories) |
| `oploc1 id=spiralstairs_wooden (16675)` | 2 | 4502[373] | generic: stairs, ladders, spirit trees (ladders_stairs, area_gnome/spirit_tree) |
| `oploc1 id=spirittree_small (1295)` | 8 | 4495[735] | generic: stairs, ladders, spirit trees (ladders_stairs, area_gnome/spirit_tree) |
| `oploc1 id=stronghold_ent (1294)` | 1 | 4495[920] | generic: stairs, ladders, spirit trees (ladders_stairs, area_gnome/spirit_tree) |
| `oploc1 id=treedoorr (1968)` | 1 | 4495[794] | generic: doors (doors.loc categories) |
| `oploc1 id=treegnomelooserailing (2186)` | 1 | 4502[743] | generic: doors (doors.loc categories) |
| `oploc1 id=trollromance_stronghold_exit_tunnel (5008)` | 2 | 4491[267] | Keldagrim travel: rides from 4494/4495 (keldagrim_travel.rs2) |
| `oploc1 id=vc_keldagrim_stairs_upper (10288)` | 2 | 4492[371] | generic: stairs, ladders, spirit trees (ladders_stairs, area_gnome/spirit_tree) |
| `oploc1 id=vc_ladder_down (10322)` | 1 | 4489[581] | generic: stairs, ladders, spirit trees (ladders_stairs, area_gnome/spirit_tree) |
| `oploc1 id=vc_manhole_open (10321)` | 4 | 4489[690] | generic: stairs, ladders, spirit trees (ladders_stairs, area_gnome/spirit_tree) |
| `oploc2 id=bankbooth (10355)` | 1 | 4502[377] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `oploc2 id=dwarf_keldagrim_bankbooth (6084)` | 3 | 4494[2664] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `oploc2 id=dwarf_market_gems (6162)` | 1 | 4500[2374] | generic or other quest content the session passes (bound; see Open for any unverified row) |
| `oploc2 id=dwarf_market_silver (6164)` | 1 | 4494[1138] | generic or other quest content the session passes (bound; see Open for any unverified row) |
| `oploc2 id=exchange_bank_wall_bank (10060)` | 1 | 4495[1324] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `oploc2 id=exchange_bank_wall_bank_multi (30389)` | 1 | 4500[1959] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `oploc2 id=fai_falador_bankbooth (24101)` | 2 | 4502[522] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `oploc2 id=fai_varrock_bankbooth (10583)` | 1 | 4494[4408] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `oploc2 id=fai_varrock_bankbooth_multi (34810)` | 4 | 4494[3843] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `oploc2 id=farming_fruit_tree_patch_2 (7963)` | 1 | 4502[769] | generic: farming and travel; 4502 Pay exists in farming_gardener.rs2 (a4 owns 4503) |
| `oploc2 id=ge_keldagrim_trapdoor (16168)` | 19 | 4489[880] | Keldagrim travel: rides from 4494/4495 (keldagrim_travel.rs2) |
| `oploc3 id=fai_varrock_bankbooth_multi (34810)` | 1 | 4502[838] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `oploc3 id=spirittree_small (1295)` | 2 | 4495[915] | generic: stairs, ladders, spirit trees (ladders_stairs, area_gnome/spirit_tree) |
| `oploc6 id=dwarf_keldagrim_spinning_machine (6080)` | 1 | 4494[1672] | the quest (gdwarf_*) |
| `[oploct_v2] id=dwarf_keldagrim_spinning_machine (6080), , com=inventor` | 1 | 4494[1662] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `[oploct_v2] id=dwarf_keldagrim_spinning_machine (6080), , com=inventor` | 3 | 4494[1703] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `[oploct_v2] id=farming_fruit_tree_patch_1 (7962), , com=inventory:item` | 1 | 4502[323] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `[oploct_v2] id=farming_fruit_tree_patch_2 (7963), , com=inventory:item` | 1 | 4502[772] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `[oploct_v2] id=farming_fruit_tree_patch_3 (7964), , com=inventory:item` | 1 | 4502[498] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `[oploct_v2] id=farming_fruit_tree_patch_4 (7965), , com=inventory:item` | 1 | 4502[442] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `[oploct_v2] id=farming_tree_patch_1 (8388), , com=inventory:items (149` | 1 | 4502[657] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `[oploct_v2] id=farming_tree_patch_2 (8389), , com=inventory:items (149` | 1 | 4502[584] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `[oploct_v2] id=farming_tree_patch_3 (8390), , com=inventory:items (149` | 1 | 4502[242] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `[oploct_v2] id=farming_tree_patch_5 (19147), , com=inventory:items (14` | 1 | 4502[360] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `[oploct_v2] id=forestry_fire (49927), , com=inventory:items (149:0), o` | 8 | 4500[3488] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `opnpc1 aluft_gnome_inventor (2558), )` | 2 | 4493[342] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_barmaid_poor (2383), )` | 3 | 4494[1986] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_barman_rich (2384), )` | 4 | 4491[1023] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_black_guard_leader (6045), )` | 3 | 4491[685] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_black_guard_supreme_leader (6044), )` | 1 | 4491[880] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_boatman_mines (2432)), )` | 2 | 4491[285] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_director_blue_opal (5999), )` | 2 | 4494[622] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_director_green_gemstone (6021), )` | 34 | 4494[697] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_director_red_axe_multi (6538)), )` | 3 | 4494[514] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_director_silver_cog (6023), )` | 4 | 4500[57] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_director_white_chisel (6022), )` | 9 | 4500[58] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_director_yellow_fortune (6000), )` | 1 | 4494[579] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_drunken_dwarf_multi (3198)), )` | 2 | 4494[2064] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_dwarf_man1 (2397), )` | 1 | 4501[582] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_dwarf_man10 (2406), )` | 2 | 4493[401] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_dwarf_man11 (2407), )` | 1 | 4494[2286] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_dwarf_man2 (2398), )` | 1 | 4494[2105] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_dwarf_man3 (2399), )` | 6 | 4494[2208] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_dwarf_man4 (2400), )` | 1 | 4494[2089] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_dwarf_man5 (2401), )` | 2 | 4494[2146] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_dwarf_man6 (2402), )` | 4 | 4491[1040] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_dwarf_man7 (2403), )` | 2 | 4493[113] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_dwarf_man8 (2404), )` | 3 | 4500[2595] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_dwarf_man9 (2405), )` | 2 | 4493[150] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_evil_cat_multi (6539)), )` | 1 | 4494[529] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_excentric_dwarf (2374), )` | 11 | 4491[786] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_factory_worker2 (2378), )` | 1 | 4496[410] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_factory_worker3 (2379), )` | 1 | 4500[1855] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_gardener_dwarf (2375), )` | 2 | 4493[179] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_gnome_emissary_multi (6542)), )` | 3 | 4494[507] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_gnome_female_emissary_multi (6546)), )` | 1 | 4491[1142] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_gnome_male_emissary_multi (6547)), )` | 1 | 4491[1126] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_guarding_cat (2346), )` | 5 | 4492[775] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_inn_keeper_rich (2382), )` | 1 | 4491[1091] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_librarian (2370), )` | 2 | 4492[973] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_library_assistant (2371), )` | 3 | 4492[932] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_library_customer (2372), )` | 2 | 4492[1068] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_library_customer_human (2373), )` | 1 | 4492[953] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_old_dwarf1 (2394), )` | 1 | 4492[856] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_old_dwarf2 (2395), )` | 2 | 4491[740] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_rowdy_dwarf (2393), )` | 5 | 4494[2024] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_secretary_blue_opal (5992), )` | 3 | 4494[545] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_secretary_brown_engine (5996), )` | 3 | 4500[967] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_secretary_green_gemstone (5993), )` | 26 | 4494[694] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_secretary_purple_pewter (5990), )` | 3 | 4493[462] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_secretary_red_axe_multi (6536)), )` | 3 | 4494[501] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_secretary_silver_cog (5995), )` | 5 | 4500[68] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_secretary_white_chisel (5994), )` | 18 | 4494[727] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_secretary_yellow_fortune (5991), )` | 2 | 4494[585] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_shop_armour (2358), )` | 3 | 4491[991] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_shop_bakery (2361), )` | 2 | 4500[608] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_shop_cloth (2366), )` | 1 | 4494[2305] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_shop_cloth_poor (2367), )` | 12 | 4494[1141] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_shop_craft (2363), )` | 1 | 4500[2398] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_shop_gems (2362), )` | 1 | 4500[2377] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_shop_general_store (2359), )` | 3 | 4493[445] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_shop_sculpture (2347), )` | 20 | 4492[319] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_shop_sculpture_model_multi (3197)), )` | 12 | 4492[318] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_shop_ship_mechanic (2360), )` | 2 | 4493[358] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_shop_warhammer (2356), )` | 1 | 4494[2324] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_shop_weapons (2357), )` | 8 | 4491[941] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 dwarf_city_train_conductor5 (2389), )` | 1 | 4500[2562] | Keldagrim travel: rides from 4494/4495 (keldagrim_travel.rs2) |
| `opnpc1 dwarfrock_engineer1 (4893), )` | 2 | 4493[378] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 growncat (1619), )` | 3 | 4489[66] | generic or other quest content the session passes (bound; see Open for any unverified row) |
| `opnpc1 ics_little_hipriest_town (6192)), )` | 1 | 4488[9] | generic or other quest content the session passes (bound; see Open for any unverified row) |
| `opnpc1 macro_frog_2 (5901)), )` | 2 | 4500[1506] | Kiss the Frog random event (macro_frog.rs2, new) |
| `opnpc1 macro_jekyll (307), )` | 1 | 4491[1008] | generic or other quest content the session passes (bound; see Open for any unverified row) |
| `opnpc1 poh_stonemason (3098), )` | 2 | 4493[249] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc1 port_task_master_brimhaven (15465), )` | 2 | 4502[482] | generic: farming and travel; 4502 Pay exists in farming_gardener.rs2 (a4 owns 4503) |
| `opnpc1 thurgo (4733), )` | 11 | 4494[3391] | generic or other quest content the session passes (bound; see Open for any unverified row) |
| `opnpc1 vc_bellemorde (4573), )` | 1 | 4489[776] | generic or other quest content the session passes (bound; see Open for any unverified row) |
| `opnpc1 vc_bones (4576), )` | 3 | 4489[557] | generic or other quest content the session passes (bound; see Open for any unverified row) |
| `opnpc1 vc_face (4590), )` | 1 | 4489[794] | generic or other quest content the session passes (bound; see Open for any unverified row) |
| `opnpc1 vc_grimesquit (4586), )` | 1 | 4489[359] | generic or other quest content the session passes (bound; see Open for any unverified row) |
| `opnpc1 vc_hooknosed_jack (4588), )` | 1 | 4489[638] | generic or other quest content the session passes (bound; see Open for any unverified row) |
| `opnpc1 vc_jimmy_dazzler (4589), )` | 1 | 4489[572] | generic or other quest content the session passes (bound; see Open for any unverified row) |
| `opnpc1 vc_pox (4574), )` | 1 | 4489[630] | generic or other quest content the session passes (bound; see Open for any unverified row) |
| `opnpc1 vc_smokin_joe (4592), )` | 2 | 4492[339] | generic or other quest content the session passes (bound; see Open for any unverified row) |
| `opnpc1 vc_the_beast (4572), )` | 1 | 4492[373] | generic or other quest content the session passes (bound; see Open for any unverified row) |
| `opnpc1 vc_trotters (392), )` | 1 | 4489[345] | generic or other quest content the session passes (bound; see Open for any unverified row) |
| `opnpc1 xbows_sales_dwarf_keldegrim (2783), )` | 1 | 4500[2408] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc2 dwarf_city_berserker3 (6052), )` | 1 | 4491[913] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc2 dwarf_city_black_guard3 (6048), )` | 1 | 4491[871] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc2 giantrat_grey (2859), )` | 1 | 4494[4020] | generic or other quest content the session passes (bound; see Open for any unverified row) |
| `opnpc2 goblin_unarmed_melee_1 (3029), )` | 2 | 4488[57] | generic or other quest content the session passes (bound; see Open for any unverified row) |
| `opnpc2 thief2 (3253), )` | 1 | 4489[792] | generic or other quest content the session passes (bound; see Open for any unverified row) |
| `opnpc3 banker1_east (1634), )` | 3 | 4494[3535] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `opnpc3 banker1_west (1633), )` | 2 | 4490[103] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `opnpc3 banker2_east (3089), )` | 3 | 4500[298] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `opnpc3 blast_furnace_instructor (2923), )` | 1 | 4494[1555] | generic or other quest content the session passes (bound; see Open for any unverified row) |
| `opnpc3 captain_barnaby (3650)), )` | 1 | 4502[470] | generic: farming and travel; 4502 Pay exists in farming_gardener.rs2 (a4 owns 4503) |
| `opnpc3 deadman_banker_grey_west (6528)), )` | 8 | 4495[1352] | generic: banking (interface_bank; chest, wall and Deadman banker added in bank_recorded.rs2) |
| `opnpc3 dwarf_city_shop_cloth (2366), )` | 1 | 4494[2301] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc3 dwarf_city_shop_cloth_poor (2367), )` | 4 | 4494[1414] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc3 dwarf_city_shop_gems (2362), )` | 1 | 4500[2372] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc3 dwarf_city_shop_general_store (2359), )` | 2 | 4493[433] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc3 dwarf_city_shop_warhammer (2356), )` | 1 | 4494[2337] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc3 dwarf_city_shop_weapons (2357), )` | 4 | 4491[943] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc3 dwarf_city_train_conductor4 (2388), )` | 1 | 4494[2740] | Keldagrim travel: rides from 4494/4495 (keldagrim_travel.rs2) |
| `opnpc3 elkoy_village (6266)), )` | 2 | 4502[750] | generic or other quest content the session passes (bound; see Open for any unverified row) |
| `opnpc3 farming_gardener_fruit_1 (2682), )` | 2 | 4502[308] | generic: farming and travel; 4502 Pay exists in farming_gardener.rs2 (a4 owns 4503) |
| `opnpc3 farming_gardener_fruit_2 (2683), )` | 2 | 4502[766] | generic: farming and travel; 4502 Pay exists in farming_gardener.rs2 (a4 owns 4503) |
| `opnpc3 farming_gardener_fruit_4 (2670), )` | 2 | 4502[437] | generic: farming and travel; 4502 Pay exists in farming_gardener.rs2 (a4 owns 4503) |
| `opnpc3 farming_gardener_tree_1 (2678), )` | 3 | 4502[652] | generic: farming and travel; 4502 Pay exists in farming_gardener.rs2 (a4 owns 4503) |
| `opnpc3 farming_gardener_tree_2 (2679), )` | 2 | 4502[574] | generic: farming and travel; 4502 Pay exists in farming_gardener.rs2 (a4 owns 4503) |
| `opnpc3 farming_gardener_tree_3_02 (11957), )` | 2 | 4502[236] | generic: farming and travel; 4502 Pay exists in farming_gardener.rs2 (a4 owns 4503) |
| `opnpc3 farming_gardener_tree_gnome (2687), )` | 3 | 4502[356] | generic: farming and travel; 4502 Pay exists in farming_gardener.rs2 (a4 owns 4503) |
| `opnpc3 farming_tools_leprechaun (0), )` | 1 | 4502[355] | generic: farming and travel; 4502 Pay exists in farming_gardener.rs2 (a4 owns 4503) |
| `opnpc3 garth (2669), )` | 3 | 4502[494] | generic: farming and travel; 4502 Pay exists in farming_gardener.rs2 (a4 owns 4503) |
| `opnpc3 ge_clerk_1 (2148), )` | 8 | 4489[233] | generic: Grand Exchange (interface_grandexchange) |
| `opnpc3 ge_clerk_2 (2149), )` | 1 | 4489[232] | generic: Grand Exchange (interface_grandexchange) |
| `opnpc3 ge_clerk_3 (2150), )` | 1 | 4500[3400] | generic: Grand Exchange (interface_grandexchange) |
| `opnpc3 ge_clerk_4 (2151), )` | 7 | 4495[1326] | generic: Grand Exchange (interface_grandexchange) |
| `opnpc3 gnome_rometti (14), )` | 1 | 4495[820] | generic or other quest content the session passes (bound; see Open for any unverified row) |
| `opnpc3 growncat (1619), )` | 1 | 4489[56] | generic or other quest content the session passes (bound; see Open for any unverified row) |
| `opnpc3 poh_stonemason (3098), )` | 2 | 4493[235] | Keldagrim npc (keldagrim_npcs.rs2, gdwarf_*, forget_*) |
| `opnpc3 smithing_guild_ore_seller (1560), )` | 2 | 4500[554] | generic: checked by the binding sweep |
| `opnpc5 growncat (1619), )` | 1 | 4489[73] | generic or other quest content the session passes (bound; see Open for any unverified row) |
| `[opnpct] dwarf_city_excentric_dwarf (2374), ), com=inventory:items (14` | 1 | 4492[1575] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `[opnpct] dwarf_city_excentric_dwarf (2374), ), com=magic_spellbook:fir` | 1 | 4492[1620] | client UI, the recorder's own |
| `[opnpct] dwarf_city_rowdy_dwarf (2393), ), com=inventory:items (149:0)` | 1 | 4500[2554] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `[opnpct] dwarf_city_secretary_green_gemstone (5993), ), com=inventory:` | 1 | 4496[564] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `[opnpct] dwarf_city_shop_cloth_poor (2367), ), com=inventory:items (14` | 1 | 4494[1630] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `[opnpct] dwarf_city_shop_sculpture (2347), ), com=inventory:items (149` | 1 | 4492[1991] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `[opnpct] dwarf_city_shop_sculpture_model_multi (3197)), ), com=invento` | 1 | 4494[4518] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `[opnpct] vc_bellemorde (4573), ), com=inventory:items (149:0), obj=raw` | 1 | 4489[789] | the recorder's kit: tablets, potions, jewellery (teleport items audited per skill 2b in earlier ports) |
| `opobj3 id=coal (453)` | 1 | 4494[1633] | generic: checked by the binding sweep |
| `opobj3 id=dwarf_battleaxe_new (5059)` | 4 | 4494[4128] | the quest (gdwarf_*) |
| `opobj3 id=dwarf_battleaxe_old (5056)` | 2 | 4494[2647] | the quest (gdwarf_*) |
| `opobj3 id=dwarf_clothes (5067)` | 4 | 4494[1891] | the quest (gdwarf_*) |
| `opobj3 id=dwarf_library_book (5065)` | 2 | 4494[1362] | the quest (gdwarf_*) |
| `opobj3 id=dwarf_perfect_left_boot (5062)` | 15 | 4491[857] | the quest (gdwarf_*) |
| `opobj3 id=dwarf_perfect_right_boot (5063)` | 10 | 4491[792] | the quest (gdwarf_*) |
| `opobj3 id=iron_bar (2351)` | 1 | 4494[4066] | generic: checked by the binding sweep |
| `[opobjt] id=dwarf_perfect_right_boot (5063), , selectedcom=magic_spell` | 11 | 4492[1608] | client UI, the recorder's own |
| `[resume_p_objdialog] id=65535` | 1 | 4494[3528] | generic: Grand Exchange (interface_grandexchange) |
| `[resume_p_objdialog] id=amulet_of_defence (1729)` | 1 | 4495[1112] | generic: Grand Exchange (interface_grandexchange) |
| `[resume_p_objdialog] id=bronze_bar (2349)` | 1 | 4500[352] | generic: Grand Exchange (interface_grandexchange) |
| `[resume_p_objdialog] id=emerald_ring (1639)` | 1 | 4496[127] | generic: Grand Exchange (interface_grandexchange) |
| `[resume_p_objdialog] id=gold_bar (2357)` | 1 | 4500[1934] | generic: Grand Exchange (interface_grandexchange) |
| `[resume_p_objdialog] id=iron_bar (2351)` | 2 | 4494[3893] | generic: Grand Exchange (interface_grandexchange) |
| `[resume_p_objdialog] id=jewl_emerald_bracelet (11076)` | 1 | 4496[140] | generic: Grand Exchange (interface_grandexchange) |
| `[resume_p_objdialog] id=lawrune (563)` | 1 | 4489[257] | generic: Grand Exchange (interface_grandexchange) |
| `[resume_p_objdialog] id=mithril_bar (2359)` | 1 | 4500[339] | generic: Grand Exchange (interface_grandexchange) |
| `[resume_p_objdialog] id=mithril_ore (447)` | 1 | 4500[1429] | generic: Grand Exchange (interface_grandexchange) |
| `[resume_p_objdialog] id=raw_trout (335)` | 3 | 4500[3408] | generic: Grand Exchange (interface_grandexchange) |
| `[resume_p_objdialog] id=redberry_pie (2325)` | 1 | 4494[3487] | generic: Grand Exchange (interface_grandexchange) |
| `[resume_p_objdialog] id=sapphire (1607)` | 1 | 4494[3559] | generic: Grand Exchange (interface_grandexchange) |
| `[resume_p_objdialog] id=silver_bar (2355)` | 1 | 4500[1924] | generic: Grand Exchange (interface_grandexchange) |
| `[resume_p_objdialog] id=silver_ore (442)` | 2 | 4495[1342] | generic: Grand Exchange (interface_grandexchange) |
| `[resume_p_objdialog] id=steel_bar (2353)` | 1 | 4500[373] | generic: Grand Exchange (interface_grandexchange) |
| `[resume_p_objdialog] id=strung_emerald_amulet (1696)` | 1 | 4496[157] | generic: Grand Exchange (interface_grandexchange) |

## Wire audit

Our side is the tick logs of `_giantdwarf_recorded.lua`, `_keldagrim_audit.lua` and `giantdwarf.lua`, and a
`TORIRSSERVER_TRACE_OUT=2` packet trace, against 4491, 4492, 4493, 4494, 4500 and 4501.

| section | R | ours | verdict |
|---|---|---|---|
| camera, boat scene | 16 moves | 16: every value equal, every tick equal from the first move | matched (fixed: the crash pages were 1 tick early) |
| camera, meeting | 21 moves | 21: every value equal, every tick equal | matched (fixed: the ending was 3 ticks late, then 1 early) |
| music and jingles | silence, Crest of a Wave, Land of the Dwarves, the meeting's, `the_face_of_a_king_the_giant_dwarf` | same ids and envelopes | matched |
| synths | steamship, crash, cart, `dwarf_fillmachine`, `strike_and_light`, `forget_glass_chink` | same | matched |
| player animations in the quest | every recorded seq | each one played: fill machine, ignite, both sapphire crafts, the bookcase climb and its forced steps, pickup, read book, clink | matched |
| player animations left over | eating, tablets, cooking, farming, digging, a staff block, a strike cast, a shrug, gem cutting, ladder reach, railing | none are this content | the recorder's own or generic |
| meeting: `[say]`, `[facing]`, transformation, ops, level for every actor | per actor | equal; the inventory files the cat's ops and level under its sleeping type on the wire and its awake type in ours (tick 844 has both) | matched |
| arrest guards' "Halt!" | [515], [516] | same | matched |
| trading floor: shouts, turns to the pit, the referee | per type | every recorded line and tile, on the median gap | built |
| market shopper and stallholders | 4 stops, 4 keepers | same stops, lines and faces; the order round the stalls is ours | built |
| bankers | 152 restated faces | same tile, every 16 ticks | matched |
| Dromund | 630 steps | the rectangle, with passthrough (he walks through a player, as recorded) | matched |

Remaining npc-info differences after all of the above (`npc_block_inventory.py`, 15 types, 56 facing rows): the npcs no
test talks to (six directors and secretaries refusing the player, the guards the recorder attacked). Facing on a talk
is the engine's; it was compared on the 40-odd townsfolk the audit does talk to.

Not matched, by choice: a cart's type swap at the depot (the recording adds one type and transforms it next tick; ours
adds the right type), and carts stepped by one-tile `npc_tele` because the route is over track and through the factory
door, which the client walks but the wire labels a teleport, not a walk.

## Random events

`macro_schedule.rs2`. Every recording from login: events land on an exact multiple of 500 ticks since login, 20 of 728
checks (about 1 in 36), a held player's a few ticks late. Picked in proportion to how often each was recorded across
all cached sessions (Rick 61, Jekyll 55, Genie 49, Arnav 46, Prison Pete 32, Molly 29, Quiz Master 26, frog 24). Staff
level 2 accounts are skipped: that is every automated run. `::randomevent` forces one on any account.

## Rewards and journal

Scroll: 2 Quest Points; 2,500 Mining, Smithing, Crafting; 1,500 Magic, Thieving, Firemaking; the meeting notes (4501
[1099]). Journal: 22 recorded opened states, rewritten row for row from them. Stage 50 was opened by no recording.

## Open

- Dromund's rule fits 13 grabs; other angles are extrapolated.
- The "impressed" boundary of 50 points and the firemaking roll on the spinning machine are unverified.
- The gap between cart deliveries (60 ticks) is a fit to partial sightings.
- Moods on the wiki-sourced rows are unverified, and the Frog's three other menu rows close the menu.
- Random events the tree has no script for (sandwich lady, drunken dwarf, beekeeper, maze, gravedigger, mime, count
  check) are not offered.
- Factory workers: both recorded talks were walked away from, so they have no pages.
- Multinpc stances (boatman, red traders) follow the shell by the engine's rule; not photographed.
- `lint_quest.py` aborts on another session's duplicate var name (`ge_newoffer_price`), so it did not run.
