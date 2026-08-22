/*
 * The message store the cache's chatbox scripts read.
 *
 * At a cache revision the client draws no chat line at all: interface 162 ships
 * 500 text components and `[proc,rebuildchatbox]` fills them, reading every
 * message back through the CHAT_GETHISTORY* opcodes. So the store *is* the
 * chatbox, and the things worth testing are the ones whose only symptom is
 * "the chatbox is wrong" with nothing in the renderer to blame:
 *
 *   - the per-type rings. `[proc,script553]` finds the newest message by
 *     sweeping every chat type and reading line 0 of each, so a message filed
 *     under the wrong type, or a ring that shifts the wrong way, moves lines
 *     between filter tabs.
 *   - the uid WALK. rebuildchatbox starts at the newest uid and calls
 *     chat_getprevuid until it gets -1, one call per line it draws. If prev
 *     ever skips or repeats, the chatbox drops or doubles messages; if it does
 *     not terminate, the script does not either.
 *   - the recycled tail. A ring at capacity reuses its oldest node, and that
 *     node is still on the global uid chain until it is unlinked. Getting that
 *     wrong leaves the walk pointing at a node holding a newer message.
 */

#include "game/rs_chat.h"
#include "game/rs_social.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "  FAIL ");                                                            \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fprintf(stderr, "\n       (%s at %s:%d)\n", #cond, __FILE__, __LINE__);                \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/* The three types the chatbox actually separates in this test: a server line,
 * a public line and a private one. Numeric because they are the *server's*
 * values -- the wire carries the type and the cache's scripts switch on it. */
enum
{
    TYPE_GAME = 0,
    TYPE_PUBLIC = 2,
    TYPE_PRIVATE = 3,
};

/* Walk the history the way rebuildchatbox does: newest uid first, then
 * chat_getprevuid until -1. Returns how many nodes the walk visited and fills
 * `out` with their texts. */
static int
walk_history(
    struct RS_Chat* chat,
    int start_uid,
    char out[][RS_CHAT_TEXT_LEN],
    int cap)
{
    int n = 0;
    int uid = start_uid;

    while( uid != -1 && n < cap )
    {
        struct RS_ChatNode const* node = RS_Chat_NodeByUid(chat, uid);
        if( !node )
            break;
        snprintf(out[n], RS_CHAT_TEXT_LEN, "%s", node->text);
        n++;
        uid = RS_Chat_PrevUid(chat, uid);
    }
    return n;
}

/* The newest uid across every type, which is what [proc,script553] computes. */
static int
newest_uid(struct RS_Chat const* chat)
{
    int best = -1;
    for( int type = 0; type < RS_CHAT_TYPE_MAX; type++ )
    {
        struct RS_ChatNode const* node;
        if( RS_Chat_TypeCount(chat, type) <= 0 )
            continue;
        node = RS_Chat_NodeByTypeAndLine(chat, type, 0);
        if( node && node->uid > best )
            best = node->uid;
    }
    return best;
}

int
main(void)
{
    /* ---- per-type rings, newest at line 0 ---------------------------- */
    {
        struct RS_Chat chat;
        struct RS_ChatNode const* node;

        RS_Chat_Init(&chat, "Player");
        RS_Chat_AddMessage(&chat, TYPE_GAME, NULL, NULL, "welcome", 10);
        RS_Chat_AddMessage(&chat, TYPE_PUBLIC, "Zezima", "zezima", "hello", 11);
        RS_Chat_AddMessage(&chat, TYPE_GAME, NULL, NULL, "you gain xp", 12);

        CHECK(RS_Chat_TypeCount(&chat, TYPE_GAME) == 2, "two game lines, got %d",
              RS_Chat_TypeCount(&chat, TYPE_GAME));
        CHECK(RS_Chat_TypeCount(&chat, TYPE_PUBLIC) == 1, "one public line, got %d",
              RS_Chat_TypeCount(&chat, TYPE_PUBLIC));
        CHECK(RS_Chat_TypeCount(&chat, TYPE_PRIVATE) == 0, "no private lines, got %d",
              RS_Chat_TypeCount(&chat, TYPE_PRIVATE));
        /* A type this client has never seen answers empty rather than
         * misbehaving: script553 sweeps 0..118 on every rebuild. */
        CHECK(RS_Chat_TypeCount(&chat, 118) == 0, "an unseen type is empty");

        node = RS_Chat_NodeByTypeAndLine(&chat, TYPE_GAME, 0);
        CHECK(node && strcmp(node->text, "you gain xp") == 0, "line 0 is the newest of its type");
        node = RS_Chat_NodeByTypeAndLine(&chat, TYPE_GAME, 1);
        CHECK(node && strcmp(node->text, "welcome") == 0, "line 1 is the one before it");
        CHECK(!RS_Chat_NodeByTypeAndLine(&chat, TYPE_GAME, 2), "past the end is absent");

        node = RS_Chat_NodeByTypeAndLine(&chat, TYPE_PUBLIC, 0);
        CHECK(node && strcmp(node->name, "Zezima") == 0, "the printable name is kept");
        CHECK(node && strcmp(node->sender, "zezima") == 0, "and the account name separately");
        CHECK(node && node->clock == 11, "the client cycle is stamped, got %d",
              node ? node->clock : -1);

        RS_Chat_Free(&chat);
    }

    /* ---- the uid walk, across types ---------------------------------- */
    {
        struct RS_Chat chat;
        char seen[8][RS_CHAT_TEXT_LEN];
        int n;

        RS_Chat_Init(&chat, "Player");
        RS_Chat_AddMessage(&chat, TYPE_GAME, NULL, NULL, "first", 1);
        RS_Chat_AddMessage(&chat, TYPE_PUBLIC, "Bob", "bob", "second", 2);
        RS_Chat_AddMessage(&chat, TYPE_PRIVATE, "Ann", "ann", "third", 3);
        RS_Chat_AddMessage(&chat, TYPE_GAME, NULL, NULL, "fourth", 4);

        CHECK(newest_uid(&chat) == 3, "the newest uid is the last message's, got %d",
              newest_uid(&chat));

        n = walk_history(&chat, newest_uid(&chat), seen, 8);
        CHECK(n == 4, "the walk visits every message once, got %d", n);
        /* Newest first, and interleaved across types -- the walk is by time,
         * not by tab. */
        CHECK(n == 4 && strcmp(seen[0], "fourth") == 0, "walk[0] newest");
        CHECK(n == 4 && strcmp(seen[1], "third") == 0, "walk[1]");
        CHECK(n == 4 && strcmp(seen[2], "second") == 0, "walk[2]");
        CHECK(n == 4 && strcmp(seen[3], "first") == 0, "walk[3] oldest");
        CHECK(RS_Chat_PrevUid(&chat, 0) == -1, "the oldest has no previous");
        CHECK(RS_Chat_NextUid(&chat, 3) == -1, "the newest has no next");
        CHECK(RS_Chat_NextUid(&chat, 0) == 1, "next walks forward again");

        /* A uid that never existed, and one from a message that has gone. */
        CHECK(!RS_Chat_NodeByUid(&chat, 99), "an unknown uid has no node");
        CHECK(RS_Chat_PrevUid(&chat, 99) == -1, "and no previous");

        RS_Chat_Free(&chat);
    }

    /* ---- a ring at capacity recycles its tail ------------------------ */
    {
        struct RS_Chat chat;
        char seen[RS_CHAT_TYPE_LINES + 8][RS_CHAT_TEXT_LEN];
        struct RS_ChatNode const* node;
        int n;

        RS_Chat_Init(&chat, "Player");
        for( int i = 0; i < RS_CHAT_TYPE_LINES + 5; i++ )
        {
            char text[32];
            snprintf(text, sizeof(text), "m%d", i);
            RS_Chat_AddMessage(&chat, TYPE_GAME, NULL, NULL, text, i);
        }

        CHECK(RS_Chat_TypeCount(&chat, TYPE_GAME) == RS_CHAT_TYPE_LINES,
              "the ring stops at its capacity, got %d", RS_Chat_TypeCount(&chat, TYPE_GAME));
        node = RS_Chat_NodeByTypeAndLine(&chat, TYPE_GAME, 0);
        CHECK(node && strcmp(node->text, "m104") == 0, "newest is the last added, got '%s'",
              node ? node->text : "(none)");
        node = RS_Chat_NodeByTypeAndLine(&chat, TYPE_GAME, RS_CHAT_TYPE_LINES - 1);
        CHECK(node && strcmp(node->text, "m5") == 0, "oldest kept is m5, got '%s'",
              node ? node->text : "(none)");

        /* The five dropped messages are gone from the uid chain too -- a
         * recycled node must be unlinked, or the walk finds it holding a newer
         * message and never terminates. */
        CHECK(!RS_Chat_NodeByUid(&chat, 0), "a dropped message has no node");
        n = walk_history(&chat, newest_uid(&chat), seen, RS_CHAT_TYPE_LINES + 8);
        CHECK(n == RS_CHAT_TYPE_LINES, "the walk visits exactly what is kept, got %d", n);

        RS_Chat_Free(&chat);
    }

    /* ---- friend / ignore state, as gethistoryex reports it ----------- */
    {
        struct RS_Chat chat;
        struct RS_Social social;
        struct RS_ChatNode const* node;

        RS_Chat_Init(&chat, "Player");
        RS_Social_Init(&social);
        RS_Social_AddFriend(&social, "bob", 0);
        RS_Social_AddIgnore(&social, "mallory");

        RS_Chat_AddMessage(&chat, TYPE_PUBLIC, "Bob", "bob", "hi", 1);
        RS_Chat_AddMessage(&chat, TYPE_PUBLIC, "Mallory", "mallory", "spam", 2);
        RS_Chat_AddMessage(&chat, TYPE_GAME, NULL, NULL, "server", 3);

        node = RS_Chat_NodeByUid(&chat, 0);
        CHECK(node && RS_Chat_NodeFriendState(node, &social) == 1, "a friend reads 1");
        node = RS_Chat_NodeByUid(&chat, 1);
        CHECK(node && RS_Chat_NodeFriendState(node, &social) == 2, "an ignored sender reads 2");
        node = RS_Chat_NodeByUid(&chat, 2);
        CHECK(node && RS_Chat_NodeFriendState(node, &social) == 0, "no sender reads 0");
        node = RS_Chat_NodeByUid(&chat, 0);
        CHECK(node && RS_Chat_NodeFriendState(node, NULL) == 0, "no social store reads 0");

        RS_Chat_Free(&chat);
    }

    if( g_failures )
    {
        fprintf(stderr, "chat store: %d failure(s)\n", g_failures);
        return 1;
    }
    fprintf(stderr, "chat store: all checks passed\n");
    return 0;
}
