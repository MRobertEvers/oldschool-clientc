#include "game/rs_clan.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ---- a bounds-checked big-endian reader ----------------------------------- */

struct ClanReader
{
    uint8_t const* data;
    int length;
    int position;
    bool failed;
};

static void
reader_init(struct ClanReader* r, uint8_t const* data, int length)
{
    r->data = data;
    r->length = length;
    r->position = 0;
    r->failed = false;
}

static bool
reader_has(struct ClanReader* r, int count)
{
    if( r->failed || r->position + count > r->length )
    {
        r->failed = true;
        return false;
    }
    return true;
}

static int
g1(struct ClanReader* r)
{
    if( !reader_has(r, 1) )
        return 0;
    return r->data[r->position++];
}

static int
g1s(struct ClanReader* r)
{
    return (int)(int8_t)g1(r);
}

static int
g2(struct ClanReader* r)
{
    int const hi = g1(r);
    return (hi << 8) | g1(r);
}

static int
g4(struct ClanReader* r)
{
    /* Two statements: the order two calls in one expression run in is
     * unspecified. */
    uint32_t const hi = (uint32_t)g2(r);
    uint32_t const lo = (uint32_t)g2(r);
    return (int)((hi << 16) | lo);
}

static int64_t
g8(struct ClanReader* r)
{
    uint64_t const hi = (uint32_t)g4(r);
    uint64_t const lo = (uint32_t)g4(r);
    return (int64_t)((hi << 32) | lo);
}

/* gjstr into `out`, truncated to its capacity. */
static void
gjstr(struct ClanReader* r, char* out, int cap)
{
    int written = 0;
    for( ;; )
    {
        if( !reader_has(r, 1) )
            break;
        int const c = r->data[r->position++];
        if( c == 0 )
            break;
        if( written + 1 < cap )
            out[written++] = (char)c;
    }
    if( cap > 0 )
        out[written] = '\0';
}

/* gjstrnull: a leading 0 byte is consumed and the string is absent. */
static bool
gjstrnull(struct ClanReader* r, char* out, int cap)
{
    if( !reader_has(r, 1) )
        return false;
    if( r->data[r->position] == 0 )
    {
        r->position++;
        out[0] = '\0';
        return false;
    }
    gjstr(r, out, cap);
    return true;
}

/* The member-hash prefix of the add events: a 255 byte means "no hash"
 * (-1); anything else is the first byte of a g8. */
static int64_t
g_optional_hash(struct ClanReader* r)
{
    if( !reader_has(r, 1) )
        return -1;
    if( r->data[r->position] == 255 )
    {
        r->position++;
        return -1;
    }
    return g8(r);
}

/* ---- channels ---------------------------------------------------------------- */

static void
channel_free(struct RS_ClanChannel* channel)
{
    if( !channel )
        return;
    free(channel->users);
    free(channel->sorted);
    free(channel);
}

static void
channel_reserve(struct RS_ClanChannel* channel, int count)
{
    if( count <= channel->user_cap )
        return;
    int const cap = count + 5;
    void* const grown = realloc(channel->users, (size_t)cap * sizeof(channel->users[0]));
    assert(grown);
    channel->users = grown;
    channel->user_cap = cap;
}

static void
channel_invalidate_sort(struct RS_ClanChannel* channel)
{
    free(channel->sorted);
    channel->sorted = NULL;
}

/*
 * Replace `*slot` with `replacement`, keeping the old object alive while the
 * active pointer reads it (the client's active pointer is a snapshot).
 */
static void
store_replace_channel(
    struct RS_ClanStore* store,
    struct RS_ClanChannel** slot,
    struct RS_ClanChannel* replacement)
{
    struct RS_ClanChannel* const old = *slot;
    *slot = replacement;
    if( !old )
        return;
    if( store->active_channel == old )
    {
        if( store->retired_channel && store->retired_channel != old )
            channel_free(store->retired_channel);
        store->retired_channel = old;
        return;
    }
    channel_free(old);
}

static struct RS_ClanChannel**
channel_slot(struct RS_ClanStore* store, int clan)
{
    if( clan < 0 )
        return &store->listened_channel;
    if( clan >= RS_CLAN_AFFINED_SLOTS )
        return NULL;
    return &store->affined_channel[clan];
}

bool
RS_ClanStore_ApplyChannelFull(
    struct RS_ClanStore* store,
    uint8_t const* data,
    int length)
{
    assert(store);
    assert(data || length == 0);
    struct ClanReader r;
    reader_init(&r, data, length);
    store->channel_transmit_serial++;
    int const clan = g1s(&r);
    struct RS_ClanChannel** const slot = channel_slot(store, clan);
    if( r.failed || !slot )
        return false;
    if( length == 1 )
    {
        store_replace_channel(store, slot, NULL);
        return true;
    }

    struct RS_ClanChannel* const channel = calloc(1, sizeof(*channel));
    assert(channel);
    int const flags = g1(&r);
    bool const has_hashes = (flags & 1) != 0;
    int version = 2;
    if( flags & 4 )
        version = g1(&r);
    channel->has_display_names = true;
    channel->clan_hash = g8(&r);
    channel->update_num = g8(&r);
    gjstr(&r, channel->name, sizeof(channel->name));
    (void)g1(&r);
    channel->rank_kick = g1s(&r);
    channel->rank_talk = g1s(&r);
    int const count = g2(&r);
    channel_reserve(channel, count);
    for( int i = 0; i < count && !r.failed; i++ )
    {
        struct RS_ClanChannelUser* const user = &channel->users[i];
        if( has_hashes )
            (void)g8(&r);
        gjstr(&r, user->name, sizeof(user->name));
        user->rank = g1s(&r);
        user->world = g2(&r);
        if( version >= 3 )
            (void)g1(&r);
    }
    channel->user_count = count;
    if( r.failed )
    {
        channel_free(channel);
        return false;
    }
    store_replace_channel(store, slot, channel);
    return true;
}

bool
RS_ClanStore_ApplyChannelDelta(
    struct RS_ClanStore* store,
    uint8_t const* data,
    int length,
    int* out_request_full)
{
    assert(store);
    assert(data || length == 0);
    assert(out_request_full);
    *out_request_full = INT32_MIN;
    struct ClanReader r;
    reader_init(&r, data, length);
    store->channel_transmit_serial++;
    int const clan = g1s(&r);
    struct RS_ClanChannel** const slot = channel_slot(store, clan);
    int64_t const hash = g8(&r);
    int64_t const update_num = g8(&r);
    if( r.failed || !slot )
        return false;
    struct RS_ClanChannel* const channel = *slot;
    if( !channel || update_num > channel->update_num )
    {
        *out_request_full = clan;
        return true;
    }
    if( update_num < channel->update_num )
        return true;
    if( hash != channel->clan_hash )
        return false;

    for( int type = g1(&r); type != 0 && !r.failed; type = g1(&r) )
    {
        switch( type )
        {
        case 1: /* add user */
        {
            (void)g_optional_hash(&r);
            struct RS_ClanChannelUser user;
            memset(&user, 0, sizeof(user));
            (void)gjstrnull(&r, user.name, sizeof(user.name));
            user.world = g2(&r);
            user.rank = g1s(&r);
            (void)g8(&r);
            if( r.failed )
                break;
            channel_reserve(channel, channel->user_count + 1);
            channel->users[channel->user_count++] = user;
            channel_invalidate_sort(channel);
            break;
        }
        case 2: /* update user */
        case 5: /* update user, v2 */
        {
            if( type == 5 )
                (void)g1(&r);
            int const index = g2(&r);
            int const rank = g1s(&r);
            int const world = g2(&r);
            (void)g8(&r);
            char name[RS_CLAN_NAME_LEN];
            gjstr(&r, name, sizeof(name));
            if( type == 5 )
                (void)g1(&r);
            if( r.failed || index < 0 || index >= channel->user_count )
                return false;
            channel->users[index].rank = rank;
            channel->users[index].world = world;
            snprintf(channel->users[index].name, sizeof(channel->users[index].name), "%s", name);
            break;
        }
        case 3: /* delete user */
        {
            int const index = g2(&r);
            (void)g1(&r);
            (void)g_optional_hash(&r);
            if( r.failed || index < 0 || index >= channel->user_count )
                return false;
            memmove(&channel->users[index], &channel->users[index + 1],
                (size_t)(channel->user_count - index - 1) * sizeof(channel->users[0]));
            channel->user_count--;
            channel_invalidate_sort(channel);
            break;
        }
        case 4: /* base settings: talk then kick, the reverse of the full packet */
        {
            char name[RS_CLAN_NAME_LEN];
            if( gjstrnull(&r, name, sizeof(name)) )
            {
                snprintf(channel->name, sizeof(channel->name), "%s", name);
                (void)g1(&r);
                channel->rank_talk = g1s(&r);
                channel->rank_kick = g1s(&r);
            }
            break;
        }
        default:
            return false;
        }
    }
    if( r.failed )
        return false;
    channel->update_num++;
    return true;
}

/* ---- settings ---------------------------------------------------------------- */

static void
settings_free(struct RS_ClanSettings* settings)
{
    if( !settings )
        return;
    free(settings->affined_hash);
    free(settings->affined_name);
    free(settings->affined_has_name);
    free(settings->affined_rank);
    free(settings->affined_extra_info);
    free(settings->affined_join_runeday);
    free(settings->affined_muted);
    free(settings->sorted);
    free(settings->banned_hash);
    free(settings->banned_name);
    free(settings->banned_has_name);
    for( int i = 0; i < settings->setting_count; i++ )
        free(settings->settings[i].string_value);
    free(settings->settings);
    free(settings);
}

#define GROW(array, cap) \
    do \
    { \
        void* const grown_ = realloc((array), (size_t)(cap) * sizeof(*(array))); \
        assert(grown_); \
        (array) = grown_; \
    } while( 0 )

static void
settings_reserve_affined(struct RS_ClanSettings* s, int count)
{
    if( count <= s->affined_cap )
        return;
    int const cap = count + 5;
    GROW(s->affined_hash, cap);
    GROW(s->affined_name, cap);
    GROW(s->affined_has_name, cap);
    GROW(s->affined_rank, cap);
    GROW(s->affined_extra_info, cap);
    GROW(s->affined_join_runeday, cap);
    GROW(s->affined_muted, cap);
    s->affined_cap = cap;
}

static void
settings_reserve_banned(struct RS_ClanSettings* s, int count)
{
    if( count <= s->banned_cap )
        return;
    int const cap = count + 5;
    GROW(s->banned_hash, cap);
    GROW(s->banned_name, cap);
    GROW(s->banned_has_name, cap);
    s->banned_cap = cap;
}

/*
 * class245.method6785, exactly: walk the ranks from slot 1 with slot 0 as the
 * running best. A higher rank takes over as owner -- and when the rank it
 * displaces was 125, the displaced slot becomes the replacement owner. A 125
 * that does not take over is the replacement if none has been found yet. The
 * owner's rank then reads 126. No members, no owner and no replacement.
 */
static void
settings_recompute_owners(struct RS_ClanSettings* s)
{
    s->owner_slot = -1;
    s->replacement_owner_slot = -1;
    if( s->affined_count <= 0 )
        return;
    int owner = 0;
    int best = s->affined_rank[0];
    int replacement = -1;
    for( int i = 1; i < s->affined_count; i++ )
    {
        int const rank = s->affined_rank[i];
        if( rank > best )
        {
            if( best == RS_CLAN_RANK_REPLACEMENT_OWNER )
                replacement = owner;
            owner = i;
            best = rank;
        }
        else if( replacement == -1 && rank == RS_CLAN_RANK_REPLACEMENT_OWNER )
            replacement = i;
    }
    s->owner_slot = owner;
    s->replacement_owner_slot = replacement;
    s->affined_rank[owner] = (int8_t)RS_CLAN_RANK_OWNER;
}

static struct RS_ClanSetting*
settings_find(struct RS_ClanSettings* s, int id)
{
    for( int i = 0; i < s->setting_count; i++ )
        if( s->settings[i].id == id )
            return &s->settings[i];
    return NULL;
}

static struct RS_ClanSetting*
settings_put(struct RS_ClanSettings* s, int id, int type)
{
    struct RS_ClanSetting* existing = settings_find(s, id);
    if( !existing )
    {
        if( s->setting_count == s->setting_cap )
        {
            s->setting_cap = s->setting_cap ? s->setting_cap * 2 : 4;
            GROW(s->settings, s->setting_cap);
        }
        existing = &s->settings[s->setting_count++];
        memset(existing, 0, sizeof(*existing));
        existing->id = id;
    }
    free(existing->string_value);
    existing->string_value = NULL;
    existing->type = type;
    return existing;
}

static void
settings_invalidate_sort(struct RS_ClanSettings* s)
{
    free(s->sorted);
    s->sorted = NULL;
}

static struct RS_ClanSettings**
settings_slot(struct RS_ClanStore* store, int clan)
{
    if( clan < 0 )
        return &store->listened_settings;
    if( clan >= RS_CLAN_AFFINED_SLOTS )
        return NULL;
    return &store->affined_settings[clan];
}

static void
store_replace_settings(
    struct RS_ClanStore* store,
    struct RS_ClanSettings** slot,
    struct RS_ClanSettings* replacement)
{
    struct RS_ClanSettings* const old = *slot;
    *slot = replacement;
    if( !old )
        return;
    if( store->active_settings == old )
    {
        if( store->retired_settings && store->retired_settings != old )
            settings_free(store->retired_settings);
        store->retired_settings = old;
        return;
    }
    settings_free(old);
}

bool
RS_ClanStore_ApplySettingsFull(
    struct RS_ClanStore* store,
    uint8_t const* data,
    int length)
{
    assert(store);
    assert(data || length == 0);
    struct ClanReader r;
    reader_init(&r, data, length);
    store->settings_transmit_serial++;
    int const clan = g1s(&r);
    struct RS_ClanSettings** const slot = settings_slot(store, clan);
    if( r.failed || !slot )
        return false;
    if( length == 1 )
    {
        store_replace_settings(store, slot, NULL);
        return true;
    }

    int const version = g1(&r);
    if( version < 1 || version > 6 )
        return false;
    struct RS_ClanSettings* const s = calloc(1, sizeof(*s));
    assert(s);
    int const flags = g1(&r);
    s->has_hashes = (flags & 1) != 0;
    s->has_display_names = (flags & 2) != 0;
    s->update_num = g4(&r);
    s->creation_time = g4(&r);
    if( version <= 3 && s->creation_time != 0 )
        s->creation_time += 16912800;
    int const affined = g2(&r);
    int const banned = g1(&r);
    gjstr(&r, s->name, sizeof(s->name));
    if( version >= 4 )
        (void)g4(&r);
    s->allow_unaffined = g1(&r) == 1;
    s->rank_talk = g1s(&r);
    s->rank_kick = g1s(&r);
    s->rank_lootshare = g1s(&r);
    s->rank_coinshare = g1s(&r);

    settings_reserve_affined(s, affined);
    for( int i = 0; i < affined && !r.failed; i++ )
    {
        s->affined_hash[i] = s->has_hashes ? g8(&r) : 0;
        s->affined_has_name[i] =
            s->has_display_names && gjstrnull(&r, s->affined_name[i], RS_CLAN_NAME_LEN);
        if( !s->affined_has_name[i] )
            s->affined_name[i][0] = '\0';
        s->affined_rank[i] = (int8_t)g1s(&r);
        s->affined_extra_info[i] = version >= 2 ? g4(&r) : 0;
        s->affined_join_runeday[i] = version >= 5 ? g2(&r) : 0;
        s->affined_muted[i] = version >= 6 ? g1(&r) == 1 : false;
    }
    s->affined_count = affined;
    settings_recompute_owners(s);

    settings_reserve_banned(s, banned);
    for( int i = 0; i < banned && !r.failed; i++ )
    {
        s->banned_hash[i] = s->has_hashes ? g8(&r) : 0;
        s->banned_has_name[i] =
            s->has_display_names && gjstrnull(&r, s->banned_name[i], RS_CLAN_NAME_LEN);
        if( !s->banned_has_name[i] )
            s->banned_name[i][0] = '\0';
    }
    s->banned_count = banned;

    if( version >= 3 )
    {
        int const count = g2(&r);
        for( int i = 0; i < count && !r.failed; i++ )
        {
            int const key = g4(&r);
            int const id = key & 0x3FFFFFFF;
            int const type = (int)((unsigned)key >> 30);
            if( type == RS_CLAN_SETTING_INT )
                settings_put(s, id, type)->int_value = g4(&r);
            else if( type == RS_CLAN_SETTING_LONG )
                settings_put(s, id, type)->long_value = g8(&r);
            else if( type == RS_CLAN_SETTING_STRING )
            {
                char value[RS_CLAN_NAME_LEN + 1];
                gjstr(&r, value, sizeof(value));
                char* const copy = strdup(value);
                assert(copy);
                settings_put(s, id, type)->string_value = copy;
            }
        }
    }
    if( r.failed )
    {
        settings_free(s);
        return false;
    }
    store_replace_settings(store, slot, s);
    return true;
}

/* class245.method6776, the two add-member events. */
static bool
settings_add_member(
    struct RS_ClanSettings* s,
    int64_t hash,
    char const* name,
    bool has_name,
    int join_runeday)
{
    if( (hash > 0) != s->has_hashes || has_name != s->has_display_names )
        return false;
    settings_reserve_affined(s, s->affined_count + 1);
    int const i = s->affined_count;
    s->affined_hash[i] = hash;
    snprintf(s->affined_name[i], RS_CLAN_NAME_LEN, "%s", has_name ? name : "");
    s->affined_has_name[i] = has_name;
    if( s->owner_slot == -1 )
    {
        s->affined_rank[i] = (int8_t)RS_CLAN_RANK_OWNER;
        s->owner_slot = i;
    }
    else
        s->affined_rank[i] = 0;
    s->affined_extra_info[i] = 0;
    s->affined_join_runeday[i] = join_runeday;
    s->affined_muted[i] = false;
    s->affined_count++;
    settings_invalidate_sort(s);
    return true;
}

static void
settings_shift_affined(struct RS_ClanSettings* s, int index)
{
    int const tail = s->affined_count - index - 1;
#define SHIFT(array) memmove(&(array)[index], &(array)[index + 1], (size_t)tail * sizeof((array)[0]))
    SHIFT(s->affined_hash);
    SHIFT(s->affined_name);
    SHIFT(s->affined_has_name);
    SHIFT(s->affined_rank);
    SHIFT(s->affined_extra_info);
    SHIFT(s->affined_join_runeday);
    SHIFT(s->affined_muted);
#undef SHIFT
}

static uint32_t
extra_info_mask(int start_bit, int end_bit)
{
    uint32_t const high = end_bit == 31 ? 0xFFFFFFFFu : ((1u << ((end_bit + 1) & 31)) - 1u);
    uint32_t const low = (1u << (start_bit & 31)) - 1u;
    return high ^ low;
}

bool
RS_ClanStore_ApplySettingsDelta(
    struct RS_ClanStore* store,
    uint8_t const* data,
    int length,
    int* out_request_full)
{
    assert(store);
    assert(data || length == 0);
    assert(out_request_full);
    *out_request_full = INT32_MIN;
    struct ClanReader r;
    reader_init(&r, data, length);
    store->settings_transmit_serial++;
    int const clan = g1s(&r);
    struct RS_ClanSettings** const slot = settings_slot(store, clan);
    int64_t const owner = g8(&r);
    int const update_num = g4(&r);
    if( r.failed || !slot )
        return false;
    struct RS_ClanSettings* const s = *slot;
    if( !s || update_num > s->update_num )
    {
        *out_request_full = clan;
        return true;
    }
    if( update_num < s->update_num )
        return true;
    /* The client never assigns its settings' owner field, so a delta for any
     * owner but 0 is rejected. */
    if( owner != 0 )
        return false;

    for( int type = g1(&r); type != 0 && !r.failed; type = g1(&r) )
    {
        switch( type )
        {
        case 1: /* add member */
        case 13: /* add member, with join runeday */
        {
            int64_t const hash = g_optional_hash(&r);
            char name[RS_CLAN_NAME_LEN];
            bool has_name = gjstrnull(&r, name, sizeof(name));
            int const join_runeday = type == 13 ? g2(&r) : 0;
            if( has_name && !name[0] )
                has_name = false;
            if( r.failed || !settings_add_member(s, hash, name, has_name, join_runeday) )
                return false;
            break;
        }
        case 2: /* set rank */
        {
            int const index = g2(&r);
            int const rank = g1s(&r);
            if( r.failed || index < 0 || index >= s->affined_count )
                return false;
            if( rank == RS_CLAN_RANK_OWNER || rank == 127 )
                break;
            if( index == s->owner_slot &&
                (s->replacement_owner_slot == -1 ||
                    s->affined_rank[s->replacement_owner_slot] < RS_CLAN_RANK_REPLACEMENT_OWNER) )
                break;
            if( s->affined_rank[index] == rank )
                break;
            s->affined_rank[index] = (int8_t)rank;
            settings_recompute_owners(s);
            break;
        }
        case 3: /* add banned */
        {
            int64_t const hash = g_optional_hash(&r);
            char name[RS_CLAN_NAME_LEN];
            bool has_name = gjstrnull(&r, name, sizeof(name));
            if( has_name && !name[0] )
                has_name = false;
            if( r.failed || (hash > 0) != s->has_hashes || has_name != s->has_display_names )
                return false;
            settings_reserve_banned(s, s->banned_count + 1);
            s->banned_hash[s->banned_count] = hash;
            snprintf(s->banned_name[s->banned_count], RS_CLAN_NAME_LEN, "%s", has_name ? name : "");
            s->banned_has_name[s->banned_count] = has_name;
            s->banned_count++;
            break;
        }
        case 4: /* base settings */
            s->allow_unaffined = g1(&r) == 1;
            s->rank_talk = g1s(&r);
            s->rank_kick = g1s(&r);
            s->rank_lootshare = g1s(&r);
            s->rank_coinshare = g1s(&r);
            break;
        case 5: /* delete member */
        {
            int const index = g2(&r);
            if( r.failed || index < 0 || index >= s->affined_count )
                return false;
            settings_shift_affined(s, index);
            s->affined_count--;
            settings_invalidate_sort(s);
            if( s->affined_count == 0 )
            {
                s->owner_slot = -1;
                s->replacement_owner_slot = -1;
            }
            else
                settings_recompute_owners(s);
            break;
        }
        case 6: /* delete banned */
        {
            int const index = g2(&r);
            if( r.failed || index < 0 || index >= s->banned_count )
                return false;
            int const tail = s->banned_count - index - 1;
            memmove(&s->banned_hash[index], &s->banned_hash[index + 1], (size_t)tail * sizeof(s->banned_hash[0]));
            memmove(&s->banned_name[index], &s->banned_name[index + 1], (size_t)tail * sizeof(s->banned_name[0]));
            memmove(&s->banned_has_name[index], &s->banned_has_name[index + 1],
                (size_t)tail * sizeof(s->banned_has_name[0]));
            s->banned_count--;
            break;
        }
        case 7: /* member extra-info bits */
        {
            int const index = g2(&r);
            int const value = g4(&r);
            int const start_bit = g1(&r);
            int const end_bit = g1(&r);
            if( r.failed || index < 0 || index >= s->affined_count )
                return false;
            uint32_t const mask = extra_info_mask(start_bit, end_bit);
            s->affined_extra_info[index] = (int)(((uint32_t)s->affined_extra_info[index] & ~mask) |
                                                 (((uint32_t)value << (start_bit & 31)) & mask));
            break;
        }
        case 8: /* int setting */
        {
            int const id = g4(&r);
            int const value = g4(&r);
            settings_put(s, id, RS_CLAN_SETTING_INT)->int_value = value;
            break;
        }
        case 9: /* long setting */
        {
            int const id = g4(&r);
            int64_t const value = g8(&r);
            settings_put(s, id, RS_CLAN_SETTING_LONG)->long_value = value;
            break;
        }
        case 10: /* string setting, at most 80 characters */
        {
            int const id = g4(&r);
            char value[RS_CLAN_NAME_LEN + 1];
            gjstr(&r, value, sizeof(value));
            char* const copy = strdup(value);
            assert(copy);
            settings_put(s, id, RS_CLAN_SETTING_STRING)->string_value = copy;
            break;
        }
        case 11: /* varbit setting: merge bits into an int setting */
        {
            int const id = g4(&r);
            int const value = g4(&r);
            int const start_bit = g1(&r);
            int const end_bit = g1(&r);
            uint32_t const mask = extra_info_mask(start_bit, end_bit);
            struct RS_ClanSetting* const existing = settings_find(s, id);
            uint32_t const old = existing && existing->type == RS_CLAN_SETTING_INT ? (uint32_t)existing->int_value : 0;
            int const merged = (int)((old & ~mask) | (((uint32_t)value << (start_bit & 31)) & mask));
            settings_put(s, id, RS_CLAN_SETTING_INT)->int_value = merged;
            break;
        }
        case 12: /* clan name */
            gjstr(&r, s->name, sizeof(s->name));
            (void)g4(&r);
            break;
        case 14: /* member muted */
        {
            int const index = g2(&r);
            bool const muted = g1(&r) == 1;
            if( r.failed || index < 0 || index >= s->affined_count )
                return false;
            s->affined_muted[index] = muted;
            break;
        }
        case 15: /* set owner: the old owner becomes the replacement */
        {
            int const index = g2(&r);
            if( r.failed || index < 0 || index >= s->affined_count )
                return false;
            if( index != s->owner_slot && s->affined_rank[index] != RS_CLAN_RANK_OWNER )
            {
                if( s->owner_slot >= 0 )
                {
                    s->affined_rank[s->owner_slot] = (int8_t)RS_CLAN_RANK_REPLACEMENT_OWNER;
                    s->replacement_owner_slot = s->owner_slot;
                }
                s->affined_rank[index] = (int8_t)RS_CLAN_RANK_OWNER;
                s->owner_slot = index;
            }
            break;
        }
        default:
            return false;
        }
    }
    if( r.failed )
        return false;
    s->update_num++;
    return true;
}

/* ---- the store ------------------------------------------------------------- */

void
RS_ClanStore_Init(struct RS_ClanStore* store)
{
    assert(store);
    memset(store, 0, sizeof(*store));
}

static void
varclans_clear(struct RS_ClanStore* store)
{
    for( int i = 0; i < store->varclan_count; i++ )
        free(store->varclans[i].string_value);
    store->varclan_count = 0;
}

void
RS_ClanStore_Reset(struct RS_ClanStore* store, bool include_varclan)
{
    assert(store);
    for( int i = 0; i < RS_CLAN_AFFINED_SLOTS; i++ )
    {
        channel_free(store->affined_channel[i]);
        store->affined_channel[i] = NULL;
        settings_free(store->affined_settings[i]);
        store->affined_settings[i] = NULL;
    }
    channel_free(store->listened_channel);
    store->listened_channel = NULL;
    settings_free(store->listened_settings);
    store->listened_settings = NULL;
    channel_free(store->retired_channel);
    store->retired_channel = NULL;
    settings_free(store->retired_settings);
    store->retired_settings = NULL;
    /* The client's active pointers survive a logout pointing at objects the
     * reset dropped; here the objects are gone, so the pointers go too. */
    store->active_channel = NULL;
    store->active_settings = NULL;
    if( include_varclan )
    {
        varclans_clear(store);
        store->varclan_enabled = false;
    }
}

void
RS_ClanStore_Free(struct RS_ClanStore* store)
{
    if( !store )
        return;
    RS_ClanStore_Reset(store, true);
    free(store->varclans);
    store->varclans = NULL;
    store->varclan_cap = 0;
}

void
RS_ClanStore_VarClanEnable(struct RS_ClanStore* store)
{
    assert(store);
    varclans_clear(store);
    store->varclan_enabled = true;
}

void
RS_ClanStore_VarClanDisable(struct RS_ClanStore* store)
{
    assert(store);
    varclans_clear(store);
    store->varclan_enabled = false;
}

bool
RS_ClanStore_ApplyVarClan(
    struct RS_ClanStore* store,
    uint8_t const* data,
    int length,
    int (*type_of)(void* user, int var_id),
    void* type_user)
{
    assert(store);
    assert(data || length == 0);
    assert(type_of);
    struct ClanReader r;
    reader_init(&r, data, length);
    int const id = g2(&r);
    if( r.failed )
        return false;
    int const type = type_of(type_user, id);
    struct RS_VarClan value;
    memset(&value, 0, sizeof(value));
    value.id = id;
    value.type = type;
    switch( type )
    {
    case RS_VARCLAN_INT:
        value.int_value = g4(&r);
        break;
    case RS_VARCLAN_LONG:
        value.long_value = g8(&r);
        break;
    case RS_VARCLAN_STRING:
    {
        char text[256];
        (void)g1(&r); /* gjstr2's version byte */
        gjstr(&r, text, sizeof(text));
        value.string_value = strdup(text);
        assert(value.string_value);
        break;
    }
    default:
        return false;
    }
    if( r.failed )
    {
        free(value.string_value);
        return false;
    }
    /* A VARCLAN with no profile creates one. */
    store->varclan_enabled = true;
    for( int i = 0; i < store->varclan_count; i++ )
    {
        if( store->varclans[i].id != id )
            continue;
        free(store->varclans[i].string_value);
        store->varclans[i] = value;
        return true;
    }
    if( store->varclan_count == store->varclan_cap )
    {
        store->varclan_cap = store->varclan_cap ? store->varclan_cap * 2 : 16;
        GROW(store->varclans, store->varclan_cap);
    }
    store->varclans[store->varclan_count++] = value;
    return true;
}

struct RS_VarClan const*
RS_ClanStore_VarClan(
    struct RS_ClanStore const* store,
    int id)
{
    assert(store);
    for( int i = 0; i < store->varclan_count; i++ )
        if( store->varclans[i].id == id )
            return &store->varclans[i];
    return NULL;
}

/* ---- script reads ---------------------------------------------------------- */

bool
RS_ClanStore_FindSettings(
    struct RS_ClanStore* store,
    int index)
{
    assert(store);
    assert(index < RS_CLAN_AFFINED_SLOTS);
    struct RS_ClanSettings* const found = index < 0 ? store->listened_settings : store->affined_settings[index];
    if( !found )
        return false;
    if( store->active_settings != found && store->retired_settings == store->active_settings )
    {
        settings_free(store->retired_settings);
        store->retired_settings = NULL;
    }
    store->active_settings = found;
    return true;
}

bool
RS_ClanStore_FindChannel(
    struct RS_ClanStore* store,
    int index)
{
    assert(store);
    assert(index < RS_CLAN_AFFINED_SLOTS);
    struct RS_ClanChannel* const found = index < 0 ? store->listened_channel : store->affined_channel[index];
    if( !found )
        return false;
    if( store->active_channel != found && store->retired_channel == store->active_channel )
    {
        channel_free(store->retired_channel);
        store->retired_channel = NULL;
    }
    store->active_channel = found;
    if( index >= 0 )
        store->active_channel_affined_index = index;
    return true;
}

static void
lowercase_into(char* out, int cap, char const* in)
{
    int i = 0;
    for( ; in[i] && i + 1 < cap; i++ )
        out[i] = (char)tolower((unsigned char)in[i]);
    out[i] = '\0';
}

/* Sort slots by a per-slot key, keys absent sorting last. Insertion sort: the
 * lists are a clan's worth of members. */
static void
sort_slots(int* slots, int count, char const* const* keys)
{
    for( int i = 0; i < count; i++ )
        slots[i] = i;
    for( int i = 1; i < count; i++ )
    {
        for( int j = i; j > 0; j-- )
        {
            char const* const a = keys[slots[j - 1]];
            char const* const b = keys[slots[j]];
            bool const out_of_order = a == NULL ? b != NULL : (b != NULL && strcmp(a, b) > 0);
            if( !out_of_order )
                break;
            int const t = slots[j - 1];
            slots[j - 1] = slots[j];
            slots[j] = t;
        }
    }
}

/* 3818's order: lower-cased names, absent names last. */
static int const*
settings_sorted(struct RS_ClanSettings* s)
{
    if( s->sorted || s->affined_count == 0 )
        return s->sorted;
    char (*lower)[RS_CLAN_NAME_LEN] = malloc((size_t)s->affined_count * sizeof(*lower));
    char const** keys = malloc((size_t)s->affined_count * sizeof(*keys));
    s->sorted = malloc((size_t)s->affined_count * sizeof(int));
    assert(lower);
    assert(keys);
    assert(s->sorted);
    for( int i = 0; i < s->affined_count; i++ )
    {
        lowercase_into(lower[i], RS_CLAN_NAME_LEN, s->affined_name[i]);
        keys[i] = s->affined_has_name[i] ? lower[i] : NULL;
    }
    sort_slots(s->sorted, s->affined_count, keys);
    free(keys);
    free(lower);
    return s->sorted;
}

int
RS_ClanSettings_SortedAffinedSlot(
    struct RS_ClanSettings* settings,
    int position)
{
    assert(settings);
    if( position < 0 || position >= settings->affined_count )
        return -1;
    return settings_sorted(settings)[position];
}

/* 3817: a case-insensitive binary search over 3818's order. */
int
RS_ClanSettings_AffinedSlotByName(
    struct RS_ClanSettings* settings,
    char const* name)
{
    assert(settings);
    assert(name);
    if( !name[0] || settings->affined_count == 0 )
        return -1;
    char key[RS_CLAN_NAME_LEN];
    lowercase_into(key, sizeof(key), name);
    int const* const sorted = settings_sorted(settings);
    int lo = 0;
    int hi = settings->affined_count - 1;
    while( lo <= hi )
    {
        int const mid = (lo + hi) / 2;
        int const slot = sorted[mid];
        if( !settings->affined_has_name[slot] )
        {
            hi = mid - 1;
            continue;
        }
        char probe[RS_CLAN_NAME_LEN];
        lowercase_into(probe, sizeof(probe), settings->affined_name[slot]);
        int const cmp = strcmp(probe, key);
        if( cmp < 0 )
            lo = mid + 1;
        else if( cmp > 0 )
            hi = mid - 1;
        else
            return slot;
    }
    return -1;
}

int
RS_ClanSettings_ExtraInfoBits(
    struct RS_ClanSettings const* settings,
    int slot,
    int start_bit,
    int end_bit)
{
    assert(settings);
    assert(slot >= 0);
    assert(slot < settings->affined_count);
    uint32_t const mask = end_bit == 31 ? 0xFFFFFFFFu : ((1u << ((end_bit + 1) & 31)) - 1u);
    return (int)(((uint32_t)settings->affined_extra_info[slot] & mask) >> (start_bit & 31));
}

bool
RS_ClanSettings_IntSetting(
    struct RS_ClanSettings const* settings,
    int id,
    int* out_value)
{
    assert(settings);
    assert(out_value);
    for( int i = 0; i < settings->setting_count; i++ )
    {
        if( settings->settings[i].id != id )
            continue;
        if( settings->settings[i].type != RS_CLAN_SETTING_INT )
            return false;
        *out_value = settings->settings[i].int_value;
        return true;
    }
    return false;
}

int
RS_ClanChannel_UserSlotByName(
    struct RS_ClanChannel const* channel,
    char const* name)
{
    assert(channel);
    assert(name);
    for( int i = 0; i < channel->user_count; i++ )
        if( strcasecmp(channel->users[i].name, name) == 0 )
            return i;
    return -1;
}

/* 3861's order: names normalised the way usernames compare -- case folded, a
 * space equal to an underscore. */
int
RS_ClanChannel_SortedUserSlot(
    struct RS_ClanChannel* channel,
    int position)
{
    assert(channel);
    if( position < 0 || position >= channel->user_count )
        return -1;
    if( !channel->sorted )
    {
        char (*normal)[RS_CLAN_NAME_LEN] = malloc((size_t)channel->user_count * sizeof(*normal));
        char const** keys = malloc((size_t)channel->user_count * sizeof(*keys));
        channel->sorted = malloc((size_t)channel->user_count * sizeof(int));
        assert(normal);
        assert(keys);
        assert(channel->sorted);
        for( int i = 0; i < channel->user_count; i++ )
        {
            lowercase_into(normal[i], RS_CLAN_NAME_LEN, channel->users[i].name);
            for( char* c = normal[i]; *c; c++ )
                if( *c == ' ' )
                    *c = '_';
            keys[i] = normal[i];
        }
        sort_slots(channel->sorted, channel->user_count, keys);
        free(keys);
        free(normal);
    }
    return channel->sorted[position];
}
