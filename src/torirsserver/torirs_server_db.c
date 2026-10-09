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
 *
 * **One grammar, read the way LostCity and cachepack read it.** The authored
 * files under `server/scripts` and cachepack's rank-0 `configs/all.dbtable` are
 * the same grammar (3rd/rscache/tools/cachepack/config/cp_db.c is the reference):
 *
 *   - a column's id is its POSITION among the block's `column=` lines, and a hole
 *     in the numbering is a line of its own, `column=<name>,ABSENT`;
 *   - `null` is -1 for every int-like type, not only the reference types;
 *   - a boolean is `true` / `false`;
 *   - an int is a decimal; a coord is `level_mx_mz_lx_lz`;
 *   - a tuple's last field takes the rest of the line, commas and all, and an
 *     earlier one escapes its commas `\,`; `\\`, `\ ` (a trailing blank), `/\/`
 *     (not a comment) and `\^` (not a constant) escape what the line cleaner or
 *     the constant expansion would otherwise take;
 *   - `default=<column>,<v>...` on a table is the tuple a row that does not state
 *     the column inherits, one line per tuple.
 *
 * This reader used to take `true` and `null` in an int/boolean/coord column as
 * `atoi` does — both 0 — on 221 authored rows, with nothing reported.
 */

#include "torirs_server_db.h"
#include "rscache_valuetype.h"

#include "content/content_value.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
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
/* Bumped whenever a row is added, re-tabled or freed: the lookup indexes below
 * are rebuilt from g_rows on the first lookup after it moves. */
static int g_rows_generation = 1;

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
    /* Not `grown ? grown : array`: that kept the old array and let the caller
     * write past its end. */
    assert(grown);
    return grown;
}

/* The row's store for `col_id`, created on first write. Not db_grow: its
 * 32-slot floor on a per-row array would put most of the inline
 * columns[TORIRSSERVER_DB_COLUMN_MAX] waste straight back. */
static struct ToriRSServerDbRowColumn*
db_row_cell_ensure(
    struct ToriRSServerDbRow* row,
    int col_id)
{
    struct ToriRSServerDbRowCell* cell;

    assert(row);
    assert(col_id >= 0);
    assert(col_id < TORIRSSERVER_DB_COLUMN_MAX);

    for( int i = 0; i < row->cell_count; i++ )
    {
        if( row->cells[i].col_id == col_id )
            return &row->cells[i].store;
    }
    if( row->cell_count == row->cell_capacity )
    {
        row->cell_capacity = row->cell_capacity ? row->cell_capacity * 2 : 2;
        row->cells = realloc(
            row->cells, (size_t)row->cell_capacity * sizeof(*row->cells));
        assert(row->cells);
    }
    cell = &row->cells[row->cell_count++];
    memset(cell, 0, sizeof(*cell));
    cell->col_id = col_id;
    return &cell->store;
}

/* ------------------------------------------------------------------ */
/* Value types                                                         */
/* ------------------------------------------------------------------ */

/** The property that makes a `column=` line a hole in the column numbering. */
#define DB_ABSENT "ABSENT"

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

/** Set tuple position `position` of `column` from its type word. */
static void
db_column_type(
    struct ToriRSServerDbColumn* column,
    int position,
    const char* word)
{
    assert(column);
    assert(position >= 0);
    assert(position < TORIRSSERVER_DB_TUPLE_MAX);
    assert(word);

    /* `coord` resolves against no pack (kind COUNT) and reads its own spelling. */
    column->kind[position] = db_kind_for_type(word);
    if( strcmp(word, "boolean") == 0 )
        column->literal[position] = TORIRSSERVER_DB_LITERAL_BOOLEAN;
    else if( strcmp(word, "coord") == 0 )
        column->literal[position] = TORIRSSERVER_DB_LITERAL_COORD;
    else
        column->literal[position] = TORIRSSERVER_DB_LITERAL_INT;
}

void
ToriRSServer_DbColumnTypeCode(
    struct ToriRSServerDbColumn* column,
    int position,
    int type_code)
{
    const struct RSCache_ValueType* type = RSCache_ValueTypeOfId(type_code);

    assert(column);
    db_column_type(column, position, type ? type->word : "int");
}

void
ToriRSServer_DbColumnNameSet(
    struct ToriRSServerDbTable* table,
    int index,
    const char* name)
{
    assert(table);
    assert(name);
    if( index < 0 || index >= table->column_count || table->columns[index].type_count <= 0 )
        return;
    free((void*)table->columns[index].name);
    table->columns[index].name = strdup(name);
    assert(table->columns[index].name);
}

/* ------------------------------------------------------------------ */
/* Lookups                                                             */
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

/*
 * The lookup indexes: built from g_rows on first use after the row set moves
 * (g_rows_generation), never per query.
 *
 * Every lookup here used to be a walk of all ~30k rows, and `query_row` made
 * that a walk per CANDIDATE: `db_findnext` over a table asked
 * `ToriRSServer_DbRowInTable(table, i)` for each i, which re-walked g_rows from
 * the start to find the table's i-th row. That was half the server's time in a
 * raid scriptrun (every attack's db_find over the combat tables).
 *
 * Both indexes hold storage positions, and order ties by storage position, so
 * every answer is the one the linear walk gave: the FIRST row with an id, and a
 * table's rows in load order.
 *
 *   g_by_row_id    positions sorted (row_id, position)
 *   g_by_table     positions sorted (table_id, position); a table's rows are
 *                  one contiguous span, named by g_table_spans
 */
struct DbTableSpan
{
    int table_id;
    int first; /* into g_by_table */
    int count;
};

static int* g_by_row_id;
static int* g_by_table;
static struct DbTableSpan* g_table_spans;
static int g_table_span_count;
static int g_index_generation;

static int
db_by_row_id_cmp(
    const void* a,
    const void* b)
{
    int left = *(const int*)a;
    int right = *(const int*)b;

    if( g_rows[left].row_id != g_rows[right].row_id )
        return g_rows[left].row_id < g_rows[right].row_id ? -1 : 1;
    return left < right ? -1 : left > right ? 1 : 0;
}

static int
db_by_table_cmp(
    const void* a,
    const void* b)
{
    int left = *(const int*)a;
    int right = *(const int*)b;

    if( g_rows[left].table_id != g_rows[right].table_id )
        return g_rows[left].table_id < g_rows[right].table_id ? -1 : 1;
    return left < right ? -1 : left > right ? 1 : 0;
}

static void
db_index_free(void)
{
    free(g_by_row_id);
    free(g_by_table);
    free(g_table_spans);
    g_by_row_id = NULL;
    g_by_table = NULL;
    g_table_spans = NULL;
    g_table_span_count = 0;
    g_index_generation = 0;
}

static void
db_index_build(void)
{
    if( g_index_generation == g_rows_generation )
        return;
    db_index_free();
    g_index_generation = g_rows_generation;
    if( g_row_count <= 0 )
        return;
    g_by_row_id = malloc((size_t)g_row_count * sizeof(*g_by_row_id));
    assert(g_by_row_id);
    g_by_table = malloc((size_t)g_row_count * sizeof(*g_by_table));
    assert(g_by_table);
    g_table_spans = malloc((size_t)g_row_count * sizeof(*g_table_spans));
    assert(g_table_spans);
    for( int i = 0; i < g_row_count; i++ )
    {
        g_by_row_id[i] = i;
        g_by_table[i] = i;
    }
    qsort(g_by_row_id, (size_t)g_row_count, sizeof(*g_by_row_id), db_by_row_id_cmp);
    qsort(g_by_table, (size_t)g_row_count, sizeof(*g_by_table), db_by_table_cmp);
    for( int i = 0; i < g_row_count; i++ )
    {
        int table_id = g_rows[g_by_table[i]].table_id;

        if( g_table_span_count == 0 ||
            g_table_spans[g_table_span_count - 1].table_id != table_id )
        {
            g_table_spans[g_table_span_count].table_id = table_id;
            g_table_spans[g_table_span_count].first = i;
            g_table_spans[g_table_span_count].count = 0;
            g_table_span_count++;
        }
        g_table_spans[g_table_span_count - 1].count++;
    }
}

/** The table's span in g_by_table, or NULL when it has no rows. */
static const struct DbTableSpan*
db_table_span(int table_id)
{
    int low = 0;
    int high;

    db_index_build();
    high = g_table_span_count - 1;
    while( low <= high )
    {
        int mid = low + (high - low) / 2;

        if( g_table_spans[mid].table_id == table_id )
            return &g_table_spans[mid];
        if( g_table_spans[mid].table_id < table_id )
            low = mid + 1;
        else
            high = mid - 1;
    }
    return NULL;
}

const struct ToriRSServerDbRow*
ToriRSServer_DbRow(int row_id)
{
    int low = 0;
    int high;
    int found = -1;

    if( row_id < 0 )
        return NULL;
    db_index_build();
    high = g_row_count - 1;
    /* The lowest position holding the id: the row the linear walk met first. */
    while( low <= high )
    {
        int mid = low + (high - low) / 2;
        int mid_id = g_rows[g_by_row_id[mid]].row_id;

        if( mid_id >= row_id )
        {
            if( mid_id == row_id )
                found = mid;
            high = mid - 1;
        }
        else
            low = mid + 1;
    }
    return found < 0 ? NULL : &g_rows[g_by_row_id[found]];
}

const struct ToriRSServerDbRowColumn*
ToriRSServer_DbRowColumn(
    const struct ToriRSServerDbRow* row,
    int col_id)
{
    assert(row);
    assert(col_id >= 0);
    assert(col_id < TORIRSSERVER_DB_COLUMN_MAX);

    for( int i = 0; i < row->cell_count; i++ )
    {
        if( row->cells[i].col_id == col_id )
            return &row->cells[i].store;
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
    const struct DbTableSpan* span = db_table_span(table_id);

    return span ? span->count : 0;
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
static int g_ordered_count;     /* rows covered */
static int g_ordered_generation; /* g_rows_generation it was built from */

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
    if( g_ordered_generation == g_rows_generation )
        return;
    free(g_ordered);
    g_ordered = NULL;
    g_ordered_count = 0;
    g_ordered_generation = g_rows_generation;
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
    int low = 0;
    int high;
    int first = -1;

    if( index < 0 )
        return NULL;
    db_ordered_build();
    if( !g_ordered )
        return NULL;
    /* Sorted by (table_id, row_id), so a table's rows are contiguous: find the
     * first of them, then step `index` into the run. */
    high = g_ordered_count - 1;
    while( low <= high )
    {
        int mid = low + (high - low) / 2;
        int mid_table = g_rows[g_ordered[mid]].table_id;

        if( mid_table >= table_id )
        {
            if( mid_table == table_id )
                first = mid;
            high = mid - 1;
        }
        else
            low = mid + 1;
    }
    if( first < 0 || first + index >= g_ordered_count )
        return NULL;
    {
        const struct ToriRSServerDbRow* row = &g_rows[g_ordered[first + index]];

        return row->table_id == table_id ? row : NULL;
    }
}

const struct ToriRSServerDbRow*
ToriRSServer_DbRowInTable(
    int table_id,
    int index)
{
    const struct DbTableSpan* span;

    if( index < 0 )
        return NULL;
    span = db_table_span(table_id);
    if( !span || index >= span->count )
        return NULL;
    return &g_rows[g_by_table[span->first + index]];
}

/* ------------------------------------------------------------------ */
/* Line text: the grammar's escapes                                    */
/* ------------------------------------------------------------------ */

/*
 * The escapes are cachepack's (`db_append_field` in cp_db.c writes them, and its
 * `db_clean_value` / `db_unescape` / `db_split` read them); these are the same
 * three steps, so a line means one thing to both programs.
 */

/* ------------------------------------------------------------------ */
/* Values                                                              */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* .dbtable                                                            */
/* ------------------------------------------------------------------ */

/** A table's `default=` lines, read once the block's columns all are: a
 *  default may name a column declared below it. */
struct DbPendingDefaults
{
    char** lines;
    int* line_numbers;
    int count;
    int capacity;
};

/* ------------------------------------------------------------------ */
/* .dbrow                                                              */
/* ------------------------------------------------------------------ */
/* Loading                                                             */
void
ToriRSServer_DbFree(void)
{
    /* Rows before tables: a value is a union, and only the table's schema says
     * which positions hold a strdup'd string to free. */
    for( int i = 0; i < g_row_count; i++ )
    {
        free((void*)g_rows[i].symbol);
        for( int cell = 0; cell < g_rows[i].cell_count; cell++ )
        {
            struct ToriRSServerDbRowColumn* store = &g_rows[i].cells[cell].store;
            const struct ToriRSServerDbTable* table = ToriRSServer_DbTable(g_rows[i].table_id);
            const struct ToriRSServerDbColumn* column;

            /* A cell only exists after a write through a resolved column. */
            assert(table);
            assert(g_rows[i].cells[cell].col_id < table->column_count);
            column = &table->columns[g_rows[i].cells[cell].col_id];
            assert(column->type_count > 0);
            for( int val = 0; val < store->count; val++ )
            {
                if( column->is_string[val % column->type_count] )
                    free((void*)store->values[val].text);
            }
            free(store->values);
        }
        free(g_rows[i].cells);
    }
    free(g_rows);
    g_rows = NULL;
    g_row_count = 0;
    g_row_capacity = 0;
    g_rows_generation++;
    db_index_free();

    for( int i = 0; i < g_table_count; i++ )
    {
        free((void*)g_tables[i].symbol);
        for( int col = 0; col < g_tables[i].column_count; col++ )
        {
            struct ToriRSServerDbColumn* column = &g_tables[i].columns[col];

            for( int val = 0; val < column->default_count; val++ )
            {
                if( column->type_count > 0 &&
                    column->is_string[val % column->type_count] )
                    free((void*)column->defaults[val].text);
            }
            free(column->defaults);
            free((void*)g_tables[i].columns[col].name);
        }
    }
    free(g_tables);
    g_tables = NULL;
    g_table_count = 0;
    g_table_capacity = 0;
    free(g_ordered);
    g_ordered = NULL;
    g_ordered_count = 0;
    g_ordered_generation = 0;
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
        for( int cell = 0; cell < row->cell_count; cell++ )
        {
            if( row->cells[cell].store.count > 0 )
            {
                has_values = 1;
                break;
            }
        }
        /* Authored rows win — do not overwrite. */
        if( has_values )
            return NULL;
        row->table_id = table_id;
        g_rows_generation++;
        return row;
    }

    g_rows = db_grow(g_rows, &g_row_capacity, g_row_count, sizeof(*g_rows));
    row = &g_rows[g_row_count++];
    memset(row, 0, sizeof(*row));
    row->symbol = strdup(symbol);
    row->row_id = row_id;
    row->table_id = table_id;
    g_rows_generation++;
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
    {
        column->kind[i] = TORIRSSERVER_PACK_COUNT;
        column->literal[i] = TORIRSSERVER_DB_LITERAL_INT;
    }
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
    /* The union needs the schema to tell strings from ints — DbColumnDefine
     * must have run for this column first. */
    assert(column->type_count > 0);
    for( int i = 0; i < column->default_count; i++ )
    {
        if( column->is_string[i % column->type_count] )
            free((void*)column->defaults[i].text);
    }
    free(column->defaults);
    column->defaults = count > 0 ? calloc((size_t)count, sizeof(*column->defaults)) : NULL;
    column->default_count = column->defaults ? count : 0;
    for( int i = 0; i < column->default_count; i++ )
    {
        if( column->is_string[i % column->type_count] )
            column->defaults[i].text = values[i].text ? strdup(values[i].text) : NULL;
        else
            column->defaults[i].value = values[i].value;
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
    const struct ToriRSServerDbTable* table;
    const struct ToriRSServerDbColumn* column;

    assert(row);
    assert(col_id >= 0);
    assert(col_id < TORIRSSERVER_DB_COLUMN_MAX);
    assert(count >= 0);
    assert(values || count == 0);

    /* The union needs the row's table schema to tell strings from ints. */
    table = ToriRSServer_DbTable(row->table_id);
    assert(table);
    assert(col_id < table->column_count);
    column = &table->columns[col_id];
    assert(column->type_count > 0);

    store = db_row_cell_ensure(row, col_id);
    for( int i = 0; i < store->count; i++ )
    {
        if( column->is_string[i % column->type_count] )
            free((void*)store->values[i].text);
    }
    free(store->values);
    store->values = NULL;
    store->count = 0;
    store->capacity = 0;

    for( int i = 0; i < count; i++ )
    {
        struct ToriRSServerDbValue copy = { { 0 } };

        store->values = db_grow(store->values, &store->capacity, store->count,
                                sizeof(*store->values));
        if( column->is_string[i % column->type_count] )
        {
            if( values[i].text )
                copy.text = strdup(values[i].text);
        }
        else
        {
            copy.value = values[i].value;
        }
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
