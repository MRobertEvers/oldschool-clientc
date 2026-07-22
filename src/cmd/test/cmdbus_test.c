#include "cmd/cmdbus.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;

#define RUN_TEST(fn)                                                                              \
    do                                                                                            \
    {                                                                                             \
        fn();                                                                                     \
        tests_run++;                                                                              \
        printf("ok - %s\n", #fn);                                                                 \
    } while( 0 )

static void
test_push_pop_roundtrip(void)
{
    struct ToriRS_CmdBus bus;
    CmdBus_Init(&bus);

    assert(CmdBus_PushFrame(&bus, 1234));
    assert(CmdBus_PushKey(&bus, TORIRS_CMD_INPUT_KEY_DOWN, 7));
    assert(CmdBus_PushMouseButton(&bus, TORIRS_CMD_INPUT_MOUSE_DOWN, 1, 100, -50));
    uint8_t raw[] = { 0xde, 0xad, 0xbe, 0xef };
    assert(CmdBus_Push(&bus, TORIRS_CMD_NET_RECV, raw, sizeof(raw)));

    struct ToriRS_CmdHeader h;
    uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];

    assert(CmdBus_Pop(&bus, &h, payload));
    assert(h.type == TORIRS_CMD_FRAME && h.length == sizeof(struct ToriRS_CmdFrame));
    struct ToriRS_CmdFrame frame;
    memcpy(&frame, payload, sizeof(frame));
    assert(frame.now_ms == 1234);

    assert(CmdBus_Pop(&bus, &h, payload));
    assert(h.type == TORIRS_CMD_INPUT_KEY_DOWN && payload[0] == 7);

    assert(CmdBus_Pop(&bus, &h, payload));
    assert(h.type == TORIRS_CMD_INPUT_MOUSE_DOWN);
    struct ToriRS_CmdMouseButton mb;
    memcpy(&mb, payload, sizeof(mb));
    assert(mb.button == 1 && mb.x == 100 && mb.y == -50);

    assert(CmdBus_Pop(&bus, &h, payload));
    assert(h.type == TORIRS_CMD_NET_RECV && h.length == 4);
    assert(memcmp(payload, raw, 4) == 0);

    assert(!CmdBus_Pop(&bus, &h, payload));
}

static void
test_zero_length_payload(void)
{
    struct ToriRS_CmdBus bus;
    CmdBus_Init(&bus);

    assert(CmdBus_Push(&bus, TORIRS_CMD_INPUT_CLEAR_KEYS, NULL, 0));

    struct ToriRS_CmdHeader h;
    uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];
    assert(CmdBus_Pop(&bus, &h, payload));
    assert(h.type == TORIRS_CMD_INPUT_CLEAR_KEYS && h.length == 0);
    assert(!CmdBus_Pop(&bus, &h, payload));
}

static void
test_wraparound(void)
{
    struct ToriRS_CmdBus bus;
    CmdBus_Init(&bus);

    /* Cycle far more bytes than the ring holds so head/tail wrap repeatedly,
     * with a payload size chosen to not divide the ring size evenly. */
    uint8_t chunk[1013];
    for( int i = 0; i < (int)sizeof(chunk); i++ )
        chunk[i] = (uint8_t)(i * 31);

    struct ToriRS_CmdHeader h;
    uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];
    for( int round = 0; round < 1000; round++ )
    {
        chunk[0] = (uint8_t)round;
        assert(CmdBus_Push(&bus, TORIRS_CMD_NET_RECV, chunk, sizeof(chunk)));
        assert(CmdBus_Push(&bus, TORIRS_CMD_NET_RECV, chunk, sizeof(chunk)));
        assert(CmdBus_Pop(&bus, &h, payload));
        assert(h.length == sizeof(chunk));
        assert(memcmp(payload, chunk, sizeof(chunk)) == 0);
        assert(CmdBus_Pop(&bus, &h, payload));
        assert(memcmp(payload, chunk, sizeof(chunk)) == 0);
    }
    assert(!CmdBus_Pop(&bus, &h, payload));
}

static void
test_overflow_rejected(void)
{
    struct ToriRS_CmdBus bus;
    CmdBus_Init(&bus);

    uint8_t chunk[TORIRS_CMD_MAX_PAYLOAD];
    memset(chunk, 0xab, sizeof(chunk));

    int pushed = 0;
    while( CmdBus_Push(&bus, TORIRS_CMD_NET_RECV, chunk, sizeof(chunk)) )
        pushed++;
    /* Ring must fill and then reject (no partial writes, no crash). */
    assert(pushed >= 10);
    assert(!CmdBus_Push(&bus, TORIRS_CMD_NET_RECV, chunk, sizeof(chunk)));

    /* Draining one message frees room for exactly one more. */
    struct ToriRS_CmdHeader h;
    uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];
    assert(CmdBus_Pop(&bus, &h, payload));
    assert(CmdBus_Push(&bus, TORIRS_CMD_NET_RECV, chunk, sizeof(chunk)));

    int drained = 0;
    while( CmdBus_Pop(&bus, &h, payload) )
    {
        assert(h.length == sizeof(chunk));
        drained++;
    }
    assert(drained == pushed);
}

static void
test_oversize_payload_rejected(void)
{
    struct ToriRS_CmdBus bus;
    CmdBus_Init(&bus);

    /* Length fields beyond the cap must be rejected up front. A uint16_t can
     * express up to 65535, which exceeds TORIRS_CMD_MAX_PAYLOAD. */
    static uint8_t big[65535];
    assert(!CmdBus_Push(&bus, TORIRS_CMD_NET_RECV, big, 65535));
    assert(CmdRing_IsEmpty(&bus.ring));
}

static const char* TEST_RECORD_PATH = "build/cmdbus_test_record.trscmd";

static void
test_record_replay_roundtrip(void)
{
    struct ToriRS_CmdBus bus;
    CmdBus_Init(&bus);
    assert(CmdBus_RecordOpen(&bus, TEST_RECORD_PATH));

    /* Simulate three loop iterations; drain each as the main loop would. */
    struct ToriRS_CmdHeader h;
    uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];
    uint8_t netbytes[] = { 1, 2, 3, 4, 5 };

    assert(CmdBus_PushFrame(&bus, 100));
    assert(CmdBus_PushKey(&bus, TORIRS_CMD_INPUT_KEY_DOWN, 3));
    assert(CmdBus_PushMouseMove(&bus, 10, 20));
    while( CmdBus_Pop(&bus, &h, payload) )
        ;

    assert(CmdBus_PushFrame(&bus, 120));
    while( CmdBus_Pop(&bus, &h, payload) )
        ;

    assert(CmdBus_PushFrame(&bus, 140));
    assert(CmdBus_Push(&bus, TORIRS_CMD_NET_RECV, netbytes, sizeof(netbytes)));
    assert(CmdBus_PushKey(&bus, TORIRS_CMD_INPUT_KEY_UP, 3));
    while( CmdBus_Pop(&bus, &h, payload) )
        ;

    CmdBus_RecordClose(&bus);

    /* Replay and verify the exact sequence comes back, frame by frame. */
    FILE* f = CmdReplay_Open(TEST_RECORD_PATH);
    assert(f);

    struct ToriRS_CmdBus replay_bus;
    CmdBus_Init(&replay_bus);
    uint64_t now = 0;

    /* Frame 1 */
    assert(CmdReplay_PumpFrame(f, &replay_bus, &now));
    assert(now == 100);
    assert(CmdBus_Pop(&replay_bus, &h, payload) && h.type == TORIRS_CMD_FRAME);
    assert(CmdBus_Pop(&replay_bus, &h, payload));
    assert(h.type == TORIRS_CMD_INPUT_KEY_DOWN && payload[0] == 3);
    assert(CmdBus_Pop(&replay_bus, &h, payload));
    assert(h.type == TORIRS_CMD_INPUT_MOUSE_MOVE);
    assert(!CmdBus_Pop(&replay_bus, &h, payload));

    /* Frame 2 (empty) */
    assert(CmdReplay_PumpFrame(f, &replay_bus, &now));
    assert(now == 120);
    assert(CmdBus_Pop(&replay_bus, &h, payload) && h.type == TORIRS_CMD_FRAME);
    assert(!CmdBus_Pop(&replay_bus, &h, payload));

    /* Frame 3 */
    assert(CmdReplay_PumpFrame(f, &replay_bus, &now));
    assert(now == 140);
    assert(CmdBus_Pop(&replay_bus, &h, payload) && h.type == TORIRS_CMD_FRAME);
    assert(CmdBus_Pop(&replay_bus, &h, payload));
    assert(h.type == TORIRS_CMD_NET_RECV && h.length == sizeof(netbytes));
    assert(memcmp(payload, netbytes, sizeof(netbytes)) == 0);
    assert(CmdBus_Pop(&replay_bus, &h, payload));
    assert(h.type == TORIRS_CMD_INPUT_KEY_UP && payload[0] == 3);
    assert(!CmdBus_Pop(&replay_bus, &h, payload));

    /* EOF */
    assert(!CmdReplay_PumpFrame(f, &replay_bus, &now));
    fclose(f);
    remove(TEST_RECORD_PATH);
}

static void
test_replay_rejects_bad_magic(void)
{
    FILE* f = fopen(TEST_RECORD_PATH, "wb");
    assert(f);
    fputs("NOTACMDFILE", f);
    fclose(f);

    assert(CmdReplay_Open(TEST_RECORD_PATH) == NULL);
    remove(TEST_RECORD_PATH);
}

int
main(void)
{
    RUN_TEST(test_push_pop_roundtrip);
    RUN_TEST(test_zero_length_payload);
    RUN_TEST(test_wraparound);
    RUN_TEST(test_overflow_rejected);
    RUN_TEST(test_oversize_payload_rejected);
    RUN_TEST(test_record_replay_roundtrip);
    RUN_TEST(test_replay_rejects_bad_magic);
    printf("cmdbus: %d tests passed\n", tests_run);
    return 0;
}
