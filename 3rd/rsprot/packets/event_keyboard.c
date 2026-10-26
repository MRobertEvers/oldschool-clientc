/* EVENT_KEYBOARD. Hand-written; see event_keyboard.h. */
#include "event_keyboard.h"

/* v1 -- revs 223. */
void
packet_event_keyboard_v1_in(RsprotExec *x, MsgEventKeyboard *m)
{
    /* Four bytes a key, to the end of the payload. */
    for (int32_t i = 0; RSPROT_MORE(x, m->key_count, i, MSG_EVENT_KEYBOARD_MAX); i++) {
        RSPROT_U1_SUB128(x, m->keys[i].key);
        RSPROT_U3_123(x, m->keys[i].delta);
    }
}

/* v2 -- revs 224. */
void
packet_event_keyboard_v2_in(RsprotExec *x, MsgEventKeyboard *m)
{
    /* Four bytes a key, to the end of the payload. */
    for (int32_t i = 0; RSPROT_MORE(x, m->key_count, i, MSG_EVENT_KEYBOARD_MAX); i++) {
        RSPROT_U3_123(x, m->keys[i].delta);
        RSPROT_U1_ADD128(x, m->keys[i].key);
    }
}

/* v3 -- revs 225, 231, 236. */
void
packet_event_keyboard_v3_in(RsprotExec *x, MsgEventKeyboard *m)
{
    /* Four bytes a key, to the end of the payload. */
    for (int32_t i = 0; RSPROT_MORE(x, m->key_count, i, MSG_EVENT_KEYBOARD_MAX); i++) {
        RSPROT_U1_ADD128(x, m->keys[i].key);
        RSPROT_U3_132(x, m->keys[i].delta);
    }
}

/* v4 -- revs 226. */
void
packet_event_keyboard_v4_in(RsprotExec *x, MsgEventKeyboard *m)
{
    /* Four bytes a key, to the end of the payload. */
    for (int32_t i = 0; RSPROT_MORE(x, m->key_count, i, MSG_EVENT_KEYBOARD_MAX); i++) {
        RSPROT_U3_132(x, m->keys[i].delta);
        RSPROT_U1_ADD128(x, m->keys[i].key);
    }
}

/* v5 -- revs 227, 240. */
void
packet_event_keyboard_v5_in(RsprotExec *x, MsgEventKeyboard *m)
{
    /* Four bytes a key, to the end of the payload. */
    for (int32_t i = 0; RSPROT_MORE(x, m->key_count, i, MSG_EVENT_KEYBOARD_MAX); i++) {
        RSPROT_U1(x, m->keys[i].key);
        RSPROT_U3_123(x, m->keys[i].delta);
    }
}

/* v6 -- revs 228. */
void
packet_event_keyboard_v6_in(RsprotExec *x, MsgEventKeyboard *m)
{
    /* Four bytes a key, to the end of the payload. */
    for (int32_t i = 0; RSPROT_MORE(x, m->key_count, i, MSG_EVENT_KEYBOARD_MAX); i++) {
        RSPROT_U1_NEG(x, m->keys[i].key);
        RSPROT_U3_132(x, m->keys[i].delta);
    }
}

/* v7 -- revs 229. */
void
packet_event_keyboard_v7_in(RsprotExec *x, MsgEventKeyboard *m)
{
    /* Four bytes a key, to the end of the payload. */
    for (int32_t i = 0; RSPROT_MORE(x, m->key_count, i, MSG_EVENT_KEYBOARD_MAX); i++) {
        RSPROT_U3_123(x, m->keys[i].delta);
        RSPROT_U1(x, m->keys[i].key);
    }
}

/* v8 -- revs 230. */
void
packet_event_keyboard_v8_in(RsprotExec *x, MsgEventKeyboard *m)
{
    /* Four bytes a key, to the end of the payload. */
    for (int32_t i = 0; RSPROT_MORE(x, m->key_count, i, MSG_EVENT_KEYBOARD_MAX); i++) {
        RSPROT_U3_132(x, m->keys[i].delta);
        RSPROT_U1(x, m->keys[i].key);
    }
}

/* v9 -- revs 232. */
void
packet_event_keyboard_v9_in(RsprotExec *x, MsgEventKeyboard *m)
{
    /* Four bytes a key, to the end of the payload. */
    for (int32_t i = 0; RSPROT_MORE(x, m->key_count, i, MSG_EVENT_KEYBOARD_MAX); i++) {
        RSPROT_U1(x, m->keys[i].key);
        RSPROT_U3_213(x, m->keys[i].delta);
    }
}

/* v10 -- revs 233. */
void
packet_event_keyboard_v10_in(RsprotExec *x, MsgEventKeyboard *m)
{
    /* Four bytes a key, to the end of the payload. */
    for (int32_t i = 0; RSPROT_MORE(x, m->key_count, i, MSG_EVENT_KEYBOARD_MAX); i++) {
        RSPROT_U3_321(x, m->keys[i].delta);
        RSPROT_U1_SUB128(x, m->keys[i].key);
    }
}

/* v11 -- revs 234. */
void
packet_event_keyboard_v11_in(RsprotExec *x, MsgEventKeyboard *m)
{
    /* Four bytes a key, to the end of the payload. */
    for (int32_t i = 0; RSPROT_MORE(x, m->key_count, i, MSG_EVENT_KEYBOARD_MAX); i++) {
        RSPROT_U1_NEG(x, m->keys[i].key);
        RSPROT_U3_123(x, m->keys[i].delta);
    }
}

/* v12 -- revs 235. */
void
packet_event_keyboard_v12_in(RsprotExec *x, MsgEventKeyboard *m)
{
    /* Four bytes a key, to the end of the payload. */
    for (int32_t i = 0; RSPROT_MORE(x, m->key_count, i, MSG_EVENT_KEYBOARD_MAX); i++) {
        RSPROT_U1_NEG(x, m->keys[i].key);
        RSPROT_U3_213(x, m->keys[i].delta);
    }
}

/* v13 -- revs 237. */
void
packet_event_keyboard_v13_in(RsprotExec *x, MsgEventKeyboard *m)
{
    /* Four bytes a key, to the end of the payload. */
    for (int32_t i = 0; RSPROT_MORE(x, m->key_count, i, MSG_EVENT_KEYBOARD_MAX); i++) {
        RSPROT_U3_321(x, m->keys[i].delta);
        RSPROT_U1_NEG(x, m->keys[i].key);
    }
}

/* v14 -- revs 238. */
void
packet_event_keyboard_v14_in(RsprotExec *x, MsgEventKeyboard *m)
{
    /* Four bytes a key, to the end of the payload. */
    for (int32_t i = 0; RSPROT_MORE(x, m->key_count, i, MSG_EVENT_KEYBOARD_MAX); i++) {
        RSPROT_U1(x, m->keys[i].key);
        RSPROT_U3_132(x, m->keys[i].delta);
    }
}

/* v15 -- revs 239. */
void
packet_event_keyboard_v15_in(RsprotExec *x, MsgEventKeyboard *m)
{
    /* Four bytes a key, to the end of the payload. */
    for (int32_t i = 0; RSPROT_MORE(x, m->key_count, i, MSG_EVENT_KEYBOARD_MAX); i++) {
        RSPROT_U1_NEG(x, m->keys[i].key);
        RSPROT_U3_321(x, m->keys[i].delta);
    }
}

/* v16 -- revs 241. */
void
packet_event_keyboard_v16_in(RsprotExec *x, MsgEventKeyboard *m)
{
    /* Four bytes a key, to the end of the payload. */
    for (int32_t i = 0; RSPROT_MORE(x, m->key_count, i, MSG_EVENT_KEYBOARD_MAX); i++) {
        RSPROT_U3_321(x, m->keys[i].delta);
        RSPROT_U1(x, m->keys[i].key);
    }
}

const RsprotVersionRange rsprot_event_keyboard_in[] = {
    { 223, 223, (RsprotCodecFn)packet_event_keyboard_v1_in },
    { 224, 224, (RsprotCodecFn)packet_event_keyboard_v2_in },
    { 225, 225, (RsprotCodecFn)packet_event_keyboard_v3_in },
    { 226, 226, (RsprotCodecFn)packet_event_keyboard_v4_in },
    { 227, 227, (RsprotCodecFn)packet_event_keyboard_v5_in },
    { 228, 228, (RsprotCodecFn)packet_event_keyboard_v6_in },
    { 229, 229, (RsprotCodecFn)packet_event_keyboard_v7_in },
    { 230, 230, (RsprotCodecFn)packet_event_keyboard_v8_in },
    { 231, 231, (RsprotCodecFn)packet_event_keyboard_v3_in },
    { 232, 232, (RsprotCodecFn)packet_event_keyboard_v9_in },
    { 233, 233, (RsprotCodecFn)packet_event_keyboard_v10_in },
    { 234, 234, (RsprotCodecFn)packet_event_keyboard_v11_in },
    { 235, 235, (RsprotCodecFn)packet_event_keyboard_v12_in },
    { 236, 236, (RsprotCodecFn)packet_event_keyboard_v3_in },
    { 237, 237, (RsprotCodecFn)packet_event_keyboard_v13_in },
    { 238, 238, (RsprotCodecFn)packet_event_keyboard_v14_in },
    { 239, 239, (RsprotCodecFn)packet_event_keyboard_v15_in },
    { 240, 240, (RsprotCodecFn)packet_event_keyboard_v5_in },
    { 241, 241, (RsprotCodecFn)packet_event_keyboard_v16_in },
};

const int rsprot_event_keyboard_in_count =
    (int)(sizeof(rsprot_event_keyboard_in) / sizeof(rsprot_event_keyboard_in[0]));
