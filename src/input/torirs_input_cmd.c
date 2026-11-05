#include "torirs_input_cmd.h"

#include <string.h>

/* A gesture frame's identities: the fields its kind does not carry. */
static void
gesture_init(
    struct LibToriRS_Gesture* gesture,
    enum LibToriRS_GestureKind kind,
    uint8_t phase,
    uint8_t flags,
    int16_t x,
    int16_t y)
{
    memset(gesture, 0, sizeof(*gesture));
    gesture->kind = (uint8_t)kind;
    gesture->phase = phase;
    gesture->flags = flags;
    gesture->x = x;
    gesture->y = y;
    gesture->scale = 1.0f;
}

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
    case TORIRS_CMD_INPUT_PINCH:
    {
        struct ToriRS_CmdPinch cmd;
        if( header->length < sizeof(cmd) )
            return 1;
        memcpy(&cmd, payload, sizeof(cmd));
        if( cmd.scale_q16 > 0 )
            LibToriRS_Input_PushPinch(
                input, (float)cmd.scale_q16 / 65536.0f, cmd.x, cmd.y, cmd.from_wheel);
        return 1;
    }
    case TORIRS_CMD_INPUT_SCROLL:
    {
        struct ToriRS_CmdScroll cmd;
        if( header->length < sizeof(cmd) )
            return 1;
        memcpy(&cmd, payload, sizeof(cmd));
        if( cmd.px_per_notch > 0 )
            LibToriRS_Input_PushScroll(
                input,
                (float)cmd.dx_milli / 1000.0f,
                (float)cmd.dy_milli / 1000.0f,
                cmd.px_per_notch,
                cmd.trackpad,
                cmd.flipped);
        return 1;
    }
    case TORIRS_CMD_GESTURE_PAN:
    {
        struct ToriRS_CmdGesturePan cmd;
        struct LibToriRS_Gesture gesture;
        if( header->length < sizeof(cmd) )
            return 1;
        memcpy(&cmd, payload, sizeof(cmd));
        /* A frame from a recording is data, not a call: one no encoder could
         * have written (CmdBus_PushGesturePan asserts all three) is dropped,
         * as a short payload is. */
        if( cmd.phase >= LIBTORIRS_GESTURE_PHASE_COUNT || cmd.fingers == 0 )
            return 1;
        gesture_init(&gesture, LIBTORIRS_GESTURE_PAN, cmd.phase, cmd.flags, cmd.x, cmd.y);
        gesture.fingers = cmd.fingers;
        gesture.dx = (float)cmd.dx_q16 / 65536.0f;
        gesture.dy = (float)cmd.dy_q16 / 65536.0f;
        LibToriRS_Input_PushGesture(input, &gesture);
        return 1;
    }
    case TORIRS_CMD_GESTURE_PINCH:
    {
        struct ToriRS_CmdGesturePinch cmd;
        struct LibToriRS_Gesture gesture;
        if( header->length < sizeof(cmd) )
            return 1;
        memcpy(&cmd, payload, sizeof(cmd));
        if( cmd.phase >= LIBTORIRS_GESTURE_PHASE_COUNT || cmd.scale_q16 <= 0 )
            return 1;
        gesture_init(&gesture, LIBTORIRS_GESTURE_PINCH, cmd.phase, cmd.flags, cmd.x, cmd.y);
        gesture.scale = (float)cmd.scale_q16 / 65536.0f;
        LibToriRS_Input_PushGesture(input, &gesture);
        return 1;
    }
    case TORIRS_CMD_GESTURE_TWIST:
    {
        struct ToriRS_CmdGestureTwist cmd;
        struct LibToriRS_Gesture gesture;
        if( header->length < sizeof(cmd) )
            return 1;
        memcpy(&cmd, payload, sizeof(cmd));
        if( cmd.phase >= LIBTORIRS_GESTURE_PHASE_COUNT )
            return 1;
        gesture_init(&gesture, LIBTORIRS_GESTURE_TWIST, cmd.phase, cmd.flags, cmd.x, cmd.y);
        gesture.radians = (float)cmd.radians_q16 / 65536.0f;
        LibToriRS_Input_PushGesture(input, &gesture);
        return 1;
    }
    case TORIRS_CMD_INPUT_CLEAR_KEYS:
        LibToriRS_Input_ClearKeys(input);
        return 1;
    case TORIRS_CMD_INPUT_MOUSE_LEAVE:
        LibToriRS_Input_PushMouseLeave(input);
        return 1;
    default:
        return 0;
    }
}
