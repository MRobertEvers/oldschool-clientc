# The solo Olm solver (v2 layers)

File: `script/plugins/quest_driver/cox2_olm.lua`, a `QD.T2` room (`cox_olm`) on
the ToB v2 stack (TOB2_DESIGN.md): PREDICT and STRATEGY here; PLAN (`aplan`),
EXECUTE, MONITOR and the resource layer shared. Test: `test/raids/cox_olm_solo.lua`
(and `test/raids/cox_olm_douse.lua` for the fire wall's douse).
Mechanics and every number: `OLM_SPEC.md`. Method: `docs/CONTENT_SOLVER_MANUAL.md`.

The solver exists to check the content: every mechanic it meets is checked as it
lands (CONTENT CHECKS below), and a mismatch names the mechanic.

## The kit

Gear: We Do Raids' minimum CoX list (wedoraids.com/raid-requirements); the
slots it leaves open and the head phase from the wiki's "Minimum" setup
(Chambers_of_Xeric/Strategies). No void.

| | |
|---|---|
| worn | helm of neitiznot, fighter torso, black d'hide chaps, Barrows gloves, abyssal tentacle, dragon defender, amulet of fury, fire cape, dragon boots, berserker ring (i), ruby dragon bolts (e) |
| mage hand | trident of the swamp, occult necklace, imbued Saradomin cape (defender, torso) |
| head | dragon crossbow (rapid: 5 ticks, the head's 4:1), Ava's assembler, black d'hide body (fury, defender) |
| carried | toxic blowpipe, bandos godsword (the list's; not used on Olm) |
| supplies | the raid's (+) potions only -- overload x2, Xeric's aid x7, revitalisation x5, prayer enhance x4 -- one stamina, a rune pouch of Water Strike |

## Perception (only what a client is sent)

| fact | from |
|---|---|
| the chamber origin and wall | `locs`: the head-tile locs (olm_head*, at local x 4 and 22, z 34) |
| the head's facing, each action and its tick | `loc_anims` on the head loc: turn -> new facing, attack -> the step fired, idle -> facing; spawn -> a rise (glowing = the penultimate); death -> a fall |
| which special | `loc_anims` on the left claw: crystal / lightning / swirl / infinity signs; clench |
| the hands, the head | `npcs` rows; the last hitsplat (a landed hit steers the head; my hit rate per hand) |
| the auto's style | `projectiles` 1339 magic / 1340 ranged |
| spheres | `projectiles` 1341/1343/1345 |
| hazards | `locs` acid pools, seedlings, bombs, bursts; `spotanims` lightning, portal, shadows -- a FRESH send is a tile's largest `cycles_left` rising (both lanes; scriptrun keeps a floor graphic 40 ticks, the client its sequence); `npcs` olm_firewall_npc |
| chat | the phase's power; "smothered you in acid" (the drip); "chosen you as its target" (falling crystals) |
| my overload | its dose count falling (the timer varp is not transmitted) |

The rotation is counted, as a player counts it, from the rise (P1 + 4, later
+ 8), and RE-ANCHORED on the first action seen off the count (a turn or an
attack; the step counted on the wrong tick is undone). The offset is a content
check (`clock`).

## PREDICT: the threat field

| hazard | term |
|---|---|
| acid pool | its tile, every tick it stands (forbid) |
| acid drip | a mark: the tile I end tick k-1 on holds a pool from k, 16 ticks (forbid) |
| seedling | its tile at first sight + 2 (forbid); the sight kept to the burst even when a pool replaces the loc |
| lightning | each bolt's tile at each future tick (forbid) |
| portal | rings of min(50, 5 a tile) out to the chamber's far corner at first sight + 7 |
| crystal bomb | rings at first sight + 5 |
| fire wall | the row between the walls at the flames' first sight + 9 (57-65, forbid); the flames themselves (forbid: they block) |
| falling crystals | a mark: never end two ticks on one tile |
| shadows | 3x3 at the landing |
| my overload | 10 a hit, unprayable, at the drink + 1, 3, 5, 7, 9 |

## STRATEGY: the fight as a state machine

`CO.machine`: wait -> mage -> melee -> interim -> (P2) -> interim -> p3-low-melee
-> p3-low-mage -> p3-kill-melee -> p3-kill-mage -> head. Every change is traced
with its cause. A hand never hit shows no health bar: unknown is FULL.

Under it:

- **the 4:1 (`CO.cycle`)**, the melee hand: CYCLE plays the guides' tiles (ring
  finger z 42 for the special, the first auto and the null -- not tile 3, which
  noodles on a 0 -- the mid skip z 35 for the second auto; east z -> 72 - z),
  one swing a step, on the step + 1. In a flame phase the first auto's read is
  the flame-skip row (z 43 west, 28 east), where no wall fits. RECOVER the moment
  a step leaves the loop's state, with its cause (special, auto, turn, portal),
  planned by the step search scored for ending in the loop's state.
- **the step search (`CO.search`)**, the mage hand and the head: an expectimax
  DP over the next four reads' view bins, a swing per window when the target is
  in reach in time, chance on whether it lands (the measured hit rate).
- **trapped (`CO.douse`)**: between two walls, Water Strike the segment toward
  where I am going; the cast outranks the prayer panel.
- **prayers**: protect only from the tick before an action that fires at me
  until it lands (the content reads it at landing); an overhead replaces the
  lit one by itself (never pressed "off" as well); the boost while there is
  something to hit.
- **supplies**: overloaded the whole fight -- drunk again the tick it ends (+525,
  when the 50 comes back), never while it runs; a fighting stat under its base
  takes a revitalisation; the brew floor is a machine (steady / draining: the
  floor plus the overload damage still owed); prayer enhance, the stamina.

## CONTENT CHECKS (monitor rules)

`olm_step` (every action against the rotation model), `olm_mech` (`clock`,
`portal`, `burst`, `one_head`), `olm_stats` (overloaded and undrained, every
tick with something to hit), `threat` (hp lost above the field's max).

## PASS

The raid finished, on 48 seeds (`--name ol1..ol48`): the head dead with the
player alive, the exit crystal gone, the Ancient chest searched and its loot
banked; every content check clean; the 4:1's tell (specials skipped per state)
reported. Then one live seed with an identical tick log (run the live lane as
`--name ol1_p1`: the account and run seed scriptrun's bot uses; ol1_p1
2026-10-13: identical but for absolute-clock varps); then crops of every
mechanic (`test/raids/cox_olm_mechanics.lua`, `::coxolmdo <n>`).
