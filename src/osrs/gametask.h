#ifndef GTASK_H
#define GTASK_H

#include "libg.h"
#include "osrs/gametask_status.h"
#include "osrs/gio.h"

enum GameTaskKind
{
    GTASK_KIND_INIT_IO,
    GTASK_KIND_INIT_SCENE,
};

struct GTaskInitIO;
struct GTaskInitScene;
struct GameTask
{
    enum GameTaskStatus status;

    enum GameTaskKind kind;
    union
    {
        struct GTaskInitIO* _init_io;
        struct GTaskInitScene* _init_scene;
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

enum GameTaskStatus
gametask_step(struct GameTask* task);

void
gametask_free(struct GameTask* task);

#endif