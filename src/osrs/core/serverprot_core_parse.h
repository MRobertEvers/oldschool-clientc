#ifndef OSRS_CORE_SERVERPROT_CORE_PARSE_H
#define OSRS_CORE_SERVERPROT_CORE_PARSE_H

/* serverprot_core_parse — pure inbound deserializer layer.
 *
 * Each serverprot_core_parse_*_v1() reads a raw byte buffer and writes the
 * decoded payload into `out->u.<member>_v1`, and returns 1 on success
 * (0 on hard failure). Versioning is per semantic `SERVERPROT_*_V1` /
 * `Pkt*V1` type, not a separate RevServerProtPacket-level layout field.
 *
 * The caller (e.g. serverprot_netrev245_2_parse) sets `out->packet_type` to
 * the semantic SERVERPROT_*_V1 value before or after calling; these functions
 * do not depend on GGame.
 */

#include <stdint.h>

struct RevServerProtPacket;

/* ── Map / zone ───────────────────────────────────────────────────────────── */

int
serverprot_core_parse_maprebuild_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);

int
serverprot_core_parse_update_zone_partial_follows_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_update_zone_full_follows_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);

int
serverprot_core_parse_loc_add_change_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_loc_del_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_loc_anim_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_loc_merge_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);

int
serverprot_core_parse_obj_add_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_obj_del_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_obj_reveal_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_obj_count_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);

int
serverprot_core_parse_map_projanim_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_map_anim_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);

/* ── Interface ────────────────────────────────────────────────────────────── */

int
serverprot_core_parse_if_settab_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_if_setcolour_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_if_sethide_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_if_setobject_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_if_setmodel_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_if_setanim_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_if_setplayerhead_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_if_settext_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_if_setnpchead_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_if_setposition_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_if_setscrollpos_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_if_open_chat_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_if_open_main_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_if_open_side_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_if_open_main_side_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_if_open_overlay_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_if_close_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);

/* ── Inventory + stats ─────────────────────────────────────────────────────── */

int
serverprot_core_parse_update_stat_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_update_runenergy_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_update_runweight_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_varp_small_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_varp_large_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_reset_anims_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_update_pid_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);

/* ── Camera + misc ────────────────────────────────────────────────────────── */

int
serverprot_core_parse_cam_lookat_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_cam_moveto_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_cam_shake_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_cam_reset_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_unset_map_flag_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_hint_arrow_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_set_multiway_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_set_player_op_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);

/* ── Entity streams ───────────────────────────────────────────────────────── */

int
serverprot_core_parse_npc_info_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_player_info_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);

/* ── Inventory (heap fields) ─────────────────────────────────────────────── */

int
serverprot_core_parse_update_inv_full_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_update_inv_stop_transmit_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_update_inv_partial_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);

/* ── Interface additions ─────────────────────────────────────────────────── */

int
serverprot_core_parse_if_settab_active_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);

/* ── Messages + chat ──────────────────────────────────────────────────────── */

int
serverprot_core_parse_message_game_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_message_private_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_chat_filter_settings_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);

/* ── Social ───────────────────────────────────────────────────────────────── */

int
serverprot_core_parse_update_friendlist_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_update_ignorelist_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);

/* ── Tutorial / reboot / audio ───────────────────────────────────────────── */

int
serverprot_core_parse_tut_flash_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_tut_open_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_update_reboot_timer_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_synth_sound_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_midi_song_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_midi_jingle_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);

/* ── Zero-payload packets ─────────────────────────────────────────────────── */

int
serverprot_core_parse_logout_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_p_countdialog_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_finish_tracking_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_enable_tracking_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_last_login_info_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);
int
serverprot_core_parse_varp_sync_v1(
    uint8_t* data,
    int n,
    struct RevServerProtPacket* out);

#endif /* OSRS_CORE_SERVERPROT_CORE_PARSE_H */
