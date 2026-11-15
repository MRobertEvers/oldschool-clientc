#include "torirsserver/wire/core/wire_in.h"

#include "net/jbase37.h"
#include "net/rev/pktnames.h"
#include "torirsserver/wire/core/wire_if_inbound.h"

#include <assert.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Running one rsprot codec over an inbound payload                    */
/* ------------------------------------------------------------------ */

RsprotExec*
wire_in_reader(struct WireInReader* r, uint8_t const* in, int in_len)
{
    assert(r);
    assert(in || in_len == 0);
    rsprot_buf_wrap_read(&r->buf, in, in_len);
    rsprot_exec_decode(&r->exec, &r->buf);
    r->len = in_len;
    return &r->exec;
}

int
wire_in_read_ok(struct WireInReader const* r)
{
    assert(r);
    return rsprot_exec_ok(&r->exec) && !r->buf.err;
}

int
wire_in_read_all(struct WireInReader const* r)
{
    return wire_in_read_ok(r) && r->buf.rpos == r->len;
}

int
wire_in_done(struct RsprotBuf const* w, int name, int* out_len)
{
    assert(w);
    assert(out_len);
    if( !rsprot_buf_ok(w) )
        return PKTOUT_NAME_NONE;
    *out_len = (int)w->wpos;
    return name;
}

/* ------------------------------------------------------------------ */
/* Canonical bodies                                                    */
/* ------------------------------------------------------------------ */

/*
 * The value a modern client puts in the "selected" obj and sub fields when
 * what is armed is a SPELL rather than an inventory item.
 *
 * There is no separate targeted-spell opcode: OPNPCT carries both "use this
 * item on that npc" and "cast this spell at that npc", and the only thing that
 * tells them apart is that a spell has no item behind it, so both of its item
 * fields are this sentinel (`net_out_opnpct` vs `net_out_opnpcu`, and
 * IF_BUTTONX's OUT_IF_BUTTONX_OBJ_NONE for the same reason).
 *
 * The distinction is not cosmetic: a use-on resolves against the *target*
 * (`[apnpcu,door]`), a cast resolves against the *spell*
 * (`[apnpct,magic_spellbook:wind_strike]`). Translating both to the use-on
 * form cost every spell its trigger, silently, because `useon_tail` then
 * rejects the packet for naming an empty inventory slot.
 */
#define WIRE_IN_SELECTED_NONE 0xffff

static int
selected_is_spell(int selected_obj, int selected_sub)
{
    return selected_obj == WIRE_IN_SELECTED_NONE && selected_sub == WIRE_IN_SELECTED_NONE;
}

/*
 * The `useObj, useSlot, useCom` tail `useon_tail` reads with
 * `component_bytes == 2`.
 *
 * The component is truncated to its low half deliberately. The classic
 * two-byte field is a bare component id; a modern client sends a four-byte
 * combined id whose HIGH half is the interface. `useon_tail` reads the field
 * and discards it -- the validation that matters is
 * `player->inv[use_slot].obj_id == use_obj` -- so the low half is passed
 * through to keep the byte count right rather than because anything reads it.
 */
static void
put_useon_tail(struct RsprotBuf* w, int use_obj, int use_slot, int use_com)
{
    rsprot_p2_12(w, (uint16_t)use_obj);
    rsprot_p2_12(w, (uint16_t)use_slot);
    rsprot_p2_12(w, (uint16_t)(use_com & 0xffff));
}

/*
 * The spell tail every targeted-cast body ends with: the spell's component
 * uid, whole. Four bytes, unlike the use-on tail's two, for the opposite
 * reason: here the component *is* the trigger subject, so `(218 << 16) | 11`
 * must not arrive as 11.
 */
static void
put_spell_tail(struct RsprotBuf* w, int spell_com)
{
    rsprot_p4_1234(w, (uint32_t)spell_com);
}

void
wire_in_index(struct RsprotBuf* w, int index)
{
    rsprot_p2_12(w, (uint16_t)index);
}

void
wire_in_tile(struct RsprotBuf* w, int x, int z, int id)
{
    rsprot_p2_12(w, (uint16_t)x);
    rsprot_p2_12(w, (uint16_t)z);
    rsprot_p2_12(w, (uint16_t)id);
}

/*
 * The use-on family is where a modern revision's own prot names mislead.
 * RSProt declares OPNPCU / OPLOCU / OPOBJU prots and ships NO decoder for any
 * of them: the client sends the T form for "use item on X" too, and the U
 * opcodes are vestigial. Translating T into U, unless a spell is armed, is the
 * whole of the mapping.
 */
int
wire_in_index_target(
    struct RsprotBuf* w,
    int name_t,
    int name_u,
    int index,
    int selected_obj,
    int selected_sub,
    int selected_com)
{
    wire_in_index(w, index);
    if( selected_is_spell(selected_obj, selected_sub) )
    {
        put_spell_tail(w, selected_com);
        return name_t;
    }
    put_useon_tail(w, selected_obj, selected_sub, selected_com);
    return name_u;
}

int
wire_in_tile_target(
    struct RsprotBuf* w,
    int name_t,
    int name_u,
    int x,
    int z,
    int id,
    int selected_obj,
    int selected_sub,
    int selected_com)
{
    wire_in_tile(w, x, z, id);
    if( selected_is_spell(selected_obj, selected_sub) )
    {
        put_spell_tail(w, selected_com);
        return name_t;
    }
    put_useon_tail(w, selected_obj, selected_sub, selected_com);
    return name_u;
}

/*
 * The classic OPHELDU, whose TARGET is what was clicked and whose SELECTED is
 * what was carried -- the order `handle_opheldu` reads: `obj, slot, com` first
 * is the target, `useObj, useSlot, useCom` second is the selection.
 *
 * OPHELDT (a spell on an inventory item -- High Alchemy, Superheat, the
 * enchants) arrives as the same packet, and the same sentinel separates them:
 * the spell is the TARGET, and being a spell it carries no obj and no slot. It
 * gets its own body -- item first, then the spell's whole component uid --
 * because `[opheldt,magic_spellbook:high_alchemy]` is keyed by the spell and
 * `handle_opheldu` would have read the spell's empty item fields as the
 * subject and dropped the packet.
 */
int
wire_in_if_buttont(
    struct RsprotBuf* w,
    int target_com,
    int target_obj,
    int target_sub,
    int selected_com,
    int selected_obj,
    int selected_sub)
{
    if( selected_is_spell(target_obj, target_sub) )
    {
        rsprot_p2_12(w, (uint16_t)selected_obj);
        rsprot_p2_12(w, (uint16_t)selected_sub);
        rsprot_p4_1234(w, (uint32_t)selected_com);
        put_spell_tail(w, target_com);
        return PKTOUT_NAME_OPHELDT;
    }
    rsprot_p2_12(w, (uint16_t)target_obj);
    rsprot_p2_12(w, (uint16_t)target_sub);
    rsprot_p4_1234(w, (uint32_t)target_com);
    rsprot_p2_12(w, (uint16_t)selected_obj);
    rsprot_p2_12(w, (uint16_t)selected_sub);
    rsprot_p4_1234(w, (uint32_t)selected_com);
    return PKTOUT_NAME_OPHELDU;
}

/*
 * p4_le src com, p2_le src obj, p2_le+128 src sub, p4 dst com, p2_be+128 dst
 * obj, p2_be+128 dst sub: revision 239's own IF_BUTTOND, which is what
 * handle_inv_buttond was written against.
 */
void
wire_in_inv_buttond(
    struct RsprotBuf* w,
    int selected_com,
    int selected_obj,
    int selected_sub,
    int target_com,
    int target_obj,
    int target_sub)
{
    rsprot_p4_4321(w, selected_com);
    rsprot_p2_21(w, selected_obj);
    rsprot_p2_21_add128(w, selected_sub);
    rsprot_p4_1234(w, (uint32_t)target_com);
    rsprot_p2_12_add128(w, target_obj);
    rsprot_p2_12_add128(w, target_sub);
}

/*
 * MOVE_MINIMAPCLICK arrives here too and is routed as MOVE_GAMECLICK. The
 * classic minimap body is a start tile plus signed waypoint pairs from which
 * `handle_move_minimapclick` reconstructs a destination; a modern client
 * states the destination outright (its anticheat tail is read and dropped),
 * so the server's answer to both is the same walk.
 */
void
wire_in_move(struct RsprotBuf* w, int key_combination, int x, int z)
{
    rsprot_p1(w, (uint8_t)key_combination);
    rsprot_p2_12(w, (uint16_t)x);
    rsprot_p2_12(w, (uint16_t)z);
}

/*
 * The sub-id is semantic, not padding. Dynamic chatmenu rows all share
 * chatmenu:options as their uid; the trailing p2 is the only value that
 * distinguishes row 1 from row 2. Discarding it turns a six-byte row answer
 * into a bare parent resume and reopens the same choice forever. The world
 * handler also accepts the classic four-byte form.
 */
void
wire_in_resume_pausebutton(struct RsprotBuf* w, int combined_id, int sub)
{
    rsprot_p4_1234(w, (uint32_t)combined_id);
    rsprot_p2_12(w, (uint16_t)sub);
}

void
wire_in_clickworldmap(struct RsprotBuf* w, int packed)
{
    rsprot_p4_1234(w, (uint32_t)packed);
}

void
wire_in_set_heading(struct RsprotBuf* w, int heading)
{
    rsprot_p1(w, (uint8_t)heading);
}

/*
 * The header is spelled as revision 239 spells it (g2Alt3 sub, g2 obj, g4Alt1
 * component, g4Alt1 crc), which is what wire_if_script_trigger_decode reads;
 * the typed arguments behind it have one layout at every revision and are
 * copied. The arguments cannot be interpreted without the crc's signature, so
 * the router does that; this only proves the fixed fields decode.
 */
int
wire_in_if_script_trigger(
    struct RsprotBuf* w,
    int sub,
    int obj,
    int combined_id,
    int crc,
    uint8_t const* args,
    int args_len)
{
    struct WireIfScriptTrigger trigger;
    size_t start = w->wpos;

    assert(args || args_len == 0);
    rsprot_p2_21_add128(w, sub);
    rsprot_p2_12(w, obj);
    rsprot_p4_4321(w, combined_id);
    rsprot_p4_4321(w, crc);
    if( args_len > 0 )
        rsprot_pdata(w, args, (size_t)args_len);
    if( !rsprot_buf_ok(w) )
        return 0;
    return wire_if_script_trigger_decode(
        w->data + start, (int)(w->wpos - start), NULL, &trigger);
}

/* ------------------------------------------------------------------ */
/* Bodies that are the same at 239..242                                */
/* ------------------------------------------------------------------ */

/** Byte-for-byte, for the packets whose body genuinely did not move. */
static int
passthrough(uint8_t const* in, int in_len, uint8_t* out, int out_cap, int* out_len)
{
    if( in_len > out_cap )
        return 0;
    if( in_len > 0 )
        memcpy(out, in, (size_t)in_len);
    *out_len = in_len;
    return 1;
}

static int
nul_prefix(uint8_t const* in, int in_len, int* text_len)
{
    for( int i = 0; i < in_len; i++ )
        if( in[i] == 0 )
        {
            *text_len = i;
            return 1;
        }
    return 0;
}

int
wire_in_body(
    int name,
    uint8_t const* in,
    int in_len,
    uint8_t* out,
    int out_cap,
    int* out_len)
{
    struct RsprotBuf w;

    assert(in || in_len == 0);
    assert(out);
    assert(out_len);
    *out_len = 0;
    rsprot_buf_wrap(&w, out, (size_t)out_cap);

    switch( name )
    {
    /*
     * IF_BUTTONX: gCombinedId uid, g2 sub, g2 obj, g1 op[, g1 subop].
     *
     * Kept unified until the world handler. Besides preserving IF_SUBOP's
     * trailing subop, this retains the object id on item-backed component
     * ops 6..10; fanning out into the classic OPHELD family dropped those,
     * because OPHELD stops at 5.
     */
    case PKTOUT_NAME_IF_BUTTONX:
    case PKTOUT_NAME_IF_SUBOP:
    {
        struct WireIfButton button;

        if( !wire_if_button_decode(in, in_len, name == PKTOUT_NAME_IF_SUBOP, &button) ||
            !passthrough(in, in_len, out, out_cap, out_len) )
            return PKTOUT_NAME_NONE;
        return name;
    }

    /* The two text dialogs are byte-identical gjstr bodies. Validate the
     * terminator so a malformed var-byte packet cannot be routed as a
     * different callback after its framing has already succeeded. */
    case PKTOUT_NAME_RESUME_P_NAMEDIALOG:
    case PKTOUT_NAME_RESUME_P_STRINGDIALOG:
    {
        struct WireByteString text;

        if( !wire_resume_text_decode(in, in_len, &text) ||
            !passthrough(in, in_len, out, out_cap, out_len) )
            return PKTOUT_NAME_NONE;
        return name;
    }

    case PKTOUT_NAME_RESUME_P_COUNTDIALOG_LONG:
    {
        int64_t count;

        if( !wire_resume_count_long_decode(in, in_len, &count) ||
            !passthrough(in, in_len, out, out_cap, out_len) )
            return PKTOUT_NAME_NONE;
        return name;
    }

    case PKTOUT_NAME_RESUME_P_OBJDIALOG:
    {
        int32_t object_id;

        if( !wire_resume_object_decode(in, in_len, &object_id) ||
            !passthrough(in, in_len, out, out_cap, out_len) )
            return PKTOUT_NAME_NONE;
        return name;
    }

    /* Modern social mutations carry a NUL-terminated display name; the
     * embedded classic world service consumes the equivalent base-37 p8. */
    case PKTOUT_NAME_FRIENDLIST_ADD:
    case PKTOUT_NAME_FRIENDLIST_DEL:
    case PKTOUT_NAME_IGNORELIST_ADD:
    case PKTOUT_NAME_IGNORELIST_DEL:
    {
        char text[32];
        int text_len;
        uint64_t name37;

        if( !nul_prefix(in, in_len, &text_len) || text_len + 1 != in_len ||
            text_len <= 0 || text_len >= (int)sizeof(text) )
            return PKTOUT_NAME_NONE;
        memcpy(text, in, (size_t)text_len);
        text[text_len] = '\0';
        name37 = strtobase37(text);
        if( name37 == 0 )
            return PKTOUT_NAME_NONE;
        rsprot_p8_12345678(&w, name37);
        return wire_in_done(&w, name, out_len);
    }

    /* CLIENT_CHEAT changed only its string terminator. Normalise it for the
     * embedded world's classic gjstr reader after validating the whole body.
     * (A 241 client may append one autocomplete byte; ours does not.) */
    case PKTOUT_NAME_CLIENT_CHEAT:
    {
        int text_len;

        if( !nul_prefix(in, in_len, &text_len) || text_len + 1 != in_len ||
            text_len + 1 > out_cap )
            return PKTOUT_NAME_NONE;
        memcpy(out, in, (size_t)text_len);
        out[text_len] = '\n';
        *out_len = text_len + 1;
        return name;
    }

    /* MESSAGE_PRIVATE changed from a p8 recipient to a NUL name. Its Huffman
     * tail is unchanged, so translate only the address and keep the bytes. */
    case PKTOUT_NAME_MESSAGE_PRIVATE:
    {
        char text[32];
        int text_len;
        uint64_t name37;
        int packed_len;

        if( !nul_prefix(in, in_len, &text_len) || text_len <= 0 ||
            text_len >= (int)sizeof(text) )
            return PKTOUT_NAME_NONE;
        memcpy(text, in, (size_t)text_len);
        text[text_len] = '\0';
        name37 = strtobase37(text);
        packed_len = in_len - text_len - 1;
        if( name37 == 0 || packed_len < 0 )
            return PKTOUT_NAME_NONE;
        rsprot_p8_12345678(&w, name37);
        if( packed_len > 0 )
            rsprot_pdata(&w, in + text_len + 1, (size_t)packed_len);
        return wire_in_done(&w, name, out_len);
    }

    /* MESSAGE_PUBLIC is RSProt's MessagePublic: g1 type, g1 colour, g1
     * effect, a pattern byte per colour past 12 (13-20), the Huffman text,
     * and a clan type after it when the type is 3. handle_message_public
     * reads colour, effect, text. A patterned colour is dropped: the
     * player-info chat block this server writes carries no pattern, so the
     * line could not be passed on as it was said. */
    case PKTOUT_NAME_MESSAGE_PUBLIC:
    {
        int text_end;

        if( in_len < 4 || in[1] >= 13 )
            return PKTOUT_NAME_NONE;
        text_end = in[0] == 3 ? in_len - 1 : in_len;
        if( text_end <= 3 )
            return PKTOUT_NAME_NONE;
        rsprot_p1(&w, in[1]);
        rsprot_p1(&w, in[2]);
        rsprot_pdata(&w, in + 3, (size_t)(text_end - 3));
        return wire_in_done(&w, name, out_len);
    }

    /*
     * Verified identical at every modern revision and listed rather than left
     * to fall through, so that "unchanged" is a claim someone made and not an
     * omission:
     *
     *   IF_BUTTON            gCombinedId == p4
     *   RESUME_P_COUNTDIALOG g4
     *   CLOSE_MODAL          empty
     *   WINDOW_STATUS        g1, g2, g2
     *   CHAT_SETMODE         three bytes
     */
    case PKTOUT_NAME_IF_BUTTON:
    case PKTOUT_NAME_RESUME_P_COUNTDIALOG:
    case PKTOUT_NAME_CLOSE_MODAL:
    case PKTOUT_NAME_IDLE_TIMER:
    case PKTOUT_NAME_NO_TIMEOUT:
    case PKTOUT_NAME_EVENT_MOUSE_MOVE:
    case PKTOUT_NAME_MAP_BUILD_COMPLETE:
    case PKTOUT_NAME_EVENT_APPLET_FOCUS:
    case PKTOUT_NAME_WINDOW_STATUS:
    case PKTOUT_NAME_CHAT_SETMODE:
        if( !passthrough(in, in_len, out, out_cap, out_len) )
            return PKTOUT_NAME_NONE;
        return name;

    default:
        return PKTOUT_NAME_NONE;
    }
}
