# Painters cullmap blobs

Files named `painters_cullmap_baked_r{R}_f{Z}_w{W}_h{H}.bin` are **generated** binary visibility tables for the painters frustum cullmap. Do not edit them by hand.

## Regenerate

From the repository root (or any working directory):

1. Build the host tool:

   ```sh
   cd tools/ci/gen_painters_cullmap && make
   ```

2. Run the batch script (writes into this directory by default):

   ```sh
   node tools/ci/gen_painters_cullmap/batch_cullmaps.mjs
   ```

   Options:

   - `--near <int>` — near clip Z passed to the generator (default `50`, matching `camera->near_plane_z` in `LibToriRS_GameNew`; must match runtime when loading).
   - `--out-dir <path>` — output directory (default: this `cullmaps` folder).
   - `-j [N]`, `--jobs [N]` — run up to `N` generator processes in parallel (default: CPU count; `-j 1` is serial). With `jobs > 1`, child `stdio` is discarded to avoid interleaved output.
   - `--dry-run` — print `gen_painters_cullmap` command lines without running them.

Viewport and radius grids are defined in `tools/ci/gen_painters_cullmap/batch_cullmaps.mjs` (`BAKE_SCREEN`, `BAKE_RADIUS`, `DEFAULT_NEAR`). The same numbers are duplicated in `src/osrs/painters_cullmap_baked_path.c` (`PCULL_BAKE_*`) for runtime picking.

`painters_cullmap_baked_path` fills `struct PaintersCullmapBakedParams` and returns malloc’d filename/path strings; it does not read files. Loading uses `painters_cullmap_from_blob` after the game reads bytes, or `painters_cullmap_build` for a runtime bake.

`.bin` files are listed in `.gitignore`; only this README is tracked.
