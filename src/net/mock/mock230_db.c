/*
 * `.dbtable` / `.dbrow` — the server's client-database tables.
 *
 * See mock230_db.h for the shape and for why these are a different population
 * from the cache's db tables. This file is the reader; mock230_ops_db.c is the
 * RuneScript surface over it.
 *
 * Two things here are worth knowing before editing:
 *
 * **Tables must be fully read before any row is.** A `data=` line cannot be
 * parsed without its column's declared tuple types — `data=coord_pair,A,B` is
 * two coords or one string depending on the table — so mock230_db_load makes two
 * passes over the tree rather than one. A single walk that happened to read
 * tables first would work until someone added a `.dbrow` that sorted earlier.
 *
 * **A `data=` line with the wrong arity is rejected, not truncated.** The values
 * are stored flat and the tuple count is derived by division, so one short line
 * would silently reinterpret every tuple after it in that column.
 */

#include "mock230_db.h"

#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Declared in mock230_content.h. Routed through there so a bad line in a
 * `.dbtable` counts the same as a bad line anywhere else and the server refuses
 * to start on it. */
#define DB_ERROR(...) mock230_content_report_error(__VA_ARGS__)

static struct Mock230DbTable* g_tables;
static int g_table_count;
static int g_table_capacity;

static struct Mock230DbRow* g_rows;
static int g_row_count;
static int g_row_capacity;

static void*
db_grow(
    void* array,
    int* capacity,
    int count,
    size_t element)
{
    void* grown;

    if( count < *capacity )
        return array;
    *capacity = *capacity ? *capacity * 2 : 32;
    grown = realloc(array, (size_t)*capacity * element);
    return grown ? grown : array;
}

/* ------------------------------------------------------------------ */
/* Value types                                                         */
/* ------------------------------------------------------------------ */

/*
 * A tuple position's declared type, mapped onto how to read it.
 *
 * `LIST`, `INDEXED` and `REQUIRED` are *flags* on the column, not types, and the
 * reference writes them in the same comma-separated list. They are recognised by
 * being upper-case, which is the discriminator the format actually offers.
 */
static int
db_type_is_flag(const char* text)
{
    for( const char* scan = text; *scan; scan++ )
    {
        if( *scan >= 'a' && *scan <= 'z' )
            return 0;
    }
    return *text != '\0';
}

/*
 * The type words that carry no symbol: the value is read as itself.
 *
 * Stated as a list rather than inferred from "db_kind_for_type said COUNT",
 * because those are two different answers and collapsing them is a silent
 * default. `int` resolves against no pack; `synth` resolves against a pack this
 * runtime does not load — and both used to reach `atoi()`, so a column declared
 * `synth` turned every sound *name* in it into 0 without a word. LostCity's
 * `consume.dbtable` has one, its `prayers.dbtable` has one, and 340 of its dbrows
 * name a sound or a music track; the tree's `pack/4_soundeffects.pack` is 12,010
 * lines of `synth_<id>` filler, so not one of those names could ever have
 * resolved. Reading them as zero is triage §13 bar 1 exactly — an unresolved name
 * answered with a default.
 *
 * So an unrecognised type word is a load error naming the word. The fix for
 * `synth`/`midi` is to name the sound and music namespaces and give this runtime
 * their packs, not to widen this list.
 */
static int
db_type_is_literal(const char* name)
{
    static const char* const k_literals[] = { "int", "string", "boolean", "coord" };

    for( size_t i = 0; i < sizeof(k_literals) / sizeof(k_literals[0]); i++ )
    {
        if( strcmp(name, k_literals[i]) == 0 )
            return 1;
    }
    return 0;
}

/** The pack a declared type resolves against, or MOCK230_PACK_COUNT for a
 *  literal. Mirrors mock230_content.c's `.enum` type table — same question. */
static enum Mock230PackKind
db_kind_for_type(const char* name)
{
    static const struct
    {
        const char* name;
        enum Mock230PackKind kind;
    } k_map[] = {
        { "npc", MOCK230_PACK_NPC },           { "namedobj", MOCK230_PACK_OBJ },
        { "obj", MOCK230_PACK_OBJ },           { "loc", MOCK230_PACK_LOC },
        { "seq", MOCK230_PACK_SEQ },           { "spotanim", MOCK230_PACK_SPOTANIM },
        { "inv", MOCK230_PACK_INV },           { "varp", MOCK230_PACK_VARP },
        { "interface", MOCK230_PACK_INTERFACE },
        { "component", MOCK230_PACK_COMPONENT },
        { "stat", MOCK230_PACK_STAT },         { "param", MOCK230_PACK_PARAM },
        { "category", MOCK230_PACK_CATEGORY }, { "enum", MOCK230_PACK_ENUM },
        { "struct", MOCK230_PACK_STRUCT },     { "dbrow", MOCK230_PACK_DBROW },
        { "dbtable", MOCK230_PACK_DBTABLE },
    };

    for( size_t i = 0; i < sizeof(k_map) / sizeof(k_map[0]); i++ )
    {
        if( strcmp(name, k_map[i].name) == 0 )
            return k_map[i].kind;
    }
    return MOCK230_PACK_COUNT;
}

/*
 * A coord literal: `level_mx_mz_lx_lz`.
 *
 * Packed exactly as ssc_lex.c packs it — `(level << 28) | ((mx * 64 + lx) << 14)
 * | (mz * 64 + lz)` — because the compiler and this reader hand the same number
 * to the same host commands. Two packings would put a script's literal and a
 * config's literal in different places on the map, which reads as a content bug
 * rather than a decoder one.
 */
static int
db_parse_coord(
    const char* text,
    int* out_ok)
{
    int parts[5];
    int count = 0;
    const char* scan = text;

    *out_ok = 0;
    while( count < 5 )
    {
        int value = 0;
        int digits = 0;

        while( *scan >= '0' && *scan <= '9' )
        {
            value = value * 10 + (*scan - '0');
            scan++;
            digits++;
        }
        if( !digits )
            return 0;
        parts[count++] = value;
        if( *scan == '_' )
        {
            scan++;
            continue;
        }
        break;
    }
    if( count != 5 || *scan != '\0' )
        return 0;
    *out_ok = 1;
    return (int)(((unsigned)parts[0] << 28) |
                 ((unsigned)((parts[1] * 64) + parts[3]) << 14) |
                 (unsigned)((parts[2] * 64) + parts[4]));
}

/* ------------------------------------------------------------------ */
/* Lookups                                                             */
/* ------------------------------------------------------------------ */

static struct Mock230DbTable*
table_by_symbol(const char* symbol)
{
    for( int i = 0; i < g_table_count; i++ )
    {
        if( g_tables[i].symbol && strcmp(g_tables[i].symbol, symbol) == 0 )
            return &g_tables[i];
    }
    return NULL;
}

const struct Mock230DbTable*
mock230_db_table(int table_id)
{
    if( table_id < 0 )
        return NULL;
    for( int i = 0; i < g_table_count; i++ )
    {
        if( g_tables[i].table_id == table_id )
            return &g_tables[i];
    }
    return NULL;
}

const struct Mock230DbRow*
mock230_db_row(int row_id)
{
    if( row_id < 0 )
        return NULL;
    for( int i = 0; i < g_row_count; i++ )
    {
        if( g_rows[i].row_id == row_id )
            return &g_rows[i];
    }
    return NULL;
}

int
mock230_db_column_index(
    const struct Mock230DbTable* table,
    const char* name)
{
    assert(table);
    assert(name);
    for( int i = 0; i < table->column_count; i++ )
    {
        if( table->columns[i].name && strcmp(table->columns[i].name, name) == 0 )
            return i;
    }
    return -1;
}

int
mock230_db_row_count(int table_id)
{
    int count = 0;

    for( int i = 0; i < g_row_count; i++ )
    {
        if( g_rows[i].table_id == table_id )
            count++;
    }
    return count;
}

const struct Mock230DbRow*
mock230_db_row_in_table(
    int table_id,
    int index)
{
    int seen = 0;

    if( index < 0 )
        return NULL;
    for( int i = 0; i < g_row_count; i++ )
    {
        if( g_rows[i].table_id != table_id )
            continue;
        if( seen == index )
            return &g_rows[i];
        seen++;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* .dbtable                                                            */
/* ------------------------------------------------------------------ */

static void
load_dbtable_file(const char* path)
{
    FILE* file = fopen(path, "rb");
    char raw[1024];
    struct Mock230DbTable* table = NULL;
    int line_number = 0;

    if( !file )
        return;
    while( fgets(raw, sizeof(raw), file) )
    {
        char* line = mock230_content_clean_line(raw);
        char* header;
        char* value;

        line_number++;
        if( !*line )
            continue;

        header = mock230_content_section_header(line);
        if( header )
        {
            g_tables = db_grow(g_tables, &g_table_capacity, g_table_count,
                               sizeof(*g_tables));
            table = &g_tables[g_table_count++];
            memset(table, 0, sizeof(*table));
            table->symbol = strdup(header);
            table->table_id = mock230_content_symbol(MOCK230_PACK_DBTABLE, header);
            if( table->table_id < 0 )
                DB_ERROR("%s:%d: dbtable `%s` has no id — run tools/ss_allocate.py\n",
                         path, line_number, header);
            continue;
        }

        value = mock230_content_split_key_value(line);
        if( !value || !table )
        {
            DB_ERROR("%s:%d: expected `key=value` inside a [section]\n", path,
                     line_number);
            continue;
        }
        if( strcmp(line, "column") != 0 )
            continue;

        {
            struct Mock230DbColumn* column;
            char* cursor = value;
            char* comma;

            if( table->column_count >= MOCK230_DB_COLUMN_MAX )
            {
                DB_ERROR("%s:%d: more than %d columns in one dbtable\n", path,
                         line_number, MOCK230_DB_COLUMN_MAX);
                continue;
            }
            column = &table->columns[table->column_count];
            memset(column, 0, sizeof(*column));

            comma = strchr(cursor, ',');
            if( !comma )
            {
                DB_ERROR("%s:%d: column needs `name,type[,type...]`\n", path,
                         line_number);
                continue;
            }
            *comma = '\0';
            column->name = strdup(cursor);
            cursor = comma + 1;

            while( *cursor )
            {
                char* end = strchr(cursor, ',');

                if( end )
                    *end = '\0';
                if( db_type_is_flag(cursor) )
                {
                    /* LIST / INDEXED / REQUIRED. Only LIST changes behaviour and
                     * it changes it for the *content*, not for us: every column
                     * here is read as a list of tuples, and a non-LIST column is
                     * simply one whose rows only ever declare one. */
                }
                else if( column->type_count >= MOCK230_DB_TUPLE_MAX )
                {
                    DB_ERROR("%s:%d: more than %d types in column `%s`\n", path,
                             line_number, MOCK230_DB_TUPLE_MAX, column->name);
                }
                else if( db_kind_for_type(cursor) == MOCK230_PACK_COUNT &&
                         !db_type_is_literal(cursor) )
                {
                    DB_ERROR("%s:%d: column `%s` declares type `%s`, which nothing "
                             "here resolves — a name in it would be read as 0\n",
                             path, line_number, column->name, cursor);
                }
                else
                {
                    column->is_string[column->type_count] =
                        strcmp(cursor, "string") == 0;
                    column->kind[column->type_count] = db_kind_for_type(cursor);
                    /* `coord` needs its own parse and resolves against no pack;
                     * remembered by kind staying COUNT plus the name, which the
                     * row reader re-derives. Storing the type name per position
                     * would be the alternative and is not worth the bytes for one
                     * special case. */
                    if( strcmp(cursor, "coord") == 0 )
                        column->kind[column->type_count] = MOCK230_PACK_COUNT;
                    column->type_count++;
                }
                if( !end )
                    break;
                cursor = end + 1;
            }

            if( column->type_count == 0 )
            {
                DB_ERROR("%s:%d: column `%s` declares no types\n", path,
                         line_number, column->name);
                continue;
            }
            table->column_count++;
        }
    }
    fclose(file);
}

/* ------------------------------------------------------------------ */
/* .dbrow                                                              */
/* ------------------------------------------------------------------ */

/** Read one `data=` value against its declared tuple position. */
static struct Mock230DbValue
row_value(
    const struct Mock230DbColumn* column,
    int position,
    const char* text,
    int* out_ok)
{
    struct Mock230DbValue out = { 0, NULL };
    const char* expanded = text;

    *out_ok = 1;
    if( *text == '^' )
    {
        expanded = mock230_content_constant(text);
        if( !expanded )
        {
            *out_ok = 0;
            return out;
        }
    }

    if( column->is_string[position] )
    {
        out.text = strdup(expanded);
        return out;
    }
    if( column->kind[position] != MOCK230_PACK_COUNT )
    {
        /* `null` is a real answer (id -1), not a miss — same rule as
         * mock230_content_symbol_checked / param=death_drop,null. */
        if( !mock230_content_symbol_checked(column->kind[position], expanded,
                                            &out.value) )
            *out_ok = 0;
        return out;
    }
    /*
     * A literal. A coord is written `0_40_52_35_23`, so try that first — atoi()
     * on one silently yields the level and every zone test then compares against
     * tile 0.
     */
    {
        int coord_ok = 0;
        int coord = db_parse_coord(expanded, &coord_ok);

        if( coord_ok )
        {
            out.value = coord;
            return out;
        }
    }
    out.value = atoi(expanded);
    return out;
}

static void
load_dbrow_file(const char* path)
{
    FILE* file = fopen(path, "rb");
    char raw[2048];
    struct Mock230DbRow* row = NULL;
    const struct Mock230DbTable* table = NULL;
    int line_number = 0;

    if( !file )
        return;
    while( fgets(raw, sizeof(raw), file) )
    {
        char* line = mock230_content_clean_line(raw);
        char* header;
        char* value;

        line_number++;
        if( !*line )
            continue;

        header = mock230_content_section_header(line);
        if( header )
        {
            g_rows = db_grow(g_rows, &g_row_capacity, g_row_count, sizeof(*g_rows));
            row = &g_rows[g_row_count++];
            memset(row, 0, sizeof(*row));
            row->symbol = strdup(header);
            row->row_id = mock230_content_symbol(MOCK230_PACK_DBROW, header);
            row->table_id = -1;
            table = NULL;
            if( row->row_id < 0 )
                DB_ERROR("%s:%d: dbrow `%s` has no id — run tools/ss_allocate.py\n",
                         path, line_number, header);
            continue;
        }

        value = mock230_content_split_key_value(line);
        if( !value || !row )
        {
            DB_ERROR("%s:%d: expected `key=value` inside a [section]\n", path,
                     line_number);
            continue;
        }

        if( strcmp(line, "table") == 0 )
        {
            table = table_by_symbol(value);
            if( !table )
            {
                DB_ERROR("%s:%d: dbrow `%s` names unknown table `%s`\n", path,
                         line_number, row->symbol, value);
                continue;
            }
            row->table_id = table->table_id;
            continue;
        }
        if( strcmp(line, "data") != 0 )
            continue;

        if( !table )
        {
            DB_ERROR("%s:%d: `data=` before `table=` in dbrow `%s`\n", path,
                     line_number, row->symbol);
            continue;
        }

        {
            char* comma = strchr(value, ',');
            const struct Mock230DbColumn* column;
            struct Mock230DbRowColumn* store;
            struct Mock230DbValue tuple[MOCK230_DB_TUPLE_MAX];
            int filled = 0;
            int index;
            int resolved = 1;
            char* cursor;

            if( !comma )
            {
                DB_ERROR("%s:%d: data needs `column,value[,value...]`\n", path,
                         line_number);
                continue;
            }
            *comma = '\0';
            index = mock230_db_column_index(table, value);
            if( index < 0 )
            {
                DB_ERROR("%s:%d: table `%s` has no column `%s`\n", path,
                         line_number, table->symbol, value);
                continue;
            }
            column = &table->columns[index];
            cursor = comma + 1;

            while( *cursor && filled < column->type_count )
            {
                char* end = strchr(cursor, ',');
                int value_ok = 0;

                /* The last declared position takes the rest of the line, commas
                 * and all. Only a trailing `string` can contain one, and the
                 * reference's grammar has no escape — so this is the only reading
                 * that does not lose text. */
                if( end && filled + 1 < column->type_count )
                    *end = '\0';
                else
                    end = NULL;

                tuple[filled] = row_value(column, filled, cursor, &value_ok);
                if( !value_ok )
                {
                    DB_ERROR("%s:%d: `%s` does not resolve\n", path, line_number,
                             cursor);
                    resolved = 0;
                }
                filled++;
                if( !end )
                    break;
                cursor = end + 1;
            }

            /*
             * Arity is a hard error. The values are stored flat and the tuple
             * count is `count / type_count`, so appending a short tuple would
             * shift every later tuple in this column by one position — a
             * coord-pair list would start pairing the end of one zone with the
             * start of the next, and nothing would report it.
             */
            if( filled != column->type_count )
            {
                DB_ERROR("%s:%d: column `%s` takes %d value(s), got %d\n", path,
                         line_number, column->name, column->type_count, filled);
                continue;
            }
            if( !resolved )
                continue;

            store = &row->columns[index];
            for( int i = 0; i < filled; i++ )
            {
                store->values = db_grow(store->values, &store->capacity,
                                        store->count, sizeof(*store->values));
                store->values[store->count++] = tuple[i];
            }
        }
    }
    fclose(file);
}

/* ------------------------------------------------------------------ */
/* Loading                                                             */
/* ------------------------------------------------------------------ */

static void
walk_suffix(
    const char* dir,
    const char* suffix,
    void (*handler)(const char*))
{
    DIR* handle = opendir(dir);
    struct dirent* entry;

    if( !handle )
        return;
    while( (entry = readdir(handle)) != NULL )
    {
        char path[1024];
        struct stat info;
        size_t name_length;
        size_t suffix_length = strlen(suffix);

        if( entry->d_name[0] == '.' )
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        if( stat(path, &info) != 0 )
            continue;
        if( S_ISDIR(info.st_mode) )
        {
            walk_suffix(path, suffix, handler);
            continue;
        }
        name_length = strlen(entry->d_name);
        if( name_length >= suffix_length &&
            strcmp(entry->d_name + name_length - suffix_length, suffix) == 0 )
        {
            /* Machine exports — see mock230_db_load. */
            if( strcmp(entry->d_name, "all.dbtable") == 0 ||
                strcmp(entry->d_name, "all.dbrow") == 0 )
                continue;
            handler(path);
        }
    }
    closedir(handle);
}

void
mock230_db_load(const char* dir)
{
    char scripts[1024];

    mock230_db_free();
    /* Server DB source has exactly one root. Client cache exports and flagged
     * client lanes use a different grammar (`columndef=` / `values=`), and the
     * binary loader below is their route into this runtime. Walking the whole
     * content tree made a feature-only client dbrow look like malformed server
     * content before its valid cache record was loaded. */
    snprintf(scripts, sizeof(scripts), "%s/server/scripts", dir);
    walk_suffix(scripts, ".dbtable", load_dbtable_file);
    walk_suffix(scripts, ".dbrow", load_dbrow_file);
}

void
mock230_db_free(void)
{
    for( int i = 0; i < g_table_count; i++ )
    {
        free((void*)g_tables[i].symbol);
        for( int col = 0; col < g_tables[i].column_count; col++ )
        {
            struct Mock230DbColumn* column = &g_tables[i].columns[col];

            for( int val = 0; val < column->default_count; val++ )
                free((void*)column->defaults[val].text);
            free(column->defaults);
            free((void*)g_tables[i].columns[col].name);
        }
    }
    free(g_tables);
    g_tables = NULL;
    g_table_count = 0;
    g_table_capacity = 0;

    for( int i = 0; i < g_row_count; i++ )
    {
        free((void*)g_rows[i].symbol);
        for( int col = 0; col < MOCK230_DB_COLUMN_MAX; col++ )
        {
            struct Mock230DbRowColumn* store = &g_rows[i].columns[col];

            for( int val = 0; val < store->count; val++ )
                free((void*)store->values[val].text);
            free(store->values);
        }
    }
    free(g_rows);
    g_rows = NULL;
    g_row_count = 0;
    g_row_capacity = 0;
}

/* ------------------------------------------------------------------ */
/* Cache-import helpers (used by mock230_dbinfo.c)                     */
/* ------------------------------------------------------------------ */

struct Mock230DbTable*
mock230_db_ensure_table(
    int table_id,
    const char* symbol,
    int replace_empty)
{
    struct Mock230DbTable* table;

    assert(table_id >= 0);
    assert(symbol);

    for( int i = 0; i < g_table_count; i++ )
    {
        if( g_tables[i].table_id != table_id )
            continue;
        table = &g_tables[i];
        if( table->column_count > 0 && !replace_empty )
            return table;
        if( replace_empty && table->column_count == 0 )
        {
            /* Stub from a prior incomplete load — clear so the caller can
             * redefine. Symbol is kept when it matches. */
            return table;
        }
        return table;
    }

    g_tables = db_grow(g_tables, &g_table_capacity, g_table_count, sizeof(*g_tables));
    table = &g_tables[g_table_count++];
    memset(table, 0, sizeof(*table));
    table->symbol = strdup(symbol);
    table->table_id = table_id;
    return table;
}

struct Mock230DbRow*
mock230_db_ensure_row(
    int row_id,
    const char* symbol,
    int table_id)
{
    struct Mock230DbRow* row;
    int has_values = 0;

    assert(row_id >= 0);
    assert(symbol);
    assert(table_id >= 0);

    for( int i = 0; i < g_row_count; i++ )
    {
        if( g_rows[i].row_id != row_id )
            continue;
        row = &g_rows[i];
        for( int col = 0; col < MOCK230_DB_COLUMN_MAX; col++ )
        {
            if( row->columns[col].count > 0 )
            {
                has_values = 1;
                break;
            }
        }
        /* Authored rows win — do not overwrite. */
        if( has_values )
            return NULL;
        row->table_id = table_id;
        return row;
    }

    g_rows = db_grow(g_rows, &g_row_capacity, g_row_count, sizeof(*g_rows));
    row = &g_rows[g_row_count++];
    memset(row, 0, sizeof(*row));
    row->symbol = strdup(symbol);
    row->row_id = row_id;
    row->table_id = table_id;
    return row;
}

void
mock230_db_column_define(
    struct Mock230DbTable* table,
    int col_id,
    const char* name,
    int type_count,
    const int* is_string)
{
    struct Mock230DbColumn* column;

    assert(table);
    assert(col_id >= 0);
    assert(col_id < MOCK230_DB_COLUMN_MAX);
    assert(type_count > 0);
    assert(type_count <= MOCK230_DB_TUPLE_MAX);
    assert(is_string);

    column = &table->columns[col_id];
    for( int i = 0; i < column->default_count; i++ )
        free((void*)column->defaults[i].text);
    free(column->defaults);
    column->defaults = NULL;
    column->default_count = 0;
    if( column->name )
        free((void*)column->name);
    column->name = name ? strdup(name) : NULL;
    column->type_count = type_count;
    for( int i = 0; i < type_count; i++ )
        column->is_string[i] = is_string[i] ? 1 : 0;
    for( int i = type_count; i < MOCK230_DB_TUPLE_MAX; i++ )
    {
        column->is_string[i] = 0;
        column->kind[i] = MOCK230_PACK_COUNT;
    }
    if( col_id + 1 > table->column_count )
        table->column_count = col_id + 1;
}

void
mock230_db_column_defaults_set(
    struct Mock230DbTable* table,
    int col_id,
    const struct Mock230DbValue* values,
    int count)
{
    struct Mock230DbColumn* column;

    assert(table);
    assert(col_id >= 0);
    assert(col_id < MOCK230_DB_COLUMN_MAX);
    assert(count >= 0);
    assert(values || count == 0);

    column = &table->columns[col_id];
    for( int i = 0; i < column->default_count; i++ )
        free((void*)column->defaults[i].text);
    free(column->defaults);
    column->defaults = count > 0 ? calloc((size_t)count, sizeof(*column->defaults)) : NULL;
    column->default_count = column->defaults ? count : 0;
    for( int i = 0; i < column->default_count; i++ )
    {
        column->defaults[i].value = values[i].value;
        column->defaults[i].text = values[i].text ? strdup(values[i].text) : NULL;
    }
}

void
mock230_db_row_column_set(
    struct Mock230DbRow* row,
    int col_id,
    const struct Mock230DbValue* values,
    int count)
{
    struct Mock230DbRowColumn* store;

    assert(row);
    assert(col_id >= 0);
    assert(col_id < MOCK230_DB_COLUMN_MAX);
    assert(count >= 0);
    assert(values || count == 0);

    store = &row->columns[col_id];
    for( int i = 0; i < store->count; i++ )
        free((void*)store->values[i].text);
    free(store->values);
    store->values = NULL;
    store->count = 0;
    store->capacity = 0;

    for( int i = 0; i < count; i++ )
    {
        struct Mock230DbValue copy = { 0, NULL };

        store->values = db_grow(store->values, &store->capacity, store->count,
                                sizeof(*store->values));
        copy.value = values[i].value;
        if( values[i].text )
            copy.text = strdup(values[i].text);
        store->values[store->count++] = copy;
    }
}

int
mock230_db_table_count(void)
{
    return g_table_count;
}

int
mock230_db_total_row_count(void)
{
    return g_row_count;
}
