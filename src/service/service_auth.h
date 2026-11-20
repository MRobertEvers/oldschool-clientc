#ifndef SRC_SERVICE_SERVICE_AUTH_H
#define SRC_SERVICE_SERVICE_AUTH_H

/*
 * The signature every request between the servers carries.
 *
 * Five servers -- the game, the account server, the hiscores server, the
 * website and the IO server -- each run on their own or inside another's
 * process (service_link.h). A request that crosses a socket from one to
 * another proves it came from one of them: an HMAC-SHA256, under a key they
 * all hold, of the method, the path, the time and the SHA-256 of the body,
 * sent as
 *
 *   X-Torirs-Service: <unix seconds>.<64 hex>
 *
 * and checked against the receiver's clock within SERVICE_AUTH_WINDOW_SECONDS.
 * The key never crosses the wire, and a request cannot be altered or moved
 * to another path; one can be replayed inside the window, which every route
 * that changes anything tolerates (a lease's return is token-checked, a world's
 * record is sequence-numbered).
 *
 * The key is TORIRS_SERVICE_KEY (64 hex digits) when set, else
 * `<saves>/service.key`, made on first use -- with an exclusive create, so of
 * several servers starting at once on a fresh box one writes it and the rest
 * read it. Servers on other machines are given the same key (copy the file,
 * or set the variable).
 *
 * A request a handler in the same process makes directly is `trusted`
 * (HttpRequest) and needs no signature: it never left the process.
 */

#include "service/http_server.h"

#include <stddef.h>

#define SERVICE_AUTH_HEADER "X-Torirs-Service"
#define SERVICE_AUTH_KEY_BYTES 32
#define SERVICE_AUTH_WINDOW_SECONDS 120
/** "X-Torirs-Service: " + unix + '.' + 64 hex + "\r\n" + NUL, with room. */
#define SERVICE_AUTH_LINE_MAX 128

/** Load the key, once per process, before the first signed request or check.
 *  Aborts when it cannot: a server that cannot sign or check refuses every
 *  other server, silently. Idempotent. */
void
ServiceAuth_Load(void);

/** The header line (with its "\r\n") that signs `method` `path` with `body`
 *  (`body_len` bytes; NULL with 0 for none), into `out`. */
void
ServiceAuth_HeaderLine(
    char const* method,
    char const* path,
    void const* body,
    int body_len,
    char out[SERVICE_AUTH_LINE_MAX]);

/** 1 when `req` came from this process (`trusted`) or carries a signature
 *  this key made for its method, path and body within the window. */
int
ServiceAuth_Check(struct HttpRequest const* req);

#endif
