#ifndef OSRS_CORE_SERVERPROT_CORE_PARSE_H
#define OSRS_CORE_SERVERPROT_CORE_PARSE_H

/* serverprot_core_parse — pure inbound deserializer layer.
 *
 * Each serverprot_core_parse_xxx_v1() takes a raw byte buffer and fills a typed
 * WireIn struct.  These functions have ZERO GGame knowledge; they only
 * deal with bytes and structs.
 *
 * _v1 means the wire format is stable across any revision sharing this layout.
 * Phases 5a-5d extract parsers from serverprot_netrev245_2_parse.c incrementally.
 */

#include <stdint.h>

/* ── Map / zone (Phase 5a) ────────────────────────────────────────────────── */

/** Parse 4-byte REBUILD_NORMAL payload: two big-endian u16 (zonex, zonez).
 * Returns 1 on success. */
int
serverprot_core_parse_maprebuild_v1(
    uint8_t* data,
    int      n,
    int*     out_zonex,
    int*     out_zonez);

/** UPDATE_ZONE_PARTIAL_FOLLOWS / UPDATE_ZONE_FULL_FOLLOWS / UPDATE_ZONE_PARTIAL_ENCLOSED.
 * 2 bytes (base_x g1, base_z g1). */
struct WireIn_UpdateZone_v1 { int base_x; int base_z; };
int serverprot_core_parse_update_zone_v1(
    uint8_t* data, int n, struct WireIn_UpdateZone_v1* out);

/** LOC_ADD_CHANGE — 4 bytes (pos g1, info g1, loc_id g2). */
struct WireIn_LocAddChange_v1 { int pos; int info; int loc_id; };
int serverprot_core_parse_loc_add_change_v1(
    uint8_t* data, int n, struct WireIn_LocAddChange_v1* out);

/** LOC_DEL — 2 bytes (pos g1, info g1). */
struct WireIn_LocDel_v1 { int pos; int info; };
int serverprot_core_parse_loc_del_v1(
    uint8_t* data, int n, struct WireIn_LocDel_v1* out);

/** LOC_ANIM — 4 bytes (pos g1, info g1, seq_id g2). */
struct WireIn_LocAnim_v1 { int pos; int info; int seq_id; };
int serverprot_core_parse_loc_anim_v1(
    uint8_t* data, int n, struct WireIn_LocAnim_v1* out);

/** LOC_MERGE — 13 bytes. */
struct WireIn_LocMerge_v1
{
    int pos; int info; int loc_id;
    int start; int end; int pid;
    int east; int south; int west; int north;
};
int serverprot_core_parse_loc_merge_v1(
    uint8_t* data, int n, struct WireIn_LocMerge_v1* out);

/** OBJ_ADD — 5 bytes (pos g1, obj_id g2, count g2). */
struct WireIn_ObjAdd_v1 { int pos; int obj_id; int count; };
int serverprot_core_parse_obj_add_v1(
    uint8_t* data, int n, struct WireIn_ObjAdd_v1* out);

/** OBJ_DEL — 3 bytes (pos g1, obj_id g2). */
struct WireIn_ObjDel_v1 { int pos; int obj_id; };
int serverprot_core_parse_obj_del_v1(
    uint8_t* data, int n, struct WireIn_ObjDel_v1* out);

/** OBJ_REVEAL — 7 bytes (pos g1, obj_id g2, count g2, receiver g2). */
struct WireIn_ObjReveal_v1 { int pos; int obj_id; int count; int receiver; };
int serverprot_core_parse_obj_reveal_v1(
    uint8_t* data, int n, struct WireIn_ObjReveal_v1* out);

/** OBJ_COUNT — 9 bytes (pos g1, obj_id g2, old_count g2, new_count g2). */
struct WireIn_ObjCount_v1 { int pos; int obj_id; int old_count; int new_count; };
int serverprot_core_parse_obj_count_v1(
    uint8_t* data, int n, struct WireIn_ObjCount_v1* out);

/** MAP_PROJANIM — 14 bytes. */
struct WireIn_MapProjAnim_v1
{
    int pos; int dx_offset; int dz_offset; int target; int spotanim;
    int src_height; int dst_height; int start_delay; int end_delay;
    int peak; int arc;
};
int serverprot_core_parse_map_projanim_v1(
    uint8_t* data, int n, struct WireIn_MapProjAnim_v1* out);

/** MAP_ANIM — 6 bytes (pos g1, id g2, height g1, delay g2). */
struct WireIn_MapAnim_v1 { int pos; int id; int height; int delay; };
int serverprot_core_parse_map_anim_v1(
    uint8_t* data, int n, struct WireIn_MapAnim_v1* out);

/* ── Interface (Phase 5b) ─────────────────────────────────────────────────── */

/** IF_SETTAB — 3 bytes (component_id g2, tab_id g1). */
struct WireIn_IfSettab_v1 { int component_id; int tab_id; };
int serverprot_core_parse_if_settab_v1(
    uint8_t* data, int n, struct WireIn_IfSettab_v1* out);

/** IF_SETCOLOUR — 4 bytes (component_id g2, colour g2). */
struct WireIn_IfSetcolour_v1 { int component_id; int colour; };
int serverprot_core_parse_if_setcolour_v1(
    uint8_t* data, int n, struct WireIn_IfSetcolour_v1* out);

/** IF_SETHIDE — 3 bytes (component_id g2, hide g1). */
struct WireIn_IfSethide_v1 { int component_id; int hide; };
int serverprot_core_parse_if_sethide_v1(
    uint8_t* data, int n, struct WireIn_IfSethide_v1* out);

/** IF_SETOBJECT — 6 bytes (component_id g2, obj_id g2, zoom g2). */
struct WireIn_IfSetobject_v1 { int component_id; int obj_id; int zoom; };
int serverprot_core_parse_if_setobject_v1(
    uint8_t* data, int n, struct WireIn_IfSetobject_v1* out);

/** IF_SETMODEL — 4 bytes (component_id g2, model_id g2). */
struct WireIn_IfSetmodel_v1 { int component_id; int model_id; };
int serverprot_core_parse_if_setmodel_v1(
    uint8_t* data, int n, struct WireIn_IfSetmodel_v1* out);

/** IF_SETANIM — 4 bytes (component_id g2, anim_id g2). */
struct WireIn_IfSetanim_v1 { int component_id; int anim_id; };
int serverprot_core_parse_if_setanim_v1(
    uint8_t* data, int n, struct WireIn_IfSetanim_v1* out);

/** IF_SETPLAYERHEAD — 2 bytes (component_id g2). */
struct WireIn_IfSetplayerhead_v1 { int component_id; };
int serverprot_core_parse_if_setplayerhead_v1(
    uint8_t* data, int n, struct WireIn_IfSetplayerhead_v1* out);

/** IF_SETTEXT — variable (component_id g2, text newline-terminated). caller frees text. */
struct WireIn_IfSettext_v1 { int component_id; char* text; };
int serverprot_core_parse_if_settext_v1(
    uint8_t* data, int n, struct WireIn_IfSettext_v1* out);

/** IF_SETNPCHEAD — 4 bytes (component_id g2, npc_id g2). */
struct WireIn_IfSetnpchead_v1 { int component_id; int npc_id; };
int serverprot_core_parse_if_setnpchead_v1(
    uint8_t* data, int n, struct WireIn_IfSetnpchead_v1* out);

/** IF_SETPOSITION — 6 bytes (component_id g2, x g2b, z g2b). */
struct WireIn_IfSetposition_v1 { int component_id; int x; int z; };
int serverprot_core_parse_if_setposition_v1(
    uint8_t* data, int n, struct WireIn_IfSetposition_v1* out);

/** IF_SETSCROLLPOS — 4 bytes (component_id g2, pos g2). */
struct WireIn_IfSetscrollpos_v1 { int component_id; int pos; };
int serverprot_core_parse_if_setscrollpos_v1(
    uint8_t* data, int n, struct WireIn_IfSetscrollpos_v1* out);

/** IF_OPENCHAT / IF_OPENMAIN / IF_OPENSIDE — 2 bytes (component_id g2). */
struct WireIn_IfOpen_v1 { int component_id; };
int serverprot_core_parse_if_open_v1(
    uint8_t* data, int n, struct WireIn_IfOpen_v1* out);

/** IF_OPENMAIN_SIDE — 4 bytes (main_component_id g2, side_component_id g2). */
struct WireIn_IfOpenMainSide_v1 { int main_component_id; int side_component_id; };
int serverprot_core_parse_if_open_main_side_v1(
    uint8_t* data, int n, struct WireIn_IfOpenMainSide_v1* out);

/** IF_CLOSE — zero payload.  Returns 1 always. */
int serverprot_core_parse_if_close_v1(uint8_t* data, int n);

/* ── Inventory + stats (Phase 5c) ─────────────────────────────────────────── */

/** UPDATE_STAT — 6 bytes (stat g1, xp g4, level g1). */
struct WireIn_UpdateStat_v1 { int stat; int xp; int level; };
int serverprot_core_parse_update_stat_v1(
    uint8_t* data, int n, struct WireIn_UpdateStat_v1* out);

/** UPDATE_RUNENERGY — 1 byte (run_energy g1). */
struct WireIn_UpdateRunenergy_v1 { int run_energy; };
int serverprot_core_parse_update_runenergy_v1(
    uint8_t* data, int n, struct WireIn_UpdateRunenergy_v1* out);

/** UPDATE_RUNWEIGHT — 2 bytes (run_weight g2b). */
struct WireIn_UpdateRunweight_v1 { int run_weight; };
int serverprot_core_parse_update_runweight_v1(
    uint8_t* data, int n, struct WireIn_UpdateRunweight_v1* out);

/** VARP_SMALL — 3 bytes (variable g2, value g1b). */
struct WireIn_VarpSmall_v1 { int variable; int value; };
int serverprot_core_parse_varp_small_v1(
    uint8_t* data, int n, struct WireIn_VarpSmall_v1* out);

/** VARP_LARGE — 6 bytes (variable g2, value g4). */
struct WireIn_VarpLarge_v1 { int variable; int value; };
int serverprot_core_parse_varp_large_v1(
    uint8_t* data, int n, struct WireIn_VarpLarge_v1* out);

/** RESET_ANIMS — zero payload. Returns 1 always. */
int serverprot_core_parse_reset_anims_v1(uint8_t* data, int n);

/** UPDATE_PID — 3 bytes (local_player_index g2, unused g1). */
struct WireIn_UpdatePid_v1 { int local_player_index; int unused; };
int serverprot_core_parse_update_pid_v1(
    uint8_t* data, int n, struct WireIn_UpdatePid_v1* out);

/* ── Camera + misc (Phase 5d) ─────────────────────────────────────────────── */

/** CAM_LOOKAT / CAM_MOVETO — 6 bytes (local_x g2, local_z g2, height g2). */
struct WireIn_CamLookAt_v1 { int local_x; int local_z; int height; };
int serverprot_core_parse_cam_lookat_v1(
    uint8_t* data, int n, struct WireIn_CamLookAt_v1* out);

struct WireIn_CamMoveTo_v1 { int local_x; int local_z; int height; };
int serverprot_core_parse_cam_moveto_v1(
    uint8_t* data, int n, struct WireIn_CamMoveTo_v1* out);

/** CAM_SHAKE — 4 bytes (axis g1, amplitude g1, frequency g1, speed g1). */
struct WireIn_CamShake_v1 { int axis; int amplitude; int frequency; int speed; };
int serverprot_core_parse_cam_shake_v1(
    uint8_t* data, int n, struct WireIn_CamShake_v1* out);

/** CAM_RESET — zero payload. Returns 1 always. */
int serverprot_core_parse_cam_reset_v1(uint8_t* data, int n);

/** UNSET_MAP_FLAG — zero payload. Returns 1 always. */
int serverprot_core_parse_unset_map_flag_v1(uint8_t* data, int n);

/** HINT_ARROW — variable (type g1, id g2; z g2 + height g1 when type >= 3). */
struct WireIn_HintArrow_v1 { int type; int id; int z; int height; };
int serverprot_core_parse_hint_arrow_v1(
    uint8_t* data, int n, struct WireIn_HintArrow_v1* out);

/** SET_MULTIWAY — 1 byte (multiway g1). */
struct WireIn_SetMultiway_v1 { int multiway; };
int serverprot_core_parse_set_multiway_v1(
    uint8_t* data, int n, struct WireIn_SetMultiway_v1* out);

/** SET_PLAYER_OP — variable (op_index g1, priority g1, op_text newline-str). caller frees op_text. */
struct WireIn_SetPlayerOp_v1 { int op_index; int priority; char* op_text; };
int serverprot_core_parse_set_player_op_v1(
    uint8_t* data, int n, struct WireIn_SetPlayerOp_v1* out);

#endif /* OSRS_CORE_SERVERPROT_CORE_PARSE_H */
