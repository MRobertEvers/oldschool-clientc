#ifndef RSPROT_PACKET_UPDATE_PLAYER_MODEL_V1_H
#define RSPROT_PACKET_UPDATE_PLAYER_MODEL_V1_H

/*
 * UPDATE_PLAYER_MODEL_V1 -- client -> server.
 *
 * The character designer's choice, 223-225 (13 bytes): the body type, the
 * seven ident kits and the five colours, a byte each. 223 calls the prot
 * UPDATE_PLAYER_MODEL, 224-225 UPDATE_PLAYER_MODEL_V1 (RSProt
 * UPDATE_PLAYER_MODEL_OLD). RSProx has no decoder for the 26-byte V2, so
 * neither does this library.
 *
 * Hand-written from RSProx's decoder (protocol/osrs-<N>/.../decoder/codec);
 * the C here is the source -- no generator writes this file.
 */

#include "../include/rsprot_exec.h"

typedef struct MsgUpdatePlayerModelV1 {
	int32_t body_type;
	int32_t ident_kit[7];
	int32_t colours[5];
} MsgUpdatePlayerModelV1;

/* v1 -- revs 223-225. */
void packet_update_player_model_v1_v1_in(RsprotExec *x, MsgUpdatePlayerModelV1 *m);

#define packet_update_player_model_v1_osrs223_in packet_update_player_model_v1_v1_in
#define packet_update_player_model_v1_osrs224_in packet_update_player_model_v1_v1_in
#define packet_update_player_model_v1_osrs225_in packet_update_player_model_v1_v1_in

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_update_player_model_v1_in[];
extern const int rsprot_update_player_model_v1_in_count;

#endif
