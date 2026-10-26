#ifndef RSPROT_PACKET_UPDATE_FRIENDCHAT_CHANNEL_FULL_V1_H
#define RSPROT_PACKET_UPDATE_FRIENDCHAT_CHANNEL_FULL_V1_H

/*
 * UPDATE_FRIENDCHAT_CHANNEL_FULL_V1 -- server -> client.
 *
 * The whole friends chat channel, or (an empty payload) leaving it.
 * `channel_name` is base37.
 * V1: an unsigned kick rank and a byte entry count.
 *
 * Hand-written from RSProx's decoder (protocol/osrs-<N>/.../decoder/codec);
 * the C here is the source -- no generator writes this file.
 */

#include "../include/rsprot_exec.h"

#define MSG_FRIENDCHAT_V1_MAX_ENTRIES 512

typedef struct MsgUpdateFriendchatChannelFullV1Entry {
	const char *name;
	int32_t world_id;
	int32_t rank;
	const char *world_name;
} MsgUpdateFriendchatChannelFullV1Entry;

typedef struct MsgUpdateFriendchatChannelFullV1 {
	/** 0: the payload is empty -- the channel was left. */
	int32_t joined;
	const char *channel_owner;
	int64_t channel_name; /* base37 */
	int32_t kick_rank;
	int32_t entry_count;
	MsgUpdateFriendchatChannelFullV1Entry entries[MSG_FRIENDCHAT_V1_MAX_ENTRIES];
} MsgUpdateFriendchatChannelFullV1;

/* v1 -- revs 222-226. */
void packet_update_friendchat_channel_full_v1_v1_out(RsprotExec *x, MsgUpdateFriendchatChannelFullV1 *m);

#define packet_update_friendchat_channel_full_v1_osrs222_out packet_update_friendchat_channel_full_v1_v1_out
#define packet_update_friendchat_channel_full_v1_osrs223_out packet_update_friendchat_channel_full_v1_v1_out
#define packet_update_friendchat_channel_full_v1_osrs224_out packet_update_friendchat_channel_full_v1_v1_out
#define packet_update_friendchat_channel_full_v1_osrs225_out packet_update_friendchat_channel_full_v1_v1_out
#define packet_update_friendchat_channel_full_v1_osrs226_out packet_update_friendchat_channel_full_v1_v1_out

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_update_friendchat_channel_full_v1_out[];
extern const int rsprot_update_friendchat_channel_full_v1_out_count;

#endif
