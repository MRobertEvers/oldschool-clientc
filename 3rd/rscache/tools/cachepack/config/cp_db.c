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

/* ---- the tuple-type alphabet -------------------------------------------- */

/*
 * A column's tuple positions carry ScriptVarType *base codes*, and they used to be
 * emitted as bare numbers: `defaulttypes=13:22` said nothing about being a coord.
 *
 * These are NOT the character codes `cp_param_type_char` uses — a param's coord is
 * `'c'` (99) and a dbtable's is 22 — so this is a second alphabet over the same
 * concepts, and it is derived rather than remembered. Twenty-five distinct codes
 * occur across cache.osrs239's 246 dbtables and 16,711 dbrows. Each name below
 * needed two signals to agree before it was written down:
 *
 *   - **the column names carrying it**, from gameval archive 10 (see
 *     `keyed_gameval_name`) — a column literally called `npc` or `mapelement` or
 *     `stat` is the cache stating the type itself;
 *   - **the value range**, from every int in `all.dbrow` declared at that
 *     position, against each config namespace's highest id — which *excludes*
 *     candidates rather than choosing between them.
 *
 * Worked cases, because the interesting ones are where the two disagree with a
 * first guess:
 *
 *   74   `dbrow`. Carried by `parent_quest`, `requirement_quests`, `task` and by
 *        `sailing_charting_core`, which is itself a table name. Its highest value
 *        is 16939 — exactly this cache's highest dbrow id, which no other
 *        namespace reaches.
 *   13   `namedobj`, and 33 `obj`. Both sit in the obj id range, so the range
 *        cannot separate them; the column *named* `namedobj` carries 13, while 33
 *        is carried by `item` and the eleven `wearpos_*` columns.
 *   9    `component`. Values run 983112..57606177, which fits no flat namespace at
 *        all — they are `(interface << 16) | child`, and the columns are `*_com`
 *        and `teleport_if_layer`.
 *   22   `coord`. Also fits nothing flat: values reach 855167577, a packed coord.
 *   6    `seq`. One column carries it, `customisation_loc_anim`, and its 21 values
 *        land in 13174..13586 where only the seq group reaches.
 *
 * Three codes are deliberately left as numbers, because one signal is not two:
 *
 *   8    every one of its 140 values is literally `10`. A constant tells you
 *        nothing about a type, and its two column names (`loc`, `static_facility`)
 *        point at a type 30 already covers.
 *   26   `enum` and `struct` both fit its range, and its three column names
 *        (`omnishop_shop_filter_titles`, `map_slideshow`, `reward_league_relics`)
 *        do not choose between them.
 *   118  one column, `charting_type`, six distinct values in 155..160. Nothing
 *        discriminates.
 *
 * An unknown code round-trips as its own number, which is what keeps a newer cache
 * from losing a type this table has never seen.
 */
static const struct
{
    int code;
    const char* name;
} k_db_types[] = {
    { 0, "int" },       { 1, "boolean" },  { 6, "seq" },     { 9, "component" },
    { 10, "idkit" },    { 11, "track" },   { 13, "namedobj" }, { 14, "synth" },
    { 17, "stat" },     { 22, "coord" },   { 23, "graphic" }, { 30, "loc" },
    { 31, "model" },    { 32, "npc" },     { 33, "obj" },     { 36, "string" },
    { 39, "inv" },      { 41, "category" }, { 59, "mapelement" }, { 73, "struct" },
    { 74, "dbrow" },    { 209, "varp" },
};

#define DB_TYPE_COUNT ((int)(sizeof(k_db_types) / sizeof(k_db_types[0])))

/** The spelling for a tuple-type code: a name, or the number itself. */
static void
db_type_name(
    int code,
    char* out,
    size_t out_size)
{
    for( int i = 0; i < DB_TYPE_COUNT; i++ )
    {
        if( k_db_types[i].code == code )
        {
            snprintf(out, out_size, "%s", k_db_types[i].name);
            return;
        }
    }
    snprintf(out, out_size, "%d", code);
}

/**
 * Inverse of `db_type_name`. Returns the code, or -1 for a token that is neither.
 *
 * A bare number is accepted so that a code this alphabet has never named still
 * reads back, which is the half that makes the round trip total.
 */
static int
db_type_code(const char* text)
{
    char* end;
    long value;

    if( !text || !text[0] )
        return -1;
    for( int i = 0; i < DB_TYPE_COUNT; i++ )
    {
        if( strcmp(k_db_types[i].name, text) == 0 )
            return k_db_types[i].code;
    }
    value = strtol(text, &end, 10);
    if( end == text || *end )
        return -1;
    return (int)value;
}

/** The config namespace named by one DB tuple type, or -1 for scalar/asset
 *  types cachepack cannot resolve through a config pack. */
static int
db_type_ref_type(int code)
{
    switch( code )
    {
    case 6: return CP_TYPE_SEQ;
    case 13: return CP_TYPE_OBJ; /* namedobj */
    case 23: return CP_TYPE_SPOTANIM;
    case 30: return CP_TYPE_LOC;
    case 32: return CP_TYPE_NPC;
    case 33: return CP_TYPE_OBJ;
    case 39: return CP_TYPE_INV;
    case 59: return CP_TYPE_MAPELEMENT;
    case 73: return CP_TYPE_STRUCT;
    case 74: return CP_TYPE_DBROW;
    case 209: return CP_TYPE_VARP;
    default: return -1;
    }
}

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

/**
 * One column: its id, the cache's name for it, and its tuple types.
 *
 *     columndef=13:startcoord,coord
 *     columndef=23:requirement_stats,stat,int
 *
 * `name` is the cache's own, from gameval archive 10, and is documentation — the
 * packer reads past it. It is emitted **empty rather than omitted** when the cache
 * does not name the column (`columndef=7:,int`), because the field's *position* is
 * what makes the line parseable; a name that sometimes is not there would make the
 * first field ambiguous with a type.
 *
 * This replaces `types=`/`defaulttypes=`, which wrote bare numbers and no name at
 * all — `defaulttypes=13:22` for what is now `columndef=13:startcoord,coord`. Both
 * old spellings are still read; see `parse_columns`.
 *
 * **`columndef` and not `column`, which is what it was first called.** The server's
 * own `.dbtable` grammar uses `column=<name>,<type>...`, and `ToriRSServer_DbLoad`
 * walks the *whole* content tree for `*.dbtable` — which matches
 * `configs/all.dbtable`. So `column=13:startcoord,coord` was read by the server's
 * parser as a column named `13:startcoord` with one unrecognised type, and the
 * mock refused to boot. Two grammars cannot share a key in one file namespace, and
 * the cache's is the one that has to move: the server's is LostCity's.
 *
 * That collision also exposed what the server has been doing with these files all
 * along — see docs/DBTABLES.md §8.
 */
static void
emit_column(
    struct CP_Ctx* ctx,
    struct CP_Lines* out,
    const char* prefix,
    int table_id,
    int column_id,
    const struct RSCache_DbColumn* column)
{
    if( !column->present )
        return;

    char buf[8192];
    const char* column_name = cp_db_column_name(&ctx->names, table_id, column_id);
    int w = snprintf(buf, sizeof(buf), "%d:%s", column_id, column_name ? column_name : "");
    for( int i = 0; i < column->type_count; i++ )
    {
        char type_name[32];

        db_type_name(column->types[i], type_name, sizeof(type_name));
        w += snprintf(buf + w, sizeof(buf) - (size_t)w, ",%s", type_name);
    }
    cp_lines_addf(out, "columndef=%s", buf);

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
        emit_column(ctx, out, "", entry.table_id, c, &entry.columns[c]);

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
        emit_column(ctx, out, "default", id, c, &entry.columns[c]);

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
 * Read the `column=` / `values=` / `defaults=` lines of one block into columns.
 *
 * A column's declaration must precede its value lines, because a field's
 * ScriptVarType is what decides whether it parses as a string or a 4-byte int —
 * which is also the order the emitter writes them in.
 *
 * **Two spellings of the declaration are accepted**, and the difference is one
 * leading field:
 *
 *     column=13:startcoord,coord      current  — a name, then types by name
 *     defaulttypes=13:22              legacy   — no name, types as bare numbers
 *     types=13:22                     legacy, on a dbrow
 *
 * The legacy keys are still read because dropping them would turn an
 * un-regenerated tree into a *silent* corruption rather than an error: nothing in
 * this parser requires a column line to exist, so a block whose declarations were
 * all unrecognised would pack as an empty record and report success.
 */
static int
parse_columns(
    struct CP_Ctx* ctx,
    const struct CP_Config* config,
    const char* legacy_types_key,
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
        int is_named = strcmp(key, "columndef") == 0;
        int is_types = is_named || strcmp(key, legacy_types_key) == 0;

        if( strcmp(key, "columns") == 0 )
        {
            declared = atoi(value);
            continue;
        }
        if( !is_types && strcmp(key, values_key) != 0 )
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

        if( is_types )
        {
            int n = 0;
            int* types = NULL;
            char* field = cursor;

            /* The current spelling leads with the column's name, which is
             * documentation: skipped here, not validated against the cache. A tree
             * whose gameval names have moved on is still a correct tree. */
            if( is_named && field )
            {
                char* discard;
                field = split_escaped(field, &discard);
            }

            while( field )
            {
                char* one;
                int* grown;
                int code;

                field = split_escaped(field, &one);
                code = db_type_code(one);
                if( code < 0 )
                {
                    free(types);
                    goto fail;
                }
                grown = realloc(types, (size_t)(n + 1) * sizeof(int));
                if( !grown )
                {
                    free(types);
                    goto fail;
                }
                types = grown;
                types[n++] = code;
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
                    int ref_type = db_type_ref_type(c->types[f]);

                    v->is_string = false;
                    if( ref_type >= 0 )
                    {
                        if( !cp_resolve_ref(ctx, (enum CP_TypeId)ref_type, one,
                                            &v->int_value) )
                            goto fail;
                    }
                    else
                    {
                        char* end;
                        long parsed = strtol(one, &end, 10);

                        if( end == one || *end )
                        {
                            cp_warn(ctx, &ctx->warn_unresolved_name,
                                    "dbrow [%s] type %d requires an integer, got '%s'",
                                    config->debugname, c->types[f], one);
                            goto fail;
                        }
                        v->int_value = (int)parsed;
                    }
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
        /*
         * `cp_resolve_ref`, not a bare `cp_name_find`, and this was the one
         * reference in the tool that did not go through it.
         *
         * `table=` is keyed by *name*, so renaming a table in
         * `configs/all.dbtable.compack` without re-keying `all.dbrow` leaves this
         * lookup returning -1 — and -1 is exactly how the decoder spells "opcode 4
         * never appeared", so the encoder simply omits the opcode. The row packs
         * two bytes shorter with no table binding at all and `DB_GETROWTABLE`
         * answers -1, while the run reports `0 failed, 0 unresolved names`.
         *
         * Measured: staling a single `table=` line took the dbrow group from
         * 1,774,143 to 1,774,141 bytes in silence. `cp_resolve_ref` warns on a name
         * the pack does not know and accepts a bare number, which is the same
         * contract every other reference in this tool already has.
         */
        if( strcmp(config->lines[i].key, "table") == 0 )
            cp_resolve_ref(ctx, CP_TYPE_DBTABLE, config->lines[i].value, &entry.table_id);
    }
    if( !parse_columns(ctx, config, "types", "values", &entry.columns, &entry.column_count) )
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
    if( !parse_columns(ctx, config, "defaulttypes", "defaults", &entry.columns,
                       &entry.column_count) )
        return refuse(ctx, "dbtable", config);

    written = RSCache_Dat2ConfigDbTableEncode(&entry, out, out_capacity);
    RSCache_Dat2ConfigDbTableFreeInplace(&entry);
    return written;
}
