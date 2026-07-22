#include "net_out.h"

#include "wordpack.h"

#include <rsbuffer.h>
#include <string.h>

/* rsbuffer has no p8; write a big-endian i64 as two u32 words. */
static void
out_p8(struct RSCache_Buffer* payload, int64_t value)
{
    p4(payload, (int)((uint64_t)value >> 32));
    p4(payload, (int)((uint64_t)value & 0xffffffffu));
}

int
net_out_opcode(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int out_name)
{
    int wire = rev->packetout_code(out_name);
    if( wire < 0 )
        return -1;
    buf[0] = (uint8_t)((wire + isaac_next(random_out)) & 0xff);
    return 1;
}

/* Write the opcode and arm `b` over the payload region. Returns 0 on
 * success, -1 when the rev lacks the packet or cap is too small. */
static int
out_begin(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int out_name,
    int min_payload,
    struct RSCache_Buffer* b)
{
    if( cap < 1 + min_payload )
        return -1;
    if( net_out_opcode(rev, random_out, buf, out_name) < 0 )
        return -1;
    RSCache_BufferInit(b, buf + 1, (uint32_t)(cap - 1));
    return 0;
}

static int
out_empty(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int out_name)
{
    if( cap < 1 )
        return -1;
    if( net_out_opcode(rev, random_out, buf, out_name) < 0 )
        return -1;
    return 1;
}

/* -- keepalive / timers ------------------------------------------------- */

int
net_out_no_timeout(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap)
{
    return out_empty(rev, random_out, buf, cap, PKTOUT_NAME_NO_TIMEOUT);
}

int
net_out_idle_timer(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap)
{
    return out_empty(rev, random_out, buf, cap, PKTOUT_NAME_IDLE_TIMER);
}

int
net_out_map_build_complete(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap)
{
    return out_empty(rev, random_out, buf, cap, PKTOUT_NAME_MAP_BUILD_COMPLETE);
}

/* -- interface ---------------------------------------------------------- */

static int
out_com2(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int out_name,
    int component_id)
{
    struct RSCache_Buffer b;
    if( out_begin(rev, random_out, buf, cap, out_name, 2, &b) < 0 )
        return -1;
    p2(&b, component_id);
    return 1 + (int)b.position;
}

int
net_out_if_button(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int component_id)
{
    return out_com2(rev, random_out, buf, cap, PKTOUT_NAME_IF_BUTTON, component_id);
}

int
net_out_resume_pausebutton(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int component_id)
{
    return out_com2(rev, random_out, buf, cap, PKTOUT_NAME_RESUME_PAUSEBUTTON, component_id);
}

int
net_out_close_modal(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap)
{
    return out_empty(rev, random_out, buf, cap, PKTOUT_NAME_CLOSE_MODAL);
}

int
net_out_resume_countdialog(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int amount)
{
    struct RSCache_Buffer b;
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_RESUME_P_COUNTDIALOG, 4, &b) < 0 )
        return -1;
    p4(&b, amount);
    return 1 + (int)b.position;
}

int
net_out_tut_clickside(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int tab)
{
    struct RSCache_Buffer b;
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_TUT_CLICKSIDE, 1, &b) < 0 )
        return -1;
    p1(&b, tab);
    return 1 + (int)b.position;
}

/* -- movement ----------------------------------------------------------- */

/* Reference tryMove packet body (Client.ts:6068-6104): ctrl, then the turn
 * waypoint closest to the source as absolute tiles, then up to 24 signed
 * (dx,dz) byte pairs walking toward the destination. route uses the tryMove
 * scratch layout (collision_map_try_route): [0] = destination, ascending
 * toward the source. A 1-entry route degenerates to the old
 * single-coordinate body. */
static int
out_move(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int out_name,
    int base_x,
    int base_z,
    int const* route_x,
    int const* route_z,
    int route_len,
    int ctrl_held,
    int trailer_len)
{
    struct RSCache_Buffer b;
    if( route_len < 1 )
        return -1;
    int buffer_size = route_len < 25 ? route_len : 25;
    int idx = route_len - 1;
    int start_x = route_x[idx];
    int start_z = route_z[idx];
    if( out_begin(
            rev, random_out, buf, cap, out_name, 1 + buffer_size * 2 + 3 + trailer_len, &b) < 0 )
        return -1;
    p1(&b, buffer_size * 2 + 3 + trailer_len); /* var-u8 payload length */
    p1(&b, ctrl_held ? 1 : 0);
    p2(&b, base_x + start_x);
    p2(&b, base_z + start_z);
    for( int i = 1; i < buffer_size; i++ )
    {
        idx--;
        p1(&b, route_x[idx] - start_x);
        p1(&b, route_z[idx] - start_z);
    }
    return 1 + (int)b.position;
}

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
    int ctrl_held)
{
    return out_move(
        rev,
        random_out,
        buf,
        cap,
        PKTOUT_NAME_MOVE_GAMECLICK,
        base_x,
        base_z,
        route_x,
        route_z,
        route_len,
        ctrl_held,
        0);
}

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
    int ctrl_held)
{
    return out_move(
        rev,
        random_out,
        buf,
        cap,
        PKTOUT_NAME_MOVE_OPCLICK,
        base_x,
        base_z,
        route_x,
        route_z,
        route_len,
        ctrl_held,
        0);
}

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
    int nearest)
{
    int n = out_move(
        rev,
        random_out,
        buf,
        cap,
        PKTOUT_NAME_MOVE_MINIMAPCLICK,
        base_x,
        base_z,
        route_x,
        route_z,
        route_len,
        ctrl_held,
        14);
    struct RSCache_Buffer b;
    if( n < 0 )
        return -1;
    /* Anticheat trailer (reference Client.ts minimap click). */
    RSCache_BufferInit(&b, buf + n, (uint32_t)(cap - n));
    p1(&b, click_x);
    p1(&b, click_y);
    p2(&b, camera_yaw);
    p1(&b, 57);
    p1(&b, minimap_angle);
    p1(&b, minimap_zoom);
    p1(&b, 89);
    p2(&b, player_fine_x);
    p2(&b, player_fine_z);
    p1(&b, nearest);
    p1(&b, 63);
    return n + (int)b.position;
}

/* -- held-item / inventory-component ops -------------------------------- */

static int
out_obj_slot_com(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int out_name,
    int obj_id,
    int slot,
    int component_id)
{
    struct RSCache_Buffer b;
    if( out_begin(rev, random_out, buf, cap, out_name, 6, &b) < 0 )
        return -1;
    p2(&b, obj_id);
    p2(&b, slot);
    p2(&b, component_id);
    return 1 + (int)b.position;
}

int
net_out_opheld(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int op_num,
    int obj_id,
    int slot,
    int component_id)
{
    if( op_num < 1 || op_num > 5 )
        return -1;
    return out_obj_slot_com(
        rev, random_out, buf, cap, PKTOUT_NAME_OPHELD1 + (op_num - 1), obj_id, slot, component_id);
}

int
net_out_opheldt(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int obj_id,
    int slot,
    int component_id,
    int spell_component_id)
{
    struct RSCache_Buffer b;
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_OPHELDT, 8, &b) < 0 )
        return -1;
    p2(&b, obj_id);
    p2(&b, slot);
    p2(&b, component_id);
    p2(&b, spell_component_id);
    return 1 + (int)b.position;
}

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
    int use_component_id)
{
    struct RSCache_Buffer b;
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_OPHELDU, 12, &b) < 0 )
        return -1;
    p2(&b, obj_id);
    p2(&b, slot);
    p2(&b, component_id);
    p2(&b, use_obj_id);
    p2(&b, use_slot);
    p2(&b, use_component_id);
    return 1 + (int)b.position;
}

int
net_out_inv_button(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int op_num,
    int obj_id,
    int slot,
    int component_id)
{
    if( op_num < 1 || op_num > 5 )
        return -1;
    return out_obj_slot_com(
        rev,
        random_out,
        buf,
        cap,
        PKTOUT_NAME_INV_BUTTON1 + (op_num - 1),
        obj_id,
        slot,
        component_id);
}

int
net_out_inv_buttond(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int component_id,
    int from_slot,
    int to_slot,
    int mode)
{
    struct RSCache_Buffer b;
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_INV_BUTTOND, 7, &b) < 0 )
        return -1;
    p2(&b, component_id);
    p2(&b, from_slot);
    p2(&b, to_slot);
    p1(&b, mode);
    return 1 + (int)b.position;
}

/* -- world entity ops --------------------------------------------------- */

static int
out_xz_id(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int out_name,
    int abs_x,
    int abs_z,
    int id)
{
    struct RSCache_Buffer b;
    if( out_begin(rev, random_out, buf, cap, out_name, 6, &b) < 0 )
        return -1;
    p2(&b, abs_x);
    p2(&b, abs_z);
    p2(&b, id);
    return 1 + (int)b.position;
}

int
net_out_oploc(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int op_num,
    int abs_x,
    int abs_z,
    int loc_id)
{
    if( op_num < 1 || op_num > 5 )
        return -1;
    return out_xz_id(
        rev, random_out, buf, cap, PKTOUT_NAME_OPLOC1 + (op_num - 1), abs_x, abs_z, loc_id);
}

int
net_out_oploct(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int abs_x,
    int abs_z,
    int loc_id,
    int spell_component_id)
{
    struct RSCache_Buffer b;
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_OPLOCT, 8, &b) < 0 )
        return -1;
    p2(&b, abs_x);
    p2(&b, abs_z);
    p2(&b, loc_id);
    p2(&b, spell_component_id);
    return 1 + (int)b.position;
}

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
    int use_component_id)
{
    struct RSCache_Buffer b;
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_OPLOCU, 12, &b) < 0 )
        return -1;
    p2(&b, abs_x);
    p2(&b, abs_z);
    p2(&b, loc_id);
    p2(&b, use_obj_id);
    p2(&b, use_slot);
    p2(&b, use_component_id);
    return 1 + (int)b.position;
}

static int
out_slot2(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int out_name,
    int slot)
{
    struct RSCache_Buffer b;
    if( out_begin(rev, random_out, buf, cap, out_name, 2, &b) < 0 )
        return -1;
    p2(&b, slot);
    return 1 + (int)b.position;
}

int
net_out_opnpc(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int op_num,
    int npc_slot)
{
    if( op_num < 1 || op_num > 5 )
        return -1;
    return out_slot2(rev, random_out, buf, cap, PKTOUT_NAME_OPNPC1 + (op_num - 1), npc_slot);
}

int
net_out_opnpct(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int npc_slot,
    int spell_component_id)
{
    struct RSCache_Buffer b;
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_OPNPCT, 4, &b) < 0 )
        return -1;
    p2(&b, npc_slot);
    p2(&b, spell_component_id);
    return 1 + (int)b.position;
}

int
net_out_opnpcu(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int npc_slot,
    int use_obj_id,
    int use_slot,
    int use_component_id)
{
    struct RSCache_Buffer b;
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_OPNPCU, 8, &b) < 0 )
        return -1;
    p2(&b, npc_slot);
    p2(&b, use_obj_id);
    p2(&b, use_slot);
    p2(&b, use_component_id);
    return 1 + (int)b.position;
}

int
net_out_opobj(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int op_num,
    int abs_x,
    int abs_z,
    int obj_id)
{
    if( op_num < 1 || op_num > 5 )
        return -1;
    return out_xz_id(
        rev, random_out, buf, cap, PKTOUT_NAME_OPOBJ1 + (op_num - 1), abs_x, abs_z, obj_id);
}

int
net_out_opobjt(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int abs_x,
    int abs_z,
    int obj_id,
    int spell_component_id)
{
    struct RSCache_Buffer b;
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_OPOBJT, 8, &b) < 0 )
        return -1;
    p2(&b, abs_x);
    p2(&b, abs_z);
    p2(&b, obj_id);
    p2(&b, spell_component_id);
    return 1 + (int)b.position;
}

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
    int use_component_id)
{
    struct RSCache_Buffer b;
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_OPOBJU, 12, &b) < 0 )
        return -1;
    p2(&b, abs_x);
    p2(&b, abs_z);
    p2(&b, obj_id);
    p2(&b, use_obj_id);
    p2(&b, use_slot);
    p2(&b, use_component_id);
    return 1 + (int)b.position;
}

int
net_out_opplayer(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int op_num,
    int player_slot)
{
    if( op_num < 1 || op_num > 5 )
        return -1;
    return out_slot2(rev, random_out, buf, cap, PKTOUT_NAME_OPPLAYER1 + (op_num - 1), player_slot);
}

int
net_out_opplayert(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int player_slot,
    int spell_component_id)
{
    struct RSCache_Buffer b;
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_OPPLAYERT, 4, &b) < 0 )
        return -1;
    p2(&b, player_slot);
    p2(&b, spell_component_id);
    return 1 + (int)b.position;
}

int
net_out_opplayeru(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int player_slot,
    int use_obj_id,
    int use_slot,
    int use_component_id)
{
    struct RSCache_Buffer b;
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_OPPLAYERU, 8, &b) < 0 )
        return -1;
    p2(&b, player_slot);
    p2(&b, use_obj_id);
    p2(&b, use_slot);
    p2(&b, use_component_id);
    return 1 + (int)b.position;
}

/* -- chat / social ------------------------------------------------------ */

int
net_out_message_public(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    char const* text)
{
    /* MESSAGE_PUBLIC: var-u8 length, body = colour(0) effect(0) wordpacked. */
    struct RSCache_Buffer b;
    int len_pos;
    int start;
    if( !text )
        return -1;
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_MESSAGE_PUBLIC, 7, &b) < 0 )
        return -1;
    p1(&b, 0); /* length placeholder */
    len_pos = (int)b.position - 1;
    start = (int)b.position;
    p1(&b, 0); /* colour */
    p1(&b, 0); /* effect */
    wordpack_pack(&b, text);
    buf[1 + len_pos] = (uint8_t)((int)b.position - start);
    return 1 + (int)b.position;
}

int
net_out_message_private(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int64_t to_name37,
    char const* text)
{
    /* var-u8 length, body = name37(8) + wordpacked message. */
    struct RSCache_Buffer b;
    int len_pos;
    int start;
    if( !text )
        return -1;
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_MESSAGE_PRIVATE, 12, &b) < 0 )
        return -1;
    p1(&b, 0); /* length placeholder */
    len_pos = (int)b.position - 1;
    start = (int)b.position;
    out_p8(&b, to_name37);
    wordpack_pack(&b, text);
    buf[1 + len_pos] = (uint8_t)((int)b.position - start);
    return 1 + (int)b.position;
}

int
net_out_client_cheat(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    char const* text)
{
    /* var-u8 length, body = jstr (newline-terminated) without "::". */
    struct RSCache_Buffer b;
    int len_pos;
    int start;
    if( !text )
        return -1;
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_CLIENT_CHEAT, (int)strlen(text) + 3, &b) < 0 )
        return -1;
    p1(&b, 0); /* length placeholder */
    len_pos = (int)b.position - 1;
    start = (int)b.position;
    pjstr(&b, text, RSCACHE_JSTR_TERMINATOR_NEWLINE);
    buf[1 + len_pos] = (uint8_t)((int)b.position - start);
    return 1 + (int)b.position;
}

int
net_out_chat_setmode(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int public_mode,
    int private_mode,
    int trade_mode)
{
    struct RSCache_Buffer b;
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_CHAT_SETMODE, 3, &b) < 0 )
        return -1;
    p1(&b, public_mode);
    p1(&b, private_mode);
    p1(&b, trade_mode);
    return 1 + (int)b.position;
}

static int
out_name37(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int out_name,
    int64_t name37)
{
    struct RSCache_Buffer b;
    if( out_begin(rev, random_out, buf, cap, out_name, 8, &b) < 0 )
        return -1;
    out_p8(&b, name37);
    return 1 + (int)b.position;
}

int
net_out_friendlist_add(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int64_t name37)
{
    return out_name37(rev, random_out, buf, cap, PKTOUT_NAME_FRIENDLIST_ADD, name37);
}

int
net_out_friendlist_del(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int64_t name37)
{
    return out_name37(rev, random_out, buf, cap, PKTOUT_NAME_FRIENDLIST_DEL, name37);
}

int
net_out_ignorelist_add(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int64_t name37)
{
    return out_name37(rev, random_out, buf, cap, PKTOUT_NAME_IGNORELIST_ADD, name37);
}

int
net_out_ignorelist_del(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int64_t name37)
{
    return out_name37(rev, random_out, buf, cap, PKTOUT_NAME_IGNORELIST_DEL, name37);
}

int
net_out_report_abuse(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int64_t name37,
    int reason,
    int moderator_mute)
{
    struct RSCache_Buffer b;
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_REPORT_ABUSE, 10, &b) < 0 )
        return -1;
    out_p8(&b, name37);
    p1(&b, reason);
    p1(&b, moderator_mute ? 1 : 0);
    return 1 + (int)b.position;
}

/* -- misc --------------------------------------------------------------- */

int
net_out_idk_savedesign(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int gender,
    int const idkit[7],
    int const colours[5])
{
    struct RSCache_Buffer b;
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_IDK_SAVEDESIGN, 13, &b) < 0 )
        return -1;
    p1(&b, gender);
    for( int i = 0; i < 7; i++ )
        p1(&b, idkit[i] < 0 ? 255 : idkit[i]);
    for( int i = 0; i < 5; i++ )
        p1(&b, colours[i]);
    return 1 + (int)b.position;
}

int
net_out_event_tracking(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    uint8_t const* data,
    int len)
{
    /* var-u16 length + raw tracking blob (empty stub is valid). */
    struct RSCache_Buffer b;
    if( len < 0 )
        len = 0;
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_EVENT_TRACKING, 2 + len, &b) < 0 )
        return -1;
    p2(&b, len);
    for( int i = 0; i < len; i++ )
        p1(&b, data[i]);
    return 1 + (int)b.position;
}
