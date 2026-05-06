#include "serverprot_core_parse.h"

#include "osrs/core/serverprot_packets.h"
#include "osrs/rscache/rsbuf.h"
#include "osrs/wordpack.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

int
serverprot_core_parse_maprebuild_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.map_rebuild_v1.zonex = g2(&b);
    out->u.map_rebuild_v1.zonez = g2(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_update_zone_partial_follows_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.update_zone_partial_follows_v1.base_x = g1(&b);
    out->u.update_zone_partial_follows_v1.base_z = g1(&b);

    return 1;
}
int
serverprot_core_parse_update_zone_full_follows_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.update_zone_full_follows_v1.base_x = g1(&b);
    out->u.update_zone_full_follows_v1.base_z = g1(&b);

    return 1;
}
int
serverprot_core_parse_loc_add_change_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.loc_add_change_v1.pos = g1(&b);
    out->u.loc_add_change_v1.info = g1(&b);
    out->u.loc_add_change_v1.loc_id = g2(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_loc_del_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.loc_del_v1.pos = g1(&b);
    out->u.loc_del_v1.info = g1(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_loc_anim_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.loc_anim_v1.pos = g1(&b);
    out->u.loc_anim_v1.info = g1(&b);
    out->u.loc_anim_v1.seq_id = g2(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_loc_merge_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.loc_merge_v1.pos = g1(&b);
    out->u.loc_merge_v1.info = g1(&b);
    out->u.loc_merge_v1.loc_id = g2(&b);
    out->u.loc_merge_v1.start = g2(&b);
    out->u.loc_merge_v1.end = g2(&b);
    out->u.loc_merge_v1.pid = g2(&b);
    out->u.loc_merge_v1.east = g1b(&b);
    out->u.loc_merge_v1.south = g1b(&b);
    out->u.loc_merge_v1.west = g1b(&b);
    out->u.loc_merge_v1.north = g1b(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_obj_add_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.obj_add_v1.pos = g1(&b);
    out->u.obj_add_v1.obj_id = g2(&b);
    out->u.obj_add_v1.count = g2(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_obj_del_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.obj_del_v1.pos = g1(&b);
    out->u.obj_del_v1.obj_id = g2(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_obj_reveal_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.obj_reveal_v1.pos = g1(&b);
    out->u.obj_reveal_v1.obj_id = g2(&b);
    out->u.obj_reveal_v1.count = g2(&b);
    out->u.obj_reveal_v1.receiver = g2(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_obj_count_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.obj_count_v1.pos = g1(&b);
    out->u.obj_count_v1.obj_id = g2(&b);
    out->u.obj_count_v1.old_count = g2(&b);
    out->u.obj_count_v1.new_count = g2(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_map_projanim_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.map_projanim_v1.pos = g1(&b);
    out->u.map_projanim_v1.dx_offset = g1b(&b);
    out->u.map_projanim_v1.dz_offset = g1b(&b);
    out->u.map_projanim_v1.target = g2b(&b);
    out->u.map_projanim_v1.spotanim = g2(&b);
    out->u.map_projanim_v1.src_height = g1(&b);
    out->u.map_projanim_v1.dst_height = g1(&b);
    out->u.map_projanim_v1.start_delay = g2(&b);
    out->u.map_projanim_v1.end_delay = g2(&b);
    out->u.map_projanim_v1.peak = g1(&b);
    out->u.map_projanim_v1.arc = g1(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_map_anim_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.map_anim_v1.pos = g1(&b);
    out->u.map_anim_v1.id = g2(&b);
    out->u.map_anim_v1.height = g1(&b);
    out->u.map_anim_v1.delay = g2(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_if_settab_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
        out->u.if_settab_v1.component_id = g2(&b);
        out->u.if_settab_v1.tab_id = g1(&b);
    assert(b.position == n);
    return 1;
}
int
serverprot_core_parse_if_setcolour_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
        out->u.if_setcolour_v1.component_id = g2(&b);
        out->u.if_setcolour_v1.colour = g2(&b);
    assert(b.position == n);
    return 1;
}
int
serverprot_core_parse_if_sethide_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
        out->u.if_sethide_v1.component_id = g2(&b);
        out->u.if_sethide_v1.hide = g1(&b);
    assert(b.position == n);
    return 1;
}
int
serverprot_core_parse_if_setobject_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
        out->u.if_setobject_v1.component_id = g2(&b);
        out->u.if_setobject_v1.obj_id = g2(&b);
        out->u.if_setobject_v1.zoom = g2(&b);
    assert(b.position == n);
    return 1;
}
int
serverprot_core_parse_if_setmodel_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
        out->u.if_setmodel_v1.component_id = g2(&b);
        out->u.if_setmodel_v1.model_id = g2(&b);
    assert(b.position == n);
    return 1;
}
int
serverprot_core_parse_if_setanim_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
        out->u.if_setanim_v1.component_id = g2(&b);
        out->u.if_setanim_v1.anim_id = g2(&b);
    assert(b.position == n);
    return 1;
}
int
serverprot_core_parse_if_setplayerhead_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
        out->u.if_setplayerhead_v1.component_id = g2(&b);
    assert(b.position == n);
    return 1;
}
int
serverprot_core_parse_if_settext_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.if_settext_v1.component_id = g2(&b);
    out->u.if_settext_v1.text = gstringnewline(&b);

    return 1;
}
int
serverprot_core_parse_if_setnpchead_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.if_setnpchead_v1.component_id = g2(&b);
    out->u.if_setnpchead_v1.npc_id = g2(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_if_setposition_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.if_setposition_v1.component_id = g2(&b);
    out->u.if_setposition_v1.x = g2b(&b);
    out->u.if_setposition_v1.z = g2b(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_if_setscrollpos_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.if_setscrollpos_v1.component_id = g2(&b);
    out->u.if_setscrollpos_v1.pos = g2(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_if_open_chat_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.if_openchat_v1.component_id = g2(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_if_open_main_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.if_openmain_v1.component_id = g2(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_if_open_side_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.if_openside_v1.component_id = g2(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_if_open_main_side_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.if_openmain_side_v1.main_component_id = g2(&b);
    out->u.if_openmain_side_v1.side_component_id = g2(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_if_close_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    (void)data;
    (void)n;
    out->u.if_close_v1._torirs_empty = 0;

    return 1;
}
int
serverprot_core_parse_update_stat_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.update_stat_v1.stat = g1(&b);
    out->u.update_stat_v1.xp = g4(&b);
    out->u.update_stat_v1.level = g1(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_update_runenergy_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.update_run_energy_v1.run_energy = g1(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_update_runweight_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.update_runweight_v1.run_weight = g2b(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_varp_small_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.varp_small_v1.variable = g2(&b);
    out->u.varp_small_v1.value = g1b(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_varp_large_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.varp_large_v1.variable = g2(&b);
    out->u.varp_large_v1.value = g4(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_reset_anims_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    (void)data;
    (void)n;

    return 1;
}
int
serverprot_core_parse_update_pid_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.update_pid_v1.local_player_index = g2(&b);
    out->u.update_pid_v1.unused = g1(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_cam_lookat_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.cam_lookat_v1.local_x = g2(&b);
    out->u.cam_lookat_v1.local_z = g2(&b);
    out->u.cam_lookat_v1.height = g2(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_cam_moveto_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.cam_moveto_v1.local_x = g2(&b);
    out->u.cam_moveto_v1.local_z = g2(&b);
    out->u.cam_moveto_v1.height = g2(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_cam_shake_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.cam_shake_v1.axis = g1(&b);
    out->u.cam_shake_v1.amplitude = g1(&b);
    out->u.cam_shake_v1.frequency = g1(&b);
    out->u.cam_shake_v1.speed = g1(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_cam_reset_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    (void)data;
    (void)n;

    return 1;
}
int
serverprot_core_parse_unset_map_flag_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    (void)data;
    (void)n;

    return 1;
}
int
serverprot_core_parse_hint_arrow_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.hint_arrow_v1.type = g1(&b);
    out->u.hint_arrow_v1.id = g2(&b);
    out->u.hint_arrow_v1.z = 0;
    out->u.hint_arrow_v1.height = 0;
    if( out->u.hint_arrow_v1.type >= 3 )
    {
        out->u.hint_arrow_v1.z = g2(&b);
        out->u.hint_arrow_v1.height = g1(&b);
    }
    return 1;
}

int
serverprot_core_parse_set_multiway_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.set_multiway_v1.multiway = g1(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_set_player_op_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.set_player_op_v1.op_index = g1(&b);
    out->u.set_player_op_v1.priority = g1(&b);
    out->u.set_player_op_v1.op_text = gstringnewline(&b);

    return 1;
}
int
serverprot_core_parse_npc_info_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    out->u.npc_info_v1.data = malloc(n);
    memcpy(out->u.npc_info_v1.data, data, n);
    out->u.npc_info_v1.length = n;
    return 1;
}

int
serverprot_core_parse_player_info_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    out->u.player_info_v1.data = malloc(n);
    memcpy(out->u.player_info_v1.data, data, n);
    out->u.player_info_v1.length = n;
    return 1;
}

int
serverprot_core_parse_update_inv_full_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct PktUpdateInvFullV1* p = &out->u.update_inv_full_v1;
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    p->component_id = g2(&b);
    p->size = g1(&b);
    p->obj_ids = malloc(p->size * sizeof(int));
    p->obj_counts = malloc(p->size * sizeof(int));
    for( int i = 0; i < p->size; i++ )
    {
        p->obj_ids[i] = g2(&b);
        int count = g1(&b);
        if( count == 255 )
            count = g4(&b);
        p->obj_counts[i] = count;
    }
    return 1;
}

int
serverprot_core_parse_update_inv_stop_transmit_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.update_inv_stop_transmit_v1.component_id = g2(&b);
    assert(b.position == n);
    return 1;
}

int
serverprot_core_parse_update_inv_partial_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct PktUpdateInvPartialV1* p = &out->u.update_inv_partial_v1;
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    p->component_id = g2(&b);
    int max_entries = (n - 2) / 5 + 1;
    p->entries = malloc(max_entries * sizeof(struct PktUpdateInvPartialEntryV1));
    int count = 0;
    while( b.position < n )
    {
        int slot = g1(&b);
        int obj_id_raw = g2(&b);
        int cnt = g1(&b);
        if( cnt == 255 )
            cnt = g4(&b);
        p->entries[count].slot = slot;
        p->entries[count].obj_id = obj_id_raw - 1;
        p->entries[count].count = cnt;
        count++;
    }
    p->count = count;
    return 1;
}

int
serverprot_core_parse_if_settab_active_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.if_settab_active_v1.tab_id = g1(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_message_game_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.message_game_v1.text = gstringnewline(&b);

    return 1;
}
int
serverprot_core_parse_message_private_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    if( n < 13 )
        return 0;
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.message_private_v1.from = g8(&b);
    out->u.message_private_v1.message_id = g4(&b);
    out->u.message_private_v1.staff_mod = g1(&b);
    out->u.message_private_v1.text = wordpack_unpack(&b, n - 13);
    if( !out->u.message_private_v1.text )
        return 0;
    return 1;
}

int
serverprot_core_parse_chat_filter_settings_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.chat_filter_settings_v1.chat_public_mode = g1(&b);
    out->u.chat_filter_settings_v1.chat_private_mode = g1(&b);
    out->u.chat_filter_settings_v1.chat_trade_mode = g1(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_update_friendlist_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.update_friendlist_v1.username = g8(&b);
    out->u.update_friendlist_v1.world = g1(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_update_ignorelist_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct PktUpdateIgnoreListV1* p = &out->u.update_ignorelist_v1;
    int count = n / 8;
    if( count <= 0 )
    {
        p->usernames = NULL;
        p->count = 0;
        return 1;
    }
    int64_t* names = (int64_t*)malloc((size_t)count * sizeof(int64_t));
    if( !names )
        return 0;
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    for( int i = 0; i < count; i++ )
        names[i] = g8(&b);
    p->usernames = names;
    p->count = count;
    return 1;
}

int
serverprot_core_parse_tut_flash_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.tut_flash_v1.tab_id = g1(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_tut_open_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.tut_open_v1.component_id = g2(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_update_reboot_timer_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.reboot_timer_v1.ticks = g2(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_synth_sound_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.synth_sound_v1.id = g2(&b);
    out->u.synth_sound_v1.loops = g1(&b);
    out->u.synth_sound_v1.delay = g2(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_midi_song_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.midi_song_v1.id = g2(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_midi_jingle_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)data, n);
    out->u.midi_jingle_v1.delay = g2(&b);
    out->u.midi_jingle_v1.id = g2(&b);
    assert(b.position == n);

    return 1;
}
int
serverprot_core_parse_logout_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    (void)data;
    (void)n;

    return 1;
}
int
serverprot_core_parse_p_countdialog_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    (void)data;
    (void)n;

    return 1;
}
int
serverprot_core_parse_finish_tracking_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    (void)data;
    (void)n;

    return 1;
}
int
serverprot_core_parse_enable_tracking_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    (void)data;
    (void)n;

    return 1;
}
int
serverprot_core_parse_last_login_info_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    (void)data;
    (void)n;

    return 1;
}
int
serverprot_core_parse_varp_sync_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out)
{
    (void)data;
    (void)n;

    return 1;
}
