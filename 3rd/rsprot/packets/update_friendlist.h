#ifndef RSPROT_PACKET_UPDATE_FRIENDLIST_H
#define RSPROT_PACKET_UPDATE_FRIENDLIST_H

/*
 * UPDATE_FRIENDLIST -- server -> client. Friend list entries.
 *
 * HAND-WRITTEN, and this is the case docs/RSPROT_CODEC_GENERATOR_PLAN.md §4
 * argued should never be generated.
 *
 * ## The discriminator Kotlin has and the wire does not
 *
 * RSProt writes:
 *
 *     if (friend is UpdateFriendList.OnlineFriend) {
 *         buffer.pjstr(friend.worldName); buffer.p1(friend.platform)
 *         buffer.p4(friend.worldFlags)
 *     }
 *
 * `is OnlineFriend` is a Kotlin TYPE test. Nothing on the wire says which
 * subclass an entry was, so a generator would have to infer the rule -- and a
 * wrong inference encodes correctly and decodes into the wrong shape, with the
 * three extra fields either swallowing the notes string or not being read at
 * all. Either way every entry after it is misaligned.
 *
 * The rule, taken from the client rather than guessed: an entry is online when
 * `world_id > 0`. `world_id` is transferred four fields earlier, so once it is
 * written down the branch is ordinary and legal -- `RSPROT_BRANCH` accepts it
 * because the field really has moved. What could not be automated is knowing
 * that `world_id > 0` is the rule; writing it here is stating a fact, not
 * teaching a generator to guess.
 *
 * ## No count
 *
 * The entry list is delimited by the payload. See RSPROT_MORE in rsprot_exec.h.
 */

#include "../include/rsprot_exec.h"

typedef struct MsgUpdateFriendListEntry
{
    int32_t added;
    const char *name;
    /** Empty string when the friend has no previous name; the wire always
     *  carries the field. */
    const char *previous_name;
    /** > 0 means online, and IS the discriminator for the three fields below. */
    int32_t world_id;
    int32_t rank;
    int32_t properties;
    /* Present only when world_id > 0. */
    const char *world_name;
    int32_t platform;
    int32_t world_flags;
    const char *notes;
} MsgUpdateFriendListEntry;

typedef struct MsgUpdateFriendList
{
    /** NOT on the wire; decoding discovers it. */
    int32_t count;
    MsgUpdateFriendListEntry *entries;
} MsgUpdateFriendList;

#define MSG_UPDATE_FRIENDLIST_MAX_ENTRIES 256

/* v1 -- revs 221-239. */
void packet_update_friendlist_v1_out(RsprotExec *x, MsgUpdateFriendList *m);

#define packet_update_friendlist_rev221_out packet_update_friendlist_v1_out
#define packet_update_friendlist_rev230_out packet_update_friendlist_v1_out
#define packet_update_friendlist_rev239_out packet_update_friendlist_v1_out

extern const RsprotVersionRange rsprot_update_friendlist_out[];
extern const int rsprot_update_friendlist_out_count;

#endif
