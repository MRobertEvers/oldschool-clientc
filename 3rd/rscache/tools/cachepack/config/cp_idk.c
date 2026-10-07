#include "cachepack.h"

#include "datatypes/dat2_config_enum.h"
#include "datatypes/dat2_config_idk.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/*
 * Every key is written for every record, `key=default` when the stream does not
 * state it, straight from the decoder's presence bits; a stated list with no
 * entries is `key=empty`.
 */

/* A `yes` flag key: the opcode carries no payload, so its presence is the value. */
static int
parse_flag(const char* value)
{
    return strcmp(value, "yes") == 0;
}

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

/* In the order they are written. `model`, `recol` and `retex` are one opcode with
 * a count each (INDEXED); the ten if-model slots are ten opcodes, so ten keys. */
const struct CP_KeySpec cp_idk_keys[] = {
    { "bodypart", 0, NULL, NULL },
    { "model", CP_KEY_INDEXED, NULL, NULL },
    { "ifmodel1", 0, NULL, NULL },
    { "ifmodel2", 0, NULL, NULL },
    { "ifmodel3", 0, NULL, NULL },
    { "ifmodel4", 0, NULL, NULL },
    { "ifmodel5", 0, NULL, NULL },
    { "ifmodel6", 0, NULL, NULL },
    { "ifmodel7", 0, NULL, NULL },
    { "ifmodel8", 0, NULL, NULL },
    { "ifmodel9", 0, NULL, NULL },
    { "ifmodel10", 0, NULL, NULL },
    { "recol", CP_KEY_INDEXED, NULL, NULL },
    { "retex", CP_KEY_INDEXED, NULL, NULL },
    { "notselectable", 0, NULL, NULL },
    { NULL, 0, NULL, NULL },
};

/** A stated recolour/retexture list: its pairs, or `stem=empty`. */
static void
emit_pairs(
    struct CP_Lines* out,
    const struct RSCache_Presence* has,
    int field,
    const int* from,
    const int* to,
    int count,
    const char* stem)
{
    if( !RSCache_PresenceHas(has, field) )
        cp_lines_add_default(out, stem);
    else if( count == 0 )
        cp_lines_add_empty(out, stem);
    else
        cp_emit_recols(out, from, to, count, stem);
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
    const struct RSCache_Presence* has = &entry.present;

    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigIdkDecodeInplace(&entry, (char*)record, record_size);
    if( entry._consumed != record_size )
        cp_warn(ctx, &ctx->warn_short_decode, "idk %d: consumed %d of %d bytes", id,
                entry._consumed, record_size);

    if( RSCache_PresenceHas(has, RSCACHE_IDK_FIELD_BODY_PART) )
        cp_lines_addf(out, "bodypart=%d", entry.body_part_id);
    else
        cp_lines_add_default(out, "bodypart");

    if( !RSCache_PresenceHas(has, RSCACHE_IDK_FIELD_MODELS) )
        cp_lines_add_default(out, "model");
    else if( entry.model_ids_count == 0 )
        cp_lines_add_empty(out, "model");
    else
    {
        for( int i = 0; i < entry.model_ids_count; i++ )
            cp_lines_addf(out, "model%d=%d", i + 1, entry.model_ids[i]);
    }

    /* The ten `if_model_ids` are the models the character-design interface draws
     * for this kit, indexed by wear slot. */
    for( int i = 0; i < 10; i++ )
    {
        char key[16];

        snprintf(key, sizeof(key), "ifmodel%d", i + 1);
        if( RSCache_PresenceHas(has, RSCACHE_IDK_FIELD_IF_MODEL_FIRST + i) )
            cp_lines_addf(out, "%s=%d", key, entry.if_model_ids[i]);
        else
            cp_lines_add_default(out, key);
    }

    emit_pairs(out, has, RSCACHE_IDK_FIELD_RECOLOURS, entry.recolors_from, entry.recolors_to,
               entry.recolor_count, "recol");
    emit_pairs(out, has, RSCACHE_IDK_FIELD_RETEXTURES, entry.retextures_from,
               entry.retextures_to, entry.retexture_count, "retex");

    if( RSCache_PresenceHas(has, RSCACHE_IDK_FIELD_NOT_SELECTABLE) )
        cp_lines_addf(out, "notselectable=yes");
    else
        cp_lines_add_default(out, "notselectable");

    idk_free_inplace(&entry);
    return 1;
}

/** Is `key` the stem itself or `stem<digit>...`? */
static int
key_in_family(
    const char* key,
    const char* stem)
{
    size_t n = strlen(stem);

    if( strncmp(key, stem, n) != 0 )
        return 0;
    return key[n] == '\0' || (key[n] >= '0' && key[n] <= '9');
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
    /* The empty-record decode gives the client defaults and no presence: a field
     * is written only if a line below states it. */
    RSCache_Dat2ConfigIdkDecodeInplace(
        &entry, (char*)cp_empty_record, (int)sizeof(cp_empty_record));
    entry._id = id;

    struct CP_IntList models = { 0 };
    struct CP_IntList recol_s = { 0 }, recol_d = { 0 };
    struct CP_IntList retex_s = { 0 }, retex_d = { 0 };
    uint32_t written = 0;

#define IDK_SET(field) RSCache_PresenceSet(&entry.present, RSCACHE_IDK_FIELD_##field)

    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;
        int ok = 1;
        int index;

        if( cp_value_is_default(value) )
            continue;

        if( strcmp(key, "bodypart") == 0 )
        {
            IDK_SET(BODY_PART);
            ok = cp_parse_int(value, &entry.body_part_id) && entry.body_part_id >= 0 &&
                 entry.body_part_id <= 255;
        }
        else if( strcmp(key, "notselectable") == 0 )
        {
            IDK_SET(NOT_SELECTABLE);
            ok = parse_flag(value);
            entry.is_not_selectable = true;
        }
        else if( (index = cp_indexed_key(key, "ifmodel")) >= 0 )
        {
            if( index >= 10 )
                ok = 0;
            else
            {
                RSCache_PresenceSet(&entry.present, RSCACHE_IDK_FIELD_IF_MODEL_FIRST + index);
                ok = cp_parse_int(value, &entry.if_model_ids[index]);
            }
        }
        else if( strcmp(key, "model") == 0 )
        {
            IDK_SET(MODELS);
            ok = cp_value_is_empty(value);
        }
        else if( (index = cp_indexed_key(key, "model")) >= 0 )
        {
            int model = 0;
            IDK_SET(MODELS);
            ok = cp_parse_int(value, &model);
            cp_intlist_set(&models, index, model);
        }
        else if( key_in_family(key, "recol") )
        {
            /* The pairs are collected in bulk below; `recol=empty` states the list. */
            IDK_SET(RECOLOURS);
            if( strcmp(key, "recol") == 0 )
                ok = cp_value_is_empty(value);
        }
        else if( key_in_family(key, "retex") )
        {
            IDK_SET(RETEXTURES);
            if( strcmp(key, "retex") == 0 )
                ok = cp_value_is_empty(value);
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

#undef IDK_SET

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

    written = RSCache_Dat2ConfigIdkEncodeProfile(&ctx->profile, &entry, out, out_capacity);

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

/*
 * LostCity's grammar, both ways, the one every authored `.enum` uses:
 *
 *     [magic_carpet_path]
 *     inputtype=int
 *     outputtype=coord
 *     val=0,0_51_48_41_35
 *     default=null
 *
 * `inputtype`/`outputtype` are ScriptVarType words (cp_value.c); a `val=` line is
 * `<key>,<value>`, the key spelled by the input type and the value -- everything
 * after the first comma, so a string may hold commas -- by the output type;
 * `default=` is spelled by the output type. All through the one table: an obj is
 * its name, a coord `level_mx_mz_lx_lz`, a boolean `yes`/`no`, -1 `null`.
 *
 * The value map is one opcode with a count, and WHICH opcode (5 string, 6 int, 7
 * long) the output type says -- as does the default's (3 string, 4 int, 8
 * long). There used to be a key per opcode (`valstr=`, `vallong=`,
 * `defaultstr=`, `defaultlong=`): one meaning, three spellings. A record whose
 * map or default opcode disagrees with its output type has no text in this
 * grammar; no cache here holds one (every string enum in cache.osrs239 states
 * opcodes 5 and 3, every other 6 and 4, and none states 7 or 8 -- no type this
 * table lists is long-valued), and one is refused loudly rather than written in
 * a second spelling.
 */
const struct CP_KeySpec cp_enum_keys[] = {
    { "inputtype", 0, NULL, NULL },
    { "outputtype", 0, NULL, NULL },
    { "val", CP_KEY_LIST, NULL, NULL },
    { "default", 0, NULL, NULL },
    { NULL, 0, NULL, NULL },
};

/* A key's type: the input type, or a plain int when it is none (absent, or a
 * string type -- a key is always an int on the wire). */
static const struct CP_ValueType*
enum_key_type(const struct CP_ValueType* input)
{
    if( input && input->spell == CP_VALUE_STRING )
        return NULL;
    return input;
}

int
cp_unpack_enum(
    struct CP_Ctx* ctx,
    int id,
    const uint8_t* record,
    int record_size,
    struct CP_Lines* out)
{
    struct RSCache_Dat2ConfigEnum entry;
    const struct RSCache_Presence* has = &entry.present;
    const struct CP_ValueType* input = NULL;
    const struct CP_ValueType* output = NULL;
    int is_string;
    char word[16];

    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigEnumDecodeInplace(&entry, record, record_size);
    if( entry._consumed != record_size )
        cp_warn(ctx, &ctx->warn_short_decode, "enum %d: consumed %d of %d bytes", id,
                entry._consumed, record_size);

    if( RSCache_PresenceHas(has, RSCACHE_ENUM_FIELD_INPUT_TYPE) )
    {
        input = cp_value_type_of_char((unsigned char)entry.input_type);
        cp_lines_addf(out, "inputtype=%s",
                      cp_value_char_text((unsigned char)entry.input_type, word, sizeof(word)));
    }
    else
        cp_lines_add_default(out, "inputtype");
    if( RSCache_PresenceHas(has, RSCACHE_ENUM_FIELD_OUTPUT_TYPE) )
    {
        output = cp_value_type_of_char((unsigned char)entry.output_type);
        cp_lines_addf(out, "outputtype=%s",
                      cp_value_char_text((unsigned char)entry.output_type, word, sizeof(word)));
    }
    else
        cp_lines_add_default(out, "outputtype");
    is_string = output && output->spell == CP_VALUE_STRING;

    {
        int strings = RSCache_PresenceHas(has, RSCACHE_ENUM_FIELD_STRING_VALUES);
        int ints = RSCache_PresenceHas(has, RSCACHE_ENUM_FIELD_INT_VALUES);
        int def_string = RSCache_PresenceHas(has, RSCACHE_ENUM_FIELD_DEFAULT_STRING);
        int def_int = RSCache_PresenceHas(has, RSCACHE_ENUM_FIELD_DEFAULT_INT);

        if( RSCache_PresenceHas(has, RSCACHE_ENUM_FIELD_LONG_VALUES) ||
            RSCache_PresenceHas(has, RSCACHE_ENUM_FIELD_DEFAULT_LONG) ||
            (strings && !is_string) || (ints && is_string) || (def_string && !is_string) ||
            (def_int && is_string) )
        {
            fprintf(stderr,
                    "cachepack: enum %d: its value map or default is not the kind its output "
                    "type `%s` says (string %d/%d, int %d/%d, long %d/%d) -- no text states "
                    "that\n",
                    id,
                    RSCache_PresenceHas(has, RSCACHE_ENUM_FIELD_OUTPUT_TYPE)
                        ? cp_value_char_text((unsigned char)entry.output_type, word,
                                             sizeof(word))
                        : "default",
                    strings, def_string, ints, def_int,
                    RSCache_PresenceHas(has, RSCACHE_ENUM_FIELD_LONG_VALUES),
                    RSCache_PresenceHas(has, RSCACHE_ENUM_FIELD_DEFAULT_LONG));
            RSCache_Dat2ConfigEnumFreeInplace(&entry);
            return 0;
        }

        if( !strings && !ints )
            cp_lines_add_default(out, "val");
        else if( entry.count == 0 )
            cp_lines_add_empty(out, "val");
        else
        {
            for( int i = 0; i < entry.count; i++ )
            {
                char key_buf[64];
                char value_buf[64];
                const char* key = cp_value_int_text(ctx, enum_key_type(input), entry.keys[i],
                                                    CP_BOOL_YES_NO, key_buf, sizeof(key_buf));

                if( is_string )
                {
                    char escaped[8192];

                    assert(entry.string_values[i]);
                    cp_value_string_escape(entry.string_values[i], 0, escaped, sizeof(escaped));
                    cp_lines_addf(out, "val=%s,%s", key, escaped);
                }
                else
                    cp_lines_addf(out, "val=%s,%s", key,
                                  cp_value_int_text(ctx, output, entry.int_values[i],
                                                    CP_BOOL_YES_NO, value_buf,
                                                    sizeof(value_buf)));
            }
        }

        if( def_string )
        {
            char escaped[8192];

            cp_value_string_escape(entry.default_string, 1, escaped, sizeof(escaped));
            cp_lines_addf(out, "default=%s", escaped);
        }
        else if( def_int )
        {
            char number[64];

            cp_lines_addf(out, "default=%s",
                          cp_value_int_text(ctx, output, entry.default_int, CP_BOOL_YES_NO,
                                            number, sizeof(number)));
        }
        else
            cp_lines_add_default(out, "default");
    }

    RSCache_Dat2ConfigEnumFreeInplace(&entry);
    return 1;
}

/* The first comma not escaped by a backslash, or NULL. */
static char*
enum_comma(char* raw)
{
    for( char* p = raw; *p; p++ )
    {
        if( *p == '\\' && p[1] )
        {
            p++;
            continue;
        }
        if( *p == ',' )
            return p;
    }
    return NULL;
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
    /* The empty-record decode gives the client defaults and no presence: a field
     * is written only if a line below states it. */
    RSCache_Dat2ConfigEnumDecodeInplace(&entry, cp_empty_record, (int)sizeof(cp_empty_record));
    entry.id = id;

    int capacity = config->count > 0 ? config->count : 1;
    int* keys = calloc((size_t)capacity, sizeof(int));
    int* int_values = NULL;
    char** string_values = NULL;
    int count = 0;
    uint32_t written = 0;
    const struct CP_ValueType* input = NULL;
    const struct CP_ValueType* output = NULL;
    int is_string;
    assert(keys);

#define ENUM_SET(field) RSCache_PresenceSet(&entry.present, RSCACHE_ENUM_FIELD_##field)

    /*
     * The types first, in their own pass: every `val=` and the `default=` are read
     * THROUGH them, and nothing says a person must write them above the rows.
     */
    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;
        int is_input = strcmp(key, "inputtype") == 0;
        char word[64];
        int ch;

        if( (!is_input && strcmp(key, "outputtype") != 0) || cp_value_is_default(value) )
            continue;
        cp_unescape(value, word, sizeof(word));
        ch = cp_value_char_read(word);
        if( !ch )
        {
            fprintf(stderr, "cachepack: enum [%s]: bad value for %s (`%s` is no type)\n",
                    config->debugname, key, word);
            goto done;
        }
        if( is_input )
        {
            ENUM_SET(INPUT_TYPE);
            entry.input_type = (char)ch;
            input = cp_value_type_of_char(ch);
        }
        else
        {
            ENUM_SET(OUTPUT_TYPE);
            entry.output_type = (char)ch;
            output = cp_value_type_of_char(ch);
        }
    }
    is_string = output && output->spell == CP_VALUE_STRING;
    entry.output_is_string = is_string;

    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;
        int ok = 1;

        if( cp_value_is_default(value) )
            continue;

        if( strcmp(key, "inputtype") == 0 || strcmp(key, "outputtype") == 0 )
            continue; /* the pass above */
        else if( strcmp(key, "default") == 0 )
        {
            char buf[8192] = { 0 };

            if( is_string )
            {
                ENUM_SET(DEFAULT_STRING);
                ok = cp_value_string_read(ctx, value, buf, sizeof(buf));
                free(entry.default_string);
                entry.default_string = strdup(buf);
                assert(entry.default_string);
            }
            else
            {
                ENUM_SET(DEFAULT_INT);
                cp_unescape(value, buf, sizeof(buf));
                ok = cp_value_int_read(ctx, output, buf, &entry.default_int);
            }
        }
        else if( strcmp(key, "val") == 0 )
        {
            if( is_string )
                ENUM_SET(STRING_VALUES);
            else
                ENUM_SET(INT_VALUES);

            if( !cp_value_is_empty(value) )
            {
                char raw[8192];
                char key_text[1024];
                char* comma;

                snprintf(raw, sizeof(raw), "%s", value);
                comma = enum_comma(raw);
                ok = comma != NULL;
                if( ok )
                {
                    *comma = '\0';
                    cp_unescape(raw, key_text, sizeof(key_text));
                    ok = cp_value_int_read(ctx, enum_key_type(input), key_text, &keys[count]);
                }
                if( ok && is_string )
                {
                    char text[8192] = { 0 };

                    if( !string_values )
                    {
                        string_values = calloc((size_t)capacity, sizeof(char*));
                        assert(string_values);
                    }
                    ok = cp_value_string_read(ctx, comma + 1, text, sizeof(text));
                    string_values[count] = strdup(text);
                    assert(string_values[count]);
                }
                else if( ok )
                {
                    char text[8192];

                    if( !int_values )
                    {
                        int_values = calloc((size_t)capacity, sizeof(int));
                        assert(int_values);
                    }
                    cp_unescape(comma + 1, text, sizeof(text));
                    ok = cp_value_int_read(ctx, output, text, &int_values[count]);
                }
                if( ok )
                    count++;
                else if( string_values )
                {
                    free(string_values[count]);
                    string_values[count] = NULL;
                }
            }
        }
        else
            cp_warn(ctx, &ctx->warn_unknown_key, "enum [%s]: unknown key %s",
                    config->debugname, key);

        if( !ok )
        {
            fprintf(stderr, "cachepack: enum [%s]: bad value for %s (`%s`)\n", config->debugname,
                    key, value);
            goto done;
        }
    }

#undef ENUM_SET

    entry.keys = keys;
    entry.int_values = int_values;
    entry.string_values = string_values;
    entry.count = count;
    written = RSCache_Dat2ConfigEnumEncode(&entry, out, out_capacity);

done:
    entry.keys = keys;
    entry.int_values = int_values;
    entry.long_values = NULL;
    entry.string_values = string_values;
    entry.count = count;
    RSCache_Dat2ConfigEnumFreeInplace(&entry);
    return written;
}
