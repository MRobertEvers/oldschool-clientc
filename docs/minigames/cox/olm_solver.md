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
| the hands, the head | `npcs` rows; my swings (the monitor's) on the hand I aimed at, each resolved by the next hitsplat on it (a splat running out reads cycle 0: not a hit); a hitsplat at health 0 is a kill |
| the auto's style | `projectiles` 1339 magic / 1340 ranged |
| spheres | `projectiles` 1341/1343/1345 |
| hazards | `locs` acid pools, seedlings, bombs, bursts; `spotanims` lightning, portal, shadows -- a FRESH send is a tile's largest `cycles_left` rising (both lanes; scriptrun keeps a floor graphic 40 ticks, the client its sequence); `npcs` olm_firewall_npc |
| chat | the phase's power; "smothered you in acid" (the drip); "chosen you as its target" (falling crystals) |
| my overload | its dose count falling (the timer varp is not transmitted) |

The rotation is counted, as a player counts it, from the rise (P1 + 4, later
+ 8), and RE-ANCHORED on the first action seen off the count (a turn or an
attack; the step counted on the wrong tick is undone). The offset is a content
check (`clock`).

The count is the content's (`~cox_olm_act`, Molgoatkirby's counting guide):
autos on the odd steps; the even steps walk their own list (null, burst, null,
lightning, null, teleport [+ null, heal in P3]) and a CLENCHED even step is a
clenched null that leaves the list where it is. The clench is predicted at
every even step from the heaviest non-killing hit LAUNCHED at the melee hand
since the last even step (>= 30), and its 45 ticks counted from its anim; the
ghost clench from the mage hand's death anim (the next tick on the rise's
2-tick grid). A hand's damage steers the step after its LAUNCH (the attack
rolls as it goes out; the trident's splat lands 3-4 ticks later): a step whose
steering launch is still in the air is judged when it lands.

## PREDICT: the threat field

| hazard | term |
|---|---|
| acid pool | its tile, every tick it stands (forbid) |
| acid drip | a mark: the tile I end tick k-1 on holds a pool from k, 16 ticks (forbid) |
| seedling | its tile at first sight + 2 (forbid); the sight kept to the burst even when a pool replaces the loc |
| lightning | each bolt's tile at each future tick, to the end of its 17 (forbid); its direction carried per column from the tile it was on before, at first sight from the end nearer |
| portal | rings of min(50, 5 a tile) out to the chamber's far corner at first sight + 7, and a required (lethal) zone on the tile itself then, so the plan's lookahead runs to it |
| crystal bomb | rings at first sight + 5 |
| fire wall | the walls judge where I END the cast step (they stand at C + 1), so the dodge is the step tick's own run: before a seen auto in a flame phase, P3 or the final stand, on a tile a wall fits (both rows in the head's view, z 29..42), ending the step tick within a tile of the read tile is priced (a mark, 6); once the flames stand (C + 1): the row between at their first sight + 9 (57-65, forbid), the flames (forbid, pass). The cast's own projectile flies 15 cycles and is not used (not seen alike on both lanes) |
| falling crystals | a mark: never end a tick on the tile I ended two ticks before (aimed at A on the end of A - 1, landing at A + 2 on the end of A + 1), cry + 13 |
| shadows | 3x3 at the landing (forbid: 10-16 beside, 16-25 under) |
| my overload | 10 a hit, unprayable, at the drink + 1, 3, 5, 7, 9 |

## STRATEGY: the fight as a state machine

`CO.machine`: wait -> melee <-> mage -> interim -> (P2) -> interim ->
p3-low-melee -> p3-low-mage -> p3-kill-melee -> p3-kill-mage -> head. Every
change is traced with its cause. A hand never hit shows no health bar: unknown
is FULL.

- **melee first** (P1/P2): the melee hand by the 4:1 while it is up and not
  clenched. **mage** only while the melee hand is CLENCHED (invulnerable: its 45
  ticks go to the mage hand) or dead; from the tick the coming even step is
  known to clench (a hit of 30 launched since the last one, its splat up) when
  it is 3 ticks off or less; back to the 4:1 4 ticks before the clench ends
  (no melee swing window before it ends). The mage hand's death ends a clench
  on the ghost tick, and the specials slip a step: the 4:1 recovers.
- **the 4:1 (`CO.cycle`)**, the melee hand (Synq's: the first auto at the ring
  finger z 42, the mid skip z 35 touched for the second auto's read, back to
  the ring finger to skip the special; east z -> 72 - z): one swing a step, on
  the step + 1. The slot (0 the auto before a null, 1 the null, 2 the auto
  before a special, 3 the special) comes from the even list, not a counter. In
  a flame phase the first auto's read is the flame-skip row (z 43 west, 28
  east), where no wall fits. RECOVER the moment a step leaves the loop's state,
  with its cause, planned by the step search scored for ending in the loop.
- **head running (`CO.headrun`)**, the mage hand, P1-P3 (Synq 2:18:34): unseen
  at EVERY read, so every step is a skip and a turn. The read tile by the
  facing it finds: the melee side's view -> the mid skip (z 35; the cast since
  the last step steers it to the mage side, a splash to the middle); the mage
  side's -> between (z 38) -> middle; the middle -> the ring finger (z 42) ->
  the melee side. When the first is out of reach (a splash turned the head
  middle from the mid skip) the mage side's long line (z 30). A cast fits a
  window only where the run to the next read still does after it (a cast
  stands; from the ring finger it drags two tiles in); each read is a required
  zone over its bin's rows (30), so the beam keeps a run that pays only at it.
- **the last phase (P3)**: its own states, unchanged: both hands to 40, then
  the melee hand dead, then the mage hand inside the 50-tick regen (the melee
  states 4:1, the mage states head-run); then the head.
- **the step search (`CO.search`)**, the head: an expectimax DP over the next
  four reads' view bins, a swing per window when the target is in reach in
  time, chance on whether it lands (the measured hit rate).
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

`olm_step` (every turn against the model's, judged once the launches
steering it have landed), `olm_mech` (`clock`, `portal`, `burst`, `one_head`;
`special`: the claw's sign exactly when the even list and the view say a
special fires; `auto`: the head's attack anim exactly when the step owes me an
auto; `clench`: the clench anim exactly when a non-killing hit of 30 went out
since the last even step; `unclench`: 45 ticks on; `ghost`: the melee hand's
unclench on the poll after the mage hand dies; `wall`: the flames the tick
after a step the head attacked on; `points`), `olm_stats` (overloaded and
undrained, every tick with something to hit), `threat` (hp lost above the
field's max).

The tells (the summary's `tells`): per state, steps, autos skipped / owed,
specials skipped / due, head runs (a step the head turned on with nothing
fired), clenched nulls.

## PASS

The raid finished, on 48 seeds (`--name ol1..ol48`): the head dead with the
player alive, the exit crystal gone, the Ancient chest searched and its loot
banked; every content check clean; the 4:1's tell (specials skipped per state)
reported. Then one live seed with an identical tick log (run the live lane as
`--name ol1_p1`: the account and run seed scriptrun's bot uses; ol1_p1
2026-10-13: identical but for absolute-clock varps); then crops of every
mechanic (`test/raids/cox_olm_mechanics.lua`, `::coxolmdo <n>`).
