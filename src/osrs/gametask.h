#ifndef GAMETASK_H
#define GAMETASK_H

#include "osrs/game.h"
#include "osrs/gametask_status.h"
#include "osrs/gio.h"

enum GameTaskKind
{
    GAMETASK_KIND_INIT_IO,
    GAMETASK_KIND_INIT_SCENE,
    GAMETASK_KIND_INIT_SCENE_DAT,
    GAMETASK_KIND_LOAD_DAT,
};

struct TaskInitIO;
struct TaskInitScene;
struct TaskInitSceneDat;
struct TaskLoadDat;
struct GameTask
{
    enum GameTaskStatus status;

    enum GameTaskKind kind;
    union
    {
        struct TaskInitIO* _init_io;
        struct TaskInitScene* _init_scene;
        struct TaskInitSceneDat* _init_scene_dat;
        struct TaskLoadDat* _load_dat;
    };

    struct GameTask* next;
};

struct GameTask*
gametask_new_init_io(
    struct GGame* game,
    struct GIOQueue* io);

struct GameTask*
gametask_new_init_scene(
    struct GGame* game,
    int map_sw_x,
    int map_sw_z,
    int map_ne_x,
    int map_ne_z);

struct GameTask*
gametask_new_init_scene_dat(
    struct GGame* game,
    int map_sw_x,
    int map_sw_z,
    int map_ne_x,
    int map_ne_z);

struct GameTask*
gametask_new_load_dat(
    struct GGame* game,
    int* sequence_ids,
    int sequence_count,
    int* model_ids,
    int model_count);

enum GameTaskStatus
gametask_step(struct GameTask* task);

void
gametask_free(struct GameTask* task);

#endif