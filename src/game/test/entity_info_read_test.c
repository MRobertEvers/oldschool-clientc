/*
 * EntityInfoScratch_Read (game/entity_info_scratch.h): the read both clients
 * fill an entity-info op list with never hands back an incomplete list.
 *
 *   - A legal rev-242 PLAYER_INFO, written by the server's own writer
 *     (player_info_osrs242_write): all 255 players an observer can track
 *     entering view in one tick, each with the blocks a busy tick carries
 *     (four hits, four headbars, eight graphics, a sequence, a tint, an exact
 *     move, a face, a chat line, name extras, a reset, a move speed). Read
 *     from a default-sized list, it gives every op a read into a list big
 *     enough for anything gives, and leaves the player table exactly as that
 *     read leaves it (the list grew and the packet was read again from the
 *     table as it stood before it).
 *   - The next tick reads in step from that table.
 *   - A legal rev-254 NPC_INFO with 1,400 npcs entering view (a stateless
 *     stream): every op.
 *
 * Run: make -C src test-entity-info-read
 */
#include "game/entity_info_scratch.h"
#include "streams/info_rs.h"
#include "streams/player_info.h"
#include "streams/player_info_write.h"
#include "streams/rsprot_info.h"

#include "rsprot_buf.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

#define LOCAL_INDEX 42
#define TRACKED 255

/* The client's player table, as net/proto/proto_info.c holds it. */
static RsprotPlayerInfo g_pi;

static int
pi_read(uint8_t const* data, int len, struct RsprotInfoList* out)
{
    return player_info_osrs242(&g_pi, data, len, out);
}

static size_t
pi_state_size(void)
{
    return sizeof(g_pi);
}

static void
pi_state_save(void* out)
{
    memcpy(out, &g_pi, sizeof(g_pi));
}

static void
pi_state_load(void const* in)
{
    memcpy(&g_pi, in, sizeof(g_pi));
}

static struct EntityInfoReader const k_player_reader = {
    .read = pi_read,
    .state_size = pi_state_size,
    .state_save = pi_state_save,
    .state_load = pi_state_load,
};

static int32_t
gpi_coord(int level, int x, int z)
{
    return (int32_t)(((level & 3) << 28) | ((x & 16383) << 14) | (z & 16383));
}

/* A read into a list big enough for any packet: the reference. */
static int
read_unbounded(
    int (*read)(uint8_t const*, int, struct RsprotInfoList*),
    uint8_t const* data,
    int len,
    struct RsprotInfoOp** out_ops)
{
    enum
    {
        OPS = 1 << 18,
        BYTES = 1 << 22
    };
    struct RsprotInfoOp* ops = calloc(OPS, sizeof(*ops));
    uint8_t* bytes = malloc(BYTES);
    RsprotInfoList list;
    int n;

    assert(ops);
    assert(bytes);
    rsprot_info_list_init(&list, ops, OPS, bytes, BYTES);
    n = read(data, len, &list);
    assert(!list.overflow);
    free(bytes);
    *out_ops = ops;
    return n;
}

static int
same_kinds(
    struct RsprotInfoOp const* a,
    struct RsprotInfoOp const* b,
    int n)
{
    for( int i = 0; i < n; i++ )
        if( a[i].kind != b[i].kind || a[i].who != b[i].who )
            return 0;
    return 1;
}

static void
busy_ext(struct RsprotPlayerExt* ext, int i)
{
    static uint8_t const chat[] = { 0x11, 0x92, 0x03, 0xf4 };

    memset(ext, 0, sizeof(*ext));
    ext->has_face = 1;
    ext->face.kind = RSPROT_FACE_KIND_ANGLE;
    ext->face.angle = 512;
    ext->has_hit = 1;
    ext->hit_type = 1;
    ext->hit_value = 10 + i % 50;
    ext->hit_extra_count = 3;
    for( int k = 0; k < 3; k++ )
    {
        ext->hit_extra[k].type = 1;
        ext->hit_extra[k].value = k + 1;
    }
    ext->has_headbar = 1;
    ext->headbar_duration = 40;
    ext->headbar_start_fill = 20;
    ext->headbar_end_fill = 200;
    ext->headbar_extra_count = 3;
    ext->has_chat = 1;
    ext->chat_data = chat;
    ext->chat_len = (int)sizeof(chat);
    ext->has_spotanim = 1;
    ext->spotanim_count = RSPROT_PLAYER_SPOTANIM_MAX;
    for( int k = 0; k < RSPROT_PLAYER_SPOTANIM_MAX; k++ )
    {
        ext->spotanims[k].slot = k;
        ext->spotanims[k].id = 100 + k;
    }
    ext->has_seq = 1;
    ext->seq_id = 808;
    ext->has_move_speed = 1;
    ext->move_speed = 1;
    ext->has_name_extras = 1;
    ext->name_before = "";
    ext->name_after = "";
    ext->name_after_level = "";
    ext->has_player_reset = 1;
    ext->has_tint = 1;
    ext->tint_end_cycle = 100;
    ext->tint_weight = 64;
    ext->has_exact_move = 1;
    ext->exact_end_x = 2;
    ext->exact_end_cycle = 30;
}

static void
test_a_crowded_player_info_reads_whole(void)
{
    static uint8_t init_bytes[4608];
    static uint8_t packet[1 << 16];
    static struct RsprotPlayerExt exts[TRACKED];
    static struct RsprotPlayerUpdate updates[TRACKED + 1];
    static RsprotPlayerInfo before;
    static RsprotPlayerInfo after_reference;
    struct RsprotPlayerInfoWriter server;
    struct RsprotBuf buf;
    struct EntityInfoScratchSlot slot;
    RsprotInfoList list;
    struct RsprotInfoOp* reference;
    int count = 0;
    int reference_count;
    int read_count;
    int len;

    printf("entity info read: 255 players entering view with a busy tick's blocks\n");
    rsprot_buf_wrap(&buf, init_bytes, sizeof(init_bytes));
    rsprot_player_info_write_init(&buf, LOCAL_INDEX, gpi_coord(0, 3200, 3200));
    rsprot_player_info_reset(&g_pi, LOCAL_INDEX);
    rsprot_player_info_init(&g_pi, init_bytes, (int)buf.wpos);
    rsprot_player_info_writer_init(&server, LOCAL_INDEX, gpi_coord(0, 3200, 3200));

    updates[count++] = (struct RsprotPlayerUpdate){
        .index = LOCAL_INDEX, .visible = 1, .coord = gpi_coord(0, 3200, 3200)
    };
    for( int i = 0, index = 1; i < TRACKED - 1; index++ )
    {
        if( index == LOCAL_INDEX )
            continue;
        busy_ext(&exts[i], i);
        updates[count++] = (struct RsprotPlayerUpdate){
            .index = index,
            .visible = 1,
            .coord = gpi_coord(0, 3190 + i % 20, 3190 + i / 20),
            .ext = &exts[i],
        };
        i++;
    }
    rsprot_buf_wrap(&buf, packet, sizeof(packet));
    player_info_osrs242_write(&buf, &server, updates, count);
    assert(rsprot_buf_ok(&buf));
    len = (int)buf.wpos;

    memcpy(&before, &g_pi, sizeof(g_pi));
    reference_count = read_unbounded(pi_read, packet, len, &reference);
    memcpy(&after_reference, &g_pi, sizeof(g_pi));
    printf("  a %d-byte packet of %d ops (a list holds %d by default)\n", len, reference_count,
           (int)ENTITY_INFO_SCRATCH_OPS_MAX);
    CHECK(reference_count > ENTITY_INFO_SCRATCH_OPS_MAX, "the packet needs more than a default list");

    memcpy(&g_pi, &before, sizeof(g_pi));
    memset(&slot, 0, sizeof(slot));
    EntityInfoScratch_Borrow(&slot, &list);
    read_count = EntityInfoScratch_Read(&slot, &list, &k_player_reader, packet, len);
    printf("  the shared read gave %d ops, overflow %d\n", read_count, list.overflow);
    CHECK(!list.overflow, "the list the read hands back is complete");
    CHECK(read_count == reference_count, "every op: %d of %d", read_count, reference_count);
    CHECK(list.count == reference_count, "the list holds every op: %d", list.count);
    CHECK(
        read_count == reference_count && same_kinds(list.ops, reference, reference_count),
        "the ops are the reference's, in its order");
    CHECK(memcmp(&g_pi, &after_reference, sizeof(g_pi)) == 0,
          "the player table is where one whole read leaves it");
    CHECK(slot.ops == list.ops, "the kept list is the one that grew");
    EntityInfoScratch_Release(&slot, &list);
    free(reference);

    /* The next tick, nobody moving: in step with the table. */
    {
        RsprotInfoList next;

        count = 0;
        updates[count++] = (struct RsprotPlayerUpdate){
            .index = LOCAL_INDEX, .visible = 1, .coord = gpi_coord(0, 3200, 3200)
        };
        for( int i = 0, index = 1; i < TRACKED - 1; index++ )
        {
            if( index == LOCAL_INDEX )
                continue;
            updates[count++] = (struct RsprotPlayerUpdate){
                .index = index,
                .visible = 1,
                .coord = gpi_coord(0, 3190 + i % 20, 3190 + i / 20),
            };
            i++;
        }
        rsprot_buf_wrap(&buf, packet, sizeof(packet));
        player_info_osrs242_write(&buf, &server, updates, count);
        assert(rsprot_buf_ok(&buf));
        EntityInfoScratch_Borrow(&slot, &next);
        (void)EntityInfoScratch_Read(&slot, &next, &k_player_reader, packet, (int)buf.wpos);
        CHECK(rsprot_info_list_complete(&next), "the next tick reads whole and in step");
        EntityInfoScratch_Release(&slot, &next);
    }
    {
        struct EntityInfoScratch scratch;

        memset(&scratch, 0, sizeof(scratch));
        scratch.player = slot;
        EntityInfoScratch_Free(&scratch);
    }
}

/* MSB-first bits, as rsprot_gbits reads them. */
struct Bits
{
    uint8_t* buf;
    int pos;
};

static void
bits(struct Bits* w, int value, int count)
{
    for( int i = count - 1; i >= 0; i-- )
    {
        if( (value >> i) & 1 )
            w->buf[w->pos >> 3] |= (uint8_t)(0x80 >> (w->pos & 7));
        w->pos++;
    }
}

static void
test_a_crowded_npc_info_reads_whole(void)
{
    static uint8_t packet[1 << 14];
    struct EntityInfoReader const reader = { .read = npc_info_rs254 };
    struct EntityInfoScratchSlot slot;
    RsprotInfoList list;
    struct RsprotInfoOp* reference;
    struct Bits w = { packet, 0 };
    int reference_count;
    int read_count;
    int len;

    printf("entity info read: 1,400 npcs entering view (rev 254)\n");
    memset(packet, 0, sizeof(packet));
    bits(&w, 0, 8); /* no npcs in view before */
    for( int i = 0; i < 1400; i++ )
    {
        bits(&w, i + 1, 14); /* slot */
        bits(&w, 1 + i % 100, 11); /* type */
        bits(&w, i % 16, 5);
        bits(&w, i % 13, 5);
        bits(&w, 0, 1); /* no extended info */
    }
    bits(&w, 16383, 14); /* the terminator */
    len = (w.pos + 7) / 8 + 4;

    reference_count = read_unbounded(npc_info_rs254, packet, len, &reference);
    printf("  a %d-byte packet of %d ops\n", len, reference_count);
    CHECK(reference_count > ENTITY_INFO_SCRATCH_OPS_MAX, "the packet needs more than a default list");
    memset(&slot, 0, sizeof(slot));
    EntityInfoScratch_Borrow(&slot, &list);
    read_count = EntityInfoScratch_Read(&slot, &list, &reader, packet, len);
    printf("  the shared read gave %d ops, overflow %d\n", read_count, list.overflow);
    CHECK(!list.overflow, "the list the read hands back is complete");
    CHECK(read_count == reference_count, "every op: %d of %d", read_count, reference_count);
    CHECK(
        read_count == reference_count && same_kinds(list.ops, reference, reference_count),
        "the ops are the reference's, in its order");
    EntityInfoScratch_Release(&slot, &list);
    free(reference);
    {
        struct EntityInfoScratch scratch;

        memset(&scratch, 0, sizeof(scratch));
        scratch.npc = slot;
        EntityInfoScratch_Free(&scratch);
    }
}

int
main(void)
{
    test_a_crowded_player_info_reads_whole();
    test_a_crowded_npc_info_reads_whole();
    if( g_failures )
    {
        printf("entity info read: %d FAILED\n", g_failures);
        return 1;
    }
    printf("entity info read: ALL PASS\n");
    return 0;
}
