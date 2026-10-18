# Tekton: recorded behaviour

Read from four submissions, all on Grid Master worlds (see [README.md](README.md)):

| Submission | Raid | What it holds |
|---|---|---|
| 820 | four-man (`raids_client_partysize` 4, `_scaled` 54) | the whole fight: 7 sessions, 6 anvil repairs, 116 sparks, 74 swings |
| 3285 | solo | wake, one session of 7 swings, the kill |
| 843 | solo | wake, contact, one swing, the kill |
| 3389 | solo, Challenge Mode | wake, one full first session, a kill while walking |

Tool: `tools/cox/rsprox_tekton.py` (sections timeline, phases, swings, sparks, heals, hits, death). Its full output over these four is [tekton.out](tekton.out). The content is `cox_tekton.rs2`; the spec is `docs/minigames/cox/rooms/tekton_spec.md`, whose timing and strike rows this file overrules.

**Status key:** **fixed** = changed to match the recording; **match** = ours already did it; **open** = not settled by these recordings.

## Reading the coordinates (do this first)

The transcriber maps every instance tile back to the template tile it was copied from.

- A player (1x1) comes out right.
- A 4x4 npc's printed tile is its instance south-west tile mapped back. In a turned room that is another corner of the template footprint. Its step labels and face angles stay in the instance frame.
- 820 and 3285 are in a room turned 180°: the template south-west tile is the printed tile − (3, 3), and a face angle needs +1024. The tool reads the turn off the walk steps (label against coordinate delta) and corrects for it.
- 3389's room is turned 90°. The mapping breaks at every chunk edge (the coordinates jump by 15), so its geometry is not used.

Face angles: 0 is south, 512 west, 1024 north, 1536 east.

## The strike

This is the finding that matters most for anyone standing next to him.

**A swing strikes the way he was already facing.** The `face_angle` sent with a swing aims the *next* swing. The facing he holds going into a swing was set at the previous swing, toward the side the raider stood on at that swing's scan. At contact he faces the side he reached the raider on.

**Which tiles, by style** (the raider's edge at the end of the swing tick, against the facing he held; 820 and 3285):

| Style | In front | On his right | Behind | On his left |
|---|---|---|---|---|
| crush | 24 of 24 hit | 0 of 3 | 0 of 13 | — |
| stab | 15 of 15 hit | 0 of 3 | 0 of 9 | — |
| slash | 12 of 12 hit | **7 of 7 hit** | 0 of 14 | 0 of 1 |

The one stab "in front, no hit" is 843's last swing: he died before it landed.

**When it lands:** a stab or a crush on the tick after the windup (39 of 39 hitsplats), a slash two ticks after (20 of 20). "His right" is the side clockwise of his facing: facing north, his right is the east column.

This is Near-Reality's model (only a slash cleaves right; the previous swing's facing). Our spec had ruled for the wiki ("front and right on every swing"). **Fixed.** It is also why Synq's counterclockwise rule works: leaving the faced side counterclockwise puts you on his left, which is never hit; clockwise puts you on his right, which a slash hits.

**Damage:** 820 is scaled for 54, so its hits (up to 48 through Protect from Melee) are not comparable with solo numbers. Every recorded hit was on prayer. **Open:** what Protect from Melee does to his hit. Ours still halves it, Near-Reality's rule.

## The session clock

C is contact: the tick of the stance animation.

| | Recorded | Ours before |
|---|---|---|
| Contact | stance seq (delay 30) and the fighting form on the same tick, C (8 of 8) | form a tick later, seq delay 0 |
| First swing | C+3 the first time; C+2 after a repair | C+1 |
| Swing period | 3 | 3 (match) |
| Last swing | at C+27 the first time, C+47 after a repair. He swings there AND turns to walk on the same tick; that swing lands. 9, then 16 swings (7 of 7 sessions). | out 30 / 45 ticks after the first swing, no swing on that tick (10 / 15) |
| The walk back | first step 2 ticks after the turn (6 of 6) | same tick |
| Red → orange | C+17 (6 of 6) | C+18 |

The stance-leave animation (`tekton_ready_transition_to_idle`) appears only when he leaves with nobody to swing at. All **fixed**.

**Contact bug found on the way:** our Tekton kept walking two tiles after contact. The pursuit's route outlived `npc_setmode(none)`, so the walkblocker went under his old tile, retreat could not find it, and the stale blocker walled his way back to the anvil (scriptrun `tekton6`). Contact now stops him with `npc_walk(npc_coord)`. **Fixed.**

## The wake and the rises

| | Recorded | Ours before |
|---|---|---|
| Wake | a raider 4 tiles from his footprint at the end of W; rise seq on W+2 (3285, 843) | rise on W+1 |
| Rise → walk | rise seq at R; walking form, first step and (first wake) the entrance fire all at R+2 (7 of 7) | form and fire at R |

Neither solo raider ever stood at 5, so "4" (Synq) stands. In 820 he woke while the local raider was 9 away: another raider woke him. **Fixed.**

## The anvil

A is the lean: the tick after the step onto his work tile. In 820 that step is a teleport.

| | Recorded (6 repairs) | Ours before |
|---|---|---|
| Lean | A, seq delay 15 | the arrival tick, delay 0 |
| Heal | every 3 ticks from A+3: 85 of 8,400 (base / 100 + 1; the wiki's "heals 4" solo) | 4/300 with each volley |
| Rise | on the heal cadence: A+21, 21, 18, 15, 15, 12 | A+23 |
| Volleys | every 4 ticks on a room clock (first 0..3 ticks after the lean). They continue through the rise animation until he walks (R+2), so 4–6 a repair. | A+3, +7, ... (5) |

**Open:** what brings him off the anvil before A+21. It is not his hp, and it is not the local raider's distance. The transcript shows only the local player, so the other three raiders' positions are missing. Ours always rises at A+21.

## The sparks

| | Recorded (116 sparks) | Ours before |
|---|---|---|
| Per volley | 4 for 4 raiders (one each) | match |
| Target | the raider's tile at the end of the tick before the throw (16 of 16 when they moved) | match |
| Flight end cycle | exactly 60 + 5·(Chebyshev distance), every spark | match |
| Start cycle | random 0..30 | 0 |
| Slope / heights / progress | random 20..25 / 0, 0 / 0 | 25 / 100, 5 / 64 |
| Sounds | 155 at the lava tile (delay = the start cycle, range 8); 156 at the landing (the blast's delay, range 8) | none |
| Landing | ±1 tick around `(end + 15) / 30` at the same distance; the blast's own delay is random 0..5 cycles | `(end + 15) / 30` (inside the recorded spread at every distance except 13) |
| Hit | anyone within 1 of the blast at the end of the tick before (5 of 5 hit at 0–1, 0 of 20 at 2); 13–16 | match |
| Lava tiles (CCW room) | (7,11) (20,5) (26,20) (12,26) template-local; each used once a volley | Near-Reality's five, none of them |

CCW is **fixed**. The THRU and CW tiles are still Near-Reality's (**open**: no recording has a repair in those rooms).

## Death

D is the killing blow.

| | Recorded (4 kills) | Ours before |
|---|---|---|
| Death seq | on D, with the hit | D+1 (now `npc_setdeathanimnow`) |
| Npc gone, journal, drop, walkblocker gone | D+3 | match |
| Journal and drop tile | his south-west tile + (1, 1), the inner tile of his middle | + (2, 2) |
| Party message | `<col=ef20ff>As Tekton is slain by <name>, his supplies are dropped for all.</col>` | none |
| Fire out, crystal shatter (seq 7506), sounds 1021 + 3821 (delay 5, range 10) | the room clock's next turn at or after D+3 (D+3..D+5) | D+3, both sounds delay 0 |
| Crystal gone | 4 ticks after the shatter | 2 |

All **fixed**. The recordings replace the fire and the crystal with `invisible_type8_nonblocking`; ours deletes them, which looks the same. The journal's rotation varies (3, 3, 2): **open**.

## Also in the recordings, not acted on

- The fire and the crystal are re-sent (`loc_add_change`) on every turn of the 4-tick room clock, and the walkblocker on every swing.
- Every form change carries `enabled_ops 0b11111` and a `level_change`; the death clears ops to `0b00000`. That is the engine-wide rule already implemented.
