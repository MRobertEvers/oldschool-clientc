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
    { 223, 223, (RsprotCodecFn)packet_clickworldmap_v1_in },
    { 224, 224, (RsprotCodecFn)packet_clickworldmap_v2_in },
    { 225, 225, (RsprotCodecFn)packet_clickworldmap_v3_in },
    { 226, 226, (RsprotCodecFn)packet_clickworldmap_v2_in },
    { 227, 229, (RsprotCodecFn)packet_clickworldmap_v1_in },
    { 230, 230, (RsprotCodecFn)packet_clickworldmap_v2_in },
    { 231, 231, (RsprotCodecFn)packet_clickworldmap_v1_in },
    { 232, 232, (RsprotCodecFn)packet_clickworldmap_v4_in },
    { 233, 233, (RsprotCodecFn)packet_clickworldmap_v3_in },
    { 234, 234, (RsprotCodecFn)packet_clickworldmap_v4_in },
    { 235, 235, (RsprotCodecFn)packet_clickworldmap_v2_in },
    { 236, 237, (RsprotCodecFn)packet_clickworldmap_v3_in },
    { 238, 238, (RsprotCodecFn)packet_clickworldmap_v2_in },
    { 239, 240, (RsprotCodecFn)packet_clickworldmap_v4_in },
    { 241, 241, (RsprotCodecFn)packet_clickworldmap_v1_in },
};

const int rsprot_clickworldmap_in_count =
    (int)(sizeof(rsprot_clickworldmap_in) / sizeof(rsprot_clickworldmap_in[0]));
