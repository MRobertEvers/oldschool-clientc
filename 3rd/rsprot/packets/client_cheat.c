/* CLIENT_CHEAT. Hand-written; see client_cheat.h. */
#include "client_cheat.h"

/* v1 -- revs 221-239. */
void
packet_client_cheat_v1_in(RsprotExec *x, MsgClientCheat *m)
{
    RSPROT_JSTR(x, m->command);
}

/* v2 -- revs 240-241. */
void
packet_client_cheat_v2_in(RsprotExec *x, MsgClientCheat *m)
{
    RSPROT_JSTR(x, m->command);
    for (int32_t i = 0; RSPROT_MORE(x, m->has_autocomplete, i, 1); i++)
        RSPROT_U1(x, m->autocomplete);
}

/* osrsm241 -- the Android 241.4 client (Old School mobile). */
void
packet_client_cheat_osrsm241_in(RsprotExec *x, MsgClientCheat *m)
{
    RSPROT_JSTR(x, m->command);
    for (int32_t i = 0; RSPROT_MORE(x, m->has_autocomplete, i, 1); i++)
        RSPROT_U1(x, m->autocomplete);
}

const RsprotVersionRange rsprot_client_cheat_in[] = {
    { RSPROT_OSRS_ID(221), RSPROT_OSRS_ID(239), (RsprotCodecFn)packet_client_cheat_v1_in },
    { RSPROT_OSRS_ID(240), RSPROT_OSRS_ID(241), (RsprotCodecFn)packet_client_cheat_v2_in },
    { RSPROT_OSRSM_ID(241), RSPROT_OSRSM_ID(241), (RsprotCodecFn)packet_client_cheat_osrsm241_in },
};

const int rsprot_client_cheat_in_count =
    (int)(sizeof(rsprot_client_cheat_in) / sizeof(rsprot_client_cheat_in[0]));
