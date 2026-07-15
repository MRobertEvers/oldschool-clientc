/* SSE4.1 128-bit float paths; AoSoA block loads. */
#include <smmintrin.h>

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
                1.0f,
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
                __m128 lx = _mm_load_ps(blk->position_x);
                __m128 ly = _mm_load_ps(blk->position_y);
                __m128 lz = _mm_load_ps(blk->position_z);
                float ox[TRSPK_VERTEX_SIMD_LANES];
                float oy[TRSPK_VERTEX_SIMD_LANES];
                float oz[TRSPK_VERTEX_SIMD_LANES];
                _mm_storeu_ps(ox, lx);
                _mm_storeu_ps(oy, ly);
                _mm_storeu_ps(oz, lz);
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
            const __m128 vc = _mm_set1_ps(bake->cos_yaw);
            const __m128 vs = _mm_set1_ps(bake->sin_yaw);
            const __m128 vtx = _mm_set1_ps(bake->tx);
            const __m128 vty = _mm_set1_ps(bake->ty);
            const __m128 vtz = _mm_set1_ps(bake->tz);
            uint32_t b = 0;
            for( ; b < full_b; ++b )
            {
                const TRSPK_VertexMetalSoaosBlock* blk = &a->blocks[b];
                const __m128 lx = _mm_load_ps(blk->position_x);
                const __m128 ly = _mm_load_ps(blk->position_y);
                const __m128 lz = _mm_load_ps(blk->position_z);
                const __m128 vx = _mm_add_ps(
                    _mm_add_ps(_mm_mul_ps(lx, vc), _mm_mul_ps(lz, vs)), vtx);
                const __m128 vy = _mm_add_ps(ly, vty);
                const __m128 vz = _mm_add_ps(
                    _mm_sub_ps(_mm_mul_ps(lz, vc), _mm_mul_ps(lx, vs)), vtz);
                float ox[TRSPK_VERTEX_SIMD_LANES];
                float oy[TRSPK_VERTEX_SIMD_LANES];
                float oz[TRSPK_VERTEX_SIMD_LANES];
                _mm_storeu_ps(ox, vx);
                _mm_storeu_ps(oy, vy);
                _mm_storeu_ps(oz, vz);
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
                __m128 lx = _mm_load_ps(blk->position_x);
                __m128 ly = _mm_load_ps(blk->position_y);
                __m128 lz = _mm_load_ps(blk->position_z);
                float ox[TRSPK_VERTEX_SIMD_LANES];
                float oy[TRSPK_VERTEX_SIMD_LANES];
                float oz[TRSPK_VERTEX_SIMD_LANES];
                _mm_storeu_ps(ox, lx);
                _mm_storeu_ps(oy, ly);
                _mm_storeu_ps(oz, lz);
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
            const __m128 vc = _mm_set1_ps(bake->cos_yaw);
            const __m128 vs = _mm_set1_ps(bake->sin_yaw);
            const __m128 vtx = _mm_set1_ps(bake->tx);
            const __m128 vty = _mm_set1_ps(bake->ty);
            const __m128 vtz = _mm_set1_ps(bake->tz);
            uint32_t b = 0;
            for( ; b < full_b; ++b )
            {
                const TRSPK_VertexWebGL1SoaosBlock* blk = &a->blocks[b];
                const __m128 lx = _mm_load_ps(blk->position_x);
                const __m128 ly = _mm_load_ps(blk->position_y);
                const __m128 lz = _mm_load_ps(blk->position_z);
                const __m128 vx = _mm_add_ps(
                    _mm_add_ps(_mm_mul_ps(lx, vc), _mm_mul_ps(lz, vs)), vtx);
                const __m128 vy = _mm_add_ps(ly, vty);
                const __m128 vz = _mm_add_ps(
                    _mm_sub_ps(_mm_mul_ps(lz, vc), _mm_mul_ps(lx, vs)), vtz);
                float ox[TRSPK_VERTEX_SIMD_LANES];
                float oy[TRSPK_VERTEX_SIMD_LANES];
                float oz[TRSPK_VERTEX_SIMD_LANES];
                _mm_storeu_ps(ox, vx);
                _mm_storeu_ps(oy, vy);
                _mm_storeu_ps(oz, vz);
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
