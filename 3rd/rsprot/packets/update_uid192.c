/* UPDATE_UID192. Hand-written -- see update_uid192.h. */
#include "update_uid192.h"

/*
 * v100 -- revs 227-241.
 * gdata(24), g4 crc.
 */
void
packet_update_uid192_v100_out(RsprotExec *x, MsgUpdateUid192 *m)
{
    RSPROT_DATA(x, m->data, 24);
    RSPROT_U4(x, m->crc);
}

const RsprotVersionRange rsprot_update_uid192_out[] = {
    { 227, 241, (RsprotCodecFn)packet_update_uid192_v100_out },
};

const int rsprot_update_uid192_out_count =
    (int)(sizeof(rsprot_update_uid192_out) / sizeof(rsprot_update_uid192_out[0]));
