/* UPDATE_FRIENDCHAT_CHANNEL_FULL_V2. Hand-written; see update_friendchat_channel_full_v2.h. */
#include "update_friendchat_channel_full_v2.h"

/* v1 -- revs 223-241. */
void
packet_update_friendchat_channel_full_v2_v1_out(RsprotExec *x, MsgUpdateFriendchatChannelFullV2 *m)
{
    for (int32_t i = 0; RSPROT_MORE(x, m->joined, i, 1); i++) {
        RSPROT_JSTR(x, m->channel_owner);
        RSPROT_U8(x, m->channel_name);
        RSPROT_I1(x, m->kick_rank);
        RSPROT_COUNT_AS(x, SMARTNULL, m->entry_count, MSG_FRIENDCHAT_V2_MAX_ENTRIES);
        for (int32_t k = 0; k < m->entry_count; k++) {
            RSPROT_JSTR(x, m->entries[k].name);
            RSPROT_U2(x, m->entries[k].world_id);
            RSPROT_I1(x, m->entries[k].rank);
            RSPROT_JSTR(x, m->entries[k].world_name);
        }
    }
}

const RsprotVersionRange rsprot_update_friendchat_channel_full_v2_out[] = {
    { 223, 241, (RsprotCodecFn)packet_update_friendchat_channel_full_v2_v1_out },
};

const int rsprot_update_friendchat_channel_full_v2_out_count =
    (int)(sizeof(rsprot_update_friendchat_channel_full_v2_out) / sizeof(rsprot_update_friendchat_channel_full_v2_out[0]));
