// Host harness for the emscripten build of src/.
//
// Three things a browser has to supply that a desktop host gets for free:
//
//   1. Boot parameters. main() still parses argv and reads getenv, so the page
//      turns its query string into Module.arguments and ENV rather than
//      inventing a second configuration path.
//
//   2. The files main() opens by name. A manifest is named on the command
//      line, which here means the query string — so it cannot be decided at
//      link time. They are fetched from the IO server's /boot/ route into the
//      virtual filesystem before main() runs, which is why any manifest works
//      against any build.
//
//   3. The IO pump. The client has no disk: its cache reads pile up in wasm
//      memory as an encoded batch and stay there until something carries them
//      to the IO server.
//
// Load this BEFORE torirs.js: it defines the Module object the runtime reads.
//
// Query string — the page's command line:
//   ?arg=--manifest&arg=manifests/manifest_rs254.ini&arg=--offline   one arg per param
//   ?args=--manifest,manifests/manifest_rs254.ini,--offline          same, comma-joined
//   ?env=TORIRS_TASK_LOG=1&env=TORIRS_NET_DEBUG=1          environment
//   ?io=http://host:port/io                                IO endpoint (/io)
//   ?io_sync=0                                             see "Pumping" below
//
// Repeated `arg=` is the form run-live.sh generates and the one to prefer:
// each value is percent-encoded on its own, so an argument may contain a comma,
// a space or an `&` — a password, a TORIRS_NET_CHEAT string. The comma-joined
// `args=` form stays because it is far easier to type by hand.
//
// ## Pumping
//
// A task pipeline is serial: it issues a read, parks, and cannot resume until
// the answer lands. If the answer only arrives on a later turn of the event
// loop, a frame can satisfy exactly one read — and a boot that reads several
// hundred archives then takes several hundred frames, while the client's 20ms
// logic ticks keep queueing more work behind them.
//
// So the boot's pump is synchronous, and runs from inside the client's
// PlatformX_IO_Process: requests go out and data comes back before Process
// returns, exactly as the native backend behaves. A boot then costs a handful
// of frames rather than hundreds. The cost is a blocked main thread while it
// happens, which is why the IO log below reports what each frame spent.
//
// A live client pays that cost for nothing. A synchronous XMLHttpRequest
// freezes the main thread for much longer than the request takes (measured on
// localhost: 4.4ms average, 17ms worst, against a 3.55ms round trip), and the
// reads a live client still issues are the ones that coincide with something
// new on screen — the first play of an npc's hit sound is a fetch on the frame
// its hitsplat is drawn. So the client stops asking for the blocking pump once
// it reaches APP_STATE_READY (PlatformXIO_Web_SetBlockingReads) and this file's
// frame-gated fetch carries the batch instead. Both paths run in one session;
// pump() must therefore always be willing to deliver.
//
// ?io_sync=0 declines the blocking pump outright, boot included: everything is
// frame-gated fetch. The client supports both — PlatformX_IO_Pending is what
// tells its scheduler whether a read is still outstanding.

(function () {
  'use strict';

  var params = new URLSearchParams(window.location.search);

  var DEFAULT_ARGS = ['--manifest', 'manifests/manifest_rs254.ini', '--offline'];

  function readArgs() {
    var repeated = params.getAll('arg');
    if (repeated.length > 0) { return repeated; }
    if (params.has('args')) {
      return params.get('args').split(',').filter(function (s) { return s.length > 0; });
    }
    return DEFAULT_ARGS;
  }

  // Accepts both `env=A=1&env=B=2` and the single `env=A=1;B=2` form. Splitting
  // on the FIRST `=` only, so a value may itself contain one.
  function readEnv() {
    var out = [];
    params.getAll('env').forEach(function (group) {
      group.split(';').forEach(function (pair) {
        var eq = pair.indexOf('=');
        if (eq > 0) { out.push([pair.substring(0, eq), pair.substring(eq + 1)]); }
      });
    });
    return out;
  }

  var args = readArgs();
  var ioUrl = params.get('io') || '/io';
  var bootUrl = ioUrl.replace(/\/io$/, '/boot');
  var statsUrl = ioUrl.replace(/\/io$/, '/stats');
  var useSync = params.get('io_sync') !== '0';

  // Where the IndexedDB build reaches JS5. Defaults to this page's host, which
  // is right when the client and the cache server are served from one machine
  // (the usual local setup) and overridable when they are not.
  var js5Host = params.get('js5_host') || window.location.hostname || 'localhost';
  var js5Port = parseInt(params.get('js5_port') || '43594', 10);
  var cacheReset = params.get('cache_reset') === '1';

  var FRAME_MS = 1000 / 60;

  // ------------------------------------------------------------------- log

  var logEl = null;
  var statusEl = null;
  var ioLogEl = null;
  var logLines = [];
  var LOG_MAX = 400;

  function log(line, isError) {
    if (isError) { console.error(line); } else { console.log(line); }
    logLines.push(line);
    if (logLines.length > LOG_MAX) { logLines.shift(); }
    if (!logEl) { logEl = document.getElementById('log'); }
    if (logEl) {
      logEl.textContent = logLines.join('\n');
      logEl.scrollTop = logEl.scrollHeight;
    }
  }

  function setStatus(text) {
    if (!statusEl) { statusEl = document.getElementById('status'); }
    if (statusEl) { statusEl.textContent = text; }
  }

  function kb(bytes) {
    if (bytes >= 1048576) { return (bytes / 1048576).toFixed(1) + 'MB'; }
    return (bytes / 1024).toFixed(1) + 'KB';
  }

  // ---------------------------------------------------- synchronous transfer
  //
  // A synchronous XHR may not set responseType, so binary comes back as a
  // string with one character per byte — hence the x-user-defined override,
  // which keeps bytes 0x80-0xFF from being mangled into replacement chars.

  function xhrSyncBytes(method, url, body) {
    var result = xhrSync(method, url, body, null);
    return result.status === 200 ? result.bytes : null;
  }

  // The same transfer, but reporting enough to do a conditional request:
  // {status, bytes, etag}. status 0 means the request never reached anyone,
  // which is a different thing from a 404 and has a different answer.
  function xhrSync(method, url, body, etag) {
    var xhr = new XMLHttpRequest();
    try {
      xhr.open(method, url, false);
      xhr.overrideMimeType('text/plain; charset=x-user-defined');
      if (body) { xhr.setRequestHeader('Content-Type', 'application/octet-stream'); }
      // Asks the server "still this one?". A 304 comes back with no body, so
      // the copy already held is the answer.
      if (etag) { xhr.setRequestHeader('If-None-Match', etag); }
      xhr.send(body || null);
    } catch (err) {
      return { status: 0, bytes: null, etag: null, error: err.message };
    }
    if (xhr.status === 304) {
      return { status: 304, bytes: null, etag: etag };
    }
    if (xhr.status !== 200) {
      return { status: xhr.status, bytes: null, etag: null };
    }
    var text = xhr.responseText;
    var out = new Uint8Array(text.length);
    for (var i = 0; i < text.length; i++) { out[i] = text.charCodeAt(i) & 0xFF; }
    var tag = null;
    try { tag = xhr.getResponseHeader('ETag'); } catch (err) { /* not exposed */ }
    return { status: 200, bytes: out, etag: tag };
  }

  // ------------------------------------------------------------ boot files
  //
  // The files main() opens with fopen: the manifest named on the command line,
  // and the RevConfig INIs that manifest names. Fetched into the virtual
  // filesystem at the same relative paths the client will use, because
  // BootManifest resolves its values against the manifest's own directory.

  var boot = {
    loaded: [],
    missing: [],
    /** Files the server confirmed unchanged (304), and files served from the
     *  stored copy because nothing answered. */
    revalidated: 0,
    offline: 0,

    write: function (path, bytes) {
      var parts = path.split('/').filter(function (s) { return s.length > 0; });
      var dir = '';
      for (var i = 0; i < parts.length - 1; i++) {
        dir += '/' + parts[i];
        try { Module.FS.mkdir(dir); } catch (err) { /* already there */ }
      }
      Module.FS.writeFile('/' + parts.join('/'), bytes);
    },

    /*
     * Fetch one boot file, revalidating rather than re-downloading.
     *
     * These are the client's configuration — the manifest and the RevConfig
     * INIs it names — and they are edited by hand between runs, so a copy held
     * from last time can never simply be trusted. Nor should it be thrown away:
     * a conditional request asks the server "still this one?" and a 304 answers
     * it in a header instead of a body.
     *
     * Three outcomes, and the third is the reason any of this exists:
     *
     *   200  the file changed (or is new) — take it and remember the validator
     *   304  unchanged — use the stored copy, no body transferred
     *   0    nothing answered — the server is gone, so use the stored copy and
     *        say so. A page whose config server is down still boots from what
     *        it fetched last time, instead of failing on a file it has.
     *
     * The IO server's /boot/ route is tried first and the bare path second: the
     * IndexedDB build has no IO server, and its page may be served by anything
     * that hands out files.
     */
    fetch: function (path) {
      var cached = store.bootGet(path);
      var result = xhrSync('GET', bootUrl + '/' + path, null, cached && cached.etag);
      var via = '/boot/';

      if (result.status === 404 || result.status === 0) {
        var direct = xhrSync('GET', '/' + path, null, cached && cached.etag);
        // Prefer whichever route actually said something about the file.
        if (direct.status === 200 || direct.status === 304) {
          result = direct;
          via = '/';
        } else if (result.status === 0 && direct.status !== 0) {
          result = direct;
          via = '/';
        }
      }

      if (result.status === 200) {
        store.bootPut(path, result.bytes, result.etag);
        this.write(path, result.bytes);
        this.loaded.push(path + (cached ? ' (changed)' : ''));
        return result.bytes;
      }
      if (result.status === 304 && cached) {
        this.write(path, cached.bytes);
        this.loaded.push(path + ' (unchanged)');
        this.revalidated++;
        return cached.bytes;
      }
      if (cached) {
        // Unreachable, or the server no longer has it. Either way this page has
        // a copy and refusing to boot would help nobody.
        this.write(path, cached.bytes);
        this.loaded.push(path + ' (offline copy)');
        this.offline++;
        return cached.bytes;
      }
      this.missing.push(path);
      return null;
    },

    // `revconfig_ui` / `revconfig_cache` out of an INI, comments stripped.
    revconfigPaths: function (bytes) {
      var text = new TextDecoder('utf-8', { fatal: false }).decode(bytes);
      var out = [];
      text.split('\n').forEach(function (line) {
        line = line.split(';')[0].split('#')[0].trim();
        var eq = line.indexOf('=');
        if (eq < 0) { return; }
        var key = line.substring(0, eq).trim();
        if (key === 'revconfig_ui' || key === 'revconfig_cache') {
          out.push(line.substring(eq + 1).trim());
        }
      });
      return out;
    },

    // `[client:args]` is intentionally one exact argv token per `arg=` line,
    // matching the repeated query-string form above. Do not trim or interpret
    // quotes/comments: the C INI reader does neither for a value.
    clientArgs: function (bytes) {
      var text = new TextDecoder('utf-8', { fatal: false }).decode(bytes);
      var out = [];
      var section = '';
      text.split('\n').forEach(function (raw) {
        var line = raw.replace(/\r$/, '');
        // The C reader skips indentation before each element, but preserves
        // everything after `=` as the value. Use a separate structural view
        // so leading whitespace is ignored without altering the argv token.
        var structural = line.replace(/^[ \t]+/, '');
        if (structural.charAt(0) === '[') {
          var close = structural.indexOf(']');
          if (close < 0) { return; }
          section = structural.substring(1, close);
          return;
        }
        if (section !== 'client:args') { return; }
        if (structural.substring(0, 4) !== 'arg=') { return; }
        out.push(structural.substring(4));
      });
      return out;
    },

    load: function () {
      var self = this;
      var seen = {};

      function take(path) {
        if (!path || path.charAt(0) === '/' || seen[path]) { return null; }
        seen[path] = true;
        return self.fetch(path);
      }

      function argumentTakesValue(argument) {
        return argument === '--manifest' || argument === '--port' ||
          argument === '--revconfig' || argument === '--revconfig-cache' ||
          argument === '--connect' || argument === '--user' ||
          argument === '--pass' || argument === '--rev' ||
          argument === '--windowmode' || argument === '--window';
      }

      function takeArgFiles(argv) {
        for (var j = 0; j < argv.length; j++) {
          var flag = argv[j];
          var value = argv[j + 1];
          if ((flag === '--revconfig' || flag === '--revconfig-cache') &&
              j + 1 < argv.length) {
            take(value);
            // main.c probes for a sibling _cache.ini when only the _ui.ini was
            // given; fetch it so the probe finds it or honestly does not.
            if (/_ui\.ini$/.test(value)) { take(value.replace(/_ui\.ini$/, '_cache.ini')); }
          }
          if (argumentTakesValue(flag) && j + 1 < argv.length) { j++; }
        }
      }

      for (var i = 0; i < args.length; i++) {
        var flag = args[i];
        var value = args[i + 1];
        if (flag === '--manifest' && i + 1 < args.length) {
          var bytes = take(value);
          if (bytes) {
            var dir = value.indexOf('/') >= 0 ? value.replace(/\/[^/]*$/, '/') : '';
            self.revconfigPaths(bytes).forEach(function (rel) { take(dir + rel); });
            // Additional CLI paths keep CLI/CWD semantics; unlike typed
            // revconfig keys they are not relative to the manifest directory.
            takeArgFiles(self.clientArgs(bytes));
          }
          break; // main.c bootstraps from the first real --manifest option.
        }
        if (argumentTakesValue(flag) && i + 1 < args.length) { i++; }
      }
      takeArgFiles(args);
    }
  };

  // ----------------------------------------------------------- plugin files
  //
  // The plugin manifest and the Lua scripts it names.
  //
  // NOT boot files, and they must not become any: those are staged into MEMFS,
  // which forgets everything when the tab closes and holds nothing a second
  // run can revalidate against. These go into the RECORD DATABASE beside the
  // player's saved options, and the client reads them back through the IO
  // queue -- TORIRS_IOK_SCRIPT, answered by platform_x_io.c out of the store
  // (see read_script_item there). Nothing is baked into the module and nothing
  // is opened with fopen.
  //
  // Fetched only on a miss. A script the database already holds is used as it
  // stands, so a second run installs nothing and a page whose file server is
  // gone still starts every plugin it ran last time.
  //
  // WHY THIS EXISTS: the wire build never needed it -- a script request
  // crosses to io_server, which reads them off real disk -- but the IndexedDB
  // build links the DESKTOP io backend (platform.mk: "the same
  // platform_x_io.c the desktop does"), so the same request was an fopen
  // against an empty MEMFS. It failed, and a missing manifest is deliberately
  // silent in task_plugin_io.c ("absent is the ordinary case -- a client with
  // no scripts installed"). The roster showed the two statically linked C
  // plugins and nothing said why the other five were gone.

  /* Mirrors of two C constants, because they are what the CLIENT will ask for
   * and this side only has to agree with it: SCRIPT_DIR in main.c, and
   * PLUGIN_MANIFEST_DEFAULT_PATH in plugin/task_plugin_io.h. Both are
   * overridable by env var on the desktop; a browser has no environment to
   * override them from. */
  var SCRIPT_ROOT = 'script';
  var PLUGIN_MANIFEST_PATH = 'plugins/plugins.ini';

  var plugins = {
    stored: 0,
    fetched: 0,
    missing: [],

    /*
     * `source=` paths out of a plugin manifest, in section order.
     *
     * The one key plugin_manifest_parse reads that names a FILE, and
     * deliberately nothing else: this has to know what the client will ask
     * for, not what any of it means. `enabled=0` entries are taken too --
     * the switch is the user's, and a disabled plugin has to be able to come
     * back without a reload.
     */
    sources: function (bytes) {
      var text = new TextDecoder('utf-8', { fatal: false }).decode(bytes);
      var out = [];
      text.split('\n').forEach(function (raw) {
        var line = raw.replace(/\r$/, '').replace(/^[ \t]+/, '');
        if (line.charAt(0) === ';' || line.charAt(0) === '#') { return; }
        var eq = line.indexOf('=');
        if (eq < 0) { return; }
        if (line.substring(0, eq).trim() !== 'source') { return; }
        out.push(line.substring(eq + 1).trim());
      });
      return out;
    },

    /* One file into the store, at the path the client will name it by. The
     * stored copy wins: it is durable, it costs no request, and it is what
     * makes the page work with its file server switched off. */
    take: function (path) {
      var held = store.fileGet(path);
      if (held) { this.stored++; return held; }

      var result = xhrSync('GET', bootUrl + '/' + path, null, null);
      if (result.status !== 200) { result = xhrSync('GET', '/' + path, null, null); }
      if (result.status !== 200) { this.missing.push(path); return null; }
      store.filePut(path, result.bytes);
      this.fetched++;
      return result.bytes;
    },

    /*
     * The manifest, then every script it names.
     *
     * The manifest is re-fetched rather than taken from the store, because it
     * is the LIST: a script added to it between runs would otherwise never be
     * asked for, and the page would keep running last week's plugin set with
     * no way to notice. Its scripts are then taken normally, so adding one
     * costs one request and the rest stay put.
     */
    load: function () {
      var manifestPath = SCRIPT_ROOT + '/' + PLUGIN_MANIFEST_PATH;
      var result = xhrSync('GET', bootUrl + '/' + manifestPath, null, null);
      var manifest;

      if (result.status !== 200) { result = xhrSync('GET', '/' + manifestPath, null, null); }
      if (result.status === 200) {
        store.filePut(manifestPath, result.bytes);
        manifest = result.bytes;
      } else {
        /* No server, or no manifest on it. The stored copy is the honest
         * answer -- a page that ran plugins yesterday should run them today
         * with its file server switched off. */
        manifest = store.fileGet(manifestPath);
        if (!manifest) { this.missing.push(manifestPath); return; }
      }

      var self = this;
      this.sources(manifest).forEach(function (rel) {
        self.take(SCRIPT_ROOT + '/' + rel);
      });
    }
  };

  // --------------------------------------------------------------- store
  //
  // The browser's cache, for the `make -C src web-idb` build. Absent from the
  // wire build, where io_server holds the cache and this is dead weight — the
  // module simply never calls into it.
  //
  // ## What it holds and why it is shaped this way
  //
  // One record per archive, keyed by (cache, table, archive), holding the exact
  // container bytes an idx record would have addressed. Not a dat2 file: a
  // sector chain exists to pack variable-length archives into one file without
  // a per-archive inode, and IndexedDB is already a keyed store, so the chain
  // would cost the 520-byte sector headers and the orphaned sectors every
  // rewrite leaves behind while buying nothing.
  //
  // ## Why the bytes live here rather than in the wasm heap
  //
  // The C side reads through a synchronous dat2 facade and this lane has no
  // ASYNCIFY, so a get() that had to reach IndexedDB could not answer the call
  // it stands in for. The resolution is to hold the records on this side,
  // hydrated in one cursor pass before main() runs, and let get() be a map
  // lookup. As a bonus the resident set is not wasm memory, so a cache larger
  // than the 4GB wasm ceiling is not itself a reason the module dies.
  //
  // A record that is not resident reads as absent, which is what an empty cache
  // looks like — so JS5 downloads it again and re-writes it. That is a
  // bandwidth cost and never a wrong archive.

  var STORE_DB = 'torirs-cache';
  var STORE_VERSION = 1;
  // Writes are batched: a JS5 boot installs hundreds of records, and a
  // transaction each would spend more time in IndexedDB than on the wire.
  var STORE_FLUSH_RECORDS = 64;
  var STORE_FLUSH_MS = 250;

  var store = {
    db: null,
    cacheKey: null,
    records: new Map(),   // "table/archive" -> Uint8Array
    tables: new Map(),    // table -> record count
    bytes: 0,
    files: new Map(),     // client-owned file path -> Uint8Array
    bootFiles: new Map(), // boot config path -> {bytes, etag}
    pendingGroups: [],
    pendingFiles: [],
    pendingBoot: [],
    flushTimer: null,
    writeErrors: 0,
    hydrated: 0,

    open: function () {
      var self = this;
      return new Promise(function (resolve, reject) {
        var request = indexedDB.open(STORE_DB, STORE_VERSION);
        request.onupgradeneeded = function (event) {
          var db = event.target.result;
          if (!db.objectStoreNames.contains('groups')) {
            var groups = db.createObjectStore('groups', { keyPath: 'k' });
            // The hydrate cursor walks one cache; without this index it would
            // have to scan every generation the browser has ever held.
            groups.createIndex('by_cache', 'c', { unique: false });
          }
          if (!db.objectStoreNames.contains('files')) {
            db.createObjectStore('files', { keyPath: 'k' });
          }
          // Configuration the client opens by name — the manifest, the
          // RevConfig INIs — kept with the validator the server gave it, so the
          // next boot can ask "still this one?" instead of re-downloading, and
          // can fall back to this copy when nothing answers.
          if (!db.objectStoreNames.contains('boot')) {
            db.createObjectStore('boot', { keyPath: 'k' });
          }
        };
        request.onsuccess = function () { self.db = request.result; resolve(); };
        request.onerror = function () { reject(request.error); };
      });
    },

    // Drop every record for one cache. `?cache_reset=1` — the only way to make
    // a cold boot reproducible once a warm one has been measured.
    clear: function (cacheKey) {
      var self = this;
      return new Promise(function (resolve) {
        if (!self.db) { resolve(0); return; }
        var tx = self.db.transaction(['groups'], 'readwrite');
        var index = tx.objectStore('groups').index('by_cache');
        var removed = 0;
        index.openCursor(IDBKeyRange.only(cacheKey)).onsuccess = function (ev) {
          var cursor = ev.target.result;
          if (!cursor) { return; }
          cursor.delete();
          removed++;
          cursor.continue();
        };
        tx.oncomplete = function () { resolve(removed); };
        tx.onerror = function () { resolve(removed); };
      });
    },

    /*
     * Everything that is not scoped to a cache: the client's saved options and
     * the boot configuration, with the validators the server gave it.
     *
     * Split from the cache hydrate below because of an ordering knot. The cache
     * records are keyed by cache name; the cache name is in the manifest; and
     * the manifest is a boot file that must itself be revalidated first. So the
     * un-scoped half loads, then the manifest is read, then the cache it names.
     */
    hydrateShared: function () {
      var self = this;
      return new Promise(function (resolve) {
        if (!self.db) { resolve(); return; }
        var tx = self.db.transaction(['files', 'boot'], 'readonly');
        tx.objectStore('files').openCursor().onsuccess = function (ev) {
          var cursor = ev.target.result;
          if (!cursor) { return; }
          self.files.set(cursor.value.k, new Uint8Array(cursor.value.d));
          cursor.continue();
        };
        tx.objectStore('boot').openCursor().onsuccess = function (ev) {
          var cursor = ev.target.result;
          if (!cursor) { return; }
          self.bootFiles.set(cursor.value.k,
                             { bytes: new Uint8Array(cursor.value.d), etag: cursor.value.e });
          cursor.continue();
        };
        tx.oncomplete = function () { resolve(); };
        tx.onerror = function () { resolve(); };
      });
    },

    // One cache's archive records. This is the whole reason main() can start
    // with a synchronous cache underneath it.
    hydrateCache: function (cacheKey) {
      var self = this;
      self.cacheKey = cacheKey;
      self.records.clear();
      self.tables.clear();
      self.bytes = 0;
      return new Promise(function (resolve) {
        if (!self.db) { resolve(); return; }
        var tx = self.db.transaction(['groups'], 'readonly');
        var index = tx.objectStore('groups').index('by_cache');
        index.openCursor(IDBKeyRange.only(cacheKey)).onsuccess = function (ev) {
          var cursor = ev.target.result;
          if (!cursor) { return; }
          var row = cursor.value;
          var bytes = new Uint8Array(row.d);
          self.records.set(row.t + '/' + row.a, bytes);
          self.tables.set(row.t, (self.tables.get(row.t) || 0) + 1);
          self.bytes += bytes.length;
          cursor.continue();
        };
        tx.oncomplete = function () {
          self.hydrated = self.records.size;
          resolve();
        };
        tx.onerror = function () { resolve(); };
      });
    },

    // --- the four calls dat2_web_store.c makes ---------------------------

    select: function (cacheKey) {
      // The store is per-cache and hydrated for exactly one. Asking for another
      // is a boot that would silently read one generation's archives against
      // another's reference tables, so it fails rather than answers.
      return this.db !== null && this.cacheKey === cacheKey;
    },

    get: function (table, archive) {
      return this.records.get(table + '/' + archive) || null;
    },

    put: function (table, archive, bytes) {
      var key = table + '/' + archive;
      var previous = this.records.get(key);
      if (previous) { this.bytes -= previous.length; }
      else { this.tables.set(table, (this.tables.get(table) || 0) + 1); }
      this.records.set(key, bytes);
      this.bytes += bytes.length;
      this.pendingGroups.push({
        k: this.cacheKey + '|' + table + '|' + archive,
        c: this.cacheKey, t: table, a: archive, d: bytes
      });
      this.scheduleFlush();
      return true;
    },

    hasTable: function (table) {
      return (this.tables.get(table) || 0) > 0;
    },

    recordCount: function () { return this.records.size; },
    byteCount: function () { return this.bytes; },

    // --- boot configuration ----------------------------------------------
    //
    // Hydrated with everything else before main(), so boot.fetch — which runs
    // synchronously — can consult it without reaching the database.

    bootGet: function (path) {
      return this.bootFiles.get(path) || null;
    },

    bootPut: function (path, bytes, etag) {
      this.bootFiles.set(path, { bytes: bytes, etag: etag || null });
      this.pendingBoot.push({ k: path, d: bytes, e: etag || null });
      this.scheduleFlush();
    },

    // --- client-owned files ----------------------------------------------
    //
    // The player's saved options, and the plugin manifest and scripts. MEMFS
    // would forget them when the tab closes -- for the options that is the
    // same defect rs_prefs.c exists to fix one layer up, and for the plugins
    // it is why the roster came up holding only the C ones.

    fileGet: function (path) {
      var bytes = this.files.get(path);
      return bytes === undefined ? null : bytes;
    },

    filePut: function (path, bytes) {
      this.files.set(path, bytes);
      this.pendingFiles.push({ k: path, d: bytes });
      this.scheduleFlush();
      return true;
    },

    // --- write-behind -----------------------------------------------------

    scheduleFlush: function () {
      var self = this;
      if (this.pendingGroups.length + this.pendingFiles.length +
          this.pendingBoot.length >= STORE_FLUSH_RECORDS) {
        this.flush();
        return;
      }
      if (this.flushTimer !== null) { return; }
      this.flushTimer = window.setTimeout(function () {
        self.flushTimer = null;
        self.flush();
      }, STORE_FLUSH_MS);
    },

    flush: function () {
      var self = this;
      var groups = this.pendingGroups;
      var files = this.pendingFiles;
      var bootRows = this.pendingBoot;

      if (this.flushTimer !== null) {
        window.clearTimeout(this.flushTimer);
        this.flushTimer = null;
      }
      if (!this.db ||
          (groups.length === 0 && files.length === 0 && bootRows.length === 0)) { return; }
      this.pendingGroups = [];
      this.pendingFiles = [];
      this.pendingBoot = [];

      try {
        var tx = this.db.transaction(['groups', 'files', 'boot'], 'readwrite');
        var groupStore = tx.objectStore('groups');
        var fileStore = tx.objectStore('files');
        var bootStore = tx.objectStore('boot');
        groups.forEach(function (row) {
          // A copy, and a plain ArrayBuffer: the Uint8Array came out of the
          // wasm heap's buffer, which structured clone would either reject or
          // serialize whole.
          groupStore.put({ k: row.k, c: row.c, t: row.t, a: row.a, d: row.d.slice().buffer });
        });
        files.forEach(function (row) {
          fileStore.put({ k: row.k, d: row.d.slice().buffer });
        });
        bootRows.forEach(function (row) {
          bootStore.put({ k: row.k, d: row.d.slice().buffer, e: row.e });
        });
        tx.onerror = function () {
          // Quota, most likely. The records stay resident, so this session is
          // unaffected; what is lost is the warm start next time.
          if (self.writeErrors++ === 0) {
            log('cache: IndexedDB refused a write (' +
                (tx.error ? tx.error.name : 'unknown') +
                ') — this session is fine, but the cache will not persist', true);
          }
        };
      } catch (err) {
        if (self.writeErrors++ === 0) {
          log('cache: could not write to IndexedDB — ' + err.message, true);
        }
      }
    }
  };

  // ------------------------------------------------------------------ IO
  //
  // The wasm side owns the encoding; this only moves bytes. Every heap view is
  // re-read from Module on each use because ALLOW_MEMORY_GROWTH detaches and
  // replaces the old ArrayBuffer, and a view cached across an allocation would
  // silently write into freed memory.

  var IO_LOG_MAX = 200;

  var io = {
    endpoint: ioUrl,
    ready: false,
    inflight: 0,
    batches: 0,
    bytesIn: 0,
    failures: 0,
    statsPtr: 0,

    // The client's frame, not the page's: bumped by pump(), which the client
    // calls once at the top of each of its frames.
    frame: 0,
    frameIoMs: 0,
    frameIoReqs: 0,
    framesOverrun: 0,
    slowestFrameMs: 0,
    entries: [],
    entriesDirty: false,
    // A dead server produces one failure per request for as long as the client
    // keeps trying, which buries the one line that matters. Say it once, then
    // count.
    transportDown: 0,

    // Take whatever the client queued and put it on the wire.
    //
    // Called from two places, and it has to be safe in both: once per client
    // frame (where it is usually a no-op), and from inside the client's
    // PlatformX_IO_Process when the pump is synchronous.
    take: function () {
      var len = Module._torirs_io_request_len();
      if (len <= 0) { return null; }
      try {
        var ptr = Module._torirs_io_request_ptr();
        // Copy before releasing: the wasm buffer is reused for the next batch.
        var batch = Module.HEAPU8.slice(ptr, ptr + len);
        Module._torirs_io_request_taken();
        return batch;
      } catch (err) {
        // Called from inside a wasm frame, so an escaping exception would
        // surface as a trap. Nothing was consumed, so the next pump retries.
        this.failures++;
        log('io: could not read the request batch: ' + err.message, true);
        return null;
      }
    },

    // Synchronous round trip, from inside PlatformX_IO_Process. Returns true
    // when the batch was delivered, false to let the client fall back to
    // waiting for the asynchronous path.
    pumpSync: function () {
      // `?io_sync=0` answers no here rather than by hiding this method, which
      // is what the wasm side's "the page decides whether it can do that" means
      // — false leaves the batch queued for the frame-gated path instead of
      // consuming it. Without this the flag disabled only the asynchronous
      // pump and left the blocking one running, which is the opposite of what
      // it reads like.
      if (!this.ready || !useSync) { return false; }
      var batch = this.take();
      if (!batch) { return true; }

      var started = performance.now();
      var reply = null;
      var failure = null;
      try {
        reply = xhrSyncBytes('POST', this.endpoint, batch);
        if (!reply) { failure = 'the server answered with an error status'; }
      } catch (err) {
        failure = err.message;
      }
      var elapsed = performance.now() - started;

      if (!reply) {
        this.failures++;
        this.reportTransportFailure(failure);
        Module._torirs_io_fail_pending();
        this.record(elapsed, 0, 0, true);
        return true;
      }
      this.transportDown = 0;
      this.batches++;
      this.deliver(reply);
      this.record(elapsed, reply.length, 0, false);
      return true;
    },

    // The request never reached the server, or the server never answered. That
    // is one condition, not one per archive: name it on the first failure with
    // what it usually means, then keep a count rather than repeating it.
    reportTransportFailure: function (message) {
      this.transportDown++;
      if (this.transportDown === 1) {
        log('io: cannot reach the IO server at ' + this.endpoint + ' — ' + message, true);
        log('io: it was serving until now, so it has most likely stopped. Every ' +
            'cache read will fail until it is back; check the terminal it was ' +
            'started in.', true);
      } else if (this.transportDown % 100 === 0) {
        log('io: still unreachable (' + this.transportDown + ' failed reads)', true);
      }
    },

    // Frame-gated delivery: does not block, but a frame satisfies one read.
    //
    // Runs whether or not the synchronous path is enabled. `useSync` is no
    // longer "which of the two carries the batch" — the wasm side decides that
    // per frame (PlatformXIO_Web_SetBlockingReads: yes while booting, no once
    // the client is live), so a batch it declined to pump has nobody else to
    // carry it and the task queue parks forever. There is no double delivery to
    // guard against: pumpSync already called _torirs_io_request_taken, so
    // take() sees a zero-length request and this returns.
    pump: function () {
      this.endFrame();
      if (!this.ready) { return; }

      var batch = this.take();
      if (!batch) { return; }

      var self = this;
      var started = performance.now();
      var startFrame = this.frame;
      this.inflight++;
      this.batches++;

      fetch(this.endpoint, {
        method: 'POST',
        headers: { 'Content-Type': 'application/octet-stream' },
        body: batch,
        cache: 'no-store'
      })
        .then(function (response) {
          if (!response.ok) { throw new Error('io server returned ' + response.status); }
          return response.arrayBuffer();
        })
        .then(function (buffer) {
          self.inflight--;
          self.transportDown = 0;
          var bytes = new Uint8Array(buffer);
          self.deliver(bytes);
          self.record(performance.now() - started, bytes.length,
                      self.frame - startFrame, false);
        })
        .catch(function (err) {
          self.inflight--;
          self.failures++;
          self.reportTransportFailure(err.message);
          // Failing the outstanding reads is deliberate. A dropped request
          // leaves the task queue parked forever with nothing to show for it;
          // an errored slot makes the waiting task take its own failure path,
          // which at least says which archive died.
          Module._torirs_io_fail_pending();
          self.record(performance.now() - started, 0, self.frame - startFrame, true);
        });
    },

    deliver: function (bytes) {
      var ptr = 0;
      try {
        ptr = Module._torirs_io_response_alloc(bytes.length);
        if (!ptr) {
          log('io: out of wasm memory for a ' + bytes.length + ' byte response', true);
          Module._torirs_io_fail_pending();
          return;
        }
        // Fetch the view AFTER the allocation: growing the heap replaces it.
        Module.HEAPU8.set(bytes, ptr);
        Module._torirs_io_response_submit(ptr, bytes.length);
      } catch (err) {
        this.failures++;
        log('io: could not deliver a ' + bytes.length + ' byte response: ' + err.message, true);
        Module._torirs_io_fail_pending();
        return;
      }
      this.bytesIn += bytes.length;
    },

    record: function (ms, bytes, frames, failed) {
      this.frameIoMs += ms;
      this.frameIoReqs++;
      this.entries.push({
        frame: this.frame, ms: ms, bytes: bytes, frames: frames, failed: failed
      });
      if (this.entries.length > IO_LOG_MAX) { this.entries.shift(); }
      this.entriesDirty = true;
    },

    // Close out the frame just ended and start the next. A frame that spent
    // more than one frame's worth of time in IO is the thing worth naming:
    // synchronously, that is a stalled main thread; asynchronously, it is a
    // read that outlived the frame that asked for it.
    endFrame: function () {
      if (this.frameIoReqs > 0) {
        if (this.frameIoMs > FRAME_MS) { this.framesOverrun++; }
        if (this.frameIoMs > this.slowestFrameMs) { this.slowestFrameMs = this.frameIoMs; }
      }
      this.frameIoMs = 0;
      this.frameIoReqs = 0;
      this.frame++;
    },

    stats: function () {
      // Absent on the IndexedDB build, which has no wire to count. Tested
      // rather than assumed: this runs every frame from the moment the runtime
      // is up, which is before the store has finished hydrating and so before
      // anything else can tell the two lanes apart.
      if (!this.ready || typeof Module._torirs_io_stats !== 'function') { return null; }
      if (!this.statsPtr) { this.statsPtr = Module._malloc(5 * 4); }
      Module._torirs_io_stats(this.statsPtr);
      var view = Module.HEAP32;
      var base = this.statsPtr >> 2;
      return {
        requests: view[base],
        cacheHits: view[base + 1],
        completed: view[base + 2],
        failed: view[base + 3],
        pending: view[base + 4]
      };
    }
  };

  // The wasm side reaches both pumps through this (see platform_x_io_web.c),
  // and the record store through torirsStore (see dat2_web_store.c).
  var Module = {};
  window.Module = Module;
  Module.torirsIO = io;
  Module.torirsStore = store;

  // ------------------------------------------------------- the JS5 barrier
  //
  // Only the IndexedDB build has one. The reference tables must be installed
  // before main() opens the cache — App_Init decodes them itself and is not a
  // tolerant reader — and the download cannot happen inside main(), because a
  // WebSocket delivers nothing to a thread that is spinning on it and this lane
  // has no ASYNCIFY.
  //
  // So the loop is inverted: the page steps the C state machine from a
  // setTimeout chain, which yields to the event loop between steps, and starts
  // main() itself when the machine says ready. See
  // src/platform/web_cache_boot.c.
  //
  // ## Why this cannot be a preRun run-dependency
  //
  // The obvious shape — addRunDependency in preRun, remove it when the prime
  // finishes — deadlocks. preRun happens *before* initRuntime, and a run
  // dependency taken there also holds initRuntime back, so the native functions
  // the prime is made of cannot be called yet: emscripten's own assertion for
  // it reads "native function called before runtime initialization" (and
  // without assertions it is a wasm trap at a nonsense address).
  //
  // The correct seam is one step later. Module.noInitialRun tells the runtime
  // to finish initializing and then stop, leaving main() to be started by
  // Module.callMain once the barrier is done.

  var js5 = {
    active: false,
    key: null,
    steps: 0,
    started: 0,

    // The manifest main() will open. Same scan main.c does: the first real
    // --manifest wins.
    manifestPath: function () {
      for (var i = 0; i < args.length; i++) {
        if (args[i] === '--manifest' && i + 1 < args.length) { return args[i + 1]; }
      }
      return null;
    },

    // Does this build have a cache of its own to prime? Answered before the
    // runtime finishes initializing, because the answer decides whether main()
    // starts on its own.
    wanted: function () {
      return typeof Module._torirs_web_cache_prime_begin === 'function';
    },

    /*
     * The whole pre-main() sequence, for both lanes.
     *
     * The ordering is forced and worth stating, because it is not obvious and
     * it is circular-looking:
     *
     *   1. open IndexedDB
     *   2. hydrate the un-scoped stores (client files, boot config + validators)
     *   3. load the boot files — revalidating each against the server, which
     *      needs step 2 to have a validator to send
     *   4. ask C which cache the manifest names — needs step 3 to have put the
     *      manifest where fopen can see it
     *   5. hydrate that cache's records — needs step 4 for the key
     *   6. prime the JS5 metadata, then start main()
     *
     * Steps 4-6 are the IndexedDB lane only; the wire lane stops after 3 and
     * starts main(), which is why boot-file revalidation is shared by both.
     */
    begin: function () {
      var self = this;
      this.active = true;
      this.started = performance.now();

      store.open()
        .catch(function (err) {
          // A page in private browsing may have no IndexedDB at all. Boot
          // files then have no stored copy and are simply fetched, which is
          // what the harness did before any of this existed.
          log('cache: IndexedDB is unavailable (' +
              (err && err.message ? err.message : err) + ') — nothing will persist', true);
        })
        .then(function () { return store.hydrateShared(); })
        .then(function () {
          boot.load();
          if (boot.loaded.length) { log('torirs: boot files ' + boot.loaded.join(' ')); }
          if (boot.revalidated || boot.offline) {
            log('torirs: boot config — ' + boot.revalidated + ' unchanged' +
                (boot.offline ? ', ' + boot.offline + ' from the offline copy' : ''));
          }
          if (boot.missing.length) {
            log('torirs: boot files NOT available: ' + boot.missing.join(' '), true);
          }
          /*
           * After the boot files and before main(), for the same reason the
           * cache records are hydrated before it: the client reads a script
           * through the IO queue, the store answers that read synchronously,
           * and the store can only answer what it already holds.
           *
           * The wire lane skips it -- there a script request crosses to
           * io_server, which reads the real directory, and staging a second
           * copy here would be a second thing to keep in step.
           */
          if (self.wanted()) {
            plugins.load();
            if (plugins.stored || plugins.fetched) {
              log('torirs: plugins — ' + plugins.stored + ' stored, ' +
                  plugins.fetched + ' fetched');
            }
            if (plugins.missing.length) {
              log('torirs: plugin files NOT available: ' +
                  plugins.missing.join(' '), true);
            }
          }
          if (!self.wanted()) { self.finish(); return null; }
          return self.beginCache();
        })
        .catch(function (err) {
          log('torirs: boot failed — ' + (err && err.message ? err.message : err), true);
          self.finish();
        });
    },

    beginCache: function () {
      var self = this;
      var manifest = this.manifestPath();

      if (!manifest) {
        log('cache: no --manifest in the query string, so no cache to open', true);
        this.finish();
        return null;
      }
      // C reads the manifest and names the cache; this side must never parse
      // `[cache:boot] dir=` itself, or the two readers drift.
      this.key = Module.ccall('torirs_web_cache_key', 'string', ['string'], [manifest]);
      if (!this.key) {
        this.fail('the manifest names no cache');
        return null;
      }

      return Promise.resolve(cacheReset ? store.clear(self.key) : 0)
        .then(function (cleared) {
          if (cleared) { log('cache: cleared ' + cleared + ' record(s) for ' + self.key); }
          return store.hydrateCache(self.key);
        })
        .then(function () {
          log('cache: ' + self.key + ' — ' + store.recordCount() + ' record(s), ' +
              kb(store.byteCount()) + ' resident' +
              (store.recordCount() === 0 ? ' (cold start)' : ''));
          if (Module.ccall('torirs_web_cache_prime_begin', 'number',
                           ['string', 'number'], [js5Host, js5Port]) !== 0) {
            self.fail('could not start the JS5 primer');
            return;
          }
          self.step();
        });
    },

    // One tick per turn of the event loop. setTimeout(0) rather than a tight
    // loop is the entire point: it is what lets the WebSocket deliver.
    step: function () {
      var self = this;
      var status = Module._torirs_web_cache_prime_step();
      this.steps++;

      if (status === 0) {
        if (this.steps % 200 === 0) { this.report(); }
        window.setTimeout(function () { self.step(); }, 0);
        return;
      }
      if (status < 0) {
        this.report();
        this.fail(this.diagnose());
        return;
      }
      log('cache: metadata ready in ' +
          ((performance.now() - this.started) / 1000).toFixed(1) + 's');
      this.finish();
    },

    report: function () {
      if (typeof Module._torirs_web_cache_prime_stats !== 'function') { return; }
      var ptr = Module._malloc(5 * 4);
      Module._torirs_web_cache_prime_stats(ptr);
      var view = Module.HEAP32;
      var base = ptr >> 2;
      setStatus('cache: priming from ws://' + js5Host + ':' + js5Port +
                ' — ' + view[base + 1] + ' reference tables, ' +
                kb(view[base + 2]) + ' received');
      Module._free(ptr);
    },

    // Two failures look identical from the page and have opposite fixes: no
    // server there, versus a server whose cache the client will not accept.
    // Whether any bytes arrived separates them, so say which one it was rather
    // than printing the "start a server" advice at someone whose server is
    // running and answering.
    diagnose: function () {
      if (typeof Module._torirs_web_cache_prime_stats !== 'function') { return null; }
      var ptr = Module._malloc(5 * 4);
      Module._torirs_web_cache_prime_stats(ptr);
      var base = ptr >> 2;
      var bytes = Module.HEAP32[base + 2];
      var error = Module.HEAP32[base + 4];
      Module._free(ptr);
      if (bytes > 0) {
        return { answered: true, bytes: bytes, error: error };
      }
      return { answered: false, bytes: bytes, error: error };
    },

    fail: function (why) {
      if (why && why.answered) {
        // JS5_ERROR_* from src/js5/js5.h. 11 is REFERENCE, which for a cache
        // this repo packed itself is nearly always the 16-bit ceiling: the JS5
        // request carries a 2-byte group id, so a table holding ids at or above
        // 65536 cannot be served by this protocol at all.
        log('cache: the JS5 server answered (' + kb(why.bytes) + ') but the client ' +
            'rejected its metadata — JS5 error ' + why.error +
            (why.error === 11 ? ' (reference table): a table almost certainly holds ' +
              'group ids past 65535, which a 4-byte JS5 request cannot address' : ''), true);
        this.finish();
        return;
      }
      log('cache: ' + (typeof why === 'string' ? why : 'the JS5 server did not answer') +
          '. Start one with:', true);
      log('  make -C src js5-server && ./src/build_opt/js5_server --cache <dir> ' +
          '--revision <rev> --port ' + js5Port, true);
      this.finish();
    },

    // Start main(), whether the prime succeeded or not.
    //
    // Running it after a failure is deliberate: the client then reports a cache
    // it cannot read, which says what is wrong. Leaving the page on "loading…"
    // instead is indistinguishable from a hang, and the two have completely
    // different fixes.
    finish: function () {
      if (!this.active) { return; }
      this.active = false;
      try {
        Module.callMain(args);
      } catch (err) {
        log('torirs: main() did not start — ' + (err && err.message ? err.message : err), true);
      }
    }
  };

  // ------------------------------------------------------------------- UI

  function renderIoLog() {
    if (!io.entriesDirty) { return; }
    io.entriesDirty = false;
    if (!ioLogEl) { ioLogEl = document.getElementById('iolog'); }
    if (!ioLogEl) { return; }

    var rows = io.entries.slice(-60).reverse().map(function (e) {
      var cls = e.failed ? 'bad' : (e.ms > FRAME_MS || e.frames > 0 ? 'slow' : '');
      var span = e.frames > 0 ? ' · ' + e.frames + 'f' : '';
      return '<div class="' + cls + '">' +
        '<span class="f">f' + e.frame + '</span>' +
        '<span class="t">' + e.ms.toFixed(1) + 'ms</span>' +
        '<span class="b">' + (e.failed ? 'FAILED' : kb(e.bytes)) + '</span>' +
        '<span class="s">' + span + '</span>' +
        '</div>';
    }).join('');
    ioLogEl.innerHTML = rows;
  }

  function heapMb() {
    try { return (Module.HEAPU8.length / 1048576).toFixed(0); } catch (err) { return '?'; }
  }

  // The frame-aligned pump. The client calls into this too (so a request never
  // waits longer than it must), but the harness drives its own animation frame
  // as well: in asynchronous mode the wasm loop can be blocked on exactly the
  // IO this pump delivers, and a pump that only ran when the client ran would
  // deadlock there.
  function hostFrame() {
    io.pump();

    // The IndexedDB build has no wire and so no io stats; what matters there is
    // how the cache is filling.
    if (store.cacheKey) {
      setStatus(
        'heap ' + heapMb() + 'MB' +
        '  ·  cache ' + store.cacheKey +
        '  ' + store.recordCount() + ' records' +
        '  ' + kb(store.byteCount()) +
        '  ·  hydrated ' + store.hydrated +
        '  written ' + (store.recordCount() - store.hydrated) +
        (store.writeErrors ? '  ·  ' + store.writeErrors + ' write errors' : '')
      );
      renderIoLog();
      window.requestAnimationFrame(hostFrame);
      return;
    }

    var stats = io.stats();
    if (stats) {
      setStatus(
        'heap ' + heapMb() + 'MB' +
        '  ·  io ' + (useSync ? 'sync-boot' : 'async') +
        '  req ' + stats.requests +
        '  hit ' + stats.cacheHits +
        '  done ' + stats.completed +
        '  fail ' + (stats.failed + io.failures) +
        '  pending ' + stats.pending +
        '  ·  ' + kb(io.bytesIn) + ' in ' + io.batches + ' batches' +
        '  ·  slow frames ' + io.framesOverrun +
        ' (worst ' + io.slowestFrameMs.toFixed(0) + 'ms)'
      );
    }
    renderIoLog();
    window.requestAnimationFrame(hostFrame);
  }

  // --------------------------------------------------------------- Module

  Module.arguments = args;
  Module.canvas = (function () {
    var canvas = document.getElementById('canvas');
    // Without this a lost WebGL context fails silently and the page just
    // stops painting, which looks exactly like a hung client.
    canvas.addEventListener('webglcontextlost', function (e) {
      log('webgl context lost', true);
      e.preventDefault();
    }, false);
    return canvas;
  })();

  Module.print = function (text) { log(text, false); };
  Module.printErr = function (text) { log(text, true); };

  Module.preRun = [function () {
    readEnv().forEach(function (pair) { ENV[pair[0]] = pair[1]; });
    /*
     * Take main() off the runtime's hands, on both lanes.
     *
     * Everything main() needs to find — the manifest, the RevConfig INIs — now
     * comes from a store that has to be opened asynchronously first, and
     * nothing here may call into wasm anyway: preRun runs before initRuntime.
     * So the whole boot sequence moves after runtime init and ends by calling
     * main() itself. See js5.begin.
     */
    Module.noInitialRun = true;
  }];

  // The last few records a session wrote are still in the batch when the tab
  // goes away. pagehide rather than beforeunload: it is the one that fires on
  // mobile and on a bfcache eviction, and losing the tail of a download would
  // make the next boot re-fetch it for no reason.
  window.addEventListener('pagehide', function () { store.flush(); });

  Module.onRuntimeInitialized = function () {
    io.ready = true;
    if (store.cacheKey) {
      log('torirs: runtime up, cache in IndexedDB, js5 ws://' + js5Host + ':' + js5Port);
    } else {
      log('torirs: runtime up, io endpoint ' + io.endpoint +
          ' (' + (useSync ? 'synchronous while booting, frame-gated once live'
                          : 'frame-gated') + ')');
    }
    log('torirs: argv ' + JSON.stringify(args));
    // Which cache the server has open. Changing the manifest in the URL
    // changes the client but not the server, and a client booting one
    // generation against another's cache just fails to decode anything —
    // so both halves say what they are, side by side, before that happens.
    fetch(statsUrl, { cache: 'no-store' })
      .then(function (r) { return r.ok ? r.text() : ''; })
      .then(function (text) {
        var match = /serving=(.*?)\s+served=/.exec(text);
        if (match) { log('torirs: io server has ' + match[1] + ' open'); }
      })
      .catch(function () { /* older server, or none: not worth a message */ });
    installMemtraceButton();
    window.requestAnimationFrame(hostFrame);
    // The runtime is up, so native calls are legal — and main() has not run,
    // because preRun set noInitialRun. This is the window the barrier needs.
    js5.begin();
  };

  // ------------------------------------------------------------------ memtrace
  //
  // MEMTRACE=1 builds only. The page never exits, so nothing ever runs the
  // tracer's atexit flush — the button *is* the flush, and because flushing
  // closes the trace file, it also ends recording for the session. That is why
  // this is a deliberate click and not something wired to beforeunload.
  //
  // The controls stay hidden on an untraced build rather than erroring when
  // pressed, so the page looks the same as it always did unless the tracer is
  // actually linked in.

  // Flush once, then read the bytes out of MEMFS. Repeat presses re-read the
  // same closed file instead of flushing again (which would be a no-op anyway).
  var memtraceBytes = null;

  function memtraceRead() {
    if (memtraceBytes) { return memtraceBytes; }
    Module._torirs_memtrace_web_flush();
    // UTF8ToString is not in EXPORTED_RUNTIME_METHODS; HEAPU8 is, and the path
    // is plain ASCII, so read the C string out of the heap directly.
    var addr = Module._torirs_memtrace_web_path();
    var path = '';
    while (Module.HEAPU8[addr]) { path += String.fromCharCode(Module.HEAPU8[addr++]); }
    memtraceBytes = Module.FS.readFile(path);
    log('memtrace: flushed ' + memtraceBytes.length + ' bytes from ' + path +
        ' (recording has stopped)');
    return memtraceBytes;
  }

  // Hand the trace to viewer.html in a new tab.
  //
  // The bytes go over postMessage as a transferable ArrayBuffer rather than
  // through a URL or storage: a trace is tens to hundreds of MB, which no query
  // string or localStorage will hold, and transferring costs no copy. The
  // viewer announces itself with 'memtrace-ready' and confirms with
  // 'memtrace-acked'; the retry loop covers the case where it finished loading
  // before this tab attached its listener.
  function memtraceOpenViewer() {
    var bytes;
    try {
      bytes = memtraceRead();
    } catch (e) {
      log('memtrace: cannot read the trace — ' + e, true);
      return;
    }

    var viewer = window.open('./viewer.html', '_blank');
    if (!viewer) {
      log('memtrace: the viewer tab was blocked — allow popups for this site, ' +
          'or use "save .bin" and open viewer.html by hand', true);
      return;
    }

    var origin = window.location.origin;
    var acked = false;
    var retries = 0;
    var timer = null;

    function send() {
      if (acked) { return; }
      // A transfer neuters the buffer, so each attempt sends its own copy —
      // otherwise a retry would post an empty one.
      var copy = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
      try {
        viewer.postMessage({ type: 'memtrace-bytes', buffer: copy }, origin, [copy]);
      } catch (e) {
        /* viewer not ready yet; the retry below covers it */
      }
    }

    function onMessage(ev) {
      if (ev.source !== viewer || ev.origin !== origin || !ev.data) { return; }
      if (ev.data.type === 'memtrace-ready') {
        send();
      } else if (ev.data.type === 'memtrace-acked') {
        acked = true;
        window.clearInterval(timer);
        window.removeEventListener('message', onMessage);
        log('memtrace: viewer loaded the trace');
      }
    }

    window.addEventListener('message', onMessage);
    timer = window.setInterval(function () {
      if (acked) { return; }
      send();
      if (++retries >= 20) {
        window.clearInterval(timer);
        window.removeEventListener('message', onMessage);
        log('memtrace: the viewer never acknowledged the trace — is ' +
            'viewer.html served next to this page?', true);
      }
    }, 100);
    send();
  }

  function memtraceDownload() {
    var bytes;
    try {
      bytes = memtraceRead();
    } catch (e) {
      log('memtrace: cannot read the trace — ' + e, true);
      return;
    }
    var url = URL.createObjectURL(new Blob([bytes], { type: 'application/octet-stream' }));
    var link = document.createElement('a');
    link.href = url;
    link.download = 'memtrace.bin';
    link.click();
    URL.revokeObjectURL(url);
    log('memtrace: saved memtrace.bin — summarize with ' +
        'python3 tools/memtrace/summarize.py memtrace.bin');
  }

  function installMemtraceButton() {
    var controls = document.getElementById('memtrace');
    if (!controls) { return; }
    if (typeof Module._torirs_memtrace_web_flush !== 'function'
        || typeof Module._torirs_memtrace_web_path !== 'function'
        || !Module.FS) {
      return; // not a MEMTRACE build — leave the controls hidden
    }
    document.getElementById('btn-memtrace-view').onclick = memtraceOpenViewer;
    document.getElementById('btn-memtrace-download').onclick = memtraceDownload;
    controls.hidden = false;
    log('memtrace: heap tracing is on — "heap profile" opens the viewer');
  }

  Module.setStatus = function (text) { if (text) { setStatus(text); } };
})();
