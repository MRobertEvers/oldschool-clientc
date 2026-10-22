#ifndef WORLD_ENTITY_TRANSPARENCY_H
#define WORLD_ENTITY_TRANSPARENCY_H

/*
 * An actor's TRANSPARENCY: the rev-241 NPC_INFO block (flag 0x8000) that
 * fades a whole body in or out between two client cycles. Ported from the
 * rev-239 deob only, where the npc's copy is Npc field1467 (class657):
 *
 *   set       class657.method14216(now, start, end, from, to, use_start):
 *             start/end are absolute cycles (now + the wire's signed
 *             shorts); the fade runs from `from` when use_start is set or
 *             the start has already passed, else from the alpha it shows now.
 *   read      class657.method14218(cycle): the alpha before the start, `to`
 *             from the end on, linear between -- and the value it returns is
 *             remembered, so the next fade can start from it.
 *   drawn     Npc.getModel (class86.method2930) hands the BODY model that
 *             alpha (Model.method4830); Model.method4901 / method4912 then
 *             draw a face of alpha a < 253 at a + m * (253 - a) >> 8, and a
 *             model of alpha 255 (-1 as the byte) not at all.
 *
 * The Fractured Archive's bosses use it: Ocellus fades out over cycles 30..45
 * of his teleport and in over 0..20 at the middle (RSProx 4301 [986], [990]).
 */

#include <stdint.h>

struct WorldEntityTransparency
{
    /** Set once a block has arrived; until then the body is opaque (the
     *  deob's field1467 is null). */
    int set;
    int start_cycle;
    int end_cycle;
    uint8_t from;
    uint8_t to;
    /** class657 field7006: the alpha last shown. */
    uint8_t current;
};

/** class657.method14216. */
static inline void
WorldEntityTransparency_Set(
    struct WorldEntityTransparency* t,
    int now,
    int start_cycle,
    int end_cycle,
    int from,
    int to,
    int use_start)
{
    t->set = 1;
    t->start_cycle = start_cycle;
    t->end_cycle = end_cycle;
    t->to = (uint8_t)to;
    if( use_start || t->start_cycle < now )
        t->from = (uint8_t)from;
    else
        t->from = t->current;
}

/** class657.method14218: the body's alpha on `cycle`, 0 opaque .. 255 gone. */
static inline int
WorldEntityTransparency_Alpha(
    struct WorldEntityTransparency* t,
    int cycle)
{
    if( !t->set )
        return 0;
    if( cycle < t->start_cycle )
        return t->current;
    if( cycle >= t->end_cycle )
    {
        t->current = t->to;
        return t->current;
    }
    {
        float const k = (float)(cycle - t->start_cycle) / (float)(t->end_cycle - t->start_cycle);

        t->current = (uint8_t)(int)((float)(t->to - t->from) * k + (float)t->from);
    }
    return t->current;
}

/** Model.method4912: face alpha `a` (0..255) under body alpha `m`. */
static inline int
WorldEntityTransparency_Face(
    int a,
    int m)
{
    if( m > 0 && a < 253 )
        return a + (m * (253 - a) >> 8);
    return a;
}

#endif
