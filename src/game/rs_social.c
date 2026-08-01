#include "rs_social.h"

#include "net/jbase37.h"

#include <assert.h>
#include <ctype.h>
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
 * lowercase-with-underscores from a packet, "Bob" from a chat line, "B o b"
 * from a typed prompt. strtobase37 folds case and maps every character it does
 * not know (space, underscore) onto the same slot, so all three agree. */
static int64_t
name_hash(char const* name)
{
    if( !name || !name[0] )
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

    if( !out || cap <= 0 )
        return;
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

int
RS_Social_AddFriend(
    struct RS_Social* social,
    char const* name,
    int world)
{
    assert(social);
    if( !name || !name[0] || social->friend_count >= RS_SOCIAL_FRIEND_MAX )
        return 0;
    if( find_hash(social->friend_hash, social->friend_name, social->friend_count, name) >= 0 )
        return 0;
    strncpy(social->friend_name[social->friend_count], name, RS_SOCIAL_NAME_LEN - 1);
    social->friend_name[social->friend_count][RS_SOCIAL_NAME_LEN - 1] = '\0';
    social->friend_hash[social->friend_count] = name_hash(name);
    social->friend_world[social->friend_count] = world;
    social->friend_count++;
    return 1;
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
    for( int i = idx; i + 1 < social->friend_count; i++ )
    {
        memcpy(social->friend_name[i], social->friend_name[i + 1], RS_SOCIAL_NAME_LEN);
        social->friend_hash[i] = social->friend_hash[i + 1];
        social->friend_world[i] = social->friend_world[i + 1];
    }
    social->friend_count--;
    return 1;
}

int
RS_Social_AddIgnore(
    struct RS_Social* social,
    char const* name)
{
    assert(social);
    if( !name || !name[0] || social->ignore_count >= RS_SOCIAL_IGNORE_MAX )
        return 0;
    if( find_hash(social->ignore_hash, social->ignore_name, social->ignore_count, name) >= 0 )
        return 0;
    strncpy(social->ignore_name[social->ignore_count], name, RS_SOCIAL_NAME_LEN - 1);
    social->ignore_name[social->ignore_count][RS_SOCIAL_NAME_LEN - 1] = '\0';
    social->ignore_hash[social->ignore_count] = name_hash(name);
    social->ignore_count++;
    return 1;
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
    for( int i = idx; i + 1 < social->ignore_count; i++ )
    {
        memcpy(social->ignore_name[i], social->ignore_name[i + 1], RS_SOCIAL_NAME_LEN);
        social->ignore_hash[i] = social->ignore_hash[i + 1];
    }
    social->ignore_count--;
    return 1;
}

int
RS_Social_FriendCount(struct RS_Social const* social)
{
    if( !social )
        return 0;
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
    if( !out || cap <= 0 )
        return;
    out[0] = '\0';
    if( !social || index < 0 || index >= social->friend_count )
        return;
    RS_Social_DisplayName(social->friend_name[index], out, cap);
}

void
RS_Social_IgnoreName(
    struct RS_Social const* social,
    int index,
    char* out,
    int cap)
{
    if( !out || cap <= 0 )
        return;
    out[0] = '\0';
    if( !social || index < 0 || index >= social->ignore_count )
        return;
    RS_Social_DisplayName(social->ignore_name[index], out, cap);
}

int
RS_Social_FriendWorld(
    struct RS_Social const* social,
    int index)
{
    if( !social || index < 0 || index >= social->friend_count )
        return 0;
    return social->friend_world[index];
}

int
RS_Social_IsFriend(
    struct RS_Social const* social,
    char const* name)
{
    if( !social || !name )
        return 0;
    return find_hash(social->friend_hash, social->friend_name, social->friend_count, name) >= 0;
}

int
RS_Social_IsIgnored(
    struct RS_Social const* social,
    char const* name)
{
    if( !social || !name )
        return 0;
    return find_hash(social->ignore_hash, social->ignore_name, social->ignore_count, name) >= 0;
}
