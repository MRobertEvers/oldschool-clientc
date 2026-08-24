/*
 * Deck-local <-> parent-view coordinate transform and view membership
 * (SAILING_PLAN C5.1/C5.2).
 *
 * Split out of wev.c on purpose: wev.c is linked into decode-only tests and
 * carries no dependency beyond libc, while these four functions need the
 * toridraw fixed-point trig tables. Keeping them here means test-wev's link
 * set is unchanged and the painter gate — which already links
 * graphics/shared_tables.c — picks up only this file.
 *
 * The transform is the exact inverse of the one C3's frame_view_push composes,
 * which is what makes a round trip land an actor back where the camera and the
 * server both think it is. Fine units (128/tile) throughout; angles 0..2047.
 */
#include "toridraw_math.h"
#include "wev.h"

#include <assert.h>

void
Wev_DeckFromParent(
    struct WevDeckBox const* box,
    int parent_x,
    int parent_z,
    int* out_deck_x,
    int* out_deck_z)
{
    int dx;
    int dz;
    int inv_angle;
    int cs;
    int sn;

    assert(box);
    assert(out_deck_x);
    assert(out_deck_z);

    dx = parent_x - box->pos_x;
    dz = parent_z - box->pos_z;

    /* R(-angle) as R(2048 - angle): one table, no negative-index special case.
     * Same expression app_wev_bind_view_cameras carries the eye through, so a
     * deck actor and the deck camera cannot disagree about where the deck is. */
    inv_angle = (2048 - (box->angle & 0x7ff)) & 0x7ff;
    cs = ToriDraw_Cos(inv_angle);
    sn = ToriDraw_Sin(inv_angle);

    *out_deck_x = ((dx * cs + dz * sn) >> 16) - box->recenter_x;
    *out_deck_z = ((dz * cs - dx * sn) >> 16) - box->recenter_z;
}

void
Wev_ParentFromDeck(
    struct WevDeckBox const* box,
    int deck_x,
    int deck_z,
    int* out_parent_x,
    int* out_parent_z)
{
    int lx;
    int lz;
    int angle;
    int cs;
    int sn;

    assert(box);
    assert(out_parent_x);
    assert(out_parent_z);

    lx = deck_x + box->recenter_x;
    lz = deck_z + box->recenter_z;

    angle = box->angle & 0x7ff;
    cs = ToriDraw_Cos(angle);
    sn = ToriDraw_Sin(angle);

    *out_parent_x = ((lx * cs + lz * sn) >> 16) + box->pos_x;
    *out_parent_z = ((lz * cs - lx * sn) >> 16) + box->pos_z;
}

bool
Wev_DeckContainsDeckPoint(
    struct WevDeckBox const* box,
    int deck_x,
    int deck_z)
{
    assert(box);
    /* A deck whose size was never published (the window between spawn and the
     * first REBUILD_WORLDENTITY) has an empty rectangle and owns nothing —
     * which is the right answer, not a contract violation. */
    if( box->size_x_tiles <= 0 || box->size_z_tiles <= 0 )
        return false;
    if( deck_x < 0 || deck_z < 0 )
        return false;
    return deck_x < box->size_x_tiles * 128 && deck_z < box->size_z_tiles * 128;
}

bool
Wev_DeckContainsParentPoint(
    struct WevDeckBox const* box,
    int parent_x,
    int parent_z)
{
    int deck_x;
    int deck_z;

    assert(box);
    Wev_DeckFromParent(box, parent_x, parent_z, &deck_x, &deck_z);
    return Wev_DeckContainsDeckPoint(box, deck_x, deck_z);
}
