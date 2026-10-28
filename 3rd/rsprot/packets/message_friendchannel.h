#ifndef RSPROT_PACKET_MESSAGE_FRIENDCHANNEL_H
#define RSPROT_PACKET_MESSAGE_FRIENDCHANNEL_H

/*
 * MESSAGE_FRIENDCHANNEL -- server -> client.
 *
 * A friends chat message. `channel_name` is base37 (RSProx Base37.decodeWithCase).
 *
 * The text is the chat Huffman code (rsprot_compression.h), carried as the
 * rest of the payload: a smart length then the bits; RSProx decodes it with
 * the cache's huffman table.
 *
 * Hand-written from RSProx's decoder (protocol/osrs-<N>/.../decoder/codec);
 * the C here is the source -- no generator writes this file.
 */

#include "../include/rsprot_exec.h"

typedef struct MsgMessageFriendchannel {
	const char *sender;
	int64_t channel_name;
	int32_t world_id;
	int32_t world_message_counter;
	int32_t chat_crown_type;
	const uint8_t *huffman;
	int32_t huffman_length;
} MsgMessageFriendchannel;

/* v1 -- revs 223-241. */
void packet_message_friendchannel_v1_out(RsprotExec *x, MsgMessageFriendchannel *m);

#define packet_message_friendchannel_osrs223_out packet_message_friendchannel_v1_out
#define packet_message_friendchannel_osrs224_out packet_message_friendchannel_v1_out
#define packet_message_friendchannel_osrs225_out packet_message_friendchannel_v1_out
#define packet_message_friendchannel_osrs226_out packet_message_friendchannel_v1_out
#define packet_message_friendchannel_osrs227_out packet_message_friendchannel_v1_out
#define packet_message_friendchannel_osrs228_out packet_message_friendchannel_v1_out
#define packet_message_friendchannel_osrs229_out packet_message_friendchannel_v1_out
#define packet_message_friendchannel_osrs230_out packet_message_friendchannel_v1_out
#define packet_message_friendchannel_osrs231_out packet_message_friendchannel_v1_out
#define packet_message_friendchannel_osrs232_out packet_message_friendchannel_v1_out
#define packet_message_friendchannel_osrs233_out packet_message_friendchannel_v1_out
#define packet_message_friendchannel_osrs234_out packet_message_friendchannel_v1_out
#define packet_message_friendchannel_osrs235_out packet_message_friendchannel_v1_out
#define packet_message_friendchannel_osrs236_out packet_message_friendchannel_v1_out
#define packet_message_friendchannel_osrs237_out packet_message_friendchannel_v1_out
#define packet_message_friendchannel_osrs238_out packet_message_friendchannel_v1_out
#define packet_message_friendchannel_osrs239_out packet_message_friendchannel_v1_out
#define packet_message_friendchannel_osrs240_out packet_message_friendchannel_v1_out
#define packet_message_friendchannel_osrs241_out packet_message_friendchannel_v1_out
#define packet_message_friendchannel_osrsm241_out packet_message_friendchannel_v1_out

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_message_friendchannel_out[];
extern const int rsprot_message_friendchannel_out_count;

#endif
