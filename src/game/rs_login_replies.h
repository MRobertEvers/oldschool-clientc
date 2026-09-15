#ifndef SRC_GAME_RS_LOGIN_REPLIES_H
#define SRC_GAME_RS_LOGIN_REPLIES_H

#include "revconfig/revconfig.h"

/*
 * What a login rejection means, in words.
 *
 * The reply CODE is protocol and belongs to net/; the SENTENCES are
 * presentation and differ per revision, which is the split revconfig exists
 * for. The old lane answers codes 3..21 in two lines and the modern one -3..74
 * in three, and neither table has any business being compiled into the client.
 *
 * Built once per session from the same sources RevConfigRefs reads, and kept
 * alive beside it: the builder's item buffer is torn down after the bake, and
 * these lines are needed every time a login fails.
 */

#define RS_LOGIN_REPLY_LINES 3
#define RS_LOGIN_REPLY_LINE_LEN 256

struct RS_LoginReply
{
    int code;
    /** RS_TitleScreen to land on, or -1 to leave the screen alone. */
    int screen;
    char line[RS_LOGIN_REPLY_LINES][RS_LOGIN_REPLY_LINE_LEN];
};

/** One `[string:<name>] text=` — a line the client knows when to say but not
 *  what to say. */
struct RS_LoginString
{
    char name[64];
    char text[RS_LOGIN_REPLY_LINE_LEN];
};

struct RS_LoginReplyTable
{
    struct RS_LoginReply* entries;
    int count;
    int capacity;
    /* [string:<name>] prose, in the same table because it comes from the same
     * sources and has the same lifetime. */
    struct RS_LoginString* strings;
    int string_count;
    int string_capacity;
};

/**
 * The text for `[string:<name>]`, or NULL when the profile declares none.
 *
 * NULL means the revision has nothing to say there, and the caller draws
 * nothing -- the standing undeclared-means-absent contract, not a failure.
 */
char const*
RS_LoginReplies_String(
    struct RS_LoginReplyTable const* table,
    char const* name);

void
RS_LoginReplies_Init(struct RS_LoginReplyTable* table);

void
RS_LoginReplies_Free(struct RS_LoginReplyTable* table);

/** Append every `[login_reply:…]` in `items`. Later sources extend earlier
 *  ones, and a repeated code replaces what the earlier source said. */
void
RS_LoginReplies_AddFromItems(
    struct RS_LoginReplyTable* table,
    struct RevConfigItemBuffer const* items);

/**
 * Load every `[login_reply:…]` from the three profile sources, in the same
 * order everything else reads them: shared files first, the boot manifest's
 * own inline sections last, so a lane can reword one code without copying the
 * whole table.
 */
void
RS_LoginReplies_LoadSources(
    struct RS_LoginReplyTable* table,
    char const* ui_ini,
    char const* cache_ini,
    char const* inline_ini);

/**
 * The entry for `code`, falling back to `[login_reply:default]`, or NULL when
 * the profile declares neither.
 *
 * NULL is a real answer and not a failure: a profile that says nothing about
 * login failures has none of this text, and the caller's job is then to say so
 * in a diagnostic rather than to invent a sentence.
 */
struct RS_LoginReply const*
RS_LoginReplies_Get(
    struct RS_LoginReplyTable const* table,
    int code);

#endif /* SRC_GAME_RS_LOGIN_REPLIES_H */
