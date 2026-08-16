# Slayer content queue — monsters, masters, equipment

Companion to `docs/slayer_rewards.md` / `docs/slayer_rewards_server_reqs.md`
(the Rewards panel, 426/924, built 2026-08-02) and the plan this queue was
generated for. Those docs cover the reward-points UI; this one covers the
underlying skill — Slayer level gating, task assignment/membership across the
full roster, monster-specific protection gear, and the masters themselves.

Status legend: **not started** · **membership: Turael-only** (assigns fine,
kill does not count outside Turael's 24-task list) · **in progress** · **done**.
Update the status column as rows land — do not let this file drift from the
tree the way `docs/slayer_rewards_server_reqs.md` warns its own discovery pass did.

The task table below is **generated**, not hand-typed —
`tools/gen_slayer_queue.py` reads `configs/all.dbrow` directly (the cache's own
`slayer_task` rows) so a cache bump regenerates it instead of someone silently
falling out of sync. Re-run with:

```sh
tools/gen_slayer_queue.py --status "not started" > /tmp/slayer_tasks.md
```

and splice the output back into §C below (the `--status` default only stamps
*new* rows; rows already marked "done" or "in progress" by hand should be kept,
not overwritten — diff before pasting).

---

## A. Slayer masters — 9 in this cache

Source: [Slayer § Slayer Masters](https://oldschool.runescape.wiki/w/Slayer#Slayer_Masters), [Slayer master](https://oldschool.runescape.wiki/w/Slayer_master).

| # | master | gameval | coord (wiki) | reqs | spawned? | status |
|---|---|---|---|---|---|---|
| 1 | [Turael](https://oldschool.runescape.wiki/w/Turael) / Aya | `slayer_master_1_tureal` (13618), `_1_aya` (13619) | 2932, 3537 | none | **yes** (m45_55.spawn) | done — spawned; skip rule + streak reset in `slayer_masters.rs2`; shop |
| 9 | [Spria](https://oldschool.runescape.wiki/w/Spria) | `slayer_master_9_active` (10432) via multinpc `slayer_master_9` (10440) | 3092, 3268 | A Porcine of Interest | yes (base) | done — quest gate (`%porcine >= ^poi_complete`), skip rule (no streak reset), shop |
| 7 | [Krystilia](https://oldschool.runescape.wiki/w/Krystilia) | `slayer_master_7` (7663) | 3110, 3516 | none | yes | done — wilderness-only credit, separate counter + 5-task points delay (`slayer_kill.rs2`), shop |
| 2 | [Mazchna](https://oldschool.runescape.wiki/w/Mazchna) / Achtryn | `slayer_master_2_mazchna` (13620), `_achtryn_vis` (13621) | 3511, 3509 | 20 combat, Priest in Peril | **yes** (m54_54.spawn) | done — spawned; quest gate (`%priestperil >= ^priestperil_complete`); shop |
| 3 | [Vannaka](https://oldschool.runescape.wiki/w/Vannaka) | `slayer_master_3` (403) | 3147, 9914 | 40 combat | yes | done — shop |
| 4 | [Chaeldar](https://oldschool.runescape.wiki/w/Chaeldar) | `slayer_master_4` (404) | 2444, 4431 | 70 combat, Lost City | yes | shop done; quest gate not started — `quest_lostcity` does not exist anywhere in this content tree (no directory, no varp) |
| 8 | [Konar quo Maten](https://oldschool.runescape.wiki/w/Konar_quo_Maten) | `slayer_master_8` (8623) | 1309, 3785 | 75 combat | yes | shop done; location wire and brimstone key **out of scope** — `slayer_area` dbtable carries only descriptive text (no coordinate bounds) and no `brimstone_key` obj exists in this cache |
| 6 | [Nieve](https://oldschool.runescape.wiki/w/Nieve) / Steve | `slayer_master_nieve` (6797) via multinpc `slayer_master_6` (490) | 2432, 3424 | 85 combat | yes (base) | done — shop; multinpc dispatch unchanged, not re-verified this pass |
| 5 | [Duradel](https://oldschool.runescape.wiki/w/Duradel) / Kuradal | `slayer_master_5_duradel` (13622), `_kuradal` (13623) | 2869, 2982 p1 | 100 combat, 50 Slayer, Shilo Village | **yes** (m44_46.spawn) | spawned + shop; quest gate not started — `quest_shilovillage` does not exist anywhere in this content tree |

Out of scope (post-239 cache): [Mortimer](https://oldschool.runescape.wiki/w/Mortimer) — *"the most difficult Slayer Master… assignments exclusively focus on monsters with superior variants."* No cache record for this cache revision.

Behaviours the wiki states that current code does not implement:

- **Turael skip.** *"Turael is willing to replace an assignment given by another Slayer Master with an easier one, if the task to be replaced wouldn't normally be given by him (for example, he will replace a player's Black demon task but not a Crawling Hand task)."* Today `~slayer_master_allows_free_reset` wipes any task unconditionally and the streak survives; vanilla refuses tasks already on his own list and **resets `%slayer_tasks_completed` to 0**.
- **Krystilia.** *"Only monsters slain within the Wilderness count towards her tasks… Assignments from her have a separate task completion counter to standard assignments, so players will have to complete five of her Slayer tasks to begin earning Slayer reward points."*
- **Konar.** *"Unlike other Slayer Masters, her Slayer tasks also include the location in which she asks you to slay… when on a Slayer task assigned by Konar, all kills on task have a chance of dropping a Brimstone key."*

---

## B. Slayer equipment

Source: [Slayer equipment](https://oldschool.runescape.wiki/w/Slayer_equipment) (full table). "have" = obj stats/level-req exist in this cache/content tree, "does" = the mechanic is implemented.

| Slayer | item | gameval | obtained | use (wiki quote) | have | does |
|--:|---|---|---|---|:-:|:-:|
| 1 | [Enchanted gem](https://oldschool.runescape.wiki/w/Enchanted_gem) | `slayer_gem` | 1 coin | *"Contacting Slayer masters; checking how many kills are left"* | ✓ | ✗ |
| 1 | [Expeditious bracelet](https://oldschool.runescape.wiki/w/Expeditious_bracelet) | — | Lvl-1 Enchant on opal bracelet | *"25% chance for a monster killed to count as two kills"*, 30 charges | ✓ | ✓ |
| 1 | [Bracelet of slaughter](https://oldschool.runescape.wiki/w/Bracelet_of_slaughter) | — | Lvl-3 Enchant on topaz bracelet | *"25% chance for a slayer assignment kill to not count"*, 30 charges | ✓ | ✓ |
| 1 | [Black mask](https://oldschool.runescape.wiki/w/Black_mask) | `harmless_black_mask*` (10 charges) | Cave horror drop | *"+16.67% Attack and Strength boost against the player's Slayer assignment"* | ✓ | ✗ |
| 1 | [Black mask (i)](https://oldschool.runescape.wiki/w/Black_mask_(i)) | — | NMZ / Soul Wars / Scroll of Imbuing | *"additional +15% Ranged … and +15% Magic … against the player's Slayer assignment"* | ✓ | ✗ |
| 1 | [Slayer helmet](https://oldschool.runescape.wiki/w/Slayer_helmet) (+ (i), 6 recolours) | `slayer_helm`, `slayer_helm_i`, `_black/_green/_red/_purple/_turquoise/_hydra` | 400 reward points | *"Crafted from a spiny helmet, a facemask, a pair of earmuffs, a nosepeg, an uncharged black mask, and an enchanted gem with 55 Crafting"* | ✓ | base helm ✓ (`slayer_helm.rs2`, spiny helmet omitted — no cache obj); recolours + (i) not started |
| 1 | [Spiny helmet](https://oldschool.runescape.wiki/w/Spiny_helmet) | — | 650 coins | *"Protecting against Wall beasts"* | ✗ | ✓ via Slayer helmet only (obj itself absent from cache) |
| 1 | [Reinforced goggles](https://oldschool.runescape.wiki/w/Reinforced_goggles) | — | Spria, A Porcine of Interest | *"Protecting against Sourhogs' attacks"* | ✓ | ✗ |
| 1 | [Granite boots](https://oldschool.runescape.wiki/w/Granite_boots) | — | Wyvern Cave drop | *"Protecting from the extreme heat of the floor of the Karuulm Slayer Dungeon"* | ✓ | partial (`minigame_karuulm`) |
| 1 | [Shayzien armour](https://oldschool.runescape.wiki/w/Shayzien_armour) | — | Combat Ring, Shayzien Encampment | *"Protecting from the poison attack of the lizardman shamans"* | ✓ | ✗ |
| 10 | [Facemask](https://oldschool.runescape.wiki/w/Facemask) | `slayer_facemask` | 200 coins | *"Protecting against the environment within the Smoke Dungeon, and against Dust devils' attacks"* | ✓ | ✓ (dust devils + smoke devils, `slayer_gear.rs2`) |
| 15 | [Earmuffs](https://oldschool.runescape.wiki/w/Earmuffs) | `slayer_earmuffs` | 200 coins | *"Protecting against Banshees"* | ✓ | ✓ |
| 20 | [Bag of salt](https://oldschool.runescape.wiki/w/Bag_of_salt) | `slayer_bag_of_salt` | 10 coins each | *"Finishing off Rock slugs (one per slug)"* | ✓ | ✓ |
| 22 | [Ice cooler](https://oldschool.runescape.wiki/w/Ice_cooler) | `slayer_icy_water` | 1 coin each | *"Finishing off Desert lizards (one per lizard)"* | ✓ | ✓ |
| 25 | [Mirror shield](https://oldschool.runescape.wiki/w/Mirror_shield) | `slayer_mirror_shield` | 5000 coins | *"Protecting against Basilisks and Cockatrice"* | ✓ | ✓ |
| 32 | [Fishing explosive](https://oldschool.runescape.wiki/w/Fishing_explosive) | — | 60 coins | *"Luring Mogres; instantly awakening the Kraken boss"* | ✓ | ✗ |
| 33 | [Lit bug lantern](https://oldschool.runescape.wiki/w/Lit_bug_lantern) | `slayer_buglan_on` / `_off` | 130 coins | *"Harming Harpie Bug Swarms… must be lit to work"*, 33 Firemaking | ✓ | ✓ (harpie bug swarms, `slayer_gear.rs2`) |
| 35 | [Witchwood icon](https://oldschool.runescape.wiki/w/Witchwood_icon) | `witchwood_icon` | 900 coins | *"Protecting against Cave horrors"* | ✓ | ✓ (cave + jungle horrors, `slayer_gear.rs2`) |
| 37 | [Insulated boots](https://oldschool.runescape.wiki/w/Insulated_boots) | `slayer_boots` | 200 coins | *"Protecting against Killerwatts"* | ✓ | ✓ (`slayer_gear.rs2`) |
| 39 | [Slayer bell](https://oldschool.runescape.wiki/w/Slayer_bell) | — | 150 coins | *"Luring Molanisks"* | ✗ | ✗ — lure mechanic, no location data, deferred |
| 42 | [Slayer gloves](https://oldschool.runescape.wiki/w/Slayer_gloves) | `deal_slayer_gloves` (Rum Deal reward name, no plain gameval) | 200 coins | *"Protecting against Fever spiders"* | ✓ | ✓ (`slayer_gear.rs2`) |
| 44 | [Boots of stone](https://oldschool.runescape.wiki/w/Boots_of_stone) | — | 200 coins | Karuulm Slayer Dungeon floor heat | ✓ | partial |
| 44 | [Boots of brimstone](https://oldschool.runescape.wiki/w/Boots_of_brimstone) | — | drake's claw on boots of stone | same, superior stats | ✓ | partial |
| 51 | [Tortugan shield](https://oldschool.runescape.wiki/w/Tortugan_shield) | — | Elder Blunn's Spear and Shield Stall | Dire/Shellbane gryphon protection | ✗ | out of scope (post-239) |
| 55 | [Leaf-bladed spear](https://oldschool.runescape.wiki/w/Leaf-bladed_spear) | `slayer_leafbladed_spear` | 31,000 coins | *"Killing Turoths and Kurasks"* | ✓ | ✓ (turoth + kurask damage cap, `slayer_gear.rs2`) |
| 55 | [Leaf-bladed sword](https://oldschool.runescape.wiki/w/Leaf-bladed_sword) | `leafbladed_sword` | Turoth/Kurask drop | same | ✓ | ✓ |
| 55 | [Leaf-bladed battleaxe](https://oldschool.runescape.wiki/w/Leaf-bladed_battleaxe) | `leafbladed_battleaxe` | Kurask drop | same, +17.5% damage, 65 Attack | ✓ | zero-damage cap only, no +17.5% bonus yet |
| 55 | [Slayer's staff](https://oldschool.runescape.wiki/w/Slayer%27s_staff) (+ (e)) | `slayer_staff` | 21,000 coins | *"Allowing the use of the Magic Dart spell to kill Turoths and Kurasks"* | ✓ | ✓ (Magic Dart exempted from the cap; `slayers_staff.rs2`, `magic_dart.rs2`) |
| 55 | [Broad arrows](https://oldschool.runescape.wiki/w/Broad_arrows) / [bolts](https://oldschool.runescape.wiki/w/Broad_bolts) / arrowheads / unf bolts / packs | `slayer_broad_arrows`, `slayer_broad_bolt` | 60 c / 35 points per 250 | *"Killing Turoths and Kurasks"*; fletching unlocks at 52/55 Fletching | ✓ | ✓ damage cap only, no fletching unlock |
| 56 | [Crystal chime](https://oldschool.runescape.wiki/w/Crystal_chime) | `crystal_chime` | crystal chime seed on singing bowl | *"Killing Warped Terrorbirds and Warped Tortoises"* | ✓ | quest-only, not wired to slayer |
| 57 | [Fungicide spray](https://oldschool.runescape.wiki/w/Fungicide_spray) / [Fungicide](https://oldschool.runescape.wiki/w/Fungicide) | `slayer_spray_pump_0..10`, `slayer_fungicide` | 300 c / 10 c | *"Finishing off Mutated Zygomites (10 per refill)"* | ✓ | ✗ |
| 60 | [Nose peg](https://oldschool.runescape.wiki/w/Nose_peg) | `slayer_nosepeg` | 200 coins | *"Protecting against Aberrant spectres"* | ✓ | ✓ (`slayer_gear.rs2`) |
| 75 | [Rock hammer](https://oldschool.runescape.wiki/w/Rock_hammer) | `slayer_rock_hammer` | 500 coins | *"Finishing off Gargoyles"* | ✓ | ✓ |
| 75 | [Rock thrownhammer](https://oldschool.runescape.wiki/w/Rock_thrownhammer) | `slayer_rock_thrownhammer` | 200 coins | *"Finishing off Gargoyles (one per gargoyle)"* | ✓ | ✓ (consumed on use, `slayer_specials.rs2`) |
| — | [Granite hammer](https://oldschool.runescape.wiki/w/Granite_hammer) | `granite_hammer` | Grotesque Guardians | gargoyle finisher alternative (Slayer monsters table) | ✓ | ✓ |
| — | [Brine sabre](https://oldschool.runescape.wiki/w/Brine_sabre) | `olaf2_brine_sabre` (Olaf's Quest II reward name, no plain gameval) | Brine rat | rockslug finisher alternative (Slayer monsters table) | ✓ | ✓ (worn-weapon auto-finish, `slayer_specials.rs2`) |
| — | [Slayer ring](https://oldschool.runescape.wiki/w/Slayer_ring) | `slayer_ring_1..8` | 75 Crafting + Ring Bling perk | teleports, master contact | ✓ | ✓ (`slayer_ring.rs2`) |
| — | [Slayer Equipment shop](https://oldschool.runescape.wiki/w/Slayer_Equipment) | — | every master's `op4=Trade` | *"All Slayer masters run a Slayer Equipment shop, selling the same items at the same price."* | ✓ | ✓ (`shop/slayer_equipment/`, bound on all 13 master npcs) |
| 1 | [Black mask](https://oldschool.runescape.wiki/w/Black_mask) damage bonus | — | — | *"+16.67% Attack and Strength boost"* | ✓ | ✓ (`combat_stats.rs2` `~player_combat_stat`; (i) Ranged/Magic bonus out of scope — no imbued obj in cache) |

---

## C. Slayer monsters / tasks

Target is the **cache's own dbtable 113 `slayer_task`** (129 numbered rows +
34 boss rows, id -1), not the wiki's current live list — see the plan's scope
boundary (no custodian stalkers, gryphons, aquanites, Mortimer, or full nagua
family beyond what this cache names). `min Slayer lvl` below is the cache's
`min_stat_requirement_all` column, read directly — authoritative over any wiki
approximation. Required items are cross-referenced from
[Slayer monsters](https://oldschool.runescape.wiki/w/Slayer_monsters) and folded into §B above rather than
repeated per row; consult that table when implementing a row with a Slayer
level in the low-to-mid range (item requirements cluster there).

<!-- generated by tools/gen_slayer_queue.py — 129 tasks, 34 boss rows, 22 with membership -->

| id | task | min combat | min Slayer lvl | status |
|--:|---|--:|--:|---|
| 1 | [monkeys](https://oldschool.runescape.wiki/w/Slayer_task/Monkeys) | - | - | membership: Turael-only |
| 2 | [goblins](https://oldschool.runescape.wiki/w/Slayer_task/Goblins) | - | - | membership: Turael-only |
| 3 | [rats](https://oldschool.runescape.wiki/w/Slayer_task/Rats) | - | - | membership: Turael-only |
| 4 | [spiders](https://oldschool.runescape.wiki/w/Slayer_task/Spiders) | - | - | membership: Turael-only |
| 5 | [birds](https://oldschool.runescape.wiki/w/Slayer_task/Birds) | - | - | membership: Turael-only |
| 6 | [cows](https://oldschool.runescape.wiki/w/Slayer_task/Cows) | - | - | membership: Turael-only |
| 7 | [scorpions](https://oldschool.runescape.wiki/w/Slayer_task/Scorpions) | 7 | - | membership: Turael-only |
| 8 | [bats](https://oldschool.runescape.wiki/w/Slayer_task/Bats) | 5 | - | membership: Turael-only |
| 9 | [wolves](https://oldschool.runescape.wiki/w/Slayer_task/Wolves) | 20 | - | membership: Turael-only |
| 10 | [zombies](https://oldschool.runescape.wiki/w/Slayer_task/Zombies) | 10 | - | membership: Turael-only |
| 11 | [skeletons](https://oldschool.runescape.wiki/w/Slayer_task/Skeletons) | 15 | - | membership: Turael-only |
| 12 | [ghosts](https://oldschool.runescape.wiki/w/Slayer_task/Ghosts) | 13 | - | membership: Turael-only |
| 13 | [bears](https://oldschool.runescape.wiki/w/Slayer_task/Bears) | 13 | - | membership: Turael-only |
| 14 | [hill giants](https://oldschool.runescape.wiki/w/Slayer_task/Hill_giants) | 25 | - | membership: done (hill giants, npc_type) |
| 15 | [ice giants](https://oldschool.runescape.wiki/w/Slayer_task/Ice_giants) | 50 | - | membership: done (ice giants, npc_type) |
| 16 | [fire giants](https://oldschool.runescape.wiki/w/Slayer_task/Fire_giants) | 65 | - | membership: done (fire giants, npc_type) |
| 17 | [moss giants](https://oldschool.runescape.wiki/w/Slayer_task/Moss_giants) | 40 | - | membership: done (moss giants, npc_type) |
| 18 | [trolls](https://oldschool.runescape.wiki/w/Slayer_task/Trolls) | 60 | - | not started |
| 19 | [ice warriors](https://oldschool.runescape.wiki/w/Slayer_task/Ice_warriors) | 45 | - | membership: done (ice warriors) |
| 20 | [ogres](https://oldschool.runescape.wiki/w/Slayer_task/Ogres) | 40 | - | not started |
| 21 | [hobgoblins](https://oldschool.runescape.wiki/w/Slayer_task/Hobgoblins) | 20 | - | membership: done (hobgoblins) |
| 22 | [dogs](https://oldschool.runescape.wiki/w/Slayer_task/Dogs) | 15 | - | membership: Turael-only |
| 23 | [ghouls](https://oldschool.runescape.wiki/w/Slayer_task/Ghouls) | 25 | - | membership: done (ghouls) |
| 24 | [green dragons](https://oldschool.runescape.wiki/w/Slayer_task/Green_dragons) | 52 | - | membership: done (green dragons, npc_type) |
| 25 | [blue dragons](https://oldschool.runescape.wiki/w/Slayer_task/Blue_dragons) | 65 | - | membership: done (blue dragons, npc_type) |
| 26 | [red dragons](https://oldschool.runescape.wiki/w/Slayer_task/Red_dragons) | 68 | - | membership: done (red dragons, npc_type) |
| 27 | [black dragons](https://oldschool.runescape.wiki/w/Slayer_task/Black_dragons) | 80 | - | membership: done (black dragons, npc_type) |
| 28 | [lesser demons](https://oldschool.runescape.wiki/w/Slayer_task/Lesser_demons) | 60 | - | membership: done (lesser demons, npc_type) |
| 29 | [greater demons](https://oldschool.runescape.wiki/w/Slayer_task/Greater_demons) | 70 | - | membership: done (greater demons, npc_type) |
| 30 | [black demons](https://oldschool.runescape.wiki/w/Slayer_task/Black_demons) | 80 | - | membership: done (black demons, npc_type) |
| 31 | [hellhounds](https://oldschool.runescape.wiki/w/Slayer_task/Hellhounds) | 75 | - | membership: done (hellhounds) |
| 32 | [shadow warriors](https://oldschool.runescape.wiki/w/Slayer_task/Shadow_warriors) | 60 | - | membership: done (shadow warriors) |
| 33 | [werewolves](https://oldschool.runescape.wiki/w/Slayer_task/Werewolves) | 60 | - | not started |
| 34 | [vampyres](https://oldschool.runescape.wiki/w/Slayer_task/Vampyres) | 35 | - | not started |
| 35 | [dagannoth](https://oldschool.runescape.wiki/w/Slayer_task/Dagannoth) | 75 | - | membership: done (dagannoth) |
| 36 | [turoth](https://oldschool.runescape.wiki/w/Slayer_task/Turoth) | 60 | 55 | membership: done (turoth) |
| 37 | [cave crawlers](https://oldschool.runescape.wiki/w/Slayer_task/Cave_crawlers) | 10 | 10 | membership: Turael-only |
| 38 | [banshees](https://oldschool.runescape.wiki/w/Slayer_task/Banshees) | 20 | 15 | membership: Turael-only |
| 39 | [crawling hands](https://oldschool.runescape.wiki/w/Slayer_task/Crawling_hands) | - | 5 | membership: Turael-only |
| 40 | [infernal mages](https://oldschool.runescape.wiki/w/Slayer_task/Infernal_mages) | 40 | 45 | not started |
| 41 | [aberrant spectres](https://oldschool.runescape.wiki/w/Slayer_task/Aberrant_spectres) | 65 | 60 | not started |
| 42 | [abyssal demons](https://oldschool.runescape.wiki/w/Slayer_task/Abyssal_demons) | 85 | 85 | not started |
| 43 | [basilisks](https://oldschool.runescape.wiki/w/Slayer_task/Basilisks) | 25 | 40 | not started |
| 44 | [cockatrice](https://oldschool.runescape.wiki/w/Slayer_task/Cockatrice) | 25 | 25 | membership: done (cockatrice) |
| 45 | [kurask](https://oldschool.runescape.wiki/w/Slayer_task/Kurask) | 65 | 70 | not started |
| 46 | [gargoyles](https://oldschool.runescape.wiki/w/Slayer_task/Gargoyles) | 80 | 75 | not started |
| 47 | [pyrefiends](https://oldschool.runescape.wiki/w/Slayer_task/Pyrefiends) | 25 | 30 | not started |
| 48 | [bloodveld](https://oldschool.runescape.wiki/w/Slayer_task/Bloodveld) | 50 | 50 | membership: done (bloodveld) |
| 49 | [dust devils](https://oldschool.runescape.wiki/w/Slayer_task/Dust_devils) | 70 | 65 | membership: done (dust devils) |
| 50 | [jellies](https://oldschool.runescape.wiki/w/Slayer_task/Jellies) | 57 | 52 | membership: done (jellies) |
| 51 | [rockslugs](https://oldschool.runescape.wiki/w/Slayer_task/Rockslugs) | 20 | 20 | membership: done (rockslugs) |
| 52 | [nechryael](https://oldschool.runescape.wiki/w/Slayer_task/Nechryael) | 85 | 80 | not started |
| 53 | [kalphites](https://oldschool.runescape.wiki/w/Slayer_task/Kalphites) | 15 | - | membership: Turael-only |
| 54 | [earth warriors](https://oldschool.runescape.wiki/w/Slayer_task/Earth_warriors) | - | 15 | not started |
| 55 | [otherworldly beings](https://oldschool.runescape.wiki/w/Slayer_task/Otherworldly_beings) | 40 | - | not started |
| 56 | [elves](https://oldschool.runescape.wiki/w/Slayer_task/Elves) | 70 | - | not started |
| 57 | [dwarves](https://oldschool.runescape.wiki/w/Slayer_task/Dwarves) | 6 | - | membership: Turael-only |
| 58 | [bronze dragons](https://oldschool.runescape.wiki/w/Slayer_task/Bronze_dragons) | 75 | - | membership: done (bronze dragons, npc_type) |
| 59 | [iron dragons](https://oldschool.runescape.wiki/w/Slayer_task/Iron_dragons) | 80 | - | membership: done (iron dragons, npc_type) |
| 60 | [steel dragons](https://oldschool.runescape.wiki/w/Slayer_task/Steel_dragons) | 85 | - | membership: done (steel dragons, npc_type) |
| 61 | [wall beasts](https://oldschool.runescape.wiki/w/Slayer_task/Wall_beasts) | 30 | 35 | not started |
| 62 | [cave slimes](https://oldschool.runescape.wiki/w/Slayer_task/Cave_slimes) | 15 | 17 | not started |
| 63 | [Cave Bugs](https://oldschool.runescape.wiki/w/Slayer_task/Cave_Bugs) | - | 7 | not started |
| 64 | [shades](https://oldschool.runescape.wiki/w/Slayer_task/Shades) | 30 | - | not started |
| 65 | [crocodiles](https://oldschool.runescape.wiki/w/Slayer_task/Crocodiles) | 50 | - | not started |
| 66 | [dark beasts](https://oldschool.runescape.wiki/w/Slayer_task/Dark_beasts) | 90 | 90 | not started |
| 67 | [mogres](https://oldschool.runescape.wiki/w/Slayer_task/Mogres) | 30 | 32 | not started |
| 68 | [lizards](https://oldschool.runescape.wiki/w/Slayer_task/Lizards) | - | 22 | membership: Turael-only |
| 69 | [fever spiders](https://oldschool.runescape.wiki/w/Slayer_task/Fever_spiders) | 40 | 42 | not started |
| 70 | [harpie bug swarms](https://oldschool.runescape.wiki/w/Slayer_task/Harpie_bug_swarms) | 45 | 33 | not started |
| 71 | [sea snakes](https://oldschool.runescape.wiki/w/Slayer_task/Sea_snakes) | 50 | 40 | not started |
| 72 | [skeletal wyverns](https://oldschool.runescape.wiki/w/Slayer_task/Skeletal_wyverns) | 70 | 72 | not started |
| 73 | [killerwatts](https://oldschool.runescape.wiki/w/Slayer_task/Killerwatts) | 50 | 37 | membership: done (killerwatts) |
| 74 | [mutated zygomites](https://oldschool.runescape.wiki/w/Slayer_task/Mutated_zygomites) | 60 | 57 | not started |
| 75 | [icefiends](https://oldschool.runescape.wiki/w/Slayer_task/Icefiends) | 20 | - | membership: Turael-only |
| 76 | [minotaurs](https://oldschool.runescape.wiki/w/Slayer_task/Minotaurs) | 7 | - | membership: Turael-only |
| 77 | [fleshcrawlers](https://oldschool.runescape.wiki/w/Slayer_task/Fleshcrawlers) | 15 | - | not started |
| 78 | [catablepon](https://oldschool.runescape.wiki/w/Slayer_task/Catablepon) | 35 | - | not started |
| 79 | [ankou](https://oldschool.runescape.wiki/w/Slayer_task/Ankou) | 40 | - | not started |
| 80 | [cave horrors](https://oldschool.runescape.wiki/w/Slayer_task/Cave_horrors) | 85 | 58 | not started |
| 81 | [jungle horrors](https://oldschool.runescape.wiki/w/Slayer_task/Jungle_horrors) | 65 | - | not started |
| 83 | [suqahs](https://oldschool.runescape.wiki/w/Slayer_task/Suqahs) | 85 | - | not started |
| 84 | [brine rats](https://oldschool.runescape.wiki/w/Slayer_task/Brine_rats) | 45 | 47 | not started |
| 85 | [scabarites](https://oldschool.runescape.wiki/w/Slayer_task/Scabarites) | 85 | - | not started |
| 86 | [terror dogs](https://oldschool.runescape.wiki/w/Slayer_task/Terror_dogs) | 60 | 40 | not started |
| 87 | [molanisks](https://oldschool.runescape.wiki/w/Slayer_task/Molanisks) | 50 | 39 | not started |
| 88 | [waterfiends](https://oldschool.runescape.wiki/w/Slayer_task/Waterfiends) | 75 | - | membership: done (waterfiends) |
| 89 | [Spiritual creatures](https://oldschool.runescape.wiki/w/Slayer_task/Spiritual_creatures) | 60 | 63 | not started |
| 90 | [lizardmen](https://oldschool.runescape.wiki/w/Slayer_task/Lizardmen) | - | - | not started |
| 91 | [magic axes](https://oldschool.runescape.wiki/w/Slayer_task/Magic_axes) | 40 | 23 | not started |
| 92 | [cave kraken](https://oldschool.runescape.wiki/w/Slayer_task/Cave_kraken) | 80 | 87 | not started |
| 93 | [mithril dragons](https://oldschool.runescape.wiki/w/Slayer_task/Mithril_dragons) | - | - | not started |
| 94 | [aviansies](https://oldschool.runescape.wiki/w/Slayer_task/Aviansies) | - | - | not started |
| 95 | [smoke devils](https://oldschool.runescape.wiki/w/Slayer_task/Smoke_devils) | 85 | 93 | not started |
| 96 | [tzhaar](https://oldschool.runescape.wiki/w/Slayer_task/Tzhaar) | - | - | not started |
| 97 | [TzTok-Jad](https://oldschool.runescape.wiki/w/Slayer_task/TzTok-Jad) | - | - | not started |
| 98 | boss (generic, category 980 dispatch) | - | - | not started |
| 99 | [mammoths](https://oldschool.runescape.wiki/w/Slayer_task/Mammoths) | - | - | not started |
| 100 | [rogues](https://oldschool.runescape.wiki/w/Slayer_task/Rogues) | - | - | not started |
| 101 | [ents](https://oldschool.runescape.wiki/w/Slayer_task/Ents) | - | - | not started |
| 102 | [bandits](https://oldschool.runescape.wiki/w/Slayer_task/Bandits) | - | - | not started |
| 103 | [dark warriors](https://oldschool.runescape.wiki/w/Slayer_task/Dark_warriors) | - | - | not started |
| 104 | [lava dragons](https://oldschool.runescape.wiki/w/Slayer_task/Lava_dragons) | - | - | membership: done (lava dragons, npc_type) |
| 105 | [TzKal-Zuk](https://oldschool.runescape.wiki/w/Slayer_task/TzKal-Zuk) | - | - | not started |
| 106 | [fossil island wyverns](https://oldschool.runescape.wiki/w/Slayer_task/Fossil_island_wyverns) | 60 | 66 | membership: done (fossil island wyverns) |
| 107 | [revenants](https://oldschool.runescape.wiki/w/Slayer_task/Revenants) | - | - | not started |
| 108 | [adamant dragons](https://oldschool.runescape.wiki/w/Slayer_task/Adamant_dragons) | - | - | membership: done (adamant dragons, npc_type) |
| 109 | [rune dragons](https://oldschool.runescape.wiki/w/Slayer_task/Rune_dragons) | - | - | membership: done (rune dragons, npc_type) |
| 110 | [chaos druids](https://oldschool.runescape.wiki/w/Slayer_task/Chaos_druids) | - | - | not started |
| 111 | [wyrms](https://oldschool.runescape.wiki/w/Slayer_task/Wyrms) | - | 62 | not started |
| 112 | [drakes](https://oldschool.runescape.wiki/w/Slayer_task/Drakes) | - | 84 | not started |
| 113 | [hydras](https://oldschool.runescape.wiki/w/Slayer_task/Hydras) | - | 95 | not started |
| 114 | [temple spiders](https://oldschool.runescape.wiki/w/Slayer_task/Temple_spiders) | 40 | - | not started |
| 115 | [undead druids](https://oldschool.runescape.wiki/w/Slayer_task/Undead_druids) | 70 | - | not started |
| 116 | [sulphur lizards](https://oldschool.runescape.wiki/w/Slayer_task/Sulphur_lizards) | - | 44 | not started |
| 117 | [brutal black dragons](https://oldschool.runescape.wiki/w/Slayer_task/Brutal_black_dragons) | 100 | 77 | not started |
| 118 | [sand crabs](https://oldschool.runescape.wiki/w/Slayer_task/Sand_crabs) | - | - | not started |
| 119 | [black knights](https://oldschool.runescape.wiki/w/Slayer_task/Black_knights) | - | - | not started |
| 120 | [pirates](https://oldschool.runescape.wiki/w/Slayer_task/Pirates) | - | 39 | not started |
| 121 | [sourhogs](https://oldschool.runescape.wiki/w/Slayer_task/Sourhogs) | - | - | membership: done (sourhogs) |
| 122 | [warped creatures](https://oldschool.runescape.wiki/w/Slayer_task/Warped_creatures) | - | - | not started |
| 123 | [lesser nagua](https://oldschool.runescape.wiki/w/Slayer_task/Lesser_nagua) | - | 48 | not started |
| 124 | [araxytes](https://oldschool.runescape.wiki/w/Slayer_task/Araxytes) | - | 92 | not started |
| 125 | [crabs](https://oldschool.runescape.wiki/w/Slayer_task/Crabs) | - | - | not started |
| 126 | [custodian stalkers](https://oldschool.runescape.wiki/w/Custodian_stalker) | - | 54 | not started — npcs present (`juvenile/mature/elder/baby_custodian_stalker`) |
| 127 | [metal dragons](https://oldschool.runescape.wiki/w/Slayer_task/Metal_dragons) | 85 | - | membership: done (metal dragons, npc_type) |
| 128 | slayer gryphons | 50 | 51 | membership: done (gryphons -- cache has slayer_gryphon_1/2, not out of scope after all) |
| 129 | aquanites | 80 | 78 | out of scope (post-239, no cache npc) |
| 130 | [frost dragons](https://oldschool.runescape.wiki/w/Slayer_task/Frost_dragons) | 85 | 87 | membership: done (frost dragons, npc_type) |

### Boss tasks (id = -1, no kill quantity — `%if1 = -1`, handled in `slayer_kill.rs2:42`)

| task | status |
|---|---|
| [Araxxor](https://oldschool.runescape.wiki/w/Araxxor) | not started |
| [Barrows brothers](https://oldschool.runescape.wiki/w/Barrows_Brothers) | not started |
| [Callisto](https://oldschool.runescape.wiki/w/Callisto) | not started |
| [Cerberus](https://oldschool.runescape.wiki/w/Cerberus) | not started |
| [Commander Zilyana](https://oldschool.runescape.wiki/w/Commander_Zilyana) | not started |
| [Crazy Archaeologists](https://oldschool.runescape.wiki/w/Crazy_Archaeologist) | not started |
| [Dagannoth Kings](https://oldschool.runescape.wiki/w/Dagannoth_Kings) | not started |
| Duke Sucellus | out of scope (post-239) |
| [General Graardor](https://oldschool.runescape.wiki/w/General_Graardor) | not started |
| [K'ril Tsutsaroth](https://oldschool.runescape.wiki/w/K%27ril_Tsutsaroth) | not started |
| [Kree'arra](https://oldschool.runescape.wiki/w/Kree%27arra) | not started |
| [Sarachnis](https://oldschool.runescape.wiki/w/Sarachnis) | not started |
| [Scorpia](https://oldschool.runescape.wiki/w/Scorpia) | not started |
| Vardorvis | out of scope (post-239) |
| [Venenatis](https://oldschool.runescape.wiki/w/Venenatis) | not started |
| [Vet'ion](https://oldschool.runescape.wiki/w/Vet%27ion) | not started |
| [Vorkath](https://oldschool.runescape.wiki/w/Vorkath) | in progress (`minigame_vorkath/vorkath.rs2` exists — verify slayer task hook) |
| [Zulrah](https://oldschool.runescape.wiki/w/Zulrah) | not started |
| the Abyssal Sire | in progress (`minigame_sire/sire.rs2` exists — verify slayer task hook) |
| [the Alchemical Hydra](https://oldschool.runescape.wiki/w/Alchemical_Hydra) | not started |
| the Cave Kraken boss | in progress (`minigame_kraken/kraken.rs2` exists — verify slayer task hook) |
| [the Chaos Elemental](https://oldschool.runescape.wiki/w/Chaos_Elemental) | not started |
| [the Chaos Fanatic](https://oldschool.runescape.wiki/w/Chaos_Fanatic) | not started |
| [the Giant Mole](https://oldschool.runescape.wiki/w/Giant_Mole) | not started |
| the Grotesque Guardians | in progress (`minigame_grotesque/grotesque.rs2` exists — verify slayer task hook) |
| [the Kalphite Queen](https://oldschool.runescape.wiki/w/Kalphite_Queen) | not started |
| [the King Black Dragon](https://oldschool.runescape.wiki/w/King_Black_Dragon) | not started |
| the Leviathan | out of scope (post-239) |
| the Maggot King | out of scope (post-239) |
| the Phantom Muspah | out of scope (post-239) |
| [the Thermonuclear Smoke Devil](https://oldschool.runescape.wiki/w/Thermonuclear_Smoke_Devil) | in progress (`minigame_thermy/thermy.rs2` exists — verify slayer task hook) |
| the Whisperer | out of scope (post-239) |
| the shellbane gryphon | out of scope (post-239) |

---

## Superior slayer monsters

Source: [Superior slayer monster](https://oldschool.runescape.wiki/w/Superior_slayer_monster).
19 mapped in `skill_slayer/scripts/slayer_superior.rs2`; unique loot is a
`(Unique loot deferred)` stub for all of them (Phase 6 of the implementation
plan). Remaining categories to map (`slayer_superior_for_category`), if the
roster has npcs in them: dark beast, smoke devil, pyrefiend, turoth/kurask.

## Phase 6 status — spawns and drop tables

- **Spawns**: `tools/gen_spawns.py` re-run against the xrsps npc/obj dumps
  (23,141 npc + 2,256 obj spawns across 972 squares, byte-identical to the
  tree's existing spawns — confirmed by diff — so this was a clean no-op
  refresh, not a drift fix). The dump has no record for Turael/Mazchna/
  Duradel, so the three hand-authored master spawns from §A were applied
  directly to the existing (un-regenerated) files instead — running the
  regenerator without `--source` would silently degrade every file's wiki
  citation line to "unspecified", so the citation-preserving files were kept
  and only the 3 target squares were hand-edited. **Any future re-run of this
  tool must pass `--source`, and will still drop the three master lines**;
  re-add from this doc's §A coordinates afterward.
- **Drop tables**: 10 of the 14 newly npc_type-bound families (giants,
  demons, dragons — §C) already had `[ai_queue3,…]` drop tables from the
  LostCity port; not re-verified for completeness or wiki accuracy. The
  ~70 tasks still marked "not started" in §C have no membership row and, by
  extension, no drop-table binding audit — `tools/wiki_droptable.py --batch`
  against the newly-bound gamevals is the next step, not run this pass
  (volume: potentially dozens of files, each needing the id-join verification
  `docs/NPC_WIKI_STATS_PLAN.md` §2 requires).
- **Superior unique loot**: still the `(Unique loot deferred)` stub for all 19
  mapped categories; not touched this pass.

## Cross-references

- Baseline / what already works: implementation plan §"Baseline", not repeated here.
- Engine seams (`slayer_cap_finish_damage`, `[opnpc2,_]`): implementation plan §"Baseline".
- Rewards panel (426/924, points, unlocks): `docs/slayer_rewards.md`, `docs/slayer_rewards_server_reqs.md`.
- Wiki drop-table pipeline: `docs/NPC_WIKI_DROPTABLES_PLAN.md`.
- Wiki combat-stats pipeline + id-join rule: `docs/NPC_WIKI_STATS_PLAN.md` §2.
