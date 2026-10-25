/* HISCORE_REPLY. Hand-written -- see hiscore_reply.h. */
#include "hiscore_reply.h"

/*
 * v1 (rsprox_versions.txt 100) -- revs 227-241.
 * g1 requestId, g1 responseType; 0: g1 version, g1 statCount,
 * [g2 id, g4 rank, g4 result]*, g4 overallRank, g8 overallExperience,
 * g2 activityCount, [g2 id, g4 rank, g4 result]*; 1: gjstr reason.
 */
void
packet_hiscore_reply_v1_out(RsprotExec *x, MsgHiscoreReply *m)
{
    RSPROT_U1(x, m->request_id);
    RSPROT_U1(x, m->response_type);
    if (RSPROT_BRANCH(x, m->response_type) == 0) {
        RSPROT_U1(x, m->version);
        RSPROT_COUNT(x, m->stat_count, MSG_HISCORE_REPLY_MAX_STATS);
        for (int32_t i = 0; i < m->stat_count; i++) {
            RSPROT_U2(x, m->stats[i].id);
            RSPROT_U4(x, m->stats[i].rank);
            RSPROT_U4(x, m->stats[i].result);
        }
        RSPROT_U4(x, m->overall_rank);
        RSPROT_U8(x, m->overall_experience);
        RSPROT_COUNT2(x, m->activity_count, MSG_HISCORE_REPLY_MAX_ACTIVITIES);
        for (int32_t i = 0; i < m->activity_count; i++) {
            RSPROT_U2(x, m->activities[i].id);
            RSPROT_U4(x, m->activities[i].rank);
            RSPROT_U4(x, m->activities[i].result);
        }
    } else if (RSPROT_BRANCH(x, m->response_type) == 1) {
        RSPROT_JSTR(x, m->reason);
    } else {
        rsprot_exec_fail(x, "HISCORE_REPLY: unknown response type");
    }
}

const RsprotVersionRange rsprot_hiscore_reply_out[] = {
    { 227, 241, (RsprotCodecFn)packet_hiscore_reply_v1_out },
};

const int rsprot_hiscore_reply_out_count =
    (int)(sizeof(rsprot_hiscore_reply_out) / sizeof(rsprot_hiscore_reply_out[0]));
