/*
 * ToriRS_IDB.groupPut batches: many puts, one transaction.
 *
 * Node only, no browser or build step. The shipped torirs_idb.js is evaluated
 * against a fake indexedDB that counts transactions and records what each
 * one wrote, so the test can say "24 rows, 1 commit" rather than trust it.
 *
 * What this pins: a put resolves at once and its bytes are readable before
 * any commit (groupGet answers from the queue); the queue commits in ONE
 * transaction; a flush past GROUP_FLUSH_ROWS commits without waiting on the
 * timer; rows queued during an open transaction land in the next one, never
 * a concurrent one.
 */
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const SRC = fs.readFileSync(path.join(__dirname, '..', 'torirs_idb.js'), 'utf8');

/* The smallest fake of the IndexedDB surface the module touches. */
function makeIndexedDb() {
  const stores = { groups: new Map(), files: new Map(), boot: new Map() };
  const log = { transactions: [], open: 0 };
  const request = (result, fail) => {
    const r = { result, error: fail || null };
    setImmediate(() => { (fail ? r.onerror : r.onsuccess) && (fail ? r.onerror() : r.onsuccess()); });
    return r;
  };
  const db = {
    objectStoreNames: { contains: (n) => n in stores },
    transaction(names, mode) {
      const tx = { names, mode, puts: [], oncomplete: null, onabort: null, onerror: null, error: null };
      log.transactions.push(tx);
      /* Only writers are counted: the property under test is that the
       * writer never has two transactions open, whatever reads overlap. */
      if (mode === 'readwrite') {
        log.open++;
        if (log.open > 1) { throw new Error('two readwrite transactions open at once'); }
      }
      let pending = 0;
      const settle = () => setImmediate(() => { if (pending === 0) { log.open--; tx.oncomplete && tx.oncomplete(); } });
      tx.objectStore = (name) => ({
        put(row) { pending++; tx.puts.push(row.k); stores[name].set(row.k, row); setImmediate(() => { pending--; if (pending === 0) settle(); }); },
        get(key) { return request(stores[name].get(key) || undefined); },
        getAllKeys(range) {
          const keys = Array.from(stores[name].keys()).filter(k => !range || (k >= range.lower && k <= range.upper));
          return request(keys);
        },
        getAll(range) {
          const rows = Array.from(stores[name].values()).filter(r => !range || (r.k >= range.lower && r.k <= range.upper));
          return request(rows);
        },
      });
      if (mode === 'readwrite') { setImmediate(() => { if (pending === 0) settle(); }); }
      return tx;
    },
  };
  return {
    stores, log,
    open() {
      const r = { result: db };
      setImmediate(() => { r.onupgradeneeded && r.onupgradeneeded({ target: { result: db } }); r.onsuccess && r.onsuccess(); });
      return r;
    },
  };
}

async function main() {
  const fake = makeIndexedDb();
  const context = {
    window: {}, indexedDB: fake, console, setTimeout, clearTimeout, setImmediate,
    URLSearchParams,
    IDBKeyRange: { bound: (lower, upper) => ({ lower, upper }) },
  };
  context.window.addEventListener = () => {};
  vm.createContext(context);
  vm.runInContext(SRC, context, { filename: 'torirs_idb.js' });
  const idb = context.window.ToriRS_IDB;

  const bytes = (n) => new Uint8Array([n & 0xff, (n >> 8) & 0xff]);

  /* 1. A put resolves at once; the row is readable before any commit. */
  await idb.groupPut('c', 8, 1, 0, bytes(1));
  assert.strictEqual(fake.log.transactions.filter(t => t.mode === 'readwrite').length, 0,
    'a put must not open a transaction on its own');
  const early = await idb.groupGet('c', 8, 1, 0);
  assert.deepStrictEqual(Array.from(early), [1, 0], 'a queued row answers a read');

  /* 2. Twenty-four puts, one commit (under the ceiling). */
  for (let i = 2; i <= 24; i++) { await idb.groupPut('c', 8, i, 0, bytes(i)); }
  await idb.flush();
  const rw = fake.log.transactions.filter(t => t.mode === 'readwrite');
  assert.strictEqual(rw.length, 1, `24 rows should commit in 1 transaction, saw ${rw.length}`);
  assert.strictEqual(rw[0].puts.length, 24);
  assert.strictEqual(fake.stores.groups.size, 24);

  /* 3. Past the row ceiling the flush does not wait for the timer, and no
   *    transaction ever carries more than the ceiling: a read created
   *    between two of them waits for at most one small commit. */
  await idb.groupGet('c', 8, 2, 0);   /* a client that is reading: small commits */
  for (let i = 100; i < 100 + 300; i++) { await idb.groupPut('c', 12, i, 0, bytes(i)); }
  await new Promise(r => setImmediate(r));
  await new Promise(r => setImmediate(r));
  const rw2 = fake.log.transactions.filter(t => t.mode === 'readwrite');
  assert.ok(rw2.length >= 2, 'a full queue commits without waiting for the timer');
  await idb.flush();
  assert.strictEqual(fake.stores.groups.size, 24 + 300);
  const biggest = Math.max(...fake.log.transactions.filter(t => t.mode === 'readwrite').map(t => t.puts.length));
  assert.ok(biggest <= 96, `no transaction past the ceiling, saw ${biggest}`);
  const commits = fake.log.transactions.filter(t => t.mode === 'readwrite').length - rw.length;
  assert.ok(commits >= 4, `300 rows need at least 4 commits at 96 rows, saw ${commits}`);

  /* 3b. Nobody reading for a while: the drain switches to big commits, so
   *     the queue a boot leaves behind is on disk in a few transactions. */
  await new Promise(r => setTimeout(r, 300));
  for (let i = 1000; i < 1000 + 500; i++) { await idb.groupPut('c', 12, i, 0, bytes(i)); }
  await idb.flush();
  const idleBiggest = Math.max(...fake.log.transactions.filter(t => t.mode === 'readwrite').map(t => t.puts.length));
  assert.ok(idleBiggest >= 400, `an idle drain commits big batches, saw at most ${idleBiggest}`);
  assert.strictEqual(fake.stores.groups.size, 24 + 300 + 500);

  /* 4. A row keyed like an earlier one replaces it in the queue, not beside it. */
  await idb.groupPut('c', 8, 1, 0, bytes(77));
  await idb.groupPut('c', 8, 1, 0, bytes(78));
  await idb.flush();
  const last = fake.log.transactions.filter(t => t.mode === 'readwrite').pop();
  assert.strictEqual(last.puts.length, 1, 'two puts of one key queue one row');
  assert.deepStrictEqual(Array.from(await idb.groupGet('c', 8, 1, 0)), [78, 0]);

  /* 5. The prefetch's two whole-table questions: which ids are stored, and
   *    every row at once -- both including what is still only queued. */
  await idb.groupPut('c', 12, 7777, 0, bytes(7));
  const present = await idb.groupIdsPresent('c', 12, 0);
  assert.ok(present.has(100) && present.has(399) && present.has(7777), 'stored and queued ids are present');
  assert.ok(!present.has(1), 'another table\'s id is not');
  const whole = await idb.groupGetAll('c', 12, 0);
  assert.strictEqual(whole.size, 300 + 500 + 1, `the whole table comes back at once, got ${whole.size}`);
  assert.deepStrictEqual(Array.from(whole.get(7777)), [7, 0]);
  await idb.flush();

  const stats = idb.writeStats();
  assert.strictEqual(stats.queued, 0);
  assert.strictEqual(stats.written, 24 + 300 + 500 + 1 + 1);
  console.log(`idb batch write: ok (${stats.written} rows in ${stats.flushes} transactions)`);
}

main().catch(err => { console.error(err); process.exit(1); });
