/*
 * The CLIENT half of friends / ignore / private chat, as a test.
 *
 * Why this file exists. When the feature landed, the server half had two
 * permanent checks (`ToriRSServer --selftest`'s "the friend service" section and
 * `test-torirsserver-embed`'s social block) and the client half had **none** — the
 * store, the CS2 host ops and the friend-transmit repaint channel were proved
 * once, by hand, in a headless run, and nothing would have gone red if any of
 * them regressed. Every failure this feature actually hit was on the client
 * side, so that was the half without a net:
 *
 *   - `IF_SETONFRIENDTRANSMIT` (2420) sitting in the SETON *discard* group.
 *     The panel then paints once at mount and never again; the add still
 *     reaches the server, so the server-side tests stay green while the list
 *     the player sees stays empty.
 *   - `CC_SETONFRIENDTRANSMIT` (1420) missing from the CC discard group. Not a
 *     no-op: a signature-driven SETON op with no entry in the generated stack
 *     table reaches the meta stub, which *asserts* — the client dies.
 *   - `chat_sendprivate`'s two string pops in the wrong order, which addresses
 *     the message to its own body. Ten opcodes in this family had wrong or
 *     unknown stack shapes for exactly this reason.
 *   - `friend_getname` pushing one string instead of two, which shifts the
 *     string stack under clientscript 125 rather than failing where it happens.
 *
 * So the assertions below are chosen to be the ones those bugs break, and they
 * are driven through the real VM dispatch and the real RS_CS2Host_Exec rather
 * than by calling the handlers directly — a table edit that stops routing an
 * opcode is precisely the regression, and a direct call would not see it.
 *
 * No cache and no server: the provider is an empty dat2 build cache, because
 * nothing on this path reads one. That is deliberate — a gate that skips when
 * a cache is missing is a gate that eventually only ever skips.
 */

#include "cs2vm2/cs2_opcode.h"
#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_host.h"
#include "cs2vm2/cs2vm2_script.h"
#include "engine/dat2/dat2_buildcache.h"
#include "game/rs_cs2_host.h"
#include "game/rs_social.h"
#include "game/rs_ui_slots.h"
#include "inv/inv_manager.h"
#include "ui/uitree.h"

/* The generated stack-shape table, included for its own sake: the MANUAL_STACK
 * block in gen_opcode_stack.py is the only thing standing between this family
 * and the name-based guess that got it wrong, and a regeneration that loses it
 * would otherwise be invisible until a script desynced. */
#include "cs2vm2/cs2vm2_opcode_stack.gen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( cond )                                                                                 \
        {                                                                                          \
            printf("  ok   ");                                                                     \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL ");                                                                     \
            printf(__VA_ARGS__);                                                                   \
            printf("\n       (%s at %s:%d)\n", #cond, __FILE__, __LINE__);                         \
            g_fail++;                                                                              \
        }                                                                                          \
    } while( 0 )

/* ==========================================================================
 * Part 1 — the store (rs_social.c)
 * ========================================================================== */

static void
test_store(void)
{
    struct RS_Social social;
    char name[RS_SOCIAL_NAME_LEN];

    printf("social: the client's friend/ignore store\n");

    RS_Social_Init(&social);

    /*
     * -1 while the friend server has not said "loaded", the real count after.
     * Both directions matter and for different reasons: script 125 renders a
     * negative count as "Loading friends list", and script 681 refuses every
     * list mutation with "system busy" while it is negative — so an answer
     * that is negative *after* loading disables the panel's buttons, and one
     * that is non-negative *before* it paints an empty list as though the
     * server had answered.
     */
    social.server_status = RS_SOCIAL_SERVER_LOADING;
    CHECK(RS_Social_FriendCount(&social) < 0, "a friend list that has not loaded reads as negative");
    social.server_status = RS_SOCIAL_SERVER_CONNECTED;
    CHECK(RS_Social_FriendCount(&social) == 0, "and as its real count once it has");

    CHECK(RS_Social_AddFriend(&social, "bob", 1) == 1, "bob can be added");
    CHECK(RS_Social_FriendCount(&social) == 1, "and the list is one long");
    CHECK(RS_Social_FriendWorld(&social, 0) == 1, "carrying the world he is on");

    /*
     * One player, three spellings. Names reach this store lowercase from a
     * packet, capitalised from a chat line and with spaces from a typed
     * prompt; keyed by display string they were three players, and "Del
     * Friend" on a row the panel drew as "Bob" then deleted nothing.
     */
    CHECK(RS_Social_AddFriend(&social, "Bob", 1) == 0, "adding him again under another case does nothing");
    CHECK(RS_Social_FriendCount(&social) == 1, "so he is still one entry");
    CHECK(RS_Social_IsFriend(&social, "BOB"), "and any spelling finds him");

    RS_Social_FriendName(&social, 0, name, (int)sizeof(name));
    CHECK(strcmp(name, "Bob") == 0, "the panel is handed the display form, got \"%s\"", name);

    RS_Social_DisplayName("bob_smith", name, (int)sizeof(name));
    CHECK(strcmp(name, "Bob Smith") == 0, "underscores become spaces and words capitalise, got \"%s\"", name);

    /* Deletion closes the gap rather than swapping the tail in: the panel
     * draws the list in index order and a swap silently reorders it. */
    RS_Social_AddFriend(&social, "carol", 0);
    RS_Social_AddFriend(&social, "dave", 2);
    CHECK(RS_Social_DelFriend(&social, "Carol") == 1, "carol can be deleted under a different case");
    CHECK(RS_Social_FriendCount(&social) == 2, "leaving two");
    RS_Social_FriendName(&social, 1, name, (int)sizeof(name));
    CHECK(strcmp(name, "Dave") == 0, "and the gap closes rather than the tail being swapped in, got \"%s\"", name);

    CHECK(RS_Social_DelFriend(&social, "nobody") == 0, "deleting a stranger changes nothing");

    /* The ignore list is a separate list and must never answer negative —
     * script 129 reads `< 0` as "loading" and 681 refuses on it. */
    CHECK(RS_Social_IgnoreCount(&social) == 0, "the ignore list starts empty and non-negative");
    CHECK(RS_Social_AddIgnore(&social, "eve") == 1, "eve can be ignored");
    CHECK(RS_Social_IsIgnored(&social, "Eve"), "and is found under any spelling");
    CHECK(!RS_Social_IsFriend(&social, "eve"), "without joining the friend list");
    CHECK(RS_Social_IgnoreCount(&social) >= 0, "the ignore count is never negative");
}

/* ==========================================================================
 * Part 2 — VM dispatch, against a recording host
 *
 * What this isolates from part 3: whether the opcode reaches a handler at all,
 * and in what shape. A routing table that drops an opcode into a discard group
 * is invisible to anything that calls the handler directly.
 * ========================================================================== */

struct RecordingHost
{
    int calls;
    enum CS2VM_HostRequestKind kind;
    struct CS2VM_HostRequest_Social social;
    struct CS2VM_HostRequest_Chat chat;
    char seen_name[64];
    char seen_text[64];
};

static int
recording_host_exec(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest* request)
{
    struct RecordingHost* host = (struct RecordingHost*)thread->vm->user;

    host->calls++;
    host->kind = request->kind;
    if( request->kind == CS2VM_HOST_REQUEST_FRIEND_GETNAME ||
        request->kind == CS2VM_HOST_REQUEST_FRIEND_ADD )
    {
        host->social = request->u.social;
        snprintf(
            host->seen_name,
            sizeof(host->seen_name),
            "%s",
            request->u.social.name ? request->u.social.name : "");
    }
    if( request->kind == CS2VM_HOST_REQUEST_CHAT_SETFILTER ||
        request->kind == CS2VM_HOST_REQUEST_CHAT_SENDPUBLIC ||
        request->kind == CS2VM_HOST_REQUEST_CHAT_SENDPRIVATE )
    {
        host->chat = request->u.chat;
        snprintf(
            host->seen_name,
            sizeof(host->seen_name),
            "%s",
            request->u.chat.name ? request->u.chat.name : "");
        snprintf(
            host->seen_text,
            sizeof(host->seen_text),
            "%s",
            request->u.chat.text ? request->u.chat.text : "");
    }
    return CS2VM_EXECNO_OK;
}

/*
 * Assemble and run a one-opcode script. `pushes[i]` is an int constant unless
 * it is STR_PUSH, in which case `strs[str_cursor++]` goes on the string stack
 * instead — which is how the real scripts interleave the two stacks.
 */
#define STR_PUSH 0x7f000001

static void
run_op(
    struct RecordingHost* host,
    int op,
    int operand,
    int const* pushes,
    int push_count,
    char const* const* strs,
    int* out_top_int,
    int* out_int_depth)
{
    struct CS2VM2 vm;
    struct CS2VM2_Script script;
    int const op_count = push_count + 2;
    int str_cursor = 0;

    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, host, recording_host_exec);

    CS2VM2_ScriptInit(&script);
    script.script_id = 9995;
    script.op_count = op_count;
    script.opcodes = calloc((size_t)op_count, sizeof(uint16_t));
    script.int_operands = calloc((size_t)op_count, sizeof(int));
    script.string_operands = calloc((size_t)op_count, sizeof(char*));

    for( int i = 0; i < push_count; i++ )
    {
        if( pushes[i] == STR_PUSH )
        {
            script.opcodes[i] = (uint16_t)CS2_OP_PUSH_CONSTANT_STRING;
            script.string_operands[i] = strdup(strs && strs[str_cursor] ? strs[str_cursor] : "");
            str_cursor++;
            continue;
        }
        script.opcodes[i] = (uint16_t)CS2_OP_PUSH_CONSTANT_INT;
        script.int_operands[i] = pushes[i];
    }
    script.opcodes[push_count] = (uint16_t)op;
    script.int_operands[push_count] = operand;
    script.opcodes[push_count + 1] = (uint16_t)CS2_OP_RETURN;

    struct CS2VM2_Thread* thread = CS2VM2_ThreadMain(&vm);
    CS2VM2_PushCallScript(thread, &script);
    CS2VM2_RunScript(thread);

    if( out_int_depth )
        *out_int_depth = thread->ints_stack_top;
    if( out_top_int )
    {
        *out_top_int = 0;
        CS2VM2_PopInt(thread, out_top_int);
    }

    CS2VM2_Free(&vm);
    for( int i = 0; i < push_count; i++ )
        free(script.string_operands[i]);
    free(script.opcodes);
    free(script.int_operands);
    free(script.string_operands);
}

static void
test_vm_dispatch(void)
{
    printf("social: CS2 dispatch — the opcode reaches a handler, in the right shape\n");

    /* FRIEND_GETNAME(index): one int arg, and the host sees it. */
    {
        struct RecordingHost host;
        int const pushes[1] = { 3 };
        memset(&host, 0, sizeof(host));
        run_op(&host, CS2_OP_FRIEND_GETNAME, 0, pushes, 1, NULL, NULL, NULL);
        CHECK(host.calls == 1, "friend_getname reaches the host");
        CHECK(host.kind == CS2VM_HOST_REQUEST_FRIEND_GETNAME, "as a FRIEND_GETNAME request");
        CHECK(host.social.index == 3, "carrying the index it was given, got %d", host.social.index);
    }

    /* FRIEND_ADD(username): one string arg. */
    {
        struct RecordingHost host;
        int const pushes[1] = { STR_PUSH };
        char const* strs[1] = { "bob" };
        memset(&host, 0, sizeof(host));
        run_op(&host, CS2_OP_FRIEND_ADD, 0, pushes, 1, strs, NULL, NULL);
        CHECK(host.calls == 1, "friend_add reaches the host");
        CHECK(host.kind == CS2VM_HOST_REQUEST_FRIEND_ADD, "as a FRIEND_ADD request");
        CHECK(strcmp(host.seen_name, "bob") == 0, "with the name, got \"%s\"", host.seen_name);
    }

    /*
     * CHAT_SENDPRIVATE(username, mes) — the transposition test.
     *
     * Both arguments are strings, so a swapped pop order type-checks, runs,
     * and sends the message body to a player named after the message. The
     * host-op stage caught this by hand: `MESSAGE_PRIVATE to=hi_there "Guest"`.
     */
    {
        struct RecordingHost host;
        int const pushes[2] = { STR_PUSH, STR_PUSH };
        char const* strs[2] = { "bob", "hi there" };
        memset(&host, 0, sizeof(host));
        run_op(&host, CS2_OP_CHAT_SENDPRIVATE, 0, pushes, 2, strs, NULL, NULL);
        CHECK(host.calls == 1, "chat_sendprivate reaches the host");
        CHECK(host.kind == CS2VM_HOST_REQUEST_CHAT_SENDPRIVATE, "as a CHAT_SENDPRIVATE request");
        CHECK(strcmp(host.seen_name, "bob") == 0, "addressed to the first argument, got \"%s\"", host.seen_name);
        CHECK(strcmp(host.seen_text, "hi there") == 0, "with the second as the body, got \"%s\"", host.seen_text);
    }

    /*
     * CHAT_SENDPUBLIC(mes, colour_effect) — the mixed-arity twin of the test
     * above.
     *
     * The transposition the case above guards cannot happen here: ints and
     * strings are separate stacks, so a string argument and an int argument
     * never contend for a slot. What this pins instead is that the opcode is
     * read as one of EACH — copying its private twin's two-string shape pops a
     * string nobody pushed, and the submit aborts with the line still in the
     * box — and that the int lands in the field the outbound packet reads as
     * colour/effect rather than being dropped on the floor, which is how a
     * message would go out in the wrong colour with nothing to show for it.
     */
    {
        struct RecordingHost host;
        int const pushes[2] = { STR_PUSH, 0x0102 };
        char const* strs[1] = { "hi there" };
        memset(&host, 0, sizeof(host));
        run_op(&host, CS2_OP_CHAT_SENDPUBLIC, 0, pushes, 2, strs, NULL, NULL);
        CHECK(host.calls == 1, "chat_sendpublic reaches the host");
        CHECK(host.kind == CS2VM_HOST_REQUEST_CHAT_SENDPUBLIC, "as a CHAT_SENDPUBLIC request");
        CHECK(
            strcmp(host.seen_text, "hi there") == 0,
            "with the first argument as the body, got \"%s\"",
            host.seen_text);
        CHECK(
            host.chat.colour_effect == 0x0102,
            "and the second as the packed colour/effect, got 0x%X",
            host.chat.colour_effect);
    }

    /* CHAT_SETFILTER(public, private, trade) — three ints in source order. */
    {
        struct RecordingHost host;
        int const pushes[3] = { 1, 2, 0 };
        memset(&host, 0, sizeof(host));
        run_op(&host, CS2_OP_CHAT_SETFILTER, 0, pushes, 3, NULL, NULL, NULL);
        CHECK(host.calls == 1, "chat_setfilter reaches the host");
        CHECK(host.kind == CS2VM_HOST_REQUEST_CHAT_SETFILTER, "as a CHAT_SETFILTER request");
        CHECK(
            host.chat.public_mode == 1 && host.chat.private_mode == 2 && host.chat.trade_mode == 0,
            "with its three modes in source order, got %d/%d/%d",
            host.chat.public_mode,
            host.chat.private_mode,
            host.chat.trade_mode);
    }

    /*
     * IF_SETONFRIENDTRANSMIT (2420) — the load-bearing one.
     *
     * This registration IS the friends panel's whole reactive path. Put it
     * back in the SETON discard group and every server-side test stays green
     * while the list paints once at mount and never again. Stack shape:
     * int [scriptId, widgetUid], string [signature]; an empty signature takes
     * no further args.
     */
    {
        struct RecordingHost host;
        /* The uid and the script id are opaque here — the assertion is about
         * routing, not about which component or script. Deliberately not a real
         * interface id: this file resolves no names through the pack, so it may
         * not spell one either. */
        int const pushes[3] = { 0xC0DE, STR_PUSH, 0xBEEF };
        char const* strs[1] = { "" };
        memset(&host, 0, sizeof(host));
        run_op(&host, CS2_OP_IF_SETONFRIENDTRANSMIT, 0, pushes, 3, strs, NULL, NULL);
        CHECK(host.calls == 1, "if_setonfriendtransmit reaches the host rather than being discarded");
        CHECK(
            host.kind == CS2VM_HOST_REQUEST_IF_SETONFRIENDTRANSMIT,
            "as its own request kind, so the slot resolver can find the hook");
    }

    /*
     * CC_SETONFRIENDTRANSMIT (1420) — the twin, which is discarded on purpose.
     *
     * "Discarded" has to mean *parsed and dropped*, not "not routed": an
     * unrouted signature-driven SETON op falls through to the stack-meta stub,
     * which asserts and kills the client. The property asserted here is that
     * the op consumes exactly its own operands — a sentinel pushed underneath
     * must still be on the stack, and be the only thing left.
     *
     * A CC_* registration takes its component from the *operand* (active/dot),
     * not from the int stack, so unlike its IF twin there is no widget uid to
     * push: int [scriptId], string [signature].
     */
    {
        struct RecordingHost host;
        int const pushes[3] = { 0x5EED, 631, STR_PUSH };
        char const* strs[1] = { "" };
        int top = 0, depth = 0;
        memset(&host, 0, sizeof(host));
        run_op(&host, CS2_OP_CC_SETONFRIENDTRANSMIT, 0, pushes, 3, strs, &top, &depth);
        CHECK(depth == 1, "cc_setonfriendtransmit consumes exactly its own operands, %d left", depth);
        CHECK(top == 0x5EED, "leaving the sentinel below it untouched, got 0x%X", top);
        CHECK(
            host.kind == CS2VM_HOST_REQUEST_CC_SETONFRIENDTRANSMIT,
            "and retains its own request kind while the host drops it deliberately");
    }
}

/* ==========================================================================
 * Part 3 — the real host, over a real store
 * ========================================================================== */

static int
call_social(
    struct CS2VM2_Thread* t,
    int opcode,
    int index,
    char* name)
{
    struct CS2VM_HostRequest req;
    memset(&req, 0, sizeof(req));
    req.kind = (enum CS2VM_HostRequestKind)opcode;
    req.u.social.opcode = opcode;
    req.u.social.index = index;
    req.u.social.name = name;
    return RS_CS2Host_Exec(t, &req);
}

static void
test_host_ops(void)
{
    struct UITree* tree = UITree_New(256);
    struct Dat2BuildCache* bc = dat2_buildcache_new();
    struct CacheProvider* provider = dat2_buildcache_as_provider(bc);
    struct InvManager invs;
    struct RS_CS2Host host;
    struct RS_Social social;
    struct CS2VM2 vm;
    int filter_modes[3] = { 0, 0, 0 };
    int iv = 0;
    char* sv = NULL;
    char* sv2 = NULL;

    printf("social: the host ops answer from the store, and the mutators do both halves\n");

    InvManager_Init(&invs);
    RS_CS2Host_Init(&host, tree, provider, &invs, NULL, NULL, NULL);
    RS_Social_Init(&social);
    RS_Social_AddFriend(&social, "bob", 7);
    RS_CS2Host_SetSocial(&host, &social, filter_modes, 7);

    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, &host, RS_CS2Host_Exec);
    struct CS2VM2_Thread* t = CS2VM2_ThreadMain(&vm);

    /* friend_count: the value clientscript 125 branches on. */
    call_social(t, CS2_OP_FRIEND_COUNT, 0, NULL);
    CS2VM2_PopInt(t, &iv);
    CHECK(iv == 1, "friend_count answers the store, got %d", iv);

    social.server_status = RS_SOCIAL_SERVER_LOADING;
    call_social(t, CS2_OP_FRIEND_COUNT, 0, NULL);
    CS2VM2_PopInt(t, &iv);
    CHECK(iv < 0, "and answers negative while the friend server is still loading, got %d", iv);
    social.server_status = RS_SOCIAL_SERVER_CONNECTED;

    /*
     * friend_getname pushes TWO strings — the display name and the player's
     * previous name. Pushing one shifts clientscript 125's string stack and
     * fails somewhere else entirely, which is why it is asserted here.
     */
    call_social(t, CS2_OP_FRIEND_GETNAME, 0, NULL);
    CHECK(CS2VM2_PopStr(t, &sv) == CS2VM_EXECNO_OK, "friend_getname pushes a second string");
    CHECK(sv && sv[0] == '\0', "which is empty (there is no rename model)");
    CHECK(CS2VM2_PopStr(t, &sv2) == CS2VM_EXECNO_OK, "and a first");
    CHECK(sv2 && strcmp(sv2, "Bob") == 0, "which is the display name, got \"%s\"", sv2 ? sv2 : "(null)");

    /* map_world: the header reads "World N" off this, and a friend row is only
     * green when friend_getworld equals it. Stubbed to 0 it read "World 0". */
    {
        struct CS2VM_HostRequest req;
        memset(&req, 0, sizeof(req));
        req.kind = CS2VM_HOST_REQUEST_MAP_WORLD;
        RS_CS2Host_Exec(t, &req);
        CS2VM2_PopInt(t, &iv);
        CHECK(iv == 7, "map_world answers this client's world, got %d", iv);
    }

    call_social(t, CS2_OP_FRIEND_GETWORLD, 0, NULL);
    CS2VM2_PopInt(t, &iv);
    CHECK(iv == 7, "friend_getworld answers the stored world, got %d", iv);

    /*
     * The mutators do BOTH halves — the local store and the outbound packet.
     * Not redundant: the server answers a delete or an ignore-add with
     * nothing, so a client that only sent would show a deleted friend forever.
     * Drop either half and one of the two checks below goes red.
     */
    {
        struct RS_CS2SocialSend send;
        char name[] = "carol";

        host.friend_transmit_dirty = 0;
        call_social(t, CS2_OP_FRIEND_ADD, 0, name);
        CHECK(RS_Social_IsFriend(&social, "carol"), "friend_add writes the local store");
        CHECK(
            host.friend_transmit_dirty != 0,
            "and flags the repaint, without which the panel never redraws");
        CHECK(RS_CS2Host_TakeSocialSend(&host, &send), "and queues a packet for the server");
        CHECK(
            send.kind == RS_CS2_SOCIAL_SEND_FRIEND_ADD && strcmp(send.name, "carol") == 0,
            "naming the player it added, got kind=%d \"%s\"",
            send.kind,
            send.name);

        call_social(t, CS2_OP_FRIEND_DEL, 0, name);
        CHECK(!RS_Social_IsFriend(&social, "carol"), "friend_del drops her locally too");
        CHECK(RS_CS2Host_TakeSocialSend(&host, &send), "and queues the delete");
        CHECK(send.kind == RS_CS2_SOCIAL_SEND_FRIEND_DEL, "as a delete, got kind=%d", send.kind);
        CHECK(!RS_CS2Host_TakeSocialSend(&host, &send), "the queue drains empty");
    }

    /* chat_sendprivate through the real host: the queued packet must carry the
     * addressee and the body in the fields their names claim. */
    {
        struct CS2VM_HostRequest req;
        struct RS_CS2SocialSend send;
        char to[] = "bob";
        char body[] = "hi there";

        memset(&req, 0, sizeof(req));
        req.kind = CS2VM_HOST_REQUEST_CHAT_SENDPRIVATE;
        req.u.chat.opcode = CS2_OP_CHAT_SENDPRIVATE;
        req.u.chat.name = to;
        req.u.chat.text = body;
        RS_CS2Host_Exec(t, &req);

        CHECK(RS_CS2Host_TakeSocialSend(&host, &send), "chat_sendprivate queues a message");
        CHECK(
            send.kind == RS_CS2_SOCIAL_SEND_MESSAGE_PRIVATE,
            "as a private message, got kind=%d",
            send.kind);
        CHECK(strcmp(send.name, "bob") == 0, "to the addressee, got \"%s\"", send.name);
        CHECK(strcmp(send.text, "hi there") == 0, "carrying the body, got \"%s\"", send.text);
    }

    /* docheat through the real host: the chatbox's own "::foo" handler
     * (distinct from app.c's TORIRS_NET_CHEAT dev hook) must queue the raw
     * text, "::"-prefix already stripped by the caller script. */
    {
        struct CS2VM_HostRequest req;
        struct RS_CS2SocialSend send;
        char cheat[] = "tele 0,50,50";

        memset(&req, 0, sizeof(req));
        req.kind = CS2VM_HOST_REQUEST_DOCHEAT;
        req.u.chat.opcode = CS2_OP_DOCHEAT;
        req.u.chat.text = cheat;
        RS_CS2Host_Exec(t, &req);

        CHECK(RS_CS2Host_TakeSocialSend(&host, &send), "docheat queues a cheat send");
        CHECK(send.kind == RS_CS2_SOCIAL_SEND_CHEAT, "as a cheat, got kind=%d", send.kind);
        CHECK(strcmp(send.text, "tele 0,50,50") == 0, "carrying the text, got \"%s\"", send.text);
    }

    /* The chat modes are a pointer into RS_UISlots, not a copy: chat_setfilter
     * and the IF1 privacy bar are two writers of one value. */
    {
        struct CS2VM_HostRequest req;
        memset(&req, 0, sizeof(req));
        req.kind = CS2VM_HOST_REQUEST_CHAT_SETFILTER;
        req.u.chat.opcode = CS2_OP_CHAT_SETFILTER;
        req.u.chat.public_mode = 0;
        req.u.chat.private_mode = 2;
        req.u.chat.trade_mode = 1;
        RS_CS2Host_Exec(t, &req);
        CHECK(
            filter_modes[RS_UI_CHAT_FILTER_PRIVATE] == 2,
            "chat_setfilter writes through to the UI's own modes, got %d",
            filter_modes[RS_UI_CHAT_FILTER_PRIVATE]);
    }

    /* Notify is what the four social packets call; without it the store
     * changes and nothing repaints. */
    host.friend_transmit_dirty = 0;
    RS_CS2Host_NotifyFriendChanged(&host);
    CHECK(host.friend_transmit_dirty != 0, "NotifyFriendChanged raises the repaint flag");

    CS2VM2_Free(&vm);
    UITree_Free(tree);
    dat2_buildcache_free(bc);
}

/* ==========================================================================
 * Part 4 — the generated stack table
 *
 * gen_opcode_stack.py used to guess this family from the opcode *name*
 * ("FRIEND_*" means one int arg; "NAME" in the name means it returns a
 * string"). That guess was wrong for 23 opcodes. The MANUAL_STACK block that
 * replaced it is data in a script nothing else checks, so a regeneration that
 * loses it would be silent until a real clientscript desynced.
 * ========================================================================== */

struct StackWant
{
    int opcode;
    char const* label;
    int int_in, str_in, int_out, str_out;
};

static void
test_stack_table(void)
{
    static struct StackWant const wants[] = {
        { CS2_OP_FRIEND_COUNT, "friend_count() -> int", 0, 0, 1, 0 },
        { CS2_OP_FRIEND_GETNAME, "friend_getname(index) -> name, prev", 1, 0, 0, 2 },
        { CS2_OP_FRIEND_GETWORLD, "friend_getworld(index) -> world", 1, 0, 1, 0 },
        { CS2_OP_FRIEND_ADD, "friend_add(username)", 0, 1, 0, 0 },
        { CS2_OP_IGNORE_GETNAME, "ignore_getname(index) -> name, prev", 1, 0, 0, 2 },
        { CS2_OP_CHAT_SETFILTER, "chat_setfilter(public, private, trade)", 3, 0, 0, 0 },
        { CS2_OP_CHAT_SENDPRIVATE, "chat_sendprivate(username, mes)", 0, 2, 0, 0 },
        { CS2_OP_CHAT_SENDPUBLIC, "chat_sendpublic(mes, colour_effect)", 1, 1, 0, 0 },
        { CS2_OP_DOCHEAT, "docheat(text)", 0, 1, 0, 0 },
    };

    printf("social: the generated stack shapes (MANUAL_STACK, not the name guess)\n");

    for( size_t i = 0; i < sizeof(wants) / sizeof(wants[0]); i++ )
    {
        struct StackWant const* w = &wants[i];
        struct CS2VM2OpcodeStack const* got = &g_cs2vm2_opcode_stack[w->opcode];

        CHECK(got->known == 1, "%s has a stated shape", w->label);
        CHECK(
            got->int_in == w->int_in && got->str_in == w->str_in && got->int_out == w->int_out &&
                got->str_out == w->str_out,
            "%s is (%d,%d,%d,%d), got (%d,%d,%d,%d)",
            w->label,
            w->int_in,
            w->str_in,
            w->int_out,
            w->str_out,
            got->int_in,
            got->str_in,
            got->int_out,
            got->str_out);
    }
}

int
main(void)
{
    printf("TEST: friends / ignore / private chat — the client half\n");

    test_store();
    test_vm_dispatch();
    test_host_ops();
    test_stack_table();

    if( g_fail )
    {
        printf("social: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("social: all checks passed\n");
    return 0;
}
