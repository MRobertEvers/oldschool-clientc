# Slow search: porting z-buffered models by annealing bands AND geometry

Running record of the two-speed automatic port in
[`rs2012_face_priorities`](src/engine/proctex/test/rs2012_face_priorities.c):
a fast candidate rank over priority assignments, and a slow simulated-annealing
search that additionally fuzzes vertices, both judged by the same objective —
pixels the painter's render leaves visibly behind the z-buffer's.

Companion docs: [`docs/backporting_zbuffer_models.md`](docs/backporting_zbuffer_models.md)
(the manual procedure and the three authoring rules),
[`docs/rs2012_qbd_priorities/`](docs/rs2012_qbd_priorities/README.md) (pictures).

---

## How to prompt for a tool like this

Asked for directly, so recorded directly. The prompts that produced working
iterations of this tool shared five properties; the ones that produced waste
lacked one of them.

1. **Name the objective as a measurement, not a vibe.** "Minimize difference
   from the z render" is buildable; "make it look better" is not. The moment
   the objective was stated as *pixels where the painter shows a surface the
   z-buffer proves is behind another*, every later argument ("is this candidate
   better?") became a number instead of a screenshot debate.

2. **Demand that the proposer and the judge be different code.** The first
   version of this tool used its pairwise model both to propose bands and to
   declare them good. It shipped a confident regression (4.5% → 7.4%). The
   fix — candidates from anywhere, ranking only by the end-to-end render —
   is the single most valuable structural instruction you can give.

3. **Require a do-nothing candidate and refusal-to-regress.** "Depth sort, no
   bands" is always in the race. If nothing beats it, the tool says so and
   ships it. Without this the tool *must* output something, and a forced
   output is where confident garbage comes from.

4. **Name the budget and the record.** "Slow" is a licence to burn CPU only if
   bounded (iterations, views, resolution, seed — all CLI, all printed) and
   recorded (this file, plus the tool's own log). A search that cannot be
   replayed from its seed is an anecdote.

5. **State what is allowed to change.** "Fuzz the vertices" needed a contract:
   how far (±6 units on a 1,700-unit model), in what direction (radially from
   the feature's own centroid, so it inflates/deflates rather than shears),
   and judged against what (the z-buffer of the PRISTINE geometry, so the fuzz
   cannot drag the reference along with the mistake).

What I would push back on next time: "iterate through all the possible priority
mappings" is 12^216 on this model — no. The honest version of the request is
*search the space under the true objective*, which is what this builds.

---

## The tool

One binary, two speeds, one judge.

```sh
make -C src rs2012-face-priorities        # builds with -fopenmp

# fast only: rank candidate assignments, write the winner
src/build*/rs2012_face_priorities --in head.ob3 --in body.ob3 \
    --out out_head.ob3 --out out_body.ob3 --report --views 16 --pitches 5

# fast + slow: anneal bands and vertex fuzz on top of the fast winner
... --slow 4000 --slow-res 128 --slow-views 6 --slow-pitches 3 --slow-seed 0x51F0D5
```

### Fast search (seconds)

Generates candidate band assignments and ranks them by the true objective:

| candidate | what it is |
|---|---|
| depth sort (no bands) | the stripped model; the floor everything must beat |
| as shipped (inherited) | whatever priorities the input carries today |
| climb, floor 10/40/150/400 | the pairwise hill climb at four evidence floors |

Every candidate is scored by `evaluate_assignment`: render through the internal
painter model over every pose × camera angle inside the client's pitch clamp,
count pixels left visibly behind the z-buffer reference. Table printed in rank
order; winner written; ties go to the depth sort.

Bulk-flex spellings were **removed** from the candidate set: the internal
painter sorts strictly by (band, depth) while the engine splices flexible
priorities at three depth averages, so ranking a flex candidate with the wrong
rule scored it at 40% wrong for reasons that were the model's, not the
content's. They come back if/when the evaluator reproduces the splice.

### Slow search (minutes; `--slow N`)

Simulated annealing over a wider state than any priority can reach:

- **band per feature** (0–9), and
- **radial offset per feature**: every vertex of a feature may move up to
  ±`--slow-max-offset` (default 6) units along the unit vector from the
  feature's centroid — inflate or deflate slightly. This is the edit for error
  that has *no* right order: two surfaces interleaving at near-equal depth.

Mechanics:

- Judged against per-view z-buffer maps of the **pristine** geometry, computed
  once. Slack of 6 units so the fuzz itself is never counted as error.
- Mutations: 55% reband a random feature, 45% step a random feature's offset.
- Temperature: 2% of the starting cost, decaying ×0.001 geometrically.
- CPU: OpenMP over views, one raster scratch per thread (capped at 8).
  **GPU: not built.** This repo's renderer is pure software and its only GPU
  context is fixed-function D3D9, which has no compute path. A GPU variant
  would rasterize candidate orders as depth-only passes under D3D11/Vulkan —
  real work, out of scope here, recorded so nobody assumes it exists.
- Deterministic from `--slow-seed`; every run prints its seed.
- **Adopt only strict improvement.** On a tie or worse, the fast winner ships
  untouched and the tool says so.
- An accepted fuzz is written into the output `.ob3` vertices (shifted back up
  for version-13+ inputs). Inputs are never modified.

### What "several animations" means here, honestly

The tool accepts `--cache` + `--frames` and treats pose as a sampling axis —
every pose × every angle feeds one set of counters. On the QBD lane this axis
is **dark**: every animation frame collapses the model to a point because the
lane's framemaps do not decode under the destination cache's codec (see
`docs/backporting_zbuffer_models.md` §Rule 2a). The pose guard detects the
collapse, drops the poses, and refuses to author rather than rank against
garbage. Until the framemap decode is fixed, both searches run on the bind
pose, and this file will say so rather than imply otherwise.

---

## Run record

### 2026-08-10 — QBD default form (models 70260 + 69766)

Command:

```sh
src/build_win64_opt/rs2012_face_priorities \
    --in  .../rs2012_model_70260.ob3 --in  .../rs2012_model_69766.ob3 \
    --out docs/rs2012_qbd_priorities/slow/rs2012_model_70260.ob3 \
    --out docs/rs2012_qbd_priorities/slow/rs2012_model_69766.ob3 \
    --report --views 16 --pitches 5 --res 224 \
    --slow 4000 --slow-res 128 --slow-views 6 --slow-pitches 3 --slow-seed 0x51F0D5
```

Full logs: [`docs/rs2012_qbd_priorities/slow/`](docs/rs2012_qbd_priorities/slow/)
(`qbd_default.log`, `qbd_soul.log`), outputs beside them. Both 3,000-iteration
anneals together finished in under ten minutes on 8 threads.

**Fast rank, default form** (16 views × 5 pitches @ 224 px):

```
strategy                            wrong  of drawn  bands
depth sort (no bands)               14568    5.584%      1   <- chosen
climb, floor 400                    15437    5.917%      2
climb, floor 150                    35909   13.763%      3
climb, floor 10                     36422   13.960%      5
climb, floor 40                     37526   14.383%      3
as shipped (inherited)              48841   18.720%      8
```

The headline for this lane is unchanged and now machine-verified per run: the
inherited RS727 priorities are the worst candidate by 3×, and nothing beats
stripping them. The tool writes the depth-sort model and says so.

**Slow search, attempt 1 — a schedule lesson.** 4,000 iterations at initial
temperature 2% of baseline: the walker climbed to ~2,800 wrong px (start
1,749), spent the whole budget above the incumbent, and finished with **zero
improvements**. Starting from an already-optimized state and then walking hot
just throws the incumbent away. Kept in the record because it is the failure
mode anyone reimplementing this will hit first.

**Slow search, attempt 2 — cold schedule + restart-from-best every 500:**

| form | start (fast winner) | end | change | features moved |
|---|---:|---:|---:|---:|
| default (70260+69766) | 1,749 wrong px | **1,525** | **−12.8%** | 153 |
| tortured soul (70761) | 108 wrong px | **54** | **−50.0%** | 14 |

The improvement is real *under this objective* and comes almost entirely from
the vertex fuzz — the geometry edit reaching error that no priority can, which
was the hypothesis behind widening the state. Offsets stayed within ±6 units;
the output renders visually indistinguishable from the input.

**Cross-check under the real engine sorter** (the viewer, head framing,
3 yaws × pitch 300): shipped 8.04% → slow output **2.56%**, against 2.52% for
the earlier fast-authored model. So under the engine the slow model is a tie
with the fast one, not 12.8% better — the internal gain does not transfer at
full magnitude. That is the known evaluator/engine gap doing exactly what the
open-issues section says it does, and it is why this record quotes both
numbers instead of the flattering one. Until the internal painter reproduces
the engine's flexible-priority splice and its exact bucket order, the slow
search optimizes a close cousin of the truth.

**Verdict for this lane:** ship the fast winner (strip). The slow machinery
works, finds genuine improvements under its objective, refuses ties, and bakes
its fuzz into valid `.ob3`s — but its measured advantage does not yet survive
contact with the real sorter, and a tool that ships on its own say-so against
that evidence would be repeating the mistake this file exists to prevent.

### Known open issues

- **Evaluator vs engine magnitude disagreement.** This tool's internal painter
  and the real `sort_face_draw_order` agree on *ordering* of candidates but
  not on magnitudes (the viewer scored the floor-40 climb ≈4.2% where this
  evaluator scores it ≈14%). Until reconciled — diff the two draw orders face
  by face on one view — treat the ranking as trustworthy and the absolute
  percentages as internal units.
- **Animation axis dark on this lane** (framemap codec, above).
- **The smoke test showed the fast table can differ run to run** with sampling
  density; the seeds and densities in this record are what the numbers mean.
