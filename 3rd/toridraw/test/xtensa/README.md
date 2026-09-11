# The Xtensa lane's proof, on hardware

`toridraw_presorted_xtensa_test.c` scores `tri.flat.rgb565.xtensa.S` against
`raster_flat_screen_*_branching_s4` built for RGB565, framebuffer against
framebuffer. It is the same claim `toridraw_presorted_neon_test.c` makes for
the AArch64 lane and it is made the same way — with that test's eight
generators and its guard bands — but it cannot be a `make -C src test-*`
target, because the code under test does not run on the host.

So it is an ESP-IDF app, and running it means flashing a part.

```
. $IDF_PATH/export.sh                 # or export.ps1
idf.py set-target esp32s3
idf.py -p <PORT> flash monitor
```

It prints `RESULT: PASS` or the first eight mismatching pixels with the
triangle that produced each.

## Two builds, not one

`-DTORIDRAW_XTENSA_NO_PIE=1` swaps the opaque span's vector store for a scalar
one. Run both: they must agree pixel for pixel, and the pair is also the
measurement of what the vector store is worth. Add it to
`main/CMakeLists.txt`'s `target_compile_options`.

## Do not build it under a long path

ESP-IDF on Windows hits `CMAKE_OBJECT_PATH_MAX` well before the 260-character
limit, and the failure is a wall of `CMake Warning` followed by a link error
that names none of this. Build from a short directory.

## What it measured, ESP32-S3 rev v0.2 at 240 MHz, IDF v5.4.2, -O2

Cycles per triangle, 64 triangles x 200 reps, framebuffer in internal SRAM:

| | C reference | asm, scalar fill | asm, PIE fill |
|---|---|---|---|
| opaque, model-scale faces | 594.3 | 458.0 | 458.9 |
| opaque, panel-width faces | 9004.1 | 6640.2 | 4513.6 |
| alpha, model-scale faces | 1350.1 | 859.4 | 859.4 |
| alpha, panel-width faces | 81950.8 | 56836.0 | 56836.0 |

The alpha row is the control: its fill is scalar in both builds, so the two
columns agreeing to the cycle is what says the only thing the flag changed is
the opaque fill.

The opaque rows are the finding. A model's faces are ~11 pixels a span, which
is under the kernel's sixteen-pixel threshold, so the vector path never runs
and the two builds tie. It is worth 1.47x on spans that cross the panel — a
floor quad, a background plate, a scaled-up icon. Both are true and the
kernel keeps the vector path for the second without paying for it in the
first.

### The edge-slope route

512 divides x 200 reps. The kernel divides exactly, with `QUOS`; the
alternative every other lane takes is a float reciprocal.

| dy range | `quos`, exact | float reciprocal | slopes that differ |
|---|---|---|---|
| 1..90 (on-screen) | 20.06 cyc | 75.46 cyc | 0 of 512 |
| 1..4000 (clipped/far) | 20.99 cyc | 75.45 cyc | 11 of 512, worst 2 units of 16.16 |

There is no trade here to weigh: the exact route is 3.6x faster AND exact.
The float route only looks reasonable on ISAs that divide four lanes at once,
which is why AArch64 takes it and this lane does not. A 16.16 quotient needs
more than a 24-bit mantissa once `dy` grows, which is the second row.
