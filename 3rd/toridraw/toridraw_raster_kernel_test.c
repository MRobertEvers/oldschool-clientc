/*
 * Public typed raster-kernel routing regression.
 *
 * This stays on the public surface: complete SD/HD callback tables, explicit
 * per-call rendering, and legacy-wrapper parity.
 */

#include "toridraw.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(TORIDRAW_PIXEL16) && !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

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
#define SD_FACE_COUNT 4
#define SD_VERTEX_COUNT (SD_FACE_COUNT * 3)

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

struct RenderEnv
{
    struct ToriDraw_Scene* scene;
    toripixel_t* pixels;
    const int* texture_texels;
};

struct SDFixture
{
    struct ToriDraw_Model model;
    struct ToriDraw_ModelHandle handle;
    vertexint_t vertex_x[SD_VERTEX_COUNT];
    vertexint_t vertex_y[SD_VERTEX_COUNT];
    vertexint_t vertex_z[SD_VERTEX_COUNT];
    faceint_t face_a[SD_FACE_COUNT];
    faceint_t face_b[SD_FACE_COUNT];
    faceint_t face_c[SD_FACE_COUNT];
    hsl16_t shade_a[SD_FACE_COUNT];
    hsl16_t shade_b[SD_FACE_COUNT];
    hsl16_t shade_c[SD_FACE_COUNT];
    alphaint_t alpha[SD_FACE_COUNT];
    int face_info[SD_FACE_COUNT];
    faceint_t texture[SD_FACE_COUNT];
};

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

static struct ToriDraw_Texture*
make_test_texture(void)
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
        texture->texels[i] = 0x00C06020;
    texture->width = TEST_TEXTURE_WIDTH;
    texture->height = TEST_TEXTURE_WIDTH;
    texture->opaque = false;
    return texture;
}

static bool
render_env_init(struct RenderEnv* env)
{
    struct ToriDraw_Texture* texture;

    memset(env, 0, sizeof(*env));
    env->scene = ToriDraw_SceneNew(TORIDRAW_SCENE_FULL, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    env->pixels = calloc((size_t)VIEW_STRIDE * VIEW_HEIGHT, sizeof(*env->pixels));
    texture = make_test_texture();
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

static void
clear_pixels(struct RenderEnv* env)
{
    memset(env->pixels, 0, (size_t)VIEW_STRIDE * VIEW_HEIGHT * sizeof(*env->pixels));
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
sd_fixture_init(struct SDFixture* fixture)
{
    static const int centre_x[SD_FACE_COUNT] = { -120, -40, 40, 120 };

    memset(fixture, 0, sizeof(*fixture));
    fixture->model.vertex_count = SD_VERTEX_COUNT;
    fixture->model.face_count = SD_FACE_COUNT;
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
    fixture->model.textured_face_count = 1;

    for( int face = 0; face < SD_FACE_COUNT; face++ )
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
    }

    fixture->shade_a[0] = 0x1234;
    fixture->shade_b[0] = 0x1235;
    fixture->shade_c[0] = 0x1236;
    fixture->alpha[0] = 17;

    fixture->shade_a[1] = 0x2345;
    fixture->shade_b[1] = 0x3456;
    fixture->shade_c[1] = TORIDRAWHSL16_FLAT;

    fixture->shade_a[2] = 40;
    fixture->shade_b[2] = 60;
    fixture->shade_c[2] = 80;
    fixture->texture[2] = TEST_TEXTURE_ID;

    fixture->shade_a[3] = 70;
    fixture->shade_b[3] = 90;
    fixture->shade_c[3] = TORIDRAWHSL16_FLAT;
    fixture->texture[3] = TEST_TEXTURE_ID;

    fixture->handle.kind = TORIDRAWMK_MODEL;
    fixture->handle.u.model.model = &fixture->model;
    ToriDraw_ModelSetBoundsCylinder(&fixture->model);
}

static void
sd_fixture_destroy(struct SDFixture* fixture)
{
    free(fixture->model.bounds_cylinder);
    fixture->model.bounds_cylinder = NULL;
}

static void
check_sd_builtin_kernel(
    const struct ToriDraw_RasterKernelSD* kernel,
    uint32_t expected_flags,
    const char* label)
{
    CHECK(kernel != NULL, "%s built-in kernel is null", label);
    if( !kernel )
        return;
    CHECK(kernel->vtable != NULL, "%s vtable is null", label);
    if( !kernel->vtable )
        return;
    for( int slot = 0; slot < TORIDRAW_RASTER_FACE_SD_CLASS_COUNT; slot++ )
        CHECK(kernel->vtable->draw[slot] != NULL, "%s slot %d is null", label, slot);
    CHECK(kernel->flags == expected_flags, "%s flags 0x%x, expected 0x%x", label,
          kernel->flags, expected_flags);
}

static void
check_hd_builtin_kernel(
    const struct ToriDraw_RasterKernelHD* kernel,
    uint32_t expected_flags,
    const char* label)
{
    CHECK(kernel != NULL, "%s built-in kernel is null", label);
    if( !kernel )
        return;
    CHECK(kernel->vtable != NULL, "%s vtable is null", label);
    if( !kernel->vtable )
        return;
    for( int slot = 0; slot < TORIDRAW_RASTER_FACE_HD_CLASS_COUNT; slot++ )
        CHECK(kernel->vtable->draw[slot] != NULL, "%s slot %d is null", label, slot);
    CHECK(kernel->flags == expected_flags, "%s flags 0x%x, expected 0x%x", label,
          kernel->flags, expected_flags);
}

static void
test_typed_builtin_kernels(void)
{
    const struct ToriDraw_RasterKernelSD* sd_branching =
        ToriDraw_RasterKernelSDGetBranching();
    const struct ToriDraw_RasterKernelSD* sd_scanline =
        ToriDraw_RasterKernelSDGetScanline();
    const struct ToriDraw_RasterKernelSD* sd_smooth_branching =
        ToriDraw_RasterKernelSDGetSmoothBranching();
    const struct ToriDraw_RasterKernelSD* sd_smooth_scanline =
        ToriDraw_RasterKernelSDGetSmoothScanline();
    const struct ToriDraw_RasterKernelSD* sd_zbuffered =
        ToriDraw_RasterKernelSDGetZBuffered();
    const struct ToriDraw_RasterKernelSD* sd_smooth_zbuffered =
        ToriDraw_RasterKernelSDGetSmoothZBuffered();
    const struct ToriDraw_RasterKernelHD* hd_branching =
        ToriDraw_RasterKernelHDGetBranching();
    const struct ToriDraw_RasterKernelHD* hd_scanline =
        ToriDraw_RasterKernelHDGetScanline();
    const struct ToriDraw_RasterKernelHD* hd_zbuffered =
        ToriDraw_RasterKernelHDGetZBuffered();

    CHECK(sizeof(struct ToriDraw_RasterKernelSDVTable) ==
              4 * sizeof(ToriDraw_RasterKernelSDFaceFn),
          "SD vtable is not four typed slots");
    CHECK(sizeof(struct ToriDraw_RasterKernelHDVTable) ==
              6 * sizeof(ToriDraw_RasterKernelHDFaceFn),
          "HD vtable is not six typed slots");
    check_sd_builtin_kernel(sd_branching, TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
                            "SD branching");
    check_sd_builtin_kernel(sd_scanline, TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
                            "SD scanline");
    check_sd_builtin_kernel(sd_smooth_branching,
                            TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
                            "SD smooth branching");
    check_sd_builtin_kernel(sd_smooth_scanline,
                            TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
                            "SD smooth scanline");
    check_sd_builtin_kernel(sd_zbuffered, TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER,
                            "SD z-buffered");
    check_sd_builtin_kernel(sd_smooth_zbuffered,
                            TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER,
                            "SD smooth z-buffered");
    check_hd_builtin_kernel(hd_branching, TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
                            "HD branching");
    check_hd_builtin_kernel(hd_scanline, TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
                            "HD scanline");
    check_hd_builtin_kernel(hd_zbuffered, TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER,
                            "HD z-buffered");
    CHECK(sd_branching != sd_scanline && sd_branching != sd_smooth_branching &&
              sd_scanline != sd_smooth_scanline,
          "SD built-in kernels are unexpectedly aliased");
    CHECK(hd_branching != hd_scanline,
          "HD built-in kernels are unexpectedly aliased");
#ifdef TORIDRAW_PIXEL16
    CHECK(sizeof(toripixel_t) == 2, "Pixel16 toripixel_t is %zu bytes", sizeof(toripixel_t));
#else
    CHECK(sizeof(toripixel_t) == 4, "Pixel32 toripixel_t is %zu bytes", sizeof(toripixel_t));
#endif
}

struct SDSpy
{
    const struct SDFixture* fixture;
    const struct RenderEnv* env;
    int calls[TORIDRAW_RASTER_FACE_SD_CLASS_COUNT];
    int face_calls[SD_FACE_COUNT];
    int call_order[SD_FACE_COUNT];
    int total_calls;
    uint32_t expected_flags;
};

static enum ToriDraw_RasterFaceClassSD
sd_expected_class(int face)
{
#ifdef TORIDRAW_PIXEL16
    static const enum ToriDraw_RasterFaceClassSD expected[SD_FACE_COUNT] = {
        TORIDRAW_RASTER_FACE_SD_GOURAUD,
        TORIDRAW_RASTER_FACE_SD_FLAT,
        TORIDRAW_RASTER_FACE_SD_GOURAUD,
        TORIDRAW_RASTER_FACE_SD_FLAT,
    };
#else
    static const enum ToriDraw_RasterFaceClassSD expected[SD_FACE_COUNT] = {
        TORIDRAW_RASTER_FACE_SD_GOURAUD,
        TORIDRAW_RASTER_FACE_SD_FLAT,
        TORIDRAW_RASTER_FACE_SD_TEXTURED,
        TORIDRAW_RASTER_FACE_SD_TEXTURED_FLAT,
    };
#endif
    return expected[face];
}

static void
sd_verify_target(const struct SDSpy* spy, const struct ToriDraw_RasterTarget* target)
{
    CHECK(target != NULL, "SD callback received a null target");
    if( !target )
        return;
    CHECK(target->pixel_buffer == spy->env->pixels + CLIP_LEFT + CLIP_TOP * VIEW_STRIDE,
          "SD framebuffer was not clip-rebased");
    CHECK(target->width == CLIP_RIGHT - CLIP_LEFT &&
              target->height == CLIP_BOTTOM - CLIP_TOP && target->stride == VIEW_STRIDE,
          "SD target geometry %dx%d/%d", target->width, target->height, target->stride);
    CHECK(target->clip_origin_x == CLIP_LEFT && target->clip_origin_y == CLIP_TOP,
          "SD clip origin (%d,%d)", target->clip_origin_x, target->clip_origin_y);
    CHECK(target->vertex_count == SD_VERTEX_COUNT, "SD vertex count %d", target->vertex_count);
    CHECK(target->screen_vertices_x == spy->env->scene->screen_vertices_x &&
              target->orthographic_vertices_x == spy->env->scene->orthographic_vertices_x,
          "SD projection arrays differ from scene scratch");
    CHECK(target->posed_vertices_x == spy->fixture->vertex_x &&
              target->bind_vertices_x == spy->fixture->vertex_x,
          "SD model arrays were not published");
    if( spy->expected_flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER )
        CHECK(target->depth_test && target->zbuffer != NULL,
              "SD flags 0x%x did not publish a depth target", spy->expected_flags);
    else
        CHECK(!target->depth_test && target->zbuffer == NULL,
              "SD flags 0x%x unexpectedly depth-tested", spy->expected_flags);
}

static void
sd_verify_face(const struct SDSpy* spy, const struct ToriDraw_RasterFaceSD* face)
{
    int index;

    CHECK(face != NULL, "SD callback received a null face");
    if( !face )
        return;
    index = face->face_index;
    CHECK(index >= 0 && index < SD_FACE_COUNT, "SD face index %d", index);
    if( index < 0 || index >= SD_FACE_COUNT )
        return;
    CHECK(face->face_class == sd_expected_class(index), "SD face %d class %d", index,
          (int)face->face_class);
    CHECK(face->vertex[0] == spy->fixture->face_a[index] &&
              face->vertex[1] == spy->fixture->face_b[index] &&
              face->vertex[2] == spy->fixture->face_c[index],
          "SD face %d vertices", index);
    CHECK(!face->near_clipped, "SD face %d unexpectedly near-clipped", index);

    if( index == 0 )
        CHECK(face->shade[0] == 0x1234 && face->shade[1] == 0x1235 &&
                  face->shade[2] == 0x1236 && face->opacity == 238,
              "SD Gouraud descriptor");
    else if( index == 1 )
        CHECK(face->shade[0] == 0x2345 && face->shade[1] == 0x2345 &&
                  face->shade[2] == 0x2345 && face->opacity == 255,
              "SD flat descriptor");
    else
    {
        int const shade_a = index == 2 ? 40 : 70;
        int const shade_b = index == 2 ? 60 : 70;
        int const shade_c = index == 2 ? 80 : 70;

        CHECK(face->shade[0] == shade_a && face->shade[1] == shade_b &&
                  face->shade[2] == shade_c && face->opacity == 255,
              "SD face %d shade/opacity", index);
#ifndef TORIDRAW_PIXEL16
        CHECK(face->texture.texture_id == TEST_TEXTURE_ID &&
                  face->texture.texels == spy->env->texture_texels &&
                  face->texture.width == TEST_TEXTURE_WIDTH &&
                  face->texture.height == TEST_TEXTURE_WIDTH,
              "SD face %d texture identity", index);
        CHECK(face->texture.gate == TORIDRAW_RASTER_TEXTURE_COLOR_KEY &&
                  face->texture.render_type == 0 && face->texture.frame_fallback,
              "SD face %d texture policy", index);
        CHECK(face->texture.frame.p == face->vertex[0] &&
                  face->texture.frame.m == face->vertex[1] &&
                  face->texture.frame.n == face->vertex[2],
              "SD face %d fallback frame", index);
#endif
    }
}

static void
sd_spy_record(
    struct SDSpy* spy,
    enum ToriDraw_RasterFaceClassSD slot,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceSD* face)
{
    CHECK(spy != NULL, "SD slot %d received null user data", (int)slot);
    if( !spy )
        return;
    CHECK(face && face->face_class == slot, "SD slot %d received class %d", (int)slot,
          face ? (int)face->face_class : -1);
    spy->calls[slot]++;
    if( spy->total_calls < SD_FACE_COUNT )
        spy->call_order[spy->total_calls] = face ? face->face_index : -1;
    spy->total_calls++;
    if( face && face->face_index >= 0 && face->face_index < SD_FACE_COUNT )
        spy->face_calls[face->face_index]++;
    sd_verify_target(spy, target);
    sd_verify_face(spy, face);
}

#define DEFINE_SD_CALLBACK(name, slot)                                                           \
    static void name(void* user_data, const struct ToriDraw_RasterTarget* target,                 \
                     const struct ToriDraw_RasterFaceSD* face)                                    \
    {                                                                                              \
        sd_spy_record((struct SDSpy*)user_data, slot, target, face);                               \
    }

DEFINE_SD_CALLBACK(sd_spy_gouraud, TORIDRAW_RASTER_FACE_SD_GOURAUD)
DEFINE_SD_CALLBACK(sd_spy_flat, TORIDRAW_RASTER_FACE_SD_FLAT)
DEFINE_SD_CALLBACK(sd_spy_textured, TORIDRAW_RASTER_FACE_SD_TEXTURED)
DEFINE_SD_CALLBACK(sd_spy_textured_flat, TORIDRAW_RASTER_FACE_SD_TEXTURED_FLAT)

static const struct ToriDraw_RasterKernelSDVTable sd_full_vtable = {
    .draw = {
        [TORIDRAW_RASTER_FACE_SD_GOURAUD] = sd_spy_gouraud,
        [TORIDRAW_RASTER_FACE_SD_FLAT] = sd_spy_flat,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED] = sd_spy_textured,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED_FLAT] = sd_spy_textured_flat,
    },
};

static void
check_sd_full_calls(const struct SDSpy* spy)
{
#ifdef TORIDRAW_PIXEL16
    static const int expected[TORIDRAW_RASTER_FACE_SD_CLASS_COUNT] = { 2, 2, 0, 0 };
#else
    static const int expected[TORIDRAW_RASTER_FACE_SD_CLASS_COUNT] = { 1, 1, 1, 1 };
#endif

    CHECK(spy->total_calls == SD_FACE_COUNT, "SD total calls %d", spy->total_calls);
    for( int slot = 0; slot < TORIDRAW_RASTER_FACE_SD_CLASS_COUNT; slot++ )
        CHECK(spy->calls[slot] == expected[slot], "SD slot %d calls %d", slot,
              spy->calls[slot]);
    for( int face = 0; face < SD_FACE_COUNT; face++ )
        CHECK(spy->face_calls[face] == 1, "SD face %d calls %d", face,
              spy->face_calls[face]);
}

static void
sd_spy_reset(struct SDSpy* spy, uint32_t expected_flags)
{
    memset(spy->calls, 0, sizeof(spy->calls));
    memset(spy->face_calls, 0, sizeof(spy->face_calls));
    for( int i = 0; i < SD_FACE_COUNT; i++ )
        spy->call_order[i] = -1;
    spy->total_calls = 0;
    spy->expected_flags = expected_flags;
}

static void
check_sd_model_order(const struct SDSpy* spy, const char* label)
{
    for( int face = 0; face < SD_FACE_COUNT; face++ )
        CHECK(spy->call_order[face] == face, "%s call %d used face %d", label, face,
              spy->call_order[face]);
}

static void
check_sd_sorted_order(
    const struct SDSpy* spy,
    const struct ToriDraw_Scene* scene,
    const char* label)
{
    CHECK(scene->tmp_face_order_count == SD_FACE_COUNT, "%s sorted count %d", label,
          scene->tmp_face_order_count);
    for( int i = 0; i < SD_FACE_COUNT && i < scene->tmp_face_order_count; i++ )
        CHECK(spy->call_order[i] == scene->tmp_face_order[i],
              "%s call %d used face %d, sorted face %d", label, i, spy->call_order[i],
              scene->tmp_face_order[i]);
}

static int
project_and_sort_sd(
    const struct SDFixture* fixture,
    struct RenderEnv* env,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* viewport,
    struct ToriDraw_Camera* camera)
{
    int result = ToriDraw_RenderModel1Project(
        fixture->handle, env->scene, position, viewport, camera);

    CHECK(result == TORIDRAW_CULL_VISIBLE, "SD projection returned %d", result);
    if( result == TORIDRAW_CULL_VISIBLE )
        CHECK(ToriDraw_RenderModel2SortFaces(fixture->handle, env->scene) == SD_FACE_COUNT,
              "SD sort count");
    return result;
}

static void
test_sd_direct_apis(const struct SDFixture* fixture, struct RenderEnv* env)
{
    struct ToriDraw_ViewPort viewport = test_viewport();
    struct ToriDraw_Camera camera = test_camera();
    struct ToriDraw_Position position = { .z = CAMERA_DISTANCE };
    struct SDSpy spy = { .fixture = fixture, .env = env };
    const struct ToriDraw_RasterKernelSD full_kernel = {
        .vtable = &sd_full_vtable,
        .user_data = &spy,
        .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
    };
    const struct ToriDraw_RasterKernelSD none_kernel = {
        .vtable = &sd_full_vtable,
        .user_data = &spy,
        .flags = TORIDRAW_RASTER_KERNEL_FLAG_NONE,
    };
#ifndef TORIDRAW_PIXEL16
    const struct ToriDraw_RasterKernelSD z_kernel = {
        .vtable = &sd_full_vtable,
        .user_data = &spy,
        .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER,
    };
    const struct ToriDraw_RasterKernelSD sorted_z_kernel = {
        .vtable = &sd_full_vtable,
        .user_data = &spy,
        .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING |
                 TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER,
    };
#endif
    int result;

    sd_spy_reset(&spy, full_kernel.flags);
    clear_pixels(env);
    result = ToriDraw_RenderModelWithRasterKernel(
        fixture->handle, env->scene, &position, &viewport, &camera, env->pixels, &full_kernel);
    CHECK(result == TORIDRAW_CULL_VISIBLE, "direct SD render returned %d", result);
    check_sd_full_calls(&spy);
    check_sd_sorted_order(&spy, env->scene, "direct SD sorted painter");
    CHECK(count_nonzero_pixels(env) == 0, "SD spy callbacks drew pixels");

    sd_spy_reset(&spy, none_kernel.flags);
    clear_pixels(env);
    result = ToriDraw_RenderModelWithRasterKernel(
        fixture->handle, env->scene, &position, &viewport, &camera, env->pixels,
        &none_kernel);
    CHECK(result == TORIDRAW_CULL_VISIBLE, "direct SD model-order painter returned %d",
          result);
    check_sd_full_calls(&spy);
    check_sd_model_order(&spy, "direct SD model-order painter");
    CHECK(count_nonzero_pixels(env) == 0, "SD model-order painter callbacks drew pixels");

    sd_spy_reset(&spy, full_kernel.flags);
    clear_pixels(env);
    if( project_and_sort_sd(fixture, env, &position, &viewport, &camera) ==
        TORIDRAW_CULL_VISIBLE )
    {
        result = ToriDraw_RenderModel3RasterWithRasterKernel(
            env->scene, &viewport, &camera, env->pixels, &full_kernel);
        CHECK(result == TORIDRAW_CULL_VISIBLE, "direct SD phase3 returned %d", result);
        check_sd_full_calls(&spy);
        check_sd_sorted_order(&spy, env->scene, "direct SD phase3 sorted painter");
        CHECK(count_nonzero_pixels(env) == 0, "SD phase3 spy callbacks drew pixels");
    }

#ifndef TORIDRAW_PIXEL16
    sd_spy_reset(&spy, z_kernel.flags);
    clear_pixels(env);
    result = ToriDraw_RenderZBufferedWithRasterKernel(
        fixture->handle, env->scene, &position, &viewport, &camera, env->pixels, &z_kernel);
    CHECK(result == TORIDRAW_CULL_VISIBLE, "direct SD depth returned %d", result);
    check_sd_full_calls(&spy);
    check_sd_model_order(&spy, "direct SD explicit model-order depth");
    CHECK(count_nonzero_pixels(env) == 0, "SD depth spy callbacks drew pixels");

    sd_spy_reset(&spy, z_kernel.flags);
    clear_pixels(env);
    result = ToriDraw_RenderModelWithRasterKernel(
        fixture->handle, env->scene, &position, &viewport, &camera, env->pixels, &z_kernel);
    CHECK(result == TORIDRAW_CULL_VISIBLE, "generic SD depth returned %d", result);
    check_sd_full_calls(&spy);
    check_sd_model_order(&spy, "generic SD model-order depth");

    sd_spy_reset(&spy, sorted_z_kernel.flags);
    clear_pixels(env);
    result = ToriDraw_RenderModelWithRasterKernel(
        fixture->handle, env->scene, &position, &viewport, &camera, env->pixels,
        &sorted_z_kernel);
    CHECK(result == TORIDRAW_CULL_VISIBLE, "generic SD sorted depth returned %d", result);
    check_sd_full_calls(&spy);
    check_sd_sorted_order(&spy, env->scene, "generic SD sorted depth");

    sd_spy_reset(&spy, sorted_z_kernel.flags);
    clear_pixels(env);
    result = ToriDraw_RenderZBufferedWithRasterKernel(
        fixture->handle, env->scene, &position, &viewport, &camera, env->pixels,
        &sorted_z_kernel);
    CHECK(result == TORIDRAW_CULL_VISIBLE, "direct SD sorted depth returned %d", result);
    check_sd_full_calls(&spy);
    check_sd_sorted_order(&spy, env->scene, "direct SD explicit sorted depth");
    CHECK(count_nonzero_pixels(env) == 0, "SD sorted depth callbacks drew pixels");
#endif
}

static void
test_sd_legacy_parity(const struct SDFixture* fixture, struct RenderEnv* env)
{
    struct ToriDraw_ViewPort viewport = test_viewport();
    struct ToriDraw_Camera camera = test_camera();
    struct ToriDraw_Position position = { .z = CAMERA_DISTANCE };
    const struct ToriDraw_RasterKernelSD* branching =
        ToriDraw_RasterKernelSDGetBranching();
    const struct ToriDraw_RasterKernelSD* smooth =
        ToriDraw_RasterKernelSDGetSmoothBranching();
#ifndef TORIDRAW_PIXEL16
    const struct ToriDraw_RasterKernelSD* smooth_z =
        ToriDraw_RasterKernelSDGetSmoothZBuffered();
#endif
    size_t const image_bytes =
        (size_t)VIEW_STRIDE * VIEW_HEIGHT * sizeof(*env->pixels);
    toripixel_t* reference = malloc(image_bytes);
    int result;

    CHECK(reference != NULL, "SD parity image allocation");
    if( !reference )
        return;

    ToriDraw_RasterSetScanline(false);
    clear_pixels(env);
    ToriDraw_RenderModel(
        fixture->handle, env->scene, &position, &viewport, &camera, env->pixels);
    memcpy(reference, env->pixels, image_bytes);
    CHECK(count_nonzero_pixels(env) > 0, "legacy SD wrapper drew no pixels");

    ToriDraw_RasterSetScanline(true);
    clear_pixels(env);
    result = ToriDraw_RenderModelWithRasterKernel(
        fixture->handle, env->scene, &position, &viewport, &camera, env->pixels, branching);
    CHECK(result == TORIDRAW_CULL_VISIBLE, "explicit SD kernel returned %d", result);
    CHECK(memcmp(reference, env->pixels, image_bytes) == 0,
          "legacy SD wrapper differs from the branching built-in kernel");

    ToriDraw_RasterSetScanline(false);
    clear_pixels(env);
    if( project_and_sort_sd(fixture, env, &position, &viewport, &camera) ==
        TORIDRAW_CULL_VISIBLE )
        CHECK(ToriDraw_RenderModel3Raster(
                  env->scene, &viewport, &camera, env->pixels, true) ==
                  TORIDRAW_CULL_VISIBLE,
              "legacy smooth SD phase3");
    memcpy(reference, env->pixels, image_bytes);

    clear_pixels(env);
    if( project_and_sort_sd(fixture, env, &position, &viewport, &camera) ==
        TORIDRAW_CULL_VISIBLE )
        CHECK(ToriDraw_RenderModel3RasterWithRasterKernel(
                  env->scene, &viewport, &camera, env->pixels, smooth) ==
                  TORIDRAW_CULL_VISIBLE,
              "explicit smooth SD phase3");
    CHECK(memcmp(reference, env->pixels, image_bytes) == 0,
          "legacy smooth phase3 differs from the smooth built-in kernel");

#ifndef TORIDRAW_PIXEL16
    clear_pixels(env);
    result = ToriDraw_RenderZBuffered(
        fixture->handle, env->scene, &position, &viewport, &camera, env->pixels, true);
    CHECK(result == TORIDRAW_CULL_VISIBLE, "legacy smooth SD depth returned %d", result);
    memcpy(reference, env->pixels, image_bytes);

    clear_pixels(env);
    result = ToriDraw_RenderZBufferedWithRasterKernel(
        fixture->handle, env->scene, &position, &viewport, &camera, env->pixels, smooth_z);
    CHECK(result == TORIDRAW_CULL_VISIBLE, "explicit smooth SD depth returned %d", result);
    CHECK(memcmp(reference, env->pixels, image_bytes) == 0,
          "legacy smooth SD depth differs from the smooth built-in kernel");
#endif

    free(reference);
}

#ifndef TORIDRAW_PIXEL16

#define HD_FACE_COUNT 6
#define HD_VERTEX_COUNT (HD_FACE_COUNT * 3)
#define HD_TEXTURE_COUNT 4

struct HDFixture
{
    struct ToriDraw_ModelHD hd;
    struct ToriDraw_ModelHandle handle;
    vertexint_t vertex_x[HD_VERTEX_COUNT];
    vertexint_t vertex_y[HD_VERTEX_COUNT];
    vertexint_t vertex_z[HD_VERTEX_COUNT];
    faceint_t face_a[HD_FACE_COUNT];
    faceint_t face_b[HD_FACE_COUNT];
    faceint_t face_c[HD_FACE_COUNT];
    hsl16_t shade_a[HD_FACE_COUNT];
    hsl16_t shade_b[HD_FACE_COUNT];
    hsl16_t shade_c[HD_FACE_COUNT];
    hsl16_t authored_color[HD_FACE_COUNT];
    alphaint_t alpha[HD_FACE_COUNT];
    int face_info[HD_FACE_COUNT];
    faceint_t texture[HD_FACE_COUNT];
    faceint_t texture_coord[HD_FACE_COUNT];
    uint8_t render_type[HD_TEXTURE_COUNT];
    faceint_t texture_p[HD_TEXTURE_COUNT];
    faceint_t texture_m[HD_TEXTURE_COUNT];
    faceint_t texture_n[HD_TEXTURE_COUNT];
};

static void
hd_fixture_init(struct HDFixture* fixture)
{
    static const int centre_x[HD_FACE_COUNT] = { -125, -75, -25, 25, 75, 125 };
    struct ToriDraw_Model* model;

    memset(fixture, 0, sizeof(*fixture));
    model = &fixture->hd.base;
    model->vertex_count = HD_VERTEX_COUNT;
    model->face_count = HD_FACE_COUNT;
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

    for( int face = 0; face < HD_FACE_COUNT; face++ )
    {
        int const vertex = face * 3;
        int const x = centre_x[face];

        fixture->vertex_x[vertex + 0] = (vertexint_t)(x - 18);
        fixture->vertex_y[vertex + 0] = -25;
        fixture->vertex_x[vertex + 1] = (vertexint_t)(x + 18);
        fixture->vertex_y[vertex + 1] = -25;
        fixture->vertex_x[vertex + 2] = (vertexint_t)x;
        fixture->vertex_y[vertex + 2] = 25;
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

    for( int coord = 0; coord < HD_TEXTURE_COUNT; coord++ )
    {
        int const face = coord + 2;

        fixture->shade_a[face] = (hsl16_t)(30 + coord * 10);
        fixture->shade_b[face] = (hsl16_t)(40 + coord * 10);
        fixture->shade_c[face] = (hsl16_t)(50 + coord * 10);
        fixture->texture[face] = (faceint_t)coord;
        fixture->texture_coord[face] = (faceint_t)coord;
        fixture->render_type[coord] = (uint8_t)coord;
        fixture->texture_p[coord] = fixture->face_a[face];
        fixture->texture_m[coord] = fixture->face_b[face];
        fixture->texture_n[coord] = fixture->face_c[face];
    }
    fixture->alpha[2] = 64;

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
init_hd_materials(
    struct ToriDraw_HDMaterial materials[HD_TEXTURE_COUNT],
    const struct RenderEnv* env)
{
    memset(materials, 0, HD_TEXTURE_COUNT * sizeof(*materials));
    for( int i = 0; i < HD_TEXTURE_COUNT; i++ )
    {
        materials[i].texels = env->texture_texels;
        materials[i].width = TEST_TEXTURE_WIDTH;
        materials[i].gate = i % 3;
        materials[i].clamp_s = i & 1;
        materials[i].clamp_t = (i >> 1) & 1;
        materials[i].modulate = i == 3;
        materials[i].texture_neutral = 128;
    }
}

struct HDSpy
{
    const struct HDFixture* fixture;
    const struct RenderEnv* env;
    int calls[TORIDRAW_RASTER_FACE_HD_CLASS_COUNT];
    int face_calls[HD_FACE_COUNT];
    int call_order[HD_FACE_COUNT];
    int total_calls;
    uint32_t expected_flags;
};

static enum ToriDraw_RasterFaceClassHD
hd_expected_class(int face)
{
    static const enum ToriDraw_RasterFaceClassHD expected[HD_FACE_COUNT] = {
        TORIDRAW_RASTER_FACE_HD_GOURAUD,
        TORIDRAW_RASTER_FACE_HD_FLAT,
        TORIDRAW_RASTER_FACE_HD_PLANE,
        TORIDRAW_RASTER_FACE_HD_CYLINDER,
        TORIDRAW_RASTER_FACE_HD_CUBE,
        TORIDRAW_RASTER_FACE_HD_SPHERE,
    };
    return expected[face];
}

static void
hd_verify_target(const struct HDSpy* spy, const struct ToriDraw_RasterTarget* target)
{
    CHECK(target != NULL, "HD callback received a null target");
    if( !target )
        return;
    CHECK(target->pixel_buffer == spy->env->pixels + CLIP_LEFT + CLIP_TOP * VIEW_STRIDE,
          "HD framebuffer was not clip-rebased");
    CHECK(target->width == CLIP_RIGHT - CLIP_LEFT &&
              target->height == CLIP_BOTTOM - CLIP_TOP && target->stride == VIEW_STRIDE,
          "HD target geometry %dx%d/%d", target->width, target->height, target->stride);
    CHECK(target->vertex_count == HD_VERTEX_COUNT, "HD vertex count %d", target->vertex_count);
    CHECK(target->posed_vertices_x == spy->fixture->vertex_x &&
              target->bind_vertices_x == spy->fixture->vertex_x,
          "HD model arrays were not published");
    if( spy->expected_flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER )
        CHECK(target->depth_test && target->zbuffer != NULL,
              "HD flags 0x%x did not publish a depth target", spy->expected_flags);
    else
        CHECK(!target->depth_test && target->zbuffer == NULL,
              "HD flags 0x%x unexpectedly depth-tested", spy->expected_flags);
}

static void
hd_verify_face(const struct HDSpy* spy, const struct ToriDraw_RasterFaceHD* face)
{
    int index;

    CHECK(face != NULL, "HD callback received a null face");
    if( !face )
        return;
    index = face->face_index;
    CHECK(index >= 0 && index < HD_FACE_COUNT, "HD face index %d", index);
    if( index < 0 || index >= HD_FACE_COUNT )
        return;
    CHECK(face->face_class == hd_expected_class(index), "HD face %d class %d", index,
          (int)face->face_class);
    CHECK(face->vertex[0] == spy->fixture->face_a[index] &&
              face->vertex[1] == spy->fixture->face_b[index] &&
              face->vertex[2] == spy->fixture->face_c[index],
          "HD face %d vertices", index);
    CHECK(!face->near_clipped, "HD face %d unexpectedly near-clipped", index);
    if( index >= 2 )
    {
        int const coord = index - 2;

        CHECK(face->texture.texture_id == coord &&
                  face->texture.texels == spy->env->texture_texels &&
                  face->texture.width == TEST_TEXTURE_WIDTH &&
                  face->texture.height == TEST_TEXTURE_WIDTH,
              "HD face %d texture identity", index);
        CHECK(face->texture.render_type == (unsigned int)coord &&
                  !face->texture.frame_fallback,
              "HD face %d render type/fallback", index);
        if( coord == 0 )
            CHECK(face->texture.mapping.vertex_frame.p == spy->fixture->texture_p[coord] &&
                      face->texture.mapping.vertex_frame.m == spy->fixture->texture_m[coord] &&
                      face->texture.mapping.vertex_frame.n == spy->fixture->texture_n[coord],
                  "HD plane frame");
        else
            CHECK(face->texture.mapping.hd_mapping ==
                      &spy->fixture->hd.texture_mappings[coord],
                  "HD mapped face %d mapping pointer", index);
    }
}

static void
hd_spy_record(
    struct HDSpy* spy,
    enum ToriDraw_RasterFaceClassHD slot,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceHD* face)
{
    CHECK(spy != NULL, "HD slot %d received null user data", (int)slot);
    if( !spy )
        return;
    CHECK(face && face->face_class == slot, "HD slot %d received class %d", (int)slot,
          face ? (int)face->face_class : -1);
    spy->calls[slot]++;
    if( spy->total_calls < HD_FACE_COUNT )
        spy->call_order[spy->total_calls] = face ? face->face_index : -1;
    spy->total_calls++;
    if( face && face->face_index >= 0 && face->face_index < HD_FACE_COUNT )
        spy->face_calls[face->face_index]++;
    hd_verify_target(spy, target);
    hd_verify_face(spy, face);
}

#define DEFINE_HD_CALLBACK(name, slot)                                                           \
    static void name(void* user_data, const struct ToriDraw_RasterTarget* target,                 \
                     const struct ToriDraw_RasterFaceHD* face)                                    \
    {                                                                                              \
        hd_spy_record((struct HDSpy*)user_data, slot, target, face);                               \
    }

DEFINE_HD_CALLBACK(hd_spy_gouraud, TORIDRAW_RASTER_FACE_HD_GOURAUD)
DEFINE_HD_CALLBACK(hd_spy_flat, TORIDRAW_RASTER_FACE_HD_FLAT)
DEFINE_HD_CALLBACK(hd_spy_plane, TORIDRAW_RASTER_FACE_HD_PLANE)
DEFINE_HD_CALLBACK(hd_spy_cylinder, TORIDRAW_RASTER_FACE_HD_CYLINDER)
DEFINE_HD_CALLBACK(hd_spy_cube, TORIDRAW_RASTER_FACE_HD_CUBE)
DEFINE_HD_CALLBACK(hd_spy_sphere, TORIDRAW_RASTER_FACE_HD_SPHERE)

static const struct ToriDraw_RasterKernelHDVTable hd_full_vtable = {
    .draw = {
        [TORIDRAW_RASTER_FACE_HD_GOURAUD] = hd_spy_gouraud,
        [TORIDRAW_RASTER_FACE_HD_FLAT] = hd_spy_flat,
        [TORIDRAW_RASTER_FACE_HD_PLANE] = hd_spy_plane,
        [TORIDRAW_RASTER_FACE_HD_CYLINDER] = hd_spy_cylinder,
        [TORIDRAW_RASTER_FACE_HD_CUBE] = hd_spy_cube,
        [TORIDRAW_RASTER_FACE_HD_SPHERE] = hd_spy_sphere,
    },
};

static void
check_hd_full_calls(const struct HDSpy* spy)
{
    CHECK(spy->total_calls == HD_FACE_COUNT, "HD total calls %d", spy->total_calls);
    for( int slot = 0; slot < TORIDRAW_RASTER_FACE_HD_CLASS_COUNT; slot++ )
        CHECK(spy->calls[slot] == 1, "HD slot %d calls %d", slot, spy->calls[slot]);
    for( int face = 0; face < HD_FACE_COUNT; face++ )
        CHECK(spy->face_calls[face] == 1, "HD face %d calls %d", face,
              spy->face_calls[face]);
}

static void
hd_spy_reset(struct HDSpy* spy, uint32_t expected_flags)
{
    memset(spy->calls, 0, sizeof(spy->calls));
    memset(spy->face_calls, 0, sizeof(spy->face_calls));
    for( int i = 0; i < HD_FACE_COUNT; i++ )
        spy->call_order[i] = -1;
    spy->total_calls = 0;
    spy->expected_flags = expected_flags;
}

static void
check_hd_model_order(const struct HDSpy* spy, const char* label)
{
    for( int face = 0; face < HD_FACE_COUNT; face++ )
        CHECK(spy->call_order[face] == face, "%s call %d used face %d", label, face,
              spy->call_order[face]);
}

static void
check_hd_sorted_order(
    const struct HDSpy* spy,
    const struct ToriDraw_Scene* scene,
    const char* label)
{
    CHECK(scene->tmp_face_order_count == HD_FACE_COUNT, "%s sorted count %d", label,
          scene->tmp_face_order_count);
    for( int i = 0; i < HD_FACE_COUNT && i < scene->tmp_face_order_count; i++ )
        CHECK(spy->call_order[i] == scene->tmp_face_order[i],
              "%s call %d used face %d, sorted face %d", label, i, spy->call_order[i],
              scene->tmp_face_order[i]);
}

static void
check_hd_stats(const struct ToriDraw_HDRenderStats* stats)
{
    CHECK(stats->faces == HD_FACE_COUNT && stats->drawn_untextured == 2 &&
              stats->drawn_plane == 1 && stats->drawn_cylinder == 1 &&
              stats->drawn_cube == 1 && stats->drawn_sphere == 1,
          "HD routing stats %d/%d/%d/%d/%d/%d", stats->faces, stats->drawn_untextured,
          stats->drawn_plane, stats->drawn_cylinder, stats->drawn_cube,
          stats->drawn_sphere);
}

static void
test_hd_direct_apis(const struct HDFixture* fixture, struct RenderEnv* env)
{
    struct ToriDraw_ViewPort viewport = test_viewport();
    struct ToriDraw_Camera camera = test_camera();
    struct ToriDraw_Position position = { .z = CAMERA_DISTANCE };
    struct ToriDraw_HDMaterial materials[HD_TEXTURE_COUNT];
    struct ToriDraw_HDMaterials table = { materials, HD_TEXTURE_COUNT };
    struct ToriDraw_HDRenderStats stats;
    struct HDSpy spy = { .fixture = fixture, .env = env };
    const struct ToriDraw_RasterKernelHD full_kernel = {
        .vtable = &hd_full_vtable,
        .user_data = &spy,
        .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
    };
    const struct ToriDraw_RasterKernelHD none_kernel = {
        .vtable = &hd_full_vtable,
        .user_data = &spy,
        .flags = TORIDRAW_RASTER_KERNEL_FLAG_NONE,
    };
    const struct ToriDraw_RasterKernelHD z_kernel = {
        .vtable = &hd_full_vtable,
        .user_data = &spy,
        .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER,
    };
    const struct ToriDraw_RasterKernelHD sorted_z_kernel = {
        .vtable = &hd_full_vtable,
        .user_data = &spy,
        .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING |
                 TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER,
    };
    int result;

    init_hd_materials(materials, env);
    hd_spy_reset(&spy, full_kernel.flags);
    clear_pixels(env);
    result = ToriDraw_RenderHDWithRasterKernel(
        fixture->handle, env->scene, &position, &viewport, &camera, env->pixels, &table,
        &stats, &full_kernel);
    CHECK(result == TORIDRAW_CULL_VISIBLE, "direct HD render returned %d", result);
    check_hd_full_calls(&spy);
    check_hd_sorted_order(&spy, env->scene, "direct HD sorted painter");
    check_hd_stats(&stats);
    CHECK(count_nonzero_pixels(env) == 0, "HD spy callbacks drew pixels");

    hd_spy_reset(&spy, none_kernel.flags);
    clear_pixels(env);
    result = ToriDraw_RenderHDWithRasterKernel(
        fixture->handle, env->scene, &position, &viewport, &camera, env->pixels, &table,
        &stats, &none_kernel);
    CHECK(result == TORIDRAW_CULL_VISIBLE, "direct HD model-order painter returned %d",
          result);
    check_hd_full_calls(&spy);
    check_hd_model_order(&spy, "direct HD model-order painter");
    check_hd_stats(&stats);
    CHECK(count_nonzero_pixels(env) == 0, "HD model-order painter callbacks drew pixels");

    hd_spy_reset(&spy, z_kernel.flags);
    clear_pixels(env);
    result = ToriDraw_RenderHDZBufferedWithRasterKernel(
        fixture->handle, env->scene, &position, &viewport, &camera, env->pixels, &table,
        &stats, &z_kernel);
    CHECK(result == TORIDRAW_CULL_VISIBLE, "direct HD depth returned %d", result);
    check_hd_full_calls(&spy);
    check_hd_model_order(&spy, "direct HD explicit model-order depth");
    check_hd_stats(&stats);

    hd_spy_reset(&spy, z_kernel.flags);
    clear_pixels(env);
    result = ToriDraw_RenderHDWithRasterKernel(
        fixture->handle, env->scene, &position, &viewport, &camera, env->pixels, &table,
        &stats, &z_kernel);
    CHECK(result == TORIDRAW_CULL_VISIBLE, "generic HD depth returned %d", result);
    check_hd_full_calls(&spy);
    check_hd_model_order(&spy, "generic HD model-order depth");
    check_hd_stats(&stats);

    hd_spy_reset(&spy, sorted_z_kernel.flags);
    clear_pixels(env);
    result = ToriDraw_RenderHDWithRasterKernel(
        fixture->handle, env->scene, &position, &viewport, &camera, env->pixels, &table,
        &stats, &sorted_z_kernel);
    CHECK(result == TORIDRAW_CULL_VISIBLE, "generic HD sorted depth returned %d", result);
    check_hd_full_calls(&spy);
    check_hd_sorted_order(&spy, env->scene, "generic HD sorted depth");
    check_hd_stats(&stats);

    hd_spy_reset(&spy, sorted_z_kernel.flags);
    clear_pixels(env);
    result = ToriDraw_RenderHDZBufferedWithRasterKernel(
        fixture->handle, env->scene, &position, &viewport, &camera, env->pixels, &table,
        &stats, &sorted_z_kernel);
    CHECK(result == TORIDRAW_CULL_VISIBLE, "direct HD sorted depth returned %d", result);
    check_hd_full_calls(&spy);
    check_hd_sorted_order(&spy, env->scene, "direct HD explicit sorted depth");
    check_hd_stats(&stats);
    CHECK(count_nonzero_pixels(env) == 0, "HD sorted depth callbacks drew pixels");
}

static void
test_hd_legacy_parity(const struct HDFixture* fixture, struct RenderEnv* env)
{
    struct ToriDraw_ViewPort viewport = test_viewport();
    struct ToriDraw_Camera camera = test_camera();
    struct ToriDraw_Position position = { .z = CAMERA_DISTANCE };
    struct ToriDraw_HDMaterial materials[HD_TEXTURE_COUNT];
    struct ToriDraw_HDMaterials table = { materials, HD_TEXTURE_COUNT };
    struct ToriDraw_HDRenderStats legacy_stats;
    struct ToriDraw_HDRenderStats direct_stats;
    const struct ToriDraw_RasterKernelHD* branching =
        ToriDraw_RasterKernelHDGetBranching();
    const struct ToriDraw_RasterKernelHD* zbuffered =
        ToriDraw_RasterKernelHDGetZBuffered();
    size_t const image_bytes =
        (size_t)VIEW_STRIDE * VIEW_HEIGHT * sizeof(*env->pixels);
    toripixel_t* reference = malloc(image_bytes);
    int legacy_result;
    int direct_result;

    CHECK(reference != NULL, "HD parity image allocation");
    if( !reference )
        return;
    init_hd_materials(materials, env);
    ToriDraw_RasterSetScanline(false);

    clear_pixels(env);
    legacy_result = ToriDraw_RenderHD(
        fixture->handle, env->scene, &position, &viewport, &camera, env->pixels, &table,
        &legacy_stats);
    memcpy(reference, env->pixels, image_bytes);
    ToriDraw_RasterSetScanline(true);
    clear_pixels(env);
    direct_result = ToriDraw_RenderHDWithRasterKernel(
        fixture->handle, env->scene, &position, &viewport, &camera, env->pixels, &table,
        &direct_stats, branching);
    CHECK(legacy_result == direct_result && direct_result == TORIDRAW_CULL_VISIBLE,
          "HD sorted parity results %d/%d", legacy_result, direct_result);
    CHECK(memcmp(&legacy_stats, &direct_stats, sizeof(legacy_stats)) == 0,
          "HD sorted parity stats differ");
    CHECK(memcmp(reference, env->pixels, image_bytes) == 0,
          "legacy HD wrapper differs from the branching built-in kernel");

    ToriDraw_RasterSetScanline(false);
    clear_pixels(env);
    legacy_result = ToriDraw_RenderHDZBuffered(
        fixture->handle, env->scene, &position, &viewport, &camera, env->pixels, &table,
        &legacy_stats);
    memcpy(reference, env->pixels, image_bytes);
    clear_pixels(env);
    direct_result = ToriDraw_RenderHDZBufferedWithRasterKernel(
        fixture->handle, env->scene, &position, &viewport, &camera, env->pixels, &table,
        &direct_stats, zbuffered);
    CHECK(legacy_result == direct_result && direct_result == TORIDRAW_CULL_VISIBLE,
          "HD depth parity results %d/%d", legacy_result, direct_result);
    CHECK(memcmp(&legacy_stats, &direct_stats, sizeof(legacy_stats)) == 0,
          "HD depth parity stats differ");
    CHECK(memcmp(reference, env->pixels, image_bytes) == 0,
          "legacy HD depth differs from the branching built-in kernel");

    free(reference);
}

#else /* TORIDRAW_PIXEL16 */

struct Pixel16HDSpy
{
    int calls;
};

static void
pixel16_hd_unexpected_callback(
    void* user_data,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceHD* face)
{
    struct Pixel16HDSpy* spy = user_data;

    (void)target;
    (void)face;
    spy->calls++;
}

static const struct ToriDraw_RasterKernelHDVTable pixel16_hd_vtable = {
    .draw = {
        [TORIDRAW_RASTER_FACE_HD_GOURAUD] = pixel16_hd_unexpected_callback,
        [TORIDRAW_RASTER_FACE_HD_FLAT] = pixel16_hd_unexpected_callback,
        [TORIDRAW_RASTER_FACE_HD_PLANE] = pixel16_hd_unexpected_callback,
        [TORIDRAW_RASTER_FACE_HD_CYLINDER] = pixel16_hd_unexpected_callback,
        [TORIDRAW_RASTER_FACE_HD_CUBE] = pixel16_hd_unexpected_callback,
        [TORIDRAW_RASTER_FACE_HD_SPHERE] = pixel16_hd_unexpected_callback,
    },
};

static void
check_zero_hd_stats(const struct ToriDraw_HDRenderStats* stats, const char* label)
{
    struct ToriDraw_HDRenderStats zero = {0};

    CHECK(memcmp(stats, &zero, sizeof(*stats)) == 0, "%s did not clear stats", label);
}

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
static void
test_pixel16_sd_depth_unsupported(
    const struct SDFixture* fixture,
    struct RenderEnv* env,
    bool explicit_kernel)
{
    struct ToriDraw_ViewPort viewport = test_viewport();
    struct ToriDraw_Camera camera = test_camera();
    struct ToriDraw_Position position = { .z = CAMERA_DISTANCE };
    struct SDSpy spy = { .fixture = fixture, .env = env };
    const struct ToriDraw_RasterKernelSD kernel = {
        .vtable = &sd_full_vtable,
        .user_data = &spy,
        .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER,
    };
    pid_t child = fork();
    int status = 0;

    CHECK(child >= 0, "fork for Pixel16 SD depth unsupported check");
    if( child < 0 )
        return;
    if( child == 0 )
    {
        int result;

        close(STDERR_FILENO);
        if( explicit_kernel )
            result = ToriDraw_RenderZBufferedWithRasterKernel(
                fixture->handle, env->scene, &position, &viewport, &camera, env->pixels,
                &kernel);
        else
            result = ToriDraw_RenderZBuffered(
                fixture->handle, env->scene, &position, &viewport, &camera, env->pixels,
                false);
        _exit(result == TORIDRAW_CULL_ERROR && spy.total_calls == 0 ? 0 : 2);
    }

    CHECK(waitpid(child, &status, 0) == child, "wait for Pixel16 SD depth child");
    CHECK((WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT) ||
              (WIFEXITED(status) && WEXITSTATUS(status) == 0),
          "Pixel16 %s SD depth status 0x%x", explicit_kernel ? "explicit" : "legacy",
          status);
}
#endif

static void
test_pixel16_hd_unsupported(const struct SDFixture* fixture, struct RenderEnv* env)
{
    struct ToriDraw_ViewPort viewport = test_viewport();
    struct ToriDraw_Camera camera = test_camera();
    struct ToriDraw_Position position = { .z = CAMERA_DISTANCE };
    struct ToriDraw_HDRenderStats stats;
    struct Pixel16HDSpy spy = {0};
    const struct ToriDraw_RasterKernelHD painter_kernel = {
        .vtable = &pixel16_hd_vtable,
        .user_data = &spy,
        .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
    };
    const struct ToriDraw_RasterKernelHD z_kernel = {
        .vtable = &pixel16_hd_vtable,
        .user_data = &spy,
        .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER,
    };
    int result;

    memset(&stats, 0xA5, sizeof(stats));
    result = ToriDraw_RenderHD(
        fixture->handle, env->scene, &position, &viewport, &camera, env->pixels, NULL,
        &stats);
    CHECK(result == TORIDRAW_CULL_ERROR, "Pixel16 legacy HD returned %d", result);
    check_zero_hd_stats(&stats, "Pixel16 legacy HD");

    memset(&stats, 0xA5, sizeof(stats));
    result = ToriDraw_RenderHDWithRasterKernel(
        fixture->handle, env->scene, &position, &viewport, &camera, env->pixels, NULL,
        &stats, &painter_kernel);
    CHECK(result == TORIDRAW_CULL_ERROR, "Pixel16 direct HD returned %d", result);
    check_zero_hd_stats(&stats, "Pixel16 direct HD");

    memset(&stats, 0xA5, sizeof(stats));
    result = ToriDraw_RenderHDZBuffered(
        fixture->handle, env->scene, &position, &viewport, &camera, env->pixels, NULL,
        &stats);
    CHECK(result == TORIDRAW_CULL_ERROR, "Pixel16 legacy HD depth returned %d", result);
    check_zero_hd_stats(&stats, "Pixel16 legacy HD depth");

    memset(&stats, 0xA5, sizeof(stats));
    result = ToriDraw_RenderHDZBufferedWithRasterKernel(
        fixture->handle, env->scene, &position, &viewport, &camera, env->pixels, NULL,
        &stats, &z_kernel);
    CHECK(result == TORIDRAW_CULL_ERROR, "Pixel16 direct HD depth returned %d", result);
    check_zero_hd_stats(&stats, "Pixel16 direct HD depth");
    CHECK(spy.calls == 0, "Pixel16 unsupported HD path invoked %d callbacks", spy.calls);
}

#endif /* TORIDRAW_PIXEL16 */

int
main(void)
{
    struct SDFixture sd_fixture;
    struct RenderEnv env;

    ToriDraw_Init();
    ToriDraw_RasterSetScanline(false);
    test_typed_builtin_kernels();
    sd_fixture_init(&sd_fixture);
    if( !render_env_init(&env) )
    {
        sd_fixture_destroy(&sd_fixture);
        return 1;
    }

    test_sd_direct_apis(&sd_fixture, &env);
    test_sd_legacy_parity(&sd_fixture, &env);

#ifndef TORIDRAW_PIXEL16
    {
        struct HDFixture hd_fixture;

        hd_fixture_init(&hd_fixture);
        test_hd_direct_apis(&hd_fixture, &env);
        test_hd_legacy_parity(&hd_fixture, &env);
        hd_fixture_destroy(&hd_fixture);
    }
#else
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    test_pixel16_sd_depth_unsupported(&sd_fixture, &env, false);
    test_pixel16_sd_depth_unsupported(&sd_fixture, &env, true);
#endif
    test_pixel16_hd_unsupported(&sd_fixture, &env);
#endif

    render_env_destroy(&env);
    sd_fixture_destroy(&sd_fixture);
    if( failures )
    {
        fprintf(stderr, "toridraw_raster_kernel_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("toridraw_raster_kernel_test: all checks passed\n");
    return 0;
}
