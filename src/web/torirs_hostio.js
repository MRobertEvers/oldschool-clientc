/*
 * Where the browser's IO executor gets its bytes.
 *
 * platform/platform_web_io.js is the executor: it reads the queue, decides what
 * each item means, and calls the cache format's C API to decode. It never
 * touches the network or the database itself. This file is the other half --
 * the page's own knowledge of where things live, expressed as six async
 * functions.
 *
 * The split is deliberate. The executor is queue mechanics and belongs with the
 * platform; this is deployment -- which server, which database, which URL --
 * and belongs with the page, because a different page (a test harness, an
 * offline bundle, an embedded viewer) legitimately answers these differently
 * with the executor unchanged.
 *
 * ## Everything here is async, and nothing here blocks
 *
 * Every function returns a promise and every network call is `fetch`. There is
 * no synchronous XHR anywhere in this file, and that is the point: a
 * synchronous request freezes the whole tab for its duration -- measured on
 * localhost at 4.4ms average and 17ms worst against a 3.55ms round trip, and
 * far worse against a real server -- and it freezes the main thread, which is
 * the one drawing frames. The queue can carry an outstanding item, so there is
 * nothing to gain by blocking and a visible stutter to lose.
 *
 * ## Local first, server second
 *
 * Every read tries the record database before the network. That is not a cache
 * optimisation bolted on top -- it is what makes the browser lane work at all:
 * the database is where JS5 puts what it downloads, and where a previous
 * session's files survive a reload. The server is the fallback for what the
 * database has never held, which is exactly the case that used to have no route
 * at all (a plugin's assets, named by the plugin at runtime, which no amount of
 * page-side pre-staging could have anticipated).
 *
 * ## Two failures that must not be confused
 *
 * A server that answers "no such file" is a server that is THERE. Only a
 * request nobody answers is an outage. The two are told apart by throwing an
 * error carrying `torirsUnreachable` for the second, which the executor turns
 * into PlatformX_IO_ServerReachable going false -- and which the client uses to
 * switch off the parts of its UI that cannot work without a server behind them.
 * Returning null, by contrast, simply means "not there", and is an ordinary
 * answer.
 */

(function () {
  'use strict';

  /**
   * Build the provider.
   *
   * `store` is the record database (torirs_host.js's `store`), `bootUrl` the
   * route that serves source files. Passed in rather than reached for so this
   * file has no opinion about how the page is put together.
   */
  window.ToriRS_CreateHostIO = function (store, bootUrl) {
    /* Paths the server has already denied. A plugin that asks every frame for
     * an asset that does not exist then costs one round trip for the session
     * rather than one per frame. Not populated on a transport failure: the file
     * may well be there, and remembering "absent" would outlive the outage. */
    const denied = new Set();

    const unreachable = (message) => {
      const err = new Error(message);
      err.torirsUnreachable = true;
      return err;
    };

    /*
     * One conditional GET, over the two routes a file may be served by.
     *
     * `/boot/<path>` is the IO server's source route and is tried first; the
     * bare path is the fallback, because a page may be served by anything that
     * hands out files and need not have an IO server at all.
     *
     * Returns bytes, or null when the file is genuinely absent. Throws
     * `torirsUnreachable` when neither route answered -- see the file comment.
     */
    async function fetchFile(path) {
      let sawResponse = false;

      for (const url of [`${bootUrl}/${path}`, `/${path}`]) {
        let response;
        try {
          response = await fetch(url, { cache: 'no-store' });
        } catch (err) {
          /* A network-level failure. Try the other route before concluding
           * anything: the IO server may be gone while the static host that
           * serves the page is fine. */
          continue;
        }
        sawResponse = true;
        if (response.ok) {
          return new Uint8Array(await response.arrayBuffer());
        }
        /* A 404 from /boot/ is not the end -- the bare path may still have it.
         * Any other status is the server refusing, and refusing twice is still
         * the server being there. */
      }

      if (!sawResponse) { throw unreachable(`nothing answered for ${path}`); }
      return null;
    }

    return {
      /*
       * A server-backed file: a plugin script, its manifest, a shipped asset, a
       * config file. Database first, then the server.
       */
      async readFile(path) {
        const held = store.fileGet(path);
        if (held) { return held; }
        if (denied.has(path)) { return null; }

        const bytes = await fetchFile(path);
        if (bytes) { store.filePut(path, bytes, null); }
        else { denied.add(path); }
        return bytes;
      },

      /*
       * A file the PLAYER owns: saved settings, a plugin's saved assets.
       *
       * The database and nothing else, ever. These are this browser's own --
       * asking a server for one would put a single shared copy in front of
       * every client that machine answers and read back settings its user never
       * chose, and io_server refuses them by kind for the same reason. A miss
       * here means "not saved yet", which is an answer and not a reason to go
       * looking elsewhere.
       */
      async readClientFile(path) {
        return store.fileGet(path);
      },

      async writeClientFile(path, bytes) {
        store.filePut(path, bytes, null);
      },

      /*
       * One cache group's raw container.
       *
       * The database is the real source here: it is what JS5 fills, so on a
       * warm cache every read is answered without a request. The server
       * fallback covers a page served alongside an io_server that holds the
       * cache, and is simply absent for a page that has none.
       */
      async readArchive(table, archive) {
        const held = store.get(table, archive);
        if (held) { return held; }

        const path = `cache/${table}/${archive}`;
        if (denied.has(path)) { return null; }
        const bytes = await fetchFile(path);
        if (bytes) { store.put(table, archive, bytes); }
        else { denied.add(path); }
        return bytes;
      },

      /* Archive 255 of a table is its reference table -- the same container
       * shape as any group, addressed the way the cache addresses it. */
      async readReferenceTable(table) {
        return await this.readArchive(255, table);
      },

      /*
       * The XTEA key for a map archive, or null.
       *
       * Null for everything today, and deliberately a FUNCTION rather than an
       * absent feature: whether a table is encrypted is a property of the cache
       * profile, the keys arrive with the deployment, and the executor should
       * not have to change shape when a page starts supplying them. A page that
       * has keys overrides this one method.
       */
      async xteaKey(table, archive) {
        return null;
      },
    };
  };
})();
