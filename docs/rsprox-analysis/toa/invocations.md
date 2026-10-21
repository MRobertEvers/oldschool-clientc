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

## The Wardens' six

Every recorded Wardens kill, its Warden invocations from the 6729 masks (index
4292 has a rev-241 format: `id=script6729 (6729)`):

| Raid | Level | Warden invocations |
|---|---|---|
| 14, 101 #1, 553 #1 | Entry / 150 | none |
| index 4292 | 160 | none (Tumeken's Warden in phase two) |
| 101 #2-4, 1779 | 300 / 500 / 700, 500 | Ancient Haste, Overclocked |
| 553 #2 | 150 | Ancient Haste, Acceleration, Penetration, Overclocked |
| 1845, 1852 #1-7 | 520-530 | all but Insanity |
| 553 #3 | 310 | all six |

| Invocation | Recorded | Ours |
|---|---|---|
| Ancient Haste | the charge 3 a tick and 6 an orb (2 and 4 without), in every raid with it ([wardens_p1p2.md](wardens_p1p2.md)) | `^toa_wd_charge_*_haste` |
| Acceleration | the Warden every 6 (7); the obelisk open 28 / 22 (36 / 30) | `~toa_wd_rate`, `~toa_wd_ob_charge` |
| Penetration | every obelisk hit: the message, overheads off, no lock; no harder skulls or sweeps | `~toa_wd_ob_hit` |
| Overclocked | slams every 5 (7, 6 under half health) | `~toa_wd3_rate` |
| Overclocked 2 | every 4, the `02` slams | `~toa_wd3_rate` |
| Insanity | every 3; siphons released 8 after landing (18); slams resume; phantoms 7 / 6, enraged 4 / 3 (8, 5) | `~toa_wd3_rate`, `~toa_wd3_phantom_rate` |

The wiki is wrong on two: Overclocked is two ticks faster, not one, and
Insanity's enrage lightning is no faster. Ancient Haste is not the reference's
doubling. Unrecorded, ours as the wiki or unchanged: Tumeken's Warden under
Acceleration (7), the warning ring under Penetration (14), a beam without it
(13), Insanity's tile rows (18) and Akkha's and Kephri's phantoms under it
(their slots' 7 / 6). Test: `test/raids/toa_wardens_inv_tells.lua`.

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
