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
 * ## Route
 *
 *   GET <io>/cache/dat1/<table>/<archive>?flags=<n>
 *
 *   200  the container's bytes
 *   404  the server does not have it -- an ANSWER: a built world has holes at
 *        its edges, and so does a versionlist's map index
 *   503  io_server has no on-demand source configured, which is an outage and
 *        is reported as one
 */

(function () {
  'use strict';

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
    }

    /**
     * One archive's bytes, or null when the server does not have it.
     *
     * Null is an ANSWER here, not a failure. An outage throws, carrying
     * `torirsUnreachable` -- the flag the executor turns into
     * PlatformWeb_IO_ServerReachable going false -- because "there is no such
     * map square" and "nothing answered" must not reach the client as the same
     * fact.
     */
    async file(table, archive, flags) {
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
