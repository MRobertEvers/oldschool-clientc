#ifndef RSPROT_PACKET_GROUP_FULL_H
#define RSPROT_PACKET_GROUP_FULL_H

/*
 * GROUP_FULL -- server -> client.
 *
 * Hand-written from RSProx (GroupFullDecoder, protocol/osrs-<N>/.../decoder/codec/group/GroupFullDecoder.kt).
 * RSProt has no codec for this prot, and the RSProx decoder loops to the end of the payload, and its last entry is a blob of everything left, so it is written by hand: tools/rsprot_gen.py has no spec for it.
 * Field names are RSProx's; version numbers are local to this file.
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

/* v1 -- revs 239-241. */
void packet_group_full_v1_out(RsprotExec *x, MsgGroupFull *m);

#define packet_group_full_osrs239_out packet_group_full_v1_out
#define packet_group_full_osrs240_out packet_group_full_v1_out
#define packet_group_full_osrs241_out packet_group_full_v1_out

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_group_full_out[];
extern const int rsprot_group_full_out_count;

#endif
