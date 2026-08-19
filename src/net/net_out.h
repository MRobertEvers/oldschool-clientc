#ifndef SRC_NET_NET_OUT_H
#define SRC_NET_NET_OUT_H

/*
 * Outbound packet builders. Each writes an ISAAC-encrypted opcode byte
 * (reference Packet.pIsaac) followed by the payload — including the var-u8 /
 * var-u16 length byte(s) for variable packets, since the send path is raw
 * bytes — into a caller buffer, returning the byte count. Wire opcodes are
 * resolved through the revision table from canonical PKTOUT_NAME_* values;
 * a builder returns -1 when the packet is absent from the revision (caller
 * skips sending) or on overflow.
 *
 * Payload layouts follow the authoritative LostCity_Server client-packet
 * decoders (engine/src/network/game/client/codec Decoder.ts files).
 */

#include "isaac.h"
#include "rev/gameproto_revisions.h"

#include <stdint.h>

/** Encrypt+write one opcode byte for a canonical out-name.
 *  Returns 1, or -1 when the revision lacks the packet. */
int
net_out_opcode(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int out_name);

/* Each builder returns total bytes written into buf, or -1 on
 * overflow / packet-absent-from-revision. */

/* -- keepalive / timers ------------------------------------------------- */
int
net_out_no_timeout(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap);
int
net_out_idle_timer(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap);
int
net_out_map_build_complete(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap);

/** WINDOW_STATUS: Display layout mode 0/1/2 + canvas size; encoded per revision. */
int
net_out_window_status(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int mode,
    int width,
    int height);

/* -- interface ---------------------------------------------------------- */
int
net_out_if_button(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int component_id);
/** IF_BUTTON1..10: op `op_num` of an IF3 component. `sub` is the grid cell the
 *  click landed in, or -1 for a plain widget. */
int
net_out_if_button_op(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int op_num,
    int component_id,
    int sub);
/** IF_BUTTON1..10 on an object-backed IF3 child. Rev239's collapsed packet
 * carries the object id as its item/component discriminator. */
int
net_out_if_button_obj_op(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int op_num,
    int component_id,
    int sub,
    int obj_id);
/** CLICK_WORLD_MAP: the absolute tile a world-map click landed on. */
int
net_out_click_world_map(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int level,
    int abs_x,
    int abs_z);
int
net_out_resume_pausebutton(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int component_id,
    int sub_id);
int
net_out_close_modal(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap);
int
net_out_resume_countdialog(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int amount);
int
net_out_tut_clickside(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int tab);

/* -- movement ----------------------------------------------------------- */
/* Reference tryMove packet: var-u8 [len=2*min(route_len,25)+3] + ctrl(1) +
 * startX(2) + startZ(2) + up to 24 signed (dx,dz) byte pairs. route uses the
 * collision_map_try_route layout ([0] = destination, ascending toward the
 * source); start = route[route_len-1] + base (scene tile -> absolute). */
int
net_out_move_gameclick(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int base_x,
    int base_z,
    int const* route_x,
    int const* route_z,
    int route_len,
    int ctrl_held);
int
net_out_move_opclick(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int base_x,
    int base_z,
    int const* route_x,
    int const* route_z,
    int route_len,
    int ctrl_held);
/* Minimap walk: gameclick body + the 14-byte anticheat trailer
 * (clickX, clickY, cameraYaw, 57, minimapAngle, minimapZoom, 89,
 *  playerFineX, playerFineZ, nearest, 63). */
int
net_out_move_minimapclick(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int base_x,
    int base_z,
    int const* route_x,
    int const* route_z,
    int route_len,
    int ctrl_held,
    int click_x,
    int click_y,
    int camera_yaw,
    int minimap_angle,
    int minimap_zoom,
    int player_fine_x,
    int player_fine_z,
    int nearest);

/* -- held-item / inventory-component ops -------------------------------- */
/* op is 1..5 (OPHELD1..OPHELD5 etc). */
int
net_out_opheld(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int op_num,
    int obj_id,
    int slot,
    int component_id);
int
net_out_opheldt(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int obj_id,
    int slot,
    int component_id,
    int spell_component_id);
int
net_out_opheldu(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int obj_id,
    int slot,
    int component_id,
    int use_obj_id,
    int use_slot,
    int use_component_id);
int
net_out_inv_button(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int op_num,
    int obj_id,
    int slot,
    int component_id);
/**
 * INV_BUTTOND / IfButtonD — drag release.
 *
 * Classic (lc254 / lc245_2): p{2|4} src_com, p2 from_slot, p2 to_slot, p1 mode.
 * Rev-230: 16-byte dual-endpoint frame naming both widgets (see net_out.c);
 * `mode` is unused there. Empty slots send obj_id = -1.
 */
int
net_out_inv_buttond(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int src_com,
    int src_obj,
    int src_slot,
    int dst_com,
    int dst_obj,
    int dst_slot,
    int mode);

/* -- world entity ops --------------------------------------------------- */
int
net_out_oploc(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int op_num,
    int abs_x,
    int abs_z,
    int loc_id);
int
net_out_oploct(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int abs_x,
    int abs_z,
    int loc_id,
    int spell_component_id);
int
net_out_oplocu(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int abs_x,
    int abs_z,
    int loc_id,
    int use_obj_id,
    int use_slot,
    int use_component_id);
int
net_out_opnpc(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int op_num,
    int npc_slot);
int
net_out_opnpct(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int npc_slot,
    int spell_component_id);
int
net_out_opnpcu(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int npc_slot,
    int use_obj_id,
    int use_slot,
    int use_component_id);
int
net_out_opobj(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int op_num,
    int abs_x,
    int abs_z,
    int obj_id);
int
net_out_opobjt(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int abs_x,
    int abs_z,
    int obj_id,
    int spell_component_id);
int
net_out_opobju(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int abs_x,
    int abs_z,
    int obj_id,
    int use_obj_id,
    int use_slot,
    int use_component_id);
int
net_out_opplayer(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int op_num,
    int player_slot);
int
net_out_opplayert(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int player_slot,
    int spell_component_id);
int
net_out_opplayeru(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int player_slot,
    int use_obj_id,
    int use_slot,
    int use_component_id);

/* -- chat / social ------------------------------------------------------ */
/** `colour_effect` is the pair the wire carries as two bytes and every other
 *  surface carries as one int: high byte the chat colour, low byte the effect
 *  (the same packing PLAYER_INFO's inbound chat block uses). 0 is plain
 *  yellow, which is what a client with no chat-style selector always sends. */
int
net_out_message_public(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    char const* text,
    int colour_effect);
int
net_out_message_private(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int64_t to_name37,
    char const* text);
int
net_out_client_cheat(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    char const* text);
int
net_out_chat_setmode(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int public_mode,
    int private_mode,
    int trade_mode);
int
net_out_friendlist_add(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int64_t name37);
int
net_out_friendlist_del(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int64_t name37);
int
net_out_ignorelist_add(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int64_t name37);
int
net_out_ignorelist_del(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int64_t name37);
int
net_out_report_abuse(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int64_t name37,
    int reason,
    int moderator_mute);

/* -- misc --------------------------------------------------------------- */
int
net_out_idk_savedesign(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int gender,
    int const idkit[7],
    int const colours[5]);
/** var-u16 tracking blob reply (stub payload allowed: len 0). */
int
net_out_event_tracking(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    uint8_t const* data,
    int len);

#endif
