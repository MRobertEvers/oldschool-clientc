#ifndef SRC_GAME_TASK_GAMEPROTO_EXEC_H
#define SRC_GAME_TASK_GAMEPROTO_EXEC_H

/*
 * One inbound packet = one Task. Enqueued FIFO on app->exec_runner (the
 * serial game-action pipeline), so packets execute in wire order across IO
 * yields, and any follow-on task a packet enqueues (interface slot mount,
 * world rebuild) runs before the next packet is popped — the pump in
 * app_logic_tick only pops a new packet when the queue is idle.
 *
 * Simple state mutations run synchronously through RS_GameProto_Exec (kept
 * App-optional and pure for the unit tests); packets whose execution needs
 * cache IO (entity info decode, world rebuild) await their loads inside the
 * task — never a blocking drain.
 */

#include "net/rev/revpacket.h"

struct App;
struct ToriRS_Task;
struct World;

/**
 * Map-square rect covering the classic 104x104 scene for a REBUILD_NORMAL
 * center zone: scene sw tile = (zone - 6) * 8, squares covering [sw, sw+103].
 * Exposed for the residency unit test (test-net-exec).
 */
void
rebuild_square_rect(
    int zone_x,
    int zone_z,
    int* mx0,
    int* mz0,
    int* mx1,
    int* mz1);

/** True when every square in [mx0..mx1] x [mz0..mz1] is already resident. */
int
rebuild_squares_resident(
    struct World const* world,
    int mx0,
    int mz0,
    int mx1,
    int mz1);

/** Takes ownership of the packet's heap fields (freed with the task). */
struct ToriRS_Task*
CreateTask_GameProtoExec(
    struct App* app,
    struct RevPacket* packet);

#endif
