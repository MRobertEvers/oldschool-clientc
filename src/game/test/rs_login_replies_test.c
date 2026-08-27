/*
 * The login-reply table, against the shipped profile.
 *
 * Read from the real revconfig rather than a fixture: the point is that the
 * table a player actually gets is complete and says the right things. A
 * rejection the profile has no words for shows a blank box, which is the one
 * failure mode a login screen cannot afford -- the player is left with a form
 * that did nothing and no reason why.
 */

#include "game/rs_login_replies.h"

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

static char const* const k_ui_ini = "../revconfig/rs245_2lc/rs245_2lc_dat1_ui.ini";

static void
test_shipped_table(void)
{
    struct RS_LoginReplyTable table;
    struct RS_LoginReply const* reply;

    RS_LoginReplies_Init(&table);
    RS_LoginReplies_LoadSources(&table, k_ui_ini, NULL, NULL);

    TEST_ASSERT(table.count > 0, "the shipped profile declares login replies");

    /* The one every player meets. Client-TS response 3. */
    reply = RS_LoginReplies_Get(&table, 3);
    TEST_ASSERT(reply != NULL, "code 3 is declared");
    if( reply )
    {
        TEST_ASSERT(reply->code == 3, "code 3 resolves to itself, not the default");
        TEST_ASSERT(
            strcmp(reply->line[1], "Invalid username or password.") == 0,
            "code 3 says what went wrong");
    }

    /* Two lines, and the reference leaves the first blank on this one -- an
     * empty line must stay empty rather than inherit anything. */
    if( reply )
        TEST_ASSERT(reply->line[0][0] == '\0', "code 3 has no first line");

    reply = RS_LoginReplies_Get(&table, 7);
    TEST_ASSERT(reply && strcmp(reply->line[0], "This world is full.") == 0, "code 7 lines");

    /* An unlisted code must still say something: falling through to nothing is
     * how a player ends up staring at a form that silently did nothing. */
    reply = RS_LoginReplies_Get(&table, 99);
    TEST_ASSERT(reply != NULL, "an unlisted code falls back to the default");
    if( reply )
        TEST_ASSERT(reply->line[0][0] != '\0', "the default says something");

    /* The transport's own case, which no protocol byte covers. */
    reply = RS_LoginReplies_Get(&table, REVCONFIG_LOGIN_REPLY_CODE_CONNECT_FAILED);
    TEST_ASSERT(reply != NULL, "connect_failed is declared");
    if( reply )
        TEST_ASSERT(
            reply->code == REVCONFIG_LOGIN_REPLY_CODE_CONNECT_FAILED,
            "connect_failed resolves to itself, not the default");

    /* And the prose the client knows when to say but not what to say. */
    TEST_ASSERT(
        RS_LoginReplies_String(&table, "connecting") != NULL, "[string:connecting] is declared");
    TEST_ASSERT(
        RS_LoginReplies_String(&table, "no_such_string") == NULL,
        "an undeclared string is absent, not empty");

    /* The boot bar's captions. Without these the longest wait a player meets
     * -- the gameframe bake after a successful login -- is a silent black
     * screen, which reads as a hang. */
    TEST_ASSERT(
        RS_LoginReplies_String(&table, "entering_world") != NULL,
        "the post-login load has a caption");
    TEST_ASSERT(RS_LoginReplies_String(&table, "loading") != NULL, "the boot load has a caption");

    RS_LoginReplies_Free(&table);
}

/* A profile that declares nothing must answer NULL rather than a blank entry:
 * the caller logs the raw code instead of drawing an empty box. */
static void
test_absent_profile(void)
{
    struct RS_LoginReplyTable table;

    RS_LoginReplies_Init(&table);
    RS_LoginReplies_LoadSources(&table, NULL, NULL, NULL);
    TEST_ASSERT(table.count == 0, "no sources, no entries");
    TEST_ASSERT(RS_LoginReplies_Get(&table, 3) == NULL, "undeclared profile answers NULL");
    RS_LoginReplies_Free(&table);
}

int
main(void)
{
    g_failures = 0;
    g_checks = 0;

    test_shipped_table();
    test_absent_profile();

    if( g_failures )
    {
        fprintf(stderr, "%d failure(s) of %d check(s)\n", g_failures, g_checks);
        return 1;
    }
    printf("rs_login_replies_test: ok (%d checks)\n", g_checks);
    return 0;
}
