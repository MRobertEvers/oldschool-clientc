#include "cachepack.h"

#include "datatypes/dat2_config_obj.h"

#include <stdlib.h>
#include <string.h>

/*
 * Items.
 *
 * Two things here have no rev-254 equivalent and are worth naming. `if_actions`
 * (opcodes 35-39) are the *interface* ops — what the item offers when it is sitting
 * in a widget rather than on the ground — and `sub_actions` is a 5x20 grid of
 * submenu entries hanging off each ground op. Both are separate from the five
 * plain ops, so they get their own keys rather than being folded in.
 *
 * Lossy per the library: opcode 9 is a string the decoder discards, an action of
 * literal "Hidden" normalises to absent, and a name of literal "null" is
 * indistinguishable from no name at all.
 */

#define EMIT_INT(field, key)                                                                  \
    do                                                                                        \
    {                                                                                         \
        if( entry->field != defaults->field )                                                 \
            cp_lines_addf(out, key "=%d", entry->field);                                      \
    } while( 0 )

#define EMIT_BOOL(field, key)                                                                 \
    do                                                                                        \
    {                                                                                         \
        if( entry->field != defaults->field )                                                 \
            cp_lines_addf(out, key "=%s", entry->field ? "yes" : "no");                       \
    } while( 0 )

#define EMIT_OBJ(field, key)                                                                  \
    cp_emit_ref(ctx, out, key, CP_TYPE_OBJ, entry->field, defaults->field)

static void
emit_obj(
    struct CP_Ctx* ctx,
    const struct RSCache_Dat2ConfigObj* entry,
    const struct RSCache_Dat2ConfigObj* defaults,
    struct CP_Lines* out)
{
    /* The decoder seeds the name with "null" rather than leaving it absent, so a
     * record that genuinely says "null" and one that says nothing are the same
     * state by the time it gets here — see the header's note. */
    if( entry->name && strcmp(entry->name, "null") != 0 )
        cp_lines_add_str(out, "name", entry->name);
    if( entry->examine )
        cp_lines_add_str(out, "desc", entry->examine);

    EMIT_INT(inventory_model_id, "model");
    EMIT_INT(zoom2d, "2dzoom");
    EMIT_INT(xan2d, "2dxan");
    EMIT_INT(yan2d, "2dyan");
    EMIT_INT(zan2d, "2dzan");
    EMIT_INT(offset_x2d, "2dxof");
    EMIT_INT(offset_y2d, "2dyof");
    EMIT_INT(resize_x, "resizex");
    EMIT_INT(resize_y, "resizey");
    EMIT_INT(resize_z, "resizez");
    EMIT_INT(cost, "cost");
    EMIT_INT(stacking_behaviour, "stackable");
    EMIT_BOOL(tradeable, "tradeable");
    EMIT_BOOL(ge_tradeable, "getradeable");
    EMIT_BOOL(is_members, "members");
    EMIT_INT(wearpos_1, "wearpos");
    EMIT_INT(wearpos_2, "wearpos2");
    EMIT_INT(wearpos_3, "wearpos3");
    EMIT_INT(ambient, "ambient");
    EMIT_INT(contrast, "contrast");
    EMIT_INT(team, "team");
    EMIT_INT(weight, "weight");
    EMIT_INT(category, "category");
    EMIT_INT(shift_click_drop_index, "shiftclickdrop");

    EMIT_INT(male_model_0, "manwear");
    EMIT_INT(male_model_1, "manwear2");
    EMIT_INT(male_model_2, "manwear3");
    EMIT_INT(male_offset, "manwearoff");
    EMIT_INT(male_head_model, "manhead");
    EMIT_INT(male_head_model_2, "manhead2");
    EMIT_INT(female_model_0, "womanwear");
    EMIT_INT(female_model_1, "womanwear2");
    EMIT_INT(female_model_2, "womanwear3");
    EMIT_INT(female_offset, "womanwearoff");
    EMIT_INT(female_head_model, "womanhead");
    EMIT_INT(female_head_model_2, "womanhead2");

    EMIT_OBJ(noted_id, "certlink");
    EMIT_OBJ(noted_template, "certtemplate");
    EMIT_OBJ(bought_id, "boughtlink");
    EMIT_OBJ(bought_template_id, "boughttemplate");
    EMIT_OBJ(placeholder_id, "placeholderlink");
    EMIT_OBJ(placeholder_template_id, "placeholdertemplate");

    /* The stack-variant table: at `count` of this item, draw that item's model. */
    for( int i = 0; i < 10; i++ )
    {
        if( entry->count_obj[i] || entry->count_co[i] )
            cp_lines_addf(out, "countobj%d=%s,%d", i + 1,
                          cp_name_ensure(ctx, CP_TYPE_OBJ, entry->count_obj[i]),
                          entry->count_co[i]);
    }

    cp_emit_entity_ops(out, entry->actions, 5, &entry->entity_ops);
    for( int i = 0; i < 5; i++ )
    {
        if( entry->if_actions[i] )
        {
            char key[16];
            snprintf(key, sizeof(key), "ifop%d", i + 1);
            cp_lines_add_str(out, key, entry->if_actions[i]);
        }
    }
    for( int i = 0; i < 5; i++ )
    {
        if( !entry->sub_actions[i] )
            continue;
        for( int j = 0; j < 20; j++ )
        {
            if( !entry->sub_actions[i][j] )
                continue;
            char buf[1024];
            snprintf(buf, sizeof(buf), "%d,%d,%s", i + 1, j + 1, entry->sub_actions[i][j]);
            cp_lines_add_str(out, "subaction", buf);
        }
    }

    cp_emit_recols(out, entry->recolors_from, entry->recolors_to, entry->recolor_count, "recol");
    cp_emit_recols(out, entry->retextures_from, entry->retextures_to, entry->retexture_count,
                   "retex");
    cp_emit_params(ctx, out, &entry->params);
}

#undef EMIT_INT
#undef EMIT_BOOL
#undef EMIT_OBJ

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
    if( !entry )
        return 0;
    struct RSCache_Dat2ConfigObj* defaults = RSCache_Dat2ConfigObjNewDecodeProfile(
        &ctx->profile, (char*)cp_empty_record, (int)sizeof(cp_empty_record));
    if( !defaults )
    {
        RSCache_Dat2ConfigObjFree(entry);
        return 0;
    }

    emit_obj(ctx, entry, defaults, out);

    RSCache_Dat2ConfigObjFree(defaults);
    RSCache_Dat2ConfigObjFree(entry);
    return 1;
}

uint32_t
cp_pack_obj(
    struct CP_Ctx* ctx,
    int id,
    const struct CP_Config* config,
    uint8_t* out,
    uint32_t out_capacity)
{
    struct RSCache_Dat2ConfigObj* entry = RSCache_Dat2ConfigObjNewDecodeProfile(
        &ctx->profile, (char*)cp_empty_record, (int)sizeof(cp_empty_record));
    if( !entry )
        return 0;

    struct CP_IntList recol_s = { 0 }, recol_d = { 0 };
    struct CP_IntList retex_s = { 0 }, retex_d = { 0 };
    uint32_t written = 0;

    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;
        int ok = 1;
        int index;
        char scratch[2048];
        char* fields[3];

        if( strcmp(key, "name") == 0 )
        {
            char buf[1024];
            cp_unescape(value, buf, sizeof(buf));
            free(entry->name);
            entry->name = strdup(buf);
        }
        else if( strcmp(key, "desc") == 0 )
        {
            char buf[4096];
            cp_unescape(value, buf, sizeof(buf));
            free(entry->examine);
            entry->examine = strdup(buf);
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
                ok = cp_resolve_ref(ctx, CP_TYPE_OBJ, fields[0], &entry->count_obj[index]) &&
                     cp_parse_int(fields[1], &entry->count_co[index]);
            }
        }
        else if( (index = cp_indexed_key(key, "ifop")) >= 0 )
        {
            if( index >= 5 )
            {
                ok = 0;
            }
            else
            {
                char buf[1024];
                cp_unescape(value, buf, sizeof(buf));
                free(entry->if_actions[index]);
                entry->if_actions[index] = strdup(buf);
            }
        }
        else if( strcmp(key, "subaction") == 0 )
        {
            char buf[1024];
            cp_unescape(value, buf, sizeof(buf));
            /* `<action>,<sub>,<text>` — the text is the tail, since a menu entry
             * may contain commas. */
            char* first = strchr(buf, ',');
            char* second = first ? strchr(first + 1, ',') : NULL;
            int action = 0, sub = 0;
            if( !second )
            {
                ok = 0;
            }
            else
            {
                *first = '\0';
                *second = '\0';
                ok = cp_parse_int(buf, &action) && cp_parse_int(first + 1, &sub) &&
                     action >= 1 && action <= 5 && sub >= 1 && sub <= 20;
                if( ok )
                {
                    if( !entry->sub_actions[action - 1] )
                        entry->sub_actions[action - 1] = calloc(20, sizeof(char*));
                    if( entry->sub_actions[action - 1] )
                    {
                        free(entry->sub_actions[action - 1][sub - 1]);
                        entry->sub_actions[action - 1][sub - 1] = strdup(second + 1);
                    }
                    else
                        ok = 0;
                }
            }
        }
        else if( cp_parse_entity_op(entry->actions, 5, &entry->entity_ops, key, value) )
        {
            /* consumed */
        }
        else if( strcmp(key, "param") == 0 )
            ok = cp_parse_param(ctx, &entry->params, value);
        else if( strcmp(key, "model") == 0 )
            ok = cp_parse_int(value, &entry->inventory_model_id);
        else if( strcmp(key, "2dzoom") == 0 )
            ok = cp_parse_int(value, &entry->zoom2d);
        else if( strcmp(key, "2dxan") == 0 )
            ok = cp_parse_int(value, &entry->xan2d);
        else if( strcmp(key, "2dyan") == 0 )
            ok = cp_parse_int(value, &entry->yan2d);
        else if( strcmp(key, "2dzan") == 0 )
            ok = cp_parse_int(value, &entry->zan2d);
        else if( strcmp(key, "2dxof") == 0 )
            ok = cp_parse_int(value, &entry->offset_x2d);
        else if( strcmp(key, "2dyof") == 0 )
            ok = cp_parse_int(value, &entry->offset_y2d);
        else if( strcmp(key, "resizex") == 0 )
            ok = cp_parse_int(value, &entry->resize_x);
        else if( strcmp(key, "resizey") == 0 )
            ok = cp_parse_int(value, &entry->resize_y);
        else if( strcmp(key, "resizez") == 0 )
            ok = cp_parse_int(value, &entry->resize_z);
        else if( strcmp(key, "cost") == 0 )
            ok = cp_parse_int(value, &entry->cost);
        else if( strcmp(key, "stackable") == 0 )
            ok = cp_parse_int(value, &entry->stacking_behaviour);
        else if( strcmp(key, "tradeable") == 0 )
            ok = cp_parse_bool(value, &entry->tradeable);
        else if( strcmp(key, "getradeable") == 0 )
            ok = cp_parse_bool(value, &entry->ge_tradeable);
        else if( strcmp(key, "members") == 0 )
            ok = cp_parse_bool(value, &entry->is_members);
        else if( strcmp(key, "wearpos") == 0 )
            ok = cp_parse_int(value, &entry->wearpos_1);
        else if( strcmp(key, "wearpos2") == 0 )
            ok = cp_parse_int(value, &entry->wearpos_2);
        else if( strcmp(key, "wearpos3") == 0 )
            ok = cp_parse_int(value, &entry->wearpos_3);
        else if( strcmp(key, "ambient") == 0 )
            ok = cp_parse_int(value, &entry->ambient);
        else if( strcmp(key, "contrast") == 0 )
            ok = cp_parse_int(value, &entry->contrast);
        else if( strcmp(key, "team") == 0 )
            ok = cp_parse_int(value, &entry->team);
        else if( strcmp(key, "weight") == 0 )
            ok = cp_parse_int(value, &entry->weight);
        else if( strcmp(key, "category") == 0 )
            ok = cp_parse_int(value, &entry->category);
        else if( strcmp(key, "shiftclickdrop") == 0 )
            ok = cp_parse_int(value, &entry->shift_click_drop_index);
        else if( strcmp(key, "manwear") == 0 )
            ok = cp_parse_int(value, &entry->male_model_0);
        else if( strcmp(key, "manwear2") == 0 )
            ok = cp_parse_int(value, &entry->male_model_1);
        else if( strcmp(key, "manwear3") == 0 )
            ok = cp_parse_int(value, &entry->male_model_2);
        else if( strcmp(key, "manwearoff") == 0 )
            ok = cp_parse_int(value, &entry->male_offset);
        else if( strcmp(key, "manhead") == 0 )
            ok = cp_parse_int(value, &entry->male_head_model);
        else if( strcmp(key, "manhead2") == 0 )
            ok = cp_parse_int(value, &entry->male_head_model_2);
        else if( strcmp(key, "womanwear") == 0 )
            ok = cp_parse_int(value, &entry->female_model_0);
        else if( strcmp(key, "womanwear2") == 0 )
            ok = cp_parse_int(value, &entry->female_model_1);
        else if( strcmp(key, "womanwear3") == 0 )
            ok = cp_parse_int(value, &entry->female_model_2);
        else if( strcmp(key, "womanwearoff") == 0 )
            ok = cp_parse_int(value, &entry->female_offset);
        else if( strcmp(key, "womanhead") == 0 )
            ok = cp_parse_int(value, &entry->female_head_model);
        else if( strcmp(key, "womanhead2") == 0 )
            ok = cp_parse_int(value, &entry->female_head_model_2);
        else if( strcmp(key, "certlink") == 0 )
            ok = cp_resolve_ref(ctx, CP_TYPE_OBJ, value, &entry->noted_id);
        else if( strcmp(key, "certtemplate") == 0 )
            ok = cp_resolve_ref(ctx, CP_TYPE_OBJ, value, &entry->noted_template);
        else if( strcmp(key, "boughtlink") == 0 )
            ok = cp_resolve_ref(ctx, CP_TYPE_OBJ, value, &entry->bought_id);
        else if( strcmp(key, "boughttemplate") == 0 )
            ok = cp_resolve_ref(ctx, CP_TYPE_OBJ, value, &entry->bought_template_id);
        else if( strcmp(key, "placeholderlink") == 0 )
            ok = cp_resolve_ref(ctx, CP_TYPE_OBJ, value, &entry->placeholder_id);
        else if( strcmp(key, "placeholdertemplate") == 0 )
            ok = cp_resolve_ref(ctx, CP_TYPE_OBJ, value, &entry->placeholder_template_id);
        else if( strncmp(key, "recol", 5) == 0 || strncmp(key, "retex", 5) == 0 )
        {
            /* collected below */
        }
        else
            cp_warn(ctx, &ctx->warn_unknown_key, "obj [%s]: unknown key %s",
                    config->debugname, key);

        if( !ok )
        {
            fprintf(stderr, "cachepack: obj [%s]: bad value for %s\n", config->debugname, key);
            goto done;
        }
    }

    if( !cp_collect_pairs(config, "recol", &recol_s, &recol_d) ||
        !cp_collect_pairs(config, "retex", &retex_s, &retex_d) )
    {
        fprintf(stderr, "cachepack: obj [%s]: mismatched recolour pairs\n", config->debugname);
        goto done;
    }

    entry->recolors_from = recol_s.items;
    entry->recolors_to = recol_d.items;
    entry->recolor_count = recol_s.count;
    entry->retextures_from = retex_s.items;
    entry->retextures_to = retex_d.items;
    entry->retexture_count = retex_s.count;

    written = RSCache_Dat2ConfigObjEncodeProfile(&ctx->profile, entry, out, out_capacity);

done:
    entry->recolors_from = entry->recolors_to = NULL;
    entry->retextures_from = entry->retextures_to = NULL;
    RSCache_Dat2ConfigObjFree(entry);
    cp_intlist_free(&recol_s);
    cp_intlist_free(&recol_d);
    cp_intlist_free(&retex_s);
    cp_intlist_free(&retex_d);
    return written;
}
