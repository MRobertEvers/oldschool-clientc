/*
 * The player account server: its lease table on its own, then its routes as
 * a world, the gate and `--account` reach them -- through player_client.c,
 * with the handler called in process (PlayerClient_SetLocal), so no socket.
 *
 * The contract it pins:
 *   - one character is held by one world; a second world is refused with the
 *     first one's name, and only after the password was checked;
 *   - the hold does not lapse: it is the note "this world has the save", and
 *     only the save's return, the world's hello or an operator clears it;
 *   - a login hands the world the server's copy of the save, and tells a world
 *     that had the character already that its own copy is the newer;
 *   - a save is taken back only under the live lease, so a world whose
 *     character was taken from it cannot overwrite anyone's;
 *   - a world's hello frees what an EARLIER run held, keeping the saves it
 *     names and anything this run has been granted;
 *   - the journal brings every hold back after a restart, and a torn last
 *     line is not one;
 *   - the account site's edits are refused while a world has the character;
 *   - display names are one registry;
 *   - an unanswered call says so (UNAVAILABLE, -1), never "wrong password".
 *
 * Run: make -C src test-player-server
 */

#include "mem/torirs_mem.h"
#include "playerserver/player_client.h"
#include "playerserver/player_lease.h"
#include "playerserver/player_proto.h"
#include "playerserver/player_server.h"
#include "torirsserver/torirs_server_auth.h"
#include "torirsserver/torirs_server_savefile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

#define TEST_SAVES "build/player_server_test_saves"

static int g_failures;

static void
check(
    int ok,
    const char* what)
{
    if( ok )
        return;
    fprintf(stderr, "FAIL: %s\n", what);
    g_failures++;
}

/* ------------------------------------------------------------------ */
/* The table                                                           */
/* ------------------------------------------------------------------ */

static void
check_table(void)
{
    struct PlayerLeases table;
    char a[PLAYER_LEASE_TOKEN_HEX + 1];
    char b[PLAYER_LEASE_TOKEN_HEX + 1];
    char holder[PLAYER_WORLD_MAX];
    char world[PLAYER_WORLD_MAX];
    char keep[1][PLAYER_KEY_MAX] = { "carol" };
    int had;

    PlayerLeases_Open(&table, "");
    check(PlayerLeases_Acquire(&table, "alice", "w1", "boot1", a, &had, holder, sizeof(holder)) ==
                  1 &&
              strlen(a) == PLAYER_LEASE_TOKEN_HEX && !had,
          "table: a free character is granted, with a token");
    check(PlayerLeases_Acquire(&table, "alice", "w2", "bootX", b, &had, holder, sizeof(holder)) ==
                  0 &&
              strcmp(holder, "w1") == 0 && b[0] == '\0',
          "table: a second world is refused, told the holder");
    check(PlayerLeases_Acquire(&table, "alice", "w1", "boot1", b, &had, holder, sizeof(holder)) ==
                  1 &&
              strcmp(a, b) != 0 && had,
          "table: the holder asking again is granted a new token, and told it had it");
    check(!PlayerLeases_Holds(&table, "alice", "w1", a) && PlayerLeases_Holds(&table, "alice", "w1", b),
          "table: only the newest token holds it");
    check(PlayerLeases_Release(&table, "alice", "w1", a) == 0 &&
              PlayerLeases_Find(&table, "alice") != NULL,
          "table: the older token's release changes nothing");
    check(PlayerLeases_Release(&table, "alice", "w2", b) == 0,
          "table: another world cannot release it");
    check(PlayerLeases_Release(&table, "alice", "w1", b) == 1 &&
              PlayerLeases_Find(&table, "alice") == NULL,
          "table: the live token's release frees it");

    /* Hello: an earlier run's holds go, but the saves it names and this
     * run's grants stay. */
    PlayerLeases_Acquire(&table, "alice", "w1", "boot1", a, &had, holder, sizeof(holder));
    PlayerLeases_Acquire(&table, "carol", "w1", "boot1", a, &had, holder, sizeof(holder));
    PlayerLeases_Acquire(&table, "dave", "w1", "boot2", a, &had, holder, sizeof(holder));
    PlayerLeases_Acquire(&table, "erin", "w2", "bootX", b, &had, holder, sizeof(holder));
    check(PlayerLeases_DropWorld(&table, "w1", "boot2", (char const (*)[PLAYER_KEY_MAX])keep, 1,
                                 NULL, NULL) == 1 &&
              PlayerLeases_Find(&table, "alice") == NULL &&
              PlayerLeases_Find(&table, "carol") != NULL &&
              PlayerLeases_Find(&table, "dave") != NULL &&
              PlayerLeases_Find(&table, "erin") != NULL,
          "table: a hello drops the earlier run's unkept holds only");

    /* An operator. */
    check(PlayerLeases_Revoke(&table, "erin", world, sizeof(world)) == 1 && strcmp(world, "w2") == 0 &&
              PlayerLeases_Find(&table, "erin") == NULL && !PlayerLeases_Holds(&table, "erin", "w2", b),
          "table: an operator takes a character back, and its token is dead");
    check(PlayerLeases_Revoke(&table, "erin", world, sizeof(world)) == 0, "table: nothing to take");
    PlayerLeases_Close(&table);
}

static void
check_journal(void)
{
    char path[1024];
    struct PlayerLeases table;
    char a[PLAYER_LEASE_TOKEN_HEX + 1];
    char b[PLAYER_LEASE_TOKEN_HEX + 1];
    char holder[PLAYER_WORLD_MAX];
    struct PlayerLease const* row;
    int had;
    FILE* torn;

    snprintf(path, sizeof(path), "%s/lease_journal_test.journal", TEST_SAVES);
    remove(path);
    PlayerLeases_Open(&table, path);
    check(table.count == 0, "journal: none yet, an empty table");
    PlayerLeases_Acquire(&table, "alice", "w1", "boot1", a, &had, holder, sizeof(holder));
    PlayerLeases_Acquire(&table, "bob", "w2", "boot2", b, &had, holder, sizeof(holder));
    PlayerLeases_Acquire(&table, "carol", "w2", "boot2", b, &had, holder, sizeof(holder));
    PlayerLeases_Release(&table, "carol", "w2", b);
    PlayerLeases_Close(&table);
    /* A write cut off by a crash. */
    torn = fopen(path, "ab");
    check(torn != NULL, "journal: the file is there");
    if( torn )
    {
        fputs("A\tdave\tw3\tboot3\t0123456789abcdef0123456789abcdef", torn);
        fclose(torn);
    }

    PlayerLeases_Open(&table, path);
    row = PlayerLeases_Find(&table, "alice");
    check(row && strcmp(row->world, "w1") == 0 && strcmp(row->token, a) == 0 &&
              strcmp(row->boot, "boot1") == 0,
          "journal: a hold comes back with its world, run and token, however long ago");
    check(PlayerLeases_Find(&table, "bob") != NULL, "journal: every hold comes back");
    check(PlayerLeases_Find(&table, "carol") == NULL, "journal: a released one stays released");
    check(PlayerLeases_Find(&table, "dave") == NULL, "journal: a torn line is not a hold");
    check(PlayerLeases_Acquire(&table, "alice", "w2", "boot2", b, &had, holder, sizeof(holder)) == 0 &&
              strcmp(holder, "w1") == 0,
          "journal: after the restart the second world is still refused");
    check(PlayerLeases_DropWorld(&table, "w1", "boot9", NULL, 0, NULL, NULL) == 1,
          "journal: w1's next run's hello frees it");
    PlayerLeases_Close(&table);

    PlayerLeases_Open(&table, path);
    check(PlayerLeases_Find(&table, "alice") == NULL && PlayerLeases_Find(&table, "bob") != NULL,
          "journal: the hello's drop is written down too");
    PlayerLeases_Close(&table);
    remove(path);
}

/* ------------------------------------------------------------------ */
/* The routes, through the client                                      */
/* ------------------------------------------------------------------ */

static int g_each;

static void
on_each(
    struct PlayerAccountInfo const* account,
    void* ctx)
{
    (void)account;
    (void)ctx;
    g_each++;
}

static void
check_routes(void)
{
    struct PlayerLogin w1;
    struct PlayerLogin w2;
    struct PlayerLogin again;
    struct PlayerAccountInfo account;
    char owner[PLAYER_KEY_MAX];
    char token[TORIRSSERVER_LINK_TOKEN_HEX + 1];
    char holder[PLAYER_WORLD_MAX];
    char world[PLAYER_WORLD_MAX];
    char keep[1][PLAYER_KEY_MAX] = { "bob" };
    char const* problem = NULL;
    char* save = NULL;

    check(PlayerServer_Open() == 1, "routes: the server opens the saves directory");
#ifndef _WIN32
    {
        /* A second player account server on these saves -- another world
         * that names none -- cannot have the lock, so never opens the
         * journal under this one. */
        char lock_path[1024];
        int fd;

        snprintf(lock_path, sizeof(lock_path), "%s/player_server.lock", TEST_SAVES);
        fd = open(lock_path, O_RDWR);
        check(fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) != 0,
              "routes: the saves directory's lock is held while the server is open");
        if( fd >= 0 )
            close(fd);
    }
#endif
    PlayerClient_SetLocal(PlayerServer_Handle);

    /* A first login: claimed, leased, and no save yet. */
    PlayerClient_Login("w1", "boot1", "Alice", "blue7horse", &w1);
    check(w1.result == TORIRSSERVER_AUTH_CLAIMED && w1.lease[0] && !w1.admin && !w1.had &&
              !w1.save && w1.save_len == 0,
          "routes: a first login claims the name, takes the lease, and has no save");
    PlayerClient_LoginFree(&w1);
    PlayerClient_Login("w2", "boot2", "Alice", "wrongpass1", &w2);
    check(w2.result == TORIRSSERVER_AUTH_REFUSED && !w2.lease[0] && !w2.holder[0],
          "routes: a wrong password elsewhere is refused as one, saying nothing of the lease");
    PlayerClient_Login("w2", "boot2", "alice", "blue7horse", &w2);
    check(w2.result == TORIRSSERVER_AUTH_ONLINE && strcmp(w2.holder, "w1") == 0 && !w2.lease[0],
          "routes: the right password on another world: already logged in, on w1");
    PlayerClient_Login("w2", "boot2", "alice", NULL, &w2);
    check(w2.result == TORIRSSERVER_AUTH_ONLINE, "routes: a claim (no password) is refused too");
    check(PlayerClient_Account("alice", &account) == 1 && strcmp(account.world, "w1") == 0,
          "routes: the account says which world has the character");
    check(PlayerClient_SaveGet("alice", &save, world, sizeof(world)) == 0 && strcmp(world, "w1") == 0,
          "routes: no save here yet, and w1 has the character");

    /* The editor cannot write while a world has it. */
    check(PlayerClient_SavePut("alice", "[player]\nname = Alice\n", world, sizeof(world)) == 0 &&
              strcmp(world, "w1") == 0,
          "routes: an edit while a world has the character is refused, naming it");

    /* The save comes back at the logout, under the lease. */
    check(PlayerClient_Logout("w2", "alice", w1.lease, "[player]\nname = Mallory\n", holder,
                              sizeof(holder)) == 0 &&
              strcmp(holder, "w1") == 0,
          "routes: another world cannot hand the save back");
    check(PlayerClient_Logout("w1", "alice", w1.lease, "[player]\nname = Alice\nx = 1\n", holder,
                              sizeof(holder)) == 1,
          "routes: the holder hands the save back");
    check(PlayerClient_Account("alice", &account) == 1 && !account.world[0],
          "routes: and the note is gone");
    check(PlayerClient_SaveGet("alice", &save, world, sizeof(world)) == 1 && save &&
              strcmp(save, "[player]\nname = Alice\nx = 1\n") == 0 && !world[0],
          "routes: the server's copy is the one handed back");
    ToriMem_Free(ToriMem_Net(), save);
    check(PlayerClient_Logout("w1", "alice", w1.lease, "[player]\nname = Late\n", holder,
                              sizeof(holder)) == 0,
          "routes: a second return under the spent lease is refused");

    /* The next login is handed that save. */
    PlayerClient_Login("w2", "boot2", "alice", "blue7horse", &w2);
    check(w2.result == TORIRSSERVER_AUTH_OK && !w2.had && w2.save &&
              strcmp(w2.save, "[player]\nname = Alice\nx = 1\n") == 0,
          "routes: the next world's login carries the save");
    PlayerClient_LoginFree(&w2);
    PlayerClient_Login("w2", "boot2", "alice", "blue7horse", &again);
    check(again.result == TORIRSSERVER_AUTH_OK && again.had && strcmp(again.lease, w2.lease) != 0,
          "routes: the holder's second login: a new token, and told its own copy is the newer");
    PlayerClient_LoginFree(&again);
    check(PlayerClient_Logout("w2", "alice", w2.lease, "[player]\nname = Old\n", holder,
                              sizeof(holder)) == 0,
          "routes: the replaced session's token cannot hand it back");
    check(PlayerClient_Release("w2", "alice", again.lease) == 1,
          "routes: a release with no save frees it, the copy here standing");
    check(PlayerClient_SaveGet("alice", &save, world, sizeof(world)) == 1 && save &&
              strcmp(save, "[player]\nname = Alice\nx = 1\n") == 0,
          "routes: ...unchanged");
    ToriMem_Free(ToriMem_Net(), save);

    /* A restart's hello. */
    PlayerClient_Login("w1", "boot1", "alice", NULL, &w1);
    PlayerClient_LoginFree(&w1);
    check(PlayerClient_Create("Bob", "red5tiger", 0, &problem) == TORIRSSERVER_CREATE_OK,
          "routes: create");
    PlayerClient_Login("w1", "boot1", "bob", NULL, &again);
    PlayerClient_LoginFree(&again);
    check(PlayerClient_Hello("w1", "boot1", NULL, 0) == 0,
          "routes: a late hello of this run lets go of nothing it was granted");
    check(PlayerClient_Hello("w1", "boot3", keep, 1) == 1 &&
              PlayerClient_Account("alice", &account) == 1 && !account.world[0] &&
              PlayerClient_Account("bob", &account) == 1 && strcmp(account.world, "w1") == 0,
          "routes: the next run's hello frees what it has no save for, keeping bob");
    check(PlayerClient_Logout("w1", "bob", again.lease, "[player]\nname = Bob\n", holder,
                              sizeof(holder)) == 1,
          "routes: and bob's save comes back under the old run's lease");

    /* An operator. */
    PlayerClient_Login("w1", "boot3", "alice", NULL, &w1);
    PlayerClient_LoginFree(&w1);
    check(PlayerClient_Revoke("alice", world, sizeof(world)) == 1 && strcmp(world, "w1") == 0,
          "routes: an operator takes alice back from w1");
    check(PlayerClient_Logout("w1", "alice", w1.lease, "[player]\nname = Stale\n", holder,
                              sizeof(holder)) == 0,
          "routes: w1's copy is refused after that");
    check(PlayerClient_SavePut("alice", "[player]\nname = Alice\nx = 2\n", world, sizeof(world)) == 1,
          "routes: with no world holding it, an edit lands");

    PlayerClient_Login("w1", "boot3", "", "blue7horse", &w1);
    check(w1.result == TORIRSSERVER_AUTH_UNUSABLE, "routes: an unusable name");
    check(PlayerClient_Auth("alice", "blue7horse") == TORIRSSERVER_AUTH_OK,
          "routes: a check with no lease");

    /* The records. */
    check(PlayerClient_Create("bob", "red5tiger", 0, &problem) == TORIRSSERVER_CREATE_EXISTS,
          "routes: create refuses an existing name");
    check(PlayerClient_Create("carl", "aaaaaaaa", 0, &problem) == TORIRSSERVER_CREATE_WEAK &&
              problem && problem[0],
          "routes: create says what a weak password breaks");
    check(!PlayerClient_IsAdmin("bob") && PlayerClient_SetAdmin("bob", 1) &&
              PlayerClient_IsAdmin("bob"),
          "routes: the admin flag");
    check(PlayerClient_SetRecorded("bob", 1) && PlayerClient_IsRecorded("bob"),
          "routes: the record flag");
    PlayerClient_Login("w1", "boot3", "bob", "red5tiger", &w1);
    check(w1.result == TORIRSSERVER_AUTH_OK && w1.admin && w1.record && w1.save &&
              strcmp(w1.save, "[player]\nname = Bob\n") == 0,
          "routes: a login carries the account's flags and the save");
    PlayerClient_LoginFree(&w1);
    PlayerClient_Release("w1", "bob", w1.lease);
    check(!PlayerClient_SetAdmin("nobody", 1), "routes: no record, nothing set");
    check(PlayerClient_SetPassword("bob", "green3fox", &problem) &&
              PlayerClient_Auth("bob", "green3fox") == TORIRSSERVER_AUTH_OK,
          "routes: a new password");
    g_each = 0;
    check(PlayerClient_Each("", on_each, NULL) == 2 && g_each == 2, "routes: every account");
    check(PlayerClient_Each("bo", on_each, NULL) == 1, "routes: filtered by key");
    check(PlayerClient_Account("nobody", &account) == 0, "routes: no record, no account");

    /* Display names. */
    check(PlayerClient_NameOwner("Zezima", owner, sizeof(owner)) == 0, "names: a free name");
    check(PlayerClient_NameClaim("Zezima", "alice") && !PlayerClient_NameClaim("zezima", "bob"),
          "names: one account has it");
    check(PlayerClient_NameOwner("zezima", owner, sizeof(owner)) == 1 && strcmp(owner, "alice") == 0,
          "names: and the registry says whose");
    check(PlayerClient_NameOwner("bob", owner, sizeof(owner)) == 1 && strcmp(owner, "bob") == 0,
          "names: a login name is its account's");
    PlayerClient_NameRelease("zezima", "bob");
    check(PlayerClient_NameOwner("zezima", owner, sizeof(owner)) == 1,
          "names: only the holder releases it");
    PlayerClient_NameRelease("zezima", "alice");
    check(PlayerClient_NameOwner("zezima", owner, sizeof(owner)) == 0, "names: released");

    /* Links. */
    check(PlayerClient_Link("alice", "wrongpass1", "matt", token) == TORIRSSERVER_LINK_REFUSED,
          "routes: a link needs the account's password");
    check(PlayerClient_Link("alice", "blue7horse", "matt", token) == TORIRSSERVER_LINK_OK &&
              strlen(token) == TORIRSSERVER_LINK_TOKEN_HEX,
          "routes: linked, with a token");
    check(PlayerClient_ManagedBy("alice", owner, sizeof(owner)) == 1 && strcmp(owner, "matt") == 0,
          "routes: managed by its login");
    check(PlayerClient_LinkVerify("alice", "matt", token) &&
              !PlayerClient_LinkVerify("alice", "matt", "0000") &&
              PlayerClient_LinkCheck("alice", "someone", token) == 0,
          "routes: only the live token verifies");
    check(PlayerClient_LoginIsStaff("matt") == 0, "routes: alice is not admin, so matt is not staff");
    PlayerClient_Login("w1", "boot3", "alice", "blue7horse", &w1);
    check(w1.result == TORIRSSERVER_AUTH_MANAGED, "routes: a linked account's password is refused");
    check(PlayerClient_Assign("bob", "matt", token) == TORIRSSERVER_LINK_OK &&
              PlayerClient_LoginIsStaff("matt") == 1,
          "routes: staff's assign; an admin account makes its login staff");
    check(PlayerClient_Unlink("alice") && PlayerClient_ManagedBy("alice", owner, sizeof(owner)) == 0,
          "routes: unlinked");

    PlayerClient_SetLocal(NULL);
    PlayerServer_Close();
}

static void
check_silent(void)
{
    struct PlayerLogin login;
    struct PlayerAccountInfo account;
    char owner[PLAYER_KEY_MAX];
    char token[TORIRSSERVER_LINK_TOKEN_HEX + 1];
    char holder[PLAYER_WORLD_MAX];
    char* save = NULL;

    /* Port 1: nothing listens there. */
    setenv("TORIRS_PLAYER_SERVER", "127.0.0.1:1", 1);
    PlayerClient_Login("w1", "boot1", "alice", "blue7horse", &login);
    check(login.result == TORIRSSERVER_AUTH_UNAVAILABLE, "silent: a login is unavailable");
    check(PlayerClient_Auth("alice", "blue7horse") == TORIRSSERVER_AUTH_UNAVAILABLE,
          "silent: a check is unavailable");
    check(PlayerClient_Account("alice", &account) == -1, "silent: an account lookup says so");
    check(PlayerClient_ManagedBy("alice", owner, sizeof(owner)) == -1,
          "silent: managed-by refuses (-1)");
    check(PlayerClient_LinkCheck("alice", "matt", "00") == -1, "silent: a link check says so");
    check(PlayerClient_Link("alice", "blue7horse", "matt", token) == TORIRSSERVER_LINK_UNAVAILABLE,
          "silent: a link is unavailable");
    check(PlayerClient_Logout("w1", "alice", "00", "[player]\n", holder, sizeof(holder)) == -1,
          "silent: a return says so, so the world keeps the save");
    check(PlayerClient_SaveGet("alice", &save, holder, sizeof(holder)) == -1 && !save,
          "silent: a save read says so");
    check(PlayerClient_NameOwner("zezima", owner, sizeof(owner)) == -1, "silent: a name lookup says so");
    check(!PlayerClient_NameClaim("zezima", "alice"), "silent: a name is not claimed");
    unsetenv("TORIRS_PLAYER_SERVER");
}

int
main(void)
{
    char path[1024];

    setenv("TORIRSSERVER_SAVES", TEST_SAVES, 1);
    ToriMem_Init();
    remove(ToriRSServer_AccountPath("alice"));
    remove(ToriRSServer_AccountPath("bob"));
    remove(ToriRSServer_AccountPath("carl"));
    remove(ToriRSServer_SaveCanonicalPath("alice"));
    remove(ToriRSServer_SaveCanonicalPath("bob"));
    remove(TEST_SAVES "/names/zezima");
    snprintf(path, sizeof(path), "%s/player_leases.journal", TEST_SAVES);
    remove(path);
    ToriRSServer_SaveMkdirParent(path);

    check_table();
    check_journal();
    check_routes();
    check_silent();

    ToriMem_Shutdown();
    if( g_failures )
    {
        fprintf(stderr, "player server: %d check(s) failed\n", g_failures);
        return 1;
    }
    printf("player server: all checks passed\n");
    return 0;
}
