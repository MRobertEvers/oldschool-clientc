# Vendored Nuklear backends

Upstream: [Immediate-Mode-UI/Nuklear](https://github.com/Immediate-Mode-UI/Nuklear) (files copied from `master` as of vendor date).

- `nuklear.h` — root amalgamated header.
- `sdl_opengl3/nuklear_torirs_sdl_gl3.h` — prefixed copy of `demo/sdl_opengl3/nuklear_sdl_gl3.h` (avoids duplicate `nk_sdl_*` symbols when linking multiple SDL backends in one binary).
- `sdl_opengles2/nuklear_torirs_sdl_gles2.h` — prefixed `demo/sdl_opengles2/nuklear_sdl_gles2.h`.
- `sdl_renderer/nuklear_torirs_sdl_renderer.h` — prefixed `demo/sdl_renderer/nuklear_sdl_renderer.h`.
- `d3d11/` — `demo/d3d11` (Direct3D 11).
- `gdi/nuklear_gdi.h`, `rawfb/nuklear_rawfb.h` — reference / future Win32 or framebuffer targets (not linked in default `osx` build).

Metal UI drawing is implemented under `nuklear_metal_sdl.mm` / `nuklear_metal_sdl.h` in this folder (not shipped by upstream Nuklear).
