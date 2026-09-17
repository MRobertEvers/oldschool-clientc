/*
 * ToriRS_CreateHostIO().readFile -- the path plugin scripts, manifests and
 * assets take in a browser -- against a fake server and a fake IndexedDB.
 *
 * Node only, no browser or build step. The shipped file is evaluated as-is.
 *
 * The case this exists for: readFile used to answer from the database first
 * and never ask the server again, so a browser that had loaded a plugin once
 * kept running that text after the script was rewritten. On the rs289lc web
 * lane five V1-era scripts failed "must declare a non-empty V2 id" while
 * io_server was serving the V2 files.
 */
const assert = require('assert');
const fs = require('fs');
const path = require('path');

const SRC = fs.readFileSync(path.join(__dirname, '..', 'torirs_hostio.js'), 'utf8');

const PATH = 'script/plugins/loot_beam.lua';
const OLD = 'return { title = "v1" }';
const NEW = 'return { id = "loot-beam" }';

const bytes = (value) => new Uint8Array(Buffer.from(value, 'utf8'));
const text = (value) => (value === null ? null : Buffer.from(value).toString('utf8'));

function makeServer() {
  return {
    files: new Map(),       // URL -> { body, etag }
    down: false,
    requests: [],           // { url, ifNoneMatch }
    put(url, body, etag) { this.files.set(url, { body: bytes(body), etag }); },
    async fetch(url, options) {
      const ifNoneMatch = (options && options.headers && options.headers['If-None-Match']) || null;
      this.requests.push({ url, ifNoneMatch });
      if (this.down) { throw new Error('network down'); }
      const hit = this.files.get(url);
      const status = !hit ? 404 : (ifNoneMatch && ifNoneMatch === hit.etag ? 304 : 200);
      return {
        status,
        ok: status === 200,
        headers: { get: (name) => (String(name).toLowerCase() === 'etag' && hit ? hit.etag : null) },
        async arrayBuffer() {
          assert.strictEqual(status, 200, `status ${status} has no body`);
          return hit.body.buffer.slice(hit.body.byteOffset, hit.body.byteOffset + hit.body.byteLength);
        }
      };
    }
  };
}

function makeIdb() {
  return {
    rows: new Map(),
    async fileEntry(file) {
      const row = this.rows.get(file);
      return row ? { bytes: new Uint8Array(row.bytes), etag: row.etag } : null;
    },
    async fileGet(file) {
      const row = this.rows.get(file);
      return row ? new Uint8Array(row.bytes) : null;
    },
    async filePut(file, body, etag) { this.rows.set(file, { bytes: new Uint8Array(body), etag: etag || null }); }
  };
}

function hostio(server, idb) {
  const window = { ToriRS_IDB: idb };
  new Function('window', 'fetch', SRC)(window, server.fetch.bind(server));
  return window.ToriRS_CreateHostIO('osrs239', '/boot', null);
}

async function staleCopyIsReplaced() {
  const server = makeServer();
  const idb = makeIdb();
  idb.rows.set(PATH, { bytes: bytes(OLD), etag: '"v1"' });
  server.put(`/boot/${PATH}`, NEW, '"v2"');

  const io = hostio(server, idb);
  assert.strictEqual(text(await io.readFile(PATH)), NEW);
  assert.strictEqual(text(idb.rows.get(PATH).bytes), NEW);
  assert.strictEqual(idb.rows.get(PATH).etag, '"v2"');
  assert.strictEqual(server.requests[0].ifNoneMatch, '"v1"');
}

async function copyWithoutValidatorIsReplaced() {
  const server = makeServer();
  const idb = makeIdb();
  idb.rows.set(PATH, { bytes: bytes(OLD), etag: null });
  server.put(`/boot/${PATH}`, NEW, '"v2"');

  const io = hostio(server, idb);
  assert.strictEqual(text(await io.readFile(PATH)), NEW);
  assert.strictEqual(server.requests[0].ifNoneMatch, null);
}

async function unchangedIsOneRequestPerSession() {
  const server = makeServer();
  const idb = makeIdb();
  idb.rows.set(PATH, { bytes: bytes(NEW), etag: '"v2"' });
  server.put(`/boot/${PATH}`, NEW, '"v2"');

  const io = hostio(server, idb);
  for (let frame = 0; frame < 5; frame++) {
    assert.strictEqual(text(await io.readFile(PATH)), NEW);
  }
  assert.deepStrictEqual(server.requests, [{ url: `/boot/${PATH}`, ifNoneMatch: '"v2"' }]);
}

async function serverDownServesTheCopy() {
  const server = makeServer();
  const idb = makeIdb();
  idb.rows.set(PATH, { bytes: bytes(OLD), etag: '"v1"' });
  server.down = true;

  const io = hostio(server, idb);
  assert.strictEqual(text(await io.readFile(PATH)), OLD);
}

async function serverDownWithNoCopyIsAnOutage() {
  const server = makeServer();
  server.down = true;

  const io = hostio(server, makeIdb());
  await assert.rejects(io.readFile(PATH), (err) => err.torirsUnreachable === true);
}

async function deletedOnServerIsNotServedStale() {
  const server = makeServer();
  const idb = makeIdb();
  idb.rows.set(PATH, { bytes: bytes(OLD), etag: '"v1"' });

  const io = hostio(server, idb);
  assert.strictEqual(await io.readFile(PATH), null);
  assert.strictEqual(await io.readFile(PATH), null);
  assert.strictEqual(server.requests.length, 2, 'both routes once, then the denial holds');
}

async function main() {
  await staleCopyIsReplaced();
  await copyWithoutValidatorIsReplaced();
  await unchangedIsOneRequestPerSession();
  await serverDownServesTheCopy();
  await serverDownWithNoCopyIsAnOutage();
  await deletedOnServerIsNotServedStale();
  console.log('hostio readFile: ok');
}

main().catch(err => {
  console.error(err && err.stack || err);
  process.exitCode = 1;
});
