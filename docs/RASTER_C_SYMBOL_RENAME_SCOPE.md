# C symbol rename scope (optional)

This is the **design reference** for todo `c-symbol-rename-pass` in the raster catalogue plan. A real rename is a large diff touching every call site.

**Status:** Per-variant `raster_*` / `draw_scanline_*` entry points under `src/graphics/raster/{flat,gouraud,texture}/*.c` (non-`span/`) now use identifiers that **underscore-mirror their source filenames** (e.g. `flat.screen.opaque.branching.s4` → `raster_flat_screen_opaque_branching_s4`). ISA span files (`tex.span.*.u.c`, `gouraud.screen.alpha.span.u.c`) were **not** renamed so compile-time ISA selection keeps identical symbols across backends.

## Rules (from plan)

- Underscore slugs mirror Variant ID token order: `draw_scanline_<family>_<space>_<gate>_<kernel>[_ordered]`, `draw_span_<family>_<space>_<gate>_<kernel>[_vec]`, `raster_face_*` mappings in the plan’s face/scanline/span section.
- Full grammar: `<family>.<space>.<gate>.<kernel>[.<walk>][.<layer>]` in docs; C uses `_` not `.`.

## Where to start

1. Internal `draw_texture_scanline_*` / `raster_linear_*` helpers used only inside `raster/texture/span/tex.span.*.u.c` and branching texture TUs.
2. Then `raster_face_texture_*` entry points (model pipeline boundary).
3. `raster_*` triangle entry points last (widest blast radius).

See the plan section **Face, Scanline, and Span naming review** for the explicit current-to-proposed mapping tables.
