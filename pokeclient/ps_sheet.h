#ifndef PS_SHEET_H
#define PS_SHEET_H

/*
 * A decoded PokeSprite sheet: the header (src/pokesprite/pokesprite_format.h)
 * plus its pixels as 0xAARRGGBB. For a LAYER sheet those "colours" are data:
 * R material, G band, B depth, A coverage.
 */

#include "pokesprite/pokesprite_format.h"

#include <stdint.h>

/* Facing rows, in the order every sheet stores them. */
enum PS_Facing
{
    PS_FACING_DOWN = 0,
    PS_FACING_LEFT = 1,
    PS_FACING_RIGHT = 2,
    PS_FACING_UP = 3,
};

struct PS_Sheet
{
    struct PokeSpriteSheet head; /* head.png is NULL here: the pixels are below */
    int width;                   /* cell_w * frames */
    int height;                  /* cell_h * facings */
    uint32_t* pixels;            /* width * height, 0xAARRGGBB */
    int total_ms;                /* one pass through the frames */
};

/**
 * Decode .pss bytes. Returns NULL when they are not a valid sheet or the PNG
 * inside does not decode -- bytes off the network, so a runtime answer.
 */
struct PS_Sheet*
PS_SheetDecode(
    const uint8_t* bytes,
    int size);

void
PS_SheetFree(struct PS_Sheet* sheet);

/** The cell for (facing, frame): its top-left pixel; rows are sheet->width apart. */
const uint32_t*
PS_SheetCell(
    const struct PS_Sheet* sheet,
    int facing,
    int frame);

/**
 * The frame showing `elapsed_ms` into the sequence, by the sheet's own
 * per-frame durations. Looping wraps; otherwise it holds on the last frame.
 */
int
PS_SheetFrameAt(
    const struct PS_Sheet* sheet,
    int elapsed_ms,
    int loop);

/** OSRS yaw (0..2047; 0 faces -z/down, 512 -x/left, 1024 up, 1536 right) -> facing row. */
int
PS_FacingFromYaw(int yaw);

#endif
