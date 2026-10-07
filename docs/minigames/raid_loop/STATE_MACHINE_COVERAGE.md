# State coverage across the six ported rooms — RUNTIME

Why this exists: every port was proved by "identical readings" — byte-identical
ledgers, or identical per-name tick logs. **That proof is worth only as much as
the states the runs actually entered.** A change hung off a state nothing
enters is indistinguishable from no change, so a green survey with
byte-identical ledgers can "prove" a port that never ran the new code.

The case that established it (Xarpus agent, 2026-10-07): a demonstration change
hung off `holding_out`, surveyed **5 of 5 green with byte-identical ledgers** —
and that state is never entered in any measured run. The change had never run.

## Method

`QD.raid.sm_coverage(st)` (`cde6121fa`) is now emitted by the play library's
summary (`34281e774`), so **every room reports its own counts in its ledger**
and coverage is part of the comparison instead of an assumption. Rows below are
one run per room on that hook, same binary and pack. Format is
`state ticks/entries`.

**`sm_coverage` is not callable from a test script** — the test sandbox has no
`QD` global (Xarpus agent hit `attempt to index a nil value (global 'QD')`),
which is why the summary is the right emitter.

### Reading a zero correctly — three kinds, only one is a gap

1. **Structural.** A state that leaves within the tick it is entered always
   has `ticks 0`. For a one-tick state **entries are the reading**. `pre 0/1`,
   `absent 0/1`, `standup 0/1`, `dodging 0/12` are all fully exercised.
   `sm_coverage`'s NEVER list requires *both* counts zero, so it does not make
   this mistake — but a human skimming for "0" will.
2. **Mode- or shape-exclusive.** `xarpus_spit_solo` in a trio and
   `xarpus_spit_trio` in a solo: the decide instantiates one or the other, so
   "0 in this survey" is the survey's shape. Classification **(a)**.
3. **A genuine gap** — nothing entered it and something could have. **(b)** or
   **(c)**.

### THE LIMIT OF THIS WHOLE DOCUMENT — read before trusting "every state fired"

**A coverage table keyed on STATES cannot see a state that is entered but
whose branch is never taken.** State entry counts catch the Xarpus case — a
change hung off a state nothing enters — and they do **not** catch a change
hung off a condition *inside* a state that is entered. "Every state fired" is
therefore a weaker guarantee than it reads.

The Nylocas agent's example is the one to remember: `PILLAR_DEFENCE` and
`SELF_DEFENCE` both show healthy counts (32/1 and 209/1 below), and their
*leave* condition has never once been taken, because the `P.room_copy`
override returns above the state tick and makes the whole body dead for waves
1..31. A per-state count shows both states green while the edge out of them is
unreachable.

**BOTH STRONGER READINGS ARE NOW IMPLEMENTED** (`b2fe8cd99`), so this limit
is closed in the layer even though the tables below predate it:

| clause | means | catches |
|---|---|---|
| `NEVER` | states never entered | a change hung off a state nothing reaches — Xarpus's `holding_out` |
| `COLD` | declared `<state>/<event>` handlers never invoked, listed only for states that WERE entered | a change hung off a branch inside a live state — Nylocas's `defence_clear` |
| `edges` | every transition taken, `FROM>TO/event xN` | which edge out of a hot state is dead |

The edge key carries the EVENT, not just from>to, because that is where the
blindness actually sits: the Nylocas finding is not "PILLAR_DEFENCE never
reaches BOSS" but "`waves_over` never moved it" while `boss_phase` moved it
hundreds of times. Keyed on from>to alone the row reads `PILLAR_DEFENCE>BOSS
48` and looks healthy. A premise break counts as an edge under
`premise_broken`, so a state only ever left by its premise does not read as
having no exit taken.

It is a counter, not a bigger trace: `m.trace` carries `by = <event>` already,
but `trace_max` is 64 rows against a 400-600 tick room, so the trace is lossy
at exactly the end where a late edge would show. The counter is O(1) per MOVE
rather than per tick — Nylocas makes 6-27 moves in 400-600 ticks — and bounded
by the declaration.

**What the numbers still do not prove.** `edges` is a histogram, not a
denominator: the declared edge set is not statically knowable, because a
handler returns its target at run time, so an edge at zero appears as an
ABSENT key to be read against the declaration rather than announced. And a zero
does not say *which* of (a)/(b)/(c) it is — in the Nylocas case the cause was
entirely outside the declaration, an override returning above the state tick so
the event that owns the edge is never raised. **The counter makes the question
askable per edge instead of per state; it does not answer it.** And none of the
three readings sees a condition *inside* a handler body: that is ordinary
branch coverage and needs a different tool.

So: `NEVER` empty means "every state was entered", `COLD` empty means "every
declared handler ran", `edges` shows which transitions happened, and none of
them means "every line ran".

## The rows

### verzik — `_play_verzik`, seat 1
| machine | counts | never |
|---|---|---|
| `verzik_phase` | p1 107/1, p2 223/1, p3 150/1, pre 0/1, t12 12/1, t23 4/1 | — |
| `verzik_rotation` | autos 83/3, webs 41/1, yellows 14/1, crabs 7/1, opening 5/1 | **ball** |
| `verzik_enrage` | RING 90/23, SWING 34/23, PROTECT 13/1 | **EAT, SHARE** |
| `verzik_dawnbringer` | held 17/1, done 90/1, absent 0/1 | — |
| `verzik_specdump` | wielded 4/1, done 30/1, absent 0/1, carried 0/1 | — |

### maiden — `_play_maiden`
| machine | counts | never |
|---|---|---|
| `maiden_phase` | P100 1/1, P70 1/1, P50 1/1, P30 1/1 | **DEAD** |
| `maiden_freezer` | CAST 153/7, F_ON_BOSS 124/10, DODGE 28/10, DRAIN 7/1 | **PREAIM, RETURN** |
| `maiden_crab` ×9 | see below | per position |

`maiden_crab` instances present: N1, N2, N3, N4in, N4out, S1, S2, S4in, S4out.
**`S3` has no instance at all** — not even a `GONE` row, so the machine was
never run for that position. THAWED is 0 for N1, N2, N4in, S4in, S4out in this
run; FROZEN is 0 for N1.

### bloat — `_play_bloat`, summed over 5 names x 3 seats (its agent's numbers)
| machine | counts | never |
|---|---|---|
| `bloat_cycle` | active 1084/40, down 818/30, rising 76/27, stomp 27/27, dead 20/10 | — all fired |
| `bloat_down_spec` | worn 1073/30, spent 392/15, stowed 372/30, armed 168/30 | — all fired |
| `bloat_raider` | hiding 1084/40, attacking 732/30, leaving 135/56, rise_swing 54/27, outside 20/25 | **flinch, run_by, stomp_eat, tick_eat** |
| `bloat_runby` | no instance in any record | never created |

Note `bloat_cycle` `dead` DOES fire here (20/10) where the single run showed
zero — the single-run rows elsewhere in this document are correspondingly weak,
and this is the only room whose numbers are summed over a whole survey.

### nylocas — `_play_nylocas`, 9 names, all three seats (its agent's numbers)
Counts are **how many of the nine names entered the state**.

| machine | counts | never |
|---|---|---|
| `nylocas_room` | WAVES 9, CLEANUP 9, LANDING 9, DEAD 9, FORM_MELEE 9, FORM_MAGIC 9, FORM_RANGED 9 | — all fired |
| `nylocas_mage` | AT_STAND 9, KILL 9, PRE_STAND 9, PILLAR_DEFENCE 8, SELF_DEFENCE 8, BOSS 9 | **CLEANUP (0 of 9)** |
| `nylocas_ranger` | AT_STAND 9, KILL 9, PRE_STAND 9, PILLAR_DEFENCE 8, SELF_DEFENCE 9, BOSS 9 | **CLEANUP (0 of 9)** |
| `nylocas_melee` | AT_STAND 9, KILL 9, PRE_STAND **3**, PILLAR_DEFENCE 8, SELF_DEFENCE 7, BOSS 9 | **CLEANUP (0 of 9)** |

All three form states fire on every one of the nine names (490 / 432 / 378
entries, each name a different order), and `LANDING` is entered on every name:
**the seed set is NOT too narrow for her form order.** The edge clause on a
live run shows the dead edge directly — `SELF_DEFENCE>BOSS/boss_phase 1` is
present and no `waves_over`, `defence_clear` or `aggro_clear` edge appears
anywhere.

### sotetseg — `_play_sotetseg`
| machine | counts | never |
|---|---|---|
| `sotetseg_room` | fight_mid 52/1, fight_start 47/1, fight_last 38/1, maze_1 36/1, maze_2 31/1 | **dead** |
| `sotetseg_prayer` | melee 77/51, magic 42/35, missiles 18/18 | — |
| `sotetseg_weapon` | scythe 122/3, maul_equip 7/1, bow_shoot 5/1, maul_swing 3/2, opening 0/1 | — |
| `sotetseg_seat_1` | fight 137/5, shadow_realm 59/2, follow 6/2, boss_gone 2/2 | **gather** |

### xarpus — `_play_xarpus` trio, plus its agent's solo/trio data
| machine | counts | never |
|---|---|---|
| `xarpus_room` | feeding 114/1, spit 96/1, gaze 36/1, standup 0/1 | **dead** (p1 only; fires 2/1 for p2 and p3) |
| `xarpus_exhumed` | waiting 70/5, covering 31/4, hunting 13/4 | — |
| `xarpus_spit_trio` | in_melee 39/20, stepping_out 19/19, holding_out 19/19, pressing_in 19/19 | — |
| `xarpus_gaze` | swinging 20/5, stalking 10/5, relocating 4/4, stopping 2/2 | **holding** |

All 18 Xarpus states are entered across the two shapes.

## The dead-state list, classified

| room | state(s) | class | reason |
|---|---|---|---|
| maiden | `maiden_freezer` **PREAIM**, **RETURN** (since DELETED by its agent) | **(c)** | Nothing transitions into either. The preaim hold is written inline in `F_ON_BOSS`'s tick; `CAST`'s end goes straight to `F_ON_BOSS`. The **old table's comment claiming "bar near a threshold -> PREAIM" was false**, so the behaviour as measured never had them. Left declared with notes: a no-behaviour-change port must not invent them. **The one (c) that does not mean "fix it" — waking them is the owner's call.** |
| maiden | `maiden_crab` **S3 instance absent** | **(b) or (c), UNRESOLVED** | Nine of ten positions have instances; S3 has none. Either S3 did not spawn on this seed (b) or the port never instantiates it (c). **Referred to the Maiden agent.** |
| bloat | `bloat_runby` all 6 states | **(a)** | A Hard-mode drain path the Normal survey cannot reach. Stated by its agent unprompted. Its byte-identical ledgers are no evidence about these six. |
| bloat | `bloat_raider` **flinch, stomp_eat, tick_eat** | **(a)**, my (b) guess REFUTED | Its agent drove the real declaration offline with synthetic duty events and reached every state, then showed these three are Entry's stomp plan (`modes.entry stomp_plan = "stay"`), so they are unreachable in Normal rather than missed by the seeds. **Nothing of Bloat's is in the (b) bucket.** I predicted `rise_swing` would be near-zero too and that is refuted outright: 27 entries over 30 down-seats. |
| xarpus | `xarpus_spit_solo` (trio run), `xarpus_spit_trio` (solo run) | **(a)** | Mode-exclusive: the decide instantiates one or the other. |
| xarpus | `xarpus_gaze` **holding** | **(b)** | Fires once, for p1 only, in the agent's trio data. A seat-and-shape accident, not wiring. |
| nylocas | seat **CLEANUP**, all three seats, 0 of 9 | **(a)**, my (b) guess REFUTED | Unreachable under the kept flags. A seat is nearly always inside PILLAR_DEFENCE or SELF_DEFENCE when the last wave goes out; those states only REMEMBER `waves_over`, and the memory is read by `defence_clear`/`aggro_clear`, which cannot fire while `P.room_copy` takes the tick. Every seat goes from a defence state straight to BOSS. **The consequence is larger than the state**: `ny.in_cleanup` never reads true for a trio, so the cleanup blast window, the cleanup order and the cleanup's weapon rule are all dead with it. |
| nylocas | `nylocas_melee` **PRE_STAND**, 3 of 9 | **(b)** | Reachable; the meleer's wave list is shortest so it rarely runs out of named targets before the next wave. A genuine seed-narrowness finding. |
| nylocas | `PILLAR_DEFENCE` / `SELF_DEFENCE` **leave edge** | **(c), but correctly preserved** | NOT a dead state — both states are hot. Their *leave* is unreachable under the kept plan because an override returns above the state tick. A state-keyed table cannot see this; see the limit section. The port correctly kept it dead, because the rule as written was never the behaviour as measured. Waking it is the owner's call. |
| sotetseg | `sotetseg_seat_1` **gather** | **(b)** | **Referred to the Sotetseg agent** — a seat state that never fires on any of five names would be worth knowing about. |
| verzik | `verzik_rotation` **ball**, `verzik_enrage` **SHARE** | **(a)** for this run | She died before her green ball, which is expected for the fast pace. `_vzslowp3` exists precisely to reach the ball; these two are covered there, not here. |
| verzik | `verzik_enrage` **EAT** | **(b)** | My demonstration state. Did not fire on this seed, but fires on others — its change moved the tick logs on 5 of 19 survey runs. |

### RESOLVED: the leader's terminal state is unreachable BY CONSTRUCTION

This was recorded as an open gap needing new tooling. It is not open, and no
tooling is needed — the answer is determined by the play loop, and I read it
rather than testing for it.

`QD.raid._play_tick` returns `"ok"` **the moment it sees the boss's
`npc_death` row** (`raid_play.lua`, the `st.stop = "npc_death of slot ..."`
arm; the `room_cleared` arm does the same), and `QD.raid.play`'s loop breaks on
that outcome and returns. **The machine is stepped only inside that loop.** So
the leader's machine stops being stepped at the kill, and a terminal state
whose entry depends on reading her row *gone* can never be entered on the
leader — in any room, in any test shape.

**The relay does not change this**, and it was the natural thing to try.
`test/raids/_play_normal.lua` does keep the leader alive and deciding for
hundreds of ticks after each boss dies — it crosses to the next room, buys at
the supply chest after Bloat and after Sotetseg, and takes the trapdoor — but
all of that happens **outside `t.raid.play`**, after the loop has returned,
with the machine no longer being stepped. The next room opens a fresh
`t.raid.play` with a fresh `st` and fresh instances. So a relay run will show
the same zeros.

**Why members DO enter it.** My first explanation was a one-tick race and it
was wrong — or rather imprecise, which here is worse, because a race sounds
timing-dependent and this is not. The relay agent gave the exact mechanism and
it is **two different exit paths**, which I then confirmed in the source:

    if st.log and st.boss_slot ~= nil then
        -- the boss's npc_death ROW -> st.stop, return "ok"
    end
    ...
    if st.boss_gone >= 3 then ... end

- A seat **with** a tick log and a resolved boss slot — the leader — exits on
  the boss's `npc_death` **row**, which it sees on the tick she dies. Its
  machine never gets a tick in which the boss is gone.
- A seat **without** one — the members — never takes that exit at all and falls
  through to the three-ticks-absent path. **Those three ticks are stepped**,
  the derivation raises the boss-gone event in them, and the machine
  transitions. Two of the three fall inside the terminal state, which is
  exactly the `2/1` observed.

So the `NEVER` for p1 and `2/1` for p2 and p3 across all six rooms is
**structural, not timing-dependent**, and no run of any shape can flip it. The
leader would enter a terminal state only if `st.log` were false or
`boss_slot` unresolved on it, which the relay does not change.

**One case neither of us can predict from the loop**, raised by the relay
agent and worth recording as genuinely open: **a seat that DIES in a room.**
Its play ends by some other path, so its coverage may differ, and relay runs
have had deaths. That row is to be read specifically when the relay runs.

So the classification is **(a), definitively** — unreachable for one seat by
construction, reachable for the other two — and the states should stay
declared, because two of three seats do enter them. Nothing to fix, nothing to
build.

**What would overturn this:** a leader terminal-state entry in any run. The
relay agent is capturing `sm coverage:` from every room's `play.fight` row when
the relay next runs — it costs nothing — and will report at once if any leader
terminal state has an entry, so the prediction has a stated falsifier rather
than being an assumption. The prediction on record: the leader's terminal
states remain zero and the members' do not.

**Why the baseline warning does not bite the relay agent**, and the reasoning
is reusable: everything it measures from a relay run comes from
`p<n>/ticklog.tsv` (the raider rows' hp and prayer columns, the room-start
marks) and from `client.log` readouts — **never** from the `play.fight` detail
column. So its per-room damage table, prayer drain and supply budget compare
cleanly across `34281e774`. The rule generalises: a measurement taken from the
tick log is unaffected by the hook; only a measurement taken from the summary
string is.

### A systematic finding across all six rooms

**Every room's terminal state is NEVER entered by the seat whose play ends
first**: `dead`, `DEAD`, `boss_gone`. The reason is the same everywhere — play
ends on the npc_death row before the machine reads her row gone. The Xarpus
agent diagnosed it independently ("the leader's play ends on the npc_death row
before it reads his row gone, the members read the gap") and its data confirms
the shape: `xarpus_room` `dead` is NEVER for p1 but 2/1 for p2 and p3.

Classification **(a)** — unreachable by design for that seat, in every room. It
is recorded here so that six separate agents do not each rediscover it, and so
nobody reads a terminal state's zero as a port bug. It also means **no room's
terminal state has ever been exercised on the leader**, which is worth the
owner knowing.

## How much of `COLD` is signal — two views, and why it stays as it is

The Nylocas agent measured it honestly and reported against its own interest:
on its room `COLD` is **about 7 percent signal** — the two lines that matter
(`PILLAR_DEFENCE/waves_over`, `SELF_DEFENCE/waves_over`) in about thirty, the
rest being eight `stay` entries per state for events that simply did not
happen. It suggested an opt-in marker so a shared `stay` sentinel could be
excluded.

**Declined, on the Bloat agent's argument**, which I think is right: every COLD
entry in its room is legitimate, because a duty can arrive in any state and
**the declaration is the room's contract, not a transcript of these five
runs**. A handler that is cold on these seeds is exactly what the author must
keep declared. Filtering by a sentinel would hide the case where an author
*thought* they had covered an event and the `stay` was the bug.

And `COLD` earned its keep twice over in ways neither of us predicted:

- **It names a dead edge at its definition** rather than leaving it inferred
  from a zero in another table — Nylocas's two `waves_over` lines.
- **It proves an invariant a hand-written clamp was enforcing.** Bloat found
  `leaving/swing_window` COLD, which is the measurement that the swing duty
  never returns inside a down once the run-out has started — something the
  seam48 `leave_age` clamp was already making true. It had considered "make
  `leaving` sticky" as a demonstration change and *reasoned* it would be a
  no-op; the cold list is the evidence instead of the reasoning, and it tells
  you the clamp could be replaced by the state and nothing else.

So the ratio is the price of a complete contract, and it is recorded here so
nobody reads the row as noise-free or as alarming.

## Correction to the previous revision of this file

The earlier revision recorded a "header disagrees with the declaration" finding
against Xarpus (`stopping`, `holding`, `pressing_in` named in comments but
supposedly absent from the declarations). **That was my error.** I read the
working tree while its agent had a demonstration patch reverse-applied; all 18
states are and were declared. Confirmed by this run's own rows, which show
`stopping 2/2` and `pressing_in 19/19` firing.

One real header mismatch existed and its agent fixed it in the same commit that
caused it (`02ad72cc5`): `xarpus_gaze`'s header did not name `stalking` after
the demonstration change added it. Nylocas's header was checked and is correct —
its `FORM_*` states are built by a factory rather than written as literal
tables, which is what a naive grep misses.

The lesson stands even though the instance was false: a header naming a machine
the code does not implement is how the next author hangs a change off a state
that is not there — which is exactly what Maiden's false `PREAIM` comment did.

## Re-run status against the layer changes that postdate the ports' proofs

| room | re-run since `8c51e3841` (event-cache fix)? |
|---|---|
| maiden | **yes** — four 9-name batches agree byte-identically; does not use `sm_events` |
| sotetseg | **yes** — found the bug independently; 4 of 5 bit-identical, `svcplaysotet` explained and fixed |
| xarpus | **yes** — 5 of 5 pass, all five ledgers byte-identical to the pre-port run, 47 rows each |
| verzik | **yes** — surveys unmoved, all 19 per-name tick logs identical |
| nylocas | **yes** — 9 of 9 with every reading identical |
| bloat | **yes** — 5 of 5 green, every name reproduces its baseline exactly. Its plan body has NO await (no PT, no `QD.ticks`, no `QD.tick()`), so its decide cannot itself cross a tick boundary; Bloat was only ever exposed through the library's own advance |

`b0fc8aa55` (the premise) needs no re-run anywhere: no declaration uses
`premise`, so its only runtime branch is never taken. Verified standalone.

### WARNING: `34281e774` INVALIDATED EVERY ROOM'S ARCHIVED BASELINE

Raised by the Xarpus agent, and it is the most practically important line in
this document. The play library's summary **is the detail column of the
`play.fight` row**. So every archived "byte-identical ledger" taken before
`34281e774` now differs from any fresh run by construction, in five rows per
room, and **none of those differences are behaviour changes**.

Anyone re-verifying a room must **re-baseline**: run the pre-port file and the
ported file on the SAME library revision, rather than diffing against an
archive from before the hook. The structural rows are still directly
comparable across the hook — for Xarpus those are `fight.done`, `trio.*`,
`play.gaze_kept` and the tick-log damage; each room has its own equivalents.
It is the `play.fight` detail that moved.

Affected archives: `build/seam_state/sm_xarpus/{before,after,after_layerfix}`
and the equivalents for the other five rooms.

### And no room's proof covers the hook itself

Also the Xarpus agent's point, and it is fair. Each room proved its port on the
library as it then stood; `34281e774` changed the shared library afterwards, so
no room's byte-identical evidence covers it. What can be said without a re-run:

- The hook runs in `_play_summary`, which is called **after** play ends
  (`QD.raid.play` returns `result, summary, st`), so it cannot affect a play
  decision. The failure modes available to it are an error inside
  `sm_coverage` aborting the run, or a longer detail string tripping a limit.
- Neither occurred: all six rooms ran clean on the hook, and again after
  nesting (`06f07bd62`) and after the edge counters (`b2fe8cd99`), with
  identical verdicts each time and with Verzik, Bloat, Nylocas and Xarpus
  byte-identical in their coverage clauses between sweeps.

**CONFIRMED BEHAVIOUR-NEUTRAL by two rooms, independently**, so no further
re-verification is needed and the duplicate run can be skipped:

- **Bloat** re-baselined ON `34281e774` and got 5 of 5 green with **every
  reading unchanged from its first proof** — four survey rounds and three layer
  revisions later — per name: 142 ticks 85/18/25, 144 88/84/91, 139 124/58/60,
  141 117/104/120, 143 122/121/122. Those are tick-log and structural readings,
  not the `play.fight` detail, so they are exactly the rows that remain
  comparable across the hook. Its coverage sums from the library's clause also
  came out **identical to the ones its own local row had measured**, which
  cross-checks the two implementations against each other.
- **Nylocas** re-verified 9 of 9 KEEP with every reading identical against
  `b2fe8cd99` and `06f07bd62` as well, having correctly noted that its earlier
  proof against `8c51e3841` did not carry over on its own because
  `b2fe8cd99` touches `sm_run`'s handler path and `sm_go`.

Together with three six-room sweeps of my own showing identical verdicts, the
library change is neutral. **It was my change, so the burden was mine rather
than the room agents'** — five surveys at roughly twenty minutes each should
not have been charged to them for a shared-library change they did not make.

**`34281e774` changes ledger detail strings** for the six ported rooms, so the
next before/after comparison in each must be like-for-like on that commit or
later. It makes a comparison strictly stronger: the counts are identical on both
sides of a true refactor, and a port that strands a state now shows it in the
same diff that proves the readings.
