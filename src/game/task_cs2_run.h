#ifndef TASK_CS2_RUN_H
#define TASK_CS2_RUN_H

#include "asyncio.h"

struct RS_CS2Host;
struct CS2VM2_Script;

/**
 * Cooperative CS2 script runner: binds RS_CS2Host_Exec, runs CS2VM2_ThreadRun,
 * and on YIELD awaits the matching CreateTask_*Load then re-enters ThreadRun
 * (CS2VM2 restores the opcode site so the host succeeds without PushCallScript).
 */
struct ToriRS_Task*
CreateTask_CS2Run(
    struct RS_CS2Host* host,
    int script_id,
    int active_component_id,
    int dot_component_id,
    int const* int_args,
    int int_arg_count);

/** Same as CreateTask_CS2Run but starts from an already-loaded script pointer. */
struct ToriRS_Task*
CreateTask_CS2RunScript(
    struct RS_CS2Host* host,
    struct CS2VM2_Script* script,
    int active_component_id,
    int dot_component_id,
    int const* int_args,
    int int_arg_count);

/**
 * Run registered inv-transmit hooks whose triggers match container_id
 * (or all hooks when container_id < 0).
 */
struct ToriRS_Task*
CreateTask_CS2InvTransmitDispatch(
    struct RS_CS2Host* host,
    int container_id);

#endif /* TASK_CS2_RUN_H */
