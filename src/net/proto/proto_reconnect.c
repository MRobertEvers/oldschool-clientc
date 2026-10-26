/*
 * The local index after each RECONNECT in an RSProx recording.
 *
 * A reconnect proper keeps the index (the deob's RECONNECT_OK path reads only
 * the GPI block), and RSProx's ReconnectDecoder re-seeds under the session's.
 * A world hop does not: the new world assigns a new slot, in a LOGIN_OK the
 * proxy does not record -- it writes the RECONNECT pseudo-packet instead. The
 * stream still parses under the old index (the bit sections walk the same
 * counts), so nothing fails until a player whose slot lies between the old
 * and new one enters view, and then every PLAYER_INFO does: recording 4282
 * (rev 227) hops at tick 1579 from 177 to 163 and RSProx's own transcript loses
 * PLAYER_INFO from tick 2470 to the end.
 *
 * The new slot is taken from the camera: the first CAM_TARGET after the
 * RECONNECT that targets a player. It is kept only when PLAYER_INFO decodes
 * whole under it up to the next RECONNECT; otherwise RSProx's rule stands, and
 * the line says so.
 */
#include "net/rsprox_recording.h"
#include "net/rev/gameproto_parse.h" /* gameproto_free */
#include "net/rev/gameproto_revisions.h"
#include "streams/rsprot_info.h"

#include <assert.h>
#include <string.h>

#include "log/torirs_log.h"

/* The player the camera targets at frame `fr`, or -1 when it is not a
 * CAM_TARGET or targets anything else (type 0 is a player at every version). */
static int
camera_player(struct GameProtoRevTable const* rev, struct RsproxRecording const* rec,
              struct RsproxRecordingFrame const* fr)
{
    struct RevPacket pkt;
    int index = -1;

    if( rev->packetin_code(fr->opcode) != PKT_NAME_CAM_TARGET )
        return -1;
    memset(&pkt, 0, sizeof(pkt));
    if( rev->parse_wire(rev, fr->opcode, rec->data + fr->offset, (int)fr->length, &pkt) == 1 &&
        pkt.packet_type == PKT_NAME_CAM_TARGET && pkt._cam_target.type == 0 )
        index = pkt._cam_target.index;
    gameproto_free(&pkt);
    return index;
}

/* Whether PLAYER_INFO reads whole from `from` up to `to` with the table
 * re-seeded from the RECONNECT at `from` under `index`. */
static int
decodes_under(
    struct GameProtoRevTable const* rev,
    struct RsproxRecording const* rec,
    int from,
    int to,
    int index)
{
    static RsprotInfoOp ops[8192];
    static uint8_t bytes[1 << 16];
    struct RsproxRecordingFrame const* re = &rec->frames[from];
    RsprotInfoList list;

    rsprot_info_list_init(&list, ops, (int)(sizeof(ops) / sizeof(ops[0])), bytes, sizeof(bytes));

    rev->player_info_set_local(index);
    rev->player_info_init(rec->data + re->offset, (int)re->length);
    for( int f = from + 1; f < to; f++ )
    {
        struct RsproxRecordingFrame const* fr = &rec->frames[f];

        if( fr->opcode == RSPROX_RECORDING_RECONNECT ||
            fr->opcode == RSPROX_RECORDING_PACKET_GROUP_END ||
            rev->packetin_code(fr->opcode) != PKT_NAME_PLAYER_INFO )
            continue;
        rev->player_info_read(rec->data + fr->offset, (int)fr->length, &list);
        if( !rsprot_info_list_complete(&list) )
            return 0;
    }
    return 1;
}

int
rsprox_recording_resolve_reconnects(
    struct GameProtoRevTable const* rev, struct RsproxRecording* rec)
{
    int current = rec->local_player_index;
    int moved = 0;

    assert(rec);
    for( int f = 0; f < rec->frame_count; f++ )
    {
        struct RsproxRecordingFrame* re = &rec->frames[f];
        int next = f + 1;
        int camera = -1;

        if( re->opcode != RSPROX_RECORDING_RECONNECT )
            continue;
        while( next < rec->frame_count && rec->frames[next].opcode != RSPROX_RECORDING_RECONNECT )
            next++;
        for( int k = f + 1; k < next && camera < 0; k++ )
        {
            struct RsproxRecordingFrame const* fr = &rec->frames[k];

            if( fr->opcode == RSPROX_RECORDING_PACKET_GROUP_END )
                continue;
            camera = camera_player(rev, rec, fr);
        }
        re->reconnect_index = current;
        if( camera <= 0 || camera == current )
            continue;
        if( decodes_under(rev, rec, f, next, camera) )
        {
            TORIRS_REPORT("rsprox: the RECONNECT at tick %d continues as player %d, not %d (the "
                       "camera's target; PLAYER_INFO reads whole under it)\n",
                       re->tick, camera, current);
            re->reconnect_index = camera;
            current = camera;
            moved++;
        }
        else
            TORIRS_REPORT("rsprox: the RECONNECT at tick %d: the camera targets player %d, but "
                       "PLAYER_INFO does not read under it; keeping %d\n",
                       re->tick, camera, current);
    }
    return moved;
}
