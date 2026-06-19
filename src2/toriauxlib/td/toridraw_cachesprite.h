#ifndef TORIDRAW_CACHESPRITE_H
#define TORIDRAW_CACHESPRITE_H

#include "toridraw/toridraw_sprite.h"

struct CacheDatPix32;
struct CacheDatPix8Palette;

struct ToriDraw_Sprite*
ToriDraw_SpriteNewFromCachePix32(struct CacheDatPix32* pix32);

struct ToriDraw_Sprite*
ToriDraw_SpriteNewFromCachePix8Palette(struct CacheDatPix8Palette* pix8_palette);

#endif
