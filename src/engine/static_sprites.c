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

/*
 * Slot -> RevConfig section name. One string per slot and nothing else: the
 * archive, jagfile stem, pixel format and frame count that used to sit beside
 * each of these were a second, silent copy of the `[sprite:<name>]` vocabulary,
 * and the two had already drifted (the profiles declared scrollbar0/scrollbar1
 * while this table declared one two-frame `scrollbar`).
 */
static char const* const k_static_sprite_names[STATIC_SPRITE_COUNT] = {
    [STATIC_SPRITE_COMPASS] = "compass",
    [STATIC_SPRITE_MAPEDGE] = "mapedge",
    [STATIC_SPRITE_MAPSCENE] = "mapscene",
    [STATIC_SPRITE_MAPFUNCTION] = "mapfunction",
    [STATIC_SPRITE_HEADICONS] = "headicons",
    [STATIC_SPRITE_HEADICONS_PK] = "headicons_pk",
    [STATIC_SPRITE_HEADICONS_PRAYER] = "headicons_prayer",
    [STATIC_SPRITE_HEADICONS_HINT] = "headicons_hint",
    [STATIC_SPRITE_HITMARKS] = "hitmarks",
    [STATIC_SPRITE_MAPMARKER] = "mapmarker",
    [STATIC_SPRITE_CROSS] = "cross",
    [STATIC_SPRITE_MAPDOTS] = "mapdots",
    [STATIC_SPRITE_SCROLLBAR] = "scrollbar",
    [STATIC_SPRITE_MOD_ICONS] = "mod_icons",
};

char const*
StaticSprite_SlotName(enum StaticSpriteSlot slot)
{
    assert(slot >= 0 && slot < STATIC_SPRITE_COUNT);
    assert(k_static_sprite_names[slot]);
    return k_static_sprite_names[slot];
}
