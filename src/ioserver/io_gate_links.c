/*
 * The web logins' links database. See io_gate_links.h.
 */
#include "io_gate_links.h"

#include "mem/torirs_mem.h"
#include "torirsserver/torirs_server_savefile.h"

#include <sqlite3.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define IO_GATE_LINKS_STALE_MAX 16

struct IoGateLinks
{
    sqlite3* db;
    sqlite3_stmt* put;
    sqlite3_stmt* token;
    sqlite3_stmt* list;
    sqlite3_stmt* drop;
};

static void
must(
    sqlite3* db,
    int rc,
    char const* what)
{
    if( rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW )
    {
        fprintf(stderr, "io_server: gate links: %s: %s\n", what, sqlite3_errmsg(db));
        abort();
    }
}

struct IoGateLinks*
IoGateLinks_Open(char const* path)
{
    struct IoGateLinks* links;

    assert(path);
    links = ToriMem_Calloc(ToriMem_Net(), 1, sizeof(*links));
    assert(links);
    ToriRSServer_SaveMkdirParent(path);
    must(links->db,
         sqlite3_open_v2(path, &links->db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL),
         "open");
#ifndef _WIN32
    /* The tokens are bearer secrets: owner-only, like the key files. */
    chmod(path, 0600);
#endif
    sqlite3_busy_timeout(links->db, 5000);
    must(links->db,
         sqlite3_exec(links->db,
                      "CREATE TABLE IF NOT EXISTS links ("
                      " player TEXT PRIMARY KEY,"
                      " user TEXT NOT NULL,"
                      " display TEXT NOT NULL,"
                      " token TEXT NOT NULL,"
                      " linked_at INTEGER NOT NULL);"
                      "CREATE INDEX IF NOT EXISTS links_user ON links(user);",
                      NULL, NULL, NULL),
         "create");
    must(links->db,
         sqlite3_prepare_v2(links->db,
                            "INSERT OR REPLACE INTO links (player, user, display, token, linked_at)"
                            " VALUES (?1, ?2, ?3, ?4, ?5)",
                            -1, &links->put, NULL),
         "prepare put");
    must(links->db,
         sqlite3_prepare_v2(links->db, "SELECT token FROM links WHERE player = ?1 AND user = ?2",
                            -1, &links->token, NULL),
         "prepare token");
    must(links->db,
         sqlite3_prepare_v2(links->db,
                            "SELECT display, token, player FROM links WHERE user = ?1"
                            " ORDER BY linked_at, player",
                            -1, &links->list, NULL),
         "prepare list");
    must(links->db,
         sqlite3_prepare_v2(links->db, "DELETE FROM links WHERE player = ?1 AND user = ?2", -1,
                            &links->drop, NULL),
         "prepare drop");
    return links;
}

void
IoGateLinks_Put(
    struct IoGateLinks* links,
    char const* user,
    char const* display_name,
    char const* token,
    long long now)
{
    char player[64];

    assert(links);
    assert(user);
    assert(display_name);
    assert(token);
    ToriRSServer_SaveFileName(display_name, player, sizeof(player));
    assert(player[0]);
    sqlite3_bind_text(links->put, 1, player, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(links->put, 2, user, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(links->put, 3, display_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(links->put, 4, token, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(links->put, 5, now);
    must(links->db, sqlite3_step(links->put), "put");
    sqlite3_reset(links->put);
    sqlite3_clear_bindings(links->put);
}

int
IoGateLinks_Token(
    struct IoGateLinks* links,
    char const* user,
    char const* display_name,
    char out_token[TORIRSSERVER_LINK_TOKEN_HEX + 1])
{
    char player[64];
    int found = 0;
    int rc;

    assert(links);
    assert(user);
    assert(display_name);
    assert(out_token);
    out_token[0] = '\0';
    ToriRSServer_SaveFileName(display_name, player, sizeof(player));
    if( !player[0] )
        return 0;
    sqlite3_bind_text(links->token, 1, player, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(links->token, 2, user, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(links->token);
    must(links->db, rc, "token");
    if( rc == SQLITE_ROW )
    {
        char const* token = (char const*)sqlite3_column_text(links->token, 0);

        if( token && strlen(token) == TORIRSSERVER_LINK_TOKEN_HEX )
        {
            memcpy(out_token, token, TORIRSSERVER_LINK_TOKEN_HEX + 1);
            found = 1;
        }
    }
    sqlite3_reset(links->token);
    sqlite3_clear_bindings(links->token);
    return found;
}

int
IoGateLinks_List(
    struct IoGateLinks* links,
    char const* user,
    char out_names[][IO_GATE_LINKS_NAME_MAX],
    int max)
{
    char stale[IO_GATE_LINKS_STALE_MAX][64];
    int stale_count = 0;
    int count = 0;
    int rc;

    assert(links);
    assert(user);
    assert(out_names);
    sqlite3_bind_text(links->list, 1, user, -1, SQLITE_TRANSIENT);
    while( (rc = sqlite3_step(links->list)) == SQLITE_ROW )
    {
        char const* display = (char const*)sqlite3_column_text(links->list, 0);
        char const* token = (char const*)sqlite3_column_text(links->list, 1);
        char const* player = (char const*)sqlite3_column_text(links->list, 2);

        if( !display || !token || !player )
            continue;
        /* The record decides: an unlinked or re-linked account's row is a
         * button that would only fail. */
        if( !ToriRSServer_AuthLinkVerify(display, user, token) )
        {
            if( stale_count < IO_GATE_LINKS_STALE_MAX )
                snprintf(stale[stale_count++], sizeof(stale[0]), "%s", player);
            continue;
        }
        if( count < max )
            snprintf(out_names[count++], IO_GATE_LINKS_NAME_MAX, "%s", display);
    }
    must(links->db, rc, "list");
    sqlite3_reset(links->list);
    sqlite3_clear_bindings(links->list);

    for( int i = 0; i < stale_count; i++ )
    {
        fprintf(stderr, "io_server: gate links: '%s' is no longer linked to '%s'; row dropped\n",
                stale[i], user);
        sqlite3_bind_text(links->drop, 1, stale[i], -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(links->drop, 2, user, -1, SQLITE_TRANSIENT);
        must(links->db, sqlite3_step(links->drop), "drop");
        sqlite3_reset(links->drop);
        sqlite3_clear_bindings(links->drop);
    }
    return count;
}
