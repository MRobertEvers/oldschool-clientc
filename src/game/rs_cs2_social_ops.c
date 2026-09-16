/*
 * The friends-chat, clan settings and clan channel clientscript commands
 * (3604, 3611-3657, 3800-3890), popped by their catalogue signature.
 *
 * Behaviour is the rev-239 Java client's (Statics.method6641 for 36xx,
 * method9026 for 38xx), cross-checked against the native rev-216 client's
 * ExecuteCommand3800To3899. Where the client throws -- an active clan getter
 * with no clan found, a slot outside the member arrays, a negative member
 * index -- the script aborts here, with a line saying which command and why.
 */

#include "game/rs_cs2_host.h"

#include "cs2vm2/cs2_opcode.h"
#include "cs2vm2/cs2_opcode_meta.h"
#include "cs2vm2/cs2vm2.h"
#include "game/rs_clan.h"
#include "game/rs_friends_chat.h"
#include "game/rs_social.h"
#include "log/torirs_log.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int
script_error(int opcode, char const* why)
{
    TORIRS_LOG("cs2: %s (opcode %d): %s\n", CS2_OpCode_String(opcode), opcode, why);
    return CS2VM_EXECNO_ERROR;
}

static int
push_bool(struct CS2VM2_Thread* vm, bool value)
{
    return CS2VM2_PushInt(vm, value ? 1 : 0);
}

static int
push_text(struct CS2VM2_Thread* vm, char const* text)
{
    return CS2VM2_PushStr(vm, text[0] ? CS2VM2_StrDup(vm, text) : CS2VM2_StrEmpty(vm));
}

static void
send_named(
    struct RS_CS2Host* host,
    enum RS_CS2SocialSendKind kind,
    char const* name,
    int value0,
    int value1,
    int value2)
{
    struct RS_CS2SocialSend send;
    memset(&send, 0, sizeof(send));
    send.kind = kind;
    snprintf(send.name, sizeof(send.name), "%s", name ? name : "");
    send.values[0] = value0;
    send.values[1] = value1;
    send.values[2] = value2;
    RS_CS2Host_SendPush(host, &send);
}

/* A friends-chat member index: the client checks only `i < count`, so a
 * negative index throws. */
static int
chat_member(
    struct RS_FriendsChat const* chat,
    int opcode,
    int index,
    struct RS_FriendsChatMember const** out)
{
    *out = NULL;
    if( !chat->in_channel || index >= chat->member_count )
        return CS2VM_EXECNO_OK;
    if( index < 0 )
        return script_error(opcode, "negative member index");
    *out = &chat->members[index];
    return CS2VM_EXECNO_OK;
}

static bool
chat_sort_key(int opcode, enum RS_SocialSortKey* out)
{
    switch( opcode )
    {
    case CS2_OP_FRIENDSCHAT_SORT_LEGACY: *out = RS_SOCIAL_SORT_LEGACY; return true;
    case CS2_OP_FRIENDSCHAT_SORT_NAME: *out = RS_SOCIAL_SORT_NAME; return true;
    case CS2_OP_FRIENDSCHAT_SORT_WORLD: *out = RS_SOCIAL_SORT_WORLD; return true;
    case CS2_OP_FRIENDSCHAT_SORT_LASTWORLDCHANGE: *out = RS_SOCIAL_SORT_LAST_WORLD_CHANGE; return true;
    case CS2_OP_FRIENDSCHAT_SORT_ONLINE_STATUS: *out = RS_SOCIAL_SORT_ONLINE_STATUS; return true;
    case CS2_OP_FRIENDSCHAT_SORT_ONLINE_NAME: *out = RS_SOCIAL_SORT_ONLINE_NAME; return true;
    case CS2_OP_FRIENDSCHAT_SORT_ONLINE_LASTWORLDCHANGE: *out = RS_SOCIAL_SORT_ONLINE_LAST_WORLD_CHANGE; return true;
    case CS2_OP_FRIENDSCHAT_SORT_ONLINE_WORLD: *out = RS_SOCIAL_SORT_ONLINE_WORLD; return true;
    case CS2_OP_FRIENDSCHAT_SORT_OWNWORLD_NAME: *out = RS_SOCIAL_SORT_OWN_WORLD_NAME; return true;
    case CS2_OP_FRIENDSCHAT_SORT_OWNWORLD_WORLD: *out = RS_SOCIAL_SORT_OWN_WORLD_LAST_WORLD_CHANGE; return true;
    case CS2_OP_FRIENDSCHAT_SORT_RANK: *out = RS_SOCIAL_SORT_RANK; return true;
    default: return false;
    }
}

/* The active clan settings every 38xx getter reads. The client derefs it
 * unguarded, so none found is an abort. */
static int
active_settings(
    struct RS_CS2Host* host,
    int opcode,
    struct RS_ClanSettings** out)
{
    *out = host->clan.active_settings;
    if( !*out )
        return script_error(opcode, "no active clan settings");
    return CS2VM_EXECNO_OK;
}

static int
active_channel(
    struct RS_CS2Host* host,
    int opcode,
    struct RS_ClanChannel** out)
{
    *out = host->clan.active_channel;
    if( !*out )
        return script_error(opcode, "no active clan channel");
    return CS2VM_EXECNO_OK;
}

static int
affined_slot(
    struct RS_ClanSettings const* settings,
    int opcode,
    int slot)
{
    if( slot < 0 || slot >= settings->affined_count )
        return script_error(opcode, "affined slot out of range");
    return CS2VM_EXECNO_OK;
}

int
RS_CS2Host_ExecSocialOp(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostSignatureArgs const* args)
{
    assert(host);
    assert(vm);
    assert(args);
    int const opcode = args->opcode;
    int const* const ints = args->ints;
    char* const* const strs = args->strs;
    struct RS_FriendsChat* const chat = &host->friends_chat;
    struct RS_ClanStore* const clan = &host->clan;
    int rc;

    switch( opcode )
    {
    /* friend_setrank(name, rank): the friends-chat rank for a friend. */
    case CS2_OP_FRIEND_SETRANK:
        send_named(host, RS_CS2_SOCIAL_SEND_FRIENDCHAT_SETRANK, strs[0], 0, 0, ints[0]);
        return CS2VM_EXECNO_OK;
    /* friendlist_sort_rank: a chaining rank comparator on the friend list. */
    case CS2_OP_FRIENDLIST_SORT_RANK:
        if( host->social )
            RS_Social_SortAppend(&host->social->friend_sort, RS_SOCIAL_SORT_RANK, ints[0] == 1);
        return CS2VM_EXECNO_OK;

    /* ---- friends chat -------------------------------------------------- */
    case CS2_OP_FRIENDSCHAT_GETCHATDISPLAYNAME:
        return push_text(vm, chat->in_channel ? chat->display_name : "");
    case CS2_OP_FRIENDSCHAT_GETCHATCOUNT:
        return CS2VM2_PushInt(vm, chat->in_channel ? chat->member_count : 0);
    case CS2_OP_FRIENDSCHAT_GETCHATUSERNAME:
    case CS2_OP_FRIENDSCHAT_GETCHATUSERWORLD:
    case CS2_OP_FRIENDSCHAT_GETCHATUSERRANK:
    case CS2_OP_FRIENDSCHAT_ISSELF:
    case CS2_OP_FRIENDSCHAT_ISFRIEND:
    case CS2_OP_FRIENDSCHAT_ISIGNORE:
    {
        struct RS_FriendsChatMember const* member;
        if( (rc = chat_member(chat, opcode, ints[0], &member)) != CS2VM_EXECNO_OK )
            return rc;
        switch( opcode )
        {
        case CS2_OP_FRIENDSCHAT_GETCHATUSERNAME:
        {
            char display[RS_FRIENDS_CHAT_NAME_LEN];
            RS_Social_DisplayName(member ? member->name : "", display, sizeof(display));
            return push_text(vm, display);
        }
        case CS2_OP_FRIENDSCHAT_GETCHATUSERWORLD:
            return CS2VM2_PushInt(vm, member ? member->world : 0);
        case CS2_OP_FRIENDSCHAT_GETCHATUSERRANK:
            return CS2VM2_PushInt(vm, member ? member->rank : 0);
        case CS2_OP_FRIENDSCHAT_ISSELF:
            return push_bool(vm, member && host->local_player_name[0] &&
                                     RS_FriendsChat_NamesEqual(member->name, host->local_player_name));
        case CS2_OP_FRIENDSCHAT_ISFRIEND:
            return push_bool(vm, member && host->social && RS_Social_IsFriend(host->social, member->name));
        default:
            return push_bool(vm, member && host->social && RS_Social_IsIgnored(host->social, member->name));
        }
    }
    case CS2_OP_FRIENDSCHAT_GETCHATMINKICK:
        return CS2VM2_PushInt(vm, chat->in_channel ? chat->kick_rank : 0);
    case CS2_OP_FRIENDSCHAT_GETCHATRANK:
        return CS2VM2_PushInt(vm, chat->in_channel ? chat->local_rank : 0);
    case CS2_OP_FRIENDSCHAT_GETCHATOWNERNAME:
        return push_text(vm, chat->in_channel && chat->has_owner ? chat->owner : "");
    /* Kick only goes out from inside a channel; join ignores an empty name;
     * leave is always sent, in or out. */
    case CS2_OP_FRIENDSCHAT_KICKUSER:
        if( chat->in_channel )
            send_named(host, RS_CS2_SOCIAL_SEND_FRIENDCHAT_KICK, strs[0], 0, 0, 0);
        return CS2VM_EXECNO_OK;
    case CS2_OP_FRIENDSCHAT_JOINCHAT:
        if( strs[0] && strs[0][0] )
            send_named(host, RS_CS2_SOCIAL_SEND_FRIENDCHAT_JOIN, strs[0], 0, 0, 0);
        return CS2VM_EXECNO_OK;
    case CS2_OP_FRIENDSCHAT_LEAVECHAT:
        send_named(host, RS_CS2_SOCIAL_SEND_FRIENDCHAT_LEAVE, NULL, 0, 0, 0);
        return CS2VM_EXECNO_OK;

    case CS2_OP_FRIENDSCHAT_SORT_RESET:
        if( chat->in_channel )
            RS_Social_SortReset(&chat->sort);
        return CS2VM_EXECNO_OK;
    case CS2_OP_FRIENDSCHAT_SORT_APPLY:
        if( chat->in_channel )
            RS_FriendsChat_Sort(chat, host->map_world);
        return CS2VM_EXECNO_OK;
    case CS2_OP_FRIENDSCHAT_SORT_LEGACY:
    case CS2_OP_FRIENDSCHAT_SORT_NAME:
    case CS2_OP_FRIENDSCHAT_SORT_WORLD:
    case CS2_OP_FRIENDSCHAT_SORT_LASTWORLDCHANGE:
    case CS2_OP_FRIENDSCHAT_SORT_ONLINE_STATUS:
    case CS2_OP_FRIENDSCHAT_SORT_ONLINE_NAME:
    case CS2_OP_FRIENDSCHAT_SORT_ONLINE_LASTWORLDCHANGE:
    case CS2_OP_FRIENDSCHAT_SORT_ONLINE_WORLD:
    case CS2_OP_FRIENDSCHAT_SORT_OWNWORLD_NAME:
    case CS2_OP_FRIENDSCHAT_SORT_OWNWORLD_WORLD:
    case CS2_OP_FRIENDSCHAT_SORT_RANK:
    {
        enum RS_SocialSortKey key;
        bool const known = chat_sort_key(opcode, &key);
        assert(known);
        (void)known;
        if( chat->in_channel )
            RS_Social_SortAppend(&chat->sort, key, ints[0] == 1);
        return CS2VM_EXECNO_OK;
    }

    /* ---- clan settings ------------------------------------------------- */
    case CS2_OP_ACTIVECLANSETTINGS_FIND_LISTENED:
        return push_bool(vm, RS_ClanStore_FindSettings(clan, -1));
    case CS2_OP_ACTIVECLANSETTINGS_FIND_AFFINED:
        if( ints[0] < 0 || ints[0] >= RS_CLAN_AFFINED_SLOTS )
            return script_error(opcode, "clan index out of range");
        return push_bool(vm, RS_ClanStore_FindSettings(clan, ints[0]));

    case CS2_OP_ACTIVECLANSETTINGS_GETCLANNAME:
    case CS2_OP_ACTIVECLANSETTINGS_GETALLOWUNAFFINED:
    case CS2_OP_ACTIVECLANSETTINGS_GETRANKTALK:
    case CS2_OP_ACTIVECLANSETTINGS_GETRANKKICK:
    case CS2_OP_ACTIVECLANSETTINGS_GETRANKLOOTSHARE:
    case CS2_OP_ACTIVECLANSETTINGS_GETCOINSHARE:
    case CS2_OP_ACTIVECLANSETTINGS_GETAFFINEDCOUNT:
    case CS2_OP_ACTIVECLANSETTINGS_GETAFFINEDDISPLAYNAME:
    case CS2_OP_ACTIVECLANSETTINGS_GETAFFINEDRANK:
    case CS2_OP_ACTIVECLANSETTINGS_GETBANNEDCOUNT:
    case CS2_OP_ACTIVECLANSETTINGS_GETBANNEDDISPLAYNAME:
    case CS2_OP_ACTIVECLANSETTINGS_GETAFFINEDEXTRAINFO:
    case CS2_OP_ACTIVECLANSETTINGS_GETCURRENTOWNER_SLOT:
    case CS2_OP_ACTIVECLANSETTINGS_GETREPLACEMENTOWNER_SLOT:
    case CS2_OP_ACTIVECLANSETTINGS_GETAFFINEDSLOT:
    case CS2_OP_ACTIVECLANSETTINGS_GETSORTEDAFFINEDSLOT:
    case CS2_OP_ACTIVECLANSETTINGS_GETAFFINEDJOINRUNEDAY:
    case CS2_OP_ACTIVECLANSETTINGS_GETAFFINEDMUTED:
    {
        struct RS_ClanSettings* s;
        if( (rc = active_settings(host, opcode, &s)) != CS2VM_EXECNO_OK )
            return rc;
        switch( opcode )
        {
        case CS2_OP_ACTIVECLANSETTINGS_GETCLANNAME:
            return push_text(vm, s->name);
        case CS2_OP_ACTIVECLANSETTINGS_GETALLOWUNAFFINED:
            return push_bool(vm, s->allow_unaffined);
        case CS2_OP_ACTIVECLANSETTINGS_GETRANKTALK:
            return CS2VM2_PushInt(vm, s->rank_talk);
        case CS2_OP_ACTIVECLANSETTINGS_GETRANKKICK:
            return CS2VM2_PushInt(vm, s->rank_kick);
        case CS2_OP_ACTIVECLANSETTINGS_GETRANKLOOTSHARE:
            return CS2VM2_PushInt(vm, s->rank_lootshare);
        case CS2_OP_ACTIVECLANSETTINGS_GETCOINSHARE:
            return CS2VM2_PushInt(vm, s->rank_coinshare);
        case CS2_OP_ACTIVECLANSETTINGS_GETAFFINEDCOUNT:
            return CS2VM2_PushInt(vm, s->affined_count);
        case CS2_OP_ACTIVECLANSETTINGS_GETBANNEDCOUNT:
            return CS2VM2_PushInt(vm, s->banned_count);
        case CS2_OP_ACTIVECLANSETTINGS_GETCURRENTOWNER_SLOT:
            return CS2VM2_PushInt(vm, s->owner_slot);
        case CS2_OP_ACTIVECLANSETTINGS_GETREPLACEMENTOWNER_SLOT:
            return CS2VM2_PushInt(vm, s->replacement_owner_slot);
        /* A name the server did not send is Java's null. */
        case CS2_OP_ACTIVECLANSETTINGS_GETAFFINEDDISPLAYNAME:
            if( (rc = affined_slot(s, opcode, ints[0])) != CS2VM_EXECNO_OK )
                return rc;
            if( !s->has_display_names )
                return script_error(opcode, "clan settings carry no display names");
            return s->affined_has_name[ints[0]] ? CS2VM2_PushStr(vm, CS2VM2_StrDup(vm, s->affined_name[ints[0]]))
                                                : CS2VM2_PushStr(vm, NULL);
        case CS2_OP_ACTIVECLANSETTINGS_GETAFFINEDRANK:
            if( (rc = affined_slot(s, opcode, ints[0])) != CS2VM_EXECNO_OK )
                return rc;
            return CS2VM2_PushInt(vm, s->affined_rank[ints[0]]);
        case CS2_OP_ACTIVECLANSETTINGS_GETBANNEDDISPLAYNAME:
            if( ints[0] < 0 || ints[0] >= s->banned_count )
                return script_error(opcode, "banned slot out of range");
            if( !s->has_display_names )
                return script_error(opcode, "clan settings carry no display names");
            return s->banned_has_name[ints[0]] ? CS2VM2_PushStr(vm, CS2VM2_StrDup(vm, s->banned_name[ints[0]]))
                                               : CS2VM2_PushStr(vm, NULL);
        case CS2_OP_ACTIVECLANSETTINGS_GETAFFINEDEXTRAINFO:
            if( (rc = affined_slot(s, opcode, ints[0])) != CS2VM_EXECNO_OK )
                return rc;
            return CS2VM2_PushInt(vm, RS_ClanSettings_ExtraInfoBits(s, ints[0], ints[1], ints[2]));
        case CS2_OP_ACTIVECLANSETTINGS_GETAFFINEDSLOT:
            return CS2VM2_PushInt(vm, strs[0] ? RS_ClanSettings_AffinedSlotByName(s, strs[0]) : -1);
        case CS2_OP_ACTIVECLANSETTINGS_GETSORTEDAFFINEDSLOT:
        {
            int const slot = RS_ClanSettings_SortedAffinedSlot(s, ints[0]);
            if( slot < 0 )
                return script_error(opcode, "sorted position out of range");
            return CS2VM2_PushInt(vm, slot);
        }
        case CS2_OP_ACTIVECLANSETTINGS_GETAFFINEDJOINRUNEDAY:
            if( (rc = affined_slot(s, opcode, ints[0])) != CS2VM_EXECNO_OK )
                return rc;
            return CS2VM2_PushInt(vm, s->affined_join_runeday[ints[0]]);
        default:
            if( (rc = affined_slot(s, opcode, ints[0])) != CS2VM_EXECNO_OK )
                return rc;
            return push_bool(vm, s->affined_muted[ints[0]]);
        }
    }

    /* affinedclansettings_addbanned_fromchannel(slot, clan) and
     * _setmuted_fromchannel(slot, muted, clan): act on a member of affined
     * clan CHANNEL `clan`. Banning only reaches a guest (rank -1). */
    case CS2_OP_AFFINEDCLANSETTINGS_ADDBANNED_FROMCHANNEL:
    case CS2_OP_AFFINEDCLANSETTINGS_SETMUTED_FROMCHANNEL:
    {
        int const slot = ints[0];
        int const clan_index = opcode == CS2_OP_AFFINEDCLANSETTINGS_ADDBANNED_FROMCHANNEL ? ints[1] : ints[2];
        if( clan_index < 0 || clan_index >= RS_CLAN_AFFINED_SLOTS )
            return script_error(opcode, "clan index out of range");
        struct RS_ClanChannel const* const channel = clan->affined_channel[clan_index];
        if( !channel || slot < 0 || slot >= channel->user_count )
            return CS2VM_EXECNO_OK;
        struct RS_ClanChannelUser const* const user = &channel->users[slot];
        if( opcode == CS2_OP_AFFINEDCLANSETTINGS_ADDBANNED_FROMCHANNEL )
        {
            if( user->rank != RS_CLAN_RANK_GUEST )
                return CS2VM_EXECNO_OK;
            send_named(host, RS_CS2_SOCIAL_SEND_CLAN_ADDBANNED, user->name, clan_index, slot, 0);
            return CS2VM_EXECNO_OK;
        }
        send_named(host, RS_CS2_SOCIAL_SEND_CLAN_SETMUTED, user->name, clan_index, slot, ints[1] == 1);
        return CS2VM_EXECNO_OK;
    }

    /* ---- clan channel -------------------------------------------------- */
    case CS2_OP_ACTIVECLANCHANNEL_FIND_LISTENED:
        return push_bool(vm, RS_ClanStore_FindChannel(clan, -1));
    case CS2_OP_ACTIVECLANCHANNEL_FIND_AFFINED:
        if( ints[0] < 0 || ints[0] >= RS_CLAN_AFFINED_SLOTS )
            return script_error(opcode, "clan index out of range");
        return push_bool(vm, RS_ClanStore_FindChannel(clan, ints[0]));

    case CS2_OP_ACTIVECLANCHANNEL_GETCLANNAME:
    case CS2_OP_ACTIVECLANCHANNEL_GETRANKKICK:
    case CS2_OP_ACTIVECLANCHANNEL_GETRANKTALK:
    case CS2_OP_ACTIVECLANCHANNEL_GETUSERCOUNT:
    case CS2_OP_ACTIVECLANCHANNEL_GETUSERDISPLAYNAME:
    case CS2_OP_ACTIVECLANCHANNEL_GETUSERRANK:
    case CS2_OP_ACTIVECLANCHANNEL_GETUSERWORLD:
    case CS2_OP_ACTIVECLANCHANNEL_GETUSERSLOT:
    case CS2_OP_ACTIVECLANCHANNEL_GETSORTEDUSERSLOT:
    {
        struct RS_ClanChannel* ch;
        if( (rc = active_channel(host, opcode, &ch)) != CS2VM_EXECNO_OK )
            return rc;
        switch( opcode )
        {
        case CS2_OP_ACTIVECLANCHANNEL_GETCLANNAME:
            return push_text(vm, ch->name);
        case CS2_OP_ACTIVECLANCHANNEL_GETRANKKICK:
            return CS2VM2_PushInt(vm, ch->rank_kick);
        case CS2_OP_ACTIVECLANCHANNEL_GETRANKTALK:
            return CS2VM2_PushInt(vm, ch->rank_talk);
        case CS2_OP_ACTIVECLANCHANNEL_GETUSERCOUNT:
            return CS2VM2_PushInt(vm, ch->user_count);
        case CS2_OP_ACTIVECLANCHANNEL_GETUSERSLOT:
            if( !ch->has_display_names )
                return script_error(opcode, "Displaynames not available");
            return CS2VM2_PushInt(vm, strs[0] ? RS_ClanChannel_UserSlotByName(ch, strs[0]) : -1);
        case CS2_OP_ACTIVECLANCHANNEL_GETSORTEDUSERSLOT:
        {
            int const slot = RS_ClanChannel_SortedUserSlot(ch, ints[0]);
            if( slot < 0 )
                return script_error(opcode, "sorted position out of range");
            return CS2VM2_PushInt(vm, slot);
        }
        default:
            break;
        }
        if( ints[0] < 0 || ints[0] >= ch->user_count )
            return script_error(opcode, "user index out of range");
        struct RS_ClanChannelUser const* const user = &ch->users[ints[0]];
        if( opcode == CS2_OP_ACTIVECLANCHANNEL_GETUSERDISPLAYNAME )
            return push_text(vm, user->name);
        if( opcode == CS2_OP_ACTIVECLANCHANNEL_GETUSERRANK )
            return CS2VM2_PushInt(vm, user->rank);
        return CS2VM2_PushInt(vm, user->world);
    }
    /* Kicks from the affined channel the last find_affined chose, and only a
     * guest. */
    case CS2_OP_ACTIVECLANCHANNEL_KICKUSER:
    {
        int const index = clan->active_channel_affined_index;
        struct RS_ClanChannel const* const channel = clan->affined_channel[index];
        int const slot = ints[0];
        if( !channel || slot < 0 || slot >= channel->user_count )
            return CS2VM_EXECNO_OK;
        if( channel->users[slot].rank != RS_CLAN_RANK_GUEST )
            return CS2VM_EXECNO_OK;
        send_named(host, RS_CS2_SOCIAL_SEND_CLAN_KICKUSER, channel->users[slot].name, index, slot, 0);
        return CS2VM_EXECNO_OK;
    }

    case CS2_OP_CLANPROFILE_FIND:
        return push_bool(vm, clan->varclan_enabled);

    default:
        TORIRS_LOG("RS_CS2Host_ExecSocialOp: unhandled opcode %d\n", opcode);
        assert(0 && "RS_CS2Host_ExecSocialOp: unexpected opcode");
        return CS2VM_EXECNO_ERROR;
    }
}

/* PUSH_VARCLANSETTING (74): an int setting of the active clan settings, -1 when
 * absent or not an int. The client derefs the active settings unguarded. */
int
RS_CS2Host_PushVarClanSetting(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    int setting_id)
{
    assert(host);
    assert(vm);
    struct RS_ClanSettings const* const s = host->clan.active_settings;
    if( !s )
        return script_error(CS2_OP_PUSH_VARCLANSETTING, "no active clan settings");
    int value;
    if( !RS_ClanSettings_IntSetting(s, setting_id, &value) )
        value = -1;
    return CS2VM2_PushInt(vm, value);
}

/* PUSH_VARCLAN (76): an int varclan, its type's default 0 when unset. A long or
 * string varclan, or no profile at all, throws in the client. */
int
RS_CS2Host_PushVarClan(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    int var_id)
{
    assert(host);
    assert(vm);
    if( !host->clan.varclan_enabled )
        return script_error(CS2_OP_PUSH_VARCLAN, "no clan profile");
    int const type = RS_CS2Host_VarClanType(host, var_id);
    if( type != RS_VARCLAN_INT )
        return script_error(CS2_OP_PUSH_VARCLAN, "varclan is not an int");
    struct RS_VarClan const* const value = RS_ClanStore_VarClan(&host->clan, var_id);
    return CS2VM2_PushInt(vm, value ? value->int_value : 0);
}
