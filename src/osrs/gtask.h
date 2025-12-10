#ifndef GTASK_H
#define GTASK_H

#include "osrs/gio.h"

enum GTaskStatus
{
    GTASK_STATUS_PENDING,
    GTASK_STATUS_RUNNING,
    GTASK_STATUS_COMPLETED,
    GTASK_STATUS_FAILED,
};

enum GTaskKind
{
    GTASK_KIND_INIT_IO,
    GTASK_KIND_INIT_SCENE,
};

struct GTaskInitIO;
struct GTaskInitScene;
struct GTask
{
    enum GTaskStatus status;

    enum GTaskKind kind;
    union
    {
        struct GTaskInitIO* _init_io;
        struct GTaskInitScene* _init_scene;
    };

    struct GTask* next;
};

struct GTask* gtask_new_init_io(struct GIOQueue* io);
struct GTask* gtask_new_init_scene(struct GIOQueue* io, int chunk_x, int chunk_y);

enum GTaskStatus gtask_step(struct GTask* task);

void gtask_free(struct GTask* task);

#endif