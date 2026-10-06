# The Fortis Colosseum: asset inventory

`AV_INVENTORY.tsv` has 1,419 rows. Each row is one rev-239 cache asset that ships for the
Fortis Colosseum, with the source of its purpose. The columns are the Inferno's:
`kind, id, cache_name, unit, purpose, used_by, status, detail`. Our server has **no
Colosseum content**, so this is what a plan and the first build are written from.

The assets come from `sources/cache_{npc,seq,spotanim,sounds,locs,vars,objs,interfaces,enums_dbrows,music,map}.txt`.
`tools/waves_gate/cache_dump.py --game colosseum` writes them; the selection rules, what
was read, and the negative results are in `sources/LEDGER_cache.md`. The TSV is built by
`build/inventory_state/colosseum_av.py` (not committed, as the Inferno's builder).

**Status.** Two values, as the brief sets them:
- `not_built`: a cache source gives the purpose, and nothing of ours builds it. Sources:
  an npc record's own `name=` and ops (its identity); an npc's ready/walk anim; a
  spotanim's or loc's `anim=`; a seq's frame sound (the sound's purpose is "plays at frame N
  of seq S"); a loc's `soundid`/`soundrandom`; the cache map placing a loc; a clientscript
  that reads a var, inv, enum or interface (`read_by:` in the dumps); a struct's or dbrow's
  own text; an enum loaded through a typed param; a clientscript's own code.
- `unknown_purpose`: no cache binding gives one. It is left alone until a source does.
There is no corpus yet, so the wiki, Blert and plugins are not sources here: everything
the cache does not bind is `unsourced` and listed under "Re-check when the corpus lands".
A cache name is never a purpose: `npc_colossi_finalboss_01_grapple_attack_telegraph` is
`unknown_purpose` however plain its name.

**Unit** is a filing choice, not a purpose. In order: an npc by its record identity
(`name=`, symbol); a seq by the npc that binds it, else by the synth menu of its frame
sounds, else by the spotanim/loc that animates with it, else by name (`unit by name` in
detail); a sound by its synth menu (dbrow `synth_npccolosseum`'s sub-menus: `bosscolossi`
-> sol_heredit, `manticore` -> manticore, ...), else by what binds it; a spotanim by its
anim's sounds, else by name; a loc by where the map places it (lobby -> lobby_and_entry,
the arena building -> shared) or by its symbol. `synth_bosscolossi` is filed under
sol_heredit because its sounds are the frame sounds of the `npc_colossi_finalboss_*` seqs
and those are Sol Heredit's ready/walk anims (12821); every other menu names its monster.

**used_by** lists every line in our tree that names the record's symbol whole: a scoped
scan (`build/inventory_state/colo_explore/treescan.py`) of OSRS-Content `server/`,
`npc_combat/`, `npc_stats/`, `npc_movement/` and `src/torirsserver/` for Colosseum words
found 75 files; the symbol match then ran over those files' code (after `//`). 74 rows
have a hit. `docs/audio/*.tsv` (name tables) are not uses.

## Counts (not_built / unknown_purpose)

| kind | waves | trio | javelin | jaguar | shaman | manticore | shockwave | minotaur | modifiers | sol | lobby/entry | rewards | shared | unknown | total |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| npc (45) | . | 3/0 | 1/0 | 1/0 | 1/0 | 1/0 | 1/0 | 2/0 | 2/1 | 2/1 | 13/1 | 3/0 | . | 7/5 | 37/8 |
| seq (166) | . | 0/9 | 6/7 | 0/3 | 0/2 | 8/7 | 3/2 | 2/9 | 7/4 | 12/11 | 7/0 | 12/4 | 12/0 | 25/14 | 94/72 |
| spotanim (83) | . | . | 0/6 | . | . | 0/7 | 0/1 | 0/1 | 0/4 | 0/11 | . | 0/10 | . | 0/43 | 0/83 |
| sound (503) | . | 3/1 | 11/9 | 4/0 | 1/1 | 13/35 | 9/5 | 12/2 | 9/52 | 76/215 | 6/0 | 5/6 | 19/0 | 3/6 | 171/332 |
| loc (401) | . | . | . | . | . | . | . | . | . | . | 86/3 | 2/0 | 258/0 | 1/51 | 347/54 |
| varp (15) | 4/2 | . | . | . | . | . | . | . | 0/1 | . | . | 4/4 | . | . | 8/7 |
| varbit (35) | 0/3 | . | . | . | . | . | . | . | 12/0 | 1/2 | 0/7 | 3/7 | . | . | 16/19 |
| varc (3) | . | . | . | . | . | . | . | . | 1/0 | . | . | 1/0 | . | 1/0 | 3/0 |
| interface (7) | . | . | . | . | . | . | . | . | 2/0 | . | 1/0 | 4/0 | . | . | 7/0 |
| clientscript (63) | . | . | . | . | . | . | . | . | 38/0 | 1/0 | . | 23/0 | . | 1/0 | 63/0 |
| enum (9) | 0/1 | . | . | . | . | . | . | . | 6/0 | . | . | 2/0 | . | . | 8/1 |
| struct (32) | . | . | . | . | . | . | . | . | 14/0 | . | . | 17/0 | . | 1/0 | 32/0 |
| dbrow (23) | . | 1/0 | 1/0 | 1/0 | 1/0 | 1/0 | 1/0 | 1/0 | 1/0 | 2/0 | 3/0 | 6/0 | 1/0 | 3/0 | 23/0 |
| inv (4) | . | . | . | . | . | . | . | . | . | . | . | 4/0 | . | . | 4/0 |
| obj (28) | . | . | . | . | . | . | . | . | . | . | . | 27/0 | . | 1/0 | 28/0 |
| music (1) | . | . | . | . | . | . | . | . | . | . | . | . | 1/0 | . | 1/0 |
| jingle (1) | . | . | . | . | . | . | . | . | . | . | . | 0/1 | . | . | 0/1 |
| all (1419) | 4/6 | 7/10 | 19/22 | 6/3 | 3/3 | 23/49 | 14/8 | 17/12 | 92/62 | 94/240 | 116/11 | 113/32 | 291/0 | 43/119 | 842/577 |

- **`shared`** is the arena building's scenery (258 locs placed in m28_48: walls, stands,
  floors, statues) and the generic human ready/walk/run anims and sounds several units
  reuse. Nothing in it needs building: the engine loads the cache map as it is.
- **`unknown`** holds records the name rule caught that nothing files: the gibs
  (`colosseum_*_gib` 12828-12832), the Pillar `colosseum_safespot_dying` 12820, the Doom
  Scorpion 12822 and its `scorpion_update_*` rig (its sounds sit in `synth_doomsnail`, a
  sub-menu of the Colosseum's monster menu; nothing joins it to the Doom modifier but the
  word), the `colosseeum_gladiator_*`/`colosseum_duelist_flavour` npcs, 36
  `greybox_colosseum_wall*` locs that no map places, the `*_echo` spotanims 3145-3158 and
  `colosseum_holy_fire_echo`, and the explosion/crystal/hot-sand vfx with no binding.

## 1. The arena and the lobby (sources/cache_map.txt)

- **m28_48, region 7216** (world 1792-1855, 3072-3135; levels 0-2): the Colosseum building
  in Civitas. `colosseum_entrance_outside` 50749 "Colosseum entrance", op1 Enter, at local
  4,34 level 0 (`cache_map.txt:268`). Level 0 holds a walled square at local x 18-47,
  z 20-49 bounded by `inviswall_blockrange` 85 and `icon_diag_wall01/02` (`cache_map.txt:8,48-49`);
  levels 1-2 are the stands (`entrance_colosseum01_floor*`, `wallkit_colosseum03_*`).
  **Which tiles the fight uses, and whether it is an instance copy of this square, the cache
  does not say.**
- **m28_148, region 7316** (world 1792-1855, 9472-9535; level 0): the lobby.
  `colosseum_exit_lobby` 50750 "Stairs" at 4,34, `colosseum_entrance` 50751 "Entrance" op1
  Enter at 18,34, `colosseum_bank` 50748 "Bank chest" (Use, Collect) at 13,29,
  `colosseum_scoreboard` 50747 (View, View-stats, View-glory) at 12,41, seven bunk beds and
  shelves (`cache_map.txt:370-373`). 4,253 placements of 86 locs, including an anvil,
  braziers and combat dummies.
- **Placed by no map, so spawned by the server:** `colosseum_reward` 50741 "Rewards Chest"
  (op1 Search, anim `chest_colosseum01_reward01_spawn_01`), `colosseum_exit` 50752 "Gate"
  (Exit, Quick-exit), `colosseum_molten_pool_1..3` 50743-50745 (anim
  `vfx_colosseum_hot_sand_02_idle_01`), `colosseum_holy_fire` 50746 "Molten Sand",
  `colosseum_wave_egg_shell` 50742 (`cache_locs.txt:839-1010`).
- No npc spawn is cache data; our tree has no `.spawn` for either square.

## 2. The monsters (npc record, hitpoints, the rig's sequences)

`stat4` is the record's hitpoints; lengths are game ticks (`sources/cache_seq.txt`; a
skeletal seq's length is an inference, see the ledger). The npc records bind only the ready
and walk anims; every attack, defend and death sequence below is selected by name and is
`unknown_purpose` until a source says which attack plays it.

- **Fremennik trio** (`fremennik_trio`): 12814 archer, 12815 seer, 12816 berserker
  (hp 50/50/48). Seqs 10850 archer attack (1.93t, frame sound `arrow_launch`), 10853 seer
  cast (2.20t, `varlamore_fremennik_mage_fire_cast_01`), 10856 berserker stab (1.30t,
  `stabsword_stab`), a defend (2.10t) and death (668.23t: a 20,000-cycle last frame, the
  Inferno's Yt-HurKot quirk) each. Ready/walk are the shared human anims. Sounds: synth
  `fremennikmage` 2.
- **Javelin Colossus** (`javelin_colossus`): 12817, hp 220. Ready `npc_colossi_javelin_01_idle`
  (2t), walk `npc_colossi_javelin_walk` (3t). Unbound: `_range_attack` 3t, `_artillery_attack`
  3t, `_death` 4t, `_walkfade`; projectile seqs `_spearhead`, `_artillery_slow/fast/fire`,
  `_spearhead_fire_slow/fast` (1-2t) are the anims of spotanims 2673-2678. Sounds: synth
  `javelincolossi` 19, one borrowed `npc_akkha_melee_spear_swoosh_01` in-band.
- **Jaguar warrior** (`jaguar_warrior`): 12810, hp 125. 10847 claws attack (2.10t, three
  `varlamore_jaguar_warrior_*` swipes in-band), 10848 defend, 10849 death.
- **Serpent shaman** (`serpent_shaman`): 12811 `colosseum_standard_mager`, hp 125. 10859
  casting (3.33t, `varl_serpent_shaman_water_cast_01`), 10860 death. Its impact sound
  `varl_serpent_shaman_water_impact_01` is in the synth menu only.
- **Manticore** (`manticore`): 12818, hp 250. Ready `npc_manticore_01_idle`, walk and
  backwards walk; unbound `_triple_charge` (4t) and `_triple_throw` (3t), `_death` and
  `_death_explode` (4t), `_spawn_01/02`. Spotanims 2681-2686 are the magic, ranged and
  melee orb projectiles and impacts; they animate with the reused
  `vfx_leviathan_01_projectile_*` seqs. Sounds: synth `manticore` 48, 13 bound in-band.
- **Shockwave Colossus** (`shockwave_colossus`): 12819, hp 125. Ready/walk bound;
  `_clapattack` 3t (also spotanim 2679), `vfx_colossi_shockwave_clap_proj`, `_death` 4t.
  Sounds: synth `shockwavecolossi` 14 (`varl_audio_shockwave_colossi_*`).
- **Minotaur** (`minotaur`): 12812 and 12813 `colosseum_minotaur_routefind` (two records,
  identical stats, hp 225, `param_26` 2 vs 4). Ready/walk bound; `_attack_melee`,
  `_attack_magic` (its frame sounds are `..._attack_magic_heal_charge/cast`), `_defend`,
  `_spawn` 5t, `_death` 3t, and three `_louder`/`_fast` variants 11746-11748. Sounds:
  synth `minotaur` 14.
- **The wave list**: `enum_5318` keys 1-9 to the nine monster npcs above, in the order
  berserker, seer, archer, manticore, javelin, jaguar, shaman, shockwave, minotaur. No
  clientscript reads it (`waves_and_reinforcements`, `unknown_purpose`).

## 3. The modifiers (cache data, `sources/cache_enums_dbrows.txt:726,884-1033`)

`enum_5312` lists fourteen modifier structs; each carries its name (`param_1896`), the
level I/II/III text (`1897-1899`; Quartet, Totemic, Dynamic Duo and Red Flag have one
level), whether it has levels (`1901`), a glory value (`1903`), an icon index (`1914`) into
the per-level sprite enums 5360/5361/5362 (picked by enum_5364; enum_5363 is a per-level
frame sprite). The texts are the cache's own statement of each modifier's effect, e.g.
struct_891 Reentry "Javelins leave a temporary pool of molten sand where they land."

| struct | name | levels | glory (1903) | rank varbit read by clientscript 4980 |
|---|---|---|---|---|
| 915 | Mantimayhem | 3 | 150 | varb4588 mantimayhem |
| 891 | Reentry | 3 | 150 | varb9792 reentry |
| 892 | Bees! | 3 | 150 | varb9791 *toxicity* |
| 893 | Volatility | 3 | 100 | varb9799 volatility |
| 894 | Blasphemy | 3 | 100 | varb9790 blasphemy |
| 897 | Relentless | 3 | 200 | varb9798 relentless |
| 898 | Quartet | 1 | 100 | - |
| 899 | Totemic | 1 | 200 | - |
| 900 | Doom | 3 | 200 | varb10681 doom |
| 901 | Dynamic Duo | 1 | 150 | - |
| 902 | Solarflare | 3 | 250 | varb9797 solarflare |
| 903 | Myopia | 3 | 200 | varb9795 myopia |
| 906 | Frailty | 3 | 200 | varb9796 frailty |
| 907 | Red Flag | 1 | 250 | - |

- The choice between waves is interface 626 `colosseum_intermission` / 865
  `colosseum_intermission_2`, drawn by clientscripts 4931-4965 (the `torirs_colosseum_mod_*`
  labels), with `varb9788_colosseum_selected_modifier`, `varc1194` and `varc1196` the
  selection and tab, and invs `colosseum_rewards_future`/`_previous` (4 slots) the reward
  preview. All of it is `not_built`.
- Bees! reads the varbit named `toxicity` (`torirs_colosseum_mod_rank.cs2`, case 892):
  quoted as the cache has it.
- Modifier creatures: 12823 Bee Swarm (op2 Attack), 12825 Healing totem (op2 Attack),
  12826 `colosseum_solar_flare` (no name, stat4 25). Their seqs and the
  `varl_audio_modifier_*` sounds (synth `colosseummodifiers`, 61) are the modifiers unit;
  the bee jar (2707/2708) and totem (2687/2688) spotanims are unbound.
- `varb9801_colosseum_doom_stacks_client` is read by `torirs_buff_bar_value` (the buff bar).

## 4. Sol Heredit (`sol_heredit`)

- 12821 `colosseum_sol_p1` "Sol Heredit", op2 Attack, stat4 **1500**; one combat record, no
  phase records. 12827 `colosseum_boss_seated` is the seated Sol (no ops). 12824
  `colosseum_beam_crystal` (no name, stat4 25) is filed here because its
  `npc_colosseum_crystal_*` seqs play `varl_colossi_crystal_*` sounds from
  `synth_bosscolossi`.
- Bound: ready `npc_colossi_finalboss_01_idle`, walk `_walk` (3t), seated idle. Unbound,
  with lengths: `_melee_attack` 4t, `_melee_attack_telegraph` 6t,
  `_grapple_attack_telegraph` 4t, `_shieldslam_telegraph` 4t, `_tripleattack` 12t and
  `_tripleattack_shorter` 11t, `_arena_jump`/`_arena_land` 3t, `_death` 8.33t. Their
  spotanims: 2666-2672 (land, triple-attack telegraphs, four melee hit graphics),
  2680 death, 2724 explosion; 2699-2706 `vfx_colossi_stab_dust_*` (unit unknown).
- Sounds: `synth_bosscolossi` 291 (`varl_colossi_boss_*`, `_tripleattack_*`, `_arena_*`,
  `_crystal_*`, `explosion_*`); 76 are frame sounds of a selected seq, 215 are in the menu
  only. **Two thirds of Sol's sounds have no binding**: the server must play them, and the
  cache does not say when.
- Vars: `varb9800_colosseum_sol_grapple_pending` is read by `wear_updateslot_546` (the
  equipment slot redraw); `varb9810_colosseum_sol_failures`,
  `varb9809_colosseum_boss_cutscene_seen` are read by no clientscript.
- The phases, the pools, the shrinking arena and the enrage are not cache data.

## 5. Lobby, entry and the surrounding systems

- **Npcs:** 12807 Minimus outside (Talk-to), 12808 Minimus (op1 **Start-wave**, op3
  **Leave**), 12809 Gloria (Talk-to; filed under rewards by her symbol `colosseum_glory`),
  13110-13113 lobby Guards (Talk-to), 13420/13421 Ueman and Seia, Teoki of Ralos/Ranul
  (Talk-to), 12833 Passionate Supporter (op2 Attack, stat4 10; unit unknown), 13362
  `quetzal_colosseum` and its five `multivarbit=varb9958_quetzal_colosseum` children (the
  quetzal landing; already driven by our `quetzal_transport.rs2`).
- **Vars nobody in the cache reads** (the server's own state, `unknown_purpose`):
  `varb9807_colosseum_master_intro`, `varb9804_colosseum_gloria_met`,
  `varb9993_colosseum_entry_warning`, `varb9808_colosseum_passionate_supporter_speech`,
  `varb9806_colosseum_herb_patch_chat`, `varb11410_colosseum_highest_wave`,
  `varp4133_colosseum_wave_start_time`, `varp4139_colosseum_killtime`,
  `varb9811/9812_colosseum_killtime_best/latest`. Their names are not purposes.
- **Interfaces:** 867 `colosseum_scoreboard` (the lobby loc's View-stats/View-glory; hooks
  only, no reader), 866 `colosseum_reward` (title "Wave Complete!"), 626/865 the
  intermission (section 3), 246/864 the reward chest (title "Fortis Colosseum"; 864 drawn
  by clientscripts 4923-4930 over inv `colosseum_rewards`, 16 slots), 592 `dizanas_quiver`.
- **Music:** midi 782 "Are You Not Entertained?", unlock hint "in the Fortis Colosseum."
  (dbrow `music_fortis_colosseum`); no `docs/audio/music_regions.tsv` row names region
  7216 or 7316. Jingle 305 `glorious_champion_fortis_colosseum` (purpose unsourced).
- **Teleports and travel (dbrows):** `quetzal_colosseum` (destination 8 "Fortis
  Colosseum"), `bank_fortis_colosseum_teleport`, `boss_fortis_colosseum_teleport`; locs
  `poh_jewellery_box_1..3_colosseum` and `quetzal_landing_site_colosseum`.

## 6. Rewards and glory

- **Glory** (`varp4130_colosseum_glory`, `varp4132_colosseum_current_glory`): the cache
  has the client's display of a wave's glory and the glory tiers, not the server's
  formula. Clientscripts 4956 and 4943 (`sources/cache_interfaces.txt:3684-3725, 3450-3491`):
  100 x wave, +1000 on wave 12; speed bonus (500 - varp4136) x wave when varp4136 is
  1-499; 100 x wave when varp4134 damage taken is 0; plus each active modifier's
  `param_1903` (multiplied by its rank in 4943/4930, not in 4956). Clientscript 4922 cuts
  varp4130 into tiers at 2000, 5000, 8000, 12000, 16000, 20000 (`:2960`), and
  `emote_checkunlocked.cs2:106` locks emote 53 until tier 6. dbrow
  `hiscores_activity_colosseum_glory` puts varp4130 on the hiscores.
- **The reward pool:** invs `colosseum_rewards` (16), `_future` and `_previous` (4 each);
  `varb9802_colosseum_loot_opened` and `varp4138_colosseum_rewards_1` are read by nothing.
  No loot table is cache data.
- **Collection log** (`enum_5414` via struct_909 `param_690`, read as namedobj by
  `collection_draw_log.cs2:60`): Smol heredit 28960, Dizana's quiver (uncharged) 28947,
  Sunfire fanatic cuirass 28936 / chausses 28939 / helm 28933, Echo crystal 28942,
  Tonalztics of ralos (uncharged) 28919, Sunfire splinters 28924, uncut onyx 6571;
  `varb9990_collection_bosses_colosseum_completed`.
- **Pet:** Smol Heredit 12857 (follower) and 12767 (house menagerie), varbits 9814/9816.
- **Combat achievements:** thirteen task structs with `param_1312=25` (929-945; the
  "Colosseum Grand Champion", "Speed-Chaser" 28:00 and "Speed-Runner" 24:00 tasks, wave 4/7
  manticore/minotaur tasks, Sol-specific tasks), struct_881 boss info
  ("Sol Heredit is the final boss of the Fortis Colosseum..."), `varb9778`.
- **Dizana's quiver and the tonalztics:** 18 objs (quiver charged/uncharged/infinite,
  trouver and broken variants, the Dizana's max cape and hood), inv `dizanas_quiver_ammo`,
  varps 4141/4142, interface 592, `synth_glaiveofralos` (11 sounds) and the
  `human_glaive_ralos01_*`, `projanim_glaive_*`, `spotanim_glaive_*` seqs and spotanims.

## 7. What our tree already names (74 rows have a used_by)

- **Entry and exit exist only for two quests:** `[oploc1,colosseum_entrance_outside]` is in
  `quests/quest_twilightspromise/scripts/twilightspromise.rs2:351-370`: it enters the arena
  at `^mg_arena_coord` = 0_28_48_31_33 (`quest_meatandgreet/configs/meatandgreet.constant:37`,
  inside the walled square of section 1) or the lobby at 0_28_148_13_50 / 0_28_148_27_12 at
  quest stages, and otherwise prints "The Colosseum entrance."; `[oploc1,colosseum_exit_lobby]`
  (`:372`) teleports to 0_28_48_4_34. `ladders_stairs/configs/maplink.dbrow:445-456` also
  links 0_28_48_3_34/_3_35 to 0_28_148_7_34 through that loc. `tele_destinations.rs2:848` maps "fortis_colosseum" to
  0_28_48_33_37 (`tele_names.enum:719`). `colosseum_entrance` (the lobby's Enter) and
  `colosseum_master` have no handler.
- **The rewards are partly built elsewhere:** Dizana's quiver
  (`interface_equipment/scripts/quiver.rs2`, `quiver.varp`, `player_ranged.rs2:510`), the
  tonalztics (`skill_combat/scripts/player/gear/tonalztics_of_ralos.rs2`, its special
  `pvm_tonalztics_of_ralos_charged.rs2`, `attack_anims_modern.obj`, `attack_sounds.obj`,
  `charges_selftest.rs2`), the pet in the menagerie (`poh_menagerie.rs2:99,146,185`), the
  max cape in the achievement gallery, the jewellery-box teleports
  (`poh_jewellery_boxes.rs2`, `poh_runtime_generated.dbrow:3561-3619`).
- **Generated ledgers name 34 Colosseum npcs** (`npc_combat/c/colosseum_*.combat`) and
  `npc/configs/npc_anims.generated.npc`. They were chosen by rig and name: Sol Heredit's
  ledger gives him the javelin colossus's `_range_attack` and `_death`
  (`npc_combat/c/colosseum_sol_p1.combat`), because the two share a rig. **Not sources.**
- `src/torirsserver/torirs_server_world_selftest.c` names only generic records (human
  anims, `arrow_launch`, an anvil) and the quiver vars.

## What a plan must know

1. **The cache gives identities and assets, not the fight.** It has every monster's record
   (hitpoints, stats, ready/walk) and every sequence, graphic and sound the fight uses, but
   no record binds an attack, defend or death animation to an npc, and no spotanim is bound
   at all. 72 sequences, all 83 spotanims and 332 sounds are `unknown_purpose`: each
   attack-to-asset pairing must come from the corpus (Blert, plugins, video frames).
2. **The modifier table is cache data and can be built from directly**: enum_5312 and its
   fourteen structs give names, level texts, glory values and icons, and clientscripts
   4931-4980 already draw the choice between waves from them (interfaces 626/865, varbit
   9788, varcs 1194/1196, invs 844/845). The server only has to set those vars and invs.
   The rank of each modifier lives in a `_stacks_client` varbit (one per levelled modifier;
   Bees! uses the one named toxicity).
3. **The wave table, the reinforcements, the reward table and the loot rates are not cache
   data.** `enum_5318` lists the nine monster kinds keyed 1-9 and is read by nothing. The
   corpus must give the waves; the plan must not read a wave order into enum_5318's keys.
4. **Glory is client display code plus server state.** The client recomputes a wave's
   glory for display (section 6); the server's own formula, the "Lots!" cap (max int) and
   the cash-out choice are not in the cache. Two client scripts disagree on rank weighting
   (4956 vs 4943/4930): settle it against a recording before writing the server formula.
5. **Sol Heredit is one npc record (12821, 1500 hp).** Phases, the grapple, the
   triple attack, the pools and the shrinking arena are server behaviour; the cache gives
   their animations (lengths 3-12 ticks) and 291 sounds, 215 of them with no binding.
6. **The arena is m28_48 (region 7216); the lobby is m28_148 (region 7316).** The fight
   square on level 0 is local x 18-47, z 20-49. Whether the fight is an instance copy is not
   cache data. The rewards chest, the exit gate, the molten pools and the "Molten Sand" loc
   are placed by no map: the server spawns them.
7. **Entry already half exists, for quests.** The outside entrance and the lobby exit have
   handlers in `twilightspromise.rs2`; `colosseum_entrance` (lobby, Enter) and Minimus's
   Start-wave/Leave have none. A build must take the existing handler over, not add a
   second `[oploc1,colosseum_entrance_outside]`.
8. **Skeletal lengths are inferred.** 41 of 166 sequences are skeletal; their tick lengths
   assume one keyframe per 20 ms client cycle. Confirm one with a frame count before a spec
   row relies on it.
9. **Death sequences of the human-rigged monsters end on a 20,000-cycle frame** (668.23
   ticks), as Yt-HurKot's does in the Inferno: the despawn must not wait for them.
10. **Our generated combat ledgers are wrong for Sol** (they give him the javelin colossus's
    attack and death) and are guesses for the rest; do not cite them.

## Re-check when the corpus lands

- Every `unknown_purpose` sequence and spotanim: which attack, tell, projectile or impact
  each is (Sol: `_melee_attack_telegraph`, `_grapple_attack_telegraph`, `_shieldslam_telegraph`,
  `_tripleattack(_shorter)`, `_arena_jump/_land`, spotanims 2666-2672, 2680, 2699-2706,
  2724; manticore orb spotanims 2681-2686 and the triple charge/throw order; javelin
  spearhead/artillery 2673-2678; shockwave clap 2679; minotaur melee/magic heal; jaguar,
  shaman and trio attacks; bee jar 2707/2708; totem 2687/2688).
- The 332 synth-only sounds: which the server plays and when (Sol 215, modifiers 52,
  manticore 35, javelin 9, glaive 6, doom 6, shockwave 5, minotaur 2, trio 1, shaman 1).
- The wave table and reinforcements; whether `enum_5318`'s keys mean anything.
- The reward table, the per-wave pool growth and the cash-out rules; the pet rate.
- Glory: the server formula, the rank weighting dispute, and the tier rewards
  (emote 53 at 20,000 is the only one the cache shows).
- The units filed by name only: the Doom Scorpion and `synth_doomsnail` (Doom modifier?),
  `colosseum_holy_fire` "Molten Sand" and the molten pools (Reentry?), the beam crystal and
  solar-flare npcs, the gibs, the Pillar `colosseum_safespot_dying`, the gladiators and the
  Passionate Supporter, the `*_echo` records (a leagues echo Sol has its own dbrow,
  `leagues_echo_sol_heredit`, excluded).
- The vars no clientscript reads (7 varps, 19 varbits): what the server stores in each.
- The fight tiles, the spawn tiles, and whether the arena is an instance.
- One skeletal sequence's length against a video frame count.
