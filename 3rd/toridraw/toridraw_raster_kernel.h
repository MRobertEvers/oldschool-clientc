#ifndef TORIDRAW_RASTER_KERNEL_H
#define TORIDRAW_RASTER_KERNEL_H

#include "toridraw_types.h"

#include <stdbool.h>
#include <stdint.h>

enum ToriDraw_RasterKernelFlags
{
    /* Traverse model face order and do not provision a depth buffer. */
    TORIDRAW_RASTER_KERNEL_FLAG_NONE = 0,
    /* Produce and consume ToriDraw's back-to-front face order. */
    TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING = 1u << 0,
    /* Provision, rebase, and reset a model-local depth buffer. */
    TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER = 1u << 1,
};

/* The four terminal algorithms in the stock/SD face rasterizer. */
enum ToriDraw_RasterFaceClassSD
{
    TORIDRAW_RASTER_FACE_SD_GOURAUD = 0,
    TORIDRAW_RASTER_FACE_SD_FLAT = 1,
    TORIDRAW_RASTER_FACE_SD_TEXTURED = 2,
    TORIDRAW_RASTER_FACE_SD_TEXTURED_FLAT = 3,
    TORIDRAW_RASTER_FACE_SD_CLASS_COUNT = 4,
};

/*
 * HD has two solid algorithms and four texture projection algorithms. Texture
 * shading, face alpha, texel gating, modulation, and depth testing are inputs
 * to these algorithms; they are deliberately not additional vtable axes.
 */
enum ToriDraw_RasterFaceClassHD
{
    TORIDRAW_RASTER_FACE_HD_GOURAUD = 0,
    TORIDRAW_RASTER_FACE_HD_FLAT = 1,
    TORIDRAW_RASTER_FACE_HD_TEXTURED_PLANE = 2,
    TORIDRAW_RASTER_FACE_HD_TEXTURED_CYLINDER = 3,
    TORIDRAW_RASTER_FACE_HD_TEXTURED_CUBE = 4,
    TORIDRAW_RASTER_FACE_HD_TEXTURED_SPHERE = 5,
    TORIDRAW_RASTER_FACE_HD_CLASS_COUNT = 6,
};

enum ToriDraw_RasterTextureGate
{
    TORIDRAW_RASTER_TEXTURE_OPAQUE = 0,
    TORIDRAW_RASTER_TEXTURE_COLOR_KEY = 1,
    TORIDRAW_RASTER_TEXTURE_TEXEL_ALPHA = 2,
};

struct ToriDraw_RasterVertexFrame
{
    int p;
    int m;
    int n;
};

/*
 * Prepared texture state for one textured face. ToriDraw has resolved and
 * bounds-checked the texture and mapping before invoking a textured callback;
 * solid callbacks must not inspect their face's texture member.
 */
struct ToriDraw_RasterTextureSD
{
    int texture_id;
    const int* texels;
    int width;
    int height;
    enum ToriDraw_RasterTextureGate gate;
    /* Non-zero render types select the stock affine texture family. */
    unsigned int render_type;
    bool frame_fallback;
    struct ToriDraw_RasterVertexFrame frame;
};

struct ToriDraw_RasterTextureHD
{
    int texture_id;
    const int* texels;
    int width;
    int height;
    enum ToriDraw_RasterTextureGate gate;
    bool clamp_s;
    bool clamp_t;

    /* Original raw render type. face_class selects the active mapping member. */
    unsigned int render_type;
    bool frame_fallback;
    union
    {
        struct ToriDraw_RasterVertexFrame vertex_frame;
        const struct ToriDraw_TexMapping* hd_mapping;
    } mapping;

    bool modulate;
    int tint_r;
    int tint_g;
    int tint_b;
    int texture_neutral;
};

/*
 * Pass-stable input shared by every callback for one model. The structure is
 * read-only, but pixel_buffer and zbuffer name writable render targets. The
 * framebuffer pointer is rebased to clip_origin_x/clip_origin_y; width and
 * height describe that rebased region, and projection_center_* use its local
 * coordinates.
 *
 * All pointed-to storage is borrowed and valid only for the active render
 * call. `internal` is reserved for ToriDraw's built-in implementations and is
 * not an extension point for application kernels.
 */
struct ToriDraw_RasterTarget
{
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
 * Normalized input for one drawable face. The same stack object may be reused
 * for the next face, so neither it nor any descriptor pointer may be retained.
 * Flat classes receive shade[0] repeated three times; opacity is effective
 * source coverage in the range 0..255.
 */
struct ToriDraw_RasterFaceSD
{
    enum ToriDraw_RasterFaceClassSD face_class;
    int face_index;
    int vertex[3];
    int shade[3];
    int opacity;
    bool near_clipped;
    struct ToriDraw_RasterTextureSD texture;
};

struct ToriDraw_RasterFaceHD
{
    enum ToriDraw_RasterFaceClassHD face_class;
    int face_index;
    int vertex[3];
    int shade[3];
    int opacity;
    bool near_clipped;
    struct ToriDraw_RasterTextureHD texture;
};

typedef void (*ToriDraw_RasterKernelSDFaceFn)(
    void* user_data,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceSD* face);

typedef void (*ToriDraw_RasterKernelHDFaceFn)(
    void* user_data,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceHD* face);

/* Every slot is required. A kernel with a NULL callback is incomplete. */
struct ToriDraw_RasterKernelSDVTable
{
    ToriDraw_RasterKernelSDFaceFn draw[TORIDRAW_RASTER_FACE_SD_CLASS_COUNT];
};

struct ToriDraw_RasterKernelHDVTable
{
    ToriDraw_RasterKernelHDFaceFn draw[TORIDRAW_RASTER_FACE_HD_CLASS_COUNT];
};

/*
 * The two stages in front of the face loop, each its own kernel.
 *
 * A model draw is three stages -- project the vertices, cull and sort the
 * faces, raster the faces -- and a raster kernel names all three, so a scene
 * can be timed with any one of them swapped and the other two held. The
 * stock objects are process-lifetime and immutable, like the raster kernels.
 *
 * PROJECTION: model space to screen space into the scene's scratch
 * (screen_vertices_*, orthographic_vertices_*, near_clipped, the projected
 * centre). Returns a TORIDRAW_CULL_* verdict; anything but VISIBLE means the
 * later stages do not run.
 *
 * FACE CULL + SORT: the winding cull and the back-to-front order, into
 * scene->tmp_face_order, returning the count. `presort` asks it to leave
 * the y-sorted screen coordinates behind for the batched raster walk
 * (scene->sm_face_x4 / y4, recorded in sm_face_xy_valid); a kernel that
 * cannot must say so there rather than leave a stale stash.
 *
 * The stock face-sort kernels:
 *
 *   bucket  the depth-bucket sort -- one scalar winding test per face,
 *           faces scattered into per-depth lists, walked from far to near.
 *   flat    a four-wide SIMD winding cull compacted into (depth, face) keys,
 *           then a bitonic network (<= 256 keys) or a two-pass radix, and
 *           the order read straight off the sorted keys. Same order as
 *           `bucket`, face for face (toridraw_face_sort_flat_test.c).
 *
 * A NULL slot is the default: the stock projection, and whichever sort
 * TORIDRAW_FACE_SORT selects (see toridraw_face_sort_flat.u.c).
 */
typedef int (*ToriDraw_ProjectionKernelFn)(
    void* user_data,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera);

typedef int (*ToriDraw_FaceCullSortKernelFn)(
    void* user_data,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    bool presort);

struct ToriDraw_ProjectionKernel
{
    const char* name;
    ToriDraw_ProjectionKernelFn project;
    void* user_data;
};

struct ToriDraw_FaceCullSortKernel
{
    const char* name;
    ToriDraw_FaceCullSortKernelFn sort;
    void* user_data;
};

/*
 * A render call borrows the selected object, its complete vtable, and
 * user_data. They must remain alive and immutable until that call returns.
 * Flags describe pass-wide requirements and are read before the face loop.
 *
 * Rendering one scene recursively or concurrently is outside the contract:
 * the scene owns one startup-allocated projection/sort scratch set.
 */
struct ToriDraw_RasterKernelSD
{
    const struct ToriDraw_RasterKernelSDVTable* vtable;
    void* user_data;
    uint32_t flags;

    /* The stages in front of the face loop; NULL selects the stock one. */
    const struct ToriDraw_ProjectionKernel* projection;
    const struct ToriDraw_FaceCullSortKernel* face_sort;
};

struct ToriDraw_RasterKernelHD
{
    const struct ToriDraw_RasterKernelHDVTable* vtable;
    void* user_data;
    uint32_t flags;
};

/* Process-lifetime, immutable built-in kernels. */
const struct ToriDraw_ProjectionKernel*
ToriDraw_ProjectionKernelGetDefault(void);

const struct ToriDraw_FaceCullSortKernel*
ToriDraw_FaceCullSortKernelGetBucket(void);

const struct ToriDraw_FaceCullSortKernel*
ToriDraw_FaceCullSortKernelGetFlat(void);

/* Whichever of the two TORIDRAW_FACE_SORT / ToriDraw_FaceSortSetFlat name. */
const struct ToriDraw_FaceCullSortKernel*
ToriDraw_FaceCullSortKernelGetDefault(void);

const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetBranching(void);

const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetScanline(void);

const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetSmoothBranching(void);

const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetSmoothScanline(void);

const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetZBuffered(void);

const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetSmoothZBuffered(void);

const struct ToriDraw_RasterKernelHD*
ToriDraw_RasterKernelHDGetBranching(void);

const struct ToriDraw_RasterKernelHD*
ToriDraw_RasterKernelHDGetScanline(void);

const struct ToriDraw_RasterKernelHD*
ToriDraw_RasterKernelHDGetZBuffered(void);

#endif
