#ifndef RSPROT_PACKET_CLIENT_CHEAT_H
#define RSPROT_PACKET_CLIENT_CHEAT_H

/*
 * CLIENT_CHEAT -- client -> server.
 *
 * A typed ::command. From 240 the client may add one byte after it: 1 when the
 * command was sent from the autocomplete list (RSProx ClientCheatDecoder,
 * `if (buffer.isReadable) buffer.g1() == 1`).
 *
 * Hand-written: v1 from RSProt's decoder (221-239), v2 from RSProx's (240-241),
 * whose optional trailing byte a spec's reads cannot say. The C here is the
 * source -- no generator writes this file.
 */

#include "../include/rsprot_exec.h"

/**
 * One struct for every version -- the union of all their fields.
 *
 * A field a version does not carry is simply not transferred, which is what
 * keeps the layer above revision-free: fill the struct, and the wire picks the
 * codec.
 */
typedef struct MsgClientCheat {
	const char *command;
	/** v2: 1 when the autocomplete byte follows, 0 when the payload ends. */
	int32_t has_autocomplete;
	/** v2: the byte; 1 means the command came from the autocomplete list. */
	int32_t autocomplete;
} MsgClientCheat;

/* v1 -- revs 221-239. */
void packet_client_cheat_v1_in(RsprotExec *x, MsgClientCheat *m);
/* v2 -- revs 240-241: v1, then the optional autocomplete byte. */
void packet_client_cheat_v2_in(RsprotExec *x, MsgClientCheat *m);

/* Per-revision entry points: `packet_client_cheat_osrs239_in` is greppable, which
 * is the point -- "what does 239 do for CLIENT_CHEAT" without reading a table. */
#define packet_client_cheat_osrs221_in packet_client_cheat_v1_in
#define packet_client_cheat_osrs222_in packet_client_cheat_v1_in
#define packet_client_cheat_osrs223_in packet_client_cheat_v1_in
#define packet_client_cheat_osrs224_in packet_client_cheat_v1_in
#define packet_client_cheat_osrs225_in packet_client_cheat_v1_in
#define packet_client_cheat_osrs226_in packet_client_cheat_v1_in
#define packet_client_cheat_osrs227_in packet_client_cheat_v1_in
#define packet_client_cheat_osrs228_in packet_client_cheat_v1_in
#define packet_client_cheat_osrs229_in packet_client_cheat_v1_in
#define packet_client_cheat_osrs230_in packet_client_cheat_v1_in
#define packet_client_cheat_osrs231_in packet_client_cheat_v1_in
#define packet_client_cheat_osrs232_in packet_client_cheat_v1_in
#define packet_client_cheat_osrs233_in packet_client_cheat_v1_in
#define packet_client_cheat_osrs234_in packet_client_cheat_v1_in
#define packet_client_cheat_osrs235_in packet_client_cheat_v1_in
#define packet_client_cheat_osrs236_in packet_client_cheat_v1_in
#define packet_client_cheat_osrs237_in packet_client_cheat_v1_in
#define packet_client_cheat_osrs238_in packet_client_cheat_v1_in
#define packet_client_cheat_osrs239_in packet_client_cheat_v1_in
#define packet_client_cheat_osrs240_in packet_client_cheat_v2_in
#define packet_client_cheat_osrs241_in packet_client_cheat_v2_in

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_client_cheat_in[];
extern const int rsprot_client_cheat_in_count;

#endif
