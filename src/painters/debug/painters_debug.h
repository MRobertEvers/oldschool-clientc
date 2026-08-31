#ifndef PAINTERS_DEBUG_H
#define PAINTERS_DEBUG_H

/**
 * The compile-time gate for everything in the painter that exists only to
 * describe a paint.
 *
 * A header, and this small, for one reason: the facility itself lives next
 * door in painters_debug.u.c, included from painters.c below the command
 * encoders and above the three drains that call it. But two sites are compiled
 * BEFORE that point -- the command-index trap in push_command_entity /
 * push_command_terrain -- and one lives in a public header's caller (app.c,
 * which hands the wedge log the eye it is about to paint with). Those need the
 * gate and the two macros here; they do not need, and must not pull in, the
 * wedge log's state, the draw-order dump or the world-entity trace. Those are
 * all next door.
 *
 * GATED AT COMPILE TIME, like the toridraw render/raster facility
 * (3rd/toridraw/toridraw_debug.h) and the occluder census
 * (TORIDRAW_OCC_CENSUS in scene_occluders.h), and for the same reason: these
 * sites sit per-tile and per-element on the paint path -- the wedge log alone
 * has ~25 of them inside the drain loop -- and a runtime branch there taxes
 * the thing being measured. With the gate off none of it is compiled, no
 * symbol it defines is emitted, and no branch survives.
 *
 *   make -C src PAINTERS_DEBUG=1 ...
 *
 * Then, at run time, in a build that has it -- all off by default, so an
 * instrumented binary is still silent until asked:
 *
 *   TORIRS_WEDGELOG=<path>       per-frame draw-order telemetry
 *   TORIRS_WEDGELOG_AT=<n>       capture on the n-th paint call (default 700)
 *   TORIRS_WEDGELOG_FRAMES=<n>   consecutive frames to record (default 1)
 *   TORIRS_PAINTER_DUMP=1        the emitted draw order, per tile
 *   TORIRS_PAINTER_DUMP_TILE=sx,sz   restrict the dump to one tile column
 *   TORIRS_WEV_DEBUG=1           trace every world-entity descent
 *
 * and, from a debugger, the two trap globals: set g_trap_command to break on a
 * command index, or g_trap_x / g_trap_z to break on a tile column.
 *
 * NOTE for anyone bisecting a draw-order bug: the traps used to be live in any
 * non-NDEBUG Apple build. They are behind this gate now, so a plain debug
 * build no longer carries them -- add PAINTERS_DEBUG=1 to get them back.
 */

#if !defined(PAINTERS_DEBUG)
#define PAINTERS_DEBUG 0
#endif

#if PAINTERS_DEBUG

#include "log/torirs_log.h"

#include <stdio.h>

/* Break on a chosen command index / tile column. Set from a debugger; -1 is
 * "never", which is what a run that nobody has attached to leaves them at. */
extern int g_trap_command;
extern int g_trap_x;
extern int g_trap_z;

/**
 * Draw-order telemetry (TORIRS_WEDGELOG=<path>). Records the eye and viewport
 * the caller is about to paint with so the log header can be compared against
 * the instrumented official client's `#path` line. No-op unless the env var is
 * set; never reads or writes painter/render state.
 */
void
painter_wedgelog_set_eye(
    int eye_x,
    int eye_y,
    int eye_z,
    int viewport_w,
    int viewport_h);

#define PAINTER_DBG_WEDGE_SET_EYE(eye_x, eye_y, eye_z, vp_w, vp_h) \
    painter_wedgelog_set_eye((eye_x), (eye_y), (eye_z), (vp_w), (vp_h))

/* __builtin_debugtrap is clang's; the guard is what it always was. */
#if defined(__APPLE__)
#define PAINTER_DBG_TRAP_COMMAND(count) \
    do \
    { \
        if( (int)(count) == g_trap_command ) \
        { \
            TORIRS_LOG("TRAP: %d\n", (int)(count)); \
            __builtin_debugtrap(); \
        } \
    } while( 0 )
#else
#define PAINTER_DBG_TRAP_COMMAND(count) ((void)(count))
#endif

/*
 * The tile trap has never actually trapped: the debugtrap under it is
 * commented out upstream of this move and stays that way, because the column
 * it names is hit thousands of times per frame and a breakpoint there is
 * unusable. What it does is print the tile index, which is the thing that was
 * wanted -- see the DoublyLinkedList note at the call site in
 * painters_distancemetric.u.c for what it was used to find.
 */
#define PAINTER_DBG_TRAP_TILE(tile_sx, tile_sz, tile_idx) \
    do \
    { \
        if( g_trap_x != -1 && g_trap_z != -1 && (tile_sx) == g_trap_x && \
            (tile_sz) == g_trap_z ) \
            printf("tile_idx: %d\n", (int)(tile_idx)); \
    } while( 0 )

#else /* !PAINTERS_DEBUG */

/* Argument-consuming, so a local that only the trap reads is still "used". */
#define PAINTER_DBG_WEDGE_SET_EYE(eye_x, eye_y, eye_z, vp_w, vp_h) \
    ((void)(eye_x), (void)(eye_y), (void)(eye_z), (void)(vp_w), (void)(vp_h))
#define PAINTER_DBG_TRAP_COMMAND(count) ((void)(count))
#define PAINTER_DBG_TRAP_TILE(tile_sx, tile_sz, tile_idx) \
    ((void)(tile_sx), (void)(tile_sz), (void)(tile_idx))

#endif /* PAINTERS_DEBUG */

#endif /* PAINTERS_DEBUG_H */
