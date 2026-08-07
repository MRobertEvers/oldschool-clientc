/* UPDATE_IGNORELIST. Hand-written -- see update_ignorelist.h for why. */
#include "update_ignorelist.h"

/*
 * Transcribed from RSProt:
 *   protocol/osrs-239/.../game/outgoing/codec/social/UpdateIgnoreListEncoder.kt
 *
 *     for (ignore in message.ignores) {
 *         when (ignore) {
 *             is AddedIgnoredEntry -> {
 *                 buffer.p1(if (ignore.added) 0x1 else 0)
 *                 buffer.pjstr(ignore.name)
 *                 buffer.pjstr(ignore.previousName ?: "")
 *                 buffer.pjstr(ignore.note)
 *             }
 *             is RemovedIgnoredEntry -> {
 *                 buffer.p1(0x4)
 *                 buffer.pjstr(ignore.name)
 *             }
 *         }
 *     }
 *
 * The two arms share their first field, so hoisting it turns a type dispatch
 * into a value dispatch and the bytes are unchanged.
 */

void
packet_update_ignorelist_v1_out(RsprotExec *x, MsgUpdateIgnoreList *m)
{
    for (int32_t i = 0;
         RSPROT_MORE(x, m->count, i, MSG_UPDATE_IGNORELIST_MAX_ENTRIES);
         i++) {
        MsgUpdateIgnoreListEntry *e = &m->entries[i];

        RSPROT_U1(x, e->flags);
        RSPROT_JSTR(x, e->name);
        /* A removal ends here; an add carries two more strings. */
        if (!(RSPROT_BRANCH(x, e->flags) & MSG_UPDATE_IGNORELIST_REMOVED)) {
            RSPROT_JSTR(x, e->previous_name);
            RSPROT_JSTR(x, e->note);
        }
    }
}

const RsprotVersionRange rsprot_update_ignorelist_out[] = {
    { 221, 239, (RsprotCodecFn)packet_update_ignorelist_v1_out },
};

const int rsprot_update_ignorelist_out_count =
    (int)(sizeof(rsprot_update_ignorelist_out) / sizeof(rsprot_update_ignorelist_out[0]));
