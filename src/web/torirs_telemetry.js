/*
 * torirs_telemetry.js -- the page's black box.
 *
 * WHY. A tablet has no console. When Safari on an iPad hangs on a world
 * rebuild, paints a gray screen, or kills the tab and reloads it ("A problem
 * repeatedly occurred"), nothing the client printed survives and nobody saw
 * it. This file keeps a ring of what happened, stores it on the device as it
 * goes, and posts it to io_server's /telemetry, which appends it to
 * logs/telemetry/<date>.jsonl (tools/web/telemetry_report.py reads them).
 *
 * WHAT IT RECORDS
 *   device       user agent, screen, dpr, cores -- once per session
 *   gl_context   every WebGL context made: version, attributes, GPU strings,
 *                limits. Each context's resources are TALLIED (buffers,
 *                textures, renderbuffers, in bytes) by wrapping the calls
 *                that make and free them, and a large upload is timed: on
 *                WebKit a buffer upload crosses to the GPU process.
 *   gl_*         context lost / restored / creation error; glGetError
 *                results (the first few); a big upload that took long
 *   stall        a gap between animation frames while the page is visible.
 *                The wasm main loop runs on rAF, so this is the client not
 *                coming back -- a hitch, or the start of a hang.
 *   heap_grow    the wasm heap grew (it never shrinks)
 *   mem_tree     where the heap is: ToriMem's group tree (live and peak
 *                bytes per group), after the heap grows and after a world
 *                build -- so an out-of-memory says what was holding it
 *   world_* gpu_* the client's own events (platform_web_telemetry.h): a
 *                rebuild began / staged / committed / finished, how many
 *                vertices the GPU world build holds, a world buffer that
 *                could not be allocated
 *   js_error, promise_rejection, wasm_abort, stderr
 *   hidden / visible / pagehide / resize
 *   touch_js     one per finger lifted, as the BROWSER saw it (CSS pixels)
 *   touch        the same finger as the client's touch policy took it:
 *                tap / drag / hold / multi (input/torirs_touch.c)
 *   ui_click     where the resulting click landed in the UI tree
 *   menu_use     the menu row the click ran (app_minimenu.c)
 *                -- the input four are kept only on a touch device, go out
 *                with the live posts, and stay out of the localStorage ring
 *
 * THREE WAYS IT GETS OUT, one for each way the page can fail:
 *   - running: posted every 30 s, at once for anything urgent, and by
 *     sendBeacon when the page is hidden or unloaded;
 *   - main thread stuck (a hang): a worker the page beats every second posts
 *     what the page last told it after 5, 20 and 60 s without a beat;
 *   - tab killed (memory): the ring is in localStorage, written on every
 *     event, and the NEXT load of the page sends it as `previous_unclean`.
 *
 * ?telemetry=0 turns all of it off. ?telemetry_url= sends somewhere else.
 * window.__torirs_blackbox() returns what is held, for a console.
 *
 * Loaded before every other script, so it sees the first WebGL context and
 * the first error. No build step; a plain <script> tag.
 */
(function (global) {
  'use strict';

  const params = new URLSearchParams(global.location.search);
  if (params.get('telemetry') === '0') { return; }

  const STORE_KEY = 'torirs.blackbox.v1';
  const RING_MAX = 400;
  const STORE_EVENTS = 150;
  const DETAIL_MAX = 600;
  const HEARTBEAT_MS = 1000;
  const FLUSH_MS = 30000;
  const STALL_RECORD_MS = 400;
  const STALL_URGENT_MS = 2000;
  const WATCHDOG_STEPS_MS = [5000, 20000, 60000];
  const BIG_UPLOAD_BYTES = 4 * 1024 * 1024;
  const BIG_UPLOAD_SLOW_MS = 50;
  const GL_ERRORS_RECORDED = 20;
  const STDERR_PER_MINUTE = 40;
  const MAX_POSTS = 400;
  const MEM_TREE_EVERY_MS = 5000;
  const MEM_TREES_MAX = 40;
  /* Input breadcrumbs: several a second while someone plays. They answer "why
   * did that tap do nothing", which a live post carries; a killed tab's
   * stored ring is for the crash, and these would push it out. */
  const INPUT_KINDS = new Set(['touch_js', 'touch', 'ui_click', 'menu_use']);
  const TOUCH_DEVICE = ((global.navigator && global.navigator.maxTouchPoints) || 0) > 0;

  const t0 = performance.now();
  const bootWall = Date.now();
  const sid = (() => {
    const bytes = new Uint8Array(8);
    try { global.crypto.getRandomValues(bytes); } catch (err) {
      for (let i = 0; i < bytes.length; i++) { bytes[i] = Math.floor(Math.random() * 256); }
    }
    return Array.from(bytes, b => b.toString(16).padStart(2, '0')).join('');
  })();
  const pageDir = global.location.pathname.replace(/\/[^/]*$/, '/');
  const endpoint = new URL(params.get('telemetry_url') || (pageDir + 'telemetry'),
    global.location.href).href;

  const now = () => Math.round(performance.now() - t0);

  // ------------------------------------------------------------------ ring

  const events = [];
  let seq = 0;
  let sentSeq = 0;
  let args = null;
  let heapProvider = null;
  /* ToriMem's group tree (torirs_mem_tree_json): taken when there is a reason
   * -- the heap grew, a world build landed -- on the next heartbeat, never
   * from inside a call the wasm made (a GL wrapper, an EM_JS event). */
  let memTreeProvider = null;
  let memTreeWanted = null;
  let memTreeLast = -MEM_TREE_EVERY_MS;
  let memTrees = 0;
  let visible = document.visibilityState !== 'hidden';
  let lastWorld = null;
  let lastStall = null;
  let heapBytes = 0;
  let heapPeak = 0;
  let fps = 0;

  function clip(value) {
    if (typeof value === 'string' && value.length > DETAIL_MAX) {
      return value.substring(0, DETAIL_MAX) + '...';
    }
    return value;
  }

  function record(kind, detail, urgent) {
    const ev = { n: ++seq, t: now(), k: kind };
    if (detail !== undefined) { ev.d = clip(detail); }
    events.push(ev);
    if (events.length > RING_MAX) { events.shift(); }
    if (kind.startsWith('world_') || kind.startsWith('gpu_world')) {
      lastWorld = { k: kind, t: ev.t };
    }
    if (!INPUT_KINDS.has(kind)) { persist(false); }
    if (urgent) { flushSoon(); }
    return ev;
  }

  // ------------------------------------------------------------------ GL

  /* One per context made. A renderer switch replaces the canvas, so the old
   * context's tally stays here, marked, rather than being added to the new. */
  const contexts = [];

  function currentGl() {
    for (let i = contexts.length - 1; i >= 0; i--) {
      if (!contexts[i].lost) { return contexts[i]; }
    }
    return contexts.length ? contexts[contexts.length - 1] : null;
  }

  function glSummary(c) {
    if (!c) { return null; }
    return {
      id: c.id, type: c.type, lost: c.lost,
      buf: c.bufBytes, buf_n: c.bufCount, tex: c.texBytes, tex_n: c.texCount,
      rb: c.rbBytes, peak: c.peakBytes, errors: c.errors,
      canvas: c.canvas ? [c.canvas.width, c.canvas.height] : null,
    };
  }

  function snapshot() {
    readHeap();
    return {
      t: now(),
      visible,
      fps,
      heap: heapBytes,
      heap_peak: heapPeak,
      gl: glSummary(currentGl()),
      gl_contexts: contexts.length,
      world: lastWorld ? { k: lastWorld.k, ago: now() - lastWorld.t } : null,
      stall: lastStall,
    };
  }

  /* Bytes a typed-array argument stands for, under every overload emscripten
   * uses: WebGL2 passes the WHOLE heap with an offset and a length. */
  function viewBytes(data, srcOffset, length) {
    if (typeof data === 'number') { return data; }
    if (!data) { return 0; }
    const per = data.BYTES_PER_ELEMENT || 1;
    if (length) { return length * per; }
    if (srcOffset) { return Math.max(0, data.byteLength - srcOffset * per); }
    return data.byteLength || 0;
  }

  const SIZED_BPP = {
    0x8229: 1, 0x822B: 2, 0x8058: 4, 0x8051: 3, 0x81A5: 2, 0x81A6: 4, 0x88F0: 4,
    0x8CAC: 4, 0x8CAD: 8, 0x881A: 8, 0x8814: 16, 0x822E: 4, 0x8D48: 1, 0x8056: 2,
    0x8057: 2, 0x8D62: 2, 0x8C43: 4,
  };
  function texelBytes(internalformat, format, type) {
    if (SIZED_BPP[internalformat]) { return SIZED_BPP[internalformat]; }
    if (type === 0x8363 || type === 0x8033 || type === 0x8034) { return 2; }
    const comps = format === 0x1906 || format === 0x1909 ? 1 : format === 0x190A ? 2 : 4;
    if (type === 0x1406) { return 4 * comps; }
    if (type === 0x140B || type === 0x8D61) { return 2 * comps; }
    return comps;
  }

  function instrument(gl, type, canvas, attrs) {
    const c = {
      id: contexts.length + 1, type: String(type), gl, canvas, lost: false,
      bufBytes: 0, bufCount: 0, texBytes: 0, texCount: 0, rbBytes: 0, peakBytes: 0,
      errors: 0,
    };
    contexts.push(c);

    const boundBuffer = new Map();
    const bufferSize = new Map();
    let activeUnit = 0;
    const boundTexture = new Map();
    const textureSize = new Map();
    let boundRb = null;
    const rbSize = new Map();

    const peak = () => {
      const total = c.bufBytes + c.texBytes + c.rbBytes;
      if (total > c.peakBytes) { c.peakBytes = total; }
    };
    const wrap = (name, fn) => {
      const orig = gl[name];
      if (typeof orig !== 'function') { return; }
      gl[name] = function () { return fn.call(this, orig, arguments); };
    };
    const timed = (kind, bytes, orig, a) => {
      if (bytes < BIG_UPLOAD_BYTES) { return orig.apply(gl, a); }
      const start = performance.now();
      const result = orig.apply(gl, a);
      const ms = performance.now() - start;
      if (ms >= BIG_UPLOAD_SLOW_MS) {
        record('gl_slow_upload', { call: kind, bytes, ms: Math.round(ms), ctx: c.id }, ms >= 1000);
      }
      return result;
    };

    wrap('bindBuffer', (orig, a) => { boundBuffer.set(a[0], a[1]); return orig.apply(gl, a); });
    wrap('bindBufferBase', (orig, a) => { boundBuffer.set(a[0], a[2]); return orig.apply(gl, a); });
    wrap('bindBufferRange', (orig, a) => { boundBuffer.set(a[0], a[2]); return orig.apply(gl, a); });
    wrap('bufferData', (orig, a) => {
      const bytes = viewBytes(a[1], a[3], a[4]);
      const buf = boundBuffer.get(a[0]);
      if (buf) {
        const before = bufferSize.get(buf);
        if (before === undefined) { c.bufCount++; }
        c.bufBytes += bytes - (before || 0);
        bufferSize.set(buf, bytes);
        peak();
      }
      return timed('bufferData', bytes, orig, a);
    });
    wrap('bufferSubData', (orig, a) => timed('bufferSubData', viewBytes(a[2], a[3], a[4]), orig, a));
    wrap('deleteBuffer', (orig, a) => {
      const before = bufferSize.get(a[0]);
      if (before !== undefined) { c.bufBytes -= before; c.bufCount--; bufferSize.delete(a[0]); }
      return orig.apply(gl, a);
    });

    const texKey = target => {
      const bindTarget = target >= 0x8515 && target <= 0x851A ? 0x8513 : target;
      return activeUnit * 0x10000 + (bindTarget & 0xFFFF);
    };
    const texAdd = (target, level, bytes) => {
      const tex = boundTexture.get(texKey(target));
      if (!tex) { return; }
      let levels = textureSize.get(tex);
      if (!levels) { levels = new Map(); textureSize.set(tex, levels); c.texCount++; }
      const key = target * 64 + level;
      c.texBytes += bytes - (levels.get(key) || 0);
      levels.set(key, bytes);
      peak();
    };
    wrap('activeTexture', (orig, a) => { activeUnit = a[0] - 0x84C0; return orig.apply(gl, a); });
    wrap('bindTexture', (orig, a) => { boundTexture.set(texKey(a[0]), a[1]); return orig.apply(gl, a); });
    wrap('texImage2D', (orig, a) => {
      let w; let h; let fmt; let typ;
      if (a.length >= 9) { w = a[3]; h = a[4]; fmt = a[6]; typ = a[7]; }
      else { const src = a[5] || {}; w = src.width || 0; h = src.height || 0; fmt = a[3]; typ = a[4]; }
      const bytes = w * h * texelBytes(a[2], fmt, typ);
      texAdd(a[0], a[1], bytes);
      return timed('texImage2D', bytes, orig, a);
    });
    wrap('texStorage2D', (orig, a) => {
      const base = a[3] * a[4] * texelBytes(a[2], 0, 0);
      texAdd(a[0], 0, a[1] > 1 ? Math.round(base * 4 / 3) : base);
      return orig.apply(gl, a);
    });
    wrap('texImage3D', (orig, a) => {
      const bytes = a[3] * a[4] * a[5] * texelBytes(a[2], a[7], a[8]);
      texAdd(a[0], a[1], bytes);
      return orig.apply(gl, a);
    });
    wrap('texStorage3D', (orig, a) => {
      texAdd(a[0], 0, a[3] * a[4] * a[5] * texelBytes(a[2], 0, 0));
      return orig.apply(gl, a);
    });
    wrap('deleteTexture', (orig, a) => {
      const levels = textureSize.get(a[0]);
      if (levels) {
        levels.forEach(bytes => { c.texBytes -= bytes; });
        c.texCount--;
        textureSize.delete(a[0]);
      }
      return orig.apply(gl, a);
    });

    wrap('bindRenderbuffer', (orig, a) => { boundRb = a[1]; return orig.apply(gl, a); });
    const rbSet = bytes => {
      if (!boundRb) { return; }
      c.rbBytes += bytes - (rbSize.get(boundRb) || 0);
      rbSize.set(boundRb, bytes);
      peak();
    };
    wrap('renderbufferStorage', (orig, a) => {
      rbSet(a[2] * a[3] * texelBytes(a[1], 0, 0));
      return orig.apply(gl, a);
    });
    wrap('renderbufferStorageMultisample', (orig, a) => {
      rbSet(a[3] * a[4] * texelBytes(a[2], 0, 0) * Math.max(1, a[1]));
      return orig.apply(gl, a);
    });
    wrap('deleteRenderbuffer', (orig, a) => {
      const before = rbSize.get(a[0]);
      if (before !== undefined) { c.rbBytes -= before; rbSize.delete(a[0]); }
      return orig.apply(gl, a);
    });

    wrap('getError', (orig, a) => {
      const code = orig.apply(gl, a);
      if (code) {
        c.errors++;
        if (c.errors <= GL_ERRORS_RECORDED) {
          record('gl_error', { code: '0x' + code.toString(16), ctx: c.id, gl: glSummary(c) }, true);
        }
      }
      return code;
    });

    canvas.addEventListener('webglcontextlost', () => {
      c.lost = true;
      record('gl_context_lost', { ctx: c.id, snapshot: snapshot() }, true);
    }, false);
    canvas.addEventListener('webglcontextrestored', () => {
      c.lost = false;
      record('gl_context_restored', { ctx: c.id }, true);
    }, false);

    const info = { ctx: c.id, type: c.type };
    try {
      info.requested = attrs || null;
      info.attrs = gl.getContextAttributes();
      info.version = gl.getParameter(gl.VERSION);
      info.glsl = gl.getParameter(gl.SHADING_LANGUAGE_VERSION);
      info.vendor = gl.getParameter(gl.VENDOR);
      info.renderer = gl.getParameter(gl.RENDERER);
      const dbg = gl.getExtension('WEBGL_debug_renderer_info');
      if (dbg) {
        info.gpu_vendor = gl.getParameter(dbg.UNMASKED_VENDOR_WEBGL);
        info.gpu = gl.getParameter(dbg.UNMASKED_RENDERER_WEBGL);
      }
      info.max_texture = gl.getParameter(gl.MAX_TEXTURE_SIZE);
      info.max_renderbuffer = gl.getParameter(gl.MAX_RENDERBUFFER_SIZE);
      info.max_viewport = Array.from(gl.getParameter(gl.MAX_VIEWPORT_DIMS) || []);
      info.canvas = [canvas.width, canvas.height];
    } catch (err) { info.error = String(err && err.message); }
    record('gl_context', info, true);
  }

  const instrumented = new WeakSet();
  const origGetContext = HTMLCanvasElement.prototype.getContext;
  HTMLCanvasElement.prototype.getContext = function (type) {
    const ctx = origGetContext.apply(this, arguments);
    if (ctx && /webgl/i.test(String(type)) && !instrumented.has(ctx)) {
      instrumented.add(ctx);
      try { instrument(ctx, type, this, arguments[1]); } catch (err) {
        record('telemetry_error', { where: 'instrument', msg: String(err && err.message) });
      }
    }
    if (!ctx && /webgl/i.test(String(type))) {
      record('gl_context_failed', { type: String(type) }, true);
    }
    return ctx;
  };
  document.addEventListener('webglcontextcreationerror', e => {
    record('gl_context_creation_error', { msg: String(e.statusMessage || '') }, true);
  }, true);

  // ------------------------------------------------------------------ heap

  function readHeap() {
    if (!heapProvider) { return; }
    try {
      const bytes = heapProvider() || 0;
      const from = heapBytes;
      /* Settled before the record: record() snapshots, which reads again. */
      heapBytes = bytes;
      if (bytes > heapPeak) { heapPeak = bytes; }
      if (from && bytes > from) {
        record('heap_grow', { from, to: bytes });
        memTreeWanted = 'heap_grow';
      }
    } catch (err) { /* the runtime is not up yet */ }
  }

  // ------------------------------------------------------------------ store

  /*
   * Written on every event, synchronously: the case this is for is a tab the
   * OS kills, which runs no handler, so anything still waiting on a timer is
   * lost with it. Events are rare (never per frame), and the write is small.
   */
  let clean = false;
  function persist(markClean) {
    if (markClean) { clean = true; }
    try {
      global.localStorage.setItem(STORE_KEY, JSON.stringify({
        sid, boot_wall: bootWall, alive_wall: Date.now(), visible, clean,
        snapshot: snapshot(), events: events.filter(e => !INPUT_KINDS.has(e.k)).slice(-STORE_EVENTS),
      }));
    } catch (err) { /* private mode or full: the live path still works */ }
  }

  let previous = null;
  try {
    const raw = global.localStorage.getItem(STORE_KEY);
    if (raw) { previous = JSON.parse(raw); }
  } catch (err) { previous = null; }

  // ------------------------------------------------------------------ post

  const device = (() => {
    const nav = global.navigator || {};
    let standalone = !!nav.standalone;
    try { standalone = standalone || global.matchMedia('(display-mode: standalone)').matches; } catch (err) { /* old */ }
    return {
      ua: nav.userAgent, platform: nav.platform, lang: nav.language,
      screen: [global.screen.width, global.screen.height], dpr: global.devicePixelRatio,
      viewport: [global.innerWidth, global.innerHeight],
      cores: nav.hardwareConcurrency || null, memory_gb: nav.deviceMemory || null,
      touch: nav.maxTouchPoints || 0, standalone,
      page: global.location.pathname,
    };
  })();

  let posts = 0;
  let disabled = false;
  let flushTimer = null;

  function send(reason, extra, beacon) {
    if (disabled || posts >= MAX_POSTS) { return; }
    const fresh = events.filter(e => e.n > sentSeq);
    const body = JSON.stringify(Object.assign({
      v: 1, sid, reason, boot_wall: bootWall, wall: Date.now(), t: now(), posts,
      device: posts === 0 ? device : undefined, args: posts === 0 ? args : undefined,
      snapshot: snapshot(), events: fresh,
    }, extra || {}));
    sentSeq = seq;
    posts++;
    if (beacon && global.navigator.sendBeacon) {
      try {
        if (global.navigator.sendBeacon(endpoint, new Blob([body], { type: 'text/plain' }))) { return; }
      } catch (err) { /* fall through to fetch */ }
    }
    try {
      fetch(endpoint, {
        method: 'POST', body, credentials: 'same-origin', keepalive: body.length < 60000,
        headers: { 'Content-Type': 'text/plain' },
      }).then(r => {
        /* An io_server that predates the route: stop asking. */
        if (r.status === 404 || r.status === 405) { disabled = true; }
      }).catch(() => { /* offline: the stored ring still has it */ });
    } catch (err) { /* no fetch */ }
  }

  function flushSoon() {
    if (flushTimer) { return; }
    flushTimer = setTimeout(() => { flushTimer = null; send('urgent'); }, 250);
  }

  // ------------------------------------------------------------------ watchdog

  /*
   * A hang is the main thread not coming back, and a stuck main thread runs
   * no timer and no handler -- so the one thing that can report it is
   * another thread. The worker holds what the page last told it and posts it
   * when the beats stop while the page is visible.
   */
  let watchdog = null;
  try {
    const source = `
      let last = 0, visible = false, state = null, endpoint = '', sid = '', alarm = 0;
      const STEPS = ${JSON.stringify(WATCHDOG_STEPS_MS)};
      onmessage = e => {
        const m = e.data;
        if (m.type === 'init') { endpoint = m.endpoint; sid = m.sid; return; }
        if (m.type === 'beat') { last = Date.now(); visible = m.visible; state = m.state; alarm = 0; }
      };
      setInterval(() => {
        if (!last || !visible || alarm >= STEPS.length) { return; }
        const gap = Date.now() - last;
        if (gap < STEPS[alarm]) { return; }
        alarm++;
        const body = JSON.stringify({ v: 1, sid, reason: 'main_thread_blocked', from: 'watchdog',
          blocked_ms: gap, wall: Date.now(), state });
        fetch(endpoint, { method: 'POST', body, credentials: 'same-origin',
          headers: { 'Content-Type': 'text/plain' } }).catch(() => {});
      }, 1000);
    `;
    const url = URL.createObjectURL(new Blob([source], { type: 'text/javascript' }));
    watchdog = new Worker(url);
    watchdog.postMessage({ type: 'init', endpoint, sid });
  } catch (err) {
    watchdog = null;
  }

  function beat() {
    if (!watchdog) { return; }
    try {
      watchdog.postMessage({
        type: 'beat', visible,
        state: { t: now(), snapshot: snapshot(), tail: events.slice(-40) },
      });
    } catch (err) { /* a detail that would not clone */ }
  }

  // ------------------------------------------------------------------ frames

  let lastFrame = 0;
  let frames = 0;
  let framesSince = performance.now();
  function frame(ts) {
    if (lastFrame && visible) {
      const gap = ts - lastFrame;
      if (gap >= STALL_RECORD_MS) {
        lastStall = { ms: Math.round(gap), at: now() };
        record('stall', {
          ms: Math.round(gap),
          world: lastWorld ? { k: lastWorld.k, ago: now() - lastWorld.t } : null,
        }, gap >= STALL_URGENT_MS);
      }
    }
    lastFrame = ts;
    frames++;
    global.requestAnimationFrame(frame);
  }
  global.requestAnimationFrame(frame);

  /* `used` is what malloc has out (torirs_heap_in_use); heap - used is the
   * room left inside the heap, and used - the tree's process total is what
   * the allocator and the untracked libraries cost. */
  function memTreeNow(why, urgent) {
    if (!memTreeProvider) { return; }
    memTreeLast = now();
    memTrees++;
    try {
      const got = memTreeProvider();
      record('mem_tree', { why, heap: heapBytes, used: got.used, tree: got.tree }, urgent);
    } catch (err) {
      record('telemetry_error', { where: 'mem_tree', msg: String(err && err.message) });
    }
  }

  function takeMemTree() {
    if (!memTreeWanted || !memTreeProvider || memTrees >= MEM_TREES_MAX) { return; }
    if (now() - memTreeLast < MEM_TREE_EVERY_MS) { return; }
    const why = memTreeWanted;
    memTreeWanted = null;
    memTreeNow(why, false);
  }

  setInterval(() => {
    const at = performance.now();
    fps = Math.round(frames * 1000 / Math.max(1, at - framesSince));
    frames = 0;
    framesSince = at;
    for (let i = 0; i < contexts.length; i++) {
      const c = contexts[i];
      if (!c.lost && c.gl && c.gl.isContextLost && c.gl.isContextLost()) { c.lost = true; }
    }
    takeMemTree();
    persist(false);
    beat();
  }, HEARTBEAT_MS);

  setInterval(() => { if (seq > sentSeq) { send('periodic'); } }, FLUSH_MS);

  // ------------------------------------------------------------------ page

  global.addEventListener('error', e => {
    record('js_error', {
      msg: String(e.message || ''), src: String(e.filename || ''), line: e.lineno, col: e.colno,
    }, true);
  });
  global.addEventListener('unhandledrejection', e => {
    const r = e.reason;
    record('promise_rejection', { msg: String((r && (r.stack || r.message)) || r) }, true);
  });
  document.addEventListener('visibilitychange', () => {
    visible = document.visibilityState !== 'hidden';
    lastFrame = 0;
    record(visible ? 'visible' : 'hidden');
    beat();
    /* iOS often never fires pagehide for a tab it later discards: hidden is
     * the last moment the page is sure to run. */
    if (!visible) { send('hidden', null, true); }
  });
  global.addEventListener('pagehide', () => {
    record('pagehide');
    persist(true);
    send('pagehide', null, true);
  });
  global.addEventListener('pageshow', e => {
    if (e.persisted) { clean = false; record('pageshow_restored'); }
  });
  let resizeTimer = null;
  global.addEventListener('resize', () => {
    clearTimeout(resizeTimer);
    resizeTimer = setTimeout(() => {
      record('resize', { viewport: [global.innerWidth, global.innerHeight], dpr: global.devicePixelRatio });
    }, 500);
  });

  // ------------------------------------------------------------------ API

  const stderrWindow = { at: 0, count: 0, dropped: 0 };
  const URGENT_LINE = /abort|cannot enlarge|out of memory|oom|glgeterror|assert|context lost/i;

  /*
   * How the last session on this device ended, for the page's memory
   * decision (torirs_host.js lowMemory): 'abort' when the client aborted
   * (out of memory is the abort this build has), 'killed' when it ended
   * visible with no pagehide -- the tab was terminated under it. null for a
   * clean end, a tab discarded in the background, or no last session.
   */
  const previousDeath = (() => {
    if (!previous) { return null; }
    /* Before `clean`: the page reloads itself after an abort, and that
     * reload is a pagehide like any other. */
    const evs = previous.events || [];
    if (evs.some(ev => ev && ev.k === 'wasm_abort')) { return 'abort'; }
    if (previous.clean) { return null; }
    return previous.visible ? 'killed' : null;
  })();

  global.ToriRS_Telemetry = {
    sid,
    previousDeath,
    record,
    snapshot,
    /** The page's command line, already redacted (torirs_host.js redactArgs). */
    setArgs(list) { args = list; },
    /** () => the wasm heap's size in bytes. */
    setHeapProvider(fn) { heapProvider = fn; readHeap(); },
    /** () => { tree, used }: ToriMem's group tree (main.c torirs_mem_tree_json)
     *  and malloc's bytes in use (torirs_heap_in_use). */
    setMemTreeProvider(fn) { memTreeProvider = fn; memTreeWanted = memTreeWanted || 'boot'; },
    /** The tree at once: for the moment the client dies, when no heartbeat
     *  is coming to take it. */
    memTreeNow(why) { memTreeNow(why, true); },
    /** A line the client wrote to stderr: kept, rate-limited. */
    stderr(line) {
      if (!String(line).trim()) { return; }
      const at = performance.now();
      if (at - stderrWindow.at > 60000) {
        if (stderrWindow.dropped) { record('stderr_dropped', { lines: stderrWindow.dropped }); }
        stderrWindow.at = at; stderrWindow.count = 0; stderrWindow.dropped = 0;
      }
      if (++stderrWindow.count > STDERR_PER_MINUTE) { stderrWindow.dropped++; return; }
      record('stderr', String(line), URGENT_LINE.test(String(line)));
    },
    flush(reason) { send(reason || 'manual'); },
  };

  /*
   * Every finger as the browser delivered it, to set beside the client's
   * `touch` for the same finger: a lift here with no `touch` after it is a
   * touch the client never received; `travel` is in CSS pixels (points), and
   * `scale` converts to the canvas pixels the client's slop is measured in.
   * `target` is the element the finger landed on ('canvas' for the game).
   */
  if (TOUCH_DEVICE) {
    const fingers = new Map();
    const opts = { capture: true, passive: true };
    const canvasScale = () => {
      const c = global.document.getElementById('canvas');
      return c && c.clientWidth > 0 ? Math.round((c.width / c.clientWidth) * 100) / 100 : null;
    };
    global.document.addEventListener('touchstart', e => {
      for (const t of e.changedTouches) {
        fingers.set(t.identifier, {
          at: performance.now(), x: t.clientX, y: t.clientY, max2: 0,
          target: (e.target && (e.target.id || e.target.tagName)) || null,
          down: e.touches.length - e.changedTouches.length,
        });
      }
    }, opts);
    global.document.addEventListener('touchmove', e => {
      for (const t of e.changedTouches) {
        const f = fingers.get(t.identifier);
        if (!f) { continue; }
        const dx = t.clientX - f.x;
        const dy = t.clientY - f.y;
        f.max2 = Math.max(f.max2, dx * dx + dy * dy);
      }
    }, opts);
    const lift = cancel => e => {
      for (const t of e.changedTouches) {
        const f = fingers.get(t.identifier);
        if (!f) { continue; }
        fingers.delete(t.identifier);
        const dx = t.clientX - f.x;
        const dy = t.clientY - f.y;
        record('touch_js', {
          ms: Math.round(performance.now() - f.at),
          travel: Math.round(Math.sqrt(Math.max(f.max2, dx * dx + dy * dy)) * 10) / 10,
          end: Math.round(Math.sqrt(dx * dx + dy * dy) * 10) / 10,
          cancel, target: f.target, fingers: f.down, scale: canvasScale(),
        });
      }
    };
    global.document.addEventListener('touchend', lift(false), opts);
    global.document.addEventListener('touchcancel', lift(true), opts);
  }

  /* platform_web_telemetry.c's way in. */
  global.__torirs_tel = (kind, detail) => {
    /* A desktop's mouse clicks say nothing about a finger's: not kept. */
    if (!TOUCH_DEVICE && INPUT_KINDS.has(kind)) { return; }
    let d = detail;
    try { d = JSON.parse(detail); } catch (err) { /* keep the string */ }
    const urgent = kind === 'gpu_world_alloc' && d && d.ok === 0;
    record(String(kind), d, urgent);
    if (kind === 'gpu_world_end' || kind === 'gpu_world_swap' || kind === 'world_load_finish' ||
        kind === 'gpu_stage_prepared') {
      memTreeWanted = kind;
    }
  };

  global.__torirs_blackbox = () => ({ sid, endpoint, previous, snapshot: snapshot(), events: events.slice() });

  // ------------------------------------------------------------------ boot

  record('boot', { previous_sid: previous ? previous.sid : null });
  if (previous && !previous.clean) {
    /* The last session on this device ended without a pagehide: the tab was
     * killed (memory), crashed, or was discarded while in the background.
     * `visible` says which of those it was most likely to be. */
    send('previous_unclean', {
      previous: {
        sid: previous.sid, boot_wall: previous.boot_wall, alive_wall: previous.alive_wall,
        ended_visible: !!previous.visible, snapshot: previous.snapshot, events: previous.events,
      },
    });
  } else {
    send('boot');
  }
})(window);
