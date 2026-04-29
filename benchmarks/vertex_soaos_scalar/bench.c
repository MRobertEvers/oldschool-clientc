/*
 * Microbenchmark: flat SoA (three streams, per-vertex index i) vs AoSoA blocks
 * (TRSPK_VertexMetalSoaosBlock lanes), matching ToriRS SoAOS layout/alignment.
 *
 * From this directory:
 *   make && ./bench_vertex_soaos_scalar
 *
 * Optional:
 *   make BENCH_VERTS=8000000 BENCH_PASSES=16 BENCH_WARMUP=2
 */

#include "../../src/platforms/ToriRSPlatformKit/src/backends/metal/metal_vertex.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

static double
now_seconds(void)
{
    struct timespec ts;
    if( clock_gettime(CLOCK_MONOTONIC, &ts) != 0 )
        return 0.0;
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

/** Flat SoA: separate malloc per component (scalar-style indexing). */
static float*
alloc_f32(size_t n)
{
    void* p = malloc(n * sizeof(float));
    if( !p )
        return NULL;
    return (float*)p;
}

static void
init_flat_soa(
    uint32_t n,
    float* px,
    float* py,
    float* pz)
{
    for( uint32_t i = 0; i < n; ++i )
    {
        float t = (float)i * (1.0f / 65536.0f);
        px[i] = t;
        py[i] = t * 1.001f;
        pz[i] = t * 0.999f;
    }
}

static void
init_soaos_from_flat(
    uint32_t n,
    const float* px,
    const float* py,
    const float* pz,
    TRSPK_VertexMetalSoaos* out)
{
    for( uint32_t i = 0; i < n; ++i )
    {
        const uint32_t bi = TRSPK_VERTEX_SOAOS_BLOCK_IDX(i);
        const uint32_t li = TRSPK_VERTEX_SOAOS_LANE_IDX(i);
        TRSPK_VertexMetalSoaosBlock* blk = &out->blocks[bi];
        blk->position_x[li] = px[i];
        blk->position_y[li] = py[i];
        blk->position_z[li] = pz[i];
    }
}

/** Same work as a lightweight bake step: mix x,z and accumulate (prevents dead code). */
static float
work_triplet(float x, float y, float z)
{
    const float c = 0.9238795f;
    const float s = 0.3826834f;
    const float tx = 12.3f;
    const float ty = -4.0f;
    const float tz = 7.0f;
    float ox = x * c + z * s + tx;
    float oy = y + ty;
    float oz = z * c - x * s + tz;
    return ox * oy + oz;
}

int
main(void)
{
    const uint32_t n = (uint32_t)BENCH_VERTS;
    if( n == 0u || (n & TRSPK_VERTEX_SOAOS_LANE_MASK) != 0u )
    {
        fprintf(
            stderr,
            "BENCH_VERTS must be non-zero and a multiple of TRSPK_VERTEX_SIMD_LANES "
            "(this build: %u).\n",
            (unsigned)TRSPK_VERTEX_SIMD_LANES);
        return 1;
    }

    float* px = alloc_f32(n);
    float* py = alloc_f32(n);
    float* pz = alloc_f32(n);
    if( !px || !py || !pz )
    {
        fprintf(stderr, "oom flat\n");
        return 1;
    }
    init_flat_soa(n, px, py, pz);

    TRSPK_VertexMetalSoaos soaos;
    if( !trspk_metal_vertex_soaos_alloc(&soaos, n) )
    {
        fprintf(stderr, "oom soaos\n");
        free(px);
        free(py);
        free(pz);
        return 1;
    }
    init_soaos_from_flat(n, px, py, pz, &soaos);

    volatile float sink = 0.0f;

    for( uint32_t w = 0; w < (uint32_t)BENCH_WARMUP; ++w )
    {
        float acc = 0.0f;
        for( uint32_t i = 0; i < n; ++i )
            acc += work_triplet(px[i], py[i], pz[i]);
        sink += acc;
    }

    double t_flat0 = now_seconds();
    for( uint32_t p = 0; p < (uint32_t)BENCH_PASSES; ++p )
    {
        float acc = 0.0f;
        for( uint32_t i = 0; i < n; ++i )
            acc += work_triplet(px[i], py[i], pz[i]);
        sink += acc;
    }
    double t_flat1 = now_seconds();

    for( uint32_t w = 0; w < (uint32_t)BENCH_WARMUP; ++w )
    {
        float acc = 0.0f;
        const uint32_t bc = soaos.block_count;
        for( uint32_t b = 0; b < bc; ++b )
        {
            const TRSPK_VertexMetalSoaosBlock* blk = &soaos.blocks[b];
            for( uint32_t j = 0; j < TRSPK_VERTEX_SIMD_LANES; ++j )
                acc += work_triplet(
                    blk->position_x[j], blk->position_y[j], blk->position_z[j]);
        }
        sink += acc;
    }

    double t_soaos0 = now_seconds();
    for( uint32_t p = 0; p < (uint32_t)BENCH_PASSES; ++p )
    {
        float acc = 0.0f;
        const uint32_t bc = soaos.block_count;
        for( uint32_t b = 0; b < bc; ++b )
        {
            const TRSPK_VertexMetalSoaosBlock* blk = &soaos.blocks[b];
            for( uint32_t j = 0; j < TRSPK_VERTEX_SIMD_LANES; ++j )
                acc += work_triplet(
                    blk->position_x[j], blk->position_y[j], blk->position_z[j]);
        }
        sink += acc;
    }
    double t_soaos1 = now_seconds();

    trspk_metal_vertex_soaos_free(&soaos);
    free(px);
    free(py);
    free(pz);

    const double flat_s = t_flat1 - t_flat0;
    const double soaos_s = t_soaos1 - t_soaos0;
    const double verts_total = (double)n * (double)BENCH_PASSES;

    printf(
        "vertex_soaos_scalar: verts=%u  lanes=%u  shift=%u  passes=%u\n",
        (unsigned)n,
        (unsigned)TRSPK_VERTEX_SIMD_LANES,
        (unsigned)TRSPK_VERTEX_SOAOS_BLOCK_SHIFT,
        (unsigned)BENCH_PASSES);
    printf(
        "  flat SoA (scalar index):  time=%.6f s  %.3f ns/vert  (sink=%g)\n",
        flat_s,
        flat_s > 0.0 ? (flat_s * 1e9) / verts_total : 0.0,
        (double)sink);
    printf(
        "  AoSoA (block+lane):       time=%.6f s  %.3f ns/vert\n",
        soaos_s,
        soaos_s > 0.0 ? (soaos_s * 1e9) / verts_total : 0.0);
    if( soaos_s > 0.0 && flat_s > 0.0 )
        printf("  ratio (flat / soaos):     %.3f\n", flat_s / soaos_s);

    return 0;
}
