/*
 * Where the browser's IO executor gets its bytes.
 *
 * platform/platform_web_io.js is the executor: it reads the queue, decides what
 * each item means, and calls the cache format's C API to decode. It never
 * touches the network or the database itself. This file is the other half --
 * the page's own knowledge of where things live, expressed as a few async
 * functions.
 *
 * The split is deliberate. The executor is queue mechanics and belongs with the
 * platform; this is deployment -- which database, which server, which URL --
 * and belongs with the page, because a different page (a test harness, an
 * offline bundle, an embedded viewer) legitimately answers these differently
 * with the executor unchanged.
 *
 * ## Everything is async, and nothing blocks
 *
 * Every function returns a promise, every network call is `fetch`, and every
 * database read is an ordinary IndexedDB request that is awaited. There is no
 * synchronous XHR here and no synchronous view of the database.
 *
 * ## Read the database; do not mirror it
 *
 * An earlier design hydrated every archive record into a JavaScript Map before
 * main() ran, and answered reads from that. It existed for exactly one reason:
 * the C side reached the store through a synchronous facade and could not await
 * an IndexedDB request. The executor is asynchronous, so that reason is gone --
 * and with it the cost, which was never small: a whole-cache cursor pass before
 * the first frame, and the entire resident cache held twice (once in the
 * database, once on the JS heap) for the life of the tab.
 *
 * So a read is a read. IndexedDB is itself an indexed, on-disk key/value store
 * with its own page cache; putting a hand-rolled one in front of it is a
 * pessimisation until something profiled says otherwise, and nothing has. If a
 * cache ever IS wanted here, it belongs behind this interface and measured --
 * not assumed.
 *
 * ## Local first, server second
 *
 * Every read tries the database before the network. That is not an
 * optimisation on top -- it is what makes the lane work at all: the database is
 * where downloads are kept and where a previous session's files survive a
 * reload. The server is the fallback for what the database has never held,
 * which is exactly the case that used to have no route (a plugin's assets,
 * named by the plugin at runtime, that no page-side pre-staging could
 * anticipate).
 *
 * ## Two failures that must not be confused
 *
 * A server that answers "no such file" is a server that is THERE. Only a
 * request nobody answers is an outage. The two are told apart by throwing an
 * error carrying `torirsUnreachable` for the second, which the executor turns
 * into PlatformX_IO_ServerReachable going false -- and which the client uses to
 * switch off the parts of its UI that cannot work without a server behind them.
 * Returning null simply means "not there", and is an ordinary answer.
 */

(function () {
  'use strict';

  const DB_NAME = 'torirs-cache';
  const DB_VERSION = 1;

  /** One IndexedDB request, as a promise. */
  function reqPromise(request) {
    return new Promise((resolve, reject) => {
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => reject(request.error);
    });
  }

  /**
   * Open the database, creating the stores if this browser has none.
   *
   * The schema is shared with torirs_host.js, which writes the same three
   * stores; `onupgradeneeded` here is what lets this file work in a page that
   * has not opened it first, and is a no-op when it has.
   */
  function openDb() {
    return new Promise((resolve, reject) => {
      let request;
      try {
        request = indexedDB.open(DB_NAME, DB_VERSION);
      } catch (err) {
        /* Private browsing with storage disabled. Not fatal: every read then
         * falls through to the server, which is a slow client rather than a
         * broken one. */
        resolve(null);
        return;
      }
      request.onupgradeneeded = event => {
        const db = event.target.result;
        if (!db.objectStoreNames.contains('groups')) {
          db.createObjectStore('groups', { keyPath: 'k' }).createIndex(
            'by_cache', 'c', { unique: false });
        }
        if (!db.objectStoreNames.contains('files')) {
          db.createObjectStore('files', { keyPath: 'k' });
        }
        if (!db.objectStoreNames.contains('boot')) {
          db.createObjectStore('boot', { keyPath: 'k' });
        }
      };
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => resolve(null);
    });
  }

  /**
   * Build the provider.
   *
   * `cacheKeyOf` is a FUNCTION returning the generation these archives belong
   * to, so two caches can share one database without reading each other's
   * groups. A function rather than a value because the provider is built while
   * the page assembles Module, and which cache is open is not settled until the
   * boot barrier has run -- reading it per request is always current and costs
   * nothing.
   *
   * `bootUrl` is the route that serves source files.
   */
  window.ToriRS_CreateHostIO = function (cacheKeyOf, bootUrl) {
    const cacheKey = () => (typeof cacheKeyOf === 'function' ? cacheKeyOf() : cacheKeyOf);
    let dbPromise = null;
    const db = () => (dbPromise || (dbPromise = openDb()));

    /* Paths the server has already denied. Not a mirror of the database -- a
     * record of questions already answered, so a plugin asking every frame for
     * an asset that does not exist costs one round trip per session rather than
     * one per frame. Never populated on a transport failure: the file may well
     * be there, and remembering "absent" would outlive the outage. */
    const denied = new Set();

    const unreachable = (message) => {
      const err = new Error(message);
      err.torirsUnreachable = true;
      return err;
    };

    async function dbGet(storeName, key) {
      const handle = await db();
      if (!handle) { return null; }
      try {
        const tx = handle.transaction([storeName], 'readonly');
        const row = await reqPromise(tx.objectStore(storeName).get(key));
        return row ? row : null;
      } catch (err) {
        return null;
      }
    }

    async function dbPut(storeName, row) {
      const handle = await db();
      if (!handle) { return; }
      try {
        const tx = handle.transaction([storeName], 'readwrite');
        tx.objectStore(storeName).put(row);
        await new Promise(resolve => {
          tx.oncomplete = resolve;
          tx.onerror = resolve;
          tx.onabort = resolve;
        });
      } catch (err) {
        /* A write that could not land is a slower next boot, never a failed
         * read: the bytes were already returned to the caller. */
      }
    }

    /* Stored as ArrayBuffer, handed out as Uint8Array. */
    const asBytes = (d) => (d ? new Uint8Array(d) : null);

    /*
     * One GET, over the two routes a file may be served by.
     *
     * `/boot/<path>` is the IO server's source route and is tried first; the
     * bare path is the fallback, because a page may be served by anything that
     * hands out files and need not have an IO server at all.
     *
     * Returns bytes, or null when the file is genuinely absent. Throws
     * `torirsUnreachable` when neither route answered -- see the file comment.
     */
    async function fetchFile(path) {
      return await fetchUrls([`${bootUrl}/${path}`, `/${path}`], path);
    }

    /*
     * Try each URL in turn; the first that answers with a body wins.
     *
     * A 404 from one route is not the end -- the next may have it. Only a list
     * where NOTHING answered at the network level is an outage, which is the
     * distinction the whole file turns on.
     */
    async function fetchUrls(urls, what) {
      let sawResponse = false;

      for (const url of urls) {
        let response;
        try {
          response = await fetch(url, { cache: 'no-store' });
        } catch (err) {
          /* Network-level failure. Try the other route before concluding
           * anything: the IO server may be gone while the static host serving
           * the page is fine. */
          continue;
        }
        sawResponse = true;
        if (response.ok) { return new Uint8Array(await response.arrayBuffer()); }
        /* A 404 from /boot/ is not the end -- the bare path may still have it.
         * Any other status is the server refusing, and refusing is still the
         * server being there. */
      }

      if (!sawResponse) { throw unreachable(`nothing answered for ${what}`); }
      return null;
    }

    return {
      /*
       * A server-backed file: a plugin script, its manifest, a shipped asset, a
       * config file. Database first, then the server.
       */
      async readFile(path) {
        const row = await dbGet('files', path);
        if (row) { return asBytes(row.d); }
        if (denied.has(path)) { return null; }

        const bytes = await fetchFile(path);
        if (bytes) { await dbPut('files', { k: path, d: bytes.slice().buffer, e: null }); }
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
       * means "not saved yet", which is an answer and not a reason to look
       * elsewhere.
       */
      async readClientFile(path) {
        const row = await dbGet('files', path);
        return row ? asBytes(row.d) : null;
      },

      async writeClientFile(path, bytes) {
        await dbPut('files', { k: path, d: bytes.slice().buffer, e: null });
      },

      /*
       * One cache group's raw container, out of the database and nowhere else.
       *
       * ARCHIVES HAVE THEIR OWN TRANSPORT and it is not this one. dat2 groups
       * arrive over JS5 and dat1 archives over the 2004 on-demand protocol,
       * each talking to its own server, and both write what they download into
       * this database. So a miss here means "not downloaded yet", and the thing
       * that fixes it is the cache producer, not an HTTP request made from this
       * file. Adding a fallback here would put a second way for an archive to
       * arrive -- one that bypasses the CRC and version checks the real
       * producers do -- to answer a question they were already answering.
       *
       * Keyed by generation as well as address, so a browser that has held two
       * caches never answers one generation's read out of the other's records:
       * that would decode, and be wrong.
       */
      async readArchive(table, archive) {
        const row = await dbGet('groups', `${cacheKey()}|${table}|${archive}`);
        return row ? asBytes(row.d) : null;
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
       * not change shape when a page starts supplying them.
       */
      async xteaKey(table, archive) {
        return null;
      },
    };
  };
})();
