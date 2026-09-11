#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_DB_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_DB_H

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

#include "torirs_server_content.h"

enum
{
    /** Cache tables are sparse by column id — `quest` declares columndef 48.
     *  Authored LostCity tables top out around 25; 64 covers both. */
    TORIRSSERVER_DB_COLUMN_MAX = 64,
    /** Measured: the widest tuple in the reference is 8. */
    TORIRSSERVER_DB_TUPLE_MAX = 8,
};

struct ToriRSServerDbValue;

struct ToriRSServerDbColumn
{
    const char* name;
    /** The tuple width — how many values one `data=` line carries. */
    int type_count;
    /** Per tuple position: text rather than a number. Decides which VM stack
     *  `db_getfield` pushes onto, so it is not cosmetic. */
    int is_string[TORIRSSERVER_DB_TUPLE_MAX];
    /** The pack a tuple position resolves against, or TORIRSSERVER_PACK_COUNT for a
     *  literal (`int`, `coord`, `string`). */
    enum ToriRSServerPackKind kind[TORIRSSERVER_DB_TUPLE_MAX];
    /** DBTABLE's optional value block. A DBROW which omits this column inherits
     *  these tuples; DB_FIND and DB_GETFIELD both observe that inheritance. */
    struct ToriRSServerDbValue* defaults;
    int default_count;
};

struct ToriRSServerDbTable
{
    const char* symbol;
    /** From pack/dbtable.pack, or -1 when the name was never allocated an id.
     *  A table with no id is unreachable from script and is reported at load. */
    int table_id;
    struct ToriRSServerDbColumn columns[TORIRSSERVER_DB_COLUMN_MAX];
    int column_count;
};

/** One value in a tuple. Which member is live is not recorded here — the
 *  column's `is_string[position]` says, with position = index % type_count in
 *  any flat values array. It used to be a pair (text non-NULL marking the
 *  strings), but the tag merely restated the schema at a cost of 4 bytes per
 *  value across the cache's ~2.4M imported values — and letting `text` answer
 *  "is this a string" invited exactly the bug where a value of 0 and an absent
 *  string were indistinguishable. */
struct ToriRSServerDbValue
{
    union
    {
        int value;
        /** May still be NULL on a string position: `null` spelled out. */
        const char* text;
    };
};

struct ToriRSServerDbRowColumn
{
    /** Flat, tuple-major: `count` is a value count, so the tuple count is
     *  `count / column->type_count`. Keeping it flat is what makes a partially
     *  written row impossible to mistake for a complete one — a `data=` line
     *  with the wrong arity is rejected rather than half-appended. */
    struct ToriRSServerDbValue* values;
    int count;
    int capacity;
};

/** One column a row actually states, keyed by the table's sparse column id. */
struct ToriRSServerDbRowCell
{
    int col_id;
    struct ToriRSServerDbRowColumn store;
};

struct ToriRSServerDbRow
{
    const char* symbol;
    int row_id;
    int table_id;
    /** Sparse, in first-write order. A typical row states 1-3 of the 64
     *  addressable columns, so the inline
     *  `columns[TORIRSSERVER_DB_COLUMN_MAX]` array this replaces was ~97%
     *  zeroes — ~21MB across the cache's ~25k rows. Read through
     *  `ToriRSServer_DbRowColumn`, which answers NULL for a column the row
     *  does not state — the same "fall back to the table's defaults" signal
     *  a zero count used to carry. */
    struct ToriRSServerDbRowCell* cells;
    int cell_count;
    int cell_capacity;
};

/** Read every `*.dbtable` then every `*.dbrow` under `dir`, recursively.
 *
 *  Order matters and is enforced by doing both passes here rather than leaving it
 *  to a directory walk: a row names its table, and a `data=` line cannot be
 *  parsed at all until the table has told us the column's tuple types. */
void
ToriRSServer_DbLoad(const char* dir);

void
ToriRSServer_DbFree(void);

/** By the id `pack/dbtable.pack` gives its name, or NULL. */
const struct ToriRSServerDbTable*
ToriRSServer_DbTable(int table_id);

/** By the id `pack/dbrow.pack` gives its name, or NULL. */
const struct ToriRSServerDbRow*
ToriRSServer_DbRow(int row_id);

/** The values `row` states at `col_id`, or NULL when it states none — the
 *  caller then falls back to the column's defaults, exactly as it did when
 *  every row carried an (empty) store for every column. */
const struct ToriRSServerDbRowColumn*
ToriRSServer_DbRowColumn(
    const struct ToriRSServerDbRow* row,
    int col_id);

/** Column index within its table, or -1. */
int
ToriRSServer_DbColumnIndex(
    const struct ToriRSServerDbTable* table,
    const char* name);

/** How many rows belong to a table — `db_listall`'s answer. */
int
ToriRSServer_DbRowCount(int table_id);

/** The `index`-th row of a table in load order, or NULL. */
const struct ToriRSServerDbRow*
ToriRSServer_DbRowInTable(
    int table_id,
    int index);

/** The index-th row of `table_id` in ascending row id — the order the cache's
 *  own `dbindex/dbindex_<table>.dbi` `[master]` block states DB_FINDALL
 *  returns, and therefore the order the client walks. Used only by the
 *  `db_listall` cursor; `db_find` keeps the storage-order scan above. See the
 *  long comment on the definition for why the two are separate. */
const struct ToriRSServerDbRow*
ToriRSServer_DbRowInTableOrdered(
    int table_id,
    int index);

/*
 * Cache import (torirs_server_dbinfo.c). Authored tables keep priority: a table that
 * already has columns is not overwritten. Rows for cache table ids are always
 * filled from the binary records — the machine-exported `configs/all.dbrow`
 * uses `values=` which the text reader does not parse.
 */

/** Ensure a table slot for `table_id`. Creates one when absent. When
 *  `replace_empty` is set and the existing table has zero columns, its schema
 *  is cleared so the caller can redefine it. */
struct ToriRSServerDbTable*
ToriRSServer_DbEnsureTable(
    int table_id,
    const char* symbol,
    int replace_empty);

/** Ensure a row slot. Creates one when absent; never replaces an authored row
 *  that already has values. */
struct ToriRSServerDbRow*
ToriRSServer_DbEnsureRow(
    int row_id,
    const char* symbol,
    int table_id);

/** Define column `col_id` (sparse id, not densified). `name` may be NULL for
 *  cache-only columns that scripts address by packed id. */
void
ToriRSServer_DbColumnDefine(
    struct ToriRSServerDbTable* table,
    int col_id,
    const char* name,
    int type_count,
    const int* is_string);

/** Replace a cache DBTABLE column's default tuples. String texts are strdup'd. */
void
ToriRSServer_DbColumnDefaultsSet(
    struct ToriRSServerDbTable* table,
    int col_id,
    const struct ToriRSServerDbValue* values,
    int count);

/** Replace the values stored at `col_id` on a row. Takes ownership of nothing —
 *  string texts are strdup'd. */
void
ToriRSServer_DbRowColumnSet(
    struct ToriRSServerDbRow* row,
    int col_id,
    const struct ToriRSServerDbValue* values,
    int count);

/** Install every DBTABLE schema the dat2 cache ships. Call this BEFORE
 *  `ToriRSServer_DbLoad`: an authored `.dbrow` may name a cache table by symbol,
 *  and a table it cannot resolve costs one error per `data=` line after it.
 *  Returns 1 on success (including "cache missing" with a diagnostic), 0
 *  never — boot continues either way, matching objinfo. */
int
ToriRSServer_DbLoadCacheTables(const char* cache_dir);

/** Fill in the cache's own DBROW values, AFTER `ToriRSServer_DbLoad`, so a row the
 *  tree states wins over the cache's copy of the same id. */
int
ToriRSServer_DbLoadCacheRows(const char* cache_dir);

int
ToriRSServer_DbTableCount(void);

/** The index-th loaded table, 0..ToriRSServer_DbTableCount()-1, or NULL. */
const struct ToriRSServerDbTable*
ToriRSServer_DbTableAt(int index);

int
ToriRSServer_DbTotalRowCount(void);

#endif
