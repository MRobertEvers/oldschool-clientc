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

## The tiers

| tier | lives in | what it holds |
|---|---|---|
| 0–1 | `kernels/` | the tables a caller takes, and the three stage objects they name |
| 2 | `families/` | projection families: a mode, its near-clip rule, and the clip/noclip pair that rule selects between |
| 3 | `graphics/`, `triangles/` | the math: per-ISA vertex kernels, sort lanes, span and triangle kernels |

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
in `scene->tmp_face_order`. Kernels: `bucket` and `flat`, which emit the same
order face for face — `toridraw_face_sort_flat_test` holds them to it. The
`presort` argument asks for the y-ordered stash the batched raster walk reads;
it is derived from the raster's door and is never a caller's to pass.

**Raster** (`ToriDraw_RasterKernelSD` / `...HD`) draws a *model*, not a face. A
kernel that only wants to supply leaf callbacks names `ToriDraw_RasterWalkPerFace`
as its `draw_model` and inherits the normalising walk; naming it is also a
declaration that the kernel has no traversal of its own, which is how the
library knows not to ask the sort for a stash nothing would read.

## Adding a variant

- **A new axis value** (another texture gate, another traversal): add it to the
  vocabulary in `tools/kernel_names.py`, then name the file for its full
  coordinate. `make -C src check-kernel-names` fails on an off-grammar name.
- **A new ISA lane**: fill that stage's hook contract — three functions for the
  flat sort (`toridraw_face_sort_flat.h`), two for the prepared projection
  (`graphics/projection_prepared.h`), the eight contract entries for the portable
  ladder. A lane that has no kernel for a family *declines*, and the caller falls
  through; declining is a supported answer, not a stub.
- **A new kernel object**: one file under `kernels/`, named
  `<stage>.<variant>.u.c`, holding the object and its getter. If it belongs in a
  table, add the table file too — a table is the unit a caller selects.
- **A new table**: fill every slot. `ToriDraw_KernelValidate` will tell you what
  you got wrong; add a case to `toridraw_kernel_matrix_test.c` so it stays told.

## Testing

```
make -C src test-toridraw-kernels     # all 16 proofs, plus the name check
make -C src test-kernel-matrix        # the cross-stage agreement test
```

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
