#ifndef RSPROT_PACKET_MESSAGE_PRIVATE_H
#define RSPROT_PACKET_MESSAGE_PRIVATE_H

/*
 * MESSAGE_PRIVATE -- server -> client.
 *
 * A private message received.
 *
 * The text is the chat Huffman code (rsprot_compression.h), carried as the
 * rest of the payload: a smart length then the bits; RSProx decodes it with
 * the cache's huffman table.
 *
 * Hand-written from RSProx's decoder (protocol/osrs-<N>/.../decoder/codec);
 * the C here is the source -- no generator writes this file.
 */

#include "../include/rsprot_exec.h"

typedef struct MsgMessagePrivate {
	const char *sender;
	int32_t world_id;
	int32_t world_message_counter;
	int32_t chat_crown_type;
	const uint8_t *huffman;
	int32_t huffman_length;
} MsgMessagePrivate;

/* v1 -- revs 222-241. */
void packet_message_private_v1_out(RsprotExec *x, MsgMessagePrivate *m);

#define packet_message_private_osrs222_out packet_message_private_v1_out
#define packet_message_private_osrs223_out packet_message_private_v1_out
#define packet_message_private_osrs224_out packet_message_private_v1_out
#define packet_message_private_osrs225_out packet_message_private_v1_out
#define packet_message_private_osrs226_out packet_message_private_v1_out
#define packet_message_private_osrs227_out packet_message_private_v1_out
#define packet_message_private_osrs228_out packet_message_private_v1_out
#define packet_message_private_osrs229_out packet_message_private_v1_out
#define packet_message_private_osrs230_out packet_message_private_v1_out
#define packet_message_private_osrs231_out packet_message_private_v1_out
#define packet_message_private_osrs232_out packet_message_private_v1_out
#define packet_message_private_osrs233_out packet_message_private_v1_out
#define packet_message_private_osrs234_out packet_message_private_v1_out
#define packet_message_private_osrs235_out packet_message_private_v1_out
#define packet_message_private_osrs236_out packet_message_private_v1_out
#define packet_message_private_osrs237_out packet_message_private_v1_out
#define packet_message_private_osrs238_out packet_message_private_v1_out
#define packet_message_private_osrs239_out packet_message_private_v1_out
#define packet_message_private_osrs240_out packet_message_private_v1_out
#define packet_message_private_osrs241_out packet_message_private_v1_out
#define packet_message_private_osrsm241_out packet_message_private_v1_out

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_message_private_out[];
extern const int rsprot_message_private_out_count;


/* --- client -> server -------------------------------------------- */

typedef struct MsgMessagePrivateClient {
	const char *name;
	const uint8_t *huffman;
	int32_t huffman_length;
} MsgMessagePrivateClient;

/* v1 -- revs 222-241. */
void packet_message_private_v1_in(RsprotExec *x, MsgMessagePrivateClient *m);

/* osrsm241 -- the Android 241.4 client. */
void packet_message_private_osrsm241_in(RsprotExec *x, MsgMessagePrivateClient *m);

#define packet_message_private_osrs222_in packet_message_private_v1_in
#define packet_message_private_osrs223_in packet_message_private_v1_in
#define packet_message_private_osrs224_in packet_message_private_v1_in
#define packet_message_private_osrs225_in packet_message_private_v1_in
#define packet_message_private_osrs226_in packet_message_private_v1_in
#define packet_message_private_osrs227_in packet_message_private_v1_in
#define packet_message_private_osrs228_in packet_message_private_v1_in
#define packet_message_private_osrs229_in packet_message_private_v1_in
#define packet_message_private_osrs230_in packet_message_private_v1_in
#define packet_message_private_osrs231_in packet_message_private_v1_in
#define packet_message_private_osrs232_in packet_message_private_v1_in
#define packet_message_private_osrs233_in packet_message_private_v1_in
#define packet_message_private_osrs234_in packet_message_private_v1_in
#define packet_message_private_osrs235_in packet_message_private_v1_in
#define packet_message_private_osrs236_in packet_message_private_v1_in
#define packet_message_private_osrs237_in packet_message_private_v1_in
#define packet_message_private_osrs238_in packet_message_private_v1_in
#define packet_message_private_osrs239_in packet_message_private_v1_in
#define packet_message_private_osrs240_in packet_message_private_v1_in
#define packet_message_private_osrs241_in packet_message_private_v1_in
#define packet_message_private_osrsm241_in packet_message_private_osrsm241_in

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_message_private_in[];
extern const int rsprot_message_private_in_count;

#endif
