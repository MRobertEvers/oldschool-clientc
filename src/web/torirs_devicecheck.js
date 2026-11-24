/*
 * The device check: a short look at this browser before the client loads,
 * shown to the player with a suggested renderer and frame rate limit and an
 * OK to accept them.
 *
 * Why before the client: torirs_host.js sizes the wasm heap for the renderer
 * the moment it loads (heapReserve), and a browser that cannot run the client
 * at all -- no WebAssembly (a Kindle's browser runs JavaScript without its
 * JIT, 2026-11-23), or no wasm SIMD -- otherwise gets a blank canvas and an
 * exception in torirs.js. So index.html loads the host and torirs.js only when
 * run() resolves, and run() never resolves on a browser that cannot play: it
 * says why instead.
 *
 * What it measures, in well under a second:
 *   - WebAssembly and wasm SIMD (the module is built with -msimd128);
 *   - WebGL 2 and WebGL 1: whether a context comes up WITHOUT a major
 *     performance caveat (a software GL), the GPU's name, whether a drawn
 *     triangle reads back in its colour, and how many triangles a short burst
 *     draws (recorded, not used: there is no calibration for it yet);
 *   - the CPU: a transform-and-span-fill kernel shaped like the software
 *     renderer's inner loops, as a score where this owner's M4 Mac is ~100;
 *   - the black box's memory of this browser: a renderer that failed here
 *     (torirs.renderer_fallback), the renderer last run (torirs.renderer).
 *
 * What OK does: window.torirsDeviceChoice = { renderer, fps }. torirs_host.js
 * boots that renderer (its flag on the command line) and sets
 * TORIRS_DEVICE_CHECK_FPS / _RENDERER, which app_boot.c writes into the
 * option store after preferences load -- exactly what picking the Limit
 * Framerate row and the Client Settings renderer would store. So the in-game
 * settings show the choice, and keep it.
 *
 * When it asks: once per tab (a reload in the same tab, such as the host's
 * own abort recovery, does not ask again), unless the player ticked "Don't
 * ask again" -- kept in localStorage under STORE_KEY, which the site's
 * Account page clears (website_site.c, "Device check"). A browser that
 * cannot run the client is told so every time regardless.
 *
 *   ?devicecheck=0   never ask (still says so when the browser cannot run it)
 *   ?devicecheck=1   ask even after "Don't ask again"
 * Not shown to automation (navigator.webdriver), a replay, an editor view or
 * an embedded frame unless ?devicecheck=1 says so.
 */
(function () {
  'use strict';

  const STORE_KEY = 'torirs.device_check';
  const TAB_KEY = 'torirs.device_check_tab';
  /* The last CPU score measured here, kept for the boots that do not measure
   * (Don't ask again, a reload in the same tab): the client reports it to the
   * server, which locks a device below LOW_SCORE (net/torirs_control.h,
   * TORIRS_DEVICE_LOW_SCORE_X10 -- the server's line, mirrored here only to
   * say so on the panel). */
  const SCORE_KEY = 'torirs.device_score';
  const LOW_SCORE = 5;
  /* The CPU kernel's units per millisecond that scores 100 (an M4 Mac, V8). */
  const CPU_REFERENCE_UNITS_PER_MS = 140;

  /* enum ToriRS_RendererKind, as torirs_host.js rendererMemory knows them. */
  /* Compatibility mode: the software renderer with every face one flat fill
   * (TORIRS_COMPAT, toridraw kernels/sd.compat_flat.u.c), no plugins, and a
   * page that mounts only the canvas (compat.html). Not a client renderer
   * kind: it boots Software (0). Kept per browser in COMPAT_KEY, so every
   * boot goes to compat.html, asked or not; ?compat=0 on the game page turns
   * it off, as picking another renderer here does. */
  const COMPAT = -2;
  const COMPAT_KEY = 'torirs.compat';
  const RENDERERS = [
    { kind: COMPAT, gl: 0, name: 'Compatibility (flat colour, no textures)' },
    { kind: 7, gl: 2, name: 'WebGL 2' },
    { kind: 8, gl: 2, name: 'WebGL 2 (z-buffer)' },
    { kind: 15, gl: 2, name: 'WebGL 2 (Sailing OSRS)' },
    { kind: 17, gl: 2, name: 'WebGL 2 (Post-TFA OSRS)' },
    { kind: 9, gl: 1, name: 'WebGL 1' },
    { kind: 10, gl: 1, name: 'WebGL 1 (z-buffer)' },
    { kind: 0, gl: 0, name: 'Software' },
    { kind: 13, gl: 0, name: 'Software (z-buffer)' },
    { kind: 14, gl: 0, name: 'Software (RS z-buffer)' },
  ];
  /* The Limit Framerate rows (cache enum_3504 on the live world); 999 is
   * what the Unlimited row writes. */
  const FRAME_RATES = [
    { fps: 5, name: '5 fps' },
    { fps: 15, name: '15 fps' },
    { fps: 20, name: '20 fps' },
    { fps: 30, name: '30 fps' },
    { fps: 60, name: '60 fps' },
    { fps: 999, name: 'Unlimited' },
  ];

  const params = new URLSearchParams(window.location.search);
  const read = (store, key) => { try { return window[store].getItem(key); } catch (err) { return null; } };
  const write = (store, key, value) => {
    try {
      if (value === null) { window[store].removeItem(key); }
      else { window[store].setItem(key, value); }
    } catch (err) { /* private mode: this boot still knows */ }
  };
  const rendererByKind = kind => RENDERERS.find(r => r.kind === kind) || null;
  const record = (kind, detail, urgent) => {
    if (window.ToriRS_Telemetry) { window.ToriRS_Telemetry.record(kind, detail, !!urgent); }
  };

  /* ---------------------------------------------------------------- probes */

  function probeWasm() {
    if (typeof WebAssembly !== 'object' || typeof WebAssembly.validate !== 'function') {
      return { wasm: false, simd: false };
    }
    /* The smallest module using a v128 op (i8x16.splat, i8x16.popcnt). */
    const simd = new Uint8Array([0, 97, 115, 109, 1, 0, 0, 0, 1, 5, 1, 96, 0, 1, 123, 3, 2, 1, 0,
      10, 10, 1, 8, 0, 65, 0, 253, 15, 253, 98, 11]);
    let ok = false;
    try { ok = WebAssembly.validate(simd); } catch (err) { ok = false; }
    return { wasm: true, simd: ok };
  }

  const SOFTWARE_GL = /swiftshader|llvmpipe|softpipe|software|basic render|mesa offscreen/i;

  function glName(gl) {
    try {
      const info = gl.getExtension('WEBGL_debug_renderer_info');
      if (info) { return String(gl.getParameter(info.UNMASKED_RENDERER_WEBGL) || ''); }
    } catch (err) { /* fall through */ }
    try { return String(gl.getParameter(gl.RENDERER) || ''); } catch (err) { return ''; }
  }

  function compile(gl, type, source) {
    const shader = gl.createShader(type);
    gl.shaderSource(shader, source);
    gl.compileShader(shader);
    return gl.getShaderParameter(shader, gl.COMPILE_STATUS) ? shader : null;
  }

  /* One triangle that must read back orange, then a short burst of many
   * small ones, synchronised by a one-pixel readPixels per draw. */
  function drawTest(gl) {
    const vs = compile(gl, gl.VERTEX_SHADER,
      'attribute vec2 p; void main() { gl_Position = vec4(p, 0.0, 1.0); }');
    const fs = compile(gl, gl.FRAGMENT_SHADER,
      'precision mediump float; void main() { gl_FragColor = vec4(1.0, 0.5, 0.25, 1.0); }');
    if (!vs || !fs) { return { ok: false, why: 'shaders do not compile' }; }
    const program = gl.createProgram();
    gl.attachShader(program, vs);
    gl.attachShader(program, fs);
    gl.bindAttribLocation(program, 0, 'p');
    gl.linkProgram(program);
    if (!gl.getProgramParameter(program, gl.LINK_STATUS)) { return { ok: false, why: 'program does not link' }; }
    gl.useProgram(program);
    gl.viewport(0, 0, 64, 64);

    const pixel = new Uint8Array(4);
    const big = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, big);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);
    gl.clearColor(0, 0, 0, 1);
    gl.clear(gl.COLOR_BUFFER_BIT);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    gl.readPixels(32, 32, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
    const near = (a, b) => Math.abs(a - b) <= 8;
    if (!(near(pixel[0], 255) && near(pixel[1], 128) && near(pixel[2], 64))) {
      return { ok: false, why: `a drawn triangle read back as ${pixel[0]},${pixel[1]},${pixel[2]}` };
    }

    const TRIS = 20000;
    const verts = new Float32Array(TRIS * 6);
    let seed = 12345;
    const rand = () => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed / 0x7fffffff * 2 - 1; };
    for (let i = 0; i < TRIS; i++) {
      const x = rand(), y = rand();
      verts.set([x, y, x + 0.05, y, x, y + 0.05], i * 6);
    }
    const many = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, many);
    gl.bufferData(gl.ARRAY_BUFFER, verts, gl.STATIC_DRAW);
    gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);
    const t0 = performance.now();
    let draws = 0;
    while (performance.now() - t0 < 120 && draws < 400) {
      gl.drawArrays(gl.TRIANGLES, 0, TRIS * 3);
      gl.readPixels(0, 0, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
      draws++;
    }
    const ms = performance.now() - t0;
    return { ok: true, tris_per_ms: Math.round(draws * TRIS / ms) };
  }

  function probeGl(type) {
    const attempt = attrs => {
      const canvas = document.createElement('canvas');
      canvas.width = 64;
      canvas.height = 64;
      try { return canvas.getContext(type, attrs); } catch (err) { return null; }
    };
    const base = { alpha: false, antialias: false, depth: false, stencil: false, preserveDrawingBuffer: true };
    let gl = attempt(Object.assign({ failIfMajorPerformanceCaveat: true }, base));
    let caveat = false;
    if (!gl) {
      gl = attempt(base);
      caveat = !!gl;
    }
    if (!gl) { return { available: false }; }
    const name = glName(gl);
    let draw;
    try { draw = drawTest(gl); } catch (err) { draw = { ok: false, why: String(err && err.message) }; }
    const lose = gl.getExtension('WEBGL_lose_context');
    if (lose) { lose.loseContext(); }
    const software = caveat || SOFTWARE_GL.test(name);
    return {
      available: true,
      name,
      caveat,
      software,
      draw_ok: draw.ok,
      draw_why: draw.why || null,
      tris_per_ms: draw.tris_per_ms || 0,
      usable: !software && draw.ok,
    };
  }

  /* Shaped like the software renderer's work: a vertex transform with a
   * perspective divide, then gouraud-like span fills into a pixel buffer.
   * The first quarter of the budget is warm-up for the JIT's tiers. */
  function probeCpu(budgetMs) {
    const N = 2048, W = 128, H = 96;
    const vx = new Float32Array(N), vy = new Float32Array(N), vz = new Float32Array(N);
    const sx = new Int32Array(N), sy = new Int32Array(N);
    const px = new Uint32Array(W * H);
    for (let i = 0; i < N; i++) { vx[i] = (i * 37) % 1000 - 500; vy[i] = (i * 91) % 1000 - 500; vz[i] = (i * 13) % 1000 + 600; }
    let acc = 0;
    const unit = k => {
      const s = Math.sin(k * 0.01), c = Math.cos(k * 0.01);
      for (let i = 0; i < N; i++) {
        const x = vx[i] * c - vz[i] * s, z = vx[i] * s + vz[i] * c + 1500;
        sx[i] = ((x * 512 / z) | 0) + 64;
        sy[i] = ((vy[i] * 512 / z) | 0) + 48;
      }
      for (let y = 0; y < H; y++) {
        const row = y * W;
        let g = (y + k) << 8;
        const x0 = sx[y & (N - 1)] & 63, x1 = x0 + 64;
        for (let x = x0; x < x1; x++) { g += 37; px[row + x] = (px[row + x] >>> 1) + ((g >>> 8) & 0xff) * 0x010101; }
      }
      acc = (acc + sx[k & (N - 1)] + px[k & 1023]) | 0;
    };
    let t0 = performance.now();
    let k = 0;
    while (performance.now() - t0 < budgetMs * 0.25) { unit(k++); }
    t0 = performance.now();
    let units = 0;
    while (performance.now() - t0 < budgetMs * 0.75) { unit(k++); units++; }
    const ms = performance.now() - t0;
    const perMs = units / ms;
    /* One decimal below 10: a JIT-less browser scores well under 1. */
    const score = perMs / CPU_REFERENCE_UNITS_PER_MS * 100;
    return { units_per_ms: Math.round(perMs * 100) / 100, score: score < 10 ? Math.round(score * 10) / 10 : Math.round(score), acc };
  }

  /* ------------------------------------------------------- recommendation */

  function recommendRenderer(gl2, gl1, memoryGb) {
    const lastRun = parseInt(read('localStorage', 'torirs.renderer') || '', 10);
    let fallback = null;
    try { fallback = JSON.parse(read('localStorage', 'torirs.renderer_fallback') || 'null'); } catch (err) { fallback = null; }
    const death = window.ToriRS_Telemetry ? window.ToriRS_Telemetry.previousDeath : null;
    const last = rendererByKind(lastRun);
    const runs = r => (r.gl === 2 ? gl2.usable : r.gl === 1 ? gl1.usable : true);

    if (fallback) {
      const failed = rendererByKind(fallback.kind);
      return { kind: 0, why: `${failed ? failed.name : 'A GPU renderer'} failed in this browser before (${fallback.why}).` };
    }
    if (death && last && last.gl) {
      return { kind: 0, why: `The last session here ended badly on ${last.name} (${death}).` };
    }
    if (last && last.gl && runs(last)) {
      return { kind: last.kind, why: `${last.name} is what you played with last, and this device runs it.` };
    }
    const small = memoryGb && memoryGb <= 2;
    if (gl2.usable && !(small && gl1.usable)) {
      return { kind: 7, why: `Your GPU (${gl2.name || 'unnamed'}) runs WebGL 2.` };
    }
    if (gl1.usable) {
      return { kind: 9, why: small ? `WebGL 1 needs less memory, and this device reports ${memoryGb} GB.`
        : `Your GPU (${gl1.name || 'unnamed'}) runs WebGL 1, not WebGL 2.` };
    }
    const seen = gl2.available ? gl2 : gl1;
    if (seen.available) {
      return { kind: 0, why: seen.software ? `The browser draws WebGL without the GPU (${seen.name || 'software GL'}): software is faster.`
        : `WebGL did not draw correctly here (${seen.draw_why}).` };
    }
    return { kind: 0, why: 'This browser has no WebGL.' };
  }

  /* Rough by construction: one CPU score, a renderer, and whether the
   * device runs on a battery. The black box records the score beside the
   * frame rate the session then reaches, which is how these get tuned. */
  function recommendFps(score, kind, mobile) {
    const r = rendererByKind(kind);
    const effective = r && r.gl ? score : score / 2;
    if (mobile) {
      if (effective >= 5) { return { fps: 20, why: 'A phone or tablet: 20 fps saves battery.' }; }
      if (effective >= 2.5) { return { fps: 15, why: 'This device is slow for the game.' }; }
      return { fps: 5, why: 'This device is very slow for the game.' };
    }
    if (effective >= 40) { return { fps: 999, why: 'This computer is fast.' }; }
    if (effective >= 20) { return { fps: 60, why: 'This computer is quick.' }; }
    if (effective >= 10) { return { fps: 30, why: 'This computer is middling for the game.' }; }
    if (effective >= 5) { return { fps: 20, why: 'This computer is slow for the game.' }; }
    if (effective >= 2.5) { return { fps: 15, why: 'This computer is very slow for the game.' }; }
    return { fps: 5, why: 'This computer is far too slow for more.' };
  }

  /* ------------------------------------------------------------------- UI */

  function el(tag, attrs, children) {
    const node = document.createElement(tag);
    Object.keys(attrs || {}).forEach(key => {
      if (key === 'text') { node.textContent = attrs[key]; }
      else { node.setAttribute(key, attrs[key]); }
    });
    (children || []).forEach(child => node.appendChild(child));
    return node;
  }

  function panel() {
    let root = document.getElementById('device-check');
    if (!root) {
      root = el('div', { id: 'device-check', role: 'dialog', 'aria-modal': 'true', 'aria-labelledby': 'device-check-title' });
      document.body.appendChild(root);
    }
    root.textContent = '';
    const box = el('div', { class: 'device-check-box' });
    root.appendChild(box);
    return box;
  }

  function showChecking() {
    const box = panel();
    box.appendChild(el('h2', { id: 'device-check-title', text: 'Checking this device…' }));
    box.appendChild(el('p', { text: 'A moment: this picks a renderer and a frame rate that suit it.' }));
  }

  function showUnsupported(wasm, found) {
    const box = panel();
    box.appendChild(el('h2', { id: 'device-check-title', text: 'This browser cannot run ToriRS' }));
    if (!wasm.wasm) {
      box.appendChild(el('p', { text: 'It has WebAssembly turned off, and the game is WebAssembly.' }));
      box.appendChild(el('p', { text: 'That is how e-readers’ browsers work (a Kindle’s, for one), and how '
        + 'some apps’ built-in browsers and some security settings work: they run pages without the '
        + 'JavaScript optimiser. In Chrome it is the “V8 optimizer” site setting.' }));
    } else {
      box.appendChild(el('p', { text: 'It is too old: the game needs WebAssembly SIMD (Chrome or Edge 91, '
        + 'Firefox 89, Safari 16.4, or newer).' }));
    }
    box.appendChild(el('p', { text: 'Open this page in Chrome, Edge, Firefox or Safari with their usual '
      + 'settings, on a computer, phone or tablet.' }));
    box.appendChild(el('h3', { text: 'What this browser has' }));
    box.appendChild(factList(wasm, found));
    /* The device bench (devicebench.html) times a frame of the game's own
     * scene in plain JavaScript: how this browser would fare without
     * WebAssembly. Linked here because this is the page such a browser
     * reaches; the sign-in does not carry a deep link through. */
    const bench = el('a', { href: 'devicebench.html', text: 'Time how this browser draws the game →' });
    bench.style.fontSize = '16px';
    box.appendChild(el('p', {}, [bench]));
  }

  /* The readings, as the player sees them: on the choice panel and on the
   * cannot-run panel alike, so a device that cannot play still says what
   * it is (and the black box gets the same numbers). */
  function factList(wasm, found) {
    const facts = el('dl', {});
    const fact = (term, value) => { facts.appendChild(el('dt', { text: term })); facts.appendChild(el('dd', { text: value })); };
    const glText = g => (!g.available ? 'not available'
      : g.usable ? `yes — ${g.name || 'GPU'}`
      : g.software ? `software only — ${g.name || 'no GPU'}` : `does not draw correctly — ${g.name || ''}`);
    fact('WebAssembly', wasm.wasm ? 'yes' : 'no');
    fact('Wasm SIMD', wasm.wasm ? (wasm.simd ? 'yes' : 'no') : '—');
    fact('WebGL 2', glText(found.gl2));
    fact('WebGL 1', glText(found.gl1));
    fact('CPU score', `${found.cpu.score} (a recent fast desktop is about 100)`);
    if (found.cores) { fact('CPU cores', String(found.cores)); }
    if (found.memory_gb) { fact('Memory', `${found.memory_gb} GB reported`); }
    return facts;
  }

  function showChoice(wasm, found, rec, fps, onOk) {
    const box = panel();
    box.appendChild(el('h2', { id: 'device-check-title', text: 'Device check' }));
    box.appendChild(factList(wasm, found));

    const rendererSelect = el('select', { id: 'device-check-renderer' });
    RENDERERS.forEach(r => {
      const usable = r.gl === 2 ? found.gl2.available : r.gl === 1 ? found.gl1.available : true;
      if (!usable) { return; }
      const option = el('option', { value: String(r.kind), text: r.kind === rec.kind ? `${r.name} (suggested)` : r.name });
      if (r.kind === rec.kind) { option.selected = true; }
      rendererSelect.appendChild(option);
    });
    const fpsSelect = el('select', { id: 'device-check-fps' });
    FRAME_RATES.forEach(f => {
      const option = el('option', { value: String(f.fps), text: f.fps === fps.fps ? `${f.name} (suggested)` : f.name });
      if (f.fps === fps.fps) { option.selected = true; }
      fpsSelect.appendChild(option);
    });
    if (found.cpu.score < LOW_SCORE) {
      box.appendChild(el('p', { class: 'lock', text: `This device is slow for the game (CPU score `
        + `${found.cpu.score}, under ${LOW_SCORE}): it plays in fixed mode at 765\u00d7503, the classic `
        + 'size, and the server keeps it there.' }));
    }
    box.appendChild(el('label', {}, [el('span', { text: 'Renderer' }), rendererSelect]));
    box.appendChild(el('p', { class: 'why', text: rec.why }));
    box.appendChild(el('label', {}, [el('span', { text: 'Limit framerate' }), fpsSelect]));
    box.appendChild(el('p', { class: 'why', text: fps.why }));

    const dontAsk = el('input', { type: 'checkbox', id: 'device-check-dont-ask' });
    box.appendChild(el('label', { class: 'check' }, [dontAsk, el('span', {
      text: 'Don’t ask again in this browser (the Account page can turn the check back on)' })]));
    box.appendChild(el('p', { class: 'why', text: 'Both can be changed later in the game’s Settings.' }));

    const ok = el('button', { type: 'button', id: 'device-check-ok', text: 'OK' });
    box.appendChild(el('div', { class: 'actions' }, [ok]));
    const finish = () => {
      onOk({
        renderer: parseInt(rendererSelect.value, 10),
        fps: parseInt(fpsSelect.value, 10),
        dont_ask: dontAsk.checked,
      });
    };
    ok.addEventListener('click', finish);
    box.addEventListener('keydown', e => { if (e.key === 'Enter' && e.target.tagName !== 'SELECT') { finish(); } });
    ok.focus();
  }

  function hide() {
    const root = document.getElementById('device-check');
    if (root) { root.remove(); }
  }

  /* ------------------------------------------------------------------ run */

  function storedRecord() {
    try { return JSON.parse(read('localStorage', STORE_KEY) || 'null'); } catch (err) { return null; }
  }

  function skipReason() {
    const forced = params.get('devicecheck');
    if (forced === '1') { return null; }
    if (forced === '0') { return '?devicecheck=0'; }
    const stored = storedRecord();
    if (stored && stored.dont_ask) { return 'don’t ask again'; }
    if (read('sessionStorage', TAB_KEY)) { return 'already asked in this tab'; }
    if (window.navigator.webdriver) { return 'automation'; }
    if (params.has('replay') || params.has('maped') || params.has('editor_view')) { return 'replay or editor'; }
    if (window.parent !== window) { return 'embedded'; }
    return null;
  }

  /* One frame for the "Checking" panel to paint before the probes, which
   * hold the main thread for their few hundred milliseconds. */
  const nextPaint = () => new Promise(resolve => {
    requestAnimationFrame(() => setTimeout(resolve, 0));
  });

  async function run() {
    const wasm = probeWasm();
    const supported = wasm.wasm && wasm.simd;
    const skip = supported ? skipReason() : null;
    if (skip) {
      record('device_check', { verdict: 'skipped', why: skip });
      /* No question asked, but the score measured here before still goes to
       * the server (a boot that is not the player's own -- automation, a
       * replay, an editor, a frame -- sends none). */
      let stored = null;
      try { stored = JSON.parse(read('localStorage', SCORE_KEY) || 'null'); } catch (err) { stored = null; }
      const own = skip === 'don\u2019t ask again' || skip === 'already asked in this tab' || skip === '?devicecheck=0';
      return own && stored && typeof stored.score === 'number' ? { renderer: null, fps: null, score: stored.score } : null;
    }

    showChecking();
    await nextPaint();
    const mobile = !!(globalThis.torirsMobileDevice && globalThis.torirsMobileDevice.mobile);
    const found = {
      gl2: probeGl('webgl2'),
      gl1: probeGl('webgl'),
      cpu: probeCpu(320),
      memory_gb: window.navigator.deviceMemory || null,
      cores: window.navigator.hardwareConcurrency || null,
      mobile,
    };
    const reported = Object.assign({}, found, { cpu: { score: found.cpu.score, units_per_ms: found.cpu.units_per_ms } });
    write('localStorage', SCORE_KEY, JSON.stringify({ score: found.cpu.score, at: Date.now() }));
    if (!supported) {
      record('device_check', { verdict: 'unsupported', wasm: wasm.wasm, simd: wasm.simd, found: reported }, true);
      showUnsupported(wasm, found);
      return new Promise(() => {});
    }
    const rec = found.cpu.score < LOW_SCORE
      ? { kind: COMPAT, why: `A CPU this slow (score ${found.cpu.score}) is best given the least to draw: `
          + 'flat colours, no textures, no plugins, and only the game on the page.' }
      : recommendRenderer(found.gl2, found.gl1, found.memory_gb);
    const fps = recommendFps(found.cpu.score, rec.kind, mobile);

    return new Promise(resolve => {
      showChoice(wasm, found, rec, fps, picked => {
        hide();
        write('sessionStorage', TAB_KEY, '1');
        if (picked.dont_ask) {
          write('localStorage', STORE_KEY, JSON.stringify({
            v: 1, dont_ask: true, at: Date.now(), renderer: picked.renderer, fps: picked.fps }));
        }
        /* The failure note made the suggestion software; a renderer picked
         * here is the player's answer to it, as a pick in Client Settings is
         * (torirs_host.js rendererMemory), so the note goes either way. */
        write('localStorage', 'torirs.renderer_fallback', null);
        record('device_check', {
          verdict: 'ok', found: reported,
          suggested: { renderer: rec.kind, fps: fps.fps }, picked,
        }, true);
        if (picked.renderer === COMPAT) {
          write('localStorage', COMPAT_KEY, '1');
          resolve({ renderer: 0, fps: picked.fps, score: found.cpu.score, compat: true });
          return;
        }
        write('localStorage', COMPAT_KEY, null);
        resolve({ renderer: picked.renderer, fps: picked.fps, score: found.cpu.score });
      });
    });
  }

  window.ToriRS_DeviceCheck = { STORE_KEY, COMPAT_KEY, SCORE_KEY, run };
})();
