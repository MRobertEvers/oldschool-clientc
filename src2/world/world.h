#ifndef WORLD_H
#define WORLD_H

#include <stdint.h>

struct WorldScene;

struct WorldEntity_DrawPosition
{
    uint16_t x;
    uint16_t z;
    uint16_t height;
};

struct WorldEntity_Scenery
{
    struct WorldEntity_DrawPosition draw_position;
};

struct WorldEntity_Terrain
{
    int x;
};

struct World
{
    struct WorldScene* scene;

    struct WorldEntity_Scenery scenery[10];
    struct WorldEntity_Terrain terrain[10];
};

#endif
