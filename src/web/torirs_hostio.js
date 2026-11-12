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

  /* Hand `bytes` to the browser as a file the player saves. */
  function saveDownload(name, bytes, type) {
    const url = URL.createObjectURL(new Blob([bytes], { type: type }));
    const link = document.createElement('a');
    link.href = url;
    link.download = name;
    document.body.appendChild(link);
    link.click();
    link.remove();
    setTimeout(() => URL.revokeObjectURL(url), 60000);
  }

  /*
   * A .zip of `entries` ({name, bytes}), STORED rather than deflated: every
   * entry is a PNG, which is compressed already, and a deflate would cost the
   * page a library to save nothing. Names keep their '/', so the archive
   * unpacks into the same player/category folders the desktop writes.
   */
  let crcTable = null;
  function crc32(bytes) {
    if (!crcTable) {
      crcTable = new Uint32Array(256);
      for (let n = 0; n < 256; n++) {
        let c = n;
        for (let k = 0; k < 8; k++) { c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; }
        crcTable[n] = c >>> 0;
      }
    }
    let crc = 0xffffffff;
    for (let i = 0; i < bytes.length; i++) { crc = crcTable[(crc ^ bytes[i]) & 0xff] ^ (crc >>> 8); }
    return (crc ^ 0xffffffff) >>> 0;
  }

  function zipStored(entries) {
    const encoder = new TextEncoder();
    const locals = [];
    const centrals = [];
    let offset = 0;
    for (const entry of entries) {
      const name = encoder.encode(entry.name);
      const crc = crc32(entry.bytes);
      const local = new DataView(new ArrayBuffer(30));
      local.setUint32(0, 0x04034b50, true);
      local.setUint16(4, 20, true);
      local.setUint16(6, 0x0800, true);              /* names are UTF-8 */
      local.setUint32(14, crc, true);
      local.setUint32(18, entry.bytes.length, true);
      local.setUint32(22, entry.bytes.length, true);
      local.setUint16(26, name.length, true);
      locals.push(new Uint8Array(local.buffer), name, entry.bytes);
      const central = new DataView(new ArrayBuffer(46));
      central.setUint32(0, 0x02014b50, true);
      central.setUint16(4, 20, true);
      central.setUint16(6, 20, true);
      central.setUint16(8, 0x0800, true);
      central.setUint32(16, crc, true);
      central.setUint32(20, entry.bytes.length, true);
      central.setUint32(24, entry.bytes.length, true);
      central.setUint16(28, name.length, true);
      central.setUint32(42, offset, true);
      centrals.push(new Uint8Array(central.buffer), name);
      offset += 30 + name.length + entry.bytes.length;
    }
    const centralSize = centrals.reduce((sum, part) => sum + part.length, 0);
    const end = new DataView(new ArrayBuffer(22));
    end.setUint32(0, 0x06054b50, true);
    end.setUint16(8, entries.length, true);
    end.setUint16(10, entries.length, true);
    end.setUint32(12, centralSize, true);
    end.setUint32(16, offset, true);
    const parts = locals.concat(centrals, [new Uint8Array(end.buffer)]);
    const out = new Uint8Array(offset + centralSize + 22);
    let at = 0;
    for (const part of parts) { out.set(part, at); at += part.length; }
    return out;
  }

  /**
   * Build the provider.
   *
   * `cacheKeyOf` is a function returning the generation these archives belong
   * to -- a function because the provider is built while the page assembles
   * Module, and which cache is open is not settled until the boot barrier has
   * run. `producers` supplies the two cache fillers; either may be absent, and
   * a deployment with neither is a client that reads only what it already has.
   */
  window.ToriRS_CreateHostIO = function (cacheKeyOf, bootUrl, producers, authBase) {
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
     * The page's picture of the database, per (cache, table, flags): one
     * key-range scan, memoised as a promise that also carries its result
     * once it has one (`settled`), plus what this session has written since.
     * What lets a cold miss be answered without a transaction -- see
     * readArchive -- and a prefetch wave ask the database only for what it
     * holds.
     */
    const presentIds = new Map();
    const stored = new Set();
    /* Whole-table reads in progress for a warm prefetch, by scan key. */
    const wholeTables = new Map();

    /*
     * Live reloads in progress, by dat2 table: the JS5 client's re-prime of
     * that index (invalidateArchive below). Until it settles the stored
     * copies of the index may be the old bytes, so every read of the index
     * -- its groups and its reference table 255/<index> -- waits here first.
     * The re-prime's own reads and writes go to the database directly and
     * never through these gates.
     */
    const reprimes = new Map();

    async function awaitReprime(table, archive, flags) {
      if (isDat1(flags) || reprimes.size === 0) { return; }
      const index = table === 255 ? archive : table;
      for (let gate = reprimes.get(index); gate; gate = reprimes.get(index)) {
        await gate.catch(() => null);
      }
    }

    function scanTable(key, table, flags) {
      const scanKey = `${key}|${table}|${flags | 0}`;
      let scan = presentIds.get(scanKey);
      if (!scan) {
        scan = typeof idb.groupIdsPresent === 'function'
          ? idb.groupIdsPresent(key, table, flags).then(set => { scan.settled = set; return set; })
          : Promise.resolve(new Set());
        scan.settled = null;
        presentIds.set(scanKey, scan);
      }
      return scan;
    }

    /*
     * The files store's keys, scanned once (a promise carrying its result
     * in `settled`), plus what this session has written. A path the scan
     * does not list and the session has not written is a miss decided
     * here, without a transaction -- see readFile and readClientFile.
     */
    let fileScan = null;
    const storedFiles = new Set();

    function scanFiles() {
      if (!fileScan) {
        /* A store that cannot list its keys (an older page, a test's
         * fake) leaves `settled` null for good: unknown, never absent. */
        fileScan = typeof idb.fileKeysPresent === 'function'
          ? idb.fileKeysPresent().then(set => { fileScan.settled = set; return set; })
          : Promise.resolve(null);
        fileScan.settled = null;
      }
      return fileScan;
    }

    const fileKnownAbsent = (path) => {
      const scan = scanFiles();
      return scan.settled !== null && !scan.settled.has(path) && !storedFiles.has(path);
    };

    /* Files already settled against the server this session. A plugin asks
     * for its assets every frame; one conditional request per path per session
     * is the price of never running a stale script, and the rest are local. */
    const settled = new Set();

    /*
     * One conditional GET, over the two routes a file may be served by.
     *
     * `/boot/<path>` is io_server's source route and is tried first; the bare
     * path is the fallback, because a page may be served by anything that hands
     * out files and need not have an io_server at all.
     *
     * @return `{ bytes, etag }` for a 200, `'unchanged'` for a 304 against
     * `etag`, or null when every route that answered said "not here".
     */
    async function fetchFile(path, etag) {
      let sawResponse = false;

      for (const url of [`${bootUrl}/${path}`, `/${path}`]) {
        let response;
        try {
          const headers = etag ? { 'If-None-Match': etag } : undefined;
          response = await fetch(url, { cache: 'no-store', headers });
        } catch (err) {
          /* Network-level failure. Try the other route before concluding
           * anything: io_server may be gone while the static host serving the
           * page is fine. */
          continue;
        }
        sawResponse = true;
        if (response.status === 304 && etag) { return 'unchanged'; }
        if (response.ok) {
          return {
            bytes: new Uint8Array(await response.arrayBuffer()),
            etag: response.headers.get('ETag')
          };
        }
        /* A 404 from one route is not the end; the other may have it. Any other
         * status is the server refusing, and refusing is still being there. */
      }

      if (!sawResponse) { throw unreachable(`nothing answered for ${path}`); }
      return null;
    }

    return {
      /*
       * A server-backed file: a plugin script, its manifest, a shipped asset, a
       * config file.
       *
       * The server is the authority and the database is its copy. The first
       * read of a path in a session revalidates the stored copy (a 304 costs a
       * header); later reads are local. This used to be "database first", which
       * meant a script stored once was never fetched again: every plugin
       * rewritten since a browser first loaded it kept running the old text.
       *
       * Nothing answering is not "not there": the stored copy is served, and
       * only with no copy does the outage reach the executor.
       */
      async readFile(path) {
        if (denied.has(path)) { return null; }
        const held = fileKnownAbsent(path) ? null : await idb.fileEntry(path);
        if (held && settled.has(path)) { return held.bytes; }

        let fetched;
        try {
          fetched = await fetchFile(path, held && held.etag);
        } catch (err) {
          if (held && err.torirsUnreachable) { return held.bytes; }
          throw err;
        }

        settled.add(path);
        if (fetched === 'unchanged') { return held.bytes; }
        if (fetched) {
          await idb.filePut(path, fetched.bytes, fetched.etag);
          storedFiles.add(path);
          return fetched.bytes;
        }
        /* The server answered and does not have it. A stored copy of a file
         * the server has deleted is exactly the staleness this guards against. */
        denied.add(path);
        return null;
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
        if (fileKnownAbsent(path)) { return null; }
        return await idb.fileGet(path);
      },

      async writeClientFile(path, bytes) {
        await idb.filePut(path, bytes, null);
        storedFiles.add(path);
      },

      /*
       * The files service (plugin/task_plugin_io.h CreateTask_PluginFiles):
       * the verbs a desktop answers over its folder, answered here over the
       * records writeClientFile made. The answer is the same text a desktop
       * gives -- a verdict line, then the verb's own -- so the plugin above
       * it is one plugin.
       *
       *   list      dir=   every .png key below it. No mtime or size: those
       *                    would need the records' VALUES, and a listing of a
       *                    folder of pictures must not read the pictures.
       *   download  dir=   all of them, as one stored .zip the browser saves;
       *            path=  or one picture, as itself.
       *   reveal, pick_folder: a browser has no folder to show or choose.
       */
      async filesService(verb, bodyBytes) {
        const body = {};
        for (const line of new TextDecoder().decode(bodyBytes || new Uint8Array(0)).split('\n')) {
          const eq = line.indexOf('=');
          if (eq > 0) { body[line.slice(0, eq)] = line.slice(eq + 1).replace(/\r$/, ''); }
        }
        const text = (answer) => new TextEncoder().encode(answer);
        const prefix = body.dir ? `${body.dir.replace(/\/+$/, '')}/` : '';
        const pngsUnder = async () => {
          if (!prefix || typeof idb.fileKeysUnder !== 'function') { return []; }
          return (await idb.fileKeysUnder(prefix)).filter(key => /\.png$/i.test(key));
        };

        if (verb === 'list') {
          const keys = await pngsUnder();
          return text(`ok\n${keys.map(key => `${key.slice(prefix.length)}\t0\t0\n`).join('')}`);
        }
        if (verb === 'download') {
          if (body.path) {
            const bytes = await idb.fileGet(body.path);
            if (!bytes) { return text(`error: ${body.path} is not stored in this browser\n`); }
            saveDownload(body.path.slice(body.path.lastIndexOf('/') + 1), bytes, 'image/png');
            return text('ok\n1\n');
          }
          const keys = await pngsUnder();
          if (!keys.length) { return text('error: there are no pictures to download yet\n'); }
          const entries = [];
          for (const key of keys) {
            const bytes = await idb.fileGet(key);
            if (bytes) { entries.push({ name: key.slice(prefix.length), bytes: bytes }); }
          }
          const stamp = new Date().toISOString().slice(0, 19).replace(/[T:]/g, '-');
          saveDownload(`screenshots_${stamp}.zip`, zipStored(entries), 'application/zip');
          return text(`ok\n${entries.length}\n`);
        }
        if (verb === 'reveal' || verb === 'pick_folder') {
          return text('unsupported: a browser keeps them in its own storage; download them\n');
        }
        return text(`error: no files/${verb}\n`);
      },

      /*
       * One request to the account server (game/task_accounts_io.h): a form
       * POST to this page's own origin, which is io_server's sign-in gate, so
       * the gate's cookie rides along without the client ever holding it
       * ('same-origin' credentials; the cookie is HttpOnly). The answer is the
       * body whatever the status -- an OAuth refusal is JSON that says why --
       * and null only when nothing answered.
       */
      async authPost(endpoint, bytes) {
        const routes = {
          token: 'oauth/token', play: 'game/play', revoke: 'oauth/revoke',
          /* The gate session's own player accounts (io_gate.h /account). */
          user: 'account/players', link: 'account/link', userplay: 'account/play',
        };
        const route = routes[endpoint];
        if (!route) { return null; }
        let response;
        try {
          response = await fetch(`${authBase || '/'}${route}`, {
            method: 'POST',
            cache: 'no-store',
            credentials: 'same-origin',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: bytes,
          });
        } catch (err) {
          return null;
        }
        return new Uint8Array(await response.arrayBuffer());
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
        await awaitReprime(table, archive, flags);
        const key = cacheKey();
        /*
         * A miss decided in memory, when the table has been scanned.
         *
         * A database read is a transaction, and a transaction on the
         * groups store queues behind every batched write ahead of it: the
         * two sound effects the title screen asks for arrived right after
         * a 17 MB fill and waited 600 ms for the fill's commits before
         * being told "no". The scan (see scanTable) is one key-range read
         * per table per session, taken when the table's index is opened --
         * before the fills -- so a cold read costs no transaction at all.
         */
        const scan = presentIds.get(`${key}|${table}|${flags | 0}`);
        if (scan && scan.settled && !scan.settled.has(archive) &&
            !stored.has(`${key}|${table}|${flags | 0}|${archive}`)) {
          return await this.produceArchive(key, table, archive, flags);
        }
        /* `flags` addresses the record as well as choosing the producer: on
         * dat1 a square's terrain and its locs share a table and an archive id
         * and differ only here. See ToriRS_IDB.groupKey. */
        const held = await idb.groupGet(key, table, archive, flags);
        if (held) { return held; }
        return await this.produceArchive(key, table, archive, flags);
      },

      /* The producer half of readArchive: fetch, store, hand over. */
      async produceArchive(key, table, archive, flags) {

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
        await idb.groupPut(key, table, archive, flags, bytes);
        stored.add(`${key}|${table}|${flags | 0}|${archive}`);
        return bytes;
      },

      /*
       * Many containers of one table at once: the database in one
       * transaction, then the producer for every miss, together.
       *
       * What a prefetch wave calls (TORIRS_IOK_CACHE_PREFETCH). The single
       * readArchive above is the right shape for a read that wants one
       * answer; a wave of 512 wants 512 answers and one storage round trip,
       * not 512 of them queued ahead of the first request to the server.
       * Returns bytes-or-null per id, in the order asked.
       */
      async readArchives(table, ids, flags) {
        await awaitReprime(table, -1, flags);
        const key = cacheKey();
        /* Which of these the database holds, from one key scan per table
         * per session -- a cold cache then asks the database for nothing
         * and goes straight to the producer. In-session writes are counted
         * by the writer's queue at scan time and by `stored` after it. */
        const scanKey = `${key}|${table}|${flags | 0}`;
        const present = await scanTable(key, table, flags);
        const want = ids.filter(id => present.has(id) || stored.has(`${scanKey}|${id}`));
        /*
         * The database's share of the wave. A table that is mostly there
         * (a warm start) is read WHOLE, once, with a single getAll, and
         * the waves take their rows out of that one answer; a table that
         * is mostly absent asks for the few it holds one by one. The
         * whole-table read is dropped as soon as it has been picked clean,
         * so a fill holds one table's bytes twice for a moment and never
         * three tables' for a session.
         */
        let held;
        if (want.length > 0 && present.size >= ids.length / 2 && typeof idb.groupGetAll === 'function') {
          let whole = wholeTables.get(scanKey);
          if (!whole) {
            whole = idb.groupGetAll(key, table, flags);
            wholeTables.set(scanKey, whole);
          }
          const rows = await whole;
          held = new Map();
          for (const id of want) {
            const bytes = rows.get(id);
            if (bytes) { held.set(id, bytes); rows.delete(id); }
          }
          if (rows.size === 0) { wholeTables.delete(scanKey); }
        } else {
          held = want.length ? await idb.groupGetMany(key, table, want, flags) : new Map();
        }
        const producer = isDat1(flags) ? onDemand() : js5();
        return await Promise.all(ids.map(async id => {
          const hit = held.get(id);
          if (hit) { return hit; }
          if (!producer) { return null; }
          const bytes = isDat1(flags)
            ? await producer.file(table, id, flags)
            : await producer.group(table, id);
          if (!bytes) { return null; }
          await idb.groupPut(key, table, id, flags, bytes);
          stored.add(`${scanKey}|${id}`);
          return bytes;
        }));
      },

      /* Archive 255 of a table is its reference table -- the same container
       * shape as any group, addressed the way the cache addresses it. dat2
       * only: dat1 has no reference tables, it has a versionlist. */
      async readReferenceTable(table) {
        /* Opening a table's index is the moment to learn which of its
         * groups are stored: it happens before any fill writes, so the scan
         * never queues behind a commit, and every later read of the table
         * decides its misses in memory. Kicked, not awaited. */
        scanTable(cacheKey(), table, CACHE_DAT2).catch(() => null);
        scanFiles().catch(() => null);
        return await this.readArchive(255, table, CACHE_DAT2);
      },

      /*
       * A live reload rewrote archive `archive` of dat2 table `table` on the
       * server (CACHE_INVALIDATE; platform_web_io.js has already dropped its
       * decoded and resident copies). The JS5 client re-primes the index
       * against this database: a fresh 255/255, 255/<table> when the stored
       * one no longer matches the master, then every stored group of the
       * table checked against the new table by the CRC of its container --
       * the test the native client makes of its sparse copy -- and the
       * rewritten ones fetched and overwritten; the rest stay and keep being
       * read from here. Reads of the table wait for it (awaitReprime).
       *
       * `opts.decodeReference(bytes)` turns a reference table into its
       * listing (id -> {crc, version}; the executor's rscache decode),
       * `opts.log(line)` prints the steps under
       * TORIRS_CACHE_INVALIDATE_DEBUG. A lane with no JS5 producer has
       * nothing stored to re-prime from and answers null.
       */
      invalidateArchive(table, archive, opts) {
        const producer = js5();
        if (!producer || typeof producer.invalidateArchive !== 'function') { return null; }
        const key = cacheKey();
        const scanKey = `${key}|${table}|${CACHE_DAT2}`;
        const options = opts || {};
        /* The whole-table read a warm prefetch shares would hand out rows
         * read before the rewrite. */
        wholeTables.delete(scanKey);
        const store = {
          /* Every stored id of the table: a fresh scan (the session's memo
           * may predate writes) plus what this session wrote. */
          async ids() {
            const found = typeof idb.groupIdsPresent === 'function'
              ? await idb.groupIdsPresent(key, table, CACHE_DAT2)
              : new Set();
            const prefix = `${scanKey}|`;
            for (const k of stored) {
              if (k.startsWith(prefix)) { found.add(parseInt(k.substring(prefix.length), 10)); }
            }
            return Array.from(found).sort((a, b) => a - b);
          },
          get: (id) => idb.groupGet(key, table, id, CACHE_DAT2),
          async put(id, bytes) {
            await idb.groupPut(key, table, id, CACHE_DAT2, bytes);
            stored.add(`${scanKey}|${id}`);
          },
          getReference: () => idb.groupGet(key, 255, table, CACHE_DAT2),
          async putReference(bytes) {
            await idb.groupPut(key, 255, table, CACHE_DAT2, bytes);
            stored.add(`${key}|255|${CACHE_DAT2}|${table}`);
          },
        };
        const run = producer.invalidateArchive(table, {
          store,
          decodeReference: options.decodeReference || null,
          log: options.log || null,
        });
        reprimes.set(table, run);
        const settle = () => { if (reprimes.get(table) === run) { reprimes.delete(table); } };
        run.then(settle, settle);
        return run;
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
