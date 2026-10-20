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

**The run and jump** (3299, ticks 10690–10693):
- The player clicks the entrance (`oploc1`, `inferno_entrance` at 2493,5124) from 2496,5116. There's no confirmation dialogue.
- They walk to the take-off tile, 2496,5115.
- On the tick after arriving they play `brut_player_jump_whirlpool` (6723) with an exact move north to 2496,5126: start cycle 15, end cycle 255, facing north. The server moves them there on that tick.
- The fall's first mesbox comes 5 ticks later.
- The player's appearance is re-sent at J+4 and J+11; the transcript doesn't show its contents.

**Entering** (3 of 3), J = the first mesbox, 5 ticks after the jump:

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

**Failing** (deaths: 193, 210, 2111, Leagues V; the same timing on a normal ToB death, 3299 tick 5475). K = the killing hit, T = the teleport out:

| Tick | What happens |
|---|---|
| K+3 | Death sequence |
| T = K+8 | "You have been defeated!"; teleport to a tile beside TzHaar-Ket-Keh (2495–2497, 5109–5115); song 147, jingle 90 `you_are_dead`, then 502 (Mor Ul Rek), all with no ramp |
| T+2 (T+1 in 210) | Chat `chatneu2` "Well done in Inferno, you can take this TokKul as reward.", with the TokKul already in the pack |
| on continue | Objectbox, `tzhaar_token_25` at zoom 400: "TzHaar-Ket-Keh hands you N TokKul." |

- **Nothing earned** (left on wave 1, 7 of 7): chat `chatsad1` "Not a very good attempt JalYt. Better luck next time.", and nothing after it.
- **Leaving by the exit** gets the same two dialogues, on the teleport tick.
- **TokKul:** 12, 24, 40, 60 and 2664 for 1, 2, 3, 4 and 35 waves done (19 payouts in 13 sessions). That is 2(c+1)(c+2): our `~inferno_tokkul_for_wave` doubled by the elite Karamja Diary, which these accounts carried.

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
- **The run and jump:** ours asked "Jump into the Inferno." / "Stay here." and started the fall from the click. Now there's no dialogue: the walk to 2496,5115, the jump and the exact move, then the fall 5 ticks later. The fire-cape check runs before the jump. The walk re-checks from a queue each tick, because a script waiting in `p_delay` holds the player still. Verified in the tick log: click +2 walk, +3 jump, +8 mesbox, +19 landing, +40 wave 1, as recorded.
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

## Fixed 2026-10-19

- **The jump flickered into the pit (client).** Jagex sends the jump as a teleport onto the landing tile *and* the exact move in the same update (3299 tick 10693). The rev-239 client cancels that update's move when an exact move arrives (`class109.method3804`, flag 0x4000, clears `field1582`), so the player stays on the take-off tile and the exact move carries them. Ours applied the teleport first, which put the player in the pit, and the exact move then glided them back over 15 cycles. The osrs239 decoder now marks the staged move `route_only`; the executor sets route[0] and leaves the draw position alone (`Statics.method1301`). A per-frame trace of the local player now goes take-off → pit with no jump back. Pinned by `test-mock239-playerinfo`.
- **Every Inferno area sound was a gated synth.** `~inferno_sound_near` sent `sound_synth` when `distance(coord, $at)` was in range. `coord` needs an active player, so a nibbler chewing a pillar (no active player) was silent, and only one player could hear any of them. It now sends the real `sound_area`. The nibbler's bite now sends area 595 at range 7, as recorded. The pillar's 1021 is a real area sound at range 10 instead of a huntall of synths, and the dig-up's 1644 no longer depends on a player being in scan range.

- **Failing had no dialogue, and every death was two ticks fast.** A death gave "Oh dear, you are dead!" and TokKul with no word from TzHaar-Ket-Keh, and leaving by the exit gave a chat line. Both now play the recorded dialogue, and a death plays the recorded message and music. The player death body (`player/death.rs2`, every death, not only the Inferno's) played the death sequence on K+2 and teleported on K+6. It is now K+3 and K+8, as in all four recorded deaths. Verified in a scriptrun killed on waves 1 and 2: K 59/76, sequence 62/79, teleport and music 67/84, dialogue 69/86.

## Open

- **Barrage splash targets' defend has no delay** (engine, all multi-target attacks). Recorded (3299 tick 10736): every barrage target, primary and splash, gets the defend sequence delayed to the primary's landing (86), like the impact graphic. Ours delays the primary's and sends the splash targets' with delay 0. `npc_defend` takes its delay only from a projectile sent at that npc this tick, and a splash target has none. The fix needs `npc_defend` to accept a delay, which is a new opcode.
- **Player spells play no sounds (fixed since):** `player_magic.rs2` now sends the cast synth and the impact area sound per spell.
- **Region music on landing (fixed):** the engine's region music started track 500 on the landing tick (J+11). The recording is silent until J+21. `midi_hold(10)`, a new engine command, holds region music off for those ticks; the tick log now shows only the scripted start on J+21.
- **Dig, first step (not a bug):** one tick log showed the first step on D+8 against Blert's D+6 (97 of 135). The meleer takes its target on D+6. One that surfaces over the player steps off by a random cardinal, and a blocked direction costs a tick. Blert's other 38 digs show the same spread.

- Jal-Xil's melee swing is recorded (198, 1754): `jalxil_attack_melee` with synth 598 and no delay, as ours sends. Jal-Ak's melee is `jalak_attack_melee` with 595 (31 of 31). Jal-Zek's melee appears once (1754), too far away to hear.
- `inferno_babysplitter_mage_big` / `_biggest` appear in 1754 only; not used.

## Fixed 2026-10-19: the full inventory pass

This pass ran `tools/rsprox_tells.py --anchor 7691-7710` over all 43 archive sessions with Inferno monsters, not only the four Zuk ones. Their ids: 183, 188, 190, 192, 193, 198-207, 209, 210, 213-215, 1754, 2098-2102, 2106, 2107, 2109-2112, 2114-2119, 3273, 3299 and 3959-3961. Every tell kind was compared against `inferno_mob_tells.lua` and `inferno_zuk_tells.lua` tick logs.

- **Nibbler bites on a pillar** are uniform 0-4, and a 0 is a block splat: 472 bites in 198, 1754 and 3299 gave 0 100, 1 96, 2 85, 3 91, 4 100. Ours always did 2-4 damage.
- **A pillar's fall kills the nibblers beside it with a typeless splat (17, value 10), and their hit and death sounds are area sounds (range 10).** Ours already did the damage, but through the npc queue, so the hit counted as the bound player's: their splat, their synths. It is now `npc_damage(hitsplat_damage_other, ...)`, and the engine sends any hit no player dealt as an area sound (see below).
- **Wave-end music:** see [zuk.md](zuk.md) §1. Song 147 and jingle 76 play on "Wave completed!" and track 500 returns on W+8, all with no ramp. The jump-in's music goes out to 147 at fade-out speed 50, and track 500 returns on J+21 with envelope 0 (3299, 183, 2111).
- **Logging out mid-run:**
  - A request sets varbit 1548 `tzhaar_fightcave_logout` to 1 and sends one message: "Your logout request has been received. The minigame will be paused at the end of this wave.<br>If you try to log out before that, you will have to repeat this wave." (21 times).
  - On wave 69 the request says instead: "This is the final wave. If you try to log out before the end, you will have to repeat this wave." (198).
  - The pause sets varbit 1548 to 2. Varp 561 now transmits (`inferno.varp`).
  - A logout within 20 ticks of "Wave completed!" is refused with "You cannot logout at the start of a wave!". The line is sent once; later presses in the window are silent (206: 24 of them). The lock holds after leaving the arena (2101, 2102) and does not apply over a pause (2107, 2109, 2118). The combat delay is asked first (204).
  - Ours had a two-line request message, no varbit and no lock.
- **Teleport refusal:** "Use the portal if you want to leave." (183, 201, 2111). Ours had invented "You can't teleport out of the Inferno."
- **Varbit 11878 `player_in_inferno`:** 1 from the landing tick, 0 on the teleport out (exit, death or the Zuk kill). It was never set.
- **Varbit 4605 `multiway_indicator` (engine, every area):** the multi-combat icon. It was never sent. The engine now sets it on each zone change from the same answer combat uses: a map instance is multi, otherwise `maps/multiway.csv`. That zone set is LostCity's 2004 map, so modern multi areas such as Mor Ul Rek (multi in 3299) still read single.
- **The cutscene's four corner swaps** were each a `loc_del` and then a `loc_add`. Jagex sends the `loc_add` alone.
- **The Inferno sweet:** "The heat of the **I**nferno causes the wrapper to stick to the sweet. You cannot see **any way** to open it." (200).
- **Hits and deaths no player dealt (engine):** these are an area sound at the npc's tile, range 10: the minions dying with Zuk (`dragon_hit`/`dragon_death`, `water_creature_hit`/`_death`), nibblers felled by a pillar, and the glyph under Zuk's fire (id -1). Ours sent a synth to every player within 12, or nothing. `hit_has_player_dealer` in `torirs_server_combat.c` decides: no player is behind the script, or the content named the `_other` splat.
- **The player's block sound (engine and content, every fight):** [../combat/block_sounds.md](../combat/block_sounds.md).

Checked and already right: every mob's attack, hit, death and defend sound param; the attack cadences (bat 3, blob 6, the rest 4, healers 3 and 4); Yt-HurKot's heal, which is silent with no splat and no bar update, as ours; Zuk's heal amounts (15-24, n=167). The blob's melee comes 3 ticks after a throw (9 of 10) because the blob scans on a 6-tick clock: it swings melee on the scan, or throws 3 ticks after it, as ours does.

Not Inferno content, but recorded in these sessions: **TzHaar-Ket-Rak's Challenges** (1754, ticks 1294-2071: `jad_challenge_jad`/`_healer`/`_master`, "You enter the Inferno for TzHaar-Ket-Rak's Special Challenge.") are not implemented. The older sessions' cutscene shakes are 3/2/3 where 3299 (rev 234) has 5/4/3; ours follows 3299.
