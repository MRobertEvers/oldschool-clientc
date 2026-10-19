#ifndef WORLD_ENTITY_TINT_H
#define WORLD_ENTITY_TINT_H

/*
 * An actor's TINT: revision 239's PLAYER_INFO tinting block, as the official
 * client draws it. Ported from the rev-239 deob only:
 *
 *   read      class109.method3804, flag 0x200: start = cycle + g2Alt1,
 *             end = cycle + g2, then four SIGNED bytes hue / saturation /
 *             lightness / weight -> Actor.method3182 (class95), which stores
 *             the cycles (Actor field1524 / field1469) and the four bytes
 *             (class149 field2233 / field2234 / field2232 / field2236).
 *   active    Statics.method3506: weight > 0 AS A BYTE (class149.method5142,
 *             so 128..255 is off) and start <= loopCycle < end.
 *   which     class60 (Player.getModel): Model.method4855(tint, faces) with
 *             `faces` the body's face count taken BEFORE an attached graphic
 *             is merged on, so a spotanim riding the player is never tinted.
 *   how       class144.method4849: an UNTEXTURED face below that count draws
 *             through the tinting raster entry, which recolours each of its
 *             lit HSL colours with class158.method5310 (below) -- the three
 *             corners of a gouraud face, the one colour of a flat face before
 *             its palette lookup. Textured faces are never tinted.
 *
 * method5310, verbatim: each channel moves toward its target by
 * weight/128, a target of -1 leaves the channel alone, and the three are
 * packed back without clamping (& 0xFFFF), which with a weight under 128 and
 * targets in range cannot leave the channel's range.
 */

#include <stdint.h>

struct WorldEntityTint
{
    /** Absolute client cycles (world->cycle at arrival + the wire's deltas). */
    int start_cycle;
    int end_cycle;
    int8_t hue;
    int8_t saturation;
    int8_t lightness;
    /** Out of 128; <= 0 as a signed byte is "no tint". */
    int8_t weight;
};

/** Statics.method3506: the tint draws on this cycle. */
static inline int
WorldEntityTint_Active(
    struct WorldEntityTint const* tint,
    int cycle)
{
    return tint->weight > 0 && cycle >= tint->start_cycle && cycle < tint->end_cycle;
}

/** class158.method5310: one lit HSL16 colour, tinted. */
static inline int
WorldEntityTint_Hsl16(
    int hsl,
    int hue,
    int saturation,
    int lightness,
    int weight)
{
    int h = hsl >> 10 & 0x3f;
    int s = hsl >> 7 & 0x7;
    int l = hsl & 0x7f;
    int w = weight & 0xff;

    if( hue != -1 )
        h += (hue - h) * w >> 7;
    if( saturation != -1 )
        s += (saturation - s) * w >> 7;
    if( lightness != -1 )
        l += (lightness - l) * w >> 7;
    return (h << 10 | s << 7 | l) & 0xffff;
}

#endif
