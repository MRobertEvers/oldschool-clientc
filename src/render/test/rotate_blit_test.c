/*
 * Angle-unit guard for the compass/minimap blit path.
 *
 * ToriRS_RenderCommand_Sprite.rotation_r2pi2048 carries camera yaw, where 2048
 * is a full turn — not the 65536-per-turn scale ToriDraw_SpriteTransformPixels
 * (IF3 spriteAngle) uses. Mixing the two silently under-rotates by 32x, so pin
 * the quarter turns down: a marker pixel placed north of the pivot must land
 * east / south / west at yaw 512 / 1024 / 1536, and back north at 2048.
 */
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

    ToriDraw_SpriteFree(sprite);

    if( g_failures > 0 )
    {
        printf("rotate_blit_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("All rotated-blit tests passed.\n");
    return 0;
}
