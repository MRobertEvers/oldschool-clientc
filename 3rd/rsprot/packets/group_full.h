#ifndef RSPROT_PACKET_GROUP_FULL_H
#define RSPROT_PACKET_GROUP_FULL_H

/*
 * GROUP_FULL -- server -> client.
 *
 * Hand-written from RSProx (GroupFullDecoder, protocol/osrs-<N>/.../decoder/codec/group/GroupFullDecoder.kt).
 * RSProt has no codec for this prot, and the RSProx decoder loops to the end of the payload, and its last entry is a blob of everything left, which
 * tools/rsprox_gen_codec.py does not translate -- it lists this file in HAND and
 * never overwrites it. Field names are RSProx's. Version numbers are local to
 * this file and start at 100, the RSProx numbering (gen/rsprox_versions.txt).
 */

#include "../include/rsprot_exec.h"

#define MSG_GROUP_FULL_MAX_UPDATES 64

/** `id` -1 deletes the group at `index`; otherwise it adds or changes it, and
 *  `data` is every byte after `uid` to the end of the payload (which is why an
 *  add can only be the last entry). */
typedef struct MsgGroupFullUpdate {
	int32_t index;
	int32_t id;
	int64_t uid;
	const uint8_t *data;
	int32_t data_length;
} MsgGroupFullUpdate;

typedef struct MsgGroupFull {
	int32_t updates_count; /* not on the wire: the payload ends the list */
	MsgGroupFullUpdate updates[MSG_GROUP_FULL_MAX_UPDATES];
} MsgGroupFull;

/* v100 -- revs 239-241. */
void packet_group_full_v100_out(RsprotExec *x, MsgGroupFull *m);

#define packet_group_full_rev239_out packet_group_full_v100_out
#define packet_group_full_rev240_out packet_group_full_v100_out
#define packet_group_full_rev241_out packet_group_full_v100_out

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_group_full_out[];
extern const int rsprot_group_full_out_count;

#endif
