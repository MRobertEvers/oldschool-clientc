# Colosseum: which animations each monster can play (rig pass)

Pass `matthew-mbp-m4-waves-b1-rig-colosseum`, method `docs/minigames/waves_loop/RIG_PASS.md`. Documents only.
The cache npc records bind only ready and walk animations; every other candidate below is on the same
framemap (rig) as those two, or is matched by name words only. Per-unit tables and notes:
`sources/rig/<unit>.tsv` and `.md` (nine units). Ids are osrs239 cache ids.

Tiers, weakest last: `bound` (the npc record names it) > `rig+name` (on the rig; the Jagex name states the
role) > `rig+sound` (on the rig; a frame sound's name states it) > `rig` (on the rig, role unknown) > `name`
(shared or no rig; name words only). Anything below `bound` is a CANDIDATE, not a fact.

## The colossi: Sol Heredit, javelin colossus, shockwave colossus (rig 2245, shared, 24 seqs)
One rig carries all three colossi. The names separate them; a sequence named for another colossus is
`rig`/unknown for the others and is never offered to them.

**Sol Heredit** (`colosseum_sol_p1` 12821; `colosseum_boss_seated` 12827, bound to sitting idle 10875 only).
Bound: ready 10874 `npc_colossi_finalboss_01_idle`, walk 10878.
- attack1 10882 `..._01_melee_attack` (4.0 game ticks, no frame sound), rig+name.
- attack2 10883 `..._01_melee_attack_telegraph` (6.0), attack3 10884 `..._01_grapple_attack_telegraph` (4.0), rig+name.
- attack4 10885 `..._01_shieldslam_telegraph` (4.0), attack5 10886 `npc_colossi_finalboss_tripleattack` (12.0),
  attack6 10887 `_tripleattack_shorter` (11.0), rig+name. The worker had these as `special`; nothing states
  special, so the closer made them attack variants.
- transition 10876 `..._01_arena_jump`, 10877 `..._01_arena_land` (3.0 each), rig+name.
- death 10888 `npc_colossi_finalboss_01_death` (8.33, boss death sounds), rig+name.
- spawn, defend: none on the rig. No seated-to-standing sequence is named.
- Graphics (name): 2667/2668 tripleattack telegraphs (12.0/11.0, equal to 10886/10887), 2666 colossi_01_land,
  2669-2672 finalboss_01..04_melee, 2680 finalboss_01_death (8.33, equals 10888), 2724 finalboss_explosion_01.
- Solar flare 12826: rig 2226 holds one sequence (its idle 10817). `colosseum_sol_gib` 12832: no rig.

**Javelin colossus** (12817). Bound: ready 10889, walk 10879.
- attack1 10892 `npc_colossi_javelin_01_range_attack` (3 ticks), attack2 10893 `..._artillery_attack` (3), rig+name.
- death 10894 `npc_colossi_javelin_01_death` (4), rig+name.
- spawn 10891 `..._01_walkfade`, **rig+sound** (closer: was rig+name; the name says walk+fade, only the frame
  sound `javelin_colossi_appear` states a spawn). Alternate walk 10890 rig+name. No defend, no special.
- Graphics (name): 2673 spearhead, 2674/2675 artillery slow/fast, 2676 artillery_fire, 2677/2678 spearhead_fire.
- `colosseum_colossi_gib` 12831: no rig.

**Shockwave colossus** (12819). Bound: ready 10902, walk 10881.
- attack1 10903 `npc_colossi_shockwave_01_clapattack` (3 ticks; sounds open_arms, charge, clap, projectile), rig+name.
- death 10895 `npc_colossi_shockwave_01_death` (4), rig+name. Silent walk 10880 is the Sol pet's walkanim.
- Spawn, defend, special: none. Graphic (name): 2679 `npc_colossi_shockwave_01_clapattack`.

## Human-rig monsters: jaguar warrior, serpent shaman, Fremennik trio (rig 0, ~3,905 seqs)
The human rig does NOT close the set. Every candidate here is tier `name`, never better.

**Jaguar warrior** (12810; human_ready 808, human_walk 819-822).
- attack1 10847 `npc_jaguar_ranger_claws_attack` (swipe sounds named varlamore_jaguar_warrior_*), name.
- defend 10848 `npc_jaguar_human_unarmed_def`, death 10849 `npc_jaguar_human_death`, name.
- Off-rig jaguar seqs 12491, 12492, 12498, 12499 sit on framemap 1969 (the quadruped jaguar): not this npc.
- No spotanim names a jaguar.

**Serpent shaman** (`colosseum_standard_mager` 12811; human_staffready 813, halberd walk 1205-1208).
- attack1 10859 `npc_serpent_mager_casting` (sound varl_serpent_shaman_water_cast_01), name.
- death 10860 `npc_serpent_mager_death`, name. No defend, spawn or special by name.
- No spotanim names the serpent; three lizardshaman spotanims share only "shaman" and are another monster's.

**Fremennik trio** (12814 archer, 12815 mage, 12816 melee; `colosseum_human_gib` 12828 has no rig).
- archer: attack 10850 `npc_fremennik_warbander_archer_att_colosseum` (arrow_launch), defend 10851, death 10852.
- mage: attack 10853 `..._mage_zaros_vertical_casting_walkmerge` (fire_cast; casts while walking), defend 10854, death 10855.
- melee: attack 10856 `..._melee_human_sword_stab`, defend 10857, death 10858. All tier name.
- Graphics (name, role other): 2713-2720 `vfx_colosseum_human_explosion_01..08`, probably the gib's; unstated.

## Manticore (12818, private rig 2249, 9 seqs: the rig closes the set)
Bound: ready 10863, walk 10864, backwards walk 10865.
- attack1 10869 `npc_manticore_01_triple_throw` (3 ticks; whoosh and projectile sounds), rig+name.
- transition 10868 `npc_manticore_01_triple_charge` (4 ticks; roar, charge, projectile sounds), rig+name. The
  worker had it `special`; nothing states special. Charge = windup by name; its projectile sound means it may
  itself release. Undecided until Blert or a plugin.
- death 10866 `npc_manticore_01_death` and 10867 `..._death_explode` (4 each), rig+name. spawn 10870, 10871, rig+name.
- No defend. Graphics (name): 2681/2683/2685 magic/ranged/melee projectiles and 2682/2684/2686 impacts, all on
  the LEVIATHAN projectile seqs; 2721 `vfx_colosseum_manticore_explosion_01`. `colosseum_manticore_gib` 12829: no rig.

## Minotaur (12812 and `colosseum_minotaur_routefind` 12813; rig 1758, shared with the zamorak demon boss)
Bound: ready 10840, walk 10842.
- attack1 10843 `npc_minotaur_boss_attack_melee` (2 ticks), attack2 10844 `..._attack_magic` (sounds heal_charge,
  heal_cast), attack3 11747 `..._attack_melee_louder`, rig+name.
- 11584 `mag_minotaur_melee`, 11585 `mag_minotaur_magic`: attack, rig+sound.
- spawn 10845 `npc_minotaur_boss_spawn` (5 ticks), rig+name. defend 10841 `npc_minotaur_boss_defend`, rig+name.
- death 10846 `npc_minotaur_boss_death` (3), 11748 `_death_louder`, 11587 `mag_minotaur_death`, rig+name.
- walk variants 11746, 11582, 11583; transition 11586 `mag_minotaur_ramp` (its one sound is cow_death, a stray).
- Graphic (name): 2723 `vfx_colosseum_minotaur_explosion_01`. 7 zamorak_demon_boss_* rows: rig, unknown.
  `colosseum_minotaur_gib` 12830: no rig.

## Modifiers and props
- **Bee swarm** 12823 (rig 1815, shared with tob_bloat and nightmare flies): ready 10822; attack 10823, spawn 10821,
  death (despawn) 10824, rig+name. Graphics: 2707 jar impact, 2708 jar travel (on a Zebak seq).
- **Beam crystal** 12824 (rig 2227): ready 10799; spawn 10798, transition (charge) 10801, attack 10802, death
  (despawn) 10800, rig+name. Beam graphics 2689-2697 and echo copies 3147-3155; their seqs 10803-10811 are graphics.
- **Healing totem** 12825 (rig 2231): ready 10827; attack 10828, rig+name. No spawn or death on its rig.
  Graphics: 2687 projectile, 2688 impact.
- **Doom scorpion** 12822 (rig 1426, the generic scorpion rig): ready 6252 `scorpion_update_ready`, walk 6253
  bound. attack1 6254 `scorpion_update_attack_tail`, defend 6255, death 6256, rig+name (same family as its
  bound pair). small_scorpion 6257-6261 and nature-spirit scorpion 12519/12520 are other npcs'; lobster
  6263-6267 rig, unknown. Graphics (name): 2711 player_death, 2712 attack_impact. The worker wrote "no rig";
  the closer found the binding in cache_npc.txt and added the 20 rows.
- **Pillar** (`colosseum_safespot_dying` 12820): no animation, no rig.

## Unknown
- Which Sol attack fires in which phase; the grapple versus shield slam; the four melee explosions per hit.
- Javelin range versus artillery, and its projectile spotanim. The shockwave's projectile and landing time.
- Manticore: which style each throw is, whether triple_charge releases, which death plays in the arena,
  what the two spawns mean. Minotaur: the magic (heal) attack's graphic; whether the mag_/louder sets are used.
- Every human-rig candidate (jaguar, serpent, trio) is name-only; their projectiles are unnamed.
- Doom scorpion: whether it really uses the generic scorpion set. Totem death; crystal beam pairing.
- Every gib (sol, colossi, manticore, minotaur, human): no rig; probably a model plus an explosion spotanim.

## Disagreements with the generated npc_combat ledgers
`OSRS-Content/osrs239-content/npc_combat/c/`. Each is one row in `../waves_loop/CONTENT_BUGS.md`.
- RIG-1 `colosseum_sol_p1.combat:15` death_anim `npc_colossi_javelin_01_death` (a3): the javelin colossus's
  death; Sol's own is 10888.
- RIG-2 `colosseum_sol_p1.combat:16` attack_anim `npc_colossi_javelin_01_range_attack` (a3): the javelin's;
  Sol's own melee is 10882. Its in-band attack and death sounds follow the wrong sequences.
- RIG-3, RIG-4 `colosseum_boss_seated.combat:15,16`: the same two rows.
- Not disagreements: javelin, shockwave, minotaur, manticore, bees, crystal, totem and doom scorpion agree
  with the rig (the ledger omits javelin artillery 10893 and minotaur magic 10844, and manticore has no
  attack); the human-rig monsters' ledgers are all `-` (silent, not wrong).

## What promotes a candidate
- Blert's attack tables (animation ids per attack, per monster) once the Colosseum corpus lands.
- Plugin constants that carry animation and graphic ids for these npcs (Colosseum helper plugins).
- A picture by the test driver of the candidate played on the npc's own model (confirms the rig fit,
  the death pose, and the name-only human-rig rows).
- Never: resemblance to another monster, or a generated ledger row.
