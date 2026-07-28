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
//   ?arg=--manifest&arg=manifest_rs254.ini&arg=--offline   one arg per param
//   ?args=--manifest,manifest_rs254.ini,--offline          same, comma-joined
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
// So the default pump is synchronous, and runs from inside the client's
// PlatformX_IO_Process: requests go out and data comes back before Process
// returns, exactly as the native backend behaves. A boot then costs a handful
// of frames rather than hundreds. The cost is a blocked main thread while it
// happens, which is why the IO log below reports what each frame spent.
//
// ?io_sync=0 falls back to fetch(), which does not block but is frame-gated.
// The client supports both: PlatformX_IO_Pending is what tells its scheduler
// whether a read is still outstanding.

(function () {
  'use strict';

  var params = new URLSearchParams(window.location.search);

  var DEFAULT_ARGS = ['--manifest', 'manifest_rs254.ini', '--offline'];

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
    var xhr = new XMLHttpRequest();
    xhr.open(method, url, false);
    xhr.overrideMimeType('text/plain; charset=x-user-defined');
    if (body) { xhr.setRequestHeader('Content-Type', 'application/octet-stream'); }
    xhr.send(body || null);
    if (xhr.status !== 200) { return null; }
    var text = xhr.responseText;
    var out = new Uint8Array(text.length);
    for (var i = 0; i < text.length; i++) { out[i] = text.charCodeAt(i) & 0xFF; }
    return out;
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

    write: function (path, bytes) {
      var parts = path.split('/').filter(function (s) { return s.length > 0; });
      var dir = '';
      for (var i = 0; i < parts.length - 1; i++) {
        dir += '/' + parts[i];
        try { Module.FS.mkdir(dir); } catch (err) { /* already there */ }
      }
      Module.FS.writeFile('/' + parts.join('/'), bytes);
    },

    fetch: function (path) {
      var bytes = xhrSyncBytes('GET', bootUrl + '/' + path);
      if (!bytes) { this.missing.push(path); return null; }
      this.write(path, bytes);
      this.loaded.push(path);
      return bytes;
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

    load: function () {
      var self = this;
      var seen = {};

      function take(path) {
        if (!path || path.charAt(0) === '/' || seen[path]) { return null; }
        seen[path] = true;
        return self.fetch(path);
      }

      for (var i = 0; i < args.length - 1; i++) {
        var flag = args[i];
        var value = args[i + 1];
        if (flag === '--manifest') {
          var bytes = take(value);
          if (bytes) {
            var dir = value.indexOf('/') >= 0 ? value.replace(/\/[^/]*$/, '/') : '';
            self.revconfigPaths(bytes).forEach(function (rel) { take(dir + rel); });
          }
        } else if (flag === '--revconfig' || flag === '--revconfig-cache') {
          take(value);
          // main.c probes for a sibling _cache.ini when only the _ui.ini was
          // given; fetch it so the probe finds it or honestly does not.
          if (/_ui\.ini$/.test(value)) { take(value.replace(/_ui\.ini$/, '_cache.ini')); }
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
      if (!this.ready) { return false; }
      var batch = this.take();
      if (!batch) { return true; }

      var started = performance.now();
      var reply;
      try {
        reply = xhrSyncBytes('POST', this.endpoint, batch);
      } catch (err) {
        reply = null;
        log('io: synchronous request failed: ' + err.message, true);
      }
      var elapsed = performance.now() - started;

      if (!reply) {
        this.failures++;
        Module._torirs_io_fail_pending();
        this.record(elapsed, 0, 0, true);
        return true;
      }
      this.batches++;
      this.deliver(reply);
      this.record(elapsed, reply.length, 0, false);
      return true;
    },

    // Frame-gated fallback: does not block, but a frame satisfies one read.
    pump: function () {
      this.endFrame();
      if (!this.ready || useSync) { return; }

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
          var bytes = new Uint8Array(buffer);
          self.deliver(bytes);
          self.record(performance.now() - started, bytes.length,
                      self.frame - startFrame, false);
        })
        .catch(function (err) {
          self.inflight--;
          self.failures++;
          log('io: request failed: ' + err.message, true);
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
      if (!this.ready) { return null; }
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

  // The wasm side reaches both pumps through this (see platform_x_io_web.c).
  var Module = {};
  window.Module = Module;
  Module.torirsIO = io;

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

    var stats = io.stats();
    if (stats) {
      setStatus(
        'heap ' + heapMb() + 'MB' +
        '  ·  io ' + (useSync ? 'sync' : 'async') +
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
    // Before main(), because main() opens the manifest on its first pass.
    boot.load();
  }];

  Module.onRuntimeInitialized = function () {
    io.ready = true;
    log('torirs: runtime up, io endpoint ' + io.endpoint +
        ' (' + (useSync ? 'synchronous' : 'frame-gated') + ')');
    log('torirs: argv ' + JSON.stringify(args));
    if (boot.loaded.length) { log('torirs: boot files ' + boot.loaded.join(' ')); }
    if (boot.missing.length) {
      log('torirs: boot files NOT on the server: ' + boot.missing.join(' '), true);
    }
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
    window.requestAnimationFrame(hostFrame);
  };

  Module.setStatus = function (text) { if (text) { setStatus(text); } };
})();
