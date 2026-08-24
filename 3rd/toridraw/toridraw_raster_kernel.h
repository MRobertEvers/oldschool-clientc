#ifndef TORIDRAW_RASTER_KERNEL_H
#define TORIDRAW_RASTER_KERNEL_H

#include "toridraw_types.h"

#include <stdbool.h>

/*
 * The four drawable face classes produced by ToriDraw's model decoder.  Raw
 * face_infos values are not members of this enum: the core has already
 * applied their hidden/special policy before a callback is selected.
 */
enum ToriDraw_RasterFaceClass
{
    TORIDRAW_RASTER_FACE_GOURAUD = 0,
    TORIDRAW_RASTER_FACE_FLAT = 1,
    TORIDRAW_RASTER_FACE_TEXTURED = 2,
    TORIDRAW_RASTER_FACE_TEXTURED_FLAT = 3,
    TORIDRAW_RASTER_FACE_CLASS_COUNT = 4,
};

/* A kernel may participate in either renderer, or in both. */
enum ToriDraw_RasterKernelDomain
{
    TORIDRAW_RASTER_KERNEL_STOCK = 1u << 0,
    TORIDRAW_RASTER_KERNEL_HD = 1u << 1,
};

enum ToriDraw_RasterTextureGate
{
    TORIDRAW_RASTER_TEXTURE_OPAQUE = 0,
    TORIDRAW_RASTER_TEXTURE_COLOR_KEY = 1,
    TORIDRAW_RASTER_TEXTURE_TEXEL_ALPHA = 2,
};

/* What the tagged texture mapping union below contains. */
enum ToriDraw_RasterMappingPayload
{
    TORIDRAW_RASTER_MAPPING_VERTEX_FRAME = 0,
    TORIDRAW_RASTER_MAPPING_STOCK_FACE_FALLBACK = 1,
    TORIDRAW_RASTER_MAPPING_HD = 2,
    TORIDRAW_RASTER_MAPPING_HD_FRAME_FALLBACK = 3,
};

struct ToriDraw_RasterVertexFrame
{
    int p;
    int m;
    int n;
};

/*
 * Prepared texture state for one face.  It is meaningful only for the two
 * textured face classes.  ToriDraw has resolved and bounds-checked the texture
 * and the active mapping member before invoking a callback.
 */
struct ToriDraw_RasterTexture
{
    int texture_id;
    const int* texels;
    int width;
    int height;
    enum ToriDraw_RasterTextureGate gate;
    bool clamp_s;
    bool clamp_t;

    /* The original model render-type byte, retained for implementation policy. */
    unsigned int render_type;
    enum ToriDraw_RasterMappingPayload mapping_payload;
    union
    {
        struct ToriDraw_RasterVertexFrame vertex_frame;
        const struct ToriDraw_TexMapping* hd_mapping;
    } mapping;

    /* Prepared HD sampler inputs; zero/false for the stock domain. */
    bool modulate;
    int tint_r;
    int tint_g;
    int tint_b;
    int texture_neutral;
};

/*
 * Pass-stable input shared by every callback for one model.  The structure is
 * read-only, but pixel_buffer and zbuffer name writable render targets.  The
 * framebuffer pointer is rebased to clip_origin_x/clip_origin_y; width and
 * height describe that rebased region, and projection_center_* use its local
 * coordinates.
 *
 * All pointed-to storage is borrowed and valid only for the active raster
 * pass.  `internal` is reserved for ToriDraw's built-in implementations and is
 * not an extension point for application kernels.
 */
struct ToriDraw_RasterTarget
{
    enum ToriDraw_RasterKernelDomain domain;

    toripixel_t* pixel_buffer;
    torizdepth_t* zbuffer;
    int width;
    int height;
    int stride;
    int clip_origin_x;
    int clip_origin_y;
    int projection_center_x;
    int projection_center_y;

    int near_plane_z;
    int camera_cot16;
    int model_mid_z;
    bool parallel_projection;
    bool smooth_shading;
    bool affine_textures;
    bool depth_test;
    bool near_clip_available;

    int vertex_count;
    const int* screen_vertices_x;
    const int* screen_vertices_y;
    const int* screen_vertices_z;
    const int* orthographic_vertices_x;
    const int* orthographic_vertices_y;
    const int* orthographic_vertices_z;

    const vertexint_t* posed_vertices_x;
    const vertexint_t* posed_vertices_y;
    const vertexint_t* posed_vertices_z;
    const vertexint_t* bind_vertices_x;
    const vertexint_t* bind_vertices_y;
    const vertexint_t* bind_vertices_z;

    void* internal;
};

/*
 * Normalized input for one drawable face.  The same stack object may be reused
 * for the next face, so neither this pointer nor any descriptor pointer may be
 * retained by a callback.  Flat classes receive shade[0] repeated three times;
 * opacity is effective source coverage in the range 0..255.
 */
struct ToriDraw_RasterFace
{
    enum ToriDraw_RasterFaceClass face_class;
    int face_index;
    int vertex[3];
    int shade[3];
    int opacity;
    bool near_clipped;
    struct ToriDraw_RasterTexture texture;
};

typedef void (*ToriDraw_RasterKernelFaceFn)(
    void* user_data,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFace* face);

/*
 * NULL slots are sparse overrides: resolution continues through `fallback`
 * and finally through the active render entry point's terminal kernel.  A
 * deliberate no-op must therefore be represented by a real callback.
 */
struct ToriDraw_RasterKernelVTable
{
    ToriDraw_RasterKernelFaceFn draw_gouraud;
    ToriDraw_RasterKernelFaceFn draw_flat;
    ToriDraw_RasterKernelFaceFn draw_textured;
    ToriDraw_RasterKernelFaceFn draw_textured_flat;
};

/*
 * A scene borrows this object, its vtable, its fallback chain, and user_data.
 * They must remain alive and immutable while bound or while a raster pass is
 * active.  `domains` is a non-empty mask of ToriDraw_RasterKernelDomain bits.
 * A callback may synchronously render either a different scene or the same
 * scene again. Complete same-scene renders use an independently preallocated
 * render context. Concurrent use of one scene still requires external
 * synchronization.
 */
struct ToriDraw_RasterKernel
{
    const struct ToriDraw_RasterKernelVTable* vtable;
    void* user_data;
    const struct ToriDraw_RasterKernel* fallback;
    unsigned int domains;
};

/* Process-lifetime, immutable built-in terminal roots. */
const struct ToriDraw_RasterKernel*
ToriDraw_RasterKernelGetBranching(void);

const struct ToriDraw_RasterKernel*
ToriDraw_RasterKernelGetScanline(void);

const struct ToriDraw_RasterKernel*
ToriDraw_RasterKernelGetHDBranching(void);

const struct ToriDraw_RasterKernel*
ToriDraw_RasterKernelGetHDScanline(void);

/*
 * Bind a borrowed override/root chain to a scene.  Set rejects NULL, malformed
 * or cyclic chains.  Set and Reset both reject changes during an active raster
 * pass and leave the previous binding untouched on failure.  Reset stores the
 * NULL sentinel meaning "inherit the entry point's terminal".  Get returns the
 * explicit binding, so NULL is a normal answer.
 */
bool
ToriDraw_SceneSetRasterKernel(
    struct ToriDraw_Scene* scene,
    const struct ToriDraw_RasterKernel* kernel);

bool
ToriDraw_SceneResetRasterKernel(struct ToriDraw_Scene* scene);

const struct ToriDraw_RasterKernel*
ToriDraw_SceneGetRasterKernel(const struct ToriDraw_Scene* scene);

#endif
