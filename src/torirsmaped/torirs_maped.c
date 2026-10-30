/*
 * ToriRSMapEd's core: intents in, the authoritative document mutated, facts
 * out — to the requester and, for document changes, to every session.
 *
 * The file operations are editor_host_local.c doing exactly what it did when
 * the client called it directly; this file moves the call across a wire and
 * puts the document between the wire and the disk. What is worth reading is
 * the framing loop in session_pump — the one place a malformed stream can be
 * noticed, and it must end the session rather than resync, because a
 * length-prefixed stream with a bad length has no resync point.
 */

#include "torirs_maped.h"
#include "torirs_maped_catalog.h"
#include "torirs_maped_view.h"

#include "editor/editor_jm2.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

/* Views (torirs_maped_view.c, compiled into this unit at its end). */
static int
view_state_set_intercept(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped,
    uint32_t key);
static void
view_session_gone(
    struct ToriRSMapEd* maped,
    struct ToriRSMapEdSession* session);
static void
handle_spawn_view(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped,
    const uint8_t* body,
    uint32_t length);

/* ------------------------------------------------------------------ */
/* Server lifecycle                                                    */
/* ------------------------------------------------------------------ */

void
ToriRSMapEd_Open(
    struct ToriRSMapEd* maped,
    const char* content_dir,
    const char* repo_root)
{
    assert(maped);
    assert(content_dir);

    memset(maped, 0, sizeof(*maped));
    Editor_HostOpenLocal(&maped->files, content_dir, repo_root);
    maped->open = 1;

    /* The document never derives here — deriving needs the cache profile,
     * which lives in the clients. NULL profile spells exactly that. */
    Editor_DocInit(&maped->doc, NULL);
    Editor_UndoInit(&maped->undo);

    /* The server is the single writer, so the tree lock is its to hold for
     * its whole lifetime — not something clients take turns at. */
    maped->writable =
        maped->files.vtable->session(maped->files.user_data, 1) == EDITOR_HOST_OK;
    if( !maped->writable )
        TORIRS_LOG("torirsmaped: another server holds %s — serving read-only\n",
            content_dir);

    /* The catalog reads nothing until its first query. */
    maped->catalog = ToriRSMapEdCatalog_Create(content_dir);
    maped->capabilities |= TORIRSMAPED_CAP_CATALOG;
}

void
ToriRSMapEd_Close(struct ToriRSMapEd* maped)
{
    if( !maped )
        return;

    if( maped->open )
    {
        maped->files.vtable->session(maped->files.user_data, 0);
        Editor_HostClose(&maped->files);
        Editor_DocFree(&maped->doc);
        Editor_UndoFree(&maped->undo);
        ToriRSMapEdCatalog_Free(maped->catalog);
        maped->catalog = NULL;
        maped->open = 0;
    }
}

void
ToriRSMapEd_SessionInit(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped,
    const struct ToriRSMapEdTransport* transport)
{
    int slot = -1;

    assert(session);
    assert(maped);
    assert(transport);

    for( int i = 0; i < TORIRSMAPED_SESSION_MAX; i++ )
    {
        if( !maped->sessions[i] )
        {
            slot = i;
            break;
        }
    }
    assert(slot >= 0);

    memset(session, 0, sizeof(*session));
    session->transport = *transport;
    session->alive = 1;
    session->maped = maped;
    maped->sessions[slot] = session;
}

int
ToriRSMapEd_SessionAlive(const struct ToriRSMapEdSession* session)
{
    assert(session);
    return session->alive;
}

void
ToriRSMapEd_SessionFree(struct ToriRSMapEdSession* session)
{
    if( !session )
        return;

    if( session->maped )
    {
        /* Its camera key goes with it (torirs_maped_view.h). */
        view_session_gone(session->maped, session);
        for( int i = 0; i < TORIRSMAPED_SESSION_MAX; i++ )
        {
            if( session->maped->sessions[i] == session )
                session->maped->sessions[i] = NULL;
        }
        session->maped = NULL;
    }
    ToriRSMapEd_BufFree(&session->in);
    session->alive = 0;
}

static int
send_frame(
    struct ToriRSMapEdSession* session,
    uint32_t type,
    const uint8_t* payload,
    uint32_t length)
{
    uint8_t header[TORIRSMAPED_FRAME_HEADER];

    assert(session);
    assert(payload || length == 0);

    ToriRSMapEd_WriteU32(header, type);
    ToriRSMapEd_WriteU32(header + 4, length);

    if( session->transport.send(session->transport.ctx, header, sizeof(header)) < 0 )
    {
        session->alive = 0;
        return 0;
    }
    if( length > 0
        && session->transport.send(session->transport.ctx, payload, (int)length) < 0 )
    {
        session->alive = 0;
        return 0;
    }
    return 1;
}

static void
send_status_extra(
    struct ToriRSMapEdSession* session,
    uint32_t request,
    enum EditorHost_Status status,
    uint32_t extra)
{
    uint8_t payload[12];

    ToriRSMapEd_WriteU32(payload, request);
    ToriRSMapEd_WriteU32(payload + 4, (uint32_t)status);
    ToriRSMapEd_WriteU32(payload + 8, extra);
    send_frame(session, TORIRSMAPED_FACT_STATUS, payload, sizeof(payload));
}

static void
send_status(
    struct ToriRSMapEdSession* session,
    uint32_t request,
    enum EditorHost_Status status)
{
    send_status_extra(session, request, status, 0);
}

/** A state fact to one Client's connections only. Selection and tool are a
 *  session's own business; another Client editing the same world never
 *  hears them. */
static void
broadcast_group(
    struct ToriRSMapEd* maped,
    uint32_t client_id,
    uint32_t type,
    const uint8_t* payload,
    uint32_t length)
{
    for( int i = 0; i < TORIRSMAPED_SESSION_MAX; i++ )
    {
        struct ToriRSMapEdSession* session = maped->sessions[i];
        if( session && session->alive && session->client_id == client_id )
            send_frame(session, type, payload, length);
    }
}

static void
send_buf(
    struct ToriRSMapEdSession* session,
    uint32_t type,
    const struct ToriRSMapEdBuf* payload)
{
    send_frame(
        session,
        type,
        ToriRSMapEd_BufPeek(payload),
        (uint32_t)ToriRSMapEd_BufAvailable(payload));
}

/**
 * One document change to every session, each in the form it speaks: v2
 * peers get the kind-tagged encoding, v1 peers the 144-byte square form —
 * and nothing at all for a command v1 cannot express (a spawn row, a text
 * splice), since a v1 mirror holds no such document.
 */
static void
broadcast_cmd(
    struct ToriRSMapEd* maped,
    const struct Editor_Cmd* command,
    enum Editor_CmdDirection direction)
{
    struct ToriRSMapEdBuf v2;
    uint8_t v1[8 + TORIRSMAPED_CMD_WIRE_V1];
    int has_v1;

    maped->seq++;
    memset(&v2, 0, sizeof(v2));
    ToriRSMapEd_PutU32(&v2, maped->seq);
    ToriRSMapEd_PutU32(&v2, (uint32_t)direction);
    ToriRSMapEd_CmdEncode(command, &v2);

    ToriRSMapEd_WriteU32(v1, maped->seq);
    ToriRSMapEd_WriteU32(v1 + 4, (uint32_t)direction);
    has_v1 = ToriRSMapEd_CmdEncodeV1(command, v1 + 8);

    for( int i = 0; i < TORIRSMAPED_SESSION_MAX; i++ )
    {
        struct ToriRSMapEdSession* session = maped->sessions[i];
        if( !session || !session->alive )
            continue;
        if( session->version >= TORIRSMAPED_PROTO_VERSION )
            send_buf(session, TORIRSMAPED_FACT_CMD, &v2);
        else if( has_v1 )
            send_frame(session, TORIRSMAPED_FACT_CMD, v1, sizeof(v1));
    }
    ToriRSMapEd_BufFree(&v2);
}

/**
 * What landed on disk, to every session in its own form: v2 rows of
 * (kind, key), or for v1 peers the squares among them as coordinates (and
 * nothing when no square was saved — a v1 mirror holds only squares).
 */
static void
broadcast_saved(
    struct ToriRSMapEd* maped,
    const struct Editor_DocRef* rows,
    int count)
{
    struct ToriRSMapEdBuf v2;
    struct ToriRSMapEdBuf v1;
    uint32_t squares = 0;

    if( count <= 0 )
        return;

    memset(&v2, 0, sizeof(v2));
    memset(&v1, 0, sizeof(v1));
    ToriRSMapEd_PutU32(&v2, (uint32_t)count);
    for( int i = 0; i < count; i++ )
    {
        ToriRSMapEd_PutU32(&v2, (uint32_t)rows[i].kind);
        ToriRSMapEd_PutString(&v2, rows[i].key);
        if( rows[i].kind == EDITOR_DOC_SQUARE )
            squares++;
    }
    ToriRSMapEd_PutU32(&v1, squares);
    for( int i = 0; i < count; i++ )
    {
        int map_x;
        int map_z;
        if( rows[i].kind != EDITOR_DOC_SQUARE
            || !Editor_SquareKeyParse(rows[i].key, &map_x, &map_z) )
            continue;
        ToriRSMapEd_PutU32(&v1, (uint32_t)map_x);
        ToriRSMapEd_PutU32(&v1, (uint32_t)map_z);
    }

    for( int i = 0; i < TORIRSMAPED_SESSION_MAX; i++ )
    {
        struct ToriRSMapEdSession* session = maped->sessions[i];
        if( !session || !session->alive )
            continue;
        if( session->version >= TORIRSMAPED_PROTO_VERSION )
            send_buf(session, TORIRSMAPED_FACT_SAVED, &v2);
        else if( squares > 0 )
            send_buf(session, TORIRSMAPED_FACT_SAVED, &v1);
    }
    ToriRSMapEd_BufFree(&v2);
    ToriRSMapEd_BufFree(&v1);
}

static void
handle_square_list(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped)
{
    int count = 0;
    int* coords = NULL;
    enum EditorHost_Status status;
    uint8_t* payload;
    uint32_t length;

    /* Probe for the total, then fetch it whole: the reply names every square
     * or none, never a silent prefix. */
    status = maped->files.vtable->square_list(maped->files.user_data, NULL, 0, &count);
    if( status == EDITOR_HOST_OK && count > 0 )
    {
        coords = ToriMem_Alloc(ToriMem_Editor(), (size_t)count * 2 * sizeof(*coords));
        status = maped->files.vtable->square_list(
            maped->files.user_data, coords, count, &count);
    }
    if( status != EDITOR_HOST_OK )
        count = 0;

    length = 8 + (uint32_t)count * 8;
    payload = ToriMem_Alloc(ToriMem_Editor(), length);
    ToriRSMapEd_WriteU32(payload, (uint32_t)status);
    ToriRSMapEd_WriteU32(payload + 4, (uint32_t)count);
    for( int i = 0; i < count; i++ )
    {
        ToriRSMapEd_WriteU32(payload + 8 + (uint32_t)i * 8, (uint32_t)coords[i * 2]);
        ToriRSMapEd_WriteU32(payload + 12 + (uint32_t)i * 8, (uint32_t)coords[i * 2 + 1]);
    }
    send_frame(session, TORIRSMAPED_FACT_SQUARE_LIST, payload, length);
    ToriMem_Free(ToriMem_Editor(), payload);
    ToriMem_Free(ToriMem_Editor(), coords);
}

static void
handle_square_load(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped,
    const uint8_t* body,
    uint32_t length)
{
    int map_x;
    int map_z;
    struct EditorHost_Blob jm2 = { NULL, 0 };
    struct EditorHost_Blob jl2 = { NULL, 0 };
    enum EditorHost_Status status;
    uint32_t jm2_plus;
    uint32_t jl2_plus;
    uint8_t* payload;
    uint32_t total;

    if( length != 8 )
    {
        session->alive = 0;
        return;
    }
    map_x = (int)ToriRSMapEd_ReadU32(body);
    map_z = (int)ToriRSMapEd_ReadU32(body + 4);

    status = maped->files.vtable->square_load(
        maped->files.user_data, map_x, map_z, &jm2, &jl2);

    jm2_plus = jm2.data ? (uint32_t)jm2.size + 1 : 0;
    jl2_plus = jl2.data ? (uint32_t)jl2.size + 1 : 0;

    total = 12 + (uint32_t)jm2.size + (uint32_t)jl2.size;
    payload = ToriMem_Alloc(ToriMem_Editor(), total);
    ToriRSMapEd_WriteU32(payload, (uint32_t)status);
    ToriRSMapEd_WriteU32(payload + 4, jm2_plus);
    ToriRSMapEd_WriteU32(payload + 8, jl2_plus);
    if( jm2.size > 0 )
        memcpy(payload + 12, jm2.data, jm2.size);
    if( jl2.size > 0 )
        memcpy(payload + 12 + jm2.size, jl2.data, jl2.size);

    send_frame(session, TORIRSMAPED_FACT_SQUARE_LOAD, payload, total);
    ToriMem_Free(ToriMem_Editor(), payload);
    Editor_HostBlobFree(&jm2);
    Editor_HostBlobFree(&jl2);
}

static void
handle_square_save(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped,
    const uint8_t* body,
    uint32_t length)
{
    int map_x;
    int map_z;
    uint32_t jm2_plus;
    uint32_t jl2_plus;
    size_t jm2_size;
    size_t jl2_size;
    const char* jm2_text = NULL;
    const char* jl2_text = NULL;
    enum EditorHost_Status status;

    if( length < 16 )
    {
        session->alive = 0;
        return;
    }
    map_x = (int)ToriRSMapEd_ReadU32(body);
    map_z = (int)ToriRSMapEd_ReadU32(body + 4);
    jm2_plus = ToriRSMapEd_ReadU32(body + 8);
    jl2_plus = ToriRSMapEd_ReadU32(body + 12);
    jm2_size = jm2_plus > 0 ? jm2_plus - 1 : 0;
    jl2_size = jl2_plus > 0 ? jl2_plus - 1 : 0;
    if( 16 + jm2_size + jl2_size != length )
    {
        session->alive = 0;
        return;
    }
    /* The text halves are not NUL-terminated on the wire; the file layer
     * takes (pointer, length) and never treats them as C strings. An absent
     * half stays NULL — that is the "leave this file alone" signal. */
    if( jm2_plus > 0 )
        jm2_text = (const char*)body + 16;
    if( jl2_plus > 0 )
        jl2_text = (const char*)body + 16 + jm2_size;

    status = maped->files.vtable->square_save(
        maped->files.user_data, map_x, map_z, jm2_text, jm2_size, jl2_text, jl2_size);
    send_status(session, TORIRSMAPED_REQ_SQUARE_SAVE, status);
}

static void
handle_spawn_save(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped,
    const uint8_t* body,
    uint32_t length)
{
    int map_x;
    int map_z;
    uint32_t size_plus;
    size_t size;
    const char* text = NULL;
    enum EditorHost_Status status;

    if( length < 12 )
    {
        session->alive = 0;
        return;
    }
    map_x = (int)ToriRSMapEd_ReadU32(body);
    map_z = (int)ToriRSMapEd_ReadU32(body + 4);
    size_plus = ToriRSMapEd_ReadU32(body + 8);
    size = size_plus > 0 ? size_plus - 1 : 0;
    if( 12 + size != length )
    {
        session->alive = 0;
        return;
    }
    if( size_plus > 0 )
        text = (const char*)body + 12;

    status = maped->files.vtable->spawn_save(
        maped->files.user_data, map_x, map_z, text, size);
    send_status(session, TORIRSMAPED_REQ_SPAWN_SAVE, status);
}

static void
bake_progress_line(
    void* user_data,
    const char* line)
{
    struct ToriRSMapEdSession* session = user_data;

    assert(session);
    assert(line);

    send_frame(
        session, TORIRSMAPED_FACT_BAKE_LINE, (const uint8_t*)line, (uint32_t)strlen(line));
}

static void
handle_bake(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped)
{
    enum EditorHost_Status status;
    uint8_t payload[4];

    status = maped->files.vtable->bake(maped->files.user_data, bake_progress_line, session);

    ToriRSMapEd_WriteU32(payload, (uint32_t)status);
    send_frame(session, TORIRSMAPED_FACT_BAKE_DONE, payload, sizeof(payload));
}

/* ------------------------------------------------------------------ */
/* Document handlers                                                   */
/* ------------------------------------------------------------------ */

/** "m50_50" or "ported/<lane>/maps/m50_50": the key the file layer takes. */
static void
square_file_key(
    char* out,
    size_t out_size,
    const char* storage,
    const char* key)
{
    if( storage[0] )
        snprintf(out, out_size, "%s/%s", storage, key);
    else
        snprintf(out, out_size, "%s", key);
}

/**
 * Open a square in the AUTHORITY, reading it from `storage` when it is not
 * open yet. `storage` NULL means "whatever is open, else maps/" — the v1
 * alias, which has no storage to name.
 *
 * REFUSED when the square is open from a DIFFERENT storage: the document is
 * keyed by coordinates (one world, one square per place), so a lane's copy
 * and the tree's cannot both be open.
 */
static enum EditorHost_Status
authority_open_square(
    struct ToriRSMapEd* maped,
    const char* storage,
    int map_x,
    int map_z,
    struct Editor_Square** out_square)
{
    struct Editor_Square* square;
    char key[EDITOR_DOC_KEY_MAX];
    char file_key[EDITOR_DOC_KEY_MAX + EDITOR_DOC_STORAGE_MAX];
    struct EditorHost_Blob jm2 = { NULL, 0 };
    struct EditorHost_Blob jl2 = { NULL, 0 };
    enum EditorHost_Status status;

    *out_square = NULL;
    square = Editor_DocFindSquare(&maped->doc, map_x, map_z);
    if( square && square->loaded )
    {
        if( storage && strcmp(Editor_DocSquareStorage(&maped->doc, square), storage) != 0 )
            return EDITOR_HOST_REFUSED;
        *out_square = square;
        return EDITOR_HOST_OK;
    }
    if( !square )
        square = Editor_DocOpenSquare(&maped->doc, map_x, map_z);
    if( !square )
        return EDITOR_HOST_IO_ERROR;
    Editor_DocSquareSetStorage(&maped->doc, square, storage ? storage : "");
    *out_square = square;

    Editor_SquareKey(key, map_x, map_z);
    square_file_key(
        file_key, sizeof(file_key), Editor_DocSquareStorage(&maped->doc, square), key);
    status = maped->files.vtable->doc_load(
        maped->files.user_data, EDITOR_DOC_SQUARE, file_key, 0, &jm2);
    if( status == EDITOR_HOST_OK )
    {
        struct Editor_ParseResult result;

        /* A square with no scenery ships no `.jl2`; absent is data. */
        maped->files.vtable->doc_load(
            maped->files.user_data, EDITOR_DOC_SQUARE, file_key, 1, &jl2);
        result = Editor_Jm2Parse(square, jm2.data, jm2.size);
        if( result.status != EDITOR_PARSE_OK )
        {
            TORIRS_ERR("torirsmaped: %s.jm2 line %d: parse error %d\n",
                file_key,
                result.line,
                (int)result.status);
            status = EDITOR_HOST_IO_ERROR;
        }
        else if( jl2.data && jl2.size > 0 )
        {
            result = Editor_Jl2Parse(square, jl2.data, jl2.size);
            if( result.status != EDITOR_PARSE_OK )
            {
                TORIRS_ERR("torirsmaped: %s.jl2 line %d: parse error %d\n",
                    file_key,
                    result.line,
                    (int)result.status);
                status = EDITOR_HOST_IO_ERROR;
            }
        }
        if( status == EDITOR_HOST_OK )
            square->loaded = 1;
    }
    Editor_HostBlobFree(&jm2);
    Editor_HostBlobFree(&jl2);
    return status;
}

/** The square's current text, both halves, NUL-terminated; free with
 *  ToriMem_Free. The reply is the DOCUMENT's state, not the file's: a square
 *  another client already edited answers with those edits in it, so a
 *  late-joining mirror starts identical to the authority. Emit round-trips
 *  byte-exact, so on first open this equals the file anyway. */
static void
emit_square(
    const struct Editor_Square* square,
    char** out_jm2,
    size_t* out_jm2_size,
    char** out_jl2,
    size_t* out_jl2_size)
{
    *out_jm2_size = Editor_Jm2Emit(square, NULL, 0);
    *out_jm2 = ToriMem_Alloc(ToriMem_Editor(), *out_jm2_size + 1);
    Editor_Jm2Emit(square, *out_jm2, *out_jm2_size + 1);

    *out_jl2_size = Editor_Jl2Emit(square, NULL, 0);
    *out_jl2 = ToriMem_Alloc(ToriMem_Editor(), *out_jl2_size + 1);
    Editor_Jl2Emit(square, *out_jl2, *out_jl2_size + 1);
}

/** The v1 alias of DOC_OPEN(SQUARE): same document, the v1 answer layout. */
static void
handle_square_open(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped,
    const uint8_t* body,
    uint32_t length)
{
    int map_x;
    int map_z;
    struct Editor_Square* square = NULL;
    enum EditorHost_Status status;
    char* jm2_text = NULL;
    char* jl2_text = NULL;
    size_t jm2_size = 0;
    size_t jl2_size = 0;
    uint8_t* payload;
    uint32_t total;

    if( length != 8 )
    {
        session->alive = 0;
        return;
    }
    map_x = (int)ToriRSMapEd_ReadU32(body);
    map_z = (int)ToriRSMapEd_ReadU32(body + 4);

    status = authority_open_square(maped, NULL, map_x, map_z, &square);
    if( status == EDITOR_HOST_OK )
        emit_square(square, &jm2_text, &jm2_size, &jl2_text, &jl2_size);

    total = (uint32_t)(28 + jm2_size + jl2_size);
    payload = ToriMem_Alloc(ToriMem_Editor(), total);
    ToriRSMapEd_WriteU32(payload, (uint32_t)status);
    ToriRSMapEd_WriteU32(payload + 4, (uint32_t)map_x);
    ToriRSMapEd_WriteU32(payload + 8, (uint32_t)map_z);
    ToriRSMapEd_WriteU32(payload + 12, square ? (uint32_t)square->dirty_map : 0);
    ToriRSMapEd_WriteU32(payload + 16, square ? (uint32_t)square->dirty_loc : 0);
    ToriRSMapEd_WriteU32(payload + 20, jm2_text ? (uint32_t)jm2_size + 1 : 0);
    ToriRSMapEd_WriteU32(payload + 24, jl2_text ? (uint32_t)jl2_size + 1 : 0);
    if( jm2_size > 0 )
        memcpy(payload + 28, jm2_text, jm2_size);
    if( jl2_size > 0 )
        memcpy(payload + 28 + jm2_size, jl2_text, jl2_size);
    send_frame(session, TORIRSMAPED_FACT_SQUARE_TEXT, payload, total);
    ToriMem_Free(ToriMem_Editor(), payload);
    ToriMem_Free(ToriMem_Editor(), jm2_text);
    ToriMem_Free(ToriMem_Editor(), jl2_text);
}

/** Read a SPAWN or text document's file into the authority, once. */
static enum EditorHost_Status
authority_load_doc(
    struct ToriRSMapEd* maped,
    struct Editor_DocHandle* handle,
    const char* key)
{
    struct EditorHost_Blob blob = { NULL, 0 };
    enum EditorHost_Status status;

    if( Editor_DocHandleLoaded(handle) )
        return EDITOR_HOST_OK;

    status = maped->files.vtable->doc_load(
        maped->files.user_data, handle->kind, key, 0, &blob);
    /* No file yet is a document that is empty so far — an edited spawn file
     * the square never had, a record or script about to be written. */
    if( status == EDITOR_HOST_ABSENT )
        status = EDITOR_HOST_OK;
    if( status != EDITOR_HOST_OK )
        return status;

    if( handle->spawn )
    {
        struct Editor_SpawnParseResult result =
            Editor_SpawnParse(&handle->spawn->file, blob.data, blob.size);
        if( !result.ok )
        {
            TORIRS_ERR("torirsmaped: spawn %s line %d does not parse\n", key, result.line);
            Editor_SpawnFree(&handle->spawn->file);
            Editor_HostBlobFree(&blob);
            return EDITOR_HOST_IO_ERROR;
        }
        handle->spawn->loaded = 1;
    }
    else
    {
        assert(handle->text);
        Editor_TextDocSet(handle->text, blob.data, blob.size);
    }
    Editor_HostBlobFree(&blob);
    return EDITOR_HOST_OK;
}

static void
send_doc_text(
    struct ToriRSMapEdSession* session,
    enum EditorHost_Status status,
    enum Editor_DocKind kind,
    const char* key,
    const char* storage,
    uint32_t dirty,
    int part_count,
    const char* const* parts,
    const size_t* sizes)
{
    struct ToriRSMapEdBuf payload;

    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutU32(&payload, (uint32_t)status);
    ToriRSMapEd_PutU32(&payload, (uint32_t)kind);
    ToriRSMapEd_PutString(&payload, key);
    ToriRSMapEd_PutString(&payload, storage);
    ToriRSMapEd_PutU32(&payload, dirty);
    ToriRSMapEd_PutU32(&payload, (uint32_t)part_count);
    for( int i = 0; i < part_count; i++ )
    {
        ToriRSMapEd_PutU32(&payload, parts[i] ? (uint32_t)sizes[i] + 1 : 0);
        if( parts[i] && sizes[i] > 0 )
            ToriRSMapEd_BufWrite(&payload, (const uint8_t*)parts[i], (int)sizes[i]);
    }
    send_buf(session, TORIRSMAPED_FACT_DOC_TEXT, &payload);
    ToriRSMapEd_BufFree(&payload);
}

static void
handle_doc_open(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped,
    const uint8_t* body,
    uint32_t length)
{
    struct ToriRSMapEdReader reader;
    uint32_t kind;
    char key[EDITOR_DOC_KEY_MAX + EDITOR_DOC_STORAGE_MAX];

    ToriRSMapEd_ReaderInit(&reader, body, length);
    kind = ToriRSMapEd_GetU32(&reader);
    ToriRSMapEd_GetString(&reader, key, sizeof(key));
    if( !ToriRSMapEd_ReaderDone(&reader) )
    {
        session->alive = 0;
        return;
    }
    if( kind >= EDITOR_DOC_KIND_COUNT )
    {
        send_doc_text(session, EDITOR_HOST_REFUSED, EDITOR_DOC_TEXT, "", "", 0, 0, NULL, NULL);
        return;
    }

    if( kind == EDITOR_DOC_SQUARE )
    {
        char storage[EDITOR_DOC_STORAGE_MAX];
        char bare[EDITOR_DOC_KEY_MAX];
        int map_x = 0;
        int map_z = 0;
        struct Editor_Square* square = NULL;
        enum EditorHost_Status status = EDITOR_HOST_REFUSED;
        char* texts[2] = { NULL, NULL };
        size_t sizes[2] = { 0, 0 };

        if( Editor_DocSplitSquareKey(key, storage, bare) )
        {
            Editor_SquareKeyParse(bare, &map_x, &map_z);
            status = authority_open_square(maped, storage, map_x, map_z, &square);
        }
        else
            snprintf(bare, sizeof(bare), "%.*s", (int)sizeof(bare) - 1, key);
        if( status == EDITOR_HOST_OK )
            emit_square(square, &texts[0], &sizes[0], &texts[1], &sizes[1]);
        send_doc_text(
            session,
            status,
            EDITOR_DOC_SQUARE,
            bare,
            square ? Editor_DocSquareStorage(&maped->doc, square) : "",
            square ? (uint32_t)((square->dirty_map ? 1 : 0) | (square->dirty_loc ? 2 : 0)) : 0,
            status == EDITOR_HOST_OK ? 2 : 0,
            (const char* const*)texts,
            sizes);
        ToriMem_Free(ToriMem_Editor(), texts[0]);
        ToriMem_Free(ToriMem_Editor(), texts[1]);
        return;
    }

    {
        struct Editor_DocHandle handle;
        enum EditorHost_Status status;
        char* text = NULL;
        size_t size = 0;
        const char* part;

        if( strlen(key) >= EDITOR_DOC_KEY_MAX
            || !Editor_DocOpen(&maped->doc, (enum Editor_DocKind)kind, key, &handle) )
        {
            key[EDITOR_DOC_KEY_MAX - 1] = '\0';
            send_doc_text(
                session, EDITOR_HOST_REFUSED, (enum Editor_DocKind)kind, key, "", 0, 0, NULL, NULL);
            return;
        }
        status = authority_load_doc(maped, &handle, key);
        if( status == EDITOR_HOST_OK && handle.spawn )
        {
            size = Editor_SpawnEmit(&handle.spawn->file, NULL, 0);
            text = ToriMem_Alloc(ToriMem_Editor(), size + 1);
            Editor_SpawnEmit(&handle.spawn->file, text, size + 1);
        }
        part = handle.spawn ? text : (handle.text ? handle.text->text : NULL);
        if( handle.text )
            size = handle.text->length;
        send_doc_text(
            session,
            status,
            (enum Editor_DocKind)kind,
            key,
            "",
            Editor_DocHandleDirty(&handle) ? 1u : 0u,
            status == EDITOR_HOST_OK ? 1 : 0,
            &part,
            &size);
        ToriMem_Free(ToriMem_Editor(), text);
    }
}

static void
handle_doc_close(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped,
    const uint8_t* body,
    uint32_t length)
{
    struct ToriRSMapEdReader reader;
    uint32_t kind;
    char key[EDITOR_DOC_KEY_MAX];
    struct Editor_DocHandle handle;

    ToriRSMapEd_ReaderInit(&reader, body, length);
    kind = ToriRSMapEd_GetU32(&reader);
    ToriRSMapEd_GetString(&reader, key, sizeof(key));
    if( !ToriRSMapEd_ReaderDone(&reader) )
    {
        session->alive = 0;
        return;
    }
    /* The authority keeps the document: its undo history may still name it
     * and another connection may be looking at it. Unloading on the last
     * close (plus a linger) is the design's; until then a close is a
     * statement of interest only. */
    send_status(
        session,
        TORIRSMAPED_REQ_DOC_CLOSE,
        kind < EDITOR_DOC_KIND_COUNT
                && Editor_DocFind(&maped->doc, (enum Editor_DocKind)kind, key, &handle)
            ? EDITOR_HOST_OK
            : EDITOR_HOST_ABSENT);
}

static void
handle_cmd(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped,
    const uint8_t* body,
    uint32_t length)
{
    struct Editor_Cmd command;
    struct Editor_DocHandle handle;
    int v1 = session->version < TORIRSMAPED_PROTO_VERSION;

    if( v1 )
    {
        if( !ToriRSMapEd_CmdDecodeV1(body, length, &command) )
        {
            session->alive = 0;
            return;
        }
    }
    else
    {
        enum ToriRSMapEdCmdDecodeResult decoded =
            ToriRSMapEd_CmdDecode(body, length, &command);
        if( decoded == TORIRSMAPED_CMD_DECODE_MALFORMED )
        {
            session->alive = 0;
            return;
        }
        /* Well-framed but from a newer build: nothing here can apply it. */
        if( decoded == TORIRSMAPED_CMD_DECODE_UNKNOWN_KIND )
        {
            send_status(session, TORIRSMAPED_REQ_CMD, EDITOR_HOST_REFUSED);
            return;
        }
    }

    /* An edit against a document the authority never opened is a client
     * bug, but a REMOTE one — refuse it, don't die on it. */
    if( !Editor_DocFind(&maped->doc, command.doc_kind, command.doc_key, &handle)
        || !Editor_DocHandleLoaded(&handle) )
    {
        send_status(session, TORIRSMAPED_REQ_CMD, EDITOR_HOST_ABSENT);
        return;
    }
    if( !Editor_CmdApply(&maped->doc, &command, EDITOR_CMD_FORWARD) )
    {
        send_status(
            session, TORIRSMAPED_REQ_CMD, v1 ? EDITOR_HOST_IO_ERROR : EDITOR_HOST_REFUSED);
        return;
    }
    Editor_UndoPush(&maped->undo, &command);

    /* Broadcast before the ack: the requester's mirror must have the echo in
     * hand by the time its intent call returns. */
    broadcast_cmd(maped, &command, EDITOR_CMD_FORWARD);
    send_status(session, TORIRSMAPED_REQ_CMD, EDITOR_HOST_OK);
}

struct history_ctx
{
    struct ToriRSMapEd* maped;
    enum Editor_CmdDirection direction;
};

static void
history_step_broadcast(
    void* user_data,
    const struct Editor_Cmd* command)
{
    struct history_ctx* ctx = user_data;
    broadcast_cmd(ctx->maped, command, ctx->direction);
}

static void
handle_history(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped,
    uint32_t request)
{
    struct history_ctx ctx;
    int count;

    ctx.maped = maped;
    if( request == TORIRSMAPED_REQ_UNDO )
    {
        ctx.direction = EDITOR_CMD_INVERSE;
        count = Editor_UndoUndo(&maped->undo, &maped->doc, history_step_broadcast, &ctx);
    }
    else
    {
        ctx.direction = EDITOR_CMD_FORWARD;
        count = Editor_UndoRedo(&maped->undo, &maped->doc, history_step_broadcast, &ctx);
    }
    send_status_extra(session, request, EDITOR_HOST_OK, (uint32_t)count);
}

/**
 * Write one dirty document through the file layer. Only the changed half of
 * a square is emitted — rewriting an untouched `.jl2` would diff placements
 * the user never edited — except for a square DOC_CREATE made, which writes
 * both halves and its `.filepack` the first time.
 */
static enum EditorHost_Status
save_doc(
    struct ToriRSMapEd* maped,
    struct Editor_DocHandle* handle,
    struct Editor_DocRef* out_ref)
{
    const struct EditorHost_VTable* files = maped->files.vtable;
    void* user = maped->files.user_data;
    enum EditorHost_Status status = EDITOR_HOST_OK;

    memset(out_ref, 0, sizeof(*out_ref));
    out_ref->kind = handle->kind;

    if( handle->square )
    {
        struct Editor_Square* square = handle->square;
        int index = (int)(square - maped->doc.squares);
        int created = maped->doc.square_created[index];
        char key[EDITOR_DOC_KEY_MAX];
        char file_key[EDITOR_DOC_KEY_MAX + EDITOR_DOC_STORAGE_MAX];

        Editor_SquareKey(key, square->map_x, square->map_z);
        square_file_key(
            file_key, sizeof(file_key), Editor_DocSquareStorage(&maped->doc, square), key);
        snprintf(out_ref->key, sizeof(out_ref->key), "%s", key);

        if( square->dirty_map || created )
        {
            size_t size = Editor_Jm2Emit(square, NULL, 0);
            char* text = ToriMem_Alloc(ToriMem_Editor(), size + 1);
            Editor_Jm2Emit(square, text, size + 1);
            status = files->doc_save(user, EDITOR_DOC_SQUARE, file_key, 0, text, size);
            ToriMem_Free(ToriMem_Editor(), text);
        }
        if( status == EDITOR_HOST_OK && (square->dirty_loc || created) )
        {
            size_t size = Editor_Jl2Emit(square, NULL, 0);
            char* text = ToriMem_Alloc(ToriMem_Editor(), size + 1);
            Editor_Jl2Emit(square, text, size + 1);
            status = files->doc_save(user, EDITOR_DOC_SQUARE, file_key, 1, text, size);
            ToriMem_Free(ToriMem_Editor(), text);
        }
        if( status == EDITOR_HOST_OK && created )
        {
            /* The tree indexes a square's files: without this line the cache
             * pack never sees the new square. */
            char filepack[2 * EDITOR_DOC_KEY_MAX + 16];
            int size = snprintf(filepack, sizeof(filepack), "0=%s.jm2\n1=%s.jl2\n", key, key);
            status = files->doc_save(
                user, EDITOR_DOC_SQUARE, file_key, 2, filepack, (size_t)size);
            if( status == EDITOR_HOST_OK )
                maped->doc.square_created[index] = 0;
        }
    }
    else if( handle->spawn )
    {
        size_t size = Editor_SpawnEmit(&handle->spawn->file, NULL, 0);
        char* text = ToriMem_Alloc(ToriMem_Editor(), size + 1);

        snprintf(out_ref->key, sizeof(out_ref->key), "%s", handle->spawn->key);
        Editor_SpawnEmit(&handle->spawn->file, text, size + 1);
        /* A file with nothing left in it goes away: absent == no edited
         * spawns, the state the loader already treats it as. */
        status = files->doc_save(
            user, EDITOR_DOC_SPAWN, handle->spawn->key, 0, size > 0 ? text : NULL, size);
        ToriMem_Free(ToriMem_Editor(), text);
    }
    else
    {
        assert(handle->text);
        snprintf(out_ref->key, sizeof(out_ref->key), "%s", handle->text->key);
        /* An emptied TEXT document (a detached builder's .interaction) goes
         * away, like an emptied spawn file; a CFG or IFACE save always names
         * its text. */
        status = files->doc_save(user,
            handle->kind,
            handle->text->key,
            0,
            handle->kind == EDITOR_DOC_TEXT && handle->text->length == 0 ? NULL : handle->text->text,
            handle->text->length);
    }

    if( status != EDITOR_HOST_OK )
    {
        TORIRS_ERR("torirsmaped: save failed for %s (status %d)\n", out_ref->key, (int)status);
        return status;
    }
    Editor_DocHandleClearDirty(handle);
    return EDITOR_HOST_OK;
}

static void
handle_save_all(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped)
{
    struct Editor_DocRef* rows;
    int capacity;
    int saved = 0;

    if( !maped->writable )
    {
        send_status_extra(session, TORIRSMAPED_REQ_SAVE_ALL, EDITOR_HOST_LOCKED, 0);
        return;
    }

    capacity = maped->doc.square_count + maped->doc.spawn_count + maped->doc.text_count + 1;
    rows = ToriMem_Alloc(ToriMem_Editor(), (size_t)capacity * sizeof(*rows));
    assert(rows);

    for( int i = 0; i < maped->doc.square_count; i++ )
    {
        struct Editor_DocHandle handle = { EDITOR_DOC_SQUARE, &maped->doc.squares[i], NULL, NULL };
        if( !handle.square->loaded
            || (!Editor_DocHandleDirty(&handle) && !maped->doc.square_created[i]) )
            continue;
        if( save_doc(maped, &handle, &rows[saved]) == EDITOR_HOST_OK )
            saved++;
    }
    for( int i = 0; i < maped->doc.spawn_count; i++ )
    {
        struct Editor_DocHandle handle = { EDITOR_DOC_SPAWN, NULL, maped->doc.spawns[i], NULL };
        if( !handle.spawn->loaded || !handle.spawn->dirty )
            continue;
        if( save_doc(maped, &handle, &rows[saved]) == EDITOR_HOST_OK )
            saved++;
    }
    for( int i = 0; i < maped->doc.text_count; i++ )
    {
        struct Editor_DocHandle handle = { maped->doc.texts[i]->kind, NULL, NULL,
                                           maped->doc.texts[i] };
        if( !handle.text->loaded || !handle.text->dirty )
            continue;
        if( save_doc(maped, &handle, &rows[saved]) == EDITOR_HOST_OK )
            saved++;
    }

    broadcast_saved(maped, rows, saved);
    ToriMem_Free(ToriMem_Editor(), rows);
    send_status_extra(session, TORIRSMAPED_REQ_SAVE_ALL, EDITOR_HOST_OK, (uint32_t)saved);
}

static void
handle_save(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped,
    const uint8_t* body,
    uint32_t length)
{
    struct ToriRSMapEdReader reader;
    uint32_t kind;
    char key[EDITOR_DOC_KEY_MAX];
    struct Editor_DocHandle handle;
    struct Editor_DocRef row;
    enum EditorHost_Status status;
    int created = 0;

    ToriRSMapEd_ReaderInit(&reader, body, length);
    kind = ToriRSMapEd_GetU32(&reader);
    ToriRSMapEd_GetString(&reader, key, sizeof(key));
    if( !ToriRSMapEd_ReaderDone(&reader) )
    {
        session->alive = 0;
        return;
    }
    if( !maped->writable )
    {
        send_status_extra(session, TORIRSMAPED_REQ_SAVE, EDITOR_HOST_LOCKED, 0);
        return;
    }
    if( kind >= EDITOR_DOC_KIND_COUNT
        || !Editor_DocFind(&maped->doc, (enum Editor_DocKind)kind, key, &handle)
        || !Editor_DocHandleLoaded(&handle) )
    {
        send_status_extra(session, TORIRSMAPED_REQ_SAVE, EDITOR_HOST_ABSENT, 0);
        return;
    }
    if( handle.square )
        created = maped->doc.square_created[handle.square - maped->doc.squares];
    if( !Editor_DocHandleDirty(&handle) && !created )
    {
        send_status_extra(session, TORIRSMAPED_REQ_SAVE, EDITOR_HOST_OK, 0);
        return;
    }

    status = save_doc(maped, &handle, &row);
    if( status == EDITOR_HOST_OK )
        broadcast_saved(maped, &row, 1);
    send_status_extra(session, TORIRSMAPED_REQ_SAVE, status, status == EDITOR_HOST_OK ? 1 : 0);
}

/** Apply, record and broadcast one command the SERVER authored. */
static void
server_command(
    struct ToriRSMapEd* maped,
    const struct Editor_Cmd* command)
{
    int applied = Editor_CmdApply(&maped->doc, command, EDITOR_CMD_FORWARD);

    assert(applied);
    (void)applied;
    Editor_UndoPush(&maped->undo, command);
    broadcast_cmd(maped, command, EDITOR_CMD_FORWARD);
}

/**
 * DOC_CREATE(SQUARE, key, storage, template): a NEW square in the authority
 * (design #squares). Nothing is written until Save; the fill reaches every
 * mirror as ordinary FACT_CMDs grouped into one undo step, so undoing it
 * leaves the new square empty, exactly like undoing any other edit.
 */
static void
handle_doc_create(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped,
    const uint8_t* body,
    uint32_t length)
{
    struct ToriRSMapEdReader reader;
    uint32_t kind;
    char key[EDITOR_DOC_KEY_MAX];
    char storage[EDITOR_DOC_STORAGE_MAX];
    uint32_t template_kind;
    char source_key[EDITOR_DOC_KEY_MAX + EDITOR_DOC_STORAGE_MAX];
    int fill_height;
    int fill_underlay;
    int map_x;
    int map_z;
    char file_key[EDITOR_DOC_KEY_MAX + EDITOR_DOC_STORAGE_MAX];
    struct EditorHost_Blob existing = { NULL, 0 };
    struct Editor_Square* source = NULL;
    struct Editor_Square* square;
    int index;
    int was_in_stroke;
    int filled = 0;

    ToriRSMapEd_ReaderInit(&reader, body, length);
    kind = ToriRSMapEd_GetU32(&reader);
    ToriRSMapEd_GetString(&reader, key, sizeof(key));
    ToriRSMapEd_GetString(&reader, storage, sizeof(storage));
    template_kind = ToriRSMapEd_GetU32(&reader);
    ToriRSMapEd_GetString(&reader, source_key, sizeof(source_key));
    fill_height = (int)ToriRSMapEd_GetU32(&reader);
    fill_underlay = (int)ToriRSMapEd_GetU32(&reader);
    if( !ToriRSMapEd_ReaderDone(&reader) )
    {
        session->alive = 0;
        return;
    }

    if( kind != EDITOR_DOC_SQUARE )
    {
        send_status(session, TORIRSMAPED_REQ_DOC_CREATE, EDITOR_HOST_UNSUPPORTED);
        return;
    }
    if( strcmp(storage, "maps") == 0 )
        storage[0] = '\0';
    if( !Editor_SquareKeyParse(key, &map_x, &map_z) || !Editor_DocStorageValid(storage)
        || template_kind > EDITOR_DOC_TEMPLATE_FLAT
        || (template_kind == EDITOR_DOC_TEMPLATE_FLAT
            && (fill_height < 0 || fill_height > 255 || fill_underlay < 0
                || fill_underlay > 255)) )
    {
        send_status(session, TORIRSMAPED_REQ_DOC_CREATE, EDITOR_HOST_REFUSED);
        return;
    }

    /* Taken in that storage, or open at all (one square per place). */
    square_file_key(file_key, sizeof(file_key), storage, key);
    if( maped->files.vtable->doc_load(
            maped->files.user_data, EDITOR_DOC_SQUARE, file_key, 0, &existing)
            == EDITOR_HOST_OK
        || Editor_DocFindSquare(&maped->doc, map_x, map_z) )
    {
        Editor_HostBlobFree(&existing);
        TORIRS_LOG("torirsmaped: DOC_CREATE refused — %s already exists\n", file_key);
        send_status(session, TORIRSMAPED_REQ_DOC_CREATE, EDITOR_HOST_REFUSED);
        return;
    }
    Editor_HostBlobFree(&existing);

    if( template_kind == EDITOR_DOC_TEMPLATE_COPY )
    {
        char source_storage[EDITOR_DOC_STORAGE_MAX];
        char source_bare[EDITOR_DOC_KEY_MAX];
        int source_x;
        int source_z;
        enum EditorHost_Status status = EDITOR_HOST_REFUSED;

        if( Editor_DocSplitSquareKey(source_key, source_storage, source_bare) )
        {
            Editor_SquareKeyParse(source_bare, &source_x, &source_z);
            status = authority_open_square(maped, source_storage, source_x, source_z, &source);
        }
        if( status != EDITOR_HOST_OK )
        {
            send_status(session, TORIRSMAPED_REQ_DOC_CREATE, status);
            return;
        }
    }

    square = Editor_DocOpenSquare(&maped->doc, map_x, map_z);
    if( !square )
    {
        send_status(session, TORIRSMAPED_REQ_DOC_CREATE, EDITOR_HOST_IO_ERROR);
        return;
    }
    index = (int)(square - maped->doc.squares);
    Editor_DocSquareSetStorage(&maped->doc, square, storage);
    maped->doc.square_created[index] = 1;
    square->loaded = 1;
    square->dirty_map = 1;
    square->dirty_loc = 1;

    /* The fill is its own undo step, whatever stroke a client has open. */
    was_in_stroke = maped->undo.in_stroke;
    Editor_UndoStrokeBegin(&maped->undo);
    if( template_kind == EDITOR_DOC_TEMPLATE_FLAT )
    {
        for( int x = 0; x < EDITOR_SQUARE_X; x++ )
        {
            for( int z = 0; z < EDITOR_SQUARE_Z; z++ )
            {
                struct Editor_Cmd command;
                Editor_CmdInitSquare(&command, EDITOR_CMD_TILE, map_x, map_z);
                command.u.tile.level = 0;
                command.u.tile.x = x;
                command.u.tile.z = z;
                command.u.tile.after.has_height = 1;
                command.u.tile.after.height = (uint8_t)fill_height;
                command.u.tile.after.underlay_id = (uint8_t)fill_underlay;
                server_command(maped, &command);
                filled++;
            }
        }
    }
    else if( template_kind == EDITOR_DOC_TEMPLATE_COPY )
    {
        static const struct Editor_Tile empty;

        for( int level = 0; level < EDITOR_SQUARE_LEVELS; level++ )
        {
            for( int z = 0; z < EDITOR_SQUARE_Z; z++ )
            {
                for( int x = 0; x < EDITOR_SQUARE_X; x++ )
                {
                    const struct Editor_Tile* tile = &source->tiles[Editor_TileIndex(x, z, level)];
                    struct Editor_Cmd command;

                    if( memcmp(tile, &empty, sizeof(empty)) == 0 )
                        continue;
                    Editor_CmdInitSquare(&command, EDITOR_CMD_TILE, map_x, map_z);
                    command.u.tile.level = level;
                    command.u.tile.x = x;
                    command.u.tile.z = z;
                    command.u.tile.after = *tile;
                    server_command(maped, &command);
                    filled++;
                }
            }
        }
        for( int i = 0; i < source->loc_count; i++ )
        {
            struct Editor_Cmd command;
            Editor_CmdInitSquare(&command, EDITOR_CMD_LOC, map_x, map_z);
            command.has_after = 1;
            command.u.loc.after = source->locs[i];
            server_command(maped, &command);
            filled++;
        }
    }
    Editor_UndoStrokeEnd(&maped->undo);
    if( was_in_stroke )
        Editor_UndoStrokeBegin(&maped->undo);

    TORIRS_LOG("torirsmaped: created %s (%d fill commands)\n", file_key, filled);
    send_status_extra(session, TORIRSMAPED_REQ_DOC_CREATE, EDITOR_HOST_OK, (uint32_t)filled);
}

/* ---- catalog dispatch (torirs_maped_catalog.c does the work) ---- */

static void
handle_catalog(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped,
    uint32_t type,
    const uint8_t* body,
    uint32_t length)
{
    struct ToriRSMapEdCatalog* catalog = maped->catalog;
    struct ToriRSMapEdBuf out;
    uint32_t fact;
    int ok;

    assert(catalog);
    memset(&out, 0, sizeof(out));
    if( type == TORIRSMAPED_REQ_CATALOG_QUERY )
    {
        ok = ToriRSMapEdCatalog_HandleQuery(catalog, body, length, &out);
        fact = TORIRSMAPED_FACT_CATALOG_PAGE;
    }
    else if( type == TORIRSMAPED_REQ_CATALOG_RECORD )
    {
        ok = ToriRSMapEdCatalog_HandleRecord(catalog, body, length, &out);
        fact = TORIRSMAPED_FACT_CATALOG_RECORD;
    }
    else
    {
        ok = ToriRSMapEdCatalog_HandleSquares(catalog, &maped->doc, body, length, &out);
        fact = TORIRSMAPED_FACT_CATALOG_SQUARES;
    }
    if( ok )
        send_buf(session, fact, &out);
    else
        session->alive = 0;
    ToriRSMapEd_BufFree(&out);
}

/**
 * The atlas changed under every window: a square was created in the
 * authority (`disk_changed` 0), or a save may have written squares (1: re-stat
 * maps/ and broadcast only if something there changed). Nothing to say until
 * the catalog has been built — until then no connection holds an atlas.
 */
static void
catalog_atlas_changed(
    struct ToriRSMapEd* maped,
    int disk_changed)
{
    struct ToriRSMapEdCatalog* catalog = maped->catalog;
    struct ToriRSMapEdBuf payload;

    assert(catalog);
    if( !ToriRSMapEdCatalog_Built(catalog) )
        return;
    if( disk_changed && !ToriRSMapEdCatalog_RefreshMaps(catalog) )
        return;
    memset(&payload, 0, sizeof(payload));
    ToriRSMapEdCatalog_EncodeSquares(catalog, &maped->doc, &payload);
    for( int i = 0; i < TORIRSMAPED_SESSION_MAX; i++ )
    {
        struct ToriRSMapEdSession* session = maped->sessions[i];
        if( session && session->alive && session->version >= TORIRSMAPED_PROTO_VERSION )
            send_buf(session, TORIRSMAPED_FACT_CATALOG_SQUARES_CHANGED, &payload);
    }
    ToriRSMapEd_BufFree(&payload);
}

static void
handle_frame(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped,
    uint32_t type,
    const uint8_t* body,
    uint32_t length)
{
    int v2 = session->version >= TORIRSMAPED_PROTO_VERSION;

    assert(session);
    assert(maped);
    assert(maped->open);

    switch( type )
    {
    case TORIRSMAPED_REQ_HELLO:
    {
        uint8_t payload[16];
        uint32_t wanted_group;
        uint32_t version;
        if( length != 12 )
        {
            session->alive = 0;
            return;
        }
        /* A v1 peer keeps speaking v1 — every frame it gets back is one it
         * knows. Anything newer than v1 is answered at our version. */
        version = ToriRSMapEd_ReadU32(body);
        session->version = version >= TORIRSMAPED_PROTO_VERSION ? TORIRSMAPED_PROTO_VERSION
                                                                : TORIRSMAPED_PROTO_VERSION_V1;
        session->role = ToriRSMapEd_ReadU32(body + 4);
        /* Group membership: joining an existing Client is stated, a new one
         * granted. Ids only grow, so a departed Client's id never comes
         * back attached to a stranger's state. */
        wanted_group = ToriRSMapEd_ReadU32(body + 8);
        if( wanted_group == 0 )
            wanted_group = ++maped->next_client_id;
        else if( wanted_group > maped->next_client_id )
            maped->next_client_id = wanted_group;
        session->client_id = wanted_group;
        ToriRSMapEd_WriteU32(payload, session->version);
        ToriRSMapEd_WriteU32(payload + 4, (uint32_t)maped->writable);
        ToriRSMapEd_WriteU32(payload + 8, session->client_id);
        ToriRSMapEd_WriteU32(payload + 12, maped->capabilities);
        send_frame(
            session,
            TORIRSMAPED_FACT_HELLO,
            payload,
            session->version >= TORIRSMAPED_PROTO_VERSION ? 16 : 12);
        return;
    }
    case TORIRSMAPED_REQ_SQUARE_LIST:
        handle_square_list(session, maped);
        return;
    case TORIRSMAPED_REQ_SQUARE_LOAD:
        handle_square_load(session, maped, body, length);
        return;
    case TORIRSMAPED_REQ_SQUARE_SAVE:
        handle_square_save(session, maped, body, length);
        catalog_atlas_changed(maped, 1);
        return;
    case TORIRSMAPED_REQ_SPAWN_SAVE:
        handle_spawn_save(session, maped, body, length);
        return;
    case TORIRSMAPED_REQ_BAKE:
        handle_bake(session, maped);
        return;
    case TORIRSMAPED_REQ_SQUARE_OPEN:
        handle_square_open(session, maped, body, length);
        return;
    case TORIRSMAPED_REQ_CMD:
        handle_cmd(session, maped, body, length);
        return;
    case TORIRSMAPED_REQ_UNDO:
    case TORIRSMAPED_REQ_REDO:
        handle_history(session, maped, type);
        return;
    case TORIRSMAPED_REQ_STROKE:
        if( length != 4 )
        {
            session->alive = 0;
            return;
        }
        if( ToriRSMapEd_ReadU32(body) )
            Editor_UndoStrokeBegin(&maped->undo);
        else
            Editor_UndoStrokeEnd(&maped->undo);
        send_status(session, TORIRSMAPED_REQ_STROKE, EDITOR_HOST_OK);
        return;
    case TORIRSMAPED_REQ_SAVE_ALL:
        handle_save_all(session, maped);
        catalog_atlas_changed(maped, 1);
        return;
    case TORIRSMAPED_REQ_STATE_SET:
    {
        uint32_t key;
        uint32_t count;
        uint8_t payload[12 + 4 * TORIRSMAPED_STATE_VALUES_MAX];
        int slot = -1;

        if( length < 8 )
        {
            session->alive = 0;
            return;
        }
        key = ToriRSMapEd_ReadU32(body);
        count = ToriRSMapEd_ReadU32(body + 4);
        if( count > TORIRSMAPED_STATE_VALUES_MAX || length != 8 + count * 4 )
        {
            session->alive = 0;
            return;
        }
        /* The VIEW_SELF question, and camera keys only their owner may set
         * (torirs_maped_view.h). */
        if( view_state_set_intercept(session, maped, key) )
            return;

        /* Store last-writer-wins under the requester's group, for that
         * group's STATE_SYNC replay. The store never interprets the values —
         * semantics are the clients' contract. */
        for( int i = 0; i < maped->state_count; i++ )
        {
            if( maped->state[i].client_id == session->client_id
                && maped->state[i].key == key )
            {
                slot = i;
                break;
            }
        }
        if( slot < 0 && maped->state_count < TORIRSMAPED_STATE_KEYS_MAX )
            slot = maped->state_count++;
        if( slot >= 0 )
        {
            maped->state[slot].client_id = session->client_id;
            maped->state[slot].key = key;
            maped->state[slot].count = count;
            for( uint32_t i = 0; i < count; i++ )
                maped->state[slot].values[i] =
                    (int32_t)ToriRSMapEd_ReadU32(body + 8 + i * 4);
        }
        else
            TORIRS_LOG("torirsmaped: state store full — key %u relayed but not stored\n",
                key);

        maped->seq++;
        ToriRSMapEd_WriteU32(payload, maped->seq);
        ToriRSMapEd_WriteU32(payload + 4, key);
        ToriRSMapEd_WriteU32(payload + 8, count);
        memcpy(payload + 12, body + 8, count * 4);
        broadcast_group(
            maped, session->client_id, TORIRSMAPED_FACT_STATE, payload, 12 + count * 4);
        return;
    }
    case TORIRSMAPED_REQ_STATE_SYNC:
    {
        uint8_t payload[12 + 4 * TORIRSMAPED_STATE_VALUES_MAX];
        uint32_t replayed = 0;

        for( int i = 0; i < maped->state_count; i++ )
        {
            if( maped->state[i].client_id != session->client_id )
                continue;
            ToriRSMapEd_WriteU32(payload, maped->seq);
            ToriRSMapEd_WriteU32(payload + 4, maped->state[i].key);
            ToriRSMapEd_WriteU32(payload + 8, maped->state[i].count);
            for( uint32_t v = 0; v < maped->state[i].count; v++ )
                ToriRSMapEd_WriteU32(
                    payload + 12 + v * 4, (uint32_t)maped->state[i].values[v]);
            send_frame(
                session,
                TORIRSMAPED_FACT_STATE,
                payload,
                12 + maped->state[i].count * 4);
            replayed++;
        }
        send_status_extra(
            session, TORIRSMAPED_REQ_STATE_SYNC, EDITOR_HOST_OK, replayed);
        return;
    }
    case TORIRSMAPED_REQ_BYE:
        session->alive = 0;
        return;
    default:
        break;
    }

    /* The v2 frames. A v1 peer never sends them; one that does is speaking a
     * protocol it did not declare, which is a framing error like any other. */
    if( v2 && type == TORIRSMAPED_REQ_DOC_OPEN )
    {
        handle_doc_open(session, maped, body, length);
        return;
    }
    if( v2 && type == TORIRSMAPED_REQ_DOC_CLOSE )
    {
        handle_doc_close(session, maped, body, length);
        return;
    }
    if( v2 && type == TORIRSMAPED_REQ_SAVE )
    {
        handle_save(session, maped, body, length);
        catalog_atlas_changed(maped, 1);
        return;
    }
    if( v2 && type == TORIRSMAPED_REQ_SAVE_AS )
    {
        /* Reserved (design #save): the id allocation it needs is phase 2's.
         * A square's "save as new" is DOC_CREATE with the copy template. */
        send_status(session, TORIRSMAPED_REQ_SAVE_AS, EDITOR_HOST_UNSUPPORTED);
        return;
    }
    if( v2 && type == TORIRSMAPED_REQ_DOC_CREATE )
    {
        int squares_before = maped->doc.square_count;
        handle_doc_create(session, maped, body, length);
        if( maped->doc.square_count != squares_before )
            catalog_atlas_changed(maped, 0);
        return;
    }
    if( v2 && type == TORIRSMAPED_REQ_SPAWN_VIEW )
    {
        handle_spawn_view(session, maped, body, length);
        return;
    }
    if( v2
        && (type == TORIRSMAPED_REQ_CATALOG_QUERY || type == TORIRSMAPED_REQ_CATALOG_RECORD
            || type == TORIRSMAPED_REQ_CATALOG_SQUARES) )
    {
        handle_catalog(session, maped, type, body, length);
        return;
    }

    TORIRS_ERR("torirsmaped: unknown frame type %u — closing\n", type);
    session->alive = 0;
}

int
ToriRSMapEd_SessionPump(struct ToriRSMapEdSession* session)
{
    struct ToriRSMapEd* maped;
    uint8_t chunk[16384];
    int peer_gone = 0;

    assert(session);
    assert(session->maped);
    maped = session->maped;

    if( !session->alive )
        return 0;

    for( ;; )
    {
        int taken = session->transport.recv(session->transport.ctx, chunk, sizeof(chunk));
        if( taken < 0 )
        {
            /* The peer is gone, but its final requests may be sitting whole
             * in the buffer — act on them before dying, the way a socket
             * server drains before it sees EOF. */
            peer_gone = 1;
            break;
        }
        if( taken == 0 )
            break;
        ToriRSMapEd_BufWrite(&session->in, chunk, taken);
        if( taken < (int)sizeof(chunk) )
            break;
    }

    while( session->alive && ToriRSMapEd_BufAvailable(&session->in) >= TORIRSMAPED_FRAME_HEADER )
    {
        const uint8_t* head = ToriRSMapEd_BufPeek(&session->in);
        uint32_t type = ToriRSMapEd_ReadU32(head);
        uint32_t length = ToriRSMapEd_ReadU32(head + 4);

        if( length > TORIRSMAPED_PAYLOAD_MAX )
        {
            TORIRS_LOG("torirsmaped: frame length %u exceeds the %d byte bound — closing\n",
                length,
                TORIRSMAPED_PAYLOAD_MAX);
            session->alive = 0;
            break;
        }
        if( ToriRSMapEd_BufAvailable(&session->in)
            < TORIRSMAPED_FRAME_HEADER + (int)length )
            break;

        ToriRSMapEd_BufConsume(&session->in, TORIRSMAPED_FRAME_HEADER);
        /* The payload is contiguous at the head; handled in place, then
         * consumed. Handlers must not write into `in`, and none can — the
         * only writer is the recv loop above. */
        handle_frame(session, maped, type, ToriRSMapEd_BufPeek(&session->in), length);
        ToriRSMapEd_BufConsume(&session->in, (int)length);
    }

    if( peer_gone )
        session->alive = 0;
    return session->alive;
}

/* The catalog is compiled into this unit, not linked beside it: every link
 * line that carries the server (the client with and without EMBED_SERVER,
 * the embed test, the daemon) already names this file. See the note at the
 * top of torirs_maped_catalog.c. */
#include "torirs_maped_catalog.c"

/* Views: SPAWN_VIEW, the camera keys and STATE_VIEW_SELF. Compiled into this
 * unit for the catalog's reason (torirs_maped_view.h, BUILD NOTE). */
#include "torirs_maped_view.c"
