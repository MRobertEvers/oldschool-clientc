#ifndef STATIC_SPRITES_H
#define STATIC_SPRITES_H

/*
 * Sprites the client draws itself rather than through interface data — the
 * reference client's "graphic defaults" set (compass, minimap chrome,
 * hitsplats, overhead icons, click cross, scrollbar arrows).
 *
 * These have no owning RevConfig [component:] node, so their scene ids cannot
 * live on a tree node. They are uploaded once into a bridge slot and fetched at
 * emit/draw time through UITREE_HOST_GET_STATIC_SPRITE_SCENE.
 *
 * What lives here is only the slot -> NAME binding: which picture the compass
 * overlay wants, spelled the way the RevConfig profile spells it. Where that
 * picture is — dat2 sprites-table archive, dat1 media-jagfile stem, pixel
 * format, frame count — is the profile's business and is stated in a
 * `[sprite:<name>]` section, exactly like every other UI sprite. A slot with no
 * matching section stays unbound, which is the correct answer for an era that
 * has no such pack (dat1 ships no mod_icons; rev 239 ships no mapfunction).
 */
enum StaticSpriteSlot
{
    STATIC_SPRITE_COMPASS = 0,
    STATIC_SPRITE_MAPEDGE,
    STATIC_SPRITE_MAPSCENE,
    STATIC_SPRITE_MAPFUNCTION,
    /** Dat1 packs every overhead icon in one archive; dat2 splits them. */
    STATIC_SPRITE_HEADICONS,
    STATIC_SPRITE_HEADICONS_PK,
    STATIC_SPRITE_HEADICONS_PRAYER,
    STATIC_SPRITE_HEADICONS_HINT,
    STATIC_SPRITE_HITMARKS,
    STATIC_SPRITE_MAPMARKER,
    STATIC_SPRITE_CROSS,
    STATIC_SPRITE_MAPDOTS,
    STATIC_SPRITE_SCROLLBAR,
    STATIC_SPRITE_MOD_ICONS,
    STATIC_SPRITE_COUNT
};

/** The `[sprite:<name>]` section this slot is bound to. Never NULL. */
char const*
StaticSprite_SlotName(enum StaticSpriteSlot slot);

#endif
