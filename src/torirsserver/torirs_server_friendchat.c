#include "torirs_server_friendchat.h"

#include "torirs_server.h"
#include "torirs_server_content.h"
#include "torirs_server_friends.h"
#include "mem/torirs_mem.h"
#include "net/jbase37.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

/*
 * See torirs_server_friendchat.h for the recorded behaviour this implements and
 * the sessions it is cited from. This file holds the state, applies the rules,
 * sends the packets and hands every sentence to content.
 */

enum
{
    /* Storage ceilings, not policy (the friend service's rule): content's
     * `^friendchat_member_max` decides how full a channel may get and is
     * checked against FC_MEMBERS_MAX when read. */
    FC_OWNERS_MAX = 512,
    FC_MEMBERS_MAX = 200,
    FC_BANS_MAX = 64,
};

struct FcRank
{
    int64_t friend37;
    int rank;
};

struct FcMember
{
    int64_t name37;
    /** The rank the channel last told everyone; a flush that finds it
     *  changed sends the member's row again (RSProx index 754 [186]). */
    int rank;
};

struct FcBan
{
    int64_t name37;
    int until_tick;
};

struct FcSettings
{
    int enabled;
    int64_t prefix37;
    int enter;
    int talk;
    int kick;
};

struct FcOwner
{
    /** 0 = free slot. */
    int64_t owner37;
    char owner_display[16];
    /** What the live channel runs on, and what the owner has asked for. They
     *  differ only inside a flush window (`flush_tick` non-zero). */
    struct FcSettings live;
    struct FcSettings asked;
    int flush_tick;
    /** A rank changed inside the window: the flush re-sends member rows. */
    int ranks_dirty;

    struct FcRank* ranks;
    int rank_count;
    int rank_capacity;

    /** The channel. It exists while it has members; the bans are its own and
     *  go when it empties (RSProx index 761 [248..258]). */
    struct FcMember* members;
    int member_count;
    int member_capacity;
    struct FcBan bans[FC_BANS_MAX];
    int ban_count;
    /** A disable flushed: the members are removed on this tick (a tick after
     *  the owner's line, RSProx index 753 [475..476]). 0 = none. */
    int close_tick;
};

static struct FcOwner g_owners[FC_OWNERS_MAX];
static int g_owner_count;
/** The world tick the service last saw. Join answers and flushes are timed
 *  against it; ToriRSServer_FriendchatTick keeps it current. */
static int g_now;

/** Bring the clock to the player's world's tick: a packet handler runs before
 *  this tick's ToriRSServer_FriendchatTick, and a join timed from last tick's
 *  clock would be answered on its own tick. */
static void
fc_clock(const struct ToriRSServerPlayer* player)
{
    if( player->world )
        g_now = player->world->tick;
}

/* ------------------------------------------------------------------ */
/* Content numbers                                                     */
/* ------------------------------------------------------------------ */

static int
fc_member_max(void)
{
    int value = ToriRSServer_ContentConstantInt("friendchat_member_max", FC_MEMBERS_MAX);

    if( value > FC_MEMBERS_MAX || value <= 0 )
    {
        static int reported;

        if( !reported )
        {
            reported = 1;
            fprintf(stderr,
                    "torirsserver: ^friendchat_member_max is %d; this server holds 1..%d\n",
                    value, FC_MEMBERS_MAX);
        }
        return FC_MEMBERS_MAX;
    }
    return value;
}

/** Ticks between the first settings change and the live channel taking it:
 *  100 (RSProx index 753 [375]->[475], 752 [1188]->[1288]). */
static int
fc_settings_delay(void)
{
    return ToriRSServer_ContentConstantInt("friendchat_settings_delay", 100);
}

/** Ticks between a join and its answer (1 or 2 recorded). */
static int
fc_join_delay(void)
{
    return ToriRSServer_ContentConstantInt("friendchat_join_delay", 1);
}

/** How long a kick bans, at most: the channel emptying ends it sooner. */
static int
fc_ban_ticks(void)
{
    return ToriRSServer_ContentConstantInt("friendchat_ban_ticks", 6000);
}

/* ------------------------------------------------------------------ */
/* Names                                                               */
/* ------------------------------------------------------------------ */

/*
 * Base 37 here is the friends chat's own, not net/jbase37.c's decoder: that
 * one refuses any value at or past 2^60, which is most twelve-character names
 * ("Test FC Name", RSProx index 745 [68], is 3.6e18). A name's real bound is
 * 37^12. Encoding is shared (strtobase37 keeps the first twelve characters,
 * the client's own rule); leading and trailing spaces are dropped first, since
 * a trailing one would encode as a name ending in '_'.
 */
static const char k_base37[] = "_abcdefghijklmnopqrstuvwxyz0123456789";

static int64_t
fc_name37(const char* name)
{
    char trimmed[16];
    size_t len;
    uint64_t name37;

    if( !name )
        return 0;
    while( *name == ' ' )
        name++;
    snprintf(trimmed, sizeof(trimmed), "%.12s", name);
    len = strlen(trimmed);
    while( len > 0 && trimmed[len - 1] == ' ' )
        trimmed[--len] = '\0';
    if( len == 0 )
        return 0;
    name37 = strtobase37(trimmed);
    if( name37 == 0 || name37 % 37 == 0 )
        return 0;
    return (int64_t)name37;
}

/** A base-37 name as the client shows one: underscores to spaces, each word
 *  capitalised ("test_fc_name" -> "Test Fc Name", RSProx index 745 [68]). */
static void
fc_display37(
    int64_t name37,
    char* out,
    int size)
{
    char reversed[16];
    int len = 0;
    int word_start = 1;
    uint64_t value = (uint64_t)name37;

    assert(out);
    assert(size > 0);
    out[0] = '\0';
    if( name37 <= 0 )
        return;
    while( value != 0 && len < 12 )
    {
        reversed[len++] = k_base37[value % 37];
        value /= 37;
    }
    if( value != 0 || len + 1 > size )
        return;
    for( int i = 0; i < len; i++ )
    {
        char c = reversed[len - 1 - i];

        if( c == '_' )
        {
            out[i] = ' ';
            word_start = 1;
            continue;
        }
        out[i] = word_start ? (char)toupper((unsigned char)c) : c;
        word_start = 0;
    }
    out[len] = '\0';
}

/* ------------------------------------------------------------------ */
/* Owners                                                              */
/* ------------------------------------------------------------------ */

static struct FcOwner*
owner_find(int64_t owner37)
{
    if( owner37 == 0 )
        return NULL;
    for( int i = 0; i < g_owner_count; i++ )
        if( g_owners[i].owner37 == owner37 )
            return &g_owners[i];
    return NULL;
}

static void
settings_default(struct FcSettings* settings)
{
    /* A channel nobody has set up: disabled, anyone may enter and talk, only
     * the owner may kick. [unverified: no recording shows a new account's
     * panel; the kick default is the wiki's.] */
    memset(settings, 0, sizeof(*settings));
    settings->enter = TORIRSSERVER_FRIENDCHAT_RANK_ANYONE;
    settings->talk = TORIRSSERVER_FRIENDCHAT_RANK_ANYONE;
    settings->kick = TORIRSSERVER_FRIENDCHAT_RANK_OWNER;
}

static struct FcOwner*
owner_get(int64_t owner37)
{
    struct FcOwner* owner = owner_find(owner37);

    assert(owner37 != 0);
    if( owner )
        return owner;
    if( g_owner_count >= FC_OWNERS_MAX )
    {
        fprintf(stderr, "torirsserver: the friends-chat table is full (%d owners)\n",
                FC_OWNERS_MAX);
        return NULL;
    }
    owner = &g_owners[g_owner_count++];
    memset(owner, 0, sizeof(*owner));
    owner->owner37 = owner37;
    settings_default(&owner->live);
    owner->asked = owner->live;
    fc_display37(owner37, owner->owner_display, (int)sizeof(owner->owner_display));
    return owner;
}

static int
rank_index(
    const struct FcOwner* owner,
    int64_t friend37)
{
    for( int i = 0; i < owner->rank_count; i++ )
        if( owner->ranks[i].friend37 == friend37 )
            return i;
    return -1;
}

/** The rank `who` has in `owner`'s channel: 7 the owner, a friend's rank (0
 *  for none), else -1. */
static int
rank_of(
    const struct FcOwner* owner,
    int64_t who37)
{
    int index;

    if( who37 == owner->owner37 )
        return TORIRSSERVER_FRIENDCHAT_RANK_OWNER;
    if( !ToriRSServer_FriendsIsFriend(owner->owner37, who37) )
        return TORIRSSERVER_FRIENDCHAT_RANK_ANYONE;
    index = rank_index(owner, who37);
    return index >= 0 ? owner->ranks[index].rank : TORIRSSERVER_FRIENDCHAT_RANK_FRIEND;
}

/* ------------------------------------------------------------------ */
/* Players and content                                                 */
/* ------------------------------------------------------------------ */

static struct ToriRSServerPlayer*
player_by_name37(
    struct ToriRSServer* srv,
    int64_t name37)
{
    if( !srv || name37 == 0 )
        return NULL;
    for( int i = 0; i < srv->player_count; i++ )
    {
        struct ToriRSServerPlayer* p = &srv->players[i];

        if( p->active && p->name37 == name37 )
            return p;
    }
    return NULL;
}

/** Run `[proc,<name>]` for `player`, with one string argument or none. The
 *  world's active player is put back afterwards, as social_notify_followers
 *  does: a notice must not leak into the statement that caused it. */
static void
notice(
    struct ToriRSServerPlayer* player,
    const char* proc,
    const char* arg)
{
    struct ToriRSServer* srv;
    struct ToriRSServerPlayer* was_active;
    const char* strv[1];

    assert(player);
    assert(proc);
    srv = player->world;
    if( !srv )
        return;
    was_active = srv->active_player;
    strv[0] = arg;
    ToriRSServer_WorldSetActive(srv, player);
    ToriRSServer_ScriptsRunProcSv(srv, proc, NULL, 0, arg ? strv : NULL, arg ? 1 : 0);
    ToriRSServer_WorldSetActive(srv, was_active);
}

static const char*
member_display(
    struct ToriRSServer* srv,
    int64_t name37,
    char* scratch,
    int size)
{
    struct ToriRSServerPlayer* p = player_by_name37(srv, name37);

    if( p && p->display_name[0] )
        return p->display_name;
    fc_display37(name37, scratch, size);
    return scratch;
}

/* ------------------------------------------------------------------ */
/* The channel                                                         */
/* ------------------------------------------------------------------ */

static int
member_index(
    const struct FcOwner* owner,
    int64_t name37)
{
    for( int i = 0; i < owner->member_count; i++ )
        if( owner->members[i].name37 == name37 )
            return i;
    return -1;
}

static void
send_full(
    struct ToriRSServer* srv,
    const struct FcOwner* owner,
    struct ToriRSServerPlayer* to)
{
    struct ToriRSServerFriendchatMemberOut out[FC_MEMBERS_MAX];
    char names[FC_MEMBERS_MAX][16];

    assert(owner->member_count <= FC_MEMBERS_MAX);
    for( int i = 0; i < owner->member_count; i++ )
    {
        out[i].name = member_display(srv, owner->members[i].name37, names[i], (int)sizeof(names[i]));
        out[i].world = ToriRSServer_FriendsWorld(owner->members[i].name37);
        out[i].rank = owner->members[i].rank;
    }
    ToriRSServer_SendFriendchatChannelFull(to, owner->owner_display, owner->live.prefix37,
                                           owner->live.kick, out, owner->member_count);
}

static void
send_user_to_all(
    struct ToriRSServer* srv,
    const struct FcOwner* owner,
    int64_t who37,
    int rank,
    int64_t skip37)
{
    char scratch[16];
    const char* name = member_display(srv, who37, scratch, (int)sizeof(scratch));
    int world = ToriRSServer_FriendsWorld(who37);

    for( int i = 0; i < owner->member_count; i++ )
    {
        struct ToriRSServerPlayer* p;

        if( owner->members[i].name37 == skip37 )
            continue;
        p = player_by_name37(srv, owner->members[i].name37);
        if( p )
            ToriRSServer_SendFriendchatChannelUser(p, name, world, rank);
    }
}

/** Take `who` out of the channel and tell the rest. The leaver's own packets
 *  are the caller's: a leave, a kick and a removal each word it differently. */
static void
member_remove(
    struct ToriRSServer* srv,
    struct FcOwner* owner,
    int64_t who37)
{
    int index = member_index(owner, who37);
    struct ToriRSServerPlayer* p;

    if( index < 0 )
        return;
    memmove(&owner->members[index], &owner->members[index + 1],
            (size_t)(owner->member_count - index - 1) * sizeof(owner->members[0]));
    owner->member_count--;
    p = player_by_name37(srv, who37);
    if( p && p->friendchat_owner37 == owner->owner37 )
        p->friendchat_owner37 = 0;
    send_user_to_all(srv, owner, who37, TORIRSSERVER_FRIENDCHAT_RANK_GONE, 0);
    if( owner->member_count == 0 )
    {
        /* The channel is gone, and its bans with it. */
        owner->ban_count = 0;
        owner->close_tick = 0;
    }
}

static int
member_add(
    struct FcOwner* owner,
    int64_t who37,
    int rank)
{
    if( owner->member_count >= owner->member_capacity )
    {
        int wanted = owner->member_capacity ? owner->member_capacity * 2 : 8;
        struct FcMember* grown;

        if( wanted > FC_MEMBERS_MAX )
            wanted = FC_MEMBERS_MAX;
        if( wanted <= owner->member_count )
            return 0;
        grown = (struct FcMember*)ToriMem_Realloc(ToriMem_ServerPlayers(), owner->members,
                                                  (size_t)wanted * sizeof(*grown));
        assert(grown);
        owner->members = grown;
        owner->member_capacity = wanted;
    }
    owner->members[owner->member_count].name37 = who37;
    owner->members[owner->member_count].rank = rank;
    owner->member_count++;
    return 1;
}

static int
ban_active(
    struct FcOwner* owner,
    int64_t who37)
{
    for( int i = 0; i < owner->ban_count; i++ )
    {
        if( owner->bans[i].name37 != who37 )
            continue;
        if( owner->bans[i].until_tick > g_now )
            return 1;
        owner->bans[i] = owner->bans[--owner->ban_count];
        return 0;
    }
    return 0;
}

static void
ban_add(
    struct FcOwner* owner,
    int64_t who37)
{
    for( int i = 0; i < owner->ban_count; i++ )
        if( owner->bans[i].name37 == who37 )
        {
            owner->bans[i].until_tick = g_now + fc_ban_ticks();
            return;
        }
    if( owner->ban_count >= FC_BANS_MAX )
    {
        /* The oldest ban makes room: a full ban list must not refuse a kick. */
        memmove(&owner->bans[0], &owner->bans[1], (size_t)(FC_BANS_MAX - 1) * sizeof(owner->bans[0]));
        owner->ban_count--;
    }
    owner->bans[owner->ban_count].name37 = who37;
    owner->bans[owner->ban_count].until_tick = g_now + fc_ban_ticks();
    owner->ban_count++;
}

/* ------------------------------------------------------------------ */
/* Joining                                                             */
/* ------------------------------------------------------------------ */

static void
leave_current(
    struct ToriRSServerPlayer* player,
    int tell)
{
    struct FcOwner* owner = owner_find(player->friendchat_owner37);

    if( owner )
        member_remove(player->world, owner, player->name37);
    player->friendchat_owner37 = 0;
    if( tell )
    {
        notice(player, "[proc,friendchat_left]", NULL);
        ToriRSServer_SendFriendchatChannelLeft(player);
    }
}

/** The answer to a join, on its tick. */
static void
join_answer(
    struct ToriRSServerPlayer* player,
    int64_t owner37)
{
    struct ToriRSServer* srv = player->world;
    struct FcOwner* owner = owner_find(owner37);
    int rank;
    char channel[16];

    if( !owner || !owner->live.enabled || owner->live.prefix37 == 0 )
    {
        notice(player, "[proc,friendchat_notfound]", NULL);
        return;
    }
    if( ToriRSServer_FriendsIsIgnored(owner37, player->name37) )
    {
        notice(player, "[proc,friendchat_join_ignored]", NULL);
        return;
    }
    if( ban_active(owner, player->name37) )
    {
        notice(player, "[proc,friendchat_join_banned]", NULL);
        return;
    }
    rank = rank_of(owner, player->name37);
    if( rank < owner->live.enter )
    {
        notice(player, "[proc,friendchat_join_rank]", NULL);
        return;
    }
    if( owner->member_count >= fc_member_max() )
    {
        /* [unverified: no recording shows a full channel, so there is no line
         * to say; the attempt simply comes to nothing.] */
        return;
    }
    if( !member_add(owner, player->name37, rank) )
        return;
    player->friendchat_owner37 = owner37;

    fc_display37(owner->live.prefix37, channel, (int)sizeof(channel));
    notice(player, "[proc,friendchat_joined]", channel);
    send_full(srv, owner, player);
    send_user_to_all(srv, owner, player->name37, rank, player->name37);
}

void
ToriRSServer_FriendchatJoinLeave(
    struct ToriRSServerPlayer* player,
    const char* name)
{
    int64_t owner37;

    assert(player);
    fc_clock(player);
    if( player->name37 == 0 )
        return;
    if( !name || !name[0] )
    {
        /* The leave: the line and the empty channel on the same tick
         * (RSProx index 753 [954]). */
        player->friendchat_join_owner37 = 0;
        leave_current(player, 1);
        return;
    }
    /* A join while in a channel leaves it first; the client's own
     * chatchannel_login does the same (friendschat_leavechat). */
    if( player->friendchat_owner37 != 0 )
        leave_current(player, 0);

    owner37 = fc_name37(name);
    /* A name the service has never heard of gets the refusal alone; a known
     * player's name says "Attempting..." first (RSProx index 753 [1049..1050]
     * against [1246..1247]). */
    if( owner37 == 0 || (!owner_find(owner37) && ToriRSServer_FriendsWorld(owner37) == 0) )
    {
        player->friendchat_join_owner37 = 0;
        notice(player, "[proc,friendchat_notfound]", NULL);
        return;
    }
    notice(player, "[proc,friendchat_attempting]", NULL);
    player->friendchat_join_owner37 = owner37;
    player->friendchat_join_tick = g_now + fc_join_delay();
}

/* ------------------------------------------------------------------ */
/* Talking and kicking                                                 */
/* ------------------------------------------------------------------ */

void
ToriRSServer_FriendchatSay(
    struct ToriRSServerPlayer* player,
    const char* text,
    int crown)
{
    struct ToriRSServer* srv;
    struct FcOwner* owner;
    char line[256];
    int at = 0;

    assert(player);
    assert(text);
    fc_clock(player);
    srv = player->world;
    owner = owner_find(player->friendchat_owner37);
    if( !owner || member_index(owner, player->name37) < 0 )
        return;

    /* The client sends the slash it was typed with (RSProx index 236
     * [131155]); the channel's line has neither it nor a lower-case start. */
    if( text[0] == '/' )
        text++;
    snprintf(line, sizeof(line), "%s", text);
    while( line[at] == ' ' )
        at++;
    if( !line[at] )
        return;
    line[at] = (char)toupper((unsigned char)line[at]);

    if( rank_of(owner, player->name37) < owner->live.talk )
    {
        notice(player, "[proc,friendchat_talk_refused]", NULL);
        return;
    }
    {
        int32_t counter = ToriRSServer_FriendsNextPmId();
        int world = ToriRSServer_FriendsWorld(player->name37);
        const char* sender = player->display_name[0] ? player->display_name : "";

        for( int i = 0; i < owner->member_count; i++ )
        {
            struct ToriRSServerPlayer* p = player_by_name37(srv, owner->members[i].name37);

            if( p )
                ToriRSServer_SendMessageFriendchannel(p, sender, owner->live.prefix37, world,
                                                      counter, crown, line + at);
        }
    }
}

void
ToriRSServer_FriendchatKick(
    struct ToriRSServerPlayer* player,
    const char* name)
{
    struct ToriRSServer* srv;
    struct FcOwner* owner;
    int64_t target37;
    int kicker_rank;
    struct ToriRSServerPlayer* target;

    assert(player);
    fc_clock(player);
    srv = player->world;
    owner = owner_find(player->friendchat_owner37);
    target37 = fc_name37(name);
    if( !owner || target37 == 0 || member_index(owner, player->name37) < 0 )
        return;
    kicker_rank = rank_of(owner, player->name37);
    /* The kick rank, and nobody at or above the kicker's own. [unverified:
     * only the successful kick is recorded; a refused one says nothing here.] */
    if( kicker_rank < owner->live.kick || target37 == player->name37 ||
        rank_of(owner, target37) >= kicker_rank )
        return;

    ban_add(owner, target37);
    if( member_index(owner, target37) >= 0 )
    {
        target = player_by_name37(srv, target37);
        member_remove(srv, owner, target37);
        if( target )
        {
            notice(target, "[proc,friendchat_kicked]", NULL);
            ToriRSServer_SendFriendchatChannelLeft(target);
        }
    }
    notice(player, "[proc,friendchat_kick_done]", NULL);
}

/* ------------------------------------------------------------------ */
/* Settings and ranks                                                  */
/* ------------------------------------------------------------------ */

static int
settings_equal(
    const struct FcSettings* a,
    const struct FcSettings* b)
{
    return a->enabled == b->enabled && a->prefix37 == b->prefix37 && a->enter == b->enter &&
           a->talk == b->talk && a->kick == b->kick;
}

/** A change was asked for. The first one opens the window; the others ride
 *  in it silently (RSProx index 753 [25] and [75]). */
static enum ToriRSServerFriendchatChange
settings_changed(struct FcOwner* owner)
{
    if( owner->flush_tick != 0 )
        return TORIRSSERVER_FRIENDCHAT_CHANGE_QUIET;
    if( settings_equal(&owner->asked, &owner->live) && !owner->ranks_dirty )
        return TORIRSSERVER_FRIENDCHAT_CHANGE_QUIET;
    owner->flush_tick = g_now + fc_settings_delay();
    return TORIRSSERVER_FRIENDCHAT_CHANGE_PENDING;
}

static struct FcOwner*
owner_of_player(const struct ToriRSServerPlayer* player)
{
    assert(player);
    assert(player->name37 != 0);
    fc_clock(player);
    return owner_get(player->name37);
}

static int
valid_rank_setting(int rank)
{
    return rank >= TORIRSSERVER_FRIENDCHAT_RANK_ANYONE && rank <= TORIRSSERVER_FRIENDCHAT_RANK_OWNER;
}

int
ToriRSServer_FriendchatEnabled(const struct ToriRSServerPlayer* owner)
{
    const struct FcOwner* o = owner_find(owner->name37);

    return o ? o->asked.enabled : 0;
}

int
ToriRSServer_FriendchatEnterRank(const struct ToriRSServerPlayer* owner)
{
    const struct FcOwner* o = owner_find(owner->name37);

    return o ? o->asked.enter : TORIRSSERVER_FRIENDCHAT_RANK_ANYONE;
}

int
ToriRSServer_FriendchatTalkRank(const struct ToriRSServerPlayer* owner)
{
    const struct FcOwner* o = owner_find(owner->name37);

    return o ? o->asked.talk : TORIRSSERVER_FRIENDCHAT_RANK_ANYONE;
}

int
ToriRSServer_FriendchatKickRank(const struct ToriRSServerPlayer* owner)
{
    const struct FcOwner* o = owner_find(owner->name37);

    return o ? o->asked.kick : TORIRSSERVER_FRIENDCHAT_RANK_OWNER;
}

const char*
ToriRSServer_FriendchatPrefix(const struct ToriRSServerPlayer* owner)
{
    static char buffer[16];
    const struct FcOwner* o = owner_find(owner->name37);

    buffer[0] = '\0';
    if( o )
        fc_display37(o->asked.prefix37, buffer, (int)sizeof(buffer));
    return buffer;
}

enum ToriRSServerFriendchatChange
ToriRSServer_FriendchatSetEnterRank(
    struct ToriRSServerPlayer* player,
    int rank)
{
    struct FcOwner* owner;

    assert(valid_rank_setting(rank));
    owner = owner_of_player(player);
    if( !owner )
        return TORIRSSERVER_FRIENDCHAT_CHANGE_QUIET;
    owner->asked.enter = rank;
    return settings_changed(owner);
}

enum ToriRSServerFriendchatChange
ToriRSServer_FriendchatSetTalkRank(
    struct ToriRSServerPlayer* player,
    int rank)
{
    struct FcOwner* owner;

    assert(valid_rank_setting(rank));
    owner = owner_of_player(player);
    if( !owner )
        return TORIRSSERVER_FRIENDCHAT_CHANGE_QUIET;
    owner->asked.talk = rank;
    return settings_changed(owner);
}

enum ToriRSServerFriendchatChange
ToriRSServer_FriendchatSetKickRank(
    struct ToriRSServerPlayer* player,
    int rank)
{
    struct FcOwner* owner;

    /* The kick options start at Corporal+ (chatchannel_setup:opt_kick has no
     * op1..op3). */
    assert(rank >= 2);
    assert(rank <= TORIRSSERVER_FRIENDCHAT_RANK_OWNER);
    owner = owner_of_player(player);
    if( !owner )
        return TORIRSSERVER_FRIENDCHAT_CHANGE_QUIET;
    owner->asked.kick = rank;
    return settings_changed(owner);
}

enum ToriRSServerFriendchatChange
ToriRSServer_FriendchatSetPrefix(
    struct ToriRSServerPlayer* player,
    const char* prefix)
{
    struct FcOwner* owner;
    int64_t prefix37;

    assert(prefix);
    prefix37 = fc_name37(prefix);
    owner = owner_of_player(player);
    if( !owner || prefix37 == 0 )
        return TORIRSSERVER_FRIENDCHAT_CHANGE_QUIET;
    owner->asked.prefix37 = prefix37;
    owner->asked.enabled = 1;
    if( !owner->live.enabled )
    {
        /* Enabling is not batched: the channel is open from this tick
         * (RSProx index 753 [749..757]). Whatever else was pending lands with
         * it. */
        owner->live = owner->asked;
        owner->flush_tick = 0;
        owner->close_tick = 0;
        return TORIRSSERVER_FRIENDCHAT_CHANGE_ENABLED;
    }
    return settings_changed(owner);
}

enum ToriRSServerFriendchatChange
ToriRSServer_FriendchatDisable(struct ToriRSServerPlayer* player)
{
    struct FcOwner* owner = owner_of_player(player);

    if( !owner )
        return TORIRSSERVER_FRIENDCHAT_CHANGE_QUIET;
    owner->asked.enabled = 0;
    return settings_changed(owner);
}

void
ToriRSServer_FriendchatSetRank(
    struct ToriRSServerPlayer* player,
    const char* name,
    int rank)
{
    struct FcOwner* owner;
    int64_t friend37;
    int index;

    assert(player);
    fc_clock(player);
    friend37 = fc_name37(name);
    if( player->name37 == 0 || friend37 == 0 || rank < 0 ||
        rank > TORIRSSERVER_FRIENDCHAT_RANK_GENERAL )
        return;
    if( !ToriRSServer_FriendsIsFriend(player->name37, friend37) )
        return;
    owner = owner_of_player(player);
    if( !owner )
        return;

    index = rank_index(owner, friend37);
    if( rank == TORIRSSERVER_FRIENDCHAT_RANK_FRIEND )
    {
        if( index >= 0 )
            owner->ranks[index] = owner->ranks[--owner->rank_count];
    }
    else if( index >= 0 )
        owner->ranks[index].rank = rank;
    else
    {
        if( owner->rank_count >= owner->rank_capacity )
        {
            int wanted = owner->rank_capacity ? owner->rank_capacity * 2 : 8;
            struct FcRank* grown = (struct FcRank*)ToriMem_Realloc(
                ToriMem_ServerPlayers(), owner->ranks, (size_t)wanted * sizeof(*grown));

            assert(grown);
            owner->ranks = grown;
            owner->rank_capacity = wanted;
        }
        owner->ranks[owner->rank_count].friend37 = friend37;
        owner->ranks[owner->rank_count].rank = rank;
        owner->rank_count++;
    }

    /* The friend list's row with the new rank goes straight back (RSProx
     * index 759 [3957..3979]); the channel hears it at the flush. */
    ToriRSServer_SendUpdateFriendlist(
        player, friend37,
        ToriRSServer_FriendsVisibleTo(player->name37, friend37) ? ToriRSServer_FriendsWorld(friend37)
                                                                : 0,
        rank);
    owner->ranks_dirty = 1;
    if( settings_changed(owner) == TORIRSSERVER_FRIENDCHAT_CHANGE_PENDING )
        notice(player, "[proc,friendchat_changes_pending]", NULL);
}

void
ToriRSServer_FriendchatFriendRemoved(
    int64_t owner37,
    int64_t friend37)
{
    struct FcOwner* owner = owner_find(owner37);
    int index;

    if( !owner )
        return;
    index = rank_index(owner, friend37);
    if( index < 0 )
        return;
    owner->ranks[index] = owner->ranks[--owner->rank_count];
    owner->ranks_dirty = 1;
}

int
ToriRSServer_FriendchatFriendRank(
    int64_t owner37,
    int64_t friend37)
{
    const struct FcOwner* owner = owner_find(owner37);
    int index;

    if( !owner )
        return TORIRSSERVER_FRIENDCHAT_RANK_FRIEND;
    index = rank_index(owner, friend37);
    return index >= 0 ? owner->ranks[index].rank : TORIRSSERVER_FRIENDCHAT_RANK_FRIEND;
}

/* ------------------------------------------------------------------ */
/* The flush                                                           */
/* ------------------------------------------------------------------ */

static void
flush_owner(
    struct ToriRSServer* srv,
    struct FcOwner* owner)
{
    int settings_moved = !settings_equal(&owner->live, &owner->asked);

    owner->flush_tick = 0;
    if( owner->live.enabled && !owner->asked.enabled )
    {
        struct ToriRSServerPlayer* self = player_by_name37(srv, owner->owner37);

        owner->live = owner->asked;
        owner->ranks_dirty = 0;
        if( self )
            notice(self, "[proc,friendchat_disabled]", NULL);
        if( owner->member_count > 0 )
            owner->close_tick = g_now + 1;
        return;
    }
    owner->live = owner->asked;
    if( owner->member_count == 0 )
    {
        owner->ranks_dirty = 0;
        return;
    }

    if( settings_moved )
        for( int i = 0; i < owner->member_count; i++ )
        {
            struct ToriRSServerPlayer* p = player_by_name37(srv, owner->members[i].name37);

            if( p )
                send_full(srv, owner, p);
        }

    /* Every member's rank against the new settings: a changed one is sent
     * again (RSProx index 754 [186]); one now below the entry rank is put
     * out (769 [323]). */
    for( int i = 0; i < owner->member_count; )
    {
        int64_t who37 = owner->members[i].name37;
        int rank = rank_of(owner, who37);

        if( rank < owner->live.enter )
        {
            struct ToriRSServerPlayer* p = player_by_name37(srv, who37);

            member_remove(srv, owner, who37);
            if( p )
            {
                notice(p, "[proc,friendchat_removed]", NULL);
                ToriRSServer_SendFriendchatChannelLeft(p);
            }
            continue;
        }
        if( rank != owner->members[i].rank )
        {
            owner->members[i].rank = rank;
            send_user_to_all(srv, owner, who37, rank, 0);
        }
        i++;
    }
    owner->ranks_dirty = 0;
}

static void
close_channel(
    struct ToriRSServer* srv,
    struct FcOwner* owner)
{
    owner->close_tick = 0;
    while( owner->member_count > 0 )
    {
        int64_t who37 = owner->members[owner->member_count - 1].name37;
        struct ToriRSServerPlayer* p = player_by_name37(srv, who37);

        owner->member_count--;
        if( p )
        {
            p->friendchat_owner37 = 0;
            notice(p, "[proc,friendchat_removed]", NULL);
            ToriRSServer_SendFriendchatChannelLeft(p);
        }
    }
    owner->ban_count = 0;
}

void
ToriRSServer_FriendchatTick(struct ToriRSServer* srv)
{
    assert(srv);
    g_now = srv->tick;

    for( int i = 0; i < srv->player_count; i++ )
    {
        struct ToriRSServerPlayer* p = &srv->players[i];
        int64_t owner37;

        if( !p->active || p->friendchat_join_owner37 == 0 || p->friendchat_join_tick > g_now )
            continue;
        owner37 = p->friendchat_join_owner37;
        p->friendchat_join_owner37 = 0;
        join_answer(p, owner37);
    }
    for( int i = 0; i < g_owner_count; i++ )
    {
        struct FcOwner* owner = &g_owners[i];

        if( owner->flush_tick != 0 && owner->flush_tick <= g_now )
            flush_owner(srv, owner);
        if( owner->close_tick != 0 && owner->close_tick <= g_now )
            close_channel(srv, owner);
    }
}

/* ------------------------------------------------------------------ */
/* Session                                                             */
/* ------------------------------------------------------------------ */

void
ToriRSServer_FriendchatLogin(struct ToriRSServerPlayer* player)
{
    struct FcOwner* owner;

    assert(player);
    if( player->name37 == 0 )
        return;
    player->friendchat_owner37 = 0;
    player->friendchat_join_owner37 = 0;
    owner = owner_find(player->name37);
    if( !owner && player->friendchat_saved )
    {
        owner = owner_get(player->name37);
        if( owner )
        {
            owner->live.enabled = player->friendchat_saved_enabled;
            owner->live.prefix37 = player->friendchat_saved_prefix37;
            if( valid_rank_setting(player->friendchat_saved_enter) )
                owner->live.enter = player->friendchat_saved_enter;
            if( valid_rank_setting(player->friendchat_saved_talk) )
                owner->live.talk = player->friendchat_saved_talk;
            if( player->friendchat_saved_kick >= 2 &&
                player->friendchat_saved_kick <= TORIRSSERVER_FRIENDCHAT_RANK_OWNER )
                owner->live.kick = player->friendchat_saved_kick;
            if( owner->live.prefix37 == 0 )
                owner->live.enabled = 0;
            owner->asked = owner->live;
        }
    }
    if( owner && player->display_name[0] )
        snprintf(owner->owner_display, sizeof(owner->owner_display), "%s", player->display_name);
    /* No channel yet: the login burst says so (RSProx index 753 [0]). */
    ToriRSServer_SendFriendchatChannelLeft(player);
}

const char*
ToriRSServer_FriendchatRejoinName(const struct ToriRSServerPlayer* player)
{
    assert(player);
    return player->friendchat_saved_channel;
}

const char*
ToriRSServer_FriendchatCurrent(const struct ToriRSServerPlayer* player)
{
    const struct FcOwner* owner;

    assert(player);
    owner = owner_find(player->friendchat_owner37);
    return owner ? owner->owner_display : "";
}

void
ToriRSServer_FriendchatLogout(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->friendchat_join_owner37 = 0;
    if( player->friendchat_owner37 != 0 )
        leave_current(player, 0);
}

void
ToriRSServer_FriendchatRename(
    int64_t old37,
    int64_t new37)
{
    struct FcOwner* owner = owner_find(old37);

    if( !owner || old37 == new37 || owner_find(new37) )
        return;
    owner->owner37 = new37;
    fc_display37(new37, owner->owner_display, (int)sizeof(owner->owner_display));
}

int
ToriRSServer_FriendchatSaveFields(
    const struct ToriRSServerPlayer* player,
    int* out_enabled,
    int64_t* out_prefix37,
    int* out_enter,
    int* out_talk,
    int* out_kick,
    char* out_channel,
    int channel_size)
{
    const struct FcOwner* owner;
    const struct FcOwner* in;

    assert(player);
    assert(out_enabled);
    assert(out_prefix37);
    assert(out_enter);
    assert(out_talk);
    assert(out_kick);
    assert(out_channel);
    assert(channel_size > 0);
    owner = owner_find(player->name37);
    in = owner_find(player->friendchat_owner37);
    out_channel[0] = '\0';
    if( in )
        snprintf(out_channel, (size_t)channel_size, "%s", in->owner_display);
    if( owner )
    {
        /* What the owner asked for: a window still open at logout lands. */
        *out_enabled = owner->asked.enabled;
        *out_prefix37 = owner->asked.prefix37;
        *out_enter = owner->asked.enter;
        *out_talk = owner->asked.talk;
        *out_kick = owner->asked.kick;
    }
    else
    {
        struct FcSettings defaults;

        settings_default(&defaults);
        *out_enabled = defaults.enabled;
        *out_prefix37 = defaults.prefix37;
        *out_enter = defaults.enter;
        *out_talk = defaults.talk;
        *out_kick = defaults.kick;
    }
    return owner != NULL || out_channel[0] != '\0';
}

void
ToriRSServer_FriendchatReset(void)
{
    for( int i = 0; i < g_owner_count; i++ )
    {
        ToriMem_Free(ToriMem_ServerPlayers(), g_owners[i].ranks);
        ToriMem_Free(ToriMem_ServerPlayers(), g_owners[i].members);
    }
    memset(g_owners, 0, sizeof(g_owners));
    g_owner_count = 0;
    g_now = 0;
}
