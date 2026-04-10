#include "painters_cullmap_baked_path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Must match tools/gen_painters_cullmap/batch_cullmaps.mjs (BAKE_SCREEN, BAKE_RADIUS). */
#define PCULL_BAKE_DIM_MIN 100
#define PCULL_BAKE_DIM_MAX 2000
#define PCULL_BAKE_DIM_STEP 100
#define PCULL_BAKE_RADIUS_MIN 5
#define PCULL_BAKE_RADIUS_MAX 50
#define PCULL_BAKE_RADIUS_STEP 5

static int
pick_baked_screen_dim(int v)
{
    const int step = PCULL_BAKE_DIM_STEP;
    const int vmin = PCULL_BAKE_DIM_MIN;
    const int vmax = PCULL_BAKE_DIM_MAX;
    if( v <= 0 )
        return vmin;
    int rounded = (v + step - 1) / step * step;
    if( rounded > vmax )
        rounded = vmax;
    if( rounded < vmin )
        rounded = vmin;
    return rounded;
}

static int
pick_baked_radius(int draw_radius)
{
    const int step = PCULL_BAKE_RADIUS_STEP;
    const int rmin = PCULL_BAKE_RADIUS_MIN;
    const int rmax = PCULL_BAKE_RADIUS_MAX;
    if( draw_radius <= 0 )
        return rmin;
    int rounded = (draw_radius + step - 1) / step * step;
    if( rounded > rmax )
        rounded = rmax;
    if( rounded < rmin )
        rounded = rmin;
    return rounded;
}

static void
format_fov(
    char* buf,
    size_t cap,
    int fov)
{
    snprintf(buf, cap, "f%d", fov);
}

static char*
dup_snippet(
    const char* src,
    size_t len)
{
    char* out = (char*)malloc(len + 1u);
    if( !out )
        return NULL;
    memcpy(out, src, len);
    out[len] = '\0';
    return out;
}

void
painters_cullmap_baked_params_resolve(
    int viewport_w,
    int viewport_h,
    int draw_radius,
    int near_z,
    int fov,
    struct PaintersCullmapBakedParams* out)
{
    if( !out )
        return;
    out->w = pick_baked_screen_dim(viewport_w);
    out->h = pick_baked_screen_dim(viewport_h);
    out->radius = pick_baked_radius(draw_radius);
    out->nz = near_z;
    out->fov = fov;
}

char*
painters_cullmap_baked_format_filename(const struct PaintersCullmapBakedParams* p)
{
    if( !p )
        return NULL;
    char fovpart[48];
    format_fov(fovpart, sizeof fovpart, p->fov);
    char tmp[256];
    int n = snprintf(
        tmp,
        sizeof tmp,
        "painters_cullmap_baked_r%d_%s_w%d_h%d.bin",
        p->radius,
        fovpart,
        p->w,
        p->h);
    if( n < 0 || (size_t)n >= sizeof tmp )
        return NULL;
    return dup_snippet(tmp, (size_t)n);
}

char*
painters_cullmap_baked_format_filepath(
    const char* cullmaps_dir,
    const struct PaintersCullmapBakedParams* p)
{
    if( !cullmaps_dir || !p )
        return NULL;
    char fovpart[48];
    format_fov(fovpart, sizeof fovpart, p->fov);
    char tmp[1024];
    int n = snprintf(
        tmp,
        sizeof tmp,
        "%s/painters_cullmap_baked_r%d_%s_w%d_h%d.bin",
        cullmaps_dir,
        p->radius,
        fovpart,
        p->w,
        p->h);
    if( n < 0 || (size_t)n >= sizeof tmp )
        return NULL;
    return dup_snippet(tmp, (size_t)n);
}
