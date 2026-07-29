#include "cachepack.h"

#include "datatypes/dat2_config_enum.h"
#include "datatypes/dat2_config_idk.h"

#include <stdlib.h>
#include <string.h>

/* ---- identkit ----------------------------------------------------------- */

/**
 * Release an identkit's arrays without releasing the struct.
 *
 * `RSCache_Dat2ConfigIdkFree` is `free(idk)` and nothing else — it frees the
 * struct and leaks every array hanging off it — so it cannot be used on a stack
 * record, and using it anyway aborts on a free of a stack address. Every other
 * config type in the library has a matching `...FreeInplace`; this one does not,
 * so the in-place release lives here.
 */
static void
idk_free_inplace(struct RSCache_Dat2ConfigIdk* idk)
{
    free(idk->model_ids);
    free(idk->recolors_from);
    free(idk->recolors_to);
    free(idk->retextures_from);
    free(idk->retextures_to);
    memset(idk, 0, sizeof(*idk));
}

int
cp_unpack_idk(
    struct CP_Ctx* ctx,
    int id,
    const uint8_t* record,
    int record_size,
    struct CP_Lines* out)
{
    struct RSCache_Dat2ConfigIdk entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigIdkDecodeInplace(&entry, (char*)record, record_size);

    if( entry.body_part_id != -1 )
        cp_lines_addf(out, "bodypart=%d", entry.body_part_id);
    for( int i = 0; i < entry.model_ids_count; i++ )
        cp_lines_addf(out, "model%d=%d", i + 1, entry.model_ids[i]);
    /* The ten `if_model_ids` are the models the character-design interface draws
     * for this kit, indexed by wear slot; -1 is "this slot draws nothing". */
    for( int i = 0; i < 10; i++ )
    {
        if( entry.if_model_ids[i] != -1 )
            cp_lines_addf(out, "ifmodel%d=%d", i + 1, entry.if_model_ids[i]);
    }
    cp_emit_recols(out, entry.recolors_from, entry.recolors_to, entry.recolor_count, "recol");
    cp_emit_recols(out, entry.retextures_from, entry.retextures_to, entry.retexture_count,
                   "retex");
    if( entry.is_not_selectable )
        cp_lines_addf(out, "notselectable=yes");

    idk_free_inplace(&entry);
    return 1;
}

uint32_t
cp_pack_idk(
    struct CP_Ctx* ctx,
    int id,
    const struct CP_Config* config,
    uint8_t* out,
    uint32_t out_capacity)
{
    struct RSCache_Dat2ConfigIdk entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigIdkDecodeInplace(
        &entry, (char*)cp_empty_record, (int)sizeof(cp_empty_record));
    entry._id = id;

    struct CP_IntList models = { 0 };
    struct CP_IntList recol_s = { 0 }, recol_d = { 0 };
    struct CP_IntList retex_s = { 0 }, retex_d = { 0 };
    uint32_t written = 0;

    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;
        int ok = 1;
        int index;

        if( strcmp(key, "bodypart") == 0 )
            ok = cp_parse_int(value, &entry.body_part_id);
        else if( strcmp(key, "notselectable") == 0 )
            ok = cp_parse_bool(value, &entry.is_not_selectable);
        else if( (index = cp_indexed_key(key, "ifmodel")) >= 0 )
        {
            if( index >= 10 )
                ok = 0;
            else
                ok = cp_parse_int(value, &entry.if_model_ids[index]);
        }
        else if( (index = cp_indexed_key(key, "model")) >= 0 )
        {
            int model = 0;
            ok = cp_parse_int(value, &model);
            cp_intlist_set(&models, index, model);
        }
        else if( strncmp(key, "recol", 5) == 0 || strncmp(key, "retex", 5) == 0 )
        {
            /* handled in bulk below */
        }
        else
            cp_warn(ctx, &ctx->warn_unknown_key, "idk [%s]: unknown key %s",
                    config->debugname, key);

        if( !ok )
        {
            fprintf(stderr, "cachepack: idk [%s]: bad value for %s\n", config->debugname, key);
            goto done;
        }
    }

    if( !cp_collect_pairs(config, "recol", &recol_s, &recol_d) ||
        !cp_collect_pairs(config, "retex", &retex_s, &retex_d) )
    {
        fprintf(stderr, "cachepack: idk [%s]: mismatched recolour pairs\n", config->debugname);
        goto done;
    }

    entry.model_ids = models.items;
    entry.model_ids_count = models.count;
    entry.recolors_from = recol_s.items;
    entry.recolors_to = recol_d.items;
    entry.recolor_count = recol_s.count;
    entry.retextures_from = retex_s.items;
    entry.retextures_to = retex_d.items;
    entry.retexture_count = retex_s.count;

    written = RSCache_Dat2ConfigIdkEncode(&entry, out, out_capacity);

done:
    /* The lists own the arrays the struct points at; hand ownership back before
     * the free below, which would otherwise release them twice. */
    entry.model_ids = NULL;
    entry.recolors_from = entry.recolors_to = NULL;
    entry.retextures_from = entry.retextures_to = NULL;
    idk_free_inplace(&entry);
    cp_intlist_free(&models);
    cp_intlist_free(&recol_s);
    cp_intlist_free(&recol_d);
    cp_intlist_free(&retex_s);
    cp_intlist_free(&retex_d);
    return written;
}

/* ---- enum --------------------------------------------------------------- */

int
cp_unpack_enum(
    struct CP_Ctx* ctx,
    int id,
    const uint8_t* record,
    int record_size,
    struct CP_Lines* out)
{
    struct RSCache_Dat2ConfigEnum entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigEnumDecodeInplace(&entry, record, record_size);

    if( entry.output_is_string )
        cp_lines_addf(out, "outputstring=yes");
    if( entry.default_int )
        cp_lines_addf(out, "default=%d", entry.default_int);
    if( entry.default_long )
        cp_lines_addf(out, "defaultlong=%lld", (long long)entry.default_long);
    if( entry.default_string )
        cp_lines_add_str(out, "defaultstr", entry.default_string);

    /*
     * Three spellings, one per wire opcode (5 string, 6 int, 7 long). The value
     * type is not derivable from the text — "1" is a valid string value — so the
     * key carries it rather than a heuristic guessing it back.
     */
    for( int i = 0; i < entry.count; i++ )
    {
        char buf[8192];
        if( entry.string_values )
        {
            snprintf(buf, sizeof(buf), "%d,%s", entry.keys[i],
                     entry.string_values[i] ? entry.string_values[i] : "");
            cp_lines_add_str(out, "valstr", buf);
        }
        else if( entry.long_values )
        {
            cp_lines_addf(out, "vallong=%d,%lld", entry.keys[i],
                          (long long)entry.long_values[i]);
        }
        else if( entry.int_values )
        {
            cp_lines_addf(out, "val=%d,%d", entry.keys[i], entry.int_values[i]);
        }
    }

    RSCache_Dat2ConfigEnumFreeInplace(&entry);
    return 1;
}

uint32_t
cp_pack_enum(
    struct CP_Ctx* ctx,
    int id,
    const struct CP_Config* config,
    uint8_t* out,
    uint32_t out_capacity)
{
    struct RSCache_Dat2ConfigEnum entry;
    memset(&entry, 0, sizeof(entry));
    entry.id = id;

    int capacity = config->count > 0 ? config->count : 1;
    int* keys = calloc((size_t)capacity, sizeof(int));
    int* int_values = NULL;
    int64_t* long_values = NULL;
    char** string_values = NULL;
    int count = 0;
    uint32_t written = 0;
    if( !keys )
        return 0;

    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;
        int ok = 1;

        if( strcmp(key, "outputstring") == 0 )
        {
            bool flag = false;
            ok = cp_parse_bool(value, &flag);
            entry.output_is_string = flag ? 1 : 0;
        }
        else if( strcmp(key, "default") == 0 )
            ok = cp_parse_int(value, &entry.default_int);
        else if( strcmp(key, "defaultlong") == 0 )
            ok = cp_parse_i64(value, &entry.default_long);
        else if( strcmp(key, "defaultstr") == 0 )
        {
            char buf[8192];
            cp_unescape(value, buf, sizeof(buf));
            free(entry.default_string);
            entry.default_string = strdup(buf);
        }
        else if( strcmp(key, "val") == 0 || strcmp(key, "vallong") == 0 ||
                 strcmp(key, "valstr") == 0 )
        {
            char buf[8192];
            cp_unescape(value, buf, sizeof(buf));
            char* comma = strchr(buf, ',');
            if( !comma )
            {
                ok = 0;
            }
            else
            {
                *comma = '\0';
                int entry_key = 0;
                ok = cp_parse_int(buf, &entry_key);
                if( ok )
                {
                    keys[count] = entry_key;
                    if( strcmp(key, "valstr") == 0 )
                    {
                        if( !string_values )
                            string_values = calloc((size_t)capacity, sizeof(char*));
                        if( string_values )
                            string_values[count] = strdup(comma + 1);
                        else
                            ok = 0;
                    }
                    else if( strcmp(key, "vallong") == 0 )
                    {
                        if( !long_values )
                            long_values = calloc((size_t)capacity, sizeof(int64_t));
                        int64_t tmp = 0;
                        ok = long_values && cp_parse_i64(comma + 1, &tmp);
                        if( ok )
                            long_values[count] = tmp;
                    }
                    else
                    {
                        if( !int_values )
                            int_values = calloc((size_t)capacity, sizeof(int));
                        int tmp = 0;
                        ok = int_values && cp_parse_int(comma + 1, &tmp);
                        if( ok )
                            int_values[count] = tmp;
                    }
                    if( ok )
                        count++;
                }
            }
        }
        else
            cp_warn(ctx, &ctx->warn_unknown_key, "enum [%s]: unknown key %s",
                    config->debugname, key);

        if( !ok )
        {
            fprintf(stderr, "cachepack: enum [%s]: bad value for %s\n", config->debugname, key);
            goto done;
        }
    }

    entry.keys = keys;
    entry.int_values = int_values;
    entry.long_values = long_values;
    entry.string_values = string_values;
    entry.count = count;
    written = RSCache_Dat2ConfigEnumEncode(&entry, out, out_capacity);

done:
    entry.keys = keys;
    entry.int_values = int_values;
    entry.long_values = long_values;
    entry.string_values = string_values;
    entry.count = count;
    RSCache_Dat2ConfigEnumFreeInplace(&entry);
    return written;
}
