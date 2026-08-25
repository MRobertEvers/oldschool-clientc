/*
 * Container round-trip tests: archive, dat2 group, dat1 jagfile, ".idx" index and
 * reference table.
 *
 * Every case encodes a structure, decodes it with the *existing* decoder the
 * client uses, and compares. That direction matters: the decoders are the
 * already-verified half, so agreement with them is evidence the encoders are
 * right rather than merely self-consistent.
 */

#include "rscache_test.h"

#include <rscache.h>

#include <stdlib.h>

static void
test_archive_container(void)
{
    RSCACHE_TEST_GROUP("archive container");

    static const uint8_t payload[] =
        "A config record's worth of bytes, repeated so it compresses. "
        "A config record's worth of bytes, repeated so it compresses. "
        "A config record's worth of bytes, repeated so it compresses.";
    int payload_size = (int)sizeof(payload) - 1;

    int compressions[] = {
        RSCACHE_ARCHIVE_COMPRESSION_NONE,
        RSCACHE_ARCHIVE_COMPRESSION_BZIP2,
        RSCACHE_ARCHIVE_COMPRESSION_GZIP,
    };
    const char* names[] = { "none", "bzip2", "gzip" };

    for( size_t i = 0; i < sizeof(compressions) / sizeof(compressions[0]); i++ )
    {
        rscache_test_group = names[i];

        uint32_t bound = RSCache_ArchiveEncodeBound((uint32_t)payload_size, compressions[i]);
        uint8_t* encoded = malloc(bound);
        RSCACHE_CHECK(encoded != NULL);
        if( !encoded )
            continue;

        uint32_t size =
            RSCache_ArchiveEncode(encoded, bound, payload, (uint32_t)payload_size,
                                  compressions[i], NULL);
        RSCACHE_CHECK(size > 0);
        RSCACHE_CHECK_EQ(encoded[0], compressions[i]);

        /* The decoder takes ownership of `data` and replaces it, so hand it a
         * copy it can free. */
        struct RSCache_Dat2DiskArchive archive = { 0 };
        archive.data = malloc(size);
        memcpy(archive.data, encoded, size);
        archive.data_size = (int)size;

        RSCACHE_CHECK(RSCache_ArchiveDecryptDecompress(&archive, NULL));
        RSCACHE_CHECK_EQ(archive.data_size, payload_size);
        if( archive.data_size == payload_size )
            RSCACHE_CHECK_BYTES_EQ(archive.data, payload, payload_size);

        free(archive.data);
        free(encoded);
    }

    /* Encrypted: the same round trip has to survive XTEA in both directions. */
    {
        rscache_test_group = "xtea + gzip";
        uint32_t key[4] = { 0xDEADBEEFu, 0x12345678u, 0xCAFEBABEu, 0x0BADF00Du };

        uint32_t bound =
            RSCache_ArchiveEncodeBound((uint32_t)payload_size, RSCACHE_ARCHIVE_COMPRESSION_GZIP);
        uint8_t* encoded = malloc(bound);
        RSCACHE_CHECK(encoded != NULL);
        if( encoded )
        {
            uint32_t size = RSCache_ArchiveEncode(
                encoded, bound, payload, (uint32_t)payload_size,
                RSCACHE_ARCHIVE_COMPRESSION_GZIP, key);
            RSCACHE_CHECK(size > 0);

            struct RSCache_Dat2DiskArchive archive = { 0 };
            archive.data = malloc(size);
            memcpy(archive.data, encoded, size);
            archive.data_size = (int)size;

            RSCACHE_CHECK(RSCache_ArchiveDecryptDecompress(&archive, key));
            RSCACHE_CHECK_EQ(archive.data_size, payload_size);
            if( archive.data_size == payload_size )
                RSCACHE_CHECK_BYTES_EQ(archive.data, payload, payload_size);

            free(archive.data);
            free(encoded);
        }
    }
}

static void
check_group_roundtrip(
    const char** contents,
    int file_count,
    const char* label)
{
    rscache_test_group = label;

    struct RSCache_FileList source = { 0 };
    source.files = malloc(sizeof(char*) * (size_t)file_count);
    source.file_sizes = malloc(sizeof(int) * (size_t)file_count);
    source.file_count = file_count;
    for( int i = 0; i < file_count; i++ )
    {
        source.files[i] = (char*)contents[i];
        source.file_sizes[i] = (int)strlen(contents[i]);
    }

    uint32_t bound = RSCache_FileListEncodeBound(&source);
    uint8_t* encoded = malloc(bound ? bound : 1);
    RSCACHE_CHECK(encoded != NULL);

    uint32_t size = RSCache_FileListEncode(&source, encoded, bound);
    RSCACHE_CHECK(size > 0);

    struct RSCache_FileList* decoded =
        RSCache_FileListNewFromDecode((char*)encoded, (int)size, file_count);
    RSCACHE_CHECK(decoded != NULL);
    if( decoded )
    {
        RSCACHE_CHECK_EQ(decoded->file_count, file_count);
        for( int i = 0; i < file_count && i < decoded->file_count; i++ )
        {
            RSCACHE_CHECK_EQ(decoded->file_sizes[i], source.file_sizes[i]);
            if( decoded->file_sizes[i] == source.file_sizes[i] )
                RSCACHE_CHECK_BYTES_EQ(decoded->files[i], contents[i], source.file_sizes[i]);
        }
        RSCache_FileListFree(decoded);
    }

    free(encoded);
    free(source.files);
    free(source.file_sizes);
}

static void
test_dat2_group(void)
{
    RSCACHE_TEST_GROUP("dat2 group");

    /* A single-file group has no size table at all — the decoder special-cases
     * it, so an encoder that emitted the trailer anyway would corrupt file 0. */
    {
        static const char* one[] = { "the whole group is this one file" };
        check_group_roundtrip(one, 1, "group: single file");
    }

    {
        static const char* several[] = { "first", "second file", "third",
                                         "a considerably longer fourth file" };
        check_group_roundtrip(several, 4, "group: four files");
    }

    /* Empty members are legal and must not shift the ones after them. */
    {
        static const char* with_empty[] = { "", "after an empty", "", "another" };
        check_group_roundtrip(with_empty, 4, "group: empty members");
    }
}

static void
check_jagfile_roundtrip(
    bool whole_archive,
    const char* label)
{
    rscache_test_group = label;

    static const char* names[] = { "obj.dat", "obj.idx", "npc.dat", "seq.dat" };
    static const char* bodies[] = {
        "obj data, repeated to compress. obj data, repeated to compress.",
        "\x00\x04short index",
        "npc data with some length to it so bzip2 has something to chew on",
        "seq",
    };
    int file_count = 4;

    struct RSCache_FileListDat source = { 0 };
    source.files = malloc(sizeof(char*) * (size_t)file_count);
    source.file_sizes = malloc(sizeof(int) * (size_t)file_count);
    source.file_name_hashes = malloc(sizeof(int) * (size_t)file_count);
    source.file_count = file_count;
    for( int i = 0; i < file_count; i++ )
    {
        source.files[i] = (char*)bodies[i];
        source.file_sizes[i] = (int)strlen(bodies[i]);
        source.file_name_hashes[i] = RSCache_ArchiveNameHashDat(names[i]);
    }

    uint32_t bound = RSCache_FileListDatEncodeBound(&source);
    uint8_t* encoded = malloc(bound);
    RSCACHE_CHECK(encoded != NULL);
    if( !encoded )
        return;

    uint32_t size = RSCache_FileListDatEncode(&source, whole_archive, encoded, bound);
    RSCACHE_CHECK(size > 0);

    struct RSCache_FileListDat* decoded =
        RSCache_FileListDatNewFromDecode((char*)encoded, (int)size);
    RSCACHE_CHECK(decoded != NULL);
    if( decoded )
    {
        RSCACHE_CHECK_EQ(decoded->file_count, file_count);
        for( int i = 0; i < file_count && i < decoded->file_count; i++ )
        {
            RSCACHE_CHECK_EQ(decoded->file_name_hashes[i], source.file_name_hashes[i]);
            RSCACHE_CHECK_EQ(decoded->file_sizes[i], source.file_sizes[i]);
            if( decoded->file_sizes[i] == source.file_sizes[i] && source.file_sizes[i] > 0 )
                RSCACHE_CHECK_BYTES_EQ(decoded->files[i], bodies[i], source.file_sizes[i]);
        }

        /* Lookup by name must survive, since that is how the client fetches
         * jagfile members. */
        int found = RSCache_FileListDatFindFileByName(decoded, "npc.dat");
        RSCACHE_CHECK_EQ(found, 2);

        RSCache_FileListDatFree(decoded);
    }

    free(encoded);
    free(source.files);
    free(source.file_sizes);
    free(source.file_name_hashes);
}

static void
test_jagfile(void)
{
    RSCACHE_TEST_GROUP("dat1 jagfile");

    /* Both arrangements the format allows. There is no stored mode, so both of
     * these exercise the bzip2 encoder. */
    check_jagfile_roundtrip(true, "jagfile: whole-archive compressed");
    check_jagfile_roundtrip(false, "jagfile: per-file compressed");
}

static void
test_indexed_pair(void)
{
    RSCACHE_TEST_GROUP("dat1 .dat/.idx pair");

    /* Records of length 5, 3 and 7, so offsets run 2, 7, 10 and the .dat is 17
     * bytes long. */
    static const char data[] = "..AAAAABBBCCCCCCC";
    static const uint8_t index[] = { 0x00, 0x03, 0x00, 0x05, 0x00, 0x03, 0x00, 0x07 };

    struct RSCache_FileListDatIndexed* decoded = RSCache_FileListDatIndexedNewFromDecode(
        (char*)index, (int)sizeof(index), (char*)data, (int)sizeof(data) - 1);
    RSCACHE_CHECK(decoded != NULL);
    if( !decoded )
        return;

    RSCACHE_CHECK_EQ(decoded->offset_count, 3);
    RSCACHE_CHECK_EQ(decoded->offsets[0], 2);
    RSCACHE_CHECK_EQ(decoded->offsets[1], 7);
    RSCACHE_CHECK_EQ(decoded->offsets[2], 10);

    uint32_t bound = RSCache_FileListDatIndexedEncodeIndexBound(decoded);
    uint8_t* encoded = malloc(bound);
    RSCACHE_CHECK(encoded != NULL);
    if( encoded )
    {
        uint32_t size = RSCache_FileListDatIndexedEncodeIndex(decoded, encoded, bound);
        RSCACHE_CHECK_EQ(size, sizeof(index));
        /* Byte-exact against the original index, including the final entry, which
         * is derived from data_size rather than from an offset. */
        if( size == sizeof(index) )
            RSCACHE_CHECK_BYTES_EQ(encoded, index, (int)sizeof(index));

        /* And it must decode back to the same offsets. */
        struct RSCache_FileListDatIndexed* again = RSCache_FileListDatIndexedNewFromDecode(
            (char*)encoded, (int)size, (char*)data, (int)sizeof(data) - 1);
        RSCACHE_CHECK(again != NULL);
        if( again )
        {
            RSCACHE_CHECK_EQ(again->offset_count, decoded->offset_count);
            for( int i = 0; i < again->offset_count; i++ )
                RSCACHE_CHECK_EQ(again->offsets[i], decoded->offsets[i]);
            RSCache_FileListDatIndexedFree(again);
        }
        free(encoded);
    }

    RSCache_FileListDatIndexedFree(decoded);
}

static void
check_reftable_roundtrip(
    int format,
    int flags,
    const char* label)
{
    rscache_test_group = label;

    /* Sparse ids on purpose: reference tables are not dense, and the encoder must
     * write deltas rather than positions. */
    static const int ids[] = { 0, 1, 5, 6, 40 };
    int id_count = (int)(sizeof(ids) / sizeof(ids[0]));
    int max_id = ids[id_count - 1] + 1;

    struct RSCache_ReferenceTable source = { 0 };
    source.format = format;
    source.version = 1767694604; /* a timestamp-era revision */
    source.flags = flags;
    source.id_count = id_count;
    source.ids = malloc(sizeof(int) * (size_t)id_count);
    memcpy(source.ids, ids, sizeof(ids));
    source.archive_count = max_id;
    source.archives = calloc((size_t)max_id, sizeof(struct RSCache_ReferenceTableArchive));
    for( int i = 0; i < max_id; i++ )
        RSCache_ReferenceTableSetHasArchive(&source, i, false);

    for( int i = 0; i < id_count; i++ )
    {
        struct RSCache_ReferenceTableArchive* archive = &source.archives[ids[i]];
        RSCache_ReferenceTableSetHasArchive(&source, ids[i], true);
        RSCache_ReferenceTableSetIdentifier(&source, ids[i], 0x1000 + ids[i]);
        archive->crc = (int)(0xABCD0000u + (uint32_t)ids[i]);
        RSCache_ReferenceTableSetSizes(&source, ids[i], 100 + ids[i], 200 + ids[i]);
        archive->version = 300 + ids[i];

        /* Sparse child ids too. */
        int child_count = (i % 3) + 1;
        archive->children.count = child_count;
        archive->children.files =
            calloc((size_t)child_count, sizeof(struct RSCache_ReferenceTableArchiveFile));
        archive->children.name_hashes = calloc((size_t)child_count, sizeof(int));
        for( int j = 0; j < child_count; j++ )
        {
            archive->children.files[j].id = j * 7;
            archive->children.name_hashes[j] = 0x5000 + j;
        }

        unsigned char* digest = RSCache_ReferenceTableWhirlpoolSlot(&source, ids[i]);
        for( int b = 0; b < RSCACHE_REFTABLE_WHIRLPOOL_BYTES; b++ )
            digest[b] = (unsigned char)(ids[i] * 3 + b);
    }

    uint32_t bound = RSCache_ReferenceTableEncodeBound(&source);
    uint8_t* encoded = malloc(bound);
    RSCACHE_CHECK(encoded != NULL);

    uint32_t size = RSCache_ReferenceTableEncode(&source, encoded, bound);
    RSCACHE_CHECK(size > 0);

    struct RSCache_ReferenceTable* decoded =
        RSCache_ReferenceTableNewDecode((char*)encoded, (int)size);
    RSCACHE_CHECK(decoded != NULL);

    if( decoded )
    {
        RSCACHE_CHECK_EQ(decoded->format, format);
        RSCACHE_CHECK_EQ(decoded->flags, flags);
        RSCACHE_CHECK_EQ(decoded->id_count, id_count);
        if( format >= 6 )
            RSCACHE_CHECK_EQ(decoded->version, source.version);

        for( int i = 0; i < id_count && i < decoded->id_count; i++ )
        {
            RSCACHE_CHECK_EQ(decoded->ids[i], ids[i]);

            struct RSCache_ReferenceTableArchive* want = &source.archives[ids[i]];
            struct RSCache_ReferenceTableArchive* got = &decoded->archives[ids[i]];

            RSCACHE_CHECK_EQ(got->crc, want->crc);
            RSCACHE_CHECK_EQ(got->version, want->version);
            RSCACHE_CHECK_EQ(got->children.count, want->children.count);

            if( flags & RSCACHE_REFTABLE_FLAG_IDENTIFIERS )
                RSCACHE_CHECK_EQ(
                    RSCache_ReferenceTableIdentifier(decoded, ids[i]),
                    RSCache_ReferenceTableIdentifier(&source, ids[i]));
            if( flags & RSCACHE_REFTABLE_FLAG_SIZES )
            {
                RSCACHE_CHECK_EQ(
                    RSCache_ReferenceTableCompressed(decoded, ids[i]),
                    RSCache_ReferenceTableCompressed(&source, ids[i]));
                RSCACHE_CHECK_EQ(
                    RSCache_ReferenceTableUncompressed(decoded, ids[i]),
                    RSCache_ReferenceTableUncompressed(&source, ids[i]));
            }
            if( flags & RSCACHE_REFTABLE_FLAG_WHIRLPOOL )
                RSCACHE_CHECK_BYTES_EQ(
                    RSCache_ReferenceTableWhirlpool(decoded, ids[i]),
                    RSCache_ReferenceTableWhirlpool(&source, ids[i]),
                    64);

            for( int j = 0; j < want->children.count && j < got->children.count; j++ )
            {
                /* Through the accessor on both sides: a run that came out
                 * 0,1,2,... decodes with no array at all. */
                RSCACHE_CHECK_EQ(
                    RSCache_ReferenceTableChildId(got, j),
                    RSCache_ReferenceTableChildId(want, j));
                if( flags & RSCACHE_REFTABLE_FLAG_IDENTIFIERS )
                    RSCACHE_CHECK_EQ(
                        RSCache_ReferenceTableChildNameHash(got, j),
                        RSCache_ReferenceTableChildNameHash(want, j));
            }
        }

        /* Re-encoding the decoded table must reproduce the same bytes — the real
         * test of symmetry, since it exercises decode and encode against each
         * other with no hand-written expectation in between. */
        uint32_t rebound = RSCache_ReferenceTableEncodeBound(decoded);
        uint8_t* again = malloc(rebound);
        if( again )
        {
            uint32_t again_size = RSCache_ReferenceTableEncode(decoded, again, rebound);
            RSCACHE_CHECK_EQ(again_size, size);
            if( again_size == size )
                RSCACHE_CHECK_BYTES_EQ(again, encoded, (int)size);
            free(again);
        }

        RSCache_ReferenceTableFree(decoded);
    }

    free(encoded);
    for( int i = 0; i < id_count; i++ )
    {
        free(source.archives[ids[i]].children.files);
        free(source.archives[ids[i]].children.name_hashes);
    }
    free(source.whirlpools);
    free(source.archives);
    free(source.ids);
}

static void
test_reference_table(void)
{
    RSCACHE_TEST_GROUP("reference table");

    /* The format/flag combinations the local caches actually use, from a probe
     * across all seven: modern OSRS is format 7 with flags 4 or 5, cache.643 is
     * format 6 with 0 or 1, kronos mixes formats 5 and 6. */
    check_reftable_roundtrip(7, RSCACHE_REFTABLE_FLAG_SIZES, "reftable: fmt7 flags=4");
    check_reftable_roundtrip(
        7,
        RSCACHE_REFTABLE_FLAG_SIZES | RSCACHE_REFTABLE_FLAG_IDENTIFIERS,
        "reftable: fmt7 flags=5");
    check_reftable_roundtrip(6, 0, "reftable: fmt6 flags=0");
    check_reftable_roundtrip(6, RSCACHE_REFTABLE_FLAG_IDENTIFIERS, "reftable: fmt6 flags=1");
    check_reftable_roundtrip(5, 0, "reftable: fmt5 flags=0");
    check_reftable_roundtrip(5, RSCACHE_REFTABLE_FLAG_IDENTIFIERS, "reftable: fmt5 flags=1");

    /* No shipped cache sets the whirlpool flag, but the encoder and decoder must
     * still agree on where the digests sit — between the CRCs and the sizes. */
    check_reftable_roundtrip(
        7,
        RSCACHE_REFTABLE_FLAG_SIZES | RSCACHE_REFTABLE_FLAG_WHIRLPOOL,
        "reftable: fmt7 whirlpool");

    /* Format 5 has no version field; the decoder must not invent one. */
    {
        rscache_test_group = "reftable: fmt5 has no version";
        struct RSCache_ReferenceTable table = { 0 };
        table.format = 5;
        table.version = 12345;
        table.flags = 0;
        table.id_count = 1;
        int one_id = 0;
        table.ids = &one_id;
        table.archive_count = 1;
        struct RSCache_ReferenceTableArchive archive = { 0 };
        table.archives = &archive;
        RSCache_ReferenceTableSetHasArchive(&table, 0, true);

        uint8_t buffer[256];
        uint32_t size = RSCache_ReferenceTableEncode(&table, buffer, sizeof(buffer));
        RSCACHE_CHECK(size > 0);

        struct RSCache_ReferenceTable* decoded =
            RSCache_ReferenceTableNewDecode((char*)buffer, (int)size);
        RSCACHE_CHECK(decoded != NULL);
        if( decoded )
        {
            RSCACHE_CHECK_EQ(decoded->version, 0);
            RSCache_ReferenceTableFree(decoded);
        }
    }

    /* An out-of-range format is refused rather than producing a table nothing can
     * read back. */
    {
        rscache_test_group = "reftable: bad format refused";
        struct RSCache_ReferenceTable table = { 0 };
        table.format = 4;
        int one_id = 0;
        table.ids = &one_id;
        table.id_count = 1;
        struct RSCache_ReferenceTableArchive archive = { 0 };
        table.archives = &archive;
        table.archive_count = 1;

        uint8_t buffer[64];
        RSCACHE_CHECK_EQ(RSCache_ReferenceTableEncode(&table, buffer, sizeof(buffer)), 0);
    }
}

static void
test_disk_writer(void)
{
    RSCACHE_TEST_GROUP("dat2 disk writer");

    /* Build a cache on disk and read it back through the same load path the
     * client uses. This is the end-to-end check: sector chaining, the idx record,
     * the reference table and the archive container all have to agree, and none of
     * them is verified by the in-memory tests above. */
    const char* directory = getenv("RSCACHE_TEST_TMPDIR");
    char fallback[] = "/tmp/rscache_test_cache";
    if( !directory )
        directory = fallback;

    char command[1024];
    snprintf(command, sizeof(command), "rm -rf '%s' && mkdir -p '%s'", directory, directory);
    if( system(command) != 0 )
    {
        printf("   SKIP could not prepare %s\n", directory);
        return;
    }

    /* Two archives in the configs table, one small enough for a single sector and
     * one that must span several — the sector chain is where a writer most easily
     * goes wrong. */
    uint8_t small_payload[64];
    for( size_t i = 0; i < sizeof(small_payload); i++ )
        small_payload[i] = (uint8_t)(i * 3 + 1);

    uint8_t large_payload[5000];
    uint32_t state = 4242u;
    for( size_t i = 0; i < sizeof(large_payload); i++ )
    {
        state = state * 1103515245u + 12345u;
        large_payload[i] = (uint8_t)(state >> 16);
    }

    struct
    {
        int archive_id;
        const uint8_t* data;
        int size;
        int compression;
    } cases[] = {
        { 3, small_payload, (int)sizeof(small_payload), RSCACHE_ARCHIVE_COMPRESSION_NONE },
        { 7, large_payload, (int)sizeof(large_payload), RSCACHE_ARCHIVE_COMPRESSION_GZIP },
        { 9, large_payload, (int)sizeof(large_payload), RSCACHE_ARCHIVE_COMPRESSION_BZIP2 },
    };

    int table_id = RSCACHE_DAT2_OSRS_TABLE_CONFIGS;

    for( size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++ )
    {
        uint32_t bound =
            RSCache_ArchiveEncodeBound((uint32_t)cases[i].size, cases[i].compression);
        uint8_t* container = malloc(bound);
        RSCACHE_CHECK(container != NULL);
        if( !container )
            continue;

        uint32_t container_size = RSCache_ArchiveEncode(
            container, bound, cases[i].data, (uint32_t)cases[i].size, cases[i].compression, NULL);
        RSCACHE_CHECK(container_size > 0);

        int result = RSCache_Dat2DiskWriteArchive(
            directory, table_id, cases[i].archive_id, container, (int)container_size);
        RSCACHE_CHECK_EQ(result, 0);
        free(container);
    }

    /* A reference table so the archives have metadata, written into table 255
     * exactly like any other archive. */
    {
        struct RSCache_ReferenceTable table = { 0 };
        int ids[] = { 3, 7, 9 };
        table.format = 7;
        table.version = 1767694604;
        table.flags = RSCACHE_REFTABLE_FLAG_SIZES;
        table.id_count = 3;
        table.ids = ids;
        table.archive_count = 10;
        table.archives = calloc(10, sizeof(struct RSCache_ReferenceTableArchive));
        for( int i = 0; i < 10; i++ )
            RSCache_ReferenceTableSetHasArchive(&table, i, false);
        for( int i = 0; i < 3; i++ )
        {
            RSCache_ReferenceTableSetHasArchive(&table, ids[i], true);
            table.archives[ids[i]].version = 5;
            table.archives[ids[i]].children.count = 1;
            table.archives[ids[i]].children.files =
                calloc(1, sizeof(struct RSCache_ReferenceTableArchiveFile));
        }

        uint32_t bound = RSCache_ReferenceTableEncodeBound(&table);
        uint8_t* raw = malloc(bound);
        uint32_t raw_size = RSCache_ReferenceTableEncode(&table, raw, bound);
        RSCACHE_CHECK(raw_size > 0);

        uint32_t cbound = RSCache_ArchiveEncodeBound(raw_size, RSCACHE_ARCHIVE_COMPRESSION_GZIP);
        uint8_t* container = malloc(cbound);
        uint32_t container_size = RSCache_ArchiveEncode(
            container, cbound, raw, raw_size, RSCACHE_ARCHIVE_COMPRESSION_GZIP, NULL);
        RSCACHE_CHECK(container_size > 0);

        RSCACHE_CHECK_EQ(
            RSCache_Dat2DiskWriteArchive(
                directory, RSCACHE_DAT2_DISK_REFERENCE_TABLE_ID, table_id, container,
                (int)container_size),
            0);

        free(container);
        free(raw);
        for( int i = 0; i < 3; i++ )
            free(table.archives[ids[i]].children.files);
        free(table.archives);
    }

    /* Now read it all back the ordinary way. */
    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewFromDirectory(directory);
    RSCACHE_CHECK(disk != NULL);
    if( disk )
    {
        for( size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++ )
        {
            struct RSCache_Dat2DiskArchive* archive =
                RSCache_Dat2DiskArchiveNewLoad(disk, table_id, cases[i].archive_id);
            RSCACHE_CHECK(archive != NULL);
            if( !archive )
                continue;

            RSCACHE_CHECK_EQ(archive->data_size, cases[i].size);
            if( archive->data_size == cases[i].size )
                RSCACHE_CHECK_BYTES_EQ(archive->data, cases[i].data, cases[i].size);

            RSCache_Dat2DiskArchiveFree(archive);
        }

        /* The reference table must have loaded during open, and carry what was
         * written into it. */
        struct RSCache_ReferenceTable* loaded = disk->tables[table_id];
        RSCACHE_CHECK(loaded != NULL);
        if( loaded )
        {
            RSCACHE_CHECK_EQ(loaded->format, 7);
            RSCACHE_CHECK_EQ(loaded->version, 1767694604);
            RSCACHE_CHECK_EQ(loaded->id_count, 3);
        }

        RSCache_Dat2DiskFree(disk);
    }

    snprintf(command, sizeof(command), "rm -rf '%s'", directory);
    (void)system(command);
}

static void
check_terrain_roundtrip(
    int flags,
    const char* label)
{
    rscache_test_group = label;

    /*
     * Build a terrain stream by hand rather than reading one off disk: map archives
     * are XTEA-encrypted, and a synthetic square can cover the cases that matter —
     * bare tiles, overlays, settings, underlays, an authored height, and crucially an
     * authored height of *zero*, which is the case the fixup would otherwise destroy.
     */
    uint8_t source[1 << 20];
    struct RSCache_Buffer writer;
    RSCache_BufferInit(&writer, source, sizeof(source));

    bool u16 = !(flags & RSCACHE_MAP_TERRAIN_DECODE_U8);
    int tile_index = 0;

    for( int level = 0; level < RSCACHE_MAP_TERRAIN_LEVELS; level++ )
    {
        for( int x = 0; x < RSCACHE_MAP_TERRAIN_X; x++ )
        {
            for( int z = 0; z < RSCACHE_MAP_TERRAIN_Z; z++ )
            {
                int variant = tile_index % 6;
                tile_index++;

                switch( variant )
                {
                case 0:
                    /* Bare tile: terminator only. */
                    break;
                case 1:
                    /* Overlay: opcode 2..49 then the overlay id. */
                    if( u16 )
                    {
                        p2(&writer, 6);
                        p2(&writer, 300);
                    }
                    else
                    {
                        p1(&writer, 6);
                        p1(&writer, 40);
                    }
                    break;
                case 2:
                    /* Settings: opcode 50..81. */
                    if( u16 )
                        p2(&writer, 49 + 5);
                    else
                        p1(&writer, 49 + 5);
                    break;
                case 3:
                    /* Underlay: opcode 82+. */
                    if( u16 )
                        p2(&writer, 81 + 7);
                    else
                        p1(&writer, 81 + 7);
                    break;
                case 4:
                    /* Authored height, non-zero. */
                    if( u16 )
                        p2(&writer, 1);
                    else
                        p1(&writer, 1);
                    p1(&writer, 42);
                    continue; /* opcode 1 terminates the tile */
                default:
                    /* Authored height of ZERO. Indistinguishable from "absent" if
                     * only `height` is consulted, because the fixup overwrites a
                     * zero height. This is the case that requires provenance. */
                    if( u16 )
                        p2(&writer, 1);
                    else
                        p1(&writer, 1);
                    p1(&writer, 0);
                    continue;
                }

                if( u16 )
                    p2(&writer, 0);
                else
                    p1(&writer, 0);
            }
        }
    }

    int source_size = (int)writer.position;

    struct RSCache_MapTerrain* decoded = RSCache_MapTerrainNewFromDecodeFlags(
        (char*)source, source_size, 50, 50, flags);
    RSCACHE_CHECK(decoded != NULL);
    if( !decoded )
        return;

    /* The fixup flag must actually take effect. */
    bool expect_fixed = !(flags & RSCACHE_MAP_TERRAIN_DECODE_NO_FIXUP);
    RSCACHE_CHECK_EQ(decoded->is_fixedup, expect_fixed);

    uint32_t bound = RSCache_MapTerrainEncodeBound(flags);
    uint8_t* encoded = malloc(bound);
    RSCACHE_CHECK(encoded != NULL);
    if( !encoded )
    {
        free(decoded);
        return;
    }

    uint32_t written = RSCache_MapTerrainEncode(decoded, flags, encoded, bound);
    RSCACHE_CHECK(written > 0);

    /* Byte-exact: this synthetic source lists attributes in the same canonical order
     * the encoder writes, so nothing should differ. */
    RSCACHE_CHECK_EQ(written, (uint32_t)source_size);
    if( written == (uint32_t)source_size )
        RSCACHE_CHECK_BYTES_EQ(encoded, source, source_size);

    /* And it must decode back to the same tiles. */
    struct RSCache_MapTerrain* again =
        RSCache_MapTerrainNewFromDecodeFlags((char*)encoded, (int)written, 50, 50, flags);
    RSCACHE_CHECK(again != NULL);
    if( again )
    {
        bool equal = true;
        int authored_zero_seen = 0;
        for( int i = 0; i < RSCACHE_CHUNK_TILE_COUNT; i++ )
        {
            const struct RSCache_MapFloor* want = &decoded->tiles_xyz[i];
            const struct RSCache_MapFloor* got = &again->tiles_xyz[i];

            if( want->height_authored && want->authored_height == 0 )
                authored_zero_seen++;

            if( want->overlay_id != got->overlay_id || want->underlay_id != got->underlay_id ||
                want->attr_opcode != got->attr_opcode || want->settings != got->settings ||
                want->shape != got->shape || want->rotation != got->rotation ||
                want->height != got->height ||
                want->height_authored != got->height_authored ||
                want->authored_height != got->authored_height )
            {
                equal = false;
                break;
            }
        }
        RSCACHE_CHECK(equal);
        /* The authored-zero case has to be present, or the test is not covering the
         * situation that motivated recording provenance at all. */
        RSCACHE_CHECK(authored_zero_seen > 0);
        free(again);
    }

    free(encoded);
    free(decoded);
}

static void
test_map_terrain(void)
{
    RSCACHE_TEST_GROUP("map terrain");

    /* Both widths, and both with and without the fixup — the encoder has to be
     * indifferent to whether the terrain it is handed was transformed. */
    check_terrain_roundtrip(RSCACHE_MAP_TERRAIN_DECODE_U16, "terrain: u16, fixed up");
    check_terrain_roundtrip(RSCACHE_MAP_TERRAIN_DECODE_U8, "terrain: u8, fixed up");
    check_terrain_roundtrip(
        RSCACHE_MAP_TERRAIN_DECODE_U16 | RSCACHE_MAP_TERRAIN_DECODE_NO_FIXUP,
        "terrain: u16, raw");
    check_terrain_roundtrip(
        RSCACHE_MAP_TERRAIN_DECODE_U8 | RSCACHE_MAP_TERRAIN_DECODE_NO_FIXUP,
        "terrain: u8, raw");
}

int
main(void)
{
    printf("container tests\n");
    test_map_terrain();
    test_archive_container();
    test_dat2_group();
    test_jagfile();
    test_indexed_pair();
    test_reference_table();
    test_disk_writer();
    return rscache_test_report("container");
}
