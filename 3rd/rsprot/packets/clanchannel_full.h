#ifndef RSPROT_PACKET_CLANCHANNEL_FULL_H
#define RSPROT_PACKET_CLANCHANNEL_FULL_H

/*
 * CLANCHANNEL_FULL -- server -> client.
 *
 * The whole clan channel, or (nothing after the clan type) leaving it.
 *
 * `flags`: 0x1 names as base37 longs, 0x2 as display strings (one must be
 * set; RSProx refuses neither), 0x4 a version byte follows -- absent, the
 * version is 2 (ClanChannelFull.DEFAULT_OLDSCHOOL_VERSION). From version 3
 * each member carries one more boolean.
 *
 * Hand-written from RSProx's decoder (protocol/osrs-<N>/.../decoder/codec);
 * the C here is the source -- no generator writes this file.
 */

#include "../include/rsprot_exec.h"

#define MSG_CLANCHANNEL_FULL_MAX_MEMBERS 2048

typedef struct MsgClanchannelFullMember {
	int64_t name37;
	const char *name;
	int32_t rank;
	int32_t world;
	int32_t discarded;
} MsgClanchannelFullMember;

typedef struct MsgClanchannelFull {
	int32_t clan_type;
	/** 0: the clan type alone -- the channel was left. */
	int32_t joined;
	int32_t flags;
	int32_t version;
	int64_t clan_hash;
	int64_t update_num;
	const char *clan_name;
	int32_t discarded;
	int32_t kick_rank;
	int32_t talk_rank;
	int32_t member_count;
	MsgClanchannelFullMember members[MSG_CLANCHANNEL_FULL_MAX_MEMBERS];
} MsgClanchannelFull;

/* v1 -- revs 223-241. */
void packet_clanchannel_full_v1_out(RsprotExec *x, MsgClanchannelFull *m);

#define packet_clanchannel_full_osrs223_out packet_clanchannel_full_v1_out
#define packet_clanchannel_full_osrs224_out packet_clanchannel_full_v1_out
#define packet_clanchannel_full_osrs225_out packet_clanchannel_full_v1_out
#define packet_clanchannel_full_osrs226_out packet_clanchannel_full_v1_out
#define packet_clanchannel_full_osrs227_out packet_clanchannel_full_v1_out
#define packet_clanchannel_full_osrs228_out packet_clanchannel_full_v1_out
#define packet_clanchannel_full_osrs229_out packet_clanchannel_full_v1_out
#define packet_clanchannel_full_osrs230_out packet_clanchannel_full_v1_out
#define packet_clanchannel_full_osrs231_out packet_clanchannel_full_v1_out
#define packet_clanchannel_full_osrs232_out packet_clanchannel_full_v1_out
#define packet_clanchannel_full_osrs233_out packet_clanchannel_full_v1_out
#define packet_clanchannel_full_osrs234_out packet_clanchannel_full_v1_out
#define packet_clanchannel_full_osrs235_out packet_clanchannel_full_v1_out
#define packet_clanchannel_full_osrs236_out packet_clanchannel_full_v1_out
#define packet_clanchannel_full_osrs237_out packet_clanchannel_full_v1_out
#define packet_clanchannel_full_osrs238_out packet_clanchannel_full_v1_out
#define packet_clanchannel_full_osrs239_out packet_clanchannel_full_v1_out
#define packet_clanchannel_full_osrs240_out packet_clanchannel_full_v1_out
#define packet_clanchannel_full_osrs241_out packet_clanchannel_full_v1_out

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_clanchannel_full_out[];
extern const int rsprot_clanchannel_full_out_count;

#endif
