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

int
net_out_window_status(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int mode,
    int width,
    int height)
{
    struct RSCache_Buffer b;
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_WINDOW_STATUS, 5, &b) < 0 )
        return -1;
    p1(&b, mode & 0xff);
    p2(&b, width & 0xffff);
    p2(&b, height & 0xffff);
    return 1 + (int)b.position;
}

/* -- interface ---------------------------------------------------------- */

/*
 * One interface component id, at whatever width the revision uses.
 *
 * Classic revisions send a flat 2-byte component id. rev 230 sends a packed
 * (interface << 16) | child uid, which needs 4 bytes — truncating it to 2 drops
 * the interface half, leaving the server unable to distinguish 231:5 from
 * 217:5. The width is revision state, so it comes from the table rather than
 * from a call site.
 */
static void
out_p_com(
    struct RSCache_Buffer* buf,
    int width,
    int component_id)
{
    if( width == 4 )
        p4(buf, component_id);
    else
        p2(buf, component_id);
}

static int
out_com(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int out_name,
    int component_id)
{
    struct RSCache_Buffer b;
    int width = (rev && rev->component_id_bytes > 0) ? rev->component_id_bytes : 2;

    if( out_begin(rev, random_out, buf, cap, out_name, width, &b) < 0 )
        return -1;
    out_p_com(&b, width, component_id);
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
    return out_com(rev, random_out, buf, cap, PKTOUT_NAME_IF_BUTTON, component_id);
}

int
net_out_if_button_op(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int op_num,
    int component_id,
    int sub)
{
    struct RSCache_Buffer b;
    int width = (rev && rev->component_id_bytes > 0) ? rev->component_id_bytes : 2;

    if( op_num < 1 || op_num > 10 )
        return -1;
    if( out_begin(
            rev,
            random_out,
            buf,
            cap,
            PKTOUT_NAME_IF_BUTTON1 + (op_num - 1),
            width + 2,
            &b) < 0 )
        return -1;
    out_p_com(&b, width, component_id);
    p2(&b, sub & 0xffff);
    return 1 + (int)b.position;
}

int
net_out_click_world_map(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int level,
    int abs_x,
    int abs_z)
{
    struct RSCache_Buffer b;

    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_CLICK_WORLD_MAP, 4, &b) < 0 )
        return -1;
    /* RSProt ClickWorldMap packs the destination into one int, the same
     * level<<28 | x<<14 | z coord the CS2 map ops speak. */
    p4(&b, ((level & 0x3) << 28) | ((abs_x & 0x3fff) << 14) | (abs_z & 0x3fff));
    return 1 + (int)b.position;
}

int
net_out_resume_pausebutton(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int component_id)
{
    return out_com(rev, random_out, buf, cap, PKTOUT_NAME_RESUME_PAUSEBUTTON, component_id);
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

/* Encode a MOVE_* body. route uses the tryMove scratch layout
 * (collision_map_try_route): [0] = destination, ascending toward the source.
 *
 * osrs230 MOVE_GAMECLICK is a fixed 5-byte destination body
 * (keyCombination, x, z) with no length byte and no waypoints — that is the
 * post-2013 server-authoritative wire. Every other MOVE_* (lc254 waypoint
 * packets, minimap with trailer) still writes the classic var-u8 length +
 * start + (dx,dz) pairs. A 1-entry route degenerates to start==dest with no
 * deltas on the classic path. */
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
    int dest_only;

    if( route_len < 1 )
        return -1;

    /* Fixed 5-byte MOVE_GAMECLICK on osrs230: destination only. */
    dest_only = rev->revision == GAMEPROTO_REVISION_OSRS230 &&
                out_name == PKTOUT_NAME_MOVE_GAMECLICK && trailer_len == 0;
    if( dest_only )
    {
        if( out_begin(rev, random_out, buf, cap, out_name, 5, &b) < 0 )
            return -1;
        p1(&b, ctrl_held ? 1 : 0);
        p2(&b, base_x + route_x[0]);
        p2(&b, base_z + route_z[0]);
        return 1 + (int)b.position;
    }

    {
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
    /* The component travels at the revision's full width, like every other
     * packet that names one (out_com). At rev 230 that is the packed
     * (interface << 16) | child uid — rsprot's If3Button.combinedId — and
     * truncating it to two bytes drops the interface half, so a backpack slot
     * (149<<16|0) and a worn slot arrive identical and equal to zero. */
    int width = (rev && rev->component_id_bytes > 0) ? rev->component_id_bytes : 2;

    if( out_begin(rev, random_out, buf, cap, out_name, 4 + width, &b) < 0 )
        return -1;
    p2(&b, obj_id);
    p2(&b, slot);
    out_p_com(&b, width, component_id);
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
    int width = (rev && rev->component_id_bytes > 0) ? rev->component_id_bytes : 2;

    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_OPHELDT, 4 + 2 * width, &b) < 0 )
        return -1;
    p2(&b, obj_id);
    p2(&b, slot);
    out_p_com(&b, width, component_id);
    out_p_com(&b, width, spell_component_id);
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
    int width = (rev && rev->component_id_bytes > 0) ? rev->component_id_bytes : 2;

    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_OPHELDU, 8 + 2 * width, &b) < 0 )
        return -1;
    p2(&b, obj_id);
    p2(&b, slot);
    out_p_com(&b, width, component_id);
    p2(&b, use_obj_id);
    p2(&b, use_slot);
    out_p_com(&b, width, use_component_id);
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

/* Jagex alt byte-order writers used by the rev-230 IfButtonD frame.
 * Naming matches rsareabuf / RSProt: alt1 = LE, alt2 = BE+128, alt3 = LE+128.
 * Mask every byte: Java writes `(byte)(n + 128)` and rsareabuf's put truncates;
 * RSCache_BufferP1 asserts value < 256 without accepting (v+128) >= 256. */
static void
out_p2_alt1(struct RSCache_Buffer* b, int v)
{
    p1(b, v & 0xff);
    p1(b, (v >> 8) & 0xff);
}

static void
out_p2_alt2(struct RSCache_Buffer* b, int v)
{
    p1(b, (v >> 8) & 0xff);
    p1(b, (v + 128) & 0xff);
}

static void
out_p2_alt3(struct RSCache_Buffer* b, int v)
{
    p1(b, (v + 128) & 0xff);
    p1(b, (v >> 8) & 0xff);
}

static void
out_p4_alt1(struct RSCache_Buffer* b, int v)
{
    p1(b, v & 0xff);
    p1(b, (v >> 8) & 0xff);
    p1(b, (v >> 16) & 0xff);
    p1(b, (v >> 24) & 0xff);
}

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
    int mode)
{
    struct RSCache_Buffer b;
    int width = (rev && rev->component_id_bytes > 0) ? rev->component_id_bytes : 2;

    /* Rev-230: ClientProt 48 shape — both endpoints with item ids (16 bytes).
     * Encodings from the deob class108.method3759 / class617 writers. */
    if( width == 4 )
    {
        if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_INV_BUTTOND, 16, &b) < 0 )
            return -1;
        out_p4_alt1(&b, src_com);       /* method13150: p4 LE */
        out_p2_alt1(&b, src_obj);       /* method13170: p2 LE */
        out_p2_alt3(&b, src_slot);      /* method13196: p2 LE+128 */
        p4(&b, dst_com);                /* method13396: p4 BE */
        out_p2_alt2(&b, dst_obj);       /* method13174: p2 BE+128 */
        out_p2_alt2(&b, dst_slot);      /* method13174 */
        (void)mode;
        return 1 + (int)b.position;
    }

    /* Classic 2004 INV_BUTTOND: single component + mode byte. */
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_INV_BUTTOND, 5 + width, &b) < 0 )
        return -1;
    out_p_com(&b, width, src_com);
    p2(&b, src_slot);
    p2(&b, dst_slot);
    p1(&b, mode);
    (void)src_obj;
    (void)dst_com;
    (void)dst_obj;
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
