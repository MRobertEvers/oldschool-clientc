#ifndef RSPROT_PACKET_CLANCHANNEL_DELTA_H
#define RSPROT_PACKET_CLANCHANNEL_DELTA_H

/*
 * CLANCHANNEL_DELTA -- server -> client.
 *
 * Changes to the clan channel, as opcode-tagged events until a 0 opcode or
 * the end of the payload: 1 a member joined, 2 a member's details changed,
 * 3 a member left, 4 the channel's settings (name absent: cleared), 5
 * details again (V2). `skip*` are bytes RSProx skips, kept so the layout
 * round-trips.
 *
 * Hand-written from RSProx's decoder (protocol/osrs-<N>/.../decoder/codec);
 * the C here is the source -- no generator writes this file.
 */

#include "../include/rsprot_exec.h"

#define MSG_CLANCHANNEL_DELTA_MAX_EVENTS 512

typedef struct MsgClanchannelDeltaEvent {
	int32_t opcode;
	int32_t index;
	const char *name;
	int32_t world;
	int32_t rank;
	int32_t talk_rank;
	int32_t kick_rank;
	int32_t skip_a;
	int32_t skip_b;
	int64_t skip8;
} MsgClanchannelDeltaEvent;

typedef struct MsgClanchannelDelta {
	int32_t clan_type;
	int64_t clan_hash;
	int64_t update_num;
	int32_t event_count;
	MsgClanchannelDeltaEvent events[MSG_CLANCHANNEL_DELTA_MAX_EVENTS];
} MsgClanchannelDelta;

/* v1 -- revs 223-241. */
void packet_clanchannel_delta_v1_out(RsprotExec *x, MsgClanchannelDelta *m);

#define packet_clanchannel_delta_osrs223_out packet_clanchannel_delta_v1_out
#define packet_clanchannel_delta_osrs224_out packet_clanchannel_delta_v1_out
#define packet_clanchannel_delta_osrs225_out packet_clanchannel_delta_v1_out
#define packet_clanchannel_delta_osrs226_out packet_clanchannel_delta_v1_out
#define packet_clanchannel_delta_osrs227_out packet_clanchannel_delta_v1_out
#define packet_clanchannel_delta_osrs228_out packet_clanchannel_delta_v1_out
#define packet_clanchannel_delta_osrs229_out packet_clanchannel_delta_v1_out
#define packet_clanchannel_delta_osrs230_out packet_clanchannel_delta_v1_out
#define packet_clanchannel_delta_osrs231_out packet_clanchannel_delta_v1_out
#define packet_clanchannel_delta_osrs232_out packet_clanchannel_delta_v1_out
#define packet_clanchannel_delta_osrs233_out packet_clanchannel_delta_v1_out
#define packet_clanchannel_delta_osrs234_out packet_clanchannel_delta_v1_out
#define packet_clanchannel_delta_osrs235_out packet_clanchannel_delta_v1_out
#define packet_clanchannel_delta_osrs236_out packet_clanchannel_delta_v1_out
#define packet_clanchannel_delta_osrs237_out packet_clanchannel_delta_v1_out
#define packet_clanchannel_delta_osrs238_out packet_clanchannel_delta_v1_out
#define packet_clanchannel_delta_osrs239_out packet_clanchannel_delta_v1_out
#define packet_clanchannel_delta_osrs240_out packet_clanchannel_delta_v1_out
#define packet_clanchannel_delta_osrs241_out packet_clanchannel_delta_v1_out

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_clanchannel_delta_out[];
extern const int rsprot_clanchannel_delta_out_count;

#endif
