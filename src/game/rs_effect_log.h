#ifndef SRC_GAME_RS_EFFECT_LOG_H
#define SRC_GAME_RS_EFFECT_LOG_H

/*
 * THE EFFECT LOG, as both clients keep it: the projectiles and map graphics
 * the packets announced, in one ring of struct RS_Event (game/rs_event_ring.h)
 * of RS_EFFECT_LOG_CAP entries, and read back as the effect lists. The kinds,
 * the payloads and the lifetimes are game/rs_effect_rules.h's; this unit is
 * the one copy of the push, the decode and the "still alive" rule.
 *
 * Owners: the real client (App_DriveRing.effects, app/app_plugin_drive_events.c
 * App_DriveProjectileNote / App_DriveMapAnimNote, read by plugin/
 * torirs_plugin_drive_ui.c) and the headless client (struct ScriptrunWorld,
 * scriptrun/scriptrun_world.h, read by torirsserver/torirs_server_scriptrun.c).
 * Each passes its ring and its entries, so a zeroed owner is an empty log.
 *
 * Nothing is retired from the log: an entry dies by its lifetime and is
 * overwritten in turn (RS_EVENT_RING_OVERWRITE). The log is sized past every
 * list's cap together, so an entry still alive is never overwritten; a list
 * holding its cap alive when the next entry comes is a contract violation
 * (an assert in a debug build, where the alive scan is paid for).
 *
 * App-free and cache-free: tiles are absolute, cycles are the owner's world
 * cycle at the packet.
 */

#include "game/rs_effect_rules.h"
#include "game/rs_event_ring.h"

#include <stdint.h>

/* A projectile as its packet said it (RS_EVENT_PROJECTILE + _FLIGHT). */
struct RS_EffectProjectile
{
    int32_t src_x, src_z, dst_x, dst_z, level;
    int32_t spotanim, target, start_delay, end_delay;
    int32_t cycle; /**< the owner's world cycle at the packet */
};

/* A map graphic as its packet said it (RS_EVENT_MAP_ANIM). */
struct RS_EffectMapAnim
{
    int32_t x, z, level, spotanim;
    int32_t cycle; /**< the owner's world cycle at the packet */
};

/**
 * Is entry `ev` still alive at cycle `now`, by game/rs_effect_rules.h's
 * lifetimes plus `slack_cycles`? `next` is the entry after it (a
 * RS_EVENT_PROJECTILE is judged by its RS_EVENT_PROJECTILE_FLIGHT), NULL when
 * there is none. The real client allows two ticks of slack (its world cycle
 * drifts against the server tick); the headless client none (its cycle moves
 * exactly APP_SERVER_TICK_LOGIC_CYCLES a tick).
 */
int
RS_EffectLog_Alive(
    struct RS_Event const* ev,
    struct RS_Event const* next,
    int now,
    int slack_cycles);

/** How many projectiles (`kind` RS_EVENT_PROJECTILE_FLIGHT) or map graphics
 *  (RS_EVENT_MAP_ANIM) are alive at `now`. A scan of the whole log. */
int
RS_EffectLog_Live(
    struct RS_EventRing const* ring,
    struct RS_Event const* entries,
    int kind,
    int now,
    int slack_cycles);

/** Append a projectile: the pair, back to back. `start_delay` and
 *  `end_delay` are 16-bit cycles, as on the wire. */
void
RS_EffectLog_NoteProjectile(
    struct RS_EventRing* ring,
    struct RS_Event* entries,
    int now,
    int slack_cycles,
    struct RS_EffectProjectile const* p);

/** Append a map graphic. */
void
RS_EffectLog_NoteMapAnim(
    struct RS_EventRing* ring,
    struct RS_Event* entries,
    int now,
    int slack_cycles,
    int x,
    int z,
    int level,
    int spotanim);

/** The log's projectiles, decoded into `out`, oldest first: every one it
 *  still holds, alive or not (the reader judges by its own clock). At most
 *  `cap`; returns how many. */
int
RS_EffectLog_Projectiles(
    struct RS_EventRing const* ring,
    struct RS_Event const* entries,
    struct RS_EffectProjectile* out,
    int cap);

/** The log's map graphics, decoded into `out`, oldest first, alive or not. */
int
RS_EffectLog_MapAnims(
    struct RS_EventRing const* ring,
    struct RS_Event const* entries,
    struct RS_EffectMapAnim* out,
    int cap);

#endif /* SRC_GAME_RS_EFFECT_LOG_H */
