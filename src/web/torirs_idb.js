/*
 * The browser's persistent cache storage.
 *
 * One module, because there are three readers and writers of these records --
 * the platform IO executor, the JS5 producer and the dat1 on-demand producer --
 * and a second opinion about the schema would be a cache that decodes and is
 * wrong rather than one that fails.
 *
 * ## No mirror
 *
 * Records are read from IndexedDB and returned. There is no JavaScript Map in
 * front of it and no hydrate pass before the first frame. An earlier design had
 * both, for exactly one reason: the C side reached the store through a
 * synchronous facade and could not await a database request. Everything that
 * touches this file is asynchronous now, so the reason is gone -- and with it a
 * whole-cache cursor walk at boot and the entire resident cache held twice, in
 * the database and again on the JS heap, for the life of the tab.
 *
 * IndexedDB is itself an indexed on-disk key/value store with its own page
 * cache. Putting a hand-rolled one in front of it is a pessimisation until
 * something profiled says otherwise, and nothing has. If a cache is ever wanted
 * it belongs behind this interface, and measured.
 *
 * ## Schema
 *
 *   groups   one raw container per (cache, table, archive). What is stored is
 *            exactly what an idx record addresses -- compression byte, lengths,
 *            payload -- never a decoded archive: decoding is the reader's job
 *            and a stored decode would be a second format in the database.
 *            Keyed by generation as well as address, so a browser holding two
 *            caches never answers one's read out of the other's records.
 *
 *   files    client and server files by path: saved settings, plugin scripts,
 *            shipped assets.
 *
 *   boot     configuration the client opens by name, with the validator the
 *            server gave it, so the next boot can ask "still this one?".
 */

(function () {
  'use strict';

  /*
   * Which database this cache lives in.
   *
   * A constant meant one browser origin held exactly one cache, so two worlds
   * served from the same page shared a database and could only be separated by
   * editing this file. The native build segments its cache directory by game
   * and world for the same reason -- two caches that share a home wipe each
   * other whenever their server checksums differ, which is the re-streaming
   * the cache exists to avoid.
   *
   * Named, in order, by:
   *
   *   window.TORIRS_IDB_NAME   set by the page before the first read
   *   ?idb=<name>              the query string, matching how everything else
   *                            about this page is configured
   *   'torirs-cache'           the default, unchanged
   *
   * A boot manifest spells the same thing `[cache:boot] dir=idb:<name>`, which
   * is why the value is free-form here rather than assembled from parts: the
   * manifest has already decided what the name is.
   *
   * Resolved once and then frozen. The name is read on every openDb(), and a
   * value that changed underneath a running client would split its cache
   * across two databases -- reads missing records that were written moments
   * earlier, with nothing to say why.
   */
  let dbName = null;

  function databaseName() {
    if( dbName !== null )
      return dbName;
    let name = null;
    if (typeof window.TORIRS_IDB_NAME === 'string' && window.TORIRS_IDB_NAME)
      name = window.TORIRS_IDB_NAME;
    if (!name) {
      try {
        name = new URLSearchParams(window.location.search).get('idb');
      } catch (err) {
        /* No location to read (a worker, a file: page with a hostile
         * URLSearchParams). The default is still a working cache. */
        name = null;
      }
    }
    dbName = name || 'torirs-cache';
    return dbName;
  }

  const DB_VERSION = 1;

  function reqPromise(request) {
    return new Promise((resolve, reject) => {
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => reject(request.error);
    });
  }

  function openDb() {
    return new Promise(resolve => {
      let request;
      try {
        request = indexedDB.open(databaseName(), DB_VERSION);
      } catch (err) {
        /* Private browsing with storage disabled. Not fatal: reads miss and the
         * producers refill, which is a slow client rather than a broken one. */
        resolve(null);
        return;
      }
      request.onupgradeneeded = event => {
        const db = event.target.result;
        if (!db.objectStoreNames.contains('groups')) {
          db.createObjectStore('groups', { keyPath: 'k' })
            .createIndex('by_cache', 'c', { unique: false });
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

  let dbPromise = null;
  const db = () => (dbPromise || (dbPromise = openDb()));

  let writeErrors = 0;

  async function get(storeName, key) {
    lastReadAt = nowMs();
    const handle = await db();
    if (!handle) { return null; }
    try {
      const tx = handle.transaction([storeName], 'readonly');
      return (await reqPromise(tx.objectStore(storeName).get(key))) || null;
    } catch (err) {
      return null;
    }
  }

  /*
   * One record in, awaited to completion.
   *
   * A failed write is reported once and then swallowed: quota is the usual
   * cause, the bytes have already been handed to whoever asked, and what is
   * lost is the warm start next time rather than this session.
   */
  async function put(storeName, row) {
    const handle = await db();
    if (!handle) { return; }
    try {
      const tx = handle.transaction([storeName], 'readwrite');
      tx.objectStore(storeName).put(row);
      await new Promise(resolve => {
        tx.oncomplete = resolve;
        tx.onabort = resolve;
        tx.onerror = () => {
          if (writeErrors++ === 0) {
            const why = tx.error ? tx.error.name : 'unknown';
            console.warn(`torirs cache: IndexedDB refused a write (${why}) — this ` +
                         'session is fine, but the cache will not persist');
          }
          resolve();
        };
      });
    } catch (err) {
      if (writeErrors++ === 0) {
        console.warn(`torirs cache: could not write to IndexedDB — ${err.message}`);
      }
    }
  }

  /* Stored as ArrayBuffer (structured clone would otherwise serialise the whole
   * wasm heap behind a Uint8Array view); handed out as Uint8Array. */
  const asBytes = (d) => (d ? new Uint8Array(d) : null);
  const asBuffer = (bytes) => bytes.slice().buffer;

  /*
   * Group writes are BATCHED: one transaction per flush, not one per group.
   *
   * A transaction is the unit IndexedDB commits, and a commit is a round trip
   * to the browser's storage process. The loading screen writes 24,000 groups
   * on a cold start, and awaiting a transaction apiece put every one of them
   * on the critical path of the read it belonged to: the answer could not be
   * given until its own commit had come back. Measured on the browser lane
   * (2026-09-17) the answers landed one at a time, about 0.15 ms apart,
   * behind a wire that had delivered them in bulk.
   *
   * So groupPut queues the row and returns; a flush runs when the queue
   * reaches GROUP_FLUSH_ROWS or GROUP_FLUSH_MS after the first queued row,
   * whichever comes first, and writes the whole queue in one transaction. A
   * read of a group that is still queued is answered from the queue, so
   * nothing observes the delay. What a tab that closes mid-flush loses is
   * at most one batch of a warm start, never this session's bytes -- those
   * were handed to the reader before the row was queued.
   */
  /*
   * Small transactions, one at a time. A transaction on the store holds the
   * next one -- and any READ created meanwhile -- until it commits, so the
   * size of a write batch is the longest a read can be made to wait: at
   * ~1000 rows per commit the title screen's first file read waited 700 ms
   * behind the fill's tail. 96 rows commit in a few milliseconds, and the
   * queue drains through as many of them as it takes.
   */
  const GROUP_FLUSH_ROWS = 96;
  /* ...and large ones once nobody is reading: a commit of a thousand rows
   * costs about what a commit of a hundred does, and the queue a cold boot
   * leaves behind (20,000 rows) would otherwise take seconds to drain --
   * seconds a tab closed early loses. `lastReadAt` moves on every read of
   * either store; a queue that has not been read past for this long is
   * drained in big commits. */
  const GROUP_FLUSH_ROWS_IDLE = 1024;
  const GROUP_FLUSH_IDLE_MS = 250;
  const GROUP_FLUSH_MS = 20;
  let lastReadAt = 0;
  const nowMs = () => (typeof performance !== 'undefined' ? performance.now() : Date.now());
  const groupQueue = new Map();
  let groupFlushTimer = null;
  let groupFlushing = null;
  let groupsWritten = 0;
  let groupFlushes = 0;

  /* One commit of up to GROUP_FLUSH_ROWS rows, awaited to completion. */
  async function commitGroups(rows) {
    const handle = await db();
    if (!handle) { return; }
    try {
      const tx = handle.transaction(['groups'], 'readwrite');
      const store = tx.objectStore('groups');
      for (const row of rows) { store.put(row); }
      await new Promise(resolve => {
        tx.oncomplete = resolve;
        tx.onabort = resolve;
        tx.onerror = () => {
          if (writeErrors++ === 0) {
            const why = tx.error ? tx.error.name : 'unknown';
            console.warn(`torirs cache: IndexedDB refused a write (${why}) — this ` +
                         'session is fine, but the cache will not persist');
          }
          resolve();
        };
      });
      groupsWritten += rows.length;
      groupFlushes++;
    } catch (err) {
      if (writeErrors++ === 0) {
        console.warn(`torirs cache: could not write to IndexedDB — ${err.message}`);
      }
    }
  }

  /*
   * Drain the queue, one small transaction after another, until it is
   * empty -- including rows that arrive while it runs. ONE drain at a
   * time: a second caller joins the running one rather than opening a
   * transaction beside it.
   */
  function flushGroups() {
    if (groupFlushTimer !== null) { clearTimeout(groupFlushTimer); groupFlushTimer = null; }
    if (groupFlushing) { return groupFlushing; }
    groupFlushing = (async () => {
      while (groupQueue.size > 0) {
        const rows = [];
        const limit = nowMs() - lastReadAt > GROUP_FLUSH_IDLE_MS ? GROUP_FLUSH_ROWS_IDLE : GROUP_FLUSH_ROWS;
        for (const [key, row] of groupQueue) {
          rows.push(row);
          groupQueue.delete(key);
          if (rows.length >= limit) { break; }
        }
        await commitGroups(rows);
      }
    })().finally(() => { groupFlushing = null; });
    return groupFlushing;
  }

  function scheduleGroupFlush() {
    if (groupFlushing) { return; }
    if (groupQueue.size >= GROUP_FLUSH_ROWS) { flushGroups(); return; }
    if (groupFlushTimer === null) {
      groupFlushTimer = setTimeout(() => { groupFlushTimer = null; flushGroups(); }, GROUP_FLUSH_MS);
    }
  }

  /* A tab going away flushes what it has: `pagehide` is the one event a
   * closing tab reliably delivers, and a transaction started here is allowed
   * to finish. */
  if (typeof window !== 'undefined' && typeof window.addEventListener === 'function') {
    window.addEventListener('pagehide', () => { flushGroups(); });
  }

  window.ToriRS_IDB = {
    /* Which database this cache is in. Resolved on first use and stable after,
     * so a caller that reports it reports what is actually open. */
    databaseName,

    /*
     * A container's address, and `flags` is part of it.
     *
     * Not decoration: on dat1 a map square's TERRAIN and its LOCS are asked
     * for with the same table and the same archive id -- the square's --
     * and are told apart by the flags alone (RSCache_IO_Dat1MapTerrainLoad and
     * RSCache_IO_Dat1MapSceneryLoad both pass RSCache_MapSquareId). The
     * server resolves that pair to two different archives through its
     * versionlist; on this side they are two records, and a key without the
     * flags makes them one.
     *
     * What that looked like: terrain was fetched first and stored, the locs
     * read for the same square hit that record, and the loc decoder was handed
     * terrain bytes -- which it reads as a square with nothing on it. Every
     * tile drawn, not one scenery object, and no error anywhere, because
     * "this square has no locs" is a legitimate thing for a cache to say.
     *
     * dat2 passes 0 here and is unaffected; its two are already different
     * archives.
     */
    groupKey: (cacheKey, table, archive, flags) =>
      `${cacheKey}|${table}|${archive}|${flags | 0}`,

    async groupGet(cacheKey, table, archive, flags) {
      const key = this.groupKey(cacheKey, table, archive, flags);
      /* Queued, not yet committed: the write is the answer. */
      const queued = groupQueue.get(key);
      if (queued) { return asBytes(queued.d); }
      const row = await get('groups', key);
      return row ? asBytes(row.d) : null;
    },

    /*
     * Many groups of one table in ONE readonly transaction, for a prefetch.
     *
     * A miss is the common case on a cold start, and a transaction per
     * lookup put 24,000 storage round trips ahead of the first JS5 request
     * of each wave. One transaction, one get per id, all settled together.
     * Returns a Map id -> bytes for the hits only.
     */
    async groupGetMany(cacheKey, table, ids, flags) {
      lastReadAt = nowMs();
      const found = new Map();
      const want = [];
      for (const id of ids) {
        const queued = groupQueue.get(this.groupKey(cacheKey, table, id, flags));
        if (queued) { found.set(id, asBytes(queued.d)); }
        else { want.push(id); }
      }
      if (want.length === 0) { return found; }
      const handle = await db();
      if (!handle) { return found; }
      try {
        const tx = handle.transaction(['groups'], 'readonly');
        const store = tx.objectStore('groups');
        const requests = want.map(id => reqPromise(store.get(this.groupKey(cacheKey, table, id, flags))));
        const rows = await Promise.all(requests);
        for (let i = 0; i < want.length; i++) {
          if (rows[i]) { found.set(want[i], asBytes(rows[i].d)); }
        }
      } catch (err) {
        /* Reads that failed are misses; the producer refills them. */
      }
      return found;
    },

    /*
     * Which archives of one table the database holds, as a Set of ids.
     *
     * One key-range scan (getAllKeys over the table's key prefix) instead of
     * one get per id: a prefetch wave on a cold cache asked 512 questions per
     * wave whose answer was "no" every time, and the transaction that asked
     * them sat ahead of the wave's first request to the server. The keys
     * are strings, so the range is the prefix and the prefix plus the
     * largest character. Queued-but-uncommitted rows count as present.
     */
    async groupIdsPresent(cacheKey, table, flags) {
      lastReadAt = nowMs();
      const present = new Set();
      const prefix = `${cacheKey}|${table}|`;
      const suffix = `|${flags | 0}`;
      for (const key of groupQueue.keys()) {
        if (key.startsWith(prefix) && key.endsWith(suffix)) {
          present.add(parseInt(key.substring(prefix.length), 10));
        }
      }
      const handle = await db();
      if (!handle) { return present; }
      try {
        const tx = handle.transaction(['groups'], 'readonly');
        const range = IDBKeyRange.bound(prefix, prefix + '\uffff');
        const keys = await reqPromise(tx.objectStore('groups').getAllKeys(range));
        for (const key of keys) {
          if (key.startsWith(prefix) && key.endsWith(suffix)) {
            present.add(parseInt(key.substring(prefix.length), 10));
          }
        }
      } catch (err) {
        /* An unreadable index reads as empty; the producer refills. */
      }
      return present;
    },

    /*
     * Every stored group of one table, in ONE request: a Map id -> bytes.
     *
     * What a warm prefetch wants. A wave of 512 gets in one transaction is
     * 512 answers marshalled one at a time; getAll over the table's key
     * range is one answer, and a boot that fills sprites, interfaces and
     * scripts reads three of them. Queued rows are included.
     */
    async groupGetAll(cacheKey, table, flags) {
      lastReadAt = nowMs();
      const found = new Map();
      const prefix = `${cacheKey}|${table}|`;
      const suffix = `|${flags | 0}`;
      const handle = await db();
      if (handle) {
        try {
          const tx = handle.transaction(['groups'], 'readonly');
          const range = IDBKeyRange.bound(prefix, prefix + '\uffff');
          const rows = await reqPromise(tx.objectStore('groups').getAll(range));
          for (const row of rows) {
            if (row.k.startsWith(prefix) && row.k.endsWith(suffix)) { found.set(row.a, asBytes(row.d)); }
          }
        } catch (err) {
          /* Unreadable reads as empty; the producer refills. */
        }
      }
      for (const [key, row] of groupQueue) {
        if (key.startsWith(prefix) && key.endsWith(suffix)) { found.set(row.a, asBytes(row.d)); }
      }
      return found;
    },

    /* Queued for the next batched transaction; resolves at once. See
     * flushGroups. */
    async groupPut(cacheKey, table, archive, flags, bytes) {
      groupQueue.set(this.groupKey(cacheKey, table, archive, flags), {
        k: this.groupKey(cacheKey, table, archive, flags),
        c: cacheKey, t: table, a: archive, f: flags | 0, d: asBuffer(bytes),
      });
      scheduleGroupFlush();
    },

    /** Commit every queued group write now. */
    async flush() {
      await flushGroups();
    },

    /** How the batched writer has been doing: for the status line and the
     *  probe. */
    writeStats() {
      return { queued: groupQueue.size, written: groupsWritten, flushes: groupFlushes, errors: writeErrors };
    },

    /*
     * Which paths the files store holds, as a Set: one key scan, for the
     * same reason groupIdsPresent exists. The store is a few hundred rows
     * of plugin scripts, assets and settings, and a cold boot asks for
     * most of them right after the fill -- each ask a transaction queued
     * behind the fill's commits.
     */
    async fileKeysPresent() {
      lastReadAt = nowMs();
      const present = new Set();
      const handle = await db();
      if (!handle) { return present; }
      try {
        const tx = handle.transaction(['files'], 'readonly');
        const keys = await reqPromise(tx.objectStore('files').getAllKeys());
        for (const key of keys) { present.add(key); }
      } catch (err) {
        /* Unreadable reads as empty; every path is then asked for. */
      }
      return present;
    },

    async fileGet(path) {
      const row = await get('files', path);
      return row ? asBytes(row.d) : null;
    },

    /** The stored copy WITH its validator, for a read that revalidates. */
    async fileEntry(path) {
      const row = await get('files', path);
      return row ? { bytes: asBytes(row.d), etag: row.e || null } : null;
    },

    async filePut(path, bytes, etag) {
      await put('files', { k: path, d: asBuffer(bytes), e: etag || null });
    },

    /*
     * Boot configuration -- the manifest and the RevConfig INIs it names.
     *
     * Kept apart from `files` because they are the only records with a
     * VALIDATOR that matters: they are edited by hand between runs, so a stored
     * copy can never simply be trusted, and a conditional request settles it in
     * a header instead of a body. A copy also has to survive a server that is
     * down, which is why it is stored at all rather than fetched every time.
     */
    async bootGet(path) {
      const row = await get('boot', path);
      return row ? { bytes: asBytes(row.d), etag: row.e || null } : null;
    },

    async bootPut(path, bytes, etag) {
      await put('boot', { k: path, d: asBuffer(bytes), e: etag || null });
    },

    /** Drop every group for one generation. `?cache_reset=1` — the only way to
     *  make a cold boot reproducible once a warm one has been measured. */
    async clearCache(cacheKey) {
      const handle = await db();
      if (!handle) { return 0; }
      return await new Promise(resolve => {
        let removed = 0;
        try {
          const tx = handle.transaction(['groups'], 'readwrite');
          const index = tx.objectStore('groups').index('by_cache');
          index.openCursor(IDBKeyRange.only(cacheKey)).onsuccess = ev => {
            const cursor = ev.target.result;
            if (!cursor) { return; }
            cursor.delete();
            removed++;
            cursor.continue();
          };
          tx.oncomplete = () => resolve(removed);
          tx.onerror = () => resolve(removed);
        } catch (err) {
          resolve(0);
        }
      });
    },
  };
})();
