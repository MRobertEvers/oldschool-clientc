/* VARCLAN. Hand-written; see varclan.h. */
#include "varclan.h"

/* v1 -- revs 223-241. */
void
packet_varclan_v1_out(RsprotExec *x, MsgVarclan *m)
{
    RSPROT_U2_12(x, m->id);
    RSPROT_REST(x, m->data, m->data_length);
}

const RsprotVersionRange rsprot_varclan_out[] = {
    { RSPROT_OSRS_ID(223), RSPROT_OSRS_ID(241), (RsprotCodecFn)packet_varclan_v1_out },
};

const int rsprot_varclan_out_count =
    (int)(sizeof(rsprot_varclan_out) / sizeof(rsprot_varclan_out[0]));
