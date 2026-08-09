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

/* The one-byte writers SoundAreaEncoder uses. */
static void
p1(int v)
{
    *g_w++ = (uint8_t)v;
}

static void
p1_alt1(int v)
{
    *g_w++ = (uint8_t)(v + 128);
}

static void
p1_alt2(int v)
{
    *g_w++ = (uint8_t)(0 - v);
}

static void
p1_alt3(int v)
{
    *g_w++ = (uint8_t)(128 - v);
}

/*
 * SOUND_AREA, the positional sound.
 *
 * Worth its own case for two reasons beyond the usual byte-order paranoia.
 * Every field here is a small integer in one of four one-byte encodings, three
 * of which *negate* or offset the value, so a field read with the wrong one
 * still yields a small plausible number -- a radius of 6 read as alt2 is 250,
 * which is not obviously wrong anywhere downstream, it just makes a door
 * audible across the map.
 *
 * And it is the sub-packet whose absence used to `break` the enclosed-zone
 * loop, discarding every later sub-packet in the batch. The second half of this
 * test is that batch: a SOUND_AREA followed by a MAP_ANIM, asserting the
 * MAP_ANIM still arrives.
 */
static void
test_sound_area(void)
{
    uint8_t bytes[7];
    struct RevPacket out;

    printf("-- SOUND_AREA\n");
    memset(&out, 0, sizeof(out));
    g_w = bytes;
    /* SoundAreaEncoder v19 (rev 239). */
    p1_alt3(0x35); /* coordInZone: dx 3, dz 5 */
    p1_alt2(2);    /* dropOffRange (inner)    */
    p1(6);         /* range (radius)          */
    p1_alt2(9);    /* delay                   */
    p1_alt1(3);    /* loops                   */
    p2(0x1234);    /* id                      */
    CHECK_EQ(g_w - bytes, (long)sizeof(bytes));

    CHECK_EQ(osrs239_parse(NULL, PKT_NAME_SOUND_AREA, bytes, (int)sizeof(bytes), &out), 1);
    CHECK_EQ(out._sound_area.pos, 0x35);
    CHECK_EQ(out._sound_area.inner, 2);
    CHECK_EQ(out._sound_area.radius, 6);
    CHECK_EQ(out._sound_area.delay, 9);
    CHECK_EQ(out._sound_area.loops, 3);
    CHECK_EQ(out._sound_area.id, 0x1234);
}

static void
test_sound_area_does_not_truncate_a_zone_batch(void)
{
    /*
     * UPDATE_ZONE_PARTIAL_ENCLOSED: base x, base z, then (ordinal, payload)*.
     * Ordinal 14 is SOUND_AREA, 5 is MAP_ANIM. Before SOUND_AREA was mapped,
     * the unknown ordinal made the loop break and the MAP_ANIM after it was
     * never decoded -- so this asserts the *second* sub-packet, not the first.
     */
    uint8_t bytes[64];
    struct RevPacket out;
    int len;

    printf("-- a SOUND_AREA does not truncate the zone batch after it\n");
    memset(&out, 0, sizeof(out));
    g_w = bytes;
    p1(40); /* base x */
    p1(50); /* base z */

    p1(14);        /* ordinal: SOUND_AREA */
    p1_alt3(0x35);
    p1_alt2(2);
    p1(6);
    p1_alt2(9);
    p1_alt1(3);
    p2(0x1234);

    p1(5); /* ordinal: MAP_ANIM */
    {
        /* MapAnimEncoder v19: p1Alt2 delay, p2Alt2 id, p1Alt1 height,
         * p1Alt2 coordInZone -- transcribed from the generated codec. */
        uint8_t* start = g_w;
        p1_alt2(11);    /* delay  */
        p2_alt2(0x0777); /* id     */
        p1_alt1(4);      /* height */
        p1_alt2(0x21);   /* coordInZone */
        CHECK_EQ(g_w - start, 5);
    }

    len = (int)(g_w - bytes);
    CHECK_EQ(
        osrs239_parse(NULL, PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED, bytes, len, &out), 1);
    CHECK_EQ(out._update_zone_enclosed.count, 2);
    if( out._update_zone_enclosed.count == 2 )
    {
        CHECK_EQ(out._update_zone_enclosed.entries[0].name, PKT_NAME_SOUND_AREA);
        CHECK_EQ(out._update_zone_enclosed.entries[0]._sound_area.id, 0x1234);
        CHECK_EQ(out._update_zone_enclosed.entries[1].name, PKT_NAME_MAP_ANIM);
        CHECK_EQ(out._update_zone_enclosed.entries[1]._map_anim.id, 0x0777);
    }
    if( out._update_zone_enclosed.entries )
        free(out._update_zone_enclosed.entries);
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
    test_sound_area();
    test_sound_area_does_not_truncate_a_zone_batch();
    test_short_buffers_are_rejected();

    if( g_failures )
    {
        printf("midi_packet_test: %d/%d checks FAILED\n", g_failures, g_checks);
        return 1;
    }
    printf("midi_packet_test: %d checks passed\n", g_checks);
    return 0;
}
