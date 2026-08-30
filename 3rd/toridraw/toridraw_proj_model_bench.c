/*
 * How much of a projection call is FIXED, and how much is the vertices?
 *
 *   make -C src test-proj-model-bench
 *
 * WHAT THIS DECIDES
 *
 * A projection census (graphics/proj_census.h) puts 59% of model projections
 * at exactly four vertices -- terrain tiles, which are four corners and two
 * triangles. At four vertices ToriDraw_RenderModel1Project's 4-wide loop runs
 * exactly ONCE, so everything around the loop is paid in full and amortised
 * over nothing: the fast cull, the eight-corner bounding box, the near-plane
 * decision, and the prepared kernel's fifteen-odd constant constructions.
 *
 * The proposal on the table is a frame-start arena: walk the painter's command
 * list once, batch every terrain tile's vertices, and project them all in one
 * call so that fixed cost is paid once per FRAME instead of once per tile.
 * That is a large change -- an arena, a prepass, a new batched kernel, and a
 * draw path that reads projections instead of computing them -- and its entire
 * value is the size of the fixed cost. So measure the fixed cost first.
 *
 * HOW
 *
 * Time ToriDraw_RenderModel1Project over models of several vertex counts, all
 * else equal, and fit cost(n) = fixed + n * per_vertex across the range. The
 * fixed term is what an arena can delete per tile; per_vertex is what it
 * cannot. The ratio at n = 4 is the answer:
 *
 *     fixed / cost(4)   ==   the fraction of a tile's projection an arena
 *                            could in principle remove
 *
 * "In principle" because a batched kernel still pays the per-tile translate
 * (four imuls and a few splats -- the tile's own world position) inside the
 * loop. So the number below is a CEILING, and the real change would land under
 * it. If the ceiling is small, the arena is not worth building.
 *
 * WHY MANY MODELS AND NOT ONE REPEATED
 *
 * Same reason the face-sort bench gives: one model looped hot lets the branch
 * predictor and the cache learn a single answer that no real frame repeats.
 * The models here differ in their vertices, and the walk is over a ring of
 * them.
 *
 * The camera is PREPARED (ToriDraw_ScenePrepareProjectionCamera) with the same
 * pointer the projection is handed, because that is what production does -- the
 * prepared gate is a pointer comparison, and a bench that failed it would be
 * timing a path the client never takes.
 */

#include "toridraw.h"
#include "toridraw_model.h"
#include "toridraw_scene.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define VIEW_W 512
#define VIEW_H 334

/* Enough distinct models that a ring walk does not sit in one cache line's
 * worth of geometry, few enough that they all stay resident. */
#define MODELS 256

static uint32_t g_rng = 0x9E3779B9u;

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

static double
now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec * 1e-3;
}

/*
 * A model shaped like the thing being measured: `extent` around the origin so
 * the bounds cylinder is honest, and a face list only because
 * ToriDraw_ModelSetBoundsCylinder and the handle machinery want one. Faces are
 * never sorted or drawn here.
 */
static struct ToriDraw_Model*
make_model(int vertex_count, int extent)
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
    }
    ToriDraw_ModelSetBoundsCylinder(m);
    return m;
}

int
main(void)
{
    /* 4 is the terrain tile. The rest span the range the fit needs; 380 is the
     * largest bucket the projection census records. */
    static const int counts[] = { 4, 8, 16, 32, 64, 128, 380 };
    size_t const count_n = sizeof(counts) / sizeof(counts[0]);
    double per_call[sizeof(counts) / sizeof(counts[0])];

    struct ToriDraw_Scene* scene;
    struct ToriDraw_ViewPort viewport = {
        .width = VIEW_W,
        .height = VIEW_H,
        .stride = VIEW_W,
        .x_center = VIEW_W / 2,
        .y_center = VIEW_H / 2,
        .clip_right = VIEW_W,
        .clip_bottom = VIEW_H,
    };
    struct ToriDraw_Camera camera = {
        .proj_mode = TORIDRAW_PROJ_MODE_SCALE,
        .proj_scale = TORIDRAW_PROJ_SCALE_DEFAULT,
        .near_plane_z = 50,
        .pitch = 128,
        /* Camera yaw rotates the world about the origin, so a non-zero one
         * swings a model parked on the +z axis out of the frustum and every
         * row reads "culled". Zero here, and the model yaw is zero too --
         * which is the terrain tile's case anyway. */
        .yaw = 0,
    };
    struct ToriDraw_Position position = { .z = 900, .yaw = 0 };

    ToriDraw_Init();
    scene = ToriDraw_SceneNew(TORIDRAW_SCENE_SMALL, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    assert(scene);
    /* The same pointer the projection is handed: the prepared gate is a
     * pointer comparison and this bench is worthless if it fails. */
    ToriDraw_ScenePrepareProjectionCamera(scene, &camera);

    printf("projection cost by vertex count -- %d models per size, ring walk\n", MODELS);
    printf("%8s %14s %14s\n", "verts", "us/call", "ns/vertex");
    printf("------------------------------------------\n");

    for( size_t ci = 0; ci < count_n; ci++ )
    {
        int const vc = counts[ci];
        struct ToriDraw_Model* models[MODELS];
        struct ToriDraw_ModelHandle hnds[MODELS];
        int const reps = 2000000 / (vc + 8);
        double best = 1e30;
        int m;
        int visible;

        for( m = 0; m < MODELS; m++ )
        {
            models[m] = make_model(vc, 120);
            hnds[m] = ToriDraw_ModelHandleOwned(models[m]);
        }

        visible = ToriDraw_RenderModel1Project(hnds[0], scene, &position, &viewport, &camera);
        if( visible != TORIDRAW_CULL_VISIBLE )
        {
            printf("%8d  culled (%d) -- not measured\n", vc, visible);
            for( m = 0; m < MODELS; m++ )
                ToriDraw_ModelFree(models[m]);
            per_call[ci] = -1.0;
            continue;
        }

        for( int batch = 0; batch < 5; batch++ )
        {
            double t0;
            for( m = 0; m < MODELS; m++ ) /* warm */
                ToriDraw_RenderModel1Project(hnds[m], scene, &position, &viewport, &camera);
            t0 = now_us();
            for( int r = 0; r < reps; r++ )
                ToriDraw_RenderModel1Project(
                    hnds[r % MODELS], scene, &position, &viewport, &camera);
            t0 = ( now_us() - t0 ) / reps;
            if( t0 < best )
                best = t0;
        }

        per_call[ci] = best;
        printf("%8d %14.4f %14.2f\n", vc, best, best * 1000.0 / vc);
        for( m = 0; m < MODELS; m++ )
            ToriDraw_ModelFree(models[m]);
    }

    /*
     * Least squares over the measured points: cost(n) = fixed + n * slope.
     * The fit is over the whole range rather than a two-point difference so a
     * single noisy row cannot decide the answer.
     */
    {
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        int n = 0;
        for( size_t ci = 0; ci < count_n; ci++ )
        {
            if( per_call[ci] < 0 )
                continue;
            sx += counts[ci];
            sy += per_call[ci];
            sxx += (double)counts[ci] * counts[ci];
            sxy += (double)counts[ci] * per_call[ci];
            n++;
        }
        if( n >= 2 )
        {
            double const denom = n * sxx - sx * sx;
            double const slope = ( n * sxy - sx * sy ) / denom;
            double const fixed = ( sy - slope * sx ) / n;
            printf("\n  fit: cost(n) = %.4f us + n * %.5f us\n", fixed, slope);
            if( per_call[0] > 0 )
            {
                printf("  at 4 vertices: fixed is %.1f%% of the call\n",
                       100.0 * fixed / per_call[0]);
                printf("\n  CEILING for a frame-start terrain arena: it can delete the\n");
                printf("  fixed term per tile and nothing else, so no more than %.1f%% of\n",
                       100.0 * fixed / per_call[0]);
                printf("  what a tile's projection costs today. A batched kernel still\n");
                printf("  pays the per-tile translate inside the loop, so the real change\n");
                printf("  lands under this.\n");
            }
        }
    }

    ToriDraw_SceneFree(scene);
    return 0;
}
