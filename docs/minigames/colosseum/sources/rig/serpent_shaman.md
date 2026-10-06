# Rig pass: serpent_shaman (colosseum)

Npc: `colosseum_standard_mager`, id 12811, name "Serpent shaman", models 50747, 53212-53214
(cache_npc.txt line 115, configs/all.npc:415461). hp 125 per AV_INVENTORY.

## The rig
- ready `human_staffready` (813) and walk `human_halberdwalk_f/_b/_l/_r` (1205-1208) are all on
  **framemap 0, the shared human rig** (3,905 sequences). The join does not close it.
- So the candidate set was narrowed by the unit's own name words (`serpent`, `shaman`, `mager`):
  every row found that way is tier `name`, never better.

## Candidates (10 rows in serpent_shaman.tsv)
- bound (5): ready 813; walk 1205 (forward), 1206 (back), 1207 (left), 1208 (right). Tier `bound`.
- attack1: 10859 `npc_serpent_mager_casting`, 17 frames, 3.33 game ticks, frame sound 3
  `varl_serpent_shaman_water_cast_01`. On rig 0, name says casting. Tier `name`.
- death: 10860 `npc_serpent_mager_death`, 10 frames; motion 47 client ticks (1.57 game ticks),
  last frame holds 20000 (corpse hold); forcedpriority 10; frame sound 1 `human_death`.
  Tier `name`.
- special, spawn: none found. No sequence names a spawn; the cache has no walkfade for this npc.
- defend: none. No sequence on the rig carries a serpent/shaman/mager name with block or defend.
  The generic human_* block/defend (e.g. staff_block 415) are on the rig but belong to every
  human, so they are not claimed.
- unknown: no `rig`-tier rows; the 3,900 other human-rig sequences were deliberately not listed.
- Spotanims: no spotanim name contains serpent or mager. Three `lizardshaman_*` (spit_acid,
  acid_splash, spawn_explode) share only the word shaman and belong to the Lizardman shaman: rows
  listed as role unknown, tier `name`, not to be used. The shaman's actual projectile graphic is
  not named in the cache; the water impact sound `varl_serpent_shaman_water_impact_01` is in the
  synth menu only (AV_INVENTORY).

## Ledger (OSRS-Content/osrs239-content/npc_combat/c/colosseum_standard_mager.combat)
All six rows are `-` (a5: rig shared, 3905 seqs; no sound source). No disagreement: the ledger
names nothing wrong, it names nothing.

## What only a recording, plugin constant or picture can settle
- That 10859 is the attack and 10860 the death: names only; Blert/plugin animation ids would promote.
- The projectile/impact graphic ids and their timing.
- Whether a defend/flinch animation exists (none by name).
- Attack speed and hit delay (server data, not cache).
