#include "torirs_chrome_popout_nav.h"

#include <assert.h>
#include <string.h>

/*
 * The Tori face badge.
 *
 * Hand-reduced from res/toriface.jpg, whose art is a 21x29 pixel grid drawn at
 * ~29.6 px per pixel over a painted (not transparent) checkerboard. A box
 * filter to this size loses the glasses, which are the face, so the reduction
 * is authored rather than computed: outline K, hair D/H/L, skin S/s, glasses
 * G with lenses E, mouth M, transparent '.'. res/toriface_badge.png is this
 * same picture as an image file; change both together.
 */
static char const* const k_toriface_rows[TORIRS_CHROME_TORIFACE_H] = {
    ".......KKK..",
    "......KLHHK.",
    "...KKKHHDHK.",
    "..KHDHHLHHHK",
    ".KHHDKKKHDHK",
    ".KHKSSSSKHHK",
    "KHKSSSSSSKHK",
    "KGGGGSGGGGHK",
    "KHEGSSSGEGHK",
    "KHGGSsSGGKHK",
    "KHKSSSSSSKHK",
    ".KHSSMMSSKHK",
    ".KHKSSSSKHK.",
    "..KKKSSKKK..",
    "....KKKK....",
};

static uint32_t
toriface_colour(char code)
{
    switch( code )
    {
    case 'K':
        return 0xFF000000u;
    case 'D':
        return 0xFF221C10u;
    case 'H':
        return 0xFF463222u;
    case 'L':
        return 0xFF684632u;
    case 'S':
        return 0xFFE2A272u;
    case 's':
        return 0xFFB47252u;
    case 'G':
        return 0xFF2C1E16u;
    case 'E':
        return 0xFFECDEB8u;
    case 'M':
        return 0xFFBE6E5Eu;
    default:
        return 0;
    }
}

uint32_t
ToriRSChromeToriFace_Pixel(int column, int row)
{
    assert(column >= 0);
    assert(column < TORIRS_CHROME_TORIFACE_W);
    assert(row >= 0);
    assert(row < TORIRS_CHROME_TORIFACE_H);
    return toriface_colour(k_toriface_rows[row][column]);
}

int
ToriRSPopoutNav_FirstY(int native_bottom)
{
    if( native_bottom < 0 )
        return 0;
    return native_bottom + (TORIRS_POPOUT_NAV_PITCH - TORIRS_POPOUT_NAV_BUTTON);
}

int
ToriRSPopoutNav_Capacity(int first_y, int column_h)
{
    int room;
    assert(first_y >= 0);
    room = column_h - first_y;
    if( room < TORIRS_POPOUT_NAV_BUTTON )
        return 0;
    return 1 + (room - TORIRS_POPOUT_NAV_BUTTON) / TORIRS_POPOUT_NAV_PITCH;
}

void
ToriRSPopoutNav_ComposeButton(
    uint32_t const* src,
    int src_w,
    int src_h,
    uint32_t* out)
{
    int const side = TORIRS_POPOUT_NAV_BUTTON;
    int draw_w = src_w;
    int draw_h = src_h;
    int left;
    int top;

    assert(src);
    assert(src_w > 0);
    assert(src_h > 0);
    assert(out);
    memset(out, 0, sizeof(*out) * (size_t)(side * side));

    /* Fit the longer side, keeping the aspect. Nearest-neighbour, because
     * every icon here is pixel art and a filter would blur its outline. */
    if( draw_w > TORIRS_POPOUT_NAV_ICON_MAX || draw_h > TORIRS_POPOUT_NAV_ICON_MAX )
    {
        if( src_w >= src_h )
        {
            draw_w = TORIRS_POPOUT_NAV_ICON_MAX;
            draw_h = src_h * TORIRS_POPOUT_NAV_ICON_MAX / src_w;
        }
        else
        {
            draw_h = TORIRS_POPOUT_NAV_ICON_MAX;
            draw_w = src_w * TORIRS_POPOUT_NAV_ICON_MAX / src_h;
        }
        if( draw_w < 1 )
            draw_w = 1;
        if( draw_h < 1 )
            draw_h = 1;
    }
    left = (side - draw_w) / 2;
    top = (side - draw_h) / 2;
    for( int row = 0; row < draw_h; row++ )
    {
        int const source_row = row * src_h / draw_h;
        for( int column = 0; column < draw_w; column++ )
        {
            int const source_column = column * src_w / draw_w;
            out[((top + row) * side) + left + column] =
                src[(source_row * src_w) + source_column];
        }
    }

    /* The badge sits flush in the bottom-right corner and is opaque wherever
     * it is drawn, so it reads over any icon. */
    {
        int const badge_left = side - TORIRS_CHROME_TORIFACE_W;
        int const badge_top = side - TORIRS_CHROME_TORIFACE_H;
        for( int row = 0; row < TORIRS_CHROME_TORIFACE_H; row++ )
            for( int column = 0; column < TORIRS_CHROME_TORIFACE_W; column++ )
            {
                uint32_t const pixel = ToriRSChromeToriFace_Pixel(column, row);
                if( pixel >> 24 )
                    out[((badge_top + row) * side) + badge_left + column] = pixel;
            }
    }
}
