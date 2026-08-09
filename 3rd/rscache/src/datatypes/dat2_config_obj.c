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

uint32_t
RSCache_Dat2ConfigObjEncodeFlags(
    const struct RSCache_Dat2ConfigObj* object,
    int flags,
    uint8_t* out,
    uint32_t out_capacity)
{
    if( !object || !out )
        return 0;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    /* Compare against the decode defaults so a field the packer omitted stays
     * omitted. Opcodes are written in ascending order, which is how the packer
     * emits them, so most records come back byte-exact. */
    struct RSCache_Dat2ConfigObj defaults;
    RSCache_Dat2ConfigObjInit(&defaults);

#define OBJ_P_IF(condition, opcode_value, write_body)                                              \
    do                                                                                             \
    {                                                                                              \
        if( condition )                                                                            \
        {                                                                                          \
            p1(&buffer, (opcode_value));                                                           \
            write_body;                                                                            \
        }                                                                                          \
    } while( 0 )

    {
        bool inv_int = object->inventory_model_id > 0xFFFF;
        if( inv_int )
        {
            p1(&buffer, 44);
            p4(&buffer, object->inventory_model_id);
        }
        else if( object->inventory_model_id != defaults.inventory_model_id )
        {
            p1(&buffer, 1);
            p2(&buffer, object->inventory_model_id);
        }
    }

    /* The default name is the literal "null"; a record that carries that name is
     * indistinguishable from one that omitted opcode 2. */
    if( object->name && strcmp(object->name, "null") != 0 )
    {
        p1(&buffer, 2);
        pjstr(&buffer, object->name, RSCACHE_JSTR_TERMINATOR_NULL);
    }
    if( object->examine )
    {
        p1(&buffer, 3);
        pjstr(&buffer, object->examine, RSCACHE_JSTR_TERMINATOR_NULL);
    }

    OBJ_P_IF(object->zoom2d != defaults.zoom2d, 4, p2(&buffer, object->zoom2d));
    OBJ_P_IF(object->xan2d != defaults.xan2d, 5, p2(&buffer, object->xan2d));
    OBJ_P_IF(object->yan2d != defaults.yan2d, 6, p2(&buffer, object->yan2d));
    OBJ_P_IF(object->offset_x2d != defaults.offset_x2d, 7, p2b(&buffer, object->offset_x2d));
    OBJ_P_IF(object->offset_y2d != defaults.offset_y2d, 8, p2b(&buffer, object->offset_y2d));
    /* Opcode 9 is a string the decoder discards, so it cannot be reproduced. */
    if( object->stacking_behaviour == 1 )
        p1(&buffer, 11);
    OBJ_P_IF(object->cost != defaults.cost, 12, p4(&buffer, object->cost));
    OBJ_P_IF(object->wearpos_1 != defaults.wearpos_1, 13, p1b(&buffer, object->wearpos_1));
    OBJ_P_IF(object->wearpos_2 != defaults.wearpos_2, 14, p1b(&buffer, object->wearpos_2));
    if( (flags & RSCACHE_CONFIG_OBJ_DECODE_REV238_UNTRADEABLE) && !object->tradeable )
        p1(&buffer, 15);
    if( object->is_members )
        p1(&buffer, 16);

    /* Opcode 23 carries the model *and* its offset as one record, so it has to be
     * emitted when either differs from the default. Prefer int forms (45+) when
     * any related model id exceeds u16. */
    {
        bool male0_int = object->male_model_0 > 0xFFFF;
        bool male1_int = object->male_model_1 > 0xFFFF;
        bool male2_int = object->male_model_2 > 0xFFFF;
        bool female0_int = object->female_model_0 > 0xFFFF;
        bool female1_int = object->female_model_1 > 0xFFFF;
        bool female2_int = object->female_model_2 > 0xFFFF;
        bool mhead_int = object->male_head_model > 0xFFFF;
        bool mhead2_int = object->male_head_model_2 > 0xFFFF;
        bool fhead_int = object->female_head_model > 0xFFFF;
        bool fhead2_int = object->female_head_model_2 > 0xFFFF;

        if( male0_int ||
            (object->male_model_0 != defaults.male_model_0 ||
             object->male_offset != defaults.male_offset) )
        {
            if( male0_int )
            {
                p1(&buffer, 45);
                p4(&buffer, object->male_model_0);
                p1(&buffer, object->male_offset);
            }
            else
            {
                p1(&buffer, 23);
                p2(&buffer, object->male_model_0);
                p1(&buffer, object->male_offset);
            }
        }
        if( male1_int || object->male_model_1 != defaults.male_model_1 )
        {
            if( male1_int )
            {
                p1(&buffer, 46);
                p4(&buffer, object->male_model_1);
            }
            else
            {
                p1(&buffer, 24);
                p2(&buffer, object->male_model_1);
            }
        }
        if( female0_int ||
            (object->female_model_0 != defaults.female_model_0 ||
             object->female_offset != defaults.female_offset) )
        {
            if( female0_int )
            {
                p1(&buffer, 48);
                p4(&buffer, object->female_model_0);
                p1(&buffer, object->female_offset);
            }
            else
            {
                p1(&buffer, 25);
                p2(&buffer, object->female_model_0);
                p1(&buffer, object->female_offset);
            }
        }
        if( female1_int || object->female_model_1 != defaults.female_model_1 )
        {
            if( female1_int )
            {
                p1(&buffer, 49);
                p4(&buffer, object->female_model_1);
            }
            else
            {
                p1(&buffer, 26);
                p2(&buffer, object->female_model_1);
            }
        }
        OBJ_P_IF(object->wearpos_3 != defaults.wearpos_3, 27, p1(&buffer, object->wearpos_3));

        /* A NULL action is either an absent opcode or the literal "Hidden", which the
         * decoder normalises to NULL. Both re-encode as absent. */
        for( int i = 0; i < 5; i++ )
        {
            if( object->actions[i] )
            {
                p1(&buffer, 30 + i);
                pjstr(&buffer, object->actions[i], RSCACHE_JSTR_TERMINATOR_NULL);
            }
        }
        for( int i = 0; i < 5; i++ )
        {
            if( object->if_actions[i] )
            {
                p1(&buffer, 35 + i);
                pjstr(&buffer, object->if_actions[i], RSCACHE_JSTR_TERMINATOR_NULL);
            }
        }

        if( object->recolor_count > 0 )
        {
            p1(&buffer, 40);
            p1(&buffer, object->recolor_count);
            for( int i = 0; i < object->recolor_count; i++ )
            {
                p2(&buffer, object->recolors_from[i]);
                p2(&buffer, object->recolors_to[i]);
            }
        }
        if( object->retexture_count > 0 )
        {
            p1(&buffer, 41);
            p1(&buffer, object->retexture_count);
            for( int i = 0; i < object->retexture_count; i++ )
            {
                p2(&buffer, object->retextures_from[i]);
                p2(&buffer, object->retextures_to[i]);
            }
        }

        OBJ_P_IF(
            object->shift_click_drop_index != defaults.shift_click_drop_index,
            42,
            p1b(&buffer, object->shift_click_drop_index));

        /* Opcode 43: sub-action lists, each terminated by a zero index. */
        for( int action = 0; action < 5; action++ )
        {
            if( !object->sub_actions[action] )
                continue;

            int highest = -1;
            for( int sub = 0; sub < 20; sub++ )
            {
                if( object->sub_actions[action][sub] )
                    highest = sub;
            }
            if( highest < 0 )
                continue;

            p1(&buffer, 43);
            p1(&buffer, action);
            for( int sub = 0; sub <= highest; sub++ )
            {
                if( !object->sub_actions[action][sub] )
                    continue;
                /* Indices are stored one-based; zero terminates the list. */
                p1(&buffer, sub + 1);
                pjstr(&buffer, object->sub_actions[action][sub], RSCACHE_JSTR_TERMINATOR_NULL);
            }
            p1(&buffer, 0);
        }

        /* Int model opcodes 47/50-54 when needed (45/46/48/49 already above). */
        if( male2_int )
        {
            p1(&buffer, 47);
            p4(&buffer, object->male_model_2);
        }
        if( female2_int )
        {
            p1(&buffer, 50);
            p4(&buffer, object->female_model_2);
        }
        if( mhead_int )
        {
            p1(&buffer, 51);
            p4(&buffer, object->male_head_model);
        }
        if( mhead2_int )
        {
            p1(&buffer, 52);
            p4(&buffer, object->male_head_model_2);
        }
        if( fhead_int )
        {
            p1(&buffer, 53);
            p4(&buffer, object->female_head_model);
        }
        if( fhead2_int )
        {
            p1(&buffer, 54);
            p4(&buffer, object->female_head_model_2);
        }

        if( object->ge_tradeable )
            p1(&buffer, 65);
        OBJ_P_IF(object->weight != defaults.weight, 75, p2b(&buffer, object->weight));

        if( !male2_int )
            OBJ_P_IF(
                object->male_model_2 != defaults.male_model_2, 78, p2(&buffer, object->male_model_2));
        if( !female2_int )
            OBJ_P_IF(
                object->female_model_2 != defaults.female_model_2,
                79,
                p2(&buffer, object->female_model_2));
        if( !mhead_int )
            OBJ_P_IF(
                object->male_head_model != defaults.male_head_model,
                90,
                p2(&buffer, object->male_head_model));
        if( !fhead_int )
            OBJ_P_IF(
                object->female_head_model != defaults.female_head_model,
                91,
                p2(&buffer, object->female_head_model));
        if( !mhead2_int )
            OBJ_P_IF(
                object->male_head_model_2 != defaults.male_head_model_2,
                92,
                p2(&buffer, object->male_head_model_2));
        if( !fhead2_int )
            OBJ_P_IF(
                object->female_head_model_2 != defaults.female_head_model_2,
                93,
                p2(&buffer, object->female_head_model_2));
    }

    OBJ_P_IF(object->category != defaults.category, 94, p2(&buffer, object->category));
    OBJ_P_IF(object->zan2d != defaults.zan2d, 95, p2(&buffer, object->zan2d));
    OBJ_P_IF(object->noted_id != defaults.noted_id, 97, p2(&buffer, object->noted_id));
    OBJ_P_IF(
        object->noted_template != defaults.noted_template, 98, p2(&buffer, object->noted_template));

    for( int i = 0; i < 10; i++ )
    {
        if( object->count_obj[i] != 0 || object->count_co[i] != 0 )
        {
            p1(&buffer, 100 + i);
            p2(&buffer, object->count_obj[i]);
            p2(&buffer, object->count_co[i]);
        }
    }

    OBJ_P_IF(object->resize_x != defaults.resize_x, 110, p2(&buffer, object->resize_x));
    OBJ_P_IF(object->resize_y != defaults.resize_y, 111, p2(&buffer, object->resize_y));
    OBJ_P_IF(object->resize_z != defaults.resize_z, 112, p2(&buffer, object->resize_z));
    OBJ_P_IF(object->ambient != defaults.ambient, 113, p1b(&buffer, object->ambient));
    /* The decoder multiplies the stored byte by 5, so divide going back out. */
    OBJ_P_IF(object->contrast != defaults.contrast, 114, p1b(&buffer, object->contrast / 5));
    OBJ_P_IF(object->team != defaults.team, 115, p1(&buffer, object->team));
    OBJ_P_IF(object->bought_id != defaults.bought_id, 139, p2(&buffer, object->bought_id));
    OBJ_P_IF(
        object->bought_template_id != defaults.bought_template_id,
        140,
        p2(&buffer, object->bought_template_id));
    OBJ_P_IF(
        object->placeholder_id != defaults.placeholder_id, 148, p2(&buffer, object->placeholder_id));
    OBJ_P_IF(
        object->placeholder_template_id != defaults.placeholder_template_id,
        149,
        p2(&buffer, object->placeholder_template_id));

    if( object->stacking_behaviour == 2 )
        p1(&buffer, 160);

    if( (flags & RSCACHE_CONFIG_OBJ_DECODE_REV237_ENTITY_OPS) &&
        (object->entity_ops.sub_ops_count > 0 || object->entity_ops.cond_ops_count > 0 ||
         object->entity_ops.cond_sub_ops_count > 0) )
    {
        RSCache_EntityOpsEncode(&object->entity_ops, &buffer, 30, 200, 201, 202);
    }

    if( object->params.count > 0 )
    {
        p1(&buffer, 249);
        pparams(&buffer, &object->params);
    }

#undef OBJ_P_IF

    p1(&buffer, 0);

    /* init_object allocates the default name; release it rather than leak once per
     * encoded record. */
    free(defaults.name);
    RSCache_EntityOpsFreeInplace(&defaults.entity_ops);

    return buffer.position;
}

void
RSCache_Dat2ConfigObjInit(struct RSCache_Dat2ConfigObj* object)
{
    memset(object, 0, sizeof(struct RSCache_Dat2ConfigObj));

    object->name = malloc(5);
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
        return true;
    case 0x17: /* male model 0; the trailing type byte went away at build 502 */
        object->male_model_0 = gvaruint(buffer);
        return true;
    case 0x18:
        object->male_model_1 = gvaruint(buffer);
        return true;
    case 0x19: /* female model 0 */
        object->female_model_0 = gvaruint(buffer);
        return true;
    case 0x1A:
        object->female_model_1 = gvaruint(buffer);
        return true;
    case 0x4E:
        object->male_model_2 = gvaruint(buffer);
        return true;
    case 0x4F:
        object->female_model_2 = gvaruint(buffer);
        return true;
    case 0x5A:
        object->male_head_model = gvaruint(buffer);
        return true;
    case 0x5B:
        object->female_head_model = gvaruint(buffer);
        return true;
    case 0x5C:
        object->male_head_model_2 = gvaruint(buffer);
        return true;
    case 0x5D:
        object->female_head_model_2 = gvaruint(buffer);
        return true;

    case 0x02:
        obj_b670_read_string(buffer, &object->name);
        return true;
    case 0x03: /* buff effect, where OldSchool keeps the examine text */
        obj_b670_read_string(buffer, &object->examine);
        return true;

    case 0x1E:
    case 0x1F:
    case 0x20:
    case 0x21:
    case 0x22:
        obj_b670_read_string(buffer, &object->actions[opcode - 0x1E]);
        return true;
    case 0x23:
    case 0x24:
    case 0x25:
    case 0x26:
    case 0x27:
        obj_b670_read_string(buffer, &object->if_actions[opcode - 0x23]);
        return true;
    case 0xA4: /* combine shard name */
        obj_b670_read_string(buffer, NULL);
        return true;

    case 0x04:
        object->zoom2d = g2(buffer);
        return true;
    case 0x05:
        object->xan2d = g2(buffer);
        return true;
    case 0x06:
        object->yan2d = g2(buffer);
        return true;
    case 0x5F:
        object->zan2d = g2(buffer);
        return true;
    case 0x07:
        object->offset_x2d = g2b(buffer);
        return true;
    case 0x08:
        object->offset_y2d = g2b(buffer);
        return true;

    case 0x0B:
        object->stacking_behaviour = 1;
        return true;
    case 0xA5: /* never stackable */
        object->stacking_behaviour = 0;
        return true;
    case 0x0C:
        object->cost = g4(buffer);
        return true;
    case 0x0D:
        object->wearpos_1 = g1(buffer);
        return true;
    case 0x0E:
        object->wearpos_2 = g1(buffer);
        return true;
    case 0x1B:
        object->wearpos_3 = g1(buffer);
        return true;
    case 0x10:
        object->is_members = true;
        return true;
    case 0x41: /* tradeable */
        object->ge_tradeable = true;
        return true;

    case 0x28:
        obj_b670_read_pairs(
            buffer, &object->recolors_from, &object->recolors_to, &object->recolor_count);
        return true;
    case 0x29:
        obj_b670_read_pairs(
            buffer, &object->retextures_from, &object->retextures_to, &object->retexture_count);
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
        return true;
    case 0x61:
        object->noted_id = g2(buffer);
        return true;
    case 0x62:
        object->noted_template = g2(buffer);
        return true;
    case 0x8B: /* bind link / bought id */
        object->bought_id = g2(buffer);
        return true;
    case 0x8C:
        object->bought_template_id = g2(buffer);
        return true;
    case 0x73:
        object->team = g1(buffer);
        return true;
    case 0x86: /* pick size shift */
        object->shift_click_drop_index = g1b(buffer);
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
        return true;

    case 0x6E:
        object->resize_x = g2(buffer);
        return true;
    case 0x6F:
        object->resize_y = g2(buffer);
        return true;
    case 0x70:
        object->resize_z = g2(buffer);
        return true;
    case 0x71:
        object->ambient = g1b(buffer);
        return true;
    case 0x72:
        object->contrast = g1b(buffer) * 5;
        return true;

    case 0xF9:
        RSCache_BufferReadParams(buffer, &object->params);
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
        if( flags & RSCACHE_CONFIG_OBJ_DECODE_RS2_BUILD670 )
            return obj_decode_op_rs2_b670(object, opcode, buffer);

        switch( opcode )
        {
        case 1:
            object->inventory_model_id = g2(buffer);
            break;
        case 2:
            free(object->name);
            object->name = gcstring(buffer);
            break;
        case 3:
            free(object->examine);
            object->examine = gcstring(buffer);
            break;
        case 4:
            object->zoom2d = g2(buffer);
            break;
        case 5:
            object->xan2d = g2(buffer);
            break;
        case 6:
            object->yan2d = g2(buffer);
            break;
        case 7:
            object->offset_x2d = g2b(buffer);
            break;
        case 8:
            object->offset_y2d = g2b(buffer);
            break;
        case 9:
            free(gcstring(buffer));
            break;
        case 11:
            object->stacking_behaviour = 1;
            break;
        case 12:
            object->cost = g4(buffer);
            break;
        case 13:
            object->wearpos_1 = g1b(buffer);
            break;
        case 14:
            object->wearpos_2 = g1b(buffer);
            break;
        case 15:
            if( !(flags & RSCACHE_CONFIG_OBJ_DECODE_REV238_UNTRADEABLE) )
                return false;
            object->tradeable = false;
            break;
        case 16:
            object->is_members = true;
            break;
        case 23:
            object->male_model_0 = g2(buffer);
            object->male_offset = g1(buffer);
            break;
        case 24:
            object->male_model_1 = g2(buffer);
            break;
        case 25:
            object->female_model_0 = g2(buffer);
            object->female_offset = g1(buffer);
            break;
        case 26:
            object->female_model_1 = g2(buffer);
            break;
        case 27:
            object->wearpos_3 = g1(buffer);
            break;
        case 30:
        case 31:
        case 32:
        case 33:
        case 34:
        {
            int idx = opcode - 30;
            free(object->actions[idx]);
            object->actions[idx] = gcstring(buffer);
            if( object->actions[idx] && strcasecmp(object->actions[idx], "Hidden") == 0 )
            {
                free(object->actions[idx]);
                object->actions[idx] = NULL;
            }
            break;
        }
        case 35:
        case 36:
        case 37:
        case 38:
        case 39:
            free(object->if_actions[opcode - 35]);
            object->if_actions[opcode - 35] = gcstring(buffer);
            break;
        case 40:
        {
            int recolor_count = g1(buffer);
            object->recolors_from = malloc(recolor_count * sizeof(int));
            object->recolors_to = malloc(recolor_count * sizeof(int));
            for( int i = 0; i < recolor_count; i++ )
            {
                object->recolors_from[i] = g2(buffer);
                object->recolors_to[i] = g2(buffer);
            }
            object->recolor_count = recolor_count;
            break;
        }
        case 41:
        {
            int retexture_count = g1(buffer);
            object->retextures_from = malloc(retexture_count * sizeof(int));
            object->retextures_to = malloc(retexture_count * sizeof(int));
            for( int i = 0; i < retexture_count; i++ )
            {
                object->retextures_from[i] = g2(buffer);
                object->retextures_to[i] = g2(buffer);
            }
            object->retexture_count = retexture_count;
            break;
        }
        case 42:
            object->shift_click_drop_index = g1b(buffer);
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
                    object->sub_actions[action_id][sub_action_id] = string;
                else
                    free(string);
            }
            break;
        }
        case 44:
            if( !(flags & RSCACHE_CONFIG_OBJ_DECODE_REV237_INT_MODEL_IDS) )
                return false;
            object->inventory_model_id = g4(buffer);
            break;
        case 45:
            if( !(flags & RSCACHE_CONFIG_OBJ_DECODE_REV237_INT_MODEL_IDS) )
                return false;
            object->male_model_0 = g4(buffer);
            object->male_offset = g1(buffer);
            break;
        case 46:
            if( !(flags & RSCACHE_CONFIG_OBJ_DECODE_REV237_INT_MODEL_IDS) )
                return false;
            object->male_model_1 = g4(buffer);
            break;
        case 47:
            if( !(flags & RSCACHE_CONFIG_OBJ_DECODE_REV237_INT_MODEL_IDS) )
                return false;
            object->male_model_2 = g4(buffer);
            break;
        case 48:
            if( !(flags & RSCACHE_CONFIG_OBJ_DECODE_REV237_INT_MODEL_IDS) )
                return false;
            object->female_model_0 = g4(buffer);
            object->female_offset = g1(buffer);
            break;
        case 49:
            if( !(flags & RSCACHE_CONFIG_OBJ_DECODE_REV237_INT_MODEL_IDS) )
                return false;
            object->female_model_1 = g4(buffer);
            break;
        case 50:
            if( !(flags & RSCACHE_CONFIG_OBJ_DECODE_REV237_INT_MODEL_IDS) )
                return false;
            object->female_model_2 = g4(buffer);
            break;
        case 51:
            if( !(flags & RSCACHE_CONFIG_OBJ_DECODE_REV237_INT_MODEL_IDS) )
                return false;
            object->male_head_model = g4(buffer);
            break;
        case 52:
            if( !(flags & RSCACHE_CONFIG_OBJ_DECODE_REV237_INT_MODEL_IDS) )
                return false;
            object->male_head_model_2 = g4(buffer);
            break;
        case 53:
            if( !(flags & RSCACHE_CONFIG_OBJ_DECODE_REV237_INT_MODEL_IDS) )
                return false;
            object->female_head_model = g4(buffer);
            break;
        case 54:
            if( !(flags & RSCACHE_CONFIG_OBJ_DECODE_REV237_INT_MODEL_IDS) )
                return false;
            object->female_head_model_2 = g4(buffer);
            break;
        case 65:
            object->ge_tradeable = true;
            break;
        case 75:
            object->weight = g2b(buffer);
            break;
        case 78:
            object->male_model_2 = g2(buffer);
            break;
        case 79:
            object->female_model_2 = g2(buffer);
            break;
        case 90:
            object->male_head_model = g2(buffer);
            break;
        case 91:
            object->female_head_model = g2(buffer);
            break;
        case 92:
            object->male_head_model_2 = g2(buffer);
            break;
        case 93:
            object->female_head_model_2 = g2(buffer);
            break;
        case 94:
            object->category = g2(buffer);
            break;
        case 95:
            object->zan2d = g2(buffer);
            break;
        case 97:
            object->noted_id = g2(buffer);
            break;
        case 98:
            object->noted_template = g2(buffer);
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
            break;
        case 110:
            object->resize_x = g2(buffer);
            break;
        case 111:
            object->resize_y = g2(buffer);
            break;
        case 112:
            object->resize_z = g2(buffer);
            break;
        case 113:
            object->ambient = g1b(buffer);
            break;
        case 114:
            object->contrast = g1b(buffer) * 5;
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
            break;
        case 139:
            object->bought_id = g2(buffer);
            break;
        case 140:
            object->bought_template_id = g2(buffer);
            break;
        case 148:
            object->placeholder_id = g2(buffer);
            break;
        case 149:
            object->placeholder_template_id = g2(buffer);
            break;
        case 160:
            if( !(flags & RSCACHE_CONFIG_OBJ_DECODE_REV239_STACKABLE2) )
                return false;
            object->stacking_behaviour = 2;
            break;
        case 200:
            if( !(flags & RSCACHE_CONFIG_OBJ_DECODE_REV237_ENTITY_OPS) )
                return false;
            RSCache_EntityOpsDecodeSubOp(&object->entity_ops, buffer);
            break;
        case 201:
            if( !(flags & RSCACHE_CONFIG_OBJ_DECODE_REV237_ENTITY_OPS) )
                return false;
            RSCache_EntityOpsDecodeCondOp(&object->entity_ops, buffer);
            break;
        case 202:
            if( !(flags & RSCACHE_CONFIG_OBJ_DECODE_REV237_ENTITY_OPS) )
                return false;
            RSCache_EntityOpsDecodeCondSubOp(&object->entity_ops, buffer);
            break;
        case 249:
            RSCache_BufferReadParams(buffer, &object->params);
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
            return;

        int opcode = g1(&buffer);
        if( opcode == 0 )
            return;

        if( !RSCache_Dat2ConfigObjDecodeOp(object, opcode, &buffer, (unsigned)flags) )
            return;
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

    if( !object )
        return need;

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
        if( object->if_actions[i] )
            need += (uint32_t)strlen(object->if_actions[i]) + 2u;
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
