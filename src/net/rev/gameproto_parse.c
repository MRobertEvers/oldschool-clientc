#include "gameproto_parse.h"

#include "net/wordpack.h"

#include <assert.h>
#include <rsbuffer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
parse_debug(void)
{
    static int cached = -1;
    if( cached < 0 )
        cached = getenv("TORIRS_NET_DEBUG") != NULL;
    return cached;
}

#define PARSE_LOG(...)                 \
    do                                 \
    {                                  \
        if( parse_debug() )            \
            fprintf(stderr, __VA_ARGS__); \
    } while( 0 )

/*
 * Zone sub-packet readers, shared by the top-level cases (follows mode) and
 * the UPDATE_ZONE_PARTIAL_ENCLOSED stream walk.
 */

static void
read_loc_add_change(struct RSCache_Buffer* b, struct PktLocAddChange* p)
{
    p->pos = g1(b);
    p->info = g1(b);
    p->loc_id = g2(b);
}

static void
read_loc_del(struct RSCache_Buffer* b, struct PktLocDel* p)
{
    p->pos = g1(b);
    p->info = g1(b);
}

static void
read_loc_anim(struct RSCache_Buffer* b, struct PktLocAnim* p)
{
    p->pos = g1(b);
    p->info = g1(b);
    p->seq_id = g2(b);
}

static void
read_obj_add(struct RSCache_Buffer* b, struct PktObjAdd* p)
{
    p->pos = g1(b);
    p->obj_id = g2(b);
    p->count = g2(b);
}

static void
read_obj_del(struct RSCache_Buffer* b, struct PktObjDel* p)
{
    p->pos = g1(b);
    p->obj_id = g2(b);
}

static void
read_obj_reveal(struct RSCache_Buffer* b, struct PktObjReveal* p)
{
    p->pos = g1(b);
    p->obj_id = g2(b);
    p->count = g2(b);
    p->receiver = g2(b);
}

static void
read_obj_count(struct RSCache_Buffer* b, struct PktObjCount* p)
{
    p->pos = g1(b);
    p->obj_id = g2(b);
    p->old_count = g2(b);
    p->new_count = g2(b);
}

static void
read_loc_merge(struct RSCache_Buffer* b, struct PktLocMerge* p)
{
    p->pos = g1(b);
    p->info = g1(b);
    p->loc_id = g2(b);
    p->start = g2(b);
    p->end = g2(b);
    p->pid = g2(b);
    p->east = g1b(b);
    p->south = g1b(b);
    p->west = g1b(b);
    p->north = g1b(b);
}

static void
read_map_projanim(struct RSCache_Buffer* b, struct PktMapProjAnim* p)
{
    p->pos = g1(b);
    p->dx_offset = g1b(b);
    p->dz_offset = g1b(b);
    p->target = g2b(b);
    p->spotanim = g2(b);
    p->src_height = g1(b);
    p->dst_height = g1(b);
    p->start_delay = g2(b);
    p->end_delay = g2(b);
    p->peak = g1(b);
    p->arc = g1(b);
}

static void
read_map_anim(struct RSCache_Buffer* b, struct PktMapAnim* p)
{
    p->pos = g1(b);
    p->id = g2(b);
    p->height = g1(b);
    p->delay = g2(b);
}

/* Decode one zone sub-packet by canonical name into `out`.
 * Returns 1 on success, 0 for a name that is not a zone sub-packet. */
static int
read_zone_sub_packet(
    struct RSCache_Buffer* b,
    enum GameProtoPktName name,
    struct PktZoneSubPacket* out)
{
    out->name = name;
    switch( name )
    {
    case PKT_NAME_LOC_ADD_CHANGE:
        read_loc_add_change(b, &out->_loc_add_change);
        return 1;
    case PKT_NAME_LOC_DEL:
        read_loc_del(b, &out->_loc_del);
        return 1;
    case PKT_NAME_LOC_ANIM:
        read_loc_anim(b, &out->_loc_anim);
        return 1;
    case PKT_NAME_OBJ_ADD:
        read_obj_add(b, &out->_obj_add);
        return 1;
    case PKT_NAME_OBJ_DEL:
        read_obj_del(b, &out->_obj_del);
        return 1;
    case PKT_NAME_OBJ_REVEAL:
        read_obj_reveal(b, &out->_obj_reveal);
        return 1;
    case PKT_NAME_OBJ_COUNT:
        read_obj_count(b, &out->_obj_count);
        return 1;
    case PKT_NAME_LOC_MERGE:
        read_loc_merge(b, &out->_loc_merge);
        return 1;
    case PKT_NAME_MAP_PROJANIM:
        read_map_projanim(b, &out->_map_projanim);
        return 1;
    case PKT_NAME_MAP_ANIM:
        read_map_anim(b, &out->_map_anim);
        return 1;
    default:
        return 0;
    }
}

int
gameproto_parse(
    struct GameProtoRevTable const* rev,
    enum GameProtoPktName pkt_name,
    uint8_t* data,
    int data_size,
    struct RevPacket* packet)
{
    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, data, data_size);

    packet->packet_type = pkt_name;

    switch( pkt_name )
    {
    case PKT_NAME_REBUILD_NORMAL:
    {
        PARSE_LOG("parse: REBUILD_NORMAL\n");
        packet->_map_rebuild.zonex = g2(&buffer);
        packet->_map_rebuild.zonez = g2(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_NPC_INFO:
    {
        uint8_t* commandstream = malloc(data_size);
        memcpy(commandstream, data, data_size);
        packet->_npc_info.length = data_size;
        packet->_npc_info.data = commandstream;
        return 1;
    }
    case PKT_NAME_PLAYER_INFO:
    {
        uint8_t* commandstream = malloc(data_size);
        memcpy(commandstream, data, data_size);
        packet->_player_info.length = data_size;
        packet->_player_info.data = commandstream;
        return 1;
    }
    case PKT_NAME_UPDATE_INV_FULL:
    {
        packet->_update_inv_full.component_id = g2(&buffer);
        packet->_update_inv_full.inv_id = -1; /* component id is the container key */
        packet->_update_inv_full.size = g1(&buffer);

        packet->_update_inv_full.obj_ids = malloc(packet->_update_inv_full.size * sizeof(int));
        packet->_update_inv_full.obj_counts = malloc(packet->_update_inv_full.size * sizeof(int));

        for( int i = 0; i < packet->_update_inv_full.size; i++ )
        {
            /* Wire carries obj id + 1, 0 = empty (reference stores the raw
             * value in linkObjType and every consumer reads linkObjType - 1).
             * PARTIAL below already subtracts; without the -1 here every item
             * shifts onto its neighbour id — which is usually its bank note. */
            packet->_update_inv_full.obj_ids[i] = g2(&buffer) - 1;

            int count = g1(&buffer);
            if( count == 255 )
            {
                count = g4(&buffer);
            }
            packet->_update_inv_full.obj_counts[i] = count;
        }

        return 1;
    }
    case PKT_NAME_IF_SETTAB:
    {
        packet->_if_settab.component_id = g2(&buffer);
        packet->_if_settab.tab_id = g1(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_IF_OPENCHAT:
    {
        packet->_if_openchat.component_id = g2(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_IF_CLOSE:
    {
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_IF_SETTAB_ACTIVE:
    {
        packet->_if_settab_active.tab_id = g1(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_VARP_SMALL:
    {
        packet->_varp_small.variable = g2(&buffer);
        packet->_varp_small.value = g1b(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_VARP_LARGE:
    {
        packet->_varp_large.variable = g2(&buffer);
        packet->_varp_large.value = g4(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_VARP_SYNC:
        /* Restore client vars from the server-authoritative set. No payload. */
        return 1;
    case PKT_NAME_VARP_RESET:
        /* Zero both copies. No payload. */
        return 1;
    case PKT_NAME_UPDATE_STAT:
    {
        packet->_update_stat.stat = g1(&buffer);
        packet->_update_stat.xp = g4(&buffer);
        packet->_update_stat.level = g1(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_UPDATE_RUNENERGY:
    {
        packet->_update_run_energy.run_energy = g1(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_IF_SETCOLOUR:
    {
        packet->_if_setcolour.component_id = g2(&buffer);
        packet->_if_setcolour.colour = g2(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_IF_SETHIDE:
    {
        packet->_if_sethide.component_id = g2(&buffer);
        packet->_if_sethide.hide = g1(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_IF_SETOBJECT:
    {
        packet->_if_setobject.component_id = g2(&buffer);
        packet->_if_setobject.obj_id = g2(&buffer);
        packet->_if_setobject.zoom = g2(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_IF_SETMODEL:
    {
        packet->_if_setmodel.component_id = g2(&buffer);
        packet->_if_setmodel.model_id = g2(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_IF_SETANIM:
    {
        packet->_if_setanim.component_id = g2(&buffer);
        packet->_if_setanim.anim_id = g2(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_IF_SETPLAYERHEAD:
    {
        packet->_if_setplayerhead.component_id = g2(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_IF_SETTEXT:
    {
        packet->_if_settext.component_id = g2(&buffer);
        packet->_if_settext.text = gstringnewline(&buffer);
        return 1;
    }
    case PKT_NAME_IF_SETNPCHEAD:
    {
        packet->_if_setnpchead.component_id = g2(&buffer);
        packet->_if_setnpchead.npc_id = g2(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_IF_SETPOSITION:
    {
        packet->_if_setposition.component_id = g2(&buffer);
        packet->_if_setposition.x = g2b(&buffer);
        packet->_if_setposition.z = g2b(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_IF_SETSCROLLPOS:
    {
        packet->_if_setscrollpos.component_id = g2(&buffer);
        packet->_if_setscrollpos.pos = g2(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_MESSAGE_GAME:
    {
        packet->_message_game.text = gstringnewline(&buffer);
        return 1;
    }
    case PKT_NAME_MESSAGE_PRIVATE:
    {
        if( data_size < 13 )
            return 0;
        packet->_message_private.from = g8(&buffer);
        packet->_message_private.message_id = g4(&buffer);
        packet->_message_private.staff_mod = g1(&buffer);
        packet->_message_private.text = wordpack_unpack(&buffer, data_size - 13);
        if( !packet->_message_private.text )
            return 0;
        return 1;
    }
    case PKT_NAME_CHAT_FILTER_SETTINGS:
    {
        packet->_chat_filter_settings.chat_public_mode = g1(&buffer);
        packet->_chat_filter_settings.chat_private_mode = g1(&buffer);
        packet->_chat_filter_settings.chat_trade_mode = g1(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_UPDATE_ZONE_PARTIAL_FOLLOWS:
    {
        packet->_update_zone_partial_follows.base_x = g1(&buffer);
        packet->_update_zone_partial_follows.base_z = g1(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS:
    {
        packet->_update_zone_full_follows.base_x = g1(&buffer);
        packet->_update_zone_full_follows.base_z = g1(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED:
    {
        /* Base zone origin, then a stream of zone sub-packets whose opcodes
         * are plain (non-ISAAC) wire bytes resolved through the rev table. */
        struct PktUpdateZoneEnclosed* enc = &packet->_update_zone_enclosed;
        int cap = 16;

        enc->base_x = g1(&buffer);
        enc->base_z = g1(&buffer);
        enc->count = 0;
        enc->entries = malloc(cap * sizeof(*enc->entries));

        while( buffer.position < data_size )
        {
            int wire = g1(&buffer);
            enum GameProtoPktName sub = (enum GameProtoPktName)rev->packetin_code(wire);

            if( enc->count == cap )
            {
                cap *= 2;
                enc->entries = realloc(enc->entries, cap * sizeof(*enc->entries));
            }
            if( !read_zone_sub_packet(&buffer, sub, &enc->entries[enc->count]) )
            {
                /* Unknown sub-opcode: the rest of the stream is unreadable. */
                PARSE_LOG(
                    "parse: ZONE_ENCLOSED unknown sub-opcode %d at %d/%d\n",
                    wire,
                    buffer.position,
                    data_size);
                break;
            }
            enc->count++;
        }
        return 1;
    }
    case PKT_NAME_LOC_ADD_CHANGE:
    {
        read_loc_add_change(&buffer, &packet->_loc_add_change);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_LOC_DEL:
    {
        read_loc_del(&buffer, &packet->_loc_del);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_LOC_ANIM:
    {
        read_loc_anim(&buffer, &packet->_loc_anim);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_OBJ_ADD:
    {
        read_obj_add(&buffer, &packet->_obj_add);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_OBJ_DEL:
    {
        read_obj_del(&buffer, &packet->_obj_del);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_OBJ_REVEAL:
    {
        read_obj_reveal(&buffer, &packet->_obj_reveal);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_OBJ_COUNT:
    {
        read_obj_count(&buffer, &packet->_obj_count);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_LOC_MERGE:
    {
        read_loc_merge(&buffer, &packet->_loc_merge);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_MAP_PROJANIM:
    {
        read_map_projanim(&buffer, &packet->_map_projanim);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_MAP_ANIM:
    {
        read_map_anim(&buffer, &packet->_map_anim);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_CAM_LOOKAT:
    {
        /* p1 x, p1 z, p2 height, p1 rate, p1 rate2 — six bytes, but not three
         * u16s. Read as u16 the whole thing slides: x and z fuse into one
         * number, height lands in z, and the two rates fuse into height, which
         * puts the camera somewhere off the map and leaves the shot unchanged. */
        packet->_cam_lookat.local_x = g1(&buffer);
        packet->_cam_lookat.local_z = g1(&buffer);
        packet->_cam_lookat.height = g2(&buffer);
        packet->_cam_lookat.rate = g1(&buffer);
        packet->_cam_lookat.rate2 = g1(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_CAM_MOVETO:
    {
        /* p1 x, p1 z, p2 height, p1 rate, p1 rate2 — six bytes, but not three
         * u16s. Read as u16 the whole thing slides: x and z fuse into one
         * number, height lands in z, and the two rates fuse into height, which
         * puts the camera somewhere off the map and leaves the shot unchanged. */
        packet->_cam_moveto.local_x = g1(&buffer);
        packet->_cam_moveto.local_z = g1(&buffer);
        packet->_cam_moveto.height = g2(&buffer);
        packet->_cam_moveto.rate = g1(&buffer);
        packet->_cam_moveto.rate2 = g1(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_CAM_SHAKE:
    {
        packet->_cam_shake.axis = g1(&buffer);
        packet->_cam_shake.amplitude = g1(&buffer);
        packet->_cam_shake.frequency = g1(&buffer);
        packet->_cam_shake.speed = g1(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_CAM_RESET:
        /* no payload */
        return 1;
    case PKT_NAME_IF_OPENMAIN:
    {
        packet->_if_openmain.component_id = g2(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_IF_OPENSIDE:
    {
        packet->_if_openside.component_id = g2(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_IF_OPENOVERLAY:
    {
        packet->_if_openoverlay.component_id = g2(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_IF_OPENMAIN_SIDE:
    {
        packet->_if_openmain_side.main_component_id = g2(&buffer);
        packet->_if_openmain_side.side_component_id = g2(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_HINT_ARROW:
    {
        /* Fixed 6 bytes; the trailing fields are padding for entity types
         * (see PktHintArrow). */
        packet->_hint_arrow.type = g1(&buffer);
        packet->_hint_arrow.id = g2(&buffer);
        packet->_hint_arrow.z = g2(&buffer);
        packet->_hint_arrow.height = g1(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_RESET_ANIMS:
        /* no payload */
        return 1;
    case PKT_NAME_UPDATE_PID:
    {
        packet->_update_pid.local_player_index = g2(&buffer);
        packet->_update_pid.unused = g1(&buffer); /* members flag */
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_UPDATE_RUNWEIGHT:
    {
        packet->_update_runweight.run_weight = g2b(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_UNSET_MAP_FLAG:
        /* osrs230 SET_MAP_FLAG is 2 bytes (x, z) with 255,255 = clear. lc254
         * UNSET_MAP_FLAG is 0 bytes and always clears. */
        if( data_size >= 2 )
        {
            packet->_set_map_flag.x = g1(&buffer);
            packet->_set_map_flag.z = g1(&buffer);
            packet->_set_map_flag.clear =
                (packet->_set_map_flag.x == 255 && packet->_set_map_flag.z == 255);
            assert(buffer.position == data_size);
        }
        else
        {
            packet->_set_map_flag.x = 255;
            packet->_set_map_flag.z = 255;
            packet->_set_map_flag.clear = 1;
        }
        return 1;
    case PKT_NAME_UPDATE_INV_STOP_TRANSMIT:
    {
        packet->_update_inv_stop_transmit.component_id = g2(&buffer);
        packet->_update_inv_stop_transmit.inv_id = -1;
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_UPDATE_INV_PARTIAL:
    {
        packet->_update_inv_partial.component_id = g2(&buffer);
        packet->_update_inv_partial.inv_id = -1;
        /* count is derived from remaining bytes; allocate conservatively */
        int max_entries = (data_size - 2) / 5 + 1;
        packet->_update_inv_partial.entries =
            (struct PktUpdateInvPartialEntry*)malloc(max_entries * sizeof(struct PktUpdateInvPartialEntry));
        int n = 0;
        while( buffer.position < data_size )
        {
            int slot = g1(&buffer);
            int obj_id_raw = g2(&buffer);
            int count = g1(&buffer);
            if( count == 255 )
                count = g4(&buffer);
            packet->_update_inv_partial.entries[n].slot = slot;
            packet->_update_inv_partial.entries[n].obj_id = obj_id_raw - 1;
            packet->_update_inv_partial.entries[n].count = count;
            n++;
        }
        packet->_update_inv_partial.count = n;
        return 1;
    }
    case PKT_NAME_SET_MULTIWAY:
    {
        packet->_set_multiway.multiway = g1(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_UPDATE_IGNORELIST:
    {
        struct PktUpdateIgnoreList* ig = &packet->_update_ignorelist;
        ig->count = data_size / 8;
        ig->names37 = ig->count > 0 ? malloc(ig->count * sizeof(int64_t)) : NULL;
        for( int i = 0; i < ig->count; i++ )
            ig->names37[i] = g8(&buffer);
        return 1;
    }
    case PKT_NAME_UPDATE_FRIENDLIST:
    {
        packet->_update_friendlist.name37 = g8(&buffer);
        packet->_update_friendlist.world = g1(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_FRIENDLIST_LOADED:
    {
        packet->_friendlist_loaded.status = g1(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_UPDATE_REBOOT_TIMER:
    {
        packet->_update_reboot_timer.ticks = g2(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_LAST_LOGIN_INFO:
    {
        packet->_last_login_info.last_ip = g4(&buffer);
        packet->_last_login_info.days_since_login = g2(&buffer);
        packet->_last_login_info.days_since_recovery = g1(&buffer);
        packet->_last_login_info.unread_messages = g2(&buffer);
        packet->_last_login_info.member_warning = g1(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_LOGOUT:
    case PKT_NAME_P_COUNTDIALOG:
    case PKT_NAME_FINISH_TRACKING:
    case PKT_NAME_ENABLE_TRACKING:
        /* no payload */
        return 1;
    case PKT_NAME_TUT_FLASH:
    {
        packet->_tut_flash.tab = g1(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_TUT_OPEN:
    {
        packet->_tut_open.component_id = g2(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_SYNTH_SOUND:
    {
        packet->_synth_sound.id = g2(&buffer);
        packet->_synth_sound.loops = g1(&buffer);
        packet->_synth_sound.delay = g2(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_MIDI_SONG:
    {
        packet->_midi_song.id = g2(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_MIDI_JINGLE:
    {
        packet->_midi_jingle.id = g2(&buffer);
        packet->_midi_jingle.delay = g2(&buffer);
        assert(buffer.position == data_size);
        return 1;
    }
    case PKT_NAME_SET_PLAYER_OP:
    {
        packet->_set_player_op.slot = g1(&buffer);
        packet->_set_player_op.primary = g1(&buffer);
        packet->_set_player_op.text = gstringnewline(&buffer);
        return 1;
    }
    default:
        PARSE_LOG("parse: unknown packet name %d\n", pkt_name);
        break;
    }

    return 0;
}

void
gameproto_free(struct RevPacket* p)
{
    if( !p )
        return;

    switch( p->packet_type )
    {
    case PKT_NAME_NPC_INFO:
        free(p->_npc_info.data);
        p->_npc_info.data = NULL;
        p->_npc_info.length = 0;
        break;
    case PKT_NAME_PLAYER_INFO:
        free(p->_player_info.data);
        p->_player_info.data = NULL;
        p->_player_info.length = 0;
        break;
    case PKT_NAME_REBUILD_NORMAL:
    case PKT_NAME_REBUILD_REGION:
        /* dat2/osrs230 XTEA keys (NULL on the lc254 path), and the instanced
         * descriptor grid (NULL for REBUILD_NORMAL). */
        free(p->_map_rebuild.region_ids);
        free(p->_map_rebuild.region_keys);
        free(p->_map_rebuild.zones);
        p->_map_rebuild.region_ids = NULL;
        p->_map_rebuild.region_keys = NULL;
        p->_map_rebuild.zones = NULL;
        p->_map_rebuild.region_count = 0;
        break;
    case PKT_NAME_UPDATE_INV_FULL:
        free(p->_update_inv_full.obj_ids);
        free(p->_update_inv_full.obj_counts);
        p->_update_inv_full.obj_ids = NULL;
        p->_update_inv_full.obj_counts = NULL;
        p->_update_inv_full.size = 0;
        break;
    case PKT_NAME_IF_SETTEXT:
        free(p->_if_settext.text);
        p->_if_settext.text = NULL;
        break;
    case PKT_NAME_MESSAGE_GAME:
        free(p->_message_game.text);
        p->_message_game.text = NULL;
        break;
    case PKT_NAME_MESSAGE_PRIVATE:
        free(p->_message_private.text);
        p->_message_private.text = NULL;
        break;
    case PKT_NAME_UPDATE_INV_PARTIAL:
        free(p->_update_inv_partial.entries);
        p->_update_inv_partial.entries = NULL;
        p->_update_inv_partial.count = 0;
        break;
    case PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED:
        free(p->_update_zone_enclosed.entries);
        p->_update_zone_enclosed.entries = NULL;
        p->_update_zone_enclosed.count = 0;
        break;
    case PKT_NAME_UPDATE_IGNORELIST:
        free(p->_update_ignorelist.names37);
        p->_update_ignorelist.names37 = NULL;
        p->_update_ignorelist.count = 0;
        break;
    case PKT_NAME_SET_PLAYER_OP:
        free(p->_set_player_op.text);
        p->_set_player_op.text = NULL;
        break;
    default:
        break;
    }
}
