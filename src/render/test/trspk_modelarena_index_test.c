/* The indexed find must answer exactly what the old scan answered.
 *
 * trspk_modelarena_find used to walk every slot; it is now a hash bucket
 * chained through the slots. That is a real data structure with real ways to
 * be wrong -- a stale bucket head after a free, an entry lost when the table
 * grows and rehashes, a slot still reachable after clear() -- and every one of
 * those shows up as a model drawn with another model's geometry rather than as
 * a crash.
 *
 * So every check here is against a brute-force scan of the slots performed in
 * this file. Whatever the index says, the scan is the truth.
 *
 *   make test-trspk-modelarena-index
 */
#include "core/trspk_modelarena.h"
#include "core/trspk_triangles.h"
#include "core/trspk_vbo.h"

#include <stdio.h>
#include <stdlib.h>

#define ELEMENTS 64
#define POSES 8

static int g_failures = 0;

/* What the old implementation did, kept as the oracle. */
static uint32_t
reference_find(const struct TRSPK_ModelArena* arena, int element_id, int pose_id)
{
    uint32_t i;
    for( i = 0u; i < arena->slot_count; ++i )
    {
        const struct TRSPK_ModelSlot* slot = &arena->slots[i];
        if( (slot->flags & TRSPK_MODELSLOT_FLAG_ALIVE) != 0u &&
            slot->element_id == element_id && slot->pose_id == pose_id )
            return i;
    }
    return TRSPK_MODELSLOT_NULL_IDX;
}

static void
check_all(const struct TRSPK_ModelArena* arena, const char* what)
{
    int e;
    int p;
    for( e = 0; e < ELEMENTS; e++ )
    {
        for( p = 0; p < POSES; p++ )
        {
            uint32_t want = reference_find(arena, e, p);
            uint32_t got = trspk_modelarena_find(arena, e, p);
            if( want != got )
            {
                printf("  FAIL %s: (%d,%d) index says %u, scan says %u\n",
                    what, e, p, (unsigned)got, (unsigned)want);
                g_failures++;
                return;
            }
        }
    }
    /* Keys that were never inserted must miss. */
    if( trspk_modelarena_find(arena, ELEMENTS + 5, 0) != TRSPK_MODELSLOT_NULL_IDX ||
        trspk_modelarena_find(arena, 0, POSES + 5) != TRSPK_MODELSLOT_NULL_IDX )
    {
        printf("  FAIL %s: absent key was found\n", what);
        g_failures++;
    }
}

int
main(void)
{
    struct TRSPK_VBO* vbo;
    struct TRSPK_Triangles triangles;
    struct TRSPK_ModelArena* arena;
    uint32_t slots[ELEMENTS][POSES];
    int e;
    int p;

    printf("trspk-modelarena-index\n");

    vbo = trspk_vbo_create(1024u, TRSPK_VERTEX_FORMAT_D3D9);
    if( !vbo )
    {
        fprintf(stderr, "no vbo\n");
        return 1;
    }
    memset(&triangles, 0, sizeof(triangles));
    /* A small initial capacity on purpose: the table has to grow and rehash
     * several times over the loop below, which is where entries get lost. */
    arena = trspk_modelarena_create(vbo, &triangles, 3u, 4u);
    if( !arena )
    {
        fprintf(stderr, "no arena\n");
        return 1;
    }

    for( e = 0; e < ELEMENTS; e++ )
        for( p = 0; p < POSES; p++ )
            slots[e][p] = trspk_modelarena_load(arena, e, p, 3u);
    check_all(arena, "after loading every key");

    /* Every slot must still be findable at its own index. */
    for( e = 0; e < ELEMENTS; e++ )
    {
        for( p = 0; p < POSES; p++ )
        {
            if( trspk_modelarena_find(arena, e, p) != slots[e][p] )
            {
                printf("  FAIL (%d,%d) does not resolve to the slot load returned\n", e, p);
                g_failures++;
                e = ELEMENTS;
                break;
            }
        }
    }
    printf("  ok   every loaded key resolves to its own slot\n");

    /* Unload a scattered third, including bucket heads and chain interiors. */
    for( e = 0; e < ELEMENTS; e++ )
        for( p = 0; p < POSES; p++ )
            if( ((e * POSES) + p) % 3 == 0 )
                trspk_modelarena_unload(arena, slots[e][p]);
    check_all(arena, "after a scattered unload");
    printf("  ok   unloaded keys miss, survivors still hit\n");

    /* Reload them, which reuses freed slots off the free list. */
    for( e = 0; e < ELEMENTS; e++ )
        for( p = 0; p < POSES; p++ )
            if( ((e * POSES) + p) % 3 == 0 )
                slots[e][p] = trspk_modelarena_load(arena, e, p, 3u);
    check_all(arena, "after reloading into freed slots");
    printf("  ok   reload through the free list stays consistent\n");

    /* Whole elements at once, the path the renderer uses on despawn. */
    for( e = 0; e < ELEMENTS; e += 7 )
        trspk_modelarena_unload_element(arena, e);
    check_all(arena, "after unload_element");
    printf("  ok   unload_element removes every pose of the element\n");

    trspk_modelarena_clear(arena);
    for( e = 0; e < ELEMENTS; e++ )
    {
        for( p = 0; p < POSES; p++ )
        {
            if( trspk_modelarena_find(arena, e, p) != TRSPK_MODELSLOT_NULL_IDX )
            {
                printf("  FAIL (%d,%d) still found after clear\n", e, p);
                g_failures++;
                e = ELEMENTS;
                break;
            }
        }
    }
    printf("  ok   clear leaves nothing reachable\n");

    /* And the arena is still usable afterwards. */
    slots[0][0] = trspk_modelarena_load(arena, 11, 3, 3u);
    if( trspk_modelarena_find(arena, 11, 3) != slots[0][0] )
    {
        printf("  FAIL reload after clear\n");
        g_failures++;
    }
    else
        printf("  ok   the arena still works after clear\n");

    trspk_modelarena_free(arena);
    trspk_triangles_free(&triangles);
    trspk_vbo_free(vbo);

    if( g_failures )
    {
        printf("%d failures\n", g_failures);
        return 1;
    }
    printf("All trspk model arena index tests passed.\n");
    return 0;
}
