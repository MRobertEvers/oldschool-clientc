#ifndef TORIRS_FONT_FROM_RSCACHE_H
#define TORIRS_FONT_FROM_RSCACHE_H

struct RSCache_Dat2DiskArchive;
struct ToriRS_Font;

/**
 * Takes ownership of both archives (frees them).
 * Font glyphs come from the fonts table; pixel data from the sprites table (same id).
 */
struct ToriRS_Font*
ToriRS_FontFromDat2Archives(
    struct RSCache_Dat2DiskArchive* font_archive,
    struct RSCache_Dat2DiskArchive* sprite_archive,
    int font_id);

#endif
