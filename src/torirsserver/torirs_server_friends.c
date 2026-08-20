#include "mock230_friends.h"
#include <assert.h>

#include "mock230.h"
#include "mock230_content.h"
#include "net/jbase37.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * See mock230_friends.h for what this is, what the reference does, and why
 * there is no persistence. This file is the mechanism only: it holds state,
 * answers questions and applies the reference's rules. It writes no packet and
 * knows no opcode — every caller is expected to ask what changed and encode it
 * itself, which is the same split the reference has between
 * FriendServerRepository (rules) and FriendServer (wire).
 *
 * It also emits no player-facing string, because the reference emits none on
 * this path: every failure below is silent, exactly as `addFriend` returning
 * bare is silent.
 */

/*
 * The world this server is.
 *
 * `UPDATE_FRIENDLIST` carries one byte per friend meaning "0 = offline,
 * non-zero = the world they are on", and the client draws "World N" from it.
 * The reference reads the number from `Environment.NODE_ID`, i.e. from
 * deployment config, not from content — a world number identifies a *server
 * instance*, and no content file could state it or be checked against it. One
 * process here is one world, so it is a constant, and the client's own
 * `RS_Social.node_id` is already 1 to match.
 */
enum
{
    MOCK230_WORLD_ID = 1,
};

struct Mock230SocialEntry
{
    /** 0 means the slot is free. A valid base-37 name is never 0 —
     *  `base37tostr` calls 0 `invalid_name`. */
    int64_t name37;

    /*
     * Both lists are grown on demand rather than sized at the ceiling. Most
     * entries in a live roster are *followers* — names somebody else listed,
     * with empty lists of their own — so a flat `int64_t[256]` per entry would
     * be megabytes of zeroes, and this module is linked into the client under
     * EMBED_SERVER=1.
     */
    int64_t* friends;
    int friend_count;
    int friend_capacity;

    int64_t* ignores;
    int ignore_count;
    int ignore_capacity;

    /** 0 = offline. Non-zero = MOCK230_WORLD_ID. */
    int world;

    int public_mode;
    int private_mode;
    int trade_mode;

    /** `> 1` is the reference's threshold for "staff", and staff see through
     *  everyone's privacy settings. Nothing in this server sets it above 0 yet;
     *  it is here because `isVisibleTo`'s first line reads it, and dropping the
     *  line would be dropping a rule rather than not having a way to reach it. */
    int staff_level;
};

static struct Mock230SocialEntry g_roster[MOCK230_SOCIAL_ROSTER_MAX];
static int g_roster_count;

/** Message ids start at 1 and never reach 0 again — see the header. */
static int32_t g_pm_counter = 1;

static int g_caps_resolved;
static int g_cap_friends;
static int g_cap_ignores;
static int g_cap_pm_bytes;

/* ------------------------------------------------------------------ */
/* Names                                                               */
/* ------------------------------------------------------------------ */

/*
 * The reference's `fromBase37(name) === 'invalid_name'` guard, asked through
 * this tree's own codec so the two halves cannot disagree about what a name is.
 *
 * Every handler in the reference checks this before doing anything
 * (FriendListAddHandler.ts:9 and its three siblings), because the name arrives
 * as eight attacker-controlled bytes.
 */
static int
name37_is_valid(int64_t name37)
{
    char buffer[32];

    if( name37 <= 0 )
        return 0;
    base37tostr((uint64_t)name37, buffer, (int)sizeof(buffer));
    return strcmp(buffer, "invalid_name") != 0;
}

/* ------------------------------------------------------------------ */
/* The roster                                                          */
/* ------------------------------------------------------------------ */

static struct Mock230SocialEntry*
entry_find(int64_t name37)
{
    for( int i = 0; i < g_roster_count; i++ )
        if( g_roster[i].name37 == name37 )
            return &g_roster[i];
    return NULL;
}

/*
 * Find or create. Creating on *any* mention — being added to someone's list
 * counts — is what makes an offline player enumerable: `getFollowers` walks
 * entries, so a name with no entry has no followers and cannot be told anything
 * when it logs in.
 *
 * Entries are never recycled. A long-lived process eventually runs out and says
 * so; recycling would silently forget a list somebody is still holding, which
 * is the worse failure.
 */
static struct Mock230SocialEntry*
entry_get(int64_t name37)
{
    struct Mock230SocialEntry* entry = entry_find(name37);

    if( entry )
        return entry;
    if( g_roster_count >= MOCK230_SOCIAL_ROSTER_MAX )
    {
        char name[32];

        base37tostr((uint64_t)name37, name, (int)sizeof(name));
        fprintf(stderr,
                "mock230: the friend roster is full (%d names); `%s` cannot be "
                "tracked\n",
                MOCK230_SOCIAL_ROSTER_MAX, name);
        return NULL;
    }
    entry = &g_roster[g_roster_count++];
    memset(entry, 0, sizeof(*entry));
    entry->name37 = name37;
    /*
     * The reference's default for a player it has never heard of, and the
     * reason `isVisibleTo` hides an unknown player: `privateChatByPlayer[x] ??
     * ChatModePrivate.OFF` (FriendServerRepository.ts:344).
     */
    entry->private_mode = MOCK230_CHAT_PRIVATE_OFF;
    return entry;
}

static int
list_index_of(const int64_t* list, int count, int64_t name37)
{
    for( int i = 0; i < count; i++ )
        if( list[i] == name37 )
            return i;
    return -1;
}

/* ------------------------------------------------------------------ */
/* Caps                                                                */
/* ------------------------------------------------------------------ */

/*
 * One content constant, or the storage ceiling.
 *
 * `mock230_content_constant_int` is deliberately not used: it reports a missing
 * constant through the *content error count*, which `mock230_pack` fails on —
 * and these three constants do not exist in the content tree yet (this lane
 * cannot write it). Failing the tree's content gate on behalf of a file nobody
 * has written would break every other lane's build, so this reads the raw text
 * and reports for itself.
 */
static int
cap_resolve(const char* name, int ceiling, int* out_missing)
{
    const char* text = mock230_content_constant(name);
    char* end;
    long value;

    if( !text )
    {
        *out_missing += 1;
        return ceiling;
    }
    value = strtol(text, &end, 10);
    while( *end == ' ' || *end == '\t' )
        end++;
    if( end == text || *end || value <= 0 )
    {
        fprintf(stderr, "mock230: `^%s` is `%s`, which is not a positive number\n", name,
                text);
        *out_missing += 1;
        return ceiling;
    }
    if( value > ceiling )
    {
        /* Not clamped silently: content asked for more than the array holds,
         * and the array is the thing that has to grow. */
        fprintf(stderr,
                "mock230: `^%s` is %ld but this server stores at most %d; "
                "raise MOCK230_SOCIAL_* in mock230_friends.h\n",
                name, value, ceiling);
        return ceiling;
    }
    return (int)value;
}

static void
caps_resolve(void)
{
    int missing = 0;

    if( g_caps_resolved )
        return;
    g_caps_resolved = 1;

    g_cap_friends = cap_resolve("friend_max", MOCK230_SOCIAL_FRIENDS_MAX, &missing);
    g_cap_ignores = cap_resolve("ignore_max", MOCK230_SOCIAL_IGNORES_MAX, &missing);
    /*
     * The reference caps the *decoded* private message at 100 characters
     * (MessagePrivateHandler.ts:13). The ceiling used when content is silent is
     * the wire's own: a var-u8 packet cannot carry more than 255 bytes, so
     * "as much as fits" is a real answer rather than an invented one.
     */
    g_cap_pm_bytes = cap_resolve("private_message_max_bytes", 255, &missing);

    if( missing )
        fprintf(stderr,
                "mock230: %d friend/ignore cap(s) are not in any .constant — "
                "the lists are limited only by this server's array sizes. "
                "Content owns these: see docs/FRIENDS_PRIVATE_CHAT.md §2.3.\n",
                missing);
}

int
mock230_friends_cap_friends(void)
{
    caps_resolve();
    return g_cap_friends;
}

int
mock230_friends_cap_ignores(void)
{
    caps_resolve();
    return g_cap_ignores;
}

int
mock230_friends_cap_pm_bytes(void)
{
    caps_resolve();
    return g_cap_pm_bytes;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

void
mock230_friends_reset(void)
{
    for( int i = 0; i < g_roster_count; i++ )
    {
        free(g_roster[i].friends);
        free(g_roster[i].ignores);
    }
    memset(g_roster, 0, sizeof(g_roster));
    g_roster_count = 0;
    g_pm_counter = 1;
    g_caps_resolved = 0;
}

/* ------------------------------------------------------------------ */
/* Presence                                                            */
/* ------------------------------------------------------------------ */

/** The reference clamps an out-of-range mode to ON rather than refusing the
 *  login (FriendServer.ts:120-123). */
static int
clamp_private_mode(int mode)
{
    if( mode != MOCK230_CHAT_PRIVATE_ON && mode != MOCK230_CHAT_PRIVATE_FRIENDS &&
        mode != MOCK230_CHAT_PRIVATE_OFF )
        return MOCK230_CHAT_PRIVATE_ON;
    return mode;
}

int
mock230_friends_login(
    int64_t name37,
    int public_mode,
    int private_mode,
    int trade_mode,
    int staff_level)
{
    static int announced;
    struct Mock230SocialEntry* entry;

    if( !name37_is_valid(name37) )
        return 0;
    entry = entry_get(name37);
    if( !entry )
        return 0;

    entry->world = MOCK230_WORLD_ID;
    entry->public_mode = public_mode;
    entry->private_mode = clamp_private_mode(private_mode);
    entry->trade_mode = trade_mode;
    entry->staff_level = staff_level;

    if( !announced && getenv("MOCK230_VERBOSE") )
    {
        announced = 1;
        fprintf(stderr,
                "mock230: friend and ignore lists are in-memory only; a restart "
                "loses them (docs/FRIENDS_PRIVATE_CHAT.md §6)\n");
    }
    return 1;
}

void
mock230_friends_logout(int64_t name37)
{
    struct Mock230SocialEntry* entry = entry_find(name37);

    if( !entry )
        return;
    /* Only presence goes. The lists stay: they are what the *other* players'
     * panels are drawn from, and with no database there is nothing to reload
     * them from on the next login. */
    entry->world = 0;
}

int
mock230_friends_world(int64_t name37)
{
    const struct Mock230SocialEntry* entry = entry_find(name37);

    return entry ? entry->world : 0;
}

void
mock230_friends_set_chat_modes(
    int64_t name37,
    int public_mode,
    int private_mode,
    int trade_mode)
{
    struct Mock230SocialEntry* entry;

    if( !name37_is_valid(name37) )
        return;
    entry = entry_get(name37);
    if( !entry )
        return;
    entry->public_mode = public_mode;
    entry->private_mode = clamp_private_mode(private_mode);
    entry->trade_mode = trade_mode;
}

void
mock230_friends_chat_modes(
    int64_t name37,
    int* out_public,
    int* out_private,
    int* out_trade)
{
    const struct Mock230SocialEntry* entry = entry_find(name37);

    if( out_public )
        *out_public = entry ? entry->public_mode : 0;
    if( out_private )
        *out_private = entry ? entry->private_mode : MOCK230_CHAT_PRIVATE_OFF;
    if( out_trade )
        *out_trade = entry ? entry->trade_mode : 0;
}

/* ------------------------------------------------------------------ */
/* Mutation                                                            */
/* ------------------------------------------------------------------ */

/*
 * The four mutations are two functions used twice: a friend list and an ignore
 * list differ only in which array and which cap. Writing them out four times is
 * how the two halves come to disagree about the invalid-name check.
 */

/*
 * Both names checked, both entries present, the owner's returned.
 *
 * The *target* gets an entry too, even though nothing is written to it. That is
 * what makes `getFollowers` work while the target is offline, and it is how the
 * reference behaves for free: its `playerFriends` table is keyed by the owner
 * and read by scanning, so a target with no row is simply a row with no
 * friends.
 */
static struct Mock230SocialEntry*
mutation_owner(
    int64_t name37,
    int64_t target37,
    enum Mock230SocialResult* out_result)
{
    struct Mock230SocialEntry* entry;

    if( !name37_is_valid(name37) || !name37_is_valid(target37) )
    {
        *out_result = MOCK230_SOCIAL_INVALID_NAME;
        return NULL;
    }
    if( !entry_get(target37) || !(entry = entry_get(name37)) )
    {
        *out_result = MOCK230_SOCIAL_NO_ROOM;
        return NULL;
    }
    *out_result = MOCK230_SOCIAL_OK;
    return entry;
}

static enum Mock230SocialResult
list_add(
    int64_t** list,
    int* count,
    int* capacity,
    int cap,
    int64_t target37)
{
    if( list_index_of(*list, *count, target37) >= 0 )
        return MOCK230_SOCIAL_UNCHANGED;
    if( *count >= cap )
        return MOCK230_SOCIAL_FULL;
    if( *count >= *capacity )
    {
        int wanted = *capacity ? *capacity * 2 : 8;
        int64_t* grown;

        if( wanted > cap )
            wanted = cap;
        /* The cap check above already guarantees this, and the guard is here
         * anyway: clamping the growth to the cap means a cap that ever stopped
         * bounding the count would turn into a write past the end. */
        if( wanted <= *count )
            return MOCK230_SOCIAL_FULL;
        grown = (int64_t*)realloc(*list, (size_t)wanted * sizeof(**list));
        assert(grown);
        *list = grown;
        *capacity = wanted;
    }
    (*list)[(*count)++] = target37;
    return MOCK230_SOCIAL_OK;
}

static enum Mock230SocialResult
list_del(int64_t* list, int* count, int64_t target37)
{
    int index = list_index_of(list, *count, target37);

    if( index < 0 )
        return MOCK230_SOCIAL_UNCHANGED;
    /* Order is the list's meaning — the reference loads it `orderBy created`
     * and the client draws it in the order it arrives — so this closes the gap
     * rather than swapping the tail in. */
    memmove(&list[index], &list[index + 1],
            (size_t)(*count - index - 1) * sizeof(list[0]));
    (*count)--;
    return MOCK230_SOCIAL_OK;
}

/** A delete needs no entry created for either side: nothing to remove from a
 *  list that does not exist. */
static struct Mock230SocialEntry*
deletion_owner(
    int64_t name37,
    int64_t target37,
    enum Mock230SocialResult* out_result)
{
    struct Mock230SocialEntry* entry;

    if( !name37_is_valid(name37) || !name37_is_valid(target37) )
    {
        *out_result = MOCK230_SOCIAL_INVALID_NAME;
        return NULL;
    }
    entry = entry_find(name37);
    if( !entry )
    {
        *out_result = MOCK230_SOCIAL_UNCHANGED;
        return NULL;
    }
    *out_result = MOCK230_SOCIAL_OK;
    return entry;
}

enum Mock230SocialResult
mock230_friends_add(int64_t name37, int64_t target37)
{
    enum Mock230SocialResult result;
    struct Mock230SocialEntry* entry = mutation_owner(name37, target37, &result);

    if( !entry )
        return result;
    return list_add(&entry->friends, &entry->friend_count, &entry->friend_capacity,
                    mock230_friends_cap_friends(), target37);
}

enum Mock230SocialResult
mock230_friends_del(int64_t name37, int64_t target37)
{
    enum Mock230SocialResult result;
    struct Mock230SocialEntry* entry = deletion_owner(name37, target37, &result);

    if( !entry )
        return result;
    return list_del(entry->friends, &entry->friend_count, target37);
}

enum Mock230SocialResult
mock230_friends_ignore_add(int64_t name37, int64_t target37)
{
    enum Mock230SocialResult result;
    struct Mock230SocialEntry* entry = mutation_owner(name37, target37, &result);

    if( !entry )
        return result;
    return list_add(&entry->ignores, &entry->ignore_count, &entry->ignore_capacity,
                    mock230_friends_cap_ignores(), target37);
}

enum Mock230SocialResult
mock230_friends_ignore_del(int64_t name37, int64_t target37)
{
    enum Mock230SocialResult result;
    struct Mock230SocialEntry* entry = deletion_owner(name37, target37, &result);

    if( !entry )
        return result;
    return list_del(entry->ignores, &entry->ignore_count, target37);
}

/* ------------------------------------------------------------------ */
/* Queries                                                             */
/* ------------------------------------------------------------------ */

int
mock230_friends_count(int64_t name37)
{
    const struct Mock230SocialEntry* entry = entry_find(name37);

    return entry ? entry->friend_count : 0;
}

int
mock230_friends_get(
    int64_t name37,
    int index,
    int64_t* out_name37,
    int* out_world)
{
    const struct Mock230SocialEntry* entry = entry_find(name37);
    int64_t friend37;

    if( !entry || index < 0 || index >= entry->friend_count )
        return 0;
    friend37 = entry->friends[index];
    if( out_name37 )
        *out_name37 = friend37;
    if( out_world )
        *out_world = mock230_friends_visible_to(name37, friend37)
                         ? mock230_friends_world(friend37)
                         : 0;
    return 1;
}

int
mock230_friends_ignore_count(int64_t name37)
{
    const struct Mock230SocialEntry* entry = entry_find(name37);

    return entry ? entry->ignore_count : 0;
}

int
mock230_friends_ignore_get(int64_t name37, int index, int64_t* out_name37)
{
    const struct Mock230SocialEntry* entry = entry_find(name37);

    if( !entry || index < 0 || index >= entry->ignore_count )
        return 0;
    if( out_name37 )
        *out_name37 = entry->ignores[index];
    return 1;
}

int
mock230_friends_is_friend(int64_t name37, int64_t other37)
{
    const struct Mock230SocialEntry* entry = entry_find(name37);

    return entry && list_index_of(entry->friends, entry->friend_count, other37) >= 0;
}

int
mock230_friends_is_ignored(int64_t name37, int64_t other37)
{
    const struct Mock230SocialEntry* entry = entry_find(name37);

    return entry && list_index_of(entry->ignores, entry->ignore_count, other37) >= 0;
}

int
mock230_friends_visible_to(int64_t viewer37, int64_t other37)
{
    const struct Mock230SocialEntry* viewer = entry_find(viewer37);
    const struct Mock230SocialEntry* other = entry_find(other37);

    /* FriendServerRepository.ts:332-355, in its own order — every line of it is
     * a rule and the order is the rule. */

    /* Staff see everyone, whatever anyone has set. */
    if( viewer && viewer->staff_level > 1 )
        return 1;

    /* Being ignored by the other player hides them from you, regardless of
     * their chat mode and of whether you are their friend. */
    if( other && list_index_of(other->ignores, other->ignore_count, viewer37) >= 0 )
        return 0;

    /* A player the service has never heard of defaults to OFF. */
    if( !other || other->private_mode == MOCK230_CHAT_PRIVATE_OFF )
        return 0;

    if( other->private_mode == MOCK230_CHAT_PRIVATE_FRIENDS )
        return list_index_of(other->friends, other->friend_count, viewer37) >= 0;

    return 1;
}

int
mock230_friends_followers(int64_t name37, int64_t* out, int max)
{
    int found = 0;

    for( int i = 0; i < g_roster_count; i++ )
    {
        const struct Mock230SocialEntry* entry = &g_roster[i];

        if( list_index_of(entry->friends, entry->friend_count, name37) < 0 )
            continue;
        if( out && found < max )
            out[found] = entry->name37;
        found++;
    }
    return found;
}

/* ------------------------------------------------------------------ */
/* Private messages                                                    */
/* ------------------------------------------------------------------ */

int32_t
mock230_friends_next_pm_id(void)
{
    int32_t id = (MOCK230_WORLD_ID << 24) | (g_pm_counter & 0x00ffffff);

    g_pm_counter++;
    /* Wrapping back through 0 would hand out an id the client throws away, so
     * the counter restarts at 1 rather than at 0. */
    if( (g_pm_counter & 0x00ffffff) == 0 )
        g_pm_counter = 1;
    return id;
}

/* ------------------------------------------------------------------ */
/* Spam protection                                                     */
/* ------------------------------------------------------------------ */

int
mock230_friends_social_gate(struct Mock230Player* player)
{
    assert(player);
    if( player->social_protect )
        return 0;
    player->social_protect = 1;
    return 1;
}
