#ifndef TORIRS_FONT_FROM_RSCACHE_H
#define TORIRS_FONT_FROM_RSCACHE_H

struct RSCache;
struct RSCache_Dat2DiskArchive;
struct ToriRS_Font;

/**
 * Takes ownership of both archives (frees them).
 * Font glyphs come from the fonts table; pixel data from the sprites table (same id).
 * `profile` selects the metrics blob layout (OldSchool 257-byte vs RS2 263-byte).
 */
struct ToriRS_Font*
ToriRS_FontFromDat2Archives(
    const struct RSCache* profile,
    struct RSCache_Dat2DiskArchive* font_archive,
    struct RSCache_Dat2DiskArchive* sprite_archive,
    int font_id);

struct RSCache_FileListDat;

/**
 * Decode a dat1 title-jagfile font ("<font_name>.dat" + "index.dat", e.g. "b12").
 * Does not free the jagfile.
 *
 * The stem also selects the GLYPH LAYOUT, because in this cache family that is
 * what a stem is for. LostCity's 2004 builds ship "b12" -- 94 glyph records in
 * CHARSET order -- and its 2005 builds ship "b12_full" beside it: 256 records
 * indexed by character code. Two names, two layouts, one archive; the client
 * that asks for "b12_full" is asking for the second, and the `_full` suffix is
 * the cache's own name for it rather than a heuristic invented here. Which
 * stem a world uses is stated in its revconfig (`[font:b12] font_name=`), so
 * the choice is data, not detection.
 */
struct ToriRS_Font*
ToriRS_FontFromDat1Jagfile(
    struct RSCache_FileListDat* title_jagfile,
    char const* font_name);

#endif
