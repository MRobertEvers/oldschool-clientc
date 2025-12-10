#ifndef GTASK_INIT_SCENE_H
#define GTASK_INIT_SCENE_H

#include "osrs/gio.h"

struct GTaskInitScene
{
    struct GIOQueue* io;
    int chunk_x;
    int chunk_y;
};

struct GTaskInitScene* gtask_init_scene_new(struct GIOQueue* io, int chunk_x, int chunk_y);
void gtask_init_scene_free(struct GTaskInitScene* task);

enum GTaskStatus gtask_init_scene_step(struct GTaskInitScene* task);

#endif