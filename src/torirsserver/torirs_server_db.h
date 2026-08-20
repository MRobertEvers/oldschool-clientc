#ifndef SRC_NET_MOCK_MOCK230_DB_H
#define SRC_NET_MOCK_MOCK230_DB_H

/*
 * The server's client-database tables — LostCity's `.dbtable` and `.dbrow`.
 *
 * This is the runtime half of a thing whose compiler half already existed:
 * `ssc_symbols.c` reads `.dbtable` configs to turn `combat_style_table:damagestyle`
 * into a packed column reference, and nothing could read the *rows*. That gap is
 * why `skill_prayer` shipped a bespoke `.prayer` grammar the engine parsed in C,
 * and why the ported thieving content flattened its drop rates into `.constant`
 * files — both were written to avoid needing this. The prayer one is gone:
 * `skill_prayer/configs/prayers.dbtable` is an ordinary table now and no C reads
 * it.
 *
 * Not to be confused with the *cache's* db tables, which `configs/all.dbtable`
 * holds and rev 230's CS2 reads through its own DB_* opcodes (docs/cs2vm.md).
 * Those belong to the client. These belong to the server, and their ids are
 * allocated above the cache's high-water mark so the two populations cannot
 * collide — see pack/dbtable.pack.
 *
 * The shape, which is not obvious from the file format:
 *
 *   [coord_pair_table]                       a TABLE declares columns
 *   column=coord_pair,coord,coord,LIST       one column, a TUPLE of two coords
 *
 *   [sheepherder_in_pen]                     a ROW belongs to one table
 *   table=coord_pair_table
 *   data=coord_pair,0_40_52_35_23,0_40_52_49_36    appends ONE tuple
 *
 * So a column is a list of tuples, not a list of values, and the two lengths are
 * different numbers: `db_getfieldcount` returns the *tuple* count, while
 * `db_getfield` pushes one value per type in the tuple — which is what lets
 * `$coord1, $coord2 = db_getfield(...)` receive two.
 */

#include "mock230_content.h"

enum
{
    /** Cache tables are sparse by column id — `quest` declares columndef 48.
     *  Authored LostCity tables top out around 25; 64 covers both. */
    MOCK230_DB_COLUMN_MAX = 64,
    /** Measured: the widest tuple in the reference is 8. */
    MOCK230_DB_TUPLE_MAX = 8,
};

struct Mock230DbValue;

struct Mock230DbColumn
{
    const char* name;
    /** The tuple width — how many values one `data=` line carries. */
    int type_count;
    /** Per tuple position: text rather than a number. Decides which VM stack
     *  `db_getfield` pushes onto, so it is not cosmetic. */
    int is_string[MOCK230_DB_TUPLE_MAX];
    /** The pack a tuple position resolves against, or MOCK230_PACK_COUNT for a
     *  literal (`int`, `coord`, `string`). */
    enum Mock230PackKind kind[MOCK230_DB_TUPLE_MAX];
    /** DBTABLE's optional value block. A DBROW which omits this column inherits
     *  these tuples; DB_FIND and DB_GETFIELD both observe that inheritance. */
    struct Mock230DbValue* defaults;
    int default_count;
};

struct Mock230DbTable
{
    const char* symbol;
    /** From pack/dbtable.pack, or -1 when the name was never allocated an id.
     *  A table with no id is unreachable from script and is reported at load. */
    int table_id;
    struct Mock230DbColumn columns[MOCK230_DB_COLUMN_MAX];
    int column_count;
};

struct Mock230DbValue
{
    int value;
    /** Non-NULL only where the column's type at this position is `string`. */
    const char* text;
};

struct Mock230DbRowColumn
{
    /** Flat, tuple-major: `count` is a value count, so the tuple count is
     *  `count / column->type_count`. Keeping it flat is what makes a partially
     *  written row impossible to mistake for a complete one — a `data=` line
     *  with the wrong arity is rejected rather than half-appended. */
    struct Mock230DbValue* values;
    int count;
    int capacity;
};

struct Mock230DbRow
{
    const char* symbol;
    int row_id;
    int table_id;
    struct Mock230DbRowColumn columns[MOCK230_DB_COLUMN_MAX];
};

/** Read every `*.dbtable` then every `*.dbrow` under `dir`, recursively.
 *
 *  Order matters and is enforced by doing both passes here rather than leaving it
 *  to a directory walk: a row names its table, and a `data=` line cannot be
 *  parsed at all until the table has told us the column's tuple types. */
void
mock230_db_load(const char* dir);

void
mock230_db_free(void);

/** By the id `pack/dbtable.pack` gives its name, or NULL. */
const struct Mock230DbTable*
mock230_db_table(int table_id);

/** By the id `pack/dbrow.pack` gives its name, or NULL. */
const struct Mock230DbRow*
mock230_db_row(int row_id);

/** Column index within its table, or -1. */
int
mock230_db_column_index(
    const struct Mock230DbTable* table,
    const char* name);

/** How many rows belong to a table — `db_listall`'s answer. */
int
mock230_db_row_count(int table_id);

/** The `index`-th row of a table in load order, or NULL. */
const struct Mock230DbRow*
mock230_db_row_in_table(
    int table_id,
    int index);

/** The index-th row of `table_id` in ascending row id — the order the cache's
 *  own `dbindex/dbindex_<table>.dbi` `[master]` block states DB_FINDALL
 *  returns, and therefore the order the client walks. Used only by the
 *  `db_listall` cursor; `db_find` keeps the storage-order scan above. See the
 *  long comment on the definition for why the two are separate. */
const struct Mock230DbRow*
mock230_db_row_in_table_ordered(
    int table_id,
    int index);

/*
 * Cache import (mock230_dbinfo.c). Authored tables keep priority: a table that
 * already has columns is not overwritten. Rows for cache table ids are always
 * filled from the binary records — the machine-exported `configs/all.dbrow`
 * uses `values=` which the text reader does not parse.
 */

/** Ensure a table slot for `table_id`. Creates one when absent. When
 *  `replace_empty` is set and the existing table has zero columns, its schema
 *  is cleared so the caller can redefine it. */
struct Mock230DbTable*
mock230_db_ensure_table(
    int table_id,
    const char* symbol,
    int replace_empty);

/** Ensure a row slot. Creates one when absent; never replaces an authored row
 *  that already has values. */
struct Mock230DbRow*
mock230_db_ensure_row(
    int row_id,
    const char* symbol,
    int table_id);

/** Define column `col_id` (sparse id, not densified). `name` may be NULL for
 *  cache-only columns that scripts address by packed id. */
void
mock230_db_column_define(
    struct Mock230DbTable* table,
    int col_id,
    const char* name,
    int type_count,
    const int* is_string);

/** Replace a cache DBTABLE column's default tuples. String texts are strdup'd. */
void
mock230_db_column_defaults_set(
    struct Mock230DbTable* table,
    int col_id,
    const struct Mock230DbValue* values,
    int count);

/** Replace the values stored at `col_id` on a row. Takes ownership of nothing —
 *  string texts are strdup'd. */
void
mock230_db_row_column_set(
    struct Mock230DbRow* row,
    int col_id,
    const struct Mock230DbValue* values,
    int count);

/** Install every DBTABLE schema the dat2 cache ships. Call this BEFORE
 *  `mock230_db_load`: an authored `.dbrow` may name a cache table by symbol,
 *  and a table it cannot resolve costs one error per `data=` line after it.
 *  Returns 1 on success (including "cache missing" with a diagnostic), 0
 *  never — boot continues either way, matching objinfo. */
int
mock230_db_load_cache_tables(const char* cache_dir);

/** Fill in the cache's own DBROW values, AFTER `mock230_db_load`, so a row the
 *  tree states wins over the cache's copy of the same id. */
int
mock230_db_load_cache_rows(const char* cache_dir);

int
mock230_db_table_count(void);

/** The index-th loaded table, 0..mock230_db_table_count()-1, or NULL. */
const struct Mock230DbTable*
mock230_db_table_at(int index);

int
mock230_db_total_row_count(void);

#endif
