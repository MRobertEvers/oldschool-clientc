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

/** Takes ownership of the packet's heap fields (freed with the task). */
struct ToriRS_Task*
CreateTask_GameProtoExec(
    struct App* app,
    struct RevPacket* packet);

#endif
