# Ice demon — tick spec, rulings and content audit (2026-10-18)

Unit `icedemon_tightrope` (COX_FINISH_PLAN.md R). Content:
`OSRS-Content/osrs-content/server/scripts/minigames/minigame_cox/scripts/cox_icedemon.rs2`,
the ice demon blocks of `configs/cox.constant` / `cox.npc` / `cox.loc`. Solver plan:
`icedemon_solver.md`. Ruling order (plan): Jagex > recorded data > wiki > plugin
constants > Near-Reality > our code.

## 0. Sources

| tag | source | file |
|---|---|---|
| **JA** | Mod Ash: kindling roll (17 Jun 2021), tree depletion (28 Jun 2020), brazier skill roll (16 Jul 2026), icefiend cadence (13 Jul 2026), regen table (9 Jul 2026), magic defence from Defence (28 Feb 2019) | quoted in `sources/wiki_Ice_demon.wikitext`, `wiki_Brazier_Chambers_of_Xeric.wikitext` |
| **JB** | Mod Bruno, 28 Feb 2019: kindling / xp per chop by level | `wiki_Kindling_Chambers_of_Xeric.wikitext` |
| **JN** | newsposts: *Chambers of Xeric Changes* (29 Nov 2023: stackable kindling cap 28, 32 points a light, 36 a kindling, secondaries to everyone, magic def 60→40); *Kebos/Collection Log* (31 Jan 2019: fuel = light); *Grotesque Guardians* (tinderbox spawn); *PvM QoL* (bronze axe on the floor); *Party Pete* (thaw healing adjusted for big parties); *Christmas 2023* (no kindling in storage) | `sources/newsposts/` |
| **C** | osrs239 cache: npcs 7584 `raids_icedemon_noncombat`, 7585 `_combat`, 7586 `raids_icefiend`; locs 29747/29748 brazier unlit (op Light) / lit (op Fuel), 29763/29764 sapling (Chop) / stump, 29876 `raids_icedemon_snow` "Ice storm" 2×2, 31634 tinderbox (Take), 31862 bronze axe (Take); objs 20799 `raids_wood` kindling, 590 tinderbox, 1351 bronze axe; healthbar 7 (sprites 1416/1415) | `configs/all.*` |
| **M** | template maps m51_83 cells 0/1, 1/1 and m52_83 cell 0/1, plane 0: braziers, saplings, axe, tinderbox, `raids_doorway` | `tools/cox_template_survey.py --find` |
| **W** | wiki: Ice demon, Brazier (CoX), Kindling (CoX), Sapling (CoX), Ice storm, Icefiend, CoX/Strategies "Ice demon", CoX "Points", CAs Kill It with Fire / Cryo No More / Blizzard Dodger | `sources/wiki_*.wikitext` (Brazier, Kindling, Sapling, Ice storm, Icefiend, Bronze axe fetched 2026-10-18, raw) |
| **P** | de0 `CoxTimersPlugin` and cox_qol `CoxAdditionsPlugin`: "Ice Demon pop" = GraphicsObject **188** in the room; cox_qol draws the frozen demon's `healthRatio` as the thaw bar | `sources/de0/`, `sources/cox_qol/` |
| **V** | Synq 0:14:39 (`synq_transcript.md:190-208`); The Academy `yt_f4cH7_HSpC4`; Plank2g `yt_aU4DDfFig0c`; Steins;Gate `yt_DQaQCq4prz8`; PecanBread CM `yt_38cqq0xFEYM` 18:29; Tubby 29:08 | `synq_transcript.md`, `sources/transcripts/` |
| **NR** | Near-Reality `IceDemonRoom`, `IceDemon`, `IcefiendNPC`, `BrazierObject`, `SaplingObject`, `RaidWoodcutting`, `TinderboxObject`, `BronzeAxeObject`, `ScalingMechanics` | `~/Documents/git_repos/RSPS-NEAR-REALITY/near-reality-server-main` |

## 1. Geometry (template room-local, unturned)

| | CCW (m51_83 0/1) | THRU (m51_83 1/1) | CW (m52_83 0/1) | source |
|---|---|---|---|---|
| demon + ice storm SW (2×2) | (7,21) | (14,24) | (21,18) | NR stormTile = demon tile |
| exit `raids_doorway` | (4,21) | (14,26) | (23,18) | M — the storm stands in front of it |
| braziers 0..3 | (8,18) (11,20) (11,23) (8,25) | (11,24) (13,21) (16,21) (18,24) | (21,22) (18,20) (18,17) (21,15) | NR = W (Brazier locations) = M ✅ all 12 |
| icefiend for brazier i | (9,18) (11,21) (11,22) (9,25) | (11,23) (14,21) (15,21) (18,23) | (20,22) (18,19) (18,18) (20,15) | NR |
| thawed demon walks to | (14,21) | (14,17) | (14,19) | NR iceDemonLocations[2] |
| tinderbox / axe (Take) | (16,3) / (20,7) | (16,2) / (22,5) | (12,3) / (10,4) | M |
| saplings | 13: x 21-26, z 5-20 (the corridor) | 14: x 14-23, z 1-11 | 18: x 3-13, z 5-20 | M |

## 2. Tick spec

`T` = server tick. NPCs run before players; a player queue runs before that
player's movement, so a landing on `L` reads the tile the player ended `L-1` on.

| # | mechanic | decide / read | geometry | numbers (solo, max stats) | tell the client sees | source |
|---|---|---|---|---|---|---|
| I1 | **Take axe / tinderbox** | on the click (oploc, adjacent) | the floor locs above | needs a free slot; one axe at a time; the loc stays | `human_pickupfloor` (827); obj in pack | W Strategies "permanent item spawn"; JN; NR |
| I2 | **Chop** | roll every 4 ticks (click arms 3 out) | any sapling | success = the standard normal-tree roll for the axe (no other source); yield `randominc(floor(WC/12))`, 0→1, max 8: 99 WC = avg 4.11; xp 30/45/55/62.5/68.5/73.5/77.7/81.5 for 1..8; stack ≤ 28, no chop at 28 | axe anim; "You get some kindling."; inventory count | JA, JB, W Kindling |
| I3 | **Sapling falls** | on a successful chop | that sapling → stump 29764 | 1/X, X = max(3, 2×party) ×5 here: **solo 1/15**; stump back after 15 ticks | loc change to Stump | JA (×5); NR respawn 15 (W "a few seconds") |
| I4 | **Light** (unlit) | first roll 3 ticks after the click, then every 4 until lit | the brazier | needs ≥1 kindling + tinderbox; success 8% at FM 1 → 78% at 99 (stat_random 20/199 of 256); deposits every held kindling (≤28) | `human_light_torch` (3687) each try; loc 29747→29748 (fire anim); message | JA, W Brazier ("Failed attempts do not light"), firemaking cadence (LostCity) |
| I5 | **Fuel** (lit) | on the click | the brazier | no roll; all held kindling; as effective as lighting | anim 832; message | JN 31 Jan 2019 |
| I6 | **Thaw** | every tick of stage 0 | all four braziers | each brazier holds 5 units a kindling and burns min(party, units) = **1/tick solo** → a kindling burns 5 ticks; pool **480 × party**, −2 per unit burned; nothing burning → pool heals 2-5/tick (party-adjusted). Solo need: **48 kindling** (two unguarded braziers of 24 lit together = 120 ticks) | the frozen demon's **healthbar 7** (blue → green) shrinks with the pool: `health_ratio/health_scale` | W Strategies "54 kindling is enough" + V Synq "absolute minimum ... two braziers of 24" (RULED: 480, §3 R1); NR shape; JN Party Pete |
| I7 | **Icefiends** | each acts every 3 or 4 ticks (re-rolled), only while its brazier burns | one per raider (min(party,4)), on random braziers (solo: 1 of 4) | each action: 1/6 chance to remove **one kindling's fuel (5 units)**; a brazier at 0 goes out | fiend anim `pyrefiend_attack` (1582) on each action; loc 29748→29747 when out | JA (cadence, 1/6), W ("remove a charge"), NR (placement random, anim) — RULED §3 R2 |
| I8 | (guarded multiplier) | — | — | NR burns a guarded brazier at ×1.5 instead of ×2.0 — **dropped**: the 1/6 deduction is the fiend's effect | — | RULED §3 R2 |
| I9 | **Pop** (stage 0 → 1) | the tick the pool reaches 0 | — | braziers out; fiends die; the demon walks out of the storm to the arena tile; 7 ticks later (stage 2) he transforms to 7585, full 140 hp, aggressive | **graphic 188** (smokepuff_large) at each brazier; fiend death anim 1580; demon walks; npc id change 7584→7585 | P (188), NR (walk, 7 ticks, poof location) |
| I10 | **Storm / passage** | removed when the demon dies | 2×2 at the demon's tile, in front of the exit Passage | the way on is shut until he dies | loc 29876 present / gone | NR removeStorm on death; P (29876 is de0's room obstacle) |
| I11 | **Style choice** | at the swing, T; prayers as of end of T-1 | every raider within 16 | Protect from Missiles anywhere → snow boulder; Protect from Magic only → Ice Burst; both/neither → 50/50 | — | W (both pages), NR attack() |
| I12 | **Snow boulder** | swing T: aims at the target's tile at T (end T-1); lands L = T + 2 + g/3 (g: the target's distance from his 2x2) | 3x3 round the aimed tile | accuracy (390+9)×64 = 25536 vs the victim's ranged defence roll; 0..**30**; ×1.5 CM | `demon_casting`; projectile **1324** from his SW tile to the TILE, start 51, angle 8, progress 64, 350→10 raw, end 30×(L−T); `castlewars_catapult_fire` at him; at L impact **1325** (height 10) + `100_cauldron_explodes` on the tile | RSProx RI-A1, A5 (tells); NR max 30; W 3×3 |
| I13 | **Prayer read** | at landing L | — | Protect from Missiles leaves **50%**; Protect from Magic leaves **33%** | splat | NR get{Ranged,Magic}PrayerMultiplier (no higher source) |
| I14 | **Ice Burst** | swing T; lands L = T + 1 + (1+g)/3 | 3×3 round the target's tile at T | accuracy as I12 vs magic defence; 0..**25**; freezes everyone caught for 8 ticks | `demon_casting`; projectile **366** `ice_burst_travel`, start 51, 325→10 raw, end 30×(L−T) (at g 1 never drawn); `ice_cast` at him; at L nine **363** at height 10, delays 0,3,3,6,6,9,9,12,12 + `ice_burst_impact`; `paralyze` to the frozen | RSProx RI-A3, A4 (tells); NR (max 25, freeze 8) |
| I15 | **Attack speed** | — | — | **3** ticks | — | W infobox (NR returns speed+1: RULED wiki) |
| I16 | **Damage taken** | every player hit | — | ×0.33 except fire spells ×2.5 (150% weakness) and demonbane ×1.15; magic defence from **Defence 160** + magic bonus 40 | — | W, JA, JN |
| I17 | **Defence bonuses** | — | — | stab 70, slash 70, crush 110, magic 40, ranged 140; regen 1/100 ticks | — | W infobox, JA |
| I18 | **Points** | per deposit | — | 32 a light + 36 a kindling; capped ≈ 6.5 stacks (≈6050 normal; cox_assistant 6760) — cap formula unknown | points HUD | JN, W Points |
| I19 | **Drops** | at death | — | every raider: 7 stinkhorn, 7 endarkened juice, 5 cicely; killer: fiendish ashes | inventory / ground | W, JN |

## 3. Rulings

- **R1 thaw pool.** NR's 250×party thaws a solo demon on 25 kindling. The wiki
  ("54 kindling is enough") and Synq ("absolute minimum ... two braziers of 24")
  outrank NR; 480 = 48 × 5 × 2 makes two unguarded braziers of 24 thaw him on their
  last unit, with "around 10 extra" for a third brazier (Synq) shaving ~25 ticks.
  The burn rate (1 unit/tick, NR) is unpublished: a kindling burning 5 ticks puts a
  solo 2-brazier thaw at 120 ticks (72 s). **Probe P1** times it against video.
- **R2 icefiend.** Ash's 3-or-4 cadence and 1/6 chance are Jagex; "a charge" (wiki)
  is ruled one kindling (5 units). Our old code emptied the whole brazier on a
  success: with ~1 success per 21 ticks a guarded brazier died within seconds and
  no brazier a fiend guards could ever be kept lit — contradicting Synq lighting a
  guarded third brazier. NR's ×1.5 is NR's own stand-in for the fiend and is
  dropped so the effect is not counted twice.
- **R3 solo fiend count.** Wiki: one per raider (solo 1). Synq's "light the two
  braziers with no ice fiend on them" reads as two fiends solo; Tubby's "the one that
  does not have the ice next to it" is ambiguous. Wiki outranks video: 1, at a random
  brazier (NR). Open: **probe P2** (a video frame of a solo room).
- **R4 tells.** The thaw bar (healthbar 7 over 7584) is the room's progress tell
  (The Academy; NR's type-7 hitbar; cox_qol draws `getHealthRatio` of the frozen
  demon). The pop is graphic 188 (two plugins key on it; NR's 86 loses).
- **R5 fire multiplier** is not two values: "150% weakness" = 250% of the roll.
- **R6 prayer on the boulder.** Wiki and Synq say Protect from Missiles only makes
  him throw boulders; neither says it reduces them. NR's 0.5 is the only number and
  stands (a dodged boulder deals nothing anyway).

## 4. Content audit (mechanic / ours before / OSRS / verdict → fix)

| # | mechanic | ours (before 2026-10-18) | OSRS | verdict |
|---|---|---|---|---|
| A1 | chop saplings | **no handler**: `raids_woodsource_roots` had no oploc | Ash roll, ×5 depletion | **FIXED**: `[oploc1/3,raids_woodsource_roots]`, normal-tree roll, Ash yield, Bruno xp, 28 cap, 1/15 depletion, 15-tick stump; `op3=hidden` in cox.loc |
| A2 | kindling per chop | `1 + WC/12` (99 → 9 capped 8, every chop) | `randominc(WC/12)`, 0→1 (99 → avg 4.11) | **FIXED** (`~cox_icedemon_kindling_roll`; `~cox_icedemon_kindling` now the max) |
| A3 | take axe | no handler | Take, 1 slot | **FIXED** `[oploc1,raids_icedemon_axe]` |
| A4 | take tinderbox | bound by **quest_corsaircurse** (sets Ithoi's hut ablaze) | Take | **BLOCKED** — `~cox_icedemon_take_tinderbox` written; the binding must move off the Corsair Curse file (not ours) |
| A5 | light roll | unconditional deposit | 8%→78% skill roll, retries | **FIXED** firemaking cadence loop, `op3=hidden` |
| A6 | points | 10 a kindling | 32 a light + 36 a kindling | **FIXED** (cap still unimplemented — §5) |
| A7 | thaw pool | 250×party (25 kindling solo) | ~48 kindling solo | **FIXED** 480 (R1) |
| A8 | icefiend success | **emptied the brazier** | removes a charge | **FIXED** one kindling's fuel (R2); anim 1582 on each action |
| A9 | guarded ×1.5 | applied | — | **REMOVED** (R2) |
| A10 | fiend placement | always braziers 0..n-1 | random (NR) | **FIXED** draws first, then spawns |
| A11 | thaw tell | none (bar never sent) | blue/green bar | **FIXED** healthbar 7, hitsplat=no, splatless hitmark on change |
| A12 | ice storm | never placed | 29876 2×2 at the demon, gone at death | **FIXED** placed at spawn, `loc_del` on death |
| A13 | pop tell | red message only | graphic 188 | **FIXED** smokepuff_large at each brazier; fiend death anim 1580 then despawn |
| A14 | demon after thaw | never attacked unprompted (huntmode none), **wandered** (generated wander 5) | aggressive, stands | **FIXED** `huntmode=aggressive`, `huntrange 8`, `wanderrange 0`, and an `npc_attackplayer` on the nearest raider at the transform (the probe showed the form change keeps the frozen form's mode, so the hunt alone never engaged) |
| A15 | attacks | instant damage on swing, no anim/projectile/gfx, no accuracy, prayer 33% both | I12-I14 | **FIXED**: anim 69, projectile 1324 to the tile, impact 1325, burst 363 ×9, landing queue, accuracy roll, 50%/33% at landing, freeze 8 |
| A16 | hit area | players within 1 of the target's tile at the swing, hit at once | 3×3 at landing | **FIXED** (the dodge now works) |
| A17 | defence bonuses | none (0) | stab 70 … ranged 140 | **FIXED** cox.npc |
| A18 | magic defence from Defence | uses Magic 390 (`~npc_defence_roll`, combat_stats.rs2) | Defence 160 | **FIXED by root** (2026-10-18): an ice demon exception in `~npc_defence_roll`, citing Mod Ash |
| A19 | death | engine default only | storm gone, secondaries ×3 per raider, ashes | **FIXED** `[ai_queue3,raids_icedemon_combat]` + `death_drop,fiendish_ashes` |
| A20 | timers with no player | demon/fiend timers on registers | — | ✅ kept (no %varp anywhere in npc scripts); spawn no longer writes `%varp6794` |
| A21 | `::cox_icedemon_fuel` | lit braziers 1..3 assuming fiend at 0 | — | debug only; now fills the actually unguarded braziers. No test may use it |

## 5. Still open

- A4 tinderbox: fixed by root (corsaircurse.rs2 calls ~cox_icedemon_take_tinderbox inside a raid).
- Kindling points cap (≈6.5 stacks; formula unknown) not implemented.
- Thaw burn rate and solo fiend count: probes P1/P2.
- Sapling chop success roll: no source; the normal-tree roll is used.
- Light-roll cadence (3 then 4) is the firemaking skill's; NR retries every 2.
- Demon hunt range 8 is a calibration (Blizzard Dodger says the far walls are out of it).
- cox_selftest rows that pinned the old behaviour (`~cox_icedemon_kindling(24) = 3`,
  the ×1.5 guarded burn) will now fail: they pin wrong behaviour (CLAUDE.md) — root to update.

## 6. Probes (what the first run must print)

P1 thaw ticks from first light to graphic 188 with 2×28 unguarded (expect ~120 at 48;
with 56 burnt in parallel, 120); P2 fiend count/placement per seed; P3 boulder landing
tick vs projectile arrival at d = 1..10; P4 a 2-tile step dodges every boulder; P5 no
`abort` / `no primary player bound` lines.

## 7. Probe results, 2026-10-18 (scriptrun, `::coxtest 3 0`, seed 0, solo, 99s)

Probe: `/private/tmp/claude-501/-Users-matthewevers-Documents-git-repos-3draster/161e2e26-be26-45f2-96e7-4b10a354aafa/scratchpad/probe/icedemon_probe.lua`
(crude: no solver, a carried tinderbox because A4 is blocked).

- Axe Take works; the floor tinderbox's Take runs the Corsair Curse handler (A4).
- Chopping 28 kindling with the bronze axe: 47 / 38 / 38 ticks. Each light lit on
  the first or second roll (3-7 ticks incl. the walk).
- Three stacks of 28 lit at t140, t192, t246; the bar read 78% → 33% → 21 / 13 / 5 /
  1%; the pop (four graphic 188 at the braziers) at t282, 142 ticks after the first
  light; the demon walked out and became 7585 at t290 (P1: 84 kindling deposited,
  the thaw needed ~48 of it).
- Thawed, he engaged at once (after the aggro fix); a boulder every 3 ticks, projectile
  1324 aimed at my tile, impact 1325; with Protect from Missiles the hits were 8, 14,
  12, 11, 14 ... (≤ 15). The splat lands about one tick after the projectile's visible
  arrival: P3 to settle against video before the solver keys on it.
- Killed: the storm loc deleted, 7 stinkhorn / 7 juice / 5 cicely in the pack,
  fiendish ashes dropped (obj_add 25766). The fiend death no longer trips the engine's
  content-contract warning.
- `abort` / `no primary player bound`: none.
