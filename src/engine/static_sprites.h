#ifndef STATIC_SPRITES_H
#define STATIC_SPRITES_H

/*
 * Sprites the client knows by name rather than through interface data — the
 * reference client's "graphic defaults" set (compass, minimap chrome, hitsplats,
 * overhead icons, click cross, scrollbar arrows).
 *
 * These have no owning RevConfig [component:] node, so their scene ids cannot
 * live on a tree node. They are uploaded once into a bridge slot and fetched at
 * emit/draw time through UITREE_HOST_GET_STATIC_SPRITE_SCENE.
 *
 * Names per era: dat2/OSRS resolves an archive in the sprites table by name
 * (see xrsps GraphicsDefaults); dat1 unpacks the media jagfile by file stem
 * (see Client-TS Client.load "Unpacking media").
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

struct StaticSpriteDef
{
    /** Provider name-map key; matches the RevConfig [sprite:<name>] section. */
    char const* name;
    /** Dat2 sprites-table archive name; NULL when the era lacks this sprite. */
    char const* dat2_name;
    /** Dat1 media jagfile stem (no ".dat"); NULL when the era lacks it. */
    char const* dat1_name;
    /** Dat1 pixel format: "pix8" or "pix32". */
    char const* dat1_format;
    /** Dat1 frame count; 0 = single frame. */
    int dat1_atlas_count;
};

struct StaticSpriteDef const*
StaticSprite_Def(enum StaticSpriteSlot slot);

char const*
StaticSprite_SlotName(enum StaticSpriteSlot slot);

#endif
