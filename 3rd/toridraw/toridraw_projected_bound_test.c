/*
 * The screen box ToriDraw_Project leaves in scene->aabb, checked against one
 * computed the slow way from the projected vertices it also leaves behind.
 *
 *   make -C src test-projected-bound
 *
 * WHAT IT PINS
 *
 * toridraw_projected_bound has three ways of arriving at the same box: the
 * AArch64 prepared kernel accumulates lane-wise min/max in registers as it
 * projects and hands the tail (count & 3) back; the NEON / SSE4.1 sweep reads
 * the outputs four at a time; the scalar loop does the rest. Every one of
 * them has to agree with a plain loop over screen_vertices_x/y, dilated by
 * TORIDRAW_PICK_SLOP and offset to the framebuffer -- or, when the clipping
 * family parked a vertex at the near-clip sentinel, be the whole plane.
 *
 * So: many sizes (every 1..40, then up through the largest census bucket),
 * both model-yaw shapes (the kernel has a separate no-yaw loop), textured and
 * not (separate entry points), and a placement with the camera inside the
 * model so the clipping family runs and the sentinel case is real.
 */

#include "toridraw.h"
#include "toridraw_model.h"
#include "toridraw_scene.h"
#include "toridraw_types.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIEW_W 512
#define VIEW_H 334
#define SLOP 5
#define NEAR_CLIP_SENTINEL (-5000)

static uint32_t g_rng = 0x2545F491u;

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
        /* ToriDraw_ModelHasTextures reads the count, not the array; both
         * are set so the textured entry point is the one that runs. */
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

static int g_checked = 0;
static int g_clipped_boxes = 0;
static int g_culled = 0;

/* One projection, one comparison. Returns 0 on agreement. */
static int
check_one(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ViewPort* vp,
    struct ToriDraw_Camera* cam,
    int vertex_count,
    int model_yaw,
    int textured,
    int pos_z,
    int extent)
{
    struct ToriDraw_Model* m = make_model(vertex_count, extent, textured);
    struct ToriDraw_ModelHandle hnd = ToriDraw_ModelHandleOwned(m);
    struct ToriDraw_Position position = { .z = pos_z, .yaw = model_yaw };
    int cull;
    int min_x = INT_MAX;
    int max_x = INT_MIN;
    int min_y = INT_MAX;
    int max_y = INT_MIN;
    int sentinel = 0;
    int fail = 0;

    cull = ToriDraw_RenderModel1Project(hnd, scene, &position, vp, cam);
    if( cull != TORIDRAW_CULL_VISIBLE && cull != TORIDRAW_CULL_AABB )
    {
        /* The fast cull never saw the box; nothing to compare. */
        g_culled++;
        ToriDraw_ModelFree(m);
        return 0;
    }

    for( int i = 0; i < vertex_count; i++ )
    {
        int const sx = scene->screen_vertices_x[i];
        int const sy = scene->screen_vertices_y[i];
        if( scene->near_clipped && sx == NEAR_CLIP_SENTINEL )
            sentinel = 1;
        if( sx < min_x )
            min_x = sx;
        if( sx > max_x )
            max_x = sx;
        if( sy < min_y )
            min_y = sy;
        if( sy > max_y )
            max_y = sy;
    }

    if( scene->aabb.kind != TORIDRAW_AABB_KIND_VERTICES )
    {
        printf("FAIL n=%d: aabb kind %d\n", vertex_count, scene->aabb.kind);
        fail = 1;
    }
    else if( sentinel )
    {
        g_clipped_boxes++;
        if( scene->aabb.min_screen_x != INT_MIN / 2 || scene->aabb.max_screen_x != INT_MAX / 2 ||
            scene->aabb.min_screen_y != INT_MIN / 2 || scene->aabb.max_screen_y != INT_MAX / 2 )
        {
            printf(
                "FAIL n=%d yaw=%d tex=%d z=%d: sentinel present but box is finite "
                "[%d,%d]x[%d,%d]\n",
                vertex_count, model_yaw, textured, pos_z, scene->aabb.min_screen_x,
                scene->aabb.max_screen_x, scene->aabb.min_screen_y, scene->aabb.max_screen_y);
            fail = 1;
        }
    }
    else
    {
        int const want_min_x = min_x + vp->x_center - SLOP;
        int const want_max_x = max_x + vp->x_center + SLOP;
        int const want_min_y = min_y + vp->y_center - SLOP;
        int const want_max_y = max_y + vp->y_center + SLOP;
        if( scene->aabb.min_screen_x != want_min_x || scene->aabb.max_screen_x != want_max_x ||
            scene->aabb.min_screen_y != want_min_y || scene->aabb.max_screen_y != want_max_y )
        {
            printf(
                "FAIL n=%d yaw=%d tex=%d z=%d: box [%d,%d]x[%d,%d] want [%d,%d]x[%d,%d] "
                "(bound_vertices=%d near_clipped=%d)\n",
                vertex_count, model_yaw, textured, pos_z, scene->aabb.min_screen_x,
                scene->aabb.max_screen_x, scene->aabb.min_screen_y, scene->aabb.max_screen_y,
                want_min_x, want_max_x, want_min_y, want_max_y,
                scene->projection_bound_vertices, (int)scene->near_clipped);
            fail = 1;
        }
        /* And the cull verdict has to be the box's own. */
        {
            int const left = vp->x_center - vp->width / 2;
            int const top = vp->y_center - vp->height / 2;
            int const off = want_min_x >= left + vp->width || want_min_y >= top + vp->height ||
                            want_max_x < left || want_max_y < top;
            if( off != (cull == TORIDRAW_CULL_AABB) )
            {
                printf("FAIL n=%d: box off-screen=%d but cull=%d\n", vertex_count, off, cull);
                fail = 1;
            }
        }
    }

    g_checked++;
    ToriDraw_ModelFree(m);
    return fail;
}

int
main(void)
{
    struct ToriDraw_Scene* scene;
    struct ToriDraw_ViewPort vp = {
        .width = VIEW_W,
        .height = VIEW_H,
        .stride = VIEW_W,
        /* Not at the origin, so a box in viewport coordinates and a box in
         * framebuffer coordinates cannot be confused for one another. */
        .x_center = 4 + VIEW_W / 2,
        .y_center = 4 + VIEW_H / 2,
        .clip_left = 4,
        .clip_top = 4,
        .clip_right = 4 + VIEW_W,
        .clip_bottom = 4 + VIEW_H,
    };
    struct ToriDraw_Camera camera = {
        .projection_mode = TORIDRAW_PROJECTION_MODE_SCALE,
        .projection_scale = TORIDRAW_PROJECTION_SCALE_DEFAULT,
        .near_plane_z = 50,
        .pitch = 128,
        .yaw = 0,
    };
    static const int sizes[] = { 41, 44, 47, 48, 49, 51, 60, 63, 64, 65, 100, 127, 128, 129,
                                 253, 256, 300, 380, 390 };
    int failures = 0;

    ToriDraw_Init();
    scene = ToriDraw_SceneNew(TORIDRAW_SCENE_SMALL, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    assert(scene);
    /* The same pointer the projection is handed, so the prepared (assembly)
     * gate passes exactly as it does in the client. */
    ToriDraw_ScenePrepareProjectionCamera(scene, &camera);

    for( int pass = 0; pass < 2; pass++ )
    {
        /* Pass 0: the model well in front of the camera (no-clip family, the
         * kernel that fills the bound block). Pass 1: the camera inside the
         * model's sphere (clipping family; vertices straddle the near plane
         * and some of every few models carry the sentinel). */
        int const pos_z = pass == 0 ? 900 : 40;
        int const extent = pass == 0 ? 120 : 200;

        for( int yaw = 0; yaw < 2; yaw++ )
            for( int tex = 0; tex < 2; tex++ )
            {
                int const model_yaw = yaw ? 300 : 0;
                for( int n = 1; n <= 40; n++ )
                    for( int rep = 0; rep < 3; rep++ )
                        failures += check_one(
                            scene, &vp, &camera, n, model_yaw, tex, pos_z, extent);
                for( size_t si = 0; si < sizeof(sizes) / sizeof(sizes[0]); si++ )
                    for( int rep = 0; rep < 2; rep++ )
                        failures += check_one(
                            scene, &vp, &camera, sizes[si], model_yaw, tex, pos_z, extent);
            }
    }

    /* Models pushed to the viewport edge, so the cull verdict is exercised
     * in both directions rather than always "visible". */
    for( int n = 1; n <= 40; n++ )
    {
        struct ToriDraw_Model* m = make_model(n, 60, 0);
        struct ToriDraw_ModelHandle hnd = ToriDraw_ModelHandleOwned(m);
        for( int dx = -900; dx <= 900; dx += 60 )
        {
            struct ToriDraw_Position position = { .x = dx, .z = 700, .yaw = 0 };
            int cull = ToriDraw_RenderModel1Project(hnd, scene, &position, &vp, &camera);
            if( cull == TORIDRAW_CULL_VISIBLE || cull == TORIDRAW_CULL_AABB )
            {
                int min_x = INT_MAX;
                int max_x = INT_MIN;
                for( int i = 0; i < n; i++ )
                {
                    if( scene->screen_vertices_x[i] < min_x )
                        min_x = scene->screen_vertices_x[i];
                    if( scene->screen_vertices_x[i] > max_x )
                        max_x = scene->screen_vertices_x[i];
                }
                if( scene->aabb.min_screen_x != min_x + vp.x_center - SLOP ||
                    scene->aabb.max_screen_x != max_x + vp.x_center + SLOP )
                {
                    printf("FAIL edge n=%d dx=%d: x box [%d,%d] want [%d,%d]\n", n, dx,
                           scene->aabb.min_screen_x, scene->aabb.max_screen_x,
                           min_x + vp.x_center - SLOP, max_x + vp.x_center + SLOP);
                    failures++;
                }
                g_checked++;
            }
            else
                g_culled++;
        }
        ToriDraw_ModelFree(m);
    }

    printf(
        "projected bound: %d boxes checked (%d whole-plane from a near-clipped vertex, "
        "%d models fast-culled before a box existed) -- %s\n",
        g_checked, g_clipped_boxes, g_culled, failures ? "FAIL" : "PASS");
    if( g_clipped_boxes == 0 )
    {
        printf("  no model ever carried the near-clip sentinel: the clipping family was not "
               "exercised -- FAIL\n");
        failures++;
    }

    ToriDraw_SceneFree(scene);
    return failures ? 1 : 0;
}
