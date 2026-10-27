/*
 * The device's saved accounts (game/rs_accounts.h) and the flat JSON reader
 * that reads the account server's answers (net/flat_json.h).
 *
 * Run: make -C src test-rs-accounts
 */

#include "game/rs_accounts.h"
#include "mem/torirs_mem.h"
#include "net/flat_json.h"

#include <stdio.h>
#include <string.h>

static int g_failures;
static int g_checks;

#define TEST_ASSERT(cond, msg)                                                                     \
    do                                                                                             \
    {                                                                                              \
        g_checks++;                                                                                \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                        \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static void
test_remember_and_forget(void)
{
    struct RS_Accounts accounts;

    RS_Accounts_Init(&accounts);
    RS_Accounts_Remember(&accounts, "Alice", "rt-a");
    RS_Accounts_Remember(&accounts, "Bob", "rt-b");
    TEST_ASSERT(accounts.count == 2 && strcmp(accounts.list[0].name, "Bob") == 0,
                "the latest sign-in is first");
    RS_Accounts_Remember(&accounts, "alice", "rt-a2");
    TEST_ASSERT(accounts.count == 2, "the same account (as the server keys it) is not added twice");
    TEST_ASSERT(strcmp(accounts.list[0].name, "alice") == 0 &&
                    strcmp(accounts.list[0].refresh_token, "rt-a2") == 0,
                "it moves first with its rotated token");
    TEST_ASSERT(RS_Accounts_Find(&accounts, "BOB") == 1, "find folds case");
    TEST_ASSERT(RS_Accounts_Find(&accounts, "carol") == -1, "and misses an unknown name");
    RS_Accounts_Forget(&accounts, 0);
    TEST_ASSERT(accounts.count == 1 && strcmp(accounts.list[0].name, "Bob") == 0,
                "forget drops one and closes the gap");

    for( int i = 0; i < RS_ACCOUNTS_MAX + 3; i++ )
    {
        char name[16];

        snprintf(name, sizeof(name), "p%d", i);
        RS_Accounts_Remember(&accounts, name, "rt");
    }
    TEST_ASSERT(accounts.count == RS_ACCOUNTS_MAX, "the list stops at its cap");
    TEST_ASSERT(RS_Accounts_Find(&accounts, "Bob") == -1, "the least recently played falls off");
}

static void
test_round_trip(void)
{
    struct RS_Accounts accounts;
    struct RS_Accounts read;
    void* data = NULL;
    int size = 0;
    static char const garbage[] = "[account]\nname = NoToken\n\n[other]\nname = x\nrefresh = y\n"
                                  "[account]\nrefresh = no-name\n";

    RS_Accounts_Init(&accounts);
    RS_Accounts_Remember(&accounts, "Second Account", "eyJ.second.sig");
    RS_Accounts_Remember(&accounts, "Matt", "eyJ.first.sig");
    RS_Accounts_Encode(&accounts, &data, &size);
    RS_Accounts_Decode(&read, data, size);
    ToriMem_Free(ToriMem_Process(), data);
    TEST_ASSERT(read.count == 2, "both accounts survive the file");
    TEST_ASSERT(strcmp(read.list[0].name, "Matt") == 0 &&
                    strcmp(read.list[0].refresh_token, "eyJ.first.sig") == 0,
                "in order, with their tokens");
    TEST_ASSERT(strcmp(read.list[1].name, "Second Account") == 0,
                "a name with a space survives");

    RS_Accounts_Decode(&read, garbage, (int)strlen(garbage));
    TEST_ASSERT(read.count == 0, "a section missing its name or token, or not an account, is skipped");
    RS_Accounts_Decode(&read, "", 0);
    TEST_ASSERT(read.count == 0, "an empty file is no accounts");
}

static void
test_flat_json(void)
{
    static char const body[] =
        " {\"access_token\":\"a.b.c\", \"nested\":{\"token\":\"wrong\",\"x\":[1,\"}\"]},"
        "\"expires_in\": 600, \"name\":\"Bo\\\"b\\u0041\", \"token\" : \"right\"}";
    char text[64];
    long long number = 0;

    TEST_ASSERT(FlatJson_String(body, strlen(body), "access_token", text, sizeof(text)) &&
                    strcmp(text, "a.b.c") == 0,
                "json: a string field");
    TEST_ASSERT(FlatJson_String(body, strlen(body), "token", text, sizeof(text)) &&
                    strcmp(text, "right") == 0,
                "json: a nested object's key is not the top level's");
    TEST_ASSERT(FlatJson_Int64(body, strlen(body), "expires_in", &number) && number == 600,
                "json: a number field");
    TEST_ASSERT(FlatJson_String(body, strlen(body), "name", text, sizeof(text)) &&
                    strcmp(text, "Bo\"bA") == 0,
                "json: escapes decode");
    TEST_ASSERT(!FlatJson_String(body, strlen(body), "missing", text, sizeof(text)),
                "json: a missing key is absent");
    TEST_ASSERT(!FlatJson_String(body, strlen(body), "access_token", text, 3),
                "json: a value that does not fit is refused, not truncated");
    TEST_ASSERT(!FlatJson_String("[\"token\"]", 9, "token", text, sizeof(text)),
                "json: an array has no keys");
    TEST_ASSERT(!FlatJson_String("{\"token\":\"unterminated", 22, "token", text, sizeof(text)),
                "json: an unterminated string is refused");
}

/* The gate's /account/players answer: a web login and its linked players. */
static void
test_site_session(void)
{
    static char const body[] = "{\"user\":\"matt\",\"players\":\"Matt\\nSecond Account\"}";
    struct RS_SiteSession site;

    RS_SiteSession_Decode(&site, body, (int)strlen(body));
    TEST_ASSERT(site.signed_in && strcmp(site.user, "matt") == 0, "site: the web login is read");
    TEST_ASSERT(site.count == 2 && strcmp(site.players[0], "Matt") == 0 &&
                    strcmp(site.players[1], "Second Account") == 0,
                "site: the players split on newlines");
    TEST_ASSERT(RS_SiteSession_Find(&site, "second_account") == 1,
                "site: find folds as the server keys accounts");
    RS_SiteSession_Add(&site, "MATT");
    TEST_ASSERT(site.count == 2, "site: a linked player is not added twice");
    RS_SiteSession_Add(&site, "Third");
    TEST_ASSERT(site.count == 3 && RS_SiteSession_Find(&site, "third") == 2,
                "site: a new link joins the end");

    {
        static char const empty[] = "{\"user\":\"matt\",\"players\":\"\"}";

        RS_SiteSession_Decode(&site, empty, (int)strlen(empty));
    }
    TEST_ASSERT(site.signed_in && site.count == 0, "site: signed in with nothing linked");
    {
        static char const refused[] = "{\"error\":\"invalid_request\"}";

        RS_SiteSession_Decode(&site, refused, (int)strlen(refused));
    }
    TEST_ASSERT(!site.signed_in && site.count == 0, "site: an error body is not a session");
    RS_SiteSession_Decode(&site, "", 0);
    TEST_ASSERT(!site.signed_in, "site: nor is nothing");
}

int
main(void)
{
    ToriMem_Init();
    test_remember_and_forget();
    test_round_trip();
    test_flat_json();
    test_site_session();
    if( g_failures )
    {
        fprintf(stderr, "%d failure(s) of %d check(s)\n", g_failures, g_checks);
        return 1;
    }
    printf("rs_accounts_test: ok (%d checks)\n", g_checks);
    return 0;
}
