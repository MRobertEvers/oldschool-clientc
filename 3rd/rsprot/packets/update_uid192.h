#ifndef RSPROT_PACKET_UPDATE_UID192_H
#define RSPROT_PACKET_UPDATE_UID192_H

/*
 * UPDATE_UID192 -- server -> client.
 *
 * Hand-written from RSProx (UpdateUid192Decoder, protocol/osrs-<N>/.../decoder/codec/misc/client/UpdateUid192Decoder.kt).
 * RSProt has no codec for this prot, and the RSProx decoder reads a fixed blob and checks a CRC over it, so it is written by hand: tools/rsprot_gen.py has no spec for it.
 * Field names are RSProx's; version numbers are local to this file.
 */

#include "../include/rsprot_exec.h"

typedef struct MsgUpdateUid192 {
	const uint8_t *data; /* 24 bytes */
	int32_t crc;         /* CRC-32 of `data`; RSProx rejects a mismatch */
} MsgUpdateUid192;

/* v1 -- revs 223-241. */
void packet_update_uid192_v1_out(RsprotExec *x, MsgUpdateUid192 *m);

#define packet_update_uid192_osrs223_out packet_update_uid192_v1_out
#define packet_update_uid192_osrs224_out packet_update_uid192_v1_out
#define packet_update_uid192_osrs225_out packet_update_uid192_v1_out
#define packet_update_uid192_osrs226_out packet_update_uid192_v1_out
#define packet_update_uid192_osrs227_out packet_update_uid192_v1_out
#define packet_update_uid192_osrs228_out packet_update_uid192_v1_out
#define packet_update_uid192_osrs229_out packet_update_uid192_v1_out
#define packet_update_uid192_osrs230_out packet_update_uid192_v1_out
#define packet_update_uid192_osrs231_out packet_update_uid192_v1_out
#define packet_update_uid192_osrs232_out packet_update_uid192_v1_out
#define packet_update_uid192_osrs233_out packet_update_uid192_v1_out
#define packet_update_uid192_osrs234_out packet_update_uid192_v1_out
#define packet_update_uid192_osrs235_out packet_update_uid192_v1_out
#define packet_update_uid192_osrs236_out packet_update_uid192_v1_out
#define packet_update_uid192_osrs237_out packet_update_uid192_v1_out
#define packet_update_uid192_osrs238_out packet_update_uid192_v1_out
#define packet_update_uid192_osrs239_out packet_update_uid192_v1_out
#define packet_update_uid192_osrs240_out packet_update_uid192_v1_out
#define packet_update_uid192_osrs241_out packet_update_uid192_v1_out

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_update_uid192_out[];
extern const int rsprot_update_uid192_out_count;

#endif
