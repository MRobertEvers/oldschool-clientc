#ifndef SRC_GAME_RS_FRIENDS_CHAT_H
#define SRC_GAME_RS_FRIENDS_CHAT_H

/*
 * The friends chat (the legacy clan chat): the channel the player is in, its
 * owner and kick rank, and who is in it.
 *
 * Mirrors the rev-239 client's class485 (Statics.field2935): filled by
 * UPDATE_FRIENDCHAT_CHANNEL_FULL_V2 and UPDATE_FRIENDCHAT_CHANNEL_SINGLEUSER,
 * read by the 3611-3627 clientscript commands and ordered by the 3644-3657 sort
 * chain. `in_channel` false is the client's null channel.
 */

#include "game/rs_social.h"

#include <stdbool.h>
#include <stdint.h>

#define RS_FRIENDS_CHAT_CAPACITY 500
#define RS_FRIENDS_CHAT_NAME_LEN 64

struct RS_FriendsChatMember
{
    char name[RS_FRIENDS_CHAT_NAME_LEN]; /* as sent */
    int world;
    int rank;         /* signed byte */
    int world_change; /* add-order serial, larger = more recent */
};

struct RS_FriendsChat
{
    bool in_channel;
    char owner[RS_FRIENDS_CHAT_NAME_LEN];
    bool has_owner;
    char display_name[RS_FRIENDS_CHAT_NAME_LEN];
    int kick_rank;  /* signed byte */
    /* The local player's own rank, set when their entry arrives and never
     * reset, as in the client. */
    int local_rank;
    int serial;
    struct RS_FriendsChatMember members[RS_FRIENDS_CHAT_CAPACITY];
    int member_count;
    struct RS_SocialSortChain sort;
    /* Bumped by both channel packets: onclantransmit. */
    int transmit_serial;
};

void
RS_FriendsChat_Init(struct RS_FriendsChat* chat);

/** UPDATE_FRIENDCHAT_CHANNEL_FULL_V2. An empty payload is a leave.
 *  `local_name` is the local player's name, for their own rank. */
bool
RS_FriendsChat_ApplyFull(
    struct RS_FriendsChat* chat,
    uint8_t const* data,
    int length,
    char const* local_name);

/** UPDATE_FRIENDCHAT_CHANNEL_SINGLEUSER. */
bool
RS_FriendsChat_ApplySingleUser(
    struct RS_FriendsChat* chat,
    uint8_t const* data,
    int length,
    char const* local_name);

/** friendschat_sort_apply: stable sort by the chain, by name when empty. */
void
RS_FriendsChat_Sort(
    struct RS_FriendsChat* chat,
    int own_world);

/** True when `a` and `b` name the same player (case and space/underscore
 *  folded). */
bool
RS_FriendsChat_NamesEqual(char const* a, char const* b);

#endif
