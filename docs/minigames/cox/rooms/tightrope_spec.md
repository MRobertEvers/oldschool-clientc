# Tightrope — tick spec, rulings and content audit (2026-10-18)

Unit `icedemon_tightrope` (COX_FINISH_PLAN.md R). Content: the tightrope procs of
`OSRS-Content/osrs239-content/server/scripts/minigames/minigame_cox/scripts/cox_puzzles.rs2`
(the thieving procs there are another unit's), the tightrope blocks of
`configs/cox.constant` / `cox.npc`. Solver plan: `tightrope_solver.md`.
Ruling order: Jagex > recorded data > wiki > plugin constants > Near-Reality > ours.

## 0. Sources

| tag | source | file |
|---|---|---|
| **JA** | Mod Ash, 29 Dec 2020: the Agility requirement is "between 80% and 100% of the average agility level of the team, scaled randomly" | quoted in `sources/wiki_Chambers_of_Xeric_Strategies.wikitext:845` |
| **JN** | newsposts: DT2 beta (Rapid Heal/Redemption interplay with the "rope skip"); CoX Changes 2023 ("Nothing's changing about Tightrope") | `sources/newsposts/` |
| **C** | cache: npcs 7559 `raids_tightrope_ranger`, 7560 `raids_tightrope_mage`; locs 29749 `raids_tightrope_barrier` "Shimmering barrier" (1×2, op Dispel, anim castlewars_portalglow), 29750 `raids_tightrope_end` "Tightrope" (op Cross, blockwalk), 29751 `raids_tightrope_keystone_loc` (op Take); obj 20884 `raids_tightrope_keystone`; seqs 426 human_bow, 711 human_caststrike, 762/763 logbalance walk/ready; spotanims 15 rune_arrow_travel, 129/130/131 fireblast cast/travel/impact | `configs/all.*` |
| **M** | template m51_83 cells 0/1, 1/1 and m52_83 cell 0/1, plane 1: rope ends, keystone, `raids_doorway` | `tools/cox_template_survey.py --find` |
| **W** | wiki: Tightrope (CoX), Keystone crystal, Shimmering barrier (fetched 2026-10-18), Deathly ranger, Deathly mage, CoX/Strategies "Tightrope", CoX "Points", No Time for Death | `sources/wiki_*.wikitext` |
| **V** | Synq 0:26:21 (`synq_transcript.md:239-256`); The Academy `yt_erDL_d2QcTc`; Plank2g `yt_2M2bzzc3X90`; Nang *Tightrope Skip* `yt_aSXVrgu5RF8` | transcripts |
| **NR** | Near-Reality `DeathlyRoom`, `DeathlyNPC`, `Tightrope`, `KeystoneCrystal`, `ShimmeringBarrier`, `ScalingMechanics` | NR tree |

No plugin carries a tightrope constant beyond room ids (cox_assistant's points
model, de0's barrier 29749 as the room's obstacle). Recorded tick data: RSProx
820, 843, 3282, 3285, 3389, seven rooms (`docs/rsprox-analysis/cox/tightrope.md`,
rows RT-*), which outranks every row below where they differ.

## 1. Geometry (template room-local, unturned, plane 1 of the template)

| | CCW (m51_83 0/1) | THRU (m51_83 1/1) | CW (m52_83 0/1) | source |
|---|---|---|---|---|
| rope ends (blockwalk, op Cross) | (18,23) — (26,23) | (18,23) — (26,23) | (8,18) — (8,26) | M |
| crossing lands on | (27,23) / (17,23) | same | (8,27) / (8,17) | NR (object ±9) |
| keystone loc | (28,26) | (28,26) | (5,28) | M |
| barrier (1×2) SW, angle | **(6,16) a0** → (6,16)(6,17) | **(13,24) a1** → (13,24)(14,24) | **(27,15) a2** → (27,15)(27,16) | W Shimmering barrier locations = NR barrierLocations ✅ |
| exit `raids_doorway` | (3,16) | (13,25) | (28,15) | M — barrier one tile inside it ✅ |
| mages (2 solo) | (21,19) (23,19) | (21,19) (23,19) | (12,21) (12,23) | NR mageSpawnLocations |
| rangers (2 solo) | (20,26) (22,26) | (20,26) (22,26) | (5,20) (5,22) | NR rangerSpawnLocations |

## 2. Tick spec

| # | mechanic | decide / read | geometry | numbers (solo, max stats) | tell | source |
|---|---|---|---|---|---|---|
| T1 | **Guards' count** | at spawn | above | rangers 2 (3 at 4+, 4 at 7+), mages 2 (3 at 4+, 4 at 10+) | npcs present | W ("In solo and small groups ... two ... alongside two"), NR |
| T2 | **Passive** | — | — | not aggressive; retaliate when attacked; range 10 | — | W infoboxes "aggressive = No" |
| T3 | **Wake on Cross** | the tick the crosser steps on (after arrival at the rope end) | every guard in the room not already in combat | each idle guard targets the crosser; 1-4 of them shout "Intruder!" / "Stop them at all costs!" / "Protect the keystone!"; EVERY crossing (a busy guard keeps its target: "as long as you aggro the NPCs first the person crossing will not get aggro", Plank2g) | npc_say overheads; guards face/attack | W Strategies, Keystone; NR Tightrope; V |
| T4 | **Crossing** | 10 ticks, one tile a tick, the crosser delayed throughout | rope end → one past the far end | needs Agility ≥ **floor(base Agility × (80..100)%)**, the % fixed per raid (99 → 79..99); xp 7.5 | logbalance walk (762/763); "You walk carefully across the tightrope..." | JA; NR (10 ticks, xp) |
| T5 | **The dump** | hits whose landing falls while the crosser is delayed wait; all land on the **first tick off the rope** | — | every guard attack in flight (≈2-3 each over a crossing) | splats in one tick | W Strategies: "The player will take all damage in one tick after crossing the rope"; engine: a player queue runs only when not delayed (`player_can_access`) |
| T6 | **Guard swing** | engine attack clock, every **4** ticks, range 10, line of sight | — | accuracy **(210+9)×(120+64) = 40296** vs the target's ranged / magic defence roll; ranger 0..**70**, mage 0..**22** on a hit | ranger anim 426 + projectile 15 (42→30, 40+10+5/tile cycles); mage anim 711, cast 129 (h 90), projectile 130 (43→25, 57+18+5/tile), impact 131 / splash 339 | W infoboxes (max, attack speed, levels, bonuses); NR DeathlyNPC (ids, timing); W No Time for Death (accuracy matters: tank gear) |
| T7 | **Prayer read** | at **landing** | — | matching overhead leaves **33%** | — | W ("partially reduced"), NR 0.33 |
| T8 | **Landing tick** | L = swing + round(arrive/30) | — | ranger at d tiles: 50 + 5d cycles; mage 75 + 5d | projectile arrival | NR Projectile timing; Olm's verified rounding |
| T9 | **Keystone** | Take | keystone loc (stays) | 1 free slot; one held at a time; drops on death; ironmen may pick up a teammate's | anim 832; obj in pack | W Keystone; NR KeystoneCrystal; W CoX Changes |
| T10 | **Dispel** | Dispel op or keystone-on-barrier | the barrier | consumes the keystone, removes the barrier, kills every surviving guard; points to the dispeller | barrier gone; guards gone | W Keystone, Shimmering barrier; NR ShimmeringBarrier (2000 points) |
| T11 | **De-aggro** | a guard whose target leaves its render range drops it | — | used to reset a guard (Synq "run out of its render distance"; "step on these skulls") | — | W Strategies, V |
| T12 | **Safespots** | — | layout-specific tiles where rangers / mages cannot see | — | — | W gallery (images, not tiles) |

## 3. Rulings

- **R1 the dump is the engine's queue rule, not a script volley.** The wiki states
  the outcome; the rope skip (Nang, Synq) shows the mechanism: hits launched while
  you walk all land together when you step off, a second crossing collects a second
  volley, and a "lost tick" shows splats before you re-mount. Holding hits in the
  crosser's queue while they are delayed reproduces all three. Our old code paused
  the guards and rolled ONE hit each at the far end (too little; the skip was free).
- **R2 accuracy.** The wiki's tank advice (Torag's / Justiciar vs Karil's / Masori)
  only makes sense with an accuracy roll; NR rolls one. Ours had none.
- **R3 prayer read at landing** (NR applies the multiplier on hit application). The
  skip guide's "pray mage for this third volley after the second round of attacks
  have launched" agrees: the overhead counts when the hit lands.
- **R4 points** go to the dispeller (wiki), 2000 (NR; "some points" in the wiki).
- **R5 Agility requirement.** Ash (80-100% of the team average) outranks NR (lowest
  + random(2)). Solo = own level; the % is drawn from the raid seed so every
  crossing in a raid agrees.
- **R6 guard positions.** NR picks a random tile of two per guard (two can stack);
  ours keeps the two tiles distinct. Unsettled, kept.

## 4. Content audit

| # | mechanic | ours (before 2026-10-18) | OSRS | verdict |
|---|---|---|---|---|
| A1 | barrier tile | (14,14) / CW (14,12), angle 0: **mid-room, blocking nothing** | (6,16)a0 / (13,24)a1 / (27,15)a2 at the exit | **FIXED** |
| A2 | barrier removal | `loc_del(0)` (pops back a tick later) | stays gone | **FIXED** hold 30000 |
| A3 | wake | latched in a player varp on the FIRST crossing only; all guards | every crossing, idle guards only, 1-4 shouts | **FIXED** |
| A4 | the dump | guards paused (`npc_setmode none`) during the walk, one roll each at the end, then `applayer2` | every hit in flight, landing on the first tick off | **FIXED** (engine queue rule; pause/dump/resume procs removed) |
| A5 | crossing end | stood ON the far blockwalk rope-end loc | one past it | **FIXED** `~cox_tightrope_landing` |
| A6 | Agility requirement | none | 80-100% of the team average | **FIXED** |
| A7 | accuracy | none (flat 0..max) | (210+9)×(120+64) vs defence roll | **FIXED** |
| A8 | prayer read | at the swing (max scaled) | at landing | **FIXED** `[queue,cox_tightrope_land]` (+ gear reduction) |
| A9 | projectile timing | generic (40,36,32,15,0,11,5), floor(d/30) | NR (42,30,40,15,10,64,5) / (43,25,57,15,18,64,5), rounded | **FIXED** |
| A10 | keystone take | no "already holding" check; points on take, latched in a player varp | one held; points on dispel | **FIXED** |
| A11 | dispel | op only; "The barrier does not budge." | op or use keystone; NR text | **FIXED** `[oplocu]` + NR lines |
| A12 | survivors killed | `npc_del` (no death) | "automatically die" | kept (`npc_del`); a death animation is cosmetic — open |
| A13 | drops | `death_drop null` | Bones, always | **FIXED** cox.npc |
| A14 | spawn wrote `%varp` | `%varp6803/7042 = 0` in the spawn proc | — | **REMOVED** (a spawn may run with no player) |
| A15 | guards wander | generated `defaultmode=wander`, `wanderrange 5`, `maxrange 7` | stand on their platform | **OPEN** — no source either way; left |
| A16 | timers with no player | no tightrope npc timer | — | ✅ |

## 5. Still open

- A12 (death animation on dispel), A15 (wander), T11 de-aggro range (engine
  `maxrange 7` vs "render distance"), T12 safespot tiles (wiki images only).
- cox_selftest rows pinning the old wake latch / take-time points now fail: they pin
  wrong behaviour — root to update (`~cox_tightrope_wake` second call, keystone points).

## 6. Probes

P1 a cross with all four guards idle: hit rows all on one tick, 1 tick after the
crosser's last step; P2 a second crossing: a second volley; P3 Agility gate refuses
at 60 Agility in a 99-requirement raid; P4 the barrier sits in front of the Passage
and Dispel kills the survivors; P5 no `abort` / `no primary player bound`.

## 7. Probe results, 2026-10-18 (scriptrun, `::coxtest 4 0`, seed 0, solo, 99s)

Probe: `/private/tmp/claude-501/-Users-matthewevers-Documents-git-repos-3draster/161e2e26-be26-45f2-96e7-4b10a354aafa/scratchpad/probe/tightrope_probe.lua`.

- 4 guards present (2 + 2); barrier found in front of the exit; rope end found.
- Cross 1 with all four idle, Protect from Missiles: walked 10 ticks; **99 → 36 in
  one tick**, on the tile one past the far end, the tick after the last step (T5 ✅).
- Keystone taken (one held); cross 2 (hp reset to 99): **99 → 61 in one tick** (the
  guards re-targeted the second crossing ✅).
- Dispel: keystone consumed, barrier gone, 0 guards left (T10 ✅).
- `abort` / `no primary player bound`: none.
