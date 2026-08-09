/*
 * Entity viewer front-end.
 *
 * Two halves talk here: ev_server (which owns the cache) answers over HTTP, and
 * ev_wasm (which owns toridraw's software renderer) draws into a buffer this
 * blits onto a canvas. Nothing in this file knows anything about cache formats.
 */

'use strict';

const CANVAS = document.getElementById('view');
const CTX = CANVAS.getContext('2d');

/* Client ticks are 20 ms; a sequence frame's delay is counted in them. */
const TICK_MS = 20;

const state = {
  wasm: null,
  npcs: [],
  npcId: null,
  detail: null,
  seqId: null,
  frameCount: 0,
  frame: 0,
  playing: true,
  yaw: 0,
  pitch: 200,
  zoom: 1400,
  /* Time owed to the current frame, so a frame with delay=3 holds for three
   * ticks instead of the display's refresh rate deciding playback speed. */
  frameAcc: 0,
  lastT: 0,
};

/* ---- wasm bridge --------------------------------------------------------- */

function wasmCall(mod) {
  return {
    init: mod.cwrap('ev_w_init', null, []),
    alloc: mod.cwrap('ev_w_alloc', 'number', ['number']),
    release: mod.cwrap('ev_w_release', null, ['number']),
    setModel: mod.cwrap('ev_w_set_model', 'number', ['number', 'number']),
    setAnim: mod.cwrap('ev_w_set_anim', 'number', ['number', 'number']),
    clearAnim: mod.cwrap('ev_w_clear_anim', null, []),
    frameCount: mod.cwrap('ev_w_frame_count', 'number', []),
    frameDelay: mod.cwrap('ev_w_frame_delay', 'number', ['number']),
    modelHeight: mod.cwrap('ev_w_model_height', 'number', []),
    lastCull: mod.cwrap('ev_w_last_cull', 'number', []),
    render: mod.cwrap('ev_w_render', 'number',
      ['number', 'number', 'number', 'number', 'number', 'number']),
    mod,
  };
}

/** Hand a fetched blob to the module: copy into its heap, call, free. */
async function feed(url, fn) {
  const res = await fetch(url);
  if (!res.ok) return 0;
  const bytes = new Uint8Array(await res.arrayBuffer());
  const ptr = state.wasm.alloc(bytes.length);
  if (!ptr) return 0;
  state.wasm.mod.HEAPU8.set(bytes, ptr);
  const out = fn(ptr, bytes.length);
  state.wasm.release(ptr);
  return out;
}

/* ---- npc list ------------------------------------------------------------ */

function renderNpcList() {
  const q = document.getElementById('npcSearch').value.trim().toLowerCase();
  const list = document.getElementById('npcList');
  const matches = [];

  for (const n of state.npcs) {
    if (q) {
      const hay = `${n.id} ${(n.name || '').toLowerCase()} ${(n.gameval || '').toLowerCase()}`;
      if (!hay.includes(q)) continue;
    }
    matches.push(n);
    /* The list is 16,292 rows; building them all makes typing stutter. The
     * count below still reports the true total. */
    if (matches.length >= 400) break;
  }

  list.innerHTML = '';
  const frag = document.createDocumentFragment();
  for (const n of matches) {
    const row = document.createElement('div');
    row.className = 'row' + (n.id === state.npcId ? ' sel' : '');
    row.innerHTML =
      `<span class="id">${n.id}</span>` +
      `<span class="nm">${escapeHtml(n.name || '(unnamed)')}</span>` +
      `<span class="badge">${n.rig}/${n.maybe}</span>`;
    row.title = n.gameval || '';
    row.onclick = () => selectNpc(n.id);
    frag.appendChild(row);
  }
  list.appendChild(frag);

  const total = q
    ? state.npcs.filter((n) => {
        const hay = `${n.id} ${(n.name || '').toLowerCase()} ${(n.gameval || '').toLowerCase()}`;
        return hay.includes(q);
      }).length
    : state.npcs.length;
  document.getElementById('npcCount').textContent =
    `${matches.length} of ${total} shown · badge is rig/maybe counts`;
}

function escapeHtml(s) {
  return String(s).replace(/[&<>"]/g, (c) =>
    ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
}

/* ---- selection ----------------------------------------------------------- */

async function selectNpc(id) {
  state.npcId = id;
  state.seqId = null;
  renderNpcList();

  const detail = await (await fetch(`/api/npc/${id}.json`)).json();
  state.detail = detail;

  document.getElementById('stageTitle').textContent =
    `${detail.name || '(unnamed)'} — npc ${id}` +
    (detail.gameval ? ` · ${detail.gameval}` : '');

  const faces = await feed(`/api/npc/${id}.model`, (p, n) => state.wasm.setModel(p, n));
  if (!faces) {
    document.getElementById('stageMeta').textContent = 'this npc has no renderable model';
  } else {
    const h = state.wasm.modelHeight();
    /* Frame the model rather than keeping a fixed distance: npcs run from a
     * chicken to a 20-tile boss and one zoom cannot suit both. The multiplier
     * is the orbit distance in model heights — 1.6 puts a typical npc at about
     * two thirds of the canvas. */
    state.zoom = Math.max(260, Math.min(6000, Math.round(h * 1.6) || 900));
    document.getElementById('stageMeta').textContent =
      `${faces} faces · rigs ${detail.framemaps.join(', ') || 'none'}` +
      (detail.skinned ? ' · animaya skinned' : '') +
      ' · drag to orbit · wheel to zoom';
  }

  state.wasm.clearAnim();
  state.frameCount = 0;
  state.frame = 0;
  updateFrameUi();
  renderAnimList();
}

/**
 * Does this animation match the search box?
 *
 * Matches the gameval name, the sequence id typed in full, and the words
 * `skeletal` / `classic` — an npc on the human rig lists 3,905 sequences, and
 * `walk` or `death` is how you get from that to the handful you want. The id is
 * compared as a whole word rather than a substring so `9421` does not also
 * bring back 19421.
 */
function animMatches(a, q) {
  if (!q) return true;
  if (String(a.seq) === q) return true;
  if ((a.name || '').toLowerCase().includes(q)) return true;
  const kind = a.skeletal ? 'skeletal' : 'classic';
  return kind.startsWith(q);
}

function renderAnimList() {
  const el = document.getElementById('animList');
  const d = state.detail;
  el.innerHTML = '';
  if (!d) { el.innerHTML = '<div class="note">Pick an npc.</div>'; return; }

  const q = document.getElementById('animSearch').value.trim().toLowerCase();
  const rig = d.rig.filter((a) => animMatches(a, q));

  const frag = document.createDocumentFragment();

  frag.appendChild(group(
    'rig', `Rigging matches (${countLabel(rig.length, d.rig.length)})`,
    'Sequences built on this npc’s own rig. Concrete: sharing a rig is the ' +
    'precondition for an animation applying at all.'));

  for (const a of rig) {
    /* A skeletal sequence poses through the model's Animaya skin. An npc
     * without one is left in its bind pose rather than mis-animated, so say so
     * on the row instead of letting it look like a broken animation. */
    const unplayable = a.skeletal && !d.skinned;
    const badge = (a.skeletal ? 'skeletal · ' : '') + `${a.frames} frames`;
    frag.appendChild(animRow(a.seq, a.name, badge, false, unplayable));
  }

  const allMaybes = d.maybe.filter((m) => !m.in_rig);
  const maybes = allMaybes.filter((m) => animMatches(m, q));
  const corroborated = d.maybe.length - allMaybes.length;
  frag.appendChild(group(
    'maybe', `Name guesses (${countLabel(maybes.length, allMaybes.length)})`,
    'Sequences whose gameval name shares a distinctive word with this npc’s. ' +
    'A guess — it finds animations nothing points at, and it produces false ' +
    `positives.${corroborated ? ` ${corroborated} more also matched by rig.` : ''}`));

  for (const m of maybes) {
    frag.appendChild(animRow(m.seq, m.name, `score ${m.score}`, true));
  }
  if (!maybes.length) {
    const n = document.createElement('div');
    n.className = 'note';
    n.textContent = allMaybes.length
      ? 'No name guesses match the search.'
      : 'No name guesses outside the rig set.';
    frag.appendChild(n);
  }
  if (q && !rig.length && !maybes.length) {
    const n = document.createElement('div');
    n.className = 'note';
    n.textContent = `Nothing matches “${q}”.`;
    frag.appendChild(n);
  }

  el.appendChild(frag);

  const skel = rig.filter((a) => a.skeletal).length;
  document.getElementById('animCount').textContent =
    `${countLabel(rig.length, d.rig.length)} rig` +
    (skel ? ` (${skel} skeletal)` : '') +
    ` · ${countLabel(maybes.length, allMaybes.length)} maybe`;
}

/** `n` when nothing is filtered, `n of total` when something is. */
function countLabel(shown, total) {
  return shown === total ? `${total}` : `${shown} of ${total}`;
}

function group(kind, title, note) {
  const wrap = document.createElement('div');
  const head = document.createElement('div');
  head.className = 'grouphead';
  head.innerHTML = `<span class="tag ${kind}"></span><strong>${escapeHtml(title)}</strong>`;
  wrap.appendChild(head);
  const n = document.createElement('div');
  n.className = 'note';
  n.textContent = note;
  wrap.appendChild(n);
  return wrap;
}

function animRow(seq, name, badge, isMaybe, unplayable) {
  const row = document.createElement('div');
  row.className = 'row' + (seq === state.seqId ? ' sel' : '');
  row.innerHTML =
    `<span class="id">${seq}</span>` +
    `<span class="gv" style="flex:1">${escapeHtml(name || '(unnamed)')}</span>` +
    `<span class="badge">${escapeHtml(badge)}</span>`;
  if (isMaybe) row.style.opacity = '0.85';
  if (unplayable) {
    row.style.opacity = '0.45';
    row.title = 'skeletal, but this npc has no Animaya skin — it cannot play this';
  }
  row.onclick = () => selectSeq(seq);
  return row;
}

async function selectSeq(seq) {
  state.seqId = seq;
  const frames = await feed(`/api/seq/${seq}.anim`, (p, n) => state.wasm.setAnim(p, n));
  state.frameCount = frames;
  state.frame = 0;
  state.frameAcc = 0;
  state.playing = frames > 0;
  document.getElementById('playBtn').textContent = state.playing ? 'Pause' : 'Play';
  updateFrameUi();
  renderAnimList();
}

function updateFrameUi() {
  const slider = document.getElementById('frameSlider');
  slider.max = Math.max(0, state.frameCount - 1);
  slider.value = state.frame;
  slider.disabled = state.frameCount === 0;
  document.getElementById('playBtn').disabled = state.frameCount === 0;
  document.getElementById('frameLabel').textContent = state.frameCount
    ? `seq ${state.seqId} · frame ${state.frame + 1}/${state.frameCount}`
    : 'bind pose';
}

/* ---- render loop --------------------------------------------------------- */

function frameLoop(t) {
  requestAnimationFrame(frameLoop);
  if (!state.wasm) return;

  const dt = state.lastT ? t - state.lastT : 0;
  state.lastT = t;

  if (state.playing && state.frameCount > 0) {
    /* A frame's delay is in ticks, and a delay of 0 would spin the animation as
     * fast as the display refreshes, so it counts as one tick. */
    const delay = Math.max(1, state.wasm.frameDelay(state.frame));
    state.frameAcc += dt;
    while (state.frameAcc >= delay * TICK_MS) {
      state.frameAcc -= delay * TICK_MS;
      state.frame = (state.frame + 1) % state.frameCount;
    }
    document.getElementById('frameSlider').value = state.frame;
    document.getElementById('frameLabel').textContent =
      `seq ${state.seqId} · frame ${state.frame + 1}/${state.frameCount}`;
  }

  const w = CANVAS.width;
  const h = CANVAS.height;
  const ptr = state.wasm.render(
    w, h, state.yaw, state.pitch, state.zoom,
    state.frameCount > 0 ? state.frame : -1);
  if (!ptr) return;

  const bytes = state.wasm.mod.HEAPU8.subarray(ptr, ptr + w * h * 4);
  CTX.putImageData(new ImageData(new Uint8ClampedArray(bytes), w, h), 0, 0);
}

/* ---- input --------------------------------------------------------------- */

function wireInput() {
  let dragging = false;
  let lastX = 0;
  let lastY = 0;

  CANVAS.addEventListener('pointerdown', (e) => {
    dragging = true;
    lastX = e.clientX;
    lastY = e.clientY;
    CANVAS.setPointerCapture(e.pointerId);
  });
  CANVAS.addEventListener('pointerup', (e) => {
    dragging = false;
    CANVAS.releasePointerCapture(e.pointerId);
  });
  CANVAS.addEventListener('pointermove', (e) => {
    if (!dragging) return;
    /* 2048 units per turn, so ~5.7 units per pixel gives a full turn across a
     * 360-pixel drag. */
    state.yaw = (state.yaw - (e.clientX - lastX) * 6) & 2047;
    state.pitch = Math.max(1, Math.min(511, state.pitch + (e.clientY - lastY) * 3));
    lastX = e.clientX;
    lastY = e.clientY;
  });
  CANVAS.addEventListener('wheel', (e) => {
    e.preventDefault();
    state.zoom = Math.max(200, Math.min(12000, state.zoom + e.deltaY * 3));
  }, { passive: false });

  document.getElementById('npcSearch').addEventListener('input', renderNpcList);
  /* The animation query survives changing npc on purpose: comparing "what is
   * each of these creatures' death animation" is the reason to have it. */
  document.getElementById('animSearch').addEventListener('input', renderAnimList);

  document.getElementById('playBtn').onclick = () => {
    state.playing = !state.playing;
    document.getElementById('playBtn').textContent = state.playing ? 'Pause' : 'Play';
  };
  document.getElementById('bindBtn').onclick = () => {
    state.wasm.clearAnim();
    state.seqId = null;
    state.frameCount = 0;
    state.frame = 0;
    state.playing = false;
    document.getElementById('playBtn').textContent = 'Play';
    updateFrameUi();
    renderAnimList();
  };
  document.getElementById('frameSlider').oninput = (e) => {
    state.playing = false;
    document.getElementById('playBtn').textContent = 'Play';
    state.frame = Number(e.target.value);
    updateFrameUi();
  };
}

/* ---- boot ---------------------------------------------------------------- */

(async function main() {
  const mod = await EVModule();
  state.wasm = wasmCall(mod);
  state.wasm.init();

  state.npcs = await (await fetch('/api/npcs.json')).json();
  renderNpcList();
  wireInput();
  requestAnimationFrame(frameLoop);

  /* Something on screen at once, rather than an empty canvas: the first npc
   * with a model of its own. */
  const first = state.npcs.find((n) => n.rig > 0) || state.npcs[0];
  if (first) selectNpc(first.id);
})();
