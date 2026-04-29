#ifndef TORIRS_PLATFORM_KIT_TRSPK_VERTEX_SOAOS_CONFIG_H
#define TORIRS_PLATFORM_KIT_TRSPK_VERTEX_SOAOS_CONFIG_H

#include <stdint.h>

/* Same dispatch order as trspk_vertex_buffer_simd.u.c */
/* TRSPK_VERTEX_SOAOS_ALIGNMENT: plain ints (not 16u); _Alignas() rejects unsigned literals on some GCC/MinGW. */
#if ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && !defined(NEON_DISABLED)
#define TRSPK_VERTEX_SIMD_LANES 4u
#define TRSPK_VERTEX_SOAOS_BLOCK_SHIFT 2u
#define TRSPK_VERTEX_SOAOS_ALIGNMENT 16
#elif defined(__AVX2__) && !defined(AVX2_DISABLED)
#define TRSPK_VERTEX_SIMD_LANES 8u
#define TRSPK_VERTEX_SOAOS_BLOCK_SHIFT 3u
#define TRSPK_VERTEX_SOAOS_ALIGNMENT 32
#elif defined(__SSE4_1__) && !defined(SSE2_DISABLED)
#define TRSPK_VERTEX_SIMD_LANES 4u
#define TRSPK_VERTEX_SOAOS_BLOCK_SHIFT 2u
#define TRSPK_VERTEX_SOAOS_ALIGNMENT 16
#elif defined(__SSE2__) && !defined(SSE2_DISABLED)
#define TRSPK_VERTEX_SIMD_LANES 4u
#define TRSPK_VERTEX_SOAOS_BLOCK_SHIFT 2u
#define TRSPK_VERTEX_SOAOS_ALIGNMENT 16
#else
#define TRSPK_VERTEX_SIMD_LANES 1u
#define TRSPK_VERTEX_SOAOS_BLOCK_SHIFT 0u
#define TRSPK_VERTEX_SOAOS_ALIGNMENT 16
#endif

/** Lane index: SIMD_LANES is a power of 2. */
#define TRSPK_VERTEX_SOAOS_LANE_MASK (TRSPK_VERTEX_SIMD_LANES - 1u)

#define TRSPK_VERTEX_SOAOS_BLOCK_IDX(vertex_i) \
    ((uint32_t)(vertex_i) >> TRSPK_VERTEX_SOAOS_BLOCK_SHIFT)
#define TRSPK_VERTEX_SOAOS_LANE_IDX(vertex_i) \
    ((uint32_t)(vertex_i) & TRSPK_VERTEX_SOAOS_LANE_MASK)

/** Vertex index from block index and lane (power-of-2 lanes). */
#define TRSPK_VERTEX_SOAOS_VERT_INDEX(block_i, lane_i) \
    (((uint32_t)(block_i) << TRSPK_VERTEX_SOAOS_BLOCK_SHIFT) + (uint32_t)(lane_i))

/** Full SIMD blocks that fit in n vertices (floor(n / SIMD_LANES)). */
#define TRSPK_VERTEX_SOAOS_FULL_BLOCK_COUNT(n) \
    ((uint32_t)(n) >> TRSPK_VERTEX_SOAOS_BLOCK_SHIFT)

/** First vertex index after full blocks (n rounded down to multiple of SIMD_LANES). */
#define TRSPK_VERTEX_SOAOS_TAIL_START(n) \
    ((uint32_t)(n) & (uint32_t) ~TRSPK_VERTEX_SOAOS_LANE_MASK)

/** Block count to hold n vertices (ceil(n / SIMD_LANES)). */
#define TRSPK_VERTEX_SOAOS_BLOCK_COUNT(n) \
    (((uint32_t)(n) + TRSPK_VERTEX_SOAOS_LANE_MASK) >> TRSPK_VERTEX_SOAOS_BLOCK_SHIFT)

#endif
