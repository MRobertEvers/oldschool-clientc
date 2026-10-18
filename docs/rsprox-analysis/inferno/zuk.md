# TzKal-Zuk: recorded behaviour

The sources are submissions 3299, 1754 and 199 (see [README.md](README.md)). Ticks are each recording's own. Ours were measured with `test/waves/inferno_zuk_tells.lua` and `tools/rsprox/ticklog_tells.py`, and every row marked **matches** was checked in that run (2026-10-18). Content is in `OSRS-Content/osrs239-content/server/scripts/minigames/minigame_inferno/`.

## 1. Wave 68 → the cutscene

W is the tick wave 68's last Jad leaves. M is the mesbox tick.

| Tick | Recorded (3299, W = 13632) | Ours before | Now |
|---|---|---|---|
| W | `Wave completed!` (plain, no colour) and `midi_jingle` 76 `the_fight_continues_fight_cave`. This holds for **every** wave, 68 of 68. | `<col=ef1020>Wave completed!`, no jingle | matches |
| W+10 = M | Mesbox "A great power is starting to shake the cavern..." with no continue. `fade_overlay` and clientscript 948 `(0,255,0,0,15)`: black, clear → opaque over 15 cycles. The music fades out. | "Wave: 69" printed here; a fade over 50 cycles with script 951; music stopped | matches (the mesbox, 948 out over 15, `midi_song(-1)`) |
| M+2 | Cutscene lock: `cutscene_status`, `fov_clamp`, `minimap_state` 2. Side tabs 0–6, 11 and 12 and xp_drops closed. The player is moved to (2271,5356). `cam_shake` on axis 0/1/2 with random 5/4/3, amplitude 0, rate 0. Synth 1678 `rumbling`. | Three shakes (6,10,2); `cave_rumbling_1`; "TzKal-Zuk stirs." and two "Oh no!..." game messages; no varbits or tabs | matches; the invented messages are removed |
| M+3 | `cam_lookat` (2268,5368) height 140; `cam_moveto` (2275,5347) height 1000; rate 0, rate2 100 | moveto (2276,5349), lookat (2271,5365) at height 1000, rate 10 | matches |
| M+4 | `inferno_prisonroof_hidden` = 1 (no loc packet for the roof). The six seal locs go to state2 together: both flanks, both floor corners, both corner joins. The centre slab (2270,5363) is deleted. Zuk added at (2268,5364) with `zuk_spawn`; the glyph added at (2270,5363). Tzhaar-Ket-Keh's chathead (`inferno_master`, `chatscared2`, no continue): "No! TzKal-Zuk's prison is breaking down. There's<br>nothing I can do for you now, JalYt!". 948 `(0,0,0,255,50)`: fade in over 50. | State2 flanks two ticks earlier; corners a tick later; slab removed a tick before Zuk; fade-in over 120; a roof `loc_del`; no chathead | matches |
| M+5 | `loc_anim` on both flanks (7559/7560); synth 1487 `explosion` | `cave_collapse_1` and "The rocks holding TzKal-Zuk break loose!" | matches |
| M+6 | Synth 3686 `grim_giant_roar` | none | matches |
| M+8 | `midi_song` 500 (Inferno) | at M+12 | matches |
| M+12 | Lock released. `<col=ef1020>Wave: 69</col>`. `inferno_zuk_hp`/`_base_hp` set to 1200. Both flanks changed in place to state3. `cam_reset`, tabs reopened, chathead and fade closed, `inferno_hp_hud` opened. | "The seal crumbles." and `cavein` from a Zuk queue; hp bar opened at M+4 | matches |
| W→M | 10 ticks | 11 | matches (`^inferno_zuk_cutscene_delay` 3) |

Every "Wave: N" line is `<col=ef1020>Wave: N</col>`. Ours had no `</col>`; fixed for all waves.

**Not reproducible here:** Jagex also adds each dialogue to chat history as typed messages ("mesbox" and "dialogue" types). Our engine has no message types besides the game type, so these are not sent.

## 2. The Ancestral Glyph

A = the tick it is added (M+4).

| | Recorded (3299, 199, 1754) | Ours before | Now |
|---|---|---|---|
| Drop onto its row (z 5360) | A+2, A+3, A+4, one tile a tick | A+4..A+6 | matches (`^inferno_glyph_drop_delay` 1) |
| First strafe step | A+9, the tick after the camera returns | A+13 | matches (`^inferno_glyph_ready_delay` 8) |
| First direction | west in 3299, east in 1754 and 199 | random | unchanged |
| Dwell at an end | seen 5 ticks (2257: arrives A+21, leaves A+26) | 5 | unchanged |
| Hit by Zuk | `moving_safe_spot_hit`, delayed to the shot's landing (106/126) | delay = landing − 25 | matches |

## 3. Zuk's attack

| | Recorded (99 shots) | Ours before | Now |
|---|---|---|---|
| First shot | M+25, the tick the glyph first reaches an end | M+29 (glyph late) | matches |
| Cadence | 10 ticks; 7 from the crossing of 240 | same | unchanged |
| Animation | `zuk_attack` | same | unchanged |
| Projectile | `inferno_zuk_projectile` (1375) from Zuk's centre (2271,5367). Start 50, height 97 (v2 390) → 31, peak 0. Arc 64 into the glyph, **128** at the player. | Start 56, height 80, peak 16, arc 64 at both | matches |
| Flight | **16 + 10 × the player's Chebyshev distance from Zuk's centre**, whether the shot goes at the player or into the glyph: 9 → 106, 10 → 116, 11 → 126, 12 → 136. 92 of 99 shots fit exactly; the rest are a tick of movement. | 56 + 36 + 8 × distance to the target | matches |
| Hit | The splat lands ⌊end/30⌋ ticks after the shot (106 → +3, 126 → +4) | same | matches |
| Into the glyph | Area 155 `fireblast_cast_and_fire` at Zuk's centre (range 15); area 156 `fireblast_hit`, delayed to the landing, on the glyph (range 10) | `sound_synth` 155 and 156 | matches (range-checked, for one player) |
| At the player | Synth 155. On a hit, `fireblast_impact` on the player (height 124, delayed to the landing) and area 156. On a miss, `failedspell_impact` and area 227 `spellfail` (199 tick 135). | Every shot hit with `fireblast_impact` at height 0... | matches: Mod Ash's hybrid roll (average of effective magic and ranged accuracy, 550/550, against the average of the player's magic and ranged defence; wiki) and a 0–148 damage roll |
| Zuk hit by the player | `zuk_defend` on the attack tick, delayed to the arrow's landing (86/76) | on the landing tick, delay 0 | **open**: engine-wide, see [../combat/defend_timing.md](../combat/defend_timing.md) |

## 4. Thresholds and the enrage

| | Recorded | Ours |
|---|---|---|
| Jad | Added on the hit that takes Zuk to ≤ 480 (452, 395, 453 in the three) | matches |
| Healers | Added on the hit that takes Zuk to ≤ 240 (218, 193, 199) | matches |
| Enrage message | "TzKal-Zuk has become enraged and is fighting for his life." on the same tick | was missing; **added** |
| Health readout | none | ours printed "TzKal-Zuk: N/1200" (and the glyph's) on every hit; **removed** |

## 5. Jal-MejJak (Zuk's healers)

| | Recorded | Ours before | Now |
|---|---|---|---|
| Tiles | SW of Zuk +(−6,−1), (−2,−1), (+8,−1), (+12,−1) | same | unchanged |
| Rise | `dagannoth_water_creature_spring_up` on the spawn tick | same | unchanged |
| First throw | spawn + 4, then every 3 ticks, all four together | spawn + 7 | matches |
| Heal throw | `dagannoth_water_creature_attack`. Projectile 660 `wild_falloff_meteor_flying` to Zuk: cycles 0 → 90, height 17 → 32, peak 16, arc 64. Area 155 delay 51, range 8, on the healer. | 30 → 100 high, start 50, arc 0, no sound | matches |
| Heal lands | throw + 4: a heal splat (6) per healer and one `tzhaar_heal` spotanim on Zuk (height 256) | throw + 1, no spotanim | matches |
| Heal amount | 15–24 | same | unchanged |
| After being attacked | Three balls a volley (660, cycles 0 → 90, height 17 → 0, arc 0, peak random 40–50) to tiles on rows 5355–5358 within ±5 x of the healer. Area 155 delay 51, range 8. On throw + 3 each tile gets 659 `wild_falloff_meteor_blast` and area 156 (range 8), both delayed a random 0–5 cycles, and the damage. | Ball 0 → 75, height 0, peak 45; splash sent at the throw, delayed 75; synth sounds | parameters match (from the code; not exercised by the test run) |
| Defend | `dagannoth_water_creature_defend`, delayed to the hit | — | — |
| Killed with Zuk | Hit sound 900 `water_creature_hit`; death `dagannoth_water_creature_death` with 899 | `dagganoth_hit`, `..._go_down`, `dagganoth_death` | matches |

## 6. JalTok-Jad, Zuk phase, and every Jad's shots

| | Recorded | Ours before | Now |
|---|---|---|---|
| Spawn | (2270,5347) | same | unchanged |
| First attack | spawn +6, +6, +8 (Blert's 13 runs: 6–8, median 7) | 7 | unchanged |
| Cadence | 8 ticks | same | unchanged |
| Magic, at a person | Swing: synth 159 `firebreath`. Swing + 3: three projectiles from Jad's centre at height 128 → 31, peak 16, arc 32. They are `tzhaar_fire_spit_travel` 2 → 2+8d, `_follow_travel` 6 → 6+8d and `_end_travel` 10 → 12+8d (190 fans, 4 recordings). Synth 162, then `firewave_impact` on the player and synth 163, both delayed to the landing. No launch spotanim. | 162 on the swing (delay 20). A `tzhaar_fire_spit_launch` flare. Height 100. Flights 10/14/20 + 8d. | matches (wave Jads too) |
| Ranged, at a person | Silent swing. Swing + 3: `tzhaar_rock_smash` on the player's tile at **height 92**, `firewave_impact` delayed 60, synth 163. | rock smash at height 0 | matches |
| At the glyph | Everything goes to the glyph's TILE. Magic: area 159 on Jad at the swing; at swing + 3, area 162 and the fan as **map** projectiles, flight 2 + 8 × (distance to the glyph's near edge); `firewave_impact` and area 163 on the tile at the landing. Ranged: map 451 at height 92, map 157 delayed 60, area 163. | Everything sent on the swing as npc-targeted graphics, with no sounds | matches |
| Yt-HurKot | Three, on the row north of Jad at x +0, +1, +2 of his SW tile (1754, 199) | a random 3×3 at +(1..3, 6..8) | matches |
| Killed with Zuk | Hit sound 410 `dragon_hit` on D, `jaltokjad_death` with 409 on D+1, removed with Zuk | death animation played twice; removed mid-clip | matches |

## 7. The kill and the exit

D = the `zuk_death` tick.

| Tick | Recorded (3299, D = 13729) | Ours before | Now |
|---|---|---|---|
| D | `zuk_death`. Every minion takes the rest of its hitpoints as hitsplat 17 (`hitsplat_damage_other`). "Your TzKal-Zuk kill count is: `<col=ff0000>`N`</col>`." `inferno_hp_hud` closed. | minions deleted on D+1, no death | matches |
| D | "Duration: `<col=ff0000>`MM:SS`</col>` (new personal best)" | — | **open**: we keep no run timer |
| D+1 | Minion deaths (above) | — | matches |
| D+3 | Healers removed | — | matches |
| D+5 | Zuk, the glyph and Jad removed. Teleport out, jingle 76, 948 `(0,0,0,255,50)`. Then TzHaar-Ket-Keh (`chathap2`): "You are very impressive for a JalYt. You managed to<br>defeat TzKal-Zuk, for now...". Then (`chathap1`): "Please accept this cape as a token of appreciation." Then `objectbox_double` (tokkul 25, cape; zoom 400): "TzHaar-Ket-Keh hands you an Infernal cape and<br>16440 TokKul." | "You have defeated TzKal-Zuk!" and "You have defeated TzKal-Zuk! Please accept this Infernal cape." as game messages | matches |
| Killing hit vs `zuk_death` | Same tick | ours plays `zuk_death` a tick after the killing splat | **open** (engine death timing, every npc) |

## Still open

- **The JalZek/JalXil set** spawn timer: no recording runs long enough.
- **"Duration" / personal-best line**: needs a run timer.
- **Dialogue copies in chat history**: needs message types in the engine.
- **Defend animations sent on the attack tick, delayed to the landing**: Zuk, Jad and the healers all do it, as does every NPC. This is engine work: [../combat/defend_timing.md](../combat/defend_timing.md) has the data and a proposed fix.
- **`zuk_death` on the killing-hit tick**: engine.
