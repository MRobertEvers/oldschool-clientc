#ifndef SRC_TORIRSMAPED_TORIRS_MAPED_ACCTSAVE_H
#define SRC_TORIRSMAPED_TORIRS_MAPED_ACCTSAVE_H

/*
 * A player save as text: the accounts module's pure pieces -- the stat
 * table, the level/xp table, the account and loadout name rules, and the
 * line-preserving save codec. Out of torirs_maped_accounts.h so that a
 * process with no editor daemon can edit a save the same way: the account
 * site's staff player editor (src/websiteserver/website_site.c) links
 * torirs_maped_acctsave.c on its own. The daemon still compiles it into
 * torirs_maped.c, right before torirs_maped_accounts.c.
 */

#include <stddef.h>
#include <stdint.h>

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
    /** The look: wear positions and design colours (the wire's). */
    TORIRSMAPED_ACCT_LOOK_SLOTS = 12,
    TORIRSMAPED_ACCT_LOOK_COLOURS = 5,
    /** `[container.<inv>]` sections a view carries, and the slots of one
     *  whose inv states no size (configs/all.inv). */
    TORIRSMAPED_ACCT_CONTAINERS_MAX = 32,
    TORIRSMAPED_ACCT_CONTAINER_SLOTS = 2048,
    /** Loadouts a lane keeps, and a loadout name's length. */
    TORIRSMAPED_ACCT_LOADOUTS_MAX = 64,
    TORIRSMAPED_ACCT_LOADOUT_NAME_MAX = 32,
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

/** A loadout name as it is kept: lower-case alphanumerics and '_' (' ' and
 *  '-' become '_', anything else dropped), at most
 *  TORIRSMAPED_ACCT_LOADOUT_NAME_MAX; "" refuses. */
void
ToriRSMapEd_AcctLoadoutName(
    const char* name,
    char* out,
    size_t out_size);
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

#endif
