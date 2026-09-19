#ifndef TORIDRAW_SPRITE_FROM_TORIRS_H
#define TORIDRAW_SPRITE_FROM_TORIRS_H

struct ToriDraw_Sprite;
struct ToriRS_Sprite;
struct ToriRS_SpriteFrame;

/**
 * Move one frame into a ToriDraw_Sprite, which takes its ARGB buffer. The
 * frame is left with no pixels — it is the source's last use. NULL on
 * failure, in which case the frame keeps them.
 */
struct ToriDraw_Sprite*
ToriDraw_SpriteFromToriRSFrame(struct ToriRS_SpriteFrame* frame);

/**
 * Move all frames of a ToriRS_Sprite into a heap array of ToriDraw_Sprite*.
 * Caller owns the array and each sprite; `src` is left holding no pixels.
 * Returns NULL on failure.
 */
struct ToriDraw_Sprite**
ToriDraw_SpritesFromToriRS(
    struct ToriRS_Sprite* src,
    int* out_count);

#endif
