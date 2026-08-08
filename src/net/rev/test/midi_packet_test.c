/*
 * The three revision-239 music packets, encoded the way the server encodes them
 * and parsed back.
 *
 * These readers cannot be checked by eye. Every field is a two-byte quantity in
 * one of four interchangeable encodings (big-endian, little-endian, and each
 * with +128 on the low byte), the ids sit *between* the fade fields rather than
 * before them, and the whole packet is a fixed length -- so transposing two
 * fields still parses, still consumes exactly the right number of bytes, and
 * still produces plausible small integers. Nothing downstream would notice; the
 * music would just fade wrong, or play the variant instead of the song.
 *
 * So the encoders are transcribed here from RSProt's Kotlin (the same source
 * the readers were written against) and the test asserts the round trip. The
 * asymmetric values matter: id 0x1234 and fade 0x0056 would survive a
 * byte-order mistake that 0x1111 would hide.
 */

#include "net/rev/gameproto_revisions.h"
#include "net/rev/pktnames.h"
#include "net/rev/revpacket.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
osrs239_parse(
    struct GameProtoRevTable const* rev,
    int pkt_name,
    uint8_t const* data,
    int len,
    struct RevPacket* out);

/*
 * osrs239_parse delegates unhandled packet names to the rev-230 reader and
 * initialises player info elsewhere. Neither is on the path for the three music
 * prots -- they return from osrs239_parse's own switch -- so linking the whole
 * chain in would drag the entity decoder and the cache buffer along for nothing.
 *
 * These abort rather than no-op: if a future change routes one of these packets
 * through the fallback, the test must fail loudly instead of quietly measuring
 * the wrong reader.
 */
int
osrs230_parse(
    struct GameProtoRevTable const* rev,
    int pkt_name,
    uint8_t const* data,
    int len,
    struct RevPacket* out)
{
    (void)rev;
    (void)data;
    (void)len;
    (void)out;
    printf("  FAIL: packet name %d fell through to the rev-230 reader\n", pkt_name);
    abort();
}

void
osrs239_playerinfo_init(void);

void
osrs239_playerinfo_init(void)
{
    printf("  FAIL: player info init reached from a music packet\n");
    abort();
}

static int g_failures;
static int g_checks;

#define CHECK_EQ(actual, expected)                                                                 \
    do                                                                                             \
    {                                                                                              \
        long a_ = (long)(actual);                                                                  \
        long e_ = (long)(expected);                                                                \
        g_checks++;                                                                                \
        if( a_ != e_ )                                                                             \
        {                                                                                          \
            printf("  FAIL %s:%d: %s = %ld, expected %ld\n", __FILE__, __LINE__, #actual, a_, e_); \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/* The four two-byte writers, transcribed from JagexByteBufExtensions.kt. */
static uint8_t* g_w;

static void
p2(int v)
{
    *g_w++ = (uint8_t)(v >> 8);
    *g_w++ = (uint8_t)v;
}

static void
p2_alt1(int v)
{
    *g_w++ = (uint8_t)v;
    *g_w++ = (uint8_t)(v >> 8);
}

static void
p2_alt2(int v)
{
    *g_w++ = (uint8_t)(v >> 8);
    *g_w++ = (uint8_t)(v + 128);
}

static void
p2_alt3(int v)
{
    *g_w++ = (uint8_t)(v + 128);
    *g_w++ = (uint8_t)(v >> 8);
}

static void
test_midi_song_v2(void)
{
    uint8_t bytes[10];
    struct RevPacket out;

    printf("-- MIDI_SONG_V2\n");
    memset(&out, 0, sizeof(out));
    g_w = bytes;
    /* MidiSongV2Encoder */
    p2_alt1(0x0102); /* fadeInDelay  */
    p2(0x0304);      /* fadeOutDelay */
    p2_alt3(0x1234); /* id           */
    p2(7);           /* fadeInSpeed  */
    p2_alt3(9);      /* fadeOutSpeed */
    CHECK_EQ(g_w - bytes, (long)sizeof(bytes));

    CHECK_EQ(osrs239_parse(NULL, PKT_NAME_MIDI_SONG, bytes, (int)sizeof(bytes), &out), 1);
    CHECK_EQ(out._midi_song.id, 0x1234);
    CHECK_EQ(out._midi_song.fade_in_ms, 7 * 20);
    CHECK_EQ(out._midi_song.fade_out_ms, 9 * 20);
}

static void
test_midi_song_with_secondary(void)
{
    uint8_t bytes[12];
    struct RevPacket out;

    printf("-- MIDI_SONG_WITHSECONDARY\n");
    memset(&out, 0, sizeof(out));
    g_w = bytes;
    /* MidiSongWithSecondaryEncoder: note the ids are not adjacent. */
    p2(0x1234);      /* primaryId    */
    p2_alt3(0x0102); /* fadeInDelay  */
    p2_alt1(0x5678); /* secondaryId  */
    p2(11);          /* fadeOutSpeed */
    p2_alt1(0x0304); /* fadeOutDelay */
    p2_alt3(13);     /* fadeInSpeed  */
    CHECK_EQ(g_w - bytes, (long)sizeof(bytes));

    CHECK_EQ(
        osrs239_parse(NULL, PKT_NAME_MIDI_SONG_WITHSECONDARY, bytes, (int)sizeof(bytes), &out), 1);
    CHECK_EQ(out._midi_song_with_secondary.primary_id, 0x1234);
    CHECK_EQ(out._midi_song_with_secondary.secondary_id, 0x5678);
    CHECK_EQ(out._midi_song_with_secondary.fade_out_ms, 11 * 20);
    CHECK_EQ(out._midi_song_with_secondary.fade_in_ms, 13 * 20);
}

static void
test_midi_song_with_secondary_absent_ids(void)
{
    uint8_t bytes[12];
    struct RevPacket out;

    printf("-- MIDI_SONG_WITHSECONDARY, 65535 means absent\n");
    memset(&out, 0, sizeof(out));
    g_w = bytes;
    p2(65535);
    p2_alt3(0);
    p2_alt1(65535);
    p2(0);
    p2_alt1(0);
    p2_alt3(0);

    CHECK_EQ(
        osrs239_parse(NULL, PKT_NAME_MIDI_SONG_WITHSECONDARY, bytes, (int)sizeof(bytes), &out), 1);
    CHECK_EQ(out._midi_song_with_secondary.primary_id, -1);
    CHECK_EQ(out._midi_song_with_secondary.secondary_id, -1);
}

static void
test_midi_swap(void)
{
    uint8_t bytes[8];
    struct RevPacket out;

    printf("-- MIDI_SWAP\n");
    memset(&out, 0, sizeof(out));
    g_w = bytes;
    /* MidiSwapEncoder */
    p2(21);          /* fadeOutSpeed */
    p2(0x0102);      /* fadeInDelay  */
    p2_alt2(23);     /* fadeInSpeed  */
    p2(0x0304);      /* fadeOutDelay */
    CHECK_EQ(g_w - bytes, (long)sizeof(bytes));

    CHECK_EQ(osrs239_parse(NULL, PKT_NAME_MIDI_SWAP, bytes, (int)sizeof(bytes), &out), 1);
    CHECK_EQ(out._midi_swap.fade_out_ms, 21 * 20);
    CHECK_EQ(out._midi_swap.fade_in_ms, 23 * 20);
}

static void
test_short_buffers_are_rejected(void)
{
    /*
     * A reader that stops early still returns a plausible packet, and because
     * these are fixed-length prots the framer would carry on from the declared
     * length regardless -- the damage would be silent wrong fades forever after.
     *
     * Feeding one byte less than declared proves the reader actually reaches
     * the end: if it does, the buffer runs dry and the parse fails. This is the
     * check that would catch a field being dropped from the middle, which the
     * round trips above cannot, since they only assert the fields that are read.
     */
    uint8_t bytes[16] = { 0 };
    struct RevPacket out;

    printf("-- truncated payloads are rejected\n");
    memset(&out, 0, sizeof(out));
    CHECK_EQ(osrs239_parse(NULL, PKT_NAME_MIDI_SONG_WITHSECONDARY, bytes, 11, &out), 0);
    CHECK_EQ(osrs239_parse(NULL, PKT_NAME_MIDI_SWAP, bytes, 7, &out), 0);
    CHECK_EQ(osrs239_parse(NULL, PKT_NAME_MIDI_SONG, bytes, 9, &out), 0);
    /* ...and the full length is accepted, so the above is not failing for some
     * unrelated reason. */
    CHECK_EQ(osrs239_parse(NULL, PKT_NAME_MIDI_SONG_WITHSECONDARY, bytes, 12, &out), 1);
    CHECK_EQ(osrs239_parse(NULL, PKT_NAME_MIDI_SWAP, bytes, 8, &out), 1);
    CHECK_EQ(osrs239_parse(NULL, PKT_NAME_MIDI_SONG, bytes, 10, &out), 1);
}

int
main(void)
{
    test_midi_song_v2();
    test_midi_song_with_secondary();
    test_midi_song_with_secondary_absent_ids();
    test_midi_swap();
    test_short_buffers_are_rejected();

    if( g_failures )
    {
        printf("midi_packet_test: %d/%d checks FAILED\n", g_failures, g_checks);
        return 1;
    }
    printf("midi_packet_test: %d checks passed\n", g_checks);
    return 0;
}
