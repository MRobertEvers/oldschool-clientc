#ifndef SRC_TORIRSMAPED_TORIRS_MAPED_ACCOUNTS_H
#define SRC_TORIRSMAPED_TORIRS_MAPED_ACCOUNTS_H

/*
 * Accounts: the lane's player saves, managed from the editor
 * (docs/MAPED_STATUS.md "Accounts"; owner `accounts`).
 *
 * The lane's game server keeps its players under `<live_dir>/saves/`
 * (torirs_maped_live.c sets TORIRSSERVER_SAVES there): one `<name>.ini` per
 * account (torirs_server_save.c's format), the password records under
 * `saves/accounts/`, the quest harness's checkpoints under
 * `saves/checkpoints/`. Only the editor server knows the live dir, so every
 * account operation is a request here; windows never touch the files.
 *
 * Two ways an edit is applied, chosen HERE from the server's player list
 * (CTL_FACT_PLAYERS, torirs_maped_live.c):
 *
 *   online   the account is logged in on the attached lane: the edit is a
 *            cheat through the control link (CTL_CHEAT: setlevel, setvar,
 *            item, wield, tele, complete, questmark, maxstats, ...), and a
 *            REFRESH asks the server for `::checkpoint <leg>`, whose file
 *            (`saves/checkpoints/<leg>.ini`, every non-zero varp) is read
 *            back as the account's SNAPSHOT -- what the view shows while the
 *            account is online (flag TORIRSMAPED_ACCT_SNAPSHOT).
 *   offline  the save file is edited in place with a line-preserving codec
 *            (comments and keys this build does not know kept; the server
 *            skips unknown keys). Quest completion has no offline form: those
 *            cheats are QUEUED per account in `<live_dir>/accounts.ini` and
 *            flushed a few seconds after the account logs in.
 *
 * `<live_dir>/accounts.ini` (this module's, the same codec): [lane]
 * play_user / play_pass -- the account RUN and Play log in as, surviving a
 * daemon restart; [passwords] <name> = <password> for accounts made here (a
 * dev lane: plain text, as the lane's default password is in the code);
 * [queue] <name>.<n> = <cheat line>.
 *
 * Wire (protocol v2; strings `u32 len, bytes`; requests 65..80 and facts
 * 191..200 are this owner's, torirs_maped.h):
 *
 *   ACCOUNTS_LIST   (empty)
 *       -> FACT_ACCOUNTS  u32 status (UNSUPPORTED: no lane), string live_dir,
 *                         string play_account, u32 link_attached, u32 count,
 *                         count * (string name, u32 flags, i32 x, i32 z,
 *                         i32 level, u32 queued)
 *       Broadcast to every v2 connection whenever the set, an online state
 *       or the play account changes.
 *   ACCOUNT_VIEW    string name
 *       -> FACT_ACCOUNT   u32 status, string name, u32 flags, string status
 *                         ("online at 3222,3218 level 0 ..." / "offline: edits
 *                         write the save"), string display_name, i32 x,
 *                         i32 z, i32 level, u32 stat_count, stat_count *
 *                         (i32 boosted, i32 xp_tenths), u32 varp_count,
 *                         varp_count * (u32 id, i32 value, string name),
 *                         u32 inv_count, inv_count * (u32 slot, i32 obj,
 *                         i32 count), u32 worn_count, ..., u32 bank_count,
 *                         ..., u32 queue_count, queue_count * string,
 *                         u32 quest_count, quest_count * (string dbrow,
 *                         u32 state (TORIRSMAPED_QUEST_*), i32 value)
 *       Broadcast (every window shows the same account the same way); a
 *       status NOT_FOUND names a missing save.
 *   ACCOUNT_CREATE  string name, string password, string fixture ("" = the
 *                   lane's play fixture)            -> FACT_ACCOUNT_STATUS,
 *                   then FACT_ACCOUNTS + FACT_ACCOUNT broadcast
 *   ACCOUNT_DELETE  string name                     -> FACT_ACCOUNT_STATUS,
 *                   FACT_ACCOUNTS (refused while online)
 *   ACCOUNT_DUPLICATE string name, string new_name, string password
 *                                                   -> as CREATE
 *   ACCOUNT_USE     string name                     -> FACT_ACCOUNT_STATUS,
 *                   FACT_ACCOUNTS, FACT_LIVE_PLAY (the play account changed)
 *   ACCOUNT_SET     string name, string key, string value
 *                                                   -> FACT_ACCOUNT_STATUS
 *                   (its message names the path: "online: ::setlevel 7 50"
 *                   or "offline: wrote saves/x.ini"), then FACT_ACCOUNT
 *       keys (one vocabulary for the panel and `torirsmapedctl account-set`):
 *         player.x | player.z | player.level  <n>
 *         player.pos                           <x> <z> <level>
 *         stats.<id|name>                      <level 1..99>
 *         stats.<id|name>.xp                   <xp>   (online: grows only)
 *         stats.all                            max | reset
 *         varps.<id|name>  varbits.<id|name>   <value>
 *         vars.<name>                          <value> (varbit first, then varp)
 *         inv.<slot> | worn.<slot> | bank.<slot>  <obj id|name> [count] | -
 *         inv.clear
 *         quests.<dbrow>                       complete | mark
 *         quests.all                           mark
 *         cheat                                <line> (online: sent; offline: queued)
 *         refresh                              (online: a checkpoint)
 *   ACCOUNT_PLACE   string name, i32 x, i32 z, i32 level
 *                                                   -> as SET player.pos
 *   ACCOUNTS_QUESTS (empty)
 *       -> FACT_QUESTS    u32 count, count * (string dbrow, string display,
 *                         string var ("varp 29", "varbit 5078", "proc"),
 *                         i32 complete_value)
 *       The quest rows of the content's `quest` dbtable (configs/all.dbrow)
 *       with the state each one's `::complete` arm writes
 *       (quests/scripts/quest_cheat.rs2 + the `^constant`s). A quest whose
 *       arm goes through a proc has var "proc": its state cannot be read
 *       from a save.
 *
 *   FACT_ACCOUNT_STATUS  u32 request, u32 status (enum EditorHost_Status),
 *                        string account, string message. To the requester;
 *                        a queue flush broadcasts one with request 0.
 *
 * Varcs are client-side: no server op or cheat writes one for a logged-in
 * player, and a save holds none, so they are not here (the status doc says
 * so).
 *
 * BUILD NOTE: torirs_maped_accounts.c is compiled INTO torirs_maped.c after
 * torirs_maped_live.c (it reads the jobs' player list and sends on the
 * control link). Never link it on its own.
 */

#include "torirs_maped.h"

#include <stddef.h>
#include <stdint.h>

enum
{
    TORIRSMAPED_REQ_ACCOUNTS_LIST = 65,
    TORIRSMAPED_REQ_ACCOUNT_VIEW = 66,
    TORIRSMAPED_REQ_ACCOUNT_CREATE = 67,
    TORIRSMAPED_REQ_ACCOUNT_DELETE = 68,
    TORIRSMAPED_REQ_ACCOUNT_DUPLICATE = 69,
    TORIRSMAPED_REQ_ACCOUNT_USE = 70,
    TORIRSMAPED_REQ_ACCOUNT_SET = 71,
    TORIRSMAPED_REQ_ACCOUNT_PLACE = 72,
    TORIRSMAPED_REQ_ACCOUNTS_QUESTS = 73,
    TORIRSMAPED_REQ_ACCOUNTS_RANGE_FIRST = 65,
    TORIRSMAPED_REQ_ACCOUNTS_RANGE_LAST = 80,

    TORIRSMAPED_FACT_ACCOUNTS = 191,
    TORIRSMAPED_FACT_ACCOUNT = 192,
    TORIRSMAPED_FACT_ACCOUNT_STATUS = 193,
    TORIRSMAPED_FACT_QUESTS = 194,
    TORIRSMAPED_FACT_ACCOUNTS_RANGE_FIRST = 191,
    TORIRSMAPED_FACT_ACCOUNTS_RANGE_LAST = 200,
};

/** FACT_ACCOUNTS / FACT_ACCOUNT flags. */
enum
{
    /** The attached lane lists the account among its players. */
    TORIRSMAPED_ACCT_ONLINE = 1,
    /** The lane's play account (RUN and Play log in as it). */
    TORIRSMAPED_ACCT_PLAY = 2,
    /** The editor knows its password (made here, or the lane's default). */
    TORIRSMAPED_ACCT_PASSWORD = 4,
    /** The view's values are a server checkpoint, not the save. */
    TORIRSMAPED_ACCT_SNAPSHOT = 8,
    /** A checkpoint was asked for and has not landed yet. */
    TORIRSMAPED_ACCT_REFRESHING = 16,
    /** The save file exists (an account may be known only by its queue). */
    TORIRSMAPED_ACCT_SAVE = 32,
};

/** FACT_ACCOUNT quest states. */
enum
{
    /** Its arm goes through a proc, or the var could not be read. */
    TORIRSMAPED_QUEST_UNKNOWN = 0,
    TORIRSMAPED_QUEST_NOT_COMPLETE = 1,
    TORIRSMAPED_QUEST_COMPLETE = 2,
};

enum
{
    TORIRSMAPED_ACCT_STATS = 25,
    TORIRSMAPED_ACCT_INV_SLOTS = 28,
    TORIRSMAPED_ACCT_WORN_SLOTS = 14,
    /** Bank slots the view and an offline edit accept (the cache sizes the
     *  real one; a slot past its size is skipped by the server). */
    TORIRSMAPED_ACCT_BANK_SLOTS = 1200,
    /** Account names are 1..12 characters (the server's rule). */
    TORIRSMAPED_ACCT_NAME_MAX = 12,
    /** Queued cheats per account. */
    TORIRSMAPED_ACCT_QUEUE_MAX = 300,
};

/** The stat names in protocol order (pack/stat.pack); "?" past the end. */
static inline const char*
ToriRSMapEd_AcctStatName(int stat)
{
    static const char* const names[TORIRSMAPED_ACCT_STATS] = {
        "attack",    "defence",    "strength",  "hitpoints",  "ranged",   "prayer",       "magic",
        "cooking",   "woodcutting", "fletching", "fishing",   "firemaking", "crafting",   "smithing",
        "mining",    "herblore",   "agility",   "thieving",   "slayer",   "farming",      "runecraft",
        "hunter",    "construction", "sailing",  "summoning",
    };
    if( stat < 0 || stat >= TORIRSMAPED_ACCT_STATS )
        return "?";
    return names[stat];
}

/** The stat named `name` (a pack name or its decimal id), or -1. */
int
ToriRSMapEd_AcctStatFind(const char* name);

/** The experience a level starts at (the OSRS table, as the server's
 *  ToriRSServer_CombatXpForLevel); 0 for level 1. */
int
ToriRSMapEd_AcctXpForLevel(int level);

/** The level `xp` reaches, 1..99. */
int
ToriRSMapEd_AcctLevelForXp(int xp);

/** The server's save file name for an account: lower-case alphanumerics,
 *  ' ' / '-' / '_' as '_', everything else dropped ("" refuses). */
void
ToriRSMapEd_AcctFileName(
    const char* name,
    char* out,
    size_t out_size);

/** NULL when `name` and `password` may make an account (the server's login
 *  rules: name 1..12 word characters; password 8..20 with a letter and a
 *  digit, not containing the name); else why not. */
const char*
ToriRSMapEd_AcctCredentialsProblem(
    const char* name,
    const char* password);

/* ---- the save codec --------------------------------------------------------
 *
 * A player save (or this module's registry) as its LINES, so an edit touches
 * one line and the rest -- comments, blank lines, keys this build does not
 * know -- come back byte for byte. */

struct ToriRSMapEdAcctSave
{
    /** Each line without its newline. */
    char** lines;
    int count;
    int capacity;
    /** The text used "\r\n". */
    int crlf;
    /** The text ended with a newline (an empty text counts as yes). */
    int final_newline;
};

void
ToriRSMapEd_AcctSaveInit(struct ToriRSMapEdAcctSave* save);

/** NULL-tolerant. */
void
ToriRSMapEd_AcctSaveFree(struct ToriRSMapEdAcctSave* save);

/** Replace the contents with `text`. */
void
ToriRSMapEd_AcctSaveParse(
    struct ToriRSMapEdAcctSave* save,
    const char* text,
    size_t length);

/** The text, newline style kept. Returns the full length; writes at most
 *  `capacity` - 1 bytes and a NUL. */
size_t
ToriRSMapEd_AcctSaveEmit(
    const struct ToriRSMapEdAcctSave* save,
    char* out,
    size_t capacity);

/** 1 and the value (trimmed) when `[section]` holds `key`. */
int
ToriRSMapEd_AcctSaveGet(
    const struct ToriRSMapEdAcctSave* save,
    const char* section,
    const char* key,
    char* out,
    size_t out_size);

/** Set `key = value` in `[section]`: the line is rewritten in place, else
 *  appended to the section (before its trailing blank lines), the section
 *  made at the end when missing. Returns 1 when the text changed. */
int
ToriRSMapEd_AcctSaveSet(
    struct ToriRSMapEdAcctSave* save,
    const char* section,
    const char* key,
    const char* value);

/** Remove `key` from `[section]`. Returns 1 when a line went. */
int
ToriRSMapEd_AcctSaveRemove(
    struct ToriRSMapEdAcctSave* save,
    const char* section,
    const char* key);

/** The `index`th key line of `[section]` (0 first): 1 with key and value,
 *  0 past the end. Comment and blank lines do not count. */
int
ToriRSMapEd_AcctSaveNth(
    const struct ToriRSMapEdAcctSave* save,
    const char* section,
    int index,
    char* out_key,
    size_t key_size,
    char* out_value,
    size_t value_size);

/** 1 when the file read and parsed; 0 (and an empty save) when it does not
 *  exist or cannot be read. */
int
ToriRSMapEd_AcctSaveLoad(
    struct ToriRSMapEdAcctSave* save,
    const char* path);

/** Write atomically (a temporary beside, renamed over). 1 when written. */
int
ToriRSMapEd_AcctSaveWrite(
    const struct ToriRSMapEdAcctSave* save,
    const char* path);

/* ---- the quest index -------------------------------------------------------- */

struct ToriRSMapEdAcctQuest
{
    char dbrow[64];
    char display[64];
    /** 0 none (a proc arm), 1 varp, 2 varbit. */
    int var_kind;
    int var_id;
    /** The constant's name and value its arm writes; value -1 unresolved. */
    char constant[64];
    int complete_value;
};

/** Parse the `::complete` arms of quest_cheat.rs2's text: one row per
 *  `if ($row = <dbrow>) {` with the first `%var >= ^constant` (or
 *  `~proc >= ^constant`) test inside it. Returns the count (at most `max`). */
int
ToriRSMapEd_AcctQuestArms(
    const char* text,
    size_t length,
    struct ToriRSMapEdAcctQuest* out,
    int max);

/** The dbrow blocks of `table=quest` in an all.dbrow text: dbrow name and
 *  `data=displayname,<text>`. Returns the count. */
int
ToriRSMapEd_AcctQuestRows(
    const char* text,
    size_t length,
    struct ToriRSMapEdAcctQuest* out,
    int max);

/** `^name = <value>` in a .constant text: 1 and the value when found. */
int
ToriRSMapEd_AcctConstantFind(
    const char* text,
    size_t length,
    const char* name,
    int* out_value);

/* ---- the server hooks (torirs_maped.c) --------------------------------------- */

/** The accounts module's service pass: the registry restored once, logins
 *  noticed (queued cheats flushed), checkpoint refreshes read. Called from
 *  the session pump and the daemon's idle loop; never blocks. */
void
ToriRSMapEd_AccountsService(struct ToriRSMapEd* maped);

#endif
