#include "rs_social.h"

#include "net/jbase37.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

void
RS_Social_Init(struct RS_Social* social)
{
    assert(social);
    memset(social, 0, sizeof(*social));
    /*
     * CONNECTED, not LOADING. The friend-server status only ever moves off this
     * value when a server says so (FRIENDLIST_LOADED carries it), and a client
     * booted with no server at all — every offline / dat1 manifest — would
     * otherwise sit on "Loading friends list<br>Please wait..." forever instead
     * of showing the correct empty state.
     */
    social->server_status = RS_SOCIAL_SERVER_CONNECTED;
    social->node_id = 1;
}

/* Base-37 key for a name in any of the forms this store is handed one:
 * lowercase-with-underscores from a base37 packet, "Eden\xa0Cosine" as a
 * jstr packet spells it, "Bob" from a chat line, "B o b" from a typed prompt.
 * strtobase37 folds case and maps every character it does not know (space,
 * underscore, the cp1252 NBSP) onto the same slot, so all of them agree. */
static int64_t
name_hash(char const* name)
{
    assert(name);
    if( !name[0] )
        return 0;
    return (int64_t)strtobase37(name);
}

static int
find_hash(
    int64_t const* hashes,
    char const names[][RS_SOCIAL_NAME_LEN],
    int count,
    char const* name)
{
    int64_t hash = name_hash(name);

    if( hash != 0 )
    {
        for( int i = 0; i < count; i++ )
        {
            if( hashes[i] == hash )
                return i;
        }
        return -1;
    }
    /* Unpackable name: fall back to the literal comparison this store used
     * before it had hashes, so nothing that worked stops working. */
    for( int i = 0; i < count; i++ )
    {
        if( strcasecmp(names[i], name) == 0 )
            return i;
    }
    return -1;
}

void
RS_Social_DisplayName(
    char const* raw,
    char* out,
    int cap)
{
    int at_word_start = 1;
    int i = 0;

    if( cap <= 0 )
        return;
    assert(out);
    out[0] = '\0';
    if( !raw )
        return;

    for( ; raw[i] != '\0' && i < cap - 1; i++ )
    {
        char c = raw[i];

        if( c == '_' )
            c = ' ';
        if( at_word_start && c >= 'a' && c <= 'z' )
            c = (char)toupper((unsigned char)c);
        at_word_start = (c == ' ');
        out[i] = c;
    }
    out[i] = '\0';
}

void
RS_Social_SetFriendWorld(
    struct RS_Social* social,
    int index,
    int world)
{
    assert(social);
    assert(index >= 0);
    assert(index < social->friend_count);
    int const previous = social->friend_world[index];
    if( previous == world )
        return;
    social->friend_world[index] = world;
    social->friend_world_change[index] = ++social->world_change_counter;
    if( previous == -1 && world == 0 )
        social->friend_world_change[index] = -social->friend_world_change[index];
}

int
RS_Social_AddFriend(
    struct RS_Social* social,
    char const* name,
    int world)
{
    assert(social);
    assert(name);
    if( !name[0] || social->friend_count >= RS_SOCIAL_FRIEND_MAX )
        return 0;
    if( find_hash(social->friend_hash, social->friend_name, social->friend_count, name) >= 0 )
        return 0;
    strncpy(social->friend_name[social->friend_count], name, RS_SOCIAL_NAME_LEN - 1);
    social->friend_name[social->friend_count][RS_SOCIAL_NAME_LEN - 1] = '\0';
    social->friend_hash[social->friend_count] = name_hash(name);
    /* A new entry's world starts unknown (-1) and the first report is a
     * change, as in the client, so it gets a serial like any later one. */
    social->friend_world[social->friend_count] = -1;
    social->friend_world_change[social->friend_count] = 0;
    social->friend_rank[social->friend_count] = 0;
    social->friend_previous_name[social->friend_count][0] = '\0';
    social->friend_count++;
    RS_Social_SetFriendWorld(social, social->friend_count - 1, world);
    return 1;
}

void
RS_Social_DelFriendAt(
    struct RS_Social* social,
    int idx)
{
    assert(social);
    assert(idx >= 0);
    assert(idx < social->friend_count);
    for( int i = idx; i + 1 < social->friend_count; i++ )
    {
        memcpy(social->friend_name[i], social->friend_name[i + 1], RS_SOCIAL_NAME_LEN);
        social->friend_hash[i] = social->friend_hash[i + 1];
        social->friend_world[i] = social->friend_world[i + 1];
        social->friend_world_change[i] = social->friend_world_change[i + 1];
        social->friend_rank[i] = social->friend_rank[i + 1];
        memcpy(social->friend_previous_name[i], social->friend_previous_name[i + 1], RS_SOCIAL_NAME_LEN);
    }
    social->friend_count--;
}

int
RS_Social_DelFriend(
    struct RS_Social* social,
    char const* name)
{
    assert(social);
    int idx = find_hash(social->friend_hash, social->friend_name, social->friend_count, name);
    if( idx < 0 )
        return 0;
    RS_Social_DelFriendAt(social, idx);
    return 1;
}

void
RS_Social_SetFriendNames(
    struct RS_Social* social,
    int index,
    char const* name,
    char const* previous_name)
{
    assert(social);
    assert(index >= 0);
    assert(index < social->friend_count);
    assert(name);
    assert(previous_name);
    snprintf(social->friend_name[index], RS_SOCIAL_NAME_LEN, "%s", name);
    social->friend_hash[index] = name_hash(name);
    snprintf(social->friend_previous_name[index], RS_SOCIAL_NAME_LEN, "%s", previous_name);
}

void
RS_Social_FriendWorldNotice(
    struct RS_Social* social,
    char const* name,
    int world,
    int clock)
{
    struct RS_SocialNotices* q;
    int64_t hash;
    bool queue = true;
    int kept = 0;

    assert(social);
    assert(name);
    assert(world >= 0);
    q = &social->notices;
    hash = name_hash(name);
    /* Every queued notice of the other direction for this friend goes, and
     * then nothing is queued (Statics.method10091's loop over field5447). */
    for( int i = 0; i < q->count; i++ )
    {
        struct RS_SocialNotice const* n = &q->entries[i];
        bool const same = n->hash != 0 ? n->hash == hash : strcasecmp(n->name, name) == 0;

        if( same && ((world != 0 && n->world == 0) || (world == 0 && n->world != 0)) )
        {
            queue = false;
            continue;
        }
        if( kept != i )
            q->entries[kept] = *n;
        kept++;
    }
    q->count = kept;
    if( !queue )
        return;
    /* Ten seconds at the most (class474 lives ten to eleven), so a full
     * queue is that many world changes inside eleven seconds. */
    assert(q->count < RS_SOCIAL_NOTICE_MAX);
    snprintf(q->entries[q->count].name, RS_SOCIAL_NAME_LEN, "%s", name);
    q->entries[q->count].hash = hash;
    q->entries[q->count].world = world;
    q->entries[q->count].stamp = clock / RS_SOCIAL_CYCLES_PER_SECOND + 5;
    q->count++;
}

int
RS_Social_TakeDueNotice(
    struct RS_Social* social,
    int clock,
    char* text,
    int cap)
{
    struct RS_SocialNotices* q;
    int const now = clock / RS_SOCIAL_CYCLES_PER_SECOND;

    assert(social);
    assert(text);
    assert(cap > 0);
    q = &social->notices;
    for( int i = 0; i < q->count; i++ )
    {
        if( q->entries[i].stamp >= now - 5 )
            continue;
        snprintf(text, (size_t)cap, "%s%s", q->entries[i].name,
            q->entries[i].world > 0 ? " has logged in." : " has logged out.");
        memmove(&q->entries[i], &q->entries[i + 1], (size_t)(q->count - i - 1) * sizeof(q->entries[0]));
        q->count--;
        return 1;
    }
    return 0;
}

int
RS_Social_AddIgnore(
    struct RS_Social* social,
    char const* name)
{
    assert(social);
    assert(name);
    if( !name[0] || social->ignore_count >= RS_SOCIAL_IGNORE_MAX )
        return 0;
    if( find_hash(social->ignore_hash, social->ignore_name, social->ignore_count, name) >= 0 )
        return 0;
    strncpy(social->ignore_name[social->ignore_count], name, RS_SOCIAL_NAME_LEN - 1);
    social->ignore_name[social->ignore_count][RS_SOCIAL_NAME_LEN - 1] = '\0';
    social->ignore_hash[social->ignore_count] = name_hash(name);
    social->ignore_previous_name[social->ignore_count][0] = '\0';
    social->ignore_count++;
    return 1;
}

void
RS_Social_DelIgnoreAt(
    struct RS_Social* social,
    int index)
{
    assert(social);
    assert(index >= 0);
    assert(index < social->ignore_count);
    for( int i = index; i + 1 < social->ignore_count; i++ )
    {
        memcpy(social->ignore_name[i], social->ignore_name[i + 1], RS_SOCIAL_NAME_LEN);
        social->ignore_hash[i] = social->ignore_hash[i + 1];
        memcpy(social->ignore_previous_name[i], social->ignore_previous_name[i + 1], RS_SOCIAL_NAME_LEN);
    }
    social->ignore_count--;
}

int
RS_Social_DelIgnore(
    struct RS_Social* social,
    char const* name)
{
    assert(social);
    int idx = find_hash(social->ignore_hash, social->ignore_name, social->ignore_count, name);
    if( idx < 0 )
        return 0;
    RS_Social_DelIgnoreAt(social, idx);
    return 1;
}

int
RS_Social_IgnoreIndex(
    struct RS_Social const* social,
    char const* name)
{
    assert(social);
    assert(name);
    return find_hash(social->ignore_hash, social->ignore_name, social->ignore_count, name);
}

void
RS_Social_SetIgnoreNames(
    struct RS_Social* social,
    int index,
    char const* name,
    char const* previous_name)
{
    assert(social);
    assert(index >= 0);
    assert(index < social->ignore_count);
    assert(name);
    assert(previous_name);
    snprintf(social->ignore_name[index], RS_SOCIAL_NAME_LEN, "%s", name);
    social->ignore_hash[index] = name_hash(name);
    snprintf(social->ignore_previous_name[index], RS_SOCIAL_NAME_LEN, "%s", previous_name);
}

void
RS_Social_ResetForLogin(struct RS_Social* social)
{
    assert(social);
    social->friend_count = 0;
    social->ignore_count = 0;
    social->server_status = RS_SOCIAL_SERVER_LOADING;
}

void
RS_Social_SortReset(struct RS_SocialSortChain* chain)
{
    assert(chain);
    chain->count = 0;
}

static bool
sort_key_is_terminal(enum RS_SocialSortKey key)
{
    return key == RS_SOCIAL_SORT_LEGACY || key == RS_SOCIAL_SORT_NAME ||
           key == RS_SOCIAL_SORT_LAST_WORLD_CHANGE;
}

void
RS_Social_SortAppend(
    struct RS_SocialSortChain* chain,
    enum RS_SocialSortKey key,
    bool ascending)
{
    assert(chain);
    if( chain->count > 0 && sort_key_is_terminal(chain->steps[chain->count - 1].key) )
        return;
    if( chain->count >= RS_SOCIAL_SORT_CHAIN_MAX )
        return;
    chain->steps[chain->count].key = key;
    chain->steps[chain->count].ascending = ascending;
    chain->count++;
}

/* A Username's separators (deob Statics.method8328): trimmed from both ends
 * before the key is made. The bytes are the wire's cp1252. */
static bool
name_is_separator(unsigned char c)
{
    return c == 0xa0 || c == ' ' || c == '_' || c == '-';
}

/* One character of a Username's key (deob Statics.method12690): separators
 * fold to '_', the accented Latin-1 letters to their base letter, the rest
 * to lower case. 0 drops the character. */
static char
name_key_char(unsigned char c)
{
    static char const k_latin1[64] = {
        /* 0xc0 */ 'a', 'a', 'a', 'a', 'a', 0, 0, 'c', 'e', 'e', 'e', 'e', 0, 'i', 'i', 'i',
        /* 0xd0 */ 0, 'n', 'o', 'o', 'o', 'o', 'o', 0, 0, 'u', 'u', 'u', 'u', 0, 0, 'b',
        /* 0xe0 */ 'a', 'a', 'a', 'a', 'a', 0, 0, 'c', 'e', 'e', 'e', 'e', 0, 'i', 'i', 'i',
        /* 0xf0 */ 0, 'n', 'o', 'o', 'o', 'o', 'o', 0, 0, 'u', 'u', 'u', 'u', 0, 0, 'y',
    };

    if( name_is_separator(c) )
        return '_';
    if( (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || c == '#' || c == '[' || c == ']' )
        return (char)c;
    if( c >= 'A' && c <= 'Z' )
        return (char)(c - 'A' + 'a');
    if( c == 0x9f ) /* cp1252 Y-diaeresis, U+0178 */
        return 'y';
    if( c >= 0xc0 )
        return k_latin1[c - 0xc0];
    return 0;
}

/* The key a Username compares by (deob class7.field12): the name trimmed of
 * separators, at most 12 characters, folded by name_key_char. "" when the
 * name is no Username (empty, or too long), which compareTo puts last. */
static void
name_key(char const* name, char out[RS_SOCIAL_NAME_LEN])
{
    size_t start = 0;
    size_t end = strlen(name);
    int n = 0;

    while( start < end && name_is_separator((unsigned char)name[start]) )
        start++;
    while( end > start && name_is_separator((unsigned char)name[end - 1]) )
        end--;
    out[0] = '\0';
    if( end - start < 1 || end - start > 12 )
        return;
    for( size_t i = start; i < end; i++ )
    {
        char const c = name_key_char((unsigned char)name[i]);

        if( c )
            out[n++] = c;
    }
    out[n] = '\0';
}

/* The order the client sorts names in (deob class628, class7.method28): by
 * the Username key, a String compareTo; a name with no key sorts after every
 * name with one. */
static int
compare_names(char const* a_raw, char const* b_raw)
{
    char a[RS_SOCIAL_NAME_LEN];
    char b[RS_SOCIAL_NAME_LEN];

    name_key(a_raw, a);
    name_key(b_raw, b);
    if( !a[0] || !b[0] )
        return (a[0] ? -1 : 0) + (b[0] ? 1 : 0);
    return strcmp(a, b);
}

static int
compare_ints(int a, int b)
{
    return a < b ? -1 : a > b ? 1 : 0;
}

int
RS_Social_CompareEntries(
    struct RS_SocialSortChain const* chain,
    int own_world,
    struct RS_SocialSortEntry const* a,
    struct RS_SocialSortEntry const* b)
{
    assert(chain);
    assert(a);
    assert(b);
    if( chain->count == 0 )
        return compare_names(a->name, b->name);

    bool const online_a = a->world != 0;
    bool const online_b = b->world != 0;
    bool const ours_a = a->world == own_world;
    bool const ours_b = b->world == own_world;
    for( int i = 0; i < chain->count; i++ )
    {
        bool const ascending = chain->steps[i].ascending;
        int result = 0;
        switch( chain->steps[i].key )
        {
        case RS_SOCIAL_SORT_LEGACY:
        case RS_SOCIAL_SORT_NAME:
            result = compare_names(a->name, b->name);
            return ascending ? result : -result;
        case RS_SOCIAL_SORT_LAST_WORLD_CHANGE:
            result = compare_ints(a->world_change, b->world_change);
            return ascending ? result : -result;
        case RS_SOCIAL_SORT_WORLD:
            result = compare_ints(a->world, b->world);
            break;
        case RS_SOCIAL_SORT_RANK:
            result = compare_ints(a->rank, b->rank);
            break;
        case RS_SOCIAL_SORT_ONLINE_STATUS:
            if( online_a != online_b )
                result = online_a ? -1 : 1;
            break;
        /* These four decide outright once their condition holds -- a tie
         * included (class143, class135, class136, class132 return the
         * difference without deferring) -- and defer only when it does not. */
        case RS_SOCIAL_SORT_ONLINE_NAME:
            if( !(online_a && online_b) )
                continue;
            result = compare_names(a->name, b->name);
            return ascending ? result : -result;
        case RS_SOCIAL_SORT_ONLINE_LAST_WORLD_CHANGE:
            if( !(online_a && online_b) )
                continue;
            result = compare_ints(a->world_change, b->world_change);
            return ascending ? result : -result;
        case RS_SOCIAL_SORT_OWN_WORLD_NAME:
            if( !(ours_a && ours_b) )
                continue;
            result = compare_names(a->name, b->name);
            return ascending ? result : -result;
        case RS_SOCIAL_SORT_OWN_WORLD_LAST_WORLD_CHANGE:
            if( !(ours_a && ours_b) )
                continue;
            result = compare_ints(a->world_change, b->world_change);
            return ascending ? result : -result;
        case RS_SOCIAL_SORT_ONLINE_WORLD:
            if( ours_a != ours_b )
                result = ours_a ? -1 : 1;
            break;
        }
        if( result != 0 )
            return ascending ? result : -result;
    }
    return 0;
}

static int
compare_friends(struct RS_Social const* social, int a, int b)
{
    struct RS_SocialSortEntry const entry_a = {
        social->friend_name[a], social->friend_world[a], social->friend_world_change[a], social->friend_rank[a]
    };
    struct RS_SocialSortEntry const entry_b = {
        social->friend_name[b], social->friend_world[b], social->friend_world_change[b], social->friend_rank[b]
    };
    return RS_Social_CompareEntries(&social->friend_sort, social->node_id, &entry_a, &entry_b);
}

static void
swap_friends(struct RS_Social* social, int a, int b)
{
    char name[RS_SOCIAL_NAME_LEN];
    memcpy(name, social->friend_name[a], RS_SOCIAL_NAME_LEN);
    memcpy(social->friend_name[a], social->friend_name[b], RS_SOCIAL_NAME_LEN);
    memcpy(social->friend_name[b], name, RS_SOCIAL_NAME_LEN);
    int64_t const hash = social->friend_hash[a];
    social->friend_hash[a] = social->friend_hash[b];
    social->friend_hash[b] = hash;
    int const world = social->friend_world[a];
    social->friend_world[a] = social->friend_world[b];
    social->friend_world[b] = world;
    int const change = social->friend_world_change[a];
    social->friend_world_change[a] = social->friend_world_change[b];
    social->friend_world_change[b] = change;
    int const rank = social->friend_rank[a];
    social->friend_rank[a] = social->friend_rank[b];
    social->friend_rank[b] = rank;
    memcpy(name, social->friend_previous_name[a], RS_SOCIAL_NAME_LEN);
    memcpy(social->friend_previous_name[a], social->friend_previous_name[b], RS_SOCIAL_NAME_LEN);
    memcpy(social->friend_previous_name[b], name, RS_SOCIAL_NAME_LEN);
}

/* Insertion sort: stable, like the client's Arrays.sort over objects, and the
 * lists are at most a couple of hundred long. */
void
RS_Social_SortFriends(struct RS_Social* social)
{
    assert(social);
    for( int i = 1; i < social->friend_count; i++ )
        for( int j = i; j > 0 && compare_friends(social, j - 1, j) > 0; j-- )
            swap_friends(social, j - 1, j);
}

/* The ignore list only ever gets the two name comparators. */
static int
compare_ignores(struct RS_Social const* social, int a, int b)
{
    int const result = compare_names(social->ignore_name[a], social->ignore_name[b]);
    if( social->ignore_sort.count == 0 )
        return result;
    return social->ignore_sort.steps[0].ascending ? result : -result;
}

void
RS_Social_SortIgnores(struct RS_Social* social)
{
    assert(social);
    for( int i = 1; i < social->ignore_count; i++ )
    {
        for( int j = i; j > 0 && compare_ignores(social, j - 1, j) > 0; j-- )
        {
            char name[RS_SOCIAL_NAME_LEN];
            memcpy(name, social->ignore_name[j - 1], RS_SOCIAL_NAME_LEN);
            memcpy(social->ignore_name[j - 1], social->ignore_name[j], RS_SOCIAL_NAME_LEN);
            memcpy(social->ignore_name[j], name, RS_SOCIAL_NAME_LEN);
            int64_t const hash = social->ignore_hash[j - 1];
            social->ignore_hash[j - 1] = social->ignore_hash[j];
            social->ignore_hash[j] = hash;
            memcpy(name, social->ignore_previous_name[j - 1], RS_SOCIAL_NAME_LEN);
            memcpy(social->ignore_previous_name[j - 1], social->ignore_previous_name[j], RS_SOCIAL_NAME_LEN);
            memcpy(social->ignore_previous_name[j], name, RS_SOCIAL_NAME_LEN);
        }
    }
}

int
RS_Social_FriendCount(struct RS_Social const* social)
{
    assert(social);
    /*
     * The 2004 client says this by reporting 0 friends while
     * friendServerStatus != 2 (Client.ts, the CC_FRIENDS_* clientCode branch);
     * the rev-230 panel spells the same state as a negative count, so that is
     * the translation. -1 rather than -2: both render the same text, and -1 is
     * what script 129's ignore twin uses.
     */
    if( social->server_status != RS_SOCIAL_SERVER_CONNECTED )
        return -1;
    return social->friend_count;
}

int
RS_Social_IgnoreCount(struct RS_Social const* social)
{
    return social ? social->ignore_count : 0;
}

void
RS_Social_FriendName(
    struct RS_Social const* social,
    int index,
    char* out,
    int cap)
{
    if( cap <= 0 )
        return;
    assert(out);
    out[0] = '\0';
    if( !social || index < 0 || index >= social->friend_count )
        return;
    snprintf(out, (size_t)cap, "%s", social->friend_name[index]);
}

void
RS_Social_IgnoreName(
    struct RS_Social const* social,
    int index,
    char* out,
    int cap)
{
    if( cap <= 0 )
        return;
    assert(out);
    out[0] = '\0';
    if( !social || index < 0 || index >= social->ignore_count )
        return;
    snprintf(out, (size_t)cap, "%s", social->ignore_name[index]);
}

void
RS_Social_FriendPreviousName(
    struct RS_Social const* social,
    int index,
    char* out,
    int cap)
{
    if( cap <= 0 )
        return;
    assert(social);
    assert(out);
    out[0] = '\0';
    if( index < 0 || index >= social->friend_count )
        return;
    snprintf(out, (size_t)cap, "%s", social->friend_previous_name[index]);
}

void
RS_Social_IgnorePreviousName(
    struct RS_Social const* social,
    int index,
    char* out,
    int cap)
{
    if( cap <= 0 )
        return;
    assert(social);
    assert(out);
    out[0] = '\0';
    if( index < 0 || index >= social->ignore_count )
        return;
    snprintf(out, (size_t)cap, "%s", social->ignore_previous_name[index]);
}

int
RS_Social_FriendRank(
    struct RS_Social const* social,
    int index)
{
    assert(social);
    if( index < 0 || index >= social->friend_count )
        return 0;
    return social->friend_rank[index];
}

int
RS_Social_FriendWorld(
    struct RS_Social const* social,
    int index)
{
    assert(social);
    if( index < 0 || index >= social->friend_count )
        return 0;
    return social->friend_world[index];
}

int
RS_Social_IsFriend(
    struct RS_Social const* social,
    char const* name)
{
    assert(social);
    assert(name);
    return find_hash(social->friend_hash, social->friend_name, social->friend_count, name) >= 0;
}

int
RS_Social_FriendIndex(
    struct RS_Social const* social,
    char const* name)
{
    assert(social);
    assert(name);
    return find_hash(social->friend_hash, social->friend_name, social->friend_count, name);
}

int
RS_Social_IsIgnored(
    struct RS_Social const* social,
    char const* name)
{
    assert(social);
    assert(name);
    return find_hash(social->ignore_hash, social->ignore_name, social->ignore_count, name) >= 0;
}

bool
RS_Social_MessageSeen(
    struct RS_Social const* social,
    int64_t key)
{
    assert(social);
    for( int i = 0; i < RS_SOCIAL_MESSAGE_RING; i++ )
        if( social->message_ring.ids[i] == key )
            return true;
    return false;
}

void
RS_Social_MessageRemember(
    struct RS_Social* social,
    int64_t key)
{
    assert(social);
    social->message_ring.ids[social->message_ring.head] = key;
    social->message_ring.head = (social->message_ring.head + 1) % RS_SOCIAL_MESSAGE_RING;
}

bool
RS_Social_MessageSeenOrRemember(
    struct RS_Social* social,
    int64_t key)
{
    assert(social);
    if( RS_Social_MessageSeen(social, key) )
        return true;
    RS_Social_MessageRemember(social, key);
    return false;
}
