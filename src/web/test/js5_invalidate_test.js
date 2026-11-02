'use strict';
/*
 * A live reload's CACHE_INVALIDATE re-primes the web client's JS5 cache
 * (torirs_js5.js invalidateArchive, through torirs_hostio.js against a fake
 * IndexedDB) the way the native client's Js5ClientInvalidateArchive does:
 *
 *   - a fresh 255/255, then 255/<index> because its master entry moved, then
 *     ONLY the group whose CRC changed -- the store is overwritten with the
 *     new bytes and an untouched group is still answered from the store with
 *     no request at all;
 *   - a request of that index already on the wire when the reload lands is
 *     parked, the socket reopened, and the request answered with the NEW
 *     bytes, never the old answer still in flight; a read of the index made
 *     during the re-prime waits for it instead of reading the old store;
 *   - a second invalidate while the first re-prime is in flight runs after
 *     it, reads a master asked after the first finished, and leaves the
 *     store holding the latest bytes.
 *
 * The fake server behaves like js5_server: it serves from a snapshot of its
 * cache that is refreshed only when a 255/255 request arrives
 * (Js5ServerCacheLoad), so a rewrite is invisible until the client asks for
 * the master -- which is what makes the ordering above observable.
 *
 * Node 22+ (global WebSocket), nothing installed:
 *
 *   node src/web/test/js5_invalidate_test.js
 */
const net = require('net');
const crypto = require('crypto');
const fs = require('fs');
const path = require('path');
const assert = require('assert');

const GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';
const CONFIGS = 2;

function crc32(buf) {
  let c = 0xFFFFFFFF;
  for (const b of buf) {
    c ^= b;
    for (let k = 0; k < 8; k++) { c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1); }
  }
  return (c ^ 0xFFFFFFFF) >>> 0;
}

/* An uncompressed container: [0][size:u32][payload]. */
function container(payload) {
  const head = Buffer.alloc(5);
  head.writeUInt32BE(payload.length, 1);
  return Buffer.concat([head, Buffer.from(payload)]);
}

/*
 * The test's reference-table payload: [count:u16] then (id:u16, crc:u32) per
 * group. The client never parses it -- decodeReference (the executor's rscache
 * decode in the browser) is injected -- so a format of the test's own is
 * exactly what the seam allows.
 */
function referencePayload(groups) {
  const ids = Array.from(groups.keys()).sort((a, b) => a - b);
  const out = Buffer.alloc(2 + ids.length * 6);
  out.writeUInt16BE(ids.length, 0);
  ids.forEach((id, i) => {
    out.writeUInt16BE(id, 2 + i * 6);
    out.writeUInt32BE(crc32(groups.get(id)), 4 + i * 6);
  });
  return out;
}

function decodeReference(bytes) {
  const payload = Buffer.from(bytes).subarray(5);
  const listing = new Map();
  const count = payload.readUInt16BE(0);
  for (let i = 0; i < count; i++) {
    listing.set(payload.readUInt16BE(2 + i * 6), { crc: payload.readUInt32BE(4 + i * 6), version: 1 });
  }
  return listing;
}

/* A cache: table -> (group -> container). 255/<table> and 255/255 derived. */
class Cache {
  constructor() { this.tables = new Map(); }
  set(table, group, text) {
    if (!this.tables.has(table)) { this.tables.set(table, new Map()); }
    this.tables.get(table).set(group, container(Buffer.from(text)));
  }
  snapshot() {
    const files = new Map();
    const refs = new Map();
    for (const [table, groups] of this.tables) {
      for (const [group, bytes] of groups) { files.set(`${table}/${group}`, bytes); }
      const ref = container(referencePayload(groups));
      refs.set(table, ref);
      files.set(`255/${table}`, ref);
    }
    const top = Math.max(...this.tables.keys());
    const master = Buffer.alloc((top + 1) * 8);
    for (const [table, ref] of refs) {
      master.writeUInt32BE(crc32(ref), table * 8);
      master.writeUInt32BE(1, table * 8 + 4);
    }
    files.set('255/255', container(master));
    return files;
  }
}

function fakeServer(cache, { delayMs = 0 } = {}) {
  const state = { connections: 0, requests: [], served: cache.snapshot(), server: null, port: 0, delayMs };
  state.server = net.createServer(socket => {
    state.connections++;
    let buffered = Buffer.alloc(0);
    let upgraded = false;
    let shook = false;
    const send = payload => {
      if (socket.destroyed) { return; }
      const head = payload.length < 126
        ? Buffer.from([0x82, payload.length])
        : Buffer.from([0x82, 126, payload.length >> 8, payload.length & 0xff]);
      socket.write(Buffer.concat([head, payload]));
    };
    const onMessage = payload => {
      if (!shook) {
        assert.strictEqual(payload.length, 21);
        shook = true;
        send(Buffer.from([0]));
        return;
      }
      assert.strictEqual(payload.length, 4, 'a request is 4 bytes');
      const archive = payload[1];
      const group = (payload[2] << 8) | payload[3];
      const name = `${archive}/${group}`;
      state.requests.push(name);
      /* js5_server re-reads its cache on every master request. */
      if (name === '255/255') { state.served = cache.snapshot(); }
      const bytes = state.served.get(name);
      if (!bytes) { return; }
      assert.ok(bytes.length - 5 + 8 <= 512, 'test containers fit one block');
      const header = Buffer.from([archive, group >> 8, group & 0xff, 0, 0, 0, 0, 0]);
      header.writeUInt32BE(bytes.length - 5, 4);
      const answer = Buffer.concat([header, bytes.subarray(5)]);
      if (state.delayMs) { setTimeout(() => send(answer), state.delayMs); }
      else { send(answer); }
    };
    socket.on('data', chunk => {
      buffered = Buffer.concat([buffered, chunk]);
      if (!upgraded) {
        const end = buffered.indexOf('\r\n\r\n');
        if (end < 0) return;
        const request = buffered.subarray(0, end).toString();
        buffered = buffered.subarray(end + 4);
        const key = /Sec-WebSocket-Key: (.+)\r\n/i.exec(request)[1].trim();
        const accept = crypto.createHash('sha1').update(key + GUID).digest('base64');
        socket.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n' +
          `Connection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
        upgraded = true;
      }
      for (;;) {
        if (buffered.length < 2) return;
        const opcode = buffered[0] & 0x0f;
        let len = buffered[1] & 0x7f;
        let at = 2;
        if (len === 126) { len = buffered.readUInt16BE(2); at = 4; }
        const masked = (buffered[1] & 0x80) !== 0;
        const need = at + (masked ? 4 : 0) + len;
        if (buffered.length < need) return;
        let payload = buffered.subarray(at + (masked ? 4 : 0), need);
        if (masked) {
          const mask = buffered.subarray(at, at + 4);
          payload = Buffer.from(payload.map((b, i) => b ^ mask[i % 4]));
        }
        buffered = buffered.subarray(need);
        if (opcode === 8) { socket.end(); return; }
        if (opcode === 2) onMessage(payload);
      }
    });
    socket.on('error', () => {});
  });
  return new Promise(resolve => state.server.listen(0, '127.0.0.1', () => {
    state.port = state.server.address().port;
    resolve(state);
  }));
}

/* The groups store, as torirs_idb.js answers it. */
function fakeIdb() {
  const rows = new Map();
  const k = (c, t, a, f) => `${c}|${t}|${a}|${f | 0}`;
  return {
    rows,
    reads: 0,
    held(table, archive) {
      const row = rows.get(k('lane', table, archive, 0));
      return row ? Buffer.from(row).toString('latin1') : null;
    },
    async groupGet(c, t, a, f) { this.reads++; const row = rows.get(k(c, t, a, f)); return row ? new Uint8Array(row) : null; },
    async groupGetMany(c, t, ids, f) {
      const found = new Map();
      for (const id of ids) { const row = rows.get(k(c, t, id, f)); if (row) { found.set(id, new Uint8Array(row)); } }
      return found;
    },
    async groupIdsPresent(c, t, f) {
      const prefix = `${c}|${t}|`;
      const suffix = `|${f | 0}`;
      const out = new Set();
      for (const key of rows.keys()) {
        if (key.startsWith(prefix) && key.endsWith(suffix)) { out.add(parseInt(key.substring(prefix.length), 10)); }
      }
      return out;
    },
    async groupPut(c, t, a, f, bytes) { rows.set(k(c, t, a, f), new Uint8Array(bytes)); },
  };
}

function load(server) {
  const window = {};
  const dir = path.join(__dirname, '..');
  new Function('window', 'WebSocket', fs.readFileSync(path.join(dir, 'torirs_js5.js'), 'utf8'))(window, WebSocket);
  const idb = fakeIdb();
  window.ToriRS_IDB = idb;
  new Function('window', 'fetch', fs.readFileSync(path.join(dir, 'torirs_hostio.js'), 'utf8'))(window, null);
  const js5 = window.ToriRS_CreateJs5('127.0.0.1', server.port, 241, `ws://127.0.0.1:${server.port}/`);
  const host = window.ToriRS_CreateHostIO(() => 'lane', '/boot', { js5: () => js5 }, '/');
  return { js5, host, idb };
}

const text = bytes => (bytes ? Buffer.from(bytes).subarray(5).toString('latin1') : null);
const sleep = ms => new Promise(r => setTimeout(r, ms));

async function boot(host) {
  await host.readReferenceTable(CONFIGS);
  for (const g of [6, 9, 10]) { await host.readArchive(CONFIGS, g, 0); }
  await host.readArchive(3, 0, 0);
}

async function main() {
  let passed = 0;
  const cache = new Cache();
  cache.set(CONFIGS, 6, 'loc blueflower op3=null');
  cache.set(CONFIGS, 9, 'npc v1');
  cache.set(CONFIGS, 10, 'obj v1');
  cache.set(3, 0, 'interface v1');
  const server = await fakeServer(cache);
  const { js5, host, idb } = load(server);

  /* 0. Boot: the store holds what the client read. */
  await boot(host);
  assert.ok(idb.held(CONFIGS, 6).endsWith('op3=null'));
  assert.ok(idb.held(255, CONFIGS));
  passed++;

  /* 1. One group rewritten: 255/255, 255/2 and that group, nothing else. */
  {
    cache.set(CONFIGS, 6, 'loc blueflower op3=Smell');
    server.requests.length = 0;
    const lines = [];
    const done = await host.invalidateArchive(CONFIGS, 6, { decodeReference, log: l => lines.push(l) });
    assert.deepStrictEqual(server.requests, ['255/255', '255/2', '2/6'],
      `the re-prime asks the master, the table and the rewritten group only: ${server.requests}`);
    assert.deepStrictEqual(done.refetched, [6]);
    assert.ok(idb.held(CONFIGS, 6).endsWith('op3=Smell'), 'the store holds the new bytes');
    assert.strictEqual(text(await idb.groupGet('lane', 255, CONFIGS, 0)), text(cache.snapshot().get('255/2')),
      'and the new reference table');
    assert.strictEqual(lines.length, 3, lines.join('\n'));
    assert.match(lines[0], /^cache invalidate: js5 fetched 255\/255 \(\d+ bytes\)$/);
    assert.match(lines[1], /^cache invalidate: js5 fetched 255\/2 \(\d+ bytes\)$/);
    assert.match(lines[2], /^cache invalidate: js5 fetched 2\/6 \(\d+ bytes\)$/);
    passed++;

    /* An untouched group and the rewritten one are both local now. */
    server.requests.length = 0;
    assert.strictEqual(text(await host.readArchive(CONFIGS, 9, 0)), 'npc v1');
    assert.strictEqual(text(await host.readArchive(CONFIGS, 6, 0)), 'loc blueflower op3=Smell');
    assert.strictEqual(text(await host.readArchive(3, 0, 0)), 'interface v1');
    assert.ok(await host.readReferenceTable(CONFIGS));
    assert.deepStrictEqual(server.requests, [], `no request after the re-prime: ${server.requests}`);
    passed++;
  }

  /* 2. A request of the index on the wire when the reload lands: parked,
   *    the socket reopened, answered with the new bytes. A read during the
   *    re-prime waits for it rather than reading the old store. */
  {
    server.delayMs = 40;
    cache.set(CONFIGS, 9, 'npc v2');           /* not served until the next master */
    const connections = server.connections;
    server.requests.length = 0;
    const onWire = js5.group(CONFIGS, 9);       /* a producer miss, in flight */
    await sleep(10);
    assert.deepStrictEqual(server.requests, ['2/9'], 'the old request reached the server');
    const reload = host.invalidateArchive(CONFIGS, 9, { decodeReference });
    const during = host.readArchive(CONFIGS, 9, 0);
    const [answer, , read] = await Promise.all([onWire, reload, during]);
    assert.strictEqual(text(answer), 'npc v2', 'the in-flight request is answered after the re-prime, new bytes');
    assert.strictEqual(text(read), 'npc v2', 'a read during the re-prime waited and read the new store');
    assert.ok(server.connections > connections, 'the socket was reopened under the stale answer');
    assert.strictEqual(server.requests[1], '255/255', `the master went first on the new socket: ${server.requests}`);
    assert.ok(idb.held(CONFIGS, 9).endsWith('npc v2'));
    assert.strictEqual(js5.stats().failed, null);
    passed++;
  }

  /* 3. Two invalidates, the second while the first is in flight:
   *    serialised, and the store ends on the latest bytes. */
  {
    server.delayMs = 30;
    cache.set(CONFIGS, 10, 'obj v2');
    server.requests.length = 0;
    const first = host.invalidateArchive(CONFIGS, 10, { decodeReference });
    await sleep(5);
    cache.set(CONFIGS, 10, 'obj v3');
    const second = host.invalidateArchive(CONFIGS, 10, { decodeReference });
    const during = host.readArchive(CONFIGS, 10, 0);
    const [a, b, read] = await Promise.all([first, second, during]);
    assert.deepStrictEqual(server.requests, ['255/255', '255/2', '2/10', '255/255', '255/2', '2/10'],
      `one re-prime after the other: ${server.requests}`);
    assert.deepStrictEqual(a.refetched, [10]);
    assert.deepStrictEqual(b.refetched, [10]);
    assert.strictEqual(text(read), 'obj v3', 'a read queued behind both reads the latest');
    assert.ok(idb.held(CONFIGS, 10).endsWith('obj v3'), 'the store holds the latest bytes');
    server.requests.length = 0;
    server.delayMs = 0;
    assert.strictEqual(text(await host.readArchive(CONFIGS, 10, 0)), 'obj v3');
    assert.strictEqual(text(await host.readArchive(CONFIGS, 6, 0)), 'loc blueflower op3=Smell');
    assert.deepStrictEqual(server.requests, []);
    assert.strictEqual(js5.stats().reprimes, 4);
    passed++;
  }

  /* 4. Nothing changed: the stored table still matches the master, so only
   *    the master is asked and no group is fetched. */
  {
    server.requests.length = 0;
    const done = await host.invalidateArchive(CONFIGS, 6, { decodeReference });
    assert.deepStrictEqual(server.requests, ['255/255']);
    assert.deepStrictEqual(done.refetched, []);
    passed++;
  }

  /* 5. A table the decoder cannot read: every stored group is suspect and
   *    fetched again (and still correct). */
  {
    server.requests.length = 0;
    cache.set(CONFIGS, 9, 'npc v3');
    const done = await host.invalidateArchive(CONFIGS, 9, { decodeReference: () => null });
    assert.deepStrictEqual(done.refetched, [6, 9, 10]);
    assert.ok(idb.held(CONFIGS, 9).endsWith('npc v3'));
    passed++;
  }

  console.log(`js5_invalidate_test: ${passed} passed`);
  process.exit(0);
}

main().catch(err => { console.error(err); process.exit(1); });
