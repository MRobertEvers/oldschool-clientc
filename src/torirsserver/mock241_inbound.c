#include "torirsserver/mock241_inbound.h"

#include "torirsserver/mock239_inbound.h"
#include "torirsserver/mock239_interface_inbound.h"
#include "torirsserver/wire/osrs241_clientprot.h"
#include "net/rev/pktnames.h"

#include <assert.h>
#include <rsareabuf.h>
#include <string.h>

/*
 * Revision 241's client -> server bodies, rewritten into the shapes the world
 * handlers read -- a COPY of mock239_inbound.c's job, done differently.
 *
 * Every fixed-layout 241 client prot is read through the SAME generated table
 * the client writes it with (torirsserver/wire/osrs241_clientprot_table.h, from RSProx's
 * osrs-241 decoders), by role. Two transcriptions of one packet cannot
 * disagree when there is only one.
 *
 * What each body becomes is exactly what mock239_inbound_translate makes of
 * the 239 packet of the same name, and the packets whose 241 body is
 * byte-identical to 239's (the dialogs, the social names, CLIENT_CHEAT,
 * MESSAGE_PRIVATE, IF_BUTTONX, IF_SUBOP...) are handed to it unchanged.
 */

#define MOCK241_SELECTED_NONE 0xffff

static int
selected_is_spell(int selected_obj, int selected_sub)
{
    return selected_obj == MOCK241_SELECTED_NONE && selected_sub == MOCK241_SELECTED_NONE;
}

static void
put_tile_target(struct RSAreaBuf* w, int x, int z, int id)
{
    rsab_p2(w, (uint16_t)x);
    rsab_p2(w, (uint16_t)z);
    rsab_p2(w, (uint16_t)id);
}

static void
put_useon_tail(struct RSAreaBuf* w, int use_obj, int use_slot, int use_com)
{
    rsab_p2(w, (uint16_t)use_obj);
    rsab_p2(w, (uint16_t)use_slot);
    rsab_p2(w, (uint16_t)(use_com & 0xffff));
}

static void
put_spell_tail(struct RSAreaBuf* w, int spell_com)
{
    rsab_p4(w, (uint32_t)spell_com);
}

/** Read `layout` in full; the body must be exactly its size. */
static int
read_exact(
    struct Osrs241CpLayout const* layout,
    uint8_t const* in,
    int in_len,
    int32_t vals[OSRS241_CP_ROLE_COUNT])
{
    memset(vals, 0, sizeof(int32_t) * OSRS241_CP_ROLE_COUNT);
    return osrs241_cp_read(layout, in, in_len, vals) == in_len;
}

static struct Osrs241CpLayout const*
op_layout(int name)
{
    switch( name )
    {
    case PKTOUT_NAME_OPNPC1: return &g_osrs241_cp_opnpc1_v2;
    case PKTOUT_NAME_OPNPC2: return &g_osrs241_cp_opnpc2_v2;
    case PKTOUT_NAME_OPNPC3: return &g_osrs241_cp_opnpc3_v2;
    case PKTOUT_NAME_OPNPC4: return &g_osrs241_cp_opnpc4_v2;
    case PKTOUT_NAME_OPNPC5: return &g_osrs241_cp_opnpc5_v2;
    case PKTOUT_NAME_OPLOC1: return &g_osrs241_cp_oploc1_v2;
    case PKTOUT_NAME_OPLOC2: return &g_osrs241_cp_oploc2_v2;
    case PKTOUT_NAME_OPLOC3: return &g_osrs241_cp_oploc3_v2;
    case PKTOUT_NAME_OPLOC4: return &g_osrs241_cp_oploc4_v2;
    case PKTOUT_NAME_OPLOC5: return &g_osrs241_cp_oploc5_v2;
    case PKTOUT_NAME_OPOBJ1: return &g_osrs241_cp_opobj1_v2;
    case PKTOUT_NAME_OPOBJ2: return &g_osrs241_cp_opobj2_v2;
    case PKTOUT_NAME_OPOBJ3: return &g_osrs241_cp_opobj3_v2;
    case PKTOUT_NAME_OPOBJ4: return &g_osrs241_cp_opobj4_v2;
    case PKTOUT_NAME_OPOBJ5: return &g_osrs241_cp_opobj5_v2;
    case PKTOUT_NAME_OPPLAYER1: return &g_osrs241_cp_opplayer1;
    case PKTOUT_NAME_OPPLAYER2: return &g_osrs241_cp_opplayer2;
    case PKTOUT_NAME_OPPLAYER3: return &g_osrs241_cp_opplayer3;
    case PKTOUT_NAME_OPPLAYER4: return &g_osrs241_cp_opplayer4;
    case PKTOUT_NAME_OPPLAYER5: return &g_osrs241_cp_opplayer5;
    case PKTOUT_NAME_OPPLAYER6: return &g_osrs241_cp_opplayer6;
    case PKTOUT_NAME_OPPLAYER7: return &g_osrs241_cp_opplayer7;
    case PKTOUT_NAME_OPPLAYER8: return &g_osrs241_cp_opplayer8;
    default: return NULL;
    }
}

int
mock241_inbound_translate(
    int wire_opcode,
    int name,
    uint8_t const* in,
    int in_len,
    uint8_t* out,
    int out_cap,
    int* out_len)
{
    struct RSAreaBuf w;
    int32_t v[OSRS241_CP_ROLE_COUNT];
    struct Osrs241CpLayout const* layout;

    assert(out);
    assert(out_len);
    assert(in || in_len == 0);
    *out_len = 0;
    rsab_wrap(&w, out, (size_t)out_cap);

    /* Op packets with only an index or a tile target. Note the 241 table
     * names OPPLAYER6..8 too, which 239's translator does not take. */
    layout = op_layout(name);
    if( layout )
    {
        if( !read_exact(layout, in, in_len, v) )
            return PKTOUT_NAME_NONE;
        if( name >= PKTOUT_NAME_OPLOC1 && name <= PKTOUT_NAME_OPLOC5 )
            put_tile_target(&w, v[OSRS241_CP_X], v[OSRS241_CP_Z], v[OSRS241_CP_ID]);
        else if( name >= PKTOUT_NAME_OPOBJ1 && name <= PKTOUT_NAME_OPOBJ5 )
            put_tile_target(&w, v[OSRS241_CP_X], v[OSRS241_CP_Z], v[OSRS241_CP_ID]);
        else
            rsab_p2(&w, (uint16_t)v[OSRS241_CP_INDEX]);
        if( !rsab_ok(&w) )
            return PKTOUT_NAME_NONE;
        *out_len = (int)rsab_len(&w);
        return name;
    }

    switch( name )
    {
    /* The use-on and cast family: the T prot carries both, told apart by the
     * no-item sentinel in both selected fields, as at 239. */
    case PKTOUT_NAME_OPNPCT:
        if( !read_exact(&g_osrs241_cp_opnpct, in, in_len, v) )
            return PKTOUT_NAME_NONE;
        rsab_p2(&w, (uint16_t)v[OSRS241_CP_INDEX]);
        if( selected_is_spell(v[OSRS241_CP_SELECTED_OBJ], v[OSRS241_CP_SELECTED_SUB]) )
        {
            put_spell_tail(&w, v[OSRS241_CP_SELECTED_COMBINED_ID]);
            *out_len = (int)rsab_len(&w);
            return PKTOUT_NAME_OPNPCT;
        }
        put_useon_tail(&w, v[OSRS241_CP_SELECTED_OBJ], v[OSRS241_CP_SELECTED_SUB],
                       v[OSRS241_CP_SELECTED_COMBINED_ID]);
        *out_len = (int)rsab_len(&w);
        return PKTOUT_NAME_OPNPCU;

    case PKTOUT_NAME_OPLOCT:
    case PKTOUT_NAME_OPOBJT:
    {
        int const is_loc = name == PKTOUT_NAME_OPLOCT;

        if( !read_exact(is_loc ? &g_osrs241_cp_oploct : &g_osrs241_cp_opobjt, in, in_len, v) )
            return PKTOUT_NAME_NONE;
        put_tile_target(&w, v[OSRS241_CP_X], v[OSRS241_CP_Z], v[OSRS241_CP_ID]);
        if( selected_is_spell(v[OSRS241_CP_SELECTED_OBJ], v[OSRS241_CP_SELECTED_SUB]) )
        {
            put_spell_tail(&w, v[OSRS241_CP_SELECTED_COMBINED_ID]);
            *out_len = (int)rsab_len(&w);
            return name;
        }
        put_useon_tail(&w, v[OSRS241_CP_SELECTED_OBJ], v[OSRS241_CP_SELECTED_SUB],
                       v[OSRS241_CP_SELECTED_COMBINED_ID]);
        *out_len = (int)rsab_len(&w);
        return is_loc ? PKTOUT_NAME_OPLOCU : PKTOUT_NAME_OPOBJU;
    }

    case PKTOUT_NAME_OPPLAYERT:
        if( !read_exact(&g_osrs241_cp_opplayert, in, in_len, v) )
            return PKTOUT_NAME_NONE;
        rsab_p2(&w, (uint16_t)v[OSRS241_CP_INDEX]);
        if( selected_is_spell(v[OSRS241_CP_SELECTED_OBJ], v[OSRS241_CP_SELECTED_SUB]) )
        {
            put_spell_tail(&w, v[OSRS241_CP_SELECTED_COMBINED_ID]);
            *out_len = (int)rsab_len(&w);
            return PKTOUT_NAME_OPPLAYERT;
        }
        put_useon_tail(&w, v[OSRS241_CP_SELECTED_OBJ], v[OSRS241_CP_SELECTED_SUB],
                       v[OSRS241_CP_SELECTED_COMBINED_ID]);
        *out_len = (int)rsab_len(&w);
        return PKTOUT_NAME_OPPLAYERU;

    /* IF_BUTTONT: component use-on and spell-on-item, as at 239. */
    case PKTOUT_NAME_IF_BUTTONT:
        if( !read_exact(&g_osrs241_cp_if_buttont, in, in_len, v) )
            return PKTOUT_NAME_NONE;
        if( selected_is_spell(v[OSRS241_CP_TARGET_OBJ], v[OSRS241_CP_TARGET_SUB]) )
        {
            rsab_p2(&w, (uint16_t)v[OSRS241_CP_SELECTED_OBJ]);
            rsab_p2(&w, (uint16_t)v[OSRS241_CP_SELECTED_SUB]);
            rsab_p4(&w, (uint32_t)v[OSRS241_CP_SELECTED_COMBINED_ID]);
            put_spell_tail(&w, v[OSRS241_CP_TARGET_COMBINED_ID]);
            *out_len = (int)rsab_len(&w);
            return PKTOUT_NAME_OPHELDT;
        }
        rsab_p2(&w, (uint16_t)v[OSRS241_CP_TARGET_OBJ]);
        rsab_p2(&w, (uint16_t)v[OSRS241_CP_TARGET_SUB]);
        rsab_p4(&w, (uint32_t)v[OSRS241_CP_TARGET_COMBINED_ID]);
        rsab_p2(&w, (uint16_t)v[OSRS241_CP_SELECTED_OBJ]);
        rsab_p2(&w, (uint16_t)v[OSRS241_CP_SELECTED_SUB]);
        rsab_p4(&w, (uint32_t)v[OSRS241_CP_SELECTED_COMBINED_ID]);
        *out_len = (int)rsab_len(&w);
        return PKTOUT_NAME_OPHELDU;

    /* IF_BUTTOND: re-spelled into the 16-byte body handle_inv_buttond reads
     * (the same body a 239 client sends and 239's translator passes through):
     * p4_le src com, p2_le src obj, p2_le+128 src sub, p4 dst com,
     * p2_be+128 dst obj, p2_be+128 dst sub. */
    case PKTOUT_NAME_INV_BUTTOND:
        if( !read_exact(&g_osrs241_cp_if_buttond, in, in_len, v) )
            return PKTOUT_NAME_NONE;
        rsab_p4_alt1(&w, v[OSRS241_CP_SELECTED_COMBINED_ID]);
        rsab_p2_alt1(&w, v[OSRS241_CP_SELECTED_OBJ]);
        rsab_p2_alt3(&w, v[OSRS241_CP_SELECTED_SUB]);
        rsab_p4(&w, (uint32_t)v[OSRS241_CP_TARGET_COMBINED_ID]);
        rsab_p2_alt2(&w, v[OSRS241_CP_TARGET_OBJ]);
        rsab_p2_alt2(&w, v[OSRS241_CP_TARGET_SUB]);
        *out_len = (int)rsab_len(&w);
        return name;

    /* MOVE_GAMECLICK / MOVE_MINIMAPCLICK: the destination header, into
     * handle_move's `ctrl, x, z`. The minimap's anticheat tail is read for
     * validation and dropped, as at 239. */
    case PKTOUT_NAME_MOVE_GAMECLICK:
    case PKTOUT_NAME_MOVE_MINIMAPCLICK:
        if( !read_exact(name == PKTOUT_NAME_MOVE_GAMECLICK ? &g_osrs241_cp_move_gameclick
                                                         : &g_osrs241_cp_move_minimapclick,
                        in, in_len, v) )
            return PKTOUT_NAME_NONE;
        rsab_p1(&w, (uint8_t)v[OSRS241_CP_KEY_COMBINATION]);
        rsab_p2(&w, (uint16_t)v[OSRS241_CP_X]);
        rsab_p2(&w, (uint16_t)v[OSRS241_CP_Z]);
        *out_len = (int)rsab_len(&w);
        return PKTOUT_NAME_MOVE_GAMECLICK;

    case PKTOUT_NAME_RESUME_PAUSEBUTTON:
        if( !read_exact(&g_osrs241_cp_resume_pausebutton, in, in_len, v) )
            return PKTOUT_NAME_NONE;
        rsab_p4(&w, (uint32_t)v[OSRS241_CP_COMBINED_ID]);
        rsab_p2(&w, (uint16_t)v[OSRS241_CP_SUB]);
        *out_len = (int)rsab_len(&w);
        return name;

    case PKTOUT_NAME_CLICK_WORLD_MAP:
        if( !read_exact(&g_osrs241_cp_clickworldmap, in, in_len, v) )
            return PKTOUT_NAME_NONE;
        rsab_p4(&w, (uint32_t)v[OSRS241_CP_PACKED]);
        *out_len = (int)rsab_len(&w);
        return name;

    /* SetHeadingDecoder: g1Alt1 at 241, a plain byte at 239. */
    case PKTOUT_NAME_SET_HEADING:
        if( !read_exact(&g_osrs241_cp_set_heading, in, in_len, v) )
            return PKTOUT_NAME_NONE;
        rsab_p1(&w, (uint8_t)v[OSRS241_CP_HEADING]);
        *out_len = (int)rsab_len(&w);
        return name;

    /* IF_SCRIPT_TRIGGER: the 241 twelve-byte header re-spelled as 239's
     * (g2Alt3 sub, g2 obj, g4Alt1 component, g4Alt1 crc) so the interface
     * router's one decoder reads it; the typed arguments behind it did not
     * change and are copied. */
    case PKTOUT_NAME_IF_SCRIPT_TRIGGER:
    {
        struct Mock239IfScriptTrigger trigger;
        int head = osrs241_cp_bytes(&g_osrs241_cp_if_script_trigger);

        memset(v, 0, sizeof(v));
        if( in_len > out_cap || osrs241_cp_read(&g_osrs241_cp_if_script_trigger, in, in_len, v) != head )
            return PKTOUT_NAME_NONE;
        rsab_p2_alt3(&w, v[OSRS241_CP_SUB]);
        rsab_p2(&w, v[OSRS241_CP_OBJ]);
        rsab_p4_alt1(&w, v[OSRS241_CP_COMBINED_ID]);
        rsab_p4_alt1(&w, v[OSRS241_CP_CRC]);
        rsab_pdata(&w, in + head, (size_t)(in_len - head));
        if( !rsab_ok(&w) )
            return PKTOUT_NAME_NONE;
        *out_len = (int)rsab_len(&w);
        if( !mock239_if_script_trigger_decode(out, *out_len, NULL, &trigger) )
            return PKTOUT_NAME_NONE;
        return name;
    }

    /* IF_BUTTONX / IF_SUBOP: byte-identical to 239 (gCombinedId, g2 sub, g2
     * obj, g1 op[, g1 subop]); validated and passed through whole, as 239's
     * translator does under its 239 opcodes. */
    case PKTOUT_NAME_IF_BUTTONX:
    case PKTOUT_NAME_IF_SUBOP:
    {
        struct Mock239IfButton button;

        if( !mock239_if_button_decode(in, in_len, name == PKTOUT_NAME_IF_SUBOP, &button) ||
            in_len > out_cap )
            return PKTOUT_NAME_NONE;
        memcpy(out, in, (size_t)in_len);
        *out_len = in_len;
        return name;
    }

    /* BUG_REPORT is not routed at 239 either. */

    /*
     * Byte-identical to 239 (RSProx 239 and 241 decode bodies equal):
     * IF_BUTTON, the four dialog resumes, the social
     * names, CLIENT_CHEAT (a 241 client may append one autocomplete byte; ours
     * does not), MESSAGE_PRIVATE, MESSAGE_PUBLIC, WINDOW_STATUS, the empty
     * packets. 239's translator does exactly the right thing with them.
     */
    case PKTOUT_NAME_IF_BUTTON:
    case PKTOUT_NAME_RESUME_P_NAMEDIALOG:
    case PKTOUT_NAME_RESUME_P_STRINGDIALOG:
    case PKTOUT_NAME_RESUME_P_COUNTDIALOG_LONG:
    case PKTOUT_NAME_RESUME_P_OBJDIALOG:
    case PKTOUT_NAME_RESUME_P_COUNTDIALOG:
    case PKTOUT_NAME_FRIENDLIST_ADD:
    case PKTOUT_NAME_FRIENDLIST_DEL:
    case PKTOUT_NAME_IGNORELIST_ADD:
    case PKTOUT_NAME_IGNORELIST_DEL:
    case PKTOUT_NAME_CLIENT_CHEAT:
    case PKTOUT_NAME_MESSAGE_PRIVATE:
    case PKTOUT_NAME_MESSAGE_PUBLIC:
    case PKTOUT_NAME_CLOSE_MODAL:
    case PKTOUT_NAME_IDLE_TIMER:
    case PKTOUT_NAME_NO_TIMEOUT:
    case PKTOUT_NAME_EVENT_MOUSE_MOVE:
    case PKTOUT_NAME_MAP_BUILD_COMPLETE:
    case PKTOUT_NAME_EVENT_APPLET_FOCUS:
    case PKTOUT_NAME_WINDOW_STATUS:
    case PKTOUT_NAME_CHAT_SETMODE:
        /* 239's translator keys its fan-out cases on 239 OPCODES; hand it an
         * opcode no 239 case matches so only the name decides. */
        return mock239_inbound_translate(-1, name, in, in_len, out, out_cap, out_len);

    default:
        (void)wire_opcode;
        return PKTOUT_NAME_NONE;
    }
}
