/* UPDATE_FRIENDCHAT_CHANNEL_SINGLEUSER. Hand-written; see update_friendchat_channel_singleuser.h. */
#include "update_friendchat_channel_singleuser.h"

/* v1 -- revs 222-241. */
void
packet_update_friendchat_channel_singleuser_v1_out(RsprotExec *x, MsgUpdateFriendchatChannelSingleuser *m)
{
    RSPROT_JSTR(x, m->name);
    RSPROT_U2_12(x, m->world_id);
    RSPROT_I1(x, m->rank);
    if (RSPROT_BRANCH(x, m->rank) != -128)
        RSPROT_JSTR(x, m->world_name);
}

const RsprotVersionRange rsprot_update_friendchat_channel_singleuser_out[] = {
    { RSPROT_OSRS_ID(222), RSPROT_OSRS_ID(241), (RsprotCodecFn)packet_update_friendchat_channel_singleuser_v1_out },
};

const int rsprot_update_friendchat_channel_singleuser_out_count =
    (int)(sizeof(rsprot_update_friendchat_channel_singleuser_out) / sizeof(rsprot_update_friendchat_channel_singleuser_out[0]));
