#ifndef RSPROT_INFO_BLOCKS_H
#define RSPROT_INFO_BLOCKS_H

/*
 * The extended-info blocks of PLAYER_INFO and NPC_INFO: one reader per block
 * SHAPE, shared by every version of both streams.
 *
 * A shape's fields come in the same order at every revision 223-241, so each
 * reader below is straight-line C. What a
 * revision changes is how each field is ENCODED -- g2 or g2Alt1 or g2Alt3 --
 * and that is the block's encoding struct: one read function per field, filled
 * in by the revision's constants (<stream>_osrs<N>.c).
 *
 * Every reader writes a BLOCK op, then the ops for its block (rsprot_info.h),
 * and returns 0 -- with what it wrote retracted and a STOP written -- when the
 * block did not read to its end. The conversions from wire values to the
 * executor's (65535 is -1, the facing block's kinds, a pathing entity's
 * 0x10000 + player as 32768 + player) are the reference client's, as the
 * hand-written 239 and 241 readers had them.
 */

#include "rsprot_info.h"
#include "rsprot_info_internal.h"

/** One scalar read: rsprot_g1, rsprot_g2_alt1, rsprot_gsmart1or2, ... */
typedef int32_t (*RsprotRead)(RsprotBuf *b);

/** What every block reader is handed: the cursor, the op list, and the entity
 *  whose flag word is being read. */
typedef struct RsprotInfoBlocks
{
    RsprotBuf *b;
    RsprotInfoList *out;
    int who;
    uint32_t flag;
    /* PLAYER_INFO only: the slot table and this packet's movement ops, which
     * the traversal and exact-move blocks amend. */
    RsprotPlayerPacket *player;
    /* NPC_INFO only: the type lookup CUSTOM_TYPED needs (NULL ends the tail). */
    RsprotNpcTypeLengths lengths;
    void *ctx;
} RsprotInfoBlocks;

/* --- encodings ------------------------------------------------------------ */

/** MOVE_SPEED, TEMP_MOVE_SPEED, RESET, TRANSFORMATION, LEVEL_CHANGE,
 *  ENABLED_OPS: one field. */
typedef struct RsprotInfoValueEnc
{
    RsprotRead value;
} RsprotInfoValueEnc;

typedef struct RsprotInfoSequenceEnc
{
    RsprotRead id;
    RsprotRead delay;
} RsprotInfoSequenceEnc;

typedef struct RsprotInfoFacePathingEntityEnc
{
    RsprotRead index;
    RsprotRead index_hi;
} RsprotInfoFacePathingEntityEnc;

/** The player's FACE_ANGLE (227-238): an angle. */
typedef struct RsprotInfoFaceAngleEnc
{
    RsprotRead angle;
} RsprotInfoFaceAngleEnc;

/** The npc's FACE_ANGLE (231-238): an angle and whether it is instant. */
typedef struct RsprotInfoFaceAngleInstantEnc
{
    RsprotRead angle;
    RsprotRead instant;
} RsprotInfoFaceAngleInstantEnc;

typedef struct RsprotInfoFaceCoordEnc
{
    RsprotRead x;
    RsprotRead z;
    RsprotRead instant;
} RsprotInfoFaceCoordEnc;

typedef struct RsprotInfoFacingEnc
{
    RsprotRead flag;
    RsprotRead entity_type;
    RsprotRead index;
    RsprotRead fallback_angle;
    RsprotRead x;
    RsprotRead z;
    RsprotRead size;
    RsprotRead angle;
} RsprotInfoFacingEnc;

typedef struct RsprotInfoChatEnc
{
    RsprotRead colour_effects;
    RsprotRead mod_icon;
    RsprotRead autotyper;
    RsprotRead data_len;
    uint8_t data_order; /* RsprotInfoPrim DATA* */
    RsprotRead pattern;
} RsprotInfoChatEnc;

typedef struct RsprotInfoExactMoveEnc
{
    RsprotRead dx1;
    RsprotRead dz1;
    RsprotRead dx2;
    RsprotRead dz2;
    RsprotRead delay1;
    RsprotRead delay2;
    RsprotRead direction;
} RsprotInfoExactMoveEnc;

/** 227-240's spot anims. */
typedef struct RsprotInfoSpotanimEnc
{
    RsprotRead count;
    RsprotRead slot;
    RsprotRead id;
    RsprotRead height_delay;
} RsprotInfoSpotanimEnc;

/** 223-224's npc spot anim: one, with no count or slot (slot 0). */
typedef struct RsprotInfoSpotanimSingleEnc
{
    RsprotRead id;
    RsprotRead height_delay;
} RsprotInfoSpotanimSingleEnc;

/** 240+: each spot anim says whether it loops. */
typedef struct RsprotInfoSpotanimLoopEnc
{
    RsprotRead count;
    RsprotRead slot;
    RsprotRead id;
    RsprotRead height_delay;
    RsprotRead loop;
} RsprotInfoSpotanimLoopEnc;

/** 227-237: hits and headbars in one block. `type`, `delay` and `value` are
 *  read in each hit form, and `type` again for each bar, with one encoding. */
typedef struct RsprotInfoHitsEnc
{
    RsprotRead count;
    RsprotRead type;
    RsprotRead delay;
    RsprotRead main_type;
    RsprotRead value;
    RsprotRead soak_type;
    RsprotRead soak_value;
    RsprotRead count2;
    RsprotRead end_time;
    RsprotRead start_time;
    RsprotRead start_fill;
    RsprotRead end_fill;
} RsprotInfoHitsEnc;

typedef struct RsprotInfoHitmarksEnc
{
    RsprotRead count;
    RsprotRead type;
    RsprotRead value;
    RsprotRead delay;
    RsprotRead limit;
} RsprotInfoHitmarksEnc;

typedef struct RsprotInfoHeadbarsEnc
{
    RsprotRead count;
    RsprotRead type;
    RsprotRead end_time;
    RsprotRead start_time;
    RsprotRead start_fill;
    RsprotRead end_fill;
} RsprotInfoHeadbarsEnc;

typedef struct RsprotInfoTintingEnc
{
    RsprotRead start;
    RsprotRead end;
    RsprotRead hue;
    RsprotRead saturation;
    RsprotRead lightness;
    RsprotRead weight;
} RsprotInfoTintingEnc;

typedef struct RsprotInfoTransparencyEnc
{
    RsprotRead start;
    RsprotRead end;
    RsprotRead alpha_start;
    RsprotRead alpha_end;
    RsprotRead use_start;
} RsprotInfoTransparencyEnc;

typedef struct RsprotInfoFreezeEnc
{
    RsprotRead delay;
    RsprotRead duration;
    RsprotRead cancel;
} RsprotInfoFreezeEnc;

/** A length, then the body in the clear at its byte order. */
typedef struct RsprotInfoAppearanceEnc
{
    RsprotRead data_len;
    uint8_t data_order; /* RsprotInfoPrim DATA* */
} RsprotInfoAppearanceEnc;

typedef struct RsprotInfoBasChangeEnc
{
    RsprotRead flag;
    RsprotRead anims[15]; /* bit n of the mask is anims[n] */
} RsprotInfoBasChangeEnc;

/** BodyCustomisation / HeadCustomisation (V1, V2): the recolour and retexture
 *  arrays take their length from the npc type. A two-byte model id of 0xFFFF
 *  is none; `model_none` is that value, -1 where the id is four bytes. */
typedef struct RsprotInfoCustomTypedEnc
{
    RsprotRead flag;
    RsprotRead model_count;
    RsprotRead model;
    int32_t model_none;
    RsprotRead recolour;
    RsprotRead retexture;
    RsprotRead mirror;
} RsprotInfoCustomTypedEnc;

/** BodyCustomisationV3 (counted arrays), and V4, which adds a recolour-all
 *  form ahead of the recolour list. */
typedef struct RsprotInfoCustomEnc
{
    RsprotRead flag;
    RsprotRead model_count;
    RsprotRead model;
    RsprotRead recol_all_flag; /* V4 */
    RsprotRead recol_all;      /* V4 */
    RsprotRead recolour_count;
    RsprotRead recolour;
    RsprotRead retexture_count;
    RsprotRead retexture;
    RsprotRead body_type;
    RsprotRead identkit_count;
    RsprotRead identkit;
} RsprotInfoCustomEnc;

typedef struct RsprotInfoHeadiconCustomEnc
{
    RsprotRead flag;
    RsprotRead group;
    RsprotRead index;
} RsprotInfoHeadiconCustomEnc;

/* --- the flag word and the readers ----------------------------------------- */

/**
 * Read `who`'s flag word: one byte, and one more for each continuation bit in
 * `ext` (0 ends it) that the word carries. An `illegal` bit is RSProx's throw
 * and stops the read; a bit no block names reads nothing, as in RSProx, whose
 * dispatch tests only the bits it names (227 sends 0x4000 with no bytes behind
 * it). Readies `t` for the blocks. Returns 0 on a stop.
 */
int rsprot_info_flags(RsprotInfoBlocks *t, int who, const uint32_t ext[3], uint32_t illegal);

/* Each returns 1, or 0 with a STOP written. `bit` is the block's flag. */
int rsprot_info_say(RsprotInfoBlocks *t, uint32_t bit);
int rsprot_info_name_change(RsprotInfoBlocks *t, uint32_t bit);
int rsprot_info_name_extras(RsprotInfoBlocks *t, uint32_t bit);
int rsprot_info_move_speed(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoValueEnc *e);
int rsprot_info_temp_move_speed(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoValueEnc *e);
int rsprot_info_reset(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoValueEnc *e);
int rsprot_info_transformation(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoValueEnc *e);
int rsprot_info_level_change(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoValueEnc *e);
int rsprot_info_enabled_ops(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoValueEnc *e);
int rsprot_info_sequence(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoSequenceEnc *e);
int rsprot_info_face_pathing_entity(
    RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoFacePathingEntityEnc *e);
int rsprot_info_face_angle(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoFaceAngleEnc *e);
int rsprot_info_face_angle_instant(
    RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoFaceAngleInstantEnc *e);
int rsprot_info_face_coord(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoFaceCoordEnc *e);
int rsprot_info_facing(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoFacingEnc *e);
int rsprot_info_chat(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoChatEnc *e);
int rsprot_info_chat_old(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoChatEnc *e);
int rsprot_info_spotanim_single(RsprotInfoBlocks *t, uint32_t bit,
                                const RsprotInfoSpotanimSingleEnc *e);
int rsprot_info_exact_move(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoExactMoveEnc *e);
/** The player's: the move this slot made this cycle is not applied
 *  (class109.method3804 clears field1582 after method3132). */
int rsprot_info_player_exact_move(
    RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoExactMoveEnc *e);
int rsprot_info_spotanim(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoSpotanimEnc *e);
int rsprot_info_spotanim_loop(
    RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoSpotanimLoopEnc *e);
int rsprot_info_hits(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoHitsEnc *e);
int rsprot_info_hitmarks(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoHitmarksEnc *e);
int rsprot_info_headbars(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoHeadbarsEnc *e);
int rsprot_info_tinting(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoTintingEnc *e);
int rsprot_info_transparency(
    RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoTransparencyEnc *e);
int rsprot_info_freeze(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoFreezeEnc *e);
int rsprot_info_appearance(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoAppearanceEnc *e);
int rsprot_info_bas_change(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoBasChangeEnc *e);
int rsprot_info_custom_typed(
    RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoCustomTypedEnc *e);
int rsprot_info_head_custom_typed(
    RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoCustomTypedEnc *e);
int rsprot_info_custom_v3(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoCustomEnc *e);
int rsprot_info_custom_v4(RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoCustomEnc *e);
int rsprot_info_headicon_custom(
    RsprotInfoBlocks *t, uint32_t bit, const RsprotInfoHeadiconCustomEnc *e);

#endif
