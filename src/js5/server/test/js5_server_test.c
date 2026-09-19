#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
/* Darwin files mkdtemp() under BSD extensions, which _POSIX_C_SOURCE hides. */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#endif
#endif

#include "js5/server/js5_server_cache.h"
#include "js5/server/js5_server_session.h"

#include <archive.h>
#include <checksum.h>
#include <dat2disk.h>
#include <reference_table.h>

#include <stdbool.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#define JS5_TEST_REVISION 239u
#define JS5_TEST_MASTER_ARCHIVE 255u
#define JS5_TEST_MASTER_GROUP 255u

struct TestContext
{
    int checks;
    int failures;
};

#define CHECK(context, condition)                                                                  \
    do                                                                                             \
    {                                                                                              \
        (context)->checks++;                                                                       \
        if( !(condition) )                                                                         \
        {                                                                                          \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition);                         \
            (context)->failures++;                                                                 \
            ok = false;                                                                            \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while( 0 )

static void
write_u32(uint8_t* data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24u);
    data[1] = (uint8_t)(value >> 16u);
    data[2] = (uint8_t)(value >> 8u);
    data[3] = (uint8_t)value;
}

static void
make_handshake(uint8_t handshake[21], uint32_t revision)
{
    memset(handshake, 0, 21u);
    handshake[0] = 15u;
    write_u32(handshake + 1u, revision);
    write_u32(handshake + 5u, 0x11223344u);
    write_u32(handshake + 9u, 0x55667788u);
    write_u32(handshake + 13u, 0x99aabbccu);
    write_u32(handshake + 17u, 0xddeeff00u);
}

static void
make_request(uint8_t request[4], uint8_t opcode, uint8_t archive, uint16_t group)
{
    request[0] = opcode;
    request[1] = archive;
    request[2] = (uint8_t)(group >> 8u);
    request[3] = (uint8_t)group;
}

static bool
create_test_cache_directory(char* target, size_t capacity)
{
#ifdef _WIN32
    char root[MAX_PATH];
    char placeholder[MAX_PATH];
    DWORD length = GetTempPathA((DWORD)sizeof(root), root);
    if( length == 0u || length >= sizeof(root) ||
        !GetTempFileNameA(root, "jss", 0u, placeholder) || !DeleteFileA(placeholder) ||
        !CreateDirectoryA(placeholder, NULL) )
        return false;
    if( strlen(placeholder) + 1u > capacity )
    {
        RemoveDirectoryA(placeholder);
        return false;
    }
    memcpy(target, placeholder, strlen(placeholder) + 1u);
    return true;
#else
    const char pattern[] = "/tmp/oldschool-js5-server-cache-XXXXXX";
    if( sizeof(pattern) > capacity )
        return false;
    memcpy(target, pattern, sizeof(pattern));
    return mkdtemp(target) != NULL;
#endif
}

static void
remove_test_cache_directory(const char* target)
{
    static const char* names[] = {
        "main_file_cache.dat2",
        "main_file_cache.idx2",
        "main_file_cache.idx255",
    };
    char path[1024];

    if( !target || !target[0] )
        return;
    for( size_t index = 0u; index < sizeof(names) / sizeof(names[0]); index++ )
    {
        snprintf(path, sizeof(path), "%s/%s", target, names[index]);
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

static bool
make_integrity_group(
    uint32_t version,
    uint8_t** out,
    int* out_size)
{
    static const uint8_t payload[] = { 'j', 's', '5', '-', 'i', 'n', 't', 'e', 'g', 'r', 'i', 't', 'y' };
    uint32_t bound = RSCache_ArchiveEncodeBound(
        (uint32_t)sizeof(payload), RSCACHE_ARCHIVE_COMPRESSION_NONE);
    uint8_t* data = (uint8_t*)malloc((size_t)bound + 4u);
    uint32_t size = data ? RSCache_ArchiveEncode(
                               data,
                               bound,
                               payload,
                               (uint32_t)sizeof(payload),
                               RSCACHE_ARCHIVE_COMPRESSION_NONE,
                               NULL)
                         : 0u;
    if( size == 0u || size > INT_MAX - 4 )
    {
        free(data);
        return false;
    }
    write_u32(data + size, version);
    *out = data;
    *out_size = (int)size + 4;
    return true;
}

static bool
make_integrity_reference(
    uint16_t group,
    uint32_t group_crc,
    uint32_t group_version,
    uint8_t** out,
    int* out_size)
{
    struct RSCache_ReferenceTable table;
    struct RSCache_ReferenceTableArchiveFile child;
    int id = group;
    uint8_t* encoded = NULL;
    uint8_t* container = NULL;
    uint32_t encoded_bound;
    uint32_t encoded_size;
    uint32_t container_bound;
    uint32_t container_size;

    memset(&table, 0, sizeof(table));
    memset(&child, 0, sizeof(child));
    table.format = 7;
    table.version = 17;
    table.id_count = 1;
    table.ids = &id;
    table.archive_count = (int)group + 1;
    table.archives = (struct RSCache_ReferenceTableArchive*)calloc(
        (size_t)table.archive_count, sizeof(*table.archives));
    if( !table.archives )
        return false;
    for( int index = 0; index < table.archive_count; index++ )
        RSCache_ReferenceTableSetHasArchive(&table, index, false);
    RSCache_ReferenceTableSetHasArchive(&table, group, true);
    table.archives[group].crc = (int)group_crc;
    table.archives[group].version = (int)group_version;
    table.archives[group].children.count = 1;
    table.archives[group].children.files = &child;

    encoded_bound = RSCache_ReferenceTableEncodeBound(&table);
    encoded = (uint8_t*)malloc(encoded_bound);
    encoded_size = encoded
                       ? RSCache_ReferenceTableEncode(&table, encoded, encoded_bound)
                       : 0u;
    free(table.archives);
    if( encoded_size == 0u )
    {
        free(encoded);
        return false;
    }
    container_bound = RSCache_ArchiveEncodeBound(
        encoded_size, RSCACHE_ARCHIVE_COMPRESSION_NONE);
    container = (uint8_t*)malloc(container_bound);
    container_size = container ? RSCache_ArchiveEncode(
                                     container,
                                     container_bound,
                                     encoded,
                                     encoded_size,
                                     RSCACHE_ARCHIVE_COMPRESSION_NONE,
                                     NULL)
                               : 0u;
    free(encoded);
    if( container_size == 0u || container_size > INT_MAX )
    {
        free(container);
        return false;
    }
    *out = container;
    *out_size = (int)container_size;
    return true;
}

static struct Js5ServerSession*
new_active_session(
    struct Js5ServerCache* cache,
    const struct Js5ServerSessionConfig* config,
    uint64_t now_ms)
{
    uint8_t handshake[21];
    const uint8_t* output = NULL;
    size_t output_size = 0u;
    struct Js5ServerSession* session = Js5ServerSessionNew(cache, config, now_ms);
    if( !session )
        return NULL;
    make_handshake(handshake, config ? config->revision : JS5_TEST_REVISION);
    if( Js5ServerSessionFeed(session, handshake, sizeof(handshake), now_ms) != 0 ||
        Js5ServerSessionPeekOutput(session, &output, &output_size) != 1 ||
        output_size != 1u || output[0] != 0u ||
        Js5ServerSessionConsumeOutput(session, output_size, now_ms) != 0 )
    {
        Js5ServerSessionFree(session);
        return NULL;
    }
    return session;
}

static bool
consume_expected_response(
    struct Js5ServerSession* session,
    const uint8_t* expected,
    size_t expected_size,
    uint8_t xor_key,
    uint64_t* now_ms)
{
    size_t offset = 0u;
    while( offset < expected_size )
    {
        const uint8_t* output = NULL;
        size_t output_size = 0u;
        if( Js5ServerSessionPeekOutput(session, &output, &output_size) != 1 || !output ||
            output_size == 0u || output_size > expected_size - offset )
            return false;
        for( size_t index = 0u; index < output_size; index++ )
        {
            if( output[index] != (uint8_t)(expected[offset + index] ^ xor_key) )
                return false;
        }
        (*now_ms)++;
        if( Js5ServerSessionConsumeOutput(session, output_size, *now_ms) != 0 )
            return false;
        offset += output_size;
    }
    return true;
}

static bool
test_cache_master(struct TestContext* context, struct Js5ServerCache* cache)
{
    bool ok = true;
    struct Js5ServerBlob blob = { 0 };
    uint8_t* response = NULL;
    size_t response_size = 0u;

    CHECK(context, Js5ServerCacheMasterSize(cache) == 205u);
    CHECK(
        context,
        Js5ServerCacheLoad(cache, JS5_TEST_MASTER_ARCHIVE, JS5_TEST_MASTER_GROUP, &blob) ==
            JS5_SERVER_CACHE_OK);
    CHECK(context, blob.size == 205u);
    CHECK(context, blob.data != NULL && blob.data[0] == 0u);
    CHECK(
        context,
        Js5ServerCacheBuildResponse(
            cache,
            JS5_TEST_MASTER_ARCHIVE,
            JS5_TEST_MASTER_GROUP,
            &response,
            &response_size) == JS5_SERVER_CACHE_OK);
    CHECK(context, response_size == 208u);
    CHECK(
        context,
        response[0] == JS5_TEST_MASTER_ARCHIVE && response[1] == 0u &&
            response[2] == 0xffu && memcmp(response + 3u, blob.data, blob.size) == 0);

cleanup:
    free(response);
    Js5ServerBlobFree(&blob);
    return ok;
}

static bool
test_fragmentation_and_modes(struct TestContext* context, struct Js5ServerCache* cache)
{
    bool ok = true;
    struct Js5ServerSession* session = NULL;
    struct Js5ServerSessionStats stats;
    struct Js5ServerSessionConfig config;
    uint8_t handshake[21];
    uint8_t request[4];
    const uint8_t* output = NULL;
    size_t output_size = 0u;
    uint8_t* expected = NULL;
    size_t expected_size = 0u;
    uint64_t now_ms = 100u;

    Js5ServerSessionConfigInit(&config);
    session = Js5ServerSessionNew(cache, &config, now_ms);
    CHECK(context, session != NULL);
    make_handshake(handshake, JS5_TEST_REVISION);

    CHECK(context, Js5ServerSessionFeed(session, handshake, 1u, ++now_ms) == 0);
    CHECK(context, Js5ServerSessionPeekOutput(session, &output, &output_size) == 0);
    CHECK(context, Js5ServerSessionFeed(session, handshake + 1u, 3u, ++now_ms) == 0);
    CHECK(context, Js5ServerSessionFeed(session, handshake + 4u, 7u, ++now_ms) == 0);
    Js5ServerSessionGetStats(session, &stats);
    CHECK(context, stats.state == JS5_SERVER_SESSION_HANDSHAKE && stats.client_revision == 0u);
    CHECK(context, Js5ServerSessionFeed(session, handshake + 11u, 10u, ++now_ms) == 0);
    Js5ServerSessionGetStats(session, &stats);
    CHECK(
        context,
        stats.state == JS5_SERVER_SESSION_ACTIVE && stats.client_revision == JS5_TEST_REVISION);
    CHECK(
        context,
        Js5ServerSessionPeekOutput(session, &output, &output_size) == 1 &&
            output_size == 1u && output[0] == 0u);
    CHECK(context, Js5ServerSessionConsumeOutput(session, 1u, ++now_ms) == 0);

    make_request(request, 2u, 0u, 0u);
    CHECK(context, Js5ServerSessionFeed(session, request, 1u, ++now_ms) == 0);
    Js5ServerSessionGetStats(session, &stats);
    CHECK(context, !stats.logged_in);
    CHECK(context, Js5ServerSessionFeed(session, request + 1u, 3u, ++now_ms) == 0);
    Js5ServerSessionGetStats(session, &stats);
    CHECK(context, stats.logged_in);
    make_request(request, 3u, 0u, 0u);
    CHECK(context, Js5ServerSessionFeed(session, request, sizeof(request), ++now_ms) == 0);
    Js5ServerSessionGetStats(session, &stats);
    CHECK(context, !stats.logged_in);

    make_request(request, 0u, JS5_TEST_MASTER_ARCHIVE, JS5_TEST_MASTER_GROUP);
    CHECK(context, Js5ServerSessionFeed(session, request, 2u, ++now_ms) == 0);
    Js5ServerSessionGetStats(session, &stats);
    CHECK(context, stats.requests_received == 0u && stats.queued_normal == 0u);
    CHECK(context, Js5ServerSessionFeed(session, request + 2u, 2u, ++now_ms) == 0);
    Js5ServerSessionGetStats(session, &stats);
    CHECK(context, stats.requests_received == 1u && stats.queued_normal == 1u);
    CHECK(
        context,
        Js5ServerCacheBuildResponse(
            cache,
            JS5_TEST_MASTER_ARCHIVE,
            JS5_TEST_MASTER_GROUP,
            &expected,
            &expected_size) == JS5_SERVER_CACHE_OK);
    CHECK(context, consume_expected_response(session, expected, expected_size, 0u, &now_ms));
    CHECK(context, Js5ServerSessionPeekOutput(session, &output, &output_size) == 0);
    Js5ServerSessionGetStats(session, &stats);
    CHECK(context, stats.responses_served == 1u && stats.bytes_sent == expected_size + 1u);

cleanup:
    free(expected);
    Js5ServerSessionFree(session);
    return ok;
}

static bool
test_handshake_statuses(struct TestContext* context, struct Js5ServerCache* cache)
{
    bool ok = true;
    struct Js5ServerSession* session = NULL;
    struct Js5ServerSessionConfig config;
    struct Js5ServerSessionStats stats;
    uint8_t handshake[21];
    const uint8_t* output = NULL;
    size_t output_size = 0u;

    Js5ServerSessionConfigInit(&config);
    session = Js5ServerSessionNew(cache, &config, 0u);
    CHECK(context, session != NULL);
    make_handshake(handshake, JS5_TEST_REVISION - 1u);
    CHECK(context, Js5ServerSessionFeed(session, handshake, sizeof(handshake), 1u) == 0);
    Js5ServerSessionGetStats(session, &stats);
    CHECK(context, stats.state == JS5_SERVER_SESSION_CLOSING);
    CHECK(
        context,
        Js5ServerSessionPeekOutput(session, &output, &output_size) == 1 &&
            output_size == 1u && output[0] == 6u);
    CHECK(context, Js5ServerSessionConsumeOutput(session, 1u, 2u) == 0);
    Js5ServerSessionGetStats(session, &stats);
    CHECK(
        context,
        stats.state == JS5_SERVER_SESSION_CLOSED && stats.client_revision == 238u &&
            Js5ServerSessionWantsClose(session));
    Js5ServerSessionFree(session);
    session = NULL;

    Js5ServerSessionConfigInit(&config);
    config.server_full = true;
    session = Js5ServerSessionNew(cache, &config, 10u);
    CHECK(context, session != NULL);
    make_handshake(handshake, JS5_TEST_REVISION);
    CHECK(context, Js5ServerSessionFeed(session, handshake, 9u, 11u) == 0);
    CHECK(context, Js5ServerSessionFeed(session, handshake + 9u, 12u, 12u) == 0);
    CHECK(
        context,
        Js5ServerSessionPeekOutput(session, &output, &output_size) == 1 &&
            output_size == 1u && output[0] == 7u);
    CHECK(context, Js5ServerSessionConsumeOutput(session, 1u, 13u) == 0);
    CHECK(context, Js5ServerSessionWantsClose(session));

cleanup:
    Js5ServerSessionFree(session);
    return ok;
}

static bool
test_priority_and_duplicates(
    struct TestContext* context,
    struct Js5ServerCache* cache,
    uint16_t reference_group)
{
    bool ok = true;
    struct Js5ServerSession* session = NULL;
    struct Js5ServerSessionStats stats;
    uint8_t request[4];
    uint8_t* master = NULL;
    size_t master_size = 0u;
    uint8_t* reference = NULL;
    size_t reference_size = 0u;
    const uint8_t* output = NULL;
    size_t output_size = 0u;
    uint64_t now_ms = 1000u;

    CHECK(
        context,
        Js5ServerCacheBuildResponse(
            cache,
            JS5_TEST_MASTER_ARCHIVE,
            JS5_TEST_MASTER_GROUP,
            &master,
            &master_size) == JS5_SERVER_CACHE_OK);
    CHECK(
        context,
        Js5ServerCacheBuildResponse(
            cache, JS5_TEST_MASTER_ARCHIVE, reference_group, &reference, &reference_size) ==
            JS5_SERVER_CACHE_OK);

    session = new_active_session(cache, NULL, now_ms++);
    CHECK(context, session != NULL);
    make_request(request, 0u, JS5_TEST_MASTER_ARCHIVE, JS5_TEST_MASTER_GROUP);
    CHECK(context, Js5ServerSessionFeed(session, request, sizeof(request), now_ms++) == 0);
    make_request(request, 1u, JS5_TEST_MASTER_ARCHIVE, reference_group);
    CHECK(context, Js5ServerSessionFeed(session, request, sizeof(request), now_ms++) == 0);
    Js5ServerSessionGetStats(session, &stats);
    CHECK(
        context,
        stats.requests_received == 2u && stats.queued_normal == 1u &&
            stats.queued_urgent == 1u);
    CHECK(context, consume_expected_response(session, reference, reference_size, 0u, &now_ms));
    CHECK(context, consume_expected_response(session, master, master_size, 0u, &now_ms));
    CHECK(context, Js5ServerSessionPeekOutput(session, &output, &output_size) == 0);
    Js5ServerSessionGetStats(session, &stats);
    CHECK(
        context,
        stats.responses_served == 2u && stats.queued_normal == 0u &&
            stats.queued_urgent == 0u);
    Js5ServerSessionFree(session);
    session = NULL;

    session = new_active_session(cache, NULL, now_ms++);
    CHECK(context, session != NULL);
    make_request(request, 0u, JS5_TEST_MASTER_ARCHIVE, JS5_TEST_MASTER_GROUP);
    CHECK(context, Js5ServerSessionFeed(session, request, sizeof(request), now_ms++) == 0);
    make_request(request, 1u, JS5_TEST_MASTER_ARCHIVE, JS5_TEST_MASTER_GROUP);
    CHECK(context, Js5ServerSessionFeed(session, request, sizeof(request), now_ms++) == 0);
    CHECK(context, consume_expected_response(session, master, master_size, 0u, &now_ms));
    CHECK(context, consume_expected_response(session, master, master_size, 0u, &now_ms));
    CHECK(context, Js5ServerSessionPeekOutput(session, &output, &output_size) == 0);
    Js5ServerSessionGetStats(session, &stats);
    CHECK(
        context,
        stats.requests_received == 2u && stats.responses_served == 2u &&
            stats.error == JS5_SERVER_SESSION_ERROR_NONE);

cleanup:
    Js5ServerSessionFree(session);
    free(master);
    free(reference);
    return ok;
}

static bool
test_xor_and_backpressure(
    struct TestContext* context,
    struct Js5ServerCache* cache,
    uint16_t big_group,
    const uint8_t* expected,
    size_t expected_size)
{
    bool ok = true;
    struct Js5ServerSession* session = NULL;
    struct Js5ServerSessionStats stats;
    uint8_t control[4] = { 4u, 0xa5u, 0u, 0u };
    uint8_t request[4];
    const uint8_t* first = NULL;
    const uint8_t* repeated = NULL;
    size_t first_size = 0u;
    size_t repeated_size = 0u;
    uint64_t now_ms = 2000u;

    CHECK(context, expected_size > 512u && expected[512] == 0xffu);
    session = new_active_session(cache, NULL, now_ms++);
    CHECK(context, session != NULL);
    CHECK(context, Js5ServerSessionFeed(session, control, sizeof(control), now_ms++) == 0);
    Js5ServerSessionGetStats(session, &stats);
    CHECK(context, stats.xor_key == 0xa5u);
    make_request(request, 1u, JS5_TEST_MASTER_ARCHIVE, big_group);
    CHECK(context, Js5ServerSessionFeed(session, request, sizeof(request), now_ms++) == 0);
    CHECK(
        context,
        Js5ServerSessionPeekOutput(session, &first, &first_size) == 1 && first != NULL &&
            first_size > 512u);
    CHECK(
        context,
        first[0] == (uint8_t)(expected[0] ^ 0xa5u) &&
            first[1] == (uint8_t)(expected[1] ^ 0xa5u) &&
            first[2] == (uint8_t)(expected[2] ^ 0xa5u) &&
            first[512] == (uint8_t)(0xffu ^ 0xa5u));
    CHECK(
        context,
        Js5ServerSessionPeekOutput(session, &repeated, &repeated_size) == 1 &&
            repeated == first && repeated_size == first_size);
    CHECK(context, Js5ServerSessionConsumeOutput(session, 1u, now_ms++) == 0);
    CHECK(
        context,
        Js5ServerSessionPeekOutput(session, &repeated, &repeated_size) == 1 &&
            repeated == first + 1u && repeated_size == first_size - 1u &&
            repeated[0] == (uint8_t)(expected[1] ^ 0xa5u));
    Js5ServerSessionGetStats(session, &stats);
    CHECK(context, stats.bytes_sent == 2u && stats.responses_served == 0u);

    bool first_chunk_matches = true;
    for( size_t index = 1u; index < first_size; index++ )
    {
        if( first[index] != (uint8_t)(expected[index] ^ 0xa5u) )
        {
            first_chunk_matches = false;
            break;
        }
    }
    CHECK(context, first_chunk_matches);
    CHECK(context, Js5ServerSessionConsumeOutput(session, first_size - 1u, now_ms++) == 0);
    if( first_size < expected_size )
    {
        CHECK(
            context,
            consume_expected_response(
                session,
                expected + first_size,
                expected_size - first_size,
                0xa5u,
                &now_ms));
    }
    CHECK(context, Js5ServerSessionPeekOutput(session, &repeated, &repeated_size) == 0);
    Js5ServerSessionGetStats(session, &stats);
    CHECK(
        context,
        stats.responses_served == 1u && stats.bytes_sent == expected_size + 1u &&
            stats.error == JS5_SERVER_SESSION_ERROR_NONE);

cleanup:
    Js5ServerSessionFree(session);
    return ok;
}

static bool
test_queue_limit(struct TestContext* context, struct Js5ServerCache* cache)
{
    bool ok = true;
    struct Js5ServerSession* session = NULL;
    struct Js5ServerSessionConfig config;
    struct Js5ServerSessionStats stats;
    uint8_t request[4];

    Js5ServerSessionConfigInit(&config);
    config.max_pending_per_lane = 1u;
    session = new_active_session(cache, &config, 3000u);
    CHECK(context, session != NULL);
    make_request(request, 0u, JS5_TEST_MASTER_ARCHIVE, JS5_TEST_MASTER_GROUP);
    CHECK(context, Js5ServerSessionFeed(session, request, sizeof(request), 3001u) == 0);
    make_request(request, 1u, JS5_TEST_MASTER_ARCHIVE, JS5_TEST_MASTER_GROUP);
    CHECK(context, Js5ServerSessionFeed(session, request, sizeof(request), 3002u) == 0);
    Js5ServerSessionGetStats(session, &stats);
    CHECK(context, stats.queued_normal == 1u && stats.queued_urgent == 1u);
    make_request(request, 0u, JS5_TEST_MASTER_ARCHIVE, JS5_TEST_MASTER_GROUP);
    CHECK(context, Js5ServerSessionFeed(session, request, sizeof(request), 3003u) == -1);
    Js5ServerSessionGetStats(session, &stats);
    CHECK(
        context,
        stats.state == JS5_SERVER_SESSION_FAILED &&
            stats.error == JS5_SERVER_SESSION_ERROR_QUEUE_FULL &&
            stats.requests_received == 2u && Js5ServerSessionWantsClose(session));

cleanup:
    Js5ServerSessionFree(session);
    return ok;
}

static bool
test_protocol_errors(struct TestContext* context, struct Js5ServerCache* cache)
{
    bool ok = true;
    struct Js5ServerSession* session = NULL;
    struct Js5ServerSessionStats stats;
    uint8_t frame[4];

    session = Js5ServerSessionNew(cache, NULL, 0u);
    CHECK(context, session != NULL);
    frame[0] = 14u;
    CHECK(context, Js5ServerSessionFeed(session, frame, 1u, 1u) == -1);
    Js5ServerSessionGetStats(session, &stats);
    CHECK(context, stats.error == JS5_SERVER_SESSION_ERROR_PROTOCOL);
    Js5ServerSessionFree(session);
    session = NULL;

    session = new_active_session(cache, NULL, 10u);
    CHECK(context, session != NULL);
    make_request(frame, 9u, 0u, 0u);
    CHECK(context, Js5ServerSessionFeed(session, frame, sizeof(frame), 11u) == -1);
    Js5ServerSessionGetStats(session, &stats);
    CHECK(context, stats.error == JS5_SERVER_SESSION_ERROR_PROTOCOL);
    Js5ServerSessionFree(session);
    session = NULL;

    session = new_active_session(cache, NULL, 20u);
    CHECK(context, session != NULL);
    make_request(frame, 2u, 0u, 1u);
    CHECK(context, Js5ServerSessionFeed(session, frame, sizeof(frame), 21u) == -1);
    Js5ServerSessionGetStats(session, &stats);
    CHECK(context, stats.error == JS5_SERVER_SESSION_ERROR_PROTOCOL);
    Js5ServerSessionFree(session);
    session = NULL;

    session = new_active_session(cache, NULL, 30u);
    CHECK(context, session != NULL);
    frame[0] = 4u;
    frame[1] = 0x55u;
    frame[2] = 1u;
    frame[3] = 0u;
    CHECK(context, Js5ServerSessionFeed(session, frame, sizeof(frame), 31u) == -1);
    Js5ServerSessionGetStats(session, &stats);
    CHECK(context, stats.error == JS5_SERVER_SESSION_ERROR_PROTOCOL);

cleanup:
    Js5ServerSessionFree(session);
    return ok;
}

static bool
test_timeouts(struct TestContext* context, struct Js5ServerCache* cache)
{
    bool ok = true;
    struct Js5ServerSession* session = NULL;
    struct Js5ServerSessionConfig config;
    struct Js5ServerSessionStats stats;
    uint8_t request[4];
    const uint8_t* output = NULL;
    size_t output_size = 0u;

    Js5ServerSessionConfigInit(&config);
    config.handshake_timeout_ms = 5u;
    session = Js5ServerSessionNew(cache, &config, 100u);
    CHECK(context, session != NULL);
    Js5ServerSessionTick(session, 106u);
    Js5ServerSessionGetStats(session, &stats);
    CHECK(
        context,
        stats.state == JS5_SERVER_SESSION_FAILED &&
            stats.error == JS5_SERVER_SESSION_ERROR_TIMEOUT);
    Js5ServerSessionFree(session);
    session = NULL;

    Js5ServerSessionConfigInit(&config);
    config.idle_timeout_ms = 5u;
    session = new_active_session(cache, &config, 200u);
    CHECK(context, session != NULL);
    Js5ServerSessionTick(session, 206u);
    Js5ServerSessionGetStats(session, &stats);
    CHECK(context, stats.error == JS5_SERVER_SESSION_ERROR_TIMEOUT);
    Js5ServerSessionFree(session);
    session = NULL;

    Js5ServerSessionConfigInit(&config);
    config.output_timeout_ms = 5u;
    session = new_active_session(cache, &config, 300u);
    CHECK(context, session != NULL);
    make_request(request, 1u, JS5_TEST_MASTER_ARCHIVE, JS5_TEST_MASTER_GROUP);
    CHECK(context, Js5ServerSessionFeed(session, request, sizeof(request), 301u) == 0);
    CHECK(context, Js5ServerSessionPeekOutput(session, &output, &output_size) == 1);
    CHECK(context, output_size > 0u);
    Js5ServerSessionTick(session, 307u);
    Js5ServerSessionGetStats(session, &stats);
    CHECK(
        context,
        stats.error == JS5_SERVER_SESSION_ERROR_TIMEOUT &&
            Js5ServerSessionWantsClose(session));

cleanup:
    Js5ServerSessionFree(session);
    return ok;
}

static bool
test_missing_group(struct TestContext* context, struct Js5ServerCache* cache)
{
    bool ok = true;
    struct Js5ServerSession* session = NULL;
    struct Js5ServerSessionStats stats;
    uint8_t request[4];
    const uint8_t* output = NULL;
    size_t output_size = 0u;

    session = new_active_session(cache, NULL, 4000u);
    CHECK(context, session != NULL);
    make_request(request, 1u, 254u, 65535u);
    CHECK(context, Js5ServerSessionFeed(session, request, sizeof(request), 4001u) == 0);
    CHECK(context, Js5ServerSessionPeekOutput(session, &output, &output_size) == -1);
    Js5ServerSessionGetStats(session, &stats);
    CHECK(
        context,
        stats.state == JS5_SERVER_SESSION_FAILED &&
            stats.error == JS5_SERVER_SESSION_ERROR_CACHE_MISS && stats.cache_misses == 1u &&
            Js5ServerSessionWantsClose(session));

cleanup:
    Js5ServerSessionFree(session);
    return ok;
}

static bool
test_cache_integrity_rejection(struct TestContext* context)
{
    enum
    {
        TEST_ARCHIVE = 2,
        TEST_GROUP = 10,
    };
    const uint32_t group_version = 0x10203040u;
    bool ok = true;
    char target[1024] = { 0 };
    uint8_t* group = NULL;
    int group_size = 0;
    uint8_t* reference = NULL;
    int reference_size = 0;
    struct Js5ServerCache* cache = NULL;
    struct Js5ServerBlob blob = { 0 };

    CHECK(context, create_test_cache_directory(target, sizeof(target)));
    CHECK(context, make_integrity_group(group_version, &group, &group_size));
    CHECK(
        context,
        make_integrity_reference(
            TEST_GROUP,
            RSCache_Crc32Buffer(group, (size_t)group_size - 4u),
            group_version,
            &reference,
            &reference_size));
    CHECK(
        context,
        RSCache_Dat2DiskWriteArchive(
            target, 255, TEST_ARCHIVE, reference, reference_size) == 0);
    CHECK(
        context,
        RSCache_Dat2DiskWriteArchive(
            target, TEST_ARCHIVE, TEST_GROUP, group, group_size) == 0);
    cache = Js5ServerCacheOpen(target);
    CHECK(context, cache != NULL);
    CHECK(
        context,
        Js5ServerCacheLoad(cache, TEST_ARCHIVE, TEST_GROUP, &blob) ==
            JS5_SERVER_CACHE_OK);
    CHECK(context, blob.size == (size_t)group_size - 4u);
    Js5ServerBlobFree(&blob);
    Js5ServerCacheFree(cache);
    cache = NULL;

    /* A syntactically valid container with a stale local version is corrupt. */
    group[group_size - 1] ^= 1u;
    CHECK(
        context,
        RSCache_Dat2DiskWriteArchive(
            target, TEST_ARCHIVE, TEST_GROUP, group, group_size) == 0);
    cache = Js5ServerCacheOpen(target);
    CHECK(context, cache != NULL);
    CHECK(
        context,
        Js5ServerCacheLoad(cache, TEST_ARCHIVE, TEST_GROUP, &blob) ==
            JS5_SERVER_CACHE_ERROR);
    Js5ServerCacheFree(cache);
    cache = NULL;

    /* Restore the version and corrupt only payload, preserving valid framing. */
    group[group_size - 1] ^= 1u;
    group[5] ^= 1u;
    CHECK(
        context,
        RSCache_Dat2DiskWriteArchive(
            target, TEST_ARCHIVE, TEST_GROUP, group, group_size) == 0);
    cache = Js5ServerCacheOpen(target);
    CHECK(context, cache != NULL);
    CHECK(
        context,
        Js5ServerCacheLoad(cache, TEST_ARCHIVE, TEST_GROUP, &blob) ==
            JS5_SERVER_CACHE_ERROR);

cleanup:
    Js5ServerBlobFree(&blob);
    Js5ServerCacheFree(cache);
    free(reference);
    free(group);
    remove_test_cache_directory(target);
    return ok;
}

int
main(int argc, char** argv)
{
    struct TestContext context = { 0 };
    struct Js5ServerCache* cache = NULL;
    uint8_t* big_response = NULL;
    size_t big_response_size = 0u;
    uint16_t reference_group = 0u;
    uint16_t big_group = 0u;
    bool found_reference = false;
    bool found_big = false;

    if( argc != 2 )
    {
        fprintf(stderr, "usage: %s <canonical-cache-read-only>\n", argv[0]);
        return 2;
    }
    cache = Js5ServerCacheOpen(argv[1]);
    if( !cache )
    {
        fprintf(stderr, "js5_server_test: cannot open canonical cache %s\n", argv[1]);
        return 2;
    }

    for( uint16_t group = 0u; group < 255u; group++ )
    {
        uint8_t* response = NULL;
        size_t response_size = 0u;
        if( Js5ServerCacheBuildResponse(
                cache, JS5_TEST_MASTER_ARCHIVE, group, &response, &response_size) !=
            JS5_SERVER_CACHE_OK )
            continue;
        if( !found_reference )
        {
            reference_group = group;
            found_reference = true;
        }
        if( !found_big && response_size > 512u && response[512] == 0xffu )
        {
            big_response = response;
            big_response_size = response_size;
            big_group = group;
            found_big = true;
            response = NULL;
        }
        free(response);
        if( found_reference && found_big )
            break;
    }
    if( !found_reference || !found_big )
    {
        fprintf(stderr, "js5_server_test: canonical cache lacks required reference fixtures\n");
        free(big_response);
        Js5ServerCacheFree(cache);
        return 2;
    }

    test_cache_master(&context, cache);
    test_fragmentation_and_modes(&context, cache);
    test_handshake_statuses(&context, cache);
    test_priority_and_duplicates(&context, cache, reference_group);
    test_xor_and_backpressure(&context, cache, big_group, big_response, big_response_size);
    test_queue_limit(&context, cache);
    test_protocol_errors(&context, cache);
    test_timeouts(&context, cache);
    test_missing_group(&context, cache);
    test_cache_integrity_rejection(&context);

    free(big_response);
    Js5ServerCacheFree(cache);
    if( context.failures != 0 )
    {
        fprintf(
            stderr,
            "js5_server_test: %d of %d checks failed\n",
            context.failures,
            context.checks);
        return 1;
    }
    printf("js5_server_test: %d checks passed\n", context.checks);
    return 0;
}
