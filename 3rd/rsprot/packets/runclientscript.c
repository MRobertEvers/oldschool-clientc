/* RUNCLIENTSCRIPT. Hand-written -- see runclientscript.h for why. */
#include "runclientscript.h"

#include <string.h>

/*
 * Transcribed from RSProt:
 *   protocol/osrs-239/.../game/outgoing/codec/misc/player/RunClientScriptEncoder.kt
 *
 *     buffer.pjstr(CharBuffer.wrap(types))
 *     for (i in (types.size - 1) downTo 0) {
 *         when (types[i]) {
 *             'W' -> { pVarInt2(v.size); for (e in v) pVarInt2s(e) }
 *             'X' -> { pVarInt2(v.size); for (e in v) pjstr(e) }
 *             's' -> pjstr(v)
 *             'Ï' -> p8(v)
 *             else -> p4(v)
 *         }
 *     }
 *     buffer.p4(message.id)
 */

void
packet_runclientscript_v1_out(RsprotExec *x, MsgRunClientScript *m)
{
    int32_t argc;

    RSPROT_JSTR(x, m->types);
    /*
     * The argument count is the LENGTH of the string just transferred -- there
     * is no count field. `types` is a borrow into the payload while decoding
     * and the caller's own pointer while encoding, so strlen answers in both
     * directions; a NULL only happens on a malformed read, which has already
     * latched an error.
     */
    if (!m->types) {
        rsprot_exec_fail(x, "RUNCLIENTSCRIPT has no type string");
        return;
    }
    argc = (int32_t)strlen(m->types);
    if (argc > MSG_RUNCLIENTSCRIPT_MAX_ARGS) {
        rsprot_exec_fail(x, "RUNCLIENTSCRIPT argument count exceeds capacity");
        return;
    }

    /* REVERSE. Reading these forward gives the right bytes to the wrong slots,
     * and only a uniformly-typed argument list hides it. */
    for (int32_t i = argc - 1; i >= 0; i--) {
        MsgRunClientScriptArg *a = &m->args[i];

        switch (m->types[i]) {
        case 'W': {
            int32_t n = rsprot_xcount(x, RSPROT_PRIM_VARINT2, &a->elem_count,
                                      MSG_RUNCLIENTSCRIPT_MAX_ELEMS, "elem_count");
            for (int32_t k = 0; k < n; k++)
                rsprot_x(x, RSPROT_PRIM_VARINT2S, &a->ints[k], "ints");
            break;
        }
        case 'X': {
            int32_t n = rsprot_xcount(x, RSPROT_PRIM_VARINT2, &a->elem_count,
                                      MSG_RUNCLIENTSCRIPT_MAX_ELEMS, "elem_count");
            for (int32_t k = 0; k < n; k++)
                rsprot_xstr(x, &a->strs[k], RSPROT_STR_PLAIN, "strs");
            break;
        }
        case 's':
            RSPROT_JSTR(x, a->sval);
            break;
        case (char)MSG_RUNCLIENTSCRIPT_TYPE_LONG:
            RSPROT_U8(x, a->lval);
            break;
        default:
            RSPROT_U4(x, a->ival);
            break;
        }
    }

    /* Last, after every argument. */
    RSPROT_U4(x, m->id);
}

const RsprotVersionRange rsprot_runclientscript_out[] = {
    { 221, 239, (RsprotCodecFn)packet_runclientscript_v1_out },
};

const int rsprot_runclientscript_out_count =
    (int)(sizeof(rsprot_runclientscript_out) / sizeof(rsprot_runclientscript_out[0]));
