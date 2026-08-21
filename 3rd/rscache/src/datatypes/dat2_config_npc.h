#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_NPC_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_NPC_H

#include "../rsbuffer.h"
#include "../rscache_profile.h"
#include "dat2_entity_ops.h"

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
    /**
     * Opcode 3 — the examine string.
     *
     * Retired from Jagex's own npc records in 2006: no record in cache.osrs239
     * states one (all 16,292 decode with the opcode unused), and the reference
     * client ignores the opcode without reading a payload. It is decoded and
     * encoded here because this tree's content pack *authors* examine text into
     * the npc archive, exactly as it already does for a loc (`desc`, the same
     * opcode) — the field is the historical native slot for the data, and a
     * record that carries one is only ever read back by this client.
     */
    char* desc;
    int size;
    int standing_animation;
    int walking_animation;
    int idle_rotate_left_animation;
    int idle_rotate_right_animation;
    int rotate180_animation;
    int rotate_left_animation;
    int rotate_right_animation;
    char* actions[5]; // Options 30-34
    /* Recolour/retexture pairs. These are 16-bit *unsigned* quantities — an HSL
     * word (hue<<10|sat<<7|lum) or a texture id — so they must not be stored in
     * a signed 16-bit field: every colour from 0x8000 up came back negative and
     * only round-tripped because both the encoder and the ToriRS adaptor
     * happened to cast back through `uint16_t`. `int` is what every other config
     * struct here uses for the same data. */
    int* recolor_to_find;
    int* recolor_to_replace;
    int recolor_count;
    int* retexture_to_find;
    int* retexture_to_replace;
    int retexture_count;
    int* chathead_models;
    int chathead_models_count;
    bool is_minimap_visible;
    int combat_level;
    int width_scale;
    int height_scale;
    bool has_render_priority;
    /** Opcode 111 under rev233+ sets this to 2; opcode 99 sets it to 1. */
    int render_priority;
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
    /**
     * Opcode 127 — BasType / render-animation id (RS2). -1 when absent.
     * When set, idle/walk come from the BasType rather than opcodes 13/14.
     */
    int bas_type_id;

    /* --- rev 231+ --- */
    /** Opcode 126. -1 means "unset" so the post-decode default can apply. */
    int footprint_size;
    /* --- rev 234+ --- */
    bool unknown1; /* opcode 129 */
    /* --- rev 235+ --- */
    bool can_hide_for_overlap; /* opcode 145 */
    int overlap_tint_hsl;      /* opcode 146; 0 when absent */
    /* --- rev 236+ --- */
    bool idle_anim_restart; /* opcode 130 */
    bool zbuf;              /* default true; opcode 147 clears */

    /*
     * Movement sounds, opcode 134.
     *
     * Four sound-effect ids, one per movement state, and the radius (in tiles)
     * within which they are audible. 65535 on the wire means "no sound for this
     * state" and is stored as -1, the same sentinel the rest of this struct
     * uses for an absent id.
     *
     * These were decoded and thrown away for a long time -- the byte layout was
     * needed so the rest of the record stayed aligned, but nothing kept the
     * values, so an NPC could never be heard moving.
     */
    int sound_idle;   /* opcode 134 */
    int sound_crawl;  /* opcode 134 */
    int sound_walk;   /* opcode 134 */
    int sound_run;    /* opcode 134 */
    int sound_radius; /* opcode 134, tiles */

    /** Opcode 140: volume scale for this npc's sounds, 0..255. 255 when absent. */
    int ambient_sound_volume;

    /** Rev 237+: sub-ops / conditional ops. Plain ops still live in `actions`. */
    struct RSCache_EntityOps entity_ops;

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

/** Rev 231+: opcode 126 footprintSize + post-decode default. */
#define RSCACHE_CONFIG_NPC_DECODE_REV231_FOOTPRINT 4
/** Rev 234+: opcode 129 unknown1. */
#define RSCACHE_CONFIG_NPC_DECODE_REV234_FLAGS 8
/** Rev 235+: opcodes 145/146 overlap fields (stored, not just consumed). */
#define RSCACHE_CONFIG_NPC_DECODE_REV235_OVERLAP 16
/** Rev 236+: opcodes 130 idleAnimRestart, 147 zbuf=false. */
#define RSCACHE_CONFIG_NPC_DECODE_REV236_FLAGS 32
/** Rev 237+: opcodes 61/62 int model arrays. */
#define RSCACHE_CONFIG_NPC_DECODE_REV237_INT_MODEL_IDS 64
/** Rev 237+: opcodes 251/252/253 EntityOps. */
#define RSCACHE_CONFIG_NPC_DECODE_REV237_ENTITY_OPS 128

/** Rev 233+: opcode 111 means renderPriority=2 instead of isFollower. */
#define RSCACHE_CONFIG_NPC_DECODE_REV233_OP111 256

/**
 * The RS2 build-669+ branch (rev 727 and its neighbours).
 *
 * Not a widening of the 643 shape — a different stream. Model ids in opcodes 1
 * and 60 became **varuint** (two bytes, or four when the top bit of the first is
 * set), so a model list only reads at the right length by accident and every
 * field after it lands in the wrong place; that alone is why 9,258 of 15,661
 * npc records in `cache.rs727_preeoc` stopped mid-record under the 643 codec.
 * Opcodes 106 and 118 also changed structure, and roughly thirty opcodes have
 * no 643 counterpart at all.
 *
 * Because the same opcode number means a different structure, this gets its own
 * codec version rather than a flag on the 643 body — the rule stated in
 * dat2_config_loc.h, applied here for the same reason.
 */
#define RSCACHE_CONFIG_NPC_DECODE_RS2_BUILD669 512

/** Archive revision at which the head-icon bitfield appeared (game rev 210).
 *  RuneLite's NpcLoader gates the same field on the same value. */
#define RSCACHE_NPC_ARCHIVE_REV_210 1493

/*
 * Codec versions.
 *
 * A field that merely got wider or gained a flag is absorbed by
 * RSCache_Dat2ConfigNpcFlags. A different *stream shape* gets a version here,
 * and a revision module pins it (see rev_dat2_rs727.c). The derivation below is
 * only the fallback for a cache nobody declared.
 */
#define RSCACHE_CODEC_NPC_OSRS 1
#define RSCACHE_CODEC_NPC_RS2 2
/** RS2 build 669+: varuint model ids and the later opcode set. */
#define RSCACHE_CODEC_NPC_RS2_BUILD669 3

/** Which npc codec this cache uses. */
int
RSCache_Dat2ConfigNpcCodecVersion(const struct RSCache* cache);

/** Era payload flags for this cache. */
int
RSCache_Dat2ConfigNpcFlags(const struct RSCache* cache);

/**
 * Set the type's defaults on an already-zeroed record.
 *
 * Over thirty fields, and not optional: `size` is 1, every animation is -1,
 * the scales are 128, `rotation_speed` is 32, `overlap_tint_hsl` is 39188 and
 * every stat is 1. A record decoded without this looks plausible and re-encodes
 * to different bytes, which is the only symptom it has.
 */
void
RSCache_Dat2ConfigNpcInit(struct RSCache_Dat2ConfigNpc* npc);

/**
 * Fix up the record once its stream is exhausted.
 *
 * `footprint_size` falls back to a value derived from `size`, which is only
 * knowable after the last opcode. Must run after decoding, by any path.
 */
void
RSCache_Dat2ConfigNpcFinish(struct RSCache_Dat2ConfigNpc* npc, unsigned flags);

/**
 * Handle one opcode, advancing `buffer`. True when consumed, false when unknown.
 *
 * The extension point a server-side npc record delegates through: embed this
 * struct at offset zero, call this first, and handle only what comes back false.
 * See `opcode_codec.h`.
 *
 * `flags` must come from `RSCache_Dat2ConfigNpcFlags` — opcode 102 changes shape
 * at rev 210, and six further era gates ride on the same word. Passing 0 decodes
 * a modern cache wrongly and silently.
 */
bool
RSCache_Dat2ConfigNpcDecodeOp(
    struct RSCache_Dat2ConfigNpc* npc,
    int opcode,
    struct RSCache_Buffer* buffer,
    unsigned flags);

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

/** An upper bound on what `RSCache_Dat2ConfigNpcEncodeProfile` will write. */
uint32_t
RSCache_Dat2ConfigNpcEncodeBound(const struct RSCache_Dat2ConfigNpc* npc);

/** Release what the record owns, leaving the struct itself to the caller.
 *  Split out of `Free` so a caller holding the record by value — as the codec
 *  interface does — can release it without the double free `Free` would cause. */
void
RSCache_Dat2ConfigNpcFreeInplace(struct RSCache_Dat2ConfigNpc* npc);

void
RSCache_Dat2ConfigNpcFree(struct RSCache_Dat2ConfigNpc* npc);

void
RSCache_Dat2ConfigNpcPrint(const struct RSCache_Dat2ConfigNpc* npc);

#endif