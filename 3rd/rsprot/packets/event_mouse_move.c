/* EVENT_MOUSE_MOVE. Hand-written; see event_mouse_move.h. */
#include "event_mouse_move.h"

/* v1 -- revs 222-241. */
void
packet_event_mouse_move_v1_in(RsprotExec *x, MsgEventMouseMove *m)
{
    RSPROT_U1(x, m->step_excess);
    RSPROT_U1(x, m->end_excess);
    for (int32_t i = 0; RSPROT_MORE(x, m->move_count, i, MSG_EVENT_MOUSE_MOVE_MAX); i++) {
        RSPROT_U1(x, m->moves[i].packed);
        if ((RSPROT_BRANCH(x, m->moves[i].packed) & 0xE0) == 0xE0) {
            RSPROT_U1(x, m->moves[i].time_lo);
            RSPROT_U4_1234(x, m->moves[i].position);
        } else if ((m->moves[i].packed & 0xC0) == 0xC0) {
            RSPROT_U4_1234(x, m->moves[i].position);
        } else if (m->moves[i].packed & 0x80) {
            RSPROT_U1(x, m->moves[i].dx);
            RSPROT_U1(x, m->moves[i].dy);
        } else {
            RSPROT_U1(x, m->moves[i].low);
        }
    }
}

const RsprotVersionRange rsprot_event_mouse_move_in[] = {
    { RSPROT_OSRS_ID(222), RSPROT_OSRS_ID(241), (RsprotCodecFn)packet_event_mouse_move_v1_in },
};

const int rsprot_event_mouse_move_in_count =
    (int)(sizeof(rsprot_event_mouse_move_in) / sizeof(rsprot_event_mouse_move_in[0]));
