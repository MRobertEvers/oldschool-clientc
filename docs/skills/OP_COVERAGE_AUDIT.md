# Skill op coverage audit (2026-11-17)

Woodcutting (2026-11-16) had 65 tree locs that offered "Chop down" and did
nothing. This audit asks the same question of every other skill: which cache
records offer a skill's op, are placed in the world or clicked in a recording,
and have no script that answers them. Then it checks the shapes and numbers of
what is bound against the RSProx recordings.

## Method

`tools/rsprox/op_coverage.py` (new):

- `static <op label>...` lists every loc/npc whose op is one of the labels, its
  placements (maps + `.spawn`), and whether a trigger answers it (by name,
  overlay category, the global `_`, or a multiloc/multinpc relative).
- `recorded` lists every loc/npc op clicked in the 941 cached transcripts
  (`~/.rsprox-index-cache/home-default/.../archive`), sessions and clicks, and
  whether a trigger answers it. 1,524 of 7,855 recorded (type, op) pairs had
  none on 2026-11-17, most of them quest, minigame and seasonal content.
- `inventory` does the same for inventory ops, item-on-item, item-on-loc and
  item-on-npc. SUSPECT rows (a trigger exists but never names the other item)
  are usually a category dispatch: read the script before calling one a gap.
  Two were checked and were not gaps (Saradomin brew via `brew.dbrow`;
  zenyte shard on uncut onyx is "Nothing interesting happens." in index 3324
  too).

A bound script can still decline; this finds what has no script at all.

## Fixed

| skill | defect | evidence | test |
|---|---|---|---|
| Mining | 7 Prifddinas rocks and 25 cave-wall ore veins (`lotr_mine_wall_*`) offered Mine and did nothing; `rocks3` (18 placed) too | 1102 (prif gold -> `prif_mine_rocks1_empty`), 3046 (coal vein -> `lotr_mine_wall_depleted_l/mid`) | `test/skills/mining_rocks.lua` 12/12 |
| Mining | respawns were LostCity's (iron 20, coal 100, mithril 400, amethyst 2400) and scaled by world population | `add_overlaytimer_loc` over every session: clay 2, copper/tin 4, iron 9 (254/281), coal 49, gold 99, mithril 199, runite 1199, amethyst 124, granite 9 | same |
| Mining | Mining Guild resource area did not halve respawns | 3224 coal 24, runite 599; 3114 iron 4; amethyst unchanged 124 | same (`guild.respawn` 4) |
| Thieving | Fortis (5) and Prifddinas (4) stalls and Keldagrim's bakery offered Steal-from and did nothing | 1099, 1102, 1101, 213 | `test/skills/thieving_stalls.lua` 11/11 |
| Thieving | stall xp/respawn were 2004's: silver 54 (wiki 205), spice 81 (92), gem 16 (408); respawns 2-6x long | pinned wiki; fruit stall refill = wiki ticks - 1 (4448, 120/120) | `tools/check_stall_contract.py` 7/7 (was 4, 3 skipped) |
| Thieving | "some silver" -> "a piece of silver"; the baker's stall's "You attempt to steal some bread..." line; Prifddinas stalls never empty (132 steals, 1102); only the fruit stall sends the second `pick` (4448 120/120 vs 0/193 elsewhere) | as cited | same |
| Thieving | 201 npc types offered Pickpocket and did nothing: Kourend men/women, Hosidius/Kourend guards, Varlamore citizens, tourists, knights, paladins, heroes, warriors, farmers | female citizens/tourists say "the woman's pocket" (1099, 168/168) | `test/thieving/kourend_pickpocket.lua` 3/3 |
| Cooking | 9 ranges/ovens and the Tempoross lobby fire offered Cook and did nothing (no category) | 1095 [16733..16737] | `test/skills/cooking_ranges.lua` 4/4 |
| Prayer | the five demonic ashes' Scatter did nothing | 1095, 1099, 819, 820: line, `farming_ingredient_sprinkle`, `farming_sprinkle`, xp, one tick | `test/skills/prayer_scatter_bury.lua` 12/12 |
| Prayer | bury took the bones and paid xp a tick late (LostCity order) | 3443 32/32, 900 12/12: delete + xp on the click tick | same |
| Woodcutting | bird nests could not be searched; trees, bird houses and the WC Guild shrine gave the pre-2019 seed nest (5073) | 1095 [21462], [22182]; only 22798 is ever dropped on the wire | `test/skills/bird_nests.lua` 8/8 |

## Open, ranked by evidence

Recorded = clicked in a cached RSProx session (sessions / clicks).

### Mining (56 placed types still unbound)
- Lovakite rocks (81 placed), Lead/Nickel/Rubium (Sailing ores, 97 placed; lead
  recorded respawn 9), sunstone (22), salt and basalt rocks (Weiss, 42),
  volcanic sulphur (5), volcanic ash piles (Fossil Island, 16), daeyalt,
  Camdozaal barronite (recorded 3268), infernal shale (recorded 3267),
  calcified rocks (Varlamore, recorded 1102: respawn 11-14 varies),
  amalgamation (61), gold veins `dwarf_gold_mine*` (155 placed; location and
  depleted form unidentified), GIM island iron (900: no depletion seen),
  Lunar Isle stalagmites, Zalcano, GOTR guardian remains (minigames).
- Success curves for the new ores need fitting from recordings (swings per
  success), not guessing.

### Thieving
- TzHaar gem/ore counters (1138/1144/1145: 4,326 steals; a second `pick` on
  every steal), Varlamore house heists (chests, jewellery cases, wardrobes:
  recorded), Aldarin chest (606 clicks), Port Roberts stalls (10, Sailing),
  fish/veg/seed/wine/crossbow/magic/scimitar stalls.
- Pickpocket kinds with their own loot: Master Farmer (16 types; recorded),
  TzHaar-Hur (6), cave goblins (18), vyres (~25 named), elves (~20 named),
  Wealthy citizen and its urchins (recorded 1099), pirate, bandit, priest,
  salvager.
- The Fortis market's merchants and guards are not spawned at all (no
  owner/guard can catch a thief there); Varlamore's population in general is
  missing (`placed=0` for every Varlamore citizen type).

### Fishing
- Ominous fishing spots (fishing explosive, 22 recorded clicks), minnow spots
  (4), Camdozaal spots, Tempoross pools, Evil Bob's island.

### Cooking / Smithing
- Gnome cooker, POH cooking pot, Dave's stove (RFD), Perilous Moons stove
  (bream, 1101), Tempoross shrine.
- Anvils left: Dragon Slayer II forge, barbarian anvil, POH anvil, Chasm anvil,
  Elemental Workshop workbench (recorded 2/8).

### Inventory and use-on (recorded, unbound)
- Seed box Fill/Check (22 sessions, 1,004 clicks), herb sack Fill/Check (21,
  1,019), forager's pouch, dirty arrowtips Clean, Wyson nests (3 tables
  fetched: `Bird_nest_(Wyson*)`), clue nests.
- Divine potions (crushed crystal shard on super potions: 3+2+1 sessions),
  anchovy paste, lava scale shards, moonlight grub paste, Hosidius saltpetre on
  compost, farming capes' teleport.

### Agility shortcuts
- Placed and unbound: Cross 90, Jump-to 46, Squeeze-through 41, Climb-over 13,
  Jump 11, Walk-across 10, Jump-over 9, Crawl-through 6. Recorded: Lost Tribe
  hole (7 sessions), Imcando door squeeze (4), Taverley dragon jump-up (3),
  Zul-Andra stepping stones (3), Burgh de Rott fence (2), Anglers' Retreat (2),
  Dagannoth crevice (2), the haunted mine's shortcuts. Each needs its
  `exactmove`/walk copied from the recording (rsprox-content §12).
