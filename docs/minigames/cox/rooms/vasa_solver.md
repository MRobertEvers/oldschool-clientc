# Vasa Nistirio: solver plan (v2, 2026-10-18)

The room's solver is `script/plugins/quest_driver/cox2_vasa.lua`, a `QD.T2` room shaped like `cox2_tekton.lua`
and `cox2_olm.lua`. Its test is `test/raids/cox_vasa.lua`. The mechanics are in `vasa_spec.md`, refined by the
recordings in `docs/rsprox-analysis/cox/vasa.md` (rows `RV-*`). This plan replaces the first solver, which was
written against Near-Reality's tick table and route tables and played badly (the owner, 2026-10-18).

## The pass criterion (the owner, 2026-10-18)

The special is **taken**, as every recorded solo raider takes it, and the raider eats up after it. GREEN means all
of these, read from the server's tick log:

- Vasa is dead;
- the exit crystals are gone, and the way on is taken;
- the raider is alive;
- no boulder damage;
- no stomp damage.

The explosion is reported (how many, and the largest) but not judged.

## A party

The same test runs a party: `--bots 2..5`, the same command. The leader builds the raid with `::coxtest`. Each member
then joins it on the leader's tile with `::coxtestjoin` (a test path: real raid parties, `raids_party_recruitment`,
are not implemented). Every seat runs this solver, and the tick-log counts are every raider's, so a party is green
only if no one takes a boulder or a stomp.

The content counts the raid's raiders (`^cox_var_party_n`, `~cox_party_size_at`) and scales Vasa's pool to them
when he wakes (`~cox_scale_hp`: 300 per raider at full combat). Every form change keeps that pool
(`~cox_vasa_retype`), because `npc_changetype` resets it to the record's 300. The special groups every raider in
the room, and splits the bank over them. Boulders go to every raider each volley.

2026-10-18 sweeps:

| Party | Green | Notes |
|---|---|---|
| Solo | 8/8 | seeds 1-8 |
| Duo | 15/16 | seed 10: one 5 hp boulder; pool 600 through 9 form changes |
| Trio | 7/8 | seed 3: one 1 hp stomp; pool 900 |
| Five | 4/4 | pool 1500 |

Not scaled for a party yet: the crystal's hitpoints, and Vasa's defence and other stats (`~cox_scale_defence`,
`~cox_scale_stat`).

## The strategy

A strong solo human (Synq 0:55:16-1:07:56, Tubby, edb0ys; the wiki), with the room as recorded:

1. **Wake** him: walk to within 7 tiles of his middle. The fire goes up at T0+4 and the special starts at
   T0+5 (S).
2. **The special.** Nothing that heals is used from S to S+3: the bank reads HP at the teleport. From S+3 to S+11
   I am stunned and my clicks do nothing. At S+12 the explosion takes HP - 5. After that I brew back up to the
   floor.
3. **Ranging** (he walks, about 13 ticks to the lit crystal, and walks back after it breaks): blowpipe him.
   - **Boulders:** one comes at my tile every 3 ticks. The throw on T reads my tile at the end of T-1 and lands
     a 3x3 on T+2, reading the end of T+1. Two tiles of running clear it.
   - **Never under his 5x5.**
   - **Drift toward the lit crystal**, so the switch costs no walking.
   - **Prayer:** Protect from Missiles, and Rigour when points allow.
4. **The crystal** (he turns into the healing form 2 ticks after he stops; he is invulnerable and throws
   nothing):
   - the stab weapon onto the crystal (dragon sword; the raid kit without one uses the tentacle);
   - attack from a face he is not standing on;
   - Piety on, protection off.
   - Breaking it denies his heal. He walks back, pauses, and picks another crystal.
5. **Re-special:** the 67 siphon ticks run out, he heals what he stored and walks to the centre for the special.
   Handled as in step 2.
6. **Done:** his death is seen, then the exit crystals go.

## Perception (only what the client is sent)

| Fact | Verb | How |
|---|---|---|
| Vasa | `F.npcs` | the rows with ids 7565 (rocks), 7566 (walking) and 7567 (healing); his server tile is his 5x5 south-west |
| His events | npc row `seq_id`, `seq_tick` | each keyed on the tick it is **first seen**: `vasa_spawn` (T0), `vasa_stun_spawn` (S), `vasa_attack`, `vasa_heal_spawn`, `vasa_healed`, `vasa_death` |
| The lit crystal | `F.npcs` | the row with id 7568, a 4x4 |
| Boulders | `api_drive.projectiles` | spotanim `raids_vasanistirio_range_travel`, keyed on destination and first-seen tick |
| The exit crystals | `api_drive.locs` | `raids_blockage_purple_small` in the room |
| Me | `F.me`, `F.hp`, `F.prayer`, `F.lit` | |

No route or crystal tables: everything comes from the rows. The recorded room's geometry is still open
(`RV-G1`), and nothing the solver does depends on it.

## The threat field (predict), in hp

| Term | Window | Expected / max | Kind |
|---|---|---|---|
| A boulder in flight | 3x3 on its destination, read at the end of first-seen + 1 | 7 / 18 (x0.33 prayed) | forbid |
| The next boulders | marks: my tile at the end of T-1 for each throw T while he walks, every 3 ticks from the last seen; 3x3, read at the end of T+1 | 7 / 18 | forbid |
| Stomp | his footprint on his dead-reckoned walk (his last step, repeated until he touches the lit crystal), with the tick before | 4 / 8 | forbid |
| Beside him | a tile's ring round that footprint, while he can walk (a turn at a leg's corner swings him a tile sideways) | 2 | cost |
| His lane | walking to a lit crystal: the box from his footprint to the crystal, grown by a tile | 3 | cost |
| His line | the same, within 3.5 of the line from his middle to the crystal's | 3 | cost |

Why the costs, not rules: the crystals stand in alcoves at the ends of lanes his own width. Ahead of him there, the
only way out is through him, so the planner must never wait there. As rules, the grown footprint walled the planner in,
and it took the stomp instead (sweep s8a, 47 stomps). Behind him is always safe: he does not turn back until the
crystal breaks or the siphon ends.

**His next step, both ways.** He steps before my click is handled, and the press's reach is read against where he
stands then. Guessing either way put me on a rock: assumed standing, the press walked me in; assumed stepping, he
stopped and the shot fired where I stood. So the target's path is his next two dead-reckoned steps, and its
alternative (`path_alt`) is him standing still. The planner keeps the worse of the two, so it plans an attack only
where it is safe whichever he does. That took the 40-seed sweep from 36 to **39/40**, with no boulder damage on any
seed and the kill sooner (mean death t453 against t505).

What did not work: his whole dead-reckoned walk as the path, which runs into walls (20/40); range 4 when his next
step leaves 5 (30/40).

**Open:** seed 15. His leg after a special runs two rows past the crystal before the last turn, and one stomp catches
me waiting there. Growing the lane box by 2 fixes it, but loses three other seeds (37/40).

**Synq's solo guide** (`docs/minigames/cox/synq_transcript.md`, 0:55:16-1:07:56), against the solver:

| Synq | The solver |
|---|---|
| Ranged gear on, walk to the centre to wake him | the `wake` pull |
| The bank reads HP at the teleport; brew after it, never before | no healing S..S+3, the brew floor after |
| Prayers stay off if clicked while stunned | none until S+12 |
| Run two tiles after every pipe hit; protect from missiles | the boulder marks; PfMissiles |
| Never under him; he tramples up to 8 a tick | the stomp, beside, lane and line terms |
| Drift toward the lit crystal | the face pull |
| After a crystal, path to where he just was | tried: kills sooner (mean death t433 against t505), but 29/40 green against 36/40; not used |

The room's real floor can be read with `QD.COXNI.reach_row` (one row of `api_drive.route` answers).

## The mode machine (strategy)

| Mode | When | Do |
|---|---|---|
| `wake` | rocks, not woken | pull to his middle; ranged set on |
| `special` | S to S+12 | stand; no healing before S+3; brew after S+12 |
| `range` | walking (to a crystal, back, or pausing) | pipe target; pull toward the lit crystal; PfMissiles + Rigour |
| `crystal` | healing form, crystal row present | melee set; crystal target; Piety; protection off |
| `done` | dead | nothing |

**Targets** (value in hp): Vasa: size 5, range 4 (the pipe reaches 5; he moves), speed 2, value 3. The crystal:
size 4, range 1, speed 4, value 5. A crystal hit is worth more than a Vasa hit, because breaking it keeps both
the heal and the timer.

**Supplies:**
- the room's drinks (overload, prayer enhance, stamina): `QD.COXO.drinks`;
- a stat restore when Attack, Strength or Ranged is 20 under its base;
- the brew floor: 40, none from S to S+3.

## Probes (the first run prints)

- **P1:** the boulder read tick. My tile at the end of T-1 is where it lands, and it reads the end of T+1 (the
  `vasa_mech` monitor rule).
- **P2:** the heal-transform tick after arrival.
- **P3:** crystal damage per tick with the sword.
- **P4:** the special's hp-5 against my HP at S+3.
