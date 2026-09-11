#include "torirsserver/torirs_server_interface_state.h"

#include <rsareabuf.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond, ...)                                                                  \
    do                                                                                    \
    {                                                                                     \
        if( !(cond) )                                                                     \
        {                                                                                 \
            fprintf(stderr, "FAIL: ");                                                    \
            fprintf(stderr, __VA_ARGS__);                                                  \
            fprintf(stderr, "\n");                                                       \
            failures++;                                                                   \
        }                                                                                 \
    } while( 0 )

static void
check_bytes(
    const char* label,
    const uint8_t* got,
    size_t got_len,
    const uint8_t* want,
    size_t want_len)
{
    if( got_len == want_len && memcmp(got, want, want_len) == 0 )
        return;
    fprintf(stderr, "FAIL: %s: got %zu bytes", label, got_len);
    for( size_t i = 0; i < got_len; i++ )
        fprintf(stderr, " %02x", got[i]);
    fprintf(stderr, "; want %zu bytes", want_len);
    for( size_t i = 0; i < want_len; i++ )
        fprintf(stderr, " %02x", want[i]);
    fprintf(stderr, "\n");
    failures++;
}

static void
test_setevents_golden(void)
{
    static const uint8_t want[] = {
        0x03, 0x84,                         /* end p2Alt2 */
        0x55, 0x66, 0x77, 0x88,             /* events2 p4 */
        0x33, 0x44, 0x11, 0x22,             /* events1 p4Alt2 */
        0x01, 0x02,                         /* start p2 */
        0x34, 0x12, 0x78, 0x56,             /* combined id p4Alt3 */
    };
    uint8_t bytes[32] = { 0 };
    struct RSAreaBuf buf;
    uint32_t events1 = 0;
    uint32_t events2 = 0;

    rsab_wrap(&buf, bytes, sizeof(bytes));
    buf.pos = 0; /* rsab_wrap is normally a reader; rewind it for this fixture. */
    ToriRSServer_IfStateEncodeSeteventsV2(
        &buf, 0x12345678, 0x0102, 0x0304, UINT32_C(0x11223344),
        UINT32_C(0x55667788));
    check_bytes("IF_SETEVENTS_V2 golden body", bytes, rsab_len(&buf), want, sizeof(want));

    /* This expected pair is stated as literals, not computed through a decoder
     * mirror. 0x00200c0b contains pause, op1/op3/op10, target-object and target. */
    ToriRSServer_IfStateSplitClassicEvents(UINT32_C(0x00200c0b), &events1, &events2);
    CHECK(events1 == UINT32_C(0x00200801),
          "classic mask must clear bits 1..10 from events1, got 0x%08x", events1);
    CHECK(events2 == UINT32_C(0x00000205),
          "classic op bits must move down into events2, got 0x%08x", events2);
}

static void
test_clearinv_golden(void)
{
    static const uint8_t want[] = { 0x56, 0x78, 0x12, 0x34 };
    uint8_t bytes[8] = { 0 };
    struct RSAreaBuf buf;

    rsab_wrap(&buf, bytes, sizeof(bytes));
    buf.pos = 0;
    ToriRSServer_IfStateEncodeClearinv(&buf, 0x12345678);
    check_bytes("IF_CLEARINV golden body", bytes, rsab_len(&buf), want, sizeof(want));
}

static void
test_range_replacement(void)
{
    struct ToriRSServerInterfaceState state;
    const int uid = 0x01230045;

    ToriRSServer_IfStateInit(&state);
    CHECK(ToriRSServer_IfStateSeteventsV2(&state, uid, 0, 9, 1, 11),
          "initial event range inserts");
    CHECK(ToriRSServer_IfStateSeteventsV2(&state, uid, 3, 5, 2, 22),
          "inner event range inserts");
    CHECK(state.event_count == 3, "inner replacement should split to 3 ranges, got %d",
          state.event_count);
    CHECK(state.events[0].start == 0 && state.events[0].end == 2 &&
              state.events[0].events1 == 1,
          "left split must preserve the old mask");
    CHECK(state.events[1].start == 3 && state.events[1].end == 5 &&
              state.events[1].events2 == 22,
          "middle range must carry the replacement masks");
    CHECK(state.events[2].start == 6 && state.events[2].end == 9 &&
              state.events[2].events1 == 1,
          "right split must preserve the old mask");

    CHECK(ToriRSServer_IfStateSeteventsV2(&state, uid, 2, 7, 3, 33),
          "crossing event range inserts");
    CHECK(state.event_count == 3, "crossing replacement should still have 3 ranges, got %d",
          state.event_count);
    CHECK(state.events[0].start == 0 && state.events[0].end == 1,
          "crossing replacement left edge");
    CHECK(state.events[1].start == 2 && state.events[1].end == 7 &&
              state.events[1].events1 == 3 && state.events[1].events2 == 33,
          "crossing replacement body");
    CHECK(state.events[2].start == 8 && state.events[2].end == 9,
          "crossing replacement right edge");

    CHECK(ToriRSServer_IfStateSeteventsV2(&state, uid, -1, -1, 4, 44),
          "static-widget -1 range is representable");
    CHECK(state.event_count == 4 && state.events[0].start == -1 && state.events[0].end == -1,
          "static-widget range stays distinct from dynamic slot zero");
}

static void
test_mount_lifecycle(void)
{
    struct ToriRSServerInterfaceState state;
    const int root_slot = 0x00640001;
    const int nested_slot = 0x00c80002;
    const int leaf_slot = 0x012c0003;
    const int unrelated_slot = 0x00640004;

    ToriRSServer_IfStateInit(&state);
    ToriRSServer_IfStateOpenTop(&state, 100);
    CHECK(ToriRSServer_IfStateOpenSub(&state, root_slot, 200, 0), "open root child");
    CHECK(ToriRSServer_IfStateOpenSub(&state, nested_slot, 300, 1), "open nested child");
    CHECK(ToriRSServer_IfStateOpenSub(&state, leaf_slot, 400, 1), "open nested leaf");
    CHECK(ToriRSServer_IfStateOpenSub(&state, unrelated_slot, 500, 1), "open sibling");
    CHECK(ToriRSServer_IfStateSeteventsV2(&state, 0x00c80009, -1, -1, 1, 1),
          "events on child group");
    CHECK(ToriRSServer_IfStateSeteventsV2(&state, 0x012c0009, 0, 4, 2, 2),
          "events on grandchild group");
    CHECK(ToriRSServer_IfStateSeteventsV2(&state, 0x01f40009, 0, 4, 3, 3),
          "events on sibling group");

    CHECK(ToriRSServer_IfStateCloseSub(&state, root_slot), "close mounted root child");
    CHECK(state.mount_count == 1 && state.mounts[0].interface_id == 500,
          "close must recursively remove nested mounts but keep siblings");
    CHECK(state.event_count == 1 && state.events[0].component_uid == 0x01f40009,
          "close must clear events for every recursively unloaded group");

    CHECK(ToriRSServer_IfStateOpenSub(&state, root_slot, 600, 0), "reopen target");
    CHECK(ToriRSServer_IfStateMoveSub(&state, root_slot, 0x00640005), "move mounted child");
    CHECK(state.mount_count == 2 && state.mounts[1].target_uid == 0x00640005 &&
              state.mounts[1].interface_id == 600,
          "movesub must preserve the group and type at the new target");

    ToriRSServer_IfStateOpenTop(&state, 101);
    CHECK(state.root_interface == 101 && state.mount_count == 0,
          "opentop must reset the authoritative mount tree");
    CHECK(state.event_count == 1 && state.events[0].component_uid == 0x01f40009,
          "opentop must retain IF_SETEVENTS authority for gameframe re-arming");
}

static void
test_resync_golden(void)
{
    static const uint8_t want[] = {
        0x12, 0x34, 0x00, 0x02,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0xa1, 0xb2, 0xc3, 0xd4, 0xe5, 0xf6, 0x02,
        /* No event-count field: the first event begins immediately here. */
        0x11, 0x22, 0x33, 0x44, 0xff, 0xff, 0x00, 0x03,
        0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc,
    };
    struct ToriRSServerInterfaceState state;
    uint8_t bytes[128] = { 0 };
    struct RSAreaBuf buf;

    ToriRSServer_IfStateInit(&state);
    ToriRSServer_IfStateOpenTop(&state, 0x1234);
    CHECK(ToriRSServer_IfStateOpenSub(&state, 0x01020304, 0x0506, 7), "first mount");
    CHECK(ToriRSServer_IfStateOpenSub(&state, (int)UINT32_C(0xa1b2c3d4), 0xe5f6, 2),
          "second mount");
    CHECK(ToriRSServer_IfStateSeteventsV2(
              &state, 0x11223344, -1, 3, UINT32_C(0x55667788),
              UINT32_C(0x99aabbcc)),
          "resync event");

    rsab_wrap(&buf, bytes, sizeof(bytes));
    buf.pos = 0;
    ToriRSServer_IfStateEncodeResyncV2(&buf, &state);
    CHECK(ToriRSServer_IfStateResyncPayloadSize(&state) == sizeof(want),
          "resync size must be 4 + 7N + 16M, got %zu",
          ToriRSServer_IfStateResyncPayloadSize(&state));
    check_bytes("IF_RESYNC_V2 golden body", bytes, rsab_len(&buf), want, sizeof(want));
}

int
main(void)
{
    test_setevents_golden();
    test_clearinv_golden();
    test_range_replacement();
    test_mount_lifecycle();
    test_resync_golden();

    if( failures )
    {
        fprintf(stderr, "ToriRSServer interface-state test: %d failure(s)\n", failures);
        return 1;
    }
    fprintf(stderr, "ToriRSServer interface-state test: PASS\n");
    return 0;
}
