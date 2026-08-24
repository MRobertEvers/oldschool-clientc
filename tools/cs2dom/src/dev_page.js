/*
 * The dev page.
 *
 * One file, no build step, no framework — it is a tool for looking at a tree of
 * boxes, and a tool that needed its own toolchain would be one more thing between
 * a save and seeing the result.
 *
 * Three panes: the interface as the client's layout rules place it, the state it
 * reads as controls, and the cache records it compiles to. The third pane is the
 * point as much as the first — the .if and the .cs2 are what actually ships, so
 * they are never more than a glance away.
 */

export function page() {
    return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>cs2dom</title>
<style>
  :root {
    --bg: #14120e; --panel: #1c1915; --line: #2e2a22; --ink: #e8e1d2; --dim: #9b9280;
    --accent: #e2a93f; --hot: #4cbba0; --bad: #e0674a;
    --mono: "SF Mono", ui-monospace, Menlo, monospace;
  }
  * { box-sizing: border-box; }
  body {
    margin: 0; background: var(--bg); color: var(--ink);
    font: 13px/1.5 ui-sans-serif, system-ui, sans-serif;
    height: 100vh; display: grid; grid-template-rows: auto 1fr;
  }
  header {
    display: flex; align-items: center; gap: 16px; padding: 8px 14px;
    border-bottom: 1px solid var(--line); background: var(--panel);
  }
  header h1 { font: 600 13px var(--mono); margin: 0; letter-spacing: .08em; text-transform: uppercase; color: var(--accent); }
  header .status { font: 11px var(--mono); color: var(--dim); }
  header .status.bad { color: var(--bad); }
  header .status.fresh { color: var(--hot); }
  header .spacer { flex: 1; }
  button, input {
    font: 12px var(--mono); color: var(--ink); background: #241f19;
    border: 1px solid var(--line); border-radius: 3px; padding: 4px 8px;
  }
  button { cursor: pointer; }
  button:hover { border-color: var(--accent); }
  .picker { position: relative; width: min(460px, 44vw); }
  #pick { width: 100%; padding: 6px 9px; outline: none; }
  #pick:focus { border-color: var(--accent); box-shadow: 0 0 0 2px rgba(226,169,63,.12); }
  #pick::-webkit-search-cancel-button { cursor: pointer; }
  .pickmenu {
    display: none; position: absolute; z-index: 100; top: calc(100% + 6px); left: 0; right: 0;
    max-height: min(520px, 72vh); overflow-y: auto; padding: 5px;
    border: 1px solid #413a2e; border-radius: 5px; background: #181510;
    box-shadow: 0 14px 36px rgba(0,0,0,.5);
  }
  .pickmenu.open { display: block; }
  .pickgroup {
    padding: 7px 8px 4px; color: var(--dim); font: 600 9px var(--mono);
    letter-spacing: .11em; text-transform: uppercase;
  }
  .pickrow {
    display: grid; grid-template-columns: auto minmax(0, 1fr) auto; align-items: center; gap: 8px;
    width: 100%; padding: 7px 8px; border: 0; border-radius: 3px; background: transparent;
    color: var(--ink); text-align: left; cursor: pointer;
  }
  .pickrow:hover, .pickrow.active { background: #2a241b; }
  .pickrow.active { box-shadow: inset 2px 0 var(--accent); }
  .pickname { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .pickid { color: #766e5f; font: 10px var(--mono); }
  .picksource {
    min-width: 48px; padding: 1px 5px; border: 1px solid #4c4436; border-radius: 8px;
    color: var(--dim); font: 600 8px var(--mono); text-align: center; text-transform: uppercase;
  }
  .picksource.authored { border-color: #82652d; color: var(--accent); }
  .picksource.dat2 { border-color: #276f62; color: var(--hot); }
  .picksource.content { border-color: #5a536f; color: #aaa0cf; }
  .pickempty, .pickmore { padding: 10px 8px; color: var(--dim); font: 10px var(--mono); }
  .pickmore { border-top: 1px solid var(--line); margin-top: 4px; }

  main { display: grid; grid-template-columns: 1fr 260px 420px; overflow: hidden; }
  section { overflow: auto; padding: 14px; }
  section + section { border-left: 1px solid var(--line); }
  h2 {
    font: 600 10px var(--mono); letter-spacing: .12em; text-transform: uppercase;
    color: var(--dim); margin: 0 0 10px;
  }

  /* the stage: a checkerboard so a transparent component still reads as one */
  #stage {
    position: relative; margin: 0 auto;
    background-color: #0c0b09;
    background-image: linear-gradient(45deg, #17150f 25%, transparent 25%),
                      linear-gradient(-45deg, #17150f 25%, transparent 25%),
                      linear-gradient(45deg, transparent 75%, #17150f 75%),
                      linear-gradient(-45deg, transparent 75%, #17150f 75%);
    background-size: 16px 16px;
    background-position: 0 0, 0 8px, 8px -8px, -8px 0;
    outline: 1px solid var(--line); overflow: hidden; cursor: default;
    touch-action: none; user-select: none;
  }
  #stage:focus-visible { box-shadow: 0 0 0 2px var(--accent); }
  .box { position: absolute; overflow: hidden; }
  .box.text { display: flex; white-space: pre; }
  .box.unknown {
    border: 1px dashed #4a4335; color: var(--dim);
    font: 10px var(--mono); display: flex; align-items: center; justify-content: center;
  }
  /* A native model widget is not clipped to its own 31x32 (or similarly small)
     component rectangle. It paints across the enclosing interface clip. The
     model plane therefore owns that clip-sized area; the marker retains the
     authored widget rectangle for loading errors, outlines and tree hover. */
  .box.model { pointer-events: none; }
  .box.model canvas { position: absolute; inset: 0; width: 100%; height: 100%; image-rendering: pixelated; }
  .box.model .model-marker {
    position: absolute; z-index: 1; border: 1px dashed #4a4335; color: var(--dim);
    font: 10px var(--mono); display: flex; align-items: center; justify-content: center;
    overflow: hidden; text-align: center;
  }
  .box.model.ready .model-marker { border: 0; color: transparent; }
  .box.model.outline { outline: 0; }
  .box.model.outline .model-marker { outline: 1px solid var(--accent); }
  .box.text canvas { width: 100%; height: 100%; image-rendering: pixelated; }
  .box img { width: 100%; height: 100%; image-rendering: pixelated; }
  .box.graphic canvas { width: 100%; height: 100%; image-rendering: pixelated; }
  .box.line > svg { position: absolute; max-width: none; max-height: none; pointer-events: none; }
  .box.outline { outline: 1px solid var(--accent); outline-offset: 0; }
  #stage.wire .box { outline: 1px solid rgba(226,169,63,.35); }
  #stage.wire .box.model { outline: 0; }
  #stage.wire .box.model .model-marker { outline: 1px solid rgba(226,169,63,.35); }
  .opmenu {
    position: absolute; z-index: 20; min-width: 150px; padding: 4px;
    border: 1px solid #5b503d; border-radius: 4px; background: #181510;
    box-shadow: 0 8px 24px rgba(0,0,0,.55);
  }
  .opmenu button { display: block; width: 100%; border: 0; text-align: left; background: transparent; }
  .opmenu button:hover { background: #2a241b; }

  .rowlabel { font: 11px var(--mono); color: var(--dim); }
  .tree { font: 11px var(--mono); }
  .tree div { padding: 1px 0; cursor: default; white-space: nowrap; }
  .tree div:hover { color: var(--accent); }
  .tree .bound { color: var(--hot); }

  .control { margin-bottom: 12px; }
  .control label { display: flex; justify-content: space-between; font: 11px var(--mono); color: var(--dim); }
  .control label b { color: var(--ink); font-weight: 500; }
  .control input[type=range] { width: 100%; margin-top: 4px; padding: 0; }
  .control .who { font: 10px var(--mono); color: #6f6857; margin-top: 3px; }
  .control input[type=number] { width: 72px; padding: 2px 4px; }
  .stateactions {
    display: flex; align-items: center; gap: 7px; padding-bottom: 10px;
    margin-bottom: 12px; border-bottom: 1px solid var(--line);
  }
  .stateactions button:disabled { cursor: default; opacity: .45; }
  .stateactions .dirty { color: var(--accent); }
  .invrow { display: flex; align-items: center; gap: 6px; margin: 3px 0; font: 11px var(--mono); }
  .invrow span { flex: 1; color: var(--dim); }
  .unmodelled {
    border: 1px solid #4a4335; border-left: 2px solid var(--accent);
    padding: 8px; margin-bottom: 14px; font: 10px/1.5 var(--mono); color: var(--dim);
  }
  .unmodelled b { display: block; color: var(--accent); margin-bottom: 4px; }

  pre {
    background: #100e0b; border: 1px solid var(--line); border-radius: 3px;
    padding: 10px; overflow-x: auto; font: 11px/1.45 var(--mono); margin: 0 0 12px;
  }
  pre .k { color: var(--accent); }
  .filename { font: 10px var(--mono); color: var(--dim); margin: 0 0 4px; letter-spacing: .04em; }
  .error {
    background: #2a1712; border: 1px solid var(--bad); color: #ffcdbd;
    padding: 10px; border-radius: 3px; font: 11px/1.5 var(--mono); white-space: pre-wrap;
  }
  .warn { color: #d9a441; font: 11px var(--mono); margin-bottom: 8px; }
</style>
</head>
<body>
<header>
  <h1>cs2dom</h1>
  <div class="picker" id="picker">
    <input id="pick" type="search" autocomplete="off" spellcheck="false"
      placeholder="Search interfaces…" aria-label="Interface" aria-expanded="false"
      aria-controls="pickmenu" aria-autocomplete="list">
    <div id="pickmenu" class="pickmenu" role="listbox" aria-label="Interfaces"></div>
  </div>
  <button id="add">new component</button>
  <label class="rowlabel"><input type="checkbox" id="wire"> outlines</label>
  <span class="spacer"></span>
  <span class="status" id="status">connecting…</span>
</header>

<main>
  <section>
    <h2>Preview <span class="rowlabel" id="dims"></span></h2>
    <div id="stage"></div>
    <h2 style="margin-top:16px">Tree</h2>
    <div class="tree" id="tree"></div>
  </section>

  <section>
    <h2>Host state</h2>
    <div class="stateactions" id="stateactions" hidden>
      <button type="button" id="save-state" disabled>Save state</button>
      <button type="button" id="revert-state" disabled>Revert</button>
      <span class="rowlabel" id="state-note" aria-live="polite">No pending changes</span>
    </div>
    <div id="controls"></div>
  </section>

  <section>
    <h2>Cache records</h2>
    <div id="records"></div>
  </section>
</main>

<script src="/toridraw/ev_wasm.js"></script>
<script type="module">
import { createHostRuntime } from '/runtime/host_runtime.js';
import { createWasmCS2Runtime } from '/runtime/wasm_runtime.js';
import { paintCacheText } from '/runtime/font_runtime.js';

const state = {};            // Values committed to the live React-side host.
const draftState = {};       // Host-state edits waiting for an explicit save.
let data = null;
let catalog = [];
let chosen = null;
let pickerMatches = [];
let pickerActive = -1;
let refreshEpoch = 0;
let renderedControlsKey = null;
let stateDirty = false;
let hostRuntime = null;
let wasmRuntime = null;
let runtimeWarnings = [];
let runtimeMode = 'unavailable';
let runtimeCycle = 0;
const heldButtons = new Set();
const hostDataCache = new Map();

const $ = (id) => document.getElementById(id);

async function refresh({ hotReload = false } = {}) {
  const epoch = ++refreshEpoch;
  if( catalog.length === 0 ) {
    const listing = await fetch('/catalog').then((response) => response.json());
    if( epoch !== refreshEpoch ) return;
    catalog = listing.interfaces || [];
    if( !chosen ) chosen = catalog[0] && catalog[0].key;
    populatePicker();
  }
  const query = new URLSearchParams({
    state: JSON.stringify(state),
    interface: chosen || '',
  });
  const response = await fetch('/state?' + query);
  const next = await response.json();
  if( epoch !== refreshEpoch ) return;
  const iface = next.interfaces && next.interfaces[0];
  let session = null;
  if( !next.error && iface ) {
    $('status').textContent = 'loading C CS2VM…';
    $('status').className = 'status';
    session = await createRuntimeSession(iface);
    if( epoch !== refreshEpoch ) {
      disposeRuntimeSession(session, false);
      return;
    }
  }
  data = next;
  if( session ) installRuntimeSession(session);
  render({ hotReload });
}

async function createRuntimeSession(iface) {
  const session = {
    host: null,
    wasm: null,
    warnings: [],
    mode: 'unavailable',
  };
  try {
    const hostData = await loadHostData(iface.runtime);
    session.host = createHostRuntime(iface.runtime.ir, {
      state,
      viewport: iface.viewport,
      hostData,
      invoke: (intent) => invokeSessionIntent(session, intent),
    });
    const bytecode = iface.runtime.bytecode;
    if( bytecode?.available ) {
      session.wasm = await createWasmCS2Runtime({
        program: bytecode,
        host: session.host,
      });
      session.mode = 'wasm';
    } else {
      for( const warning of bytecode?.warnings || [] )
        pushRuntimeWarning(session.warnings, warning);
      pushRuntimeWarning(session.warnings,
        'Original CS2 bytecode is unavailable; scripts are not executed.');
      session.mode = 'static';
    }
    session.host.mount();
    const snapshot = session.host.snapshot();
    iface.viewport = snapshot.viewport;
    iface.boxes = snapshot.boxes;
    iface.runtimeVersion = snapshot.version;
  } catch( error ) {
    session.wasm?.destroy?.();
    session.wasm = null;
    session.host = null;
    session.mode = 'unavailable';
    pushRuntimeWarning(session.warnings, 'script runtime stopped: ' + error.message);
  }
  return session;
}

function installRuntimeSession(session) {
  resetRuntimeInteraction();
  hostRuntime = session.host;
  wasmRuntime = session.wasm;
  runtimeWarnings = session.warnings;
  runtimeMode = session.mode;
}

function invokeSessionIntent(session, intent) {
  if( !session.wasm ) return 0;
  try {
    return session.wasm.invokeIntent(intent);
  } catch( error ) {
    pushRuntimeWarning(session.warnings,
      'C CS2VM/WASM: ' + (error?.message || String(error)));
    return 0;
  }
}

async function loadHostData(runtime) {
  /* Direct data remains useful to embedders and focused tests. The dev server
     supplies a URL so the full cache table is fetched only once per source. */
  if( runtime?.hostData ) return runtime.hostData;
  const url = runtime?.hostDataUrl;
  if( !url ) return null;
  let pending = hostDataCache.get(url);
  if( !pending ) {
    pending = fetch(url).then((response) => {
      if( !response.ok ) throw new Error('HOST data request failed (' + response.status + ')');
      return response.json();
    }).catch((error) => {
      hostDataCache.delete(url);
      throw error;
    });
    hostDataCache.set(url, pending);
  }
  return pending;
}

function pushRuntimeWarning(warnings, message) {
  if( message && !warnings.includes(message) ) warnings.push(message);
}

function applyRuntimeSnapshot(iface = currentInterface()) {
  if( !iface || !hostRuntime ) return;
  const snapshot = hostRuntime.snapshot();
  iface.viewport = snapshot.viewport;
  iface.boxes = snapshot.boxes;
  iface.runtimeVersion = snapshot.version;
}

function populatePicker() {
  const pick = $('pick');
  pick.placeholder = 'Search ' + catalog.length.toLocaleString() + ' interfaces…';
  syncPickerLabel();
  renderPicker('');
}

function sourceTitle(source) {
  return source === 'authored' ? 'Authored TSX'
    : source === 'dat2' ? 'Dat2 cache' : 'OSRS-Content';
}

function syncPickerLabel() {
  const entry = catalog.find((item) => item.key === chosen);
  $('pick').value = entry ? entry.name : '';
}

function matchingEntries(query) {
  const terms = query.trim().toLowerCase().split(/\s+/).filter(Boolean);
  const ranked = catalog.map((entry) => {
    const name = entry.name.toLowerCase();
    const source = entry.source.toLowerCase();
    const title = sourceTitle(entry.source).toLowerCase();
    const id = String(entry.interfaceId);
    const haystack = name + ' ' + source + ' ' + title + ' ' + id;
    if( !terms.every((term) => haystack.includes(term)) ) return null;
    let score = 0;
    for( const term of terms ) {
      score += name === term ? 0 : name.startsWith(term) ? 1 : name.includes(term) ? 3
        : id === term ? 2 : source.startsWith(term) || title.startsWith(term) ? 4 : 8;
    }
    return { entry, score };
  }).filter(Boolean);
  ranked.sort((a, b) => a.score - b.score ||
    a.entry.name.localeCompare(b.entry.name, undefined, { numeric: true }) ||
    a.entry.interfaceId - b.entry.interfaceId);
  return ranked.map((item) => item.entry);
}

function renderPicker(query) {
  const menu = $('pickmenu');
  menu.innerHTML = '';
  pickerActive = -1;

  const matches = matchingEntries(query);
  const visible = query.trim()
    ? matches.slice(0, 120)
    : ['authored', 'dat2', 'content'].flatMap((source) =>
        matches.filter((entry) => entry.source === source).slice(0, 20));
  pickerMatches = [];

  if( visible.length === 0 ) {
    const empty = document.createElement('div');
    empty.className = 'pickempty';
    empty.textContent = 'No interfaces match “' + query.trim() + '”.';
    menu.appendChild(empty);
    return;
  }

  for( const source of ['authored', 'dat2', 'content'] ) {
    const entries = visible.filter((entry) => entry.source === source);
    if( entries.length === 0 ) continue;
    const group = document.createElement('div');
    group.className = 'pickgroup';
    group.textContent = sourceTitle(source);
    menu.appendChild(group);

    for( const entry of entries ) {
      const index = pickerMatches.length;
      pickerMatches.push(entry);
      const row = document.createElement('button');
      row.type = 'button';
      row.id = 'pickoption-' + index;
      row.className = 'pickrow';
      row.setAttribute('role', 'option');
      row.setAttribute('aria-selected', entry.key === chosen ? 'true' : 'false');

      const sourceBadge = document.createElement('span');
      sourceBadge.className = 'picksource ' + source;
      sourceBadge.textContent = source === 'content' ? 'files' : source;
      const name = document.createElement('span');
      name.className = 'pickname';
      name.textContent = entry.name;
      const id = document.createElement('span');
      id.className = 'pickid';
      id.textContent = '#' + entry.interfaceId;
      row.append(sourceBadge, name, id);
      row.onpointerdown = (event) => event.preventDefault();
      row.onclick = () => chooseInterface(entry);
      menu.appendChild(row);
    }
  }

  if( matches.length > visible.length ) {
    const more = document.createElement('div');
    more.className = 'pickmore';
    more.textContent = 'Showing ' + visible.length.toLocaleString() + ' of ' +
      matches.length.toLocaleString() + ' matches — keep typing to narrow the list.';
    menu.appendChild(more);
  }
}

function openPicker() {
  $('pickmenu').classList.add('open');
  $('pick').setAttribute('aria-expanded', 'true');
}

function closePicker() {
  $('pickmenu').classList.remove('open');
  $('pick').setAttribute('aria-expanded', 'false');
  $('pick').removeAttribute('aria-activedescendant');
  pickerActive = -1;
  syncPickerLabel();
}

function movePickerActive(delta) {
  if( pickerMatches.length === 0 ) return;
  pickerActive = pickerActive < 0
    ? (delta > 0 ? 0 : pickerMatches.length - 1)
    : (pickerActive + delta + pickerMatches.length) % pickerMatches.length;
  document.querySelectorAll('.pickrow').forEach((row, index) => {
    row.classList.toggle('active', index === pickerActive);
  });
  const row = $('pickoption-' + pickerActive);
  $('pick').setAttribute('aria-activedescendant', row.id);
  row.scrollIntoView({ block: 'nearest' });
}

function chooseInterface(entry) {
  chosen = entry.key;
  for( const key of Object.keys(state) ) delete state[key];
  for( const key of Object.keys(draftState) ) delete draftState[key];
  renderedControlsKey = null;
  setStateDirty(false);
  resetRuntimeInteraction();
  closePicker();
  refresh();
}

function render({ hotReload = false } = {}) {
  const records = $('records');
  if( data.error ) {
    $('status').textContent = 'error';
    $('status').className = 'status bad';
    records.innerHTML = '<div class="error"></div>';
    records.firstChild.textContent = data.error;
    if( !hotReload ) {
      $('stage').innerHTML = '';
      $('tree').innerHTML = '';
      $('controls').innerHTML = '';
      $('stateactions').hidden = true;
      renderedControlsKey = null;
    }
    return;
  }

  $('status').textContent = 'built ' + new Date().toLocaleTimeString() +
    (runtimeMode === 'static' ? ' — static (no CS2 bytecode)' :
      runtimeMode === 'unavailable' ? ' — runtime unavailable' : ' — C CS2VM/WASM');
  $('status').className = runtimeMode === 'unavailable' ? 'status bad' : 'status fresh';
  setTimeout(() => { $('status').className = 'status'; }, 900);

  if( !hotReload && !$('pickmenu').classList.contains('open') ) syncPickerLabel();
  const iface = data.interfaces[0];
  if( !iface ) return;

  drawStage(iface);
  drawTree(iface);
  if( !hotReload ) drawControls(iface);
  drawRecords(iface);
}

function drawStage(iface) {
  const stage = $('stage');
  const root = iface.boxes[0];
  const width = iface.viewport?.width || (root ? Math.max(root.w, 32) : 256);
  const height = iface.viewport?.height || (root ? Math.max(root.h, 32) : 128);
  stage.style.width = width + 'px';
  stage.style.height = height + 'px';
  stage.className = $('wire').checked ? 'wire' : '';
  stage.tabIndex = 0;
  stage.setAttribute('aria-label', 'Interactive React interface preview');
  $('dims').textContent = width + '×' + height + ' — interface ' + iface.interfaceId +
    ' — live React tree';

  stage.innerHTML = '';
  const epoch = ++modelEpoch;

  for( const box of iface.boxes ) {
    const role = roleOf(box.type);
    const modelSurface = role === 'model' ? modelRenderSurface(box, width, height) : null;
    const element = document.createElement('div');
    element.className = 'box ' + role;
    element.style.left = (modelSurface?.left ?? box.x) + 'px';
    element.style.top = (modelSurface?.top ?? box.y) + 'px';
    element.style.width = (modelSurface?.width ?? box.w) + 'px';
    element.style.height = (modelSurface?.height ?? box.h) + 'px';
    if( box.emitted === false || box.effectiveHidden || box.culled )
      element.style.display = 'none';
    if( role !== 'model' && role !== 'line' && box.clip && box.w > 0 && box.h > 0 ) {
      const top = Math.max(0, box.clip.top - box.y);
      const right = Math.max(0, box.x + box.w - box.clip.right);
      const bottom = Math.max(0, box.y + box.h - box.clip.bottom);
      const left = Math.max(0, box.clip.left - box.x);
      if( top || right || bottom || left )
        element.style.clipPath = 'inset(' + top + 'px ' + right + 'px ' +
          bottom + 'px ' + left + 'px)';
    }
    if( box.props.transparency )
      element.style.opacity = String(1 - box.props.transparency / 255);
    element.setAttribute('aria-label', box.name + ', ' + box.kind + ', file ' + box.fileId);
    element.dataset.name = box.name;

    paint(element, box, iface, modelSurface);
    stage.appendChild(element);
    if( role === 'text' )
      paintCacheText(element, box, iface, () => epoch === modelEpoch).catch(() => false);
    if( role === 'model' ) paintModel(element, box, iface, epoch, modelSurface);
  }
}

function modelRenderSurface(box, stageWidth, stageHeight) {
  const clip = box.clip || { left: 0, top: 0, right: stageWidth, bottom: stageHeight };
  const left = Math.max(0, Math.min(stageWidth, Math.floor(clip.left ?? 0)));
  const top = Math.max(0, Math.min(stageHeight, Math.floor(clip.top ?? 0)));
  const right = Math.max(left, Math.min(stageWidth, Math.ceil(clip.right ?? stageWidth)));
  const bottom = Math.max(top, Math.min(stageHeight, Math.ceil(clip.bottom ?? stageHeight)));
  return {
    left, top, width: right - left, height: bottom - top,
    widgetX: (box.x | 0) - left, widgetY: (box.y | 0) - top,
    widgetWidth: box.w | 0, widgetHeight: box.h | 0,
  };
}

function roleOf(type) {
  return ({ 0: 'layer', 2: 'inv', 3: 'rect', 4: 'text', 5: 'graphic', 6: 'model', 8: 'text', 9: 'line' })[type] || 'unknown';
}

function paint(element, box, iface, modelSurface = null) {
  const props = box.props;
  switch( roleOf(box.type) ) {
    case 'rect':
      if( props.fill ) element.style.background = colour(props.color);
      else element.style.border = '1px solid ' + colour(props.color);
      break;
    case 'text': {
      element.textContent = String(props.text ?? '');
      element.style.color = colour(props.color);
      element.style.font = '12px ui-monospace, monospace';
      element.style.justifyContent = ['flex-start', 'center', 'flex-end'][props.halign | 0] || 'flex-start';
      element.style.alignItems = ['flex-start', 'center', 'flex-end'][props.valign | 0] || 'flex-start';
      if( props.shadow ) element.style.textShadow = '1px 1px 0 #000';
      break;
    }
    case 'graphic': {
      if( props.sprite >= 0 ) {
        const url = '/sprite/' + iface.spriteSource + '/' + props.sprite + '.png' +
          (props.tiled ? '?tile=1' : '');
        const image = new Image();
        image.onerror = () => {
          element.classList.add('unknown');
          element.textContent = 'sprite ' + props.sprite;
        };
        if( props.tiled ) {
          const canvas = document.createElement('canvas');
          canvas.width = Math.max(1, box.w | 0);
          canvas.height = Math.max(1, box.h | 0);
          image.onload = () => {
            const context = canvas.getContext('2d');
            const pattern = context?.createPattern(image, 'repeat');
            if( pattern ) {
              context.fillStyle = pattern;
              context.fillRect(0, 0, canvas.width, canvas.height);
            }
          };
          element.appendChild(canvas);
        } else {
          image.alt = '';
          element.appendChild(image);
        }
        image.src = url;
      }
      break;
    }
    case 'line': {
      const stroke = Math.max(1, props.lineWidth | 0);
      const pad = Math.ceil(stroke / 2);
      const width = Math.max(1, (box.w | 0) + pad * 2);
      const height = Math.max(1, (box.h | 0) + pad * 2);
      const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
      const line = document.createElementNS('http://www.w3.org/2000/svg', 'line');
      svg.setAttribute('width', String(width));
      svg.setAttribute('height', String(height));
      svg.setAttribute('viewBox', '0 0 ' + width + ' ' + height);
      svg.style.left = -pad + 'px';
      svg.style.top = -pad + 'px';
      if( box.clip ) {
        const surfaceLeft = box.x - pad;
        const surfaceTop = box.y - pad;
        const top = Math.max(0, box.clip.top - surfaceTop);
        const right = Math.max(0, surfaceLeft + width - box.clip.right);
        const bottom = Math.max(0, surfaceTop + height - box.clip.bottom);
        const left = Math.max(0, box.clip.left - surfaceLeft);
        if( top || right || bottom || left )
          svg.style.clipPath = 'inset(' + top + 'px ' + right + 'px ' +
            bottom + 'px ' + left + 'px)';
      }
      line.setAttribute('x1', String(pad));
      line.setAttribute('y1', String(pad + (props.lineDirection ? box.h : 0)));
      line.setAttribute('x2', String(pad + box.w));
      line.setAttribute('y2', String(pad + (props.lineDirection ? 0 : box.h)));
      line.setAttribute('stroke', colour(props.color));
      line.setAttribute('stroke-width', String(stroke));
      line.setAttribute('stroke-linecap', 'square');
      line.setAttribute('shape-rendering', 'crispEdges');
      svg.appendChild(line);
      element.style.overflow = 'visible';
      element.appendChild(svg);
      break;
    }
    case 'model':
      {
        const marker = document.createElement('div');
        marker.className = 'model-marker';
        marker.style.left = (modelSurface?.widgetX ?? 0) + 'px';
        marker.style.top = (modelSurface?.widgetY ?? 0) + 'px';
        marker.style.width = Math.max(0, modelSurface?.widgetWidth ?? box.w) + 'px';
        marker.style.height = Math.max(0, modelSurface?.widgetHeight ?? box.h) + 'px';
        marker.textContent = props.model >= 0 ? 'loading model ' + props.model : 'loading player';
        element.appendChild(marker);
      }
      break;
    case 'layer':
      break;
    default:
      element.textContent = box.kind;
  }
}

/* ---- toridraw model components ---------------------------------------- */

let modelEpoch = 0;
let modelWasm = null;
let modelQueue = Promise.resolve();
const modelBlobs = new Map();
const animationBlobs = new Map();

async function wasmRenderer() {
  if( modelWasm ) return modelWasm;
  if( typeof EVModule !== 'function' ) throw new Error('toridraw module did not load');
  const mod = await EVModule({ locateFile: () => '/toridraw/ev_wasm.wasm' });
  const wrap = (name, result, args) => mod.cwrap(name, result, args);
  modelWasm = {
    mod,
    init: wrap('ev_w_init', null, []),
    alloc: wrap('ev_w_alloc', 'number', ['number']),
    release: wrap('ev_w_release', null, ['number']),
    setModel: wrap('ev_w_set_model', 'number', ['number', 'number']),
    setModelHd: wrap('ev_w_set_model_hd', 'number', ['number', 'number']),
    clearModelHd: wrap('ev_w_clear_model_hd', null, []),
    setTextures: wrap('ev_w_set_textures', 'number', ['number', 'number']),
    setAnim: wrap('ev_w_set_anim', 'number', ['number', 'number']),
    clearAnim: wrap('ev_w_clear_anim', null, []),
    frameCount: wrap('ev_w_frame_count', 'number', []),
    frameDelay: wrap('ev_w_frame_delay', 'number', ['number']),
    renderWidget: wrap('ev_w_render_widget', 'number', Array(16).fill('number')),
  };
  modelWasm.init();
  return modelWasm;
}

function pushWasm(wasm, bytes, fn) {
  const ptr = wasm.alloc(bytes.length || 1);
  if( !ptr ) return 0;
  if( bytes.length ) wasm.mod.HEAPU8.set(bytes, ptr);
  const result = fn(ptr, bytes.length);
  wasm.release(ptr);
  return result;
}

async function fetchModel(url) {
  if( modelBlobs.has(url) ) return modelBlobs.get(url);
  const pending = (async () => {
    const response = await fetch(url);
    if( !response.ok ) throw new Error((await response.text()) || 'model not found');
    const bytes = new Uint8Array(await response.arrayBuffer());
    const textureIds = response.headers.get('X-Texture-Ids');
    let textures = new Uint8Array(0);
    if( textureIds ) {
      const textureResponse = await fetch('/model/textures.bin?ids=' + encodeURIComponent(textureIds));
      if( textureResponse.ok ) textures = new Uint8Array(await textureResponse.arrayBuffer());
    }
    return { bytes, textures };
  })();
  modelBlobs.set(url, pending);
  return pending;
}

async function fetchAnimation(id) {
  if( animationBlobs.has(id) ) return animationBlobs.get(id);
  const pending = fetch('/model/seq/' + id + '.anim').then(async (response) => {
    if( response.status === 404 ) return null;
    if( !response.ok ) throw new Error((await response.text()) || 'animation not found');
    return new Uint8Array(await response.arrayBuffer());
  }).catch((error) => {
    animationBlobs.delete(id);
    throw error;
  });
  animationBlobs.set(id, pending);
  return pending;
}

function currentAnimationFrame(wasm, startedAt) {
  const count = Math.max(0, wasm.frameCount() | 0);
  if( count === 0 ) return { frame: -1, wait: 0 };
  const delays = Array.from({ length: count }, (_, index) =>
    Math.max(1, wasm.frameDelay(index) | 0));
  const cycle = delays.reduce((sum, delay) => sum + delay, 0);
  let tick = Math.floor(Math.max(0, performance.now() - startedAt) / 20) % cycle;
  for( let frame = 0; frame < count; frame++ ) {
    if( tick < delays[frame] )
      return { frame, wait: Math.max(1, (delays[frame] - tick) * 20) };
    tick -= delays[frame];
  }
  return { frame: 0, wait: 20 };
}

function paintModel(element, box, iface, epoch, surface) {
  if( box.w <= 0 || box.h <= 0 || !surface || surface.width <= 0 || surface.height <= 0 ) return;
  const marker = element.querySelector('.model-marker');
  const source = box.presentation?.source || {
    kind: box.props.clientCode === 328 && box.props.model < 0 ? 'playerSelf' : 'model',
    id: box.props.model,
  };
  const player = ['playerHead', 'playerSelf', 'playerChatHead'].includes(source.kind);
  if( source.kind === 'none' || (!player && !(source.id >= 0)) ) {
    if( marker ) marker.textContent = 'model source unavailable';
    return;
  }
  const route = source.kind === 'object' ? 'obj/' + source.id + '.model'
    : source.kind === 'npcHead' || source.kind === 'npcModel' ? 'npc/' + source.id + '.model'
    : source.kind === 'locModel' ? 'loc/' + source.id + '.model'
    : player ? 'player.model' : source.id + '.model';
  const url = '/model/' + iface.modelSource + '/' + route;
  const sequence = box.presentation?.sequence ?? box.props.seq ?? -1;
  const startedAt = performance.now();
  const modelPending = fetchModel(url);
  const animationPending = sequence >= 0
    ? fetchAnimation(sequence).catch(() => null) : Promise.resolve(null);

  const renderFrame = () => {
    modelQueue = modelQueue.then(async () => {
      const [{ bytes, textures }, animation, wasm] = await Promise.all([
        modelPending, animationPending, wasmRenderer(),
      ]);
      if( epoch !== modelEpoch || !element.isConnected ) return;

      const magic = String.fromCharCode(...bytes.subarray(0, 4));
      const faces = pushWasm(wasm, bytes, (ptr, size) =>
        magic === 'EVH1' ? wasm.setModelHd(ptr, size) : wasm.setModel(ptr, size));
      if( magic !== 'EVH1' ) wasm.clearModelHd();
      pushWasm(wasm, textures, (ptr, size) => wasm.setTextures(ptr, size));
      wasm.clearAnim();
      const animated = animation &&
        pushWasm(wasm, animation, (ptr, size) => wasm.setAnim(ptr, size)) > 0;
      if( !faces ) throw new Error('model decode failed');

      const timing = animated ? currentAnimationFrame(wasm, startedAt) : { frame: -1, wait: 0 };
      const width = Math.max(1, Math.min(1024, surface.width | 0));
      const height = Math.max(1, Math.min(1024, surface.height | 0));
      const ptr = wasm.renderWidget(
        width, height,
        surface.widgetX | 0, surface.widgetY | 0,
        surface.widgetWidth | 0, surface.widgetHeight | 0,
        Math.max(1, box.props.zoom | 0), box.props.xAngle | 0,
        box.props.yAngle | 0, box.props.zAngle | 0,
        box.props.xOffset | 0, box.props.yOffset | 0,
        box.presentation?.orthographic ?? Boolean(box.props.orthographic),
        box.presentation?.fixedZoom ?? Boolean(box.props.fixedZoom),
        Boolean(source.composed), timing.frame);
      if( !ptr ) throw new Error('model widget render failed');
      const rgba = new Uint8ClampedArray(
        wasm.mod.HEAPU8.slice(ptr, ptr + width * height * 4));
      if( epoch !== modelEpoch || !element.isConnected ) return;

      let canvas = element.querySelector('canvas');
      if( !canvas ) {
        canvas = document.createElement('canvas');
        element.prepend(canvas);
      }
      canvas.width = width;
      canvas.height = height;
      canvas.getContext('2d').putImageData(new ImageData(rgba, width, height), 0, 0);
      element.classList.add('ready');
      element.removeAttribute('data-error');
      if( animated && timing.wait > 0 ) setTimeout(renderFrame, timing.wait);
    }).catch((error) => {
      if( epoch === modelEpoch && element.isConnected ) {
        if( marker ) marker.textContent = 'model unavailable';
        element.dataset.error = error.message;
      }
    });
  };
  renderFrame();
}

function colour(value) {
  const rgb = (value | 0) & 0xffffff;
  return '#' + rgb.toString(16).padStart(6, '0');
}

function drawTree(iface) {
  const tree = $('tree');
  tree.innerHTML = '';
  const depth = new Map();
  for( const box of iface.boxes ) {
    const linkedLevel = box.layer === null ? 0 : (depth.get(box.layer) ?? 0) + 1;
    const level = Number.isInteger(box.depth) ? box.depth : linkedLevel;
    depth.set(box.fileId, level);

    const row = document.createElement('div');
    const bound = box.dynamic.length ? ' <span class="bound">◆ ' + box.dynamic.join(' ') + '</span>' : '';
    const events = box.events.length ? ' <span class="rowlabel">' + box.events.join(' ') + '</span>' : '';
    const nativeDynamic = box.native && box.native.dynamic
      ? ' <span class="bound">child ' + box.native.childIndex + '</span>' : '';
    const flags = [
      box.effectiveHidden ? 'hidden' : '',
      box.culled ? 'culled' : '',
      box.emitted === false && !box.effectiveHidden && !box.culled ? 'not walked' : '',
    ].filter(Boolean);
    const visibility = flags.length
      ? ' <span class="rowlabel">[' + flags.join(', ') + ']</span>' : '';
    row.innerHTML = '&nbsp;'.repeat(level * 2) +
      box.fileId + ' ' + box.name +
      ' <span class="rowlabel">' + box.kind.toLowerCase() + ' ' +
      box.w + '×' + box.h + '</span>' + nativeDynamic + bound + events + visibility;
    row.onmouseenter = () => {
      const target = document.querySelector('.box[data-name="' + box.name + '"]');
      if( target ) target.classList.add('outline');
    };
    row.onmouseleave = () => {
      document.querySelectorAll('.box.outline').forEach((b) => b.classList.remove('outline'));
    };
    tree.appendChild(row);
  }
}

function controlsRenderKey(iface) {
  return JSON.stringify([
    chosen, iface.interfaceId, iface.source,
    iface.unmodelled || [], iface.inputs || [],
  ]);
}

function drawControls(iface, force = false) {
  const controls = $('controls');
  const nextKey = controlsRenderKey(iface);
  $('stateactions').hidden = !iface.inputs.length;
  if( !force && nextKey === renderedControlsKey ) return;
  renderedControlsKey = nextKey;
  controls.innerHTML = '';

  if( iface.unmodelled && iface.unmodelled.length ) {
    const note = document.createElement('div');
    note.className = 'unmodelled';
    note.innerHTML = '<b>shown as 0</b>' +
      iface.unmodelled.map((u) => '<div>' + u + '</div>').join('');
    controls.appendChild(note);
  }

  if( !iface.inputs.length ) {
    const empty = document.createElement('div');
    empty.className = 'rowlabel';
    empty.textContent = iface.source !== 'authored'
      ? 'This is the static cache record. Scripts referenced by its hooks are loaded ' +
        'beside the decompiled TSX in Cache records.'
      : 'Nothing here reads host state — every prop is fixed at build time, so this ' +
        'interface needs no scripts at all.';
    controls.appendChild(empty);
    return;
  }

  for( const input of iface.inputs ) {
    controls.appendChild(
      input.control.kind === 'inventory' ? inventoryControl(input)
      : input.control.kind === 'text' ? textControl(input)
      : sliderControl(input));
  }
}

function ensure(key, fallback) {
  return key in draftState ? draftState[key] : fallback;
}

function replaceState(target, source) {
  for( const key of Object.keys(target) ) delete target[key];
  Object.assign(target, JSON.parse(JSON.stringify(source)));
}

function setStateDirty(dirty, cleanLabel = 'No pending changes') {
  stateDirty = dirty;
  $('save-state').disabled = !dirty;
  $('revert-state').disabled = !dirty;
  const note = $('state-note');
  note.textContent = dirty ? 'Unsaved changes' : cleanLabel;
  note.classList.toggle('dirty', dirty);
}

function controlShell(input, body) {
  const wrap = document.createElement('div');
  wrap.className = 'control';
  const head = document.createElement('label');
  head.innerHTML = '<span>' + input.label + ' ' + input.id + '</span>';
  wrap.appendChild(head);
  wrap.appendChild(body);
  const who = document.createElement('div');
  who.className = 'who';
  who.textContent = (input.request ? input.request + ' · ' : '') +
                    'read by ' + input.readBy.join(', ');
  wrap.appendChild(who);
  return { wrap, head };
}

function sliderControl(input) {
  const value = ensure(input.key, input.initial ?? input.control.initial ?? 0);
  const slider = document.createElement('input');
  slider.type = 'range';
  slider.min = String(input.control.min ?? 0);
  slider.max = String(input.control.max ?? 100);
  slider.step = String(input.control.step ?? 1);
  slider.value = String(value);

  const { wrap, head } = controlShell(input, slider);
  const readout = document.createElement('b');
  readout.textContent = String(value);
  head.appendChild(readout);

  slider.oninput = () => {
    draftState[input.key] = Number(slider.value);
    readout.textContent = slider.value;
    slider.removeAttribute('title');
    setStateDirty(true);
  };
  return wrap;
}

function textControl(input) {
  const value = ensure(input.key, input.initial ?? '');
  const field = document.createElement('input');
  field.type = 'text';
  field.value = String(value);
  field.style.width = '100%';

  const { wrap } = controlShell(input, field);
  field.oninput = () => {
    draftState[input.key] = field.value;
    field.placeholder = '';
    setStateDirty(true);
  };
  return wrap;
}

/*
 * An inventory is not one number, so it does not get a slider. The rows are
 * (item, count) pairs under invobj:<id>, which is the shape inv_getnum and
 * inv_total ask about in src/host.js.
 */
function inventoryControl(input) {
  const key = 'invobj:' + input.id;
  let contents = ensure(key, {});

  const editContents = (edit) => {
    /* An untouched control is not committed state. Once the user edits it, clone
       first so the outgoing request gets a new, explicit inventory value. */
    contents = draftState[key] = { ...contents };
    edit(contents);
    setStateDirty(true);
  };

  const body = document.createElement('div');
  body.className = 'inv';

  const redraw = () => {
    body.innerHTML = '';
    for( const [obj, count] of Object.entries(contents) ) {
      const row = document.createElement('div');
      row.className = 'invrow';
      row.innerHTML = '<span>obj ' + obj + '</span>';
      const amount = document.createElement('input');
      amount.type = 'number'; amount.value = String(count); amount.min = '0';
      amount.oninput = () => {
        editContents((next) => { next[obj] = Number(amount.value) || 0; });
      };
      const drop = document.createElement('button');
      drop.textContent = '×';
      drop.onclick = () => {
        editContents((next) => { delete next[obj]; });
        redraw();
      };
      row.appendChild(amount);
      row.appendChild(drop);
      body.appendChild(row);
    }
    const add = document.createElement('button');
    add.textContent = '+ item';
    add.onclick = () => {
      const obj = prompt('item id');
      if( obj === null ) return;
      editContents((next) => { next[Number(obj) || 0] = 1; });
      redraw();
    };
    body.appendChild(add);
  };
  redraw();

  const { wrap } = controlShell(input, body);
  return wrap;
}

function drawRecords(iface) {
  const records = $('records');
  records.innerHTML = '';

  for( const warning of [...(data.warnings || []), ...runtimeWarnings] ) {
    const note = document.createElement('div');
    note.className = 'warn';
    note.textContent = '⚠ ' + warning;
    records.appendChild(note);
  }

  if( iface.reactSource )
    add('decompiled/' + iface.name + '.tsx (read-only view)', iface.reactSource);
  add('interfaces/' + iface.name + '.if', iface.interfaceText);
  add('interfaces/' + iface.name + '.compack', iface.compackText);
  for( const script of iface.scripts )
    add('scripts/' + script.name + '.cs2', script.source);

  function add(name, text) {
    const title = document.createElement('div');
    title.className = 'filename';
    title.textContent = name;
    const block = document.createElement('pre');
    block.textContent = text;
    records.appendChild(title);
    records.appendChild(block);
  }
}

/* ---- live React host interaction --------------------------------------- */

function currentInterface() {
  return data && data.interfaces && data.interfaces[0];
}

function resetRuntimeInteraction() {
  disposeRuntimeSession({
    host: hostRuntime,
    wasm: wasmRuntime,
  });
  heldButtons.clear();
  runtimeCycle = 0;
  closeOpMenu();
  hostRuntime = null;
  wasmRuntime = null;
  runtimeMode = 'unavailable';
}

function disposeRuntimeSession(session, notifyFocus = true) {
  if( notifyFocus && session?.host ) {
    try { session.host.dispatch({ type: 'focus_lost' }); }
    catch { /* the old runtime is being discarded */ }
  }
  try { session?.wasm?.destroy?.(); }
  catch { /* teardown is best-effort for a stale session */ }
}

function stagePoint(event) {
  const stage = $('stage');
  const rect = stage.getBoundingClientRect();
  const iface = currentInterface();
  const width = iface?.viewport?.width || stage.clientWidth;
  const height = iface?.viewport?.height || stage.clientHeight;
  const x = rect.width ? Math.floor((event.clientX - rect.left) * width / rect.width) : -1;
  const y = rect.height ? Math.floor((event.clientY - rect.top) * height / rect.height) : -1;
  return { x, y };
}

function noteRuntimeError(error) {
  const message = error?.message || String(error);
  if( !runtimeWarnings.includes(message) ) runtimeWarnings.push(message);
  $('status').textContent = 'script interaction stopped — ' + message;
  $('status').className = 'status bad';
  const iface = currentInterface();
  if( iface ) drawRecords(iface);
}

function dispatchRuntime(input) {
  const iface = currentInterface();
  if( !iface || !hostRuntime ) return null;
  const beforeVersion = hostRuntime.version;
  const beforeWarnings = runtimeWarnings.length;
  try {
    const result = hostRuntime.dispatch(input);
    if( hostRuntime.version !== beforeVersion ) {
      applyRuntimeSnapshot(iface);
      drawStage(iface);
      drawTree(iface);
    }
    if( runtimeWarnings.length !== beforeWarnings ) drawRecords(iface);
    return result;
  } catch( error ) {
    noteRuntimeError(error);
    return null;
  }
}

function pointerButton(button) {
  return button >= 0 && button <= 2 ? button : null;
}

function closeOpMenu() {
  $('stage')?.querySelector('.opmenu')?.remove();
}

function openOpMenu(menu, point) {
  closeOpMenu();
  if( !Array.isArray(menu) || menu.length === 0 ) return;
  const shell = document.createElement('div');
  shell.className = 'opmenu';
  shell.style.left = Math.max(0, Math.min($('stage').clientWidth - 150, point.x)) + 'px';
  shell.style.top = Math.max(0, Math.min($('stage').clientHeight - 24, point.y)) + 'px';
  shell.onpointerdown = (event) => event.stopPropagation();
  for( const item of menu ) {
    const button = document.createElement('button');
    button.type = 'button';
    button.textContent = item.text || ('Option ' + item.opIndex);
    button.onclick = () => {
      closeOpMenu();
      dispatchRuntime({ type: 'op', target: item.component, opIndex: item.opIndex });
    };
    shell.appendChild(button);
  }
  $('stage').appendChild(shell);
}

function browserVk(event) {
  if( event.code === 'Delete' ) return 127;
  if( /^Key[A-Z]$/.test(event.code) ) return event.code.charCodeAt(3);
  if( /^Digit[0-9]$/.test(event.code) ) return 48 + Number(event.code.slice(5));
  if( /^Numpad[0-9]$/.test(event.code) ) return 96 + Number(event.code.slice(6));
  if( /^F(?:[1-9]|1[0-2])$/.test(event.code) ) return 111 + Number(event.code.slice(1));
  const fallback = {
    Backspace: 8, Tab: 9, Enter: 13, NumpadEnter: 13, ShiftLeft: 16,
    ShiftRight: 16, ControlLeft: 17, ControlRight: 17, AltLeft: 18,
    AltRight: 18, Escape: 27, Space: 32, PageUp: 33, PageDown: 34,
    End: 35, Home: 36, ArrowLeft: 37, ArrowUp: 38, ArrowRight: 39,
    ArrowDown: 40, NumpadMultiply: 106, NumpadAdd: 107,
    NumpadSubtract: 109, NumpadDecimal: 110, NumpadDivide: 111,
  };
  return fallback[event.code] ?? -1;
}

/* src/input/torirs_keymap.c, indexed by DOM/VK code. */
const OSRS_KEY_MAP = [
  -1,-1,-1,-1,-1,-1,-1,-1,85,80,84,-1,91,84,-1,-1,
  81,82,86,-1,-1,-1,-1,-1,-1,-1,-1,13,-1,-1,-1,-1,
  83,104,105,103,102,96,98,97,99,-1,-1,-1,-1,-1,-1,-1,
  25,16,17,18,19,20,21,22,23,24,-1,-1,-1,-1,-1,-1,
  -1,48,68,66,50,34,51,52,53,39,54,55,56,70,69,40,
  41,32,35,49,36,38,67,33,65,37,64,-1,-1,-1,-1,-1,
  228,231,227,233,224,219,225,230,226,232,89,87,-1,88,229,90,
  1,2,3,4,5,6,7,8,9,10,11,12,-1,-1,-1,101,
];

function osrsKey(event) {
  const vk = browserVk(event);
  return vk >= 0 && vk < OSRS_KEY_MAP.length ? OSRS_KEY_MAP[vk] : -1;
}

function keyCharacter(event) {
  if( event.ctrlKey || event.altKey || event.metaKey || event.isComposing ||
      typeof event.key !== 'string' || [...event.key].length !== 1 ) return -1;
  const codepoint = event.key.codePointAt(0);
  return codepoint >= 32 && codepoint <= 255 ? codepoint : -1;
}

const stage = $('stage');
stage.onpointermove = (event) => {
  dispatchRuntime({ type: 'pointer_move', ...stagePoint(event) });
};
stage.onpointerdown = (event) => {
  const button = pointerButton(event.button);
  if( button === null || !hostRuntime ) return;
  event.preventDefault();
  closeOpMenu();
  stage.focus({ preventScroll: true });
  try { stage.setPointerCapture(event.pointerId); } catch { /* already captured */ }
  heldButtons.add(button);
  const point = stagePoint(event);
  const result = dispatchRuntime({ type: 'pointer_down', button, ...point });
  if( button === 2 ) openOpMenu(result?.menu, point);
};
stage.onpointerup = (event) => {
  const button = pointerButton(event.button);
  if( button === null || !hostRuntime ) return;
  event.preventDefault();
  heldButtons.delete(button);
  dispatchRuntime({ type: 'pointer_up', button, ...stagePoint(event) });
  try { stage.releasePointerCapture(event.pointerId); } catch { /* not captured */ }
};
stage.onpointercancel = (event) => {
  const point = stagePoint(event);
  for( const button of heldButtons )
    dispatchRuntime({ type: 'pointer_up', button, ...point });
  heldButtons.clear();
};
stage.onpointerleave = () => {
  if( heldButtons.size === 0 ) dispatchRuntime({ type: 'pointer_move', x: -1, y: -1 });
};
stage.addEventListener('wheel', (event) => {
  if( !hostRuntime || event.deltaY === 0 ) return;
  event.preventDefault();
  dispatchRuntime({ type: 'wheel', wheel: -event.deltaY, ...stagePoint(event) });
}, { passive: false });
stage.oncontextmenu = (event) => {
  if( hostRuntime ) event.preventDefault();
};
stage.onkeydown = (event) => {
  if( !hostRuntime ) return;
  const keyTyped = osrsKey(event);
  const keyPressed = keyCharacter(event);
  if( keyTyped < 0 && keyPressed < 0 ) return;
  event.preventDefault();
  if( !event.repeat && keyTyped >= 0 )
    dispatchRuntime({ type: 'key_down', keyTyped, keyPressed: Math.max(0, keyPressed) });
  dispatchRuntime({ type: 'key', keyTyped, keyPressed });
};
stage.onkeyup = (event) => {
  if( !hostRuntime ) return;
  const keyTyped = osrsKey(event);
  if( keyTyped < 0 ) return;
  event.preventDefault();
  dispatchRuntime({ type: 'key_up', keyTyped, keyPressed: 0 });
};
stage.onblur = () => {
  heldButtons.clear();
  closeOpMenu();
  dispatchRuntime({ type: 'focus_lost' });
};

setInterval(() => {
  if( hostRuntime && document.visibilityState === 'visible' )
    dispatchRuntime({ type: 'tick', cycle: ++runtimeCycle });
}, 20);

$('pick').onfocus = () => {
  renderPicker('');
  openPicker();
  $('pick').select();
};
$('pick').oninput = () => {
  renderPicker($('pick').value);
  openPicker();
};
$('pick').onkeydown = (event) => {
  if( event.key === 'ArrowDown' || event.key === 'ArrowUp' ) {
    event.preventDefault();
    if( !$('pickmenu').classList.contains('open') ) {
      renderPicker('');
      openPicker();
    }
    movePickerActive(event.key === 'ArrowDown' ? 1 : -1);
  } else if( event.key === 'Enter' && pickerActive >= 0 ) {
    event.preventDefault();
    chooseInterface(pickerMatches[pickerActive]);
  } else if( event.key === 'Escape' ) {
    event.preventDefault();
    closePicker();
    $('pick').blur();
  }
};
document.addEventListener('pointerdown', (event) => {
  if( !$('picker').contains(event.target) ) closePicker();
});
$('wire').onchange = () => render();
$('save-state').onclick = () => {
  replaceState(state, draftState);
  setStateDirty(false, 'State saved');
  resetRuntimeInteraction();
  refresh();
};
$('revert-state').onclick = () => {
  replaceState(draftState, state);
  setStateDirty(false, 'Draft discarded');
  const iface = data && data.interfaces && data.interfaces[0];
  if( iface ) drawControls(iface, true);
};
$('controls').onkeydown = (event) => {
  if( event.key === 'Enter' && (event.metaKey || event.ctrlKey) && stateDirty ) {
    event.preventDefault();
    $('save-state').click();
  }
};
$('add').onclick = async () => {
  const name = prompt('component name (a file, ui/<name>.tsx)');
  if( !name ) return;
  const response = await fetch('/new', { method: 'POST', body: JSON.stringify({ name }) });
  const result = await response.json();
  if( result.error ) alert(result.error);
};

async function refreshCatalogQuietly() {
  try {
    const listing = await fetch('/catalog').then((response) => response.json());
    catalog = listing.interfaces || catalog;
  } catch { /* The current picker remains usable while the server rebuilds. */ }
}

new EventSource('/events').onmessage = () => {
  /* Source saves invalidate the live script/tree session, but not the editor shell. Keep the
     picker, Host-state draft, focus and pane scroll positions alive; replace
     only the preview, runtime tree and generated cache/script records. */
  resetRuntimeInteraction();
  refreshCatalogQuietly();
  refresh({ hotReload: true });
};
refresh();
</script>
</body>
</html>`;
}
