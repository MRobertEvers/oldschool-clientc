#ifndef RSPROT_PACKET_MESSAGE_CLANCHANNEL_SYSTEM_H
#define RSPROT_PACKET_MESSAGE_CLANCHANNEL_SYSTEM_H

/*
 * MESSAGE_CLANCHANNEL_SYSTEM -- server -> client.
 *
 * A clan channel system message.
 *
 * The text is the chat Huffman code (rsprot_compression.h), carried as the
 * rest of the payload: a smart length then the bits; RSProx decodes it with
 * the cache's huffman table.
 *
 * Hand-written from RSProx's decoder (protocol/osrs-<N>/.../decoder/codec);
 * the C here is the source -- no generator writes this file.
 */

#include "../include/rsprot_exec.h"

typedef struct MsgMessageClanchannelSystem {
	int32_t clan_type;
	int32_t world_id;
	int32_t world_message_counter;
	const uint8_t *huffman;
	int32_t huffman_length;
} MsgMessageClanchannelSystem;

/* v1 -- revs 222-241. */
void packet_message_clanchannel_system_v1_out(RsprotExec *x, MsgMessageClanchannelSystem *m);

#define packet_message_clanchannel_system_osrs222_out packet_message_clanchannel_system_v1_out
#define packet_message_clanchannel_system_osrs223_out packet_message_clanchannel_system_v1_out
#define packet_message_clanchannel_system_osrs224_out packet_message_clanchannel_system_v1_out
#define packet_message_clanchannel_system_osrs225_out packet_message_clanchannel_system_v1_out
#define packet_message_clanchannel_system_osrs226_out packet_message_clanchannel_system_v1_out
#define packet_message_clanchannel_system_osrs227_out packet_message_clanchannel_system_v1_out
#define packet_message_clanchannel_system_osrs228_out packet_message_clanchannel_system_v1_out
#define packet_message_clanchannel_system_osrs229_out packet_message_clanchannel_system_v1_out
#define packet_message_clanchannel_system_osrs230_out packet_message_clanchannel_system_v1_out
#define packet_message_clanchannel_system_osrs231_out packet_message_clanchannel_system_v1_out
#define packet_message_clanchannel_system_osrs232_out packet_message_clanchannel_system_v1_out
#define packet_message_clanchannel_system_osrs233_out packet_message_clanchannel_system_v1_out
#define packet_message_clanchannel_system_osrs234_out packet_message_clanchannel_system_v1_out
#define packet_message_clanchannel_system_osrs235_out packet_message_clanchannel_system_v1_out
#define packet_message_clanchannel_system_osrs236_out packet_message_clanchannel_system_v1_out
#define packet_message_clanchannel_system_osrs237_out packet_message_clanchannel_system_v1_out
#define packet_message_clanchannel_system_osrs238_out packet_message_clanchannel_system_v1_out
#define packet_message_clanchannel_system_osrs239_out packet_message_clanchannel_system_v1_out
#define packet_message_clanchannel_system_osrs240_out packet_message_clanchannel_system_v1_out
#define packet_message_clanchannel_system_osrs241_out packet_message_clanchannel_system_v1_out

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_message_clanchannel_system_out[];
extern const int rsprot_message_clanchannel_system_out_count;

#endif
