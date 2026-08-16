#ifndef TORIDRAW_RASTER_SCANLINE_SELECT_H
#define TORIDRAW_RASTER_SCANLINE_SELECT_H

/**
 * Runtime selector for the `scanline` raster family.
 *
 * Zero (the default) keeps every ToriDraw_Triangle* dispatcher on the
 * `branching` / `sort` kernels. Set it with ToriDraw_RasterSetScanline(), or
 * export TORIDRAW_RASTER_SCANLINE=1 before ToriDraw_Init().
 */
extern int g_toridraw_raster_scanline;

#define TORIDRAW_SCANLINE_SELECTED() (g_toridraw_raster_scanline != 0)

#endif
