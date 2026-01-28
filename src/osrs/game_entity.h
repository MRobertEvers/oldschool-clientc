#ifndef GAME_ENTITY_H
#define GAME_ENTITY_H

#include <stdbool.h>
#include <stdint.h>

struct EntityPosition
{
    int sx;
    int sz;

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
    struct EntityAnimation animation;

    struct PlayerAppearanceSlots appearance;
};

#endif