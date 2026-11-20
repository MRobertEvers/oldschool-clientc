/*
 * The 2D library's packs that shift a signed int into or past bit 31.
 *
 *   - ToriDraw2D_FillRectGradientVertical / ...GradientAlpha pack an ARGB word
 *     as (alpha << 24) | ...: every alpha of 128 or more is a shift into the
 *     sign bit, which C leaves undefined.
 *   - ToriDraw2D_BlitSpriteRotated starts its walk at anchor << 16; an anchor
 *     left of or above the sprite is a shift of a negative value.
 *
 * Built plain the old code passes this too (the compilers wrap); under
 * check-ubsan-units (-fsanitize=undefined -fno-sanitize-recover) it stops at
 * the first. The checks pin what the wrap gives: an opaque gradient draws its
 * colours and a half-transparent one blends; the rotated blit runs with a
 * pivot outside the sprite, and with one at its corner draws.
 */

#include "toridraw_2d.h"
#include "toridraw_alloc.h"
#include "toridraw_sprite.h"
#include "toridraw_types.h"
#include "graphics/pixel_format.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 64
#define H 48

static int g_checks;
static int g_failures;

static void
check(
    int ok,
    const char* what)
{
    g_checks++;
    if( !ok )
    {
        g_failures++;
        printf("  FAIL %s\n", what);
    }
}

static struct ToriDraw_ViewPort
viewport(void)
{
    struct ToriDraw_ViewPort vp;
    memset(&vp, 0, sizeof(vp));
    vp.width = W;
    vp.height = H;
    vp.stride = W;
    vp.x_center = W / 2;
    vp.y_center = H / 2;
    vp.clip_left = 0;
    vp.clip_top = 0;
    vp.clip_right = W;
    vp.clip_bottom = H;
    return vp;
}

static void
test_gradients(void)
{
    static toripixel_t px[W * H];
    struct ToriDraw_ViewPort vp = viewport();

    memset(px, 0, sizeof(px));
    ToriDraw2D_FillRectGradientVertical(&vp, 0, 0, W, H, 0xff0000, 0x0000ff, 255, px);
    check(px[0] == toripixel_pack_argb8888(0xffff0000u), "an opaque gradient's first row is its top colour");
    check(px[(H - 1) * W] != px[0], "an opaque gradient changes colour down the rect");

    memset(px, 0, sizeof(px));
    ToriDraw2D_FillRectGradientAlpha(&vp, 0, 0, W, H, 0x00ff00, 0x00ff00, 255, 128, px);
    check(px[0] == toripixel_pack_argb8888(0xff00ff00u), "alpha 255 at the top draws the colour");
    check(px[(H - 1) * W] != px[0] && px[(H - 1) * W] != 0, "alpha 128 at the bottom blends over black");
}

static void
test_rotated_blit_anchor(void)
{
    static toripixel_t at_negative[W * H];
    static toripixel_t at_zero[W * H];
    struct ToriDraw_ViewPort vp = viewport();
    /* The sprite owns and frees these, through the library's allocator. */
    uint32_t* argb = TORIDRAW_MALLOC(sizeof(uint32_t) * 16 * 16);
    struct ToriDraw_Sprite* sprite;
    int drawn = 0;

    assert(argb);
    for( int i = 0; i < 16 * 16; i++ )
        argb[i] = 0xff000000u | (uint32_t)(i * 977);
    sprite = ToriDraw_SpriteNewFromArgbOwned(argb, 16, 16);

    /* A pivot left of and above the sprite, and one at its corner, at eight
     * angles. */
    memset(at_negative, 0, sizeof(at_negative));
    memset(at_zero, 0, sizeof(at_zero));
    for( int angle = 0; angle < 2048; angle += 256 )
    {
        ToriDraw2D_BlitSpriteRotated(sprite, &vp, 8, 8, -3, -5, 40, 30, angle, at_negative);
        ToriDraw2D_BlitSpriteRotated(sprite, &vp, 8, 8, 0, 0, 40, 30, angle, at_zero);
    }
    for( int i = 0; i < W * H; i++ )
        drawn += at_zero[i] != 0;
    check(drawn > 0, "a rotated blit at anchor 0 draws");
    ToriDraw_SpriteFree(sprite);
}

int
main(void)
{
    test_gradients();
    test_rotated_blit_anchor();
    printf("toridraw_2d_wrap_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
