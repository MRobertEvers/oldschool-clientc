# Projectile System

This document explains how projectiles are defined on the server, sent to the client, and rendered as parabolic arcs in 3D space. Use it as a reference when implementing new projectiles (spells, ranged attacks, cannon balls, etc.).

---

## Overview

A projectile is a `SpotAnim` that travels from a source tile to a target entity (NPC, player, or world coordinate) along a parabolic arc. The server calculates timing and sends a `MAP_PROJANIM` packet; the client (`ClientProj.ts`) handles the physics and rendering.

```
Server Script                Server Engine               Client (Client.ts / ClientProj.ts)
─────────────────────────    ───────────────────────     ──────────────────────────────────
~npc_projectile(...)    ──►  projanim_npc(...)      ──►  MAP_PROJANIM packet
                                                         │
                                                         └─► new ClientProj(...)
                                                               setTarget(...)
                                                               move() each tick
```

---

## Server-Side: Script API

Projectile helper procs live in `content/scripts/skill_combat/scripts/projectile.rs2`.

### `~npc_projectile` — projectile aimed at an NPC

```rs2
[proc,npc_projectile](
    coord    $coord,       // origin tile (usually coord or movecoord(coord, ...))
    npc_uid  $uid,         // target NPC
    spotanim $spotanim,    // visual to render
    int      $startheight, // launch height above source tile (world units / 4 on wire)
    int      $endheight,   // arrival height above target tile (world units / 4 on wire)
    int      $delay,       // client ticks before the projectile starts moving
    int      $angle,       // launch arc angle (integer, 0–127, see physics section)
    int      $length,      // base flight ticks (often negative to fine-tune close range)
    int      $offset,      // tile-fraction offset from source where projectile spawns (0–128)
    int      $step         // extra client ticks added per tile of range
)(int)                     // returns total duration (client ticks until arrival)
```

**Duration formula:**

```
flight   = length + (npc_range(coord) * step)
duration = delay + flight
```

`npc_range` returns the current distance in tiles between the caster and the target NPC.

### `~player_projectile` — projectile aimed at a player

```rs2
[proc,player_projectile](
    coord      $coord,       // origin tile
    coord      $coord2,      // destination tile (player's current tile)
    player_uid $uid,         // target player
    spotanim   $spotanim,
    int        $startheight,
    int        $endheight,
    int        $delay,
    int        $angle,
    int        $length,
    int        $offset,
    int        $step
)(int)
```

### `~coord_projectile` — projectile aimed at a fixed tile

```rs2
[proc,coord_projectile](
    coord    $coord,        // origin tile
    coord    $coord2,       // destination tile
    spotanim $spotanim,
    int      $startheight,
    int      $endheight,
    int      $delay,
    int      $angle,
    int      $length,
    int      $offset,
    int      $step
)(int)
```

Uses `distance($coord, $coord2)` for the flight calculation instead of `npc_range`.

---

## Wiring Projectiles to DB-driven Spells

For magic spells, projectile parameters are stored in the `magic_spell_table` database (`content/scripts/skill_magic/configs/magic.dbtable`):

```
column=spotanim_proj,spotanim,int,int,int,int,int,int,int
```

Order: `spotanim, startheight, endheight, delay, angle, length, offset, step`

The spell cast proc in `player_magic.rs2` fires it:

```rs2
def_int $duration = ~npc_projectile(coord, npc_uid,
    db_getfield($spell_data, magic_spell_table:spotanim_proj, 0)); // unpacks all 8 fields
```

---

## Parameter Reference

| Parameter    | RS2 name      | Wire field             | Description                                                                                                                                           |
| ------------ | ------------- | ---------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------- |
| SpotAnim     | `spotanim`    | `g2` — spotanim ID     | The graphic to render as the projectile.                                                                                                              |
| Start height | `startheight` | `g1 × 4` → world units | Height above the **source** tile's terrain. The client subtracts this from the terrain height at the source tile. Larger = higher launch point.       |
| End height   | `endheight`   | `g1 × 4` → world units | Height above the **target** tile's terrain at the point of impact.                                                                                    |
| Delay        | `delay`       | `g2` — client ticks    | Number of client ticks after the packet arrives before the projectile begins moving. Used to sync with the caster's animation.                        |
| Angle        | `angle`       | `g1` (0–127)           | Initial vertical launch angle. Converted to radians as `angle × π/128`. A larger value produces a higher, more arced trajectory.                      |
| Length       | `length`      | _(computed into `t2`)_ | Base flight time (client ticks). Often negative to compensate for point-blank range.                                                                  |
| Offset       | `offset`      | `g1` — startpos        | How far from the source tile (in world units along the source→target vector) the projectile first appears. Avoids spawning inside the caster's model. |
| Step         | `step`        | _(computed into `t2`)_ | Additional client ticks of flight per tile of distance. Controls how fast/slow the projectile crosses each tile.                                      |

### Common preset profiles

| Profile                        | startheight | endheight | delay | angle | length | offset | step |
| ------------------------------ | ----------- | --------- | ----- | ----- | ------ | ------ | ---- |
| Magic (strike/bolt/blast/wave) | 43          | 31        | 51    | 16    | -5     | 64     | 10   |
| Arrow / bolts                  | 40          | 36        | 41    | 15    | 5      | 11     | 5    |
| Thrown (knife/dart/etc.)       | 40          | 36        | 32    | 15    | 0      | 11     | 5    |
| Telegrab                       | 35          | 0         | 48    | 16    | -2     | 64     | 10   |
| Cannon ball                    | 36          | 35        | 0     | 2     | 35     | 0      | 5    |

---

## Example: Fire Strike

**DB row** (`magic_combat_spells.dbrow`):

```
[magic_spell_fire_strike]
table=magic_spell_table
data=spell,^fire_strike
data=spellcom,magic:fire_strike
data=name,Fire Strike
data=members,false
data=levelrequired,13
data=runesrequired,mindrune,1,firerune,3,airrune,2
data=experience,115
data=maxhit,8
data=anim,human_caststrike
data=staffanim,human_caststrike_staff
data=spotanim_origin,firestrike_casting
data=spotanim_proj,firestrike_travel,43,31,51,16,-5,64,10
data=spotanim_target,firestrike_impact,124
data=sound_success,fire_strike_all
data=sound_fail,fire_strike_fail
data=continue_by_autocast,true
```

**Equivalent manual call** (if firing outside the DB system):

```rs2
def_int $duration = ~npc_projectile(coord, npc_uid, firestrike_travel, 43, 31, 51, 16, -5, 64, 10);
```

**What each parameter does for Fire Strike:**

| Parameter     | Value               | Meaning                                                                                                 |
| ------------- | ------------------- | ------------------------------------------------------------------------------------------------------- |
| `spotanim`    | `firestrike_travel` | The animated fireball graphic.                                                                          |
| `startheight` | `43`                | Fireball originates 172 world units (43 × 4) above the caster's tile — roughly chest/shoulder height.   |
| `endheight`   | `31`                | Fireball impacts 124 world units (31 × 4) above the target's tile — centre-mass on most entities.       |
| `delay`       | `51`                | ~1.7 server ticks (51/30) wait before the ball launches, matching the mid-point of `human_caststrike`.  |
| `angle`       | `16`                | Launch arc of `16 × π/128 ≈ 22.5°`. Enough upward curve to look magical without flying over the target. |
| `length`      | `-5`                | Removes 5 client ticks from the base flight, keeping point-blank casts snappy.                          |
| `offset`      | `64`                | Ball appears 64 world units (~half a tile) in front of the caster, outside the player model.            |
| `step`        | `10`                | 10 client ticks of flight per tile. At 3 tiles: flight = -5 + 30 = 25 ticks, total = 76 ticks (≈2.5 s). |

**Duration example for Fire Strike at various ranges:**

| Range (tiles) | flight = -5 + (range × 10) | duration = 51 + flight | Approx. arrival |
| ------------- | -------------------------- | ---------------------- | --------------- |
| 1             | 5                          | 56 ticks               | ~1.87 s         |
| 3             | 25                         | 76 ticks               | ~2.53 s         |
| 5             | 45                         | 96 ticks               | ~3.20 s         |
| 7             | 65                         | 116 ticks              | ~3.87 s         |
| 10            | 95                         | 146 ticks              | ~4.87 s         |

_(30 client ticks = 1 server tick = ~0.6 s; 1 client tick ≈ 0.02 s)_

---

## Client-Side Physics (`ClientProj.ts`)

The client uses a parabolic arc with constant horizontal velocity and linearly-varying vertical velocity.

### Initialisation (`setTarget`)

When the projectile first becomes active (`loopCycle >= t1`):

```typescript
// Place the projectile's starting position offset along the source→dest line
const d  = Math.sqrt(dx*dx + dz*dz);         // XZ distance (world units)
this.x   = srcX + (dx * startpos) / d;       // startpos = offset param
this.z   = srcZ + (dz * startpos) / d;
this.y   = h1;                                // h1 = terrain height − startheight

// Constant XZ velocity (units/tick)
const dt = t2 + 1 - cycle;                   // remaining ticks
this.velocityX = (dstX - this.x) / dt;
this.velocityZ = (dstZ - this.z) / dt;
this.velocity  = Math.sqrt(vx² + vz²);       // XZ speed

// Initial upward Y velocity (angle in radians = angle × π/128)
this.velocityY = -this.velocity * Math.tan(angle * 0.02454369);

// Constant Y acceleration to hit the target height exactly at t2
this.accelerationY = ((dstY - this.y - velocityY * dt) * 2.0) / (dt * dt);
```

### Per-tick update (`move`)

```typescript
this.x += velocityX * delta;
this.z += velocityZ * delta;
this.y += velocityY * delta + accelerationY * 0.5 * delta * delta;
this.velocityY += accelerationY * delta;

// Yaw and pitch are recomputed from velocity so the model always faces the direction of travel
this.yaw   = atan2(velocityX, velocityZ) × (2048 / 2π);
this.pitch = atan2(velocityY, velocity)  × (2048 / 2π);
```

### Target tracking

If the target is a mobile entity (NPC or player), the client updates `dstX/Y/Z` from the entity's current position every tick so the projectile homes in even if the target moved.

For rev239, preserve the signed 24-bit `targetIndex` exactly as decoded.
Positive values resolve NPC slot `target - 1`; negative values resolve player
index `-target - 1`; zero keeps the packet's fixed destination. The GPI player
store uses that same client index. An earlier parser added one to negative
targets as if it were converting to a different pool convention, so the
per-cycle tracker never found the player and the projectile kept flying toward
the cast-time tile. `rsprot_bridge_test` pins the wire value and
`world_test_unit` moves both an NPC and a player after launch to pin the live
re-aim.

---

## Adding a New Projectile

1. **Create or reuse a SpotAnim** in `content/pack/spotanim.pack` and ensure it has an animation sequence (`.seq`) if you want the graphic to animate in flight.

2. **Choose your parameters.** Use the presets table above as a starting point:
   - Higher `angle` = more arced path (looks magical or lobbed).
   - Lower `angle` = flatter path (arrows, thrown weapons).
   - Adjust `step` to make the projectile faster (lower) or slower (higher) per tile.
   - Set `offset` to roughly half a tile (64) for spells cast by the player model, or 0 for objects like cannons.

3. **Add the `spotanim_proj` data line** to your spell's `.dbrow`:

   ```
   data=spotanim_proj,myspell_travel,43,31,51,16,-5,64,10
   ```

4. **Or call the proc directly** in your combat script:

   ```rs2
   def_int $duration = ~npc_projectile(coord, npc_uid, myspell_travel, 43, 31, 51, 16, -5, 64, 10);
   // use $duration to delay the damage hit queue
   npc_queue(2, $damage, calc($duration / 30 + 1));
   ```

5. **For location/environment-fired projectiles** (e.g. cannons, traps), use `~coord_projectile` with the source and destination coordinates explicitly.
