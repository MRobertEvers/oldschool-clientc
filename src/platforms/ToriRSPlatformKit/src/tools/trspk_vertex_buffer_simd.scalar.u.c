static void
trspk_vertex_buffer_bake_array_to_interleaved_vertices(
    const TRSPK_VertexBuffer* src,
    const TRSPK_BakeTransform* bake,
    TRSPK_VertexBuffer* out)
{
    const uint32_t n = src->vertex_count;
    if( src->format == TRSPK_VERTEX_FORMAT_METAL_SOAOS )
    {
        const TRSPK_VertexMetalSoaos* a = &src->vertices.as_metal_soaos;
        for( uint32_t i = 0; i < n; ++i )
        {
            const uint32_t bi = TRSPK_VERTEX_SOAOS_BLOCK_IDX(i);
            const uint32_t li = TRSPK_VERTEX_SOAOS_LANE_IDX(i);
            const TRSPK_VertexMetalSoaosBlock* blk = &a->blocks[bi];
            float x = blk->position_x[li];
            float y = blk->position_y[li];
            float z = blk->position_z[li];
            trspk_bake_transform_apply(bake, &x, &y, &z);
            TRSPK_VertexMetal* v = &out->vertices.as_metal[i];
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
    else
    {
        const TRSPK_VertexWebGL1Soaos* a = &src->vertices.as_webgl1_soaos;
        for( uint32_t i = 0; i < n; ++i )
        {
            const uint32_t bi = TRSPK_VERTEX_SOAOS_BLOCK_IDX(i);
            const uint32_t li = TRSPK_VERTEX_SOAOS_LANE_IDX(i);
            const TRSPK_VertexWebGL1SoaosBlock* blk = &a->blocks[bi];
            float x = blk->position_x[li];
            float y = blk->position_y[li];
            float z = blk->position_z[li];
            trspk_bake_transform_apply(bake, &x, &y, &z);
            TRSPK_VertexWebGL1* v = &out->vertices.as_webgl1[i];
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
