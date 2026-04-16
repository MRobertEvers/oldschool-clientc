#ifndef PAINTERS_CULLMAP_BAKED_PATH_H
#define PAINTERS_CULLMAP_BAKED_PATH_H

#include <stddef.h>

/**
 * Baked cullmap identity: grid-snapped values used in generator filenames.
 * The `f{N}` / `fm{N}` segment in the basename comes from `nz` (near clip Z), matching
 * gen_painters_cullmap --near. `fov` is optional game state and is not used in filenames.
 */
struct PaintersCullmapBakedParams
{
    int radius;
    int fov;
    int w;
    int h;
    int nz;
};

/**
 * Fill params: smallest baked w/h/radius not smaller than viewport and draw_radius;
 * copies nz and fov through. Grid matches tools/ci/gen_painters_cullmap/batch_cullmaps.mjs.
 */
void
painters_cullmap_baked_params_resolve(
    int viewport_w,
    int viewport_h,
    int draw_radius,
    int near_z,
    int fov,
    struct PaintersCullmapBakedParams* out);

/**
 * malloc'd basename: painters_cullmap_baked_r{R}_f{Z}_w{W}_h{H}.bin (fm if nz < 0).
 * Caller frees with free(). NULL on allocation or overflow failure.
 */
char*
painters_cullmap_baked_format_filename(const struct PaintersCullmapBakedParams* p);

/**
 * malloc'd path: cullmaps_dir + '/' + format_filename(p). Caller frees with free().
 * NULL if dir or p is NULL, or on failure.
 */
char*
painters_cullmap_baked_format_filepath(
    const char* cullmaps_dir,
    const struct PaintersCullmapBakedParams* p);

#endif
