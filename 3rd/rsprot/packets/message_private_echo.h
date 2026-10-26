#ifndef RSPROT_PACKET_MESSAGE_PRIVATE_ECHO_H
#define RSPROT_PACKET_MESSAGE_PRIVATE_ECHO_H

/*
 * MESSAGE_PRIVATE_ECHO -- server -> client.
 *
 * A private message sent, echoed back.
 *
 * The text is the chat Huffman code (rsprot_compression.h), carried as the
 * rest of the payload: a smart length then the bits; RSProx decodes it with
 * the cache's huffman table.
 *
 * Hand-written from RSProx's decoder (protocol/osrs-<N>/.../decoder/codec);
 * the C here is the source -- no generator writes this file.
 */

#include "../include/rsprot_exec.h"

typedef struct MsgMessagePrivateEcho {
	const char *recipient;
	const uint8_t *huffman;
	int32_t huffman_length;
} MsgMessagePrivateEcho;

/* v1 -- revs 223-241. */
void packet_message_private_echo_v1_out(RsprotExec *x, MsgMessagePrivateEcho *m);

#define packet_message_private_echo_osrs223_out packet_message_private_echo_v1_out
#define packet_message_private_echo_osrs224_out packet_message_private_echo_v1_out
#define packet_message_private_echo_osrs225_out packet_message_private_echo_v1_out
#define packet_message_private_echo_osrs226_out packet_message_private_echo_v1_out
#define packet_message_private_echo_osrs227_out packet_message_private_echo_v1_out
#define packet_message_private_echo_osrs228_out packet_message_private_echo_v1_out
#define packet_message_private_echo_osrs229_out packet_message_private_echo_v1_out
#define packet_message_private_echo_osrs230_out packet_message_private_echo_v1_out
#define packet_message_private_echo_osrs231_out packet_message_private_echo_v1_out
#define packet_message_private_echo_osrs232_out packet_message_private_echo_v1_out
#define packet_message_private_echo_osrs233_out packet_message_private_echo_v1_out
#define packet_message_private_echo_osrs234_out packet_message_private_echo_v1_out
#define packet_message_private_echo_osrs235_out packet_message_private_echo_v1_out
#define packet_message_private_echo_osrs236_out packet_message_private_echo_v1_out
#define packet_message_private_echo_osrs237_out packet_message_private_echo_v1_out
#define packet_message_private_echo_osrs238_out packet_message_private_echo_v1_out
#define packet_message_private_echo_osrs239_out packet_message_private_echo_v1_out
#define packet_message_private_echo_osrs240_out packet_message_private_echo_v1_out
#define packet_message_private_echo_osrs241_out packet_message_private_echo_v1_out

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_message_private_echo_out[];
extern const int rsprot_message_private_echo_out_count;

#endif
