# Vespula — solver plan (2026-10-18)

Room file to write after F3/F4: `script/plugins/quest_driver/cox2_vespula.lua`
(a `QD.T2` room, `cox_vespula`), test `test/raids/cox_vespula.lua`. Every number and
tell: `vespula_spec.md` (rows Vn). Method: `docs/CONTENT_SOLVER_MANUAL.md`.

## 1. The strategy a strong solo human plays

The **Redemption method** (wiki Strategies; Synq 1:25:34-1:33:53; The Academy; G2
guides), the room's standard and the fastest:

1. Pass the tendrils, walk in, quick prayers = Redemption + Rigour (Augury with a
   staff). Attack the **portal** at once: that enrages Vespula, who flies to the
   portal front and stops stinging grubs (SQ 1:27:13). Whatever she tagged before
   that is draining (V8).
2. Loop: from the **safe tile** (two tiles off her footprint on her row/column, §2 of
   the spec) click the portal; the attack path puts you on the 8-range row **under
   her** (or the 7-range cove with a 7-range weapon on longrange); the swing fires;
   click back to the safe tile. Under her you take the stomp, 2-8 a tick (V13). A
   4-tick weapon does this off cooldown; a 5-tick weapon waits a tick on the safe tile.
3. Redemption fires on the first stomp at ≤9 hp and heals 24 (V19); prayer is then
   0: drink a prayer enhance / restore dose (an enhance gives +1 point a sip and keeps
   regenerating, SQ 1:30:08), turn the quick prayers back on (watch for them staying
   lit: SQ 1:31:45), continue. Never go under her with hp ≤ 8 without Redemption up:
   brew/eat on the safe tile first (AC: "over 10 HP before we click the portal").
4. Keep the portal hit at least every 24 ticks so she stays enraged (V12). If a grub
   was tagged and the portal will not die before it hatches (V8 at 125 ticks from the
   tag), a root run during an enrage window: 3-4 blossoms a tagged grub (SQ 1:27:47).
5. Portal dead → room clear; pick up the drop (2 Xeric's aid (+), revitalisation
   (+), prayer enhance (+), overload (+)), leave by the burst boils.

The intended method (land her at 20% and kill the portal while she is down) is the
fallback when Redemption is not viable (low prayer supplies); not the plan.

## 2. Perception (client-sent only)

| fact | verb | notes |
|---|---|---|
| room origin, variant, turn | `api_drive.locs` / `locs_of`: the pillar loc `raids_vespula_portal` (30072) tile + angle; tendrils (30024); boils (30070) | variant from the pillar's template position; turn from tendril vs pillar |
| Vespula's form | `api_drive.npcs` row `npc_id` 7530 / 7531 (enraged) / 7532 (grounded), `server_x/z`, `size` | the enrage tell is the type change; landing/takeoff by `seq` 7457 / 7452 |
| her ranged attack | `api_drive.projectiles` spotanim 1364 aimed at me; `seq` 7455 / 7450 | land tick = release + round(cycles/30) |
| the grub sting | grub `npc_id` 7534 → 7535 the tick after her `seq` 7454 | |
| grub meter | grub row `health_ratio` / `health_scale` | **MISSING (engine)**: see §7 E1 |
| soldiers | `npc_id` 7538/7539 rows, `seq` 7460 hatch / 7462 explode; `spotanims` 1365 / 1368; heal `projectiles` 1367 | explode warning = the 7539 form's age (33 ticks from its first sight) |
| portal health | portal row `health_ratio` (it takes hitsplats) | |
| portal drain | `projectiles` 1366 toward my tile | lands next tick on that tile (V18) |
| my hp / prayer / Redemption state | `api_drive.skill`, `varbit` of quick prayers / the prayer varps the client holds | Redemption "fired" = prayer 0 + the redemption spotanim on me |
| roots | `locs` 30068 (flowered) vs 30069 | regrow 8 ticks |
| hitsplats on me | tick log `hit_player` (monitor only) | |
| safe / attack tiles | `api_drive.server_los` / `aplan` reach set to the portal | probe P5 |

## 3. PREDICT: the threat field (expected HP, absolute ticks)

| hazard | term |
|---|---|
| stomp | enraged: every tile of her 5×5 at every future tick k while the enrage holds (last portal hit + 24): E = 5 (2-8), max 8, typeless. Not `forbid`: the method pays it; priced so Redemption is the plan's budget |
| enraged sting | her edge ring minus corners, at every even enraged tick from the transform (V14): E = 12.5, max 20 → `forbid` unless hp_worst allows (it never should) |
| portal drain | the two hit tiles, landing tick = release + 1; prayer −3 (cost in prayer units: converts to supply via the resource layer) |
| her ranged | when not enraged and no grub left (rare): my tile at the landing; E = acc × 7 × (0.5 if prayed) |
| soldier melee | a soldier's reach (edge of its 3×3) on its 4-tick clock once 5 ticks after its rise |
| soldier blast | ≤3 tiles from its centre, 33 ticks after it lands: `forbid` |
| tendrils | leaving costs 15 (never in the plan) |

## 4. STRATEGY: the mode machine

```
enter -> open (first portal hit) -> loop <-> recover -> (roots) -> loop -> clear
                                      \-> fallback (no Redemption supplies)
```

- **enter**: walk in through the tendrils (Pass), set quick prayers, go to the
  safe tile nearest the cove side.
- **open**: attack the portal at once (stop the sting cycle, SQ).
- **loop**: on the safe tile; when the weapon is ready and hp_worst after the
  under-her ticks > 0 or Redemption is up, attack the portal, then click the safe
  tile the tick after the swing registers. Re-press quick prayers after a
  Redemption; drink a prayer enhance / restore when prayer is 0.
- **recover**: hp ≤ 10 with prayer 0 → Xeric's aid on the safe tile.
- **roots**: a grub draining whose hatch tick < predicted portal death tick
  (portal health_ratio × 250 / my DPS): leave on the tick after a portal hit (so the
  enrage covers the run, ≤24 ticks), pick 3-4 blossoms, feed, return.
- **soldiers out** (failure branch): melee them once grounded; never stand in
  a 7539's blast at its 33rd tick.
- **clear**: on the portal's death row; loot the drop; exit by the burst boils.

## 5. Kit (start from `test/raids/cox_olm_solo.lua`)

Same worn kit and (+) potions. Beyond the Olm kit this room needs:

- a weapon that reaches the portal from under her: **8 range**. The Olm kit's
  dragon crossbow is range 7 (9 on longrange: "crossbows other than Armadyl or
  Zaryte ... require longrange", wiki) → use it **on longrange**; the trident of the
  swamp also works on longrange (powered staves need longrange, wiki).
- **Redemption** (49 Prayer: at 99 it heals 24).
- prayer supply: Synq "a few restores or a single prayer enhance"; wiki "an extra
  prayer potion". Olm kit's 4 prayer enhance (+) suffice for Vespula alone, but the
  raid budget (F4) should add 1-2 if Vespula precedes Olm.
- anti-poison: optional (SQ: poison only from her direct auto, which the method
  never meets); none in the kit.

## 6. Probes (each prints a row on the first run)

| P | question |
|---|---|
| P1 | grub drain: the ticks from a 7534→7535 change to the 7537 hatch (expect 125 with the current constant); compare with a recording |
| P2 | landings: hits at ≤20% before a 7532 (the 1-in-2 calibration) |
| P3 | does she reach every grub in every variant (sting trace per grub) |
| P5 | from which tiles the engine lets an 8-range / 7-range weapon swing at the portal (the attack path end tile on the press); the safe tiles of §2 confirmed walkable and stomp/sting-free |
| P6 | redemption cycle: ticks per portal hit, prayer doses per 100 portal hp |
| P8 | enraged tick parity: the sting's even ticks relative to the 7531 transform |

## 7. Engine work found (MISSING verbs / tells)

- **E1 grub meter**: a stung grub's hp changes through `npc_statheal`, which sends no
  health-bar update; the wiki's "meter above their heads" is therefore invisible to the
  client (and to `api_drive.npcs.health_ratio`). Needs a script command that pushes an
  npc health bar without a hitsplat (or the statheal path sending one), on both lanes.
  Until then the solver can only count ticks from the sting (seq 7454 + type change),
  which is a honest client-side count (a human sees the bar).
- **E2** none other: projectiles, spotanims, npc types/seqs, locs, players and skills
  are served on both lanes already (`api_drive.projectiles/spotanims/npcs/locs/skill`).

## 8. PASS criterion (written before any sweep)

48 seeds (and 1 live seed with an identical tick log), solo, Olm kit + longrange
crossbow + Redemption:

- the portal dies, the raider is alive, the drop is picked up;
- **0 enraged-sting hits** (`hit_player` from 7531 with damage > 8) — the
  method's whole point (SQ: "if you accidentally use a different tile, Vespula can
  damage you for much higher");
- **0 soldiers hatched** (no 7538 row);
- **0 portal-drain landings** while using the 8-range row (the cove is for a
  7-range weapon only);
- room time ≤ 150 ticks from the first portal hit (SQ: the portal "always" dies
  before a hatch at decent gear; with V8 a hatch is 125 ticks after a tag).

## 9. Round 2 (2026-10-18): built, swept

`script/plugins/quest_driver/cox2_vespula.lua` (QD.T2 room `cox_vespula`) and
`test/raids/cox_vespula.lua`. As planned, with these measured changes:

- **Kit**: the WDR minimum + the Olm head set (dragon crossbow on Longrange,
  range 9, 6 ticks; Ava's; d'hide body), Xeric's aid x6, revitalisation x6
  (+2: the wiki's "extra prayer potion"), prayer enhance x4, overload x2.
- **Pre-pot**: one overload before the tendrils. **Quick prayers**: Redemption,
  then Rigour pressed by the room (the shared boost waits for 40 points and a
  revitalisation after a Redemption gives fewer). Without both the portal took
  ~130 ticks and a tagged grub hatched in 3 of 48.
- **Swing value 40** (16 lost to the stomp's price: the plan never went under
  her while she was enraged). **Xeric's aid** under 8 in the loop, under 40
  out of it (her ranged, V3, is in the field while she is not enraged).
- **Root run** only when the portal's ETA (my dealt rate) lands 20+ ticks after
  the hatch; a feed that stalls 24 ticks is abandoned.
- **E2** (round 3): the server now enforces `breakroutefinding` locs (the
  abyss, the cove's back wall); the round-2 stand-in `CV.V.OSRS_REACH` is
  deleted. Kit = the whole raid's (cox_raid_solo.lua: 1 free slot, so a root
  run picks a blossom at a time). Sweep after: 48/48, room 65-131 ticks.

Sweep (scriptrun, `--name vespulaN`, N = 0..47 = the n-th raid holding the
room): **48/48 GREEN** -- exit open, alive, 0 enraged-sting hits, 0 soldiers;
no script aborts. Variants ccw 17 / thru 10 / cw 21, all four turns. Room 65-131
ticks (median 95) from the tendrils; 4-9 Redemptions; 1-3 grubs tagged before
the first enrage. Earlier sweeps' failures, in order: no swing while enraged
(value), instruction budget (loc scan), a root run 1 tick late that fed from
her edge (margin), soldiers from slow kills (pre-pot + Rigour).

## 10. Round 4 (2026-10-18): the recorded room; solo and duo

The content now follows the RSProx recordings (`vespula_spec.md` section 8).
Two tests, two methods (the owner, 2026-10-18: "Solo room should do synqs solo.
Duo room should do the actual solve."):

- **Solo, `test/raids/cox_vespula.lua`: Synq's Redemption method** (sections
  1-9, `opts.method` "redemption"). **16 of 24** green on the recorded room. A
  tagged grub now hatches 69 ticks after the sting, not 125. Every failure is
  a grub tagged on entry that hatches before the one-slot kit can feed it (a
  soldier), or a long fight lost to attrition after one.
- **Duo, `test/raids/cox_vespula_duo.lua`: the intended solve** (Synq 1:36:20,
  `opts.method` "intended", `--bots 2`). **24 of 24** green (exit open, both
  alive, 0 enraged-sting hits, 0 soldiers), 139-241 ticks, 1-2 landings.
  - Seat 1 (`role` "enrage") opens on the portal. It then shoots her, and
    re-hits the portal inside each renewal window, the last 8 ticks of her
    20-tick cycle. Enraged she tags no grub.
  - Seat 2 (`role` "feed") feeds every grub she tagged before the first enrage
    back to full (`CV.feeder`). Full stops the drain, and enraged or grounded
    she cannot tag it again. Otherwise it shoots her.
  - Under 20% each hit may land her. Grounded 33 ticks, both seats shoot the
    portal; she re-enrages as she takes off.
  - Kit: the solo test's less the magic set, five slots for blossoms.

What the duo found:

- **The crossbow reaches the portal (9 on Longrange) only from under her or
  from her reach.** The safe tile is 10 from it. So the enrage seat pays a
  stomp or two a refresh; her ring (the sting) stays forbidden.
- **Her reach where she IS, not at home.** A portal swing enrages her in place
  the next tick, and between two cycles she flies one tick and re-enrages at
  once. Enraged away from home she flies back a tile a tick, and the predicted
  reach follows her (both tiles each tick). The feed tile is measured from
  both her tile and home.
- **A renewal is a swing AT THE PORTAL.** `S.x2.swing_last_npc` (the
  executive's new record of the attack's npc) separates it from a shot at her.
  A shot at her had read as a renewal, and she lapsed and tagged a fourth
  grub (seed 10).
- **Feed tiles:** a tier on her exact reach (under her or her edge ring,
  corners out). Then the old 2-off fallback. Then "under": off her ring but
  under her, for the thru room's grub beside her hover, whose other sides are
  all void. Grounded with 8+ ticks to go, any side.
- **The feeder:** a held blossom goes to the lowest grub once its hp left is
  down to the walk there. While she is calm, the grub in hand is fed to full
  before the next one.
- **Content:** the grub's bar now falls on the client as it drains
  (`npc_headbar_from`; before, a fed grub read full forever). Picked roots
  regrow (the portal's `[ai_queue5]`).

## 11. Synq's tiles (2026-10-18)

The owner: "Synq should have a solo method for each vespula room variant so use
that video for how to solve." His guide's description links his tile markers
(pastebin thbMmDKk -> Vespula: JugTh0FG). There are three RuneLite region
tiles, one per layout, in template room-local tiles:

| layout | marker | room-local | her hover | the lane under her |
|---|---|---|---|---|
| ccw | 13138 (13,44) | (13,12) | x15-19 z8-12 | her top row z12, east from x15 |
| thru | 13138 (46,40) | (14,8) | x10-14 z10-14 | her east column x14, north from z10 |
| cw | 13394 (17,44) | (17,12) | x11-15 z8-12 | her top row z12, west from x15 |

Each marker is two tiles off her hover, on the edge row (column) that faces
the portal.

**Probe** (`::god`, teleport onto a tile, one Attack on the portal):
- From the corner of her hover the server walks on.
- From her edge-row tiles in line with the cove (ccw x17/18, cw x12/13, thru
  z12/13) it fires in place.
- From Synq's tile it runs that lane, four tiles in two ticks, her ring never
  stood on.

So Synq's loop is: run in along the lane, swing, run back, wait for the
weapon. That is 2-3 stomps a swing ("Vespula will only damage you up to
eight"; W Strategies "6-24 damage for every attack").

**The solver** (`opts.method` "redemption", `CV.V.SYNQ`) takes his tile as the
safe tile. Hovering at home, the rest of her footprint is forbidden, so the
plan keeps to the lane. Before, a heuristic tile two off her on a row or
column, nearest the portal out of its range, let the plan wander under her.

**Sweep:** 19 of 24 green, up from 16. Every failure is a grub tagged before
the first enrage that hatched before the crossbow kit killed the portal, as in
section 10.

## 12. The solo plan: Synq's way (2026-10-18)

The owner: "Do it synqs way for solo". Synq (1:25:34-1:33:53) is his tile,
the loop on Redemption, and the portal dead before a tagged grub hatches. His
words: "If you have decent gear and average luck, you should always be able
to defeat the abyssal portal before the Vespine soldier spawn." Roots are his
bad-luck fallback, not the method. The round-5 plan made them the centrepiece
because the test's dragon crossbow cannot beat a hatch: the quickest untagged
kills took 65-83 ticks from the first hit, and a tag hatches about 64 ticks
after it. That plan is withdrawn.

### 12.1 Facts (sources in sections 8-11)

- **F1 Synq's tile** per layout (template): ccw (13,12), thru (14,8), cw
  (17,12), from his markers.
- **F2 The lane.** From his tile, Attack runs you along her edge row, 4
  tiles in 2 ticks, never on her ring, to the cove-aligned tile 8 from the
  portal. The swing fires there.
- **F3 Her enrage** starts the tick after a portal swing, in place. It lasts
  20 ticks and renews while the portal was attacked within 9 ticks.
- **F4 A tag** hatches 69 ticks after the sting. One tag before the first
  enrage is common ("she will almost always tag at least one grub").
- **F5 Tumeken's shadow,** Synq's "best weapon to use on the portal": in our
  data attack rate 5, range 8 ("can reach the portal at any setting", W
  Abyssal portal). The portal's magic defence is its lowest (W). It is
  supported by `powered_staff.rs2` / `tumekens_shadow.rs2`, and
  `::maxmage` wears it charged with the max magic set.
- **F6 Prayer.** A restore only once Redemption has fired (sip under 6)
  made the doses last (sweep 14).
- **F7 The monitor.** A root pick is not a portal swing (monitor fix).

### 12.2 The method

1. **Kit (Synq's room line).** Tumeken's shadow and the max magic set
   (`::maxmage`), quick prayers Redemption + Augury. "At least a few
   restores": 3 super restores. "Some form of anti-poison": a sanfew serum.
   Brews only for a misclick (7 Saradomin brews).
2. **Open.** Through the tendrils, Attack the portal at once, then to his
   tile.
3. **Loop.** From his tile, Attack (F2), and back to his tile the tick after
   the swing. Over 9 hp before every click. A restore after each Redemption
   (F6).
4. **Grubs.** None of his steps. The fallback root run stays only for the
   case Synq calls terrible luck: a hatch projected before the portal's
   death.
5. **Soldier.** Synq: leave the raid. The test counts it a fail.

### 12.3 Proof

1. **First, the kill time.** One probe seed per layout (cw 0, thru 4, ccw
   5). The portal must die within about 64 ticks of the first hit, the
   hatch's budget, without a root run.
2. If it does, seeds 0-47 once. Green = exit open, alive, 0 enraged-sting
   hits, 0 soldiers.
3. Any failure is traced to its first wrong decision and written here
   before the code changes.

The duo (`opts.method` "intended", the crossbow kit) is not touched.

### 12.4 Proof results (2026-10-18)

**Step 1, the kill time.** With the shadow, the portal first died 65-121 ticks
after the first hit, and 44% of swings were 0.

The combat log (`varp7430_combat_debug`) showed the magic attack roll at
24,860 (about 30,300 with Augury) against the portal's 22,940 (magic 176,
bonus +60).

OSRS's shadow also triples the worn magic attack bonus ("the player's magic
attack bonus and magic damage are multiplied by three", W Tumeken's shadow:76).
Our content had only the damage half (`powered_staff.rs2`). Fixed:
`combat_stats.rs2` triples `$magicattack` for the shadow before
`%varp6285_com_magicattack`. The kill then took 35-71 ticks, median 50.

**Sweeps, seeds 0-47:**

| run | rule | green |
|---|---|---|
| vsh4 | Synq's loop, no roots | 41/48 |
| vsh5 | + his fallback: at each re-enrage, run for roots when a tagged grub would hatch before the portal's predicted death (its bar at 4.4 a tick) + 5 | 44/48 |
| vsh8 | + enter with run, Redemption and Augury already on | 44/48 (no change) |

**What is left:**

- **The opening.** 35 of 48 seeds have one grub tagged before the first
  enrage. Her first sting is S+4 (RSProx 820). The recorded solo raider (843,
  Twisted Bow, range 10) swung at S+3; ours swings at S+5 to S+7. In seed 10
  (thru) the bot moves one tile a tick for two ticks after the tendrils on
  the attack route. The planner predicts that step from the server's own
  route; the cause is not yet found.
- **The thru room's root run.** Seeds 10, 31 and 36 (thru, turns 1 and 3)
  still hatch. Seed 10's last safe decision point (t71) had about 50 portal
  hp left and the grub 26 ticks from hatching; five swings then rolled 33.
  The run at the next window was 4 ticks too late.
- **Seed 29:** one enraged sting, not yet diagnosed.

## 13. The budget kit (2026-10-19)

The owner, 2026-10-19: "avoid 'maxed' sets and prefer budget gear". Section
12's kit (Tumeken's shadow, `::~maxmage`) is withdrawn. The solo test and the
relay now play the wiki's Minimum setup (`QD.COX.SOLO_KIT`, cox2_raid.lua;
RAID_SOLO_RELAY.md section 2). `test/raids/cox_vespula.lua` spells out
`QD.COX.solo_kit_cheats()` and calls `cox_vespula_solve({ max_ticks = 1500 })`
with no weapon or method, as the relay does (`{ max_ticks = 4000 }`). The
defaults (`V.WEAPON` "xbow", `V.METHOD` "redemption") are the budget method.
A run named `vespula_<weapon><n>` probes another weapon.

### 13.1 Facts

- **B1 The relay's crash.** `V.WEAPONS[v.weapon].rate` was nil for "xbow";
  only the shadow had one. Every weapon now has a measured `rate` and a
  `stat` (the stat its pre-pot and restores keep up).
- **B2 Kill time by weapon** (probe p1, seeds 0/5/10, Synq's loop, the
  portal's own hit splats in the tick log):

  | weapon | damage a tick | zeros | kill from the first hit |
  |---|---|---|---|
  | dragon crossbow + ruby bolts (e), Rigour, ranging potion | 2.98-3.45 | ~20% | 82-94 (untagged) |
  | trident of the swamp, mystic/occult/imbued cape, Augury | 0.36-1.20 | 50-85% | never: the raider died on the stomps first |

  So the crossbow it is (`rate` 3.0; the trident's is 1.0). Over sweep s5's
  35 greens, the kill took 64-177 ticks, median 114.
- **B3 The tag cannot be outrun.** A grub tagged before the first enrage
  hatches about 69 ticks after the sting. The crossbow's best untagged kill
  is about 64-94. The first swing lands S+6 to S+8, because the lane tile is
  6 ticks' run from the tendrils, and her first sting comes before it. 23 of
  the 35 greens had one tag. So Synq's root fallback is the method here, not
  the exception.
- **B4 The full kit has no free slot.** 16 carried, 11 potions and the
  blowpipe make 28. The old filter (blossoms needed <= free slots) kept no
  grub, so no run ever started (sweeps s1/s2, 14/48).
- **B5 Run energy is the clock.** The loop runs about 4 ticks of every 6.
  With no stamina in the kit, energy is gone about 250 ticks after entry.
  After that the lane is walked, the raider is stomped every tick, and dies
  with brews still held (s3 seeds 9/14/18/28/34/40).
- **B6 The executive re-sends a world op only when its key changes.** A
  second blossom fed to the same grub was never sent (s4 seed 18 stood 14
  ticks holding it).

### 13.2 The method

1. **Pre-pot** outside the tendrils. A ranging potion if Ranged is not
   boosted (the crossbow's). Then the anglerfish: the wiki's "prepot ...
   health" for Vespula, overhealed to ~120, which also frees the run's slot.
2. **Synq's loop**, as in section 12: Redemption + Rigour, his tile, the lane.
3. **The root fallback** at each enrage start, when a tagged grub's hatch
   comes before the portal's predicted death (bar / `rate`) + 5:
   - The free slots bound the blossoms held, not the run. The run picks,
     feeds and picks again (a new feed clock each trip), yielding to every
     renewal window (`YIELD_MARGIN` 3).
   - Each grub gets enough blossoms to outlast the portal by `OUTLAST_MARGIN`
     15, each trip costing the portal `RUN_SLOW` 15 ticks, capped at full.
     A short estimate is caught at the next enrage.
   - A run that needs more blossoms than the free slots hold finishes the
     ranging potion between trips (vial smash), which makes a second slot.
   - A second feed to a grub names it by element (a new op key).
   - The stall rule counts only ticks spent feeding.
4. **Settle.** Once the portal is dead, Redemption and the boost go off and
   hp is brewed back to `POST_HP` 50. `done` waits for that, so the relay's
   next room gets a usable raider. Nothing but a gourd vial is ever dropped.

### 13.3 Sweeps (scriptrun, seeds vespula0..47)

| run | change | green |
|---|---|---|
| s1 | budget kit, rates, settle; fallback still filtered by slots | 14/48 |
| s2 | + anglerfish pre-pot (a free slot) | 14/48 (runs start; every grub abandoned by the stall clock) |
| s3 | + outlast need, feed clock per trip, yield margin 3 | 31/48 |
| s4 | + second slot from the ranging potion, stall counts feed ticks | 30/48 |
| s5 | + second feed by element | **35/48** |

By layout, s5 has ccw 16/17, cw 17/21 and thru 2/10. The duo (`--bots 2`,
seeds 0 and 5) still passes.

**What fails (s5):**
- **Thru room, 8 seeds.** 10/19/25/42 take one enraged sting at the first
  enrage: the step back off the attack corner (6474,81 turn 1; 6453,110
  turn 3) lands on her ring as she enrages in place. 16/24/31/36 lose a feed:
  the plan holds short of the feed tile beside her hover, as in section
  12.4. Then the grub hatches, she tags the rest, and the room snowballs.
- **ccw/cw, 5 seeds.** 9 died on run energy (B5). 14 had a soldier 1 tick
  before the kill. 17/34/35: the enrage lapsed during a long run, and she
  tagged 9-10 grubs.

**Next** (not done): the thru room's first-enrage step and feed tile (the
planner's prediction of her reach as she enrages in place). Walking, not
running, on root trips that have time to spare (B5).

## 14. The thru room (2026-10-19)

Section 13 left thru at 2 of 10. This section is the room's geometry and
what each failure was, read from the s5 tick logs, before the code.

### 14.1 Facts (template room-local, unturned; turn 1 maps (x,z) to world (ox+z, oz+31-x), turn 3 to (ox+31-z, oz+x))

```
 27 ####...##...hh.....hh..ooo.#####     roots (2x2): (12,26) (18,24) (19,26) (7,27) (4,28) (2,26)
 24 #####.............h#############
 18 #######GGG.....GGG##############     G4 (7,16)            G3 (15,16)
 16 #######GGG.....GGG##############     N = (14,16): two off her top row
 15 ##########.....#################     her ring row z15 (x10-14)
 14 ##########VVVVV#################
 13 ##########VVVVVx|oooooo#########     fire tiles (14,12) (14,13): the cove
 12 #######GGGVVVVVx|oooooo#########     G2 (7,10): beside her, walls on three sides
 10 #######GGGVVVVVGGG##############     G1 (15,8)
  9 ##########.....GGG##############     her ring row z9
  8 ##########.....GGG##############     S = Synq's tile (14,8)
  1 ###############..###############
     01234567890123456789012345678901
```

- **T1 She fills the corridor.** Her hover x10-14 z10-14 spans the 5-wide
  corridor. Everything north of her (two grubs, every root) is reached
  under her, along the lane x14, with a 2-tile run step over the ring row
  z15 (and z9 to the south).
- **T2 The enrage is in place, then she flies home.** The first swing
  lands while she flies to a grub. She enrages where she is and then flies
  home one tile a tick. The sting that shows at server tick T reads my tile
  at the end of T-1 against her tile at T. Seeds 10/19 (turn 1): I was on
  6474,81 at t98, she was at 6476 at t98 and 6475 at t99, and I was stung
  at t99. Seeds 25/42 (turn 3) are the mirror image. The Redemption branch
  priced her reach as fixed on the tile she enraged on, so the step back to
  S landed on her ring one tick later. (The duo's model was "both tiles,
  k-1 and k". It misses the k+1 tile.)
- **T3 The first swing walks one tile past the fire tile** (6476,81 ->
  6476,80, 6451,110 -> 6450,111), in every thru seed, green ones too. The
  first swing's clock is one tick later than the planner predicts, and the
  server keeps walking its route while the attack waits. The extra tile is
  on her ring row at home. It is harmless while she flies. With T2 priced,
  the plan walks off it before she arrives. An engine note, not fixed here:
  an in-range ranged attack whose timer is not ready keeps walking.
- **T4 Feed tiles.** G1 is fed from S itself (its west side, x14 z8). G3 is
  fed from N. G4 is fed from (10,16..18). G2 (7,10) can be fed only from
  under her (x10 z10-12): its other sides are wall. The Redemption branch
  forbids every tile under her except the lane, so the plan could not reach
  G2's feed tile and held short of it (seeds 31/36: wandering z16 x10-12
  for 30 ticks, then "feed abandoned").
- **T5 Roots.** The pick was the root nearest by Chebyshev distance, sent
  as a server op from wherever I stood. Root (18,24) has no route from the
  corridor side the bot comes from. Seed 16: "loc op at 6488,76: 5 -> 5"
  six times, every trip yielded before any pick, the grub hatched, the
  enrage lapsed and she tagged the room. Seeds 24/31 stalled the same way
  on (18,24) and (12,26). Also, a server op route from a fire tile goes
  over the ring by parity luck alone: (14,13) + a 2-tile run lands on z15.
- **T6 The way back is to the fire tile, not to S.** From the north, the
  yield's estimate (Chebyshev to S + 3) was 3-5 ticks too long, and every
  trip turned back early.

### 14.2 The method (code)

1. **Her reach in flight (T2):** under Redemption too, while she is enraged
   away from home, or flying with an enrage predicted (my swing's), her
   reach at plan tick t is her footprint at k-1, k and k+1 tiles flown home
   (k = t - now), from the tile she is on. The stomp is priced as at home
   (Redemption: no lethal max).
2. **Roots through the planner (T5):** each flowered root's approach is the
   client route's arrival tile beside it. The root with the fewest route
   ticks is chosen. The plan walks there (a pull), and the op is sent only
   from beside the root. A root whose op does not pick within `ROOT_STALL`
   ticks of standing beside it is skipped for `ROOT_BAD` ticks.
3. **G2 from under her (T4):** while a run feeds from a tile under her
   hover, the lane-only forbid is lifted. The stomps are priced, the ring
   stays forbidden.
4. **The way back (T6):** the yield's estimate is the client route's ticks
   to the nearer fire tile + 2, or the old S estimate if that is smaller.
5. Traces: the feed tile and the root chosen, once each per change.

PASS as in section 13. Thru seeds 4 10 16 19 24 25 31 36 42 44 first, then
all 48, then the relay harness 0..23.

### 14.3 What the tick logs added while building it

- **T2, exact (cox_vespula.rs2 `[ai_timer,raids_vespula_enraged]`):** her
  enraged timer at tick D reads my tile and hers as they stood at the end
  of D-1, before her step home. The splat lands at D+1. The sting runs on
  her own 3-tick clock and fires the first tick a raider is on her ring. So
  the ring of every tile she passes through is forbidden at that tick: a
  tile on her ring at k is under her at k-1, and the footprint union
  swallowed it (seed 4 r3: stung on 6473,81 at t98 with her on 6474). The
  plan forbids each position's ring separately.
- **The sting counter missed these.** It checked my tile one tick back
  against her current tile. Seed 4 r3's 16 went uncounted. Under Synq's loop
  it now checks both two ticks back, with her enraged then. Relay seed 11
  showed that a looser check counts a stomp of 8 plus a point of drain. The
  duo keeps its old check. **s5's greens may include missed stings**
  (seeds 4/44, the G1 feeds below).
- **T7, the engine's npc reach:** a feed op on a 3x3 grub walks to that
  side's middle tile even from an edge-adjacent tile (r1 seed 4 t152->153
  6472,81 -> 6473,81; r3 seed 44 t194->195; r4 seed 2 earlier). OSRS's
  reach is any edge-adjacent tile. G1's only open side has its middle at
  (14,9), her ring, and seed 44 r3 died to that sting. The workaround is
  `CV.feed_hold`: that op goes only when decided at T-2 or T-1, where T =
  her enrage start + 20. At T her enraged timer turns her flying and
  returns; at T+1 the flying timer re-enrages her and returns. So no sting
  reads me on (14,9). She also holds while she flies between cycles.
- **T3, pressed off a fire tile:** pressed from the lane corner + 1, or from
  behind S, the server's route ended on the cove or her ring row (seeds 19
  r4 t296, 24 r4 t341/t349). The attack is now pressed only from S, the
  corner or a fire tile (`CV.press_ok`). On the lane off those, with the
  weapon due, the plan pulls on to the nearer fire tile.
- **The second run came too late:** it was decided on the weapon's 3.0
  rate, while the thru room actually goes at ~2 with its trips. The
  predicted death now takes the measured rate when that is slower.
- **The yield is latched** until the renewal swing. Its estimate shrinks as
  I walk back, and feed/back flipped for 6 ticks (seeds 24/42 r1).

### 14.4 Sweeps (scriptrun)

| run | change | thru (10) |
|---|---|---|
| s5 | section 13 | 2 |
| r1 | 14.2 1-5 | 5 |
| r2 | + route ticks run-aware, root hysteresis, yield latch | 6 |
| r3 | + measured-rate death | 6 (3 died: G1's middle tile) |
| r4 | + rings per position, honest counter, G1 at the cycle's end | 7 |
| r5 | + press tiles, the flying hold | 8 |

**Seeds 0-47 (`cox_vespula.lua`, f2): 45/48**, up from 35. ccw 17/17
(16), cw 20/21 (17), thru 8/10 (2). **The relay harness**
(`_cox_room_budget.lua`, seeds 0-23, b2): the solve returns ok 24/24.
Counting exit open, 0 soldiers and 0 stings, it is 23/24: ccw 8/8, cw
12/12, thru 3/4. Supplies there, per room: heal 7.0 doses mean (ccw 5.4,
cw 6.8, thru 11.0, max 19 of 28) and restore 5.3 mean (ccw 5.1, cw 4.5,
thru 8.0 = all 8 every time). The duo (seeds 0, 5) still passes.

**Left:**
- **Seed 4 (thru, G1 tagged):** a feed only at each cycle's end, a blossom
  a trip. The kill took 417 ticks and one soldier came out. The engine's
  reach (T7) is the fix. With OSRS's reach, S itself is a feed tile.
- **Seed 36 (thru, G2 from under her):** two blossoms per two cycles do not
  outlast a slow portal. The snowball starts at the third run.
- **Seed 35 (cw):** her enrage lapsed during a long root run (section 13).
- **Not done:** run energy (B5, seed 9 is green now) and the walk-not-run
  idea. Thru restores are spent to the last dose: a thru room before Olm
  leaves Olm without restores.

## 15. The relay's Vespula (2026-10-19)

Relay sweep r25 (48 raids): Vespula passed 12 and failed 9. The failures
looked grouped by variant and turn (thru t3 lost 3 of 3, cw t2 lost 2 of 2),
so a turned-geometry bug was the first suspect. This section checks that
against the tests, then against the relay's own tick logs.

### 15.1 Which (variant, turn) pairs the tests deal

Both tests deal the same raid for the same `vespula<n>` (`::seedrandom` +
`::coxtest`). Variant 0 is ccw, 1 thru, 2 cw:

| pair | seeds 0-47 | seeds 48-95 |
|---|---|---|
| ccw t0 | 43 | none |
| ccw t1 | 17 26 38 45 | 53 80 93 |
| ccw t2 | 13 21 23 29 30 | 49 51 55 64 69 |
| ccw t3 | 5 6 12 18 27 32 37 | 56 58 61 67 71 72 77 78 83 84 88 95 |
| thru t1 | 4 10 16 19 | 63 91 |
| thru t3 | 24 25 31 36 42 44 | 50 54 57 62 68 74 81 |
| cw t0 | 0 3 7 9 15 20 28 33 34 39 40 46 47 | 52 59 89 |
| cw t1 | 22 | 48 60 |
| cw t2 | 11 35 41 | 65 66 70 73 75 76 79 82 85 86 92 94 |
| cw t3 | 1 2 8 14 | none |

Seeds 87 and 90 did not answer in 900 ticks. Thru t0 and thru t2 are never
dealt. The harness's seeds 0-23 skip ccw t0 and hold one cw t2 (11).

### 15.2 Facts

- **R1 It is not the geometry.** Every pair that failed in the relay is
  dealt by the tests. In isolation the harness passes cw t2 on 11, 35, 41,
  65, 66, 70, 73 and 75 (8 of 8) and thru t3 on 5 of 8 (24, 36 and 42 fail).
  Those three are the room tests' known thru failures (14.4).
- **R2 Run energy.** Player-state kind 5 in the tick log is run energy x100.
  Four relay failures entered the room nearly empty: raid21 0%, raid42 6%,
  raid44 15%, raid18 34%. The three thru t3 failures entered on 83-98% and
  were at 2% by +200 (raid19), +360 (raid25) and +369 (raid46). Every pass
  entered on 80-98% and finished before it ran dry. The relay touches an
  energy pool only in the scavenger, resource and floor-end rooms
  (`cox_energy`), so a Vespula after a combat room meets her on whatever was
  left.
- **R3 What no run does.** Synq's lane needs the 2-tile run step over her
  ring (11, 12.1 F2). Below 3% the plan walks (`RUN_PLAN_MIN`), so it will
  not take the lane while she is home. raid21 (cw t2, 0%): the first enrage
  at t2643, then a root run, back to his tile at t2660 with the window open,
  then one tile a tick to the fire tile, arriving at t2662. She lapsed at
  t2663, and after t2718 she was never enraged again. The portal took 3 hits
  in 267 ticks.
- **R4 Reproduced.** `cox_vespula.lua` now takes a run name ending
  `e<pct>`, which enters on that energy (`::~run`). Seeds 0-23 at `e0`:
  **2 of 24** green. At 99% the same seeds are 23 of 24 green.
- **R5 Where it goes** (harness thru seeds, energy spent by the solver's
  mode). Root runs (roots + feed + back) spend 55-75% of it, and the loop
  about 0.4% a tick. Seeds 24/36/42 spend 136-167% against the 100% they
  start with plus regen. Standing still regains 0.24% a tick (Agility 99),
  so 0 to 100% takes about 415 ticks.
- **R6 Not energy: raid17 and raid45** (cw). 46 of 60 and 28 of 45 swings
  were blocked (splat type 26), against about 20% elsewhere. Both entered
  with no restore dose. The Xeric's aid sips drained Ranged, nothing brought
  it back, and Rigour went dark at 29-40 prayer. Not addressed here.

### 15.3 The method (code)

1. **Rest at the tendrils.** Under the Redemption method, until run energy
   is at least `ENTER_ENERGY`, stand outside the tendrils with Redemption and
   the boost off (they would drain prayer). Then light them and go in. This
   costs time and no supplies. The stamina stays saved for Olm.
2. Then measure again: `e0` seeds 0-23, the harness 0-47, the room test
   0-47, and the thru t3 / cw t2 seeds 48-95. If the thru room still runs
   dry, walk the parts of a root trip that are 4 or more tiles off her
   hover, with time to spare before the yield.

### 15.4 Results (scriptrun)

| run | change | harness 0-47 | room test 0-47 | `e0` 0-23 | harness extra (thru t3 / cw t2, 48-95) |
|---|---|---|---|---|---|
| before | section 14, the new kit | 44/48 | 44/48 | 2/24 | (cw t2 8/8 in isolation) |
| hb1/f1 | 15.3 step 1, the rest | 44/48 | 44/48 | **24/24** | thru t3 6/7, cw t2 11/12 |
| hw3 | + the far trips walked | 42/48 | not run | not run | thru t3 4/7, cw t2 11/12 |

By pair after the rest, on harness 0-47 plus the extras: ccw t0 1/1, t1 4/4,
t2 5/5, t3 7/7; thru t1 3/4 (4), thru t3 9/13 (24 36 42 74); cw t0 13/13, t1
1/1, t2 14/15 (85), t3 4/4. The room test 0-47 has the same failing seeds (4,
24, 36, 42). At 15% (`e15`) on cw t2, thru t1 and thru t3 seeds it is 15/16
(36). The duo (`--bots 2`, seeds 0 and 5) still passes: the rest applies
only to the Redemption method.

**The rest.** From 0% it waits about 390 ticks (seed 0: in at t478, against
t85 on a full bar). It spends no supplies. Mean doses a room (harness hb1):
heal 8.2 (ccw 6.1, cw 7.2, thru 12.2, max 24, i.e. all six brews), restore 6.3
(ccw 5.6, cw 6.1, thru 7.5 of 8).

**Walking the far trips was withdrawn.** It turned run off on root trips 4 or
more tiles off her, with 6 ticks to spare before the yield. It worked, but only
for 3-5 ticks a trip, because the yield is always near. It turned 24, 36, 42,
74 and 85 green and lost 23, 25, 32, 35, 44, 50, 54 and 68. Net 3 worse, so it
is not in the code. The thru seeds still left fail on energy at a full bar: they
spend 133-174% over kills of 300+ ticks, and the loop alone costs about 0.5% a
tick (4-5 running ticks in every 6). A room that slow runs dry whatever the
trips do.

**Left:**
- **Thru t3 at full energy** (24, 36, 42, 74; seed 4 thru t1): slow kills
  with the kit's unboosted Ranged (no ranging potion since this session),
  then no energy. The levers that remain are outside this room's files or
  rules: a faster kill (a Ranged boost: the relay's overload, which section
  13's owner change keeps for Olm), or a stamina (saved for Olm).
- **R6, the restore-less relay rooms** (raid17, raid45): Ranged drained by the
  aid sips, with no revitalisation to restore it. Not touched.

## 16. The relay's arrival state (2026-10-19)

Relay sweep r27: Vespula passed 12 of 19 rooms. The relay counts a room as
passed when the solve returns ok (the portal dead, the exit open, the raider
alive). Stings and soldiers are not counted against it. Every loss was read
from its own stdout trace and tick log, before any code.

### 16.1 Facts (r27)

- **E1 Every loss opens with the same tag.** At the first swing she stings
  the grub beside her home (thru G1 (15,8); the cw grub (15,13) at Synq's
  tile; ccw's east grub). The run starts at once and gets about one portal
  swing per 20-tick cycle. raid21: 5 portal hits in t3002-3063, against 10 in
  the loop. raid25, raid33, raid18 and raid44 look the same.
- **E2 Half trips.** The run sets off for a root with less time than the
  trip needs before her renewal window closes. It turns back without a
  pick and loses about 10 ticks of loop: raid21 t3009-3016, raid25
  t2592-2604, raid44 t2644-2654, raid33 t3439-3446.
- **E3 Blocks read as swings.** The monitor counts any animation start as my
  swing, unless it is the eat animation. A soldier's hit plays the block
  animation (seq 424), and so do her ranged hits. A root pick (832) and a
  feed (827) count too. Each one moves the weapon clock 6 ticks on.
  raid19, at t3312, had the portal at 5/30 when a soldier hatched (its grub
  was tagged at the opening, and the free=0 pack made no run possible). The
  soldier hit at t3319 and t3323, and the clock moved to 3325 and then 3329.
  No renewal swing was pressed, and she lapsed at t3325. For the next 140
  ticks (t3325-3464) she shot every 3-4 ticks and the plan pressed no
  attack, because its weapon was "never ready". She then tagged 49 grubs.
  The pick and feed scripts (`cox_vespula.rs2`) set no action delay.
- **E4 No slot.** raid19 and raid41 met the room with 0 free slots and no
  empty vial. A root run there has no list (`room == 0`), so the tag
  hatches.
- **E5 The restore-less rooms** (raid21, raid44, raid45, R6). Portal zeros
  ran at 53-74%, against 0-40% elsewhere. Rigour went dark below 10 prayer,
  and Ranged was drained by aid sips. raid45 fired Redemption 36 times on
  the enhance's regeneration.

### 16.2 The method (code)

1. **Not swings:** block (`human_unarmedblock`), pick
   (`human_pickuptable`) and feed (`human_pickupfloor`) go into the
   monitor's `not_swing` (E3).
2. **A whole trip or none:** a root leg starts only when the walk to the
   root, the picks and the walk back fit before the renewal window's end
   less `YIELD_MARGIN`. Otherwise the loop goes on, and the renewal swing
   makes room for the trip. The exception is a grub within `TRIP_FORCE`
   ticks of its hatch, which goes as before (E2).
3. **A slot:** when a run has no free slot and no empty vial, a 1-dose
   restore, enhance or aid is drunk (the vial smashes) (E4).
4. Then the sweep: the 19 relay seeds, then `cox_vespula` 0-47 and the
   harness 0-23.

### 16.3 What the sweeps added

- **E6 A held feed stopped the loop.** The cycle-end feed of G1 (14.3 T7)
  returned "feed-wait" for most of each cycle, and no swing was made while
  blossoms were held. raid21 a1 got one portal hit a cycle. Now, while
  the op is held and a swing's trip would end before T - 2 (`FEED_LOOP`
  6 plus the walk), the loop takes the tick.
- **E7 The endgame run.** raid46 a2 ran for a grub 4 ticks from its hatch
  with the portal 8 ticks from dead (eta 8), and lost the room. Now no run
  goes for a grub under `TRIP_MIN` 12 ticks from its hatch, and no run
  starts at a measured eta of `ROOT_MARGIN` (20) or less.
- **E8 The flying tick between cycles drank a brew.** In the one tick
  between two renewed cycles she is not enraged, so the plan priced her
  ranged (3 x 14 over the horizon) against the loop's low hp and rescued
  with a brew. That happened every 21-23 ticks: raid45 drank 22 Saradomin
  brews, and its swings were 74% zeros on the drained Ranged; raid44 did
  the same on Xeric's aid. This, not the missing restores, was most of
  R6/E5. Her ranged is now priced only when she is calm for real (no
  renewed cycle due). The calm brew floor is skipped in that tick too.
- **E9 Redemption alone** drains a point in about 10 ticks, so the "sure to
  fire" floor is `REDEEM_BARE` 2 with the boost dark, and
  `REDEEM_PRAYER` 6 with it lit. At 0-5 points (an enhance's trickle after
  a fire), every stomp had been priced as lethal.

| run | change | relay 19 | room test 0-47 | harness 0-23 |
|---|---|---|---|---|
| r27 | section 15 | 12 | 44 | 23 clean |
| a1 (6 seeds) | 16.2 1-3 | 3 of the 6 losses turned | | |
| a2 | + E6 | 15 (raid46 lost, E7) | | |
| a3 | + E7, E9 | 16 | | |
| a6 | + E8 | **17** | **47** (4 fails) | **23** solve ok, 23 clean (4 fails) |

**Left (a6):**
- **raid21 (cw t2, 0 restores, 0 heal-free prayer):** G1 tagged at the
  opening and fed one blossom a cycle (T7). The portal was still 176 ticks
  from dead (measured) at t3069. The raider died at t3190, with prayer gone
  by about t3180. This is the T7 engine reach: with OSRS's reach, Synq's
  tile is G1's feed tile and the feeds need no cycle end.
- **raid44 (cw t2, 0 restores):** the same G1 opening. Its first four swings
  rolled 0, 0, 0, 3, and 5 grubs were tagged.
- Seed 4 of both tests (thru G1, T7) as before.
