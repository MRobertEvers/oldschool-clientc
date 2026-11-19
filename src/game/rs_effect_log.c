#include "game/rs_effect_log.h"

#include <assert.h>
#include <string.h>

int
RS_EffectLog_Alive(
    struct RS_Event const* ev,
    struct RS_Event const* next,
    int now,
    int slack_cycles)
{
    assert(ev);
    assert(slack_cycles >= 0);
    if( ev->kind == RS_EVENT_MAP_ANIM )
        return now - ev->cycle <= RS_EFFECT_MAP_ANIM_LIFE_CYCLES + slack_cycles;
    if( ev->kind == RS_EVENT_PROJECTILE )
        return next && next->kind == RS_EVENT_PROJECTILE_FLIGHT &&
               RS_EffectLog_Alive(next, NULL, now, slack_cycles);
    assert(ev->kind == RS_EVENT_PROJECTILE_FLIGHT);
    /* A projectile lives until its end cycle (launch + end_delay). */
    return now - ev->cycle <= (int)((uint32_t)ev->c >> 16) + slack_cycles;
}

int
RS_EffectLog_Live(
    struct RS_EventRing const* ring,
    struct RS_Event const* entries,
    int kind,
    int now,
    int slack_cycles)
{
    int n = 0;

    assert(ring);
    assert(entries);
    assert(kind == RS_EVENT_PROJECTILE_FLIGHT || kind == RS_EVENT_MAP_ANIM);
    if( ring->count == 0 )
        return 0;
    for( uint32_t s = ring->oldest_serial; s <= ring->newest_serial; s++ )
    {
        struct RS_Event const* e = RS_EventRing_At(ring, entries, RS_EFFECT_LOG_CAP, s);
        if( e->kind == kind && RS_EffectLog_Alive(e, NULL, now, slack_cycles) )
            n++;
    }
    return n;
}

/* Append one entry. The entry a full log overwrites is dead: one still alive
 * means a list is over its cap (rs_effect_rules.h). */
static void
effect_log_push(
    struct RS_EventRing* ring,
    struct RS_Event* entries,
    int now,
    int slack_cycles,
    int kind,
    int32_t a,
    int32_t b,
    int32_t c,
    int32_t d)
{
    struct RS_Event ev;

#ifndef NDEBUG
    if( ring->count == RS_EFFECT_LOG_CAP )
    {
        uint32_t const oldest = ring->oldest_serial;
        struct RS_Event const* old = RS_EventRing_At(ring, entries, RS_EFFECT_LOG_CAP, oldest);
        struct RS_Event const* after =
            RS_EventRing_At(ring, entries, RS_EFFECT_LOG_CAP, oldest + 1);
        assert(!RS_EffectLog_Alive(old, after, now, slack_cycles));
    }
#else
    (void)slack_cycles;
#endif
    memset(&ev, 0, sizeof(ev));
    ev.kind = kind;
    ev.cycle = now;
    ev.a = a;
    ev.b = b;
    ev.c = c;
    ev.d = d;
    (void)RS_EventRing_Push(ring, entries, RS_EFFECT_LOG_CAP, RS_EVENT_RING_OVERWRITE, &ev);
}

void
RS_EffectLog_NoteProjectile(
    struct RS_EventRing* ring,
    struct RS_Event* entries,
    int now,
    int slack_cycles,
    struct RS_EffectProjectile const* p)
{
    assert(ring);
    assert(entries);
    assert(p);
    assert(p->start_delay >= 0 && p->start_delay <= 0xffff);
    assert(p->end_delay >= 0 && p->end_delay <= 0xffff);
    /* Every projectile a client would still draw is listed; more than the
     * cap in flight at once is a contract violation, not a drop. */
    assert(
        RS_EffectLog_Live(ring, entries, RS_EVENT_PROJECTILE_FLIGHT, now, slack_cycles) <
        RS_EFFECT_PROJECTILE_CAP);
    effect_log_push(ring, entries, now, slack_cycles, RS_EVENT_PROJECTILE, p->src_x, p->src_z,
        p->dst_x, p->dst_z);
    effect_log_push(
        ring,
        entries,
        now,
        slack_cycles,
        RS_EVENT_PROJECTILE_FLIGHT,
        p->spotanim,
        p->target,
        (int32_t)((uint32_t)p->start_delay | ((uint32_t)p->end_delay << 16)),
        p->level);
}

void
RS_EffectLog_NoteMapAnim(
    struct RS_EventRing* ring,
    struct RS_Event* entries,
    int now,
    int slack_cycles,
    int x,
    int z,
    int level,
    int spotanim)
{
    assert(ring);
    assert(entries);
    assert(RS_EffectLog_Live(ring, entries, RS_EVENT_MAP_ANIM, now, slack_cycles) <
           RS_EFFECT_MAP_ANIM_CAP);
    effect_log_push(ring, entries, now, slack_cycles, RS_EVENT_MAP_ANIM, x, z, level, spotanim);
}

int
RS_EffectLog_Projectiles(
    struct RS_EventRing const* ring,
    struct RS_Event const* entries,
    struct RS_EffectProjectile* out,
    int cap)
{
    int n = 0;

    assert(ring);
    assert(entries);
    assert(out);
    assert(cap >= 0);
    if( ring->count == 0 )
        return 0;
    for( uint32_t s = ring->oldest_serial; s < ring->newest_serial && n < cap; s++ )
    {
        struct RS_Event const* p = RS_EventRing_At(ring, entries, RS_EFFECT_LOG_CAP, s);
        struct RS_Event const* f;

        if( p->kind != RS_EVENT_PROJECTILE )
            continue;
        f = RS_EventRing_At(ring, entries, RS_EFFECT_LOG_CAP, s + 1);
        assert(f->kind == RS_EVENT_PROJECTILE_FLIGHT);
        out[n].src_x = p->a;
        out[n].src_z = p->b;
        out[n].dst_x = p->c;
        out[n].dst_z = p->d;
        out[n].spotanim = f->a;
        out[n].target = f->b;
        out[n].start_delay = (int32_t)((uint32_t)f->c & 0xffffu);
        out[n].end_delay = (int32_t)((uint32_t)f->c >> 16);
        out[n].level = f->d;
        out[n].cycle = p->cycle;
        n++;
    }
    return n;
}

int
RS_EffectLog_MapAnims(
    struct RS_EventRing const* ring,
    struct RS_Event const* entries,
    struct RS_EffectMapAnim* out,
    int cap)
{
    int n = 0;

    assert(ring);
    assert(entries);
    assert(out);
    assert(cap >= 0);
    if( ring->count == 0 )
        return 0;
    for( uint32_t s = ring->oldest_serial; s <= ring->newest_serial && n < cap; s++ )
    {
        struct RS_Event const* e = RS_EventRing_At(ring, entries, RS_EFFECT_LOG_CAP, s);

        if( e->kind != RS_EVENT_MAP_ANIM )
            continue;
        out[n].x = e->a;
        out[n].z = e->b;
        out[n].level = e->c;
        out[n].spotanim = e->d;
        out[n].cycle = e->cycle;
        n++;
    }
    return n;
}
