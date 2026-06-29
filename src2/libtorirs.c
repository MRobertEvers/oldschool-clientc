#include "libtorirs.h"

#include "3rd/minipt.h"
#include "commands/libtorirs_command_queue.h"
#include "commands/libtorirs_command_queue_internal.h"
#include "games/model_viewer.h"
#include "games/runescape.h"
#include "input/libtorirs_input.h"
#include "libtorirs_internal.h"
#include "scripting/libtorirs_scripting.h"
#include "toriauxlib/toriauxlib.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_scene.h"

#include <stdio.h>
#include <stdlib.h>

#define LIBTORIRS_INPUT_SAMPLE_HZ 50
#define LIBTORIRS_INPUT_SAMPLE_MS (1000 / LIBTORIRS_INPUT_SAMPLE_HZ)
#define LIBTORIRS_INPUT_MAX_TICKS_PER_FRAME 25

#define LIBTORIRS_ANIM_SAMPLE_HZ 50
#define LIBTORIRS_ANIM_SAMPLE_MS (1000 / LIBTORIRS_ANIM_SAMPLE_HZ)
#define LIBTORIRS_ANIM_MAX_TICKS_PER_FRAME 25

static void
projectile_task_destroy(void* state)
{
    Task_GameRunescape_WorldEntityAddProjectile_Free(state);
}

static void
player_task_destroy(void* state)
{
    Task_GameRunescape_WorldEntityAddPlayer_Free(state);
}

static void
npc_task_destroy(void* state)
{
    Task_GameRunescape_WorldEntityAddNPC_Free(state);
}

struct LibToriRS_Instance*
LibToriRS_InstanceNew(void)
{
    return LibToriRS_InstanceNewWithCacheMode(TORIAUXLIBCACHE_MODE_DAT1);
}

struct LibToriRS_Instance*
LibToriRS_InstanceNewWithCacheMode(int cache_mode)
{
    enum ToriAuxLibCacheMode mode = TORIAUXLIBCACHE_MODE_DAT1;
    if( cache_mode == TORIAUXLIBCACHE_MODE_DAT2 )
        mode = TORIAUXLIBCACHE_MODE_DAT2;
    struct LibToriRS_Instance* instance = calloc(1, sizeof(struct LibToriRS_Instance));
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

    instance->scene = ToriDraw_SceneNew(TORIDRAW_SCENE_FULL);
    if( !instance->scene )
    {
        LibToriRS_IOQueueFree(instance->io_queue);
        LibToriRS_Input_Free(instance->input);
        LibToriRS_ScriptQueueFree(instance->script_queue);
        free(instance);
        return NULL;
    }

    instance->toriauxlib = ToriAuxLib_New(mode, instance->scene);
    if( !instance->toriauxlib )
    {
        ToriDraw_SceneFree(instance->scene);
        LibToriRS_IOQueueFree(instance->io_queue);
        LibToriRS_Input_Free(instance->input);
        LibToriRS_ScriptQueueFree(instance->script_queue);
        free(instance);
        return NULL;
    }

    for( int i = 0; i < LIBTORIRS_MAX_TASKS; i++ )
    {
        instance->tasks[i].next = i + 1;
        instance->tasks[i].prev = i - 1;
    }
    instance->tasks[LIBTORIRS_MAX_TASKS - 1].next = -1;
    instance->tasks[0].prev = -1;

    instance->task_free_head = 0;
    instance->task_live_head = -1;

    instance->running = true;

    ToriDraw_Init();

    return instance;
}

void
LibToriRS_InstanceFree(struct LibToriRS_Instance* instance)
{
    if( !instance )
        return;
    if( instance->io_queue )
        LibToriRS_IOQueueFree(instance->io_queue);
    if( instance->script_queue )
        LibToriRS_ScriptQueueFree(instance->script_queue);
    if( instance->input )
        LibToriRS_Input_Free(instance->input);
    if( instance->toriauxlib )
        ToriAuxLib_Free(instance->toriauxlib);
    if( instance->scene )
        ToriDraw_SceneFree(instance->scene);

    if( instance->model_viewer )
        game_modelviewer_free(instance->model_viewer);
    if( instance->runescape )
        GameRunescape_Free(instance->runescape);
    free(instance);
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
    instance->anim_last_tick_ms = time;
    instance->anim_accumulator_ms = 0;
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
        default:
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
    instance->anim_accumulator_ms += time_ms - instance->anim_last_tick_ms;
    instance->last_frame_ms = time_ms;
    instance->anim_last_tick_ms = time_ms;

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

    if( LibToriRS_Input_IsKeyDown(input, TORIRSK_ESCAPE) )
    {
        instance->running = false;
    }

    switch( instance->active_game_kind )
    {
    case GAME_HANDLE_KIND_MODEL_VIEWER:
        if( instance->model_viewer )
        {
            const int camera_movement_speed = 10;
            const int camera_rotation_speed = 10;
            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_A) )
                game_modelviewer_move_right(instance->model_viewer, camera_movement_speed);
            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_D) )
                game_modelviewer_move_left(instance->model_viewer, camera_movement_speed);
            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_W) )
                game_modelviewer_move_forward(instance->model_viewer, camera_movement_speed);
            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_S) )
                game_modelviewer_move_backward(instance->model_viewer, camera_movement_speed);
            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_R) )
                game_modelviewer_move_up(instance->model_viewer, camera_movement_speed);
            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_F) )
                game_modelviewer_move_down(instance->model_viewer, camera_movement_speed);
            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_UP) )
                game_modelviewer_rotate_up(instance->model_viewer, camera_rotation_speed);
            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_DOWN) )
                game_modelviewer_rotate_down(instance->model_viewer, camera_rotation_speed);
            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_LEFT) )
                game_modelviewer_rotate_left(instance->model_viewer, camera_rotation_speed);
            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_RIGHT) )
                game_modelviewer_rotate_right(instance->model_viewer, camera_rotation_speed);
            if( LibToriRS_Input_IsKeyDown(input, TORIRSK_SPACE) )
                game_modelviewer_next(instance->model_viewer, 1);
            if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_BACKSPACE) )
                game_modelviewer_next(instance->model_viewer, -1);
        }
        break;
    case GAME_HANDLE_KIND_RUNESCAPE:
        if( instance->runescape )
        {
            GameRunescape_ProcessInput(instance->runescape, input);
            if( LibToriRS_Input_IsKeyDown(input, TORIRSK_SPACE) )
            {
                struct GameRunescape* game = instance->runescape;
                int sx;
                int sz;
                int level;
                if( game->last_tile_valid )
                {
                    sx = game->last_tile_sx;
                    sz = game->last_tile_sz;
                    level = game->last_tile_level;
                }
                else
                {
                    sx = game->camera_position->x / 128;
                    sz = game->camera_position->z / 128;
                    level = GameRunescape_CameraTerrainLevel(game);
                }
                int const entity_id = RS_ENTITY_ID(
                    RS_ENTITY_KIND_PROJECTILE, game->next_projectile_entity_index++);
                struct Task_GameRunescape_WorldEntityAddProjectile* task =
                    Task_GameRunescape_WorldEntityAddProjectile_New(
                        game,
                        entity_id,
                        RUNESCAPE_PROJECTILE_MODEL_ID,
                        RUNESCAPE_PROJECTILE_SEQ_ID,
                        sx,
                        sz,
                        sx + 5,
                        sz,
                        level,
                        RUNESCAPE_PROJECTILE_STARTHEIGHT,
                        RUNESCAPE_PROJECTILE_ENDHEIGHT,
                        RUNESCAPE_PROJECTILE_DELAY,
                        RUNESCAPE_PROJECTILE_ANGLE,
                        RUNESCAPE_PROJECTILE_LENGTH,
                        RUNESCAPE_PROJECTILE_OFFSET,
                        RUNESCAPE_PROJECTILE_STEP);
                if( task )
                {
                    LibToriRS_TasksAdd(
                        instance,
                        task,
                        Task_GameRunescape_WorldEntityAddProjectile_Run,
                        projectile_task_destroy);
                }
            }
            if( LibToriRS_Input_IsKeyDown(input, TORIRSK_P) )
            {
                struct GameRunescape* game = instance->runescape;
                int sx;
                int sz;
                int level;
                if( game->last_tile_valid )
                {
                    sx = game->last_tile_sx;
                    sz = game->last_tile_sz;
                    level = game->last_tile_level;
                }
                else
                {
                    sx = game->camera_position->x / 128;
                    sz = game->camera_position->z / 128;
                    level = GameRunescape_CameraTerrainLevel(game);
                }
                int const entity_id = RS_ENTITY_ID(
                    RS_ENTITY_KIND_PLAYER, game->next_player_entity_index++);
                struct Task_GameRunescape_WorldEntityAddPlayer* task =
                    Task_GameRunescape_WorldEntityAddPlayer_New(
                        game,
                        entity_id,
                        RUNESCAPE_EXAMPLE_PLAYER_APPEARANCE,
                        sx,
                        sz,
                        level,
                        RUNESCAPE_EXAMPLE_PLAYER_READYANIM,
                        RUNESCAPE_EXAMPLE_PLAYER_WALKANIM,
                        RUNESCAPE_EXAMPLE_PLAYER_TURNANIM,
                        RUNESCAPE_EXAMPLE_PLAYER_RUNANIM,
                        RUNESCAPE_EXAMPLE_PLAYER_WALKANIM_B,
                        RUNESCAPE_EXAMPLE_PLAYER_WALKANIM_R,
                        RUNESCAPE_EXAMPLE_PLAYER_WALKANIM_L);
                if( task )
                {
                    LibToriRS_TasksAdd(
                        instance,
                        task,
                        Task_GameRunescape_WorldEntityAddPlayer_Run,
                        player_task_destroy);
                }
            }
            if( LibToriRS_Input_IsKeyDown(input, TORIRSK_N) )
            {
                struct GameRunescape* game = instance->runescape;
                int sx;
                int sz;
                int level;
                int npc_id;
                if( game->last_tile_valid )
                {
                    sx = game->last_tile_sx;
                    sz = game->last_tile_sz;
                    level = game->last_tile_level;
                }
                else
                {
                    sx = game->camera_position->x / 128;
                    sz = game->camera_position->z / 128;
                    level = GameRunescape_CameraTerrainLevel(game);
                }
                npc_id = ToriAuxLibCache_Mode(ToriAuxLib_C(instance->toriauxlib)) ==
                                 TORIAUXLIBCACHE_MODE_DAT1
                             ? RUNESCAPE_EXAMPLE_NPC_ID_DAT1
                             : RUNESCAPE_EXAMPLE_NPC_ID_DAT2;
                int const entity_id =
                    RS_ENTITY_ID(RS_ENTITY_KIND_NPC, game->next_npc_entity_index++);
                struct Task_GameRunescape_WorldEntityAddNPC* task =
                    Task_GameRunescape_WorldEntityAddNPC_New(
                        game, entity_id, npc_id, sx, sz, level);
                if( task )
                {
                    LibToriRS_TasksAdd(
                        instance,
                        task,
                        Task_GameRunescape_WorldEntityAddNPC_Run,
                        npc_task_destroy);
                }
            }
        }
        break;
    default:
        break;
    }
}

void
LibToriRS_FrameBegin(struct LibToriRS_Instance* instance)
{
    if( !instance )
        return;

    int cycles_elapsed = 0;
    while( instance->anim_accumulator_ms >= LIBTORIRS_ANIM_SAMPLE_MS &&
           cycles_elapsed < LIBTORIRS_ANIM_MAX_TICKS_PER_FRAME )
    {
        instance->anim_accumulator_ms -= LIBTORIRS_ANIM_SAMPLE_MS;
        cycles_elapsed++;
    }
    instance->anim_cycle_count += (uint64_t)cycles_elapsed;

    switch( instance->active_game_kind )
    {
    case GAME_HANDLE_KIND_MODEL_VIEWER:
        if( instance->model_viewer )
            game_modelviewer_frame_begin(instance->model_viewer, cycles_elapsed);
        break;
    case GAME_HANDLE_KIND_RUNESCAPE:
        if( instance->runescape )
            GameRunescape_FrameBegin(instance->runescape, cycles_elapsed);
        break;
    default:
        break;
    }
}

uint64_t
LibToriRS_GetAnimationClock(struct LibToriRS_Instance* instance)
{
    return instance->anim_cycle_count;
}

bool
LibToriRS_FrameNextCommand(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    switch( instance->active_game_kind )
    {
    case GAME_HANDLE_KIND_MODEL_VIEWER:
        if( instance->model_viewer &&
            game_modelviewer_frame_next_command(instance->model_viewer, command) )
            return true;
        break;
    case GAME_HANDLE_KIND_RUNESCAPE:
        if( instance->runescape && GameRunescape_FrameNextCommand(instance->runescape, command) )
            return true;
        break;
    default:
        break;
    }
    return false;
}

void
LibToriRS_FrameEnd(struct LibToriRS_Instance* instance)
{
    if( !instance )
        return;
    switch( instance->active_game_kind )
    {
    case GAME_HANDLE_KIND_MODEL_VIEWER:
        if( instance->model_viewer )
            game_modelviewer_frame_end(instance->model_viewer);
        break;
    case GAME_HANDLE_KIND_RUNESCAPE:
        if( instance->runescape )
            GameRunescape_FrameEnd(instance->runescape);
        break;
    default:
        break;
    }
}

bool
LibToriRS_IsRunning(struct LibToriRS_Instance* instance)
{
    if( !instance )
        return false;
    return instance->running;
}

struct ToriDraw_Scene*
LibToriRS_GetCurrentToriDrawScene(struct LibToriRS_Instance* instance)
{
    if( !instance )
        return NULL;
    return instance->scene;
}

struct ToriAuxLib*
LibToriRS_GetToriAuxLib(struct LibToriRS_Instance* instance)
{
    if( !instance )
        return NULL;
    return instance->toriauxlib;
}

static void
tasks_remove(
    struct LibToriRS_Instance* instance,
    int task_idx)
{
    if( task_idx == -1 || task_idx >= LIBTORIRS_MAX_TASKS )
        return;

    struct LibToriRS_Task* task_to_remove = &instance->tasks[task_idx];

    // ==========================================
    // PHASE 1: UNLINK FROM THE LIVE LIST
    // ==========================================
    int prev_idx = task_to_remove->prev;
    int next_idx = task_to_remove->next;

    // 1a. Update the previous node (or the live head if this is the first node)
    if( prev_idx != -1 )
    {
        instance->tasks[prev_idx].next = next_idx;
    }
    else if( instance->task_live_head == task_idx )
    {
        // If there is no previous node AND this is the head,
        // the next node becomes the new head.
        instance->task_live_head = next_idx;
    }

    // 1b. Update the next node
    if( next_idx != -1 )
    {
        instance->tasks[next_idx].prev = prev_idx;
    }

    // ==========================================
    // PHASE 2: PUSH ONTO THE FREE LIST
    // ==========================================

    // Clear the payload (optional, but prevents dangling pointers)
    task_to_remove->task = NULL;

    // Push to the front of the free list
    task_to_remove->next = instance->task_free_head;
    task_to_remove->prev = -1; // Free list doesn't strictly need prev, but it's safe to clear

    instance->task_free_head = task_idx;
}

void
LibToriRS_TasksAdd(
    struct LibToriRS_Instance* instance,
    void* task_state,
    CoreTaskFunction task_function,
    CoreTaskDestructor destroy)
{
    if( instance->task_free_head == -1 )
    {
        fprintf(stderr, "No free tasks available\n");
        assert(0);
        return;
    }

    struct CoreTask* task = core_task_new(task_state, task_function, destroy);
    if( !task )
    {
        fprintf(stderr, "Failed to create task\n");
        assert(0);
        return;
    }

    // 1. Get the new task
    int task_idx = instance->task_free_head;
    struct LibToriRS_Task* new_task = &instance->tasks[task_idx];

    // 2. POP FROM FREE LIST FIRST
    // Update the free head while new_task->next still points to the next free node
    instance->task_free_head = new_task->next;

    // 3. Setup the new task's payload
    new_task->task = task;
    new_task->wait_run = -1;

    // 4. PUSH TO LIVE LIST (Front)
    new_task->next = instance->task_live_head;
    new_task->prev = -1;

    if( instance->task_live_head != -1 )
    {
        // Update the old live head's prev pointer
        instance->tasks[instance->task_live_head].prev = task_idx;
    }

    // Set the new live head
    instance->task_live_head = task_idx;
}

bool
LibToriRS_TasksRun(struct LibToriRS_Instance* instance)
{
    if( !instance || instance->task_live_head == -1 )
        return false;

    struct LibToriRS_IOContext ctx = {
        .io = instance->io_queue,
    };

    int task_idx = instance->task_live_head;
    struct LibToriRS_Task* task = &instance->tasks[task_idx];

    if( task->wait_run >= 0 && !LibToriRS_IOQueueRunComplete(instance->io_queue, task->wait_run) )
        return true;

    int run = LibToriRS_IOQueueBeginRun(instance->io_queue);
    task->last_res = task->task->task(task->task->state, &ctx);

    switch( task->last_res )
    {
    case PT_YIELDED:
        task->wait_run = run;
        break;
    case PT_EXITED:
    case PT_ENDED:
        core_task_free(task->task);
        task->task = NULL;
        task->wait_run = -1;
        tasks_remove(instance, task_idx);
        break;
    default:
        break;
    }

    return instance->task_live_head != -1;
}

bool
LibToriRS_TasksHasLive(struct LibToriRS_Instance* instance)
{
    if( !instance )
        return false;
    return instance->task_live_head != -1;
}