// Host harness for the emscripten build of src/.
//
// Two jobs, and they are the two things a browser has to supply that a desktop
// host gets for free:
//
//   1. Boot parameters. main() still parses argv and reads getenv, so the page
//      turns its query string into Module.arguments and ENV rather than
//      inventing a second configuration path.
//
//   2. The IO pump. The client has no disk: its cache reads pile up in wasm
//      memory as an encoded batch and stay there until something carries them
//      to the IO server. Nothing inside wasm can do that — fetch resolves on a
//      later turn of the event loop, which the client's frame cannot wait for —
//      so the pump lives here and runs once per frame.
//
// Load this BEFORE torirs.js: it defines the Module object the runtime reads.
//
// Query string:
//   ?args=--manifest,manifest_rs254.ini,--offline   argv after argv[0]
//   ?env=TORIRS_TASK_LOG=1;TORIRS_NET_DEBUG=1       environment variables
//   ?io=http://host:port/io                         IO endpoint (default /io)

(function () {
  'use strict';

  var params = new URLSearchParams(window.location.search);

  var DEFAULT_ARGS = ['--manifest', 'manifest_rs254.ini', '--offline'];
  var args = params.has('args')
    ? params.get('args').split(',').filter(function (s) { return s.length > 0; })
    : DEFAULT_ARGS;

  var ioUrl = params.get('io') || '/io';

  var logEl = null;
  var statusEl = null;
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

  // ------------------------------------------------------------------ IO pump
  //
  // The wasm side owns the encoding; this only moves bytes. Every heap view is
  // re-read from Module on each use because ALLOW_MEMORY_GROWTH detaches and
  // replaces the old ArrayBuffer, and a view cached across an allocation would
  // silently write into freed memory.

  var io = {
    endpoint: ioUrl,
    inflight: 0,
    batches: 0,
    records: 0,
    bytesIn: 0,
    failures: 0,
    ready: false,
    statsPtr: 0,

    // Take whatever the client queued and put it on the wire. Safe to call
    // when nothing is queued (the common case) — one exported call and out.
    pump: function () {
      if (!this.ready) { return; }

      var len = Module._torirs_io_request_len();
      if (len <= 0) { return; }

      var ptr = Module._torirs_io_request_ptr();
      // Copy before releasing: the wasm buffer is reused for the next batch.
      var batch = Module.HEAPU8.slice(ptr, ptr + len);
      Module._torirs_io_request_taken();

      var self = this;
      this.inflight++;
      this.batches++;

      fetch(this.endpoint, {
        method: 'POST',
        headers: { 'Content-Type': 'application/octet-stream' },
        body: batch,
        cache: 'no-store'
      })
        .then(function (response) {
          if (!response.ok) {
            throw new Error('io server returned ' + response.status);
          }
          return response.arrayBuffer();
        })
        .then(function (buffer) {
          self.deliver(new Uint8Array(buffer));
          self.inflight--;
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
        });
    },

    deliver: function (bytes) {
      var ptr = Module._torirs_io_response_alloc(bytes.length);
      if (!ptr) {
        log('io: out of wasm memory for a ' + bytes.length + ' byte response', true);
        Module._torirs_io_fail_pending();
        return;
      }
      // Fetch the view AFTER the allocation: it may have grown the heap.
      Module.HEAPU8.set(bytes, ptr);
      Module._torirs_io_response_submit(ptr, bytes.length);
      this.records++;
      this.bytesIn += bytes.length;
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

  // The frame-aligned pump. The C frame calls into this too (so a request never
  // waits longer than it must), but the harness drives its own animation frame
  // as well: the wasm loop can be blocked on exactly the IO this pump delivers,
  // and a pump that only runs when the client runs would deadlock there.
  function hostFrame() {
    io.pump();

    var stats = io.stats();
    if (stats) {
      setStatus(
        'io  req ' + stats.requests +
        '  hit ' + stats.cacheHits +
        '  done ' + stats.completed +
        '  fail ' + (stats.failed + io.failures) +
        '  pending ' + stats.pending +
        '  batches ' + io.batches +
        '  in ' + (io.bytesIn / 1048576).toFixed(1) + 'MB'
      );
    }
    window.requestAnimationFrame(hostFrame);
  }

  // --------------------------------------------------------------- the Module

  var Module = {
    arguments: args,
    canvas: (function () {
      var canvas = document.getElementById('canvas');
      // Without this a lost WebGL context fails silently and the page just
      // stops painting, which looks exactly like a hung client.
      canvas.addEventListener('webglcontextlost', function (e) {
        log('webgl context lost', true);
        e.preventDefault();
      }, false);
      return canvas;
    })(),

    print: function (text) { log(text, false); },
    printErr: function (text) { log(text, true); },

    preRun: [function () {
      var env = params.get('env');
      if (env) {
        env.split(';').forEach(function (pair) {
          var eq = pair.indexOf('=');
          if (eq > 0) {
            ENV[pair.substring(0, eq)] = pair.substring(eq + 1);
          }
        });
      }
    }],

    onRuntimeInitialized: function () {
      io.ready = true;
      log('torirs: runtime up, io endpoint ' + io.endpoint);
      log('torirs: argv ' + JSON.stringify(args));
      window.requestAnimationFrame(hostFrame);
    },

    setStatus: function (text) {
      if (text) { setStatus(text); }
    }
  };

  // The wasm side reaches the pump through this (see PlatformXIO_Web_Pump).
  Module.torirsIO = io;

  window.Module = Module;
})();
