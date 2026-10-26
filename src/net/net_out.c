#include "net_out.h"
#include <assert.h>

#include "jbase37.h"
#include "net/proto/proto_out.h"

#include <rsbuffer.h>
#include <string.h>

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

/*
 * Every revision writes through RSProt (net/proto/proto_out.h): each writer
 * below says what its packet means -- fills a struct ProtoOutArgs -- and
 * proto_encode lays it out the way the revision does, version, field order
 * and byte transforms included. Where the families mean different things by
 * one click (a use-on, a walk), the writer states every meaning and each
 * revision's layout takes the fields it carries. Chat text is the
 * revision's own (GameProtoRevTable.chat_pack): OldSchool's Huffman,
 * LostCity's wordpack.
 */

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
    struct ProtoOutArgs a = { 0 };

    if( !rev || !proto_out_speaks(rev) || !proto_out_has(rev, PKTOUT_NAME_IF_BUTTONX) )
        return -1;
    a.combined_id = component_id;
    a.sub = sub & 0xffff;
    a.obj = obj_id & 0xffff;
    a.op = op_num & 0xff;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_IF_BUTTONX, &a);
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
    struct ProtoOutArgs a = { 0 };

    return proto_encode(rev, random_out, buf, cap, out_name, &a);
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

    /* The OSRS revisions report only the window class: 1 is fixed and 2 is
     * resizable.  The Display setting is finer grained (0 fixed, 1 resizable
     * classic, 2 resizable modern), so sending it verbatim turns Classic's 1
     * into Fixed on the server and causes the selected layout to be remounted
     * briefly before snapping back. */
    struct ProtoOutArgs a = { 0 };

    a.window_mode = mode == 0 ? 1 : 2;
    a.frame_width = width & 0xffff;
    a.frame_height = height & 0xffff;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_WINDOW_STATUS, &a);
}

int
net_out_send_ping_reply(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int value1,
    int value2,
    int fps)
{
    struct ProtoOutArgs a = { 0 };

    a.value1 = value1;
    a.value2 = value2;
    a.fps = fps;
    /* The share of time the reference's JVM spent collecting garbage: this
     * client has no collector. */
    a.gc_percent_time = 0;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_SEND_PING_REPLY, &a);
}

/* -- interface ---------------------------------------------------------- */

int
net_out_if_button(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int component_id)
{
    struct ProtoOutArgs a = { 0 };

    a.combined_id = component_id;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_IF_BUTTON, &a);
}

int
net_out_if_script_trigger(struct GameProtoRevTable const* rev,
    struct Isaac* random_out, uint8_t* buf, int cap, int crc, int component_id,
    int child, int object_id, const char* signature, const int* values,
    const char* const* strings)
{
    assert(rev);
    assert(random_out);
    assert(buf);
    assert(signature);
    assert(values);
    assert(strings);
    /* The arguments after the fixed twelve bytes: a zigzag varint per
     * `i`, a NUL-terminated string per `s` -- the tail RSProt carries as
     * `copy` (IfScriptTriggerDecoder). */
    static uint8_t tail[65535];
    struct RSCache_Buffer t;
    struct ProtoOutArgs a = { 0 };
    int count = (int)strlen(signature);

    if( count > 16 )
        return -1;
    RSCache_BufferInit(&t, tail, (uint32_t)sizeof(tail));
    for( int i = 0; i < count; ++i )
    {
        if( signature[i] == 'i' )
        {
            uint32_t value = ((uint32_t)values[i] << 1) ^ (uint32_t)-(values[i] < 0);
            while( value >= 128 )
            {
                p1(&t, (value & 127) | 128);
                value >>= 7;
            }
            p1(&t, value);
        }
        else
        {
            assert(strings[i]);
            if( strlen(strings[i]) + 1 > sizeof(tail) - t.position )
                return -1;
            pjstr(&t, strings[i], RSCACHE_JSTR_TERMINATOR_NULL);
        }
    }
    a.crc = crc;
    a.combined_id = component_id;
    a.sub = child & 0xffff;
    a.obj = object_id & 0xffff;
    a.copy = tail;
    a.copy_length = (int32_t)t.position;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_IF_SCRIPT_TRIGGER, &a);
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
    int collapsed;
    struct ProtoOutArgs a = { 0 };

    if( op_num < 1 || op_num > 10 )
        return -1;
    /* Same collapse as OPHELD, and this is the one that costs a tab click: a
     * plain widget carries no item, so `obj` is the not-an-item sentinel and
     * the server reads the row back as IF_BUTTON<op>. */
    collapsed = out_if_buttonx(
        rev, random_out, buf, cap, op_num, component_id, sub, OUT_IF_BUTTONX_OBJ_NONE);
    if( collapsed >= 0 )
        return collapsed;
    a.combined_id = component_id;
    a.sub = sub & 0xffff;
    a.obj = OUT_IF_BUTTONX_OBJ_NONE;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_IF_BUTTON1 + (op_num - 1), &a);
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
    /* RSProt ClickWorldMap packs the destination into one int, the same
     * level<<28 | x<<14 | z coord the CS2 map ops speak. */
    int packed = ((level & 0x3) << 28) | ((abs_x & 0x3fff) << 14) | (abs_z & 0x3fff);

    struct ProtoOutArgs a = { 0 };

    a.packed = packed;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_CLICK_WORLD_MAP, &a);
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
    struct ProtoOutArgs a = { 0 };

    a.combined_id = component_id;
    a.sub = sub_id < 0 ? 0xffff : sub_id;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_RESUME_PAUSEBUTTON, &a);
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
    struct ProtoOutArgs a = { 0 };

    a.count = amount;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_RESUME_P_COUNTDIALOG, &a);
}

int
net_out_resume_namedialog(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap,
    char const* name)
{
    struct ProtoOutArgs a = { 0 };

    a.name = name;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_RESUME_P_NAMEDIALOG, &a);
}

int
net_out_resume_stringdialog(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap,
    char const* text)
{
    struct ProtoOutArgs a = { 0 };

    a.string = text;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_RESUME_P_STRINGDIALOG, &a);
}

int
net_out_resume_countdialog_long(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap,
    int64_t amount)
{
    struct ProtoOutArgs a = { 0 };

    a.count_long = amount;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_RESUME_P_COUNTDIALOG_LONG, &a);
}

int
net_out_resume_objdialog(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap,
    int obj_id)
{
    struct ProtoOutArgs a = { 0 };

    a.obj = obj_id;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_RESUME_P_OBJDIALOG, &a);
}

/* RSProt BugReportDecoder, revisions 222-239: jstr description, g1Alt3 type,
 * jstr instructions, framed by a var-u16 length. */
int
net_out_bug_report(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap,
    char const* description, char const* instructions, int template_id)
{
    assert(description);
    assert(instructions);
    struct ProtoOutArgs a = { 0 };

    a.description = description;
    a.instructions = instructions;
    a.type = template_id;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_BUG_REPORT, &a);
}

/* RSProt SendSnapshotDecoder: jstr name, g1 rule, g1 mute (== 1). */
int
net_out_send_snapshot(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap,
    char const* name, int rule_id, int mute)
{
    assert(name);
    struct ProtoOutArgs a = { 0 };

    a.name = name;
    a.rule_id = rule_id;
    a.mute = mute;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_SEND_SNAPSHOT, &a);
}

/* RSProt FriendChatJoinLeaveDecoder: an empty body is a leave. */
int
net_out_friendchat_join_leave(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap,
    char const* name)
{
    struct ProtoOutArgs a = { 0 };

    a.has_name = name != NULL;
    a.name = name;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_FRIENDCHAT_JOIN_LEAVE, &a);
}

int
net_out_friendchat_kick(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap,
    char const* name)
{
    struct ProtoOutArgs a = { 0 };

    a.name = name;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_FRIENDCHAT_KICK, &a);
}

/* RSProt FriendChatSetRankDecoder at rev 239 (codec v4): g1Alt3 rank, jstr name. */
int
net_out_friendchat_setrank(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap,
    char const* name, int rank)
{
    assert(name);
    struct ProtoOutArgs a = { 0 };

    a.name = name;
    a.rank = rank;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_FRIENDCHAT_SETRANK, &a);
}

int
net_out_clanchannel_full_request(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap,
    int clan_id)
{
    struct ProtoOutArgs a = { 0 };

    a.clan_id = clan_id;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_CLANCHANNEL_FULL_REQUEST, &a);
}

int
net_out_clansettings_full_request(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap,
    int clan_id)
{
    struct ProtoOutArgs a = { 0 };

    a.clan_id = clan_id;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_CLANSETTINGS_FULL_REQUEST, &a);
}

/* The two member-moderation requests share one body: g1 clan, g2 member, jstr name. */
static int
out_clan_member(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap,
    int out_name, int clan_id, int member_index, int muted, char const* name)
{
    assert(name);
    struct ProtoOutArgs a = { 0 };

    a.clan_id = clan_id;
    a.member_index = member_index;
    a.muted = muted < 0 ? 0 : muted;
    a.name = name;
    return proto_encode(rev, random_out, buf, cap, out_name, &a);
}

int
net_out_clanchannel_kickuser(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap,
    int clan_id, int member_index, char const* name)
{
    return out_clan_member(rev, random_out, buf, cap, PKTOUT_NAME_CLANCHANNEL_KICKUSER, clan_id, member_index, -1, name);
}

int
net_out_affinedclansettings_addbanned_fromchannel(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap,
    int clan_id, int member_index, char const* name)
{
    return out_clan_member(rev, random_out, buf, cap, PKTOUT_NAME_AFFINEDCLANSETTINGS_ADDBANNED_FROMCHANNEL, clan_id, member_index, -1, name);
}

/* RSProt AffinedClanSettingsSetMutedFromChannelDecoder: g1 clan, g2 member,
 * g1 muted (== 1), jstr name. */
int
net_out_affinedclansettings_setmuted_fromchannel(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap,
    int clan_id, int member_index, int muted, char const* name)
{
    return out_clan_member(rev, random_out, buf, cap, PKTOUT_NAME_AFFINEDCLANSETTINGS_SETMUTED_FROMCHANNEL, clan_id, member_index, muted ? 1 : 0, name);
}

int
net_out_tut_clickside(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int tab)
{
    struct ProtoOutArgs a = { 0 };

    a.tab = tab;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_TUT_CLICKSIDE, &a);
}

/* -- movement ----------------------------------------------------------- */

/* A MOVE_* click's meaning, both ways a revision may want it. `route` uses the
 * tryMove scratch layout (collision_map_try_route): [0] = destination,
 * ascending toward the source. The OSRS revisions send the destination only
 * (the server paths); the LostCity ones send the start tile and up to 24
 * steps from it. A 1-entry route is start == destination with no steps. */
static int
out_move_args(
    struct ProtoOutArgs* a,
    int base_x,
    int base_z,
    int const* route_x,
    int const* route_z,
    int route_len,
    int ctrl_held)
{
    /* At most the start and 24 steps: the classic packet's own limit. */
    int steps = route_len < 25 ? route_len : 25;
    int idx = route_len - 1;

    if( route_len < 1 )
        return -1;
    a->key_combination = ctrl_held ? 1 : 0;
    a->x = base_x + route_x[0];
    a->z = base_z + route_z[0];
    a->route_x = base_x + route_x[idx];
    a->route_z = base_z + route_z[idx];
    a->route_count = steps - 1;
    for( int i = 0; i < steps - 1; i++ )
    {
        idx--;
        a->route_dx[i] = route_x[idx] - route_x[route_len - 1];
        a->route_dz[i] = route_z[idx] - route_z[route_len - 1];
    }
    return 0;
}

int
net_out_set_heading(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int heading)
{

    assert(heading >= 0);
    assert(heading < 16);
    struct ProtoOutArgs a = { 0 };

    a.heading = heading;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_SET_HEADING, &a);
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
    struct ProtoOutArgs a = { 0 };

    if( out_move_args(&a, base_x, base_z, route_x, route_z, route_len, ctrl_held) < 0 )
        return -1;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_MOVE_GAMECLICK, &a);
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
    struct ProtoOutArgs a = { 0 };

    if( out_move_args(&a, base_x, base_z, route_x, route_z, route_len, ctrl_held) < 0 )
        return -1;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_MOVE_OPCLICK, &a);
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
    /* The click, then the anticheat trailer (the two checkpoint constants are
     * the client's own, 57 and 89). */
    struct ProtoOutArgs a = { 0 };

    if( out_move_args(&a, base_x, base_z, route_x, route_z, route_len, ctrl_held) < 0 )
        return -1;
    a.minimap_width = click_x;
    a.minimap_height = click_y;
    a.camera_angle_y = camera_yaw;
    a.checkpoint1 = 57;
    a.checkpoint2 = minimap_angle;
    a.checkpoint3 = minimap_zoom;
    a.checkpoint4 = 89;
    a.fine_x = player_fine_x;
    a.fine_z = player_fine_z;
    a.checkpoint5 = nearest;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_MOVE_MINIMAPCLICK, &a);
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
    struct ProtoOutArgs a = { 0 };

    a.obj = obj_id;
    a.sub = slot;
    a.combined_id = component_id;
    return proto_encode(rev, random_out, buf, cap, out_name, &a);
}

/* The five ObjType inventory actions as COMPONENT ops on rev-239's backpack.
 * File scope because two callers need it now: the packet below, and
 * app_minimenu.c's cell flash, which re-enters the cell's own on_op with an
 * op number and was handing it the OPHELD index instead.  See
 * net_out_opheld_component_op. */
static const int OPHELD_IF_BUTTONX_OP[5] = { 2, 3, 4, 6, 7 };

/*
 * THE OP NUMBER A BACKPACK ROW REALLY CARRIES, for anything that has to speak
 * to the interface in its own op space rather than OPHELD's.
 *
 * The one caller that is not this file is the cell flash in app_minimenu.c:
 * rev-239's backpack installs cc_setonop on every cell, and that handler is
 * not cosmetic -- op 1 there is the shift-click-drop chain, which answers by
 * naming the real Drop op through cc_triggerop.  Dispatching it with the
 * OPHELD index meant every OPHELD1 press ("Dig", "Eat", "Open", ...) ran the
 * drop chain a tick later and put the item on the floor: measured on the
 * spade at three separate dig tiles, `<- OPHELD1 obj=952` followed every time
 * by `<- IF_BUTTONX 149:0 op=7` and `<- OPHELD5`
 * (build/quest_gate/seam_heldop1_probe2/client.log, 2026-09-20).
 *
 * 0 when `op_num` is not one of the five.
 */
int
net_out_opheld_component_op(int op_num)
{
    if( op_num < 1 || op_num > 5 )
        return 0;
    return OPHELD_IF_BUTTONX_OP[op_num - 1];
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
    /* Newer revisions have no OPHELD opcode at all: the five ObjType inventory
     * actions leave as IF_BUTTONX ops on the backpack component. The mapping is
     * NOT consecutive. Rev 239's backpack is scripted (script_7779 over
     * enum_4303): generic Use owns op 1, op 5 stays a plain component action,
     * and the five iop rows land on 2, 3, 4, 6, 7. This table is the exact
     * inverse of the server's own decoder, mock239_if_button_backpack_op
     * (src/torirsserver/mock239_interface_inbound.c), which a captured official
     * packet pins: the Dreadfowl pouch's iop4 Summon arrives as wire op 6
     * (src/torirsserver/test/mock239_interface_inbound_test.c).
     *
     * This used to be a flat `op_num + 1`, which got ops 1..3 right by accident
     * and both of the others wrong: OPHELD4 went out as op 5 and decoded as no
     * held action at all, and OPHELD5 -- Drop -- went out as op 6 and arrived
     * as OPHELD4, so a right-click Drop answered "Nothing interesting happens."
     * Nothing exercised either op end to end until the quest driver's
     * player.drop verb did. */
    int collapsed;

    if( op_num < 1 || op_num > 5 )
        return -1;
    /* Tried first because `packetout_code` is what decides, not the revision
     * number. */
    collapsed = out_if_buttonx(
        rev, random_out, buf, cap, OPHELD_IF_BUTTONX_OP[op_num - 1], component_id, slot, obj_id);
    if( collapsed >= 0 )
        return collapsed;
    return out_obj_slot_com(
        rev, random_out, buf, cap, PKTOUT_NAME_OPHELD1 + (op_num - 1), obj_id, slot, component_id);
}

/*
 * IF_SUBOP: an ObjType op's submenu entry (`subaction=<op>,<n>,<name>` in the
 * obj record, Drakan's medallion's Teleport > Ver Sinhaza). It is IF_BUTTONX
 * with one more byte, the entry, 0-based on the wire: the deob's send
 * (Statics method at class246.field3140, `new class246(40, 10)`) writes the
 * same combinedId/sub/obj/op as IF_BUTTONX, then `var6 - 1`, where var6 == 0
 * sends IF_BUTTONX instead. RSProx index 4286 [427] pins the base: a ring of
 * dueling's Rub `subop=1` landed at Castle Wars, its `subaction=4,2`.
 * `subop` here is that 0-based entry. Returns -1 when the revision has no
 * IF_SUBOP (every revision before the collapsed IF_BUTTONX family).
 */
int
net_out_opheld_subop(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int op_num,
    int subop,
    int obj_id,
    int slot,
    int component_id)
{
    assert(op_num >= 1);
    assert(op_num <= 5);
    assert(subop >= 0);
    assert(subop <= 0xff);
    if( !rev || !proto_out_speaks(rev) || !proto_out_has(rev, PKTOUT_NAME_IF_SUBOP) )
        return -1;
    {
        struct ProtoOutArgs a = { 0 };

        a.combined_id = component_id;
        a.sub = slot & 0xffff;
        a.obj = obj_id & 0xffff;
        a.op = OPHELD_IF_BUTTONX_OP[op_num - 1] & 0xff;
        a.subop = subop;
        return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_IF_SUBOP, &a);
    }
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
    /* The spell is the TARGET component and carries no obj, so its obj field is
     * the not-an-item sentinel — the same discriminator IF_BUTTONX uses. */
    struct ProtoOutArgs a = { 0 };
    int collapsed = out_if_buttont(
        rev, random_out, buf, cap, spell_component_id, -1, OUT_IF_BUTTONX_OBJ_NONE,
        component_id, slot, obj_id);

    if( collapsed >= 0 )
        return collapsed;
    a.obj = obj_id;
    a.sub = slot;
    a.combined_id = component_id;
    a.selected_combined_id = spell_component_id;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_OPHELDT, &a);
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
    struct ProtoOutArgs a = { 0 };
    int collapsed = out_if_buttont(
        rev, random_out, buf, cap, component_id, slot, obj_id, use_component_id, use_slot,
        use_obj_id);

    if( collapsed >= 0 )
        return collapsed;
    a.obj = obj_id;
    a.sub = slot;
    a.combined_id = component_id;
    a.selected_obj = use_obj_id;
    a.selected_sub = use_slot;
    a.selected_combined_id = use_component_id;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_OPHELDU, &a);
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
    struct ProtoOutArgs a = { 0 };

    if( !rev || !proto_out_speaks(rev) || !proto_out_has(rev, PKTOUT_NAME_IF_BUTTONT) )
        return -1;
    a.target_combined_id = target_com;
    a.target_sub = target_sub & 0xffff;
    a.target_obj = target_obj & 0xffff;
    a.selected_combined_id = selected_com;
    a.selected_sub = selected_sub & 0xffff;
    a.selected_obj = selected_obj & 0xffff;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_IF_BUTTONT, &a);
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
    /* IfButtonDDecoder: the drag's two endpoints, each component + sub + obj
     * (selected = the dragged-from end). LostCity's INV_BUTTOND names one
     * component, the two slots and the drag mode. */
    struct ProtoOutArgs a = { 0 };

    a.selected_combined_id = src_com;
    a.selected_obj = src_obj & 0xffff;
    a.selected_sub = src_slot & 0xffff;
    a.target_combined_id = dst_com;
    a.target_obj = dst_obj & 0xffff;
    a.target_sub = dst_slot & 0xffff;
    a.combined_id = src_com;
    a.mode = mode;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_INV_BUTTOND, &a);
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
    /* No sub-op and no control key, as the client has always sent. */
    struct ProtoOutArgs a = { 0 };

    a.x = abs_x;
    a.z = abs_z;
    a.id = id;
    return proto_encode(rev, random_out, buf, cap, out_name, &a);
}

/*
 * The OSRS targeted ops (OPLOCT, OPOBJT, OPNPCT, OPPLAYERT): what is clicked,
 * and the selected component -- a spell (no sub, no obj: 0xffff each) or the
 * carried item of a use-on, which travels as the same T packet.
 */
static int
out_proto_t(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int out_name,
    struct ProtoOutArgs* a,
    int selected_com,
    int selected_sub,
    int selected_obj)
{
    a->selected_combined_id = selected_com;
    a->selected_sub = selected_sub & 0xffff;
    a->selected_obj = selected_obj & 0xffff;
    return proto_encode(rev, random_out, buf, cap, out_name, a);
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
    struct ProtoOutArgs a = { 0 };

    a.x = abs_x;
    a.z = abs_z;
    a.id = loc_id;
    return out_proto_t(rev, random_out, buf, cap, PKTOUT_NAME_OPLOCT, &a, spell_component_id,
                       0xffff, 0xffff);
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
    /* Use-on is its own packet where the revision has one; the OSRS ones send
     * it as OPLOCT, the selected triple being the carried item. */
    struct ProtoOutArgs a = { 0 };

    a.x = abs_x;
    a.z = abs_z;
    a.id = loc_id;
    return out_proto_t(rev, random_out, buf, cap,
                       proto_out_has(rev, PKTOUT_NAME_OPLOCU) ? PKTOUT_NAME_OPLOCU : PKTOUT_NAME_OPLOCT, &a,
                       use_component_id, use_slot, use_obj_id);
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
    struct ProtoOutArgs a = { 0 };

    a.index = slot;
    return proto_encode(rev, random_out, buf, cap, out_name, &a);
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
    struct ProtoOutArgs a = { 0 };

    a.index = npc_slot;
    return out_proto_t(rev, random_out, buf, cap, PKTOUT_NAME_OPNPCT, &a, spell_component_id,
                       0xffff, 0xffff);
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
    /* Use-on is its own packet where the revision has one; the OSRS ones send
     * it as OPNPCT, the selected triple being the carried item. */
    struct ProtoOutArgs a = { 0 };

    a.index = npc_slot;
    return out_proto_t(rev, random_out, buf, cap,
                       proto_out_has(rev, PKTOUT_NAME_OPNPCU) ? PKTOUT_NAME_OPNPCU : PKTOUT_NAME_OPNPCT, &a,
                       use_component_id, use_slot, use_obj_id);
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
    struct ProtoOutArgs a = { 0 };

    a.x = abs_x;
    a.z = abs_z;
    a.id = obj_id;
    return out_proto_t(rev, random_out, buf, cap, PKTOUT_NAME_OPOBJT, &a, spell_component_id,
                       0xffff, 0xffff);
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
    /* Use-on is its own packet where the revision has one; the OSRS ones send
     * it as OPOBJT, the selected triple being the carried item. */
    struct ProtoOutArgs a = { 0 };

    a.x = abs_x;
    a.z = abs_z;
    a.id = obj_id;
    return out_proto_t(rev, random_out, buf, cap,
                       proto_out_has(rev, PKTOUT_NAME_OPOBJU) ? PKTOUT_NAME_OPOBJU : PKTOUT_NAME_OPOBJT, &a,
                       use_component_id, use_slot, use_obj_id);
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
    /* Not consecutive: OPPLAYER6..8 were named after OPPLAYERT. */
    static int const k_names[8] = {
        PKTOUT_NAME_OPPLAYER1, PKTOUT_NAME_OPPLAYER2, PKTOUT_NAME_OPPLAYER3, PKTOUT_NAME_OPPLAYER4,
        PKTOUT_NAME_OPPLAYER5, PKTOUT_NAME_OPPLAYER6, PKTOUT_NAME_OPPLAYER7, PKTOUT_NAME_OPPLAYER8,
    };

    if( op_num < 1 || op_num > 8 )
        return -1;
    return out_slot2(rev, random_out, buf, cap, k_names[op_num - 1], player_slot);
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
    struct ProtoOutArgs a = { 0 };

    a.index = player_slot;
    return out_proto_t(rev, random_out, buf, cap, PKTOUT_NAME_OPPLAYERT, &a, spell_component_id,
                       0xffff, 0xffff);
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
    /* Use-on is its own packet where the revision has one; the OSRS ones send
     * it as OPPLAYERT, the selected triple being the carried item. */
    struct ProtoOutArgs a = { 0 };

    a.index = player_slot;
    return out_proto_t(rev, random_out, buf, cap,
                       proto_out_has(rev, PKTOUT_NAME_OPPLAYERU) ? PKTOUT_NAME_OPPLAYERU : PKTOUT_NAME_OPPLAYERT, &a,
                       use_component_id, use_slot, use_obj_id);
}

/* -- chat / social ------------------------------------------------------ */

int
net_out_message_public(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    char const* text,
    int colour_effect)
{
    uint8_t packed[256];
    struct ProtoOutArgs a = { 0 };
    int packed_len;

    assert(text);
    assert(rev->chat_pack);
    packed_len = rev->chat_pack(packed, (int)sizeof(packed), text);
    if( rev->revision == GAMEPROTO_REVISION_OSRS )
    {
        /* MessagePublic: type 0 (a plain line; the deob's CHAT_SENDPUBLIC
         * pops it), colour, effect, the text. No pattern (colours 13-20) and
         * no clan type (type 3) are sent from here. */
        a.type = 0;
        a.colour = (colour_effect >> 8) & 0xff;
        a.effect = colour_effect & 0xff;
        a.huffman = packed;
        a.huffman_length = packed_len;
        return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_MESSAGE_PUBLIC, &a);
    }
    {
        /* LostCity: colour, effect and the text as the layout's `copy`. */
        uint8_t body[2 + sizeof(packed)];

        body[0] = (uint8_t)((colour_effect >> 8) & 0xff);
        body[1] = (uint8_t)(colour_effect & 0xff);
        memcpy(body + 2, packed, (size_t)packed_len);
        a.copy = body;
        a.copy_length = 2 + packed_len;
        return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_MESSAGE_PUBLIC, &a);
    }
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
    uint8_t packed[256];
    struct ProtoOutArgs a = { 0 };
    int packed_len;

    assert(text);
    assert(rev->chat_pack);
    packed_len = rev->chat_pack(packed, (int)sizeof(packed), text);
    if( rev->revision == GAMEPROTO_REVISION_OSRS )
    {
        /* MessagePrivate: the recipient's display name, then the text. */
        char name[16];

        base37tostr((uint64_t)to_name37, name, (int)sizeof(name));
        a.name = name;
        a.huffman = packed;
        a.huffman_length = packed_len;
        return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_MESSAGE_PRIVATE, &a);
    }
    a.name37 = to_name37;
    a.copy = packed;
    a.copy_length = packed_len;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_MESSAGE_PRIVATE, &a);
}

int
net_out_client_cheat(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    char const* text)
{
    /* The command without "::": a jstr at the OSRS revisions, newline-ended
     * bytes at the LostCity ones. */
    struct ProtoOutArgs a = { 0 };

    assert(text);
    a.command = text;
    a.copy = (uint8_t const*)text;
    a.copy_length = (int32_t)strlen(text);
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_CLIENT_CHEAT, &a);
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
    struct ProtoOutArgs a = { 0 };

    a.public_chat_filter = public_mode;
    a.private_chat_filter = private_mode;
    a.trade_chat_filter = trade_mode;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_CHAT_SETMODE, &a);
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
    /* Friend/ignore add/del: a name at the OSRS revisions, its name37 at the
     * LostCity ones. */
    struct ProtoOutArgs a = { 0 };
    char name[16];

    base37tostr((uint64_t)name37, name, (int)sizeof(name));
    a.name = name;
    a.name37 = name37;
    return proto_encode(rev, random_out, buf, cap, out_name, &a);
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
    struct ProtoOutArgs a = { 0 };

    a.name37 = name37;
    a.reason = reason;
    a.mute = moderator_mute ? 1 : 0;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_REPORT_ABUSE, &a);
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
    struct ProtoOutArgs a = { 0 };

    a.gender = gender;
    for( int i = 0; i < 7; i++ )
        a.idkit[i] = idkit[i] < 0 ? 255 : idkit[i];
    for( int i = 0; i < 5; i++ )
        a.colours[i] = colours[i];
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_IDK_SAVEDESIGN, &a);
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
    /* The raw tracking blob (an empty stub is valid). */
    struct ProtoOutArgs a = { 0 };

    a.copy = data;
    a.copy_length = len < 0 ? 0 : len;
    return proto_encode(rev, random_out, buf, cap, PKTOUT_NAME_EVENT_TRACKING, &a);
}
