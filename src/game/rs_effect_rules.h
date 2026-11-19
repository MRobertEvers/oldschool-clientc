#ifndef SRC_GAME_RS_EFFECT_RULES_H
#define SRC_GAME_RS_EFFECT_RULES_H

/*
 * How long the packet-time effect lists keep an entry, and how many they hold.
 *
 * Both clients keep the effects the server announced as the packets said them:
 * projectiles (MAP_PROJANIM / SCRIPTEDPROJ), map graphics (MAP_ANIM) and loc
 * animations (LOC_ANIM). The real client keeps them in App.drive_events
 * (app/app_plugin_drive_events.c, read by plugin/torirs_plugin_drive_ui.c);
 * the headless client in its core (scriptrun/scriptrun_core.c, read by
 * torirsserver/torirs_server_scriptrun.c). Every site that states one of these
 * numbers reads it from here.
 *
 * A list holds every entry still alive; more than its capacity alive at once
 * is an assert on both clients, never a dropped entry (the dropped one is the
 * newest, the one a bot must dodge).
 */

#include "game/rs_server_tick.h" /* APP_SERVER_TICK_LOGIC_CYCLES */

enum
{
    /* Projectiles in flight. It was 64 on the real client: Zebak's lobs (a
     * projectile per acid tile and per jug, flying 3-5 ticks) put more than
     * that in flight at once. A projectile lives until its end cycle on both
     * clients (launch + end_delay), so there is no lifetime constant. */
    RS_EFFECT_PROJECTILE_CAP = 256,

    /* Map graphics alive at once. Bloat's hand volleys below 40% put ~320
     * floor graphics in the 40-tick window (16 shadows + 16 splats every 4
     * ticks); the Path of Crondis's beams draw up to 120 every 5 ticks
     * (docs/rsprox-analysis/toa/crondis.md). */
    RS_EFFECT_MAP_ANIM_CAP = 2048,

    /* Loc animations alive at once. Jagex re-sends the Path of Crondis's 80
     * trap locs' idles every tick before the room starts (~30 a tick after). */
    RS_EFFECT_LOC_ANIM_CAP = 4096,

    /* A map graphic is listed for this many server ticks after the tick that
     * sent it: the longest the floor graphics the runners are built for last
     * (Verzik's pools, 14 ticks). A spotanim's own sequence length would be
     * exact; the consumers judge by id and tile. */
    RS_EFFECT_MAP_ANIM_LIFE_TICKS = 40,

    /* A loc animation is listed for this many server ticks after the tick
     * that sent it, on both clients. (The Great Olm's head is a loc: its
     * facing and its attacks are told only by these.) */
    RS_EFFECT_LOC_ANIM_LIFE_TICKS = 40,

    /* THE TWO CLIENTS DIFFER HERE. The real client retires a loc animation
     * from its list by its own world cycle, which runs on the wall clock and
     * does not move exactly APP_SERVER_TICK_LOGIC_CYCLES a tick, so it keeps
     * one this many ticks and filters the rows it lists by server tick
     * (RS_EFFECT_LOC_ANIM_LIFE_TICKS, DriveUi_LocAnims). The headless client
     * retires by server tick at RS_EFFECT_LOC_ANIM_LIFE_TICKS. What either
     * lists is the same; only how long the real client stores one differs. */
    RS_EFFECT_LOC_ANIM_CLIENT_KEEP_TICKS = RS_EFFECT_LOC_ANIM_LIFE_TICKS + 2,
};

/*
 * THE EFFECT LOG: the real client keeps the effects as the packets said them
 * in one ring of struct RS_Event (game/rs_event_ring.h), written by the packet
 * task, and its effect lists are READ from it: every entry still alive by the
 * lifetimes above, oldest first. Nothing is retired from it; an entry dies by
 * its lifetime and is overwritten in turn. It holds more than every list's
 * cap together, and overwriting an entry that is still alive is an assert:
 * the list that would lose it is fuller than its cap.
 *
 * Its kinds and payloads (absolute tiles, in the frame of the view the packet
 * named; RS_Event.cycle is the owner's world cycle at the packet):
 *   RS_EVENT_PROJECTILE         a=src x  b=src z  c=dst x  d=dst z
 *   RS_EVENT_PROJECTILE_FLIGHT  a=spotanim  b=target (wire encoding)
 *                               c=start_delay | end_delay << 16 (cycles, each
 *                                 16 bits as on the wire)
 *                               d=level (the destination's)
 *     -- always the entry right after its PROJECTILE: a projectile is the
 *        pair (the record carries four numbers; it needs nine).
 *   RS_EVENT_MAP_ANIM           a=x  b=z  c=level  d=spotanim
 */
enum
{
    RS_EFFECT_LOG_CAP = 4096,
};
_Static_assert(
    RS_EFFECT_LOG_CAP >= 2 * RS_EFFECT_PROJECTILE_CAP + RS_EFFECT_MAP_ANIM_CAP,
    "the effect log holds every list at its cap at once");

/* RS_EFFECT_MAP_ANIM_LIFE_TICKS in world cycles: both clients count a map
 * graphic's age in whole server ticks times APP_SERVER_TICK_LOGIC_CYCLES
 * (the headless core's cycle moves exactly that a tick; the real client's
 * App_DriveMapAnimElapsed counts server ticks). */
#define RS_EFFECT_MAP_ANIM_LIFE_CYCLES (RS_EFFECT_MAP_ANIM_LIFE_TICKS * APP_SERVER_TICK_LOGIC_CYCLES)

#endif
