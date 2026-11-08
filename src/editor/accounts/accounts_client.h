#ifndef SRC_EDITOR_ACCOUNTS_ACCOUNTS_CLIENT_H
#define SRC_EDITOR_ACCOUNTS_ACCOUNTS_CLIENT_H

/*
 * The client half of the accounts module (torirsmaped/torirs_maped_accounts.h
 * defines the frames): what the editor server last said about the lane's
 * accounts (FACT_ACCOUNTS), the account one window looks at (FACT_ACCOUNT),
 * the content's quest rows (FACT_QUESTS) and the status lines. Fed frames,
 * never a socket: an Editor_AccountsTransport carries the requests up and
 * the facts down -- the editor host in the client (accounts_panel.c), a raw
 * session in a test.
 */

#include <stddef.h>
#include <stdint.h>

#include "torirsmaped/torirs_maped_accounts.h"
#include "torirsmaped/torirs_maped_buf.h"

enum
{
    EDITOR_ACCOUNTS_MAX = 64,
    EDITOR_ACCOUNTS_VARPS_MAX = 512,
    /** Backpack, worn, bank, then the other containers' items. */
    EDITOR_ACCOUNTS_ITEMS_MAX = 28 + 14 + 1200 + 4096,
    /** configs/all.inv's invs the model keeps (FACT_INVS). */
    EDITOR_ACCOUNTS_INVS_MAX = 2048,
    EDITOR_ACCOUNTS_QUEUE_MAX = 64,
    EDITOR_ACCOUNTS_QUESTS_MAX = 320,
    EDITOR_ACCOUNTS_LOG_LINES = 64,
    EDITOR_ACCOUNTS_LOG_LINE_MAX = 192,
};

struct Editor_AccountRow
{
    char name[64];
    uint32_t flags;
    int32_t x;
    int32_t z;
    int32_t level;
    uint32_t queued;
};

struct Editor_AccountVarp
{
    uint32_t id;
    int32_t value;
    char name[96];
};

/** Item sections: the three spelled containers, then the view's other
 *  containers (`EDITOR_ACCOUNTS_SECTION_CONTAINER + i` is containers[i]). */
enum
{
    EDITOR_ACCOUNTS_SECTION_INV = 0,
    EDITOR_ACCOUNTS_SECTION_WORN = 1,
    EDITOR_ACCOUNTS_SECTION_BANK = 2,
    EDITOR_ACCOUNTS_SECTION_CONTAINER = 3,
};

struct Editor_AccountItem
{
    /** 0 inv, 1 worn, 2 bank, 3 + i the view's containers[i]. */
    int section;
    uint32_t slot;
    int32_t obj;
    int32_t count;
};

struct Editor_AccountQuestState
{
    char dbrow[64];
    uint32_t state;
    int32_t value;
};

/** One `[container.<inv>]` of a view. */
struct Editor_AccountContainer
{
    uint32_t inv;
    char name[64];
    /** configs/all.inv's size; 0 when it states none. */
    uint32_t size;
};

/** What an account looks like: the save's look and its worn items. */
struct Editor_AccountLook
{
    /** The account (a save file name) this is; "" none. */
    char name[64];
    int gender;
    /** The kit at each wear position (-1 none), the server's resolution. */
    int32_t kits[TORIRSMAPED_ACCT_LOOK_SLOTS];
    int32_t colours[TORIRSMAPED_ACCT_LOOK_COLOURS];
    /** The obj worn at each worn slot, -1 empty. */
    int32_t worn[TORIRSMAPED_ACCT_WORN_SLOTS];
};

/** One FACT_LOADOUTS row; items are sections 0 (backpack) and 1 (worn). */
struct Editor_AccountLoadout
{
    char name[64];
    struct Editor_AccountItem items[TORIRSMAPED_ACCT_WORN_SLOTS + TORIRSMAPED_ACCT_INV_SLOTS];
    int item_count;
};

/** One FACT_INVS row. */
struct Editor_AccountInv
{
    uint32_t id;
    char name[64];
    uint32_t size;
};

/** FACT_ACCOUNT. */
struct Editor_AccountView
{
    uint32_t status;
    char name[64];
    uint32_t flags;
    char status_text[256];
    char display[64];
    int32_t x;
    int32_t z;
    int32_t level;
    int32_t boosted[TORIRSMAPED_ACCT_STATS];
    int32_t xp_tenths[TORIRSMAPED_ACCT_STATS];
    struct Editor_AccountVarp varps[EDITOR_ACCOUNTS_VARPS_MAX];
    int varp_count;
    struct Editor_AccountItem items[EDITOR_ACCOUNTS_ITEMS_MAX];
    int item_count;
    char queue[EDITOR_ACCOUNTS_QUEUE_MAX][200];
    int queue_count;
    struct Editor_AccountQuestState quests[EDITOR_ACCOUNTS_QUESTS_MAX];
    int quest_count;
    /** The look (has_look 0 from a server that sends none). */
    int has_look;
    struct Editor_AccountLook look;
    struct Editor_AccountContainer containers[TORIRSMAPED_ACCT_CONTAINERS_MAX];
    int container_count;
};

/** FACT_QUESTS rows. */
struct Editor_AccountQuest
{
    char dbrow[64];
    char display[64];
    char var[32];
    int32_t complete_value;
};

struct Editor_AccountsModel
{
    /* ---- FACT_ACCOUNTS ---- */
    /** EDITOR_HOST_UNSUPPORTED while the server has no lane. */
    uint32_t list_status;
    char live_dir[1024];
    char play_account[64];
    int attached;
    struct Editor_AccountRow rows[EDITOR_ACCOUNTS_MAX];
    int row_count;
    int list_serial;

    /* ---- FACT_ACCOUNT: the newest for `view.name` ---- */
    struct Editor_AccountView view;
    int view_serial;

    /* ---- FACT_QUESTS ---- */
    struct Editor_AccountQuest quests[EDITOR_ACCOUNTS_QUESTS_MAX];
    int quest_count;
    int quests_serial;

    /* ---- the play account's look, from any FACT_ACCOUNT for it (the
     *      followed account or not): what Place player's ghost wears ---- */
    struct Editor_AccountLook play_look;
    /** 1 when play_look is the CURRENT play account's. */
    int play_look_known;
    int play_look_serial;

    /* ---- FACT_LOADOUTS ---- */
    uint32_t loadouts_status;
    struct Editor_AccountLoadout loadouts[TORIRSMAPED_ACCT_LOADOUTS_MAX];
    int loadout_count;
    int loadouts_serial;

    /* ---- FACT_INVS ---- */
    struct Editor_AccountInv invs[EDITOR_ACCOUNTS_INVS_MAX];
    int inv_count;
    int invs_serial;

    /* ---- FACT_ACCOUNT_STATUS: the newest, and a log ---- */
    uint32_t last_request;
    uint32_t last_status;
    char last_account[64];
    char last_message[1024];
    int status_serial;
    char log[EDITOR_ACCOUNTS_LOG_LINES][EDITOR_ACCOUNTS_LOG_LINE_MAX];
    int log_head;
    int log_count;
    int log_serial;
};

void
Editor_AccountsModelInit(struct Editor_AccountsModel* model);

/** 1 when `type` is one of the accounts facts (191..200). */
int
Editor_AccountsIsFact(uint32_t type);

/**
 * Fold one fact into the model. 1 applied, 0 not an accounts fact, -1 a
 * malformed one (dropped, logged). A FACT_ACCOUNT for an account other than
 * `follow` (when non-NULL and non-empty) is logged and not kept, so a window
 * keeps looking at its own account while another window's view arrives.
 */
int
Editor_AccountsModelApply(
    struct Editor_AccountsModel* model,
    const char* follow,
    uint32_t type,
    const uint8_t* body,
    uint32_t length);

/** Append one line to the model's log (the panel echoes the Live log's
 *  answers to an online cheat here). */
void
Editor_AccountsModelNote(
    struct Editor_AccountsModel* model,
    const char* text);

/** The `index`th newest log line (0 newest), or NULL past the end. */
const char*
Editor_AccountsLogLine(
    const struct Editor_AccountsModel* model,
    int index);

/** The list row named `name` (a save file name), or NULL. */
const struct Editor_AccountRow*
Editor_AccountsRow(
    const struct Editor_AccountsModel* model,
    const char* name);

/** The quest row of `dbrow`, or NULL. */
const struct Editor_AccountQuest*
Editor_AccountsQuest(
    const struct Editor_AccountsModel* model,
    const char* dbrow);

/** The view's item in `section` (0 inv, 1 worn, 2 bank) at `slot`, or NULL. */
const struct Editor_AccountItem*
Editor_AccountsItem(
    const struct Editor_AccountView* view,
    int section,
    uint32_t slot);

/** The view's varp `id` value; 1 when the view holds it. */
int
Editor_AccountsVarp(
    const struct Editor_AccountView* view,
    uint32_t id,
    int32_t* out_value);

/* ---- requests: each appends the PAYLOAD and returns the frame type -------- */

uint32_t
Editor_AccountsEncodeList(struct ToriRSMapEdBuf* out);

uint32_t
Editor_AccountsEncodeQuests(struct ToriRSMapEdBuf* out);

uint32_t
Editor_AccountsEncodeLoadouts(struct ToriRSMapEdBuf* out);

uint32_t
Editor_AccountsEncodeInvs(struct ToriRSMapEdBuf* out);

/** LOADOUT_SAVE: `account`'s worn items and backpack become `loadout`. */
uint32_t
Editor_AccountsEncodeLoadoutSave(
    struct ToriRSMapEdBuf* out,
    const char* loadout,
    const char* account);

uint32_t
Editor_AccountsEncodeLoadoutDelete(
    struct ToriRSMapEdBuf* out,
    const char* loadout);

/** The loadout named `name` (its kept spelling), or NULL. */
const struct Editor_AccountLoadout*
Editor_AccountsLoadout(
    const struct Editor_AccountsModel* model,
    const char* name);

/** The inv `id` of FACT_INVS, or NULL. */
const struct Editor_AccountInv*
Editor_AccountsInv(
    const struct Editor_AccountsModel* model,
    uint32_t id);

uint32_t
Editor_AccountsEncodeView(
    struct ToriRSMapEdBuf* out,
    const char* name);

uint32_t
Editor_AccountsEncodeCreate(
    struct ToriRSMapEdBuf* out,
    const char* name,
    const char* password,
    const char* fixture);

uint32_t
Editor_AccountsEncodeDuplicate(
    struct ToriRSMapEdBuf* out,
    const char* name,
    const char* new_name,
    const char* password);

uint32_t
Editor_AccountsEncodeDelete(
    struct ToriRSMapEdBuf* out,
    const char* name);

uint32_t
Editor_AccountsEncodeUse(
    struct ToriRSMapEdBuf* out,
    const char* name);

/** ACCOUNT_SET: `key` and `value` as torirs_maped_accounts.h spells them. */
uint32_t
Editor_AccountsEncodeSet(
    struct ToriRSMapEdBuf* out,
    const char* name,
    const char* key,
    const char* value);

uint32_t
Editor_AccountsEncodePlace(
    struct ToriRSMapEdBuf* out,
    const char* name,
    int x,
    int z,
    int level);

/* ---- the transport ---------------------------------------------------------- */

struct Editor_AccountsTransport
{
    void* user;
    /** Send one request frame. 1 sent, 0 the connection is gone. */
    int (*send)(void* user, uint32_t type, const uint8_t* payload, uint32_t length);
    /** Take the next accounts fact that arrived, or return 0. The body is
     *  the transport's until `release`. */
    int (*take)(void* user, uint32_t* out_type, uint8_t** out_body, uint32_t* out_length);
    void (*release)(void* user, uint8_t* body);
};

/** Send `payload` (freed) as `type`. Returns the transport's answer. */
int
Editor_AccountsSend(
    const struct Editor_AccountsTransport* transport,
    uint32_t type,
    struct ToriRSMapEdBuf* payload);

/** Drain every pending fact into `model` (FACT_ACCOUNT kept only for
 *  `follow`, see Editor_AccountsModelApply). Returns how many applied. */
int
Editor_AccountsDrain(
    const struct Editor_AccountsTransport* transport,
    struct Editor_AccountsModel* model,
    const char* follow);

/** "online at 3222,3218 level 0" / "offline" for a list row. */
void
Editor_AccountsRowLine(
    const struct Editor_AccountRow* row,
    char* out,
    size_t out_size);

#endif
