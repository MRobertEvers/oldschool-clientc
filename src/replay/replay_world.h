#ifndef SRC_REPLAY_REPLAY_WORLD_H
#define SRC_REPLAY_REPLAY_WORLD_H

/*
 * A seek's world keyframe on its way from the replay transport to the
 * executor (the replay scrubber design, "A seek, step by step": World keyframe
 * at P).
 *
 * The transport builds it from the ledger (ReplayLedger_WorldAt) when the
 * chase reaches P, posts it here, and stages a TORIRS_CONTROL_REPLAY_WORLD
 * control frame (net/torirs_control.h) carrying its tick and serial at that
 * byte of the stream; the executor takes the delivery by that serial
 * (task_gameproto_exec.c, replay_world_apply.c) and asserts it is the one
 * here. The keyframe is megabytes of tables and op lists, so it never crosses
 * the stream: the transport and the client are one process.
 *
 * One slot: a seek has one keyframe in flight, and the scrubber hands out no
 * other until the client has taken it (ReplayWorld_Pending). A keyframe whose
 * frame never reached the client -- the session dropped with it still staged
 * -- is withdrawn by the transport.
 */

#include "replay/replay_ledger.h"

#include <stdint.h>

struct ReplayWorldDelivery
{
    struct ReplayWorldKeyframe kf;
    /** 1: the scene in force at the keyframe is not the one the client has;
     *  rebuild it from `scene_bytes` (the original REBUILD, verbatim) through
     *  the normal path, MAP_BUILD_COMPLETE and all. 0: the scene is loaded;
     *  every zone is cleared the way UPDATE_ZONE_FULL_FOLLOWS clears one. */
    int rebuild;
    /** The REBUILD's wire opcode and payload (rebuild only). */
    int scene_wire;
    uint8_t* scene_bytes;
    int scene_len;
    /** The tick the keyframe stands in front of, for the report. */
    int tick;
};

/** Post `d` (ownership moves here) into the empty slot. Returns its serial,
 *  never 0. */
uint32_t
ReplayWorld_Post(struct ReplayWorldDelivery* d);

/** The delivery posted under `serial`, handed over (free it with
 *  ReplayWorldDelivery_Free); NULL when it is not the one in the slot. */
struct ReplayWorldDelivery*
ReplayWorld_Take(uint32_t serial);

/** Free the delivery posted under `serial`, which must be the one in the slot:
 *  its frame will never reach the client. */
void
ReplayWorld_Withdraw(uint32_t serial);

/** 1 while a posted delivery waits for the client. */
int
ReplayWorld_Pending(void);

/** Accepts NULL. */
void
ReplayWorldDelivery_Free(struct ReplayWorldDelivery* d);

struct App;
struct ToriRS_Task;

/** The world's dynamic state gone (replay_world_apply.c): every player and
 *  npc this client tracks, the world entities, queued zone packets. A chase's
 *  first tick does it (its world packets are withheld, so whatever stands is
 *  the world the seek started from, animating for nothing), and the keyframe
 *  does it again before it puts its own world in. */
void
ReplayWorld_ClearEntities(struct App* app);

/** Apply a delivery (replay_world_apply.c): the executor's REPLAY_WORLD
 *  control-frame arm awaits it. Takes ownership of `d`. */
struct ToriRS_Task*
CreateTask_ReplayWorldApply(
    struct App* app,
    struct ReplayWorldDelivery* d);

#endif
