#include "cachepack.h"

#include "datatypes/dat2_config_obj.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/*
 * Items.
 *
 * Two things here have no rev-254 equivalent and are worth naming. `if_actions`
 * (opcodes 35-39) are the *interface* ops — what the item offers when it is sitting
 * in a widget rather than on the ground — and `sub_actions` is a 5x20 grid of
 * submenu entries hanging off each ground op. Both are separate from the five
 * plain ops, so they get their own keys rather than being folded in.
 *
 * Every key is written for every record, straight from the decoder's presence
 * bits: the value when the stream states the field, `key=default` when it does
 * not. A name the stream states as the literal "null" is `name=null`, a name it
 * never states is `name=default`; a ground op spelled "hidden" is written as
 * spelled (the client hides it, the record still states it). Separate opcodes are
 * separate keys (`op1`..`op5`, `ifop1`..`ifop5`, `countobj1`..`countobj10`); the
 * worn models are one field each with their offset (`manwear` + `manwearoff`
 * are opcode 23 together). The only loss left is the library's: opcode 9, an
 * opcode stated twice, and the stream's opcode order.
 */

static int
obj_flags(const struct CP_Ctx* ctx)
{
    return RSCache_Dat2ConfigObjFlags(&ctx->profile);
}

static int
obj_applies_untradeable(const struct CP_Ctx* ctx)
{
    return (obj_flags(ctx) & RSCACHE_CONFIG_OBJ_DECODE_REV238_UNTRADEABLE) != 0;
}

static int
obj_applies_entity_ops(const struct CP_Ctx* ctx)
{
    return (obj_flags(ctx) & RSCACHE_CONFIG_OBJ_DECODE_REV237_ENTITY_OPS) != 0;
}

/* In the order they are written. */
const struct CP_KeySpec cp_obj_keys[] = {
    { "name", 0, NULL, NULL },
    { "desc", 0, NULL, NULL },
    { "model", 0, NULL, NULL },
    { "2dzoom", 0, NULL, NULL },
    { "2dxan", 0, NULL, NULL },
    { "2dyan", 0, NULL, NULL },
    { "2dzan", 0, NULL, NULL },
    { "2dxof", 0, NULL, NULL },
    { "2dyof", 0, NULL, NULL },
    { "resizex", 0, NULL, NULL },
    { "resizey", 0, NULL, NULL },
    { "resizez", 0, NULL, NULL },
    { "cost", 0, NULL, NULL },
    { "stackable", 0, NULL, NULL },
    { "tradeable", 0, obj_applies_untradeable, NULL },
    { "getradeable", 0, NULL, NULL },
    { "members", 0, NULL, NULL },
    { "wearpos", 0, NULL, NULL },
    { "wearpos2", 0, NULL, NULL },
    { "wearpos3", 0, NULL, NULL },
    { "ambient", 0, NULL, NULL },
    { "contrast", 0, NULL, NULL },
    { "team", 0, NULL, NULL },
    { "weight", 0, NULL, NULL },
    { "category", 0, NULL, NULL },
    { "shiftclickdrop", 0, NULL, NULL },
    { "manwear", 0, NULL, NULL },
    { "manwear2", 0, NULL, NULL },
    { "manwear3", 0, NULL, NULL },
    { "manwearoff", 0, NULL, NULL },
    { "manhead", 0, NULL, NULL },
    { "manhead2", 0, NULL, NULL },
    { "womanwear", 0, NULL, NULL },
    { "womanwear2", 0, NULL, NULL },
    { "womanwear3", 0, NULL, NULL },
    { "womanwearoff", 0, NULL, NULL },
    { "womanhead", 0, NULL, NULL },
    { "womanhead2", 0, NULL, NULL },
    { "certlink", 0, NULL, NULL },
    { "certtemplate", 0, NULL, NULL },
    { "boughtlink", 0, NULL, NULL },
    { "boughttemplate", 0, NULL, NULL },
    { "placeholderlink", 0, NULL, NULL },
    { "placeholdertemplate", 0, NULL, NULL },
    { "countobj1", 0, NULL, NULL },
    { "countobj2", 0, NULL, NULL },
    { "countobj3", 0, NULL, NULL },
    { "countobj4", 0, NULL, NULL },
    { "countobj5", 0, NULL, NULL },
    { "countobj6", 0, NULL, NULL },
    { "countobj7", 0, NULL, NULL },
    { "countobj8", 0, NULL, NULL },
    { "countobj9", 0, NULL, NULL },
    { "countobj10", 0, NULL, NULL },
    { "op1", 0, NULL, NULL },
    { "op2", 0, NULL, NULL },
    { "op3", 0, NULL, NULL },
    { "op4", 0, NULL, NULL },
    { "op5", 0, NULL, NULL },
    { "subop", CP_KEY_LIST, obj_applies_entity_ops, NULL },
    { "condop", CP_KEY_LIST, obj_applies_entity_ops, NULL },
    { "condsubop", CP_KEY_LIST, obj_applies_entity_ops, NULL },
    { "ifop1", 0, NULL, NULL },
    { "ifop2", 0, NULL, NULL },
    { "ifop3", 0, NULL, NULL },
    { "ifop4", 0, NULL, NULL },
    { "ifop5", 0, NULL, NULL },
    { "subaction", CP_KEY_LIST, NULL, NULL },
    { "recol", CP_KEY_INDEXED, NULL, NULL },
    { "retex", CP_KEY_INDEXED, NULL, NULL },
    { "param", CP_KEY_LIST, NULL, NULL },
    { NULL, 0, NULL, NULL },
};

#define OBJ_HAS(field) RSCache_PresenceHas(&entry->present, RSCACHE_OBJ_FIELD_##field)

#define EMIT_INT(field, member, key)                                                          \
    do                                                                                        \
    {                                                                                         \
        if( OBJ_HAS(field) )                                                                  \
            cp_lines_addf(out, key "=%d", entry->member);                                     \
        else                                                                                  \
            cp_lines_add_default(out, key);                                                   \
    } while( 0 )

/* A payload-free flag opcode: its presence is the value. */
#define EMIT_FLAG(field, key, text)                                                           \
    do                                                                                        \
    {                                                                                         \
        if( OBJ_HAS(field) )                                                                  \
            cp_lines_addf(out, key "=" text);                                                 \
        else                                                                                  \
            cp_lines_add_default(out, key);                                                   \
    } while( 0 )

#define EMIT_OBJ(field, member, key)                                                          \
    do                                                                                        \
    {                                                                                         \
        if( OBJ_HAS(field) )                                                                  \
            cp_emit_name(ctx, out, key, CP_TYPE_OBJ, entry->member);                          \
        else                                                                                  \
            cp_lines_add_default(out, key);                                                   \
    } while( 0 )

static void
emit_str(
    struct CP_Lines* out,
    const char* key,
    int present,
    const char* value)
{
    if( !present )
    {
        cp_lines_add_default(out, key);
        return;
    }
    assert(value);
    cp_lines_add_str(out, key, value);
}

static void
emit_pairs(
    struct CP_Lines* out,
    int present,
    const int* from,
    const int* to,
    int count,
    const char* stem)
{
    if( !present )
        cp_lines_add_default(out, stem);
    else if( count == 0 )
        cp_lines_add_empty(out, stem);
    else
        cp_emit_recols(out, from, to, count, stem);
}

/* The rev-237 entity-op lists, each from its own presence bit. The line format
 * is cp_emit_entity_ops's, which cp_parse_entity_op reads back. */
static void
emit_entity_ops(
    const struct RSCache_Dat2ConfigObj* entry,
    struct CP_Lines* out)
{
    const struct RSCache_EntityOps* ops = &entry->entity_ops;
    char buf[1024];

    if( !OBJ_HAS(ENTITY_SUB_OPS) )
        cp_lines_add_default(out, "subop");
    else if( ops->sub_ops_count == 0 )
        cp_lines_add_empty(out, "subop");
    for( int i = 0; OBJ_HAS(ENTITY_SUB_OPS) && i < ops->sub_ops_count; i++ )
    {
        const struct RSCache_EntitySubOp* sub = &ops->sub_ops[i];
        snprintf(buf, sizeof(buf), "%d,%d,%s", sub->index, sub->sub_id,
                 sub->text ? sub->text : "");
        cp_lines_add_str(out, "subop", buf);
    }

    if( !OBJ_HAS(ENTITY_COND_OPS) )
        cp_lines_add_default(out, "condop");
    else if( ops->cond_ops_count == 0 )
        cp_lines_add_empty(out, "condop");
    for( int i = 0; OBJ_HAS(ENTITY_COND_OPS) && i < ops->cond_ops_count; i++ )
    {
        const struct RSCache_EntityCondOp* cond = &ops->cond_ops[i];
        snprintf(buf, sizeof(buf), "%d,%d,%d,%d,%d,%s", cond->index, cond->varp_id,
                 cond->varbit_id, cond->min_value, cond->max_value,
                 cond->text ? cond->text : "");
        cp_lines_add_str(out, "condop", buf);
    }

    if( !OBJ_HAS(ENTITY_COND_SUB_OPS) )
        cp_lines_add_default(out, "condsubop");
    else if( ops->cond_sub_ops_count == 0 )
        cp_lines_add_empty(out, "condsubop");
    for( int i = 0; OBJ_HAS(ENTITY_COND_SUB_OPS) && i < ops->cond_sub_ops_count; i++ )
    {
        const struct RSCache_EntityCondSubOp* cond = &ops->cond_sub_ops[i];
        snprintf(buf, sizeof(buf), "%d,%d,%d,%d,%d,%d,%s", cond->index, cond->sub_id,
                 cond->varp_id, cond->varbit_id, cond->min_value, cond->max_value,
                 cond->text ? cond->text : "");
        cp_lines_add_str(out, "condsubop", buf);
    }
}

static void
emit_obj(
    struct CP_Ctx* ctx,
    const struct RSCache_Dat2ConfigObj* entry,
    struct CP_Lines* out)
{
    char key[32];

    emit_str(out, "name", OBJ_HAS(NAME), entry->name);
    emit_str(out, "desc", OBJ_HAS(DESC), entry->examine);

    EMIT_INT(MODEL, inventory_model_id, "model");
    EMIT_INT(ZOOM2D, zoom2d, "2dzoom");
    EMIT_INT(XAN2D, xan2d, "2dxan");
    EMIT_INT(YAN2D, yan2d, "2dyan");
    EMIT_INT(ZAN2D, zan2d, "2dzan");
    EMIT_INT(XOF2D, offset_x2d, "2dxof");
    EMIT_INT(YOF2D, offset_y2d, "2dyof");
    EMIT_INT(RESIZEX, resize_x, "resizex");
    EMIT_INT(RESIZEY, resize_y, "resizey");
    EMIT_INT(RESIZEZ, resize_z, "resizez");
    EMIT_INT(COST, cost, "cost");
    EMIT_INT(STACKABLE, stacking_behaviour, "stackable");
    if( obj_applies_untradeable(ctx) )
        EMIT_FLAG(UNTRADEABLE, "tradeable", "no");
    EMIT_FLAG(GE_TRADEABLE, "getradeable", "yes");
    EMIT_FLAG(MEMBERS, "members", "yes");
    EMIT_INT(WEARPOS, wearpos_1, "wearpos");
    EMIT_INT(WEARPOS2, wearpos_2, "wearpos2");
    EMIT_INT(WEARPOS3, wearpos_3, "wearpos3");
    EMIT_INT(AMBIENT, ambient, "ambient");
    EMIT_INT(CONTRAST, contrast, "contrast");
    EMIT_INT(TEAM, team, "team");
    EMIT_INT(WEIGHT, weight, "weight");
    EMIT_INT(CATEGORY, category, "category");
    EMIT_INT(SHIFT_CLICK_DROP, shift_click_drop_index, "shiftclickdrop");

    /* A worn model and its offset are one opcode: both keys follow its bit. */
    EMIT_INT(MANWEAR, male_model_0, "manwear");
    EMIT_INT(MANWEAR2, male_model_1, "manwear2");
    EMIT_INT(MANWEAR3, male_model_2, "manwear3");
    EMIT_INT(MANWEAR, male_offset, "manwearoff");
    EMIT_INT(MANHEAD, male_head_model, "manhead");
    EMIT_INT(MANHEAD2, male_head_model_2, "manhead2");
    EMIT_INT(WOMANWEAR, female_model_0, "womanwear");
    EMIT_INT(WOMANWEAR2, female_model_1, "womanwear2");
    EMIT_INT(WOMANWEAR3, female_model_2, "womanwear3");
    EMIT_INT(WOMANWEAR, female_offset, "womanwearoff");
    EMIT_INT(WOMANHEAD, female_head_model, "womanhead");
    EMIT_INT(WOMANHEAD2, female_head_model_2, "womanhead2");

    EMIT_OBJ(CERTLINK, noted_id, "certlink");
    EMIT_OBJ(CERTTEMPLATE, noted_template, "certtemplate");
    EMIT_OBJ(BOUGHTLINK, bought_id, "boughtlink");
    EMIT_OBJ(BOUGHTTEMPLATE, bought_template_id, "boughttemplate");
    EMIT_OBJ(PLACEHOLDERLINK, placeholder_id, "placeholderlink");
    EMIT_OBJ(PLACEHOLDERTEMPLATE, placeholder_template_id, "placeholdertemplate");

    /* The stack-variant table: at `count` of this item, draw that item's model. */
    for( int i = 0; i < 10; i++ )
    {
        snprintf(key, sizeof(key), "countobj%d", i + 1);
        if( RSCache_PresenceHas(&entry->present, RSCACHE_OBJ_FIELD_COUNTOBJ1 + i) )
            cp_lines_addf(out, "%s=%s,%d", key,
                          cp_name_ensure(ctx, CP_TYPE_OBJ, entry->count_obj[i]),
                          entry->count_co[i]);
        else
            cp_lines_add_default(out, key);
    }

    for( int i = 0; i < 5; i++ )
    {
        snprintf(key, sizeof(key), "op%d", i + 1);
        emit_str(out, key, RSCache_PresenceHas(&entry->present, RSCACHE_OBJ_FIELD_OP1 + i),
                 entry->actions[i] ? entry->actions[i] : entry->hidden_actions[i]);
    }
    if( obj_applies_entity_ops(ctx) )
        emit_entity_ops(entry, out);
    for( int i = 0; i < 5; i++ )
    {
        snprintf(key, sizeof(key), "ifop%d", i + 1);
        emit_str(out, key, RSCache_PresenceHas(&entry->present, RSCACHE_OBJ_FIELD_IOP1 + i),
                 entry->if_actions[i]);
    }

    /* `subaction=<op>,<sub>,<text>`; a ground op whose stated list is empty is the
     * bare `subaction=<op>`. */
    {
        int any = 0;
        for( int i = 0; i < 5; i++ )
        {
            int lines = 0;
            if( !RSCache_PresenceHas(&entry->present, RSCACHE_OBJ_FIELD_SUBOP1 + i) )
                continue;
            any = 1;
            for( int j = 0; entry->sub_actions[i] && j < 20; j++ )
            {
                char buf[1024];
                if( !entry->sub_actions[i][j] )
                    continue;
                snprintf(buf, sizeof(buf), "%d,%d,%s", i + 1, j + 1, entry->sub_actions[i][j]);
                cp_lines_add_str(out, "subaction", buf);
                lines++;
            }
            if( lines == 0 )
                cp_lines_addf(out, "subaction=%d", i + 1);
        }
        if( !any )
            cp_lines_add_default(out, "subaction");
    }

    emit_pairs(out, OBJ_HAS(RECOL), entry->recolors_from, entry->recolors_to,
               entry->recolor_count, "recol");
    emit_pairs(out, OBJ_HAS(RETEX), entry->retextures_from, entry->retextures_to,
               entry->retexture_count, "retex");

    if( !OBJ_HAS(PARAMS) )
        cp_lines_add_default(out, "param");
    else if( entry->params.count == 0 )
        cp_lines_add_empty(out, "param");
    else
        cp_emit_params(ctx, out, &entry->params);
}

#undef EMIT_INT
#undef EMIT_FLAG
#undef EMIT_OBJ
#undef OBJ_HAS

int
cp_unpack_obj(
    struct CP_Ctx* ctx,
    int id,
    const uint8_t* record,
    int record_size,
    struct CP_Lines* out)
{
    struct RSCache_Dat2ConfigObj* entry =
        RSCache_Dat2ConfigObjNewDecodeProfile(&ctx->profile, (char*)record, record_size);
    assert(entry);
    if( entry->_consumed != record_size )
        cp_warn(ctx, &ctx->warn_short_decode, "obj %d: consumed %d of %d bytes", id,
                entry->_consumed, record_size);

    emit_obj(ctx, entry, out);

    RSCache_Dat2ConfigObjFree(entry);
    return 1;
}

/* A `yes`/`no` flag key whose opcode carries no payload: the one value the
 * stream can state is `stated`, anything else is a third state it cannot hold. */
static int
parse_flag(
    const char* value,
    const char* stated)
{
    return strcmp(value, stated) == 0;
}

static char*
dup_unescaped(const char* value)
{
    char buf[4096];
    char* copy;

    cp_unescape(value, buf, sizeof(buf));
    copy = strdup(buf);
    assert(copy);
    return copy;
}

uint32_t
cp_pack_obj(
    struct CP_Ctx* ctx,
    int id,
    const struct CP_Config* config,
    uint8_t* out,
    uint32_t out_capacity)
{
    /* The empty-record decode gives the client defaults and no presence: a field
     * is written only if a line below states it. */
    struct RSCache_Dat2ConfigObj* entry = RSCache_Dat2ConfigObjNewDecodeProfile(
        &ctx->profile, (char*)cp_empty_record, (int)sizeof(cp_empty_record));
    assert(entry);

    struct CP_IntList recol_s = { 0 }, recol_d = { 0 };
    struct CP_IntList retex_s = { 0 }, retex_d = { 0 };
    uint32_t written = 0;

#define OBJ_SET(field) RSCache_PresenceSet(&entry->present, RSCACHE_OBJ_FIELD_##field)
#define OBJ_SET_AT(first, index)                                                              \
    RSCache_PresenceSet(&entry->present, RSCACHE_OBJ_FIELD_##first + (index))
#define INT_KEY(name, field, member)                                                          \
    else if( strcmp(key, name) == 0 )                                                         \
    {                                                                                         \
        OBJ_SET(field);                                                                       \
        ok = cp_parse_int(value, &entry->member);                                             \
    }
#define OBJ_KEY(name, field, member)                                                          \
    else if( strcmp(key, name) == 0 )                                                         \
    {                                                                                         \
        OBJ_SET(field);                                                                       \
        ok = cp_resolve_ref(ctx, CP_TYPE_OBJ, value, &entry->member);                         \
    }

    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;
        int ok = 1;
        int index;
        char scratch[2048];
        char* fields[3];

        /* Not stated: the field keeps its client default and no opcode is
         * written. cp_keys_check_config has already made sure it is the only
         * line for its key. */
        if( cp_value_is_default(value) )
            continue;

        if( strcmp(key, "name") == 0 )
        {
            OBJ_SET(NAME);
            free(entry->name);
            entry->name = dup_unescaped(value);
        }
        else if( strcmp(key, "desc") == 0 )
        {
            OBJ_SET(DESC);
            free(entry->examine);
            entry->examine = dup_unescaped(value);
        }
        else if( (index = cp_indexed_key(key, "countobj")) >= 0 )
        {
            if( index >= 10 || strlen(value) >= sizeof(scratch) ||
                cp_split(value, scratch, fields, 2) != 2 )
            {
                ok = 0;
            }
            else
            {
                OBJ_SET_AT(COUNTOBJ1, index);
                ok = cp_resolve_ref(ctx, CP_TYPE_OBJ, fields[0], &entry->count_obj[index]) &&
                     cp_parse_int(fields[1], &entry->count_co[index]);
            }
        }
        else if( (index = cp_indexed_key(key, "ifop")) >= 0 )
        {
            if( index >= 5 )
                ok = 0;
            else
            {
                OBJ_SET_AT(IOP1, index);
                free(entry->if_actions[index]);
                entry->if_actions[index] = dup_unescaped(value);
            }
        }
        else if( (index = cp_indexed_key(key, "op")) >= 0 )
        {
            if( index >= 5 )
                ok = 0;
            else
            {
                /* As the decoder reads it: an op spelled "hidden" is stated and
                 * hidden, its spelling kept for the write-back. */
                char* text = dup_unescaped(value);
                OBJ_SET_AT(OP1, index);
                free(entry->actions[index]);
                free(entry->hidden_actions[index]);
                entry->actions[index] = NULL;
                entry->hidden_actions[index] = NULL;
                if( strcasecmp(text, "Hidden") == 0 )
                    entry->hidden_actions[index] = text;
                else
                    entry->actions[index] = text;
            }
        }
        else if( strcmp(key, "subaction") == 0 )
        {
            /* `<op>,<sub>,<text>`, the text the tail since a menu entry may
             * contain commas; a bare `<op>` states that op's list empty. */
            char buf[1024];
            char* first;
            char* second;
            int action = 0, sub = 0;

            cp_unescape(value, buf, sizeof(buf));
            first = strchr(buf, ',');
            second = first ? strchr(first + 1, ',') : NULL;
            if( !first )
            {
                ok = cp_parse_int(buf, &action) && action >= 1 && action <= 5;
                if( ok )
                    OBJ_SET_AT(SUBOP1, action - 1);
            }
            else if( !second )
                ok = 0;
            else
            {
                *first = '\0';
                *second = '\0';
                ok = cp_parse_int(buf, &action) && cp_parse_int(first + 1, &sub) &&
                     action >= 1 && action <= 5 && sub >= 1 && sub <= 20;
                if( ok )
                {
                    OBJ_SET_AT(SUBOP1, action - 1);
                    if( !entry->sub_actions[action - 1] )
                    {
                        entry->sub_actions[action - 1] = calloc(20, sizeof(char*));
                        assert(entry->sub_actions[action - 1]);
                    }
                    free(entry->sub_actions[action - 1][sub - 1]);
                    entry->sub_actions[action - 1][sub - 1] = strdup(second + 1);
                    assert(entry->sub_actions[action - 1][sub - 1]);
                }
            }
        }
        else if( strcmp(key, "subop") == 0 || strcmp(key, "condop") == 0 ||
                 strcmp(key, "condsubop") == 0 )
        {
            if( strcmp(key, "subop") == 0 )
                OBJ_SET(ENTITY_SUB_OPS);
            else if( strcmp(key, "condop") == 0 )
                OBJ_SET(ENTITY_COND_OPS);
            else
                OBJ_SET(ENTITY_COND_SUB_OPS);
            if( !cp_value_is_empty(value) )
                ok = cp_parse_entity_op(entry->actions, 5, &entry->entity_ops, key, value);
        }
        else if( strcmp(key, "param") == 0 )
        {
            OBJ_SET(PARAMS);
            if( !cp_value_is_empty(value) )
                ok = cp_parse_param(ctx, &entry->params, value);
        }
        else if( strcmp(key, "tradeable") == 0 )
        {
            /* Opcode 15 says "not tradeable"; it has no way to say "tradeable". */
            OBJ_SET(UNTRADEABLE);
            ok = parse_flag(value, "no");
            entry->tradeable = false;
        }
        else if( strcmp(key, "getradeable") == 0 )
        {
            OBJ_SET(GE_TRADEABLE);
            ok = parse_flag(value, "yes");
            entry->ge_tradeable = true;
        }
        else if( strcmp(key, "members") == 0 )
        {
            OBJ_SET(MEMBERS);
            ok = parse_flag(value, "yes");
            entry->is_members = true;
        }
        else if( strcmp(key, "category") == 0 )
        {
            OBJ_SET(CATEGORY);
            ok = cp_resolve_category(ctx, value, &entry->category);
        }
        INT_KEY("model", MODEL, inventory_model_id)
        INT_KEY("2dzoom", ZOOM2D, zoom2d)
        INT_KEY("2dxan", XAN2D, xan2d)
        INT_KEY("2dyan", YAN2D, yan2d)
        INT_KEY("2dzan", ZAN2D, zan2d)
        INT_KEY("2dxof", XOF2D, offset_x2d)
        INT_KEY("2dyof", YOF2D, offset_y2d)
        INT_KEY("resizex", RESIZEX, resize_x)
        INT_KEY("resizey", RESIZEY, resize_y)
        INT_KEY("resizez", RESIZEZ, resize_z)
        INT_KEY("cost", COST, cost)
        INT_KEY("stackable", STACKABLE, stacking_behaviour)
        INT_KEY("wearpos", WEARPOS, wearpos_1)
        INT_KEY("wearpos2", WEARPOS2, wearpos_2)
        INT_KEY("wearpos3", WEARPOS3, wearpos_3)
        INT_KEY("ambient", AMBIENT, ambient)
        INT_KEY("contrast", CONTRAST, contrast)
        INT_KEY("team", TEAM, team)
        INT_KEY("weight", WEIGHT, weight)
        INT_KEY("shiftclickdrop", SHIFT_CLICK_DROP, shift_click_drop_index)
        /* A worn model and its offset are one opcode; either line states it, and
         * the other keeps its client default. */
        INT_KEY("manwear", MANWEAR, male_model_0)
        INT_KEY("manwearoff", MANWEAR, male_offset)
        INT_KEY("manwear2", MANWEAR2, male_model_1)
        INT_KEY("manwear3", MANWEAR3, male_model_2)
        INT_KEY("manhead", MANHEAD, male_head_model)
        INT_KEY("manhead2", MANHEAD2, male_head_model_2)
        INT_KEY("womanwear", WOMANWEAR, female_model_0)
        INT_KEY("womanwearoff", WOMANWEAR, female_offset)
        INT_KEY("womanwear2", WOMANWEAR2, female_model_1)
        INT_KEY("womanwear3", WOMANWEAR3, female_model_2)
        INT_KEY("womanhead", WOMANHEAD, female_head_model)
        INT_KEY("womanhead2", WOMANHEAD2, female_head_model_2)
        OBJ_KEY("certlink", CERTLINK, noted_id)
        OBJ_KEY("certtemplate", CERTTEMPLATE, noted_template)
        OBJ_KEY("boughtlink", BOUGHTLINK, bought_id)
        OBJ_KEY("boughttemplate", BOUGHTTEMPLATE, bought_template_id)
        OBJ_KEY("placeholderlink", PLACEHOLDERLINK, placeholder_id)
        OBJ_KEY("placeholdertemplate", PLACEHOLDERTEMPLATE, placeholder_template_id)
        else if( strncmp(key, "recol", 5) == 0 )
        {
            /* The pairs are collected below; `recol=empty` states an empty list. */
            OBJ_SET(RECOL);
        }
        else if( strncmp(key, "retex", 5) == 0 )
            OBJ_SET(RETEX);
        else
            cp_warn(ctx, &ctx->warn_unknown_key, "obj [%s]: unknown key %s",
                    config->debugname, key);

        if( !ok )
        {
            fprintf(stderr, "cachepack: obj [%s]: bad value for %s=%s\n",
                    config->debugname, key, value);
            goto done;
        }
    }

#undef OBJ_SET
#undef OBJ_SET_AT
#undef INT_KEY
#undef OBJ_KEY

    if( !cp_collect_pairs(config, "recol", &recol_s, &recol_d) ||
        !cp_collect_pairs(config, "retex", &retex_s, &retex_d) )
    {
        fprintf(stderr, "cachepack: obj [%s]: mismatched recolour pairs\n", config->debugname);
        goto done;
    }

    free(entry->recolors_from);
    free(entry->recolors_to);
    free(entry->retextures_from);
    free(entry->retextures_to);
    entry->recolors_from = recol_s.items;
    entry->recolors_to = recol_d.items;
    entry->recolor_count = recol_s.count;
    entry->retextures_from = retex_s.items;
    entry->retextures_to = retex_d.items;
    entry->retexture_count = retex_s.count;

    written = RSCache_Dat2ConfigObjEncodeProfile(&ctx->profile, entry, out, out_capacity);

    /* The lists are the intlists' storage, released below. */
    entry->recolors_from = entry->recolors_to = NULL;
    entry->retextures_from = entry->retextures_to = NULL;

done:
    RSCache_Dat2ConfigObjFree(entry);
    cp_intlist_free(&recol_s);
    cp_intlist_free(&recol_d);
    cp_intlist_free(&retex_s);
    cp_intlist_free(&retex_d);
    return written;
}
