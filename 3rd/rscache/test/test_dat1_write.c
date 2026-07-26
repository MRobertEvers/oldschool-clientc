/*
 * Dat1 write path: NPC encode round-trip and Dat1DiskWriteArchive sector round-trip.
 */
#include "rscache_test.h"

#include <rscache.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
prepare_directory(const char* directory)
{
    char command[1024];
    snprintf(command, sizeof(command), "rm -rf '%s' && mkdir -p '%s'", directory, directory);
    return system(command);
}

static void
cleanup_directory(const char* directory)
{
    char command[1024];
    snprintf(command, sizeof(command), "rm -rf '%s'", directory);
    (void)system(command);
}

static void
test_npc_encode_decode_roundtrip(void)
{
    RSCACHE_TEST_GROUP("dat1 npc encode/decode");

    struct RSCache_Dat1ConfigNpc npc;
    memset(&npc, 0, sizeof(npc));
    npc.size = 1;
    npc.readyanim = -1;
    npc.walkanim = -1;
    npc.walkanim_b = -1;
    npc.walkanim_r = -1;
    npc.walkanim_l = -1;
    npc.minimap = true;
    npc.vislevel = -1;
    npc.resizeh = 128;
    npc.resizev = 128;
    npc.resizex = -1;
    npc.resizey = -1;
    npc.resizez = -1;
    npc.headicon = -1;
    npc.turnspeed = 32;

    npc.name = (char*)"Guard";
    npc.desc = (char*)"He looks tough.";
    npc.size = 1;
    int models[] = { 100, 101 };
    npc.models = models;
    npc.models_count = 2;
    npc.readyanim = 808;
    npc.walkanim = 819;
    npc.op[0] = (char*)"Talk-to";
    npc.vislevel = 21;
    npc.resizeh = 140;

    uint32_t bound = RSCache_Dat1ConfigNpcEncodeBound(&npc);
    uint8_t* encoded = malloc(bound);
    RSCACHE_CHECK(encoded != NULL);
    if( !encoded )
        return;

    uint32_t size = RSCache_Dat1ConfigNpcEncode(&npc, encoded, bound);
    RSCACHE_CHECK(size > 0);
    RSCACHE_CHECK(size <= bound);

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, encoded, size);
    struct RSCache_Dat1ConfigNpc* decoded = RSCache_Dat1ConfigNpcDecodeOne(&buffer);
    RSCACHE_CHECK(decoded != NULL);
    if( decoded )
    {
        RSCACHE_CHECK_STR_EQ(decoded->name, "Guard");
        RSCACHE_CHECK_STR_EQ(decoded->desc, "He looks tough.");
        RSCACHE_CHECK_EQ(decoded->models_count, 2);
        RSCACHE_CHECK_EQ(decoded->models[0], 100);
        RSCACHE_CHECK_EQ(decoded->models[1], 101);
        RSCACHE_CHECK_EQ(decoded->readyanim, 808);
        RSCACHE_CHECK_EQ(decoded->walkanim, 819);
        RSCACHE_CHECK_STR_EQ(decoded->op[0], "Talk-to");
        RSCACHE_CHECK_EQ(decoded->vislevel, 21);
        RSCACHE_CHECK_EQ(decoded->resizeh, 140);
        RSCACHE_CHECK_EQ(decoded->size, 1);
        RSCACHE_CHECK_EQ(decoded->turnspeed, 32);
        RSCache_Dat1ConfigNpcFree(decoded);
    }

    /* Byte-exact: encode again from decoded defaults should match if we re-decode. */
    free(encoded);
}

static void
test_seq_encode_decode_roundtrip(void)
{
    RSCACHE_TEST_GROUP("dat1 seq encode/decode");

    struct RSCache_Dat1ConfigSeq seq;
    memset(&seq, 0, sizeof(seq));
    seq.loops = -1;
    seq.priority = 5;
    seq.replaceheldleft = -1;
    seq.replaceheldright = -1;
    seq.maxloops = 99;
    seq.preanim_move = -1;
    seq.postanim_move = -1;
    seq.duplicate_behavior = -1;

    int frames[] = { 0x1234 };
    int iframes[] = { -1 };
    int delay[] = { 5 };
    seq.frame_count = 1;
    seq.frames = frames;
    seq.iframes = iframes;
    seq.delay = delay;
    seq.stretches = true;
    int walkmerge[] = { 1, 2, 9999999 };
    seq.walkmerge = walkmerge;

    uint32_t bound = RSCache_Dat1ConfigSeqEncodeBound(&seq);
    uint8_t* encoded = malloc(bound);
    RSCACHE_CHECK(encoded != NULL);
    if( !encoded )
        return;

    uint32_t size = RSCache_Dat1ConfigSeqEncode(&seq, encoded, bound);
    RSCACHE_CHECK(size > 0);

    struct RSCache_Dat1ConfigSeq decoded;
    int consumed =
        RSCache_Dat1ConfigSeqDecodeInplace(&decoded, (char*)encoded, (int)size);
    RSCACHE_CHECK(consumed == (int)size);
    RSCACHE_CHECK_EQ(decoded.frame_count, 1);
    RSCACHE_CHECK_EQ(decoded.frames[0], 0x1234);
    RSCACHE_CHECK_EQ(decoded.iframes[0], -1);
    RSCACHE_CHECK_EQ(decoded.delay[0], 5);
    RSCACHE_CHECK(decoded.stretches);
    RSCACHE_CHECK(decoded.walkmerge != NULL);
    RSCACHE_CHECK_EQ(decoded.walkmerge[0], 1);
    RSCACHE_CHECK_EQ(decoded.walkmerge[1], 2);
    RSCACHE_CHECK_EQ(decoded.walkmerge[2], 9999999);

    RSCache_Dat1ConfigSeqFreeInplace(&decoded);
    free(encoded);
}

static void
test_dat1_disk_write_archive_roundtrip(void)
{
    RSCACHE_TEST_GROUP("dat1 disk write archive");

    const char* directory = getenv("RSCACHE_TEST_TMPDIR");
    char fallback[] = "/tmp/rscache_test_dat1_write";
    if( !directory )
        directory = fallback;

    if( prepare_directory(directory) != 0 )
    {
        printf("   SKIP could not prepare %s\n", directory);
        return;
    }

    static const uint8_t payload[] = "hello-dat1-archive-payload";
    int table_id = RSCACHE_DAT1_DISK_TABLE_CONFIGS;
    int archive_id = 2;

    RSCACHE_CHECK(
        RSCache_Dat1DiskWriteArchive(
            directory, table_id, archive_id, payload, (int)sizeof(payload)) == 0);

    /* Read back through the same sector path Dat1DiskArchiveNewLoad uses. */
    char path[1024];
    snprintf(path, sizeof(path), "%s/main_file_cache.dat", directory);
    FILE* dat_file = fopen(path, "rb");
    RSCACHE_CHECK(dat_file != NULL);
    if( !dat_file )
    {
        cleanup_directory(directory);
        return;
    }

    snprintf(path, sizeof(path), "%s/main_file_cache.idx%d", directory, table_id);
    FILE* index_file = fopen(path, "rb");
    RSCACHE_CHECK(index_file != NULL);
    if( !index_file )
    {
        fclose(dat_file);
        cleanup_directory(directory);
        return;
    }

    struct RSCache_Dat2DiskIndexRecord record = { 0 };
    RSCACHE_CHECK(RSCache_Dat2DiskIndexFileReadRecord(index_file, archive_id, &record) == 0);
    fclose(index_file);

    RSCACHE_CHECK_EQ(record.length, (int)sizeof(payload));
    RSCACHE_CHECK(record.sector > 0);

    struct RSCache_Dat2DiskArchive raw = { 0 };
    int rc = RSCache_Dat2DiskDatFileReadArchive(
        dat_file, table_id, archive_id, record.sector, record.length, &raw);
    fclose(dat_file);
    RSCACHE_CHECK(rc == 0);
    if( rc == 0 )
    {
        RSCACHE_CHECK_EQ(raw.data_size, (int)sizeof(payload));
        RSCACHE_CHECK_BYTES_EQ(raw.data, payload, sizeof(payload));
        free(raw.data);
    }

    cleanup_directory(directory);
}

static void
test_anim_base_frames_encode_decode(void)
{
    RSCACHE_TEST_GROUP("dat1 anim encode/decode");

    struct RSCache_Dat1AnimBase base = { 0 };
    uint8_t types[] = { 0, 1 };
    uint8_t label0[] = { 0 };
    uint8_t label1[] = { 1 };
    uint8_t* labels[] = { label0, label1 };
    uint16_t label_counts[] = { 1, 1 };
    base.length = 2;
    base.types = types;
    base.labels = labels;
    base.label_counts = label_counts;

    struct RSCache_Dat1AnimFrame frame = { 0 };
    int16_t groups[] = { 1 };
    int16_t xs[] = { 10 };
    int16_t ys[] = { 0 };
    int16_t zs[] = { -5 };
    uint8_t bone_flags[] = { 0, 5 }; /* bone1: x|z */
    bool synthesized[] = { false };
    frame.id = 42;
    frame.base = &base;
    frame.length = 1;
    frame.groups = groups;
    frame.x = xs;
    frame.y = ys;
    frame.z = zs;
    frame.delay = 3;
    frame.flag_count = 2;
    frame.bone_flags = bone_flags;
    frame.synthesized = synthesized;

    struct RSCache_Dat1AnimBaseFrames abf = { 0 };
    abf.base = &base;
    abf.frames = &frame;
    abf.frame_count = 1;

    uint32_t bound = RSCache_Dat1AnimBaseFramesEncodeBound(&abf);
    uint8_t* encoded = malloc(bound);
    RSCACHE_CHECK(encoded != NULL);
    if( !encoded )
        return;

    uint32_t size = RSCache_Dat1AnimBaseFramesEncode(&abf, encoded, bound);
    RSCACHE_CHECK(size > 0);

    struct RSCache_Dat1AnimBaseFrames* decoded =
        RSCache_Dat1AnimBaseFramesNewDecode((char*)encoded, (int)size);
    RSCACHE_CHECK(decoded != NULL);
    if( decoded )
    {
        RSCACHE_CHECK_EQ(decoded->frame_count, 1);
        RSCACHE_CHECK_EQ(decoded->frames[0].id, 42);
        RSCACHE_CHECK_EQ(decoded->frames[0].delay, 3);
        RSCACHE_CHECK_EQ(decoded->base->length, 2);
        RSCACHE_CHECK(decoded->frames[0].length >= 1);
        RSCACHE_CHECK_EQ(decoded->frames[0].flag_count, 2);
        RSCache_Dat1AnimBaseFramesFree(decoded);
    }

    free(encoded);
}

int
main(void)
{
    test_npc_encode_decode_roundtrip();
    test_seq_encode_decode_roundtrip();
    test_dat1_disk_write_archive_roundtrip();
    test_anim_base_frames_encode_decode();

    printf(
        "\n%d checks, %d failures\n", rscache_test_checks, rscache_test_failures);
    return rscache_test_failures == 0 ? 0 : 1;
}
