/**
 * Id allocation (editor_alloc.h) against tools/ss_allocate.py.
 *
 *   editor_alloc_test <content-dir> <oracle-output> <oracle-dir>
 *
 * The oracle (editor_alloc_oracle.py) ran ss_allocate.py's own functions over
 * the same tree and wrote, per namespace, the floor, the base, the id and
 * spelling one fresh name gets, the authority and the layer sizes, plus the
 * ledger bytes write_pack would leave. This loads each namespace from the
 * tree, must reach every one of those numbers, and must produce the same
 * ledger bytes by APPLYING its commands to the ledger's text — the allocator
 * never writes a file.
 *
 * Then the rules on small layers: held ids are skipped, a bare var name its
 * prefixed spelling binds is known, a prefixed name keeps a free id and is
 * refused a held or sub-floor one, the varp ceiling holds, cache-owned ids
 * never go into the ledger, membership lines are not doubled, an append
 * larger than one splice is split, and the whole plan undoes as one stroke.
 *
 * Read-only over the content tree.
 */

#include "editor/test/editor_test_text.h"
#include "editor/editor_alloc.h"
#include "editor/editor_cfg.h"
#include "editor/editor_cmd.h"
#include "editor/editor_doc.h"
#include "mem/torirs_mem.h"
#include "torirsserver/torirs_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(
    EDITOR_ALLOC_VARP_CEILING
        == TORIRSSERVER_VARP_CACHE_MAX + TORIRSSERVER_VARP_SERVER_HEADROOM,
    "the editor's varp ceiling is the server's varp array");

static int g_checks;
static int g_failures;

static void
check(
    int condition,
    const char* what)
{
    g_checks++;
    if( !condition )
    {
        fprintf(stderr, "FAIL: %s\n", what);
        g_failures++;
    }
}

static char*
slurp(
    const char* path,
    size_t* out_size)
{
    FILE* f = fopen(path, "rb");
    long size;
    char* data;

    *out_size = 0;
    if( !f )
        return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    rewind(f);
    data = malloc((size_t)size + 1);
    if( !data )
    {
        fprintf(stderr, "out of memory\n");
        exit(2);
    }
    if( size > 0 && fread(data, 1, (size_t)size, f) != (size_t)size )
    {
        fclose(f);
        free(data);
        return NULL;
    }
    fclose(f);
    data[size] = '\0';
    *out_size = (size_t)size;
    editor_test_text_lf(data, out_size);
    return data;
}

static struct Editor_Doc g_doc;

/** Open TEXT document `key` holding `text` (NULL = absent, empty). */
static struct Editor_TextDoc*
open_text(
    const char* key,
    const char* text,
    size_t length)
{
    struct Editor_DocHandle handle;

    if( !Editor_DocOpen(&g_doc, EDITOR_DOC_TEXT, key, &handle) || !handle.text )
        return NULL;
    Editor_TextDocSet(handle.text, text ? text : "", text ? length : 0);
    return handle.text;
}

static int
apply_all(
    struct Editor_UndoStack* undo,
    const struct Editor_Cmd* commands,
    int count)
{
    int applied = 0;

    Editor_UndoStrokeBegin(undo);
    for( int i = 0; i < count; i++ )
    {
        if( Editor_CmdApply(&g_doc, &commands[i], EDITOR_CMD_FORWARD) )
        {
            Editor_UndoPush(undo, &commands[i]);
            applied++;
        }
    }
    Editor_UndoStrokeEnd(undo);
    return applied;
}

/* ---- against the script --------------------------------------------------- */

static void
test_against_oracle(
    const char* content_dir,
    const char* oracle_path,
    const char* oracle_dir)
{
    FILE* oracle = fopen(oracle_path, "r");
    char line[1024];
    int namespaces = 0;
    int ledgers = 0;

    check(oracle != NULL, "the oracle's output is readable");
    if( !oracle )
        return;

    while( fgets(line, sizeof(line), oracle) )
    {
        char ns[48];
        char name[128];
        char authority[32];
        int floor;
        int base;
        int next;
        int ledger_names;
        int other_names;
        int highest_other;
        struct Editor_AllocNs state;
        char what[256];

        if( sscanf(line,
                "%47s %d %d %d %127s %31s %d %d %d",
                ns,
                &floor,
                &base,
                &next,
                name,
                authority,
                &ledger_names,
                &other_names,
                &highest_other)
            != 9 )
            continue;
        namespaces++;

        check(Editor_AllocNsLoad(&state, content_dir, ns), "the namespace loads");
#define SAME(cond, label)                                                                          \
    snprintf(what, sizeof(what), "%s: %s", ns, label);                                             \
    check(cond, what)
        SAME(state.floor == floor, "floor matches ss_allocate.py's declared_base");
        SAME(state.ids_server == (strcmp(authority, "server") == 0), "ids authority matches");
        SAME(state.ledger.count == ledger_names, "ledger bindings match read_pack");
        SAME(state.other.count == other_names, "other-layer bindings match other_layers");
        SAME(state.highest_other == highest_other, "other layers' highest id matches");
        SAME(Editor_AllocNsBase(&state) == base, "base = max(mark, floor) matches");
        SAME(Editor_AllocNsNextId(&state) == next, "the next free id matches");
        if( state.floor != floor || Editor_AllocNsBase(&state) != base )
            fprintf(stderr, "  %s: C floor %d base %d next %d, script floor %d base %d next %d\n",
                ns, state.floor, Editor_AllocNsBase(&state), Editor_AllocNsNextId(&state), floor,
                base, next);

        if( state.ids_server && !state.not_allocatable )
        {
            /* The ledger path end to end: plan, apply to the ledger's text,
             * compare with write_pack's bytes. */
            char ledger_path[4096];
            char expect_path[4096];
            char key[EDITOR_DOC_KEY_MAX];
            size_t ledger_length;
            size_t expect_length;
            char* ledger;
            char* expect;
            struct Editor_AllocRequest request;
            struct Editor_AllocResult result;
            struct Editor_Cmd commands[8];
            struct Editor_UndoStack undo;
            struct Editor_TextDoc* text;

            snprintf(ledger_path, sizeof(ledger_path), "%s/pack/%s.alloc", content_dir, ns);
            snprintf(expect_path, sizeof(expect_path), "%s/%s.alloc", oracle_dir, ns);
            ledger = slurp(ledger_path, &ledger_length);
            expect = slurp(expect_path, &expect_length);

            memset(&request, 0, sizeof(request));
            request.ns = ns;
            request.name = "editor_alloc_probe";
            request.target = EDITOR_ALLOC_LEDGER;
            request.id_file.text = ledger;
            request.id_file.length = ledger_length;
            Editor_AllocPlan(&state, &request, commands, 8, &result);
            SAME(result.status == EDITOR_ALLOC_OK, "a fresh name is allocated");
            SAME(result.id == next && strcmp(result.name, name) == 0,
                "the id and spelling match the script's");

            memset(&g_doc, 0, sizeof(g_doc));
            Editor_DocInit(&g_doc, NULL);
            Editor_UndoInit(&undo);
            Editor_AllocDocKey(ns, "alloc", key, sizeof(key));
            text = open_text(key, ledger, ledger_length);
            SAME(text != NULL, "the ledger opens as a TEXT document");
            if( text )
            {
                SAME(apply_all(&undo, commands, result.command_count) == result.command_count,
                    "every command applies");
                SAME(expect && text->length == expect_length
                         && memcmp(text->text, expect, expect_length) == 0,
                    "the ledger after the commands is write_pack's, byte for byte");
                Editor_UndoUndo(&undo, &g_doc, NULL, NULL);
                SAME(text->length == ledger_length
                         && (ledger_length == 0 || memcmp(text->text, ledger, ledger_length) == 0),
                    "one undo takes the allocation back");
            }
            Editor_UndoFree(&undo);
            Editor_DocFree(&g_doc);
            free(ledger);
            free(expect);

            /* An empty ledger: header, marker, line. */
            snprintf(expect_path, sizeof(expect_path), "%s/%s.empty.alloc", oracle_dir, ns);
            expect = slurp(expect_path, &expect_length);
            {
                struct Editor_AllocText empty = { NULL, 0 };
                char bytes[4096];
                size_t length = Editor_AllocLedgerAppend(ns, &empty, next, name, bytes, sizeof(bytes));

                SAME(expect && length == expect_length && memcmp(bytes, expect, length) == 0,
                    "an empty ledger gets write_pack's header and marker");
            }
            free(expect);
            ledgers++;

            /* After one allocation the next name goes past it. */
            {
                int id;
                char coined[EDITOR_CMD_NAME_MAX];

                SAME(Editor_AllocNsCoin(&state, EDITOR_ALLOC_LEDGER, "editor_alloc_probe2", &id,
                         coined, sizeof(coined))
                         == EDITOR_ALLOC_OK,
                    "a second name allocates");
                SAME(id > next, "the second id is past the first");
                SAME(Editor_AllocNsCoin(&state, EDITOR_ALLOC_LEDGER, "editor_alloc_probe", &id,
                         coined, sizeof(coined))
                         == EDITOR_ALLOC_EXISTS
                         && id == next,
                    "the first name is now known");
            }
        }
        else
        {
            int id;
            char coined[EDITOR_CMD_NAME_MAX];

            SAME(Editor_AllocNsCoin(&state, EDITOR_ALLOC_LEDGER, "editor_alloc_probe", &id, coined,
                     sizeof(coined))
                     == EDITOR_ALLOC_REFUSED,
                "a cache-owned namespace is never allocated into the ledger");
            SAME(Editor_AllocNsCoin(&state, EDITOR_ALLOC_MEMBER_INDEX, "editor_alloc_probe", &id,
                     coined, sizeof(coined))
                     == EDITOR_ALLOC_OK
                     && id == next && strcmp(coined, name) == 0,
                "a new cache record takes the script's id in the member index");
        }
#undef SAME
        Editor_AllocNsFree(&state);
    }
    fclose(oracle);
    printf("  against ss_allocate.py: %d namespaces, %d ledgers appended\n", namespaces, ledgers);
    check(namespaces >= 15, "the oracle covered the namespaces");
}

/* ---- the rules on small layers -------------------------------------------- */

static void
add(
    void (*feed)(struct Editor_AllocNs*, const char*, size_t),
    struct Editor_AllocNs* state,
    const char* text)
{
    feed(state, text, strlen(text));
}

static void
test_rules(void)
{
    struct Editor_AllocNs state;
    char name[EDITOR_CMD_NAME_MAX];
    int id;

    /* Base and held ids. */
    Editor_AllocNsInit(&state, "enum", 100, 1);
    add(Editor_AllocNsAddMembers, &state, "// cache\n5=a\n7=b  // cache: b\n\n");
    add(Editor_AllocNsAddLedger, &state, "// header\n" EDITOR_ALLOC_MARKER "\n100=mine\n");
    add(Editor_AllocNsAddConfig, &state, "[enum_50]\nx=1\n [enum_60] \n[enum_x]\n");
    check(state.highest_other == 60, "numeric blocks raise the mark");
    check(Editor_AllocNsBase(&state) == 101, "base is past the ledger's highest");
    check(Editor_AllocNsLookup(&state, "b", &id, name, sizeof(name)) && id == 7,
        "a trailing // note is not part of the name");
    check(Editor_AllocNsCoin(&state, EDITOR_ALLOC_LEDGER, "fresh", &id, name, sizeof(name))
                  == EDITOR_ALLOC_OK
              && id == 101 && strcmp(name, "fresh") == 0,
        "a fresh name gets the base");
    check(Editor_AllocNsCoin(&state, EDITOR_ALLOC_LEDGER, "a", &id, name, sizeof(name))
                  == EDITOR_ALLOC_EXISTS
              && id == 5,
        "a name another layer binds exists");
    check(Editor_AllocNsCoin(&state, EDITOR_ALLOC_LEDGER, "bad name", &id, name, sizeof(name))
              == EDITOR_ALLOC_BAD_NAME,
        "a name a header cannot carry is refused");
    Editor_AllocNsFree(&state);

    /* Below the floor, the floor wins; held ids above the mark are skipped. */
    Editor_AllocNsInit(&state, "struct", 10, 1);
    add(Editor_AllocNsAddMembers, &state, "1=x\n");
    check(Editor_AllocNsBase(&state) == 10, "the floor beats a low mark");
    Editor_AllocNsFree(&state);

    /* Vars: prefixes, adoption, the ceiling. */
    Editor_AllocNsInit(&state, "varp", 6400, 1);
    add(Editor_AllocNsAddMembers, &state, "318=varp318_troll_varbit\n");
    add(Editor_AllocNsAddLedger, &state, "6400=varp6400_mine\n6405=varp6405_held\n");
    check(Editor_AllocNsLookup(&state, "troll_varbit", &id, name, sizeof(name)) && id == 318
              && strcmp(name, "varp318_troll_varbit") == 0,
        "a bare var name is known by its prefixed spelling");
    check(Editor_AllocNsCoin(&state, EDITOR_ALLOC_LEDGER, "counter", &id, name, sizeof(name))
                  == EDITOR_ALLOC_OK
              && id == 6406 && strcmp(name, "varp6406_counter") == 0,
        "a new var is coined varp<id>_<name>");
    check(Editor_AllocNsCoin(&state, EDITOR_ALLOC_LEDGER, "varp6402_kept", &id, name, sizeof(name))
                  == EDITOR_ALLOC_OK
              && id == 6402 && strcmp(name, "varp6402_kept") == 0,
        "a prefixed name keeps a free id");
    check(Editor_AllocNsCoin(&state, EDITOR_ALLOC_LEDGER, "varp6405_other", &id, name, sizeof(name))
              == EDITOR_ALLOC_ID_TAKEN,
        "a prefixed name is refused a held id");
    check(Editor_AllocNsCoin(&state, EDITOR_ALLOC_LEDGER, "varp100_low", &id, name, sizeof(name))
              == EDITOR_ALLOC_ID_TAKEN,
        "a prefixed name is refused an id below the floor");
    check(Editor_AllocNsCoin(&state, EDITOR_ALLOC_LEDGER, "varp7000_mine", &id, name, sizeof(name))
                  == EDITOR_ALLOC_EXISTS
              && id == 6400,
        "a prefixed name whose bare name is bound exists");
    check(Editor_AllocNsCoin(&state, EDITOR_ALLOC_LEDGER, "next", &id, name, sizeof(name))
                  == EDITOR_ALLOC_OK
              && id == 6407,
        "an adopted id does not move the counter");
    check(Editor_AllocNsCoin(&state, EDITOR_ALLOC_LEDGER, "varp9600_over", &id, name, sizeof(name))
              == EDITOR_ALLOC_FULL,
        "no varp at or past the server headroom");
    Editor_AllocNsFree(&state);

    Editor_AllocNsInit(&state, "varp", 6400, 1);
    add(Editor_AllocNsAddLedger, &state, "9599=varp9599_last\n");
    check(Editor_AllocNsCoin(&state, EDITOR_ALLOC_LEDGER, "one_more", &id, name, sizeof(name))
              == EDITOR_ALLOC_FULL,
        "a full headroom refuses the next varp");
    Editor_AllocNsFree(&state);

    /* Cache-owned ids. */
    Editor_AllocNsInit(&state, "varbit", 25000, 0);
    check(Editor_AllocNsCoin(&state, EDITOR_ALLOC_LEDGER, "x", &id, name, sizeof(name))
              == EDITOR_ALLOC_REFUSED,
        "ids = cache never goes into the ledger");
    check(Editor_AllocNsCoin(&state, EDITOR_ALLOC_MEMBER_INDEX, "x", &id, name, sizeof(name))
                  == EDITOR_ALLOC_OK
              && id == 25000 && strcmp(name, "varb25000_x") == 0,
        "a new varbit goes into the member index, prefixed");
    Editor_AllocNsFree(&state);

    Editor_AllocNsInit(&state, "component", 0, 0);
    check(Editor_AllocNsCoin(&state, EDITOR_ALLOC_MEMBER_INDEX, "x", &id, name, sizeof(name))
              == EDITOR_ALLOC_REFUSED,
        "a composed id is never counted out");
    Editor_AllocNsFree(&state);
}

static void
test_plan(void)
{
    struct Editor_AllocNs state;
    struct Editor_AllocRequest request;
    struct Editor_AllocResult result;
    struct Editor_Cmd commands[8];
    struct Editor_UndoStack undo;
    struct Editor_TextDoc* ledger;
    struct Editor_TextDoc* client;
    struct Editor_TextDoc* server;
    struct Editor_DocHandle record;
    const char* server_text = "// names\nvarp6400_mine\n";

    memset(&g_doc, 0, sizeof(g_doc));
    Editor_DocInit(&g_doc, NULL);
    Editor_UndoInit(&undo);

    Editor_AllocNsInit(&state, "varp", 6400, 1);
    add(Editor_AllocNsAddLedger, &state, "6400=varp6400_mine\n");

    /* Into an empty ledger: header + marker + line. */
    memset(&request, 0, sizeof(request));
    request.ns = "varp";
    request.name = "flag";
    request.target = EDITOR_ALLOC_LEDGER;
    request.member_client = 1;
    request.member_server = 1;
    request.add_record = 1;
    request.server_file.text = server_text;
    request.server_file.length = strlen(server_text);

    check(Editor_AllocPlan(&state, &request, commands, 1, &result) == EDITOR_ALLOC_NO_ROOM,
        "too few command slots is NO_ROOM");
    check(Editor_AllocNsNextId(&state) == 6401, "and the state did not move");

    check(Editor_AllocPlan(&state, &request, commands, 8, &result) == EDITOR_ALLOC_OK,
        "the plan is made");
    check(result.id == 6401 && strcmp(result.name, "varp6401_flag") == 0, "the coined var");
    check(result.command_count == 4, "the ledger, two membership lines, the record");
    check(commands[3].doc_kind == EDITOR_DOC_CFG && commands[3].kind == EDITOR_CMD_CFG_BLOCK
              && strcmp(commands[3].doc_key, "varp:varp6401_flag") == 0
              && commands[3].has_after && !commands[3].has_before,
        "the record is a CFG_BLOCK add for <ns>:<name>");

    ledger = open_text("pack:varp.alloc", NULL, 0);
    client = open_text("pack:varp.client", NULL, 0);
    server = open_text("pack:varp.server", server_text, strlen(server_text));
    check(ledger && client && server, "the ledger and membership files open as TEXT documents");
    check(Editor_DocOpen(&g_doc, EDITOR_DOC_CFG, "varp:varp6401_flag", &record) && record.text,
        "the new record opens empty");
    if( record.text )
        Editor_TextDocSet(record.text, "", 0);
    if( !ledger || !client || !server || !record.text )
        return;

    check(apply_all(&undo, commands, result.command_count) == 4, "the plan applies as one stroke");
    check(strstr(ledger->text, EDITOR_ALLOC_MARKER "\n6401=varp6401_flag\n") != NULL,
        "the ledger has the marker and the line");
    check(strcmp(client->text, "varp6401_flag\n") == 0, "the client membership line");
    check(strcmp(server->text, "// names\nvarp6400_mine\nvarp6401_flag\n") == 0,
        "the server membership line is appended");
    check(strcmp(record.text->text, "[varp6401_flag]\n") == 0, "the record's header");

    check(Editor_UndoUndo(&undo, &g_doc, NULL, NULL) == 4, "one undo takes the allocation back");
    check(ledger->length == 0 && client->length == 0 && record.text->length == 0
              && strcmp(server->text, server_text) == 0,
        "every file is as it was");

    /* An append past one splice is split at the bound and lands whole. */
    {
        char big[2500];
        struct Editor_AllocText current = { ledger->text, ledger->length };
        struct Editor_Cmd split[4];
        int count;

        for( size_t i = 0; i < sizeof(big); i++ )
            big[i] = (char)('a' + (int)(i % 26));
        count = Editor_AllocEmitAppend("pack:varp.alloc", &current, big, sizeof(big), split, 4);
        check(count == 3 && split[0].u.text_splice.after_len == EDITOR_CMD_SPLICE_MAX
                  && split[2].u.text_splice.after_len == sizeof(big) - 2 * EDITOR_CMD_SPLICE_MAX,
            "2500 bytes are three splices at the bound");
        check(Editor_AllocEmitAppend("pack:varp.alloc", &current, big, sizeof(big), split, 2) == -1,
            "too few slots for the split is -1");
        check(apply_all(&undo, split, count) == 3 && ledger->length == sizeof(big)
                  && memcmp(ledger->text, big, sizeof(big)) == 0,
            "the split append lands whole and in order");
        Editor_UndoUndo(&undo, &g_doc, NULL, NULL);
        check(ledger->length == 0, "and undoes as one stroke");
    }

    /* A name already in a membership file gets no second line. */
    request.name = "again";
    request.add_record = 0;
    request.member_client = 0;
    request.server_file.text = "varp6402_again\n";
    request.server_file.length = strlen(request.server_file.text);
    request.id_file.text = "x\n";
    request.id_file.length = 2;
    check(Editor_AllocPlan(&state, &request, commands, 8, &result) == EDITOR_ALLOC_OK
              && result.id == 6402,
        "a second plan");
    check(result.command_count == 1 && strcmp(commands[0].doc_key, "pack:varp.alloc") == 0,
        "only the ledger line: the membership line already exists");

    /* The member index for a cache record. */
    {
        struct Editor_AllocNs varbits;

        Editor_AllocNsInit(&varbits, "varbit", 25000, 0);
        memset(&request, 0, sizeof(request));
        request.ns = "varbit";
        request.name = "new_bit";
        request.target = EDITOR_ALLOC_MEMBER_INDEX;
        request.id_file.text = "24999=varb24999_x";
        request.id_file.length = strlen(request.id_file.text);
        check(Editor_AllocPlan(&varbits, &request, commands, 8, &result) == EDITOR_ALLOC_OK,
            "a varbit plan");
        check(result.command_count == 1 && strcmp(commands[0].doc_key, "compack:varbit") == 0
                  && commands[0].u.text_splice.after_len == strlen("\n25000=varb25000_new_bit\n")
                  && memcmp(commands[0].u.text_splice.text, "\n25000=varb25000_new_bit\n",
                         commands[0].u.text_splice.after_len)
                         == 0,
            "the member index gets the line, after the missing final newline");
        Editor_AllocNsFree(&varbits);
    }

    {
        char key[EDITOR_DOC_KEY_MAX];

        check(Editor_AllocDocKey("3_interfaces", "index", key, sizeof(key))
                  && strcmp(key, "pack:3_interfaces.pack") == 0,
            "a non-config namespace's member index is its pack");
        check(Editor_DocKeyValid(EDITOR_DOC_TEXT, "compack:loc"), "compack:<type> is a TEXT key");
        check(!Editor_DocKeyValid(EDITOR_DOC_TEXT, "pack:../x.alloc"), "a path in pack: is refused");
        check(!Editor_DocKeyValid(EDITOR_DOC_TEXT, "pack:varp.txt"), "an unknown pack file is refused");
    }

    Editor_AllocNsFree(&state);
    Editor_UndoFree(&undo);
    Editor_DocFree(&g_doc);
}

/* ---- copying a record (SAVE_AS's plan) ---------------------------------------- */

/** The name of the first block in `text` holding a line of at least
 *  `min_line` bytes (0: the first block), into `out`. */
static int
find_block(
    const char* text,
    size_t length,
    size_t min_line,
    char* out,
    size_t out_size)
{
    size_t at = 0;
    size_t header = (size_t)-1;

    while( at < length )
    {
        const char* newline = memchr(text + at, '\n', length - at);
        size_t stop = newline ? (size_t)(newline - text) : length;

        if( text[at] == '[' )
            header = at;
        if( header != (size_t)-1 && stop - at >= min_line )
        {
            const char* close = memchr(text + header, ']', stop - header);
            if( text[header] == '[' && close && (size_t)(close - text - header - 1) < out_size )
            {
                snprintf(out, out_size, "%.*s", (int)(close - text - header - 1), text + header + 1);
                return 1;
            }
        }
        at = newline ? stop + 1 : length;
    }
    return 0;
}

/** Copy `source_name` of `ns` from the real tree, through the plan and the
 *  documents, and hold every byte. */
static void
test_copy_one(
    const char* content_dir,
    const char* ns,
    size_t min_line)
{
    char path[4096];
    char name[EDITOR_CMD_NAME_MAX];
    char what[512];
    char ledger_key[EDITOR_DOC_KEY_MAX];
    char client_key[EDITOR_DOC_KEY_MAX];
    char record_key[EDITOR_DOC_KEY_MAX];
    const char* new_name = "editor_alloc_copy_probe";
    size_t config_length;
    size_t ledger_length;
    size_t client_length;
    size_t start;
    size_t end;
    char* config;
    char* ledger_file;
    char* client_file;
    char* expected;
    size_t expected_length;
    struct Editor_AllocNs state;
    struct Editor_AllocCopyRequest request;
    struct Editor_AllocResult result;
    struct Editor_CfgBlock source;
    struct Editor_Cmd* commands;
    struct Editor_UndoStack undo;
    struct Editor_TextDoc* ledger;
    struct Editor_TextDoc* client;
    struct Editor_DocHandle record;
    int capacity;
    int next_id;
    int splices = 0;
    int fields = 0;

    snprintf(path, sizeof(path), "%s/configs/all.%s", content_dir, ns);
    config = slurp(path, &config_length);
    snprintf(what, sizeof(what), "copy: configs/all.%s reads", ns);
    check(config != NULL, what);
    if( !config )
        return;
    snprintf(what, sizeof(what), "copy: all.%s has a block with a %d-byte line", ns, (int)min_line);
    check(find_block(config, config_length, min_line, name, sizeof(name)), what);
    check(Editor_CfgBlockExtent(config, config_length, name, &start, &end), "copy: the block's extent");
    snprintf(path, sizeof(path), "%s/pack/%s.alloc", content_dir, ns);
    ledger_file = slurp(path, &ledger_length);
    snprintf(path, sizeof(path), "%s/pack/%s.client", content_dir, ns);
    client_file = slurp(path, &client_length);

    memset(&g_doc, 0, sizeof(g_doc));
    Editor_DocInit(&g_doc, NULL);
    Editor_UndoInit(&undo);
    check(Editor_AllocNsLoad(&state, content_dir, ns), "copy: the namespace loads");
    next_id = Editor_AllocNsNextId(&state);

    memset(&request, 0, sizeof(request));
    request.ns = ns;
    request.source_name = name;
    request.source_text = config + start;
    request.source_length = end - start;
    request.new_name = new_name;
    request.target = EDITOR_ALLOC_LEDGER;
    request.member_client = 1;
    request.id_file.text = ledger_file;
    request.id_file.length = ledger_length;
    request.client_file.text = client_file;
    request.client_file.length = client_length;
    capacity = Editor_AllocCopyCommandBound(&request);
    commands = calloc((size_t)capacity, sizeof(*commands));
    if( !commands )
        exit(2);

    check(Editor_AllocCopyPlan(&state, &request, commands, 1, &result) == EDITOR_ALLOC_NO_ROOM
              && Editor_AllocNsNextId(&state) == next_id,
        "copy: too few slots is NO_ROOM, and nothing was coined");
    {
        struct Editor_AllocCopyRequest missing = request;
        missing.source_name = "not_this_block";
        check(Editor_AllocCopyPlan(&state, &missing, commands, capacity, &result) == EDITOR_ALLOC_REFUSED,
            "copy: a source text without that header is REFUSED");
    }
    {
        struct Editor_AllocCopyRequest taken = request;
        taken.new_name = name;
        check(Editor_AllocCopyPlan(&state, &taken, commands, capacity, &result) == EDITOR_ALLOC_EXISTS,
            "copy: onto a name the namespace binds is EXISTS");
    }
    snprintf(what, sizeof(what), "copy: %s:%s plans", ns, name);
    check(Editor_AllocCopyPlan(&state, &request, commands, capacity, &result) == EDITOR_ALLOC_OK, what);
    snprintf(what, sizeof(what), "copy: the copy gets the id a fresh name gets (%d, wanted %d)", result.id,
        next_id);
    check(result.id == next_id && strcmp(result.name, new_name) == 0, what);
    for( int i = 0; i < result.command_count; i++ )
    {
        if( commands[i].doc_kind != EDITOR_DOC_CFG )
            continue;
        splices += commands[i].kind == EDITOR_CMD_TEXT_SPLICE;
        fields += commands[i].kind == EDITOR_CMD_CFG_FIELD;
    }

    Editor_AllocDocKey(ns, "alloc", ledger_key, sizeof(ledger_key));
    Editor_AllocDocKey(ns, "client", client_key, sizeof(client_key));
    snprintf(record_key, sizeof(record_key), "%s:%s", ns, new_name);
    ledger = open_text(ledger_key, ledger_file, ledger_length);
    client = open_text(client_key, client_file, client_length);
    check(ledger && client && Editor_DocOpen(&g_doc, EDITOR_DOC_CFG, record_key, &record) && record.text,
        "copy: the ledger, the membership file and the new record open");
    if( !ledger || !client || !record.text )
        goto done;
    Editor_TextDocSet(record.text, "", 0);

    snprintf(what, sizeof(what), "copy: all %d commands apply as one stroke", result.command_count);
    check(apply_all(&undo, commands, result.command_count) == result.command_count, what);

    /* The record: "[new]\n" and every FIELD / OTHER line of the source. */
    Editor_CfgBlockInit(&source);
    Editor_CfgBlockSet(&source, config + start, end - start);
    expected = malloc((end - start) + EDITOR_CMD_NAME_MAX + 8);
    if( !expected )
        exit(2);
    expected_length = (size_t)sprintf(expected, "[%s]\n", new_name);
    for( int i = Editor_CfgBlockHeader(&source) + 1; i < source.line_count; i++ )
    {
        const struct Editor_CfgLine* line = &source.lines[i];
        if( line->kind != EDITOR_CFG_LINE_FIELD && line->kind != EDITOR_CFG_LINE_OTHER )
            continue;
        memcpy(expected + expected_length, source.text + line->start, line->length);
        expected_length += line->length;
        expected[expected_length++] = '\n';
    }
    snprintf(what, sizeof(what), "copy: [%s] is [%s]'s fields byte for byte (%d CFG_FIELD, %d splices)",
        new_name, name, fields, splices);
    check(record.text->length == expected_length && memcmp(record.text->text, expected, expected_length) == 0,
        what);
    if( min_line >= EDITOR_CMD_VALUE_MAX )
        check(splices > 0, "copy: a value past one CFG_FIELD went across as a splice");

    /* The ledger and the membership file: appended, nothing else touched. */
    {
        char bytes[4096];
        struct Editor_AllocText current = { ledger_file, ledger_length };
        size_t added = Editor_AllocLedgerAppend(ns, &current, result.id, new_name, bytes, sizeof(bytes));

        check(ledger->length == ledger_length + added
                  && (ledger_length == 0 || memcmp(ledger->text, ledger_file, ledger_length) == 0)
                  && memcmp(ledger->text + ledger_length, bytes, added) == 0,
            "copy: the ledger is its old bytes plus write_pack's append");
    }
    {
        int newline = client_length > 0 && client_file[client_length - 1] != '\n';
        size_t name_length = strlen(new_name);
        check(client->length == client_length + newline + name_length + 1
                  && memcmp(client->text, client_file, client_length) == 0
                  && memcmp(client->text + client->length - name_length - 1, new_name, name_length) == 0,
            "copy: the client membership file gains exactly the name's line");
    }

    /* Round trips through the codecs: the ledger reads back the id; the
     * record saved into all.<type> indexes, and the file around it is
     * untouched and re-emits byte for byte. */
    {
        struct Editor_AllocNs reread;
        int id = -1;

        Editor_AllocNsInit(&reread, ns, 0, 1);
        Editor_AllocNsAddLedger(&reread, ledger->text, ledger->length);
        check(Editor_AllocNsLookup(&reread, new_name, &id, NULL, 0) && id == result.id,
            "copy: the saved ledger binds the copy to its id");
        Editor_AllocNsFree(&reread);
    }
    {
        size_t merged_length = 0;
        char* merged = Editor_CfgFileReplaceBlock(
            config, config_length, new_name, record.text->text, record.text->length, &merged_length);
        struct Editor_CfgFile file;
        size_t at;
        size_t emitted = 0;
        int block;
        int round_trip = 1;

        Editor_CfgFileIndex(&file, merged, merged_length);
        block = Editor_CfgFileFind(&file, new_name);
        check(block >= 0 && file.blocks[block].end - file.blocks[block].start == record.text->length
                  && memcmp(merged + file.blocks[block].start, record.text->text, record.text->length) == 0,
            "copy: the record saved into all.<type> is found whole");
        check(memcmp(merged, config, config_length) == 0, "copy: every byte before it is the file's own");
        at = block >= 0 ? file.blocks[block].start : merged_length;
        while( at < merged_length )
        {
            struct Editor_CfgLine line;
            size_t next = Editor_CfgLineParse(merged, merged_length, at, &line);
            char buffer[2048];
            size_t written = Editor_CfgEmitLines(merged, &line, 1, NULL);

            if( written > sizeof(buffer) )
            {
                round_trip = 0;
                break;
            }
            Editor_CfgEmitLines(merged, &line, 1, buffer);
            round_trip &= written == next - at && memcmp(buffer, merged + at, written) == 0;
            emitted += written;
            at = next;
        }
        check(round_trip && block >= 0 && emitted == merged_length - file.blocks[block].start,
            "copy: the saved record re-emits byte for byte through the line codec");
        Editor_CfgFileFree(&file);
        ToriMem_Free(ToriMem_Editor(), merged);
    }

    check(Editor_UndoUndo(&undo, &g_doc, NULL, NULL) == result.command_count,
        "copy: one undo takes the whole copy back");
    check(ledger->length == ledger_length && client->length == client_length && record.text->length == 0
              && (ledger_length == 0 || memcmp(ledger->text, ledger_file, ledger_length) == 0)
              && memcmp(client->text, client_file, client_length) == 0,
        "copy: after the undo every document is its old bytes");

    free(expected);
    Editor_CfgBlockFree(&source);
done:
    free(commands);
    Editor_AllocNsFree(&state);
    Editor_UndoFree(&undo);
    Editor_DocFree(&g_doc);
    free(config);
    free(ledger_file);
    free(client_file);
}

static void
test_copy(const char* content_dir)
{
    test_copy_one(content_dir, "loc", 0);
    test_copy_one(content_dir, "npc", 0);
    /* An obj with a param past EDITOR_CMD_VALUE_MAX: the splice path. */
    test_copy_one(content_dir, "obj", EDITOR_CMD_VALUE_MAX + 8);
}

int
main(
    int argc,
    char** argv)
{
    test_rules();
    test_plan();
    if( argc > 1 )
        test_copy(argv[1]);
    if( argc > 3 )
        test_against_oracle(argv[1], argv[2], argv[3]);
    else
        check(0, "usage: editor_alloc_test <content-dir> <oracle-output> <oracle-dir>");
    printf("editor_alloc_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
