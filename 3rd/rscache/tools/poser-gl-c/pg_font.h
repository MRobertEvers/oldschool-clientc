#ifndef POSER_GL_C_PG_FONT_H
#define POSER_GL_C_PG_FONT_H

/*
 * The editor's only font: a 5x7 ASCII set on a 6x8 cell, baked into an R8 atlas.
 *
 * poser-gl draws its UI with LEGUI's NanoVG text, backed by a TTF. Nothing here
 * can do that — there is no rasteriser in this repo and the caches' own fonts are
 * unreachable before a cache has been chosen, which is exactly when the start
 * screen needs to draw. A bitmap set authored in the source has no dependencies
 * and no load order.
 */

#include "pg_gl.h"

#define PG_FONT_FIRST_CHAR 32
#define PG_FONT_LAST_CHAR 126
#define PG_FONT_GLYPH_W 5
#define PG_FONT_GLYPH_H 7
#define PG_FONT_CELL_W 6
#define PG_FONT_CELL_H 8
#define PG_FONT_ATLAS_COLS 16
#define PG_FONT_ATLAS_ROWS 6
#define PG_FONT_ATLAS_W (PG_FONT_ATLAS_COLS * PG_FONT_CELL_W)
#define PG_FONT_ATLAS_H (PG_FONT_ATLAS_ROWS * PG_FONT_CELL_H)

/** Upload the atlas. Returns 0 on success. */
int
pg_font_init(void);

void
pg_font_free(void);

PG_GLuint
pg_font_texture(void);

/** Cell origin of `c` in the atlas, in texels. Unmapped characters land on '?'. */
void
pg_font_cell(char c, int* out_x, int* out_y);

/** Pixel width of `text` at scale 1. */
int
pg_font_text_width(const char* text);

/**
 * The raw atlas, `PG_FONT_ATLAS_W * PG_FONT_ATLAS_H` bytes, 0 or 255. Exposed so
 * the specimen dump can write it without a GL context.
 */
const unsigned char*
pg_font_atlas_pixels(void);

#endif
