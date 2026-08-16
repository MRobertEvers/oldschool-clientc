#ifndef RSCACHE_TOOLS_PORT_PLAN_H
#define RSCACHE_TOOLS_PORT_PLAN_H

#include "anim_affinity.h"
#include "asset_access.h"

#include <stdbool.h>
#include <stdint.h>

/** Named animation slots normalised across eras. */
enum Tool_AnimSlot
{
    TOOL_ANIM_IDLE = 0,
    TOOL_ANIM_WALK,
    TOOL_ANIM_WALK_BACK,
    TOOL_ANIM_WALK_LEFT,
    TOOL_ANIM_WALK_RIGHT,
    TOOL_ANIM_IDLE_ROTATE_LEFT,
    TOOL_ANIM_IDLE_ROTATE_RIGHT,
    TOOL_ANIM_RUN,
    TOOL_ANIM_RUN_ROTATE180,
    TOOL_ANIM_RUN_ROTATE_LEFT,
    TOOL_ANIM_RUN_ROTATE_RIGHT,
    TOOL_ANIM_CRAWL,
    TOOL_ANIM_CRAWL_ROTATE180,
    TOOL_ANIM_CRAWL_ROTATE_LEFT,
    TOOL_ANIM_CRAWL_ROTATE_RIGHT,
    TOOL_ANIM_SLOT_COUNT
};

struct Tool_NeutralNpc
{
    int source_id;
    char* name;
    char* desc;
    int size;
    int* models;
    int models_count;
    int* chathead_models;
    int chathead_models_count;
    int* recolor_to_find;
    int* recolor_to_replace;
    int recolor_count;
    int* retexture_to_find;
    int* retexture_to_replace;
    int retexture_count;
    char* actions[5];
    int combat_level;
    int width_scale;
    int height_scale;
    int ambient;
    int contrast;
    bool is_minimap_visible;
    bool is_interactable;
    bool rotation_flag;
    int rotation_speed;
    int category;
    int height;

    /* Animation set: presence is independent of value (dat2 calloc trap). */
    int anim[TOOL_ANIM_SLOT_COUNT];
    bool anim_present[TOOL_ANIM_SLOT_COUNT];
    int bas_type_id; /* source BasType id, or -1 */
    bool bas_present;
};

void
tool_neutral_npc_free(struct Tool_NeutralNpc* n);

int
tool_neutral_npc_from_dat2(
    struct Tool_Dat2Cache* c,
    const struct RSCache_Dat2ConfigNpc* npc,
    int npc_id,
    struct Tool_NeutralNpc* out);

int
tool_neutral_npc_from_dat1(
    const struct RSCache_Dat1ConfigNpc* npc,
    int npc_id,
    struct Tool_NeutralNpc* out);

/** Convert neutral NPC into a dat2 config for the destination profile. */
struct RSCache_Dat2ConfigNpc*
tool_neutral_npc_to_dat2(
    const struct Tool_NeutralNpc* n,
    const struct RSCache* dest_profile,
    int emit_bas,
    int bas_dest_id,
    char*** warnings,
    int* warning_count);

struct RSCache_Dat1ConfigNpc*
tool_neutral_npc_to_dat1(
    const struct Tool_NeutralNpc* n,
    char*** warnings,
    int* warning_count);

/* ---- Id remapping ------------------------------------------------------- */

struct Tool_IdMapEntry
{
    int source_id;
    int dest_id;
};

struct Tool_IdMap
{
    struct Tool_IdMapEntry* entries;
    int count;
    int capacity;
};

void
tool_id_map_init(struct Tool_IdMap* m);

void
tool_id_map_free(struct Tool_IdMap* m);

int
tool_id_map_put(
    struct Tool_IdMap* m,
    int source_id,
    int dest_id);

int
tool_id_map_lookup(
    const struct Tool_IdMap* m,
    int source_id,
    int* dest_id);

/**
 * Same-id-if-free: return source_id when free, else next free id at or above
 * source_id. `is_free(id, userdata)` returns non-zero when the id is available.
 */
int
tool_alloc_id(
    int source_id,
    int (*is_free)(int id, void* userdata),
    void* userdata);

/* ---- Port plan ---------------------------------------------------------- */

struct Tool_PortPlan
{
    struct Tool_NeutralNpc npc;
    int* related_seq_ids;
    int related_seq_count;

    struct Tool_IdMap model_map;
    struct Tool_IdMap chathead_map;
    struct Tool_IdMap seq_map;
    struct Tool_IdMap framemap_map;
    struct Tool_IdMap frame_archive_map; /* animations archive ids (dat2) */
    struct Tool_IdMap frame_id_map; /* dat2 (arch<<16)|file -> dat1 global u16 */
    struct Tool_IdMap bas_map;

    char** warnings;
    int warning_count;
};

void
tool_port_plan_free(struct Tool_PortPlan* p);

void
tool_port_plan_add_warning(
    struct Tool_PortPlan* p,
    const char* fmt,
    ...);

void
tool_port_plan_print(const struct Tool_PortPlan* p);

int
tool_port_plan_write_json(
    const struct Tool_PortPlan* p,
    const char* path);

#endif
