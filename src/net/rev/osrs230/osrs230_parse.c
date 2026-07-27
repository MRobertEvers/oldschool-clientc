#include "net/rev/gameproto_revisions.h"
#include "net/rev/packets/pkt_rebuild_normal.h"
#include "net/rev/pktnames.h"
#include "net/rev/revpacket.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Byte cursor over a framed payload. Every reader clamps to `len` and sets
 * `over` on the first read past the end, so a layout that does not match the
 * wire is caught by the caller (which drops the packet) instead of applying
 * half-decoded inventory state. */
struct Osrs230Cursor
{
    uint8_t const* data;
    int len;
    int pos;
    int over;
};

static int
c_g1(struct Osrs230Cursor* c)
{
    if( c->pos + 1 > c->len )
    {
        c->over = 1;
        return 0;
    }
    return c->data[c->pos++];
}

/* gByteAlt2: the writer sends (-value); reference reads -readByte(). */
static int
c_g1alt2(struct Osrs230Cursor* c)
{
    return (-c_g1(c)) & 0xff;
}

static int
c_g2(struct Osrs230Cursor* c)
{
    int hi = c_g1(c);
    int lo = c_g1(c);
    return (hi << 8) | lo;
}

static int
c_g4(struct Osrs230Cursor* c)
{
    int b0 = c_g1(c);
    int b1 = c_g1(c);
    int b2 = c_g1(c);
    int b3 = c_g1(c);
    return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

/* gIntAlt1: little-endian. */
static int
c_g4alt1(struct Osrs230Cursor* c)
{
    int b0 = c_g1(c);
    int b1 = c_g1(c);
    int b2 = c_g1(c);
    int b3 = c_g1(c);
    return (b3 << 24) | (b2 << 16) | (b1 << 8) | b0;
}

/* gSmart: 1 byte below 128, else a 2-byte big-endian value biased by 0x8000. */
static int
c_gsmart(struct Osrs230Cursor* c)
{
    int peek;
    if( c->pos + 1 > c->len )
    {
        c->over = 1;
        return 0;
    }
    peek = c->data[c->pos];
    if( peek < 128 )
        return c_g1(c);
    return c_g2(c) - 0x8000;
}

/* One inventory slot body, shared by the full and partial encoders:
 *   p1Alt2 count (255 escapes to a p4Alt1 real count), p2 objId + 1. */
static void
osrs230_read_inv_slot(
    struct Osrs230Cursor* c,
    int* out_obj_id,
    int* out_count)
{
    int count = c_g1alt2(c);
    if( count == 255 )
        count = c_g4alt1(c);
    *out_count = count;
    *out_obj_id = c_g2(c) - 1;
}

/*
 * OSRS rev-230 protocol dispatch (the rev-table `parse` slot). The revision
 * selects which versioned packet parser under net/rev/packets/ decodes each
 * canonical name — e.g. REBUILD_NORMAL -> pkt_rebuild_normal_read, and (future)
 * PLAYER_INFO -> pkt_player_info_v5_read, NPC_INFO -> pkt_npc_info_v5_read.
 * Returns 1 (parsed, push), 0 (parsed, skip), <0 (not mine -> shared parser).
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
    switch( pkt_name )
    {
    case PKT_NAME_REBUILD_NORMAL:
        return pkt_rebuild_normal_read(data, len, out);

    /* IF_OPENTOP (op 60, 2 bytes): interfaceId as p2Alt1 (little-endian short,
     * bytes [lo, hi]). Opens a group as the gameframe root. */
    case PKT_NAME_IF_OPENTOP:
        if( len < 2 )
            return 0;
        out->_if_opentop.interface_id = data[0] | (data[1] << 8);
        return 1;

    /* IF_OPENSUB (op 6, 7 bytes): p1 type, p2Alt2 interfaceId (bytes
     * [hi, lo+128]), p4Alt3 destinationCombinedId (bytes [b2, b3, b0, b1]).
     * RSProt IfOpenSubEncoder. destinationCombinedId = destIface<<16 | destComp. */
    case PKT_NAME_IF_OPENSUB:
        if( len < 7 )
            return 0;
        out->_if_opensub.type = data[0];
        out->_if_opensub.interface_id = (data[1] << 8) | ((data[2] - 128) & 0xff);
        out->_if_opensub.target_uid =
            (data[4] << 24) | (data[3] << 16) | (data[6] << 8) | data[5];
        return 1;

    /* IF_CLOSESUB (op 36, 4 bytes): combinedId as p4 (big-endian packed int).
     * RSProt IfCloseSubEncoder. */
    case PKT_NAME_IF_CLOSESUB:
        if( len < 4 )
            return 0;
        out->_if_closesub.target_uid =
            (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
        return 1;

    /* UPDATE_INV_FULL (op 10, var-u16). RSProt UpdateInvFullEncoder:
     *   p4 combinedId, p2 inventoryId, p2 capacity, then per slot the shared
     *   slot body. The lc254 layout the common parser assumes (p2 component,
     *   p1 size, id-then-count) does not fit here. */
    case PKT_NAME_UPDATE_INV_FULL:
    {
        struct Osrs230Cursor cur = { data, len, 0, 0 };
        int capacity;
        out->_update_inv_full.component_id = c_g4(&cur);
        out->_update_inv_full.inv_id = c_g2(&cur);
        capacity = c_g2(&cur);
        if( cur.over || capacity < 0 || capacity > 4096 )
            return 0;
        out->_update_inv_full.size = capacity;
        out->_update_inv_full.obj_ids = malloc((size_t)capacity * sizeof(int));
        out->_update_inv_full.obj_counts = malloc((size_t)capacity * sizeof(int));
        if( !out->_update_inv_full.obj_ids || !out->_update_inv_full.obj_counts )
        {
            free(out->_update_inv_full.obj_ids);
            free(out->_update_inv_full.obj_counts);
            out->_update_inv_full.obj_ids = NULL;
            out->_update_inv_full.obj_counts = NULL;
            out->_update_inv_full.size = 0;
            return 0;
        }
        for( int i = 0; i < capacity; i++ )
            osrs230_read_inv_slot(
                &cur, &out->_update_inv_full.obj_ids[i], &out->_update_inv_full.obj_counts[i]);
        if( cur.over )
        {
            fprintf(
                stderr,
                "osrs230: UPDATE_INV_FULL inv=%d capacity=%d overran %d bytes; dropped\n",
                out->_update_inv_full.inv_id,
                capacity,
                len);
            free(out->_update_inv_full.obj_ids);
            free(out->_update_inv_full.obj_counts);
            out->_update_inv_full.obj_ids = NULL;
            out->_update_inv_full.obj_counts = NULL;
            out->_update_inv_full.size = 0;
            return 0;
        }
        return 1;
    }

    /* UPDATE_INV_PARTIAL (op 37, var-u16): p4 combinedId, p2 inventoryId, then
     * {gSmart slot + slot body} until the frame is consumed. A trailing partial
     * record means the layout is wrong for this payload, so drop the whole
     * packet rather than write a mis-decoded slot into the container. */
    case PKT_NAME_UPDATE_INV_PARTIAL:
    {
        struct Osrs230Cursor cur = { data, len, 0, 0 };
        int max_entries;
        int written = 0;
        out->_update_inv_partial.component_id = c_g4(&cur);
        out->_update_inv_partial.inv_id = c_g2(&cur);
        out->_update_inv_partial.count = 0;
        out->_update_inv_partial.entries = NULL;
        if( cur.over )
            return 0;
        /* Smallest record is 4 bytes (1 slot + 1 count + 2 obj). */
        max_entries = (len - cur.pos) / 4 + 1;
        out->_update_inv_partial.entries = (struct PktUpdateInvPartialEntry*)malloc(
            (size_t)max_entries * sizeof(struct PktUpdateInvPartialEntry));
        if( !out->_update_inv_partial.entries )
            return 0;
        while( cur.pos < len && written < max_entries )
        {
            struct PktUpdateInvPartialEntry* entry = &out->_update_inv_partial.entries[written];
            entry->slot = c_gsmart(&cur);
            osrs230_read_inv_slot(&cur, &entry->obj_id, &entry->count);
            if( cur.over )
                break;
            written++;
        }
        if( cur.over || cur.pos != len )
        {
            fprintf(
                stderr,
                "osrs230: UPDATE_INV_PARTIAL inv=%d did not consume its %d-byte frame "
                "(stopped at %d after %d slots); dropped\n",
                out->_update_inv_partial.inv_id,
                len,
                cur.pos,
                written);
            free(out->_update_inv_partial.entries);
            out->_update_inv_partial.entries = NULL;
            return 0;
        }
        out->_update_inv_partial.count = written;
        return 1;
    }

    /* MESSAGE_GAME (op 90, var-u8): p1 type then a NUL-terminated string. The
     * shared lc254 parser reads a newline-terminated string over the whole
     * payload and would swallow the type byte into the text. */
    case PKT_NAME_MESSAGE_GAME:
    {
        int text_len;
        if( len < 1 )
            return 0;
        text_len = len - 1;
        out->_message_game.text = malloc((size_t)text_len + 1);
        if( !out->_message_game.text )
            return 0;
        memcpy(out->_message_game.text, data + 1, (size_t)text_len);
        out->_message_game.text[text_len] = '\0';
        return 1;
    }

    /* UPDATE_STAT_V2 (op 114, 7 bytes): p1 stat, p1 base level, p4 xp, p1
     * boosted level. The lc254 layout is 6 bytes in a different order, and its
     * parser asserts the frame is fully consumed. */
    case PKT_NAME_UPDATE_STAT:
    {
        struct Osrs230Cursor cur = { data, len, 0, 0 };
        out->_update_stat.stat = c_g1(&cur);
        out->_update_stat.level = c_g1(&cur);
        out->_update_stat.xp = c_g4(&cur);
        (void)c_g1(&cur); /* boosted level: no client-side consumer yet */
        return cur.over ? 0 : 1;
    }

    /* UPDATE_RUNENERGY (op 77, 2 bytes): p2 hundredths of a percent. lc254
     * sends a single byte. */
    case PKT_NAME_UPDATE_RUNENERGY:
    {
        if( len < 2 )
            return 0;
        out->_update_run_energy.run_energy = ((data[0] << 8) | data[1]) / 100;
        return 1;
    }

    /* UPDATE_INV_STOPTRANSMIT (op 80): p4 combinedId. The server stops sending
     * this container; nothing to apply client-side beyond the record. */
    case PKT_NAME_UPDATE_INV_STOP_TRANSMIT:
        if( len < 4 )
            return 0;
        out->_update_inv_stop_transmit.component_id =
            (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
        out->_update_inv_stop_transmit.inv_id = -1;
        return 1;

    default:
        return -1; /* fall back to the shared gameproto_parse */
    }
}
