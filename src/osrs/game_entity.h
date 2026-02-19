#ifndef GAME_ENTITY_H
#define GAME_ENTITY_H

#include <stdbool.h>
#include <stdint.h>

struct EntitySize
{
    int x;
    int z;
};

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

    int primary_anim;
    int primary_anim_frame;
    int primary_anim_cycle;
    int primary_anim_delay;
    int primary_anim_loop;
    int secondary_anim;
    int secondary_anim_frame;
    int secondary_anim_cycle;
    int secondary_anim_delay;
    int secondary_anim_loop;
};

#define ENTITY_DAMAGE_SLOTS 4

struct NPCEntity
{
    int alive;

    struct EntityPathing pathing;
    struct EntitySize size;
    struct EntitySceneElement scene_element2;
    struct EntityDrawPosition draw_position;
    struct EntityOrientation orientation;
    struct EntityAnimation animation;

    // Deprecated below
    struct EntityPosition position;

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
    struct EntityAnimation animation;

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
    int entity_id;
    struct EntitySceneElement scene_element;
    struct EntitySceneElement scene_element_two;
    struct EntitySceneCoord scene_coord;
    struct EntityAction actions[10];
};

struct MapBuildTileEntity
{
    int entity_id;
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

enum EntityKind
{
    ENTITY_KIND_NONE,
    ENTITY_KIND_PLAYER = 1,
    ENTITY_KIND_NPC = 2,
    ENTITY_KIND_MAP_BUILD_LOC = 3,
    ENTITY_KIND_MAP_BUILD_TILE = 4,
};

static inline uint32_t
entity_unified_id(
    enum EntityKind kind,
    int entity_id)
{
    uint8_t kind_bits = kind << 28;
    return kind_bits | entity_id;
}

static inline enum EntityKind
entity_kind_from_uid(uint32_t unified_id)
{
    return (enum EntityKind)(unified_id >> 28);
}

static inline int
entity_id_from_uid(uint32_t unified_id)
{
    return unified_id & 0x0FFFFFFF;
}

#define PATHSTEP_RUN 1
#define PATHSTEP_WALK 0

void
entity_pathing_push_xz(
    struct EntityPathing* pathing,
    int x,
    int z,
    int step_type);

void
entity_pathing_push_step(
    struct EntityPathing* pathing,
    int step_type,
    int direction);

enum PathingJump
{
    PATHING_JUMP_TELEPORT,
    PATHING_JUMP_WALK,
};

enum PathingJump
entity_pathing_jump(
    struct EntityPathing* pathing,
    bool force_teleport,
    int x,
    int z);

void
entity_draw_position_set_to_tile(
    struct EntityDrawPosition* draw_position,
    int tile_x,
    int tile_z,
    int size_x,
    int size_z);

#endif