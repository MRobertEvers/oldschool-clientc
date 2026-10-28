#ifndef PS_COMPOSE_H
#define PS_COMPOSE_H

/*
 * A player from its LAYER sheets: one per worn obj and per uncovered identity
 * kit, all in the same humanoid cell with the same anchor. The port of
 * tools/pokesprite/layers.py composite(), which is the reference: nearest
 * depth wins per pixel, colour from that layer's ramp (built from the
 * possibly-recoloured base), overlap lines where one layer passes in front of
 * another, a tinted 1px outline, eyes on the face.
 */

#include "ps_sheet.h"

#include <stdint.h>

#define PS_COMPOSE_MAX_LAYERS 16
#define PS_COMPOSE_MAX_RECOLOURS 8

struct PS_Recolour
{
    uint16_t from_hsl16; /* a design part's identity colour */
    uint16_t to_hsl16;   /* the player's choice */
};

struct PS_ComposeInput
{
    const struct PS_Sheet* layers[PS_COMPOSE_MAX_LAYERS];
    int layer_count;
    int facing;
    /* Each layer's frame: layers of one player share a sequence, so this is
     * normally one index, clamped per layer to that layer's frame count. */
    int frame;
    struct PS_Recolour recolours[PS_COMPOSE_MAX_RECOLOURS];
    int recolour_count;
};

/**
 * Composite into `out`, cell_w * cell_h of 0xAARRGGBB (alpha 0 or 255).
 * Every layer must share one cell size; that is the humanoid contract.
 */
void
PS_Compose(
    const struct PS_ComposeInput* in,
    uint32_t* out,
    int cell_w,
    int cell_h);

#endif
