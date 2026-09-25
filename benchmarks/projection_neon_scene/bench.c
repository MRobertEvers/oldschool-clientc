/*
 * Current ToriDraw NEON projection versus fully hand-written AArch64 NEON.
 *
 * The workload is built from decoded OSRS model archives, not generated
 * vertices.  By default it takes a deterministic slice of the revision-239
 * content checkout and places those models through a camera-space scene.  Use
 * --models to point at another directory containing model_*.model archives.
 */

#define _POSIX_C_SOURCE 200809L

#include "datatypes/model.h"
#include "graphics/shared_tables.h"
#include "projection_neon_scene.h"

/* Pull in the exact production entry points being measured. */
#include "graphics/projection16_simd.u.c"

#include <assert.h>
#include <errno.h>
#include <glob.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !defined(__aarch64__)
#error "projection_neon_scene requires AArch64/Advanced SIMD"
#endif

#define CHECK_OFFSET(member, expected)                                                             \
    _Static_assert(                                                                                \
        offsetof(struct ProjectionNeonSceneArgs, member) == (expected),                            \
        "ProjectionNeonSceneArgs assembly offset mismatch: " #member)

CHECK_OFFSET(
    ortho_x,
    0);
CHECK_OFFSET(
    ortho_y,
    8);
CHECK_OFFSET(
    ortho_z,
    16);
CHECK_OFFSET(
    screen_x,
    24);
CHECK_OFFSET(
    screen_y,
    32);
CHECK_OFFSET(
    screen_z,
    40);
CHECK_OFFSET(
    vertex_x,
    48);
CHECK_OFFSET(
    vertex_y,
    56);
CHECK_OFFSET(
    vertex_z,
    64);
CHECK_OFFSET(
    vertex_count,
    72);
CHECK_OFFSET(
    cos_model_yaw16,
    76);
CHECK_OFFSET(
    sin_model_yaw16,
    80);
CHECK_OFFSET(
    cos_camera_yaw16,
    84);
CHECK_OFFSET(
    sin_camera_yaw16,
    88);
CHECK_OFFSET(
    cos_camera_pitch16,
    92);
CHECK_OFFSET(
    sin_camera_pitch16,
    96);
CHECK_OFFSET(
    scene_x,
    100);
CHECK_OFFSET(
    scene_y,
    104);
CHECK_OFFSET(
    scene_z,
    108);
CHECK_OFFSET(
    camera_cot15,
    112);
CHECK_OFFSET(
    model_mid_z,
    116);
CHECK_OFFSET(
    near_plane_z,
    120);
CHECK_OFFSET(
    model_yaw_nonzero,
    124);
_Static_assert(
    sizeof(struct ProjectionNeonSceneArgs) == 128,
    "ProjectionNeonSceneArgs assembly size mismatch");

enum Variant
{
    VAR_FUSED_TEX,
    VAR_UNFUSED_TEX,
    VAR_FUSED_NOTEX,
    VAR_UNFUSED_NOTEX,
    VAR_COUNT
};

static const char* const variant_name[VAR_COUNT] = {
    "fused/tex",
    "unfused/tex",
    "fused/notex",
    "unfused/notex",
};

struct SceneModel
{
    int16_t* x;
    int16_t* y;
    int16_t* z;
    uint32_t count;
    uint32_t padded_count;
    int textured;
    int model_yaw;
    int camera_pitch;
    int camera_yaw;
    int scene_x;
    int scene_y;
    int scene_z;
    int model_mid_z;
    int near_plane_z;
    int camera_cot16;
    struct ProjectionNeonSceneArgs assembly_args;
};

struct Buffers
{
    int32_t* ox;
    int32_t* oy;
    int32_t* oz;
    int32_t* sx;
    int32_t* sy;
    int32_t* sz;
    uint32_t capacity;
};

struct Scene
{
    struct SceneModel* models;
    size_t count;
    uint64_t tex_vertices;
    uint64_t notex_vertices;
    size_t tex_models;
    size_t notex_models;
    uint32_t max_padded_count;
};

static volatile uint64_t bench_sink;

static void
die(const char* message)
{
    fprintf(stderr, "projection_neon_scene: %s\n", message);
    exit(1);
}

static double
now_seconds(void)
{
    struct timespec ts;
    if( clock_gettime(CLOCK_MONOTONIC, &ts) != 0 )
        die("clock_gettime failed");
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int16_t
reference_vertex(
    int value,
    int format_version)
{
    if( format_version >= 13 )
        value >>= 2;
    if( value < INT16_MIN )
        value = INT16_MIN;
    if( value > INT16_MAX )
        value = INT16_MAX;
    return (int16_t)value;
}

static uint8_t*
read_file(
    const char* path,
    size_t* out_size)
{
    FILE* file = fopen(path, "rb");
    long end;
    uint8_t* bytes;
    size_t size;

    if( !file )
        return NULL;
    if( fseek(file, 0, SEEK_END) != 0 )
        die("cannot seek model file");
    end = ftell(file);
    if( end <= 0 )
        die("empty model file");
    if( fseek(file, 0, SEEK_SET) != 0 )
        die("cannot rewind model file");
    size = (size_t)end;
    bytes = (uint8_t*)malloc(size);
    assert(bytes);
    if( fread(bytes, 1, size, file) != size )
        die("short read from model file");
    fclose(file);
    *out_size = size;
    return bytes;
}

static int
model_is_textured(const struct RSCache_Model* model)
{
    if( !model->face_textures )
        return 0;
    for( int i = 0; i < model->face_count; ++i )
        if( model->face_textures[i] >= 0 )
            return 1;
    return 0;
}

static int
scene_add_model(
    struct Scene* scene,
    const char* path,
    size_t ordinal)
{
    size_t byte_count;
    uint8_t* bytes = read_file(path, &byte_count);
    struct RSCache_Model* decoded;
    struct SceneModel* dst;
    uint32_t padded;

    if( !bytes )
        return 0;
    decoded = RSCache_ModelNewDecode(bytes, (int)byte_count);
    free(bytes);
    if( !decoded )
        return 0;
    if( decoded->vertex_count <= 0 || decoded->vertex_count > 8192 )
    {
        RSCache_ModelFree(decoded);
        return 0;
    }

    scene->models =
        (struct SceneModel*)realloc(scene->models, (scene->count + 1) * sizeof(*scene->models));
    assert(scene->models);
    dst = &scene->models[scene->count];
    memset(dst, 0, sizeof(*dst));
    dst->count = (uint32_t)decoded->vertex_count;
    padded = (dst->count + 3u) & ~3u;
    dst->padded_count = padded;
    dst->x = (int16_t*)malloc((size_t)padded * sizeof(*dst->x));
    dst->y = (int16_t*)malloc((size_t)padded * sizeof(*dst->y));
    dst->z = (int16_t*)malloc((size_t)padded * sizeof(*dst->z));
    assert(dst->x);
    assert(dst->y);
    assert(dst->z);

    for( uint32_t i = 0; i < dst->count; ++i )
    {
        dst->x[i] = reference_vertex(decoded->vertices_x[i], decoded->format_version);
        dst->y[i] = reference_vertex(decoded->vertices_y[i], decoded->format_version);
        dst->z[i] = reference_vertex(decoded->vertices_z[i], decoded->format_version);
    }
    for( uint32_t i = dst->count; i < padded; ++i )
    {
        dst->x[i] = dst->x[dst->count - 1];
        dst->y[i] = dst->y[dst->count - 1];
        dst->z[i] = dst->z[dst->count - 1];
    }

    dst->textured = model_is_textured(decoded);
    /* A deterministic camera-space placement distribution.  Geometry and
       model sizes are the decoded scene assets; only placement is synthesized
       so the benchmark does not depend on async client boot order. */
    dst->model_yaw = (ordinal % 5u == 0u) ? 0 : (int)((ordinal * 97u) & 2047u);
    dst->camera_pitch = 160 + (int)((ordinal % 5u) * 24u);
    dst->camera_yaw = (int)((ordinal % 3u) * 24u);
    dst->scene_x = ((int)(ordinal % 13u) - 6) * 96;
    dst->scene_y = ((int)(ordinal % 7u) - 3) * 32;
    dst->scene_z = 900 + (int)((ordinal % 17u) * 112u);
    dst->model_mid_z = dst->scene_z;
    dst->near_plane_z = 50;
    dst->camera_cot16 = 1 << 16; /* reference scale 512 */

    if( dst->textured )
    {
        scene->tex_models++;
        scene->tex_vertices += dst->count;
    }
    else
    {
        scene->notex_models++;
        scene->notex_vertices += dst->count;
    }
    if( padded > scene->max_padded_count )
        scene->max_padded_count = padded;
    scene->count++;
    RSCache_ModelFree(decoded);
    return 1;
}

static void
scene_load(
    struct Scene* scene,
    const char* model_dir,
    size_t wanted)
{
    glob_t paths;
    char pattern[1024];
    size_t stride;
    size_t loaded = 0;

    memset(scene, 0, sizeof(*scene));
    if( snprintf(pattern, sizeof(pattern), "%s/model_*.model", model_dir) >= (int)sizeof(pattern) )
        die("model directory path is too long");
    memset(&paths, 0, sizeof(paths));
    if( glob(pattern, 0, NULL, &paths) != 0 || paths.gl_pathc == 0 )
        die("no model_*.model archives found (pass --models DIR)");

    /* Spread the sample over the full content corpus instead of selecting a
       lexicographic prefix with unusually similar ids. */
    stride = paths.gl_pathc / wanted;
    if( stride < 1 )
        stride = 1;
    for( size_t i = 0; i < paths.gl_pathc && loaded < wanted; i += stride )
        if( scene_add_model(scene, paths.gl_pathv[i], i) )
            loaded++;
    globfree(&paths);

    if( scene->count == 0 )
        die("none of the model archives decoded");
    if( scene->tex_models == 0 || scene->notex_models == 0 )
        die("model sample must contain both textured and untextured models");
}

static void
scene_free(struct Scene* scene)
{
    for( size_t i = 0; i < scene->count; ++i )
    {
        free(scene->models[i].x);
        free(scene->models[i].y);
        free(scene->models[i].z);
    }
    free(scene->models);
    memset(scene, 0, sizeof(*scene));
}

static void
buffers_alloc(
    struct Buffers* buffers,
    uint32_t capacity)
{
    size_t bytes = (size_t)capacity * sizeof(int32_t);
    memset(buffers, 0, sizeof(*buffers));
    buffers->capacity = capacity;
    buffers->ox = (int32_t*)malloc(bytes);
    buffers->oy = (int32_t*)malloc(bytes);
    buffers->oz = (int32_t*)malloc(bytes);
    buffers->sx = (int32_t*)malloc(bytes);
    buffers->sy = (int32_t*)malloc(bytes);
    buffers->sz = (int32_t*)malloc(bytes);
    assert(buffers->ox);
    assert(buffers->oy);
    assert(buffers->oz);
    assert(buffers->sx);
    assert(buffers->sy);
    assert(buffers->sz);
}

static void
buffers_free(struct Buffers* buffers)
{
    free(buffers->ox);
    free(buffers->oy);
    free(buffers->oz);
    free(buffers->sx);
    free(buffers->sy);
    free(buffers->sz);
    memset(buffers, 0, sizeof(*buffers));
}

static void
prepare_assembly_args(
    struct SceneModel* model,
    struct Buffers* buffers)
{
    struct ProjectionNeonSceneArgs* args = &model->assembly_args;
    memset(args, 0, sizeof(*args));
    args->ortho_x = buffers->ox;
    args->ortho_y = buffers->oy;
    args->ortho_z = buffers->oz;
    args->screen_x = buffers->sx;
    args->screen_y = buffers->sy;
    args->screen_z = buffers->sz;
    args->vertex_x = model->x;
    args->vertex_y = model->y;
    args->vertex_z = model->z;
    args->vertex_count = model->count;
    args->cos_model_yaw16 = ToriDraw_ReadCosTable(model->model_yaw);
    args->sin_model_yaw16 = ToriDraw_ReadSinTable(model->model_yaw);
    args->cos_camera_yaw16 = ToriDraw_ReadCosTable(model->camera_yaw);
    args->sin_camera_yaw16 = ToriDraw_ReadSinTable(model->camera_yaw);
    args->cos_camera_pitch16 = ToriDraw_ReadCosTable(model->camera_pitch);
    args->sin_camera_pitch16 = ToriDraw_ReadSinTable(model->camera_pitch);
    args->scene_x = model->scene_x;
    args->scene_y = model->scene_y;
    args->scene_z = model->scene_z;
    args->camera_cot15 = model->camera_cot16 >> 1;
    args->model_mid_z = model->model_mid_z;
    args->near_plane_z = model->near_plane_z;
    args->model_yaw_nonzero = model->model_yaw != 0;
}

#if defined(__clang__) || defined(__GNUC__)
#define NOINLINE __attribute__((noinline))
#else
#define NOINLINE
#endif

static NOINLINE void
run_current(
    enum Variant variant,
    const struct SceneModel* model,
    struct Buffers* out)
{
    int n = (int)model->count;
    vertexint_t* vx = (vertexint_t*)model->x;
    vertexint_t* vy = (vertexint_t*)model->y;
    vertexint_t* vz = (vertexint_t*)model->z;

    switch( variant )
    {
    case VAR_FUSED_TEX:
        project_vertices_array_fused_clip(
            out->ox,
            out->oy,
            out->oz,
            out->sx,
            out->sy,
            out->sz,
            vx,
            vy,
            vz,
            n,
            model->model_yaw,
            model->model_mid_z,
            model->scene_x,
            model->scene_y,
            model->scene_z,
            model->near_plane_z,
            model->camera_cot16,
            model->camera_pitch,
            model->camera_yaw);
        break;
    case VAR_UNFUSED_TEX:
        project_vertices_array_clip(
            out->ox,
            out->oy,
            out->oz,
            out->sx,
            out->sy,
            out->sz,
            vx,
            vy,
            vz,
            n,
            model->model_yaw,
            model->model_mid_z,
            model->scene_x,
            model->scene_y,
            model->scene_z,
            model->near_plane_z,
            model->camera_cot16,
            model->camera_pitch,
            model->camera_yaw);
        break;
    case VAR_FUSED_NOTEX:
        project_vertices_array_fused_notex_clip(
            out->sx,
            out->sy,
            out->sz,
            vx,
            vy,
            vz,
            n,
            model->model_yaw,
            model->model_mid_z,
            model->scene_x,
            model->scene_y,
            model->scene_z,
            model->near_plane_z,
            model->camera_cot16,
            model->camera_pitch,
            model->camera_yaw);
        break;
    case VAR_UNFUSED_NOTEX:
        project_vertices_array_notex_clip(
            out->sx,
            out->sy,
            out->sz,
            vx,
            vy,
            vz,
            n,
            model->model_yaw,
            model->model_mid_z,
            model->scene_x,
            model->scene_y,
            model->scene_z,
            model->near_plane_z,
            model->camera_cot16,
            model->camera_pitch,
            model->camera_yaw);
        break;
    default:
        assert(0);
    }
}

static NOINLINE void
run_assembly(
    enum Variant variant,
    const struct SceneModel* model,
    struct Buffers* out)
{
    const struct ProjectionNeonSceneArgs* args = &model->assembly_args;
    assert(args->screen_x == out->sx);
    switch( variant )
    {
    case VAR_FUSED_TEX:
        project_vertices_asm_fused_tex(args);
        break;
    case VAR_UNFUSED_TEX:
        project_vertices_asm_unfused_tex(args);
        break;
    case VAR_FUSED_NOTEX:
        project_vertices_asm_fused_notex(args);
        break;
    case VAR_UNFUSED_NOTEX:
        project_vertices_asm_unfused_notex(args);
        break;
    default:
        assert(0);
    }
}

static int
variant_accepts(
    enum Variant variant,
    const struct SceneModel* model)
{
    int wants_tex = variant == VAR_FUSED_TEX || variant == VAR_UNFUSED_TEX;
    return wants_tex == model->textured;
}

static void
compare_array(
    const char* label,
    const int32_t* expected,
    const int32_t* actual,
    uint32_t count,
    size_t model_index,
    enum Variant variant)
{
    for( uint32_t i = 0; i < count; ++i )
    {
        if( expected[i] != actual[i] )
        {
            fprintf(
                stderr,
                "mismatch %s model=%zu vertex=%u variant=%s current=%d asm=%d\n",
                label,
                model_index,
                i,
                variant_name[variant],
                expected[i],
                actual[i]);
            exit(2);
        }
    }
}

static void
validate_scene(
    const struct Scene* scene,
    struct Buffers* current,
    struct Buffers* assembly,
    enum Variant variant)
{
    int tex = variant == VAR_FUSED_TEX || variant == VAR_UNFUSED_TEX;
    for( size_t i = 0; i < scene->count; ++i )
    {
        const struct SceneModel* model = &scene->models[i];
        if( !variant_accepts(variant, model) )
            continue;
        memset(current->ox, 0x55, (size_t)current->capacity * sizeof(int32_t));
        memset(current->oy, 0x55, (size_t)current->capacity * sizeof(int32_t));
        memset(current->oz, 0x55, (size_t)current->capacity * sizeof(int32_t));
        memset(current->sx, 0x55, (size_t)current->capacity * sizeof(int32_t));
        memset(current->sy, 0x55, (size_t)current->capacity * sizeof(int32_t));
        memset(current->sz, 0x55, (size_t)current->capacity * sizeof(int32_t));
        memset(assembly->ox, 0x66, (size_t)assembly->capacity * sizeof(int32_t));
        memset(assembly->oy, 0x66, (size_t)assembly->capacity * sizeof(int32_t));
        memset(assembly->oz, 0x66, (size_t)assembly->capacity * sizeof(int32_t));
        memset(assembly->sx, 0x66, (size_t)assembly->capacity * sizeof(int32_t));
        memset(assembly->sy, 0x66, (size_t)assembly->capacity * sizeof(int32_t));
        memset(assembly->sz, 0x66, (size_t)assembly->capacity * sizeof(int32_t));
        run_current(variant, model, current);
        run_assembly(variant, model, assembly);
        if( tex )
        {
            compare_array("ortho_x", current->ox, assembly->ox, model->count, i, variant);
            compare_array("ortho_y", current->oy, assembly->oy, model->count, i, variant);
            compare_array("ortho_z", current->oz, assembly->oz, model->count, i, variant);
        }
        compare_array("screen_x", current->sx, assembly->sx, model->count, i, variant);
        compare_array("screen_y", current->sy, assembly->sy, model->count, i, variant);
        compare_array("screen_z", current->sz, assembly->sz, model->count, i, variant);
    }
}

static double
time_scene(
    const struct Scene* scene,
    struct Buffers* out,
    enum Variant variant,
    int repetitions,
    int assembly)
{
    double begin = now_seconds();
    for( int rep = 0; rep < repetitions; ++rep )
    {
        for( size_t i = 0; i < scene->count; ++i )
        {
            const struct SceneModel* model = &scene->models[i];
            if( !variant_accepts(variant, model) )
                continue;
            if( assembly )
                run_assembly(variant, model, out);
            else
                run_current(variant, model, out);
            bench_sink += (uint32_t)out->sx[model->count - 1];
            bench_sink += (uint32_t)out->sz[0];
        }
    }
    return now_seconds() - begin;
}

static int
compare_double(
    const void* a,
    const void* b)
{
    double aa = *(const double*)a;
    double bb = *(const double*)b;
    return (aa > bb) - (aa < bb);
}

static void
benchmark_variant(
    const struct Scene* scene,
    struct Buffers* current,
    struct Buffers* assembly,
    enum Variant variant,
    int repetitions,
    int samples)
{
    double current_times[31];
    double assembly_times[31];
    uint64_t vertices = (variant == VAR_FUSED_TEX || variant == VAR_UNFUSED_TEX)
                            ? scene->tex_vertices
                            : scene->notex_vertices;
    size_t models = (variant == VAR_FUSED_TEX || variant == VAR_UNFUSED_TEX) ? scene->tex_models
                                                                             : scene->notex_models;

    assert(samples > 0 && samples <= 31);
    for( int i = 0; i < 3; ++i )
    {
        (void)time_scene(scene, current, variant, 1, 0);
        (void)time_scene(scene, assembly, variant, 1, 1);
    }
    for( int sample = 0; sample < samples; ++sample )
    {
        if( sample & 1 )
        {
            assembly_times[sample] = time_scene(scene, assembly, variant, repetitions, 1);
            current_times[sample] = time_scene(scene, current, variant, repetitions, 0);
        }
        else
        {
            current_times[sample] = time_scene(scene, current, variant, repetitions, 0);
            assembly_times[sample] = time_scene(scene, assembly, variant, repetitions, 1);
        }
    }
    qsort(current_times, (size_t)samples, sizeof(double), compare_double);
    qsort(assembly_times, (size_t)samples, sizeof(double), compare_double);
    {
        double cur = current_times[samples / 2];
        double as = assembly_times[samples / 2];
        double invocations = (double)models * repetitions;
        double projected = (double)vertices * repetitions;
        printf(
            "%-16s %5zu models %9" PRIu64 " verts | current %7.3f ns/v %7.1f ns/model"
            " | asm %7.3f ns/v %7.1f ns/model | %5.2fx\n",
            variant_name[variant],
            models,
            vertices,
            cur * 1e9 / projected,
            cur * 1e9 / invocations,
            as * 1e9 / projected,
            as * 1e9 / invocations,
            cur / as);
    }
}

static void
usage(const char* argv0)
{
    fprintf(
        stderr,
        "usage: %s [--models DIR] [--model-count N] [--repetitions N] [--samples N]\n",
        argv0);
}

int
main(
    int argc,
    char** argv)
{
    const char* model_dir = "../../OSRS-Content/osrs239-content/models";
    size_t model_count = 256;
    int repetitions = 100;
    int samples = 9;
    struct Scene scene;
    struct Buffers current;
    struct Buffers assembly;

    for( int i = 1; i < argc; ++i )
    {
        if( strcmp(argv[i], "--models") == 0 && i + 1 < argc )
            model_dir = argv[++i];
        else if( strcmp(argv[i], "--model-count") == 0 && i + 1 < argc )
            model_count = (size_t)strtoul(argv[++i], NULL, 10);
        else if( strcmp(argv[i], "--repetitions") == 0 && i + 1 < argc )
            repetitions = atoi(argv[++i]);
        else if( strcmp(argv[i], "--samples") == 0 && i + 1 < argc )
            samples = atoi(argv[++i]);
        else
        {
            usage(argv[0]);
            return 1;
        }
    }
    if( model_count == 0 || repetitions <= 0 || samples <= 0 || samples > 31 )
        die("counts must be positive and samples must be <= 31");

    ToriDraw_InitSinTable();
    ToriDraw_InitCosTable();
    ToriDraw_InitTanTable();
    scene_load(&scene, model_dir, model_count);
    buffers_alloc(&current, scene.max_padded_count);
    buffers_alloc(&assembly, scene.max_padded_count);
    for( size_t i = 0; i < scene.count; ++i )
        prepare_assembly_args(&scene.models[i], &assembly);

    printf(
        "OSRS real-model scene: %zu calls, %zu textured/%zu untextured, "
        "%" PRIu64 " vertices\n",
        scene.count,
        scene.tex_models,
        scene.notex_models,
        scene.tex_vertices + scene.notex_vertices);
    printf("validation: current production NEON == hand-written AArch64");
    for( int variant = 0; variant < VAR_COUNT; ++variant )
    {
        validate_scene(&scene, &current, &assembly, (enum Variant)variant);
        printf(".");
        fflush(stdout);
    }
    printf(" PASS\n");
    printf("median of %d paired samples, %d scene repetitions/sample\n", samples, repetitions);
    for( int variant = 0; variant < VAR_COUNT; ++variant )
        benchmark_variant(&scene, &current, &assembly, (enum Variant)variant, repetitions, samples);
    printf("checksum: 0x%016" PRIx64 "\n", bench_sink);

    buffers_free(&assembly);
    buffers_free(&current);
    scene_free(&scene);
    return 0;
}
