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

#define ENTITY_DAMAGE_SLOTS 4

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

    /* Client.ts: damage/health for hitsplat and health bar */
    int damage_values[ENTITY_DAMAGE_SLOTS];
    int damage_types[ENTITY_DAMAGE_SLOTS];
    int damage_cycles[ENTITY_DAMAGE_SLOTS];
    int combat_cycle;
    int health;
    int total_health;
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
    int primary_anim;
    int primary_anim_delay;
    int secondary_anim;
    struct EntityAnimation animation;

    struct PlayerAppearanceSlots appearance;
    struct EntityPathing pathing;

    /* Client.ts: damage/health for hitsplat and health bar */
    int damage_values[ENTITY_DAMAGE_SLOTS];
    int damage_types[ENTITY_DAMAGE_SLOTS];
    int damage_cycles[ENTITY_DAMAGE_SLOTS];
    int combat_cycle;
    int health;
    int total_health;
};

/* Client.ts addHitmark: add damage to first expired slot; display for 70 cycles */
void
entity_add_hitmark(
    int* damage_values,
    int* damage_types,
    int* damage_cycles,
    int loop_cycle,
    int damage_type,
    int damage_value);

#endif