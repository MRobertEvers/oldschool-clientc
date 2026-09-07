#ifndef SRC_GAME_TASK_EXEC_ENTITY_INFO_H
#define SRC_GAME_TASK_EXEC_ENTITY_INFO_H

/*
 * PLAYER_INFO / NPC_INFO application tasks: decode the command stream (pure
 * CPU), then walk the ops, awaiting cache loads (npc configs, appearance
 * idk/obj configs + models, sequences) before each spawn/appearance apply —
 * the packet's own task context, never a blocking drain. Awaited by
 * Task_GameProtoExec so packet order holds.
 */

#include <stdint.h>

struct App;

/** Origin shared by all root NPC deltas, including when the local player is
 * aboard. The explicit packet origin takes precedence over the player route. */
void RS_EntityInfo_NpcOrigin(struct App* app, int* out_x, int* out_z, int* out_level);
struct ToriRS_Task;

struct ToriRS_Task*
CreateTask_ExecPlayerInfo(
    struct App* app,
    uint8_t const* data,
    int length);

struct ToriRS_Task*
CreateTask_ExecNpcInfo(
    struct App* app,
    uint8_t const* data,
    int length);

/*
 * Releases the op-array scratch both decoders borrow from App. Call once at
 * teardown; safe on an App that never ran a packet.
 */
void
Task_EntityInfoScratchFree(struct App* app);

#endif
