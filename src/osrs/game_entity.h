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

struct EntityDrawPosition
{
    int x;
    int z;
    int height;
};

struct EntityPosition
{
    int x;
    int z;
    int height;
};

struct EntityAction
{
    int code;
    char name[32];
};

struct EntityFacing
{
    int entity_id;
};

struct EntitySceneElement
{
    int element_id;
};

struct EntityMinimapElement
{
    int element_id;
};

struct EntitySceneCoord
{
    int sx;
    int sz;
    int slevel;
};

struct EntityDebugKey
{
    char name[32];
    char svalue[32];
    int ivalue;
    float fvalue;
};

struct EntityOrientation
{
    int yaw;
    int dst_yaw;
    int face_entity; /* Client.ts: -1 = none; < 32768 = npc id; >= 32768 = player index + 32768 */
    /* Client.ts FACESQUARE: world tile coords, cleared after use */
    int face_square_x;
    int face_square_z;
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

    struct EntityPathing pathing;
    struct EntitySceneElement scene_element2;
    struct EntityDrawPosition draw_position;

    // Deprecated below
    struct EntityPosition position;
    struct EntityOrientation orientation;
    struct EntityAnimation animation;

    /* Client.ts: primary from packet (attack/spell), secondary from movement */
    void* scene_element;
    int npc_type_id; /* CacheDatConfigNpc id for name/options */
    int size_x;
    int size_z;
    int primary_anim;
    int primary_anim_frame;
    int primary_anim_cycle;
    int primary_anim_delay;
    int primary_anim_loop;
    int secondary_anim;
    int secondary_anim_frame;
    int secondary_anim_cycle;
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
    struct PlayerAppearanceSlots appearance;
    struct EntityPathing pathing;
    struct EntitySceneElement scene_element2;
    struct EntityDrawPosition draw_position;

    // Deprecated below
    void* scene_element;

    struct EntityPosition position;
    struct EntityOrientation orientation;
    /* Client.ts: primary from packet (attack/spell), secondary from movement */
    int primary_anim;
    int primary_anim_frame;
    int primary_anim_cycle;
    int primary_anim_delay;
    int primary_anim_loop;
    int secondary_anim;
    int secondary_anim_frame;
    int secondary_anim_cycle;
    struct EntityAnimation animation;

    /* Client.ts: damage/health for hitsplat and health bar */
    int damage_values[ENTITY_DAMAGE_SLOTS];
    int damage_types[ENTITY_DAMAGE_SLOTS];
    int damage_cycles[ENTITY_DAMAGE_SLOTS];
    int combat_cycle;
    int health;
    int total_health;
};

struct MapBuildLocEntity
{
    struct EntitySceneElement scene_element;
    struct EntitySceneElement scene_element_two;
    struct EntitySceneCoord scene_coord;
    struct EntityAction action[10];
};

struct MapBuildTileEntity
{
    struct EntitySceneElement scene_element;
    struct EntitySceneCoord scene_coord;
};

void
entity_add_hitmark(
    int* damage_values,
    int* damage_types,
    int* damage_cycles,
    int loop_cycle,
    int damage_type,
    int damage_value);

#endif