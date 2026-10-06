# Ledger: the Inferno's cache dumps (`sources/cache_*.txt`)

Pinned 2026-10-03 by the inventory agent of the waves loop. The dumps are produced by
`tools/waves_gate/cache_dump.py`. Re-running the command below over the same content
reproduces every file byte for byte.

```
python3 tools/waves_gate/cache_dump.py --game inferno \
    --content OSRS-Content/osrs239-content \
    --out docs/minigames/inferno/sources \
    --index build/inventory_state/inferno.cache_index.json
```

## What was read

- **Cache:** revision 239 (`OSRS-Content/osrs239-content/meta.ini`: `revision = 239`,
  `rev_name = osrs239`, source `cache.osrs239`). The dumps quote the unpacked text form in
  OSRS-Content at commit `c70422bdf6` (2026-10-03, branch `matthew-mbp-m4-waves-b1`); the
  binary cache is not read.
- **Config files:**
  - each `configs/all.<kind>` with its `configs/all.<kind>.compack` (`id=symbol`), for npc,
    seq, spotanim, loc, obj, varp, varbit, enum, dbrow and struct;
  - name tables `pack/4_soundeffects.pack`, `pack/3_interfaces.pack`,
    `pack/6_musictracks.pack`, `pack/11_musicjingles.pack` and `pack/12_clientscripts.pack`;
  - `interfaces/<name>.if` and the decompiled CS2 suite `scripts/<name>.cs2`;
  - `songs/*.jmid` (checked for presence only) and the tree's `docs/audio/*.tsv`.
- **Map files** (`maps/m<x>_<z>.jl2`, the `==== LOC ====` section; each line is
  `level x z: loc shape [angle]`):
  - `maps/m35_83.jl2`, the Inferno's template square (`^inferno_template = 0_35_83_0_0`,
    region 9043), all of it on every level;
  - `maps/m38_80.jl2`, the box x 55..63, z 0..10;
  - `maps/m38_79.jl2`, the box x 50..62, z 45..57.

  The two boxes surround the two placements of `inferno_entrance` 30352: m38_80 at 61,4
  and m38_79 at 56,51. Our `^inferno_entrance_walk` is 0_38_79_56_51, and TzHaar-Ket-Keh
  is spawned at 2494,5113 by `server/scripts/areas/world/configs/m38_79.spawn:66`.

## Selection rules (the cache decides; each record's `why=` line names the rule that chose it)

1. **By name.** The symbol is split on `_`, and a record is selected when one word part
   is any of the following:
   - `inferno`, `infernopet`, `zuk`, `tzkal`, `tzkalzuk`, `zukrek` or `infernalcape`;
   - a `jal` word: `jal`, `jalnib`, `jalmejrah`, `jalak`, `jalakrek`, `jaltokjad`,
     `jaltok`, `jalimkot`, `jalxil`, `jalakxil`, `jalrek`, `jalmejjak` or `jalzek`.

   A record also matches when its symbol contains `infernal_cape`, `moving_safe_spot` or
   `safe_spot_distructible`.

   An npc is also selected when its `name=` (colour tags stripped) is one of the Inferno's
   monster names: Jal-Nib, Jal-Nib-Rek, Jal-MejRah, Jal-Ak, Jal-AkRek-Mej/-Xil/-Ket,
   Jal-ImKot, Jal-Xil, Jal-Zek, JalTok-Jad, Yt-HurKot, TzKal-Zuk, Jal-MejJak, Ancestral
   Glyph, Rocky support, TzHaar-Ket-Keh, TzRek-Zuk or JalRek-Jad.

   **Excluded by symbol:**
   - `colosseum_*` (another minigame);
   - `placeholder_*` and `cert_*` (bank placeholders and notes);
   - `br_*`, `trailblazer*`, `league_*` and `deadman_*` (other game modes);
   - `jad_challenge_*` (TzHaar-Ket-Rak's Challenges, a separate activity that reuses
     Inferno assets);
   - `tzhaar_fightcave_*` (the Fight Caves' own Yt-HurKot);
   - `slayer_infernal*`, `superior_infernal*`, `*infernal_mage*`, `*infernalmage*` and
     `slayerguide_*` (the Slayer monster "Infernal Mage" is not the Inferno).

   `infernal` alone is not a match, because the infernal tools, eels and shale are not the
   Inferno. Placeholders are named here so their absence is a decision.
2. **By binding.** A record that an already selected record names is selected. The rule
   runs to a fixed point over these fields:
   - an npc's animation fields (`readyanim`, `walkanim`, `walkanim_b/l/r`) and its
     `multinpc*` and `multivar*`;
   - a spotanim's or a loc's `anim=`;
   - a loc's `multiloc*`, `multivarbit` and `soundid`;
   - a sequence's frame `sound=`.

   **Recorded, not selected:** a varbit's `basevar`. A shared carrier varp (the pet
   menagerie, the collection log, the combat-achievement bitfields) is not the Inferno's
   asset, and the four carriers that are the Inferno's are already selected by name.
3. **By family.** These are the asset families `docs/INFERNO_SOUNDS.md` joins on one Jagex
   asset name on both sides (sections 5 and 6):
   - the sequences `lizard_cleric_*` and `dagannoth_water_creature_*`;
   - the sounds `lizard_cleric_*`, `firebat_*`, `lavabeast_*` and `magmaquiris_*`;
   - only the four `dagganoth_*` sounds that section 5 joins (`attack`, `hit`, `death`,
     `spines`). The other 23 `dagganoth_*` sounds are Waterbirth scenery.

   No Fight Caves sequence family is included: the Inferno reuses the Fight Caves'
   sounds, not their animations.
4. **By a document in the tree** (`doc:`):
   - the sound ids `docs/INFERNO_SOUNDS.md` sections 2, 3, 5, 7 and 8 place in the Inferno
     (155, 156, 163, 166, 408, 409, 410, 2294, 3528, and the cave block 2039-2046);
   - spotanim 660, which `docs/BOSS_ASSETS.md:250` names as Jal-MejJak's projectile
     (a Kronos id) although no script of ours names it.
5. **By map.** These are every loc the map files above place in the selected area.
6. **By reference.** A record is selected when a non-comment line of
   `server/scripts/minigames/minigame_inferno/` (scripts and configs) or of
   `npc_combat/i/inferno_*.combat` names its symbol.
   - Tokens behind `$`, `^`, `~` and `@` (locals, constants, procs, labels) are skipped.
   - When a token names records of more than one kind, the call it sits in decides
     (`npc_anim(` means a seq, `[oploc1,` a loc, `sound_synth(` a sound, and so on).
   - `param=attack_sound,N` (and `defend_`, `death_`) selects sound N by number.
   - The selftest scratch var `varp7_mock_quest_progress` is excluded.
   - Clientscripts 948 and 951 are selected by number from
     `^clientscript_fade_in/out` (`inferno.constant:200-201`).
   - Music: track 500 (`inferno`) is selected by name. Archive 502 (Mor Ul Rek, the
     entrance area's track) is selected by id: its pack name is the unrecovered
     `song_502`, and `docs/audio/music_tracks_osrs239.tsv:414` and
     `music_regions.tsv:528` name it.
7. **Structs** have no symbol, so they are selected by content:
   - every combat-achievement struct (`param_1306` present) whose boss group is
     `param_1312=60`, which is the group of "Kill a Jal-Zek within the Inferno";
   - any struct with a string param equal to "The Inferno", "TzKal-Zuk",
     "Defeat TzKal-Zuk" or "Kill TzKal-Zuk".

   An **enum** is selected only by name; none matched.

## Counts (records per file)

| file | kind | records |
|---|---|---|
| cache_npc.txt | npc | 30 |
| cache_seq.txt | seq | 96 (each with frame count, run-length frame lengths, cycles, ms, game ticks, frame sounds) |
| cache_spotanim.txt | spotanim | 25 |
| cache_locs.txt | loc | 106 (with up to 12 map placements each, cited to the `.jl2` line) |
| cache_vars.txt | varp / varbit | 5 / 26 |
| cache_objs.txt | obj | 22 |
| cache_sounds.txt | sound | 37 |
| cache_interfaces.txt | interface / clientscript | 3 / 7 |
| cache_enums_dbrows.txt | dbrow / struct / enum | 8 / 19 / 0 |
| cache_music.txt | music / jingle | 2 / 0 |

## Negative results, kept

- **No selected sequence carries a frame sound.** Across 96 sequences there is not one
  `sound=` line. This agrees with `docs/INFERNO_SOUNDS.md` section 4, so no sound can be
  played twice.
- **The map has no rocky-support locs.** `maps/m35_83.jl2` places no `inferno_safespot1..3`
  (30353-30355), and neither does any script or engine file.
- **No interface for a wave counter, timer, entry warning or practice mode.**
  `pack/3_interfaces.pack` has `inferno_hp_hud` (596) only.
- **No enum matches the name rule. No jingle matches the name rule.**
- **No dumped file is over 2 MB.** The largest is `cache_locs.txt` (2,525 lines).
