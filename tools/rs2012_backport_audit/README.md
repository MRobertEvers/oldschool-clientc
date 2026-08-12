# rs2012 backport audit

Animation-aware audit of a backported RS2012 model on the SD painter engine,
with a live web UI.

A 2012-era model was authored against a z-buffered renderer. This engine sorts
whole faces and paints them back to front, which cannot represent everything a
z-buffer can — and the model's baked-in face priorities were tuned (by whoever
tuned them) against poses that are not the poses its animations visit. This
tool takes the model plus its sequences and measures, frame by frame, every
way the two pipelines disagree, then tells you whether a priority assignment
can carry the model or whether it needs the per-model depth test.

```
# everything defaulted for the Queen Black Dragon:
python tools/rs2012_backport_audit/audit.py --preset qbd --open

# custom model + sequences:
python tools/rs2012_backport_audit/audit.py \
    --model path/to/part1.ob3 --model path/to/part2.ob3 \
    --seq rs2012_seq_16715 --seq 16714 \
    --candidate authored=path/part1_authored.ob3,path/part2_authored.ob3 \
    --out build/backport_audit/mything --port 8151 --open
```

The run is deliberately slow and progressive: after every render it rewrites
`status.json` and drops screenshots, and the built-in server
(http://127.0.0.1:8151/) feeds `ui/index.html`, which polls and live-updates —
progress, findings, per-frame charts, and painter / z-buffer / diff contact
sheets as they are produced. The server stays up after the run so the report
remains browsable; Ctrl+C stops it.

## What it needs

- `make -C src rs2012-model-view` (the measurement harness; this tool's flags
  `--near`, `--pose-stats`, `--id-dump`, `--labels-out`, `--repl` were added
  for the audit)
- `make -C src rs2012-model-nudge` for the z-fighting repair stage
- `make -C src rs2012-face-priorities` for the optional search stage
- Python 3, ideally with Pillow (`pip install pillow`). Without Pillow the
  shots stay BMP and the flicker/crack metrics are skipped.
- A packed dat2 cache carrying the animations (`cache.osrs239.rs2012` at the
  repo root for the QBD lane) and the lane's text seq config for frame lists.

## The live view

The UI's top panel is a live, orbitable render of the actual model — drag to
orbit, scrub through every sampled pose of every sequence, and flip between
candidates (`inherited` / `stripped` / `repaired` / `searched`) and between the
painter and the depth-tested kernels with one click. That is the before/after
toggle: same camera, same pose, only the thing under test changes.

It is not a re-implementation: each candidate gets one persistent
`rs2012_model_view --repl` process (toridraw linked as a library, the same
project/sort/raster calls the client makes) that holds the model loaded and
answers orbit requests in ~30 ms through `/live?cand=..&pose=..&yaw=..`. The
pool starts lazily and stays up after the run, so the finished report remains
inspectable until Ctrl+C.

## Pipeline

| stage | what happens | what it can find |
|---|---|---|
| decode | model stats, bind bounds, seq frame lists, rig labels (`--labels-out`) | scratch-tier demand, depth-table demand, translucency/texture exposure |
| structures | rig groups × shipped priorities, no rendering: overlapping bind boxes that share a band are z-fight prone, overlaps pinned to different bands are forced-order prone | artifact suspects from rigging + priorities alone; seeds the repair pool |
| static | bind-pose render per candidate: sort score, compare, order check | baseline sort error, kernel order-dependence |
| prescan | every sampled frame posed with `--pose-stats-only`; per-sequence union bounds pick ONE camera (`--focus/--radius/--near 49`) shared by all of its frames | collapsed/exploded frame decodes, per-pose depth-table overflow, framing impossibility |
| sweep | every frame × candidate rendered painter+zbuffer with the shared camera; scored, diffed, screenshotted | per-frame sort violations, sort flicker, coverage cracks |
| attribution | worst frames re-rendered with `--id-dump`, joined to rig PATCHES (connected shells inside a group, built from the exported topology), split into LAYERING pairs (painter >2 units behind) and TIE pairs (within ±2 units: no stable order — z-fighting), each with a mask PNG showing where; compared against the bind matrices | which shell clips into which — including shells fighting INSIDE one rig group — and where on screen |
| repair | the measure→nudge→look loop (see below) | whether moving/pinning the fighters cures the fighting; writes `repaired_*.ob3` (+ `repaired.moves`) and re-scores it as a candidate |
| search | `rs2012_face_priorities` fast search judged over a pose corpus sampled from the sequences (`--slow N` opts into the anneal); output re-scored across the whole sweep as one more candidate | whether re-banding rescues the model under animation |
| verdict | per-candidate static/mean/max table | ship priorities vs. opt into `TORIDRAW_MODEL_FLAG_ZBUFFER` (`param=zbuffer_model,int,1`) |

## The repair loop

A deterministic measure→nudge→look cycle in four tiers, coarse to fine, every
trial judged end-to-end through the stock kernels on the frames the ties were
measured on. A move is adopted only when its pair's ties drop ≥20% while
total ties and sort error hold; "do nothing" wins every pair where nothing
safe exists. No randomness anywhere except the explicitly seeded tier 4.

1. **Pair tier** — per fighting pair: normal-direction separations of each
   side at every `--repair-amounts` magnitude, both signs. For a shell
   fighting itself (every face is "front" somewhere), the measured conflict
   graph is 2-colored (**bipartition**) to recover the two interleaved sides,
   and each side is tried as a geometry move and as a priority pin (band 5
   over a flexible band-10 bulk — the painter-native fix: the tie stops being
   arbitrary instead of the depths changing). Rounds re-rank from fresh
   measurements and repeat (`--repair-rounds`) until one adopts nothing.
2. **Vertex tier** (`--repair-vertex-pass`) — on the worst survivor, one
   vertex at a time along its own surface normal ±1/±2, sweeping until a
   sweep adopts nothing.
3. **Anneal tier** (`--repair-anneal N`, OFF by default) — seeded simulated
   annealing over compound move states, proposals weighted by where the
   measured ties still are; its best state is adopted only if it strictly
   beats the deterministic result.
4. Everything shares `--repair-trial-budget` (each trial renders the judge
   frames), and the adopted move list ships as `repaired.moves` next to the
   `repaired_*.ob3` files, so a result is reproducible and auditable.

Detection and proposal never grade themselves — the judge is always the real
renderer — and if any priority pin is adopted, the `repaired` candidate is
scored (and live-viewed) with priorities honoured, since the pin is the fix.

Candidates always include `inherited` (the .ob3 as it is) and `stripped`
(`--ignore-priorities`, i.e. the pure depth sort — the "do nothing" floor);
`--candidate name=a.ob3,b.ob3` adds more, e.g. a slow-search output.

## The artifact taxonomy it measures

Backporting a z-buffered-era model to a face-sorting engine produces a small,
recurring set of artifact classes. Each maps to a metric in the UI:

1. **Depth-sort violations / interpenetration** — where parts of the model pass
   through each other, no ordering of whole faces is right; the painter shows a
   surface that is genuinely behind. Metric: `sort error` — % of covered pixels
   behind the z-buffer's answer (perspective-correct, coplanar-seam slack of
   one raster unit). Per frame, per candidate.
2. **Priority poisoning** — the inherited 2012 priority bytes were authored for
   a different renderer and pin faces into the wrong bands. Metric: `inherited`
   vs `stripped` vs `searched` across the same frames. (On the QBD, inherited
   is ~3× worse than doing nothing.)
3. **Sort flicker / z-fighting** — equal-depth faces have no stable order, so
   pixel ownership flips between frames while the geometry there is still.
   Metrics: `flicker` — % of stable covered pixels (z-buffer render unchanged
   between consecutive frames) whose painter pixel changed anyway — and the
   attribution stage's TIE pairs (contested pixels within ±2 depth units,
   per structure pair, with a mask showing where). The repair stage then
   tries to cure the tie pairs by physically separating the structures.
4. **Animation-only clipping** — poses bring structures into contact the bind
   pose never has, so no static priority can be right in every frame. Metric:
   the attribution stage's (bone group over bone group) pair matrix, minus the
   pairs already present at bind. Self-pairs (a group over itself) are real:
   a wing folding through itself.
5. **Depth-table overflow** — the sort indexes faces by projected depth;
   a model needs its whole diameter in levels. The reference table has 1,500,
   `TORIDRAW_SCENE_DEPTH_16K` has 16,384, and faces outside are silently never
   drawn. Animation grows the demand (QBD: 1,961 at bind, 6,095 mid-wake).
   Metric: per-frame `depth_levels_needed`, with findings at both thresholds.
6. **Scratch-tier overflow** — vertex/face counts vs the scene scratch tiers
   (2K/4K/8K vertices). Static finding.
7. **Translucency order-dependence** — alpha faces composite in sort order in
   BOTH pipelines (the depth-tested path tests but does not write for them), so
   they set a floor no candidate can beat. Metrics: alpha face count, and the
   `order check` residual (depth-tested render vs itself with face order
   reversed) which should sit near that floor.
8. **Coverage cracks** — the depth-tested kernels copy the stock span rules, so
   silhouettes must match; a nonzero `cracks` metric means candidate geometry
   (e.g. slow-search vertex fuzz) opened seams in a pose, or something is
   genuinely broken.
9. **Frame-decode failures** — a staged/packed frame that decodes under the
   wrong codec collapses the rig to a point (or blows it up). The prescan
   classifies each frame (`ok` / `collapsed` / `exploded` / `failed`) by
   comparing posed to bind extent and excludes bad frames from scoring, but
   flags them: they will misrender in game too.
10. **Texture-path divergence** — the depth-tested texture kernels divide per
    pixel and have no affine twin; textured faces shimmer differently between
    pipelines. The audit reports exposure (textured face count); render both
    routes with `--textures`/`--lane-textures` on the viewer when it matters.

## Camera discipline (why the numbers are comparable)

Cross-frame metrics are only meaningful if every frame of a sequence shares a
camera. The prescan unions the posed bounding boxes and derives one
`--focus`/`--radius` per sequence; `--near 49` takes the engine's icon-camera
lane past the world renderer's 3,500-unit far cull (a posed giant needs more
distance than that); and the zoom is solved so `distance + radius` stays inside
the 16K depth table. When no zoom can (reach ≥ ~7,000), the tool crops and
says so — that impossibility is itself artifact class 5.

## Scoring fixes worth knowing about (2026-08-11)

Two measurement bugs were fixed in `rs2012_model_view` while building this
audit; numbers produced before the fix mix modes and are not comparable:

- The model adaptor ships bit 0 (now `TORIDRAW_MODEL_FLAG_ZBUFFER`) set, and
  the sorter drops priorities for z-buffer-flagged models, so the viewer's
  first view always sorted UNPRIORITIZED; under `--compare`/`--order-check`
  the stale flag then leaked into every later view's primary sort. The flags
  are now set before every projection, so all views honour the requested mode.
- `--order-check` cleared its reversed-order tile to black instead of the
  background colour, so on any non-black `--bg` it reported ~100% difference.

## Output layout

```
build/backport_audit/<name>/
  status.json     # everything the UI shows, machine-readable
  shots/          # painter/zbuf/diff/mask PNGs + thumbs, stable names
  work/           # intermediates; deleted at the end of a run
```
