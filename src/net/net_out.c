#include "net_out.h"
#include <assert.h>

#include "jbase37.h"
#include "wordpack.h"
#include "rev/osrs241/osrs241_clientprot.h"

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


/* ------------------------------------------------------------------ */
/* Revision 241                                                         */
/* ------------------------------------------------------------------ */

/*
 * Revision 241's fixed-layout client prots are not spelled out here field by
 * field the way 239's are below. They are ONE generated table
 * (net/rev/osrs241/clientprot.h, from RSProx's osrs-241 decoders) that the
 * server's reader runs too (src/torirsserver/mock241_inbound.c), so a 241
 * packet is filled by ROLE -- "x", "z", "id", "controlKey" -- and the table
 * decides the order and the byte transforms.
 *
 * Every function in this file with a 239 branch has a 241 branch ahead of it
 * that comes here; the 239 code is untouched.
 */
static int
is_osrs241(struct GameProtoRevTable const* rev)
{
    return rev && rev->revision == GAMEPROTO_REVISION_OSRS241;
}

/** Write one 241 layout as the whole payload of `out_name`. */
static int
out241(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int out_name,
    struct Osrs241CpLayout const* layout,
    int32_t const vals[OSRS241_CP_ROLE_COUNT])
{
    struct RSCache_Buffer b;
    int n = osrs241_cp_bytes(layout);

    assert(layout->size == n);
    if( out_begin(rev, random_out, buf, cap, out_name, n, &b) < 0 )
        return -1;
    b.position += (uint32_t)osrs241_cp_write(layout, vals, b.data + b.position,
                                             (int)(b.size - b.position));
    return 1 + (int)b.position;
}

/** A var-byte prot whose body is one fixed 241 layout plus `trailer_len` bytes
 *  the caller writes after it (the movement packets). Returns the buffer
 *  position after the layout, with `*b` armed for the trailer. */
static int
out241_var_u8_begin(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int out_name,
    struct Osrs241CpLayout const* layout,
    int32_t const vals[OSRS241_CP_ROLE_COUNT],
    int trailer_len,
    struct RSCache_Buffer* b)
{
    int n = osrs241_cp_bytes(layout);

    assert(layout->size == -1);
    if( out_begin(rev, random_out, buf, cap, out_name, 1 + n + trailer_len, b) < 0 )
        return -1;
    p1(b, n + trailer_len);
    b->position += (uint32_t)osrs241_cp_write(layout, vals, b->data + b->position,
                                              (int)(b->size - b->position));
    return 0;
}

static struct Osrs241CpLayout const*
osrs241_oploc_layout(int op_num)
{
    static struct Osrs241CpLayout const* const k[5] = {
        &g_osrs241_cp_oploc1_v2, &g_osrs241_cp_oploc2_v2, &g_osrs241_cp_oploc3_v2,
        &g_osrs241_cp_oploc4_v2, &g_osrs241_cp_oploc5_v2,
    };
    assert(op_num >= 1);
    assert(op_num <= 5);
    return k[op_num - 1];
}

static struct Osrs241CpLayout const*
osrs241_opobj_layout(int op_num)
{
    static struct Osrs241CpLayout const* const k[5] = {
        &g_osrs241_cp_opobj1_v2, &g_osrs241_cp_opobj2_v2, &g_osrs241_cp_opobj3_v2,
        &g_osrs241_cp_opobj4_v2, &g_osrs241_cp_opobj5_v2,
    };
    assert(op_num >= 1);
    assert(op_num <= 5);
    return k[op_num - 1];
}

static struct Osrs241CpLayout const*
osrs241_opnpc_layout(int op_num)
{
    static struct Osrs241CpLayout const* const k[5] = {
        &g_osrs241_cp_opnpc1_v2, &g_osrs241_cp_opnpc2_v2, &g_osrs241_cp_opnpc3_v2,
        &g_osrs241_cp_opnpc4_v2, &g_osrs241_cp_opnpc5_v2,
    };
    assert(op_num >= 1);
    assert(op_num <= 5);
    return k[op_num - 1];
}

static struct Osrs241CpLayout const*
osrs241_opplayer_layout(int op_num)
{
    static struct Osrs241CpLayout const* const k[8] = {
        &g_osrs241_cp_opplayer1, &g_osrs241_cp_opplayer2, &g_osrs241_cp_opplayer3,
        &g_osrs241_cp_opplayer4, &g_osrs241_cp_opplayer5, &g_osrs241_cp_opplayer6,
        &g_osrs241_cp_opplayer7, &g_osrs241_cp_opplayer8,
    };
    assert(op_num >= 1);
    assert(op_num <= 8);
    return k[op_num - 1];
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
    if( rev && (rev->revision == GAMEPROTO_REVISION_OSRS239 || is_osrs241(rev)) )
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
    int payload = 12;
    int count = (int)strlen(signature);
    if( count > 16 ) return -1;
    /* Check capacity before spending an ISAAC word. */
    for( int i = 0; i < count; ++i )
    {
        if( signature[i] == 'i' )
        {
            uint32_t value = ((uint32_t)values[i] << 1) ^ (uint32_t)-(values[i] < 0);
            do { payload++; value >>= 7; } while( value );
        }
        else
        {
            assert(strings[i]);
            size_t length = strlen(strings[i]);
            if( length > 65535 ) return -1;
            payload += (int)length + 1;
        }
    }
    if( payload > 65535 || cap < payload + 3 ) return -1;
    struct RSCache_Buffer out;
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_IF_SCRIPT_TRIGGER,
                  payload + 2, &out) < 0 ) return -1;
    p2(&out, payload);
    if( is_osrs241(rev) )
    {
        /* IfScriptTriggerDecoder (241): gCombinedIdAlt2, g2Alt1 obj, g2Alt1
         * sub, g4Alt2 crc -- the same twelve bytes, reordered. */
        int32_t vals[OSRS241_CP_ROLE_COUNT] = { 0 };
        vals[OSRS241_CP_COMBINED_ID] = component_id;
        vals[OSRS241_CP_OBJ] = object_id;
        vals[OSRS241_CP_SUB] = child;
        vals[OSRS241_CP_CRC] = crc;
        out.position += (uint32_t)osrs241_cp_write(&g_osrs241_cp_if_script_trigger, vals,
                                                   out.data + out.position,
                                                   (int)(out.size - out.position));
    }
    else
    {
        p2_le_add128(&out, child);
        p2(&out, object_id);
        p4_le(&out, component_id);
        p4_le(&out, crc);
    }
    for( int i = 0; i < count; ++i )
    {
        if( signature[i] == 'i' )
        {
            uint32_t value = ((uint32_t)values[i] << 1) ^ (uint32_t)-(values[i] < 0);
            while( value >= 128 ) { p1(&out, (value & 127) | 128); value >>= 7; }
            p1(&out, value);
        }
        else
        {
            for( const unsigned char* p = (const unsigned char*)strings[i]; *p; ++p )
                p1(&out, *p);
            p1(&out, 0);
        }
    }
    return 1 + (int)out.position;
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

    if( is_osrs241(rev) )
    {
        int32_t vals[OSRS241_CP_ROLE_COUNT] = { 0 };
        vals[OSRS241_CP_PACKED] =
            ((level & 0x3) << 28) | ((abs_x & 0x3fff) << 14) | (abs_z & 0x3fff);
        return out241(rev, random_out, buf, cap, PKTOUT_NAME_CLICK_WORLD_MAP,
                      &g_osrs241_cp_clickworldmap, vals);
    }
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
    if( is_osrs241(rev) )
    {
        int32_t vals[OSRS241_CP_ROLE_COUNT] = { 0 };
        vals[OSRS241_CP_COMBINED_ID] = component_id;
        vals[OSRS241_CP_SUB] = sub_id < 0 ? 0xffff : sub_id;
        return out241(rev, random_out, buf, cap, PKTOUT_NAME_RESUME_PAUSEBUTTON,
                      &g_osrs241_cp_resume_pausebutton, vals);
    }
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

/*
 * A var-u8 packet whose body `write` fills. The length byte is back-patched
 * once the body is known, the way net_out_client_cheat does it.
 */
static int
out_var_u8_end(uint8_t* buf, struct RSCache_Buffer* b, int len_pos, int start)
{
    int const length = (int)b->position - start;
    if( length > 255 )
        return -1;
    buf[1 + len_pos] = (uint8_t)length;
    return 1 + (int)b->position;
}

static int
out_var_u8_begin(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int out_name,
    int min_payload,
    struct RSCache_Buffer* b,
    int* len_pos,
    int* start)
{
    if( out_begin(rev, random_out, buf, cap, out_name, 1 + min_payload, b) < 0 )
        return -1;
    p1(b, 0);
    *len_pos = (int)b->position - 1;
    *start = (int)b->position;
    return 0;
}

static int
out_jstr_only(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int out_name,
    char const* text)
{
    struct RSCache_Buffer b;
    int len_pos;
    int start;
    assert(text);
    if( out_var_u8_begin(rev, random_out, buf, cap, out_name, (int)strlen(text) + 1, &b, &len_pos, &start) < 0 )
        return -1;
    pjstr(&b, text, RSCACHE_JSTR_TERMINATOR_NULL);
    return out_var_u8_end(buf, &b, len_pos, start);
}

int
net_out_resume_namedialog(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap,
    char const* name)
{
    return out_jstr_only(rev, random_out, buf, cap, PKTOUT_NAME_RESUME_P_NAMEDIALOG, name);
}

int
net_out_resume_stringdialog(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap,
    char const* text)
{
    return out_jstr_only(rev, random_out, buf, cap, PKTOUT_NAME_RESUME_P_STRINGDIALOG, text);
}

int
net_out_resume_countdialog_long(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap,
    int64_t amount)
{
    struct RSCache_Buffer b;
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_RESUME_P_COUNTDIALOG_LONG, 8, &b) < 0 )
        return -1;
    out_p8(&b, amount);
    return 1 + (int)b.position;
}

int
net_out_resume_objdialog(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap,
    int obj_id)
{
    struct RSCache_Buffer b;
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_RESUME_P_OBJDIALOG, 2, &b) < 0 )
        return -1;
    p2(&b, obj_id);
    return 1 + (int)b.position;
}

/* RSProt BugReportDecoder, revisions 222-239: jstr description, g1Alt3 type,
 * jstr instructions, framed by a var-u16 length. */
int
net_out_bug_report(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap,
    char const* description, char const* instructions, int template_id)
{
    struct RSCache_Buffer b;
    assert(description);
    assert(instructions);
    int const body = (int)strlen(description) + 1 + 1 + (int)strlen(instructions) + 1;
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_BUG_REPORT, 2 + body, &b) < 0 )
        return -1;
    p2(&b, 0);
    int const len_pos = (int)b.position - 2;
    int const start = (int)b.position;
    if( is_osrs241(rev) )
    {
        /* BugReportDecoder (241): gjstr instructions, g1Alt2 type, gjstr
         * description. RSProx names the leading string `instructions` at 239
         * too, where RSProt names it the description; 241 follows RSProx. */
        pjstr(&b, instructions, RSCACHE_JSTR_TERMINATOR_NULL);
        out_p1_alt2(&b, template_id);
        pjstr(&b, description, RSCACHE_JSTR_TERMINATOR_NULL);
    }
    else
    {
        pjstr(&b, description, RSCACHE_JSTR_TERMINATOR_NULL);
        out_p1_alt3(&b, template_id);
        pjstr(&b, instructions, RSCACHE_JSTR_TERMINATOR_NULL);
    }
    int const length = (int)b.position - start;
    buf[1 + len_pos] = (uint8_t)(length >> 8);
    buf[1 + len_pos + 1] = (uint8_t)length;
    return 1 + (int)b.position;
}

/* RSProt SendSnapshotDecoder: jstr name, g1 rule, g1 mute (== 1). */
int
net_out_send_snapshot(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap,
    char const* name, int rule_id, int mute)
{
    struct RSCache_Buffer b;
    int len_pos;
    int start;
    assert(name);
    if( out_var_u8_begin(rev, random_out, buf, cap, PKTOUT_NAME_SEND_SNAPSHOT, (int)strlen(name) + 3, &b, &len_pos, &start) < 0 )
        return -1;
    pjstr(&b, name, RSCACHE_JSTR_TERMINATOR_NULL);
    p1(&b, rule_id);
    p1(&b, mute);
    return out_var_u8_end(buf, &b, len_pos, start);
}

/* RSProt FriendChatJoinLeaveDecoder: an empty body is a leave. */
int
net_out_friendchat_join_leave(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap,
    char const* name)
{
    struct RSCache_Buffer b;
    int len_pos;
    int start;
    if( out_var_u8_begin(rev, random_out, buf, cap, PKTOUT_NAME_FRIENDCHAT_JOIN_LEAVE, name ? (int)strlen(name) + 1 : 0, &b, &len_pos, &start) < 0 )
        return -1;
    if( name )
        pjstr(&b, name, RSCACHE_JSTR_TERMINATOR_NULL);
    return out_var_u8_end(buf, &b, len_pos, start);
}

int
net_out_friendchat_kick(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap,
    char const* name)
{
    return out_jstr_only(rev, random_out, buf, cap, PKTOUT_NAME_FRIENDCHAT_KICK, name);
}

/* RSProt FriendChatSetRankDecoder at rev 239 (codec v4): g1Alt3 rank, jstr name. */
int
net_out_friendchat_setrank(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap,
    char const* name, int rank)
{
    struct RSCache_Buffer b;
    int len_pos;
    int start;
    assert(name);
    if( out_var_u8_begin(rev, random_out, buf, cap, PKTOUT_NAME_FRIENDCHAT_SETRANK, (int)strlen(name) + 2, &b, &len_pos, &start) < 0 )
        return -1;
    /* FriendChatSetRankDecoder: g1Alt3 rank at 239, a plain g1 at 241. */
    if( is_osrs241(rev) )
        p1(&b, rank);
    else
        out_p1_alt3(&b, rank);
    pjstr(&b, name, RSCACHE_JSTR_TERMINATOR_NULL);
    return out_var_u8_end(buf, &b, len_pos, start);
}

int
net_out_clanchannel_full_request(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap,
    int clan_id)
{
    struct RSCache_Buffer b;
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_CLANCHANNEL_FULL_REQUEST, 1, &b) < 0 )
        return -1;
    p1(&b, clan_id);
    return 1 + (int)b.position;
}

int
net_out_clansettings_full_request(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap,
    int clan_id)
{
    struct RSCache_Buffer b;
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_CLANSETTINGS_FULL_REQUEST, 1, &b) < 0 )
        return -1;
    p1(&b, clan_id);
    return 1 + (int)b.position;
}

/* The two member-moderation requests share one body: g1 clan, g2 member, jstr name. */
static int
out_clan_member(
    struct GameProtoRevTable const* rev, struct Isaac* random_out, uint8_t* buf, int cap,
    int out_name, int clan_id, int member_index, int muted, char const* name)
{
    struct RSCache_Buffer b;
    int len_pos;
    int start;
    assert(name);
    if( out_var_u8_begin(rev, random_out, buf, cap, out_name, (int)strlen(name) + 5, &b, &len_pos, &start) < 0 )
        return -1;
    p1(&b, clan_id);
    p2(&b, member_index);
    if( muted >= 0 )
        p1(&b, muted);
    pjstr(&b, name, RSCACHE_JSTR_TERMINATOR_NULL);
    return out_var_u8_end(buf, &b, len_pos, start);
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

    /* MoveGameClickDecoder (241): g1Alt1 keyCombination, g2Alt2 z, g2Alt2 x,
     * behind the same var-byte length as 239. */
    if( is_osrs241(rev) && trailer_len == 0 && out_name == PKTOUT_NAME_MOVE_GAMECLICK )
    {
        int32_t vals[OSRS241_CP_ROLE_COUNT] = { 0 };
        vals[OSRS241_CP_KEY_COMBINATION] = ctrl_held ? 1 : 0;
        vals[OSRS241_CP_X] = base_x + route_x[0];
        vals[OSRS241_CP_Z] = base_z + route_z[0];
        if( out241_var_u8_begin(rev, random_out, buf, cap, out_name,
                                &g_osrs241_cp_move_gameclick, vals, 0, &b) < 0 )
            return -1;
        return 1 + (int)b.position;
    }
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
net_out_set_heading(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int heading)
{
    struct RSCache_Buffer b;

    assert(heading >= 0);
    assert(heading < 16);
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_SET_HEADING, 1, &b) < 0 )
        return -1;
    /* RSProt SetHeadingDecoder v4, used by revision 239: no byte transform.
     * RSProx's 241 SetHeadingDecoder reads g1Alt1. */
    if( is_osrs241(rev) )
        out_p1_alt1(&b, heading);
    else
        p1(&b, heading);
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
    if( is_osrs241(rev) )
    {
        /* MoveMinimapClickDecoder (241): the destination header is
         * MOVE_GAMECLICK's 241 one (g1Alt1 key, g2Alt2 z, g2Alt2 x); the
         * anticheat trailer that follows is unchanged from 239 and is the same
         * generated layout's tail, so the whole body is one row. */
        struct RSCache_Buffer b241;
        int32_t vals[OSRS241_CP_ROLE_COUNT] = { 0 };

        if( route_len < 1 )
            return -1;
        vals[OSRS241_CP_KEY_COMBINATION] = ctrl_held ? 1 : 0;
        vals[OSRS241_CP_X] = base_x + route_x[0];
        vals[OSRS241_CP_Z] = base_z + route_z[0];
        vals[OSRS241_CP_MINIMAP_WIDTH] = click_x;
        vals[OSRS241_CP_MINIMAP_HEIGHT] = click_y;
        vals[OSRS241_CP_CAMERA_ANGLE_Y] = camera_yaw;
        vals[OSRS241_CP_CHECKPOINT1] = 57;
        vals[OSRS241_CP_CHECKPOINT2] = minimap_angle;
        vals[OSRS241_CP_CHECKPOINT3] = minimap_zoom;
        vals[OSRS241_CP_CHECKPOINT4] = 89;
        vals[OSRS241_CP_FINE_X] = player_fine_x;
        vals[OSRS241_CP_FINE_Z] = player_fine_z;
        vals[OSRS241_CP_CHECKPOINT5] = nearest;
        if( out241_var_u8_begin(rev, random_out, buf, cap, PKTOUT_NAME_MOVE_MINIMAPCLICK,
                                &g_osrs241_cp_move_minimapclick, vals, 0, &b241) < 0 )
            return -1;
        return 1 + (int)b241.position;
    }
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
    struct RSCache_Buffer b;

    assert(op_num >= 1);
    assert(op_num <= 5);
    assert(subop >= 0);
    assert(subop <= 0xff);
    if( !rev || rev->packetout_code(PKTOUT_NAME_IF_SUBOP) < 0 )
        return -1;
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_IF_SUBOP, 10, &b) < 0 )
        return -1;
    p4(&b, component_id);
    p2(&b, slot & 0xffff);
    p2(&b, obj_id & 0xffff);
    p1(&b, OPHELD_IF_BUTTONX_OP[op_num - 1] & 0xff);
    p1(&b, subop);
    return 1 + (int)b.position;
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
    if( is_osrs241(rev) )
    {
        int32_t vals[OSRS241_CP_ROLE_COUNT] = { 0 };
        vals[OSRS241_CP_TARGET_COMBINED_ID] = target_com;
        vals[OSRS241_CP_TARGET_SUB] = target_sub & 0xffff;
        vals[OSRS241_CP_TARGET_OBJ] = target_obj & 0xffff;
        vals[OSRS241_CP_SELECTED_COMBINED_ID] = selected_com;
        vals[OSRS241_CP_SELECTED_SUB] = selected_sub & 0xffff;
        vals[OSRS241_CP_SELECTED_OBJ] = selected_obj & 0xffff;
        return out241(rev, random_out, buf, cap, PKTOUT_NAME_IF_BUTTONT,
                      &g_osrs241_cp_if_buttont, vals);
    }
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

    /* IfButtonDDecoder (241): the drag's two endpoints, each component +
     * sub + obj, in the 241 table's order. Selected = the dragged-from end. */
    if( is_osrs241(rev) )
    {
        int32_t vals[OSRS241_CP_ROLE_COUNT] = { 0 };
        vals[OSRS241_CP_SELECTED_COMBINED_ID] = src_com;
        vals[OSRS241_CP_SELECTED_OBJ] = src_obj & 0xffff;
        vals[OSRS241_CP_SELECTED_SUB] = src_slot & 0xffff;
        vals[OSRS241_CP_TARGET_COMBINED_ID] = dst_com;
        vals[OSRS241_CP_TARGET_OBJ] = dst_obj & 0xffff;
        vals[OSRS241_CP_TARGET_SUB] = dst_slot & 0xffff;
        (void)mode;
        return out241(rev, random_out, buf, cap, PKTOUT_NAME_INV_BUTTOND,
                      &g_osrs241_cp_if_buttond, vals);
    }
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
    if( is_osrs241(rev) )
    {
        /* OpLoc<n>V2 / OpObj<n>V2 (241): x, z, id, a sub-op byte and the
         * control-key byte, each op in its own order. Sub-op 0 and no control
         * key, as the 239 writers below send. */
        int32_t vals[OSRS241_CP_ROLE_COUNT] = { 0 };
        int is_loc = out_name >= PKTOUT_NAME_OPLOC1 && out_name <= PKTOUT_NAME_OPLOC5;

        vals[OSRS241_CP_X] = abs_x;
        vals[OSRS241_CP_Z] = abs_z;
        vals[OSRS241_CP_ID] = id;
        return out241(rev, random_out, buf, cap, out_name,
                      is_loc ? osrs241_oploc_layout(out_name - PKTOUT_NAME_OPLOC1 + 1)
                             : osrs241_opobj_layout(out_name - PKTOUT_NAME_OPOBJ1 + 1),
                      vals);
    }
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
    int width = (rev && rev->component_id_bytes > 0) ? rev->component_id_bytes : 2;
    if( is_osrs241(rev) )
    {
        /* OpLocTDecoder (241). A spell: the selected component is the spell,
         * with no sub and no obj (0xffff each), as at 239. */
        int32_t vals[OSRS241_CP_ROLE_COUNT] = { 0 };
        vals[OSRS241_CP_X] = abs_x;
        vals[OSRS241_CP_Z] = abs_z;
        vals[OSRS241_CP_ID] = loc_id;
        vals[OSRS241_CP_SELECTED_COMBINED_ID] = spell_component_id;
        vals[OSRS241_CP_SELECTED_SUB] = 0xffff;
        vals[OSRS241_CP_SELECTED_OBJ] = 0xffff;
        return out241(rev, random_out, buf, cap, PKTOUT_NAME_OPLOCT, &g_osrs241_cp_oploct, vals);
    }
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
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_OPLOCT, 6 + width, &b) < 0 )
        return -1;
    p2(&b, abs_x);
    p2(&b, abs_z);
    p2(&b, loc_id);
    /* The spell IS the identity of a targeted cast — `[apnpct,magic:<spell>]`
     * is keyed by this component and nothing else — so it goes on the wire at
     * the revision's full component width. Written as `p2` it truncated to the
     * child index, and every interface's child 11 arrived looking like Wind
     * Strike. Same rule and same reason as handle_resume_pausebutton's. */
    out_p_com(&b, width, spell_component_id);
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
    if( is_osrs241(rev) )
    {
        /* Use-on travels as OPLOCT at 241 as at 239: the selected triple is
         * the carried item. */
        int32_t vals[OSRS241_CP_ROLE_COUNT] = { 0 };
        vals[OSRS241_CP_X] = abs_x;
        vals[OSRS241_CP_Z] = abs_z;
        vals[OSRS241_CP_ID] = loc_id;
        vals[OSRS241_CP_SELECTED_COMBINED_ID] = use_component_id;
        vals[OSRS241_CP_SELECTED_SUB] = use_slot & 0xffff;
        vals[OSRS241_CP_SELECTED_OBJ] = use_obj_id & 0xffff;
        return out241(rev, random_out, buf, cap, PKTOUT_NAME_OPLOCT, &g_osrs241_cp_oploct, vals);
    }
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
    if( is_osrs241(rev) && out_name >= PKTOUT_NAME_OPNPC1 && out_name <= PKTOUT_NAME_OPNPC5 )
    {
        /* OpNpc<n>V2Decoder (241): index, sub-op, control key. */
        int32_t vals[OSRS241_CP_ROLE_COUNT] = { 0 };
        vals[OSRS241_CP_INDEX] = slot;
        return out241(rev, random_out, buf, cap, out_name,
                      osrs241_opnpc_layout(out_name - PKTOUT_NAME_OPNPC1 + 1), vals);
    }
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
    int width = (rev && rev->component_id_bytes > 0) ? rev->component_id_bytes : 2;
    if( is_osrs241(rev) )
    {
        int32_t vals[OSRS241_CP_ROLE_COUNT] = { 0 };
        vals[OSRS241_CP_INDEX] = npc_slot;
        vals[OSRS241_CP_SELECTED_COMBINED_ID] = spell_component_id;
        vals[OSRS241_CP_SELECTED_SUB] = 0xffff;
        vals[OSRS241_CP_SELECTED_OBJ] = 0xffff;
        return out241(rev, random_out, buf, cap, PKTOUT_NAME_OPNPCT, &g_osrs241_cp_opnpct, vals);
    }
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
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_OPNPCT, 2 + width, &b) < 0 )
        return -1;
    p2(&b, npc_slot);
    /* The spell IS the identity of a targeted cast — `[apnpct,magic:<spell>]`
     * is keyed by this component and nothing else — so it goes on the wire at
     * the revision's full component width. Written as `p2` it truncated to the
     * child index, and every interface's child 11 arrived looking like Wind
     * Strike. Same rule and same reason as handle_resume_pausebutton's. */
    out_p_com(&b, width, spell_component_id);
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
    if( is_osrs241(rev) )
    {
        int32_t vals[OSRS241_CP_ROLE_COUNT] = { 0 };
        vals[OSRS241_CP_INDEX] = npc_slot;
        vals[OSRS241_CP_SELECTED_COMBINED_ID] = use_component_id;
        vals[OSRS241_CP_SELECTED_SUB] = use_slot & 0xffff;
        vals[OSRS241_CP_SELECTED_OBJ] = use_obj_id & 0xffff;
        return out241(rev, random_out, buf, cap, PKTOUT_NAME_OPNPCT, &g_osrs241_cp_opnpct, vals);
    }
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
    int width = (rev && rev->component_id_bytes > 0) ? rev->component_id_bytes : 2;
    if( is_osrs241(rev) )
    {
        int32_t vals[OSRS241_CP_ROLE_COUNT] = { 0 };
        vals[OSRS241_CP_X] = abs_x;
        vals[OSRS241_CP_Z] = abs_z;
        vals[OSRS241_CP_ID] = obj_id;
        vals[OSRS241_CP_SELECTED_COMBINED_ID] = spell_component_id;
        vals[OSRS241_CP_SELECTED_SUB] = 0xffff;
        vals[OSRS241_CP_SELECTED_OBJ] = 0xffff;
        return out241(rev, random_out, buf, cap, PKTOUT_NAME_OPOBJT, &g_osrs241_cp_opobjt, vals);
    }
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
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_OPOBJT, 6 + width, &b) < 0 )
        return -1;
    p2(&b, abs_x);
    p2(&b, abs_z);
    p2(&b, obj_id);
    /* The spell IS the identity of a targeted cast — `[apnpct,magic:<spell>]`
     * is keyed by this component and nothing else — so it goes on the wire at
     * the revision's full component width. Written as `p2` it truncated to the
     * child index, and every interface's child 11 arrived looking like Wind
     * Strike. Same rule and same reason as handle_resume_pausebutton's. */
    out_p_com(&b, width, spell_component_id);
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
    if( is_osrs241(rev) )
    {
        int32_t vals[OSRS241_CP_ROLE_COUNT] = { 0 };
        vals[OSRS241_CP_X] = abs_x;
        vals[OSRS241_CP_Z] = abs_z;
        vals[OSRS241_CP_ID] = obj_id;
        vals[OSRS241_CP_SELECTED_COMBINED_ID] = use_component_id;
        vals[OSRS241_CP_SELECTED_SUB] = use_slot & 0xffff;
        vals[OSRS241_CP_SELECTED_OBJ] = use_obj_id & 0xffff;
        return out241(rev, random_out, buf, cap, PKTOUT_NAME_OPOBJT, &g_osrs241_cp_opobjt, vals);
    }
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

    if( op_num < 1 || op_num > 8 )
        return -1;
    if( is_osrs241(rev) )
    {
        /* OpPlayer1..8Decoder (241): index and control key, per-op order. */
        static int const k_names[8] = {
            PKTOUT_NAME_OPPLAYER1, PKTOUT_NAME_OPPLAYER2, PKTOUT_NAME_OPPLAYER3,
            PKTOUT_NAME_OPPLAYER4, PKTOUT_NAME_OPPLAYER5, PKTOUT_NAME_OPPLAYER6,
            PKTOUT_NAME_OPPLAYER7, PKTOUT_NAME_OPPLAYER8,
        };
        int32_t vals[OSRS241_CP_ROLE_COUNT] = { 0 };
        vals[OSRS241_CP_INDEX] = player_slot;
        return out241(rev, random_out, buf, cap, k_names[op_num - 1],
                      osrs241_opplayer_layout(op_num), vals);
    }
    if( op_num >= 6 )
    {
        /* RSProt OpPlayer6..8Decoder at rev 239. Only 239 names these rows; an
         * older revision has no row and builds nothing. */
        int const out_name = op_num == 6 ? PKTOUT_NAME_OPPLAYER6
                             : op_num == 7 ? PKTOUT_NAME_OPPLAYER7
                                           : PKTOUT_NAME_OPPLAYER8;
        if( out_begin(rev, random_out, buf, cap, out_name, 3, &b) < 0 )
            return -1;
        switch( op_num )
        {
        case 6: out_p2_alt3(&b, player_slot); out_p1_alt3(&b, 0); break;
        case 7: p1(&b, 0); p2(&b, player_slot); break;
        default: p2(&b, player_slot); out_p1_alt2(&b, 0); break;
        }
        return 1 + (int)b.position;
    }
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
    int width = (rev && rev->component_id_bytes > 0) ? rev->component_id_bytes : 2;
    if( is_osrs241(rev) )
    {
        int32_t vals[OSRS241_CP_ROLE_COUNT] = { 0 };
        vals[OSRS241_CP_INDEX] = player_slot;
        vals[OSRS241_CP_SELECTED_COMBINED_ID] = spell_component_id;
        vals[OSRS241_CP_SELECTED_SUB] = 0xffff;
        vals[OSRS241_CP_SELECTED_OBJ] = 0xffff;
        return out241(rev, random_out, buf, cap, PKTOUT_NAME_OPPLAYERT,
                      &g_osrs241_cp_opplayert, vals);
    }
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
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_OPPLAYERT, 2 + width, &b) < 0 )
        return -1;
    p2(&b, player_slot);
    /* The spell IS the identity of a targeted cast — `[apnpct,magic:<spell>]`
     * is keyed by this component and nothing else — so it goes on the wire at
     * the revision's full component width. Written as `p2` it truncated to the
     * child index, and every interface's child 11 arrived looking like Wind
     * Strike. Same rule and same reason as handle_resume_pausebutton's. */
    out_p_com(&b, width, spell_component_id);
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
    if( is_osrs241(rev) )
    {
        int32_t vals[OSRS241_CP_ROLE_COUNT] = { 0 };
        vals[OSRS241_CP_INDEX] = player_slot;
        vals[OSRS241_CP_SELECTED_COMBINED_ID] = use_component_id;
        vals[OSRS241_CP_SELECTED_SUB] = use_slot & 0xffff;
        vals[OSRS241_CP_SELECTED_OBJ] = use_obj_id & 0xffff;
        return out241(rev, random_out, buf, cap, PKTOUT_NAME_OPPLAYERT,
                      &g_osrs241_cp_opplayert, vals);
    }
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
    char const* text,
    int colour_effect)
{
    /* MESSAGE_PUBLIC: var-u8 length, body = colour effect wordpacked. */
    struct RSCache_Buffer b;
    int len_pos;
    int start;
    assert(text);
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_MESSAGE_PUBLIC, 7, &b) < 0 )
        return -1;
    p1(&b, 0); /* length placeholder */
    len_pos = (int)b.position - 1;
    start = (int)b.position;
    /* the body's one writer, shared with scriptrun's api_drive.say */
    wordpack_message_public_body(&b, text, colour_effect);
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
    assert(text);
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_MESSAGE_PRIVATE, 12, &b) < 0 )
        return -1;
    /* MessagePrivateDecoder is unchanged at 241. */
    if( rev->revision == GAMEPROTO_REVISION_OSRS239 || is_osrs241(rev) )
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
    assert(text);
    if( out_begin(rev, random_out, buf, cap, PKTOUT_NAME_CLIENT_CHEAT, (int)strlen(text) + 3, &b) < 0 )
        return -1;
    p1(&b, 0); /* length placeholder */
    len_pos = (int)b.position - 1;
    start = (int)b.position;
    pjstr(
        &b,
        text,
        (rev->revision == GAMEPROTO_REVISION_OSRS239 || is_osrs241(rev))
            ? RSCACHE_JSTR_TERMINATOR_NULL
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
    /* Friend/ignore add/del are a lone gjstr at 239 and 241 alike. */
    if( rev->revision == GAMEPROTO_REVISION_OSRS239 || is_osrs241(rev) )
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
