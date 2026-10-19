# The solo shamans solver (plan, v2 layers)

Plan for `script/plugins/quest_driver/cox2_shamans.lua` (a `QD.T2` room like
`cox2_olm.lua`) and `test/raids/cox_shamans.lua`. Spec and content audit:
`shamans_spec.md` (row ids S-* below). Written 2026-10-18; no solver code yet
(the room entry `::coxtest 8 <n>` landed the same day).

## The strategy a strong solo human plays

From Synq (0:34:38-0:41:59), the wiki (CoX/Strategies, Shayzien Specialist)
and the video corpus (Tubby, TheEdB0ys, Extile, Gnomonkey, Beleti):

1. **Protect from Missiles all room** (blocks the ranged auto; S-RNG). Anti-
   poison drunk before or on the first poison (the auto poisons through the
   prayer; S-RNG/S-ACID-P).
2. **Range from the wall.** Stand on a tile whose 3x3 neighbourhood holds a
   blocked tile (a wall, a corner, a pillar): the shaman cannot jump there
   (S-JUMP). Every room layout has long wall runs.
3. **Move on every swing tick** (the shaman's 4-tick clock): be stepping on the
   tick the shaman animates, ≥ 3 tiles from my tile at the end of the tick
   before it, by the read tick of the acid (S-ACID-T). Done right "the acid
   splats will never hit you" at any distance ≥ 2 from the body. The swing
   cadence after the move: fire, step, fire (Synq: move on T, fire on T+1 /
   T+2; Beleti: "walk a tile, then run" to save run energy). Never be
   adjacent (d = 1): melee, and an instant splat.
4. **One shaman at a time.** Engage the nearer one from max range; the other
   stays idle while I am outside its 8-tile hunt (S-AGGRO; WIKI-CA "may not
   aggro for a while"). Lure the engaged one toward a wall / the entry if
   needed.
5. **Spawns:** when the shaman raises its hands (anim 7157) and three spawns
   appear (one under me), walk 3+ tiles away from all three before their
   fuses run out; the one under me goes first; they telegraph with 7159 three
   ticks before (S-SPAWN, S-SPAWN-X). "Small chip damage ... fine to take a
   few to keep up DPS" (Synq) — priced, not forbidden.
6. **A jump that does happen** (I was caught off a wall): run 2+ tiles from
   the tile I stood on at its anim (S-JUMP-L), and do not click the airborne
   shaman (hits do nothing; S-JUMP-A).
7. After the last kill: pick up the buchu seeds (S-DROP; Beleti "you won't
   need the other two"), then out through the exit once the blockers burst
   (S-CLEAR, clear + 8).

Optional (not in the first version): safespot/lure via the jump into a
pocket and a 10-range weapon (WIKI-CA, Tubby) — the kit has no 10-range
weapon; long-range dragon crossbow is 9 > 8 and is enough to outrange.

## Kit

Start from the Olm kit (`test/raids/cox_olm_solo.lua`, We Do Raids minimum +
the raid's (+) potions). This room needs, beyond it:

| need | why | proposal |
|---|---|---|
| anti-poison | severity-12 poison from the auto and the acid; the (+) potion set has none | 1 sanfew serum(4) or anti-venom (wiki Strategies "bring a sanfew serum, prepot range and antipoison"); or the raid's own antipoison if the supplies unit implements it |
| ranged weapon | the dragon crossbow + ruby dragon bolts (e) are already in the kit (the head phase); long range = 9 tiles, outranges 8 | worn for the room, Ava's assembler, black d'hide body |
| run energy | moving every 4 ticks | the kit's stamina |

No other gear change. The blowpipe (in the kit) is the faster weapon for a
strong human but needs the move-fire rhythm at 5 tiles; v1 uses the crossbow
on long range.

## Perception (only what a client is sent)

| fact | verb |
|---|---|
| shamans: tile (server_x/z), size 3, health, current anim + its tick | `api_drive.npcs` rows `server_x/z`, `size`, `seq_id`, `seq_tick` (key on FIRST SEEN, not `seq_tick`) |
| swing tick T | `seq_id` 7193 (ranged or acid: indistinguishable at T, and both are answered the same way: move), 7192 melee, 7152 jump, 7157 summon |
| which style it was | projectile 1291 (ranged) vs 1293 (acid) in `api_drive.projectiles` — but see the MAP_PROJANIM gap below; the plan does not need it (move on every swing) |
| acid landing tile / tick | the target tile = my tile at the end of T-1 (I know it); L from d at T (S-ACID-T); confirm with spotanim 1294 in `api_drive.spotanims` |
| jump landing | my tile at end of T-1; the shaman reappears there on T+4 (`npcs` row moves; anim 6946) |
| spawns | `npcs` rows 6768 (`zeah_lizardshaman_spawn`), first-seen tick; anim 7159 = 3 ticks left |
| tendrils, blockers | `api_drive.locs` 29768 (`t.world.loc_near`; Pass = `t.player.click_loc(..., 1)`, the CCW layout has a second set inside the room); `npcs` 7575 |
| room geometry (walls, open tiles) | `api_drive.los` / the collision the plan already floods (`aplan`); a "jump-proof tile" = one with a blocked tile in its 3x3 |
| my prayer, poison, hp | the prayer verbs; the poison orb varp the client is sent; hitpoints |
| drops | `api_drive.objs` (raids_seed_buchuleaf) |

**Missing / to check (engine work, not strategy changes):**
- `api_drive.projectiles` may not carry the shaman's shots: the first probe's
  server printed `osrs239 has no MAP_PROJANIM -- dropping it`. Not needed by
  this plan, but a client would draw the acid in flight; the root/engine
  should confirm projectiles reach both lanes.
- No verb reads a camera shake (the spawn tell is also the anim 7157 and the
  spawns themselves; nothing missing for the solver).
- A "tile's 3x3 has a blocked tile" query in C (cheap in Lua for one tile; the
  planner wants it per candidate tile — add to the `aplan` builder as a soft
  term or a precomputed mask, `api_drive.aplan` side).

## PREDICT: the threat field

| hazard | term |
|---|---|
| acid | on a swing first seen at T by shaman s with d = range(s, my tile at end of T-1): the 5x5 round that tile, at the end of T+L-1, L = 1+floor((1+d)/3); cost E[29.5 x Shayzien factor] + poison; a ranged swing looks the same, so EVERY 7193 swing is priced as a possible acid at 1/6 (not adjacent) — the plan just leaves the 5x5 |
| pre-swing | a shaman whose attack clock is due on T (its last swing + 4, or + 8 after a jump; keyed on first-seen swing ticks) prices my tile at the end of T-1 as "the acid target if the swing is acid": the plan wants to be MOVING on T (Synq) — modelled as: the 5x5 round my end-of-(T-1) tile at end of T+1 costs 1/6 x 29.5 (L ≥ 2 because d ≥ 2 is enforced) |
| jump | a tile whose 3x3 is all open at the end of T-1 of a due swing costs 1/6 x (DPS loss of a jump, ~8 ticks) — soft; after a seen 7152: the 3x3 round the jump tile at end of T+3 (forbid, 22.5) |
| melee | d = 1 at a due swing: forbid (melee 1/7 + instant acid) |
| spawn burst | each spawn's 5x5 at its burst tick (first seen + fuse; fuse unknown 10-20, the one under me first; 7159 = 3 ticks left): cost 5-10 by distance, at fuse ≥ first-seen + 10 from the earliest |
| ranged auto | 0 under the prayer; poison handled by the RESOURCE layer (drink at first poison) |
| aggro | a tile within 8 of the idle shaman: soft cost (pulls a second shaman: doubles every term above) |

## STRATEGY: the mode machine

| mode | enter | do | leave |
|---|---|---|---|
| ENTER | room start | prayers on (Protect from Missiles + Eagle Eye / Rigour), switch to the crossbow, long range | through the tendrils → PICK |
| PICK | no target | choose the shaman nearer a wall run / the one I can reach without entering the other's 8 | → ENGAGE |
| ENGAGE | target chosen | attack from 8-9 on a jump-proof tile; on each of its due swings be stepping on T (plan) | target dead → PICK; none left → LOOT |
| DODGE_SPAWN | spawns first seen | leave all three 5x5s before the earliest burst; keep firing when the plan allows | spawns gone |
| DODGE_JUMP | 7152 seen | leave the 3x3 by T+3; don't click it till T+5 | landed |
| LOOT | room clear | pick up buchu seeds; wait for the blockers' burst (clear + 8) | exit passage |

Prayer: Protect from Missiles throughout; switch to Protect from Melee never
(d = 1 is forbidden). Poison: drink at the first poison splat.

## Probes (first run prints one row per fact)

1. Acid hits by (d at swing, ticks I moved on T, T+1, T+2): confirms S-ACID-T.
2. Jumps attempted per swing from a wall tile vs an open tile (expect 0 on a
   jump-proof tile).
3. Re-target after a jump (which player, which tick).
4. Spawn fuse spread and burst damage by distance.
5. Whether projectiles 1291/1293 are seen at all (the MAP_PROJANIM gap).

## PASS

On `::coxtest 8 <n>`, 48 seeds: both shamans dead, the player alive, **0
acid hits, 0 jump slams, 0 melee hits, 0 unprotected ranged damage**
(Shayzien Specialist's "without taking damage from any shamans", which
excludes poison); spawn burst damage allowed (≤ 30 total — Synq: "a few to
keep up DPS"); the room done in ≤ 200 ticks (Synq/Beleti: about a minute for
the kill; budget doubled). Then one live seed with an identical tick log.

## Round 2: what the solver does (2026-10-18; 48/48 green)

`script/plugins/quest_driver/cox2_shamans.lua`, `test/raids/cox_shamans.lua`.
Sweep: `--name shamans0..47` (n = N), 48/48 PASS, mean 239 ticks, zero
shaman damage on every seed, spawn chip 0-16 (77 in all), no script aborts.

What the probes changed from the plan above:

- **Reactive, not pre-emptive, dodging.** Pricing every due swing as an acid
  (Synq's blowpipe method) left a ranged bot never shooting with two shamans
  awake, so it bought hits. The rule that works: keep every awake shaman 5+
  tiles off (a 10/tick term inside 4) -- from 5 the acid takes 3 ticks
  (S-ACID-T), so it is dodged after its swing and projectile show; the 5x5
  is forbidden on the read tick and the one after, with a soft ring a tile
  wider (a planned 2-tile step sometimes comes out as 1). Only while a shaman
  is within 4 is the pre-move mark kept.
- **One at a time:** an idle shaman's hunt range (9) is priced.
- **Entry:** the tendril is Passed by `world_op` (each set once; the hop is
  confirmed by my tile), then 6 ticks with no target so the plan gets clear
  of the door before the first shot.
- **No line of sight for 8 ticks:** walk closer (a shaman 6 off behind a wall
  never swung and was never shot).
- **The exit spawns** are blockwalk-all npcs the plan's map does not hold:
  forbidden tiles.
- **Done:** two deaths seen (solo count) and the exit spawns gone.

Kit beyond the Olm kit: the dragon crossbow + ruby dragon bolts (e) on rapid
(the kit's head weapon), black d'hide body, Ava's assembler, antidote++(4),
one stamina, 6 Xeric's aid (+), 4 revitalisation (+).

## Round 3: the whole-raid contract (2026-10-18)

The solver finds its room itself (`QD.COX.here()`, else the cell the room's
way out leads to); `opts.centre` is optional. It drinks an anti-poison
(sanfew serum, antidote++, plain antipoison: whichever is carried) only once
poisoned. The test carries exactly `cox_raid_solo.lua`'s kit (no
anti-poison): with the corrected 12-damage poison, 48/48 still pass; the room
costs 58-109 hp (poison included), paid from Xeric's aid. **Kit request:** a
sanfew serum for a raid holding shamans (wiki Strategies).

## Round 4: the recorded room (2026-10-18)

The content moved to Jagex's recorded behaviour (`docs/rsprox-analysis/cox/shamans.md`): acid more often (2 in 7)
and later (T + 2 + gap / 2), the summon a swing (about 1 in 7), the jump landing T + 6, spawns bursting T + 10 / 12 /
14 in the 3x3. The solver's model followed:

- acid L = 2 + gap / 2; the pre-move mark only in reach (gap <= 1, the only +2), CLOSE 2 (was 4: within 4 the old
  acid came in 2 ticks, and the bot walked half the room);
- the acid keyed on its tiles (the `launched` flag flips 61 cycles in and made every spit two), its landing from the
  caster's swing when seen;
- jump read T + 5, SWING_JUMP 9; the summon counted as a swing;
- spawns: SPAWN_R 1, fuse 8 from first sight, a warned spawn's 3x3 forbidden; a spawn key seen again after a gap or
  past its burst is a new spawn (slots are reused every summon);
- the flick: on from two ticks before a due swing until the swing is SEEN (a shaman stepping into range swings
  late).

Seeds 0, 3, 5, 9, 17, 30 (the drained arrival, 23 prayer, no restore): 4 of 6 green, no aborts. Open:
**seed 5** runs out of prayer (0 at t545; the room is ~330 ticks of ranging at 79 Ranged) and the next spit lands;
**seed 17** stepped into an acid's 5x5 on its read tick though the solver had the acid (seen on the swing tick,
landing computed right, `acid at ... lands t367` in the trace) -- a planner question, not the model's.
