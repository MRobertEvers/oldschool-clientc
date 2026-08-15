#include "js5.h"
#include <assert.h>

#include "platform/sockstream.h"

#include <archive.h>
#include <checksum.h>
#include <dat2disk.h>
#include <reference_table.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define JS5_REQUEST_BUCKETS 4096u
#define JS5_HANDSHAKE_BYTES 21u
#define JS5_PACKET_BYTES 4u
#define JS5_RESPONSE_HEADER_BYTES 8u
#define JS5_NETWORK_BLOCK_BYTES 512u
#define JS5_READ_BUFFER_BYTES 4096u
#define JS5_READ_ITERATIONS 100u

enum Js5RequestKind
{
    JS5_REQUEST_KIND_MASTER = 0,
    JS5_REQUEST_KIND_REFERENCE,
    JS5_REQUEST_KIND_GROUP,
};

enum Js5RequestState
{
    JS5_REQUEST_WAIT_METADATA = 0,
    JS5_REQUEST_QUEUED_NORMAL,
    JS5_REQUEST_QUEUED_URGENT,
    JS5_REQUEST_TX_NORMAL,
    JS5_REQUEST_TX_URGENT,
    JS5_REQUEST_INFLIGHT_NORMAL,
    JS5_REQUEST_INFLIGHT_URGENT,
};

enum Js5GroupState
{
    JS5_GROUP_UNKNOWN = 0,
    JS5_GROUP_MISSING,
    JS5_GROUP_PENDING,
    JS5_GROUP_READY,
};

enum Js5TxKind
{
    JS5_TX_NONE = 0,
    JS5_TX_HANDSHAKE,
    JS5_TX_CONTROL,
    JS5_TX_REQUEST,
};

struct Js5Request
{
    uint32_t key;
    uint32_t expected_crc;
    uint32_t expected_version;
    uint16_t archive;
    uint16_t group;
    enum Js5RequestKind kind;
    enum Js5RequestState state;
    enum Js5Priority priority;
    bool demanded;

    struct Js5Request* hash_next;
    struct Js5Request* queue_previous;
    struct Js5Request* queue_next;
};

struct Js5RequestQueue
{
    struct Js5Request* first;
    struct Js5Request* last;
    uint32_t count;
};

struct Js5Archive
{
    bool registered;
    bool background;
    bool optional;
    bool master_present;
    bool reference_ready;
    bool empty;
    bool scanning;
    bool ready;

    uint32_t reference_crc;
    uint32_t reference_version;
    struct RSCache_ReferenceTable* reference;
    uint8_t* group_states;
    int scan_position;
    uint32_t valid_groups;
    uint32_t total_groups;
};

struct Js5CompletionNode
{
    struct Js5Completion value;
    struct Js5CompletionNode* next;
};

struct Js5Client
{
    struct Js5Config config;
    char* host;

    struct Js5TransportOps transport;
    void* transport_user;
    void* stream;
    bool default_transport;

    struct Js5StorageOps storage;
    void* storage_user;

    enum Js5State state;
    enum Js5Error last_error;
    uint8_t handshake_status;
    uint8_t xor_key;
    uint16_t current_port;
    bool using_fallback;

    uint64_t now_ms;
    uint64_t connect_started_ms;
    uint64_t last_activity_ms;
    uint64_t backoff_until_ms;

    uint32_t connect_failures;
    uint32_t failure_streak;
    uint32_t crc_failures;
    uint32_t io_failures;
    uint32_t reconnects;
    uint32_t random_state;

    uint8_t tx_buffer[JS5_HANDSHAKE_BYTES];
    size_t tx_size;
    size_t tx_position;
    enum Js5TxKind tx_kind;
    struct Js5Request* tx_request;

    uint8_t control_buffer[JS5_PACKET_BYTES * 2u];
    size_t control_size;
    size_t control_position;
    bool login_control_dirty;
    bool xor_control_dirty;

    uint8_t response_header[JS5_RESPONSE_HEADER_BYTES];
    size_t response_header_size;
    uint8_t* response_data;
    size_t response_size;
    size_t response_position;
    size_t response_block_position;
    bool response_needs_marker;
    struct Js5Request* response_request;

    struct Js5Request* requests[JS5_REQUEST_BUCKETS];
    struct Js5RequestQueue urgent_queue;
    struct Js5RequestQueue normal_queue;
    uint32_t inflight_urgent;
    uint32_t inflight_normal;

    struct Js5Archive archives[JS5_ARCHIVE_COUNT];
    uint32_t master_crc[JS5_ARCHIVE_COUNT];
    uint32_t master_version[JS5_ARCHIVE_COUNT];
    uint16_t master_entries;
    bool master_ready;
    int scan_archive;

    struct Js5CompletionNode* completion_first;
    struct Js5CompletionNode* completion_last;

    uint32_t registered_archives;
    uint32_t references_ready;
    uint32_t archives_ready;
    uint32_t groups_total;
    uint32_t groups_valid;
    uint64_t bytes_received;
    uint64_t bytes_persisted;
};

static void js5_disconnect(struct Js5Client* client, enum Js5Error error);
static bool js5_process_registered_archive(struct Js5Client* client, int archive);
static bool js5_install_reference(
    struct Js5Client* client,
    int archive,
    const uint8_t* data,
    size_t size,
    bool from_network);
static int js5_validate_local_group(struct Js5Client* client, int archive, int group);

static char*
js5_string_duplicate(const char* value)
{
    assert(value);
    size_t size = strlen(value) + 1u;
    char* copy = (char*)malloc(size);
    if( copy )
        memcpy(copy, value, size);
    return copy;
}

static uint32_t
js5_read_u32(const uint8_t* data)
{
    return ((uint32_t)data[0] << 24u) | ((uint32_t)data[1] << 16u) |
           ((uint32_t)data[2] << 8u) | (uint32_t)data[3];
}

static void
js5_write_u32(uint8_t* data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24u);
    data[1] = (uint8_t)(value >> 16u);
    data[2] = (uint8_t)(value >> 8u);
    data[3] = (uint8_t)value;
}

static bool
js5_mask_get(const uint32_t mask[JS5_ARCHIVE_MASK_WORDS], int archive)
{
    if( archive < 0 || archive >= JS5_ARCHIVE_COUNT )
        return false;
    return (mask[(unsigned)archive >> 5u] & (1u << ((unsigned)archive & 31u))) != 0u;
}

static void
js5_mask_set(uint32_t mask[JS5_ARCHIVE_MASK_WORDS], int archive, bool value)
{
    if( archive < 0 || archive >= JS5_ARCHIVE_COUNT )
        return;
    uint32_t bit = 1u << ((unsigned)archive & 31u);
    if( value )
        mask[(unsigned)archive >> 5u] |= bit;
    else
        mask[(unsigned)archive >> 5u] &= ~bit;
}

static uint32_t
js5_request_hash(uint32_t key)
{
    return (key * 2654435761u) & (JS5_REQUEST_BUCKETS - 1u);
}

static struct Js5Request*
js5_request_find(const struct Js5Client* client, uint32_t key)
{
    struct Js5Request* request = client->requests[js5_request_hash(key)];
    while( request )
    {
        if( request->key == key )
            return request;
        request = request->hash_next;
    }
    return NULL;
}

/* Rev239 responses carry no priority bit. The authoritative client resolves a
 * same-key promotion by consulting urgent in-flight requests first, then the
 * original normal in-flight request. */
static struct Js5Request*
js5_request_find_inflight(const struct Js5Client* client, uint32_t key)
{
    struct Js5Request* normal = NULL;
    struct Js5Request* request = client->requests[js5_request_hash(key)];
    while( request )
    {
        if( request->key == key )
        {
            if( request->state == JS5_REQUEST_INFLIGHT_URGENT )
                return request;
            if( request->state == JS5_REQUEST_INFLIGHT_NORMAL )
                normal = request;
        }
        request = request->hash_next;
    }
    return normal;
}

static struct Js5Request*
js5_request_find_urgent(const struct Js5Client* client, uint32_t key)
{
    struct Js5Request* request = client->requests[js5_request_hash(key)];
    while( request )
    {
        if( request->key == key &&
            (request->state == JS5_REQUEST_QUEUED_URGENT ||
             request->state == JS5_REQUEST_TX_URGENT ||
             request->state == JS5_REQUEST_INFLIGHT_URGENT) )
            return request;
        request = request->hash_next;
    }
    return NULL;
}

static void
js5_request_hash_insert(struct Js5Client* client, struct Js5Request* request)
{
    uint32_t bucket = js5_request_hash(request->key);
    request->hash_next = client->requests[bucket];
    client->requests[bucket] = request;
}

static void
js5_request_hash_remove(struct Js5Client* client, struct Js5Request* request)
{
    uint32_t bucket = js5_request_hash(request->key);
    struct Js5Request** link = &client->requests[bucket];
    while( *link )
    {
        if( *link == request )
        {
            *link = request->hash_next;
            request->hash_next = NULL;
            return;
        }
        link = &(*link)->hash_next;
    }
}

static void
js5_queue_append(struct Js5RequestQueue* queue, struct Js5Request* request)
{
    request->queue_previous = queue->last;
    request->queue_next = NULL;
    if( queue->last )
        queue->last->queue_next = request;
    else
        queue->first = request;
    queue->last = request;
    queue->count++;
}

static void
js5_queue_prepend(struct Js5RequestQueue* queue, struct Js5Request* request)
{
    request->queue_previous = NULL;
    request->queue_next = queue->first;
    if( queue->first )
        queue->first->queue_previous = request;
    else
        queue->last = request;
    queue->first = request;
    queue->count++;
}

static void
js5_queue_remove(struct Js5RequestQueue* queue, struct Js5Request* request)
{
    if( request->queue_previous )
        request->queue_previous->queue_next = request->queue_next;
    else
        queue->first = request->queue_next;
    if( request->queue_next )
        request->queue_next->queue_previous = request->queue_previous;
    else
        queue->last = request->queue_previous;
    request->queue_previous = NULL;
    request->queue_next = NULL;
    if( queue->count > 0u )
        queue->count--;
}

static struct Js5Request*
js5_queue_pop(struct Js5RequestQueue* queue)
{
    struct Js5Request* request = queue->first;
    if( request )
        js5_queue_remove(queue, request);
    return request;
}

static void
js5_completion_push(
    struct Js5Client* client,
    enum Js5CompletionKind kind,
    enum Js5Error error,
    int archive,
    int group,
    enum Js5Priority priority,
    size_t bytes,
    bool from_network)
{
    struct Js5CompletionNode* node =
        (struct Js5CompletionNode*)malloc(sizeof(struct Js5CompletionNode));
    if( !node )
    {
        client->last_error = JS5_ERROR_NO_MEMORY;
        client->state = JS5_STATE_FAILED;
        return;
    }
    memset(node, 0, sizeof(*node));
    node->value.kind = kind;
    node->value.error = error;
    node->value.archive = (uint16_t)archive;
    node->value.group = (uint16_t)group;
    node->value.priority = priority;
    node->value.bytes = bytes > UINT32_MAX ? UINT32_MAX : (uint32_t)bytes;
    node->value.from_network = from_network;
    if( client->completion_last )
        client->completion_last->next = node;
    else
        client->completion_first = node;
    client->completion_last = node;
}

static void
js5_terminal_error(
    struct Js5Client* client,
    enum Js5Error error,
    int archive,
    int group)
{
    if( client->stream )
    {
        client->transport.close(client->transport_user, client->stream);
        client->stream = NULL;
    }
    client->last_error = error;
    client->state = JS5_STATE_FAILED;
    js5_completion_push(
        client,
        JS5_COMPLETION_ERROR,
        error,
        archive,
        group,
        JS5_PRIORITY_NORMAL,
        0u,
        false);
}

static void
js5_release_storage_data(struct Js5Client* client, uint8_t* data)
{
    if( !data )
        return;
    if( client->storage.release_raw )
        client->storage.release_raw(client->storage_user, data);
    else
        free(data);
}

static void*
js5_sockstream_open(void* user, const char* host, int port, int timeout_ms)
{
    (void)user;
    struct SockStream* stream = sockstream_new();
    if( !stream )
        return NULL;
    int timeout_seconds = timeout_ms <= 0 ? 0 : (timeout_ms + 999) / 1000;
    sockstream_connect(stream, host, port, timeout_seconds);
    return stream;
}

static int
js5_sockstream_poll(void* user, void* opaque)
{
    (void)user;
    int result = sockstream_poll_connect((struct SockStream*)opaque);
    if( result == SOCKSTREAM_CONNECT_SUCCESS )
        return 1;
    if( result == SOCKSTREAM_CONNECT_INFLIGHT )
        return 0;
    return -1;
}

static int
js5_sockstream_read(void* user, void* opaque, uint8_t* data, size_t size)
{
    (void)user;
    if( size > (size_t)INT_MAX )
        size = INT_MAX;
    struct SockStream* stream = (struct SockStream*)opaque;
    int result = sockstream_recv(stream, data, (int)size);
    if( result > 0 )
        return result;
    if( result == SOCKSTREAM_ERROR_NODATA && sockstream_is_connected(stream) )
        return 0;
    return -1;
}

static int
js5_sockstream_write(void* user, void* opaque, const uint8_t* data, size_t size)
{
    (void)user;
    if( size > (size_t)INT_MAX )
        size = INT_MAX;
    struct SockStream* stream = (struct SockStream*)opaque;
    int result = sockstream_send(stream, data, (int)size);
    if( result >= 0 )
        return result;
    if( sockstream_lasterror(stream) == SOCKSTREAM_ERROR_WOULDBLOCK )
        return 0;
    return -1;
}

static void
js5_sockstream_close(void* user, void* opaque)
{
    (void)user;
    sockstream_free((struct SockStream*)opaque);
}

void
Js5ConfigInit(struct Js5Config* config)
{
    assert(config);
    memset(config, 0, sizeof(*config));
    config->primary_port = 43594u;
    config->fallback_port = 443u;
    config->revision = JS5_REVISION_239;
    config->connect_timeout_ms = 30000u;
    config->inactivity_timeout_ms = 30000u;
    config->reconnect_delay_ms = 60000u;
    config->boot_failure_limit = 4u;
    config->max_response_bytes = 2u * 1024u * 1024u;
    config->local_scan_budget = 32u;
    config->random_seed = 0x6d2b79f5u;
    config->background_fill = true;
}

void
Js5ArchiveSelectionSet(
    struct Js5ArchiveSelection* selection,
    int archive,
    bool enabled,
    bool background,
    bool optional)
{
    assert(selection);
    js5_mask_set(selection->enabled, archive, enabled);
    js5_mask_set(selection->background, archive, enabled && background);
    js5_mask_set(selection->optional, archive, enabled && optional);
}

struct Js5Client*
Js5ClientNew(
    const struct Js5Config* config,
    const struct Js5TransportOps* transport,
    void* transport_user,
    const struct Js5StorageOps* storage,
    void* storage_user)
{
    if( !config || !config->host || !config->host[0] || config->revision == 0u ||
        config->primary_port == 0u )
        return NULL;

    struct Js5Client* client = (struct Js5Client*)calloc(1u, sizeof(struct Js5Client));
    if( !client )
        return NULL;
    client->config = *config;
    client->host = js5_string_duplicate(config->host);
    if( !client->host )
    {
        free(client);
        return NULL;
    }
    client->config.host = client->host;
    client->transport_user = transport_user;
    client->storage_user = storage_user;
    if( storage )
        client->storage = *storage;

    if( transport )
        client->transport = *transport;
    else
    {
        client->default_transport = true;
        client->transport.open = js5_sockstream_open;
        client->transport.poll_connect = js5_sockstream_poll;
        client->transport.read = js5_sockstream_read;
        client->transport.write = js5_sockstream_write;
        client->transport.close = js5_sockstream_close;
    }

    if( !client->transport.open || !client->transport.poll_connect || !client->transport.read ||
        !client->transport.write || !client->transport.close )
    {
        free(client->host);
        free(client);
        return NULL;
    }

    if( client->config.connect_timeout_ms == 0u )
        client->config.connect_timeout_ms = 30000u;
    if( client->config.inactivity_timeout_ms == 0u )
        client->config.inactivity_timeout_ms = 30000u;
    if( client->config.max_response_bytes < 9u )
        client->config.max_response_bytes = 2u * 1024u * 1024u;
    if( client->config.boot_failure_limit == 0u )
        client->config.boot_failure_limit = 4u;
    if( client->config.local_scan_budget == 0u )
        client->config.local_scan_budget = 32u;
    client->random_state = client->config.random_seed ? client->config.random_seed : 1u;
    client->current_port = client->config.primary_port;
    client->state = JS5_STATE_IDLE;

    for( int archive = 0; archive < JS5_ARCHIVE_COUNT; archive++ )
    {
        if( js5_mask_get(config->archives.enabled, archive) )
        {
            if( !Js5ClientRegisterArchive(
                    client,
                    archive,
                    js5_mask_get(config->archives.background, archive),
                    js5_mask_get(config->archives.optional, archive)) )
            {
                Js5ClientFree(client);
                return NULL;
            }
        }
    }
    return client;
}

static void
js5_archive_clear(struct Js5Client* client, struct Js5Archive* archive)
{
    if( archive->reference )
        RSCache_ReferenceTableFree(archive->reference);
    free(archive->group_states);
    if( client->groups_total >= archive->total_groups )
        client->groups_total -= archive->total_groups;
    if( client->groups_valid >= archive->valid_groups )
        client->groups_valid -= archive->valid_groups;
    memset(archive, 0, sizeof(*archive));
}

void
Js5ClientFree(struct Js5Client* client)
{
    if( !client )
        return;
    if( client->stream )
        client->transport.close(client->transport_user, client->stream);
    free(client->response_data);

    for( unsigned bucket = 0u; bucket < JS5_REQUEST_BUCKETS; bucket++ )
    {
        struct Js5Request* request = client->requests[bucket];
        while( request )
        {
            struct Js5Request* next = request->hash_next;
            free(request);
            request = next;
        }
    }
    for( int archive = 0; archive < JS5_ARCHIVE_COUNT; archive++ )
        js5_archive_clear(client, &client->archives[archive]);

    while( client->completion_first )
    {
        struct Js5CompletionNode* node = client->completion_first;
        client->completion_first = node->next;
        free(node);
    }
    free(client->host);
    free(client);
}

static bool
js5_request_is_inflight(enum Js5RequestState state)
{
    return state == JS5_REQUEST_INFLIGHT_NORMAL || state == JS5_REQUEST_INFLIGHT_URGENT;
}

static void
js5_request_destroy(struct Js5Client* client, struct Js5Request* request)
{
    if( !request )
        return;
    if( request->state == JS5_REQUEST_QUEUED_NORMAL )
        js5_queue_remove(&client->normal_queue, request);
    else if( request->state == JS5_REQUEST_QUEUED_URGENT )
        js5_queue_remove(&client->urgent_queue, request);
    else if( request->state == JS5_REQUEST_INFLIGHT_NORMAL && client->inflight_normal > 0u )
        client->inflight_normal--;
    else if( request->state == JS5_REQUEST_INFLIGHT_URGENT && client->inflight_urgent > 0u )
        client->inflight_urgent--;
    js5_request_hash_remove(client, request);
    free(request);
}

static struct Js5Request*
js5_request_new(
    struct Js5Client* client,
    enum Js5RequestKind kind,
    int archive,
    int group,
    uint32_t crc,
    uint32_t version,
    enum Js5Priority priority,
    bool demanded,
    bool wait_metadata)
{
    uint32_t key = ((uint32_t)archive << 16u) | (uint32_t)group;
    struct Js5Request* existing = js5_request_find(client, key);
    if( existing )
        return existing;

    struct Js5Request* request = (struct Js5Request*)calloc(1u, sizeof(*request));
    if( !request )
    {
        js5_terminal_error(client, JS5_ERROR_NO_MEMORY, archive, group);
        return NULL;
    }
    request->key = key;
    request->expected_crc = crc;
    request->expected_version = version;
    request->archive = (uint16_t)archive;
    request->group = (uint16_t)group;
    request->kind = kind;
    request->priority = priority;
    request->demanded = demanded;
    request->state = wait_metadata ? JS5_REQUEST_WAIT_METADATA
                                   : (priority == JS5_PRIORITY_URGENT
                                          ? JS5_REQUEST_QUEUED_URGENT
                                          : JS5_REQUEST_QUEUED_NORMAL);
    js5_request_hash_insert(client, request);
    if( !wait_metadata )
    {
        if( priority == JS5_PRIORITY_URGENT )
            js5_queue_append(&client->urgent_queue, request);
        else
            js5_queue_append(&client->normal_queue, request);
    }
    return request;
}

static bool
js5_request_promote(struct Js5Client* client, struct Js5Request* request)
{
    if( request->priority == JS5_PRIORITY_URGENT )
    {
        request->demanded = true;
        return true;
    }
    if( request->state == JS5_REQUEST_QUEUED_NORMAL )
    {
        js5_queue_remove(&client->normal_queue, request);
        request->priority = JS5_PRIORITY_URGENT;
        request->demanded = true;
        request->state = JS5_REQUEST_QUEUED_URGENT;
        js5_queue_append(&client->urgent_queue, request);
        return true;
    }
    if( request->state == JS5_REQUEST_WAIT_METADATA )
    {
        request->priority = JS5_PRIORITY_URGENT;
        request->demanded = true;
        return true;
    }
    if( request->state != JS5_REQUEST_TX_NORMAL &&
        request->state != JS5_REQUEST_INFLIGHT_NORMAL )
        return true;

    struct Js5Request* urgent = js5_request_find_urgent(client, request->key);
    if( urgent )
    {
        urgent->demanded = true;
        request->demanded = false;
        return true;
    }

    urgent = (struct Js5Request*)calloc(1u, sizeof(*urgent));
    if( !urgent )
    {
        js5_terminal_error(client, JS5_ERROR_NO_MEMORY, request->archive, request->group);
        return false;
    }
    urgent->key = request->key;
    urgent->expected_crc = request->expected_crc;
    urgent->expected_version = request->expected_version;
    urgent->archive = request->archive;
    urgent->group = request->group;
    urgent->kind = request->kind;
    urgent->state = JS5_REQUEST_QUEUED_URGENT;
    urgent->priority = JS5_PRIORITY_URGENT;
    urgent->demanded = true;
    request->demanded = false;
    js5_request_hash_insert(client, urgent);
    js5_queue_append(&client->urgent_queue, urgent);
    return true;
}

static void
js5_request_queue_waiting(struct Js5Client* client, struct Js5Request* request)
{
    request->state = request->priority == JS5_PRIORITY_URGENT ? JS5_REQUEST_QUEUED_URGENT
                                                              : JS5_REQUEST_QUEUED_NORMAL;
    if( request->state == JS5_REQUEST_QUEUED_URGENT )
        js5_queue_append(&client->urgent_queue, request);
    else
        js5_queue_append(&client->normal_queue, request);
}

static bool
js5_group_listed(const struct Js5Archive* archive, int group)
{
    return archive->reference && group >= 0 && group < archive->reference->archive_count &&
           archive->reference->archives[group].index >= 0 &&
           archive->reference->archives[group].children.count > 0;
}

static void
js5_try_archive_ready(struct Js5Client* client, int archive_id)
{
    struct Js5Archive* archive = &client->archives[archive_id];
    if( archive->ready || archive->empty || !archive->reference_ready || archive->scanning ||
        archive->valid_groups != archive->total_groups )
        return;
    archive->ready = true;
    client->archives_ready++;
    js5_completion_push(
        client,
        JS5_COMPLETION_ARCHIVE_READY,
        JS5_ERROR_NONE,
        archive_id,
        0,
        JS5_PRIORITY_NORMAL,
        0u,
        false);
}

static bool
js5_mark_group_ready(
    struct Js5Client* client,
    int archive_id,
    int group,
    bool emit,
    enum Js5Priority priority,
    size_t bytes,
    bool from_network)
{
    struct Js5Archive* archive = &client->archives[archive_id];
    if( !js5_group_listed(archive, group) )
        return false;
    if( archive->group_states[group] != JS5_GROUP_READY )
    {
        archive->group_states[group] = JS5_GROUP_READY;
        archive->valid_groups++;
        client->groups_valid++;
    }
    if( emit )
        js5_completion_push(
            client,
            JS5_COMPLETION_GROUP_READY,
            JS5_ERROR_NONE,
            archive_id,
            group,
            priority,
            bytes,
            from_network);
    js5_try_archive_ready(client, archive_id);
    return client->state != JS5_STATE_FAILED;
}

static struct RSCache_ReferenceTable*
js5_decode_reference(
    struct Js5Client* client,
    const uint8_t* raw,
    size_t raw_size,
    uint32_t expected_crc,
    uint32_t expected_version,
    size_t* container_size_out)
{
    size_t container_size = 0u;
    assert(raw);
    if( !RSCache_ArchiveRawContainerLength(raw, raw_size, &container_size) )
        return NULL;
    size_t trailer_size = raw_size - container_size;
    if( trailer_size != 0u && trailer_size != 2u && trailer_size != 4u )
        return NULL;
    if( RSCache_Crc32Buffer(raw, container_size) != expected_crc || container_size > INT_MAX )
        return NULL;
    if( raw[0] > RSCACHE_ARCHIVE_COMPRESSION_GZIP )
        return NULL;

    uint32_t unpacked_size = js5_read_u32(raw + 1u);
    if( raw[0] != RSCACHE_ARCHIVE_COMPRESSION_NONE )
    {
        if( container_size < 9u )
            return NULL;
        unpacked_size = js5_read_u32(raw + 5u);
    }
    if( unpacked_size > client->config.max_response_bytes || unpacked_size > INT_MAX )
        return NULL;

    struct RSCache_Dat2DiskArchive packed;
    memset(&packed, 0, sizeof(packed));
    packed.data = (char*)malloc(container_size);
    if( !packed.data )
    {
        js5_terminal_error(client, JS5_ERROR_NO_MEMORY, 255, 0);
        return NULL;
    }
    memcpy(packed.data, raw, container_size);
    packed.data_size = (int)container_size;
    if( !RSCache_ArchiveDecryptDecompress(&packed, NULL) )
    {
        free(packed.data);
        return NULL;
    }

    struct RSCache_ReferenceTable* table =
        RSCache_ReferenceTableNewDecode(packed.data, packed.data_size);
    free(packed.data);
    if( !table )
        return NULL;
    if( (uint32_t)table->version != expected_version ||
        (table->flags & (RSCACHE_REFTABLE_FLAG_WHIRLPOOL | RSCACHE_REFTABLE_FLAG_HASH)) != 0 )
    {
        RSCache_ReferenceTableFree(table);
        return NULL;
    }
    for( int index = 0; index < table->id_count; index++ )
    {
        int group = table->ids[index];
        if( group < 0 || group > UINT16_MAX || group >= table->archive_count ||
            table->archives[group].index < 0 || table->archives[group].children.count < 0 )
        {
            RSCache_ReferenceTableFree(table);
            return NULL;
        }
    }
    if( container_size_out )
        *container_size_out = container_size;
    return table;
}

static bool
js5_reference_apply(
    struct Js5Client* client,
    int archive_id,
    struct RSCache_ReferenceTable* table,
    bool from_network,
    size_t bytes)
{
    struct Js5Archive* archive = &client->archives[archive_id];
    uint8_t* states = NULL;
    if( table->archive_count > 0 )
    {
        states = (uint8_t*)calloc((size_t)table->archive_count, sizeof(uint8_t));
        if( !states )
        {
            RSCache_ReferenceTableFree(table);
            js5_terminal_error(client, JS5_ERROR_NO_MEMORY, 255, archive_id);
            return false;
        }
    }

    uint32_t total = 0u;
    for( int index = 0; index < table->id_count; index++ )
    {
        int group = table->ids[index];
        if( table->archives[group].children.count > 0 )
        {
            states[group] = JS5_GROUP_MISSING;
            total++;
        }
    }

    if( archive->reference )
        RSCache_ReferenceTableFree(archive->reference);
    free(archive->group_states);
    archive->reference = table;
    archive->group_states = states;
    archive->reference_ready = true;
    archive->scanning = true;
    archive->scan_position = 0;
    archive->total_groups = total;
    archive->valid_groups = 0u;
    client->references_ready++;
    client->groups_total += total;
    js5_completion_push(
        client,
        JS5_COMPLETION_REFERENCE_READY,
        JS5_ERROR_NONE,
        archive_id,
        0,
        JS5_PRIORITY_URGENT,
        bytes,
        from_network);
    return client->state != JS5_STATE_FAILED;
}

static bool
js5_install_reference(
    struct Js5Client* client,
    int archive_id,
    const uint8_t* data,
    size_t size,
    bool from_network)
{
    struct Js5Archive* archive = &client->archives[archive_id];
    size_t container_size = 0u;
    struct RSCache_ReferenceTable* table = js5_decode_reference(
        client,
        data,
        size,
        archive->reference_crc,
        archive->reference_version,
        &container_size);
    if( !table )
        return false;

    if( from_network )
    {
        if( !client->storage.install_reference ||
            client->storage.install_reference(
                client->storage_user, archive_id, data, container_size) <= 0 )
        {
            RSCache_ReferenceTableFree(table);
            js5_terminal_error(client, JS5_ERROR_STORAGE, 255, archive_id);
            return false;
        }
        client->bytes_persisted += container_size;
    }
    else if( client->storage.activate_reference &&
             client->storage.activate_reference(
                 client->storage_user, archive_id, data, container_size) <= 0 )
    {
        RSCache_ReferenceTableFree(table);
        js5_terminal_error(client, JS5_ERROR_STORAGE, 255, archive_id);
        return false;
    }
    return js5_reference_apply(client, archive_id, table, from_network, container_size);
}

static int
js5_validate_local_group(struct Js5Client* client, int archive_id, int group)
{
    struct Js5Archive* archive = &client->archives[archive_id];
    if( !js5_group_listed(archive, group) )
        return 0;
    if( archive->group_states[group] == JS5_GROUP_READY )
        return 1;
    if( !client->storage.read_raw )
        return 0;

    uint8_t* data = NULL;
    size_t size = 0u;
    int result = client->storage.read_raw(client->storage_user, archive_id, group, &data, &size);
    if( result == JS5_STORAGE_ERROR )
    {
        js5_terminal_error(client, JS5_ERROR_STORAGE, archive_id, group);
        return -1;
    }
    if( result != JS5_STORAGE_OK || !data )
    {
        js5_release_storage_data(client, data);
        return 0;
    }

    const struct RSCache_ReferenceTableArchive* metadata = &archive->reference->archives[group];
    bool valid = RSCache_ArchiveRawValidate(
        data,
        size,
        (uint32_t)metadata->crc,
        (uint32_t)metadata->version,
        NULL);
    js5_release_storage_data(client, data);
    if( !valid )
        return 0;
    if( !js5_mark_group_ready(
            client, archive_id, group, false, JS5_PRIORITY_NORMAL, size, false) )
        return -1;
    return 1;
}

static struct Js5Request*
js5_queue_group_request(
    struct Js5Client* client,
    int archive_id,
    int group,
    enum Js5Priority priority,
    bool demanded)
{
    struct Js5Archive* archive = &client->archives[archive_id];
    const struct RSCache_ReferenceTableArchive* metadata = &archive->reference->archives[group];
    struct Js5Request* request =
        js5_request_find(client, ((uint32_t)archive_id << 16u) | (uint32_t)group);
    if( request )
    {
        request->expected_crc = (uint32_t)metadata->crc;
        request->expected_version = (uint32_t)metadata->version;
        request->demanded = request->demanded || demanded;
        if( priority == JS5_PRIORITY_URGENT && !js5_request_promote(client, request) )
            return NULL;
        if( request->state == JS5_REQUEST_WAIT_METADATA )
            js5_request_queue_waiting(client, request);
    }
    else
    {
        request = js5_request_new(
            client,
            JS5_REQUEST_KIND_GROUP,
            archive_id,
            group,
            (uint32_t)metadata->crc,
            (uint32_t)metadata->version,
            priority,
            demanded,
            false);
    }
    if( request )
        archive->group_states[group] = JS5_GROUP_PENDING;
    return request;
}

static void
js5_archive_empty(struct Js5Client* client, int archive_id)
{
    struct Js5Archive* archive = &client->archives[archive_id];
    if( archive->empty )
        return;
    archive->empty = true;
    archive->ready = true;
    client->archives_ready++;
    js5_completion_push(
        client,
        JS5_COMPLETION_ARCHIVE_EMPTY,
        JS5_ERROR_NONE,
        archive_id,
        0,
        JS5_PRIORITY_NORMAL,
        0u,
        false);
}

static bool
js5_process_registered_archive(struct Js5Client* client, int archive_id)
{
    struct Js5Archive* archive = &client->archives[archive_id];
    if( !archive->registered || archive->reference_ready || archive->empty || !client->master_ready )
        return true;

    bool present = archive_id < client->master_entries &&
                   (client->master_crc[archive_id] != 0u ||
                    client->master_version[archive_id] != 0u);
    if( !present )
    {
        if( archive->optional )
        {
            js5_archive_empty(client, archive_id);
            return client->state != JS5_STATE_FAILED;
        }
        js5_terminal_error(client, JS5_ERROR_REFERENCE, 255, archive_id);
        return false;
    }

    archive->master_present = true;
    archive->reference_crc = client->master_crc[archive_id];
    archive->reference_version = client->master_version[archive_id];

    if( client->storage.read_raw )
    {
        uint8_t* data = NULL;
        size_t size = 0u;
        int result = client->storage.read_raw(client->storage_user, 255, archive_id, &data, &size);
        if( result == JS5_STORAGE_ERROR )
        {
            js5_release_storage_data(client, data);
            js5_terminal_error(client, JS5_ERROR_STORAGE, 255, archive_id);
            return false;
        }
        if( result == JS5_STORAGE_OK && data )
        {
            bool installed = js5_install_reference(client, archive_id, data, size, false);
            js5_release_storage_data(client, data);
            if( installed )
                return true;
            if( client->state == JS5_STATE_FAILED )
                return false;
        }
        else
            js5_release_storage_data(client, data);
    }

    return js5_request_new(
               client,
               JS5_REQUEST_KIND_REFERENCE,
               255,
               archive_id,
               archive->reference_crc,
               archive->reference_version,
               JS5_PRIORITY_URGENT,
               false,
               false) != NULL;
}

bool
Js5ClientRegisterArchive(
    struct Js5Client* client,
    int archive_id,
    bool background,
    bool optional)
{
    assert(client);
    if( archive_id < 0 || archive_id >= 255 || client->state == JS5_STATE_FAILED )
        return false;
    struct Js5Archive* archive = &client->archives[archive_id];
    if( archive->registered )
    {
        archive->background = archive->background || background;
        archive->optional = archive->optional && optional;
        return true;
    }
    archive->registered = true;
    archive->background = background;
    archive->optional = optional;
    client->registered_archives++;

    if( !client->master_ready && !js5_request_find(client, 0x00ffffffu) )
    {
        if( !js5_request_new(
                client,
                JS5_REQUEST_KIND_MASTER,
                255,
                255,
                0u,
                0u,
                JS5_PRIORITY_URGENT,
                false,
                false) )
            return false;
    }
    if( client->master_ready )
        return js5_process_registered_archive(client, archive_id);
    return true;
}

enum Js5RequestResult
Js5ClientRequestGroup(
    struct Js5Client* client,
    int archive_id,
    int group,
    enum Js5Priority priority)
{
    if( !client || archive_id < 0 || archive_id >= 255 || group < 0 || group > UINT16_MAX ||
        (priority != JS5_PRIORITY_NORMAL && priority != JS5_PRIORITY_URGENT) ||
        client->state == JS5_STATE_FAILED )
        return JS5_REQUEST_ERROR;
    struct Js5Archive* archive = &client->archives[archive_id];
    if( !archive->registered )
    {
        client->last_error = JS5_ERROR_ARGUMENT;
        return JS5_REQUEST_ERROR;
    }
    if( archive->empty )
    {
        client->last_error = JS5_ERROR_NOT_FOUND;
        return JS5_REQUEST_ERROR;
    }

    uint32_t key = ((uint32_t)archive_id << 16u) | (uint32_t)group;
    struct Js5Request* request = js5_request_find(client, key);
    if( !archive->reference_ready )
    {
        if( request )
        {
            request->demanded = true;
            if( priority == JS5_PRIORITY_URGENT &&
                !js5_request_promote(client, request) )
                return JS5_REQUEST_ERROR;
        }
        else if( !js5_request_new(
                     client,
                     JS5_REQUEST_KIND_GROUP,
                     archive_id,
                     group,
                     0u,
                     0u,
                     priority,
                     true,
                     true) )
            return JS5_REQUEST_ERROR;
        return JS5_REQUEST_DEFERRED;
    }
    if( !js5_group_listed(archive, group) )
    {
        client->last_error = JS5_ERROR_NOT_FOUND;
        return JS5_REQUEST_ERROR;
    }
    if( archive->group_states[group] == JS5_GROUP_READY )
        return JS5_REQUEST_ALREADY_READY;

    if( request && request->state != JS5_REQUEST_WAIT_METADATA )
    {
        request->demanded = true;
        if( priority == JS5_PRIORITY_URGENT && !js5_request_promote(client, request) )
            return JS5_REQUEST_ERROR;
        return JS5_REQUEST_QUEUED;
    }

    int local = js5_validate_local_group(client, archive_id, group);
    if( local < 0 )
        return JS5_REQUEST_ERROR;
    if( local > 0 )
    {
        if( request )
            js5_request_destroy(client, request);
        js5_completion_push(
            client,
            JS5_COMPLETION_GROUP_READY,
            JS5_ERROR_NONE,
            archive_id,
            group,
            priority,
            0u,
            false);
        return client->state == JS5_STATE_FAILED ? JS5_REQUEST_ERROR
                                                 : JS5_REQUEST_ALREADY_READY;
    }
    if( !js5_queue_group_request(client, archive_id, group, priority, true) )
        return JS5_REQUEST_ERROR;
    return JS5_REQUEST_QUEUED;
}

static void
js5_resolve_waiting_requests(struct Js5Client* client)
{
    for( unsigned bucket = 0u; bucket < JS5_REQUEST_BUCKETS; bucket++ )
    {
        struct Js5Request* request = client->requests[bucket];
        while( request )
        {
            struct Js5Request* next = request->hash_next;
            if( request->kind == JS5_REQUEST_KIND_GROUP &&
                request->state == JS5_REQUEST_WAIT_METADATA )
            {
                int archive_id = request->archive;
                int group = request->group;
                struct Js5Archive* archive = &client->archives[archive_id];
                if( archive->empty ||
                    (archive->reference_ready && !js5_group_listed(archive, group)) )
                {
                    js5_completion_push(
                        client,
                        JS5_COMPLETION_ERROR,
                        JS5_ERROR_NOT_FOUND,
                        archive_id,
                        group,
                        request->priority,
                        0u,
                        false);
                    js5_request_destroy(client, request);
                }
                else if( archive->reference_ready )
                {
                    int local = js5_validate_local_group(client, archive_id, group);
                    if( local > 0 )
                    {
                        js5_completion_push(
                            client,
                            JS5_COMPLETION_GROUP_READY,
                            JS5_ERROR_NONE,
                            archive_id,
                            group,
                            request->priority,
                            0u,
                            false);
                        js5_request_destroy(client, request);
                    }
                    else if( local == 0 )
                        js5_queue_group_request(
                            client,
                            archive_id,
                            group,
                            request->priority,
                            request->demanded);
                }
            }
            if( client->state == JS5_STATE_FAILED )
                return;
            request = next;
        }
    }
}

static void
js5_scan_local(struct Js5Client* client)
{
    uint32_t budget = client->config.local_scan_budget;
    unsigned archives_visited = 0u;
    while( budget > 0u && archives_visited < JS5_ARCHIVE_COUNT )
    {
        int archive_id = client->scan_archive;
        client->scan_archive = (client->scan_archive + 1) % JS5_ARCHIVE_COUNT;
        archives_visited++;
        struct Js5Archive* archive = &client->archives[archive_id];
        if( !archive->registered || !archive->reference_ready || !archive->scanning )
            continue;

        while( budget > 0u && archive->scan_position < archive->reference->archive_count )
        {
            int group = archive->scan_position++;
            budget--;
            if( !js5_group_listed(archive, group) ||
                archive->group_states[group] == JS5_GROUP_READY )
                continue;
            int local = js5_validate_local_group(client, archive_id, group);
            if( local < 0 )
                return;
            if( local == 0 && archive->background &&
                !js5_queue_group_request(
                    client, archive_id, group, JS5_PRIORITY_NORMAL, false) )
                return;
        }
        if( archive->scan_position >= archive->reference->archive_count )
        {
            archive->scanning = false;
            js5_try_archive_ready(client, archive_id);
            if( client->state == JS5_STATE_FAILED )
                return;
        }
    }
}

static void
js5_response_reset(struct Js5Client* client)
{
    free(client->response_data);
    client->response_data = NULL;
    client->response_size = 0u;
    client->response_position = 0u;
    client->response_block_position = 0u;
    client->response_needs_marker = false;
    client->response_request = NULL;
    client->response_header_size = 0u;
}

static bool
js5_has_network_work(const struct Js5Client* client)
{
    return client->urgent_queue.count != 0u || client->normal_queue.count != 0u ||
           client->inflight_urgent != 0u || client->inflight_normal != 0u ||
           client->tx_request != NULL;
}

static uint8_t
js5_random_xor_key(struct Js5Client* client)
{
    uint32_t state = client->random_state;
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    client->random_state = state ? state : 1u;
    uint8_t key = (uint8_t)state;
    return key ? key : 1u;
}

static void
js5_requeue_requests(struct Js5Client* client)
{
    client->inflight_normal = 0u;
    client->inflight_urgent = 0u;
    client->tx_request = NULL;
    for( unsigned bucket = 0u; bucket < JS5_REQUEST_BUCKETS; bucket++ )
    {
        for( struct Js5Request* request = client->requests[bucket]; request;
             request = request->hash_next )
        {
            if( js5_request_is_inflight(request->state) ||
                request->state == JS5_REQUEST_TX_NORMAL ||
                request->state == JS5_REQUEST_TX_URGENT )
            {
                if( request->priority == JS5_PRIORITY_URGENT )
                {
                    request->state = JS5_REQUEST_QUEUED_URGENT;
                    js5_queue_prepend(&client->urgent_queue, request);
                }
                else
                {
                    request->state = JS5_REQUEST_QUEUED_NORMAL;
                    js5_queue_prepend(&client->normal_queue, request);
                }
            }
        }
    }
}

static void
js5_disconnect(struct Js5Client* client, enum Js5Error error)
{
    bool had_connection = client->stream != NULL;
    if( client->stream )
    {
        client->transport.close(client->transport_user, client->stream);
        client->stream = NULL;
    }
    js5_response_reset(client);
    js5_requeue_requests(client);
    client->tx_kind = JS5_TX_NONE;
    client->tx_size = 0u;
    client->tx_position = 0u;
    client->control_size = 0u;
    client->control_position = 0u;
    client->login_control_dirty = false;
    client->xor_control_dirty = false;
    client->last_error = error;
    client->handshake_status = 0u;
    if( had_connection )
        client->reconnects++;

    if( client->config.fallback_port != 0u &&
        client->config.fallback_port != client->config.primary_port )
        client->using_fallback = !client->using_fallback;
    client->current_port = client->using_fallback ? client->config.fallback_port
                                                  : client->config.primary_port;

    if( js5_has_network_work(client) )
    {
        uint32_t delay =
            Js5ClientMetadataReady(client) ? client->config.reconnect_delay_ms : 0u;
        client->backoff_until_ms = client->now_ms + delay;
        client->state = delay == 0u ? JS5_STATE_IDLE : JS5_STATE_BACKOFF;
    }
    else
        client->state = JS5_STATE_IDLE;
}

static void
js5_transport_failure(struct Js5Client* client, enum Js5Error error)
{
    client->failure_streak++;
    if( !Js5ClientMetadataReady(client) &&
        client->failure_streak >= client->config.boot_failure_limit )
    {
        js5_terminal_error(client, error, 255, 255);
        return;
    }
    js5_disconnect(client, error);
}

static bool
js5_process_master(struct Js5Client* client, const uint8_t* data, size_t size)
{
    if( size < 5u || data[0] != RSCACHE_ARCHIVE_COMPRESSION_NONE ||
        (size_t)js5_read_u32(data + 1u) != size - 5u || ((size - 5u) & 7u) != 0u ||
        (size - 5u) / 8u > JS5_ARCHIVE_COUNT )
        return false;

    memset(client->master_crc, 0, sizeof(client->master_crc));
    memset(client->master_version, 0, sizeof(client->master_version));
    client->master_entries = (uint16_t)((size - 5u) / 8u);
    for( unsigned archive = 0u; archive < client->master_entries; archive++ )
    {
        client->master_crc[archive] = js5_read_u32(data + 5u + archive * 8u);
        client->master_version[archive] = js5_read_u32(data + 9u + archive * 8u);
    }
    client->master_ready = true;
    js5_completion_push(
        client,
        JS5_COMPLETION_MASTER_READY,
        JS5_ERROR_NONE,
        255,
        255,
        JS5_PRIORITY_URGENT,
        size,
        true);
    if( client->state == JS5_STATE_FAILED )
        return false;
    for( int archive = 0; archive < 255; archive++ )
    {
        if( client->archives[archive].registered &&
            !js5_process_registered_archive(client, archive) )
            return false;
    }
    return true;
}

static bool
js5_process_group(
    struct Js5Client* client,
    struct Js5Request* request,
    const uint8_t* data,
    size_t size)
{
    int archive_id = request->archive;
    int group = request->group;
    if( !client->storage.install_group ||
        client->storage.install_group(
            client->storage_user,
            archive_id,
            group,
            data,
            size,
            request->expected_version) <= 0 )
    {
        js5_terminal_error(client, JS5_ERROR_STORAGE, archive_id, group);
        return false;
    }
    client->bytes_persisted += size + 4u;
    return js5_mark_group_ready(
        client,
        archive_id,
        group,
        request->demanded,
        request->priority,
        size + 4u,
        true);
}

static bool
js5_response_finish(struct Js5Client* client)
{
    struct Js5Request* request = client->response_request;
    if( !request || client->response_position != client->response_size )
        return false;

    if( request->kind != JS5_REQUEST_KIND_MASTER &&
        RSCache_Crc32Buffer(client->response_data, client->response_size) !=
            request->expected_crc )
    {
        client->crc_failures++;
        if( client->crc_failures >= 4u )
        {
            js5_terminal_error(client, JS5_ERROR_CRC, request->archive, request->group);
            return false;
        }
        client->xor_key = js5_random_xor_key(client);
        js5_disconnect(client, JS5_ERROR_CRC);
        return false;
    }

    bool processed = false;
    if( request->kind == JS5_REQUEST_KIND_MASTER )
        processed = js5_process_master(client, client->response_data, client->response_size);
    else if( request->kind == JS5_REQUEST_KIND_REFERENCE )
        processed = js5_install_reference(
            client,
            request->group,
            client->response_data,
            client->response_size,
            true);
    else if( client->archives[request->archive].reference_ready &&
             js5_group_listed(&client->archives[request->archive], request->group) &&
             client->archives[request->archive].group_states[request->group] ==
                 JS5_GROUP_READY )
    {
        if( request->demanded )
            js5_completion_push(
                client,
                JS5_COMPLETION_GROUP_READY,
                JS5_ERROR_NONE,
                request->archive,
                request->group,
                request->priority,
                client->response_size,
                true);
        processed = client->state != JS5_STATE_FAILED;
    }
    else
        processed = js5_process_group(client, request, client->response_data, client->response_size);

    if( !processed )
    {
        if( client->state != JS5_STATE_FAILED )
            js5_terminal_error(
                client,
                request->kind == JS5_REQUEST_KIND_REFERENCE ? JS5_ERROR_REFERENCE
                                                            : JS5_ERROR_PROTOCOL,
                request->archive,
                request->group);
        return false;
    }

    client->crc_failures = 0u;
    client->failure_streak = 0u;
    client->last_error = JS5_ERROR_NONE;
    client->response_request = NULL;
    free(client->response_data);
    client->response_data = NULL;
    client->response_size = 0u;
    client->response_position = 0u;
    client->response_block_position = 0u;
    client->response_header_size = 0u;
    js5_request_destroy(client, request);
    return client->state != JS5_STATE_FAILED;
}

static bool
js5_response_begin(struct Js5Client* client)
{
    uint8_t archive = client->response_header[0];
    uint16_t group = (uint16_t)(((uint16_t)client->response_header[1] << 8u) |
                                (uint16_t)client->response_header[2]);
    uint8_t compression = client->response_header[3];
    uint32_t compressed_size = js5_read_u32(client->response_header + 4u);
    size_t header_size = compression == RSCACHE_ARCHIVE_COMPRESSION_NONE ? 5u : 9u;
    if( compression > RSCACHE_ARCHIVE_COMPRESSION_GZIP ||
        (size_t)compressed_size > SIZE_MAX - header_size )
        return false;
    size_t response_size = header_size + (size_t)compressed_size;
    if( response_size > client->config.max_response_bytes )
        return false;

    uint32_t key = ((uint32_t)archive << 16u) | group;
    struct Js5Request* request = js5_request_find_inflight(client, key);
    if( !request )
        return false;

    uint8_t* response = (uint8_t*)malloc(response_size);
    if( !response )
    {
        js5_terminal_error(client, JS5_ERROR_NO_MEMORY, archive, group);
        return false;
    }
    response[0] = compression;
    memcpy(response + 1u, client->response_header + 4u, 4u);
    client->response_data = response;
    client->response_size = response_size;
    client->response_position = 5u;
    client->response_block_position = JS5_RESPONSE_HEADER_BYTES;
    client->response_request = request;
    client->response_header_size = 0u;
    if( client->response_position == client->response_size )
        return js5_response_finish(client);
    return true;
}

static bool
js5_parse_bytes(struct Js5Client* client, const uint8_t* data, size_t size)
{
    size_t position = 0u;
    while( position < size )
    {
        if( !client->response_data )
        {
            size_t take = JS5_RESPONSE_HEADER_BYTES - client->response_header_size;
            if( take > size - position )
                take = size - position;
            memcpy(
                client->response_header + client->response_header_size, data + position, take);
            client->response_header_size += take;
            position += take;
            if( client->response_header_size == JS5_RESPONSE_HEADER_BYTES &&
                !js5_response_begin(client) )
                return false;
            continue;
        }

        if( client->response_block_position == JS5_NETWORK_BLOCK_BYTES )
        {
            if( data[position++] != 0xffu )
                return false;
            client->response_block_position = 1u;
            if( position == size )
                continue;
        }

        size_t take = client->response_size - client->response_position;
        size_t block_remaining = JS5_NETWORK_BLOCK_BYTES - client->response_block_position;
        if( take > block_remaining )
            take = block_remaining;
        if( take > size - position )
            take = size - position;
        memcpy(client->response_data + client->response_position, data + position, take);
        client->response_position += take;
        client->response_block_position += take;
        position += take;
        if( client->response_position == client->response_size &&
            !js5_response_finish(client) )
            return false;
    }
    return true;
}

static void
js5_prepare_handshake(struct Js5Client* client)
{
    memset(client->tx_buffer, 0, sizeof(client->tx_buffer));
    client->tx_buffer[0] = 15u;
    js5_write_u32(client->tx_buffer + 1u, client->config.revision);
    for( unsigned index = 0u; index < 4u; index++ )
        js5_write_u32(client->tx_buffer + 5u + index * 4u, client->config.seeds[index]);
    client->tx_kind = JS5_TX_HANDSHAKE;
    client->tx_size = JS5_HANDSHAKE_BYTES;
    client->tx_position = 0u;
    client->state = JS5_STATE_HANDSHAKE_WRITE;
}

static bool
js5_start_connect(struct Js5Client* client)
{
    client->stream = client->transport.open(
        client->transport_user,
        client->host,
        client->current_port,
        (int)client->config.connect_timeout_ms);
    client->connect_started_ms = client->now_ms;
    if( !client->stream )
    {
        client->connect_failures++;
        js5_transport_failure(client, JS5_ERROR_CONNECT);
        return false;
    }
    client->state = JS5_STATE_CONNECTING;
    return true;
}

static int
js5_write_buffer(
    struct Js5Client* client,
    const uint8_t* data,
    size_t size,
    size_t* position)
{
    int written = client->transport.write(
        client->transport_user, client->stream, data + *position, size - *position);
    if( written < 0 )
    {
        client->io_failures++;
        js5_transport_failure(client, JS5_ERROR_IO);
        return -1;
    }
    if( written == 0 )
        return 0;
    if( (size_t)written > size - *position )
    {
        js5_terminal_error(client, JS5_ERROR_PROTOCOL, 0, 0);
        return -1;
    }
    *position += (size_t)written;
    client->last_activity_ms = client->now_ms;
    return *position == size ? 1 : 0;
}

static void
js5_prepare_controls(struct Js5Client* client)
{
    if( client->control_position < client->control_size )
        return;
    client->control_position = 0u;
    client->control_size = 0u;
    if( client->login_control_dirty )
    {
        uint8_t* packet = client->control_buffer + client->control_size;
        packet[0] = client->config.logged_in ? 2u : 3u;
        packet[1] = 0u;
        packet[2] = 0u;
        packet[3] = 0u;
        client->control_size += JS5_PACKET_BYTES;
        client->login_control_dirty = false;
    }
    if( client->xor_control_dirty && client->xor_key != 0u )
    {
        uint8_t* packet = client->control_buffer + client->control_size;
        packet[0] = 4u;
        packet[1] = client->xor_key;
        packet[2] = 0u;
        packet[3] = 0u;
        client->control_size += JS5_PACKET_BYTES;
        client->xor_control_dirty = false;
    }
}

static int
js5_active_write(struct Js5Client* client)
{
    for( unsigned iteration = 0u; iteration < JS5_READ_ITERATIONS; iteration++ )
    {
        js5_prepare_controls(client);
        if( client->control_position < client->control_size )
        {
            int result = js5_write_buffer(
                client,
                client->control_buffer,
                client->control_size,
                &client->control_position);
            if( result <= 0 )
                return result;
            client->control_position = 0u;
            client->control_size = 0u;
            continue;
        }

        if( !client->tx_request )
        {
            struct Js5Request* request = NULL;
            if( client->urgent_queue.first &&
                client->inflight_urgent < JS5_MAX_INFLIGHT_PER_LANE )
                request = js5_queue_pop(&client->urgent_queue);
            else if( client->normal_queue.first &&
                     client->inflight_normal < JS5_MAX_INFLIGHT_PER_LANE )
                request = js5_queue_pop(&client->normal_queue);
            if( !request )
                return 1;

            bool urgent = request->state == JS5_REQUEST_QUEUED_URGENT;
            request->state = urgent ? JS5_REQUEST_TX_URGENT : JS5_REQUEST_TX_NORMAL;
            client->tx_request = request;
            client->tx_kind = JS5_TX_REQUEST;
            client->tx_buffer[0] = urgent ? 1u : 0u;
            client->tx_buffer[1] = (uint8_t)(request->key >> 16u);
            client->tx_buffer[2] = (uint8_t)(request->key >> 8u);
            client->tx_buffer[3] = (uint8_t)request->key;
            client->tx_size = JS5_PACKET_BYTES;
            client->tx_position = 0u;
        }

        int result = js5_write_buffer(
            client, client->tx_buffer, client->tx_size, &client->tx_position);
        if( result <= 0 )
            return result;
        struct Js5Request* request = client->tx_request;
        if( request->state == JS5_REQUEST_TX_URGENT )
        {
            request->state = JS5_REQUEST_INFLIGHT_URGENT;
            client->inflight_urgent++;
        }
        else
        {
            request->state = JS5_REQUEST_INFLIGHT_NORMAL;
            client->inflight_normal++;
        }
        client->tx_request = NULL;
        client->tx_kind = JS5_TX_NONE;
        client->tx_size = 0u;
        client->tx_position = 0u;
    }
    return 1;
}

static int
js5_active_read(struct Js5Client* client)
{
    uint8_t buffer[JS5_READ_BUFFER_BYTES];
    for( unsigned iteration = 0u; iteration < JS5_READ_ITERATIONS; iteration++ )
    {
        int received = client->transport.read(
            client->transport_user, client->stream, buffer, sizeof(buffer));
        if( received < 0 )
        {
            client->io_failures++;
            js5_transport_failure(client, JS5_ERROR_IO);
            return -1;
        }
        if( received == 0 )
            return 0;
        if( (size_t)received > sizeof(buffer) )
        {
            js5_terminal_error(client, JS5_ERROR_PROTOCOL, 0, 0);
            return -1;
        }
        client->last_activity_ms = client->now_ms;
        client->bytes_received += (uint32_t)received;
        if( client->xor_key != 0u )
        {
            for( int index = 0; index < received; index++ )
                buffer[index] ^= client->xor_key;
        }
        if( !js5_parse_bytes(client, buffer, (size_t)received) )
        {
            if( client->state == JS5_STATE_ACTIVE )
                js5_terminal_error(client, JS5_ERROR_PROTOCOL, 0, 0);
            return -1;
        }
    }
    return 1;
}

void
Js5ClientSetLoggedIn(struct Js5Client* client, bool logged_in)
{
    assert(client);
    if( client->config.logged_in == logged_in )
        return;
    client->config.logged_in = logged_in;
    if( client->state == JS5_STATE_ACTIVE )
        client->login_control_dirty = true;
}

void
Js5ClientSetSeeds(struct Js5Client* client, const uint32_t seeds[4])
{
    assert(client);
    assert(seeds);
    memcpy(client->config.seeds, seeds, sizeof(client->config.seeds));
}

int
Js5ClientTick(struct Js5Client* client, uint64_t now_ms)
{
    assert(client);
    client->now_ms = now_ms;
    if( client->state == JS5_STATE_FAILED )
        return -1;

    js5_resolve_waiting_requests(client);
    if( client->state == JS5_STATE_FAILED )
        return -1;
    js5_scan_local(client);
    if( client->state == JS5_STATE_FAILED )
        return -1;

    if( (client->state == JS5_STATE_HANDSHAKE_WRITE ||
         client->state == JS5_STATE_HANDSHAKE_STATUS) &&
        now_ms - client->connect_started_ms > client->config.connect_timeout_ms )
    {
        client->connect_failures++;
        js5_transport_failure(client, JS5_ERROR_TIMEOUT);
        return client->state == JS5_STATE_FAILED ? -1 : 0;
    }

    for( unsigned transition = 0u; transition < 5u; transition++ )
    {
        switch( client->state )
        {
        case JS5_STATE_IDLE:
            if( !js5_has_network_work(client) )
                return 0;
            if( !js5_start_connect(client) )
                return client->state == JS5_STATE_FAILED ? -1 : 0;
            break;

        case JS5_STATE_BACKOFF:
            if( now_ms < client->backoff_until_ms )
                return 0;
            client->state = JS5_STATE_IDLE;
            break;

        case JS5_STATE_CONNECTING:
        {
            if( now_ms - client->connect_started_ms > client->config.connect_timeout_ms )
            {
                client->connect_failures++;
                js5_transport_failure(client, JS5_ERROR_TIMEOUT);
                return client->state == JS5_STATE_FAILED ? -1 : 0;
            }
            int result = client->transport.poll_connect(client->transport_user, client->stream);
            if( result < 0 )
            {
                client->connect_failures++;
                js5_transport_failure(client, JS5_ERROR_CONNECT);
                return client->state == JS5_STATE_FAILED ? -1 : 0;
            }
            if( result == 0 )
                return 0;
            js5_prepare_handshake(client);
            break;
        }

        case JS5_STATE_HANDSHAKE_WRITE:
        {
            int result = js5_write_buffer(
                client, client->tx_buffer, client->tx_size, &client->tx_position);
            if( result < 0 )
                return client->state == JS5_STATE_FAILED ? -1 : 0;
            if( result == 0 )
                return 0;
            client->tx_kind = JS5_TX_NONE;
            client->tx_size = 0u;
            client->tx_position = 0u;
            client->state = JS5_STATE_HANDSHAKE_STATUS;
            break;
        }

        case JS5_STATE_HANDSHAKE_STATUS:
        {
            uint8_t status = 0u;
            int received =
                client->transport.read(client->transport_user, client->stream, &status, 1u);
            if( received < 0 )
            {
                client->io_failures++;
                js5_transport_failure(client, JS5_ERROR_IO);
                return client->state == JS5_STATE_FAILED ? -1 : 0;
            }
            if( received == 0 )
                return 0;
            if( received != 1 )
            {
                js5_terminal_error(client, JS5_ERROR_PROTOCOL, 255, 255);
                return -1;
            }
            client->last_activity_ms = now_ms;
            client->handshake_status = status;
            if( status != 0u )
            {
                enum Js5Error error = status == 6u   ? JS5_ERROR_OUT_OF_DATE
                                      : status == 7u || status == 9u
                                          ? JS5_ERROR_SERVER_FULL
                                          : JS5_ERROR_HANDSHAKE;
                js5_terminal_error(client, error, 255, 255);
                return -1;
            }
            client->state = JS5_STATE_ACTIVE;
            client->last_error = JS5_ERROR_NONE;
            client->login_control_dirty = true;
            client->xor_control_dirty = client->xor_key != 0u;
            break;
        }

        case JS5_STATE_ACTIVE:
            if( js5_active_write(client) < 0 )
                return client->state == JS5_STATE_FAILED ? -1 : 0;
            if( client->state != JS5_STATE_ACTIVE )
                return 0;
            if( js5_active_read(client) < 0 )
                return client->state == JS5_STATE_FAILED ? -1 : 0;
            if( client->state == JS5_STATE_ACTIVE && js5_has_network_work(client) &&
                now_ms - client->last_activity_ms > client->config.inactivity_timeout_ms )
            {
                client->io_failures++;
                js5_transport_failure(client, JS5_ERROR_TIMEOUT);
            }
            return client->state == JS5_STATE_FAILED ? -1 : 0;

        case JS5_STATE_FAILED:
            return -1;
        }
    }
    return client->state == JS5_STATE_FAILED ? -1 : 0;
}

bool
Js5ClientNextCompletion(struct Js5Client* client, struct Js5Completion* completion)
{
    assert(client);
    if( !client->completion_first )
        return false;
    assert(completion);
    struct Js5CompletionNode* node = client->completion_first;
    client->completion_first = node->next;
    if( !client->completion_first )
        client->completion_last = NULL;
    *completion = node->value;
    free(node);
    return true;
}

void
Js5ClientGetProgress(const struct Js5Client* client, struct Js5Progress* progress)
{
    assert(progress);
    memset(progress, 0, sizeof(*progress));
    if( !client )
        return;
    progress->state = client->state;
    progress->last_error = client->last_error;
    progress->current_port = client->current_port;
    progress->handshake_status = client->handshake_status;
    progress->xor_key = client->xor_key;
    progress->queued_urgent = client->urgent_queue.count;
    progress->inflight_urgent = client->inflight_urgent;
    progress->queued_normal = client->normal_queue.count;
    progress->inflight_normal = client->inflight_normal;
    progress->registered_archives = client->registered_archives;
    progress->references_ready = client->references_ready;
    progress->archives_ready = client->archives_ready;
    progress->groups_total = client->groups_total;
    progress->groups_valid = client->groups_valid;
    progress->bytes_received = client->bytes_received;
    progress->bytes_persisted = client->bytes_persisted;
    progress->reconnects = client->reconnects;
    progress->boot_failure_streak = client->failure_streak;
    progress->crc_failures = client->crc_failures;
    progress->io_failures = client->io_failures;
}

enum Js5State
Js5ClientState(const struct Js5Client* client)
{
    return client ? client->state : JS5_STATE_FAILED;
}

enum Js5Error
Js5ClientLastError(const struct Js5Client* client)
{
    return client ? client->last_error : JS5_ERROR_ARGUMENT;
}

bool
Js5ClientArchiveReady(const struct Js5Client* client, int archive)
{
    return client && archive >= 0 && archive < 255 && client->archives[archive].ready;
}

bool
Js5ClientMetadataReady(const struct Js5Client* client)
{
    assert(client);
    if( !client->master_ready )
        return false;
    for( int archive = 0; archive < 255; archive++ )
    {
        if( client->archives[archive].registered && !client->archives[archive].empty &&
            !client->archives[archive].reference_ready )
            return false;
    }
    return true;
}

bool
Js5ClientGroupReady(const struct Js5Client* client, int archive, int group)
{
    if( !client || archive < 0 || archive >= 255 || group < 0 ||
        !js5_group_listed(&client->archives[archive], group) )
        return false;
    return client->archives[archive].group_states[group] == JS5_GROUP_READY;
}

bool
Js5ClientReferenceMetadata(
    const struct Js5Client* client,
    int archive,
    uint32_t* crc,
    uint32_t* version)
{
    if( !client || archive < 0 || archive >= 255 ||
        !client->archives[archive].master_present )
        return false;
    if( crc )
        *crc = client->archives[archive].reference_crc;
    if( version )
        *version = client->archives[archive].reference_version;
    return true;
}

bool
Js5ClientGroupMetadata(
    const struct Js5Client* client,
    int archive,
    int group,
    uint32_t* crc,
    uint32_t* version,
    bool* ready)
{
    if( !client || archive < 0 || archive >= 255 || group < 0 ||
        !js5_group_listed(&client->archives[archive], group) )
        return false;
    const struct RSCache_ReferenceTableArchive* metadata =
        &client->archives[archive].reference->archives[group];
    if( crc )
        *crc = (uint32_t)metadata->crc;
    if( version )
        *version = (uint32_t)metadata->version;
    if( ready )
        *ready = client->archives[archive].group_states[group] == JS5_GROUP_READY;
    return true;
}
