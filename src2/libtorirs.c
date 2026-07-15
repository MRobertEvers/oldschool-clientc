#include "libtorirs.h"

#include "3rd/minipt.h"
#include "commands/libtorirs_command_queue.h"
#include "commands/libtorirs_command_queue_internal.h"
#include "games/interface_editor.h"
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

    LibToriRS_TaskRunner_Init(&instance->task_runner, instance->io_queue);

    instance->running = true;

    ToriDraw_Init();

    return instance;
}

void
LibToriRS_InstanceSetClientKronos(
    struct LibToriRS_Instance* instance,
    bool kronos)
{
    if( !instance )
        return;
    instance->client_kronos = kronos;
    if( instance->toriauxlib )
    {
        struct ToriAuxLibCache* cache = ToriAuxLib_C(instance->toriauxlib);
        if( cache )
            ToriAuxLibCache_SetClientKronos(cache, kronos);
    }
}

bool
LibToriRS_InstanceIsKronos(struct LibToriRS_Instance* instance)
{
    return instance && instance->client_kronos;
}

void
LibToriRS_InstanceFree(struct LibToriRS_Instance* instance)
{
    if( !instance )
        return;
    while( instance->task_runner.count > 0 )
    {
        struct LibToriRS_Task* task = instance->task_runner.tasks[instance->task_runner.head];
        if( task && task->vtable && task->vtable->free_fn )
            task->vtable->free_fn(task);
        LibToriRS_TaskRunner_Pop(&instance->task_runner);
    }
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
    if( instance->interface_editor )
        GameInterfaceEditor_Free(instance->interface_editor);
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
        case LIBTORIRS_COMMAND_TYPE_MOUSE_WHEEL:
            LibToriRS_Input_PushMouseWheel(
                instance->input,
                command_queue->commands[i].u.mouse_wheel.wheel);
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

    if( LibToriRS_Input_IsKeyDown(input, TORIRSK_ESCAPE) &&
        instance->active_game_kind != GAME_HANDLE_KIND_INTERFACE_EDITOR )
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
                if( game->world_pick.last_tile_valid )
                {
                    sx = game->world_pick.last_tile_sx;
                    sz = game->world_pick.last_tile_sz;
                    level = game->world_pick.last_tile_level;
                }
                else
                {
                    sx = game->camera_position->x / 128;
                    sz = game->camera_position->z / 128;
                    level = GameRunescape_CameraTerrainLevel(game);
                }
                int const entity_id = RS_ENTITY_ID(
                    RS_ENTITY_KIND_PROJECTILE, game->entities.next_projectile_index++);
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
                    LibToriRS_TasksAddLegacy(
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
                if( game->world_pick.last_tile_valid )
                {
                    sx = game->world_pick.last_tile_sx;
                    sz = game->world_pick.last_tile_sz;
                    level = game->world_pick.last_tile_level;
                }
                else
                {
                    sx = game->camera_position->x / 128;
                    sz = game->camera_position->z / 128;
                    level = GameRunescape_CameraTerrainLevel(game);
                }
                int const entity_id = RS_ENTITY_ID(
                    RS_ENTITY_KIND_PLAYER, game->entities.next_player_index++);
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
                    LibToriRS_TasksAddLegacy(
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
                if( game->world_pick.last_tile_valid )
                {
                    sx = game->world_pick.last_tile_sx;
                    sz = game->world_pick.last_tile_sz;
                    level = game->world_pick.last_tile_level;
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
                    RS_ENTITY_ID(RS_ENTITY_KIND_NPC, game->entities.next_npc_index++);
                struct Task_GameRunescape_WorldEntityAddNPC* task =
                    Task_GameRunescape_WorldEntityAddNPC_New(
                        game, entity_id, npc_id, sx, sz, level);
                if( task )
                {
                    LibToriRS_TasksAddLegacy(
                        instance,
                        task,
                        Task_GameRunescape_WorldEntityAddNPC_Run,
                        npc_task_destroy);
                }
            }
        }
        break;
    case GAME_HANDLE_KIND_INTERFACE_EDITOR:
        if( instance->interface_editor )
            GameInterfaceEditor_ProcessInput(instance->interface_editor, input);
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
    case GAME_HANDLE_KIND_INTERFACE_EDITOR:
        if( instance->interface_editor )
            GameInterfaceEditor_FrameBegin(instance->interface_editor, cycles_elapsed);
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
    case GAME_HANDLE_KIND_INTERFACE_EDITOR:
        if( instance->interface_editor &&
            GameInterfaceEditor_FrameNextCommand(instance->interface_editor, command) )
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
    case GAME_HANDLE_KIND_INTERFACE_EDITOR:
        if( instance->interface_editor )
            GameInterfaceEditor_FrameEnd(instance->interface_editor);
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

struct LibToriRS_LegacyTaskAdapter
{
    struct LibToriRS_Task base;
    void* state;
    int (*run_fn)(void*, struct LibToriRS_IOContext*);
    void (*destroy_fn)(void*);
};

static int
libtorirs_legacy_task_run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct LibToriRS_LegacyTaskAdapter* adapter =
        LibToriRS_container_of(base, struct LibToriRS_LegacyTaskAdapter, base);
    return adapter->run_fn(adapter->state, ctx);
}

static void
libtorirs_legacy_task_free(struct LibToriRS_Task* base)
{
    struct LibToriRS_LegacyTaskAdapter* adapter =
        LibToriRS_container_of(base, struct LibToriRS_LegacyTaskAdapter, base);
    if( adapter->destroy_fn )
        adapter->destroy_fn(adapter->state);
    free(adapter);
}

static struct LibToriRS_TaskVTable g_legacy_task_vtable = {
    .run_fn = libtorirs_legacy_task_run,
    .free_fn = libtorirs_legacy_task_free,
};

void
LibToriRS_TasksAdd(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_Task* task)
{
    if( !instance || !task )
        return;
    LibToriRS_TaskRunner_Add(&instance->task_runner, task);
}

void
LibToriRS_TasksAddLegacy(
    struct LibToriRS_Instance* instance,
    void* task_state,
    int (*run_fn)(void*, struct LibToriRS_IOContext*),
    void (*destroy_fn)(void*))
{
    struct LibToriRS_LegacyTaskAdapter* adapter = NULL;

    if( !instance )
        return;

    adapter = calloc(1, sizeof(struct LibToriRS_LegacyTaskAdapter));
    if( !adapter )
        return;

    adapter->base.vtable = &g_legacy_task_vtable;
    adapter->state = task_state;
    adapter->run_fn = run_fn;
    adapter->destroy_fn = destroy_fn;
    LibToriRS_TaskRunner_Add(&instance->task_runner, &adapter->base);
}

bool
LibToriRS_TasksRun(struct LibToriRS_Instance* instance)
{
    if( !instance )
        return false;
    return LibToriRS_TaskRunner_Run(&instance->task_runner);
}

bool
LibToriRS_TasksHasLive(struct LibToriRS_Instance* instance)
{
    if( !instance )
        return false;
    return instance->task_runner.count > 0;
}