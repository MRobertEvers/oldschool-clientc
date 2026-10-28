#ifndef RSPROT_PACKET_UPDATE_IGNORELIST_H
#define RSPROT_PACKET_UPDATE_IGNORELIST_H

/*
 * UPDATE_IGNORELIST -- server -> client. Ignore list entries.
 *
 * HAND-WRITTEN, and the second of the two sealed-`when` packets
 * docs/RSPROT_CODEC_GENERATOR_PLAN.md §4 argued against generating.
 *
 * ## The tag exists; Kotlin just does not spell it as a field
 *
 * RSProt writes:
 *
 *     when (ignore) {
 *         is AddedIgnoredEntry   -> { p1(if (added) 0x1 else 0); pjstr(name)
 *                                     pjstr(previousName ?: ""); pjstr(note) }
 *         is RemovedIgnoredEntry -> { p1(0x4); pjstr(name) }
 *     }
 *
 * Both arms open with a p1, and its VALUE is the discriminator: 0x4 means
 * removed, anything else means added and its low bit is the `added` flag. A
 * generator would have to derive that from two arms' leading constants, which
 * is inference; here it is one transferred field with the rule beside it.
 *
 * Modelled as a real field rather than reconstructed, so a caller sets `flags`
 * and the codec is a plain branch. `RSPROT_BRANCH` accepts it because the field
 * genuinely moved first -- the encoder's arms just happened to write it twice.
 */

#include "../include/rsprot_exec.h"

/** Removal bit. Set means "no longer ignored" and the entry stops after the
 *  name; clear means an add, whose low bit carries `added`. */
#define MSG_UPDATE_IGNORELIST_REMOVED 0x4

typedef struct MsgUpdateIgnoreListEntry
{
    /** The discriminator AND the payload of an add. See the bit above. */
    int32_t flags;
    const char *name;
    /* Present only when MSG_UPDATE_IGNORELIST_REMOVED is clear. */
    const char *previous_name;
    const char *note;
} MsgUpdateIgnoreListEntry;

typedef struct MsgUpdateIgnoreList
{
    /** NOT on the wire; decoding discovers it. */
    int32_t count;
    MsgUpdateIgnoreListEntry *entries;
} MsgUpdateIgnoreList;

#define MSG_UPDATE_IGNORELIST_MAX_ENTRIES 256

/* v1 -- revs 221-239. */
void packet_update_ignorelist_v1_out(RsprotExec *x, MsgUpdateIgnoreList *m);

#define packet_update_ignorelist_osrs221_out packet_update_ignorelist_v1_out
#define packet_update_ignorelist_osrs222_out packet_update_ignorelist_v1_out
#define packet_update_ignorelist_osrs223_out packet_update_ignorelist_v1_out
#define packet_update_ignorelist_osrs224_out packet_update_ignorelist_v1_out
#define packet_update_ignorelist_osrs225_out packet_update_ignorelist_v1_out
#define packet_update_ignorelist_osrs226_out packet_update_ignorelist_v1_out
#define packet_update_ignorelist_osrs227_out packet_update_ignorelist_v1_out
#define packet_update_ignorelist_osrs228_out packet_update_ignorelist_v1_out
#define packet_update_ignorelist_osrs229_out packet_update_ignorelist_v1_out
#define packet_update_ignorelist_osrs230_out packet_update_ignorelist_v1_out
#define packet_update_ignorelist_osrs231_out packet_update_ignorelist_v1_out
#define packet_update_ignorelist_osrs232_out packet_update_ignorelist_v1_out
#define packet_update_ignorelist_osrs233_out packet_update_ignorelist_v1_out
#define packet_update_ignorelist_osrs234_out packet_update_ignorelist_v1_out
#define packet_update_ignorelist_osrs235_out packet_update_ignorelist_v1_out
#define packet_update_ignorelist_osrs236_out packet_update_ignorelist_v1_out
#define packet_update_ignorelist_osrs237_out packet_update_ignorelist_v1_out
#define packet_update_ignorelist_osrs238_out packet_update_ignorelist_v1_out
#define packet_update_ignorelist_osrs239_out packet_update_ignorelist_v1_out
#define packet_update_ignorelist_osrs240_out packet_update_ignorelist_v1_out
#define packet_update_ignorelist_osrs241_out packet_update_ignorelist_v1_out
#define packet_update_ignorelist_osrsm241_out packet_update_ignorelist_v1_out

extern const RsprotVersionRange rsprot_update_ignorelist_out[];
extern const int rsprot_update_ignorelist_out_count;

#endif
