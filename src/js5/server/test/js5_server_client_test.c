#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "js5/js5.h"
#include "js5/js5_rscache.h"
#include "platform/sockstream.h"

#include <dat2disk.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#include <unistd.h>
#endif

static uint64_t
now_ms(void)
{
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec now;
    if( clock_gettime(CLOCK_MONOTONIC, &now) != 0 )
        return 0u;
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
#endif
}

static void
delay_one_ms(void)
{
#ifdef _WIN32
    Sleep(1u);
#else
    struct timespec delay = { 0, 1000000L };
    nanosleep(&delay, NULL);
#endif
}

static bool
create_target(char* target, size_t capacity)
{
#ifdef _WIN32
    char root[MAX_PATH];
    char placeholder[MAX_PATH];
    DWORD length = GetTempPathA((DWORD)sizeof(root), root);
    if( length == 0u || length >= sizeof(root) ||
        !GetTempFileNameA(root, "jsc", 0u, placeholder) || !DeleteFileA(placeholder) ||
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
    const char pattern[] = "/tmp/oldschool-js5-network-XXXXXX";
    if( sizeof(pattern) > capacity )
        return false;
    memcpy(target, pattern, sizeof(pattern));
    return mkdtemp(target) != NULL;
#endif
}

static void
remove_target(const char* target)
{
    static const char* names[] = {
        "main_file_cache.dat2",
        "main_file_cache.idx2",
        "main_file_cache.idx255",
    };
    char path[1024];
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

int
main(int argc, char** argv)
{
    char target[1024] = { 0 };
    struct RSCache_Dat2Disk* disk = NULL;
    struct Js5RscacheStorage storage;
    struct Js5Client* client = NULL;
    struct Js5Config config;
    struct Js5StorageOps operations;
    struct RSCache_Dat2DiskArchive* persisted = NULL;
    int result = 1;
    int port;

    memset(&storage, 0, sizeof(storage));
    if( argc != 2 )
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return 2;
    }
    port = atoi(argv[1]);
    if( port <= 0 || port > 65535 || !create_target(target, sizeof(target)) )
        goto cleanup;
    disk = RSCache_Dat2DiskNewSparseFromDirectory(target);
    if( !disk || !Js5RscacheStorageInit(&storage, disk, target) || sockstream_init() != 0 )
        goto cleanup;

    Js5ConfigInit(&config);
    config.host = "127.0.0.1";
    config.primary_port = (uint16_t)port;
    config.fallback_port = (uint16_t)port;
    config.revision = JS5_REVISION_239;
    config.reconnect_delay_ms = 0u;
    config.local_scan_budget = 64u;
    config.max_response_bytes = 4u * 1024u * 1024u;
    Js5ArchiveSelectionSet(&config.archives, 2, true, false, false);
    operations = Js5RscacheStorageOperations();
    client = Js5ClientNew(&config, NULL, NULL, &operations, &storage);
    if( !client ||
        Js5ClientRequestGroup(client, 2, 10, JS5_PRIORITY_URGENT) != JS5_REQUEST_DEFERRED )
        goto cleanup;

    uint64_t deadline = now_ms() + 20000u;
    while( now_ms() < deadline )
    {
        if( Js5ClientTick(client, now_ms()) < 0 )
            goto cleanup;
        if( Js5ClientMetadataReady(client) && Js5ClientGroupReady(client, 2, 10) )
            break;
        delay_one_ms();
    }
    if( !Js5ClientMetadataReady(client) || !Js5ClientGroupReady(client, 2, 10) )
        goto cleanup;
    persisted = RSCache_Dat2DiskArchiveNewLoadRaw(disk, 2, 10);
    if( !persisted || persisted->data_size <= 5 )
        goto cleanup;

    printf(
        "js5_server_client: PASS (metadata primed, group 2/10 persisted, %d bytes)\n",
        persisted->data_size);
    result = 0;

cleanup:
    RSCache_Dat2DiskArchiveFree(persisted);
    Js5ClientFree(client);
    Js5RscacheStorageClear(&storage);
    RSCache_Dat2DiskFree(disk);
    sockstream_cleanup();
    if( target[0] )
        remove_target(target);
    if( result != 0 )
        fprintf(stderr, "js5_server_client: FAIL\n");
    return result;
}
