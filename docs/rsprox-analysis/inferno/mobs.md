# The Inferno's waves: recorded mob behaviour

Sources: submission 3299 (waves 1–68, with names) and the Leagues runs 1754 and 198 (rev 227, v1 projectiles). See [README.md](README.md).

Tools:
- `tools/rsprox/rsprox_attack_tells.py` attributes everything sent on a single mob's attack tick, and on the following ticks, to that attack (`--attack` names the sequences, `--size` the footprints).
- `tools/rsprox/rsprox_tells.py --timeline` gives the ordered stream.

Every row was read from these recordings unless it says otherwise. Content: `scripts/inferno_ai.rs2` (wave mobs), `inferno_adds.rs2` (the Zuk set and healers), `inferno_zek.rs2` (resurrection), `inferno_pillars.rs2`, `inferno.rs2` (entry) and `configs/inferno.npc`.

## Attacks

Projectile heights are in our wire units: v2 ÷ 4, or v1 as sent. "Flight" is end − start in client cycles. "d" is the Chebyshev distance from the projectile's source tile to the target, except for Jal-Xil, which is measured from its footprint's edge. Every flight fit is exact over the recorded range.

| Mob | Sequence | Projectile | Source | Start | Flight | Peak / arc | Height | Sounds (synth to the target unless "area") |
|---|---|---|---|---|---|---|---|---|
| Jal-Nib (nibbler) | `jalnib_attack` | — | — | — | — | — | — | area 595 `lavabeast_attack`, range 7 (7 of 7) |
| Jal-MejRah (bat) | `jalmejrah_attack` | `inferno_harpie_proj` | SW tile | 10 | 22 + 8d | 16 / 128 | 50 → 31 | 291 `firebat_attack`, delay 15 (142 of 142) |
| Jal-Ak (blob), ranged | `jalak_attack_ranged` | `inferno_splitter_range` | SW + (1,1) | 10 | 22 + 8d | 16 / 32 | 40 → 31 | none on the throw; 3403 `fi_rock_hit` delayed to the landing (156 of 156) |
| Jal-Ak, magic | `jalak_attack_magic` | `inferno_splitter_mage` | SW + (1,1) | 28 | 8d | 16 / 32 | 40 → 31 | 595 `lavabeast_attack`; 3528 `surok_mindcontrol_impact` delayed to the landing (44 of 44) |
| Jal-Ak, melee | `jalak_attack_melee` | — | — | — | — | — | — | 595 |
| Jal-AkRek-Xil (ranged split) | `jalak_attack_ranged` | `inferno_babysplitter_range` | its tile | 10 | 22 + 8d | 16 / 32 | 20 → 31 | none on the throw; 3403 delayed to the landing (78 of 78) |
| Jal-AkRek-Mej (magic split) | `jalak_attack_magic` | `inferno_babysplitter_mage` | its tile | 28 | 10d to 2 tiles, then 8d | 16 / 32 | 20 → 31 | 595; 3528 delayed to the landing (41 of 41) |
| Jal-AkRek-Ket (melee split) | `jalak_attack_melee` | — | — | — | — | — | — | 595 (33 of 33) |
| Jal-ImKot (meleer) | `jalimkot_attack` | — | — | — | — | — | — | 608 `lizard_cleric_attack` (45 of 45) |
| Jal-Xil (ranger) | `jalxil_attack_ranged` | `inferno_xil_projectile` **×2** | two different tiles of the four two out from its centre: SW + (1,1) ± (2,0) / (0,2) | 60 | 8 × edge gap − 6 | 16 / 0 | 64 → 31 | 598 `magmaquiris_attack`, delay 50 (403 of 403) |
| Jal-Zek (mager) | `jalakxil_attack_magic` | `inferno_zek_projectile` | SW + (2,2) | 28 | 8d | 0 / 32 | 100 → 31 | 598, no delay. Also `fireblast_impact` (height 124) on the player, delayed to the landing, prayed or not (393 of 393) |
| JalTok-Jad | see [zuk.md](zuk.md) §6 | | | | | | | |
| Yt-HurKot (Jad healer) | `lizard_cleric_heal` | — | — | — | — | — | — | none. `tzhaar_heal` on Jad at height **256** on the heal tick (25 of 25) |

**Hit timing:** the player's splat lands ⌊end / 30⌋ ticks after the attack. Isolated attacks: bat 72 → +2, blob 120 → +4, split 80 → +2, mager 92 → +3. Our `~inferno_land_hit` already does this, so correcting the flights corrects the hits.

## Hit, death and defend

| Mob | Defend (delayed to the shot that hits it) | Hit sound | Death | Death sound |
|---|---|---|---|---|
| Nibbler | `jalnib_defend` | 597 `lavabeast_hit` | `jalnib_death` | 596 `lavabeast_death` |
| Bat | `jalmejrah_defend` | 296 `firebat_hit` | `jalmejrah_death` | 295 `firebat_death` |
| Blob, splits | `jalak_defend` | 597 | `jalak_death` | 596 |
| Meleer | `jalimkot_defend` | 610 `lizard_cleric_hit` | `jalimkot_death` | 609 `lizard_cleric_death` |
| Ranger | `jalxil_defend` | 600 `magmaquiris_hit` | `jalxil_death` | 599 `magmaquiris_death` |
| Mager | **`jalakxil_ready`**: its "defend" is its ready pose | 600 | `jalakxil_death` | 599 |

## Special moves

**Jal-ImKot's dig** (7 of 7):
- D: `jalimkot_digdown`, with area 1641 `mole_burrow_down` (range 10) on its tile.
- D+3: it telejumps to the new tile and plays `jalimkot_digup`, with area 1644 `mole_burrow_up` (range 10) there, all on the same tick.
- It stands still until D+6 and swings on D+12 if it can reach (4 of 5).

**Jal-Zek's resurrection** (3 in 3299, plus 17 more in the Leagues runs):
- The mager plays `jalakxil_resurrect` and the chat line "Jal-Zek uses his power to trigger the rebirth of a fallen foe." is sent.
- On the same tick:
  - the revived mob is added;
  - `tzhaar_heal` (height 150) plays as a map graphic on its middle tile;
  - area 1095 `zanik_resurrection` (range 10) plays on the mager's own tile.
- Revive tiles (local 33–37, 29–32) and the next-attack gap are consistent with our Blert-calibrated rules.

**A Rocky support falls:**
- The support NPC (7709) is swapped for `inferno_safespot_dying` (7710), which plays `safe_spot_distructible_pillar_collapse`; the loc is deleted.
- Area 1021 `100_cauldron_explodes` (range 10) plays on the support's middle tile.
- The dying NPC is removed 2 ticks later.
- There is **no chat line**.

**Entering** (3 of 3), J = the Jump-in:

| Tick | What happens |
|---|---|
| J | Mesbox "You jump into the fiery cauldron of the Inferno. Your heart is pulsating." (no continue); music out |
| J+1 | 948 `(0,255,0,0,50)`: fade to black |
| J+6 | Mesbox "You fall and fall and feel the temperature rising." |
| J+9 | Mesbox "Your heart is in your throat..." |
| J+11 | Mesbox "You hit the ground in the centre of The Inferno."; teleport into the arena, `quest_lunar_landing_on_face` |
| J+12 | 948 `(0,0,0,255,50)`: fade back in |
| J+21 | Track 500 |
| J+32 | "Wave: 1" |

## What was wrong in ours, and the fix (2026-10-18)

- **Every projectile:** start, flight formula, heights, peak and arc were Kronos-style guesses (bat 41 + 5 + 5d, blob 41 + 5 + 5d at height 50, mager 28 + 36 + 8d at height 96 with arc 192, ranger 50 + 40 + 8d from fixed tiles). All of them now follow the table.
- **The ranger** fired from two fixed tiles. It now picks two of the four recorded tiles.
- **Sounds:**
  - the bat's sound had no delay (15);
  - the ranger's had no delay (50);
  - the blob's and the splits' ranged throws played a cast sound they don't have, and had no landing sound;
  - magic throws played 3528 on the cast instead of 595 there and 3528 on the landing;
  - the nibbler was silent;
  - the mager put no `fireblast_impact` on the player;
  - Yt-HurKot played an invented `heal` sound, and its `tzhaar_heal` sat at height 0.
- **Records:** the mager's defend animation was null (now `jalakxil_ready`); the magic split's `attack_sound` was 3528 (now 595); the nibbler had no sound params (now 595 / 597 / 596).
- **Dig:** `jalimkot_digup` played on D+6 instead of with the D+3 telejump, and both dig sounds were missing.
- **Resurrection:** the graphic, the sound and the chat line were missing.
- **Pillars:** an invented "A rocky support collapses!" line and a `cave_collapse_1` synth replaced with the recorded area sound.
- **Entry:** we teleported straight in. Now the recorded fall plays, and wave 1 starts 21 ticks after landing (`^inferno_entry_delay` was 8).
- The Zuk set (`inferno_ranger_finalwave`, `inferno_mager_finalwave`) shares the wave mobs' shot procs. No recording shows a set, so their shots at the glyph are the same shots aimed at the glyph's tile.

## Verified (2026-10-18)

`test/waves/inferno_mob_tells.lua` was read with `tools/rsprox/ticklog_tells.py`. It runs the real jump-in (`::infernojump`), waves 1, 9, 35 and 62, a forced resurrection (`::infernorevive`) and a natural dig. Every row above matched:
- **Jump-in:** land J+11, track J+21, wave 1 on J+32.
- **Shots:** each projectile's start, flight and height, with the bat's 291 delayed 15 and the ranger's 598 delayed 50.
- **Landing sounds:** 3403 / 3528 delayed to the landing.
- **Mager:** `fireblast_impact` on the player.
- **Blocks:** the player's block delayed to each landing.
- **Resurrection:** the sequence, the graphic on the revived mob's middle tile, the sound and the spawn all on one tick.
- **Dig:** `digdown` and 1641 on D; telejump, `digup` and 1644 on D+3; first swing on D+12.

## Open

- **Region music on landing (fixed):** the engine's region music started track 500 on the landing tick (J+11). The recording is silent until J+21. `midi_hold(10)`, a new engine command, holds region music off for those ticks; the tick log now shows only the scripted start on J+21.
- **Dig, first step (not a bug):** one tick log showed the first step on D+8 against Blert's D+6 (97 of 135). The meleer takes its target on D+6. One that surfaces over the player steps off by a random cardinal, and a blocked direction costs a tick. Blert's other 38 digs show the same spread.

- Jal-Ak's melee and Jal-Xil/Jal-Zek's adjacent melee swings are never recorded near enough to read a sound, so they keep the record's `attack_sound` with no delay.
- `inferno_babysplitter_mage_big` / `_biggest` appear in 1754 only; not used.
