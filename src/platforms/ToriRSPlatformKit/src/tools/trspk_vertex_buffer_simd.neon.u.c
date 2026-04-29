/* NEON 128-bit float paths; AoSoA block loads. */
#include <arm_neon.h>

static void
trspk_vertex_buffer_bake_array_to_interleaved_vertices(
    const TRSPK_VertexBuffer* src,
    const TRSPK_BakeTransform* bake,
    TRSPK_VertexBuffer* out,
    double d3d8_frame_clock)
{
    const uint32_t n = src->vertex_count;
    if( src->format == TRSPK_VERTEX_FORMAT_D3D8_SOAOS )
    {
        const TRSPK_VertexD3D8Soaos* a = &src->vertices.as_d3d8_soaos;
        float color_lin[4];
        for( uint32_t i = 0; i < n; ++i )
        {
            const uint32_t bi = TRSPK_VERTEX_SOAOS_BLOCK_IDX(i);
            const uint32_t li = TRSPK_VERTEX_SOAOS_LANE_IDX(i);
            const TRSPK_VertexD3D8SoaosBlock* blk = &a->blocks[bi];
            float x = blk->position_x[li];
            float y = blk->position_y[li];
            float z = blk->position_z[li];
            trspk_bake_transform_apply(bake, &x, &y, &z);
            color_lin[0] = blk->color_r[li];
            color_lin[1] = blk->color_g[li];
            color_lin[2] = blk->color_b[li];
            color_lin[3] = blk->color_a[li];
            trspk_d3d8_fvf_from_model_vertex(
                x,
                y,
                z,
                color_lin,
                blk->texcoord_u[li],
                blk->texcoord_v[li],
                blk->tex_id[li],
                blk->uv_mode[li],
                d3d8_frame_clock,
                &out->vertices.as_d3d8[i]);
        }
        return;
    }
    (void)d3d8_frame_clock;
    if( src->format == TRSPK_VERTEX_FORMAT_METAL_SOAOS )
    {
        const TRSPK_VertexMetalSoaos* a = &src->vertices.as_metal_soaos;
        TRSPK_VertexMetal* dst = out->vertices.as_metal;
        const uint32_t full_b = TRSPK_VERTEX_SOAOS_FULL_BLOCK_COUNT(n);

        if( bake->identity )
        {
            uint32_t b = 0;
            for( ; b < full_b; ++b )
            {
                const TRSPK_VertexMetalSoaosBlock* blk = &a->blocks[b];
                float32x4_t lx = vld1q_f32(blk->position_x);
                float32x4_t ly = vld1q_f32(blk->position_y);
                float32x4_t lz = vld1q_f32(blk->position_z);
                float ox[TRSPK_VERTEX_SIMD_LANES];
                float oy[TRSPK_VERTEX_SIMD_LANES];
                float oz[TRSPK_VERTEX_SIMD_LANES];
                vst1q_f32(ox, lx);
                vst1q_f32(oy, ly);
                vst1q_f32(oz, lz);
                for( uint32_t j = 0; j < TRSPK_VERTEX_SIMD_LANES; ++j )
                {
                    const uint32_t ii = TRSPK_VERTEX_SOAOS_VERT_INDEX(b, j);
                    TRSPK_VertexMetal* v = &dst[ii];
                    v->position[0] = ox[j];
                    v->position[1] = oy[j];
                    v->position[2] = oz[j];
                    v->position[3] = blk->position_w[j];
                    v->color[0] = blk->color_r[j];
                    v->color[1] = blk->color_g[j];
                    v->color[2] = blk->color_b[j];
                    v->color[3] = blk->color_a[j];
                    v->texcoord[0] = blk->texcoord_u[j];
                    v->texcoord[1] = blk->texcoord_v[j];
                    v->tex_id = blk->tex_id[j];
                    v->uv_mode = blk->uv_mode[j];
                }
            }
            for( uint32_t i = TRSPK_VERTEX_SOAOS_TAIL_START(n); i < n; ++i )
            {
                const uint32_t bi = TRSPK_VERTEX_SOAOS_BLOCK_IDX(i);
                const uint32_t li = TRSPK_VERTEX_SOAOS_LANE_IDX(i);
                const TRSPK_VertexMetalSoaosBlock* blk = &a->blocks[bi];
                TRSPK_VertexMetal* v = &dst[i];
                v->position[0] = blk->position_x[li];
                v->position[1] = blk->position_y[li];
                v->position[2] = blk->position_z[li];
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
        else
        {
            const float32x4_t vc = vdupq_n_f32(bake->cos_yaw);
            const float32x4_t vs = vdupq_n_f32(bake->sin_yaw);
            const float32x4_t vtx = vdupq_n_f32(bake->tx);
            const float32x4_t vty = vdupq_n_f32(bake->ty);
            const float32x4_t vtz = vdupq_n_f32(bake->tz);
            uint32_t b = 0;
            for( ; b < full_b; ++b )
            {
                const TRSPK_VertexMetalSoaosBlock* blk = &a->blocks[b];
                const float32x4_t lx = vld1q_f32(blk->position_x);
                const float32x4_t ly = vld1q_f32(blk->position_y);
                const float32x4_t lz = vld1q_f32(blk->position_z);
                const float32x4_t vx = vaddq_f32(
                    vaddq_f32(vmulq_f32(lx, vc), vmulq_f32(lz, vs)), vtx);
                const float32x4_t vy = vaddq_f32(ly, vty);
                const float32x4_t vz = vaddq_f32(
                    vsubq_f32(vmulq_f32(lz, vc), vmulq_f32(lx, vs)), vtz);
                float ox[TRSPK_VERTEX_SIMD_LANES];
                float oy[TRSPK_VERTEX_SIMD_LANES];
                float oz[TRSPK_VERTEX_SIMD_LANES];
                vst1q_f32(ox, vx);
                vst1q_f32(oy, vy);
                vst1q_f32(oz, vz);
                for( uint32_t j = 0; j < TRSPK_VERTEX_SIMD_LANES; ++j )
                {
                    const uint32_t ii = TRSPK_VERTEX_SOAOS_VERT_INDEX(b, j);
                    TRSPK_VertexMetal* v = &dst[ii];
                    v->position[0] = ox[j];
                    v->position[1] = oy[j];
                    v->position[2] = oz[j];
                    v->position[3] = blk->position_w[j];
                    v->color[0] = blk->color_r[j];
                    v->color[1] = blk->color_g[j];
                    v->color[2] = blk->color_b[j];
                    v->color[3] = blk->color_a[j];
                    v->texcoord[0] = blk->texcoord_u[j];
                    v->texcoord[1] = blk->texcoord_v[j];
                    v->tex_id = blk->tex_id[j];
                    v->uv_mode = blk->uv_mode[j];
                }
            }
            for( uint32_t i = TRSPK_VERTEX_SOAOS_TAIL_START(n); i < n; ++i )
            {
                const uint32_t bi = TRSPK_VERTEX_SOAOS_BLOCK_IDX(i);
                const uint32_t li = TRSPK_VERTEX_SOAOS_LANE_IDX(i);
                const TRSPK_VertexMetalSoaosBlock* blk = &a->blocks[bi];
                float x = blk->position_x[li];
                float y = blk->position_y[li];
                float z = blk->position_z[li];
                trspk_bake_transform_apply(bake, &x, &y, &z);
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
    }
    else
    {
        const TRSPK_VertexWebGL1Soaos* a = &src->vertices.as_webgl1_soaos;
        TRSPK_VertexWebGL1* dst = out->vertices.as_webgl1;
        const uint32_t full_b = TRSPK_VERTEX_SOAOS_FULL_BLOCK_COUNT(n);

        if( bake->identity )
        {
            uint32_t b = 0;
            for( ; b < full_b; ++b )
            {
                const TRSPK_VertexWebGL1SoaosBlock* blk = &a->blocks[b];
                float32x4_t lx = vld1q_f32(blk->position_x);
                float32x4_t ly = vld1q_f32(blk->position_y);
                float32x4_t lz = vld1q_f32(blk->position_z);
                float ox[TRSPK_VERTEX_SIMD_LANES];
                float oy[TRSPK_VERTEX_SIMD_LANES];
                float oz[TRSPK_VERTEX_SIMD_LANES];
                vst1q_f32(ox, lx);
                vst1q_f32(oy, ly);
                vst1q_f32(oz, lz);
                for( uint32_t j = 0; j < TRSPK_VERTEX_SIMD_LANES; ++j )
                {
                    const uint32_t ii = TRSPK_VERTEX_SOAOS_VERT_INDEX(b, j);
                    TRSPK_VertexWebGL1* v = &dst[ii];
                    v->position[0] = ox[j];
                    v->position[1] = oy[j];
                    v->position[2] = oz[j];
                    v->position[3] = blk->position_w[j];
                    v->color[0] = blk->color_r[j];
                    v->color[1] = blk->color_g[j];
                    v->color[2] = blk->color_b[j];
                    v->color[3] = blk->color_a[j];
                    v->texcoord[0] = blk->texcoord_u[j];
                    v->texcoord[1] = blk->texcoord_v[j];
                    v->tex_id = blk->tex_id[j];
                    v->uv_mode = blk->uv_mode[j];
                }
            }
            for( uint32_t i = TRSPK_VERTEX_SOAOS_TAIL_START(n); i < n; ++i )
            {
                const uint32_t bi = TRSPK_VERTEX_SOAOS_BLOCK_IDX(i);
                const uint32_t li = TRSPK_VERTEX_SOAOS_LANE_IDX(i);
                const TRSPK_VertexWebGL1SoaosBlock* blk = &a->blocks[bi];
                TRSPK_VertexWebGL1* v = &dst[i];
                v->position[0] = blk->position_x[li];
                v->position[1] = blk->position_y[li];
                v->position[2] = blk->position_z[li];
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
        else
        {
            const float32x4_t vc = vdupq_n_f32(bake->cos_yaw);
            const float32x4_t vs = vdupq_n_f32(bake->sin_yaw);
            const float32x4_t vtx = vdupq_n_f32(bake->tx);
            const float32x4_t vty = vdupq_n_f32(bake->ty);
            const float32x4_t vtz = vdupq_n_f32(bake->tz);
            uint32_t b = 0;
            for( ; b < full_b; ++b )
            {
                const TRSPK_VertexWebGL1SoaosBlock* blk = &a->blocks[b];
                const float32x4_t lx = vld1q_f32(blk->position_x);
                const float32x4_t ly = vld1q_f32(blk->position_y);
                const float32x4_t lz = vld1q_f32(blk->position_z);
                const float32x4_t vx = vaddq_f32(
                    vaddq_f32(vmulq_f32(lx, vc), vmulq_f32(lz, vs)), vtx);
                const float32x4_t vy = vaddq_f32(ly, vty);
                const float32x4_t vz = vaddq_f32(
                    vsubq_f32(vmulq_f32(lz, vc), vmulq_f32(lx, vs)), vtz);
                float ox[TRSPK_VERTEX_SIMD_LANES];
                float oy[TRSPK_VERTEX_SIMD_LANES];
                float oz[TRSPK_VERTEX_SIMD_LANES];
                vst1q_f32(ox, vx);
                vst1q_f32(oy, vy);
                vst1q_f32(oz, vz);
                for( uint32_t j = 0; j < TRSPK_VERTEX_SIMD_LANES; ++j )
                {
                    const uint32_t ii = TRSPK_VERTEX_SOAOS_VERT_INDEX(b, j);
                    TRSPK_VertexWebGL1* v = &dst[ii];
                    v->position[0] = ox[j];
                    v->position[1] = oy[j];
                    v->position[2] = oz[j];
                    v->position[3] = blk->position_w[j];
                    v->color[0] = blk->color_r[j];
                    v->color[1] = blk->color_g[j];
                    v->color[2] = blk->color_b[j];
                    v->color[3] = blk->color_a[j];
                    v->texcoord[0] = blk->texcoord_u[j];
                    v->texcoord[1] = blk->texcoord_v[j];
                    v->tex_id = blk->tex_id[j];
                    v->uv_mode = blk->uv_mode[j];
                }
            }
            for( uint32_t i = TRSPK_VERTEX_SOAOS_TAIL_START(n); i < n; ++i )
            {
                const uint32_t bi = TRSPK_VERTEX_SOAOS_BLOCK_IDX(i);
                const uint32_t li = TRSPK_VERTEX_SOAOS_LANE_IDX(i);
                const TRSPK_VertexWebGL1SoaosBlock* blk = &a->blocks[bi];
                float x = blk->position_x[li];
                float y = blk->position_y[li];
                float z = blk->position_z[li];
                trspk_bake_transform_apply(bake, &x, &y, &z);
                TRSPK_VertexWebGL1* v = &dst[i];
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
    }
}
