/*
 * The lease table. See player_lease.h.
 */
#include "player_lease.h"

#include "mem/torirs_mem.h"
#include "torirsserver/torirs_server_auth.h"
#include "torirsserver/torirs_server_savefile.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* The journal is rewritten once it holds this many lines per lease, plus the
 * floor, so a quiet table is not rewritten on every login. */
#define LEASE_JOURNAL_PER_ROW 4
#define LEASE_JOURNAL_FLOOR 256

static struct PlayerLease*
lease_find(
    struct PlayerLeases const* table,
    char const* key)
{
    for( int i = 0; i < table->count; i++ )
        if( strcmp(table->rows[i].key, key) == 0 )
            return &table->rows[i];
    return NULL;
}

static struct PlayerLease*
lease_add(
    struct PlayerLeases* table,
    char const* key)
{
    struct PlayerLease* row;

    if( table->count == table->cap )
    {
        table->cap = table->cap ? table->cap * 2 : 256;
        table->rows = ToriMem_Realloc(ToriMem_Net(), table->rows,
                                      (size_t)table->cap * sizeof(*table->rows));
        assert(table->rows);
    }
    row = &table->rows[table->count++];
    memset(row, 0, sizeof(*row));
    snprintf(row->key, sizeof(row->key), "%s", key);
    return row;
}

static void
lease_remove(
    struct PlayerLeases* table,
    struct PlayerLease* row)
{
    int index = (int)(row - table->rows);

    assert(index >= 0);
    assert(index < table->count);
    table->rows[index] = table->rows[table->count - 1];
    table->count--;
}

/* ------------------------------------------------------------------ */
/* The journal                                                         */
/* ------------------------------------------------------------------ */

static void
journal_open_append(struct PlayerLeases* table)
{
    table->journal = fopen(table->journal_path, "ab");
    if( !table->journal )
    {
        fprintf(stderr, "player_server: cannot append to %s; a lease it grants would be "
                        "forgotten at the next restart\n",
                table->journal_path);
        abort();
    }
}

static void
journal_compact(struct PlayerLeases* table)
{
    size_t cap = 64 + (size_t)table->count * (PLAYER_KEY_MAX + PLAYER_WORLD_MAX + 48);
    char* text = ToriMem_Alloc(ToriMem_Net(), cap);
    size_t len = 0;

    assert(text);
    text[0] = '\0';
    for( int i = 0; i < table->count; i++ )
    {
        struct PlayerLease const* row = &table->rows[i];

        len += (size_t)snprintf(text + len, cap - len, "A\t%s\t%s\t%s\t%s\n", row->key,
                                row->world, row->boot, row->token);
        assert(len < cap);
    }
    if( table->journal )
        fclose(table->journal);
    table->journal = NULL;
    if( !ToriRSServer_SaveWriteText(table->journal_path, text) )
    {
        fprintf(stderr, "player_server: cannot rewrite %s\n", table->journal_path);
        abort();
    }
    ToriMem_Free(ToriMem_Net(), text);
    table->journal_lines = table->count;
    journal_open_append(table);
}

/* One change of hands, on disk before the answer goes back. */
static void
journal_line(
    struct PlayerLeases* table,
    char const* line)
{
    if( !table->journal_path[0] )
        return;
    assert(table->journal);
    if( fputs(line, table->journal) < 0 || fflush(table->journal) != 0 )
    {
        fprintf(stderr, "player_server: cannot write %s\n", table->journal_path);
        abort();
    }
    table->journal_lines++;
    if( table->journal_lines > table->count * LEASE_JOURNAL_PER_ROW + LEASE_JOURNAL_FLOOR )
        journal_compact(table);
}

static void
journal_set(
    struct PlayerLeases* table,
    struct PlayerLease const* row)
{
    char line[PLAYER_KEY_MAX + PLAYER_WORLD_MAX + PLAYER_BOOT_HEX + PLAYER_LEASE_TOKEN_HEX + 16];

    snprintf(line, sizeof(line), "A\t%s\t%s\t%s\t%s\n", row->key, row->world, row->boot,
             row->token);
    journal_line(table, line);
}

static void
journal_release(
    struct PlayerLeases* table,
    char const* key)
{
    char line[PLAYER_KEY_MAX + 8];

    snprintf(line, sizeof(line), "R\t%s\n", key);
    journal_line(table, line);
}

/* Apply one journal line to the table (opening's replay). A line that does
 * not parse is a torn last write: skipped. */
static void
journal_apply(
    struct PlayerLeases* table,
    char* line)
{
    char* f[5];
    int n = PlayerProto_Fields(line, f, 5);

    if( n == 5 && strcmp(f[0], "A") == 0 && f[1][0] && f[2][0] &&
        strlen(f[4]) == PLAYER_LEASE_TOKEN_HEX )
    {
        struct PlayerLease* row = lease_find(table, f[1]);

        if( !row )
            row = lease_add(table, f[1]);
        snprintf(row->world, sizeof(row->world), "%s", f[2]);
        snprintf(row->boot, sizeof(row->boot), "%s", f[3]);
        snprintf(row->token, sizeof(row->token), "%s", f[4]);
    }
    else if( n == 2 && strcmp(f[0], "R") == 0 )
    {
        struct PlayerLease* row = lease_find(table, f[1]);

        if( row )
            lease_remove(table, row);
    }
}

static void
journal_replay(struct PlayerLeases* table)
{
    FILE* file = fopen(table->journal_path, "rb");
    char line[512];

    /* No journal: a server that has never granted a lease. */
    if( !file )
        return;
    while( fgets(line, sizeof(line), file) )
    {
        /* A line with no newline is the torn end of a write that never
         * finished: not a change anyone was told about. */
        if( !strchr(line, '\n') )
            break;
        line[strcspn(line, "\r\n")] = '\0';
        journal_apply(table, line);
    }
    fclose(file);
}

/* ------------------------------------------------------------------ */
/* The table                                                           */
/* ------------------------------------------------------------------ */

void
PlayerLeases_Open(
    struct PlayerLeases* table,
    char const* journal_path)
{
    assert(table);
    assert(journal_path);
    memset(table, 0, sizeof(*table));
    snprintf(table->journal_path, sizeof(table->journal_path), "%s", journal_path);
    if( !table->journal_path[0] )
        return;
    journal_replay(table);
    journal_compact(table);
}

void
PlayerLeases_Close(struct PlayerLeases* table)
{
    if( !table )
        return;
    if( table->journal )
        fclose(table->journal);
    ToriMem_Free(ToriMem_Net(), table->rows);
    memset(table, 0, sizeof(*table));
}

int
PlayerLeases_Acquire(
    struct PlayerLeases* table,
    char const* key,
    char const* world,
    char const* boot,
    char out_token[PLAYER_LEASE_TOKEN_HEX + 1],
    int* out_was_holder,
    char* out_holder,
    size_t holder_cap)
{
    struct PlayerLease* row;
    uint8_t bytes[PLAYER_LEASE_TOKEN_HEX / 2];

    assert(table);
    assert(key);
    assert(key[0]);
    assert(world);
    assert(world[0]);
    assert(boot);
    assert(out_token);
    assert(out_was_holder);
    assert(out_holder);
    assert(holder_cap > 0);
    out_token[0] = '\0';
    out_holder[0] = '\0';
    *out_was_holder = 0;
    row = lease_find(table, key);
    if( row && strcmp(row->world, world) != 0 )
    {
        snprintf(out_holder, holder_cap, "%s", row->world);
        return 0;
    }
    *out_was_holder = row != NULL;
    if( !row )
        row = lease_add(table, key);
    snprintf(row->world, sizeof(row->world), "%s", world);
    snprintf(row->boot, sizeof(row->boot), "%s", boot[0] ? boot : "-");
    ToriRSServer_AuthRandom(bytes, sizeof(bytes));
    ToriRSServer_AuthHex(bytes, sizeof(bytes), row->token);
    journal_set(table, row);
    snprintf(out_token, PLAYER_LEASE_TOKEN_HEX + 1, "%s", row->token);
    return 1;
}

int
PlayerLeases_Holds(
    struct PlayerLeases const* table,
    char const* key,
    char const* world,
    char const* token)
{
    struct PlayerLease const* row;

    assert(table);
    assert(key);
    assert(world);
    assert(token);
    row = lease_find(table, key);
    return row && strcmp(row->world, world) == 0 && strcmp(row->token, token) == 0;
}

int
PlayerLeases_Release(
    struct PlayerLeases* table,
    char const* key,
    char const* world,
    char const* token)
{
    struct PlayerLease* row;

    assert(table);
    assert(key);
    assert(world);
    assert(token);
    if( !PlayerLeases_Holds(table, key, world, token) )
        return 0;
    row = lease_find(table, key);
    lease_remove(table, row);
    journal_release(table, key);
    return 1;
}

int
PlayerLeases_Revoke(
    struct PlayerLeases* table,
    char const* key,
    char* out_world,
    size_t world_cap)
{
    struct PlayerLease* row;

    assert(table);
    assert(key);
    assert(out_world);
    assert(world_cap > 0);
    out_world[0] = '\0';
    row = lease_find(table, key);
    if( !row )
        return 0;
    snprintf(out_world, world_cap, "%s", row->world);
    lease_remove(table, row);
    journal_release(table, key);
    return 1;
}

static int
key_kept(
    char const* key,
    char const (*keep)[PLAYER_KEY_MAX],
    int keep_count)
{
    for( int i = 0; i < keep_count; i++ )
        if( strcmp(keep[i], key) == 0 )
            return 1;
    return 0;
}

int
PlayerLeases_DropWorld(
    struct PlayerLeases* table,
    char const* world,
    char const* boot,
    char const (*keep)[PLAYER_KEY_MAX],
    int keep_count,
    void (*dropped)(char const* key, void* ctx),
    void* ctx)
{
    int count = 0;

    assert(table);
    assert(world);
    assert(world[0]);
    assert(boot);
    assert(keep_count >= 0);
    for( int i = table->count - 1; i >= 0; i-- )
    {
        char key[PLAYER_KEY_MAX];

        if( strcmp(table->rows[i].world, world) != 0 )
            continue;
        /* Granted to this run: the hello came late, after its logins. */
        if( boot[0] && strcmp(table->rows[i].boot, boot) == 0 )
            continue;
        if( keep_count > 0 && key_kept(table->rows[i].key, keep, keep_count) )
            continue;
        snprintf(key, sizeof(key), "%s", table->rows[i].key);
        lease_remove(table, &table->rows[i]);
        journal_release(table, key);
        if( dropped )
            dropped(key, ctx);
        count++;
    }
    return count;
}

struct PlayerLease const*
PlayerLeases_Find(
    struct PlayerLeases const* table,
    char const* key)
{
    assert(table);
    assert(key);
    return lease_find(table, key);
}
