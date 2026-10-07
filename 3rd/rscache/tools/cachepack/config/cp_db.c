#include "cachepack.h"

#include "cp_merge.h"
#include "datatypes/dat2_config_db.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/*
 * The client database: dbtable (config group 39) declares columns and their types,
 * dbrow (group 38) carries one record's values. They are the backing store for the
 * quest list, the collection log, drop tables and the like, read from CS2 through
 * the DB_* opcodes.
 *
 * ## One grammar: the authored one
 *
 * Both types are written and read in LostCity's grammar, the one every authored
 * `.dbtable` / `.dbrow` under `server/scripts` already uses and the game server's own
 * reader (`load_dbtable_file` / `load_dbrow_file` in torirs_server_db.c) parses:
 *
 *     [quest]                            [quest_animalmagnetism]
 *     column=id,int                      table=quest
 *     column=sortname,string             data=id,123
 *     column=startcoord,coord            data=sortname,Animal Magnetism
 *     column=startnpc,npc                data=startcoord,0_49_52_31_10
 *     default=members,true               data=startnpc,ava
 *
 * A column's id is its POSITION among the block's `column=` lines, as in LostCity
 * and in both of this repo's readers (sscompile's `ssc_symbols.c` and the server).
 * There used to be a second, machine spelling for the cache export
 * (`columns=` / `columndef=<id>:<name>,<types>` / `values=<id>:<tuple>:<v>`), and
 * the two never agreed about anything; that spelling is gone and is not read.
 *
 * ## What the cache needs that LostCity never wrote
 *
 * Everything below is an *extension* the server reader already accepts, chosen so
 * a cache record round-trips byte-exactly:
 *
 *   - **A hole in the column numbering.** 27 of cache.osrs239's 246 tables skip
 *     ids (`quest` has no column 20 or 32). Positional ids can only say that with a
 *     line for the hole, so the hole is a column carrying the property `ABSENT`:
 *     `column=<name>,ABSENT`. Every positional reader counts it -- which is the
 *     point -- and the encoder gives it no entry. An upper-case word is a property
 *     to both LostCity and the server, never a type.
 *   - **The table's column-array size** (the `alloc` byte) is the number of
 *     `column=` lines, holes included; a row's is its table's, which every row in
 *     cache.osrs239 and cache.osrs230 agrees with.
 *   - **A string field holding a comma** that is not the tuple's last field (18
 *     in cache.osrs239). The last field takes the rest of the line, commas and
 *     all, exactly as the server reads it; an earlier one escapes it `\,`. So
 *     does `\\`, a trailing blank (`\ `), the second slash of `//` (`/\/`), and a
 *     leading `^` (`\^`), each because the server's line cleaner or constant
 *     expansion would otherwise take it. The server's db reader reads every one
 *     of them back (its clean is escape-aware: `\,` `\\` `\ ` `/\/` `\^` `\n`
 *     `\r`); no authored line needs one.
 *
 * Values are spelled as the content spells them: a reference by name (`null` for
 * -1), a coord as `level_mx_mz_lx_lz`, a boolean as `true`/`false`, a string raw.
 * Bare numbers are read wherever the server's reader reads them, and a `^name`
 * expands to the constant's text first, as it does there.
 *
 * ## Rows need their table
 *
 * A `data=` line carries a column NAME and untyped values, so packing a row needs
 * the table's declaration: which position the name is, and whether each value is a
 * string, a coord or a reference. `pack` reads every `.dbtable` in the tree and
 * merges the layers itself (`db_schema`); `verify` and `unpack`, which have a cache
 * and no walked tree, take the cache's own table records.
 *
 * ## Full keys
 *
 * The column block is one opcode (3 on a row, 1 on a table). When the stream does
 * not carry it, `column`/`default` and `data` read `default`; when it carries it
 * with nothing in it, `empty`. A row's `table` is `default` when opcode 4 is
 * absent.
 */

const struct CP_KeySpec cp_dbrow_keys[] = {
    { "table", 0, NULL, NULL },
    { "data", CP_KEY_LIST, NULL, NULL },
    { NULL, 0, NULL, NULL },
};

const struct CP_KeySpec cp_dbtable_keys[] = {
    { "column", CP_KEY_LIST, NULL, NULL },
    { "default", CP_KEY_LIST, NULL, "column" },
    { NULL, 0, NULL, NULL },
};

/** The property marking a hole in a table's column numbering. */
#define DB_ABSENT "ABSENT"

/* ---- the tuple-type alphabet -------------------------------------------- */

/*
 * A column's tuple positions carry ScriptVarType *ids*, the numbers the dat2
 * DBTABLE record stores (a coord is 22 here, where a param or an enum stores the
 * character 'c'). The id, its word and how a value of it is spelled all come
 * from the one table in cp_value.c, which the params, the enums and the server
 * band writer read too -- so an authored `column=bar,namedobj` and a cache export
 * say the same thing, and a value is spelled the same in a dbrow as in a param.
 * A boolean in a dbrow is `true`/`false`, as LostCity's dbrows write it.
 *
 * An unknown id round-trips as its own number, which is what keeps a newer cache
 * from losing a type the table has never seen.
 */

/** A column type's word, or its id in decimal. */
static void
db_type_name(
    int code,
    char* out,
    size_t out_size)
{
    const char* text = cp_value_id_text(code, out, out_size);

    if( text != out )
        snprintf(out, out_size, "%s", text);
}

/* ---- line text: escaping ------------------------------------------------ */

/*
 * One field into `buf` at `*w`. `last` is the tuple's last field, which takes the
 * rest of the line and so may hold a bare comma. See the file comment for why
 * each escape exists.
 */
static void
db_append_field(
    char* buf,
    size_t cap,
    int* w,
    const char* text,
    int last)
{
    size_t length = strlen(text);
    size_t trailing = 0;

    if( last )
    {
        while( trailing < length && (unsigned char)text[length - 1 - trailing] <= ' ' )
            trailing++;
    }
    for( size_t i = 0; i < length; i++ )
    {
        unsigned char c = (unsigned char)text[i];
        int escape = c == '\\' || (c == ',' && !last) || (c == '^' && i == 0) ||
                     (c == '/' && i > 0 && text[i - 1] == '/') || i >= length - trailing;

        assert(*w + 4 < (int)cap);
        if( c == '\n' || c == '\r' )
        {
            buf[(*w)++] = '\\';
            buf[(*w)++] = c == '\n' ? 'n' : 'r';
            continue;
        }
        if( escape )
            buf[(*w)++] = '\\';
        buf[(*w)++] = (char)c;
    }
    buf[*w] = '\0';
}

/** Is `s[at]` escaped -- preceded by an odd run of backslashes? */
static int
db_escaped(
    const char* s,
    size_t at)
{
    size_t run = 0;

    while( at > run && s[at - 1 - run] == '\\' )
        run++;
    return run & 1;
}

/**
 * The server's line cleaner, escape-aware: cut at the first unescaped `//` and
 * trim unescaped trailing blanks. In place.
 */
static void
db_clean_value(char* s)
{
    size_t length;

    for( size_t i = 0; s[i]; i++ )
    {
        if( s[i] == '\\' && s[i + 1] )
        {
            i++;
            continue;
        }
        if( s[i] == '/' && s[i + 1] == '/' )
        {
            s[i] = '\0';
            break;
        }
    }
    length = strlen(s);
    while( length && (unsigned char)s[length - 1] <= ' ' && !db_escaped(s, length - 1) )
        s[--length] = '\0';
    while( *s == ' ' || *s == '\t' )
        memmove(s, s + 1, strlen(s));
}

/** Undo `db_append_field`'s escapes, in place. */
static void
db_unescape(char* s)
{
    char* w = s;

    for( char* r = s; *r; r++ )
    {
        if( *r == '\\' && r[1] )
        {
            r++;
            *w++ = *r == 'n' ? '\n' : *r == 'r' ? '\r' : *r;
            continue;
        }
        *w++ = *r;
    }
    *w = '\0';
}

/**
 * Split a cleaned value `<head>,<f1>,...,<fn>` into the head and exactly `n`
 * fields: the first n-1 end at an unescaped comma, the last takes the rest. The
 * pieces are left escaped (a caret is only a constant when it is not). Returns
 * the number of fields found, which is short of `n` when the line is.
 */
static int
db_split(
    char* s,
    int n,
    char** head,
    char** fields)
{
    char* cursor = s;
    int found = 0;

    *head = s;
    for( ;; )
    {
        char* comma = NULL;

        for( char* p = cursor; *p; p++ )
        {
            if( *p == '\\' && p[1] )
            {
                p++;
                continue;
            }
            if( *p == ',' )
            {
                comma = p;
                break;
            }
        }
        if( cursor == s )
        {
            /* The head: the column name. */
            if( !comma )
                return 0;
            *comma = '\0';
            cursor = comma + 1;
            continue;
        }
        if( found == n - 1 || !comma )
        {
            fields[found++] = cursor;
            return found;
        }
        *comma = '\0';
        fields[found++] = cursor;
        cursor = comma + 1;
    }
}

/* ---- typed values, for every reader (cachepack.h) ----------------------- */

void
cp_value_clean(char* text)
{
    assert(text);
    db_clean_value(text);
}

int
cp_value_split(
    char* text,
    int n,
    char** fields)
{
    int found = 0;
    char* cursor = text;

    assert(text);
    assert(n > 0);
    assert(fields);
    db_clean_value(text);
    while( found < n )
    {
        char* comma = NULL;

        if( found < n - 1 )
        {
            for( char* p = cursor; *p; p++ )
            {
                if( *p == '\\' && p[1] )
                {
                    p++;
                    continue;
                }
                if( *p == ',' )
                {
                    comma = p;
                    break;
                }
            }
        }
        fields[found++] = cursor;
        if( !comma )
            break;
        *comma = '\0';
        cursor = comma + 1;
    }
    for( int i = 0; i < found; i++ )
    {
        if( fields[i][0] != '^' )
            db_unescape(fields[i]);
    }
    return found;
}

/* ---- columns: a parsed block -------------------------------------------- */

struct DbColText
{
    char* name;
    int absent; /* an `ABSENT` hole: a position with no column */
    int type_count;
    int* types;
};

/** A table as its text declares it: the schema rows are packed against. */
struct DbSchema
{
    int valid;    /* parsed without error */
    int stated;   /* the column block (opcode 1) is present */
    int count;    /* column lines, holes included: the alloc byte */
    struct DbColText* cols;
};

static void
db_schema_free(struct DbSchema* schema)
{
    for( int i = 0; i < schema->count; i++ )
    {
        free(schema->cols[i].name);
        free(schema->cols[i].types);
    }
    free(schema->cols);
    memset(schema, 0, sizeof(*schema));
}

static int
db_schema_find(
    const struct DbSchema* schema,
    const char* name)
{
    for( int i = 0; i < schema->count; i++ )
    {
        if( !schema->cols[i].absent && strcmp(schema->cols[i].name, name) == 0 )
            return i;
    }
    return -1;
}

static int
db_is_property(const char* token)
{
    for( const char* p = token; *p; p++ )
    {
        if( *p >= 'a' && *p <= 'z' )
            return 0;
    }
    return *token && !(*token >= '0' && *token <= '9') && *token != '-';
}

/**
 * The `column=` lines of one block. `where` names it in errors. LostCity's
 * properties (INDEXED, REQUIRED, LIST, CLIENTSIDE) are not in the dat2 record and
 * are accepted and dropped; `ABSENT` makes the line a hole.
 */
static int
db_parse_columns(
    const struct CP_Config* config,
    const char* where,
    struct DbSchema* out)
{
    int capacity = 0;

    memset(out, 0, sizeof(*out));
    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        char text[8192];
        char* cursor;
        struct DbColText col;

        if( strcmp(key, "column") != 0 || cp_value_is_default(config->lines[i].value) )
            continue;
        out->stated = 1;
        if( cp_value_is_empty(config->lines[i].value) )
            continue;

        snprintf(text, sizeof(text), "%s", config->lines[i].value);
        db_clean_value(text);
        memset(&col, 0, sizeof(col));
        cursor = text;
        for( int part = 0; cursor; part++ )
        {
            char* comma = strchr(cursor, ',');

            if( comma )
                *comma = '\0';
            if( part == 0 )
            {
                if( !*cursor )
                {
                    fprintf(stderr, "cachepack: dbtable [%s]: a column with no name\n", where);
                    goto fail_col;
                }
                col.name = strdup(cursor);
                assert(col.name);
            }
            else if( db_is_property(cursor) )
            {
                if( strcmp(cursor, DB_ABSENT) == 0 )
                    col.absent = 1;
                else if( strcmp(cursor, "INDEXED") != 0 && strcmp(cursor, "REQUIRED") != 0 &&
                         strcmp(cursor, "LIST") != 0 && strcmp(cursor, "CLIENTSIDE") != 0 )
                {
                    fprintf(stderr, "cachepack: dbtable [%s]: column `%s` has unknown property %s\n",
                            where, col.name, cursor);
                    goto fail_col;
                }
            }
            else
            {
                int code = cp_value_id_read(cursor);

                if( code < 0 || code > 0xFFFF )
                {
                    fprintf(stderr, "cachepack: dbtable [%s]: column `%s` has unknown type %s\n",
                            where, col.name, cursor);
                    goto fail_col;
                }
                col.types = realloc(col.types, (size_t)(col.type_count + 1) * sizeof(int));
                assert(col.types);
                col.types[col.type_count++] = code;
            }
            cursor = comma ? comma + 1 : NULL;
        }
        if( col.absent && col.type_count > 0 )
        {
            fprintf(stderr, "cachepack: dbtable [%s]: column `%s` is ABSENT and has types\n",
                    where, col.name);
            goto fail_col;
        }
        if( !col.absent && db_schema_find(out, col.name) >= 0 )
        {
            fprintf(stderr, "cachepack: dbtable [%s]: column `%s` declared twice\n", where,
                    col.name);
            goto fail_col;
        }
        if( out->count == 0xFF )
        {
            fprintf(stderr, "cachepack: dbtable [%s]: more than 255 columns\n", where);
            goto fail_col;
        }
        if( out->count == capacity )
        {
            capacity = capacity ? capacity * 2 : 16;
            out->cols = realloc(out->cols, (size_t)capacity * sizeof(*out->cols));
            assert(out->cols);
        }
        out->cols[out->count++] = col;
        continue;

    fail_col:
        free(col.name);
        free(col.types);
        db_schema_free(out);
        return 0;
    }
    out->valid = 1;
    return 1;
}

/**
 * One `data=` / `default=` value against a schema: find the column, read its
 * tuple and append it to `cols[column]`. `cols` is the record's column array,
 * `schema->count` long.
 */
static int
db_parse_tuple(
    struct CP_Ctx* ctx,
    const char* type_name,
    const char* where,
    const struct DbSchema* schema,
    const char* raw,
    struct RSCache_DbColumn* cols)
{
    char text[8192];
    char* head;
    char* fields[256];
    int index;
    const struct DbColText* spec;
    struct RSCache_DbColumn* col;
    int found;

    if( strlen(raw) >= sizeof(text) )
    {
        fprintf(stderr, "cachepack: %s [%s]: a line longer than %d bytes\n", type_name, where,
                (int)sizeof(text));
        return 0;
    }
    snprintf(text, sizeof(text), "%s", raw);
    db_clean_value(text);

    /* The head first, to know how many fields to cut. */
    {
        char probe[8192];
        char* probe_head;
        char* probe_fields[1];

        snprintf(probe, sizeof(probe), "%s", text);
        if( db_split(probe, 1, &probe_head, probe_fields) < 1 )
        {
            fprintf(stderr, "cachepack: %s [%s]: `%s` needs `column,value[,value...]`\n",
                    type_name, where, raw);
            return 0;
        }
        db_unescape(probe_head);
        index = db_schema_find(schema, probe_head);
        if( index < 0 )
        {
            fprintf(stderr, "cachepack: %s [%s]: the table has no column `%s`\n", type_name,
                    where, probe_head);
            return 0;
        }
    }
    spec = &schema->cols[index];
    if( spec->type_count < 1 || spec->type_count > (int)(sizeof(fields) / sizeof(fields[0])) )
    {
        fprintf(stderr, "cachepack: %s [%s]: column `%s` declares %d types\n", type_name, where,
                spec->name, spec->type_count);
        return 0;
    }
    found = db_split(text, spec->type_count, &head, fields);
    if( found != spec->type_count )
    {
        fprintf(stderr, "cachepack: %s [%s]: column `%s` takes %d value(s), got %d\n", type_name,
                where, spec->name, spec->type_count, found);
        return 0;
    }

    col = &cols[index];
    if( !col->present )
    {
        col->present = true;
        col->type_count = spec->type_count;
        col->types = malloc((size_t)spec->type_count * sizeof(int));
        assert(col->types);
        memcpy(col->types, spec->types, (size_t)spec->type_count * sizeof(int));
    }
    col->values = realloc(col->values, (size_t)((col->tuple_count + 1) * col->type_count) *
                                           sizeof(*col->values));
    assert(col->values);
    /* Zeroed whole before any field is read, so a failure part-way leaves a tuple
     * the free can walk. */
    memset(&col->values[col->tuple_count * col->type_count], 0,
           (size_t)col->type_count * sizeof(*col->values));
    for( int f = 0; f < col->type_count; f++ )
    {
        struct RSCache_DbValue* v = &col->values[col->tuple_count * col->type_count + f];
        char* field = fields[f];
        const char* value = field;

        /* `^name` is a constant unless the caret is escaped, as the server reads it. */
        if( field[0] == '^' )
        {
            value = cp_value_constant_text(ctx, field + 1);
            if( !value )
            {
                int caret = 0;

                /* `^true` / `^false`, which the language defines. */
                if( cp_resolve_caret(ctx, field, &caret) )
                {
                    static char number[32];
                    snprintf(number, sizeof(number), "%d", caret);
                    value = number;
                }
            }
            if( !value )
            {
                fprintf(stderr, "cachepack: %s [%s]: no `%s` in any .constant\n", type_name,
                        where, field);
                /* The tuple is counted so the free below releases its strings. */
                col->tuple_count++;
                return 0;
            }
        }
        else
            db_unescape(field);

        if( RSCache_DbTypeIsString(col->types[f]) )
        {
            v->is_string = true;
            v->string_value = strdup(value);
            assert(v->string_value);
        }
        else if( !cp_value_int_read(ctx, cp_value_type_of_id(col->types[f]), value,
                                    &v->int_value) )
        {
            char type_word[32];

            db_type_name(col->types[f], type_word, sizeof(type_word));
            fprintf(stderr, "cachepack: %s [%s]: column `%s` value `%s` is not a %s\n",
                    type_name, where, spec->name, value, type_word);
            col->tuple_count++;
            return 0;
        }
    }
    col->tuple_count++;
    return 1;
}

/* ---- the table schemas a row is packed against -------------------------- */

/*
 * Indexed by table id. Built once per context: from the walked tree when there is
 * one (`pack`), else from the open cache (`verify`, which packs the text it just
 * unpacked and must read it against the same tables).
 */
static struct
{
    const struct CP_Ctx* ctx;
    int built;
    struct DbSchema* tables;
    int count;
} g_schemas;

static void
db_schemas_put(
    int id,
    struct DbSchema* schema)
{
    if( id < 0 || id > 0xFFFFFF )
    {
        db_schema_free(schema);
        return;
    }
    if( id >= g_schemas.count )
    {
        g_schemas.tables =
            realloc(g_schemas.tables, (size_t)(id + 1) * sizeof(*g_schemas.tables));
        assert(g_schemas.tables);
        memset(&g_schemas.tables[g_schemas.count], 0,
               (size_t)(id + 1 - g_schemas.count) * sizeof(*g_schemas.tables));
        g_schemas.count = id + 1;
    }
    db_schema_free(&g_schemas.tables[id]);
    g_schemas.tables[id] = *schema;
}

static void db_unpack_table_schema(struct CP_Ctx*, int, const struct RSCache_Dat2ConfigDbTable*,
                                   struct DbSchema*);

static void
db_schemas_build(struct CP_Ctx* ctx)
{
    for( int i = 0; i < g_schemas.count; i++ )
        db_schema_free(&g_schemas.tables[i]);
    free(g_schemas.tables);
    memset(&g_schemas, 0, sizeof(g_schemas));
    g_schemas.ctx = ctx;
    g_schemas.built = 1;

    if( ctx->walk.count > 0 )
    {
        /*
         * Every `.dbtable` layer, merged the way the packer merges the type, so a
         * row reads the same declaration the dbtable pass encodes. The packer
         * packs dbrow before dbtable and keeps its merge private, so this is a
         * second merge of the same files rather than a shared one.
         */
        const char* found[CP_PACK_MAX_SOURCES];
        int found_count = cp_walk_find(&ctx->walk, "dbtable", found, CP_PACK_MAX_SOURCES);
        struct CP_MergeSet merged;

        memset(&merged, 0, sizeof(merged));
        merged.keys = cp_dbtable_keys;
        for( int i = 0; i < found_count; i++ )
        {
            struct CP_ConfigFile layer;

            if( !cp_config_file_load(&layer, found[i]) )
                continue;
            cp_merge_add(&merged, &layer, cp_merge_rank_for(i, found[i]), found[i]);
            cp_config_file_free(&layer);
        }
        for( int r = 0; r < merged.count; r++ )
        {
            const struct CP_MergedRecord* rec = &merged.records[r];
            struct CP_ConfigLine* lines = malloc((size_t)(rec->count + 1) * sizeof(*lines));
            struct CP_Config view;
            struct DbSchema schema;
            int id = cp_name_find(ctx, CP_TYPE_DBTABLE, rec->debugname);

            assert(lines);
            for( int i = 0; i < rec->count; i++ )
            {
                lines[i].key = rec->lines[i].key;
                lines[i].value = rec->lines[i].value;
                lines[i].line_no = 0;
            }
            memset(&view, 0, sizeof(view));
            view.debugname = rec->debugname;
            view.lines = lines;
            view.count = rec->count;
            if( id >= 0 && db_parse_columns(&view, rec->debugname, &schema) )
                db_schemas_put(id, &schema);
            free(lines);
        }
        cp_merge_free(&merged);
        return;
    }

    if( ctx->cache_open )
    {
        struct CP_Group group;

        if( !cp_group_open(ctx, CP_TYPE_DBTABLE, &group) )
            return;
        for( int i = 0; i < group.count; i++ )
        {
            int size = 0;
            const uint8_t* record = cp_group_record(&group, i, &size);
            struct RSCache_Dat2ConfigDbTable entry;
            struct DbSchema schema;

            if( !record )
                continue;
            memset(&entry, 0, sizeof(entry));
            RSCache_Dat2ConfigDbTableDecodeInplace(&entry, record, size);
            db_unpack_table_schema(ctx, group.ids ? group.ids[i] : i, &entry, &schema);
            db_schemas_put(group.ids ? group.ids[i] : i, &schema);
            RSCache_Dat2ConfigDbTableFreeInplace(&entry);
        }
        cp_group_free(&group);
    }
}

static const struct DbSchema*
db_schema(
    struct CP_Ctx* ctx,
    int table_id)
{
    if( g_schemas.ctx != ctx || !g_schemas.built )
        db_schemas_build(ctx);
    if( table_id < 0 || table_id >= g_schemas.count || !g_schemas.tables[table_id].valid )
        return NULL;
    return &g_schemas.tables[table_id];
}

/* ---- unpack ------------------------------------------------------------- */

/** The text name of column `c` of `table_id`: the cache's, else `col<c>`. */
static const char*
db_column_text_name(
    struct CP_Ctx* ctx,
    int table_id,
    int c,
    char* buf,
    size_t buf_size)
{
    const char* name = cp_db_column_name(&ctx->names, table_id, c);

    if( name && *name && !strchr(name, ',') && !db_is_property(name) &&
        !cp_value_is_default(name) && !cp_value_is_empty(name) )
        return name;
    snprintf(buf, buf_size, "col%d", c);
    return buf;
}

/**
 * The schema a cache table's text states: one entry per id up to its alloc,
 * holes ABSENT. Never invalid -- every decoded table has a spelling.
 */
static void
db_unpack_table_schema(
    struct CP_Ctx* ctx,
    int table_id,
    const struct RSCache_Dat2ConfigDbTable* entry,
    struct DbSchema* out)
{
    memset(out, 0, sizeof(*out));
    out->valid = 1;
    out->stated = RSCache_PresenceHas(&entry->present, RSCACHE_DBTABLE_FIELD_COLUMNS);
    out->count = entry->column_count;
    if( out->count == 0 )
        return;
    out->cols = calloc((size_t)out->count, sizeof(*out->cols));
    assert(out->cols);
    for( int c = 0; c < out->count; c++ )
    {
        const struct RSCache_DbColumn* col = &entry->columns[c];
        char buf[32];

        out->cols[c].name = strdup(db_column_text_name(ctx, table_id, c, buf, sizeof(buf)));
        assert(out->cols[c].name);
        out->cols[c].absent = !col->present;
        out->cols[c].type_count = col->present ? col->type_count : 0;
        if( out->cols[c].type_count > 0 )
        {
            out->cols[c].types = malloc((size_t)col->type_count * sizeof(int));
            assert(out->cols[c].types);
            memcpy(out->cols[c].types, col->types, (size_t)col->type_count * sizeof(int));
        }
    }
}

/** `<key>=<name>,<v1>,...` for one tuple. */
static void
db_emit_tuple(
    struct CP_Ctx* ctx,
    struct CP_Lines* out,
    const char* key,
    const char* name,
    const struct RSCache_DbColumn* col,
    int tuple)
{
    static char buf[65536];
    int w = snprintf(buf, sizeof(buf), "%s", name);

    for( int f = 0; f < col->type_count; f++ )
    {
        const struct RSCache_DbValue* v = &col->values[tuple * col->type_count + f];
        char number[64];
        const char* text = v->is_string
                               ? (v->string_value ? v->string_value : "")
                               : cp_value_int_text(ctx, cp_value_type_of_id(col->types[f]),
                                                   v->int_value, CP_BOOL_TRUE_FALSE, number,
                                                   sizeof(number));

        buf[w++] = ',';
        buf[w] = '\0';
        db_append_field(buf, sizeof(buf), &w, text, f == col->type_count - 1);
    }
    cp_lines_addf(out, "%s=%s", key, buf);
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
    struct DbSchema schema;
    int defaults = 0;
    int ok = 1;

    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigDbTableDecodeInplace(&entry, record, record_size);
    db_unpack_table_schema(ctx, id, &entry, &schema);

    if( !schema.stated )
    {
        cp_lines_add_default(out, "column");
        cp_lines_add_default(out, "default");
        goto done;
    }
    if( schema.count == 0 )
        cp_lines_add_empty(out, "column");
    for( int c = 0; c < schema.count; c++ )
    {
        const struct DbColText* spec = &schema.cols[c];
        char line[8192];
        int w = snprintf(line, sizeof(line), "%s", spec->name);

        /* A trailing hole is a hole like any other: the line count is the alloc. */
        if( spec->absent )
            w += snprintf(line + w, sizeof(line) - (size_t)w, ",%s", DB_ABSENT);
        for( int t = 0; t < spec->type_count; t++ )
        {
            char word[32];

            db_type_name(spec->types[t], word, sizeof(word));
            w += snprintf(line + w, sizeof(line) - (size_t)w, ",%s", word);
        }
        cp_lines_addf(out, "column=%s", line);
    }
    for( int c = 0; c < entry.column_count; c++ )
    {
        const struct RSCache_DbColumn* col = &entry.columns[c];

        if( !col->present )
            continue;
        if( col->type_count == 0 && col->tuple_count > 0 )
        {
            fprintf(stderr, "cachepack: dbtable %d: column %d has defaults and no types — the "
                            "grammar cannot state that\n", id, c);
            ok = 0;
        }
        for( int t = 0; t < col->tuple_count && col->values; t++ )
        {
            db_emit_tuple(ctx, out, "default", schema.cols[c].name, col, t);
            defaults++;
        }
    }
    if( defaults == 0 )
        cp_lines_add_empty(out, "default");

done:
    db_schema_free(&schema);
    RSCache_Dat2ConfigDbTableFreeInplace(&entry);
    return ok;
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
    const struct DbSchema* schema;
    int has_table;
    int lines = 0;
    int ok = 1;

    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigDbRowDecodeInplace(&entry, record, record_size);
    has_table = RSCache_PresenceHas(&entry.present, RSCACHE_DBROW_FIELD_TABLE);

    if( has_table )
        cp_emit_name(ctx, out, "table", CP_TYPE_DBTABLE, entry.table_id);
    else
        cp_lines_add_default(out, "table");

    if( !RSCache_PresenceHas(&entry.present, RSCACHE_DBROW_FIELD_COLUMNS) )
    {
        cp_lines_add_default(out, "data");
        goto done;
    }

    /*
     * A `data=` line names its column and nothing else, so the row is only
     * expressible if its table declares exactly what the row carries: the same
     * column positions, the same types, the same array size. Every row in
     * cache.osrs239 and cache.osrs230 does. One that does not is refused here,
     * by name, rather than written as text that packs to different bytes.
     */
    schema = has_table ? db_schema(ctx, entry.table_id) : NULL;
    if( !schema )
    {
        fprintf(stderr, "cachepack: dbrow %d: %s — its values cannot be named\n", id,
                has_table ? "its table is not in the cache" : "it has columns and no table");
        ok = 0;
        goto done;
    }
    if( entry.column_count != schema->count )
    {
        fprintf(stderr, "cachepack: dbrow %d: %d column slots where its table has %d\n", id,
                entry.column_count, schema->count);
        ok = 0;
    }
    for( int c = 0; c < entry.column_count && ok; c++ )
    {
        const struct RSCache_DbColumn* col = &entry.columns[c];
        const struct DbColText* spec;

        if( !col->present )
            continue;
        spec = c < schema->count ? &schema->cols[c] : NULL;
        if( !spec || spec->absent || spec->type_count != col->type_count ||
            memcmp(spec->types, col->types, (size_t)col->type_count * sizeof(int)) != 0 )
        {
            fprintf(stderr, "cachepack: dbrow %d: column %d is not declared that way by its "
                            "table\n", id, c);
            ok = 0;
            break;
        }
        if( col->tuple_count == 0 || !col->values )
        {
            fprintf(stderr, "cachepack: dbrow %d: column %d is listed with no values — the "
                            "grammar cannot state that\n", id, c);
            ok = 0;
            break;
        }
        for( int t = 0; t < col->tuple_count; t++ )
        {
            db_emit_tuple(ctx, out, "data", spec->name, col, t);
            lines++;
        }
    }
    if( ok && lines == 0 )
        cp_lines_add_empty(out, "data");

done:
    RSCache_Dat2ConfigDbRowFreeInplace(&entry);
    return ok;
}

/* ---- pack --------------------------------------------------------------- */

/** The machine spelling that preceded this one. Read as an error, not skipped. */
static int
db_legacy_key(
    const char* type_name,
    const struct CP_Config* config)
{
    static const char* const k_legacy[] = { "columns", "columndef", "values", "defaults",
                                            "types", "defaulttypes" };

    for( int i = 0; i < config->count; i++ )
    {
        for( size_t k = 0; k < sizeof(k_legacy) / sizeof(k_legacy[0]); k++ )
        {
            if( strcmp(config->lines[i].key, k_legacy[k]) == 0 )
            {
                fprintf(stderr, "cachepack: %s [%s]: `%s=` is the retired machine spelling — "
                                "a db block is `column=`/`default=` or `table=`/`data=`\n",
                        type_name, config->debugname, k_legacy[k]);
                return 1;
            }
        }
    }
    return 0;
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
    struct DbSchema schema;
    uint32_t written = 0;
    int ok = 1;

    if( db_legacy_key("dbtable", config) )
        return 0;
    if( !db_parse_columns(config, config->debugname, &schema) )
        return 0;

    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigDbTableDecodeInplace(&entry, cp_empty_record, (int)sizeof(cp_empty_record));
    entry.id = id;
    if( schema.stated )
        RSCache_PresenceSet(&entry.present, RSCACHE_DBTABLE_FIELD_COLUMNS);
    entry.column_count = schema.count;
    if( schema.count > 0 )
    {
        entry.columns = calloc((size_t)schema.count, sizeof(*entry.columns));
        assert(entry.columns);
    }
    for( int c = 0; c < schema.count; c++ )
    {
        if( schema.cols[c].absent )
            continue;
        entry.columns[c].present = true;
        entry.columns[c].type_count = schema.cols[c].type_count;
        if( schema.cols[c].type_count > 0 )
        {
            entry.columns[c].types = malloc((size_t)schema.cols[c].type_count * sizeof(int));
            assert(entry.columns[c].types);
            memcpy(entry.columns[c].types, schema.cols[c].types,
                   (size_t)schema.cols[c].type_count * sizeof(int));
        }
    }

    for( int i = 0; i < config->count && ok; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;

        if( strcmp(key, "column") == 0 )
            continue;
        if( strcmp(key, "default") != 0 )
        {
            cp_warn(ctx, &ctx->warn_unknown_key, "dbtable [%s]: unknown key %s",
                    config->debugname, key);
            continue;
        }
        if( cp_value_is_default(value) || cp_value_is_empty(value) )
            continue;
        if( !schema.stated )
        {
            fprintf(stderr, "cachepack: dbtable [%s]: a default with no columns\n",
                    config->debugname);
            ok = 0;
            break;
        }
        ok = db_parse_tuple(ctx, "dbtable", config->debugname, &schema, value, entry.columns);
    }

    if( ok )
    {
        assert(out_capacity >= RSCache_Dat2ConfigDbTableEncodeBound(&entry));
        written = RSCache_Dat2ConfigDbTableEncode(&entry, out, out_capacity);
    }
    db_schema_free(&schema);
    RSCache_Dat2ConfigDbTableFreeInplace(&entry);
    return written;
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
    const struct DbSchema* schema = NULL;
    int stated = 0;
    uint32_t written = 0;
    int ok = 1;

    if( db_legacy_key("dbrow", config) )
        return 0;

    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigDbRowDecodeInplace(&entry, cp_empty_record, (int)sizeof(cp_empty_record));
    entry.id = id;

    /* The table first, wherever its line sits: every value is read against it. */
    for( int i = 0; i < config->count; i++ )
    {
        const char* value = config->lines[i].value;
        char name[1024];

        if( strcmp(config->lines[i].key, "table") != 0 || cp_value_is_default(value) )
            continue;
        snprintf(name, sizeof(name), "%s", value);
        db_clean_value(name);
        if( !cp_resolve_ref(ctx, CP_TYPE_DBTABLE, name, &entry.table_id) || entry.table_id < 0 )
        {
            fprintf(stderr, "cachepack: dbrow [%s]: unknown table `%s`\n", config->debugname,
                    name);
            return 0;
        }
        RSCache_PresenceSet(&entry.present, RSCACHE_DBROW_FIELD_TABLE);
    }

    for( int i = 0; i < config->count && ok; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;

        if( strcmp(key, "table") == 0 )
            continue;
        if( strcmp(key, "data") != 0 )
        {
            cp_warn(ctx, &ctx->warn_unknown_key, "dbrow [%s]: unknown key %s", config->debugname,
                    key);
            continue;
        }
        if( cp_value_is_default(value) )
            continue;
        if( !stated )
        {
            stated = 1;
            RSCache_PresenceSet(&entry.present, RSCACHE_DBROW_FIELD_COLUMNS);
            if( !RSCache_PresenceHas(&entry.present, RSCACHE_DBROW_FIELD_TABLE) )
            {
                fprintf(stderr, "cachepack: dbrow [%s]: `data=` with no `table=`\n",
                        config->debugname);
                ok = 0;
                break;
            }
            schema = db_schema(ctx, entry.table_id);
            if( !schema || !schema->stated )
            {
                fprintf(stderr, "cachepack: dbrow [%s]: table `%s` declares no columns here\n",
                        config->debugname, cp_name_ensure(ctx, CP_TYPE_DBTABLE, entry.table_id));
                ok = 0;
                break;
            }
            /* A row's column array is its table's, holes included. */
            entry.column_count = schema->count;
            if( schema->count > 0 )
            {
                entry.columns = calloc((size_t)schema->count, sizeof(*entry.columns));
                assert(entry.columns);
            }
        }
        if( cp_value_is_empty(value) )
            continue;
        ok = db_parse_tuple(ctx, "dbrow", config->debugname, schema, value, entry.columns);
    }

    if( ok )
    {
        assert(out_capacity >= RSCache_Dat2ConfigDbRowEncodeBound(&entry));
        written = RSCache_Dat2ConfigDbRowEncode(&entry, out, out_capacity);
    }
    RSCache_Dat2ConfigDbRowFreeInplace(&entry);
    return written;
}
