#ifndef TORIDRAW_FONT_FROM_TORIRS_H
#define TORIDRAW_FONT_FROM_TORIRS_H

struct ToriDraw_Font;
struct ToriRS_Font;

/** Deep-copy ToriRS_Font glyph data into a heap ToriDraw_Font. Caller frees with ToriDraw_FontFree. */
struct ToriDraw_Font*
ToriDraw_FontFromToriRS(struct ToriRS_Font const* src);

#endif
