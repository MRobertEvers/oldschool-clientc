#ifndef SRC_GAME_RS_CLAN_H
#define SRC_GAME_RS_CLAN_H

/*
 * The client's clans: the clan CHANNEL (who is in the chat, and the chat's
 * talk/kick ranks), the clan SETTINGS (members, ranks, bans, owner, typed
 * settings) and the clan PROFILE varclan store.
 *
 * Mirrors the rev-239 client: ClanChannel class233, ClanSettings class245,
 * the varclan class541. Filled by the server packets CLANCHANNEL_FULL /
 * CLANCHANNEL_DELTA, CLANSETTINGS_FULL / CLANSETTINGS_DELTA and VARCLAN /
 * VARCLAN_ENABLE / VARCLAN_DISABLE; read by the 3800-3899 clientscript
 * commands and the core PUSH_VARCLANSETTING (74) / PUSH_VARCLAN (76).
 *
 * Every clan packet names its clan with a signed byte: 0..3 are the player's
 * own (AFFINED) clans, a negative byte the one guest clan being LISTENED to.
 * A full packet whose payload is only that byte means the player left it.
 *
 * The ACTIVE clan the getters read is chosen by the find commands and is a
 * snapshot: a later full packet builds a new object and the active one keeps
 * reading the old until a find runs again. That is kept exactly -- replaced
 * objects the active pointer still references are retired, not freed, until
 * nothing points at them.
 */

#include <stdbool.h>
#include <stdint.h>

#define RS_CLAN_AFFINED_SLOTS 4
#define RS_CLAN_NAME_LEN 80
/* Ranks with a meaning of their own: the owner is always 126, and the member
 * who takes over when the owner leaves is 125. A channel guest is -1. */
#define RS_CLAN_RANK_OWNER 126
#define RS_CLAN_RANK_REPLACEMENT_OWNER 125
#define RS_CLAN_RANK_GUEST (-1)

struct RS_ClanChannelUser
{
    char name[RS_CLAN_NAME_LEN];
    int rank; /* signed byte */
    int world;
};

struct RS_ClanChannel
{
    int64_t clan_hash;
    int64_t update_num;
    char name[RS_CLAN_NAME_LEN];
    int rank_kick; /* signed byte */
    int rank_talk; /* signed byte */
    bool has_display_names;
    struct RS_ClanChannelUser* users;
    int user_count;
    int user_cap;
    /* Slots in name order, built on demand, dropped on add/remove. */
    int* sorted;
};

enum RS_ClanSettingType
{
    RS_CLAN_SETTING_INT = 0,
    RS_CLAN_SETTING_LONG = 1,
    RS_CLAN_SETTING_STRING = 2,
};

struct RS_ClanSetting
{
    int id; /* low 30 bits of the wire key */
    int type; /* enum RS_ClanSettingType */
    int int_value;
    int64_t long_value;
    char* string_value;
};

struct RS_ClanSettings
{
    int update_num;
    int creation_time;
    char name[RS_CLAN_NAME_LEN];
    bool allow_unaffined;
    int rank_talk;
    int rank_kick;
    int rank_lootshare;
    int rank_coinshare;
    bool has_hashes;
    bool has_display_names;

    /* Affined members, parallel arrays. A name is absent (has_name false)
     * when the server sent none. */
    int affined_count;
    int affined_cap;
    int64_t* affined_hash;
    char (*affined_name)[RS_CLAN_NAME_LEN];
    bool* affined_has_name;
    int8_t* affined_rank;
    int* affined_extra_info;
    int* affined_join_runeday;
    bool* affined_muted;
    int* sorted; /* slots in lower-cased name order, built on demand */

    int banned_count;
    int banned_cap;
    int64_t* banned_hash;
    char (*banned_name)[RS_CLAN_NAME_LEN];
    bool* banned_has_name;

    int owner_slot;             /* -1 with no members */
    int replacement_owner_slot; /* -1 when none */

    struct RS_ClanSetting* settings;
    int setting_count;
    int setting_cap;
};

enum RS_VarClanType
{
    RS_VARCLAN_INT = 0,
    RS_VARCLAN_LONG = 1,
    RS_VARCLAN_STRING = 2,
};

struct RS_VarClan
{
    int id;
    int type; /* enum RS_VarClanType */
    int int_value;
    int64_t long_value;
    char* string_value;
};

struct RS_ClanStore
{
    struct RS_ClanChannel* affined_channel[RS_CLAN_AFFINED_SLOTS];
    struct RS_ClanChannel* listened_channel;
    struct RS_ClanSettings* affined_settings[RS_CLAN_AFFINED_SLOTS];
    struct RS_ClanSettings* listened_settings;

    /* What the 38xx getters read; see the header comment. NULL until a find
     * succeeds. */
    struct RS_ClanChannel* active_channel;
    struct RS_ClanSettings* active_settings;
    /* The affined index the last successful activeclanchannel_find_affined
     * chose; activeclanchannel_kickuser sends for it. 0 until then. */
    int active_channel_affined_index;

    /* Objects a full packet replaced while the active pointer still read
     * them. Freed when the pointer moves or the store resets. */
    struct RS_ClanChannel* retired_channel;
    struct RS_ClanSettings* retired_settings;

    /* The clan profile. `varclan_enabled` false is "no profile": CLANPROFILE_FIND
     * answers 0. */
    bool varclan_enabled;
    struct RS_VarClan* varclans;
    int varclan_count;
    int varclan_cap;

    /* Bumped by every channel / settings packet, before decoding, which is
     * what fires onclanchanneltransmit / onclansettingstransmit. */
    int channel_transmit_serial;
    int settings_transmit_serial;
};

void
RS_ClanStore_Init(struct RS_ClanStore* store);

/** Free every clan object. The varclan store survives, as it does a login in
 *  the client, unless `include_varclan` is set. */
void
RS_ClanStore_Reset(struct RS_ClanStore* store, bool include_varclan);

void
RS_ClanStore_Free(struct RS_ClanStore* store);

/* ---- packets ----------------------------------------------------------------
 *
 * Each returns false when the payload does not decode. A FULL packet that fails
 * leaves the store as it was. A DELTA applies its events in order, as the
 * client does, so one that fails part-way keeps the events before the failure
 * and does not advance the update number -- the next delta then re-requests
 * the full state. The DELTA forms set `*out_request_full` to the clan byte to
 * re-request in full (the object is missing or behind), or leave it at
 * INT32_MIN.
 */

bool
RS_ClanStore_ApplyChannelFull(
    struct RS_ClanStore* store,
    uint8_t const* data,
    int length);

bool
RS_ClanStore_ApplyChannelDelta(
    struct RS_ClanStore* store,
    uint8_t const* data,
    int length,
    int* out_request_full);

bool
RS_ClanStore_ApplySettingsFull(
    struct RS_ClanStore* store,
    uint8_t const* data,
    int length);

bool
RS_ClanStore_ApplySettingsDelta(
    struct RS_ClanStore* store,
    uint8_t const* data,
    int length,
    int* out_request_full);

void
RS_ClanStore_VarClanEnable(struct RS_ClanStore* store);

void
RS_ClanStore_VarClanDisable(struct RS_ClanStore* store);

/** VARCLAN: g2 id, then the value in the var's type -- which the caller
 *  resolves from the VarClanType config (`type`, enum RS_VarClanType). */
bool
RS_ClanStore_ApplyVarClan(
    struct RS_ClanStore* store,
    uint8_t const* data,
    int length,
    int (*type_of)(void* user, int var_id),
    void* type_user);

/* ---- script reads ---------------------------------------------------------- */

/** activeclansettings_find_listened / _affined. Out-of-range `index` is the
 *  caller's to reject. */
bool
RS_ClanStore_FindSettings(
    struct RS_ClanStore* store,
    int index);

bool
RS_ClanStore_FindChannel(
    struct RS_ClanStore* store,
    int index);

/** activeclansettings_getaffinedslot: the slot named `name` (any case), -1. */
int
RS_ClanSettings_AffinedSlotByName(
    struct RS_ClanSettings* settings,
    char const* name);

/** activeclansettings_getsortedaffinedslot: the slot at name-order position
 *  `position`, or -1 out of range. */
int
RS_ClanSettings_SortedAffinedSlot(
    struct RS_ClanSettings* settings,
    int position);

/** activeclansettings_getaffinedextrainfo: `(extra & mask(end)) >>> start`. */
int
RS_ClanSettings_ExtraInfoBits(
    struct RS_ClanSettings const* settings,
    int slot,
    int start_bit,
    int end_bit);

/** An int clan setting (PUSH_VARCLANSETTING), or false when absent or not
 *  an int. */
bool
RS_ClanSettings_IntSetting(
    struct RS_ClanSettings const* settings,
    int id,
    int* out_value);

/** activeclanchannel_getuserslot: first user named `name`, any case, or -1. */
int
RS_ClanChannel_UserSlotByName(
    struct RS_ClanChannel const* channel,
    char const* name);

/** activeclanchannel_getsorteduserslot, or -1 out of range. */
int
RS_ClanChannel_SortedUserSlot(
    struct RS_ClanChannel* channel,
    int position);

/** A varclan, or NULL when the profile does not hold it. */
struct RS_VarClan const*
RS_ClanStore_VarClan(
    struct RS_ClanStore const* store,
    int id);

#endif
