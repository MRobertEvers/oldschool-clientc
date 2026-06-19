#ifndef TORIDRAW_CACHESPRITE_H
#define TORIDRAW_CACHESPRITE_H

#include "toridraw/toridraw_sprite.h"

struct RSCacheDat1A_Pix32;
struct RSCacheDat1A_Pix8Palette;

struct ToriDraw_Sprite*
ToriDraw_SpriteNewFromCachePix32(struct RSCacheDat1A_Pix32* pix32);

struct ToriDraw_Sprite*
ToriDraw_SpriteNewFromCachePix8Palette(struct RSCacheDat1A_Pix8Palette* pix8_palette);

#endif
