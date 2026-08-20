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

#include "editor/editor_jm2.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        fprintf(
            stderr,
            "torirsmaped: another server holds %s — serving read-only\n",
            content_dir);
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

/** A document fact to every attached session, the requester included —
 *  mirrors mutate on the broadcast, never on the request. */
static void
broadcast(
    struct ToriRSMapEd* maped,
    uint32_t type,
    const uint8_t* payload,
    uint32_t length)
{
    for( int i = 0; i < TORIRSMAPED_SESSION_MAX; i++ )
    {
        struct ToriRSMapEdSession* session = maped->sessions[i];
        if( session && session->alive )
            send_frame(session, type, payload, length);
    }
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
broadcast_cmd(
    struct ToriRSMapEd* maped,
    const struct Editor_Cmd* command,
    enum Editor_CmdDirection direction)
{
    uint8_t payload[8 + TORIRSMAPED_CMD_WIRE];

    maped->seq++;
    ToriRSMapEd_WriteU32(payload, maped->seq);
    ToriRSMapEd_WriteU32(payload + 4, (uint32_t)direction);
    ToriRSMapEd_CmdEncode(command, payload + 8);
    broadcast(maped, TORIRSMAPED_FACT_CMD, payload, sizeof(payload));
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
        coords = malloc((size_t)count * 2 * sizeof(*coords));
        assert(coords);
        status = maped->files.vtable->square_list(
            maped->files.user_data, coords, count, &count);
    }
    if( status != EDITOR_HOST_OK )
        count = 0;

    length = 8 + (uint32_t)count * 8;
    payload = malloc(length);
    assert(payload);
    ToriRSMapEd_WriteU32(payload, (uint32_t)status);
    ToriRSMapEd_WriteU32(payload + 4, (uint32_t)count);
    for( int i = 0; i < count; i++ )
    {
        ToriRSMapEd_WriteU32(payload + 8 + (uint32_t)i * 8, (uint32_t)coords[i * 2]);
        ToriRSMapEd_WriteU32(payload + 12 + (uint32_t)i * 8, (uint32_t)coords[i * 2 + 1]);
    }
    send_frame(session, TORIRSMAPED_FACT_SQUARE_LIST, payload, length);
    free(payload);
    free(coords);
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
    payload = malloc(total);
    assert(payload);
    ToriRSMapEd_WriteU32(payload, (uint32_t)status);
    ToriRSMapEd_WriteU32(payload + 4, jm2_plus);
    ToriRSMapEd_WriteU32(payload + 8, jl2_plus);
    if( jm2.size > 0 )
        memcpy(payload + 12, jm2.data, jm2.size);
    if( jl2.size > 0 )
        memcpy(payload + 12 + jm2.size, jl2.data, jl2.size);

    send_frame(session, TORIRSMAPED_FACT_SQUARE_LOAD, payload, total);
    free(payload);
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

static void
handle_square_open(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped,
    const uint8_t* body,
    uint32_t length)
{
    int map_x;
    int map_z;
    struct Editor_Square* square;
    enum EditorHost_Status status = EDITOR_HOST_OK;
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

    square = Editor_DocOpenSquare(&maped->doc, map_x, map_z);
    if( !square )
        status = EDITOR_HOST_IO_ERROR;

    if( square && !square->loaded )
    {
        struct EditorHost_Blob jm2 = { NULL, 0 };
        struct EditorHost_Blob jl2 = { NULL, 0 };

        status = maped->files.vtable->square_load(
            maped->files.user_data, map_x, map_z, &jm2, &jl2);
        if( status == EDITOR_HOST_OK )
        {
            struct Editor_ParseResult result = Editor_Jm2Parse(square, jm2.data, jm2.size);
            if( result.status != EDITOR_PARSE_OK )
            {
                fprintf(
                    stderr,
                    "torirsmaped: m%d_%d.jm2 line %d: parse error %d\n",
                    map_x,
                    map_z,
                    result.line,
                    (int)result.status);
                status = EDITOR_HOST_IO_ERROR;
            }
            else if( jl2.data && jl2.size > 0 )
            {
                result = Editor_Jl2Parse(square, jl2.data, jl2.size);
                if( result.status != EDITOR_PARSE_OK )
                {
                    fprintf(
                        stderr,
                        "torirsmaped: m%d_%d.jl2 line %d: parse error %d\n",
                        map_x,
                        map_z,
                        result.line,
                        (int)result.status);
                    status = EDITOR_HOST_IO_ERROR;
                }
            }
        }
        Editor_HostBlobFree(&jm2);
        Editor_HostBlobFree(&jl2);
    }

    /* The reply is the DOCUMENT's state, not the file's: a square another
     * client already edited answers with those edits in it, so a late-joining
     * mirror starts identical to the authority. Emit round-trips byte-exact,
     * so on first open this equals the file anyway. */
    if( status == EDITOR_HOST_OK && square && square->loaded )
    {
        jm2_size = Editor_Jm2Emit(square, NULL, 0);
        jm2_text = malloc(jm2_size + 1);
        assert(jm2_text);
        Editor_Jm2Emit(square, jm2_text, jm2_size + 1);

        jl2_size = Editor_Jl2Emit(square, NULL, 0);
        jl2_text = malloc(jl2_size + 1);
        assert(jl2_text);
        Editor_Jl2Emit(square, jl2_text, jl2_size + 1);
    }

    total = (uint32_t)(28 + jm2_size + jl2_size);
    payload = malloc(total);
    assert(payload);
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
    free(payload);
    free(jm2_text);
    free(jl2_text);
}

static void
handle_cmd(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped,
    const uint8_t* body,
    uint32_t length)
{
    struct Editor_Cmd command;
    struct Editor_Square* square;

    if( !ToriRSMapEd_CmdDecode(body, length, &command) )
    {
        session->alive = 0;
        return;
    }

    /* An edit against a square the authority never opened is a client bug,
     * but a REMOTE one — refuse it, don't die on it. */
    square = Editor_DocFindSquare(&maped->doc, command.map_x, command.map_z);
    if( !square || !square->loaded )
    {
        send_status(session, TORIRSMAPED_REQ_CMD, EDITOR_HOST_ABSENT);
        return;
    }
    if( !Editor_CmdApply(&maped->doc, &command, EDITOR_CMD_FORWARD) )
    {
        send_status(session, TORIRSMAPED_REQ_CMD, EDITOR_HOST_IO_ERROR);
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

static void
handle_save_all(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped)
{
    int saved = 0;
    int saved_coords[EDITOR_DOC_MAX_SQUARES * 2];

    if( !maped->writable )
    {
        send_status_extra(session, TORIRSMAPED_REQ_SAVE_ALL, EDITOR_HOST_LOCKED, 0);
        return;
    }

    for( int i = 0; i < maped->doc.square_count; i++ )
    {
        struct Editor_Square* square = &maped->doc.squares[i];
        char* jm2_text = NULL;
        char* jl2_text = NULL;
        size_t jm2_length = 0;
        size_t jl2_length = 0;
        enum EditorHost_Status status;

        if( !square->dirty_map && !square->dirty_loc )
            continue;

        /* Only the changed half is emitted; the other stays NULL and the file
         * layer leaves that file alone. Rewriting an untouched `.jl2` would
         * diff placements the user never edited. */
        if( square->dirty_map )
        {
            jm2_length = Editor_Jm2Emit(square, NULL, 0);
            jm2_text = malloc(jm2_length + 1);
            assert(jm2_text);
            Editor_Jm2Emit(square, jm2_text, jm2_length + 1);
        }
        if( square->dirty_loc )
        {
            jl2_length = Editor_Jl2Emit(square, NULL, 0);
            jl2_text = malloc(jl2_length + 1);
            assert(jl2_text);
            Editor_Jl2Emit(square, jl2_text, jl2_length + 1);
        }

        status = maped->files.vtable->square_save(
            maped->files.user_data,
            square->map_x,
            square->map_z,
            jm2_text,
            jm2_length,
            jl2_text,
            jl2_length);
        free(jm2_text);
        free(jl2_text);

        if( status != EDITOR_HOST_OK )
        {
            fprintf(
                stderr,
                "torirsmaped: save failed for %d,%d\n",
                square->map_x,
                square->map_z);
            continue;
        }
        square->dirty_map = 0;
        square->dirty_loc = 0;
        saved_coords[saved * 2] = square->map_x;
        saved_coords[saved * 2 + 1] = square->map_z;
        saved++;
    }

    if( saved > 0 )
    {
        uint32_t total = 4 + (uint32_t)saved * 8;
        uint8_t* payload = malloc(total);
        assert(payload);
        ToriRSMapEd_WriteU32(payload, (uint32_t)saved);
        for( int i = 0; i < saved; i++ )
        {
            ToriRSMapEd_WriteU32(payload + 4 + (uint32_t)i * 8, (uint32_t)saved_coords[i * 2]);
            ToriRSMapEd_WriteU32(
                payload + 8 + (uint32_t)i * 8, (uint32_t)saved_coords[i * 2 + 1]);
        }
        broadcast(maped, TORIRSMAPED_FACT_SAVED, payload, total);
        free(payload);
    }
    send_status_extra(session, TORIRSMAPED_REQ_SAVE_ALL, EDITOR_HOST_OK, (uint32_t)saved);
}

static void
handle_frame(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped,
    uint32_t type,
    const uint8_t* body,
    uint32_t length)
{
    assert(session);
    assert(maped);
    assert(maped->open);

    switch( type )
    {
    case TORIRSMAPED_REQ_HELLO:
    {
        uint8_t payload[12];
        uint32_t wanted_group;
        if( length != 12 )
        {
            session->alive = 0;
            return;
        }
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
        /* Version mismatch is the peer's to notice: the answer always carries
         * the server's version, and a client that needs more hangs up. */
        ToriRSMapEd_WriteU32(payload, TORIRSMAPED_PROTO_VERSION);
        ToriRSMapEd_WriteU32(payload + 4, (uint32_t)maped->writable);
        ToriRSMapEd_WriteU32(payload + 8, session->client_id);
        send_frame(session, TORIRSMAPED_FACT_HELLO, payload, sizeof(payload));
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
            fprintf(
                stderr,
                "torirsmaped: state store full — key %u relayed but not stored\n",
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
        fprintf(stderr, "torirsmaped: unknown frame type %u — closing\n", type);
        session->alive = 0;
        return;
    }
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
            fprintf(
                stderr,
                "torirsmaped: frame length %u exceeds the %d byte bound — closing\n",
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
