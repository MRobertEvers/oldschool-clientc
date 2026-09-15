# ToriDraw kernels

Drawing a model is three stages — **project**, **cull + sort**, **raster** — and
each has many variants. A caller selects one object that names all three and is
otherwise unaware of them.

```c
renderer->kernel = ToriDraw_KernelTake(scene, ToriDraw_KernelGetSoftwarePainter());
```

That is the whole interface. `ToriDraw_KernelTake` validates the table against
the scene it will draw into, reports if some stage will not be what the table's
name implies, and provisions the scratch the three stages hand each other.

Names are not abbreviated. `projection` is not `proj`, `perspective` is not
`persp`, and an axis that is off carries a value (`nomodulate`, `painter`,
`nofacealpha`) rather than being absent — absence cannot be grepped for, and it
makes the axis order load-bearing for reading rather than just for sorting.

## The tiers

| tier | lives in | what it holds |
|---|---|---|
| 0–1 | `kernels/` | the tables a caller takes, and the three stage objects they name |
| 2 | `families/` | projection families: a mode, its near-clip rule, and the clip/noclip pair that rule selects between |
| 3 | `impl/projection/`, `impl/facesort/`, `impl/raster/` | the math: per-ISA vertex kernels, sort lanes, span and triangle kernels |

Three directories sit beside those tiers and are not part of them:
`graphics/` holds what the kernels share — templates, step helpers, the winding
and clamp headers, the y-order table; `census/` holds every counter and
ablation, so no hot file hosts instrumentation; `bench/` holds code reachable
only from a benchmark.

Nothing in tiers 0–2 contains a loop. They name and select; tier 3 draws.

## The six laws

1. **A caller holds one table, never a stage.** Renderers take a prebaked
   `ToriDraw_Kernel`, or assemble one naming every slot, and call the
   `*WithTable` entries.
2. **Every slot is filled; NULL is a meaning, not a default.** A NULL projection
   or sort asserts. The legal NULLs are the raster slots: no SD raster (the GPU
   table), no HD raster (every SD table). A table names exactly one raster, and
   which slot holds it says which pipeline the table is for.
3. **Traits, not pointer identity.** Capabilities and requirements are bits a
   kernel declares — `provides`/`needs`, `flags`, `zbuffered_variant`. Address
   comparison can only ever recognise the library's own kernels, and only until
   someone adds another.
4. **Env knobs resolve inside getters, once.** See the knob inventory at the top
   of `toridraw_raster_kernel.h`. After a renderer has taken its table, no stage
   reads the environment.
5. **Scratch is declared and provisioned before frame one.** `KernelScratchNeeds`
   answers what the triple touches, `KernelEnsureScratch` allocates it, and
   `KernelValidate` reports OK / DEGRADED / INCOMPATIBLE — so "I chose the fast
   table and silently got the slow path" is an init-time line on stderr rather
   than a shape in a profile.
6. **Hot-path branches only on per-model facts.** What is constant for a frame
   is baked at selection. What genuinely varies per model — parallel camera,
   `may_clip`, the depth flag, the face class — dispatches through a family or a
   twin slot the kernel owns.

## The stages

**Projection** (`ToriDraw_ProjectionKernel`) dispatches through two families,
perspective and parallel. A family owns its own near-clip rule and the two
vertex kernels that rule selects between, because "can a vertex reach the near
plane, and which plane is that" has a different answer under a projection that
divides by z than under one that does not. Kernels: `prepared` (the default) and
`portable`, which differ in exactly the two perspective slots.

**Cull + sort** (`ToriDraw_FaceCullSortKernel`) produces the back-to-front order
in `scene->tmp_face_order`. Kernels: `bucket` and `bitonic+radix`, which emit
the same order face for face — `toridraw_face_sort_bitonic_radix_test` holds
them to it. `bitonic+radix` reports itself as `radix` on a build with no vector
lane, where the bitonic network does not exist. The
`presort` argument asks for the y-ordered stash the batched raster walk reads;
it is derived from the raster's door and is never a caller's to pass.

**Raster** (`ToriDraw_RasterKernelSD` / `...HD`) draws a *model*, not a face. A
kernel that only wants to supply leaf callbacks names `ToriDraw_RasterWalkPerFace`
as its `draw_model` and inherits the normalising walk; naming it is also a
declaration that the kernel has no traversal of its own, which is how the
library knows not to ask the sort for a stash nothing would read.

## The pixel format

Everything that knows how a colour is laid out in a framebuffer word lives in
`graphics/pixel_format.h` and nowhere else. Eight formats ship — XRGB8888
(the default), ARGB8888, RGBA8888, ABGR8888, BGRA8888, RGB565, ARGB1555 and
RGB565_BE — selected at compile time with
`-DTORIDRAW_PIXEL_FORMAT=TORIDRAW_PF_RGB565`, the same way `VERTEXINT_BITS`
and the ISA lanes are selected. Every raster family draws on every one of them.

RGB565_BE is the odd one and worth knowing about: it is a BYTE ORDER, not a
channel layout — RGB565 with the high byte at the low address, which is what
an SPI display controller clocks out and what `esp_lcd` can swap for you on
its i80 bus and not on SPI. It is here so a client driving one writes the
panel's own words rather than byte-swapping every frame on the way out.

This is affordable because **no hot loop interpolates in pixel space**. The
solid families walk a packed HSL16 word and index a palette that was packed
once at build time; the texture families sample a texel, and a TEXEL IS NOT A
PIXEL — texture data stays ARGB8888 in memory on every target, the composite
(colour key, texel alpha, shade, tint) runs in 8-bit channels exactly as it
always has, and `toritexel_to_pixel` converts once, at the store. On the two
formats where the shading space *is* the framebuffer format that conversion is
the identity by definition.

Two rules hold the boundary:

- **A name carries the format it writes.** `toripixel_rgb565_alpha_blend`
  blends RGB565 and nothing else; a hand-written door is
  `toridraw_flat_opaque_s4_sorting_xrgb8888_asm`. Every implementation is
  compiled on every build so they can be tested against each other, and the
  selection block binds exactly one of them to the neutral spelling a kernel
  writes (`alpha_blend`, `shade_blend`, `toripixel_pack_argb8888`).
- **A name without a format token promises it works for all of them.** That is
  why the palette is `g_hsl16_to_pixel_table` and not `..._to_rgb_table`: its
  element is a `toripixel_t`.

An ISA door claims the format it implements — `TORIPIXEL_IS_XRGB8888` for the
tri/span assembly, `TORIPIXEL_LANES_8BIT` for the vector alpha spans,
`TORIPIXEL_TEXEL_SPACE_IS_NATIVE` for the vector texture spans. A door with no
kernel for the selected format *declines*, and the caller falls through to the
C twin, exactly as a projection lane declines for a family it has no kernel
for. Writing a door for another format means a new file naming it and a claim
of its own.

`impl/raster/asm/tri.flat.rgb565.xtensa.S` is the first of those: the flat
presorted-run doors in Xtensa LX7 for the ESP32-S3, indexing a 2-byte palette,
storing 2-byte pixels and blending with the RGB565 spread arithmetic. It is
gated on the FORMAT as well as the lane, because a kernel that stores halfwords
into a 32-bit buffer is not a slower answer, it is a wrong one. When a family
acquires a second format its header also acquires a neutral spelling for the
door -- `TORIDRAW_FLAT_PRESORTED_RUN_OPAQUE` -- and the batched walk writes
that instead of a name with a format in it. The gouraud and texture families
have not acquired one, so they have not grown the spelling.

```
make -C src test-scanline-formats      # every raster family x all eight formats
make -C src test-raster-kernel-altformat   # the routing contract, built RGB565
```

The first is the one that matters: it compiles with
`-Werror=incompatible-pointer-types`, so a kernel that has not been converted
cannot silently accept a native buffer, and it holds every family to the same
branching references on all eight formats.

## Adding a variant

- **A new axis value** (another texture gate, another traversal): add it to the
  vocabulary in `tools/kernel_names.py`, then name the file for its full
  coordinate. `make -C src check-kernel-names` fails on an off-grammar name.
- **A lane with only SOME of a stage's doors**: supported, and the gate is per
  family. `TORIDRAW_RASTER_BATCH` asks whether there is ANY untextured run
  door to flush to, and each face class carries its own `#ifdef`; a class with
  no door is classified `NONE` and the per-face walk draws it, which is what
  the textured four have always done. The Xtensa lane has the flat doors and
  not the gouraud ones, and gets the batched walk for the half it can use.
- **A new ISA lane**: fill that stage's hook contract — three functions for the
  bitonic+radix sort (`impl/facesort/facesort.bitonic_radix.small.dispatch.h`),
  two for the prepared projection
  (`impl/projection/projection.perspective.prepared.dispatch.h`), the eight contract entries for the portable
  ladder. A lane that has no kernel for a family *declines*, and the caller falls
  through; declining is a supported answer, not a stub.
- **A new kernel object**: one file under `kernels/`, named
  `<stage>.<variant>.u.c`, holding the object and its getter. If it belongs in a
  table, add the table file too — a table is the unit a caller selects.
- **A new table**: fill every slot. `ToriDraw_KernelValidate` will tell you what
  you got wrong; add a case to `toridraw_kernel_matrix_test.c` so it stays told.

## Testing

```
make -C src test-toridraw-kernels     # every proof, plus the name check
make -C src test-kernel-matrix        # the cross-stage agreement test
make -C src test-scanline-formats     # every raster family x all eight formats
make -C src check-xtensa-asm          # the ESP32-S3 kernel still assembles
```

`check-xtensa-asm` is a build, not a test, and it is the most a host can say
about that lane. The proof itself runs on the part: `3rd/toridraw/test/xtensa`
is an ESP-IDF app that scores the kernel against the same C references over
the same generators, and its README carries what it measured.

The matrix is the one that sees what no single-stage test can: a projection, a
sort and a raster can each be correct alone while the pair of them disagrees
about the buffer they hand each other, and the only symptom is pixels. It renders
one fixture through every table × sort × batch arm and hashes the frame.

Three of its fixture properties are load-bearing and each was found by mutation,
not by reasoning — the header says which, and why a fixture that violates them
agrees with itself no matter what the kernels do.

**When you add an assertion here, prove it can fail.** Every claim in these
tests was established that way.

## Catalog

`docs/toridraw_kernel_catalog.md` is generated:

```
python3 tools/kernel_names.py --catalog docs/toridraw_kernel_catalog.md
```

It is derived from the filenames rather than maintained beside them, which is
the payoff for naming every axis: a hand-kept catalog is a second copy of the
truth and the copy is wrong within a month.
