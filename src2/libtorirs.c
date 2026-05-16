#include "libtorirs.h"

#include "commands/libtorirs_command_queue_internal.h"
#include "input/libtorirs_input.h"
#include "libtorirs_internal.h"
#include "scripting/libtorirs_scripting.h"

#include <stdio.h>
#include <stdlib.h>

struct LibToriRS_Instance*
LibToriRS_InstanceNew(void)
{
    struct LibToriRS_Instance* instance = malloc(sizeof(struct LibToriRS_Instance));
    if( !instance )
        return NULL;
    instance->script_queue = LibToriRS_ScriptQueueNew();
    if( !instance->script_queue )
    {
        free(instance);
        return NULL;
    }

    instance->input = LibToriRS_Input_New();
    if( !instance->input )
    {
        LibToriRS_ScriptQueueFree(instance->script_queue);
        free(instance);
        return NULL;
    }

    instance->running = true;

    return instance;
}

void
LibToriRS_InstanceFree(struct LibToriRS_Instance* instance)
{
    if( !instance )
        return;
    if( instance->script_queue )
        LibToriRS_ScriptQueueFree(instance->script_queue);
    if( instance->input )
        LibToriRS_Input_Free(instance->input);
    free(instance);
    instance = NULL;
}

void
LibToriRS_InitTime(
    struct LibToriRS_Instance* instance,
    uint64_t time)
{
    if( !instance )
        return;
    LibToriRS_Input_Init(instance->input, time);
}

struct LibToriRS_ScriptQueue*
LibToriRS_GetScriptQueue(struct LibToriRS_Instance* instance)
{
    if( !instance )
        return NULL;
    return instance->script_queue;
}

void
LibToriRS_ProcessCommandQueue(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_CommandQueue* command_queue,
    uint64_t time)
{
    if( !instance )
        return;

    LibToriRS_Input_Begin(instance->input, time);

    for( int i = 0; i < command_queue->count; i++ )
    {
        switch( command_queue->commands[i].type )
        {
        case LIBTORIRS_COMMAND_TYPE_QUIT:
            instance->running = false;
            break;
        case LIBTORIRS_COMMAND_TYPE_KEY_DOWN:
            LibToriRS_Input_PushKeyDown(instance->input, command_queue->commands[i].u.key_down.key);
            break;
        case LIBTORIRS_COMMAND_TYPE_KEY_UP:
            LibToriRS_Input_PushKeyUp(instance->input, command_queue->commands[i].u.key_up.key);
            break;
        case LIBTORIRS_COMMAND_TYPE_MOUSE_DOWN:
            LibToriRS_Input_PushMouseDown(
                instance->input,
                command_queue->commands[i].u.mouse_down.button,
                command_queue->commands[i].u.mouse_down.mouse_x,
                command_queue->commands[i].u.mouse_down.mouse_y);
            break;
        case LIBTORIRS_COMMAND_TYPE_MOUSE_UP:
            LibToriRS_Input_PushMouseUp(
                instance->input,
                command_queue->commands[i].u.mouse_up.button,
                command_queue->commands[i].u.mouse_up.mouse_x,
                command_queue->commands[i].u.mouse_up.mouse_y);
            break;
        case LIBTORIRS_COMMAND_TYPE_MOUSE_MOVE:
            LibToriRS_Input_PushMouseMove(
                instance->input,
                command_queue->commands[i].u.mouse_move.mouse_x,
                command_queue->commands[i].u.mouse_move.mouse_y);
            break;
        }
    }

    LibToriRS_Input_End(instance->input);
}

void
LibToriRS_ProcessInput(struct LibToriRS_Instance* instance)
{
    if( !instance )
        return;

    struct LibToriRS_Input* input = instance->input;

    for( enum LibToriRS_MouseButton b = TORIRSM_LEFT; b < TORIRSM_COUNT; b++ )
    {
        if( LibToriRS_Input_IsDragStart(input, b) )
            printf(
                "LibToriRS_Input_IsDragStart(button=%d) at (%d,%d)\n",
                (int)b,
                input->curr.mouse_x,
                input->curr.mouse_y);

        if( LibToriRS_Input_IsDragEnd(input, b) )
            printf(
                "LibToriRS_Input_IsDragEnd(button=%d) at (%d,%d)\n",
                (int)b,
                input->curr.mouse_x,
                input->curr.mouse_y);

        if( LibToriRS_Input_IsDoubleClick(input, b) )
            printf(
                "LibToriRS_Input_IsDoubleClick(button=%d) at (%d,%d)\n",
                (int)b,
                input->curr.mouse_x,
                input->curr.mouse_y);
        else if( LibToriRS_Input_IsClick(input, b) )
            printf(
                "LibToriRS_Input_IsClick(button=%d) at (%d,%d)\n",
                (int)b,
                input->curr.mouse_x,
                input->curr.mouse_y);
    }

    if( LibToriRS_Input_IsKeyDown(input, TORIRSK_SPACE) )
    {
        struct LibToriRS_Script* script = LibToriRS_ScriptQueueEmplace(
            LibToriRS_GetScriptQueue(instance), "print('Hello, World!')");
        script->is_inline = true;
    }

    if( LibToriRS_Input_IsKeyDown(input, TORIRSK_ESCAPE) )
    {
        instance->running = false;
    }
}

bool
LibToriRS_IsRunning(struct LibToriRS_Instance* instance)
{
    if( !instance )
        return false;
    return instance->running;
}