#include "net/mock/mock239_inbound.h"

#include "net/mock/mock239_interface_inbound.h"
#include "net/rev/pktnames.h"

#include <rsareabuf.h>
#include <string.h>

/*
 * The wire opcodes that fan out. Everything else is identified by the canonical
 * name the 239 table already resolved, so only the ambiguous ones are numbers
 * here -- see src/net/rev/osrs239/packetout.h for the generated table.
 */
#define OSRS239_OP_IF_BUTTONX 47
#define OSRS239_OP_IF_SUBOP 40
#define OSRS239_OP_IF_BUTTONT 27
#define OSRS239_OP_IF_SCRIPT_TRIGGER 56

/* ------------------------------------------------------------------ */

/** Byte-for-byte, for the packets whose body genuinely did not move. */
static int
passthrough(
    uint8_t const* in,
    int in_len,
    uint8_t* out,
    int out_cap,
    int* out_len)
{
    if( in_len > out_cap )
        return 0;
    memcpy(out, in, (size_t)in_len);
    *out_len = in_len;
    return 1;
}

/**
 * `x, z, id` -- the body OPLOC<n>, OPOBJ<n> and their use-on forms all start
 * with at 230. Its own function because three translations end in it and the
 * argument order is the only thing that distinguishes them.
 */
static void
put_tile_target(
    struct RSAreaBuf* w,
    int x,
    int z,
    int id)
{
    rsab_p2(w, (uint16_t)x);
    rsab_p2(w, (uint16_t)z);
    rsab_p2(w, (uint16_t)id);
}

/**
 * The `useObj, useSlot, useCom` tail `useon_tail` reads with
 * `component_bytes == 2`.
 *
 * The component is truncated to its low half deliberately. 230's two-byte field
 * is a bare component id; 239 sends a four-byte combined id whose HIGH half is
 * the interface. `useon_tail` reads the field and immediately discards it --
 * the validation that matters is `player->inv[use_slot].obj_id == use_obj` --
 * so the low half is passed through to keep the byte count right rather than
 * because anything reads it.
 */
static void
put_useon_tail(
    struct RSAreaBuf* w,
    int use_obj,
    int use_slot,
    int use_com)
{
    rsab_p2(w, (uint16_t)use_obj);
    rsab_p2(w, (uint16_t)use_slot);
    rsab_p2(w, (uint16_t)(use_com & 0xffff));
}

/* ------------------------------------------------------------------ */

int
mock239_inbound_translate(
    int wire_opcode,
    int name,
    uint8_t const* in,
    int in_len,
    uint8_t* out,
    int out_cap,
    int* out_len)
{
    struct RSAreaBuf r;
    struct RSAreaBuf w;

    *out_len = 0;
    rsab_wrap(&r, (void*)in, (size_t)in_len);
    rsab_wrap(&w, out, (size_t)out_cap);

    /*
     * The fan-out cases first, because they are keyed on the opcode and the
     * name they were given (PKTOUT_NAME_NONE) says nothing.
     */
    switch( wire_opcode )
    {
    /*
     * IfButtonXDecoder: gCombinedId uid, g2 sub, g2 obj, g1 op.
     *
     * One opcode covering what 230 splits into three families. The
     * discriminator is `obj`: a component click carries the no-item sentinel,
     * an inventory-row click carries the obj the row is holding. That is the
     * same distinction 230's client makes when it decides between IF_BUTTON<n>
     * and OPHELD<n>, just made here instead of there.
     *
     * OPHELD and INV_BUTTON are not distinguished, because `handle_opheld` and
     * `handle_inv_button` are the same function -- see the `base` computation
     * in `handle_opheld_packet`, which exists precisely because rev 230's
     * gameframe routes inventory ops through both.
     */
    case OSRS239_OP_IF_BUTTONX:
    case OSRS239_OP_IF_SUBOP:
    {
        struct Mock239IfButton button;
        int has_subop = wire_opcode == OSRS239_OP_IF_SUBOP;

        if( !mock239_if_button_decode(in, in_len, has_subop, &button) )
            return PKTOUT_NAME_NONE;
        /* Keep the unified packet unified until the world handler. Besides
         * preserving IF_SUBOP's trailing subop, this retains the object id on
         * item-backed component ops 6..10; the old fan-out dropped those
         * packets because OPHELD stops at 5. */
        if( !passthrough(in, in_len, out, out_cap, out_len) )
            return PKTOUT_NAME_NONE;
        return has_subop ? PKTOUT_NAME_IF_SUBOP : PKTOUT_NAME_IF_BUTTONX;
    }

    /* Golden opcode 2929 / IF_TRIGGEROPLOCAL. The typed tail cannot be
     * interpreted without crc's signature, but the fixed fields can be
     * validated here and the entire body is preserved for the interface
     * router. The session already consumed this prot's outer p2 length. */
    case OSRS239_OP_IF_SCRIPT_TRIGGER:
    {
        struct Mock239IfScriptTrigger trigger;

        if( !mock239_if_script_trigger_decode(in, in_len, NULL, &trigger) )
            return PKTOUT_NAME_NONE;
        if( !passthrough(in, in_len, out, out_cap, out_len) )
            return PKTOUT_NAME_NONE;
        return PKTOUT_NAME_IF_SCRIPT_TRIGGER;
    }

    /*
     * IfButtonTDecoder: pCombinedId target, p2Alt2 selectedSub, p2Alt3
     * targetObj, p2Alt1 targetSub, p2Alt2 selectedObj, pCombinedIdAlt2
     * selected.
     *
     * 230's OPHELDU, with the two halves interleaved and in five byte orders.
     * The TARGET is what was clicked and the SELECTED is what was carried,
     * which is the order `handle_opheldu` reads: `obj, slot, com` first is the
     * target, `useObj, useSlot, useCom` second is the selection.
     *
     * OPHELDT (item on spell) arrives here too and is not separable on the
     * wire: the spell is simply a target component whose obj is the
     * not-an-item sentinel. It translates to OPHELDU with that sentinel intact
     * rather than being guessed at, so a use-on-spell reaches a handler that
     * can see what it is instead of being dropped here.
     */
    case OSRS239_OP_IF_BUTTONT:
    {
        int target_com = rsab_g4(&r);
        int selected_sub = rsab_g2_alt2(&r);
        int target_obj = rsab_g2_alt3(&r);
        int target_sub = rsab_g2_alt1(&r);
        int selected_obj = rsab_g2_alt2(&r);
        int selected_com = rsab_g4_alt2(&r);

        if( !rsab_ok(&r) )
            return PKTOUT_NAME_NONE;
        rsab_p2(&w, (uint16_t)target_obj);
        rsab_p2(&w, (uint16_t)target_sub);
        rsab_p4(&w, (uint32_t)target_com);
        rsab_p2(&w, (uint16_t)selected_obj);
        rsab_p2(&w, (uint16_t)selected_sub);
        rsab_p4(&w, (uint32_t)selected_com);
        *out_len = (int)rsab_len(&w);
        return PKTOUT_NAME_OPHELDU;
    }

    default:
        break;
    }

    switch( name )
    {
    /*
     * OpNpc<n>V2Decoder: g2Alt1 index, g1Alt2 subop, g1Alt3 ctrl.
     * handle_opnpc reads a bare p2 slot.
     *
     * The subop is the sub-option inside a multi-option entry (the modern
     * client's right-click submenus). Nothing in this tree has a submenu, so it
     * is read and dropped rather than skipped -- reading keeps the cursor
     * honest against the declared length.
     */
    case PKTOUT_NAME_OPNPC1:
    case PKTOUT_NAME_OPNPC2:
    case PKTOUT_NAME_OPNPC3:
    case PKTOUT_NAME_OPNPC4:
    case PKTOUT_NAME_OPNPC5:
    {
        int index = rsab_g2_alt1(&r);
        (void)rsab_g1_alt2(&r); /* subop */
        (void)rsab_g1_alt3(&r); /* ctrl */
        if( !rsab_ok(&r) )
            return PKTOUT_NAME_NONE;
        rsab_p2(&w, (uint16_t)index);
        *out_len = (int)rsab_len(&w);
        return name;
    }

    /* OpLoc<n>V2Decoder: g2Alt2 z, g2Alt1 x, g1Alt1 ctrl, g1Alt1 subop, g2 id.
     * Note z BEFORE x, which no other packet in this file does. */
    case PKTOUT_NAME_OPLOC1:
    case PKTOUT_NAME_OPLOC2:
    case PKTOUT_NAME_OPLOC3:
    case PKTOUT_NAME_OPLOC4:
    case PKTOUT_NAME_OPLOC5:
    {
        int z = rsab_g2_alt2(&r);
        int x = rsab_g2_alt1(&r);
        (void)rsab_g1_alt1(&r); /* ctrl */
        (void)rsab_g1_alt1(&r); /* subop */
        {
            int id = rsab_g2(&r);
            if( !rsab_ok(&r) )
                return PKTOUT_NAME_NONE;
            put_tile_target(&w, x, z, id);
        }
        *out_len = (int)rsab_len(&w);
        return name;
    }

    /* OpObj<n>V2Decoder: g2Alt2 id, g1Alt3 subop, g2Alt2 z, g1Alt3 ctrl, g2 x.
     * The id leads and the x trails -- the exact reverse of what 230 reads. */
    case PKTOUT_NAME_OPOBJ1:
    case PKTOUT_NAME_OPOBJ2:
    case PKTOUT_NAME_OPOBJ3:
    case PKTOUT_NAME_OPOBJ4:
    case PKTOUT_NAME_OPOBJ5:
    {
        int id = rsab_g2_alt2(&r);
        int z;
        int x;
        (void)rsab_g1_alt3(&r); /* subop */
        z = rsab_g2_alt2(&r);
        (void)rsab_g1_alt3(&r); /* ctrl */
        x = rsab_g2(&r);
        if( !rsab_ok(&r) )
            return PKTOUT_NAME_NONE;
        put_tile_target(&w, x, z, id);
        *out_len = (int)rsab_len(&w);
        return name;
    }

    /*
     * The use-on family, and the one place the 239 table's own names mislead.
     *
     * RSProt declares OPNPCU / OPLOCU / OPOBJU prots at 239 and ships NO
     * decoder for any of them: the modern client sends the T ("targeted") form
     * for "use item on X", and the U opcodes are vestigial. So the U names in
     * this switch would never fire, and the T names -- which 230 has no handler
     * for at all -- are what actually arrives. Translating T into U is the
     * whole of the mapping.
     *
     * OpNpcTDecoder: g1Alt2 ctrl, g2 selectedSub, g2Alt2 index,
     *                g2 selectedObj, gCombinedId selectedCombinedId.
     */
    case PKTOUT_NAME_OPNPCT:
    {
        int index;
        int sel_sub;
        int sel_obj;
        int sel_com;
        (void)rsab_g1_alt2(&r); /* ctrl */
        sel_sub = rsab_g2(&r);
        index = rsab_g2_alt2(&r);
        sel_obj = rsab_g2(&r);
        sel_com = rsab_g4(&r);
        if( !rsab_ok(&r) )
            return PKTOUT_NAME_NONE;
        rsab_p2(&w, (uint16_t)index);
        put_useon_tail(&w, sel_obj, sel_sub, sel_com);
        *out_len = (int)rsab_len(&w);
        return PKTOUT_NAME_OPNPCU;
    }

    /* OpLocTDecoder: g1Alt2 ctrl, g2Alt2 selectedSub, g2Alt1 id,
     *                gCombinedIdAlt1 selectedCombinedId, g2Alt3 z,
     *                g2Alt1 x, g2Alt1 selectedObj. */
    case PKTOUT_NAME_OPLOCT:
    {
        int sel_sub;
        int id;
        int sel_com;
        int z;
        int x;
        int sel_obj;
        (void)rsab_g1_alt2(&r); /* ctrl */
        sel_sub = rsab_g2_alt2(&r);
        id = rsab_g2_alt1(&r);
        sel_com = rsab_g4_alt1(&r);
        z = rsab_g2_alt3(&r);
        x = rsab_g2_alt1(&r);
        sel_obj = rsab_g2_alt1(&r);
        if( !rsab_ok(&r) )
            return PKTOUT_NAME_NONE;
        put_tile_target(&w, x, z, id);
        put_useon_tail(&w, sel_obj, sel_sub, sel_com);
        *out_len = (int)rsab_len(&w);
        return PKTOUT_NAME_OPLOCU;
    }

    /* OpObjTDecoder: g2Alt1 selectedObj, g2Alt2 id, g2 x,
     *                gCombinedIdAlt2 selectedCombinedId, g1 ctrl,
     *                g2Alt1 selectedSub, g2 z. */
    case PKTOUT_NAME_OPOBJT:
    {
        int sel_obj;
        int id;
        int x;
        int sel_com;
        int sel_sub;
        int z;
        sel_obj = rsab_g2_alt1(&r);
        id = rsab_g2_alt2(&r);
        x = rsab_g2(&r);
        sel_com = rsab_g4_alt2(&r);
        (void)rsab_g1(&r); /* ctrl */
        sel_sub = rsab_g2_alt1(&r);
        z = rsab_g2(&r);
        if( !rsab_ok(&r) )
            return PKTOUT_NAME_NONE;
        put_tile_target(&w, x, z, id);
        put_useon_tail(&w, sel_obj, sel_sub, sel_com);
        *out_len = (int)rsab_len(&w);
        return PKTOUT_NAME_OPOBJU;
    }

    /* MoveGameClickDecoder: g2Alt1 x, g2Alt1 z, g1Alt3 keyCombination.
     * handle_move reads ctrl FIRST, then x and z big-endian. */
    case PKTOUT_NAME_MOVE_GAMECLICK:
    {
        int x = rsab_g2_alt1(&r);
        int z = rsab_g2_alt1(&r);
        int ctrl = rsab_g1_alt3(&r);
        if( !rsab_ok(&r) )
            return PKTOUT_NAME_NONE;
        rsab_p1(&w, (uint8_t)ctrl);
        rsab_p2(&w, (uint16_t)x);
        rsab_p2(&w, (uint16_t)z);
        *out_len = (int)rsab_len(&w);
        return PKTOUT_NAME_MOVE_GAMECLICK;
    }

    /*
     * MoveMinimapClickDecoder: the same three fields, then eight more the
     * client sends as an anti-cheat checkpoint (minimap size, camera angle,
     * five constant bytes, a fine coordinate).
     *
     * Routed as MOVE_GAMECLICK rather than as itself. 230's minimap body is a
     * different thing entirely -- a start tile plus signed waypoint pairs, from
     * which `handle_move_minimapclick` reconstructs a destination -- and there
     * is nothing to reconstruct here: the destination is stated outright. The
     * two packets differ only in how the client says where to go, and the
     * server's answer to both is the same walk.
     */
    case PKTOUT_NAME_MOVE_MINIMAPCLICK:
    {
        int x = rsab_g2_alt1(&r);
        int z = rsab_g2_alt1(&r);
        int ctrl = rsab_g1_alt3(&r);
        if( !rsab_ok(&r) )
            return PKTOUT_NAME_NONE;
        rsab_p1(&w, (uint8_t)ctrl);
        rsab_p2(&w, (uint16_t)x);
        rsab_p2(&w, (uint16_t)z);
        *out_len = (int)rsab_len(&w);
        return PKTOUT_NAME_MOVE_GAMECLICK;
    }

    /* ResumePauseButtonDecoder: gCombinedIdAlt3 uid, g2 sub.
     *
     * The sub-id is semantic, not padding. Dynamic chatmenu rows all share
     * chatmenu:options as their uid; the trailing g2 is the only value that
     * distinguishes row 1 from row 2. Discarding it turns the golden client's
     * six-byte row answer into a bare parent resume, loses last_slot, and
     * reopens the same choice forever. Keep the canonical p4 + p2 pair; the
     * world handler also accepts the classic four-byte form. */
    case PKTOUT_NAME_RESUME_PAUSEBUTTON:
    {
        int uid = rsab_g4_alt3(&r);
        int sub = rsab_g2(&r);
        if( !rsab_ok(&r) )
            return PKTOUT_NAME_NONE;
        rsab_p4(&w, (uint32_t)uid);
        rsab_p2(&w, (uint16_t)sub);
        *out_len = (int)rsab_len(&w);
        return name;
    }

    /* ClickWorldMapDecoder: g4Alt1 packed coord. 230 reads a plain p4. */
    case PKTOUT_NAME_CLICK_WORLD_MAP:
    {
        int packed = rsab_g4_alt1(&r);
        if( !rsab_ok(&r) )
            return PKTOUT_NAME_NONE;
        rsab_p4(&w, (uint32_t)packed);
        *out_len = (int)rsab_len(&w);
        return name;
    }

    /* The two text dialogs are byte-identical gjstr bodies at 239. Validate
     * the terminator so a malformed var-byte packet cannot be routed as a
     * different callback after its framing has already succeeded. */
    case PKTOUT_NAME_RESUME_P_NAMEDIALOG:
    case PKTOUT_NAME_RESUME_P_STRINGDIALOG:
    {
        struct Mock239ByteString text;

        if( !mock239_resume_text_decode(in, in_len, &text) ||
            !passthrough(in, in_len, out, out_cap, out_len) )
            return PKTOUT_NAME_NONE;
        return name;
    }

    case PKTOUT_NAME_RESUME_P_COUNTDIALOG_LONG:
    {
        int64_t count;

        if( !mock239_resume_count_long_decode(in, in_len, &count) ||
            !passthrough(in, in_len, out, out_cap, out_len) )
            return PKTOUT_NAME_NONE;
        return name;
    }

    case PKTOUT_NAME_RESUME_P_OBJDIALOG:
    {
        int32_t object_id;

        if( !mock239_resume_object_decode(in, in_len, &object_id) ||
            !passthrough(in, in_len, out, out_cap, out_len) )
            return PKTOUT_NAME_NONE;
        return name;
    }

    /*
     * Verified identical at both revisions, one decoder at a time, and listed
     * rather than left to fall through so that "unchanged" is a claim someone
     * made and not an omission:
     *
     *   IF_BUTTON           gCombinedId  == p4
     *   IF_BUTTOND          gCombinedIdAlt1, g2Alt1, g2Alt3, gCombinedId,
     *                       g2Alt2, g2Alt2 -- field for field what
     *                       handle_inv_buttond reads
     *   RESUME_P_COUNTDIALOG g4
     *   CLOSE_MODAL         empty
     *   WINDOW_STATUS       g1, g2, g2
     *   CLIENT_CHEAT        gjstr
     *   FRIENDLIST_ADD/DEL  gjstr
     *   IGNORELIST_ADD/DEL  gjstr
     *   MESSAGE_PRIVATE     gjstr + huffman
     *   CHAT_SETMODE        three bytes
     */
    case PKTOUT_NAME_IF_BUTTON:
    case PKTOUT_NAME_INV_BUTTOND:
    case PKTOUT_NAME_RESUME_P_COUNTDIALOG:
    case PKTOUT_NAME_CLOSE_MODAL:
    case PKTOUT_NAME_IDLE_TIMER:
    case PKTOUT_NAME_NO_TIMEOUT:
    case PKTOUT_NAME_EVENT_MOUSE_MOVE:
    case PKTOUT_NAME_MAP_BUILD_COMPLETE:
    case PKTOUT_NAME_EVENT_APPLET_FOCUS:
    case PKTOUT_NAME_WINDOW_STATUS:
    case PKTOUT_NAME_CLIENT_CHEAT:
    case PKTOUT_NAME_FRIENDLIST_ADD:
    case PKTOUT_NAME_FRIENDLIST_DEL:
    case PKTOUT_NAME_IGNORELIST_ADD:
    case PKTOUT_NAME_IGNORELIST_DEL:
    case PKTOUT_NAME_MESSAGE_PRIVATE:
    case PKTOUT_NAME_CHAT_SETMODE:
        if( !passthrough(in, in_len, out, out_cap, out_len) )
            return PKTOUT_NAME_NONE;
        return name;

    default:
        /*
         * Dropped, and this is the safe answer rather than the lazy one.
         *
         * Passing an un-transcribed 239 body to a 230 handler is the failure
         * mode this whole file exists to remove: it frames, it validates, and
         * it acts on numbers that came out of the wrong bytes. A packet that
         * never arrives has a symptom; one that arrives wrong does not.
         */
        return PKTOUT_NAME_NONE;
    }
}
