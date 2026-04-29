/**
 * Standalone test for trspk_batch16: chunk roll at uint16 limits and clear/destroy cleanup.
 * Links ToriRS tools without full dash.c / trspk_math.c — stubs below satisfy vertex_buffer.c.
 */

#include "trspk_batch16.h"
#include "trspk_vertex_format.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Satisfy trspk_vertex_buffer.c via trspk_math.h inlines (no dash.c / trspk_math.c). */
int
dash_hsl16_to_rgb(int hsl16)
{
    (void)hsl16;
    return 0;
}

float
trspk_pack_gpu_uv_mode_float(float anim_u, float anim_v)
{
    unsigned mag_u = (unsigned)fmaxf(0.0f, fminf(255.0f, anim_u * 256.0f + 0.5f));
    unsigned mag_v = (unsigned)fmaxf(0.0f, fminf(255.0f, anim_v * 256.0f + 0.5f));
    unsigned enc = 0u;
    if( mag_u > 0u )
        enc = 1u + mag_u;
    else if( mag_v > 0u )
        enc = 257u + mag_v;
    return 2.0f * (float)enc;
}

#define FAIL(...)                                                                                  \
    do                                                                                             \
    {                                                                                              \
        fprintf(stderr, __VA_ARGS__);                                                              \
        fprintf(stderr, "\n");                                                                     \
        return 1;                                                                                  \
    } while( 0 )

static int
test_minimal_create_destroy(void)
{
    TRSPK_Batch16* b = trspk_batch16_create(1024u, 3072u, TRSPK_VERTEX_FORMAT_TRSPK);
    if( !b )
        FAIL("trspk_batch16_create failed");
    trspk_batch16_begin(b);
    TRSPK_Vertex tri[3] = { 0 };
    if( !trspk_batch16_append_triangle(b, tri, (TRSPK_ModelId)1u, 0u, 0u) )
    {
        trspk_batch16_destroy(b);
        FAIL("append_triangle on minimal batch failed");
    }
    trspk_batch16_end(b);
    trspk_batch16_destroy(b);
    return 0;
}

int
main(void)
{
    trspk_batch16_destroy(NULL);

    if( test_minimal_create_destroy() != 0 )
        return 1;

    const uint32_t n = 65535u;
    TRSPK_Vertex* verts = (TRSPK_Vertex*)calloc((size_t)n, sizeof(TRSPK_Vertex));
    uint16_t* indices = (uint16_t*)malloc((size_t)n * sizeof(uint16_t));
    if( !verts || !indices )
    {
        free(verts);
        free(indices);
        FAIL("calloc/malloc for full-chunk mesh failed");
    }

    for( uint32_t t = 0u; t < 21845u; ++t )
    {
        indices[3u * t + 0u] = (uint16_t)(3u * t + 0u);
        indices[3u * t + 1u] = (uint16_t)(3u * t + 1u);
        indices[3u * t + 2u] = (uint16_t)(3u * t + 2u);
    }

    TRSPK_Batch16* batch = trspk_batch16_create(0u, 0u, TRSPK_VERTEX_FORMAT_TRSPK);
    if( !batch )
    {
        free(verts);
        free(indices);
        FAIL("trspk_batch16_create failed");
    }

    trspk_batch16_begin(batch);

    if( trspk_batch16_add_mesh(
            batch,
            verts,
            n,
            indices,
            n,
            (TRSPK_ModelId)42u,
            7u,
            99u) < 0 )
    {
        trspk_batch16_destroy(batch);
        free(verts);
        free(indices);
        FAIL("add_mesh (full chunk) failed");
    }

    free(verts);
    free(indices);
    verts = NULL;
    indices = NULL;

    if( trspk_batch16_chunk_count(batch) != 1u )
        FAIL("expected 1 chunk after full mesh, got %u", trspk_batch16_chunk_count(batch));

    const uint32_t stride = trspk_vertex_format_stride(TRSPK_VERTEX_FORMAT_TRSPK);
    const void* cv = NULL;
    const void* ci = NULL;
    uint32_t vb = 0u;
    uint32_t ib = 0u;
    trspk_batch16_get_chunk(batch, 0u, &cv, &vb, &ci, &ib);
    if( vb != n * stride || ib != n * (uint32_t)sizeof(uint16_t) )
        FAIL(
            "chunk 0 sizes wrong: vb=%u (want %u), ib=%u (want %u)",
            vb,
            n * stride,
            ib,
            n * (uint32_t)sizeof(uint16_t));

    if( trspk_batch16_entry_count(batch) != 1u )
        FAIL("entry_count after first mesh: %u", trspk_batch16_entry_count(batch));

    const TRSPK_Batch16Entry* e0 = trspk_batch16_get_entry(batch, 0u);
    if( !e0 || e0->chunk_index != 0u || e0->vbo_offset != 0u || e0->ebo_offset != 0u )
        FAIL("first entry metadata wrong");

    TRSPK_Vertex tri[3] = { 0 };
    if( !trspk_batch16_append_triangle(
            batch, tri, (TRSPK_ModelId)43u, 8u, 100u) )
    {
        trspk_batch16_destroy(batch);
        FAIL("append_triangle after full chunk failed (roll)");
    }

    if( trspk_batch16_chunk_count(batch) != 2u )
        FAIL("expected 2 chunks after roll, got %u", trspk_batch16_chunk_count(batch));

    trspk_batch16_get_chunk(batch, 0u, &cv, &vb, &ci, &ib);
    if( vb != n * stride || ib != n * (uint32_t)sizeof(uint16_t) )
        FAIL("chunk 0 changed after roll");

    trspk_batch16_get_chunk(batch, 1u, &cv, &vb, &ci, &ib);
    if( vb != 3u * stride || ib != 3u * (uint32_t)sizeof(uint16_t) )
        FAIL("chunk 1 size wrong: vb=%u ib=%u", vb, ib);

    if( trspk_batch16_entry_count(batch) != 2u )
        FAIL("entry_count after roll: %u", trspk_batch16_entry_count(batch));

    const TRSPK_Batch16Entry* e1 = trspk_batch16_get_entry(batch, 1u);
    if( !e1 || e1->chunk_index != 1u || e1->vbo_offset != 0u || e1->ebo_offset != 0u ||
        e1->element_count != 3u )
        FAIL("second entry metadata wrong (chunk_index=%u)", e1 ? (unsigned)e1->chunk_index : 999u);

    trspk_batch16_end(batch);

    trspk_batch16_clear(batch);
    if( trspk_batch16_chunk_count(batch) != 0u || trspk_batch16_entry_count(batch) != 0u )
        FAIL("clear did not reset counts: chunks=%u entries=%u",
             trspk_batch16_chunk_count(batch),
             trspk_batch16_entry_count(batch));

    trspk_batch16_destroy(batch);
    trspk_batch16_destroy(NULL);

    printf("trspk_batch16_test: ok\n");
    return 0;
}
