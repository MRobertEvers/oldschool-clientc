#ifndef RSPROT_PACKET_EVENT_KEYBOARD_H
#define RSPROT_PACKET_EVENT_KEYBOARD_H

/*
 * EVENT_KEYBOARD -- client -> server.
 *
 * Key presses: a key code and the time since the previous one, per key.
 *
 * Hand-written from RSProx's decoder (protocol/osrs-<N>/.../decoder/codec);
 * the C here is the source -- no generator writes this file.
 */

#include "../include/rsprot_exec.h"

#define MSG_EVENT_KEYBOARD_MAX 1024

typedef struct MsgEventKeyboardKey {
	int32_t key;
	int32_t delta;
} MsgEventKeyboardKey;

typedef struct MsgEventKeyboard {
	int32_t key_count;
	MsgEventKeyboardKey keys[MSG_EVENT_KEYBOARD_MAX];
} MsgEventKeyboard;

/* v1 -- revs 223. */
void packet_event_keyboard_v1_in(RsprotExec *x, MsgEventKeyboard *m);
/* v2 -- revs 224. */
void packet_event_keyboard_v2_in(RsprotExec *x, MsgEventKeyboard *m);
/* v3 -- revs 225, 231, 236. */
void packet_event_keyboard_v3_in(RsprotExec *x, MsgEventKeyboard *m);
/* v4 -- revs 226. */
void packet_event_keyboard_v4_in(RsprotExec *x, MsgEventKeyboard *m);
/* v5 -- revs 227, 240. */
void packet_event_keyboard_v5_in(RsprotExec *x, MsgEventKeyboard *m);
/* v6 -- revs 228. */
void packet_event_keyboard_v6_in(RsprotExec *x, MsgEventKeyboard *m);
/* v7 -- revs 229. */
void packet_event_keyboard_v7_in(RsprotExec *x, MsgEventKeyboard *m);
/* v8 -- revs 230. */
void packet_event_keyboard_v8_in(RsprotExec *x, MsgEventKeyboard *m);
/* v9 -- revs 232. */
void packet_event_keyboard_v9_in(RsprotExec *x, MsgEventKeyboard *m);
/* v10 -- revs 233. */
void packet_event_keyboard_v10_in(RsprotExec *x, MsgEventKeyboard *m);
/* v11 -- revs 234. */
void packet_event_keyboard_v11_in(RsprotExec *x, MsgEventKeyboard *m);
/* v12 -- revs 235. */
void packet_event_keyboard_v12_in(RsprotExec *x, MsgEventKeyboard *m);
/* v13 -- revs 237. */
void packet_event_keyboard_v13_in(RsprotExec *x, MsgEventKeyboard *m);
/* v14 -- revs 238. */
void packet_event_keyboard_v14_in(RsprotExec *x, MsgEventKeyboard *m);
/* v15 -- revs 239. */
void packet_event_keyboard_v15_in(RsprotExec *x, MsgEventKeyboard *m);
/* v16 -- revs 241. */
void packet_event_keyboard_v16_in(RsprotExec *x, MsgEventKeyboard *m);

#define packet_event_keyboard_osrs223_in packet_event_keyboard_v1_in
#define packet_event_keyboard_osrs224_in packet_event_keyboard_v2_in
#define packet_event_keyboard_osrs225_in packet_event_keyboard_v3_in
#define packet_event_keyboard_osrs226_in packet_event_keyboard_v4_in
#define packet_event_keyboard_osrs227_in packet_event_keyboard_v5_in
#define packet_event_keyboard_osrs228_in packet_event_keyboard_v6_in
#define packet_event_keyboard_osrs229_in packet_event_keyboard_v7_in
#define packet_event_keyboard_osrs230_in packet_event_keyboard_v8_in
#define packet_event_keyboard_osrs231_in packet_event_keyboard_v3_in
#define packet_event_keyboard_osrs232_in packet_event_keyboard_v9_in
#define packet_event_keyboard_osrs233_in packet_event_keyboard_v10_in
#define packet_event_keyboard_osrs234_in packet_event_keyboard_v11_in
#define packet_event_keyboard_osrs235_in packet_event_keyboard_v12_in
#define packet_event_keyboard_osrs236_in packet_event_keyboard_v3_in
#define packet_event_keyboard_osrs237_in packet_event_keyboard_v13_in
#define packet_event_keyboard_osrs238_in packet_event_keyboard_v14_in
#define packet_event_keyboard_osrs239_in packet_event_keyboard_v15_in
#define packet_event_keyboard_osrs240_in packet_event_keyboard_v5_in
#define packet_event_keyboard_osrs241_in packet_event_keyboard_v16_in

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_event_keyboard_in[];
extern const int rsprot_event_keyboard_in_count;

#endif
