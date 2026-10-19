# Skilling: farming, herblore, fires and cooking

How the resource rooms' skilling answers a raider: raking, planting, picking herbs, cleaning them, mixing potions, lighting a fire and cooking on it.

- Tool: `tools/cox/rsprox_skilling.py`. Its sections are `rake`, `plant`, `pick`, `clean`, `mix`, `fire` and `cook`. For each click (tick A) it lists the raider's seqs, synth sounds, messages, pack changes and xp as A+n until his next click.
- The content cites these rows as `RK-*` in `cox_resource.rs2`, `cox_herblore.rs2`, `cox_cooking.rs2` and their constants.

    python3 tools/cox/rsprox_skilling.py ~/.rsprox/binary/archive/{1106,3282,1108,1102,1710,3285,1158}.txt

## Coverage: nothing inside the raid

No recording farms, brews, fishes, catches a bat or cooks in the Chambers (checked 2026-10-19):

- **Content search.** No submission holds a herb patch stage (`raids_patch_*`, 29997-30011), the empty patch (29765), the gourd tree (29772), the weeds (29773), a water or empty gourd (20800-20801), a grimy or clean raid herb (20901-20908), or a raw, cooked or burnt raid fish or bat (20853-20883). The exceptions are the cave worms (3389, a scavenger drop) and collection-log views (1710, 1718).
- **Map squares.** Every CoX instance square (50-52 x 80-90), paged, holds six raids. Five are the Grid Master raids already used (820, 843, 3282, 3285, 3389): they take their potions from the bosses and never touch a patch. The sixth, 2528 (rev 229, a normal world), is a scouting session: the raider enters and leaves 17 times and never passes the first doorway. The other sessions those squares return (390, 1510, 3221, 3226, 1758, 1858, 1158, 846, 3959-3961) are login varp dumps, Tombs of Amascut, the Gauntlet and the like.
- **In the raid, only these two clicks.** 3285 clicks a bat once at t14969 and the geyser once at t16194, both in passing: nothing is caught or filled.

So the rows below are **Jagex's ordinary skilling** of the same kind: herb patches, herbs, logs and the Gauntlet's own one-step potions. The raid's patches and gourds are raid code. Treat a row as how Jagex's engine does that action, and an inference about the raid. A row is used only where no higher source says otherwise.

**The proxy's tick.** A click is filed under the tick in which the proxy saw it. The server's answer can therefore land on A or A+1 by chance:
- A fire's first strike lands on A 4 times out of 7.
- A Gauntlet herb-into-vial lands on A 6 times out of 12.

The rows therefore time everything from the server's first answer, never from the click.

| Session | Revision | World | Used for |
|---|---|---|---|
| 1106 | 227 | Leagues V | raking an allotment (8), planting (5) |
| 3282 | 234 | Grid Master | raking (7), planting (15), at a Hespori patch |
| 1108 | 227 | Leagues V | lighting logs (7), herb patch picking (1, from the start to "The herb patch is now empty."), planting (3) |
| 1102, 1710 | 227 | Leagues VI, V | cleaning herbs (23, 122) |
| 3285, 1158 | 234, 237 | Grid Master, Leagues VI | the Gauntlet's herb into vial and dust into potion (4, 8) |

The Leagues sessions mix a whole stack on one click (a relic), so their mixes give no cadence. Their raking, planting, picking, cleaning and fires behave as on a normal world.

## The rows

| Row | Recorded |
|---|---|
| RK-R1 | **Raking:** `farming_raking` (2273) with the synth `farming_raking` (2442), once every **4** ticks while it goes on (all 48 gaps in 15 runs). A yield lands on a rake's own tick. |
| RK-S1 | **Planting:** on S, `farming_seed_dibbing` (2291) with the synth `farming_dibbing` (2432), and the seed leaves the pack. On **S+3** come "You plant a tarromin seed in the herb patch." (or the patch's own wording), the xp and the seeded patch (22 of 22; a 23rd, the wrong seed for the patch, was refused on the click). |
| RK-P1 | **Picking a herb patch:** on S, "You begin to harvest the herb patch." and `picking_low` (2282). On S+1 the first herb with the synth `pick` (2581) and its xp. Then a herb every **3** ticks, each with `pick`, and its `picking_low` the tick before it. The last herb comes with "The herb patch is now empty." (5 herbs, one patch.) |
| RK-C1 | **Cleaning:** "You clean the Grimy ranarr weed." (the grimy item's own name) with one of `herblore_clean_herb_1`..`_4` (3920-3923) at random, the xp, all on the click's tick (145). No seq. |
| RK-M1 | **Herb into vial, one step:** `human_herbing_vial` (363), the synth `grind` (2608), the message and the potion together, on the click's tick (proxy A or A+1, as a fire's strike). The Gauntlet's "You put the grym leaf into the vial of water." and "You mix the crystal dust into your potion." (12). |
| RK-F1 | **Lighting logs:** on S, "You attempt to light the logs.", `human_createfire` (733) and `tinderbox_strike` (2599). On **S+4**, "The fire catches and the logs begin to burn.", the fire (26185), the synth `fire_lit` (2596) and the xp (7 of 7). |

## Fixed (2026-10-19)

| Was | Now |
|---|---|
| a brew every **3** ticks: `p_delay(2)` after each potion, and `p_delay(n)` resumes n + 1 ticks on | every 2 (the potions' Recipe `ticks = 2`), the first on the click (RK-M1), with `grind` |
| "You clean the golpar.", silent | "You clean the Grimy golpar." with a `herblore_clean_herb` sound (RK-C1), through the shared `~clean_herb` |
| planting: the seeded patch, the message and the xp on the click, no sound | the dibber's seq and `farming_dibbing` on the click, the rest on S+3 (RK-S1); a seed whose patch another raider planted meanwhile comes back |
| a herb every 2 ticks (Near-Reality), with `picking_low` on the herb's tick, silent | every 3, the first on S+1, `pick` with each herb and `picking_low` a tick before it (RK-P1) |
| a rake every 3 ticks (Near-Reality), silent | every 4, with `farming_raking` (RK-R1) |
| kindling could not be lit; raw raid fish and bats could not be cooked; eating paid nobody | kindling lights with a tinderbox (one piece of the stack) on the log cadence (RK-F1), 48 xp (wiki Kindling). The 14 raw foods cook on any fire through the cooking table (`cox_cooking.dbrow`: the wiki's levels and xp). Each cooked piece remembers its cook (`inv_setvar`), and a piece eaten hurt pays the cook 4 + 8 x tier, x1.5 when someone else eats it, falling off past 20 x the party and ending at 40 x (wiki Chambers of Xeric, "Cooking Food") |
| `cox2_supplies.lua` counted a fishing try that caught nothing as food | only a caught fish counts |

`test/raids/_skilling_tells.lua` plays the resource-room solver and then lights kindling, cooks the fish and eats it hurt. It checks every row above off the tick log, plus the cook's points:
- planting: the dibber with its sound, and the seeded patch on A+3, not on A;
- picking: every pick with `pick`, `picking_low` the tick before, at least 3 ticks apart;
- cleaning sounds;
- brewing: `grind` with every potion, and each potion as prompt after its click as the fire's strike;
- the fire: `fire_lit`, and 4k ticks after the first strike.

It passes on layouts 0, 2 and 4. Its fire check caught a real slip while this was built: `~push_player` spent a tick before the fire went in. `cox_resource.lua` passes all six variants (both rooms; plain kit, raid kit, Olm kit). One whole raid with the potions made in the raid (`cox_raid_solo_prep.lua`, raida0) brewed in both resource rooms and raked both floors' weeds. It reached Olm carrying 2 overloads, 7 aids, 5+4 revitalisations and 4+3 enhances, then died in the last phase with no healing left (the plan's open "no healing at Olm").

## Open

| Item | State |
|---|---|
| The raid's own patches, gourd tree, geyser, weeds, Bubbles, bats and cooking | Unrecorded. The rows are the engine's own; a recording of a normal-world raid that preps would settle them. |
| A make-menu batch | One potion every 2 ticks is the Recipe's `ticks = 2`. Scriptrun's bot answers the menu with the quantity it opens on (1), so a batch is not driven by a test. |
| The fire's wording for kindling | "You attempt to light the logs." and "The fire catches and the kindlings begin to burn." are Near-Reality's (`Firemaking.KINDLING`). |
| Raid food burn rates | Unpublished ("Needs skilling success chart" on every raw page). Ours run through Near-Reality's stop levels (21/35/50/65/75/85/97) from about 55% at the requirement (`cox_cooking.dbrow`). |
| The cook's points in a party | Counted per cook, which matches the raid's count only for a solo raid. The fall-off between the caps is ours (straight). A piece dropped and picked up again forgets its cook. |
| Mixing messages | "You mix the golpar with the endarkened juice into a potion." and the overload's line are ours: no recording or page gives the raid's. |
