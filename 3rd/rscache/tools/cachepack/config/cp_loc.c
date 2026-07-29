#include "cachepack.h"

#include "datatypes/dat2_config_loc.h"

#include <stdlib.h>
#include <string.h>

/*
 * Locs (scenery).
 *
 * The widest record in the cache and the most lossy: the library's decoder consumes
 * roughly 25 opcodes without storing them, collapses three groups of aliased
 * opcodes, and normalises an action of literal "hidden" to absent. Everything below
 * is the part it does keep, which is also the part the client reads — so the text
 * is a complete view of the loc as the game sees it, and an incomplete copy of the
 * file. The register marks it lossy for that reason.
 *
 * The models list is the one genuinely awkward shape. A loc carries parallel
 * `shapes` / `models` / `lengths` arrays: one *shape* (wall, corner, roof, centre
 * piece, ...) owning a list of model ids, because the map's shape selector picks
 * which list to draw. It is written one line per shape, models comma-joined.
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

static void
emit_loc(
    struct CP_Ctx* ctx,
    const struct RSCache_Dat2ConfigLoc* entry,
    const struct RSCache_Dat2ConfigLoc* defaults,
    struct CP_Lines* out)
{
    if( entry->name )
        cp_lines_add_str(out, "name", entry->name);
    if( entry->desc )
        cp_lines_add_str(out, "desc", entry->desc);

    /*
     * Two forms, and the difference is on the wire, not a presentation choice.
     * Opcode 1 gives every model its own shape; opcode 5 gives one flat model list
     * that applies whatever shape the map asks for, and the decoder marks that case
     * by leaving `shapes` NULL with a count of 1. Writing the second as `shape1=`
     * would re-encode it as opcode 1 and change what the client draws.
     */
    for( int s = 0; s < entry->shapes_and_model_count; s++ )
    {
        char buf[4096];
        int w = 0;
        buf[0] = '\0';
        for( int m = 0; m < entry->lengths[s]; m++ )
        {
            w += snprintf(buf + w, sizeof(buf) - (size_t)w, m ? ",%d" : "%d",
                          entry->models[s][m]);
            if( w >= (int)sizeof(buf) - 12 )
                break;
        }
        if( entry->shapes )
            cp_lines_addf(out, "shape%d=%d,%s", s + 1, entry->shapes[s], buf);
        else
            cp_lines_addf(out, "models=%s", buf);
    }

    EMIT_INT(size_x, "width");
    EMIT_INT(size_z, "length");
    EMIT_INT(blocks_walk, "blockwalk");
    EMIT_INT(blocks_projectiles, "blockrange");
    EMIT_INT(wall_width, "wallwidth");
    EMIT_INT(is_interactive, "active");
    EMIT_INT(contoured_ground, "contourground");
    EMIT_INT(contour_ground_type, "contourgroundtype");
    EMIT_INT(contour_ground_param, "contourgroundparam");
    EMIT_INT(sharelight, "sharelight");
    EMIT_INT(occlude, "occlude");
    cp_emit_ref(ctx, out, "anim", CP_TYPE_SEQ, entry->seq_id, defaults->seq_id);
    EMIT_INT(ambient, "ambient");
    EMIT_INT(contrast, "contrast");

    cp_emit_entity_ops(out, entry->actions, 10, &entry->entity_ops);

    cp_emit_recols(out, entry->recolors_from, entry->recolors_to, entry->recolor_count, "recol");
    cp_emit_recols(out, entry->retextures_from, entry->retextures_to, entry->retexture_count,
                   "retex");

    EMIT_INT(map_function_id, "mapfunction");
    EMIT_INT(map_scene_id, "mapscene");
    EMIT_INT(mirrored, "mirror");
    EMIT_BOOL(shadowed, "shadow");
    EMIT_INT(resize_x, "resizex");
    EMIT_INT(resize_height, "resizey");
    EMIT_INT(resize_z, "resizez");
    EMIT_INT(offset_x, "offsetx");
    EMIT_INT(offset_y, "offsety");
    EMIT_INT(offset_z, "offsetz");
    EMIT_INT(obstructs_ground, "forcedecor");
    EMIT_INT(break_routefinding, "breakroutefinding");
    EMIT_INT(support_items, "raiseobject");

    cp_emit_ref(ctx, out, "multivarbit", CP_TYPE_VARBIT, entry->transform_varbit,
                defaults->transform_varbit);
    cp_emit_ref(ctx, out, "multivarp", CP_TYPE_VARP, entry->transform_varp,
                defaults->transform_varp);
    for( int i = 0; i < entry->transform_count; i++ )
    {
        if( entry->transforms[i] < 0 )
            cp_lines_addf(out, "multiloc%d=-1", i + 1);
        else
            cp_lines_addf(out, "multiloc%d=%s", i + 1,
                          cp_name_ensure(ctx, CP_TYPE_LOC, entry->transforms[i]));
    }

    EMIT_INT(ambient_sound_id, "soundid");
    EMIT_INT(ambient_sound_distance, "sounddistance");
    EMIT_INT(ambient_sound_retain, "soundretain");
    EMIT_INT(ambient_sound_ticks_min, "soundmintick");
    EMIT_INT(ambient_sound_ticks_max, "soundmaxtick");
    for( int i = 0; i < entry->ambient_sound_id_count; i++ )
        cp_lines_addf(out, "soundrandom%d=%d", i + 1, entry->ambient_sound_ids[i]);

    EMIT_BOOL(seq_random_start, "randomanimstart");
    for( int i = 0; i < entry->random_seq_id_count; i++ )
        cp_lines_addf(out, "randomanim%d=%s,%d", i + 1,
                      cp_name_ensure(ctx, CP_TYPE_SEQ, entry->random_seq_ids[i]),
                      entry->random_seq_delays[i]);
    for( int i = 0; i < entry->campaign_id_count; i++ )
        cp_lines_addf(out, "campaign%d=%d", i + 1, entry->campaign_ids[i]);

    EMIT_INT(sound_distance_fade_curve, "sounddistancefade");
    EMIT_INT(sound_fade_in_curve, "soundfadeincurve");
    EMIT_INT(sound_fade_in_duration, "soundfadein");
    EMIT_INT(sound_fade_out_curve, "soundfadeoutcurve");
    EMIT_INT(sound_fade_out_duration, "soundfadeout");
    EMIT_BOOL(unknown1, "opcode94");
    EMIT_INT(sound_visibility, "soundvisibility");
    EMIT_INT(raise, "raise");

    cp_emit_params(ctx, out, &entry->params);
}

#undef EMIT_INT
#undef EMIT_BOOL

int
cp_unpack_loc(
    struct CP_Ctx* ctx,
    int id,
    const uint8_t* record,
    int record_size,
    struct CP_Lines* out)
{
    struct RSCache_Dat2ConfigLoc* entry =
        RSCache_Dat2ConfigLocNewDecodeProfile(&ctx->profile, (char*)record, record_size);
    if( !entry )
        return 0;
    if( entry->_consumed != record_size )
        cp_warn(ctx, &ctx->warn_short_decode, "loc %d: consumed %d of %d bytes", id,
                entry->_consumed, record_size);

    struct RSCache_Dat2ConfigLoc* defaults = RSCache_Dat2ConfigLocNewDecodeProfile(
        &ctx->profile, (char*)cp_empty_record, (int)sizeof(cp_empty_record));
    if( !defaults )
    {
        RSCache_Dat2ConfigLocFree(entry);
        return 0;
    }

    emit_loc(ctx, entry, defaults, out);

    RSCache_Dat2ConfigLocFree(defaults);
    RSCache_Dat2ConfigLocFree(entry);
    return 1;
}

/** Shapes arrive as `shapeN=<shape>,<model>,<model>...`, one line per shape. */
struct ShapeList
{
    int* shapes;
    int** models;
    int* lengths;
    int count;
    int capacity;
};

static int
shapes_set(
    struct ShapeList* list,
    int index,
    int shape,
    const int* models,
    int model_count)
{
    if( index < 0 )
        return 0;
    if( index >= list->capacity )
    {
        int next = list->capacity ? list->capacity : 4;
        while( next <= index )
            next *= 2;
        int* shapes = realloc(list->shapes, (size_t)next * sizeof(int));
        int** model_lists = realloc(list->models, (size_t)next * sizeof(int*));
        int* lengths = realloc(list->lengths, (size_t)next * sizeof(int));
        if( shapes )
            list->shapes = shapes;
        if( model_lists )
            list->models = model_lists;
        if( lengths )
            list->lengths = lengths;
        if( !shapes || !model_lists || !lengths )
            return 0;
        for( int i = list->capacity; i < next; i++ )
        {
            list->shapes[i] = 0;
            list->models[i] = NULL;
            list->lengths[i] = 0;
        }
        list->capacity = next;
    }
    int* copy = malloc((size_t)(model_count > 0 ? model_count : 1) * sizeof(int));
    if( !copy )
        return 0;
    memcpy(copy, models, (size_t)model_count * sizeof(int));
    free(list->models[index]);
    list->shapes[index] = shape;
    list->models[index] = copy;
    list->lengths[index] = model_count;
    if( index >= list->count )
        list->count = index + 1;
    return 1;
}

static void
shapes_free(struct ShapeList* list)
{
    for( int i = 0; i < list->capacity; i++ )
        free(list->models[i]);
    free(list->shapes);
    free(list->models);
    free(list->lengths);
    memset(list, 0, sizeof(*list));
}

uint32_t
cp_pack_loc(
    struct CP_Ctx* ctx,
    int id,
    const struct CP_Config* config,
    uint8_t* out,
    uint32_t out_capacity)
{
    struct RSCache_Dat2ConfigLoc* entry = RSCache_Dat2ConfigLocNewDecodeProfile(
        &ctx->profile, (char*)cp_empty_record, (int)sizeof(cp_empty_record));
    if( !entry )
        return 0;

    struct ShapeList shapes = { 0 };
    struct CP_IntList flat_models = { 0 };
    int have_flat_models = 0;
    struct CP_IntList transforms = { 0 };
    struct CP_IntList sounds = { 0 };
    struct CP_IntList campaigns = { 0 };
    struct CP_IntList random_seq = { 0 }, random_delay = { 0 };
    struct CP_IntList recol_s = { 0 }, recol_d = { 0 };
    struct CP_IntList retex_s = { 0 }, retex_d = { 0 };
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
        else if( strcmp(key, "desc") == 0 )
        {
            char buf[4096];
            cp_unescape(value, buf, sizeof(buf));
            free(entry->desc);
            entry->desc = strdup(buf);
        }
        else if( (index = cp_indexed_key(key, "shape")) >= 0 )
        {
            char big[8192];
            char* parts[256];
            if( strlen(value) >= sizeof(big) )
            {
                ok = 0;
            }
            else
            {
                int n = cp_split(value, big, parts, 256);
                int shape = 0;
                ok = n >= 1 && cp_parse_int(parts[0], &shape);
                int models[255];
                int model_count = 0;
                for( int f = 1; f < n && ok; f++ )
                    ok = cp_parse_int(parts[f], &models[model_count++]);
                if( ok )
                    ok = shapes_set(&shapes, index, shape, models, model_count);
            }
        }
        else if( strcmp(key, "models") == 0 )
        {
            char big[8192];
            char* parts[256];
            if( strlen(value) >= sizeof(big) )
            {
                ok = 0;
            }
            else
            {
                int n = cp_split(value, big, parts, 256);
                for( int f = 0; f < n && ok; f++ )
                {
                    int model = 0;
                    ok = cp_parse_int(parts[f], &model);
                    cp_intlist_push(&flat_models, model);
                }
                have_flat_models = 1;
            }
        }
        else if( (index = cp_indexed_key(key, "multiloc")) >= 0 )
        {
            if( strcmp(value, "-1") == 0 )
                cp_intlist_set(&transforms, index, -1);
            else
            {
                ok = cp_resolve_ref(ctx, CP_TYPE_LOC, value, &tmp);
                cp_intlist_set(&transforms, index, tmp);
            }
        }
        else if( (index = cp_indexed_key(key, "soundrandom")) >= 0 )
        {
            ok = cp_parse_int(value, &tmp);
            cp_intlist_set(&sounds, index, tmp);
        }
        else if( (index = cp_indexed_key(key, "campaign")) >= 0 )
        {
            ok = cp_parse_int(value, &tmp);
            cp_intlist_set(&campaigns, index, tmp);
        }
        else if( (index = cp_indexed_key(key, "randomanim")) >= 0 )
        {
            char scratch[512];
            char* fields[2];
            if( strlen(value) >= sizeof(scratch) || cp_split(value, scratch, fields, 2) != 2 )
            {
                ok = 0;
            }
            else
            {
                int delay = 0;
                ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, fields[0], &tmp) &&
                     cp_parse_int(fields[1], &delay);
                cp_intlist_set(&random_seq, index, tmp);
                cp_intlist_set(&random_delay, index, delay);
            }
        }
        else if( cp_parse_entity_op(entry->actions, 10, &entry->entity_ops, key, value) )
        {
            /* consumed */
        }
        else if( strcmp(key, "param") == 0 )
            ok = cp_parse_param(ctx, &entry->params, value);
        else if( strcmp(key, "width") == 0 )
            ok = cp_parse_int(value, &entry->size_x);
        else if( strcmp(key, "length") == 0 )
            ok = cp_parse_int(value, &entry->size_z);
        else if( strcmp(key, "blockwalk") == 0 )
            ok = cp_parse_int(value, &entry->blocks_walk);
        else if( strcmp(key, "blockrange") == 0 )
            ok = cp_parse_int(value, &entry->blocks_projectiles);
        else if( strcmp(key, "wallwidth") == 0 )
            ok = cp_parse_int(value, &entry->wall_width);
        else if( strcmp(key, "active") == 0 )
            ok = cp_parse_int(value, &entry->is_interactive);
        else if( strcmp(key, "contourground") == 0 )
            ok = cp_parse_int(value, &entry->contoured_ground);
        else if( strcmp(key, "contourgroundtype") == 0 )
            ok = cp_parse_int(value, &entry->contour_ground_type);
        else if( strcmp(key, "contourgroundparam") == 0 )
            ok = cp_parse_int(value, &entry->contour_ground_param);
        else if( strcmp(key, "sharelight") == 0 )
            ok = cp_parse_int(value, &entry->sharelight);
        else if( strcmp(key, "occlude") == 0 )
            ok = cp_parse_int(value, &entry->occlude);
        else if( strcmp(key, "anim") == 0 )
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->seq_id);
        else if( strcmp(key, "ambient") == 0 )
            ok = cp_parse_int(value, &entry->ambient);
        else if( strcmp(key, "contrast") == 0 )
            ok = cp_parse_int(value, &entry->contrast);
        else if( strcmp(key, "mapfunction") == 0 )
            ok = cp_parse_int(value, &entry->map_function_id);
        else if( strcmp(key, "mapscene") == 0 )
            ok = cp_parse_int(value, &entry->map_scene_id);
        else if( strcmp(key, "mirror") == 0 )
            ok = cp_parse_int(value, &entry->mirrored);
        else if( strcmp(key, "shadow") == 0 )
        {
            /* The struct stores this one as an int, not a bool. */
            bool flag = false;
            ok = cp_parse_bool(value, &flag);
            entry->shadowed = flag;
        }
        else if( strcmp(key, "resizex") == 0 )
            ok = cp_parse_int(value, &entry->resize_x);
        else if( strcmp(key, "resizey") == 0 )
            ok = cp_parse_int(value, &entry->resize_height);
        else if( strcmp(key, "resizez") == 0 )
            ok = cp_parse_int(value, &entry->resize_z);
        else if( strcmp(key, "offsetx") == 0 )
            ok = cp_parse_int(value, &entry->offset_x);
        else if( strcmp(key, "offsety") == 0 )
            ok = cp_parse_int(value, &entry->offset_y);
        else if( strcmp(key, "offsetz") == 0 )
            ok = cp_parse_int(value, &entry->offset_z);
        else if( strcmp(key, "forcedecor") == 0 )
            ok = cp_parse_int(value, &entry->obstructs_ground);
        else if( strcmp(key, "breakroutefinding") == 0 )
            ok = cp_parse_int(value, &entry->break_routefinding);
        else if( strcmp(key, "raiseobject") == 0 )
            ok = cp_parse_int(value, &entry->support_items);
        else if( strcmp(key, "multivarbit") == 0 )
            ok = cp_resolve_ref(ctx, CP_TYPE_VARBIT, value, &entry->transform_varbit);
        else if( strcmp(key, "multivarp") == 0 )
            ok = cp_resolve_ref(ctx, CP_TYPE_VARP, value, &entry->transform_varp);
        else if( strcmp(key, "soundid") == 0 )
            ok = cp_parse_int(value, &entry->ambient_sound_id);
        else if( strcmp(key, "sounddistance") == 0 )
            ok = cp_parse_int(value, &entry->ambient_sound_distance);
        else if( strcmp(key, "soundretain") == 0 )
            ok = cp_parse_int(value, &entry->ambient_sound_retain);
        else if( strcmp(key, "soundmintick") == 0 )
            ok = cp_parse_int(value, &entry->ambient_sound_ticks_min);
        else if( strcmp(key, "soundmaxtick") == 0 )
            ok = cp_parse_int(value, &entry->ambient_sound_ticks_max);
        else if( strcmp(key, "randomanimstart") == 0 )
            ok = cp_parse_bool(value, &entry->seq_random_start);
        else if( strcmp(key, "sounddistancefade") == 0 )
            ok = cp_parse_int(value, &entry->sound_distance_fade_curve);
        else if( strcmp(key, "soundfadeincurve") == 0 )
            ok = cp_parse_int(value, &entry->sound_fade_in_curve);
        else if( strcmp(key, "soundfadein") == 0 )
            ok = cp_parse_int(value, &entry->sound_fade_in_duration);
        else if( strcmp(key, "soundfadeoutcurve") == 0 )
            ok = cp_parse_int(value, &entry->sound_fade_out_curve);
        else if( strcmp(key, "soundfadeout") == 0 )
            ok = cp_parse_int(value, &entry->sound_fade_out_duration);
        else if( strcmp(key, "opcode94") == 0 )
            ok = cp_parse_bool(value, &entry->unknown1);
        else if( strcmp(key, "soundvisibility") == 0 )
            ok = cp_parse_int(value, &entry->sound_visibility);
        else if( strcmp(key, "raise") == 0 )
            ok = cp_parse_int(value, &entry->raise);
        else if( strncmp(key, "recol", 5) == 0 || strncmp(key, "retex", 5) == 0 )
        {
            /* collected below */
        }
        else
            cp_warn(ctx, &ctx->warn_unknown_key, "loc [%s]: unknown key %s",
                    config->debugname, key);

        if( !ok )
        {
            fprintf(stderr, "cachepack: loc [%s]: bad value for %s\n", config->debugname, key);
            goto done;
        }
    }

    if( !cp_collect_pairs(config, "recol", &recol_s, &recol_d) ||
        !cp_collect_pairs(config, "retex", &retex_s, &retex_d) )
    {
        fprintf(stderr, "cachepack: loc [%s]: mismatched recolour pairs\n", config->debugname);
        goto done;
    }

    if( have_flat_models )
    {
        /* Opcode 5's shape: one model list, no shapes. `shapes == NULL` with a
         * count of 1 is what the decoder produces and what the encoder tests. */
        entry->shapes = NULL;
        entry->lengths = &flat_models.count;
        entry->models = &flat_models.items;
        entry->shapes_and_model_count = 1;
    }
    else
    {
        entry->shapes = shapes.shapes;
        entry->models = shapes.models;
        entry->lengths = shapes.lengths;
        entry->shapes_and_model_count = shapes.count;
    }
    entry->transforms = transforms.items;
    entry->transform_count = transforms.count;
    entry->ambient_sound_ids = sounds.items;
    entry->ambient_sound_id_count = sounds.count;
    entry->campaign_ids = campaigns.items;
    entry->campaign_id_count = campaigns.count;
    entry->random_seq_ids = random_seq.items;
    entry->random_seq_delays = random_delay.items;
    entry->random_seq_id_count = random_seq.count;
    entry->recolors_from = recol_s.items;
    entry->recolors_to = recol_d.items;
    entry->recolor_count = recol_s.count;
    entry->retextures_from = retex_s.items;
    entry->retextures_to = retex_d.items;
    entry->retexture_count = retex_s.count;

    written = RSCache_Dat2ConfigLocEncode(&ctx->profile, entry, out, out_capacity);

done:
    entry->shapes = NULL;
    entry->models = NULL;
    entry->lengths = NULL;
    entry->shapes_and_model_count = 0;
    entry->transforms = NULL;
    entry->ambient_sound_ids = NULL;
    entry->campaign_ids = NULL;
    entry->random_seq_ids = NULL;
    entry->random_seq_delays = NULL;
    entry->recolors_from = entry->recolors_to = NULL;
    entry->retextures_from = entry->retextures_to = NULL;
    RSCache_Dat2ConfigLocFree(entry);
    shapes_free(&shapes);
    cp_intlist_free(&flat_models);
    cp_intlist_free(&transforms);
    cp_intlist_free(&sounds);
    cp_intlist_free(&campaigns);
    cp_intlist_free(&random_seq);
    cp_intlist_free(&random_delay);
    cp_intlist_free(&recol_s);
    cp_intlist_free(&recol_d);
    cp_intlist_free(&retex_s);
    cp_intlist_free(&retex_d);
    return written;
}
