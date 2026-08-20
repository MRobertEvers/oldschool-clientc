/*
 * LOC_ADD_CHANGE_V2's per-placement menu, server writer against client reader.
 *
 * The two fields under test are the ones that let one loc record be two things:
 *
 *   opFlags   a 5-bit mask of which right-click options are SHOWN
 *   ops       (slot, label) pairs REPLACING the loctype's own labels
 *
 * Both were written as constants before this — count 0, mask 0x1f — because
 * nothing could say otherwise. `doors/scripts/doors_selfstage.rs2` now does:
 * a door with no opened counterpart in the cache swings by re-adding ITSELF at
 * the swung tile with everything hidden and "Close" on op2.
 *
 * ## Why this is a test and not a read-through
 *
 * The slot index is offset-encoded on the wire, and both directions must undo
 * it the same way or the label lands on the wrong menu row. RSProt keys its map
 * by the ONE-based op number and writes `p1 key - 1`; the 239 gamepack indexes
 * its five-slot array with the byte it read, unadjusted. So the wire byte is
 * the ZERO-based slot, and the generated codec's `RSPROT_XFORM(..., -1)` adds
 * one back on decode.
 *
 * Get that wrong by one and nothing errors. The packet still frames, the door
 * still swings, and the menu row simply says the wrong thing — or says nothing,
 * because the label went to a slot the mask hid. That is exactly the class of
 * bug a round trip catches and reading cannot.
 *
 * Scope is deliberately the wire alone: no scene, no world, no VM. The writer
 * is reached through the revision's own payload vtable — the same function
 * pointer the running server calls — and the reader is the generated codec
 * `osrs239_parse.c` itself decodes with. Linking the client's parse wrapper
 * here would drag the login handshake, the entity streams and the cache in
 * behind it, which would make a test of six bytes depend on all of them.
 *
 * The wrapper's one piece of added logic is `slot = key - 1`, and the invariant
 * it rests on is asserted directly below: the codec hands back the ONE-based op
 * number.
 */

#include "rsareabuf.h"

#include "../mock230_wire.h"
#include "../mock230_zone.h"

#include "packets/loc_add_change_v2.h"
#include "rsprot_buffer.h"
#include "rsprot_exec.h"

#include <stdio.h>
#include <string.h>

static int g_checks;
static int g_failures;

static void
check(
    int ok,
    const char* what)
{
    g_checks++;
    if( !ok )
        g_failures++;
    printf("loc_ops_wire: %-56s %s\n", what, ok ? "ok" : "FAILED");
}

static void
check_str(
    const char* got,
    const char* want,
    const char* what)
{
    int ok = strcmp(got, want) == 0;

    g_checks++;
    if( !ok )
    {
        g_failures++;
        printf("loc_ops_wire: %-56s FAILED (got \"%s\", want \"%s\")\n", what, got, want);
        return;
    }
    printf("loc_ops_wire: %-56s ok\n", what);
}

/* What `osrs239_parse.c` reduces the decoded message to, so the assertions read
 * the way the client's packet does. `ops[i]` is indexed by SLOT, i.e. `key - 1`
 * — the mapping under test. */
struct Decoded
{
    int loc_id;
    int pos;
    int info;
    int op_flags;
    char ops[5][32];
    /* The raw one-based keys, kept so the offset can be asserted directly
     * rather than only through its consequence. */
    int keys[5];
    int key_count;
};

/* Encode one LOC_ADD_CHANGE through the 239 writer, then read it back with the
 * generated codec — the same pair a live server and client use. */
static int
roundtrip(
    const struct Mock230ZoneEvent* event,
    struct Decoded* out)
{
    const struct Mock230Wire* wire = mock230_wire_by_name("osrs239");
    struct RSAreaBuf buf;
    uint8_t bytes[512];
    Rsprot_MsgLocAddChangeV2_opsElem ops[256];
    MsgLocAddChangeV2 m;
    RSProt_Buffer c;
    RsprotExec x;
    int props = ((event->shape & 0x1f) << 2) | (event->angle & 3);
    int written;

    if( !wire || !wire->payload || !wire->payload->zone_payload )
        return 0;
    rsab_wrap(&buf, bytes, sizeof(bytes));
    if( !wire->payload->zone_payload(&buf, PKT_NAME_LOC_ADD_CHANGE, event, event->pos, props) )
        return 0;
    written = (int)rsab_len(&buf);

    memset(&m, 0, sizeof(m));
    m.ops = ops;
    RSProt_BufferWrapRead(&c, bytes, written);
    rsprot_exec_decode(&x, &c);
    packet_loc_add_change_v2_rev239_out(&x, &m);
    if( !rsprot_exec_ok(&x) || c.err )
        return 0;
    /* Every byte, and no more: a layout that framed short would leave the next
     * record in an enclosed stream reading from the middle of this one. */
    if( c.rpos != written )
        return 0;

    memset(out, 0, sizeof(*out));
    out->loc_id = m.id;
    out->pos = m.coord_in_zone_packed;
    out->info = m.loc_properties_packed;
    out->op_flags = m.op_flags;
    out->key_count = m.ops_count;
    for( int i = 0; i < m.ops_count && i < 5; i++ )
        out->keys[i] = ops[i].key;
    /* The mapping osrs239_parse.c performs, reproduced here because it is what
     * is under test. */
    for( int i = 0; i < m.ops_count; i++ )
    {
        int slot = ops[i].key - 1;

        if( slot < 0 || slot >= 5 || !ops[i].value )
            continue;
        snprintf(out->ops[slot], sizeof(out->ops[slot]), "%s", ops[i].value);
    }
    return 1;
}

int
main(void)
{
    struct Mock230ZoneEvent event;
    struct Decoded got;

    /* ---------------------------------------------------------------
     * A placement with nothing to say: the loctype's menu, untouched.
     * This is what every loc_add in the tree encodes as, so it is the
     * case a regression would break most widely.
     * --------------------------------------------------------------- */
    memset(&event, 0, sizeof(event));
    event.kind = MOCK230_ZONE_EV_LOC_ADD_CHANGE;
    event.pos = (3 << 4) | 5;
    event.shape = 0; /* wall_straight */
    event.angle = 2; /* loc_east */
    event.id = 15056; /* farming_shed_poordoor — the Lumbridge Swamp hut door */
    mock230_loc_ops_default(&event.ops);

    if( !roundtrip(&event, &got) )
    {
        printf("loc_ops_wire: could not round-trip the neutral placement\n");
        return 1;
    }
    check(got.loc_id == 15056, "neutral: loc id survives");
    check(got.pos == event.pos, "neutral: coord-in-zone survives");
    check((got.info >> 2) == 0 && (got.info & 3) == 2, "neutral: shape and angle survive");
    check(got.op_flags == 0x1f, "neutral: every op slot is shown");
    check(got.key_count == 0, "neutral: the override list is empty, not five blanks");
    for( int i = 0; i < 5; i++ )
        check(got.ops[i][0] == '\0', "neutral: no slot carries a replacement label");

    /* ---------------------------------------------------------------
     * A swung door: op1 hidden, "Close" on op2.
     *
     * The mask and the label have to agree about WHICH slot, and slot 2
     * is index 1 — one off in either direction puts "Close" on a hidden
     * row (invisible door) or leaves "Open" showing beside it.
     * --------------------------------------------------------------- */
    memset(&event, 0, sizeof(event));
    event.kind = MOCK230_ZONE_EV_LOC_ADD_CHANGE;
    event.pos = (3 << 4) | 6;
    event.shape = 0;
    event.angle = 3;
    event.id = 15056;
    event.ops.flags = 1 << 1; /* op2 only */
    snprintf(event.ops.name[1], sizeof(event.ops.name[1]), "Close");

    if( !roundtrip(&event, &got) )
    {
        printf("loc_ops_wire: could not round-trip the swung placement\n");
        return 1;
    }
    check(got.op_flags == 0x02, "swung: only op2 is shown");
    check(got.key_count == 1, "swung: exactly one slot is overridden");
    /* The offset itself. Slot index 1 goes on the wire as 1 and the codec's
     * XFORM hands back 2 — the one-based op number `osrs239_parse.c` subtracts
     * from. Asserting the consequence alone would let a writer that wrote the
     * one-based number and a reader that did not subtract agree with each
     * other and disagree with the real client. */
    check(got.keys[0] == 2, "swung: the codec reports the one-based op number");
    check(got.ops[0][0] == '\0', "swung: op1 carries no label");
    check_str(got.ops[1], "Close", "swung: op2 says Close");
    check(got.ops[2][0] == '\0' && got.ops[3][0] == '\0' && got.ops[4][0] == '\0',
          "swung: no label leaked into a later slot");

    /* ---------------------------------------------------------------
     * Every slot at once. The neutral and single-slot cases above both
     * pass if the writer silently transposed slots 0 and 1; this one
     * does not, because each label names its own index.
     * --------------------------------------------------------------- */
    memset(&event, 0, sizeof(event));
    event.kind = MOCK230_ZONE_EV_LOC_ADD_CHANGE;
    event.pos = 0;
    event.shape = 9; /* wall_diagonal */
    event.angle = 1;
    event.id = 1535; /* poordoor */
    event.ops.flags = 0x1f;
    for( int i = 0; i < 5; i++ )
        snprintf(event.ops.name[i], sizeof(event.ops.name[i]), "slot%d", i);

    if( !roundtrip(&event, &got) )
    {
        printf("loc_ops_wire: could not round-trip the all-slots placement\n");
        return 1;
    }
    check(got.op_flags == 0x1f, "all slots: mask survives");
    check((got.info >> 2) == 9, "all slots: wall_diagonal shape survives");
    for( int i = 0; i < 5; i++ )
    {
        char want[16];

        snprintf(want, sizeof(want), "slot%d", i);
        check_str(got.ops[i], want, "all slots: each label lands on its own slot");
    }

    printf("loc_ops_wire: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
