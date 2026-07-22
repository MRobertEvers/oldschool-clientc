#include "torirs_input_cmd.h"

#include <string.h>

int
ToriRS_Input_ApplyCmd(
    struct LibToriRS_Input* input,
    const struct ToriRS_CmdHeader* header,
    const uint8_t* payload)
{
    switch( header->type )
    {
    case TORIRS_CMD_INPUT_KEY_DOWN:
    {
        struct ToriRS_CmdKey cmd;
        if( header->length < sizeof(cmd) )
            return 1;
        memcpy(&cmd, payload, sizeof(cmd));
        LibToriRS_Input_PushKeyDown(input, (enum LibToriRS_KeyCode)cmd.keycode);
        return 1;
    }
    case TORIRS_CMD_INPUT_KEY_UP:
    {
        struct ToriRS_CmdKey cmd;
        if( header->length < sizeof(cmd) )
            return 1;
        memcpy(&cmd, payload, sizeof(cmd));
        LibToriRS_Input_PushKeyUp(input, (enum LibToriRS_KeyCode)cmd.keycode);
        return 1;
    }
    case TORIRS_CMD_INPUT_KEY_EVENT:
    {
        struct ToriRS_CmdKeyEvent cmd;
        if( header->length < sizeof(cmd) )
            return 1;
        memcpy(&cmd, payload, sizeof(cmd));
        LibToriRS_Input_PushKeyEvent(input, cmd.key_typed, cmd.key_pressed, cmd.is_repeat);
        return 1;
    }
    case TORIRS_CMD_INPUT_OSRS_KEY:
    {
        struct ToriRS_CmdOsrsKey cmd;
        if( header->length < sizeof(cmd) )
            return 1;
        memcpy(&cmd, payload, sizeof(cmd));
        LibToriRS_Input_SetOsrsKeyState(input, cmd.osrs_key, cmd.down, cmd.pressed_edge);
        return 1;
    }
    case TORIRS_CMD_INPUT_MOUSE_DOWN:
    case TORIRS_CMD_INPUT_MOUSE_UP:
    {
        struct ToriRS_CmdMouseButton cmd;
        if( header->length < sizeof(cmd) )
            return 1;
        memcpy(&cmd, payload, sizeof(cmd));
        if( header->type == TORIRS_CMD_INPUT_MOUSE_DOWN )
            LibToriRS_Input_PushMouseDown(
                input, (enum LibToriRS_MouseButton)cmd.button, cmd.x, cmd.y);
        else
            LibToriRS_Input_PushMouseUp(
                input, (enum LibToriRS_MouseButton)cmd.button, cmd.x, cmd.y);
        return 1;
    }
    case TORIRS_CMD_INPUT_MOUSE_MOVE:
    {
        struct ToriRS_CmdMouseMove cmd;
        if( header->length < sizeof(cmd) )
            return 1;
        memcpy(&cmd, payload, sizeof(cmd));
        LibToriRS_Input_PushMouseMove(input, cmd.x, cmd.y);
        return 1;
    }
    case TORIRS_CMD_INPUT_MOUSE_WHEEL:
    {
        struct ToriRS_CmdMouseWheel cmd;
        if( header->length < sizeof(cmd) )
            return 1;
        memcpy(&cmd, payload, sizeof(cmd));
        LibToriRS_Input_PushMouseWheel(input, cmd.wheel_y);
        return 1;
    }
    case TORIRS_CMD_INPUT_CLEAR_KEYS:
        LibToriRS_Input_ClearKeys(input);
        return 1;
    default:
        return 0;
    }
}
