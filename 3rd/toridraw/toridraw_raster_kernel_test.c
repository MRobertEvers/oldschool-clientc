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

/*
 * Does THIS BUILD have the whole-model run door?
 *
 * Runtime-gated, never #ifdef TORIDRAW_RASTER_BATCH: that macro is derived
 * inside the library's own translation unit from -D flags this test is not
 * compiled with, so a preprocessor gate here compiles the check away and
 * reports a pass. Ask the kernel what it actually named instead.
 *
 * A lane with no presorted-run assembly resolves toridraw_raster_walk_batched
 * to the per-face walk, so the branching kernel HAS no door, the sort is
 * correctly told not to stash, and every expectation that depends on the
 * stash has to stand down with it. That is the 16-bit ABI's situation by
 * construction -- the run kernels store 4-byte pixels -- and it is also any
 * lane built with TORIDRAW_NO_SIMD.
 */
static bool
whole_model_door_built(void)
{
    return ToriDraw_RasterKernelSDGetBranching()->draw_model != ToriDraw_RasterWalkPerFace;
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
        .projection_mode = TORIDRAW_PROJECTION_MODE_SCALE,
        .projection_scale = TORIDRAW_PROJECTION_SCALE_DEFAULT,
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
    fixture->model.has_bounds_cylinder = false;
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

    /* The vtable is the face callbacks and nothing else; the stage-3 entry
     * lives on the kernel, because stage 3 draws a MODEL. */
    CHECK(sizeof(struct ToriDraw_RasterKernelSDVTable) ==
              4 * sizeof(ToriDraw_RasterKernelSDFaceFn),
          "SD vtable is not four typed face slots");

    /*
     * Which walk each kernel names.
     *
     * Every raster kernel has a draw_model, because that IS stage 3. Naming
     * the stock ToriDraw_RasterWalkPerFace is how a kernel says it has no
     * traversal of its own and only supplies the four leaf callbacks -- which
     * is what the scanline and smooth families do, and must keep doing: they
     * are different rasterisers, and a presorted run drawing their faces would
     * draw wrong pixels.
     */
    CHECK(sd_branching->draw_model != NULL, "branching names a walk");
    CHECK(ToriDraw_RasterKernelSDGetBranchingPerFace()->draw_model ==
              ToriDraw_RasterWalkPerFace,
          "branching-per-face names the stock walk");
    CHECK(ToriDraw_RasterKernelSDGetBranchingPerFace()->vtable->draw[0] ==
              sd_branching->vtable->draw[0],
          "the per-face twin draws the same faces");
    CHECK(sd_scanline->draw_model == ToriDraw_RasterWalkPerFace,
          "scanline names the stock walk");
    CHECK(sd_smooth_branching->draw_model == ToriDraw_RasterWalkPerFace,
          "smooth branching names the stock walk");
    CHECK(ToriDraw_RasterKernelSDGetGpu()->draw_model == NULL,
          "the gpu kernel has no raster stage at all");

    /*
     * Which depth-tested twin each painter names.
     *
     * The stage-3 entries read this slot to honour TORIDRAW_MODEL_FLAG_ZBUFFER
     * instead of recognising the caller's kernel by address. The smooth twin
     * draws the same pixels as the flat one today -- the depth family shares
     * one vtable -- so the only thing holding the smooth painters to the
     * smooth twin is this check and the day the family grows a smooth
     * callback.
     */
    CHECK(sd_branching->zbuffered_variant != NULL, "branching names no depth twin");
    CHECK(sd_scanline->zbuffered_variant == sd_branching->zbuffered_variant,
          "the flat painters disagree on their depth twin");
    CHECK(sd_smooth_branching->zbuffered_variant != NULL,
          "smooth branching names no depth twin");
    CHECK(sd_smooth_scanline->zbuffered_variant == sd_smooth_branching->zbuffered_variant,
          "the smooth painters disagree on their depth twin");
    CHECK(sd_smooth_branching->zbuffered_variant != sd_branching->zbuffered_variant,
          "the smooth painter took the flat painter's depth twin");
    CHECK((sd_branching->zbuffered_variant->flags &
           TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER) != 0,
          "a depth twin that does not ask for the depth buffer");
    CHECK((sd_branching->zbuffered_variant->flags &
           TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING) != 0,
          "a sorting painter's depth twin dropped the sort");
    CHECK(sd_zbuffered->zbuffered_variant == NULL,
          "a depth kernel is its own twin and should name none");
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
    static const enum ToriDraw_RasterFaceClassSD expected[SD_FACE_COUNT] = {
        TORIDRAW_RASTER_FACE_SD_GOURAUD,
        TORIDRAW_RASTER_FACE_SD_FLAT,
        TORIDRAW_RASTER_FACE_SD_TEXTURED,
        TORIDRAW_RASTER_FACE_SD_TEXTURED_FLAT,
    };
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
    static const int expected[TORIDRAW_RASTER_FACE_SD_CLASS_COUNT] = { 1, 1, 1, 1 };

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
    /*
     * A caller's own RASTER kernel. It names a walk and four leaf callbacks
     * and nothing else -- stages 1 and 2 belong to the table, and the entries
     * used here are the ones whose caller names no table, so they run the
     * library's default projection and sort.
     */
    const struct ToriDraw_RasterKernelSD full_kernel = {
        .draw_model = ToriDraw_RasterWalkPerFace,
        .vtable = &sd_full_vtable,
        .user_data = &spy,
        .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
    };
    const struct ToriDraw_RasterKernelSD none_kernel = {
        .draw_model = ToriDraw_RasterWalkPerFace,
        .vtable = &sd_full_vtable,
        .user_data = &spy,
        .flags = TORIDRAW_RASTER_KERNEL_FLAG_NONE,
    };
#ifndef TORIDRAW_PIXEL16
    const struct ToriDraw_RasterKernelSD z_kernel = {
        .draw_model = ToriDraw_RasterWalkPerFace,
        .vtable = &sd_full_vtable,
        .user_data = &spy,
        .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER,
    };
    const struct ToriDraw_RasterKernelSD sorted_z_kernel = {
        .draw_model = ToriDraw_RasterWalkPerFace,
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
    fixture->hd.texture_mappings = NULL;
    fixture->hd.base.has_bounds_cylinder = false;
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
        TORIDRAW_RASTER_FACE_HD_TEXTURED_PLANE,
        TORIDRAW_RASTER_FACE_HD_TEXTURED_CYLINDER,
        TORIDRAW_RASTER_FACE_HD_TEXTURED_CUBE,
        TORIDRAW_RASTER_FACE_HD_TEXTURED_SPHERE,
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
DEFINE_HD_CALLBACK(hd_spy_plane, TORIDRAW_RASTER_FACE_HD_TEXTURED_PLANE)
DEFINE_HD_CALLBACK(hd_spy_cylinder, TORIDRAW_RASTER_FACE_HD_TEXTURED_CYLINDER)
DEFINE_HD_CALLBACK(hd_spy_cube, TORIDRAW_RASTER_FACE_HD_TEXTURED_CUBE)
DEFINE_HD_CALLBACK(hd_spy_sphere, TORIDRAW_RASTER_FACE_HD_TEXTURED_SPHERE)

static const struct ToriDraw_RasterKernelHDVTable hd_full_vtable = {
    .draw = {
        [TORIDRAW_RASTER_FACE_HD_GOURAUD] = hd_spy_gouraud,
        [TORIDRAW_RASTER_FACE_HD_FLAT] = hd_spy_flat,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_PLANE] = hd_spy_plane,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_CYLINDER] = hd_spy_cylinder,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_CUBE] = hd_spy_cube,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_SPHERE] = hd_spy_sphere,
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
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_PLANE] = pixel16_hd_unexpected_callback,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_CYLINDER] = pixel16_hd_unexpected_callback,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_CUBE] = pixel16_hd_unexpected_callback,
        [TORIDRAW_RASTER_FACE_HD_TEXTURED_SPHERE] = pixel16_hd_unexpected_callback,
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
        .draw_model = ToriDraw_RasterWalkPerFace,
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


/*
 * Kernel scratch: what a kernel needs, what a scene has, and the gap.
 *
 * The interesting case is the FULL scene. The flat sort's keys and the batched
 * walk's stash are small-tier scratch, so a full scene cannot satisfy either --
 * and the point of the needs mask is that this is now something a caller can
 * ask about instead of discovering in a profile.
 */
static void
test_kernel_scratch(void)
{
    struct ToriDraw_Scene* small;
    struct ToriDraw_Scene* full;
    uint32_t needs;

    small = ToriDraw_SceneNew(TORIDRAW_SCENE_SMALL, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    CHECK(small != NULL, "small scene allocated");
    full = ToriDraw_SceneNew(TORIDRAW_SCENE_FULL, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    CHECK(full != NULL, "full scene allocated");
    if( !small || !full )
        return;

    /* A small scene created the stock way already carries everything the stock
     * painter asks for -- the ensure API is a no-op there, which is what keeps
     * it from changing any existing caller's allocation. */
    needs = ToriDraw_SceneKernelScratchNeeds(small, ToriDraw_RasterKernelSDGetBranching());
    /* Only where the run door was built: with no door the branching kernel is
     * the per-face walk, and asking the sort to fill a stash nothing loads is
     * precisely what the needs mask exists to prevent. */
    if( whole_model_door_built() )
        CHECK(needs & TORIDRAW_SCENE_SCRATCH_PRESORT_XY, "small branching wants the stash");
    else
        CHECK(!(needs & TORIDRAW_SCENE_SCRATCH_PRESORT_XY),
              "small branching does not want the stash with no door built");
    CHECK(needs & TORIDRAW_SCENE_SCRATCH_CSR_SORT, "small branching wants the CSR sort");
    CHECK(ToriDraw_SceneHasScratch(small, needs), "small scene already satisfies it");
    CHECK(ToriDraw_SceneEnsureKernelScratch(small, ToriDraw_RasterKernelSDGetBranching()),
          "ensure succeeds on a small scene");

    /* The scanline family never loads the stash, so it must not be asked for:
     * filling it would be seven stores per face into a buffer nobody reads. */
    needs = ToriDraw_SceneKernelScratchNeeds(small, ToriDraw_RasterKernelSDGetScanline());
    CHECK(!(needs & TORIDRAW_SCENE_SCRATCH_PRESORT_XY),
          "scanline does not ask for the stash");

    /* A z-buffered kernel runs no face sort at all, so it needs neither the
     * order nor any sort scratch. */
    needs = ToriDraw_SceneKernelScratchNeeds(small, ToriDraw_RasterKernelSDGetZBuffered());
    CHECK(!(needs & TORIDRAW_SCENE_SCRATCH_FACE_ORDER),
          "zbuffered does not ask for the face order");
    CHECK(!(needs & TORIDRAW_SCENE_SCRATCH_CSR_SORT),
          "zbuffered does not ask for sort scratch");

    /* The GPU kernel sorts but never rasters in software: order yes, stash no. */
    needs = ToriDraw_SceneKernelScratchNeeds(small, ToriDraw_RasterKernelSDGetGpu());
    CHECK(needs & TORIDRAW_SCENE_SCRATCH_FACE_ORDER, "gpu wants the face order");
    CHECK(!(needs & TORIDRAW_SCENE_SCRATCH_PRESORT_XY), "gpu does not want the stash");

    /* THE FULL SCENE. It takes the dense bucket table, and neither the flat
     * keys nor the stash are asked for -- because on a full scene the flat
     * face-sort kernel runs the same bucket sort the bucket kernel does. */
    needs = ToriDraw_SceneKernelScratchNeeds(full, ToriDraw_RasterKernelSDGetBranching());
    CHECK(needs & TORIDRAW_SCENE_SCRATCH_BUCKET_SORT, "full branching wants the buckets");
    CHECK(!(needs & TORIDRAW_SCENE_SCRATCH_PRESORT_XY),
          "full scene is not asked for the stash");
    CHECK(!(needs & TORIDRAW_SCENE_SCRATCH_FLAT_KEYS),
          "full scene is not asked for the flat keys");
    CHECK(ToriDraw_SceneHasScratch(full, needs), "full scene satisfies its own needs");
    CHECK(!(ToriDraw_SceneScratchResident(full) & TORIDRAW_SCENE_SCRATCH_PRESORT_XY),
          "full scene really has no stash");

    /* Idempotent: asking twice allocates nothing the second time and still
     * reports satisfied. */
    CHECK(ToriDraw_SceneEnsureKernelScratch(full, ToriDraw_RasterKernelSDGetBranching()),
          "ensure succeeds on a full scene");
    CHECK(ToriDraw_SceneEnsureKernelScratch(full, ToriDraw_RasterKernelSDGetBranching()),
          "ensure is idempotent");


    /* Traits, not identity. A sort declares what it can hand on and what it
     * needs; the scratch API reads that rather than comparing pointers, so a
     * caller's own sort kernel is reasoned about the same way. */
    {
        const struct ToriDraw_FaceCullSortKernel* bucket =
            ToriDraw_FaceCullSortKernelGetBucket();
        const struct ToriDraw_FaceCullSortKernel* flat =
            ToriDraw_FaceCullSortKernelGetFlat();
        struct ToriDraw_Kernel k;

        CHECK(bucket->provides & TORIDRAW_FACESORT_PROVIDES_FACE_ORDER,
              "bucket provides the face order");
        CHECK(flat->provides & TORIDRAW_FACESORT_PROVIDES_FACE_ORDER,
              "flat provides the face order");
        /* Both can stash: the small-scene bucket sort does it in
         * bucket_sort_by_average_depth_small, the flat sort in its own block. */
        CHECK(bucket->provides & TORIDRAW_FACESORT_PROVIDES_PRESORTED_XY,
              "bucket can presort");
        CHECK(flat->provides & TORIDRAW_FACESORT_PROVIDES_PRESORTED_XY,
              "flat can presort");
        /* Only the flat sort sorts keys, and only it degrades on a full scene. */
        CHECK(!(bucket->needs & TORIDRAW_FACESORT_NEEDS_FLAT_KEYS),
              "bucket needs no key arrays");
        CHECK(flat->needs & TORIDRAW_FACESORT_NEEDS_FLAT_KEYS,
              "flat needs the key arrays");
        CHECK(flat->needs & TORIDRAW_FACESORT_NEEDS_SMALL_SCENE,
              "flat needs a small scene to be itself");

        /* The needs mask follows the declaration: naming the bucket sort must
         * not reserve key arrays it will never touch. */
        k = *ToriDraw_KernelGetSoftwarePainter();
        k.face_sort = bucket;
        CHECK(!(ToriDraw_KernelScratchNeeds(small, &k) &
                TORIDRAW_SCENE_SCRATCH_FLAT_KEYS),
              "bucket sort asks for no flat keys");
        k.face_sort = flat;
        CHECK(ToriDraw_KernelScratchNeeds(small, &k) & TORIDRAW_SCENE_SCRATCH_FLAT_KEYS,
              "flat sort asks for the flat keys");
        /* On a full scene neither does, because neither runs there. */
        CHECK(!(ToriDraw_KernelScratchNeeds(full, &k) &
                TORIDRAW_SCENE_SCRATCH_FLAT_KEYS),
              "full scene asks for no flat keys even for the flat sort");
    }

    ToriDraw_SceneFree(small);
    ToriDraw_SceneFree(full);
}

/*
 * The whole-model door against the per-face walk, pixel for pixel.
 *
 * The batched walk had no end-to-end coverage: test-presorted-neon scores the
 * eight run kernels in isolation, against C references it calls itself, but
 * nothing checked the WALK that stages faces into those runs -- which class it
 * assigns a face to, when it flushes, and whether the result is the picture
 * the per-face path draws. The two are the same kernel set reached two ways,
 * so they must agree exactly.
 *
 * Needs a SMALL scene: the y-ordered stash the door reads is small-tier
 * scratch, and the rest of this file's fixtures are TORIDRAW_SCENE_FULL, which
 * is why the door never fired here before.
 */
static void
test_whole_model_door(struct SDFixture* fixture)
{
    struct ToriDraw_Scene* scene =
        ToriDraw_SceneNew(TORIDRAW_SCENE_SMALL, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    struct ToriDraw_ViewPort viewport = test_viewport();
    struct ToriDraw_Camera camera = test_camera();
    struct ToriDraw_Position position = { .z = 420 };
    size_t const count = (size_t)VIEW_STRIDE * VIEW_HEIGHT;
    toripixel_t* batched = calloc(count, sizeof(toripixel_t));
    toripixel_t* per_face = calloc(count, sizeof(toripixel_t));
    struct ToriDraw_Texture* texture = make_test_texture();
    int diff = 0;

    if( !whole_model_door_built() )
    {
        printf("  whole-model door not built on this lane -- skipped\n");
        ToriDraw_SceneFree(scene);
        free(batched);
        free(per_face);
        if( texture )
            ToriDraw_TextureFree(texture);
        return;
    }

    CHECK(scene != NULL, "door: scene");
    CHECK(batched != NULL && per_face != NULL, "door: framebuffers");
    CHECK(texture != NULL, "door: texture");
    if( !scene || !batched || !per_face || !texture )
        goto done;
    ToriDraw_SceneSetTexture(scene, TEST_TEXTURE_ID, texture);

    /* The stash only exists if the scene was prepared for a kernel that wants
     * it -- which is the scratch API's whole job. */
    CHECK(ToriDraw_SceneEnsureKernelScratch(scene, ToriDraw_RasterKernelSDGetBranching()),
          "door: scratch for the branching kernel");
    CHECK(ToriDraw_SceneHasScratch(scene, TORIDRAW_SCENE_SCRATCH_PRESORT_XY),
          "door: the stash is resident");

    CHECK(ToriDraw_RenderModelWithRasterKernel(
              fixture->handle, scene, &position, &viewport, &camera, batched,
              ToriDraw_RasterKernelSDGetBranching()) == TORIDRAW_CULL_VISIBLE,
          "door: batched render visible");
    /* Set by the sort when it actually stashed. If this is zero the door
     * cannot have fired and the comparison below is vacuous. */
    CHECK(scene->sm_face_xy_valid, "door: the sort stashed, so the door was reachable");

    CHECK(ToriDraw_RenderModelWithRasterKernel(
              fixture->handle, scene, &position, &viewport, &camera, per_face,
              ToriDraw_RasterKernelSDGetBranchingPerFace()) == TORIDRAW_CULL_VISIBLE,
          "door: per-face render visible");

    for( size_t i = 0; i < count; i++ )
    {
        if( batched[i] != per_face[i] )
            diff++;
    }
    CHECK(diff == 0, "door: %d pixels differ between the batched and per-face walks", diff);

    /* And the comparison is not vacuous in the other direction either: the
     * model has to have drawn something. */
    {
        int drawn = 0;
        for( size_t i = 0; i < count; i++ )
            if( batched[i] != 0 )
                drawn++;
        CHECK(drawn > 0, "door: the batched walk drew no pixels at all");
    }

done:
    if( texture )
        ToriDraw_SceneSetTexture(scene, TEST_TEXTURE_ID, NULL);
    free(batched);
    free(per_face);
    ToriDraw_SceneFree(scene);
}


/*
 * The kernel table: what each prebaked triple asks the scene for, and what
 * ToriDraw_KernelValidate says about the pairing.
 *
 * The interesting assertions are the negative ones. A table whose raster has
 * no whole-model door must not make the sort stash for it, and a table on a
 * full scene must be reported DEGRADED rather than silently taking a slower
 * path -- that report is the entire reason the enum has three values.
 */
static void
test_kernel_tables(void)
{
    struct ToriDraw_Scene* small =
        ToriDraw_SceneNew(TORIDRAW_SCENE_SMALL, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    struct ToriDraw_Scene* full =
        ToriDraw_SceneNew(TORIDRAW_SCENE_FULL, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    const struct ToriDraw_Kernel* painter = ToriDraw_KernelGetSoftwarePainter();
    const struct ToriDraw_Kernel* scanline = ToriDraw_KernelGetSoftwareScanline();
    const struct ToriDraw_Kernel* zbuf = ToriDraw_KernelGetSoftwareZBuffered();
    const struct ToriDraw_Kernel* gpu = ToriDraw_KernelGetGpu();
    const struct ToriDraw_Kernel* baker = ToriDraw_KernelGetSpriteBaker();
    const char* why = NULL;

    CHECK(small != NULL && full != NULL, "tables: scenes");
    if( !small || !full )
        return;

    /* Every table names three real stages. */
    CHECK(painter->name && scanline->name && zbuf->name && gpu->name && baker->name,
          "tables: every table is named");
    CHECK(gpu->raster == NULL, "tables: the gpu table has no raster stage");
    CHECK(painter->raster != NULL, "tables: the painter table has a raster stage");

    /* The painter is the only table that asks for the presort stash, and only
     * on a small scene. */
    if( whole_model_door_built() )
        CHECK(ToriDraw_KernelScratchNeeds(small, painter) & TORIDRAW_SCENE_SCRATCH_PRESORT_XY,
              "tables: painter wants the stash on a small scene");
    else
        CHECK(!(ToriDraw_KernelScratchNeeds(small, painter) &
                TORIDRAW_SCENE_SCRATCH_PRESORT_XY),
              "tables: painter does not want the stash with no door built");
    CHECK(!(ToriDraw_KernelScratchNeeds(small, scanline) &
            TORIDRAW_SCENE_SCRATCH_PRESORT_XY),
          "tables: scanline does not want the stash");
    CHECK(!(ToriDraw_KernelScratchNeeds(small, baker) & TORIDRAW_SCENE_SCRATCH_PRESORT_XY),
          "tables: the sprite baker does not want the stash");
    CHECK(!(ToriDraw_KernelScratchNeeds(small, gpu) & TORIDRAW_SCENE_SCRATCH_PRESORT_XY),
          "tables: the gpu table does not want the stash");
    CHECK(!(ToriDraw_KernelScratchNeeds(full, painter) & TORIDRAW_SCENE_SCRATCH_PRESORT_XY),
          "tables: a full scene is never asked for the stash");

    /* The z-buffered table runs no sort at all. */
    CHECK(!(ToriDraw_KernelScratchNeeds(small, zbuf) & TORIDRAW_SCENE_SCRATCH_FACE_ORDER),
          "tables: zbuffered wants no face order");
    /* The gpu table does: it sorts for the upload. */
    CHECK(ToriDraw_KernelScratchNeeds(small, gpu) & TORIDRAW_SCENE_SCRATCH_FACE_ORDER,
          "tables: gpu wants the face order");

    /* Fit. On a small scene the painter is whole; on a full one it degrades,
     * and says which half degraded. */
    CHECK(ToriDraw_KernelValidate(painter, small, &why) == TORIDRAW_KERNEL_FIT_OK,
          "tables: painter fits a small scene (%s)", why);
    CHECK(ToriDraw_KernelValidate(painter, full, &why) == TORIDRAW_KERNEL_FIT_DEGRADED,
          "tables: painter degrades on a full scene (%s)", why);
    CHECK(why != NULL && strcmp(why, "ok") != 0, "tables: a degrade explains itself");

    /* The scanline table has no door, so a full scene costs it nothing extra
     * beyond the sort fallback -- it must not be reported as a raster degrade. */
    CHECK(ToriDraw_KernelValidate(scanline, small, &why) == TORIDRAW_KERNEL_FIT_OK,
          "tables: scanline fits a small scene (%s)", why);

    CHECK(ToriDraw_KernelValidate(gpu, small, &why) == TORIDRAW_KERNEL_FIT_OK,
          "tables: the gpu table is valid (%s)", why);

    /* A hand-built table with a hole in its raster vtable is refused. */
    {
        struct ToriDraw_RasterKernelSDVTable broken = *painter->raster->vtable;
        struct ToriDraw_RasterKernelSD raster = *painter->raster;
        struct ToriDraw_Kernel table = *painter;

        broken.draw[TORIDRAW_RASTER_FACE_SD_FLAT] = NULL;
        raster.vtable = &broken;
        table.raster = &raster;
        CHECK(ToriDraw_KernelValidate(&table, small, &why) ==
                  TORIDRAW_KERNEL_FIT_INCOMPATIBLE,
              "tables: a NULL face slot is incompatible");
    }

    /* Ensure is idempotent and satisfies what it reported. */
    CHECK(ToriDraw_KernelEnsureScratch(small, painter), "tables: ensure for the painter");
    CHECK(ToriDraw_SceneHasScratch(small, ToriDraw_KernelScratchNeeds(small, painter)),
          "tables: ensure satisfied the painter's needs");
    CHECK(ToriDraw_KernelEnsureScratch(small, painter), "tables: ensure is idempotent");

    ToriDraw_SceneFree(small);
    ToriDraw_SceneFree(full);
}


/*
 * The table-driven stage entries against the kernel-driven ones.
 *
 * Same three stages reached two ways, so the pictures must be identical. The
 * part worth pinning is stage 2: both entries decide the presort themselves,
 * from the raster they name, and both must reach the same answer the caller
 * used to reach by picking a function whose name said "Presorted" -- including
 * the negative case, where a raster with no whole-model door must NOT stash,
 * because that store is pure cost for a raster that will not read it.
 */
static void
test_table_entries(struct SDFixture* fixture)
{
    struct ToriDraw_Scene* scene =
        ToriDraw_SceneNew(TORIDRAW_SCENE_SMALL, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    struct ToriDraw_ViewPort viewport = test_viewport();
    struct ToriDraw_Camera camera = test_camera();
    struct ToriDraw_Position position = { .z = 420 };
    size_t const count = (size_t)VIEW_STRIDE * VIEW_HEIGHT;
    toripixel_t* via_table = calloc(count, sizeof(toripixel_t));
    toripixel_t* via_kernel = calloc(count, sizeof(toripixel_t));
    struct ToriDraw_Texture* texture = make_test_texture();
    const struct ToriDraw_Kernel* painter = ToriDraw_KernelGetSoftwarePainter();
    const struct ToriDraw_Kernel* baker = ToriDraw_KernelGetSpriteBaker();
    const struct ToriDraw_Kernel* scanline = ToriDraw_KernelGetSoftwareScanline();
    int diff = 0;
    int drawn = 0;

    CHECK(scene && via_table && via_kernel && texture, "entries: fixtures");
    if( !scene || !via_table || !via_kernel || !texture )
        goto done;
    ToriDraw_SceneSetTexture(scene, TEST_TEXTURE_ID, texture);
    CHECK(ToriDraw_KernelEnsureScratch(scene, painter), "entries: scratch");

    CHECK(ToriDraw_RenderModelWithTable(
              fixture->handle, scene, &position, &viewport, &camera, via_table,
              painter) == TORIDRAW_CULL_VISIBLE,
          "entries: table render visible");
    CHECK(ToriDraw_RenderModelWithRasterKernel(
              fixture->handle, scene, &position, &viewport, &camera, via_kernel,
              ToriDraw_RasterKernelSDGetBranching()) == TORIDRAW_CULL_VISIBLE,
          "entries: kernel render visible");

    for( size_t i = 0; i < count; i++ )
    {
        if( via_table[i] != via_kernel[i] )
            diff++;
        if( via_table[i] != 0 )
            drawn++;
    }
    CHECK(diff == 0, "entries: %d pixels differ between the table and kernel entries", diff);
    CHECK(drawn > 0, "entries: the table entry drew nothing");

    /* Stage 2's own decision, both ways round. The painter's raster has a
     * door, so its sort stashes; the baker's does not, so its sort must not --
     * and that is visible in the flag the sort records. */
    ToriDraw_RenderModel1ProjectWithTable(
        fixture->handle, scene, &position, &viewport, &camera, painter);
    ToriDraw_RenderModel2SortFacesWithTable(fixture->handle, scene, painter);
    if( whole_model_door_built() )
        CHECK(scene->sm_face_xy_valid,
              "entries: the painter table stashes, because its raster has a door");
    else
        CHECK(!scene->sm_face_xy_valid,
              "entries: the painter table does not stash with no door built");

    ToriDraw_RenderModel1ProjectWithTable(
        fixture->handle, scene, &position, &viewport, &camera, baker);
    ToriDraw_RenderModel2SortFacesWithTable(fixture->handle, scene, baker);
    CHECK(!scene->sm_face_xy_valid,
          "entries: the sprite-baker table does not stash, because its raster has no door");

    /*
     * The third rasteriser, for completeness: painter (a door) and baker (no
     * door) are the two above, and scanline is the case where the raster is a
     * different family altogether -- it must not be asked to stash either,
     * since a presorted run drawing scanline faces would draw the branching
     * family's pixels.
     */
    ToriDraw_RenderModel1ProjectWithTable(
        fixture->handle, scene, &position, &viewport, &camera, scanline);
    ToriDraw_RenderModel2SortFacesWithTable(fixture->handle, scene, scanline);
    CHECK(!scene->sm_face_xy_valid,
          "entries: the scanline table does not stash, because its raster has no door");

done:
    if( texture && scene )
        ToriDraw_SceneSetTexture(scene, TEST_TEXTURE_ID, NULL);
    free(via_table);
    free(via_kernel);
    ToriDraw_SceneFree(scene);
}

int
main(void)
{
    struct SDFixture sd_fixture;
    struct RenderEnv env;

    ToriDraw_Init();
    ToriDraw_RasterSetScanline(false);
    test_typed_builtin_kernels();
    test_kernel_scratch();
    test_kernel_tables();
    sd_fixture_init(&sd_fixture);
    if( !render_env_init(&env) )
    {
        sd_fixture_destroy(&sd_fixture);
        return 1;
    }

    test_whole_model_door(&sd_fixture);
    test_table_entries(&sd_fixture);
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
