#include "net_out.h"

#include "jbase37.h"
#include "wordpack.h"

#include <rsbuffer.h>
#include <string.h>

/*
 * The ten alternate-byte-order writers this file used to define — out_p1_alt1,
 * out_p1_alt2, out_p1_alt3, out_p2_alt1..3, out_p4_alt1..3 and out_p8 — are
 * rsbuffer's. They lived here because the cache format only ever writes
 * big-endian, so rscache had no reason to carry them; the game protocol does,
 * and this outbound path writes into an RSCache_Buffer. See
 * `docs/BUFFER_ACCESSOR_AUDIT.md`.
 *
 * The names now say the byte order instead of an ordinal: `out_p4_alt3` was the
 * 2,1,4,3 permutation and is `p4_2143`. The width-1 forms are value transforms
 * with no order at all (`p1_neg` wrote -v), and the width-2 forms carry both,
 * which is why `p2_be` and `p2_be_add128` are separate names for the same byte
 * order and different bytes.
 */
#define out_p1_alt1 p1_add128
#define out_p1_alt2 p1_neg
#define out_p1_alt3 p1_sub128
#define out_p2_alt1 p2_le
#define out_p2_alt2 p2_be_add128
#define out_p2_alt3 p2_le_add128
#define out_p4_alt1 p4_le
#define out_p4_alt2 p4_3412
#define out_p4_alt3 p4_2143

/* p8 is two big-endian p4s, which is what RSCache_BufferP8 already is. */
#define out_p8(buffer, value) RSCache_BufferP8(buffer, value)


/* rsbuffer has no p8; write a big-endian i64 as two u32 words. */
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

/*
 * IF_BUTTONX — the packet twenty-two canonical names collapse into.
 *
 * From revision ~237 OldSchool stopped giving each verb its own opcode:
 * OPHELD1..5, INV_BUTTON1..5 and IF_BUTTON1..10 are all
 * `p4 combinedId, p2 sub, p2 obj, p1 op`, and the op number is a FIELD. `obj`
 * is the discriminator the server reads back — 0xffff means "not an item", so
 * a plain widget click and an inventory verb differ by that alone.
 *
 * Returns -1 when this revision has no IF_BUTTONX row, which is what every
 * revision before 237 looks like; the caller then builds its own opcode.
 */
static int
out_if_buttonx(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int op_num,
    int component_id,
    int sub,
    int obj_id)
{
    struct RSCache_Buffer b;

    if( !rev || rev->packetout_code(PKTOUT_NAME_IF_BUTTONX) < 0 )
        return -1;
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_IF_BUTTONX, 9, &b) < 0 )
        return -1;
    p4(&b, component_id);
    p2(&b, sub & 0xffff);
    p2(&b, obj_id & 0xffff);
    p1(&b, op_num & 0xff);
    return 1 + (int)b.position;
}

/** The sentinel IF_BUTTONX's `obj` carries when the click is not on an item. */
#define OUT_IF_BUTTONX_OBJ_NONE 0xffff

/* Defined beside the other alt-order writers it needs; used by the use-on
 * builders above them. */
static int
out_if_buttont(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int target_com,
    int target_sub,
    int target_obj,
    int selected_com,
    int selected_sub,
    int selected_obj);

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
    int wire_mode = mode;

    /* Revision 239 reports only the window class: 1 is fixed and 2 is
     * resizable.  The Display setting is finer grained (0 fixed, 1 resizable
     * classic, 2 resizable modern), so sending it verbatim turns Classic's 1
     * into Fixed on the server and causes the selected layout to be remounted
     * briefly before snapping back.  Revision 230's mock-local extension does
     * carry the three-way layout value and must remain unchanged. */
    if( rev && rev->revision == GAMEPROTO_REVISION_OSRS239 )
        wire_mode = mode == 0 ? 1 : 2;

    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_WINDOW_STATUS, 5, &b) < 0 )
        return -1;
    p1(&b, wire_mode & 0xff);
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

    int collapsed;

    if( op_num < 1 || op_num > 10 )
        return -1;
    /* Same collapse as OPHELD, and this is the one that costs a tab click: a
     * plain widget carries no item, so `obj` is the not-an-item sentinel and
     * the server reads the row back as IF_BUTTON<op>. */
    collapsed = out_if_buttonx(
        rev, random_out, buf, cap, op_num, component_id, sub, OUT_IF_BUTTONX_OBJ_NONE);
    if( collapsed >= 0 )
        return collapsed;
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
net_out_if_button_obj_op(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int op_num,
    int component_id,
    int sub,
    int obj_id)
{
    int collapsed;

    if( op_num < 1 || op_num > 10 )
        return -1;
    collapsed =
        out_if_buttonx(rev, random_out, buf, cap, op_num, component_id, sub, obj_id);
    if( collapsed >= 0 )
        return collapsed;
    return net_out_if_button_op(
        rev, random_out, buf, cap, op_num, component_id, sub);
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
    if( rev->revision == GAMEPROTO_REVISION_OSRS239 )
        out_p4_alt1(
            &b, ((level & 0x3) << 28) | ((abs_x & 0x3fff) << 14) | (abs_z & 0x3fff));
    else
        p4(&b, ((level & 0x3) << 28) | ((abs_x & 0x3fff) << 14) | (abs_z & 0x3fff));
    return 1 + (int)b.position;
}

int
net_out_resume_pausebutton(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int component_id,
    int sub_id)
{
    if( rev->revision == GAMEPROTO_REVISION_OSRS239 )
    {
        struct RSCache_Buffer b;
        if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_RESUME_PAUSEBUTTON, 6, &b) < 0 )
            return -1;
        out_p4_alt3(&b, component_id);
        p2(&b, sub_id < 0 ? 0xffff : sub_id);
        return 1 + (int)b.position;
    }
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

    /* Modern destination-only movement. 239 retains the outer var-byte length
     * but transforms both coordinates and the key-combination byte. */
    if( rev->revision == GAMEPROTO_REVISION_OSRS239 && trailer_len == 0 &&
        out_name == PKTOUT_NAME_MOVE_GAMECLICK )
    {
        if( out_begin(rev, random_out, buf, cap, out_name, 6, &b) < 0 )
            return -1;
        p1(&b, 5);
        out_p2_alt1(&b, base_x + route_x[0]);
        out_p2_alt1(&b, base_z + route_z[0]);
        out_p1_alt3(&b, ctrl_held ? 1 : 0);
        return 1 + (int)b.position;
    }
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
    if( rev->revision == GAMEPROTO_REVISION_OSRS239 )
    {
        struct RSCache_Buffer b239;
        if( route_len < 1 ||
            out_begin(rev, random_out, buf, cap, PKTOUT_NAME_MOVE_MINIMAPCLICK, 19, &b239) < 0 )
            return -1;
        p1(&b239, 18);
        out_p2_alt1(&b239, base_x + route_x[0]);
        out_p2_alt1(&b239, base_z + route_z[0]);
        out_p1_alt3(&b239, ctrl_held ? 1 : 0);
        p1(&b239, click_x);
        p1(&b239, click_y);
        p2(&b239, camera_yaw);
        p1(&b239, 57);
        p1(&b239, minimap_angle);
        p1(&b239, minimap_zoom);
        p1(&b239, 89);
        p2(&b239, player_fine_x);
        p2(&b239, player_fine_z);
        p1(&b239, nearest);
        return 1 + (int)b239.position;
    }
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
    int collapsed;

    if( op_num < 1 || op_num > 5 )
        return -1;
    /* Newer revisions have no OPHELD opcode at all. IF3's generic Use row is
     * op 1, so its five ObjType inventory actions are IF_BUTTONX ops 2..6.
     * (The golden client's default Drop fallback is op 7; either 6 or 7 maps
     * back to classic OPHELD5 at the server.) Tried first because
     * `packetout_code` is what decides, not the revision number. */
    collapsed =
        out_if_buttonx(rev, random_out, buf, cap, op_num + 1, component_id, slot, obj_id);
    if( collapsed >= 0 )
        return collapsed;
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

    /* The spell is the TARGET component and carries no obj, so its obj field is
     * the not-an-item sentinel — the same discriminator IF_BUTTONX uses. */
    int collapsed = out_if_buttont(
        rev, random_out, buf, cap, spell_component_id, -1, OUT_IF_BUTTONX_OBJ_NONE,
        component_id, slot, obj_id);

    if( collapsed >= 0 )
        return collapsed;
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

    int collapsed = out_if_buttont(
        rev, random_out, buf, cap, component_id, slot, obj_id, use_component_id, use_slot,
        use_obj_id);

    if( collapsed >= 0 )
        return collapsed;
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
    int collapsed;

    if( op_num < 1 || op_num > 5 )
        return -1;
    /* Same collapse as OPHELD and IF_BUTTON: INV_BUTTON<n> has no opcode of its
     * own from 237 on. */
    collapsed =
        out_if_buttonx(rev, random_out, buf, cap, op_num, component_id, slot, obj_id);
    if( collapsed >= 0 )
        return collapsed;
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
/* p4Alt2: [v>>8, v, v>>24, v>>16]. */
/* p4Alt3: [v>>16, v>>24, v, v>>8]. */
/*
 * IF_BUTTONT — "use the item I selected on this one", component to component.
 *
 * IfButtonTDecoder, in its own read order:
 *   pCombinedId target, p2Alt2 selectedSub, p2Alt3 targetObj,
 *   p2Alt1 targetSub, p2Alt2 selectedObj, pCombinedIdAlt2 selected
 *
 * Six fields, five different byte orders, and the two halves interleaved rather
 * than written as two triples — which is why this cannot be assembled from the
 * OPHELDU writer's field order with a different opcode. The TARGET is what was
 * clicked and the SELECTED is what was carried, matching 230's OPHELDU where
 * the first triple is the target.
 *
 * Returns -1 when the revision has no IF_BUTTONT row.
 */
static int
out_if_buttont(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int target_com,
    int target_sub,
    int target_obj,
    int selected_com,
    int selected_sub,
    int selected_obj)
{
    struct RSCache_Buffer b;

    if( !rev || rev->packetout_code(PKTOUT_NAME_IF_BUTTONT) < 0 )
        return -1;
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_IF_BUTTONT, 16, &b) < 0 )
        return -1;
    p4(&b, target_com);
    out_p2_alt2(&b, selected_sub);
    out_p2_alt3(&b, target_obj);
    out_p2_alt1(&b, target_sub);
    out_p2_alt2(&b, selected_obj);
    out_p4_alt2(&b, selected_com);
    return 1 + (int)b.position;
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
    if( rev->revision == GAMEPROTO_REVISION_OSRS239 )
    {
        if( out_begin(rev, random_out, buf, cap, out_name, 8, &b) < 0 )
            return -1;
        if( out_name >= PKTOUT_NAME_OPLOC1 && out_name <= PKTOUT_NAME_OPLOC5 )
        {
            switch( out_name - PKTOUT_NAME_OPLOC1 + 1 )
            {
            case 1:
                out_p2_alt2(&b, abs_z); out_p2_alt1(&b, abs_x);
                out_p1_alt1(&b, 0); out_p1_alt1(&b, 0); p2(&b, id); break;
            case 2:
                out_p2_alt3(&b, abs_z); out_p2_alt3(&b, id); p2(&b, abs_x);
                out_p1_alt2(&b, 0); p1(&b, 0); break;
            case 3:
                out_p2_alt2(&b, id); out_p1_alt3(&b, 0); p1(&b, 0);
                out_p2_alt3(&b, abs_z); out_p2_alt2(&b, abs_x); break;
            case 4:
                out_p1_alt1(&b, 0); out_p2_alt1(&b, abs_x); out_p2_alt3(&b, id);
                out_p1_alt2(&b, 0); p2(&b, abs_z); break;
            default:
                out_p2_alt3(&b, abs_z); out_p2_alt1(&b, abs_x);
                out_p1_alt1(&b, 0); out_p1_alt2(&b, 0); p2(&b, id); break;
            }
        }
        else
        {
            switch( out_name - PKTOUT_NAME_OPOBJ1 + 1 )
            {
            case 1:
                out_p2_alt2(&b, id); out_p1_alt3(&b, 0); out_p2_alt2(&b, abs_z);
                out_p1_alt3(&b, 0); p2(&b, abs_x); break;
            case 2:
                out_p1_alt1(&b, 0); out_p1_alt3(&b, 0); p2(&b, abs_z);
                out_p2_alt1(&b, id); out_p2_alt3(&b, abs_x); break;
            case 3:
                out_p1_alt1(&b, 0); out_p2_alt1(&b, abs_x); out_p2_alt3(&b, id);
                out_p1_alt1(&b, 0); out_p2_alt2(&b, abs_z); break;
            case 4:
                out_p1_alt3(&b, 0); p2(&b, id); p2(&b, abs_x);
                out_p2_alt1(&b, abs_z); out_p1_alt2(&b, 0); break;
            default:
                out_p1_alt2(&b, 0); out_p2_alt2(&b, abs_x); p2(&b, id);
                out_p2_alt3(&b, abs_z); out_p1_alt1(&b, 0); break;
            }
        }
        return 1 + (int)b.position;
    }
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
    if( rev->revision == GAMEPROTO_REVISION_OSRS239 )
    {
        if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_OPLOCT, 15, &b) < 0 )
            return -1;
        out_p1_alt2(&b, 0);
        out_p2_alt2(&b, 0xffff);
        out_p2_alt1(&b, loc_id);
        out_p4_alt1(&b, spell_component_id);
        out_p2_alt3(&b, abs_z);
        out_p2_alt1(&b, abs_x);
        out_p2_alt1(&b, 0xffff);
        return 1 + (int)b.position;
    }
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
    if( rev->revision == GAMEPROTO_REVISION_OSRS239 )
    {
        if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_OPLOCT, 15, &b) < 0 )
            return -1;
        out_p1_alt2(&b, 0);
        out_p2_alt2(&b, use_slot);
        out_p2_alt1(&b, loc_id);
        out_p4_alt1(&b, use_component_id);
        out_p2_alt3(&b, abs_z);
        out_p2_alt1(&b, abs_x);
        out_p2_alt1(&b, use_obj_id);
        return 1 + (int)b.position;
    }
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
    if( rev->revision == GAMEPROTO_REVISION_OSRS239 &&
        out_name >= PKTOUT_NAME_OPNPC1 && out_name <= PKTOUT_NAME_OPNPC5 )
    {
        if( out_begin(rev, random_out, buf, cap, out_name, 4, &b) < 0 )
            return -1;
        switch( out_name - PKTOUT_NAME_OPNPC1 + 1 )
        {
        case 1:
            out_p2_alt1(&b, slot); out_p1_alt2(&b, 0); out_p1_alt3(&b, 0); break;
        case 2:
            out_p2_alt3(&b, slot); out_p1_alt2(&b, 0); out_p1_alt3(&b, 0); break;
        case 3:
            out_p1_alt2(&b, 0); out_p1_alt3(&b, 0); out_p2_alt2(&b, slot); break;
        case 4:
            out_p1_alt1(&b, 0); p2(&b, slot); out_p1_alt2(&b, 0); break;
        default:
            out_p1_alt1(&b, 0); out_p1_alt2(&b, 0); out_p2_alt2(&b, slot); break;
        }
        return 1 + (int)b.position;
    }
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
    if( rev->revision == GAMEPROTO_REVISION_OSRS239 )
    {
        if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_OPNPCT, 11, &b) < 0 )
            return -1;
        out_p1_alt2(&b, 0);
        p2(&b, 0xffff);
        out_p2_alt2(&b, npc_slot);
        p2(&b, 0xffff);
        p4(&b, spell_component_id);
        return 1 + (int)b.position;
    }
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
    if( rev->revision == GAMEPROTO_REVISION_OSRS239 )
    {
        if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_OPNPCT, 11, &b) < 0 )
            return -1;
        out_p1_alt2(&b, 0);
        p2(&b, use_slot);
        out_p2_alt2(&b, npc_slot);
        p2(&b, use_obj_id);
        p4(&b, use_component_id);
        return 1 + (int)b.position;
    }
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
    if( rev->revision == GAMEPROTO_REVISION_OSRS239 )
    {
        if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_OPOBJT, 15, &b) < 0 )
            return -1;
        out_p2_alt1(&b, 0xffff);
        out_p2_alt2(&b, obj_id);
        p2(&b, abs_x);
        out_p4_alt2(&b, spell_component_id);
        p1(&b, 0);
        out_p2_alt1(&b, 0xffff);
        p2(&b, abs_z);
        return 1 + (int)b.position;
    }
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
    if( rev->revision == GAMEPROTO_REVISION_OSRS239 )
    {
        if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_OPOBJT, 15, &b) < 0 )
            return -1;
        out_p2_alt1(&b, use_obj_id);
        out_p2_alt2(&b, obj_id);
        p2(&b, abs_x);
        out_p4_alt2(&b, use_component_id);
        p1(&b, 0);
        out_p2_alt1(&b, use_slot);
        p2(&b, abs_z);
        return 1 + (int)b.position;
    }
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
    struct RSCache_Buffer b;

    if( op_num < 1 || op_num > 5 )
        return -1;
    if( rev->revision == GAMEPROTO_REVISION_OSRS239 )
    {
        int out_name = PKTOUT_NAME_OPPLAYER1 + (op_num - 1);
        if( out_begin(rev, random_out, buf, cap, out_name, 3, &b) < 0 )
            return -1;
        switch( op_num )
        {
        case 1: out_p1_alt3(&b, 0); out_p2_alt1(&b, player_slot); break;
        case 2: out_p2_alt1(&b, player_slot); out_p1_alt2(&b, 0); break;
        case 3: out_p1_alt2(&b, 0); out_p2_alt3(&b, player_slot); break;
        case 4: p2(&b, player_slot); p1(&b, 0); break;
        default: out_p1_alt3(&b, 0); p2(&b, player_slot); break;
        }
        return 1 + (int)b.position;
    }
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
    if( rev->revision == GAMEPROTO_REVISION_OSRS239 )
    {
        if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_OPPLAYERT, 11, &b) < 0 )
            return -1;
        out_p2_alt2(&b, 0xffff);
        out_p1_alt1(&b, 0);
        out_p2_alt3(&b, 0xffff);
        out_p4_alt2(&b, spell_component_id);
        p2(&b, player_slot);
        return 1 + (int)b.position;
    }
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
    if( rev->revision == GAMEPROTO_REVISION_OSRS239 )
    {
        if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_OPPLAYERT, 11, &b) < 0 )
            return -1;
        out_p2_alt2(&b, use_slot);
        out_p1_alt1(&b, 0);
        out_p2_alt3(&b, use_obj_id);
        out_p4_alt2(&b, use_component_id);
        p2(&b, player_slot);
        return 1 + (int)b.position;
    }
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
    /* 239 uses var-u16 + NUL recipient; classic uses var-u8 + name37. */
    struct RSCache_Buffer b;
    int len_pos;
    int start;
    if( !text )
        return -1;
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_MESSAGE_PRIVATE, 12, &b) < 0 )
        return -1;
    if( rev->revision == GAMEPROTO_REVISION_OSRS239 )
    {
        char name[16];
        p2(&b, 0);
        len_pos = (int)b.position - 2;
        start = (int)b.position;
        base37tostr((uint64_t)to_name37, name, (int)sizeof(name));
        pjstr(&b, name, RSCACHE_JSTR_TERMINATOR_NULL);
        wordpack_pack(&b, text);
        {
            int payload_len = (int)b.position - start;
            buf[1 + len_pos] = (uint8_t)(payload_len >> 8);
            buf[1 + len_pos + 1] = (uint8_t)payload_len;
        }
        return 1 + (int)b.position;
    }
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
    pjstr(
        &b,
        text,
        rev->revision == GAMEPROTO_REVISION_OSRS239 ? RSCACHE_JSTR_TERMINATOR_NULL
                                                    : RSCACHE_JSTR_TERMINATOR_NEWLINE);
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
    if( rev->revision == GAMEPROTO_REVISION_OSRS239 )
    {
        char name[16];
        int body_len;

        base37tostr((uint64_t)name37, name, (int)sizeof(name));
        body_len = (int)strlen(name) + 1;
        if( out_begin(rev, random_out, buf, cap, out_name, body_len + 1, &b) < 0 )
            return -1;
        p1(&b, body_len);
        pjstr(&b, name, RSCACHE_JSTR_TERMINATOR_NULL);
        return 1 + (int)b.position;
    }
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
