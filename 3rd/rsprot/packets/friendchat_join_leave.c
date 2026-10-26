/* FRIENDCHAT_JOIN_LEAVE. Hand-written; see friendchat_join_leave.h. */
#include "friendchat_join_leave.h"

/* v1 -- revs 223-241. */
void
packet_friendchat_join_leave_v1_in(RsprotExec *x, MsgFriendchatJoinLeave *m)
{
    for (int32_t i = 0; RSPROT_MORE(x, m->has_name, i, 1); i++)
        RSPROT_JSTR(x, m->name);
}

const RsprotVersionRange rsprot_friendchat_join_leave_in[] = {
    { 223, 241, (RsprotCodecFn)packet_friendchat_join_leave_v1_in },
};

const int rsprot_friendchat_join_leave_in_count =
    (int)(sizeof(rsprot_friendchat_join_leave_in) / sizeof(rsprot_friendchat_join_leave_in[0]));
