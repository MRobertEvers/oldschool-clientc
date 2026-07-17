#ifndef TORIRS_SPRITE_FROM_RSCACHE_H
#define TORIRS_SPRITE_FROM_RSCACHE_H

struct RSCache_Dat2DiskArchive;
struct RSCache_Dat2SpritePack;
struct ToriRS_Sprite;

/** Takes ownership of archive (frees it). */
struct ToriRS_Sprite*
ToriRS_SpriteFromDat2Archive(
    struct RSCache_Dat2DiskArchive* archive,
    int sprite_id);

/** Does not free pack. Builds ARGB frames for all sprites in the pack. */
struct ToriRS_Sprite*
ToriRS_SpriteFromDat2Pack(
    struct RSCache_Dat2SpritePack* pack,
    int sprite_id);

#endif
