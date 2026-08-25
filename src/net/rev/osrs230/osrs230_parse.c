#include "net/rev/gameproto_revisions.h"
#include <assert.h>
#include "net/rev/packets/pkt_rebuild_normal.h"
#include "net/rev/packets/pkt_rebuild_region.h"
#include "net/rev/pktnames.h"
#include "net/rev/revpacket.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rsprot_buffer.h"

/*
 * The byte cursor and its six readers used to live here: a private
 * `struct Osrs230Cursor` plus six readers spelled c_g1 / c_g1alt2 / c_g2 /
 * c_g4 / c_g4alt1 / c_gsmart. They
 * are RSProt_Buffer's, and now come from it — see
 * `3rd/rsprot/src/rsprot_buffer.h` and `docs/BUFFER_ACCESSOR_AUDIT.md`.
 *
 * The names carry the byte order rather than an `alt` ordinal, so a call site
 * states what it reads: `RSProt_BufferG4Le` was little-endian, and says so now.
 *
 * Same clamping contract as before — a read past the end latches an error and
 * returns 0 instead of running off the buffer, and the caller drops the packet
 * rather than applying half-decoded state. `cur.err` replaces `cur.err`.
 */

/* One inventory slot body, shared by the full and partial encoders:
 *   p1Alt2 count (255 escapes to a p4Alt1 real count), p2 objId + 1. */
static void
osrs230_read_inv_slot(
    RSProt_Buffer* c,
    int* out_obj_id,
    int* out_count)
{
    int count = RSProt_BufferG1_neg(c);
    if( count == 255 )
        count = RSProt_BufferG4Le(c);
    *out_count = count;
    *out_obj_id = RSProt_BufferG2Be(c) - 1;
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

    case PKT_NAME_REBUILD_REGION:
        return pkt_rebuild_region_read(data, len, out);

    /*
     * RUNCLIENTSCRIPT (op 84). Reference layout: a newline-terminated string of
     * one type character per argument, then the arguments in REVERSE order
     * ('s' -> string, anything else -> p4), then the script id as p4. The
     * reverse order is not a quirk of this port — the reference writer pushes
     * the CS2 operand stack, which unwinds last-argument-first.
     */
    case PKT_NAME_RUNCLIENTSCRIPT:
    {
        RSProt_Buffer cur;
        RSProt_BufferWrapRead(&cur, data, len);
        char types[PKT_RUNCLIENTSCRIPT_ARG_MAX + 1];
        int argc = 0;
        int terminated = 0;

        for( ;; )
        {
            int ch = RSProt_BufferG1(&cur);

            if( cur.err )
                break;
            if( ch == '\n' || ch == 0 )
            {
                terminated = 1;
                break;
            }
            /*
             * More arguments than the struct holds FAILS the packet, and that
             * is the whole point of the branch.
             *
             * Stopping at the cap and decoding anyway is what this did, and it
             * is worse than it looks: the unread type characters stay in the
             * stream, so the very next read — argument 19's int — comes off
             * those stray type bytes instead of off a p4. Every argument is
             * then wrong, `cur.err` never trips because the packet is long
             * enough, and the parse returns 1. A dropped RUNCLIENTSCRIPT is a
             * panel that does not appear; a decoded one is a panel drawn from
             * garbage, which is indistinguishable from a content bug.
             *
             * Unreachable from this repo's own server (the host aborts above
             * TORIRSSERVER_RUNCLIENTSCRIPT_ARG_MAX and the compiler above
             * SSC_MAX_VARARG_TYPES, both smaller), reachable from any other.
             */
            if( argc >= PKT_RUNCLIENTSCRIPT_ARG_MAX )
                return 0;
            types[argc++] = (char)ch;
        }
        types[argc] = '\0';
        if( cur.err || !terminated )
            return 0;

        struct PktRunClientScript* p = pkt_runclientscript_reset(out);
        p->argc = argc;
        for( int i = argc - 1; i >= 0; i-- )
        {
            if( types[i] == 's' )
            {
                char* dst = p->strv[i];
                int n = 0;
                for( ;; )
                {
                    int ch = RSProt_BufferG1(&cur);
                    if( cur.err || ch == '\n' || ch == 0 )
                        break;
                    if( n < PKT_RUNCLIENTSCRIPT_STR_LEN - 1 )
                        dst[n++] = (char)ch;
                }
                dst[n] = '\0';
                p->str_mask |= 1u << i;
            }
            else
            {
                p->intv[i] = RSProt_BufferG4Be(&cur);
            }
        }
        p->script_id = RSProt_BufferG4Be(&cur);
        return cur.err ? 0 : 1;
    }

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

    /* IF_MOVESUB (op 42, 8 bytes): dest then source, each p4Alt1 (LE). */
    case PKT_NAME_IF_MOVESUB:
        if( len < 8 )
            return 0;
        out->_if_movesub.dest_uid =
            data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
        out->_if_movesub.source_uid =
            data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
        return 1;

    /*
     * The IF_SET* family.
     *
     * rev 230 addresses a component as a packed (interface << 16) | child uid,
     * where lc254 uses a flat p2 component id — so the shared parser's g2 would
     * read half of the interface number and then misalign every field after it.
     * The client stores the uid in `component_id` unchanged; UITree at this
     * revision is packed-uid addressed throughout.
     */
    case PKT_NAME_IF_SETTEXT:
        if( len < 5 )
            return 0;
        out->_if_settext.component_id =
            (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
        {
            int text_len = len - 4;
            char* text = (char*)malloc((size_t)text_len + 1);

            assert(text);
            /* Newline-terminated on the wire, like every other Jagex string. */
            memcpy(text, data + 4, (size_t)text_len);
            text[text_len] = '\0';
            for( int i = 0; i < text_len; i++ )
            {
                if( text[i] == '\n' )
                {
                    text[i] = '\0';
                    break;
                }
            }
            out->_if_settext.text = text;
        }
        return 1;

    case PKT_NAME_IF_SETNPCHEAD:
        if( len < 6 )
            return 0;
        out->_if_setnpchead.component_id =
            (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
        out->_if_setnpchead.npc_id = (data[4] << 8) | data[5];
        return 1;

    case PKT_NAME_IF_SETPLAYERHEAD:
        if( len < 4 )
            return 0;
        out->_if_setplayerhead.component_id =
            (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
        return 1;

    case PKT_NAME_IF_SETANIM:
        if( len < 6 )
            return 0;
        out->_if_setanim.component_id =
            (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
        out->_if_setanim.anim_id = (data[4] << 8) | data[5];
        return 1;

    case PKT_NAME_IF_SETHIDE:
        if( len < 5 )
            return 0;
        out->_if_sethide.component_id =
            (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
        out->_if_sethide.hide = data[4];
        return 1;

    /* IF_SETEVENTS (op 47, 12 bytes). RSProt IfSetEventsEncoder writes
     * p4Alt3 combinedId, p2Alt2 start, p4Alt1 events, p2 end — four different
     * byte orders in one packet, which is normal for Jagex and is why this
     * cannot go through the shared parser. */
    case PKT_NAME_IF_SETEVENTS:
        if( len < 12 )
            return 0;
        out->_if_setevents.component_id =
            (data[1] << 24) | (data[0] << 16) | (data[3] << 8) | data[2];
        out->_if_setevents.from = (data[4] << 8) | ((data[5] - 128) & 0xff);
        out->_if_setevents.events =
            data[6] | (data[7] << 8) | (data[8] << 16) | (data[9] << 24);
        out->_if_setevents.to = (data[10] << 8) | data[11];
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
        RSProt_Buffer cur;
        RSProt_BufferWrapRead(&cur, data, len);
        int capacity;
        out->_update_inv_full.component_id = RSProt_BufferG4Be(&cur);
        out->_update_inv_full.inv_id = RSProt_BufferG2Be(&cur);
        capacity = RSProt_BufferG2Be(&cur);
        if( cur.err || capacity < 0 || capacity > 4096 )
            return 0;
        out->_update_inv_full.size = capacity;
        out->_update_inv_full.obj_ids = malloc((size_t)capacity * sizeof(int));
        out->_update_inv_full.obj_counts = malloc((size_t)capacity * sizeof(int));
        assert(out->_update_inv_full.obj_ids);
        assert(out->_update_inv_full.obj_counts);
        for( int i = 0; i < capacity; i++ )
            osrs230_read_inv_slot(
                &cur, &out->_update_inv_full.obj_ids[i], &out->_update_inv_full.obj_counts[i]);
        if( cur.err )
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
        RSProt_Buffer cur;
        RSProt_BufferWrapRead(&cur, data, len);
        int max_entries;
        int written = 0;
        out->_update_inv_partial.component_id = RSProt_BufferG4Be(&cur);
        out->_update_inv_partial.inv_id = RSProt_BufferG2Be(&cur);
        out->_update_inv_partial.count = 0;
        out->_update_inv_partial.entries = NULL;
        if( cur.err )
            return 0;
        /* Smallest record is 4 bytes (1 slot + 1 count + 2 obj). */
        max_entries = (len - cur.rpos) / 4 + 1;
        out->_update_inv_partial.entries = (struct PktUpdateInvPartialEntry*)malloc(
            (size_t)max_entries * sizeof(struct PktUpdateInvPartialEntry));
        if( !out->_update_inv_partial.entries )
            return 0;
        while( cur.rpos < len && written < max_entries )
        {
            struct PktUpdateInvPartialEntry* entry = &out->_update_inv_partial.entries[written];
            entry->slot = RSProt_BufferGSmart1or2(&cur);
            osrs230_read_inv_slot(&cur, &entry->obj_id, &entry->count);
            if( cur.err )
                break;
            written++;
        }
        if( cur.err || cur.rpos != len )
        {
            fprintf(
                stderr,
                "osrs230: UPDATE_INV_PARTIAL inv=%d did not consume its %d-byte frame "
                "(stopped at %d after %d slots); dropped\n",
                out->_update_inv_partial.inv_id,
                len,
                cur.rpos,
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
        /* The leading byte is the chat type. This revision's packet has no
         * optional sender field, so `name` stays NULL. */
        out->_message_game.type = data[0];
        out->_message_game.text = malloc((size_t)text_len + 1);
        assert(out->_message_game.text);
        memcpy(out->_message_game.text, data + 1, (size_t)text_len);
        out->_message_game.text[text_len] = '\0';
        return 1;
    }

    /* UPDATE_STAT_V2 (op 114, 7 bytes): p1 stat, p1 base level, p4 xp, p1
     * boosted level. The lc254 layout is 6 bytes in a different order, and its
     * parser asserts the frame is fully consumed. */
    case PKT_NAME_UPDATE_STAT:
    {
        RSProt_Buffer cur;
        RSProt_BufferWrapRead(&cur, data, len);
        int base;

        out->_update_stat.stat = RSProt_BufferG1(&cur);
        base = RSProt_BufferG1(&cur);
        out->_update_stat.xp = RSProt_BufferG4Be(&cur);
        /*
         * The BOOSTED level is what `.level` carries, not the base one.
         *
         * RS_GameProtoExec writes it straight into `stats->current_level`,
         * having just let RS_PlayerStats_SetXp derive `base_level` from the xp
         * — so the base level on the wire is redundant and the boosted one is
         * the field with a consumer. The health orb reads current_level for
         * hitpoints, which is the same number as the player's hitpoints; sending
         * the base here pins the orb at full health for the whole session.
         */
        out->_update_stat.level = RSProt_BufferG1(&cur);
        (void)base;
        return cur.err ? 0 : 1;
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

    /*
     * MidiSongV2Encoder v5 (rev 230): p2Alt2 fadeOutDelay, p2Alt2 fadeOutSpeed,
     * p2 fadeInDelay, p2 id, p2Alt3 fadeInSpeed.
     *
     * Needed here rather than in the shared reader because that one is the
     * pre-V2 two-byte form -- it reads the id straight off the front and
     * asserts the frame is consumed, which on a 10-byte V2 payload gives a
     * garbage song id and trips the assert. Until this existed, music could not
     * play on this revision at all.
     */
    case PKT_NAME_MIDI_SONG:
    {
        RSProt_Buffer cur;
        int fade_out_speed;
        int fade_in_speed;

        RSProt_BufferWrapRead(&cur, data, len);
        (void)RSProt_BufferG2Be_add128(&cur); /* fade-out delay */
        fade_out_speed = RSProt_BufferG2Be_add128(&cur);
        (void)RSProt_BufferG2Be(&cur); /* fade-in delay */
        out->_midi_song.id = RSProt_BufferG2Be(&cur);
        fade_in_speed = RSProt_BufferG2Le_add128(&cur);
        if( out->_midi_song.id == 65535 )
            out->_midi_song.id = -1;
        /* Speeds are client cycles; the player works in milliseconds. */
        out->_midi_song.fade_out_ms = fade_out_speed * 20;
        out->_midi_song.fade_in_ms = fade_in_speed * 20;
        return cur.err ? 0 : 1;
    }

    default:
        return -1; /* fall back to the shared gameproto_parse */
    }
}
