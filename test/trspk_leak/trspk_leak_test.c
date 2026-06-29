/**
 * CPU-only steady-state leak test for model arena, pose table, and grid atlas.
 * No GPU / OpenGL required.
 */

#include "platformkit/core/trspk_atlas.h"
#include "platformkit/core/trspk_modelarena.h"
#include "platformkit/core/trspk_pose.h"
#include "platformkit/core/trspk_triangles.h"
#include "platformkit/core/trspk_vbo.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAIL(...)                                                                                  \
    do                                                                                             \
    {                                                                                              \
        fprintf(stderr, __VA_ARGS__);                                                              \
        fprintf(stderr, "\n");                                                                     \
        return 1;                                                                                  \
    } while( 0 )

struct AliveCountCtx
{
    uint32_t count;
};

static void
count_alive_slot(
    const struct TRSPK_ModelSlot* slot,
    void* user_data)
{
    (void)slot;
    struct AliveCountCtx* ctx = (struct AliveCountCtx*)user_data;
    ctx->count++;
}

static uint32_t
alive_model_slots(const struct TRSPK_ModelArena* arena)
{
    struct AliveCountCtx ctx = { 0u };
    trspk_modelarena_visit_alive(arena, count_alive_slot, &ctx);
    return ctx.count;
}

static int
test_modelarena_pose_steady_state(void)
{
    struct TRSPK_VBO* vbo = trspk_vbo_create(0u, TRSPK_VERTEX_FORMAT_OPENGL3);
    struct TRSPK_Triangles tri = { 0 };
    trspk_triangles_ensure(&tri, TRSPK_TRIANGLES_INIT_CAP);

    struct TRSPK_ModelArena* arena =
        trspk_modelarena_create(vbo, &tri, 1u << 16, 64u);
    if( !arena )
        FAIL("trspk_modelarena_create failed");

    struct TRSPK_PoseTable poses;
    memset(&poses, 0, sizeof(poses));
    trspk_pose_table_init(&poses);

    const uint32_t baseline_cursor = arena->write_cursor;

    for( int element_id = 0; element_id < 8; ++element_id )
    {
        const uint32_t slot = trspk_modelarena_load(arena, element_id, 0, 12u);
        trspk_pose_table_set(&poses, element_id, 0, 0, slot * 12u);
        trspk_modelarena_unload_element(arena, element_id);
        trspk_pose_table_remove_element(&poses, element_id);
    }
    trspk_modelarena_gc(arena);

    const uint32_t baseline_capacity = vbo->capacity;
    const uint32_t baseline_slots = arena->slot_count;
    const uint32_t baseline_pose_elements = poses.element_count;

    for( int cycle = 0; cycle < 64; ++cycle )
    {
        for( int element_id = 0; element_id < 8; ++element_id )
        {
            const uint32_t slot = trspk_modelarena_load(arena, element_id, 0, 12u);
            trspk_pose_table_set(&poses, element_id, 0, 0, slot * 12u);
            trspk_modelarena_unload_element(arena, element_id);
            trspk_pose_table_remove_element(&poses, element_id);
        }

        trspk_modelarena_gc(arena);
    }

    if( alive_model_slots(arena) != 0u )
        FAIL("alive model slots != 0 after cycles");
    if( arena->write_cursor != baseline_cursor )
        FAIL("write_cursor grew: %u -> %u", baseline_cursor, arena->write_cursor);
    if( vbo->capacity > baseline_capacity )
        FAIL("vbo capacity grew after warmup: %u -> %u", baseline_capacity, vbo->capacity);
    if( arena->slot_count > baseline_slots )
        FAIL("slot_count grew after warmup: %u -> %u", baseline_slots, arena->slot_count);
    if( poses.element_count > baseline_pose_elements )
        FAIL(
            "pose element_count grew after warmup: %u -> %u",
            baseline_pose_elements,
            poses.element_count);

    uint32_t probe_base = 0u;
    if( trspk_pose_table_get(&poses, 0, 0, 0, &probe_base) )
        FAIL("pose entry still live after remove cycles");

    trspk_pose_table_free(&poses);
    trspk_modelarena_free(arena);
    trspk_triangles_free(&tri);
    trspk_vbo_free(vbo);
    return 0;
}

static int
test_atlas_grid_steady_state(void)
{
    struct TRSPK_Atlas atlas;
    memset(&atlas, 0, sizeof(atlas));

    if( !trspk_atlas_init_grid(&atlas, 512u, 512u, 64u, 64u, 4u) )
        FAIL("trspk_atlas_init_grid failed");

    uint8_t pixels[64u * 64u * 4u];
    memset(pixels, 0xAB, sizeof(pixels));

    for( int cycle = 0; cycle < 32; ++cycle )
    {
        for( uint32_t slot = 0u; slot < 8u; ++slot )
        {
            if( !trspk_atlas_grid_insert_at(
                    &atlas, slot, pixels, 64u * 4u, 64u, 64u, NULL) )
            {
                trspk_atlas_free(&atlas);
                FAIL("trspk_atlas_grid_insert_at failed slot %u", slot);
            }

            struct TRSPK_AtlasTile tile;
            if( !trspk_atlas_grid_tile_for_slot(&atlas, slot, &tile) )
            {
                trspk_atlas_free(&atlas);
                FAIL("trspk_atlas_grid_tile_for_slot failed slot %u", slot);
            }

            for( uint32_t row = 0u; row < tile.h; ++row )
            {
                uint8_t* dst = atlas.pixels + (size_t)(tile.y + row) * atlas.stride
                    + (size_t)tile.x * atlas.channels;
                memset(dst, 0, (size_t)tile.w * atlas.channels);
            }
        }
    }

    trspk_atlas_free(&atlas);
    return 0;
}

int
main(void)
{
    if( test_modelarena_pose_steady_state() != 0 )
        return 1;
    if( test_atlas_grid_steady_state() != 0 )
        return 1;

    printf("trspk_leak_test ok\n");
    return 0;
}
