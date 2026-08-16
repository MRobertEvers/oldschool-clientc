/*
 * Angle-unit guard for the compass/minimap blit path.
 *
 * ToriRS_RenderCommand_Sprite.rotation_r2pi2048 carries camera yaw, where 2048
 * is a full turn — not the 65536-per-turn scale ToriDraw_SpriteTransformPixels
 * (IF3 spriteAngle) uses. Mixing the two silently under-rotates by 32x, so pin
 * the quarter turns down: a marker pixel placed north of the pivot must land
 * east / south / west at yaw 512 / 1024 / 1536, and back north at 2048.
 */
#include "render/trspk_sprite.h"
#include "toridraw.h"
#include "toridraw_sprite.h"
#include "toridraw_types.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    BOX = 9,
    PIVOT = BOX / 2,
    OFFSET = 3,
    MARKER_ARGB = (int)0xFFFF00FFu,
};

static int g_failures = 0;

/*
 * The blit inverse-maps with a >>16 truncation, so a source pixel can land on
 * more than one destination pixel and biases up to a pixel low/left. Compare
 * the centroid against the ideal position with a pixel of slack — still far
 * tighter than the 32x error this guards against.
 */
static void
check_marker_at(
    char const* what,
    int const* canvas,
    int expect_x,
    int expect_y)
{
    int sum_x = 0;
    int sum_y = 0;
    int count = 0;
    double got_x;
    double got_y;
    double dx;
    double dy;

    for( int y = 0; y < BOX; y++ )
        for( int x = 0; x < BOX; x++ )
            if( canvas[y * BOX + x] == MARKER_ARGB )
            {
                sum_x += x;
                sum_y += y;
                count++;
            }

    if( count == 0 )
    {
        printf("  FAIL %-28s marker not drawn\n", what);
        g_failures++;
        return;
    }

    got_x = (double)sum_x / count;
    got_y = (double)sum_y / count;
    dx = got_x - expect_x;
    dy = got_y - expect_y;
    if( dx * dx + dy * dy <= 1.5 * 1.5 )
    {
        printf("  ok   %-28s marker at (%.1f,%.1f)\n", what, got_x, got_y);
        return;
    }
    printf(
        "  FAIL %-28s marker at (%.1f,%.1f), expected (%d,%d)\n",
        what,
        got_x,
        got_y,
        expect_x,
        expect_y);
    g_failures++;
}

/* One marker pixel `OFFSET` above the sprite centre, everything else clear. */
static struct ToriDraw_Sprite*
marker_sprite(void)
{
    uint32_t* pixels = calloc((size_t)BOX * (size_t)BOX, sizeof(uint32_t));
    assert(pixels);
    pixels[(PIVOT - OFFSET) * BOX + PIVOT] = (uint32_t)MARKER_ARGB;
    return ToriDraw_SpriteNewFromArgbOwned(pixels, BOX, BOX);
}

/* Opaque everywhere; used as a mask that keeps every destination pixel. */
static struct ToriDraw_Sprite*
solid_sprite(void)
{
    uint32_t* pixels = calloc((size_t)BOX * (size_t)BOX, sizeof(uint32_t));
    assert(pixels);
    for( int i = 0; i < BOX * BOX; i++ )
        pixels[i] = 0xFF808080u;
    return ToriDraw_SpriteNewFromArgbOwned(pixels, BOX, BOX);
}

static void
blit_at_yaw(
    struct ToriDraw_Sprite* sprite,
    int yaw_r2pi2048,
    int* canvas)
{
    struct ToriDraw_ViewPort view_port;

    memset(canvas, 0, sizeof(int) * (size_t)BOX * (size_t)BOX);
    memset(&view_port, 0, sizeof(view_port));
    view_port.width = BOX;
    view_port.height = BOX;
    view_port.stride = BOX;
    view_port.clip_right = BOX;
    view_port.clip_bottom = BOX;

    ToriDraw2D_BlitSpriteRotatedEx(
        sprite, &view_port, 0, 0, BOX, BOX, PIVOT, PIVOT, PIVOT, PIVOT, yaw_r2pi2048, canvas);
}

/*
 * The GPU backends bake the rotated mask into one reused buffer rather than a
 * fresh calloc per frame. The blit only writes the pixels the rotated source
 * covers, so a bake that does not wipe first inherits the previous bake's
 * corners -- on the minimap that is last frame's rotation smeared under this
 * one, and it only shows once the buffer has been used at least twice.
 *
 * Bake the marker at two yaws a quarter turn apart through the same bake and
 * assert the second carries exactly one marker. Two means the first survived.
 */
static int
bake_and_count(
    struct TRSPK_RotmaskBake* bake,
    struct ToriDraw_Sprite* sprite,
    struct ToriDraw_Sprite* mask,
    int yaw_r2pi2048,
    int needs_clear)
{
    struct ToriRS_RenderCommand_Sprite cmd;
    uint32_t const* baked;
    int marked = 0;

    memset(&cmd, 0, sizeof(cmd));
    cmd.dst_anchor_x = PIVOT;
    cmd.dst_anchor_y = PIVOT;
    cmd.src_anchor_x = PIVOT;
    cmd.src_anchor_y = PIVOT;
    cmd.mask_keep_opaque = 1;
    cmd.rotation_r2pi2048 = yaw_r2pi2048;
    cmd.mask_needs_clear = (uint8_t)needs_clear;

    baked = trspk_sprite_rotmask_bake(bake, &cmd, sprite, mask, BOX, BOX);
    assert(baked);
    for( int i = 0; i < BOX * BOX; i++ )
        if( (baked[i] & 0x00FFFFFFu) != 0u )
            marked++;
    return marked;
}

static void
expect_count(
    char const* what,
    int got,
    int want)
{
    if( got == want )
    {
        printf("  ok   %-28s %d px\n", what, got);
        return;
    }
    printf("  FAIL %-28s %d px, expected %d\n", what, got, want);
    g_failures++;
}

static void
expect_more_than(
    char const* what,
    int got,
    int floor_)
{
    if( got > floor_ )
    {
        printf("  ok   %-28s %d px vs %d cleared\n", what, got, floor_);
        return;
    }
    printf("  FAIL %-28s %d px, expected more than %d\n", what, got, floor_);
    g_failures++;
}

static void
check_bake_reuse_is_clean(struct ToriDraw_Sprite* sprite)
{
    /* Opaque everywhere, so the mask gates nothing and the marker is free to
     * rotate. A trimmed mask would confound this with its own windowing. */
    struct ToriDraw_Sprite* mask = solid_sprite();
    struct TRSPK_RotmaskBake fresh = { 0 };
    struct TRSPK_RotmaskBake cleared = { 0 };
    struct TRSPK_RotmaskBake kept = { 0 };
    int one_bake;

    /* The marker maps to more than one destination pixel (the >>16 inverse map
     * truncates), so the count is not 1 -- measure one bake into a never-used
     * buffer and compare the reuse cases against that rather than a literal. */
    one_bake = bake_and_count(&fresh, sprite, mask, 512, 1);

    /* mask_needs_clear = 1: the second bake must look like the first. */
    (void)bake_and_count(&cleared, sprite, mask, 0, 1);
    expect_count(
        "reuse + clear wipes stale", bake_and_count(&cleared, sprite, mask, 512, 1), one_bake);

    /*
     * mask_needs_clear = 0: the caller asked to keep what was there, so yaw 0's
     * marker survives alongside yaw 512's. This is the half that proves the
     * flag is consulted at all -- without it the "clear" case above would pass
     * on a bake that always wipes.
     *
     * Asserted as "more than the cleared count" rather than a sum: the two
     * yaws land on different numbers of pixels (the inverse map's >>16 can
     * duplicate one source pixel), and pinning that arithmetic would be
     * pinning the blit's rounding, not this buffer's reuse policy.
     *
     * The first bake grows the buffer and is wiped regardless -- realloc's new
     * tail is uninitialised, so what survives here is a previous bake, never
     * heap garbage.
     */
    (void)bake_and_count(&kept, sprite, mask, 0, 0);
    expect_more_than(
        "reuse without clear keeps", bake_and_count(&kept, sprite, mask, 512, 0), one_bake);

    trspk_sprite_rotmask_bake_release(&fresh);
    trspk_sprite_rotmask_bake_release(&cleared);
    trspk_sprite_rotmask_bake_release(&kept);
    ToriDraw_SpriteFree(mask);
}

int
main(void)
{
    struct ToriDraw_Sprite* sprite;
    int canvas[BOX * BOX];

    ToriDraw_Init();
    sprite = marker_sprite();
    assert(sprite);

    printf("TEST: rotated blit takes camera-yaw units (2048 = full turn)\n");

    blit_at_yaw(sprite, 0, canvas);
    check_marker_at("yaw 0 (north)", canvas, PIVOT, PIVOT - OFFSET);

    blit_at_yaw(sprite, 512, canvas);
    check_marker_at("yaw 512 (quarter turn)", canvas, PIVOT + OFFSET, PIVOT);

    blit_at_yaw(sprite, 1024, canvas);
    check_marker_at("yaw 1024 (half turn)", canvas, PIVOT, PIVOT + OFFSET);

    blit_at_yaw(sprite, 1536, canvas);
    check_marker_at("yaw 1536 (three quarters)", canvas, PIVOT - OFFSET, PIVOT);

    /* Wraps, and does not assert inside ToriDraw_Sin/Cos. */
    blit_at_yaw(sprite, 2048, canvas);
    check_marker_at("yaw 2048 wraps to 0", canvas, PIVOT, PIVOT - OFFSET);

    blit_at_yaw(sprite, -512, canvas);
    check_marker_at("yaw -512 wraps to 1536", canvas, PIVOT - OFFSET, PIVOT);

    /* The bug this guards: yaw fed to a 65536-per-turn consumer barely moves. */
    blit_at_yaw(sprite, 16, canvas);
    check_marker_at("yaw 16 is a small nudge", canvas, PIVOT, PIVOT - OFFSET);

    check_bake_reuse_is_clean(sprite);

    ToriDraw_SpriteFree(sprite);

    if( g_failures > 0 )
    {
        printf("rotate_blit_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("All rotated-blit tests passed.\n");
    return 0;
}
