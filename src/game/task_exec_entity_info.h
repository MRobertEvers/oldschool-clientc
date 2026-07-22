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

#endif
