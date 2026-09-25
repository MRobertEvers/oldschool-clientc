/*
 * Plugin navigation in a lane's pop-out column: the geometry, the composed
 * button picture and the mode names.
 *
 * The numbers are the OSRS239 lane's own. Script 5356 lays its three launcher
 * buttons 30x30 at y 0, 36 and 72, so they end at 102 and the next button of
 * its own would go at 108; `popout:buttons` is 491 tall at 765x503.
 *
 * Run: make -C src test-chrome-popout-nav
 */

#include "ui/torirs_chrome_exec_kind.h"
#include "ui/torirs_chrome_popout_nav.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

#define CHECK(cond, what)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL: %s (%s:%d)\n", what, __FILE__, __LINE__);                                \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

enum
{
    SIDE = TORIRS_POPOUT_NAV_BUTTON
};

static uint32_t
at(uint32_t const* picture, int column, int row)
{
    return picture[(row * SIDE) + column];
}

/** Every opaque badge pixel is in the bottom-right corner, exactly. */
static int
badge_exact(uint32_t const* picture)
{
    int const left = SIDE - TORIRS_CHROME_TORIFACE_W;
    int const top = SIDE - TORIRS_CHROME_TORIFACE_H;
    for( int row = 0; row < TORIRS_CHROME_TORIFACE_H; row++ )
        for( int column = 0; column < TORIRS_CHROME_TORIFACE_W; column++ )
        {
            uint32_t const pixel = ToriRSChromeToriFace_Pixel(column, row);
            if( (pixel >> 24) && at(picture, left + column, top + row) != pixel )
                return 0;
        }
    return 1;
}

static void
test_geometry(void)
{
    CHECK(ToriRSPopoutNav_FirstY(102) == 108, "first engine button one pitch below the lane's last");
    CHECK(ToriRSPopoutNav_FirstY(-1) == 0, "a column showing no lane button starts at the top");
    CHECK(ToriRSPopoutNav_Capacity(108, 491) == 10, "ten buttons fit under three lane buttons at 503");
    CHECK(ToriRSPopoutNav_Capacity(108, 138) == 1, "exactly one button's height fits one button");
    CHECK(ToriRSPopoutNav_Capacity(108, 137) == 0, "a pixel short fits none");
    CHECK(ToriRSPopoutNav_Capacity(108, 60) == 0, "a column shorter than its lane buttons fits none");
}

static void
test_small_icon_centred(void)
{
    uint32_t icon[10 * 10];
    uint32_t picture[SIDE * SIDE];
    for( int i = 0; i < 100; i++ )
        icon[i] = 0xFFFF0000u;
    ToriRSPopoutNav_ComposeButton(icon, 10, 10, picture);
    CHECK(at(picture, 10, 10) == 0xFFFF0000u, "a small icon's first pixel lands centred");
    /* Row 14 is the icon's last row above the badge, which covers x >= 18 from row 15. */
    CHECK(at(picture, 19, 14) == 0xFFFF0000u, "a small icon is not scaled");
    CHECK(at(picture, 9, 10) == 0, "left of the icon stays transparent");
    CHECK(at(picture, 20, 10) == 0, "right of the icon stays transparent");
    CHECK(at(picture, 0, 0) == 0, "the top-left corner stays transparent");
    CHECK(badge_exact(picture), "the badge is drawn in the bottom-right corner");
}

static void
test_large_icon_fitted(void)
{
    static uint32_t icon[64 * 32];
    uint32_t picture[SIDE * SIDE];
    int const drawn_w = TORIRS_POPOUT_NAV_ICON_MAX;
    int const drawn_h = 32 * TORIRS_POPOUT_NAV_ICON_MAX / 64;
    int const left = (SIDE - drawn_w) / 2;
    int const top = (SIDE - drawn_h) / 2;
    for( int row = 0; row < 32; row++ )
        for( int column = 0; column < 64; column++ )
            icon[(row * 64) + column] = column < 32 ? 0xFF00FF00u : 0xFF0000FFu;
    ToriRSPopoutNav_ComposeButton(icon, 64, 32, picture);
    CHECK(at(picture, left, top) == 0xFF00FF00u, "a wide icon is fitted to the icon box from its left");
    CHECK(at(picture, left - 1, top) == 0, "nothing is drawn left of the fitted icon");
    CHECK(at(picture, left, top - 1) == 0, "nothing is drawn above the fitted icon");
    CHECK(at(picture, left + drawn_w - 1, top) == 0xFF0000FFu, "the fitted icon keeps its right half");
    CHECK(at(picture, left, top + drawn_h) == 0, "the fitted icon keeps its aspect");
    CHECK(badge_exact(picture), "the badge is drawn over a large icon");
}

static void
test_badge_art(void)
{
    int opaque = 0;
    for( int row = 0; row < TORIRS_CHROME_TORIFACE_H; row++ )
        for( int column = 0; column < TORIRS_CHROME_TORIFACE_W; column++ )
        {
            uint32_t const pixel = ToriRSChromeToriFace_Pixel(column, row);
            CHECK((pixel >> 24) == 0 || (pixel >> 24) == 0xFF, "badge alpha is all or nothing");
            if( pixel >> 24 )
                opaque++;
        }
    CHECK(opaque > (TORIRS_CHROME_TORIFACE_W * TORIRS_CHROME_TORIFACE_H) / 2, "the badge is mostly face");
    /* The lenses are the lightest pixels in the face; without them the badge
     * reads as a brown blob, which is why the reduction is hand-drawn. */
    CHECK(ToriRSChromeToriFace_Pixel(2, 8) == 0xFFECDEB8u, "the left lens is drawn");
    CHECK(ToriRSChromeToriFace_Pixel(8, 8) == 0xFFECDEB8u, "the right lens is drawn");
}

static void
test_mode_names(void)
{
    CHECK(ToriRSPluginNav_ModeFromName("auto") == TORIRS_PLUGIN_NAV_AUTO, "auto parses");
    CHECK(ToriRSPluginNav_ModeFromName("rail") == TORIRS_PLUGIN_NAV_RAIL, "rail parses");
    CHECK(ToriRSPluginNav_ModeFromName("popout") < 0, "an unknown mode is refused");
    CHECK(strcmp(ToriRSPluginNav_ModeName(TORIRS_PLUGIN_NAV_RAIL), "rail") == 0, "rail's name");
    CHECK(strcmp(ToriRSPluginNav_ModeName(TORIRS_PLUGIN_NAV_AUTO), "auto") == 0, "auto's name");
}

int
main(void)
{
    test_geometry();
    test_small_icon_centred();
    test_large_icon_fitted();
    test_badge_art();
    test_mode_names();
    if( g_failures )
    {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("All popout nav tests passed.\n");
    return 0;
}
