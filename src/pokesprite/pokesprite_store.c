#include "pokesprite_store.h"

#include "pokesprite_format.h"

#include <assert.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct PokeSpriteStore
{
    char root[512];
    sqlite3* index;
    sqlite3* missing;
    sqlite3_stmt* lookup;
    sqlite3_stmt* record_miss;
};

static void
must(
    sqlite3* db,
    int rc,
    const char* what)
{
    if( rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW )
    {
        fprintf(stderr, "pokesprite store: %s: %s\n", what, sqlite3_errmsg(db));
        abort();
    }
}

struct PokeSpriteStore*
PokeSpriteStore_Open(const char* root)
{
    assert(root);
    char path[640];
    struct stat st;
    snprintf(path, sizeof(path), "%s/index.db", root);
    if( stat(path, &st) != 0 )
        return NULL;

    struct PokeSpriteStore* s = (struct PokeSpriteStore*)calloc(1, sizeof(*s));
    assert(s);
    snprintf(s->root, sizeof(s->root), "%s", root);

    /* Read-write although this never writes: the generator keeps the index in
     * WAL mode, and a read-only connection cannot open a WAL database whose
     * -shm file is gone (a clean close, a fresh clone) -- it has to create it. */
    must(s->index, sqlite3_open_v2(path, &s->index, SQLITE_OPEN_READWRITE, NULL), "open index.db");
    /* The offline generator may be writing the index while this serves: a
     * lock held for a moment is a wait, not a failure. */
    sqlite3_busy_timeout(s->index, 10000);
    must(s->index,
         sqlite3_prepare_v2(s->index, "SELECT path FROM sheet WHERE key = ?1", -1, &s->lookup, NULL),
         "prepare lookup");

    snprintf(path, sizeof(path), "%s/missing.db", root);
    must(s->missing,
         sqlite3_open_v2(path, &s->missing, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL),
         "open missing.db");
    sqlite3_busy_timeout(s->missing, 10000);
    must(s->missing,
         sqlite3_exec(s->missing,
                      "CREATE TABLE IF NOT EXISTS missing (key TEXT PRIMARY KEY, first_seen TEXT, "
                      "last_seen TEXT, count INTEGER)",
                      NULL, NULL, NULL),
         "create missing");
    must(s->missing,
         sqlite3_prepare_v2(s->missing,
                            "INSERT INTO missing (key, first_seen, last_seen, count) "
                            "VALUES (?1, datetime('now'), datetime('now'), 1) "
                            "ON CONFLICT(key) DO UPDATE SET last_seen = datetime('now'), "
                            "count = count + 1",
                            -1, &s->record_miss, NULL),
         "prepare record_miss");
    return s;
}

void
PokeSpriteStore_Close(struct PokeSpriteStore* s)
{
    if( !s )
        return;
    sqlite3_finalize(s->lookup);
    sqlite3_finalize(s->record_miss);
    sqlite3_close(s->index);
    sqlite3_close(s->missing);
    free(s);
}

static void
record_miss(
    struct PokeSpriteStore* s,
    const char* key)
{
    sqlite3_reset(s->record_miss);
    must(s->missing, sqlite3_bind_text(s->record_miss, 1, key, -1, SQLITE_TRANSIENT), "bind miss");
    must(s->missing, sqlite3_step(s->record_miss), "record miss");
}

uint8_t*
PokeSpriteStore_Fetch(
    struct PokeSpriteStore* s,
    const char* key,
    int* out_size)
{
    assert(s);
    assert(key);
    assert(out_size);
    assert(PokeSprite_KeyValid(key));
    *out_size = 0;

    sqlite3_reset(s->lookup);
    must(s->index, sqlite3_bind_text(s->lookup, 1, key, -1, SQLITE_TRANSIENT), "bind lookup");
    int rc = sqlite3_step(s->lookup);
    must(s->index, rc, "lookup");
    if( rc != SQLITE_ROW )
    {
        record_miss(s, key);
        return NULL;
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", s->root, (const char*)sqlite3_column_text(s->lookup, 0));

    /*
     * An index row whose file is gone is a broken store, not a miss: the
     * generator wrote one without the other, and recording it as "missing"
     * would only re-render it while hiding why it vanished. Say so, loudly.
     */
    FILE* f = fopen(path, "rb");
    if( !f )
    {
        fprintf(stderr, "pokesprite store: index names %s for '%s' but it cannot be opened\n",
                path, key);
        abort();
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* bytes = (uint8_t*)malloc(n > 0 ? (size_t)n : 1u);
    assert(bytes);
    size_t got = fread(bytes, 1, (size_t)n, f);
    fclose(f);
    if( got != (size_t)n )
    {
        fprintf(stderr, "pokesprite store: short read of %s\n", path);
        abort();
    }
    *out_size = (int)n;
    return bytes;
}

static long
count(
    sqlite3* db,
    const char* sql)
{
    sqlite3_stmt* st;
    long n = 0;
    must(db, sqlite3_prepare_v2(db, sql, -1, &st, NULL), "prepare count");
    if( sqlite3_step(st) == SQLITE_ROW )
        n = (long)sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return n;
}

void
PokeSpriteStore_Counts(
    struct PokeSpriteStore* s,
    long* out_sheets,
    long* out_missing)
{
    assert(s);
    assert(out_sheets);
    assert(out_missing);
    *out_sheets = count(s->index, "SELECT COUNT(*) FROM sheet");
    *out_missing = count(s->missing, "SELECT COUNT(*) FROM missing");
}
