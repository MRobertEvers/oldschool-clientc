/* CLANSETTINGS_FULL. Hand-written; see clansettings_full.h. */
#include "clansettings_full.h"

/* v1 -- revs 222-241. */
void
packet_clansettings_full_v1_out(RsprotExec *x, MsgClansettingsFull *m)
{
    RSPROT_U1(x, m->clan_type);
    for (int32_t i = 0; RSPROT_MORE(x, m->joined, i, 1); i++) {
        int32_t hashes, names;

        RSPROT_U1(x, m->version);
        RSPROT_U1(x, m->flags);
        hashes = (RSPROT_BRANCH(x, m->flags) & 0x1) != 0;
        names = (RSPROT_BRANCH(x, m->flags) & 0x2) != 0;
        if (hashes == names) {
            rsprot_exec_fail(x, "CLANSETTINGS_FULL: members need exactly one of hashes, names");
            return;
        }
        RSPROT_U4_1234(x, m->update_num);
        RSPROT_U4_1234(x, m->creation_time);
        RSPROT_COUNT2(x, m->affined_member_count, MSG_CLANSETTINGS_FULL_MAX_MEMBERS);
        RSPROT_COUNT(x, m->banned_member_count, MSG_CLANSETTINGS_FULL_MAX_BANNED);
        RSPROT_JSTR(x, m->clan_name);
        if (RSPROT_BRANCH(x, m->version) >= 4)
            RSPROT_U4_1234(x, m->skip4);
        RSPROT_U1(x, m->allow_unaffined);
        RSPROT_U1(x, m->talk_rank);
        RSPROT_U1(x, m->kick_rank);
        RSPROT_U1(x, m->lootshare_rank);
        RSPROT_U1(x, m->coinshare_rank);
        for (int32_t k = 0; k < m->affined_member_count; k++) {
            if (hashes)
                RSPROT_U8_12345678(x, m->members[k].hash);
            else
                RSPROT_JSTRNULL(x, m->members[k].display_name);
            RSPROT_U1(x, m->members[k].rank);
            if (RSPROT_BRANCH(x, m->version) >= 2)
                RSPROT_U4_1234(x, m->members[k].extra_info);
            if (RSPROT_BRANCH(x, m->version) >= 5)
                RSPROT_U2_12(x, m->members[k].join_rune_day);
            if (RSPROT_BRANCH(x, m->version) >= 6)
                RSPROT_U1(x, m->members[k].muted);
        }
        for (int32_t k = 0; k < m->banned_member_count; k++) {
            if (hashes)
                RSPROT_U8_12345678(x, m->banned[k].hash);
            else
                RSPROT_JSTRNULL(x, m->banned[k].display_name);
        }
        if (RSPROT_BRANCH(x, m->version) >= 3) {
            RSPROT_COUNT2(x, m->setting_count, MSG_CLANSETTINGS_FULL_MAX_SETTINGS);
            for (int32_t k = 0; k < m->setting_count; k++) {
                RSPROT_U4_1234(x, m->settings[k].header);
                switch ((uint32_t)RSPROT_BRANCH(x, m->settings[k].header) >> 30) {
                case 0:
                    RSPROT_U4_1234(x, m->settings[k].ival);
                    break;
                case 1:
                    RSPROT_U8_12345678(x, m->settings[k].lval);
                    break;
                case 2:
                    RSPROT_JSTR(x, m->settings[k].sval);
                    break;
                default:
                    rsprot_exec_fail(x, "CLANSETTINGS_FULL: setting type 3");
                    return;
                }
            }
        }
    }
}

const RsprotVersionRange rsprot_clansettings_full_out[] = {
    { RSPROT_OSRS_ID(222), RSPROT_OSRS_ID(241), (RsprotCodecFn)packet_clansettings_full_v1_out },
};

const int rsprot_clansettings_full_out_count =
    (int)(sizeof(rsprot_clansettings_full_out) / sizeof(rsprot_clansettings_full_out[0]));
