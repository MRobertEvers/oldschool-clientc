#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_INTERFACE_STATE_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_INTERFACE_STATE_H

/*
 * Authoritative server-side interface state for one player.
 *
 * Revision 239 can replace the client's complete interface tree with one
 * IF_RESYNC_V2 packet.  Producing that packet from "whatever we happened to
 * send recently" is not enough: IF_OPENSUB replaces an occupied target,
 * IF_CLOSESUB recursively removes children mounted into the closed group, and
 * IF_SETEVENTS replaces an inclusive sub-id interval (splitting older ranges
 * on either side).  This table applies those same mutations at send time, so a
 * resync is a snapshot of server authority rather than a replay heuristic.
 */

#include <stddef.h>
#include <stdint.h>

struct RSAreaBuf;

enum
{
    /* The current content pack has 180 IF_SETEVENTS call sites and fewer than
     * 50 simultaneous mounts.  These limits leave ample room for interval
     * splits while keeping the per-player table fixed and allocation-free. */
    TORIRSSERVER_IF_MOUNT_MAX = 256,
    TORIRSSERVER_IF_EVENT_RANGE_MAX = 1024,
    TORIRSSERVER_IF_RESYNC_MAX_PAYLOAD =
        4 + TORIRSSERVER_IF_MOUNT_MAX * 7 + TORIRSSERVER_IF_EVENT_RANGE_MAX * 16,
};

struct ToriRSServerIfMount
{
    int32_t target_uid;
    int32_t interface_id;
    int32_t type;
};

struct ToriRSServerIfEventRange
{
    int32_t component_uid;
    int32_t start;
    int32_t end;
    uint32_t events1;
    uint32_t events2;
};

struct ToriRSServerInterfaceState
{
    int32_t root_interface;
    int mount_count;
    int event_count;
    struct ToriRSServerIfMount mounts[TORIRSSERVER_IF_MOUNT_MAX];
    struct ToriRSServerIfEventRange events[TORIRSSERVER_IF_EVENT_RANGE_MAX];
};

/** Empty state has no root (`-1`, encoded as 65535 by IF_RESYNC_V2). */
void
ToriRSServer_IfStateInit(struct ToriRSServerInterfaceState* state);

/** Opening a new top-level interface begins a fresh mount tree. Event ranges
 * persist so a subsequent IF_RESYNC_V2 re-arms the replacement gameframe;
 * closing a mounted group removes that group's ranges explicitly. */
void
ToriRSServer_IfStateOpenTop(
    struct ToriRSServerInterfaceState* state,
    int interface_id);

/** Mount/replace one sub-interface. Returns 0 only when the table is full or
 * the arguments cannot be represented by the revision-239 packet. */
int
ToriRSServer_IfStateOpenSub(
    struct ToriRSServerInterfaceState* state,
    int target_uid,
    int interface_id,
    int type);

/** Close the sub mounted at target_uid and recursively retire child mounts and
 * event ranges belonging to every group removed with it. */
int
ToriRSServer_IfStateCloseSub(
    struct ToriRSServerInterfaceState* state,
    int target_uid);

/** Apply IF_MOVESUB's destination replacement and source move. */
int
ToriRSServer_IfStateMoveSub(
    struct ToriRSServerInterfaceState* state,
    int source_uid,
    int dest_uid);

/** Replace one inclusive event interval with two explicit revision-239 words.
 * Older overlapping intervals are split exactly as the golden client's
 * `method2869` range updater does. */
int
ToriRSServer_IfStateSeteventsV2(
    struct ToriRSServerInterfaceState* state,
    int component_uid,
    int start,
    int end,
    uint32_t events1,
    uint32_t events2);

/** Convert the classic 32-bit mask to V2. Bits 1..10 are ignored in events1
 * by revision 239 and become events2 bits 0..9. */
void
ToriRSServer_IfStateSplitClassicEvents(
    uint32_t classic,
    uint32_t* events1,
    uint32_t* events2);

/** Raw revision-239 packet bodies. These accept an already-opened buffer so
 * tests can compare their bytes against hard-coded golden fixtures without
 * decoding them through a mirror of the same implementation. */
void
ToriRSServer_IfStateEncodeSeteventsV2(
    struct RSAreaBuf* buf,
    int component_uid,
    int start,
    int end,
    uint32_t events1,
    uint32_t events2);

void
ToriRSServer_IfStateEncodeClearinv(
    struct RSAreaBuf* buf,
    int component_uid);

void
ToriRSServer_IfStateEncodeResyncV2(
    struct RSAreaBuf* buf,
    const struct ToriRSServerInterfaceState* state);

size_t
ToriRSServer_IfStateResyncPayloadSize(const struct ToriRSServerInterfaceState* state);

#endif
