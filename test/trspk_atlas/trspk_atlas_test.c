#include "trspk_atlas.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FAIL(...)                                                                                  \
    do                                                                                             \
    {                                                                                              \
        fprintf(stderr, __VA_ARGS__);                                                              \
        fprintf(stderr, "\n");                                                                   \
        return 1;                                                                                  \
    } while( 0 )

static int
expect_dirty_rect(
    const struct TRSPK_Atlas* atlas,
    uint32_t                  x,
    uint32_t                  y,
    uint32_t                  w,
    uint32_t                  h)
{
    struct TRSPK_AtlasDirtyRect rect;
    if( !trspk_atlas_is_dirty(atlas) || !trspk_atlas_get_dirty_rect(atlas, &rect) )
        FAIL("expected dirty atlas");
    if( rect.x != x || rect.y != y || rect.w != w || rect.h != h )
        FAIL(
            "dirty rect was %u,%u %ux%u; expected %u,%u %ux%u",
            rect.x, rect.y, rect.w, rect.h, x, y, w, h);
    return 0;
}

static int
expect_clean(const struct TRSPK_Atlas* atlas)
{
    struct TRSPK_AtlasDirtyRect rect = { 99u, 99u, 99u, 99u };
    if( trspk_atlas_is_dirty(atlas) || trspk_atlas_get_dirty_rect(atlas, &rect) )
        FAIL("expected clean atlas");
    return 0;
}

static int
test_grid_insert_and_merge(void)
{
    struct TRSPK_Atlas atlas = { 0 };
    if( !trspk_atlas_init_grid(&atlas, 8u, 8u, 4u, 4u, 1u) )
        FAIL("grid init failed");
    if( expect_clean(&atlas) )
        return 1;

    const uint8_t first[6] = { 1u, 2u, 3u, 4u, 5u, 6u };
    if( !trspk_atlas_grid_insert_at(&atlas, 3u, first, 2u, 2u, 3u, NULL) )
        FAIL("grid insert failed");
    if( expect_dirty_rect(&atlas, 4u, 4u, 2u, 3u) )
        return 1;

    const uint8_t second = 7u;
    if( !trspk_atlas_grid_insert_at(&atlas, 0u, &second, 1u, 1u, 1u, NULL) )
        FAIL("second grid insert failed");
    if( expect_dirty_rect(&atlas, 0u, 0u, 6u, 7u) )
        return 1;

    if( atlas.pixels[0] != 7u || atlas.pixels[4u * atlas.stride + 4u] != 1u ||
        atlas.pixels[6u * atlas.stride + 5u] != 6u )
        FAIL("grid insert copied incorrect pixels");

    trspk_atlas_clear_dirty(&atlas);
    if( expect_clean(&atlas) )
        return 1;

    /* Reserving/querying a slot does not alter pixels or dirty state. */
    if( !trspk_atlas_grid_insert_at(&atlas, 1u, NULL, 0u, 4u, 4u, NULL) )
        FAIL("grid reservation failed");
    if( expect_clean(&atlas) )
        return 1;

    trspk_atlas_free(&atlas);
    return 0;
}

static int
test_update_clear_and_legacy_api(void)
{
    struct TRSPK_Atlas atlas = { 0 };
    if( !trspk_atlas_init_grid(&atlas, 8u, 8u, 4u, 4u, 2u) )
        FAIL("grid init failed");

    const uint8_t pixels[12] = {
        1u, 2u, 3u, 4u, 5u, 6u,
        7u, 8u, 9u, 10u, 11u, 12u,
    };
    if( !trspk_atlas_update_rect(&atlas, 2u, 3u, pixels, 6u, 3u, 2u) )
        FAIL("rect update failed");
    if( expect_dirty_rect(&atlas, 2u, 3u, 3u, 2u) )
        return 1;

    if( !trspk_atlas_clear_rect(&atlas, 4u, 4u, 3u, 3u) )
        FAIL("rect clear failed");
    if( expect_dirty_rect(&atlas, 2u, 3u, 5u, 4u) )
        return 1;

    for( uint32_t y = 4u; y < 7u; y++ )
        for( uint32_t x = 4u; x < 7u; x++ )
            for( uint32_t channel = 0u; channel < atlas.channels; channel++ )
                if( atlas.pixels[y * atlas.stride + x * atlas.channels + channel] != 0u )
                    FAIL("rect clear left a nonzero pixel");

    trspk_atlas_clear_dirty(&atlas);
    if( trspk_atlas_update_rect(&atlas, 7u, 7u, pixels, 6u, 2u, 2u) )
        FAIL("out-of-bounds update unexpectedly succeeded");
    if( trspk_atlas_clear_rect(&atlas, UINT32_MAX, 0u, 1u, 1u) )
        FAIL("overflowing clear unexpectedly succeeded");
    if( expect_clean(&atlas) )
        return 1;

    /* Old direct-write callers still request a safe full upload. */
    trspk_atlas_set_dirty(&atlas);
    if( expect_dirty_rect(&atlas, 0u, 0u, 8u, 8u) )
        return 1;

    trspk_atlas_clear_dirty(&atlas);
    memset(atlas.pixels, 0xff, (size_t)atlas.stride * atlas.height);
    trspk_atlas_clear(&atlas);
    if( expect_dirty_rect(&atlas, 0u, 0u, 8u, 8u) )
        return 1;
    for( uint32_t i = 0u; i < atlas.stride * atlas.height; i++ )
        if( atlas.pixels[i] != 0u )
            FAIL("full clear left a nonzero byte");

    trspk_atlas_free(&atlas);
    return 0;
}

static int
test_binpack_insert_bounds(void)
{
    struct TRSPK_Atlas atlas = { 0 };
    if( !trspk_atlas_init_binpack(&atlas, 16u, 16u, 4u) )
        FAIL("binpack init failed");

    uint8_t pixels[5u * 3u * 4u];
    memset(pixels, 0x5a, sizeof(pixels));
    struct TRSPK_AtlasTile tile;
    if( !trspk_atlas_binpack_insert(&atlas, pixels, 5u * 4u, 5u, 3u, &tile) )
        FAIL("binpack insert failed");
    if( expect_dirty_rect(&atlas, tile.x, tile.y, 5u, 3u) )
        return 1;

    trspk_atlas_clear_dirty(&atlas);
    if( !trspk_atlas_binpack_insert(&atlas, NULL, 0u, 2u, 2u, &tile) )
        FAIL("binpack reservation failed");
    if( expect_clean(&atlas) )
        return 1;

    trspk_atlas_free(&atlas);
    return 0;
}

int
main(void)
{
    if( test_grid_insert_and_merge() || test_update_clear_and_legacy_api() ||
        test_binpack_insert_bounds() )
        return 1;

    printf("trspk_atlas_test: ok\n");
    return 0;
}
