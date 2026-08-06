/*
 * The DB_* host commands — RuneScript's view of the server's db tables.
 *
 * First of the per-domain opcode files that docs/osrs230_mockserver.md §6.1
 * step 2 left undone. `mock230_script_command` in mock230_scripts.c offers each
 * domain the opcode in turn and each returns 1 when it handled it, which is the
 * same contract the VM already uses for the host callback as a whole — so the
 * split composes rather than needing a dispatch table of its own.
 *
 * ------------------------------------------------------------------
 * The packed column reference
 * ------------------------------------------------------------------
 *
 * `combat_style_table:damagestyle` compiles to one int:
 *
 *     (table << 12) | (column << 4) | tuple_index
 *
 * `ssc_symbols.c` emits it and `DbOps.ts` unpacks it, and until now nothing in
 * this repo had read the second half — docs/serverscript.md recorded the packing
 * as *unverified*. It is verified now, against the reference's own unpack:
 * table is `>> 12 & 0xffff`, column `>> 4 & 0x7f`, tuple index `& 0xf`.
 *
 * The tuple index is **1-based, with 0 meaning "the whole tuple"**. That is the
 * one part no naming makes obvious: `db_getfield(row, tbl:col, i)` normally
 * pushes every value in tuple `i`, but a reference written `tbl:col(2)` pushes
 * only the second. Reading 0 as "element zero" would push one value where content
 * expects two, and everything the script reads afterwards comes off the wrong
 * stack — the failure is a wrong *shape*, not a wrong number, so it surfaces far
 * from here.
 *
 * ------------------------------------------------------------------
 * The query iterator
 * ------------------------------------------------------------------
 *
 * `db_find` selects rows and `db_findnext` walks them, so there is per-script
 * iteration state. It lives on the *player* rather than on the VM state, matching
 * where this server already keeps `last_int` and the interaction — and unlike the
 * reference, which hangs it on ScriptState. The difference matters for one reason:
 * a script that suspends mid-walk (a dialogue between two rows) keeps its query
 * here, whereas a per-state copy would need the whole query preserved across the
 * park. Both are defensible; this one costs less.
 */

#include "mock230.h"
#include "mock230_db.h"

#include "ss_opcode.h"
#include "ssvm.h"

#include <stdio.h>

/** table id, column index and tuple index out of a packed column reference. */
static void
unpack_column(
    int32_t packed,
    int* out_table,
    int* out_column,
    int* out_tuple)
{
    *out_table = (packed >> 12) & 0xffff;
    *out_column = (packed >> 4) & 0x7f;
    /* 1-based on the wire, so -1 here means "the whole tuple". */
    *out_tuple = (packed & 0xf) - 1;
}

/** The table and column a packed reference names, aborting the script when
 *  either is absent. Returns NULL having aborted. */
static const struct Mock230DbColumn*
resolve_column(
    struct SSVM_State* state,
    int32_t packed,
    const struct Mock230DbTable** out_table,
    int* out_index,
    int* out_tuple)
{
    int table_id;
    int column_index;
    const struct Mock230DbTable* table;

    unpack_column(packed, &table_id, &column_index, out_tuple);
    table = mock230_db_table(table_id);
    if( !table )
    {
        SSVM_Abort(state, "db table %d is not defined by any .dbtable config", table_id);
        return NULL;
    }
    if( column_index < 0 || column_index >= table->column_count )
    {
        SSVM_Abort(state, "db table `%s` has no column %d (it has %d)", table->symbol,
                   column_index, table->column_count);
        return NULL;
    }
    if( table->columns[column_index].type_count <= 0 )
    {
        SSVM_Abort(state, "db table `%s` column %d is not defined", table->symbol,
                   column_index);
        return NULL;
    }
    *out_table = table;
    *out_index = column_index;
    return &table->columns[column_index];
}

/** Does this row's `column` hold `value` at any tuple position? */
static int
row_holds(
    const struct Mock230DbRow* row,
    const struct Mock230DbColumn* definition,
    int column,
    int tuple_position,
    int value)
{
    const struct Mock230DbRowColumn* store = &row->columns[column];
    const struct Mock230DbValue* values = store->values;
    int count = store->count;

    if( count == 0 )
    {
        values = definition->defaults;
        count = definition->default_count;
    }

    for( int i = 0; i < count; i++ )
    {
        if( tuple_position >= 0 && definition->type_count > 0 &&
            i % definition->type_count != tuple_position )
            continue;
        if( values[i].value == value )
            return 1;
    }
    return 0;
}

/*
 * The `index`-th row the current query selects, or NULL past the end.
 *
 * One function for both queries: `db_listall` is a query with no predicate
 * (column -1) and `db_find` is the same walk with one. Keeping them together is
 * what stops `db_findnext` needing to know which one selected it — the cursor is
 * an index into the *matches*, not into the table, in both cases.
 */
static const struct Mock230DbRow*
query_row(
    const struct Mock230Player* player,
    int index)
{
    int seen = 0;
    const struct Mock230DbTable* table;

    if( index < 0 )
        return NULL;
    if( player->db_query_column < 0 )
        return mock230_db_row_in_table(player->db_query_table, index);
    table = mock230_db_table(player->db_query_table);
    if( !table || player->db_query_column >= table->column_count )
        return NULL;

    for( int i = 0;; i++ )
    {
        const struct Mock230DbRow* row = mock230_db_row_in_table(player->db_query_table, i);

        if( !row )
            return NULL;
        if( !row_holds(row, &table->columns[player->db_query_column],
                       player->db_query_column, player->db_query_tuple,
                       player->db_query_value) )
            continue;
        if( seen == index )
            return row;
        seen++;
    }
}

int
mock230_ops_db(
    struct SSVM_State* state,
    int opcode,
    int dot)
{
    struct Mock230Server* srv = (struct Mock230Server*)state->env->host.user;
    struct Mock230Player* player = srv->active_player;

    (void)dot;

    switch( opcode )
    {
    /*
     * `db_getfield(row, table:column, listindex)` — one push per type in the
     * tuple, which is what lets `$a, $b = db_getfield(...)` receive two values.
     *
     * A row belonging to a *different* table than the reference names is not an
     * error in the reference: it yields the column's defaults. Here it yields
     * zeros with a diagnostic under verbose, because this tree has no `default=`
     * on a dbtable column yet and silently substituting one would be inventing
     * the value rather than reading it.
     */
    case SS_OP_DB_GETFIELD:
    {
        int32_t values[3];
        const struct Mock230DbTable* table = NULL;
        const struct Mock230DbColumn* column;
        const struct Mock230DbRow* row;
        const struct Mock230DbRowColumn* store;
        int column_index = 0;
        int tuple_index = 0;
        int first;
        int last;

        for( int i = 2; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        column = resolve_column(state, values[1], &table, &column_index, &tuple_index);
        if( !column )
            return 1;
        row = mock230_db_row(values[0]);
        if( !row )
        {
            SSVM_Abort(state, "db row %d is not defined by any .dbrow config", values[0]);
            return 1;
        }

        first = 0;
        last = column->type_count;
        if( tuple_index >= 0 )
        {
            if( tuple_index >= column->type_count )
            {
                SSVM_Abort(state, "tuple index %d out of range for `%s:%s` (width %d)",
                           tuple_index, table->symbol, column->name,
                           column->type_count);
                return 1;
            }
            first = tuple_index;
            last = tuple_index + 1;
        }

        store = &row->columns[column_index];
        for( int i = first; i < last; i++ )
        {
            int offset = (values[2] * column->type_count) + i;
            const struct Mock230DbValue* source = store->values;
            int source_count = store->count;

            if( source_count == 0 )
            {
                source = column->defaults;
                source_count = column->default_count;
            }

            if( row->table_id != table->table_id || offset < 0 || offset >= source_count )
            {
                /* Past the end, or a row from another table. Push the type's zero
                 * so the stack shape still matches what the script declared —
                 * getting the *shape* wrong is far worse than getting the value
                 * wrong, because it corrupts everything read afterwards. */
                if( column->is_string[i] )
                    SSVM_PushStr(state, "");
                else
                    SSVM_PushInt(state, 0);
                continue;
            }
            if( column->is_string[i] )
                SSVM_PushStr(state, source[offset].text
                                        ? source[offset].text
                                        : "");
            else
                SSVM_PushInt(state, source[offset].value);
        }
        return 1;
    }

    /*
     * How many *tuples* the column holds for this row.
     *
     * `count` is a value count, so this is a division. Returning the value count
     * would make every `while ($i < db_getfieldcount(...))` loop run twice as
     * long as it should and read past the end — which the getfield above would
     * then answer with zeros rather than a diagnostic.
     */
    case SS_OP_DB_GETFIELDCOUNT:
    {
        int32_t values[2];
        const struct Mock230DbTable* table = NULL;
        const struct Mock230DbColumn* column;
        const struct Mock230DbRow* row;
        int column_index = 0;
        int tuple_index = 0;

        for( int i = 1; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        column = resolve_column(state, values[1], &table, &column_index, &tuple_index);
        if( !column )
            return 1;
        row = mock230_db_row(values[0]);
        if( !row )
        {
            SSVM_Abort(state, "db row %d is not defined by any .dbrow config", values[0]);
            return 1;
        }
        if( row->table_id != table->table_id )
        {
            SSVM_PushInt(state, 0);
            return 1;
        }
        {
            int count = row->columns[column_index].count;
            if( count == 0 )
                count = column->default_count;
            SSVM_PushInt(state, count / column->type_count);
        }
        return 1;
    }

    /** Which table a row belongs to. */
    case SS_OP_DB_GETROWTABLE:
    {
        int32_t row_id;
        const struct Mock230DbRow* row;

        if( !SSVM_PopInt(state, &row_id) )
            return 1;
        row = mock230_db_row(row_id);
        if( !row )
        {
            SSVM_Abort(state, "db row %d is not defined by any .dbrow config", row_id);
            return 1;
        }
        SSVM_PushInt(state, row->table_id);
        return 1;
    }

    /*
     * `db_listall(table)` — select every row of a table, then walk with
     * db_findnext. The `_with_count` form also pushes how many it found.
     *
     * The reference keeps the matched ids in a list; this keeps the query and
     * re-tests it as the walk goes, which is the same rows in the same order
     * without the allocation. See `query_row` above.
     */
    case SS_OP_DB_LISTALL:
    case SS_OP_DB_LISTALL_WITH_COUNT:
    {
        int32_t table_id;

        if( !SSVM_PopInt(state, &table_id) )
            return 1;
        if( !mock230_db_table(table_id) )
        {
            SSVM_Abort(state, "db_listall on table %d, which is not defined", table_id);
            return 1;
        }
        player->db_query_table = table_id;
        player->db_query_index = -1;
        player->db_query_column = -1;
        player->db_query_tuple = -1;
        if( opcode == SS_OP_DB_LISTALL_WITH_COUNT )
            SSVM_PushInt(state, mock230_db_row_count(table_id));
        return 1;
    }

    /*
     * `db_find(table:column, value)` — the rows whose column holds `value`,
     * then walk with db_findnext. The `_with_count` form also pushes how many
     * matched.
     *
     * This is the query the reference means by an INDEXED column, and the index
     * is the part that is not ported: 29 rows are scanned rather than hashed.
     * The behaviour is what content sees, and content sees the same rows in the
     * same order either way.
     *
     * A tuple index in the reference (`tbl:col(2)`) narrows which position of
     * the tuple is compared; without one, any position matching is a match,
     * which is what makes `db_find` on a LIST column mean "contains".
     */
    case SS_OP_DB_FIND:
    case SS_OP_DB_FIND_WITH_COUNT:
    {
        int32_t packed;
        int32_t value;
        const struct Mock230DbTable* table = NULL;
        const struct Mock230DbColumn* column;
        int column_index = 0;
        int tuple_index = 0;

        if( !SSVM_PopInt(state, &value) )
            return 1;
        if( !SSVM_PopInt(state, &packed) )
            return 1;
        column = resolve_column(state, packed, &table, &column_index, &tuple_index);
        if( !column )
            return 1;
        player->db_query_table = table->table_id;
        player->db_query_index = -1;
        player->db_query_column = column_index;
        player->db_query_tuple = tuple_index;
        player->db_query_value = value;
        if( opcode == SS_OP_DB_FIND_WITH_COUNT )
        {
            int matched = 0;

            while( query_row(player, matched) )
                matched++;
            SSVM_PushInt(state, matched);
        }
        return 1;
    }

    /** The next row of the current query, or -1 when exhausted. */
    case SS_OP_DB_FINDNEXT:
    {
        const struct Mock230DbRow* row;

        if( player->db_query_table < 0 )
        {
            SSVM_Abort(state, "db_findnext with no query selected");
            return 1;
        }
        row = query_row(player, player->db_query_index + 1);
        if( !row )
        {
            SSVM_PushInt(state, -1);
            return 1;
        }
        player->db_query_index++;
        SSVM_PushInt(state, row->row_id);
        return 1;
    }

    /** The `index`-th row of the current query without moving the cursor. */
    case SS_OP_DB_FINDBYINDEX:
    {
        int32_t index;
        const struct Mock230DbRow* row;

        if( !SSVM_PopInt(state, &index) )
            return 1;
        if( player->db_query_table < 0 )
        {
            SSVM_Abort(state, "db_findbyindex with no query selected");
            return 1;
        }
        row = query_row(player, index);
        SSVM_PushInt(state, row ? row->row_id : -1);
        return 1;
    }

    default:
        return 0;
    }
}
