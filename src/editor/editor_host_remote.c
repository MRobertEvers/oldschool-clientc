/*
 * The MapEd client: everything a connection to ToriRSMapEd can say and hear.
 *
 * Two layers share this file because they share the connection:
 *
 *   - The EditorHost vtable — the file-flavoured operations (square list,
 *     spawn save, bake). Same seam the local binding implements, so callers
 *     holding a `struct EditorHost` need not know there is a wire.
 *   - The document and state layer — Editor_HostMapEd* — which has no local
 *     equivalent ON PURPOSE: the authoritative document lives only in the
 *     server, so "apply this edit locally" is not an operation any binding
 *     could offer. Intents go up; the mirror mutates only in DrainFacts,
 *     when the server's broadcast comes back — the sender applies its own
 *     echo exactly like every other connection.
 *
 * Two deployments, one protocol:
 *
 *   server=embed   the server runs inside this process; the wire is a pair
 *                  of in-memory byte queues (torirs_maped_embed.h).
 *   server=tcp     the torirsmaped daemon; the wire is a loopback socket.
 *
 * The request calls are synchronous — every one rides an explicit user
 * action whose caller wants the answer in hand — but the connection is not:
 * broadcasts (FACT_CMD, FACT_STATE, FACT_SAVED) arrive whenever another
 * connection acts, including in the middle of an exchange. Any frame that is
 * a broadcast gets stashed, never dropped, and DrainFacts delivers the stash
 * plus whatever else has arrived. STATE_SET is the one fire-and-forget
 * request, because it rides interactive paths that must not stall a frame.
 */

#include "editor_host.h"
#include "mem/torirs_mem.h"

#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_build.h"
#include "torirsmaped/torirs_maped_view.h"
#include "torirsmaped/torirs_maped_catalog.h"

/*
 * TORIRS_MAPED_NO_EMBED: build the TCP half only.
 *
 * A connection that dials a daemon needs no server in its own address space,
 * and torirsmapedctl is the proof — it is a controller, and a controller that
 * had to link a document, a content tree and a bake to talk to one would make
 * "controllers hold nothing" a claim rather than a fact. Everything else
 * (the game client) leaves the flag off and gets both deployments.
 */
#if !defined(TORIRS_MAPED_NO_EMBED)
#include "torirsmaped/torirs_maped_embed.h"
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

#if !defined(_WIN32)
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

/** Loopback round trips are milliseconds; a minute means the daemon is gone. */
#define REMOTE_OP_TIMEOUT_MS (60 * 1000)
/** A bake shells out to make and can legitimately run for many minutes. The
 *  timeout bounds the gap between progress lines, not the whole bake. */
#define REMOTE_BAKE_TIMEOUT_MS (10 * 60 * 1000)

/** A broadcast that arrived while a request was waiting for its answer. */
struct stashed_fact
{
    struct stashed_fact* next;
    uint32_t type;
    uint32_t length;
    uint8_t data[];
};

struct maped_remote
{
    /** Non-NULL for the embed binding. Owned unless `borrowed_embed`. */
    struct ToriRSMapEdEmbed* embed;
    int embed_client;
    /** A peer binding shares another binding's embed and must not stop it. */
    int borrowed_embed;
    /** >= 0 for the tcp binding. */
    int fd;
    /** Reply bytes not yet parsed into whole frames. */
    struct ToriRSMapEdBuf in;
    /** Broadcasts seen while awaiting a reply, in arrival order. */
    struct stashed_fact* stash_head;
    struct stashed_fact* stash_tail;
    /** FACT_HELLO's answer: whether the server can save its tree. */
    int writable;
    /** FACT_HELLO's answer: the Client (session group) this connection
     *  belongs to. State facts relay only within it. */
    uint32_t client_id;
    /** FACT_HELLO v2's capability bits. */
    uint32_t capabilities;
    /** The newest FACT_CATALOG_SQUARES_CHANGED payload DrainFacts saw, kept
     *  until Editor_HostMapEdCatalogSquaresChanged takes it (only the newest
     *  matters: each one is a whole atlas). NULL when none is pending. */
    struct stashed_fact* atlas_changed;
    /** Live-editor facts (173..190: builds, RUN, the live link) DrainFacts
     *  kept for Editor_HostMapEdLiveTake, oldest first. */
    struct stashed_fact* live_head;
    struct stashed_fact* live_tail;
};

static int
is_live_fact(uint32_t type)
{
    return type >= TORIRSMAPED_FACT_LIVE_RANGE_FIRST && type <= TORIRSMAPED_FACT_LIVE_RANGE_LAST;
}

static int
is_broadcast(uint32_t type)
{
    /* Build and live facts go to every connection (torirs_maped_build.h), so
     * one can arrive in the middle of any request's exchange. */
    return type == TORIRSMAPED_FACT_CMD || type == TORIRSMAPED_FACT_STATE
           || type == TORIRSMAPED_FACT_SAVED
           || type == TORIRSMAPED_FACT_CATALOG_SQUARES_CHANGED || is_live_fact(type);
}

/** Keep one live-editor fact for Editor_HostMapEdLiveTake. */
static void
live_keep(struct maped_remote* remote, uint32_t type, const uint8_t* body, uint32_t length)
{
    struct stashed_fact* fact = ToriMem_Alloc(ToriMem_Editor(), sizeof(*fact) + length);
    assert(fact);
    fact->next = NULL;
    fact->type = type;
    fact->length = length;
    if( length > 0 )
        memcpy(fact->data, body, length);
    if( remote->live_tail )
        remote->live_tail->next = fact;
    else
        remote->live_head = fact;
    remote->live_tail = fact;
}

static int
link_send(
    struct maped_remote* remote,
    const uint8_t* data,
    int len)
{
    assert(remote);
    assert(data || len == 0);

#if !defined(TORIRS_MAPED_NO_EMBED)
    if( remote->embed )
        return ToriRSMapEd_EmbedWrite(remote->embed, remote->embed_client, data, len) == len;
#endif

#if !defined(_WIN32)
    {
        int sent_total = 0;
        while( sent_total < len )
        {
            ssize_t sent =
                send(remote->fd, data + sent_total, (size_t)(len - sent_total), 0);
            if( sent > 0 )
            {
                sent_total += (int)sent;
                continue;
            }
            if( sent < 0 && errno == EINTR )
                continue;
            return 0;
        }
        return 1;
    }
#else
    return 0;
#endif
}

/**
 * Move whatever the server has said into `in`. Returns 1 when bytes arrived,
 * 0 when the wait timed out empty, -1 when the server is gone.
 */
static int
link_fill(
    struct maped_remote* remote,
    int timeout_ms)
{
    uint8_t chunk[16384];

    assert(remote);

#if !defined(TORIRS_MAPED_NO_EMBED)
    if( remote->embed )
    {
        int got = 0;
        int taken;

        ToriRSMapEd_EmbedPump(remote->embed);
        while( (taken = ToriRSMapEd_EmbedRead(
                    remote->embed, remote->embed_client, chunk, sizeof(chunk))) > 0 )
        {
            ToriRSMapEd_BufWrite(&remote->in, chunk, taken);
            got = 1;
        }
        if( taken < 0 && !got )
            return -1;
        return got;
    }
#endif

#if !defined(_WIN32)
    {
        fd_set readable;
        struct timeval timeout;
        ssize_t taken;
        int ready;

        FD_ZERO(&readable);
        FD_SET(remote->fd, &readable);
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;
        ready = select(remote->fd + 1, &readable, NULL, NULL, &timeout);
        if( ready < 0 )
            return errno == EINTR ? 0 : -1;
        if( ready == 0 )
            return 0;
        taken = recv(remote->fd, chunk, sizeof(chunk), 0);
        if( taken <= 0 )
            return -1;
        ToriRSMapEd_BufWrite(&remote->in, chunk, (int)taken);
        return 1;
    }
#else
    (void)timeout_ms;
    return -1;
#endif
}

/** One whole frame off the buffer, header consumed, or NULL if none is
 *  complete. The payload pointer is valid until the next buffer write; the
 *  caller consumes `out_length` when done. */
static const uint8_t*
take_frame(
    struct maped_remote* remote,
    uint32_t* out_type,
    uint32_t* out_length)
{
    const uint8_t* head;
    uint32_t length;

    if( ToriRSMapEd_BufAvailable(&remote->in) < TORIRSMAPED_FRAME_HEADER )
        return NULL;
    head = ToriRSMapEd_BufPeek(&remote->in);
    length = ToriRSMapEd_ReadU32(head + 4);
    if( length > TORIRSMAPED_PAYLOAD_MAX )
    {
        TORIRS_ERR("editor: maped answered a corrupt frame\n");
        return NULL;
    }
    if( ToriRSMapEd_BufAvailable(&remote->in)
        < TORIRSMAPED_FRAME_HEADER + (int)length )
        return NULL;

    *out_type = ToriRSMapEd_ReadU32(head);
    *out_length = length;
    ToriRSMapEd_BufConsume(&remote->in, TORIRSMAPED_FRAME_HEADER);
    return ToriRSMapEd_BufPeek(&remote->in);
}

static void
frame_done(
    struct maped_remote* remote,
    uint32_t length)
{
    ToriRSMapEd_BufConsume(&remote->in, (int)length);
}

static void
stash_broadcast(
    struct maped_remote* remote,
    uint32_t type,
    const uint8_t* body,
    uint32_t length)
{
    struct stashed_fact* fact = ToriMem_Alloc(ToriMem_Editor(), sizeof(*fact) + length);

    fact->next = NULL;
    fact->type = type;
    fact->length = length;
    memcpy(fact->data, body, length);
    if( remote->stash_tail )
        remote->stash_tail->next = fact;
    else
        remote->stash_head = fact;
    remote->stash_tail = fact;
}

/**
 * Block until a frame of type `want_a` (or `want_b`, when non-zero) arrives,
 * up to the timeout. Broadcasts encountered on the way are stashed for
 * DrainFacts; any other type is a protocol error and returns NULL.
 */
static const uint8_t*
await_reply(
    struct maped_remote* remote,
    int timeout_ms,
    uint32_t want_a,
    uint32_t want_b,
    uint32_t* out_type,
    uint32_t* out_length)
{
    int waited_ms = 0;

    assert(remote);
    assert(out_type);
    assert(out_length);

    for( ;; )
    {
        const uint8_t* body = take_frame(remote, out_type, out_length);
        if( body )
        {
            if( *out_type == want_a || (want_b && *out_type == want_b) )
                return body;
            if( is_broadcast(*out_type) )
            {
                stash_broadcast(remote, *out_type, body, *out_length);
                frame_done(remote, *out_length);
                continue;
            }
            TORIRS_ERR("editor: maped answered frame %u where %u was expected\n",
                *out_type,
                want_a);
            frame_done(remote, *out_length);
            return NULL;
        }

        int got = link_fill(remote, 500);
        if( got < 0 )
        {
            TORIRS_LOG("editor: maped server is gone\n");
            return NULL;
        }
        if( got == 0 )
        {
            waited_ms += 500;
            if( waited_ms >= timeout_ms )
            {
                TORIRS_LOG("editor: maped did not answer within %d ms\n", timeout_ms);
                return NULL;
            }
        }
    }
}

static int
send_request(
    struct maped_remote* remote,
    uint32_t type,
    const uint8_t* payload,
    uint32_t length)
{
    uint8_t header[TORIRSMAPED_FRAME_HEADER];

    ToriRSMapEd_WriteU32(header, type);
    ToriRSMapEd_WriteU32(header + 4, length);
    if( !link_send(remote, header, sizeof(header)) )
        return 0;
    if( length > 0 && !link_send(remote, payload, (int)length) )
        return 0;
    return 1;
}

/** Send one request and wait for the FACT_STATUS answering it.
 *  `out_extra` (optional) receives the status frame's count field. */
static enum EditorHost_Status
rpc_status(
    struct maped_remote* remote,
    uint32_t request,
    const uint8_t* payload,
    uint32_t length,
    uint32_t* out_extra)
{
    uint32_t fact_type;
    uint32_t fact_length;
    const uint8_t* fact;
    enum EditorHost_Status status;

    if( !send_request(remote, request, payload, length) )
        return EDITOR_HOST_IO_ERROR;

    fact = await_reply(
        remote, REMOTE_OP_TIMEOUT_MS, TORIRSMAPED_FACT_STATUS, 0, &fact_type, &fact_length);
    if( !fact || fact_length != 12 || ToriRSMapEd_ReadU32(fact) != request )
    {
        if( fact )
            frame_done(remote, fact_length);
        return EDITOR_HOST_IO_ERROR;
    }
    status = (enum EditorHost_Status)ToriRSMapEd_ReadU32(fact + 4);
    if( out_extra )
        *out_extra = ToriRSMapEd_ReadU32(fact + 8);
    frame_done(remote, fact_length);
    return status;
}

/* ------------------------------------------------------------------ */
/* The EditorHost vtable — the file-flavoured half                     */
/* ------------------------------------------------------------------ */

static enum EditorHost_Status
remote_square_list(
    void* user_data,
    int* out_coords,
    int max,
    int* out_count)
{
    struct maped_remote* remote = user_data;
    uint32_t fact_type;
    uint32_t fact_length;
    const uint8_t* fact;
    enum EditorHost_Status status;
    int count;

    assert(remote);
    assert(out_count);
    assert(out_coords || max == 0);

    if( !send_request(remote, TORIRSMAPED_REQ_SQUARE_LIST, NULL, 0) )
        return EDITOR_HOST_IO_ERROR;

    fact = await_reply(
        remote,
        REMOTE_OP_TIMEOUT_MS,
        TORIRSMAPED_FACT_SQUARE_LIST,
        0,
        &fact_type,
        &fact_length);
    if( !fact || fact_length < 8 )
    {
        if( fact )
            frame_done(remote, fact_length);
        return EDITOR_HOST_IO_ERROR;
    }
    status = (enum EditorHost_Status)ToriRSMapEd_ReadU32(fact);
    count = (int)ToriRSMapEd_ReadU32(fact + 4);
    if( fact_length != 8 + (uint32_t)count * 8 )
    {
        frame_done(remote, fact_length);
        return EDITOR_HOST_IO_ERROR;
    }
    for( int i = 0; i < count && i < max; i++ )
    {
        out_coords[i * 2] = (int)ToriRSMapEd_ReadU32(fact + 8 + (uint32_t)i * 8);
        out_coords[i * 2 + 1] = (int)ToriRSMapEd_ReadU32(fact + 12 + (uint32_t)i * 8);
    }
    *out_count = count;
    frame_done(remote, fact_length);
    return status;
}

/** Shared tail of SQUARE_LOAD and SQUARE_TEXT decoding: two size_plus fields
 *  at `at`, text following, into NUL-terminated blobs. Returns 0 on a length
 *  mismatch. */
static int
decode_two_blobs(
    const uint8_t* fact,
    uint32_t fact_length,
    uint32_t at,
    struct EditorHost_Blob* out_jm2,
    struct EditorHost_Blob* out_jl2)
{
    uint32_t jm2_plus = ToriRSMapEd_ReadU32(fact + at);
    uint32_t jl2_plus = ToriRSMapEd_ReadU32(fact + at + 4);
    size_t jm2_size = jm2_plus > 0 ? jm2_plus - 1 : 0;
    size_t jl2_size = jl2_plus > 0 ? jl2_plus - 1 : 0;

    if( at + 8 + jm2_size + jl2_size != fact_length )
        return 0;

    /* Blob contract: NUL-terminated for the codec, `size` excluding it. */
    if( jm2_plus > 0 )
    {
        out_jm2->data = ToriMem_Alloc(ToriMem_Editor(), jm2_size + 1);
        memcpy(out_jm2->data, fact + at + 8, jm2_size);
        out_jm2->data[jm2_size] = '\0';
        out_jm2->size = jm2_size;
    }
    if( jl2_plus > 0 )
    {
        out_jl2->data = ToriMem_Alloc(ToriMem_Editor(), jl2_size + 1);
        memcpy(out_jl2->data, fact + at + 8 + jm2_size, jl2_size);
        out_jl2->data[jl2_size] = '\0';
        out_jl2->size = jl2_size;
    }
    return 1;
}

static enum EditorHost_Status
remote_square_load(
    void* user_data,
    int map_x,
    int map_z,
    struct EditorHost_Blob* out_jm2,
    struct EditorHost_Blob* out_jl2)
{
    struct maped_remote* remote = user_data;
    uint8_t payload[8];
    uint32_t fact_type;
    uint32_t fact_length;
    const uint8_t* fact;
    enum EditorHost_Status status;

    assert(remote);
    assert(out_jm2);
    assert(out_jl2);

    out_jm2->data = NULL;
    out_jm2->size = 0;
    out_jl2->data = NULL;
    out_jl2->size = 0;

    ToriRSMapEd_WriteU32(payload, (uint32_t)map_x);
    ToriRSMapEd_WriteU32(payload + 4, (uint32_t)map_z);
    if( !send_request(remote, TORIRSMAPED_REQ_SQUARE_LOAD, payload, sizeof(payload)) )
        return EDITOR_HOST_IO_ERROR;

    fact = await_reply(
        remote,
        REMOTE_OP_TIMEOUT_MS,
        TORIRSMAPED_FACT_SQUARE_LOAD,
        0,
        &fact_type,
        &fact_length);
    if( !fact || fact_length < 12 )
    {
        if( fact )
            frame_done(remote, fact_length);
        return EDITOR_HOST_IO_ERROR;
    }
    status = (enum EditorHost_Status)ToriRSMapEd_ReadU32(fact);
    if( !decode_two_blobs(fact, fact_length, 4, out_jm2, out_jl2) )
        status = EDITOR_HOST_IO_ERROR;
    frame_done(remote, fact_length);
    return status;
}

static enum EditorHost_Status
remote_square_save(
    void* user_data,
    int map_x,
    int map_z,
    const char* jm2_text,
    size_t jm2_length,
    const char* jl2_text,
    size_t jl2_length)
{
    struct maped_remote* remote = user_data;
    size_t jm2_size = jm2_text ? jm2_length : 0;
    size_t jl2_size = jl2_text ? jl2_length : 0;
    uint32_t length = (uint32_t)(16 + jm2_size + jl2_size);
    uint8_t* payload;
    enum EditorHost_Status status;

    assert(remote);

    payload = ToriMem_Alloc(ToriMem_Editor(), length);
    ToriRSMapEd_WriteU32(payload, (uint32_t)map_x);
    ToriRSMapEd_WriteU32(payload + 4, (uint32_t)map_z);
    ToriRSMapEd_WriteU32(payload + 8, jm2_text ? (uint32_t)jm2_size + 1 : 0);
    ToriRSMapEd_WriteU32(payload + 12, jl2_text ? (uint32_t)jl2_size + 1 : 0);
    if( jm2_size > 0 )
        memcpy(payload + 16, jm2_text, jm2_size);
    if( jl2_size > 0 )
        memcpy(payload + 16 + jm2_size, jl2_text, jl2_size);

    status = rpc_status(remote, TORIRSMAPED_REQ_SQUARE_SAVE, payload, length, NULL);
    ToriMem_Free(ToriMem_Editor(), payload);
    return status;
}

static enum EditorHost_Status
remote_spawn_save(
    void* user_data,
    int map_x,
    int map_z,
    const char* text,
    size_t length)
{
    struct maped_remote* remote = user_data;
    size_t size = text ? length : 0;
    uint32_t payload_length = (uint32_t)(12 + size);
    uint8_t* payload;
    enum EditorHost_Status status;

    assert(remote);

    payload = ToriMem_Alloc(ToriMem_Editor(), payload_length);
    ToriRSMapEd_WriteU32(payload, (uint32_t)map_x);
    ToriRSMapEd_WriteU32(payload + 4, (uint32_t)map_z);
    ToriRSMapEd_WriteU32(payload + 8, text ? (uint32_t)size + 1 : 0);
    if( size > 0 )
        memcpy(payload + 12, text, size);

    status = rpc_status(remote, TORIRSMAPED_REQ_SPAWN_SAVE, payload, payload_length, NULL);
    ToriMem_Free(ToriMem_Editor(), payload);
    return status;
}

static enum EditorHost_Status
remote_bake(
    void* user_data,
    EditorHost_ProgressFn on_progress,
    void* progress_user_data)
{
    struct maped_remote* remote = user_data;

    assert(remote);

    if( !send_request(remote, TORIRSMAPED_REQ_BAKE, NULL, 0) )
        return EDITOR_HOST_BAKE_FAILED;

    for( ;; )
    {
        uint32_t fact_type;
        uint32_t fact_length;
        const uint8_t* fact = await_reply(
            remote,
            REMOTE_BAKE_TIMEOUT_MS,
            TORIRSMAPED_FACT_BAKE_LINE,
            TORIRSMAPED_FACT_BAKE_DONE,
            &fact_type,
            &fact_length);

        if( !fact )
            return EDITOR_HOST_BAKE_FAILED;

        if( fact_type == TORIRSMAPED_FACT_BAKE_LINE )
        {
            if( on_progress )
            {
                char* line = ToriMem_Alloc(ToriMem_Editor(), fact_length + 1);
                memcpy(line, fact, fact_length);
                line[fact_length] = '\0';
                on_progress(progress_user_data, line);
                ToriMem_Free(ToriMem_Editor(), line);
            }
            frame_done(remote, fact_length);
            continue;
        }
        if( fact_length == 4 )
        {
            enum EditorHost_Status status =
                (enum EditorHost_Status)ToriRSMapEd_ReadU32(fact);
            frame_done(remote, fact_length);
            return status;
        }
        frame_done(remote, fact_length);
        return EDITOR_HOST_BAKE_FAILED;
    }
}

/** The lock is the server's now: `acquire` answers what HELLO reported, so a
 *  caller probing "can this session save" keeps its one code path. */
static enum EditorHost_Status
remote_session(
    void* user_data,
    int acquire)
{
    struct maped_remote* remote = user_data;

    assert(remote);

    if( !acquire )
        return EDITOR_HOST_OK;
    return remote->writable ? EDITOR_HOST_OK : EDITOR_HOST_LOCKED;
}

static void
free_stash(struct maped_remote* remote)
{
    while( remote->stash_head )
    {
        struct stashed_fact* next = remote->stash_head->next;
        ToriMem_Free(ToriMem_Editor(), remote->stash_head);
        remote->stash_head = next;
    }
    remote->stash_tail = NULL;
}

static void
remote_free(void* user_data)
{
    struct maped_remote* remote = user_data;
    uint8_t header[TORIRSMAPED_FRAME_HEADER];

    if( !remote )
        return;

    /* Best-effort goodbye so the daemon logs a departure, not a drop. */
    ToriRSMapEd_WriteU32(header, TORIRSMAPED_REQ_BYE);
    ToriRSMapEd_WriteU32(header + 4, 0);
    link_send(remote, header, sizeof(header));

#if !defined(TORIRS_MAPED_NO_EMBED)
    if( remote->embed )
    {
        ToriRSMapEd_EmbedPump(remote->embed);
        if( remote->borrowed_embed )
            ToriRSMapEd_EmbedDisconnect(remote->embed, remote->embed_client);
        else
            ToriRSMapEd_EmbedStop(remote->embed);
    }
#endif
#if !defined(_WIN32)
    if( remote->fd >= 0 )
        close(remote->fd);
#endif
    free_stash(remote);
    ToriMem_Free(ToriMem_Editor(), remote->atlas_changed);
    while( remote->live_head )
    {
        struct stashed_fact* next = remote->live_head->next;
        ToriMem_Free(ToriMem_Editor(), remote->live_head);
        remote->live_head = next;
    }
    ToriRSMapEd_BufFree(&remote->in);
    ToriMem_Free(ToriMem_Editor(), remote);
}

static enum EditorHost_Status
open_doc(
    struct maped_remote* remote,
    enum Editor_DocKind kind,
    const char* key,
    struct EditorHost_DocText* out_text);

/** The AUTHORITY's text of one part — what the document says now, which is
 *  what a client reading "the file" through the server should see. */
static enum EditorHost_Status
remote_doc_load(
    void* user_data,
    enum Editor_DocKind kind,
    const char* key,
    int part,
    struct EditorHost_Blob* out_blob)
{
    struct maped_remote* remote = user_data;
    struct EditorHost_DocText text;
    enum EditorHost_Status status;

    assert(remote);
    assert(key);
    assert(out_blob);

    out_blob->data = NULL;
    out_blob->size = 0;
    status = open_doc(remote, kind, key, &text);
    if( status == EDITOR_HOST_OK )
    {
        if( part < 0 || part >= text.part_count )
            status = EDITOR_HOST_REFUSED;
        else if( !text.parts[part].data )
            status = EDITOR_HOST_ABSENT;
        else
        {
            *out_blob = text.parts[part];
            text.parts[part].data = NULL;
            text.parts[part].size = 0;
        }
    }
    Editor_HostDocTextFree(&text);
    return status;
}

/** A client never writes files: the authority saves its own documents. */
static enum EditorHost_Status
remote_doc_save(
    void* user_data,
    enum Editor_DocKind kind,
    const char* key,
    int part,
    const char* text,
    size_t length)
{
    (void)user_data;
    (void)kind;
    (void)key;
    (void)part;
    (void)text;
    (void)length;
    return EDITOR_HOST_UNSUPPORTED;
}

static const struct EditorHost_VTable remote_vtable = {
    remote_square_list, remote_square_load, remote_square_save, remote_spawn_save,
    remote_bake,        remote_session,     remote_free,        remote_doc_load,
    remote_doc_save,
};

/* ------------------------------------------------------------------ */
/* Opening a connection                                                */
/* ------------------------------------------------------------------ */

/** The binding behind a host, asserted to be this one — the document layer
 *  has no meaning against any other. */
static struct maped_remote*
as_remote(const struct EditorHost* host)
{
    assert(host);
    assert(host->vtable == &remote_vtable);
    return host->user_data;
}

int
Editor_HostIsMapEd(const struct EditorHost* host)
{
    assert(host);
    return host->vtable == &remote_vtable;
}

/** The HELLO round trip: proves the wire before the editor builds on it,
 *  learns whether the server can save its tree, and settles which Client
 *  (session group) this connection belongs to — `group` 0 asks for a fresh
 *  one, anything else joins that group's state relay. */
static int
hello(
    struct maped_remote* remote,
    uint32_t role,
    uint32_t group)
{
    uint8_t payload[12];
    uint32_t fact_type;
    uint32_t fact_length;
    const uint8_t* fact;
    uint32_t version;

    ToriRSMapEd_WriteU32(payload, TORIRSMAPED_PROTO_VERSION);
    ToriRSMapEd_WriteU32(payload + 4, role);
    ToriRSMapEd_WriteU32(payload + 8, group);
    if( !send_request(remote, TORIRSMAPED_REQ_HELLO, payload, sizeof(payload)) )
        return 0;

    /* 16 bytes from a v2 server; a v1 server answers 12 and fails the
     * version check below. */
    fact = await_reply(
        remote, REMOTE_OP_TIMEOUT_MS, TORIRSMAPED_FACT_HELLO, 0, &fact_type, &fact_length);
    if( !fact || (fact_length != 12 && fact_length != 16) )
    {
        if( fact )
            frame_done(remote, fact_length);
        return 0;
    }
    version = ToriRSMapEd_ReadU32(fact);
    remote->writable = (int)ToriRSMapEd_ReadU32(fact + 4);
    remote->client_id = ToriRSMapEd_ReadU32(fact + 8);
    remote->capabilities = fact_length == 16 ? ToriRSMapEd_ReadU32(fact + 12) : 0;
    frame_done(remote, fact_length);
    if( version != TORIRSMAPED_PROTO_VERSION )
    {
        TORIRS_LOG("editor: maped speaks protocol %u, this client %d\n",
            version,
            TORIRSMAPED_PROTO_VERSION);
        return 0;
    }
    return 1;
}

#if !defined(TORIRS_MAPED_NO_EMBED)

int
Editor_HostOpenMapEdEmbed(
    struct EditorHost* host,
    const char* content_dir,
    const char* repo_root)
{
    struct maped_remote* remote;

    assert(host);
    assert(content_dir);

    remote = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*remote));
    remote->fd = -1;

    remote->embed = ToriRSMapEd_EmbedStart(content_dir, repo_root);
    if( !remote->embed )
    {
        ToriMem_Free(ToriMem_Editor(), remote);
        return 0;
    }
    remote->embed_client = 0;

    if( !hello(remote, TORIRSMAPED_ROLE_GENERIC, 0) )
    {
        ToriRSMapEd_EmbedStop(remote->embed);
        ToriRSMapEd_BufFree(&remote->in);
        ToriMem_Free(ToriMem_Editor(), remote);
        return 0;
    }

    host->vtable = &remote_vtable;
    host->user_data = remote;
    return 1;
}

int
Editor_HostOpenMapEdEmbedPeer(
    struct EditorHost* host,
    const struct EditorHost* peer,
    int role,
    int join_group)
{
    struct maped_remote* peer_remote;
    struct maped_remote* remote;
    int client_id;

    assert(host);

    peer_remote = as_remote(peer);
    assert(peer_remote->embed);

    client_id = ToriRSMapEd_EmbedConnect(peer_remote->embed);
    if( client_id < 0 )
    {
        TORIRS_LOG("editor: the embedded maped has no free connection\n");
        return 0;
    }

    remote = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*remote));
    remote->fd = -1;
    remote->embed = peer_remote->embed;
    remote->embed_client = client_id;
    remote->borrowed_embed = 1;

    if( !hello(remote, (uint32_t)role, join_group ? peer_remote->client_id : 0) )
    {
        ToriRSMapEd_EmbedDisconnect(remote->embed, client_id);
        ToriRSMapEd_BufFree(&remote->in);
        ToriMem_Free(ToriMem_Editor(), remote);
        return 0;
    }

    host->vtable = &remote_vtable;
    host->user_data = remote;
    return 1;
}

#endif /* !TORIRS_MAPED_NO_EMBED */

int
Editor_HostOpenMapEdTcp(
    struct EditorHost* host,
    const char* server_host,
    int port,
    int role,
    uint32_t join_group)
{
#if !defined(_WIN32)
    struct maped_remote* remote;
    struct addrinfo hints;
    struct addrinfo* found = NULL;
    char port_text[16];
    int fd = -1;

    assert(host);
    assert(server_host);

    if( port <= 0 )
        port = TORIRSMAPED_DEFAULT_PORT;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port_text, sizeof(port_text), "%d", port);
    if( getaddrinfo(server_host, port_text, &hints, &found) != 0 || !found )
    {
        TORIRS_ERR("editor: cannot resolve maped host %s\n", server_host);
        return 0;
    }
    for( struct addrinfo* cursor = found; cursor; cursor = cursor->ai_next )
    {
        fd = socket(cursor->ai_family, cursor->ai_socktype, cursor->ai_protocol);
        if( fd < 0 )
            continue;
        if( connect(fd, cursor->ai_addr, cursor->ai_addrlen) == 0 )
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(found);
    if( fd < 0 )
    {
        TORIRS_ERR("editor: cannot connect to maped at %s:%d\n", server_host, port);
        return 0;
    }

    remote = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*remote));
    remote->fd = fd;

    if( !hello(remote, (uint32_t)role, join_group) )
    {
        close(fd);
        ToriRSMapEd_BufFree(&remote->in);
        ToriMem_Free(ToriMem_Editor(), remote);
        return 0;
    }

    host->vtable = &remote_vtable;
    host->user_data = remote;
    return 1;
#else
    (void)host;
    (void)server_host;
    (void)port;
    (void)role;
    (void)join_group;
    TORIRS_LOG("editor: server=tcp is not built for this platform yet\n");
    return 0;
#endif
}

/* ------------------------------------------------------------------ */
/* The document and state layer                                        */
/* ------------------------------------------------------------------ */

int
Editor_HostMapEdWritable(const struct EditorHost* host)
{
    return as_remote(host)->writable;
}

uint32_t
Editor_HostMapEdClientId(const struct EditorHost* host)
{
    return as_remote(host)->client_id;
}

uint32_t
Editor_HostMapEdCapabilities(const struct EditorHost* host)
{
    return as_remote(host)->capabilities;
}

void
Editor_HostDocTextFree(struct EditorHost_DocText* text)
{
    if( !text )
        return;

    Editor_HostBlobFree(&text->parts[0]);
    Editor_HostBlobFree(&text->parts[1]);
    text->part_count = 0;
}

static enum EditorHost_Status
open_doc(
    struct maped_remote* remote,
    enum Editor_DocKind kind,
    const char* key,
    struct EditorHost_DocText* out_text)
{
    struct ToriRSMapEdBuf payload;
    struct ToriRSMapEdReader reader;
    uint32_t fact_type;
    uint32_t fact_length;
    const uint8_t* fact;
    enum EditorHost_Status status;
    uint32_t part_count;
    int sent;

    memset(out_text, 0, sizeof(*out_text));
    out_text->kind = kind;

    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutU32(&payload, (uint32_t)kind);
    ToriRSMapEd_PutString(&payload, key);
    sent = send_request(
        remote,
        TORIRSMAPED_REQ_DOC_OPEN,
        ToriRSMapEd_BufPeek(&payload),
        (uint32_t)ToriRSMapEd_BufAvailable(&payload));
    ToriRSMapEd_BufFree(&payload);
    if( !sent )
        return EDITOR_HOST_IO_ERROR;

    fact = await_reply(
        remote, REMOTE_OP_TIMEOUT_MS, TORIRSMAPED_FACT_DOC_TEXT, 0, &fact_type, &fact_length);
    if( !fact )
        return EDITOR_HOST_IO_ERROR;

    ToriRSMapEd_ReaderInit(&reader, fact, fact_length);
    status = (enum EditorHost_Status)ToriRSMapEd_GetU32(&reader);
    out_text->kind = (enum Editor_DocKind)ToriRSMapEd_GetU32(&reader);
    ToriRSMapEd_GetString(&reader, out_text->key, sizeof(out_text->key));
    ToriRSMapEd_GetString(&reader, out_text->storage, sizeof(out_text->storage));
    out_text->dirty = ToriRSMapEd_GetU32(&reader);
    part_count = ToriRSMapEd_GetU32(&reader);
    if( part_count > 2 )
        reader.bad = 1;
    for( uint32_t i = 0; i < part_count && !reader.bad; i++ )
    {
        uint32_t size_plus = ToriRSMapEd_GetU32(&reader);
        uint32_t size = size_plus > 0 ? size_plus - 1 : 0;

        if( reader.bad || reader.length - reader.at < size )
        {
            reader.bad = 1;
            break;
        }
        /* Blob contract: NUL-terminated for the codec, `size` excluding it;
         * absent (size_plus 0) stays NULL. */
        if( size_plus > 0 )
        {
            out_text->parts[i].data = ToriMem_Alloc(ToriMem_Editor(), size + 1);
            memcpy(out_text->parts[i].data, fact + reader.at, size);
            out_text->parts[i].data[size] = '\0';
            out_text->parts[i].size = size;
        }
        reader.at += size;
    }
    out_text->part_count = (int)part_count;
    if( !ToriRSMapEd_ReaderDone(&reader) )
    {
        Editor_HostDocTextFree(out_text);
        status = EDITOR_HOST_IO_ERROR;
    }
    frame_done(remote, fact_length);
    return status;
}

enum EditorHost_Status
Editor_HostMapEdDocOpen(
    struct EditorHost* host,
    enum Editor_DocKind kind,
    const char* key,
    struct EditorHost_DocText* out_text)
{
    assert(key);
    assert(out_text);
    return open_doc(as_remote(host), kind, key, out_text);
}

/** Send `u32 kind, string key` as `request` and await its FACT_STATUS. */
static enum EditorHost_Status
rpc_doc_ref(
    struct maped_remote* remote,
    uint32_t request,
    enum Editor_DocKind kind,
    const char* key,
    uint32_t* out_extra)
{
    struct ToriRSMapEdBuf payload;
    enum EditorHost_Status status;

    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutU32(&payload, (uint32_t)kind);
    ToriRSMapEd_PutString(&payload, key);
    status = rpc_status(
        remote,
        request,
        ToriRSMapEd_BufPeek(&payload),
        (uint32_t)ToriRSMapEd_BufAvailable(&payload),
        out_extra);
    ToriRSMapEd_BufFree(&payload);
    return status;
}

enum EditorHost_Status
Editor_HostMapEdDocClose(
    struct EditorHost* host,
    enum Editor_DocKind kind,
    const char* key)
{
    assert(key);
    return rpc_doc_ref(as_remote(host), TORIRSMAPED_REQ_DOC_CLOSE, kind, key, NULL);
}

int
Editor_HostMapEdSave(
    struct EditorHost* host,
    enum Editor_DocKind kind,
    const char* key)
{
    uint32_t count = 0;
    enum EditorHost_Status status;

    assert(key);

    status = rpc_doc_ref(as_remote(host), TORIRSMAPED_REQ_SAVE, kind, key, &count);
    if( status != EDITOR_HOST_OK )
        return -1;
    return (int)count;
}

enum EditorHost_Status
Editor_HostMapEdDocCreate(
    struct EditorHost* host,
    const struct EditorHost_DocCreate* request)
{
    struct maped_remote* remote = as_remote(host);
    struct ToriRSMapEdBuf payload;
    enum EditorHost_Status status;

    assert(request);

    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutU32(&payload, (uint32_t)request->kind);
    ToriRSMapEd_PutString(&payload, request->key);
    ToriRSMapEd_PutString(&payload, request->storage);
    ToriRSMapEd_PutU32(&payload, (uint32_t)request->template_kind);
    ToriRSMapEd_PutString(&payload, request->source_key);
    ToriRSMapEd_PutU32(&payload, (uint32_t)request->fill_height);
    ToriRSMapEd_PutU32(&payload, (uint32_t)request->fill_underlay);
    status = rpc_status(
        remote,
        TORIRSMAPED_REQ_DOC_CREATE,
        ToriRSMapEd_BufPeek(&payload),
        (uint32_t)ToriRSMapEd_BufAvailable(&payload),
        NULL);
    ToriRSMapEd_BufFree(&payload);
    return status;
}

enum EditorHost_Status
Editor_HostMapEdSquareOpen(
    struct EditorHost* host,
    int map_x,
    int map_z,
    struct EditorHost_Blob* out_jm2,
    struct EditorHost_Blob* out_jl2,
    int* out_dirty_map,
    int* out_dirty_loc)
{
    struct maped_remote* remote = as_remote(host);
    uint8_t payload[8];
    uint32_t fact_type;
    uint32_t fact_length;
    const uint8_t* fact;
    enum EditorHost_Status status;

    assert(out_jm2);
    assert(out_jl2);
    assert(out_dirty_map);
    assert(out_dirty_loc);

    out_jm2->data = NULL;
    out_jm2->size = 0;
    out_jl2->data = NULL;
    out_jl2->size = 0;
    *out_dirty_map = 0;
    *out_dirty_loc = 0;

    ToriRSMapEd_WriteU32(payload, (uint32_t)map_x);
    ToriRSMapEd_WriteU32(payload + 4, (uint32_t)map_z);
    if( !send_request(remote, TORIRSMAPED_REQ_SQUARE_OPEN, payload, sizeof(payload)) )
        return EDITOR_HOST_IO_ERROR;

    fact = await_reply(
        remote,
        REMOTE_OP_TIMEOUT_MS,
        TORIRSMAPED_FACT_SQUARE_TEXT,
        0,
        &fact_type,
        &fact_length);
    if( !fact || fact_length < 28 )
    {
        if( fact )
            frame_done(remote, fact_length);
        return EDITOR_HOST_IO_ERROR;
    }
    status = (enum EditorHost_Status)ToriRSMapEd_ReadU32(fact);
    *out_dirty_map = (int)ToriRSMapEd_ReadU32(fact + 12);
    *out_dirty_loc = (int)ToriRSMapEd_ReadU32(fact + 16);
    if( !decode_two_blobs(fact, fact_length, 20, out_jm2, out_jl2) )
        status = EDITOR_HOST_IO_ERROR;
    frame_done(remote, fact_length);
    return status;
}

enum EditorHost_Status
Editor_HostMapEdCmd(
    struct EditorHost* host,
    const struct Editor_Cmd* command)
{
    struct maped_remote* remote = as_remote(host);
    struct ToriRSMapEdBuf payload;
    enum EditorHost_Status status;

    assert(command);

    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_CmdEncode(command, &payload);
    status = rpc_status(
        remote,
        TORIRSMAPED_REQ_CMD,
        ToriRSMapEd_BufPeek(&payload),
        (uint32_t)ToriRSMapEd_BufAvailable(&payload),
        NULL);
    ToriRSMapEd_BufFree(&payload);
    return status;
}

static int
history_request(
    struct EditorHost* host,
    uint32_t request)
{
    struct maped_remote* remote = as_remote(host);
    uint32_t count = 0;

    if( rpc_status(remote, request, NULL, 0, &count) != EDITOR_HOST_OK )
        return 0;
    return (int)count;
}

int
Editor_HostMapEdUndo(struct EditorHost* host)
{
    return history_request(host, TORIRSMAPED_REQ_UNDO);
}

int
Editor_HostMapEdRedo(struct EditorHost* host)
{
    return history_request(host, TORIRSMAPED_REQ_REDO);
}

enum EditorHost_Status
Editor_HostMapEdStroke(
    struct EditorHost* host,
    int begin)
{
    struct maped_remote* remote = as_remote(host);
    uint8_t payload[4];

    ToriRSMapEd_WriteU32(payload, (uint32_t)begin);
    return rpc_status(remote, TORIRSMAPED_REQ_STROKE, payload, sizeof(payload), NULL);
}

int
Editor_HostMapEdSaveAll(struct EditorHost* host)
{
    struct maped_remote* remote = as_remote(host);
    uint32_t count = 0;
    enum EditorHost_Status status =
        rpc_status(remote, TORIRSMAPED_REQ_SAVE_ALL, NULL, 0, &count);

    if( status == EDITOR_HOST_LOCKED )
        return -1;
    if( status != EDITOR_HOST_OK )
        return -1;
    return (int)count;
}

int
Editor_HostMapEdStateSet(
    struct EditorHost* host,
    uint32_t key,
    const int32_t* values,
    int count)
{
    struct maped_remote* remote = as_remote(host);
    uint8_t payload[8 + 4 * TORIRSMAPED_STATE_VALUES_MAX];

    assert(values || count == 0);
    assert(count >= 0);
    assert(count <= TORIRSMAPED_STATE_VALUES_MAX);

    ToriRSMapEd_WriteU32(payload, key);
    ToriRSMapEd_WriteU32(payload + 4, (uint32_t)count);
    for( int i = 0; i < count; i++ )
        ToriRSMapEd_WriteU32(payload + 8 + (uint32_t)i * 4, (uint32_t)values[i]);
    /* Fire and forget: the echo arrives as a FACT_STATE broadcast. */
    return send_request(remote, TORIRSMAPED_REQ_STATE_SET, payload, 8 + (uint32_t)count * 4);
}

int
Editor_HostMapEdStateSync(struct EditorHost* host)
{
    struct maped_remote* remote = as_remote(host);
    uint32_t count = 0;

    /* The replayed FACT_STATEs are broadcasts by type, so await_reply stashes
     * each one on the way to the status frame — DrainFacts then delivers
     * them, which is exactly the path a live change takes. */
    if( rpc_status(remote, TORIRSMAPED_REQ_STATE_SYNC, NULL, 0, &count) != EDITOR_HOST_OK )
        return -1;
    return (int)count;
}

enum EditorHost_Status
Editor_HostMapEdSpawnView(
    struct EditorHost* host,
    const int32_t* camera,
    int* out_pid)
{
    struct maped_remote* remote = as_remote(host);
    uint8_t payload[4 * TORIRSMAPED_CAMERA_VALUES];
    uint32_t pid = 0;
    enum EditorHost_Status status;

    assert(camera);
    assert(out_pid);
    for( int i = 0; i < TORIRSMAPED_CAMERA_VALUES; i++ )
        ToriRSMapEd_WriteU32(payload + 4 * i, (uint32_t)camera[i]);
    status = rpc_status(remote, TORIRSMAPED_REQ_SPAWN_VIEW, payload, sizeof(payload), &pid);
    *out_pid = (int)pid;
    return status;
}

int
Editor_HostMapEdLiveSend(struct EditorHost* host, uint32_t type, const uint8_t* payload, uint32_t length)
{
    struct maped_remote* remote = as_remote(host);
    assert(payload || length == 0);
    /* Only the live-editor's requests ride this: anything else has its own
     * call that awaits its answer. */
    assert(type >= TORIRSMAPED_REQ_LIVE_RANGE_FIRST);
    assert(type <= TORIRSMAPED_REQ_LIVE_RANGE_LAST);
    return send_request(remote, type, payload, length);
}

int
Editor_HostMapEdLiveTake(struct EditorHost* host, uint32_t* out_type, uint8_t** out_body, uint32_t* out_length)
{
    struct maped_remote* remote = as_remote(host);
    struct stashed_fact* fact;
    assert(out_type);
    assert(out_body);
    assert(out_length);
    fact = remote->live_head;
    if( !fact )
        return 0;
    remote->live_head = fact->next;
    if( !remote->live_head )
        remote->live_tail = NULL;
    /* The fact's own allocation carries the body: hand back a copy the
     * caller frees, and free the record. */
    *out_type = fact->type;
    *out_length = fact->length;
    *out_body = ToriMem_Alloc(ToriMem_Editor(), fact->length ? fact->length : 1);
    assert(*out_body);
    if( fact->length > 0 )
        memcpy(*out_body, fact->data, fact->length);
    ToriMem_Free(ToriMem_Editor(), fact);
    return 1;
}

/* ------------------------------------------------------------------ */
/* The catalog                                                         */
/* ------------------------------------------------------------------ */

/*
 * Each answer is decoded into ONE allocation: the arrays first (sized from
 * bounds the payload length gives — a wire string costs at least four bytes,
 * so no count can exceed length / its minimum encoding), then the strings,
 * NUL-terminated (a string never grows: `u32 len, bytes` becomes
 * `bytes, NUL`). A hostile count past those bounds is malformed, not an
 * allocation request.
 */

struct catalog_arena
{
    char* at;
    char* end;
};

static const char*
arena_string(
    struct ToriRSMapEdReader* reader,
    struct catalog_arena* arena)
{
    uint32_t length;
    const uint8_t* bytes = ToriRSMapEd_GetBytes(reader, &length);
    char* text;
    if( !bytes || (size_t)(arena->end - arena->at) < (size_t)length + 1 )
    {
        reader->bad = 1;
        return "";
    }
    text = arena->at;
    memcpy(text, bytes, length);
    text[length] = '\0';
    arena->at += length + 1;
    return text;
}

/** Send `payload` as `request` and await `fact`; NULL when the link failed. */
static const uint8_t*
catalog_rpc(
    struct maped_remote* remote,
    uint32_t request,
    struct ToriRSMapEdBuf* payload,
    uint32_t fact,
    uint32_t* out_length)
{
    uint32_t fact_type;
    int sent = send_request(
        remote,
        request,
        ToriRSMapEd_BufPeek(payload),
        (uint32_t)ToriRSMapEd_BufAvailable(payload));
    ToriRSMapEd_BufFree(payload);
    if( !sent )
        return NULL;
    return await_reply(remote, REMOTE_OP_TIMEOUT_MS, fact, 0, &fact_type, out_length);
}

enum EditorHost_Status
Editor_HostMapEdCatalogQuery(
    struct EditorHost* host,
    uint32_t kind,
    const char* filter,
    uint32_t flags,
    uint32_t page,
    uint32_t page_size,
    struct EditorHost_CatalogPage* out_page)
{
    struct maped_remote* remote = as_remote(host);
    struct ToriRSMapEdBuf payload;
    struct ToriRSMapEdReader reader;
    struct catalog_arena arena;
    const uint8_t* fact;
    uint32_t length;
    uint32_t rows;
    size_t field_bound;
    struct EditorHost_CatalogField* fields;
    int field_at = 0;
    enum EditorHost_Status status;
    char* mem;

    assert(filter);
    assert(out_page);
    memset(out_page, 0, sizeof(*out_page));
    if( !(remote->capabilities & TORIRSMAPED_CAP_CATALOG) )
        return EDITOR_HOST_UNSUPPORTED;

    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutU32(&payload, kind);
    ToriRSMapEd_PutString(&payload, filter);
    ToriRSMapEd_PutU32(&payload, flags);
    ToriRSMapEd_PutU32(&payload, page);
    ToriRSMapEd_PutU32(&payload, page_size);
    fact = catalog_rpc(remote, TORIRSMAPED_REQ_CATALOG_QUERY, &payload,
        TORIRSMAPED_FACT_CATALOG_PAGE, &length);
    if( !fact )
        return EDITOR_HOST_IO_ERROR;

    ToriRSMapEd_ReaderInit(&reader, fact, length);
    status = (enum EditorHost_Status)ToriRSMapEd_GetU32(&reader);
    out_page->total = ToriRSMapEd_GetU32(&reader);
    out_page->page = ToriRSMapEd_GetU32(&reader);
    out_page->page_size = ToriRSMapEd_GetU32(&reader);
    rows = ToriRSMapEd_GetU32(&reader);
    /* a row is at least 24 bytes, a field at least 8 */
    if( reader.bad || rows > length / 24 )
    {
        frame_done(remote, length);
        memset(out_page, 0, sizeof(*out_page));
        return EDITOR_HOST_IO_ERROR;
    }
    field_bound = length / 8;
    mem = ToriMem_Alloc(ToriMem_Editor(),
        (size_t)rows * sizeof(*out_page->rows) + field_bound * sizeof(*fields) + length + 1);
    assert(mem);
    out_page->storage = mem;
    out_page->rows = (struct EditorHost_CatalogRow*)mem;
    fields = (struct EditorHost_CatalogField*)(mem + (size_t)rows * sizeof(*out_page->rows));
    arena.at = (char*)(fields + field_bound);
    arena.end = arena.at + length + 1;

    for( uint32_t i = 0; i < rows && !reader.bad; i++ )
    {
        struct EditorHost_CatalogRow* row = &out_page->rows[i];
        uint32_t count;
        row->kind = ToriRSMapEd_GetU32(&reader);
        row->id = (int32_t)ToriRSMapEd_GetU32(&reader);
        row->name = arena_string(&reader, &arena);
        row->display = arena_string(&reader, &arena);
        row->provenance = ToriRSMapEd_GetU32(&reader);
        count = ToriRSMapEd_GetU32(&reader);
        if( reader.bad || count > field_bound - (size_t)field_at )
        {
            reader.bad = 1;
            break;
        }
        row->fields = fields + field_at;
        row->field_count = (int)count;
        for( uint32_t f = 0; f < count; f++ )
        {
            fields[field_at].key = arena_string(&reader, &arena);
            fields[field_at].value = arena_string(&reader, &arena);
            field_at++;
        }
    }
    out_page->row_count = (int)rows;
    frame_done(remote, length);
    if( !ToriRSMapEd_ReaderDone(&reader) )
    {
        Editor_HostCatalogPageFree(out_page);
        return EDITOR_HOST_IO_ERROR;
    }
    return status;
}

void
Editor_HostCatalogPageFree(struct EditorHost_CatalogPage* page)
{
    if( !page )
        return;
    ToriMem_Free(ToriMem_Editor(), page->storage);
    memset(page, 0, sizeof(*page));
}

enum EditorHost_Status
Editor_HostMapEdCatalogRecord(
    struct EditorHost* host,
    uint32_t kind,
    int32_t id,
    const char* name,
    struct EditorHost_CatalogRecord* out_record)
{
    struct maped_remote* remote = as_remote(host);
    struct ToriRSMapEdBuf payload;
    struct ToriRSMapEdReader reader;
    struct catalog_arena arena;
    const uint8_t* fact;
    uint32_t length;
    uint32_t count;
    enum EditorHost_Status status;
    char* mem;
    size_t source_bound;
    size_t field_bound;

    assert(name);
    assert(out_record);
    memset(out_record, 0, sizeof(*out_record));
    if( !(remote->capabilities & TORIRSMAPED_CAP_CATALOG) )
        return EDITOR_HOST_UNSUPPORTED;

    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutU32(&payload, kind);
    ToriRSMapEd_PutU32(&payload, (uint32_t)id);
    ToriRSMapEd_PutString(&payload, name);
    fact = catalog_rpc(remote, TORIRSMAPED_REQ_CATALOG_RECORD, &payload,
        TORIRSMAPED_FACT_CATALOG_RECORD, &length);
    if( !fact )
        return EDITOR_HOST_IO_ERROR;

    /* a source is at least 12 bytes, a field at least 20 */
    source_bound = length / 12;
    field_bound = length / 20;
    mem = ToriMem_Alloc(ToriMem_Editor(), source_bound * sizeof(*out_record->sources)
                                              + field_bound * sizeof(*out_record->fields) + length + 1);
    assert(mem);
    out_record->storage = mem;
    out_record->sources = (struct EditorHost_CatalogSource*)mem;
    out_record->fields =
        (struct EditorHost_CatalogRecordField*)(mem + source_bound * sizeof(*out_record->sources));
    arena.at = (char*)(out_record->fields + field_bound);
    arena.end = arena.at + length + 1;

    ToriRSMapEd_ReaderInit(&reader, fact, length);
    status = (enum EditorHost_Status)ToriRSMapEd_GetU32(&reader);
    out_record->kind = ToriRSMapEd_GetU32(&reader);
    out_record->id = (int32_t)ToriRSMapEd_GetU32(&reader);
    out_record->name = arena_string(&reader, &arena);
    out_record->display = arena_string(&reader, &arena);
    out_record->provenance = ToriRSMapEd_GetU32(&reader);
    count = ToriRSMapEd_GetU32(&reader);
    if( count > source_bound )
        reader.bad = 1;
    for( uint32_t i = 0; i < count && !reader.bad; i++ )
    {
        out_record->sources[i].path = arena_string(&reader, &arena);
        out_record->sources[i].provenance = ToriRSMapEd_GetU32(&reader);
        out_record->sources[i].line = ToriRSMapEd_GetU32(&reader);
    }
    out_record->source_count = reader.bad ? 0 : (int)count;
    count = ToriRSMapEd_GetU32(&reader);
    if( count > field_bound )
        reader.bad = 1;
    for( uint32_t i = 0; i < count && !reader.bad; i++ )
    {
        struct EditorHost_CatalogRecordField* field = &out_record->fields[i];
        uint32_t source;
        uint32_t old_source;
        field->key = arena_string(&reader, &arena);
        field->value = arena_string(&reader, &arena);
        source = ToriRSMapEd_GetU32(&reader);
        field->overridden_value = arena_string(&reader, &arena);
        old_source = ToriRSMapEd_GetU32(&reader);
        field->source = source < (uint32_t)out_record->source_count ? (int)source : -1;
        field->overridden_source =
            old_source < (uint32_t)out_record->source_count ? (int)old_source : -1;
        if( field->overridden_source < 0 && !field->overridden_value[0] )
            field->overridden_value = NULL;
    }
    out_record->field_count = reader.bad ? 0 : (int)count;
    frame_done(remote, length);
    if( !ToriRSMapEd_ReaderDone(&reader) )
    {
        Editor_HostCatalogRecordFree(out_record);
        return EDITOR_HOST_IO_ERROR;
    }
    return status;
}

void
Editor_HostCatalogRecordFree(struct EditorHost_CatalogRecord* record)
{
    if( !record )
        return;
    ToriMem_Free(ToriMem_Editor(), record->storage);
    memset(record, 0, sizeof(*record));
}

/** FACT_CATALOG_SQUARES / _CHANGED into `out`. Returns the fact's status,
 *  or EDITOR_HOST_IO_ERROR (and `out` empty) when it is malformed. */
static enum EditorHost_Status
decode_catalog_squares(
    const uint8_t* body,
    uint32_t length,
    struct EditorHost_CatalogSquares* out)
{
    struct ToriRSMapEdReader reader;
    struct catalog_arena arena;
    enum EditorHost_Status status;
    uint32_t count;
    char* mem;
    const char** storages;
    struct EditorHost_CatalogArea* areas;
    struct EditorHost_CatalogSquare* squares;
    int* area_ids;
    int area_at = 0;
    /* a storage is >= 4 bytes, an area >= 12, a square >= 32, an area id 4 */
    size_t storage_bound = length / 4;
    size_t area_bound = length / 12;
    size_t square_bound = length / 32;
    size_t id_bound = length / 4;

    memset(out, 0, sizeof(*out));
    mem = ToriMem_Alloc(ToriMem_Editor(), storage_bound * sizeof(*storages)
                                              + area_bound * sizeof(*areas)
                                              + square_bound * sizeof(*squares)
                                              + id_bound * sizeof(*area_ids) + length + 1);
    assert(mem);
    storages = (const char**)mem;
    areas = (struct EditorHost_CatalogArea*)(storages + storage_bound);
    squares = (struct EditorHost_CatalogSquare*)(areas + area_bound);
    area_ids = (int*)(squares + square_bound);
    arena.at = (char*)(area_ids + id_bound);
    arena.end = arena.at + length + 1;

    ToriRSMapEd_ReaderInit(&reader, body, length);
    status = (enum EditorHost_Status)ToriRSMapEd_GetU32(&reader);
    count = ToriRSMapEd_GetU32(&reader);
    if( count > storage_bound )
        reader.bad = 1;
    for( uint32_t i = 0; i < count && !reader.bad; i++ )
        storages[i] = arena_string(&reader, &arena);
    out->storage_count = (int)count;
    count = ToriRSMapEd_GetU32(&reader);
    if( count > area_bound )
        reader.bad = 1;
    for( uint32_t i = 0; i < count && !reader.bad; i++ )
    {
        areas[i].internal_name = arena_string(&reader, &arena);
        areas[i].display_name = arena_string(&reader, &arena);
        areas[i].is_main = (int)ToriRSMapEd_GetU32(&reader);
    }
    out->area_count = (int)count;
    count = ToriRSMapEd_GetU32(&reader);
    if( count > square_bound )
        reader.bad = 1;
    for( uint32_t i = 0; i < count && !reader.bad; i++ )
    {
        struct EditorHost_CatalogSquare* sq = &squares[i];
        uint32_t storage;
        uint32_t primary;
        uint32_t area_n;
        sq->map_x = (int32_t)ToriRSMapEd_GetU32(&reader);
        sq->map_z = (int32_t)ToriRSMapEd_GetU32(&reader);
        storage = ToriRSMapEd_GetU32(&reader);
        sq->flags = ToriRSMapEd_GetU32(&reader);
        sq->spawn_count = (int)ToriRSMapEd_GetU32(&reader);
        sq->rgb = ToriRSMapEd_GetU32(&reader);
        primary = ToriRSMapEd_GetU32(&reader);
        area_n = ToriRSMapEd_GetU32(&reader);
        if( storage >= (uint32_t)out->storage_count || area_n > id_bound - (size_t)area_at
            || (primary != TORIRSMAPED_CATALOG_NO_AREA && primary >= (uint32_t)out->area_count) )
        {
            reader.bad = 1;
            break;
        }
        sq->storage = (int)storage;
        sq->primary_area = primary == TORIRSMAPED_CATALOG_NO_AREA ? -1 : (int)primary;
        sq->areas = area_ids + area_at;
        sq->area_count = (int)area_n;
        for( uint32_t a = 0; a < area_n; a++ )
        {
            uint32_t area = ToriRSMapEd_GetU32(&reader);
            if( area >= (uint32_t)out->area_count )
                reader.bad = 1;
            area_ids[area_at++] = (int)area;
        }
    }
    out->square_count = (int)count;
    out->storages = storages;
    out->areas = areas;
    out->squares = squares;
    out->storage_mem = mem;
    if( !ToriRSMapEd_ReaderDone(&reader) )
    {
        Editor_HostCatalogSquaresFree(out);
        return EDITOR_HOST_IO_ERROR;
    }
    return status;
}

enum EditorHost_Status
Editor_HostMapEdCatalogSquares(
    struct EditorHost* host,
    struct EditorHost_CatalogSquares* out_atlas)
{
    struct maped_remote* remote = as_remote(host);
    struct ToriRSMapEdBuf payload;
    const uint8_t* fact;
    uint32_t length;
    enum EditorHost_Status status;

    assert(out_atlas);
    memset(out_atlas, 0, sizeof(*out_atlas));
    if( !(remote->capabilities & TORIRSMAPED_CAP_CATALOG) )
        return EDITOR_HOST_UNSUPPORTED;

    memset(&payload, 0, sizeof(payload));
    fact = catalog_rpc(remote, TORIRSMAPED_REQ_CATALOG_SQUARES, &payload,
        TORIRSMAPED_FACT_CATALOG_SQUARES, &length);
    if( !fact )
        return EDITOR_HOST_IO_ERROR;
    status = decode_catalog_squares(fact, length, out_atlas);
    frame_done(remote, length);
    return status;
}

int
Editor_HostMapEdCatalogSquaresChanged(
    struct EditorHost* host,
    struct EditorHost_CatalogSquares* out_atlas)
{
    struct maped_remote* remote = as_remote(host);
    struct stashed_fact* fact;
    enum EditorHost_Status status;

    assert(out_atlas);
    memset(out_atlas, 0, sizeof(*out_atlas));
    fact = remote->atlas_changed;
    if( !fact )
        return 0;
    remote->atlas_changed = NULL;
    status = decode_catalog_squares(fact->data, fact->length, out_atlas);
    ToriMem_Free(ToriMem_Editor(), fact);
    if( status == EDITOR_HOST_IO_ERROR )
    {
        TORIRS_LOG("editor: dropping a malformed FACT_CATALOG_SQUARES_CHANGED\n");
        return 0;
    }
    return 1;
}

void
Editor_HostCatalogSquaresFree(struct EditorHost_CatalogSquares* atlas)
{
    if( !atlas )
        return;
    ToriMem_Free(ToriMem_Editor(), atlas->storage_mem);
    memset(atlas, 0, sizeof(*atlas));
}

static void
deliver_fact(
    const struct EditorHostMapEdFacts* sink,
    uint32_t type,
    const uint8_t* body,
    uint32_t length)
{
    switch( type )
    {
    case TORIRSMAPED_FACT_CMD:
    {
        struct Editor_Cmd command;
        enum ToriRSMapEdCmdDecodeResult decoded;

        if( length < 8 )
        {
            TORIRS_LOG("editor: dropping a malformed FACT_CMD\n");
            return;
        }
        decoded = ToriRSMapEd_CmdDecode(body + 8, length - 8, &command);
        if( decoded == TORIRSMAPED_CMD_DECODE_UNKNOWN_KIND )
        {
            /* A newer peer's edit: apply nothing, re-open to resync. */
            if( sink->on_resync )
                sink->on_resync(sink->user_data, command.doc_kind, command.doc_key);
            return;
        }
        if( decoded != TORIRSMAPED_CMD_DECODE_OK )
        {
            TORIRS_LOG("editor: dropping a malformed FACT_CMD\n");
            return;
        }
        if( sink->on_cmd )
            sink->on_cmd(
                sink->user_data,
                ToriRSMapEd_ReadU32(body),
                (int)ToriRSMapEd_ReadU32(body + 4),
                &command);
        return;
    }
    case TORIRSMAPED_FACT_SAVED:
    {
        struct ToriRSMapEdReader reader;
        struct Editor_DocRef* rows;
        uint32_t count;

        ToriRSMapEd_ReaderInit(&reader, body, length);
        count = ToriRSMapEd_GetU32(&reader);
        /* Each row is at least 8 bytes, which bounds a hostile count. */
        if( reader.bad || count > length / 8 )
            return;
        rows = ToriMem_Alloc(ToriMem_Editor(), ((size_t)count + 1) * sizeof(*rows));
        assert(rows);
        for( uint32_t i = 0; i < count; i++ )
        {
            uint32_t kind = ToriRSMapEd_GetU32(&reader);
            rows[i].kind = (enum Editor_DocKind)kind;
            ToriRSMapEd_GetString(&reader, rows[i].key, sizeof(rows[i].key));
            if( kind >= EDITOR_DOC_KIND_COUNT )
                reader.bad = 1;
        }
        if( ToriRSMapEd_ReaderDone(&reader) && sink->on_saved )
            sink->on_saved(sink->user_data, (int)count, rows);
        ToriMem_Free(ToriMem_Editor(), rows);
        return;
    }
    case TORIRSMAPED_FACT_STATE:
    {
        uint32_t count;
        int32_t values[TORIRSMAPED_STATE_VALUES_MAX];
        if( length < 12 )
            return;
        count = ToriRSMapEd_ReadU32(body + 8);
        if( count > TORIRSMAPED_STATE_VALUES_MAX || length != 12 + count * 4 )
            return;
        for( uint32_t i = 0; i < count; i++ )
            values[i] = (int32_t)ToriRSMapEd_ReadU32(body + 12 + i * 4);
        if( sink->on_state )
            sink->on_state(
                sink->user_data, ToriRSMapEd_ReadU32(body + 4), values, (int)count);
        return;
    }
    default:
        return;
    }
}

int
Editor_HostMapEdDrainFacts(
    struct EditorHost* host,
    const struct EditorHostMapEdFacts* sink)
{
    struct maped_remote* remote = as_remote(host);
    int delivered = 0;

    assert(sink);

    /* Stashed first — they arrived first. */
    while( remote->stash_head )
    {
        struct stashed_fact* fact = remote->stash_head;
        remote->stash_head = fact->next;
        if( !remote->stash_head )
            remote->stash_tail = NULL;
        if( is_live_fact(fact->type) )
        {
            /* Kept for the Live panel's poll (Editor_HostMapEdLiveTake). */
            live_keep(remote, fact->type, fact->data, fact->length);
            ToriMem_Free(ToriMem_Editor(), fact);
            delivered++;
            continue;
        }
        if( fact->type == TORIRSMAPED_FACT_CATALOG_SQUARES_CHANGED )
        {
            /* Kept for the picker's poll rather than handed to the sink. */
            fact->next = NULL;
            ToriMem_Free(ToriMem_Editor(), remote->atlas_changed);
            remote->atlas_changed = fact;
            delivered++;
            continue;
        }
        deliver_fact(sink, fact->type, fact->data, fact->length);
        ToriMem_Free(ToriMem_Editor(), fact);
        delivered++;
    }

    /* Then whatever the wire has right now — a poll, never a wait. */
    for( ;; )
    {
        uint32_t type;
        uint32_t length;
        const uint8_t* body;

        if( ToriRSMapEd_BufAvailable(&remote->in) < TORIRSMAPED_FRAME_HEADER )
        {
            if( link_fill(remote, 0) <= 0 )
                break;
            continue;
        }
        body = take_frame(remote, &type, &length);
        if( !body )
        {
            /* A header whose body has not all arrived: read what the wire
             * has, or stop until the next poll. Breaking without the read
             * would stall every later drain on the same partial frame (a
             * build streaming FACT_BUILD_LINEs found it). */
            if( link_fill(remote, 0) <= 0 )
                break;
            continue;
        }
        if( type == TORIRSMAPED_FACT_CATALOG_SQUARES_CHANGED )
        {
            struct stashed_fact* fact =
                ToriMem_Alloc(ToriMem_Editor(), sizeof(*fact) + length);
            assert(fact);
            fact->next = NULL;
            fact->type = type;
            fact->length = length;
            memcpy(fact->data, body, length);
            ToriMem_Free(ToriMem_Editor(), remote->atlas_changed);
            remote->atlas_changed = fact;
            delivered++;
        }
        else if( is_live_fact(type) )
        {
            live_keep(remote, type, body, length);
            delivered++;
        }
        else if( is_broadcast(type) )
        {
            deliver_fact(sink, type, body, length);
            delivered++;
        }
        else
            TORIRS_LOG("editor: dropping unsolicited maped frame %u outside a request\n",
                type);
        frame_done(remote, length);
    }
    return delivered;
}
