# State coverage across the six ported rooms

Why this exists: every port's proof is "identical readings" — byte-identical
ledgers, or identical per-name tick logs. **That proof is worth only as much as
the states the runs actually entered.** A change hung off a state nothing
enters is indistinguishable from no change, so a green survey with
byte-identical ledgers can "prove" a port that never exercised the new code.

The case that established it (Xarpus agent, 2026-10-07): a demonstration change
hung off `holding_out`, surveyed **5 of 5 green with byte-identical ledgers** —
and the state is never entered in any measured run. The change had simply never
run.

Classification used below, as the coordinator set it:

- **(a) unreachable in the mode we test** — fine, but must be recorded so
  nobody mistakes a byte-identical ledger for proof of it.
- **(b) reachable but missed by these seeds** — the seeds are too narrow and we
  should say so.
- **(c) dead because the port wired it wrong** — a bug, must be fixed.

## Method, and its present limit — read this before trusting a row

Two sources, and they are not equally strong:

1. **Runtime counts** from `m.counts` / `m.visits`, surfaced by
   `QD.raid.sm_coverage(st)` (committed `cde6121fa`). This is the authority.
2. **A static screen**: a state with no inbound transition is dead by
   construction. Useful but **not a verdict** — every room's handlers use
   dynamic targets (`return nil, ev.to`, `return nil, go`, `nym_to_form(name)`,
   `sm_force`), so the static orphan list is full of false positives. It
   correctly found Maiden's `PREAIM`, which the Maiden agent independently
   confirmed; it also falsely flagged Verzik's `p1 t12 p2 t23 p3` (reached by
   `return nil, ev.to`) and Maiden's `DODGE` (reached by `sm_force`).

**`sm_coverage` has no call site yet, and that is deliberate.** The natural
hook is the play library's `_play_summary`, and landing a shared-library change
while four room agents were running before/after comparison batches would have
changed ledger detail strings underneath them mid-batch and wrecked the proofs
in flight. The gate had 16–18 live raid processes throughout this work. So the
runtime rows below are the ones the room agents produced from their own runs,
and **the rooms marked REQUESTED are not yet measured.** That is the honest
state; it is not a claim that those states are covered.

## Declared inventory — 23 machines, 6 rooms

Complete, read from the declarations (including the three declared in loops and
the three built by a factory, which a naive grep misses).

| room | machine | states | instances |
|---|---|---|---|
| bloat | `bloat_cycle` | active, down, stomp, rising, dead | 1 |
| bloat | `bloat_raider` | outside, run_by, hiding, attacking, leaving, rise_swing, tick_eat, stomp_eat, flinch | 1 |
| bloat | `bloat_runby` | waiting, equipping, swinging, fired, done, gave_up | 1 |
| bloat | `bloat_down_spec` | stowed, worn, armed, spent | 1 |
| maiden | `maiden_phase` | P100, P70, P50, P30, DEAD | 1 |
| maiden | `maiden_crab` | WALKING, FROZEN, THAWED, GONE | **10** (per position) |
| maiden | `maiden_freezer` | DRAIN, F_ON_BOSS, PREAIM, CAST, RETURN, DODGE | 1 |
| maiden | `maiden_scythe` | OPEN, DRAIN, S_ON_BOSS, LANE, CLAWS, DODGE | 1 |
| nylocas | `nylocas_room` | WAVES, CLEANUP, LANDING, FORM_MELEE, FORM_MAGIC, FORM_RANGED, DEAD | 1 |
| nylocas | `nylocas_mage` / `_ranger` / `_melee` | AT_STAND, KILL, PRE_STAND, PILLAR_DEFENCE, SELF_DEFENCE, CLEANUP, BOSS | 3 declarations, shared table |
| sotetseg | `sotetseg_room` | fight_start, maze_1, fight_mid, maze_2, fight_last, dead | 1 |
| sotetseg | `sotetseg_prayer` | melee, magic, missiles | 1 |
| sotetseg | `sotetseg_weapon` | opening, bow_shoot, scythe, maul_equip, maul_swing | 1 |
| sotetseg | `sotetseg_seat_1/2/3` | fight, gather, shadow_realm, follow, boss_gone | 3 declarations |
| verzik | `verzik_phase` | pre, p1, t12, p2, t23, p3 | 1 |
| verzik | `verzik_rotation` | opening, crabs, webs, yellows, ball, autos | 1 |
| verzik | `verzik_enrage` | RING, SWING, SHARE, PROTECT, EAT | 1 |
| verzik | `verzik_dawnbringer` | absent, held, done | 1 |
| verzik | `verzik_specdump` | absent, carried, wielded, done | 1 |
| xarpus | `xarpus_room` | feeding, standup, spit, gaze, dead | 1 |
| xarpus | `xarpus_exhumed` | waiting, hunting, covering | 1 |
| xarpus | `xarpus_spit_solo` | melee, clearing, dodging | 1 |
| xarpus | `xarpus_spit_trio` | in_melee, stepping_out, holding_out | 1 |
| xarpus | `xarpus_gaze` | swinging, relocating, stalking | 1 |

## MAIDEN — measured, 9 names (`d55cbe235`)

Reported by its agent from `m.counts` / `m.visits`.

| machine | state | entries |
|---|---|---|
| `maiden_freezer` | DRAIN | 27 |
| | F_ON_BOSS | 300 |
| | CAST | 186 |
| | DODGE | 303 |
| | **PREAIM** | **0** |
| | **RETURN** | **0** |
| `maiden_scythe` | OPEN (twisted_bow) | 282 |
| | DRAIN (tonalztics) | 216 |
| | S_ON_BOSS (scythe) | 822 |
| | LANE (dinhs_bulwark) | 42 |
| | CLAWS (dragon_claws) | 96 |
| | DODGE | entered |
| `maiden_crab` ×10, `maiden_phase` | all states | entered |

**Dead: `maiden_freezer` PREAIM and RETURN — classification (c), wired dead,
but deliberately and correctly so.** Nothing transitions into either: the
preaim hold is written inline in `F_ON_BOSS`'s tick, and `CAST`'s end goes
straight back to `F_ON_BOSS`. The *old* table carried a comment claiming "bar
near a threshold -> PREAIM" that was **false** — so the behaviour as measured
never had these states, and a no-behaviour-change port must not invent them.
The agent left them declared with notes rather than deleting them, which is
right: waking them is the owner's call, not a port's. **Action: none now; the
owner decides.** This is the one row where (c) does not mean "fix it".

## The other five rooms — runtime rows REQUESTED, not measured

Partial statements from their agents, recorded as evidence but not as coverage:

- **bloat** — `bloat_runby` (all 6 states) is a **Hard-mode drain path the
  Normal survey cannot reach**: **classification (a)**, stated honestly by its
  agent before being asked. Its byte-identical ledgers are therefore no
  evidence about any of those six states. `bloat_raider`'s `flinch`,
  `rise_swing`, `tick_eat` and `gave_up` are the next most likely zeros and are
  unmeasured.
- **xarpus** — `xarpus_spit_trio` `holding_out`: **0 ticks, classification (a)**
  ("0 ticks waiting" in the solo gaze row), and it is the case that started
  this whole exercise. Its agent re-pointed its demonstration change at a state
  the rows prove is entered.
- **nylocas** — `PILLAR_DEFENCE` and `SELF_DEFENCE` are entered, but their
  *leave* condition is unreachable under the kept plan: an override
  (`P.room_copy`) returns above the state tick, so the whole body is dead for
  waves 1..31. That is a **dead transition inside a live state**, which this
  table's shape does not capture and which a per-state count would not reveal.
  Worth a column in a future revision.
- **sotetseg**, **verzik** — unmeasured.

**Verzik is my own room and I could not measure it either**, for the same
reason: the gate was continuously occupied by other agents' comparison batches,
and the hook that emits the counts is a shared-library change I declined to
land mid-batch. `verzik_enrage` `EAT` is the one Verzik state I can speak to,
and only indirectly: its demonstration change moved the tick logs on 5 of 19
runs, so EAT demonstrably fired. Had those 19 been identical I would have had
to report that the change never ran, not that it was harmless.

## A second finding: the file headers disagree with the declarations

Three ports' header comments name states the declarations do not contain. These
are doc/code mismatches in brand-new code, and they are the same class of
error as the false `PREAIM` comment in Maiden's old table — a comment that
describes a machine the code does not implement.

| room | header claims | declared |
|---|---|---|
| xarpus | `xarpus_gaze`: swinging / relocating / **stopping** / **holding** | swinging, relocating, **stalking** |
| xarpus | `xarpus_spit_trio`: in_melee / stepping_out / holding_out / **pressing_in** | in_melee, stepping_out, holding_out |

(`nylocas_room`'s header was checked and is **correct** — its `FORM_*` states
are real, built by the `nym_form_state` factory rather than written as literal
tables, which is why a naive grep misses them.)

The Xarpus agent described `stopping` as having "3 stops in the solo room",
which cannot be a count for a state that is not declared — so either the name
in its message is loose or the header is stale. Asked.

## What is still owed

1. `sm_coverage`'s call site in `_play_summary`, to be landed when the gate is
   quiet and every room agent has been warned that ledger detail strings will
   gain a section (identical on both sides of any comparison taken on the same
   library, so it strengthens rather than breaks the proofs — but it must not
   land mid-batch).
2. Runtime rows for bloat, nylocas, sotetseg, verzik, and the two Xarpus
   machines not yet reported.
3. **Re-run status against the layer changes that postdate the ports' proofs**
   (`8c51e3841` the event-cache fix, `b0fc8aa55` the premise):

| room | re-run since `8c51e3841`? |
|---|---|
| maiden | **yes** — four 9-name batches agree byte-identically; does not use `sm_events` at all |
| sotetseg | **yes** — found the bug independently; derives per decide, 4 of 5 bit-identical, `svcplaysotet` divergence explained and fixed |
| xarpus | **in flight** — re-running 5 names, comparing archived ledgers byte for byte |
| nylocas | **asked, not confirmed** |
| bloat | **asked, not confirmed** — uses `sm_events`, so most exposed of the five |
| verzik | **yes** — surveys unmoved, all 19 per-name tick logs identical (`demo` vs `vfix`) |

`b0fc8aa55` (the premise) needs no re-run anywhere: no declaration in any room
uses `premise`, so the only runtime change (`if s.premise ~= nil`) is never
taken. Verified standalone.
