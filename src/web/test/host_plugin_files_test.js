/*
 * The IndexedDB build's plugin-file staging, driven against a fake server.
 *
 * Node only -- no browser, no wasm, no build step, matching chrome_dom_test.js.
 * What it pins is the one thing the browser lane gets wrong invisibly: a plugin
 * script stored in the record database outlives the BINARY it was stored
 * against. tile_indicator's draw.tile grew a sixth argument, the module was
 * rebuilt, and the store kept handing back the five-argument script -- so every
 * frame died in the new binding with "bad argument #6 to 'tile'" while the file
 * on disk had been correct the whole time.
 *
 * So the assertions here are about WHEN a request goes out, not about Lua: a
 * held script must be offered to the server as a validator, a changed one must
 * win, and a server that answers nothing must not cost the page its plugins.
 *
 * `xhrSync` and `plugins` are lifted out of torirs_host.js by source text
 * rather than reimplemented -- the file is one IIFE that boots a whole page on
 * load, and a copy of the logic would pass while the shipped copy regressed.
 */
var assert = require('assert');
var fs = require('fs');
var path = require('path');

var SRC = fs.readFileSync(path.join(__dirname, '..', 'torirs_host.js'), 'utf8');

/* ---- lifting a declaration out of the IIFE -------------------------------- */

// From `needle` to the brace that closes it. Counting braces is enough here:
// neither declaration contains a brace inside a string or a regex literal, and
// a change that put one there would fail loudly rather than silently.
function declaration(needle) {
  var start = SRC.indexOf(needle);
  assert.notStrictEqual(start, -1, 'torirs_host.js no longer contains: ' + needle);
  var depth = 0;
  for (var i = SRC.indexOf('{', start); i < SRC.length; i++) {
    if (SRC[i] === '{') { depth++; }
    else if (SRC[i] === '}') {
      depth--;
      if (depth === 0) { return SRC.substring(start, i + 1) + ';'; }
    }
  }
  assert.fail('unbalanced braces after: ' + needle);
}

var SOURCE = [
  declaration('function xhrSync('),
  declaration('var plugins = ')
].join('\n');

/* ---- a server, and a store ------------------------------------------------ */

function makeServer() {
  var server = {
    files: {},          // url path -> {body, etag}
    down: false,        // nothing answers, on any route
    requests: [],       // {url, ifNoneMatch}
    put: function (p, body, etag) { this.files[p] = { body: body, etag: etag }; }
  };
  server.XMLHttpRequest = function () {
    var self = this;
    this.status = 0;
    this.responseText = '';
    this._headers = {};
    this.open = function (method, url) { self._url = url; };
    this.overrideMimeType = function () {};
    this.setRequestHeader = function (k, v) { self._headers[k] = v; };
    this.getResponseHeader = function (k) {
      return k === 'ETag' ? (self._etag || null) : null;
    };
    this.send = function () {
      var tag = self._headers['If-None-Match'] || null;
      server.requests.push({ url: self._url, ifNoneMatch: tag });
      if (server.down) { throw new Error('network down'); }
      var hit = server.files[self._url];
      if (!hit) { self.status = 404; return; }
      if (tag && tag === hit.etag) { self.status = 304; return; }
      self.status = 200;
      self._etag = hit.etag;
      self.responseText = hit.body;
    };
  };
  return server;
}

function makeStore() {
  return {
    files: new Map(),
    etags: new Map(),
    fileGet: function (p) {
      var b = this.files.get(p);
      return b === undefined ? null : b;
    },
    fileEtag: function (p) { return this.etags.get(p) || null; },
    filePut: function (p, bytes, etag) {
      this.files.set(p, bytes);
      this.etags.set(p, etag || null);
      return true;
    }
  };
}

// A fresh `plugins`, wired to one server and one store. SCRIPT_ROOT and the
// manifest path are the file's own constants, re-declared here because they
// mirror C constants and a test that invented its own would not notice a drift.
function harness(server, store) {
  return new Function(
    'store', 'bootUrl', 'SCRIPT_ROOT', 'PLUGIN_MANIFEST_PATH',
    'XMLHttpRequest', 'TextDecoder',
    SOURCE + '\nreturn plugins;'
  )(store, '/boot', 'script', 'plugins/plugins.ini',
    server.XMLHttpRequest, TextDecoder);
}

function text(bytes) {
  return bytes === null ? null : Buffer.from(bytes).toString('latin1');
}

/* ---- take() --------------------------------------------------------------- */

var PATH = 'script/plugins/tile_indicator.lua';
var OLD = 'draw.tile(x, z, level, colour, fill)';
var NEW = 'draw.tile(x, z, level, colour, fill_colour, alpha)';

// Nothing held: one unconditional request, and the answer is kept with its
// validator so the next boot can ask about it.
(function firstRun() {
  var server = makeServer();
  var store = makeStore();
  server.put('/boot/' + PATH, NEW, '"v1"');

  var plugins = harness(server, store);
  assert.strictEqual(text(plugins.take(PATH)), NEW);
  assert.strictEqual(plugins.fetched, 1);
  assert.strictEqual(plugins.stored, 0);
  assert.strictEqual(server.requests.length, 1);
  assert.strictEqual(server.requests[0].ifNoneMatch, null);
  assert.strictEqual(store.fileEtag(PATH), '"v1"');
})();

// Held and unchanged: the validator goes out, a 304 comes back, and no body
// crosses. This is the case that has to stay cheap.
(function unchanged() {
  var server = makeServer();
  var store = makeStore();
  server.put('/boot/' + PATH, NEW, '"v1"');
  store.filePut(PATH, Buffer.from(NEW, 'latin1'), '"v1"');

  var plugins = harness(server, store);
  assert.strictEqual(text(plugins.take(PATH)), NEW);
  assert.strictEqual(plugins.stored, 1);
  assert.strictEqual(plugins.fetched, 0);
  assert.strictEqual(server.requests.length, 1);
  assert.strictEqual(server.requests[0].ifNoneMatch, '"v1"');
})();

// THE REGRESSION. The store holds the script the old binary was built against;
// disk holds the one the new binary needs. Held-until-missing never asked, and
// the page ran the five-argument call into the six-argument binding forever.
(function changedUnderneath() {
  var server = makeServer();
  var store = makeStore();
  server.put('/boot/' + PATH, NEW, '"v2"');
  store.filePut(PATH, Buffer.from(OLD, 'latin1'), '"v1"');

  var plugins = harness(server, store);
  assert.strictEqual(text(plugins.take(PATH)), NEW);
  assert.strictEqual(plugins.fetched, 1);
  assert.strictEqual(store.fileEtag(PATH), '"v2"');
  assert.strictEqual(text(store.fileGet(PATH)), NEW);
})();

// A row written before files carried a validator. It has no etag, so the
// request must go out unconditional -- otherwise the copy stored back when
// nothing revalidated it would be the one copy that can never be replaced.
(function heldWithoutValidator() {
  var server = makeServer();
  var store = makeStore();
  server.put('/boot/' + PATH, NEW, '"v2"');
  store.filePut(PATH, Buffer.from(OLD, 'latin1'));

  var plugins = harness(server, store);
  assert.strictEqual(text(plugins.take(PATH)), NEW);
  assert.strictEqual(server.requests[0].ifNoneMatch, null);
})();

// No server at all. A page that ran plugins yesterday runs them today: the
// stored copy is the answer, and nothing is reported missing.
(function serverGone() {
  var server = makeServer();
  var store = makeStore();
  store.filePut(PATH, Buffer.from(OLD, 'latin1'), '"v1"');
  server.down = true;

  var plugins = harness(server, store);
  assert.strictEqual(text(plugins.take(PATH)), OLD);
  assert.strictEqual(plugins.stored, 1);
  assert.deepStrictEqual(plugins.missing, []);
})();

// No server and nothing held is the only shape that is actually missing.
(function serverGoneAndNothingHeld() {
  var server = makeServer();
  var store = makeStore();
  server.down = true;

  var plugins = harness(server, store);
  assert.strictEqual(plugins.take(PATH), null);
  assert.deepStrictEqual(plugins.missing, [PATH]);
})();

// The bare path is the second route, for a page served by something that is
// not the IO server. It carries the validator too.
(function fallsBackToTheBarePath() {
  var server = makeServer();
  var store = makeStore();
  server.put('/' + PATH, NEW, '"v2"');
  store.filePut(PATH, Buffer.from(OLD, 'latin1'), '"v1"');

  var plugins = harness(server, store);
  assert.strictEqual(text(plugins.take(PATH)), NEW);
  assert.strictEqual(server.requests.length, 2);
  assert.strictEqual(server.requests[0].url, '/boot/' + PATH);
  assert.strictEqual(server.requests[1].url, '/' + PATH);
  assert.strictEqual(server.requests[1].ifNoneMatch, '"v1"');
})();

/* ---- load() --------------------------------------------------------------- */

// Every script the manifest names is revalidated, not just newly added ones.
(function loadRevalidatesEveryScript() {
  var server = makeServer();
  var store = makeStore();
  var manifest = '[tile-indicator-lua]\nsource=plugins/tile_indicator.lua\n' +
                 'enabled=1\n\n[lootbeam]\nsource=plugins/lootbeam.lua\nenabled=0\n';
  server.put('/boot/script/plugins/plugins.ini', manifest, '"m1"');
  server.put('/boot/' + PATH, NEW, '"v2"');
  server.put('/boot/script/plugins/lootbeam.lua', 'return {}', '"l1"');
  store.filePut(PATH, Buffer.from(OLD, 'latin1'), '"v1"');
  store.filePut('script/plugins/lootbeam.lua', Buffer.from('return {}', 'latin1'), '"l1"');

  var plugins = harness(server, store);
  plugins.load();

  // The stale one replaced, the current one revalidated in a header.
  assert.strictEqual(text(store.fileGet(PATH)), NEW);
  assert.strictEqual(plugins.fetched, 1);
  assert.strictEqual(plugins.stored, 1);
  assert.deepStrictEqual(plugins.missing, []);
  // A disabled plugin is still staged: the switch is the user's, and it has to
  // be able to come back without a reload.
  assert.ok(server.requests.some(function (r) {
    return r.url === '/boot/script/plugins/lootbeam.lua';
  }));
})();

console.log('host plugin files: ok');
