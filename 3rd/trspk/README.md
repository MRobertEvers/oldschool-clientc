# TRSPK — ToriRS Platform Kit (CPU geometry helpers + OpenGL3 loader)

Modular C graphics toolkit for retained-mode ToriRS platform renderers.
Provides CPU-side vertex buffers, texture atlases, pose tables, and
per-frame index chains. There is no virtual backend — platform code
owns the `TORIRSRC_*` switch and calls TRSPK helpers directly.

Platform ownership, renderer selection, compatibility constraints, and known
defects are registered in
[`docs/platform_quirks.md`](../../docs/platform_quirks.md).

## Layout

- `core/` — shared VBO/IBO/atlas/arena/pose/draw-range helpers
- `opengl3/` — GL proc loader, vertex layout, embedded GLSL
- `webgl1/`, `d3d9/` — vertex layouts (kept for the VBO union)

## Build

Compile `trspk_unity.c` (core) plus optionally `opengl3/opengl3_sdlgl.c`.
Add `-I3rd/trspk` so headers resolve as `"core/trspk_vbo.h"`.
