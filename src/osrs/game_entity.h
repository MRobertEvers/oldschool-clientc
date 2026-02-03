#ifndef GAME_ENTITY_H
#define GAME_ENTITY_H

#include <stdbool.h>
#include <stdint.h>

struct EntityPathing
{
    int route_length;
    int route_x[10];
    int route_z[10];
    int route_run[10];
};

struct EntityPosition
{
    int x;
    int z;

    int height;
};

struct EntityOrientation
{
    int yaw;
    int dst_yaw;
};

struct EntityAnimation
{
    int readyanim;
    int walkanim;
    int turnanim;
    int runanim;
    int walkanim_b;
    int walkanim_r;
    int walkanim_l;
};

struct NPCEntity
{
    int alive;
    void* scene_element;
    int size_x;
    int size_z;
    struct EntityPosition position;
    struct EntityOrientation orientation;
    struct EntityAnimation animation;
    struct EntityPathing pathing;

    int curranim;
    void* scene_readyanim;
    void* scene_walkanim;
    void* scene_runanim;
    void* scene_walkanim_b;
    void* scene_walkanim_r;
    void* scene_walkanim_l;
};

struct PlayerAppearanceSlots
{
    int slots[12];
    int colors[5];
};

struct PlayerEntity
{
    int alive;
    void* scene_element;
    struct EntityPosition position;
    struct EntityOrientation orientation;
    int curranim;
    struct EntityAnimation animation;

    struct PlayerAppearanceSlots appearance;
    struct EntityPathing pathing;
};

#endif