/* Literal RSProt fixtures for every additional revision-239 interface setter. */

#include "torirsserver/mock239_interface_setters.h"

#include <rsareabuf.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void
check_bytes(
    const char* label,
    const uint8_t* got,
    size_t got_len,
    const uint8_t* want,
    size_t want_len)
{
    if( got_len == want_len && memcmp(got, want, want_len) == 0 )
    {
        printf("ok: %s\n", label);
        return;
    }
    fprintf(stderr, "FAIL: %s: got", label);
    for( size_t i = 0; i < got_len; i++ )
        fprintf(stderr, " %02x", got[i]);
    fprintf(stderr, "; want");
    for( size_t i = 0; i < want_len; i++ )
        fprintf(stderr, " %02x", want[i]);
    fprintf(stderr, "\n");
    failures++;
}

#define RUN_FIXTURE(label, call, ...)                                                        \
    do                                                                                       \
    {                                                                                        \
        static const uint8_t want[] = { __VA_ARGS__ };                                       \
        uint8_t got[16] = { 0 };                                                              \
        struct RSAreaBuf buf;                                                                 \
        rsab_wrap(&buf, got, sizeof(got));                                                    \
        buf.pos = 0;                                                                          \
        call;                                                                                 \
        check_bytes(label, got, rsab_len(&buf), want, sizeof(want));                         \
    } while( 0 )

int
main(void)
{
    /* Asymmetric, non-zero values make every order/transform choice visible. */
    RUN_FIXTURE(
        "IF_SETROTATESPEED",
        mock239_encode_if_setrotatespeed(&buf, 0x11223344, 0x5566, 0x7788),
        0x77, 0x88, 0x55, 0xe6, 0x22, 0x11, 0x44, 0x33);

    RUN_FIXTURE(
        "IF_SETANGLE",
        mock239_encode_if_setangle(&buf, 0x11223344, 0x5566, 0x7788, 0x99aa),
        0x22, 0x11, 0x44, 0x33, 0x55, 0x66, 0x77, 0x88, 0x2a, 0x99);

    /* Golden chatmenu placement: y=-13 g2Alt3, uid 219:1 g4Alt2,
     * x=0 g2Alt3.  These are the exact bytes sent between IF_OPENSUB and
     * RUNCLIENTSCRIPT 58 for every revision-239 choice menu. */
    RUN_FIXTURE(
        "IF_SETPOSITION chatmenu:options (0,-13)",
        mock239_encode_if_setposition(&buf, 0x00db0001, 0, -13),
        0x73, 0xff, 0x00, 0x01, 0x00, 0xdb, 0x80, 0x00);

    RUN_FIXTURE(
        "IF_SETNPCHEAD_ACTIVE",
        mock239_encode_if_setnpchead_active(&buf, 0x11223344, 0x5566),
        0x55, 0xe6, 0x33, 0x44, 0x11, 0x22);

    RUN_FIXTURE(
        "IF_SETPLAYERMODEL_BASECOLOUR",
        mock239_encode_if_setplayermodel_basecolour(&buf, 0x11223344, 0x55, 0x66),
        0x55, 0x66, 0x11, 0x22, 0x33, 0x44);

    RUN_FIXTURE(
        "IF_SETPLAYERMODEL_BODYTYPE",
        mock239_encode_if_setplayermodel_bodytype(&buf, 0x11223344, 0x55),
        0x33, 0x44, 0x11, 0x22, 0x55);

    RUN_FIXTURE(
        "IF_SETPLAYERMODEL_OBJ",
        mock239_encode_if_setplayermodel_obj(&buf, 0x11223344, 0x55667788),
        0x33, 0x44, 0x11, 0x22, 0x66, 0x55, 0x88, 0x77);

    RUN_FIXTURE(
        "IF_SETPLAYERMODEL_SELF",
        mock239_encode_if_setplayermodel_self(&buf, 0x11223344, 1),
        0x7f, 0x11, 0x22, 0x33, 0x44);

    /* Boolean normalisation is part of the encoder contract, not a caller
     * convention: every non-zero copyObjs value is the same on wire. */
    RUN_FIXTURE(
        "IF_SETPLAYERMODEL_SELF boolean normalisation",
        mock239_encode_if_setplayermodel_self(&buf, 0x11223344, -7),
        0x7f, 0x11, 0x22, 0x33, 0x44);

    if( failures )
    {
        fprintf(stderr, "mock239 interface setters: %d FAILED\n", failures);
        return 1;
    }
    printf("mock239 interface setters: all 9 passed\n");
    return 0;
}
