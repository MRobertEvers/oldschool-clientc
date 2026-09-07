# GPU recheck — `gl3-zbuffer` against `soft3d`, final binaries

Worker `client`, 2026-09-06 ~22:00 CDT. Both sessions ran the **same** command
script (`gl3-results.json` / `soft3d-results.json` carry every command), so the
pause point, camera, vessel state and scene population agree scene for scene.

Provenance recorded in both result files and verified before the run:

| Artifact | SHA256 | Which pair |
|---|---|---|
| `src/torirs` | `c7e62cce180aaeb6bc4cd044818b0209ca99edb478f5280c3ebd10d6b79b1d8d` | 2026-09-06 21:38 |
| `src/build_opt/torirsserver` | `a53899eb8ce5d086e36df3653aaccfd2ee93c05f8e767ece05ecc40a94b9c3db` | 2026-09-06 21:38 |
| `script.dat` (30,076 scripts) | `902a6b8d242bba01990b3955100e301c2a0edb6b1f18fc6ca83fd703a880df59` | unchanged throughout |

**The GPU lane was not re-run on either later pair** (`4854e303` at 08:08 or the
final `5e34b81f` / `6a0e7f04` at 09:35). GPU-1/2/3 are `gl3-zbuffer` findings
outside the sailing scope, and none of the changes between the pairs touches a
GL path.

- GPU session: `/tmp/sailing-final-gpu-gl3`, PID 1644,
  `start --renderer gl3-zbuffer --boat skiff` — **windowed**, no `--headless`,
  a real window opened. `renderer: gl3-zbuffer`, `headless: false`.
- Software counterpart: `/tmp/sailing-final-gpu-soft`, PID 1818,
  `start --headless --boat skiff`. `renderer: soft3d`.
- Extra negative control for defect 1 below: `/tmp/sailing-final-gpu-softwin`,
  PID 6702, soft3d **windowed** (no `--headless`).
- All three sessions were stopped by this worker.

`deck-*.png` and `ocean-skiff.png` **have since been moved to
`historical/`**: they were produced at 13:23 by client `54a09b3f…` with pack
`a14736bc…` and `allow_stale_scripts: true`. They are kept only because
`historical/deck-baseline.png` is cited as prior-art evidence for defect 2.
They are **not** final evidence. (The `gl3-*` and `soft3d-*` captures in this
directory are this section's own and stay here.)

## Scene-by-scene: what the images actually show

Every PNG below was opened and read. `wev` counters were captured with each
frame and are **identical between the two renderers in all five scenes**,
including per-view `bounds` and `yaw`:

| Scene | `wev` (id, visible, flat, markers, model/terrain/actor cmds) — same in both |
|---|---|
| ocean-idle | 1, T, F, 1, 7/10/1; bounds 393152,404224,393408,404864; yaw 0 |
| underway-h06 | 1, T, F, 1, 7/10/1; bounds 392812,404007,393458,404625; yaw 273 |
| deck-baseline | 1, T, F, 1, 8/10/1 |
| deck-net-installed | 1, T, F, 1, **9**/10/1 (the net adds one deck model) |
| three-overlap | 1 T/F 8/10/1; 2 T/F 6/10/0; 3 T/**flat**/6/10/0, yaw 256 |

1. **`gl3-ocean-idle.png` vs `soft3d-ocean-idle.png`** — stopped skiff at the
   surveyed ocean tile 3072,3160, camera 1024/256/1200. Both draw the same hull:
   tan planking, upright furled sail, red masthead pennant, the cyan deck
   highlight square, the player standing amidships, `Adamant Bane 80/80` and the
   `Steering` / `Repairs: No kits` Facilities panel. Magnified side by side
   (`gl3-idle-figure-ok-x9.png`) the player model is fully drawn in **both**, one
   idle-animation frame apart. Renderer differences: gl3's water is smoother and
   its hull edges are cleaner, soft3d's water carries visible dither noise; gl3
   omits the top-left `Set heading` mouseover only because the OS cursor sits
   elsewhere in a real window (proved below).
2. **`gl3-underway-h06.png` vs `soft3d-underway-h06.png`** — `cheat vesselsail 6 2`,
   `step 90`. Both: hull swung to heading 6 (state `heading 6, angle 384,
   sails_set 1`), sail set and bellied out, boom and forestay drawn, the cyan
   helm diamond rotated with the deck, minimap boat icon rotated with land
   entering the top-right, chat `Vessel 1 sailing heading 6 at tier 2`. The rig,
   hull silhouette and deck all match. gl3's sail shading is slightly flatter
   than soft3d's leech gradient.
3. **`gl3-deck-baseline.png` vs `soft3d-deck-baseline.png`** — deck camera
   256/256/1000. Same hull pose, sail, rigging, cyan helm diamond, and a
   single-row Facilities panel. **Differs — see defect 2.**
4. **`gl3-deck-net-installed.png` vs `soft3d-deck-net-installed.png`** — after
   `cheat sailnetfixture 4`. Both show the trawling net's grey webbing rigged
   amidships beside the mast, a **second row appears in the Facilities panel**
   with its own chevrons, and the chat reads "A trawling net is ready. Use
   sailshoal to place a native shallow shoal, then operate the net."
   `model_commands` rises 8 → 9 in both. **Also affected by defect 2.**
5. **`gl3-three-overlap.png` vs `soft3d-three-overlap.png`** — the client tool's
   three-boat fixture rebuilt from its own harness commands (`vesselgoto`,
   two `vesselspawnat`, `vesselboard 1`, `helm`, `camera 1024 320 1200`).
   Both: the aboard hull on the right with the cyan helm square, vessel 2 full on
   the left, vessel 3 **flattened** — visible in both as the thin dark
   spar-and-hull shadow lying on the water to starboard (magnified in
   `gl3-flat-silhouette-x3.png`, the two renderers draw it the same). Three markers on the
   minimap in both. This confirms the group-1 overlap flattening reaches the GPU
   lane unchanged.

## Defects found (reported, not fixed — this worker makes no code edits)

### 1. Chat-filter button captions ride 5 px low on the `gl3-zbuffer` lane

`gl3-chatfilter-caption-shift-x6.png` (6x, three rows: gl3-zbuffer /
soft3d headless / soft3d windowed). On the GPU lane the white filter name
("Game", "Public", "Private", "Channel", "Clan", "Trade") is drawn **5 pixels
lower** and collides with the green `On` state line under it; the text is
garbled at 1x. Measured with a glyph-row profile:

- name line rows: gl3 **486-493**, soft3d headless 481-488, soft3d windowed 481-488.
- the green `On` line is byte-identical in all three (174 px, x 89-409, y 491-498).
- the button plates do not move: a dy-correlation over the whole bar peaks at
  `dy = 0` with only 1,114 differing pixels of 12,880.
- the single-line `All` caption shifts the same 5 px; the `Report` caption does
  not (486-493 in both), so this is one text component's placement, not a font.
- sidebar text (`Steering`, `80/80`) and the chat lines are pixel-identical.

The windowed soft3d control rules out headless-vs-windowed: it matches headless
soft3d exactly. This is renderer-lane specific.

### 2. On the `gl3-zbuffer` lane the player on a boat deck is occluded by the deck

`gl3-deck-figure-occluded-x6.png` (6x crop of the deck-camera pair,
gl3 left / soft3d right). At camera 256/256/1000 the gl3 frame draws the boat's plank /
gunwale band **over** the standing player: only a sliver of the green tunic and
one arm survive below the plank edge, the head and shoulders are gone. soft3d
draws the whole character above the same band. The figure's feet and the tunic
bottom sit at the same y in both, so the actor is at the same place and is being
depth-tested wrongly, not moved. `actor_commands` is 1 in both, so the actor is
emitted either way — this is a GPU depth/ordering fault, not a culled command.

At the wide camera (1024/256/1200, scenes 1, 2 and 5) the figure clears the band
and both renderers agree, which is why the earlier wide-camera GPU evidence did
not catch it.

**Not a regression from this phase.** The same occlusion is present in
`historical/deck-baseline.png` from the earlier gl3 run against client
`54a09b3f…` (`gl3-deck-figure-occluded-prior-x5.png` is the magnified crop). It is a
pre-existing `gl3-zbuffer` defect.

### 3. `gl3-zbuffer` process footprint is ~15x soft3d and grows at scene changes

Same command script, measured with `ps -o rss` on the two live PIDs:

| Point | gl3 (PID 1644) | soft3d (PID 1818) |
|---|---|---|
| HUD at ocean-idle | 537.1 MiB | 321.6 MiB |
| HUD at underway-h06 | 1.84 GiB | 339.8 MiB |
| HUD at three-overlap | 2.27 GiB | 351.7 MiB |
| RSS after the run | **2969 MiB** | **199 MiB** |
| RSS after 5 more captures at a fixed camera | 2976 MiB | 199 MiB |

Repeated captures at a static camera add ~1 MiB each, so this is not a
per-capture leak: the growth lands on scene/renderer transitions (camera change,
boat spawns, sail set, facility install). Recorded as an observation with its
measurement, not as a passing or failing assertion.

## Things checked and found *not* to be defects

- **The small grey wedge floating on the water** at ~(300,197) appears in the
  soft3d captures and not in `gl3-ocean-idle.png`. Probed directly: after
  `restore gpu_base` + `hover 302 197` **both** renderers draw it in the same
  place (`gl3-water-wedge-probe-x6.png`, gl3 left / soft3d right — both also show
  the white water-hover box), and both then print the same `Set heading`
  mouseover at top-left. It is an animated world element whose
  phase differs between processes — the same non-reproducibility
  `client/frame-assert.md` records. Not a GPU fault.
- **`world_order_mode`** is 1 on every gl3 capture and 0 on every soft3d
  capture. Expected: the z-buffer lane selects its own world ordering. The
  resulting `wev` command counts and bounds are identical.
- Timing in this phase is not performance evidence: several workers ran clients
  concurrently. `gl3-three-overlap.png` shows `Frame: 32.45 ms` against soft3d's
  `0.96 ms`, and that number must not be used.
