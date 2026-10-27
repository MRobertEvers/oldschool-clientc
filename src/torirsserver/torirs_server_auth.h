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
 *     admin = 1        optional; staff: the jmod crown, and ::commands where
 *                      passwords are checked (ToriRSServer_PlayerMayCheat)
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
 * visitor signs in with a web login. Web logins live in `<saves>/gate/`, not
 * beside the players, and come only from invites an operator made
 * (`torirsserver --account invite [name]`), redeemed on the gate's /invite page
 * under the same password rules. Past the page the game asks for a player
 * account exactly as above; the two are unrelated, and the gate's login makes
 * nobody staff. The gate's session is a ticket -- the web login's name and an
 * expiry under an HMAC keyed by `<saves>/gate/gate.key` -- which io_server
 * issues as a cookie and torirsserver requires on the game WebSocket, so the
 * socket is as closed as the page and the two processes share a key file and
 * nothing else.
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

/**
 * Check a web login for the sign-in gate (`<saves>/gate/<name>.ini`). Never
 * claims: a name with no login is REFUSED, because every one came from an
 * invite. Not a player account; see the top of this file.
 */
enum ToriRSServerAuthResult
ToriRSServer_GateLogin(
    const char* name,
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
 * Write an invite: `<saves>/gate/invites/<code>.ini`, redeemed at the gate's
 * `/invite/<code>` for a web login. `name` NULL lets the visitor choose the
 * name; otherwise only that name can redeem it (it must pass AuthNameProblem).
 * Returns 1 when written.
 */
int
ToriRSServer_InviteCreate(
    const char* name,
    char out_code[TORIRSSERVER_INVITE_CODE_BYTES * 2 + 1]);

/** 1 when `code` is an unredeemed invite; `out_name` gets the name it is for,
 *  "" when the player chooses. */
int
ToriRSServer_InviteLookup(
    const char* code,
    char* out_name,
    size_t cap);

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
};

/** Spend `code` on a new web login `name` / `password`, deleting the
 *  invite. `*out_problem` is a sentence for BAD_NAME and WEAK, else NULL. */
enum ToriRSServerInviteResult
ToriRSServer_InviteRedeem(
    const char* code,
    const char* name,
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
