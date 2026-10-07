# Ledger: the Fortis Colosseum's cache dumps (`sources/cache_*.txt`)

Pinned 2026-10-03 by the Colosseum inventory agent of the waves loop. The dumps are written
by `tools/waves_gate/cache_dump.py`; re-running the command below over the same content
reproduces every file byte for byte (and the Inferno's dumps are unchanged by the
Colosseum's entry: re-run into a scratch directory, `diff -rq` empty, index identical).

```
python3 tools/waves_gate/cache_dump.py --game colosseum \
    --content OSRS-Content/osrs239-content \
    --out docs/minigames/colosseum/sources \
    --index build/inventory_state/colosseum.cache_index.json
```

## What was read

- **Cache:** revision 239 (`OSRS-Content/osrs239-content/meta.ini`), the unpacked text form
  in OSRS-Content at `c70422bdf6` (2026-10-03). The binary cache is not read.
- **Config files:** `configs/all.<kind>` with `.compack` for npc, seq, spotanim, loc, obj,
  varp, varbit, varc, inv, enum, dbrow, struct; `configs/all.param` (param types);
  `pack/4_soundeffects`, `3_interfaces`, `6_musictracks`, `11_musicjingles`,
  `12_clientscripts`; `interfaces/<name>.if`; the decompiled CS2 suite `scripts/*.cs2`
  (all 9,725 files read for the reader scan); `songs/*.jmid` (presence); `docs/audio/*.tsv`.
- **Map files** (`==== LOC ====` section, `level x z: loc shape [angle]`), both whole:
  - `maps/m28_48.jl2`: region 7216, world x 1792-1855, z 3072-3135, levels 0-2. The
    Colosseum building in Civitas illa Fortis: `colosseum_entrance_outside` 50749 at local
    4,34 level 0 (`cache_map.txt`), the arena floor and stands. 3,194 placements, 259 locs.
  - `maps/m28_148.jl2`: region 7316, world x 1792-1855, z 9472-9535, level 0. The lobby:
    `colosseum_exit_lobby` at 4,34, `colosseum_entrance` 50751 at 18,34,
    `colosseum_bank` at 13,29, `colosseum_scoreboard` at 12,41, bunk beds, shelves.
    4,253 placements, 86 locs.
  - Read during exploration and **not** dumped: `m22_145`, `m21_147`, `m25_148`, `m20_50`
    and others place `wallkit_colosseum10_*` / `civitas_colosseum_brazier*`, a generic wall
    kit and brazier used elsewhere (the Aldarin cellar, Civitas streets). Our tree's
    `maplink.dbrow:449` links `0_28_48_3_34` to `0_28_148_7_34`, which agrees.

## Selection rules (each record's `why=` names the rule)

1. **By name.** The symbol is split on `_`; a record is selected when a word part is
   `colosseum`, `colosseeum`, `colossi`, `colossus`, `solheredit`, `solhereditecho`,
   `colosseumrewards`, `npccolosseum`, `colosseummodifiers`, `glaiveofralos`, `tonalztics`,
   `dizanas`, `manticore`, `warband`, `warbander`, `glaive`, `minimus` or `gloria`; or the
   symbol contains `sol_heredit`, `sunfire_splinter`, `minotaur_boss`, `jaguar_ranger`,
   `jaguar_human`, `jaguar_warrior`, `serpent_mager`, `serpent_shaman`, `doom_scorpion`,
   `ralos01`, or the word `echo_crystal`. An npc is also selected by `name=` Sol Heredit,
   Minimus, Gloria or Smol Heredit. Applied to npc, seq, spotanim, loc, obj, varp, varbit,
   varc, inv, enum, dbrow, sounds, interfaces and clientscripts.
   **Excluded:** `placeholder_`, `cert_`, `br_`, `trailblazer`, `league(s)_`, `deadman`
   anywhere (`deadman_breach_sol_heredit`, `varb19657_deadman_finale_teleport_colosseum_complete`),
   `vmq2` anywhere (a quest's knight and its varbit), `mag_` (the Meat and Greet quest's
   records: `mag_colosseum_kebab`, `mag_minotaur`; `quest_meatandgreet` uses them),
   `skill_feature_` (skill guide rows) and `cluehelper_`. `sunfire` alone and `fortis`
   alone are not matches: sunfire runes and wine, and the city, are not the Colosseum.
2. **By id block.** seq 10798-10923 and spotanim 2666-2734 are contiguous blocks every other
   record of which rule 1 chose; the three seqs left (10861 `npc_human_staffready_spawn`,
   10862 `npc_human_ready_spawn`, 10872 `human_shield_combatant_idle`) are selected as
   `block`. Selection only: nothing about a purpose.
3. **By the cache's own sound browser (`synth`).** dbrow `synth_npccolosseum` (table `synth`)
   has ten sub-menus, `synth_colosseumrewards` one; every sound they list is selected with
   its menu: bosscolossi 291, colosseummodifiers 61, manticore 48, javelincolossi 19,
   minotaur 14, shockwavecolossi 14, glaiveofralos 11, doomsnail 9, jaguarwarrior 3,
   fremennikmage 2, serpentshaman 2. `synth_solhereditecho` (its sounds are `l5_*`, the
   leagues echo boss) is name-selected as a dbrow, its sounds are not.
4. **By map.** Every loc the two squares place (338 distinct locs).
5. **By reference.** Our generated combat ledgers `npc_combat/c/colosseum_*.combat` (no
   Colosseum content exists in our server); they add three `scorpion_update_*` seqs.
6. **By binding**, to a fixed point (as the Inferno): an npc's anim fields and multinpc, a
   spotanim's or loc's `anim=`, a loc's multiloc, multivar, `soundid` and (Colosseum only)
   `soundrandom1..N`, and a seq's frame `sound=`. A varbit's basevar is recorded.
7. **By data** (`data_bind`, Colosseum only):
   - a varbit whose `basevar` is a selected varp (`varb9805_civitas_spawn` on
     `varp4138_colosseum_rewards_1` is the one this rule alone adds);
   - structs by content: combat-achievement tasks with `param_1312=25` (the group of
     "Defeat Sol Heredit once."); modifier records (`param_1895` and `param_1896` present);
     a string param equal to "Fortis Colosseum", "Sol Heredit", "Dizana's", "Dizana's (l)"
     or starting "Defeat Sol Heredit";
   - an enum all of whose values are selected npc ids (enum_5318), and an enum or struct
     named in a name-selected clientscript;
   - a struct param followed by its declared type (`all.param` `type=`: g enum, J struct,
     o/O obj, A seq, t spotanim, n npc, l loc, P synth), except `param_1307`, the
     combat-achievement category struct shared by every task (struct_3594);
   - an enum's values only when a clientscript reads it with a typed `enum()` call, directly
     or through the struct param it was loaded from (enum_5414 via `param_690` is read as
     `namedobj` by `collection_draw_log.cs2:60`). Enums in this cache carry no value type.
8. **Readers** (`cs2_scan`): the whole CS2 suite is scanned for `%<var>`, `interface_<id>`,
   `inv_<id>`, `enum_<id>`; a clientscript that names a selected var, interface or inv is
   selected (`reads ...`) and quoted at those lines only; a name-selected script is quoted
   whole. Each var, interface, enum and inv record lists its readers (`read_by:`, first 12);
   interfaces list their own `on*=i:<script>` hooks.

## Counts (records per file)

| file | kind | records |
|---|---|---|
| cache_npc.txt | npc | 45 (12807-12836, 12843, 12767, 12857, 13110-13113, 13362, 13350-13354, 13420-13421) |
| cache_seq.txt | seq | 166 (125 with a frame list, 41 skeletal; 59 carry frame sounds) |
| cache_spotanim.txt | spotanim | 83 |
| cache_sounds.txt | sound | 503 (474 from the synth menus, 29 by binding only) |
| cache_locs.txt | loc | 401 (338 placed in the two squares; 74 by name, 13 of them placed) |
| cache_map.txt | map | m28_48 and m28_148 whole: every distinct loc, then every placement of a name-selected loc |
| cache_vars.txt | varp / varbit / varc | 15 / 35 / 3 (8, 16 and 3 read by a clientscript) |
| cache_objs.txt | obj / inv | 28 / 4 |
| cache_interfaces.txt | interface / clientscript | 7 / 63 |
| cache_enums_dbrows.txt | dbrow / enum / struct | 23 / 9 / 32 |
| cache_music.txt | music / jingle | 1 / 1 |

## How lengths are given

A sequence with a frame list is summed as the Inferno's: client cycles of 20 ms, game
ticks of 30 cycles. A skeletal sequence (`mayaid`, no frame list) is given as
`mayarange end - start` client cycles. **That one keyframe advances per 20 ms client cycle
is inferred from how the client steps skeletal animations, not checked against a
recording**; a video frame count (`tools/waves_gate/frame_count.py`) should confirm one,
e.g. `npc_colossi_finalboss_01_melee_attack` 0..120 = 4.00 ticks. Frame-sound frame
numbers of a skeletal sequence are in the same cycle units.

## The tables the plan asked for

- **Modifier table: cache data, quoted whole.** `enum_5312` (`cache_enums_dbrows.txt:726`)
  maps index 0-13 to fourteen structs (`:884-1033`): 915 Mantimayhem, 891 Reentry,
  892 Bees!, 893 Volatility, 894 Blasphemy, 897 Relentless, 898 Quartet, 899 Totemic,
  900 Doom, 901 Dynamic Duo, 902 Solarflare, 903 Myopia, 906 Frailty, 907 Red Flag.
  `param_1896` name, `1897`/`1898`/`1899` the level I/II/III text (four have only I),
  `1901` has levels, `1903` glory value, `1914` icon index into enum_5360/5361/5362 (one per
  level, chosen by enum_5364), `1895` the modifier's index, `1900` 0 everywhere. The
  per-modifier rank varbits are read by clientscript 4980 (`cache_interfaces.txt:4283`).
- **Wave table: not cache data.** No enum, struct, dbrow or dbtable lists waves. The only
  wave-adjacent table is `enum_5318` (`:757`): keys 1-9 to the nine monster npcs (12816
  berserker, 12815 seer, 12814 archer, 12818 manticore, 12817 javelin colossus, 12810
  jaguar warrior, 12811 serpent shaman, 12819 shockwave colossus, 12812 minotaur). No
  clientscript reads it; what the keys mean is unsourced.
- **Reward table: not cache data.** The cache has the inventories the rewards sit in
  (`colosseum_rewards` 16 slots, `colosseum_rewards_future` and `_previous` 4 each,
  `cache_objs.txt`), the collection log list `enum_5414` (`:845`: Smol Heredit, Dizana's
  quiver, Sunfire fanatic cuirass/chausses/helm, echo crystal, tonalztics, sunfire splinters,
  uncut onyx) and struct_909 (`param_689` "Fortis Colosseum"). No loot table, roll or rate.
  The tables `reward_selection`, `reward` and `minigame_teleport` hold no Colosseum row.
- **Glory rules: client display code, not tables.** Clientscript 4956
  (`cache_interfaces.txt:3684`) and 4943 (`:3450`) draw a wave's glory: 100 x wave
  (+1000 on wave 12), a speed bonus (500 - last wave duration) x wave when the duration is
  1-499 (varp4136 units), 100 x wave when varp4134 damage taken is 0, and the active
  modifiers' `param_1903` (4943 and 4930 multiply it by the modifier's rank; 4956 does
  not). Clientscript 4922 (`:2960`) cuts varp4130 glory into tiers at
  2000/5000/8000/12000/16000/20000; `emote_checkunlocked.cs2:106` locks emote 53 below
  tier 6. These are what the client shows; the server's own formula is not in the cache.

## Negative results, kept

- **The npc records bind no attack, defend or death animation.** Only ready and walk anims;
  every attack sequence (`npc_colossi_finalboss_01_melee_attack`, `npc_manticore_01_triple_throw`...)
  is selected by name or block and its purpose is unsourced.
- **No spotanim is bound by anything** (projectiles and graphics are the server's choice).
- **No clientscript reads 7 varps and 19 varbits** (e.g. `varp4133_colosseum_wave_start_time`,
  `varp4138_colosseum_rewards_1`, `varb11410_colosseum_highest_wave`,
  `varb9993_colosseum_entry_warning`); they are `unknown_purpose`.
- **Interfaces 246, 592, 866, 867 are named by no clientscript**; only their own hooks run.
- **Sol Heredit has one combat npc record** (`colosseum_sol_p1` 12821, stat4 1500); phases
  are not separate records. `colosseum_boss_seated` 12827 is the seated Sol.
- **No dumped file is over 2 MB.** The largest is `cache_interfaces.txt` (~340 KB).
