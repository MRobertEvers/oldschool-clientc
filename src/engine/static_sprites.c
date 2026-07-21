#include "engine/static_sprites.h"

#include "ui/uitree_host.h"

#include <assert.h>
#include <stddef.h>

/* ui/ mirrors the slots it asks for (see enum UITreeStaticSpriteSlot). */
_Static_assert(
    (int)STATIC_SPRITE_COMPASS == (int)UITREE_STATIC_SPRITE_COMPASS,
    "ui compass slot mirror out of sync");
_Static_assert(
    (int)STATIC_SPRITE_CROSS == (int)UITREE_STATIC_SPRITE_CROSS,
    "ui cross slot mirror out of sync");

/* Dat1 formats/frame counts mirror Client-TS Client.load (Pix8/Pix32.depack
 * calls in "Unpacking media"); dat2 names mirror xrsps GraphicsDefaults. */
static struct StaticSpriteDef const k_static_sprites[STATIC_SPRITE_COUNT] = {
    [STATIC_SPRITE_COMPASS] = { "compass", "compass", "compass", "pix32", 0 },
    [STATIC_SPRITE_MAPEDGE] = { "mapedge", "mapedge", "mapedge", "pix32", 0 },
    [STATIC_SPRITE_MAPSCENE] = { "mapscene", "mapscene", "mapscene", "pix8", 50 },
    [STATIC_SPRITE_MAPFUNCTION] = { "mapfunction", "mapfunction", "mapfunction", "pix32", 50 },
    [STATIC_SPRITE_HEADICONS] = { "headicons", NULL, "headicons", "pix32", 20 },
    [STATIC_SPRITE_HEADICONS_PK] = { "headicons_pk", "headicons_pk", NULL, NULL, 0 },
    [STATIC_SPRITE_HEADICONS_PRAYER] = { "headicons_prayer", "headicons_prayer", NULL, NULL, 0 },
    [STATIC_SPRITE_HEADICONS_HINT] = { "headicons_hint", "headicons_hint", NULL, NULL, 0 },
    [STATIC_SPRITE_HITMARKS] = { "hitmarks", "hitmark", "hitmarks", "pix32", 20 },
    [STATIC_SPRITE_MAPMARKER] = { "mapmarker", "mapmarker", "mapmarker", "pix32", 2 },
    [STATIC_SPRITE_CROSS] = { "cross", "cross", "cross", "pix32", 8 },
    [STATIC_SPRITE_MAPDOTS] = { "mapdots", "mapdots", "mapdots", "pix32", 4 },
    [STATIC_SPRITE_SCROLLBAR] = { "scrollbar", "scrollbar", "scrollbar", "pix8", 2 },
    [STATIC_SPRITE_MOD_ICONS] = { "mod_icons", "mod_icons", NULL, NULL, 0 },
};

struct StaticSpriteDef const*
StaticSprite_Def(enum StaticSpriteSlot slot)
{
    assert(slot >= 0 && slot < STATIC_SPRITE_COUNT);
    return &k_static_sprites[slot];
}

char const*
StaticSprite_SlotName(enum StaticSpriteSlot slot)
{
    return StaticSprite_Def(slot)->name;
}
