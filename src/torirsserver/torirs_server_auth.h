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
 *   dev_torirsserver             does not (TORIRSSERVER_DEV=0 turns it on)
 *   the in-client embedded world does not; it serves one local player
 *
 * The record lives beside the character, keyed by the same sanitised name
 * (ToriRSServer_AccountPath):
 *
 *   <saves>/accounts/<name>.ini
 *     [account]
 *     password = pbkdf2-sha256$<iterations>$<salt hex>$<key hex>
 *
 * A separate file rather than a key in the character save, because the save
 * is rewritten from the player struct on every logout and is hand-edited, and
 * a credential should survive both without riding through either.
 *
 * **The first login claims the name.** A name with no record takes the
 * password it logged in with, which is also how an existing character (saved
 * before passwords) gets one, and how an operator resets a forgotten one:
 * delete the record and the next login sets it.
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
