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
  /* io_server's origin, which is also where the dat1 proxy answers. Derived
   * from the one endpoint the page was given rather than configured again, so
   * all four routes are the same server by construction. */
  const cacheBase = ioUrl.replace(/\/io$/, '');

  // Where the IndexedDB build reaches JS5. Defaults to this page's host, which
  // is right when the client and the cache server are served from one machine
  // (the usual local setup) and overridable when they are not.
  const js5Host = params.get('js5_host') || window.location.hostname || 'localhost';
  const js5Port = parseInt(params.get('js5_port') || '43594', 10);
  /*
   * There is no dat1 endpoint to configure here, and that is the point.
   *
   * A page cannot reach a LostCity cache: the on-demand protocol is raw TCP on
   * the game port, and the jag archives are HTTP with no CORS. io_server holds
   * that client on the page's behalf and answers over its own origin, so where
   * the SERVER is stays where it belongs -- the manifest's `[net:boot]`, read
   * by io_server, which is also the process that has to reach it.
   */

  /*
   * Which cache this manifest names, and which generation it is.
   *
   * Read here rather than asked of C, because there is no longer any C to ask:
   * both producers are JavaScript and the metadata barrier they needed is
   * gone. The cache key separates one generation's groups from another's in
   * the database; the revision is what the JS5 handshake announces (a mismatch
   * is answered with status 6, so guessing it is not an option).
   *
   * Nothing about the SERVER is read here. Where a dat1 cache comes from is
   * io_server's business, off the same manifest -- see onDemandProducer.
   *
   * The manifest is fetched by boot.load below in any case, so this is a second
   * read of bytes the page already has rather than a second request.
   */
  let cacheKey = '';
  let cacheRevision = 0;

  function readBootIdentity(bytes) {
    const text = new TextDecoder('utf-8', { fatal: false }).decode(bytes);
    let section = '';
    text.split('\n').forEach(raw => {
      const line = raw.replace(/\r$/, '').replace(/^[ \t]+/, '');
      if (line.charAt(0) === ';' || line.charAt(0) === '#') { return; }
      if (line.charAt(0) === '[') {
        const close = line.indexOf(']');
        if (close > 0) { section = line.substring(1, close); }
        return;
      }
      if (section !== 'cache:boot') { return; }
      const eq = line.indexOf('=');
      if (eq < 0) { return; }
      const key = line.substring(0, eq).trim();
      const value = line.substring(eq + 1).trim();
      if (key === 'dir') { cacheKey = value; }
      else if (key === 'revision') { cacheRevision = parseInt(value, 10) || 0; }
    });
  }

  /* An explicit ?js5_revision= wins, for a server announcing something the
   * manifest does not state. */
  const js5Revision = () =>
    parseInt(params.get('js5_revision') || '', 10) || cacheRevision;
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
    async fetch(path) {
      const cached = await ToriRS_IDB.bootGet(path);
      let bytes = null;
      let sawResponse = false;

      for (const url of [`${bootUrl}/${path}`, `/${path}`]) {
        let response;
        try {
          const headers = cached && cached.etag ? { 'If-None-Match': cached.etag } : undefined;
          response = await fetch(url, { cache: 'no-store', headers });
        } catch (err) {
          /* Network-level failure on this route. The other may still answer. */
          continue;
        }
        sawResponse = true;
        if (response.status === 304 && cached) {
          this.write(path, cached.bytes);
          this.loaded.push(`${path} (unchanged)`);
          this.revalidated++;
          return cached.bytes;
        }
        if (response.ok) {
          bytes = new Uint8Array(await response.arrayBuffer());
          await ToriRS_IDB.bootPut(path, bytes, response.headers.get('ETag'));
          this.write(path, bytes);
          this.loaded.push(path + (cached ? ' (changed)' : ''));
          return bytes;
        }
      }

      if (cached) {
        /* Nothing answered, or the server no longer has it. Either way this page
         * holds a copy and refusing to boot would help nobody. */
        this.write(path, cached.bytes);
        this.loaded.push(`${path} (offline copy)`);
        if (!sawResponse) { this.offline++; }
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

    async load() {
      const seen = {};

      const take = async (path) => {
        if (!path || path.charAt(0) === '/' || seen[path]) { return null; }
        seen[path] = true;
        return await this.fetch(path);
      };

      function argumentTakesValue(argument) {
        return argument === '--manifest' || argument === '--port' ||
          argument === '--revconfig' || argument === '--revconfig-cache' ||
          argument === '--connect' || argument === '--user' ||
          argument === '--pass' || argument === '--rev' ||
          argument === '--windowmode' || argument === '--window';
      }

      async function takeArgFiles(argv) {
        for (let j = 0; j < argv.length; j++) {
          const flag = argv[j];
          const value = argv[j + 1];
          if ((flag === '--revconfig' || flag === '--revconfig-cache') &&
              j + 1 < argv.length) {
            await take(value);
            // main.c probes for a sibling _cache.ini when only the _ui.ini was
            // given; fetch it so the probe finds it or honestly does not.
            if (/_ui\.ini$/.test(value)) { await take(value.replace(/_ui\.ini$/, '_cache.ini')); }
          }
          if (argumentTakesValue(flag) && j + 1 < argv.length) { j++; }
        }
      }

      for (let i = 0; i < args.length; i++) {
        const flag = args[i];
        const value = args[i + 1];
        if (flag === '--manifest' && i + 1 < args.length) {
          const bytes = await take(value);
          if (bytes) {
            /* Which cache, and which generation. Both are needed before the
             * first read: the key addresses the records, the revision is what
             * the JS5 handshake announces. */
            readBootIdentity(bytes);
            const dir = value.includes('/') ? value.replace(/\/[^/]*$/, '/') : '';
            for (const rel of this.revconfigPaths(bytes)) { await take(dir + rel); }
            // Additional CLI paths keep CLI/CWD semantics; unlike typed
            // revconfig keys they are not relative to the manifest directory.
            await takeArgFiles(this.clientArgs(bytes));
          }
          break; // main.c bootstraps from the first real --manifest option.
        }
        if (argumentTakesValue(flag) && i + 1 < args.length) { i++; }
      }
      await takeArgFiles(args);
    }
  };

  // --------------------------------------------------------------- boot
  //
  // main() is started here rather than by the runtime, because preRun set
  // noInitialRun.
  //
  // THERE IS NO METADATA BARRIER ANY MORE. There was one, and it was load
  // bearing: App_Init opened a dat2 disk and decoded its reference tables
  // itself, so JS5 had to have installed them before main() ran, and the wait
  // could not happen inside main() because a WebSocket cannot deliver to a
  // thread that is spinning on it.
  //
  // Both halves of that are gone. A browser opens no cache now -- the platform
  // IO executor answers cache reads out of the database -- and a reference
  // table is fetched on demand like any other container, by an executor that
  // can await it. So there is nothing to install before main() and nothing to
  // wait for.
  const clientBoot = {
    async start() {
      /*
       * Stage the files main() opens BY NAME before calling it.
       *
       * These are the one thing that does not go through the IO queue: the boot
       * manifest and the RevConfig INIs it names are read with fopen, against
       * MEMFS, which starts empty. Everything else the client reads is a queue
       * item the platform executor answers, and needs no staging at all -- the
       * plugin manifest, its scripts and its assets used to be pre-staged here
       * and are not any more, because the executor fetches each on the read
       * that wants it.
       *
       * Awaited rather than raced: main() parses the manifest almost
       * immediately, and a file still in flight is one main() will not find.
       */
      try {
        await boot.load();
        if (boot.loaded.length) { log(`torirs: boot files ${boot.loaded.join(' ')}`); }
        if (boot.missing.length) {
          log(`torirs: boot files MISSING ${boot.missing.join(' ')} — main() will ` +
              'not find them', true);
        }
        if (boot.offline) {
          log(`torirs: ${boot.offline} boot file(s) served from the stored copy; ` +
              'the config server did not answer', true);
        }
      } catch (err) {
        log(`torirs: boot files could not be staged — ${err && err.message}`, true);
      }

      try {
        Module.callMain(args);
      } catch (err) {
        log(`torirs: main() did not start — ${err && err.message ? err.message : err}`, true);
      }
    },
  };


  // ------------------------------------------------------------------- UI

  function heapMb() {
    try { return (Module.HEAPU8.length / 1048576).toFixed(0); } catch (err) { return '?'; }
  }

  /*
   * The harness's own animation frame.
   *
   * It pumps nothing. It used to: the wire lane parked its request batch in
   * wasm memory and needed somebody to carry it, and a pump that only ran when
   * the client ran could deadlock waiting on IO the client was blocked on. The
   * platform executor awaits its own reads now, so a frame here is only a
   * status line.
   */
  function hostFrame() {
    const js5 = js5Client ? js5Client.stats() : null;
    const od = onDemandClient ? onDemandClient.stats() : null;

    setStatus(
      `heap ${heapMb()}MB` +
      (cacheKey ? `  ·  cache ${cacheKey}` : '') +
      (js5 ? `  ·  js5 ${js5.groups} groups ${kb(js5.bytes)}` +
             (js5.inflight ? ` (${js5.inflight} in flight)` : '') +
             (js5.failed ? '  ·  JS5 DOWN' : '') : '') +
      (od ? `  ·  ondemand ${od.files} files ${kb(od.bytes)}` +
            (od.failed ? '  ·  ONDEMAND DOWN' : '') : '')
    );
    window.requestAnimationFrame(hostFrame);
  }

  // --------------------------------------------------------------- Module

  /*
   * The object the emscripten runtime reads.
   *
   * torirs.js is a plain script (-sMODULARIZE=0), and its first act is to
   * adopt a `Module` global if one is already there. This file is loaded
   * before it for exactly that reason, so defining it is this file's job --
   * every hook below hangs off this object.
   */
  const Module = {};
  window.Module = Module;

  /*
   * main() is this file's to start, and the flag saying so has to be set HERE.
   *
   * Everything main() opens by name -- the boot manifest, the RevConfig INIs it
   * names -- is staged into MEMFS asynchronously by clientBoot.start, so the
   * runtime must init and then STOP, leaving main() to be called once the
   * staging is done.
   *
   * Not in preRun, which is where this used to be and where it does nothing.
   * The generated runtime reads the flag at module scope, before run():
   *
   *     var shouldRunNow = true;
   *     if (Module["noInitialRun"]) shouldRunNow = false;
   *     run();                       // <- preRun executes in here
   *
   * so a preRun assignment lands after the decision it is trying to make.
   * main() then ran with an empty filesystem, printed "bootmanifest: cannot
   * open", and booted an unconfigured client -- no [net:boot], therefore not
   * networked, therefore the offline default of map 50,50 loaded and the
   * gameframe mounted against nothing. That is what a manifest-less boot looks
   * like, and it looks nothing like the missing flag that caused it. Worse,
   * clientBoot.start then called callMain a second time, so a run that
   * appeared to work was two mains deep.
   */
  Module.noInitialRun = true;

  /*
   * The two cache producers, built on the first read that needs one.
   *
   * Lazy, not eager: a boot uses exactly one of them -- which one is decided
   * by the container the client phrases its read in (dat2 -> JS5, dat1 -> the
   * 2004 on-demand protocol) -- and building both up front would open a
   * second WebSocket to a port nothing is listening on, whose failure reads
   * as an outage rather than as the unused half it is.
   */
  let js5Client = null;
  let onDemandClient = null;

  const js5Producer = () => {
    if (!js5Client) {
      js5Client = window.ToriRS_CreateJs5(js5Host, js5Port, js5Revision());
    }
    return js5Client;
  };

  const onDemandProducer = () => {
    if (!onDemandClient) {
      onDemandClient = window.ToriRS_CreateOnDemand(cacheBase);
    }
    return onDemandClient;
  };

  /*
   * Where the platform IO executor gets its bytes
   * (platform/platform_web_io.js reads it as Module.torirsHostIO).
   *
   * This file supplies the deployment knowledge -- which database, which
   * server, which producer -- and torirs_hostio.js supplies the queue
   * mechanics. Guarded so the page still loads, and can still say what is
   * wrong, when torirs_hostio.js was not included.
   *
   * cacheKey is passed as a function: the provider is built here, while the
   * page is still assembling Module, and which generation the archives belong
   * to is not read until boot.load parses the manifest.
   */
  if (typeof window.ToriRS_CreateHostIO === 'function') {
    Module.torirsHostIO = window.ToriRS_CreateHostIO(
      () => cacheKey, bootUrl, { js5: js5Producer, onDemand: onDemandProducer });
  }

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

  /* The environment, which is all preRun is for here: it runs before
   * initRuntime, so nothing in it may call into wasm, and the one flag that
   * used to live here had to move -- see Module.noInitialRun above. */
  Module.preRun = [() => {
    readEnv().forEach(pair => { ENV[pair[0]] = pair[1]; });
  }];

  /* Nothing to flush on the way out. Records are written straight through to
   * IndexedDB as they arrive (torirs_idb.js), so a tab that goes away loses
   * nothing -- which is what the write-behind batch used to risk. */

  Module.onRuntimeInitialized = () => {
    log(`torirs: runtime up, cache in IndexedDB` +
        (cacheKey ? ` (${cacheKey})` : '') +
        `, js5 ws://${js5Host}:${js5Port}`);
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
    clientBoot.start();
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
