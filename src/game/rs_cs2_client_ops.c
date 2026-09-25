/*
 * The client-state clientscript commands: 31xx client settings, 32xx options,
 * 33xx client facts, the platform families (Steam 37xx, federated login 563x),
 * the sidebar reservation (6231/6232) and the three client identity getters
 * in 65xx.
 *
 * Every opcode here is a signature-popped command (struct
 * CS2VM_HostSignatureArgs): the VM has already taken exactly the catalogue's
 * arguments, in push order, and this pushes the results.
 *
 * Behaviour is the rev-239 Java client's (Statics.java, method7113 for 31xx,
 * method4261 for 32xx, method13940 for 33xx). Where that client has no handler
 * the native rev-216 client is the reference, and the comment says so. The
 * `later client` notes are opcodes present in the RuneLite 1.12.34 client but
 * not 1.12.33: this cache's scripts call them, so they are implemented as the
 * later client does.
 */

#include "game/rs_cs2_host.h"

#include "cs2vm2/cs2_opcode.h"
#include "cs2vm2/cs2vm2.h"
#include "engine/cache_provider.h"
#include "engine/torirs_types.h"
#include "game/rs_chat.h"
#include "game/rs_player_stats.h"
#include "log/torirs_log.h"

#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
RS_CS2ClientState_Init(struct RS_CS2ClientState* state)
{
    assert(state);
    memset(state, 0, sizeof(*state));
    /* Statics / client.java defaults. */
    state->show_mouseover_text = true;
    state->render_self = true;
    state->show_mouse_cross = true;
    state->show_loading_messages = true;
    state->remember_username = true;
    state->freecam_speed = 12;
    state->freecam_speed_shift = 6;
    state->key_input_mode = RS_CS2_KEY_INPUT_ALL;
    state->loading_percent = 10;
    /* Bit 0 is the members flag of the world-list entry. The worlds this
     * client connects to are members worlds, and nothing on the wire says
     * otherwise; the reference reads it from the world it logged in to. */
    state->world_flags = 1;
}

void
RS_CS2ClientState_Free(struct RS_CS2ClientState* state)
{
    if( !state )
        return;
    for( int i = 0; i < state->translation_count; i++ )
    {
        free(state->translations[i].key);
        free(state->translations[i].value);
    }
    free(state->translations);
    state->translations = NULL;
    state->translation_count = 0;
    state->translation_cap = 0;
}

char const*
RS_CS2ClientState_Translation(
    struct RS_CS2ClientState const* state,
    char const* key)
{
    assert(state);
    assert(key);
    for( int i = 0; i < state->translation_count; i++ )
        if( strcasecmp(state->translations[i].key, key) == 0 )
            return state->translations[i].value;
    return NULL;
}

/* translations_set: `map.put(key.toLowerCase(), value)`. */
static void
translation_set(
    struct RS_CS2ClientState* state,
    char const* key,
    char const* value)
{
    for( int i = 0; i < state->translation_count; i++ )
    {
        if( strcasecmp(state->translations[i].key, key) != 0 )
            continue;
        char* const copy = strdup(value);
        assert(copy);
        free(state->translations[i].value);
        state->translations[i].value = copy;
        return;
    }
    if( state->translation_count == state->translation_cap )
    {
        int const cap = state->translation_cap ? state->translation_cap * 2 : 16;
        void* const grown = realloc(state->translations, (size_t)cap * sizeof(state->translations[0]));
        assert(grown);
        state->translations = grown;
        state->translation_cap = cap;
    }
    char* const key_copy = strdup(key);
    char* const value_copy = strdup(value);
    assert(key_copy);
    assert(value_copy);
    for( char* c = key_copy; *c; c++ )
        *c = (char)tolower((unsigned char)*c);
    state->translations[state->translation_count].key = key_copy;
    state->translations[state->translation_count].value = value_copy;
    state->translation_count++;
}

/* Java's isDecimalLong + parseLong (Statics method10036/method3796): an
 * optional sign then at least one digit, else 0. */
static int64_t
parse_decimal_long(char const* text)
{
    if( !text )
        return 0;
    char const* cursor = text;
    bool const negative = *cursor == '-';
    if( *cursor == '-' || *cursor == '+' )
        cursor++;
    if( !*cursor )
        return 0;
    uint64_t value = 0;
    for( ; *cursor; cursor++ )
    {
        if( *cursor < '0' || *cursor > '9' )
            return 0;
        uint64_t const next = value * 10 + (uint64_t)(*cursor - '0');
        if( next / 10 != value || next > (uint64_t)INT64_MAX + (negative ? 1u : 0u) )
            return 0;
        value = next;
    }
    return negative ? (int64_t)(0 - value) : (int64_t)value;
}

static int
push_bool(struct CS2VM2_Thread* vm, bool value)
{
    return CS2VM2_PushInt(vm, value ? 1 : 0);
}

static void
queue_text(
    struct RS_CS2Host* host,
    enum RS_CS2SocialSendKind kind,
    char const* text)
{
    struct RS_CS2SocialSend send;
    memset(&send, 0, sizeof(send));
    send.kind = kind;
    snprintf(send.text, sizeof(send.text), "%s", text ? text : "");
    RS_CS2Host_SendPush(host, &send);
}

int
RS_CS2Host_ExecClientOp(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostSignatureArgs const* args)
{
    assert(host);
    assert(vm);
    assert(args);
    struct RS_CS2ClientState* const state = &host->client;
    int const* const ints = args->ints;
    char* const* const strs = args->strs;

    switch( args->opcode )
    {
    /* mes_typed(type, text): native 216 0xc1e AddChat(type, "", text). */
    case CS2_OP_MES_TYPED:
        RS_CS2Host_ChatAdd(host, ints[0], NULL, NULL, strs[0] ? strs[0] : "");
        return CS2VM_EXECNO_OK;

    case CS2_OP_RESUME_NAMEDIALOG:
        queue_text(host, RS_CS2_SOCIAL_SEND_RESUME_NAMEDIALOG, strs[0]);
        return CS2VM_EXECNO_OK;
    case CS2_OP_RESUME_STRINGDIALOG:
        queue_text(host, RS_CS2_SOCIAL_SEND_RESUME_STRINGDIALOG, strs[0]);
        return CS2VM_EXECNO_OK;
    case CS2_OP_RESUME_COUNTDIALOG_LONG:
    {
        struct RS_CS2SocialSend send;
        memset(&send, 0, sizeof(send));
        send.kind = RS_CS2_SOCIAL_SEND_RESUME_COUNTDIALOG_LONG;
        send.long_value = parse_decimal_long(strs[0]);
        RS_CS2Host_SendPush(host, &send);
        return CS2VM_EXECNO_OK;
    }
    case CS2_OP_RESUME_OBJDIALOG:
    {
        struct RS_CS2SocialSend send;
        memset(&send, 0, sizeof(send));
        send.kind = RS_CS2_SOCIAL_SEND_RESUME_OBJDIALOG;
        send.values[0] = ints[0];
        RS_CS2Host_SendPush(host, &send);
        return CS2VM_EXECNO_OK;
    }

    /*
     * opplayer(op, name): Statics method7673. The first player in view whose
     * name matches -- never the local player -- is the target, and only ops
     * 1, 4, 6 and 7 send anything; another op still counts as found. No match
     * is a game-channel "Unable to find <name>".
     */
    case CS2_OP_OPPLAYER:
    {
        char const* const name = strs[0] ? strs[0] : "";
        int const slot = host->player_slot_by_name ? host->player_slot_by_name(host->world_user, name) : -1;
        if( slot < 0 )
        {
            char line[128];
            snprintf(line, sizeof(line), "Unable to find %s", name);
            RS_CS2Host_ChatAdd(host, 4, NULL, NULL, line);
            return CS2VM_EXECNO_OK;
        }
        int const op = ints[0];
        if( op != 1 && op != 4 && op != 6 && op != 7 )
            return CS2VM_EXECNO_OK;
        struct RS_CS2SocialSend send;
        memset(&send, 0, sizeof(send));
        send.kind = RS_CS2_SOCIAL_SEND_OPPLAYER;
        send.values[0] = op;
        send.values[1] = slot;
        RS_CS2Host_SendPush(host, &send);
        return CS2VM_EXECNO_OK;
    }

    /* openurl(url, boolean): the client hands the url to its launcher and drops
     * the boolean. */
    case CS2_OP_OPENURL:
        snprintf(state->pending_open_url, sizeof(state->pending_open_url), "%s", strs[0] ? strs[0] : "");
        return CS2VM_EXECNO_OK;

    /* bug_report(template, description, instructions): nothing is sent when
     * either text is over 500 characters. */
    case CS2_OP_BUG_REPORT:
    {
        char const* const description = strs[0] ? strs[0] : "";
        char const* const instructions = strs[1] ? strs[1] : "";
        if( strlen(description) > 500 || strlen(instructions) > 500 )
            return CS2VM_EXECNO_OK;
        struct RS_CS2SocialSend send;
        memset(&send, 0, sizeof(send));
        send.kind = RS_CS2_SOCIAL_SEND_BUG_REPORT;
        send.values[0] = ints[0];
        snprintf(send.text, sizeof(send.text), "%s", description);
        snprintf(send.text2, sizeof(send.text2), "%s", instructions);
        RS_CS2Host_SendPush(host, &send);
        return CS2VM_EXECNO_OK;
    }

    /* chat_sendabusereport(name, rule, mute): the wire carries rule - 1. */
    case CS2_OP_CHAT_SENDABUSEREPORT:
    {
        struct RS_CS2SocialSend send;
        memset(&send, 0, sizeof(send));
        send.kind = RS_CS2_SOCIAL_SEND_ABUSE_REPORT;
        snprintf(send.name, sizeof(send.name), "%s", strs[0] ? strs[0] : "");
        send.values[0] = ints[0] - 1;
        send.values[1] = ints[1];
        RS_CS2Host_SendPush(host, &send);
        return CS2VM_EXECNO_OK;
    }

    case CS2_OP_SETMOUSECAM:
        state->middle_mouse_camera = ints[0] == 1;
        return CS2VM_EXECNO_OK;
    case CS2_OP_SETSHOWMOUSEOVERTEXT:
        state->show_mouseover_text = ints[0] == 1;
        return CS2VM_EXECNO_OK;
    case CS2_OP_RENDERSELF:
        state->render_self = ints[0] == 1;
        return CS2VM_EXECNO_OK;

    /* The draw-player-names mask: 1 friends, 2 clan members, 4 everyone else,
     * 8 the local player. */
    case CS2_OP_SETDRAWPLAYERNAMES_FRIENDS:
    case CS2_OP_SETDRAWPLAYERNAMES_CLANMATES:
    case CS2_OP_SETDRAWPLAYERNAMES_OTHERS:
    case CS2_OP_SETDRAWPLAYERNAMES_SELF:
    {
        int const bit = args->opcode == CS2_OP_SETDRAWPLAYERNAMES_FRIENDS     ? RS_CS2_DRAW_NAMES_FRIENDS
                        : args->opcode == CS2_OP_SETDRAWPLAYERNAMES_CLANMATES ? RS_CS2_DRAW_NAMES_CLAN
                        : args->opcode == CS2_OP_SETDRAWPLAYERNAMES_OTHERS    ? RS_CS2_DRAW_NAMES_OTHERS
                                                                              : RS_CS2_DRAW_NAMES_SELF;
        if( ints[0] == 1 )
            state->draw_player_names |= bit;
        else
            state->draw_player_names &= ~bit;
        return CS2VM_EXECNO_OK;
    }
    case CS2_OP_RESETDRAWPLAYERNAMES:
        state->draw_player_names = 0;
        return CS2VM_EXECNO_OK;

    case CS2_OP_SETSHOWMOUSECROSS:
        state->show_mouse_cross = ints[0] == 1;
        return CS2VM_EXECNO_OK;
    case CS2_OP_SETSHOWLOADINGMESSAGES:
        state->show_loading_messages = ints[0] == 1;
        return CS2VM_EXECNO_OK;
    case CS2_OP_SETSIMULATEDSHIFTACTIVE:
        state->simulated_shift = ints[0] == 1;
        return CS2VM_EXECNO_OK;
    case CS2_OP_GETSIMULATEDSHIFTACTIVE:
        return push_bool(vm, state->simulated_shift);
    /* setfreecamspeed(normal, shift): no clamping. */
    case CS2_OP_SETFREECAMSPEED:
        state->freecam_speed = ints[0];
        state->freecam_speed_shift = ints[1];
        return CS2VM_EXECNO_OK;

    /* Which components get key events: 0 every listener, 1 none, 2 one
     * interface (target = interface id), 3 one component (target = its id). */
    case CS2_OP_SETKEYINPUTMODE_COMPONENT:
        state->key_input_mode = RS_CS2_KEY_INPUT_COMPONENT;
        state->key_input_target = ints[0];
        return CS2VM_EXECNO_OK;
    case CS2_OP_SETKEYINPUTMODE_INTERFACE:
        state->key_input_mode = RS_CS2_KEY_INPUT_INTERFACE;
        state->key_input_target = ints[0];
        return CS2VM_EXECNO_OK;
    case CS2_OP_SETKEYINPUTMODE_ALL:
        state->key_input_mode = RS_CS2_KEY_INPUT_ALL;
        return CS2VM_EXECNO_OK;
    case CS2_OP_SETKEYINPUTMODE_NONE:
        state->key_input_mode = RS_CS2_KEY_INPUT_NONE;
        return CS2VM_EXECNO_OK;

    /* Device option 2 is the hide-username preference. */
    case CS2_OP_SETHIDEUSERNAME:
        RS_CS2Host_SetOption(host, RS_CS2_OPTION_DEVICE, 2, ints[0] == 1 ? 1 : 0);
        return CS2VM_EXECNO_OK;
    case CS2_OP_GETHIDEUSERNAME:
        return push_bool(vm, RS_CS2Host_GetOption(host, RS_CS2_OPTION_DEVICE, 2) != 0);
    /* Turning remember-username off also forgets the saved name. */
    case CS2_OP_SETREMEMBERUSERNAME:
        state->remember_username = ints[0] == 1;
        if( !state->remember_username )
            state->forget_saved_username = true;
        return CS2VM_EXECNO_OK;
    case CS2_OP_GETREMEMBERUSERNAME:
        return push_bool(vm, state->remember_username);
    /* Device option 4 is "title music disabled", the inverse of the argument. */
    case CS2_OP_SETTITLESCREENSOUND:
        RS_CS2Host_SetOption(host, RS_CS2_OPTION_DEVICE, 4, ints[0] == 1 ? 0 : 1);
        return CS2VM_EXECNO_OK;
    case CS2_OP_GETTITLESCREENSOUND:
        return push_bool(vm, RS_CS2Host_GetOption(host, RS_CS2_OPTION_DEVICE, 4) == 0);

    /* Store / purchase / notification queries the desktop client answers with
     * constants: nothing is available and nothing is enabled. */
    case CS2_OP_GETTERMSANDPRIVACY:
    case CS2_OP_ELIGIBLEFORFREETRIAL:
    case CS2_OP_ELIGIBLEFORINTRODUCTORYPRICE:
    case CS2_OP_GETPUCHASEHISTORYSTATUS:
    case CS2_OP_SHOP_PURCHASEITEMSTATUS:
    case CS2_OP_SHOP_REQUESTDATASTATUS:
    case CS2_OP_SHOP_GETCATEGORYCOUNT:
    case CS2_OP_SHOP_GETCATEGORYID:
    case CS2_OP_SHOP_GETINDEXFORCATEGORYID:
    case CS2_OP_SHOP_GETINDEXFORCATEGORYNAME:
    case CS2_OP_SHOP_GETPRODUCTCOUNT:
    case CS2_OP_SHOP_ISPRODUCTAVAILABLE:
    case CS2_OP_SHOP_ISPRODUCTRECOMMENDED:
    case CS2_OP_NOTIFICATIONS_GETENABLED:
        return CS2VM2_PushInt(vm, 0);
    case CS2_OP_SHOP_GETCATEGORYDESCRIPTION:
        return CS2VM2_PushStr(vm, CS2VM2_StrEmpty(vm));
    case CS2_OP_SHOP_GETPRODUCTDETAILS:
        for( int i = 0; i < 9; i++ )
            if( CS2VM2_PushStr(vm, CS2VM2_StrEmpty(vm)) != CS2VM_EXECNO_OK )
                return CS2VM_EXECNO_ERROR;
        return CS2VM_EXECNO_OK;

    /* Login-loading percent (starts at 10) and the preload fraction out of
     * 10000, 10000 once preloading is done. The App mirrors both. */
    case CS2_OP_GETLOADINGPROGRESS:
        return CS2VM2_PushInt(vm, state->loading_percent);
    case CS2_OP_GETPRELOADPROGRESS:
        return CS2VM2_PushInt(vm, state->preload_progress_done ? 10000 : state->preload_progress);

    /* Brightness is device option 6, 0..100. The client stores a gamma and
     * converts both ways; the percentage round-trips. */
    case CS2_OP_SETBRIGHTNESS:
    {
        int const clamped = ints[0] < 0 ? 0 : ints[0] > 100 ? 100 : ints[0];
        RS_CS2Host_SetOption(host, RS_CS2_OPTION_DEVICE, 6, clamped);
        return CS2VM_EXECNO_OK;
    }
    case CS2_OP_GETBRIGHTNESS:
        return CS2VM2_PushInt(vm, RS_CS2Host_GetOption(host, RS_CS2_OPTION_DEVICE, 6));
    /* getantidrag: native 216 0xc70, the flag SETANTIDRAG (3183) writes. */
    case CS2_OP_GETANTIDRAG:
        return push_bool(vm, host->tree && host->tree->anti_drag);
    /* Draw distance is device option 14; no clamp. */
    case CS2_OP_SETDRAWDISTANCE:
        RS_CS2Host_SetOption(host, RS_CS2_OPTION_DEVICE, 14, ints[0]);
        return CS2VM_EXECNO_OK;
    case CS2_OP_GETDRAWDISTANCE:
        return CS2VM2_PushInt(vm, RS_CS2Host_GetOption(host, RS_CS2_OPTION_DEVICE, 14));

    /* Discord rich presence (native 216 0xc73 / 0xc74): this client has no
     * Discord integration, which the native client reports the same way. */
    case CS2_OP_UNKNOWN_COMMAND_3187:
    case CS2_OP_UNKNOWN_COMMAND_3188:
        TORIRS_LOG("cs2: Discord is not initialized (opcode %d)\n", args->opcode);
        return CS2VM_EXECNO_OK;

    /* gameoption_exists / deviceoption_exists: the ids each table has. */
    case CS2_OP_GAMEOPTION_EXISTS:
        return push_bool(vm, ints[0] == 1 || ints[0] == 7 || ints[0] == 8 || ints[0] == 9);
    case CS2_OP_DEVICEOPTION_EXISTS:
        return push_bool(vm, ints[0] == 2 || ints[0] == 3 || ints[0] == 4 || ints[0] == 5 ||
                                 ints[0] == 6 || ints[0] == 14 || ints[0] == 19 || ints[0] == 22);
    /* gameoption_getrange: min then max. Any other id throws in the client. */
    case CS2_OP_GAMEOPTION_GETRANGE:
    {
        int min;
        int max;
        if( ints[0] == 1 )
        {
            min = 0;
            max = 1;
        }
        else if( ints[0] == 7 || ints[0] == 8 || ints[0] == 9 )
        {
            min = 0;
            max = 100;
        }
        else
        {
            TORIRS_LOG("cs2: Unrecognized game option %d\n", ints[0]);
            return CS2VM_EXECNO_ERROR;
        }
        if( CS2VM2_PushInt(vm, min) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        return CS2VM2_PushInt(vm, max);
    }

    /* The RT7 renderer (a later client's GPU renderer): enabled, and its SD or
     * HD quality. Neither reference client has these; the state is what the
     * commands name, for the renderer to read. */
    case CS2_OP_RT7_SETENABLED:
        state->rt7_enabled = ints[0] == 1;
        return CS2VM_EXECNO_OK;
    case CS2_OP_RT7_SD:
        state->rt7_hd = false;
        return CS2VM_EXECNO_OK;
    case CS2_OP_RT7_HD:
        state->rt7_hd = true;
        return CS2VM_EXECNO_OK;
    case CS2_OP_RT7_GETENABLED:
        return push_bool(vm, state->rt7_enabled);

    /* translations_set(key, value) / translations_clear (later client). */
    case CS2_OP_TRANSLATIONS_SET:
        translation_set(state, strs[0] ? strs[0] : "", strs[1] ? strs[1] : "");
        return CS2VM_EXECNO_OK;
    case CS2_OP_TRANSLATIONS_CLEAR:
        RS_CS2ClientState_Free(state);
        return CS2VM_EXECNO_OK;

    /* The reboot countdown in client cycles, 0 when none is running. */
    case CS2_OP_REBOOTTIMER:
        return CS2VM2_PushInt(vm, state->reboot_timer_cycles);
    case CS2_OP_REBOOTMESSAGE:
        return CS2VM2_PushStr(vm, CS2VM2_StrDup(vm, state->reboot_message));
    /* The player-moderator flag from the login response. */
    case CS2_OP_PLAYERMOD:
        return push_bool(vm, state->player_moderator);
    /* The current world's flag mask. */
    case CS2_OP_WORLDFLAGS:
        return CS2VM2_PushInt(vm, state->world_flags);
    /* The idle countdown (native 216 0xd00): five minutes after the last input,
     * in milliseconds. */
    case CS2_OP_IDLETIMER_GET:
    {
        if( state->last_input_ms < 0 )
            return CS2VM2_PushInt(vm, 300000);
        int64_t const left = state->last_input_ms - state->now_ms + 300000;
        return CS2VM2_PushInt(vm, left < 0 ? 0 : (int)left);
    }
    case CS2_OP_IDLETIMER_RESET:
        state->last_input_ms = state->now_ms;
        return CS2VM_EXECNO_OK;
    /* The raw run energy (runenergy_visible is the same value / 100). */
    case CS2_OP_RUNENERGY:
        return CS2VM2_PushInt(vm, host->stats ? host->stats->run_energy_raw : 0);
    /* The third byte of UPDATE_STAT for one skill. The client has no bounds
     * check; an index outside its 25 skills throws. */
    case CS2_OP_STAT_UNKNOWN:
        if( ints[0] < 0 || ints[0] >= RS_CS2_CLIENT_STAT_COUNT )
        {
            TORIRS_LOG("cs2: stat_unknown index %d out of range\n", ints[0]);
            return CS2VM_EXECNO_ERROR;
        }
        return CS2VM2_PushInt(vm, state->stat_unknown[ints[0]]);
    case CS2_OP_UNKNOWN_COMMAND_3333:
        return CS2VM2_PushStr(vm, CS2VM2_StrDup(vm, state->server_string_3333));
    /* wec_name(worldentity config): "" for -1; the config's name otherwise,
     * whose default is the literal "null". */
    case CS2_OP_WEC_NAME:
        if( ints[0] == -1 )
            return CS2VM2_PushStr(vm, CS2VM2_StrEmpty(vm));
        return CS2VM2_PushStr(vm, CS2VM2_StrDup(vm, host->worldentity_config_name
                                                        ? host->worldentity_config_name(host->world_user, ints[0])
                                                        : "null"));

    /* Steam (native 216 ExecuteCommand3700To3799): each op answers whether the
     * Steam SDK call succeeded, and with no SDK that is 0. */
    case CS2_OP_STEAM_SETACHIEVEMENT:
    case CS2_OP_STEAM_SETSTAT:
    case CS2_OP_STEAM_STORESTATS:
        return CS2VM2_PushInt(vm, 0);

    /* Federated login (native 216 5600-5699) through a partner platform this
     * client has none of: a login request does nothing, the state is 0. */
    case CS2_OP_FEDERATED_LOGIN:
    case CS2_OP_FEDERATED_SHOP:
        return CS2VM_EXECNO_OK;
    case CS2_OP_FEDERATED_LOGIN_STATE:
        return CS2VM2_PushInt(vm, 0);

    /* The popout sidebar (native 216 RequestOpenPopoutSideBar /
     * RequestClosePopoutSideBar): reserve `width` pixels for sidebar slot
     * `slot`, or release the slot. */
    case CS2_OP_SIDEBAR_SETWIDTH:
        if( ints[0] >= 0 && ints[0] < RS_CS2_SIDEBAR_SLOTS )
            state->sidebar_width[ints[0]] = ints[1];
        return CS2VM_EXECNO_OK;
    case CS2_OP_SIDEBAR_CLEARWIDTH:
        if( ints[0] >= 0 && ints[0] < RS_CS2_SIDEBAR_SLOTS )
            state->sidebar_width[ints[0]] = 0;
        return CS2VM_EXECNO_OK;

    case CS2_OP_SETFOLLOWEROPSLOWPRIORITY:
        state->follower_ops_low_priority = ints[0] == 1;
        return CS2VM_EXECNO_OK;
    /* 5 when launched through a Jagex launcher session, else 0. */
    case CS2_OP_PLATFORMTYPE:
        return CS2VM2_PushInt(vm, state->platform_type);
    /* The client's revision and build: the later client pushes (240, 1). This
     * client speaks revision 239. */
    case CS2_OP_CLIENT_VERSION:
        if( CS2VM2_PushInt(vm, 239) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        return CS2VM2_PushInt(vm, 1);

    default:
        TORIRS_LOG("RS_CS2Host_ExecClientOp: unhandled opcode %d\n", args->opcode);
        assert(0 && "RS_CS2Host_ExecClientOp: unexpected opcode");
        return CS2VM_EXECNO_ERROR;
    }
}
