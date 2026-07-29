#include "cachepack.h"

#include "datatypes/dat2_config_npc.h"

#include <stdlib.h>
#include <string.h>

/*
 * NPCs.
 *
 * Everything the library's decoder keeps is written out. Where a field has a
 * decode default the line is omitted, and the default is not hardcoded here: a
 * reference record is decoded from a bare terminator and every field compared
 * against it. So a default that changes in the library changes here too, instead
 * of quietly starting to emit a line that was previously implied.
 *
 * The type is marked lossy in the register for one reason, recorded in the
 * library's own header: opcodes 93, 107 and 109 *clear* flags the decoder never
 * sets true, so a record carrying them cannot be told from one that did not.
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

#define EMIT_SEQ(field, key)                                                                  \
    cp_emit_ref(ctx, out, key, CP_TYPE_SEQ, entry->field, defaults->field)

static void
emit_npc(
    struct CP_Ctx* ctx,
    const struct RSCache_Dat2ConfigNpc* entry,
    const struct RSCache_Dat2ConfigNpc* defaults,
    struct CP_Lines* out)
{
    if( entry->name )
        cp_lines_add_str(out, "name", entry->name);

    for( int i = 0; i < entry->models_count; i++ )
        cp_lines_addf(out, "model%d=%d", i + 1, entry->models[i]);
    for( int i = 0; i < entry->chathead_models_count; i++ )
        cp_lines_addf(out, "head%d=%d", i + 1, entry->chathead_models[i]);

    EMIT_INT(size, "size");
    EMIT_SEQ(standing_animation, "readyanim");
    EMIT_SEQ(walking_animation, "walkanim");
    EMIT_SEQ(idle_rotate_left_animation, "idleleftanim");
    EMIT_SEQ(idle_rotate_right_animation, "idlerightanim");
    EMIT_SEQ(rotate180_animation, "walkanim_b");
    EMIT_SEQ(rotate_left_animation, "walkanim_l");
    EMIT_SEQ(rotate_right_animation, "walkanim_r");
    EMIT_SEQ(run_animation, "runanim");
    EMIT_SEQ(run_rotate180_animation, "runanim_b");
    EMIT_SEQ(run_rotate_left_animation, "runanim_l");
    EMIT_SEQ(run_rotate_right_animation, "runanim_r");
    EMIT_SEQ(crawl_animation, "crawlanim");
    EMIT_SEQ(crawl_rotate180_animation, "crawlanim_b");
    EMIT_SEQ(crawl_rotate_left_animation, "crawlanim_l");
    EMIT_SEQ(crawl_rotate_right_animation, "crawlanim_r");

    cp_emit_entity_ops(out, entry->actions, 5, &entry->entity_ops);

    cp_emit_recols(out, entry->recolor_to_find, entry->recolor_to_replace, entry->recolor_count,
                   "recol");
    cp_emit_recols(out, entry->retexture_to_find, entry->retexture_to_replace,
                   entry->retexture_count, "retex");

    EMIT_BOOL(is_minimap_visible, "minimap");
    EMIT_INT(combat_level, "vislevel");
    EMIT_INT(width_scale, "resizeh");
    EMIT_INT(height_scale, "resizev");
    EMIT_BOOL(has_render_priority, "alwaysontop");
    EMIT_INT(render_priority, "renderpriority");
    EMIT_INT(ambient, "ambient");
    EMIT_INT(contrast, "contrast");

    /* A head icon is an (archive, sprite index) pair per set bit of a bitfield, so
     * both halves go on one line — splitting them into two indexed keys would let a
     * hand-edit produce a pair that names a sprite in the wrong archive. */
    for( int i = 0; i < entry->head_icon_count; i++ )
        cp_lines_addf(out, "headicon%d=%d,%d", i + 1, entry->head_icon_archive_ids[i],
                      entry->head_icon_sprite_index[i]);

    EMIT_INT(rotation_speed, "turnspeed");
    cp_emit_ref(ctx, out, "multivarbit", CP_TYPE_VARBIT, entry->varbit_id, defaults->varbit_id);
    cp_emit_ref(ctx, out, "multivarp", CP_TYPE_VARP, entry->varp_index, defaults->varp_index);
    for( int i = 0; i < entry->configs_count; i++ )
    {
        /* A transform target of -1 is "no npc in this slot", and the slot still
         * counts — the varbit's value indexes this list positionally. */
        if( entry->configs[i] < 0 )
            cp_lines_addf(out, "multinpc%d=-1", i + 1);
        else
            cp_lines_addf(out, "multinpc%d=%s", i + 1,
                          cp_name_ensure(ctx, CP_TYPE_NPC, entry->configs[i]));
    }

    EMIT_BOOL(is_interactable, "interactable");
    EMIT_BOOL(rotation_flag, "rotationflag");
    EMIT_BOOL(is_pet, "pet");
    EMIT_BOOL(low_priority_follower_ops, "lowpriorityops");
    EMIT_INT(height, "height");
    EMIT_INT(category, "category");
    for( int i = 0; i < 6; i++ )
    {
        if( entry->stats[i] != defaults->stats[i] )
            cp_lines_addf(out, "stat%d=%d", i + 1, entry->stats[i]);
    }
    EMIT_INT(bas_type_id, "bastype");
    EMIT_INT(footprint_size, "footprintsize");
    EMIT_BOOL(unknown1, "opcode129");
    EMIT_BOOL(can_hide_for_overlap, "hideforoverlap");
    EMIT_INT(overlap_tint_hsl, "overlaptint");
    EMIT_BOOL(idle_anim_restart, "idleanimrestart");
    EMIT_BOOL(zbuf, "zbuf");

    cp_emit_params(ctx, out, &entry->params);
}

#undef EMIT_INT
#undef EMIT_BOOL
#undef EMIT_SEQ

int
cp_unpack_npc(
    struct CP_Ctx* ctx,
    int id,
    const uint8_t* record,
    int record_size,
    struct CP_Lines* out)
{
    struct RSCache_Dat2ConfigNpc* entry =
        RSCache_Dat2ConfigNpcNewDecodeProfile(&ctx->profile, (char*)record, record_size);
    if( !entry )
        return 0;
    if( entry->_consumed != record_size )
        cp_warn(ctx, &ctx->warn_short_decode, "npc %d: consumed %d of %d bytes", id,
                entry->_consumed, record_size);

    struct RSCache_Dat2ConfigNpc* defaults = RSCache_Dat2ConfigNpcNewDecodeProfile(
        &ctx->profile, (char*)cp_empty_record, (int)sizeof(cp_empty_record));
    if( !defaults )
    {
        RSCache_Dat2ConfigNpcFree(entry);
        return 0;
    }

    emit_npc(ctx, entry, defaults, out);

    RSCache_Dat2ConfigNpcFree(defaults);
    RSCache_Dat2ConfigNpcFree(entry);
    return 1;
}

uint32_t
cp_pack_npc(
    struct CP_Ctx* ctx,
    int id,
    const struct CP_Config* config,
    uint8_t* out,
    uint32_t out_capacity)
{
    struct RSCache_Dat2ConfigNpc* entry = RSCache_Dat2ConfigNpcNewDecodeProfile(
        &ctx->profile, (char*)cp_empty_record, (int)sizeof(cp_empty_record));
    if( !entry )
        return 0;

    struct CP_IntList models = { 0 }, heads = { 0 }, transforms = { 0 };
    struct CP_IntList recol_s = { 0 }, recol_d = { 0 };
    struct CP_IntList retex_s = { 0 }, retex_d = { 0 };
    struct CP_IntList icon_archive = { 0 }, icon_sprite = { 0 };
    uint32_t written = 0;

    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;
        int ok = 1;
        int index;
        int tmp = 0;

        if( strcmp(key, "name") == 0 )
        {
            char buf[1024];
            cp_unescape(value, buf, sizeof(buf));
            free(entry->name);
            entry->name = strdup(buf);
        }
        else if( (index = cp_indexed_key(key, "model")) >= 0 )
        {
            ok = cp_parse_int(value, &tmp);
            cp_intlist_set(&models, index, tmp);
        }
        else if( (index = cp_indexed_key(key, "head")) >= 0 && strncmp(key, "headicon", 8) != 0 )
        {
            ok = cp_parse_int(value, &tmp);
            cp_intlist_set(&heads, index, tmp);
        }
        else if( (index = cp_indexed_key(key, "headicon")) >= 0 )
        {
            char scratch[64];
            char* fields[2];
            if( strlen(value) >= sizeof(scratch) || cp_split(value, scratch, fields, 2) != 2 )
            {
                ok = 0;
            }
            else
            {
                int archive = 0, sprite = 0;
                ok = cp_parse_int(fields[0], &archive) && cp_parse_int(fields[1], &sprite);
                cp_intlist_set(&icon_archive, index, archive);
                cp_intlist_set(&icon_sprite, index, sprite);
            }
        }
        else if( (index = cp_indexed_key(key, "multinpc")) >= 0 )
        {
            if( strcmp(value, "-1") == 0 )
                cp_intlist_set(&transforms, index, -1);
            else
            {
                ok = cp_resolve_ref(ctx, CP_TYPE_NPC, value, &tmp);
                cp_intlist_set(&transforms, index, tmp);
            }
        }
        else if( (index = cp_indexed_key(key, "stat")) >= 0 )
        {
            if( index >= 6 )
                ok = 0;
            else
                ok = cp_parse_int(value, &entry->stats[index]);
        }
        else if( cp_parse_entity_op(entry->actions, 5, &entry->entity_ops, key, value) )
        {
            /* consumed */
        }
        else if( strcmp(key, "param") == 0 )
            ok = cp_parse_param(ctx, &entry->params, value);
        else if( strcmp(key, "size") == 0 )
            ok = cp_parse_int(value, &entry->size);
        else if( strcmp(key, "readyanim") == 0 )
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->standing_animation);
        else if( strcmp(key, "walkanim") == 0 )
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->walking_animation);
        else if( strcmp(key, "idleleftanim") == 0 )
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->idle_rotate_left_animation);
        else if( strcmp(key, "idlerightanim") == 0 )
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->idle_rotate_right_animation);
        else if( strcmp(key, "walkanim_b") == 0 )
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->rotate180_animation);
        else if( strcmp(key, "walkanim_l") == 0 )
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->rotate_left_animation);
        else if( strcmp(key, "walkanim_r") == 0 )
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->rotate_right_animation);
        else if( strcmp(key, "runanim") == 0 )
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->run_animation);
        else if( strcmp(key, "runanim_b") == 0 )
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->run_rotate180_animation);
        else if( strcmp(key, "runanim_l") == 0 )
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->run_rotate_left_animation);
        else if( strcmp(key, "runanim_r") == 0 )
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->run_rotate_right_animation);
        else if( strcmp(key, "crawlanim") == 0 )
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->crawl_animation);
        else if( strcmp(key, "crawlanim_b") == 0 )
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->crawl_rotate180_animation);
        else if( strcmp(key, "crawlanim_l") == 0 )
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->crawl_rotate_left_animation);
        else if( strcmp(key, "crawlanim_r") == 0 )
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->crawl_rotate_right_animation);
        else if( strcmp(key, "minimap") == 0 )
            ok = cp_parse_bool(value, &entry->is_minimap_visible);
        else if( strcmp(key, "vislevel") == 0 )
            ok = cp_parse_int(value, &entry->combat_level);
        else if( strcmp(key, "resizeh") == 0 )
            ok = cp_parse_int(value, &entry->width_scale);
        else if( strcmp(key, "resizev") == 0 )
            ok = cp_parse_int(value, &entry->height_scale);
        else if( strcmp(key, "alwaysontop") == 0 )
            ok = cp_parse_bool(value, &entry->has_render_priority);
        else if( strcmp(key, "renderpriority") == 0 )
            ok = cp_parse_int(value, &entry->render_priority);
        else if( strcmp(key, "ambient") == 0 )
            ok = cp_parse_int(value, &entry->ambient);
        else if( strcmp(key, "contrast") == 0 )
            ok = cp_parse_int(value, &entry->contrast);
        else if( strcmp(key, "turnspeed") == 0 )
            ok = cp_parse_int(value, &entry->rotation_speed);
        else if( strcmp(key, "multivarbit") == 0 )
            ok = cp_resolve_ref(ctx, CP_TYPE_VARBIT, value, &entry->varbit_id);
        else if( strcmp(key, "multivarp") == 0 )
            ok = cp_resolve_ref(ctx, CP_TYPE_VARP, value, &entry->varp_index);
        else if( strcmp(key, "interactable") == 0 )
            ok = cp_parse_bool(value, &entry->is_interactable);
        else if( strcmp(key, "rotationflag") == 0 )
            ok = cp_parse_bool(value, &entry->rotation_flag);
        else if( strcmp(key, "pet") == 0 )
            ok = cp_parse_bool(value, &entry->is_pet);
        else if( strcmp(key, "lowpriorityops") == 0 )
            ok = cp_parse_bool(value, &entry->low_priority_follower_ops);
        else if( strcmp(key, "height") == 0 )
            ok = cp_parse_int(value, &entry->height);
        else if( strcmp(key, "category") == 0 )
            ok = cp_parse_int(value, &entry->category);
        else if( strcmp(key, "bastype") == 0 )
            ok = cp_parse_int(value, &entry->bas_type_id);
        else if( strcmp(key, "footprintsize") == 0 )
            ok = cp_parse_int(value, &entry->footprint_size);
        else if( strcmp(key, "opcode129") == 0 )
            ok = cp_parse_bool(value, &entry->unknown1);
        else if( strcmp(key, "hideforoverlap") == 0 )
            ok = cp_parse_bool(value, &entry->can_hide_for_overlap);
        else if( strcmp(key, "overlaptint") == 0 )
            ok = cp_parse_int(value, &entry->overlap_tint_hsl);
        else if( strcmp(key, "idleanimrestart") == 0 )
            ok = cp_parse_bool(value, &entry->idle_anim_restart);
        else if( strcmp(key, "zbuf") == 0 )
            ok = cp_parse_bool(value, &entry->zbuf);
        else if( strncmp(key, "recol", 5) == 0 || strncmp(key, "retex", 5) == 0 )
        {
            /* collected below */
        }
        else
            cp_warn(ctx, &ctx->warn_unknown_key, "npc [%s]: unknown key %s",
                    config->debugname, key);

        if( !ok )
        {
            fprintf(stderr, "cachepack: npc [%s]: bad value for %s\n", config->debugname, key);
            goto done;
        }
    }

    if( !cp_collect_pairs(config, "recol", &recol_s, &recol_d) ||
        !cp_collect_pairs(config, "retex", &retex_s, &retex_d) )
    {
        fprintf(stderr, "cachepack: npc [%s]: mismatched recolour pairs\n", config->debugname);
        goto done;
    }

    entry->models = models.items;
    entry->models_count = models.count;
    entry->chathead_models = heads.items;
    entry->chathead_models_count = heads.count;
    entry->configs = transforms.items;
    entry->configs_count = transforms.count;
    entry->head_icon_archive_ids = icon_archive.items;
    entry->head_icon_count = icon_archive.count;

    entry->recolor_to_find = recol_s.items;
    entry->recolor_to_replace = recol_d.items;
    entry->recolor_count = recol_s.count;
    entry->retexture_to_find = retex_s.items;
    entry->retexture_to_replace = retex_d.items;
    entry->retexture_count = retex_s.count;

    if( icon_sprite.count > 0 )
    {
        short* sprites = malloc((size_t)icon_sprite.count * sizeof(short));
        if( !sprites )
            goto done;
        for( int i = 0; i < icon_sprite.count; i++ )
            sprites[i] = (short)icon_sprite.items[i];
        entry->head_icon_sprite_index = sprites;
    }

    written = RSCache_Dat2ConfigNpcEncodeProfile(&ctx->profile, entry, out, out_capacity);

done:
    /* The int lists own their arrays; hand ownership back so the free below does
     * not release them twice. `head_icon_sprite_index` is the one array this
     * function allocates itself, and it is freed with the struct. */
    entry->models = NULL;
    entry->chathead_models = NULL;
    entry->configs = NULL;
    entry->head_icon_archive_ids = NULL;
    entry->recolor_to_find = entry->recolor_to_replace = NULL;
    entry->retexture_to_find = entry->retexture_to_replace = NULL;
    RSCache_Dat2ConfigNpcFree(entry);
    cp_intlist_free(&models);
    cp_intlist_free(&heads);
    cp_intlist_free(&transforms);
    cp_intlist_free(&recol_s);
    cp_intlist_free(&recol_d);
    cp_intlist_free(&retex_s);
    cp_intlist_free(&retex_d);
    cp_intlist_free(&icon_archive);
    cp_intlist_free(&icon_sprite);
    return written;
}
