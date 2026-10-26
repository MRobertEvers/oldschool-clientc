/* UPDATE_UID192. Hand-written -- see update_uid192.h. */
#include "update_uid192.h"

/*
 * v1 -- revs 222-241.
 * gdata(24), g4 crc.
 */
void
packet_update_uid192_v1_out(RsprotExec *x, MsgUpdateUid192 *m)
{
    RSPROT_DATA(x, m->data, 24);
    RSPROT_U4_1234(x, m->crc);
}

const RsprotVersionRange rsprot_update_uid192_out[] = {
    { RSPROT_OSRS_ID(222), RSPROT_OSRS_ID(241), (RsprotCodecFn)packet_update_uid192_v1_out },
};

const int rsprot_update_uid192_out_count =
    (int)(sizeof(rsprot_update_uid192_out) / sizeof(rsprot_update_uid192_out[0]));
