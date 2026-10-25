#ifndef RSPROT_PACKET_UPDATE_UID192_H
#define RSPROT_PACKET_UPDATE_UID192_H

/*
 * UPDATE_UID192 -- server -> client.
 *
 * Hand-written from RSProx (UpdateUid192Decoder, protocol/osrs-<N>/.../decoder/codec/misc/client/UpdateUid192Decoder.kt).
 * RSProt has no codec for this prot, and the RSProx decoder reads a fixed blob and checks a CRC over it, which
 * tools/rsprox_gen_codec.py does not translate -- it lists this file in HAND and
 * never overwrites it. Field names are RSProx's. Version numbers are local to
 * this file and start at 100, the RSProx numbering (gen/rsprox_versions.txt).
 */

#include "../include/rsprot_exec.h"

typedef struct MsgUpdateUid192 {
	const uint8_t *data; /* 24 bytes */
	int32_t crc;         /* CRC-32 of `data`; RSProx rejects a mismatch */
} MsgUpdateUid192;

/* v100 -- revs 227-241. */
void packet_update_uid192_v100_out(RsprotExec *x, MsgUpdateUid192 *m);

#define packet_update_uid192_rev227_out packet_update_uid192_v100_out
#define packet_update_uid192_rev228_out packet_update_uid192_v100_out
#define packet_update_uid192_rev229_out packet_update_uid192_v100_out
#define packet_update_uid192_rev230_out packet_update_uid192_v100_out
#define packet_update_uid192_rev231_out packet_update_uid192_v100_out
#define packet_update_uid192_rev232_out packet_update_uid192_v100_out
#define packet_update_uid192_rev233_out packet_update_uid192_v100_out
#define packet_update_uid192_rev234_out packet_update_uid192_v100_out
#define packet_update_uid192_rev235_out packet_update_uid192_v100_out
#define packet_update_uid192_rev236_out packet_update_uid192_v100_out
#define packet_update_uid192_rev237_out packet_update_uid192_v100_out
#define packet_update_uid192_rev238_out packet_update_uid192_v100_out
#define packet_update_uid192_rev239_out packet_update_uid192_v100_out
#define packet_update_uid192_rev240_out packet_update_uid192_v100_out
#define packet_update_uid192_rev241_out packet_update_uid192_v100_out

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_update_uid192_out[];
extern const int rsprot_update_uid192_out_count;

#endif
