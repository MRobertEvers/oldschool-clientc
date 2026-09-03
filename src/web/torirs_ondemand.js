/*
 * The dat1 cache producer: one raw container at a time, through io_server.
 *
 * ## Why this is a fetch and not a socket
 *
 * A dat1 cache lives on the LostCity server, and a page cannot read it. The
 * 2004 on-demand protocol runs on the game port over raw TCP, which a browser
 * has no way to speak; the eight jag archives (title, config, interface,
 * media, versionlist, textures, wordenc, sounds) are HTTP resources served
 * with no Access-Control-Allow-Origin, so a cross-origin fetch of one comes
 * back opaque. Neither is a gap in the server -- both are what a 2004 server
 * is -- and neither can be worked around from inside the tab.
 *
 * So io_server holds the on-demand client and this asks it. That is also where
 * the protocol's real complexity stays: which table is HTTP and which is the
 * socket, the checksum in each jag route, the file-id offset the socket
 * numbers its archives by, and the versionlist lookup that turns a map SQUARE
 * into an archive id. Every one of those is already written, in C, in
 * platform_x_io_ondemand.c, and running the native client's copy of it is the
 * whole reason there is a proxy rather than a second implementation here.
 *
 * ## What crosses
 *
 * The container exactly as the server serves it, undecoded: the jag file for
 * the config table, the still-compressed file for the rest. The browser's
 * IndexedDB holds raw containers for dat2 already, and the decode happens once
 * on the way out of the store (platform_web_io.js picks the dat1 decoder off
 * the item's flags). One shape for both containers is what keeps that single
 * decode step honest.
 *
 * ## Routes
 *
 *   POST <io>/cache/dat1/batch          the usual one; see `file` below
 *   GET  <io>/cache/dat1/<table>/<archive>?flags=<n>
 *
 *   200  the container's bytes (per file inside a batch: size 0 = absent)
 *   404  the server does not have it -- an ANSWER: a built world has holes at
 *        its edges, and so does a versionlist's map index
 *   503  io_server has no on-demand source configured, which is an outage and
 *        is reported as one
 */

(function () {
  'use strict';

  /* How long the first call of a batch waits for company. The reads of one
   * scheduler pass reach here a few event-loop turns apart (each behind its
   * own database miss), so a couple of milliseconds gathers a whole pass;
   * a lone read costs that much latency and nothing else. */
  const BATCH_WINDOW_MS = 2;
  /* A batch is one HTTP body; keep a rebuild's worth from becoming one
   * enormous one. */
  const BATCH_MAX = 512;

  class OnDemandClient {
    /**
     * `base` is the io_server origin this page is already talking to, as the
     * /io endpoint with its last segment removed -- the same derivation
     * torirs_host.js uses for /boot and /stats, so all four routes are one
     * server by construction rather than by four settings agreeing.
     */
    constructor(base) {
      this.base = base;
      this.bytesReceived = 0;
      this.filesReceived = 0;
      this.failed = false;
      /* Calls gathered for the next batch, and the timer that sends it. */
      this.batch = [];
      this.batchTimer = null;
      /* Set once the server has shown it has no batch route. */
      this.noBatch = false;
    }

    /**
     * One archive's bytes, or null when the server does not have it.
     *
     * Null is an ANSWER here, not a failure. An outage throws, carrying
     * `torirsUnreachable` -- the flag the executor turns into
     * PlatformWeb_IO_ServerReachable going false -- because "there is no such
     * map square" and "nothing answered" must not reach the client as the same
     * fact.
     *
     * ## Batched, because the client asks in batches
     *
     * The executor hands every read of a scheduler pass over at once -- a
     * region rebuild is hundreds of files in one Process -- and each arrives
     * here as its own call a few event-loop turns apart, after its database
     * miss. A GET per file would put them through the browser's six-per-host
     * connection limit and through io_server's one-request-at-a-time loop,
     * which is one blocking read of the LostCity socket per file, in a line.
     *
     * So calls made within a short window are coalesced into one
     * `POST /cache/dat1/batch`, which io_server answers as one pipeline on
     * that socket. Nothing is requested that was not asked for: the batch is
     * exactly the set of files the client's tasks queued, carried together.
     * An io_server without the route (405/404 on the POST) gets the per-file
     * GETs instead, so an older server still serves.
     */
    async file(table, archive, flags) {
      if (this.noBatch) { return await this.fileSingle(table, archive, flags); }
      return await new Promise((resolve, reject) => {
        this.batch.push({ table, archive, flags: flags | 0, resolve, reject });
        if (this.batchTimer === null) {
          this.batchTimer = setTimeout(() => this.flush(), BATCH_WINDOW_MS);
        }
        if (this.batch.length >= BATCH_MAX) { this.flush(); }
      });
    }

    /** Send what has gathered. Each waiter is settled from its own record. */
    async flush() {
      const batch = this.batch;
      this.batch = [];
      if (this.batchTimer !== null) { clearTimeout(this.batchTimer); this.batchTimer = null; }
      if (batch.length === 0) { return; }

      let results;
      try {
        results = await this.fileBatch(batch);
      } catch (err) {
        if (err && err.torirsNoBatchRoute) {
          /* The server predates the route: answer each the old way, once,
           * and stop trying the batch for the rest of the session. */
          this.noBatch = true;
          for (const entry of batch) {
            this.fileSingle(entry.table, entry.archive, entry.flags)
              .then(entry.resolve, entry.reject);
          }
          return;
        }
        for (const entry of batch) { entry.reject(err); }
        return;
      }
      for (let i = 0; i < batch.length; i++) { batch[i].resolve(results[i]); }
    }

    /*
     * The batch wire, both directions little-endian:
     *   request   u32 count, then count x (u32 table, u32 archive, u32 flags)
     *   response  u32 count, then count x (u32 size, size bytes); size 0 is
     *             "the server does not have it", the same answer a 404 is on
     *             the single route
     */
    async fileBatch(batch) {
      const url = `${this.base}/cache/dat1/batch`;
      const body = new Uint8Array(4 + batch.length * 12);
      const view = new DataView(body.buffer);
      view.setUint32(0, batch.length, true);
      for (let i = 0; i < batch.length; i++) {
        view.setUint32(4 + i * 12, batch[i].table, true);
        view.setUint32(8 + i * 12, batch[i].archive, true);
        view.setUint32(12 + i * 12, batch[i].flags, true);
      }

      let response;
      try {
        response = await fetch(url, { method: 'POST', body, cache: 'no-store' });
      } catch (err) {
        this.failed = true;
        const outage = new Error(`ondemand: nothing answered for ${url}`);
        outage.torirsUnreachable = true;
        throw outage;
      }
      if (response.status === 404 || response.status === 405) {
        const missing = new Error('ondemand: io_server has no batch route');
        missing.torirsNoBatchRoute = true;
        throw missing;
      }
      if (response.status === 503) {
        this.failed = true;
        const outage = new Error(
          'ondemand: io_server has no LostCity server to read the cache from ' +
          '(its manifest needs [cache:boot] source=ondemand)');
        outage.torirsUnreachable = true;
        throw outage;
      }
      if (!response.ok) {
        this.failed = false;
        return batch.map(() => null);
      }

      const bytes = new Uint8Array(await response.arrayBuffer());
      const reply = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
      const count = reply.getUint32(0, true);
      if (count !== batch.length) {
        throw new Error(`ondemand: batch answered ${count} of ${batch.length} files`);
      }
      const results = [];
      let at = 4;
      for (let i = 0; i < count; i++) {
        const size = reply.getUint32(at, true);
        at += 4;
        if (size === 0) { results.push(null); continue; }
        results.push(bytes.slice(at, at + size));
        at += size;
        this.filesReceived += 1;
        this.bytesReceived += size;
      }
      this.failed = false;
      return results;
    }

    /** The single-file route, for a server without the batch one. */
    async fileSingle(table, archive, flags) {
      const url = `${this.base}/cache/dat1/${table}/${archive}?flags=${flags | 0}`;
      let response;

      try {
        response = await fetch(url, { cache: 'no-store' });
      } catch (err) {
        this.failed = true;
        const outage = new Error(`ondemand: nothing answered for ${url}`);
        outage.torirsUnreachable = true;
        throw outage;
      }

      if (response.status === 404) { this.failed = false; return null; }
      if (response.status === 503) {
        this.failed = true;
        const outage = new Error(
          'ondemand: io_server has no LostCity server to read the cache from ' +
          '(its manifest needs [cache:boot] source=ondemand)');
        outage.torirsUnreachable = true;
        throw outage;
      }
      if (!response.ok) {
        /* A refusal is still an answer from a server that is there, so this is
         * not an outage -- it is one read that did not work. */
        this.failed = false;
        return null;
      }

      const bytes = new Uint8Array(await response.arrayBuffer());
      this.failed = false;
      this.filesReceived += 1;
      this.bytesReceived += bytes.length;
      return bytes;
    }

    stats() {
      return {
        files: this.filesReceived,
        bytes: this.bytesReceived,
        failed: this.failed,
      };
    }
  }

  window.ToriRS_CreateOnDemand = function (base) {
    return new OnDemandClient(base);
  };
})();
