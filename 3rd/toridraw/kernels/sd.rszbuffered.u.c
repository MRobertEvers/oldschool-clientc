#ifndef TORIDRAW_KERNELS_SD_RSZBUFFERED_U_C
#define TORIDRAW_KERNELS_SD_RSZBUFFERED_U_C

/*
 * The rev-241 client's depth-buffered raster, one kernel object per pass.
 *
 * ONE vtable, two objects. The face callbacks are the same in both passes;
 * what differs is which faces the walk lets through, which is the pass the
 * object carries as its user_data. The walk is its own (toridraw_rszbuf_walk,
 * impl/raster/walk/walk.rszbuf.u.c): the client's face orders are not the
 * stock walk's, and it reads the rev-241 projection's scratch.
 *
 * Both need the frame-wide depth buffer (ToriDraw_SceneZBufferBeginPass), and
 * the RSZBUF scratch group the rszbuf projection fills.
 */

#include "impl/raster/walk/walk.rszbuf.u.c"

static const struct ToriDraw_RasterKernelSDVTable g_rszbuffered_vtable = {
    .draw = {
        [TORIDRAW_RASTER_FACE_SD_GOURAUD] = toridraw_rszbuf_gouraud,
        [TORIDRAW_RASTER_FACE_SD_FLAT] = toridraw_rszbuf_flat,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED] = toridraw_rszbuf_textured,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED_FLAT] = toridraw_rszbuf_textured_flat,
    },
};

static struct ToriDraw_RsZbufPassSpec g_rszbuffered_opaque_pass = {
    .pass = TORIDRAW_RSZBUF_PASS_OPAQUE,
};

static struct ToriDraw_RsZbufPassSpec g_rszbuffered_translucent_pass = {
    .pass = TORIDRAW_RSZBUF_PASS_TRANSLUCENT,
};

static struct ToriDraw_RasterKernelSD g_rszbuffered_opaque_kernel = {
    .name = "rszbuffered_opaque",
    .draw_model = toridraw_rszbuf_walk,
    .vtable = &g_rszbuffered_vtable,
    .user_data = &g_rszbuffered_opaque_pass,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER |
             TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_RSZBUF_SCRATCH,
};

static struct ToriDraw_RasterKernelSD g_rszbuffered_translucent_kernel = {
    .name = "rszbuffered_translucent",
    .draw_model = toridraw_rszbuf_walk,
    .vtable = &g_rszbuffered_vtable,
    .user_data = &g_rszbuffered_translucent_pass,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER |
             TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_RSZBUF_SCRATCH,
};

#endif /* TORIDRAW_KERNELS_SD_RSZBUFFERED_U_C */
