/* HINT_ARROW. Hand-written -- see hint_arrow.h. */
#include "hint_arrow.h"

/*
 * v1 (rsprox_versions.txt 100) -- revs 227-234.
 * g1 type, then per type (see the struct).
 */
void
packet_hint_arrow_v1_out(RsprotExec *x, MsgHintArrow *m)
{
    int32_t t;

    RSPROT_U1(x, m->type);
    t = RSPROT_BRANCH(x, m->type);
    if (t == 0) {
        RSPROT_DATA(x, m->skip0, 5);
    } else if (t == 1 || t == 10) {
        RSPROT_U2(x, m->index);
        RSPROT_DATA(x, m->skip0, 3);
    } else if (t >= 2 && t <= 6) {
        RSPROT_U2(x, m->x);
        RSPROT_U2(x, m->z);
        RSPROT_U1(x, m->height);
    }
    else
        rsprot_exec_fail(x, "HINT_ARROW: unknown type");
}

/*
 * v2 (rsprox_versions.txt 101) -- revs 235-241.
 * 227's arms, plus type 11: g2 world entity index, g3 height.
 */
void
packet_hint_arrow_v2_out(RsprotExec *x, MsgHintArrow *m)
{
    int32_t t;

    RSPROT_U1(x, m->type);
    t = RSPROT_BRANCH(x, m->type);
    if (t == 0) {
        RSPROT_DATA(x, m->skip0, 5);
    } else if (t == 1 || t == 10) {
        RSPROT_U2(x, m->index);
        RSPROT_DATA(x, m->skip0, 3);
    } else if (t >= 2 && t <= 6) {
        RSPROT_U2(x, m->x);
        RSPROT_U2(x, m->z);
        RSPROT_U1(x, m->height);
    } else if (t == 11) {
        RSPROT_U2(x, m->index);
        RSPROT_U3(x, m->height);
    } else {
        rsprot_exec_fail(x, "HINT_ARROW: unknown type");
    }
}

const RsprotVersionRange rsprot_hint_arrow_out[] = {
    { 227, 234, (RsprotCodecFn)packet_hint_arrow_v1_out },
    { 235, 241, (RsprotCodecFn)packet_hint_arrow_v2_out },
};

const int rsprot_hint_arrow_out_count =
    (int)(sizeof(rsprot_hint_arrow_out) / sizeof(rsprot_hint_arrow_out[0]));
