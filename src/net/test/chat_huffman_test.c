/*
 * Chat text: OldSchool's Huffman (net/chat_huffman.h) and LostCity's wordpack
 * byte wrappers, as the revision tables hand them out.
 *
 * The compiled-in table is checked against the cache's own file, because that
 * file is what a real client builds its codec from: a table that drifted from
 * it would still round-trip through itself perfectly and turn every recorded
 * line into noise. Run:
 *   make -C src test-chat-huffman
 */

#include "mem/torirs_mem.h"
#include "net/chat_huffman.h"
#include "net/wordpack.h"

#include <rscache.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int Failures = 0;

#define CHECK(cond, what)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if( cond )                                                                                 \
            printf("  ok:   %s\n", what);                                                          \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s\n", what);                                                          \
            Failures++;                                                                            \
        }                                                                                          \
    } while( 0 )

static int
dat2_archive_by_name(
    struct RSCache_ReferenceTable* table,
    char const* name)
{
    char buf[64];
    int name_hash;

    snprintf(buf, sizeof(buf), "%s", name);
    name_hash = RSCache_ArchiveNameHashDat2(buf);
    for( int i = 0; i < table->archive_count; i++ )
    {
        if( RSCache_ReferenceTableIdentifier(table, i) == name_hash )
            return i;
    }
    return -1;
}

static void
test_table_is_the_caches(void)
{
    struct RSCache_Dat2Disk* disk;
    struct RSCache_ReferenceTable* table;
    struct RSCache_Dat2DiskArchive* archive;
    int archive_id;

    printf("the compiled-in table is cache.osrs241's \"huffman\"\n");
    disk = RSCache_Dat2DiskNewFromDirectory("../cache.osrs241");
    if( !disk )
    {
        printf("   SKIP cache.osrs241: not present\n");
        return;
    }
    table = RSCache_Dat2DiskReferenceTable(disk, RSCACHE_DAT2_TABLE_BINARY);
    CHECK(table != NULL, "cache.osrs241 has a binary table");
    if( !table )
    {
        RSCache_Dat2DiskFree(disk);
        return;
    }
    archive_id = dat2_archive_by_name(table, "huffman");
    CHECK(archive_id >= 0, "the binary table names \"huffman\"");
    if( archive_id >= 0 )
    {
        archive = RSCache_Dat2DiskArchiveNewLoad(disk, RSCACHE_DAT2_TABLE_BINARY, archive_id);
        CHECK(archive != NULL, "\"huffman\" loads");
        if( archive )
        {
            CHECK(archive->data_size == 256, "\"huffman\" is 256 bytes");
            CHECK(
                archive->data_size == 256 &&
                    memcmp(archive->data, g_chat_huffman_osrs, 256) == 0,
                "\"huffman\" equals g_chat_huffman_osrs");
            RSCache_Dat2DiskArchiveFree(archive);
        }
    }
    RSCache_Dat2DiskFree(disk);
}

static int
huffman_round_trips(char const* text)
{
    /* 22 bits for the rarest bytes: a 255-byte line needs ~700. */
    uint8_t packed[1024];
    int len = chat_huffman_pack(packed, (int)sizeof(packed), text);
    char* back = chat_huffman_unpack(packed, len);
    int const same = strcmp(back, text) == 0;

    ToriMem_Free(ToriMem_Net(), back);
    return same;
}

static void
test_huffman(void)
{
    uint8_t packed[1024];
    char every[256];
    char* text;
    int len;

    printf("huffman\n");
    CHECK(huffman_round_trips("Hello world!"), "a line round-trips, case kept");
    CHECK(huffman_round_trips("Selling lobbies 250ea @ GE, pm me :)"), "punctuation round-trips");
    {
        int first_bad = -1;

        for( int c = 1; c < 256 && first_bad < 0; c++ )
        {
            char one[3] = { (char)c, 'a', '\0' };
            if( !huffman_round_trips(one) )
                first_bad = c;
        }
        if( first_bad >= 0 )
            printf("   byte %d does not round-trip\n", first_bad);
        CHECK(first_bad < 0, "every byte 1..255 round-trips on its own (each has a code)");
    }
    for( int i = 0; i < 255; i++ )
        every[i] = (char)(i + 1);
    every[255] = '\0';
    CHECK(huffman_round_trips(every), "all 255 in one line round-trip");

    len = chat_huffman_pack(packed, (int)sizeof(packed), "hello");
    CHECK(len > 1 && packed[0] == 5, "the byte count leads, pSmart1or2");
    len = chat_huffman_pack(packed, (int)sizeof(packed), every);
    CHECK(len > 2 && packed[0] == (0x80 | (255 >> 8)) && packed[1] == 255,
          "a count past 127 takes two bytes");

    /* What the deob's catch makes of bits that run out. */
    len = chat_huffman_pack(packed, (int)sizeof(packed), "a longer line of chat");
    text = chat_huffman_unpack(packed, len - 2);
    CHECK(strcmp(text, "Cabbage") == 0, "a cut-short line reads \"Cabbage\"");
    ToriMem_Free(ToriMem_Net(), text);
    text = chat_huffman_unpack(packed, 0);
    CHECK(strcmp(text, "Cabbage") == 0, "an empty tail reads \"Cabbage\"");
    ToriMem_Free(ToriMem_Net(), text);

    CHECK(chat_huffman_says("hi"), "a short line is said");
    CHECK(!chat_huffman_says(""), "an empty line is not");
    memset(every, 'a', 81);
    every[81] = '\0';
    CHECK(!chat_huffman_says(every), "81 characters is past the chatbox's limit");
}

/* Two lines the recorder typed in RSProx session 4262 (rev 239, tick 18025):
 * their MESSAGE_PUBLIC bodies, as the client wrote them, carry exactly these
 * bytes between the effect and the clan type. */
static void
test_recorded_lines(void)
{
    static uint8_t const ohhh[] = { 0x04, 0xc8, 0x42, 0x00 };
    static uint8_t const drakan[] = {
        0x26, 0x9a, 0x08, 0xa9, 0xed, 0xe2, 0x82, 0xf3, 0xdc, 0x7b, 0x08, 0xb6,
        0xb4, 0xd9, 0x1e, 0x56, 0x87, 0x4d, 0xac, 0x55, 0x54, 0x5c, 0x96,
    };
    uint8_t packed[64];
    char* text;
    int len;

    printf("lines from a recording\n");
    len = chat_huffman_pack(packed, (int)sizeof(packed), "ohhh");
    CHECK(len == (int)sizeof(ohhh) && memcmp(packed, ohhh, sizeof(ohhh)) == 0,
          "\"ohhh\" packs to the client's bytes");
    text = chat_huffman_unpack(ohhh, (int)sizeof(ohhh));
    CHECK(strcmp(text, "ohhh") == 0, "and the client's bytes read \"ohhh\"");
    ToriMem_Free(ToriMem_Net(), text);
    len = chat_huffman_pack(packed, (int)sizeof(packed), "im just about to begin fighting drakan");
    CHECK(len == (int)sizeof(drakan) && memcmp(packed, drakan, sizeof(drakan)) == 0,
          "a 38-character line packs to the client's bytes");
    text = chat_huffman_unpack(drakan, (int)sizeof(drakan));
    CHECK(strcmp(text, "im just about to begin fighting drakan") == 0,
          "and the client's bytes read back");
    ToriMem_Free(ToriMem_Net(), text);
}

static void
test_wordpack_bytes(void)
{
    uint8_t packed[256];
    char* text;
    int len;

    printf("wordpack bytes (LostCity)\n");
    len = wordpack_pack_bytes(packed, (int)sizeof(packed), "hello mate");
    text = wordpack_unpack_bytes(packed, len);
    /* wordpack is lower-case on the wire; the reader sentence-cases. A line
     * that ends on half a byte gains the pad nibble as a trailing space, which
     * is wordpack's own reading. */
    if( strncmp(text, "Hello mate", 10) != 0 )
        printf("   wordpack gave \"%s\"\n", text);
    CHECK(strncmp(text, "Hello mate", 10) == 0, "a line round-trips through wordpack");
    ToriMem_Free(ToriMem_Net(), text);
}

int
main(void)
{
    ToriMem_Init();
    test_table_is_the_caches();
    test_huffman();
    test_recorded_lines();
    test_wordpack_bytes();
    if( Failures > 0 )
    {
        printf("FAILED: %d\n", Failures);
        return 1;
    }
    printf("PASSED\n");
    return 0;
}
