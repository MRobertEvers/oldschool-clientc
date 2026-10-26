#ifndef RSPROT_PACKET_MESSAGE_PUBLIC_H
#define RSPROT_PACKET_MESSAGE_PUBLIC_H

/*
 * MESSAGE_PUBLIC -- client -> server.
 *
 * A public chat message. A colour 13..20 is a pattern of colour - 12 bytes
 * after the effect; type 3 (the clan main channel) ends with a clan type
 * byte after the text.
 *
 * The text is the chat Huffman code (rsprot_compression.h), carried as the
 * rest of the payload: a smart length then the bits; RSProx decodes it with
 * the cache's huffman table.
 *
 * Hand-written from RSProx's decoder (protocol/osrs-<N>/.../decoder/codec);
 * the C here is the source -- no generator writes this file.
 */

#include "../include/rsprot_exec.h"

typedef struct MsgMessagePublic {
	int32_t type;
	int32_t colour;
	int32_t effect;
	int32_t pattern[8];
	const uint8_t *huffman;
	int32_t huffman_length;
	int32_t clan_type;
} MsgMessagePublic;

/* v1 -- revs 222-241. */
void packet_message_public_v1_in(RsprotExec *x, MsgMessagePublic *m);

#define packet_message_public_osrs222_in packet_message_public_v1_in
#define packet_message_public_osrs223_in packet_message_public_v1_in
#define packet_message_public_osrs224_in packet_message_public_v1_in
#define packet_message_public_osrs225_in packet_message_public_v1_in
#define packet_message_public_osrs226_in packet_message_public_v1_in
#define packet_message_public_osrs227_in packet_message_public_v1_in
#define packet_message_public_osrs228_in packet_message_public_v1_in
#define packet_message_public_osrs229_in packet_message_public_v1_in
#define packet_message_public_osrs230_in packet_message_public_v1_in
#define packet_message_public_osrs231_in packet_message_public_v1_in
#define packet_message_public_osrs232_in packet_message_public_v1_in
#define packet_message_public_osrs233_in packet_message_public_v1_in
#define packet_message_public_osrs234_in packet_message_public_v1_in
#define packet_message_public_osrs235_in packet_message_public_v1_in
#define packet_message_public_osrs236_in packet_message_public_v1_in
#define packet_message_public_osrs237_in packet_message_public_v1_in
#define packet_message_public_osrs238_in packet_message_public_v1_in
#define packet_message_public_osrs239_in packet_message_public_v1_in
#define packet_message_public_osrs240_in packet_message_public_v1_in
#define packet_message_public_osrs241_in packet_message_public_v1_in

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_message_public_in[];
extern const int rsprot_message_public_in_count;

#endif
