#ifndef RSPROT_STORAGE_H
#define RSPROT_STORAGE_H

/*
 * Element storage for the codecs that take their arrays from the CALLER.
 *
 * A handful of packets/ codecs (IF_RESYNC*, IF_INITIALSTATE, LOC_ADD_CHANGE_V2,
 * the friend / ignore lists, the two inventory updates, RUNCLIENTSCRIPT) write
 * their lists through pointers in the message rather than into it -- a host points
 * them at its own buffers. Anything else that runs a codec over a
 * zeroed message (the codec sweep, the recording check's validate) would write
 * through NULL, so this points them at generous pools, by prot name.
 *
 * Two independent pools, `side` 0 and 1, for a caller that holds two messages
 * at once (an encode and the decode it is compared with).
 */

/** Point `msg`'s caller-owned arrays at pool `side`, when `prot` has any. */
void
rsprot_storage_provide(char const* prot, void* msg, int side);

#endif
