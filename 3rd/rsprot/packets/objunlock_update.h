#ifndef RSPROT_PACKET_OBJUNLOCK_UPDATE_H
#define RSPROT_PACKET_OBJUNLOCK_UPDATE_H

/*
 * OBJUNLOCK_UPDATE -- server -> client.
 *
 * Hand-written from RSProx (ObjUnlockUpdateDecoder, protocol/osrs-<N>/.../decoder/codec/misc/client/ObjUnlockUpdateDecoder.kt).
 * RSProt has no codec for this prot, and the RSProx decoder loops to the end of the payload, which
 * tools/rsprox_gen_codec.py does not translate -- it lists this file in HAND and
 * never overwrites it. Field names are RSProx's. Version numbers are local to
 * this file and start at 100, the RSProx numbering (gen/rsprox_versions.txt).
 */

#include "../include/rsprot_exec.h"

#define MSG_OBJUNLOCK_UPDATE_MAX_ENTRIES 4096

/** A 64-bit word of unlock bits. Its word index is the running sum of `delta`
 *  over this and every earlier entry (RSProx: Math.addExact). */
typedef struct MsgObjUnlockEntry {
	int32_t delta;
	int64_t bits;
} MsgObjUnlockEntry;

typedef struct MsgObjunlockUpdate {
	int32_t entries_count; /* not on the wire: the payload ends the list */
	MsgObjUnlockEntry entries[MSG_OBJUNLOCK_UPDATE_MAX_ENTRIES];
} MsgObjunlockUpdate;

/* v100 -- revs 241. */
void packet_objunlock_update_v100_out(RsprotExec *x, MsgObjunlockUpdate *m);

#define packet_objunlock_update_rev241_out packet_objunlock_update_v100_out

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_objunlock_update_out[];
extern const int rsprot_objunlock_update_out_count;

#endif
