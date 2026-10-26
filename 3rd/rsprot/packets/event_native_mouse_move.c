/* EVENT_NATIVE_MOUSE_MOVE. Hand-written; see event_native_mouse_move.h. */
#include "event_native_mouse_move.h"

/* v1 -- revs 223-241. */
void
packet_event_native_mouse_move_v1_in(RsprotExec *x, MsgEventNativeMouseMove *m)
{
    RSPROT_U1(x, m->step_excess);
    RSPROT_U1(x, m->end_excess);
    for (int32_t i = 0; RSPROT_MORE(x, m->move_count, i, MSG_EVENT_NATIVE_MOUSE_MOVE_MAX); i++) {
        RSPROT_U1(x, m->moves[i].packed);
        if ((RSPROT_BRANCH(x, m->moves[i].packed) & 0xE0) == 0xE0) {
            RSPROT_U1(x, m->moves[i].time_lo);
            RSPROT_I2_12(x, m->moves[i].y);
            RSPROT_I2_12(x, m->moves[i].x);
        } else if ((m->moves[i].packed & 0xC0) == 0xC0) {
            RSPROT_I2_12(x, m->moves[i].y);
            RSPROT_I2_12(x, m->moves[i].x);
        } else if (m->moves[i].packed & 0x80) {
            RSPROT_U1(x, m->moves[i].dx);
            RSPROT_U1(x, m->moves[i].dy);
        } else {
            RSPROT_U1(x, m->moves[i].low);
        }
        RSPROT_U1(x, m->moves[i].last_mouse_button);
    }
}

const RsprotVersionRange rsprot_event_native_mouse_move_in[] = {
    { 223, 241, (RsprotCodecFn)packet_event_native_mouse_move_v1_in },
};

const int rsprot_event_native_mouse_move_in_count =
    (int)(sizeof(rsprot_event_native_mouse_move_in) / sizeof(rsprot_event_native_mouse_move_in[0]));
