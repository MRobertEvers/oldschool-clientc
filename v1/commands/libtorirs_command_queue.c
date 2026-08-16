#include "libtorirs_command_queue.h"

#include "libtorirs_command_queue_internal.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct LibToriRS_Command*
emplace_command(struct LibToriRS_CommandQueue* command_queue)
{
    assert(command_queue);
    if( command_queue->count >= LIBTORIRS_COMMAND_QUEUE_MAX_SIZE )
        return NULL;
    return &command_queue->commands[command_queue->count++];
}

struct LibToriRS_CommandQueue*
LibToriRS_CommandQueue_New(void)
{
    struct LibToriRS_CommandQueue* command_queue = malloc(sizeof(struct LibToriRS_CommandQueue));
    if( !command_queue )
        return NULL;
    memset(command_queue, 0, sizeof(struct LibToriRS_CommandQueue));
    return command_queue;
}

void
LibToriRS_CommandQueue_Free(struct LibToriRS_CommandQueue* command_queue)
{
    if( !command_queue )
        return;
    free(command_queue);
    command_queue = NULL;
}

void
LibToriRS_CommandQueue_Clear(struct LibToriRS_CommandQueue* command_queue)
{
    assert(command_queue);
    command_queue->count = 0;
}

bool
LibToriRS_CommandQueue_IsQuit(struct LibToriRS_CommandQueue* command_queue)
{
    assert(command_queue);
    return command_queue->quit;
}

void
LibToriRS_CommandQueue_PushQuitEvent(struct LibToriRS_CommandQueue* command_queue)
{
    assert(command_queue);

    command_queue->quit = true;

    struct LibToriRS_Command* command = emplace_command(command_queue);
    if( !command )
        return;
    command->type = LIBTORIRS_COMMAND_TYPE_QUIT;
}

void
LibToriRS_CommandQueue_PushKeyEvent(
    struct LibToriRS_CommandQueue* command_queue,
    enum LibToriRS_KeyCode key,
    enum LibToriRS_KeyEventType event_type)
{
    assert(command_queue);
    struct LibToriRS_Command* command = emplace_command(command_queue);
    if( !command )
        return;
    switch( event_type )
    {
    case TORIRSEV_KEY_DOWN:
        command->type = LIBTORIRS_COMMAND_TYPE_KEY_DOWN;
        command->u.key_down.key = key;
        command->u.key_down.event_type = event_type;
        break;
    case TORIRSEV_KEY_UP:
        command->type = LIBTORIRS_COMMAND_TYPE_KEY_UP;
        command->u.key_up.key = key;
        command->u.key_up.event_type = event_type;
        break;
    default:
        assert(0);
        return;
    }
}

void
LibToriRS_CommandQueue_PushMouseButtonEvent(
    struct LibToriRS_CommandQueue* command_queue,
    enum LibToriRS_MouseButton button,
    enum LibToriRS_MouseEventType event_type,
    int mouse_x,
    int mouse_y)
{
    assert(command_queue);
    struct LibToriRS_Command* command = emplace_command(command_queue);
    if( !command )
        return;

    switch( event_type )
    {
    case TORIRSEV_MOUSE_DOWN:
        command->type = LIBTORIRS_COMMAND_TYPE_MOUSE_DOWN;
        command->u.mouse_down.button = button;
        command->u.mouse_down.event_type = event_type;
        command->u.mouse_down.mouse_x = mouse_x;
        command->u.mouse_down.mouse_y = mouse_y;
        break;
    case TORIRSEV_MOUSE_UP:
        command->type = LIBTORIRS_COMMAND_TYPE_MOUSE_UP;
        command->u.mouse_up.button = button;
        command->u.mouse_up.event_type = event_type;
        command->u.mouse_up.mouse_x = mouse_x;
        command->u.mouse_up.mouse_y = mouse_y;
        break;
    default:
        assert(0);
        return;
    }
}

void
LibToriRS_CommandQueue_PushMouseMoveEvent(
    struct LibToriRS_CommandQueue* command_queue,
    int mouse_x,
    int mouse_y,
    int xrel,
    int yrel)
{
    assert(command_queue);
    struct LibToriRS_Command* command = emplace_command(command_queue);
    if( !command )
        return;
    command->type = LIBTORIRS_COMMAND_TYPE_MOUSE_MOVE;
    command->u.mouse_move.mouse_x = mouse_x;
    command->u.mouse_move.mouse_y = mouse_y;
    command->u.mouse_move.xrel = xrel;
    command->u.mouse_move.yrel = yrel;
}

void
LibToriRS_CommandQueue_PushMouseWheelEvent(
    struct LibToriRS_CommandQueue* command_queue,
    int wheel_y,
    int mouse_x,
    int mouse_y)
{
    assert(command_queue);
    struct LibToriRS_Command* command = emplace_command(command_queue);
    if( !command )
        return;
    command->type = LIBTORIRS_COMMAND_TYPE_MOUSE_WHEEL;
    command->u.mouse_wheel.wheel = wheel_y;
    command->u.mouse_wheel.mouse_x = mouse_x;
    command->u.mouse_wheel.mouse_y = mouse_y;
}