#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_NPC_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_NPC_H

#include "../rsbuffer.h"
#include "../rscache_profile.h"

#include <stdbool.h>

/**
 * Sourced from Runelite!
 *
 */
struct RSCache_Dat2ConfigNpc
{
    int* models;
    int models_count;
    char* name;
    int size;
    int standing_animation;
    int walking_animation;
    int idle_rotate_left_animation;
    int idle_rotate_right_animation;
    int rotate180_animation;
    int rotate_left_animation;
    int rotate_right_animation;
    char* actions[5]; // Options 30-34
    short* recolor_to_find;
    short* recolor_to_replace;
    int recolor_count;
    short* retexture_to_find;
    short* retexture_to_replace;
    int retexture_count;
    int* chathead_models;
    int chathead_models_count;
    bool is_minimap_visible;
    int combat_level;
    int width_scale;
    int height_scale;
    bool has_render_priority;
    int ambient;
    int contrast;
    int* head_icon_archive_ids;
    short* head_icon_sprite_index;
    int head_icon_count;
    int rotation_speed;
    int varbit_id;
    int varp_index;
    int* configs;
    int configs_count;
    bool is_interactable;
    bool rotation_flag;
    bool is_pet;
    int run_animation;
    int run_rotate180_animation;
    int run_rotate_left_animation;
    int run_rotate_right_animation;
    int crawl_animation;
    int crawl_rotate180_animation;
    int crawl_rotate_left_animation;
    int crawl_rotate_right_animation;
    bool low_priority_follower_ops;
    int height;
    int category;
    int stats[6]; // Stats for opcodes 74-79
    struct RSCache_Params params;

    /** Bytes consumed by the last decode, set when the terminating opcode 0 is
     *  reached. Same diagnostic the loc decoder carries: comparing it against
     *  the file size detects field misalignment, because a decoder reading the
     *  wrong shape almost never lands exactly on the terminator. Zero means the
     *  decode bailed before the terminator. */
    int _consumed;
};

/**
 * Opcode 102 carries a head-icon *bitfield* plus a bigsmart/ushortsmart pair per
 * set bit from game revision 210; before that it was a single u16 sprite index.
 *
 * This flag was the point of the whole profile exercise. The decoder used to
 * compute the era correctly at function scope and then shadow it inside case 102
 * with a hardcoded `bool rev210_head_icons = true; // TODO: Make this
 * configurable`, leaving the computed value unused — which the compiler had been
 * reporting all along as an unused variable, invisible because src/makefile
 * builds rscache with -w. Any pre-210 dat2 cache decoded opcode 102 with the
 * wrong shape and misaligned every field after it.
 *
 * Verified by exact consumption (decode every record under both shapes and count
 * how many land exactly on their terminating opcode 0 — see _consumed below):
 *
 *   cache          records  op102   exact:modern   exact:old
 *   cache            14205   2004      **14205**       14196
 *   cache.jan2026    15535   2177      **15535**       15526
 *   cache.kronos      9326   1296          9311    **9326**
 *   cache.osrs184     9306   1298          9291    **9306**
 *   cache.osrs230    14205   2004      **14205**       14196
 *   cache.osrs239    16292   2269          2462        2462
 *
 * So the gate is real: the two pre-210 caches only reach 100% with the old shape,
 * and the modern caches only with the new one. The hardcoded `true` was silently
 * misdecoding 15 records in each pre-210 cache.
 *
 * cache.osrs239 reaches 100% under *neither* shape (2462/16292 both ways, so not
 * this gate). Rev 239 npc records carry some further change this decoder does not
 * yet know about — a pre-existing gap, unrelated to the head-icon flag, and not
 * reached by any shipped manifest.
 */
#define RSCACHE_CONFIG_NPC_DECODE_REV210_HEAD_ICONS 1

/** Archive revision at which the head-icon bitfield appeared (game rev 210).
 *  RuneLite's NpcLoader gates the same field on the same value. */
#define RSCACHE_NPC_ARCHIVE_REV_210 1493

/** Era payload flags for this cache. */
int
RSCache_Dat2ConfigNpcFlags(const struct RSCache* cache);

struct RSCache_Dat2ConfigNpc*
RSCache_Dat2ConfigNpcNewDecodeProfile(
    const struct RSCache* cache,
    char* data,
    int data_size);

/** Retained entry point: decode knowing only the npc group's archive revision. */
struct RSCache_Dat2ConfigNpc*
RSCache_Dat2ConfigNpcNewDecode(int revision, char* buffer, int buffer_size);
void
RSCache_Dat2ConfigNpcFree(struct RSCache_Dat2ConfigNpc* npc);

void
RSCache_Dat2ConfigNpcPrint(const struct RSCache_Dat2ConfigNpc* npc);

#endif