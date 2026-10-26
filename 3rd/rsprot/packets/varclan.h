#ifndef RSPROT_PACKET_VARCLAN_H
#define RSPROT_PACKET_VARCLAN_H

/*
 * VARCLAN -- server -> client.
 *
 * A clan variable. RSProx keeps its value as the raw rest of the payload
 * (VarClan.UnknownVarClanData): the type is the cache's varclan's.
 *
 * Hand-written from RSProx's decoder (protocol/osrs-<N>/.../decoder/codec);
 * the C here is the source -- no generator writes this file.
 */

#include "../include/rsprot_exec.h"

typedef struct MsgVarclan {
	int32_t id;
	const uint8_t *data;
	int32_t data_length;
} MsgVarclan;

/* v1 -- revs 223-241. */
void packet_varclan_v1_out(RsprotExec *x, MsgVarclan *m);

#define packet_varclan_osrs223_out packet_varclan_v1_out
#define packet_varclan_osrs224_out packet_varclan_v1_out
#define packet_varclan_osrs225_out packet_varclan_v1_out
#define packet_varclan_osrs226_out packet_varclan_v1_out
#define packet_varclan_osrs227_out packet_varclan_v1_out
#define packet_varclan_osrs228_out packet_varclan_v1_out
#define packet_varclan_osrs229_out packet_varclan_v1_out
#define packet_varclan_osrs230_out packet_varclan_v1_out
#define packet_varclan_osrs231_out packet_varclan_v1_out
#define packet_varclan_osrs232_out packet_varclan_v1_out
#define packet_varclan_osrs233_out packet_varclan_v1_out
#define packet_varclan_osrs234_out packet_varclan_v1_out
#define packet_varclan_osrs235_out packet_varclan_v1_out
#define packet_varclan_osrs236_out packet_varclan_v1_out
#define packet_varclan_osrs237_out packet_varclan_v1_out
#define packet_varclan_osrs238_out packet_varclan_v1_out
#define packet_varclan_osrs239_out packet_varclan_v1_out
#define packet_varclan_osrs240_out packet_varclan_v1_out
#define packet_varclan_osrs241_out packet_varclan_v1_out

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_varclan_out[];
extern const int rsprot_varclan_out_count;

#endif
