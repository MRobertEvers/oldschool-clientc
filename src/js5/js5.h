#ifndef SRC_JS5_JS5_H
#define SRC_JS5_JS5_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JS5_REVISION_239 239u
#define JS5_ARCHIVE_COUNT 256
#define JS5_ARCHIVE_MASK_WORDS (JS5_ARCHIVE_COUNT / 32)
#define JS5_MAX_INFLIGHT_PER_LANE 200

enum Js5Priority
{
    JS5_PRIORITY_NORMAL = 0,
    JS5_PRIORITY_URGENT = 1,
};

enum Js5State
{
    JS5_STATE_IDLE = 0,
    JS5_STATE_BACKOFF,
    JS5_STATE_CONNECTING,
    JS5_STATE_HANDSHAKE_WRITE,
    JS5_STATE_HANDSHAKE_STATUS,
    JS5_STATE_ACTIVE,
    JS5_STATE_FAILED,
};

enum Js5Error
{
    JS5_ERROR_NONE = 0,
    JS5_ERROR_ARGUMENT,
    JS5_ERROR_NO_MEMORY,
    JS5_ERROR_CONNECT,
    JS5_ERROR_TIMEOUT,
    JS5_ERROR_IO,
    JS5_ERROR_OUT_OF_DATE,
    JS5_ERROR_SERVER_FULL,
    JS5_ERROR_HANDSHAKE,
    JS5_ERROR_PROTOCOL,
    JS5_ERROR_CRC,
    JS5_ERROR_REFERENCE,
    JS5_ERROR_STORAGE,
    JS5_ERROR_NOT_FOUND,
};

enum Js5CompletionKind
{
    JS5_COMPLETION_MASTER_READY = 0,
    JS5_COMPLETION_REFERENCE_READY,
    JS5_COMPLETION_ARCHIVE_READY,
    JS5_COMPLETION_ARCHIVE_EMPTY,
    JS5_COMPLETION_GROUP_READY,
    JS5_COMPLETION_ERROR,
};

enum Js5RequestResult
{
    JS5_REQUEST_ERROR = -1,
    JS5_REQUEST_ALREADY_READY = 0,
    JS5_REQUEST_QUEUED = 1,
    JS5_REQUEST_DEFERRED = 2,
};

enum Js5StorageResult
{
    JS5_STORAGE_ERROR = -1,
    JS5_STORAGE_MISSING = 0,
    JS5_STORAGE_OK = 1,
};

/*
 * Transport callbacks are deliberately smaller than SockStream's API so tests
 * and embedded executors can provide a deterministic byte pipe.
 *
 * open() starts a non-blocking connection. poll_connect() returns 1 when
 * connected, 0 while in progress and -1 on failure. read()/write() return a
 * positive byte count, 0 for would-block and -1 for a closed/failed stream.
 */
struct Js5TransportOps
{
    void* (*open)(void* user, const char* host, int port, int timeout_ms);
    int (*poll_connect)(void* user, void* stream);
    int (*read)(void* user, void* stream, uint8_t* data, size_t size);
    int (*write)(void* user, void* stream, const uint8_t* data, size_t size);
    void (*close)(void* user, void* stream);
};

/*
 * read_raw() returns an owned buffer containing the exact idx entry bytes.
 * release_raw() releases it; when release_raw is NULL, free() is used.
 *
 * install_reference() receives a raw JS5 container with no revision trailer.
 * activate_reference() is optional and is called for a validated local
 * reference so adapters with an in-memory table registry can publish it.
 * install_group() receives the same plus the reference-table version separately;
 * it must publish container || version_be32. Returning success is the
 * persistence boundary after which JS5 emits a READY completion.
 */
struct Js5StorageOps
{
    int (*read_raw)(
        void* user,
        int archive,
        int group,
        uint8_t** data,
        size_t* size);
    void (*release_raw)(void* user, uint8_t* data);
    int (*install_reference)(
        void* user,
        int archive,
        const uint8_t* container,
        size_t size);
    int (*activate_reference)(
        void* user,
        int archive,
        const uint8_t* container,
        size_t size);
    int (*install_group)(
        void* user,
        int archive,
        int group,
        const uint8_t* container,
        size_t size,
        uint32_t version);
};

struct Js5ArchiveSelection
{
    uint32_t enabled[JS5_ARCHIVE_MASK_WORDS];
    uint32_t background[JS5_ARCHIVE_MASK_WORDS];
    uint32_t optional[JS5_ARCHIVE_MASK_WORDS];
};

struct Js5Config
{
    const char* host;
    uint16_t primary_port;
    uint16_t fallback_port;
    uint32_t revision;
    uint32_t seeds[4];
    bool logged_in;

    uint32_t connect_timeout_ms;
    uint32_t inactivity_timeout_ms;
    uint32_t reconnect_delay_ms;
    /* Consecutive transport failures allowed before metadata is ready (default 4). */
    uint32_t boot_failure_limit;
    uint32_t max_response_bytes;
    uint32_t local_scan_budget;
    uint32_t random_seed;

    /*
     * Fill the whole cache in the background, not just what is asked for.
     *
     * True is the desktop default and what "incremental cache" usually means:
     * every group the reference tables list and the disk does not have is
     * queued on the normal lane, so the cache converges on a complete mirror
     * while the client runs.
     *
     * A browser wants the opposite. A tab that quietly pulls a couple of
     * hundred megabytes is a bad citizen on someone's connection, it competes
     * with the reads the boot is actually blocked on, and the records have to
     * be held resident on the JavaScript side (see dat2_web_store.h) — so the
     * web lane sets this false and fetches on demand. The cache then converges
     * on the working set instead of the mirror, which is the right target when
     * the storage is a browser's.
     */
    bool background_fill;

    struct Js5ArchiveSelection archives;
};

struct Js5Completion
{
    enum Js5CompletionKind kind;
    enum Js5Error error;
    enum Js5Priority priority;
    uint16_t archive;
    uint16_t group;
    uint32_t bytes;
    bool from_network;
};

struct Js5Progress
{
    enum Js5State state;
    enum Js5Error last_error;
    uint16_t current_port;
    uint8_t handshake_status;
    uint8_t xor_key;

    uint32_t queued_urgent;
    uint32_t inflight_urgent;
    uint32_t queued_normal;
    uint32_t inflight_normal;
    uint32_t registered_archives;
    uint32_t references_ready;
    uint32_t archives_ready;
    uint32_t groups_total;
    uint32_t groups_valid;
    uint64_t bytes_received;
    uint64_t bytes_persisted;
    uint32_t reconnects;
    uint32_t boot_failure_streak;
    uint32_t crc_failures;
    uint32_t io_failures;
};

struct Js5Client;

void
Js5ConfigInit(struct Js5Config* config);

void
Js5ArchiveSelectionSet(
    struct Js5ArchiveSelection* selection,
    int archive,
    bool enabled,
    bool background,
    bool optional);

/*
 * A NULL transport selects the SockStream adapter. The process must have called
 * sockstream_init() already; Js5Client never calls global sockstream cleanup and
 * therefore cannot tear down the game connection's socket subsystem.
 */
struct Js5Client*
Js5ClientNew(
    const struct Js5Config* config,
    const struct Js5TransportOps* transport,
    void* transport_user,
    const struct Js5StorageOps* storage,
    void* storage_user);

void
Js5ClientFree(struct Js5Client* client);

/* Registering the first archive queues the mandatory urgent 255/255 request. */
bool
Js5ClientRegisterArchive(
    struct Js5Client* client,
    int archive,
    bool background,
    bool optional);

enum Js5RequestResult
Js5ClientRequestGroup(
    struct Js5Client* client,
    int archive,
    int group,
    enum Js5Priority priority);

void
Js5ClientSetLoggedIn(
    struct Js5Client* client,
    bool logged_in);

void
Js5ClientSetSeeds(
    struct Js5Client* client,
    const uint32_t seeds[4]);

/* now_ms is executor-owned monotonic time. Returns -1 only after terminal failure. */
int
Js5ClientTick(
    struct Js5Client* client,
    uint64_t now_ms);

bool
Js5ClientNextCompletion(
    struct Js5Client* client,
    struct Js5Completion* completion);

void
Js5ClientGetProgress(
    const struct Js5Client* client,
    struct Js5Progress* progress);

enum Js5State
Js5ClientState(const struct Js5Client* client);

enum Js5Error
Js5ClientLastError(const struct Js5Client* client);

/* True only after the reference table is installed and its local scan completed. */
bool
Js5ClientArchiveReady(
    const struct Js5Client* client,
    int archive);

/* Every enabled non-empty master entry has a validated, persisted reference table. */
bool
Js5ClientMetadataReady(const struct Js5Client* client);

bool
Js5ClientGroupReady(
    const struct Js5Client* client,
    int archive,
    int group);

/* Metadata accessors are useful to executor diagnostics and deterministic tests. */
bool
Js5ClientReferenceMetadata(
    const struct Js5Client* client,
    int archive,
    uint32_t* crc,
    uint32_t* version);

bool
Js5ClientGroupMetadata(
    const struct Js5Client* client,
    int archive,
    int group,
    uint32_t* crc,
    uint32_t* version,
    bool* ready);

#ifdef __cplusplus
}
#endif

#endif
