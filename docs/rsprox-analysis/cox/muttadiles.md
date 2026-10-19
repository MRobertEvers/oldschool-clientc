# Muttadiles: recorded behaviour

Read from submissions 3285, 3282 and 843 (solo, normal), 3389 (solo, Challenge Mode) and 820 (a four-man raid,
re-transcribed with every raider shown), all on Grid Master worlds; see [README.md](README.md). Tool:
`tools/cox/rsprox_muttadiles.py` (sections `meals`, `attacks`, `projectiles`, `submerged`, `prayer`, `surfacing`,
`moves`, `sounds`, `steps`). The room's spec is `docs/minigames/cox/rooms/muttadiles_spec.md`. The content cites these
findings as RECORDED (`cox_muttadiles.rs2`, `cox.constant`, `cox.npc`).

    python3 tools/cox/rsprox_muttadiles.py --section attacks ~/.rsprox/binary/archive/{3285,3282,843}.txt <820>.txt

**Ids.** 7561 submerged (5x5), 7562 small (3x3), 7563 large surfaced (5x5), 7564 the meat tree (2x2, on loc 30012).
Coordinates go through `tools/cox/rsprox_room.py` (turned rooms, the footprint corner). **820 is scaled as 54
players** (Grid Master's +50), so its hitpoints, heals and damage are left out: there, one junior bite heals 1050,
15% of 7000. Its targets, timing and odds are fine to use.

Ticks are relative to the event that defines each row: **D** the small one's death animation, **E** the large one's
emerge, **A** an attack's animation. Heights are in this tree's units (raw / 4); `d` is the Chebyshev distance from
the footprint's middle to the target.

## Hitpoints and meals

| Row | Recorded |
|---|---|
| hp | solo 250 each (small and large); CM 375 |
| trigger | hp at or under 50% on the tick a hit lands: it faces the tree on that tick |
| bites | 3 a meal, on consecutive ticks, `dohgadyle_bite` each; the first on the tick it stands at the tree (the tick after the trigger when it is already there) |
| heal | 15% of its max a bite: 35 at a raid scaled to combat 119 (236 hp), 37 at 126 (250 hp); 18 bites in two kills |
| meals | at most 3; the large one in 820 turned to the tree once and was killed before a bite |
| damage while eating | **lands**: hits splat on a Muttadile walking to the tree and between its bites (820, 3285) |

## Attacks

| Row | Recorded |
|---|---|
| cadence | 4 ticks between attacks, now and then 5 |
| reach | "in reach" = edge-adjacent to the footprint after its own step on that tick |
| small, in reach | 40 bites, 3 spits |
| large, in reach | 11 of 11 melee: 8 `dohgadyle_bite`, 3 `dohgadyle_slam` (bite vs slam inconclusive) |
| large, thrown runs | party R4 M4 R3 R3+ R2; solo R7 R3 R2: runs of three or more, the first ranged; the coin's odds are not settled |
| targets (820) | a random raider in the room each attack: the small one went back to its last target 8 times in 32 (1 in 4, a random pick among four); never "the nearest" by rule; a melee when the picked target is in reach |
| erratic step | about 1 attack in 7 (17 of ~115) is followed by ONE step, mostly at A+3: see "Movement" |

## Movement

From `--section steps` (every recording) and `--section moves`. **A** is an attack's tick; "single" is a step with
none the tick before or after it.

| Row | Recorded |
|---|---|
| spawn | the small one at template (15, 6) in the cw room (3285, 3282, 843; 3389's thru room (15, 5)) |
| wander | before its first fight it wanders, walks of 8-10 tiles one a tick (3282 to (+6, +8) from its spawn, 820 (-8, +7), 3389): a wander range of at least 8 |
| opening | it takes no target while it walks: it faced a raider on the tick each walk ended (3282, 820, 3389: at 8, 13 and 7 tiles), not while a raider 6 off watched it walk |
| the free tick | in a fight it never steps the tick after an attack (0 of 102) and rarely two after (4 of 101). Its steps come 3 ticks after (10 of 100) or on a late attack's chase (A+5 on) |
| the drop | 3 ticks after an attack its face is cleared (`face_pathingentity entity=null`) and set on a raider again the next tick: 3282 18 of 28 attacks, 3285 26 of 39 (the large one 5 of 7). In a party the face goes to the next raider instead |
| the step | 13 of 15 solo single steps fall on such a tick. Mostly diagonal: 23 of 28 singles, where a random neighbour gives half. While it stands away from its spawn the singles keep pointing back: 3282 (19, 7) -> (15, 3), one per cycle for four cycles, and (21, 14) -> (18, 11) for three |
| the draw | the singles are likeliest as one dumb step (diagonal, else X, else Z) toward a random tile within 8 of the small one's spawn (log-likelihood -31.7 over 19, against -37.0 round where it stands and -39.5 for a random neighbour); for the large one, within 8 of where it stands (-15.6 over 9, against -19.4 round where it surfaced) |

## Projectiles

From the footprint's middle; slope 5; the hit lands `end // 30` ticks after the swing (end 86 → +2, 91 → +3,
126 → +4).

| Attack | Projectile | Start → end height | Delay → end | Progress |
|---|---|---|---|---|
| small spit (`dohgadyle_bark`) | 1291 `lizardman_spit` | 25 → 36 | 5 → 61 + 5d | 115 |
| large spit (`dohgadyle_bark`) | 1291 `lizardman_spit` | 46 → 36 | 5 → 56 + 5d | 250 |
| large magic (`dohgadyle_lazor`) | 393 `dragon_ranged_fire_attack` | 62 → 36 | 61 → 56 + 5d | 128 |
| submerged sweep | 393 | 62 → 36 | 61 → 56 + 5d | 128 |

No graphic is sent on the target for any of them (Near-Reality's `firewave_impact` is not in the game).

## Submerged

| Row | Recorded |
|---|---|
| cadence | a sweep every 6 ticks (throws 6, 12, 18 … apart, never 4) |
| odds | each raider in sight 1 in 3: solo 9 of ~31 sweeps; party 56 of ~160 raider-sweeps |
| sound | `firewave_cast_and_fire` (162) at its middle per throw, delay 10, range 15 |

## Prayer

Under the matching protection prayer the sweep's max was 7 (unprayed 23) and the small spit's 7 (unprayed 30).
That fits 33% getting through, which the content keeps. Protect from Melee stops the bites; the slam is not checked
in these recordings.

## Surfacing

| Row | Recorded (five kills) |
|---|---|
| small death | `dohgadyle_death` at D; deleted with its drops at D+3 |
| emerge | `dohgadyle_emerge` at E = D+3 … D+6 (0–3 ticks after the deletion), with the first step out |
| walk | 5 steps, one a tick, from E |
| retype | 7561 → 7563 at E+5; first attack from E+5 |
| message | **none**: no "surges out of the water" message is sent |

## Death and drops

| Row | Recorded |
|---|---|
| message | `<col=ef20ff>As the Muttadile is slain by <name>, supplies are dropped for all.</col>` (friends-chat notification) at each Muttadile's deletion tick (D+3), with its drops |
| drops | big bones despawn in 199 ticks, the vials in 2999 |
| exit | the crystal's smash animation one tick after the large one's deletion |
| sounds | no death sound for either |

## The meat tree

| Row | Recorded |
|---|---|
| hp | 5 × Woodcutting (headbar `healthbar_standard_30`) |
| damage | (WC / 2) − 7 … + 3 |
| rolls | every 5 ticks while chopping |
| sound | `tree_creak_1` (2070) on every chop that lands (3285: 6 of 6; 3282: 10 of 10) |
| animation | none: no flinch, no death |
| messages | "You swing your axe at the tree.", "You hack away some of the meat." per landed chop, "You hack away the final pieces of meat." on the last |
| strip | the tree npc is deleted and loc 30013 `raids_meat_tree_empty` appears on the killing chop's tick (3282: 5 ticks after the last splat, the next roll), with no hitsplat |

## Sounds

| Event | Sound |
|---|---|
| spit (both) | area `creeper_spit` (385) at its middle, delay 0, range 10 |
| large magic | area `firewave_cast_and_fire` (162), delay 10, range 10 |
| slam | area `firewave_hit` (163), delay 30, range 6 |
| small bite | synth `stabsword_slash` (2548) to its target (3285: 22 of 22) |
| hit taken | synth `crocodile_hit` (388) on every hit on a Muttadile (3285: 63 of 63; 843: 21 of 21) |
| sweep | area 162, delay 10, range 15 |

## Fixed in our content (2026-10-18)

| Was | Now |
|---|---|
| sweep every 4 ticks; Near-Reality's projectile with `firewave_impact` on the target | every 6; the recorded projectile and sound; no graphic |
| spits and magic on Near-Reality's rows (delay 15/40, length 28, landing by rounding) | the recorded heights, delays, progress; landing `end // 30` |
| no attack sounds | spit, lazor, slam, small bite as above; `crocodile_hit` as both forms' defend sound |
| small melees 3/4 in reach, large 1/2 | both 9 in 10 (`^cox_mutta_*_melee_odds = 10`) |
| the nearest raider, re-asserted every tick; a hit pulled it to the hitter | a random raider after every attack (`~cox_mutta_retarget`); a hit only targets when it has none |
| no erratic movement | 1 in 7 attacks owe one step to a random neighbour 3 ticks later (`~cox_mutta_step_tick`) |
| the erratic step to a random neighbour; the target held through it; the default wander 5; a target at once | 3 ticks after every attack the target is let go for a tick (the face clears) and taken again the next; on 1 in 7 that tick is ONE dumb step toward a random tile within 8 of its spawn (the large one: of itself); wander 8; before its first fight no target while it walks (2026-10-18). Ours over 6 scriptrun rooms: a single per 5.8 attacks (recorded 5.6), 26 of 26 diagonal, never one tick after an attack |
| meals of 2 bites, 25% each, 4 ticks apart; checked on its next timer | 3 bites, 15% each, on consecutive ticks; checked on the hit's tick too (`[ai_queue1]`) |
| immune while on a meal unless frozen | damageable (`~cox_mutta_prepare_hit` removed with its dispatch) |
| emerge on D+3, retype 3 ticks later; "The larger Muttadile surges out of the water." | emerge 0–3 ticks after the deletion, a step a tick, retype at E+5; no message |
| no slain message | the recorded friends-chat line at each death |
| tree: no chop messages; stripped by its death queue, 3 ticks after the last chop; human block/death anims | the three messages; strip and `npc_del` on the killing chop; `tree_creak_1`, no animations; rolls 5 apart |

## Still open

- The large one's ranged/magic coin after a run of three: R4 M4 R3 R3+ R2 and R7 R3 R2 do not fix the odds.
- Bite vs slam for the large one in reach: 8 to 3 in eleven; the content keeps 1/2.
- The melee odds' exact value: 1 in 10 and 1 in 16 both fit 3 spits in 43.
- Whether the slam goes through Protect from Melee: not in these recordings (kept from the guides).
- The drop: recorded on about 2 attacks in 3, ours on every one; which cycles skip it is not settled.
- The erratic step's draw for the large one: 9 singles; "round itself" beats "round where it surfaced" but not by much.
  Its real spawn is in the water, which the recordings cannot place.
