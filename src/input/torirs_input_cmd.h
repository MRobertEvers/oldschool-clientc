#ifndef SRC_INPUT_TORIRS_INPUT_CMD_H
#define SRC_INPUT_TORIRS_INPUT_CMD_H

#include "cmd/cmdbus.h"
#include "input/torirs_input.h"

/**
 * Apply one TORIRS_CMD_INPUT_* command to the input state. This is the single
 * decode point between the command bus and ToriRS_Input; every producer (SDL,
 * replay, test harnesses) encodes, and only this function decodes.
 *
 * Returns 1 when the command was an input command (now applied), 0 otherwise.
 */
int
ToriRS_Input_ApplyCmd(
    struct LibToriRS_Input* input,
    const struct ToriRS_CmdHeader* header,
    const uint8_t* payload);

#endif
