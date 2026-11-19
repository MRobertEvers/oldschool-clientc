#include "game/rs_hint_arrow.h"

#include <assert.h>
#include <string.h>

void
RS_HintArrow_Clear(struct RS_HintArrow* arrow)
{
    assert(arrow);
    memset(arrow, 0, sizeof(*arrow));
}

void
RS_HintArrow_Set(
    struct RS_HintArrow* arrow,
    int wire_type,
    int id,
    int z,
    int height)
{
    int type = wire_type == RS_HINT_ARROW_WIRE_CLEAR ? 0 : wire_type;

    assert(arrow);
    /*
     * The five tile forms are one form with five anchors, and the reference
     * folds them here rather than at the draw: 2 is the tile's centre, 3/4 its
     * west/east edge, 5/6 its south/north (Client.ts's own 64/0/128 pairs, and
     * `class268.method6676`'s at rev 239). Left as five types, every place
     * that draws a hint would have to know all five.
     */
    arrow->offset_x = 64;
    arrow->offset_z = 64;
    switch( type )
    {
    case RS_HINT_ARROW_COORD_WEST:
        arrow->offset_x = 0;
        break;
    case RS_HINT_ARROW_COORD_EAST:
        arrow->offset_x = 128;
        break;
    case RS_HINT_ARROW_COORD_SOUTH:
        arrow->offset_z = 0;
        break;
    case RS_HINT_ARROW_COORD_NORTH:
        arrow->offset_z = 128;
        break;
    default:
        break;
    }
    if( type >= RS_HINT_ARROW_COORD && type <= RS_HINT_ARROW_COORD_NORTH )
        type = RS_HINT_ARROW_COORD;

    arrow->type = type;
    arrow->target = id;
    arrow->tile_z = z;
    arrow->height = height;
}

int
RS_HintArrow_CoordFineX(
    struct RS_HintArrow const* arrow,
    int base_x)
{
    assert(arrow);
    return ((arrow->target - base_x) << 7) + arrow->offset_x;
}

int
RS_HintArrow_CoordFineZ(
    struct RS_HintArrow const* arrow,
    int base_z)
{
    assert(arrow);
    return ((arrow->tile_z - base_z) << 7) + arrow->offset_z;
}
