#ifndef TORIRS_SPRITE_FROM_RSCACHE_H
#define TORIRS_SPRITE_FROM_RSCACHE_H

struct RSCache;
struct RSCache_Dat2DiskArchive;
struct RSCache_Dat2SpritePack;
struct ToriRS_Sprite;

/** Takes ownership of archive (frees it). `profile` selects the era-gated decode
 *  rules (the OldSchool 232+ opaque-index override); NULL keeps the older ones. */
struct ToriRS_Sprite*
ToriRS_SpriteFromDat2Archive(
    struct RSCache_Dat2DiskArchive* archive,
    int sprite_id,
    const struct RSCache* profile);

/** Does not free pack. Builds ARGB frames for all sprites in the pack. */
struct ToriRS_Sprite*
ToriRS_SpriteFromDat2Pack(
    struct RSCache_Dat2SpritePack* pack,
    int sprite_id);

struct RSCache_FileListDat;

/**
 * Decode a named dat1 media-jagfile sprite ("<data_filename>" + "<index_filename>").
 * format: "pix8" or "pix32". atlas_count <= 0 loads one frame at atlas_index;
 * atlas_count > 0 loads frames [0..atlas_count) (trailing failed frames truncate).
 * Returns NULL when frame 0 fails to decode.
 */
struct ToriRS_Sprite*
ToriRS_SpriteFromDat1Jagfile(
    struct RSCache_FileListDat* media_jagfile,
    char const* format,
    char const* data_filename,
    char const* index_filename,
    int atlas_index,
    int atlas_count);

/**
 * Decode a dat1 interface-component sprite ref: "name", "name,idx" or "name[idx]".
 * ".dat" is appended when missing; index file is "index.dat". Tries pix32 first,
 * then pix8 (component graphics do not record the format). Single frame.
 */
struct ToriRS_Sprite*
ToriRS_SpriteFromDat1Ref(
    struct RSCache_FileListDat* media_jagfile,
    char const* ref);

/** Extract a subrect of every frame (no-op when width/height <= 0). */
void
ToriRS_SpriteApplyCrop(
    struct ToriRS_Sprite* sprite,
    int crop_x,
    int crop_y,
    int crop_width,
    int crop_height);

/** transform: "flip_h" or "flip_v"; mirrors every frame in place. */
void
ToriRS_SpriteApplyTransform(
    struct ToriRS_Sprite* sprite,
    char const* transform);

#endif
