#include "dat2_config_obj.h"

#include "dat2_entity_ops.h"

#include "../rsbuffer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

void
RSCache_Dat2ConfigObjInit(struct RSCache_Dat2ConfigObj* object);

/**
 * The RS2 build at which obj model ids became varuint.
 *
 * From rsmv's `src/opcodes/typedef.jsonc`: `item_modelid` is
 * `{">=670":"varuint", ">=0":"ushort"}`.
 */
#define REV_OBJ_RS2_VARUINT_MODELS 670

/** Record that the stream stated `field` (see RSCACHE_OBJ_FIELD_*). */
#define OBJ_SET(field) RSCache_PresenceSet(&object->present, RSCACHE_OBJ_FIELD_##field)
#define OBJ_SET_AT(first, index) RSCache_PresenceSet(&object->present, RSCACHE_OBJ_FIELD_##first + (index))

int
RSCache_Dat2ConfigObjCodecVersion(const struct RSCache* cache)
{
    int derived = RSCACHE_CODEC_OBJ_DEFAULT;
    if( RSCache_IsRs2Dat2(cache) &&
        RSCache_RevisionAtLeastRs2(
            cache,
            RSCACHE_TYPE_OBJ,
            REV_OBJ_RS2_VARUINT_MODELS,
            RSCACHE_GROUP_REVISION_UNKNOWN,
            false) )
        derived = RSCACHE_CODEC_OBJ_RS2_BUILD670;
    /* An explicit pin from the revision module wins. */
    return RSCache_CodecVersionOr(cache, RSCACHE_TYPE_OBJ, derived);
}

int
RSCache_Dat2ConfigObjFlags(const struct RSCache* cache)
{
    int flags = 0;

    assert(cache);
    /* The codec version decides the stream shape; the flag carries it into the
     * shared decoder body, so a revision module can pin it. */
    if( RSCache_Dat2ConfigObjCodecVersion(cache) == RSCACHE_CODEC_OBJ_RS2_BUILD670 )
        return RSCACHE_CONFIG_OBJ_DECODE_RS2_BUILD670;
    if( RSCache_Dat2ConfigObjCodecVersion(cache) == RSCACHE_CODEC_OBJ_RS2_634 )
        return RSCACHE_CONFIG_OBJ_DECODE_RS2_634;
    if( RSCache_Dat2ConfigObjCodecVersion(cache) == RSCACHE_CODEC_OBJ_RS2_530 )
        return RSCACHE_CONFIG_OBJ_DECODE_RS2_530;

    if( !RSCache_IsOsrs(cache) )
        return 0;

    if( RSCache_RevisionAtLeastOsrs(
            cache, RSCACHE_TYPE_OBJ, 237, RSCACHE_GROUP_REVISION_UNKNOWN, false) )
    {
        flags |= RSCACHE_CONFIG_OBJ_DECODE_REV237_ENTITY_OPS;
        flags |= RSCACHE_CONFIG_OBJ_DECODE_REV237_INT_MODEL_IDS;
    }
    if( RSCache_RevisionAtLeastOsrs(
            cache, RSCACHE_TYPE_OBJ, 238, RSCACHE_GROUP_REVISION_UNKNOWN, false) )
        flags |= RSCACHE_CONFIG_OBJ_DECODE_REV238_UNTRADEABLE;
    if( RSCache_RevisionAtLeastOsrs(
            cache, RSCACHE_TYPE_OBJ, 239, RSCACHE_GROUP_REVISION_UNKNOWN, false) )
        flags |= RSCACHE_CONFIG_OBJ_DECODE_REV239_STACKABLE2;

    return flags;
}

uint32_t
RSCache_Dat2ConfigObjEncode(
    const struct RSCache_Dat2ConfigObj* object,
    uint8_t* out,
    uint32_t out_capacity)
{
    return RSCache_Dat2ConfigObjEncodeFlags(object, 0, out, out_capacity);
}

uint32_t
RSCache_Dat2ConfigObjEncodeProfile(
    const struct RSCache* cache,
    const struct RSCache_Dat2ConfigObj* object,
    uint8_t* out,
    uint32_t out_capacity)
{
    assert(cache);
    return RSCache_Dat2ConfigObjEncodeFlags(
        object, RSCache_Dat2ConfigObjFlags(cache), out, out_capacity);
}

/*
 * A model id: the int form when the codec has one (rev 237+ packs every model
 * that way) or the id does not fit a u16, the u16 form otherwise. `offset` is
 * the trailing byte of the worn-model opcodes (23/45, 25/48), -1 for none.
 */
static void
obj_put_model(
    struct RSCache_Buffer* buffer,
    int flags,
    int narrow_opcode,
    int wide_opcode,
    int value,
    int offset)
{
    bool wide = (flags & RSCACHE_CONFIG_OBJ_DECODE_REV237_INT_MODEL_IDS) || value > 0xFFFF;

    p1(buffer, wide ? wide_opcode : narrow_opcode);
    if( wide )
        p4(buffer, value);
    else
        p2(buffer, value);
    if( offset >= 0 )
        p1(buffer, offset);
}

uint32_t
RSCache_Dat2ConfigObjEncodeFlags(
    const struct RSCache_Dat2ConfigObj* object,
    int flags,
    uint8_t* out,
    uint32_t out_capacity)
{
    assert(object);
    assert(out);

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    const struct RSCache_Presence* has = &object->present;

    /* Exactly the fields the record states (see `present`), whatever their
     * values: a field explicitly stated with the client's default is still
     * written, and one left at a non-default value by hand without its bit is a
     * caller bug, not something to guess at. */
#define OBJ_HAS(field) RSCache_PresenceHas(has, RSCACHE_OBJ_FIELD_##field)
#define OBJ_P_IF(field, opcode_value, write_body)                                                  \
    do                                                                                             \
    {                                                                                              \
        if( OBJ_HAS(field) )                                                                       \
        {                                                                                          \
            p1(&buffer, (opcode_value));                                                           \
            write_body;                                                                            \
        }                                                                                          \
    } while( 0 )

    if( OBJ_HAS(MODEL) )
        obj_put_model(&buffer, flags, 1, 44, object->inventory_model_id, -1);

    if( OBJ_HAS(NAME) )
    {
        assert(object->name);
        p1(&buffer, 2);
        pjstr(&buffer, object->name, RSCACHE_JSTR_TERMINATOR_NULL);
    }
    if( OBJ_HAS(DESC) )
    {
        assert(object->examine);
        p1(&buffer, 3);
        pjstr(&buffer, object->examine, RSCACHE_JSTR_TERMINATOR_NULL);
    }

    OBJ_P_IF(ZOOM2D, 4, p2(&buffer, object->zoom2d));
    OBJ_P_IF(XAN2D, 5, p2(&buffer, object->xan2d));
    OBJ_P_IF(YAN2D, 6, p2(&buffer, object->yan2d));
    OBJ_P_IF(XOF2D, 7, p2b(&buffer, object->offset_x2d));
    OBJ_P_IF(YOF2D, 8, p2b(&buffer, object->offset_y2d));
    /* Opcode 9 is a string the decoder discards, so it cannot be reproduced. */
    /* Stackable is one field with two opcodes: 11 says 1, 160 (below) says 2. An
     * RS2 0xA5 ("never stackable", 0) has no OldSchool opcode. */
    if( OBJ_HAS(STACKABLE) && object->stacking_behaviour == 1 )
        p1(&buffer, 11);
    OBJ_P_IF(COST, 12, p4(&buffer, object->cost));
    OBJ_P_IF(WEARPOS, 13, p1b(&buffer, object->wearpos_1));
    OBJ_P_IF(WEARPOS2, 14, p1b(&buffer, object->wearpos_2));
    if( (flags & RSCACHE_CONFIG_OBJ_DECODE_REV238_UNTRADEABLE) && OBJ_HAS(UNTRADEABLE) )
        p1(&buffer, 15);
    if( OBJ_HAS(MEMBERS) )
        p1(&buffer, 16);

    if( OBJ_HAS(MANWEAR) )
        obj_put_model(&buffer, flags, 23, 45, object->male_model_0, object->male_offset);
    if( OBJ_HAS(MANWEAR2) )
        obj_put_model(&buffer, flags, 24, 46, object->male_model_1, -1);
    if( OBJ_HAS(WOMANWEAR) )
        obj_put_model(&buffer, flags, 25, 48, object->female_model_0, object->female_offset);
    if( OBJ_HAS(WOMANWEAR2) )
        obj_put_model(&buffer, flags, 26, 49, object->female_model_1, -1);
    OBJ_P_IF(WEARPOS3, 27, p1(&buffer, object->wearpos_3));

    /* A hidden op reads NULL; the stream's own spelling of it is written back. */
    for( int i = 0; i < 5; i++ )
    {
        if( !RSCache_PresenceHas(has, RSCACHE_OBJ_FIELD_OP1 + i) )
            continue;
        const char* text = object->actions[i] ? object->actions[i] : object->hidden_actions[i];
        assert(text);
        p1(&buffer, 30 + i);
        pjstr(&buffer, text, RSCACHE_JSTR_TERMINATOR_NULL);
    }
    for( int i = 0; i < 5; i++ )
    {
        if( !RSCache_PresenceHas(has, RSCACHE_OBJ_FIELD_IOP1 + i) )
            continue;
        assert(object->if_actions[i]);
        p1(&buffer, 35 + i);
        pjstr(&buffer, object->if_actions[i], RSCACHE_JSTR_TERMINATOR_NULL);
    }

    if( OBJ_HAS(RECOL) )
    {
        p1(&buffer, 40);
        p1(&buffer, object->recolor_count);
        for( int i = 0; i < object->recolor_count; i++ )
        {
            p2(&buffer, object->recolors_from[i]);
            p2(&buffer, object->recolors_to[i]);
        }
    }
    if( OBJ_HAS(RETEX) )
    {
        p1(&buffer, 41);
        p1(&buffer, object->retexture_count);
        for( int i = 0; i < object->retexture_count; i++ )
        {
            p2(&buffer, object->retextures_from[i]);
            p2(&buffer, object->retextures_to[i]);
        }
    }

    OBJ_P_IF(SHIFT_CLICK_DROP, 42, p1b(&buffer, object->shift_click_drop_index));

    /* Opcode 43: one sub-op list per ground op it names, each terminated by a
     * zero index. A stated list may be empty. */
    for( int action = 0; action < 5; action++ )
    {
        if( !RSCache_PresenceHas(has, RSCACHE_OBJ_FIELD_SUBOP1 + action) )
            continue;
        p1(&buffer, 43);
        p1(&buffer, action);
        for( int sub = 0; object->sub_actions[action] && sub < 20; sub++ )
        {
            if( !object->sub_actions[action][sub] )
                continue;
            /* Indices are stored one-based; zero terminates the list. */
            p1(&buffer, sub + 1);
            pjstr(&buffer, object->sub_actions[action][sub], RSCACHE_JSTR_TERMINATOR_NULL);
        }
        p1(&buffer, 0);
    }

    OBJ_P_IF(GE_TRADEABLE, 65, (void)0);
    OBJ_P_IF(WEIGHT, 75, p2b(&buffer, object->weight));

    if( OBJ_HAS(MANWEAR3) )
        obj_put_model(&buffer, flags, 78, 47, object->male_model_2, -1);
    if( OBJ_HAS(WOMANWEAR3) )
        obj_put_model(&buffer, flags, 79, 50, object->female_model_2, -1);
    if( OBJ_HAS(MANHEAD) )
        obj_put_model(&buffer, flags, 90, 51, object->male_head_model, -1);
    if( OBJ_HAS(WOMANHEAD) )
        obj_put_model(&buffer, flags, 91, 53, object->female_head_model, -1);
    if( OBJ_HAS(MANHEAD2) )
        obj_put_model(&buffer, flags, 92, 52, object->male_head_model_2, -1);
    if( OBJ_HAS(WOMANHEAD2) )
        obj_put_model(&buffer, flags, 93, 54, object->female_head_model_2, -1);

    OBJ_P_IF(CATEGORY, 94, p2(&buffer, object->category));
    OBJ_P_IF(ZAN2D, 95, p2(&buffer, object->zan2d));
    OBJ_P_IF(CERTLINK, 97, p2(&buffer, object->noted_id));
    OBJ_P_IF(CERTTEMPLATE, 98, p2(&buffer, object->noted_template));

    for( int i = 0; i < 10; i++ )
    {
        if( !RSCache_PresenceHas(has, RSCACHE_OBJ_FIELD_COUNTOBJ1 + i) )
            continue;
        p1(&buffer, 100 + i);
        p2(&buffer, object->count_obj[i]);
        p2(&buffer, object->count_co[i]);
    }

    OBJ_P_IF(RESIZEX, 110, p2(&buffer, object->resize_x));
    OBJ_P_IF(RESIZEY, 111, p2(&buffer, object->resize_y));
    OBJ_P_IF(RESIZEZ, 112, p2(&buffer, object->resize_z));
    OBJ_P_IF(AMBIENT, 113, p1b(&buffer, object->ambient));
    /* The decoder multiplies the stored byte by 5, so divide going back out. */
    OBJ_P_IF(CONTRAST, 114, p1b(&buffer, object->contrast / 5));
    OBJ_P_IF(TEAM, 115, p1(&buffer, object->team));
    OBJ_P_IF(BOUGHTLINK, 139, p2(&buffer, object->bought_id));
    OBJ_P_IF(BOUGHTTEMPLATE, 140, p2(&buffer, object->bought_template_id));
    OBJ_P_IF(PLACEHOLDERLINK, 148, p2(&buffer, object->placeholder_id));
    OBJ_P_IF(PLACEHOLDERTEMPLATE, 149, p2(&buffer, object->placeholder_template_id));

    if( OBJ_HAS(STACKABLE) && object->stacking_behaviour == 2 )
        p1(&buffer, 160);

    /* Each of 200/201/202 carries one entry, so a stated list is a non-empty
     * one; the three are written together in list order. */
    if( (flags & RSCACHE_CONFIG_OBJ_DECODE_REV237_ENTITY_OPS) &&
        (OBJ_HAS(ENTITY_SUB_OPS) || OBJ_HAS(ENTITY_COND_OPS) || OBJ_HAS(ENTITY_COND_SUB_OPS)) )
    {
        RSCache_EntityOpsEncode(&object->entity_ops, &buffer, 30, 200, 201, 202);
    }

    if( OBJ_HAS(PARAMS) )
    {
        p1(&buffer, 249);
        pparams(&buffer, &object->params);
    }

#undef OBJ_P_IF
#undef OBJ_HAS

    p1(&buffer, 0);

    return buffer.position;
}

void
RSCache_Dat2ConfigObjInit(struct RSCache_Dat2ConfigObj* object)
{
    assert(object);
    memset(object, 0, sizeof(struct RSCache_Dat2ConfigObj));
    /* Client defaults below, and nothing stated: a decoded opcode sets its bit. */
    RSCache_PresenceReset(&object->present);

    object->name = malloc(5);
    assert(object->name);
    strcpy(object->name, "null");

    object->examine = NULL;

    object->resize_x = 128;
    object->resize_y = 128;
    object->resize_z = 128;
    object->xan2d = 0;
    object->yan2d = 0;
    object->zan2d = 0;
    object->cost = 1;

    object->tradeable = true;
    object->ge_tradeable = false;
    object->stacking_behaviour = 0;
    object->inventory_model_id = 0;
    object->wearpos_1 = -1;
    object->wearpos_2 = -1;
    object->wearpos_3 = -1;
    object->is_members = false;

    object->zoom2d = 2000;
    object->offset_x2d = 0;
    object->offset_y2d = 0;

    object->ambient = 0;
    object->contrast = 0;

    object->male_model_0 = -1;
    object->male_model_1 = -1;
    object->male_model_2 = -1;
    object->male_offset = 0;
    object->male_head_model = -1;
    object->male_head_model_2 = -1;

    object->female_model_0 = -1;
    object->female_model_1 = -1;
    object->female_model_2 = -1;
    object->female_offset = 0;
    object->female_head_model = -1;
    object->female_head_model_2 = -1;

    object->category = 0;
    object->noted_id = -1;
    object->noted_template = -1;
    object->team = 0;
    object->weight = 0;
    object->shift_click_drop_index = -2;
    object->bought_id = -1;
    object->bought_template_id = -1;
    object->item_type = 0;
    object->lend_id = -1;
    object->lend_template_id = -1;
    object->placeholder_id = -1;
    object->placeholder_template_id = -1;

    RSCache_EntityOpsInit(&object->entity_ops);
}

struct RSCache_Dat2ConfigObj*
RSCache_Dat2ConfigObjNewDecode(
    char* buffer,
    int buffer_size)
{
    struct RSCache_Dat2ConfigObj* object = malloc(sizeof(struct RSCache_Dat2ConfigObj));
    assert(object);
    RSCache_Dat2ConfigObjInit(object);
    RSCache_Dat2ConfigObjDecodeInplaceFlags(object, buffer, buffer_size, 0);
    return object;
}

struct RSCache_Dat2ConfigObj*
RSCache_Dat2ConfigObjNewDecodeProfile(
    const struct RSCache* cache,
    char* buffer,
    int buffer_size)
{
    struct RSCache_Dat2ConfigObj* object;

    assert(cache);
    object = malloc(sizeof(struct RSCache_Dat2ConfigObj));
    assert(object);
    RSCache_Dat2ConfigObjInit(object);
    RSCache_Dat2ConfigObjDecodeInplaceFlags(
        object, buffer, buffer_size, RSCache_Dat2ConfigObjFlags(cache));
    return object;
}

void
RSCache_Dat2ConfigObjFreeInplace(struct RSCache_Dat2ConfigObj* object)
{
    int i;

    if( !object )
        return;

    free(object->name);
    free(object->examine);
    free(object->recolors_from);
    free(object->recolors_to);
    free(object->retextures_from);
    free(object->retextures_to);
    for( i = 0; i < 5; i++ )
    {
        free(object->actions[i]);
        free(object->hidden_actions[i]);
        free(object->if_actions[i]);
        if( object->sub_actions[i] )
        {
            for( int j = 0; j < 20; j++ )
                free(object->sub_actions[i][j]);
            free(object->sub_actions[i]);
        }
    }
    RSCache_EntityOpsFreeInplace(&object->entity_ops);
    for( i = 0; i < object->params.count; i++ )
        free(object->params.values[i]);
    free(object->params.keys);
    free(object->params.values);
    free(object->params.kinds);
}

void
RSCache_Dat2ConfigObjFree(struct RSCache_Dat2ConfigObj* object)
{
    if( !object )
        return;
    RSCache_Dat2ConfigObjFreeInplace(object);
    free(object);
}

void
RSCache_Dat2ConfigObjDecodeInplace(
    struct RSCache_Dat2ConfigObj* object,
    char* data,
    int data_size)
{
    RSCache_Dat2ConfigObjDecodeInplaceFlags(object, data, data_size, 0);
}


/* ---- RS2 build 670+ (rev 727) ------------------------------------------- */

/*
 * A separate stream from the 643/OldSchool one. Sourced from rsmv's
 * `src/opcodes/items.jsonc` resolved at buildnr 727, and checked the only way a
 * layout can be: a config record ends with opcode 0 at exactly its file length,
 * so a wrong payload width anywhere makes a record miss its terminator. All
 * 24,803 obj records in `cache.rs727_preeoc` consume exactly under this table.
 *
 * Fields this struct already models are stored; the rest are consumed at the
 * right width and dropped, which is what keeps the record aligned.
 */

static void
obj_b670_read_string(
    struct RSCache_Buffer* buffer,
    char** slot)
{
    char* s = gcstring(buffer);
    if( !slot )
    {
        free(s);
        return;
    }
    free(*slot);
    *slot = s;
}

static void
obj_b670_read_pairs(
    struct RSCache_Buffer* buffer,
    int** out_from,
    int** out_to,
    int* out_count)
{
    int length = gushortsmart(buffer);

    free(*out_from);
    free(*out_to);
    *out_from = length > 0 ? malloc((size_t)length * sizeof(int)) : NULL;
    *out_to = length > 0 ? malloc((size_t)length * sizeof(int)) : NULL;
    *out_count = (*out_from && *out_to) ? length : 0;

    for( int i = 0; i < length; i++ )
    {
        int from = g2(buffer);
        int to = g2(buffer);
        if( *out_count )
        {
            (*out_from)[i] = from;
            (*out_to)[i] = to;
        }
    }
}

/* ---- RS2 rev 530 ------------------------------------------------------- */

/*
 * Exact wire table from 2009scape's ItemDefinition.parseDefinition. This is
 * deliberately a whole codec: opcodes 23/25 have no trailing offset byte,
 * opcode 42 is a counted byte array, and 96/121-130 do not exist in the modern
 * OldSchool body with these meanings.
 */
static bool
obj_decode_op_rs2_530(
    struct RSCache_Dat2ConfigObj* object,
    int opcode,
    struct RSCache_Buffer* buffer);

/* ---- RS2 rev 634 ------------------------------------------------------- */

/*
 * Exact wire table from the rev-634 client's own item decoder — `Class213`,
 * method1566, in the deobfuscated 634 tree (~/Documents/git_repos/634-client).
 *
 * Read against that method arm by arm, 634's table is rev 530's plus exactly
 * three opcodes, and the three are why the 530 body cannot read a 634 cache:
 *
 *   18   multi stack size, a u16 the client keeps and this struct does not
 *   132  quest ids: a byte count then that many u16s
 *   134  a single byte (pick-size shift in later builds)
 *
 * Everything else — 23/24/25/26 with no trailing type byte, opcode 42's counted
 * *byte* array, 96, the 100-109 stack pairs, 121-130 — the two clients read
 * identically, so those arms delegate rather than being copied and left to
 * drift. Opcode 96 is the one arm that had to be restated: the 634 client reads
 * it unsigned where 2009scape's 530 reads a signed byte.
 *
 * ## Not the build-670 body with narrower ids
 *
 * That was tried first, since 670's only *documented* change is varuint model
 * ids, and it consumes every record in `cache.void634` exactly. It is still
 * wrong: the 670 body reads opcode 42 as a smart count followed by byte *pairs*
 * and opcode 132's count as a smart. Neither shape occurs in this cache, so the
 * measurement could not see it — a reminder that exact consumption proves a
 * table is not contradicted, not that it is right. The client is the authority.
 */
static bool
obj_decode_op_rs2_634(
    struct RSCache_Dat2ConfigObj* object,
    int opcode,
    struct RSCache_Buffer* buffer)
{
    switch( opcode )
    {
    case 18: /* multi stack size */
        g2(buffer);
        return true;
    case 96: /* unsigned here, where the 530 client reads a signed byte */
        object->item_type = g1(buffer);
        OBJ_SET(RS2_ITEM_TYPE);
        return true;
    case 132: /* quest ids */
    {
        int count = g1(buffer);
        for( int i = 0; i < count; i++ )
            g2(buffer);
        return true;
    }
    case 134:
        object->shift_click_drop_index = g1(buffer);
        OBJ_SET(SHIFT_CLICK_DROP);
        return true;
    default:
        return obj_decode_op_rs2_530(object, opcode, buffer);
    }
}

static bool
obj_decode_op_rs2_530(
    struct RSCache_Dat2ConfigObj* object,
    int opcode,
    struct RSCache_Buffer* buffer)
{
    switch( opcode )
    {
    case 1: object->inventory_model_id = g2(buffer); OBJ_SET(MODEL); return true;
    case 2: obj_b670_read_string(buffer, &object->name); OBJ_SET(NAME); return true;
    case 3: obj_b670_read_string(buffer, &object->examine); OBJ_SET(DESC); return true;
    case 4: object->zoom2d = g2(buffer); OBJ_SET(ZOOM2D); return true;
    case 5: object->xan2d = g2(buffer); OBJ_SET(XAN2D); return true;
    case 6: object->yan2d = g2(buffer); OBJ_SET(YAN2D); return true;
    case 7: object->offset_x2d = g2b(buffer); OBJ_SET(XOF2D); return true;
    case 8: object->offset_y2d = g2b(buffer); OBJ_SET(YOF2D); return true;
    case 10: return true; /* unused, payload-free in the 530 client */
    case 11: object->stacking_behaviour = 1; OBJ_SET(STACKABLE); return true;
    case 12: object->cost = g4(buffer); OBJ_SET(COST); return true;
    case 16: object->is_members = true; OBJ_SET(MEMBERS); return true;

    case 23: object->male_model_0 = g2(buffer); OBJ_SET(MANWEAR); return true;
    case 24: object->male_model_1 = g2(buffer); OBJ_SET(MANWEAR2); return true;
    case 25: object->female_model_0 = g2(buffer); OBJ_SET(WOMANWEAR); return true;
    case 26: object->female_model_1 = g2(buffer); OBJ_SET(WOMANWEAR2); return true;

    case 30:
    case 31:
    case 32:
    case 33:
    case 34:
        obj_b670_read_string(buffer, &object->actions[opcode - 30]);
        OBJ_SET_AT(OP1, opcode - 30);
        return true;
    case 35:
    case 36:
    case 37:
    case 38:
    case 39:
        obj_b670_read_string(buffer, &object->if_actions[opcode - 35]);
        OBJ_SET_AT(IOP1, opcode - 35);
        return true;

    case 40:
    case 41:
    {
        int count = g1(buffer);
        int** from = opcode == 40 ? &object->recolors_from : &object->retextures_from;
        int** to = opcode == 40 ? &object->recolors_to : &object->retextures_to;
        int* stored = opcode == 40 ? &object->recolor_count : &object->retexture_count;
        free(*from);
        free(*to);
        *from = count ? malloc((size_t)count * sizeof(int)) : NULL;
        *to = count ? malloc((size_t)count * sizeof(int)) : NULL;
        *stored = (*from && *to) ? count : 0;
        for( int i = 0; i < count; i++ )
        {
            int a = g2(buffer);
            int b = g2(buffer);
            if( *stored )
            {
                (*from)[i] = a;
                (*to)[i] = b;
            }
        }
        if( opcode == 40 )
            OBJ_SET(RECOL);
        else
            OBJ_SET(RETEX);
        return true;
    }
    case 42:
    {
        int count = g1(buffer);
        for( int i = 0; i < count; i++ )
            g1(buffer);
        return true;
    }
    case 65: object->ge_tradeable = true; OBJ_SET(GE_TRADEABLE); return true;
    case 78: object->male_model_2 = g2(buffer); OBJ_SET(MANWEAR3); return true;
    case 79: object->female_model_2 = g2(buffer); OBJ_SET(WOMANWEAR3); return true;
    case 90: object->male_head_model = g2(buffer); OBJ_SET(MANHEAD); return true;
    case 91: object->female_head_model = g2(buffer); OBJ_SET(WOMANHEAD); return true;
    case 92: object->male_head_model_2 = g2(buffer); OBJ_SET(MANHEAD2); return true;
    case 93: object->female_head_model_2 = g2(buffer); OBJ_SET(WOMANHEAD2); return true;
    case 95: object->zan2d = g2(buffer); OBJ_SET(ZAN2D); return true;
    case 96: object->item_type = g1b(buffer); OBJ_SET(RS2_ITEM_TYPE); return true;
    case 97: object->noted_id = g2(buffer); OBJ_SET(CERTLINK); return true;
    case 98: object->noted_template = g2(buffer); OBJ_SET(CERTTEMPLATE); return true;

    case 100:
    case 101:
    case 102:
    case 103:
    case 104:
    case 105:
    case 106:
    case 107:
    case 108:
    case 109:
        object->count_obj[opcode - 100] = g2(buffer);
        object->count_co[opcode - 100] = g2(buffer);
        OBJ_SET_AT(COUNTOBJ1, opcode - 100);
        return true;
    case 110: object->resize_x = g2(buffer); OBJ_SET(RESIZEX); return true;
    case 111: object->resize_y = g2(buffer); OBJ_SET(RESIZEY); return true;
    case 112: object->resize_z = g2(buffer); OBJ_SET(RESIZEZ); return true;
    case 113: object->ambient = g1b(buffer); OBJ_SET(AMBIENT); return true;
    case 114: object->contrast = g1b(buffer) * 5; OBJ_SET(CONTRAST); return true;
    case 115: object->team = g1(buffer); OBJ_SET(TEAM); return true;
    case 121: object->lend_id = g2(buffer); OBJ_SET(RS2_LEND); return true;
    case 122: object->lend_template_id = g2(buffer); OBJ_SET(RS2_LEND_TEMPLATE); return true;

    case 125:
    case 126:
        g1(buffer); g1(buffer); g1(buffer);
        return true;
    case 127:
    case 128:
    case 129:
    case 130:
        g1(buffer); g2(buffer);
        return true;
    case 249:
        RSCache_BufferReadParams(buffer, &object->params);
        OBJ_SET(PARAMS);
        return true;
    default:
        return false;
    }
}

static bool
obj_decode_op_rs2_b670(
    struct RSCache_Dat2ConfigObj* object,
    int opcode,
    struct RSCache_Buffer* buffer)
{
    switch( opcode )
    {
    /* Every model field is a varuint here — the whole reason for this codec. */
    case 0x01:
        object->inventory_model_id = gvaruint(buffer);
        OBJ_SET(MODEL);
        return true;
    case 0x17: /* male model 0; the trailing type byte went away at build 502 */
        object->male_model_0 = gvaruint(buffer);
        OBJ_SET(MANWEAR);
        return true;
    case 0x18:
        object->male_model_1 = gvaruint(buffer);
        OBJ_SET(MANWEAR2);
        return true;
    case 0x19: /* female model 0 */
        object->female_model_0 = gvaruint(buffer);
        OBJ_SET(WOMANWEAR);
        return true;
    case 0x1A:
        object->female_model_1 = gvaruint(buffer);
        OBJ_SET(WOMANWEAR2);
        return true;
    case 0x4E:
        object->male_model_2 = gvaruint(buffer);
        OBJ_SET(MANWEAR3);
        return true;
    case 0x4F:
        object->female_model_2 = gvaruint(buffer);
        OBJ_SET(WOMANWEAR3);
        return true;
    case 0x5A:
        object->male_head_model = gvaruint(buffer);
        OBJ_SET(MANHEAD);
        return true;
    case 0x5B:
        object->female_head_model = gvaruint(buffer);
        OBJ_SET(WOMANHEAD);
        return true;
    case 0x5C:
        object->male_head_model_2 = gvaruint(buffer);
        OBJ_SET(MANHEAD2);
        return true;
    case 0x5D:
        object->female_head_model_2 = gvaruint(buffer);
        OBJ_SET(WOMANHEAD2);
        return true;

    case 0x02:
        obj_b670_read_string(buffer, &object->name);
        OBJ_SET(NAME);
        return true;
    case 0x03: /* buff effect, where OldSchool keeps the examine text */
        obj_b670_read_string(buffer, &object->examine);
        OBJ_SET(DESC);
        return true;

    case 0x1E:
    case 0x1F:
    case 0x20:
    case 0x21:
    case 0x22:
        obj_b670_read_string(buffer, &object->actions[opcode - 0x1E]);
        OBJ_SET_AT(OP1, opcode - 0x1E);
        return true;
    case 0x23:
    case 0x24:
    case 0x25:
    case 0x26:
    case 0x27:
        obj_b670_read_string(buffer, &object->if_actions[opcode - 0x23]);
        OBJ_SET_AT(IOP1, opcode - 0x23);
        return true;
    case 0xA4: /* combine shard name */
        obj_b670_read_string(buffer, NULL);
        return true;

    case 0x04:
        object->zoom2d = g2(buffer);
        OBJ_SET(ZOOM2D);
        return true;
    case 0x05:
        object->xan2d = g2(buffer);
        OBJ_SET(XAN2D);
        return true;
    case 0x06:
        object->yan2d = g2(buffer);
        OBJ_SET(YAN2D);
        return true;
    case 0x5F:
        object->zan2d = g2(buffer);
        OBJ_SET(ZAN2D);
        return true;
    case 0x07:
        object->offset_x2d = g2b(buffer);
        OBJ_SET(XOF2D);
        return true;
    case 0x08:
        object->offset_y2d = g2b(buffer);
        OBJ_SET(YOF2D);
        return true;

    case 0x0B:
        object->stacking_behaviour = 1;
        OBJ_SET(STACKABLE);
        return true;
    case 0xA5: /* never stackable */
        object->stacking_behaviour = 0;
        OBJ_SET(STACKABLE);
        return true;
    case 0x0C:
        object->cost = g4(buffer);
        OBJ_SET(COST);
        return true;
    case 0x0D:
        object->wearpos_1 = g1(buffer);
        OBJ_SET(WEARPOS);
        return true;
    case 0x0E:
        object->wearpos_2 = g1(buffer);
        OBJ_SET(WEARPOS2);
        return true;
    case 0x1B:
        object->wearpos_3 = g1(buffer);
        OBJ_SET(WEARPOS3);
        return true;
    case 0x10:
        object->is_members = true;
        OBJ_SET(MEMBERS);
        return true;
    case 0x41: /* tradeable */
        object->ge_tradeable = true;
        OBJ_SET(GE_TRADEABLE);
        return true;

    case 0x28:
        obj_b670_read_pairs(
            buffer, &object->recolors_from, &object->recolors_to, &object->recolor_count);
        OBJ_SET(RECOL);
        return true;
    case 0x29:
        obj_b670_read_pairs(
            buffer, &object->retextures_from, &object->retextures_to, &object->retexture_count);
        OBJ_SET(RETEX);
        return true;
    case 0x2A: /* recolour palette: (index, value) byte pairs */
    {
        int length = gushortsmart(buffer);
        for( int i = 0; i < length; i++ )
        {
            g1(buffer);
            g1(buffer);
        }
        return true;
    }

    case 0x5E:
        object->category = g2(buffer);
        OBJ_SET(CATEGORY);
        return true;
    case 0x61:
        object->noted_id = g2(buffer);
        OBJ_SET(CERTLINK);
        return true;
    case 0x62:
        object->noted_template = g2(buffer);
        OBJ_SET(CERTTEMPLATE);
        return true;
    case 0x8B: /* bind link / bought id */
        object->bought_id = g2(buffer);
        OBJ_SET(BOUGHTLINK);
        return true;
    case 0x8C:
        object->bought_template_id = g2(buffer);
        OBJ_SET(BOUGHTTEMPLATE);
        return true;
    case 0x73:
        object->team = g1(buffer);
        OBJ_SET(TEAM);
        return true;
    case 0x86: /* pick size shift */
        object->shift_click_drop_index = g1b(buffer);
        OBJ_SET(SHIFT_CLICK_DROP);
        return true;

    case 0x64:
    case 0x65:
    case 0x66:
    case 0x67:
    case 0x68:
    case 0x69:
    case 0x6A:
    case 0x6B:
    case 0x6C:
    case 0x6D:
        object->count_obj[opcode - 0x64] = g2(buffer);
        object->count_co[opcode - 0x64] = g2(buffer);
        OBJ_SET_AT(COUNTOBJ1, opcode - 0x64);
        return true;

    case 0x6E:
        object->resize_x = g2(buffer);
        OBJ_SET(RESIZEX);
        return true;
    case 0x6F:
        object->resize_y = g2(buffer);
        OBJ_SET(RESIZEY);
        return true;
    case 0x70:
        object->resize_z = g2(buffer);
        OBJ_SET(RESIZEZ);
        return true;
    case 0x71:
        object->ambient = g1b(buffer);
        OBJ_SET(AMBIENT);
        return true;
    case 0x72:
        object->contrast = g1b(buffer) * 5;
        OBJ_SET(CONTRAST);
        return true;

    case 0xF9:
        RSCache_BufferReadParams(buffer, &object->params);
        OBJ_SET(PARAMS);
        return true;

    /* --- consumed at the right width, nothing in this struct to hold them --- */

    case 0x0F:
    case 0x9C:
    case 0x9D:
    case 0xA7:
    case 0xA8:
    case 0xB2:
        return true; /* payload-free flags */

    case 0x60: /* dummy item */
        g1(buffer);
        return true;

    case 0x0A:
    case 0x12: /* multi stack size */
    case 0x2C:
    case 0x2D:
    case 0x79: /* loan id */
    case 0x7A: /* loan template */
    case 0x8E:
    case 0x8F:
    case 0x90:
    case 0x91:
    case 0x92:
    case 0x96:
    case 0x97:
    case 0x98:
    case 0x99:
    case 0x9A:
    case 0xA1:
    case 0xA2:
    case 0xA3:
        g2(buffer);
        return true;

    case 0x2B: /* name colour */
    case 0x45: /* buy limit */
        g4(buffer);
        return true;

    case 0x7D: /* male wear translate */
    case 0x7E: /* female wear translate */
        g1(buffer);
        g1(buffer);
        g1(buffer);
        return true;

    case 0x7F:
    case 0x80:
    case 0x81:
    case 0x82:
        g1(buffer);
        g2(buffer);
        return true;

    case 0xB5: /* big value: two ints */
        g4(buffer);
        g4(buffer);
        return true;

    case 0x84: /* quest ids */
    {
        int length = gushortsmart(buffer);
        for( int i = 0; i < length; i++ )
            g2(buffer);
        return true;
    }

    default:
        /* Unknown payload length: stop rather than misalign later fields. No
         * record in `cache.rs727_preeoc` reaches here. */
        return false;
    }
}

bool
RSCache_Dat2ConfigObjDecodeOp(
    struct RSCache_Dat2ConfigObj* object,
    int opcode,
    struct RSCache_Buffer* buffer,
    unsigned flags)
{
        /* A different stream, not a wider field: dispatch whole rather than
         * threading build-670 exceptions through every case below. */
        if( flags & RSCACHE_CONFIG_OBJ_DECODE_RS2_530 )
            return obj_decode_op_rs2_530(object, opcode, buffer);
        if( flags & RSCACHE_CONFIG_OBJ_DECODE_RS2_BUILD670 )
            return obj_decode_op_rs2_b670(object, opcode, buffer);
        if( flags & RSCACHE_CONFIG_OBJ_DECODE_RS2_634 )
            return obj_decode_op_rs2_634(object, opcode, buffer);

        switch( opcode )
        {
        case 1:
            object->inventory_model_id = g2(buffer);
            OBJ_SET(MODEL);
            break;
        case 2:
            free(object->name);
            object->name = gcstring(buffer);
            OBJ_SET(NAME);
            break;
        case 3:
            free(object->examine);
            object->examine = gcstring(buffer);
            OBJ_SET(DESC);
            break;
        case 4:
            object->zoom2d = g2(buffer);
            OBJ_SET(ZOOM2D);
            break;
        case 5:
            object->xan2d = g2(buffer);
            OBJ_SET(XAN2D);
            break;
        case 6:
            object->yan2d = g2(buffer);
            OBJ_SET(YAN2D);
            break;
        case 7:
            object->offset_x2d = g2b(buffer);
            OBJ_SET(XOF2D);
            break;
        case 8:
            object->offset_y2d = g2b(buffer);
            OBJ_SET(YOF2D);
            break;
        case 9:
            free(gcstring(buffer));
            break;
        case 11:
            object->stacking_behaviour = 1;
            OBJ_SET(STACKABLE);
            break;
        case 12:
            object->cost = g4(buffer);
            OBJ_SET(COST);
            break;
        case 13:
            object->wearpos_1 = g1b(buffer);
            OBJ_SET(WEARPOS);
            break;
        case 14:
            object->wearpos_2 = g1b(buffer);
            OBJ_SET(WEARPOS2);
            break;
        case 15:
            if( !(flags & RSCACHE_CONFIG_OBJ_DECODE_REV238_UNTRADEABLE) )
                return false;
            object->tradeable = false;
            OBJ_SET(UNTRADEABLE);
            break;
        case 16:
            object->is_members = true;
            OBJ_SET(MEMBERS);
            break;
        case 23:
            object->male_model_0 = g2(buffer);
            object->male_offset = g1(buffer);
            OBJ_SET(MANWEAR);
            break;
        case 24:
            object->male_model_1 = g2(buffer);
            OBJ_SET(MANWEAR2);
            break;
        case 25:
            object->female_model_0 = g2(buffer);
            object->female_offset = g1(buffer);
            OBJ_SET(WOMANWEAR);
            break;
        case 26:
            object->female_model_1 = g2(buffer);
            OBJ_SET(WOMANWEAR2);
            break;
        case 27:
            object->wearpos_3 = g1(buffer);
            OBJ_SET(WEARPOS3);
            break;
        case 30:
        case 31:
        case 32:
        case 33:
        case 34:
        {
            int idx = opcode - 30;
            free(object->actions[idx]);
            free(object->hidden_actions[idx]);
            object->hidden_actions[idx] = NULL;
            object->actions[idx] = gcstring(buffer);
            /* The client hides an op spelled "hidden"; keep the spelling so the
             * stated opcode can be written back. */
            if( object->actions[idx] && strcasecmp(object->actions[idx], "Hidden") == 0 )
            {
                object->hidden_actions[idx] = object->actions[idx];
                object->actions[idx] = NULL;
            }
            OBJ_SET_AT(OP1, idx);
            break;
        }
        case 35:
        case 36:
        case 37:
        case 38:
        case 39:
            free(object->if_actions[opcode - 35]);
            object->if_actions[opcode - 35] = gcstring(buffer);
            OBJ_SET_AT(IOP1, opcode - 35);
            break;
        case 40:
        {
            int recolor_count = g1(buffer);
            free(object->recolors_from);
            free(object->recolors_to);
            object->recolors_from = malloc(recolor_count * sizeof(int));
            object->recolors_to = malloc(recolor_count * sizeof(int));
            for( int i = 0; i < recolor_count; i++ )
            {
                object->recolors_from[i] = g2(buffer);
                object->recolors_to[i] = g2(buffer);
            }
            object->recolor_count = recolor_count;
            OBJ_SET(RECOL);
            break;
        }
        case 41:
        {
            int retexture_count = g1(buffer);
            free(object->retextures_from);
            free(object->retextures_to);
            object->retextures_from = malloc(retexture_count * sizeof(int));
            object->retextures_to = malloc(retexture_count * sizeof(int));
            for( int i = 0; i < retexture_count; i++ )
            {
                object->retextures_from[i] = g2(buffer);
                object->retextures_to[i] = g2(buffer);
            }
            object->retexture_count = retexture_count;
            OBJ_SET(RETEX);
            break;
        }
        case 42:
            object->shift_click_drop_index = g1b(buffer);
            OBJ_SET(SHIFT_CLICK_DROP);
            break;
        case 43:
        {
            int action_id = g1(buffer);
            bool valid = action_id >= 0 && action_id < 5;
            if( valid && !object->sub_actions[action_id] )
            {
                object->sub_actions[action_id] = (char**)malloc(20 * sizeof(char*));
                memset(object->sub_actions[action_id], 0, 20 * sizeof(char*));
            }

            while( true )
            {
                int sub_action_id = g1(buffer) - 1;
                if( sub_action_id == -1 )
                    break;
                char* string = gcstring(buffer);
                if( valid && sub_action_id >= 0 && sub_action_id < 20 )
                {
                    free(object->sub_actions[action_id][sub_action_id]);
                    object->sub_actions[action_id][sub_action_id] = string;
                }
                else
                    free(string);
            }
            /* An out-of-range op index is consumed and dropped: nothing stored. */
            if( valid )
                OBJ_SET_AT(SUBOP1, action_id);
            break;
        }
        case 44:
            if( !(flags & RSCACHE_CONFIG_OBJ_DECODE_REV237_INT_MODEL_IDS) )
                return false;
            object->inventory_model_id = g4(buffer);
            OBJ_SET(MODEL);
            break;
        case 45:
            if( !(flags & RSCACHE_CONFIG_OBJ_DECODE_REV237_INT_MODEL_IDS) )
                return false;
            object->male_model_0 = g4(buffer);
            object->male_offset = g1(buffer);
            OBJ_SET(MANWEAR);
            break;
        case 46:
            if( !(flags & RSCACHE_CONFIG_OBJ_DECODE_REV237_INT_MODEL_IDS) )
                return false;
            object->male_model_1 = g4(buffer);
            OBJ_SET(MANWEAR2);
            break;
        case 47:
            if( !(flags & RSCACHE_CONFIG_OBJ_DECODE_REV237_INT_MODEL_IDS) )
                return false;
            object->male_model_2 = g4(buffer);
            OBJ_SET(MANWEAR3);
            break;
        case 48:
            if( !(flags & RSCACHE_CONFIG_OBJ_DECODE_REV237_INT_MODEL_IDS) )
                return false;
            object->female_model_0 = g4(buffer);
            object->female_offset = g1(buffer);
            OBJ_SET(WOMANWEAR);
            break;
        case 49:
            if( !(flags & RSCACHE_CONFIG_OBJ_DECODE_REV237_INT_MODEL_IDS) )
                return false;
            object->female_model_1 = g4(buffer);
            OBJ_SET(WOMANWEAR2);
            break;
        case 50:
            if( !(flags & RSCACHE_CONFIG_OBJ_DECODE_REV237_INT_MODEL_IDS) )
                return false;
            object->female_model_2 = g4(buffer);
            OBJ_SET(WOMANWEAR3);
            break;
        case 51:
            if( !(flags & RSCACHE_CONFIG_OBJ_DECODE_REV237_INT_MODEL_IDS) )
                return false;
            object->male_head_model = g4(buffer);
            OBJ_SET(MANHEAD);
            break;
        case 52:
            if( !(flags & RSCACHE_CONFIG_OBJ_DECODE_REV237_INT_MODEL_IDS) )
                return false;
            object->male_head_model_2 = g4(buffer);
            OBJ_SET(MANHEAD2);
            break;
        case 53:
            if( !(flags & RSCACHE_CONFIG_OBJ_DECODE_REV237_INT_MODEL_IDS) )
                return false;
            object->female_head_model = g4(buffer);
            OBJ_SET(WOMANHEAD);
            break;
        case 54:
            if( !(flags & RSCACHE_CONFIG_OBJ_DECODE_REV237_INT_MODEL_IDS) )
                return false;
            object->female_head_model_2 = g4(buffer);
            OBJ_SET(WOMANHEAD2);
            break;
        case 65:
            object->ge_tradeable = true;
            OBJ_SET(GE_TRADEABLE);
            break;
        case 75:
            object->weight = g2b(buffer);
            OBJ_SET(WEIGHT);
            break;
        case 78:
            object->male_model_2 = g2(buffer);
            OBJ_SET(MANWEAR3);
            break;
        case 79:
            object->female_model_2 = g2(buffer);
            OBJ_SET(WOMANWEAR3);
            break;
        case 90:
            object->male_head_model = g2(buffer);
            OBJ_SET(MANHEAD);
            break;
        case 91:
            object->female_head_model = g2(buffer);
            OBJ_SET(WOMANHEAD);
            break;
        case 92:
            object->male_head_model_2 = g2(buffer);
            OBJ_SET(MANHEAD2);
            break;
        case 93:
            object->female_head_model_2 = g2(buffer);
            OBJ_SET(WOMANHEAD2);
            break;
        case 94:
            object->category = g2(buffer);
            OBJ_SET(CATEGORY);
            break;
        case 95:
            object->zan2d = g2(buffer);
            OBJ_SET(ZAN2D);
            break;
        case 97:
            object->noted_id = g2(buffer);
            OBJ_SET(CERTLINK);
            break;
        case 98:
            object->noted_template = g2(buffer);
            OBJ_SET(CERTTEMPLATE);
            break;
        case 100:
        case 101:
        case 102:
        case 103:
        case 104:
        case 105:
        case 106:
        case 107:
        case 108:
        case 109:
            object->count_obj[opcode - 100] = g2(buffer);
            object->count_co[opcode - 100] = g2(buffer);
            OBJ_SET_AT(COUNTOBJ1, opcode - 100);
            break;
        case 110:
            object->resize_x = g2(buffer);
            OBJ_SET(RESIZEX);
            break;
        case 111:
            object->resize_y = g2(buffer);
            OBJ_SET(RESIZEY);
            break;
        case 112:
            object->resize_z = g2(buffer);
            OBJ_SET(RESIZEZ);
            break;
        case 113:
            object->ambient = g1b(buffer);
            OBJ_SET(AMBIENT);
            break;
        case 114:
            object->contrast = g1b(buffer) * 5;
            OBJ_SET(CONTRAST);
            break;
        case 115:
            /*
             * One byte, not two.
             *
             * This read `g2` and swallowed the following opcode byte, so every
             * record carrying a team misparsed from here on. It stayed invisible
             * because the misparse usually landed on a zero and was taken for the
             * terminator: 79 obj records in osrs239 stopped 85-107 bytes early
             * with no error, and nothing measured obj's consumption because the
             * struct has no `_consumed` field.
             *
             * Settled against the reference decoder rather than guessed — its
             * opcode 115 uses the `& 255` single-byte read, where the u16 opcodes
             * either side of it (110-112) use the two-byte one.
             */
            object->team = g1(buffer);
            OBJ_SET(TEAM);
            break;
        case 139:
            object->bought_id = g2(buffer);
            OBJ_SET(BOUGHTLINK);
            break;
        case 140:
            object->bought_template_id = g2(buffer);
            OBJ_SET(BOUGHTTEMPLATE);
            break;
        case 148:
            object->placeholder_id = g2(buffer);
            OBJ_SET(PLACEHOLDERLINK);
            break;
        case 149:
            object->placeholder_template_id = g2(buffer);
            OBJ_SET(PLACEHOLDERTEMPLATE);
            break;
        case 160:
            if( !(flags & RSCACHE_CONFIG_OBJ_DECODE_REV239_STACKABLE2) )
                return false;
            object->stacking_behaviour = 2;
            OBJ_SET(STACKABLE);
            break;
        case 200:
            if( !(flags & RSCACHE_CONFIG_OBJ_DECODE_REV237_ENTITY_OPS) )
                return false;
            RSCache_EntityOpsDecodeSubOp(&object->entity_ops, buffer);
            OBJ_SET(ENTITY_SUB_OPS);
            break;
        case 201:
            if( !(flags & RSCACHE_CONFIG_OBJ_DECODE_REV237_ENTITY_OPS) )
                return false;
            RSCache_EntityOpsDecodeCondOp(&object->entity_ops, buffer);
            OBJ_SET(ENTITY_COND_OPS);
            break;
        case 202:
            if( !(flags & RSCACHE_CONFIG_OBJ_DECODE_REV237_ENTITY_OPS) )
                return false;
            RSCache_EntityOpsDecodeCondSubOp(&object->entity_ops, buffer);
            OBJ_SET(ENTITY_COND_SUB_OPS);
            break;
        case 249:
            RSCache_BufferReadParams(buffer, &object->params);
            OBJ_SET(PARAMS);
            break;
        default:
            /* Unknown payload length: stop rather than misalign later fields. */
            return false;
        }

    /* Fell out of the switch: a case handled this opcode. */
    return true;
}

void
RSCache_Dat2ConfigObjDecodeInplaceFlags(
    struct RSCache_Dat2ConfigObj* object,
    char* data,
    int data_size,
    int flags)
{
    struct RSCache_Buffer buffer;

    assert(object);
    RSCache_BufferInit(&buffer, (uint8_t*)(data), (uint32_t)(data_size));

    RSCache_Dat2ConfigObjInit(object);

    while( true )
    {
        if( buffer.position >= buffer.size )
        {
            object->_consumed = (int)buffer.position;
            return;
        }

        int opcode = g1(&buffer);
        if( opcode == 0 )
        {
            object->_consumed = (int)buffer.position;
            return;
        }

        if( !RSCache_Dat2ConfigObjDecodeOp(object, opcode, &buffer, (unsigned)flags) )
        {
            object->_consumed = (int)buffer.position;
            return;
        }
    }
}
uint32_t
RSCache_Dat2ConfigObjEncodeBound(const struct RSCache_Dat2ConfigObj* object)
{
    /*
     * Same shape as the npc and loc bounds: one flat allowance covering all 82
     * scalar opcodes, then every variable-length part measured from the record.
     * Checked against a canary on every obj in the cache by `test_opcode_codec`.
     */
    uint32_t need = 2048u;
    int i;

    assert(object);

    need += (uint32_t)object->recolor_count * 4u + 2u;
    need += (uint32_t)object->retexture_count * 4u + 2u;

    if( object->name )
        need += (uint32_t)strlen(object->name) + 2u;
    if( object->examine )
        need += (uint32_t)strlen(object->examine) + 2u;
    for( i = 0; i < 5; i++ )
    {
        if( object->actions[i] )
            need += (uint32_t)strlen(object->actions[i]) + 2u;
        if( object->hidden_actions[i] )
            need += (uint32_t)strlen(object->hidden_actions[i]) + 2u;
        if( object->if_actions[i] )
            need += (uint32_t)strlen(object->if_actions[i]) + 2u;
        need += 3u; /* opcode 43, its op index and terminator, even when empty */
        if( object->sub_actions[i] )
        {
            for( int sub = 0; sub < 20; sub++ )
            {
                if( object->sub_actions[i][sub] )
                    need += (uint32_t)strlen(object->sub_actions[i][sub]) + 4u;
            }
        }
    }

    need += RSCache_EntityOpsBound(&object->entity_ops);
    need += 1u + RSCache_BufferParamsBound(&object->params);
    return need;
}
