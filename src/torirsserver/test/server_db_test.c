/*
 * The server's dbrow store, its lookups against the linear walk they replaced.
 *
 * `ToriRSServer_DbRow`, `DbRowInTable`, `DbRowCount` and `DbRowInTableOrdered`
 * answer from indexes built on first use after the row set changes. The answer
 * each must give is the one a walk of the rows in load order gives -- the first
 * row with an id, a table's rows in load order, ascending row id for the
 * ordered view -- so this keeps its own load-order record and asks every
 * question both ways: before and after rows are added past a lookup, after a
 * row moves table, and across a free and reload.
 */

#include "torirs_server_db.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* torirs_server_db.c reports bad config lines through the content loader; the
 * store's lookups never do. */
void
ToriRSServer_ContentReportError(
    const char* format,
    ...)
{
    va_list args;

    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

static int g_failures;

static void
check(
    int condition,
    const char* what)
{
    if( !condition )
    {
        fprintf(stderr, "FAIL: %s\n", what);
        g_failures++;
    }
}

#define MAX_ROWS 512

/* The load-order record: what a linear walk of the store would see. */
static int g_ids[MAX_ROWS];
static int g_tables[MAX_ROWS];
static int g_count;

static void
add_row(
    int row_id,
    int table_id)
{
    for( int i = 0; i < g_count; i++ )
    {
        if( g_ids[i] == row_id )
        {
            /* An existing row with no values moves table and keeps its place. */
            check(ToriRSServer_DbEnsureRow(row_id, "moved", table_id) != NULL,
                  "an empty existing row can be re-tabled");
            g_tables[i] = table_id;
            return;
        }
    }
    check(g_count < MAX_ROWS, "fixture fits");
    check(ToriRSServer_DbEnsureRow(row_id, "row", table_id) != NULL, "row is created");
    g_ids[g_count] = row_id;
    g_tables[g_count] = table_id;
    g_count++;
}

/* The index-th row of a table in load order, by walking the record. */
static int
walk_in_table(
    int table_id,
    int index)
{
    int seen = 0;

    for( int i = 0; i < g_count; i++ )
    {
        if( g_tables[i] != table_id )
            continue;
        if( seen == index )
            return g_ids[i];
        seen++;
    }
    return -1;
}

static int
walk_count(int table_id)
{
    int count = 0;

    for( int i = 0; i < g_count; i++ )
        count += g_tables[i] == table_id;
    return count;
}

/* The index-th row of a table in ascending row id. */
static int
walk_ordered(
    int table_id,
    int index)
{
    int ids[MAX_ROWS];
    int n = 0;

    for( int i = 0; i < g_count; i++ )
    {
        if( g_tables[i] == table_id )
            ids[n++] = g_ids[i];
    }
    for( int i = 1; i < n; i++ )
    {
        int v = ids[i];
        int j = i - 1;

        while( j >= 0 && ids[j] > v )
        {
            ids[j + 1] = ids[j];
            j--;
        }
        ids[j + 1] = v;
    }
    return index < n ? ids[index] : -1;
}

static int
id_of(const struct ToriRSServerDbRow* row)
{
    return row ? row->row_id : -1;
}

/* Every lookup, against the walk, for tables -1..max_table and ids -1..max_id. */
static void
check_all(
    const char* stage,
    int max_table,
    int max_id)
{
    char what[160];

    check(ToriRSServer_DbTotalRowCount() == g_count, "total row count");
    for( int table = -1; table <= max_table; table++ )
    {
        int count = walk_count(table);

        snprintf(what, sizeof(what), "%s: table %d row count", stage, table);
        check(ToriRSServer_DbRowCount(table) == count, what);
        for( int index = -2; index <= count + 1; index++ )
        {
            int want = index < 0 ? -1 : walk_in_table(table, index);
            int want_ordered = index < 0 ? -1 : walk_ordered(table, index);

            snprintf(what, sizeof(what), "%s: table %d load-order row %d", stage, table, index);
            check(id_of(ToriRSServer_DbRowInTable(table, index)) == want, what);
            snprintf(what, sizeof(what), "%s: table %d ascending row %d", stage, table, index);
            check(id_of(ToriRSServer_DbRowInTableOrdered(table, index)) == want_ordered, what);
        }
    }
    for( int id = -1; id <= max_id; id++ )
    {
        const struct ToriRSServerDbRow* row = ToriRSServer_DbRow(id);
        int table = -1;

        for( int i = 0; i < g_count; i++ )
        {
            if( g_ids[i] == id )
                table = g_tables[i];
        }
        snprintf(what, sizeof(what), "%s: row %d found exactly when loaded", stage, id);
        check((row != NULL) == (table >= 0), what);
        snprintf(what, sizeof(what), "%s: row %d answers its own id and table", stage, id);
        check(!row || (row->row_id == id && row->table_id == table), what);
    }
}

int
main(void)
{
    unsigned seed = 12345u;

    /* Five tables, row ids out of order and the tables interleaved, so load
     * order, id order and table grouping all disagree. Table 4 gets no rows. */
    for( int t = 0; t < 5; t++ )
        ToriRSServer_DbEnsureTable(t, "table", 0);
    for( int i = 0; i < 200; i++ )
    {
        seed = seed * 1103515245u + 12345u;
        add_row((int)((seed >> 8) % 400), (int)((seed >> 20) % 4));
    }
    check_all("loaded", 5, 400);

    /* Rows added after the indexes were built must be seen. Fresh ids only:
     * a repeat takes the re-table path, which would mask a missed rebuild. */
    for( int i = 0; i < 40; i++ )
        add_row(400 + ((i * 37) % 60), i % 5);
    check_all("grown", 5, 460);

    /* A row that moves table keeps its load position in the new one. */
    add_row(g_ids[3], 4);
    add_row(g_ids[10], 0);
    check_all("re-tabled", 5, 460);

    /* Free and reload a smaller store: nothing of the old one survives. */
    ToriRSServer_DbFree();
    g_count = 0;
    check_all("freed", 5, 460);
    ToriRSServer_DbEnsureTable(7, "table", 0);
    add_row(9, 7);
    add_row(2, 7);
    add_row(5, 7);
    check_all("reloaded", 8, 460);
    ToriRSServer_DbFree();

    if( g_failures )
    {
        fprintf(stderr, "server_db_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("server_db_test: ok\n");
    return 0;
}
