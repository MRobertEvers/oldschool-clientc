# tools/zuk_glyph — the Zuk draw-order lane

A headless, deterministic run of the TzKal-Zuk encounter against the in-process
server, and a check that the floor stays under the fight.

Written for [the Ancestral Glyph lava overdraw](../../docs/glyph_lava_overdraw.md)
and kept because the defect class is wider than that one bug: anything that
lets a tile's ground pass land after an element standing on that tile shows up
here as a line of text, from a run that costs about ninety seconds.

## Using it

```sh
tools/zuk_glyph/run.sh check     # the gate: 0 ground-over-entity rows
tools/zuk_glyph/run.sh prove     # the same run, fix off -- 431 rows, must FAIL
tools/zuk_glyph/run.sh shots     # a frame strip through the glyph's walk
tools/zuk_glyph/run.sh ab        # both arms, one binary, pixel diff
tools/zuk_glyph/run.sh spans     # each mover's span beside its model's reach
```

`check` is the one to run after touching the painter. `prove` is the one to run
after touching `check`: a draw-order gate that cannot fail is worse than none,
and this checkout forbids proving that by editing a source file (several
sessions build from it at once — CLAUDE.md).

Both need a content bake, which this script will not do behind your back:

```sh
TORIRS_PREPARE_ONLY=1 ./run-live.sh manifests/manifest_osrs239.ini
```

That builds `cache.osrs239.baked` (the Inferno's npcs, its instance template and
its lava plates are all content) and the server script pack that `::zuktest`
lives in.

## Knobs

| variable | default | what |
|---|---|---|
| `ZUK_GLYPH_OUT` | `$SCRATCH/zuk_glyph` | where runs land |
| `ZUK_GLYPH_CAM` | `7552,-3000,7100,520,0` | `TORIRS_WEDGE_CAM`: x,y,z fine, pitch/yaw 2048 per turn |
| `ZUK_GLYPH_FRAMES` | `4200` | run length |
| `ZUK_GLYPH_LOG_AT` / `_LOG_FRAMES` | `2600` / `1500` | the wedge-log window, in PAINT calls |
| `ZUK_GLYPH_SIM` | `300,zuktest` | `TORIRS_SIM_CMD` |
| `ZUK_GLYPH_SHOT_*` | | start / step / count for `shots` and `ab` |

The camera matters more than anything else here. Whether the seam exception
fires at all is a function of where the eye sits relative to a tile, so a camera
that shows nothing is not evidence that there is nothing — the gate is only as
sensitive as the eye it is given. Over one run with the fix off, the shipped
default finds 431 violations, `6100,-2200,6900,400,1700` finds 296,
`7104,-2500,7000,450,0` finds 8, and three of the fifteen positions swept find
none at all. **If you change `ZUK_GLYPH_CAM`, run `prove` afterwards**, or you
have quietly turned the gate off.

Wedge-log frame numbers are **paint calls**, not `TORIRS_BMP_SERIES` frame
numbers. On this lane the offset is 28 (boot frames that render no world), so
paint 3463 is frame 3491. Check it rather than trusting it if the lane changes.

## Why the runs are reproducible

Two runs of the same command line paint the same world, which is what makes
`ab` mean anything. It takes both halves of the clock:

- `TORIRS_MAX_FRAMES` already frame-locks the **client**: one 20 ms logic tick
  per frame, never the wall clock (`src/app/app_frame.c`).
- `TORIRS_EMBED_CLOCK_MS=20` frame-locks the **embedded server's** 600 ms tick
  the same way (`src/platform/net_transport_embed.c`). Without it the world
  ticks whenever the host gets there; two runs of this encounter put the glyph
  in different places and its animation on a different frame, and no comparison
  survives that. It was added for this lane.

What still differs between two runs is the debug overlay's frame-time readout,
which is why `diff_frames.py` ignores the leftmost 180 columns.

## Files

- `run.sh` — the lane.
- `world.ini` — `manifests/manifest_osrs239.ini` with the baked cache and the
  embedded transport, and nothing else changed. Keep it that way.
- `check_draw_order.py` — the invariant, read off a `TORIRS_WEDGELOG` capture.
  A tile there is `(plane, x, z)`; the grid is a stack and reading it as
  `(x, z)` turns every upper storey in Lumbridge into a false positive.
- `summarise_spans.py` — one row per element from a
  `TORIRS_MOVER_FOOTPRINT_DEBUG` trace: does its span cover its model?
- `diff_frames.py` — pixel diff of two `TORIRS_BMP_SERIES` strips.
