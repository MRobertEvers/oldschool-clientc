/* Literal-byte gate for every golden rev239 RUNCLIENTSCRIPT wire type. */

#include "torirsserver/mock239_runclientscript.h"

#include <rsareabuf.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition, label)                                                                    \
    do                                                                                             \
    {                                                                                              \
        if( condition )                                                                            \
            printf("ok: %s\n", label);                                                            \
        else                                                                                       \
        {                                                                                          \
            printf("FAIL: %s\n", label);                                                         \
            failures++;                                                                            \
        }                                                                                          \
    } while( 0 )

int
main(void)
{
    uint8_t storage[128] = { 0 };
    struct RSAreaBuf buf;
    int32_t ints[] = { -1, 0, 300 };
    char const* strings[] = { "a", "" };
    char types[] = { 'i', 'W', 's', 'X', (char)0xcf, '\0' };
    struct Mock239ClientScriptArg args[5] = { 0 };
    static uint8_t const expected[] = {
        /* types + NUL */
        'i', 'W', 's', 'X', 0xcf, 0x00,
        /* reversed arg 4: long */
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        /* reversed arg 3: string array */
        0x02, 'a', 0x00, 0x00,
        /* reversed arg 2: string */
        'h', 'i', 0x00,
        /* reversed arg 1: int array; 300 zigzags to 600 = d8 04 varint */
        0x03, 0x01, 0x00, 0xd8, 0x04,
        /* reversed arg 0: scalar int */
        0x01, 0x02, 0x03, 0x04,
        /* script id */
        0x11, 0x22, 0x33, 0x44,
    };

    args[0].int_value = 0x01020304;
    args[1].int_array = ints;
    args[1].array_count = 3;
    args[2].string_value = "hi";
    args[3].string_array = strings;
    args[3].array_count = 2;
    args[4].long_value = INT64_C(0x0102030405060708);

    rsab_wrap(&buf, storage, sizeof(storage));
    CHECK(mock239_encode_runclientscript(&buf, 0x11223344, types, args, 5),
          "all five rev239 argument kinds encode");
    CHECK(rsab_len(&buf) == sizeof(expected), "payload length is exact");
    CHECK(memcmp(storage, expected, sizeof(expected)) == 0,
          "payload bytes match the golden decoder literally");

    /* Login's camera bootstrap is deliberately a literal-wire fixture.  The
     * golden decoder reads arguments in reverse order before script 605 stores
     * them in varcs 1338..1341; changing either the order or one of these four
     * values collapses the wheel and both settings sliders. */
    {
        struct Mock239ClientScriptArg zoom[4] = { 0 };
        static uint8_t const zoom_expected[] = {
            'i', 'i', 'i', 'i', 0x00,
            0x00, 0x00, 0x03, 0x80, /* arg 3: 896 */
            0x00, 0x00, 0x00, 0x80, /* arg 2: 128 */
            0x00, 0x00, 0x03, 0x80, /* arg 1: 896 */
            0x00, 0x00, 0x00, 0x80, /* arg 0: 128 */
            0x00, 0x00, 0x02, 0x5d, /* script 605 */
        };

        zoom[0].int_value = 128;
        zoom[1].int_value = 896;
        zoom[2].int_value = 128;
        zoom[3].int_value = 896;
        memset(storage, 0, sizeof(storage));
        rsab_wrap(&buf, storage, sizeof(storage));
        CHECK(mock239_encode_runclientscript(&buf, 605, "iiii", zoom, 4),
              "camera bootstrap script 605 encodes");
        CHECK(rsab_len(&buf) == sizeof(zoom_expected),
              "camera bootstrap payload length is exact");
        CHECK(memcmp(storage, zoom_expected, sizeof(zoom_expected)) == 0,
              "script 605 carries 128,896,128,896 in golden reverse-argument order");
    }

    rsab_wrap(&buf, storage, sizeof(storage));
    args[1].array_count = -1;
    CHECK(!mock239_encode_runclientscript(&buf, 1, "W", &args[1], 1),
          "negative array length is refused");

    rsab_wrap(&buf, storage, sizeof(storage));
    args[1].array_count = 128;
    CHECK(!mock239_encode_runclientscript(&buf, 1, "W", &args[1], 1),
          "count that wraps the golden client's byte loop is refused");

    rsab_wrap(&buf, storage, sizeof(storage));
    args[1].array_count = 3;
    CHECK(!mock239_encode_runclientscript(&buf, 1, "Wi", &args[1], 1),
          "type count must equal argument count");

    rsab_wrap(&buf, storage, 4);
    args[0].int_value = 7;
    CHECK(!mock239_encode_runclientscript(&buf, 1, "i", args, 1),
          "short destination is reported");

    if( failures )
    {
        printf("mock239 RUNCLIENTSCRIPT: %d FAILED\n", failures);
        return 1;
    }
    printf("mock239 RUNCLIENTSCRIPT: all passed\n");
    return 0;
}
