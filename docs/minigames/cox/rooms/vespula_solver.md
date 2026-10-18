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
