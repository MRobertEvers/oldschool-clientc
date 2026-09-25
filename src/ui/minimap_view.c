#include "ui/minimap_view.h"

#include <assert.h>
#include <math.h>
#include <string.h>

void
MinimapView_Reset(struct MinimapView* view)
{
    assert(view);
    memset(view, 0, sizeof(*view));
    view->flag_tile_x = -1;
    view->flag_tile_z = -1;
}

void
MinimapView_Invalidate(struct MinimapView* view)
{
    assert(view);
    /* Only the box goes. A flag is the player's destination and outlives any
     * frame that happened not to draw the map -- a tab switch would otherwise
     * cancel a walk. */
    view->valid = false;
}

void
MinimapView_Publish(struct MinimapView* view, int x, int y, int w, int h, int rotation_r2pi2048)
{
    assert(view);
    view->x = x;
    view->y = y;
    view->w = w;
    view->h = h;
    view->rotation_r2pi2048 = rotation_r2pi2048;
    view->valid = true;
}

int
MinimapView_Yaw(struct MinimapView const* view)
{
    assert(view);
    return view->rotation_r2pi2048 & 0x7ff;
}

void
MinimapView_SetFlag(struct MinimapView* view, int scene_tile_x, int scene_tile_z)
{
    assert(view);
    view->flag_tile_x = scene_tile_x;
    view->flag_tile_z = scene_tile_z;
}

bool
MinimapView_RebaseFlag(struct MinimapView* view, int base_dx, int base_dz, int scene_size_tiles)
{
    assert(view);
    assert(scene_size_tiles > 0);

    if( !MinimapView_HasFlag(view) )
        return false;
    view->flag_tile_x -= base_dx;
    view->flag_tile_z -= base_dz;
    /* A flag the reload left behind is dropped rather than clamped. Clamped,
     * it would mark the edge of the new scene -- a destination the player
     * never chose, which they are still walking towards. */
    if( view->flag_tile_x < 0 || view->flag_tile_z < 0 || view->flag_tile_x >= scene_size_tiles ||
        view->flag_tile_z >= scene_size_tiles )
    {
        MinimapView_ClearFlag(view);
        return false;
    }
    return true;
}

void
MinimapView_ClearFlag(struct MinimapView* view)
{
    assert(view);
    view->flag_tile_x = -1;
    view->flag_tile_z = -1;
}

bool
MinimapView_ClickToFineOffset(
    struct MinimapView const* view,
    struct MinimapRotation const* rotation,
    int mouse_x,
    int mouse_y,
    int* out_centre_x,
    int* out_centre_y,
    int* out_fine_x,
    int* out_fine_z)
{
    int centre_x;
    int centre_y;

    assert(view);
    assert(rotation);
    assert(out_centre_x);
    assert(out_centre_y);
    assert(out_fine_x);
    assert(out_fine_z);

    if( !view->valid )
        return false;
    if( mouse_x < view->x || mouse_x >= view->x + view->w || mouse_y < view->y ||
        mouse_y >= view->y + view->h )
        return false;

    centre_x = mouse_x - (view->x + view->w / 2);
    centre_y = mouse_y - (view->y + view->h / 2);

    /* Sixteen bits of fixed point out, five bits of fine-units-per-pixel back
     * in, so the shift is eleven rather than sixteen. The scale is folded into
     * the same shift as the rotation, which is why no factor of 32 appears
     * anywhere in this function. */
    *out_fine_x = (centre_y * rotation->sin + centre_x * rotation->cos) >> 11;
    *out_fine_z = (centre_y * rotation->cos - centre_x * rotation->sin) >> 11;
    *out_centre_x = centre_x;
    *out_centre_y = centre_y;
    return true;
}

bool
MinimapView_PlaceDot(
    struct MinimapRotation const* rotation,
    int fine_x,
    int fine_z,
    int sprite_w,
    int sprite_h,
    int* out_dx,
    int* out_dy)
{
    int pixel_x;
    int pixel_z;
    int x;
    int y;

    assert(rotation);
    assert(out_dx);
    assert(out_dy);

    pixel_x = fine_x / MINIMAP_FINE_PER_PIXEL;
    pixel_z = fine_z / MINIMAP_FINE_PER_PIXEL;
    /* Squared, so the radius is a circle. Compared per axis instead, the map's
     * corners would reach a third further than its sides and the dots there
     * would sit outside the round frame the map is drawn in. */
    if( pixel_x * pixel_x + pixel_z * pixel_z >
        MINIMAP_DOT_RADIUS_PIXELS * MINIMAP_DOT_RADIUS_PIXELS )
        return false;

    x = (pixel_z * rotation->sin + pixel_x * rotation->cos) >> 16;
    y = (pixel_z * rotation->cos - pixel_x * rotation->sin) >> 16;

    /* Half the sprite back, so the icon is centred on the thing rather than
     * hanging down-right of it -- and the vertical axis inverts, because the
     * world's z grows north and the screen's y grows down. */
    *out_dx = x - sprite_w / 2;
    *out_dy = -y - sprite_h / 2;
    return true;
}

enum MinimapHintBand
MinimapView_HintBand(int fine_x, int fine_z)
{
    int const pixel_x = fine_x / MINIMAP_FINE_PER_PIXEL;
    int const pixel_z = fine_z / MINIMAP_FINE_PER_PIXEL;
    int const distance = pixel_x * pixel_x + pixel_z * pixel_z;

    /* 65 and 300 pixels, squared, exactly as the reference spells them. */
    if( distance <= 4225 )
        return MINIMAP_HINT_ON_MAP;
    if( distance >= 90000 )
        return MINIMAP_HINT_UNSHOWN;
    return MINIMAP_HINT_ON_RIM;
}

void
MinimapView_PlaceRimMarker(
    struct MinimapRotation const* rotation,
    int fine_x,
    int fine_z,
    int sprite_w,
    int sprite_h,
    int radius_x,
    int radius_z,
    int* out_dx,
    int* out_dy,
    int* out_rotate_r2pi2048)
{
    int pixel_x;
    int pixel_z;
    int x;
    int y;
    double bearing;
    int turn;

    assert(rotation);
    assert(out_dx);
    assert(out_dy);
    assert(out_rotate_r2pi2048);

    pixel_x = fine_x / MINIMAP_FINE_PER_PIXEL;
    pixel_z = fine_z / MINIMAP_FINE_PER_PIXEL;
    /* The same turn into map space PlaceDot makes, and for the same reason:
     * the rim the arrow sits on is the MAP's, so the direction has to be the
     * one the map is drawn in and not the one the world is laid out in. */
    x = (pixel_z * rotation->sin + pixel_x * rotation->cos) >> 16;
    y = (pixel_z * rotation->cos - pixel_x * rotation->sin) >> 16;

    /* atan2(east, north) is the bearing clockwise from up, which is both where
     * on the rim the arrow goes and how far the art has to turn -- one angle,
     * used twice, which is why the arrow always points along the radius it
     * sits on. Double precision for one marker a frame; the reference uses the
     * same library call rather than its own tables. */
    bearing = atan2((double)x, (double)y);

    *out_dx = (int)(sin(bearing) * radius_x) - sprite_w / 2;
    /* Screen y grows down where north grows up, hence the negation. The extra
     * ten pixels are the reference's own raise (deob method2022 subtracts them
     * after centring; Client.ts folds them into the same literal), which lifts
     * the arrow clear of the map's bottom chrome. */
    *out_dy = -(int)(cos(bearing) * radius_z) - 10 - sprite_h / 2;

    /* Radians to the blit's 2048 units, wrapped the long way so a westward
     * bearing (negative) does not come out negative. */
    turn = (int)(bearing * (2048.0 / (2.0 * 3.14159265358979323846)));
    *out_rotate_r2pi2048 = ((turn % 2048) + 2048) % 2048;
}

void
MinimapDots_Reset(struct MinimapDots* dots)
{
    assert(dots);
    /* The count alone: the array is large and rewriting all of it every frame
     * would cost more than every dot in it is worth. Push blanks each slot as
     * it is handed out instead. */
    dots->count = 0;
}

struct UITreeMinimapDot*
MinimapDots_Push(struct MinimapDots* dots)
{
    struct UITreeMinimapDot* dot;

    assert(dots);
    if( dots->count >= MINIMAP_DOTS_MAX )
        return NULL;
    dot = &dots->dots[dots->count++];
    memset(dot, 0, sizeof(*dot));
    return dot;
}
