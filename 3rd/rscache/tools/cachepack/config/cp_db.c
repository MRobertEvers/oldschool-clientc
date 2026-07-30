#include "cachepack.h"

#include "datatypes/dat2_config_db.h"

#include <stdlib.h>
#include <string.h>

/*
 * The client database: dbtable (config group 39) declares columns and their types,
 * dbrow (group 38) carries one record's values. Nothing in rev 254 resembles them —
 * they are the backing store for the modern collection log, drop tables and the
 * like, read from CS2 through the DB_* opcodes.
 *
 * Both directions, and two things had to be settled before the pack half could
 * exist at all. Neither is visible from the struct — only from the bytes.
 *
 * **A string value can contain the separator.** Fields in a tuple are comma
 * separated, and 1,245 of this cache's 96,439 `values=` lines hold a comma inside
 * a string (`Graveyard of Heroes, Part 2`). Splitting on a bare comma turns one
 * field into two, silently, for exactly the records nobody checks. So a comma in a
 * value is written `\,` and a backslash `\\` — the same escape
 * `append_escaped_arg` uses for interface hook arguments, and for the same reason:
 * that bug corrupted 942 interfaces before it was found.
 *
 * **The column-array size is not derivable.** `decode_columns` reads an `alloc`
 * byte and indexes columns *by id* into an array that size; the text emits only
 * the columns that are present. Those are not the same number — 8,257 of 16,711
 * dbrows have `alloc` larger than their highest present column. So `columns=` is
 * written explicitly rather than inferred from the highest id seen.
 */

/**
 * Append `text` with `,` and `\` escaped. Returns the new write offset.
 *
 * A value is free text: it can hold the field separator, and 1,245 lines in this
 * cache do.
 */
static int
append_escaped(char* buf, size_t cap, int w, const char* text)
{
    for( const char* c = text; *c && w < (int)cap - 2; c++ )
    {
        if( *c == ',' || *c == '\\' )
            buf[w++] = '\\';
        buf[w++] = *c;
    }
    buf[w] = '\0';
    return w;
}

/** Inverse of `append_escaped`, in place. Returns the start of the next field, or
 *  NULL at the end of the line. */
static char*
split_escaped(char* cursor, char** out_field)
{
    char* write = cursor;

    *out_field = cursor;
    for( char* read = cursor;; read++ )
    {
        if( *read == '\\' && read[1] )
        {
            *write++ = *++read;
            continue;
        }
        if( *read == ',' )
        {
            *write = '\0';
            return read + 1;
        }
        if( !*read )
        {
            *write = '\0';
            return NULL;
        }
        *write++ = *read;
    }
}

static void
emit_column(
    struct CP_Lines* out,
    const char* prefix,
    int column_id,
    const struct RSCache_DbColumn* column)
{
    if( !column->present )
        return;

    char buf[8192];
    int w = snprintf(buf, sizeof(buf), "%d:", column_id);
    for( int i = 0; i < column->type_count; i++ )
        w += snprintf(buf + w, sizeof(buf) - (size_t)w, i ? ",%d" : "%d", column->types[i]);
    cp_lines_addf(out, "%stypes=%s", prefix, buf);

    for( int t = 0; t < column->tuple_count; t++ )
    {
        w = snprintf(buf, sizeof(buf), "%d:%d:", column_id, t);
        for( int f = 0; f < column->type_count; f++ )
        {
            const struct RSCache_DbValue* value = &column->values[t * column->type_count + f];
            if( w >= (int)sizeof(buf) - 32 )
                break;
            if( value->is_string )
            {
                if( f )
                    buf[w++] = ',';
                w = append_escaped(buf, sizeof(buf), w,
                                   value->string_value ? value->string_value : "");
            }
            else
                w += snprintf(buf + w, sizeof(buf) - (size_t)w, f ? ",%d" : "%d",
                              value->int_value);
        }
        cp_lines_add_str(out, prefix[0] ? "defaults" : "values", buf);
    }
}

int
cp_unpack_dbrow(
    struct CP_Ctx* ctx,
    int id,
    const uint8_t* record,
    int record_size,
    struct CP_Lines* out)
{
    struct RSCache_Dat2ConfigDbRow entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigDbRowDecodeInplace(&entry, record, record_size);

    cp_lines_addf(out, "columns=%d", entry.column_count);
    cp_emit_ref(ctx, out, "table", CP_TYPE_DBTABLE, entry.table_id, -1);
    for( int c = 0; c < entry.column_count; c++ )
        emit_column(out, "", c, &entry.columns[c]);

    RSCache_Dat2ConfigDbRowFreeInplace(&entry);
    return 1;
}

int
cp_unpack_dbtable(
    struct CP_Ctx* ctx,
    int id,
    const uint8_t* record,
    int record_size,
    struct CP_Lines* out)
{
    struct RSCache_Dat2ConfigDbTable entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigDbTableDecodeInplace(&entry, record, record_size);

    cp_lines_addf(out, "columns=%d", entry.column_count);
    for( int c = 0; c < entry.column_count; c++ )
        emit_column(out, "default", c, &entry.columns[c]);

    RSCache_Dat2ConfigDbTableFreeInplace(&entry);
    return 1;
}

/*
 * Both packers refuse rather than approximate.
 *
 * **The library now has encoders** — `RSCache_Dat2ConfigDbRowEncode` and
 * `…DbTableEncode`, held to byte-identity against all 16,711 dbrows and 246
 * dbtables in cache.osrs239 (`test/test_db_encode.c`). So the reason these refuse
 * is no longer "there is no encoder"; it is that **this text format cannot be read
 * back unambiguously**.
 *
 * `emit_column` comma-separates a tuple's fields, and 1,245 of the 96,439 `values=`
 * lines contain a comma *inside a string value* — `Graveyard of Heroes, Part 2`.
 * Splitting on commas turns one field into two, silently. Fixing it means escaping
 * the separator the way `append_escaped_arg` does for interface hooks, which is a
 * format change and a re-export, not a packer.
 *
 * Until then, refusing is right: a packer that reads this format back would corrupt
 * those records rather than fail.
 *
 * A dbrow's on-disk layout interleaves a column bitmap with per-column type lists
 * whose widths depend on the table's declaration, and the library has no encoder
 * for it. Writing one from the decoder's struct would be an unvalidated guess at a
 * format nothing in the repo can check — and a wrong dbrow does not fail loudly,
 * it feeds a CS2 script a plausible value from the wrong column.
 */
static uint32_t
refuse(
    struct CP_Ctx* ctx,
    const char* type,
    const struct CP_Config* config)
{
    fprintf(
        stderr,
        "cachepack: %s [%s]: rscache has no %s encoder — the source cache's records are "
        "kept unchanged\n",
        type,
        config->debugname,
        type);
    return 0;
}

/** Grow `cols` so column `c` exists. 0 on failure. */
static int
ensure_column(struct RSCache_DbColumn** cols, int* count, int c)
{
    struct RSCache_DbColumn* grown;

    if( c < 0 || c > 0xff )
        return 0;
    if( c < *count )
        return 1;
    grown = realloc(*cols, (size_t)(c + 1) * sizeof(*grown));
    if( !grown )
        return 0;
    memset(&grown[*count], 0, (size_t)(c + 1 - *count) * sizeof(*grown));
    *cols = grown;
    *count = c + 1;
    return 1;
}

/**
 * Read the `types=` / `values=` / `defaults=` lines of one block into columns.
 *
 * `types=` must precede its column's value lines, because a field's ScriptVarType
 * is what decides whether it parses as a string or a 4-byte int — which is also
 * the order the emitter writes them in.
 */
static int
parse_columns(
    const struct CP_Config* config,
    const char* types_key,
    const char* values_key,
    struct RSCache_DbColumn** out_cols,
    int* out_count)
{
    struct RSCache_DbColumn* cols = NULL;
    int count = 0;
    int declared = -1;

    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        char* value = config->lines[i].value;
        char scratch[8192];
        char* cursor;
        int col;

        if( strcmp(key, "columns") == 0 )
        {
            declared = atoi(value);
            continue;
        }
        if( strcmp(key, types_key) != 0 && strcmp(key, values_key) != 0 )
            continue;

        /*
         * Unescape *before* splitting, because there are two layers and they nest.
         *
         * `cp_lines_add_str` escapes `\`, newline and carriage return on the way
         * out, so this file's own `\,` arrives here as two characters plus a
         * comma. Splitting first sees the bare comma and cuts the field in half —
         * which is exactly what happened to all 1,256 rows holding a comma inside a
         * string. `cp_unescape` peels the outer layer, leaving `\,` for
         * `split_escaped` to read as a literal comma.
         */
        cp_unescape(value, scratch, (int)sizeof(scratch));
        cursor = scratch;
        col = (int)strtol(cursor, &cursor, 10);
        if( *cursor != ':' || !ensure_column(&cols, &count, col) )
            goto fail;
        cursor++;

        if( strcmp(key, types_key) == 0 )
        {
            int n = 0;
            int* types = NULL;

            for( char* field = cursor; field; )
            {
                char* one;
                int* grown;

                field = split_escaped(field, &one);
                grown = realloc(types, (size_t)(n + 1) * sizeof(int));
                if( !grown )
                {
                    free(types);
                    goto fail;
                }
                types = grown;
                types[n++] = (int)strtol(one, NULL, 10);
            }
            free(cols[col].types);
            cols[col].present = true;
            cols[col].types = types;
            cols[col].type_count = n;
        }
        else
        {
            struct RSCache_DbColumn* c = &cols[col];
            struct RSCache_DbValue* grown;
            int tuple;
            int f = 0;

            /* The tuple index is stated but not stored: tuples are dense and
             * emitted in order, so appending reproduces the layout. */
            tuple = (int)strtol(cursor, &cursor, 10);
            (void)tuple;
            if( *cursor != ':' || !c->present || c->type_count <= 0 )
                goto fail;
            cursor++;

            grown = realloc(c->values, (size_t)((c->tuple_count + 1) * c->type_count) *
                                           sizeof(*grown));
            if( !grown )
                goto fail;
            c->values = grown;
            memset(&c->values[c->tuple_count * c->type_count], 0,
                   (size_t)c->type_count * sizeof(*grown));

            for( char* field = cursor; field && f < c->type_count; f++ )
            {
                struct RSCache_DbValue* v =
                    &c->values[(c->tuple_count * c->type_count) + f];
                char* one;

                field = split_escaped(field, &one);
                if( RSCache_DbTypeIsString(c->types[f]) )
                {
                    v->is_string = true;
                    v->string_value = strdup(one);
                }
                else
                {
                    v->is_string = false;
                    v->int_value = (int)strtol(one, NULL, 10);
                }
            }
            c->tuple_count++;
        }
    }

    /* `columns=` is the decoder's `alloc` byte, and it is *not* the highest
     * present id plus one — 8,257 dbrows have slack. Honour it. */
    if( declared > count && !ensure_column(&cols, &count, declared - 1) )
        goto fail;
    *out_cols = cols;
    *out_count = declared >= 0 ? declared : count;
    return 1;

fail:
    for( int c = 0; c < count; c++ )
    {
        free(cols[c].types);
        for( int v = 0; v < cols[c].tuple_count * cols[c].type_count; v++ )
            free(cols[c].values[v].string_value);
        free(cols[c].values);
    }
    free(cols);
    return 0;
}

uint32_t
cp_pack_dbrow(
    struct CP_Ctx* ctx,
    int id,
    const struct CP_Config* config,
    uint8_t* out,
    uint32_t out_capacity)
{
    struct RSCache_Dat2ConfigDbRow entry;
    uint32_t written;

    memset(&entry, 0, sizeof(entry));
    entry.id = id;
    entry.table_id = -1;
    for( int i = 0; i < config->count; i++ )
    {
        if( strcmp(config->lines[i].key, "table") == 0 )
            entry.table_id = cp_name_find(ctx, CP_TYPE_DBTABLE, config->lines[i].value);
    }
    if( !parse_columns(config, "types", "values", &entry.columns, &entry.column_count) )
        return refuse(ctx, "dbrow", config);

    written = RSCache_Dat2ConfigDbRowEncode(&entry, out, out_capacity);
    RSCache_Dat2ConfigDbRowFreeInplace(&entry);
    return written;
}

uint32_t
cp_pack_dbtable(
    struct CP_Ctx* ctx,
    int id,
    const struct CP_Config* config,
    uint8_t* out,
    uint32_t out_capacity)
{
    struct RSCache_Dat2ConfigDbTable entry;
    uint32_t written;

    memset(&entry, 0, sizeof(entry));
    entry.id = id;
    /* A dbtable's lines are prefixed: the emitter writes `defaulttypes=` and
     * `defaults=`, because a table declares defaults where a row carries values. */
    if( !parse_columns(config, "defaulttypes", "defaults", &entry.columns,
                       &entry.column_count) )
        return refuse(ctx, "dbtable", config);

    written = RSCache_Dat2ConfigDbTableEncode(&entry, out, out_capacity);
    RSCache_Dat2ConfigDbTableFreeInplace(&entry);
    return written;
}
