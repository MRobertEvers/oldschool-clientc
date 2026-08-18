# Theatre of Blood — tick pipelines, spawn geometry and movement

Companion to [`THEATRE_OF_BLOOD_PLAN.md`](THEATRE_OF_BLOOD_PLAN.md). The plan says
*what* to build; this document says **exactly which tick each thing happens on and
exactly which tile it happens at**.

It exists because "Xarpus attacks every 4 ticks" is not enough to implement Xarpus.
An attack in OSRS is not one event — it is **four**, on up to four different ticks:

```
scan  →  fire  →  flight  →  impact
```

Get the gap between *scan* and *fire* wrong and melee Xarpus is unplayable: the
poison lands on the tile in front of him instead of behind you, and the room is
permanently ruined for phase 3. Get it wrong at Verzik and the tank dies to a melee
they visibly stepped out of. Every room in this raid is built on that gap.

---

## Contents

1. [The attack pipeline](#1-the-attack-pipeline)
2. [Room 1 — Maiden](#2-room-1--maiden)
3. [Room 2 — Bloat](#3-room-2--bloat)
4. [Room 3 — Nylocas](#4-room-3--nylocas)
5. [Room 4 — Sotetseg](#5-room-4--sotetseg)
6. [Room 5 — Xarpus](#6-room-5--xarpus)
7. [Room 6 — Verzik](#7-room-6--verzik)
8. [Corrections to the plan](#8-corrections-to-the-plan)
9. [What is still unmeasured](#9-what-is-still-unmeasured)

---

## 1. The attack pipeline

### 1.1 The tick order this all rests on

A server tick in OSRS processes three groups, in this order:

> **client input → NPC turns (in NPC-index order) → player turns (in PID order)**
>
> — [Tick manipulation](https://oldschool.runescape.wiki/w/Tick_manipulation),
> paraphrasing Mod Ash. NPC actions therefore resolve **before** player movement
> within the same tick.

Everything below is a consequence of that one line. Because the NPC moves and
attacks before the player does, **an NPC acting on tick T sees the world exactly as
it stood at the end of tick T−1.** A player who clicks to step away on tick T is
still, as far as that NPC is concerned, standing where they were.

That is why every ToB movement instruction in every guide is "move **one tick
before**", never "move when you see it".

### 1.2 The four stages

| Stage | Tick | What happens |
|---|---|---|
| **Scan** | `T` (using positions from end of `T−1`) | The NPC's attack cooldown hits 0. It re-reads its target, reads the target's tile **as of the previous tick**, decides *which* attack to use (melee vs projectile, bomb vs slam), and locks the destination tile. |
| **Fire** | `T` | Animation starts, sound plays, projectile is created. From the client's point of view scan and fire are the same tick — the scan is invisible. |
| **Flight** | `T` … `T+f` | The projectile travels. `f = remainingCycles / 30` ticks (there are **30 client cycles per game tick**; `CYCLES_PER_GAME_TICK = GAME_TICK_LENGTH / CLIENT_TICK_LENGTH = 600/20`). `f` is distance-dependent for most ToB projectiles. |
| **Impact** | `T+f` | Damage is applied, ground effects are placed, AoE resolves. For "damage on impact" attacks (Verzik urnbomb, Xarpus spit, Maiden blood splat) the **position check happens here**, against the victim's previous-tick tile. |

### 1.3 The evidence

Three independent plugins implement the same `T−1` rule, and none of them cites the
others:

**blert** — [`VerzikDataTracker.java`](sources/blert_plugin/VerzikDataTracker.java):

```java
if (tick == nextVerzikAttackTick - 1) {
    if (phase == VerzikPhase.P2)      { checkForBounceChances(); }
    else if (phase == VerzikPhase.P3) { checkForMeleeChance(); }
}
```

It evaluates who is in melee range **one tick before the attack**, because that is
when the server decided.

**TobMistakeTracker** — [`VerzikMeleeChancedTracker.java`](sources/tobmistaketracker/VerzikMeleeChancedTracker.java)
stores `lastTickTargetArea` / `lastTickVerzikArea` every tick and, when the attack
animation fires, judges the tank using **last tick's** areas:

```java
private boolean isWronglyTanked(WorldArea verzikArea, WorldArea tankArea) {
    return !verzikArea.intersectsWith(tankArea) && verzikArea.distanceTo(tankArea) == 1;
}
```

Note the shape of that test — it is the actual melee-chance condition, and it says
two things at once: **adjacent = melee chance; underneath = no melee chance.** That
is the mechanical basis of "walk under her".

**TobMistakeTracker** again — every ground-damage detector in the plugin
(`MaidenMistakeDetector`, `BloatMistakeDetector`, `VerzikP2MistakeDetector`) tests
`raider.getPreviousWorldLocation()`, never the current one.

**Player-side corroboration.** Every tick guide says the same thing in words:

- Verzik P3 — *"the goal here is to **step away from Verzik one tick before she
  launches her attack**"* ([Granddad Jad, One Tick Tanking](https://www.youtube.com/watch?v=3lQjrLeuvHo&t=158)).
- Verzik P2 — *"it's best to stay close to Verzik for **every tick except for the
  dangerous tick**"* ([Plank2g](https://www.youtube.com/watch?v=eswoo8D364c)).
- Xarpus — *"Melee users can preemptively step back for **1 tick** when Xarpus
  attacks with poison. If on the correct timing, no poison will splatter on a tile
  next to him"* ([Theatre of Blood/Strategies](https://oldschool.runescape.wiki/w/Theatre_of_Blood/Strategies?oldid=15301586)).

### 1.4 The implementation contract

```
on npc tick:
    if (--attack_cooldown > 0) return

    # ---- SCAN (uses positions as of the end of the previous tick) ----
    target      = select_target()          # per-boss rule
    target_tile = target.tile_last_tick    # NOT target.tile
    attack      = choose_attack(target, target_tile)

    # ---- FIRE (same tick) ----
    play_anim(attack.seq); play_sound(attack.sound)
    if attack.is_projectile:
        proj = spawn_projectile(attack.spotanim, from=self, to=target_tile,
                                flight_ticks = f(distance))
        schedule(impact, tick + proj.flight_ticks)
    else:
        apply_hit(target)                  # melee: immediate, or +1 for Sotetseg

    attack_cooldown = attack_rate
```

`target.tile_last_tick` is the whole thing. If the server reads the player's tile
*after* player movement, every step-back trick in this raid silently stops working
and nothing in a damage test will catch it.

---

## 2. Room 1 — Maiden

### 2.1 Attack pipeline

`attackrate` 10. Two attacks share one clock.

| | Blackstorm (auto) | Blood splat |
|---|---|---|
| Seq | `maiden_attack_special` **8092** | `maiden_attack_blood` **8091** |
| Sound (fire) | `tob_maiden_shadow_attack` 3293 | `tob_maiden_blood_attack` 3981 |
| Projectile | `maiden_shadow_proj` **1577** | `maiden_blood_proj` **1578** |
| Sound (impact) | `tob_maiden_shadow_attack_part_2` 3234 | `tob_maiden_blood_impact` 3547 |
| Ground effect | none | graphic `maiden_lingering_blood` **1579** |

```
T-1   (nothing visible)
T     SCAN  target = nearest player, using end-of-T-1 positions;
            ties -> whoever is north/east of her; ties -> orb order.
            Choose blood splat or blackstorm (splat has a 3-attack cooldown).
      FIRE  seq 8092 or 8091; projectile 1577 or 1578 at the LOCKED tile.
T+f   IMPACT
        blackstorm: damage 36.5 + 3.5c, halved by Protect from Magic,
                    over-hit (no tick-eat); 50% stat drain roll.
        blood splat: place a splat on every locked tile.
T+f .. T+f+10
      Each splat is live for exactly **11 ticks** and damages anyone whose
      PREVIOUS-tick tile is the splat tile.
T+10  next SCAN
```

**`MAIDEN_BLOOD_GAME_TICK_LENGTH = 11`** — *"Each blood tile from maiden lasts
exactly 11 ticks"*
([`MaidenMistakeDetector.java`](sources/tobmistaketracker/MaidenMistakeDetector.java)).
This is a real number and it replaces the plan's earlier `[MEASURE]` for splat
lifetime. It is **not** the blood-spawn *trail* lifetime — that is a separate
entity (game object **32984**) with its own despawn, still unmeasured.

The same file also shows the splat's activation tick is computed from the
projectile, not from a constant:

```java
int gameTicksToActivate = floor(projectile.getRemainingCycles() / 30);
int activationTick      = client.getTickCount() + gameTicksToActivate;
```

So **the splat lands when the projectile lands** — flight time is distance-dependent
and must be modelled as a projectile, not as a fixed `+2`.

### 2.2 Crab spawn geometry and order

10 positions × 2 variants. The "scuffed" variant is the second tile the game may
pick for that position; which one is used is not random per crab but per spawn set
— a "scuffed" set is the thing the ToB Utilities plugin warns about.

| Position | Normal | Scuffed | Notes |
|---|---|---|---|
| N1 | (3173, 4456) | (3174, 4457) | closest to Maiden, north |
| N2 | (3177, 4456) | (3178, 4457) | |
| N3 | (3181, 4456) | (3182, 4457) | |
| N4 inner | (3185, 4454) | (3186, 4455) | the two N4s are why there are 5 north spawns |
| N4 outer | (3185, 4456) | (3186, 4457) | |
| S1 | (3173, 4436) | (3174, 4435) | closest to Maiden, south |
| S2 | (3177, 4436) | (3178, 4435) | |
| S3 | (3181, 4436) | (3182, 4435) | |
| S4 inner | (3185, 4438) | (3186, 4437) | |
| S4 outer | (3185, 4436) | (3186, 4435) | |

Source: [`MaidenCrab.java`](sources/blert_plugin/MaidenCrab.java), corroborated by
the OpenOSRS plugin's freeze groups.

Structure worth encoding rather than transcribing: the north row is `y = 4456`
(scuffed `4457`), the south row `y = 4436` (scuffed `4435`), and x runs
`3173, 3177, 3181, 3185` — a stride of 4 — with the scuffed variant one tile east
and one tile further out. The arena is mirror-symmetric about `y = 4446`.

**Count:** `min(2 × players_in_room, 10)`. **Hard Mode always spawns 10.** With
fewer than 10, the occupied positions are chosen at random from the ten — which is
why a 4-scale can be missing an N1 or an S1 and the freezers have a documented
fallback.

**Order:** all crabs of a set spawn on the **same tick**, the tick the Maiden's
body transmogs to 8361 / 8362 / 8363. Whether the spawn is on that tick or one
after is `[M4]`.

### 2.3 Crab movement

- They walk **straight toward the Maiden** — the Wiki says *"which will walk
  straight toward her"*, i.e. a direct line, not a pathfind. They are 1×1 and the
  lane between spawn and boss is open, so a straight line and a BFS agree; encode
  the straight line because it is what the freeze positions assume.
- Reaching her — blert's test is
  `crabNpc.getWorldArea().distanceTo2D(maiden.getWorldArea()) <= 1` — heals her by
  **2 × the crab's current hitpoints** and increments `c` permanently.
- Freeze chance scales with the freezer's **effective magic attack bonus**, hitting
  100 % at **+140**. That is a per-cast roll against the crab, not a boss property.
- Movement speed (walk 1 tile/tick vs run 2) is `[M23]` — no source states it, and
  it decides whether the documented freeze timings ("freeze N1 on the first tick
  possible", "the south freezer must wait an extra tick before S2") are reproducible.

### 2.4 Blood spawns

- Spawn from a landed splat: **10 %**, or **20 %** if a player stood on that splat.
  **Halved if every player dodged every splat that cycle.** Hard cap **8** alive.
- 120 / 105 / 90 HP (5 / 4 / ≤3).
- They wander and leave game object **32984** behind them. Standing on a trail
  damages the player and heals the Maiden by the same amount.
- Hard Mode: invulnerable, and the trails never expire.
- Their step rule and trail lifetime are `[M5]`.

---

## 3. Room 2 — Bloat

### 3.1 The down cycle, tick-exact

`BLOAT_DOWN_CYCLE_TICKS = 32`, `BLOAT_STOMP_TICK = 3`, and blert sets
`nextUpTick = downTick + 32 + 1`.

```
T      DOWN. seq tob_bloat_sleep 8082, sound tob_bloat_snore 3976.
       Attacks stop. Incoming damage is no longer halved.
T+1 .. T+28   attackable window. Five 4-tick attacks from adjacent,
              four from range.
T+29   STOMP. 40-80 to everything in range, tick-eatable.
              Bloat's Defence is restored to base.
              (= "the 30th tick of inactivity" if you count T as tick 1.)
T+30 .. T+32  rise animation — this is the flinch window.
T+33   UP. Flies resume; damage to Bloat is halved again.
```

The [flinch guide](https://www.youtube.com/watch?v=oKXoj9Yxy7Q) describes the rise
window from the player side: *"when he starts to get back up you'll see his hands
move, his head will move a little bit, and that's when you click back… 99 % of the
time you will click **five tiles** back"* — five tiles is exactly far enough to be
out of the first fly's reach given the run speed and the rise duration. If the
implementation's rise window is not 3 ticks, flinching either becomes free or
becomes impossible, and that is the acceptance test.

### 3.2 The walk clock

```
walk_ticks   = rand(34..42) + (previous_down_was_attacked ? 0 : 4)
first walk   = rand(34..42) + 4          (nothing attacked the down before it)
turn_cooldown  = 32 on entry (16 in Hard Mode); decrements ONLY while walking
after a turn or a speed change: down_lockout = 5   (cannot go down for 5 ticks)
```

Speed is a pure function of health: `>60 %` walk, `40–60 %` run,
`<40 %` **flips walk/run on every attack made against it, hit or miss**.

### 3.3 Flies — a per-tick attack

`attackrate = 1` in the cache. There is no scan/fire/impact split: on **every**
tick Bloat is up, every player with line of sight takes 10–20 (×0.75 with Protect
from Missiles), and the hit **spreads** — from each player hit, the LOS test is
re-run from *that player* to every other player.

The LOS test is custom and must be implemented as such:

> *"rather than using one tile within the Bloat's 5×5 area as the reference point,
> **all tiles on the side of the Bloat nearest to the player are checked**, and the
> attack is allowed to go through if any of them has an unobstructed view"*
> — Mod Ash, quoted on [Pestilent Bloat](https://oldschool.runescape.wiki/w/Pestilent_Bloat?oldid=15273178)

### 3.4 Falling flesh — a two-stage effect with a one-tick kill window

| Stage | Id | Meaning |
|---|---|---|
| Shadow (telegraph) | graphics **1570–1573** (`tob_bloat_falling_flesh1..4`) | the landing tile is marked |
| Impact | graphics **1576** (`tob_bloat_blood_splat`) | 30–50 damage + stun |

`BloatMistakeDetector` adds the tile on graphic 1576 and **clears the set at the end
of every tick** — so a hand tile is lethal for **exactly one tick**, the tick the
blood splat appears, and it is judged against the player's previous-tick tile.

Falls only while Bloat is walking and only below **90 %** health in Normal Mode;
in Hard Mode from the moment the first player enters, and **also during the down**.

### 3.5 Room geometry

| Feature | Coordinates |
|---|---|
| Fight area | (3288, 4440) – (3304, 4455) |
| North quadrant | x 3292–3299, y 4451–4455 |
| East quadrant | x 3298–3303, y 4444–4451 |
| West quadrant | x 3288–3292, y 4444–4451 |
| South quadrant | x 3292–3299, y 4440–4444 |
| Pillar-hug tiles N/E/S/W | (3292, 4450) / (3298, 4451) / (3299, 4445) / (3293, 4444) |
| Barrier tile / loc | (3288, 4447) / loc 32755 |
| Tank (blocks LOS) | locs 32955, 32957, 32959, 32960, 32964, 33084 |
| Tank top | locs 32958, 32962, 32964, 32965, 33062 |
| Ceiling chains | locs 32949–32954, 32970 |
| Floor | ground objects 32941–32948 |

Bloat walks the ring around the tank. Start **position is fixed**, start
**direction is random**.

---

## 4. Room 3 — Nylocas

### 4.1 The wave cycle

Everything is on a **4-tick cycle**; waves only ever spawn on cycle tick 0, and the
first wave spawns on the **fourth tick of the room**.

```
spawn_tick(1)  = 4
spawn_tick(n+1) = spawn_tick(n) + naturalStall[n]        if room_count < cap
                = (retry on the next cycle tick 0)       otherwise  -> "stall"
```

`naturalStall` is per-wave; the full table is in
[`nylocas_waves.md`](nylocas_waves.md). Sum for waves 1–30 is **232**, so a
stall-free room reaches wave 31 on room tick **236**.

### 4.2 Lane spawn geometry — a 2×2 block per lane

tob-qol's `NYLOCAS_VALID_SPAWNS` gives twelve **scene** points; region 13122's base
is (3264, 4224), so:

| Lane | Scene points | World tiles |
|---|---|---|
| West | (17,24) (17,25) (18,24) (18,25) | (3281,4248) (3281,4249) (3282,4248) (3282,4249) |
| South | (31,9) (31,10) (32,9) (32,10) | (3295,4233) (3295,4234) (3296,4233) (3296,4234) |
| East | (46,24) (46,25) (47,24) (47,25) | (3310,4248) (3310,4249) (3311,4248) (3311,4249) |

blert names the individual anchors it keys the wave table off:
`WEST_LANE_SOUTH (3281,4248)`, `WEST_LANE_NORTH (3281,4249)`,
`SOUTH_LANE_WEST (3295,4233)`, `SOUTH_LANE_EAST (3296,4233)`,
`EAST_LANE_SOUTH (3310,4248)`, `EAST_LANE_NORTH (3310,4249)`, plus
`EAST_LANE_SOUTHWEST (3309,4248)` — **east bigs only**, because a 2×2 needs its
south-west corner one tile further west to fit the block.

Two independent sources, same twelve tiles. This is the strongest spawn evidence in
the raid.

**Slot ordering** used by [`nylocas_waves.md`](nylocas_waves.md) is blert's:

```
[ east-south, east-north, south-west, south-east, west-south, west-north ]
```

### 4.3 Splits — same tick, six candidate tiles

From [`Nylo.java`](sources/blert_plugin/Nylo.java):

```java
public boolean isPossibleParentOf(Nylo other) {
    if (!big || other.isLaneSpawn() || deathTick != other.spawnTick) return false;
    int dx = other.spawnPoint.getX() - deathPoint.getX();
    int dy = other.spawnPoint.getY() - deathPoint.getY();
    return (dy == 0 && dx >= -1 && dx <= 1) || (dy == 1 && dx >= 0 && dx <= 2);
}
```

Two facts, both hard:

1. **`deathTick == spawnTick`** — the two splits appear on the **same tick the big
   dies**, not the tick after.
2. Relative to the big's **south-west tile**, a split can appear at exactly six
   offsets: `(-1,0) (0,0) (+1,0)` and `(0,+1) (+1,+1) (+2,+1)`.

Each split's style is **random** and independent. Splits are **never aggros**.

### 4.4 Movement, targeting and flicker

- Every wave-spawned nylo **always targets the same pillar in every encounter** —
  the assignment is fixed data, not a nearest-pillar computation. **The
  spawn→pillar mapping is `[M24]`**: no source in `sources/` publishes it, and it
  cannot be derived from the wave table.
- **Aggros** (`tob_nylocas_fighting_*`) walk at the players instead and stop
  slightly short before attacking. Fixed per wave; see the wave table.
- **Flickers** start at wave 16: spawn as *a*, switch to *b* **after passing the
  lane's halfway point**, hold *b* for exactly **2 ticks**, then settle on *c*.
  A hit landing the tick before a switch still damages the old style.
- **Everything self-destructs at 52 ticks of age** (31.2 s), wave spawns and splits
  alike.
- Immune/shield graphic **1558** *"spawns on the SW tile of the nylo"* (tob-qol) —
  useful for the wrong-style feedback.
- Attack rate **3** for every small and big (cache `attackrate`).

### 4.5 Pillars

Four supports at the corners of a 10×10 box: **(3291, 4244), (3291, 4254),
(3301, 4244), (3301, 4254)**
([Support (Theatre of Blood)](https://oldschool.runescape.wiki/w/Support_(Theatre_of_Blood)?oldid=15201834)
map data). Loc **32862** intact → **32863** breaking → **32864** broken; npc
`tob_nylocas_support` **8358** (10790 entry / 10811 hard) carries the health.

Collapse: up to 50 to the whole room, and **every nylo that was attacking it
retargets a player**. All four → instant team death. Per-pillar hitpoints remain
`[M7]`.

### 4.6 Nylocas Prinkipas — correction

Hard Mode's demi-boss is **four npc ids**, not one, and the plan's §3.1 was wrong:

| Symbol | Id |
|---|---:|
| `nylocas_miniboss_spawning_hard` | **10803** |
| `nylocas_miniboss_melee_hard` | **10804** |
| `nylocas_miniboss_magic_hard` | **10805** |
| `nylocas_miniboss_ranged_hard` | **10806** |

Cache record: **size 3**, level 400, `attackrate` **4**, strength bonus 30, magic
attack 500. It behaves like Vasilias (10-tick colour switch) and **counts as 3**
toward the room cap. Waves 10 / 20 / 30 in Hard Mode carry a forced 16-tick stall.

### 4.7 Vasilias

Spawning form 8354 → melee 8355 / magic 8356 / ranged 8357.

```
form change every 10 ticks, always to one of the OTHER two styles
attack rate 4  ->  exactly two attacks per form
the player's attack is INTERRUPTED on the tick the form changes
```

10 ticks is 2 swings of a 4- or 5-tick weapon, 3 of a 3-tick, 4 of a 2-tick — which
is the entire reason the room's weapon advice reads the way it does.

---

## 5. Room 4 — Sotetseg

### 5.1 Attack pipeline

Attack rate **5**.

| | Melee | Magic ball | Death ball |
|---|---|---|---|
| Seq | `tob_sotetseg_attack_melee` **8138** | `tob_sotetseg_attack_ranged` **8139** | 8139 |
| Sound | 3540 | 3539 | 4001 (hum) |
| Projectile | — | `tob_sotetseg_maging` **1606** | `tob_sotetseg_sharedattack` **1604** |
| Split projectiles | — | 1606 (magic) + `tob_sotetseg_ranging` **1607** (ranged) | — |

```
T     SCAN + FIRE.
        melee if a player is in melee range, else magic.
T+1   MELEE hitsplat lands  <-- one tick late, unlike almost every other NPC
T+f   MAGIC impact: up to 50; if unblocked, the victim's protection prayers are
        DISABLED for 5 ticks (3 s). Retribution/Smite/Redemption still work.
        Then split: one 1606 at another player, one 1607 at another.
        Each split obeys its own prayer.
T+5   next SCAN
```

*"Sotetseg's melee attack is not tick-eatable, although unlike most melee attacks by
NPCs, **the hitsplat is applied one tick after the attack**"* — Sotetseg wiki page.
That is a real, testable deviation and it must be modelled explicitly.

**Death ball** fires after **10 magic attacks**. Up to 188 party-scaled, **split
among every player inside the 3×3 around the target**, tick-eatable. The reliable
player cue is *"Sote's second head bob"* — i.e. the ball is telegraphed by the
animation, and the eat window is on the second attack animation
([Guide:Advanced Theatre of Blood](https://oldschool.runescape.wiki/w/Guide:Advanced_Theatre_of_Blood?oldid=15222073)).

### 5.2 Maze geometry

| | Overworld | Underworld |
|---|---|---|
| Region | 13123 | **13379, plane 3** |
| Scene offset of maze cell (0,0) | **(9, 22)** | (42, 31) |
| World origin | **(3273, 4310, 0)** | (3354, 4311, 3) per blert |
| Grid | **14 wide × 15 high** | same |

The overworld numbers agree exactly across two sources: region 13123 has base
(3264, 4288), and `(3264+9, 4288+22) = (3273, 4310)` — tob-qol's scene offset and
blert's world point are the same tile. The underworld pair does **not** reconcile
the same way and is flagged `[M25]`.

Ground objects: **33033** disabled (overworld), **33034** inactive,
**33035** active/red (plus 41750–41753 in newer content), portal **33037**,
underworld rocks 33063–33065.

### 5.3 Maze rules, tick by tick

```
on reaching 66.6% / 33.3% for the first time:
    seq tob_sotetseg_shadow_portal 8142, sound 3963, "Sotetseg chooses you..."
    chosen player -> underworld; everyone else -> far end of the arena
    Sotetseg becomes 8387 and DEALS NO DAMAGE for the duration

every 7 ticks   the chosen player takes 1-3 chip damage
every tick      the chosen player's tile is mirrored into the overworld as 33035
on first step onto ROW 4   spawn the tornado (tob_sotetseg_creeper 8389) at the
                            path start; it then follows the path
on stepping BACK to row 3  despawn the tornado
on a wrong tile            6.67% of current hp + 15 per tick to nearby players
                            (+11 in Entry Mode)
every 4 ticks   if nobody is on the grid, despawn the maze and resume the fight
```

That last line is the mechanic behind **"off on 3"**: the maze despawn check runs on
its own 4-tick cycle, so clicking off the second-to-last row on cycle tick 3 puts
you off the grid on tick 0 — the same tick the maze despawns — and the rag damage
is nulled. If the despawn check is not on a 4-tick cycle, this technique
disappears.

### 5.4 Player pathing inside the maze

The maze is the one place in the raid where **player** pathing is a mechanic:

> *"your character will always calculate its path movement — it will always go
> forward in the direction it needs to go and then finish off by doing a diagonal"*
> — [cBold, Sotetseg Maze Guide](https://www.youtube.com/watch?v=JdtL9UI5uy0&t=180)

Cardinal-first-then-diagonal is why some L-shapes are safe and others rag: an L that
goes diagonal *first* steps off the path. **The tiles cannot be run-skipped.**
Optimal paths in the two worked examples in that guide are **12 and 13 steps**, so
a generated path is roughly 12–13 moves over the 15 rows.

### 5.5 The maze generator

Recovered in full from the community trainer at
<https://devqhp.github.io/osrs/sotetseg/>
([`sources/community_tools/devqhp_sotetseg.js`](sources/community_tools/devqhp_sotetseg.js)).
Its author built it from mazes shared by named high-level players and the We Do
Raids Discord, so this is an **empirical model, not decompiled code** — strong, but
to be verified rather than trusted (see [`COMMUNITY_SOURCES.md` §3.1](COMMUNITY_SOURCES.md#31-sotetseg-maze-trainer--devqhp)).

```
maze_width   = 14      maze_height  = 15
path_turns   = 8       max_x_change = 5      tornado_row = 4

seed[0] = rand(1, 13)                       # never the far-west column
for i in 1..7:
    seed[i] = rand(max(seed[i-1] - 5, 0), min(seed[i-1] + 5, 13))

x = seed[0];  s = 0
for y = 14 down to 0:
    if y is odd:                            # rows 13,11,9,7,5,3,1
        next_x = seed[++s]
        set every tile from min(x,next_x) to max(x,next_x) on row y
        x = next_x
    else:                                   # rows 14,12,10,8,6,4,2,0
        set the single tile (x, y)

start = (seed[0], 14)      end = (seed[7], 0)
```

Eight seeds, seven horizontal runs, eight single tiles. The Wiki's worked example
seed for its demonstration clip — `7 10 12 11 12 7 11 12` — is exactly eight
numbers, each within 5 of its predecessor, which is an independent check on both
`path_turns = 8` and `max_x_change = 5`.

This reduces plan task **M11** from "derive the generator" to "confirm the
empirical generator", which the trainer itself makes cheap: generate 10 000 mazes
from each and compare the distributions of start column, per-row run length and
optimal path length.

---

## 6. Room 5 — Xarpus

This is the room the pipeline matters most in, and the one the plan got least
precise. Xarpus does two separate things on his clock: he **turns** and he **spits**.

### 6.1 What the sources actually say

**blert** treats them as one event — the field is literally named `nextTurnTick`
and it dispatches the spit on it:

```java
private static final int FIRST_P2_TURN_TICK = 7;
private static final int TICKS_PER_TURN_P2  = 4;
private static final int TICKS_PER_TURN_P3  = 8;
...
if (tick == nextTurnTick && phase == XarpusPhase.P2) {
    nextTurnTick += TICKS_PER_TURN_P2;
    dispatchEvent(new NpcAttackEvent(..., NpcAttack.TOB_XARPUS_SPIT, xarpus));
}
```

**OpenOSRS** treats the **orientation change** as the observable event and resyncs
its countdown to it:

```java
ticksUntilShoot--;
if (ticksUntilShoot <= 0) ticksUntilShoot = 4;
if (previousTurn != npc.getOrientation()) {
    ticksUntilShoot = staring ? 8 : 4;      // P3 : P2
    previousTurn = npc.getOrientation();
}
```

Those two are only consistent if **the turn and the spit are on the same tick** —
blert counts 4 from the spit, OpenOSRS counts 4 from the turn, and both land on the
same next event. The turn is not a separate earlier telegraph; it is the *visible
half* of the same tick the projectile leaves.

**The players agree, and they are more specific than either plugin.** Two
independent Xarpus guides describe the same tick:

- *"As soon as Xarpus **looks at you or your teammates and changes direction**, you
  will send your second special attack"* — [Crusher, 5 Tick Xarpus Guide](https://www.youtube.com/watch?v=fPpIRjQWtlE&t=343).
  The turn is used as a *cycle anchor*, which only works if it is deterministic and
  on the attack clock.
- *"another visual cue is to click attack **right before he turns**; this should
  result in you moving to attack Xarpus as he turns. [When he releases the] poison
  ball, that's when you should click back"* — [Plank2g, Melee Xarpus](https://www.youtube.com/watch?v=Lt-iZwJUKmc&t=91).
- *"the high-pitched chirp of Xarpus' acid spitting attack is occurring on the
  **same tick that we are using to step back**"* — [Crusher](https://www.youtube.com/watch?v=fPpIRjQWtlE&t=457).

**And a community simulator names the scan tick in so many words.** The
[Xarpus melee trainer](https://spacescape20xx.itch.io/xarpus-melee-trainer) by
SpaceScape instructs:

> *"**On tick 1 the boss registers your position.** Move away on tick 2; you will
> be away from the boss by tick 3. Attack on tick 4, repeat."*

"The boss registers your position" is the scan stage, arrived at independently by a
player building a trainer rather than by a plugin author reading event streams. It
also pins the relative ordering inside the 4-tick loop: **register → move → be
clear → attack**, with the player's attack on the last tick before the next
register. The same page adds *"wait for a tick if poison is on any 'wrong' tiles
(it means you're not in cycle)"* — i.e. a splat landing next to Xarpus is the
observable failure of exactly this alignment.

One caveat worth carrying: the trainer exposes a **`Players` setting that changes
Xarpus' attack frequency**, and its author notes *"setting Players to 1 is more
accurate for 5-ticking"*. If the spit cadence is not a flat 4 ticks at every scale,
the plan's single constant is wrong — raised as `[M31]`.

### 6.2 The resulting model

```
T-1   The melee player must ALREADY have left the adjacent tile.
      (Their step-back click goes in on T-2 so the move completes on T-1,
       or on T-1 so it completes during T-1's player phase — see [M26].)

T     SCAN   pick the next target in orb order.
             Read that player's tile AS OF THE END OF T-1.
      TURN   set orientation to face that tile.  <-- the visible telegraph,
                                                     same tick as the fire
      FIRE   seq tob_xarpus_attack_ranged 8059;
             sound tob_xarpus_attack_ranged_projectile_5 3290 (the "chirp");
             projectile tob_xarpus_acidspit 1555 -> the locked tile.

T+f   IMPACT sound tob_xarpus_acid_floor_hit 4005, graphic
             tob_xarpus_acidsplash 1556; place ground object 32744 on the
             locked tile and its 3x3.  The splat is PERMANENT for the fight.
             Chain: the first target's splat goes to the next player in orb
             order; every later target's chains to the next TWO.
             If all players share a tile, or only one is present, the splat
             goes to a RANDOM UNCOVERED tile instead.

T+4   next SCAN
```

The reason a melee player steps back **before** T and not on T is §1.1: Xarpus
resolves on T before the player moves on T, so a step taken on T is invisible to
him and the acid lands on the tile in front of him — permanently ruining a melee
tile and, if it is one of the four quadrant approach tiles, phase 3.

### 6.3 Phase clocks

```
P1 -> P2 transition (npc 8339 -> 8340, seq tob_xarpus_fly_up 8061) at tick X
X+7    FIRST spit                      (blert FIRST_P2_TURN_TICK = 7)
X+11, X+15, X+19, ...  every 4 ticks   (TICKS_PER_TURN_P2 = 4)

screech at <=25% health (22.5% Entry): overhead text, sound 3549/4007
    spitting stops entirely
    next quadrant turn = screechTick + 8      (TICKS_PER_TURN_P3 = 8)
    then every 8 ticks, NEVER the same quadrant twice in a row
```

**The 5t vs 5.3t consequence.** Xarpus on 4 and a scythe on 5 desynchronise by one
tick every three attacks:

| Swing | What the player sees |
|---|---|
| 1 | scythe swings while running **in** |
| 2 | scythe swings having **arrived** |
| 3 | scythe swings while running **away** ← the recognisable one |
| 4 | the "missed hit" — step back and lose a tick (5.33t), or **rag a tile** and keep it (5t) |

This is not decoration: if the server's spit clock is not exactly 4 and the scan is
not exactly one tick ahead of the fire, this four-beat pattern does not emerge and
every Xarpus guide ever written stops describing the implementation. **It is the
single best acceptance test in the raid.**

Phase 3's `22121` is the same arithmetic against 8 ticks: a 5-tick weapon fits
2, 2, 1, 2, 1 swings into successive 8-tick gaze windows, repeating every 5 turns
(40 ticks = 8 scythe swings).

### 6.4 Phase 1 — exhumed

Ground object **32743**, seqs `tob_xarpus_exhumed_start/loop/end` 8064–8066,
sounds 3230 open / 3995 close, heal projectile
`tob_xarpus_exhumed_energyorb` **1550**, absorb seq `tob_xarpus_absorb` 8060 +
sound 3956.

| Players | 1 | 2 | 3 | 4 | 5 |
|---|---:|---:|---:|---:|---:|
| Exhumed budget | 7 | 9 | 12 | 15 | 18 |

(OpenOSRS `XarpusHandler`; the Wiki contradicts itself here — see the plan's §15 C3.)

**Standing on an exhumed suppresses its heal** for as long as you stand there. Each
exhumed heals **more than once** — the `Perfect Xarpus` task allows up to two heals
per exhumed, which means the heal is a repeating tick event, not a one-shot. Its
period, the heal amount, the open duration and the spawn cadence are all still
unmeasured (`[M13]`–`[M15]`).

Absorbed exhumed permanently scale **two** later numbers: P2 poison damage and the
P3 retaliation (base 50–75, × `1 + 0.4 × stat uplift`).

---

## 7. Room 6 — Verzik

### 7.1 Phase 1 — 14 ticks, pillar-aware

```
T     SCAN  for each pillar that is hiding >=1 player, target that PILLAR;
            for each player not behind a pillar, target the PLAYER.
      FIRE  seq verzik_phase1_attack_magic 8109, projectile 1580.
T+f   IMPACT  pillar: exactly ONE hit however many players hid behind it.
              player: up to 137, halved to 68 by Protect from Magic,
                      NOT tick-eatable.
T+14  next SCAN
```

Damage cap while shielded: **10 melee / 3 ranged / 3 magic per hitsplat**.
Dawnbringer is exempt from both the cap and the accuracy roll (75–150).

### 7.2 Phase 2 — 4 ticks, and the "dangerous tick"

```
phase start (8371 -> 8372, seq verzik_phase2_spawn 8112)
+3    FIRST attack                       (P2_TICKS_BEFORE_FIRST_ATTACK_AFTER_SPAWN)
then every 4 ticks:

  T-1   SCAN WINDOW. blert's checkForBounceChances() runs here.
        Every player whose area is in melee distance of, or inside, Verzik's
        area is a BOUNCE (body-slam) candidate.
        ** This is "the dangerous tick" in every P2 guide. **

  T     FIRE. Choose:
          - a bounce candidate exists     -> BODY SLAM, seq 8116:
                up to 45 rolled vs CRUSH defence, knockback 3, stun 3 s.
                Under her instead of beside her -> the 82 STOMP, always hits,
                overhead "There's nothing for you there!"
          - zap cooldown expired (>=4 attacks) -> LIGHTNING, projectile 1585
          - purple cooldown expired (>=16 attacks) -> ATHANATOS, projectile 1586
          - otherwise -> URNBOMB, seq 8114, projectile 1583, one per player,
                aimed at each player's LOCKED (previous-tick) tile.

  T+f   IMPACT. Urnbomb: graphic 1584 marks the tile; up to 44 (22 with
        Protect from Missiles) to anyone whose previous-tick tile is that tile.
        The tile is dangerous for EXACTLY ONE TICK
        (VerzikP2MistakeDetector clears activeBombTiles every tick).
        Hard Mode additionally leaves game object 41747 — an acid pool that
        persists 16 ticks and deals up to 10.
```

**Scythe walking** falls straight out of this: you are adjacent on three of the
four ticks and off the tile on the scan tick. The player-facing cue is *"click back
as you see the green bomb land on the floor"* — i.e. the previous cycle's impact is
the metronome for the next cycle's scan.

**Reds (phase 2.5), at ≤ 35 %:**

```
reds tick R   attacking stops; seq verzik_phase2_heal 8117;
              two Nylocas Matomenos 8385 spawn at her left and right.
              ALL damage dealt to her during the animation HEALS her.
R+12          next attack        (P2_TICKS_BEFORE_FIRST_ATTACK_AFTER_REDS)
              and 7 attacks later the next reds
                                 (P2_ATTACKS_PER_REDS = 7)
```

She now prefers the AoE blood spell (projectile 1591, impact 1592): 3×3 around one
player, up to 45, **healing her for half the total damage — including damage that
Protect from Magic blocked.**

### 7.3 Phase 3 — 7 ticks (5 enraged), scan at T−1

```
phase start (8372 -> 8373 -> 8374, seq 8118 then verzik_phase3_spawn 8119)
+12   FIRST attack                       (P3_TICKS_BEFORE_FIRST_ATTACK)
then every 7 ticks (5 once enraged):

  T-1   SCAN WINDOW. checkForMeleeChance():
          tank_area adjacent to verzik_area  AND NOT intersecting it
          -> melee is on the table this attack.
          Under her (areas intersect) -> melee is OFF the table.
          Out of range -> melee is off the table.

  T     FIRE:
          melee chance taken -> seq verzik_phase3_attack_melee 8123,
              up to 63 to EVERY player adjacent to her, unblockable.
          otherwise -> ranged seq 8125 + projectile 1593, or
                       magic  seq 8124 + projectile 1594.
              Up to 34 each (17 prayed) and they hit EVERY player in the room.
  T+f   IMPACT.
```

The tank instruction *"step away from Verzik one tick before she launches her
attack"* is literally the `T−1` scan window, and *"walk under her"* is literally the
`intersectsWith` branch of the same test.

**Aggro** is sticky: she keeps one target for the phase and only re-picks if that
player is out of melee distance for more than 10 seconds (~17 ticks), the timer
resetting whenever they are back in range. Hard Mode re-picks on **every special**.

**Specials — fixed order, four autos between:**

```
CRABS -> WEBS -> YELLOWS -> BALL -> CRABS -> ...
```

| Special | Effect on the clock |
|---|---|
| CRABS | consumes the attack slot; `attacksUntilSpecial` +1 |
| WEBS | suspends the auto clock; next auto **+10** after she re-acquires |
| YELLOWS | suspends the auto clock; next auto **+7** |
| BALL | next auto is delayed by `+12 − attack_speed` |

Webs: seq `verzik_phase3_attack_webspin` **8127**, projectile 1601, npc
`verzik_web_npc` **8376** with 10 hp. She walks to the centre, becomes
**invulnerable and hard** (players are pushed out and cannot walk through her), then
throws **three webs at a time** at players not sharing a tile. The team's DPS
rotation is clockwise from the south, one attack every 5 ticks, *"wait one tick
after the XP drop and then run"* — and everyone must attack **on the same tick and
the same tile**, or a laggard's webs outlive the rotation
([07samsquanches](https://www.youtube.com/watch?v=oGPT3sZMnd8&t=408)).

Yellows: graphic **1595**, one pool per living raider (**×3 in Hard Mode**), each
protecting exactly one player. Missing costs up to 80, tick-eatable.

Green ball: projectile **1598**; 75 % of the target's Hitpoints *level*; cannot
bounce to the same player twice.

**Enrage at ≤ 20 %:** attack speed 7 → 5; one tornado (`tob_verzik_creeper` 8386,
player graphic **1602**) per player; contact costs 50 % of current hp (min 5) and
heals her **3×**; the tornado **respawns 16 ticks** after it connects.

### 7.4 Verzik spawn geometry

Exploding-crab spawn tiles, from [`VerzikCrab.java`](sources/blert_plugin/VerzikCrab.java):

| Name | Tile |
|---|---|
| NORTH | (3167, 4320) |
| NORTHEAST | (3177, 4319) |
| NORTHWEST | (3157, 4320) |
| EAST | (3176, 4315) |
| WEST | (3157, 4315) |
| SOUTH | (3166, 4308) |
| SOUTHEAST | (3179, 4310) |
| SOUTHWEST | (3157, 4311) |
| SOUTH_FAR | (3171, 4303) |
| CENTER | (3168, 4315) — *only if Verzik has been dragged* |

The room is (3154, 4302) 28 × 21, so these ring the arena with `CENTER` at Verzik's
own start position. `CENTER` existing as a named spawn is itself a mechanic: the
crabs spawn **relative to Verzik**, not at fixed arena tiles, which is why dragging
her changes where they appear.

Reds spawn **at her left and right**, not from this table.

---

## 8. Corrections to the plan

Applied to [`THEATRE_OF_BLOOD_PLAN.md`](THEATRE_OF_BLOOD_PLAN.md):

| # | Was | Now |
|---|---|---|
| 1 | Nylocas Prinkipas = npc 10803 | **10803–10806** (`nylocas_miniboss_{spawning,melee,magic,ranged}_hard`), size **3**, `attackrate` 4 |
| 2 | Maiden blood splat lifetime `[MEASURE M5]` | **11 ticks**, from `MAIDEN_BLOOD_GAME_TICK_LENGTH`. M5 now covers only the blood-*spawn* trail |
| 3 | Blood splat lands `+2` | Lands when the **projectile** lands; flight is distance-dependent |
| 4 | Bloat "stomp then rise" unquantified | Down `T`, stomp `T+29`, **rise `T+33`** — a 3-tick flinch window |
| 5 | Bloat falling flesh damage window unstated | **Exactly one tick**, the tick graphic 1576 appears |
| 6 | Verzik P2 urnbomb tile duration unstated | **Exactly one tick** |
| 7 | Verzik melee condition "in melee range" | `adjacent AND NOT overlapping` — under her is safe |
| 8 | Nylo splits "on death" | **Same tick as the death**, at six named offsets from the big's SW tile |
| 9 | Nylo lane spawns = 7 anchors | **12 tiles**, a 2×2 block per lane, two sources agreeing |
| 10 | Ver Sinhaza = region 14642 | **14386 and 14642** |
| 11 | — | Maze scene offset (9, 22) reconciles exactly with world (3273, 4310) |

## 9. What is still unmeasured

New tasks raised by this pass, continuing the plan's numbering:

| # | Room | Question |
|---|---|---|
| M23 | Maiden | Do Nylocas Matomenos **walk or run** to the boss? The published freeze timings only reproduce under one of the two. |
| M24 | Nylocas | The fixed **spawn → pillar assignment**. Known to be constant per spawn; not published anywhere in `sources/`. |
| M25 | Sotetseg | Reconcile the underworld maze origin: tob-qol scene (42, 31) vs blert world (3354, 4311, 3). |
| M26 | All | Does a step-back **click** on tick `T−1` complete in time, or must the click be on `T−2`? Decides whether the guides' "one tick before" means the click or the move. |
| M27 | Xarpus | Confirm the turn and the spit share a tick (this document's reading) rather than the turn leading by 1–4 ticks. Watch orientation and projectile-spawn ticks side by side. |
| M28 | Verzik | Do the P2/P3 scan windows use the tank's tile at end-of-`T−1`, or their tile at the *start* of `T` before movement? Equivalent in practice, different to implement. |
| M29 | Sotetseg | Death-ball projectile flight time, and which animation frame the tick-eat window opens on. |
| M30 | Maiden | Blood-splat projectile flight time as a function of distance. |
| M31 | Xarpus | Does the spit cadence vary with party size? The community trainer exposes a `Players` setting that changes attack frequency and notes that `Players = 1` "is more accurate for 5-ticking". |

**Two tasks moved from "unmeasured" to "verify the community model":**

- **M11** (Sotetseg maze generator) — an empirical generator is now recovered in
  full at §5.5. Verify it statistically rather than deriving it.
- **M5** (Maiden blood lifetime) — the *splat* is 11 ticks (§2.1). Only the blood
  *spawn trail* remains unknown.

---

*Compiled 17 August 2026 from the plugin sources and 19 guide transcripts in
[`sources/`](sources/).*
