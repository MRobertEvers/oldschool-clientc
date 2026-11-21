#ifndef SRC_GAME_TASK_GAMEPROTO_EXEC_H
#define SRC_GAME_TASK_GAMEPROTO_EXEC_H

/*
 * One inbound packet = one Task. Enqueued FIFO on app->packet_runner (the
 * serial game-action pipeline), so packets execute in wire order across IO
 * yields, and any follow-on task a packet enqueues (interface slot mount,
 * world rebuild) runs before the next packet is popped — the pump in
 * app_logic_tick only pops a new packet when the queue is idle.
 *
 * Simple state mutations run synchronously through App_GameProto_Exec (the
 * model, RS_ServerState_Apply, then the App's presentation; the unit tests
 * drive the App-free RS_GameProto_Exec); packets whose execution needs
 * cache IO (entity info decode, world rebuild) await their loads inside the
 * task — never a blocking drain.
 */

#include "net/rev/revpacket.h"

struct App;
struct ToriRS_Task;

/**
 * Map-square rect covering the classic 104x104 scene for a REBUILD_NORMAL
 * center zone: scene sw tile = (zone - 6) * 8, squares covering [sw, sw+103].
 * Exposed for the unit test (test-net-exec) and asset prefetch.
 */
void
rebuild_square_rect(
    int zone_x,
    int zone_z,
    int* mx0,
    int* mz0,
    int* mx1,
    int* mz1);

/**
 * Free the executor's finished task objects.
 *
 * One task object is made per server packet and dropped when the packet has
 * been applied: 4 KB each, eighty on the tick a bank opens. The executor keeps
 * finished ones and the next packet takes one (task_gameproto_exec.c). Call
 * this once the packet queue has been freed; tasks released after it are
 * freed, not kept.
 */
void
GameProtoExec_TaskPoolFree(void);

/** Takes ownership of the packet's heap fields (freed with the task). */
struct ToriRS_Task*
CreateTask_GameProtoExec(
    struct App* app,
    struct RevPacket* packet);

#endif
