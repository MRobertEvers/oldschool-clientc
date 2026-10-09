# Blert references: what successful real teams do, per room, mode and scale

Written by `tools/raid_gate/blert_reference.py` (do not edit by hand: rerun it).  One JSON
per room/mode/scale beside this file; `tools/raid_gate/raid_report.py RUN --against
<file>` prints a run's same numbers against the reference's median and range and flags
every number outside the range.  Streams are cached under `build/blert/<room>/` (not
committed).  `attack_definitions.json` and `spell_definitions.json` are Blert's own
(its repository), the table the attack and weapon names come from.

Key: `outcome.*` the room; `output.phase.P.boss_pct_per_tick` the boss's hitpoints lost
per tick in phase P as a percent of her base (the damage output); `role.R.*` per role
(`solo`; `freezer`; Maiden's others `dps1`, `dps2` by attacks on her, other rooms' `melee`/`range`/`mage` numbered by attack count); `react.*` ticks from
an event to a role's first response; `boss.*` the boss's attacks.  What Blert cannot see:
another raider's hitpoints, what was eaten, the damage of one hit (a hit on a recorder is
its hitpoints drop in the six ticks after the attack, approximate); see the tool's
docstring.

## Per-wave scripts (`<room>_<mode>_<scale>.script.json`)

Written by `tools/raid_gate/blert_script.py <room> --mode M --scale S` from the same cached
streams and the same rooms (the reference's `selection.uuids`); read by
`tools/raid_gate/raid_report.py RUN --waves <file> [--wave N] [--roles p0=ROLE,...]`, which
aligns each of a run's waves on its own spawn tick and names, per role, the first tick
the run left the script (tile / late / none / target / return).  Every number is
`{median, min, max, n}` over the rooms.

- `anchor`: what tiles are relative to (Maiden: her SW tile; Nylocas: the Vasilias' spawn
  tile, region-local 30,23; other rooms: the boss's first tile). `lanes`: each spawn lane's
  tile relative to the anchor (Maiden N1..N4out / S1..S4out, Blert's crab positions;
  Nylocas W / S / E).
- `segments[]` in room order. Maiden `100`, `70`, `50`, `30`; Nylocas `w1`..`w31`,
  `cleanup`, `boss`; other rooms Blert's phase events. `start` (ticks from the room's
  tick 0), `length`, `adds` (spawns in the segment), `lanes` (spawns per lane over all
  rooms; a Nylocas lane is `<lane>-<style>[-big]`, `split-*` a big one's split), `leaks`
  and `leak_hp` (Maiden: crabs reaching her, the crab's hitpoints then).
- `segments[].roles.<role>` (classify_roles: Maiden `dps1`/`dps2`/`freezer`, others
  `mage`/`range`/`melee`): `tile` (modal tile over the segment relative to the anchor:
  `mode` across rooms, its `share`, and `dx`/`dy` ranges of each room's modal tile);
  `spawn_tile` (where the role stood on the spawn tick); `first_action` (ticks from the
  spawn to its first attack) and `no_action_share`; `attacks`; `casts[i]` (the i-th
  attack's offset, kept while half the rooms have one); `targets[i]` (the i-th DISTINCT
  target in order: `boss`, a lane, or `other`, with its `share`); `attack_names`;
  `return_to_boss` (offset of the first boss attack after its first add attack);
  `weapon` and `prayer` (modal per room, counted over rooms).

## Bloat, normal, scale 3

19 rooms (30 candidates, 19 death-free, 0 used with a death in the room); harvested 2026-10-06; file `bloat_normal_3.json`.
Roles seen (by what each raider did): melee1+melee2+melee3 x19.

| Number | median [min-max] n |
|---|---|
| `boss.attacks.bloat_stomp` | 1 [1-2] n=19 |
| `boss.cadence` | 70 [68-76] n=5 |
| `boss.first_attack` | 71 [68-76] n=19 |
| `outcome.boss_death_tick` | 137 [75-195] n=19 |
| `outcome.boss_heal` | 0 [0-0] n=19 |
| `outcome.deaths` | 0 [0-0] n=19 |
| `outcome.hp_lost.melee1` | 91 [49-217] n=6 |
| `outcome.hp_lost.melee2` | 97 [31-122] n=10 |
| `outcome.hp_lost.melee3` | 105 [11-127] n=5 |
| `outcome.leaks` | 0 [0-0] n=19 |
| `outcome.phase.down1.boss_heal` | 0 [0-0] n=19 |
| `outcome.phase.down1.leaks` | 0 [0-0] n=19 |
| `outcome.phase.down1.start` | 42 [39-47] n=19 |
| `outcome.phase.down1.ticks` | 33 [31-33] n=19 |
| `outcome.phase.down2.boss_heal` | 0 [0-0] n=18 |
| `outcome.phase.down2.leaks` | 0 [0-0] n=18 |
| `outcome.phase.down2.start` | 113 [107-119] n=18 |
| `outcome.phase.down2.ticks` | 24.5 [13-33] n=18 |
| `outcome.phase.down3.boss_heal` | 0 [0-0] n=2 |
| `outcome.phase.down3.leaks` | 0 [0-0] n=2 |
| `outcome.phase.down3.start` | 185.5 [184-187] n=2 |
| `outcome.phase.down3.ticks` | 8 [8-8] n=2 |
| `outcome.phase.start.boss_heal` | 0 [0-0] n=19 |
| `outcome.phase.start.leaks` | 0 [0-0] n=19 |
| `outcome.phase.start.start` | 0 [0-0] n=19 |
| `outcome.phase.start.ticks` | 42 [39-47] n=19 |
| `outcome.phase.walk2.boss_heal` | 0 [0-0] n=18 |
| `outcome.phase.walk2.leaks` | 0 [0-0] n=18 |
| `outcome.phase.walk2.start` | 74.5 [72-80] n=18 |
| `outcome.phase.walk2.ticks` | 36 [35-43] n=18 |
| `outcome.phase.walk3.boss_heal` | 0 [0-0] n=4 |
| `outcome.phase.walk3.leaks` | 0 [0-0] n=4 |
| `outcome.phase.walk3.start` | 144.5 [141-147] n=4 |
| `outcome.phase.walk3.ticks` | 19.5 [1-40] n=4 |
| `outcome.room_ticks` | 137 [75-195] n=19 |
| `output.phase.down1.boss_hp_per_tick` | 26.97 [18.7-35.13] n=19 |
| `output.phase.down1.boss_pct_per_tick` | 1.798 [1.246-2.342] n=19 |
| `output.phase.down2.boss_hp_per_tick` | 22.8 [19.91-26.41] n=18 |
| `output.phase.down2.boss_pct_per_tick` | 1.52 [1.327-1.761] n=18 |
| `output.phase.down3.boss_hp_per_tick` | 11.81 [8.12-15.5] n=2 |
| `output.phase.down3.boss_pct_per_tick` | 0.787 [0.542-1.033] n=2 |
| `output.phase.start.boss_hp_per_tick` | 0.77 [0-9.34] n=19 |
| `output.phase.start.boss_pct_per_tick` | 0.052 [0-0.623] n=19 |
| `output.phase.walk2.boss_hp_per_tick` | 0.085 [0-2.03] n=18 |
| `output.phase.walk2.boss_pct_per_tick` | 0.006 [0-0.135] n=18 |
| `output.phase.walk3.boss_hp_per_tick` | 0 [0-0] n=4 |
| `output.phase.walk3.boss_pct_per_tick` | 0 [0-0] n=4 |
| `role.melee1.barrage_pct` | 0 [0-0] n=19 |
| `role.melee1.cadence` | 5 [4-6] n=19 |
| `role.melee1.eat_at_hp_pct` | 25 [6-73] n=16 |
| `role.melee1.magic_pct` | 0 [0-0] n=19 |
| `role.melee1.melee_pct` | 75 [57.9-100] n=19 |
| `role.melee1.phase.down1.attacks_add` | 0 [0-0] n=19 |
| `role.melee1.phase.down1.attacks_boss` | 6 [5-7] n=19 |
| `role.melee1.phase.down1.dist_boss` | 3 [0-7] n=19 |
| `role.melee1.phase.down1.eats` | 0 [0-2] n=6 |
| `role.melee1.phase.down2.attacks_add` | 0 [0-0] n=18 |
| `role.melee1.phase.down2.attacks_boss` | 4 [2-6] n=18 |
| `role.melee1.phase.down2.dist_boss` | 4 [1-7] n=18 |
| `role.melee1.phase.down2.eats` | 0 [0-0] n=5 |
| `role.melee1.phase.down3.attacks_add` | 0 [0-0] n=2 |
| `role.melee1.phase.down3.attacks_boss` | 1 [1-1] n=2 |
| `role.melee1.phase.down3.dist_boss` | 5 [3-7] n=2 |
| `role.melee1.phase.start.attacks_add` | 0 [0-0] n=19 |
| `role.melee1.phase.start.attacks_boss` | 3 [0-8] n=19 |
| `role.melee1.phase.start.dist_boss` | 3 [2-5] n=19 |
| `role.melee1.phase.start.eats` | 0 [0-8] n=6 |
| `role.melee1.phase.walk2.attacks_add` | 0 [0-0] n=18 |
| `role.melee1.phase.walk2.attacks_boss` | 0 [0-5] n=18 |
| `role.melee1.phase.walk2.dist_boss` | 3 [2-6] n=18 |
| `role.melee1.phase.walk2.eats` | 0 [0-1] n=5 |
| `role.melee1.phase.walk3.attacks_add` | 0 [0-0] n=4 |
| `role.melee1.phase.walk3.attacks_boss` | 0 [0-0] n=4 |
| `role.melee1.phase.walk3.dist_boss` | 3 [1-4] n=4 |
| `role.melee1.ranged_pct` | 25 [0-42.1] n=19 |
| `role.melee2.barrage_pct` | 0 [0-0] n=19 |
| `role.melee2.cadence` | 5 [5-5] n=19 |
| `role.melee2.eat_at_hp_pct` | 36 [5-91] n=14 |
| `role.melee2.magic_pct` | 0 [0-0] n=19 |
| `role.melee2.melee_pct` | 76.9 [66.7-100] n=19 |
| `role.melee2.phase.down1.attacks_add` | 0 [0-0] n=19 |
| `role.melee2.phase.down1.attacks_boss` | 6 [5-7] n=19 |
| `role.melee2.phase.down1.dist_boss` | 3 [0-7] n=19 |
| `role.melee2.phase.down1.eats` | 0 [0-1] n=10 |
| `role.melee2.phase.down2.attacks_add` | 0 [0-0] n=18 |
| `role.melee2.phase.down2.attacks_boss` | 3.5 [2-6] n=18 |
| `role.melee2.phase.down2.dist_boss` | 2.25 [0-7] n=18 |
| `role.melee2.phase.down2.eats` | 0 [0-0] n=9 |
| `role.melee2.phase.down3.attacks_add` | 0 [0-0] n=2 |
| `role.melee2.phase.down3.attacks_boss` | 0.5 [0-1] n=2 |
| `role.melee2.phase.down3.dist_boss` | 2.25 [0-4.5] n=2 |
| `role.melee2.phase.down3.eats` | 0 [0-0] n=2 |
| `role.melee2.phase.start.attacks_add` | 0 [0-0] n=19 |
| `role.melee2.phase.start.attacks_boss` | 3 [0-6] n=19 |
| `role.melee2.phase.start.dist_boss` | 3 [2-11] n=19 |
| `role.melee2.phase.start.eats` | 0 [0-2] n=10 |
| `role.melee2.phase.walk2.attacks_add` | 0 [0-0] n=18 |
| `role.melee2.phase.walk2.attacks_boss` | 0 [0-1] n=18 |
| `role.melee2.phase.walk2.dist_boss` | 4 [1-6] n=18 |
| `role.melee2.phase.walk2.eats` | 0 [0-1] n=9 |
| `role.melee2.phase.walk3.attacks_add` | 0 [0-0] n=4 |
| `role.melee2.phase.walk3.attacks_boss` | 0 [0-0] n=4 |
| `role.melee2.phase.walk3.dist_boss` | 3.25 [1-4] n=4 |
| `role.melee2.phase.walk3.eats` | 0 [0-0] n=4 |
| `role.melee2.ranged_pct` | 23.1 [0-33.3] n=19 |
| `role.melee3.barrage_pct` | 0 [0-0] n=19 |
| `role.melee3.cadence` | 5 [4-5] n=19 |
| `role.melee3.eat_at_hp_pct` | 23.5 [6-66] n=8 |
| `role.melee3.magic_pct` | 0 [0-0] n=19 |
| `role.melee3.melee_pct` | 100 [70-100] n=19 |
| `role.melee3.phase.down1.attacks_add` | 0 [0-0] n=19 |
| `role.melee3.phase.down1.attacks_boss` | 6 [1-7] n=19 |
| `role.melee3.phase.down1.dist_boss` | 4 [0-7] n=19 |
| `role.melee3.phase.down1.eats` | 0 [0-1] n=5 |
| `role.melee3.phase.down2.attacks_add` | 0 [0-0] n=18 |
| `role.melee3.phase.down2.attacks_boss` | 4 [1-5] n=18 |
| `role.melee3.phase.down2.dist_boss` | 3 [0-7] n=18 |
| `role.melee3.phase.down2.eats` | 0 [0-1] n=5 |
| `role.melee3.phase.down3.attacks_add` | 0 [0-0] n=2 |
| `role.melee3.phase.down3.attacks_boss` | 0 [0-0] n=2 |
| `role.melee3.phase.down3.dist_boss` | 4 [1-7] n=2 |
| `role.melee3.phase.start.attacks_add` | 0 [0-0] n=19 |
| `role.melee3.phase.start.attacks_boss` | 2 [0-5] n=19 |
| `role.melee3.phase.start.dist_boss` | 4 [2-11] n=19 |
| `role.melee3.phase.start.eats` | 0 [0-0] n=5 |
| `role.melee3.phase.walk2.attacks_add` | 0 [0-0] n=18 |
| `role.melee3.phase.walk2.attacks_boss` | 0 [0-0] n=18 |
| `role.melee3.phase.walk2.dist_boss` | 3.25 [2-7] n=18 |
| `role.melee3.phase.walk2.eats` | 0 [0-1] n=5 |
| `role.melee3.phase.walk3.attacks_add` | 0 [0-0] n=4 |
| `role.melee3.phase.walk3.attacks_boss` | 0 [0-0] n=4 |
| `role.melee3.phase.walk3.dist_boss` | 4 [3-5] n=4 |
| `role.melee3.ranged_pct` | 0 [0-30] n=19 |
| `react.phase.down1.melee1.attack` | 3 [1-5] n=19 |
| `react.phase.down1.melee1.step` | 0 [0-5] n=18 |
| `react.phase.down1.melee1.swap` | 4 [0-7] n=18 |
| `react.phase.down1.melee2.attack` | 3 [1-6] n=19 |
| `react.phase.down1.melee2.step` | 1 [0-2] n=19 |
| `react.phase.down1.melee2.swap` | 5.5 [3-20] n=18 |
| `react.phase.down1.melee3.attack` | 4 [2-8] n=18 |
| `react.phase.down1.melee3.step` | 1 [0-14] n=19 |
| `react.phase.down1.melee3.swap` | 6 [4-15] n=18 |
| `react.phase.down2.melee1.attack` | 5 [3-7] n=18 |
| `react.phase.down2.melee1.step` | 0 [0-2] n=18 |
| `react.phase.down2.melee1.swap` | 8 [4-10] n=14 |
| `react.phase.down2.melee2.attack` | 5.5 [2-11] n=18 |
| `react.phase.down2.melee2.step` | 0 [0-2] n=18 |
| `react.phase.down2.melee2.swap` | 7 [4-15] n=17 |
| `react.phase.down2.melee3.attack` | 6 [4-9] n=18 |
| `react.phase.down2.melee3.step` | 0 [0-3] n=18 |
| `react.phase.down2.melee3.swap` | 9.5 [6-18] n=16 |
| `react.phase.down3.melee1.attack` | 3.5 [3-4] n=2 |
| `react.phase.down3.melee1.step` | 0.5 [0-1] n=2 |
| `react.phase.down3.melee1.swap` | 5 [5-5] n=1 |
| `react.phase.down3.melee2.attack` | 4 [4-4] n=1 |
| `react.phase.down3.melee2.step` | 1 [0-2] n=2 |
| `react.phase.down3.melee3.step` | 0.5 [0-1] n=2 |
| `react.phase.down3.melee3.swap` | 4 [4-4] n=1 |
| `react.phase.walk2.melee1.attack` | 11 [0-18] n=4 |
| `react.phase.walk2.melee1.step` | 0 [0-4] n=18 |
| `react.phase.walk2.melee1.swap` | 4.5 [0-17] n=12 |
| `react.phase.walk2.melee2.attack` | 0 [0-0] n=1 |
| `react.phase.walk2.melee2.step` | 0 [0-1] n=18 |
| `react.phase.walk2.melee2.swap` | 6.5 [0-16] n=16 |
| `react.phase.walk2.melee3.step` | 0 [0-3] n=18 |
| `react.phase.walk2.melee3.swap` | 2 [0-13] n=10 |
| `react.phase.walk3.melee1.step` | 1 [0-3] n=4 |
| `react.phase.walk3.melee1.swap` | 20 [20-20] n=1 |
| `react.phase.walk3.melee2.step` | 0 [0-3] n=3 |
| `react.phase.walk3.melee3.step` | 0 [0-3] n=4 |

Weapons per role and phase (Blert's weapon name, variants merged, or the id it does not know: rooms using it / median attacks in those rooms):

- `melee1|down1`: CHALLY: 17 / 2, SCYTHE: 16 / 4, CLAW: 4 / 1.5, SAELDOR: 2 / 3.5
- `melee1|down2`: SCYTHE: 15 / 3, CLAW: 12 / 1, CHALLY: 6 / 1, SAELDOR: 2 / 4
- `melee1|down3`: SCYTHE: 1 / 1, CHALLY: 1 / 1
- `melee1|start`: ZCB: 11 / 3, TWISTED_BOW: 5 / 3, SCYTHE: 2 / 4.5, BOWFA: 1 / 3
- `melee1|walk2`: ZCB: 2 / 3, TWISTED_BOW: 2 / 1.5, CHALLY: 1 / 1
- `melee2|down1`: CHALLY: 18 / 2, SCYTHE: 18 / 4, CLAW: 7 / 1, SOULREAPER_AXE: 1 / 4
- `melee2|down2`: SCYTHE: 17 / 3, CLAW: 12 / 1, CHALLY: 5 / 1, SOULREAPER_AXE: 1 / 4
- `melee2|down3`: SCYTHE: 1 / 1
- `melee2|start`: ZCB: 14 / 3, TONALZTICS: 1 / 3, BGS/GODSWORD: 1 / 1, SCYTHE: 1 / 5
- `melee2|walk2`: CHALLY: 1 / 1
- `melee3|down1`: CHALLY: 18 / 1.5, SCYTHE: 15 / 4, CLAW: 7 / 1, 4151: 3 / 3
- `melee3|down2`: SCYTHE: 14 / 3, CLAW: 9 / 1, CHALLY: 8 / 1, 4151: 3 / 4
- `melee3|start`: ZCB: 9 / 3, SCYTHE: 3 / 1

Where each role stands (offset from the boss's SW tile, share of the phase's ticks, top 3):

- `melee1|down1`: (5,4) 9%, (7,-1) 8%, (2,11) 7%
- `melee1|down2`: (7,5) 6%, (-1,-1) 5%, (0,-2) 5%
- `melee1|down3`: (3,11) 25%, (6,11) 6%, (7,10) 6%
- `melee1|start`: (0,-5) 11%, (0,9) 8%, (2,0) 6%
- `melee1|walk2`: (7,-1) 5%, (6,11) 4%, (6,0) 4%
- `melee1|walk3`: (7,7) 10%, (7,6) 10%, (6,4) 8%
- `melee2|down1`: (5,4) 10%, (7,-6) 9%, (7,-1) 8%
- `melee2|down2`: (0,-1) 7%, (7,5) 7%, (1,0) 7%
- `melee2|down3`: (0,6) 12%, (7,4) 12%, (8,9) 12%
- `melee2|start`: (0,-5) 8%, (0,9) 7%, (2,4) 6%
- `melee2|walk2`: (0,-1) 4%, (6,11) 4%, (2,0) 3%
- `melee2|walk3`: (7,5) 11%, (1,11) 9%, (7,7) 8%
- `melee3|down1`: (7,9) 5%, (7,5) 5%, (2,0) 4%
- `melee3|down2`: (3,0) 8%, (7,11) 6%, (6,4) 5%
- `melee3|down3`: (7,9) 19%, (7,4) 19%, (7,11) 12%
- `melee3|start`: (13,-2) 6%, (0,9) 5%, (0,-5) 4%
- `melee3|walk2`: (7,-1) 4%, (1,11) 4%, (6,0) 3%
- `melee3|walk3`: (6,11) 11%, (7,4) 9%, (7,5) 6%

## Maiden, entry, scale 1

1 rooms (1 candidates, 1 death-free, 0 used with a death in the room); harvested 2026-10-06; file `maiden_entry_1.json`.
Roles seen (by what each raider did): solo x1.

| Number | median [min-max] n |
|---|---|
| `boss.attacks.maiden_auto` | 7 [7-7] n=1 |
| `boss.cadence` | 10 [10-10] n=1 |
| `boss.first_attack` | 10 [10-10] n=1 |
| `outcome.boss_death_tick` | 82 [82-82] n=1 |
| `outcome.boss_heal` | 6 [6-6] n=1 |
| `outcome.deaths` | 0 [0-0] n=1 |
| `outcome.leaks` | 2 [2-2] n=1 |
| `outcome.phase.100.boss_heal` | 0 [0-0] n=1 |
| `outcome.phase.100.leaks` | 0 [0-0] n=1 |
| `outcome.phase.100.start` | 0 [0-0] n=1 |
| `outcome.phase.100.ticks` | 26 [26-26] n=1 |
| `outcome.phase.30.boss_heal` | 0 [0-0] n=1 |
| `outcome.phase.30.leaks` | 0 [0-0] n=1 |
| `outcome.phase.30.start` | 61 [61-61] n=1 |
| `outcome.phase.30.ticks` | 21 [21-21] n=1 |
| `outcome.phase.50.boss_heal` | 6 [6-6] n=1 |
| `outcome.phase.50.leaks` | 1 [1-1] n=1 |
| `outcome.phase.50.start` | 41 [41-41] n=1 |
| `outcome.phase.50.ticks` | 20 [20-20] n=1 |
| `outcome.phase.70.boss_heal` | 0 [0-0] n=1 |
| `outcome.phase.70.leaks` | 1 [1-1] n=1 |
| `outcome.phase.70.start` | 26 [26-26] n=1 |
| `outcome.phase.70.ticks` | 15 [15-15] n=1 |
| `outcome.room_ticks` | 82 [82-82] n=1 |
| `output.phase.100.boss_hp_per_tick` | 4.96 [4.96-4.96] n=1 |
| `output.phase.100.boss_pct_per_tick` | 0.992 [0.992-0.992] n=1 |
| `output.phase.30.boss_hp_per_tick` | 7.76 [7.76-7.76] n=1 |
| `output.phase.30.boss_pct_per_tick` | 1.552 [1.552-1.552] n=1 |
| `output.phase.50.boss_hp_per_tick` | 7.15 [7.15-7.15] n=1 |
| `output.phase.50.boss_pct_per_tick` | 1.43 [1.43-1.43] n=1 |
| `output.phase.70.boss_hp_per_tick` | 4.73 [4.73-4.73] n=1 |
| `output.phase.70.boss_pct_per_tick` | 0.947 [0.947-0.947] n=1 |
| `role.solo.barrage_pct` | 0 [0-0] n=1 |
| `role.solo.cadence` | 5 [5-5] n=1 |
| `role.solo.magic_pct` | 0 [0-0] n=1 |
| `role.solo.melee_pct` | 100 [100-100] n=1 |
| `role.solo.phase.100.attacks_add` | 0 [0-0] n=1 |
| `role.solo.phase.100.attacks_boss` | 4 [4-4] n=1 |
| `role.solo.phase.100.dist_boss` | 3 [3-3] n=1 |
| `role.solo.phase.30.attacks_add` | 0 [0-0] n=1 |
| `role.solo.phase.30.attacks_boss` | 3 [3-3] n=1 |
| `role.solo.phase.30.dist_boss` | 1 [1-1] n=1 |
| `role.solo.phase.50.attacks_add` | 1 [1-1] n=1 |
| `role.solo.phase.50.attacks_boss` | 3 [3-3] n=1 |
| `role.solo.phase.50.dist_boss` | 1 [1-1] n=1 |
| `role.solo.phase.70.attacks_add` | 1 [1-1] n=1 |
| `role.solo.phase.70.attacks_boss` | 2 [2-2] n=1 |
| `role.solo.phase.70.dist_boss` | 1 [1-1] n=1 |
| `role.solo.prayer.maiden_auto.lit_ticks` | 35 [35-35] n=1 |
| `role.solo.prayer.maiden_auto.right_pct` | 100 [100-100] n=1 |
| `role.solo.ranged_pct` | 0 [0-0] n=1 |
| `react.phase.30.solo.attack` | 4 [4-4] n=1 |
| `react.phase.30.solo.step` | 0 [0-0] n=1 |
| `react.phase.30.solo.swap` | 1 [1-1] n=1 |
| `react.phase.50.solo.attack` | 4 [4-4] n=1 |
| `react.phase.50.solo.attack_add` | 4 [4-4] n=1 |
| `react.phase.50.solo.step` | 0 [0-0] n=1 |
| `react.phase.70.solo.attack` | 4 [4-4] n=1 |
| `react.phase.70.solo.attack_add` | 9 [9-9] n=1 |
| `react.phase.70.solo.step` | 0 [0-0] n=1 |

Weapons per role and phase (Blert's weapon name, variants merged, or the id it does not know: rooms using it / median attacks in those rooms):

- `solo|100`: SCYTHE: 1 / 4
- `solo|30`: CLAW: 1 / 2, SCYTHE: 1 / 1
- `solo|50`: SCYTHE: 1 / 4
- `solo|70`: SCYTHE: 1 / 3

Where each role stands (offset from the boss's SW tile, share of the phase's ticks, top 3):

- `solo|100`: (5,6) 12%, (0,8) 12%, (22,1) 8%
- `solo|30`: (6,5) 19%, (6,3) 19%, (5,6) 10%
- `solo|50`: (6,5) 25%, (6,3) 15%, (6,1) 10%
- `solo|70`: (5,6) 20%, (6,5) 20%, (6,4) 13%

## Maiden, normal, scale 3

24 rooms (26 candidates, 24 death-free, 0 used with a death in the room); harvested 2026-10-06; file `maiden_normal_3.json`.
Roles seen (by what each raider did): dps1+dps2+freezer x24.

| Number | median [min-max] n |
|---|---|
| `boss.attacks.maiden_auto` | 12 [10-16] n=24 |
| `boss.attacks.maiden_blood` | 3 [1-6] n=24 |
| `boss.cadence` | 10 [10-10] n=24 |
| `boss.first_attack` | 9 [9-10] n=24 |
| `boss.hit_on_recorder.maiden_auto` | 12.5 [0-62] n=112 |
| `boss.hit_on_recorder.maiden_blood` | 0 [0-41] n=26 |
| `outcome.boss_death_tick` | 157.5 [132-204] n=24 |
| `outcome.boss_heal` | 223.5 [0-821] n=24 |
| `outcome.deaths` | 0 [0-0] n=24 |
| `outcome.hp_lost.dps1` | 60.5 [24-106] n=10 |
| `outcome.hp_lost.dps2` | 102 [92-133] n=5 |
| `outcome.hp_lost.freezer` | 65 [18-142] n=20 |
| `outcome.leaks` | 5 [0-13] n=24 |
| `outcome.phase.100.boss_heal` | 0 [0-8] n=24 |
| `outcome.phase.100.leaks` | 0 [0-0] n=24 |
| `outcome.phase.100.start` | 0 [0-0] n=24 |
| `outcome.phase.100.ticks` | 42 [32-52] n=24 |
| `outcome.phase.30.boss_heal` | 49 [0-762] n=24 |
| `outcome.phase.30.leaks` | 2 [0-9] n=24 |
| `outcome.phase.30.start` | 107 [87-156] n=24 |
| `outcome.phase.30.ticks` | 48 [34-75] n=24 |
| `outcome.phase.50.boss_heal` | 76.5 [0-411] n=24 |
| `outcome.phase.50.leaks` | 2 [0-5] n=24 |
| `outcome.phase.50.start` | 69.5 [57-107] n=24 |
| `outcome.phase.50.ticks` | 40 [25-66] n=24 |
| `outcome.phase.70.boss_heal` | 1 [0-226] n=24 |
| `outcome.phase.70.leaks` | 1 [0-2] n=24 |
| `outcome.phase.70.start` | 42 [32-52] n=24 |
| `outcome.phase.70.ticks` | 30 [20-55] n=24 |
| `outcome.room_ticks` | 157.5 [132-204] n=24 |
| `output.phase.100.boss_hp_per_tick` | 17.94 [15.1-21.83] n=24 |
| `output.phase.100.boss_pct_per_tick` | 0.683 [0.575-0.832] n=24 |
| `output.phase.30.boss_hp_per_tick` | 19.11 [15.93-25.91] n=24 |
| `output.phase.30.boss_pct_per_tick` | 0.728 [0.607-0.987] n=24 |
| `output.phase.50.boss_hp_per_tick` | 15.245 [11.35-24.6] n=24 |
| `output.phase.50.boss_pct_per_tick` | 0.581 [0.432-0.937] n=24 |
| `output.phase.70.boss_hp_per_tick` | 18.945 [10.51-28.9] n=24 |
| `output.phase.70.boss_pct_per_tick` | 0.722 [0.4-1.101] n=24 |
| `role.dps1.barrage_pct` | 0 [0-0] n=24 |
| `role.dps1.boss_targeted_pct` | 38 [5.6-58.8] n=24 |
| `role.dps1.cadence` | 5 [2-5] n=24 |
| `role.dps1.eat_at_hp_pct` | 60 [16-81] n=9 |
| `role.dps1.magic_pct` | 0 [0-0] n=24 |
| `role.dps1.melee_pct` | 88.9 [2.8-100] n=24 |
| `role.dps1.phase.100.attacks_add` | 0 [0-0] n=24 |
| `role.dps1.phase.100.attacks_boss` | 7.5 [6-15] n=24 |
| `role.dps1.phase.100.dist_boss` | 1 [1-4] n=24 |
| `role.dps1.phase.100.eats` | 0 [0-0] n=10 |
| `role.dps1.phase.30.attacks_add` | 1 [0-8] n=24 |
| `role.dps1.phase.30.attacks_boss` | 7 [5-17] n=24 |
| `role.dps1.phase.30.dist_boss` | 1 [1-3] n=24 |
| `role.dps1.phase.30.eats` | 0 [0-0] n=10 |
| `role.dps1.phase.50.attacks_add` | 2 [0-10] n=24 |
| `role.dps1.phase.50.attacks_boss` | 6.5 [3-19] n=24 |
| `role.dps1.phase.50.dist_boss` | 1 [1-4] n=24 |
| `role.dps1.phase.50.eats` | 0 [0-0] n=10 |
| `role.dps1.phase.70.attacks_add` | 1 [0-15] n=24 |
| `role.dps1.phase.70.attacks_boss` | 5 [3-12] n=24 |
| `role.dps1.phase.70.dist_boss` | 1 [1-4] n=24 |
| `role.dps1.phase.70.eats` | 0 [0-0] n=10 |
| `role.dps1.prayer.maiden_auto.lit_ticks` | 24 [3-50] n=17 |
| `role.dps1.prayer.maiden_auto.right_pct` | 50 [0-100] n=23 |
| `role.dps1.ranged_pct` | 11.1 [0-97.2] n=24 |
| `role.dps2.barrage_pct` | 0 [0-0] n=24 |
| `role.dps2.boss_targeted_pct` | 41.45 [18.8-64.3] n=24 |
| `role.dps2.cadence` | 5 [2-5] n=24 |
| `role.dps2.eat_at_hp_pct` | 36 [14-75] n=14 |
| `role.dps2.magic_pct` | 0 [0-0] n=24 |
| `role.dps2.melee_pct` | 89.3 [16-100] n=24 |
| `role.dps2.phase.100.attacks_add` | 0 [0-0] n=24 |
| `role.dps2.phase.100.attacks_boss` | 7 [6-11] n=24 |
| `role.dps2.phase.100.dist_boss` | 1 [1-2] n=24 |
| `role.dps2.phase.100.eats` | 0 [0-0] n=5 |
| `role.dps2.phase.30.attacks_add` | 1 [0-3] n=24 |
| `role.dps2.phase.30.attacks_boss` | 6.5 [5-12] n=24 |
| `role.dps2.phase.30.dist_boss` | 1 [1-2] n=24 |
| `role.dps2.phase.30.eats` | 0 [0-1] n=5 |
| `role.dps2.phase.50.attacks_add` | 3 [1-8] n=24 |
| `role.dps2.phase.50.attacks_boss` | 5 [1-17] n=24 |
| `role.dps2.phase.50.dist_boss` | 1 [1-5] n=24 |
| `role.dps2.phase.50.eats` | 0 [0-3] n=5 |
| `role.dps2.phase.70.attacks_add` | 1 [0-8] n=24 |
| `role.dps2.phase.70.attacks_boss` | 5 [3-17] n=24 |
| `role.dps2.phase.70.dist_boss` | 1 [1-3] n=24 |
| `role.dps2.phase.70.eats` | 0 [0-2] n=5 |
| `role.dps2.prayer.maiden_auto.lit_ticks` | 41 [1-50] n=19 |
| `role.dps2.prayer.maiden_auto.right_pct` | 45 [0-100] n=24 |
| `role.dps2.ranged_pct` | 10.7 [0-84] n=24 |
| `role.freezer.barrage_pct` | 42.4 [34.4-60.7] n=24 |
| `role.freezer.boss_targeted_pct` | 19.95 [6.2-50] n=24 |
| `role.freezer.cadence` | 5 [5-5] n=24 |
| `role.freezer.eat_at_hp_pct` | 75 [18-96] n=34 |
| `role.freezer.magic_pct` | 44.25 [37.5-64.3] n=24 |
| `role.freezer.melee_pct` | 10.7 [0-28.1] n=24 |
| `role.freezer.phase.100.attacks_add` | 0 [0-0] n=24 |
| `role.freezer.phase.100.attacks_boss` | 7 [5-9] n=24 |
| `role.freezer.phase.100.dist_boss` | 10 [7-10] n=24 |
| `role.freezer.phase.100.eats` | 0 [0-0] n=20 |
| `role.freezer.phase.30.attacks_add` | 4 [2-7] n=24 |
| `role.freezer.phase.30.attacks_boss` | 4 [1-10] n=24 |
| `role.freezer.phase.30.dist_boss` | 1 [1-3] n=24 |
| `role.freezer.phase.30.eats` | 0.5 [0-3] n=20 |
| `role.freezer.phase.50.attacks_add` | 5 [3-8] n=24 |
| `role.freezer.phase.50.attacks_boss` | 2 [1-7] n=24 |
| `role.freezer.phase.50.dist_boss` | 7 [1-10] n=24 |
| `role.freezer.phase.50.eats` | 0 [0-0] n=20 |
| `role.freezer.phase.70.attacks_add` | 4 [2-7] n=24 |
| `role.freezer.phase.70.attacks_boss` | 2 [0-4] n=24 |
| `role.freezer.phase.70.dist_boss` | 10 [6-11] n=24 |
| `role.freezer.phase.70.eats` | 0 [0-0] n=20 |
| `role.freezer.prayer.maiden_auto.lit_ticks` | 43 [6-50] n=9 |
| `role.freezer.prayer.maiden_auto.right_pct` | 0 [0-100] n=24 |
| `role.freezer.ranged_pct` | 45 [25-58.6] n=24 |
| `react.phase.30.dps1.attack` | 4 [0-4] n=24 |
| `react.phase.30.dps1.attack_add` | 4 [1-6] n=17 |
| `react.phase.30.dps1.step` | 1 [0-17] n=24 |
| `react.phase.30.dps1.swap` | 5 [0-16] n=18 |
| `react.phase.30.dps2.attack` | 4 [0-5] n=24 |
| `react.phase.30.dps2.attack_add` | 4 [2-9] n=20 |
| `react.phase.30.dps2.step` | 1 [0-3] n=24 |
| `react.phase.30.dps2.swap` | 5.5 [0-20] n=20 |
| `react.phase.30.freezer.attack` | 1 [1-7] n=24 |
| `react.phase.30.freezer.attack_add` | 1 [1-7] n=24 |
| `react.phase.30.freezer.step` | 1.5 [0-7] n=24 |
| `react.phase.30.freezer.swap` | 18 [0-19] n=23 |
| `react.phase.50.dps1.attack` | 4 [0-5] n=24 |
| `react.phase.50.dps1.attack_add` | 4 [0-13] n=22 |
| `react.phase.50.dps1.step` | 1 [0-7] n=24 |
| `react.phase.50.dps1.swap` | 5 [0-11] n=18 |
| `react.phase.50.dps2.attack` | 4 [0-4] n=24 |
| `react.phase.50.dps2.attack_add` | 4 [2-17] n=24 |
| `react.phase.50.dps2.step` | 1 [0-8] n=24 |
| `react.phase.50.dps2.swap` | 5 [0-20] n=19 |
| `react.phase.50.freezer.attack` | 1 [1-6] n=24 |
| `react.phase.50.freezer.attack_add` | 1 [1-6] n=24 |
| `react.phase.50.freezer.step` | 2 [0-9] n=24 |
| `react.phase.50.freezer.swap` | 9 [2-19] n=17 |
| `react.phase.70.dps1.attack` | 4 [0-5] n=24 |
| `react.phase.70.dps1.attack_add` | 4 [3-16] n=20 |
| `react.phase.70.dps1.step` | 1 [0-6] n=24 |
| `react.phase.70.dps1.swap` | 5 [0-12] n=18 |
| `react.phase.70.dps2.attack` | 4 [0-4] n=24 |
| `react.phase.70.dps2.attack_add` | 4 [3-9] n=19 |
| `react.phase.70.dps2.step` | 1 [0-3] n=24 |
| `react.phase.70.dps2.swap` | 5 [0-16] n=18 |
| `react.phase.70.freezer.attack` | 1 [1-6] n=24 |
| `react.phase.70.freezer.attack_add` | 1 [1-6] n=24 |
| `react.phase.70.freezer.step` | 2 [0-12] n=22 |
| `react.phase.70.freezer.swap` | 8 [0-20] n=17 |

Weapons per role and phase (Blert's weapon name, variants merged, or the id it does not know: rooms using it / median attacks in those rooms):

- `dps1|100`: TWISTED_BOW: 19 / 1, SCYTHE: 18 / 5, TONALZTICS: 16 / 1, HAMMER/HAMMER_BOP: 4 / 1.5
- `dps1|30`: SCYTHE: 18 / 7, CLAW: 8 / 1, BLOWPIPE: 5 / 16, CHALLY: 5 / 1
- `dps1|50`: SCYTHE: 18 / 7, DINHS: 7 / 1, BLOWPIPE: 6 / 19, TWISTED_BOW: 2 / 4.5
- `dps1|70`: SCYTHE: 18 / 5, ZCB: 8 / 1, BLOWPIPE: 6 / 14.5, TWISTED_BOW: 2 / 4
- `dps2|100`: SCYTHE: 23 / 5, TWISTED_BOW: 22 / 1, TONALZTICS: 15 / 1, ELDER_MAUL: 8 / 1
- `dps2|30`: SCYTHE: 23 / 7, ZCB: 10 / 1, CLAW: 6 / 1, CHALLY: 4 / 1
- `dps2|50`: SCYTHE: 23 / 8, DINHS: 9 / 1, BLOWPIPE: 1 / 24, CHIN_RED: 1 / 1
- `dps2|70`: SCYTHE: 23 / 5, ZCB: 9 / 1, BLOWPIPE: 1 / 25, SULPHUR_BLADES: 1 / 1
- `freezer|100`: TWISTED_BOW: 23 / 5, TONALZTICS: 23 / 1, BLOWPIPE: 14 / 1, ELDER_MAUL: 2 / 1
- `freezer|30`: SCEPTRE: 20 / 4, TWISTED_BOW: 19 / 1, SCYTHE: 19 / 3, ZCB: 6 / 1
- `freezer|50`: SCEPTRE: 20 / 4.5, TWISTED_BOW: 20 / 2, ZCB: 15 / 1, EYE_OF_AYAK: 5 / 1
- `freezer|70`: TWISTED_BOW: 22 / 2, SCEPTRE: 20 / 4, EYE_OF_AYAK: 3 / 1, KODAI: 2 / 6.5

Where each role stands (offset from the boss's SW tile, share of the phase's ticks, top 3):

- `dps1|100`: (4,6) 13%, (5,6) 12%, (3,6) 8%
- `dps1|30`: (5,6) 16%, (4,6) 8%, (3,6) 5%
- `dps1|50`: (5,6) 15%, (4,6) 11%, (6,5) 9%
- `dps1|70`: (5,6) 13%, (3,6) 12%, (4,6) 11%
- `dps2|100`: (5,6) 10%, (6,5) 8%, (3,6) 7%
- `dps2|30`: (6,5) 8%, (5,6) 6%, (2,6) 6%
- `dps2|50`: (6,4) 10%, (6,5) 9%, (6,2) 8%
- `dps2|70`: (6,5) 23%, (6,3) 9%, (5,6) 9%
- `freezer|100`: (15,-1) 10%, (14,1) 10%, (15,2) 8%
- `freezer|30`: (6,0) 13%, (6,3) 11%, (6,2) 11%
- `freezer|50`: (8,-1) 6%, (15,3) 5%, (9,-1) 5%
- `freezer|70`: (15,0) 17%, (15,3) 8%, (15,2) 7%

## Nylocas, normal, scale 3

27 rooms (31 candidates, 27 death-free, 0 used with a death in the room); harvested 2026-10-06; file `nylocas_normal_3.json`.
Roles seen (by what each raider did): mage+melee+range x27.

| Number | median [min-max] n |
|---|---|
| `boss.attacks.nylo_mage` | 6 [3-10] n=27 |
| `boss.attacks.nylo_melee` | 7 [4-10] n=27 |
| `boss.attacks.nylo_range` | 6 [2-10] n=27 |
| `boss.cadence` | 4 [4-5] n=27 |
| `boss.first_attack` | 314 [299-360] n=27 |
| `boss.hit_on_recorder.nylo_mage` | 4 [0-45] n=53 |
| `boss.hit_on_recorder.nylo_melee` | 0 [0-17] n=76 |
| `boss.hit_on_recorder.nylo_range` | 0 [0-50] n=62 |
| `outcome.boss_death_tick` | 410 [371-471] n=27 |
| `outcome.boss_heal` | 90 [0-284] n=27 |
| `outcome.deaths` | 0 [0-0] n=27 |
| `outcome.hp_lost.mage` | 67 [24-120] n=14 |
| `outcome.hp_lost.melee` | 85 [19-122] n=9 |
| `outcome.hp_lost.range` | 58 [19-140] n=9 |
| `outcome.leaks` | 0 [0-0] n=27 |
| `outcome.phase.boss.boss_heal` | 90 [0-284] n=27 |
| `outcome.phase.boss.leaks` | 0 [0-0] n=27 |
| `outcome.phase.boss.start` | 308 [296-357] n=27 |
| `outcome.phase.boss.ticks` | 95 [75-123] n=27 |
| `outcome.phase.cleanup_end.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.cleanup_end.leaks` | 0 [0-0] n=27 |
| `outcome.phase.cleanup_end.start` | 292 [277-341] n=27 |
| `outcome.phase.cleanup_end.ticks` | 17 [16-19] n=27 |
| `outcome.phase.start.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.start.leaks` | 0 [0-0] n=27 |
| `outcome.phase.start.start` | 0 [0-0] n=27 |
| `outcome.phase.start.ticks` | 4 [4-8] n=27 |
| `outcome.phase.wave1.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.wave1.leaks` | 0 [0-0] n=27 |
| `outcome.phase.wave1.start` | 4 [4-8] n=27 |
| `outcome.phase.wave1.ticks` | 4 [4-4] n=27 |
| `outcome.phase.wave10.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.wave10.leaks` | 0 [0-0] n=27 |
| `outcome.phase.wave10.start` | 68 [68-72] n=27 |
| `outcome.phase.wave10.ticks` | 8 [8-16] n=27 |
| `outcome.phase.wave11.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.wave11.leaks` | 0 [0-0] n=27 |
| `outcome.phase.wave11.start` | 76 [76-86] n=27 |
| `outcome.phase.wave11.ticks` | 8 [8-16] n=27 |
| `outcome.phase.wave12.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.wave12.leaks` | 0 [0-0] n=27 |
| `outcome.phase.wave12.start` | 84 [84-94] n=27 |
| `outcome.phase.wave12.ticks` | 8 [8-12] n=27 |
| `outcome.phase.wave13.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.wave13.leaks` | 0 [0-0] n=27 |
| `outcome.phase.wave13.start` | 92 [92-102] n=27 |
| `outcome.phase.wave13.ticks` | 8 [8-12] n=27 |
| `outcome.phase.wave14.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.wave14.leaks` | 0 [0-0] n=27 |
| `outcome.phase.wave14.start` | 104 [100-112] n=27 |
| `outcome.phase.wave14.ticks` | 8 [8-20] n=27 |
| `outcome.phase.wave15.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.wave15.leaks` | 0 [0-0] n=27 |
| `outcome.phase.wave15.start` | 112 [108-124] n=27 |
| `outcome.phase.wave15.ticks` | 8 [8-16] n=27 |
| `outcome.phase.wave16.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.wave16.leaks` | 0 [0-0] n=27 |
| `outcome.phase.wave16.start` | 120 [116-139] n=27 |
| `outcome.phase.wave16.ticks` | 4 [4-8] n=27 |
| `outcome.phase.wave17.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.wave17.leaks` | 0 [0-0] n=27 |
| `outcome.phase.wave17.start` | 124 [120-147] n=27 |
| `outcome.phase.wave17.ticks` | 12 [12-12] n=27 |
| `outcome.phase.wave18.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.wave18.leaks` | 0 [0-0] n=27 |
| `outcome.phase.wave18.start` | 136 [132-159] n=27 |
| `outcome.phase.wave18.ticks` | 8 [8-8] n=27 |
| `outcome.phase.wave19.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.wave19.leaks` | 0 [0-0] n=27 |
| `outcome.phase.wave19.start` | 144 [140-167] n=27 |
| `outcome.phase.wave19.ticks` | 12 [12-40] n=27 |
| `outcome.phase.wave2.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.wave2.leaks` | 0 [0-0] n=27 |
| `outcome.phase.wave2.start` | 8 [8-12] n=27 |
| `outcome.phase.wave2.ticks` | 4 [4-4] n=27 |
| `outcome.phase.wave20.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.wave20.leaks` | 0 [0-0] n=27 |
| `outcome.phase.wave20.start` | 160 [152-188] n=27 |
| `outcome.phase.wave20.ticks` | 16 [16-16] n=27 |
| `outcome.phase.wave21.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.wave21.leaks` | 0 [0-0] n=27 |
| `outcome.phase.wave21.start` | 176 [168-204] n=27 |
| `outcome.phase.wave21.ticks` | 8 [8-12] n=27 |
| `outcome.phase.wave22.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.wave22.leaks` | 0 [0-0] n=27 |
| `outcome.phase.wave22.start` | 184 [176-212] n=27 |
| `outcome.phase.wave22.ticks` | 12 [12-12] n=27 |
| `outcome.phase.wave23.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.wave23.leaks` | 0 [0-0] n=27 |
| `outcome.phase.wave23.start` | 196 [188-224] n=27 |
| `outcome.phase.wave23.ticks` | 8 [8-8] n=27 |
| `outcome.phase.wave24.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.wave24.leaks` | 0 [0-0] n=27 |
| `outcome.phase.wave24.start` | 204 [196-232] n=27 |
| `outcome.phase.wave24.ticks` | 8 [8-12] n=27 |
| `outcome.phase.wave25.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.wave25.leaks` | 0 [0-0] n=27 |
| `outcome.phase.wave25.start` | 212 [204-240] n=27 |
| `outcome.phase.wave25.ticks` | 8 [8-12] n=27 |
| `outcome.phase.wave26.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.wave26.leaks` | 0 [0-0] n=27 |
| `outcome.phase.wave26.start` | 220 [212-248] n=27 |
| `outcome.phase.wave26.ticks` | 4 [4-16] n=27 |
| `outcome.phase.wave27.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.wave27.leaks` | 0 [0-0] n=27 |
| `outcome.phase.wave27.start` | 224 [216-252] n=27 |
| `outcome.phase.wave27.ticks` | 8 [8-24] n=27 |
| `outcome.phase.wave28.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.wave28.leaks` | 0 [0-0] n=27 |
| `outcome.phase.wave28.start` | 232 [224-260] n=27 |
| `outcome.phase.wave28.ticks` | 16 [4-32] n=27 |
| `outcome.phase.wave29.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.wave29.leaks` | 0 [0-0] n=27 |
| `outcome.phase.wave29.start` | 244 [228-284] n=27 |
| `outcome.phase.wave29.ticks` | 8 [4-24] n=27 |
| `outcome.phase.wave3.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.wave3.leaks` | 0 [0-0] n=27 |
| `outcome.phase.wave3.start` | 12 [12-16] n=27 |
| `outcome.phase.wave3.ticks` | 4 [4-4] n=27 |
| `outcome.phase.wave30.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.wave30.leaks` | 0 [0-0] n=27 |
| `outcome.phase.wave30.start` | 256 [240-288] n=27 |
| `outcome.phase.wave30.ticks` | 4 [4-12] n=27 |
| `outcome.phase.wave31.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.wave31.leaks` | 0 [0-0] n=27 |
| `outcome.phase.wave31.start` | 260 [244-293] n=27 |
| `outcome.phase.wave31.ticks` | 36 [23-56] n=27 |
| `outcome.phase.wave4.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.wave4.leaks` | 0 [0-0] n=27 |
| `outcome.phase.wave4.start` | 16 [16-20] n=27 |
| `outcome.phase.wave4.ticks` | 4 [4-4] n=27 |
| `outcome.phase.wave5.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.wave5.leaks` | 0 [0-0] n=27 |
| `outcome.phase.wave5.start` | 20 [20-24] n=27 |
| `outcome.phase.wave5.ticks` | 16 [16-16] n=27 |
| `outcome.phase.wave6.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.wave6.leaks` | 0 [0-0] n=27 |
| `outcome.phase.wave6.start` | 36 [36-40] n=27 |
| `outcome.phase.wave6.ticks` | 4 [4-4] n=27 |
| `outcome.phase.wave7.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.wave7.leaks` | 0 [0-0] n=27 |
| `outcome.phase.wave7.start` | 40 [40-44] n=27 |
| `outcome.phase.wave7.ticks` | 12 [12-12] n=27 |
| `outcome.phase.wave8.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.wave8.leaks` | 0 [0-0] n=27 |
| `outcome.phase.wave8.start` | 52 [52-56] n=27 |
| `outcome.phase.wave8.ticks` | 4 [4-4] n=27 |
| `outcome.phase.wave9.boss_heal` | 0 [0-0] n=27 |
| `outcome.phase.wave9.leaks` | 0 [0-0] n=27 |
| `outcome.phase.wave9.start` | 56 [56-60] n=27 |
| `outcome.phase.wave9.ticks` | 12 [12-12] n=27 |
| `outcome.room_ticks` | 412 [372-472] n=27 |
| `output.phase.boss.boss_hp_per_tick` | 20.31 [15.66-25.99] n=27 |
| `output.phase.boss.boss_pct_per_tick` | 1.083 [0.835-1.386] n=27 |
| `output.phase.cleanup_end.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.cleanup_end.boss_pct_per_tick` | 0 [0-0] n=27 |
| `output.phase.start.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.start.boss_pct_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave1.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave1.boss_pct_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave10.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave10.boss_pct_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave11.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave11.boss_pct_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave12.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave12.boss_pct_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave13.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave13.boss_pct_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave14.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave14.boss_pct_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave15.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave15.boss_pct_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave16.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave16.boss_pct_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave17.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave17.boss_pct_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave18.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave18.boss_pct_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave19.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave19.boss_pct_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave2.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave2.boss_pct_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave20.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave20.boss_pct_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave21.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave21.boss_pct_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave22.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave22.boss_pct_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave23.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave23.boss_pct_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave24.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave24.boss_pct_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave25.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave25.boss_pct_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave26.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave26.boss_pct_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave27.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave27.boss_pct_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave28.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave28.boss_pct_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave29.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave29.boss_pct_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave3.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave3.boss_pct_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave30.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave30.boss_pct_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave31.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave31.boss_pct_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave4.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave4.boss_pct_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave5.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave5.boss_pct_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave6.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave6.boss_pct_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave7.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave7.boss_pct_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave8.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave8.boss_pct_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave9.boss_hp_per_tick` | 0 [0-0] n=27 |
| `output.phase.wave9.boss_pct_per_tick` | 0 [0-0] n=27 |
| `role.mage.barrage_pct` | 4.1 [1-11.9] n=27 |
| `role.mage.boss_targeted_pct` | 31.2 [5.3-56.2] n=27 |
| `role.mage.cadence` | 3 [3-4] n=27 |
| `role.mage.eat_at_hp_pct` | 58 [13-91] n=26 |
| `role.mage.magic_pct` | 75.8 [66.3-86.3] n=27 |
| `role.mage.melee_pct` | 9.5 [4.1-18.3] n=27 |
| `role.mage.phase.boss.attacks_add` | 0 [0-0] n=27 |
| `role.mage.phase.boss.attacks_boss` | 19 [15-26] n=27 |
| `role.mage.phase.boss.dist_boss` | 4 [1-4] n=27 |
| `role.mage.phase.boss.eats` | 0 [0-2] n=14 |
| `role.mage.phase.cleanup_end.attacks_add` | 0 [0-0] n=27 |
| `role.mage.phase.cleanup_end.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.cleanup_end.dist_boss` | 4 [1-5] n=27 |
| `role.mage.phase.cleanup_end.eats` | 0 [0-1] n=14 |
| `role.mage.phase.start.attacks_add` | 0 [0-0] n=27 |
| `role.mage.phase.start.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.start.dist_boss` | 6 [3-8] n=27 |
| `role.mage.phase.start.eats` | 0 [0-0] n=14 |
| `role.mage.phase.wave1.attacks_add` | 1 [0-1] n=27 |
| `role.mage.phase.wave1.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.wave1.dist_boss` | 2.5 [2-4] n=27 |
| `role.mage.phase.wave1.eats` | 0 [0-0] n=14 |
| `role.mage.phase.wave10.attacks_add` | 2 [1-3] n=27 |
| `role.mage.phase.wave10.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.wave10.dist_boss` | 7 [1-7] n=27 |
| `role.mage.phase.wave10.eats` | 0 [0-0] n=14 |
| `role.mage.phase.wave11.attacks_add` | 2 [1-4] n=27 |
| `role.mage.phase.wave11.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.wave11.dist_boss` | 4 [2-7] n=27 |
| `role.mage.phase.wave11.eats` | 0 [0-0] n=14 |
| `role.mage.phase.wave12.attacks_add` | 1 [0-2] n=27 |
| `role.mage.phase.wave12.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.wave12.dist_boss` | 2 [1-4.5] n=27 |
| `role.mage.phase.wave12.eats` | 0 [0-0] n=14 |
| `role.mage.phase.wave13.attacks_add` | 2 [1-4] n=27 |
| `role.mage.phase.wave13.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.wave13.dist_boss` | 2 [1-4] n=27 |
| `role.mage.phase.wave13.eats` | 0 [0-0] n=14 |
| `role.mage.phase.wave14.attacks_add` | 3 [1-5] n=27 |
| `role.mage.phase.wave14.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.wave14.dist_boss` | 2 [1-3.5] n=27 |
| `role.mage.phase.wave14.eats` | 0 [0-0] n=14 |
| `role.mage.phase.wave15.attacks_add` | 3 [1-5] n=27 |
| `role.mage.phase.wave15.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.wave15.dist_boss` | 4 [1-7] n=27 |
| `role.mage.phase.wave15.eats` | 0 [0-0] n=14 |
| `role.mage.phase.wave16.attacks_add` | 1 [0-3] n=27 |
| `role.mage.phase.wave16.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.wave16.dist_boss` | 6 [2-7] n=27 |
| `role.mage.phase.wave16.eats` | 0 [0-0] n=14 |
| `role.mage.phase.wave17.attacks_add` | 4 [2-5] n=27 |
| `role.mage.phase.wave17.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.wave17.dist_boss` | 4 [1.5-6] n=27 |
| `role.mage.phase.wave17.eats` | 0 [0-1] n=14 |
| `role.mage.phase.wave18.attacks_add` | 2 [0-3] n=27 |
| `role.mage.phase.wave18.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.wave18.dist_boss` | 4 [2-6.5] n=27 |
| `role.mage.phase.wave18.eats` | 0 [0-0] n=14 |
| `role.mage.phase.wave19.attacks_add` | 4 [2-10] n=27 |
| `role.mage.phase.wave19.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.wave19.dist_boss` | 3.5 [1.5-6] n=27 |
| `role.mage.phase.wave19.eats` | 0 [0-0] n=14 |
| `role.mage.phase.wave2.attacks_add` | 0 [0-2] n=27 |
| `role.mage.phase.wave2.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.wave2.dist_boss` | 6 [1-6.5] n=27 |
| `role.mage.phase.wave2.eats` | 0 [0-0] n=14 |
| `role.mage.phase.wave20.attacks_add` | 4 [1-6] n=27 |
| `role.mage.phase.wave20.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.wave20.dist_boss` | 2 [1-3] n=27 |
| `role.mage.phase.wave20.eats` | 0 [0-0] n=14 |
| `role.mage.phase.wave21.attacks_add` | 1 [0-2] n=27 |
| `role.mage.phase.wave21.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.wave21.dist_boss` | 2 [1-4] n=27 |
| `role.mage.phase.wave21.eats` | 0 [0-0] n=14 |
| `role.mage.phase.wave22.attacks_add` | 3 [1-4] n=27 |
| `role.mage.phase.wave22.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.wave22.dist_boss` | 4.5 [1.5-7] n=27 |
| `role.mage.phase.wave22.eats` | 0 [0-0] n=14 |
| `role.mage.phase.wave23.attacks_add` | 2 [1-3] n=27 |
| `role.mage.phase.wave23.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.wave23.dist_boss` | 4 [1-7] n=27 |
| `role.mage.phase.wave23.eats` | 0 [0-0] n=14 |
| `role.mage.phase.wave24.attacks_add` | 2 [1-3] n=27 |
| `role.mage.phase.wave24.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.wave24.dist_boss` | 2.5 [1-4.5] n=27 |
| `role.mage.phase.wave24.eats` | 0 [0-0] n=14 |
| `role.mage.phase.wave25.attacks_add` | 2 [1-4] n=27 |
| `role.mage.phase.wave25.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.wave25.dist_boss` | 3 [1-5] n=27 |
| `role.mage.phase.wave25.eats` | 0 [0-0] n=14 |
| `role.mage.phase.wave26.attacks_add` | 1 [0-4] n=27 |
| `role.mage.phase.wave26.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.wave26.dist_boss` | 2 [1-4] n=27 |
| `role.mage.phase.wave26.eats` | 0 [0-0] n=14 |
| `role.mage.phase.wave27.attacks_add` | 3 [1-5] n=27 |
| `role.mage.phase.wave27.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.wave27.dist_boss` | 2 [1-4] n=27 |
| `role.mage.phase.wave27.eats` | 0 [0-0] n=14 |
| `role.mage.phase.wave28.attacks_add` | 5 [1-8] n=27 |
| `role.mage.phase.wave28.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.wave28.dist_boss` | 2 [1-6] n=27 |
| `role.mage.phase.wave28.eats` | 0 [0-0] n=14 |
| `role.mage.phase.wave29.attacks_add` | 2 [0-8] n=27 |
| `role.mage.phase.wave29.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.wave29.dist_boss` | 3 [1-7] n=27 |
| `role.mage.phase.wave29.eats` | 0 [0-0] n=14 |
| `role.mage.phase.wave3.attacks_add` | 1 [1-1] n=27 |
| `role.mage.phase.wave3.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.wave3.dist_boss` | 6 [1-6.5] n=27 |
| `role.mage.phase.wave3.eats` | 0 [0-0] n=14 |
| `role.mage.phase.wave30.attacks_add` | 2 [0-4] n=27 |
| `role.mage.phase.wave30.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.wave30.dist_boss` | 4 [1-7] n=27 |
| `role.mage.phase.wave30.eats` | 0 [0-0] n=14 |
| `role.mage.phase.wave31.attacks_add` | 10 [5-13] n=27 |
| `role.mage.phase.wave31.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.wave31.dist_boss` | 3 [2-5] n=27 |
| `role.mage.phase.wave31.eats` | 0 [0-1] n=14 |
| `role.mage.phase.wave4.attacks_add` | 1 [0-1] n=27 |
| `role.mage.phase.wave4.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.wave4.dist_boss` | 4 [2-5] n=27 |
| `role.mage.phase.wave4.eats` | 0 [0-0] n=14 |
| `role.mage.phase.wave5.attacks_add` | 4 [1-5] n=27 |
| `role.mage.phase.wave5.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.wave5.dist_boss` | 3 [1-5.5] n=27 |
| `role.mage.phase.wave5.eats` | 0 [0-0] n=14 |
| `role.mage.phase.wave6.attacks_add` | 1 [0-2] n=27 |
| `role.mage.phase.wave6.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.wave6.dist_boss` | 3 [2-4] n=27 |
| `role.mage.phase.wave6.eats` | 0 [0-0] n=14 |
| `role.mage.phase.wave7.attacks_add` | 2 [1-4] n=27 |
| `role.mage.phase.wave7.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.wave7.dist_boss` | 3 [1-4] n=27 |
| `role.mage.phase.wave7.eats` | 0 [0-0] n=14 |
| `role.mage.phase.wave8.attacks_add` | 1 [1-2] n=27 |
| `role.mage.phase.wave8.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.wave8.dist_boss` | 4 [2-4] n=27 |
| `role.mage.phase.wave8.eats` | 0 [0-0] n=14 |
| `role.mage.phase.wave9.attacks_add` | 3 [1-4] n=27 |
| `role.mage.phase.wave9.attacks_boss` | 0 [0-0] n=27 |
| `role.mage.phase.wave9.dist_boss` | 3 [1-7] n=27 |
| `role.mage.phase.wave9.eats` | 0 [0-0] n=14 |
| `role.mage.prayer.nylo_mage.lit_ticks` | 3 [0-5] n=20 |
| `role.mage.prayer.nylo_mage.right_pct` | 100 [0-100] n=22 |
| `role.mage.prayer.nylo_melee.lit_ticks` | 3.75 [1-8.5] n=22 |
| `role.mage.prayer.nylo_melee.right_pct` | 100 [0-100] n=25 |
| `role.mage.prayer.nylo_range.lit_ticks` | 4 [0-7] n=19 |
| `role.mage.prayer.nylo_range.right_pct` | 100 [0-100] n=22 |
| `role.mage.ranged_pct` | 15.4 [6.2-24.1] n=27 |
| `role.melee.barrage_pct` | 0 [0-0] n=27 |
| `role.melee.boss_targeted_pct` | 35 [12.5-50] n=27 |
| `role.melee.cadence` | 4 [4-5] n=27 |
| `role.melee.eat_at_hp_pct` | 33 [17-81] n=15 |
| `role.melee.magic_pct` | 23.1 [8.6-34.2] n=27 |
| `role.melee.melee_pct` | 62.7 [52-82.1] n=27 |
| `role.melee.phase.boss.attacks_add` | 0 [0-0] n=27 |
| `role.melee.phase.boss.attacks_boss` | 20 [15-31] n=27 |
| `role.melee.phase.boss.dist_boss` | 4 [1-4] n=27 |
| `role.melee.phase.boss.eats` | 0 [0-0] n=9 |
| `role.melee.phase.cleanup_end.attacks_add` | 0 [0-0] n=27 |
| `role.melee.phase.cleanup_end.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.cleanup_end.dist_boss` | 4 [1-4] n=27 |
| `role.melee.phase.cleanup_end.eats` | 0 [0-2] n=9 |
| `role.melee.phase.start.attacks_add` | 0 [0-0] n=27 |
| `role.melee.phase.start.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.start.dist_boss` | 6 [6-8] n=27 |
| `role.melee.phase.start.eats` | 0 [0-0] n=9 |
| `role.melee.phase.wave1.attacks_add` | 1 [0-1] n=27 |
| `role.melee.phase.wave1.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.wave1.dist_boss` | 2.5 [2-7] n=27 |
| `role.melee.phase.wave1.eats` | 0 [0-0] n=9 |
| `role.melee.phase.wave10.attacks_add` | 2 [1-4] n=27 |
| `role.melee.phase.wave10.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.wave10.dist_boss` | 3 [0.5-6] n=27 |
| `role.melee.phase.wave10.eats` | 0 [0-0] n=9 |
| `role.melee.phase.wave11.attacks_add` | 2 [1-4] n=27 |
| `role.melee.phase.wave11.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.wave11.dist_boss` | 3 [1-4.5] n=27 |
| `role.melee.phase.wave11.eats` | 0 [0-0] n=9 |
| `role.melee.phase.wave12.attacks_add` | 2 [1-3] n=27 |
| `role.melee.phase.wave12.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.wave12.dist_boss` | 4.5 [2-7] n=27 |
| `role.melee.phase.wave12.eats` | 0 [0-0] n=9 |
| `role.melee.phase.wave13.attacks_add` | 2 [2-3] n=27 |
| `role.melee.phase.wave13.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.wave13.dist_boss` | 4.5 [2-7] n=27 |
| `role.melee.phase.wave13.eats` | 0 [0-0] n=9 |
| `role.melee.phase.wave14.attacks_add` | 2 [1-5] n=27 |
| `role.melee.phase.wave14.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.wave14.dist_boss` | 3.5 [1-6] n=27 |
| `role.melee.phase.wave14.eats` | 0 [0-0] n=9 |
| `role.melee.phase.wave15.attacks_add` | 2 [1-4] n=27 |
| `role.melee.phase.wave15.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.wave15.dist_boss` | 2 [1-4] n=27 |
| `role.melee.phase.wave15.eats` | 0 [0-0] n=9 |
| `role.melee.phase.wave16.attacks_add` | 1 [0-2] n=27 |
| `role.melee.phase.wave16.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.wave16.dist_boss` | 3 [2-6] n=27 |
| `role.melee.phase.wave16.eats` | 0 [0-0] n=9 |
| `role.melee.phase.wave17.attacks_add` | 3 [2-4] n=27 |
| `role.melee.phase.wave17.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.wave17.dist_boss` | 3 [2-5] n=27 |
| `role.melee.phase.wave17.eats` | 0 [0-0] n=9 |
| `role.melee.phase.wave18.attacks_add` | 2 [0-3] n=27 |
| `role.melee.phase.wave18.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.wave18.dist_boss` | 4 [1-6] n=27 |
| `role.melee.phase.wave18.eats` | 0 [0-0] n=9 |
| `role.melee.phase.wave19.attacks_add` | 3 [1-8] n=27 |
| `role.melee.phase.wave19.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.wave19.dist_boss` | 2.5 [1-5] n=27 |
| `role.melee.phase.wave19.eats` | 0 [0-0] n=9 |
| `role.melee.phase.wave2.attacks_add` | 0 [0-1] n=27 |
| `role.melee.phase.wave2.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.wave2.dist_boss` | 6.5 [3-7] n=27 |
| `role.melee.phase.wave2.eats` | 0 [0-0] n=9 |
| `role.melee.phase.wave20.attacks_add` | 4 [1-6] n=27 |
| `role.melee.phase.wave20.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.wave20.dist_boss` | 4 [1.5-6] n=27 |
| `role.melee.phase.wave20.eats` | 0 [0-0] n=9 |
| `role.melee.phase.wave21.attacks_add` | 2 [1-3] n=27 |
| `role.melee.phase.wave21.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.wave21.dist_boss` | 3.5 [2-6] n=27 |
| `role.melee.phase.wave21.eats` | 0 [0-0] n=9 |
| `role.melee.phase.wave22.attacks_add` | 3 [1-3] n=27 |
| `role.melee.phase.wave22.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.wave22.dist_boss` | 3 [2-4.5] n=27 |
| `role.melee.phase.wave22.eats` | 0 [0-0] n=9 |
| `role.melee.phase.wave23.attacks_add` | 2 [1-3] n=27 |
| `role.melee.phase.wave23.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.wave23.dist_boss` | 3 [1-5.5] n=27 |
| `role.melee.phase.wave23.eats` | 0 [0-0] n=9 |
| `role.melee.phase.wave24.attacks_add` | 2 [1-3] n=27 |
| `role.melee.phase.wave24.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.wave24.dist_boss` | 4 [1.5-7] n=27 |
| `role.melee.phase.wave24.eats` | 0 [0-0] n=9 |
| `role.melee.phase.wave25.attacks_add` | 2 [0-3] n=27 |
| `role.melee.phase.wave25.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.wave25.dist_boss` | 4 [2-7] n=27 |
| `role.melee.phase.wave25.eats` | 0 [0-0] n=9 |
| `role.melee.phase.wave26.attacks_add` | 1 [0-4] n=27 |
| `role.melee.phase.wave26.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.wave26.dist_boss` | 2.5 [1-6] n=27 |
| `role.melee.phase.wave26.eats` | 0 [0-0] n=9 |
| `role.melee.phase.wave27.attacks_add` | 2 [1-6] n=27 |
| `role.melee.phase.wave27.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.wave27.dist_boss` | 2 [1-4] n=27 |
| `role.melee.phase.wave27.eats` | 0 [0-0] n=9 |
| `role.melee.phase.wave28.attacks_add` | 4 [0-8] n=27 |
| `role.melee.phase.wave28.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.wave28.dist_boss` | 3 [1-5] n=27 |
| `role.melee.phase.wave28.eats` | 0 [0-0] n=9 |
| `role.melee.phase.wave29.attacks_add` | 1 [0-4] n=27 |
| `role.melee.phase.wave29.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.wave29.dist_boss` | 3.5 [1.5-7] n=27 |
| `role.melee.phase.wave29.eats` | 0 [0-0] n=9 |
| `role.melee.phase.wave3.attacks_add` | 1 [1-1] n=27 |
| `role.melee.phase.wave3.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.wave3.dist_boss` | 5 [3-6] n=27 |
| `role.melee.phase.wave3.eats` | 0 [0-0] n=9 |
| `role.melee.phase.wave30.attacks_add` | 1 [0-3] n=27 |
| `role.melee.phase.wave30.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.wave30.dist_boss` | 4 [2-7] n=27 |
| `role.melee.phase.wave30.eats` | 0 [0-0] n=9 |
| `role.melee.phase.wave31.attacks_add` | 8 [3-12] n=27 |
| `role.melee.phase.wave31.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.wave31.dist_boss` | 3 [2-5] n=27 |
| `role.melee.phase.wave31.eats` | 0 [0-1] n=9 |
| `role.melee.phase.wave4.attacks_add` | 1 [1-1] n=27 |
| `role.melee.phase.wave4.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.wave4.dist_boss` | 3 [2.5-4] n=27 |
| `role.melee.phase.wave4.eats` | 0 [0-0] n=9 |
| `role.melee.phase.wave5.attacks_add` | 4 [2-4] n=27 |
| `role.melee.phase.wave5.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.wave5.dist_boss` | 3.5 [2-5] n=27 |
| `role.melee.phase.wave5.eats` | 0 [0-0] n=9 |
| `role.melee.phase.wave6.attacks_add` | 1 [0-2] n=27 |
| `role.melee.phase.wave6.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.wave6.dist_boss` | 3 [1.5-7] n=27 |
| `role.melee.phase.wave6.eats` | 0 [0-0] n=9 |
| `role.melee.phase.wave7.attacks_add` | 2 [2-4] n=27 |
| `role.melee.phase.wave7.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.wave7.dist_boss` | 6 [4.5-7] n=27 |
| `role.melee.phase.wave7.eats` | 0 [0-0] n=9 |
| `role.melee.phase.wave8.attacks_add` | 1 [0-2] n=27 |
| `role.melee.phase.wave8.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.wave8.dist_boss` | 4 [1-6] n=27 |
| `role.melee.phase.wave8.eats` | 0 [0-0] n=9 |
| `role.melee.phase.wave9.attacks_add` | 3 [2-4] n=27 |
| `role.melee.phase.wave9.attacks_boss` | 0 [0-0] n=27 |
| `role.melee.phase.wave9.dist_boss` | 3 [2-4] n=27 |
| `role.melee.phase.wave9.eats` | 0 [0-0] n=9 |
| `role.melee.prayer.nylo_mage.lit_ticks` | 3 [0-6] n=21 |
| `role.melee.prayer.nylo_mage.right_pct` | 100 [0-100] n=24 |
| `role.melee.prayer.nylo_melee.lit_ticks` | 4.5 [1-17] n=19 |
| `role.melee.prayer.nylo_melee.right_pct` | 100 [0-100] n=25 |
| `role.melee.prayer.nylo_range.lit_ticks` | 3 [0-6] n=21 |
| `role.melee.prayer.nylo_range.right_pct` | 100 [0-100] n=25 |
| `role.melee.ranged_pct` | 11.6 [5.3-25.5] n=27 |
| `role.range.barrage_pct` | 0 [0-0] n=27 |
| `role.range.boss_targeted_pct` | 33.3 [13-57.9] n=27 |
| `role.range.cadence` | 3 [2-4] n=27 |
| `role.range.eat_at_hp_pct` | 73 [1-96] n=17 |
| `role.range.magic_pct` | 14 [6-25.5] n=27 |
| `role.range.melee_pct` | 8.1 [3.8-14.3] n=27 |
| `role.range.phase.boss.attacks_add` | 0 [0-0] n=27 |
| `role.range.phase.boss.attacks_boss` | 19 [14-34] n=27 |
| `role.range.phase.boss.dist_boss` | 4 [1-4] n=27 |
| `role.range.phase.boss.eats` | 0 [0-1] n=9 |
| `role.range.phase.cleanup_end.attacks_add` | 0 [0-0] n=27 |
| `role.range.phase.cleanup_end.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.cleanup_end.dist_boss` | 4 [1-4] n=27 |
| `role.range.phase.cleanup_end.eats` | 0 [0-2] n=9 |
| `role.range.phase.start.attacks_add` | 0 [0-0] n=27 |
| `role.range.phase.start.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.start.dist_boss` | 6 [4-7] n=27 |
| `role.range.phase.start.eats` | 0 [0-0] n=9 |
| `role.range.phase.wave1.attacks_add` | 1 [0-1] n=27 |
| `role.range.phase.wave1.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.wave1.dist_boss` | 2.5 [2-4] n=27 |
| `role.range.phase.wave1.eats` | 0 [0-0] n=9 |
| `role.range.phase.wave10.attacks_add` | 2 [1-5] n=27 |
| `role.range.phase.wave10.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.wave10.dist_boss` | 2 [1-5.5] n=27 |
| `role.range.phase.wave10.eats` | 0 [0-0] n=9 |
| `role.range.phase.wave11.attacks_add` | 3 [2-7] n=27 |
| `role.range.phase.wave11.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.wave11.dist_boss` | 2 [0-4] n=27 |
| `role.range.phase.wave11.eats` | 0 [0-0] n=9 |
| `role.range.phase.wave12.attacks_add` | 2 [0-3] n=27 |
| `role.range.phase.wave12.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.wave12.dist_boss` | 3 [1-4] n=27 |
| `role.range.phase.wave12.eats` | 0 [0-0] n=9 |
| `role.range.phase.wave13.attacks_add` | 3 [1-5] n=27 |
| `role.range.phase.wave13.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.wave13.dist_boss` | 3 [1-4] n=27 |
| `role.range.phase.wave13.eats` | 0 [0-0] n=9 |
| `role.range.phase.wave14.attacks_add` | 3 [1-8] n=27 |
| `role.range.phase.wave14.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.wave14.dist_boss` | 2 [1-5] n=27 |
| `role.range.phase.wave14.eats` | 0 [0-0] n=9 |
| `role.range.phase.wave15.attacks_add` | 3 [2-8] n=27 |
| `role.range.phase.wave15.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.wave15.dist_boss` | 3 [1-7] n=27 |
| `role.range.phase.wave15.eats` | 0 [0-0] n=9 |
| `role.range.phase.wave16.attacks_add` | 2 [0-4] n=27 |
| `role.range.phase.wave16.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.wave16.dist_boss` | 2 [1-4] n=27 |
| `role.range.phase.wave16.eats` | 0 [0-0] n=9 |
| `role.range.phase.wave17.attacks_add` | 4 [3-6] n=27 |
| `role.range.phase.wave17.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.wave17.dist_boss` | 2 [1-3.5] n=27 |
| `role.range.phase.wave17.eats` | 0 [0-0] n=9 |
| `role.range.phase.wave18.attacks_add` | 2 [0-4] n=27 |
| `role.range.phase.wave18.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.wave18.dist_boss` | 2 [1-5.5] n=27 |
| `role.range.phase.wave18.eats` | 0 [0-0] n=9 |
| `role.range.phase.wave19.attacks_add` | 5 [3-16] n=27 |
| `role.range.phase.wave19.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.wave19.dist_boss` | 2 [1-3] n=27 |
| `role.range.phase.wave19.eats` | 0 [0-0] n=9 |
| `role.range.phase.wave2.attacks_add` | 1 [0-2] n=27 |
| `role.range.phase.wave2.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.wave2.dist_boss` | 4 [1.5-5] n=27 |
| `role.range.phase.wave2.eats` | 0 [0-0] n=9 |
| `role.range.phase.wave20.attacks_add` | 5 [2-7] n=27 |
| `role.range.phase.wave20.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.wave20.dist_boss` | 2 [1-4] n=27 |
| `role.range.phase.wave20.eats` | 0 [0-0] n=9 |
| `role.range.phase.wave21.attacks_add` | 2 [1-5] n=27 |
| `role.range.phase.wave21.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.wave21.dist_boss` | 4 [1.5-6] n=27 |
| `role.range.phase.wave21.eats` | 0 [0-0] n=9 |
| `role.range.phase.wave22.attacks_add` | 4 [2-6] n=27 |
| `role.range.phase.wave22.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.wave22.dist_boss` | 2 [1-5] n=27 |
| `role.range.phase.wave22.eats` | 0 [0-0] n=9 |
| `role.range.phase.wave23.attacks_add` | 2 [0-4] n=27 |
| `role.range.phase.wave23.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.wave23.dist_boss` | 3 [1-5] n=27 |
| `role.range.phase.wave23.eats` | 0 [0-0] n=9 |
| `role.range.phase.wave24.attacks_add` | 3 [2-4] n=27 |
| `role.range.phase.wave24.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.wave24.dist_boss` | 2 [1-4] n=27 |
| `role.range.phase.wave24.eats` | 0 [0-0] n=9 |
| `role.range.phase.wave25.attacks_add` | 3 [1-4] n=27 |
| `role.range.phase.wave25.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.wave25.dist_boss` | 2 [1-4] n=27 |
| `role.range.phase.wave25.eats` | 0 [0-0] n=9 |
| `role.range.phase.wave26.attacks_add` | 2 [0-6] n=27 |
| `role.range.phase.wave26.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.wave26.dist_boss` | 2 [0-5] n=27 |
| `role.range.phase.wave26.eats` | 0 [0-0] n=9 |
| `role.range.phase.wave27.attacks_add` | 3 [2-10] n=27 |
| `role.range.phase.wave27.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.wave27.dist_boss` | 2 [1-5] n=27 |
| `role.range.phase.wave27.eats` | 0 [0-0] n=9 |
| `role.range.phase.wave28.attacks_add` | 7 [1-11] n=27 |
| `role.range.phase.wave28.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.wave28.dist_boss` | 2 [0-4] n=27 |
| `role.range.phase.wave28.eats` | 0 [0-0] n=9 |
| `role.range.phase.wave29.attacks_add` | 2 [0-9] n=27 |
| `role.range.phase.wave29.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.wave29.dist_boss` | 2 [1-7] n=27 |
| `role.range.phase.wave29.eats` | 0 [0-0] n=9 |
| `role.range.phase.wave3.attacks_add` | 1 [0-2] n=27 |
| `role.range.phase.wave3.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.wave3.dist_boss` | 3 [1-5] n=27 |
| `role.range.phase.wave3.eats` | 0 [0-0] n=9 |
| `role.range.phase.wave30.attacks_add` | 2 [0-4] n=27 |
| `role.range.phase.wave30.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.wave30.dist_boss` | 2 [1-4] n=27 |
| `role.range.phase.wave30.eats` | 0 [0-0] n=9 |
| `role.range.phase.wave31.attacks_add` | 11 [6-18] n=27 |
| `role.range.phase.wave31.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.wave31.dist_boss` | 2 [1-4] n=27 |
| `role.range.phase.wave31.eats` | 0 [0-0] n=9 |
| `role.range.phase.wave4.attacks_add` | 1 [0-2] n=27 |
| `role.range.phase.wave4.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.wave4.dist_boss` | 3 [1-4.5] n=27 |
| `role.range.phase.wave4.eats` | 0 [0-0] n=9 |
| `role.range.phase.wave5.attacks_add` | 4 [2-6] n=27 |
| `role.range.phase.wave5.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.wave5.dist_boss` | 2.5 [0-4] n=27 |
| `role.range.phase.wave5.eats` | 0 [0-1] n=9 |
| `role.range.phase.wave6.attacks_add` | 1 [0-2] n=27 |
| `role.range.phase.wave6.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.wave6.dist_boss` | 3 [1-4] n=27 |
| `role.range.phase.wave6.eats` | 0 [0-0] n=9 |
| `role.range.phase.wave7.attacks_add` | 2 [1-5] n=27 |
| `role.range.phase.wave7.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.wave7.dist_boss` | 3 [2-4] n=27 |
| `role.range.phase.wave7.eats` | 0 [0-0] n=9 |
| `role.range.phase.wave8.attacks_add` | 1 [0-1] n=27 |
| `role.range.phase.wave8.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.wave8.dist_boss` | 3 [1-7] n=27 |
| `role.range.phase.wave8.eats` | 0 [0-0] n=9 |
| `role.range.phase.wave9.attacks_add` | 4 [1-5] n=27 |
| `role.range.phase.wave9.attacks_boss` | 0 [0-0] n=27 |
| `role.range.phase.wave9.dist_boss` | 2 [1-4] n=27 |
| `role.range.phase.wave9.eats` | 0 [0-0] n=9 |
| `role.range.prayer.nylo_mage.lit_ticks` | 2.5 [0-6.5] n=20 |
| `role.range.prayer.nylo_mage.right_pct` | 100 [0-100] n=23 |
| `role.range.prayer.nylo_melee.lit_ticks` | 4 [0-13] n=23 |
| `role.range.prayer.nylo_melee.right_pct` | 100 [0-100] n=26 |
| `role.range.prayer.nylo_range.lit_ticks` | 3.25 [0-6] n=24 |
| `role.range.prayer.nylo_range.right_pct` | 100 [0-100] n=26 |
| `role.range.ranged_pct` | 79.3 [65.5-85.4] n=27 |
| `react.phase.boss.mage.attack` | 3 [3-3] n=27 |
| `react.phase.boss.mage.step` | 3.5 [3-4] n=2 |
| `react.phase.boss.mage.swap` | 5 [2-11] n=27 |
| `react.phase.boss.melee.attack` | 3 [3-14] n=27 |
| `react.phase.boss.melee.step` | 3 [2-19] n=6 |
| `react.phase.boss.melee.swap` | 6 [0-11] n=27 |
| `react.phase.boss.range.attack` | 3 [3-6] n=27 |
| `react.phase.boss.range.step` | 3 [0-17] n=8 |
| `react.phase.boss.range.swap` | 6 [2-14] n=27 |
| `react.phase.cleanup_end.mage.attack` | 19 [19-20] n=15 |
| `react.phase.cleanup_end.mage.step` | 0 [0-9] n=20 |
| `react.phase.cleanup_end.mage.swap` | 3 [0-11] n=25 |
| `react.phase.cleanup_end.melee.attack` | 19 [19-20] n=14 |
| `react.phase.cleanup_end.melee.step` | 0 [0-19] n=23 |
| `react.phase.cleanup_end.melee.swap` | 6 [0-14] n=26 |
| `react.phase.cleanup_end.range.attack` | 19 [19-20] n=14 |
| `react.phase.cleanup_end.range.step` | 0 [0-19] n=20 |
| `react.phase.cleanup_end.range.swap` | 3.5 [0-16] n=24 |
| `react.phase.wave1.mage.attack` | 3 [2-4] n=27 |
| `react.phase.wave1.mage.attack_add` | 3 [2-4] n=27 |
| `react.phase.wave1.mage.step` | 0 [0-1] n=27 |
| `react.phase.wave1.mage.swap` | 19 [6-20] n=8 |
| `react.phase.wave1.melee.attack` | 3 [3-8] n=27 |
| `react.phase.wave1.melee.attack_add` | 3 [3-8] n=27 |
| `react.phase.wave1.melee.step` | 0 [0-9] n=27 |
| `react.phase.wave1.melee.swap` | 5 [0-15] n=19 |
| `react.phase.wave1.range.attack` | 1 [1-7] n=27 |
| `react.phase.wave1.range.attack_add` | 1 [1-7] n=27 |
| `react.phase.wave1.range.step` | 0 [0-2] n=27 |
| `react.phase.wave1.range.swap` | 14 [10-20] n=18 |
| `react.phase.wave10.mage.attack` | 1 [0-5] n=27 |
| `react.phase.wave10.mage.attack_add` | 1 [0-5] n=27 |
| `react.phase.wave10.mage.step` | 2 [0-14] n=27 |
| `react.phase.wave10.mage.swap` | 3 [0-19] n=27 |
| `react.phase.wave10.melee.attack` | 3 [0-6] n=27 |
| `react.phase.wave10.melee.attack_add` | 3 [0-6] n=27 |
| `react.phase.wave10.melee.step` | 0 [0-2] n=27 |
| `react.phase.wave10.melee.swap` | 7 [0-16] n=25 |
| `react.phase.wave10.range.attack` | 1 [0-5] n=27 |
| `react.phase.wave10.range.attack_add` | 1 [0-5] n=27 |
| `react.phase.wave10.range.step` | 1 [0-5] n=27 |
| `react.phase.wave10.range.swap` | 3 [0-11] n=27 |
| `react.phase.wave11.mage.attack` | 1 [0-2] n=27 |
| `react.phase.wave11.mage.attack_add` | 1 [0-2] n=27 |
| `react.phase.wave11.mage.step` | 2 [0-5] n=27 |
| `react.phase.wave11.mage.swap` | 2 [0-11] n=27 |
| `react.phase.wave11.melee.attack` | 1 [0-5] n=27 |
| `react.phase.wave11.melee.attack_add` | 1 [0-5] n=27 |
| `react.phase.wave11.melee.step` | 1 [0-4] n=27 |
| `react.phase.wave11.melee.swap` | 4 [0-15] n=25 |
| `react.phase.wave11.range.attack` | 1 [0-3] n=27 |
| `react.phase.wave11.range.attack_add` | 1 [0-3] n=27 |
| `react.phase.wave11.range.step` | 1 [0-19] n=27 |
| `react.phase.wave11.range.swap` | 2 [0-13] n=27 |
| `react.phase.wave12.mage.attack` | 3 [0-8] n=27 |
| `react.phase.wave12.mage.attack_add` | 3 [0-8] n=27 |
| `react.phase.wave12.mage.step` | 1 [0-12] n=27 |
| `react.phase.wave12.mage.swap` | 2 [0-17] n=27 |
| `react.phase.wave12.melee.attack` | 1 [0-9] n=27 |
| `react.phase.wave12.melee.attack_add` | 1 [0-9] n=27 |
| `react.phase.wave12.melee.step` | 0 [0-3] n=27 |
| `react.phase.wave12.melee.swap` | 3 [0-12] n=26 |
| `react.phase.wave12.range.attack` | 1 [0-8] n=27 |
| `react.phase.wave12.range.attack_add` | 1 [0-8] n=27 |
| `react.phase.wave12.range.step` | 1 [0-11] n=27 |
| `react.phase.wave12.range.swap` | 4 [0-10] n=25 |
| `react.phase.wave13.mage.attack` | 1 [0-4] n=27 |
| `react.phase.wave13.mage.attack_add` | 1 [0-4] n=27 |
| `react.phase.wave13.mage.step` | 1 [0-8] n=27 |
| `react.phase.wave13.mage.swap` | 2 [0-15] n=23 |
| `react.phase.wave13.melee.attack` | 0 [0-4] n=27 |
| `react.phase.wave13.melee.attack_add` | 0 [0-4] n=27 |
| `react.phase.wave13.melee.step` | 1 [0-2] n=27 |
| `react.phase.wave13.melee.swap` | 3 [0-19] n=25 |
| `react.phase.wave13.range.attack` | 0 [0-6] n=27 |
| `react.phase.wave13.range.attack_add` | 0 [0-6] n=27 |
| `react.phase.wave13.range.step` | 1 [0-7] n=27 |
| `react.phase.wave13.range.swap` | 2 [1-8] n=24 |
| `react.phase.wave14.mage.attack` | 1 [0-5] n=27 |
| `react.phase.wave14.mage.attack_add` | 1 [0-5] n=27 |
| `react.phase.wave14.mage.step` | 1 [0-15] n=27 |
| `react.phase.wave14.mage.swap` | 4 [0-15] n=17 |
| `react.phase.wave14.melee.attack` | 1 [0-4] n=27 |
| `react.phase.wave14.melee.attack_add` | 1 [0-4] n=27 |
| `react.phase.wave14.melee.step` | 0 [0-2] n=27 |
| `react.phase.wave14.melee.swap` | 7 [0-20] n=20 |
| `react.phase.wave14.range.attack` | 1 [0-7] n=27 |
| `react.phase.wave14.range.attack_add` | 1 [0-7] n=27 |
| `react.phase.wave14.range.step` | 0 [0-6] n=27 |
| `react.phase.wave14.range.swap` | 3 [0-18] n=16 |
| `react.phase.wave15.mage.attack` | 1 [0-3] n=27 |
| `react.phase.wave15.mage.attack_add` | 1 [0-3] n=27 |
| `react.phase.wave15.mage.step` | 0 [0-9] n=27 |
| `react.phase.wave15.mage.swap` | 4 [0-20] n=17 |
| `react.phase.wave15.melee.attack` | 1 [0-4] n=27 |
| `react.phase.wave15.melee.attack_add` | 1 [0-4] n=27 |
| `react.phase.wave15.melee.step` | 0 [0-2] n=27 |
| `react.phase.wave15.melee.swap` | 4 [0-18] n=19 |
| `react.phase.wave15.range.attack` | 1 [0-5] n=27 |
| `react.phase.wave15.range.attack_add` | 1 [0-5] n=27 |
| `react.phase.wave15.range.step` | 0 [0-8] n=27 |
| `react.phase.wave15.range.swap` | 3 [0-16] n=15 |
| `react.phase.wave16.mage.attack` | 1 [0-4] n=27 |
| `react.phase.wave16.mage.attack_add` | 1 [0-4] n=27 |
| `react.phase.wave16.mage.step` | 1 [0-9] n=27 |
| `react.phase.wave16.mage.swap` | 14 [0-19] n=20 |
| `react.phase.wave16.melee.attack` | 2 [0-5] n=27 |
| `react.phase.wave16.melee.attack_add` | 2 [0-5] n=27 |
| `react.phase.wave16.melee.step` | 0 [0-3] n=27 |
| `react.phase.wave16.melee.swap` | 4.5 [0-16] n=22 |
| `react.phase.wave16.range.attack` | 1 [0-8] n=27 |
| `react.phase.wave16.range.attack_add` | 1 [0-8] n=27 |
| `react.phase.wave16.range.step` | 1 [0-11] n=27 |
| `react.phase.wave16.range.swap` | 5 [0-20] n=21 |
| `react.phase.wave17.mage.attack` | 1 [0-3] n=27 |
| `react.phase.wave17.mage.attack_add` | 1 [0-3] n=27 |
| `react.phase.wave17.mage.step` | 2 [0-6] n=27 |
| `react.phase.wave17.mage.swap` | 11.5 [2-19] n=22 |
| `react.phase.wave17.melee.attack` | 2 [0-7] n=27 |
| `react.phase.wave17.melee.attack_add` | 2 [0-7] n=27 |
| `react.phase.wave17.melee.step` | 0 [0-2] n=27 |
| `react.phase.wave17.melee.swap` | 4 [0-20] n=21 |
| `react.phase.wave17.range.attack` | 1 [0-4] n=27 |
| `react.phase.wave17.range.attack_add` | 1 [0-4] n=27 |
| `react.phase.wave17.range.step` | 0 [0-10] n=27 |
| `react.phase.wave17.range.swap` | 3 [0-16] n=23 |
| `react.phase.wave18.mage.attack` | 2 [0-9] n=27 |
| `react.phase.wave18.mage.attack_add` | 2 [0-9] n=27 |
| `react.phase.wave18.mage.step` | 1 [0-7] n=26 |
| `react.phase.wave18.mage.swap` | 2 [0-12] n=22 |
| `react.phase.wave18.melee.attack` | 2 [0-10] n=27 |
| `react.phase.wave18.melee.attack_add` | 2 [0-10] n=27 |
| `react.phase.wave18.melee.step` | 0 [0-2] n=27 |
| `react.phase.wave18.melee.swap` | 4 [0-20] n=26 |
| `react.phase.wave18.range.attack` | 2 [0-11] n=27 |
| `react.phase.wave18.range.attack_add` | 2 [0-11] n=27 |
| `react.phase.wave18.range.step` | 0 [0-4] n=26 |
| `react.phase.wave18.range.swap` | 1 [0-20] n=25 |
| `react.phase.wave19.mage.attack` | 1.5 [0-3] n=26 |
| `react.phase.wave19.mage.attack_add` | 1.5 [0-3] n=26 |
| `react.phase.wave19.mage.step` | 1.5 [0-8] n=26 |
| `react.phase.wave19.mage.swap` | 4 [0-16] n=11 |
| `react.phase.wave19.melee.attack` | 2 [0-6] n=27 |
| `react.phase.wave19.melee.attack_add` | 2 [0-6] n=27 |
| `react.phase.wave19.melee.step` | 0 [0-6] n=27 |
| `react.phase.wave19.melee.swap` | 4 [0-19] n=25 |
| `react.phase.wave19.range.attack` | 1 [0-7] n=27 |
| `react.phase.wave19.range.attack_add` | 1 [0-7] n=27 |
| `react.phase.wave19.range.step` | 0 [0-20] n=25 |
| `react.phase.wave19.range.swap` | 4 [0-17] n=25 |
| `react.phase.wave2.mage.attack` | 5 [0-6] n=27 |
| `react.phase.wave2.mage.attack_add` | 5 [0-6] n=27 |
| `react.phase.wave2.mage.step` | 0 [0-3] n=27 |
| `react.phase.wave2.mage.swap` | 15 [2-19] n=11 |
| `react.phase.wave2.melee.attack` | 4 [0-5] n=27 |
| `react.phase.wave2.melee.attack_add` | 4 [0-5] n=27 |
| `react.phase.wave2.melee.step` | 0 [0-5] n=27 |
| `react.phase.wave2.melee.swap` | 1 [1-11] n=19 |
| `react.phase.wave2.range.attack` | 2 [0-4] n=27 |
| `react.phase.wave2.range.attack_add` | 2 [0-4] n=27 |
| `react.phase.wave2.range.step` | 0 [0-2] n=27 |
| `react.phase.wave2.range.swap` | 10 [6-19] n=19 |
| `react.phase.wave20.mage.attack` | 1 [0-3] n=27 |
| `react.phase.wave20.mage.attack_add` | 1 [0-3] n=27 |
| `react.phase.wave20.mage.step` | 1 [0-5] n=27 |
| `react.phase.wave20.mage.swap` | 14 [3-20] n=27 |
| `react.phase.wave20.melee.attack` | 2 [0-9] n=27 |
| `react.phase.wave20.melee.attack_add` | 2 [0-9] n=27 |
| `react.phase.wave20.melee.step` | 0 [0-8] n=27 |
| `react.phase.wave20.melee.swap` | 3 [0-12] n=24 |
| `react.phase.wave20.range.attack` | 1 [0-12] n=27 |
| `react.phase.wave20.range.attack_add` | 1 [0-12] n=27 |
| `react.phase.wave20.range.step` | 2 [0-12] n=27 |
| `react.phase.wave20.range.swap` | 4 [0-12] n=26 |
| `react.phase.wave21.mage.attack` | 2 [0-10] n=27 |
| `react.phase.wave21.mage.attack_add` | 2 [0-10] n=27 |
| `react.phase.wave21.mage.step` | 1 [0-5] n=27 |
| `react.phase.wave21.mage.swap` | 3 [0-9] n=27 |
| `react.phase.wave21.melee.attack` | 2 [0-5] n=27 |
| `react.phase.wave21.melee.attack_add` | 2 [0-5] n=27 |
| `react.phase.wave21.melee.step` | 0 [0-2] n=27 |
| `react.phase.wave21.melee.swap` | 6 [0-16] n=21 |
| `react.phase.wave21.range.attack` | 1 [0-6] n=27 |
| `react.phase.wave21.range.attack_add` | 1 [0-6] n=27 |
| `react.phase.wave21.range.step` | 0 [0-4] n=27 |
| `react.phase.wave21.range.swap` | 2.5 [0-7] n=26 |
| `react.phase.wave22.mage.attack` | 1 [0-10] n=27 |
| `react.phase.wave22.mage.attack_add` | 1 [0-10] n=27 |
| `react.phase.wave22.mage.step` | 0 [0-6] n=27 |
| `react.phase.wave22.mage.swap` | 4.5 [0-18] n=18 |
| `react.phase.wave22.melee.attack` | 1 [0-10] n=27 |
| `react.phase.wave22.melee.attack_add` | 1 [0-10] n=27 |
| `react.phase.wave22.melee.step` | 0 [0-6] n=27 |
| `react.phase.wave22.melee.swap` | 4 [0-12] n=21 |
| `react.phase.wave22.range.attack` | 0 [0-3] n=27 |
| `react.phase.wave22.range.attack_add` | 0 [0-3] n=27 |
| `react.phase.wave22.range.step` | 2 [0-8] n=27 |
| `react.phase.wave22.range.swap` | 5 [0-15] n=25 |
| `react.phase.wave23.mage.attack` | 1 [1-4] n=27 |
| `react.phase.wave23.mage.attack_add` | 1 [1-4] n=27 |
| `react.phase.wave23.mage.step` | 2 [0-5] n=27 |
| `react.phase.wave23.mage.swap` | 5.5 [0-14] n=12 |
| `react.phase.wave23.melee.attack` | 2 [1-5] n=27 |
| `react.phase.wave23.melee.attack_add` | 2 [1-5] n=27 |
| `react.phase.wave23.melee.step` | 0 [0-6] n=27 |
| `react.phase.wave23.melee.swap` | 2 [0-17] n=22 |
| `react.phase.wave23.range.attack` | 1 [0-10] n=27 |
| `react.phase.wave23.range.attack_add` | 1 [0-10] n=27 |
| `react.phase.wave23.range.step` | 1 [0-8] n=27 |
| `react.phase.wave23.range.swap` | 3 [0-10] n=23 |
| `react.phase.wave24.mage.attack` | 1 [0-3] n=27 |
| `react.phase.wave24.mage.attack_add` | 1 [0-3] n=27 |
| `react.phase.wave24.mage.step` | 1 [0-5] n=27 |
| `react.phase.wave24.mage.swap` | 6.5 [0-19] n=14 |
| `react.phase.wave24.melee.attack` | 2 [0-7] n=27 |
| `react.phase.wave24.melee.attack_add` | 2 [0-7] n=27 |
| `react.phase.wave24.melee.step` | 0 [0-3] n=27 |
| `react.phase.wave24.melee.swap` | 5 [0-9] n=22 |
| `react.phase.wave24.range.attack` | 0 [0-3] n=27 |
| `react.phase.wave24.range.attack_add` | 0 [0-3] n=27 |
| `react.phase.wave24.range.step` | 3 [0-6] n=27 |
| `react.phase.wave24.range.swap` | 2 [1-16] n=21 |
| `react.phase.wave25.mage.attack` | 1 [0-2] n=27 |
| `react.phase.wave25.mage.attack_add` | 1 [0-2] n=27 |
| `react.phase.wave25.mage.step` | 0 [0-13] n=27 |
| `react.phase.wave25.mage.swap` | 6 [0-13] n=13 |
| `react.phase.wave25.melee.attack` | 3 [0-14] n=27 |
| `react.phase.wave25.melee.attack_add` | 3 [0-14] n=27 |
| `react.phase.wave25.melee.step` | 0 [0-4] n=27 |
| `react.phase.wave25.melee.swap` | 2 [0-20] n=21 |
| `react.phase.wave25.range.attack` | 1 [0-6] n=27 |
| `react.phase.wave25.range.attack_add` | 1 [0-6] n=27 |
| `react.phase.wave25.range.step` | 2 [0-9] n=27 |
| `react.phase.wave25.range.swap` | 1 [0-18] n=21 |
| `react.phase.wave26.mage.attack` | 1 [0-4] n=27 |
| `react.phase.wave26.mage.attack_add` | 1 [0-4] n=27 |
| `react.phase.wave26.mage.step` | 1 [0-12] n=27 |
| `react.phase.wave26.mage.swap` | 1.5 [0-20] n=12 |
| `react.phase.wave26.melee.attack` | 2 [0-6] n=27 |
| `react.phase.wave26.melee.attack_add` | 2 [0-6] n=27 |
| `react.phase.wave26.melee.step` | 0 [0-3] n=27 |
| `react.phase.wave26.melee.swap` | 3 [0-19] n=22 |
| `react.phase.wave26.range.attack` | 1 [0-5] n=27 |
| `react.phase.wave26.range.attack_add` | 1 [0-5] n=27 |
| `react.phase.wave26.range.step` | 1 [0-7] n=27 |
| `react.phase.wave26.range.swap` | 4 [0-16] n=17 |
| `react.phase.wave27.mage.attack` | 0 [0-5] n=27 |
| `react.phase.wave27.mage.attack_add` | 0 [0-5] n=27 |
| `react.phase.wave27.mage.step` | 1 [0-12] n=27 |
| `react.phase.wave27.mage.swap` | 10 [1-20] n=10 |
| `react.phase.wave27.melee.attack` | 2 [0-4] n=27 |
| `react.phase.wave27.melee.attack_add` | 2 [0-4] n=27 |
| `react.phase.wave27.melee.step` | 0 [0-11] n=27 |
| `react.phase.wave27.melee.swap` | 4 [0-15] n=22 |
| `react.phase.wave27.range.attack` | 1 [0-2] n=27 |
| `react.phase.wave27.range.attack_add` | 1 [0-2] n=27 |
| `react.phase.wave27.range.step` | 0 [0-9] n=27 |
| `react.phase.wave27.range.swap` | 5 [0-19] n=17 |
| `react.phase.wave28.mage.attack` | 1 [0-5] n=27 |
| `react.phase.wave28.mage.attack_add` | 1 [0-5] n=27 |
| `react.phase.wave28.mage.step` | 1.5 [0-12] n=24 |
| `react.phase.wave28.mage.swap` | 10 [0-18] n=13 |
| `react.phase.wave28.melee.attack` | 2 [0-7] n=27 |
| `react.phase.wave28.melee.attack_add` | 2 [0-7] n=27 |
| `react.phase.wave28.melee.step` | 0 [0-4] n=27 |
| `react.phase.wave28.melee.swap` | 2 [0-19] n=21 |
| `react.phase.wave28.range.attack` | 1 [0-5] n=27 |
| `react.phase.wave28.range.attack_add` | 1 [0-5] n=27 |
| `react.phase.wave28.range.step` | 2 [0-6] n=25 |
| `react.phase.wave28.range.swap` | 4 [0-20] n=20 |
| `react.phase.wave29.mage.attack` | 2 [0-5] n=27 |
| `react.phase.wave29.mage.attack_add` | 2 [0-5] n=27 |
| `react.phase.wave29.mage.step` | 2 [0-19] n=26 |
| `react.phase.wave29.mage.swap` | 5 [0-16] n=11 |
| `react.phase.wave29.melee.attack` | 3 [0-8] n=27 |
| `react.phase.wave29.melee.attack_add` | 3 [0-8] n=27 |
| `react.phase.wave29.melee.step` | 0 [0-6] n=27 |
| `react.phase.wave29.melee.swap` | 4.5 [0-17] n=26 |
| `react.phase.wave29.range.attack` | 1 [0-9] n=27 |
| `react.phase.wave29.range.attack_add` | 1 [0-9] n=27 |
| `react.phase.wave29.range.step` | 1 [0-9] n=27 |
| `react.phase.wave29.range.swap` | 10 [0-17] n=25 |
| `react.phase.wave3.mage.attack` | 1 [0-3] n=27 |
| `react.phase.wave3.mage.attack_add` | 1 [0-3] n=27 |
| `react.phase.wave3.mage.step` | 0 [0-2] n=27 |
| `react.phase.wave3.mage.swap` | 11.5 [10-19] n=14 |
| `react.phase.wave3.melee.attack` | 0 [0-2] n=27 |
| `react.phase.wave3.melee.attack_add` | 0 [0-2] n=27 |
| `react.phase.wave3.melee.step` | 1 [0-2] n=27 |
| `react.phase.wave3.melee.swap` | 2.5 [0-11] n=18 |
| `react.phase.wave3.range.attack` | 3 [0-5] n=27 |
| `react.phase.wave3.range.attack_add` | 3 [0-5] n=27 |
| `react.phase.wave3.range.step` | 0 [0-3] n=27 |
| `react.phase.wave3.range.swap` | 6 [2-15] n=19 |
| `react.phase.wave30.mage.attack` | 2 [0-5] n=27 |
| `react.phase.wave30.mage.attack_add` | 2 [0-5] n=27 |
| `react.phase.wave30.mage.step` | 1 [0-17] n=26 |
| `react.phase.wave30.mage.swap` | 4.5 [1-19] n=14 |
| `react.phase.wave30.melee.attack` | 1 [0-6] n=27 |
| `react.phase.wave30.melee.attack_add` | 1 [0-6] n=27 |
| `react.phase.wave30.melee.step` | 0 [0-3] n=27 |
| `react.phase.wave30.melee.swap` | 3 [0-14] n=23 |
| `react.phase.wave30.range.attack` | 1 [0-5] n=27 |
| `react.phase.wave30.range.attack_add` | 1 [0-5] n=27 |
| `react.phase.wave30.range.step` | 0 [0-4] n=27 |
| `react.phase.wave30.range.swap` | 5 [0-9] n=25 |
| `react.phase.wave31.mage.attack` | 1 [0-4] n=27 |
| `react.phase.wave31.mage.attack_add` | 1 [0-4] n=27 |
| `react.phase.wave31.mage.step` | 2 [0-11] n=25 |
| `react.phase.wave31.mage.swap` | 10.5 [1-19] n=16 |
| `react.phase.wave31.melee.attack` | 2 [0-11] n=27 |
| `react.phase.wave31.melee.attack_add` | 2 [0-11] n=27 |
| `react.phase.wave31.melee.step` | 0 [0-6] n=27 |
| `react.phase.wave31.melee.swap` | 3 [0-17] n=20 |
| `react.phase.wave31.range.attack` | 1 [0-8] n=27 |
| `react.phase.wave31.range.attack_add` | 1 [0-8] n=27 |
| `react.phase.wave31.range.step` | 0 [0-17] n=27 |
| `react.phase.wave31.range.swap` | 2 [0-13] n=25 |
| `react.phase.wave4.mage.attack` | 1 [1-4] n=27 |
| `react.phase.wave4.mage.attack_add` | 1 [1-4] n=27 |
| `react.phase.wave4.mage.step` | 0 [0-2] n=27 |
| `react.phase.wave4.mage.swap` | 8 [6-20] n=15 |
| `react.phase.wave4.melee.attack` | 1 [1-3] n=27 |
| `react.phase.wave4.melee.attack_add` | 1 [1-3] n=27 |
| `react.phase.wave4.melee.step` | 0 [0-3] n=27 |
| `react.phase.wave4.melee.swap` | 3 [0-20] n=20 |
| `react.phase.wave4.range.attack` | 3 [0-5] n=27 |
| `react.phase.wave4.range.attack_add` | 3 [0-5] n=27 |
| `react.phase.wave4.range.step` | 0 [0-2] n=27 |
| `react.phase.wave4.range.swap` | 3 [1-19] n=19 |
| `react.phase.wave5.mage.attack` | 1 [0-3] n=27 |
| `react.phase.wave5.mage.attack_add` | 1 [0-3] n=27 |
| `react.phase.wave5.mage.step` | 0 [0-3] n=27 |
| `react.phase.wave5.mage.swap` | 4.5 [2-19] n=16 |
| `react.phase.wave5.melee.attack` | 1 [0-3] n=27 |
| `react.phase.wave5.melee.attack_add` | 1 [0-3] n=27 |
| `react.phase.wave5.melee.step` | 0 [0-0] n=27 |
| `react.phase.wave5.melee.swap` | 3 [0-19] n=21 |
| `react.phase.wave5.range.attack` | 1 [0-3] n=27 |
| `react.phase.wave5.range.attack_add` | 1 [0-3] n=27 |
| `react.phase.wave5.range.step` | 2 [0-7] n=27 |
| `react.phase.wave5.range.swap` | 1 [0-15] n=19 |
| `react.phase.wave6.mage.attack` | 1 [0-4] n=27 |
| `react.phase.wave6.mage.attack_add` | 1 [0-4] n=27 |
| `react.phase.wave6.mage.step` | 2 [0-5] n=27 |
| `react.phase.wave6.mage.swap` | 3 [0-20] n=18 |
| `react.phase.wave6.melee.attack` | 2 [0-8] n=27 |
| `react.phase.wave6.melee.attack_add` | 2 [0-8] n=27 |
| `react.phase.wave6.melee.step` | 0 [0-2] n=27 |
| `react.phase.wave6.melee.swap` | 4 [0-9] n=21 |
| `react.phase.wave6.range.attack` | 1 [0-7] n=27 |
| `react.phase.wave6.range.attack_add` | 1 [0-7] n=27 |
| `react.phase.wave6.range.step` | 2 [0-14] n=27 |
| `react.phase.wave6.range.swap` | 2 [0-14] n=19 |
| `react.phase.wave7.mage.attack` | 1 [0-7] n=27 |
| `react.phase.wave7.mage.attack_add` | 1 [0-7] n=27 |
| `react.phase.wave7.mage.step` | 0 [0-4] n=27 |
| `react.phase.wave7.mage.swap` | 3 [0-17] n=20 |
| `react.phase.wave7.melee.attack` | 3 [0-4] n=27 |
| `react.phase.wave7.melee.attack_add` | 3 [0-4] n=27 |
| `react.phase.wave7.melee.step` | 0 [0-6] n=27 |
| `react.phase.wave7.melee.swap` | 1 [0-5] n=21 |
| `react.phase.wave7.range.attack` | 1 [0-3] n=27 |
| `react.phase.wave7.range.attack_add` | 1 [0-3] n=27 |
| `react.phase.wave7.range.step` | 2 [0-10] n=27 |
| `react.phase.wave7.range.swap` | 3 [0-12] n=19 |
| `react.phase.wave8.mage.attack` | 1 [0-3] n=27 |
| `react.phase.wave8.mage.attack_add` | 1 [0-3] n=27 |
| `react.phase.wave8.mage.step` | 3 [0-10] n=27 |
| `react.phase.wave8.mage.swap` | 4 [0-11] n=22 |
| `react.phase.wave8.melee.attack` | 1 [0-9] n=27 |
| `react.phase.wave8.melee.attack_add` | 1 [0-9] n=27 |
| `react.phase.wave8.melee.step` | 0 [0-4] n=27 |
| `react.phase.wave8.melee.swap` | 4 [0-19] n=19 |
| `react.phase.wave8.range.attack` | 1 [0-6] n=27 |
| `react.phase.wave8.range.attack_add` | 1 [0-6] n=27 |
| `react.phase.wave8.range.step` | 2 [0-7] n=27 |
| `react.phase.wave8.range.swap` | 3 [0-20] n=27 |
| `react.phase.wave9.mage.attack` | 1 [0-3] n=27 |
| `react.phase.wave9.mage.attack_add` | 1 [0-3] n=27 |
| `react.phase.wave9.mage.step` | 2 [0-7] n=27 |
| `react.phase.wave9.mage.swap` | 3 [0-20] n=24 |
| `react.phase.wave9.melee.attack` | 1 [0-5] n=27 |
| `react.phase.wave9.melee.attack_add` | 1 [0-5] n=27 |
| `react.phase.wave9.melee.step` | 0 [0-4] n=27 |
| `react.phase.wave9.melee.swap` | 3 [0-20] n=21 |
| `react.phase.wave9.range.attack` | 1 [0-3] n=27 |
| `react.phase.wave9.range.attack_add` | 1 [0-3] n=27 |
| `react.phase.wave9.range.step` | 2 [0-3] n=27 |
| `react.phase.wave9.range.swap` | 3 [0-16] n=27 |

Weapons per role and phase (Blert's weapon name, variants merged, or the id it does not know: rooms using it / median attacks in those rooms):

- `mage|boss`: TWISTED_BOW: 27 / 5, EYE_OF_AYAK: 27 / 6, SCYTHE: 26 / 6, ZCB: 21 / 1
- `mage|wave1`: EYE_OF_AYAK: 21 / 1, SCEPTRE: 1 / 1
- `mage|wave10`: TWISTED_BOW: 17 / 1, BLOWPIPE: 17 / 1, EYE_OF_AYAK: 10 / 1, DART: 2 / 1
- `mage|wave11`: SCEPTRE: 23 / 2, EYE_OF_AYAK: 10 / 1, BLOWPIPE: 3 / 1
- `mage|wave12`: EYE_OF_AYAK: 23 / 1, SCEPTRE: 7 / 1, DART: 1 / 1, SCYTHE: 1 / 1
- `mage|wave13`: EYE_OF_AYAK: 25 / 1, SCYTHE: 10 / 1, CLAW: 4 / 1, SCEPTRE: 3 / 1
- `mage|wave14`: EYE_OF_AYAK: 27 / 2, BLOWPIPE: 3 / 1, SCYTHE: 1 / 1, CLAW: 1 / 2
- `mage|wave15`: EYE_OF_AYAK: 27 / 2, BLOWPIPE: 8 / 1.5, SCYTHE: 1 / 1
- `mage|wave16`: EYE_OF_AYAK: 26 / 1, BLOWPIPE: 1 / 1
- `mage|wave17`: EYE_OF_AYAK: 27 / 3, BLOWPIPE: 5 / 2, DART: 1 / 1, SCEPTRE: 1 / 1
- `mage|wave18`: EYE_OF_AYAK: 19 / 1, BLOWPIPE: 8 / 1, TWISTED_BOW: 5 / 1, CLAW: 1 / 1
- `mage|wave19`: EYE_OF_AYAK: 27 / 3, BLOWPIPE: 8 / 2, DART: 2 / 1
- `mage|wave2`: EYE_OF_AYAK: 9 / 1
- `mage|wave20`: EYE_OF_AYAK: 26 / 4, BLOWPIPE: 4 / 1, SCYTHE: 4 / 1, DART: 1 / 1
- `mage|wave21`: SCEPTRE: 21 / 1, EYE_OF_AYAK: 15 / 1, BLOWPIPE: 1 / 1
- `mage|wave22`: EYE_OF_AYAK: 26 / 2.5, SCYTHE: 6 / 1, TWISTED_BOW: 4 / 1, BLOWPIPE: 3 / 2
- `mage|wave23`: EYE_OF_AYAK: 26 / 2, BLOWPIPE: 5 / 1, TWISTED_BOW: 1 / 1
- `mage|wave24`: EYE_OF_AYAK: 26 / 2, BLOWPIPE: 6 / 1, TWISTED_BOW: 1 / 1, DART: 1 / 1
- `mage|wave25`: EYE_OF_AYAK: 27 / 2, BLOWPIPE: 3 / 1, SCEPTRE: 2 / 1, SCYTHE: 1 / 1
- `mage|wave26`: EYE_OF_AYAK: 23 / 1, SCYTHE: 1 / 1, SCEPTRE: 1 / 1, CLAW: 1 / 1
- `mage|wave27`: EYE_OF_AYAK: 27 / 3, SCEPTRE: 2 / 1.5, CLAW: 1 / 1, TWISTED_BOW: 1 / 1
- `mage|wave28`: EYE_OF_AYAK: 27 / 3, SCEPTRE: 8 / 2, BLOWPIPE: 1 / 2
- `mage|wave29`: EYE_OF_AYAK: 20 / 2.5, SCEPTRE: 8 / 1
- `mage|wave3`: EYE_OF_AYAK: 27 / 1
- `mage|wave30`: EYE_OF_AYAK: 24 / 2, SCEPTRE: 3 / 1
- `mage|wave31`: EYE_OF_AYAK: 27 / 7, BLOWPIPE: 9 / 2, SCEPTRE: 8 / 1.5, SCYTHE: 7 / 1
- `mage|wave4`: EYE_OF_AYAK: 26 / 1
- `mage|wave5`: EYE_OF_AYAK: 27 / 3, CLAW: 11 / 1, BLOWPIPE: 4 / 1, DART: 1 / 2
- `mage|wave6`: EYE_OF_AYAK: 24 / 1, TWISTED_BOW: 1 / 1, BLOWPIPE: 1 / 2
- `mage|wave7`: EYE_OF_AYAK: 27 / 2, TWISTED_BOW: 11 / 1
- `mage|wave8`: EYE_OF_AYAK: 27 / 1
- `mage|wave9`: EYE_OF_AYAK: 27 / 2, TWISTED_BOW: 19 / 1, BLOWPIPE: 1 / 1, SCEPTRE: 1 / 1
- `melee|boss`: EYE_OF_AYAK: 27 / 7, SCYTHE: 22 / 6, TWISTED_BOW: 22 / 4, ZCB: 19 / 1
- `melee|wave1`: EYE_OF_AYAK: 17 / 1
- `melee|wave10`: SULPHUR_BLADES: 18 / 2, EYE_OF_AYAK: 6 / 1, GLACIAL_TEMOTLI: 3 / 2, SCYTHE: 2 / 1
- `melee|wave11`: EYE_OF_AYAK: 19 / 2, SULPHUR_BLADES: 6 / 1.5, BLOWPIPE: 3 / 2, GLACIAL_TEMOTLI: 2 / 1
- `melee|wave12`: EYE_OF_AYAK: 16 / 1, SULPHUR_BLADES: 11 / 1, BLOWPIPE: 1 / 2, DUAL_MACUAHUITL: 1 / 1
- `melee|wave13`: SCYTHE: 21 / 1, SULPHUR_BLADES: 18 / 1, DUAL_MACUAHUITL: 3 / 1, GLACIAL_TEMOTLI: 3 / 2
- `melee|wave14`: SULPHUR_BLADES: 20 / 1.5, SCYTHE: 6 / 1, DUAL_MACUAHUITL: 3 / 2, GLACIAL_TEMOTLI: 3 / 2
- `melee|wave15`: SULPHUR_BLADES: 20 / 2, SCYTHE: 4 / 1, DINHS: 4 / 1, CLAW: 3 / 1
- `melee|wave16`: SULPHUR_BLADES: 17 / 1, GLACIAL_TEMOTLI: 3 / 1, SCYTHE: 2 / 1, EYE_OF_AYAK: 2 / 1.5
- `melee|wave17`: SULPHUR_BLADES: 18 / 2, EYE_OF_AYAK: 12 / 2, BLOWPIPE: 6 / 2.5, GLACIAL_TEMOTLI: 3 / 3
- `melee|wave18`: SULPHUR_BLADES: 12 / 1.5, EYE_OF_AYAK: 10 / 1, BLOWPIPE: 4 / 1, TWISTED_BOW: 2 / 1
- `melee|wave19`: EYE_OF_AYAK: 18 / 2, SULPHUR_BLADES: 13 / 2, BLOWPIPE: 6 / 2, DUAL_MACUAHUITL: 3 / 3
- `melee|wave2`: EYE_OF_AYAK: 2 / 1, TWISTED_BOW: 1 / 1
- `melee|wave20`: SULPHUR_BLADES: 19 / 2, SCYTHE: 16 / 1, EYE_OF_AYAK: 15 / 2, BLOWPIPE: 3 / 4
- `melee|wave21`: SULPHUR_BLADES: 18 / 1.5, SCYTHE: 5 / 1, EYE_OF_AYAK: 4 / 1, DUAL_MACUAHUITL: 3 / 1
- `melee|wave22`: SCYTHE: 15 / 2, SULPHUR_BLADES: 12 / 1, DINHS: 7 / 1, BLOWPIPE: 6 / 1
- `melee|wave23`: SULPHUR_BLADES: 16 / 1, EYE_OF_AYAK: 4 / 1, SCYTHE: 4 / 1, DUAL_MACUAHUITL: 3 / 2
- `melee|wave24`: SULPHUR_BLADES: 16 / 1, SCYTHE: 15 / 1, EYE_OF_AYAK: 4 / 1, GLACIAL_TEMOTLI: 3 / 2
- `melee|wave25`: SULPHUR_BLADES: 12 / 1, SCYTHE: 8 / 1, EYE_OF_AYAK: 4 / 1, DUAL_MACUAHUITL: 3 / 1
- `melee|wave26`: SCYTHE: 11 / 1, SULPHUR_BLADES: 7 / 1, GLACIAL_TEMOTLI: 3 / 1, DINHS: 2 / 1
- `melee|wave27`: SULPHUR_BLADES: 18 / 1, SCYTHE: 8 / 1, EYE_OF_AYAK: 4 / 2, DUAL_MACUAHUITL: 3 / 1
- `melee|wave28`: SULPHUR_BLADES: 16 / 3, SCYTHE: 11 / 1, EYE_OF_AYAK: 5 / 1, DUAL_MACUAHUITL: 3 / 3
- `melee|wave29`: SULPHUR_BLADES: 13 / 1, SCYTHE: 8 / 1, EYE_OF_AYAK: 6 / 1, DUAL_MACUAHUITL: 2 / 1.5
- `melee|wave3`: SULPHUR_BLADES: 16 / 1, EYE_OF_AYAK: 3 / 1, DUAL_MACUAHUITL: 3 / 1, GLACIAL_TEMOTLI: 3 / 1
- `melee|wave30`: SULPHUR_BLADES: 11 / 2, SCYTHE: 9 / 1, EYE_OF_AYAK: 3 / 1, GLACIAL_TEMOTLI: 3 / 1
- `melee|wave31`: SULPHUR_BLADES: 21 / 5, EYE_OF_AYAK: 17 / 2, SCYTHE: 10 / 1.5, BLOWPIPE: 4 / 1.5
- `melee|wave4`: EYE_OF_AYAK: 15 / 1, SULPHUR_BLADES: 8 / 1, GLACIAL_TEMOTLI: 3 / 1, DUAL_MACUAHUITL: 1 / 1
- `melee|wave5`: SULPHUR_BLADES: 21 / 3, TWISTED_BOW: 16 / 1, EYE_OF_AYAK: 3 / 1, DUAL_MACUAHUITL: 3 / 3
- `melee|wave6`: SULPHUR_BLADES: 10 / 1, EYE_OF_AYAK: 5 / 1, BLOWPIPE: 4 / 1, GLACIAL_TEMOTLI: 1 / 1
- `melee|wave7`: SULPHUR_BLADES: 20 / 1, SCYTHE: 19 / 1, DUAL_MACUAHUITL: 3 / 1, GLACIAL_TEMOTLI: 3 / 3
- `melee|wave8`: SULPHUR_BLADES: 13 / 1, EYE_OF_AYAK: 4 / 1, DUAL_MACUAHUITL: 3 / 1, GLACIAL_TEMOTLI: 2 / 1
- `melee|wave9`: SULPHUR_BLADES: 21 / 2, EYE_OF_AYAK: 8 / 1, DUAL_MACUAHUITL: 3 / 3, GLACIAL_TEMOTLI: 3 / 3
- `range|boss`: EYE_OF_AYAK: 27 / 7, SCYTHE: 23 / 5, TWISTED_BOW: 20 / 5, ZCB: 18 / 1
- `range|wave1`: TWISTED_BOW: 17 / 1, WEBWEAVER: 1 / 1
- `range|wave10`: CHIN_BLACK: 23 / 2, BLOWPIPE: 14 / 1, CHIN_RED: 3 / 2, SCYTHE: 1 / 1
- `range|wave11`: BLOWPIPE: 24 / 2, EYE_OF_AYAK: 11 / 1, CHIN_BLACK: 9 / 1, CHIN_RED: 1 / 2
- `range|wave12`: BLOWPIPE: 17 / 1, EYE_OF_AYAK: 13 / 1, TWISTED_BOW: 1 / 1, SCYTHE: 1 / 1
- `range|wave13`: BLOWPIPE: 24 / 2, SCYTHE: 18 / 1, EYE_OF_AYAK: 3 / 2
- `range|wave14`: BLOWPIPE: 27 / 3, EYE_OF_AYAK: 7 / 1, TWISTED_BOW: 1 / 1, SCYTHE: 1 / 1
- `range|wave15`: BLOWPIPE: 27 / 2, EYE_OF_AYAK: 5 / 1, CHIN_BLACK: 4 / 1
- `range|wave16`: BLOWPIPE: 22 / 2, CHIN_BLACK: 4 / 1, EYE_OF_AYAK: 1 / 1
- `range|wave17`: BLOWPIPE: 27 / 3, EYE_OF_AYAK: 16 / 2, CHIN_BLACK: 2 / 1
- `range|wave18`: BLOWPIPE: 20 / 1.5, EYE_OF_AYAK: 8 / 1, TWISTED_BOW: 5 / 1, CLAW: 1 / 1
- `range|wave19`: BLOWPIPE: 27 / 5, EYE_OF_AYAK: 12 / 1, CHIN_BLACK: 2 / 1.5, CLAW: 1 / 1
- `range|wave2`: TWISTED_BOW: 16 / 1, BLOWPIPE: 9 / 1, WEBWEAVER: 1 / 1
- `range|wave20`: BLOWPIPE: 27 / 3, EYE_OF_AYAK: 15 / 1, TWISTED_BOW: 9 / 1, SCYTHE: 8 / 1
- `range|wave21`: CHIN_BLACK: 21 / 1, BLOWPIPE: 21 / 1, EYE_OF_AYAK: 3 / 1, SCYTHE: 1 / 1
- `range|wave22`: BLOWPIPE: 24 / 4, TWISTED_BOW: 6 / 1, CHIN_BLACK: 6 / 1, CHIN_RED: 3 / 1
- `range|wave23`: BLOWPIPE: 20 / 1, TWISTED_BOW: 15 / 1, SCYTHE: 2 / 1.5, NOXIOUS_HALBERD: 1 / 1
- `range|wave24`: BLOWPIPE: 27 / 2, TWISTED_BOW: 16 / 1, CHIN_BLACK: 1 / 1, EYE_OF_AYAK: 1 / 1
- `range|wave25`: BLOWPIPE: 23 / 1, TWISTED_BOW: 11 / 1, EYE_OF_AYAK: 3 / 2, SCYTHE: 3 / 1
- `range|wave26`: BLOWPIPE: 21 / 2, EYE_OF_AYAK: 4 / 1, SCYTHE: 2 / 1
- `range|wave27`: BLOWPIPE: 27 / 3, EYE_OF_AYAK: 9 / 1, SCYTHE: 1 / 1
- `range|wave28`: BLOWPIPE: 27 / 5, EYE_OF_AYAK: 7 / 1, CHIN_BLACK: 5 / 2, TWISTED_BOW: 2 / 1
- `range|wave29`: BLOWPIPE: 23 / 3, EYE_OF_AYAK: 4 / 1, TWISTED_BOW: 1 / 1, CHIN_RED: 1 / 2
- `range|wave3`: TWISTED_BOW: 13 / 1, BLOWPIPE: 9 / 1, WEBWEAVER: 1 / 1
- `range|wave30`: BLOWPIPE: 23 / 2, CHIN_BLACK: 5 / 1, EYE_OF_AYAK: 4 / 1, TWISTED_BOW: 2 / 1
- `range|wave31`: BLOWPIPE: 27 / 8, CHIN_BLACK: 18 / 2, EYE_OF_AYAK: 16 / 2, None: 4 / 1
- `range|wave4`: BLOWPIPE: 10 / 1, TWISTED_BOW: 3 / 1, WEBWEAVER: 1 / 1
- `range|wave5`: BLOWPIPE: 26 / 3, TWISTED_BOW: 11 / 1, EYE_OF_AYAK: 5 / 1, CLAW: 3 / 1
- `range|wave6`: CHIN_BLACK: 12 / 1, BLOWPIPE: 9 / 2, SCYTHE: 1 / 1
- `range|wave7`: BLOWPIPE: 18 / 2, TWISTED_BOW: 15 / 1, EYE_OF_AYAK: 8 / 1, CHIN_BLACK: 1 / 1
- `range|wave8`: EYE_OF_AYAK: 15 / 1, BLOWPIPE: 3 / 1, SCYTHE: 1 / 1, TWISTED_BOW: 1 / 1
- `range|wave9`: BLOWPIPE: 26 / 3, CHIN_BLACK: 11 / 1, EYE_OF_AYAK: 7 / 1, TWISTED_BOW: 6 / 1

Where each role stands (offset from the boss's SW tile, share of the phase's ticks, top 3):

- `mage|boss`: (2,4) 32%, (1,4) 16%, (3,4) 11%
- `mage|cleanup_end`: (2,4) 34%, (1,4) 11%, (3,4) 10%
- `mage|start`: (1,7) 26%, (2,7) 20%, (1,5) 11%
- `mage|wave1`: (1,-3) 22%, (2,-3) 16%, (1,-1) 13%
- `mage|wave10`: (7,1) 37%, (7,2) 19%, (6,2) 11%
- `mage|wave11`: (7,1) 10%, (2,-4) 10%, (7,2) 9%
- `mage|wave12`: (2,-2) 9%, (3,2) 8%, (2,1) 6%
- `mage|wave13`: (3,2) 10%, (0,2) 7%, (0,1) 6%
- `mage|wave14`: (0,2) 14%, (-2,2) 8%, (-4,2) 7%
- `mage|wave15`: (6,2) 14%, (7,2) 12%, (7,1) 9%
- `mage|wave16`: (7,1) 23%, (7,2) 15%, (6,2) 14%
- `mage|wave17`: (2,2) 9%, (6,2) 8%, (2,1) 7%
- `mage|wave18`: (4,1) 11%, (4,2) 9%, (2,2) 9%
- `mage|wave19`: (2,1) 14%, (4,1) 10%, (3,2) 7%
- `mage|wave2`: (7,1) 18%, (7,2) 9%, (6,0) 6%
- `mage|wave20`: (1,2) 9%, (2,-4) 7%, (-1,2) 6%
- `mage|wave21`: (-4,1) 13%, (-2,2) 10%, (2,1) 10%
- `mage|wave22`: (7,2) 10%, (6,1) 9%, (4,2) 9%
- `mage|wave23`: (7,1) 17%, (5,1) 9%, (2,-4) 8%
- `mage|wave24`: (2,-4) 10%, (1,-2) 9%, (2,-2) 8%
- `mage|wave25`: (3,1) 10%, (5,1) 9%, (1,1) 9%
- `mage|wave26`: (-3,1) 18%, (-1,2) 16%, (-1,1) 14%
- `mage|wave27`: (-3,1) 15%, (-2,1) 12%, (-1,1) 11%
- `mage|wave28`: (1,2) 14%, (1,1) 12%, (2,2) 11%
- `mage|wave29`: (1,2) 11%, (1,1) 10%, (5,1) 10%
- `mage|wave3`: (7,2) 24%, (7,1) 9%, (4,-2) 8%
- `mage|wave30`: (7,1) 14%, (4,2) 12%, (3,2) 10%
- `mage|wave31`: (3,2) 10%, (2,1) 10%, (2,2) 7%
- `mage|wave4`: (2,-4) 23%, (1,-4) 9%, (2,-2) 6%
- `mage|wave5`: (-4,2) 9%, (7,1) 8%, (2,1) 5%
- `mage|wave6`: (-4,2) 36%, (-2,1) 7%, (-4,1) 6%
- `mage|wave7`: (1,-4) 14%, (2,-4) 11%, (-4,2) 10%
- `mage|wave8`: (-4,2) 32%, (-4,1) 25%, (-3,2) 14%
- `mage|wave9`: (7,2) 8%, (-4,2) 7%, (4,1) 7%
- `melee|boss`: (1,4) 28%, (2,4) 18%, (4,1) 15%
- `melee|cleanup_end`: (1,4) 20%, (2,4) 18%, (4,1) 11%
- `melee|start`: (2,7) 31%, (1,7) 15%, (2,5) 11%
- `melee|wave1`: (2,-3) 20%, (7,2) 14%, (2,1) 12%
- `melee|wave10`: (-4,2) 10%, (-3,2) 6%, (-1,3) 5%
- `melee|wave11`: (-4,2) 9%, (0,1) 6%, (-4,1) 6%
- `melee|wave12`: (7,2) 15%, (4,1) 6%, (6,1) 6%
- `melee|wave13`: (6,2) 13%, (7,2) 10%, (4,2) 4%
- `melee|wave14`: (-4,1) 4%, (0,-1) 4%, (-2,-3) 4%
- `melee|wave15`: (-3,1) 6%, (0,1) 6%, (-1,1) 5%
- `melee|wave16`: (-4,2) 8%, (-4,4) 7%, (-3,2) 6%
- `melee|wave17`: (-4,4) 5%, (-2,-2) 5%, (-1,2) 4%
- `melee|wave18`: (4,0) 6%, (2,1) 5%, (1,1) 5%
- `melee|wave19`: (0,1) 8%, (1,1) 6%, (2,1) 5%
- `melee|wave2`: (7,2) 47%, (6,1) 14%, (4,-1) 9%
- `melee|wave20`: (6,2) 4%, (6,1) 4%, (4,2) 3%
- `melee|wave21`: (1,-4) 6%, (2,-3) 4%, (1,0) 4%
- `melee|wave22`: (1,-4) 15%, (2,-4) 13%, (2,-2) 9%
- `melee|wave23`: (0,-3) 5%, (5,-2) 4%, (4,-1) 4%
- `melee|wave24`: (7,2) 10%, (7,1) 7%, (6,1) 6%
- `melee|wave25`: (5,1) 7%, (3,1) 7%, (6,2) 6%
- `melee|wave26`: (1,2) 9%, (-2,1) 8%, (-3,1) 6%
- `melee|wave27`: (-2,1) 7%, (0,2) 4%, (2,-1) 4%
- `melee|wave28`: (0,-2) 6%, (2,-1) 4%, (4,-1) 4%
- `melee|wave29`: (2,-2) 8%, (2,-1) 6%, (2,0) 4%
- `melee|wave3`: (7,2) 22%, (6,0) 8%, (5,0) 8%
- `melee|wave30`: (5,-1) 5%, (2,-4) 4%, (1,-4) 4%
- `melee|wave31`: (2,4) 4%, (3,-3) 4%, (-1,-3) 3%
- `melee|wave4`: (1,-4) 36%, (2,-4) 10%, (-1,-2) 8%
- `melee|wave5`: (2,-4) 8%, (-4,1) 8%, (1,-4) 5%
- `melee|wave6`: (7,1) 7%, (-4,4) 6%, (5,-3) 6%
- `melee|wave7`: (7,1) 20%, (7,2) 18%, (6,2) 11%
- `melee|wave8`: (4,4) 8%, (4,0) 6%, (3,-2) 5%
- `melee|wave9`: (1,-4) 11%, (0,-2) 7%, (-3,1) 6%
- `range|boss`: (2,4) 41%, (-1,2) 10%, (1,-1) 7%
- `range|cleanup_end`: (2,4) 43%, (1,4) 10%, (2,0) 8%
- `range|start`: (1,7) 32%, (2,7) 14%, (0,5) 9%
- `range|wave1`: (-4,1) 12%, (-3,2) 10%, (-4,2) 10%
- `range|wave10`: (2,2) 10%, (-2,1) 7%, (2,-4) 5%
- `range|wave11`: (0,1) 11%, (2,0) 9%, (-1,1) 8%
- `range|wave12`: (-4,1) 16%, (0,2) 9%, (-3,1) 7%
- `range|wave13`: (-4,2) 12%, (-3,1) 12%, (-4,1) 10%
- `range|wave14`: (2,-4) 9%, (1,1) 7%, (3,1) 7%
- `range|wave15`: (3,1) 7%, (7,2) 7%, (2,1) 6%
- `range|wave16`: (1,1) 16%, (2,1) 14%, (3,1) 8%
- `range|wave17`: (-1,1) 12%, (1,1) 12%, (2,1) 8%
- `range|wave18`: (0,1) 12%, (1,2) 9%, (0,2) 8%
- `range|wave19`: (1,1) 18%, (1,2) 12%, (0,1) 12%
- `range|wave2`: (4,1) 10%, (-4,1) 10%, (4,2) 8%
- `range|wave20`: (-1,1) 10%, (1,2) 9%, (1,0) 7%
- `range|wave21`: (4,1) 20%, (2,1) 6%, (5,1) 6%
- `range|wave22`: (2,1) 8%, (2,2) 6%, (4,1) 6%
- `range|wave23`: (-4,1) 10%, (-4,2) 6%, (1,1) 6%
- `range|wave24`: (-4,1) 10%, (-1,1) 10%, (-3,2) 8%
- `range|wave25`: (1,1) 12%, (2,1) 9%, (1,-4) 6%
- `range|wave26`: (2,0) 9%, (1,0) 9%, (2,1) 8%
- `range|wave27`: (2,0) 12%, (2,2) 12%, (2,1) 9%
- `range|wave28`: (1,1) 15%, (2,1) 11%, (0,1) 8%
- `range|wave29`: (2,1) 11%, (0,1) 8%, (-4,1) 8%
- `range|wave3`: (2,-4) 10%, (2,-3) 10%, (4,1) 9%
- `range|wave30`: (-4,1) 16%, (0,1) 12%, (-2,1) 10%
- `range|wave31`: (-1,1) 7%, (1,1) 7%, (3,1) 6%
- `range|wave4`: (-4,2) 13%, (2,-4) 11%, (-2,0) 10%
- `range|wave5`: (1,-4) 11%, (-4,2) 11%, (-4,1) 8%
- `range|wave6`: (1,-4) 39%, (2,0) 8%, (1,-3) 6%
- `range|wave7`: (1,-4) 29%, (1,-3) 9%, (1,-2) 9%
- `range|wave8`: (-3,2) 22%, (-4,2) 20%, (-3,1) 9%
- `range|wave9`: (-1,1) 14%, (1,1) 10%, (-3,1) 6%

## Sotetseg, normal, scale 3

20 rooms (29 candidates, 20 death-free, 0 used with a death in the room); harvested 2026-10-06; file `sotetseg_normal_3.json`.
Roles seen (by what each raider did): melee1+melee2+melee3 x19; melee1+melee2+range x1.

| Number | median [min-max] n |
|---|---|
| `boss.attacks.sote_ball` | 19 [13-25] n=20 |
| `boss.attacks.sote_death_ball` | 1 [1-2] n=20 |
| `boss.attacks.sote_melee` | 9 [1-17] n=20 |
| `boss.cadence` | 5 [5-5] n=20 |
| `boss.first_attack` | 6 [6-8] n=20 |
| `boss.hit_on_recorder.sote_ball` | 0 [0-74] n=154 |
| `boss.hit_on_recorder.sote_death_ball` | 0 [0-29] n=10 |
| `boss.hit_on_recorder.sote_melee` | 0 [0-58] n=69 |
| `outcome.boss_death_tick` | 212.5 [164-262] n=20 |
| `outcome.boss_heal` | 0 [0-19] n=20 |
| `outcome.deaths` | 0 [0-0] n=20 |
| `outcome.hp_lost.melee1` | 92.5 [27-155] n=8 |
| `outcome.hp_lost.melee2` | 108 [3-225] n=11 |
| `outcome.hp_lost.melee3` | 105 [83-135] n=5 |
| `outcome.leaks` | 0 [0-0] n=20 |
| `outcome.phase.maze1.boss_heal` | 0 [0-9] n=20 |
| `outcome.phase.maze1.leaks` | 0 [0-0] n=20 |
| `outcome.phase.maze1.start` | 52.5 [42-67] n=20 |
| `outcome.phase.maze1.ticks` | 74.5 [59-96] n=20 |
| `outcome.phase.maze2.boss_heal` | 0 [0-12] n=20 |
| `outcome.phase.maze2.leaks` | 0 [0-0] n=20 |
| `outcome.phase.maze2.start` | 128.5 [106-159] n=20 |
| `outcome.phase.maze2.ticks` | 78.5 [58-108] n=20 |
| `outcome.phase.start.boss_heal` | 0 [0-0] n=20 |
| `outcome.phase.start.leaks` | 0 [0-0] n=20 |
| `outcome.phase.start.start` | 0 [0-0] n=20 |
| `outcome.phase.start.ticks` | 52.5 [42-67] n=20 |
| `outcome.room_ticks` | 212.5 [164-262] n=20 |
| `output.phase.maze1.boss_hp_per_tick` | 12.995 [10.35-18.86] n=20 |
| `output.phase.maze1.boss_pct_per_tick` | 0.433 [0.345-0.629] n=20 |
| `output.phase.maze2.boss_hp_per_tick` | 13.095 [9.29-18.09] n=20 |
| `output.phase.maze2.boss_pct_per_tick` | 0.436 [0.31-0.603] n=20 |
| `output.phase.start.boss_hp_per_tick` | 18.275 [14.75-22.98] n=20 |
| `output.phase.start.boss_pct_per_tick` | 0.609 [0.492-0.766] n=20 |
| `role.melee1.barrage_pct` | 0 [0-3.8] n=20 |
| `role.melee1.boss_targeted_pct` | 32.15 [12.8-53.1] n=20 |
| `role.melee1.cadence` | 5 [5-5] n=20 |
| `role.melee1.eat_at_hp_pct` | 29 [5-86] n=21 |
| `role.melee1.magic_pct` | 0 [0-3.8] n=20 |
| `role.melee1.melee_pct` | 96.7 [56.4-97.1] n=20 |
| `role.melee1.phase.maze1.attacks_add` | 0 [0-0] n=20 |
| `role.melee1.phase.maze1.attacks_boss` | 10 [8-13] n=20 |
| `role.melee1.phase.maze1.dist_boss` | 5 [1-6] n=20 |
| `role.melee1.phase.maze1.eats` | 0 [0-3] n=8 |
| `role.melee1.phase.maze2.attacks_add` | 0 [0-0] n=20 |
| `role.melee1.phase.maze2.attacks_boss` | 10 [8-16] n=20 |
| `role.melee1.phase.maze2.dist_boss` | 5 [1-6] n=20 |
| `role.melee1.phase.maze2.eats` | 1 [0-3] n=8 |
| `role.melee1.phase.start.attacks_add` | 0 [0-0] n=20 |
| `role.melee1.phase.start.attacks_boss` | 10 [8-13] n=20 |
| `role.melee1.phase.start.dist_boss` | 5 [1-6] n=20 |
| `role.melee1.phase.start.eats` | 0 [0-0] n=8 |
| `role.melee1.prayer.sote_melee.lit_ticks` | 4.5 [1-6] n=7 |
| `role.melee1.prayer.sote_melee.right_pct` | 33.3 [0-80] n=13 |
| `role.melee1.ranged_pct` | 3.2 [0-43.6] n=20 |
| `role.melee2.barrage_pct` | 0 [0-6.2] n=20 |
| `role.melee2.boss_targeted_pct` | 29.6 [21.9-43.3] n=20 |
| `role.melee2.cadence` | 5 [5-5] n=20 |
| `role.melee2.eat_at_hp_pct` | 41.5 [5-97] n=30 |
| `role.melee2.magic_pct` | 0 [0-6.2] n=20 |
| `role.melee2.melee_pct` | 96.65 [93.8-100] n=20 |
| `role.melee2.phase.maze1.attacks_add` | 0 [0-0] n=20 |
| `role.melee2.phase.maze1.attacks_boss` | 10 [8-13] n=20 |
| `role.melee2.phase.maze1.dist_boss` | 4 [1-6] n=20 |
| `role.melee2.phase.maze1.eats` | 0 [0-3] n=11 |
| `role.melee2.phase.maze2.attacks_add` | 0 [0-0] n=20 |
| `role.melee2.phase.maze2.attacks_boss` | 10 [7-16] n=20 |
| `role.melee2.phase.maze2.dist_boss` | 4 [1-6] n=20 |
| `role.melee2.phase.maze2.eats` | 1 [0-2] n=11 |
| `role.melee2.phase.start.attacks_add` | 0 [0-0] n=20 |
| `role.melee2.phase.start.attacks_boss` | 9.5 [8-13] n=20 |
| `role.melee2.phase.start.dist_boss` | 4 [1-6] n=20 |
| `role.melee2.phase.start.eats` | 0 [0-0] n=11 |
| `role.melee2.prayer.sote_melee.lit_ticks` | 5 [0-11] n=11 |
| `role.melee2.prayer.sote_melee.right_pct` | 33.3 [0-100] n=15 |
| `role.melee2.ranged_pct` | 3.3 [0-4] n=20 |
| `role.melee3.barrage_pct` | 0 [0-3.8] n=19 |
| `role.melee3.boss_targeted_pct` | 35.5 [25-63] n=19 |
| `role.melee3.cadence` | 5 [5-5] n=19 |
| `role.melee3.eat_at_hp_pct` | 49 [1-89] n=17 |
| `role.melee3.magic_pct` | 0 [0-3.8] n=19 |
| `role.melee3.melee_pct` | 96.6 [95.8-100] n=19 |
| `role.melee3.phase.maze1.attacks_add` | 0 [0-0] n=19 |
| `role.melee3.phase.maze1.attacks_boss` | 9 [8-12] n=19 |
| `role.melee3.phase.maze1.dist_boss` | 4 [1-6] n=19 |
| `role.melee3.phase.maze1.eats` | 1 [0-2] n=5 |
| `role.melee3.phase.maze2.attacks_add` | 0 [0-0] n=19 |
| `role.melee3.phase.maze2.attacks_boss` | 9 [7-16] n=19 |
| `role.melee3.phase.maze2.dist_boss` | 4 [1-6] n=19 |
| `role.melee3.phase.maze2.eats` | 1 [1-2] n=5 |
| `role.melee3.phase.start.attacks_add` | 0 [0-0] n=19 |
| `role.melee3.phase.start.attacks_boss` | 9 [7-12] n=19 |
| `role.melee3.phase.start.dist_boss` | 4 [1-5] n=19 |
| `role.melee3.phase.start.eats` | 0 [0-0] n=5 |
| `role.melee3.prayer.sote_melee.lit_ticks` | 2 [1-7] n=11 |
| `role.melee3.prayer.sote_melee.right_pct` | 25 [0-100] n=18 |
| `role.melee3.ranged_pct` | 3.4 [0-4.2] n=19 |
| `role.range.barrage_pct` | 0 [0-0] n=1 |
| `role.range.boss_targeted_pct` | 34.5 [34.5-34.5] n=1 |
| `role.range.cadence` | 5 [5-5] n=1 |
| `role.range.magic_pct` | 0 [0-0] n=1 |
| `role.range.melee_pct` | 10 [10-10] n=1 |
| `role.range.phase.maze1.attacks_add` | 0 [0-0] n=1 |
| `role.range.phase.maze1.attacks_boss` | 8 [8-8] n=1 |
| `role.range.phase.maze1.dist_boss` | 5 [5-5] n=1 |
| `role.range.phase.maze2.attacks_add` | 0 [0-0] n=1 |
| `role.range.phase.maze2.attacks_boss` | 10 [10-10] n=1 |
| `role.range.phase.maze2.dist_boss` | 6 [6-6] n=1 |
| `role.range.phase.start.attacks_add` | 0 [0-0] n=1 |
| `role.range.phase.start.attacks_boss` | 12 [12-12] n=1 |
| `role.range.phase.start.dist_boss` | 5 [5-5] n=1 |
| `role.range.prayer.sote_melee.lit_ticks` | 2.5 [2.5-2.5] n=1 |
| `role.range.prayer.sote_melee.right_pct` | 66.7 [66.7-66.7] n=1 |
| `role.range.ranged_pct` | 90 [90-90] n=1 |
| `react.phase.maze1.melee1.attack` | 17 [17-17] n=1 |
| `react.phase.maze1.melee1.step` | 3 [3-20] n=11 |
| `react.phase.maze1.melee1.swap` | 14 [6-20] n=4 |
| `react.phase.maze1.melee2.attack` | 17 [17-17] n=1 |
| `react.phase.maze1.melee2.step` | 3 [3-20] n=14 |
| `react.phase.maze1.melee2.swap` | 7 [5-7] n=7 |
| `react.phase.maze1.melee3.attack` | 19 [19-19] n=1 |
| `react.phase.maze1.melee3.step` | 3 [3-3] n=12 |
| `react.phase.maze1.melee3.swap` | 7 [6-17] n=5 |
| `react.phase.maze2.melee1.attack` | 17 [17-17] n=1 |
| `react.phase.maze2.melee1.step` | 3 [3-3] n=9 |
| `react.phase.maze2.melee1.swap` | 7.5 [6-18] n=4 |
| `react.phase.maze2.melee2.attack` | 19 [19-19] n=1 |
| `react.phase.maze2.melee2.step` | 3 [3-17] n=14 |
| `react.phase.maze2.melee2.swap` | 7 [5-17] n=7 |
| `react.phase.maze2.melee3.attack` | 18 [18-18] n=1 |
| `react.phase.maze2.melee3.step` | 3 [3-3] n=13 |
| `react.phase.maze2.melee3.swap` | 8 [6-18] n=5 |

Weapons per role and phase (Blert's weapon name, variants merged, or the id it does not know: rooms using it / median attacks in those rooms):

- `melee1|maze1`: SCYTHE: 19 / 9, ELDER_MAUL: 13 / 1, CLAW: 1 / 1, TWISTED_BOW: 1 / 8
- `melee1|maze2`: SCYTHE: 19 / 9, ELDER_MAUL: 12 / 1, CHALLY: 7 / 1, ZCB: 1 / 1
- `melee1|start`: SCYTHE: 19 / 8, ELDER_MAUL: 15 / 1, TWISTED_BOW: 14 / 1, SCEPTRE: 5 / 1
- `melee2|maze1`: SCYTHE: 19 / 9, ELDER_MAUL: 13 / 1, CLAW: 1 / 1, HAMMER/HAMMER_BOP: 1 / 1
- `melee2|maze2`: SCYTHE: 19 / 9, ELDER_MAUL: 10 / 1, CHALLY: 3 / 1, CLAW: 1 / 2
- `melee2|start`: SCYTHE: 19 / 8, TWISTED_BOW: 12 / 1, ELDER_MAUL: 11 / 1, ZCB: 3 / 1
- `melee3|maze1`: SCYTHE: 18 / 8, ELDER_MAUL: 13 / 1, HAMMER/HAMMER_BOP: 1 / 1, FANG: 1 / 10
- `melee3|maze2`: SCYTHE: 18 / 8, ELDER_MAUL: 12 / 1, CHALLY: 5 / 1, FANG: 1 / 15
- `melee3|start`: SCYTHE: 18 / 8, TWISTED_BOW: 13 / 1, ELDER_MAUL: 9 / 1, ZCB: 3 / 1
- `range|maze1`: HAMMER/HAMMER_BOP: 1 / 1, TWISTED_BOW: 1 / 7
- `range|maze2`: TWISTED_BOW: 1 / 9, CLAW: 1 / 1
- `range|start`: TWISTED_BOW: 1 / 11, HAMMER/HAMMER_BOP: 1 / 1

Where each role stands (offset from the boss's SW tile, share of the phase's ticks, top 3):

- `melee1|maze1`: (5,4) 15%, (-1,0) 14%, (0,5) 12%
- `melee1|maze2`: (0,5) 18%, (-1,0) 13%, (5,4) 12%
- `melee1|start`: (0,5) 20%, (-1,0) 17%, (5,4) 16%
- `melee2|maze1`: (4,-1) 25%, (0,5) 10%, (5,4) 7%
- `melee2|maze2`: (4,-1) 27%, (0,5) 10%, (0,6) 7%
- `melee2|start`: (4,-1) 30%, (0,5) 14%, (-1,0) 10%
- `melee3|maze1`: (4,-1) 18%, (-1,0) 16%, (5,4) 10%
- `melee3|maze2`: (4,-1) 19%, (-1,0) 17%, (5,4) 16%
- `melee3|start`: (-1,0) 26%, (4,-1) 25%, (5,4) 13%
- `range|maze1`: (5,4) 70%, (3,-1) 9%, (5,0) 7%
- `range|maze2`: (6,4) 55%, (5,4) 14%, (4,-1) 5%
- `range|start`: (5,4) 53%, (6,4) 27%, (4,-18) 3%

## Verzik, normal, scale 3

20 rooms (25 candidates, 20 death-free, 0 used with a death in the room); harvested 2026-10-08; file `verzik_normal_3.json`.
Roles seen (by what each raider did): melee1+melee2+melee3 x20.

| Number | median [min-max] n |
|---|---|
| `boss.attacks.verzik_p2_bounce` | 1 [1-2] n=15 |
| `boss.attacks.verzik_p2_cabbage` | 22 [18-33] n=20 |
| `boss.attacks.verzik_p2_mage` | 9.5 [5-15] n=20 |
| `boss.attacks.verzik_p2_purple` | 1 [1-2] n=20 |
| `boss.attacks.verzik_p2_zap` | 5 [4-6] n=20 |
| `boss.attacks.verzik_p3_ball` | 1 [1-1] n=2 |
| `boss.attacks.verzik_p3_mage` | 7 [3-12] n=20 |
| `boss.attacks.verzik_p3_melee` | 1 [1-1] n=3 |
| `boss.attacks.verzik_p3_range` | 6.5 [1-11] n=20 |
| `boss.attacks.verzik_p3_webs` | 1 [1-1] n=20 |
| `boss.attacks.verzik_p3_yellows` | 1 [1-1] n=7 |
| `boss.cadence` | 4 [4-4] n=20 |
| `boss.first_attack` | 83 [74-134] n=20 |
| `boss.hit_on_recorder.verzik_p2_bounce` | 0 [0-23] n=4 |
| `boss.hit_on_recorder.verzik_p2_mage` | 0 [0-0] n=82 |
| `boss.hit_on_recorder.verzik_p2_zap` | 5 [3-48] n=46 |
| `boss.hit_on_recorder.verzik_p3_ball` | 0 [0-0] n=2 |
| `boss.hit_on_recorder.verzik_p3_mage` | 4 [0-19] n=60 |
| `boss.hit_on_recorder.verzik_p3_melee` | 0 [0-0] n=3 |
| `boss.hit_on_recorder.verzik_p3_range` | 0 [0-40] n=72 |
| `outcome.boss_death_tick` | 404 [362-568] n=20 |
| `outcome.boss_heal` | 2763.5 [2641-3137] n=20 |
| `outcome.deaths` | 0 [0-0] n=20 |
| `outcome.hp_lost.melee1` | 147.5 [113-246] n=14 |
| `outcome.hp_lost.melee2` | 181 [117-212] n=5 |
| `outcome.hp_lost.melee3` | 127 [99-200] n=5 |
| `outcome.leaks` | 0 [0-0] n=20 |
| `outcome.phase.phase2.boss_heal` | 158 [115-500] n=20 |
| `outcome.phase.phase2.leaks` | 0 [0-0] n=20 |
| `outcome.phase.phase2.start` | 67 [58-118] n=20 |
| `outcome.phase.phase2.ticks` | 199.5 [166-256] n=20 |
| `outcome.phase.phase3.boss_heal` | 2547 [2507-2783] n=20 |
| `outcome.phase.phase3.leaks` | 0 [0-0] n=20 |
| `outcome.phase.phase3.start` | 266.5 [231-365] n=20 |
| `outcome.phase.phase3.ticks` | 138.5 [120-211] n=20 |
| `outcome.phase.start.boss_heal` | 0 [0-0] n=20 |
| `outcome.phase.start.leaks` | 0 [0-0] n=20 |
| `outcome.phase.start.start` | 0 [0-0] n=20 |
| `outcome.phase.start.ticks` | 67 [58-118] n=20 |
| `outcome.room_ticks` | 404 [362-568] n=20 |
| `output.phase.phase2.boss_hp_per_tick` | 13.155 [10.65-15.16] n=20 |
| `output.phase.phase2.boss_pct_per_tick` | 0.54 [0.437-0.622] n=20 |
| `output.phase.phase3.boss_hp_per_tick` | 18.525 [12.96-21.19] n=20 |
| `output.phase.phase3.boss_pct_per_tick` | 0.76 [0.532-0.87] n=20 |
| `output.phase.start.boss_hp_per_tick` | 0 [0-0] n=20 |
| `output.phase.start.boss_pct_per_tick` | 0 [0-0] n=20 |
| `role.melee1.barrage_pct` | 0 [0-2.4] n=20 |
| `role.melee1.boss_targeted_pct` | 21.95 [10.7-70.4] n=20 |
| `role.melee1.cadence` | 5 [5-5] n=20 |
| `role.melee1.eat_at_hp_pct` | 62 [8-100] n=90 |
| `role.melee1.magic_pct` | 8.8 [6-11.5] n=20 |
| `role.melee1.melee_pct` | 88.35 [77.5-94] n=20 |
| `role.melee1.phase.phase2.attacks_add` | 6 [2-15] n=20 |
| `role.melee1.phase.phase2.attacks_boss` | 29 [25-37] n=20 |
| `role.melee1.phase.phase2.dist_boss` | 2 [2-4] n=20 |
| `role.melee1.phase.phase2.eats` | 1 [0-5] n=14 |
| `role.melee1.phase.phase3.attacks_add` | 0 [0-0] n=20 |
| `role.melee1.phase.phase3.attacks_boss` | 25 [22-34] n=20 |
| `role.melee1.phase.phase3.dist_boss` | 5 [4-5] n=20 |
| `role.melee1.phase.phase3.eats` | 2 [0-8] n=14 |
| `role.melee1.phase.start.attacks_add` | 0 [0-0] n=20 |
| `role.melee1.phase.start.attacks_boss` | 14 [12-23] n=20 |
| `role.melee1.phase.start.dist_boss` | 9 [5-9] n=20 |
| `role.melee1.phase.start.eats` | 0 [0-4] n=14 |
| `role.melee1.prayer.verzik_p2_mage.lit_ticks` | 31 [5-50] n=19 |
| `role.melee1.prayer.verzik_p2_mage.right_pct` | 100 [100-100] n=19 |
| `role.melee1.prayer.verzik_p3_mage.lit_ticks` | 23.5 [5-50] n=6 |
| `role.melee1.prayer.verzik_p3_mage.right_pct` | 63.35 [50-83.3] n=6 |
| `role.melee1.prayer.verzik_p3_melee.right_pct` | 0 [0-0] n=1 |
| `role.melee1.prayer.verzik_p3_range.lit_ticks` | 14.5 [4-50] n=6 |
| `role.melee1.prayer.verzik_p3_range.right_pct` | 69.45 [14.3-85.7] n=6 |
| `role.melee1.ranged_pct` | 1.45 [0-14.6] n=20 |
| `role.melee2.barrage_pct` | 0 [0-4.7] n=20 |
| `role.melee2.boss_targeted_pct` | 19.25 [4.5-66.7] n=20 |
| `role.melee2.cadence` | 5 [5-5] n=20 |
| `role.melee2.eat_at_hp_pct` | 67 [14-109] n=44 |
| `role.melee2.magic_pct` | 6.35 [4.3-17.6] n=20 |
| `role.melee2.melee_pct` | 92.45 [82.4-95.6] n=20 |
| `role.melee2.phase.phase2.attacks_add` | 4.5 [2-9] n=20 |
| `role.melee2.phase.phase2.attacks_boss` | 30 [25-37] n=20 |
| `role.melee2.phase.phase2.dist_boss` | 3 [1-4] n=20 |
| `role.melee2.phase.phase2.eats` | 2 [0-3] n=5 |
| `role.melee2.phase.phase3.attacks_add` | 0 [0-2] n=20 |
| `role.melee2.phase.phase3.attacks_boss` | 24.5 [21-33] n=20 |
| `role.melee2.phase.phase3.dist_boss` | 5 [3-5] n=20 |
| `role.melee2.phase.phase3.eats` | 3 [0-7] n=5 |
| `role.melee2.phase.start.attacks_add` | 0 [0-0] n=20 |
| `role.melee2.phase.start.attacks_boss` | 13 [11-24] n=20 |
| `role.melee2.phase.start.dist_boss` | 9 [5-9] n=20 |
| `role.melee2.phase.start.eats` | 3 [0-6] n=5 |
| `role.melee2.prayer.verzik_p2_mage.lit_ticks` | 45.75 [15-50] n=18 |
| `role.melee2.prayer.verzik_p2_mage.right_pct` | 100 [100-100] n=18 |
| `role.melee2.prayer.verzik_p3_mage.lit_ticks` | 12 [10-33] n=3 |
| `role.melee2.prayer.verzik_p3_mage.right_pct` | 55.6 [42.9-90] n=3 |
| `role.melee2.prayer.verzik_p3_melee.right_pct` | 0 [0-0] n=1 |
| `role.melee2.prayer.verzik_p3_range.lit_ticks` | 6.75 [5-8.5] n=2 |
| `role.melee2.prayer.verzik_p3_range.right_pct` | 40 [0-42.9] n=3 |
| `role.melee2.ranged_pct` | 0 [0-2.9] n=20 |
| `role.melee3.barrage_pct` | 0 [0-1.1] n=20 |
| `role.melee3.boss_targeted_pct` | 47 [11.1-71.4] n=20 |
| `role.melee3.cadence` | 5 [5-5] n=20 |
| `role.melee3.eat_at_hp_pct` | 56.5 [1-101] n=26 |
| `role.melee3.magic_pct` | 7.35 [3.4-18.5] n=20 |
| `role.melee3.melee_pct` | 92.3 [81.5-95.6] n=20 |
| `role.melee3.phase.phase2.attacks_add` | 3.5 [2-14] n=20 |
| `role.melee3.phase.phase2.attacks_boss` | 30 [25-34] n=20 |
| `role.melee3.phase.phase2.dist_boss` | 3 [1-4] n=20 |
| `role.melee3.phase.phase2.eats` | 1 [0-4] n=5 |
| `role.melee3.phase.phase3.attacks_add` | 0 [0-1] n=20 |
| `role.melee3.phase.phase3.attacks_boss` | 24 [20-32] n=20 |
| `role.melee3.phase.phase3.dist_boss` | 5 [3-5] n=20 |
| `role.melee3.phase.phase3.eats` | 0 [0-4] n=5 |
| `role.melee3.phase.start.attacks_add` | 0 [0-0] n=20 |
| `role.melee3.phase.start.attacks_boss` | 13 [11-23] n=20 |
| `role.melee3.phase.start.dist_boss` | 9 [5-9] n=20 |
| `role.melee3.phase.start.eats` | 1 [0-3] n=5 |
| `role.melee3.prayer.verzik_p2_mage.lit_ticks` | 33.5 [3-50] n=20 |
| `role.melee3.prayer.verzik_p2_mage.right_pct` | 100 [100-100] n=20 |
| `role.melee3.prayer.verzik_p3_mage.lit_ticks` | 36.25 [5-50] n=10 |
| `role.melee3.prayer.verzik_p3_mage.right_pct` | 75 [0-91.7] n=11 |
| `role.melee3.prayer.verzik_p3_melee.right_pct` | 0 [0-0] n=1 |
| `role.melee3.prayer.verzik_p3_range.lit_ticks` | 12 [4-50] n=9 |
| `role.melee3.prayer.verzik_p3_range.right_pct` | 60 [0-80] n=11 |
| `role.melee3.ranged_pct` | 0 [0-4.5] n=20 |
| `react.phase.phase2.melee1.attack` | 14 [14-14] n=19 |
| `react.phase.phase2.melee1.step` | 0 [0-9] n=20 |
| `react.phase.phase2.melee1.swap` | 2 [0-10] n=5 |
| `react.phase.phase2.melee2.attack` | 14 [14-15] n=19 |
| `react.phase.phase2.melee2.step` | 0 [0-15] n=20 |
| `react.phase.phase2.melee2.swap` | 2 [0-3] n=3 |
| `react.phase.phase2.melee3.attack` | 14 [14-20] n=19 |
| `react.phase.phase2.melee3.step` | 0 [0-14] n=20 |
| `react.phase.phase2.melee3.swap` | 0 [0-0] n=2 |
| `react.phase.phase3.melee1.attack` | 7 [7-9] n=20 |
| `react.phase.phase3.melee1.step` | 1 [0-7] n=20 |
| `react.phase.phase3.melee1.swap` | 10.5 [8-13] n=2 |
| `react.phase.phase3.melee2.attack` | 7 [7-8] n=20 |
| `react.phase.phase3.melee2.step` | 1 [0-3] n=20 |
| `react.phase.phase3.melee2.swap` | 8 [8-8] n=3 |
| `react.phase.phase3.melee3.attack` | 7 [7-11] n=20 |
| `react.phase.phase3.melee3.step` | 1 [0-4] n=20 |
| `react.phase.phase3.melee3.swap` | 0 [0-0] n=1 |

Weapons per role and phase (Blert's weapon name, variants merged, or the id it does not know: rooms using it / median attacks in those rooms):

- `melee1|phase2`: SCYTHE: 20 / 32, BLOWPIPE: 17 / 2, CLAW: 15 / 1, SULPHUR_BLADES: 3 / 1
- `melee1|phase3`: SCYTHE: 20 / 23, CLAW: 13 / 1, CHALLY: 5 / 1, ZCB: 3 / 1
- `melee1|start`: SCYTHE: 20 / 8.5, DAWN: 20 / 4, EYE_OF_AYAK: 19 / 2, CLAW: 3 / 1
- `melee2|phase2`: SCYTHE: 18 / 33, CLAW: 11 / 1, BLOWPIPE: 6 / 1, NOXIOUS_HALBERD: 2 / 41.5
- `melee2|phase3`: SCYTHE: 18 / 23, CLAW: 13 / 1, ZCB: 3 / 1, NOXIOUS_HALBERD: 2 / 28.5
- `melee2|start`: DAWN: 20 / 4, SCYTHE: 18 / 9, EYE_OF_AYAK: 10 / 4, SULPHUR_BLADES: 1 / 11
- `melee3|phase2`: SCYTHE: 18 / 31, CLAW: 7 / 1, BLOWPIPE: 7 / 1, CHALLY: 3 / 1
- `melee3|phase3`: SCYTHE: 18 / 23, CLAW: 13 / 1, CHALLY: 10 / 1, NOXIOUS_HALBERD: 1 / 30
- `melee3|start`: DAWN: 20 / 4, SCYTHE: 18 / 9, EYE_OF_AYAK: 12 / 1.5, CLAW: 3 / 1

Where each role stands (offset from the boss's SW tile, share of the phase's ticks, top 3):

- `melee1|phase2`: (-1,2) 31%, (-2,2) 9%, (3,2) 6%
- `melee1|phase3`: (0,5) 8%, (1,-5) 8%, (5,1) 5%
- `melee1|start`: (-1,9) 55%, (-5,4) 10%, (-3,4) 8%
- `melee2|phase2`: (3,1) 10%, (-1,2) 8%, (1,-1) 8%
- `melee2|phase3`: (0,5) 9%, (1,-5) 8%, (1,-3) 4%
- `melee2|start`: (-1,9) 52%, (-5,4) 12%, (-3,4) 11%
- `melee3|phase2`: (-1,2) 10%, (3,1) 10%, (-1,1) 7%
- `melee3|phase3`: (1,-5) 7%, (0,5) 6%, (5,2) 4%
- `melee3|start`: (-1,9) 47%, (-5,4) 12%, (-3,4) 10%

## Xarpus, normal, scale 3

13 rooms (20 candidates, 13 death-free, 0 used with a death in the room); harvested 2026-10-06; file `xarpus_normal_3.json`.
Roles seen (by what each raider did): melee1+melee2+melee3 x12; melee+range1+range2 x1.

| Number | median [min-max] n |
|---|---|
| `boss.attacks.xarpus_spit` | 27 [23-30] n=13 |
| `boss.attacks.xarpus_turn` | 6 [5-8] n=13 |
| `boss.cadence` | 4 [4-4] n=13 |
| `boss.first_attack` | 123 [123-124] n=13 |
| `outcome.boss_death_tick` | 276 [257-298] n=13 |
| `outcome.boss_heal` | 418 [151-505] n=13 |
| `outcome.deaths` | 0 [0-0] n=13 |
| `outcome.hp_lost.melee` | 86 [86-86] n=1 |
| `outcome.hp_lost.melee1` | 69 [30-81] n=6 |
| `outcome.hp_lost.melee2` | 68 [58-99] n=3 |
| `outcome.hp_lost.melee3` | 77 [45-108] n=5 |
| `outcome.leaks` | 0 [0-0] n=13 |
| `outcome.phase.phase1.boss_heal` | 208 [7-308] n=13 |
| `outcome.phase.phase1.leaks` | 0 [0-0] n=13 |
| `outcome.phase.phase1.start` | 116 [116-117] n=13 |
| `outcome.phase.phase1.ticks` | 112 [96-124] n=13 |
| `outcome.phase.phase2.boss_heal` | 94 [25-139] n=13 |
| `outcome.phase.phase2.leaks` | 0 [0-0] n=13 |
| `outcome.phase.phase2.start` | 228 [212-240] n=13 |
| `outcome.phase.phase2.ticks` | 51 [45-70] n=13 |
| `outcome.phase.start.boss_heal` | 96 [50-182] n=13 |
| `outcome.phase.start.leaks` | 0 [0-0] n=13 |
| `outcome.phase.start.start` | 0 [0-0] n=13 |
| `outcome.phase.start.ticks` | 116 [116-117] n=13 |
| `outcome.room_ticks` | 276 [257-298] n=13 |
| `output.phase.phase1.boss_hp_per_tick` | 20.54 [17.52-23.91] n=13 |
| `output.phase.phase1.boss_pct_per_tick` | 0.539 [0.46-0.628] n=13 |
| `output.phase.phase2.boss_hp_per_tick` | 19.71 [15.5-22.53] n=13 |
| `output.phase.phase2.boss_pct_per_tick` | 0.517 [0.407-0.591] n=13 |
| `output.phase.start.boss_hp_per_tick` | 7.84 [7.68-8.01] n=13 |
| `output.phase.start.boss_pct_per_tick` | 0.206 [0.201-0.21] n=13 |
| `role.melee.barrage_pct` | 0 [0-0] n=1 |
| `role.melee.cadence` | 5 [5-5] n=1 |
| `role.melee.eat_at_hp_pct` | 35 [35-35] n=1 |
| `role.melee.magic_pct` | 0 [0-0] n=1 |
| `role.melee.melee_pct` | 97.1 [97.1-97.1] n=1 |
| `role.melee.phase.phase1.attacks_add` | 0 [0-0] n=1 |
| `role.melee.phase.phase1.attacks_boss` | 24 [24-24] n=1 |
| `role.melee.phase.phase1.dist_boss` | 3 [3-3] n=1 |
| `role.melee.phase.phase1.eats` | 0 [0-0] n=1 |
| `role.melee.phase.phase2.attacks_add` | 0 [0-0] n=1 |
| `role.melee.phase.phase2.attacks_boss` | 10 [10-10] n=1 |
| `role.melee.phase.phase2.dist_boss` | 2 [2-2] n=1 |
| `role.melee.phase.phase2.eats` | 0 [0-0] n=1 |
| `role.melee.phase.start.attacks_add` | 0 [0-0] n=1 |
| `role.melee.phase.start.attacks_boss` | 0 [0-0] n=1 |
| `role.melee.phase.start.dist_boss` | 2 [2-2] n=1 |
| `role.melee.phase.start.eats` | 0 [0-0] n=1 |
| `role.melee.ranged_pct` | 2.9 [2.9-2.9] n=1 |
| `role.melee1.barrage_pct` | 0 [0-0] n=12 |
| `role.melee1.cadence` | 5 [4-5] n=12 |
| `role.melee1.eat_at_hp_pct` | 36.5 [23-77] n=6 |
| `role.melee1.magic_pct` | 0 [0-3.2] n=12 |
| `role.melee1.melee_pct` | 96.8 [93.5-100] n=12 |
| `role.melee1.phase.phase1.attacks_add` | 0 [0-0] n=12 |
| `role.melee1.phase.phase1.attacks_boss` | 21.5 [19-29] n=12 |
| `role.melee1.phase.phase1.dist_boss` | 4 [2-4] n=12 |
| `role.melee1.phase.phase1.eats` | 0 [0-0] n=6 |
| `role.melee1.phase.phase2.attacks_add` | 0 [0-0] n=12 |
| `role.melee1.phase.phase2.attacks_boss` | 10 [9-12] n=12 |
| `role.melee1.phase.phase2.dist_boss` | 2.5 [2-4] n=12 |
| `role.melee1.phase.phase2.eats` | 0 [0-0] n=6 |
| `role.melee1.phase.start.attacks_add` | 0 [0-0] n=12 |
| `role.melee1.phase.start.attacks_boss` | 0 [0-0] n=12 |
| `role.melee1.phase.start.dist_boss` | 4 [2-5] n=12 |
| `role.melee1.phase.start.eats` | 0 [0-0] n=6 |
| `role.melee1.ranged_pct` | 3.2 [0-3.4] n=12 |
| `role.melee2.barrage_pct` | 0 [0-0] n=12 |
| `role.melee2.cadence` | 5 [5-5] n=12 |
| `role.melee2.eat_at_hp_pct` | 38 [6-48] n=3 |
| `role.melee2.magic_pct` | 0 [0-0] n=12 |
| `role.melee2.melee_pct` | 96.9 [96.3-100] n=12 |
| `role.melee2.phase.phase1.attacks_add` | 0 [0-0] n=12 |
| `role.melee2.phase.phase1.attacks_boss` | 21.5 [19-24] n=12 |
| `role.melee2.phase.phase1.dist_boss` | 3.75 [2-4] n=12 |
| `role.melee2.phase.phase1.eats` | 0 [0-0] n=3 |
| `role.melee2.phase.phase2.attacks_add` | 0 [0-0] n=12 |
| `role.melee2.phase.phase2.attacks_boss` | 9 [8-12] n=12 |
| `role.melee2.phase.phase2.dist_boss` | 2.75 [2-4] n=12 |
| `role.melee2.phase.phase2.eats` | 0 [0-0] n=3 |
| `role.melee2.phase.start.attacks_add` | 0 [0-0] n=12 |
| `role.melee2.phase.start.attacks_boss` | 0 [0-0] n=12 |
| `role.melee2.phase.start.dist_boss` | 3 [2-5] n=12 |
| `role.melee2.phase.start.eats` | 0 [0-0] n=3 |
| `role.melee2.ranged_pct` | 3.1 [0-3.7] n=12 |
| `role.melee3.barrage_pct` | 0 [0-0] n=12 |
| `role.melee3.cadence` | 5 [5-5] n=12 |
| `role.melee3.eat_at_hp_pct` | 25 [6-57] n=7 |
| `role.melee3.magic_pct` | 0 [0-0] n=12 |
| `role.melee3.melee_pct` | 100 [96.3-100] n=12 |
| `role.melee3.phase.phase1.attacks_add` | 0 [0-0] n=12 |
| `role.melee3.phase.phase1.attacks_boss` | 21 [19-24] n=12 |
| `role.melee3.phase.phase1.dist_boss` | 4 [2-4] n=12 |
| `role.melee3.phase.phase1.eats` | 0 [0-0] n=5 |
| `role.melee3.phase.phase2.attacks_add` | 0 [0-0] n=12 |
| `role.melee3.phase.phase2.attacks_boss` | 9 [8-10] n=12 |
| `role.melee3.phase.phase2.dist_boss` | 2.25 [2-4] n=12 |
| `role.melee3.phase.phase2.eats` | 0 [0-2] n=5 |
| `role.melee3.phase.start.attacks_add` | 0 [0-0] n=12 |
| `role.melee3.phase.start.attacks_boss` | 0 [0-0] n=12 |
| `role.melee3.phase.start.dist_boss` | 4 [2-5] n=12 |
| `role.melee3.phase.start.eats` | 0 [0-0] n=5 |
| `role.melee3.ranged_pct` | 0 [0-3.7] n=12 |
| `role.range1.barrage_pct` | 0 [0-0] n=1 |
| `role.range1.cadence` | 2 [2-2] n=1 |
| `role.range1.magic_pct` | 0 [0-0] n=1 |
| `role.range1.melee_pct` | 3.9 [3.9-3.9] n=1 |
| `role.range1.phase.phase1.attacks_add` | 0 [0-0] n=1 |
| `role.range1.phase.phase1.attacks_boss` | 57 [57-57] n=1 |
| `role.range1.phase.phase1.dist_boss` | 8 [8-8] n=1 |
| `role.range1.phase.phase2.attacks_add` | 0 [0-0] n=1 |
| `role.range1.phase.phase2.attacks_boss` | 19 [19-19] n=1 |
| `role.range1.phase.phase2.dist_boss` | 4 [4-4] n=1 |
| `role.range1.phase.start.attacks_add` | 0 [0-0] n=1 |
| `role.range1.phase.start.attacks_boss` | 0 [0-0] n=1 |
| `role.range1.phase.start.dist_boss` | 5 [5-5] n=1 |
| `role.range1.ranged_pct` | 96.1 [96.1-96.1] n=1 |
| `role.range2.barrage_pct` | 0 [0-0] n=1 |
| `role.range2.cadence` | 2 [2-2] n=1 |
| `role.range2.magic_pct` | 0 [0-0] n=1 |
| `role.range2.melee_pct` | 11.3 [11.3-11.3] n=1 |
| `role.range2.phase.phase1.attacks_add` | 0 [0-0] n=1 |
| `role.range2.phase.phase1.attacks_boss` | 55 [55-55] n=1 |
| `role.range2.phase.phase1.dist_boss` | 6 [6-6] n=1 |
| `role.range2.phase.phase2.attacks_add` | 0 [0-0] n=1 |
| `role.range2.phase.phase2.attacks_boss` | 7 [7-7] n=1 |
| `role.range2.phase.phase2.dist_boss` | 4 [4-4] n=1 |
| `role.range2.phase.start.attacks_add` | 0 [0-0] n=1 |
| `role.range2.phase.start.attacks_boss` | 0 [0-0] n=1 |
| `role.range2.phase.start.dist_boss` | 3 [3-3] n=1 |
| `role.range2.ranged_pct` | 88.7 [88.7-88.7] n=1 |
| `react.phase.phase1.melee.attack` | 1 [1-1] n=1 |
| `react.phase.phase1.melee.step` | 8 [8-8] n=1 |
| `react.phase.phase1.melee.swap` | 3 [3-3] n=1 |
| `react.phase.phase1.melee1.attack` | 1 [1-2] n=12 |
| `react.phase.phase1.melee1.step` | 1.5 [1-15] n=12 |
| `react.phase.phase1.melee1.swap` | 3 [2-8] n=12 |
| `react.phase.phase1.melee2.attack` | 1 [1-2] n=12 |
| `react.phase.phase1.melee2.step` | 4.5 [1-11] n=12 |
| `react.phase.phase1.melee2.swap` | 3 [2-6] n=12 |
| `react.phase.phase1.melee3.attack` | 1 [1-2] n=12 |
| `react.phase.phase1.melee3.step` | 1.5 [0-14] n=12 |
| `react.phase.phase1.melee3.swap` | 5 [2-12] n=12 |
| `react.phase.phase1.range1.attack` | 3 [3-3] n=1 |
| `react.phase.phase1.range1.step` | 1 [1-1] n=1 |
| `react.phase.phase1.range1.swap` | 11 [11-11] n=1 |
| `react.phase.phase1.range2.attack` | 2 [2-2] n=1 |
| `react.phase.phase1.range2.step` | 7 [7-7] n=1 |
| `react.phase.phase1.range2.swap` | 10 [10-10] n=1 |
| `react.phase.phase2.melee.attack` | 0 [0-0] n=1 |
| `react.phase.phase2.melee.step` | 0 [0-0] n=1 |
| `react.phase.phase2.melee1.attack` | 1.5 [0-4] n=12 |
| `react.phase.phase2.melee1.step` | 0 [0-0] n=12 |
| `react.phase.phase2.melee1.swap` | 10 [3-17] n=2 |
| `react.phase.phase2.melee2.attack` | 2 [0-4] n=12 |
| `react.phase.phase2.melee2.step` | 0 [0-1] n=12 |
| `react.phase.phase2.melee2.swap` | 2 [2-8] n=3 |
| `react.phase.phase2.melee3.attack` | 3 [0-4] n=12 |
| `react.phase.phase2.melee3.step` | 0 [0-6] n=12 |
| `react.phase.phase2.melee3.swap` | 3 [1-12] n=3 |
| `react.phase.phase2.range1.attack` | 1 [1-1] n=1 |
| `react.phase.phase2.range1.step` | 0 [0-0] n=1 |
| `react.phase.phase2.range2.attack` | 1 [1-1] n=1 |
| `react.phase.phase2.range2.step` | 4 [4-4] n=1 |
| `react.phase.phase2.range2.swap` | 6 [6-6] n=1 |

Weapons per role and phase (Blert's weapon name, variants merged, or the id it does not know: rooms using it / median attacks in those rooms):

- `melee1|phase1`: ELDER_MAUL: 11 / 1, SCYTHE: 11 / 19, TONALZTICS: 8 / 1, BGS/GODSWORD: 2 / 1
- `melee1|phase2`: SCYTHE: 11 / 9, CHALLY: 4 / 1, CLAW: 1 / 1, EYE_OF_AYAK: 1 / 1
- `melee2|phase1`: ELDER_MAUL: 12 / 1, SCYTHE: 12 / 19.5, TONALZTICS: 6 / 1, BGS/GODSWORD: 3 / 1
- `melee2|phase2`: SCYTHE: 12 / 9, CHALLY: 5 / 1, CLAW: 2 / 1, SULPHUR_BLADES: 1 / 1
- `melee3|phase1`: ELDER_MAUL: 12 / 1, SCYTHE: 12 / 19, TONALZTICS: 4 / 1, CLAW: 1 / 1
- `melee3|phase2`: SCYTHE: 12 / 8, CHALLY: 4 / 1, CLAW: 3 / 1, BLOWPIPE: 1 / 1
- `melee|phase1`: ELDER_MAUL: 1 / 1, SCYTHE: 1 / 22, TONALZTICS: 1 / 1
- `melee|phase2`: SCYTHE: 1 / 10
- `range1|phase1`: HAMMER/HAMMER_BOP: 1 / 2, BLOWPIPE: 1 / 55
- `range1|phase2`: BLOWPIPE: 1 / 18, CHALLY: 1 / 1
- `range2|phase1`: HAMMER/HAMMER_BOP: 1 / 2, BLOWPIPE: 1 / 53
- `range2|phase2`: BLOWPIPE: 1 / 2, NOXIOUS_HALBERD: 1 / 4, CHALLY: 1 / 1

Where each role stands (offset from the boss's SW tile, share of the phase's ticks, top 3):

- `melee1|phase1`: (-1,4) 10%, (3,4) 10%, (1,4) 6%
- `melee1|phase2`: (-2,2) 12%, (0,-2) 11%, (4,0) 11%
- `melee1|start`: (-2,3) 4%, (-2,0) 4%, (4,2) 4%
- `melee2|phase1`: (-2,3) 9%, (-2,-1) 8%, (-1,-2) 7%
- `melee2|phase2`: (-2,0) 14%, (-2,2) 14%, (4,2) 9%
- `melee2|start`: (-2,2) 5%, (-2,0) 4%, (1,-2) 4%
- `melee3|phase1`: (3,-2) 12%, (4,-1) 9%, (4,1) 8%
- `melee3|phase2`: (4,0) 16%, (0,-2) 15%, (2,-2) 13%
- `melee3|start`: (4,1) 9%, (4,-2) 7%, (4,0) 4%
- `melee|phase1`: (-2,-1) 23%, (-2,3) 18%, (-2,1) 10%
- `melee|phase2`: (-1,-2) 42%, (4,0) 24%, (-2,2) 10%
- `melee|start`: (-2,2) 24%, (-6,6) 15%, (-1,-1) 10%
- `range1|phase1`: (8,5) 11%, (6,8) 11%, (0,8) 11%
- `range1|phase2`: (0,4) 40%, (2,4) 28%, (4,2) 12%
- `range1|start`: (5,1) 36%, (4,2) 17%, (7,0) 11%
- `range2|phase1`: (0,-6) 11%, (-2,-6) 11%, (4,-6) 10%
- `range2|phase2`: (3,4) 24%, (-1,4) 20%, (1,4) 12%
- `range2|start`: (1,-3) 28%, (2,-3) 10%, (4,-3) 8%
