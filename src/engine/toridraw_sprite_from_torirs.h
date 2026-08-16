#ifndef TORIDRAW_SPRITE_FROM_TORIRS_H
#define TORIDRAW_SPRITE_FROM_TORIRS_H

struct ToriDraw_Sprite;
struct ToriRS_Sprite;
struct ToriRS_SpriteFrame;

/** Deep-copy one frame into a ToriDraw_Sprite (owns ARGB). NULL on failure. */
struct ToriDraw_Sprite*
ToriDraw_SpriteFromToriRSFrame(struct ToriRS_SpriteFrame const* frame);

/**
 * Convert all frames of a ToriRS_Sprite into a heap array of ToriDraw_Sprite*.
 * Caller owns the array and each sprite. Returns NULL on failure.
 */
struct ToriDraw_Sprite**
ToriDraw_SpritesFromToriRS(
    struct ToriRS_Sprite const* src,
    int* out_count);

#endif
