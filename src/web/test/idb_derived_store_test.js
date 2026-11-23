'use strict';
/*
 * ToriRS_IDB's derived-record database (docs/CS2_JIT.md 4.6), and the two
 * host functions over it (torirs_hostio.js readDerived / writeDerived).
 *
 * Node only. The shipped torirs_idb.js and torirs_hostio.js are evaluated
 * against a fake indexedDB that keeps one set of stores per DATABASE, so the
 * test can say which database a record went to and which version each was
 * opened at.
 *
 * What this pins:
 *   - the records live in "<idb name>-derived", version 1, store `derived`
 *     with a `by_cache` index -- never a fourth store in "torirs-cache",
 *     which stays at version 1 and never sees a derived row;
 *   - a row is keyed "<cacheKey>|<name>" and carries the cache key;
 *   - derivedGet answers the bytes or null; derivedPut answers whether the
 *     transaction committed (false on an abort, and on no database at all);
 *   - clearCache(key) removes that cache's derived rows and no other cache's;
 *   - hostio's readDerived / writeDerived key by the provider's cacheKey().
 *
 *   node src/web/test/idb_derived_store_test.js
 */
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const IDB_SRC = fs.readFileSync(path.join(__dirname, '..', 'torirs_idb.js'), 'utf8');
const HOSTIO_SRC = fs.readFileSync(path.join(__dirname, '..', 'torirs_hostio.js'), 'utf8');

function makeIndexedDb() {
  const databases = new Map(); /* name -> { version, stores: Map(name -> {rows: Map, indexes}) } */
  const opens = [];
  let failNextWrite = false;
  const later = (fn) => setImmediate(fn);
  const request = (result) => {
    const r = { result, error: null };
    later(() => r.onsuccess && r.onsuccess());
    return r;
  };
  function dbHandle(name) {
    const entry = databases.get(name);
    return {
      name,
      objectStoreNames: { contains: (n) => entry.stores.has(n) },
      createObjectStore(store, opts) {
        const s = { keyPath: opts.keyPath, rows: new Map(), indexes: new Map() };
        entry.stores.set(store, s);
        return { createIndex(iname, keyPath) { s.indexes.set(iname, keyPath); } };
      },
      transaction(names, mode) {
        const tx = { mode, oncomplete: null, onabort: null, onerror: null, error: null };
        let pending = 0;
        const done = () => later(() => {
          if (pending !== 0) { return; }
          if (mode === 'readwrite' && failNextWrite) {
            failNextWrite = false;
            tx.error = { name: 'QuotaExceededError' };
            tx.onerror && tx.onerror();
            return;
          }
          tx.oncomplete && tx.oncomplete();
        });
        tx.objectStore = (storeName) => {
          const s = entry.stores.get(storeName);
          if (!s) { throw new Error(`no store ${storeName} in ${name}`); }
          return {
            put(row) { pending++; s.rows.set(row[s.keyPath], row); later(() => { pending--; done(); }); },
            get(key) { return request(s.rows.get(key)); },
            index(iname) {
              const keyPath = s.indexes.get(iname);
              if (!keyPath) { throw new Error(`no index ${iname}`); }
              return {
                openCursor(range) {
                  const r = { result: null, onsuccess: null };
                  const keys = Array.from(s.rows.keys()).filter(k => s.rows.get(k)[keyPath] === range.only);
                  let i = 0;
                  const step = () => later(() => {
                    if (i >= keys.length) {
                      r.result = null;
                      r.onsuccess && r.onsuccess({ target: r });
                      done();
                      return;
                    }
                    const key = keys[i++];
                    r.result = { delete() { s.rows.delete(key); }, continue() { step(); } };
                    r.onsuccess && r.onsuccess({ target: r });
                  });
                  pending++;
                  later(() => { pending--; step(); });
                  return r;
                },
              };
            },
          };
        };
        if (mode === 'readwrite') { later(done); }
        return tx;
      },
    };
  }
  return {
    databases, opens,
    failNextWrite() { failNextWrite = true; },
    open(name, version) {
      opens.push({ name, version });
      const r = { result: null };
      later(() => {
        let entry = databases.get(name);
        const upgrade = !entry || entry.version < version;
        if (!entry) { entry = { version, stores: new Map() }; databases.set(name, entry); }
        entry.version = version;
        r.result = dbHandle(name);
        if (upgrade && r.onupgradeneeded) { r.onupgradeneeded({ target: { result: r.result } }); }
        r.onsuccess && r.onsuccess();
      });
      return r;
    },
  };
}

function load(fake) {
  const context = {
    window: {}, indexedDB: fake, console: { log() {}, warn() {}, error() {} },
    setTimeout, clearTimeout, setImmediate, URLSearchParams, Date, Uint8Array, Map, Set, Promise,
    IDBKeyRange: { bound: (lower, upper) => ({ lower, upper }), only: (v) => ({ only: v }) },
    performance: { now: () => Date.now() },
  };
  context.window.addEventListener = () => {};
  context.window.location = { search: '' };
  vm.createContext(context);
  vm.runInContext(IDB_SRC, context, { filename: 'torirs_idb.js' });
  vm.runInContext(HOSTIO_SRC, context, { filename: 'torirs_hostio.js' });
  return context.window;
}

const bytes = (...v) => new Uint8Array(v);

async function main() {
  let passed = 0;
  const ok = () => { passed++; };

  const fake = makeIndexedDb();
  const win = load(fake);
  const idb = win.ToriRS_IDB;

  /* 1. Its own database, version 1; nothing is added to torirs-cache. */
  assert.strictEqual(idb.derivedDatabaseName(), 'torirs-cache-derived');
  assert.strictEqual(await idb.derivedGet('../../cache.osrs239', 'cs2jit-1a2b3c4d-v1.pack'), null, 'absent is null');
  assert.ok(fake.databases.has('torirs-cache-derived'), 'the derived database exists once used');
  assert.ok(!fake.databases.has('torirs-cache'), 'the cache database was not even opened by a derived read');
  const derivedOpen = fake.opens.find(o => o.name === 'torirs-cache-derived');
  assert.strictEqual(derivedOpen.version, 1, 'opened at version 1');
  const ddb = fake.databases.get('torirs-cache-derived');
  assert.ok(ddb.stores.has('derived'), 'store "derived"');
  assert.strictEqual(ddb.stores.get('derived').indexes.get('by_cache'), 'c', 'by_cache indexes the cache key');
  ok();

  /* 2. Put and get, keyed by cache and name. */
  assert.strictEqual(await idb.derivedPut('A', 'cs2jit-1a2b3c4d-v1.pack', bytes(1, 2, 3)), true, 'committed');
  assert.strictEqual(await idb.derivedPut('B', 'cs2jit-1a2b3c4d-v1.pack', bytes(9)), true);
  assert.strictEqual(await idb.derivedPut('A', 'other.bin', bytes(4)), true);
  const rows = ddb.stores.get('derived').rows;
  assert.ok(rows.has('A|cs2jit-1a2b3c4d-v1.pack') && rows.has('B|cs2jit-1a2b3c4d-v1.pack'), 'keyed "<cacheKey>|<name>"');
  const row = rows.get('A|cs2jit-1a2b3c4d-v1.pack');
  assert.strictEqual(row.c, 'A');
  assert.strictEqual(row.n, 'cs2jit-1a2b3c4d-v1.pack');
  assert.ok(row.d instanceof ArrayBuffer || Object.prototype.toString.call(row.d) === '[object ArrayBuffer]',
    'stored as an ArrayBuffer');
  assert.deepStrictEqual(Array.from(await idb.derivedGet('A', 'cs2jit-1a2b3c4d-v1.pack')), [1, 2, 3]);
  assert.deepStrictEqual(Array.from(await idb.derivedGet('B', 'cs2jit-1a2b3c4d-v1.pack')), [9]);
  /* Replaced whole. */
  assert.strictEqual(await idb.derivedPut('A', 'cs2jit-1a2b3c4d-v1.pack', bytes(5, 6)), true);
  assert.deepStrictEqual(Array.from(await idb.derivedGet('A', 'cs2jit-1a2b3c4d-v1.pack')), [5, 6]);
  ok();

  /* 3. A refused commit answers false. */
  fake.failNextWrite();
  assert.strictEqual(await idb.derivedPut('A', 'cs2jit-1a2b3c4d-v1.pack', bytes(7)), false, 'a refused write');
  ok();

  /* 4. clearCache drops that cache's derived rows only. */
  await idb.clearCache('A');
  assert.strictEqual(await idb.derivedGet('A', 'cs2jit-1a2b3c4d-v1.pack'), null, 'A\'s pack is gone');
  assert.strictEqual(await idb.derivedGet('A', 'other.bin'), null, 'every A record is gone');
  assert.deepStrictEqual(Array.from(await idb.derivedGet('B', 'cs2jit-1a2b3c4d-v1.pack')), [9], 'B stays');
  assert.strictEqual(fake.databases.get('torirs-cache').version, 1, 'torirs-cache stays at version 1');
  assert.ok(!fake.databases.get('torirs-cache').stores.has('derived'), 'and gains no store');
  ok();

  /* 5. The host functions key by the provider's cacheKey(). */
  let key = 'gen-1';
  const host = win.ToriRS_CreateHostIO(() => key, '/boot/', {}, '');
  assert.strictEqual(await host.writeDerived('cs2jit-1a2b3c4d-v1.pack', bytes(1, 1)), true);
  assert.ok(rows.has('gen-1|cs2jit-1a2b3c4d-v1.pack'), 'written under cacheKey()');
  assert.deepStrictEqual(Array.from(await host.readDerived('cs2jit-1a2b3c4d-v1.pack')), [1, 1]);
  key = 'gen-2';
  assert.strictEqual(await host.readDerived('cs2jit-1a2b3c4d-v1.pack'), null, 'another generation does not see it');
  ok();

  /* 6. No IndexedDB at all (private browsing): absent and failed, no throw. */
  {
    const throwing = { open() { throw new Error('disabled'); } };
    const w2 = load(throwing);
    assert.strictEqual(await w2.ToriRS_IDB.derivedGet('A', 'x'), null);
    assert.strictEqual(await w2.ToriRS_IDB.derivedPut('A', 'x', bytes(1)), false);
    ok();
  }

  console.log(`idb_derived_store_test: ${passed} ok`);
}

main().catch(err => { console.error(`FAIL: ${err.stack || err.message}`); process.exit(1); });
