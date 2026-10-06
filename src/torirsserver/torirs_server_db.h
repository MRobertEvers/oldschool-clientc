#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_DB_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_DB_H

struct RSCache_ServerPack;

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
    /** Cache tables are sparse by column id — `quest` declares column 48.
     *  Authored LostCity tables top out around 25; 64 covers both. */
    TORIRSSERVER_DB_COLUMN_MAX = 64,
    /** Measured: the widest tuple in the reference is 8. */
    TORIRSSERVER_DB_TUPLE_MAX = 8,
};

struct ToriRSServerDbValue;

/** How a literal tuple position (kind == TORIRSSERVER_PACK_COUNT, not a string)
 *  reads its text. LostCity's spellings, and cachepack's: `null` is -1 for all
 *  three; a boolean is `true`/`false` (or a number); a coord is
 *  `level_mx_mz_lx_lz` (or a number); an int is a decimal and nothing else. */
enum ToriRSServerDbLiteral
{
    TORIRSSERVER_DB_LITERAL_INT = 0,
    TORIRSSERVER_DB_LITERAL_BOOLEAN,
    TORIRSSERVER_DB_LITERAL_COORD,
};

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
    /** For a literal position, which literal (enum ToriRSServerDbLiteral).
     *  Without it `int`, `boolean` and `coord` were one bucket read by `atoi`,
     *  so `members,true` was 0 and `null` was 0. */
    unsigned char literal[TORIRSSERVER_DB_TUPLE_MAX];
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
/** Set tuple position `position` of `column` from a ScriptVarType id (a client
 *  dbtable record's type code). */
void
ToriRSServer_DbColumnTypeCode(
    struct ToriRSServerDbColumn* column,
    int position,
    int type_code);

/** Name column `index` of `table` (a no-op for a column the table does not
 *  define: an ABSENT hole). */
void
ToriRSServer_DbColumnNameSet(
    struct ToriRSServerDbTable* table,
    int index,
    const char* name);

/** Every db table and row from the server pack's client records. Returns 0, or
 *  -1 after a report when an archive does not validate. */
int
ToriRSServer_DbLoadPack(struct RSCache_ServerPack* pack);

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
 * filled from the binary records; `configs/all.dbrow` is the same records as
 * text and is not walked. `configs/all.dbtable` is read for one thing the
 * binary lacks, the column names (and type words), positionally.
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



int
ToriRSServer_DbTableCount(void);

/** The index-th loaded table, 0..ToriRSServer_DbTableCount()-1, or NULL. */
const struct ToriRSServerDbTable*
ToriRSServer_DbTableAt(int index);

int
ToriRSServer_DbTotalRowCount(void);

#endif
