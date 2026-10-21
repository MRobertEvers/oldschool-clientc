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

Every Wardens kill the index holds (20 sessions, 42 kills; `rsprox_index.py
search npc 11751`). A raid's set is the last 6729 panel before its "You enter
the Tombs", whatever the panel's role (a raider who clears the panel one
invocation at a time enters with the last, empty, one: 3263); 42 of 42 agree
with what their fight shows (the charge bar, phase two's rate, the slams). The
table names the ones the write-ups cite; the rest repeat these sets.

| Raid | Level | Warden invocations |
|---|---|---|
| 14, 101 #1, 553 #1 | Entry / 150 | none |
| index 4292 | 160 | none (Tumeken's Warden in phase two) |
| 101 #2-4, 1779 | 300 / 500 / 700, 500 | Ancient Haste, Overclocked |
| 553 #2 | 150 | Ancient Haste, Acceleration, Penetration, Overclocked |
| 1845, 1852 #1-7 | 520-530 | all but Insanity |
| 553 #3, index 1737 #1 | 310, 515 | all six |

| Invocation | Recorded | Ours |
|---|---|---|
| Ancient Haste | the charge 3 a tick and 6 an orb (2 and 4 without), in every raid with it ([wardens_p1p2.md](wardens_p1p2.md)) | `^toa_wd_charge_*_haste` |
| Acceleration | the Warden every 6 (7); the obelisk open 28 / 22 (36 / 30) | `~toa_wd_rate`, `~toa_wd_ob_charge` |
| Penetration | every obelisk hit: the message, overheads off, no lock; no harder skulls or sweeps | `~toa_wd_ob_hit` |
| Overclocked | slams every 5 (7, 6 under half health) | `~toa_wd3_rate` |
| Overclocked 2 | every 4, the `02` slams | `~toa_wd3_rate` |
| Insanity | every 3; siphons released 8 after landing (18); slams resume; phantoms 7 / 6, enraged 4 / 3 (8, 5), in both raids with it | `~toa_wd3_rate`, `~toa_wd3_phantom_rate` |

The wiki is wrong on two: Overclocked is two ticks faster, not one, and
Insanity's enrage lightning is no faster. Ancient Haste is not the reference's
doubling. Unrecorded, ours as the wiki or unchanged: Tumeken's Warden under
Acceleration (7), the warning ring under Penetration (14), a beam without it
(13), Insanity's tile rows (18) and Akkha's and Kephri's phantoms under it
(their slots' 7 / 6). Test: `test/raids/toa_wardens_inv_tells.lua`.

## Kephri's five

All 20 sessions with Kephri (`search npc 11719`, 41 fights; parties from
`--all-players` transcripts):

| Invocation | Recorded | Ours |
|---|---|---|
| Lively Larvae | hatching eggs a cast: without it 2 (14: 3 casts); with it solo 3, 4, 4, 5, 5, 5, 5, 6, 6; two or three raiders 4-5 (17); five or six 5-10 (11) | `~toa_keph_hatching`; solo 3-6 was 4-6 |
| More Overlords | first stun melee + ranged, second mage + melee + ranged; without it ranged, then mage + melee, **whatever the party size** (580 and 1828: three raiders, the solo set) | `~toa_keph_guardians`, 41 of 41 |
| Blowing Mud | two raiders marked a dung strike in a party (most strikes show 2; a 1 is the other raider's graphic not sent); every party ran it | `~toa_keph_dung_mark` |
| Medic! | swarms outside the stuns only with it (1741 #2: 16, 553 #2: 6, 552 #3: 3; none in 38 fights without) | `~toa_kephri_begin` |
| Aerial Assault | the radial a 3x3 at height 0 (never without it); **the bombers' shadow and blast a 3x3 too** (6-9 shadows a bomber in 34 of 34 fights, 1 in all 11 without; raiders beside the landing hit 33, 35; the landing's graphics one tile as without it) | `~toa_keph_kamikaze` was one tile |

A blast is never drawn under Kephri or past the arena: every shadow in the 20
sessions (11,592) is 3 to 8 tiles out from her middle, so a 3x3 beside her
draws 6 (`~toa_keph_blast_tile`). 1836 #5's panel reads Aerial Assault but its
fight shows none (single-tile radials and bombers): the panel was stale for a
new party's first raid.

## Zebak's four

All 21 sessions with Zebak (`search npc 11730`, 42 fights; 4288 is the
owner's rev-241 raid):

| Invocation | Recorded | Ours |
|---|---|---|
| Not Just a Head | blood barrages only with it | as recorded |
| Arterial Spray | on in every raid with Not Just a Head, so never told apart; the barrage we model is the one with it | unchanged |
| Blood Thinners | three clouds a cast | as recorded |
| Upset Stomach | the jug's splash 1 tile out (3x3) with it, 2 (5x5) without, every fight; **the poison's hops**: with it six a landing in the box -1..+3 of it (the wiki's 5x5; 277 of 361 landings six), **without it four of the eight tiles round it** (197 of 369 four; 1,259 of 1,259 hops a tile out) | `~toa_zebak_hops` always drew the Upset Stomach box; now by the invocation |

**Path level** (Crondis): the basic attack 7 / 4 enraged at levels 0-1, 6 / 3
at 3, 5 / 2 at 4-5, and the wave's gap 3, 2, 1, i.e. a tick and a tile a
`~toa_path_tier`. Ours divided the raw level by 2 without the tier's cap;
now the tier. Jugs (5-8 a landing tick), boulders (1-3) and the enrage
(about a quarter of his bar) do not move with the level.

**Health:** every recorded Zebak base is ours to the nearest 10 (1505 shown
1510, 1276 1280, 2134 2130; 40 of 42 fights, the other two a stale mask and a
miscounted party). `~toa_boss_hp` now rounds so. Kephri's shield pool is the
same in all 41 of her fights; index 4292's 265 (raw 265.7) is the one value
not on a 10, open.

**Path levels stop at 4** in the main game; 5 and 6 are seasonal (owner).
553 #3's Leagues Zebak at level 5 has its level-5 health (1650: raw 1647, the
cap-4 number 1583). `^toa_scale_path_level_max` is 4, and toa.npc's ceilings
are re-derived at 600 / 4 / 8 (`::toarooms` OK).

**Kephri by level:** swarm triples none at levels 0-1, 3 a stun at 3, about
4 at 4 (ours every 8th / 6th); stuns 54 / 51 solo, 51 / 48 in a party, at every
level.

Test: `test/raids/toa_zebak_inv_tells.lua` (the hops both ways).

## Paths

**Path level** (Scabaras's, for Kephri: the start level plus the rumblings
for Scabaras before her): her fireball flies 120 at levels 0-1, 90 at 3, 60 at
4 (41 of 41), i.e. by `~toa_path_tier`; the radial every 6 at every level.

**Pathseeker / Pathfinder / Pathmaster** announce the start level on the way
in: one "You hear a mysterious rumbling coming from the Path of X." a path at
the fade's T+4, before the time limit's line (34 raids with Pathmaster; 553 #2
had it without Walk the Path and heard nothing more). The order is Scabaras,
Het, Crondis, Apmeken in every solo raid and every raid its recorder led (20),
and Apmeken, Crondis, Het, Scabaras in every party recording made by a member
(14); ours uses the first.

**Walk the Path** (38 raids): after the first path two **different** other
paths go up, after the second one of the two left, after the third the last
one, after the fourth none. Never a finished path. Ours could level the same
path twice, or a finished one.

Revision 241 sends the rumbling with a stray `</col>` on the end (index 4292,
4 of 4; revisions 227-235 do not); ours sends it.

Tests: `test/raids/toa_kephri_inv_tells.lua`, `test/raids/toa_paths_tells.lua`.

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
