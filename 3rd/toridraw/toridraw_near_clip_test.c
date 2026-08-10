/*
 * Client-TS parity regression for faces crossing the near plane.
 *
 * Model.render2 depth-buckets a face containing the -5000 projection sentinel
 * without consulting its meaningless pre-clip winding. Model.render3ZClip then
 * clips the polygon and culls using the first three generated screen points.
 */

#include "toridraw.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIEW_W 512
#define VIEW_H 334
#define GUARD_PIXELS 64
#define CLEAR_PIXEL ((toripixel_t)0x0013579B)
#define GUARD_PIXEL ((toripixel_t)0x00654321)

static int failures;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            failures++;                                                                            \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                  \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fputc('\n', stderr);                                                                   \
        }                                                                                          \
    } while( 0 )

struct Fixture
{
    struct ToriDraw_Model model;
    struct ToriDraw_ModelHandle hnd;
    struct ToriDraw_BoundsCylinder bounds;
    vertexint_t model_x[3];
    vertexint_t model_y[3];
    vertexint_t model_z[3];
    faceint_t face_a[1];
    faceint_t face_b[1];
    faceint_t face_c[1];
    hsl16_t color_a[1];
    hsl16_t color_b[1];
    hsl16_t color_c[1];
};

static void
fixture_init(struct Fixture* fx, int reverse)
{
    memset(fx, 0, sizeof(*fx));

    fx->model.vertex_count = 3;
    fx->model.face_count = 1;
    fx->model.vertices_x = fx->model_x;
    fx->model.vertices_y = fx->model_y;
    fx->model.vertices_z = fx->model_z;
    fx->model.face_indices_a = fx->face_a;
    fx->model.face_indices_b = fx->face_b;
    fx->model.face_indices_c = fx->face_c;
    fx->model.face_colors_a = fx->color_a;
    fx->model.face_colors_b = fx->color_b;
    fx->model.face_colors_c = fx->color_c;
    fx->model.bounds_cylinder = &fx->bounds;

    /* This face is untextured. A nonzero count makes the public raster context
     * retain camera-space vertices, as a real mixed textured model does, so its
     * untextured face can take the near-clip builder. */
    fx->model.textured_face_count = 1;

    fx->face_a[0] = 0;
    fx->face_b[0] = (faceint_t)(reverse ? 2 : 1);
    fx->face_c[0] = (faceint_t)(reverse ? 1 : 2);
    fx->color_a[0] = 30000;
    fx->color_b[0] = 30000;
    fx->color_c[0] = 30000;

    fx->hnd.kind = TORIDRAWMK_MODEL;
    fx->hnd.u.model.model = &fx->model;
}

static void
seed_projected_triangle(struct ToriDraw_Scene* scene, const struct Fixture* fx)
{
    (void)fx;

    /* Camera-space triangle, near plane z=50:
     *   A=(29, 2,37) is behind, B=(23,-4,52), C=(-15,28,82) are visible.
     * The clip builder produces a CCW polygon for A,B,C and a CW polygon when
     * B/C are reversed. */
    scene->orthographic_vertices_x[0] = 29;
    scene->orthographic_vertices_y[0] = 2;
    scene->orthographic_vertices_z[0] = 37;
    scene->orthographic_vertices_x[1] = 23;
    scene->orthographic_vertices_y[1] = -4;
    scene->orthographic_vertices_z[1] = 52;
    scene->orthographic_vertices_x[2] = -15;
    scene->orthographic_vertices_y[2] = 28;
    scene->orthographic_vertices_z[2] = 82;

    /* Exact integer projection scratch produced at scale 512. The clipped
     * vertex's y remains undivided. These values give the bogus pre-clip cross
     * product -774041 even though the clipped A,B,C polygon is front-facing. */
    scene->screen_vertices_x[0] = TORIDRAW_SCREEN_X_NEAR_CLIPPED;
    scene->screen_vertices_y[0] = 1024;
    scene->screen_vertices_z[0] = 37;
    scene->screen_vertices_x[1] = 226;
    scene->screen_vertices_y[1] = -39;
    scene->screen_vertices_z[1] = 52;
    scene->screen_vertices_x[2] = -93;
    scene->screen_vertices_y[2] = 174;
    scene->screen_vertices_z[2] = 82;

    scene->near_clipped = true;
    /* Projection output state, like near_clipped above: the raster context
     * reads the plane the scratch was actually clipped against rather than the
     * camera's, so a hand-seeded projection has to state it. */
    scene->projection_near_plane_z = 50;
}

static struct ToriDraw_Scene*
new_seeded_scene(uint32_t flags, struct Fixture* fx, int reverse)
{
    struct ToriDraw_Scene* scene =
        ToriDraw_SceneNew(flags, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    CHECK(scene != NULL, "scene allocation, flags=%u", flags);
    if( !scene )
        return NULL;

    fixture_init(fx, reverse);
    seed_projected_triangle(scene, fx);
    scene->active_hnd = fx->hnd;
    return scene;
}

static void
test_sorter_keeps_sentinel_face(uint32_t flags)
{
    struct Fixture fx;
    struct ToriDraw_Scene* scene = new_seeded_scene(flags, &fx, 0);
    if( !scene )
        return;

    int count = ToriDraw_RenderModel2SortFaces(fx.hnd, scene);
    CHECK(count == 1, "sentinel face must bypass pre-clip winding, flags=%u count=%d", flags, count);
    if( count == 1 )
        CHECK(ToriDraw_FaceOrder(scene)[0] == 0, "sorted face must be face zero, flags=%u", flags);

    ToriDraw_SceneFree(scene);
}

static int
render_lit_pixels(int reverse)
{
    struct Fixture fx;
    struct ToriDraw_Scene* scene = new_seeded_scene(TORIDRAW_SCENE_FULL, &fx, reverse);
    struct ToriDraw_ViewPort viewport = {
        .width = VIEW_W,
        .height = VIEW_H,
        .stride = VIEW_W,
        .x_center = VIEW_W / 2,
        .y_center = VIEW_H / 2,
        .clip_left = 0,
        .clip_top = 0,
        .clip_right = VIEW_W,
        .clip_bottom = VIEW_H,
    };
    struct ToriDraw_Camera camera = {
        .proj_mode = TORIDRAW_PROJ_MODE_SCALE,
        .proj_scale = 512,
        .near_plane_z = 50,
    };
    size_t const view_pixels = (size_t)VIEW_W * VIEW_H;
    size_t const all_pixels = view_pixels + 2 * GUARD_PIXELS;
    toripixel_t* storage = malloc(all_pixels * sizeof(*storage));
    int lit = 0;

    if( !scene || !storage )
    {
        CHECK(storage != NULL, "pixel allocation");
        free(storage);
        ToriDraw_SceneFree(scene);
        return 0;
    }

    for( size_t i = 0; i < all_pixels; i++ )
        storage[i] = GUARD_PIXEL;
    for( size_t i = 0; i < view_pixels; i++ )
        storage[GUARD_PIXELS + i] = CLEAR_PIXEL;

    int sorted = ToriDraw_RenderModel2SortFaces(fx.hnd, scene);
    CHECK(sorted == 1, "both clipped orientations reach post-clip cull, reverse=%d sorted=%d", reverse, sorted);
    ToriDraw_RenderModel3Raster(
        scene, &viewport, &camera, storage + GUARD_PIXELS, false);

    for( size_t i = 0; i < view_pixels; i++ )
        if( storage[GUARD_PIXELS + i] != CLEAR_PIXEL )
            lit++;
    for( int i = 0; i < GUARD_PIXELS; i++ )
    {
        CHECK(storage[i] == GUARD_PIXEL, "prefix guard overwritten at %d, reverse=%d", i, reverse);
        CHECK(
            storage[GUARD_PIXELS + view_pixels + i] == GUARD_PIXEL,
            "suffix guard overwritten at %d, reverse=%d",
            i,
            reverse);
    }

    free(storage);
    ToriDraw_SceneFree(scene);
    return lit;
}

static void
test_post_clip_winding(void)
{
    int front_lit = render_lit_pixels(0);
    int back_lit = render_lit_pixels(1);

    CHECK(front_lit > 0, "front-facing clipped polygon must rasterize (lit=%d)", front_lit);
    CHECK(back_lit == 0, "back-facing clipped polygon must be culled after clipping (lit=%d)", back_lit);
}

/*
 * Vertices the clipper creates must be projected at the camera's scale, like
 * the ones the projection kernels keep.
 *
 * Everything above runs at scale 512, which is the single value at which a
 * clip vertex projected through the old `SCALE_UNIT(p) / z` was right by
 * accident. The live world camera derives its scale from the viewport height,
 * so this drives the same geometry at two scales and requires the drawn shape
 * to scale with them. Under the bug the kept vertices halve and the clipped
 * ones do not, which widens the polygon instead of shrinking it.
 */
#define SCALED_NEAR_Z 50

/* Camera space. A is behind the near plane; B and C straddle it far enough
 * that both clip intersections are interior to their edges. Chosen so the
 * whole polygon stays inside the viewport at both scales below — a shape the
 * screen clamps would report the viewport's size, not the projection's. */
static const int g_scaled_ortho_x[3] = { 10, 20, -20 };
static const int g_scaled_ortho_y[3] = { 5, -10, 15 };
static const int g_scaled_ortho_z[3] = { 40, 60, 100 };

static void
seed_scaled_triangle(struct ToriDraw_Scene* scene, int scale)
{
    for( int i = 0; i < 3; i++ )
    {
        scene->orthographic_vertices_x[i] = g_scaled_ortho_x[i];
        scene->orthographic_vertices_y[i] = g_scaled_ortho_y[i];
        scene->orthographic_vertices_z[i] = g_scaled_ortho_z[i];
        scene->screen_vertices_z[i] = g_scaled_ortho_z[i];

        if( g_scaled_ortho_z[i] < SCALED_NEAR_Z )
        {
            /* What the clipping projection kernels park for a behind-the-eye
             * vertex; y is left undivided there, so nothing may read it. */
            scene->screen_vertices_x[i] = TORIDRAW_SCREEN_X_NEAR_CLIPPED;
            scene->screen_vertices_y[i] = TORIDRAW_SCREEN_X_NEAR_CLIPPED;
            continue;
        }
        scene->screen_vertices_x[i] = g_scaled_ortho_x[i] * scale / g_scaled_ortho_z[i];
        scene->screen_vertices_y[i] = g_scaled_ortho_y[i] * scale / g_scaled_ortho_z[i];
    }

    scene->near_clipped = true;
    scene->projection_near_plane_z = SCALED_NEAR_Z;
}

/** Width of the drawn polygon's bounding box, or -1 if nothing was drawn. */
static int
render_scaled_width(int scale)
{
    struct Fixture fx;
    struct ToriDraw_Scene* scene = ToriDraw_SceneNew(TORIDRAW_SCENE_FULL, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    struct ToriDraw_ViewPort viewport = {
        .width = VIEW_W,
        .height = VIEW_H,
        .stride = VIEW_W,
        .x_center = VIEW_W / 2,
        .y_center = VIEW_H / 2,
        .clip_left = 0,
        .clip_top = 0,
        .clip_right = VIEW_W,
        .clip_bottom = VIEW_H,
    };
    struct ToriDraw_Camera camera = {
        .proj_mode = TORIDRAW_PROJ_MODE_SCALE,
        .proj_scale = scale,
        .near_plane_z = SCALED_NEAR_Z,
    };
    size_t const view_pixels = (size_t)VIEW_W * VIEW_H;
    toripixel_t* pixels = malloc(view_pixels * sizeof(*pixels));
    int min_x = VIEW_W;
    int max_x = -1;

    CHECK(scene != NULL, "scene allocation, scale=%d", scale);
    CHECK(pixels != NULL, "pixel allocation, scale=%d", scale);
    if( !scene || !pixels )
    {
        free(pixels);
        ToriDraw_SceneFree(scene);
        return -1;
    }

    fixture_init(&fx, 0);
    seed_scaled_triangle(scene, scale);
    scene->active_hnd = fx.hnd;

    for( size_t i = 0; i < view_pixels; i++ )
        pixels[i] = CLEAR_PIXEL;

    CHECK(
        ToriDraw_RenderModel2SortFaces(fx.hnd, scene) == 1,
        "clipped face must survive the sort, scale=%d",
        scale);
    ToriDraw_RenderModel3Raster(scene, &viewport, &camera, pixels, false);

    for( int y = 0; y < VIEW_H; y++ )
        for( int x = 0; x < VIEW_W; x++ )
            if( pixels[(size_t)y * VIEW_W + x] != CLEAR_PIXEL )
            {
                if( x < min_x )
                    min_x = x;
                if( x > max_x )
                    max_x = x;
            }

    free(pixels);
    ToriDraw_SceneFree(scene);
    return max_x < 0 ? -1 : max_x - min_x + 1;
}

static void
test_clip_vertices_follow_camera_scale(void)
{
    int wide = render_scaled_width(512);
    int half = render_scaled_width(256);

    CHECK(wide > 0, "clipped polygon must rasterize at scale 512 (width=%d)", wide);
    CHECK(half > 0, "clipped polygon must rasterize at scale 256 (width=%d)", half);
    if( wide <= 0 || half <= 0 )
        return;

    /* Halving the scale halves the projection; a couple of pixels of slack
     * covers the integer divides. The old code left the two clip vertices at
     * scale 512 regardless, which reports ~204 against an expected ~136. */
    CHECK(
        half * 2 >= wide - 3 && half * 2 <= wide + 3,
        "clip vertices must scale with the camera: width 512=%d, 256=%d (expected ~%d)",
        wide,
        half,
        wide / 2);
}

int
main(void)
{
    ToriDraw_Init();

    test_sorter_keeps_sentinel_face(TORIDRAW_SCENE_FULL);
    test_sorter_keeps_sentinel_face(TORIDRAW_SCENE_SMALL);
    test_post_clip_winding();
    test_clip_vertices_follow_camera_scale();

    if( failures )
        return 1;
    puts("toridraw near-clip parity tests: PASS");
    return 0;
}

