#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <time.h>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

#if !defined(__APPLE__) || !defined(__aarch64__)
#error "projection dispatch benchmark requires Apple arm64"
#endif
#if !defined(BENCH_PORTABLE_BASELINE) && !defined(TORIDRAW_APPLE_NEON_PROJECTION_ASM)
#error "projection dispatch benchmark requires the Apple projection assembly"
#endif

/* Keep the renderer wrapper static and visible to this translation unit. */
#include "toridraw_unity.c"

enum
{
    MAX_VERTICES = 63,
    GUARD_LANES = 4,
    TOTAL_LANES = GUARD_LANES + MAX_VERTICES + GUARD_LANES,
    MIX_SIZE = 1000,
    MIX_EXACT4 = 589,
    MIX_OTHER = 411,
    MAX_SAMPLES = 31
};

struct OutputBuffers
{
    int ox[TOTAL_LANES];
    int oy[TOTAL_LANES];
    int oz[TOTAL_LANES];
    int sx[TOTAL_LANES];
    int sy[TOTAL_LANES];
    int sz[TOTAL_LANES];
};

struct Fixture
{
    struct ToriDraw_Scene scene;
    struct ToriDraw_Model model;
    struct ToriDraw_ModelHandle handle;
    struct ToriDraw_Position position;
    struct ToriDraw_Camera camera;
    vertexint_t x[TOTAL_LANES];
    vertexint_t y[TOTAL_LANES];
    vertexint_t z[TOTAL_LANES];
    struct OutputBuffers outputs;
};

struct Options
{
    uint64_t iterations;
    uint64_t warmup;
    int samples;
};

static volatile uint64_t benchmark_sink;
static int mixed_counts[MIX_SIZE];

#if defined(__clang__) || defined(__GNUC__)
#define BENCH_NOINLINE __attribute__((noinline))
#else
#define BENCH_NOINLINE
#endif

#define COMPILER_BARRIER() __asm__ __volatile__("" ::: "memory")

static int*
active_int(int lanes[TOTAL_LANES])
{
    return &lanes[GUARD_LANES];
}

static vertexint_t*
active_vertex(vertexint_t lanes[TOTAL_LANES])
{
    return &lanes[GUARD_LANES];
}

static void
bind_outputs(struct Fixture* fixture)
{
    fixture->scene.screen_vertices_x = active_int(fixture->outputs.sx);
    fixture->scene.screen_vertices_y = active_int(fixture->outputs.sy);
    fixture->scene.screen_vertices_z = active_int(fixture->outputs.sz);
    fixture->scene.orthographic_vertices_x = active_int(fixture->outputs.ox);
    fixture->scene.orthographic_vertices_y = active_int(fixture->outputs.oy);
    fixture->scene.orthographic_vertices_z = active_int(fixture->outputs.oz);
}

static void
initialize_fixture(struct Fixture* fixture)
{
    static const vertexint_t shape_x[7] = { -96, 112, 104, -88, 37, -141, 73 };
    static const vertexint_t shape_y[7] = { -64, -56, 96, 88, -117, 42, 135 };
    static const vertexint_t shape_z[7] = { -72, 80, 72, -64, 151, -103, 29 };

    memset(fixture, 0, sizeof(*fixture));
    for( int lane = 0; lane < TOTAL_LANES; lane++ )
    {
        fixture->x[lane] = (vertexint_t)(0x5100 + lane * 13);
        fixture->y[lane] = (vertexint_t)(0x6100 + lane * 17);
        fixture->z[lane] = (vertexint_t)(0x7100 + lane * 19);
    }
    for( int lane = 0; lane < MAX_VERTICES; lane++ )
    {
        active_vertex(fixture->x)[lane] = shape_x[lane % 7];
        active_vertex(fixture->y)[lane] = shape_y[lane % 7];
        active_vertex(fixture->z)[lane] = shape_z[lane % 7];
    }

    fixture->model.vertex_count = MAX_VERTICES;
    fixture->model.vertices_x = active_vertex(fixture->x);
    fixture->model.vertices_y = active_vertex(fixture->y);
    fixture->model.vertices_z = active_vertex(fixture->z);
    fixture->handle.kind = TORIDRAWMK_MODEL;
    fixture->handle.u.model.model = &fixture->model;
    fixture->position = (struct ToriDraw_Position){ .x = 96, .y = -48, .z = 2048 };
    fixture->camera = (struct ToriDraw_Camera){
        .proj_mode = TORIDRAW_PROJ_MODE_SCALE,
        .proj_scale = TORIDRAW_PROJ_SCALE_DEFAULT,
        .near_plane_z = 50,
        .pitch = 128,
        .yaw = 173,
    };
    bind_outputs(fixture);
#if !defined(BENCH_PORTABLE_BASELINE)
    ToriDraw_ScenePrepareProjectionCamera(&fixture->scene, &fixture->camera);
#endif
}

static void
reset_outputs(struct OutputBuffers* outputs, int salt)
{
    int* fields[] = { outputs->ox, outputs->oy, outputs->oz, outputs->sx, outputs->sy, outputs->sz };
    for( int field = 0; field < 6; field++ )
    {
        for( int lane = 0; lane < TOTAL_LANES; lane++ )
            fields[field][lane] = 0x41000000 + salt * 0x01000000 + field * 0x00010000 + lane * 29;
    }
}

/* This is the measured production dispatch boundary. The always-inline static
 * renderer wrapper expands here, but every selector input remains a runtime
 * ABI argument to this non-inlined function. */
BENCH_NOINLINE static void
dispatch_call(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle handle,
    struct ToriDraw_Position* position,
    struct ToriDraw_Camera* camera,
    int model_pitch,
    int model_yaw,
    int model_roll,
    int camera_roll,
    int model_mid_z)
{
    toridraw_project_vertices_noclip(
        scene,
        handle,
        position,
        camera,
        model_pitch,
        model_yaw,
        model_roll,
        camera_roll,
        model_mid_z);
}

static void
select_path(struct Fixture* fixture, bool prepared)
{
#if defined(BENCH_PORTABLE_BASELINE)
    (void)fixture;
    (void)prepared;
#else
    if( prepared )
        ToriDraw_ScenePrepareProjectionCamera(&fixture->scene, &fixture->camera);
    else
        ToriDraw_SceneClearProjectionCamera(&fixture->scene);
#endif
}

static void
call_fixture(struct Fixture* fixture, int count, bool textured, bool prepared)
{
    fixture->model.vertex_count = count;
    fixture->model.textured_face_count = textured ? 1 : 0;
    select_path(fixture, prepared);
    dispatch_call(
        &fixture->scene,
        fixture->handle,
        &fixture->position,
        &fixture->camera,
        0,
        600,
        0,
        0,
        2048);
}

static bool
check_canaries(
    const struct OutputBuffers* actual,
    const struct OutputBuffers* initial,
    int count,
    bool textured,
    const char* path)
{
    const int* fields[] = { actual->ox, actual->oy, actual->oz, actual->sx, actual->sy, actual->sz };
    const int* before[] = { initial->ox, initial->oy, initial->oz, initial->sx, initial->sy, initial->sz };
    for( int field = 0; field < 6; field++ )
    {
        for( int storage_lane = 0; storage_lane < TOTAL_LANES; storage_lane++ )
        {
            int lane = storage_lane - GUARD_LANES;
            bool active = lane >= 0 && lane < count && (textured || field >= 3);
            if( !active && fields[field][storage_lane] != before[field][storage_lane] )
            {
                fprintf(stderr, "validation: %s wrote field=%d lane=%d outside active output\n", path, field, lane);
                return false;
            }
        }
    }
    return true;
}

static bool
compare_active(
    const struct OutputBuffers* portable,
    const struct OutputBuffers* prepared,
    int count,
    bool textured)
{
    const int* lhs[] = { portable->ox, portable->oy, portable->oz, portable->sx, portable->sy, portable->sz };
    const int* rhs[] = { prepared->ox, prepared->oy, prepared->oz, prepared->sx, prepared->sy, prepared->sz };
    int first_field = textured ? 0 : 3;
    int vector_lanes = count & ~3;
    for( int field = first_field; field < 6; field++ )
    {
        for( int lane = 0; lane < count; lane++ )
        {
            int a = lhs[field][GUARD_LANES + lane];
            int b = rhs[field][GUARD_LANES + lane];
            int allowed = (field == 3 || field == 4) && lane < vector_lanes ? 4 : 0;
            long long delta = (long long)b - (long long)a;
            if( llabs(delta) > allowed )
            {
                fprintf(
                    stderr,
                    "validation: count=%d %s field=%d lane=%d portable=%d prepared=%d allowed=%d\n",
                    count,
                    textured ? "textured" : "untextured",
                    field,
                    lane,
                    a,
                    b,
                    allowed);
                return false;
            }
        }
    }
    return true;
}

static bool
validate_dispatch(void)
{
    static const int counts[] = { 4, 5, 6, 7, 9, 15, 31, 63 };
    struct Fixture* portable = malloc(sizeof(*portable));
    struct Fixture* prepared = malloc(sizeof(*prepared));
    assert(portable);
    assert(prepared);
    initialize_fixture(portable);
    initialize_fixture(prepared);
    bool ok = true;

    for( int textured = 0; textured <= 1; textured++ )
    {
        for( size_t count_index = 0; count_index < sizeof(counts) / sizeof(counts[0]); count_index++ )
        {
            struct OutputBuffers portable_initial;
            struct OutputBuffers prepared_initial;
            vertexint_t portable_x_initial[TOTAL_LANES];
            vertexint_t portable_y_initial[TOTAL_LANES];
            vertexint_t portable_z_initial[TOTAL_LANES];
            vertexint_t prepared_x_initial[TOTAL_LANES];
            vertexint_t prepared_y_initial[TOTAL_LANES];
            vertexint_t prepared_z_initial[TOTAL_LANES];

            reset_outputs(&portable->outputs, 1);
            reset_outputs(&prepared->outputs, 2);
            portable_initial = portable->outputs;
            prepared_initial = prepared->outputs;
            memcpy(portable_x_initial, portable->x, sizeof(portable->x));
            memcpy(portable_y_initial, portable->y, sizeof(portable->y));
            memcpy(portable_z_initial, portable->z, sizeof(portable->z));
            memcpy(prepared_x_initial, prepared->x, sizeof(prepared->x));
            memcpy(prepared_y_initial, prepared->y, sizeof(prepared->y));
            memcpy(prepared_z_initial, prepared->z, sizeof(prepared->z));

            call_fixture(portable, counts[count_index], textured != 0, false);
            call_fixture(prepared, counts[count_index], textured != 0, true);
            if( !check_canaries(
                    &portable->outputs,
                    &portable_initial,
                    counts[count_index],
                    textured != 0,
                    "portable") ||
                !check_canaries(
                    &prepared->outputs,
                    &prepared_initial,
                    counts[count_index],
                    textured != 0,
                    "prepared") ||
                memcmp(portable->x, portable_x_initial, sizeof(portable->x)) != 0 ||
                memcmp(portable->y, portable_y_initial, sizeof(portable->y)) != 0 ||
                memcmp(portable->z, portable_z_initial, sizeof(portable->z)) != 0 ||
                memcmp(prepared->x, prepared_x_initial, sizeof(prepared->x)) != 0 ||
                memcmp(prepared->y, prepared_y_initial, sizeof(prepared->y)) != 0 ||
                memcmp(prepared->z, prepared_z_initial, sizeof(prepared->z)) != 0 ||
                !compare_active(
                    &portable->outputs,
                    &prepared->outputs,
                    counts[count_index],
                    textured != 0) )
            {
                ok = false;
                goto done;
            }
        }
    }

done:
    free(portable);
    free(prepared);
    if( ok )
#if defined(BENCH_PORTABLE_BASELINE)
        printf("dispatch validation: PASS (clean portable path repeated; exact4 + requested mixed counts; textured + untextured; canaries)\n");
#else
        printf("dispatch validation: PASS (portable vs prepared; exact4 + requested mixed counts; textured + untextured; canaries)\n");
#endif
    return ok;
}

static uint64_t
monotonic_ns(void)
{
    struct timespec ts;
    if( clock_gettime(CLOCK_MONOTONIC, &ts) != 0 )
    {
        perror("clock_gettime");
        exit(2);
    }
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static uint64_t
benchmark_path(
    struct Fixture* fixture,
    uint64_t iterations,
    bool prepared,
    bool textured,
    bool mixed)
{
    fixture->model.textured_face_count = textured ? 1 : 0;
    fixture->model.vertex_count = mixed ? mixed_counts[0] : 4;
    select_path(fixture, prepared);
    int mix_index = 0;
    uint64_t start = monotonic_ns();
    for( uint64_t iteration = 0; iteration < iterations; iteration++ )
    {
        if( mixed )
        {
            fixture->model.vertex_count = mixed_counts[mix_index++];
            if( mix_index == MIX_SIZE )
                mix_index = 0;
        }
        dispatch_call(
            &fixture->scene,
            fixture->handle,
            &fixture->position,
            &fixture->camera,
            0,
            600,
            0,
            0,
            2048);
        COMPILER_BARRIER();
    }
    uint64_t elapsed = monotonic_ns() - start;
    benchmark_sink +=
        (uint64_t)(uint32_t)active_int(fixture->outputs.sx)[0] +
        (uint64_t)(uint32_t)active_int(fixture->outputs.sy)[1] +
        (uint64_t)(uint32_t)active_int(fixture->outputs.sz)[2] +
        (uint64_t)(uint32_t)fixture->model.vertex_count;
    return elapsed;
}

static int
compare_double(const void* lhs, const void* rhs)
{
    double a = *(const double*)lhs;
    double b = *(const double*)rhs;
    return (a > b) - (a < b);
}

static void
run_workload(const struct Options* options, bool mixed)
{
    struct Fixture* fixture = malloc(sizeof(*fixture));
    double portable_textured[MAX_SAMPLES];
    double prepared_textured[MAX_SAMPLES];
    double portable_untextured[MAX_SAMPLES];
    double prepared_untextured[MAX_SAMPLES];
    assert(fixture);
    initialize_fixture(fixture);

    (void)benchmark_path(fixture, options->warmup, false, true, mixed);
    (void)benchmark_path(fixture, options->warmup, true, true, mixed);
    (void)benchmark_path(fixture, options->warmup, false, false, mixed);
    (void)benchmark_path(fixture, options->warmup, true, false, mixed);

    for( int sample = 0; sample < options->samples; sample++ )
    {
        uint64_t pt, at, pu, au;
        if( (sample & 1) == 0 )
        {
            pt = benchmark_path(fixture, options->iterations, false, true, mixed);
            at = benchmark_path(fixture, options->iterations, true, true, mixed);
            pu = benchmark_path(fixture, options->iterations, false, false, mixed);
            au = benchmark_path(fixture, options->iterations, true, false, mixed);
        }
        else
        {
            au = benchmark_path(fixture, options->iterations, true, false, mixed);
            pu = benchmark_path(fixture, options->iterations, false, false, mixed);
            at = benchmark_path(fixture, options->iterations, true, true, mixed);
            pt = benchmark_path(fixture, options->iterations, false, true, mixed);
        }
        portable_textured[sample] = (double)pt / (double)options->iterations;
        prepared_textured[sample] = (double)at / (double)options->iterations;
        portable_untextured[sample] = (double)pu / (double)options->iterations;
        prepared_untextured[sample] = (double)au / (double)options->iterations;
    }

    qsort(portable_textured, (size_t)options->samples, sizeof(double), compare_double);
    qsort(prepared_textured, (size_t)options->samples, sizeof(double), compare_double);
    qsort(portable_untextured, (size_t)options->samples, sizeof(double), compare_double);
    qsort(prepared_untextured, (size_t)options->samples, sizeof(double), compare_double);
    int median = options->samples / 2;
    double portable_weighted =
        0.3551 * portable_textured[median] + 0.6449 * portable_untextured[median];
#if !defined(BENCH_PORTABLE_BASELINE)
    double prepared_weighted =
        0.3551 * prepared_textured[median] + 0.6449 * prepared_untextured[median];
#endif
#if defined(BENCH_PORTABLE_BASELINE)
    printf(
        "%s clean baseline: tex=%.3f notex=%.3f weighted=%.3f ns/call\n",
        mixed ? "mixed589/411" : "exact4",
        portable_textured[median],
        portable_untextured[median],
        portable_weighted);
#else
    printf(
        "%s portable fallback: tex=%.3f notex=%.3f weighted=%.3f ns/call\n",
        mixed ? "mixed589/411" : "exact4",
        portable_textured[median],
        portable_untextured[median],
        portable_weighted);
    printf(
        "%s prepared: tex=%.3f notex=%.3f weighted=%.3f ns/call (%+.2f%%)\n",
        mixed ? "mixed589/411" : "exact4",
        prepared_textured[median],
        prepared_untextured[median],
        prepared_weighted,
        100.0 * (prepared_weighted / portable_weighted - 1.0));
#endif
    free(fixture);
}

static void
initialize_mixed_counts(void)
{
    static const int others[] = { 5, 6, 7, 9, 15, 31, 63 };
    for( int i = 0; i < MIX_EXACT4; i++ )
        mixed_counts[i] = 4;
    for( int i = 0; i < MIX_OTHER; i++ )
        mixed_counts[MIX_EXACT4 + i] = others[i % (int)(sizeof(others) / sizeof(others[0]))];

    uint32_t state = UINT32_C(0x589411a5);
    for( int i = MIX_SIZE - 1; i > 0; i-- )
    {
        state = state * UINT32_C(1664525) + UINT32_C(1013904223);
        int j = (int)(state % (uint32_t)(i + 1));
        int tmp = mixed_counts[i];
        mixed_counts[i] = mixed_counts[j];
        mixed_counts[j] = tmp;
    }
}

static void
print_environment(void)
{
    struct utsname host;
    char cpu[256] = { 0 };
    if( uname(&host) == 0 )
        printf("host: %s %s (%s)\n", host.sysname, host.release, host.machine);
    size_t cpu_size = sizeof(cpu);
    if( sysctlbyname("machdep.cpu.brand_string", cpu, &cpu_size, NULL, 0) == 0 )
        printf("cpu: %s\n", cpu);
#if defined(__clang__)
    printf("compiler: Clang %s\n", __clang_version__);
#endif
}

static bool
parse_positive(const char* text, uint64_t* value)
{
    char* end = NULL;
    errno = 0;
    unsigned long long parsed = strtoull(text, &end, 10);
    if( errno != 0 || end == text || *end != '\0' || parsed == 0 )
        return false;
    *value = (uint64_t)parsed;
    return true;
}

static bool
parse_options(int argc, char** argv, struct Options* options)
{
    *options = (struct Options){ .iterations = UINT64_C(2000000), .warmup = UINT64_C(200000), .samples = 9 };
    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--iterations") == 0 || strcmp(argv[i], "--warmup") == 0 )
        {
            uint64_t value;
            if( i + 1 >= argc || !parse_positive(argv[++i], &value) )
                return false;
            if( strcmp(argv[i - 1], "--iterations") == 0 )
                options->iterations = value;
            else
                options->warmup = value;
        }
        else if( strcmp(argv[i], "--samples") == 0 )
        {
            uint64_t value;
            if( i + 1 >= argc || !parse_positive(argv[++i], &value) || value > MAX_SAMPLES ||
                (value & 1) == 0 )
                return false;
            options->samples = (int)value;
        }
        else
            return false;
    }
    return true;
}

int
main(int argc, char** argv)
{
    struct Options options;
    if( !parse_options(argc, argv, &options) )
    {
        fprintf(stderr, "usage: %s [--iterations N] [--warmup N] [--samples odd-1..%d]\n", argv[0], MAX_SAMPLES);
        return 2;
    }

    ToriDraw_InitSinTable();
    ToriDraw_InitCosTable();
    initialize_mixed_counts();
    print_environment();
    if( !validate_dispatch() )
        return 1;
    run_workload(&options, false);
    run_workload(&options, true);
    printf("benchmark checksum: 0x%016" PRIx64 "\n", benchmark_sink);
    return 0;
}
