#ifndef PLATFORM_X_HTTP_H
#define PLATFORM_X_HTTP_H

/*
 * A one-shot HTTP GET, for the DESKTOP platform's own use.
 *
 * PlatformX_, not Platform_, and the prefix is the whole point: this dials a
 * socket and blocks on it, which is a thing only a platform with sockets and a
 * thread to spare can do. The browser has neither and compiles none of this --
 * its equivalent is `fetch` inside the JavaScript executor, awaited rather than
 * waited on. Naming it Platform_ would have implied a seam both platforms
 * implement, and there is no such seam here: the two do this differently enough
 * that sharing an interface would be a lie.
 *
 * (The rule, so the next file lands on the right side: PlatformX_ is the
 * desktop -- filesystem, sockets, synchronous. PlatformWeb_ is the browser --
 * IndexedDB, fetch, asynchronous. Platform_ is reserved for the few seams both
 * genuinely implement, and platform_io.h is where those are mapped.)
 *
 * Two callers want the same small thing and had one copy each until this
 * existed: the dat1 on-demand client reads a LostCity server's `/crc` and
 * versionlist routes, and the desktop IO executor asks io_server for a config
 * file or a script the local disk did not have.
 *
 * HTTP/1.0 on purpose -- the server answers with `Connection: close` and an
 * unframed body, so "the body" is "everything until the socket closes", which
 * is what keeps this small. Content-Length and chunked are still honoured when
 * present rather than assumed absent: a response that framed itself and was
 * read to EOF anyway would silently gain the framing bytes.
 *
 * BLOCKING, and only usable where that is acceptable. On the desktop it is:
 * this is the same thread that already reads the disk synchronously, and the
 * caller is a task that would otherwise be parked.
 */

/**
 * Returns a malloc'd body and writes its length to `out_size`, or NULL when the
 * request could not be made, the server answered anything but 200, or the
 * response could not be framed.
 *
 * A NULL return is deliberately not distinguished from a 404 here. Callers that
 * need the difference between "no such file" and "no server" ask a question
 * this interface cannot answer alone; see PlatformX_HttpGetStatus.
 */
char*
PlatformX_HttpGet(
    const char* host,
    int port,
    const char* route,
    int* out_size);

/**
 * As PlatformX_HttpGet, but reports the status line as well.
 *
 * `*out_status` is the HTTP status when one was read, 0 when nothing answered
 * at all. That distinction is the one the plugin lane turns on: a server
 * replying 404 proves it is THERE, while a request nobody answered is an
 * outage, and only the second should switch parts of the client off.
 */
char*
PlatformX_HttpGetStatus(
    const char* host,
    int port,
    const char* route,
    int* out_size,
    int* out_status);

#endif
