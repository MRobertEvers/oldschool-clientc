# Charged (powered) staves: projectiles and attack speed

Written 2026-08-14. Companion to `docs/WEAPON_FX.md` (which covers melee/ranged
swing animations) and `docs/ITEM_CHARGES.md` (which covers where each staff's
charges live).

A **powered staff** is a weapon that casts its own baked-in spell on every
attack — no spellbook selection, no autocast toggle, no `magic_spell_table`
row. In this tree they all run through one dispatch,
`skill_combat/scripts/player/gear/powered_staff.rs2`, reached from an early
exit in `combat.rs2`'s `[label,player_combat_start]`.

## The defect this table exists to close

Until 2026-08-14 the shared attack label did this:

```
anim(%com_attackanim, 0);          // the weapon's MELEE swing param
...
npc_queue(2, $damage_prepared, 1); // hitsplat one tick later, at any range
```

No projectile, no cast graphic, no impact graphic, and a landing time that
ignored distance. Every powered staff in the game bashed its target with a
stick. Nothing errored and nothing logged, because `%com_attackanim` is a
populated param — the same failure shape `docs/WEAPON_FX.md` describes, where a
param with a plausible default turns a missing port into a wrong answer with
nothing to grep for.

Four families were worse off than that: the tridents, Thammaron's sceptre, the
accursed sceptre and the bone staff were not in the dispatch's membership test
at all, so wielding one fell through to `@player_melee_attack` — melee reach,
melee roll, and no charge ever spent.

## Every charged staff in `cache.osrs239`

Speeds and max hits are the OSRS wiki's, checked one at a time; the cache's own
`attackrate` param agrees with every one of them. `~sa_attackrate` in
`specwep.rs2` is the one place a special attack overrides its weapon's rate.

| Staff | obj (charged) | Speed | Max hit | Cast anim | Cast gfx | **Projectile** | Impact gfx |
|---|---|---|---|---|---|---|---|
| Trident of the seas (+ full, `(e)`) | `tots`, `tots_charged`, `tots_i_charged` | 4 | ⌊Mag/3⌋−5 | `human_castwave_staff` (1167) | `slayer_tots_casting` (1251) | **`slayer_tots_projectile` (1252)** | `slayer_tots_impact` (1253) |
| Trident of the seas `(o)` — Leagues | `tots_orn`, `tots_charged_orn`, `tots_i_charged_orn` | 4 | ⌊Mag/3⌋−5 | 1167 | `slayer_tots_casting_orn_leagues6` (3721) | **`slayer_tots_projectile_orn_leagues6` (3723)** | `slayer_tots_impact_orn_leagues6` (3725) |
| Trident of the swamp (+ `(e)`) | `toxic_tots_charged`, `toxic_tots_i_charged` | 4 | ⌊Mag/3⌋−2 | 1167 | `toxic_tots_casting` (665) | **`toxic_tots_projectile` (1040)** | `toxic_tots_impact` (1042) |
| Trident of the swamp `(o)` — Leagues | `toxic_tots_charged_orn`, `toxic_tots_i_charged_orn` | 4 | ⌊Mag/3⌋−2 | 1167 | `toxic_tots_casting_orn_leagues6` (3722) | **`toxic_tots_projectile_orn_leagues6` (3724)** | `toxic_tots_impact_orn_leagues6` (3726) |
| Sanguinesti staff | `sanguinesti_staff` | 4 | ⌊Mag/3⌋ (min 6) | 1167 | `sanguinesti_staff_casting` (1540) | **`sanguinesti_staff_travel` (1539)** | `sanguinesti_staff_impact` (1541) |
| Holy sanguinesti staff | `sanguinesti_staff_or` | 4 | same as base | 1167 | `sanguinesti_staff_casting_justiciar` (1900) | **`sanguinesti_staff_travel_justiciar` (1899)** | `sanguinesti_staff_impact_justiciar` (1901) |
| Tumeken's shadow (+ Corrupted) | `tumekens_shadow`, `deadman_blighted_tumekens_shadow` | **5** | ⌊Mag/3⌋+1 | `toa_sot_cast_b` (9493) | `tumekens_shadow_casting` (2125) | **`tumekens_shadow_travel` (2126)** | `tumekens_shadow_impact` (2127) |
| Warped sceptre | `warped_sceptre` | 4 | ⌊(8·Mag+96)/37⌋ | `pog_warped_sceptre_attack` (10501) | `vfx_warped_sceptre_cast` (2567) | **`vfx_warped_sceptre_projectile_projectile` (2569)** | `vfx_warped_sceptre_projectile_impact` (2568) |
| Eye of ayak | `eye_of_ayak` | **3** (spec: **5**) | ⌊Mag/3⌋−6 | `human_eye_of_ayak_normal` (12397) | `vfx_ayak_player_normal_spotanim` (3366) | **`vfx_ayak_normal_projectile` (3367)** | `vfx_ayak_normal_impact` (3368) |
| Thammaron's sceptre | `wild_cave_sceptre_charged` | 4 | ⌊Mag/3⌋−8 | 1167 | *(none in cache)* | **`spells_thammaron01_travel01` (2340)** | *(none in cache)* |
| Accursed sceptre | `wild_cave_accursed_charged` | 4 | ⌊Mag/3⌋−6 | 1167 | *(none in cache)* | **`spells_vetion01_travel` (2337)** | *(none in cache)* |
| Bone staff | `rat_bone_staff` | 4 | ⌊Mag/3⌋+5 (+10 vs rats) | 1167 | `vfx_weapon_staff_rat01_spotanim` (2645) | **`spells_ratbone01_travel01` (2647)** | `vfx_weapon_staff_rat01_impact_01` (2646) |

Notes on the entries that look like mistakes and are not:

* **Thammaron's / accursed sceptre are projectile-only.** No `*_casting` or
  `*_impact` sibling of those two spotanims exists in the cache under any name.
  They are drawn as a bolt with no muzzle flash and no splash.
* **Holy sanguinesti staff uses the `*_justiciar` set.** Those four spotanims
  are recolours of the base staff's own four models, they are the only
  recoloured Sanguinesti FX set in the cache, and `sanguinesti_staff_or` is the
  only recoloured Sanguinesti staff obj — a 1:1 pairing. `justiciar` is the
  cache's internal name for the holy (Saradomin) kit.
* **`(a)` sceptres are not powered staves.** "Thammaron's sceptre (a)" and
  "Accursed sceptre (a)" are the *autocast* variants — they cast spellbook
  spells and are deliberately not on this dispatch. (Their records are also
  missing `weapon_attackrange` in this export, which is worth a look for
  whoever wires autocasting on them.)
* **Not charged, so not in scope here**, but powered staves all the same, and
  none of them is on the dispatch yet: Crystal staff (basic/attuned/perfected,
  `gauntlet_magic_t1..t3`), Corrupted staff (`gauntlet_magic_t*_hm`),
  Dawnbringer (`verzik_special_weapon`), Starter staff
  (`deadman_starter_staff` / `deadman_apocalypse_staff`). Their FX names —
  `crystal_staff_casting`/`_projectile`/`_impact` (1719/1720/1721), the `_hm`
  set (1722/1723/1724), `dawnbringer_casting`/`_projectile`/`_impact`
  (1543/1544/1545), `starterspell_casting`/`_travel`/`_impact`
  (1521/1522/1523) — all resolve in `configs/all.spotanim` already.

## How an id gets into that table

Name-mediated in three hops, never a bare integer:

1. RuneLite `runelite-api/.../gameval/SpotanimID.java` and `AnimationID.java`
   give the numeric id a name.
2. That name must exist in `OSRS-Content/osrs239-content/configs/all.spotanim`
   or `all.seq` — `sscompile` fails the build if it does not, which is the
   check that catches a typo (verified by feeding it a deliberately bogus name).
3. Each pairing was cross-checked against an independent server's own table
   where one exists. `Valius .../combat/magic/MagicData.java` states
   `{ -1, 75, 1167, 1251, 1252, 1253, ... } // trident of the seas` and
   `{ -1, 75, 1167, 665, 1040, 1042, ... } // trident of the swamp`, which
   agree with the gameval names above.

## Flight geometry, and why it is also the timing

All powered staves share one projectile geometry, in
`gear/powered_staff.constant`, and it is the same seven numbers this tree's own
wave spells carry in `magic_combat_spells.dbrow`
(`43,31,51,16,-5,64,10` → start height, end height, delay, angle, length,
offset, step). `~npc_projectile` turns them into
`duration = delay + (length + range × step)` in client ticks, and that duration
is what times the hitsplat, the impact spotanim and the target's flinch — so a
staff at nine tiles now lands later than one at two, the same rule cast spells
already followed.

## Tests

* `::chargesrun` (runs unconditionally in `ToriRSServer --selftest`) —
  `~chargesrun_powered_staff_attack_rates` asserts every staff's `attackrate`
  and `~sa_attackrate(eye_of_ayak) = 5`;
  `~chargesrun_powered_staff_projectiles` asserts the FX table entry by entry
  and then **sweeps every id `~powered_staff_is` accepts**, failing any that has
  no projectile, no cast animation or no max-hit formula;
  `~chargesrun_powered_staff_maxhits` boosts Magic to 99 and checks all nine
  formulas against the wiki's own numbers (the clamps make them
  indistinguishable at the harness player's own level);
  `~chargesrun_trident_charge_and_cast`, `~chargesrun_bone_staff_charge_and_bonus`
  and `~chargesrun_sceptre_powered_staff_reserve` cover the charge models.
* `ToriRSServer --selftest`, stanza *"a charged staff casts a projectile"* — the
  other half, from the wire rather than from content's own answer: a real fight
  at six tiles, and the assertions are that a `MAP_PROJANIM` carrying spotanim
  1252 reaches the client, that the player never closes to melee, that the
  tradeable full trident swaps to the partially-charged id carrying one charge
  fewer per cast, and that the gap between two consecutive `%action_delay`
  stamps is exactly 4 ticks. That last one matters because `attackrate`
  *declares* `default=4`, so a check that only read the param would pass on a
  re-export that dropped it.
