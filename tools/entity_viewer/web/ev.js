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
  lastT: 0,

  /*
   * Playback is counted in CLIENT CYCLES, not in frames.
   *
   * A frame index cannot express an attached graphic's timing: `spotanim_pl`'s
   * delay is a number of cycles between the animation starting and the graphic
   * starting, and the two sequences have different frame lengths, so there is
   * no frame number in one that names a moment in the other. Counting cycles
   * gives both a common clock, and each side's frame is looked up from it.
   */
  cycle: 0,
  cycleAcc: 0,
  bodyDelays: [],
  bodyTotal: 0,
  spotDelays: [],
  spotTotal: 0,

  /* 'npc' or 'player'. */
  mode: 'npc',
  objs: [],
  spotanims: [],
  worn: [],
  /* The attached graphic: `spotanim_pl`'s three arguments and the sequence the
   * record names. */
  fx: { id: null, seq: -1, height: 100, delay: 16, orient: -1 },
};

/** Which frame of a sequence is showing at `cycle`, or -1 once it has run out.
 *  -1 is a real state for a graphic: one that has finished is not drawn, and
 *  holding its last frame would leave it on screen for the whole recovery. */
function frameAtCycle(delays, cycle) {
  if (cycle < 0) return -1;
  let acc = 0;
  for (let i = 0; i < delays.length; i++) {
    const d = Math.max(1, delays[i]);
    if (cycle < acc + d) return i;
    acc += d;
  }
  return -1;
}

function totalCycles(delays) {
  return delays.reduce((a, d) => a + Math.max(1, d), 0);
}

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
    setFrameHeight: mod.cwrap('ev_w_set_frame_height', null, ['number']),
    setSpotModel: mod.cwrap('ev_w_set_spot_model', 'number', ['number', 'number']),
    setSpotAnim: mod.cwrap('ev_w_set_spot_anim', 'number', ['number', 'number']),
    clearSpot: mod.cwrap('ev_w_clear_spot', null, []),
    setSpotState: mod.cwrap('ev_w_set_spot_state', null, ['number', 'number']),
    spotFrameCount: mod.cwrap('ev_w_spot_frame_count', 'number', []),
    spotFrameDelay: mod.cwrap('ev_w_spot_frame_delay', 'number', ['number']),
    setModelHd: mod.cwrap('ev_w_set_model_hd', 'number', ['number', 'number']),
    clearModelHd: mod.cwrap('ev_w_clear_model_hd', null, []),
    modelHdActive: mod.cwrap('ev_w_model_hd_active', 'number', []),
    hdStats: mod.cwrap('ev_w_hd_stats', 'number', []),
    hdStatsCount: mod.cwrap('ev_w_hd_stats_count', 'number', []),
    setHdPlaceholder: mod.cwrap('ev_w_set_hd_placeholder', null, ['number']),
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

/* ---- a model file off disk ----------------------------------------------
 *
 * The browser cannot decode a cache model — that is rscache, and rscache is on
 * the server — so the file is POSTed and comes back as an ev_wire HD blob. The
 * server also reports what the file WAS, in headers, so the page can name the
 * format instead of guessing from whether the decode happened to work.
 */

/* The field order of struct ToriDraw_HDRenderStats. Kept beside the C rather
 * than derived, because the stats cross as a flat int array; a field added
 * there and not here shifts every later label onto the wrong number. */
const HD_STAT_FIELDS = [
  'faces', 'drawn_untextured', 'drawn_plane', 'drawn_cylinder', 'drawn_cube',
  'drawn_sphere', 'fallback_no_texels', 'fallback_no_mapping', 'skipped_hidden',
  'skipped_alpha', 'gate_opaque', 'gate_trans', 'gate_alpha', 'with_facealpha',
  'with_modulate',
];

function hdReadStats() {
  const w = state.wasm;
  if (!w || !w.modelHdActive()) return null;
  const ptr = w.hdStats();
  const n = w.hdStatsCount();
  if (!ptr || !n) return null;
  const words = new Int32Array(w.mod.HEAPU8.buffer, ptr, n);
  const out = {};
  for (let i = 0; i < Math.min(n, HD_STAT_FIELDS.length); i++) {
    out[HD_STAT_FIELDS[i]] = words[i];
  }
  return out;
}

function hdShowStats() {
  const el = document.getElementById('hdStats');
  const st = hdReadStats();
  if (!st) { el.classList.add('hidden'); return; }

  const rows = [
    ['faces', st.faces],
    ['texplane', st.drawn_plane],
    ['texcylinder', st.drawn_cylinder],
    ['texcube', st.drawn_cube],
    ['texsphere', st.drawn_sphere],
    ['untextured', st.drawn_untextured],
    ['gate op/tr/al', `${st.gate_opaque}/${st.gate_trans}/${st.gate_alpha}`],
    ['facealpha', st.with_facealpha],
    ['modulate', st.with_modulate],
    ['no texels', st.fallback_no_texels],
    ['no mapping', st.fallback_no_mapping],
    ['skipped', st.skipped_hidden + st.skipped_alpha],
  ];
  el.textContent = rows.map(([k, v]) => `${k.padEnd(14)}${v}`).join('\n');
  el.classList.remove('hidden');
}

async function hdLoadFile(file) {
  const info = document.getElementById('hdInfo');
  info.textContent = `reading ${file.name}…`;

  const bytes = new Uint8Array(await file.arrayBuffer());
  let res;
  try {
    res = await fetch('/api/modelfile', { method: 'POST', body: bytes });
  } catch (e) {
    info.textContent = `upload failed: ${e}`;
    return;
  }

  const format = res.headers.get('X-Model-Format') || 'unknown';
  if (!res.ok) {
    /* The server decodes what it can and 404s what it cannot, so a failure
     * here is "this is not a model archive this decoder handles" — worth
     * saying with the detected format rather than as a bare error. */
    info.textContent = `${file.name}: not decodable (looks like ${format})`;
    return;
  }

  const blob = new Uint8Array(await res.arrayBuffer());
  const ptr = state.wasm.alloc(blob.length);
  if (!ptr) { info.textContent = 'out of memory'; return; }
  state.wasm.mod.HEAPU8.set(blob, ptr);
  const faces = state.wasm.setModelHd(ptr, blob.length);
  state.wasm.release(ptr);

  if (!faces) { info.textContent = `${file.name}: blob rejected by the renderer`; return; }

  const textured = res.headers.get('X-Model-Textured') || '0';
  const hasMappings = res.headers.get('X-Model-HD') === '1';
  info.textContent =
    `${file.name} — ${format}, ${faces} faces, ${textured} textured` +
    (hasMappings ? ', mappings built' : ', no mappings');

  document.getElementById('hdClear').disabled = false;
  const ph2 = document.getElementById('hdPlaceholder');
  if (ph2 && ph2.checked) state.wasm.setHdPlaceholder(1);
  /* An HD model has no sequence, so playback would animate nothing. */
  state.playing = false;
  const play = document.getElementById('playBtn');
  if (play) play.textContent = 'Play';
  /* No explicit repaint: frameLoop is already running on rAF and will pick the
   * new subject up on its next tick, which is also when the stats become real
   * (they are filled by the render, not by adopting the model). */
}

function hdInstall() {
  const pick = document.getElementById('hdPick');
  const clear = document.getElementById('hdClear');
  const input = document.getElementById('hdInput');
  if (!pick || !input) return;

  const ph = document.getElementById('hdPlaceholder');
  if (ph) {
    ph.addEventListener('change', () => {
      /* Off is the faithful reading of a bare model file; on is the only way
       * the mapped kernels run at all, so the routing readout stays 0 for
       * cylinder/cube/sphere without it. */
      state.wasm.setHdPlaceholder(ph.checked ? 1 : 0);
    });
  }

  pick.addEventListener('click', () => input.click());
  input.addEventListener('change', () => {
    if (input.files && input.files[0]) hdLoadFile(input.files[0]);
    input.value = '';
  });
  clear.addEventListener('click', () => {
    state.wasm.clearModelHd();
    clear.disabled = true;
    document.getElementById('hdInfo').textContent = 'no file loaded';
    document.getElementById('hdStats').classList.add('hidden');
  });
}

/* ---- npc list ------------------------------------------------------------ */

function renderNpcList() {
  if (state.mode === 'player') { renderObjList(); return; }

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

/*
 * The equipment picker.
 *
 * Every obj in the cache, not only the wearable ones: filtering here would mean
 * decoding thirty thousand records to answer a question the picker can answer
 * by drawing nothing. An obj with no wear model simply adds no geometry, and
 * the chip stays so it can be taken off again.
 */
function renderObjList() {
  const q = document.getElementById('npcSearch').value.trim().toLowerCase();
  const list = document.getElementById('npcList');
  const matches = [];
  let total = 0;

  for (const o of state.objs) {
    if (q) {
      const hay = `${o.id} ${(o.name || '').toLowerCase()}`;
      if (!hay.includes(q)) continue;
    }
    total++;
    if (matches.length < 400) matches.push(o);
  }

  list.innerHTML = '';
  const frag = document.createDocumentFragment();
  for (const o of matches) {
    const on = state.worn.includes(o.id);
    const row = document.createElement('div');
    row.className = 'row' + (on ? ' sel' : '');
    row.innerHTML =
      `<span class="id">${o.id}</span>` +
      `<span class="nm">${escapeHtml(o.name || '(unnamed)')}</span>` +
      `<span class="badge">${on ? 'worn' : ''}</span>`;
    row.onclick = () => toggleWorn(o.id);
    frag.appendChild(row);
  }
  list.appendChild(frag);
  document.getElementById('npcCount').textContent =
    `${matches.length} of ${total} shown · click to equip or unequip`;
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
  state.bodyDelays = [];
  state.bodyTotal = 0;
  state.cycle = 0;
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
  /* The delay table, read once. Playback is on a cycle clock so that an
   * attached graphic's delay means something; that clock needs the lengths. */
  state.bodyDelays = [];
  for (let i = 0; i < frames; i++) state.bodyDelays.push(state.wasm.frameDelay(i));
  state.bodyTotal = totalCycles(state.bodyDelays);
  state.frame = 0;
  state.cycle = 0;
  state.cycleAcc = 0;
  state.playing = frames > 0;
  document.getElementById('playBtn').textContent = state.playing ? 'Pause' : 'Play';
  updateFrameUi();
  renderAnimList();
}

function updateFrameUi() {
  const slider = document.getElementById('frameSlider');
  slider.max = Math.max(0, state.bodyTotal - 1);
  slider.value = state.cycle;
  slider.disabled = state.frameCount === 0;
  document.getElementById('playBtn').disabled = state.frameCount === 0;
  updateFrameLabel();
}

/* ---- render loop --------------------------------------------------------- */

function frameLoop(t) {
  requestAnimationFrame(frameLoop);
  if (!state.wasm) return;

  const dt = state.lastT ? t - state.lastT : 0;
  state.lastT = t;

  if (state.playing && state.bodyTotal > 0) {
    state.cycleAcc += dt;
    while (state.cycleAcc >= TICK_MS) {
      state.cycleAcc -= TICK_MS;
      /*
       * The loop is over the BODY's length, and the graphic simply stops when
       * its own sequence runs out. That is the game's arrangement: the graphic
       * is fired once with a delay and plays through, it is not a second loop
       * running alongside.
       */
      state.cycle = (state.cycle + 1) % state.bodyTotal;
    }
    const f = frameAtCycle(state.bodyDelays, state.cycle);
    if (f >= 0) state.frame = f;
    document.getElementById('frameSlider').value = state.cycle;
    updateFrameLabel();
  }

  /* The graphic's own frame, from the same clock, offset by its delay. */
  if (state.fx.id !== null) {
    const sf = state.bodyTotal > 0
      ? frameAtCycle(state.spotDelays, state.cycle - state.fx.delay)
      : 0;
    state.wasm.setSpotState(state.fx.height, sf);
    const el = document.getElementById('fxState');
    if (el) {
      el.textContent = state.bodyTotal > 0
        ? (sf < 0 ? 'not showing' : `graphic frame ${sf + 1}/${state.spotDelays.length}`)
        : 'no animation — pick one to see the timing';
    }
  }

  /* The routing tally is a property of the last render, so it is read here and
   * not where the model was adopted. Throttled: it is a DOM write per field and
   * the numbers only change when the subject does. */
  if (state.wasm.modelHdActive()) {
    state.hdTick = (state.hdTick || 0) + 1;
    if (state.hdTick % 15 === 1) hdShowStats();
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

function updateFrameLabel() {
  document.getElementById('frameLabel').textContent = state.frameCount
    ? `seq ${state.seqId} · cycle ${state.cycle}/${state.bodyTotal} · frame ` +
      `${state.frame + 1}/${state.frameCount}`
    : 'bind pose';
}

/* ---- the player half ------------------------------------------------------
 *
 * Everything below exists because a player is not an npc with other models on
 * it, and an attached graphic is not a second thing in the scene. The client
 * merges the posed graphic into the player's own model, so the viewer asks the
 * server for the two halves and lets ev_render merge them the same way.
 */

async function rebuildPlayer() {
  const q = state.worn.length ? `?wear=${state.worn.join(',')}` : '';
  const faces = await feed(`/api/player.model${q}`, (p, n) => state.wasm.setModel(p, n));
  const meta = document.getElementById('stageMeta');
  if (!faces) {
    meta.textContent = 'the player model did not build';
    return;
  }
  const h = state.wasm.modelHeight();
  state.zoom = Math.max(260, Math.min(6000, Math.round(h * 3) || 900));
  /*
   * Pin the framing to the player alone, measured before any graphic is merged
   * in. The framing lifts the model by half the height it measures, and a large
   * attached graphic more than doubles the combined bounds — so leaving it to
   * the renderer makes the player shrink and jump the moment the graphic
   * appears, under the thing being looked at.
   */
  state.wasm.setFrameHeight(h);
  meta.textContent =
    `${faces} faces · ${state.worn.length} item(s) worn · rig 0 (the human rig)` +
    ' · drag to orbit · wheel to zoom';
  const names = state.worn.map((id) => {
    const o = state.objs.find((x) => x.id === id);
    return o && o.name ? o.name : String(id);
  });
  document.getElementById('stageTitle').textContent =
    'Player' + (names.length ? ` — wearing ${names.join(', ')}` : ' — unequipped');
}

function renderWornBar() {
  const bar = document.getElementById('wornBar');
  bar.innerHTML = '';
  if (!state.worn.length) {
    bar.innerHTML = '<span class="note" style="padding:0">Nothing equipped — click an obj.</span>';
    return;
  }
  for (const id of state.worn) {
    const o = state.objs.find((x) => x.id === id);
    const chip = document.createElement('span');
    chip.className = 'chip';
    chip.innerHTML = `${escapeHtml(o && o.name ? o.name : String(id))}<span class="x">×</span>`;
    chip.title = `obj ${id}`;
    chip.onclick = () => { toggleWorn(id); };
    bar.appendChild(chip);
  }
}

async function toggleWorn(id) {
  const at = state.worn.indexOf(id);
  if (at >= 0) state.worn.splice(at, 1);
  else state.worn.push(id);
  renderWornBar();
  renderNpcList();
  await rebuildPlayer();
}

/** Adopt a graphic: its model, and the sequence its own record names. */
async function selectSpotanim(id) {
  if (id === null) {
    state.fx.id = null;
    state.wasm.clearSpot();
    state.spotDelays = [];
    state.spotTotal = 0;
    document.getElementById('fxState').textContent = '';
    return;
  }

  let info;
  try {
    const res = await fetch(`/api/spot/${id}.json`);
    if (!res.ok) throw new Error('no such graphic');
    info = await res.json();
  } catch (e) {
    document.getElementById('fxState').textContent = `spotanim ${id} did not load`;
    return;
  }

  const turn = state.fx.orient >= 0 ? `?orient=${state.fx.orient}` : '';
  const faces = await feed(`/api/spot/${id}.model${turn}`,
    (p, n) => state.wasm.setSpotModel(p, n));
  if (!faces) {
    document.getElementById('fxState').textContent = `spotanim ${id} has no model`;
    return;
  }

  state.fx.id = id;
  state.fx.seq = info.seq;
  state.spotDelays = [];
  if (info.seq >= 0) {
    const n = await feed(`/api/seq/${info.seq}.anim`, (p, k) => state.wasm.setSpotAnim(p, k));
    for (let i = 0; i < n; i++) state.spotDelays.push(state.wasm.spotFrameDelay(i));
  }
  state.spotTotal = totalCycles(state.spotDelays);
  document.getElementById('fxState').textContent =
    `${faces} faces · seq ${info.seq} · ${state.spotDelays.length} frames, ` +
    `${state.spotTotal} cycles`;
}

function fillSpotanimList() {
  const dl = document.getElementById('fxList');
  dl.innerHTML = '';
  const frag = document.createDocumentFragment();
  for (const s of state.spotanims) {
    const o = document.createElement('option');
    o.value = s.name ? `${s.name} (${s.id})` : String(s.id);
    frag.appendChild(o);
  }
  dl.appendChild(frag);
}

/** `name (1231)` or `1231` -> 1231. */
function parseSpotanimEntry(text) {
  const m = /\((\d+)\)\s*$/.exec(text.trim());
  if (m) return Number(m[1]);
  if (/^\d+$/.test(text.trim())) return Number(text.trim());
  const hit = state.spotanims.find((s) => s.name === text.trim());
  return hit ? hit.id : null;
}

async function setMode(mode) {
  state.mode = mode;
  document.getElementById('modeNpc').classList.toggle('on', mode === 'npc');
  document.getElementById('modePlayer').classList.toggle('on', mode === 'player');
  document.getElementById('wornBar').classList.toggle('hidden', mode !== 'player');
  document.getElementById('fxBar').classList.toggle('hidden', mode !== 'player');
  document.getElementById('subjectHead').textContent = mode === 'player' ? 'Equipment' : 'NPCs';
  document.getElementById('npcSearch').placeholder =
    mode === 'player' ? 'obj name or id' : 'name, gameval or id';

  if (mode === 'npc') {
    /* Leaving player mode drops the graphic and the pinned framing with it:
     * both are player-half state, and an npc carrying a merged player graphic
     * would be a lie about what the cache says. */
    await selectSpotanim(null);
    state.wasm.setFrameHeight(0);
    renderNpcList();
    if (state.npcId !== null) await selectNpc(state.npcId);
    return;
  }

  if (!state.objs.length) {
    state.objs = await (await fetch('/api/objs.json')).json();
    state.spotanims = await (await fetch('/api/spotanims.json')).json();
    fillSpotanimList();
  }
  if (!state.detail || state.detail.framemap !== 0) {
    state.detail = await (await fetch('/api/rig/0.json')).json();
  }
  renderWornBar();
  renderNpcList();
  renderAnimList();
  await rebuildPlayer();
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
    state.bodyDelays = [];
    state.bodyTotal = 0;
    state.cycle = 0;
    state.playing = false;
    document.getElementById('playBtn').textContent = 'Play';
    updateFrameUi();
    renderAnimList();
  };
  /* 512 of 2048 is a quarter turn: the camera directly above, which is the one
   * view where a graphic's placement in the player's local xz plane is visible
   * as a distance rather than inferred from foreshortening. */
  document.getElementById('topBtn').onclick = () => { state.pitch = 512; };
  document.getElementById('frameSlider').oninput = (e) => {
    state.playing = false;
    document.getElementById('playBtn').textContent = 'Play';
    state.cycle = Number(e.target.value);
    const f = frameAtCycle(state.bodyDelays, state.cycle);
    if (f >= 0) state.frame = f;
    updateFrameUi();
  };

  document.getElementById('modeNpc').onclick = () => setMode('npc');
  document.getElementById('modePlayer').onclick = () => setMode('player');

  document.getElementById('fxPick').addEventListener('change', (e) => {
    const id = parseSpotanimEntry(e.target.value);
    if (id === null) {
      document.getElementById('fxState').textContent = 'no graphic by that name or id';
      return;
    }
    selectSpotanim(id);
  });
  document.getElementById('fxHeight').addEventListener('input', (e) => {
    state.fx.height = Number(e.target.value) || 0;
  });
  document.getElementById('fxDelay').addEventListener('input', (e) => {
    state.fx.delay = Number(e.target.value) || 0;
  });
  document.getElementById('fxOrient').addEventListener('change', (e) => {
    state.fx.orient = Number(e.target.value);
    /* The rotation is baked into the model at build time, so changing it means
     * asking the server for the model again — it is not a draw-time knob. */
    if (state.fx.id !== null) selectSpotanim(state.fx.id);
  });
  document.getElementById('fxClear').onclick = () => {
    document.getElementById('fxPick').value = '';
    selectSpotanim(null);
  };
}

/* ---- boot ---------------------------------------------------------------- */

/*
 * A viewer state in the URL: `#player&wear=22325&seq=8056&fx=1231&delay=16`.
 *
 * Worth having for two reasons. A particular combination of weapon, animation,
 * graphic and delay is a finding, and a finding that can only be reproduced by
 * describing which four boxes to fill in is a finding nobody checks. And it is
 * what lets a headless browser open the viewer already configured, which is how
 * this page is tested at all.
 *
 * Recognised: `player`, `wear=` (comma separated obj ids), `npc=`, `seq=`,
 * `fx=` (spotanim id), `delay=`, `height=`, `orient=`, `pitch=`, `yaw=`,
 * `zoom=`, `paused`, `cycle=`.
 */
function hashParams() {
  const out = {};
  const raw = location.hash.replace(/^#/, '');
  if (!raw) return out;
  for (const part of raw.split('&')) {
    if (!part) continue;
    const eq = part.indexOf('=');
    if (eq < 0) out[part] = true;
    else out[part.slice(0, eq)] = part.slice(eq + 1);
  }
  return out;
}

async function applyHash(p) {
  if (p.pitch !== undefined) state.pitch = Number(p.pitch);
  if (p.yaw !== undefined) state.yaw = Number(p.yaw) & 2047;

  if (p.player !== undefined || p.wear !== undefined) {
    if (p.wear !== undefined) {
      state.worn = String(p.wear).split(',').filter(Boolean).map(Number);
    }
    await setMode('player');
  }
  if (p.npc !== undefined) await selectNpc(Number(p.npc));
  if (p.seq !== undefined) await selectSeq(Number(p.seq));

  if (p.height !== undefined) {
    state.fx.height = Number(p.height);
    document.getElementById('fxHeight').value = state.fx.height;
  }
  if (p.delay !== undefined) {
    state.fx.delay = Number(p.delay);
    document.getElementById('fxDelay').value = state.fx.delay;
  }
  if (p.orient !== undefined) {
    state.fx.orient = Number(p.orient);
    document.getElementById('fxOrient').value = String(state.fx.orient);
  }
  if (p.fx !== undefined) {
    await selectSpotanim(Number(p.fx));
    const s = state.spotanims.find((x) => x.id === Number(p.fx));
    document.getElementById('fxPick').value = s && s.name ? `${s.name} (${s.id})` : String(p.fx);
  }

  /* Zoom last: rebuilding the player recomputes it from the model's height, so
   * an explicit one set earlier would be overwritten. */
  if (p.zoom !== undefined) state.zoom = Number(p.zoom);
  if (p.cycle !== undefined) {
    state.cycle = Number(p.cycle);
    const f = frameAtCycle(state.bodyDelays, state.cycle);
    if (f >= 0) state.frame = f;
  }
  if (p.paused !== undefined) {
    state.playing = false;
    document.getElementById('playBtn').textContent = 'Play';
  }
  updateFrameUi();
}

(async function main() {
  const mod = await EVModule();
  state.wasm = wasmCall(mod);
  state.wasm.init();

  state.npcs = await (await fetch('/api/npcs.json')).json();
  renderNpcList();
  wireInput();
  hdInstall();
  requestAnimationFrame(frameLoop);

  const p = hashParams();
  if (Object.keys(p).length) {
    await applyHash(p);
  } else {
    /* Something on screen at once, rather than an empty canvas: the first npc
     * with a model of its own. */
    const first = state.npcs.find((n) => n.rig > 0) || state.npcs[0];
    if (first) selectNpc(first.id);
  }
  window.__evReady = true;
})();
