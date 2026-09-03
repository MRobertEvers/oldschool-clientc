/*
 * The asynchronous browser boot-file staging path, driven against a fake
 * server and a fake IndexedDB facade.
 *
 * Node only -- no browser, no wasm, no build step, matching chrome_dom_test.js.
 * The test deliberately lifts `boot` out of torirs_host.js instead of copying
 * its implementation: a rewrite of the shipped request/cache policy must also
 * change these expectations.
 *
 * Plugin scripts used to have a second, synchronous staging implementation in
 * this host. They no longer do. The platform IO executor now obtains plugin
 * manifests, scripts, and assets when the client asks for them; only the boot
 * manifest and its RevConfig files are staged into MEMFS here. A plugin-like
 * path is still useful for exercising fetch(), but load() must stage only the
 * files main() opens directly.
 */
const assert = require('assert');
const fs = require('fs');
const path = require('path');

const SRC = fs.readFileSync(path.join(__dirname, '..', 'torirs_host.js'), 'utf8');

/* ---- lift the shipped declarations out of the page IIFE ----------------- */

function declaration(needle) {
  const start = SRC.indexOf(needle);
  assert.notStrictEqual(start, -1, `torirs_host.js no longer contains: ${needle}`);
  let depth = 0;
  const firstBrace = SRC.indexOf('{', start);
  assert.notStrictEqual(firstBrace, -1, `torirs_host.js has no body after: ${needle}`);
  for (let i = firstBrace; i < SRC.length; i++) {
    if (SRC[i] === '{') { depth++; }
    else if (SRC[i] === '}') {
      depth--;
      if (depth === 0) { return `${SRC.substring(start, i + 1)};`; }
    }
  }
  assert.fail(`unbalanced braces after: ${needle}`);
}

const SOURCE = [
  "let cacheKey = '';",
  'let cacheRevision = 0;',
  declaration('function readBootIdentity('),
  declaration('const boot = ')
].join('\n');

/* ---- a fetch server, an IndexedDB facade, and MEMFS --------------------- */

function bytes(value) {
  if (value instanceof Uint8Array) { return new Uint8Array(value); }
  return new Uint8Array(Buffer.from(value, 'utf8'));
}

function text(value) {
  return value === null ? null : Buffer.from(value).toString('utf8');
}

function response(status, body, etag) {
  const payload = body && bytes(body);
  return {
    status,
    ok: status >= 200 && status < 300,
    headers: {
      get(name) { return String(name).toLowerCase() === 'etag' ? etag : null; }
    },
    async arrayBuffer() {
      assert(payload, `status ${status} has no response body`);
      return payload.buffer.slice(payload.byteOffset, payload.byteOffset + payload.byteLength);
    }
  };
}

function makeServer() {
  const server = {
    files: new Map(),       // URL -> { bytes, etag }
    down: false,            // both routes reject at the network layer
    requests: [],           // { url, ifNoneMatch }
    put(url, body, etag) {
      this.files.set(url, { bytes: bytes(body), etag: etag || null });
    },
    async fetch(url, options) {
      const headers = options && options.headers;
      const ifNoneMatch = headers && headers['If-None-Match'] || null;
      this.requests.push({ url, ifNoneMatch });
      if (this.down) { throw new Error('network down'); }

      const hit = this.files.get(url);
      if (!hit) { return response(404, null, null); }
      if (ifNoneMatch && ifNoneMatch === hit.etag) {
        return response(304, null, hit.etag);
      }
      return response(200, hit.bytes, hit.etag);
    }
  };
  return server;
}

function makeIdb() {
  return {
    bootFiles: new Map(),
    async bootGet(file) {
      const row = this.bootFiles.get(file);
      return row ? { bytes: bytes(row.bytes), etag: row.etag } : null;
    },
    async bootPut(file, body, etag) {
      this.bootFiles.set(file, { bytes: bytes(body), etag: etag || null });
    },
    put(file, body, etag) {
      this.bootFiles.set(file, { bytes: bytes(body), etag: etag || null });
    },
    get(file) { return this.bootFiles.get(file) || null; }
  };
}

function makeModule() {
  const files = new Map();
  const dirs = new Set();
  return {
    files,
    dirs,
    FS: {
      mkdir(dir) { dirs.add(dir); },
      writeFile(file, body) { files.set(file, bytes(body)); }
    }
  };
}

function harness(server, idb, args) {
  const Module = makeModule();
  const result = new Function(
    'ToriRS_IDB', 'bootUrl', 'fetch', 'Module', 'TextDecoder', 'args',
    `${SOURCE}\nreturn { boot, identity: () => ({ cacheKey, cacheRevision }) };`
  )(idb, '/boot', server.fetch.bind(server), Module, TextDecoder, args || []);
  result.Module = Module;
  return result;
}

/* ---- fetch() ------------------------------------------------------------ */

const PLUGIN_PATH = 'script/plugins/tile_indicator.lua';
const OLD = 'draw.tile(x, z, level, colour, fill)';
const NEW = 'draw.tile(x, z, level, colour, fill_colour, alpha)';

async function firstFetch() {
  const server = makeServer();
  const idb = makeIdb();
  server.put(`/boot/${PLUGIN_PATH}`, NEW, '"v1"');

  const { boot, Module } = harness(server, idb);
  assert.strictEqual(text(await boot.fetch(PLUGIN_PATH)), NEW);
  assert.strictEqual(text(idb.get(PLUGIN_PATH).bytes), NEW);
  assert.strictEqual(idb.get(PLUGIN_PATH).etag, '"v1"');
  assert.strictEqual(text(Module.files.get(`/${PLUGIN_PATH}`)), NEW);
  assert.deepStrictEqual(boot.loaded, [PLUGIN_PATH]);
  assert.deepStrictEqual(server.requests, [
    { url: `/boot/${PLUGIN_PATH}`, ifNoneMatch: null }
  ]);
}

async function validatorAnd304() {
  const server = makeServer();
  const idb = makeIdb();
  idb.put(PLUGIN_PATH, NEW, '"v1"');
  server.put(`/boot/${PLUGIN_PATH}`, NEW, '"v1"');

  const { boot, Module } = harness(server, idb);
  assert.strictEqual(text(await boot.fetch(PLUGIN_PATH)), NEW);
  assert.strictEqual(boot.revalidated, 1);
  assert.strictEqual(boot.offline, 0);
  assert.deepStrictEqual(boot.loaded, [`${PLUGIN_PATH} (unchanged)`]);
  assert.strictEqual(text(Module.files.get(`/${PLUGIN_PATH}`)), NEW);
  assert.deepStrictEqual(server.requests, [
    { url: `/boot/${PLUGIN_PATH}`, ifNoneMatch: '"v1"' }
  ]);
}

async function changedUnderneath() {
  const server = makeServer();
  const idb = makeIdb();
  idb.put(PLUGIN_PATH, OLD, '"v1"');
  server.put(`/boot/${PLUGIN_PATH}`, NEW, '"v2"');

  const { boot } = harness(server, idb);
  assert.strictEqual(text(await boot.fetch(PLUGIN_PATH)), NEW);
  assert.strictEqual(text(idb.get(PLUGIN_PATH).bytes), NEW);
  assert.strictEqual(idb.get(PLUGIN_PATH).etag, '"v2"');
  assert.deepStrictEqual(boot.loaded, [`${PLUGIN_PATH} (changed)`]);
  assert.strictEqual(server.requests[0].ifNoneMatch, '"v1"');
}

async function cachedWithoutValidator() {
  const server = makeServer();
  const idb = makeIdb();
  idb.put(PLUGIN_PATH, OLD, null);
  server.put(`/boot/${PLUGIN_PATH}`, NEW, '"v2"');

  const { boot } = harness(server, idb);
  assert.strictEqual(text(await boot.fetch(PLUGIN_PATH)), NEW);
  assert.strictEqual(server.requests[0].ifNoneMatch, null);
}

async function offlineCached() {
  const server = makeServer();
  const idb = makeIdb();
  idb.put(PLUGIN_PATH, OLD, '"v1"');
  server.down = true;

  const { boot, Module } = harness(server, idb);
  assert.strictEqual(text(await boot.fetch(PLUGIN_PATH)), OLD);
  assert.strictEqual(text(Module.files.get(`/${PLUGIN_PATH}`)), OLD);
  assert.strictEqual(boot.offline, 1);
  assert.deepStrictEqual(boot.missing, []);
  assert.deepStrictEqual(boot.loaded, [`${PLUGIN_PATH} (offline copy)`]);
  assert.deepStrictEqual(server.requests.map(r => r.url), [
    `/boot/${PLUGIN_PATH}`, `/${PLUGIN_PATH}`
  ]);
}

async function missingEverywhere() {
  const server = makeServer();
  const idb = makeIdb();
  server.down = true;

  const { boot, Module } = harness(server, idb);
  assert.strictEqual(await boot.fetch(PLUGIN_PATH), null);
  assert.strictEqual(Module.files.has(`/${PLUGIN_PATH}`), false);
  assert.deepStrictEqual(boot.loaded, []);
  assert.deepStrictEqual(boot.missing, [PLUGIN_PATH]);
}

async function fallsBackToBarePath() {
  const server = makeServer();
  const idb = makeIdb();
  idb.put(PLUGIN_PATH, OLD, '"v1"');
  server.put(`/${PLUGIN_PATH}`, NEW, '"v2"');

  const { boot } = harness(server, idb);
  assert.strictEqual(text(await boot.fetch(PLUGIN_PATH)), NEW);
  assert.deepStrictEqual(server.requests, [
    { url: `/boot/${PLUGIN_PATH}`, ifNoneMatch: '"v1"' },
    { url: `/${PLUGIN_PATH}`, ifNoneMatch: '"v1"' }
  ]);
}

/* ---- load(): boot manifest and every file it names ---------------------- */

async function loadManifestAndRevconfigs() {
  const server = makeServer();
  const idb = makeIdb();
  const manifestPath = 'manifests/test.ini';
  const manifest = [
    '[cache:boot]',
    'dir=osrs239',
    'revision=239',
    'revconfig_ui=../revconfig/test_ui.ini',
    'revconfig_cache=../revconfig/test_cache.ini',
    '',
    '[client:args]',
    'arg=--revconfig',
    'arg=client/embedded_ui.ini',
    'arg=--pass',
    'arg=a value that is not a file',
    '',
    '[plugins]',
    'manifest=script/plugins/plugins.ini'
  ].join('\n');

  const wanted = new Map([
    [manifestPath, manifest],
    ['manifests/../revconfig/test_ui.ini', 'ui'],
    ['manifests/../revconfig/test_cache.ini', 'cache'],
    ['client/embedded_ui.ini', 'embedded ui'],
    ['client/embedded_cache.ini', 'embedded cache'],
    ['client/outer_cache.ini', 'outer cache']
  ]);
  for (const [file, body] of wanted) {
    server.put(`/boot/${file}`, body, `"${file}"`);
  }

  const args = [
    '--manifest', manifestPath,
    '--revconfig-cache', 'client/outer_cache.ini'
  ];
  const { boot, Module, identity } = harness(server, idb, args);
  await boot.load();

  assert.deepStrictEqual(identity(), { cacheKey: 'osrs239', cacheRevision: 239 });
  for (const [file, body] of wanted) {
    assert.strictEqual(text(Module.files.get(`/${file}`)), body, `${file} was not staged`);
    assert.strictEqual(text(idb.get(file).bytes), body, `${file} was not cached`);
  }
  assert.deepStrictEqual(boot.missing, []);
  assert.strictEqual(boot.loaded.length, wanted.size);

  /* Plugin files are intentionally not pre-staged by boot.load. Their reads
   * travel through torirs_hostio.js and the platform IO executor. */
  assert.strictEqual(
    server.requests.some(r => r.url.includes('script/plugins/plugins.ini')),
    false
  );
}

async function main() {
  await firstFetch();
  await validatorAnd304();
  await changedUnderneath();
  await cachedWithoutValidator();
  await offlineCached();
  await missingEverywhere();
  await fallsBackToBarePath();
  await loadManifestAndRevconfigs();
  console.log('host boot files: ok');
}

main().catch(err => {
  console.error(err && err.stack || err);
  process.exitCode = 1;
});
