# The Inferno: audiovisual and presentation inventory

`AV_INVENTORY.tsv` has 386 rows. Each row is one rev-239 cache asset that ships for the Inferno, set against where our content uses it. The columns are `kind, id, cache_name, unit, purpose, used_by, status, detail`.

The assets come from `sources/cache_{npc,seq,spotanim,sounds,locs,vars,objs,interfaces,enums_dbrows,music}.txt`. `tools/waves_gate/cache_dump.py` writes those files; its selection rules are in `sources/LEDGER_cache.md`.

Paths below are relative to the worktree. `MI/` means `OSRS-Content/osrs239-content/server/scripts/minigames/minigame_inferno/`. Every finding that is a defect in our content is a row in `docs/minigames/waves_loop/CONTENT_BUGS.md` (INF-AV-001 to INF-AV-009).

**How "used" was decided.** Each asset was matched whole-symbol against every non-comment line of these files:
- `MI/` (scripts and configs);
- the 13 other server files that name an Inferno symbol: `player/death.rs2`, `login.rs2`, `logout.rs2`, `areas/world/configs/m38_79.spawn`, the POH gallery and menagerie, `tele_destinations.rs2`, `tele_names.enum`, `loc_transform_carriers.varp`, `skill_combat/combat.rs2`, `inferno_potions.rs2`, `godwars_private.rs2`, and `cheat_max_gear.rs2` (debug);
- `src/torirsserver/*.c`.

Hits are sorted as follows:
- Hits in `minigame_fightcave/` and `minigame_rs2012_qbd/` are recorded as `also used elsewhere` and do not count.
- A config block header (`[inferno_creature_melee_small]` in `inferno.npc`) is a declaration, not a use.
- A hit inside a block reachable only from a `[debugproc,...]` goes to `detail` as `debug:`. These are `::inferno`, `::zuk`, `::zukanim`, `::zukseal`, `::zukstill`, `::zuklos`, `::zukhp`, `::zukquiet`, `::infernopause`, and the queues and procs only they reach (`inferno_zuk_anim`, `inferno_zuk_seal`, `inferno_zuk_still`).
- A hit in `inferno_zuktest.rs2`, `selftest_inferno_practice_fee` or `torirs_server_world_selftest.c` goes to `detail` as `tests:`.

Neither kind counts as used. Reachability was computed over every `[trigger,name]` block from the engine-fired triggers through `~proc`, `queue(`, `softtimer(`, `gosub(` and `@label` edges.

An asset also counts as used when it is reached through other content:
- **Sequences:**
  - the `readyanim`, `walkanim` or `walkanim_b/l/r` of an npc we spawn (the cache record);
  - the `death_anim` or `defend_anim` of such an npc, from the `inferno.npc` block or, failing that, `npc_anims.generated.npc`. The engine plays these itself on a hit and a death. An npc nothing can attack (TzHaar-Ket-Keh has no Attack op) is excluded.
  - the `anim=` of a spotanim we spawn or of a placed loc.
- **Sounds:** the `soundid=` of a placed loc. A frame sound of a sequence we play would also count, but no Inferno sequence has one.
- **Locs:** a loc the cache map places in the instanced square (m35_83) or in the live entrance area, or that a live line adds with `loc_add` / `loc_change`, or a multiloc child that a placed parent can resolve to.
- **Npcs:** an npc is used when it is spawned: by a live `npc_add` or `~inferno_spawn*`, by `areas/world/configs/m38_79.spawn`, or as a `multinpc` child of one of those. An npc only referenced in handlers is `unused`, with `referenced in handlers only, never spawned` in detail. This is stricter than the raid inventory.
- **Vars:** a var that is read and never written is `unused`.
- **Clientscripts:** run by the onload chain of an interface we open.
- **Music:** played by the engine's region table.

**Purpose** comes from these sources, and anything else is `unsourced`:
- the cache's own bindings: an npc record's animations, a spotanim's or loc's `anim=`, a multiloc or multinpc table and the value that selects each child, a loc's `soundid=`, and the clientscript that reads a var;
- `docs/INFERNO_SOUNDS.md`, with the layer of each sound;
- `docs/BOSS_ASSETS.md`, whose spotanim and sequence rows are Kronos ids;
- the Kronos JSON ids quoted in `MI/configs/inferno.npc`;
- the corpus pass's pins: `sources/CODE_CONSTANTS.md`, Blert `InfernoNpc.java`, the kotori/OpenOSRS Inferno plugin, inferno_scouter, inferno_2d_map, and triple_jad_sim's `RESEARCH.md`.

A cache name is never a purpose. An unused asset with no sourced purpose is `unknown_purpose`, and it is left alone. A record's own `name=` (an npc, obj or loc display name) is quoted as its identity.

**Borrowed** means our content uses an asset whose only claim to the Inferno is that our script names it. The detail column gives the source for the choice, or `unsourced`.

For sounds the status carries the provenance layer. A sound a source **states** (layer w, k or c) is `used`. A sound that is **derived** (layer t, f or d) is `borrowed`, and its detail says `derived (layers ...)`.

## Counts (used / unused / used_wrong_place / unknown_purpose / borrowed)

| kind | nibblers and pillars | bat | blob | melee | ranger | mager | jad | triple jad | zuk | entrance and systems | rewards | shared | unknown | total |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| seq (96) | 5/1/0/0/0 | 4/0/0/0/0 | 10/0/0/0/0 | 7/0/0/0/0 | 6/0/0/0/0 | 6/0/0/0/0 | 14/0/0/0/4 | . | 13/5/1/2/2 | 4/0/0/3/0 | 0/3/0/4/0 | 1/0/0/0/0 | 0/1/0/0/0 | 70/10/1/9/6 |
| spotanim (25) | . | 1/0/0/0/0 | 4/0/0/2/0 | . | 1/0/0/0/0 | 1/0/0/0/0 | 0/0/0/0/7 | . | 1/1/0/4/2 | . | . | . | 0/0/0/1/0 | 8/1/0/7/9 |
| sound (37) | . | 0/0/0/0/3 | 1/0/0/0/3 | . | . | . | 1/0/0/0/2 | . | 5/1/0/6/2 | 1/0/0/0/0 | . | 5/0/0/0/4 | 0/0/0/3/0 | 13/1/0/9/14 |
| loc (106) | 0/7/0/0/0 | . | . | . | . | . | . | . | 20/0/0/1/0 | 17/0/0/0/0 | 2/1/0/0/0 | 57/0/0/0/0 | 0/1/0/0/0 | 96/9/0/1/0 |
| npc (30) | 2/1/0/0/0 | 1/0/0/0/0 | 4/0/0/0/0 | 1/1/0/0/0 | 1/0/0/0/0 | 1/0/0/0/0 | 2/0/0/0/0 | . | 7/0/0/0/0 | 3/0/0/0/0 | 1/5/0/0/0 | . | . | 23/7/0/0/0 |
| varbit (26) | 3/0/0/0/0 | . | . | . | . | . | . | . | 3/0/0/0/0 | 0/0/1/2/0 | 0/17/0/0/0 | . | . | 6/17/1/2/0 |
| varp (5) | 1/0/0/0/0 | . | . | . | . | . | . | . | 1/0/0/0/0 | 1/0/0/0/0 | 0/1/0/0/0 | . | 0/0/0/1/0 | 3/1/0/1/0 |
| interface (3) | . | . | . | . | . | . | . | . | 1/0/0/0/1 | . | . | 0/0/0/0/1 | . | 1/0/0/0/2 |
| clientscript (7) | . | . | . | . | . | . | . | . | 5/0/0/0/2 | . | . | . | . | 5/0/0/0/2 |
| music (2) | . | . | . | . | . | . | . | . | . | 2/0/0/0/0 | . | . | . | 2/0/0/0/0 |
| obj (22) | . | . | . | . | . | . | . | . | . | 1/0/0/0/0 | 6/15/0/0/0 | . | . | 7/15/0/0/0 |
| dbrow (8) | . | . | . | . | . | . | . | . | . | 1/2/0/0/0 | 0/5/0/0/0 | . | . | 1/7/0/0/0 |
| struct (19) | . | . | . | . | . | . | . | . | . | . | 0/19/0/0/0 | . | . | 0/19/0/0/0 |
| all (386) | 11/9/0/0/0 | 6/0/0/0/3 | 19/0/0/2/3 | 8/1/0/0/0 | 8/0/0/0/0 | 8/0/0/0/0 | 17/0/0/0/13 | . | 56/7/1/13/9 | 30/2/1/5/0 | 9/66/0/4/0 | 63/0/0/0/5 | 0/2/0/5/0 | 235/87/2/29/33 |

- **The `triple_jad` column is empty on purpose.** Wave 68 uses exactly the wave-67 records: `inferno_jad`, `inferno_jad_healer` and their sequences. The cache ships nothing that belongs only to it; the 9-tick rate and the 0/3/6 offsets are server data.
- **`shared`** holds the arena's structural scenery (floors, walls, pillar edges) and the sounds two units share: `lizard_cleric_*` for Yt-HurKot and Jal-ImKot, `magmaquiris_*` for Jal-Xil and Jal-Zek, `dragon_*` for JalTok-Jad and TzKal-Zuk, and `cave_collapse_1`.
- **`unknown`** holds records the name rule caught that no binding places in the encounter: `lore_inferno*`, `tzhaar_inferno`, `poh_amulet_xeric_inferno` (Xeric's Talisman's "Inferno" teleport op) and `varp1574`.

## 0. Wrong-place findings (2), and the borrowed assets a source contradicts (1)

- **varbit 5646 `varb5646_inferno_sacrificed_firecape`** is written as 1 at `MI/scripts/inferno.rs2:227`.
  - The entrance loc 30352 resolves values 0 and 1 to `inferno_entrance_noop`, which has no op. Only 2 and up resolve to `inferno_entrance_op` (op1 Jump-in): `sources/cache_locs.txt:1602-1605`. The rule that `multiloc1` is value 0 is `tools/loc_var_audit.py:9`.
  - So no value our content writes ever offers Jump-in. `[oploc1,inferno_entrance_op]` (`MI/scripts/inferno.rs2:273`), and the fire-cape sacrifice inside it, are unreachable by click. Only the debugprocs enter. (INF-AV-001)
- **seq 2863 `dagannoth_water_creature_walk`** is Jal-MejJak's `defend_anim` at `MI/configs/inferno.npc:439`.
  - The content follows Kronos `Jal-MejJak.json` (`MI/configs/inferno.npc:434`). The cache binds 2863 as npc 7708's `walkanim` (`sources/cache_npc.txt:561`).
  - The rig's own `dagannoth_water_creature_defend` 2869 is never played. This is a Kronos-against-cache conflict for a video to settle. (INF-AV-004)
- **spotanim 130 `fireblast_travel`** (borrowed, contradicted) is Jal-MejJak's heal beam and lava ball, at `MI/scripts/inferno_adds.rs2:453,529`.
  - `docs/BOSS_ASSETS.md:250` and the Kronos line quoted at `MI/scripts/inferno_adds.rs2:526` name spotanim 660.
  - In this cache 660 is `wild_falloff_meteor_flying` (`sources/cache_spotanim.txt:75`), the flying half of the 659 `wild_falloff_meteor_blast` splash the same proc already plays.
  - The comment's reason for not porting it ("a rev-184 id") does not hold, because spotanim ids are stable. (INF-AV-005)

**Sounds played twice: none.** None of the 96 selected sequences carries a frame `sound=` (`sources/LEDGER_cache.md`, "Negative results"), so no `sound_synth` can double an in-band sound.

## 1. Animations the cache has and we never play

For each monster, the sequences on its rig that no live line or binding of ours plays:

- **Jal-Nib, the pillars.**
  - `safe_spot_distructible_pillar_collapse` 7561 (22 frames, 2 ticks).
    - `docs/BOSS_ASSETS.md:239` maps it to the Zuk seal's collapse in the LostCity port.
    - The pinned plugins pair the pillar with npc 7710, the dying pillar (`sources/inferno_scouter/src/main/java/com/infernoscouter/InfernoScouterPlugin.java:74`).
    - The content's pillar dies on `npc_param(death_anim)` of 7709, whose rig has none. (INF-AV-003)
- **Jal-MejRah, Jal-Ak, Jal-ImKot, Jal-Xil, Jal-Zek.**
  - Every sequence on these rigs is played.
  - `jalimkot_digdown` 7600 and `jalimkot_digup` 7601, and `jalakxil_resurrect` 7611, are played, but with no sound (section 3).
- **JalTok-Jad, Yt-HurKot.**
  - Every rig sequence is played. The pet sequences `jaltokjad_walk_pet` 8857 and `jaltokjad_chathead_pet` 8858 belong to the JalRek-Jad pet (rewards).
  - Yt-HurKot dies on `lizard_cleric_death` 2638 (Kronos `Yt-HurKot.json`, `MI/configs/inferno.npc:396`). Its last frame is 20,000 cycles long, so the sequence totals 669.67 game ticks (`sources/cache_seq.txt`). This is the quirk `docs/BOSS_ASSETS.md:234-237` records.
- **Jal-MejJak (Zuk's healers).**
  - `dagannoth_water_creature_spring_up` 2864 is its spawn (`docs/BOSS_ASSETS.md:230`), and it is never played (INF-AV-006).
  - `dagannoth_water_creature_defend` 2869 and `_death` 2866 are listed as unported (`docs/BOSS_ASSETS.md:231`).
  - `dagannoth_water_creature_spine_travel` 2875 has no sourced purpose. It is the one sequence of the four-member sound join (`docs/INFERNO_SOUNDS.md:148`, with sound 1623).
- **TzKal-Zuk.**
  - `zuk_spawn_no_rock` 13717 (70 frames, 7 ticks) is unknown_purpose. `MI/configs/inferno.constant:183-184` asserts it is "for re-fights" with no source.
  - `zuk_proj_short` 10106 is the `anim=` of spotanim 2382 (section 2), which is never spawned.
- **TzHaar-Ket-Keh.**
  - `thzaar_staff_attack/parry/death` 2612/2606/2608 are unknown_purpose here. Nothing can attack him, and the generated ledger chose them by name.
- **Pets (rewards).**
  - The `zukrek_*` sequences 7975-7979 belong to TzRek-Zuk. Its ready and walk anims are bound; the other three are unknown_purpose.

**Played with no sound at all.** The content plays each of these and no sound accompanies it, in-band or by `sound_synth`:
- `jalnib_attack` 7574 (on a pillar, `MI/scripts/inferno_ai.rs2:93`). This silence is deliberate (`MI/configs/inferno.npc:762-782`).
- `jalimkot_digdown` 7600 and `jalimkot_digup` 7601 (`MI/scripts/inferno_ai.rs2:226,229`).
- `jalakxil_resurrect` 7611 (`MI/scripts/inferno_zek.rs2:134`).
- Jal-MejJak's heal throw `dagannoth_water_creature_attack` 2868 (`MI/scripts/inferno_adds.rs2:468`). Its lava throw at :411 does play the attack sound.
- `moving_safe_spot_death` 7569 (`MI/scripts/inferno_glyph.rs2:102`). The glyph's death is open in `docs/INFERNO_SOUNDS.md` section 11.
- JalTok-Jad's attacks on the Ancestral Glyph (`MI/scripts/inferno_jad.rs2:96,184,202`). These play neither the attack sound nor the impact sound. (INF-AV-007)

## 2. Graphics and projectiles never spawned (8 of 25)

- **`wild_falloff_meteor_flying` 660.** The projectile the tree's sources name for Jal-MejJak (section 0).
- **`inferno_babysplitter_mage_big` 1609 and `_biggest` 1610.** Unknown_purpose. No binding or pin names them.
- **`inferno_zuk_projectile_small` 2261, `_mid` 2381, `_mid_short` 2382 and `_gigantic` 3294.** Unknown_purpose. Their ids sit far above the 2017 Inferno block (1375-1382), so they are probably later content reusing the model. The corpus has not named them.
- **`tzhaar_inferno` 453.** Unknown_purpose. It is a TzHaar graphic whose name contains the word.

**Borrowed graphics (9).**
- The Jad set: `tzhaar_fire_spit_launch` 447, `_travel` 448, `_follow_travel` 449, `_end_travel` 450, `tzhaar_rock_smash` 451 and `firewave_impact` 157.
- `tzhaar_heal` 444.
- `wild_falloff_meteor_blast` 659.

Each is sourced to a Kronos id in `docs/BOSS_ASSETS.md:251-256`. `fireblast_travel` 130 is the exception: it is unsourced and contradicted (section 0).

**Kronos-only (not quoted in the tree).** The blob and bat projectiles 1378-1382 are used under their cache names. Kronos fires 1380 `inferno_splitter_mage` for the blob's *ranged* attack and 1378 `inferno_splitter_range` for its *magic* (Kronos `JalAk.java:11-12`, outside the tree). The baby blobs and the bat agree with the names. See "What the spec pass must settle".

## 3. Sound effects

There are 37 sound rows. Of these:
- 13 are `used`, because a source states them (layers w, k, c);
- 14 are `borrowed`, because they are derived (layers t, f, d);
- 1 is `unused`;
- 9 are `unknown_purpose`.

No sound is in-band. No test asserts a sound.

| id | name | layer, `docs/INFERNO_SOUNDS.md` line | status | where it plays |
|---|---|---|---|---|
| 155 | fireblast_cast_and_fire | **w** Zuk attack (:234) | used | `attack_sound` of Zuk, `MI/scripts/inferno_zuk.rs2:442` |
| 156 | fireblast_hit | **w** Jal-MejJak lava lands (:257); t Zuk shot lands (:255-256) | used | `MI/scripts/inferno_adds.rs2:541`, `MI/scripts/inferno_zuk.rs2:474,501` |
| 163 | firewave_hit | **k** Jad projectile on the player (:253); on the glyph (:254) | used | player only: `MI/scripts/inferno_jad.rs2:243,251`. The glyph row is not played (INF-AV-007) |
| 410 | dragon_hit | **w** Zuk and Jad defend (:234-235) | used | engine `defend_sound` |
| 600 | magmaquiris_hit | **w** Jal-Zek defend (:238); f Jal-Xil defend (:239) | used | engine |
| 608 / 610 / 609 | lizard_cleric_attack / hit / death | **c** Yt-HurKot (:236); f Jal-ImKot (:240) | used | `MI/scripts/inferno_adds.rs2:246`, `MI/scripts/inferno_ai.rs2:242`, engine |
| 1615 / 1622 / 1621 | dagganoth_attack / hit / death | **c** Jal-MejJak (:237) | used | `MI/scripts/inferno_adds.rs2:412`, engine |
| 3528 | surok_mindcontrol_impact | **w** Jal-Ak and Jal-AkRek-Mej magic (:242, :252) | used | `MI/scripts/inferno_ai.rs2:179,188`. :179 also plays it for the blob's **ranged** attack, which the wiki note does not state (derived) |
| 166 | heal | d Yt-HurKot heal (:258) | borrowed | `MI/scripts/inferno_adds.rs2:231` |
| 408 | dragon_attack | t Jad attack (:235) | borrowed | `MI/scripts/inferno_jad.rs2:113,236,248` |
| 409 | dragon_death | t Zuk and Jad death (:234-235) | borrowed | engine |
| 598 / 599 | magmaquiris_attack / death | t Jal-Zek (:238); f Jal-Xil (:239) | borrowed | adds and ai, engine |
| 595 / 597 / 596 | lavabeast_attack / hit / death | f Jal-Ak family (:241-243) | borrowed | ai, engine |
| 291 / 296 / 295 | firebat_attack / hit / death | f Jal-MejRah (:244) | borrowed | `MI/scripts/inferno_ai.rs2:138`, engine |
| 2039 | cave_collapse_1 | f seal flanks and pillar collapse (:215, :217) | borrowed | `MI/scripts/inferno_pillars.rs2:114`, `MI/scripts/inferno_zuk.rs2:332` |
| 2045 | cave_rumbling_1 | f cutscene shake (:214) | borrowed | `MI/scripts/inferno_zuk.rs2:253` |
| 2294 | cavein | f seal crumbles (:216) | borrowed | `MI/scripts/inferno_zuk.rs2:381` |
| 2066 | fire_crackling_med | cache: `soundid` of `tzhaar_sulphar_vent`, placed at the entrance | used | client, from the loc |
| 1623 | dagganoth_spines | c join member, paired with seq 2875 (:148) | unused | none |
| 601 | magmaquiris_spines | none | unknown_purpose | none |
| 2040-2044, 2046 | cave_bats, cave_bubbling_loop_1, cave_growl_1, cave_insects_1, cave_insects_2, cave_rumbling_2 | none: the cave block (:206) was considered for the cutscene and not chosen | unknown_purpose | none |
| 4502, 4524 | lore_inferno_impact, lore_inferno | none: name match only | unknown_purpose | none |

**Still silent, as `docs/INFERNO_SOUNDS.md` section 11 leaves them:**
- Jal-Nib (deliberate);
- the Ancestral Glyph's death;
- Jal-MejJak's heal throw;
- the Jal-ImKot dig;
- the Jal-Zek resurrection.

## 4. Locs (106)

- **The arena.**
  - `maps/m35_83.jl2` places 69 distinct locs. All are used because the square is instanced.
  - The floors, walls, pillar edges, lowered and fixed floor and the invisible blockers are `shared`.
  - `inferno_exit` 30283 is placed at 29,13. Its `anim=inferno_exit` (7570) runs from the loc, and both of its ops are handled (`MI/scripts/inferno.rs2:288,299`).
- **The pillars (not placed).**
  - The pillar locs `inferno_safespot1..3` (30353-30355) and their four damage states `inferno_safespot_100/75/50/25` (30284-30287) are not placed.
  - The multiloc table maps damage values 0-63, 64-127, 128-191 and 192-255 to the four states (`sources/cache_locs.txt:1617`, ff.). Value 256 is `-1`, so value 255 still draws the 25% state; the loc must be removed for the pillar to vanish.
  - No map, script or engine file places these locs. The supports are the near-invisible npc 7709 alone. (INF-AV-002)
- **The Zuk seal.**
  - These are all used (`MI/scripts/inferno_zuk.rs2:82-149, 324-347, 370-400`):
    - `state1` walls 30336-30338 are placed by the map;
    - `state2` 30339-30344 and `state3` 30345/30346 are added by the content;
    - the prison roof 30356 is placed. It is a multiloc on `varb5652` (0 is the roof, 1 is hidden), and the content writes it.
  - One state1 piece, `inferno_collapsing_adjacent_corner_join_left_state1` 30335, is neither placed nor referenced: unknown_purpose. Kronos spawns its state2 twin 30342 directly (`docs/BOSS_ASSETS.md:264-266`).
- **The entrance (live map).**
  - `inferno_entrance` 30352 is placed twice: m38_80 61,4 and m38_79 56,51.
  - The 13 TzHaar and Mor Ul Rek floor, vent and gas locs around it are placed. The vent's `soundid` 2066 and the gas's `anim=gas` 523 come with them.
  - The entrance's multiloc children: see section 0 for why Jump-in is never shown.
- **Not the encounter.** `poh_mounted_infernal_cape` and `poh_mounted_max_cape_infernalcape` are used by the POH gallery. `osb5_display_zuk` and `poh_amulet_xeric_inferno` are unused.

## 5. Npc records we never spawn (7 of 30)

- **`inferno_safespot_dying` 7710.** The dying pillar. It is referenced only by a type check in `MI/scripts/inferno_waves.rs2:177` (INF-AV-003).
- **`inferno_creature_melee_small` 12594.** The second Jal-ImKot record.
  - The content gives it handlers and a revive slot (`MI/scripts/inferno_zek.rs2:13,28`) but never spawns it, so its corpse can never exist.
  - `MI/configs/inferno.npc:588-589` calls it "the second of the two" Jal-ImKot records. No source says when the game uses it.
- **`inferno_pet` 7675 (the Jal-Nib-Rek follower), `zuk_pet` 8011 (TzRek-Zuk), `jadpet_inferno` 10625 (JalRek-Jad), and their POH variants 8009 and 10620.** The pet item `infernopet` is awarded (`MI/scripts/inferno.rs2:154,325`), but no pet follower system names these npcs. Not checked further.

All 15 combat records the waves and the Zuk phase need are spawned. So are TzHaar-Ket-Keh (`m38_79.spawn:66`, with his `multinpc` variants `_1op`/`_2op`) and `poh_inferno_pet` (POH menagerie).

## 6. Vars

- **Written and read correctly.**
  - `varb5653_inferno_zuk_hp` and `varb5654_inferno_zuk_base_hp` drive the Zuk health bar. `scripts/inferno_hud_update_739.cs2:3` reads them, and the `varp1575` var-transmit trigger fires `inferno_hud_init.cs2:4`.
  - `varb5652_inferno_prisonroof_hidden` drives the prison-roof multiloc.
  - `varb5655-5657_inferno_safespot1..3_health` are written by `MI/scripts/inferno_pillars.rs2:4-17`, but they drive the multiloc of a loc nothing places (INF-AV-002).
- **Written with a value the cache does not expect.** `varb5646_inferno_sacrificed_firecape` (section 0, INF-AV-001).
- **Never written, though a reader exists.**
  - `varp1585_total_zuk_kills`. Readers:
    - the multivarp of TzHaar-Ket-Keh, whose `_2op` variant carries op3 Exchange (`sources/cache_npc.txt:43-45,695`);
    - the hiscores row's `bossvarp` (`sources/cache_enums_dbrows.txt:19`);
    - `scripts/ca_specific_killcount.cs2:120`;
    - `poh_achievement_gallery.rs2:219`.

    (INF-AV-008)
  - The 13 combat-achievement bits `varb12799-12810` and `varb12921`. (INF-AV-009)
  - `varb11986_collection_bosses_inferno_completed`, read by `scripts/torirs_collection_boss_complete.cs2:54`. (INF-AV-009)
  - `varb6270_poh_menagerie_multiform_infernopet`, read by `scripts/poh_menagerie_drawpet.cs2:23`.
- **Unknown purpose: no clientscript reads them, and nothing writes them.** `varb11878_player_in_inferno`, `varb16561_inferno_shown_gm_helm` and `varp1574_inferno_temp_protect_transmit`.
- **Our own varps.** `varp5889`-`varp6284` in `MI/configs/inferno.varp` are server-allocated, above the cache's last varp (5704). They are not cache assets and are not rows.

## 7. Interfaces

- **The cache ships one Inferno interface:** `inferno_hp_hud` 596, the Zuk health bar.
  - It is opened at `MI/scripts/inferno_zuk.rs2:179` into `toplevel_osrs_stretch:overlay_hud`.
  - Its five clientscripts 735-739 run from its onload and var-transmit chain.
- **The cache has no wave counter, timer, entry warning or practice interface.** The wave number is the chat line "Wave: N", and our content sends it (`MI/scripts/inferno_waves.rs2:69`; the plugins key on it, `sources/CODE_CONSTANTS.md:56`). Entry is a two-option chat choice (`MI/scripts/inferno.rs2:282`).
- **Borrowed:**
  - `fade_overlay` 174 with clientscripts 951 (out) and 948 (in), for the Zuk cutscene's fade. The source is the Kronos `fadeOut`/`fadeIn` shape cited at `MI/configs/inferno.constant:192-194`.
  - `toplevel_osrs_stretch` 161, the client's own top level.
- **The practice fee** (`MI/scripts/inferno_practice_fee.rs2`) is reached only from its selftest. Practice mode is entered only by `::inferno`. Its constant file says practice is NR-custom; the real game has none.

## 8. Music and jingles

- **"Inferno" (archive 500).**
  - Played by the engine's region table for the template square 9043 (`src/torirsserver/torirs_server_music_regions.gen.h:274`). It is unlocked through dbrow `music_inferno` 2811 (varp 17 bit 13).
  - The cutscene takes it away and gives it back: `MI/scripts/inferno_zuk.rs2:63,200`.
- **"Mor Ul Rek" (502).** Plays in m38_79 (region 9807, `gen.h:352`). The second entrance placement sits in m38_80 (region 9808), which has no music row. Blert names 9808 a Mor Ul Rek region (`sources/blert/plugin/src/main/java/io/blert/challenges/inferno/InfernoChallenge.java:54`). This is not a defect claim, only an observation.
- **No jingle matches.** None is played on completion.

## 9. Objs and rewards

- **Used:**
  - `infernal_cape` 21295: awarded at `MI/scripts/inferno.rs2:173`, and taken in the pet trade at :323;
  - `tzhaar_token` (Tokkul): the cape bonus at :174, and the fail formula at :126-131;
  - `tzhaar_cape_fire`: taken at :226;
  - `infernopet`: the 1/100 roll at :146 and :324;
  - `infernopet_zuk`: checked only;
  - the infernal max cape and hood: POH gallery.
- **Unused:** the broken, locked and mangled cape variants, the dummies, the Tzkal slayer helmets, `jad_pet_inferno`, and every dbrow and struct except `music_inferno`. The unused dbrows are the hiscores row, the slayer target, four league "action" rows and the teleport row. The structs are the 12 combat achievements, the boss info and the CA "Defeat TzKal-Zuk" rows.

## What the spec pass must settle (unknown, conflicting or unsourced assets on a kill path)

1. **The pillars.**
   - Whether 7710 `inferno_safespot_dying` and seq 7561 are the pillar's death. The plugins say 7709/7710 are the pillar's two npcs; `docs/BOSS_ASSETS.md:239` says 7561 is the seal's.
   - Whether the locs 30353-30355 must be placed for line of sight and walking to behave.

   Settle from a video frame of a collapse and the plugins' object list. (INF-AV-002, INF-AV-003)
2. **Jal-MejJak.**
   - Its defend (2863 per Kronos, or the rig's 2869) and death (2865 per Kronos, or 2866 `_death`).
   - Its spawn 2864.
   - Its projectile (660 per the tree's sources, against our 130).
3. **The blob's magic and ranged.**
   - **Animations:** Blert keys 7581 = mage and 7583 = ranged (`sources/blert/plugin/src/main/java/io/blert/challenges/inferno/InfernoNpc.java:38-40`). kotori names 7581 range and 7583 magic (`sources/CODE_CONSTANTS.md:53`).
   - **Projectiles:** Kronos swaps 1378/1380 against the cache names.
   - Our content follows the cache names and Blert. A frame count of a prayed blob settles it.
4. **The Zuk-phase projectile family.** `inferno_zuk_projectile_small/mid/mid_short/gigantic` (2261/2381/2382/3294) and `zuk_proj_short` 10106 are unknown. If they belong to TzHaar-Ket-Rak's Challenges, they are not ours.
5. **`zuk_spawn_no_rock` 13717.** Is it a re-fight spawn, as `inferno.constant` asserts unsourced, or a Ket-Rak asset?
6. **The bloblet graphics.** `inferno_babysplitter_mage_big/biggest` 1609/1610.
7. **The second Jal-ImKot record 12594.** When does the game use it?
8. **Every derived sound** (14 rows, layers t/f/d), and the open slots of `docs/INFERNO_SOUNDS.md` section 11.
9. **The entrance and TzHaar-Ket-Keh flow** (INF-AV-001, INF-AV-008):
   - who takes the fire cape;
   - what sets varb5646 to 2;
   - when the Exchange option appears.

## Re-check when the corpus lands

These rows are `unsourced` or rest on a reference outside the tree. A later pass closes them against `sources/`:

- **Sequences chosen by their cache name for the rig** (`MI/configs/inferno.npc:473-495`): `jalnib_defend` 7575, `jalmejrah_defend/death` 7579/7580, `jalak_defend` 7585, `jalimkot_defend/death` 7598/7599, `jalimkot_digup` 7601.
- **TzHaar-Ket-Keh's** 2606/2608/2612; the pet sequences 7976/7978/7979/8858; and 2875 `dagannoth_water_creature_spine_travel`.
- **Spotanims:** 453, 1609, 1610, 2261, 2381, 2382, 3294. Also 1378-1382, whose only id source is Kronos `JalAk.java` / `JalAkRek*.java` / `JalMejRah.java`, which is not quoted in the tree.
- **Sounds:** 601, 2040-2044, 2046, 4502, 4524.
- **Loc:** 30335. **Vars:** `varb11878`, `varb16561`, `varp1574`.
- **Kronos lines that would source an animation or graphic but are not yet quoted in the tree.** All are under `Kronos-master/kronos-server/src/main/java/io/ruin/model/activities/inferno/`:
  - `Inferno.java:130,144`: the player's entry animations 6723 `brut_player_jump_whirlpool` and 4367 `quest_lunar_landing_on_face`, the fade and the four entry messages;
  - `Inferno.java:548,572`: 7561 on the dying pillar;
  - `JalTokJad.java:74`: the Yt-HurKot heal graphic at height 250, where ours is 0 (`MI/scripts/inferno_adds.rs2:220`);
  - `TzKalZuk.java:28`: the heal beam 660.

  The corpus pass should pin them, or a wiki or video equivalent.
- **The wiki pages under `sources/wiki/` were not searched for asset ids.** Monster pages rarely carry them. The `List of sound IDs` rows are already quoted in `docs/INFERNO_SOUNDS.md`.

## Method notes and limits

- **Scripts.** The selection is `tools/waves_gate/cache_dump.py`. The usage and TSV builders are `build/inventory_state/av_build.py` and `av_tsv.py`. Like the raid's builders they are not committed; the progress notebook beside them says how to rerun them.
- **Wrong-place calls were verified by opening the records:**
  - the entrance and pillar multiloc tables;
  - the MejJak record's `walkanim`;
  - spotanim 660 and seq 7561;
  - npc 7709's model and size fields.
- **What was inferred, not run:**
  - that the client cannot click a no-op multiloc child (from `tools/loc_var_audit.py`'s stated rule);
  - that the engine plays `death_anim`/`defend_anim` itself (`docs/INFERNO_SOUNDS.md` section 9).

  No client or server was run for this inventory.
- **The pet follower system and the POH were not audited** beyond the lines that name an Inferno symbol.
