# Seed survey of the six kept ToB Entry rooms (2026-10-05)

The account name seeds the server's random numbers (the first 12 characters). Each kept room
test passes under its own name (`tob_<room>`). This survey replayed each one, unchanged, under four
other names. Seam24's fixer met the problem first: Bloat played from the Scripts tab under the
name `tobbloat1` died, and under the test runner with that name finished 85 of 87.

How it was run: the watch worktree at parent `63ea41d39`, content `fb292a9996`; for each room
`python3 tools/raid_gate/run.py --script <copy of test/raids/tob_<room>.lua> --name <name> --no-build --no-publish`,
names `sva<room>`, `svb<room>`, `svc<room>`, `svd<room>`; headless, virtual clock, one run at a time.

## Result: 7 of 24 off-seed runs are green

| Room | Green of 4 | Runs that died or did not finish |
|---|---|---|
| maiden | 1 | svamaiden, svcmaiden |
| bloat | 1 | svabloat, svbbloat |
| nylocas | 0 | none |
| sotetseg | 1 | svasotetseg, svbsotetseg |
| xarpus | 4 | none |
| verzik | 0 | none |

## Every run

- `svamaiden`: 47 FAIL 511 exit=0 pass=46 fail=1 -- failing rows: `player.died`
- `svbmaiden`: 107 FAIL 631 exit=0 pass=105 fail=2 -- failing rows: `freeze.none_full`, `spec.maiden.auto_prayed_entry`
- `svcmaiden`: 83 FAIL 315 exit=0 pass=67 fail=16 -- failing rows: `room.complete_line`, `room.complete_duration`, `room.cleared`, `spec.maiden.pool_life`, `spec.maiden.extra_scatter_size`, `spec.maiden.blood_cooldown`, `spec.maiden.pool_heal_ratio`, `spec.maiden.av.blood_throw.pool_gfx`, `spec.maiden.av.blood_throw.pool_gfx_offset`, `spec.maiden.av.blood_throw.impact_sound`, `spec.maiden.av.slug_trail.loc`, `spec.maiden.av.slug.spawn_npc`, `spec.maiden.av.slug.walk_idle_seq`, `spec.maiden.av.death_a.seq`, `spec.maiden.av.death_b.seq`, `tech.far_dodge`
- `svdmaiden`: 110 PASS 741 exit=0 pass=110 fail=0
- `svabloat`: 92 FAIL 1299 exit=0 pass=83 fail=9 -- failing rows: `bloat.killed`, `tech.hide_behind_tank`, `tech.step_off_shadow`, `tech.flinch_back`, `tech.protect_from_missiles`, `bloat.cleared`, `bloat.chest_open`, `bloat.chest_points`, `bloat.exit`
- `svbbloat`: 94 FAIL 1285 exit=0 pass=84 fail=10 -- failing rows: `bloat.killed`, `tech.hide_behind_tank`, `tech.step_off_shadow`, `tech.eat_before_stomp`, `tech.flinch_back`, `tech.protect_from_missiles`, `bloat.cleared`, `bloat.chest_open`, `bloat.chest_points`, `bloat.exit`
- `svcbloat`: 93 PASS 353 exit=0 pass=93 fail=0
- `svdbloat`: 93 FAIL 696 exit=0 pass=91 fail=2 -- failing rows: `tech.eat_before_stomp`, `tech.protect_from_missiles`
- `svanylocas`: 161 FAIL 1163 exit=0 pass=160 fail=1 -- failing rows: `spec.nylocas.av.big_death.gfx`
- `svbnylocas`: 165 FAIL 1171 exit=0 pass=163 fail=2 -- failing rows: `spec.nylocas.av.wrong_style.gfx`, `spec.nylocas.vasilias_reflect`
- `svcnylocas`: 198 FAIL 1404 exit=0 pass=192 fail=6 -- failing rows: `spec.nylocas.explosion_radius`, `spec.nylocas.av.vasilias_death.seq`, `spec.nylocas.vasilias_reflect`, `spec.nylocas.entry_recoil_cap`, `spec.nylocas.av.boss_defeated.jingle`, `room.complete_line`
- `svdnylocas`: 120 FAIL 816 exit=0 pass=96 fail=24 -- failing rows: `spec.nylocas.vasilias_tile`, `spec.nylocas.vasilias_spawn_delay`, `spec.nylocas.vasilias_repeats`, `spec.nylocas.vasilias_attackrate`, `spec.nylocas.vasilias_attack_gap_in_window`, `spec.nylocas.vasilias_switch_tick_attacks`, `spec.nylocas.vasilias_hp_entry_unit`, `spec.nylocas.vasilias_max_hit_entry`, `spec.nylocas.vasilias_switch_entry`, `spec.nylocas.vasilias_attacks_entry`, `spec.nylocas.av.vasilias_land.anim_cycles`, `spec.nylocas.vasilias_prayed_miss_entry`, `spec.nylocas.av.vasilias_idle.seq`, `spec.nylocas.av.vasilias_walk.seq`, `spec.nylocas.av.vasilias_attack.seq`, `spec.nylocas.av.vasilias_attack.proj`, `spec.nylocas.vasilias_first_form`, `spec.nylocas.av.vasilias_death.seq`, `spec.nylocas.vasilias_reflect`, `spec.nylocas.entry_recoil_cap`, `spec.nylocas.av.vasilias_land.seq`, `spec.nylocas.av.boss_defeated.jingle`, `room.complete_line`, `tech.prayer`
- `svasotetseg`: 64 FAIL 478 exit=0 pass=63 fail=1 -- failing rows: `run.unfinished`
- `svbsotetseg`: 153 FAIL 1181 exit=0 pass=148 fail=5 -- failing rows: `fight.over`, `spec.sotetseg.av.death.seq`, `spec.sotetseg.av.death.sound`, `spec.sotetseg.av.death.jingle`, `spec.sotetseg.av.death.chest_loc`
- `svcsotetseg`: 158 PASS 580 exit=0 pass=158 fail=0
- `svdsotetseg`: 168 FAIL 635 exit=0 pass=162 fail=6 -- failing rows: `elder_maul.special8`, `elder_maul.special9`, `elder_maul.special10`, `elder_maul.special11`, `elder_maul.special12`, `spec.sotetseg.defence_floor`
- `svaxarpus`: 117 PASS 305 exit=0 pass=117 fail=0
- `svbxarpus`: 122 PASS 340 exit=0 pass=122 fail=0
- `svcxarpus`: 117 PASS 291 exit=0 pass=117 fail=0
- `svdxarpus`: 116 PASS 298 exit=0 pass=116 fail=0
- `svaverzik`: 269 FAIL 981 exit=0 pass=266 fail=3 -- failing rows: `tech.p1_cap_melee_ranged`, `spec.verzik.p1_cap`, `spec.verzik.p2_bomb_prayer_read_tick`
- `svbverzik`: 274 FAIL 1214 exit=0 pass=257 fail=17 -- failing rows: `spec.verzik.p1_cap`, `spec.verzik.p2_purple_first`, `spec.verzik.reds_threshold`, `spec.verzik.av.p3_death.seq`, `spec.verzik.av.p3_death.bat`, `spec.verzik.av.p3_death.throne_loc`, `spec.verzik.av.p3_death.throne_seq`, `spec.verzik.av.p3_death.jingle`, `spec.verzik.av.npc_form_entry`, `spec.verzik.av.p3_death.bat_lifetime`, `verzik.trapdoor`, `verzik.loot_flag`, `verzik.wave_complete_line`, `verzik.trapdoor_click`, `verzik.vault_chest_var`, `verzik.chest_click`, `verzik.chest_open`
- `svcverzik`: 260 FAIL 989 exit=0 pass=255 fail=5 -- failing rows: `tech.p1_cap_melee_ranged`, `spec.verzik.p1_cap`, `spec.verzik.p2_bomb_prayer_read_tick`, `spec.verzik.av.tornado.seqs`, `spec.verzik.av.p3_enrage.tornado`
- `svdverzik`: 264 FAIL 1036 exit=0 pass=263 fail=1 -- failing rows: `spec.verzik.p2_bomb_prayer_read_tick`

## What the failing rows look like (a first reading, not a classification)

Three kinds are mixed together, and telling them apart per row is the work order in
`SEAM_TRIAGE_2026-10-05f.md`:

- The play is tuned to one seed: the character dies or the room does not finish
  (`svamaiden` died at tick 511; `svabloat` and `svbbloat` never killed Bloat; `svasotetseg` unfinished).
- The measurement assumes the seed: `spec.verzik.p1_cap` read "4,3,3 hp" against the spec's
  10,3,3 because only four fist hits landed and none rolled the cap; `freeze.none_full` judged
  "0 of 0 casts".
- A possible real disagreement that the kept seed never exercised: `tech.protect_from_missiles`
  at Bloat ("123 fly hits landed with Protect from Missiles lit ... the largest was 7 against the
  unprotected Entry maximum of 8"); `spec.nylocas.vasilias_reflect` ("largest reflect 7 > half 9 of
  max hit 19"); `spec.verzik.p2_bomb_prayer_read_tick` failing on three of four names. Each needs its
  source line before anyone calls it a content bug or a wrong spec row.

Xarpus is green on all four. No other room holds on more than one of four.
