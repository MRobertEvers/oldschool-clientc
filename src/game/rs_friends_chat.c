#include "game/rs_friends_chat.h"

#include "net/jbase37.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

struct ChatReader
{
    uint8_t const* data;
    int length;
    int position;
    bool failed;
};

static int
g1(struct ChatReader* r)
{
    if( r->failed || r->position + 1 > r->length )
    {
        r->failed = true;
        return 0;
    }
    return r->data[r->position++];
}

static int
g1s(struct ChatReader* r)
{
    return (int)(int8_t)g1(r);
}

static int
g2(struct ChatReader* r)
{
    int const hi = g1(r);
    return (hi << 8) | g1(r);
}

static int64_t
g8(struct ChatReader* r)
{
    uint64_t value = 0;
    for( int i = 0; i < 8; i++ )
        value = (value << 8) | (uint64_t)g1(r);
    return (int64_t)value;
}

static void
gjstr(struct ChatReader* r, char* out, int cap)
{
    int written = 0;
    for( ;; )
    {
        int const c = g1(r);
        if( r->failed || c == 0 )
            break;
        if( written + 1 < cap )
            out[written++] = (char)c;
    }
    out[written] = '\0';
}

/* smart1or2null: a byte under 128 is itself minus one; otherwise a u16 minus
 * 32769. 0 reads -1, "no list". */
static int
gsmart_null(struct ChatReader* r)
{
    if( r->failed || r->position >= r->length )
    {
        r->failed = true;
        return -1;
    }
    if( r->data[r->position] < 128 )
        return g1(r) - 1;
    return g2(r) - 32769;
}

bool
RS_FriendsChat_NamesEqual(char const* a, char const* b)
{
    assert(a);
    assert(b);
    for( ;; a++, b++ )
    {
        char const ca = *a == '_' ? ' ' : (char)tolower((unsigned char)*a);
        char const cb = *b == '_' ? ' ' : (char)tolower((unsigned char)*b);
        if( ca != cb )
            return false;
        if( !ca )
            return true;
    }
}

void
RS_FriendsChat_Init(struct RS_FriendsChat* chat)
{
    assert(chat);
    memset(chat, 0, sizeof(*chat));
    chat->serial = 1;
}

static void
note_local_rank(struct RS_FriendsChat* chat, struct RS_FriendsChatMember const* member, char const* local_name)
{
    if( local_name && local_name[0] && RS_FriendsChat_NamesEqual(member->name, local_name) )
        chat->local_rank = member->rank;
}

bool
RS_FriendsChat_ApplyFull(
    struct RS_FriendsChat* chat,
    uint8_t const* data,
    int length,
    char const* local_name)
{
    assert(chat);
    assert(data || length == 0);
    chat->transmit_serial++;
    if( length == 0 )
    {
        int const transmit = chat->transmit_serial;
        RS_FriendsChat_Init(chat);
        chat->transmit_serial = transmit;
        return true;
    }

    struct ChatReader r = { data, length, 0, false };
    char owner[RS_FRIENDS_CHAT_NAME_LEN];
    gjstr(&r, owner, sizeof(owner));
    int64_t const name37 = g8(&r);
    int const kick_rank = g1s(&r);
    int const count = gsmart_null(&r);
    if( r.failed )
        return false;

    /* A refresh reuses the channel, as the client does -- including, when the
     * count says "no list", the members it already had. */
    chat->in_channel = true;
    snprintf(chat->owner, sizeof(chat->owner), "%s", owner);
    chat->has_owner = owner[0] != '\0';
    {
        char raw[RS_FRIENDS_CHAT_NAME_LEN];
        base37tostr((uint64_t)name37, raw, sizeof(raw));
        RS_Social_DisplayName(raw, chat->display_name, sizeof(chat->display_name));
    }
    chat->kick_rank = kick_rank;
    if( count == -1 )
        return true;

    chat->member_count = 0;
    for( int i = 0; i < count; i++ )
    {
        struct RS_FriendsChatMember member;
        memset(&member, 0, sizeof(member));
        char world_name[RS_FRIENDS_CHAT_NAME_LEN];
        gjstr(&r, member.name, sizeof(member.name));
        member.world = g2(&r);
        member.rank = g1s(&r);
        gjstr(&r, world_name, sizeof(world_name));
        if( r.failed )
            return false;
        if( chat->member_count >= RS_FRIENDS_CHAT_CAPACITY )
            continue;
        member.world_change = chat->serial++;
        chat->members[chat->member_count++] = member;
        note_local_rank(chat, &member, local_name);
    }
    return true;
}

bool
RS_FriendsChat_ApplySingleUser(
    struct RS_FriendsChat* chat,
    uint8_t const* data,
    int length,
    char const* local_name)
{
    assert(chat);
    assert(data || length == 0);
    chat->transmit_serial++;
    if( !chat->in_channel )
        return true;

    struct ChatReader r = { data, length, 0, false };
    char name[RS_FRIENDS_CHAT_NAME_LEN];
    gjstr(&r, name, sizeof(name));
    int const world = g2(&r);
    int const rank = g1s(&r);
    if( r.failed )
        return false;

    int index = -1;
    for( int i = 0; i < chat->member_count; i++ )
    {
        if( RS_FriendsChat_NamesEqual(chat->members[i].name, name) )
        {
            index = i;
            break;
        }
    }

    /* -128 removes, and only the entry on the world the packet names. */
    if( rank == -128 )
    {
        if( index >= 0 && chat->members[index].world == world )
        {
            memmove(&chat->members[index], &chat->members[index + 1],
                (size_t)(chat->member_count - index - 1) * sizeof(chat->members[0]));
            chat->member_count--;
        }
        return true;
    }

    char world_name[RS_FRIENDS_CHAT_NAME_LEN];
    gjstr(&r, world_name, sizeof(world_name));
    if( r.failed )
        return false;
    if( index < 0 )
    {
        if( chat->member_count >= RS_FRIENDS_CHAT_CAPACITY )
            return true;
        index = chat->member_count++;
        memset(&chat->members[index], 0, sizeof(chat->members[index]));
        snprintf(chat->members[index].name, sizeof(chat->members[index].name), "%s", name);
    }
    chat->members[index].world = world;
    chat->members[index].world_change = chat->serial++;
    chat->members[index].rank = rank;
    note_local_rank(chat, &chat->members[index], local_name);
    return true;
}

void
RS_FriendsChat_Sort(
    struct RS_FriendsChat* chat,
    int own_world)
{
    assert(chat);
    for( int i = 1; i < chat->member_count; i++ )
    {
        for( int j = i; j > 0; j-- )
        {
            struct RS_FriendsChatMember const* const a = &chat->members[j - 1];
            struct RS_FriendsChatMember const* const b = &chat->members[j];
            struct RS_SocialSortEntry const ea = { a->name, a->world, a->world_change, a->rank };
            struct RS_SocialSortEntry const eb = { b->name, b->world, b->world_change, b->rank };
            if( RS_Social_CompareEntries(&chat->sort, own_world, &ea, &eb) <= 0 )
                break;
            struct RS_FriendsChatMember const t = chat->members[j - 1];
            chat->members[j - 1] = chat->members[j];
            chat->members[j] = t;
        }
    }
}
