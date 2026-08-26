/* Microbenchmark for the retained-geometry vertex bake.
 *
 * This is the inner loop of d3d9_bake_pose_vertices: for every face of a
 * posed model, bake the three corners into GPU vertices. On the XP box that
 * pipeline -- bake_face + face_colors + world_vertex + the D3D9 vertex write
 * -- is ~2.7 ms of an 18.3 ms frame, so it is worth being able to measure a
 * change here without a round trip to the box.
 *
 * The bench drives the REAL functions, never a copy of them, so a change to
 * the shipping path is what moves this number. Two axes are selectable so the
 * two optimisations can be attributed separately, and so the state the code
 * was in BEFORE each one can still be measured:
 *
 *   colour  argb    pack hsl16 straight to the D3DCOLOR the vertex stores
 *           float   hsl16 -> four normalised floats -> back to a D3DCOLOR
 *   setup   hoist   resolve the placement once per model
 *           corner  resolve it once per corner, as the old world_vertex did
 *
 * What it deliberately leaves out is the texture-map lookup and the atlas UV
 * mapping, both of which live behind a D3D9 device; faces here are untextured,
 * which is the common case for world scenery and the case where the colour
 * path dominates.
 *
 *   make bench-trspk-bake
 *   ./build_win32_opt/trspk_bake_bench <iters> <argb|float> <hoist|corner>
 */
#include "render/trspk_toridraw.h"

#include "core/trspk_vbo.h"
#include "toridraw.h"
#include "toridraw_hsl16.h"
#include "toridraw_math.h"
#include "toridraw_model.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef BENCH_FACES
#define BENCH_FACES 512
#endif
#ifndef BENCH_VERTS
#define BENCH_VERTS 300
#endif
#ifndef BENCH_ITERS
#define BENCH_ITERS 3000
#endif

/* A frame on the XP box bakes on the order of this many faces; the per-face
 * number is the real result and this only turns it into frame-ms. */
#ifndef BENCH_FACES_PER_FRAME
#define BENCH_FACES_PER_FRAME 1600
#endif

static uint32_t g_rng = 0x1234567u;

static uint32_t
bench_rand(void)
{
    g_rng = g_rng * 1664525u + 1013904223u;
    return g_rng >> 8;
}

static struct ToriDraw_Model*
bench_model_new(void)
{
    struct ToriDraw_Model* model = calloc(1, sizeof(*model));
    int i;

    if( !model )
        return NULL;

    model->vertex_count = BENCH_VERTS;
    model->face_count = BENCH_FACES;
    model->vertices_x = calloc(BENCH_VERTS, sizeof(*model->vertices_x));
    model->vertices_y = calloc(BENCH_VERTS, sizeof(*model->vertices_y));
    model->vertices_z = calloc(BENCH_VERTS, sizeof(*model->vertices_z));
    model->face_indices_a = calloc(BENCH_FACES, sizeof(*model->face_indices_a));
    model->face_indices_b = calloc(BENCH_FACES, sizeof(*model->face_indices_b));
    model->face_indices_c = calloc(BENCH_FACES, sizeof(*model->face_indices_c));
    model->face_colors_a = calloc(BENCH_FACES, sizeof(*model->face_colors_a));
    model->face_colors_b = calloc(BENCH_FACES, sizeof(*model->face_colors_b));
    model->face_colors_c = calloc(BENCH_FACES, sizeof(*model->face_colors_c));
    model->face_alphas = calloc(BENCH_FACES, sizeof(*model->face_alphas));
    if( !model->vertices_x || !model->vertices_y || !model->vertices_z ||
        !model->face_indices_a || !model->face_indices_b || !model->face_indices_c ||
        !model->face_colors_a || !model->face_colors_b || !model->face_colors_c ||
        !model->face_alphas )
        return NULL;

    for( i = 0; i < BENCH_VERTS; i++ )
    {
        model->vertices_x[i] = (vertexint_t)((int)(bench_rand() % 512u) - 256);
        model->vertices_y[i] = (vertexint_t)(-(int)(bench_rand() % 400u));
        model->vertices_z[i] = (vertexint_t)((int)(bench_rand() % 512u) - 256);
    }
    for( i = 0; i < BENCH_FACES; i++ )
    {
        /* Neighbouring faces share vertices, the way a real mesh does -- that
         * is what makes the per-corner transform redundant work. */
        int base = (int)(bench_rand() % (uint32_t)(BENCH_VERTS - 3));
        model->face_indices_a[i] = (faceint_t)base;
        model->face_indices_b[i] = (faceint_t)(base + 1);
        model->face_indices_c[i] = (faceint_t)(base + 2);
        model->face_colors_a[i] = (hsl16_t)(bench_rand() % 65536u);
        model->face_colors_b[i] = (hsl16_t)(bench_rand() % 65536u);
        model->face_colors_c[i] = (hsl16_t)(bench_rand() % 65536u);
        /* Mostly opaque, with a minority of blended faces, as a scene is. */
        model->face_alphas[i] = (alphaint_t)((bench_rand() % 8u) ? 0u : 96u);
    }
    return model;
}

static double
bench_now_ms(void)
{
    return (double)clock() * 1000.0 / (double)CLOCKS_PER_SEC;
}

int
main(int argc, char** argv)
{
    struct ToriDraw_Model* model;
    struct ToriDraw_ModelHandle handle;
    struct ToriDraw_Position position;
    struct TRSPK_WorldPlacement placement;
    struct TRSPK_VBO* vbo;
    enum TRSPK_BakeColorForm color_form = TRSPK_BAKE_COLOR_ARGB;
    bool setup_per_corner = false;
    int iters = BENCH_ITERS;
    double t0;
    double elapsed;
    double per_face_ns;
    uint32_t checksum = 0u;
    int iter;

    if( argc > 1 )
        iters = atoi(argv[1]);
    if( argc > 2 && strcmp(argv[2], "float") == 0 )
        color_form = TRSPK_BAKE_COLOR_FLOAT;
    if( argc > 3 && strcmp(argv[3], "corner") == 0 )
        setup_per_corner = true;
    if( iters <= 0 )
        iters = BENCH_ITERS;

    ToriDraw_InitMath();
    ToriDraw_InitHsl16();

    model = bench_model_new();
    if( !model )
    {
        fprintf(stderr, "bench: out of memory\n");
        return 1;
    }

    handle.kind = TORIDRAWMK_MODEL;
    handle.u.model.model = model;
    handle.u.model.ground = NULL;

    /* A placed loc: both angles non-zero, so the rotation path is live. */
    position.x = 3222 * 128;
    position.y = -240;
    position.z = 3218 * 128;
    position.pitch = 341;
    position.yaw = 1517;
    position.roll = 0;

    /* Resolved ONCE, the way the shipping bake loop resolves it. */
    trspk_toridraw_placement_init(&placement, &position);

    vbo = trspk_vbo_create(BENCH_FACES * 3, TRSPK_VERTEX_FORMAT_D3D9);
    if( !vbo )
    {
        fprintf(stderr, "bench: vbo\n");
        return 1;
    }
    trspk_vbo_ensure_capacity(vbo, (uint32_t)BENCH_FACES * 3u);

    /* Warm the caches and the branch predictors before the timed loop. */
    for( iter = 0; iter < 8; iter++ )
    {
        uint32_t face_index;
        for( face_index = 0u; face_index < (uint32_t)BENCH_FACES; face_index++ )
        {
            struct TRSPK_ToriDrawBakeFaceVerts face;
            (void)trspk_toridraw_bake_face_handle(
                handle, face_index, &placement, NULL, true, color_form, &face);
        }
    }

    t0 = bench_now_ms();
    for( iter = 0; iter < iters; iter++ )
    {
        uint32_t face_index;
        for( face_index = 0u; face_index < (uint32_t)BENCH_FACES; face_index++ )
        {
            struct TRSPK_ToriDrawBakeFaceVerts face;
            uint32_t vertex = face_index * 3u;
            float tex_id;

            /* `corner` reproduces what world_vertex used to do: resolve the
             * placement afresh for every corner it transformed. The work is
             * identical, only its position in the loop differs, which is
             * exactly the thing the hoist changed. */
            if( setup_per_corner )
            {
                /* The barriers matter: without them the compiler proves all
                 * three resolve the same loop-invariant value and deletes
                 * them, and the mode measures nothing. */
                trspk_toridraw_placement_init(&placement, &position);
                __asm__ __volatile__("" ::: "memory");
                trspk_toridraw_placement_init(&placement, &position);
                __asm__ __volatile__("" ::: "memory");
                trspk_toridraw_placement_init(&placement, &position);
                __asm__ __volatile__("" ::: "memory");
            }

            if( !trspk_toridraw_bake_face_handle(
                    handle, face_index, &placement, NULL, true, color_form, &face) )
                continue;

            tex_id = (float)face.tex_id;
            if( color_form == TRSPK_BAKE_COLOR_ARGB )
            {
                trspk_vbo_write_vertex_d3d9_argb(
                    vbo, vertex, face.wx_a, face.wy_a, face.wz_a, face.argb_a,
                    face.uv.u1, face.uv.v1, tex_id);
                trspk_vbo_write_vertex_d3d9_argb(
                    vbo, vertex + 1u, face.wx_b, face.wy_b, face.wz_b, face.argb_b,
                    face.uv.u2, face.uv.v2, tex_id);
                trspk_vbo_write_vertex_d3d9_argb(
                    vbo, vertex + 2u, face.wx_c, face.wy_c, face.wz_c, face.argb_c,
                    face.uv.u3, face.uv.v3, tex_id);
            }
            else
            {
                trspk_vbo_write_vertex_d3d9(
                    vbo, vertex, face.wx_a, face.wy_a, face.wz_a, face.color_a,
                    face.uv.u1, face.uv.v1, tex_id);
                trspk_vbo_write_vertex_d3d9(
                    vbo, vertex + 1u, face.wx_b, face.wy_b, face.wz_b, face.color_b,
                    face.uv.u2, face.uv.v2, tex_id);
                trspk_vbo_write_vertex_d3d9(
                    vbo, vertex + 2u, face.wx_c, face.wy_c, face.wz_c, face.color_c,
                    face.uv.u3, face.uv.v3, tex_id);
            }

            /* Every face's result has to be observably live, or the compiler
             * is free to drop all but the last iteration's stores -- each
             * outer pass writes the same vertex slots -- and the bench then
             * measures dead-store elimination instead of the bake. */
            checksum += vbo->vertices.as_d3d9[vertex].color;
        }
        trspk_vbo_set_dirty(vbo);
    }
    elapsed = bench_now_ms() - t0;

    per_face_ns = elapsed * 1.0e6 / ((double)iters * (double)BENCH_FACES);

    printf("trspk-bake-bench  colour=%s setup=%s\n",
        color_form == TRSPK_BAKE_COLOR_ARGB ? "argb" : "float",
        setup_per_corner ? "corner" : "hoist");
    printf("  faces/model      %d\n", BENCH_FACES);
    printf("  iterations       %d\n", iters);
    printf("  total            %.1f ms\n", elapsed);
    printf("  per face         %.1f ns\n", per_face_ns);
    printf("  per %d faces   %.3f ms   <- frame-equivalent\n",
        BENCH_FACES_PER_FRAME,
        per_face_ns * (double)BENCH_FACES_PER_FRAME / 1.0e6);
    printf("  checksum         0x%08x\n", (unsigned)checksum);

    trspk_vbo_free(vbo);
    return 0;
}
