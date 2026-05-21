#include "libtorirs.h"

#include "commands/libtorirs_command_queue.h"
#include "commands/libtorirs_command_queue_internal.h"
#include "input/libtorirs_input.h"
#include "libtorirs_internal.h"
#include "render/libtorirs_render.h"
#include "scripting/libtorirs_scripting.h"

#include <stdio.h>
#include <stdlib.h>

#define LIBTORIRS_INPUT_SAMPLE_HZ 50
#define LIBTORIRS_INPUT_SAMPLE_MS (1000 / LIBTORIRS_INPUT_SAMPLE_HZ)
#define LIBTORIRS_INPUT_MAX_TICKS_PER_FRAME 25

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

    instance->io_queue = LibToriRS_IOQueueNew();
    if( !instance->io_queue )
    {
        LibToriRS_Input_Free(instance->input);
        LibToriRS_ScriptQueueFree(instance->script_queue);
        free(instance);
        return NULL;
    }

    instance->dat1_buildcache = dat1_buildcache_new();
    if( !instance->dat1_buildcache )
    {
        LibToriRS_IOQueueFree(instance->io_queue);
        LibToriRS_Input_Free(instance->input);
        LibToriRS_ScriptQueueFree(instance->script_queue);
        free(instance);
        return NULL;
    }

    instance->gamecache = gamecache_new();
    if( !instance->gamecache )
    {
        dat1_buildcache_free(instance->dat1_buildcache);
        LibToriRS_IOQueueFree(instance->io_queue);
        LibToriRS_Input_Free(instance->input);
        LibToriRS_ScriptQueueFree(instance->script_queue);
        free(instance);
        return NULL;
    }

    instance->render_queue = LibToriRS_RenderQueue_New();

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
    if( instance->dat1_buildcache )
        dat1_buildcache_free(instance->dat1_buildcache);
    if( instance->gamecache )
        gamecache_free(instance->gamecache);
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
    instance->last_frame_ms = time;
    instance->input_accumulator_ms = 0;
}

struct LibToriRS_ScriptQueue*
LibToriRS_GetScriptQueue(struct LibToriRS_Instance* instance)
{
    if( !instance )
        return NULL;
    return instance->script_queue;
}

struct LibToriRS_IOQueue*
LibToriRS_GetIOQueue(struct LibToriRS_Instance* instance)
{
    if( !instance )
        return NULL;
    return instance->io_queue;
}

struct LibToriRS_RenderQueue*
LibToriRS_GetRenderQueue(struct LibToriRS_Instance* instance)
{
    if( !instance )
        return NULL;
    return instance->render_queue;
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
LibToriRS_TickInput(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_CommandQueue* command_queue,
    uint64_t time_ms)
{
    if( !instance || !command_queue )
        return;

    instance->input_accumulator_ms += time_ms - instance->last_frame_ms;
    instance->last_frame_ms = time_ms;

    if( LibToriRS_CommandQueue_IsQuit(command_queue) )
    {
        LibToriRS_ProcessCommandQueue(instance, command_queue, time_ms);
        LibToriRS_CommandQueue_Clear(command_queue);
    }

    int input_ticks = 0;
    while( instance->input_accumulator_ms >= LIBTORIRS_INPUT_SAMPLE_MS &&
           input_ticks < LIBTORIRS_INPUT_MAX_TICKS_PER_FRAME )
    {
        LibToriRS_ProcessCommandQueue(instance, command_queue, time_ms);
        LibToriRS_ProcessInput(instance);
        LibToriRS_CommandQueue_Clear(command_queue);
        instance->input_accumulator_ms -= LIBTORIRS_INPUT_SAMPLE_MS;
        input_ticks++;
    }
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

    if( instance->model_viewer )
    {
        const int camera_movement_speed = 10;
        const int camera_rotation_speed = 10;
        if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_A) )
        {
            game_modelviewer_move_right(instance->model_viewer, camera_movement_speed);
        }
        if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_D) )
        {
            game_modelviewer_move_left(instance->model_viewer, camera_movement_speed);
        }
        if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_W) )
        {
            game_modelviewer_move_forward(instance->model_viewer, camera_movement_speed);
        }
        if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_S) )
        {
            game_modelviewer_move_backward(instance->model_viewer, camera_movement_speed);
        }
        if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_R) )
        {
            game_modelviewer_move_up(instance->model_viewer, camera_movement_speed);
        }
        if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_F) )
        {
            game_modelviewer_move_down(instance->model_viewer, camera_movement_speed);
        }

        if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_UP) )
        {
            game_modelviewer_rotate_up(instance->model_viewer, camera_rotation_speed);
        }
        if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_DOWN) )
        {
            game_modelviewer_rotate_down(instance->model_viewer, camera_rotation_speed);
        }
        if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_LEFT) )
        {
            game_modelviewer_rotate_left(instance->model_viewer, camera_rotation_speed);
        }
        if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_RIGHT) )
        {
            game_modelviewer_rotate_right(instance->model_viewer, camera_rotation_speed);
        }
    }
}

void
LibToriRS_GameStep(struct LibToriRS_Instance* instance)
{
    if( !instance )
        return;

    LibToriRS_RenderQueue_Clear(instance->render_queue);

    if( instance->model_viewer )
    {
        struct ToriDraw_Position position = { 0 };
        position.x = -instance->model_viewer->camera_position->x;
        position.y = -instance->model_viewer->camera_position->y;
        position.z = -instance->model_viewer->camera_position->z;

        if( instance->model_viewer->model.kind == TORIDRAWMK_MODEL )
            LibToriRS_RenderQueue_PushCommandModelDraw(
                instance->render_queue,
                instance->model_viewer->model,
                &position,
                instance->model_viewer->view_port,
                instance->model_viewer->camera);
    }
}

bool
LibToriRS_IsRunning(struct LibToriRS_Instance* instance)
{
    if( !instance )
        return false;
    return instance->running;
}