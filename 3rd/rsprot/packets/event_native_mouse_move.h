#ifndef RSPROT_PACKET_EVENT_NATIVE_MOUSE_MOVE_H
#define RSPROT_PACKET_EVENT_NATIVE_MOUSE_MOVE_H

/*
 * EVENT_NATIVE_MOUSE_MOVE -- client -> server.
 *
 * Mouse movements, each one tagged by its first byte's top bits: 111 a
 * 13-bit time and an absolute position, 11 a 6-bit time and an absolute
 * position, 1 a 7-bit time and one-byte deltas (+128), 0 a 3-bit time and
 * 6-bit deltas (+32) packed into two bytes.
 * The absolute position is two signed shorts, y then x (0, -32768: off-screen);
 * each movement ends with the last mouse button.
 *
 * Hand-written from RSProx's decoder (protocol/osrs-<N>/.../decoder/codec);
 * the C here is the source -- no generator writes this file.
 */

#include "../include/rsprot_exec.h"

#define MSG_EVENT_NATIVE_MOUSE_MOVE_MAX 1024

typedef struct MsgEventNativeMouseMoveMove {
	int32_t packed;
	int32_t time_lo;
	int32_t y;
	int32_t x;
	int32_t dx;
	int32_t dy;
	int32_t low;
	int32_t last_mouse_button;
} MsgEventNativeMouseMoveMove;

typedef struct MsgEventNativeMouseMove {
	int32_t step_excess;
	int32_t end_excess;
	int32_t move_count;
	MsgEventNativeMouseMoveMove moves[MSG_EVENT_NATIVE_MOUSE_MOVE_MAX];
} MsgEventNativeMouseMove;

/* v1 -- revs 223-241. */
void packet_event_native_mouse_move_v1_in(RsprotExec *x, MsgEventNativeMouseMove *m);

/* osrsm241 -- the Android 241.4 client. */
void packet_event_native_mouse_move_osrsm241_in(RsprotExec *x, MsgEventNativeMouseMove *m);

#define packet_event_native_mouse_move_osrs223_in packet_event_native_mouse_move_v1_in
#define packet_event_native_mouse_move_osrs224_in packet_event_native_mouse_move_v1_in
#define packet_event_native_mouse_move_osrs225_in packet_event_native_mouse_move_v1_in
#define packet_event_native_mouse_move_osrs226_in packet_event_native_mouse_move_v1_in
#define packet_event_native_mouse_move_osrs227_in packet_event_native_mouse_move_v1_in
#define packet_event_native_mouse_move_osrs228_in packet_event_native_mouse_move_v1_in
#define packet_event_native_mouse_move_osrs229_in packet_event_native_mouse_move_v1_in
#define packet_event_native_mouse_move_osrs230_in packet_event_native_mouse_move_v1_in
#define packet_event_native_mouse_move_osrs231_in packet_event_native_mouse_move_v1_in
#define packet_event_native_mouse_move_osrs232_in packet_event_native_mouse_move_v1_in
#define packet_event_native_mouse_move_osrs233_in packet_event_native_mouse_move_v1_in
#define packet_event_native_mouse_move_osrs234_in packet_event_native_mouse_move_v1_in
#define packet_event_native_mouse_move_osrs235_in packet_event_native_mouse_move_v1_in
#define packet_event_native_mouse_move_osrs236_in packet_event_native_mouse_move_v1_in
#define packet_event_native_mouse_move_osrs237_in packet_event_native_mouse_move_v1_in
#define packet_event_native_mouse_move_osrs238_in packet_event_native_mouse_move_v1_in
#define packet_event_native_mouse_move_osrs239_in packet_event_native_mouse_move_v1_in
#define packet_event_native_mouse_move_osrs240_in packet_event_native_mouse_move_v1_in
#define packet_event_native_mouse_move_osrs241_in packet_event_native_mouse_move_v1_in
#define packet_event_native_mouse_move_osrsm241_in packet_event_native_mouse_move_osrsm241_in

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_event_native_mouse_move_in[];
extern const int rsprot_event_native_mouse_move_in_count;

#endif
