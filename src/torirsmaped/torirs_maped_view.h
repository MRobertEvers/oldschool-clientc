#ifndef SRC_TORIRSMAPED_TORIRS_MAPED_VIEW_H
#define SRC_TORIRSMAPED_TORIRS_MAPED_VIEW_H

/*
 * Views on the ToriRSMapEd wire (design #windows, #protocol; brief section 3,
 * owner `views`): the camera keys, the one question a view asks about itself,
 * and SPAWN_VIEW.
 *
 * A VIEW is one World rendered through one camera. Its camera is its own
 * business -- local, never on the wire -- until it opts in to "broadcast my
 * camera". Then it publishes the pose under its own state key and any other
 * view in the Client may follow that key:
 *
 *   STATE_CAMERA_BASE + i   the camera of connection i (the server's session
 *                           slot), TORIRSMAPED_CAMERA_VALUES values in the
 *                           layout below; count 0 = that view stopped
 *                           broadcasting (or left: the server publishes the
 *                           count-0 fact itself when the connection goes, and
 *                           forgets the key, so a late joiner never follows a
 *                           ghost).
 *
 *   STATE_VIEW_SELF         a QUESTION, not a field. STATE_SET(VIEW_SELF, 0
 *                           values) is answered to the asker alone with
 *                           FACT_STATE(VIEW_SELF, {connection index, camera
 *                           key, client group}); nothing is stored and
 *                           nothing relayed. It is the one key the server
 *                           reads, because only the server knows which slot
 *                           a connection holds -- and asking it through
 *                           STATE_SET lets every client binding that can
 *                           publish state ask, with no new client call.
 *
 * A connection may publish only ITS OWN camera key; a STATE_SET of another
 * slot's camera key is dropped (logged, the session kept), so a confused
 * client cannot fly someone else's broadcast.
 *
 * SPAWN_VIEW (v2 only):
 *
 *     SPAWN_VIEW   TORIRSMAPED_CAMERA_VALUES * i32   -> FACT_STATUS
 *                  (the new view's camera)              (extra = the pid)
 *
 * The server's HOST starts a viewer process joined to the requester's Client
 * (the daemon binds the launch service, platform/launch_sessions.h `view`).
 * UNSUPPORTED when no binding is set (TORIRSMAPED_CAP_SPAWN_VIEW clear) --
 * the web, where a page cannot start a process and the main tab opens the
 * viewer tab itself, and the embedded server, which has no socket for a
 * second process to reach.
 *
 * BUILD NOTE: torirs_maped_view.c is compiled INTO torirs_maped.c (an
 * #include at its end, like the catalog), so every link line that already
 * names torirs_maped.c carries it. Never link it on its own.
 */

#include "torirs_maped.h"

#include <stddef.h>
#include <stdint.h>

enum
{
    TORIRSMAPED_REQ_SPAWN_VIEW = 49,

    /** STATE_CAMERA_BASE + connection index; one per session slot. */
    TORIRSMAPED_STATE_CAMERA_BASE = 10,
    TORIRSMAPED_STATE_CAMERA_COUNT = TORIRSMAPED_SESSION_MAX,
    /** The question key (see the banner). */
    TORIRSMAPED_STATE_VIEW_SELF = 39,
};

/** A camera's values, in order. x/z are ABSOLUTE world coordinates in fine
 *  units (128 per tile) of the point the camera looks at; level is the plane
 *  that point stands on; yaw and pitch are client angle units (0..2047);
 *  zoom is the distance from that point to the eye, in fine units. An orbit
 *  pose rather than an eye position, so it means the same thing to two
 *  processes whose scenes are based at different tiles. */
enum
{
    TORIRSMAPED_CAMERA_X = 0,
    TORIRSMAPED_CAMERA_Z,
    TORIRSMAPED_CAMERA_LEVEL,
    TORIRSMAPED_CAMERA_YAW,
    TORIRSMAPED_CAMERA_PITCH,
    TORIRSMAPED_CAMERA_ZOOM,
    TORIRSMAPED_CAMERA_VALUES,
};

/** STATE_VIEW_SELF's answer values. */
enum
{
    TORIRSMAPED_VIEW_SELF_INDEX = 0,
    TORIRSMAPED_VIEW_SELF_CAMERA_KEY,
    TORIRSMAPED_VIEW_SELF_GROUP,
    TORIRSMAPED_VIEW_SELF_VALUES,
};

_Static_assert(
    TORIRSMAPED_STATE_CAMERA_BASE + TORIRSMAPED_STATE_CAMERA_COUNT <= TORIRSMAPED_STATE_VIEW_SELF,
    "the camera keys must fit below STATE_VIEW_SELF, inside views' 10..39");
_Static_assert(
    (int)TORIRSMAPED_CAMERA_VALUES <= (int)TORIRSMAPED_STATE_VALUES_MAX,
    "a camera is one state field");

static inline uint32_t
ToriRSMapEd_CameraKey(int connection_index)
{
    return (uint32_t)(TORIRSMAPED_STATE_CAMERA_BASE + connection_index);
}

/** The connection index a camera key belongs to, or -1 when `key` is not
 *  one. */
static inline int
ToriRSMapEd_CameraKeyIndex(uint32_t key)
{
    if( key < TORIRSMAPED_STATE_CAMERA_BASE
        || key >= TORIRSMAPED_STATE_CAMERA_BASE + TORIRSMAPED_STATE_CAMERA_COUNT )
        return -1;
    return (int)(key - TORIRSMAPED_STATE_CAMERA_BASE);
}

/**
 * Bind (or, with NULL, clear) SPAWN_VIEW. Sets TORIRSMAPED_CAP_SPAWN_VIEW so
 * the next FACT_HELLO reports it; the binding must outlive the server.
 */
void
ToriRSMapEd_SetViewSpawner(
    struct ToriRSMapEd* maped,
    const struct ToriRSMapEdViewSpawner* spawner);

/**
 * The camera's TEXT form, as `--camera`, `[editor:boot] camera=` and the
 * launch service's `camera=` carry it: "x,z,level,yaw,pitch,zoom" with x and
 * z in absolute TILES (a fraction allowed: "3222.5"), everything else whole.
 *
 * Parse returns 1 and fills `out` (TORIRSMAPED_CAMERA_VALUES, fine units),
 * 0 when the text is not six numbers in range (x/z 0..16383 tiles, level
 * 0..3, yaw/pitch 0..2047, zoom 0..65535). Text comes from a command line or
 * a remote peer, so a bad one is an answer, not an assert.
 */
int
ToriRSMapEd_CameraParse(
    const char* text,
    int32_t* out);

/** Writes the text form; the result parses back to exactly `camera`.
 *  Returns the length written (snprintf's). */
int
ToriRSMapEd_CameraFormat(
    const int32_t* camera,
    char* out,
    size_t out_size);

#endif
