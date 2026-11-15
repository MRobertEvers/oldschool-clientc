# Npc aggression: Lumbridge and the Stronghold of Security

How far an aggressive npc lets a player come before it starts a fight
(`huntrange`), and which of these npcs hunt at all (`huntmode`), from RSProx
recordings. Tool: `tools/rsprox/rsprox_aggro.py <area> <transcript>...`.

## Method

For every npc that first turns (`[facing]`) onto the recorder, the tool prints
the Chebyshev distance from the npc's SW tile to the recorder's tile on that
tick (the engine measures `huntrange` the same way, `nearest_victim` in
`torirs_server_combat.c`), and calls it **provoked** if the recorder op'd that
npc in the 50 ticks before. Only unprovoked turns are aggression.

`huntrange` = the largest recorded unprovoked attack distance, floored at 1
(0 never hunts). Near passes ("came within N tiles and was not attacked") do
**not** bound it: Stronghold giant spiders were recorded attacking from 5
tiles in index 4346 [905] and were passed at 0 tiles by a combat-3 player in
other sessions. A pass is a single-way zone refusing a player already in a
fight, an npc busy elsewhere or a player running past between checks, and
reads as nothing.

Combat level gates aggression separately (npc level x2), so a pass by a
high-level player is not evidence either way.

## Stronghold of Security

Sessions entering the Stronghold: index 338, 339, 2938, 3194, 3258, 4346. Every
unprovoked attack is from index 4346 (combat-8 recorder).

| type | was | now | evidence |
|---|---|---|---|
| zombies 2501..2509 (`sos_fam_zombie*`) | huntmode=none | aggressive, huntrange 2 | 4346 [474],[476],[558],[576],[587]: 0,0,1,2,2 tiles (5 events). Wiki "Zombie (Stronghold of Security)": aggressive |
| skeletons 2520..2526 (`sos_death_skeleton*`) | huntmode=none | aggressive, huntrange 5 | wiki "Skeleton (Stronghold of Security)": aggressive. No recording reaches them; 5 is the tree default for an aggressive npc |
| flesh crawlers (`sos_fam_fleshcrawler*`) | 5 | 3 | 4346 [462] 2 tiles, [529] 3 tiles |
| catablepons (`sos_pest_catablepon*`) | 5 | 5 | 4346 [904] 4, [947] 0, 2, 5, [949] 2 |
| giant spider (`sos_pest_giantspider1`) | 4 | 5 | 4346 [883] 2, [905] 5 and 1, [906] 2 |
| spider (`sos_pest_spider`) | 5 | 2 | 4346 [906] 2 tiles (1 event) |
| ankous (`sos_death_ankou*`) | 5 | 1 | 4346 [1106] 0 tiles (1 event) |
| ghosts (`sos_death_ghost*`) | 5 | 1 | 4346 [1106] 1 tile (1 event) |
| giant rats, scorpions (`sos_fam_giantrat*`, `sos_pest_scorpion*`) | 5 | 5 | no unprovoked attack recorded: unchanged |

The minotaurs, wolves, goblins and rats stay passive: passed untouched within
1-2 tiles by combat-3 and combat-8 recorders, wiki not aggressive.

## Lumbridge

| type | was | now | evidence |
|---|---|---|---|
| giant rat level 6 (`giantrat1_2`, `giantrat1_3`) | 5 | 1 | index 4349 [640]: 1 tile on a combat-8 recorder (1 event) |
| giant rat level 3 (`giantrat`), giant spider level 2 (`giantspider1`, 4), scorpion | unchanged | | no unprovoked attack recorded in any Lumbridge session |

Lumbridge's goblins, men, women, cows, chickens, frogs and imps are passive
and stay so (294 sessions, no unprovoked attack).

## Open

The single-event values (spider, ankous, ghosts, level-6 rats) are the least
the recordings support, not a measured ceiling. A recording of a low-level
account standing still beside each would settle them.

## Where the values live

- Zombies and skeletons: `_explicit/configs/server_fields.generated.npc`
  (hand-maintained since 2026-10-11; nothing regenerates it).
- The rest: `npc/configs/combat_stats.generated.npc`, pinned by their ledgers
  (`npc_stats/<shard>/<type>.stats`, `source = authored`) so a
  `tools/gen_npc_stats.py --write` keeps them; `sos_pest_giantspider1` in
  `areas/lumbridge/configs/lumbridge.npc`.
- `tools/wiki_fetch.py` `VARIANT_PAGES` names the Stronghold pages and
  `tools/gen_npc_stats.py` resolves an id there when the name's own page does
  not list it.
