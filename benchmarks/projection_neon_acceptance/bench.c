#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <time.h>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

#if !defined(__aarch64__)
#error "projection_neon_acceptance must be built for AArch64"
#endif

#if (!defined(__ARM_NEON) && !defined(__ARM_NEON__)) || defined(NEON_DISABLED)
#error "projection_neon_acceptance requires the production NEON backend"
#endif

#ifndef HAVE_TORIDRAW_PROJECTION16_APPLE_ASM
#define HAVE_TORIDRAW_PROJECTION16_APPLE_ASM 0
#endif

/* Pull in the production selector first: on AArch64 this is the NEON path. */
#include "graphics/projection16_simd.u.c"

/*
 * Pull in the scalar reference under private names.  Keeping both definitions
 * in this translation unit lets the acceptance checks call the same public
 * fused entry points with no copied projection arithmetic.
 */
#define project_vertices_array_clip accept_scalar_project_vertices_array_clip
#define project_vertices_array_noclip accept_scalar_project_vertices_array_noclip
#define project_vertices_array_notex_clip accept_scalar_project_vertices_array_notex_clip
#define project_vertices_array_notex_noclip accept_scalar_project_vertices_array_notex_noclip
#define project_vertices_array_fused_clip accept_scalar_project_vertices_array_fused_clip
#define project_vertices_array_fused_noclip accept_scalar_project_vertices_array_fused_noclip
#define project_vertices_array_fused_notex_clip accept_scalar_project_vertices_array_fused_notex_clip
#define project_vertices_array_fused_notex_noclip accept_scalar_project_vertices_array_fused_notex_noclip
#include "graphics/projection16_simd.scalar.u.c"
#undef project_vertices_array_clip
#undef project_vertices_array_noclip
#undef project_vertices_array_notex_clip
#undef project_vertices_array_notex_noclip
#undef project_vertices_array_fused_clip
#undef project_vertices_array_fused_noclip
#undef project_vertices_array_fused_notex_clip
#undef project_vertices_array_fused_notex_noclip

enum
{
    MAX_VERTICES = 9,
    GUARD_LANES = 4,
    GUARDED_LANES = GUARD_LANES + MAX_VERTICES + GUARD_LANES,
    SCREEN_LIMIT = 8192,
    RANDOM_CASES = 256,
    MAX_SAMPLES = 31
};

struct ProjectionCase
{
    char name[64];
    vertexint_t x[MAX_VERTICES];
    vertexint_t y[MAX_VERTICES];
    vertexint_t z[MAX_VERTICES];
    int model_yaw;
    int model_mid_z;
    int scene_x;
    int scene_y;
    int scene_z;
    int near_plane_z;
    int camera_cot16;
    int camera_pitch;
    int camera_yaw;
};

struct GuardedVertices
{
    vertexint_t x[GUARDED_LANES];
    vertexint_t y[GUARDED_LANES];
    vertexint_t z[GUARDED_LANES];
};

struct GuardedInt
{
    int lane[GUARDED_LANES];
};

struct GuardedOutputs
{
    struct GuardedInt ox;
    struct GuardedInt oy;
    struct GuardedInt oz;
    struct GuardedInt sx;
    struct GuardedInt sy;
    struct GuardedInt sz;
};

struct Options
{
    uint64_t iterations;
    uint64_t warmup;
    int samples;
    bool run_acceptance;
    bool run_benchmark;
};

static volatile uint64_t benchmark_sink;
static volatile int benchmark_vertex_count = 4;

#if HAVE_TORIDRAW_PROJECTION16_APPLE_ASM
/* Keep this test-only view synchronized with the private renderer ABI: six
 * output pointers followed immediately by five 16-byte camera splats. */
struct NativeProjectionBlock
{
    int* screen_x;
    int* screen_y;
    int* screen_z;
    int* ortho_x;
    int* ortho_y;
    int* ortho_z;
    _Alignas(16) int cos_camera_yaw[4];
    int sin_camera_yaw[4];
    int cos_camera_pitch[4];
    int sin_camera_pitch[4];
    int cot15[4];
    /* Written back by the generic paths: lane-wise min x, max x, min y,
     * max y over every full block (projection16.aarch64.S BOUND_INIT). Sized
     * and placed as ToriDraw_Scene.projection_bound is. */
    _Alignas(16) int bound[4][4];
};

struct NativePosition
{
    int x;
    int y;
    int z;
    int pitch;
    int yaw;
    int roll;
};

_Static_assert(sizeof(void*) == 8, "native assembly requires 64-bit pointers");
_Static_assert(offsetof(struct NativeProjectionBlock, screen_x) == 0, "native screen-x offset");
_Static_assert(offsetof(struct NativeProjectionBlock, ortho_z) == 40, "native ortho-z offset");
_Static_assert(
    offsetof(struct NativeProjectionBlock, cos_camera_yaw) == 48,
    "native prepared-camera offset");
_Static_assert(offsetof(struct NativeProjectionBlock, bound) == 128, "native bound offset");
_Static_assert(sizeof(struct NativeProjectionBlock) == 192, "native projection-block size");

extern void
toridraw_project_vertices_fused_neon_noclip_native_prepared_aarch64(
    int* const* output_pointer_block,
    const vertexint_t* vertex_x,
    const vertexint_t* vertex_y,
    const vertexint_t* vertex_z,
    int num_vertices,
    int model_yaw,
    int model_mid_z,
    const struct NativePosition* position);

extern void
toridraw_project_vertices_fused_neon_notex_noclip_native_prepared_aarch64(
    int* const* output_pointer_block,
    const vertexint_t* vertex_x,
    const vertexint_t* vertex_y,
    const vertexint_t* vertex_z,
    int num_vertices,
    int model_yaw,
    int model_mid_z,
    const struct NativePosition* position);
#endif

static vertexint_t*
guarded_vertex_data(vertexint_t lane[GUARDED_LANES])
{
    return &lane[GUARD_LANES];
}

static int*
guarded_int_data(struct GuardedInt* buffer)
{
    return &buffer->lane[GUARD_LANES];
}

static void
prepare_vertices(struct GuardedVertices* vertices, const struct ProjectionCase* test_case, int salt)
{
    for( int i = 0; i < GUARDED_LANES; i++ )
    {
        vertices->x[i] = (vertexint_t)(0x5100 + salt * 37 + i);
        vertices->y[i] = (vertexint_t)(0x6100 + salt * 41 + i);
        vertices->z[i] = (vertexint_t)(0x7100 + salt * 43 + i);
    }

    memcpy(guarded_vertex_data(vertices->x), test_case->x, sizeof(test_case->x));
    memcpy(guarded_vertex_data(vertices->y), test_case->y, sizeof(test_case->y));
    memcpy(guarded_vertex_data(vertices->z), test_case->z, sizeof(test_case->z));
}

static void
prepare_outputs(struct GuardedOutputs* outputs, int salt)
{
    struct GuardedInt* fields[] = {
        &outputs->ox, &outputs->oy, &outputs->oz, &outputs->sx, &outputs->sy, &outputs->sz
    };

    for( int field = 0; field < 6; field++ )
    {
        for( int i = 0; i < GUARDED_LANES; i++ )
            fields[field]->lane[i] = 0x51000000 + salt * 0x00100000 + field * 0x00010000 + i * 17;
    }
}

#if HAVE_TORIDRAW_PROJECTION16_APPLE_ASM
static void
prepare_native_projection_block(
    struct NativeProjectionBlock* block,
    struct GuardedOutputs* outputs,
    const struct ProjectionCase* c)
{
    memset(block, 0, sizeof(*block));
    block->screen_x = guarded_int_data(&outputs->sx);
    block->screen_y = guarded_int_data(&outputs->sy);
    block->screen_z = guarded_int_data(&outputs->sz);
    block->ortho_x = guarded_int_data(&outputs->ox);
    block->ortho_y = guarded_int_data(&outputs->oy);
    block->ortho_z = guarded_int_data(&outputs->oz);

    int cos_camera_yaw = ToriDraw_ReadCosTable(c->camera_yaw);
    int sin_camera_yaw = ToriDraw_ReadSinTable(c->camera_yaw);
    int cos_camera_pitch = ToriDraw_ReadCosTable(c->camera_pitch);
    int sin_camera_pitch = ToriDraw_ReadSinTable(c->camera_pitch);
    int cot15 = c->camera_cot16 >> 1;
    for( int lane = 0; lane < 4; lane++ )
    {
        block->cos_camera_yaw[lane] = cos_camera_yaw;
        block->sin_camera_yaw[lane] = sin_camera_yaw;
        block->cos_camera_pitch[lane] = cos_camera_pitch;
        block->sin_camera_pitch[lane] = sin_camera_pitch;
        block->cot15[lane] = cot15;
    }
}

static void
call_textured_native(
    struct GuardedOutputs* out, struct GuardedVertices* in, int count, const struct ProjectionCase* c)
{
    struct NativeProjectionBlock block;
    const struct NativePosition position = {
        .x = c->scene_x,
        .y = c->scene_y,
        .z = c->scene_z,
        .pitch = 0,
        .yaw = 0,
        .roll = 0,
    };
    prepare_native_projection_block(&block, out, c);
    toridraw_project_vertices_fused_neon_noclip_native_prepared_aarch64(
        &block.screen_x,
        guarded_vertex_data(in->x),
        guarded_vertex_data(in->y),
        guarded_vertex_data(in->z),
        count,
        c->model_yaw,
        c->model_mid_z,
        &position);
}

static void
call_untextured_native(
    struct GuardedOutputs* out, struct GuardedVertices* in, int count, const struct ProjectionCase* c)
{
    struct NativeProjectionBlock block;
    const struct NativePosition position = {
        .x = c->scene_x,
        .y = c->scene_y,
        .z = c->scene_z,
        .pitch = 0,
        .yaw = 0,
        .roll = 0,
    };
    prepare_native_projection_block(&block, out, c);
    toridraw_project_vertices_fused_neon_notex_noclip_native_prepared_aarch64(
        &block.screen_x,
        guarded_vertex_data(in->x),
        guarded_vertex_data(in->y),
        guarded_vertex_data(in->z),
        count,
        c->model_yaw,
        c->model_mid_z,
        &position);
}
#endif

static void
call_textured_neon(
    struct GuardedOutputs* out, struct GuardedVertices* in, int count, const struct ProjectionCase* c)
{
    project_vertices_array_fused_noclip(
        guarded_int_data(&out->ox),
        guarded_int_data(&out->oy),
        guarded_int_data(&out->oz),
        guarded_int_data(&out->sx),
        guarded_int_data(&out->sy),
        guarded_int_data(&out->sz),
        guarded_vertex_data(in->x),
        guarded_vertex_data(in->y),
        guarded_vertex_data(in->z),
        count,
        c->model_yaw,
        c->model_mid_z,
        c->scene_x,
        c->scene_y,
        c->scene_z,
        c->near_plane_z,
        c->camera_cot16,
        c->camera_pitch,
        c->camera_yaw);
}

static void
call_textured_scalar(
    struct GuardedOutputs* out, struct GuardedVertices* in, int count, const struct ProjectionCase* c)
{
    accept_scalar_project_vertices_array_fused_noclip(
        guarded_int_data(&out->ox),
        guarded_int_data(&out->oy),
        guarded_int_data(&out->oz),
        guarded_int_data(&out->sx),
        guarded_int_data(&out->sy),
        guarded_int_data(&out->sz),
        guarded_vertex_data(in->x),
        guarded_vertex_data(in->y),
        guarded_vertex_data(in->z),
        count,
        c->model_yaw,
        c->model_mid_z,
        c->scene_x,
        c->scene_y,
        c->scene_z,
        c->near_plane_z,
        c->camera_cot16,
        c->camera_pitch,
        c->camera_yaw);
}

static void
call_untextured_neon(
    struct GuardedOutputs* out, struct GuardedVertices* in, int count, const struct ProjectionCase* c)
{
    project_vertices_array_fused_notex_noclip(
        guarded_int_data(&out->sx),
        guarded_int_data(&out->sy),
        guarded_int_data(&out->sz),
        guarded_vertex_data(in->x),
        guarded_vertex_data(in->y),
        guarded_vertex_data(in->z),
        count,
        c->model_yaw,
        c->model_mid_z,
        c->scene_x,
        c->scene_y,
        c->scene_z,
        c->near_plane_z,
        c->camera_cot16,
        c->camera_pitch,
        c->camera_yaw);
}

static void
call_untextured_scalar(
    struct GuardedOutputs* out, struct GuardedVertices* in, int count, const struct ProjectionCase* c)
{
    accept_scalar_project_vertices_array_fused_notex_noclip(
        guarded_int_data(&out->sx),
        guarded_int_data(&out->sy),
        guarded_int_data(&out->sz),
        guarded_vertex_data(in->x),
        guarded_vertex_data(in->y),
        guarded_vertex_data(in->z),
        count,
        c->model_yaw,
        c->model_mid_z,
        c->scene_x,
        c->scene_y,
        c->scene_z,
        c->near_plane_z,
        c->camera_cot16,
        c->camera_pitch,
        c->camera_yaw);
}

static bool
check_buffer_canary(
    const struct GuardedInt* actual,
    const struct GuardedInt* initial,
    int count,
    const char* implementation,
    const char* field,
    const struct ProjectionCase* c,
    bool field_is_written)
{
    for( int i = 0; i < GUARDED_LANES; i++ )
    {
        bool active = field_is_written && i >= GUARD_LANES && i < GUARD_LANES + count;
        if( !active && actual->lane[i] != initial->lane[i] )
        {
            fprintf(
                stderr,
                "FAIL %s count=%d %s changed %s lane %d outside the active range: %d -> %d\n",
                c->name,
                count,
                implementation,
                field,
                i - GUARD_LANES,
                initial->lane[i],
                actual->lane[i]);
            return false;
        }
    }
    return true;
}

static bool
check_output_canaries(
    const struct GuardedOutputs* actual,
    const struct GuardedOutputs* initial,
    int count,
    const char* implementation,
    const struct ProjectionCase* c,
    bool textured)
{
    const struct GuardedInt* actual_fields[] = {
        &actual->ox, &actual->oy, &actual->oz, &actual->sx, &actual->sy, &actual->sz
    };
    const struct GuardedInt* initial_fields[] = {
        &initial->ox, &initial->oy, &initial->oz, &initial->sx, &initial->sy, &initial->sz
    };
    const char* names[] = { "ortho_x", "ortho_y", "ortho_z", "screen_x", "screen_y", "screen_z" };

    for( int field = 0; field < 6; field++ )
    {
        bool written = textured || field >= 3;
        if( !check_buffer_canary(
                actual_fields[field],
                initial_fields[field],
                count,
                implementation,
                names[field],
                c,
                written) )
            return false;
    }
    return true;
}

static bool
check_inputs_unchanged(
    const struct GuardedVertices* actual,
    const struct GuardedVertices* initial,
    const char* implementation,
    const struct ProjectionCase* c,
    int count)
{
    if( memcmp(actual, initial, sizeof(*actual)) != 0 )
    {
        fprintf(
            stderr,
            "FAIL %s count=%d %s modified an input or input canary\n",
            c->name,
            count,
            implementation);
        return false;
    }
    return true;
}

static bool
check_exact_field(
    const struct GuardedInt* scalar,
    const struct GuardedInt* neon,
    int count,
    const char* field,
    const struct ProjectionCase* c,
    const char* variant)
{
    const int* scalar_data = &scalar->lane[GUARD_LANES];
    const int* neon_data = &neon->lane[GUARD_LANES];
    for( int i = 0; i < count; i++ )
    {
        if( scalar_data[i] != neon_data[i] )
        {
            fprintf(
                stderr,
                "FAIL %s %s count=%d lane=%d %s scalar=%d neon=%d (exact required)\n",
                c->name,
                variant,
                count,
                i,
                field,
                scalar_data[i],
                neon_data[i]);
            return false;
        }
    }
    return true;
}

static bool
check_screen_field(
    const struct GuardedInt* scalar,
    const struct GuardedInt* neon,
    int count,
    const char* field,
    const struct ProjectionCase* c,
    const char* variant)
{
    const int* scalar_data = &scalar->lane[GUARD_LANES];
    const int* neon_data = &neon->lane[GUARD_LANES];
    int vector_lanes = count & ~3;

    for( int i = 0; i < count; i++ )
    {
        long long delta = (long long)neon_data[i] - (long long)scalar_data[i];
        long long allowed = i < vector_lanes ? 4 : 0;
        if( llabs(delta) > allowed )
        {
            fprintf(
                stderr,
                "FAIL %s %s count=%d lane=%d %s scalar=%d neon=%d delta=%lld allowed=%lld (%s lane)\n",
                c->name,
                variant,
                count,
                i,
                field,
                scalar_data[i],
                neon_data[i],
                delta,
                allowed,
                i < vector_lanes ? "vector" : "residual");
            return false;
        }
    }
    return true;
}

static bool
check_scalar_domain(
    const struct GuardedOutputs* scalar,
    int count,
    const struct ProjectionCase* c,
    const char* variant,
    bool textured)
{
    const int* sx = &scalar->sx.lane[GUARD_LANES];
    const int* sy = &scalar->sy.lane[GUARD_LANES];
    const int* sz = &scalar->sz.lane[GUARD_LANES];
    const int* oz = &scalar->oz.lane[GUARD_LANES];

    for( int i = 0; i < count; i++ )
    {
        int z = textured ? oz[i] : sz[i] + c->model_mid_z;
        if( z < c->near_plane_z )
        {
            fprintf(
                stderr,
                "FAIL harness case %s %s count=%d lane=%d violates no-clip input: z=%d near=%d\n",
                c->name,
                variant,
                count,
                i,
                z,
                c->near_plane_z);
            return false;
        }
        if( llabs((long long)sx[i]) > SCREEN_LIMIT || llabs((long long)sy[i]) > SCREEN_LIMIT )
        {
            fprintf(
                stderr,
                "FAIL harness case %s %s count=%d lane=%d is outside the acceptance domain: (%d,%d)\n",
                c->name,
                variant,
                count,
                i,
                sx[i],
                sy[i]);
            return false;
        }
    }
    return true;
}

static bool
run_case_variant(const struct ProjectionCase* c, int count, bool textured)
{
    struct GuardedVertices scalar_in;
    struct GuardedVertices neon_in;
    struct GuardedVertices scalar_initial;
    struct GuardedVertices neon_initial;
    struct GuardedOutputs scalar_out;
    struct GuardedOutputs neon_out;
    struct GuardedOutputs scalar_out_initial;
    struct GuardedOutputs neon_out_initial;
    const char* variant = textured ? "textured" : "untextured";

    prepare_vertices(&scalar_in, c, 1);
    prepare_vertices(&neon_in, c, 2);
    scalar_initial = scalar_in;
    neon_initial = neon_in;
    prepare_outputs(&scalar_out, 1);
    prepare_outputs(&neon_out, 2);
    scalar_out_initial = scalar_out;
    neon_out_initial = neon_out;

    if( textured )
    {
        call_textured_scalar(&scalar_out, &scalar_in, count, c);
        call_textured_neon(&neon_out, &neon_in, count, c);
    }
    else
    {
        call_untextured_scalar(&scalar_out, &scalar_in, count, c);
        call_untextured_neon(&neon_out, &neon_in, count, c);
    }

    if( !check_inputs_unchanged(&scalar_in, &scalar_initial, "scalar", c, count) ||
        !check_inputs_unchanged(&neon_in, &neon_initial, "NEON", c, count) ||
        !check_output_canaries(&scalar_out, &scalar_out_initial, count, "scalar", c, textured) ||
        !check_output_canaries(&neon_out, &neon_out_initial, count, "NEON", c, textured) ||
        !check_scalar_domain(&scalar_out, count, c, variant, textured) )
        return false;

    if( textured &&
        (!check_exact_field(&scalar_out.ox, &neon_out.ox, count, "ortho_x", c, variant) ||
         !check_exact_field(&scalar_out.oy, &neon_out.oy, count, "ortho_y", c, variant) ||
         !check_exact_field(&scalar_out.oz, &neon_out.oz, count, "ortho_z", c, variant)) )
        return false;

    if( !check_exact_field(&scalar_out.sz, &neon_out.sz, count, "screen_z", c, variant) ||
        !check_screen_field(&scalar_out.sx, &neon_out.sx, count, "screen_x", c, variant) ||
        !check_screen_field(&scalar_out.sy, &neon_out.sy, count, "screen_y", c, variant) )
        return false;

    return true;
}

#if HAVE_TORIDRAW_PROJECTION16_APPLE_ASM
static bool
run_native_case_variant(const struct ProjectionCase* c, int count, bool textured)
{
    struct GuardedVertices scalar_in;
    struct GuardedVertices native_in;
    struct GuardedVertices scalar_initial;
    struct GuardedVertices native_initial;
    struct GuardedOutputs scalar_out;
    struct GuardedOutputs native_out;
    struct GuardedOutputs scalar_out_initial;
    struct GuardedOutputs native_out_initial;
    const char* variant = textured ? "native textured" : "native untextured";

    prepare_vertices(&scalar_in, c, 3);
    prepare_vertices(&native_in, c, 4);
    scalar_initial = scalar_in;
    native_initial = native_in;
    prepare_outputs(&scalar_out, 3);
    prepare_outputs(&native_out, 4);
    scalar_out_initial = scalar_out;
    native_out_initial = native_out;

    if( textured )
    {
        call_textured_scalar(&scalar_out, &scalar_in, count, c);
        call_textured_native(&native_out, &native_in, count, c);
    }
    else
    {
        call_untextured_scalar(&scalar_out, &scalar_in, count, c);
        call_untextured_native(&native_out, &native_in, count, c);
    }

    if( !check_inputs_unchanged(&scalar_in, &scalar_initial, "scalar", c, count) ||
        !check_inputs_unchanged(&native_in, &native_initial, "native Apple assembly", c, count) ||
        !check_output_canaries(&scalar_out, &scalar_out_initial, count, "scalar", c, textured) ||
        !check_output_canaries(
            &native_out,
            &native_out_initial,
            count,
            "native Apple assembly",
            c,
            textured) ||
        !check_scalar_domain(&scalar_out, count, c, variant, textured) )
        return false;

    if( textured &&
        (!check_exact_field(&scalar_out.ox, &native_out.ox, count, "ortho_x", c, variant) ||
         !check_exact_field(&scalar_out.oy, &native_out.oy, count, "ortho_y", c, variant) ||
         !check_exact_field(&scalar_out.oz, &native_out.oz, count, "ortho_z", c, variant)) )
        return false;

    if( !check_exact_field(&scalar_out.sz, &native_out.sz, count, "screen_z", c, variant) ||
        !check_screen_field(&scalar_out.sx, &native_out.sx, count, "screen_x", c, variant) ||
        !check_screen_field(&scalar_out.sy, &native_out.sy, count, "screen_y", c, variant) )
        return false;

    return true;
}

static bool
run_native_case_all_counts(const struct ProjectionCase* c)
{
    for( int count = 0; count <= MAX_VERTICES; count++ )
    {
        if( !run_native_case_variant(c, count, true) ||
            !run_native_case_variant(c, count, false) )
            return false;
    }
    return true;
}
#endif

static bool
run_case_all_counts(const struct ProjectionCase* c)
{
    if( c->model_yaw == 0 )
    {
        fprintf(stderr, "FAIL harness case %s does not select the hot model-yaw branch\n", c->name);
        return false;
    }

    for( int count = 0; count <= MAX_VERTICES; count++ )
    {
        if( !run_case_variant(c, count, true) || !run_case_variant(c, count, false) )
            return false;
    }
    return true;
}

static bool
run_one_shared_tail_helper(bool textured, bool clipped, int rem)
{
    const int base = 3;
    const int model_mid_z = 17;
    const int near_plane_z = 50;
    struct GuardedInt orthographic_z;
    struct GuardedInt sx;
    struct GuardedInt sy;
    struct GuardedInt sz;
    struct GuardedInt orthographic_z_initial;
    struct GuardedInt sx_initial;
    struct GuardedInt sy_initial;
    struct GuardedInt sz_initial;
    int* z_data = guarded_int_data(&orthographic_z);
    int* sx_data = guarded_int_data(&sx);
    int* sy_data = guarded_int_data(&sy);
    int* sz_data = guarded_int_data(&sz);
    const char* helper = textured ?
        (clipped ? "projection_neon_zdiv_tex_tail_clip" : "projection_neon_zdiv_tex_tail_noclip") :
        (clipped ? "projection_neon_zdiv_notex_tail_clip" : "projection_neon_zdiv_notex_tail_noclip");

    for( int i = 0; i < GUARDED_LANES; i++ )
    {
        orthographic_z.lane[i] = 0x31000000 + i * 11;
        sx.lane[i] = 0x32000000 + i * 13;
        sy.lane[i] = 0x33000000 + i * 17;
        sz.lane[i] = 0x34000000 + i * 19;
    }
    for( int i = 0; i < MAX_VERTICES; i++ )
    {
        z_data[i] = 101 + i;
        sx_data[i] = 7000 + i * 101;
        sy_data[i] = -9000 - i * 103;
        sz_data[i] = z_data[i];
    }

    /* Known one-NR regression: an approximate reciprocal produced 8191 here. */
    z_data[base] = 50;
    sx_data[base] = 409600;
    sy_data[base] = -409599;
    sz_data[base] = z_data[base];
    z_data[base + 1] = 49;   /* clip path leaves scaled y untouched */
    sx_data[base + 1] = 12345;
    sy_data[base + 1] = -23456;
    sz_data[base + 1] = z_data[base + 1];
    z_data[base + 2] = 50;   /* -250000 / 50 exercises the -5000 nudge */
    sx_data[base + 2] = -250000;
    sy_data[base + 2] = 175001;
    sz_data[base + 2] = z_data[base + 2];

    orthographic_z_initial = orthographic_z;
    sx_initial = sx;
    sy_initial = sy;
    sz_initial = sz;

    if( textured )
    {
        if( clipped )
            projection_neon_zdiv_tex_tail_clip(
                z_data, sx_data, sy_data, sz_data, base, rem, model_mid_z, near_plane_z);
        else
            projection_neon_zdiv_tex_tail_noclip(
                z_data, sx_data, sy_data, sz_data, base, rem, model_mid_z, near_plane_z);
    }
    else
    {
        if( clipped )
            projection_neon_zdiv_notex_tail_clip(
                sx_data, sy_data, sz_data, base, rem, model_mid_z, near_plane_z);
        else
            projection_neon_zdiv_notex_tail_noclip(
                sx_data, sy_data, sz_data, base, rem, model_mid_z, near_plane_z);
    }

    if( memcmp(&orthographic_z, &orthographic_z_initial, sizeof(orthographic_z)) != 0 )
    {
        fprintf(stderr, "FAIL %s rem=%d modified its textured z input\n", helper, rem);
        return false;
    }

    for( int storage_i = 0; storage_i < GUARDED_LANES; storage_i++ )
    {
        int logical_i = storage_i - GUARD_LANES;
        bool active = logical_i >= base && logical_i < base + rem;
        if( !active &&
            (sx.lane[storage_i] != sx_initial.lane[storage_i] ||
             sy.lane[storage_i] != sy_initial.lane[storage_i] ||
             sz.lane[storage_i] != sz_initial.lane[storage_i]) )
        {
            fprintf(stderr, "FAIL %s rem=%d wrote outside [%d,%d) at lane %d\n", helper, rem, base, base + rem, logical_i);
            return false;
        }
    }

    for( int i = base; i < base + rem; i++ )
    {
        int z = textured ? z_data[i] : sz_initial.lane[GUARD_LANES + i];
        int input_x = sx_initial.lane[GUARD_LANES + i];
        int input_y = sy_initial.lane[GUARD_LANES + i];
        int expected_x;
        int expected_y;
        if( clipped && z < near_plane_z )
        {
            expected_x = TORIDRAW_SCREEN_X_NEAR_CLIPPED;
            expected_y = input_y;
        }
        else
        {
            expected_x = input_x / z;
            if( clipped && expected_x == TORIDRAW_SCREEN_X_NEAR_CLIPPED )
                expected_x = TORIDRAW_SCREEN_X_NEAR_CLIPPED_NUDGE;
            expected_y = input_y / z;
        }
        int expected_z = z - model_mid_z;

        if( sx_data[i] != expected_x || sy_data[i] != expected_y || sz_data[i] != expected_z )
        {
            fprintf(
                stderr,
                "FAIL %s rem=%d lane=%d expected=(%d,%d,%d) actual=(%d,%d,%d)\n",
                helper,
                rem,
                i,
                expected_x,
                expected_y,
                expected_z,
                sx_data[i],
                sy_data[i],
                sz_data[i]);
            return false;
        }
        if( !clipped && i == base && sx_data[i] != 8192 )
        {
            fprintf(
                stderr,
                "FAIL %s rem=%d known 409600/50 regression expected 8192, actual=%d\n",
                helper,
                rem,
                sx_data[i]);
            return false;
        }
    }
    return true;
}

static bool
run_shared_tail_helper_acceptance(void)
{
    for( int rem = 0; rem < 4; rem++ )
    {
        if( !run_one_shared_tail_helper(true, true, rem) ||
            !run_one_shared_tail_helper(true, false, rem) ||
            !run_one_shared_tail_helper(false, true, rem) ||
            !run_one_shared_tail_helper(false, false, rem) )
            return false;
    }
    return true;
}

static struct ProjectionCase
make_reciprocal_boundary_case(void)
{
    struct ProjectionCase c = { 0 };
    const vertexint_t x[MAX_VERTICES] = { 801, -801, 800, -800, 399, -399, 255, -255, 1 };
    const vertexint_t y[MAX_VERTICES] = { 800, -800, 799, -799, 401, -401, 254, -254, -1 };
    const vertexint_t z[MAX_VERTICES] = { 51, 51, 52, 52, 63, 63, 127, 127, 255 };

    snprintf(c.name, sizeof(c.name), "reciprocal-boundary");
    memcpy(c.x, x, sizeof(x));
    memcpy(c.y, y, sizeof(y));
    memcpy(c.z, z, sizeof(z));
    c.model_yaw = 1024;
    c.model_mid_z = 17;
    c.scene_x = 0;
    c.scene_y = 0;
    c.scene_z = 0;
    c.near_plane_z = 1;
    c.camera_cot16 = 65536;
    c.camera_pitch = 0;
    c.camera_yaw = 1024;
    return c;
}

static struct ProjectionCase
make_signed_tail_case(void)
{
    struct ProjectionCase c = { 0 };
    const vertexint_t x[MAX_VERTICES] = { 0, 1, -1, 49, -49, 127, -127, 511, -511 };
    const vertexint_t y[MAX_VERTICES] = { 1, -1, 50, -50, 126, -126, 255, -255, 0 };
    const vertexint_t z[MAX_VERTICES] = { 0, 1, 2, 7, 15, 31, 63, 127, 255 };

    snprintf(c.name, sizeof(c.name), "signed-tail-thresholds");
    memcpy(c.x, x, sizeof(x));
    memcpy(c.y, y, sizeof(y));
    memcpy(c.z, z, sizeof(z));
    c.model_yaw = 2047;
    c.model_mid_z = 53;
    c.scene_x = 0;
    c.scene_y = 0;
    c.scene_z = 257;
    c.near_plane_z = 50;
    c.camera_cot16 = 65536;
    c.camera_pitch = 0;
    c.camera_yaw = 1;
    return c;
}

#if HAVE_TORIDRAW_PROJECTION16_APPLE_ASM
static struct ProjectionCase
make_no_model_yaw_case(void)
{
    struct ProjectionCase c = { 0 };
    const vertexint_t x[MAX_VERTICES] = { -311, 127, 509, -73, 18, -255, 401, 92, -607 };
    const vertexint_t y[MAX_VERTICES] = { 93, -211, 47, 318, -407, 64, 159, -508, 21 };
    const vertexint_t z[MAX_VERTICES] = { 177, -91, 302, 55, -260, 411, -38, 129, 506 };

    snprintf(c.name, sizeof(c.name), "model-yaw-zero-no-rotation");
    memcpy(c.x, x, sizeof(x));
    memcpy(c.y, y, sizeof(y));
    memcpy(c.z, z, sizeof(z));
    c.model_yaw = 0;
    c.model_mid_z = 2517;
    c.scene_x = 173;
    c.scene_y = -219;
    c.scene_z = 2500;
    c.near_plane_z = 50;
    c.camera_cot16 = 65536;
    c.camera_pitch = 91;
    c.camera_yaw = 137;
    return c;
}
#endif

static uint32_t
lcg_next(uint32_t* state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static int
random_inclusive(uint32_t* state, int low, int high)
{
    uint32_t width = (uint32_t)(high - low + 1);
    return low + (int)(lcg_next(state) % width);
}

static struct ProjectionCase
make_random_case(uint32_t* state, int index)
{
    struct ProjectionCase c = { 0 };
    snprintf(c.name, sizeof(c.name), "random-%03d", index);

    for( int i = 0; i < MAX_VERTICES; i++ )
    {
        c.x[i] = (vertexint_t)random_inclusive(state, -768, 768);
        c.y[i] = (vertexint_t)random_inclusive(state, -768, 768);
        c.z[i] = (vertexint_t)random_inclusive(state, -768, 768);
    }

    c.model_yaw = random_inclusive(state, 1, 2047);
    c.scene_x = random_inclusive(state, -256, 256);
    c.scene_y = random_inclusive(state, -256, 256);
    c.scene_z = random_inclusive(state, 2200, 4095);
    c.model_mid_z = c.scene_z + random_inclusive(state, -64, 64);
    c.near_plane_z = 50;
    c.camera_cot16 = 65536;
    c.camera_pitch = random_inclusive(state, 0, 127);
    c.camera_yaw = random_inclusive(state, 1, 127);
    return c;
}

#if HAVE_TORIDRAW_PROJECTION16_APPLE_ASM
static bool
run_native_table_contracts(const struct ProjectionCase* no_yaw_case)
{
    enum
    {
        TRIG_TABLE_SIZE = 2048
    };
    const int* original_cos = ToriDraw_GetCosTable();
    const int* original_sin = ToriDraw_GetSinTable();
    int poisoned_cos[TRIG_TABLE_SIZE];
    int poisoned_sin[TRIG_TABLE_SIZE];
    memcpy(poisoned_cos, original_cos, sizeof(poisoned_cos));
    memcpy(poisoned_sin, original_sin, sizeof(poisoned_sin));

    /* A model_yaw of zero means no model rotation; neither implementation may
     * accidentally derive that path from the mutable table's entry zero. */
    poisoned_cos[0] = 12345;
    poisoned_sin[0] = -23456;
    ToriDraw_SetCosTable(poisoned_cos);
    ToriDraw_SetSinTable(poisoned_sin);
    bool ok = run_native_case_all_counts(no_yaw_case);

    /* A nonzero yaw must observe custom values through the assembly's derived
     * interleaved table. Reselecting the same pointers must refresh it after a
     * caller mutates the selected source tables. */
    struct ProjectionCase custom_case = *no_yaw_case;
    snprintf(custom_case.name, sizeof(custom_case.name), "custom-yaw-table-refresh");
    custom_case.model_yaw = 600;
    poisoned_cos[custom_case.model_yaw] = original_cos[777];
    poisoned_sin[custom_case.model_yaw] = original_sin[777];
    ToriDraw_SetCosTable(poisoned_cos);
    ToriDraw_SetSinTable(poisoned_sin);
    if( !run_native_case_all_counts(&custom_case) )
        ok = false;

    poisoned_cos[custom_case.model_yaw] = original_cos[913];
    poisoned_sin[custom_case.model_yaw] = original_sin[913];
    ToriDraw_SetCosTable(poisoned_cos);
    ToriDraw_SetSinTable(poisoned_sin);
    if( !run_native_case_all_counts(&custom_case) )
        ok = false;

    ToriDraw_SetCosTable(NULL);
    ToriDraw_SetSinTable(NULL);
    if( ToriDraw_GetCosTable() != original_cos || ToriDraw_GetSinTable() != original_sin )
    {
        fprintf(stderr, "FAIL custom trig-table NULL restoration\n");
        ok = false;
    }
    return ok;
}
#endif

static bool
run_acceptance(void)
{
    struct ProjectionCase boundary = make_reciprocal_boundary_case();
    struct ProjectionCase signed_tail = make_signed_tail_case();
#if HAVE_TORIDRAW_PROJECTION16_APPLE_ASM
    struct ProjectionCase no_model_yaw = make_no_model_yaw_case();
#endif
    uint32_t state = UINT32_C(0xc001d00d);

    if( !run_shared_tail_helper_acceptance() || !run_case_all_counts(&boundary) ||
        !run_case_all_counts(&signed_tail) )
        return false;

    for( int i = 0; i < RANDOM_CASES; i++ )
    {
        struct ProjectionCase random_case = make_random_case(&state, i);
        if( !run_case_all_counts(&random_case) )
            return false;
#if HAVE_TORIDRAW_PROJECTION16_APPLE_ASM
        if( !run_native_case_all_counts(&random_case) )
            return false;
#endif
    }

#if HAVE_TORIDRAW_PROJECTION16_APPLE_ASM
    if( !run_native_case_all_counts(&boundary) ||
        !run_native_case_all_counts(&signed_tail) ||
        !run_native_table_contracts(&no_model_yaw) )
        return false;
#endif

    printf(
        "acceptance: PASS (%d crafted + %d deterministic randomized cases; counts 0..%d; textured + untextured)\n",
        2,
        RANDOM_CASES,
        MAX_VERTICES);
    printf("shared z-div tails: PASS (tex/notex x clip/no-clip; rem 0..3; exact scalar semantics)\n");
#if HAVE_TORIDRAW_PROJECTION16_APPLE_ASM
    printf(
        "native Apple assembly: PASS (%d crafted + %d deterministic randomized cases; counts 0..%d; textured + untextured; yaw-table refresh/restore + model_yaw==0 independence)\n",
        3,
        RANDOM_CASES,
        MAX_VERTICES);
#else
    printf("native Apple assembly: SKIP (projection16.aarch64.S not linked on this target)\n");
#endif
    printf("acceptance seed: 0x%08" PRIx32 "\n", UINT32_C(0xc001d00d));
    return true;
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

#define COMPILER_BARRIER() __asm__ __volatile__("" ::: "memory")

#if defined(__clang__) || defined(__GNUC__)
#define BENCH_NOINLINE __attribute__((noinline))
#else
#define BENCH_NOINLINE
#endif

/*
 * Keep the production inline body behind a real call boundary.  Every
 * projection argument, including count, is a runtime ABI argument to these
 * wrappers so the compiler cannot specialize the hot body for this fixture.
 */
BENCH_NOINLINE void
bench_call_textured(
    int* ox,
    int* oy,
    int* oz,
    int* sx,
    int* sy,
    int* sz,
    vertexint_t* x,
    vertexint_t* y,
    vertexint_t* z,
    int count,
    int model_yaw,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_cot16,
    int camera_pitch,
    int camera_yaw)
{
    project_vertices_array_fused_noclip(
        ox,
        oy,
        oz,
        sx,
        sy,
        sz,
        x,
        y,
        z,
        count,
        model_yaw,
        model_mid_z,
        scene_x,
        scene_y,
        scene_z,
        near_plane_z,
        camera_cot16,
        camera_pitch,
        camera_yaw);
}

BENCH_NOINLINE void
bench_call_untextured(
    int* sx,
    int* sy,
    int* sz,
    vertexint_t* x,
    vertexint_t* y,
    vertexint_t* z,
    int count,
    int model_yaw,
    int model_mid_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int near_plane_z,
    int camera_cot16,
    int camera_pitch,
    int camera_yaw)
{
    project_vertices_array_fused_notex_noclip(
        sx,
        sy,
        sz,
        x,
        y,
        z,
        count,
        model_yaw,
        model_mid_z,
        scene_x,
        scene_y,
        scene_z,
        near_plane_z,
        camera_cot16,
        camera_pitch,
        camera_yaw);
}

static uint64_t
benchmark_textured(uint64_t iterations, const struct ProjectionCase* c)
{
    int ox[4];
    int oy[4];
    int oz[4];
    int sx[4];
    int sy[4];
    int sz[4];
    vertexint_t x[4] = { c->x[0], c->x[1], c->x[2], c->x[3] };
    vertexint_t y[4] = { c->y[0], c->y[1], c->y[2], c->y[3] };
    vertexint_t z[4] = { c->z[0], c->z[1], c->z[2], c->z[3] };
    int count = benchmark_vertex_count;

    uint64_t start = monotonic_ns();
    for( uint64_t i = 0; i < iterations; i++ )
    {
        bench_call_textured(
            ox,
            oy,
            oz,
            sx,
            sy,
            sz,
            x,
            y,
            z,
            count,
            c->model_yaw,
            c->model_mid_z,
            c->scene_x,
            c->scene_y,
            c->scene_z,
            c->near_plane_z,
            c->camera_cot16,
            c->camera_pitch,
            c->camera_yaw);
        COMPILER_BARRIER();
    }
    uint64_t elapsed = monotonic_ns() - start;

    benchmark_sink += (uint64_t)(uint32_t)ox[0] + (uint64_t)(uint32_t)oy[1] +
                      (uint64_t)(uint32_t)oz[2] + (uint64_t)(uint32_t)sx[3] +
                      (uint64_t)(uint32_t)sy[0] + (uint64_t)(uint32_t)sz[1];
    return elapsed;
}

static uint64_t
benchmark_untextured(uint64_t iterations, const struct ProjectionCase* c)
{
    int sx[4];
    int sy[4];
    int sz[4];
    vertexint_t x[4] = { c->x[0], c->x[1], c->x[2], c->x[3] };
    vertexint_t y[4] = { c->y[0], c->y[1], c->y[2], c->y[3] };
    vertexint_t z[4] = { c->z[0], c->z[1], c->z[2], c->z[3] };
    int count = benchmark_vertex_count;

    uint64_t start = monotonic_ns();
    for( uint64_t i = 0; i < iterations; i++ )
    {
        bench_call_untextured(
            sx,
            sy,
            sz,
            x,
            y,
            z,
            count,
            c->model_yaw,
            c->model_mid_z,
            c->scene_x,
            c->scene_y,
            c->scene_z,
            c->near_plane_z,
            c->camera_cot16,
            c->camera_pitch,
            c->camera_yaw);
        COMPILER_BARRIER();
    }
    uint64_t elapsed = monotonic_ns() - start;

    benchmark_sink += (uint64_t)(uint32_t)sx[0] + (uint64_t)(uint32_t)sy[1] +
                      (uint64_t)(uint32_t)sz[2];
    return elapsed;
}

static int
compare_double(const void* lhs, const void* rhs)
{
    double a = *(const double*)lhs;
    double b = *(const double*)rhs;
    return (a > b) - (a < b);
}

static struct ProjectionCase
make_benchmark_case(void)
{
    struct ProjectionCase c = { 0 };
    const vertexint_t x[MAX_VERTICES] = { -96, 112, 104, -88, 0, 0, 0, 0, 0 };
    const vertexint_t y[MAX_VERTICES] = { -64, -56, 96, 88, 0, 0, 0, 0, 0 };
    const vertexint_t z[MAX_VERTICES] = { -72, 80, 72, -64, 0, 0, 0, 0, 0 };

    snprintf(c.name, sizeof(c.name), "benchmark-4");
    memcpy(c.x, x, sizeof(x));
    memcpy(c.y, y, sizeof(y));
    memcpy(c.z, z, sizeof(z));
    c.model_yaw = 600;
    c.model_mid_z = 2048;
    c.scene_x = 96;
    c.scene_y = -48;
    c.scene_z = 2048;
    c.near_plane_z = 50;
    c.camera_cot16 = 65536;
    c.camera_pitch = 128;
    c.camera_yaw = 173;
    return c;
}

static void
run_benchmark(const struct Options* options)
{
    struct ProjectionCase c = make_benchmark_case();
    double textured[MAX_SAMPLES];
    double untextured[MAX_SAMPLES];

    if( benchmark_vertex_count != 4 )
    {
        fprintf(stderr, "internal benchmark count must be 4\n");
        exit(2);
    }

    (void)benchmark_textured(options->warmup, &c);
    (void)benchmark_untextured(options->warmup, &c);

    for( int sample = 0; sample < options->samples; sample++ )
    {
        uint64_t textured_elapsed;
        uint64_t untextured_elapsed;
        if( (sample & 1) == 0 )
        {
            textured_elapsed = benchmark_textured(options->iterations, &c);
            untextured_elapsed = benchmark_untextured(options->iterations, &c);
        }
        else
        {
            untextured_elapsed = benchmark_untextured(options->iterations, &c);
            textured_elapsed = benchmark_textured(options->iterations, &c);
        }
        textured[sample] = (double)textured_elapsed / (double)options->iterations;
        untextured[sample] = (double)untextured_elapsed / (double)options->iterations;
    }

    qsort(textured, (size_t)options->samples, sizeof(textured[0]), compare_double);
    qsort(untextured, (size_t)options->samples, sizeof(untextured[0]), compare_double);
    double textured_median = textured[options->samples / 2];
    double untextured_median = untextured[options->samples / 2];
    double weighted = 0.3551 * textured_median + 0.6449 * untextured_median;

    printf(
        "benchmark configuration: production NEON, 4 vertices/call, iterations=%" PRIu64
        ", warmup=%" PRIu64 ", samples=%d (median)\n",
        options->iterations,
        options->warmup,
        options->samples);
    printf("textured fused yaw-only no-clip:   %.3f ns/call\n", textured_median);
    printf("untextured fused yaw-only no-clip: %.3f ns/call\n", untextured_median);
    printf("weighted (35.51%% textured + 64.49%% untextured): %.3f ns/call\n", weighted);
    printf("benchmark checksum: 0x%016" PRIx64 "\n", benchmark_sink);
}

static void
print_environment(void)
{
    const char* cpu_override = getenv("BENCH_CPU_LABEL");
    char cpu_label[256] = { 0 };
    struct utsname host;
    if( uname(&host) == 0 )
        printf("host: %s %s (%s)\n", host.sysname, host.release, host.machine);
    else
        printf("host: uname unavailable\n");

    if( cpu_override != NULL && cpu_override[0] != '\0' )
    {
        snprintf(cpu_label, sizeof(cpu_label), "%s", cpu_override);
        printf("cpu: %s (BENCH_CPU_LABEL override)\n", cpu_label);
    }
    else
    {
#if defined(__APPLE__)
        size_t cpu_label_size = sizeof(cpu_label);
        if( sysctlbyname("machdep.cpu.brand_string", cpu_label, &cpu_label_size, NULL, 0) == 0 &&
            cpu_label[0] != '\0' )
            printf("cpu: %s\n", cpu_label);
        else
            printf("cpu: unavailable (set BENCH_CPU_LABEL)\n");
#elif defined(__linux__)
        FILE* cpuinfo = fopen("/proc/cpuinfo", "r");
        if( cpuinfo != NULL )
        {
            char line[512];
            while( fgets(line, sizeof(line), cpuinfo) != NULL )
            {
                if( strncmp(line, "model name", 10) == 0 || strncmp(line, "Model", 5) == 0 ||
                    strncmp(line, "Hardware", 8) == 0 )
                {
                    char* value = strchr(line, ':');
                    if( value != NULL )
                    {
                        value++;
                        while( *value == ' ' || *value == '\t' )
                            value++;
                        value[strcspn(value, "\r\n")] = '\0';
                        snprintf(cpu_label, sizeof(cpu_label), "%s", value);
                        break;
                    }
                }
            }
            fclose(cpuinfo);
        }
        if( cpu_label[0] != '\0' )
            printf("cpu: %s\n", cpu_label);
        else
            printf("cpu: unavailable (set BENCH_CPU_LABEL)\n");
#else
        printf("cpu: unavailable (set BENCH_CPU_LABEL)\n");
#endif
    }

#if defined(__clang__)
    printf("compiler: Clang %s\n", __clang_version__);
#elif defined(__GNUC__)
    printf("compiler: GCC %s\n", __VERSION__);
#else
    printf("compiler: unknown C compiler\n");
#endif
}

static void
print_usage(const char* argv0)
{
    printf(
        "usage: %s [--acceptance-only | --benchmark-only] [--iterations N] [--warmup N] [--samples N]\n",
        argv0);
}

static bool
parse_u64(const char* text, uint64_t* value)
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
    *options = (struct Options){
        .iterations = UINT64_C(2000000),
        .warmup = UINT64_C(100000),
        .samples = 7,
        .run_acceptance = true,
        .run_benchmark = true,
    };
    bool mode_selected = false;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--acceptance-only") == 0 || strcmp(argv[i], "--benchmark-only") == 0 )
        {
            if( mode_selected )
            {
                fprintf(stderr, "choose only one of --acceptance-only and --benchmark-only\n");
                return false;
            }
            mode_selected = true;
            options->run_acceptance = strcmp(argv[i], "--acceptance-only") == 0;
            options->run_benchmark = !options->run_acceptance;
        }
        else if( strcmp(argv[i], "--iterations") == 0 || strcmp(argv[i], "--warmup") == 0 )
        {
            if( i + 1 >= argc )
            {
                fprintf(stderr, "%s requires a positive integer\n", argv[i]);
                return false;
            }
            uint64_t value;
            if( !parse_u64(argv[++i], &value) )
            {
                fprintf(stderr, "invalid positive integer: %s\n", argv[i]);
                return false;
            }
            if( strcmp(argv[i - 1], "--iterations") == 0 )
                options->iterations = value;
            else
                options->warmup = value;
        }
        else if( strcmp(argv[i], "--samples") == 0 )
        {
            uint64_t value;
            if( i + 1 >= argc || !parse_u64(argv[++i], &value) || value > MAX_SAMPLES || (value & 1) == 0 )
            {
                fprintf(stderr, "--samples must be an odd integer from 1 through %d\n", MAX_SAMPLES);
                return false;
            }
            options->samples = (int)value;
        }
        else if( strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0 )
        {
            print_usage(argv[0]);
            exit(0);
        }
        else
        {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            return false;
        }
    }
    return true;
}

int
main(int argc, char** argv)
{
    struct Options options;
    if( !parse_options(argc, argv, &options) )
    {
        print_usage(argv[0]);
        return 2;
    }

    ToriDraw_InitSinTable();
    ToriDraw_InitCosTable();
    print_environment();

    if( options.run_acceptance && !run_acceptance() )
        return 1;
    if( options.run_benchmark )
        run_benchmark(&options);
    return 0;
}
