/*
 * The two stock projection kernels, against each other, vertex for vertex.
 *
 *   make -C src test-projection-kernel
 *
 * WHAT IT PINS
 *
 * ToriDraw_ProjectionKernelGetPrepared and ...GetPortable differ in exactly
 * the two perspective vtable slots. `prepared` sends a yaw-only model under a
 * published camera to the hand-written prepared-camera kernel -- the AArch64
 * assembly where this lane builds it, the SSE2 fused-yaw pair otherwise --
 * and falls through to the portable ladder for everything else. `portable`
 * never takes that path at all.
 *
 * They are an A/B of the same projection, so they must agree EXACTLY: same
 * screen x/y/z, same camera-space x/y/z for a textured model, same near-clip
 * sentinel placement, same screen box, same cull verdict. A divergence is
 * either a bug in the prepared kernel or a claim that the two are not
 * interchangeable, and the whole point of making them selectable is that they
 * are.
 *
 * The coverage matters as much as the comparison. The prepared gate needs
 * yaw-only geometry, a camera whose prepared block was published, and (for the
 * assembly) at least four vertices -- so the fixtures cross model yaw against
 * no yaw, textured against not, sizes that straddle the 4-vertex floor and the
 * 4-wide block boundary, and a placement with the camera inside the model so
 * the clipping family runs and the sentinel case is real. A pass that never
 * exercised the prepared path would be a pass by vacuity, so the run counts
 * how many models actually took it and fails if that is zero.
 */

#include "toridraw.h"
#include "toridraw_model.h"
#include "toridraw_model_transform.h"
#include "toridraw_raster_kernel.h"
#include "toridraw_scene.h"
#include "toridraw_types.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIEW_W 512
#define VIEW_H 334

static uint32_t g_rng = 0x9E3779B9u;
static int g_checked = 0;
static int g_prepared_eligible = 0;
static int g_clipped = 0;
static int g_failures = 0;

static uint32_t
rnd(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

static int
rnd_range(int lo, int hi)
{
    return lo + (int)(rnd() % (uint32_t)(hi - lo + 1));
}

static struct ToriDraw_Model*
make_model(int vertex_count, int extent, int textured)
{
    int const face_count = vertex_count < 3 ? 1 : vertex_count / 2;
    struct ToriDraw_Model* m = ToriDraw_ModelNew(vertex_count, face_count, 0);
    assert(m);

    m->vertices_x = malloc((size_t)vertex_count * sizeof(vertexint_t));
    m->vertices_y = malloc((size_t)vertex_count * sizeof(vertexint_t));
    m->vertices_z = malloc((size_t)vertex_count * sizeof(vertexint_t));
    m->face_indices_a = malloc((size_t)face_count * sizeof(faceint_t));
    m->face_indices_b = malloc((size_t)face_count * sizeof(faceint_t));
    m->face_indices_c = malloc((size_t)face_count * sizeof(faceint_t));
    m->face_colors_a = malloc((size_t)face_count * sizeof(hsl16_t));
    m->face_colors_b = malloc((size_t)face_count * sizeof(hsl16_t));
    m->face_colors_c = malloc((size_t)face_count * sizeof(hsl16_t));
    assert(m->vertices_x);
    assert(m->vertices_y);
    assert(m->vertices_z);
    assert(m->face_indices_a);
    assert(m->face_indices_b);
    assert(m->face_indices_c);
    assert(m->face_colors_a);
    assert(m->face_colors_b);
    assert(m->face_colors_c);
    if( textured )
    {
        m->face_textures = malloc((size_t)face_count * sizeof(faceint_t));
        assert(m->face_textures);
        m->textured_face_count = face_count;
    }

    for( int i = 0; i < vertex_count; i++ )
    {
        m->vertices_x[i] = (vertexint_t)rnd_range(-extent, extent);
        m->vertices_y[i] = (vertexint_t)rnd_range(-extent, extent);
        m->vertices_z[i] = (vertexint_t)rnd_range(-extent, extent);
    }
    for( int f = 0; f < face_count; f++ )
    {
        m->face_indices_a[f] = (faceint_t)rnd_range(0, vertex_count - 1);
        m->face_indices_b[f] = (faceint_t)rnd_range(0, vertex_count - 1);
        m->face_indices_c[f] = (faceint_t)rnd_range(0, vertex_count - 1);
        m->face_colors_a[f] = (hsl16_t)(rnd() & 0xFFFF);
        m->face_colors_b[f] = (hsl16_t)(rnd() & 0xFFFF);
        m->face_colors_c[f] = (hsl16_t)(rnd() & 0xFFFF);
        if( textured )
            m->face_textures[f] = 1;
    }
    ToriDraw_ModelSetBoundsCylinder(m);
    return m;
}

/* One projection run's whole observable result. */
struct Shot
{
    int cull;
    int count;
    int* sx;
    int* sy;
    int* sz;
    int* ox;
    int* oy;
    int* oz;
    struct ToriDraw_AABB aabb;
    int near_clipped;
};

static void
shot_take(
    struct Shot* shot,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* vp,
    struct ToriDraw_Camera* cam,
    const struct ToriDraw_ProjectionKernel* projection,
    int vertex_count)
{
    struct ToriDraw_RasterKernelSD kernel = *ToriDraw_RasterKernelSDGetBranching();
    size_t bytes;

    kernel.projection = projection;
    shot->cull = ToriDraw_RenderModel1ProjectWithKernel(hnd, scene, position, vp, cam, &kernel);
    shot->count = vertex_count;
    shot->near_clipped = scene->near_clipped ? 1 : 0;
    shot->aabb = scene->aabb;

    bytes = (size_t)shot->count * sizeof(int);
    shot->sx = malloc(bytes);
    shot->sy = malloc(bytes);
    shot->sz = malloc(bytes);
    shot->ox = malloc(bytes);
    shot->oy = malloc(bytes);
    shot->oz = malloc(bytes);
    assert(shot->sx);
    assert(shot->sy);
    assert(shot->sz);
    assert(shot->ox);
    assert(shot->oy);
    assert(shot->oz);
    memcpy(shot->sx, scene->screen_vertices_x, bytes);
    memcpy(shot->sy, scene->screen_vertices_y, bytes);
    memcpy(shot->sz, scene->screen_vertices_z, bytes);
    memcpy(shot->ox, scene->orthographic_vertices_x, bytes);
    memcpy(shot->oy, scene->orthographic_vertices_y, bytes);
    memcpy(shot->oz, scene->orthographic_vertices_z, bytes);
}

static void
shot_free(struct Shot* shot)
{
    free(shot->sx);
    free(shot->sy);
    free(shot->sz);
    free(shot->ox);
    free(shot->oy);
    free(shot->oz);
}

static void
compare(const struct Shot* a, const struct Shot* b, const char* label, int textured)
{
    if( a->cull != b->cull )
    {
        printf("  %s: cull verdict %d vs %d\n", label, a->cull, b->cull);
        g_failures++;
        return;
    }
    if( a->cull != TORIDRAW_CULL_VISIBLE )
        return;

    if( a->near_clipped != b->near_clipped )
    {
        printf("  %s: near_clipped %d vs %d\n", label, a->near_clipped, b->near_clipped);
        g_failures++;
    }
    if( a->aabb.min_screen_x != b->aabb.min_screen_x ||
        a->aabb.max_screen_x != b->aabb.max_screen_x ||
        a->aabb.min_screen_y != b->aabb.min_screen_y ||
        a->aabb.max_screen_y != b->aabb.max_screen_y )
    {
        printf(
            "  %s: box [%d..%d, %d..%d] vs [%d..%d, %d..%d]\n", label, a->aabb.min_screen_x,
            a->aabb.max_screen_x, a->aabb.min_screen_y, a->aabb.max_screen_y,
            b->aabb.min_screen_x, b->aabb.max_screen_x, b->aabb.min_screen_y,
            b->aabb.max_screen_y);
        g_failures++;
    }

    for( int i = 0; i < a->count; i++ )
    {
        if( a->sx[i] != b->sx[i] || a->sy[i] != b->sy[i] || a->sz[i] != b->sz[i] )
        {
            printf(
                "  %s: vertex %d screen (%d,%d,%d) vs (%d,%d,%d)\n", label, i, a->sx[i],
                a->sy[i], a->sz[i], b->sx[i], b->sy[i], b->sz[i]);
            g_failures++;
            return;
        }
        /* Camera-space scratch is written only by the textured family; the
         * untextured kernels leave whatever the previous model put there. */
        if( textured && (a->ox[i] != b->ox[i] || a->oy[i] != b->oy[i] || a->oz[i] != b->oz[i]) )
        {
            printf(
                "  %s: vertex %d camera (%d,%d,%d) vs (%d,%d,%d)\n", label, i, a->ox[i],
                a->oy[i], a->oz[i], b->ox[i], b->oy[i], b->oz[i]);
            g_failures++;
            return;
        }
    }
}

int
main(void)
{
    struct ToriDraw_Scene* scene;
    struct ToriDraw_ViewPort vp = {
        .width = VIEW_W,
        .height = VIEW_H,
        .stride = VIEW_W,
        .x_center = VIEW_W / 2,
        .y_center = VIEW_H / 2,
        .clip_bottom = VIEW_H,
    };
    struct ToriDraw_Camera camera = {
        .near_plane_z = 50,
        .fov_rpi2048 = 512,
        .pitch = 128,
        .yaw = 0,
    };
    /* Sizes that straddle the assembly's 4-vertex floor and the 4-wide block
     * boundary, plus a spread through the census buckets. */
    static const int sizes[] = { 1,  2,  3,  4,   5,   7,   8,   9,  11,  15,
                                 16, 17, 31, 32,  33,  63,  64,  65, 100, 128,
                                 129, 200, 255, 256, 257, 390 };

    ToriDraw_Init();
    scene = ToriDraw_SceneNew(TORIDRAW_SCENE_SMALL, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    assert(scene);
    /* The same pointer the projection is handed, so the prepared gate passes
     * exactly as it does in the client. */
    ToriDraw_ScenePrepareProjectionCamera(scene, &camera);

    for( int pass = 0; pass < 2; pass++ )
    {
        /* Pass 0: the model well in front of the camera (no-clip family, the
         * one the prepared assembly serves). Pass 1: the camera inside the
         * model's sphere, so the clipping family runs and vertices straddle
         * the near plane. */
        int const pos_z = pass == 0 ? 900 : 40;
        int const extent = pass == 0 ? 120 : 200;

        for( size_t si = 0; si < sizeof(sizes) / sizeof(sizes[0]); si++ )
        {
            for( int textured = 0; textured < 2; textured++ )
            {
                for( int yaw_case = 0; yaw_case < 2; yaw_case++ )
                {
                    struct ToriDraw_Model* m = make_model(sizes[si], extent, textured);
                    struct ToriDraw_ModelHandle hnd = ToriDraw_ModelHandleOwned(m);
                    struct ToriDraw_Position position = {
                        .x = 0,
                        .y = 0,
                        .z = pos_z,
                        /* yaw_case 0 exercises the kernel's no-yaw loop, which
                         * is a separate body from the yaw one. */
                        .yaw = yaw_case ? 700 : 0,
                    };
                    struct Shot prepared;
                    struct Shot portable;
                    char label[96];

                    snprintf(
                        label, sizeof(label), "pass%d size%d %s %s", pass, sizes[si],
                        textured ? "tex" : "notex", yaw_case ? "yaw" : "noyaw");

                    shot_take(
                        &prepared, scene, hnd, &position, &vp, &camera,
                        ToriDraw_ProjectionKernelGetPrepared(), sizes[si]);
                    shot_take(
                        &portable, scene, hnd, &position, &vp, &camera,
                        ToriDraw_ProjectionKernelGetPortable(), sizes[si]);

                    compare(&prepared, &portable, label, textured);
                    g_checked++;
                    if( prepared.cull == TORIDRAW_CULL_VISIBLE )
                    {
                        /* The gate the prepared vtable's perspective slots
                         * apply: yaw-only geometry under a published camera. */
                        if( sizes[si] >= 4 )
                            g_prepared_eligible++;
                        if( prepared.near_clipped )
                            g_clipped++;
                    }

                    shot_free(&prepared);
                    shot_free(&portable);
                    ToriDraw_ModelFree(m);
                }
            }
        }
    }

    printf(
        "projection kernels: %d fixtures compared (%d reached the prepared gate, "
        "%d near-clipped) -- %s\n",
        g_checked, g_prepared_eligible, g_clipped, g_failures ? "FAIL" : "PASS");

    if( g_prepared_eligible == 0 )
    {
        printf("  no fixture ever reached the prepared gate: the A/B compared the "
               "portable kernel with itself -- FAIL\n");
        g_failures++;
    }
    if( g_clipped == 0 )
    {
        printf("  no fixture ever near-clipped: the clipping slots were not "
               "exercised -- FAIL\n");
        g_failures++;
    }

    ToriDraw_SceneFree(scene);
    return g_failures ? 1 : 0;
}
