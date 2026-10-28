#ifndef PS_FETCH_H
#define PS_FETCH_H

/*
 * Sheets from io_server, off the frame thread.
 *
 * The client asks by key every frame it wants a sheet; the first ask queues a
 * GET /pokesprite/<key>, a worker thread fetches and decodes it, and a later
 * ask finds it READY. A 404 is remembered as MISSING for the session -- the
 * server has already logged the miss for the generator, and asking again every
 * frame would only hammer it. Anything that is not an answer (no server, a
 * 5xx, an undecodable body) is FAILED, kept apart from MISSING so a dead
 * server does not read as "the store has nothing".
 */

#include "ps_sheet.h"

enum PS_FetchState
{
    PS_FETCH_PENDING = 0,
    PS_FETCH_READY,
    PS_FETCH_MISSING,
    PS_FETCH_FAILED,
};

struct PS_Fetch;

/** `host`:`port` is io_server. Starts the worker. */
struct PS_Fetch*
PS_FetchNew(
    const char* host,
    int port);

void
PS_FetchFree(struct PS_Fetch* fetch);

/**
 * The sheet for `key` when READY (owned by the fetcher, valid until it is
 * freed), else NULL with `*out_state` saying why. Queues the key on first ask.
 */
const struct PS_Sheet*
PS_FetchGet(
    struct PS_Fetch* fetch,
    const char* key,
    enum PS_FetchState* out_state);

/** Counts for an on-screen readout: ready, pending, missing, failed. */
void
PS_FetchCounts(
    struct PS_Fetch* fetch,
    int out[4]);

#endif
