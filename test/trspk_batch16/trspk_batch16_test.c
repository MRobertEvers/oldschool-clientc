#include "trspk_batch16.h"

#include <stdint.h>
#include <stdio.h>

#define CHECK(condition, ...)                                                                       \
    do                                                                                              \
    {                                                                                               \
        if( !(condition) )                                                                          \
        {                                                                                           \
            fprintf(stderr, "trspk_batch16_test: ");                                               \
            fprintf(stderr, __VA_ARGS__);                                                           \
            fprintf(stderr, "\n");                                                                \
            return 1;                                                                               \
        }                                                                                           \
    } while( 0 )

static int
test_format_and_build_guards(void)
{
    struct TRSPK_Batch16Reservation reservation;
    struct TRSPK_Batch16* batch = trspk_batch16_create(TRSPK_VERTEX_FORMAT_D3D9);
    CHECK(batch != NULL, "D3D9 create failed");
    CHECK(
        trspk_batch16_create(TRSPK_VERTEX_FORMAT_TRSPK) == NULL,
        "CPU-only vertex format should be rejected");

    CHECK(
        !trspk_batch16_reserve_pose(batch, 1, -1, 0, 3u, &reservation),
        "reserve outside begin succeeded");
    CHECK(
        reservation.vbo == NULL && reservation.triangles == NULL &&
            reservation.chunk_index == TRSPK_BATCH16_INVALID_CHUNK,
        "failed reservation was not cleared");

    trspk_batch16_begin(batch);
    CHECK(
        !trspk_batch16_reserve_pose(batch, 1, -1, 0, 0u, &reservation),
        "zero-sized reservation succeeded");
    CHECK(
        !trspk_batch16_reserve_pose(batch, 1, -1, 0, 4u, &reservation),
        "non-triangle reservation succeeded");
    CHECK(
        !trspk_batch16_reserve_pose(
            batch, 1, -1, 0, TRSPK_BATCH16_MAX_VERTICES + 1u, &reservation),
        "oversized reservation succeeded");
    CHECK(
        !trspk_batch16_reserve_pose(batch, 1, -1, 0, 3u, NULL),
        "reservation without output succeeded");

    trspk_batch16_destroy(batch);
    trspk_batch16_destroy(NULL);
    return 0;
}

static int
test_chunk_roll_lookup_and_reuse(void)
{
    struct TRSPK_Batch16* batch = trspk_batch16_create(TRSPK_VERTEX_FORMAT_D3D9);
    struct TRSPK_Batch16Reservation first;
    struct TRSPK_Batch16Reservation duplicate;
    struct TRSPK_Batch16Reservation second;
    struct TRSPK_Batch16Reservation third;
    CHECK(batch != NULL, "create failed");

    trspk_batch16_begin(batch);
    CHECK(
        trspk_batch16_reserve_pose(
            batch, 42, -1, 7, TRSPK_BATCH16_MAX_VERTICES, &first),
        "full-chunk reservation failed");
    CHECK(
        first.chunk_index == 0u && first.vertex_base == 0u && first.vbo != NULL &&
            first.triangles != NULL,
        "first reservation location is wrong");
    CHECK(
        trspk_batch16_chunk_count(batch) == 1u && trspk_batch16_entry_count(batch) == 1u,
        "first reservation counts are wrong");

    struct TRSPK_Batch16Chunk* chunk0 = trspk_batch16_get_chunk(batch, 0u);
    CHECK(chunk0 != NULL, "chunk zero missing");
    CHECK(
        chunk0->vertex_count == TRSPK_BATCH16_MAX_VERTICES &&
            chunk0->vbo->vertex_count == TRSPK_BATCH16_MAX_VERTICES &&
            chunk0->vbo->capacity >= TRSPK_BATCH16_MAX_VERTICES,
        "full chunk VBO counts are wrong");
    CHECK(
        chunk0->triangles.cap >= TRSPK_BATCH16_MAX_VERTICES / 3u,
        "triangle config capacity is too small");
    CHECK(trspk_vbo_is_dirty(chunk0->vbo), "reserved VBO was not marked dirty");

    trspk_triangles_set(first.triangles, first.vertex_base / 3u, 123);
    first.vbo->vertices.as_d3d9[first.vertex_base].position[0] = 42.0f;

    CHECK(
        trspk_batch16_reserve_pose(
            batch, 42, -1, 7, TRSPK_BATCH16_MAX_VERTICES, &duplicate),
        "duplicate key was not idempotent");
    CHECK(
        duplicate.vbo == first.vbo && duplicate.triangles == first.triangles &&
            duplicate.chunk_index == first.chunk_index &&
            duplicate.vertex_base == first.vertex_base &&
            trspk_batch16_entry_count(batch) == 1u,
        "duplicate key allocated another entry");
    CHECK(
        !trspk_batch16_reserve_pose(batch, 42, -1, 7, 3u, &duplicate),
        "duplicate key accepted a different vertex count");

    CHECK(
        trspk_batch16_reserve_pose(batch, 43, 2, 9, 3u, &second),
        "roll reservation failed");
    CHECK(second.chunk_index == 1u && second.vertex_base == 0u, "batch did not roll");
    CHECK(
        trspk_batch16_reserve_pose(batch, 44, 2, 10, 65532u, &third),
        "exact-fill reservation failed");
    CHECK(
        third.chunk_index == 1u && third.vertex_base == 3u,
        "exact-fill reservation rolled too early");

    struct TRSPK_Batch16Reservation rolled;
    CHECK(
        trspk_batch16_reserve_pose(batch, 45, 3, 11, 3u, &rolled),
        "second roll reservation failed");
    CHECK(
        rolled.chunk_index == 2u && rolled.vertex_base == 0u &&
            trspk_batch16_chunk_count(batch) == 3u,
        "second roll location is wrong");

    const struct TRSPK_Batch16Entry* entry =
        trspk_batch16_find_entry(batch, 43, 2, 9);
    CHECK(
        entry != NULL && entry->chunk_index == 1u && entry->vertex_base == 0u &&
            entry->vertex_count == 3u,
        "key lookup returned wrong metadata");
    CHECK(
        trspk_batch16_find_entry(batch, 43, 99, 9) == NULL,
        "lookup ignored anim_index");
    CHECK(
        trspk_batch16_get_entry(batch, 0u) != NULL &&
            trspk_batch16_get_entry(batch, trspk_batch16_entry_count(batch)) == NULL &&
            trspk_batch16_get_chunk(batch, trspk_batch16_chunk_count(batch)) == NULL,
        "bounds accessors are wrong");

    struct TRSPK_VBO* const old_vbo0 = chunk0->vbo;
    struct TRSPK_VertexD3D9* const old_vertices0 = chunk0->vbo->vertices.as_d3d9;
    int* const old_triangles0 = chunk0->triangles.config;
    const uint32_t old_vbo_capacity0 = chunk0->vbo->capacity;
    const uint32_t old_triangle_capacity0 = chunk0->triangles.cap;
    struct TRSPK_Batch16Chunk* const old_chunk1 = trspk_batch16_get_chunk(batch, 1u);
    struct TRSPK_VBO* const old_vbo1 = old_chunk1->vbo;

    trspk_batch16_end(batch);
    CHECK(
        trspk_batch16_find_entry(batch, 42, -1, 7) != NULL &&
            trspk_batch16_get_chunk(batch, 0u) == chunk0 &&
            chunk0->vbo->vertices.as_d3d9[0].position[0] == 42.0f &&
            trspk_triangles_get(&chunk0->triangles, 0u) == 123,
        "end discarded retained entry/vertex/triangle data");
    CHECK(
        !trspk_batch16_reserve_pose(batch, 46, 0, 0, 3u, &duplicate),
        "reserve after end succeeded");

    trspk_batch16_clear(batch);
    CHECK(
        trspk_batch16_chunk_count(batch) == 0u && trspk_batch16_entry_count(batch) == 0u,
        "clear did not empty live counts");
    CHECK(
        trspk_batch16_find_entry(batch, 42, -1, 7) == NULL,
        "clear retained a live key");

    trspk_batch16_begin(batch);
    CHECK(
        trspk_batch16_reserve_pose(
            batch, 50, -1, 0, TRSPK_BATCH16_MAX_VERTICES, &first) &&
            trspk_batch16_reserve_pose(batch, 51, -1, 0, 3u, &second),
        "rebuild reservations failed");
    chunk0 = trspk_batch16_get_chunk(batch, 0u);
    CHECK(
        chunk0 != NULL && chunk0->vbo == old_vbo0 &&
            chunk0->vbo->vertices.as_d3d9 == old_vertices0 &&
            chunk0->triangles.config == old_triangles0 &&
            chunk0->vbo->capacity == old_vbo_capacity0 &&
            chunk0->triangles.cap == old_triangle_capacity0,
        "chunk zero allocations were not reused");
    CHECK(
        trspk_batch16_get_chunk(batch, 1u) == old_chunk1 && second.vbo == old_vbo1,
        "rolled chunk object/VBO was not reused");

    trspk_batch16_destroy(batch);
    return 0;
}

static int
test_lookup_growth(void)
{
    enum
    {
        ENTRY_COUNT = 4096
    };
    struct TRSPK_Batch16* batch = trspk_batch16_create(TRSPK_VERTEX_FORMAT_OPENGL3);
    CHECK(batch != NULL, "OpenGL3 create failed");
    trspk_batch16_begin(batch);

    for( int i = 0; i < ENTRY_COUNT; i++ )
    {
        struct TRSPK_Batch16Reservation reservation;
        CHECK(
            trspk_batch16_reserve_pose(batch, i - 2048, i % 17 - 8, i * 3, 3u, &reservation),
            "lookup-growth reserve %d failed",
            i);
    }
    CHECK(
        trspk_batch16_entry_count(batch) == ENTRY_COUNT &&
            trspk_batch16_chunk_count(batch) == 1u,
        "lookup-growth counts are wrong");

    for( int i = 0; i < ENTRY_COUNT; i++ )
    {
        const struct TRSPK_Batch16Entry* entry =
            trspk_batch16_find_entry(batch, i - 2048, i % 17 - 8, i * 3);
        CHECK(
            entry != NULL && entry->vertex_base == (uint32_t)i * 3u,
            "lookup-growth find %d failed",
            i);
    }

    trspk_batch16_destroy(batch);
    return 0;
}

int
main(void)
{
    if( test_format_and_build_guards() != 0 )
        return 1;
    if( test_chunk_roll_lookup_and_reuse() != 0 )
        return 1;
    if( test_lookup_growth() != 0 )
        return 1;

    printf("trspk_batch16_test: PASS\n");
    return 0;
}
