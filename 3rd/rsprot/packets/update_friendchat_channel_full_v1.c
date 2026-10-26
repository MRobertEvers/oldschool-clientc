/* UPDATE_FRIENDCHAT_CHANNEL_FULL_V1. Hand-written; see update_friendchat_channel_full_v1.h. */
#include "update_friendchat_channel_full_v1.h"

/* v1 -- revs 222-226. */
void
packet_update_friendchat_channel_full_v1_v1_out(RsprotExec *x, MsgUpdateFriendchatChannelFullV1 *m)
{
    for (int32_t i = 0; RSPROT_MORE(x, m->joined, i, 1); i++) {
        RSPROT_JSTR(x, m->channel_owner);
        RSPROT_U8_12345678(x, m->channel_name);
        RSPROT_U1(x, m->kick_rank);
        /* A negative count reads no entries, as RSProx's 0..<count does. */
        RSPROT_I1(x, m->entry_count);
        if (m->entry_count > MSG_FRIENDCHAT_V1_MAX_ENTRIES) {
            rsprot_exec_fail(x, "UPDATE_FRIENDCHAT_CHANNEL_FULL_V1: too many entries");
            return;
        }
        for (int32_t k = 0; k < m->entry_count; k++) {
            RSPROT_JSTR(x, m->entries[k].name);
            RSPROT_U2_12(x, m->entries[k].world_id);
            RSPROT_I1(x, m->entries[k].rank);
            RSPROT_JSTR(x, m->entries[k].world_name);
        }
    }
}

const RsprotVersionRange rsprot_update_friendchat_channel_full_v1_out[] = {
    { RSPROT_OSRS_ID(222), RSPROT_OSRS_ID(226), (RsprotCodecFn)packet_update_friendchat_channel_full_v1_v1_out },
};

const int rsprot_update_friendchat_channel_full_v1_out_count =
    (int)(sizeof(rsprot_update_friendchat_channel_full_v1_out) / sizeof(rsprot_update_friendchat_channel_full_v1_out[0]));
