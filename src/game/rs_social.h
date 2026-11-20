#ifndef SRC_GAME_RS_SOCIAL_H
#define SRC_GAME_RS_SOCIAL_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Friends / ignores store (reference friendUsername/friendUserhash/friendNodeId
 * + ignoreUserhash + friendServerStatus). Filled by UPDATE_FRIENDLIST,
 * UPDATE_IGNORELIST and FRIENDLIST_LOADED; read back out by the CS2 host ops
 * (FRIEND_COUNT / FRIEND_GETNAME / FRIEND_GETWORLD / IGNORE_*) that
 * clientscripts 125 and 129 build every panel row from.
 *
 * Entries are keyed by the base-37 hash, not by the display string: the same
 * player reaches this store as "bob" from a typed prompt, as "Bob" from a chat
 * line and as "B\xa0o b" from a packet, and all three have to be one entry
 * (the reference's Username folds case, space, underscore, hyphen and NBSP).
 * `*_name[]` keeps the name as the wire (or the prompt) spells it, and that is
 * what the panels are handed: the rev-239 client shows a Username as it was
 * given (deob class7.method35 returns the string unchanged). A base-37 wire's
 * adapter writes the 2004 client's display form (RS_Social_DisplayName's rule)
 * into the packet, because that is how its client showed a name.
 */

#define RS_SOCIAL_FRIEND_MAX 200
#define RS_SOCIAL_IGNORE_MAX 100
#define RS_SOCIAL_NAME_LEN 64
/* The client's chain is a linked list with no bound; scripts build chains of
 * two or three. A step past this many is dropped like a step after a terminal
 * comparator is. */
#define RS_SOCIAL_SORT_CHAIN_MAX 16

/*
 * One comparator of a friend- or ignore-list sort chain (rev-239 class482
 * field5843). The FRIENDLIST_SORT_* / IGNORELIST_SORT_* opcodes append one
 * each; `_APPLY` sorts by the chain.
 *
 * Most comparators CHAIN: on a tie they defer to the next step. Three are
 * TERMINAL (legacy, name, last-world-change -- class647, class663, class125
 * implement Comparator directly rather than extending the chaining class466):
 * they never defer, and a step appended after one is silently dropped, as the
 * client drops it.
 */
enum RS_SocialSortKey
{
    RS_SOCIAL_SORT_LEGACY,                       /* 3629 / 3641, terminal: name */
    RS_SOCIAL_SORT_NAME,                         /* 3630 / 3642, terminal: name */
    RS_SOCIAL_SORT_WORLD,                        /* 3631: world id */
    RS_SOCIAL_SORT_LAST_WORLD_CHANGE,            /* 3632, terminal: change serial */
    RS_SOCIAL_SORT_ONLINE_STATUS,                /* 3633: online before offline */
    RS_SOCIAL_SORT_ONLINE_NAME,                  /* 3634: name, when both online */
    RS_SOCIAL_SORT_ONLINE_LAST_WORLD_CHANGE,     /* 3635: serial, when both online */
    RS_SOCIAL_SORT_ONLINE_WORLD,                 /* 3636: on our world first */
    RS_SOCIAL_SORT_OWN_WORLD_NAME,               /* 3637: name, when both on ours */
    /* 3638. The catalogue calls it friendlist_sort_ownworld_world, but class132
     * compares the world-change serial: two friends on our world have the
     * same world, so there would be nothing to compare. */
    RS_SOCIAL_SORT_OWN_WORLD_LAST_WORLD_CHANGE,
    RS_SOCIAL_SORT_RANK, /* 3656 friends / 3657 friends chat: rank, chains */
};

/* One list entry as a sort comparator sees it: friends and friends-chat
 * members share every comparator. */
struct RS_SocialSortEntry
{
    char const* name; /* the raw stored name */
    int world;        /* 0 = offline */
    int world_change; /* the change serial */
    int rank;
};

struct RS_SocialSortChain
{
    struct
    {
        enum RS_SocialSortKey key;
        bool ascending;
    } steps[RS_SOCIAL_SORT_CHAIN_MAX];
    int count;
};

/** Compare two entries down `chain`; `own_world` is this client's world. An
 *  empty chain is the client's natural order, by name. */
int
RS_Social_CompareEntries(
    struct RS_SocialSortChain const* chain,
    int own_world,
    struct RS_SocialSortEntry const* a,
    struct RS_SocialSortEntry const* b);

enum RS_SocialServerStatus
{
    RS_SOCIAL_SERVER_LOADING = 0,
    RS_SOCIAL_SERVER_CONNECTING = 1,
    RS_SOCIAL_SERVER_CONNECTED = 2,
};

/*
 * The private and chat-channel message dedupe ring (reference field1077): one
 * ring for every message kind, as in the client. A private message keys by its
 * id, a clan or friends-chat message by (world << 32) + its counter. A key is
 * "seen" when any slot holds it, and a slot never written holds 0, so a 0 key
 * reads as seen until every slot has been written once.
 */
#define RS_SOCIAL_MESSAGE_RING 100

struct RS_SocialMessageRing
{
    int64_t ids[RS_SOCIAL_MESSAGE_RING];
    /** The slot the next remembered key goes in. */
    int head;
};

struct RS_Social
{
    char friend_name[RS_SOCIAL_FRIEND_MAX][RS_SOCIAL_NAME_LEN];
    /** Base-37 key; 0 when the name did not pack (an entry that can never be
     *  matched, which is what an empty or garbage name gives). */
    int64_t friend_hash[RS_SOCIAL_FRIEND_MAX];
    /** World the friend is on; 0 = offline (reference friendNodeId). */
    int friend_world[RS_SOCIAL_FRIEND_MAX];
    /** When the world last changed, as a serial (rev-239 class471 field5670):
     *  the next value of `world_change_counter`, negated when the change was
     *  the first report putting the friend offline. The last-world-change
     *  sorts order by it. */
    int friend_world_change[RS_SOCIAL_FRIEND_MAX];
    /** Friends-chat rank (UPDATE_FRIENDLIST), what FRIEND_GETRANK answers;
     *  0 until the server says otherwise. */
    int friend_rank[RS_SOCIAL_FRIEND_MAX];
    /** The name the friend had before a rename, raw; "" when none. */
    char friend_previous_name[RS_SOCIAL_FRIEND_MAX][RS_SOCIAL_NAME_LEN];
    int friend_count;
    int world_change_counter;
    struct RS_SocialSortChain friend_sort;

    char ignore_name[RS_SOCIAL_IGNORE_MAX][RS_SOCIAL_NAME_LEN];
    int64_t ignore_hash[RS_SOCIAL_IGNORE_MAX];
    /** The name the ignored player had before a rename, raw; "" when none
     *  (IGNORE_GETNAME's second string). */
    char ignore_previous_name[RS_SOCIAL_IGNORE_MAX][RS_SOCIAL_NAME_LEN];
    int ignore_count;
    struct RS_SocialSortChain ignore_sort;

    int server_status; /* enum RS_SocialServerStatus */
    /** Our world id, for the green same-world highlight. */
    int node_id;

    /** Kept last: the replay checkpoint's "social" domain is every field
     *  above, and the ring is checkpointed on its own (app/app_checkpoint.c). */
    struct RS_SocialMessageRing message_ring;
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

/** Remove ignore `index` (0 <= index < ignore_count), closing the gap. */
void
RS_Social_DelIgnoreAt(struct RS_Social* social, int index);

/** The ignore list index of `name`, matched as RS_Social_IsIgnored matches
 *  it; -1 when `name` is not ignored. */
int
RS_Social_IgnoreIndex(struct RS_Social const* social, char const* name);

/** Give ignore `index` the name `name` (its key follows) and the previous
 *  name `previous_name` (class473.method10601: a rename keeps the entry). */
void
RS_Social_SetIgnoreNames(
    struct RS_Social* social,
    int index,
    char const* name,
    char const* previous_name);

/** Empty both lists and set the friend server back to LOADING, as the
 *  reference's game-state reset does at every login (deob Statics.method9200
 *  -> class58.method1166). The message ring and the sort chains stay. */
void
RS_Social_ResetForLogin(struct RS_Social* social);

/**
 * Set friend `index`'s world, recording the change serial when it differs.
 * Every world write goes through here so the last-world-change sorts see it.
 */
void
RS_Social_SetFriendWorld(struct RS_Social* social, int index, int world);

/** Empty a sort chain (FRIENDLIST_SORT_RESET / IGNORELIST_SORT_RESET). */
void
RS_Social_SortReset(struct RS_SocialSortChain* chain);

/** Append one comparator, or drop it after a terminal one (see the enum). */
void
RS_Social_SortAppend(
    struct RS_SocialSortChain* chain,
    enum RS_SocialSortKey key,
    bool ascending);

/** Stable-sort the friends by `friend_sort`; by name when it is empty. */
void
RS_Social_SortFriends(struct RS_Social* social);

/** Stable-sort the ignores by `ignore_sort`; by name when it is empty. */
void
RS_Social_SortIgnores(struct RS_Social* social);

/** True when `key` is in the message ring. Does not remember it. */
bool
RS_Social_MessageSeen(
    struct RS_Social const* social,
    int64_t key);

/** Put `key` in the ring's next slot, over the oldest. Does not check it. */
void
RS_Social_MessageRemember(
    struct RS_Social* social,
    int64_t key);

/**
 * True when `key` was already in the ring; otherwise remember it and return
 * false. The private-message path. The channel paths check, then may drop the
 * message for an ignored sender without remembering it, so they call
 * RS_Social_MessageSeen and RS_Social_MessageRemember apart.
 */
bool
RS_Social_MessageSeenOrRemember(
    struct RS_Social* social,
    int64_t key);

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

/** Friend `index`'s name as stored (the wire's spelling) into `out`; "" when
 *  out of range. */
void
RS_Social_FriendName(struct RS_Social const* social, int index, char* out, int cap);

/** Ignore `index`'s name as stored into `out`; "" when out of range. */
void
RS_Social_IgnoreName(struct RS_Social const* social, int index, char* out, int cap);

/** Friend `index`'s previous name as stored into `out`; "" when none or out
 *  of range. */
void
RS_Social_FriendPreviousName(struct RS_Social const* social, int index, char* out, int cap);

/** Ignore `index`'s previous name as stored into `out`; "" when none or out
 *  of range. */
void
RS_Social_IgnorePreviousName(struct RS_Social const* social, int index, char* out, int cap);

/** Rank of friend `index`; 0 out of range. */
int
RS_Social_FriendRank(struct RS_Social const* social, int index);

/** World of friend `index`; 0 = offline, which is what out-of-range gives. */
int
RS_Social_FriendWorld(struct RS_Social const* social, int index);

/** True when `name` (any case, spaces or underscores) is on the friend list. */
int
RS_Social_IsFriend(struct RS_Social const* social, char const* name);

/** The friend list index of `name`, matched as RS_Social_IsFriend matches it
 *  (the reference's Username: case, space, underscore and NBSP fold); -1 when
 *  `name` is not a friend. */
int
RS_Social_FriendIndex(struct RS_Social const* social, char const* name);

/** True when `name` is on the ignore list. */
int
RS_Social_IsIgnored(struct RS_Social const* social, char const* name);

/**
 * The 2004 reference's JString.toScreenName: underscores become spaces and the
 * first letter of each word is capitalised. For a name that only exists as a
 * base-37 key (the rs-family wires, a typed prompt's echo); never applied to a
 * name a rev-221+ wire spelled.
 */
void
RS_Social_DisplayName(char const* raw, char* out, int cap);

#endif
