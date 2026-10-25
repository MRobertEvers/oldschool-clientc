#ifndef RSPROT_PACKET_GROUP_VAR_H
#define RSPROT_PACKET_GROUP_VAR_H

/*
 * GROUP_VAR -- server -> client.
 *
 * Hand-written from RSProx (GroupVarDecoder, protocol/osrs-<N>/.../decoder/codec/group/GroupVarDecoder.kt).
 * RSProt has no codec for this prot, and the RSProx decoder loops to the end of the payload, branching on a type packed into a field, which
 * tools/rsprox_gen_codec.py does not translate -- it lists this file in HAND and
 * never overwrites it. Field names are RSProx's. Version numbers are local to
 * this file and start at 100, the RSProx numbering (gen/rsprox_versions.txt).
 */

#include "../include/rsprot_exec.h"

#define MSG_GROUP_VAR_MAX_UPDATES 256

/**
 * One group variable. Its base type is bits 16-17 of `packed`
 * (GroupVarUpdate: 0 int, 1 long, 2 string); any other type carries the rest of
 * the payload as `data`.
 */
typedef struct MsgGroupVarUpdate {
	int32_t index;
	int32_t packed;
	int32_t int_value;
	int64_t long_value;
	const char *string_value;
	const uint8_t *data;
	int32_t data_length;
} MsgGroupVarUpdate;

typedef struct MsgGroupVar {
	int32_t updates_count; /* not on the wire: the payload ends the list */
	MsgGroupVarUpdate updates[MSG_GROUP_VAR_MAX_UPDATES];
} MsgGroupVar;

/* v100 -- revs 239-241. */
void packet_group_var_v100_out(RsprotExec *x, MsgGroupVar *m);

#define packet_group_var_rev239_out packet_group_var_v100_out
#define packet_group_var_rev240_out packet_group_var_v100_out
#define packet_group_var_rev241_out packet_group_var_v100_out

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_group_var_out[];
extern const int rsprot_group_var_out_count;

#endif
