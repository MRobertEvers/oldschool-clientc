/*
 * The accounts module (torirsmaped/torirs_maped_accounts.h): the save codec
 * byte for byte, the quest arms parser, the stat table; then the requests
 * against a real ToriRSMapEd (raw in-process connections, the live test's
 * shape) over a small content tree written under the scratch dir, with a
 * lane beside it: list, create (refusals, the save, the registry), every
 * offline SET key, the quest queue, use, duplicate, delete; and the ONLINE
 * path against the python mock control-link peer (editor/live/test/
 * mock_control_peer.py): a `join` makes the account online, a SET becomes a
 * cheat the peer logs plus a checkpoint request, the queue flushes after the
 * login, a checkpoint file the test writes becomes the view's snapshot.
 *
 *   ./accounts_test <scratch> <peer-script> <repo-root>
 */

#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_accounts.h"
#include "torirsmaped/torirs_maped_build.h"
#include "torirsmaped/torirs_maped_catalog.h"
#include "torirsmaped/torirs_maped_live.h"
#include "platform/launch_sessions.h"
#include "torirsmaped/torirs_maped_build.h"

#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char** environ;

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
    assert(data);
    if( size > 0 && fread(data, 1, (size_t)size, f) != (size_t)size )
    {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    data[size] = '\0';
    *out_size = (size_t)size;
    return data;
}

static void
write_text(
    const char* path,
    const char* text)
{
    FILE* f = fopen(path, "wb");
    assert(f);
    fputs(text, f);
    fclose(f);
}

static void
mkdirs(const char* path)
{
    char buf[2048];
    size_t used = 0;
    for( const char* scan = path; *scan && used + 1 < sizeof(buf); scan++ )
    {
        if( *scan == '/' && used > 0 )
        {
            buf[used] = '\0';
            mkdir(buf, 0755);
        }
        buf[used++] = *scan;
    }
    buf[used] = '\0';
    mkdir(buf, 0755);
}

static int
file_has(
    const char* path,
    const char* needle)
{
    size_t size;
    char* text = slurp(path, &size);
    int found = text && strstr(text, needle) != NULL;
    free(text);
    return found;
}

/* ---- pure pieces ------------------------------------------------------------- */

static void
test_pure(void)
{
    char name[64];
    check(ToriRSMapEd_AcctStatFind("cooking") == 7 && ToriRSMapEd_AcctStatFind("7") == 7
              && ToriRSMapEd_AcctStatFind("Hitpoints") == 3 && ToriRSMapEd_AcctStatFind("nothing") == -1
              && ToriRSMapEd_AcctStatFind("25") == -1,
        "stats: names and ids resolve, unknown is -1");
    check(ToriRSMapEd_AcctXpForLevel(1) == 0 && ToriRSMapEd_AcctXpForLevel(2) == 83 && ToriRSMapEd_AcctXpForLevel(10) == 1154
              && ToriRSMapEd_AcctXpForLevel(50) == 101333 && ToriRSMapEd_AcctXpForLevel(99) == 13034431,
        "xp: the OSRS table (83, 1154, 101333, 13034431)");
    check(ToriRSMapEd_AcctLevelForXp(0) == 1 && ToriRSMapEd_AcctLevelForXp(83) == 2 && ToriRSMapEd_AcctLevelForXp(100000) == 49
              && ToriRSMapEd_AcctLevelForXp(200000000) == 99,
        "xp: levels from xp");
    ToriRSMapEd_AcctFileName("Big Bob-2", name, sizeof(name));
    check(strcmp(name, "big_bob_2") == 0, "file name: lower case, ' ' and '-' as '_'");
    ToriRSMapEd_AcctFileName("../x", name, sizeof(name));
    check(strcmp(name, "x") == 0, "file name: dots and slashes dropped");
    check(ToriRSMapEd_AcctCredentialsProblem("tester", "Secret123x") == NULL, "credentials: a good pair passes");
    check(ToriRSMapEd_AcctCredentialsProblem("", "Secret123x") != NULL, "credentials: an empty name is refused");
    check(ToriRSMapEd_AcctCredentialsProblem("thirteenchars", "Secret123x") != NULL, "credentials: 13 characters refused");
    check(ToriRSMapEd_AcctCredentialsProblem("tester", "short") != NULL, "credentials: a short password is refused");
    check(ToriRSMapEd_AcctCredentialsProblem("tester", "nodigitsatall") != NULL, "credentials: no digit is refused");
    check(ToriRSMapEd_AcctCredentialsProblem("tester", "tester1234") != NULL, "credentials: the name inside is refused");
}

static void
test_codec(void)
{
    struct ToriRSMapEdAcctSave save;
    static const char text[] = "; a save\n\n[player]\nname = bob\nx = 3200\n\n[stats]\n; <stat> = <boosted> <xp>\n7 = 50 1013330\n\n[varps]\n281 = 1000\n";
    char out[2048];
    char value[64];
    char key[64];
    size_t n;

    ToriRSMapEd_AcctSaveInit(&save);
    ToriRSMapEd_AcctSaveParse(&save, text, sizeof(text) - 1);
    n = ToriRSMapEd_AcctSaveEmit(&save, out, sizeof(out));
    check(n == sizeof(text) - 1 && strcmp(out, text) == 0, "codec: parse -> emit is byte-identical");
    check(ToriRSMapEd_AcctSaveGet(&save, "player", "x", value, sizeof(value)) && strcmp(value, "3200") == 0,
        "codec: Get reads a value");
    check(!ToriRSMapEd_AcctSaveGet(&save, "player", "z", value, sizeof(value)), "codec: a missing key is 0");
    check(!ToriRSMapEd_AcctSaveGet(&save, "nothing", "x", value, sizeof(value)), "codec: a missing section is 0");
    check(ToriRSMapEd_AcctSaveSet(&save, "player", "x", "3222") == 1 && !ToriRSMapEd_AcctSaveSet(&save, "player", "x", "3222"),
        "codec: Set changes once, then says no change");
    check(ToriRSMapEd_AcctSaveSet(&save, "player", "z", "3218") == 1, "codec: Set appends a new key");
    ToriRSMapEd_AcctSaveEmit(&save, out, sizeof(out));
    check(strstr(out, "[player]\nname = bob\nx = 3222\nz = 3218\n\n[stats]") != NULL,
        "codec: the new key lands before the section's blank line, the comment above stays");
    check(ToriRSMapEd_AcctSaveSet(&save, "inv", "3", "995 100") == 1, "codec: a missing section is made");
    ToriRSMapEd_AcctSaveEmit(&save, out, sizeof(out));
    check(strstr(out, "281 = 1000\n\n[inv]\n3 = 995 100\n") != NULL && out[strlen(out) - 1] == '\n',
        "codec: the new section ends the file after a blank line");
    check(ToriRSMapEd_AcctSaveRemove(&save, "varps", "281") == 1 && !ToriRSMapEd_AcctSaveRemove(&save, "varps", "281"),
        "codec: Remove takes the line once");
    check(ToriRSMapEd_AcctSaveNth(&save, "stats", 0, key, sizeof(key), value, sizeof(value)) && strcmp(key, "7") == 0
              && strcmp(value, "50 1013330") == 0 && !ToriRSMapEd_AcctSaveNth(&save, "stats", 1, key, sizeof(key), value, sizeof(value)),
        "codec: Nth skips the comment and counts keys");
    ToriRSMapEd_AcctSaveFree(&save);

    /* CRLF and no final newline survive. */
    {
        static const char crlf[] = "[player]\r\nx = 1\r\ny = 2";
        ToriRSMapEd_AcctSaveInit(&save);
        ToriRSMapEd_AcctSaveParse(&save, crlf, sizeof(crlf) - 1);
        n = ToriRSMapEd_AcctSaveEmit(&save, out, sizeof(out));
        check(n == sizeof(crlf) - 1 && strcmp(out, crlf) == 0, "codec: CRLF and a missing final newline come back");
        ToriRSMapEd_AcctSaveSet(&save, "player", "x", "5");
        ToriRSMapEd_AcctSaveEmit(&save, out, sizeof(out));
        check(strcmp(out, "[player]\r\nx = 5\r\ny = 2") == 0, "codec: an in-place edit keeps the style");
        ToriRSMapEd_AcctSaveFree(&save);
    }
    /* A section stated twice (a hand edit appended one) reads as one; a new
     * key goes to the last block. */
    {
        static const char twice[] = "[inv]\n0 = 1 1\n\n[bank]\n0 = 5 5\n\n[inv]\n3 = 995 100\n";
        ToriRSMapEd_AcctSaveInit(&save);
        ToriRSMapEd_AcctSaveParse(&save, twice, sizeof(twice) - 1);
        check(ToriRSMapEd_AcctSaveGet(&save, "inv", "3", value, sizeof(value)) && strcmp(value, "995 100") == 0,
            "codec: a key in a repeated section's second block is found");
        check(ToriRSMapEd_AcctSaveNth(&save, "inv", 1, key, sizeof(key), value, sizeof(value)) && strcmp(key, "3") == 0,
            "codec: Nth counts across the blocks");
        ToriRSMapEd_AcctSaveSet(&save, "inv", "0", "1 2");
        ToriRSMapEd_AcctSaveSet(&save, "inv", "7", "9 9");
        ToriRSMapEd_AcctSaveEmit(&save, out, sizeof(out));
        check(strcmp(out, "[inv]\n0 = 1 2\n\n[bank]\n0 = 5 5\n\n[inv]\n3 = 995 100\n7 = 9 9\n") == 0,
            "codec: the first block's key is rewritten in place, a new key lands in the last block");
        ToriRSMapEd_AcctSaveFree(&save);
    }
    /* An empty text. */
    ToriRSMapEd_AcctSaveInit(&save);
    ToriRSMapEd_AcctSaveParse(&save, "", 0);
    check(ToriRSMapEd_AcctSaveEmit(&save, out, sizeof(out)) == 0, "codec: empty in, empty out");
    ToriRSMapEd_AcctSaveSet(&save, "player", "name", "x");
    ToriRSMapEd_AcctSaveEmit(&save, out, sizeof(out));
    check(strcmp(out, "[player]\nname = x\n") == 0, "codec: the first section of an empty file has no blank line before it");
    ToriRSMapEd_AcctSaveFree(&save);
}

static void
test_quests(void)
{
    static const char arms[] =
        "[proc,quest_cheat_mark](dbrow $row, boolean $rewards)(int)\n"
        "// Cook's Assistant\n"
        "if ($row = quest_cooksassistant) {\n"
        "    if (%varp29_cookquest >= ^cook_complete) {\n"
        "        return(^questcheat_already);\n"
        "    }\n"
        "    %varp29_cookquest = ^cook_complete;\n"
        "    return(^questcheat_set);\n"
        "}\n"
        "if ($row = miniquest_bearyoursoul) {\n"
        "    if (%varb5078_arceuus_soulbearer_story >= ^bys_complete) {\n"
        "        return(^questcheat_already);\n"
        "    }\n"
        "}\n"
        "if ($row = quest_clocktower) {\n"
        "    if (~get_cog_progress >= ^quest_cog_complete) {\n"
        "        return(^questcheat_already);\n"
        "    }\n"
        "}\n";
    static const char rows[] = "[other_row]\ntable=speedrun\ndata=displayname,No\n\n[quest_cooksassistant]\ntable=quest\n"
                               "data=id,1\ndata=displayname,Cook's Assistant\ndata=endstate,2\n\n[quest_clocktower]\ntable=quest\n"
                               "data=displayname,Clock Tower\n";
    static const char constants[] = "// cook\n^cook_started = 1\n^cook_complete = 2\n^cook_completed_twice = 22\n";
    struct ToriRSMapEdAcctQuest quests[8];
    int count;
    int value = 0;

    count = ToriRSMapEd_AcctQuestArms(arms, sizeof(arms) - 1, quests, 8);
    check(count == 3, "arms: three arms parsed");
    check(count == 3 && strcmp(quests[0].dbrow, "quest_cooksassistant") == 0 && quests[0].var_kind == 1 && quests[0].var_id == 29
              && strcmp(quests[0].constant, "cook_complete") == 0,
        "arms: a varp arm names varp 29 and ^cook_complete");
    check(count == 3 && quests[1].var_kind == 2 && quests[1].var_id == 5078 && strcmp(quests[1].constant, "bys_complete") == 0,
        "arms: a varbit arm names varbit 5078");
    check(count == 3 && quests[2].var_kind == 0 && strcmp(quests[2].constant, "quest_cog_complete") == 0,
        "arms: a proc arm has no var but its constant");
    count = ToriRSMapEd_AcctQuestRows(rows, sizeof(rows) - 1, quests, 8);
    check(count == 2 && strcmp(quests[0].dbrow, "quest_cooksassistant") == 0 && strcmp(quests[0].display, "Cook's Assistant") == 0
              && strcmp(quests[1].display, "Clock Tower") == 0,
        "rows: table=quest blocks with their display names");
    check(ToriRSMapEd_AcctConstantFind(constants, sizeof(constants) - 1, "cook_complete", &value) && value == 2,
        "constants: ^cook_complete = 2 (not ^cook_completed_twice)");
    check(!ToriRSMapEd_AcctConstantFind(constants, sizeof(constants) - 1, "cook", &value), "constants: a prefix is not a match");
}

/* ---- one raw connection on the server ----------------------------------------- */

struct fact
{
    uint32_t type;
    uint32_t length;
    uint8_t* data;
};

struct conn
{
    struct ToriRSMapEdBuf to_server;
    struct ToriRSMapEdBuf to_client;
    struct ToriRSMapEdSession session;
    struct fact facts[4096];
    int fact_count;
    int mark;
};

static int
conn_recv(void* ctx, uint8_t* dst, int max)
{
    struct conn* conn = ctx;
    return ToriRSMapEd_BufRead(&conn->to_server, dst, max);
}

static int
conn_server_send(void* ctx, const uint8_t* src, int len)
{
    struct conn* conn = ctx;
    return ToriRSMapEd_BufWrite(&conn->to_client, src, len);
}

static void
conn_open(struct conn* conn, struct ToriRSMapEd* maped)
{
    struct ToriRSMapEdTransport transport;
    memset(conn, 0, sizeof(*conn));
    transport.ctx = conn;
    transport.recv = conn_recv;
    transport.send = conn_server_send;
    ToriRSMapEd_SessionInit(&conn->session, maped, &transport);
}

static void
conn_send(struct conn* conn, uint32_t type, struct ToriRSMapEdBuf* payload)
{
    uint8_t header[8];
    int const length = payload ? ToriRSMapEd_BufAvailable(payload) : 0;
    ToriRSMapEd_WriteU32(header, type);
    ToriRSMapEd_WriteU32(header + 4, (uint32_t)length);
    ToriRSMapEd_BufWrite(&conn->to_server, header, 8);
    if( length > 0 )
        ToriRSMapEd_BufWrite(&conn->to_server, ToriRSMapEd_BufPeek(payload), length);
    if( payload )
    {
        ToriRSMapEd_BufFree(payload);
        memset(payload, 0, sizeof(*payload));
    }
}

static void
conn_parse(struct conn* conn)
{
    while( ToriRSMapEd_BufAvailable(&conn->to_client) >= 8 )
    {
        const uint8_t* head = ToriRSMapEd_BufPeek(&conn->to_client);
        uint32_t type = ToriRSMapEd_ReadU32(head);
        uint32_t length = ToriRSMapEd_ReadU32(head + 4);
        struct fact* fact;
        if( ToriRSMapEd_BufAvailable(&conn->to_client) < 8 + (int)length )
            break;
        assert(conn->fact_count < 4096);
        fact = &conn->facts[conn->fact_count++];
        fact->type = type;
        fact->length = length;
        fact->data = malloc(length ? length : 1);
        assert(fact->data);
        memcpy(fact->data, head + 8, length);
        ToriRSMapEd_BufConsume(&conn->to_client, 8 + (int)length);
    }
}

static struct conn* g_conns[4];
static int g_conn_count;

static void
pump(void)
{
    for( int i = 0; i < g_conn_count; i++ )
    {
        ToriRSMapEd_SessionPump(&g_conns[i]->session);
        conn_parse(g_conns[i]);
    }
}

static int64_t
now_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int
find_fact(struct conn* conn, uint32_t type, int from)
{
    for( int i = from; i < conn->fact_count; i++ )
        if( conn->facts[i].type == type )
            return i;
    return -1;
}

static int
wait_fact(struct conn* conn, uint32_t type, int timeout_ms)
{
    int64_t const deadline = now_ms() + timeout_ms;
    for( ;; )
    {
        int at;
        pump();
        at = find_fact(conn, type, conn->mark);
        if( at >= 0 )
            return at;
        if( now_ms() > deadline )
            return -1;
        usleep(2000);
    }
}

static void
pump_for(int ms)
{
    int64_t const deadline = now_ms() + ms;
    while( now_ms() < deadline )
    {
        pump();
        usleep(2000);
    }
}

static void
mark(struct conn* conn)
{
    pump();
    conn->mark = conn->fact_count;
}

static void
hello(struct conn* conn)
{
    struct ToriRSMapEdBuf payload;
    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutU32(&payload, TORIRSMAPED_PROTO_VERSION);
    ToriRSMapEd_PutU32(&payload, TORIRSMAPED_ROLE_CONTROLLER);
    ToriRSMapEd_PutU32(&payload, 0);
    conn_send(conn, TORIRSMAPED_REQ_HELLO, &payload);
}

/* ---- decoded answers ----------------------------------------------------------- */

struct status_fact
{
    uint32_t request;
    uint32_t status;
    char account[64];
    char message[1024];
};

/** The first FACT_ACCOUNT_STATUS for `request` after the mark; -1 on timeout. */
static int
wait_status(struct conn* conn, uint32_t request, struct status_fact* out, int timeout_ms)
{
    int64_t const deadline = now_ms() + timeout_ms;
    int from = conn->mark;
    for( ;; )
    {
        int at;
        pump();
        at = find_fact(conn, TORIRSMAPED_FACT_ACCOUNT_STATUS, from);
        if( at >= 0 )
        {
            struct ToriRSMapEdReader reader;
            ToriRSMapEd_ReaderInit(&reader, conn->facts[at].data, conn->facts[at].length);
            out->request = ToriRSMapEd_GetU32(&reader);
            out->status = ToriRSMapEd_GetU32(&reader);
            ToriRSMapEd_GetString(&reader, out->account, sizeof(out->account));
            ToriRSMapEd_GetString(&reader, out->message, sizeof(out->message));
            if( out->request == request )
                return at;
            from = at + 1;
            continue;
        }
        if( now_ms() > deadline )
            return -1;
        usleep(2000);
    }
}

struct list_fact
{
    uint32_t status;
    char live[1024];
    char play[64];
    uint32_t attached;
    int count;
    char names[16][64];
    uint32_t flags[16];
    int32_t x[16];
    int32_t z[16];
    int32_t level[16];
    uint32_t queued[16];
};

static void
decode_list(const struct fact* fact, struct list_fact* out)
{
    struct ToriRSMapEdReader reader;
    uint32_t count;
    memset(out, 0, sizeof(*out));
    ToriRSMapEd_ReaderInit(&reader, fact->data, fact->length);
    out->status = ToriRSMapEd_GetU32(&reader);
    ToriRSMapEd_GetString(&reader, out->live, sizeof(out->live));
    ToriRSMapEd_GetString(&reader, out->play, sizeof(out->play));
    out->attached = ToriRSMapEd_GetU32(&reader);
    count = ToriRSMapEd_GetU32(&reader);
    for( uint32_t i = 0; i < count && i < 16 && !reader.bad; i++ )
    {
        ToriRSMapEd_GetString(&reader, out->names[i], sizeof(out->names[i]));
        out->flags[i] = ToriRSMapEd_GetU32(&reader);
        out->x[i] = (int32_t)ToriRSMapEd_GetU32(&reader);
        out->z[i] = (int32_t)ToriRSMapEd_GetU32(&reader);
        out->level[i] = (int32_t)ToriRSMapEd_GetU32(&reader);
        out->queued[i] = ToriRSMapEd_GetU32(&reader);
        out->count++;
    }
    assert(ToriRSMapEd_ReaderDone(&reader));
}

static int
list_find(const struct list_fact* list, const char* name)
{
    for( int i = 0; i < list->count; i++ )
        if( strcmp(list->names[i], name) == 0 )
            return i;
    return -1;
}

struct view_fact
{
    uint32_t status;
    char name[64];
    uint32_t flags;
    char status_text[256];
    char display[64];
    int32_t x;
    int32_t z;
    int32_t level;
    int32_t boosted[25];
    int32_t xp_tenths[25];
    int varp_count;
    uint32_t varp_id[64];
    int32_t varp_value[64];
    char varp_name[64][96];
    int inv_count;
    uint32_t inv_slot[28];
    int32_t inv_obj[28];
    int32_t inv_n[28];
    int worn_count;
    int bank_count;
    int queue_count;
    char queue[8][256];
    int quest_count;
    char quest[8][64];
    uint32_t quest_state[8];
    int32_t quest_value[8];
    int32_t worn_obj[14];
    uint32_t gender;
    int32_t kit[12];
    int32_t colour[5];
    int container_count;
    uint32_t container_inv[4];
    char container_name[4][64];
    uint32_t container_size[4];
    int container_items[4];
    uint32_t container_slot[4];
    int32_t container_obj[4];
};

static void
decode_view(const struct fact* fact, struct view_fact* out)
{
    struct ToriRSMapEdReader reader;
    uint32_t n;
    memset(out, 0, sizeof(*out));
    ToriRSMapEd_ReaderInit(&reader, fact->data, fact->length);
    out->status = ToriRSMapEd_GetU32(&reader);
    ToriRSMapEd_GetString(&reader, out->name, sizeof(out->name));
    if( out->status != EDITOR_HOST_OK )
        return;
    out->flags = ToriRSMapEd_GetU32(&reader);
    ToriRSMapEd_GetString(&reader, out->status_text, sizeof(out->status_text));
    ToriRSMapEd_GetString(&reader, out->display, sizeof(out->display));
    out->x = (int32_t)ToriRSMapEd_GetU32(&reader);
    out->z = (int32_t)ToriRSMapEd_GetU32(&reader);
    out->level = (int32_t)ToriRSMapEd_GetU32(&reader);
    n = ToriRSMapEd_GetU32(&reader);
    assert(n == 25);
    for( int i = 0; i < 25; i++ )
    {
        out->boosted[i] = (int32_t)ToriRSMapEd_GetU32(&reader);
        out->xp_tenths[i] = (int32_t)ToriRSMapEd_GetU32(&reader);
    }
    n = ToriRSMapEd_GetU32(&reader);
    for( uint32_t i = 0; i < n && !reader.bad; i++ )
    {
        uint32_t id = ToriRSMapEd_GetU32(&reader);
        int32_t value = (int32_t)ToriRSMapEd_GetU32(&reader);
        char name[96];
        ToriRSMapEd_GetString(&reader, name, sizeof(name));
        if( out->varp_count < 64 )
        {
            out->varp_id[out->varp_count] = id;
            out->varp_value[out->varp_count] = value;
            snprintf(out->varp_name[out->varp_count], 96, "%s", name);
            out->varp_count++;
        }
    }
    n = ToriRSMapEd_GetU32(&reader);
    for( uint32_t i = 0; i < n && !reader.bad; i++ )
    {
        uint32_t slot = ToriRSMapEd_GetU32(&reader);
        int32_t obj = (int32_t)ToriRSMapEd_GetU32(&reader);
        int32_t count = (int32_t)ToriRSMapEd_GetU32(&reader);
        if( out->inv_count < 28 )
        {
            out->inv_slot[out->inv_count] = slot;
            out->inv_obj[out->inv_count] = obj;
            out->inv_n[out->inv_count] = count;
            out->inv_count++;
        }
    }
    n = ToriRSMapEd_GetU32(&reader);
    out->worn_count = (int)n;
    for( int i = 0; i < 14; i++ )
        out->worn_obj[i] = -1;
    for( uint32_t i = 0; i < n && !reader.bad; i++ )
    {
        uint32_t slot = ToriRSMapEd_GetU32(&reader);
        int32_t obj = (int32_t)ToriRSMapEd_GetU32(&reader);
        ToriRSMapEd_GetU32(&reader);
        if( slot < 14 )
            out->worn_obj[slot] = obj;
    }
    n = ToriRSMapEd_GetU32(&reader);
    out->bank_count = (int)n;
    for( uint32_t i = 0; i < n && !reader.bad; i++ )
    {
        ToriRSMapEd_GetU32(&reader);
        ToriRSMapEd_GetU32(&reader);
        ToriRSMapEd_GetU32(&reader);
    }
    n = ToriRSMapEd_GetU32(&reader);
    out->queue_count = (int)n;
    for( uint32_t i = 0; i < n && !reader.bad; i++ )
    {
        char line[256];
        ToriRSMapEd_GetString(&reader, line, sizeof(line));
        if( i < 8 )
            snprintf(out->queue[i], sizeof(out->queue[i]), "%s", line);
    }
    n = ToriRSMapEd_GetU32(&reader);
    out->quest_count = (int)n;
    for( uint32_t i = 0; i < n && !reader.bad; i++ )
    {
        char dbrow[64];
        uint32_t state;
        int32_t value;
        ToriRSMapEd_GetString(&reader, dbrow, sizeof(dbrow));
        state = ToriRSMapEd_GetU32(&reader);
        value = (int32_t)ToriRSMapEd_GetU32(&reader);
        if( i < 8 )
        {
            snprintf(out->quest[i], sizeof(out->quest[i]), "%s", dbrow);
            out->quest_state[i] = state;
            out->quest_value[i] = value;
        }
    }
    out->gender = ToriRSMapEd_GetU32(&reader);
    n = ToriRSMapEd_GetU32(&reader);
    assert(n == 12);
    for( int i = 0; i < 12; i++ )
        out->kit[i] = (int32_t)ToriRSMapEd_GetU32(&reader);
    n = ToriRSMapEd_GetU32(&reader);
    assert(n == 5);
    for( int i = 0; i < 5; i++ )
        out->colour[i] = (int32_t)ToriRSMapEd_GetU32(&reader);
    n = ToriRSMapEd_GetU32(&reader);
    out->container_count = (int)n;
    for( uint32_t i = 0; i < n && !reader.bad; i++ )
    {
        uint32_t inv = ToriRSMapEd_GetU32(&reader);
        char name[64];
        uint32_t size;
        uint32_t items;
        ToriRSMapEd_GetString(&reader, name, sizeof(name));
        size = ToriRSMapEd_GetU32(&reader);
        items = ToriRSMapEd_GetU32(&reader);
        for( uint32_t k = 0; k < items && !reader.bad; k++ )
        {
            uint32_t slot = ToriRSMapEd_GetU32(&reader);
            int32_t obj = (int32_t)ToriRSMapEd_GetU32(&reader);
            ToriRSMapEd_GetU32(&reader);
            if( i < 4 && k == 0 )
            {
                out->container_slot[i] = slot;
                out->container_obj[i] = obj;
            }
        }
        if( i < 4 )
        {
            out->container_inv[i] = inv;
            snprintf(out->container_name[i], sizeof(out->container_name[i]), "%s", name);
            out->container_size[i] = size;
            out->container_items[i] = (int)items;
        }
    }
    assert(ToriRSMapEd_ReaderDone(&reader));
}

static int
view_quest(const struct view_fact* view, const char* dbrow)
{
    for( int i = 0; i < view->quest_count && i < 8; i++ )
        if( strcmp(view->quest[i], dbrow) == 0 )
            return i;
    return -1;
}

/** The newest FACT_ACCOUNT for `name` after the mark; -1 when none. */
static int
latest_view(struct conn* conn, const char* name, struct view_fact* out)
{
    int found = -1;
    pump();
    for( int i = conn->mark; i < conn->fact_count; i++ )
    {
        if( conn->facts[i].type != TORIRSMAPED_FACT_ACCOUNT )
            continue;
        decode_view(&conn->facts[i], out);
        if( strcmp(out->name, name) == 0 )
            found = i;
    }
    if( found >= 0 )
        decode_view(&conn->facts[found], out);
    return found;
}

struct loadouts_fact
{
    uint32_t status;
    int count;
    char names[8][64];
    int items[8];
};

static void
decode_loadouts(const struct fact* fact, struct loadouts_fact* out)
{
    struct ToriRSMapEdReader reader;
    uint32_t count;
    memset(out, 0, sizeof(*out));
    ToriRSMapEd_ReaderInit(&reader, fact->data, fact->length);
    out->status = ToriRSMapEd_GetU32(&reader);
    count = ToriRSMapEd_GetU32(&reader);
    for( uint32_t i = 0; i < count && !reader.bad; i++ )
    {
        char name[64];
        uint32_t items;
        ToriRSMapEd_GetString(&reader, name, sizeof(name));
        items = ToriRSMapEd_GetU32(&reader);
        for( uint32_t k = 0; k < items && !reader.bad; k++ )
        {
            ToriRSMapEd_GetU32(&reader);
            ToriRSMapEd_GetU32(&reader);
            ToriRSMapEd_GetU32(&reader);
            ToriRSMapEd_GetU32(&reader);
        }
        if( i < 8 )
        {
            snprintf(out->names[i], sizeof(out->names[i]), "%s", name);
            out->items[i] = (int)items;
        }
        out->count++;
    }
    assert(ToriRSMapEd_ReaderDone(&reader));
}

static void
send_set(struct conn* conn, const char* name, const char* key, const char* value)
{
    struct ToriRSMapEdBuf payload;
    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutString(&payload, name);
    ToriRSMapEd_PutString(&payload, key);
    ToriRSMapEd_PutString(&payload, value);
    mark(conn);
    conn_send(conn, TORIRSMAPED_REQ_ACCOUNT_SET, &payload);
}

/** SET and its status; the status fact's text in `out`. */
static int
set_ok(struct conn* conn, const char* name, const char* key, const char* value, struct status_fact* out)
{
    send_set(conn, name, key, value);
    if( wait_status(conn, TORIRSMAPED_REQ_ACCOUNT_SET, out, 3000) < 0 )
    {
        snprintf(out->message, sizeof(out->message), "(no status)");
        return 0;
    }
    return out->status == EDITOR_HOST_OK;
}

/* ---- the mock peer ---------------------------------------------------------------- */

static int
free_port(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    socklen_t length = sizeof(addr);
    int port;
    if( fd < 0 )
        return 0;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if( bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0 )
    {
        close(fd);
        return 0;
    }
    getsockname(fd, (struct sockaddr*)&addr, &length);
    port = ntohs(addr.sin_port);
    close(fd);
    return port;
}

static pid_t
spawn_peer(const char* script, int port)
{
    char port_text[16];
    char* argv[] = { (char*)"python3", (char*)script, port_text, NULL };
    pid_t pid;
    snprintf(port_text, sizeof(port_text), "%d", port);
    if( posix_spawnp(&pid, "python3", NULL, NULL, argv, environ) != 0 )
        return -1;
    return pid;
}

static char g_peer_log[1024];

static int
peer_count(const char* needle)
{
    size_t size;
    char* text = slurp(g_peer_log, &size);
    int count = 0;
    if( !text )
        return 0;
    for( char* at = text; (at = strstr(at, needle)); at++ )
        count++;
    free(text);
    return count;
}

static int
wait_peer(const char* needle, int at_least, int timeout_ms)
{
    int64_t const deadline = now_ms() + timeout_ms;
    for( ;; )
    {
        pump();
        if( peer_count(needle) >= at_least )
            return 1;
        if( now_ms() > deadline )
            return 0;
        usleep(5000);
    }
}

/* ---- the server tests ------------------------------------------------------------- */

static void
write_content(const char* content)
{
    char path[2048];
    mkdirs(content);
    snprintf(path, sizeof(path), "%s/configs", content);
    mkdirs(path);
    snprintf(path, sizeof(path), "%s/maps", content);
    mkdirs(path);
    snprintf(path, sizeof(path), "%s/pack", content);
    mkdirs(path);
    snprintf(path, sizeof(path), "%s/server/scripts/quests/scripts", content);
    mkdirs(path);
    snprintf(path, sizeof(path), "%s/server/scripts/quests/configs", content);
    mkdirs(path);
    snprintf(path, sizeof(path), "%s/configs/all.varp", content);
    write_text(path, "[varp29_cookquest]\nscope=perm\n\n[varp281_tutorial]\nscope=perm\n\n[varp695_millcheck_multi]\nscope=perm\n");
    snprintf(path, sizeof(path), "%s/configs/all.varp.compack", content);
    write_text(path, "29=varp29_cookquest\n281=varp281_tutorial\n695=varp695_millcheck_multi\n");
    snprintf(path, sizeof(path), "%s/configs/all.varbit", content);
    write_text(path, "[varb5078_arceuus_soulbearer_story]\nbasevar=varp695_millcheck_multi\nstartbit=30\nendbit=31\n");
    snprintf(path, sizeof(path), "%s/configs/all.varbit.compack", content);
    write_text(path, "5078=varb5078_arceuus_soulbearer_story\n");
    snprintf(path, sizeof(path), "%s/configs/all.obj", content);
    write_text(path, "[coins]\nname=Coins\nstackable=yes\n\n[bronze_sword]\nname=Bronze sword\n");
    snprintf(path, sizeof(path), "%s/configs/all.obj.compack", content);
    write_text(path, "995=coins\n1277=bronze_sword\n");
    snprintf(path, sizeof(path), "%s/configs/all.dbrow", content);
    write_text(path, "[quest_cooksassistant]\ntable=quest\ndata=id,1\ndata=displayname,Cook's Assistant\n\n"
                     "[miniquest_bearyoursoul]\ntable=quest\ndata=displayname,Bear Your Soul\n\n"
                     "[quest_clocktower]\ntable=quest\ndata=displayname,Clock Tower\n");
    snprintf(path, sizeof(path), "%s/server/scripts/quests/scripts/quest_cheat.rs2", content);
    write_text(path, "[proc,quest_cheat_mark](dbrow $row, boolean $rewards)(int)\n"
                     "if ($row = quest_cooksassistant) {\n    if (%varp29_cookquest >= ^cook_complete) {\n"
                     "        return(^questcheat_already);\n    }\n    %varp29_cookquest = ^cook_complete;\n}\n"
                     "if ($row = miniquest_bearyoursoul) {\n    if (%varb5078_arceuus_soulbearer_story >= ^bys_complete) {\n"
                     "        return(^questcheat_already);\n    }\n}\n"
                     "if ($row = quest_clocktower) {\n    if (~get_cog_progress >= ^quest_cog_complete) {\n"
                     "        return(^questcheat_already);\n    }\n}\n");
    snprintf(path, sizeof(path), "%s/server/scripts/quests/configs/quest_cook.constant", content);
    write_text(path, "^cook_started = 1\n^cook_complete = 2\n");
    snprintf(path, sizeof(path), "%s/server/scripts/quests/configs/bys.constant", content);
    write_text(path, "^bys_complete = 3\n^quest_cog_complete = 8\n");
    snprintf(path, sizeof(path), "%s/configs/all.inv", content);
    write_text(path, "// invs\n\n[inv]\nsize=28\nscope=default\n\n[worn]\nsize=14\n\n[bank]\nsize=1410\n\n[tob_chests]\nsize=12\n");
    snprintf(path, sizeof(path), "%s/configs/all.inv.compack", content);
    write_text(path, "93=inv\n94=worn\n95=bank\n612=tob_chests\n");
    snprintf(path, sizeof(path), "%s/server/scripts/player/configs", content);
    mkdirs(path);
    snprintf(path, sizeof(path), "%s/server/scripts/player/configs/appearance.enum", content);
    write_text(path, "// the default body\n[default_appearance]\ninputtype=int\noutputtype=int\ndefault=-1\n"
                     "val=4,18\nval=6,26\nval=7,36\nval=8,0\nval=9,33\nval=10,42\nval=11,10\n");
}

static void
test_server(const char* scratch, const char* peer_script, const char* repo_root)
{
    static struct ToriRSMapEd* maped;
    struct ToriRSMapEdLane lane;
    struct ToriRSMapEdLiveLink link;
    struct conn a;
    struct conn b;
    char content[1024];
    char live[1024];
    char path[2048];
    char fixture[2048];
    struct ToriRSMapEdBuf payload;
    struct status_fact status;
    struct list_fact list;
    struct view_fact view;
    int at;
    int port;
    pid_t peer = -1;

    snprintf(content, sizeof(content), "%s/content", scratch);
    snprintf(live, sizeof(live), "%s/live", scratch);
    write_content(content);
    mkdirs(live);
    snprintf(fixture, sizeof(fixture), "%s/fixture.ini", scratch);
    write_text(fixture, "; a fixture\n[player]\nversion = 1\nname = fixture\nx = 3206\nz = 3233\nlevel = 0\n\n[varps]\n; the tutorial\n281 = 1000\n\n[inv]\n0 = 1277 1\n");
    setenv("TORIRSMAPED_LIVE_DIR", "", 1);
    memset(&payload, 0, sizeof(payload));

    maped = calloc(1, sizeof(*maped));
    assert(maped);
    ToriRSMapEd_Open(maped, content, repo_root);
    check(maped->writable, "server: the scratch tree is writable");
    conn_open(&a, maped);
    conn_open(&b, maped);
    g_conns[g_conn_count++] = &a;
    g_conns[g_conn_count++] = &b;
    hello(&a);
    hello(&b);
    check(wait_fact(&a, TORIRSMAPED_FACT_HELLO, 2000) >= 0 && wait_fact(&b, TORIRSMAPED_FACT_HELLO, 2000) >= 0,
        "server: two connections said hello");

    /* No lane: every request answers UNSUPPORTED. */
    mark(&a);
    conn_send(&a, TORIRSMAPED_REQ_ACCOUNTS_LIST, NULL);
    at = wait_fact(&a, TORIRSMAPED_FACT_ACCOUNTS, 2000);
    check(at >= 0, "no lane: ACCOUNTS_LIST is answered");
    if( at >= 0 )
    {
        decode_list(&a.facts[at], &list);
        check(list.status == EDITOR_HOST_UNSUPPORTED && list.count == 0, "no lane: the list says UNSUPPORTED");
    }
    ToriRSMapEd_PutString(&payload, "x");
    ToriRSMapEd_PutString(&payload, "Secret123x");
    ToriRSMapEd_PutString(&payload, "");
    mark(&a);
    conn_send(&a, TORIRSMAPED_REQ_ACCOUNT_CREATE, &payload);
    check(wait_status(&a, TORIRSMAPED_REQ_ACCOUNT_CREATE, &status, 2000) >= 0 && status.status == EDITOR_HOST_UNSUPPORTED
              && strstr(status.message, "no lane"),
        "no lane: CREATE is refused and says why");

    /* The lane: a live dir beside the content, no binaries. */
    ToriRSMapEd_LaneDefaults(&lane, repo_root, content, live);
    lane.server_bin[0] = '\0';
    lane.play_bin[0] = '\0';
    lane.js5_bin[0] = '\0';
    snprintf(lane.play_fixture, sizeof(lane.play_fixture), "%s", fixture);
    check(ToriRSMapEd_SetLane(maped, &lane), "lane: bound");
    check(ToriRSMapEd_LiveLinkNative(&link, &lane), "lane: native link");
    ToriRSMapEd_SetLiveLink(maped, &link);
    /* The catalog over the scratch tree, built now: names and varbit
     * definitions answer at once. */
    ToriRSMapEdCatalog_Refresh(maped->catalog, 1, NULL);
    check(ToriRSMapEdCatalog_Built(maped->catalog), "catalog: built over the scratch tree");

    mark(&a);
    conn_send(&a, TORIRSMAPED_REQ_ACCOUNTS_LIST, NULL);
    at = wait_fact(&a, TORIRSMAPED_FACT_ACCOUNTS, 2000);
    check(at >= 0, "list: answered with a lane");
    if( at >= 0 )
    {
        decode_list(&a.facts[at], &list);
        check(list.status == EDITOR_HOST_OK && list.count == 0 && strcmp(list.play, "maped43620") == 0
                  && strstr(list.live, "/live") && !list.attached,
            "list: empty, the lane's default play account, not attached");
    }

    /* The play account before its first login: Place player seeds its save
     * from the lane's fixture and writes the position, instead of "no
     * save". Another account without a save is still ABSENT (below). */
    ToriRSMapEd_PutString(&payload, "maped43620");
    ToriRSMapEd_PutU32(&payload, 3100);
    ToriRSMapEd_PutU32(&payload, 3300);
    ToriRSMapEd_PutU32(&payload, 1);
    mark(&a);
    conn_send(&a, TORIRSMAPED_REQ_ACCOUNT_PLACE, &payload);
    snprintf(path, sizeof(path), "%s/saves/maped43620.ini", live);
    check(wait_status(&a, TORIRSMAPED_REQ_ACCOUNT_PLACE, &status, 2000) >= 0 && status.status == EDITOR_HOST_OK
              && strstr(status.message, "offline: wrote") && strstr(status.message, "first save, from the lane's fixture"),
        "place: the play account's first save is made from the fixture");
    check(file_has(path, "name = maped43620\n") && file_has(path, "x = 3100\nz = 3300\nlevel = 1\n")
              && file_has(path, "281 = 1000\n"),
        "place: the save carries the fixture's values, the account's name and the placed tile");
    ToriRSMapEd_PutString(&payload, "maped43620");
    ToriRSMapEd_PutU32(&payload, 3101);
    ToriRSMapEd_PutU32(&payload, 3301);
    ToriRSMapEd_PutU32(&payload, 0);
    mark(&a);
    conn_send(&a, TORIRSMAPED_REQ_ACCOUNT_PLACE, &payload);
    check(wait_status(&a, TORIRSMAPED_REQ_ACCOUNT_PLACE, &status, 2000) >= 0 && status.status == EDITOR_HOST_OK
              && !strstr(status.message, "fixture") && file_has(path, "x = 3101\nz = 3301\nlevel = 0\n"),
        "place: the second place edits the save it made");
    /* Gone again, password record included, so the registry below starts
     * from the fixture test's own account. */
    ToriRSMapEd_PutString(&payload, "maped43620");
    mark(&a);
    conn_send(&a, TORIRSMAPED_REQ_ACCOUNT_DELETE, &payload);
    check(wait_status(&a, TORIRSMAPED_REQ_ACCOUNT_DELETE, &status, 2000) >= 0 && status.status == EDITOR_HOST_OK
              && access(path, F_OK) != 0,
        "place: the seeded account deletes like any other");

    /* Create: refusals, then the account. */
    ToriRSMapEd_PutString(&payload, "tester");
    ToriRSMapEd_PutString(&payload, "short");
    ToriRSMapEd_PutString(&payload, "");
    mark(&a);
    conn_send(&a, TORIRSMAPED_REQ_ACCOUNT_CREATE, &payload);
    check(wait_status(&a, TORIRSMAPED_REQ_ACCOUNT_CREATE, &status, 2000) >= 0 && status.status == EDITOR_HOST_REFUSED
              && strstr(status.message, "8 to 20"),
        "create: a short password is refused with the rule");
    ToriRSMapEd_PutString(&payload, "Tester One");
    ToriRSMapEd_PutString(&payload, "Secret123x");
    ToriRSMapEd_PutString(&payload, "");
    mark(&a);
    mark(&b);
    conn_send(&a, TORIRSMAPED_REQ_ACCOUNT_CREATE, &payload);
    check(wait_status(&a, TORIRSMAPED_REQ_ACCOUNT_CREATE, &status, 2000) >= 0 && status.status == EDITOR_HOST_OK
              && strcmp(status.account, "tester_one") == 0,
        "create: Tester One becomes tester_one");
    snprintf(path, sizeof(path), "%s/saves/tester_one.ini", live);
    check(file_has(path, "name = Tester One\n") && file_has(path, "; the tutorial\n281 = 1000\n") && file_has(path, "0 = 1277 1\n"),
        "create: the save is the fixture with the account's name, comments kept");
    snprintf(path, sizeof(path), "%s/accounts.ini", live);
    check(file_has(path, "[passwords]\ntester_one = Secret123x\n"), "create: the registry holds the password");
    at = wait_fact(&b, TORIRSMAPED_FACT_ACCOUNTS, 2000);
    check(at >= 0, "create: the other connection hears FACT_ACCOUNTS");
    if( at >= 0 )
    {
        decode_list(&b.facts[at], &list);
        at = list_find(&list, "tester_one");
        check(at >= 0 && (list.flags[at] & TORIRSMAPED_ACCT_PASSWORD) && (list.flags[at] & TORIRSMAPED_ACCT_SAVE)
                  && !(list.flags[at] & TORIRSMAPED_ACCT_ONLINE) && list.x[at] == 3206 && list.z[at] == 3233,
            "create: the list row is offline at the fixture's tile with a known password");
    }
    check(wait_fact(&b, TORIRSMAPED_FACT_ACCOUNT, 2000) >= 0, "create: the view is broadcast too");
    ToriRSMapEd_PutString(&payload, "tester one");
    ToriRSMapEd_PutString(&payload, "Secret123x");
    ToriRSMapEd_PutString(&payload, "");
    mark(&a);
    conn_send(&a, TORIRSMAPED_REQ_ACCOUNT_CREATE, &payload);
    check(wait_status(&a, TORIRSMAPED_REQ_ACCOUNT_CREATE, &status, 2000) >= 0 && status.status == EDITOR_HOST_REFUSED
              && strstr(status.message, "already"),
        "create: the same name again (another spelling) is refused");

    /* Offline edits. */
    check(set_ok(&a, "tester_one", "player.pos", "3222 3218 1", &status) && strstr(status.message, "offline: wrote"),
        "set: player.pos writes the save");
    snprintf(path, sizeof(path), "%s/saves/tester_one.ini", live);
    check(file_has(path, "x = 3222\nz = 3218\nlevel = 1\n"), "set: x, z, level rewritten in place");
    check(!set_ok(&a, "tester_one", "player.pos", "3222 3218 9", &status) && strstr(status.message, "level 0..3"),
        "set: a bad level is refused");
    check(set_ok(&a, "tester_one", "stats.cooking", "50", &status) && file_has(path, "[stats]\n7 = 50 1013330\n"),
        "set: stats.cooking 50 writes boosted and xp_tenths");
    check(set_ok(&a, "tester_one", "stats.7.xp", "100000", &status) && file_has(path, "7 = 49 1000000\n"),
        "set: stats.7.xp writes the level the xp reaches");
    check(!set_ok(&a, "tester_one", "stats.cooking", "100", &status) && strstr(status.message, "1..99"), "set: level 100 refused");
    check(!set_ok(&a, "tester_one", "stats.strenght", "50", &status) && strstr(status.message, "no stat"), "set: a misspelt stat refused");
    check(set_ok(&a, "tester_one", "stats.all", "max", &status) && file_has(path, "0 = 99 130344310\n") && file_has(path, "24 = 99 130344310\n"),
        "set: stats.all max writes every stat");
    check(set_ok(&a, "tester_one", "stats.all", "reset", &status) && !file_has(path, "= 99 130344310"),
        "set: stats.all reset removes them");
    check(set_ok(&a, "tester_one", "varps.varp29_cookquest", "2", &status) && file_has(path, "29 = 2\n"),
        "set: a varp by its catalog name");
    check(set_ok(&a, "tester_one", "vars.cookquest", "1", &status) == 0 || file_has(path, "29 = 1\n"),
        "set: vars.<name> resolves through the catalog (or says it cannot)");
    check(set_ok(&a, "tester_one", "varps.281", "0", &status) && !file_has(path, "281 = "), "set: a varp of 0 removes its line");
    check(set_ok(&a, "tester_one", "varbits.5078", "3", &status) && file_has(path, "695 = -1073741824\n"),
        "set: a varbit by id lands in its base varp's bits (bits 30..31)");
    check(set_ok(&a, "tester_one", "varbits.varb5078_arceuus_soulbearer_story", "1", &status) && file_has(path, "695 = 1073741824\n"),
        "set: a varbit by name rewrites only its bits");
    check(!set_ok(&a, "tester_one", "varbits.5078", "4", &status) && strstr(status.message, "at most 3"),
        "set: a varbit value past its bits is refused");
    check(set_ok(&a, "tester_one", "inv.3", "coins 100", &status) && file_has(path, "3 = 995 100\n"),
        "set: inv.3 coins 100 by catalog name");
    check(set_ok(&a, "tester_one", "worn.3", "1277", &status) && file_has(path, "[worn]\n3 = 1277 1\n"),
        "set: worn.3 by id, count 1");
    check(set_ok(&a, "tester_one", "bank.5", "995 10000", &status) && file_has(path, "[bank]\n5 = 995 10000\n"),
        "set: bank.5");
    check(set_ok(&a, "tester_one", "inv.3", "-", &status) && !file_has(path, "3 = 995 100\n"), "set: inv.3 - removes the slot");
    check(!set_ok(&a, "tester_one", "inv.28", "995", &status) && strstr(status.message, "0..27"), "set: slot 28 refused");
    check(set_ok(&a, "tester_one", "inv.clear", "", &status) && !file_has(path, "0 = 1277 1\n"), "set: inv.clear empties the backpack");
    check(!set_ok(&a, "tester_one", "nothing.x", "1", &status) && strstr(status.message, "unknown key"), "set: an unknown key is refused");
    check(set_ok(&a, "tester_one", "varps.29", "1", &status), "set: varp 29 back to 1 for the view");

    /* The view. */
    ToriRSMapEd_PutString(&payload, "tester_one");
    mark(&a);
    conn_send(&a, TORIRSMAPED_REQ_ACCOUNT_VIEW, &payload);
    at = wait_fact(&a, TORIRSMAPED_FACT_ACCOUNT, 2000);
    check(at >= 0, "view: answered");
    if( at >= 0 )
    {
        int q;
        decode_view(&a.facts[at], &view);
        check(view.status == EDITOR_HOST_OK && view.x == 3222 && view.z == 3218 && view.level == 1
                  && strcmp(view.display, "Tester One") == 0 && strstr(view.status_text, "offline"),
            "view: position, display name and the offline status");
        check(view.boosted[3] == 10 && view.xp_tenths[3] == 11540 && view.boosted[7] == 1,
            "view: the fresh character's hitpoints 10, cooking back at 1 after reset");
        check(view.varp_count == 2 && view.varp_id[0] == 29 && view.varp_value[0] == 1
                  && strcmp(view.varp_name[0], "varp29_cookquest") == 0,
            "view: varps carry their catalog names");
        check(view.inv_count == 0 && view.worn_count == 1 && view.bank_count == 1, "view: the item sections");
        check(view.gender == 0 && view.kit[4] == 18 && view.kit[8] == 0 && view.kit[11] == 10 && view.kit[0] == -1
                  && view.kit[3] == -1 && view.colour[0] == 0,
            "view: a save without [appearance] wears the content's default kits");
        check(view.container_count == 0, "view: no other containers yet");
        q = view_quest(&view, "quest_cooksassistant");
        check(q >= 0 && view.quest_state[q] == TORIRSMAPED_QUEST_NOT_COMPLETE && view.quest_value[q] == 1,
            "view: Cook's Assistant not complete at varp 29 = 1");
        q = view_quest(&view, "miniquest_bearyoursoul");
        check(q >= 0 && view.quest_state[q] == TORIRSMAPED_QUEST_NOT_COMPLETE && view.quest_value[q] == 1,
            "view: Bear Your Soul reads its varbit (1 of 3)");
        q = view_quest(&view, "quest_clocktower");
        check(q >= 0 && view.quest_state[q] == TORIRSMAPED_QUEST_UNKNOWN, "view: a proc arm's quest is unknown");
    }
    check(set_ok(&a, "tester_one", "varps.29", "2", &status), "set: varp 29 = 2");
    at = latest_view(&a, "tester_one", &view);
    check(at >= 0 && view_quest(&view, "quest_cooksassistant") >= 0
              && view.quest_state[view_quest(&view, "quest_cooksassistant")] == TORIRSMAPED_QUEST_COMPLETE,
        "view: Cook's Assistant complete once varp 29 reaches 2 (the broadcast after SET)");

    /* Quests: the index, and the queue. */
    mark(&a);
    conn_send(&a, TORIRSMAPED_REQ_ACCOUNTS_QUESTS, NULL);
    at = wait_fact(&a, TORIRSMAPED_FACT_QUESTS, 2000);
    check(at >= 0, "quests: answered");
    if( at >= 0 )
    {
        struct ToriRSMapEdReader reader;
        uint32_t count;
        char dbrow[64];
        char display[64];
        char var[32];
        int32_t value;
        ToriRSMapEd_ReaderInit(&reader, a.facts[at].data, a.facts[at].length);
        count = ToriRSMapEd_GetU32(&reader);
        ToriRSMapEd_GetString(&reader, dbrow, sizeof(dbrow));
        ToriRSMapEd_GetString(&reader, display, sizeof(display));
        ToriRSMapEd_GetString(&reader, var, sizeof(var));
        value = (int32_t)ToriRSMapEd_GetU32(&reader);
        check(count == 3 && strcmp(dbrow, "quest_cooksassistant") == 0 && strcmp(display, "Cook's Assistant") == 0
                  && strcmp(var, "varp 29") == 0 && value == 2,
            "quests: three rows; Cook's Assistant is varp 29 >= 2");
    }
    check(set_ok(&a, "tester_one", "quests.quest_cooksassistant", "mark", &status) && strstr(status.message, "queued"),
        "quests: an offline mark is queued");
    check(set_ok(&a, "tester_one", "quests.quest_clocktower", "complete", &status) && strstr(status.message, "queued"),
        "quests: a complete is queued too");
    check(!set_ok(&a, "tester_one", "quests.quest_nothing", "mark", &status) && strstr(status.message, "no quest row"),
        "quests: an unknown row is refused");
    check(!set_ok(&a, "tester_one", "quests.all", "complete", &status) && strstr(status.message, "mark only"),
        "quests: all complete is refused");
    check(set_ok(&a, "tester_one", "cheat", "::give coins 5", &status) && strstr(status.message, "queued"),
        "cheat: offline, a raw cheat is queued");
    snprintf(path, sizeof(path), "%s/accounts.ini", live);
    check(file_has(path, "tester_one.0 = questmark quest_cooksassistant\n") && file_has(path, "tester_one.1 = complete quest_clocktower\n")
              && file_has(path, "tester_one.2 = give coins 5\n"),
        "queue: the registry holds the three lines in order");
    at = latest_view(&a, "tester_one", &view);
    check(at >= 0 && view.queue_count == 3 && strcmp(view.queue[2], "give coins 5") == 0, "queue: the view lists them");

    /* Use, duplicate, delete. */
    ToriRSMapEd_PutString(&payload, "tester_one");
    mark(&a);
    conn_send(&a, TORIRSMAPED_REQ_ACCOUNT_USE, &payload);
    check(wait_status(&a, TORIRSMAPED_REQ_ACCOUNT_USE, &status, 2000) >= 0 && status.status == EDITOR_HOST_OK,
        "use: tester_one becomes the play account");
    check(file_has(path, "[lane]\nplay_user = tester_one\nplay_pass = Secret123x\n"), "use: remembered in the registry");
    check(wait_fact(&a, TORIRSMAPED_FACT_LIVE_PLAY, 2000) >= 0, "use: FACT_LIVE_PLAY says so");
    at = wait_fact(&a, TORIRSMAPED_FACT_ACCOUNTS, 2000);
    if( at >= 0 )
    {
        decode_list(&a.facts[at], &list);
        at = list_find(&list, "tester_one");
        check(strcmp(list.play, "tester_one") == 0 && at >= 0 && (list.flags[at] & TORIRSMAPED_ACCT_PLAY),
            "use: the list flags it as the play account");
    }
    ToriRSMapEd_PutString(&payload, "nobody");
    mark(&a);
    conn_send(&a, TORIRSMAPED_REQ_ACCOUNT_USE, &payload);
    check(wait_status(&a, TORIRSMAPED_REQ_ACCOUNT_USE, &status, 2000) >= 0 && status.status == EDITOR_HOST_ABSENT,
        "use: an unknown account is ABSENT");
    ToriRSMapEd_PutString(&payload, "tester_one");
    ToriRSMapEd_PutString(&payload, "second");
    ToriRSMapEd_PutString(&payload, "Other9pass");
    mark(&a);
    conn_send(&a, TORIRSMAPED_REQ_ACCOUNT_DUPLICATE, &payload);
    check(wait_status(&a, TORIRSMAPED_REQ_ACCOUNT_DUPLICATE, &status, 2000) >= 0 && status.status == EDITOR_HOST_OK,
        "duplicate: second from tester_one");
    snprintf(path, sizeof(path), "%s/saves/second.ini", live);
    check(file_has(path, "name = second\n") && file_has(path, "29 = 2\n") && file_has(path, "5 = 995 10000\n"),
        "duplicate: the copy carries the values under its own name");
    ToriRSMapEd_PutString(&payload, "second");
    mark(&a);
    conn_send(&a, TORIRSMAPED_REQ_ACCOUNT_DELETE, &payload);
    check(wait_status(&a, TORIRSMAPED_REQ_ACCOUNT_DELETE, &status, 2000) >= 0 && status.status == EDITOR_HOST_OK
              && access(path, F_OK) != 0,
        "delete: second is gone");
    ToriRSMapEd_PutString(&payload, "second");
    mark(&a);
    conn_send(&a, TORIRSMAPED_REQ_ACCOUNT_DELETE, &payload);
    check(wait_status(&a, TORIRSMAPED_REQ_ACCOUNT_DELETE, &status, 2000) >= 0 && status.status == EDITOR_HOST_ABSENT,
        "delete: again is ABSENT");
    ToriRSMapEd_PutString(&payload, "nobody");
    mark(&a);
    conn_send(&a, TORIRSMAPED_REQ_ACCOUNT_VIEW, &payload);
    at = wait_fact(&a, TORIRSMAPED_FACT_ACCOUNT, 2000);
    check(at >= 0, "view: a missing account is answered");
    if( at >= 0 )
    {
        decode_view(&a.facts[at], &view);
        check(view.status == EDITOR_HOST_ABSENT && strcmp(view.name, "nobody") == 0, "view: with ABSENT");
    }
    ToriRSMapEd_PutString(&payload, "nobody");
    ToriRSMapEd_PutU32(&payload, 3200);
    ToriRSMapEd_PutU32(&payload, 3200);
    ToriRSMapEd_PutU32(&payload, 0);
    mark(&a);
    conn_send(&a, TORIRSMAPED_REQ_ACCOUNT_PLACE, &payload);
    check(wait_status(&a, TORIRSMAPED_REQ_ACCOUNT_PLACE, &status, 2000) >= 0 && status.status == EDITOR_HOST_ABSENT,
        "place: a missing account is ABSENT");
    ToriRSMapEd_PutString(&payload, "tester_one");
    ToriRSMapEd_PutU32(&payload, 3100);
    ToriRSMapEd_PutU32(&payload, 3300);
    ToriRSMapEd_PutU32(&payload, 2);
    mark(&a);
    conn_send(&a, TORIRSMAPED_REQ_ACCOUNT_PLACE, &payload);
    snprintf(path, sizeof(path), "%s/saves/tester_one.ini", live);
    check(wait_status(&a, TORIRSMAPED_REQ_ACCOUNT_PLACE, &status, 2000) >= 0 && status.status == EDITOR_HOST_OK
              && file_has(path, "x = 3100\nz = 3300\nlevel = 2\n"),
        "place: offline writes the save");

    /* ---- the look, the other containers, invs, loadouts (offline) --------------- */
    snprintf(path, sizeof(path), "%s/saves/tester_one.ini", live);
    {
        FILE* f = fopen(path, "a");
        assert(f);
        fputs("\n[appearance]\n; gender, then <wearpos> = <idk> and colours\ngender = 1\nkit8 = 5\ncolours = 1 2 3 4 5\n", f);
        fclose(f);
    }
    check(set_ok(&a, "tester_one", "container.tob_chests.2", "coins 50", &status) && file_has(path, "[container.612]\n2 = 995 50\n"),
        "container: a slot of tob_chests by name");
    check(!set_ok(&a, "tester_one", "container.612.12", "995", &status) && strstr(status.message, "0..11"),
        "container: the inv's size bounds the slot");
    check(!set_ok(&a, "tester_one", "container.nosuch.0", "995", &status) && strstr(status.message, "no inv"),
        "container: an unknown inv is refused");
    ToriRSMapEd_PutString(&payload, "tester_one");
    mark(&a);
    conn_send(&a, TORIRSMAPED_REQ_ACCOUNT_VIEW, &payload);
    at = wait_fact(&a, TORIRSMAPED_FACT_ACCOUNT, 2000);
    if( at >= 0 )
        decode_view(&a.facts[at], &view);
    check(at >= 0 && view.gender == 1 && view.kit[8] == 5 && view.kit[4] == 18 && view.colour[4] == 5,
        "look: the save's gender, its own hair kit, the rest the defaults, its colours");
    check(at >= 0 && view.container_count == 1 && view.container_inv[0] == 612
              && strcmp(view.container_name[0], "tob_chests") == 0 && view.container_size[0] == 12
              && view.container_items[0] == 1 && view.container_slot[0] == 2 && view.container_obj[0] == 995,
        "view: the container with its name, size and item");
    check(set_ok(&a, "tester_one", "container.612.clear", "", &status) && !file_has(path, "2 = 995 50\n"),
        "container: clear empties it");
    mark(&a);
    conn_send(&a, TORIRSMAPED_REQ_ACCOUNTS_INVS, NULL);
    at = wait_fact(&a, TORIRSMAPED_FACT_INVS, 2000);
    {
        int found = 0;
        uint32_t count = 0;
        if( at >= 0 )
        {
            struct ToriRSMapEdReader reader;
            ToriRSMapEd_ReaderInit(&reader, a.facts[at].data, a.facts[at].length);
            count = ToriRSMapEd_GetU32(&reader);
            for( uint32_t i = 0; i < count && !reader.bad; i++ )
            {
                uint32_t id = ToriRSMapEd_GetU32(&reader);
                char name[64];
                uint32_t size;
                ToriRSMapEd_GetString(&reader, name, sizeof(name));
                size = ToriRSMapEd_GetU32(&reader);
                found |= id == 612 && strcmp(name, "tob_chests") == 0 && size == 12;
            }
        }
        check(at >= 0 && count == 4 && found, "invs: every inv with its name and size");
    }
    /* Loadouts: save from the account, list, apply, replace, delete. */
    check(set_ok(&a, "tester_one", "inv.0", "coins 7", &status), "loadout: a backpack item to save");
    ToriRSMapEd_PutString(&payload, "Melee Set");
    ToriRSMapEd_PutString(&payload, "tester_one");
    mark(&a);
    mark(&b);
    conn_send(&a, TORIRSMAPED_REQ_LOADOUT_SAVE, &payload);
    check(wait_status(&a, TORIRSMAPED_REQ_LOADOUT_SAVE, &status, 2000) >= 0 && status.status == EDITOR_HOST_OK
              && strstr(status.message, "loadout melee_set saved") && strstr(status.message, "1 worn, 1 in the backpack"),
        "loadout: saved under its kept name");
    snprintf(path, sizeof(path), "%s/accounts.ini", live);
    check(file_has(path, "[loadout.melee_set]\nworn.3 = 1277 1\ninv.0 = 995 7\n"), "loadout: the registry holds it");
    at = wait_fact(&b, TORIRSMAPED_FACT_LOADOUTS, 2000);
    {
        struct loadouts_fact loadouts;
        if( at >= 0 )
            decode_loadouts(&b.facts[at], &loadouts);
        check(at >= 0 && loadouts.status == EDITOR_HOST_OK && loadouts.count == 1
                  && strcmp(loadouts.names[0], "melee_set") == 0 && loadouts.items[0] == 2,
            "loadout: every connection hears the list");
    }
    snprintf(path, sizeof(path), "%s/saves/tester_one.ini", live);
    check(set_ok(&a, "tester_one", "worn.3", "-", &status) && set_ok(&a, "tester_one", "inv.clear", "", &status)
              && set_ok(&a, "tester_one", "inv.5", "995 1", &status),
        "loadout: the account changed since");
    check(set_ok(&a, "tester_one", "loadout", "melee set", &status) && strstr(status.message, "loadout melee_set: 1 worn, 1 in the backpack")
              && file_has(path, "3 = 1277 1\n") && file_has(path, "0 = 995 7\n") && !file_has(path, "5 = 995 1\n"),
        "loadout: applying it offline replaces worn and backpack");
    check(!set_ok(&a, "tester_one", "loadout", "nothing", &status) && strstr(status.message, "no loadout"),
        "loadout: an unknown one is refused");
    ToriRSMapEd_PutString(&payload, "melee_set");
    ToriRSMapEd_PutString(&payload, "tester_one");
    mark(&a);
    conn_send(&a, TORIRSMAPED_REQ_LOADOUT_SAVE, &payload);
    check(wait_status(&a, TORIRSMAPED_REQ_LOADOUT_SAVE, &status, 2000) >= 0 && strstr(status.message, "replaced"),
        "loadout: saving the same name replaces it");
    ToriRSMapEd_PutString(&payload, "!!");
    ToriRSMapEd_PutString(&payload, "tester_one");
    mark(&a);
    conn_send(&a, TORIRSMAPED_REQ_LOADOUT_SAVE, &payload);
    check(wait_status(&a, TORIRSMAPED_REQ_LOADOUT_SAVE, &status, 2000) >= 0 && status.status == EDITOR_HOST_REFUSED,
        "loadout: a name with no letters is refused");
    ToriRSMapEd_PutString(&payload, "spare");
    ToriRSMapEd_PutString(&payload, "tester_one");
    mark(&a);
    conn_send(&a, TORIRSMAPED_REQ_LOADOUT_SAVE, &payload);
    (void)wait_status(&a, TORIRSMAPED_REQ_LOADOUT_SAVE, &status, 2000);
    ToriRSMapEd_PutString(&payload, "spare");
    mark(&a);
    conn_send(&a, TORIRSMAPED_REQ_LOADOUT_DELETE, &payload);
    snprintf(path, sizeof(path), "%s/accounts.ini", live);
    check(wait_status(&a, TORIRSMAPED_REQ_LOADOUT_DELETE, &status, 2000) >= 0 && status.status == EDITOR_HOST_OK
              && !file_has(path, "[loadout.spare]") && file_has(path, "[loadout.melee_set]"),
        "loadout: delete removes only that one");
    ToriRSMapEd_PutString(&payload, "spare");
    mark(&a);
    conn_send(&a, TORIRSMAPED_REQ_LOADOUT_DELETE, &payload);
    check(wait_status(&a, TORIRSMAPED_REQ_LOADOUT_DELETE, &status, 2000) >= 0 && status.status == EDITOR_HOST_ABSENT,
        "loadout: deleting it again is ABSENT");
    snprintf(path, sizeof(path), "%s/saves/tester_one.ini", live);
    check(set_ok(&a, "tester_one", "varps.29", "2", &status) && set_ok(&a, "tester_one", "quests.quest_cooksassistant", "reset", &status)
              && strstr(status.message, "reset") && !file_has(path, "29 = 2\n"),
        "quests: reset puts the quest's varp back to 0 (offline)");
    check(!set_ok(&a, "tester_one", "quests.quest_clocktower", "reset", &status) && strstr(status.message, "proc"),
        "quests: a proc arm cannot be reset");
    check(set_ok(&a, "tester_one", "varps.29", "1", &status), "quests: varp 29 back to 1");

    /* ---- online, against the mock peer ------------------------------------------ */
    check(set_ok(&a, "tester_one", "stats.cooking", "50", &status), "set: cooking 50 before going online");
    port = free_port();
    check(port > 0, "peer: a free port");
    snprintf(g_peer_log, sizeof(g_peer_log), "%s/peer.log", scratch);
    setenv("MOCK_PEER_LOG", g_peer_log, 1);
    unsetenv("MOCK_PEER_TICKET");
    unlink(g_peer_log);
    peer = spawn_peer(peer_script, port);
    check(peer > 0, "peer: started");
    check(wait_peer("\"listening\"", 1, 10000), "peer: listens");
    {
        char address[64];
        snprintf(address, sizeof(address), "127.0.0.1:%d", port);
        ToriRSMapEd_PutU32(&payload, 0);
        ToriRSMapEd_PutString(&payload, address);
        ToriRSMapEd_PutString(&payload, "");
        mark(&a);
        conn_send(&a, TORIRSMAPED_REQ_LIVE_ATTACH, &payload);
    }
    {
        int64_t const deadline = now_ms() + 10000;
        while( now_ms() < deadline )
        {
            pump();
            /* Attached: the live status says so. */
            {
                int s = find_fact(&a, TORIRSMAPED_FACT_LIVE_STATUS, a.mark);
                int attached = 0;
                for( ; s >= 0 && s < a.fact_count; s = find_fact(&a, TORIRSMAPED_FACT_LIVE_STATUS, s + 1) )
                    if( ToriRSMapEd_ReadU32(a.facts[s].data) == TORIRSMAPED_LIVE_ATTACHED )
                        attached = 1;
                if( attached )
                    break;
            }
            usleep(5000);
        }
    }
    check(wait_peer("\"players\"", 1, 5000), "peer: attached and asked for its players");
    /* tester_one logs in (the peer's `join`). */
    ToriRSMapEd_PutString(&payload, "");
    ToriRSMapEd_PutString(&payload, "join tester_one");
    mark(&a);
    mark(&b);
    conn_send(&a, TORIRSMAPED_REQ_LIVE_CHEAT, &payload);
    {
        int64_t const deadline = now_ms() + 6000;
        int online = 0;
        while( now_ms() < deadline && !online )
        {
            int i;
            pump();
            for( i = find_fact(&b, TORIRSMAPED_FACT_ACCOUNTS, b.mark); i >= 0; i = find_fact(&b, TORIRSMAPED_FACT_ACCOUNTS, i + 1) )
            {
                int row;
                decode_list(&b.facts[i], &list);
                row = list_find(&list, "tester_one");
                if( row >= 0 && (list.flags[row] & TORIRSMAPED_ACCT_ONLINE) )
                    online = 1;
            }
            usleep(5000);
        }
        check(online, "online: FACT_ACCOUNTS flags tester_one online once the server lists it");
        check(online && list.attached && list.x[list_find(&list, "tester_one")] == 3222,
            "online: the row carries the server's tile");
    }
    /* The queue flushes a few seconds after the login. */
    check(wait_peer("\"line\": \"questmark quest_cooksassistant\"", 1, 8000), "flush: the queued questmark reached the peer");
    check(wait_peer("\"line\": \"complete quest_clocktower\"", 1, 2000) && wait_peer("\"line\": \"give coins 5\"", 1, 2000),
        "flush: the other two lines too, in order");
    check(wait_peer("\"line\": \"checkpoint 90001\"", 1, 2000), "flush: then a checkpoint is asked for");
    snprintf(path, sizeof(path), "%s/accounts.ini", live);
    pump_for(50);
    check(!file_has(path, "tester_one.0 = "), "flush: the registry's queue is empty");
    check(wait_status(&b, 0, &status, 3000) >= 0 && strstr(status.message, "queued cheat(s) sent"),
        "flush: a broadcast status says how many went");

    /* An online SET is a cheat, then a checkpoint; the file the test writes
     * as the server becomes the snapshot. */
    mark(&b);
    check(set_ok(&a, "tester_one", "stats.cooking", "60", &status) && strstr(status.message, "online: ::setlevel cooking 60"),
        "online: stats.cooking 60 is ::setlevel");
    check(wait_peer("\"line\": \"setlevel cooking 60\"", 1, 3000), "online: the peer logged the cheat for tester_one");
    check(peer_count("\"player\": \"tester_one\", \"line\": \"setlevel") == 1, "online: addressed to the account");
    check(wait_peer("\"line\": \"checkpoint 90001\"", 2, 3000), "online: a second checkpoint is asked for");
    check(set_ok(&a, "tester_one", "player.pos", "3222 3218 0", &status) && strstr(status.message, "::tele 0_50_50_22_18"),
        "online: player.pos is ::tele <level>_<mx>_<mz>_<lx>_<lz>");
    check(set_ok(&a, "tester_one", "worn.3", "1277", &status) && strstr(status.message, "::item 1277 1 and 1 more"),
        "online: worn is ::item then ::wield");
    check(wait_peer("\"line\": \"wield 1277\"", 1, 3000), "online: the peer logged the wield");
    check(!set_ok(&a, "tester_one", "bank.3", "995 10", &status) && strstr(status.message, "offline"),
        "online: a bank edit is refused with the way out");
    check(!set_ok(&a, "tester_one", "inv.3", "-", &status) && strstr(status.message, "no cheat empties"),
        "online: removing one slot is refused");
    check(set_ok(&a, "tester_one", "quests.quest_cooksassistant", "complete", &status) && strstr(status.message, "::complete quest_cooksassistant"),
        "online: a quest complete is sent, not queued");
    check(set_ok(&a, "tester_one", "varps.varp29_cookquest", "2", &status) && strstr(status.message, "::setvar varp29_cookquest 2"),
        "online: a varp is ::setvar by name");
    check(set_ok(&a, "tester_one", "varbits.5078", "2", &status) && strstr(status.message, "::setvar varb5078_arceuus_soulbearer_story 2"),
        "online: a varbit is ::setvar by name too");
    check(!set_ok(&a, "tester_one", "stats.7.xp", "5", &status) && strstr(status.message, "only grows"),
        "online: xp below the current is refused");
    check(set_ok(&a, "tester_one", "stats.7.xp", "200000", &status) && strstr(status.message, "::xp cooking 98667"),
        "online: xp above adds the difference (from the save's 101333)");
    check(set_ok(&a, "tester_one", "loadout", "melee_set", &status) && strstr(status.message, "online: ::clearinv and"),
        "online: a loadout is cheats");
    check(wait_peer("\"line\": \"item 995 7\"", 1, 3000) && wait_peer("\"line\": \"clearinv\"", 2, 3000),
        "online: the loadout's backpack item and both clearinvs reached the peer");
    check(set_ok(&a, "tester_one", "quests.list", "mark quest_cooksassistant miniquest_bearyoursoul nosuch", &status)
              && strstr(status.message, "skipped"),
        "online: a quest batch is sent, the unknown row skipped");
    check(wait_peer("\"line\": \"questmark miniquest_bearyoursoul\"", 1, 3000), "online: the batch reached the peer");
    check(!set_ok(&a, "tester_one", "container.612.0", "995", &status) && strstr(status.message, "offline"),
        "online: a container edit is refused with the way out");
    /* The server's checkpoint lands: the view shows it. */
    snprintf(path, sizeof(path), "%s/saves/checkpoints", live);
    mkdirs(path);
    snprintf(path, sizeof(path), "%s/saves/checkpoints/90001.ini", live);
    mark(&b);
    write_text(path, "[player]\nname = tester_one\nx = 3222\nz = 3218\nlevel = 0\n\n[stats]\n7 = 60 2737000\n\n[varps]\n29 = 2\n1000 = 7\n");
    {
        int64_t const deadline = now_ms() + 3000;
        int seen = 0;
        while( now_ms() < deadline && !seen )
        {
            pump();
            if( latest_view(&b, "tester_one", &view) >= 0 && (view.flags & TORIRSMAPED_ACCT_SNAPSHOT) )
                seen = 1;
            usleep(5000);
        }
        check(seen, "snapshot: the view is broadcast once the checkpoint file appears");
        check(seen && view.boosted[7] == 60 && view.xp_tenths[7] == 2737000 && view.varp_count == 2
                  && strstr(view.status_text, "online at 3222,3218") && strstr(view.status_text, "checkpoint"),
            "snapshot: its values are the checkpoint's, the status says so");
    }
    /* Delete refuses an online account. */
    ToriRSMapEd_PutString(&payload, "tester_one");
    mark(&a);
    conn_send(&a, TORIRSMAPED_REQ_ACCOUNT_DELETE, &payload);
    check(wait_status(&a, TORIRSMAPED_REQ_ACCOUNT_DELETE, &status, 2000) >= 0 && status.status == EDITOR_HOST_REFUSED
              && strstr(status.message, "logged in"),
        "delete: an online account is refused");
    /* Logging out: the save is the view again. */
    ToriRSMapEd_PutString(&payload, "");
    ToriRSMapEd_PutString(&payload, "leave tester_one");
    mark(&b);
    conn_send(&a, TORIRSMAPED_REQ_LIVE_CHEAT, &payload);
    {
        int64_t const deadline = now_ms() + 6000;
        int offline = 0;
        while( now_ms() < deadline && !offline )
        {
            int i;
            pump();
            for( i = find_fact(&b, TORIRSMAPED_FACT_ACCOUNTS, b.mark); i >= 0; i = find_fact(&b, TORIRSMAPED_FACT_ACCOUNTS, i + 1) )
            {
                int row;
                decode_list(&b.facts[i], &list);
                row = list_find(&list, "tester_one");
                if( row >= 0 && !(list.flags[row] & TORIRSMAPED_ACCT_ONLINE) )
                    offline = 1;
            }
            usleep(5000);
        }
        check(offline, "logout: FACT_ACCOUNTS flags tester_one offline again");
    }
    ToriRSMapEd_PutString(&payload, "tester_one");
    mark(&a);
    conn_send(&a, TORIRSMAPED_REQ_ACCOUNT_VIEW, &payload);
    at = wait_fact(&a, TORIRSMAPED_FACT_ACCOUNT, 2000);
    if( at >= 0 )
    {
        decode_view(&a.facts[at], &view);
        check(!(view.flags & TORIRSMAPED_ACCT_SNAPSHOT) && view.boosted[7] == 50 && strstr(view.status_text, "offline"),
            "logout: the view is the save's again");
    }

    mark(&a);
    conn_send(&a, TORIRSMAPED_REQ_LIVE_DETACH, NULL);
    pump_for(200);
    if( peer > 0 )
    {
        int status_dead;
        kill(peer, SIGTERM);
        waitpid(peer, &status_dead, 0);
    }
    g_conn_count = 0;
    ToriRSMapEd_SessionFree(&a.session);
    ToriRSMapEd_SessionFree(&b.session);
    ToriRSMapEd_Close(maped);
    free(maped);
}

int
main(
    int argc,
    char** argv)
{
    if( argc < 4 )
    {
        fprintf(stderr, "usage: accounts_test <scratch> <peer-script> <repo-root>\n");
        return 2;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    /* The editor server starts its children through the host's spawner, as
     * the client and torirsmaped hand it theirs (torirs_maped_build.h). */
    ToriRSMapEd_SetSpawner(LaunchSessions_SystemSpawner());
    test_pure();
    test_codec();
    test_quests();
    test_server(argv[1], argv[2], argv[3]);
    printf("accounts_test: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
