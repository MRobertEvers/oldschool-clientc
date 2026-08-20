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

#include "torirsmaped/torirs_maped.h"

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
};

static int
is_broadcast(uint32_t type)
{
    return type == TORIRSMAPED_FACT_CMD || type == TORIRSMAPED_FACT_STATE
           || type == TORIRSMAPED_FACT_SAVED;
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
        fprintf(stderr, "editor: maped answered a corrupt frame\n");
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
    struct stashed_fact* fact = malloc(sizeof(*fact) + length);

    assert(fact);
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
            fprintf(
                stderr,
                "editor: maped answered frame %u where %u was expected\n",
                *out_type,
                want_a);
            frame_done(remote, *out_length);
            return NULL;
        }

        int got = link_fill(remote, 500);
        if( got < 0 )
        {
            fprintf(stderr, "editor: maped server is gone\n");
            return NULL;
        }
        if( got == 0 )
        {
            waited_ms += 500;
            if( waited_ms >= timeout_ms )
            {
                fprintf(stderr, "editor: maped did not answer within %d ms\n", timeout_ms);
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
        out_jm2->data = malloc(jm2_size + 1);
        assert(out_jm2->data);
        memcpy(out_jm2->data, fact + at + 8, jm2_size);
        out_jm2->data[jm2_size] = '\0';
        out_jm2->size = jm2_size;
    }
    if( jl2_plus > 0 )
    {
        out_jl2->data = malloc(jl2_size + 1);
        assert(out_jl2->data);
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

    payload = malloc(length);
    assert(payload);
    ToriRSMapEd_WriteU32(payload, (uint32_t)map_x);
    ToriRSMapEd_WriteU32(payload + 4, (uint32_t)map_z);
    ToriRSMapEd_WriteU32(payload + 8, jm2_text ? (uint32_t)jm2_size + 1 : 0);
    ToriRSMapEd_WriteU32(payload + 12, jl2_text ? (uint32_t)jl2_size + 1 : 0);
    if( jm2_size > 0 )
        memcpy(payload + 16, jm2_text, jm2_size);
    if( jl2_size > 0 )
        memcpy(payload + 16 + jm2_size, jl2_text, jl2_size);

    status = rpc_status(remote, TORIRSMAPED_REQ_SQUARE_SAVE, payload, length, NULL);
    free(payload);
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

    payload = malloc(payload_length);
    assert(payload);
    ToriRSMapEd_WriteU32(payload, (uint32_t)map_x);
    ToriRSMapEd_WriteU32(payload + 4, (uint32_t)map_z);
    ToriRSMapEd_WriteU32(payload + 8, text ? (uint32_t)size + 1 : 0);
    if( size > 0 )
        memcpy(payload + 12, text, size);

    status = rpc_status(remote, TORIRSMAPED_REQ_SPAWN_SAVE, payload, payload_length, NULL);
    free(payload);
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
                char* line = malloc(fact_length + 1);
                assert(line);
                memcpy(line, fact, fact_length);
                line[fact_length] = '\0';
                on_progress(progress_user_data, line);
                free(line);
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
        free(remote->stash_head);
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
    ToriRSMapEd_BufFree(&remote->in);
    free(remote);
}

static const struct EditorHost_VTable remote_vtable = {
    remote_square_list, remote_square_load, remote_square_save, remote_spawn_save,
    remote_bake,        remote_session,     remote_free,
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

    fact = await_reply(
        remote, REMOTE_OP_TIMEOUT_MS, TORIRSMAPED_FACT_HELLO, 0, &fact_type, &fact_length);
    if( !fact || fact_length != 12 )
    {
        if( fact )
            frame_done(remote, fact_length);
        return 0;
    }
    version = ToriRSMapEd_ReadU32(fact);
    remote->writable = (int)ToriRSMapEd_ReadU32(fact + 4);
    remote->client_id = ToriRSMapEd_ReadU32(fact + 8);
    frame_done(remote, fact_length);
    if( version != TORIRSMAPED_PROTO_VERSION )
    {
        fprintf(
            stderr,
            "editor: maped speaks protocol %u, this client %d\n",
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

    remote = malloc(sizeof(*remote));
    assert(remote);
    memset(remote, 0, sizeof(*remote));
    remote->fd = -1;

    remote->embed = ToriRSMapEd_EmbedStart(content_dir, repo_root);
    if( !remote->embed )
    {
        free(remote);
        return 0;
    }
    remote->embed_client = 0;

    if( !hello(remote, TORIRSMAPED_ROLE_GENERIC, 0) )
    {
        ToriRSMapEd_EmbedStop(remote->embed);
        ToriRSMapEd_BufFree(&remote->in);
        free(remote);
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
        fprintf(stderr, "editor: the embedded maped has no free connection\n");
        return 0;
    }

    remote = malloc(sizeof(*remote));
    assert(remote);
    memset(remote, 0, sizeof(*remote));
    remote->fd = -1;
    remote->embed = peer_remote->embed;
    remote->embed_client = client_id;
    remote->borrowed_embed = 1;

    if( !hello(remote, (uint32_t)role, join_group ? peer_remote->client_id : 0) )
    {
        ToriRSMapEd_EmbedDisconnect(remote->embed, client_id);
        ToriRSMapEd_BufFree(&remote->in);
        free(remote);
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
        fprintf(stderr, "editor: cannot resolve maped host %s\n", server_host);
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
        fprintf(
            stderr, "editor: cannot connect to maped at %s:%d\n", server_host, port);
        return 0;
    }

    remote = malloc(sizeof(*remote));
    assert(remote);
    memset(remote, 0, sizeof(*remote));
    remote->fd = fd;

    if( !hello(remote, (uint32_t)role, join_group) )
    {
        close(fd);
        ToriRSMapEd_BufFree(&remote->in);
        free(remote);
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
    fprintf(stderr, "editor: server=tcp is not built for this platform yet\n");
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
    uint8_t payload[TORIRSMAPED_CMD_WIRE];

    assert(command);

    ToriRSMapEd_CmdEncode(command, payload);
    return rpc_status(remote, TORIRSMAPED_REQ_CMD, payload, sizeof(payload), NULL);
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
        if( length != 8 + TORIRSMAPED_CMD_WIRE
            || !ToriRSMapEd_CmdDecode(body + 8, TORIRSMAPED_CMD_WIRE, &command) )
        {
            fprintf(stderr, "editor: dropping a malformed FACT_CMD\n");
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
        uint32_t count;
        int coords[EDITOR_DOC_MAX_SQUARES * 2];
        if( length < 4 )
            return;
        count = ToriRSMapEd_ReadU32(body);
        if( length != 4 + count * 8 || count > EDITOR_DOC_MAX_SQUARES )
            return;
        for( uint32_t i = 0; i < count; i++ )
        {
            coords[i * 2] = (int)ToriRSMapEd_ReadU32(body + 4 + i * 8);
            coords[i * 2 + 1] = (int)ToriRSMapEd_ReadU32(body + 8 + i * 8);
        }
        if( sink->on_saved )
            sink->on_saved(sink->user_data, (int)count, coords);
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
        deliver_fact(sink, fact->type, fact->data, fact->length);
        free(fact);
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
            break;
        if( is_broadcast(type) )
        {
            deliver_fact(sink, type, body, length);
            delivered++;
        }
        else
            fprintf(
                stderr,
                "editor: dropping unsolicited maped frame %u outside a request\n",
                type);
        frame_done(remote, length);
    }
    return delivered;
}
