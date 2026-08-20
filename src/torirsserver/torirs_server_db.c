/*
 * `.dbtable` / `.dbrow` — the server's client-database tables.
 *
 * See torirs_server_db.h for the shape and for why these are a different population
 * from the cache's db tables. This file is the reader; torirs_server_ops_db.c is the
 * RuneScript surface over it.
 *
 * Two things here are worth knowing before editing:
 *
 * **Tables must be fully read before any row is.** A `data=` line cannot be
 * parsed without its column's declared tuple types — `data=coord_pair,A,B` is
 * two coords or one string depending on the table — so ToriRSServer_DbLoad makes two
 * passes over the tree rather than one. A single walk that happened to read
 * tables first would work until someone added a `.dbrow` that sorted earlier.
 *
 * **A `data=` line with the wrong arity is rejected, not truncated.** The values
 * are stored flat and the tuple count is derived by division, so one short line
 * would silently reinterpret every tuple after it in that column.
 */

#include "torirs_server_db.h"

#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Declared in torirs_server_content.h. Routed through there so a bad line in a
 * `.dbtable` counts the same as a bad line anywhere else and the server refuses
 * to start on it. */
#define DB_ERROR(...) ToriRSServer_ContentReportError(__VA_ARGS__)

static struct ToriRSServerDbTable* g_tables;
static int g_table_count;
static int g_table_capacity;

static struct ToriRSServerDbRow* g_rows;
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

/** The pack a declared type resolves against, or TORIRSSERVER_PACK_COUNT for a
 *  literal. Mirrors torirs_server_content.c's `.enum` type table — same question. */
static enum ToriRSServerPackKind
db_kind_for_type(const char* name)
{
    static const struct
    {
        const char* name;
        enum ToriRSServerPackKind kind;
    } k_map[] = {
        { "npc", TORIRSSERVER_PACK_NPC },           { "namedobj", TORIRSSERVER_PACK_OBJ },
        { "obj", TORIRSSERVER_PACK_OBJ },           { "loc", TORIRSSERVER_PACK_LOC },
        { "seq", TORIRSSERVER_PACK_SEQ },           { "spotanim", TORIRSSERVER_PACK_SPOTANIM },
        { "inv", TORIRSSERVER_PACK_INV },           { "varp", TORIRSSERVER_PACK_VARP },
        { "interface", TORIRSSERVER_PACK_INTERFACE },
        { "component", TORIRSSERVER_PACK_COMPONENT },
        { "stat", TORIRSSERVER_PACK_STAT },         { "param", TORIRSSERVER_PACK_PARAM },
        { "category", TORIRSSERVER_PACK_CATEGORY }, { "enum", TORIRSSERVER_PACK_ENUM },
        { "struct", TORIRSSERVER_PACK_STRUCT },     { "dbrow", TORIRSSERVER_PACK_DBROW },
        { "dbtable", TORIRSSERVER_PACK_DBTABLE },
    };

    for( size_t i = 0; i < sizeof(k_map) / sizeof(k_map[0]); i++ )
    {
        if( strcmp(name, k_map[i].name) == 0 )
            return k_map[i].kind;
    }
    return TORIRSSERVER_PACK_COUNT;
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

static struct ToriRSServerDbTable*
table_by_symbol(const char* symbol)
{
    for( int i = 0; i < g_table_count; i++ )
    {
        if( g_tables[i].symbol && strcmp(g_tables[i].symbol, symbol) == 0 )
            return &g_tables[i];
    }
    return NULL;
}

const struct ToriRSServerDbTable*
ToriRSServer_DbTable(int table_id)
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

const struct ToriRSServerDbRow*
ToriRSServer_DbRow(int row_id)
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
ToriRSServer_DbColumnIndex(
    const struct ToriRSServerDbTable* table,
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
ToriRSServer_DbRowCount(int table_id)
{
    int count = 0;

    for( int i = 0; i < g_row_count; i++ )
    {
        if( g_rows[i].table_id == table_id )
            count++;
    }
    return count;
}

/*
 * The index-th row of a table in *ascending row id*, which is the order
 * `DB_LISTALL` is defined to walk.
 *
 * Separate from `ToriRSServer_DbRowInTable` on purpose, and the separation is the
 * whole point of this function existing.
 *
 * **Why the order matters.** `DB_LISTALL` hands content a positional cursor —
 * `db_findbyindex(n)` is the n-th row of the table — and the cache states what
 * that order is. Every `dbindex/dbindex_<table>.dbi` carries a `[master]` block
 * whose own header reads "every row id in the table, which DB_FINDALL returns",
 * and every one of them is written ascending. That index is what the CLIENT
 * walks; a server that walked a different order would hand back a different row
 * than the player clicked. `transport_charter`'s map picker is exactly that
 * pairing: clientscript 8941 builds one pin per row in master order, and
 * `charter_map.rs2` turns the clicked pin's sub-id back into a row here.
 *
 * **Why it was not already true.** Rows are stored in the order
 * `configs/all.dbrow` is parsed, and the exporter happens to emit them sorted —
 * so across this cache's 144 indexed tables storage order and master order
 * agree 143 times. They disagree on table 118 `action`, where the file is short
 * one row the master block has and every position from index 1675 on is off by
 * one. "Happens to" is not a contract, and a positional API needs one.
 *
 * **Why this does not touch `db_find`.** `db_find` scans for rows matching a
 * value and `query_row` re-tests the predicate as it walks; its order is only
 * observable when several rows match, and changing it moves which row a great
 * deal of existing content sees first. Reordering the shared walk was tried and
 * measured: it fixes this, and it also shifts 38 unrelated selftest assertions
 * that depend on the current `db_find` order. That is a separate change with a
 * separate verification pass. So the ordered walk lives here, reached only from
 * the `db_query_column < 0` branch of `query_row`, and the find path keeps the
 * storage-order scan it has always had.
 *
 * The sorted view is an array of indices built once per table on first use, so
 * a full `db_listall` walk is O(n log n) rather than the O(n^2) the linear
 * filter would make of it — `action` alone is 2174 rows.
 */
static int* g_ordered;          /* indices into g_rows, sorted (table, row id) */
static int g_ordered_count;     /* rows covered; 0 when the view needs rebuilding */

static int
db_ordered_cmp(
    const void* a,
    const void* b)
{
    const struct ToriRSServerDbRow* left = &g_rows[*(const int*)a];
    const struct ToriRSServerDbRow* right = &g_rows[*(const int*)b];

    if( left->table_id != right->table_id )
        return left->table_id < right->table_id ? -1 : 1;
    if( left->row_id != right->row_id )
        return left->row_id < right->row_id ? -1 : 1;
    return 0;
}

static void
db_ordered_build(void)
{
    if( g_ordered_count == g_row_count && g_ordered )
        return;
    free(g_ordered);
    g_ordered = NULL;
    g_ordered_count = 0;
    if( g_row_count <= 0 )
        return;
    g_ordered = malloc((size_t)g_row_count * sizeof(*g_ordered));
    assert(g_ordered);
    for( int i = 0; i < g_row_count; i++ )
        g_ordered[i] = i;
    qsort(g_ordered, (size_t)g_row_count, sizeof(*g_ordered), db_ordered_cmp);
    g_ordered_count = g_row_count;
}

const struct ToriRSServerDbRow*
ToriRSServer_DbRowInTableOrdered(
    int table_id,
    int index)
{
    if( index < 0 )
        return NULL;
    db_ordered_build();
    if( !g_ordered )
        return NULL;
    /* Sorted by (table_id, row_id), so a table's rows are contiguous. */
    for( int i = 0; i < g_ordered_count; i++ )
    {
        const struct ToriRSServerDbRow* first = &g_rows[g_ordered[i]];

        if( first->table_id < table_id )
            continue;
        if( first->table_id > table_id )
            return NULL;
        if( i + index >= g_ordered_count )
            return NULL;
        {
            const struct ToriRSServerDbRow* row = &g_rows[g_ordered[i + index]];

            return row->table_id == table_id ? row : NULL;
        }
    }
    return NULL;
}

const struct ToriRSServerDbRow*
ToriRSServer_DbRowInTable(
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
    struct ToriRSServerDbTable* table = NULL;
    int line_number = 0;

    if( !file )
        return;
    while( fgets(raw, sizeof(raw), file) )
    {
        char* line = ToriRSServer_ContentCleanLine(raw);
        char* header;
        char* value;

        line_number++;
        if( !*line )
            continue;

        header = ToriRSServer_ContentSectionHeader(line);
        if( header )
        {
            g_tables = db_grow(g_tables, &g_table_capacity, g_table_count,
                               sizeof(*g_tables));
            table = &g_tables[g_table_count++];
            memset(table, 0, sizeof(*table));
            table->symbol = strdup(header);
            table->table_id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBTABLE, header);
            if( table->table_id < 0 )
                DB_ERROR("%s:%d: dbtable `%s` has no id — run tools/ss_allocate.py\n",
                         path, line_number, header);
            continue;
        }

        value = ToriRSServer_ContentSplitKeyValue(line);
        if( !value || !table )
        {
            DB_ERROR("%s:%d: expected `key=value` inside a [section]\n", path,
                     line_number);
            continue;
        }
        if( strcmp(line, "column") != 0 )
            continue;

        {
            struct ToriRSServerDbColumn* column;
            char* cursor = value;
            char* comma;

            if( table->column_count >= TORIRSSERVER_DB_COLUMN_MAX )
            {
                DB_ERROR("%s:%d: more than %d columns in one dbtable\n", path,
                         line_number, TORIRSSERVER_DB_COLUMN_MAX);
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
                else if( column->type_count >= TORIRSSERVER_DB_TUPLE_MAX )
                {
                    DB_ERROR("%s:%d: more than %d types in column `%s`\n", path,
                             line_number, TORIRSSERVER_DB_TUPLE_MAX, column->name);
                }
                else if( db_kind_for_type(cursor) == TORIRSSERVER_PACK_COUNT &&
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
                        column->kind[column->type_count] = TORIRSSERVER_PACK_COUNT;
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
static struct ToriRSServerDbValue
row_value(
    const struct ToriRSServerDbColumn* column,
    int position,
    const char* text,
    int* out_ok)
{
    struct ToriRSServerDbValue out = { 0, NULL };
    const char* expanded = text;

    *out_ok = 1;
    if( *text == '^' )
    {
        expanded = ToriRSServer_ContentConstant(text);
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
    if( column->kind[position] != TORIRSSERVER_PACK_COUNT )
    {
        /* `null` is a real answer (id -1), not a miss — same rule as
         * ToriRSServer_ContentSymbolChecked / param=death_drop,null. */
        if( !ToriRSServer_ContentSymbolChecked(column->kind[position], expanded,
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
    struct ToriRSServerDbRow* row = NULL;
    const struct ToriRSServerDbTable* table = NULL;
    int line_number = 0;

    if( !file )
        return;
    while( fgets(raw, sizeof(raw), file) )
    {
        char* line = ToriRSServer_ContentCleanLine(raw);
        char* header;
        char* value;

        line_number++;
        if( !*line )
            continue;

        header = ToriRSServer_ContentSectionHeader(line);
        if( header )
        {
            g_rows = db_grow(g_rows, &g_row_capacity, g_row_count, sizeof(*g_rows));
            row = &g_rows[g_row_count++];
            memset(row, 0, sizeof(*row));
            row->symbol = strdup(header);
            row->row_id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, header);
            row->table_id = -1;
            table = NULL;
            if( row->row_id < 0 )
                DB_ERROR("%s:%d: dbrow `%s` has no id — run tools/ss_allocate.py\n",
                         path, line_number, header);
            continue;
        }

        value = ToriRSServer_ContentSplitKeyValue(line);
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
            const struct ToriRSServerDbColumn* column;
            struct ToriRSServerDbRowColumn* store;
            struct ToriRSServerDbValue tuple[TORIRSSERVER_DB_TUPLE_MAX];
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
            index = ToriRSServer_DbColumnIndex(table, value);
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
            /* Machine exports — see ToriRSServer_DbLoad. */
            if( strcmp(entry->d_name, "all.dbtable") == 0 ||
                strcmp(entry->d_name, "all.dbrow") == 0 )
                continue;
            handler(path);
        }
    }
    closedir(handle);
}

/*
 * Put the cache's own column NAMES on the cache's own tables.
 *
 * A dat2 DBTABLE record carries column types and defaults and no names at all —
 * the names live in `configs/all.dbtable`, the unpacked text, which is where
 * `sscompile` reads them to compile `poh_hotspot:builddata` into a column id.
 * The runtime had no such reader, so every cache table arrived with
 * `column->name == NULL` and an authored `.dbrow` extending one could not name
 * its column: 82 `table has no column` lines for the Construction workbench's
 * four flatpack category rows, and a workbench that offers nothing.
 *
 * Names only. Types, arity and defaults stay the binary's, and a table the tree
 * defines itself is untouched — this runs before the authored `.dbtable` walk
 * and only fills a column that already exists and is still unnamed.
 */
static void
name_cache_table_columns(const char* content_dir)
{
    char path[1024];
    FILE* file;
    char raw[1024];
    struct ToriRSServerDbTable* table = NULL;

    snprintf(path, sizeof(path), "%s/configs/all.dbtable", content_dir);
    file = fopen(path, "rb");
    if( !file )
        return;
    while( fgets(raw, sizeof(raw), file) )
    {
        char* line = ToriRSServer_ContentCleanLine(raw);
        char* value;
        char* comma;
        int col_id;
        int table_id;

        if( !*line )
            continue;
        {
            char* header = ToriRSServer_ContentSectionHeader(line);

            if( header )
            {
                table = NULL;
                table_id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBTABLE, header);
                if( table_id >= 0 )
                {
                    for( int i = 0; i < g_table_count; i++ )
                    {
                        if( g_tables[i].table_id == table_id )
                        {
                            table = &g_tables[i];
                            break;
                        }
                    }
                }
                continue;
            }
        }
        value = ToriRSServer_ContentSplitKeyValue(line);
        if( !value || !table || strcmp(line, "columndef") != 0 )
            continue;
        /* `columndef=<id>:<name>,<type>[,<type>...]` */
        col_id = atoi(value);
        value = strchr(value, ':');
        if( !value )
            continue;
        value++;
        comma = strchr(value, ',');
        if( comma )
            *comma = '\0';
        if( col_id < 0 || col_id >= TORIRSSERVER_DB_COLUMN_MAX )
            continue;
        if( table->columns[col_id].type_count <= 0 || table->columns[col_id].name )
            continue;
        table->columns[col_id].name = strdup(value);
        assert(table->columns[col_id].name);
        if( col_id + 1 > table->column_count )
            table->column_count = col_id + 1;
        /* The type words the binary could not carry either. Positions must line
         * up with the arity the record already declared; a disagreement is the
         * text and the binary describing different tables, so say nothing rather
         * than name half a tuple's namespaces wrongly. */
        if( comma )
        {
            struct ToriRSServerDbColumn* column = &table->columns[col_id];
            char* cursor = comma + 1;
            int position = 0;

            while( *cursor && position < column->type_count )
            {
                char* end = strchr(cursor, ',');

                if( end )
                    *end = '\0';
                column->kind[position] = db_kind_for_type(cursor);
                if( strcmp(cursor, "coord") == 0 )
                    column->kind[position] = TORIRSSERVER_PACK_COUNT;
                position++;
                if( !end )
                    break;
                cursor = end + 1;
            }
        }
    }
    fclose(file);
}

void
ToriRSServer_DbLoad(const char* dir)
{
    char scripts[1024];

    /*
     * Deliberately NOT `ToriRSServer_DbFree()` first.
     *
     * The cache's DBTABLE schemas are installed *before* this call (see
     * torirs_server_boot.c step 3), because an authored `.dbrow` may name a cache
     * table — `poh_hotspot` — and cannot resolve one that is not loaded. A free
     * here threw those 246 schemas away again and the 82 flatpack rows went on
     * reporting `names unknown table`. Callers that reload rather than boot
     * call `ToriRSServer_DbFree` themselves.
     */
    /* Server DB source has exactly one root. Client cache exports and flagged
     * client lanes use a different grammar (`columndef=` / `values=`), and the
     * binary loader below is their route into this runtime. Walking the whole
     * content tree made a feature-only client dbrow look like malformed server
     * content before its valid cache record was loaded. */
    name_cache_table_columns(dir);
    snprintf(scripts, sizeof(scripts), "%s/server/scripts", dir);
    walk_suffix(scripts, ".dbtable", load_dbtable_file);
    walk_suffix(scripts, ".dbrow", load_dbrow_file);
}

void
ToriRSServer_DbFree(void)
{
    for( int i = 0; i < g_table_count; i++ )
    {
        free((void*)g_tables[i].symbol);
        for( int col = 0; col < g_tables[i].column_count; col++ )
        {
            struct ToriRSServerDbColumn* column = &g_tables[i].columns[col];

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
        for( int col = 0; col < TORIRSSERVER_DB_COLUMN_MAX; col++ )
        {
            struct ToriRSServerDbRowColumn* store = &g_rows[i].columns[col];

            for( int val = 0; val < store->count; val++ )
                free((void*)store->values[val].text);
            free(store->values);
        }
    }
    free(g_rows);
    g_rows = NULL;
    g_row_count = 0;
    g_row_capacity = 0;
    free(g_ordered);
    g_ordered = NULL;
    g_ordered_count = 0;
}

/* ------------------------------------------------------------------ */
/* Cache-import helpers (used by torirs_server_dbinfo.c)                     */
/* ------------------------------------------------------------------ */

struct ToriRSServerDbTable*
ToriRSServer_DbEnsureTable(
    int table_id,
    const char* symbol,
    int replace_empty)
{
    struct ToriRSServerDbTable* table;

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

struct ToriRSServerDbRow*
ToriRSServer_DbEnsureRow(
    int row_id,
    const char* symbol,
    int table_id)
{
    struct ToriRSServerDbRow* row;
    int has_values = 0;

    assert(row_id >= 0);
    assert(symbol);
    assert(table_id >= 0);

    for( int i = 0; i < g_row_count; i++ )
    {
        if( g_rows[i].row_id != row_id )
            continue;
        row = &g_rows[i];
        for( int col = 0; col < TORIRSSERVER_DB_COLUMN_MAX; col++ )
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
ToriRSServer_DbColumnDefine(
    struct ToriRSServerDbTable* table,
    int col_id,
    const char* name,
    int type_count,
    const int* is_string)
{
    struct ToriRSServerDbColumn* column;

    assert(table);
    assert(col_id >= 0);
    assert(col_id < TORIRSSERVER_DB_COLUMN_MAX);
    assert(type_count > 0);
    assert(type_count <= TORIRSSERVER_DB_TUPLE_MAX);
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
    /*
     * Every position's kind, not just the ones past `type_count`.
     *
     * `TORIRSSERVER_PACK_NPC` is 0, so a column left at the calloc'd value claims to
     * hold npc names — and the cache import calls this with types and no kinds
     * at all, which made every dat2 column an npc column. `poh_hotspot:builddata`
     * holds dbrow ids; resolving `poh_armchair_1` against the npc pack answered
     * "does not resolve", which is the polite version of the failure. The kind
     * is set afterwards by whoever knows it (see name_cache_table_columns and
     * the `.dbtable` walk), and COUNT — an int literal — is the only safe thing
     * to say until then.
     */
    for( int i = 0; i < TORIRSSERVER_DB_TUPLE_MAX; i++ )
        column->kind[i] = TORIRSSERVER_PACK_COUNT;
    for( int i = type_count; i < TORIRSSERVER_DB_TUPLE_MAX; i++ )
        column->is_string[i] = 0;
    if( col_id + 1 > table->column_count )
        table->column_count = col_id + 1;
}

void
ToriRSServer_DbColumnDefaultsSet(
    struct ToriRSServerDbTable* table,
    int col_id,
    const struct ToriRSServerDbValue* values,
    int count)
{
    struct ToriRSServerDbColumn* column;

    assert(table);
    assert(col_id >= 0);
    assert(col_id < TORIRSSERVER_DB_COLUMN_MAX);
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
ToriRSServer_DbRowColumnSet(
    struct ToriRSServerDbRow* row,
    int col_id,
    const struct ToriRSServerDbValue* values,
    int count)
{
    struct ToriRSServerDbRowColumn* store;

    assert(row);
    assert(col_id >= 0);
    assert(col_id < TORIRSSERVER_DB_COLUMN_MAX);
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
        struct ToriRSServerDbValue copy = { 0, NULL };

        store->values = db_grow(store->values, &store->capacity, store->count,
                                sizeof(*store->values));
        copy.value = values[i].value;
        if( values[i].text )
            copy.text = strdup(values[i].text);
        store->values[store->count++] = copy;
    }
}

int
ToriRSServer_DbTableCount(void)
{
    return g_table_count;
}

/** The index-th loaded table, for a caller that means to walk all of them.
 *  `ToriRSServer_DbTable` takes an id and is the lookup; this is the enumeration. */
const struct ToriRSServerDbTable*
ToriRSServer_DbTableAt(int index)
{
    if( index < 0 || index >= g_table_count )
        return NULL;
    return &g_tables[index];
}

int
ToriRSServer_DbTotalRowCount(void)
{
    return g_row_count;
}
