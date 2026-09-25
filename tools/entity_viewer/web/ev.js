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

  /* 'npc', 'obj', 'loc' or 'player'. */
  mode: 'npc',
  objs: [],
  locs: [],
  spotanims: [],
  worn: [],

  /*
   * The obj and loc subjects.
   *
   * Each carries a *part* alongside its id, and the part is not cosmetic: an
   * obj record holds several unrelated meshes (the item, the two wear sets, the
   * chatheads) and a loc holds one per shape. "obj 4151" does not name a model
   * on its own, so the variant and the shape are as much the subject as the id
   * is — which is why both are in the URL.
   */
  objId: null,
  objVariant: 0,
  objDetail: null,
  locId: null,
  locShape: null,
  locDetail: null,
  /* The attached graphic: `spotanim_pl`'s three arguments and the sequence the
   * record names. */
  fx: { id: null, seq: -1, height: 100, delay: 16, orient: -1 },

  /* The server's rig walk over the active cache. `null` until the first poll
   * answers; see watchRigs. */
  rigs: null,
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
    setTextures: mod.cwrap('ev_w_set_textures', 'number', ['number', 'number']),
    textureCount: mod.cwrap('ev_w_texture_count', 'number', []),
    setPan: mod.cwrap('ev_w_set_pan', null, ['number', 'number']),
    setIgnorePriorities: mod.cwrap('ev_w_set_ignore_priorities', null, ['number']),
    setZbufferKernels: mod.cwrap('ev_w_set_zbuffer_kernels', null, ['number']),
    move: mod.cwrap('ev_w_move', null, ['number', 'number', 'number']),
    moveReset: mod.cwrap('ev_w_move_reset', null, []),
    mod,
  };
}

/** Hand a fetched blob to the module: copy into its heap, call, free. */
async function feed(url, fn) {
  const res = await fetch(url);
  if (!res.ok) return 0;
  const bytes = new Uint8Array(await res.arrayBuffer());
  return push(bytes, fn);
}

/** The same, for bytes already in hand. */
function push(bytes, fn) {
  const ptr = state.wasm.alloc(bytes.length);
  if (!ptr) return 0;
  state.wasm.mod.HEAPU8.set(bytes, ptr);
  const out = fn(ptr, bytes.length);
  state.wasm.release(ptr);
  return out;
}

/*
 * Load a model AND the textures its faces name.
 *
 * The two are separate requests because they have different lifetimes — a
 * texture set belongs to the cache, a model to the subject — but they have to
 * arrive together, or the first frame draws a textured model with nothing to
 * sample. The server names the ids in a header rather than embedding them, so
 * the model blob stays the renderer's own format.
 *
 * An HD blob is routed to setModelHd: an RS2-era npc's faces are mostly
 * cylinder- and cube-mapped, and the classic raster can only plane-map, so
 * feeding one to setModel drops every mapped face.
 */
async function loadModelWithTextures(url) {
  const res = await fetch(url);
  if (!res.ok) return 0;
  const bytes = new Uint8Array(await res.arrayBuffer());
  const magic = magicOf(bytes);
  const faces = push(bytes, (p, n) =>
    magic === EV_MAGIC_MODEL_HD ? state.wasm.setModelHd(p, n) : state.wasm.setModel(p, n));

  if (magic !== EV_MAGIC_MODEL_HD) state.wasm.clearModelHd();

  const ids = res.headers.get('X-Texture-Ids');
  state.textureCount = 0;
  if (ids) {
    state.textureCount =
      await feed(`/api/textures.bin?ids=${encodeURIComponent(ids)}`,
                 (p, n) => state.wasm.setTextures(p, n));
  } else {
    /* Clear rather than keep: the previous subject's textures are the previous
     * subject's, and stale ids would be sampled by the new model's faces. */
    push(new Uint8Array(0), (p, n) => state.wasm.setTextures(p, n));
  }
  return faces;
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

/*
 * An exported animation (.eva) is already ev_wire, so it loads straight into
 * the module with no server round trip — unlike a model archive, which is cache
 * bytes and needs rscache to decode. Sniffing the magic is what lets one code
 * path serve both, and what makes a wrong file say so instead of being POSTed
 * to an endpoint that will reject it for the wrong reason.
 */
const EV_MAGIC_ANIM = 'EVA1';
const EV_MAGIC_MODEL = 'EVM1';
const EV_MAGIC_MODEL_HD = 'EVH1';

function magicOf(bytes) {
  if (bytes.length < 4) return '';
  return String.fromCharCode(bytes[0], bytes[1], bytes[2], bytes[3]);
}

/*
 * Open the model-file panel.
 *
 * It is collapsed by default because it is the biggest block in a column whose
 * height the subject list needs — but everything it reports (which format the
 * file was, which kernel each face reached, why a fallback fired) is written
 * INTO it, so anything that loads a file has to open it. A readout nobody can
 * see is the same as no readout.
 */
function hdReveal() {
  const box = document.getElementById('hdBox');
  if (box) box.open = true;
}

async function hdLoadAnimFile(file) {
  hdReveal();
  const info = document.getElementById('hdInfo');
  const bytes = new Uint8Array(await file.arrayBuffer());
  const magic = magicOf(bytes);

  if (magic !== EV_MAGIC_ANIM) {
    info.textContent =
      `${file.name}: not an ev_wire animation (magic "${magic}", expected EVA1)`;
    return;
  }

  const ptr = state.wasm.alloc(bytes.length);
  if (!ptr) { info.textContent = 'out of memory'; return; }
  state.wasm.mod.HEAPU8.set(bytes, ptr);
  const frames = state.wasm.setAnim(ptr, bytes.length);
  state.wasm.release(ptr);

  if (!frames) { info.textContent = `${file.name}: rejected by the renderer`; return; }

  /* Drive playback off the animation's own delays, exactly as a cache-loaded
   * sequence does — an uploaded animation is not a special case once adopted. */
  state.bodyDelays = [];
  for (let i = 0; i < frames; i++) state.bodyDelays.push(state.wasm.frameDelay(i));
  state.bodyTotal = state.bodyDelays.reduce((a, b) => a + b, 0);
  state.cycle = 0;
  state.frame = 0;
  state.playing = true;
  const play = document.getElementById('playBtn');
  if (play) play.textContent = 'Pause';
  const slider = document.getElementById('frameSlider');
  if (slider) { slider.max = Math.max(0, state.bodyTotal - 1); slider.value = 0; }

  info.textContent =
    `${info.textContent.split(' — ')[0]} — animation ${file.name}, ${frames} frames`;
}

async function hdLoadFile(file) {
  hdReveal();
  const info = document.getElementById('hdInfo');
  info.textContent = `reading ${file.name}…`;

  const bytes = new Uint8Array(await file.arrayBuffer());

  /* An ev_wire blob needs no server: it is already the renderer's own format. */
  const magic = magicOf(bytes);
  if (magic === EV_MAGIC_ANIM) {
    info.textContent = `${file.name} is an animation, not a model — loading it as one`;
    await hdLoadAnimFile(file);
    return;
  }
  if (magic === EV_MAGIC_MODEL || magic === EV_MAGIC_MODEL_HD) {
    const ptr = state.wasm.alloc(bytes.length);
    if (!ptr) { info.textContent = 'out of memory'; return; }
    state.wasm.mod.HEAPU8.set(bytes, ptr);
    const faces = magic === EV_MAGIC_MODEL_HD
      ? state.wasm.setModelHd(ptr, bytes.length)
      : state.wasm.setModel(ptr, bytes.length);
    state.wasm.release(ptr);
    if (!faces) { info.textContent = `${file.name}: blob rejected`; return; }
    info.textContent = `${file.name} — ev_wire ${magic}, ${faces} faces`;
    document.getElementById('hdClear').disabled = false;
    return;
  }

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

  /* Applies to every subject, not just an uploaded file: the question "are this
   * model's priorities meaningful" is asked of cache npcs at least as often. */
  const nopri = document.getElementById('noPriorities');
  if (nopri) {
    nopri.addEventListener('change', () => {
      state.wasm.setIgnorePriorities(nopri.checked ? 1 : 0);
    });
  }

  /* Independent of the one above on purpose: this drops the sort itself, that
   * one only drops its ranking key. Both apply to cache npcs as well as to an
   * uploaded file. */
  const zbufk = document.getElementById('zbufKernels');
  if (zbufk) {
    zbufk.addEventListener('change', () => {
      state.wasm.setZbufferKernels(zbufk.checked ? 1 : 0);
    });
  }

  pick.addEventListener('click', () => input.click());
  input.addEventListener('change', () => {
    if (input.files && input.files[0]) hdLoadFile(input.files[0]);
    input.value = '';
  });

  const animPick = document.getElementById('hdAnimPick');
  const animInput = document.getElementById('hdAnimInput');
  if (animPick && animInput) {
    animPick.addEventListener('click', () => animInput.click());
    animInput.addEventListener('change', () => {
      if (animInput.files && animInput.files[0]) hdLoadAnimFile(animInput.files[0]);
      animInput.value = '';
    });
  }
  clear.addEventListener('click', () => {
    state.wasm.clearModelHd();
    state.wasm.clearAnim();
    clear.disabled = true;
    document.getElementById('hdInfo').textContent = 'no file loaded';
    document.getElementById('hdStats').classList.add('hidden');
  });
}

/* ---- the subject list ---------------------------------------------------- */

/*
 * One column, four subjects. The list is the same widget every time — an id, a
 * name, a badge — so the modes differ in what fills it and what a click does,
 * not in how it is drawn.
 */
function renderNpcList() {
  if (state.mode === 'player') { renderObjList(); return; }
  if (state.mode === 'obj') {
    renderRecordList(state.objs, state.objId, selectObj, 'click to draw the item');
    return;
  }
  if (state.mode === 'loc') {
    renderRecordList(state.locs, state.locId, selectLoc, 'click to draw the loc');
    return;
  }

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
  /* While the npc pass is still running every badge reads 0/0, which looks
   * exactly like a cache whose npcs share no rigs. Say which it is. */
  const counting = state.rigs && !state.rigs.npcs && state.rigs.state !== 'failed';
  document.getElementById('npcCount').textContent =
    `${matches.length} of ${total} shown · ` +
    (counting ? 'still matching rigs — badges fill in' : 'badge is rig/maybe counts');
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

/*
 * The obj and loc pickers.
 *
 * Both lists are the cache's own records — 30,000 objs and 62,000 locs on
 * cache.osrs239 — so the search box is not a nicety, and the cap on rows built
 * is what keeps typing from stuttering. The count line still reports the true
 * total, so a search that matches 900 rows says so rather than looking like it
 * matched 400.
 *
 * The gameval name is searched as well as shown, because a loc's cache name is
 * "Door" several hundred times over and the gameval is the only thing that
 * tells those apart.
 */
function renderRecordList(rows, selectedId, onPick, hint) {
  const q = document.getElementById('npcSearch').value.trim().toLowerCase();
  const list = document.getElementById('npcList');
  const matches = [];
  let total = 0;

  for (const r of rows) {
    if (q) {
      const hay = `${r.id} ${(r.name || '').toLowerCase()} ${(r.gameval || '').toLowerCase()}`;
      if (!hay.includes(q)) continue;
    }
    total++;
    if (matches.length < 400) matches.push(r);
  }

  list.innerHTML = '';
  const frag = document.createDocumentFragment();
  for (const r of matches) {
    const row = document.createElement('div');
    row.className = 'row' + (r.id === selectedId ? ' sel' : '');
    row.innerHTML =
      `<span class="id">${r.id}</span>` +
      `<span class="nm">${escapeHtml(r.name || '(unnamed)')}</span>` +
      `<span class="gv" style="flex:0 1 auto">${escapeHtml(r.gameval || '')}</span>`;
    row.title = r.gameval || '';
    row.onclick = () => onPick(r.id);
    frag.appendChild(row);
  }
  list.appendChild(frag);
  document.getElementById('npcCount').textContent =
    `${matches.length} of ${total} shown · ${hint}`;
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

  const faces = await loadModelWithTextures(`/api/npc/${id}.model`);
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
      (state.textureCount ? ` · ${state.textureCount} textures` : '') +
      ' · drag or arrows to orbit · wheel to zoom · WASD/RF to fly';
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

/* ---- obj and loc --------------------------------------------------------- */

/**
 * Frame whatever was just loaded, and say what it is.
 *
 * The zoom is derived from the model's own height for the same reason the npc
 * path derives it: these two subjects run from a coin to a castle wall, and one
 * fixed distance shows one of those and not the other.
 */
function frameLoadedModel(faces, none, extra) {
  const meta = document.getElementById('stageMeta');
  if (!faces) {
    meta.textContent = none;
    return;
  }
  const h = state.wasm.modelHeight();
  state.zoom = Math.max(260, Math.min(6000, Math.round(h * 1.6) || 900));
  meta.textContent =
    `${faces} faces` + (extra ? ` · ${extra}` : '') +
    (state.textureCount ? ` · ${state.textureCount} textures` : '') +
    ' · drag or arrows to orbit · wheel to zoom · WASD/RF to fly';
}

/** The part bar: an obj's model variants, or a loc's shapes. */
function renderPartBar(parts, selected, onPick) {
  const bar = document.getElementById('partBar');
  bar.innerHTML = '';
  bar.classList.toggle('hidden', !parts.length);
  for (const p of parts) {
    const chip = document.createElement('span');
    chip.className = 'chip' + (p.key === selected ? ' on' : '');
    chip.innerHTML =
      escapeHtml(p.label) + `<span class="sub">${escapeHtml(p.sub || '')}</span>`;
    chip.title = p.title || '';
    chip.onclick = () => onPick(p.key);
    bar.appendChild(chip);
  }
}

async function selectObj(id, variant) {
  state.objId = id;
  state.seqId = null;

  if (!state.objDetail || state.objDetail.id !== id) {
    state.objDetail = await (await fetch(`/api/obj/${id}.json`)).json();
  }
  const d = state.objDetail;
  const have = d.variants.map((v) => v.variant);
  /* Keep the variant across a change of subject when the new record has it —
   * comparing two items' wear models means not re-picking "male" each time —
   * and fall back to whatever the record does have. */
  state.objVariant = have.includes(variant) ? variant
    : have.includes(state.objVariant) ? state.objVariant
      : have.length ? have[0] : 0;

  renderNpcList();
  renderPartBar(
    d.variants.map((v) => ({
      key: v.variant,
      label: v.label,
      sub: v.models.join(', '),
      title: `model ${v.models.join(', ')}`,
    })),
    state.objVariant,
    (v) => selectObj(id, v));

  document.getElementById('stageTitle').textContent =
    `${d.name || '(unnamed)'} — obj ${id}` + (d.gameval ? ` · ${d.gameval}` : '');

  const faces = await loadModelWithTextures(
    `/api/obj/${id}.model?variant=${state.objVariant}`);
  frameLoadedModel(
    faces,
    d.variants.length
      ? 'that variant’s models are not in this cache'
      : 'this obj names no model at all',
    d.variants.length ? `${ev_variantLabel(d, state.objVariant)} model` : '');

  /*
   * An obj has no animation of its own: nothing in the record names a sequence,
   * and its models carry no rig the game ever poses them on. So the panel says
   * that rather than showing an empty rig list, which would read as "the search
   * has not finished".
   */
  state.detail = null;
  clearAnimPlayback();
  renderAnimList();
}

function ev_variantLabel(detail, variant) {
  const hit = detail.variants.find((v) => v.variant === variant);
  return hit ? hit.label : 'model';
}

async function selectLoc(id, shape) {
  state.locId = id;

  if (!state.locDetail || state.locDetail.id !== id) {
    state.locDetail = await (await fetch(`/api/loc/${id}.json`)).json();
    /* A fresh loc drops the previous one's shape: shape numbers are the same
     * space for every loc, so keeping shape 9 across a click would silently ask
     * a tree for its diagonal wall and draw nothing. */
    state.locShape = null;
  }
  const d = state.locDetail;
  const have = d.shapes.map((s) => s.shape);
  state.locShape = have.includes(shape) ? shape
    : have.includes(state.locShape) ? state.locShape
      : have.length ? have[0] : null;

  state.detail = d;
  renderNpcList();
  renderPartBar(
    d.shapes.map((s) => ({
      key: s.shape,
      label: s.label,
      sub: s.shape >= 0 ? `#${s.shape}` : '',
      title: `models ${s.models.join(', ') || 'none'}`,
    })),
    state.locShape,
    (s) => selectLoc(id, s));

  document.getElementById('stageTitle').textContent =
    `${d.name || '(unnamed)'} — loc ${id}` + (d.gameval ? ` · ${d.gameval}` : '');

  const q = state.locShape === null ? '' : `?shape=${state.locShape}`;
  const faces = await loadModelWithTextures(`/api/loc/${id}.model${q}`);
  frameLoadedModel(
    faces,
    d.shapes.length
      ? 'that shape’s models are not in this cache'
      : 'this loc names no model at all',
    `${d.size_x}x${d.size_z} tiles` + (d.mirrored ? ' · mirrored' : ''));

  clearAnimPlayback();
  renderAnimList();
  /* A loc that names a sequence is nearly always animated by it — a fire, a
   * wheel, a windmill — so play that one rather than making the rig list the
   * place where "does this move" is answered. */
  if (d.seq >= 0) await selectSeq(d.seq);
}

/** Drop whatever was playing. A new subject's frames are not the old one's. */
function clearAnimPlayback() {
  state.wasm.clearAnim();
  state.seqId = null;
  state.frameCount = 0;
  state.frame = 0;
  state.bodyDelays = [];
  state.bodyTotal = 0;
  state.cycle = 0;
  updateFrameUi();
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

/* What the empty animation panel should say, which is different per mode: an
 * obj genuinely has no animation to find, while the other three have one to
 * pick. Saying "pick an npc" in obj mode would read as a page that has lost
 * track of what it is showing. */
const ANIM_EMPTY = {
  npc: 'Pick an npc.',
  obj: 'An obj names no sequence, and its models carry no rig the game poses ' +
       'them on — there is nothing to animate.',
  loc: 'Pick a loc.',
  player: 'Pick an equipment set.',
};

function renderAnimList() {
  const el = document.getElementById('animList');
  const d = state.detail;
  el.innerHTML = '';
  if (!d) {
    el.innerHTML = `<div class="note">${escapeHtml(ANIM_EMPTY[state.mode])}</div>`;
    document.getElementById('animCount').textContent = '';
    return;
  }

  const q = document.getElementById('animSearch').value.trim().toLowerCase();
  const rig = d.rig.filter((a) => animMatches(a, q));

  const frag = document.createDocumentFragment();

  /* A loc reaches its rig through the ONE sequence its record names, where an
   * npc seeds from a handful. Both answer the same question — what else is
   * built on this rig — so the note says which seed produced the list. */
  frag.appendChild(group(
    'rig', `Rigging matches (${countLabel(rig.length, d.rig.length)})`,
    (state.mode !== 'loc'
      ? 'Sequences built on this npc’s own rig. '
      : d.seq >= 0
        ? `Sequences built on the rig of this loc’s own sequence (${d.seq}). `
        : 'A loc reaches a rig only through the sequence its record names, and ' +
          'this one names none. ') +
    'Concrete: sharing a rig is the precondition for an animation applying at all.'));

  for (const a of rig) {
    /* A skeletal sequence poses through the model's Animaya skin. An npc
     * without one is left in its bind pose rather than mis-animated, so say so
     * on the row instead of letting it look like a broken animation. */
    const unplayable = a.skeletal && !d.skinned;
    const badge = (a.skeletal ? 'skeletal · ' : '') + `${a.frames} frames`;
    frag.appendChild(animRow(a.seq, a.name, badge, false, unplayable));
  }

  /* An empty list means two entirely different things and they must not look
   * alike: this npc names no animation, or the walk that would find them has
   * not finished. The second is temporary and the page is already polling. */
  if (!rig.length) {
    const n = document.createElement('div');
    n.className = 'note';
    n.textContent = d.rigs_pending
      ? 'Searching this cache for the animations that apply…'
      : d.rig.length
        ? 'No rigging matches match the search.'
        : state.mode === 'loc'
          ? 'This loc names no sequence, so nothing shares its rig.'
          : 'This npc names no animation, so nothing shares its rig.';
    frag.appendChild(n);
  }

  /*
   * Name guesses are an npc-only idea and the section is left out entirely for
   * anything else. They come from the catalog, which scores an npc's gameval
   * name against every sequence's; nothing walks objs or locs that way, so the
   * list would be empty for a reason the heading cannot express — and an empty
   * "Name guesses (0)" reads as "none found" rather than "not asked".
   */
  if (state.mode !== 'npc') {
    el.appendChild(frag);
    document.getElementById('animCount').textContent =
      `${countLabel(rig.length, d.rig.length)} rig`;
    return;
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

  /* Once per frame, not per keydown: see wireInput. */
  if (state.applyFly) state.applyFly(dt);
  if (state.applyRotate) state.applyRotate(dt);

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
    ' · drag or arrows to orbit · wheel to zoom';
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

const MODE_HEADS = { npc: 'NPCs', obj: 'Objs', loc: 'Locs', player: 'Equipment' };
const MODE_PLACEHOLDERS = {
  npc: 'name, gameval or id',
  obj: 'item name, gameval or id',
  loc: 'loc name, gameval or id',
  player: 'obj name or id',
};

/** Every obj in the cache, fetched once. 30,000 rows, so not per switch. */
async function ensureObjs() {
  if (!state.objs.length) state.objs = await (await fetch('/api/objs.json')).json();
}

async function setMode(mode) {
  state.mode = mode;
  for (const m of ['npc', 'obj', 'loc', 'player']) {
    const btn = document.getElementById('mode' + m[0].toUpperCase() + m.slice(1));
    btn.classList.toggle('on', mode === m);
  }
  document.getElementById('wornBar').classList.toggle('hidden', mode !== 'player');
  document.getElementById('fxBar').classList.toggle('hidden', mode !== 'player');
  /* The part bar belongs to whichever of the two carries parts, and it is
   * rebuilt by the selection — hiding it here stops the previous subject's
   * variants sitting above the new one's list. */
  document.getElementById('partBar').classList.toggle(
    'hidden', mode !== 'obj' && mode !== 'loc');
  document.getElementById('subjectHead').textContent = MODE_HEADS[mode];
  document.getElementById('npcSearch').placeholder = MODE_PLACEHOLDERS[mode];

  if (mode !== 'player') {
    /* Leaving player mode drops the graphic and the pinned framing with it:
     * both are player-half state, and any other subject carrying a merged
     * player graphic would be a lie about what the cache says. */
    await selectSpotanim(null);
    state.wasm.setFrameHeight(0);
  }

  /* Nothing selected in the new mode means no animation list either. Leaving
   * the previous subject's would put a loc's sequences under an npc heading,
   * and the panel has no way to say they are not this subject's. */
  if (mode === 'npc') {
    renderNpcList();
    if (state.npcId !== null) return await selectNpc(state.npcId);
    state.detail = null;
    renderAnimList();
    return;
  }

  if (mode === 'obj') {
    await ensureObjs();
    renderNpcList();
    if (state.objId !== null) return await selectObj(state.objId, state.objVariant);
    state.detail = null;
    renderAnimList();
    return;
  }

  if (mode === 'loc') {
    if (!state.locs.length) state.locs = await (await fetch('/api/locs.json')).json();
    renderNpcList();
    if (state.locId !== null) return await selectLoc(state.locId, state.locShape);
    state.detail = null;
    renderAnimList();
    return;
  }

  await ensureObjs();
  if (!state.spotanims.length) {
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

/* ---- the cache registry -------------------------------------------------
 *
 * Every mutation returns the whole list, so there is one render path and no
 * chance of the page's idea of what is active drifting from the server's.
 */
async function renderCaches(data) {
  const list = document.getElementById('cacheList');
  const active = document.getElementById('cacheActive');
  if (!list) return;

  list.innerHTML = '';
  (data.caches || []).forEach((c) => {
    const row = document.createElement('div');
    row.className = 'row' + (c.index === data.active ? ' sel' : '');
    const counts = c.indexed
      ? `${c.npcs} npcs · ${c.seqs} seqs · ${c.models} models`
      : 'not yet indexed';
    row.innerHTML =
      `<span class="cname">${c.label}</span>` +
      `<span class="crev" title="detected — click to correct">${c.rev}</span>` +
      `<span class="meta">${counts}</span>`;
    row.title = c.path;

    /* The revision is a guess and has to be correctable in place: detection
     * cannot tell osrs184 from osrs239, and the wrong profile decodes at the
     * wrong field widths rather than failing. */
    row.querySelector('.crev').onclick = async (e) => {
      e.stopPropagation();
      const next = prompt(`Revision profile for ${c.label}`, c.rev);
      if (!next || next === c.rev) return;
      document.getElementById('cacheInfo').textContent = `re-reading as ${next}…`;
      const res = await fetch(
        `/api/caches/rev?index=${c.index}&rev=${encodeURIComponent(next)}`);
      await renderCaches(await res.json());
      if (c.index === data.active) await reloadForCache();
    };
    row.onclick = async () => {
      document.getElementById('cacheInfo').textContent = `switching to ${c.label}…`;
      const res = await fetch(`/api/caches/select?index=${c.index}`);
      const next = await res.json();
      await renderCaches(next);
      /* The npc list, the animations and any loaded subject all belong to the
       * cache that was open — reload rather than leave the previous cache's
       * ids labelled with this one's name. */
      await reloadForCache();
    };

    const drop = document.createElement('button');
    drop.className = 'x';
    drop.textContent = '×';
    drop.title = 'remove from the list (does not delete anything)';
    drop.onclick = async (e) => {
      e.stopPropagation();
      const res = await fetch(`/api/caches/remove?index=${c.index}`);
      renderCaches(await res.json());
    };
    /* Before the counts, not after: the row wraps to two lines and the drop
     * button belongs on the first one, beside what it drops. */
    row.insertBefore(drop, row.querySelector('.meta'));
    list.appendChild(row);
  });

  const cur = (data.caches || [])[data.active];
  if (active) active.textContent = cur ? `${cur.label} (${cur.rev})` : 'none';
  document.getElementById('cacheInfo').textContent =
    `${(data.caches || []).length} listed`;
  state.caches = data;
}

async function reloadForCache() {
  const res = await fetch('/api/npcs.json');
  state.npcs = await res.json();

  /*
   * The obj and loc lists are the new cache's too, and they are dropped rather
   * than refetched: each is tens of thousands of rows, and neither is needed
   * until its mode is entered. Emptying them is what makes setMode refetch —
   * keeping the old cache's rows would list ids that mean something else here.
   */
  state.objs = [];
  state.locs = [];
  state.objDetail = null;
  state.locDetail = null;

  renderNpcList();
  document.getElementById('npcCount').textContent = `${state.npcs.length} npcs`;

  /*
   * The subject too, not just the list. Its id means something else in this
   * cache — a different creature, or none — and its animations belonged to the
   * cache that was open. Leaving them up puts the previous cache's sequence ids
   * under this cache's name, which is a lie the panel has no way to caveat.
   *
   * The rig list comes back empty and `rigs_pending`, because the walk over the
   * new cache has only just started; watchRigs asks again when it lands.
   */
  if (state.mode === 'npc' && state.npcId !== null) {
    await selectNpc(state.npcId);
  } else if (state.mode === 'player') {
    state.detail = await (await fetch('/api/rig/0.json')).json();
    renderAnimList();
    await rebuildPlayer();
  } else if (state.mode === 'obj' || state.mode === 'loc') {
    /* setMode refetches the list this mode needs and re-selects the subject
     * against the new cache, which is exactly the reload wanted here. */
    await setMode(state.mode);
  }

  watchRigs();
}

/*
 * Follow the server's rig walk over the newly opened cache.
 *
 * The walk is why animations exist for a cache nobody built a catalog for, and
 * it lands in two pieces: the sequence sweep (a second or so) is enough to list
 * the animations for ONE npc, and the npc pass behind it (seconds more) is what
 * fills the match counts on every row of the npc list. So this refreshes the
 * two things separately, as each arrives, rather than making the panel wait for
 * the whole walk.
 *
 * One poller at a time: a second cache switch supersedes the first, and two
 * loops racing would refetch the npc list twice and could apply the older
 * answer last.
 */
let rigPollToken = 0;

async function watchRigs() {
  const token = ++rigPollToken;
  let sawSeqs = false;

  for (;;) {
    let status;
    try {
      status = await (await fetch('/api/rigs.json')).json();
    } catch (e) {
      return;
    }
    if (token !== rigPollToken) return;
    state.rigs = status;
    renderRigStatus(status);

    if (status.ready && !sawSeqs) {
      sawSeqs = true;
      /* The animation panel can be filled in now. Re-asking for the npc detail
       * is the whole refresh: the rig list is computed per request. */
      if (state.mode === 'npc' && state.npcId !== null) {
        const detail = await (await fetch(`/api/npc/${state.npcId}.json`)).json();
        if (token !== rigPollToken) return;
        state.detail = detail;
        renderAnimList();
      } else if (state.mode === 'player') {
        state.detail = await (await fetch('/api/rig/0.json')).json();
        if (token !== rigPollToken) return;
        renderAnimList();
      }
    }

    if (status.npcs || status.state === 'failed' || status.state === 'idle') {
      if (status.npcs) {
        /* The badges. Only now: before the npc pass they are all zero, and a
         * list that says 0/0 for every row reads as "this cache has none". */
        state.npcs = await (await fetch('/api/npcs.json')).json();
        if (token !== rigPollToken) return;
        renderNpcList();
      }
      return;
    }
    await new Promise((r) => setTimeout(r, 400));
  }
}

function renderRigStatus(s) {
  const el = document.getElementById('rigStatus');
  if (!el) return;
  if (s.state === 'failed') {
    el.textContent = 'animation search failed for this cache';
    return;
  }
  if (s.npcs) {
    el.textContent =
      `${s.seqs} sequences on ${s.distinct_rigs} rigs · searched in ${(s.ms / 1000).toFixed(1)}s`;
    return;
  }
  if (!s.ready) {
    el.textContent = 'searching for animations: reading sequences…';
    return;
  }
  const pct = s.total ? Math.round((s.done * 100) / s.total) : 0;
  el.textContent = `animations ready · matching npcs ${pct}%`;
}

function wireCaches() {
  const add = document.getElementById('cacheAdd');
  if (!add) return;

  add.onclick = async () => {
    const path = document.getElementById('cachePath').value.trim();
    const rev = document.getElementById('cacheRev').value.trim();
    if (!path) return;
    document.getElementById('cacheInfo').textContent = 'adding…';
    const res = await fetch(
      `/api/caches/add?path=${encodeURIComponent(path)}&rev=${encodeURIComponent(rev)}`);
    renderCaches(await res.json());
  };

  document.getElementById('cacheScan').onclick = async () => {
    document.getElementById('cacheInfo').textContent = 'scanning…';
    const res = await fetch('/api/caches/discover');
    renderCaches(await res.json());
  };

  fetch('/api/caches.json').then((r) => r.json()).then(renderCaches);
}

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

  /*
   * Free-fly: WASD over the ground plane, R/F on the world's up axis.
   *
   * Held keys, not one step per keydown: the OS auto-repeat rate is a user
   * setting and would make movement speed differ between machines. So the key
   * set is tracked and applied once per rendered frame, which also lets two
   * keys combine into a diagonal.
   *
   * The step scales with the zoom because the same distance means something
   * different at either end of the range — 40 units is a stride next to a
   * chicken and imperceptible next to a boss.
   */
  const held = new Set();
  const FLY_KEYS = new Set(['w', 'a', 's', 'd', 'r', 'f']);
  /* The arrows rotate the camera — the keyboard equivalent of a drag, so that
   * the view can be aimed without a mouse and without losing the subject.
   * Tracked in the same held set for the same reason the fly keys are: one
   * step per keydown would run at the OS auto-repeat rate. */
  const ROT_KEYS = new Set(['arrowleft', 'arrowright', 'arrowup', 'arrowdown']);

  window.addEventListener('keydown', (e) => {
    /* A search box has the keyboard; typing "was" into it must not fly, and
     * the arrows must still move the caret. */
    const t = e.target;
    if (t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.isContentEditable)) return;
    const k = e.key.toLowerCase();
    if (k === 'x') { if (state.recentre) state.recentre(); e.preventDefault(); return; }
    if (!FLY_KEYS.has(k) && !ROT_KEYS.has(k)) return;
    held.add(k);
    /* Also stops the arrows scrolling the page behind the canvas. */
    e.preventDefault();
  });
  window.addEventListener('keyup', (e) => held.delete(e.key.toLowerCase()));
  /* Losing focus mid-key never delivers the keyup, which would leave the view
   * drifting with nothing held. */
  window.addEventListener('blur', () => held.clear());

  /*
   * Speed is per SECOND, not per frame.
   *
   * A per-frame step makes the speed depend on the framerate — the same key
   * press travels twice as far on a 120 Hz display, and a stutter changes the
   * distance covered. It also has to be scaled to the zoom, because the same
   * number of units is a stride beside a chicken and imperceptible beside a
   * boss: 0.6 crosses the framing distance in a bit under two seconds, which
   * is enough to explore without overshooting the subject in one tap.
   */
  const FLY_PER_SECOND = 0.6;

  state.applyFly = (dt) => {
    if (!held.size || !state.wasm) return;
    /* Clamp the timestep: a backgrounded tab resumes with a huge dt, and one
     * frame of that teleports the viewpoint past everything. */
    const seconds = Math.min(dt, 100) / 1000;
    const step = Math.max(1, Math.round(state.zoom * FLY_PER_SECOND * seconds));
    const forward = (held.has('w') ? step : 0) - (held.has('s') ? step : 0);
    const right = (held.has('d') ? step : 0) - (held.has('a') ? step : 0);
    const up = (held.has('r') ? step : 0) - (held.has('f') ? step : 0);
    if (forward || right || up) state.wasm.move(forward, right, up);
  };

  /*
   * Arrow-key rotation, in the same per-second currency as the fly step so the
   * two feel like one control scheme and neither depends on the framerate.
   *
   * A full turn in ~1.8s (2048 units / 1150) is fast enough to swing round to
   * the far side of a model without being twitchy on a tap. Pitch moves at a
   * third of that: its useful range is the 1..511 clamp below rather than a
   * full revolution, so the same rate would cross the whole span in a blink.
   *
   * The signs and the clamps are the drag handler's, deliberately: left drag
   * and left arrow must turn the model the same way, or the two controls
   * disagree about which way is round.
   */
  const YAW_PER_SECOND = 1150;
  const PITCH_PER_SECOND = 380;

  state.applyRotate = (dt) => {
    if (!held.size) return;
    const seconds = Math.min(dt, 100) / 1000;
    const yawStep = Math.round(YAW_PER_SECOND * seconds);
    const pitchStep = Math.round(PITCH_PER_SECOND * seconds);
    const yaw = (held.has('arrowright') ? yawStep : 0) - (held.has('arrowleft') ? yawStep : 0);
    const pitch = (held.has('arrowdown') ? pitchStep : 0) - (held.has('arrowup') ? pitchStep : 0);
    if (yaw) state.yaw = (state.yaw - yaw) & 2047;
    if (pitch) state.pitch = Math.max(1, Math.min(511, state.pitch + pitch));
  };

  wireCaches();
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

  /*
   * Re-centre: undo the flying AND the pan, then re-derive the zoom from the
   * model's own height.
   *
   * All three, because any one of them alone can leave the subject off-screen —
   * flying far enough out and then only resetting the zoom still shows nothing,
   * which reads as "the button does not work". The zoom is re-derived rather
   * than remembered so it also fixes the case of a model swapped underneath a
   * zoom chosen for the previous one.
   */
  const recentre = () => {
    state.wasm.moveReset();
    /* Zeroed even though the page never sets it: a URL hash or a later control
     * could, and a re-centre that leaves a pan behind is the same off-screen
     * subject the button exists to rescue. */
    state.wasm.setPan(0, 0);
    const h = state.wasm.modelHeight();
    if (h > 0) state.zoom = Math.max(260, Math.min(6000, Math.round(h * 1.6)));
  };
  const recentreBtn = document.getElementById('recenterBtn');
  if (recentreBtn) recentreBtn.onclick = recentre;
  state.recentre = recentre;
  document.getElementById('frameSlider').oninput = (e) => {
    state.playing = false;
    document.getElementById('playBtn').textContent = 'Play';
    state.cycle = Number(e.target.value);
    const f = frameAtCycle(state.bodyDelays, state.cycle);
    if (f >= 0) state.frame = f;
    updateFrameUi();
  };

  document.getElementById('modeNpc').onclick = () => setMode('npc');
  document.getElementById('modeObj').onclick = () => setMode('obj');
  document.getElementById('modeLoc').onclick = () => setMode('loc');
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
 * Recognised: `player`, `wear=` (comma separated obj ids), `npc=`, `obj=`,
 * `variant=`, `loc=`, `shape=`, `seq=`, `fx=` (spotanim id), `delay=`,
 * `height=`, `orient=`, `pitch=`, `yaw=`, `zoom=`, `paused`, `cycle=`.
 *
 * `obj=` and `loc=` carry a part with them — `variant=` and `shape=` — because
 * neither id names a single mesh on its own.
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
  if (p.obj !== undefined) {
    await setMode('obj');
    await selectObj(Number(p.obj), p.variant !== undefined ? Number(p.variant) : 0);
  }
  if (p.loc !== undefined) {
    await setMode('loc');
    await selectLoc(Number(p.loc), p.shape !== undefined ? Number(p.shape) : null);
  }
  /* After the subject: selecting a loc plays the sequence its record names, and
   * an explicit `seq=` is the one the link meant. */
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
  /* The boot cache's rig walk is running too — it is the same walk a switch
   * starts — so follow it from here rather than only after a switch. */
  watchRigs();
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
