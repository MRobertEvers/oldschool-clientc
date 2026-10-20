# Invocations read outside one room's own rules

The invocations below were selectable and counted in the raid level, but
nothing in the raid read them until 2026-10-20. Each is now built from RSProx
where a recording shows it, and from the wiki where none does.

Which raids ran which invocations comes from the party panel's 6729 masks at
each "You enter the Tombs" (`toa_invocation.dbrow` indices).

**Ours:** `toa_raid.rs2` (time limit), `toa_nexus.rs2` (the first path's
line), `toa_rewards.rs2` (the verdict), `toa_hud.rs2` (Deadly Prayers),
`toa_supplies.rs2` (Dehydration, Overly Draining), `skill_combat/.../specwep.rs2`
(`~sa_energy_cost`), and `toa_kephri.rs2` (Lively Larvae, Medic!).

**Tests:**
- `test/raids/toa_invocations_tells.lua`: solo in Kephri's room. Dehydration,
  Overly Draining, Deadly Prayers, Lively Larvae and Medic!.
- `test/raids/toa_time_limit.lua`: Sprint for It from the lobby to the
  Wardens' death.

## Time limit (Walk / Jog / Run / Sprint for It)

Recorded in 4288, 1852, 101, 1844, 3429 and others (the 6729 masks):

| When | Line |
|---|---|
| The Nexus, on the way in: the entry fade's T+4, with the HUD (4288 t390; 1852 ×2) | `Overall time to beat: <col=ef1020>35:00</col>. The timer starts upon choosing your first path.` |
| The first path chosen (4288 t403, the leader; 1852, a member, after "… has chosen to walk …") | `Overall time to beat: <col=ef1020>35:00</col>. The timer has started!` |
| The Wardens' death, after the completion count (60 kills) | `<col=229628>Your party beat the overall target time of 35:00!</col>` |
| The same, over the limit (1844, s1828) | `<col=ff3045>Your party failed to beat the overall target time of 35:00.</col>` |

- **The clock.** The time follows the raider's timer format: `35:00.00`
  with Precise timing on, `35:00` without. The verdict always shows `m:ss`.
- **What is compared.** The raid's *total completion time*. In 1844 the
  challenge time was 34:19.80, under the limit, but the total was 41:37.20,
  over it, and the line was "failed".
- **Failing is not a kick.** On the death tick the raid level drops by twice
  the invocation's own level (the wiki's 20 / 30 / 40 / 50). In 1844 varbit
  14380 went 520 → 490 for Jog for It. The loot rolls at the lower level.

## Deadly Prayers

"Prayer is drained by 20% of damage taken" [wiki]. In 1852 the drop is the
floor of a fifth of each hit, on the hit's own tick:

| Hit | Prayer lost |
|---|---|
| 10 | 2 |
| 12 | 2 |
| 15 | 3 |
| 17 | 3 |
| 20 | 4 |
| 23 | 4 |

Ours applies it in `~toa_damage_taken_add`, which every red splat on a raider
passes through.

## Dehydration

A drink that restores health (nectar, ambrosia) is refused with
`You've been prevented from consuming potions within the Tombs of Amascut.`

Recorded: 3431 (ambrosia, 7 of 7) and 553's second raid (1 of 1). In 553's
third raid, without Dehydration, nectar was drunk. Tears, liquid adrenaline
and the non-potions are allowed.

## Overly Draining

Every special attack costs the whole bar; liquid adrenaline halves that to 50%
[wiki]. Recorded drops of 1000 in 1751, 1757 and s580. Ours applies it in
`~sa_energy_cost`, which covers both the affordability check and the spend.

## Lively Larvae

Hatching eggs (`projectile_dungball01`) per Mass Incubation cast:

| Raiders | Lively Larvae | Hatching | Casts |
|---|---|---|---|
| 1 | off | 2 | 3 (14, s45) |
| 1 | on | 4 / 5 / 6 | 2 / 8 / 4 |
| 2–3 | on | 4 / 5 | 10 / 8 |
| 5–6 | on | 5–10 | 9 |

Every party recording ran Lively Larvae. The doc's old "4 for two or three
raiders" was the invocation, not the party size. Without it, ours throws the
wiki's 2 at any party size.

## Medic!

Swarms (`toa_kephri_shield_scarab`) outside the stuns. Recorded in 1741 (one
kill), and in 554 and 553 (after their second wake).

| Phase | First | Gap |
|---|---|---|
| Before the first break | S+18 | 12 |
| After the first wake | 15–18 ticks in | 10 |
| After the second wake, and enraged | 15–18 ticks in | 8 |

- **Movement.** They crawl: a tile every 2 ticks from 3 after rising (RSProx
  `crawl`).
- **Absorbed** 14 ticks after rising, and removed 3 later. A stun swarm walks
  a tile a tick and is absorbed at +10.
- **Heal.** Each absorb heals about 11% of the pool she is on: the shield's
  while shielded (+61, +62 on the overlay), the body's while enraged (+35,
  +33).

## Not built: Arterial Spray

Every recorded blood barrage (79 of 79) had Arterial Spray on, so the barrage
we model is already that one. Nothing records Not Just a Head without it.

The wiki gives only "the radius and healing of Zebak's blood magic will be
increased", "players must spread apart for this attack, or it will deal
increased damage" and "Zebak will heal for 2x the damage inflicted". It gives
no radius and no base heal.

## Open

- **Quiet Prayers** reads the wiki's "10% less effective" as half of the
  blocked share: a full block lets 50% through. Not checked against
  recordings (1838, 1844, 553 ran it).
