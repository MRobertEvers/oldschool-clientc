#ifndef SRC_GAME_RS_SOCIAL_H
#define SRC_GAME_RS_SOCIAL_H

#include <stdint.h>

/*
 * Friends / ignores store (reference friendUsername/friendUserhash/friendNodeId
 * + ignoreUserhash + friendServerStatus). Filled by UPDATE_FRIENDLIST,
 * UPDATE_IGNORELIST and FRIENDLIST_LOADED; read back out by the CS2 host ops
 * (FRIEND_COUNT / FRIEND_GETNAME / FRIEND_GETWORLD / IGNORE_*) that
 * clientscripts 125 and 129 build every panel row from.
 *
 * Entries are keyed by the base-37 hash, not by the display string, exactly as
 * the reference keys them: the same player reaches this store as "bob" from a
 * packet, as "Bob" from a chat line and as "B o b" from a typed prompt, and all
 * three have to be one entry. `*_name[]` keeps the raw unpacked form; the
 * display form (underscores to spaces, words capitalised) is produced on the
 * way out by RS_Social_DisplayName.
 */

#define RS_SOCIAL_FRIEND_MAX 200
#define RS_SOCIAL_IGNORE_MAX 100
#define RS_SOCIAL_NAME_LEN 64

enum RS_SocialServerStatus
{
    RS_SOCIAL_SERVER_LOADING = 0,
    RS_SOCIAL_SERVER_CONNECTING = 1,
    RS_SOCIAL_SERVER_CONNECTED = 2,
};

struct RS_Social
{
    char friend_name[RS_SOCIAL_FRIEND_MAX][RS_SOCIAL_NAME_LEN];
    /** Base-37 key; 0 when the name did not pack (an entry that can never be
     *  matched, which is what an empty or garbage name gives). */
    int64_t friend_hash[RS_SOCIAL_FRIEND_MAX];
    /** World the friend is on; 0 = offline (reference friendNodeId). */
    int friend_world[RS_SOCIAL_FRIEND_MAX];
    int friend_count;

    char ignore_name[RS_SOCIAL_IGNORE_MAX][RS_SOCIAL_NAME_LEN];
    int64_t ignore_hash[RS_SOCIAL_IGNORE_MAX];
    int ignore_count;

    int server_status; /* enum RS_SocialServerStatus */
    /** Our world id, for the green same-world highlight. */
    int node_id;
};

void
RS_Social_Init(struct RS_Social* social);

int
RS_Social_AddFriend(struct RS_Social* social, char const* name, int world);

int
RS_Social_DelFriend(struct RS_Social* social, char const* name);

int
RS_Social_AddIgnore(struct RS_Social* social, char const* name);

int
RS_Social_DelIgnore(struct RS_Social* social, char const* name);

/*
 * ---------------------------------------------------------------------------
 * Read side — what the CS2 host ops answer with.
 * ---------------------------------------------------------------------------
 */

/**
 * Friend count as clientscript 125 wants it: -1 while the friend server has not
 * said "loaded" (which renders "Loading friends list<br>Please wait..."), the
 * real count once it has.
 *
 * It must never answer negative after loading — script 681 refuses every list
 * mutation with "Unable to complete action - system busy." while the count is
 * below zero.
 */
int
RS_Social_FriendCount(struct RS_Social const* social);

/** Ignore count. Never negative: script 129 reads `< 0` as "loading" and script
 *  681 refuses ignore mutations on it. */
int
RS_Social_IgnoreCount(struct RS_Social const* social);

/** Display name of friend `index` into `out`; "" when out of range. */
void
RS_Social_FriendName(struct RS_Social const* social, int index, char* out, int cap);

/** Display name of ignore `index` into `out`; "" when out of range. */
void
RS_Social_IgnoreName(struct RS_Social const* social, int index, char* out, int cap);

/** World of friend `index`; 0 = offline, which is what out-of-range gives. */
int
RS_Social_FriendWorld(struct RS_Social const* social, int index);

/** True when `name` (any case, spaces or underscores) is on the friend list. */
int
RS_Social_IsFriend(struct RS_Social const* social, char const* name);

/** True when `name` is on the ignore list. */
int
RS_Social_IsIgnored(struct RS_Social const* social, char const* name);

/**
 * Reference JString.toScreenName: underscores become spaces and the first
 * letter of each word is capitalised. The store keeps the raw base-37 form, so
 * this is applied on the way out to anything a player reads.
 */
void
RS_Social_DisplayName(char const* raw, char* out, int cap);

#endif
