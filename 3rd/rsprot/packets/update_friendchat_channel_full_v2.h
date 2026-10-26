#ifndef RSPROT_PACKET_UPDATE_FRIENDCHAT_CHANNEL_FULL_V2_H
#define RSPROT_PACKET_UPDATE_FRIENDCHAT_CHANNEL_FULL_V2_H

/*
 * UPDATE_FRIENDCHAT_CHANNEL_FULL_V2 -- server -> client.
 *
 * The whole friends chat channel, or (an empty payload) leaving it.
 * `channel_name` is base37.
 * V2: a signed kick rank and a smart (null) entry count.
 *
 * Hand-written from RSProx's decoder (protocol/osrs-<N>/.../decoder/codec);
 * the C here is the source -- no generator writes this file.
 */

#include "../include/rsprot_exec.h"

#define MSG_FRIENDCHAT_V2_MAX_ENTRIES 512

typedef struct MsgUpdateFriendchatChannelFullV2Entry {
	const char *name;
	int32_t world_id;
	int32_t rank;
	const char *world_name;
} MsgUpdateFriendchatChannelFullV2Entry;

typedef struct MsgUpdateFriendchatChannelFullV2 {
	/** 0: the payload is empty -- the channel was left. */
	int32_t joined;
	const char *channel_owner;
	int64_t channel_name; /* base37 */
	int32_t kick_rank;
	int32_t entry_count;
	MsgUpdateFriendchatChannelFullV2Entry entries[MSG_FRIENDCHAT_V2_MAX_ENTRIES];
} MsgUpdateFriendchatChannelFullV2;

/* v1 -- revs 223-241. */
void packet_update_friendchat_channel_full_v2_v1_out(RsprotExec *x, MsgUpdateFriendchatChannelFullV2 *m);

#define packet_update_friendchat_channel_full_v2_osrs223_out packet_update_friendchat_channel_full_v2_v1_out
#define packet_update_friendchat_channel_full_v2_osrs224_out packet_update_friendchat_channel_full_v2_v1_out
#define packet_update_friendchat_channel_full_v2_osrs225_out packet_update_friendchat_channel_full_v2_v1_out
#define packet_update_friendchat_channel_full_v2_osrs226_out packet_update_friendchat_channel_full_v2_v1_out
#define packet_update_friendchat_channel_full_v2_osrs227_out packet_update_friendchat_channel_full_v2_v1_out
#define packet_update_friendchat_channel_full_v2_osrs228_out packet_update_friendchat_channel_full_v2_v1_out
#define packet_update_friendchat_channel_full_v2_osrs229_out packet_update_friendchat_channel_full_v2_v1_out
#define packet_update_friendchat_channel_full_v2_osrs230_out packet_update_friendchat_channel_full_v2_v1_out
#define packet_update_friendchat_channel_full_v2_osrs231_out packet_update_friendchat_channel_full_v2_v1_out
#define packet_update_friendchat_channel_full_v2_osrs232_out packet_update_friendchat_channel_full_v2_v1_out
#define packet_update_friendchat_channel_full_v2_osrs233_out packet_update_friendchat_channel_full_v2_v1_out
#define packet_update_friendchat_channel_full_v2_osrs234_out packet_update_friendchat_channel_full_v2_v1_out
#define packet_update_friendchat_channel_full_v2_osrs235_out packet_update_friendchat_channel_full_v2_v1_out
#define packet_update_friendchat_channel_full_v2_osrs236_out packet_update_friendchat_channel_full_v2_v1_out
#define packet_update_friendchat_channel_full_v2_osrs237_out packet_update_friendchat_channel_full_v2_v1_out
#define packet_update_friendchat_channel_full_v2_osrs238_out packet_update_friendchat_channel_full_v2_v1_out
#define packet_update_friendchat_channel_full_v2_osrs239_out packet_update_friendchat_channel_full_v2_v1_out
#define packet_update_friendchat_channel_full_v2_osrs240_out packet_update_friendchat_channel_full_v2_v1_out
#define packet_update_friendchat_channel_full_v2_osrs241_out packet_update_friendchat_channel_full_v2_v1_out

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_update_friendchat_channel_full_v2_out[];
extern const int rsprot_update_friendchat_channel_full_v2_out_count;

#endif
