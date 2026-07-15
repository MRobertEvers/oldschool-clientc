# TRSPK (src2/platformkit)

**TRSPK** (ToriRS Platform Kit) is a modular C graphics toolkit for building retained-mode ToriRS platform renderers. It provides CPU-side vertex buffers, texture atlases, pose tables, and per-frame index chains. Platform code owns a `switch` on `TORIRSRC_*` render commands and calls TRSPK helpers directly — there is no virtual backend interface.

## Folder layout

```text
src2/platformkit/
├── core/           Shared modules: VBO, IBO chain, atlas, pose table, draw ranges
├── opengl3/        OpenGL 3.2/3.3 core shaders and vertex layout
├── webgl1/         WebGL1 / GLES2 shaders and vertex layout
└── d3d9/           D3D9 fixed-function vertex layout
```

## Getting started

Read the full developer guide:

**[docs/trspk_retained_mode_renderer.md](../../docs/trspk_retained_mode_renderer.md)**

It covers architecture, the OpenGL3 reference renderer, shader contracts, and portability notes for WebGL1, D3D9, Apple GL, and Metal.

## Reference renderer

The canonical retained-mode implementation is the OpenGL3 SDL2 backend:

[`src2/platforms/platform_sdl2/platform_sdl2_renderer_opengl3.c`](../platforms/platform_sdl2/platform_sdl2_renderer_opengl3.c)

Other src2 backends using the same TRSPK core:

| Backend | File |
|---------|------|
| WebGL1 | `platform_sdl2_renderer_webgl1.c` |
| D3D9 | `platform_sdl2_renderer_d3d9.c` |
| Soft3D (immediate CPU) | `platform_sdl2_renderer_soft3d.c` |

## Related docs

- [docs/renderers.md](../../docs/renderers.md) — per-backend buffering and END_3D draw paths
- [src/platforms/ToriRSPlatformKit/README.md](../../src/platforms/ToriRSPlatformKit/README.md) — legacy toolkit (batch16/32, Metal backend)
