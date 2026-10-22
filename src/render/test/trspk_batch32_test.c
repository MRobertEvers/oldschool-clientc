/* TRSPK_Batch32: one 32-bit world build, located by vertex base.
 *
 * What a renderer relies on: every pose lands contiguously at the base it was
 * given, a key resolves to exactly that entry and nothing else does, a repeat
 * of a key in the same build hands back the original storage, and a new build
 * or a clear leaves nothing of the old one reachable. Past 65535 vertices on
 * purpose -- the page limit Batch16 exists for is the one this does not have.
 *
 *   make test-trspk-batch32
 */
#include "core/trspk_batch32.h"
#include "core/trspk_vbo.h"

#include <stdio.h>
#include <stdlib.h>

#define ELEMENTS 300
#define POSES 4
/* 300 * 4 * 99 = 118,800 vertices: well past one 16-bit page. */
#define POSE_VERTICES 99u

static int g_failures = 0;

static void
check(
    int ok,
    const char* what)
{
    if( ok )
        printf("  ok   %s\n", what);
    else
    {
        printf("  FAIL %s\n", what);
        g_failures++;
    }
}

int
main(void)
{
    struct TRSPK_Batch32* batch;
    struct TRSPK_Batch32Reservation r;
    uint32_t expected_base = 0u;
    int bases_ok = 1;
    int found_ok = 1;
    int e;
    int p;

    printf("trspk-batch32\n");

    batch = trspk_batch32_create(TRSPK_VERTEX_FORMAT_OPENGL3);
    trspk_batch32_begin(batch);
    for( e = 0; e < ELEMENTS; e++ )
    {
        for( p = 0; p < POSES; p++ )
        {
            trspk_batch32_reserve_pose(batch, e, p & 1, p, POSE_VERTICES, &r);
            if( r.vertex_base != expected_base || r.vbo != trspk_batch32_vbo(batch) )
                bases_ok = 0;
            expected_base += POSE_VERTICES;
        }
    }
    check(bases_ok, "every pose lands contiguously at the base it was given");
    check(
        trspk_batch32_vbo(batch)->vertex_count == expected_base && expected_base > 65535u,
        "one buffer holds the whole build, past a 16-bit page");
    check(
        trspk_batch32_triangles(batch)->cap >= expected_base / 3u,
        "the triangle table covers every reserved triangle");

    trspk_batch32_reserve_pose(batch, 7, 1, 3, POSE_VERTICES, &r);
    check(
        r.vertex_base == (uint32_t)(7 * POSES + 3) * POSE_VERTICES &&
            trspk_batch32_vbo(batch)->vertex_count == expected_base,
        "a repeated key returns its original storage and reserves nothing");
    trspk_batch32_end(batch);

    for( e = 0; e < ELEMENTS && found_ok; e++ )
    {
        for( p = 0; p < POSES; p++ )
        {
            const struct TRSPK_Batch32Entry* entry =
                trspk_batch32_find_entry(batch, e, p & 1, p);
            if( !entry || entry->element_id != e || entry->pose_id != p ||
                entry->vertex_base != (uint32_t)(e * POSES + p) * POSE_VERTICES ||
                entry->vertex_count != POSE_VERTICES )
            {
                found_ok = 0;
                break;
            }
        }
    }
    check(found_ok, "every key resolves to its own entry");
    check(
        trspk_batch32_find_entry(batch, ELEMENTS + 1, 0, 0) == NULL &&
            trspk_batch32_find_entry(batch, 0, 1, 0) == NULL,
        "absent keys miss (pose 0 was baked on track 0 only)");
    check(trspk_batch32_entry_count(batch) == ELEMENTS * POSES, "one entry per distinct key");

    /* A rebuild replaces the build; nothing of the old one may resolve. */
    trspk_batch32_begin(batch);
    trspk_batch32_reserve_pose(batch, 1000, 0, 0, 6u, &r);
    trspk_batch32_end(batch);
    check(
        r.vertex_base == 0u && trspk_batch32_vbo(batch)->vertex_count == 6u &&
            trspk_batch32_find_entry(batch, 0, 0, 0) == NULL &&
            trspk_batch32_find_entry(batch, 1000, 0, 0) != NULL,
        "a new build starts at zero and forgets the old one");

    trspk_batch32_clear(batch);
    check(
        trspk_batch32_vbo(batch)->vertex_count == 0u &&
            trspk_batch32_entry_count(batch) == 0u &&
            trspk_batch32_find_entry(batch, 1000, 0, 0) == NULL &&
            !trspk_batch32_building(batch),
        "clear leaves nothing reachable");

    trspk_batch32_destroy(batch);

    if( g_failures )
    {
        printf("%d failures\n", g_failures);
        return 1;
    }
    printf("All trspk batch32 tests passed.\n");
    return 0;
}
