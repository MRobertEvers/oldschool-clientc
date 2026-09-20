/* The ES 3.0 painter's index packer, against the rule it replaced.
 *
 * es3_painter_write_indices is the whole of what changed when the painter
 * stopped copying the retained world into a ring and started indexing it where
 * it was baked. The ES2 core's packer writes a page-LOCAL uint16 against a
 * bound window; this one writes an ABSOLUTE uint32 against the model's first
 * vertex in its binding's buffer. Both lanes that run this core -- the
 * browser's WebGL2 and Android's GLES3 -- get it from here. Two things can go wrong in
 * that move and neither would crash:
 *
 *   - the address no longer has 65,536 to stay inside, so an arithmetic
 *     mistake produces a triangle that reads some other model's vertices --
 *     geometry from the wrong loc, drawn without complaint;
 *   - a face the order names but the bake cannot supply must still consume
 *     exactly three indices, or every later face in the run is off by one and
 *     the caller's reservation is wrong.
 *
 * So the oracle here is the rule stated in prose, written out independently,
 * and the packer is checked against it over addresses that straddle the 16-bit
 * boundary the ES2 version could never cross.
 *
 *   make test-es3-index-pack
 */
#include "es3/es3_indices.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

static void
fail(const char* what, unsigned long expected, unsigned long got)
{
    printf("FAIL %s: expected %lu, got %lu\n", what, expected, got);
    g_failures++;
}

/*
 * The rule, written out by hand: face f of a model based at `address` occupies
 * vertices address + 3f, +1, +2; a face at or past the bake's limit is a
 * degenerate triangle on the model's first vertex.
 */
static void
reference(
    uint32_t* out,
    uint32_t address,
    uint32_t limit,
    const int* faces,
    uint32_t count)
{
    uint32_t i;
    for( i = 0u; i < count; i++ )
    {
        int face = faces[i];
        if( face >= 0 && (uint32_t)face < limit )
        {
            out[i * 3u + 0u] = address + (uint32_t)face * 3u;
            out[i * 3u + 1u] = address + (uint32_t)face * 3u + 1u;
            out[i * 3u + 2u] = address + (uint32_t)face * 3u + 2u;
        }
        else
        {
            out[i * 3u + 0u] = address;
            out[i * 3u + 1u] = address;
            out[i * 3u + 2u] = address;
        }
    }
}

static void
check_run(const char* label, uint32_t address, uint32_t limit, const int* faces, uint32_t count)
{
    uint32_t got[3u * 64u];
    uint32_t want[3u * 64u];
    uint32_t i;

    if( count > 64u )
    {
        printf("FAIL %s: the fixture is bigger than the buffers\n", label);
        g_failures++;
        return;
    }
    memset(got, 0xcd, sizeof(got));
    es3_painter_write_indices(got, address, limit, faces, count);
    reference(want, address, limit, faces, count);
    for( i = 0u; i < count * 3u; i++ )
        if( got[i] != want[i] )
        {
            char where[128];
            snprintf(where, sizeof(where), "%s index %u", label, i);
            fail(where, (unsigned long)want[i], (unsigned long)got[i]);
            return;
        }

    /*
     * And the NEON arm against the same reference.
     *
     * Not "the two arms agree" -- both are checked against the independent
     * reference above, so a shared misreading of the contract cannot pass.
     * The arm runs four faces a step and falls into the scalar tail, so every
     * fixture length matters: the counts here deliberately straddle 4.
     *
     * On a host without NEON this is the scalar path twice, which costs a
     * microsecond and keeps the test honest about what it proved.
     */
    memset(got, 0xcd, sizeof(got));
    es3_painter_write_indices_ex(got, address, limit, faces, count, true);
    for( i = 0u; i < count * 3u; i++ )
        if( got[i] != want[i] )
        {
            char where[160];
            snprintf(where, sizeof(where), "%s index %u (NEON arm)", label, i);
            fail(where, (unsigned long)want[i], (unsigned long)got[i]);
            return;
        }
}

int
main(void)
{
    /* A sorted order is a permutation with gaps, not a run: the packer must
     * not assume monotonicity. The last two entries are out of range -- one
     * past the bake, one negative, which is how an unset slot arrives. */
    static const int order[] = { 5, 0, 3, 9, 1, 8, 2, 12, -1 };
    static const uint32_t addresses[] = {
        0u,
        1u,
        /* Straddling what a uint16 index can reach: the address itself, the
         * address plus a face's offset, and both. These are the cases the ES2
         * packer cannot express at all -- it is why this renderer exists --
         * so they are the ones worth stating. */
        65536u - 30u,
        65536u,
        65536u + 7u,
        1000000u,
        /* A static batch's buffer really does get this big: the census on a
         * settled osrs239 scene reports 2,557,530 vertices across 40 pages. */
        2557530u - 60u,
    };
    uint32_t a;

    for( a = 0u; a < sizeof(addresses) / sizeof(addresses[0]); a++ )
    {
        char label[64];
        snprintf(label, sizeof(label), "address %u", addresses[a]);
        check_run(label, addresses[a], 10u, order, sizeof(order) / sizeof(order[0]));
    }

    /* A limit of zero means the bake supplied nothing: every face degenerates,
     * and the run is still exactly three indices per face. */
    check_run("empty bake", 4096u, 0u, order, sizeof(order) / sizeof(order[0]));

    /* A face exactly at the limit is OUT of range; limit - 1 is in. The
     * off-by-one here is the one that draws a neighbouring model's first
     * triangle. */
    {
        static const int edge[] = { 9, 10, 11 };
        check_run("limit edge", 777u, 10u, edge, 3u);
    }

    /* Zero faces must write nothing at all: the caller reserved nothing. */
    {
        uint32_t got[3];
        memset(got, 0xcd, sizeof(got));
        es3_painter_write_indices(got, 100u, 10u, NULL, 0u);
        if( got[0] != 0xcdcdcdcdu )
            fail("empty run wrote something", 0xcdcdcdcdul, (unsigned long)got[0]);
    }

    /* Lengths straddling the four-lane step, so the vector body and the
     * scalar tail are both exercised with in- and out-of-range faces. */
    {
        static const int run[] = { 0, 3, 1, 9, 2, 7, 4, 11, 5, 6, 8, 10, 12 };
        uint32_t n;
        for( n = 1u; n <= (uint32_t)(sizeof(run) / sizeof(run[0])); n++ )
        {
            char label[64];
            snprintf(label, sizeof(label), "step straddle n=%u", n);
            check_run(label, 4096u, 10u, run, n);
        }
    }

    if( g_failures )
    {
        printf("es3_index_pack: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("es3_index_pack: PASS\n");
    return 0;
}
