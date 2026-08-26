#include "cmd/cmdbus.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;

#define TEST_CHECK(cond)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__);                     \
            abort();                                                                               \
        }                                                                                          \
    } while( 0 )

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

    TEST_CHECK(CmdBus_PushFrame(&bus, 1234));
    TEST_CHECK(CmdBus_PushKey(&bus, TORIRS_CMD_INPUT_KEY_DOWN, 7));
    TEST_CHECK(CmdBus_PushMouseButton(&bus, TORIRS_CMD_INPUT_MOUSE_DOWN, 1, 100, -50));
    uint8_t raw[] = { 0xde, 0xad, 0xbe, 0xef };
    TEST_CHECK(CmdBus_Push(&bus, TORIRS_CMD_NET_RECV, raw, sizeof(raw)));

    struct ToriRS_CmdHeader h;
    uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];

    TEST_CHECK(CmdBus_Pop(&bus, &h, payload));
    assert(h.type == TORIRS_CMD_FRAME && h.length == sizeof(struct ToriRS_CmdFrame));
    struct ToriRS_CmdFrame frame;
    memcpy(&frame, payload, sizeof(frame));
    assert(frame.now_ms == 1234);

    TEST_CHECK(CmdBus_Pop(&bus, &h, payload));
    assert(h.type == TORIRS_CMD_INPUT_KEY_DOWN && payload[0] == 7);

    TEST_CHECK(CmdBus_Pop(&bus, &h, payload));
    assert(h.type == TORIRS_CMD_INPUT_MOUSE_DOWN);
    struct ToriRS_CmdMouseButton mb;
    memcpy(&mb, payload, sizeof(mb));
    assert(mb.button == 1 && mb.x == 100 && mb.y == -50);

    TEST_CHECK(CmdBus_Pop(&bus, &h, payload));
    assert(h.type == TORIRS_CMD_NET_RECV && h.length == 4);
    assert(memcmp(payload, raw, 4) == 0);

    TEST_CHECK(!CmdBus_Pop(&bus, &h, payload));
}

static void
test_zero_length_payload(void)
{
    struct ToriRS_CmdBus bus;
    CmdBus_Init(&bus);

    TEST_CHECK(CmdBus_Push(&bus, TORIRS_CMD_INPUT_CLEAR_KEYS, NULL, 0));

    struct ToriRS_CmdHeader h;
    uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];
    TEST_CHECK(CmdBus_Pop(&bus, &h, payload));
    assert(h.type == TORIRS_CMD_INPUT_CLEAR_KEYS && h.length == 0);
    TEST_CHECK(!CmdBus_Pop(&bus, &h, payload));
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
        TEST_CHECK(CmdBus_Push(&bus, TORIRS_CMD_NET_RECV, chunk, sizeof(chunk)));
        TEST_CHECK(CmdBus_Push(&bus, TORIRS_CMD_NET_RECV, chunk, sizeof(chunk)));
        TEST_CHECK(CmdBus_Pop(&bus, &h, payload));
        assert(h.length == sizeof(chunk));
        assert(memcmp(payload, chunk, sizeof(chunk)) == 0);
        TEST_CHECK(CmdBus_Pop(&bus, &h, payload));
        assert(memcmp(payload, chunk, sizeof(chunk)) == 0);
    }
    TEST_CHECK(!CmdBus_Pop(&bus, &h, payload));
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
    TEST_CHECK(!CmdBus_Push(&bus, TORIRS_CMD_NET_RECV, chunk, sizeof(chunk)));

    /* Draining one message frees room for exactly one more. */
    struct ToriRS_CmdHeader h;
    uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];
    TEST_CHECK(CmdBus_Pop(&bus, &h, payload));
    TEST_CHECK(CmdBus_Push(&bus, TORIRS_CMD_NET_RECV, chunk, sizeof(chunk)));

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
    TEST_CHECK(!CmdBus_Push(&bus, TORIRS_CMD_NET_RECV, big, 65535));
    assert(CmdRing_IsEmpty(&bus.ring));
}

static const char* TEST_RECORD_PATH = "build/cmdbus_test_record.trscmd";

static void
test_record_replay_roundtrip(void)
{
    struct ToriRS_CmdBus bus;
    CmdBus_Init(&bus);
    TEST_CHECK(CmdBus_RecordOpen(&bus, TEST_RECORD_PATH));

    /* Simulate three loop iterations; drain each as the main loop would. */
    struct ToriRS_CmdHeader h;
    uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];
    uint8_t netbytes[] = { 1, 2, 3, 4, 5 };

    TEST_CHECK(CmdBus_PushFrame(&bus, 100));
    TEST_CHECK(CmdBus_PushKey(&bus, TORIRS_CMD_INPUT_KEY_DOWN, 3));
    TEST_CHECK(CmdBus_PushMouseMove(&bus, 10, 20));
    while( CmdBus_Pop(&bus, &h, payload) )
        ;

    TEST_CHECK(CmdBus_PushFrame(&bus, 120));
    while( CmdBus_Pop(&bus, &h, payload) )
        ;

    TEST_CHECK(CmdBus_PushFrame(&bus, 140));
    TEST_CHECK(CmdBus_Push(&bus, TORIRS_CMD_NET_RECV, netbytes, sizeof(netbytes)));
    TEST_CHECK(CmdBus_PushKey(&bus, TORIRS_CMD_INPUT_KEY_UP, 3));
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
    TEST_CHECK(CmdReplay_PumpFrame(f, &replay_bus, &now));
    assert(now == 100);
    TEST_CHECK(CmdBus_Pop(&replay_bus, &h, payload) && h.type == TORIRS_CMD_FRAME);
    TEST_CHECK(CmdBus_Pop(&replay_bus, &h, payload));
    assert(h.type == TORIRS_CMD_INPUT_KEY_DOWN && payload[0] == 3);
    TEST_CHECK(CmdBus_Pop(&replay_bus, &h, payload));
    assert(h.type == TORIRS_CMD_INPUT_MOUSE_MOVE);
    TEST_CHECK(!CmdBus_Pop(&replay_bus, &h, payload));

    /* Frame 2 (empty) */
    TEST_CHECK(CmdReplay_PumpFrame(f, &replay_bus, &now));
    assert(now == 120);
    TEST_CHECK(CmdBus_Pop(&replay_bus, &h, payload) && h.type == TORIRS_CMD_FRAME);
    TEST_CHECK(!CmdBus_Pop(&replay_bus, &h, payload));

    /* Frame 3 */
    TEST_CHECK(CmdReplay_PumpFrame(f, &replay_bus, &now));
    assert(now == 140);
    TEST_CHECK(CmdBus_Pop(&replay_bus, &h, payload) && h.type == TORIRS_CMD_FRAME);
    TEST_CHECK(CmdBus_Pop(&replay_bus, &h, payload));
    assert(h.type == TORIRS_CMD_NET_RECV && h.length == sizeof(netbytes));
    assert(memcmp(payload, netbytes, sizeof(netbytes)) == 0);
    TEST_CHECK(CmdBus_Pop(&replay_bus, &h, payload));
    assert(h.type == TORIRS_CMD_INPUT_KEY_UP && payload[0] == 3);
    TEST_CHECK(!CmdBus_Pop(&replay_bus, &h, payload));

    /* EOF */
    TEST_CHECK(!CmdReplay_PumpFrame(f, &replay_bus, &now));
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

    TEST_CHECK(CmdReplay_Open(TEST_RECORD_PATH) == NULL);
    remove(TEST_RECORD_PATH);
}

/*
 * The host command family: an embedder saying "open interface 600" rather than
 * a device saying "the mouse moved". They ride this ring so that a host-driven
 * session records and replays like any other, which is what these check —
 * round-trip through the ring, and the variable-length run-script frame in
 * particular, whose size is the whole reason argc is carried rather than
 * implied.
 */
static void
test_host_commands_roundtrip(void)
{
    struct ToriRS_CmdBus bus;
    struct ToriRS_CmdHeader h;
    static uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];
    int32_t const args[3] = { 11, -22, 33 };

    CmdBus_Init(&bus);
    TEST_CHECK(CmdBus_PushUiOpenRoot(&bus, 600));
    TEST_CHECK(CmdBus_PushUiSetVar(&bus, TORIRS_CMD_UI_SET_VARP, 300, 100));
    TEST_CHECK(CmdBus_PushUiSetVar(&bus, TORIRS_CMD_UI_SET_VARBIT, 6440, 1));
    TEST_CHECK(CmdBus_PushUiRunScript(&bus, 3967, args, 3));
    TEST_CHECK(CmdBus_PushExecText(&bus, "setlevel attack 99"));

    TEST_CHECK(CmdBus_Pop(&bus, &h, payload));
    assert(h.type == TORIRS_CMD_UI_OPEN_ROOT);
    {
        struct ToriRS_CmdUiOpenRoot open;
        assert(h.length == sizeof(open));
        memcpy(&open, payload, sizeof(open));
        assert(open.interface_id == 600);
    }

    TEST_CHECK(CmdBus_Pop(&bus, &h, payload));
    assert(h.type == TORIRS_CMD_UI_SET_VARP);
    {
        struct ToriRS_CmdUiSetVar var;
        assert(h.length == sizeof(var));
        memcpy(&var, payload, sizeof(var));
        assert(var.id == 300 && var.value == 100);
    }

    TEST_CHECK(CmdBus_Pop(&bus, &h, payload));
    assert(h.type == TORIRS_CMD_UI_SET_VARBIT);
    {
        struct ToriRS_CmdUiSetVar var;
        memcpy(&var, payload, sizeof(var));
        assert(var.id == 6440 && var.value == 1);
    }

    TEST_CHECK(CmdBus_Pop(&bus, &h, payload));
    assert(h.type == TORIRS_CMD_UI_RUNSCRIPT);
    {
        struct ToriRS_CmdUiRunScript rs;
        /* Sized to the arguments it carries, not to the maximum: three args
         * cost three ints, and the drain bounds its read by argc. */
        assert(h.length == CmdBus_UiRunScriptBytes(3));
        assert(h.length < sizeof(rs));
        memset(&rs, 0, sizeof(rs));
        memcpy(&rs, payload, h.length);
        assert(rs.script_id == 3967);
        assert(rs.argc == 3);
        assert(rs.args[0] == 11 && rs.args[1] == -22 && rs.args[2] == 33);
    }

    TEST_CHECK(CmdBus_Pop(&bus, &h, payload));
    assert(h.type == TORIRS_CMD_EXEC_TEXT);
    /* No NUL on the wire; the header's length is the string's, as NET_CONNECT
     * has it. A drain that trusted a terminator would read past the frame. */
    assert(h.length == strlen("setlevel attack 99"));
    assert(memcmp(payload, "setlevel attack 99", h.length) == 0);

    TEST_CHECK(!CmdBus_Pop(&bus, &h, payload));
}

/** A no-argument script frame carries no argument bytes at all. */
static void
test_runscript_without_args(void)
{
    struct ToriRS_CmdBus bus;
    struct ToriRS_CmdHeader h;
    static uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];
    struct ToriRS_CmdUiRunScript rs;

    CmdBus_Init(&bus);
    TEST_CHECK(CmdBus_PushUiRunScript(&bus, 915, NULL, 0));
    TEST_CHECK(CmdBus_Pop(&bus, &h, payload));
    assert(h.type == TORIRS_CMD_UI_RUNSCRIPT);
    assert(h.length == CmdBus_UiRunScriptBytes(0));
    memset(&rs, 0, sizeof(rs));
    memcpy(&rs, payload, h.length);
    assert(rs.script_id == 915 && rs.argc == 0);
}

int
main(void)
{
    RUN_TEST(test_host_commands_roundtrip);
    RUN_TEST(test_runscript_without_args);
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
