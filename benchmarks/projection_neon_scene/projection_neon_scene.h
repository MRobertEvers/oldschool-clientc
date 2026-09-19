#ifndef PROJECTION_NEON_SCENE_H
#define PROJECTION_NEON_SCENE_H

#include <stdint.h>

/*
 * One projection call, prepared from the engine's 16.16 sine/cosine tables.
 *
 * The assembly deliberately receives one pointer instead of relying on the
 * platform ABI for nineteen individual arguments.  Keep the offsets in
 * project_vertices_aarch64.S in sync with this structure; bench.c has static
 * assertions for every assembly-visible member.
 *
 * Input and output arrays have ceil(vertex_count / 4) * 4 elements.  The
 * benchmark loader duplicates the final input vertex into the padding lanes.
 * This lets the assembly handle a real model's 1--3 vertex tail as one final
 * NEON block without reading outside either allocation.  Only vertex_count
 * results participate in validation/checksums.
 */
struct ProjectionNeonSceneArgs
{
    int32_t* ortho_x;           /*   0 */
    int32_t* ortho_y;           /*   8 */
    int32_t* ortho_z;           /*  16 */
    int32_t* screen_x;          /*  24 */
    int32_t* screen_y;          /*  32 */
    int32_t* screen_z;          /*  40 */
    const int16_t* vertex_x;    /*  48 */
    const int16_t* vertex_y;    /*  56 */
    const int16_t* vertex_z;    /*  64 */
    uint32_t vertex_count;      /*  72 */
    int32_t cos_model_yaw16;    /*  76 */
    int32_t sin_model_yaw16;    /*  80 */
    int32_t cos_camera_yaw16;   /*  84 */
    int32_t sin_camera_yaw16;   /*  88 */
    int32_t cos_camera_pitch16; /*  92 */
    int32_t sin_camera_pitch16; /*  96 */
    int32_t scene_x;            /* 100 */
    int32_t scene_y;            /* 104 */
    int32_t scene_z;            /* 108 */
    int32_t camera_cot15;       /* 112: camera_cot16 >> 1 */
    int32_t model_mid_z;        /* 116 */
    int32_t near_plane_z;       /* 120 */
    uint32_t model_yaw_nonzero; /* 124: selects the production no-yaw fast path */
};

#if defined(__aarch64__) || defined(_M_ARM64)
void
project_vertices_asm_fused_tex(const struct ProjectionNeonSceneArgs* args);
void
project_vertices_asm_unfused_tex(const struct ProjectionNeonSceneArgs* args);
void
project_vertices_asm_fused_notex(const struct ProjectionNeonSceneArgs* args);
void
project_vertices_asm_unfused_notex(const struct ProjectionNeonSceneArgs* args);
#endif

#endif
