/**
 * The whole ToriRSMapEd model with no socket, driven end to end through the
 * embed pipes:
 *
 *   - the server as DOCUMENT AUTHORITY: mirrors mutate only on FACT_CMD
 *     echoes, every connection sees every edit, history is shared, saves
 *     happen server-side and broadcast what landed;
 *   - CONNECTIONS vs CLIENTS: one process holds several connections; a
 *     Client is a group of them. Document facts cross Client boundaries,
 *     state facts do not — one Client's selection never reaches another;
 *   - the file layer underneath: square list, spawn save, the tree lock
 *     (now the server's own), atomic writes;
 *   - protocol v2: DOC_OPEN answering the same text as the v1 SQUARE_OPEN
 *     alias, spawn / config / script documents with their commands and
 *     saves, DOC_CREATE of a new square in a lane storage, and a v1 peer on
 *     the same server hearing only the v1 frames it knows.
 *
 *   maped_embed_test <scratch-dir> <source-jm2> <source-jl2>
 *
 * Writes only inside the scratch directory it is given.
 */

#include "editor/editor_areas.h"
#include "editor/editor_cmd.h"
#include "editor/editor_doc.h"
#include "editor/editor_host.h"
#include "editor/editor_jm2.h"
#include "editor/editor_spawn.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_live.h"
#include "torirsmaped/torirs_maped_build.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

static int g_checks;
static int g_failures;

static void
check(
    int condition,
    char const* what)
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
    char const* path,
    size_t* out_size)
{
    FILE* f = fopen(path, "rb");
    long size;
    char* data;
    if( !f )
        return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    rewind(f);
    data = malloc((size_t)size + 1);
    if( !data || fread(data, 1, (size_t)size, f) != (size_t)size )
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
copy_file(
    char const* from,
    char const* to)
{
    size_t size = 0;
    char* data = slurp(from, &size);
    FILE* f;
    if( !data )
    {
        fprintf(stderr, "cannot read %s\n", from);
        exit(2);
    }
    f = fopen(to, "wb");
    if( !f || fwrite(data, 1, size, f) != size )
    {
        fprintf(stderr, "cannot write %s\n", to);
        exit(2);
    }
    fclose(f);
    free(data);
}

/* ---- a mirror: what every real client keeps ------------------------------ */

struct mirror
{
    struct Editor_Doc* doc; /* NULL for a connection with no world */
    int cmds_seen;
    int saved_seen;
    int states_seen;
    uint32_t last_state_key;
    int32_t last_state_values[TORIRSMAPED_STATE_VALUES_MAX];
    int last_state_count;
};

static void
mirror_on_cmd(
    void* user_data,
    uint32_t seq,
    int direction,
    const struct Editor_Cmd* command)
{
    struct mirror* mirror = user_data;
    (void)seq;
    if( mirror->doc )
        Editor_CmdApply(
            mirror->doc,
            command,
            direction ? EDITOR_CMD_INVERSE : EDITOR_CMD_FORWARD);
    mirror->cmds_seen++;
}

static void
mirror_on_saved(
    void* user_data,
    int count,
    const struct Editor_DocRef* rows)
{
    struct mirror* mirror = user_data;
    mirror->saved_seen += count;
    if( !mirror->doc )
        return;
    for( int i = 0; i < count; i++ )
    {
        struct Editor_DocHandle handle;
        if( Editor_DocFind(mirror->doc, rows[i].kind, rows[i].key, &handle) )
            Editor_DocHandleClearDirty(&handle);
    }
}

static void
mirror_on_state(
    void* user_data,
    uint32_t key,
    const int32_t* values,
    int count)
{
    struct mirror* mirror = user_data;
    mirror->states_seen++;
    mirror->last_state_key = key;
    mirror->last_state_count = count;
    for( int i = 0; i < count && i < TORIRSMAPED_STATE_VALUES_MAX; i++ )
        mirror->last_state_values[i] = values[i];
}

static int
drain(
    struct EditorHost* host,
    struct mirror* mirror)
{
    struct EditorHostMapEdFacts sink = {
        mirror, mirror_on_cmd, mirror_on_saved, mirror_on_state, NULL
    };
    return Editor_HostMapEdDrainFacts(host, &sink);
}

static int
open_square_into(
    struct EditorHost* host,
    struct Editor_Doc* doc,
    int map_x,
    int map_z)
{
    struct EditorHost_Blob jm2 = { NULL, 0 };
    struct EditorHost_Blob jl2 = { NULL, 0 };
    int dirty_map = 0;
    int dirty_loc = 0;
    struct Editor_Square* square;
    struct Editor_ParseResult result;

    if( Editor_HostMapEdSquareOpen(host, map_x, map_z, &jm2, &jl2, &dirty_map, &dirty_loc)
        != EDITOR_HOST_OK )
        return 0;
    square = Editor_DocOpenSquare(doc, map_x, map_z);
    if( !square )
        return 0;
    result = Editor_Jm2Parse(square, jm2.data, jm2.size);
    if( result.status != EDITOR_PARSE_OK )
        return 0;
    if( jl2.data && jl2.size > 0 )
    {
        result = Editor_Jl2Parse(square, jl2.data, jl2.size);
        if( result.status != EDITOR_PARSE_OK )
            return 0;
    }
    square->dirty_map = dirty_map;
    square->dirty_loc = dirty_loc;
    Editor_HostBlobFree(&jm2);
    Editor_HostBlobFree(&jl2);
    return 1;
}

/** DOC_OPEN a non-square document into a mirror doc. */
static int
open_doc_into(
    struct EditorHost* host,
    struct Editor_Doc* doc,
    enum Editor_DocKind kind,
    char const* key)
{
    struct EditorHost_DocText text;
    struct Editor_DocHandle handle;
    int ok = 0;

    if( Editor_HostMapEdDocOpen(host, kind, key, &text) != EDITOR_HOST_OK
        || text.part_count != 1 || !Editor_DocOpen(doc, kind, key, &handle) )
    {
        Editor_HostDocTextFree(&text);
        return 0;
    }
    if( handle.spawn )
    {
        ok = Editor_SpawnParse(&handle.spawn->file, text.parts[0].data, text.parts[0].size).ok;
        handle.spawn->loaded = ok;
    }
    else
    {
        Editor_TextDocSet(handle.text, text.parts[0].data, text.parts[0].size);
        ok = 1;
    }
    Editor_HostDocTextFree(&text);
    return ok;
}

#include <errno.h>
#include <sys/stat.h>

/* What a core with no host bindings serves (ToriRSMapEd_Create): the catalog
 * and its answers, the lanes' frames, strokes joined by DOC_CREATE. BUILD,
 * LIVE and SPAWN_VIEW come only from a host that binds them. */
#define EMBED_CAPS                                                                                    \
    (TORIRSMAPED_CAP_CATALOG | TORIRSMAPED_CAP_CATALOG_PROGRESS | TORIRSMAPED_CAP_CATALOG_SQUARE_LOCS    \
     | TORIRSMAPED_CAP_CATALOG_USES | TORIRSMAPED_CAP_LANES_V2 | TORIRSMAPED_CAP_DOC_CREATE_STROKE                \
     | TORIRSMAPED_CAP_CATALOG_SQUARES_NOWAIT)

static void
mkdir_p_or_die(char const* path)
{
    if( mkdir(path, 0755) != 0 && errno != EEXIST )
    {
        fprintf(stderr, "cannot create %s\n", path);
        exit(2);
    }
}

static void
write_text(
    char const* path,
    char const* text)
{
    FILE* f = fopen(path, "wb");
    if( !f || fwrite(text, 1, strlen(text), f) != strlen(text) )
    {
        fprintf(stderr, "cannot write %s\n", path);
        exit(2);
    }
    fclose(f);
}

/* ---- a raw peer on a server core, for the v1 compatibility checks -------- */

struct raw_peer
{
    struct ToriRSMapEdBuf to_server;
    struct ToriRSMapEdBuf to_client;
    struct ToriRSMapEdSession session;
};

static int
raw_recv(
    void* ctx,
    uint8_t* dst,
    int max)
{
    struct raw_peer* peer = ctx;
    return ToriRSMapEd_BufRead(&peer->to_server, dst, max);
}

static int
raw_send(
    void* ctx,
    const uint8_t* src,
    int len)
{
    struct raw_peer* peer = ctx;
    ToriRSMapEd_BufWrite(&peer->to_client, src, len);
    return len;
}

static void
raw_attach(
    struct raw_peer* peer,
    struct ToriRSMapEd* maped)
{
    struct ToriRSMapEdTransport transport;

    memset(peer, 0, sizeof(*peer));
    transport.ctx = peer;
    transport.recv = raw_recv;
    transport.send = raw_send;
    ToriRSMapEd_SessionInit(&peer->session, maped, &transport);
}

static void
raw_send_frame(
    struct raw_peer* peer,
    uint32_t type,
    const uint8_t* payload,
    uint32_t length)
{
    uint8_t header[8];
    ToriRSMapEd_WriteU32(header, type);
    ToriRSMapEd_WriteU32(header + 4, length);
    ToriRSMapEd_BufWrite(&peer->to_server, header, 8);
    if( length > 0 )
        ToriRSMapEd_BufWrite(&peer->to_server, payload, (int)length);
    ToriRSMapEd_SessionPump(&peer->session);
}

/** Pop one frame the server sent this peer. Returns 0 when none is whole.
 *  `out_body` points into a static copy valid until the next call. */
static int
raw_take(
    struct raw_peer* peer,
    uint32_t* out_type,
    uint32_t* out_length,
    const uint8_t** out_body)
{
    static uint8_t copy[1 << 20];
    const uint8_t* head;
    uint32_t length;

    if( ToriRSMapEd_BufAvailable(&peer->to_client) < 8 )
        return 0;
    head = ToriRSMapEd_BufPeek(&peer->to_client);
    length = ToriRSMapEd_ReadU32(head + 4);
    if( ToriRSMapEd_BufAvailable(&peer->to_client) < 8 + (int)length || length > sizeof(copy) )
        return 0;
    *out_type = ToriRSMapEd_ReadU32(head);
    *out_length = length;
    memcpy(copy, head + 8, length);
    *out_body = copy;
    ToriRSMapEd_BufConsume(&peer->to_client, 8 + (int)length);
    return 1;
}

static void
raw_detach(struct raw_peer* peer)
{
    ToriRSMapEd_SessionFree(&peer->session);
    ToriRSMapEd_BufFree(&peer->to_server);
    ToriRSMapEd_BufFree(&peer->to_client);
}

/**
 * A v1 peer and a v2 peer on one server core: the v1 one is answered in v1
 * (12-byte FACT_HELLO, the 144-byte FACT_CMD) and never hears a frame it
 * does not know; a v2-only request from it is a framing error.
 */
static void
test_v1_peer(char const* scratch)
{
    struct ToriRSMapEd* maped = calloc(1, sizeof(*maped));
    struct raw_peer* v1 = calloc(1, sizeof(*v1));
    struct raw_peer* v2 = calloc(1, sizeof(*v2));
    uint8_t hello[12];
    uint32_t type = 0;
    uint32_t length = 0;
    const uint8_t* body = NULL;

    if( !maped || !v1 || !v2 )
    {
        fprintf(stderr, "out of memory\n");
        exit(2);
    }
    /* Read-only is fine: the first embed holds the lock; nothing here saves. */
    ToriRSMapEd_Open(maped, scratch, NULL);
    raw_attach(v1, maped);
    raw_attach(v2, maped);

    ToriRSMapEd_WriteU32(hello, 1);
    ToriRSMapEd_WriteU32(hello + 4, TORIRSMAPED_ROLE_GENERIC);
    ToriRSMapEd_WriteU32(hello + 8, 0);
    raw_send_frame(v1, TORIRSMAPED_REQ_HELLO, hello, 12);
    check(
        raw_take(v1, &type, &length, &body) && type == TORIRSMAPED_FACT_HELLO && length == 12
            && ToriRSMapEd_ReadU32(body) == 1,
        "a v1 HELLO is answered with the 12-byte v1 FACT_HELLO");

    ToriRSMapEd_WriteU32(hello, TORIRSMAPED_PROTO_VERSION);
    raw_send_frame(v2, TORIRSMAPED_REQ_HELLO, hello, 12);
    check(
        raw_take(v2, &type, &length, &body) && type == TORIRSMAPED_FACT_HELLO && length > 16
            && ToriRSMapEd_ReadU32(body) == 2
            && ToriRSMapEd_ReadU32(body + 12) == EMBED_CAPS,
        "a v2 HELLO gets FACT_HELLO v2 with capabilities (the catalog, the lanes, strokes; no build or live)");
    {
        /* This server is read-only (the first embed holds the tree): its v2
         * HELLO names the holder after the capabilities -- this process. */
        struct ToriRSMapEdReader reader;
        char reason[1024] = "";
        char want[64];
        if( length > 16 )
        {
            ToriRSMapEd_ReaderInit(&reader, body + 16, length - 16);
            ToriRSMapEd_GetString(&reader, reason, sizeof(reason));
        }
        snprintf(want, sizeof(want), "is held by pid %ld", (long)getpid());
        check(ToriRSMapEd_ReadU32(body + 4) == 0 && strstr(reason, want) != NULL,
            "a read-only server's v2 HELLO names who holds the tree (pid)");
    }
    /* The greeting is followed by the lane as it is (ToriRSMapEd_LiveWelcome):
     * no lane here, so detached and no play client. A v1 peer gets neither. */
    check(raw_take(v2, &type, &length, &body) && type == TORIRSMAPED_FACT_LIVE_STATUS
              && ToriRSMapEd_ReadU32(body) == TORIRSMAPED_LIVE_DETACHED,
        "a v2 HELLO is followed by the live status (detached)");
    check(raw_take(v2, &type, &length, &body) && type == TORIRSMAPED_FACT_LIVE_PLAY
              && ToriRSMapEd_ReadU32(body) == TORIRSMAPED_PLAY_NONE,
        "and by the play state (none)");
    check(!raw_take(v1, &type, &length, &body), "the v1 peer is not told the lane (it cannot read the facts)");

    {
        uint8_t coords[8];
        ToriRSMapEd_WriteU32(coords, 50);
        ToriRSMapEd_WriteU32(coords + 4, 50);
        raw_send_frame(v1, TORIRSMAPED_REQ_SQUARE_OPEN, coords, 8);
        check(
            raw_take(v1, &type, &length, &body) && type == TORIRSMAPED_FACT_SQUARE_TEXT
                && ToriRSMapEd_ReadU32(body) == EDITOR_HOST_OK,
            "a v1 peer opens a square with SQUARE_OPEN");
    }

    /* A v2 tile edit reaches the v1 peer in the 144-byte form. */
    {
        struct Editor_Cmd command;
        struct ToriRSMapEdBuf wire;
        struct Editor_Cmd decoded;

        Editor_CmdInitSquare(&command, EDITOR_CMD_TILE, 50, 50);
        command.u.tile.x = 3;
        command.u.tile.z = 4;
        command.u.tile.before =
            Editor_DocFindSquare(&maped->doc, 50, 50)->tiles[Editor_TileIndex(3, 4, 0)];
        command.u.tile.after = command.u.tile.before;
        command.u.tile.after.underlay_id = 99;
        memset(&wire, 0, sizeof(wire));
        ToriRSMapEd_CmdEncode(&command, &wire);
        raw_send_frame(
            v2,
            TORIRSMAPED_REQ_CMD,
            ToriRSMapEd_BufPeek(&wire),
            (uint32_t)ToriRSMapEd_BufAvailable(&wire));
        ToriRSMapEd_BufFree(&wire);

        check(
            raw_take(v1, &type, &length, &body) && type == TORIRSMAPED_FACT_CMD
                && length == 8 + TORIRSMAPED_CMD_WIRE_V1
                && ToriRSMapEd_CmdDecodeV1(body + 8, length - 8, &decoded)
                && decoded.u.tile.after.underlay_id == 99 && decoded.u.tile.x == 3
                && strcmp(decoded.doc_key, "m50_50") == 0,
            "the v1 peer hears the v2 edit as a v1 FACT_CMD");
        check(
            raw_take(v2, &type, &length, &body) && type == TORIRSMAPED_FACT_CMD
                && ToriRSMapEd_CmdDecode(body + 8, length - 8, &decoded)
                       == TORIRSMAPED_CMD_DECODE_OK
                && decoded.u.tile.after.underlay_id == 99,
            "the v2 peer hears it in the v2 form");
        check(
            raw_take(v2, &type, &length, &body) && type == TORIRSMAPED_FACT_STATUS
                && ToriRSMapEd_ReadU32(body + 4) == EDITOR_HOST_OK,
            "and its own request is acked");
    }

    /* A command kind from the future: refused, never applied, session kept. */
    {
        struct ToriRSMapEdBuf wire;
        memset(&wire, 0, sizeof(wire));
        ToriRSMapEd_PutU32(&wire, EDITOR_DOC_SQUARE);
        ToriRSMapEd_PutString(&wire, "m50_50");
        ToriRSMapEd_PutU32(&wire, 77);
        ToriRSMapEd_PutU32(&wire, 0);
        ToriRSMapEd_PutU32(&wire, 1);
        ToriRSMapEd_PutBytes(&wire, "xyz", 3);
        raw_send_frame(
            v2,
            TORIRSMAPED_REQ_CMD,
            ToriRSMapEd_BufPeek(&wire),
            (uint32_t)ToriRSMapEd_BufAvailable(&wire));
        {
            struct Editor_Cmd decoded;
            check(
                ToriRSMapEd_CmdDecode(
                    ToriRSMapEd_BufPeek(&wire),
                    (uint32_t)ToriRSMapEd_BufAvailable(&wire),
                    &decoded)
                        == TORIRSMAPED_CMD_DECODE_UNKNOWN_KIND
                    && decoded.doc_kind == EDITOR_DOC_SQUARE
                    && strcmp(decoded.doc_key, "m50_50") == 0,
                "an unknown command kind decodes as UNKNOWN_KIND with its document named");
        }
        ToriRSMapEd_BufFree(&wire);
        check(
            raw_take(v2, &type, &length, &body) && type == TORIRSMAPED_FACT_STATUS
                && ToriRSMapEd_ReadU32(body + 4) == EDITOR_HOST_REFUSED
                && ToriRSMapEd_SessionAlive(&v2->session),
            "the server refuses an unknown command kind and keeps the session");
        check(!raw_take(v1, &type, &length, &body), "nothing reached the v1 peer");
    }

    /* A v2-only request from a v1 peer is a protocol it never declared. */
    {
        struct ToriRSMapEdBuf wire;
        memset(&wire, 0, sizeof(wire));
        ToriRSMapEd_PutU32(&wire, EDITOR_DOC_SQUARE);
        ToriRSMapEd_PutString(&wire, "m50_50");
        raw_send_frame(
            v1,
            TORIRSMAPED_REQ_DOC_OPEN,
            ToriRSMapEd_BufPeek(&wire),
            (uint32_t)ToriRSMapEd_BufAvailable(&wire));
        ToriRSMapEd_BufFree(&wire);
        check(!ToriRSMapEd_SessionAlive(&v1->session), "a v1 peer sending DOC_OPEN is closed");
    }

    raw_detach(v1);
    raw_detach(v2);
    ToriRSMapEd_Close(maped);
    free(maped);
    free(v1);
    free(v2);
}

/* The server pack's manifest diff RUN uses to name the server-pack groups a
 * packs build rewrote (ToriRSMapEd_BuildServPackDiff). */
struct servpack_rows
{
    uint32_t rows[16][2];
    int count;
};

static void
servpack_rows_archive(
    void* user,
    uint32_t index,
    uint32_t archive)
{
    struct servpack_rows* rows = user;
    if( rows->count < 16 )
    {
        rows->rows[rows->count][0] = index;
        rows->rows[rows->count][1] = archive;
    }
    rows->count++;
}

static int
servpack_rows_has(
    const struct servpack_rows* rows,
    uint32_t group)
{
    for( int i = 0; i < rows->count && i < 16; i++ )
        if( rows->rows[i][0] == TORIRSMAPED_RELOAD_SERVPACK_INDEX && rows->rows[i][1] == group )
            return 1;
    return 0;
}

/* ---- a lane's content opened as a plain tree ------------------------------- */

/* A manifest's content_dir (or --content) naming build/lanes/<name>/content
 * without its lane: ToriRSMapEd_Open, not OpenLane. The lane's directories
 * are still links to its base, so a save must still make them real first,
 * or it lands in the base. */
static void
test_plain_open_on_lane(char const* scratch)
{
    struct ToriRSMapEd* maped = calloc(1, sizeof(*maped));
    struct ToriRSMapEdLaneDef lane;
    char repo[1024];
    char base[1100];
    char path[2048];
    char why[512];
    char* disk;
    size_t disk_size = 0;

    if( !maped )
    {
        fprintf(stderr, "out of memory\n");
        exit(2);
    }
    snprintf(repo, sizeof(repo), "%s/plainrepo", scratch);
    snprintf(base, sizeof(base), "%s/base", repo);
    mkdir_p_or_die(repo);
    mkdir_p_or_die(base);
    snprintf(path, sizeof(path), "%s/server", base);
    mkdir_p_or_die(path);
    snprintf(path, sizeof(path), "%s/server/scripts", base);
    mkdir_p_or_die(path);
    snprintf(path, sizeof(path), "%s/server/scripts/areas", base);
    mkdir_p_or_die(path);
    snprintf(path, sizeof(path), "%s/server/scripts/areas/a", base);
    mkdir_p_or_die(path);
    snprintf(path, sizeof(path), "%s/server/scripts/areas/a/a.rs2", base);
    write_text(path, "[proc,a]\n");
    check(ToriRSMapEdLanes_Create(repo, base, "plain", &lane, why, sizeof(why)) && lane.sparse,
        "plain open: a sparse lane to open");
    snprintf(path, sizeof(path), "%s/server/scripts/areas", lane.content);
    check(ToriRSMapEdLanes_IsLink(path), "plain open: the lane's areas/ is a link to its base");

    ToriRSMapEd_Open(maped, lane.content, NULL);
    check(maped->writable, "plain open: the lane's content opens writable");
    check(maped->files.vtable->doc_save(maped->files.user_data, EDITOR_DOC_TEXT, "rs2:areas/a/new.rs2", 0,
              "[proc,new]\n", 11)
              == EDITOR_HOST_OK,
        "plain open: a script saves under the linked directory");
    snprintf(path, sizeof(path), "%s/server/scripts/areas/a/new.rs2", base);
    disk = slurp(path, &disk_size);
    check(disk == NULL, "plain open: nothing landed in the lane's base");
    free(disk);
    snprintf(path, sizeof(path), "%s/server/scripts/areas/a/new.rs2", lane.content);
    disk = slurp(path, &disk_size);
    check(disk && strcmp(disk, "[proc,new]\n") == 0, "plain open: the script is the lane's");
    free(disk);
    snprintf(path, sizeof(path), "%s/server/scripts/areas", lane.content);
    check(!ToriRSMapEdLanes_IsLink(path), "plain open: areas/ was made real first");
    ToriRSMapEd_Close(maped);
    free(maped);
}

static void
test_servpack_diff(void)
{
    static const char before[] =
        "torirs-server-pack 1\n"
        "config 4b7387697f9b9abb\n"
        "input 4561257 1792535173408192663 fd05f3bfb89d53c3 configs/all.loc\n"
        "unit loc ba6fafb47e0f0266 1 62393\n"
        "archive 6 1187 1111111111111111 1 loc\n"
        "archive 70 4 2222222222222222 1 loc\n"
        "archive 70 5 3333333333333333 1 loc\n"
        "archive 73 12 4444444444444444 1 npc\n"
        "archive 128 0 5555555555555555 1 names\n"
        "archive 192 9 6666666666666666 1 npc\n"
        "archive 16 99 7777777777777777 1 varp\n";
    /* loc: two records archives changed (one row, group 70); npc: its
     * [default] band changed (192/9 -> the npc band group 9); varp 16/99
     * gone; obj 74/1 new; the name table unchanged; the band 6/1187 the same. */
    static const char after[] =
        "torirs-server-pack 1\n"
        "config 4b7387697f9b9abb\n"
        "input 4561300 1792535173408199999 0d05f3bfb89d53c3 configs/all.loc\n"
        "unit loc 0a6fafb47e0f0266 2 62393\n"
        "archive 6 1187 1111111111111111 1 loc\n"
        "archive 70 4 2222222222222299 2 loc\n"
        "archive 70 5 3333333333333399 2 loc\n"
        "archive 73 12 4444444444444444 1 npc\n"
        "archive 74 1 8888888888888888 1 obj\n"
        "archive 128 0 5555555555555555 1 names\n"
        "archive 192 9 6666666666666699 2 npc\n";
    struct ToriRSMapEdBuildSink sink;
    struct servpack_rows rows;
    int groups = -1;
    int changed;

    memset(&rows, 0, sizeof(rows));
    memset(&sink, 0, sizeof(sink));
    sink.user = &rows;
    sink.archive = servpack_rows_archive;
    changed = ToriRSMapEd_BuildServPackDiff(before, sizeof(before) - 1, after, sizeof(after) - 1,
        &sink, &groups);
    check(changed == 5, "servpack diff: 5 archives changed (2 loc records, npc default, varp gone, obj new)");
    check(groups == 4 && rows.count == 4, "servpack diff: one reload row per group (4)");
    check(servpack_rows_has(&rows, 70), "servpack diff: (255, 70) for the loc records group");
    check(servpack_rows_has(&rows, 9), "servpack diff: the npc [default] band names the npc kind (255, 9)");
    check(servpack_rows_has(&rows, 16), "servpack diff: a vanished archive is a change (255, 16)");
    check(servpack_rows_has(&rows, 74), "servpack diff: a new archive is a change (255, 74)");
    check(!servpack_rows_has(&rows, 6) && !servpack_rows_has(&rows, 73) && !servpack_rows_has(&rows, 128),
        "servpack diff: unchanged groups are not named");

    memset(&rows, 0, sizeof(rows));
    changed = ToriRSMapEd_BuildServPackDiff(after, sizeof(after) - 1, after, sizeof(after) - 1, &sink,
        &groups);
    check(changed == 0 && groups == 0 && rows.count == 0, "servpack diff: the same manifest names nothing");
    changed = ToriRSMapEd_BuildServPackDiff("archive 2 6 1 1 loc\n", 20, after, sizeof(after) - 1, &sink,
        &groups);
    check(changed == -1, "servpack diff: a text with no server-pack header is refused");
    check(ToriRSMapEd_BuildServPackReloadGroup(193, 10) == 10
              && ToriRSMapEd_BuildServPackReloadGroup(74, 3) == 74,
        "servpack diff: the client-id group names its kind; a records group names itself");
}

int
main(
    int argc,
    char** argv)
{
    char const* scratch;
    char jm2_path[1200];
    char jl2_path[1200];
    char path[1300];
    /* Connections: a is Client 1's viewer, a2 its controller (no world),
     * b a second Client's viewer, c a late joiner. */
    struct EditorHost a;
    struct EditorHost a2;
    struct EditorHost b;
    struct EditorHost c;
    struct mirror ma = { NULL, 0, 0, 0, 0, { 0 }, 0 };
    struct mirror ma2 = { NULL, 0, 0, 0, 0, { 0 }, 0 };
    struct mirror mb = { NULL, 0, 0, 0, 0, { 0 }, 0 };
    struct mirror mc = { NULL, 0, 0, 0, 0, { 0 }, 0 };
    struct Editor_Cmd command;
    struct Editor_Tile original_tile;
    int coords[8];
    int count = 0;
    char* disk;
    size_t disk_size = 0;

    if( argc != 4 )
    {
        fprintf(stderr, "usage: maped_embed_test <scratch-dir> <source-jm2> <source-jl2>\n");
        return 2;
    }
    scratch = argv[1];

    test_servpack_diff();

    snprintf(jm2_path, sizeof(jm2_path), "%s/maps/m50_50.jm2", scratch);
    snprintf(jl2_path, sizeof(jl2_path), "%s/maps/m50_50.jl2", scratch);
    copy_file(argv[2], jm2_path);
    copy_file(argv[3], jl2_path);

    ma.doc = malloc(sizeof(*ma.doc));
    mb.doc = malloc(sizeof(*mb.doc));
    mc.doc = malloc(sizeof(*mc.doc));
    if( !ma.doc || !mb.doc || !mc.doc )
    {
        fprintf(stderr, "out of memory for mirror docs\n");
        return 2;
    }
    Editor_DocInit(ma.doc, NULL);
    Editor_DocInit(mb.doc, NULL);
    Editor_DocInit(mc.doc, NULL);

    /* ---- connections and Clients ----------------------------------------- */

    check(Editor_HostOpenMapEdEmbed(&a, scratch, NULL) == 1, "viewer connection opens");
    check(Editor_HostMapEdWritable(&a), "the server holds the tree lock");
    check(
        Editor_HostOpenMapEdEmbedPeer(&a2, &a, TORIRSMAPED_ROLE_CONTROLLER, 1) == 1,
        "a controller connection joins the same Client");
    check(
        Editor_HostOpenMapEdEmbedPeer(&b, &a, TORIRSMAPED_ROLE_VIEWER, 0) == 1,
        "a second Client's viewer connects");
    check(
        Editor_HostMapEdClientId(&a) == Editor_HostMapEdClientId(&a2),
        "viewer and controller share a Client id");
    check(
        Editor_HostMapEdClientId(&a) != Editor_HostMapEdClientId(&b),
        "the second Client got its own id");

    check(
        a.vtable->square_list(a.user_data, coords, 4, &count) == EDITOR_HOST_OK
            && count == 1 && coords[0] == 50 && coords[1] == 50,
        "square_list names m50_50");

    /* ---- mirrors open the square ------------------------------------------ */

    check(open_square_into(&a, ma.doc, 50, 50) == 1, "Client 1's mirror opens the square");
    check(open_square_into(&b, mb.doc, 50, 50) == 1, "Client 2's mirror opens the square");
    original_tile = Editor_DocFindSquare(ma.doc, 50, 50)->tiles[Editor_TileIndex(0, 0, 0)];

    /* ---- DOC_OPEN and the v1 alias answer the same document --------------- */

    check(Editor_HostMapEdCapabilities(&a) == EMBED_CAPS,
        "HELLO v2 reports what the core serves and no build or live binding yet");
    {
        struct EditorHost_Blob jm2 = { NULL, 0 };
        struct EditorHost_Blob jl2 = { NULL, 0 };
        int dirty_map = 1;
        int dirty_loc = 1;
        struct EditorHost_DocText text;

        check(
            Editor_HostMapEdSquareOpen(&a, 50, 50, &jm2, &jl2, &dirty_map, &dirty_loc)
                == EDITOR_HOST_OK,
            "the v1 SQUARE_OPEN alias answers");
        check(
            Editor_HostMapEdDocOpen(&a, EDITOR_DOC_SQUARE, "m50_50", &text) == EDITOR_HOST_OK,
            "DOC_OPEN(SQUARE, m50_50) answers");
        check(text.part_count == 2, "a square answers two parts");
        check(
            text.parts[0].data && jm2.data && text.parts[0].size == jm2.size
                && memcmp(text.parts[0].data, jm2.data, jm2.size) == 0,
            "DOC_OPEN and SQUARE_OPEN answer the identical .jm2 text");
        check(
            text.parts[1].data && jl2.data && text.parts[1].size == jl2.size
                && memcmp(text.parts[1].data, jl2.data, jl2.size) == 0,
            "DOC_OPEN and SQUARE_OPEN answer the identical .jl2 text");
        check(
            text.dirty == (uint32_t)((dirty_map ? 1 : 0) | (dirty_loc ? 2 : 0)),
            "and the same dirty flags");
        check(strcmp(text.storage, "") == 0, "a content-tree square reports storage maps/");
        Editor_HostDocTextFree(&text);
        check(
            Editor_HostMapEdDocOpen(&a, EDITOR_DOC_SQUARE, "maps/m50_50", &text)
                    == EDITOR_HOST_OK
                && strcmp(text.key, "m50_50") == 0,
            "a storage-prefixed key opens the same square, canonical key back");
        Editor_HostDocTextFree(&text);
        check(
            Editor_HostMapEdDocOpen(&a, EDITOR_DOC_SQUARE, "../m50_50", &text)
                == EDITOR_HOST_REFUSED,
            "a key that leaves the tree is refused");
        Editor_HostDocTextFree(&text);
        Editor_HostBlobFree(&jm2);
        Editor_HostBlobFree(&jl2);
    }

    /* ---- a document edit crosses Client boundaries ------------------------ */

    Editor_CmdInitSquare(&command, EDITOR_CMD_TILE, 50, 50);
    command.u.tile.level = 0;
    command.u.tile.x = 0;
    command.u.tile.z = 0;
    command.u.tile.before = original_tile;
    command.u.tile.after = original_tile;
    command.u.tile.after.underlay_id = 7;

    check(
        Editor_HostMapEdCmd(&a, &command) == EDITOR_HOST_OK,
        "the authority accepts the edit");
    drain(&a, &ma);
    drain(&a2, &ma2);
    drain(&b, &mb);
    check(ma.cmds_seen == 1, "the sender got its own echo");
    check(ma2.cmds_seen == 1, "the same Client's controller heard the edit");
    check(mb.cmds_seen == 1, "the OTHER Client heard the edit too — one world");
    check(
        Editor_DocFindSquare(ma.doc, 50, 50)->tiles[Editor_TileIndex(0, 0, 0)].underlay_id
            == 7,
        "the sender's mirror applied the echo");
    check(
        Editor_DocFindSquare(mb.doc, 50, 50)->tiles[Editor_TileIndex(0, 0, 0)].underlay_id
            == 7,
        "the other Client's mirror matches");
    check(
        Editor_DocFindSquare(mb.doc, 50, 50)->dirty_map == 1,
        "the edit marked the mirror dirty");

    /* ---- shared history: the other Client can undo ------------------------ */

    check(Editor_HostMapEdUndo(&b) == 1, "the second Client undoes the edit");
    drain(&a, &ma);
    drain(&b, &mb);
    check(
        memcmp(
            &Editor_DocFindSquare(ma.doc, 50, 50)->tiles[Editor_TileIndex(0, 0, 0)],
            &original_tile,
            sizeof(original_tile))
            == 0,
        "undo restored the original authored tile everywhere");

    check(Editor_HostMapEdRedo(&a2) == 1, "the controller redoes it (no world needed)");
    drain(&a, &ma);
    drain(&b, &mb);
    check(
        Editor_DocFindSquare(ma.doc, 50, 50)->tiles[Editor_TileIndex(0, 0, 0)].underlay_id
            == 7,
        "redo re-applied it everywhere");

    /* ---- a late joiner opens the square: the answer is the DOCUMENT ------- */

    check(
        Editor_HostOpenMapEdEmbedPeer(&c, &a, TORIRSMAPED_ROLE_GENERIC, 0) == 1,
        "a late-joining connection attaches");
    check(open_square_into(&c, mc.doc, 50, 50) == 1, "it opens the square");
    check(
        Editor_DocFindSquare(mc.doc, 50, 50)->tiles[Editor_TileIndex(0, 0, 0)].underlay_id
            == 7,
        "the late joiner received the EDITED document, not the file");
    check(
        Editor_DocFindSquare(mc.doc, 50, 50)->dirty_map == 1,
        "and its dirty flags agree with the authority");
    {
        struct EditorHost_Blob jm2 = { NULL, 0 };
        struct EditorHost_Blob jl2 = { NULL, 0 };
        int dirty_map = 0;
        int dirty_loc = 0;
        struct EditorHost_DocText text;

        Editor_HostMapEdSquareOpen(&c, 50, 50, &jm2, &jl2, &dirty_map, &dirty_loc);
        check(
            Editor_HostMapEdDocOpen(&c, EDITOR_DOC_SQUARE, "m50_50", &text) == EDITOR_HOST_OK
                && text.parts[0].size == jm2.size
                && memcmp(text.parts[0].data, jm2.data, jm2.size) == 0 && (text.dirty & 1)
                && dirty_map,
            "with an unsaved edit in it, DOC_OPEN and the alias still agree");
        Editor_HostDocTextFree(&text);
        Editor_HostBlobFree(&jm2);
        Editor_HostBlobFree(&jl2);
    }

    /* ---- state is scoped to a Client -------------------------------------- */

    {
        int32_t selection[3] = { 2, 17, 33 };
        check(
            Editor_HostMapEdStateSet(&a, TORIRSMAPED_STATE_SELECTION, selection, 3) == 1,
            "the viewer publishes a selection");
    }
    drain(&a, &ma);
    drain(&a2, &ma2);
    drain(&b, &mb);
    check(ma.states_seen == 1, "the viewer got its own state echo");
    check(
        ma2.states_seen == 1 && ma2.last_state_key == TORIRSMAPED_STATE_SELECTION
            && ma2.last_state_count == 3 && ma2.last_state_values[1] == 17,
        "its controller follows the selection");
    check(mb.states_seen == 0, "the other Client never hears it");

    check(Editor_HostMapEdStateSync(&a2) == 1, "STATE_SYNC replays the group's one key");
    drain(&a2, &ma2);
    check(
        ma2.states_seen == 2 && ma2.last_state_values[2] == 33,
        "the replayed state matches");

    /* ---- save happens on the authority ------------------------------------ */

    check(Editor_HostMapEdSaveAll(&a2) == 1, "the controller saves the one dirty square");
    drain(&a, &ma);
    drain(&b, &mb);
    check(ma.saved_seen == 1 && mb.saved_seen == 1, "every Client heard what was saved");
    check(
        Editor_DocFindSquare(ma.doc, 50, 50)->dirty_map == 0
            && Editor_DocFindSquare(mb.doc, 50, 50)->dirty_map == 0,
        "mirrors cleared dirty on the broadcast");
    disk = slurp(jm2_path, &disk_size);
    check(disk && strstr(disk, "u7") != NULL, "the edit reached the disk");
    free(disk);
    disk = slurp(jl2_path, &disk_size);
    check(disk != NULL, "the untouched jl2 still exists");
    free(disk);

    /* ---- a spawn document: rows, suppressions, save ----------------------- */

    {
        struct Editor_Cmd spawn;
        struct Editor_DocHandle handle;
        char const* expect =
            "==== NPC ====\n"
            "giantspider1                                  3200  3238 0\n"
            "-goblin_unarmed_melee_4                       3202  3253 0\n";

        check(
            open_doc_into(&a, ma.doc, EDITOR_DOC_SPAWN, "m50_50") == 1,
            "a square with no edited spawn file opens an empty spawn document");
        check(
            open_doc_into(&b, mb.doc, EDITOR_DOC_SPAWN, "m50_50") == 1,
            "the other Client opens it too");

        Editor_CmdInit(&spawn, EDITOR_DOC_SPAWN, "m50_50", EDITOR_CMD_SPAWN);
        spawn.has_after = 1;
        snprintf(spawn.u.spawn.name, sizeof(spawn.u.spawn.name), "giantspider1");
        spawn.u.spawn.abs_x = 3200;
        spawn.u.spawn.abs_z = 3238;
        spawn.u.spawn.count = 1;
        check(Editor_HostMapEdCmd(&a, &spawn) == EDITOR_HOST_OK, "SPAWN add is accepted");

        Editor_CmdInit(&spawn, EDITOR_DOC_SPAWN, "m50_50", EDITOR_CMD_SPAWN_SUPPRESS);
        spawn.has_after = 1;
        snprintf(spawn.u.spawn.name, sizeof(spawn.u.spawn.name), "goblin_unarmed_melee_4");
        spawn.u.spawn.abs_x = 3202;
        spawn.u.spawn.abs_z = 3253;
        check(Editor_HostMapEdCmd(&a, &spawn) == EDITOR_HOST_OK, "SPAWN_SUPPRESS add is accepted");

        Editor_CmdInit(&spawn, EDITOR_DOC_SPAWN, "m50_50", EDITOR_CMD_SPAWN);
        spawn.has_before = 1;
        snprintf(spawn.u.spawn.name, sizeof(spawn.u.spawn.name), "nobody_here");
        check(
            Editor_HostMapEdCmd(&a, &spawn) == EDITOR_HOST_REFUSED,
            "deleting a row the file does not have is refused");

        drain(&a, &ma);
        drain(&b, &mb);
        drain(&a2, &ma2);
        drain(&c, &mc);
        check(
            Editor_DocFind(mb.doc, EDITOR_DOC_SPAWN, "m50_50", &handle)
                && Editor_SpawnRowCount(&handle.spawn->file) == 2 && handle.spawn->dirty,
            "the other Client's mirror holds both rows, dirty");
        {
            char emitted[512];
            Editor_SpawnEmit(&handle.spawn->file, emitted, sizeof(emitted));
            check(strcmp(emitted, expect) == 0, "the mirror emits the generator's row format");
        }

        check(Editor_HostMapEdSave(&b, EDITOR_DOC_SPAWN, "m50_50") == 1, "SAVE(SPAWN) writes it");
        drain(&a, &ma);
        drain(&b, &mb);
        snprintf(
            path,
            sizeof(path),
            "%s/server/scripts/areas/edited/configs/m50_50.spawn",
            scratch);
        disk = slurp(path, &disk_size);
        check(disk && strcmp(disk, expect) == 0, "the edited spawn file on disk is exact");
        free(disk);
        check(
            Editor_DocFind(ma.doc, EDITOR_DOC_SPAWN, "m50_50", &handle) && !handle.spawn->dirty,
            "FACT_SAVED (kind, key) cleared the spawn document's dirty flag everywhere");
        check(Editor_HostMapEdUndo(&a) == 1, "undo takes the suppression back");
        drain(&a, &ma);
        check(
            Editor_DocFind(ma.doc, EDITOR_DOC_SPAWN, "m50_50", &handle)
                && Editor_SpawnRowCount(&handle.spawn->file) == 1,
            "the mirror lost the suppression row");
        check(Editor_HostMapEdRedo(&a) == 1, "redo restores it");
        drain(&a, &ma);

        /* SPAWN_EDIT: the row rewritten in place, relayed, undone exactly. */
        {
            struct Editor_Cmd edit;
            struct Editor_Cmd decoded;
            struct ToriRSMapEdBuf wire;
            struct Editor_SpawnRow before;
            struct Editor_SpawnRow after;
            char emitted[512];

            memset(&before, 0, sizeof(before));
            snprintf(before.name, sizeof(before.name), "giantspider1");
            before.abs_x = 3200;
            before.abs_z = 3238;
            before.count = 1;
            after = before;
            after.abs_x = 3201;
            after.level = 1;
            check(Editor_DocFind(ma.doc, EDITOR_DOC_SPAWN, "m50_50", &handle), "a's mirror holds the spawn doc");
            Editor_SpawnEditCommand(&edit, "m50_50", &before, &after, &handle.spawn->file);
            memset(&wire, 0, sizeof(wire));
            ToriRSMapEd_CmdEncode(&edit, &wire);
            check(ToriRSMapEd_CmdDecode(ToriRSMapEd_BufPeek(&wire), (uint32_t)ToriRSMapEd_BufAvailable(&wire), &decoded)
                          == TORIRSMAPED_CMD_DECODE_OK
                      && memcmp(&decoded.u.spawn_edit, &edit.u.spawn_edit, sizeof(edit.u.spawn_edit)) == 0
                      && decoded.kind == EDITOR_CMD_SPAWN_EDIT && Editor_CmdHash(&decoded) == Editor_CmdHash(&edit),
                "SPAWN_EDIT round-trips the wire (rows and the old line's bytes)");
            ToriRSMapEd_BufFree(&wire);
            check(Editor_HostMapEdCmd(&a, &edit) == EDITOR_HOST_OK, "SPAWN_EDIT is accepted");
            check(Editor_HostMapEdCmd(&a, &edit) == EDITOR_HOST_REFUSED, "a second one finds no old row: refused");
            drain(&a, &ma);
            drain(&b, &mb);
            check(Editor_DocFind(mb.doc, EDITOR_DOC_SPAWN, "m50_50", &handle)
                      && Editor_SpawnFind(&handle.spawn->file, &after) == 1,
                "the other Client's mirror has the row rewritten on its own line");
            Editor_SpawnEmit(&handle.spawn->file, emitted, sizeof(emitted));
            check(strcmp(emitted,
                      "==== NPC ====\n"
                      "giantspider1                                  3201  3238 1\n"
                      "-goblin_unarmed_melee_4                       3202  3253 0\n")
                      == 0,
                "only that line changed");
            check(Editor_HostMapEdUndo(&a) == 1, "undo takes the edit back");
            drain(&a, &ma);
            drain(&b, &mb);
            Editor_DocFind(mb.doc, EDITOR_DOC_SPAWN, "m50_50", &handle);
            Editor_SpawnEmit(&handle.spawn->file, emitted, sizeof(emitted));
            check(strcmp(emitted, expect) == 0, "and the mirror emits the file as it was");
        }
    }

    /* ---- an overlay record block: "<type>:<name>@<overlay>" ------------------ */

    {
        struct Editor_Cmd field;
        struct Editor_DocHandle handle;
        char const* overlay = "[a]\nx=1\n\n[b]\ny=2 // note\n\n[c]\nz=3\n";
        FILE* f;

        snprintf(path, sizeof(path), "%s/server/scripts/areas/t", scratch);
        mkdir_p_or_die(path);
        snprintf(path, sizeof(path), "%s/server/scripts/areas/t/configs", scratch);
        mkdir_p_or_die(path);
        snprintf(path, sizeof(path), "%s/server/scripts/areas/t/configs/t.npc", scratch);
        f = fopen(path, "wb");
        check(f != NULL, "write the overlay fixture");
        if( f )
        {
            fputs(overlay, f);
            fclose(f);
        }
        check(open_doc_into(&a, ma.doc, EDITOR_DOC_CFG, "npc:b@areas/t/configs/t.npc") == 1
                  && Editor_DocFind(ma.doc, EDITOR_DOC_CFG, "npc:b@areas/t/configs/t.npc", &handle)
                  && handle.text->length == strlen("[b]\ny=2 // note\n\n")
                  && memcmp(handle.text->text, "[b]\ny=2 // note\n\n", handle.text->length) == 0,
            "an overlay key opens just that block of the overlay file");
        Editor_CmdInit(&field, EDITOR_DOC_CFG, "npc:b@areas/t/configs/t.npc", EDITOR_CMD_CFG_FIELD);
        field.has_before = 1;
        field.has_after = 1;
        snprintf(field.u.cfg_field.key, sizeof(field.u.cfg_field.key), "y");
        snprintf(field.u.cfg_field.before, sizeof(field.u.cfg_field.before), "2 // note");
        snprintf(field.u.cfg_field.after, sizeof(field.u.cfg_field.after), "5 // note");
        check(Editor_HostMapEdCmd(&a, &field) == EDITOR_HOST_OK, "a CFG_FIELD on the overlay block is accepted");
        check(Editor_HostMapEdSave(&a, EDITOR_DOC_CFG, "npc:b@areas/t/configs/t.npc") == 1, "SAVE writes it");
        drain(&a, &ma);
        disk = slurp(path, &disk_size);
        check(disk && strcmp(disk, "[a]\nx=1\n\n[b]\ny=5 // note\n\n[c]\nz=3\n") == 0,
            "the overlay file changed in that block only");
        free(disk);
        check(open_doc_into(&a, ma.doc, EDITOR_DOC_CFG, "npc:b@../t.npc") == 0, "an overlay path with .. is refused");
        check(Editor_HostMapEdSaveAs(&a, EDITOR_DOC_CFG, "npc:b@areas/t/configs/t.npc", "npc:b2", NULL)
                  == EDITOR_HOST_UNSUPPORTED,
            "SAVE_AS of an overlay block is unsupported");
        check(Editor_HostMapEdUndo(&a) == 1, "undo the overlay edit");
        drain(&a, &ma);
    }

    /* ---- a script document: TEXT_SPLICE ----------------------------------- */

    {
        struct Editor_Cmd splice;
        struct Editor_DocHandle handle;
        char const* body = "[opheld1,test]\nmes(\"hi\");\n";

        check(
            open_doc_into(&a, ma.doc, EDITOR_DOC_TEXT, "rs2:edited/doors/my_door.rs2") == 1,
            "a script that does not exist yet opens empty");
        Editor_CmdInit(&splice, EDITOR_DOC_TEXT, "rs2:edited/doors/my_door.rs2",
            EDITOR_CMD_TEXT_SPLICE);
        splice.u.text_splice.offset = 0;
        splice.u.text_splice.before_len = 0;
        splice.u.text_splice.after_len = (uint32_t)strlen(body);
        memcpy(splice.u.text_splice.text, body, strlen(body));
        check(Editor_HostMapEdCmd(&a, &splice) == EDITOR_HOST_OK, "an insert splice is accepted");
        drain(&a, &ma);
        check(
            Editor_DocFind(ma.doc, EDITOR_DOC_TEXT, "rs2:edited/doors/my_door.rs2", &handle)
                && strcmp(handle.text->text, body) == 0,
            "the mirror's text took the splice");

        /* A stale splice names text that is not there: refused. */
        splice.u.text_splice.offset = 1;
        splice.u.text_splice.before_len = 3;
        splice.u.text_splice.after_len = 1;
        memcpy(splice.u.text_splice.text, "zzzQ", 4);
        check(Editor_HostMapEdCmd(&a, &splice) == EDITOR_HOST_REFUSED, "a stale splice is refused");

        check(
            Editor_HostMapEdSave(&a, EDITOR_DOC_TEXT, "rs2:edited/doors/my_door.rs2") == 1,
            "SAVE(TEXT) writes the script");
        drain(&a, &ma);
        snprintf(path, sizeof(path), "%s/server/scripts/edited/doors/my_door.rs2", scratch);
        disk = slurp(path, &disk_size);
        check(disk && strcmp(disk, body) == 0, "the script landed under server/scripts/");
        free(disk);
    }

    /* ---- a config record: CFG_FIELD as a block splice --------------------- */

    {
        struct Editor_Cmd field;
        char const* before_file = "// header\n\n[door_a]\nname=Door\nop1=Open\n\n"
                                  "[door_b]\nname=Other\n";
        char const* after_file = "// header\n\n[door_a]\nname=Gate\nop1=Open\n\n"
                                 "[door_b]\nname=Other\n";

        snprintf(path, sizeof(path), "%s/configs", scratch);
        mkdir_p_or_die(path);
        snprintf(path, sizeof(path), "%s/configs/all.loc", scratch);
        write_text(path, before_file);

        check(
            open_doc_into(&a, ma.doc, EDITOR_DOC_CFG, "loc:door_a") == 1,
            "a config record opens as its block");
        {
            struct Editor_DocHandle handle;
            check(
                Editor_DocFind(ma.doc, EDITOR_DOC_CFG, "loc:door_a", &handle)
                    && strcmp(handle.text->text, "[door_a]\nname=Door\nop1=Open\n\n") == 0,
                "the block runs from its header to the next one");
        }
        Editor_CmdInit(&field, EDITOR_DOC_CFG, "loc:door_a", EDITOR_CMD_CFG_FIELD);
        snprintf(field.u.cfg_field.key, sizeof(field.u.cfg_field.key), "name");
        snprintf(field.u.cfg_field.before, sizeof(field.u.cfg_field.before), "Door");
        snprintf(field.u.cfg_field.after, sizeof(field.u.cfg_field.after), "Gate");
        check(Editor_HostMapEdCmd(&a, &field) == EDITOR_HOST_OK, "CFG_FIELD is accepted");
        check(Editor_HostMapEdCmd(&a, &field) == EDITOR_HOST_REFUSED,
            "the same edit again no longer matches and is refused");
        check(Editor_HostMapEdSave(&a, EDITOR_DOC_CFG, "loc:door_a") == 1, "SAVE(CFG) writes it");
        drain(&a, &ma);
        disk = slurp(path, &disk_size);
        check(disk && strcmp(disk, after_file) == 0,
            "only the record's block changed in all.loc");
        free(disk);
    }

    /* ---- DOC_CREATE: a new square in a lane ------------------------------- */

    {
        struct EditorHost_DocCreate create;
        struct EditorHost_DocText text;

        memset(&create, 0, sizeof(create));
        create.kind = EDITOR_DOC_SQUARE;
        snprintf(create.key, sizeof(create.key), "m50_50");
        create.template_kind = EDITOR_DOC_TEMPLATE_EMPTY;
        check(
            Editor_HostMapEdDocCreate(&a, &create) == EDITOR_HOST_REFUSED,
            "DOC_CREATE refuses a square that exists in that storage");

        snprintf(create.key, sizeof(create.key), "m51_50");
        snprintf(create.storage, sizeof(create.storage), "ported/../maps");
        check(
            Editor_HostMapEdDocCreate(&a, &create) == EDITOR_HOST_REFUSED,
            "DOC_CREATE refuses a storage that is not maps/ or a lane");

        snprintf(create.storage, sizeof(create.storage), "ported/testlane/maps");
        create.template_kind = EDITOR_DOC_TEMPLATE_FLAT;
        create.fill_height = 20;
        create.fill_underlay = 5;
        ma.cmds_seen = 0;
        check(
            Editor_HostMapEdDocCreate(&a, &create) == EDITOR_HOST_OK,
            "DOC_CREATE makes a flat square in a lane");
        drain(&a, &ma);
        check(ma.cmds_seen == 4096, "the fill arrived as 4096 tile commands");
        check(
            Editor_HostMapEdDocOpen(&b, EDITOR_DOC_SQUARE, "ported/testlane/maps/m51_50", &text)
                    == EDITOR_HOST_OK
                && strcmp(text.storage, "ported/testlane/maps") == 0 && text.dirty == 3
                && text.parts[0].data && strstr(text.parts[0].data, "0 0 0: h20 u5"),
            "another Client opens the new square: lane storage, unsaved, filled");
        Editor_HostDocTextFree(&text);
        snprintf(path, sizeof(path), "%s/ported/testlane/maps/m51_50.jm2", scratch);
        disk = slurp(path, &disk_size);
        check(disk == NULL, "nothing is written before Save");
        free(disk);
        check(
            Editor_HostMapEdDocCreate(&a, &create) == EDITOR_HOST_REFUSED,
            "creating it twice is refused");

        snprintf(create.key, sizeof(create.key), "m59_50");
        create.fill_height = 0;
        check(
            Editor_HostMapEdDocCreate(&a, &create) == EDITOR_HOST_OK,
            "DOC_CREATE makes a height 0 fill");
        drain(&a, &ma);
        check(
            Editor_HostMapEdDocOpen(&b, EDITOR_DOC_SQUARE, "ported/testlane/maps/m59_50", &text)
                    == EDITOR_HOST_OK
                && text.parts[0].data && strstr(text.parts[0].data, "0 0 0: u5"),
            "a height 0 fill has no h token: the perlin ground");
        Editor_HostDocTextFree(&text);
        create.fill_height = 20;

        create.template_kind = EDITOR_DOC_TEMPLATE_COPY;
        snprintf(create.key, sizeof(create.key), "m52_50");
        snprintf(create.storage, sizeof(create.storage), "ported/testlane/maps");
        snprintf(create.source_key, sizeof(create.source_key), "m50_50");
        check(
            Editor_HostMapEdDocCreate(&a, &create) == EDITOR_HOST_OK,
            "DOC_CREATE copies an existing square");
        drain(&a, &ma);
        {
            struct EditorHost_DocText source;
            Editor_HostMapEdDocOpen(&a, EDITOR_DOC_SQUARE, "m50_50", &source);
            Editor_HostMapEdDocOpen(&a, EDITOR_DOC_SQUARE, "ported/testlane/maps/m52_50", &text);
            check(
                text.parts[1].size == source.parts[1].size
                    && memcmp(text.parts[1].data, source.parts[1].data, source.parts[1].size)
                           == 0,
                "the copy's locs equal its source's");
            Editor_HostDocTextFree(&source);
            Editor_HostDocTextFree(&text);
        }

        check(Editor_HostMapEdSave(&a, EDITOR_DOC_SQUARE, "m51_50") == 1, "SAVE writes the new square");
        drain(&a, &ma);
        disk = slurp(path, &disk_size);
        check(disk && strstr(disk, "0 63 63: h20 u5"), "its .jm2 landed in the lane");
        free(disk);
        snprintf(path, sizeof(path), "%s/ported/testlane/maps/m51_50.filepack", scratch);
        disk = slurp(path, &disk_size);
        check(
            disk && strcmp(disk, "0=m51_50.jm2\n1=m51_50.jl2\n") == 0,
            "with its .filepack line");
        free(disk);
        snprintf(path, sizeof(path), "%s/ported/testlane/maps/m51_50.jl2", scratch);
        disk = slurp(path, &disk_size);
        check(disk != NULL, "and its (empty) .jl2");
        free(disk);
    }

    /* ---- the file lane still works ----------------------------------------- */

    {
        char const* spawns = "[npc,1234]\ncoord=0_50_50_10_10\n";
        check(
            a.vtable->spawn_save(a.user_data, 50, 50, spawns, strlen(spawns))
                == EDITOR_HOST_OK,
            "spawn_save answers OK");
        snprintf(
            path,
            sizeof(path),
            "%s/server/scripts/areas/edited/configs/m50_50.spawn",
            scratch);
        disk = slurp(path, &disk_size);
        check(disk != NULL, "the spawn file landed on disk");
        free(disk);
        check(
            a.vtable->spawn_save(a.user_data, 50, 50, NULL, 0) == EDITOR_HOST_OK,
            "an absent spawn text deletes the file");
    }

    /* ---- SAVE_AS: a config record copied to a new name ---------------------- */

    {
        char dir[1300];
        struct EditorHost_DocText text;
        struct Editor_DocHandle handle;
        int id = -1;
        char const* npc_text = "// one npc\n\n[src_npc]\nname=Src\nop1=Talk-to\n";
        char const* client_text = "// npc client half\n";
        FILE* f;

        snprintf(dir, sizeof(dir), "%s/configs", scratch);
        mkdir_p_or_die(dir);
        snprintf(dir, sizeof(dir), "%s/pack", scratch);
        mkdir_p_or_die(dir);
        snprintf(path, sizeof(path), "%s/configs/all.npc", scratch);
        f = fopen(path, "wb");
        check(f != NULL, "the npc fixture is written");
        if( f )
        {
            fputs(npc_text, f);
            fclose(f);
        }
        snprintf(path, sizeof(path), "%s/configs/all.npc.compack", scratch);
        f = fopen(path, "wb");
        if( f )
        {
            fputs("0=src_npc\n", f);
            fclose(f);
        }
        snprintf(path, sizeof(path), "%s/pack/npc.client", scratch);
        f = fopen(path, "wb");
        if( f )
        {
            fputs(client_text, f);
            fclose(f);
        }

        /* Client 2's mirror holds the membership file: it must follow. */
        check(open_doc_into(&b, mb.doc, EDITOR_DOC_TEXT, "pack:npc.client"),
            "a second Client mirrors pack:npc.client");
        check(Editor_HostMapEdSaveAs(&a, EDITOR_DOC_CFG, "npc:src_npc", "npc:copy_npc", &id)
                  == EDITOR_HOST_OK,
            "SAVE_AS copies npc:src_npc to npc:copy_npc");
        check(id == 20000, "the copy takes the npc floor id (20000)");
        drain(&b, &mb);
        check(Editor_DocFind(mb.doc, EDITOR_DOC_TEXT, "pack:npc.client", &handle) && handle.text
                  && handle.text->length == strlen(client_text) + strlen("copy_npc\n")
                  && memcmp(handle.text->text + strlen(client_text), "copy_npc\n", 9) == 0,
            "the other Client's mirror receives the membership line as FACT_CMDs");
        memset(&text, 0, sizeof(text));
        check(Editor_HostMapEdDocOpen(&a2, EDITOR_DOC_CFG, "npc:copy_npc", &text) == EDITOR_HOST_OK
                  && text.part_count == 1
                  && text.parts[0].size == strlen("[copy_npc]\nname=Src\nop1=Talk-to\n")
                  && memcmp(text.parts[0].data, "[copy_npc]\nname=Src\nop1=Talk-to\n",
                         text.parts[0].size)
                         == 0,
            "the copy opens as [copy_npc] with every field of the source");
        Editor_HostDocTextFree(&text);
        memset(&text, 0, sizeof(text));
        check(Editor_HostMapEdDocOpen(&a2, EDITOR_DOC_TEXT, "pack:npc.alloc", &text) == EDITOR_HOST_OK
                  && text.part_count == 1 && text.parts[0].data
                  && strstr((char const*)text.parts[0].data, "\n20000=copy_npc\n") != NULL,
            "the ledger holds 20000=copy_npc");
        Editor_HostDocTextFree(&text);
        check(Editor_HostMapEdSaveAs(&a, EDITOR_DOC_CFG, "npc:src_npc", "npc:copy_npc", &id)
                  == EDITOR_HOST_REFUSED,
            "a second copy onto the same name is refused");
        check(Editor_HostMapEdSaveAs(&a, EDITOR_DOC_SQUARE, "m50_50", "m50_51", &id)
                  == EDITOR_HOST_UNSUPPORTED,
            "SAVE_AS of a square is UNSUPPORTED (that is DOC_CREATE)");
        check(Editor_HostMapEdUndo(&a) > 0, "undo takes the copy back");
        drain(&b, &mb);
        check(Editor_DocFind(mb.doc, EDITOR_DOC_TEXT, "pack:npc.client", &handle) && handle.text
                  && handle.text->length == strlen(client_text),
            "and the other mirror's membership file is its old bytes");
        drain(&a, &ma);
        drain(&a2, &ma2);
        drain(&c, &mc);
    }

    /* ---- the AREAS document: open, edit, mirror, save, reopen ------------ */

    {
        struct Editor_AreasFile file;
        struct Editor_Area area;
        struct Editor_Collection* coll = calloc(1, sizeof(*coll));
        struct Editor_Cmd* cmds = calloc(4, sizeof(*cmds));
        struct Editor_DocHandle ha;
        struct Editor_DocHandle hb;
        struct EditorHost_DocText text;
        char areas_path[1300];

        if( !coll || !cmds )
        {
            fprintf(stderr, "out of memory for the areas checks\n");
            return 2;
        }
        snprintf(areas_path, sizeof(areas_path), "%s/editor/areas.txt", scratch);
        check(open_doc_into(&a, ma.doc, EDITOR_DOC_AREAS, "areas") == 1, "areas: Client 1 opens the AREAS document");
        check(open_doc_into(&b, mb.doc, EDITOR_DOC_AREAS, "areas") == 1, "areas: Client 2 opens it too");
        check(Editor_DocFind(ma.doc, EDITOR_DOC_AREAS, "areas", &ha) && ha.text->length == 0,
            "areas: no file yet = an empty document");
        memset(&text, 0, sizeof(text));
        check(Editor_HostMapEdDocOpen(&a, EDITOR_DOC_AREAS, "nope", &text) == EDITOR_HOST_REFUSED,
            "areas: any other key is refused");
        Editor_HostDocTextFree(&text);

        Editor_AreasInit(&file);
        Editor_AreasParse(&file, ha.text->text, ha.text->length);
        memset(&area, 0, sizeof(area));
        snprintf(area.name, sizeof(area.name), "Lumbridge castle");
        area.level = 0;
        area.colour = -1;
        Editor_AreaAddRect(&area, 3205, 3208, 3225, 3226);
        check(Editor_AreaSetCommand(&cmds[0], &file, NULL, &area), "areas: AREA_SET built");
        check(Editor_HostMapEdCmd(&a, &cmds[0]) == EDITOR_HOST_OK, "areas: AREA_SET is accepted");
        check(Editor_HostMapEdCmd(&a, &cmds[0]) == EDITOR_HOST_REFUSED, "areas: the same add again is refused");
        drain(&a, &ma);
        drain(&b, &mb);
        check(Editor_DocFind(mb.doc, EDITOR_DOC_AREAS, "areas", &hb)
                  && strstr(hb.text->text, "\narea Lumbridge castle | level 0 | rects 3205 3208 3225 3226\n") != NULL
                  && hb.text->dirty,
            "areas: the other Client's mirror has the row, unsaved");
        snprintf(coll->name, sizeof(coll->name), "Lumbridge");
        Editor_CollectionAdd(coll, "m50_50");
        Editor_CollectionAdd(coll, "m50_51");
        Editor_AreasParse(&file, ha.text->text, ha.text->length);
        check(Editor_CollectionSetCommand(&cmds[1], &file, NULL, coll), "areas: COLLECTION_SET built");
        check(Editor_HostMapEdCmd(&b, &cmds[1]) == EDITOR_HOST_OK, "areas: COLLECTION_SET from Client 2 is accepted");
        drain(&a, &ma);
        drain(&b, &mb);
        check(strcmp(ha.text->text, hb.text->text) == 0, "areas: both mirrors agree byte for byte");

        check(Editor_HostMapEdSave(&a, EDITOR_DOC_AREAS, "areas") == 1, "areas: SAVE writes editor/areas.txt");
        drain(&a, &ma);
        drain(&b, &mb);
        disk = slurp(areas_path, &disk_size);
        check(disk && disk_size == ha.text->length && memcmp(disk, ha.text->text, disk_size) == 0,
            "areas: the file is the document's bytes");
        free(disk);
        check(!hb.text->dirty, "areas: FACT_SAVED cleared the other mirror's dirty flag");

        /* A connection that opens it now gets the saved text. */
        memset(&text, 0, sizeof(text));
        check(Editor_HostMapEdDocOpen(&c, EDITOR_DOC_AREAS, "areas", &text) == EDITOR_HOST_OK && text.part_count == 1
                  && text.parts[0].size == ha.text->length
                  && memcmp(text.parts[0].data, ha.text->text, ha.text->length) == 0,
            "areas: a late joiner reads the same text");
        Editor_HostDocTextFree(&text);

        /* Remove and undo: the row comes back where it stood. */
        Editor_AreasParse(&file, ha.text->text, ha.text->length);
        check(Editor_AreaRemoveCommand(&cmds[2], &file, "Lumbridge castle"), "areas: AREA_REMOVE built");
        check(Editor_HostMapEdCmd(&a, &cmds[2]) == EDITOR_HOST_OK, "areas: AREA_REMOVE is accepted");
        drain(&a, &ma);
        check(strstr(ha.text->text, "area Lumbridge castle") == NULL, "areas: removed");
        check(Editor_HostMapEdUndo(&a) == 1, "areas: undo is one step");
        drain(&a, &ma);
        disk = slurp(areas_path, &disk_size);
        check(disk && disk_size == ha.text->length && memcmp(disk, ha.text->text, disk_size) == 0,
            "areas: the undo restored the saved bytes exactly");
        free(disk);

        /* Every record removed: the header goes with them and the save
         * removes the file (absent == no areas). */
        Editor_AreasParse(&file, ha.text->text, ha.text->length);
        Editor_AreaRemoveCommand(&cmds[2], &file, "Lumbridge castle");
        Editor_CollectionRemoveCommand(&cmds[3], &file, "Lumbridge");
        check(Editor_HostMapEdCmd(&a, &cmds[2]) == EDITOR_HOST_OK && Editor_HostMapEdCmd(&a, &cmds[3]) == EDITOR_HOST_OK,
            "areas: both removes are accepted");
        drain(&a, &ma);
        check(ha.text->length == 0, "areas: the document is empty again");
        check(Editor_HostMapEdSave(&a, EDITOR_DOC_AREAS, "areas") == 1, "areas: saved empty");
        disk = slurp(areas_path, &disk_size);
        check(disk == NULL, "areas: the empty document removed the file");
        free(disk);
        drain(&a, &ma);
        drain(&b, &mb);
        drain(&a2, &ma2);
        drain(&c, &mc);
        Editor_AreasFree(&file);
        free(coll);
        free(cmds);
    }

    /* ---- a v1 peer on the same protocol ------------------------------------ */

    test_v1_peer(scratch);
    test_plain_open_on_lane(scratch);

    /* ---- a second SERVER over the same tree is read-only ------------------- */

    {
        struct EditorHost d;
        check(
            Editor_HostOpenMapEdEmbed(&d, scratch, NULL) == 1,
            "a second server starts (read-only is a state, not a failure)");
        check(!Editor_HostMapEdWritable(&d), "it found the tree locked");
        Editor_HostClose(&d);
    }

    /* ---- teardown ----------------------------------------------------------- */

    /* Peers borrow a's embed, so a closes last. */
    Editor_HostClose(&a2);
    Editor_HostClose(&b);
    Editor_HostClose(&c);
    Editor_HostClose(&a);

    snprintf(path, sizeof(path), "%s/.editor-session.lock", scratch);
    disk = slurp(path, &disk_size);
    check(disk == NULL, "no lock file is stranded after close");
    free(disk);

    Editor_DocFree(ma.doc);
    Editor_DocFree(mb.doc);
    Editor_DocFree(mc.doc);
    free(ma.doc);
    free(mb.doc);
    free(mc.doc);

    printf("maped_embed_test: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
