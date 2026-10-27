#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_AUTH_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_AUTH_H

/*
 * Passwords: an account record per login name, checked at GAMELOGIN.
 *
 * Until this existed the server read the password off the wire and dropped
 * it, so anyone who typed a name got that character. That is the right
 * behaviour for a development server -- a harness logs in with whatever it
 * likes -- and the wrong one for a world on a network, so the check is a
 * property of the server (`ToriRSServer.check_passwords`), not of the code:
 *
 *   torirsserver                 checks
 *   torirsserver --dev           does not (also TORIRSSERVER_DEV=1)
 *   dev_/alt_/bank_torirsserver  do not (TORIRSSERVER_DEV=0 turns it on)
 *   the in-client embedded world does not; it serves one local player
 *
 * Every local runner starts its server in development mode: ./launch
 * (profile `dev=0` to check), run-live.sh/.ps1, run-runelite.sh and
 * profile-mac.sh (TORIRSSERVER_DEV=0 to check). Only the deploy package
 * (tools/deploy) runs the plain binary as it is.
 *
 * The record lives beside the character, keyed by the same sanitised name
 * (ToriRSServer_AccountPath):
 *
 *   <saves>/accounts/<name>.ini
 *     [account]
 *     password = pbkdf2-sha256$<iterations>$<salt hex>$<key hex>
 *     admin = 1        optional; staff: the jmod crown, and ::commands on any
 *                      server, whatever its settings (ToriRSServer_PlayerMayCheat)
 *     managed_by = matt   optional; the account is LINKED to that web login
 *                      (ToriRSServer_AuthLink) and its password no longer signs
 *                      it in -- see "Linked player accounts" below
 *     link_hash = <64 hex>   with managed_by: SHA-256 of the link token
 *
 * A separate file rather than a key in the character save, because the save
 * is rewritten from the player struct on every logout and is hand-edited, and
 * a credential should survive both without riding through either.
 *
 * **The first login claims the name.** A name with no record takes the
 * password it logged in with, provided it passes the password rules
 * (ToriRSServer_AuthPasswordProblem), which is also how an existing character
 * (saved before passwords) gets one, and how an operator resets a forgotten
 * one: delete the record and the next login sets it.
 *
 * **The sign-in gate is a second, separate account.** A world served to the
 * internet runs io_server with `--gate` in front of it (docs/WEB_SERVERS.md
 * "The sign-in gate"): a plain HTML page that serves nothing else until the
 * visitor signs in with a web login. Web logins live in the account
 * server (`<saves>/accounts.sqlite`, accountserver/account_server.h), not beside
 * the players, and come only from invites an operator made
 * (`torirsserver --account invite [name]`), redeemed on the gate's /invite page
 * under the same password rules. Past the page the game asks for a player
 * account exactly as above; the two are unrelated, and the gate's login makes
 * nobody staff. The gate's session is a ticket -- the web login's name and an
 * expiry under an HMAC keyed by `<saves>/gate/gate.key` -- which io_server
 * issues as a cookie and torirsserver requires on the game WebSocket, so the
 * socket is as closed as the page and the two processes share a key file and
 * nothing else.
 *
 * **Linked player accounts.** A web login may take player accounts under it:
 * on the gate's `/account/link` the visitor proves the player account is theirs
 * with its password (a gate session alone must never be able to take a name).
 * The account must already exist -- a link never creates one; a name is made
 * by logging in to the game --
 * and the link mints a random LINK TOKEN: the web login's links database
 * (`<saves>/gate/links.sqlite`, io_gate_links.c) keeps the token, and the
 * record gains `managed_by = <web login>` and `link_hash = SHA-256(token)`.
 * From then on the account signs in only through that login: io_server's
 * `/account/play` presents the stored token for the ticket's login, and a
 * one-time login is minted only when the record names that login AND the
 * token hashes to its link_hash (ToriRSServer_AuthLinkVerify). So an account
 * has exactly one owner and one live link -- linking it again, even by its
 * owner, mints a new token and the old one is dead -- and its password, its
 * refresh tokens (revoked when it is linked) and every other login are
 * refused. `torirsserver --account unlink <name>` clears both keys and gives
 * the account its password back; the database's row then fails the check and
 * is dropped.
 *
 * PBKDF2-HMAC-SHA256 on the SHA-256 the login handshake already carries
 * (src/net/sha256.c); the iteration count is stored per record, so raising
 * TORIRSSERVER_AUTH_ITERATIONS applies to new records without invalidating
 * old ones.
 */

#include <stddef.h>
#include <stdint.h>

/* Each check runs on the host loop, so this is a cost every login pays inside
 * a tick: 21 ms on an M4 at -O3 (100 ms at -O0), measured 2026-10-15. */
#define TORIRSSERVER_AUTH_ITERATIONS 60000
#define TORIRSSERVER_AUTH_SALT_BYTES 16
#define TORIRSSERVER_AUTH_KEY_BYTES 32
/* "pbkdf2-sha256$" + iterations + '$' + salt hex + '$' + key hex + NUL */
#define TORIRSSERVER_AUTH_RECORD_MAX 160

/* The password rules: 8 to 20 characters -- 20 is what the login screen's
 * field takes -- printable ASCII without spaces, a letter and a digit, not one
 * character repeated, and not containing the account name. */
#define TORIRSSERVER_AUTH_PASSWORD_MIN 8
#define TORIRSSERVER_AUTH_PASSWORD_MAX 20
/* A display name's length, OldSchool's. */
#define TORIRSSERVER_AUTH_NAME_MAX 12

#define TORIRSSERVER_INVITE_CODE_BYTES 16
/* A link token: 32 random bytes as hex. The record keeps its SHA-256 (also 64
 * hex); the owner's links database keeps the token. */
#define TORIRSSERVER_LINK_TOKEN_HEX 64

#define TORIRSSERVER_GATE_KEY_BYTES 32
/* The cookie io_server sets and torirsserver reads off the upgrade request. */
#define TORIRSSERVER_GATE_COOKIE "torirs_gate"
/* "<name>.<expires>.<64 hex>" + NUL, with room to spare. */
#define TORIRSSERVER_GATE_TICKET_MAX 128
#define TORIRSSERVER_GATE_TICKET_SECONDS (30LL * 24 * 60 * 60)

enum ToriRSServerAuthResult
{
    /** The password matched the name's record. */
    TORIRSSERVER_AUTH_OK,
    /** The name had no record; this password is now it. */
    TORIRSSERVER_AUTH_CLAIMED,
    /** The password did not match, or the record could not be read. */
    TORIRSSERVER_AUTH_REFUSED,
    /** The name sanitises to nothing, or the password is empty. */
    TORIRSSERVER_AUTH_UNUSABLE,
    /** The name had no record and this password breaks the rules, so it was
     *  not claimed; nothing was written. */
    TORIRSSERVER_AUTH_WEAK,
    /** The password matched, but the account is linked to a web login
     *  (`managed_by`) and signs in only through that login's session. */
    TORIRSSERVER_AUTH_MANAGED,
    /** A web login's check could not be asked: the account server
     *  (account_client.h) did not answer. Nothing was decided. */
    TORIRSSERVER_AUTH_UNAVAILABLE,
};

/**
 * Check `password` for `display_name`, claiming the name when it has no
 * record yet. A record that exists but cannot be read or parsed refuses: a
 * damaged file must not reopen the name to whoever logs in next.
 */
enum ToriRSServerAuthResult
ToriRSServer_AuthLogin(
    const char* display_name,
    const char* password);

/** NULL when `password` may be set for `display_name`; otherwise the rule it
 *  breaks, as a sentence for the person who typed it. */
const char*
ToriRSServer_AuthPasswordProblem(
    const char* display_name,
    const char* password);

/** NULL when `display_name` may be taken by a new web login: 1 to 12 characters,
 *  letters, digits, space, '-' and '_', at least one letter, no edge spaces. */
const char*
ToriRSServer_AuthNameProblem(const char* display_name);

/** 1 when the name's record says `admin = 1`. 0 for no record, an unreadable
 *  one, or an unusable name. */
int
ToriRSServer_AuthIsAdmin(const char* display_name);

/** Set or clear `admin` on an existing record. 0 when there is no usable
 *  record or it could not be rewritten. */
int
ToriRSServer_AuthSetAdmin(
    const char* display_name,
    int admin);

/**
 * The web login a player account is linked to, as it is keyed, into
 * `out_user`. 1 when it is linked; 0 when it is not or has no record; -1 when
 * the record cannot be read, which every caller must treat as a refusal.
 */
int
ToriRSServer_AuthManagedBy(
    const char* display_name,
    char* out_user,
    size_t cap);

enum ToriRSServerLinkResult
{
    /** Linked to `user` now (or already was). */
    TORIRSSERVER_LINK_OK,
    /** Wrong password, an unusable name, or an unreadable record. */
    TORIRSSERVER_LINK_REFUSED,
    /** No player account by that name (no record): only an existing account
     *  can be linked. Nothing was written. */
    TORIRSSERVER_LINK_NO_ACCOUNT,
    /** The account is linked to a different web login: one owner only. */
    TORIRSSERVER_LINK_OTHER_USER,
    TORIRSSERVER_LINK_WRITE_FAILED,
};

/**
 * Link the player account `display_name` to the web login `user`, the
 * account's own `password` proving it is the linker's, and mint the link's
 * token into `out_token` (OK only) for the caller to keep: the record holds
 * only its hash. The account must exist: a name with no record is NO_ACCOUNT
 * and nothing is written (accounts are made by logging in to the game). An
 * account linked to `user` already is re-linked with a new token. Only the record changes here; revoking the account's
 * refresh tokens is the caller's (ToriRSServer_OAuthRevokeAccount).
 */
enum ToriRSServerLinkResult
ToriRSServer_AuthLink(
    const char* display_name,
    const char* password,
    const char* user,
    char out_token[TORIRSSERVER_LINK_TOKEN_HEX + 1]);

/** 1 when the account `display_name` is linked to the web login `user` (as
 *  keyed) and `link_token` is its live link's token. */
int
ToriRSServer_AuthLinkVerify(
    const char* display_name,
    const char* user,
    const char* link_token);

/** Clear `managed_by`, so the account's own password signs it in again. 0
 *  when there is no usable record or it could not be rewritten. */
int
ToriRSServer_AuthUnlink(const char* display_name);

/* Web logins and invites are the account server's
 * (accountserver/account_server.h); a process asks it through
 * accountserver/account_client.h (AccountClient_GateLogin, AccountClient_Invite*).
 * These results are what it answers. */

enum ToriRSServerInviteResult
{
    TORIRSSERVER_INVITE_OK,
    /** No such invite: never made, already redeemed, or not a code at all. */
    TORIRSSERVER_INVITE_UNKNOWN,
    /** The invite is for another name. */
    TORIRSSERVER_INVITE_WRONG_NAME,
    /** The name breaks AuthNameProblem; `*out_problem` says how. */
    TORIRSSERVER_INVITE_BAD_NAME,
    /** The name has a web login already. */
    TORIRSSERVER_INVITE_NAME_TAKEN,
    /** The password breaks AuthPasswordProblem; `*out_problem` says how. */
    TORIRSSERVER_INVITE_WEAK,
    TORIRSSERVER_INVITE_WRITE_FAILED,
    /** The account server did not answer; nothing was spent. */
    TORIRSSERVER_INVITE_UNAVAILABLE,
};

enum ToriRSServerCreateResult
{
    TORIRSSERVER_CREATE_OK,
    /** The name breaks AuthNameProblem; `*out_problem` says how. */
    TORIRSSERVER_CREATE_BAD_NAME,
    /** The name has a record already (or a damaged one, which is kept). */
    TORIRSSERVER_CREATE_EXISTS,
    /** The password breaks AuthPasswordProblem; `*out_problem` says how. */
    TORIRSSERVER_CREATE_WEAK,
    TORIRSSERVER_CREATE_WRITE_FAILED,
    /** The account server did not answer (a web login); nothing was made. */
    TORIRSSERVER_CREATE_UNAVAILABLE,
};

/**
 * An operator making a PLAYER account outright (`torirsserver --account
 * create`, torirsctl), `admin` 1 for staff: the same record a first login
 * claims, under the same name and password rules. Never over an existing one.
 */
enum ToriRSServerCreateResult
ToriRSServer_AuthCreate(
    const char* display_name,
    const char* password,
    int admin,
    const char** out_problem);

/** Replace an existing player account's password, keeping its admin flag and
 *  any link. 0 with `*out_problem` set when it breaks the rules, there is no
 *  account, or it could not be written. */
int
ToriRSServer_AuthSetPassword(
    const char* display_name,
    const char* password,
    const char** out_problem);

/** `len` bytes of key from the hex file at `path`, writing a fresh random key
 *  there first if there is none. Aborts when it cannot: a key that silently
 *  failed to load signs nothing anyone can check. `len` is at most 64. */
void
ToriRSServer_KeyFileLoad(
    const char* path,
    uint8_t* out,
    size_t len);

/** The operating system's random bytes (aborts when it has none). */
void
ToriRSServer_AuthRandom(
    uint8_t* out,
    size_t len);

/** `len` bytes as lowercase hex into `out` (2 * len + 1 bytes). */
void
ToriRSServer_AuthHex(
    const uint8_t* bytes,
    size_t len,
    char* out);

/** HMAC-SHA256 (RFC 2104) of `message` under `key`. */
void
ToriRSServer_AuthHmacSha256(
    const uint8_t* key,
    size_t key_len,
    const uint8_t* message,
    size_t message_len,
    uint8_t out[32]);

/** Load `<saves>/gate/gate.key`, writing a fresh random one if there is
 *  none. Aborts when it cannot: a gate with no key admits nobody, silently. */
void
ToriRSServer_GateKeyLoad(void);

/** The ticket for the web login `name` (sanitised, as it is keyed), good
 *  until the unix time `expires`. Needs ToriRSServer_GateKeyLoad. */
void
ToriRSServer_GateTicketMint(
    const char* name,
    long long expires,
    char out[TORIRSSERVER_GATE_TICKET_MAX]);

/** 1 when the `len` bytes at `ticket` are a ticket this key minted that has
 *  not expired by `now`; `out_name` gets its (sanitised) web login. */
int
ToriRSServer_GateTicketCheck(
    const char* ticket,
    size_t len,
    long long now,
    char* out_name,
    size_t cap);

/** ToriRSServer_GateTicketCheck on the TORIRSSERVER_GATE_COOKIE pair of a
 *  Cookie header's value (`len` bytes, not NUL-terminated). */
int
ToriRSServer_GateCookieTicket(
    const char* cookie,
    size_t len,
    long long now,
    char* out_name,
    size_t cap);

/** PBKDF2-HMAC-SHA256 (RFC 8018), exposed for its test vectors. */
void
ToriRSServer_AuthPbkdf2(
    const char* password,
    const uint8_t* salt,
    size_t salt_len,
    int iterations,
    uint8_t* out,
    size_t out_len);

/** The record line for `password` under `salt`. `out` holds
 *  TORIRSSERVER_AUTH_RECORD_MAX bytes. */
void
ToriRSServer_AuthRecord(
    const char* password,
    const uint8_t salt[TORIRSSERVER_AUTH_SALT_BYTES],
    int iterations,
    char* out);

/** 1 when `password` matches `record`; 0 when it does not or the record is
 *  malformed. The key comparison takes the same time wherever it differs. */
int
ToriRSServer_AuthVerify(
    const char* password,
    const char* record);

#endif
