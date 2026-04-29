/*
 * Microbenchmark: bake SoA / SoAOS -> interleaved TRSPK_VertexMetal (same pattern as
 * trspk_vertex_buffer_bake_array_to_interleaved_vertices in trspk_vertex_buffer_simd.scalar.u.c).
 *
 * - "scalar flat SoA": separate streams + per-vertex index i (old layout style).
 * - "soaos": TRSPK_VertexMetalSoaos blocks + block/lane indexing.
 *
 * From this directory:
 *   make && ./bench_vertex_soaos_scalar
 */

#include "../../src/platforms/ToriRSPlatformKit/include/ToriRSPlatformKit/trspk_types.h"
#include "../../src/platforms/ToriRSPlatformKit/src/backends/metal/metal_vertex.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef BENCH_VERTS
#define BENCH_VERTS 4194304u
#endif

#ifndef BENCH_PASSES
#define BENCH_PASSES 24u
#endif

#ifndef BENCH_WARMUP
#define BENCH_WARMUP 2u
#endif

/** Mirrors trspk_bake_transform_apply (trspk_math.h) — avoids pulling dash/uv headers. */
static inline void
bench_bake_transform_apply(
    const TRSPK_BakeTransform* bake,
    float* vx,
    float* vy,
    float* vz)
{
    if( !bake || bake->identity )
        return;
    const float lx = *vx;
    const float lz = *vz;
    *vx = lx * bake->cos_yaw + lz * bake->sin_yaw + bake->tx;
    *vy = *vy + bake->ty;
    *vz = -lx * bake->sin_yaw + lz * bake->cos_yaw + bake->tz;
}

static double
now_seconds(void)
{
    struct timespec ts;
    if( clock_gettime(CLOCK_MONOTONIC, &ts) != 0 )
        return 0.0;
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

typedef struct BenchFlatSoaMetal
{
    float* position_x;
    float* position_y;
    float* position_z;
    float* position_w;
    float* color_r;
    float* color_g;
    float* color_b;
    float* color_a;
    float* texcoord_u;
    float* texcoord_v;
    uint16_t* tex_id;
    uint16_t* uv_mode;
} BenchFlatSoaMetal;

static void
bench_flat_soa_metal_free(BenchFlatSoaMetal* a)
{
    if( !a )
        return;
    free(a->position_x);
    free(a->position_y);
    free(a->position_z);
    free(a->position_w);
    free(a->color_r);
    free(a->color_g);
    free(a->color_b);
    free(a->color_a);
    free(a->texcoord_u);
    free(a->texcoord_v);
    free(a->tex_id);
    free(a->uv_mode);
    memset(a, 0, sizeof(*a));
}

static bool
bench_flat_soa_metal_alloc(BenchFlatSoaMetal* out, uint32_t n)
{
    if( !out )
        return false;
    memset(out, 0, sizeof(*out));
    if( n == 0u )
        return true;
    const size_t nf = (size_t)n * sizeof(float);
    const size_t nu = (size_t)n * sizeof(uint16_t);
    out->position_x = (float*)malloc(nf);
    out->position_y = (float*)malloc(nf);
    out->position_z = (float*)malloc(nf);
    out->position_w = (float*)malloc(nf);
    out->color_r = (float*)malloc(nf);
    out->color_g = (float*)malloc(nf);
    out->color_b = (float*)malloc(nf);
    out->color_a = (float*)malloc(nf);
    out->texcoord_u = (float*)malloc(nf);
    out->texcoord_v = (float*)malloc(nf);
    out->tex_id = (uint16_t*)malloc(nu);
    out->uv_mode = (uint16_t*)malloc(nu);
    if( !out->position_x || !out->position_y || !out->position_z || !out->position_w ||
        !out->color_r || !out->color_g || !out->color_b || !out->color_a ||
        !out->texcoord_u || !out->texcoord_v || !out->tex_id || !out->uv_mode )
    {
        bench_flat_soa_metal_free(out);
        return false;
    }
    return true;
}

static void
bench_init_flat_soa(uint32_t n, BenchFlatSoaMetal* a)
{
    for( uint32_t i = 0; i < n; ++i )
    {
        const float t = (float)i * (1.0f / 65536.0f);
        a->position_x[i] = t;
        a->position_y[i] = t * 1.001f;
        a->position_z[i] = t * 0.999f;
        a->position_w[i] = 1.0f;
        a->color_r[i] = 0.25f + t * 0.1f;
        a->color_g[i] = 0.5f;
        a->color_b[i] = 0.75f;
        a->color_a[i] = 1.0f;
        a->texcoord_u[i] = t - floorf(t);
        a->texcoord_v[i] = 1.0f - (t - floorf(t));
        a->tex_id[i] = (uint16_t)(i & 0xFFFFu);
        a->uv_mode[i] = (uint16_t)((i >> 8) & 0xFFFFu);
    }
}

/** Copy flat SoA into SoAOS blocks (same values as trspk_metal_vertex_soaos_from_trspk). */
static void
bench_flat_to_soaos(uint32_t n, const BenchFlatSoaMetal* src, TRSPK_VertexMetalSoaos* dst)
{
    for( uint32_t i = 0; i < n; ++i )
    {
        const uint32_t bi = TRSPK_VERTEX_SOAOS_BLOCK_IDX(i);
        const uint32_t li = TRSPK_VERTEX_SOAOS_LANE_IDX(i);
        TRSPK_VertexMetalSoaosBlock* blk = &dst->blocks[bi];
        blk->position_x[li] = src->position_x[i];
        blk->position_y[li] = src->position_y[i];
        blk->position_z[li] = src->position_z[i];
        blk->position_w[li] = src->position_w[i];
        blk->color_r[li] = src->color_r[i];
        blk->color_g[li] = src->color_g[i];
        blk->color_b[li] = src->color_b[i];
        blk->color_a[li] = src->color_a[i];
        blk->texcoord_u[li] = src->texcoord_u[i];
        blk->texcoord_v[li] = src->texcoord_v[i];
        blk->tex_id[li] = src->tex_id[i];
        blk->uv_mode[li] = src->uv_mode[i];
    }
}

/** Same control flow as METAL_SOAOS branch in trspk_vertex_buffer_simd.scalar.u.c */
static void
bench_bake_soaos_to_interleaved(
    uint32_t n,
    const TRSPK_VertexMetalSoaos* a,
    const TRSPK_BakeTransform* bake,
    TRSPK_VertexMetal* dst)
{
    for( uint32_t i = 0; i < n; ++i )
    {
        const uint32_t bi = TRSPK_VERTEX_SOAOS_BLOCK_IDX(i);
        const uint32_t li = TRSPK_VERTEX_SOAOS_LANE_IDX(i);
        const TRSPK_VertexMetalSoaosBlock* blk = &a->blocks[bi];
        float x = blk->position_x[li];
        float y = blk->position_y[li];
        float z = blk->position_z[li];
        bench_bake_transform_apply(bake, &x, &y, &z);
        TRSPK_VertexMetal* v = &dst[i];
        v->position[0] = x;
        v->position[1] = y;
        v->position[2] = z;
        v->position[3] = blk->position_w[li];
        v->color[0] = blk->color_r[li];
        v->color[1] = blk->color_g[li];
        v->color[2] = blk->color_b[li];
        v->color[3] = blk->color_a[li];
        v->texcoord[0] = blk->texcoord_u[li];
        v->texcoord[1] = blk->texcoord_v[li];
        v->tex_id = blk->tex_id[li];
        v->uv_mode = blk->uv_mode[li];
    }
}

/** Flat SoA streams with per-vertex index — same writes as above, different loads. */
static void
bench_bake_flat_soa_to_interleaved(
    uint32_t n,
    const BenchFlatSoaMetal* a,
    const TRSPK_BakeTransform* bake,
    TRSPK_VertexMetal* dst)
{
    for( uint32_t i = 0; i < n; ++i )
    {
        float x = a->position_x[i];
        float y = a->position_y[i];
        float z = a->position_z[i];
        bench_bake_transform_apply(bake, &x, &y, &z);
        TRSPK_VertexMetal* v = &dst[i];
        v->position[0] = x;
        v->position[1] = y;
        v->position[2] = z;
        v->position[3] = a->position_w[i];
        v->color[0] = a->color_r[i];
        v->color[1] = a->color_g[i];
        v->color[2] = a->color_b[i];
        v->color[3] = a->color_a[i];
        v->texcoord[0] = a->texcoord_u[i];
        v->texcoord[1] = a->texcoord_v[i];
        v->tex_id = a->tex_id[i];
        v->uv_mode = a->uv_mode[i];
    }
}

int
main(void)
{
    const uint32_t n = (uint32_t)BENCH_VERTS;
    if( n == 0u )
    {
        fprintf(stderr, "BENCH_VERTS must be non-zero.\n");
        return 1;
    }

    TRSPK_BakeTransform bake;
    bake.cos_yaw = 0.9238795f;
    bake.sin_yaw = 0.3826834f;
    bake.tx = 12.3f;
    bake.ty = -4.0f;
    bake.tz = 7.0f;
    bake.identity = false;

    BenchFlatSoaMetal flat;
    if( !bench_flat_soa_metal_alloc(&flat, n) )
    {
        fprintf(stderr, "oom flat soa\n");
        return 1;
    }
    bench_init_flat_soa(n, &flat);

    TRSPK_VertexMetalSoaos soaos;
    if( !trspk_metal_vertex_soaos_alloc(&soaos, n) )
    {
        fprintf(stderr, "oom soaos\n");
        bench_flat_soa_metal_free(&flat);
        return 1;
    }
    bench_flat_to_soaos(n, &flat, &soaos);

    TRSPK_VertexMetal* dst_flat = (TRSPK_VertexMetal*)malloc((size_t)n * sizeof(TRSPK_VertexMetal));
    TRSPK_VertexMetal* dst_soaos = (TRSPK_VertexMetal*)malloc((size_t)n * sizeof(TRSPK_VertexMetal));
    if( !dst_flat || !dst_soaos )
    {
        fprintf(stderr, "oom dst\n");
        free(dst_flat);
        free(dst_soaos);
        trspk_metal_vertex_soaos_free(&soaos);
        bench_flat_soa_metal_free(&flat);
        return 1;
    }

    volatile float sink = 0.0f;

    for( uint32_t w = 0; w < (uint32_t)BENCH_WARMUP; ++w )
    {
        bench_bake_flat_soa_to_interleaved(n, &flat, &bake, dst_flat);
        sink += dst_flat[n / 2u].position[0];
    }

    double t_flat0 = now_seconds();
    for( uint32_t p = 0; p < (uint32_t)BENCH_PASSES; ++p )
        bench_bake_flat_soa_to_interleaved(n, &flat, &bake, dst_flat);
    double t_flat1 = now_seconds();

    for( uint32_t w = 0; w < (uint32_t)BENCH_WARMUP; ++w )
    {
        bench_bake_soaos_to_interleaved(n, &soaos, &bake, dst_soaos);
        sink += dst_soaos[n / 2u].position[0];
    }

    double t_soaos0 = now_seconds();
    for( uint32_t p = 0; p < (uint32_t)BENCH_PASSES; ++p )
        bench_bake_soaos_to_interleaved(n, &soaos, &bake, dst_soaos);
    double t_soaos1 = now_seconds();

    sink += dst_flat[0].tex_id + dst_soaos[0].tex_id;
    (void)sink;

    free(dst_flat);
    free(dst_soaos);
    trspk_metal_vertex_soaos_free(&soaos);
    bench_flat_soa_metal_free(&flat);

    const double flat_s = t_flat1 - t_flat0;
    const double soaos_s = t_soaos1 - t_soaos0;
    const double verts_total = (double)n * (double)BENCH_PASSES;

    printf(
        "vertex_soaos_scalar (bake -> TRSPK_VertexMetal): verts=%u  lanes=%u  shift=%u  "
        "passes=%u\n",
        (unsigned)n,
        (unsigned)TRSPK_VERTEX_SIMD_LANES,
        (unsigned)TRSPK_VERTEX_SOAOS_BLOCK_SHIFT,
        (unsigned)BENCH_PASSES);
    printf(
        "  flat SoA (per-vertex i):  time=%.6f s  %.3f ns/vert\n",
        flat_s,
        flat_s > 0.0 ? (flat_s * 1e9) / verts_total : 0.0);
    printf(
        "  SoAOS (block+lane):       time=%.6f s  %.3f ns/vert\n",
        soaos_s,
        soaos_s > 0.0 ? (soaos_s * 1e9) / verts_total : 0.0);
    if( soaos_s > 0.0 && flat_s > 0.0 )
        printf("  ratio (flat / soaos):     %.3f\n", flat_s / soaos_s);

    return 0;
}
