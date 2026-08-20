#include "net/mock/mock239_varp.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void
check_fixture(
    const char* name,
    const uint8_t* got,
    size_t got_len,
    const uint8_t expected[3])
{
    if( got_len != 3 || memcmp(got, expected, 3) != 0 )
    {
        fprintf(stderr,
                "FAIL %s: got %zu byte(s) %02x %02x %02x, expected %02x %02x %02x\n",
                name,
                got_len,
                got_len > 0 ? got[0] : 0,
                got_len > 1 ? got[1] : 0,
                got_len > 2 ? got[2] : 0,
                expected[0],
                expected[1],
                expected[2]);
        failures++;
        return;
    }
    printf("ok: %s\n", name);
}

int
main(void)
{
    uint8_t data[16];
    struct RSAreaBuf buf;
    static const uint8_t player_depends[3] = { 0x80, 0xd3, 0x04 };
    static const uint8_t npc_depends[3] = { 0x80, 0x9a, 0x05 };

    memset(data, 0, sizeof(data));
    rsab_wrap(&buf, data, sizeof(data));
    mock239_encode_varp_small(&buf, 1107, 0);
    check_fixture("player AttackOption depends", data, rsab_len(&buf), player_depends);

    memset(data, 0, sizeof(data));
    rsab_wrap(&buf, data, sizeof(data));
    mock239_encode_varp_small(&buf, 1306, 0);
    check_fixture("NPC AttackOption depends", data, rsab_len(&buf), npc_depends);

    if( failures )
    {
        fprintf(stderr, "mock239 varp: %d FAILED\n", failures);
        return 1;
    }
    printf("mock239 varp: all 2 passed\n");
    return 0;
}
