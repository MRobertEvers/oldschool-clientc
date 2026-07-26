/*
 * rsbuffer unit tests.
 *
 * Two things are checked for every codec:
 *
 *  1. **Round-trip** — g(p(v)) == v across the boundaries of each encoding's
 *     representable range.
 *  2. **Width and bytes** — p emits exactly the byte sequence the reader's
 *     width discriminator expects. This is where the bugs actually live: a
 *     smart-encoding writer that picks the wrong width still round-trips
 *     through its own reader in isolation, but corrupts every field after it in
 *     a real record.
 */

#include "rscache_test.h"

#include <rscache.h>

#include <stdlib.h>

static void
rewind_for_read(struct RSCache_Buffer* buffer)
{
    /* An owned buffer's `position` is the written length; `size` is only the
     * allocation. Clamp size to the payload so the readers' end-of-buffer
     * guards fire at the real end rather than into uninitialised slack. */
    buffer->size = buffer->position;
    buffer->position = 0;
}

static void
test_fixed_width(void)
{
    RSCACHE_TEST_GROUP("fixed width p/g pairs");

    static const int u8_cases[] = { 0, 1, 127, 128, 254, 255 };
    for( size_t i = 0; i < sizeof(u8_cases) / sizeof(u8_cases[0]); i++ )
    {
        struct RSCache_Buffer buf;
        RSCache_BufferInitAlloc(&buf, 0);
        p1(&buf, u8_cases[i]);
        RSCACHE_CHECK_EQ(RSCache_BufferLength(&buf), 1);
        rewind_for_read(&buf);
        RSCACHE_CHECK_EQ(g1(&buf), u8_cases[i]);
        RSCache_BufferRelease(&buf);
    }

    static const int s8_cases[] = { -128, -1, 0, 1, 127 };
    for( size_t i = 0; i < sizeof(s8_cases) / sizeof(s8_cases[0]); i++ )
    {
        struct RSCache_Buffer buf;
        RSCache_BufferInitAlloc(&buf, 0);
        p1b(&buf, s8_cases[i]);
        RSCACHE_CHECK_EQ(RSCache_BufferLength(&buf), 1);
        rewind_for_read(&buf);
        RSCACHE_CHECK_EQ(g1b(&buf), s8_cases[i]);
        RSCache_BufferRelease(&buf);
    }

    static const int u16_cases[] = { 0, 1, 255, 256, 32767, 32768, 65535 };
    for( size_t i = 0; i < sizeof(u16_cases) / sizeof(u16_cases[0]); i++ )
    {
        struct RSCache_Buffer buf;
        RSCache_BufferInitAlloc(&buf, 0);
        p2(&buf, u16_cases[i]);
        RSCACHE_CHECK_EQ(RSCache_BufferLength(&buf), 2);
        rewind_for_read(&buf);
        RSCACHE_CHECK_EQ(g2(&buf), u16_cases[i]);
        RSCache_BufferRelease(&buf);
    }

    static const int s16_cases[] = { -32768, -1, 0, 1, 32767 };
    for( size_t i = 0; i < sizeof(s16_cases) / sizeof(s16_cases[0]); i++ )
    {
        struct RSCache_Buffer buf;
        RSCache_BufferInitAlloc(&buf, 0);
        p2b(&buf, s16_cases[i]);
        RSCACHE_CHECK_EQ(RSCache_BufferLength(&buf), 2);
        rewind_for_read(&buf);
        RSCACHE_CHECK_EQ(g2b(&buf), s16_cases[i]);
        RSCache_BufferRelease(&buf);
    }

    static const int u24_cases[] = { 0, 1, 65535, 65536, 0xFFFFFF };
    for( size_t i = 0; i < sizeof(u24_cases) / sizeof(u24_cases[0]); i++ )
    {
        struct RSCache_Buffer buf;
        RSCache_BufferInitAlloc(&buf, 0);
        p3(&buf, u24_cases[i]);
        RSCACHE_CHECK_EQ(RSCache_BufferLength(&buf), 3);
        rewind_for_read(&buf);
        RSCACHE_CHECK_EQ(g3(&buf), u24_cases[i]);
        RSCache_BufferRelease(&buf);
    }

    static const int s32_cases[] = { -2147483647 - 1, -1, 0, 1, 2147483647 };
    for( size_t i = 0; i < sizeof(s32_cases) / sizeof(s32_cases[0]); i++ )
    {
        struct RSCache_Buffer buf;
        RSCache_BufferInitAlloc(&buf, 0);
        p4(&buf, s32_cases[i]);
        RSCACHE_CHECK_EQ(RSCache_BufferLength(&buf), 4);
        rewind_for_read(&buf);
        RSCACHE_CHECK_EQ(g4(&buf), s32_cases[i]);
        RSCache_BufferRelease(&buf);
    }

    static const int64_t s64_cases[] = { INT64_MIN, -1, 0, 1, INT64_MAX };
    for( size_t i = 0; i < sizeof(s64_cases) / sizeof(s64_cases[0]); i++ )
    {
        struct RSCache_Buffer buf;
        RSCache_BufferInitAlloc(&buf, 0);
        p8(&buf, s64_cases[i]);
        RSCACHE_CHECK_EQ(RSCache_BufferLength(&buf), 8);
        rewind_for_read(&buf);
        RSCACHE_CHECK_EQ(g8(&buf), s64_cases[i]);
        RSCache_BufferRelease(&buf);
    }

    /* Big-endian byte order, spelled out. */
    {
        struct RSCache_Buffer buf;
        RSCache_BufferInitAlloc(&buf, 0);
        p4(&buf, 0x01020304);
        static const unsigned char want[] = { 0x01, 0x02, 0x03, 0x04 };
        RSCACHE_CHECK_BYTES_EQ(buf.data, want, 4);
        RSCache_BufferRelease(&buf);
    }
}

static void
test_float(void)
{
    RSCACHE_TEST_GROUP("float");

    static const float cases[] = { 0.0f, 1.0f, -1.0f, 0.5f, 3.14159f, -1234.5f };
    for( size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++ )
    {
        struct RSCache_Buffer buf;
        RSCache_BufferInitAlloc(&buf, 0);
        pf(&buf, cases[i]);
        RSCACHE_CHECK_EQ(RSCache_BufferLength(&buf), 4);
        rewind_for_read(&buf);
        float got = gf(&buf);
        /* Bit-exact: the codec is a memcpy of the IEEE-754 pattern, so any
         * difference at all is a byte-order or width bug, not rounding. */
        RSCACHE_CHECK(memcmp(&got, &cases[i], sizeof(float)) == 0);
        RSCache_BufferRelease(&buf);
    }
}

static void
test_usmart(void)
{
    RSCACHE_TEST_GROUP("usmart (u16 / u31)");

    struct
    {
        int value;
        uint32_t width;
    } cases[] = {
        { 0, 2 }, { 1, 2 }, { 0x7FFE, 2 }, { 0x7FFF, 2 },
        /* 0x8000 cannot use the 2-byte form: its high byte would set the
         * discriminator bit and be read back as a 4-byte value. */
        { 0x8000, 4 },
        { 0xFFFF, 4 },
        { 0x7FFFFFFF, 4 },
    };

    for( size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++ )
    {
        struct RSCache_Buffer buf;
        RSCache_BufferInitAlloc(&buf, 0);
        pusmart(&buf, cases[i].value);
        RSCACHE_CHECK_EQ(RSCache_BufferLength(&buf), cases[i].width);
        rewind_for_read(&buf);
        RSCACHE_CHECK_EQ(gusmart(&buf), cases[i].value);
        RSCache_BufferRelease(&buf);
    }
}

static void
test_bigsmart(void)
{
    RSCACHE_TEST_GROUP("bigsmart (u16 with -1 escape / u31)");

    struct
    {
        int value;
        uint32_t width;
    } cases[] = {
        { -1, 2 },   /* the reserved 2-byte 32767 */
        { 0, 2 },   { 1, 2 },     { 32766, 2 },
        /* 32767 must NOT use the 2-byte form — that encoding means -1. */
        { 32767, 4 },
        { 32768, 4 }, { 0x7FFFFFFF, 4 },
    };

    for( size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++ )
    {
        struct RSCache_Buffer buf;
        RSCache_BufferInitAlloc(&buf, 0);
        pbigsmart(&buf, cases[i].value);
        RSCACHE_CHECK_EQ(RSCache_BufferLength(&buf), cases[i].width);
        rewind_for_read(&buf);
        RSCACHE_CHECK_EQ(gbigsmart(&buf), cases[i].value);
        RSCache_BufferRelease(&buf);
    }

    /* -1 is exactly the two bytes 0x7F 0xFF. */
    {
        struct RSCache_Buffer buf;
        RSCache_BufferInitAlloc(&buf, 0);
        pbigsmart(&buf, -1);
        static const unsigned char want[] = { 0x7F, 0xFF };
        RSCACHE_CHECK_BYTES_EQ(buf.data, want, 2);
        RSCache_BufferRelease(&buf);
    }
}

static void
test_shortsmart(void)
{
    RSCACHE_TEST_GROUP("shortsmart (signed) / ushortsmart");

    struct
    {
        int value;
        uint32_t width;
    } signed_cases[] = {
        { -64, 1 }, { -1, 1 }, { 0, 1 },     { 63, 1 },
        { -65, 2 }, { 64, 2 }, { -16384, 2 }, { 16383, 2 },
    };
    for( size_t i = 0; i < sizeof(signed_cases) / sizeof(signed_cases[0]); i++ )
    {
        struct RSCache_Buffer buf;
        RSCache_BufferInitAlloc(&buf, 0);
        pshortsmart(&buf, signed_cases[i].value);
        RSCACHE_CHECK_EQ(RSCache_BufferLength(&buf), signed_cases[i].width);
        rewind_for_read(&buf);
        RSCACHE_CHECK_EQ(gshortsmart(&buf), signed_cases[i].value);
        RSCache_BufferRelease(&buf);
    }

    struct
    {
        int value;
        uint32_t width;
    } unsigned_cases[] = {
        { 0, 1 }, { 1, 1 }, { 127, 1 }, { 128, 2 }, { 255, 2 }, { 32767, 2 },
    };
    for( size_t i = 0; i < sizeof(unsigned_cases) / sizeof(unsigned_cases[0]); i++ )
    {
        struct RSCache_Buffer buf;
        RSCache_BufferInitAlloc(&buf, 0);
        pushortsmart(&buf, unsigned_cases[i].value);
        RSCACHE_CHECK_EQ(RSCache_BufferLength(&buf), unsigned_cases[i].width);
        rewind_for_read(&buf);
        RSCACHE_CHECK_EQ(gushortsmart(&buf), unsigned_cases[i].value);
        RSCache_BufferRelease(&buf);
    }

    /* Raw-cursor variant must agree with the buffer variant byte for byte. */
    for( size_t i = 0; i < sizeof(signed_cases) / sizeof(signed_cases[0]); i++ )
    {
        uint8_t raw[4] = { 0 };
        int write_off = 0;
        pshortsmartat(raw, &write_off, signed_cases[i].value);
        RSCACHE_CHECK_EQ(write_off, (int)signed_cases[i].width);

        int read_off = 0;
        RSCACHE_CHECK_EQ(gshortsmartat(raw, &read_off), signed_cases[i].value);
        RSCACHE_CHECK_EQ(read_off, write_off);
    }
}

static void
test_uintsmartshortcompat(void)
{
    RSCACHE_TEST_GROUP("uintsmartshortcompat (32767-escape runs)");

    static const int cases[] = { 0, 1, 127, 128, 32766, 32767, 32768, 65533, 65534, 100000 };
    for( size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++ )
    {
        struct RSCache_Buffer buf;
        RSCache_BufferInitAlloc(&buf, 0);
        puintsmartshortcompat(&buf, cases[i]);
        rewind_for_read(&buf);
        RSCACHE_CHECK_EQ(guintsmartshortcompat(&buf), cases[i]);
        RSCache_BufferRelease(&buf);
    }

    /* An exact multiple of the escape still needs the trailing remainder, or the
     * reader would keep consuming into the next field. */
    {
        struct RSCache_Buffer buf;
        RSCache_BufferInitAlloc(&buf, 0);
        puintsmartshortcompat(&buf, 32767);
        p2(&buf, 0x1234); /* a following field the reader must not eat */
        rewind_for_read(&buf);
        RSCACHE_CHECK_EQ(guintsmartshortcompat(&buf), 32767);
        RSCACHE_CHECK_EQ(g2(&buf), 0x1234);
        RSCache_BufferRelease(&buf);
    }
}

static void
test_varint2(void)
{
    RSCACHE_TEST_GROUP("varint2 (LEB128, low group first)");

    struct
    {
        int value;
        uint32_t width;
    } cases[] = {
        { 0, 1 },        { 1, 1 },      { 127, 1 },     { 128, 2 },
        { 16383, 2 },    { 16384, 3 },  { 0x7FFFFFFF, 5 },
        /* Negative round-trips through the full five groups. */
        { -1, 5 },
    };

    for( size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++ )
    {
        struct RSCache_Buffer buf;
        RSCache_BufferInitAlloc(&buf, 0);
        pvarint2(&buf, cases[i].value);
        RSCACHE_CHECK_EQ(RSCache_BufferLength(&buf), cases[i].width);
        rewind_for_read(&buf);
        RSCACHE_CHECK_EQ(gvarint2(&buf), cases[i].value);
        RSCache_BufferRelease(&buf);
    }

    /* 128 is 0x80 0x01: low seven bits first, continuation bit on the first. */
    {
        struct RSCache_Buffer buf;
        RSCache_BufferInitAlloc(&buf, 0);
        pvarint2(&buf, 128);
        static const unsigned char want[] = { 0x80, 0x01 };
        RSCACHE_CHECK_BYTES_EQ(buf.data, want, 2);
        RSCache_BufferRelease(&buf);
    }
}

static void
test_strings(void)
{
    RSCACHE_TEST_GROUP("pjstr / gcstring / gstringnewline");

    static const char* cases[] = {
        "", "a", "Resign", "Offer draw", "A longer string with spaces and 0123456789",
        "punctuation !\"&:`}~"
    };

    for( size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++ )
    {
        struct RSCache_Buffer buf;
        RSCache_BufferInitAlloc(&buf, 0);
        pjstr(&buf, cases[i], RSCACHE_JSTR_TERMINATOR_NULL);
        RSCACHE_CHECK_EQ(RSCache_BufferLength(&buf), strlen(cases[i]) + 1);
        rewind_for_read(&buf);
        char* got = gcstring(&buf);
        RSCACHE_CHECK_STR_EQ(got, cases[i]);
        free(got);
        RSCache_BufferRelease(&buf);
    }

    /* Newline terminator variant. */
    {
        struct RSCache_Buffer buf;
        RSCache_BufferInitAlloc(&buf, 0);
        pjstr(&buf, "old era", RSCACHE_JSTR_TERMINATOR_NEWLINE);
        RSCACHE_CHECK_EQ(buf.data[buf.position - 1], 0x0A);
        rewind_for_read(&buf);
        char* got = gstringnewline(&buf);
        RSCACHE_CHECK_STR_EQ(got, "old era");
        free(got);
        RSCache_BufferRelease(&buf);
    }

    /* Byte transparency both ways: pjstr writes the bytes unchanged, and
     * gcstring reads them back unchanged — including the 0x80..0x9F range. */
    {
        struct RSCache_Buffer buf;
        RSCache_BufferInitAlloc(&buf, 0);
        static const char high[] = { (char)0x80, (char)0x95, (char)0x99, 'a', ' ', 0 };
        pjstr(&buf, high, RSCACHE_JSTR_TERMINATOR_NULL);
        static const unsigned char want[] = { 0x80, 0x95, 0x99, 'a', ' ', 0x00 };
        RSCACHE_CHECK_BYTES_EQ(buf.data, want, 6);
        rewind_for_read(&buf);
        char* got = gcstring(&buf);
        RSCACHE_CHECK(got != NULL);
        RSCACHE_CHECK_EQ(strlen(got), 5);
        RSCACHE_CHECK_BYTES_EQ((const unsigned char*)got, want, 5);
        free(got);
        RSCache_BufferRelease(&buf);
    }

    /* Every non-NUL byte survives write-then-read for the null terminator. */
    for( int b = 0x01; b <= 0xFF; b++ )
    {
        char one[2] = { (char)b, 0 };
        struct RSCache_Buffer buf;
        RSCache_BufferInitAlloc(&buf, 0);
        pjstr(&buf, one, RSCACHE_JSTR_TERMINATOR_NULL);
        rewind_for_read(&buf);
        char* got = gcstring(&buf);
        RSCACHE_CHECK(got != NULL);
        RSCACHE_CHECK_EQ((unsigned char)got[0], b);
        RSCACHE_CHECK_EQ(got[1], 0);
        free(got);
        RSCache_BufferRelease(&buf);
    }

    /* Same sweep for the newline terminator; skip 0x0A (it *is* the stop). */
    for( int b = 0x01; b <= 0xFF; b++ )
    {
        if( b == 0x0A )
            continue;
        char one[2] = { (char)b, 0 };
        struct RSCache_Buffer buf;
        RSCache_BufferInitAlloc(&buf, 0);
        pjstr(&buf, one, RSCACHE_JSTR_TERMINATOR_NEWLINE);
        rewind_for_read(&buf);
        char* got = gstringnewline(&buf);
        RSCACHE_CHECK(got != NULL);
        RSCACHE_CHECK_EQ((unsigned char)got[0], b);
        RSCACHE_CHECK_EQ(got[1], 0);
        free(got);
        RSCache_BufferRelease(&buf);
    }

    /* Two consecutive strings must not run together. */
    {
        struct RSCache_Buffer buf;
        RSCache_BufferInitAlloc(&buf, 0);
        pjstr(&buf, "first", RSCACHE_JSTR_TERMINATOR_NULL);
        pjstr(&buf, "second", RSCACHE_JSTR_TERMINATOR_NULL);
        rewind_for_read(&buf);
        char* first = gcstring(&buf);
        char* second = gcstring(&buf);
        RSCACHE_CHECK_STR_EQ(first, "first");
        RSCACHE_CHECK_STR_EQ(second, "second");
        free(first);
        free(second);
        RSCache_BufferRelease(&buf);
    }
}

static void
test_cp1252(void)
{
    RSCACHE_TEST_GROUP("cp1252 <-> utf8");

    /* All non-NUL byte values survive ToUtf8 then Utf8ToCp1252. */
    {
        /* Skip the embedded NUL: strlen would stop. Test 0x01..0xFF as a string. */
        char mid[256];
        for( int i = 0; i < 255; i++ )
            mid[i] = (char)(i + 1);
        mid[255] = 0;

        char utf8[1024];
        int n = RSCache_Cp1252ToUtf8(mid, utf8, (int)sizeof(utf8));
        RSCACHE_CHECK(n > 0);
        RSCACHE_CHECK_EQ(utf8[n], 0);

        char back[256];
        int m = RSCache_Utf8ToCp1252(utf8, back, (int)sizeof(back));
        RSCACHE_CHECK_EQ(m, 255);
        RSCACHE_CHECK_BYTES_EQ((const unsigned char*)back, (const unsigned char*)mid, 255);
    }

    /* Spot-check known mappings. */
    {
        char utf8[16];
        int n;

        n = RSCache_Cp1252ToUtf8("\x80", utf8, (int)sizeof(utf8));
        RSCACHE_CHECK_EQ(n, 3);
        RSCACHE_CHECK_BYTES_EQ(
            (const unsigned char*)utf8, (const unsigned char*)"\xE2\x82\xAC", 3);

        n = RSCache_Cp1252ToUtf8("\x93", utf8, (int)sizeof(utf8));
        RSCACHE_CHECK_EQ(n, 3);
        RSCACHE_CHECK_BYTES_EQ(
            (const unsigned char*)utf8, (const unsigned char*)"\xE2\x80\x9C", 3);

        n = RSCache_Cp1252ToUtf8("\xA3", utf8, (int)sizeof(utf8));
        RSCACHE_CHECK_EQ(n, 2);
        RSCACHE_CHECK_BYTES_EQ(
            (const unsigned char*)utf8, (const unsigned char*)"\xC2\xA3", 2);

        n = RSCache_Cp1252ToUtf8("ABC", utf8, (int)sizeof(utf8));
        RSCACHE_CHECK_EQ(n, 3);
        RSCACHE_CHECK_STR_EQ(utf8, "ABC");
    }

    /* Truncation: returns needed length without overrunning dst. */
    {
        char tiny[4];
        memset(tiny, 0xAA, sizeof(tiny));
        int n = RSCache_Cp1252ToUtf8("\x80\x80", tiny, 4);
        RSCACHE_CHECK_EQ(n, 6); /* two 3-byte sequences */
        RSCACHE_CHECK_EQ(tiny[3], 0); /* always NUL-terminated when dst_size > 0 */
        /* Only the first codepoint fits (3 bytes + NUL). */
        RSCACHE_CHECK_BYTES_EQ(
            (const unsigned char*)tiny, (const unsigned char*)"\xE2\x82\xAC", 3);
    }

    /* dst_size == 0: measure only. */
    {
        int n = RSCache_Cp1252ToUtf8("\x80", NULL, 0);
        RSCACHE_CHECK_EQ(n, 3);
    }
}

static void
test_params(void)
{
    RSCACHE_TEST_GROUP("params");

    int int_values[] = { 42, -7, 0x00ABCDEF };
    char str_value[] = "param string";

    int64_t long_value = 0x1122334455667788LL;

    struct RSCache_Params src = { 0 };
    int keys[] = { 1, 0x00FFFFFF, 999, 12345, 77 };
    void* values[] = {
        &int_values[0], str_value, &int_values[1], &int_values[2], &long_value
    };
    uint8_t kinds[] = {
        RSCACHE_PARAM_INT,
        RSCACHE_PARAM_STRING,
        RSCACHE_PARAM_INT,
        RSCACHE_PARAM_INT,
        RSCACHE_PARAM_LONG,
    };
    src.keys = keys;
    src.values = values;
    src.kinds = kinds;
    src.count = 5;
    src.capacity = 5;

    struct RSCache_Buffer buf;
    RSCache_BufferInitAlloc(&buf, 0);
    pparams(&buf, &src);
    rewind_for_read(&buf);

    struct RSCache_Params got = { 0 };
    gparams(&buf, &got);

    RSCACHE_CHECK_EQ(got.count, src.count);
    if( got.count == src.count )
    {
        for( int i = 0; i < got.count; i++ )
        {
            RSCACHE_CHECK_EQ(got.keys[i], src.keys[i]);
            RSCACHE_CHECK_EQ(got.kinds[i], src.kinds[i]);
            if( src.kinds[i] == RSCACHE_PARAM_STRING )
                RSCACHE_CHECK_STR_EQ((const char*)got.values[i], (const char*)src.values[i]);
            else if( src.kinds[i] == RSCACHE_PARAM_LONG )
                RSCACHE_CHECK_EQ(*(int64_t*)got.values[i], *(int64_t*)src.values[i]);
            else
                RSCACHE_CHECK_EQ(*(int*)got.values[i], *(int*)src.values[i]);
        }
    }

    for( int i = 0; i < got.count; i++ )
        free(got.values[i]);
    free(got.keys);
    free(got.values);
    free(got.kinds);
    RSCache_BufferRelease(&buf);

    /* Empty param set is a single zero byte. */
    {
        struct RSCache_Buffer empty;
        RSCache_BufferInitAlloc(&empty, 0);
        struct RSCache_Params none = { 0 };
        pparams(&empty, &none);
        RSCACHE_CHECK_EQ(RSCache_BufferLength(&empty), 1);
        RSCACHE_CHECK_EQ(empty.data[0], 0);
        RSCache_BufferRelease(&empty);
    }
}

static void
test_ownership(void)
{
    RSCACHE_TEST_GROUP("ownership and growth");

    /* An owned buffer grows well past its initial capacity. */
    {
        struct RSCache_Buffer buf;
        RSCACHE_CHECK(RSCache_BufferInitAlloc(&buf, 4));
        for( int i = 0; i < 10000; i++ )
            p1(&buf, i & 0xff);
        RSCACHE_CHECK_EQ(RSCache_BufferLength(&buf), 10000);
        RSCACHE_CHECK(buf.size >= 10000);

        bool contents_ok = true;
        for( int i = 0; i < 10000; i++ )
        {
            if( buf.data[i] != (uint8_t)(i & 0xff) )
            {
                contents_ok = false;
                break;
            }
        }
        RSCACHE_CHECK(contents_ok);
        RSCache_BufferRelease(&buf);
        RSCACHE_CHECK(buf.data == NULL);
    }

    /* Detach hands over the payload length, not the allocation. */
    {
        struct RSCache_Buffer buf;
        RSCache_BufferInitAlloc(&buf, 1024);
        p4(&buf, 0xDEADBEEF);
        uint32_t size = 0;
        uint8_t* data = RSCache_BufferDetach(&buf, &size);
        RSCACHE_CHECK(data != NULL);
        RSCACHE_CHECK_EQ(size, 4);
        RSCACHE_CHECK(buf.data == NULL);
        RSCACHE_CHECK_EQ(buf.owns_data, false);
        free(data);
    }

    /* Detaching a borrowed buffer is refused rather than handing out memory the
     * buffer does not own. */
    {
        uint8_t storage[8] = { 0 };
        struct RSCache_Buffer buf;
        RSCache_BufferInit(&buf, storage, sizeof(storage));
        p1(&buf, 1);
        uint32_t size = 123;
        RSCACHE_CHECK(RSCache_BufferDetach(&buf, &size) == NULL);
        RSCACHE_CHECK_EQ(size, 0);
    }

    /* Borrowed buffers keep their fixed capacity: Ensure reports no room rather
     * than reallocating caller memory. */
    {
        uint8_t storage[4] = { 0 };
        struct RSCache_Buffer buf;
        RSCache_BufferInit(&buf, storage, sizeof(storage));
        RSCACHE_CHECK(RSCache_BufferEnsure(&buf, 4));
        RSCACHE_CHECK(!RSCache_BufferEnsure(&buf, 5));
        RSCACHE_CHECK(buf.data == storage);
    }

    /* A designated initialiser that predates the ownership field still yields a
     * borrowed buffer — this is how the net stack and every decoder build one. */
    {
        uint8_t storage[4] = { 0xAA, 0xBB, 0xCC, 0xDD };
        struct RSCache_Buffer buf = { .data = storage, .size = 4, .position = 0 };
        RSCACHE_CHECK_EQ(buf.owns_data, false);
        RSCACHE_CHECK_EQ(g2(&buf), 0xAABB);
        RSCACHE_CHECK(!RSCache_BufferEnsure(&buf, 3));
    }

    /* Remaining / Length agree with the cursor. */
    {
        uint8_t storage[8] = { 0 };
        struct RSCache_Buffer buf;
        RSCache_BufferInit(&buf, storage, sizeof(storage));
        RSCACHE_CHECK_EQ(RSCache_BufferRemaining(&buf), 8);
        g4(&buf);
        RSCACHE_CHECK_EQ(RSCache_BufferRemaining(&buf), 4);
        RSCACHE_CHECK_EQ(RSCache_BufferLength(&buf), 4);
        g4(&buf);
        RSCACHE_CHECK_EQ(RSCache_BufferRemaining(&buf), 0);
        /* Reading past the end yields 0 without moving the cursor. */
        RSCACHE_CHECK_EQ(g4(&buf), 0);
        RSCACHE_CHECK_EQ(buf.position, 8);
    }

    /* pwrite of nothing is a no-op, including on a zero-capacity buffer. */
    {
        struct RSCache_Buffer buf = { 0 };
        pbuf(&buf, NULL, 0);
        RSCACHE_CHECK_EQ(buf.position, 0);
    }
}

static void
test_mixed_record(void)
{
    RSCACHE_TEST_GROUP("mixed record");

    /* A miniature opcode-stream record, the shape every config encoder emits:
     * interleaved widths terminated by a zero opcode. Catches cursor drift that
     * per-codec tests cannot see. */
    struct RSCache_Buffer buf;
    RSCache_BufferInitAlloc(&buf, 0);

    p1(&buf, 1);
    pbigsmart(&buf, 40000);
    p1(&buf, 2);
    pjstr(&buf, "Ring of stone", RSCACHE_JSTR_TERMINATOR_NULL);
    p1(&buf, 3);
    pshortsmart(&buf, -300);
    p1(&buf, 4);
    p2(&buf, 65535);
    p1(&buf, 5);
    pushortsmart(&buf, 12);
    p1(&buf, 0);

    rewind_for_read(&buf);

    RSCACHE_CHECK_EQ(g1(&buf), 1);
    RSCACHE_CHECK_EQ(gbigsmart(&buf), 40000);
    RSCACHE_CHECK_EQ(g1(&buf), 2);
    char* name = gcstring(&buf);
    RSCACHE_CHECK_STR_EQ(name, "Ring of stone");
    free(name);
    RSCACHE_CHECK_EQ(g1(&buf), 3);
    RSCACHE_CHECK_EQ(gshortsmart(&buf), -300);
    RSCACHE_CHECK_EQ(g1(&buf), 4);
    RSCACHE_CHECK_EQ(g2(&buf), 65535);
    RSCACHE_CHECK_EQ(g1(&buf), 5);
    RSCACHE_CHECK_EQ(gushortsmart(&buf), 12);
    RSCACHE_CHECK_EQ(g1(&buf), 0);
    /* Exact consumption: the encoder wrote precisely what the decoder read. */
    RSCACHE_CHECK_EQ(buf.position, buf.size);

    RSCache_BufferRelease(&buf);
}

int
main(void)
{
    printf("rsbuffer tests\n");
    test_fixed_width();
    test_float();
    test_usmart();
    test_bigsmart();
    test_shortsmart();
    test_uintsmartshortcompat();
    test_varint2();
    test_strings();
    test_cp1252();
    test_params();
    test_ownership();
    test_mixed_record();
    return rscache_test_report("rsbuffer");
}
