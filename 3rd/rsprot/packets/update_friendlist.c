/* UPDATE_FRIENDLIST. Hand-written -- see update_friendlist.h for why. */
#include "update_friendlist.h"

/*
 * Transcribed from RSProt:
 *   protocol/osrs-239/.../game/outgoing/codec/social/UpdateFriendListEncoder.kt
 *
 *     for (friend in message.friends) {
 *         buffer.p1(if (friend.added) 1 else 0)
 *         buffer.pjstr(friend.name)
 *         buffer.pjstr(friend.previousName ?: "")
 *         buffer.p2(friend.worldId)
 *         buffer.p1(friend.rank)
 *         buffer.p1(friend.properties)
 *         if (friend is UpdateFriendList.OnlineFriend) {
 *             buffer.pjstr(friend.worldName)
 *             buffer.p1(friend.platform)
 *             buffer.p4(friend.worldFlags)
 *         }
 *         buffer.pjstr(friend.notes)
 *     }
 *
 * The `is OnlineFriend` test becomes `world_id > 0`, which is what
 * src/net/rev/osrs239/osrs239_parse.c has always used to decide whether to read
 * the three extra fields. Same rule, now stated where the layout is.
 */

void
packet_update_friendlist_v1_out(RsprotExec *x, MsgUpdateFriendList *m)
{
    for (int32_t i = 0;
         RSPROT_MORE(x, m->count, i, MSG_UPDATE_FRIENDLIST_MAX_ENTRIES);
         i++) {
        MsgUpdateFriendListEntry *e = &m->entries[i];

        RSPROT_BOOL(x, e->added);
        RSPROT_JSTR(x, e->name);
        RSPROT_JSTR(x, e->previous_name);
        RSPROT_U2(x, e->world_id);
        RSPROT_U1(x, e->rank);
        RSPROT_U1(x, e->properties);
        /* Online-only tail. `world_id` moved three fields ago, so this is an
         * ordinary branch on a transferred field rather than a type test. */
        if (RSPROT_BRANCH(x, e->world_id) > 0) {
            RSPROT_JSTR(x, e->world_name);
            RSPROT_U1(x, e->platform);
            RSPROT_U4(x, e->world_flags);
        }
        RSPROT_JSTR(x, e->notes);
    }
}

const RsprotVersionRange rsprot_update_friendlist_out[] = {
    { 221, 239, (RsprotCodecFn)packet_update_friendlist_v1_out },
};

const int rsprot_update_friendlist_out_count =
    (int)(sizeof(rsprot_update_friendlist_out) / sizeof(rsprot_update_friendlist_out[0]));
