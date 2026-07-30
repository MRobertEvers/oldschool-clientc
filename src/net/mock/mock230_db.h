#ifndef SRC_NET_MOCK_MOCK230_DB_H
#define SRC_NET_MOCK_MOCK230_DB_H

/*
 * The server's client-database tables — LostCity's `.dbtable` and `.dbrow`.
 *
 * This is the runtime half of a thing whose compiler half already existed:
 * `ssc_symbols.c` reads `.dbtable` configs to turn `combat_style_table:damagestyle`
 * into a packed column reference, and nothing could read the *rows*. That gap is
 * why `skill_prayer` ships a bespoke `.prayer` grammar and why the ported
 * thieving content flattened its drop rates into `.constant` files — both were
 * written to avoid needing this.
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
    /** Measured across the reference's 23 tables: `magic_spell_table` has 25. */
    MOCK230_DB_COLUMN_MAX = 32,
    /** Measured: the widest tuple in the reference is 8. */
    MOCK230_DB_TUPLE_MAX = 8,
};

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

#endif
