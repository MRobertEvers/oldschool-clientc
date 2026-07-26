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

/**
 * The RS2 (643) branch, where four opcodes carry a different payload than in OldSchool.
 *
 * NpcType.decodeOpcode branches on `game === "oldschool"` for 114, 115, 122 and 123 — and the
 * two lineages disagree on *length*, not just meaning, so decoding 643 with the OldSchool
 * shape loses the record from the first one it meets:
 *
 *   | opcode | OldSchool                        | RS2                    | delta   |
 *   |--------|----------------------------------|------------------------|---------|
 *   | 114    | run seq (u16)                    | two shadow mods (2×u8) | 0 bytes |
 *   | 115    | four run seqs (4×u16)            | two bytes              | 6 bytes |
 *   | 122    | isFollower flag                  | hit-bar sprite (u16)   | 2 bytes |
 *   | 123    | lowPriorityFollowerOps flag      | icon height (u16)      | 2 bytes |
 *
 * It also forces the *pre*-210 head-icon shape for opcode 102 (a bare u16) regardless of
 * archive revision, because the reference gates that one on
 * `(oldschool && revision < 210) || runescape` — the bitfield form is an OldSchool-only
 * addition and an RS2 archive revision says nothing about it.
 */
#define RSCACHE_CONFIG_NPC_DECODE_RS2 2

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

/**
 * Encode an npc record.
 *
 * Takes a profile because opcode 102's *shape* is era dependent — see
 * RSCACHE_CONFIG_NPC_DECODE_REV210_HEAD_ICONS. Encoding with the wrong profile
 * produces a record the target client misreads, exactly as decoding with the wrong
 * one did.
 *
 * ## Why byte-exactness is low here (~0%, and only ~33% same-length)
 *
 * This decoder establishes **no reference defaults**: the struct is calloc'd, so
 * every unset field reads as 0. The reference client — and this library's own dat1
 * npc decoder, which does `npc->readyanim = -1` — defaults the animation, scale
 * and level fields to -1 instead.
 *
 * Two consequences:
 *
 *  1. **For this encoder**, "absent" and "present with value 0" are the same state,
 *     so a field explicitly written as 0 (combat_level 0 is common) gets omitted.
 *     Nothing is corrupted — a re-decode yields the same struct, which is why the
 *     semantic round trip is 100% — but the bytes are shorter than the original.
 *
 *  2. **Independently of encoding**, a dat2 npc with no opcode 13 gets
 *     standing_animation 0 rather than -1, and that value reaches the world as a
 *     sequence id (`info->idle->readyanim` in src/world/world_cycle.c). Asking for
 *     sequence 0 is not the same as asking for no animation, and dat1 and dat2
 *     therefore disagree about the same logical field.
 *
 * Point 2 is a pre-existing decoder gap, not an encoding concern, and fixing it
 * changes what the client renders — so it is recorded here rather than changed in
 * passing. Closing it would also let this encoder distinguish absent from zero and
 * raise byte-exactness substantially; the two changes belong together.
 *
 * Separately, opcodes 93, 107 and 109 *clear* flags (is_minimap_visible,
 * is_interactable, rotation_flag) that the decoder never sets true, so their
 * presence is unrecoverable. No client code reads those three fields today.
 */
uint32_t
RSCache_Dat2ConfigNpcEncodeProfile(
    const struct RSCache* cache,
    const struct RSCache_Dat2ConfigNpc* npc,
    uint8_t* out,
    uint32_t out_capacity);

void
RSCache_Dat2ConfigNpcFree(struct RSCache_Dat2ConfigNpc* npc);

void
RSCache_Dat2ConfigNpcPrint(const struct RSCache_Dat2ConfigNpc* npc);

#endif