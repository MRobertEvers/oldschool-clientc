/*
 * The server pack container (rscache_serverpack.h): the framing every archive in
 * `server/pack` carries, the name tables, and the client-record archives the game
 * server reads its records from. Written by cachepack and read by the server
 * through this one implementation.
 */

#include "rscache.h"
#include "rscache_test.h"

#include "archive.h"
#include "dat2disk.h"
#include "rscache_serverpack.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main(void)
{
    RSCACHE_TEST_GROUP("framing: a payload round-trips and names its kind");
    {
        static const uint8_t payload[] = { 1, 2, 3, 0 };
        uint8_t framed[64];
        const uint8_t* got = NULL;
        uint32_t got_size = 0;
        uint32_t size =
            RSCache_ServerPackFrame(RSCACHE_SERVERPACK_KIND_BAND, payload, 4, framed, sizeof(framed));

        RSCACHE_CHECK_EQ((int)size, 4 + RSCACHE_SERVERPACK_HEADER);
        RSCACHE_CHECK_EQ(framed[2], RSCACHE_SERVERPACK_VERSION);
        RSCACHE_CHECK(RSCache_ServerPackUnframe(framed, size, RSCACHE_SERVERPACK_KIND_BAND, &got,
                                                &got_size));
        RSCACHE_CHECK_EQ((int)got_size, 4);
        RSCACHE_CHECK_BYTES_EQ(got, payload, 4);

        /* The kind is part of the contract: a band is not a record archive. */
        RSCACHE_CHECK(!RSCache_ServerPackUnframe(framed, size, RSCACHE_SERVERPACK_KIND_RECORDS,
                                                 &got, &got_size));
        /* A flipped payload byte fails the CRC. */
        framed[RSCACHE_SERVERPACK_HEADER + 1] ^= 0x40;
        RSCACHE_CHECK(!RSCache_ServerPackUnframe(framed, size, RSCACHE_SERVERPACK_KIND_BAND, &got,
                                                 &got_size));
        framed[RSCACHE_SERVERPACK_HEADER + 1] ^= 0x40;
        /* A version this build did not write is refused, never decoded. */
        framed[2] = RSCACHE_SERVERPACK_VERSION - 1;
        RSCACHE_CHECK(!RSCache_ServerPackUnframe(framed, size, RSCACHE_SERVERPACK_KIND_BAND, &got,
                                                 &got_size));
        framed[2] = RSCACHE_SERVERPACK_VERSION;
        RSCACHE_CHECK(!RSCache_ServerPackUnframe(framed, RSCACHE_SERVERPACK_HEADER - 1,
                                                 RSCACHE_SERVERPACK_KIND_BAND, &got, &got_size));
    }

    RSCACHE_TEST_GROUP("names: sparse ids, terminated strings, a truncation refused");
    {
        static const int ids[] = { 0, 5, 131 };
        static const char* const names[] = { "attack", "prayer", "bones" };
        uint8_t table[64];
        int got_ids[4];
        const char* got_names[4];
        uint32_t size = RSCache_ServerPackNamesEncode(ids, names, 3, table, sizeof(table));

        RSCACHE_CHECK(size > 0);
        RSCACHE_CHECK_EQ(RSCache_ServerPackNamesDecode(table, size, got_ids, got_names, 4), 3);
        RSCACHE_CHECK_EQ(got_ids[2], 131);
        RSCACHE_CHECK_STR_EQ(got_names[2], "bones");
        RSCACHE_CHECK_EQ(RSCache_ServerPackNamesDecode(table, size - 1, got_ids, got_names, 4), -1);
        RSCACHE_CHECK_EQ((int)RSCache_ServerPackNamesEncode(ids, names, 3, table, 4), 0);
    }

    RSCACHE_TEST_GROUP("records: grouping, round trip, empty bodies, malformed payloads");
    {
        static const uint8_t a[] = { 10, 11 };
        static const uint8_t c[] = { 30, 31, 32 };
        const int ids[] = { 256, 257, 511 };
        const uint8_t* bodies[] = { a, NULL, c };
        const uint32_t sizes[] = { 2, 0, 3 };
        uint8_t payload[64];
        struct RSCache_ServerPackRecords cursor;
        int id = -1;
        const uint8_t* body = NULL;
        uint32_t body_size = 0;
        uint32_t size;

        RSCACHE_CHECK_EQ(RSCache_ServerPackRecordsGroup(9), 9 + 64);
        RSCACHE_CHECK_EQ(RSCache_ServerPackRecordsArchive(255), 0);
        RSCACHE_CHECK_EQ(RSCache_ServerPackRecordsArchive(256), 1);

        size = RSCache_ServerPackRecordsEncode(ids, bodies, sizes, 3, payload, sizeof(payload));
        RSCACHE_CHECK_EQ((int)size, (int)RSCache_ServerPackRecordsBound(3, 5));

        RSCACHE_CHECK(RSCache_ServerPackRecordsBegin(&cursor, payload, size));
        RSCACHE_CHECK_EQ(RSCache_ServerPackRecordsNext(&cursor, &id, &body, &body_size), 1);
        RSCACHE_CHECK_EQ(id, 256);
        RSCACHE_CHECK_EQ((int)body_size, 2);
        RSCACHE_CHECK_BYTES_EQ(body, a, 2);
        /* A record whose client encoding is empty is still a record. */
        RSCACHE_CHECK_EQ(RSCache_ServerPackRecordsNext(&cursor, &id, &body, &body_size), 1);
        RSCACHE_CHECK_EQ(id, 257);
        RSCACHE_CHECK_EQ((int)body_size, 0);
        RSCACHE_CHECK_EQ(RSCache_ServerPackRecordsNext(&cursor, &id, &body, &body_size), 1);
        RSCACHE_CHECK_EQ(id, 511);
        RSCACHE_CHECK_BYTES_EQ(body, c, 3);
        RSCACHE_CHECK_EQ(RSCache_ServerPackRecordsNext(&cursor, &id, &body, &body_size), 0);

        /* Cut short: the last body runs past the end. */
        RSCACHE_CHECK(RSCache_ServerPackRecordsBegin(&cursor, payload, size - 1));
        RSCACHE_CHECK_EQ(RSCache_ServerPackRecordsNext(&cursor, &id, &body, &body_size), 1);
        RSCACHE_CHECK_EQ(RSCache_ServerPackRecordsNext(&cursor, &id, &body, &body_size), 1);
        RSCACHE_CHECK_EQ(RSCache_ServerPackRecordsNext(&cursor, &id, &body, &body_size), -1);

        /* Trailing bytes past the stated count are not a payload the writer made. */
        payload[size] = 0;
        RSCACHE_CHECK(RSCache_ServerPackRecordsBegin(&cursor, payload, size + 1));
        while( RSCache_ServerPackRecordsNext(&cursor, &id, &body, &body_size) == 1 )
            ;
        RSCACHE_CHECK_EQ(RSCache_ServerPackRecordsNext(&cursor, &id, &body, &body_size), -1);

        /* Ids that do not ascend: the first id rewritten to 600, above the second. */
        {
            uint8_t swapped[64];

            memcpy(swapped, payload, size);
            swapped[2] = 0;
            swapped[3] = 0;
            swapped[4] = 0x02;
            swapped[5] = 0x58;
            RSCACHE_CHECK(RSCache_ServerPackRecordsBegin(&cursor, swapped, size));
            RSCACHE_CHECK_EQ(RSCache_ServerPackRecordsNext(&cursor, &id, &body, &body_size), 1);
            RSCACHE_CHECK_EQ(RSCache_ServerPackRecordsNext(&cursor, &id, &body, &body_size), -1);
        }
    }

    RSCACHE_TEST_GROUP("directory: written archives read back; gaps are absent, damage invalid");
    {
        char dir[] = "/tmp/rscache_serverpack_XXXXXX";
        static const uint8_t body[] = { 7, 7, 7 };
        const int ids[] = { 300 };
        const uint8_t* bodies[] = { body };
        const uint32_t sizes[] = { 3 };
        uint8_t payload[64];
        uint8_t framed[128];
        uint8_t container[256];
        uint32_t payload_size;
        uint32_t framed_size;
        uint32_t container_size;
        struct RSCache_ServerPack pack;
        void* owned = NULL;
        const uint8_t* got = NULL;
        uint32_t got_size = 0;
        int group = RSCache_ServerPackRecordsGroup(9);

        RSCACHE_CHECK(mkdtemp(dir) != NULL);
        payload_size =
            RSCache_ServerPackRecordsEncode(ids, bodies, sizes, 1, payload, sizeof(payload));
        framed_size = RSCache_ServerPackFrame(RSCACHE_SERVERPACK_KIND_RECORDS, payload,
                                              payload_size, framed, sizeof(framed));
        container_size = RSCache_ArchiveEncode(container, sizeof(container), framed, framed_size,
                                               RSCACHE_ARCHIVE_COMPRESSION_GZIP, NULL);
        RSCACHE_CHECK(container_size > 0);
        RSCACHE_CHECK_EQ(
            RSCache_Dat2DiskWriteArchive(dir, group, 1, container, (int)container_size), 0);
        RSCache_Dat2DiskWriteFlush();

        RSCACHE_CHECK(RSCache_ServerPackOpen(&pack, dir));
        RSCACHE_CHECK_EQ(RSCache_ServerPackEntryCount(&pack, group), 2);
        RSCACHE_CHECK_EQ(RSCache_ServerPackRead(&pack, group, 1, RSCACHE_SERVERPACK_KIND_RECORDS,
                                                &owned, &got, &got_size),
                         1);
        RSCACHE_CHECK_EQ((int)got_size, (int)payload_size);
        RSCACHE_CHECK_BYTES_EQ(got, payload, payload_size);
        free(owned);
        /* Archive 0 is the zero-filled gap below the one written. */
        RSCACHE_CHECK_EQ(RSCache_ServerPackRead(&pack, group, 0, RSCACHE_SERVERPACK_KIND_RECORDS,
                                                &owned, &got, &got_size),
                         RSCACHE_SERVERPACK_ABSENT);
        RSCACHE_CHECK(owned == NULL);
        /* Asked for as the wrong kind: invalid, not misread. */
        RSCACHE_CHECK_EQ(RSCache_ServerPackRead(&pack, group, 1, RSCACHE_SERVERPACK_KIND_BAND,
                                                &owned, &got, &got_size),
                         RSCACHE_SERVERPACK_INVALID);
        /* A group with no idx at all. */
        RSCACHE_CHECK_EQ(RSCache_ServerPackEntryCount(&pack, 200), 0);
        RSCache_ServerPackClose(&pack);

        {
            struct RSCache_ServerPack none;
            char missing[256];

            snprintf(missing, sizeof(missing), "%s/nope", dir);
            RSCACHE_CHECK(!RSCache_ServerPackOpen(&none, missing));
            RSCache_ServerPackClose(&none);
        }
        {
            char path[512];

            snprintf(path, sizeof(path), "%s/main_file_cache.dat2", dir);
            unlink(path);
            snprintf(path, sizeof(path), "%s/main_file_cache.idx%d", dir, group);
            unlink(path);
            rmdir(dir);
        }
    }

    return rscache_test_report("serverpack");
}
