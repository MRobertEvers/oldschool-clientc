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
//   ?arg=--manifest&arg=manifests/manifest_rs254lc.ini&arg=--offline   one arg per param
//   ?args=--manifest,manifests/manifest_rs254lc.ini,--offline          same, comma-joined
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

/*
 * One function wrapped around the file, because these load as plain <script>
 * tags and classic scripts share a single global lexical scope -- everything
 * below would otherwise collide with the other two page scripts by name. Only
 * `window.Module` leaves the wrapper, which is the whole interface torirs.js
 * reads. Not a build artifact: there is no build step here, and nothing in this
 * directory is minified.
 */
(function () {
  'use strict';

  const params = new URLSearchParams(window.location.search);

  const DEFAULT_ARGS = ['--manifest', 'manifests/manifest_rs254lc.ini', '--offline'];

  function readArgs() {
    const repeated = params.getAll('arg');
    if (repeated.length > 0) { return repeated; }
    if (params.has('args')) {
      return params.get('args').split(',').filter(s => s.length > 0);
    }
    return DEFAULT_ARGS;
  }

  // Accepts both `env=A=1&env=B=2` and the single `env=A=1;B=2` form. Splitting
  // on the FIRST `=` only, so a value may itself contain one.
  function readEnv() {
    const out = [];
    params.getAll('env').forEach(group => {
      group.split(';').forEach(pair => {
        const eq = pair.indexOf('=');
        if (eq > 0) { out.push([pair.substring(0, eq), pair.substring(eq + 1)]); }
      });
    });
    return out;
  }

  const args = readArgs();
  const ioUrl = params.get('io') || '/io';
  const bootUrl = ioUrl.replace(/\/io$/, '/boot');
  const statsUrl = ioUrl.replace(/\/io$/, '/stats');
  const useSync = params.get('io_sync') !== '0';

  // Where the IndexedDB build reaches JS5. Defaults to this page's host, which
  // is right when the client and the cache server are served from one machine
  // (the usual local setup) and overridable when they are not.
  const js5Host = params.get('js5_host') || window.location.hostname || 'localhost';
  const js5Port = parseInt(params.get('js5_port') || '43594', 10);
  const cacheReset = params.get('cache_reset') === '1';

  const FRAME_MS = 1000 / 60;

  // ------------------------------------------------------------------- log

  let logEl = null;
  let statusEl = null;
  let ioLogEl = null;
  const logLines = [];
  const LOG_MAX = 400;

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
    if (bytes >= 1048576) { return `${(bytes / 1048576).toFixed(1)}MB`; }
    return `${(bytes / 1024).toFixed(1)}KB`;
  }

  // ---------------------------------------------------- synchronous transfer
  //
  // A synchronous XHR may not set responseType, so binary comes back as a
  // string with one character per byte — hence the x-user-defined override,
  // which keeps bytes 0x80-0xFF from being mangled into replacement chars.

  function xhrSyncBytes(method, url, body) {
    const result = xhrSync(method, url, body, null);
    return result.status === 200 ? result.bytes : null;
  }

  // The same transfer, but reporting enough to do a conditional request:
  // {status, bytes, etag}. status 0 means the request never reached anyone,
  // which is a different thing from a 404 and has a different answer.
  function xhrSync(method, url, body, etag) {
    const xhr = new XMLHttpRequest();
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
      return { status: 304, bytes: null, etag };
    }
    if (xhr.status !== 200) {
      return { status: xhr.status, bytes: null, etag: null };
    }
    const text = xhr.responseText;
    const out = new Uint8Array(text.length);
    for (let i = 0; i < text.length; i++) { out[i] = text.charCodeAt(i) & 0xFF; }
    let tag = null;
    try { tag = xhr.getResponseHeader('ETag'); } catch (err) { /* not exposed */ }
    return { status: 200, bytes: out, etag: tag };
  }

  // ------------------------------------------------------------ boot files
  //
  // The files main() opens with fopen: the manifest named on the command line,
  // and the RevConfig INIs that manifest names. Fetched into the virtual
  // filesystem at the same relative paths the client will use, because
  // BootManifest resolves its values against the manifest's own directory.

  const boot = {
    loaded: [],
    missing: [],
    /** Files the server confirmed unchanged (304), and files served from the
     *  stored copy because nothing answered. */
    revalidated: 0,
    offline: 0,

    write(path, bytes) {
      const parts = path.split('/').filter(s => s.length > 0);
      let dir = '';
      for (let i = 0; i < parts.length - 1; i++) {
        dir += `/${parts[i]}`;
        try { Module.FS.mkdir(dir); } catch (err) { /* already there */ }
      }
      Module.FS.writeFile(`/${parts.join('/')}`, bytes);
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
    fetch(path) {
      const cached = store.bootGet(path);
      let result = xhrSync('GET', `${bootUrl}/${path}`, null, cached && cached.etag);
      let via = '/boot/';

      if (result.status === 404 || result.status === 0) {
        const direct = xhrSync('GET', `/${path}`, null, cached && cached.etag);
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
        this.loaded.push(`${path} (unchanged)`);
        this.revalidated++;
        return cached.bytes;
      }
      if (cached) {
        // Unreachable, or the server no longer has it. Either way this page has
        // a copy and refusing to boot would help nobody.
        this.write(path, cached.bytes);
        this.loaded.push(`${path} (offline copy)`);
        this.offline++;
        return cached.bytes;
      }
      this.missing.push(path);
      return null;
    },

    // `revconfig_ui` / `revconfig_cache` out of an INI, comments stripped.
    revconfigPaths(bytes) {
      const text = new TextDecoder('utf-8', { fatal: false }).decode(bytes);
      const out = [];
      text.split('\n').forEach(line => {
        line = line.split(';')[0].split('#')[0].trim();
        const eq = line.indexOf('=');
        if (eq < 0) { return; }
        const key = line.substring(0, eq).trim();
        if (key === 'revconfig_ui' || key === 'revconfig_cache') {
          out.push(line.substring(eq + 1).trim());
        }
      });
      return out;
    },

    // `[client:args]` is intentionally one exact argv token per `arg=` line,
    // matching the repeated query-string form above. Do not trim or interpret
    // quotes/comments: the C INI reader does neither for a value.
    clientArgs(bytes) {
      const text = new TextDecoder('utf-8', { fatal: false }).decode(bytes);
      const out = [];
      let section = '';
      text.split('\n').forEach(raw => {
        const line = raw.replace(/\r$/, '');
        // The C reader skips indentation before each element, but preserves
        // everything after `=` as the value. Use a separate structural view
        // so leading whitespace is ignored without altering the argv token.
        const structural = line.replace(/^[ \t]+/, '');
        if (structural.charAt(0) === '[') {
          const close = structural.indexOf(']');
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

    load() {
      const seen = {};

      const take = (path) => {
        if (!path || path.charAt(0) === '/' || seen[path]) { return null; }
        seen[path] = true;
        return this.fetch(path);
      };

      function argumentTakesValue(argument) {
        return argument === '--manifest' || argument === '--port' ||
          argument === '--revconfig' || argument === '--revconfig-cache' ||
          argument === '--connect' || argument === '--user' ||
          argument === '--pass' || argument === '--rev' ||
          argument === '--windowmode' || argument === '--window';
      }

      function takeArgFiles(argv) {
        for (let j = 0; j < argv.length; j++) {
          const flag = argv[j];
          const value = argv[j + 1];
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

      for (let i = 0; i < args.length; i++) {
        const flag = args[i];
        const value = args[i + 1];
        if (flag === '--manifest' && i + 1 < args.length) {
          const bytes = take(value);
          if (bytes) {
            const dir = value.includes('/') ? value.replace(/\/[^/]*$/, '/') : '';
            this.revconfigPaths(bytes).forEach(rel => { take(dir + rel); });
            // Additional CLI paths keep CLI/CWD semantics; unlike typed
            // revconfig keys they are not relative to the manifest directory.
            takeArgFiles(this.clientArgs(bytes));
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
  // REVALIDATED, not fetched-once. A script the database already holds is
  // offered back to the server as a validator: a 304 costs a header and the
  // stored copy stands, a 200 replaces it, and a server that answers nothing
  // leaves the page starting every plugin it ran last time.
  //
  // Held-until-missing was the first shape and it was wrong, because the
  // stored script outlives the BINARY it was stored against. tile_indicator
  // grew a fill-colour argument, its draw.tile call went from five arguments
  // to six, and the module was rebuilt -- but the store still held the
  // five-argument script, so every frame died in the new binding with
  // "bad argument #6 to 'tile'" and the roster showed a plugin that had been
  // correct on disk for as long as it had been broken in the browser. A plugin
  // script and the api it calls are one version, and only the server knows
  // which one that is.
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
  const SCRIPT_ROOT = 'script';
  const PLUGIN_MANIFEST_PATH = 'plugins/plugins.ini';

  const plugins = {
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
    sources(bytes) {
      const text = new TextDecoder('utf-8', { fatal: false }).decode(bytes);
      const out = [];
      text.split('\n').forEach(raw => {
        const line = raw.replace(/\r$/, '').replace(/^[ \t]+/, '');
        if (line.charAt(0) === ';' || line.charAt(0) === '#') { return; }
        const eq = line.indexOf('=');
        if (eq < 0) { return; }
        if (line.substring(0, eq).trim() !== 'source') { return; }
        out.push(line.substring(eq + 1).trim());
      });
      return out;
    },

    /*
     * One file into the store, at the path the client will name it by, asked
     * for conditionally -- the same three outcomes as boot.fetch above, for
     * the same reason and against the same routes:
     *
     *   200  changed (or never held) -- take it and remember the validator
     *   304  unchanged -- the stored copy stands, no body transferred
     *   else nothing answered -- the stored copy is the honest answer, and
     *        only a path with no stored copy at all counts as missing
     */
    take(path) {
      const held = store.fileGet(path);
      const tag = held ? store.fileEtag(path) : null;
      let result = xhrSync('GET', `${bootUrl}/${path}`, null, tag);

      if (result.status === 404 || result.status === 0) {
        const direct = xhrSync('GET', `/${path}`, null, tag);
        if (direct.status === 200 || direct.status === 304) {
          result = direct;
        } else if (result.status === 0 && direct.status !== 0) {
          result = direct;
        }
      }

      if (result.status === 200) {
        store.filePut(path, result.bytes, result.etag);
        this.fetched++;
        return result.bytes;
      }
      if (held) { this.stored++; return held; }
      this.missing.push(path);
      return null;
    },

    /*
     * The manifest, then every script it names.
     *
     * The manifest is re-fetched rather than taken from the store, because it
     * is the LIST: a script added to it between runs would otherwise never be
     * asked for, and the page would keep running last week's plugin set with
     * no way to notice. Its scripts are then taken normally -- one conditional
     * request each, which for an unchanged script is a 304 and no body.
     */
    load() {
      const manifestPath = `${SCRIPT_ROOT}/${PLUGIN_MANIFEST_PATH}`;
      let result = xhrSync('GET', `${bootUrl}/${manifestPath}`, null, null);
      let manifest;

      if (result.status !== 200) { result = xhrSync('GET', `/${manifestPath}`, null, null); }
      if (result.status === 200) {
        store.filePut(manifestPath, result.bytes, result.etag);
        manifest = result.bytes;
      } else {
        /* No server, or no manifest on it. The stored copy is the honest
         * answer -- a page that ran plugins yesterday should run them today
         * with its file server switched off. */
        manifest = store.fileGet(manifestPath);
        if (!manifest) { this.missing.push(manifestPath); return; }
      }

      this.sources(manifest).forEach(rel => {
        this.take(`${SCRIPT_ROOT}/${rel}`);
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

  const STORE_DB = 'torirs-cache';
  const STORE_VERSION = 1;
  // Writes are batched: a JS5 boot installs hundreds of records, and a
  // transaction each would spend more time in IndexedDB than on the wire.
  const STORE_FLUSH_RECORDS = 64;
  const STORE_FLUSH_MS = 250;

  const store = {
    db: null,
    cacheKey: null,
    records: new Map(),   // "table/archive" -> Uint8Array
    tables: new Map(),    // table -> record count
    bytes: 0,
    files: new Map(),     // client-owned file path -> Uint8Array
    fileEtags: new Map(), // ...and the server validator the fetched ones came with
    bootFiles: new Map(), // boot config path -> {bytes, etag}
    pendingGroups: [],
    pendingFiles: [],
    pendingBoot: [],
    flushTimer: null,
    writeErrors: 0,
    hydrated: 0,

    open() {
      return new Promise((resolve, reject) => {
        const request = indexedDB.open(STORE_DB, STORE_VERSION);
        request.onupgradeneeded = event => {
          const db = event.target.result;
          if (!db.objectStoreNames.contains('groups')) {
            const groups = db.createObjectStore('groups', { keyPath: 'k' });
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
        request.onsuccess = () => { this.db = request.result; resolve(); };
        request.onerror = () => { reject(request.error); };
      });
    },

    // Drop every record for one cache. `?cache_reset=1` — the only way to make
    // a cold boot reproducible once a warm one has been measured.
    clear(cacheKey) {
      return new Promise(resolve => {
        if (!this.db) { resolve(0); return; }
        const tx = this.db.transaction(['groups'], 'readwrite');
        const index = tx.objectStore('groups').index('by_cache');
        let removed = 0;
        index.openCursor(IDBKeyRange.only(cacheKey)).onsuccess = ev => {
          const cursor = ev.target.result;
          if (!cursor) { return; }
          cursor.delete();
          removed++;
          cursor.continue();
        };
        tx.oncomplete = () => { resolve(removed); };
        tx.onerror = () => { resolve(removed); };
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
    hydrateShared() {
      return new Promise(resolve => {
        if (!this.db) { resolve(); return; }
        const tx = this.db.transaction(['files', 'boot'], 'readonly');
        tx.objectStore('files').openCursor().onsuccess = ev => {
          const cursor = ev.target.result;
          if (!cursor) { return; }
          this.files.set(cursor.value.k, new Uint8Array(cursor.value.d));
          /* Rows written before files carried a validator have no `e`. That
           * reads as "no validator", which sends the next request out
           * unconditional -- which is what replaces a copy stored back when
           * nothing revalidated it. */
          this.fileEtags.set(cursor.value.k, cursor.value.e || null);
          cursor.continue();
        };
        tx.objectStore('boot').openCursor().onsuccess = ev => {
          const cursor = ev.target.result;
          if (!cursor) { return; }
          this.bootFiles.set(cursor.value.k,
                             { bytes: new Uint8Array(cursor.value.d), etag: cursor.value.e });
          cursor.continue();
        };
        tx.oncomplete = () => { resolve(); };
        tx.onerror = () => { resolve(); };
      });
    },

    // One cache's archive records. This is the whole reason main() can start
    // with a synchronous cache underneath it.
    hydrateCache(cacheKey) {
      this.cacheKey = cacheKey;
      this.records.clear();
      this.tables.clear();
      this.bytes = 0;
      return new Promise(resolve => {
        if (!this.db) { resolve(); return; }
        const tx = this.db.transaction(['groups'], 'readonly');
        const index = tx.objectStore('groups').index('by_cache');
        index.openCursor(IDBKeyRange.only(cacheKey)).onsuccess = ev => {
          const cursor = ev.target.result;
          if (!cursor) { return; }
          const row = cursor.value;
          const bytes = new Uint8Array(row.d);
          this.records.set(`${row.t}/${row.a}`, bytes);
          this.tables.set(row.t, (this.tables.get(row.t) || 0) + 1);
          this.bytes += bytes.length;
          cursor.continue();
        };
        tx.oncomplete = () => {
          this.hydrated = this.records.size;
          resolve();
        };
        tx.onerror = () => { resolve(); };
      });
    },

    // --- the four calls dat2_web_store.c makes ---------------------------

    select(cacheKey) {
      // The store is per-cache and hydrated for exactly one. Asking for another
      // is a boot that would silently read one generation's archives against
      // another's reference tables, so it fails rather than answers.
      return this.db !== null && this.cacheKey === cacheKey;
    },

    get(table, archive) {
      return this.records.get(`${table}/${archive}`) || null;
    },

    put(table, archive, bytes) {
      const key = `${table}/${archive}`;
      const previous = this.records.get(key);
      if (previous) { this.bytes -= previous.length; }
      else { this.tables.set(table, (this.tables.get(table) || 0) + 1); }
      this.records.set(key, bytes);
      this.bytes += bytes.length;
      this.pendingGroups.push({
        k: `${this.cacheKey}|${table}|${archive}`,
        c: this.cacheKey, t: table, a: archive, d: bytes
      });
      this.scheduleFlush();
      return true;
    },

    hasTable(table) {
      return (this.tables.get(table) || 0) > 0;
    },

    recordCount() { return this.records.size; },
    byteCount() { return this.bytes; },

    // --- boot configuration ----------------------------------------------
    //
    // Hydrated with everything else before main(), so boot.fetch — which runs
    // synchronously — can consult it without reaching the database.

    bootGet(path) {
      return this.bootFiles.get(path) || null;
    },

    bootPut(path, bytes, etag) {
      this.bootFiles.set(path, { bytes, etag: etag || null });
      this.pendingBoot.push({ k: path, d: bytes, e: etag || null });
      this.scheduleFlush();
    },

    // --- client-owned files ----------------------------------------------
    //
    // The player's saved options, and the plugin manifest and scripts. MEMFS
    // would forget them when the tab closes -- for the options that is the
    // same defect rs_prefs.c exists to fix one layer up, and for the plugins
    // it is why the roster came up holding only the C ones.

    fileGet(path) {
      const bytes = this.files.get(path);
      return bytes === undefined ? null : bytes;
    },

    /* The validator the stored bytes were fetched with, or null when there is
     * none -- a file the CLIENT wrote has no server copy to be still-current
     * against, and neither has one fetched before this was recorded. */
    fileEtag(path) {
      return this.fileEtags.get(path) || null;
    },

    filePut(path, bytes, etag) {
      this.files.set(path, bytes);
      this.fileEtags.set(path, etag || null);
      this.pendingFiles.push({ k: path, d: bytes, e: etag || null });
      this.scheduleFlush();
      return true;
    },

    /* Paths the server has already said it does not have, so a plugin that
     * asks for the same absent asset every frame costs one round trip and not
     * one per frame. Session-lived and never cleared: the shipped tree does
     * not grow under a running client, and a genuinely new file arrives the
     * way every other config change does -- on the next boot. */
    missing: new Set(),

    /*
     * The store MISSED, so ask the server -- the C side's second leg.
     *
     * This is the same two-route, revalidating request boot.fetch and
     * plugins.take already make, and it exists because those two only cover
     * what the page can NAME in advance: the manifest, the INIs it points at,
     * the scripts it lists. A plugin's assets are named by the plugin, at
     * runtime, in code the page never reads -- so the only thing that can ask
     * for one is the client, at the moment it wants it, through here.
     *
     * SYNCHRONOUS, and it has to be: this lane has no ASYNCIFY and it stands
     * in for a call that must answer before PlatformX_IO_LoadItem returns (the
     * same constraint that put the cache records in this object rather than in
     * the wasm heap). The cost is bounded by what it is used for -- a handful
     * of small files at plugin boot, each one written into the store on
     * arrival so it is never asked for twice.
     *
     * Three outcomes, and the third is the one the client acts on:
     *   {status:200} the bytes, from the server or revalidated against it
     *   {status:404} the server answered, and does not have it
     *   {status:0}   nothing answered -- the server is gone, which is a
     *                different thing from a file being absent
     */
    fileFetch(path) {
      if (this.missing.has(path)) { return { status: 404, bytes: null }; }

      const held = this.fileGet(path);
      const tag = held ? this.fileEtag(path) : null;
      let result = xhrSync('GET', `${bootUrl}/${path}`, null, tag);

      if (result.status === 404 || result.status === 0) {
        const direct = xhrSync('GET', `/${path}`, null, tag);
        if (direct.status === 200 || direct.status === 304) {
          result = direct;
        } else if (result.status === 0 && direct.status !== 0) {
          result = direct;
        }
      }

      if (result.status === 200) {
        this.filePut(path, result.bytes, result.etag);
        return { status: 200, bytes: result.bytes };
      }
      /* Unchanged since the copy already held -- which is the answer, and the
       * reason the validator went out at all. */
      if (result.status === 304 && held) { return { status: 200, bytes: held }; }
      /* Never the negative cache on a transport failure: the file may well be
       * there, and remembering "absent" would outlive the outage. */
      if (result.status === 0) { return { status: 0, bytes: null }; }

      this.missing.add(path);
      return { status: 404, bytes: null };
    },

    // --- write-behind -----------------------------------------------------

    scheduleFlush() {
      if (this.pendingGroups.length + this.pendingFiles.length +
          this.pendingBoot.length >= STORE_FLUSH_RECORDS) {
        this.flush();
        return;
      }
      if (this.flushTimer !== null) { return; }
      this.flushTimer = window.setTimeout(() => {
        this.flushTimer = null;
        this.flush();
      }, STORE_FLUSH_MS);
    },

    flush() {
      const groups = this.pendingGroups;
      const files = this.pendingFiles;
      const bootRows = this.pendingBoot;

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
        const tx = this.db.transaction(['groups', 'files', 'boot'], 'readwrite');
        const groupStore = tx.objectStore('groups');
        const fileStore = tx.objectStore('files');
        const bootStore = tx.objectStore('boot');
        groups.forEach(row => {
          // A copy, and a plain ArrayBuffer: the Uint8Array came out of the
          // wasm heap's buffer, which structured clone would either reject or
          // serialize whole.
          groupStore.put({ k: row.k, c: row.c, t: row.t, a: row.a, d: row.d.slice().buffer });
        });
        files.forEach(row => {
          fileStore.put({ k: row.k, d: row.d.slice().buffer, e: row.e });
        });
        bootRows.forEach(row => {
          bootStore.put({ k: row.k, d: row.d.slice().buffer, e: row.e });
        });
        tx.onerror = () => {
          // Quota, most likely. The records stay resident, so this session is
          // unaffected; what is lost is the warm start next time.
          if (this.writeErrors++ === 0) {
            const why = tx.error ? tx.error.name : 'unknown';
            log(`cache: IndexedDB refused a write (${why}) — this session is ` +
                'fine, but the cache will not persist', true);
          }
        };
      } catch (err) {
        if (this.writeErrors++ === 0) {
          log(`cache: could not write to IndexedDB — ${err.message}`, true);
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

  const IO_LOG_MAX = 200;

  const io = {
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

    // Tell the CLIENT what this object already knows.
    //
    // The client disables the parts of its UI that cannot work without the
    // server behind them — the plugin launcher, whose scripts, assets and
    // settings all arrive over /io — and it can only do that if the verdict
    // reaches it. Mirroring rather than exporting a second opinion: the page
    // is the only thing that ever sees a request fail to leave, so its own
    // counter is the source of truth and this just forwards the boolean.
    //
    // Guarded on the export existing so an older module (or the IndexedDB
    // lane, which has no wire at all) keeps working untouched.
    mirrorTransport(down) {
      if (!this.ready || typeof Module._torirs_io_transport_down !== 'function') { return; }
      try { Module._torirs_io_transport_down(down ? 1 : 0); } catch (err) { /* module gone */ }
    },

    // A batch came back. Whatever it says about the archives inside it, the
    // transport is alive — which is the one thing mirrorTransport reports.
    transportAlive() {
      if (this.transportDown) { log('io: the IO server is answering again'); }
      this.transportDown = 0;
      this.mirrorTransport(false);
    },

    // Take whatever the client queued and put it on the wire.
    //
    // Called from two places, and it has to be safe in both: once per client
    // frame (where it is usually a no-op), and from inside the client's
    // PlatformX_IO_Process when the pump is synchronous.
    take() {
      const len = Module._torirs_io_request_len();
      if (len <= 0) { return null; }
      try {
        const ptr = Module._torirs_io_request_ptr();
        // Copy before releasing: the wasm buffer is reused for the next batch.
        const batch = Module.HEAPU8.slice(ptr, ptr + len);
        Module._torirs_io_request_taken();
        return batch;
      } catch (err) {
        // Called from inside a wasm frame, so an escaping exception would
        // surface as a trap. Nothing was consumed, so the next pump retries.
        this.failures++;
        log(`io: could not read the request batch: ${err.message}`, true);
        return null;
      }
    },

    // Synchronous round trip, from inside PlatformX_IO_Process. Returns true
    // when the batch was delivered, false to let the client fall back to
    // waiting for the asynchronous path.
    pumpSync() {
      // `?io_sync=0` answers no here rather than by hiding this method, which
      // is what the wasm side's "the page decides whether it can do that" means
      // — false leaves the batch queued for the frame-gated path instead of
      // consuming it. Without this the flag disabled only the asynchronous
      // pump and left the blocking one running, which is the opposite of what
      // it reads like.
      if (!this.ready || !useSync) { return false; }
      const batch = this.take();
      if (!batch) { return true; }

      const started = performance.now();
      let reply = null;
      let failure = null;
      try {
        reply = xhrSyncBytes('POST', this.endpoint, batch);
        if (!reply) { failure = 'the server answered with an error status'; }
      } catch (err) {
        failure = err.message;
      }
      const elapsed = performance.now() - started;

      if (!reply) {
        this.failures++;
        this.reportTransportFailure(failure);
        Module._torirs_io_fail_pending();
        this.record(elapsed, 0, 0, true);
        return true;
      }
      this.transportAlive();
      this.batches++;
      this.deliver(reply);
      this.record(elapsed, reply.length, 0, false);
      return true;
    },

    // The request never reached the server, or the server never answered. That
    // is one condition, not one per archive: name it on the first failure with
    // what it usually means, then keep a count rather than repeating it.
    reportTransportFailure(message) {
      this.transportDown++;
      this.mirrorTransport(true);
      if (this.transportDown === 1) {
        log(`io: cannot reach the IO server at ${this.endpoint} — ${message}`, true);
        log('io: it was serving until now, so it has most likely stopped. Every ' +
            'cache read will fail until it is back; check the terminal it was ' +
            'started in.', true);
      } else if (this.transportDown % 100 === 0) {
        log(`io: still unreachable (${this.transportDown} failed reads)`, true);
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
    pump() {
      this.endFrame();
      if (!this.ready) { return; }

      const batch = this.take();
      if (!batch) { return; }

      const started = performance.now();
      const startFrame = this.frame;
      this.inflight++;
      this.batches++;

      fetch(this.endpoint, {
        method: 'POST',
        headers: { 'Content-Type': 'application/octet-stream' },
        body: batch,
        cache: 'no-store'
      })
        .then(response => {
          if (!response.ok) { throw new Error(`io server returned ${response.status}`); }
          return response.arrayBuffer();
        })
        .then(buffer => {
          this.inflight--;
          this.transportAlive();
          const bytes = new Uint8Array(buffer);
          this.deliver(bytes);
          this.record(performance.now() - started, bytes.length,
                      this.frame - startFrame, false);
        })
        .catch(err => {
          this.inflight--;
          this.failures++;
          this.reportTransportFailure(err.message);
          // Failing the outstanding reads is deliberate. A dropped request
          // leaves the task queue parked forever with nothing to show for it;
          // an errored slot makes the waiting task take its own failure path,
          // which at least says which archive died.
          Module._torirs_io_fail_pending();
          this.record(performance.now() - started, 0, this.frame - startFrame, true);
        });
    },

    deliver(bytes) {
      let ptr = 0;
      try {
        ptr = Module._torirs_io_response_alloc(bytes.length);
        if (!ptr) {
          log(`io: out of wasm memory for a ${bytes.length} byte response`, true);
          Module._torirs_io_fail_pending();
          return;
        }
        // Fetch the view AFTER the allocation: growing the heap replaces it.
        Module.HEAPU8.set(bytes, ptr);
        Module._torirs_io_response_submit(ptr, bytes.length);
      } catch (err) {
        this.failures++;
        log(`io: could not deliver a ${bytes.length} byte response: ${err.message}`, true);
        Module._torirs_io_fail_pending();
        return;
      }
      this.bytesIn += bytes.length;
    },

    record(ms, bytes, frames, failed) {
      this.frameIoMs += ms;
      this.frameIoReqs++;
      this.entries.push({
        frame: this.frame, ms, bytes, frames, failed
      });
      if (this.entries.length > IO_LOG_MAX) { this.entries.shift(); }
      this.entriesDirty = true;
    },

    // Close out the frame just ended and start the next. A frame that spent
    // more than one frame's worth of time in IO is the thing worth naming:
    // synchronously, that is a stalled main thread; asynchronously, it is a
    // read that outlived the frame that asked for it.
    endFrame() {
      if (this.frameIoReqs > 0) {
        if (this.frameIoMs > FRAME_MS) { this.framesOverrun++; }
        if (this.frameIoMs > this.slowestFrameMs) { this.slowestFrameMs = this.frameIoMs; }
      }
      this.frameIoMs = 0;
      this.frameIoReqs = 0;
      this.frame++;
    },

    stats() {
      // Absent on the IndexedDB build, which has no wire to count. Tested
      // rather than assumed: this runs every frame from the moment the runtime
      // is up, which is before the store has finished hydrating and so before
      // anything else can tell the two lanes apart.
      if (!this.ready || typeof Module._torirs_io_stats !== 'function') { return null; }
      if (!this.statsPtr) { this.statsPtr = Module._malloc(5 * 4); }
      Module._torirs_io_stats(this.statsPtr);
      const view = Module.HEAP32;
      const base = this.statsPtr >> 2;
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
  const Module = {};
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

  const js5 = {
    active: false,
    key: null,
    steps: 0,
    started: 0,

    // The manifest main() will open. Same scan main.c does: the first real
    // --manifest wins.
    manifestPath() {
      for (let i = 0; i < args.length; i++) {
        if (args[i] === '--manifest' && i + 1 < args.length) { return args[i + 1]; }
      }
      return null;
    },

    // Does this build have a cache of its own to prime? Answered before the
    // runtime finishes initializing, because the answer decides whether main()
    // starts on its own.
    wanted() {
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
    begin() {
      this.active = true;
      this.started = performance.now();

      store.open()
        .catch(err => {
          // A page in private browsing may have no IndexedDB at all. Boot
          // files then have no stored copy and are simply fetched, which is
          // what the harness did before any of this existed.
          const why = err && err.message ? err.message : err;
          log(`cache: IndexedDB is unavailable (${why}) — nothing will persist`, true);
        })
        .then(() => store.hydrateShared())
        .then(() => {
          boot.load();
          if (boot.loaded.length) { log(`torirs: boot files ${boot.loaded.join(' ')}`); }
          if (boot.revalidated || boot.offline) {
            const offline = boot.offline
              ? `, ${boot.offline} from the offline copy`
              : '';
            log(`torirs: boot config — ${boot.revalidated} unchanged${offline}`);
          }
          if (boot.missing.length) {
            log(`torirs: boot files NOT available: ${boot.missing.join(' ')}`, true);
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
          if (this.wanted()) {
            plugins.load();
            if (plugins.stored || plugins.fetched) {
              log(`torirs: plugins — ${plugins.stored} stored, ${plugins.fetched} fetched`);
            }
            if (plugins.missing.length) {
              log(`torirs: plugin files NOT available: ${plugins.missing.join(' ')}`, true);
            }
          }
          if (!this.wanted()) { this.finish(); return null; }
          return this.beginCache();
        })
        .catch(err => {
          log(`torirs: boot failed — ${err && err.message ? err.message : err}`, true);
          this.finish();
        });
    },

    beginCache() {
      const manifest = this.manifestPath();

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

      return Promise.resolve(cacheReset ? store.clear(this.key) : 0)
        .then(cleared => {
          if (cleared) { log(`cache: cleared ${cleared} record(s) for ${this.key}`); }
          return store.hydrateCache(this.key);
        })
        .then(() => {
          const cold = store.recordCount() === 0 ? ' (cold start)' : '';
          log(`cache: ${this.key} — ${store.recordCount()} record(s), ` +
              `${kb(store.byteCount())} resident${cold}`);
          if (Module.ccall('torirs_web_cache_prime_begin', 'number',
                           ['string', 'number'], [js5Host, js5Port]) !== 0) {
            this.fail('could not start the JS5 primer');
            return;
          }
          this.step();
        });
    },

    // One tick per turn of the event loop. setTimeout(0) rather than a tight
    // loop is the entire point: it is what lets the WebSocket deliver.
    step() {
      const status = Module._torirs_web_cache_prime_step();
      this.steps++;

      if (status === 0) {
        if (this.steps % 200 === 0) { this.report(); }
        window.setTimeout(() => { this.step(); }, 0);
        return;
      }
      if (status < 0) {
        this.report();
        this.fail(this.diagnose());
        return;
      }
      log(`cache: metadata ready in ${((performance.now() - this.started) / 1000).toFixed(1)}s`);
      this.finish();
    },

    report() {
      if (typeof Module._torirs_web_cache_prime_stats !== 'function') { return; }
      const ptr = Module._malloc(5 * 4);
      Module._torirs_web_cache_prime_stats(ptr);
      const view = Module.HEAP32;
      const base = ptr >> 2;
      setStatus(`cache: priming from ws://${js5Host}:${js5Port} — ` +
                `${view[base + 1]} reference tables, ${kb(view[base + 2])} received`);
      Module._free(ptr);
    },

    // Two failures look identical from the page and have opposite fixes: no
    // server there, versus a server whose cache the client will not accept.
    // Whether any bytes arrived separates them, so say which one it was rather
    // than printing the "start a server" advice at someone whose server is
    // running and answering.
    diagnose() {
      if (typeof Module._torirs_web_cache_prime_stats !== 'function') { return null; }
      const ptr = Module._malloc(5 * 4);
      Module._torirs_web_cache_prime_stats(ptr);
      const base = ptr >> 2;
      const bytes = Module.HEAP32[base + 2];
      const error = Module.HEAP32[base + 4];
      Module._free(ptr);
      if (bytes > 0) {
        return { answered: true, bytes, error };
      }
      return { answered: false, bytes, error };
    },

    fail(why) {
      if (why && why.answered) {
        // JS5_ERROR_* from src/js5/js5.h. 11 is REFERENCE, which for a cache
        // this repo packed itself is nearly always the 16-bit ceiling: the JS5
        // request carries a 2-byte group id, so a table holding ids at or above
        // 65536 cannot be served by this protocol at all.
        const reference = why.error === 11
          ? ' (reference table): a table almost certainly holds group ids past ' +
            '65535, which a 4-byte JS5 request cannot address'
          : '';
        log(`cache: the JS5 server answered (${kb(why.bytes)}) but the client ` +
            `rejected its metadata — JS5 error ${why.error}${reference}`, true);
        this.finish();
        return;
      }
      const said = typeof why === 'string' ? why : 'the JS5 server did not answer';
      log(`cache: ${said}. Start one with:`, true);
      log('  make -C src js5-server && ./src/build_opt/js5_server --cache <dir> ' +
          `--revision <rev> --port ${js5Port}`, true);
      this.finish();
    },

    // Start main(), whether the prime succeeded or not.
    //
    // Running it after a failure is deliberate: the client then reports a cache
    // it cannot read, which says what is wrong. Leaving the page on "loading…"
    // instead is indistinguishable from a hang, and the two have completely
    // different fixes.
    finish() {
      if (!this.active) { return; }
      this.active = false;
      try {
        Module.callMain(args);
      } catch (err) {
        log(`torirs: main() did not start — ${err && err.message ? err.message : err}`, true);
      }
    }
  };

  // ------------------------------------------------------------------- UI

  function renderIoLog() {
    if (!io.entriesDirty) { return; }
    io.entriesDirty = false;
    if (!ioLogEl) { ioLogEl = document.getElementById('iolog'); }
    if (!ioLogEl) { return; }

    const rows = io.entries.slice(-60).reverse().map(e => {
      const cls = e.failed ? 'bad' : (e.ms > FRAME_MS || e.frames > 0 ? 'slow' : '');
      const span = e.frames > 0 ? ` · ${e.frames}f` : '';
      return `<div class="${cls}">` +
        `<span class="f">f${e.frame}</span>` +
        `<span class="t">${e.ms.toFixed(1)}ms</span>` +
        `<span class="b">${e.failed ? 'FAILED' : kb(e.bytes)}</span>` +
        `<span class="s">${span}</span>` +
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
      const errors = store.writeErrors
        ? `  ·  ${store.writeErrors} write errors`
        : '';
      setStatus(
        `heap ${heapMb()}MB` +
        `  ·  cache ${store.cacheKey}` +
        `  ${store.recordCount()} records` +
        `  ${kb(store.byteCount())}` +
        `  ·  hydrated ${store.hydrated}` +
        `  written ${store.recordCount() - store.hydrated}` +
        errors
      );
      renderIoLog();
      window.requestAnimationFrame(hostFrame);
      return;
    }

    const stats = io.stats();
    if (stats) {
      setStatus(
        `heap ${heapMb()}MB` +
        `  ·  io ${useSync ? 'sync-boot' : 'async'}` +
        `  req ${stats.requests}` +
        `  hit ${stats.cacheHits}` +
        `  done ${stats.completed}` +
        `  fail ${stats.failed + io.failures}` +
        `  pending ${stats.pending}` +
        `  ·  ${kb(io.bytesIn)} in ${io.batches} batches` +
        `  ·  slow frames ${io.framesOverrun}` +
        ` (worst ${io.slowestFrameMs.toFixed(0)}ms)`
      );
    }
    renderIoLog();
    window.requestAnimationFrame(hostFrame);
  }

  // --------------------------------------------------------------- Module

  Module.arguments = args;
  Module.canvas = (() => {
    const canvas = document.getElementById('canvas');
    // Without this a lost WebGL context fails silently and the page just
    // stops painting, which looks exactly like a hung client.
    canvas.addEventListener('webglcontextlost', e => {
      log('webgl context lost', true);
      e.preventDefault();
    }, false);
    return canvas;
  })();

  Module.print = text => { log(text, false); };
  Module.printErr = text => { log(text, true); };

  Module.preRun = [() => {
    readEnv().forEach(pair => { ENV[pair[0]] = pair[1]; });
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
  window.addEventListener('pagehide', () => { store.flush(); });

  Module.onRuntimeInitialized = () => {
    io.ready = true;
    if (store.cacheKey) {
      log(`torirs: runtime up, cache in IndexedDB, js5 ws://${js5Host}:${js5Port}`);
    } else {
      const mode = useSync
        ? 'synchronous while booting, frame-gated once live'
        : 'frame-gated';
      log(`torirs: runtime up, io endpoint ${io.endpoint} (${mode})`);
    }
    log(`torirs: argv ${JSON.stringify(args)}`);
    // Which cache the server has open. Changing the manifest in the URL
    // changes the client but not the server, and a client booting one
    // generation against another's cache just fails to decode anything —
    // so both halves say what they are, side by side, before that happens.
    fetch(statsUrl, { cache: 'no-store' })
      .then(r => r.ok ? r.text() : '')
      .then(text => {
        const match = /serving=(.*?)\s+served=/.exec(text);
        if (match) { log(`torirs: io server has ${match[1]} open`); }
      })
      .catch(() => { /* older server, or none: not worth a message */ });
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
  let memtraceBytes = null;

  function memtraceRead() {
    if (memtraceBytes) { return memtraceBytes; }
    Module._torirs_memtrace_web_flush();
    // UTF8ToString is not in EXPORTED_RUNTIME_METHODS; HEAPU8 is, and the path
    // is plain ASCII, so read the C string out of the heap directly.
    let addr = Module._torirs_memtrace_web_path();
    let path = '';
    while (Module.HEAPU8[addr]) { path += String.fromCharCode(Module.HEAPU8[addr++]); }
    memtraceBytes = Module.FS.readFile(path);
    log(`memtrace: flushed ${memtraceBytes.length} bytes from ${path} (recording has stopped)`);
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
    let bytes;
    try {
      bytes = memtraceRead();
    } catch (e) {
      log(`memtrace: cannot read the trace — ${e}`, true);
      return;
    }

    const viewer = window.open('./viewer.html', '_blank');
    if (!viewer) {
      log('memtrace: the viewer tab was blocked — allow popups for this site, ' +
          'or use "save .bin" and open viewer.html by hand', true);
      return;
    }

    const origin = window.location.origin;
    let acked = false;
    let retries = 0;
    let timer = null;

    function send() {
      if (acked) { return; }
      // A transfer neuters the buffer, so each attempt sends its own copy —
      // otherwise a retry would post an empty one.
      const copy = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
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
    timer = window.setInterval(() => {
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
    let bytes;
    try {
      bytes = memtraceRead();
    } catch (e) {
      log(`memtrace: cannot read the trace — ${e}`, true);
      return;
    }
    const url = URL.createObjectURL(new Blob([bytes], { type: 'application/octet-stream' }));
    const link = document.createElement('a');
    link.href = url;
    link.download = 'memtrace.bin';
    link.click();
    URL.revokeObjectURL(url);
    log('memtrace: saved memtrace.bin — summarize with ' +
        'python3 tools/memtrace/summarize.py memtrace.bin');
  }

  function installMemtraceButton() {
    const controls = document.getElementById('memtrace');
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

  Module.setStatus = text => { if (text) { setStatus(text); } };
})();
