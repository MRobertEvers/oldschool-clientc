/* CLICKWORLDMAP. Hand-written; see clickworldmap.h. */
#include "clickworldmap.h"

/* v1 -- revs 223, 227-229, 231, 241. */
void
packet_clickworldmap_v1_in(RsprotExec *x, MsgClickworldmap *m)
{
    RSPROT_U4_3412(x, m->packed);
}

/* v2 -- revs 224, 226, 230, 235, 238. */
void
packet_clickworldmap_v2_in(RsprotExec *x, MsgClickworldmap *m)
{
    RSPROT_U4_2143(x, m->packed);
}

/* v3 -- revs 225, 233, 236-237. */
void
packet_clickworldmap_v3_in(RsprotExec *x, MsgClickworldmap *m)
{
    RSPROT_U4_1234(x, m->packed);
}

/* v4 -- revs 232, 234, 239-240. */
void
packet_clickworldmap_v4_in(RsprotExec *x, MsgClickworldmap *m)
{
    RSPROT_U4_4321(x, m->packed);
}

const RsprotVersionRange rsprot_clickworldmap_in[] = {
    { RSPROT_OSRS_ID(223), RSPROT_OSRS_ID(223), (RsprotCodecFn)packet_clickworldmap_v1_in },
    { RSPROT_OSRS_ID(224), RSPROT_OSRS_ID(224), (RsprotCodecFn)packet_clickworldmap_v2_in },
    { RSPROT_OSRS_ID(225), RSPROT_OSRS_ID(225), (RsprotCodecFn)packet_clickworldmap_v3_in },
    { RSPROT_OSRS_ID(226), RSPROT_OSRS_ID(226), (RsprotCodecFn)packet_clickworldmap_v2_in },
    { RSPROT_OSRS_ID(227), RSPROT_OSRS_ID(229), (RsprotCodecFn)packet_clickworldmap_v1_in },
    { RSPROT_OSRS_ID(230), RSPROT_OSRS_ID(230), (RsprotCodecFn)packet_clickworldmap_v2_in },
    { RSPROT_OSRS_ID(231), RSPROT_OSRS_ID(231), (RsprotCodecFn)packet_clickworldmap_v1_in },
    { RSPROT_OSRS_ID(232), RSPROT_OSRS_ID(232), (RsprotCodecFn)packet_clickworldmap_v4_in },
    { RSPROT_OSRS_ID(233), RSPROT_OSRS_ID(233), (RsprotCodecFn)packet_clickworldmap_v3_in },
    { RSPROT_OSRS_ID(234), RSPROT_OSRS_ID(234), (RsprotCodecFn)packet_clickworldmap_v4_in },
    { RSPROT_OSRS_ID(235), RSPROT_OSRS_ID(235), (RsprotCodecFn)packet_clickworldmap_v2_in },
    { RSPROT_OSRS_ID(236), RSPROT_OSRS_ID(237), (RsprotCodecFn)packet_clickworldmap_v3_in },
    { RSPROT_OSRS_ID(238), RSPROT_OSRS_ID(238), (RsprotCodecFn)packet_clickworldmap_v2_in },
    { RSPROT_OSRS_ID(239), RSPROT_OSRS_ID(240), (RsprotCodecFn)packet_clickworldmap_v4_in },
    { RSPROT_OSRS_ID(241), RSPROT_OSRS_ID(241), (RsprotCodecFn)packet_clickworldmap_v1_in },
};

const int rsprot_clickworldmap_in_count =
    (int)(sizeof(rsprot_clickworldmap_in) / sizeof(rsprot_clickworldmap_in[0]));
