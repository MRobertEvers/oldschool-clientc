#ifndef DAT1_SPRITE_H
#define DAT1_SPRITE_H

#include "dat1_element.h"

#include <stdbool.h>

struct Dat1BuildCache;
struct DashMap;
struct FileListDat;
struct ToriDraw_Sprite;
struct UIScene;

/* ------------------------------------------------------------------
 * Dat1_Sprite
 *
 * Revconfig "sprite" element.  Declares one archive request for the
 * media jagfile; once the revconfig loader has decoded that archive
 * into dat1_buildcache, converts the filelist frames into
 * ToriDraw_Sprites and acquires them into UIScene.
 * ------------------------------------------------------------------ */
struct Dat1_Sprite
{
    enum Dat1_LoadState state;

    char name[64];
    char table[64];
    char archive[64];
    char container[64];
    char index_filename[64];
    char data_filename[64];
    char format[16];
    int  atlas_index;
    int  atlas_count;
    bool atlas_use_count;

    /* UIScene element id after successful CONVERT; -1 until then. */
    int scene_id;

    struct Dat1_ArchiveRequest waiting[DAT1_SPRITE_MAX_REQUESTS];
    int                        waiting_count;
};

void
dat1_sprite_init(struct Dat1_Sprite* s);

/**
 * Advance the sprite state machine one tick.
 *
 * @param sprite_map  Dat1_SpriteMapEntry hashmap (name -> scene_id).
 *                    Written on successful CONVERT so uitree_build can
 *                    look up the scene_id by name.
 */
void
dat1_sprite_step(
    struct Dat1_Sprite*    s,
    struct Dat1BuildCache* buildcache,
    struct UIScene*        scene,
    struct DashMap*        sprite_map);

/**
 * Decode one sprite from the media jagfile already in buildcache.
 * Used by Dat1_Interface.INSPECT to preload RS graphic sprites.
 * Returns NULL on failure (caller should check).
 */
struct ToriDraw_Sprite*
dat1_sprite_decode_media_ref(
    struct Dat1BuildCache* buildcache,
    const char*            sprite_ref);

#endif /* DAT1_SPRITE_H */
