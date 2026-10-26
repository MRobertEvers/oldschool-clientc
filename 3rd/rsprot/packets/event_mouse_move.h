#ifndef RSPROT_PACKET_EVENT_MOUSE_MOVE_H
#define RSPROT_PACKET_EVENT_MOUSE_MOVE_H

/*
 * EVENT_MOUSE_MOVE -- client -> server.
 *
 * Mouse movements, each one tagged by its first byte's top bits: 111 a
 * 13-bit time and an absolute position, 11 a 6-bit time and an absolute
 * position, 1 a 7-bit time and one-byte deltas (+128), 0 a 3-bit time and
 * 6-bit deltas (+32) packed into two bytes.
 * The absolute position is one int, y << 16 | x (Int.MIN_VALUE: off-screen).
 *
 * Hand-written from RSProx's decoder (protocol/osrs-<N>/.../decoder/codec);
 * the C here is the source -- no generator writes this file.
 */

#include "../include/rsprot_exec.h"

#define MSG_EVENT_MOUSE_MOVE_MAX 1024

typedef struct MsgEventMouseMoveMove {
	int32_t packed;
	int32_t time_lo;
	int32_t position;
	int32_t dx;
	int32_t dy;
	int32_t low;
} MsgEventMouseMoveMove;

typedef struct MsgEventMouseMove {
	int32_t step_excess;
	int32_t end_excess;
	int32_t move_count;
	MsgEventMouseMoveMove moves[MSG_EVENT_MOUSE_MOVE_MAX];
} MsgEventMouseMove;

/* v1 -- revs 223-241. */
void packet_event_mouse_move_v1_in(RsprotExec *x, MsgEventMouseMove *m);

#define packet_event_mouse_move_osrs223_in packet_event_mouse_move_v1_in
#define packet_event_mouse_move_osrs224_in packet_event_mouse_move_v1_in
#define packet_event_mouse_move_osrs225_in packet_event_mouse_move_v1_in
#define packet_event_mouse_move_osrs226_in packet_event_mouse_move_v1_in
#define packet_event_mouse_move_osrs227_in packet_event_mouse_move_v1_in
#define packet_event_mouse_move_osrs228_in packet_event_mouse_move_v1_in
#define packet_event_mouse_move_osrs229_in packet_event_mouse_move_v1_in
#define packet_event_mouse_move_osrs230_in packet_event_mouse_move_v1_in
#define packet_event_mouse_move_osrs231_in packet_event_mouse_move_v1_in
#define packet_event_mouse_move_osrs232_in packet_event_mouse_move_v1_in
#define packet_event_mouse_move_osrs233_in packet_event_mouse_move_v1_in
#define packet_event_mouse_move_osrs234_in packet_event_mouse_move_v1_in
#define packet_event_mouse_move_osrs235_in packet_event_mouse_move_v1_in
#define packet_event_mouse_move_osrs236_in packet_event_mouse_move_v1_in
#define packet_event_mouse_move_osrs237_in packet_event_mouse_move_v1_in
#define packet_event_mouse_move_osrs238_in packet_event_mouse_move_v1_in
#define packet_event_mouse_move_osrs239_in packet_event_mouse_move_v1_in
#define packet_event_mouse_move_osrs240_in packet_event_mouse_move_v1_in
#define packet_event_mouse_move_osrs241_in packet_event_mouse_move_v1_in

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_event_mouse_move_in[];
extern const int rsprot_event_mouse_move_in_count;

#endif
