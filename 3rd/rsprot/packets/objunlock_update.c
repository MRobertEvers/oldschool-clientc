/* OBJUNLOCK_UPDATE. Hand-written -- see objunlock_update.h. */
#include "objunlock_update.h"

/*
 * v100 -- revs 241.
 * while readable: gSmart1or2 delta, g8 bits.
 */
void
packet_objunlock_update_v100_out(RsprotExec *x, MsgObjunlockUpdate *m)
{
    for (int32_t i = 0; RSPROT_MORE(x, m->entries_count, i, MSG_OBJUNLOCK_UPDATE_MAX_ENTRIES); i++) {
        RSPROT_SMART(x, m->entries[i].delta);
        RSPROT_U8(x, m->entries[i].bits);
    }
}

const RsprotVersionRange rsprot_objunlock_update_out[] = {
    { 241, 241, (RsprotCodecFn)packet_objunlock_update_v100_out },
};

const int rsprot_objunlock_update_out_count =
    (int)(sizeof(rsprot_objunlock_update_out) / sizeof(rsprot_objunlock_update_out[0]));
