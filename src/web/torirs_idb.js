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

  const DB_NAME = 'torirs-cache';
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
        request = indexedDB.open(DB_NAME, DB_VERSION);
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

  window.ToriRS_IDB = {
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
      const row = await get('groups', this.groupKey(cacheKey, table, archive, flags));
      return row ? asBytes(row.d) : null;
    },

    async groupPut(cacheKey, table, archive, flags, bytes) {
      await put('groups', {
        k: this.groupKey(cacheKey, table, archive, flags),
        c: cacheKey, t: table, a: archive, f: flags | 0, d: asBuffer(bytes),
      });
    },

    async fileGet(path) {
      const row = await get('files', path);
      return row ? asBytes(row.d) : null;
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
