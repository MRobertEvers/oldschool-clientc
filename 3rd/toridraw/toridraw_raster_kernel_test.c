/*
 * Public raster-kernel routing regression.
 *
 * This is deliberately a black-box test: it constructs one ordinary model,
 * binds public kernels to public scenes, and drives the three public model
 * phases.  No private resolver or raster context is included here.
 *
 * Build and run:
 *   make -C src test-raster-kernel
 */

#include "toridraw.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIEW_WIDTH 320
#define VIEW_HEIGHT 200
#define VIEW_STRIDE 336
#define CLIP_LEFT 4
#define CLIP_TOP 6
#define CLIP_RIGHT 316
#define CLIP_BOTTOM 194
#define CAMERA_DISTANCE 600
#define TEST_TEXTURE_ID 7
#define TEST_TEXTURE_WIDTH 64
#define FACE_COUNT 4
#define VERTEX_COUNT (FACE_COUNT * 3)

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
    struct ToriDraw_ModelHandle handle;
    vertexint_t vertex_x[VERTEX_COUNT];
    vertexint_t vertex_y[VERTEX_COUNT];
    vertexint_t vertex_z[VERTEX_COUNT];
    faceint_t face_a[FACE_COUNT];
    faceint_t face_b[FACE_COUNT];
    faceint_t face_c[FACE_COUNT];
    hsl16_t shade_a[FACE_COUNT];
    hsl16_t shade_b[FACE_COUNT];
    hsl16_t shade_c[FACE_COUNT];
    alphaint_t alpha[FACE_COUNT];
    int face_info[FACE_COUNT];
    faceint_t texture[FACE_COUNT];
};

struct RenderEnv
{
    struct ToriDraw_Scene* scene;
    toripixel_t* pixels;
    const int* texture_texels;
};

struct Spy
{
    const char* name;
    const struct Fixture* fixture;
    const struct RenderEnv* env;
    int calls[TORIDRAW_RASTER_FACE_CLASS_COUNT];
    int face_calls[FACE_COUNT];
    int total_calls;
    int noop_calls;

    bool attempt_mutation;
    bool mutation_attempted;
    bool set_during_callback;
    bool reset_during_callback;
    const struct ToriDraw_RasterKernel* replacement;

    bool attempt_recursive_render;
    bool recursive_render_attempted;
    bool recursive_scratch_unchanged;
    bool recursive_contexts_distinct;
    bool set_after_nested;
    bool reset_after_nested;
    int recursive_depth;
    int recursive_depth_limit;
    bool recursive_started[3];
    const int* screen_x_by_depth[3];
    const int* face_order_by_depth[3];
};

static void
fixture_init(struct Fixture* fixture)
{
    static const int centre_x[FACE_COUNT] = { -120, -40, 40, 120 };

    memset(fixture, 0, sizeof(*fixture));
    fixture->model.vertex_count = VERTEX_COUNT;
    fixture->model.face_count = FACE_COUNT;
    fixture->model.vertices_x = fixture->vertex_x;
    fixture->model.vertices_y = fixture->vertex_y;
    fixture->model.vertices_z = fixture->vertex_z;
    fixture->model.face_indices_a = fixture->face_a;
    fixture->model.face_indices_b = fixture->face_b;
    fixture->model.face_indices_c = fixture->face_c;
    fixture->model.face_colors_a = fixture->shade_a;
    fixture->model.face_colors_b = fixture->shade_b;
    fixture->model.face_colors_c = fixture->shade_c;
    fixture->model.face_alphas = fixture->alpha;
    fixture->model.face_infos = fixture->face_info;
    fixture->model.face_textures = fixture->texture;

    /* One record is enough to retain orthographic projection scratch.  These
     * faces deliberately have no face_texture_coords array, so stock texture
     * preparation publishes its safe A/B/C fallback frame. */
    fixture->model.textured_face_count = 1;

    for( int face = 0; face < FACE_COUNT; face++ )
    {
        int const vertex = face * 3;
        int const x = centre_x[face];

        fixture->vertex_x[vertex + 0] = (vertexint_t)(x - 22);
        fixture->vertex_y[vertex + 0] = -28;
        fixture->vertex_z[vertex + 0] = 0;
        fixture->vertex_x[vertex + 1] = (vertexint_t)(x + 22);
        fixture->vertex_y[vertex + 1] = -28;
        fixture->vertex_z[vertex + 1] = 0;
        fixture->vertex_x[vertex + 2] = (vertexint_t)x;
        fixture->vertex_y[vertex + 2] = 28;
        fixture->vertex_z[vertex + 2] = 0;

        /* The same front-facing convention used by the HD routing fixture. */
        fixture->face_a[face] = (faceint_t)(vertex + 0);
        fixture->face_b[face] = (faceint_t)(vertex + 2);
        fixture->face_c[face] = (faceint_t)(vertex + 1);
        fixture->face_info[face] = 0;
        fixture->texture[face] = -1;
    }

    fixture->shade_a[0] = 0x1234;
    fixture->shade_b[0] = 0x1235;
    fixture->shade_c[0] = 0x1236;
    fixture->alpha[0] = 17; /* effective opacity 238 */

    fixture->shade_a[1] = 0x2345;
    fixture->shade_b[1] = 0x3456;
    fixture->shade_c[1] = TORIDRAWHSL16_FLAT;
    fixture->alpha[1] = 0;

    fixture->shade_a[2] = 40;
    fixture->shade_b[2] = 60;
    fixture->shade_c[2] = 80;
    fixture->alpha[2] = 200; /* ignored by stock textured policy */
    fixture->texture[2] = TEST_TEXTURE_ID;

    fixture->shade_a[3] = 70;
    fixture->shade_b[3] = 90;
    fixture->shade_c[3] = TORIDRAWHSL16_FLAT;
    fixture->alpha[3] = 254; /* ignored by stock textured policy */
    fixture->texture[3] = TEST_TEXTURE_ID;

    fixture->handle.kind = TORIDRAWMK_MODEL;
    fixture->handle.u.model.model = &fixture->model;
    ToriDraw_ModelSetBoundsCylinder(&fixture->model);
}

static void
fixture_destroy(struct Fixture* fixture)
{
    free(fixture->model.bounds_cylinder);
    fixture->model.bounds_cylinder = NULL;
}

static struct ToriDraw_Texture*
make_test_texture(int rgb)
{
    struct ToriDraw_Texture* texture = calloc(1, sizeof(*texture));

    if( !texture )
        return NULL;
    texture->texels = malloc(
        (size_t)TEST_TEXTURE_WIDTH * TEST_TEXTURE_WIDTH * sizeof(*texture->texels));
    if( !texture->texels )
    {
        free(texture);
        return NULL;
    }
    for( int i = 0; i < TEST_TEXTURE_WIDTH * TEST_TEXTURE_WIDTH; i++ )
        texture->texels[i] = rgb;
    texture->width = TEST_TEXTURE_WIDTH;
    texture->height = TEST_TEXTURE_WIDTH;
    /* Exercise normalized colour-key routing rather than the opaque default. */
    texture->opaque = false;
    return texture;
}

static bool
render_env_init(struct RenderEnv* env, int texture_rgb)
{
    struct ToriDraw_Texture* texture;

    memset(env, 0, sizeof(*env));
    env->scene = ToriDraw_SceneNew(TORIDRAW_SCENE_FULL, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    env->pixels = calloc((size_t)VIEW_STRIDE * VIEW_HEIGHT, sizeof(*env->pixels));
    texture = make_test_texture(texture_rgb);
    CHECK(env->scene != NULL, "scene allocation");
    CHECK(env->pixels != NULL, "framebuffer allocation");
    CHECK(texture != NULL, "texture allocation");
    if( !env->scene || !env->pixels || !texture )
    {
        if( texture )
            ToriDraw_TextureFree(texture);
        ToriDraw_SceneFree(env->scene);
        free(env->pixels);
        memset(env, 0, sizeof(*env));
        return false;
    }

    env->texture_texels = texture->texels;
    ToriDraw_SceneSetTexture(env->scene, TEST_TEXTURE_ID, texture);
    return true;
}

static void
render_env_destroy(struct RenderEnv* env)
{
    ToriDraw_SceneFree(env->scene);
    free(env->pixels);
    memset(env, 0, sizeof(*env));
}

static struct ToriDraw_ViewPort
test_viewport(void)
{
    struct ToriDraw_ViewPort viewport = {
        .width = VIEW_WIDTH,
        .height = VIEW_HEIGHT,
        .stride = VIEW_STRIDE,
        .x_center = VIEW_WIDTH / 2,
        .y_center = VIEW_HEIGHT / 2,
        .clip_left = CLIP_LEFT,
        .clip_top = CLIP_TOP,
        .clip_right = CLIP_RIGHT,
        .clip_bottom = CLIP_BOTTOM,
    };
    return viewport;
}

static struct ToriDraw_Camera
test_camera(void)
{
    struct ToriDraw_Camera camera = {
        .proj_mode = TORIDRAW_PROJ_MODE_SCALE,
        .proj_scale = TORIDRAW_PROJ_SCALE_DEFAULT,
        .near_plane_z = 50,
        .texture_affine = true,
    };
    return camera;
}

static bool
render_fixture_with_ordered_count(
    struct RenderEnv* env,
    const struct Fixture* fixture,
    int expected_ordered)
{
    struct ToriDraw_ViewPort viewport = test_viewport();
    struct ToriDraw_Camera camera = test_camera();
    struct ToriDraw_Position position = { .z = CAMERA_DISTANCE };
    int cull;
    int ordered;

    memset(env->pixels, 0, (size_t)VIEW_STRIDE * VIEW_HEIGHT * sizeof(*env->pixels));
    cull = ToriDraw_RenderModel1Project(
        fixture->handle, env->scene, &position, &viewport, &camera);
    CHECK(cull == TORIDRAW_CULL_VISIBLE, "fixture projection returned %d", cull);
    if( cull != TORIDRAW_CULL_VISIBLE )
        return false;

    ordered = ToriDraw_RenderModel2SortFaces(fixture->handle, env->scene);
    CHECK(ordered == expected_ordered, "fixture ordered %d/%d expected faces", ordered,
          expected_ordered);
    if( ordered != expected_ordered )
        return false;

    ToriDraw_RenderModel3Raster(env->scene, &viewport, &camera, env->pixels, false);
    return true;
}

static bool
render_fixture(struct RenderEnv* env, const struct Fixture* fixture)
{
    return render_fixture_with_ordered_count(env, fixture, FACE_COUNT);
}

static long
count_nonzero_pixels(const struct RenderEnv* env)
{
    long count = 0;

    for( int i = 0; i < VIEW_STRIDE * VIEW_HEIGHT; i++ )
        count += env->pixels[i] != 0;
    return count;
}

static void
spy_init(
    struct Spy* spy,
    const char* name,
    const struct Fixture* fixture,
    const struct RenderEnv* env)
{
    memset(spy, 0, sizeof(*spy));
    spy->name = name;
    spy->fixture = fixture;
    spy->env = env;
    spy->recursive_scratch_unchanged = true;
    spy->recursive_contexts_distinct = true;
}

static void
verify_target(const struct Spy* spy, const struct ToriDraw_RasterTarget* target)
{
    const struct Fixture* fixture = spy->fixture;
    const struct RenderEnv* env = spy->env;

    CHECK(target != NULL, "%s received a null target", spy->name);
    if( !target )
        return;
    CHECK(target->domain == TORIDRAW_RASTER_KERNEL_STOCK, "%s target domain %d", spy->name,
          (int)target->domain);
    CHECK(target->pixel_buffer == env->pixels + CLIP_LEFT + CLIP_TOP * VIEW_STRIDE,
          "%s framebuffer was not clip-rebased", spy->name);
    CHECK(target->zbuffer == NULL && !target->depth_test, "%s unexpectedly depth-tested", spy->name);
    CHECK(target->width == CLIP_RIGHT - CLIP_LEFT, "%s target width %d", spy->name, target->width);
    CHECK(target->height == CLIP_BOTTOM - CLIP_TOP, "%s target height %d", spy->name,
          target->height);
    CHECK(target->stride == VIEW_STRIDE, "%s target stride %d", spy->name, target->stride);
    CHECK(target->clip_origin_x == CLIP_LEFT && target->clip_origin_y == CLIP_TOP,
          "%s clip origin (%d,%d)", spy->name, target->clip_origin_x, target->clip_origin_y);
    CHECK(target->projection_center_x == (CLIP_RIGHT - CLIP_LEFT) / 2 &&
              target->projection_center_y == (CLIP_BOTTOM - CLIP_TOP) / 2,
          "%s projection center (%d,%d)", spy->name, target->projection_center_x,
          target->projection_center_y);
    CHECK(target->near_plane_z == env->scene->projection_near_plane_z,
          "%s near plane %d", spy->name, target->near_plane_z);
    CHECK(target->camera_cot16 != 0, "%s camera cotangent was zero", spy->name);
    CHECK(target->model_mid_z == env->scene->projected_vertex.z, "%s model mid-z %d", spy->name,
          target->model_mid_z);
    CHECK(!target->parallel_projection, "%s unexpectedly parallel", spy->name);
    CHECK(!target->smooth_shading, "%s unexpectedly smooth", spy->name);
    CHECK(target->affine_textures, "%s lost the affine camera flag", spy->name);
    CHECK(target->near_clip_available, "%s lost orthographic near-clip data", spy->name);
    CHECK(target->vertex_count == VERTEX_COUNT, "%s vertex count %d", spy->name,
          target->vertex_count);
    CHECK(target->screen_vertices_x == env->scene->screen_vertices_x &&
              target->screen_vertices_y == env->scene->screen_vertices_y &&
              target->screen_vertices_z == env->scene->screen_vertices_z,
          "%s projected arrays differ from scene scratch", spy->name);
    CHECK(target->orthographic_vertices_x == env->scene->orthographic_vertices_x &&
              target->orthographic_vertices_y == env->scene->orthographic_vertices_y &&
              target->orthographic_vertices_z == env->scene->orthographic_vertices_z,
          "%s orthographic arrays differ from scene scratch", spy->name);
    CHECK(target->posed_vertices_x == fixture->vertex_x &&
              target->posed_vertices_y == fixture->vertex_y &&
              target->posed_vertices_z == fixture->vertex_z,
          "%s posed arrays differ from the model", spy->name);
    CHECK(target->bind_vertices_x == fixture->vertex_x &&
              target->bind_vertices_y == fixture->vertex_y &&
              target->bind_vertices_z == fixture->vertex_z,
          "%s bind-pose fallback arrays differ from the model", spy->name);
}

static enum ToriDraw_RasterFaceClass
expected_class(int face_index)
{
    static const enum ToriDraw_RasterFaceClass classes[FACE_COUNT] = {
        TORIDRAW_RASTER_FACE_GOURAUD,
        TORIDRAW_RASTER_FACE_FLAT,
#ifdef TORIDRAW_PIXEL16
        TORIDRAW_RASTER_FACE_GOURAUD,
        TORIDRAW_RASTER_FACE_FLAT,
#else
        TORIDRAW_RASTER_FACE_TEXTURED,
        TORIDRAW_RASTER_FACE_TEXTURED_FLAT,
#endif
    };
    return classes[face_index];
}

static void
verify_face(const struct Spy* spy, const struct ToriDraw_RasterFace* face)
{
    const struct Fixture* fixture = spy->fixture;
    int index;

    CHECK(face != NULL, "%s received a null face", spy->name);
    if( !face )
        return;
    index = face->face_index;
    CHECK(index >= 0 && index < FACE_COUNT, "%s face index %d", spy->name, index);
    if( index < 0 || index >= FACE_COUNT )
        return;

    CHECK(face->face_class == expected_class(index), "%s face %d class %d", spy->name, index,
          (int)face->face_class);
    CHECK(face->vertex[0] == fixture->face_a[index] &&
              face->vertex[1] == fixture->face_b[index] &&
              face->vertex[2] == fixture->face_c[index],
          "%s face %d vertices (%d,%d,%d)", spy->name, index, face->vertex[0], face->vertex[1],
          face->vertex[2]);
    CHECK(!face->near_clipped, "%s face %d unexpectedly near-clipped", spy->name, index);

    if( index == 0 )
    {
        CHECK(face->shade[0] == 0x1234 && face->shade[1] == 0x1235 &&
                  face->shade[2] == 0x1236,
              "%s Gouraud shades (%d,%d,%d)", spy->name, face->shade[0], face->shade[1],
              face->shade[2]);
        CHECK(face->opacity == 238, "%s Gouraud opacity %d", spy->name, face->opacity);
    }
    else if( index == 1 )
    {
        CHECK(face->shade[0] == 0x2345 && face->shade[1] == 0x2345 &&
                  face->shade[2] == 0x2345,
              "%s flat shades were not normalized", spy->name);
        CHECK(face->opacity == 255, "%s flat opacity %d", spy->name, face->opacity);
    }
    else
    {
        int const expected_shade_a = index == 2 ? 40 : 70;
#ifdef TORIDRAW_PIXEL16
        int const expected_shade_b = index == 2 ? 60 : 70;
        int const expected_shade_c = index == 2 ? 80 : 70;

        CHECK(face->shade[0] == expected_shade_a && face->shade[1] == expected_shade_b &&
                  face->shade[2] == expected_shade_c,
              "%s Pixel16-collapsed face %d shades (%d,%d,%d)", spy->name, index,
              face->shade[0], face->shade[1], face->shade[2]);
        CHECK(face->opacity == (index == 2 ? 55 : 1),
              "%s Pixel16-collapsed face %d opacity %d", spy->name, index, face->opacity);
#else
        int const expected_shade_b = index == 2 ? 60 : 70;
        int const expected_shade_c = index == 2 ? 80 : 70;

        CHECK(face->shade[0] == expected_shade_a && face->shade[1] == expected_shade_b &&
                  face->shade[2] == expected_shade_c,
              "%s textured face %d shades (%d,%d,%d)", spy->name, index, face->shade[0],
              face->shade[1], face->shade[2]);
        CHECK(face->opacity == 255, "%s textured face %d did not ignore authored alpha",
              spy->name, index);
        CHECK(face->texture.texture_id == TEST_TEXTURE_ID, "%s texture id %d", spy->name,
              face->texture.texture_id);
        CHECK(face->texture.texels == spy->env->texture_texels, "%s texture pointer mismatch",
              spy->name);
        CHECK(face->texture.width == TEST_TEXTURE_WIDTH &&
                  face->texture.height == TEST_TEXTURE_WIDTH,
              "%s texture dimensions %dx%d", spy->name, face->texture.width,
              face->texture.height);
        CHECK(face->texture.gate == TORIDRAW_RASTER_TEXTURE_COLOR_KEY, "%s texture gate %d",
              spy->name, (int)face->texture.gate);
        CHECK(!face->texture.clamp_s && !face->texture.clamp_t, "%s stock clamp flags set",
              spy->name);
        CHECK(face->texture.render_type == 0, "%s stock render type %u", spy->name,
              face->texture.render_type);
        CHECK(face->texture.mapping_payload == TORIDRAW_RASTER_MAPPING_STOCK_FACE_FALLBACK,
              "%s stock mapping payload %d", spy->name, (int)face->texture.mapping_payload);
        CHECK(face->texture.mapping.vertex_frame.p == face->vertex[0] &&
                  face->texture.mapping.vertex_frame.m == face->vertex[1] &&
                  face->texture.mapping.vertex_frame.n == face->vertex[2],
              "%s stock fallback frame differs from A/B/C", spy->name);
        CHECK(!face->texture.modulate && face->texture.tint_r == 0 &&
                  face->texture.tint_g == 0 && face->texture.tint_b == 0 &&
                  face->texture.texture_neutral == 0,
              "%s stock face carried HD sampler state", spy->name);
#endif
    }
}

static int
fixture_dispatch_count(void);

static void
spy_attempt_same_scene_recursion(struct Spy* spy)
{
    struct ToriDraw_Scene* scene = spy->env->scene;
    struct ToriDraw_ViewPort viewport = test_viewport();
    struct ToriDraw_Camera camera = test_camera();
    struct ToriDraw_Position position = { .x = 90, .y = -35, .z = 925, .yaw = 128 };
    struct ToriDraw_ModelHandle active_hnd = scene->active_hnd;
    struct ProjectedVertex projected_vertex = scene->projected_vertex;
    struct ToriDraw_AABB aabb = scene->aabb;
    struct ToriDraw_AABB cylinder_aabb = scene->cylinder_fast_aabb;
    int screen_x[VERTEX_COUNT];
    int screen_y[VERTEX_COUNT];
    int screen_z[VERTEX_COUNT];
    int face_order[FACE_COUNT];
    int const ordered = scene->tmp_face_order_count;
    int const projection_near_plane_z = scene->projection_near_plane_z;
    bool const near_clipped = scene->near_clipped;
    int* const screen_x_pointer = scene->screen_vertices_x;
    int* const screen_y_pointer = scene->screen_vertices_y;
    int* const screen_z_pointer = scene->screen_vertices_z;
    int* const orthographic_x_pointer = scene->orthographic_vertices_x;
    int* const orthographic_y_pointer = scene->orthographic_vertices_y;
    int* const orthographic_z_pointer = scene->orthographic_vertices_z;
    int* const face_order_pointer = scene->tmp_face_order;

    spy->recursive_render_attempted = true;
    memcpy(screen_x, scene->screen_vertices_x, sizeof(screen_x));
    memcpy(screen_y, scene->screen_vertices_y, sizeof(screen_y));
    memcpy(screen_z, scene->screen_vertices_z, sizeof(screen_z));
    memcpy(face_order, scene->tmp_face_order, sizeof(face_order));

    spy->recursive_depth++;
    ToriDraw_RenderModel(
        spy->fixture->handle, scene, &position, &viewport, &camera, spy->env->pixels);
    spy->recursive_depth--;

    spy->recursive_scratch_unchanged = spy->recursive_scratch_unchanged &&
        scene->screen_vertices_x == screen_x_pointer &&
        scene->screen_vertices_y == screen_y_pointer &&
        scene->screen_vertices_z == screen_z_pointer &&
        scene->orthographic_vertices_x == orthographic_x_pointer &&
        scene->orthographic_vertices_y == orthographic_y_pointer &&
        scene->orthographic_vertices_z == orthographic_z_pointer &&
        scene->tmp_face_order == face_order_pointer &&
        memcmp(&active_hnd, &scene->active_hnd, sizeof(active_hnd)) == 0 &&
        memcmp(&projected_vertex, &scene->projected_vertex, sizeof(projected_vertex)) == 0 &&
        memcmp(&aabb, &scene->aabb, sizeof(aabb)) == 0 &&
        memcmp(&cylinder_aabb, &scene->cylinder_fast_aabb, sizeof(cylinder_aabb)) == 0 &&
        memcmp(screen_x, scene->screen_vertices_x, sizeof(screen_x)) == 0 &&
        memcmp(screen_y, scene->screen_vertices_y, sizeof(screen_y)) == 0 &&
        memcmp(screen_z, scene->screen_vertices_z, sizeof(screen_z)) == 0 &&
        scene->tmp_face_order_count == ordered &&
        memcmp(face_order, scene->tmp_face_order, sizeof(face_order)) == 0 &&
        scene->projection_near_plane_z == projection_near_plane_z &&
        scene->near_clipped == near_clipped;

    /* Popping an inner context must not make the outer kernel binding mutable. */
    spy->set_after_nested =
        spy->set_after_nested || ToriDraw_SceneSetRasterKernel(scene, spy->replacement);
    spy->reset_after_nested =
        spy->reset_after_nested || ToriDraw_SceneResetRasterKernel(scene);
}

static void
spy_record(
    struct Spy* spy,
    enum ToriDraw_RasterFaceClass slot,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFace* face)
{
    CHECK(spy != NULL, "slot %d received null user_data", (int)slot);
    if( !spy )
        return;
    CHECK(face && face->face_class == slot, "%s callback slot %d received class %d", spy->name,
          (int)slot, face ? (int)face->face_class : -1);
    spy->calls[slot]++;
    if( face && face->face_index >= 0 && face->face_index < FACE_COUNT )
        spy->face_calls[face->face_index]++;
    spy->total_calls++;
    verify_target(spy, target);
    verify_face(spy, face);

    if( spy->attempt_recursive_render )
    {
        int const depth = spy->recursive_depth;
        CHECK(depth >= 0 && depth < 3, "%s recursive depth %d", spy->name, depth);
        if( depth >= 0 && depth < 3 )
        {
            if( !spy->screen_x_by_depth[depth] )
            {
                spy->screen_x_by_depth[depth] = target->screen_vertices_x;
                spy->face_order_by_depth[depth] = spy->env->scene->tmp_face_order;
            }
            else
            {
                CHECK(spy->screen_x_by_depth[depth] == target->screen_vertices_x,
                      "%s depth %d changed projected context within a pass", spy->name, depth);
                CHECK(spy->face_order_by_depth[depth] == spy->env->scene->tmp_face_order,
                      "%s depth %d changed face-order context within a pass", spy->name, depth);
            }
            if( depth > 0 )
            {
                spy->recursive_contexts_distinct = spy->recursive_contexts_distinct &&
                    spy->screen_x_by_depth[depth] != spy->screen_x_by_depth[depth - 1] &&
                    spy->face_order_by_depth[depth] != spy->face_order_by_depth[depth - 1];
            }
        }
    }

    if( spy->attempt_mutation && !spy->mutation_attempted )
    {
        spy->mutation_attempted = true;
        spy->set_during_callback = ToriDraw_SceneSetRasterKernel(
            spy->env->scene, spy->replacement);
        spy->reset_during_callback = ToriDraw_SceneResetRasterKernel(spy->env->scene);
    }
    if( spy->attempt_recursive_render && spy->recursive_depth < spy->recursive_depth_limit &&
        !spy->recursive_started[spy->recursive_depth] )
    {
        spy->recursive_started[spy->recursive_depth] = true;
        spy_attempt_same_scene_recursion(spy);
    }
}

#define DEFINE_SPY_CALLBACK(name, slot)                                                           \
    static void name(void* user_data, const struct ToriDraw_RasterTarget* target,                 \
                     const struct ToriDraw_RasterFace* face)                                      \
    {                                                                                              \
        spy_record((struct Spy*)user_data, slot, target, face);                                    \
    }

DEFINE_SPY_CALLBACK(spy_gouraud, TORIDRAW_RASTER_FACE_GOURAUD)
DEFINE_SPY_CALLBACK(spy_flat, TORIDRAW_RASTER_FACE_FLAT)
DEFINE_SPY_CALLBACK(spy_textured, TORIDRAW_RASTER_FACE_TEXTURED)
DEFINE_SPY_CALLBACK(spy_textured_flat, TORIDRAW_RASTER_FACE_TEXTURED_FLAT)

static void
spy_explicit_noop(
    void* user_data,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFace* face)
{
    struct Spy* spy = user_data;

    CHECK(spy != NULL, "explicit no-op received null user_data");
    if( !spy )
        return;
    spy->noop_calls++;
    CHECK(face && face->face_class == TORIDRAW_RASTER_FACE_TEXTURED_FLAT,
          "%s no-op received class %d", spy->name, face ? (int)face->face_class : -1);
    verify_target(spy, target);
    verify_face(spy, face);
    /* Deliberately no draw and no delegation. */
}

static const struct ToriDraw_RasterKernelVTable full_spy_vtable = {
    .draw_gouraud = spy_gouraud,
    .draw_flat = spy_flat,
    .draw_textured = spy_textured,
    .draw_textured_flat = spy_textured_flat,
};

static const struct ToriDraw_RasterKernelVTable sparse_spy_vtable = {
    .draw_flat = spy_flat,
    .draw_textured_flat = spy_explicit_noop,
};

static int
fixture_dispatch_count(void)
{
#ifdef TORIDRAW_PIXEL16
    return 3;
#else
    return FACE_COUNT;
#endif
}

static void
check_call_multiplier(const struct Spy* spy, int multiplier)
{
#ifdef TORIDRAW_PIXEL16
    static const int expected[TORIDRAW_RASTER_FACE_CLASS_COUNT] = { 2, 1, 0, 0 };
    static const int expected_faces[FACE_COUNT] = { 1, 1, 1, 0 };
    int const expected_total = 3;
#else
    static const int expected[TORIDRAW_RASTER_FACE_CLASS_COUNT] = { 1, 1, 1, 1 };
    static const int expected_faces[FACE_COUNT] = { 1, 1, 1, 1 };
    int const expected_total = FACE_COUNT;
#endif

    CHECK(spy->total_calls == expected_total * multiplier, "%s total calls %d", spy->name,
          spy->total_calls);
    for( int slot = 0; slot < TORIDRAW_RASTER_FACE_CLASS_COUNT; slot++ )
        CHECK(spy->calls[slot] == expected[slot] * multiplier,
              "%s slot %d calls %d, expected %d", spy->name, slot, spy->calls[slot],
              expected[slot] * multiplier);
    for( int face = 0; face < FACE_COUNT; face++ )
        CHECK(spy->face_calls[face] == expected_faces[face] * multiplier,
              "%s face %d calls %d, expected %d", spy->name, face, spy->face_calls[face],
              expected_faces[face] * multiplier);
}

static void
check_one_call_per_class(const struct Spy* spy)
{
    check_call_multiplier(spy, 1);
}

static void
test_four_slots_and_callback_guard(
    const struct Fixture* fixture,
    struct RenderEnv* env)
{
    struct Spy spy;
    struct ToriDraw_RasterKernel kernel;

#ifdef TORIDRAW_PIXEL16
    printf("Pixel16 collapse reaches normalized Gouraud/flat descriptors\n");
#else
    printf("four stock slots receive normalized descriptors\n");
#endif
    spy_init(&spy, "full", fixture, env);
    kernel = (struct ToriDraw_RasterKernel){
        .vtable = &full_spy_vtable,
        .user_data = &spy,
        .domains = TORIDRAW_RASTER_KERNEL_STOCK,
    };
    spy.attempt_mutation = true;
    spy.replacement = ToriDraw_RasterKernelGetBranching();

    CHECK(ToriDraw_SceneSetRasterKernel(env->scene, &kernel), "bind complete spy");
    CHECK(ToriDraw_SceneGetRasterKernel(env->scene) == &kernel, "complete spy getter");
    render_fixture(env, fixture);
    check_one_call_per_class(&spy);
    CHECK(spy.mutation_attempted, "callback mutation was not attempted");
    CHECK(!spy.set_during_callback, "Set succeeded during a callback");
    CHECK(!spy.reset_during_callback, "Reset succeeded during a callback");
    CHECK(ToriDraw_SceneGetRasterKernel(env->scene) == &kernel,
          "callback mutation changed the binding");
    CHECK(count_nonzero_pixels(env) == 0, "spy callbacks unexpectedly drew pixels");
    CHECK(ToriDraw_SceneResetRasterKernel(env->scene), "reset complete spy");
}

static void
test_same_scene_reentrant_stock(const struct Fixture* fixture, struct RenderEnv* env)
{
    struct Spy spy;
    struct ToriDraw_RasterKernel kernel;

    printf("same-scene stock callbacks re-enter through independent startup contexts\n");
    spy_init(&spy, "recursive-stock", fixture, env);
    spy.attempt_recursive_render = true;
    spy.recursive_depth_limit = 2;
    spy.replacement = ToriDraw_RasterKernelGetBranching();
    kernel = (struct ToriDraw_RasterKernel){
        .vtable = &full_spy_vtable,
        .user_data = &spy,
        .domains = TORIDRAW_RASTER_KERNEL_STOCK,
    };

    CHECK(ToriDraw_SceneSetRasterKernel(env->scene, &kernel), "bind recursive stock spy");
    render_fixture(env, fixture);
    check_call_multiplier(&spy, spy.recursive_depth_limit + 1);
    CHECK(spy.recursive_render_attempted, "recursive stock callback did not run");
    CHECK(spy.recursive_scratch_unchanged,
          "same-scene recursive RenderModel did not restore live outer scratch");
    CHECK(spy.recursive_contexts_distinct,
          "nested stock renders shared projected or face-order scratch");
    CHECK(!spy.set_after_nested && !spy.reset_after_nested,
          "popping a nested context cleared the outer binding guard");
    CHECK(env->scene->render_context_depth == 0 &&
              env->scene->nested_render_contexts_used == 0,
          "recursive stock render leaked an active context");
    CHECK(count_nonzero_pixels(env) == 0, "recursive spy callbacks unexpectedly drew pixels");
    CHECK(ToriDraw_SceneGetRasterKernel(env->scene) == &kernel,
          "recursive render changed the scene binding");
    CHECK(ToriDraw_SceneResetRasterKernel(env->scene), "reset recursive stock spy");
}

static void
check_one_face_suppressed(const struct Spy* spy, int suppressed_face)
{
    CHECK(spy->total_calls == fixture_dispatch_count() - 1,
          "%s dispatched %d faces after suppressing face %d", spy->name, spy->total_calls,
          suppressed_face);
    for( int face = 0; face < FACE_COUNT; face++ )
    {
#ifdef TORIDRAW_PIXEL16
        int const normally_dispatched = face != 3;
#else
        int const normally_dispatched = 1;
#endif
        int const expected = normally_dispatched && face != suppressed_face;
        CHECK(spy->face_calls[face] == expected, "%s face %d calls %d, expected %d", spy->name,
              face, spy->face_calls[face], expected);
    }
}

static void
test_pre_dispatch_skips(struct Fixture* fixture, struct RenderEnv* env)
{
    struct Spy spy;
    struct ToriDraw_RasterKernel kernel = {
        .vtable = &full_spy_vtable,
        .user_data = &spy,
        .domains = TORIDRAW_RASTER_KERNEL_STOCK,
    };
    int saved_int;
    hsl16_t saved_shade;
    alphaint_t saved_alpha;
    faceint_t saved_b;
    faceint_t saved_c;

    printf("pre-dispatch hidden, invalid, alpha, texture and backface policy calls no slot\n");
    CHECK(ToriDraw_SceneSetRasterKernel(env->scene, &kernel), "bind skip-policy spy");

    saved_int = fixture->face_info[0];
    fixture->face_info[0] = 2;
    spy_init(&spy, "raw-type-2", fixture, env);
    render_fixture(env, fixture);
    check_one_face_suppressed(&spy, 0);
    fixture->face_info[0] = saved_int;

    saved_int = fixture->face_info[0];
    fixture->face_info[0] = 4;
    spy_init(&spy, "raw-type-out-of-range", fixture, env);
    render_fixture(env, fixture);
    check_one_face_suppressed(&spy, 0);
    fixture->face_info[0] = saved_int;

    saved_shade = fixture->shade_c[0];
    fixture->shade_c[0] = TORIDRAWHSL16_HIDDEN;
    spy_init(&spy, "hidden-colour", fixture, env);
    render_fixture(env, fixture);
    check_one_face_suppressed(&spy, 0);
    fixture->shade_c[0] = saved_shade;

    saved_alpha = fixture->alpha[0];
    fixture->alpha[0] = 254;
    spy_init(&spy, "alpha-cutoff", fixture, env);
    render_fixture(env, fixture);
    check_one_face_suppressed(&spy, 0);
    fixture->alpha[0] = saved_alpha;

#ifndef TORIDRAW_PIXEL16
    saved_int = fixture->texture[2];
    fixture->texture[2] = TEST_TEXTURE_ID + 1;
    spy_init(&spy, "missing-texture", fixture, env);
    render_fixture(env, fixture);
    check_one_face_suppressed(&spy, 2);
    fixture->texture[2] = (faceint_t)saved_int;

    {
        faceint_t malformed_coords[FACE_COUNT] = { -1, -1, 1, -1 };

        fixture->model.face_texture_coords = malformed_coords;
        spy_init(&spy, "malformed-texture-coordinate", fixture, env);
        render_fixture(env, fixture);
        check_one_face_suppressed(&spy, 2);
        fixture->model.face_texture_coords = NULL;
    }
#endif

    saved_b = fixture->face_b[0];
    saved_c = fixture->face_c[0];
    fixture->face_b[0] = saved_c;
    fixture->face_c[0] = saved_b;
    spy_init(&spy, "backface", fixture, env);
    render_fixture_with_ordered_count(env, fixture, FACE_COUNT - 1);
    check_one_face_suppressed(&spy, 0);
    fixture->face_b[0] = saved_b;
    fixture->face_c[0] = saved_c;

    CHECK(ToriDraw_SceneResetRasterKernel(env->scene), "reset skip-policy spy");
}

static void
test_live_chain_mutation_recovery(
    const struct Fixture* fixture,
    struct RenderEnv* env)
{
    struct Spy spy;
    struct ToriDraw_RasterKernel kernel;

    printf("a malformed live borrowed chain is discarded wholesale at pass entry\n");
    spy_init(&spy, "mutated-live-chain", fixture, env);
    kernel = (struct ToriDraw_RasterKernel){
        .vtable = &full_spy_vtable,
        .user_data = &spy,
        .domains = TORIDRAW_RASTER_KERNEL_STOCK,
    };
    CHECK(ToriDraw_SceneSetRasterKernel(env->scene, &kernel), "bind live-mutation control");

    kernel.domains = 0;
    render_fixture(env, fixture);
    CHECK(spy.total_calls == 0, "part of an invalid live chain reached a callback");
    CHECK(count_nonzero_pixels(env) > 0, "terminal did not recover an invalid live chain");
    CHECK(ToriDraw_SceneGetRasterKernel(env->scene) == &kernel,
          "pass-time validation rewrote the borrowed scene binding");

    kernel.domains = TORIDRAW_RASTER_KERNEL_STOCK;
    CHECK(ToriDraw_SceneResetRasterKernel(env->scene), "reset restored live-mutation control");
}

static void
test_sparse_fallback_and_noop(
    const struct Fixture* fixture,
    struct RenderEnv* env)
{
    struct Spy head;
    struct Spy tail;
    struct ToriDraw_RasterKernel tail_kernel;
    struct ToriDraw_RasterKernel head_kernel;

    printf("sparse fallback retains supplier user_data; explicit no-op does not inherit\n");
    spy_init(&head, "sparse-head", fixture, env);
    spy_init(&tail, "fallback-tail", fixture, env);
    tail_kernel = (struct ToriDraw_RasterKernel){
        .vtable = &full_spy_vtable,
        .user_data = &tail,
        .domains = TORIDRAW_RASTER_KERNEL_STOCK,
    };
    head_kernel = (struct ToriDraw_RasterKernel){
        .vtable = &sparse_spy_vtable,
        .user_data = &head,
        .fallback = &tail_kernel,
        .domains = TORIDRAW_RASTER_KERNEL_STOCK,
    };

    CHECK(ToriDraw_SceneSetRasterKernel(env->scene, &head_kernel), "bind sparse chain");
    render_fixture(env, fixture);
    CHECK(head.total_calls == 1 && head.calls[TORIDRAW_RASTER_FACE_FLAT] == 1,
          "sparse head supplied the wrong ordinary slots");
#ifdef TORIDRAW_PIXEL16
    CHECK(head.noop_calls == 0, "Pixel16 emitted a textured-flat slot");
    CHECK(tail.total_calls == 2 && tail.calls[TORIDRAW_RASTER_FACE_GOURAUD] == 2,
          "Pixel16 fallback tail did not supply both Gouraud faces");
    CHECK(tail.calls[TORIDRAW_RASTER_FACE_FLAT] == 0 &&
              tail.calls[TORIDRAW_RASTER_FACE_TEXTURED] == 0 &&
              tail.calls[TORIDRAW_RASTER_FACE_TEXTURED_FLAT] == 0,
          "Pixel16 sparse chain reached an unexpected slot");
#else
    CHECK(head.noop_calls == 1, "explicit no-op calls %d", head.noop_calls);
    CHECK(tail.total_calls == 2 && tail.calls[TORIDRAW_RASTER_FACE_GOURAUD] == 1 &&
              tail.calls[TORIDRAW_RASTER_FACE_TEXTURED] == 1,
          "fallback tail did not supply exactly the two null slots");
    CHECK(tail.calls[TORIDRAW_RASTER_FACE_FLAT] == 0 &&
              tail.calls[TORIDRAW_RASTER_FACE_TEXTURED_FLAT] == 0,
          "fallback traversed past an explicit slot");
#endif
    CHECK(count_nonzero_pixels(env) == 0, "sparse spy chain unexpectedly drew pixels");
    CHECK(ToriDraw_SceneResetRasterKernel(env->scene), "reset sparse chain");
}

static void
test_two_scene_independence(
    const struct Fixture* fixture,
    struct RenderEnv* first,
    struct RenderEnv* second)
{
    struct Spy first_spy;
    struct Spy second_spy;
    struct ToriDraw_RasterKernel first_kernel;
    struct ToriDraw_RasterKernel second_kernel;

    printf("two scenes retain independent complete roots\n");
    spy_init(&first_spy, "scene-one", fixture, first);
    spy_init(&second_spy, "scene-two", fixture, second);
    first_kernel = (struct ToriDraw_RasterKernel){
        .vtable = &full_spy_vtable,
        .user_data = &first_spy,
        .domains = TORIDRAW_RASTER_KERNEL_STOCK,
    };
    second_kernel = (struct ToriDraw_RasterKernel){
        .vtable = &full_spy_vtable,
        .user_data = &second_spy,
        .domains = TORIDRAW_RASTER_KERNEL_STOCK,
    };

    CHECK(ToriDraw_SceneSetRasterKernel(first->scene, &first_kernel), "bind first scene");
    CHECK(ToriDraw_SceneSetRasterKernel(second->scene, &second_kernel), "bind second scene");
    render_fixture(first, fixture);
    check_one_call_per_class(&first_spy);
    CHECK(second_spy.total_calls == 0, "first render called the second scene's kernel");
    render_fixture(second, fixture);
    check_one_call_per_class(&second_spy);
    CHECK(first_spy.total_calls == fixture_dispatch_count(),
          "second render called the first scene's kernel");
    CHECK(ToriDraw_SceneGetRasterKernel(first->scene) == &first_kernel &&
              ToriDraw_SceneGetRasterKernel(second->scene) == &second_kernel,
          "scene bindings were not independent");
    CHECK(ToriDraw_SceneResetRasterKernel(first->scene), "reset first scene");
    CHECK(ToriDraw_SceneResetRasterKernel(second->scene), "reset second scene");
}

static void
test_incompatible_domain(
    const struct Fixture* fixture,
    struct RenderEnv* env)
{
    struct Spy hd;
    struct Spy stock;
    struct ToriDraw_RasterKernel stock_kernel;
    struct ToriDraw_RasterKernel hd_kernel;

    printf("stock passes skip incompatible HD-domain nodes\n");
    spy_init(&hd, "hd-only", fixture, env);
    spy_init(&stock, "stock-fallback", fixture, env);
    stock_kernel = (struct ToriDraw_RasterKernel){
        .vtable = &full_spy_vtable,
        .user_data = &stock,
        .domains = TORIDRAW_RASTER_KERNEL_STOCK,
    };
    hd_kernel = (struct ToriDraw_RasterKernel){
        .vtable = &full_spy_vtable,
        .user_data = &hd,
        .fallback = &stock_kernel,
        .domains = TORIDRAW_RASTER_KERNEL_HD,
    };

    CHECK(ToriDraw_SceneSetRasterKernel(env->scene, &hd_kernel), "bind mixed-domain chain");
    render_fixture(env, fixture);
    CHECK(hd.total_calls == 0, "stock pass called an HD-domain node");
    check_one_call_per_class(&stock);
    CHECK(ToriDraw_SceneResetRasterKernel(env->scene), "reset mixed-domain chain");
}

static void
test_invalid_set_rejection(
    const struct Fixture* fixture,
    struct RenderEnv* env)
{
    struct Spy spy;
    struct ToriDraw_RasterKernel valid;
    struct ToriDraw_RasterKernel no_vtable;
    struct ToriDraw_RasterKernel no_domain;
    struct ToriDraw_RasterKernel unknown_domain;
    struct ToriDraw_RasterKernel cycle;

    printf("Set rejects null, malformed, unknown-domain and cyclic chains\n");
    spy_init(&spy, "valid-before-errors", fixture, env);
    valid = (struct ToriDraw_RasterKernel){
        .vtable = &full_spy_vtable,
        .user_data = &spy,
        .domains = TORIDRAW_RASTER_KERNEL_STOCK,
    };
    no_vtable = (struct ToriDraw_RasterKernel){
        .domains = TORIDRAW_RASTER_KERNEL_STOCK,
    };
    no_domain = (struct ToriDraw_RasterKernel){
        .vtable = &full_spy_vtable,
    };
    unknown_domain = (struct ToriDraw_RasterKernel){
        .vtable = &full_spy_vtable,
        .domains = 1u << 12,
    };
    cycle = (struct ToriDraw_RasterKernel){
        .vtable = &full_spy_vtable,
        .domains = TORIDRAW_RASTER_KERNEL_STOCK,
    };
    cycle.fallback = &cycle;

    CHECK(ToriDraw_SceneSetRasterKernel(env->scene, &valid), "bind valid control kernel");
    CHECK(!ToriDraw_SceneSetRasterKernel(env->scene, NULL), "Set accepted NULL");
    CHECK(!ToriDraw_SceneSetRasterKernel(env->scene, &no_vtable), "Set accepted null vtable");
    CHECK(!ToriDraw_SceneSetRasterKernel(env->scene, &no_domain), "Set accepted zero domains");
    CHECK(!ToriDraw_SceneSetRasterKernel(env->scene, &unknown_domain),
          "Set accepted unknown domain bits");
    CHECK(!ToriDraw_SceneSetRasterKernel(env->scene, &cycle), "Set accepted a fallback cycle");
    CHECK(ToriDraw_SceneGetRasterKernel(env->scene) == &valid,
          "a rejected Set changed the previous binding");
    CHECK(ToriDraw_SceneResetRasterKernel(env->scene), "reset valid control kernel");
}

static void
test_reset_and_process_terminals(
    const struct Fixture* fixture,
    struct RenderEnv* env)
{
    struct Spy spy;
    struct ToriDraw_RasterKernel kernel;
    bool const original_scanline = ToriDraw_RasterGetScanline();

    printf("Reset drops the explicit root and both process terminals remain live\n");
    spy_init(&spy, "reset-control", fixture, env);
    kernel = (struct ToriDraw_RasterKernel){
        .vtable = &full_spy_vtable,
        .user_data = &spy,
        .domains = TORIDRAW_RASTER_KERNEL_STOCK,
    };
    CHECK(ToriDraw_SceneSetRasterKernel(env->scene, &kernel), "bind reset control");
    render_fixture(env, fixture);
    check_one_call_per_class(&spy);
    CHECK(ToriDraw_SceneResetRasterKernel(env->scene), "reset explicit root");
    CHECK(ToriDraw_SceneGetRasterKernel(env->scene) == NULL, "Reset did not store inherit sentinel");

    ToriDraw_RasterSetScanline(false);
    render_fixture(env, fixture);
    CHECK(count_nonzero_pixels(env) > 0, "branching process terminal drew no pixels");
    CHECK(spy.total_calls == fixture_dispatch_count(),
          "reset scene retained the old callback chain");
    CHECK(ToriDraw_SceneGetRasterKernel(env->scene) == NULL,
          "branching process selection created an explicit binding");

    ToriDraw_RasterSetScanline(true);
    render_fixture(env, fixture);
    CHECK(count_nonzero_pixels(env) > 0, "scanline process terminal drew no pixels");
    CHECK(spy.total_calls == fixture_dispatch_count(),
          "process switch revived the old callback chain");
    CHECK(ToriDraw_SceneGetRasterKernel(env->scene) == NULL,
          "scanline process selection created an explicit binding");

    ToriDraw_RasterSetScanline(original_scanline);
}

#ifndef TORIDRAW_PIXEL16

#define HD_TEXTURE_COUNT 2

struct HDFixture
{
    struct ToriDraw_ModelHD hd;
    struct ToriDraw_ModelHandle handle;
    vertexint_t vertex_x[VERTEX_COUNT];
    vertexint_t vertex_y[VERTEX_COUNT];
    vertexint_t vertex_z[VERTEX_COUNT];
    faceint_t face_a[FACE_COUNT];
    faceint_t face_b[FACE_COUNT];
    faceint_t face_c[FACE_COUNT];
    hsl16_t shade_a[FACE_COUNT];
    hsl16_t shade_b[FACE_COUNT];
    hsl16_t shade_c[FACE_COUNT];
    hsl16_t authored_color[FACE_COUNT];
    alphaint_t alpha[FACE_COUNT];
    int face_info[FACE_COUNT];
    faceint_t texture[FACE_COUNT];
    faceint_t texture_coord[FACE_COUNT];
    uint8_t render_type[HD_TEXTURE_COUNT];
    faceint_t texture_p[HD_TEXTURE_COUNT];
    faceint_t texture_m[HD_TEXTURE_COUNT];
    faceint_t texture_n[HD_TEXTURE_COUNT];
};

struct HDSpy
{
    const char* name;
    const struct HDFixture* fixture;
    const struct RenderEnv* env;
    const struct ToriDraw_HDMaterial* materials;
    int calls[TORIDRAW_RASTER_FACE_CLASS_COUNT];
    int face_calls[FACE_COUNT];
    int total_calls;
    bool missing_last_material;
    bool first_mapping_fallback;
    bool first_malformed_render_type;
    bool expect_depth;
    bool attempt_mutation;
    bool mutation_attempted;
    bool set_during_callback;
    bool reset_during_callback;
    bool attempt_recursive_render;
    bool recursive_render_attempted;
    bool recursive_render_active;
    bool recursive_zbuffer;
    bool recursive_scratch_unchanged;
    bool recursive_context_distinct;
    const int* outer_screen_x;
    const int* outer_face_order;
    const torizdepth_t* outer_zbuffer;
};

static void
hd_fixture_init(struct HDFixture* fixture)
{
    static const int centre_x[FACE_COUNT] = { -120, -40, 40, 120 };
    struct ToriDraw_Model* model;

    memset(fixture, 0, sizeof(*fixture));
    model = &fixture->hd.base;
    model->vertex_count = VERTEX_COUNT;
    model->face_count = FACE_COUNT;
    model->vertices_x = fixture->vertex_x;
    model->vertices_y = fixture->vertex_y;
    model->vertices_z = fixture->vertex_z;
    model->face_indices_a = fixture->face_a;
    model->face_indices_b = fixture->face_b;
    model->face_indices_c = fixture->face_c;
    model->face_colors_a = fixture->shade_a;
    model->face_colors_b = fixture->shade_b;
    model->face_colors_c = fixture->shade_c;
    model->face_colors = fixture->authored_color;
    model->face_alphas = fixture->alpha;
    model->face_infos = fixture->face_info;
    model->face_textures = fixture->texture;
    model->face_texture_coords = fixture->texture_coord;
    model->textured_face_count = HD_TEXTURE_COUNT;
    model->texture_render_types = fixture->render_type;
    model->textured_p_coordinate = fixture->texture_p;
    model->textured_m_coordinate = fixture->texture_m;
    model->textured_n_coordinate = fixture->texture_n;

    for( int face = 0; face < FACE_COUNT; face++ )
    {
        int const vertex = face * 3;
        int const x = centre_x[face];

        fixture->vertex_x[vertex + 0] = (vertexint_t)(x - 22);
        fixture->vertex_y[vertex + 0] = -28;
        fixture->vertex_x[vertex + 1] = (vertexint_t)(x + 22);
        fixture->vertex_y[vertex + 1] = -28;
        fixture->vertex_x[vertex + 2] = (vertexint_t)x;
        fixture->vertex_y[vertex + 2] = 28;
        fixture->face_a[face] = (faceint_t)(vertex + 0);
        fixture->face_b[face] = (faceint_t)(vertex + 2);
        fixture->face_c[face] = (faceint_t)(vertex + 1);
        fixture->texture[face] = -1;
        fixture->texture_coord[face] = -1;
        fixture->authored_color[face] = 0x4A40;
    }

    fixture->shade_a[0] = 0x1234;
    fixture->shade_b[0] = 0x1235;
    fixture->shade_c[0] = 0x1236;
    fixture->alpha[0] = 17;

    fixture->shade_a[1] = 0x2345;
    fixture->shade_b[1] = 0x3456;
    fixture->shade_c[1] = TORIDRAWHSL16_FLAT;

    fixture->shade_a[2] = 20;
    fixture->shade_b[2] = 30;
    fixture->shade_c[2] = 40;
    fixture->alpha[2] = 64;
    fixture->texture[2] = 0;
    fixture->texture_coord[2] = 0;

    fixture->shade_a[3] = 55;
    fixture->shade_b[3] = 66;
    fixture->shade_c[3] = TORIDRAWHSL16_FLAT;
    fixture->texture[3] = 1;
    fixture->texture_coord[3] = 1;

    fixture->render_type[0] = 1;
    fixture->render_type[1] = 0;
    fixture->texture_p[0] = fixture->face_a[2];
    fixture->texture_m[0] = fixture->face_b[2];
    fixture->texture_n[0] = fixture->face_c[2];
    fixture->texture_p[1] = fixture->face_a[3];
    fixture->texture_m[1] = fixture->face_b[3];
    fixture->texture_n[1] = fixture->face_c[3];

    fixture->handle = ToriDraw_ModelHandleFromHD(&fixture->hd);
    ToriDraw_ModelSetBoundsCylinder(model);
    CHECK(ToriDraw_ModelBuildTextureMappings(
              &fixture->hd, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
          "HD mapping build");
}

static void
hd_fixture_destroy(struct HDFixture* fixture)
{
    free(fixture->hd.texture_mappings);
    free(fixture->hd.base.bounds_cylinder);
    fixture->hd.texture_mappings = NULL;
    fixture->hd.base.bounds_cylinder = NULL;
}

static void
hd_spy_init(
    struct HDSpy* spy,
    const char* name,
    const struct HDFixture* fixture,
    const struct RenderEnv* env,
    const struct ToriDraw_HDMaterial* materials)
{
    memset(spy, 0, sizeof(*spy));
    spy->name = name;
    spy->fixture = fixture;
    spy->env = env;
    spy->materials = materials;
    spy->recursive_scratch_unchanged = true;
    spy->recursive_context_distinct = true;
}

static void
verify_hd_target(const struct HDSpy* spy, const struct ToriDraw_RasterTarget* target)
{
    const struct ToriDraw_Model* model = &spy->fixture->hd.base;

    CHECK(target && target->domain == TORIDRAW_RASTER_KERNEL_HD, "%s target domain", spy->name);
    if( !target )
        return;
    CHECK(target->pixel_buffer == spy->env->pixels + CLIP_LEFT + CLIP_TOP * VIEW_STRIDE,
          "%s HD framebuffer was not clip-rebased", spy->name);
    if( spy->expect_depth )
        CHECK(target->depth_test && target->zbuffer ==
                                          spy->env->scene->zbuffer + CLIP_LEFT +
                                              CLIP_TOP * VIEW_STRIDE,
              "%s HD depth target was not clip-rebased", spy->name);
    else
        CHECK(target->zbuffer == NULL && !target->depth_test, "%s HD sorted pass depth-tested",
              spy->name);
    CHECK(target->width == CLIP_RIGHT - CLIP_LEFT &&
              target->height == CLIP_BOTTOM - CLIP_TOP && target->stride == VIEW_STRIDE,
          "%s HD target geometry %dx%d/%d", spy->name, target->width, target->height,
          target->stride);
    CHECK(target->clip_origin_x == CLIP_LEFT && target->clip_origin_y == CLIP_TOP,
          "%s HD clip origin (%d,%d)", spy->name, target->clip_origin_x,
          target->clip_origin_y);
    CHECK(target->projection_center_x == (CLIP_RIGHT - CLIP_LEFT) / 2 &&
              target->projection_center_y == (CLIP_BOTTOM - CLIP_TOP) / 2,
          "%s HD projection centre", spy->name);
    CHECK(target->near_plane_z == spy->env->scene->projection_near_plane_z &&
              target->camera_cot16 != 0 &&
              target->model_mid_z == spy->env->scene->projected_vertex.z,
          "%s HD camera state", spy->name);
    CHECK(!target->parallel_projection && !target->smooth_shading &&
              !target->affine_textures && target->near_clip_available,
          "%s HD target flags", spy->name);
    CHECK(target->vertex_count == VERTEX_COUNT, "%s HD vertex count %d", spy->name,
          target->vertex_count);
    CHECK(target->screen_vertices_x == spy->env->scene->screen_vertices_x &&
              target->orthographic_vertices_x == spy->env->scene->orthographic_vertices_x,
          "%s HD projected arrays", spy->name);
    CHECK(target->posed_vertices_x == model->vertices_x &&
              target->bind_vertices_x == model->vertices_x,
          "%s HD posed/bind arrays", spy->name);
}

static void
verify_hd_face(const struct HDSpy* spy, const struct ToriDraw_RasterFace* face)
{
    const struct HDFixture* fixture = spy->fixture;
    int index;
    enum ToriDraw_RasterFaceClass expected;

    CHECK(face != NULL, "%s received null HD face", spy->name);
    if( !face )
        return;
    index = face->face_index;
    CHECK(index >= 0 && index < FACE_COUNT, "%s HD face index %d", spy->name, index);
    if( index < 0 || index >= FACE_COUNT )
        return;
    expected = expected_class(index);
    if( spy->missing_last_material && index == 3 )
        expected = TORIDRAW_RASTER_FACE_FLAT;
    CHECK(face->face_class == expected, "%s HD face %d class %d, expected %d", spy->name,
          index, (int)face->face_class, (int)expected);
    CHECK(face->vertex[0] == fixture->face_a[index] &&
              face->vertex[1] == fixture->face_b[index] &&
              face->vertex[2] == fixture->face_c[index] && !face->near_clipped,
          "%s HD face %d source vertices", spy->name, index);

    if( index == 0 )
    {
        CHECK(face->shade[0] == 0x1234 && face->shade[1] == 0x1235 &&
                  face->shade[2] == 0x1236 && face->opacity == 238,
              "%s HD Gouraud normalization", spy->name);
    }
    else if( index == 1 )
    {
        CHECK(face->shade[0] == 0x2345 && face->shade[1] == 0x2345 &&
                  face->shade[2] == 0x2345 && face->opacity == 255,
              "%s HD flat normalization", spy->name);
    }
    else if( index == 2 )
    {
        int const rgb = ToriDraw_Hsl16ToRgb(fixture->authored_color[2]);
        int const expected_r = ((rgb >> 16) & 0xFF) * 2;
        int const expected_g = ((rgb >> 8) & 0xFF) * 2;
        int const expected_b = (rgb & 0xFF) * 2;
        int expected_gate = spy->materials[0].gate;

        if( expected_gate < TORIDRAW_HD_GATE_OPAQUE || expected_gate > TORIDRAW_HD_GATE_ALPHA )
            expected_gate = TORIDRAW_HD_GATE_OPAQUE;

        CHECK(face->shade[0] == 20 && face->shade[1] == 30 && face->shade[2] == 40 &&
                  face->opacity == 191,
              "%s HD textured normalization", spy->name);
        CHECK(face->texture.texture_id == 0 &&
                  face->texture.texels == spy->materials[0].texels &&
                  face->texture.width == TEST_TEXTURE_WIDTH &&
                  face->texture.height == TEST_TEXTURE_WIDTH,
              "%s HD mapped texture identity", spy->name);
        CHECK(face->texture.gate == (enum ToriDraw_RasterTextureGate)expected_gate &&
                  face->texture.clamp_s && !face->texture.clamp_t,
              "%s HD mapped sampler flags", spy->name);
        if( spy->first_malformed_render_type )
            CHECK(face->texture.render_type == 255 &&
                      face->texture.mapping_payload == TORIDRAW_RASTER_MAPPING_VERTEX_FRAME &&
                      face->texture.mapping.vertex_frame.p == fixture->texture_p[0] &&
                      face->texture.mapping.vertex_frame.m == fixture->texture_m[0] &&
                      face->texture.mapping.vertex_frame.n == fixture->texture_n[0],
                  "%s malformed HD render type was not coerced to its valid frame", spy->name);
        else if( spy->first_mapping_fallback )
            CHECK(face->texture.render_type == 1 &&
                      face->texture.mapping_payload ==
                          TORIDRAW_RASTER_MAPPING_HD_FRAME_FALLBACK &&
                      face->texture.mapping.vertex_frame.p == fixture->texture_p[0] &&
                      face->texture.mapping.vertex_frame.m == fixture->texture_m[0] &&
                      face->texture.mapping.vertex_frame.n == fixture->texture_n[0],
                  "%s missing HD mapping did not expose its valid frame fallback", spy->name);
        else
            CHECK(face->texture.render_type == 1 &&
                      face->texture.mapping_payload == TORIDRAW_RASTER_MAPPING_HD &&
                      face->texture.mapping.hd_mapping == fixture->hd.texture_mappings,
                  "%s HD mapping payload", spy->name);
        CHECK(face->texture.modulate && face->texture.texture_neutral == 128 &&
                  face->texture.tint_r == expected_r && face->texture.tint_g == expected_g &&
                  face->texture.tint_b == expected_b,
              "%s HD tint (%d,%d,%d)", spy->name, face->texture.tint_r,
              face->texture.tint_g, face->texture.tint_b);
    }
    else if( !spy->missing_last_material )
    {
        int expected_gate = spy->materials[1].gate;

        if( expected_gate < TORIDRAW_HD_GATE_OPAQUE || expected_gate > TORIDRAW_HD_GATE_ALPHA )
            expected_gate = TORIDRAW_HD_GATE_OPAQUE;
        CHECK(face->shade[0] == 55 && face->shade[1] == 55 && face->shade[2] == 55 &&
                  face->opacity == 255,
              "%s HD textured-flat normalization", spy->name);
        CHECK(face->texture.texture_id == 1 &&
                  face->texture.texels == spy->materials[1].texels &&
                  face->texture.gate == (enum ToriDraw_RasterTextureGate)expected_gate &&
                  !face->texture.clamp_s && face->texture.clamp_t,
              "%s HD flat texture sampler", spy->name);
        CHECK(face->texture.mapping_payload == TORIDRAW_RASTER_MAPPING_VERTEX_FRAME &&
                  face->texture.mapping.vertex_frame.p == fixture->texture_p[1] &&
                  face->texture.mapping.vertex_frame.m == fixture->texture_m[1] &&
                  face->texture.mapping.vertex_frame.n == fixture->texture_n[1],
              "%s HD frame mapping", spy->name);
        CHECK(!face->texture.modulate && face->texture.tint_r == 256 &&
                  face->texture.tint_g == 256 && face->texture.tint_b == 256 &&
                  face->texture.texture_neutral == 144,
              "%s HD non-modulated tint", spy->name);
    }
    else
    {
        CHECK(face->shade[0] == 55 && face->shade[1] == 55 && face->shade[2] == 55 &&
                  face->opacity == 255,
              "%s missing material did not route as normalized solid", spy->name);
    }
}

static void
hd_spy_attempt_same_scene_recursion(struct HDSpy* spy)
{
    struct ToriDraw_Scene* scene = spy->env->scene;
    struct ToriDraw_ViewPort viewport = test_viewport();
    struct ToriDraw_Camera camera = test_camera();
    struct ToriDraw_Position position = { .x = -70, .y = 25, .z = 900, .yaw = 96 };
    struct ToriDraw_HDMaterials table = { spy->materials, HD_TEXTURE_COUNT };
    struct ToriDraw_HDRenderStats stats;
    struct ToriDraw_ModelHandle active_hnd = scene->active_hnd;
    struct ProjectedVertex projected_vertex = scene->projected_vertex;
    struct ToriDraw_AABB aabb = scene->aabb;
    struct ToriDraw_AABB cylinder_aabb = scene->cylinder_fast_aabb;
    int screen_x[VERTEX_COUNT];
    int screen_y[VERTEX_COUNT];
    int screen_z[VERTEX_COUNT];
    int face_order[FACE_COUNT];
    int* const screen_x_pointer = scene->screen_vertices_x;
    int* const screen_y_pointer = scene->screen_vertices_y;
    int* const screen_z_pointer = scene->screen_vertices_z;
    int* const orthographic_x_pointer = scene->orthographic_vertices_x;
    int* const orthographic_y_pointer = scene->orthographic_vertices_y;
    int* const orthographic_z_pointer = scene->orthographic_vertices_z;
    int* const face_order_pointer = scene->tmp_face_order;
    int const ordered = scene->tmp_face_order_count;
    int const near_plane = scene->projection_near_plane_z;
    bool const near_clipped = scene->near_clipped;

    memcpy(screen_x, scene->screen_vertices_x, sizeof(screen_x));
    memcpy(screen_y, scene->screen_vertices_y, sizeof(screen_y));
    memcpy(screen_z, scene->screen_vertices_z, sizeof(screen_z));
    memcpy(face_order, scene->tmp_face_order, sizeof(face_order));

    spy->recursive_render_attempted = true;
    spy->recursive_render_active = true;
    spy->outer_screen_x = screen_x_pointer;
    spy->outer_face_order = face_order_pointer;
    spy->outer_zbuffer = scene->zbuffer;
    int result = spy->recursive_zbuffer
                     ? ToriDraw_RenderHDZBuffered(
                           spy->fixture->handle, scene, &position, &viewport, &camera,
                           spy->env->pixels, &table, &stats)
                     : ToriDraw_RenderHD(
                           spy->fixture->handle, scene, &position, &viewport, &camera,
                           spy->env->pixels, &table, &stats);
    spy->recursive_render_active = false;

    CHECK(result == TORIDRAW_CULL_VISIBLE, "%s nested HD render returned %d", spy->name, result);
    spy->recursive_scratch_unchanged = spy->recursive_scratch_unchanged &&
        scene->screen_vertices_x == screen_x_pointer &&
        scene->screen_vertices_y == screen_y_pointer &&
        scene->screen_vertices_z == screen_z_pointer &&
        scene->orthographic_vertices_x == orthographic_x_pointer &&
        scene->orthographic_vertices_y == orthographic_y_pointer &&
        scene->orthographic_vertices_z == orthographic_z_pointer &&
        scene->tmp_face_order == face_order_pointer &&
        memcmp(&active_hnd, &scene->active_hnd, sizeof(active_hnd)) == 0 &&
        memcmp(&projected_vertex, &scene->projected_vertex, sizeof(projected_vertex)) == 0 &&
        memcmp(&aabb, &scene->aabb, sizeof(aabb)) == 0 &&
        memcmp(&cylinder_aabb, &scene->cylinder_fast_aabb, sizeof(cylinder_aabb)) == 0 &&
        memcmp(screen_x, scene->screen_vertices_x, sizeof(screen_x)) == 0 &&
        memcmp(screen_y, scene->screen_vertices_y, sizeof(screen_y)) == 0 &&
        memcmp(screen_z, scene->screen_vertices_z, sizeof(screen_z)) == 0 &&
        scene->tmp_face_order_count == ordered &&
        memcmp(face_order, scene->tmp_face_order, sizeof(face_order)) == 0 &&
        scene->projection_near_plane_z == near_plane && scene->near_clipped == near_clipped;
}

static void
hd_spy_record(
    struct HDSpy* spy,
    enum ToriDraw_RasterFaceClass slot,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFace* face)
{
    CHECK(spy != NULL, "HD callback slot %d received null user_data", (int)slot);
    if( !spy )
        return;
    CHECK(face && face->face_class == slot, "%s HD slot %d received class %d", spy->name,
          (int)slot, face ? (int)face->face_class : -1);
    spy->calls[slot]++;
    spy->total_calls++;
    if( face && face->face_index >= 0 && face->face_index < FACE_COUNT )
        spy->face_calls[face->face_index]++;
    verify_hd_target(spy, target);
    verify_hd_face(spy, face);

    if( spy->recursive_render_active )
    {
        spy->recursive_context_distinct = spy->recursive_context_distinct &&
            target->screen_vertices_x != spy->outer_screen_x &&
            spy->env->scene->tmp_face_order != spy->outer_face_order;
        if( spy->recursive_zbuffer )
            spy->recursive_context_distinct = spy->recursive_context_distinct &&
                spy->env->scene->zbuffer != spy->outer_zbuffer;
    }
    if( spy->attempt_recursive_render && !spy->recursive_render_attempted )
        hd_spy_attempt_same_scene_recursion(spy);

    if( spy->attempt_mutation && !spy->mutation_attempted )
    {
        spy->mutation_attempted = true;
        spy->set_during_callback = ToriDraw_SceneSetRasterKernel(
            spy->env->scene, ToriDraw_RasterKernelGetHDBranching());
        spy->reset_during_callback = ToriDraw_SceneResetRasterKernel(spy->env->scene);
    }
}

#define DEFINE_HD_SPY_CALLBACK(name, slot)                                                     \
    static void name(void* user_data, const struct ToriDraw_RasterTarget* target,              \
                     const struct ToriDraw_RasterFace* face)                                   \
    {                                                                                           \
        hd_spy_record((struct HDSpy*)user_data, slot, target, face);                             \
    }

DEFINE_HD_SPY_CALLBACK(hd_spy_gouraud, TORIDRAW_RASTER_FACE_GOURAUD)
DEFINE_HD_SPY_CALLBACK(hd_spy_flat, TORIDRAW_RASTER_FACE_FLAT)
DEFINE_HD_SPY_CALLBACK(hd_spy_textured, TORIDRAW_RASTER_FACE_TEXTURED)
DEFINE_HD_SPY_CALLBACK(hd_spy_textured_flat, TORIDRAW_RASTER_FACE_TEXTURED_FLAT)

static const struct ToriDraw_RasterKernelVTable hd_spy_vtable = {
    .draw_gouraud = hd_spy_gouraud,
    .draw_flat = hd_spy_flat,
    .draw_textured = hd_spy_textured,
    .draw_textured_flat = hd_spy_textured_flat,
};

static void
render_hd_fixture(
    struct RenderEnv* env,
    const struct HDFixture* fixture,
    const struct ToriDraw_HDMaterials* materials,
    struct ToriDraw_HDRenderStats* stats)
{
    struct ToriDraw_ViewPort viewport = test_viewport();
    struct ToriDraw_Camera camera = test_camera();
    struct ToriDraw_Position position = { .z = CAMERA_DISTANCE };
    int result;

    memset(env->pixels, 0, (size_t)VIEW_STRIDE * VIEW_HEIGHT * sizeof(*env->pixels));
    result = ToriDraw_RenderHD(
        fixture->handle, env->scene, &position, &viewport, &camera, env->pixels, materials,
        stats);
    CHECK(result == TORIDRAW_CULL_VISIBLE, "HD fixture render returned %d", result);
    CHECK(env->scene->tmp_face_order_count == FACE_COUNT, "HD fixture ordered %d/%d faces",
          env->scene->tmp_face_order_count, FACE_COUNT);
}

static void
render_hd_zbuffer_fixture(
    struct RenderEnv* env,
    const struct HDFixture* fixture,
    const struct ToriDraw_HDMaterials* materials,
    struct ToriDraw_HDRenderStats* stats)
{
    struct ToriDraw_ViewPort viewport = test_viewport();
    struct ToriDraw_Camera camera = test_camera();
    struct ToriDraw_Position position = { .z = CAMERA_DISTANCE };
    int result;

    memset(env->pixels, 0, (size_t)VIEW_STRIDE * VIEW_HEIGHT * sizeof(*env->pixels));
    result = ToriDraw_RenderHDZBuffered(
        fixture->handle, env->scene, &position, &viewport, &camera, env->pixels, materials,
        stats);
    CHECK(result == TORIDRAW_CULL_VISIBLE, "HD depth fixture render returned %d", result);
}

static void
check_hd_full_calls(const struct HDSpy* spy)
{
    CHECK(spy->total_calls == FACE_COUNT, "%s HD total calls %d", spy->name, spy->total_calls);
    for( int slot = 0; slot < TORIDRAW_RASTER_FACE_CLASS_COUNT; slot++ )
        CHECK(spy->calls[slot] == 1, "%s HD slot %d calls %d", spy->name, slot,
              spy->calls[slot]);
}

static void
init_hd_materials(
    struct ToriDraw_HDMaterial materials[HD_TEXTURE_COUNT],
    const struct RenderEnv* env)
{
    memset(materials, 0, HD_TEXTURE_COUNT * sizeof(*materials));
    materials[0].texels = env->texture_texels;
    materials[0].width = TEST_TEXTURE_WIDTH;
    materials[0].gate = TORIDRAW_HD_GATE_ALPHA;
    materials[0].clamp_s = 1;
    materials[0].modulate = 1;
    materials[0].texture_neutral = 128;
    materials[1].texels = env->texture_texels;
    materials[1].width = TEST_TEXTURE_WIDTH;
    materials[1].gate = TORIDRAW_HD_GATE_TRANS;
    materials[1].clamp_t = 1;
    materials[1].texture_neutral = 144;
}

static void
test_hd_routing(struct RenderEnv* env)
{
    struct HDFixture fixture;
    struct HDSpy full;
    struct HDSpy ignored;
    struct HDSpy shared;
    struct ToriDraw_HDMaterial materials[HD_TEXTURE_COUNT];
    struct ToriDraw_HDMaterials table = { materials, HD_TEXTURE_COUNT };
    struct ToriDraw_HDMaterials missing_table = { materials, 1 };
    struct ToriDraw_HDRenderStats stats;
    struct ToriDraw_RasterKernel full_kernel;
    struct ToriDraw_RasterKernel shared_kernel;
    struct ToriDraw_RasterKernel stock_head;

    printf("HD descriptors, solid fallback, domains and callback guard\n");
    ToriDraw_HDSetTuning(NULL);
    hd_fixture_init(&fixture);
    init_hd_materials(materials, env);

    hd_spy_init(&full, "hd-full", &fixture, env, materials);
    full_kernel = (struct ToriDraw_RasterKernel){
        .vtable = &hd_spy_vtable,
        .user_data = &full,
        .domains = TORIDRAW_RASTER_KERNEL_HD,
    };
    CHECK(ToriDraw_SceneSetRasterKernel(env->scene, &full_kernel), "bind HD full spy");
    render_hd_fixture(env, &fixture, &table, &stats);
    check_hd_full_calls(&full);
    CHECK(stats.drawn_untextured == 2 && stats.drawn_cylinder == 1 &&
              stats.drawn_plane == 1 && stats.with_facealpha == 1 &&
              stats.with_modulate == 1,
          "HD full routing stats");
    CHECK(count_nonzero_pixels(env) == 0, "HD spy callbacks unexpectedly drew pixels");

    hd_spy_init(&full, "hd-reentrant", &fixture, env, materials);
    full.attempt_recursive_render = true;
    render_hd_fixture(env, &fixture, &table, &stats);
    CHECK(full.total_calls == FACE_COUNT * 2, "reentrant HD total calls %d", full.total_calls);
    for( int slot = 0; slot < TORIDRAW_RASTER_FACE_CLASS_COUNT; slot++ )
        CHECK(full.calls[slot] == 2, "reentrant HD slot %d calls %d", slot, full.calls[slot]);
    CHECK(full.recursive_render_attempted && full.recursive_scratch_unchanged &&
              full.recursive_context_distinct,
          "same-scene HD recursion did not isolate and restore live scratch");
    CHECK(env->scene->render_context_depth == 0 &&
              env->scene->nested_render_contexts_used == 0,
          "recursive HD render leaked an active context");

    hd_spy_init(&full, "hd-zbuffer-reentrant", &fixture, env, materials);
    full.expect_depth = true;
    full.attempt_recursive_render = true;
    full.recursive_zbuffer = true;
    render_hd_zbuffer_fixture(env, &fixture, &table, &stats);
    CHECK(full.total_calls == FACE_COUNT * 2, "reentrant HD zbuffer calls %d", full.total_calls);
    CHECK(full.recursive_render_attempted && full.recursive_scratch_unchanged &&
              full.recursive_context_distinct,
          "same-scene HD zbuffer recursion shared live depth or projection scratch");
    CHECK(env->scene->render_context_depth == 0 &&
              env->scene->nested_render_contexts_used == 0,
          "recursive HD zbuffer render leaked an active context");

    hd_spy_init(&full, "hd-missing-material", &fixture, env, materials);
    full.missing_last_material = true;
    render_hd_fixture(env, &fixture, &missing_table, &stats);
    CHECK(full.total_calls == FACE_COUNT &&
              full.calls[TORIDRAW_RASTER_FACE_GOURAUD] == 1 &&
              full.calls[TORIDRAW_RASTER_FACE_FLAT] == 2 &&
              full.calls[TORIDRAW_RASTER_FACE_TEXTURED] == 1 &&
              full.calls[TORIDRAW_RASTER_FACE_TEXTURED_FLAT] == 0,
          "missing HD material did not route textured-flat to solid flat");
    CHECK(stats.fallback_no_texels == 1 && stats.drawn_untextured == 3,
          "missing HD material stats");
    CHECK(ToriDraw_SceneResetRasterKernel(env->scene), "reset HD full spy");

    hd_spy_init(&ignored, "hd-stock-only", &fixture, env, materials);
    hd_spy_init(&shared, "hd-shared", &fixture, env, materials);
    shared.attempt_mutation = true;
    shared_kernel = (struct ToriDraw_RasterKernel){
        .vtable = &hd_spy_vtable,
        .user_data = &shared,
        .domains = TORIDRAW_RASTER_KERNEL_STOCK | TORIDRAW_RASTER_KERNEL_HD,
    };
    stock_head = (struct ToriDraw_RasterKernel){
        .vtable = &hd_spy_vtable,
        .user_data = &ignored,
        .fallback = &shared_kernel,
        .domains = TORIDRAW_RASTER_KERNEL_STOCK,
    };
    CHECK(ToriDraw_SceneSetRasterKernel(env->scene, &stock_head), "bind HD mixed-domain chain");
    render_hd_fixture(env, &fixture, &table, &stats);
    CHECK(ignored.total_calls == 0, "HD pass called stock-only node");
    check_hd_full_calls(&shared);
    CHECK(shared.mutation_attempted && !shared.set_during_callback &&
              !shared.reset_during_callback,
          "HD callback mutation was not rejected");
    CHECK(ToriDraw_SceneGetRasterKernel(env->scene) == &stock_head,
          "HD callback mutation changed the binding");
    CHECK(ToriDraw_SceneResetRasterKernel(env->scene), "reset HD mixed-domain chain");

    hd_fixture_destroy(&fixture);
}

static void
test_hd_edge_contracts(struct RenderEnv* env)
{
    struct HDFixture fixture;
    struct HDSpy spy;
    struct ToriDraw_HDMaterial materials[HD_TEXTURE_COUNT];
    struct ToriDraw_HDMaterials table = { materials, HD_TEXTURE_COUNT };
    struct ToriDraw_HDRenderStats stats;
    struct ToriDraw_RasterKernel kernel;
    struct ToriDraw_TexMapping* saved_mappings;
    hsl16_t* saved_colours;
    faceint_t saved_p;
    uint8_t saved_render_type;

    printf("HD malformed material, colour, mapping and depth contracts\n");
    hd_fixture_init(&fixture);
    init_hd_materials(materials, env);
    hd_spy_init(&spy, "hd-edge", &fixture, env, materials);
    kernel = (struct ToriDraw_RasterKernel){
        .vtable = &hd_spy_vtable,
        .user_data = &spy,
        .domains = TORIDRAW_RASTER_KERNEL_HD,
    };
    CHECK(ToriDraw_SceneSetRasterKernel(env->scene, &kernel), "bind HD edge spy");

    materials[1].width = 32;
    hd_spy_init(&spy, "hd-invalid-width", &fixture, env, materials);
    spy.missing_last_material = true;
    render_hd_fixture(env, &fixture, &table, &stats);
    CHECK(spy.total_calls == FACE_COUNT &&
              spy.calls[TORIDRAW_RASTER_FACE_GOURAUD] == 1 &&
              spy.calls[TORIDRAW_RASTER_FACE_FLAT] == 2 &&
              spy.calls[TORIDRAW_RASTER_FACE_TEXTURED] == 1 &&
              spy.calls[TORIDRAW_RASTER_FACE_TEXTURED_FLAT] == 0,
          "invalid HD material width did not route through solid flat");
    CHECK(stats.fallback_no_texels == 1 && stats.drawn_untextured == 3 &&
              stats.drawn_cylinder == 1 && stats.drawn_plane == 0 &&
              stats.gate_alpha == 1 && stats.gate_trans == 0,
          "invalid HD material width stats");
    materials[1].width = TEST_TEXTURE_WIDTH;

    materials[1].gate = 99;
    hd_spy_init(&spy, "hd-invalid-gate", &fixture, env, materials);
    render_hd_fixture(env, &fixture, &table, &stats);
    check_hd_full_calls(&spy);
    CHECK(stats.gate_opaque == 1 && stats.gate_trans == 0 && stats.gate_alpha == 1,
          "invalid HD gate was not coerced to opaque");
    materials[1].gate = TORIDRAW_HD_GATE_TRANS;

    saved_colours = fixture.hd.base.face_colors_b;
    fixture.hd.base.face_colors_b = NULL;
    hd_spy_init(&spy, "hd-null-lit-colours", &fixture, env, materials);
    render_hd_fixture(env, &fixture, &table, &stats);
    CHECK(spy.total_calls == 0, "HD face with a null lit-colour array reached a callback");
    CHECK(stats.skipped_hidden == FACE_COUNT && stats.drawn_untextured == 0 &&
              stats.drawn_plane == 0 && stats.drawn_cylinder == 0 &&
              stats.gate_opaque == 0 && stats.gate_trans == 0 && stats.gate_alpha == 0,
          "null HD lit-colour stats");
    fixture.hd.base.face_colors_b = saved_colours;

    saved_mappings = fixture.hd.texture_mappings;
    fixture.hd.texture_mappings = NULL;
    hd_spy_init(&spy, "hd-missing-mapping-valid-frame", &fixture, env, materials);
    spy.first_mapping_fallback = true;
    render_hd_fixture(env, &fixture, &table, &stats);
    check_hd_full_calls(&spy);
    CHECK(stats.fallback_no_mapping == 1 && stats.drawn_plane == 2 &&
              stats.drawn_cylinder == 0,
          "valid HD frame fallback stats");

    saved_p = fixture.texture_p[0];
    fixture.texture_p[0] = VERTEX_COUNT;
    hd_spy_init(&spy, "hd-missing-mapping-invalid-frame", &fixture, env, materials);
    spy.first_mapping_fallback = true;
    render_hd_fixture(env, &fixture, &table, &stats);
    CHECK(spy.total_calls == FACE_COUNT - 1 && spy.face_calls[2] == 0 &&
              spy.calls[TORIDRAW_RASTER_FACE_GOURAUD] == 1 &&
              spy.calls[TORIDRAW_RASTER_FACE_FLAT] == 1 &&
              spy.calls[TORIDRAW_RASTER_FACE_TEXTURED] == 0 &&
              spy.calls[TORIDRAW_RASTER_FACE_TEXTURED_FLAT] == 1,
          "invalid HD fallback frame reached a callback");
    CHECK(stats.fallback_no_mapping == 1 && stats.skipped_hidden == 1 &&
              stats.drawn_plane == 1 && stats.drawn_cylinder == 0 &&
              stats.gate_alpha == 1 && stats.gate_trans == 1 &&
              stats.with_facealpha == 1 && stats.with_modulate == 1,
          "invalid HD fallback frame stats");
    fixture.texture_p[0] = saved_p;
    fixture.hd.texture_mappings = saved_mappings;

    saved_render_type = fixture.render_type[0];
    fixture.render_type[0] = 255;
    hd_spy_init(&spy, "hd-malformed-render-type", &fixture, env, materials);
    spy.first_malformed_render_type = true;
    render_hd_fixture(env, &fixture, &table, &stats);
    check_hd_full_calls(&spy);
    CHECK(stats.fallback_no_mapping == 0 && stats.drawn_plane == 2 &&
              stats.drawn_cylinder == 0,
          "malformed HD render type was not coerced to plane");
    fixture.render_type[0] = saved_render_type;

    hd_spy_init(&spy, "hd-depth", &fixture, env, materials);
    spy.expect_depth = true;
    render_hd_zbuffer_fixture(env, &fixture, &table, &stats);
    check_hd_full_calls(&spy);
    CHECK(stats.drawn_untextured == 2 && stats.drawn_plane == 1 &&
              stats.drawn_cylinder == 1,
          "HD depth callback stats");
    CHECK(count_nonzero_pixels(env) == 0, "HD depth spy callbacks unexpectedly drew pixels");

    hd_spy_init(&spy, "hd-sorted-after-depth", &fixture, env, materials);
    render_hd_fixture(env, &fixture, &table, &stats);
    check_hd_full_calls(&spy);

    CHECK(ToriDraw_SceneResetRasterKernel(env->scene), "reset HD edge spy");
    hd_fixture_destroy(&fixture);
}

static void
test_explicit_hd_roots(struct RenderEnv* env)
{
    const struct ToriDraw_RasterKernel* branching = ToriDraw_RasterKernelGetHDBranching();
    const struct ToriDraw_RasterKernel* scanline = ToriDraw_RasterKernelGetHDScanline();
    struct HDFixture fixture;
    struct ToriDraw_HDMaterial materials[HD_TEXTURE_COUNT];
    struct ToriDraw_HDMaterials table = { materials, HD_TEXTURE_COUNT };
    struct ToriDraw_HDRenderStats stats;
    toripixel_t* reference;
    bool const original_scanline = ToriDraw_RasterGetScanline();
    size_t const image_bytes =
        (size_t)VIEW_STRIDE * VIEW_HEIGHT * sizeof(*env->pixels);

    printf("explicit HD roots are complete and pin their family at runtime\n");
    CHECK(branching && scanline && branching != scanline, "HD roots are null or aliased");
    if( !branching || !scanline )
        return;
    CHECK(branching->domains == TORIDRAW_RASTER_KERNEL_HD &&
              scanline->domains == TORIDRAW_RASTER_KERNEL_HD,
          "HD roots expose the wrong domains");
    CHECK(branching->vtable && scanline->vtable && branching->vtable->draw_gouraud &&
              branching->vtable->draw_flat && branching->vtable->draw_textured &&
              branching->vtable->draw_textured_flat && scanline->vtable->draw_gouraud &&
              scanline->vtable->draw_flat && scanline->vtable->draw_textured &&
              scanline->vtable->draw_textured_flat,
          "HD root vtable is incomplete");
    if( !branching->vtable || !scanline->vtable )
        return;
    CHECK(branching->vtable->draw_gouraud != scanline->vtable->draw_gouraud &&
              branching->vtable->draw_flat != scanline->vtable->draw_flat,
          "HD solid roots do not select distinct families");
    CHECK(branching->vtable->draw_textured == scanline->vtable->draw_textured &&
              branching->vtable->draw_textured_flat == scanline->vtable->draw_textured_flat,
          "HD roots unexpectedly duplicate the shared textured family");

    reference = malloc(image_bytes);
    CHECK(reference != NULL, "HD root image allocation");
    if( !reference )
        return;
    hd_fixture_init(&fixture);
    init_hd_materials(materials, env);

    CHECK(ToriDraw_SceneSetRasterKernel(env->scene, branching), "bind explicit HD branching root");
    ToriDraw_RasterSetScanline(false);
    render_hd_fixture(env, &fixture, &table, &stats);
    CHECK(count_nonzero_pixels(env) > 0, "explicit HD branching root drew no pixels");
    memcpy(reference, env->pixels, image_bytes);
    ToriDraw_RasterSetScanline(true);
    render_hd_fixture(env, &fixture, &table, &stats);
    CHECK(memcmp(reference, env->pixels, image_bytes) == 0,
          "global selector overrode an explicit HD branching root");

    CHECK(ToriDraw_SceneSetRasterKernel(env->scene, scanline), "bind explicit HD scanline root");
    ToriDraw_RasterSetScanline(true);
    render_hd_fixture(env, &fixture, &table, &stats);
    CHECK(count_nonzero_pixels(env) > 0, "explicit HD scanline root drew no pixels");
    memcpy(reference, env->pixels, image_bytes);
    ToriDraw_RasterSetScanline(false);
    render_hd_fixture(env, &fixture, &table, &stats);
    CHECK(memcmp(reference, env->pixels, image_bytes) == 0,
          "global selector overrode an explicit HD scanline root");

    CHECK(ToriDraw_SceneResetRasterKernel(env->scene), "reset explicit HD root");
    ToriDraw_RasterSetScanline(original_scanline);
    hd_fixture_destroy(&fixture);
    free(reference);
}

#endif /* !TORIDRAW_PIXEL16 */

#ifdef TORIDRAW_PIXEL16
static void
test_pixel16_hd_unsupported(const struct Fixture* fixture, struct RenderEnv* env)
{
    const struct ToriDraw_RasterKernel* branching = ToriDraw_RasterKernelGetHDBranching();
    const struct ToriDraw_RasterKernel* scanline = ToriDraw_RasterKernelGetHDScanline();
    struct ToriDraw_ViewPort viewport = test_viewport();
    struct ToriDraw_Camera camera = test_camera();
    struct ToriDraw_Position position = { .z = CAMERA_DISTANCE };
    struct ToriDraw_HDRenderStats stats;
    struct ToriDraw_HDRenderStats zero_stats = {0};
    int result;

    printf("Pixel16 HD entry points report unsupported and clear stats\n");
    CHECK(branching && scanline && branching != scanline, "Pixel16 HD roots are null or aliased");
    if( branching && scanline )
    {
        CHECK(branching->domains == TORIDRAW_RASTER_KERNEL_HD &&
                  scanline->domains == TORIDRAW_RASTER_KERNEL_HD,
              "Pixel16 HD root domains");
        CHECK(branching->vtable && scanline->vtable && branching->vtable->draw_gouraud &&
                  branching->vtable->draw_flat && branching->vtable->draw_textured &&
                  branching->vtable->draw_textured_flat && scanline->vtable->draw_gouraud &&
                  scanline->vtable->draw_flat && scanline->vtable->draw_textured &&
                  scanline->vtable->draw_textured_flat,
              "Pixel16 HD root vtable is incomplete");
    }

    memset(&stats, 0xA5, sizeof(stats));
    result = ToriDraw_RenderHD(
        fixture->handle, env->scene, &position, &viewport, &camera, env->pixels, NULL, &stats);
    CHECK(result == TORIDRAW_CULL_ERROR, "Pixel16 RenderHD returned %d", result);
    CHECK(memcmp(&stats, &zero_stats, sizeof(stats)) == 0,
          "Pixel16 RenderHD did not zero its stats");

    memset(&stats, 0xA5, sizeof(stats));
    result = ToriDraw_RenderHDZBuffered(
        fixture->handle, env->scene, &position, &viewport, &camera, env->pixels, NULL, &stats);
    CHECK(result == TORIDRAW_CULL_ERROR, "Pixel16 RenderHDZBuffered returned %d", result);
    CHECK(memcmp(&stats, &zero_stats, sizeof(stats)) == 0,
          "Pixel16 RenderHDZBuffered did not zero its stats");
}
#endif

int
main(void)
{
    struct Fixture fixture;
    struct RenderEnv first = {0};
    struct RenderEnv second = {0};

    ToriDraw_Init();
    fixture_init(&fixture);
    if( !render_env_init(&first, 0x00C06020) || !render_env_init(&second, 0x002060C0) )
    {
        render_env_destroy(&first);
        render_env_destroy(&second);
        fixture_destroy(&fixture);
        return 1;
    }

    test_four_slots_and_callback_guard(&fixture, &first);
    test_same_scene_reentrant_stock(&fixture, &first);
    test_pre_dispatch_skips(&fixture, &first);
    test_live_chain_mutation_recovery(&fixture, &first);
    test_sparse_fallback_and_noop(&fixture, &first);
    test_two_scene_independence(&fixture, &first, &second);
    test_incompatible_domain(&fixture, &first);
    test_invalid_set_rejection(&fixture, &first);
    test_reset_and_process_terminals(&fixture, &first);
#ifndef TORIDRAW_PIXEL16
    test_hd_routing(&first);
    test_hd_edge_contracts(&first);
    test_explicit_hd_roots(&first);
#else
    test_pixel16_hd_unsupported(&fixture, &first);
#endif

    render_env_destroy(&second);
    render_env_destroy(&first);
    fixture_destroy(&fixture);

    if( failures )
    {
        fprintf(stderr, "toridraw_raster_kernel_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("toridraw_raster_kernel_test: all checks passed\n");
    return 0;
}
