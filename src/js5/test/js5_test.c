#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "js5/js5.h"
#include "js5/js5_rscache.h"
#include "js5/server/js5_server_cache.h"

#include <dat2disk.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

struct FakeJs5Transport
{
    struct Js5ServerCache* server;
    uint8_t* incoming;
    size_t incoming_size;
    size_t incoming_position;
    uint8_t frame[21];
    size_t frame_size;
    size_t frame_expected;
    unsigned write_calls;
    unsigned read_calls;
    unsigned opens;
    bool handshake_complete;
    bool connected;
    bool failed;
    bool saw_512_marker;
    bool saw_xor_control;
    bool corrupt_once;
    bool corrupted;
    uint8_t corrupt_archive;
    uint16_t corrupt_group;
    bool hold_promotion;
    unsigned hold_phase;
    uint8_t hold_archive;
    uint16_t hold_group;
    uint8_t* held_response;
    size_t held_response_size;
    uint8_t xor_key;
    uint32_t seeds[4];
    struct
    {
        uint8_t opcode;
        uint8_t archive;
        uint16_t group;
    } requests[64];
    unsigned request_count;
};

static uint32_t
read_u32(const uint8_t* data)
{
    return ((uint32_t)data[0] << 24u) | ((uint32_t)data[1] << 16u) |
           ((uint32_t)data[2] << 8u) | data[3];
}

static bool
fake_append(struct FakeJs5Transport* fake, const uint8_t* data, size_t size)
{
    if( size > SIZE_MAX - fake->incoming_size )
        return false;
    uint8_t* grown = (uint8_t*)realloc(fake->incoming, fake->incoming_size + size);
    if( !grown )
        return false;
    fake->incoming = grown;
    memcpy(grown + fake->incoming_size, data, size);
    fake->incoming_size += size;
    return true;
}

static bool
fake_handle_frame(struct FakeJs5Transport* fake)
{
    if( !fake->handshake_complete )
    {
        if( fake->frame[0] != 15u || read_u32(fake->frame + 1u) != JS5_REVISION_239 )
            return false;
        for( unsigned index = 0u; index < 4u; index++ )
        {
            if( read_u32(fake->frame + 5u + index * 4u) != fake->seeds[index] )
                return false;
        }
        static const uint8_t success = 0u;
        fake->handshake_complete = true;
        fake->frame_expected = 4u;
        return fake_append(fake, &success, 1u);
    }

    uint8_t opcode = fake->frame[0];
    if( opcode == 2u || opcode == 3u )
        return fake->frame[1] == 0u && fake->frame[2] == 0u && fake->frame[3] == 0u;
    if( opcode == 4u )
    {
        if( fake->frame[2] != 0u || fake->frame[3] != 0u )
            return false;
        fake->xor_key = fake->frame[1];
        fake->saw_xor_control = fake->xor_key != 0u;
        return true;
    }
    if( opcode != 0u && opcode != 1u )
        return false;

    uint8_t archive = fake->frame[1];
    uint16_t group =
        (uint16_t)(((uint16_t)fake->frame[2] << 8u) | (uint16_t)fake->frame[3]);
    if( fake->request_count >= sizeof(fake->requests) / sizeof(fake->requests[0]) )
        return false;
    fake->requests[fake->request_count].opcode = opcode;
    fake->requests[fake->request_count].archive = archive;
    fake->requests[fake->request_count].group = group;
    fake->request_count++;

    uint8_t* response = NULL;
    size_t response_size = 0u;
    if( Js5ServerCacheBuildResponse(
            fake->server, archive, group, &response, &response_size) !=
        JS5_SERVER_CACHE_OK )
        return false;
    if( response_size > 512 && response[512] == 0xffu )
        fake->saw_512_marker = true;
    if( fake->corrupt_once && !fake->corrupted && archive == fake->corrupt_archive &&
        group == fake->corrupt_group )
    {
        response[response_size - 1] ^= 0x01u;
        fake->corrupted = true;
    }
    if( fake->xor_key != 0u )
    {
        for( size_t index = 0; index < response_size; index++ )
            response[index] ^= fake->xor_key;
    }

    if( fake->hold_promotion && archive == fake->hold_archive && group == fake->hold_group )
    {
        if( opcode == 0u && fake->hold_phase == 1u )
        {
            fake->held_response = response;
            fake->held_response_size = response_size;
            fake->hold_phase = 2u;
            return true;
        }
        if( opcode == 1u && fake->hold_phase == 2u )
        {
            bool appended = fake_append(
                fake, fake->held_response, fake->held_response_size);
            free(fake->held_response);
            fake->held_response = response;
            fake->held_response_size = response_size;
            fake->hold_phase = 3u;
            return appended;
        }
    }
    bool appended = fake_append(fake, response, response_size);
    free(response);
    return appended;
}

static bool
fake_release_held_response(struct FakeJs5Transport* fake)
{
    if( !fake || fake->hold_phase != 3u || !fake->held_response )
        return false;
    bool appended = fake_append(fake, fake->held_response, fake->held_response_size);
    free(fake->held_response);
    fake->held_response = NULL;
    fake->held_response_size = 0u;
    fake->hold_phase = 4u;
    return appended;
}

static void*
fake_open(void* user, const char* host, int port, int timeout_ms)
{
    struct FakeJs5Transport* fake = (struct FakeJs5Transport*)user;
    (void)timeout_ms;
    if( !fake || !host || strcmp(host, "mock") != 0 || port != 43594 )
        return NULL;
    fake->connected = true;
    fake->handshake_complete = false;
    fake->frame_size = 0u;
    fake->frame_expected = 21u;
    fake->xor_key = 0u;
    fake->opens++;
    return fake;
}

static int
fake_poll_connect(void* user, void* stream)
{
    return user == stream && ((struct FakeJs5Transport*)user)->connected ? 1 : -1;
}

static int
fake_write(void* user, void* stream, const uint8_t* data, size_t size)
{
    struct FakeJs5Transport* fake = (struct FakeJs5Transport*)user;
    if( stream != fake || !fake->connected || !data || size == 0u )
        return -1;
    size_t take = 1u + fake->write_calls++ % 5u;
    if( take > size )
        take = size;
    size_t consumed = 0u;
    while( consumed < take )
    {
        size_t part = fake->frame_expected - fake->frame_size;
        if( part > take - consumed )
            part = take - consumed;
        memcpy(fake->frame + fake->frame_size, data + consumed, part);
        fake->frame_size += part;
        consumed += part;
        if( fake->frame_size == fake->frame_expected )
        {
            if( !fake_handle_frame(fake) )
            {
                fake->failed = true;
                return -1;
            }
            fake->frame_size = 0u;
        }
    }
    return (int)take;
}

static int
fake_read(void* user, void* stream, uint8_t* data, size_t size)
{
    static const uint8_t chunks[] = { 1u, 2u, 7u, 3u, 29u, 5u, 11u, 17u };
    struct FakeJs5Transport* fake = (struct FakeJs5Transport*)user;
    if( stream != fake || !fake->connected || !data || size == 0u )
        return -1;
    size_t available = fake->incoming_size - fake->incoming_position;
    if( available == 0u )
        return 0;
    size_t take = chunks[fake->read_calls++ % (sizeof(chunks) / sizeof(chunks[0]))];
    if( take > available )
        take = available;
    if( take > size )
        take = size;
    memcpy(data, fake->incoming + fake->incoming_position, take);
    fake->incoming_position += take;
    return (int)take;
}

static void
fake_close(void* user, void* stream)
{
    struct FakeJs5Transport* fake = (struct FakeJs5Transport*)user;
    if( stream == fake )
        fake->connected = false;
}

static void
fake_reset_pipe(struct FakeJs5Transport* fake)
{
    free(fake->incoming);
    free(fake->held_response);
    fake->incoming = NULL;
    fake->incoming_size = 0u;
    fake->incoming_position = 0u;
    fake->connected = false;
    fake->handshake_complete = false;
    fake->frame_size = 0u;
    fake->frame_expected = 21u;
    fake->held_response = NULL;
    fake->held_response_size = 0u;
    fake->hold_promotion = false;
    fake->hold_phase = 0u;
}

static const struct Js5TransportOps FAKE_TRANSPORT = {
    fake_open,
    fake_poll_connect,
    fake_read,
    fake_write,
    fake_close,
};

static void*
fail_open(void* user, const char* host, int port, int timeout_ms)
{
    (void)user;
    (void)host;
    (void)port;
    (void)timeout_ms;
    return NULL;
}

static int
fail_poll(void* user, void* stream)
{
    (void)user;
    (void)stream;
    return -1;
}

static int
fail_read(void* user, void* stream, uint8_t* data, size_t size)
{
    (void)user;
    (void)stream;
    (void)data;
    (void)size;
    return -1;
}

static int
fail_write(void* user, void* stream, const uint8_t* data, size_t size)
{
    (void)user;
    (void)stream;
    (void)data;
    (void)size;
    return -1;
}

static void
fail_close(void* user, void* stream)
{
    (void)user;
    (void)stream;
}

static const struct Js5TransportOps FAIL_TRANSPORT = {
    fail_open,
    fail_poll,
    fail_read,
    fail_write,
    fail_close,
};

static bool
run_until_group(struct Js5Client* client, int archive, int group)
{
    for( uint64_t tick = 0u; tick < 200000u; tick++ )
    {
        if( Js5ClientTick(client, tick) < 0 )
        {
            struct Js5Progress progress;
            Js5ClientGetProgress(client, &progress);
            fprintf(
                stderr,
                "terminal at tick %llu: state=%d error=%d queued=%u/%u inflight=%u/%u\n",
                (unsigned long long)tick,
                progress.state,
                progress.last_error,
                progress.queued_urgent,
                progress.queued_normal,
                progress.inflight_urgent,
                progress.inflight_normal);
            return false;
        }
        if( Js5ClientMetadataReady(client) && Js5ClientGroupReady(client, archive, group) )
            return true;
    }
    {
        struct Js5Progress progress;
        Js5ClientGetProgress(client, &progress);
        fprintf(
            stderr,
            "timeout: state=%d error=%d metadata=%d group=%d queued=%u/%u inflight=%u/%u\n",
            progress.state,
            progress.last_error,
            Js5ClientMetadataReady(client),
            Js5ClientGroupReady(client, archive, group),
            progress.queued_urgent,
            progress.queued_normal,
            progress.inflight_urgent,
            progress.inflight_normal);
    }
    return false;
}

static bool
corrupt_first_group_sector(const char* directory, int archive, int group)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/main_file_cache.idx%d", directory, archive);
    FILE* index = fopen(path, "rb");
    if( !index )
        return false;
    if( fseek(index, (long)group * 6L, SEEK_SET) != 0 )
    {
        fclose(index);
        return false;
    }
    uint8_t record[6];
    bool read = fread(record, 1u, sizeof(record), index) == sizeof(record);
    fclose(index);
    if( !read )
        return false;
    int sector = (record[3] << 16) | (record[4] << 8) | record[5];
    if( sector <= 0 )
        return false;

    snprintf(path, sizeof(path), "%s/main_file_cache.dat2", directory);
    FILE* dat2 = fopen(path, "r+b");
    if( !dat2 )
        return false;
    long payload = (long)sector * 520L + (group > 0xffff ? 10L : 8L);
    if( fseek(dat2, payload, SEEK_SET) != 0 )
    {
        fclose(dat2);
        return false;
    }
    int byte = fgetc(dat2);
    if( byte == EOF || fseek(dat2, payload, SEEK_SET) != 0 ||
        fputc(byte ^ 0x5a, dat2) == EOF )
    {
        fclose(dat2);
        return false;
    }
    bool closed = fclose(dat2) == 0;
    return closed;
}

static bool
create_disposable_target(char* target, size_t capacity)
{
#ifdef _WIN32
    char root[MAX_PATH];
    char placeholder[MAX_PATH];
    DWORD length = GetTempPathA((DWORD)sizeof(root), root);
    if( length == 0u || length >= sizeof(root) ||
        !GetTempFileNameA(root, "js5", 0u, placeholder) )
        return false;
    if( !DeleteFileA(placeholder) || !CreateDirectoryA(placeholder, NULL) )
        return false;
    size_t needed = strlen(placeholder) + 1u;
    if( needed > capacity )
    {
        RemoveDirectoryA(placeholder);
        return false;
    }
    memcpy(target, placeholder, needed);
    return true;
#else
    const char pattern[] = "/tmp/oldschool-js5-test-XXXXXX";
    if( sizeof(pattern) > capacity )
        return false;
    memcpy(target, pattern, sizeof(pattern));
    return mkdtemp(target) != NULL;
#endif
}

static void
remove_disposable_target(const char* target)
{
    static const char* files[] = {
        "main_file_cache.dat2",
        "main_file_cache.idx7",
        "main_file_cache.idx255",
    };
    char path[1024];
    for( unsigned index = 0u; index < sizeof(files) / sizeof(files[0]); index++ )
    {
        snprintf(path, sizeof(path), "%s/%s", target, files[index]);
#ifdef _WIN32
        DeleteFileA(path);
#else
        unlink(path);
#endif
    }
#ifdef _WIN32
    RemoveDirectoryA(target);
#else
    rmdir(target);
#endif
}

static struct Js5Client*
new_client(
    struct FakeJs5Transport* fake,
    struct Js5RscacheStorage* storage,
    int archive,
    int group)
{
    struct Js5Config config;
    Js5ConfigInit(&config);
    config.host = "mock";
    config.fallback_port = config.primary_port;
    config.reconnect_delay_ms = 0u;
    config.max_response_bytes = 4u * 1024u * 1024u;
    config.local_scan_budget = 32u;
    memcpy(config.seeds, fake->seeds, sizeof(config.seeds));
    Js5ArchiveSelectionSet(&config.archives, archive, true, false, false);
    struct Js5StorageOps operations = Js5RscacheStorageOperations();
    struct Js5Client* client =
        Js5ClientNew(&config, &FAKE_TRANSPORT, fake, &operations, storage);
    if( !client )
        return NULL;
    if( Js5ClientRequestGroup(client, archive, group, JS5_PRIORITY_URGENT) !=
        JS5_REQUEST_DEFERRED )
    {
        Js5ClientFree(client);
        return NULL;
    }
    return client;
}

#define CHECK(condition)                                                                           \
    do                                                                                             \
    {                                                                                              \
        checks++;                                                                                  \
        if( !(condition) )                                                                         \
        {                                                                                          \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition);                          \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while( 0 )

int
main(int argc, char** argv)
{
    int checks = 0;
    int result = 1;
    struct Js5ServerCache* server = NULL;
    struct RSCache_Dat2Disk* disk = NULL;
    struct Js5RscacheStorage storage;
    struct Js5Client* client = NULL;
    char target[1024];
    target[0] = '\0';
    memset(&storage, 0, sizeof(storage));
    if( argc != 2 )
    {
        fprintf(stderr, "usage: %s <canonical-cache-read-only>\n", argv[0]);
        return 2;
    }
    if( !create_disposable_target(target, sizeof(target)) )
    {
        fprintf(stderr, "cannot create disposable JS5 target\n");
        return 2;
    }

    struct FakeJs5Transport fake;
    memset(&fake, 0, sizeof(fake));
    fake.seeds[0] = 0x11223344u;
    fake.seeds[1] = 0x55667788u;
    fake.seeds[2] = 0x99aabbccu;
    fake.seeds[3] = 0xddeeff00u;
    server = Js5ServerCacheOpen(argv[1]);
    CHECK(server != NULL);
    fake.server = server;

    {
        struct Js5Config failure_config;
        Js5ConfigInit(&failure_config);
        failure_config.host = "unreachable";
        failure_config.fallback_port = failure_config.primary_port;
        failure_config.reconnect_delay_ms = 0u;
        Js5ArchiveSelectionSet(&failure_config.archives, 7, true, false, false);
        struct Js5Client* failure_client =
            Js5ClientNew(&failure_config, &FAIL_TRANSPORT, NULL, NULL, NULL);
        CHECK(failure_client != NULL);
        CHECK(Js5ClientTick(failure_client, 0u) == 0);
        CHECK(Js5ClientTick(failure_client, 1u) == 0);
        CHECK(Js5ClientTick(failure_client, 2u) == 0);
        CHECK(Js5ClientTick(failure_client, 3u) == -1);
        CHECK(Js5ClientState(failure_client) == JS5_STATE_FAILED);
        CHECK(Js5ClientLastError(failure_client) == JS5_ERROR_CONNECT);
        struct Js5Progress failure_progress;
        Js5ClientGetProgress(failure_client, &failure_progress);
        CHECK(failure_progress.boot_failure_streak == 4u);
        Js5ClientFree(failure_client);
    }

    disk = RSCache_Dat2DiskNewSparseFromDirectory(target);
    CHECK(disk != NULL);
    CHECK(Js5RscacheStorageInit(&storage, disk, target));
    client = new_client(&fake, &storage, 7, 0);
    CHECK(client != NULL);
    CHECK(run_until_group(client, 7, 0));
    CHECK(fake.request_count >= 3u);
    CHECK(fake.requests[0].opcode == 1u && fake.requests[0].archive == 255u &&
          fake.requests[0].group == 255u);
    CHECK(fake.requests[1].opcode == 1u && fake.requests[1].archive == 255u &&
          fake.requests[1].group == 7u);
    CHECK(fake.requests[2].opcode == 1u && fake.requests[2].archive == 7u &&
          fake.requests[2].group == 0u);
    CHECK(fake.saw_512_marker);
    CHECK(!fake.failed);
    unsigned after_first = fake.request_count;
    Js5ClientFree(client);
    client = NULL;
    Js5RscacheStorageClear(&storage);
    RSCache_Dat2DiskFree(disk);
    disk = NULL;
    fake_reset_pipe(&fake);

    disk = RSCache_Dat2DiskNewSparseFromDirectory(target);
    CHECK(disk != NULL);
    CHECK(Js5RscacheStorageInit(&storage, disk, target));
    client = new_client(&fake, &storage, 7, 0);
    CHECK(client != NULL);
    CHECK(run_until_group(client, 7, 0));
    CHECK(fake.request_count == after_first + 1u); /* master only: ref + group came from disk */
    Js5ClientFree(client);
    client = NULL;
    Js5RscacheStorageClear(&storage);
    RSCache_Dat2DiskFree(disk);
    disk = NULL;
    fake_reset_pipe(&fake);

    CHECK(corrupt_first_group_sector(target, 7, 0));
    disk = RSCache_Dat2DiskNewSparseFromDirectory(target);
    CHECK(disk != NULL);
    CHECK(Js5RscacheStorageInit(&storage, disk, target));
    client = new_client(&fake, &storage, 7, 0);
    CHECK(client != NULL);
    CHECK(run_until_group(client, 7, 0));
    CHECK(fake.request_count == after_first + 3u); /* master + repaired group */
    CHECK(fake.requests[fake.request_count - 1u].archive == 7u &&
          fake.requests[fake.request_count - 1u].group == 0u);
    CHECK(!fake.failed);

    fake.corrupt_once = true;
    fake.corrupt_archive = 7u;
    fake.corrupt_group = 1u;
    unsigned before_crc_retry = fake.request_count;
    unsigned before_crc_open = fake.opens;
    CHECK(Js5ClientRequestGroup(client, 7, 1, JS5_PRIORITY_NORMAL) == JS5_REQUEST_QUEUED);
    CHECK(Js5ClientRequestGroup(client, 7, 1, JS5_PRIORITY_URGENT) == JS5_REQUEST_QUEUED);
    CHECK(run_until_group(client, 7, 1));
    CHECK(fake.corrupted);
    CHECK(fake.saw_xor_control);
    CHECK(fake.opens == before_crc_open + 1u);
    CHECK(fake.request_count == before_crc_retry + 2u);
    CHECK(fake.requests[before_crc_retry].opcode == 1u &&
          fake.requests[before_crc_retry + 1u].opcode == 1u);
    struct Js5Progress retry_progress;
    Js5ClientGetProgress(client, &retry_progress);
    CHECK(retry_progress.xor_key != 0u && retry_progress.crc_failures == 0u);

    /* A normal request already on the wire is not moved. Rev239 sends a second
     * urgent request and, because responses have no lane bit, resolves the
     * first same-key response against urgent-inflight before normal-inflight. */
    struct Js5Completion completion;
    while( Js5ClientNextCompletion(client, &completion) )
    {
    }
    fake.hold_promotion = true;
    fake.hold_phase = 1u;
    fake.hold_archive = 7u;
    fake.hold_group = 2u;
    unsigned before_promotion_requests = fake.request_count;
    struct Js5Progress before_promotion;
    Js5ClientGetProgress(client, &before_promotion);
    CHECK(Js5ClientRequestGroup(client, 7, 2, JS5_PRIORITY_NORMAL) == JS5_REQUEST_QUEUED);

    uint64_t now = 1000000u;
    bool normal_inflight = false;
    for( unsigned tick = 0u; tick < 1000u; tick++ )
    {
        CHECK(Js5ClientTick(client, now++) == 0);
        struct Js5Progress progress;
        Js5ClientGetProgress(client, &progress);
        if( progress.inflight_normal == 1u && fake.hold_phase == 2u )
        {
            normal_inflight = true;
            break;
        }
    }
    CHECK(normal_inflight);
    CHECK(!Js5ClientGroupReady(client, 7, 2));
    CHECK(Js5ClientRequestGroup(client, 7, 2, JS5_PRIORITY_URGENT) == JS5_REQUEST_QUEUED);
    struct Js5Progress promoted;
    Js5ClientGetProgress(client, &promoted);
    CHECK(promoted.inflight_normal == 1u && promoted.queued_urgent == 1u);

    bool urgent_won_lookup = false;
    for( unsigned tick = 0u; tick < 10000u; tick++ )
    {
        CHECK(Js5ClientTick(client, now++) == 0);
        Js5ClientGetProgress(client, &promoted);
        if( fake.hold_phase == 3u && Js5ClientGroupReady(client, 7, 2) &&
            promoted.inflight_urgent == 0u && promoted.inflight_normal == 1u )
        {
            urgent_won_lookup = true;
            break;
        }
    }
    CHECK(urgent_won_lookup);
    CHECK(fake.request_count == before_promotion_requests + 2u);
    CHECK(fake.requests[before_promotion_requests].opcode == 0u &&
          fake.requests[before_promotion_requests + 1u].opcode == 1u);
    uint64_t persisted_after_first_response = promoted.bytes_persisted;
    CHECK(persisted_after_first_response > before_promotion.bytes_persisted);

    unsigned group_completions = 0u;
    while( Js5ClientNextCompletion(client, &completion) )
    {
        if( completion.kind == JS5_COMPLETION_GROUP_READY && completion.archive == 7u &&
            completion.group == 2u )
        {
            group_completions++;
            CHECK(completion.priority == JS5_PRIORITY_URGENT);
        }
    }
    CHECK(group_completions == 1u);
    CHECK(fake_release_held_response(&fake));

    bool duplicate_consumed = false;
    for( unsigned tick = 0u; tick < 10000u; tick++ )
    {
        CHECK(Js5ClientTick(client, now++) == 0);
        Js5ClientGetProgress(client, &promoted);
        if( promoted.inflight_normal == 0u )
        {
            duplicate_consumed = true;
            break;
        }
    }
    CHECK(duplicate_consumed);
    CHECK(promoted.bytes_persisted == persisted_after_first_response);
    CHECK(!Js5ClientNextCompletion(client, &completion));
    CHECK(!fake.failed);
    result = 0;

cleanup:
    Js5ClientFree(client);
    Js5RscacheStorageClear(&storage);
    RSCache_Dat2DiskFree(disk);
    fake_reset_pipe(&fake);
    Js5ServerCacheFree(server);
    if( target[0] )
        remove_disposable_target(target);
    if( result == 0 )
        printf("js5_test: %d checks passed\n", checks);
    return result;
}
