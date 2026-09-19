/*
 * The friends-chat and clan stores, fed the server's bytes.
 *
 * These are the decoders behind 3611-3627 and 3800-3890, and every field they
 * read is positional: one width wrong and each field after it reads its
 * neighbour's bytes, while the packet still frames cleanly. So each case builds
 * the payload the way the server writes it and checks the fields the scripts
 * read, including the ones after a variable-width run.
 */

#include "game/rs_clan.h"
#include "game/rs_friends_chat.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;

#define TEST_ASSERT(cond, msg)                                                   \
    do                                                                           \
    {                                                                            \
        if( !(cond) )                                                            \
        {                                                                        \
            fprintf(stderr, "FAIL %s:%d: %s (%s)\n", __FILE__, __LINE__, msg, #cond); \
            g_failures++;                                                        \
        }                                                                        \
    } while( 0 )

struct Writer
{
    uint8_t data[1024];
    int length;
};

static void
p1(struct Writer* w, int value)
{
    assert(w->length < (int)sizeof(w->data));
    w->data[w->length++] = (uint8_t)value;
}

static void
p2(struct Writer* w, int value)
{
    p1(w, value >> 8);
    p1(w, value);
}

static void
p4(struct Writer* w, int value)
{
    p2(w, (int)((uint32_t)value >> 16));
    p2(w, value);
}

static void
p8(struct Writer* w, int64_t value)
{
    p4(w, (int)((uint64_t)value >> 32));
    p4(w, (int)value);
}

static void
pjstr(struct Writer* w, char const* text)
{
    for( ; *text; text++ )
        p1(w, *text);
    p1(w, 0);
}

/* ---- friends chat ---------------------------------------------------------- */

static void
test_friends_chat(void)
{
    struct RS_FriendsChat chat;
    struct Writer w = { .length = 0 };

    RS_FriendsChat_Init(&chat);
    pjstr(&w, "Owner Name");
    p8(&w, 0); /* channel name, base37: 0 is the empty name */
    p1(&w, 2); /* kick rank */
    p1(&w, 3); /* smart1or2null: two members */
    pjstr(&w, "Alice");
    p2(&w, 301);
    p1(&w, 0xFF); /* rank -1 */
    pjstr(&w, "World 301");
    pjstr(&w, "Bob");
    p2(&w, 302);
    p1(&w, 4);
    pjstr(&w, "World 302");
    TEST_ASSERT(RS_FriendsChat_ApplyFull(&chat, w.data, w.length, "bob"), "full decodes");
    TEST_ASSERT(chat.in_channel, "joined");
    TEST_ASSERT(strcmp(chat.owner, "Owner Name") == 0, "owner");
    TEST_ASSERT(chat.kick_rank == 2, "kick rank");
    TEST_ASSERT(chat.member_count == 2, "two members");
    TEST_ASSERT(chat.members[0].world == 301, "first member's world");
    TEST_ASSERT(chat.members[0].rank == -1, "rank is a signed byte");
    TEST_ASSERT(strcmp(chat.members[1].name, "Bob") == 0, "second member after a string");
    TEST_ASSERT(chat.members[1].rank == 4, "second member's rank");
    TEST_ASSERT(chat.local_rank == 4, "local rank matched case-folded");

    /* SINGLEUSER: add Carol, then remove Alice -- only on the world named. */
    w.length = 0;
    pjstr(&w, "Carol");
    p2(&w, 303);
    p1(&w, 1);
    pjstr(&w, "World 303");
    TEST_ASSERT(RS_FriendsChat_ApplySingleUser(&chat, w.data, w.length, "bob"), "add decodes");
    TEST_ASSERT(chat.member_count == 3, "Carol added");

    w.length = 0;
    pjstr(&w, "alice");
    p2(&w, 999);
    p1(&w, 0x80); /* -128: remove */
    TEST_ASSERT(RS_FriendsChat_ApplySingleUser(&chat, w.data, w.length, "bob"), "remove decodes");
    TEST_ASSERT(chat.member_count == 3, "a remove for another world keeps the entry");

    w.length = 0;
    pjstr(&w, "alice");
    p2(&w, 301);
    p1(&w, 0x80);
    TEST_ASSERT(RS_FriendsChat_ApplySingleUser(&chat, w.data, w.length, "bob"), "remove decodes");
    TEST_ASSERT(chat.member_count == 2, "Alice removed");
    TEST_ASSERT(strcmp(chat.members[0].name, "Bob") == 0, "the rest move up");

    /* count -1 refreshes the header and keeps the members. */
    w.length = 0;
    pjstr(&w, "New Owner");
    p8(&w, 0);
    p1(&w, 5);
    p1(&w, 0); /* smart1or2null 0 = -1 */
    TEST_ASSERT(RS_FriendsChat_ApplyFull(&chat, w.data, w.length, "bob"), "refresh decodes");
    TEST_ASSERT(chat.member_count == 2, "no list keeps the members");
    TEST_ASSERT(chat.kick_rank == 5, "header refreshed");

    int const transmit = chat.transmit_serial;
    TEST_ASSERT(RS_FriendsChat_ApplyFull(&chat, NULL, 0, "bob"), "leave");
    TEST_ASSERT(!chat.in_channel, "an empty payload leaves");
    TEST_ASSERT(chat.transmit_serial == transmit + 1, "the leave still transmits");

    /* Truncated: the member's world is cut off. */
    RS_FriendsChat_Init(&chat);
    w.length = 0;
    pjstr(&w, "Owner");
    p8(&w, 0);
    p1(&w, 0);
    p1(&w, 2);
    pjstr(&w, "Alice");
    p1(&w, 1);
    TEST_ASSERT(!RS_FriendsChat_ApplyFull(&chat, w.data, w.length, ""), "truncated full fails");
}

/* ---- clan channel ---------------------------------------------------------- */

static void
write_channel_full(struct Writer* w, int clan, int64_t update_num)
{
    w->length = 0;
    p1(w, clan);
    p1(w, 0); /* flags: no hashes, version 2 */
    p8(w, 0x1122334455667788LL);
    p8(w, update_num);
    pjstr(w, "Clan Name");
    p1(w, 0);
    p1(w, 3);    /* kick */
    p1(w, 0xFF); /* talk -1 */
    p2(w, 2);
    pjstr(w, "Zed");
    p1(w, 10);
    p2(w, 301);
    pjstr(w, "amy");
    p1(w, 126);
    p2(w, 302);
}

static void
test_clan_channel(void)
{
    struct RS_ClanStore store;
    struct Writer w;
    int request = 0;

    RS_ClanStore_Init(&store);
    write_channel_full(&w, 0, 7);
    TEST_ASSERT(RS_ClanStore_ApplyChannelFull(&store, w.data, w.length), "channel full decodes");
    struct RS_ClanChannel* channel = store.affined_channel[0];
    TEST_ASSERT(channel != NULL, "affined slot 0 filled");
    if( !channel )
        return;
    TEST_ASSERT(channel->clan_hash == 0x1122334455667788LL, "hash is a g8");
    TEST_ASSERT(strcmp(channel->name, "Clan Name") == 0, "name");
    TEST_ASSERT(channel->rank_kick == 3, "kick");
    TEST_ASSERT(channel->rank_talk == -1, "talk is signed");
    TEST_ASSERT(channel->user_count == 2, "two users");
    TEST_ASSERT(channel->users[1].world == 302, "second user's world");
    TEST_ASSERT(RS_ClanChannel_UserSlotByName(channel, "ZED") == 0, "slot by name, any case");
    TEST_ASSERT(RS_ClanChannel_SortedUserSlot(channel, 0) == 1, "amy sorts first");

    /* A delta at the channel's update number: add a user, delete slot 0. */
    w.length = 0;
    p1(&w, 0);
    p8(&w, 0x1122334455667788LL);
    p8(&w, 7);
    p1(&w, 1); /* add */
    p1(&w, 255); /* no hash */
    pjstr(&w, "Kim");
    p2(&w, 303);
    p1(&w, 5);
    p8(&w, 0);
    p1(&w, 3); /* delete */
    p2(&w, 0);
    p1(&w, 0);
    p1(&w, 255);
    p1(&w, 0); /* end */
    TEST_ASSERT(
        RS_ClanStore_ApplyChannelDelta(&store, w.data, w.length, &request), "delta decodes");
    TEST_ASSERT(request == INT32_MIN, "no resync needed");
    TEST_ASSERT(channel->user_count == 2, "one added, one deleted");
    TEST_ASSERT(strcmp(channel->users[0].name, "amy") == 0, "slot 0 deleted");
    TEST_ASSERT(strcmp(channel->users[1].name, "Kim") == 0, "Kim appended");
    TEST_ASSERT(channel->update_num == 8, "the update number advances");

    /* A delta ahead of the channel asks for the full state again. */
    w.length = 0;
    p1(&w, 0);
    p8(&w, 0x1122334455667788LL);
    p8(&w, 20);
    p1(&w, 0);
    TEST_ASSERT(
        RS_ClanStore_ApplyChannelDelta(&store, w.data, w.length, &request), "ahead decodes");
    TEST_ASSERT(request == 0, "resync clan 0");

    /* The active channel is a snapshot: a new full packet does not move it. */
    TEST_ASSERT(RS_ClanStore_FindChannel(&store, 0), "find affined 0");
    TEST_ASSERT(store.active_channel == channel, "active is the current channel");
    write_channel_full(&w, 0, 9);
    TEST_ASSERT(RS_ClanStore_ApplyChannelFull(&store, w.data, w.length), "refull decodes");
    TEST_ASSERT(store.affined_channel[0] != channel, "a new object");
    TEST_ASSERT(store.active_channel == channel, "active still reads the old one");
    TEST_ASSERT(store.active_channel->user_count == 2, "and it is still readable");

    /* The clan byte alone is a leave. */
    w.length = 0;
    p1(&w, 0xFF);
    write_channel_full(&w, 0xFF, 1);
    TEST_ASSERT(RS_ClanStore_ApplyChannelFull(&store, w.data, w.length), "listened full");
    TEST_ASSERT(store.listened_channel != NULL, "negative byte is the listened clan");
    w.length = 0;
    p1(&w, 0xFF);
    TEST_ASSERT(RS_ClanStore_ApplyChannelFull(&store, w.data, w.length), "leave decodes");
    TEST_ASSERT(store.listened_channel == NULL, "left the listened clan");

    RS_ClanStore_Free(&store);
}

/* ---- clan settings --------------------------------------------------------- */

static void
test_clan_settings(void)
{
    struct RS_ClanStore store;
    struct Writer w = { .length = 0 };

    RS_ClanStore_Init(&store);
    p1(&w, 1);    /* clan 1 */
    p1(&w, 6);    /* version */
    p1(&w, 2);    /* flags: display names, no hashes */
    p4(&w, 40);   /* update number */
    p4(&w, 1234); /* creation time */
    p2(&w, 3);    /* affined */
    p1(&w, 1);    /* banned */
    pjstr(&w, "Settings");
    p4(&w, 0);
    p1(&w, 1);    /* allow unaffined */
    p1(&w, 0xFE); /* talk -2 */
    p1(&w, 10);
    p1(&w, 20);
    p1(&w, 30);
    /* members: name, rank, extra info, join runeday, muted */
    pjstr(&w, "Uma");
    p1(&w, 125);
    p4(&w, 0x00F0);
    p2(&w, 11);
    p1(&w, 0);
    pjstr(&w, "Vic");
    p1(&w, 50);
    p4(&w, 0);
    p2(&w, 12);
    p1(&w, 1);
    pjstr(&w, "Wes");
    p1(&w, 126);
    p4(&w, 0);
    p2(&w, 13);
    p1(&w, 0);
    pjstr(&w, "Banned One");
    p2(&w, 2); /* two settings */
    p4(&w, 7); /* int setting 7 */
    p4(&w, 99);
    p4(&w, (int)((2u << 30) | 8u)); /* string setting 8 */
    pjstr(&w, "motd");
    TEST_ASSERT(RS_ClanStore_ApplySettingsFull(&store, w.data, w.length), "settings decode");
    struct RS_ClanSettings* s = store.affined_settings[1];
    TEST_ASSERT(s != NULL, "affined slot 1 filled");
    if( !s )
        return;
    TEST_ASSERT(s->update_num == 40, "update number");
    TEST_ASSERT(s->creation_time == 1234, "no adjustment past version 3");
    TEST_ASSERT(s->allow_unaffined, "allow unaffined");
    TEST_ASSERT(s->rank_talk == -2, "talk is signed");
    TEST_ASSERT(s->rank_coinshare == 30, "coinshare, the last header field");
    TEST_ASSERT(s->affined_count == 3, "three members");
    TEST_ASSERT(s->affined_join_runeday[2] == 13, "runeday of the last member");
    TEST_ASSERT(s->affined_muted[1], "muted");
    TEST_ASSERT(s->banned_count == 1, "one banned");
    TEST_ASSERT(strcmp(s->banned_name[0], "Banned One") == 0, "banned name");
    TEST_ASSERT(s->owner_slot == 2, "owner is the highest rank");
    TEST_ASSERT(s->affined_rank[2] == RS_CLAN_RANK_OWNER, "owner reads 126");
    TEST_ASSERT(s->replacement_owner_slot == 0, "the 125 is the replacement");
    TEST_ASSERT(RS_ClanSettings_ExtraInfoBits(s, 0, 4, 7) == 0xF, "extra info bits 4..7");
    int value = 0;
    TEST_ASSERT(RS_ClanSettings_IntSetting(s, 7, &value) && value == 99, "int setting");
    TEST_ASSERT(!RS_ClanSettings_IntSetting(s, 8, &value), "a string setting is not an int");
    TEST_ASSERT(RS_ClanSettings_AffinedSlotByName(s, "vic") == 1, "slot by name");

    RS_ClanStore_Free(&store);
}

/* ---- varclan --------------------------------------------------------------- */

static int
type_of_test(void* user, int var_id)
{
    (void)user;
    return var_id == 5 ? RS_VARCLAN_STRING : RS_VARCLAN_INT;
}

static void
test_varclan(void)
{
    struct RS_ClanStore store;
    struct Writer w = { .length = 0 };

    RS_ClanStore_Init(&store);
    p2(&w, 4);
    p4(&w, -3);
    TEST_ASSERT(
        RS_ClanStore_ApplyVarClan(&store, w.data, w.length, type_of_test, NULL), "int varclan");
    TEST_ASSERT(store.varclan_enabled, "a VARCLAN enables the profile");
    struct RS_VarClan const* var = RS_ClanStore_VarClan(&store, 4);
    TEST_ASSERT(var && var->int_value == -3, "int value");

    w.length = 0;
    p2(&w, 5);
    p1(&w, 0);
    pjstr(&w, "hello");
    TEST_ASSERT(
        RS_ClanStore_ApplyVarClan(&store, w.data, w.length, type_of_test, NULL), "string varclan");
    var = RS_ClanStore_VarClan(&store, 5);
    TEST_ASSERT(var && strcmp(var->string_value, "hello") == 0, "gjstr2 value");

    RS_ClanStore_VarClanEnable(&store);
    TEST_ASSERT(RS_ClanStore_VarClan(&store, 4) == NULL, "enable starts empty");
    RS_ClanStore_VarClanDisable(&store);
    TEST_ASSERT(!store.varclan_enabled, "disable");

    RS_ClanStore_Free(&store);
}

int
main(void)
{
    test_friends_chat();
    test_clan_channel();
    test_clan_settings();
    test_varclan();
    if( g_failures )
    {
        fprintf(stderr, "rs_clan_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("rs_clan_test: ok\n");
    return 0;
}
