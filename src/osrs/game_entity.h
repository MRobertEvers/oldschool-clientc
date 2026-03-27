#ifndef GAME_ENTITY_H
#define GAME_ENTITY_H

#include <stdbool.h>
#include <stdint.h>

struct EntitySprite
{
    int id;
};

struct EntitySize
{
    uint8_t x;
    uint8_t z;
};

struct EntityPathing
{
    uint8_t route_length;
    uint8_t route_x[10];
    uint8_t route_z[10];
    uint8_t route_run[10];
};

struct EntityDrawPosition
{
    int x;
    int z;
    int height;
};


struct EntityAction
{
    uint16_t code;
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
    uint16_t sx;
    uint16_t sz;
    uint8_t slevel;
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
    uint16_t yaw;
    uint16_t dst_yaw;
};

struct EntityName
{
    char name[32];
};

struct EntityDescription
{
    char description[64];
};
struct EntityAnimationStep
{
    uint16_t anim_id;
    uint8_t frame;
    uint8_t cycle;
    uint8_t delay;
    uint8_t loop;
};
struct EntityAnimation
{
    int16_t readyanim;
    int16_t walkanim;
    int16_t turnanim;
    int16_t runanim;
int16_t walkanim_b;
    int16_t walkanim_r;
    int16_t walkanim_l;

    struct EntityAnimationStep primary_anim;
    struct EntityAnimationStep secondary_anim;
};

struct EntityVisibleLevel
{
    uint8_t level;
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
    struct EntityDescription description;
    struct EntityName name;
    struct EntityVisibleLevel visible_level;
    struct EntityAction actions[10];
    int action_count;

    /* Client.ts: damage/health for hitsplat and health bar */
    uint8_t damage_values[ENTITY_DAMAGE_SLOTS];
    uint8_t damage_types[ENTITY_DAMAGE_SLOTS];
    uint8_t damage_cycles[ENTITY_DAMAGE_SLOTS];
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
    struct EntityOrientation orientation;
    struct EntityAnimation animation;
    struct EntityVisibleLevel visible_level;
    struct EntityName name;

    /* Client.ts: damage/health for hitsplat and health bar */
    uint8_t damage_values[ENTITY_DAMAGE_SLOTS];
    uint8_t damage_types[ENTITY_DAMAGE_SLOTS];
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
    struct EntityAnimation animation;
    struct EntityAnimation animation_two;
    struct EntitySceneCoord scene_coord;
    struct EntityAction* actions;
    char name[32];
    char description[64];
    uint8_t action_count;
    bool interactable;
};

struct MapBuildTileEntity
{
    int entity_id;
    struct EntitySceneElement scene_element;
    struct EntitySceneCoord scene_coord;
};

struct IFaceRedstoneEntity
{
    struct EntitySprite inactive_sprite;
    struct EntitySprite active_sprite;
};

struct IFaceMinimapEntity
{};

struct IFaceCrosshairEntity
{
    struct EntitySprite sprite;
};

struct IFaceCompassEntity
{
    struct EntitySprite sprite;
};

struct IFaceMinimenuEntity
{
    struct EntitySprite background_sprite;
};

struct IFaceSpriteEntity
{
    struct EntitySprite sprite;
};

struct IFaceOrbEntity
{
    struct EntitySprite active_sprite;
    struct EntitySprite inactive_sprite;
};

// Top Left
struct IFaceTooltipEntity
{};

struct IFaceInventoryTabsEntity
{};

void
entity_add_hitmark(
    uint8_t* damage_values,
    uint8_t* damage_types,
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
    uint32_t kind_bits = kind << 28;
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