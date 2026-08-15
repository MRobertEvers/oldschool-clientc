#include "js5_server_session.h"
#include <assert.h>

#include "js5_server_cache.h"

#include <stdlib.h>
#include <string.h>

#define JS5_HANDSHAKE_BYTES 21u
#define JS5_REQUEST_BYTES 4u
#define JS5_WIRE_BLOCK_BYTES 512u
#define JS5_OUTPUT_SCRATCH_BYTES (16u * 1024u)

#define JS5_OPCODE_HANDSHAKE 15u
#define JS5_OPCODE_NORMAL 0u
#define JS5_OPCODE_URGENT 1u
#define JS5_OPCODE_LOGGED_IN 2u
#define JS5_OPCODE_LOGGED_OUT 3u
#define JS5_OPCODE_XOR 4u

#define JS5_STATUS_OK 0u
#define JS5_STATUS_OUT_OF_DATE 6u
#define JS5_STATUS_SERVER_FULL 7u

struct Js5ServerRequest
{
    uint16_t group;
    uint8_t archive;
    uint8_t xor_key;
};

struct Js5ServerQueue
{
    struct Js5ServerRequest* requests;
    uint32_t capacity;
    uint32_t head;
    uint32_t count;
};

struct Js5ServerSession
{
    struct Js5ServerCache* cache;
    struct Js5ServerSessionConfig config;
    enum Js5ServerSessionState state;
    enum Js5ServerSessionError error;

    uint8_t input[JS5_HANDSHAKE_BYTES];
    size_t input_size;
    size_t input_expected;

    struct Js5ServerQueue urgent;
    struct Js5ServerQueue normal;
    struct Js5ServerRequest current;
    struct Js5ServerBlob current_blob;
    bool current_active;
    size_t current_blob_offset;
    size_t current_wire_position;

    uint8_t scratch[JS5_OUTPUT_SCRATCH_BYTES];
    size_t scratch_size;
    size_t scratch_position;
    bool status_pending;
    uint8_t status;

    uint64_t created_ms;
    uint64_t current_time_ms;
    uint64_t last_input_ms;
    uint64_t last_output_progress_ms;
    uint32_t client_revision;
    uint64_t requests_received;
    uint64_t responses_served;
    uint64_t bytes_sent;
    uint64_t cache_misses;
    uint8_t xor_key;
    bool logged_in;
};

static uint32_t
js5_read_u32(const uint8_t* data)
{
    return ((uint32_t)data[0] << 24u) | ((uint32_t)data[1] << 16u) |
           ((uint32_t)data[2] << 8u) | (uint32_t)data[3];
}

static void
js5_session_fail(
    struct Js5ServerSession* session,
    enum Js5ServerSessionError error)
{
    if( session->state == JS5_SERVER_SESSION_CLOSED ||
        session->state == JS5_SERVER_SESSION_FAILED )
        return;
    session->error = error;
    session->state = JS5_SERVER_SESSION_FAILED;
}

static bool
js5_queue_push(
    struct Js5ServerQueue* queue,
    struct Js5ServerRequest request)
{
    if( queue->count >= queue->capacity )
        return false;
    uint32_t tail = (queue->head + queue->count) % queue->capacity;
    queue->requests[tail] = request;
    queue->count++;
    return true;
}

static bool
js5_queue_pop(
    struct Js5ServerQueue* queue,
    struct Js5ServerRequest* request)
{
    if( queue->count == 0u )
        return false;
    *request = queue->requests[queue->head];
    queue->head = (queue->head + 1u) % queue->capacity;
    queue->count--;
    return true;
}

void
Js5ServerSessionConfigInit(struct Js5ServerSessionConfig* config)
{
    assert(config);
    memset(config, 0, sizeof(*config));
    config->revision = 239u;
    config->max_pending_per_lane = 200u;
    config->handshake_timeout_ms = 10000u;
    config->idle_timeout_ms = 300000u;
    config->output_timeout_ms = 30000u;
}

struct Js5ServerSession*
Js5ServerSessionNew(
    struct Js5ServerCache* cache,
    const struct Js5ServerSessionConfig* config,
    uint64_t now_ms)
{
    struct Js5ServerSessionConfig effective;
    struct Js5ServerSession* session;

    Js5ServerSessionConfigInit(&effective);
    if( config )
        effective = *config;
    if( effective.revision == 0u || effective.max_pending_per_lane == 0u ||
        (!cache && !effective.server_full) )
        return NULL;

    session = (struct Js5ServerSession*)calloc(1u, sizeof(*session));
    assert(session);
    session->urgent.requests = (struct Js5ServerRequest*)calloc(
        effective.max_pending_per_lane, sizeof(*session->urgent.requests));
    session->normal.requests = (struct Js5ServerRequest*)calloc(
        effective.max_pending_per_lane, sizeof(*session->normal.requests));
    if( !session->urgent.requests || !session->normal.requests )
    {
        Js5ServerSessionFree(session);
        return NULL;
    }
    session->urgent.capacity = effective.max_pending_per_lane;
    session->normal.capacity = effective.max_pending_per_lane;
    session->cache = cache;
    session->config = effective;
    session->state = JS5_SERVER_SESSION_HANDSHAKE;
    session->input_expected = JS5_HANDSHAKE_BYTES;
    session->created_ms = now_ms;
    session->current_time_ms = now_ms;
    session->last_input_ms = now_ms;
    session->last_output_progress_ms = now_ms;
    return session;
}

void
Js5ServerSessionFree(struct Js5ServerSession* session)
{
    if( !session )
        return;
    Js5ServerBlobFree(&session->current_blob);
    free(session->urgent.requests);
    free(session->normal.requests);
    memset(session, 0, sizeof(*session));
    free(session);
}

static bool
js5_session_handshake(struct Js5ServerSession* session)
{
    uint8_t status;

    if( session->input[0] != JS5_OPCODE_HANDSHAKE )
    {
        js5_session_fail(session, JS5_SERVER_SESSION_ERROR_PROTOCOL);
        return false;
    }
    session->client_revision = js5_read_u32(session->input + 1u);
    if( session->config.server_full )
        status = JS5_STATUS_SERVER_FULL;
    else if( session->client_revision != session->config.revision )
        status = JS5_STATUS_OUT_OF_DATE;
    else
        status = JS5_STATUS_OK;

    /* Bytes 5..20 are login/XTEA seeds. They are deliberately never retained. */
    memset(session->input, 0, sizeof(session->input));
    session->input_size = 0u;
    session->input_expected = JS5_REQUEST_BYTES;
    session->status = status;
    session->status_pending = true;
    if( status == JS5_STATUS_OK )
        session->state = JS5_SERVER_SESSION_ACTIVE;
    else
        session->state = JS5_SERVER_SESSION_CLOSING;
    return true;
}

static bool
js5_session_request(struct Js5ServerSession* session)
{
    uint8_t opcode = session->input[0];
    struct Js5ServerRequest request = {
        .group = (uint16_t)(((uint16_t)session->input[2] << 8u) | session->input[3]),
        .archive = session->input[1],
        .xor_key = session->xor_key,
    };

    if( opcode == JS5_OPCODE_LOGGED_IN || opcode == JS5_OPCODE_LOGGED_OUT )
    {
        if( session->input[1] != 0u || session->input[2] != 0u || session->input[3] != 0u )
            goto protocol_error;
        session->logged_in = opcode == JS5_OPCODE_LOGGED_IN;
    }
    else if( opcode == JS5_OPCODE_XOR )
    {
        if( session->input[2] != 0u || session->input[3] != 0u )
            goto protocol_error;
        session->xor_key = session->input[1];
    }
    else if( opcode == JS5_OPCODE_NORMAL || opcode == JS5_OPCODE_URGENT )
    {
        struct Js5ServerQueue* queue =
            opcode == JS5_OPCODE_URGENT ? &session->urgent : &session->normal;
        if( !js5_queue_push(queue, request) )
        {
            js5_session_fail(session, JS5_SERVER_SESSION_ERROR_QUEUE_FULL);
            return false;
        }
        session->requests_received++;
    }
    else
    {
        goto protocol_error;
    }
    memset(session->input, 0, JS5_REQUEST_BYTES);
    session->input_size = 0u;
    return true;

protocol_error:
    js5_session_fail(session, JS5_SERVER_SESSION_ERROR_PROTOCOL);
    return false;
}

int
Js5ServerSessionFeed(
    struct Js5ServerSession* session,
    const uint8_t* data,
    size_t size,
    uint64_t now_ms)
{
    if( !session || (!data && size > 0u) )
        return -1;
    session->current_time_ms = now_ms;
    if( session->state != JS5_SERVER_SESSION_HANDSHAKE &&
        session->state != JS5_SERVER_SESSION_ACTIVE )
        return size == 0u ? 0 : -1;

    for( size_t offset = 0u; offset < size; offset++ )
    {
        session->input[session->input_size++] = data[offset];
        if( session->state == JS5_SERVER_SESSION_HANDSHAKE && session->input_size == 1u &&
            session->input[0] != JS5_OPCODE_HANDSHAKE )
        {
            js5_session_fail(session, JS5_SERVER_SESSION_ERROR_PROTOCOL);
            return -1;
        }
        if( session->input_size != session->input_expected )
            continue;
        if( session->state == JS5_SERVER_SESSION_HANDSHAKE )
        {
            if( !js5_session_handshake(session) )
                return -1;
            if( session->state != JS5_SERVER_SESSION_ACTIVE && offset + 1u < size )
                return -1;
        }
        else if( !js5_session_request(session) )
        {
            return -1;
        }
    }
    session->last_input_ms = now_ms;
    return 0;
}

static int
js5_session_begin_response(struct Js5ServerSession* session)
{
    enum Js5ServerCacheResult result;

    if( !js5_queue_pop(&session->urgent, &session->current) &&
        !js5_queue_pop(&session->normal, &session->current) )
        return 0;
    result = Js5ServerCacheLoad(
        session->cache,
        session->current.archive,
        session->current.group,
        &session->current_blob);
    if( result != JS5_SERVER_CACHE_OK )
    {
        session->cache_misses++;
        js5_session_fail(
            session,
            result == JS5_SERVER_CACHE_NOT_FOUND ? JS5_SERVER_SESSION_ERROR_CACHE_MISS
                                                  : JS5_SERVER_SESSION_ERROR_CACHE);
        return -1;
    }
    session->current_active = true;
    session->current_blob_offset = 0u;
    session->current_wire_position = 0u;
    session->last_output_progress_ms = session->current_time_ms;
    return 1;
}

static void
js5_session_finish_response(struct Js5ServerSession* session)
{
    Js5ServerBlobFree(&session->current_blob);
    memset(&session->current, 0, sizeof(session->current));
    session->current_active = false;
    session->current_blob_offset = 0u;
    session->current_wire_position = 0u;
    session->responses_served++;
}

static bool
js5_session_response_complete(const struct Js5ServerSession* session)
{
    return session->current_active && session->current_wire_position >= 3u &&
           session->current_blob_offset == session->current_blob.size;
}

static void
js5_session_fill_response(struct Js5ServerSession* session)
{
    session->scratch_position = 0u;
    session->scratch_size = 0u;
    while( session->scratch_size < sizeof(session->scratch) &&
           !js5_session_response_complete(session) )
    {
        uint8_t value;
        if( session->current_wire_position == 0u )
            value = session->current.archive;
        else if( session->current_wire_position == 1u )
            value = (uint8_t)(session->current.group >> 8u);
        else if( session->current_wire_position == 2u )
            value = (uint8_t)session->current.group;
        else if( session->current_wire_position % JS5_WIRE_BLOCK_BYTES == 0u )
            value = 0xffu;
        else
            value = session->current_blob.data[session->current_blob_offset++];
        session->scratch[session->scratch_size++] = value ^ session->current.xor_key;
        session->current_wire_position++;
    }
}

int
Js5ServerSessionPeekOutput(
    struct Js5ServerSession* session,
    const uint8_t** data,
    size_t* size)
{
    assert(session);
    assert(data);
    assert(size);
    *data = NULL;
    *size = 0u;
    if( session->state == JS5_SERVER_SESSION_FAILED ||
        session->state == JS5_SERVER_SESSION_CLOSED )
        return -1;
    if( session->scratch_position < session->scratch_size )
    {
        *data = session->scratch + session->scratch_position;
        *size = session->scratch_size - session->scratch_position;
        return 1;
    }

    session->scratch_position = 0u;
    session->scratch_size = 0u;
    if( session->status_pending )
    {
        session->scratch[0] = session->status;
        session->scratch_size = 1u;
        session->status_pending = false;
    }
    else
    {
        if( js5_session_response_complete(session) )
            js5_session_finish_response(session);
        if( !session->current_active )
        {
            int begun = js5_session_begin_response(session);
            if( begun < 0 )
                return -1;
            if( begun == 0 )
            {
                if( session->state == JS5_SERVER_SESSION_CLOSING )
                    session->state = JS5_SERVER_SESSION_CLOSED;
                return 0;
            }
        }
        js5_session_fill_response(session);
    }

    *data = session->scratch;
    *size = session->scratch_size;
    return session->scratch_size > 0u ? 1 : 0;
}

int
Js5ServerSessionConsumeOutput(
    struct Js5ServerSession* session,
    size_t size,
    uint64_t now_ms)
{
    size_t available;

    assert(session);
    if( session->scratch_position > session->scratch_size )
        return -1;
    available = session->scratch_size - session->scratch_position;
    if( size > available )
    {
        js5_session_fail(session, JS5_SERVER_SESSION_ERROR_PROTOCOL);
        return -1;
    }
    session->scratch_position += size;
    session->current_time_ms = now_ms;
    session->bytes_sent += size;
    if( size > 0u )
        session->last_output_progress_ms = now_ms;
    if( session->scratch_position == session->scratch_size &&
        session->state == JS5_SERVER_SESSION_CLOSING && !session->status_pending &&
        !session->current_active )
        session->state = JS5_SERVER_SESSION_CLOSED;
    return 0;
}

void
Js5ServerSessionTick(
    struct Js5ServerSession* session,
    uint64_t now_ms)
{
    if( !session || session->state == JS5_SERVER_SESSION_CLOSED ||
        session->state == JS5_SERVER_SESSION_FAILED )
        return;
    session->current_time_ms = now_ms;
    if( session->state == JS5_SERVER_SESSION_HANDSHAKE &&
        session->config.handshake_timeout_ms > 0u &&
        now_ms - session->created_ms > session->config.handshake_timeout_ms )
    {
        js5_session_fail(session, JS5_SERVER_SESSION_ERROR_TIMEOUT);
        return;
    }
    if( session->state == JS5_SERVER_SESSION_ACTIVE && !session->current_active &&
        session->urgent.count == 0u && session->normal.count == 0u &&
        session->scratch_position == session->scratch_size &&
        session->config.idle_timeout_ms > 0u &&
        now_ms - session->last_input_ms > session->config.idle_timeout_ms )
    {
        js5_session_fail(session, JS5_SERVER_SESSION_ERROR_TIMEOUT);
        return;
    }
    if( session->scratch_position < session->scratch_size &&
        session->config.output_timeout_ms > 0u &&
        now_ms - session->last_output_progress_ms > session->config.output_timeout_ms )
        js5_session_fail(session, JS5_SERVER_SESSION_ERROR_TIMEOUT);
}

bool
Js5ServerSessionWantsClose(const struct Js5ServerSession* session)
{
    return !session || session->state == JS5_SERVER_SESSION_CLOSED ||
           session->state == JS5_SERVER_SESSION_FAILED;
}

void
Js5ServerSessionGetStats(
    const struct Js5ServerSession* session,
    struct Js5ServerSessionStats* stats)
{
    assert(stats);
    memset(stats, 0, sizeof(*stats));
    if( !session )
        return;
    stats->state = session->state;
    stats->error = session->error;
    stats->client_revision = session->client_revision;
    stats->queued_urgent = session->urgent.count;
    stats->queued_normal = session->normal.count;
    stats->requests_received = session->requests_received;
    stats->responses_served = session->responses_served;
    stats->bytes_sent = session->bytes_sent;
    stats->cache_misses = session->cache_misses;
    stats->xor_key = session->xor_key;
    stats->logged_in = session->logged_in;
}
