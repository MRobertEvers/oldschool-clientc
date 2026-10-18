# Vasa Nistirio -- solver plan (2026-10-18)

The room's v2 solver (`script/plugins/quest_driver/cox2_vasa.lua`, a `QD.T2` room like `cox2_olm.lua`) and its
test (`test/raids/cox_vasa.lua`). Mechanics, ticks and sources: `vasa_spec.md` (rows V1..V27). Nothing here is
coded yet: the room test entry (`::coxtest 10 <n>`) has only just landed.

## The strategy a strong solo human plays

From Synq (0:55:16-1:07:56), Tubby (0:13:08-0:15:53), edb0ys (0:43:15-0:48:42), Gnomonkey, PecanBread, the wiki.

1. **Before the room**: already potted (ranging + super combat / overload), ranged gear on, HP **low**: the special
   takes (HP - 5) as read at the end of the teleport, so "if you were already previously low HP from another room,
   it's actually advised not to eat up before doing Vasa" (Synq 0:56:57). Pots are drunk before the wake; nothing
   that heals is used until the teleport has landed.
2. **Wake** by walking toward the middle (within 7 of his middle). Do not stand near the entrance fire.
3. **The special** (stunned 10 ticks, prayers off): drink/eat freely from the teleport tick (T0+16) on -- it no
   longer changes the hit; be high enough after the explosion (T0+24) to take a boulder. Protection prayers are
   refused until T0+24: press Protect from Missiles on the explosion tick, not before ("Be careful not to activate
   them too early as they might just stay off").
4. **Ranging phase** (he walks 14-15 tiles to a glowing crystal, a boulder every 3 ticks): blowpipe him and step
   2 tiles after every throw -- "running two tiles after every single blowpipe hit so you never get hit by a
   boulder" (Synq 0:59:41). Drift toward the glowing crystal so the switch costs no walking. Never stand under him.
   Last-hit him with the crossbow before he reaches the crystal if it is a better hit.
5. **Crystal phase** (he is invulnerable 2 ticks after arriving; the crystal becomes hittable then): switch to the
   stab weapon, attack the crystal from the face he is NOT standing on (`vasa_spec.md` stand tiles), Piety on,
   nothing attacks you (no boulders while he siphons; never step under him). Break it before the shared 67-tick
   timer runs out; count it yourself (Synq 1:06:51: "16 hits [of a 4-tick weapon] ... Vasa will default to using
   the teleporting explosion").
6. **After a broken crystal**: back to range, and path to where he just was -- "Vasa can't go to the same crystal
   twice before its next explosion attack" (Synq 1:02:41). He pauses 3 ticks at the centre.
7. **Third crystal rule**: if the timer left is shorter than the time to break the next crystal, do not switch:
   stay ranged, position for the special and let HP sit low (edb0ys 0:48:10, Plank2g).
8. **Re-special**: the same as 3; the timer refills to 67 and any crystal may be picked.
9. **Kill**: blowpipe until dead; loot the drop (Overload (+), 2 Xeric's aid (+), 2 Twisted (+), 5 Endarkened
   juice) for the rooms after.

## Perception (`api_drive`, both lanes)

| fact | verb | how |
|---|---|---|
| Vasa's form, tile, health, anims | `npcs` | `npc_id` 7565 dormant / 7566 walking (hittable) / 7567 siphoning (invulnerable); `server_x/z` = his SW (5x5); `health_ratio`; `anim_id` (rise, special, throw, heal start/end) |
| which crystal | `locs`, `npcs` | the corner loc `raids_vasanistirio_crystal_on` (29774) and npc 7568 on it, the tick he picks it (T0+26 / pause end) |
| crystal health | `npcs` | 7568 `health_ratio/scale` |
| boulder | `projectiles` | spotanim 1329 (`raids_vasanistirio_range_travel`), `dst_x/z`, `launched`: lands 2 ticks after the tick it is first seen; the 3x3 around dst is the threat |
| special | `npcs` anim, `projectiles`, `players` | my own tile jumps at T0+16; 8x spotanim 1327 out of him at T0+19; impacts 1328 at T0+24 |
| prayer refused | `messages` | "You can't use protection prayers at the moment." |
| my HP, prayer, run | `skill`, `varbit`, `run_energy` | -- |
| the fire | `locs` | `raids_vasanistirio_fire` (30019) at the entrance |
| stomp | (derived) | my tile inside his 5x5 |

**Missing verbs (engine work, both lanes):**

- **npc hitsplat types.** Rows carry `hit_damage`/`hit_cycle` only. The cyan up-arrow store splats (hitsplat 11,
  "3" every 2 ticks) and the purple heal splat on a timeout are the human's read of the stored heal; without the
  type the solver counts siphon ticks itself (it can: the timer is deterministic), so this is not blocking.
- **npc size** is not in the row; the solver uses 5 (Vasa) and 4 (crystal) from the spec.
- Nothing else: every tell above is already a packet both lanes decode.

## Threat field (PREDICT)

| term | window | tiles | expected / max | prayer |
|---|---|---|---|---|
| boulder | lands at first-seen + 2; plan step k is hit if its tile at the END of the tick before landing is inside the 3x3 | 3x3 around `dst` | ~7 / 18 (accuracy vs my ranged defence) | PfMissiles x0.33 at landing |
| stomp | every tick | his 5x5 at the end of the previous tick (his next step predicted along the leg route) | 4 / 8 | PfMelee: 0 |
| explosion | T0+24 (G+11) | everywhere (stunned) | HP at end of G+2 minus 5 | none (prayers off) -- a RESOURCE term, not a tile term |
| fire | on Cross | the 2x2 | 22.5% of HP | none -- forbid |

Owner rule shape (ToB): standing under him is `forbid` outside the stunned window; a boulder 3x3 at landing is a
cost at its max (it is fully dodgeable, so `hp_worst` should never need it).

## Mode machine (STRATEGY)

`approach` (walk in, ranged gear, pots up, no healing) -> `wake` -> `stunned` (G..G+10: drink/eat only after
G+3; PfMissiles at G+11) -> `range` (blowpipe, dodge, drift to the lit crystal) -> `crystal` (2 ticks after his
arrival: stab weapon, open face, Piety) -> on break: `range` (path to his last stand) -> ... ; on a timer about to
run out: `pre_special` (ranged, no healing above the target HP) -> `stunned` ... -> `dead` -> `loot` -> `exit`.

Targets and worth: Vasa only in `range` (`npc_id` 7566); the crystal only while 7567 stands; a hit on a 7567 or on
a crystal before he transforms is 0 (spec V12, V20) and must never be planned.

Timer model: `siphon_left` = 67 at each special, minus one for every tick from his arrival at a crystal (the tick
his walk ends, 2 before the 7567 form) until he leaves it; he leaves on the 68th siphon tick of the cycle.

## Kit

Start from the Olm kit in `test/raids/cox_olm_solo.lua` (We Do Raids minimum: fighter torso, black d'hide chaps,
Barrows gloves, fury, fire cape, dragon defender, berserker ring (i), tentacle, toxic blowpipe with dragon darts,
dragon crossbow + ruby bolts (e), trident, CoX (+) potions: overload, Xeric's aid, revitalisation, prayer
enhance). Vasa needs beyond it:

- **a stab weapon**: the **dragon sword** -- already on the We Do Raids list, left out of the Olm inventory; the
  tentacle is slash only and the crystal's slash defence is +180 (stab -5). Keep it in the pack.
- **Piety** for the crystal phase (`varb3909_kr_knightwaves_state 8` is already in the Olm setup).
- Nothing else. The blowpipe is the ranging weapon (Vasa's light-ranged defence is +40 after Jagex's 2023 cut);
  the crossbow is for the last hit before a crystal. Healing is Xeric's aid only, as at Olm.

## Probes the first solver run must print

- **S1** every boulder: first-seen tick, dst, my tile at landing-1, hit or not (target: 0 hits outside the stun).
- **S2** each crystal: his arrival tick, the 7567 tick, my first swing tick, break tick, siphon ticks used.
- **S3** each special: G, my HP at G+2, the hit at G+11, my HP after the first heal, ticks to PfMissiles on.
- **S4** stomp ticks (target 0).
- **S5** the decision log's mode per tick (`t2 p1 t<tick> <mode> ...`).

## PASS (written before the sweep)

On 48 seeds (`::coxtest 10 <n>`): Vasa dead, the raider alive, **0 boulder damage taken outside the stun window,
0 stomp damage**, the fire never crossed, and the room done within 600 ticks of the wake. Then one live seed with an
identical tick log (manual Part 2, Phase 9).

## Round 2 (2026-10-18): the solver and its sweep

`script/plugins/quest_driver/cox2_vasa.lua` (namespace `QD.COXNI`; `t.raid.cox_vasa_solve`) and
`test/raids/cox_vasa.lua` (n = the run name's number, `::coxtest 10 n`). Kit as above; supplies 2 overload,
8 Xeric's aid, 3 revitalisation, 3 prayer enhance (all (+)).

What the solver does beyond the plan, each from a probe:

- his position is tracked ALONG his route (the leg whose span holds his tile); a plain "step toward the next
  waypoint" walked him back to legs he had passed (s48b: 49 of 215 steps mispredicted; now 710 of 12651, the
  rest the first step after a pick and his stops short of a stand);
- a seen boulder over my tile holds the swing until it lands (an attack press in reach does not move me);
- predicted throws are FORBIDDEN marks (priced, the plan stood under them for five swings' worth);
- no drift to the lit crystal before he stands on it (the nook beside it is on his way in); in the crystal
  phase a pull to the open face the client's route reaches first (`api_drive.route`), his 5x5 excluded;
- his 5x5 and one tile round it while he walks are forbidden at each tick end (two tiles, or his whole way
  ahead, left no tile in the room's nooks).

Sweep `--name vasa0..47` (scriptrun, s48c): **45/48 GREEN**. Every seed killed Vasa with the player alive, no
script abort, no "no primary player bound", and every special's explosion took exactly (hp at the teleport) - 5
(the `vasa_mech` check, 0 mismatches over 111 specials). The three red seeds are the solver's: trapped in a
dead end as he walked at me (vasa29 a stomp of 1; vasa34 a boulder of 7; vasa39 a boulder of 4 and a stomp of 2).
Typical fight: wake ~t72, 2-3 specials, dead ~300 ticks later.

Not judged by GREEN, logged: `cox_walk_on` past the exit Passage answered `stuck` on vasa0, 3, 6, 7, 14, 18 --
the Passage's far tile is the room's own last row and the next room's edge does not open there (foundation).

## Round 3 (2026-10-18)

- The way ahead is PRICED (0.5 hp a tick on every tile within 2 of where he will stand, for each tick before he
  gets there): the dead ends are left early. s48e: **47/48** (vasa23: walked onto his tile as he stopped short).
- The whole-raid contract: the solver walks into the room ahead itself when Vasa is not in sight, returns "ok"
  only once his exit crystals are gone, and with no stab weapon fights the crystals with the tentacle (the raid
  kit: GREEN on vasa0, but 6 specials and ~720 ticks against 2-3 and ~300 with the dragon sword -- the relay
  should bring the dragon sword).
- Live lane, `vasa0_p1` (`torirs_cox_vasa_opt`): GREEN, and its tick log is identical to scriptrun `vasa0`
  from the room start on (345 ticks; hits, npc animations, projectiles, locs, graphics, my tile).

## Round 4 (2026-10-18): the special, dodged

The "Vasa skip" (Synq 1:08:07, PecanBread 0:32:51, Beleti 0:27:23): the special takes only raiders in the room (or
beside the entrance fire) at its group tick; with none it is not cast and he goes straight to a crystal. The solver
wakes him and is out past the fire (op Cross, 22.5% of hitpoints each way, V6) before the group (rise + 13); and
again before every timeout (it leaves a crystal it cannot break, or with only the run to the door left on the 67),
waiting outside until the group has passed (he left his spawn after standing on it, a crystal lit, or rise + 14)
and his way no longer crosses the door. Burns cost ~0.4 x hp a dodge; the special costs hp - 5. Brew floor 35.
Restores pair with brews (Attack, Strength, Ranged 3+ under).

Room seeds 0-7: specials dodged 41/41, 0 taken; 5/8 GREEN (the three red: one stomp each, 1-7 hp).
Prep relay raid4/raid5: dodged 31/32 and 36/37, but Vasa never dies: by his room the relay is out of restores
(raid4 drank 11 brew sips and 5 restore sips there), Attack/Strength/Ranged fall to 0, the crystals stop breaking
(601 damage over 32 crystals), and every timeout heals him (~99 each, 40 of them). The room needs restores (or an
overload) kept for it.
