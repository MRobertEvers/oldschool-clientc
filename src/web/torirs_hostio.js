/*
 * Where the browser's IO executor gets its bytes.
 *
 * platform/platform_web_io.js is the executor: it reads the queue, decides what
 * each item means, and calls the cache format's C API to decode. It never
 * touches the network or the database itself. This file is the other half --
 * the page's own knowledge of where things live.
 *
 * The split is deliberate. The executor is queue mechanics and belongs with the
 * platform; this is deployment -- which database, which server, which producer
 * -- and belongs with the page, because a test harness, an offline bundle or an
 * embedded viewer legitimately answers it differently with the platform
 * unchanged.
 *
 * ## Everything is async, and nothing is mirrored
 *
 * Every function returns a promise. Reads go to IndexedDB (torirs_idb.js) and
 * are returned; there is no JavaScript Map in front of it and no hydrate pass
 * before the first frame. Both existed once because a synchronous C caller
 * could not await a database request -- a constraint this lane no longer has.
 *
 * ## Local first, producer second
 *
 * A cache read is answered from the database when it is there, and otherwise by
 * the PRODUCER for that container format: JS5 for dat2, the 2004 on-demand
 * protocol for dat1. Both are JavaScript (torirs_js5.js, torirs_ondemand.js),
 * both await their sockets, and both write what they fetch into the database on
 * the way through -- so the same read a second time is local.
 *
 * Files are different: they are served over HTTP by io_server, because they are
 * source files rather than cache containers and no cache producer knows about
 * them. That is the case that had no route at all before, which is why a
 * plugin's assets were unreachable in a browser however healthy the server was.
 *
 * ## Two failures that must not be confused
 *
 * A server that answers "no such thing" is a server that is THERE. Only a
 * request nobody answers is an outage. The second throws an error carrying
 * `torirsUnreachable`, which the executor turns into
 * PlatformWeb_IO_ServerReachable going false -- and which the client uses to
 * switch off the parts of its UI that cannot work without a server behind them.
 * Returning null means "not there", and is an ordinary answer.
 */

(function () {
  'use strict';

  /* Mirrors of the queue's cache flags (asyncio.h). Which CONTAINER a request
   * is phrased in decides which producer can answer it. */
  const CACHE_DAT2 = 0;
  const CACHE_DAT1 = 1;
  const CACHE_DAT1_MAP_TERRAIN = 2;
  const CACHE_DAT1_MAP_SCENERY = 3;

  const isDat1 = (flags) =>
    flags === CACHE_DAT1 || flags === CACHE_DAT1_MAP_TERRAIN ||
    flags === CACHE_DAT1_MAP_SCENERY;

  /**
   * Build the provider.
   *
   * `cacheKeyOf` is a function returning the generation these archives belong
   * to -- a function because the provider is built while the page assembles
   * Module, and which cache is open is not settled until the boot barrier has
   * run. `producers` supplies the two cache fillers; either may be absent, and
   * a deployment with neither is a client that reads only what it already has.
   */
  window.ToriRS_CreateHostIO = function (cacheKeyOf, bootUrl, producers) {
    const idb = window.ToriRS_IDB;
    const cacheKey = () => (typeof cacheKeyOf === 'function' ? cacheKeyOf() : cacheKeyOf);
    const js5 = () => (producers && producers.js5 ? producers.js5() : null);
    const onDemand = () => (producers && producers.onDemand ? producers.onDemand() : null);

    /* Questions already answered "no". Not a mirror of the database -- a record
     * of denials, so a plugin asking every frame for an asset that does not
     * exist costs one round trip per session rather than one per frame. Never
     * populated on a transport failure: the thing may well be there, and
     * remembering "absent" would outlive the outage. */
    const denied = new Set();

    const unreachable = (message) => {
      const err = new Error(message);
      err.torirsUnreachable = true;
      return err;
    };

    /*
     * One GET, over the two routes a file may be served by.
     *
     * `/boot/<path>` is io_server's source route and is tried first; the bare
     * path is the fallback, because a page may be served by anything that hands
     * out files and need not have an io_server at all.
     */
    async function fetchFile(path) {
      let sawResponse = false;

      for (const url of [`${bootUrl}/${path}`, `/${path}`]) {
        let response;
        try {
          response = await fetch(url, { cache: 'no-store' });
        } catch (err) {
          /* Network-level failure. Try the other route before concluding
           * anything: io_server may be gone while the static host serving the
           * page is fine. */
          continue;
        }
        sawResponse = true;
        if (response.ok) { return new Uint8Array(await response.arrayBuffer()); }
        /* A 404 from one route is not the end; the other may have it. Any other
         * status is the server refusing, and refusing is still being there. */
      }

      if (!sawResponse) { throw unreachable(`nothing answered for ${path}`); }
      return null;
    }

    return {
      /* A server-backed file: a plugin script, its manifest, a shipped asset, a
       * config file. Database first, then io_server. */
      async readFile(path) {
        const held = await idb.fileGet(path);
        if (held) { return held; }
        if (denied.has(path)) { return null; }

        const bytes = await fetchFile(path);
        if (bytes) { await idb.filePut(path, bytes, null); }
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
       * means "not saved yet", which is an answer.
       */
      async readClientFile(path) {
        return await idb.fileGet(path);
      },

      async writeClientFile(path, bytes) {
        await idb.filePut(path, bytes, null);
      },

      /*
       * One cache container: the database, then the producer for its format.
       *
       * `flags` is what says which producer can answer -- a dat1 archive and a
       * dat2 group are different wire protocols talking to different servers,
       * and neither can serve the other's request. A lane with no producer for
       * this format reads only what it already holds, which is a legitimate
       * offline deployment rather than an error.
       */
      async readArchive(table, archive, flags) {
        const key = cacheKey();
        const held = await idb.groupGet(key, table, archive);
        if (held) { return held; }

        const producer = isDat1(flags) ? onDemand() : js5();
        if (!producer) { return null; }

        /* The dat1 producer is handed the FLAGS as well as the address. A
         * map read names a square rather than an archive, and only the server
         * holding the versionlist can turn one into the other -- so the
         * distinction has to survive the trip rather than be flattened here. */
        const bytes = isDat1(flags)
          ? await producer.file(table, archive, flags)
          : await producer.group(table, archive);
        if (!bytes) { return null; }

        /* Persisted before it is returned, so the next read of the same
         * container is local and the next SESSION starts warm. */
        await idb.groupPut(key, table, archive, bytes);
        return bytes;
      },

      /* Archive 255 of a table is its reference table -- the same container
       * shape as any group, addressed the way the cache addresses it. dat2
       * only: dat1 has no reference tables, it has a versionlist. */
      async readReferenceTable(table) {
        return await this.readArchive(255, table, CACHE_DAT2);
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
