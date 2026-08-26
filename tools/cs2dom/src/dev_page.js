/*
 * The dev page.
 *
 * The developer chrome stays as one stable document, while a small local React
 * bundle owns the committed preview surface. Source saves replace only the
 * worker session/tree/records; picker and Host-state drafts stay mounted.
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

  /* The production soft renderer clears its interface framebuffer to #202428.
     Transparent margins and sprite pixels must composite over that same clear
     colour; a checkerboard changes the bank frame even when every widget is
     otherwise byte-for-byte correct. */
  #stage {
    position: relative; margin: 0 auto;
    background: #202428;
    overflow: hidden; cursor: default;
    touch-action: none; user-select: none;
  }
  #stage:focus-visible { outline: 0; }
  main section:first-child:has(#stage:focus-visible) > h2:first-child { color: var(--accent); }
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
  .tree div {
    padding: 1px 0; cursor: default; white-space: nowrap;
    content-visibility: auto; contain-intrinsic-size: auto 18px;
  }
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
  .record { content-visibility: auto; contain-intrinsic-size: auto 26px; margin-bottom: 5px; }
  .record > summary { cursor: pointer; list-style-position: inside; }
  .record > pre { margin-top: 4px; }
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

<script type="module">
import { createWorkerRuntimeController } from '/runtime/worker_runtime_controller.js';
import { createModelRenderController } from '/runtime/model_render_controller.js';
import { paintCacheText } from '/runtime/font_runtime.js';
import { mountRetainedInterfaceStage } from '/react-runtime.js';

let state = {};              // Values committed to the live React-side host.
let draftState = {};         // Host-state edits waiting for an explicit save.
let data = null;
let catalog = [];
let catalogSearchIndex = [];
let catalogByKey = new Map();
let chosen = null;
let pickerMatches = [];
let pickerActive = -1;
let pickerActiveRow = null;
let pickerSearchJob = null;
let pickerSearchEpoch = 0;
let pickerPendingMove = 0;
let refreshEpoch = 0;
let renderedControlsKey = null;
let renderedStageBoxes = new Map();
let renderedTreeRows = new Map();
let stageBoxesByName = new Map();
let outlinedStageName = null;
let stateDirty = false;
let runtimeController = null;
let reactStageMount = null;
let reactStageSurface = null;
let runtimeSession = null;
let pendingRuntimeSession = null;
let runtimeWarnings = [];
let runtimeMode = 'unavailable';
let runtimeCycle = 0;
const runtimeInitMetrics = { count: 0, maxEnqueueMs: 0, overBudget: 0 };
globalThis.__cs2domRuntimeInitMetrics = runtimeInitMetrics;
const runtimeInteractionMetrics = Object.fromEntries(
  ['enqueue', 'dispatch', 'stage', 'controller'].map((phase) =>
    [phase, { count: 0, maxMs: 0, overBudget: 0 }]));
globalThis.__cs2domRuntimeInteractionMetrics = runtimeInteractionMetrics;
let runtimeRenderFrame = 0;
let pendingRuntimeRender = null;
let recordsDrawFrame = 0;
let pendingRecordsIface = null;
let recordDrawJob = null;
let recordDrawEpoch = 0;
let activeRecordRoot = null;
let recordTextHead = null;
let recordTextTail = null;
let recordTextFrame = 0;
let stateCopyJob = null;
let controlDrawJob = null;
let renderedControlsScope = null;
let treeDrawJob = null;
let stageDrawJob = null;
let stageDrawEpoch = 0;
let stageRectFrame = 0;
let stageRectCache = null;
let modelRenderController = null;
let opMenuElement = null;
let interactionEpoch = 0;
const heldButtons = new Set();

const $ = (id) => document.getElementById(id);

/* A frame callback is a 16.7ms throughput tax even when a slice used only a
   fraction of its 4ms allowance. This FIFO posts one task at a time: browser
   input task sources can run between every slice, jobs rejoin at the tail, and
   a timer fence after at most two turns caps a continuous burst near 8ms and
   gives rendering/network work a fair
   chance even on engines which aggressively drain MessageChannel messages. */
let cooperativeHead = null;
let cooperativeTail = null;
let cooperativePosted = false;
let cooperativeBurst = 0;
const cooperativeChannel = typeof globalThis.__cs2domPostTask !== 'function' &&
  typeof MessageChannel === 'function' ? new MessageChannel() : null;
const cooperativeTaskMetrics = {
  scheduled: 0, completed: 0, queueDepth: 0, maxQueueDepth: 0,
  maxTaskMs: 0, overBudget: 0, timerFences: 0,
};
globalThis.__cs2domCooperativeTaskMetrics = cooperativeTaskMetrics;
if( cooperativeChannel ) cooperativeChannel.port1.onmessage = runCooperativeTurn;

function scheduleCooperativeTask(callback) {
  const node = { callback, next: null };
  if( cooperativeTail ) cooperativeTail.next = node;
  else cooperativeHead = node;
  cooperativeTail = node;
  cooperativeTaskMetrics.scheduled++;
  cooperativeTaskMetrics.queueDepth++;
  cooperativeTaskMetrics.maxQueueDepth = Math.max(
    cooperativeTaskMetrics.maxQueueDepth, cooperativeTaskMetrics.queueDepth);
  postCooperativeTurn();
}

function postCooperativeTurn() {
  if( cooperativePosted || !cooperativeHead ) return;
  cooperativePosted = true;
  if( cooperativeBurst >= 2 ) {
    cooperativeBurst = 0;
    cooperativeTaskMetrics.timerFences++;
    postCooperativeTimer();
  } else {
    const deterministicPost = globalThis.__cs2domPostTask;
    if( typeof deterministicPost === 'function' )
      deterministicPost(runCooperativeTurn);
    else if( cooperativeChannel ) cooperativeChannel.port2.postMessage(0);
    else postCooperativeTimer();
  }
}

function postCooperativeTimer() {
  const deterministicTimer = globalThis.__cs2domPostTimer;
  if( typeof deterministicTimer === 'function' ) deterministicTimer(runCooperativeTurn);
  else setTimeout(runCooperativeTurn, 0);
}

function runCooperativeTurn() {
  cooperativePosted = false;
  if( globalThis.navigator?.scheduling?.isInputPending?.() ) {
    cooperativeTaskMetrics.timerFences++;
    cooperativePosted = true;
    postCooperativeTimer();
    return;
  }
  const node = cooperativeHead;
  if( !node ) { cooperativeBurst = 0; return; }
  cooperativeHead = node.next;
  if( !cooperativeHead ) cooperativeTail = null;
  cooperativeTaskMetrics.queueDepth--;
  const startedAt = performance.now();
  try { node.callback(); }
  finally {
    const elapsed = performance.now() - startedAt;
    cooperativeTaskMetrics.completed++;
    cooperativeTaskMetrics.maxTaskMs = Math.max(cooperativeTaskMetrics.maxTaskMs, elapsed);
    if( elapsed >= 10 ) cooperativeTaskMetrics.overBudget++;
    if( cooperativeHead ) { cooperativeBurst++; postCooperativeTurn(); }
    else cooperativeBurst = 0;
  }
}

async function refresh({ hotReload = false } = {}) {
  const epoch = ++refreshEpoch;
  if( catalog.length === 0 ) {
    const listing = await fetch('/catalog').then((response) => response.json());
    if( epoch !== refreshEpoch ) return;
    catalog = listing.interfaces || [];
    indexCatalog();
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
    controller: ensureRuntimeController(),
    generation: 0,
    iface,
    installed: false,
    warnings: [],
    mode: 'unavailable',
  };
  pendingRuntimeSession = session;
  try {
    const config = {
      ir: iface.runtime.ir,
      state,
      viewport: iface.viewport,
      program: iface.runtime.bytecode,
      hostData: iface.runtime.hostData,
      hostDataUrl: iface.runtime.hostDataUrl,
    };
    const enqueueStartedAt = performance.now();
    const loading = session.controller.worker
      ? session.controller.reload(config) : session.controller.start(config);
    const enqueueMs = performance.now() - enqueueStartedAt;
    runtimeInitMetrics.count++;
    runtimeInitMetrics.maxEnqueueMs = Math.max(runtimeInitMetrics.maxEnqueueMs, enqueueMs);
    if( enqueueMs >= 10 ) {
      runtimeInitMetrics.overBudget++;
      console.warn('cs2dom runtime init clone exceeded 10ms:', enqueueMs.toFixed(1) + 'ms');
    }
    session.generation = session.controller.session;
    const ready = await loading;
    session.mode = ready.mode;
    session.warnings = session.controller.warnings;
    if( ready.render ) applyWorkerRender(session, ready.render);
  } catch( error ) {
    session.mode = 'unavailable';
    pushRuntimeWarning(session.warnings, 'script runtime stopped: ' + error.message);
  }
  return session;
}

function installRuntimeSession(session) {
  heldButtons.clear();
  interactionEpoch++;
  runtimeCycle = 0;
  closeOpMenu();
  runtimeSession = session;
  pendingRuntimeSession = null;
  session.installed = true;
  runtimeWarnings = session.warnings;
  runtimeMode = session.mode;
  if( session.mode !== 'unavailable' ) requestRuntimeTree(session);
}

function ensureRuntimeController() {
  if( runtimeController ) return runtimeController;
  runtimeController = createWorkerRuntimeController({
    onStagePatch: ({ render, patch }) => {
      const session = routedRuntimeSession();
      if( !session ) return;
      applyWorkerRender(session, render);
    },
    onTreeChunk: (chunk) => {
      const session = routedRuntimeSession();
      if( !session || chunk.requestId !== session.treeRequestId ) return;
      if( chunk.begin ) session.iface.treeBoxes = [];
      if( chunk.boxes?.length )
        session.iface.treeBoxes.splice(chunk.index, chunk.boxes.length, ...chunk.boxes);
      /* Accumulate transfer chunks without reconciling the growing prefix 28
         times for bankmain. One keyed reconciliation runs at idle only after
         the version-fenced stream is complete. */
      if( session.installed && chunk.done && !chunk.stale ) {
        scheduleTreeDraw(session.iface);
        if( $('wire').checked ) scheduleRuntimeRender(session.iface, session.controller, false);
      }
    },
    onWarning: (warning) => {
      const session = routedRuntimeSession();
      if( !session ) return;
      pushRuntimeWarning(session.warnings, warning);
      if( session.installed && session.iface === currentInterface() )
        scheduleRecordsDraw(session.iface);
    },
    onResult: ({ result }) => {
      if( result?.interaction && !result.interaction.menuOpen ) closeOpMenu();
    },
    onTiming: ({ phase, timing }) => observeRuntimeInteraction(phase,
      phase === 'stage' ? timing?.maxStageTaskMs : timing?.maxDispatchTaskMs),
    onReceiveTiming: ({ elapsed }) => observeRuntimeInteraction('controller', elapsed),
    onBudgetViolation: ({ elapsed, phase = 'enqueue', source = 'main-thread' }) =>
      console.warn('cs2dom ' + (source === 'runtime-worker' ? 'worker ' : '') +
        phase + ' exceeded 10ms:', elapsed.toFixed(1) + 'ms'),
  });
  reactStageMount = mountRetainedInterfaceStage($('stage'), {
    store: runtimeController,
    onCommit: commitReactStage,
  });
  return runtimeController;
}

function commitReactStage(snapshot, surface) {
  reactStageSurface = surface;
  const session = routedRuntimeSession();
  if( !snapshot.render ) {
    if( surface ) clearStageSurface();
    return;
  }
  if( !session || snapshot.session !== session.generation ) return;
  applyWorkerRender(session, snapshot.render);
  if( session.installed )
    scheduleRuntimeRender(session.iface, session.controller, true, snapshot.patch);
}

function clearStageSurface() {
  stageDrawJob = null;
  stageDrawEpoch++;
  for( const entry of renderedStageBoxes.values() ) retireStageElement(entry.element);
  renderedStageBoxes.clear();
  stageBoxesByName.clear();
  outlinedStageName = null;
  reactStageSurface?.replaceChildren();
}

function observeRuntimeInteraction(phase, rawElapsed) {
  const metric = runtimeInteractionMetrics[phase];
  const elapsed = Number(rawElapsed);
  if( !metric || !Number.isFinite(elapsed) || elapsed < 0 ) return;
  metric.count++;
  metric.maxMs = Math.max(metric.maxMs, elapsed);
  if( elapsed >= 10 ) metric.overBudget++;
}

function routedRuntimeSession() {
  const candidate = pendingRuntimeSession || runtimeSession;
  return candidate && candidate.controller === runtimeController &&
    candidate.generation === runtimeController.session ? candidate : null;
}

function applyWorkerRender(session, render) {
  session.iface.viewport = render.viewport;
  session.iface.boxes = render.boxes;
  /* Keep the controller's logical renderer keys parallel to its paint boxes.
     They intentionally do not live on HOST refs/boxes: VM identity is a
     transient generation-fenced handle, while React/DOM identity is the
     stable logical slot carried by each committed stage entry. */
  session.iface.stageEntries = render.entries;
  session.iface.runtimeVersion = render.version;
}

function requestRuntimeTree(session) {
  if( session.treePending ) {
    session.treeWanted = true;
    return;
  }
  session.treePending = true;
  session.treeWanted = false;
  /* onTreeChunk owns the inspector array; do not retain a second 1,700-widget
     copy inside the controller while bankmain streams. */
  const pending = session.controller.requestTree({ collect: false });
  session.treeRequestId = session.controller.requestId;
  pending.then(({ version }) => {
    if( version !== session.iface.runtimeVersion ) session.treeWanted = true;
  }).catch((error) => {
    if( session.installed && session.generation === session.controller.session &&
        !String(error?.message).includes('changed while it was streaming') ) noteRuntimeError(error);
  }).finally(() => {
    session.treePending = false;
    if( session.treeWanted && session.installed && session === runtimeSession )
      scheduleRuntimeTreeRefresh(session);
  });
}

function pushRuntimeWarning(warnings, message) {
  if( message && !warnings.includes(message) ) warnings.push(message);
}

function populatePicker() {
  const pick = $('pick');
  pick.placeholder = 'Search ' + catalog.length.toLocaleString() + ' interfaces…';
  syncPickerLabel();
  renderPicker('');
}

function indexCatalog() {
  catalogByKey = new Map(catalog.map((entry) => [entry.key, entry]));
  catalogSearchIndex = catalog.map((entry) => {
    const name = entry.name.toLowerCase();
    const source = entry.source.toLowerCase();
    const title = sourceTitle(entry.source).toLowerCase();
    const id = String(entry.interfaceId);
    return { entry, name, source, title, id,
      haystack: name + ' ' + source + ' ' + title + ' ' + id };
  });
}

function sourceTitle(source) {
  return source === 'authored' ? 'Authored TSX'
    : source === 'dat2' ? 'Dat2 cache' : 'OSRS-Content';
}

function syncPickerLabel() {
  const entry = catalogByKey.get(chosen);
  $('pick').value = entry ? entry.name : '';
}

function renderPicker(query) {
  const menu = $('pickmenu');
  pickerActiveRow?.classList.remove('active');
  pickerActiveRow = null;
  pickerActive = -1;
  pickerMatches = [];
  menu.setAttribute('aria-busy', 'true');
  menu.inert = true;
  const trimmed = query.trim();
  pickerSearchJob = {
    epoch: ++pickerSearchEpoch, menu, query: trimmed,
    terms: trimmed.toLowerCase().split(/\\s+/).filter(Boolean),
    index: 0, matches: 0, ranked: [],
    bySource: new Map(['authored', 'dat2', 'content'].map((source) => [source, []])),
    plan: null, planIndex: 0, phase: 'scan',
    slices: 0, startedAt: performance.now(),
  };
  schedulePickerSlice(pickerSearchJob);
}

const PICKER_SLICE_BUDGET_MS = 4;
const pickerSliceMetrics = {
  count: 0, maxMs: 0, overBudget: 0, completed: 0,
  maxSlices: 0, maxCompletionMs: 0,
};
globalThis.__cs2domPickerSliceMetrics = pickerSliceMetrics;

function rankCatalogRecord(record, terms) {
  if( !terms.every((term) => record.haystack.includes(term)) ) return null;
  let score = 0;
  for( const term of terms ) score += record.name === term ? 0
    : record.name.startsWith(term) ? 1 : record.name.includes(term) ? 3
    : record.id === term ? 2
    : record.source.startsWith(term) || record.title.startsWith(term) ? 4 : 8;
  return { entry: record.entry, score };
}

function comparePickerRank(a, b) {
  return a.score - b.score ||
    a.entry.name.localeCompare(b.entry.name, undefined, { numeric: true }) ||
    a.entry.interfaceId - b.entry.interfaceId;
}

function offerPickerRank(list, ranked, limit) {
  let low = 0;
  let high = list.length;
  while( low < high ) {
    const middle = (low + high) >>> 1;
    if( comparePickerRank(list[middle], ranked) <= 0 ) low = middle + 1;
    else high = middle;
  }
  if( low < limit ) list.splice(low, 0, ranked);
  if( list.length > limit ) list.pop();
}

function schedulePickerSlice(job) {
  scheduleCooperativeTask(() => runPickerSlice(job));
}

function runPickerSlice(job) {
  if( job !== pickerSearchJob || job.epoch !== pickerSearchEpoch ) return;
  const startedAt = performance.now();
  job.slices++;
  do {
    if( job.phase === 'scan' ) {
      if( job.index < catalogSearchIndex.length ) {
        const ranked = rankCatalogRecord(catalogSearchIndex[job.index++], job.terms);
        if( ranked ) {
          job.matches++;
          const list = job.query ? job.ranked : job.bySource.get(ranked.entry.source);
          if( list ) offerPickerRank(list, ranked, job.query ? 120 : 20);
        }
      } else {
        const visible = job.query ? job.ranked
          : ['authored', 'dat2', 'content'].flatMap((source) => job.bySource.get(source));
        job.plan = pickerPlan(visible, job.matches, job.query);
        job.phase = 'clear';
      }
    } else if( job.phase === 'clear' ) {
      if( job.menu.firstElementChild ) job.menu.firstElementChild.remove();
      else job.phase = 'build';
    } else if( job.phase === 'build' ) {
      if( job.planIndex < job.plan.length ) buildPickerPlanItem(job, job.plan[job.planIndex++]);
      else job.phase = 'done';
    }
  } while( job.phase !== 'done' &&
    performance.now() - startedAt < PICKER_SLICE_BUDGET_MS );
  const elapsed = performance.now() - startedAt;
  pickerSliceMetrics.count++;
  pickerSliceMetrics.maxMs = Math.max(pickerSliceMetrics.maxMs, elapsed);
  if( elapsed >= 10 ) pickerSliceMetrics.overBudget++;
  if( job.phase !== 'done' ) return schedulePickerSlice(job);
  job.menu.removeAttribute('aria-busy');
  job.menu.inert = false;
  pickerSearchJob = null;
  pickerSliceMetrics.completed++;
  pickerSliceMetrics.maxSlices = Math.max(pickerSliceMetrics.maxSlices, job.slices);
  pickerSliceMetrics.maxCompletionMs = Math.max(
    pickerSliceMetrics.maxCompletionMs, performance.now() - job.startedAt);
  if( pickerPendingMove ) {
    const delta = pickerPendingMove;
    pickerPendingMove = 0;
    movePickerActive(delta);
  }
}

function pickerPlan(visible, matches, query) {
  if( visible.length === 0 ) return [{ kind: 'empty', query }];
  const plan = [];
  let pickerIndex = 0;
  for( const source of ['authored', 'dat2', 'content'] ) {
    const entries = visible.filter((ranked) => ranked.entry.source === source);
    if( entries.length === 0 ) continue;
    plan.push({ kind: 'group', source });
    for( const ranked of entries ) plan.push({
      kind: 'entry', entry: ranked.entry, source, pickerIndex: pickerIndex++,
    });
  }
  if( matches > visible.length ) plan.push({ kind: 'more', shown: visible.length, matches });
  return plan;
}

function buildPickerPlanItem(job, item) {
  const element = document.createElement(item.kind === 'entry' ? 'button' : 'div');
  if( item.kind === 'empty' ) {
    element.className = 'pickempty';
    element.textContent = 'No interfaces match “' + item.query + '”.';
  } else if( item.kind === 'group' ) {
    element.className = 'pickgroup';
    element.textContent = sourceTitle(item.source);
  } else if( item.kind === 'more' ) {
    element.className = 'pickmore';
    element.textContent = 'Showing ' + item.shown.toLocaleString() + ' of ' +
      item.matches.toLocaleString() + ' matches — keep typing to narrow the list.';
  } else {
    const entry = item.entry;
    pickerMatches.push(entry);
    element.type = 'button';
    element.id = 'pickoption-' + item.pickerIndex;
    element.className = 'pickrow';
    element.setAttribute('role', 'option');
    element.setAttribute('aria-selected', entry.key === chosen ? 'true' : 'false');
    const sourceBadge = document.createElement('span');
    sourceBadge.className = 'picksource ' + item.source;
    sourceBadge.textContent = item.source === 'content' ? 'files' : item.source;
    const name = document.createElement('span');
    name.className = 'pickname';
    name.textContent = entry.name;
    const id = document.createElement('span');
    id.className = 'pickid';
    id.textContent = '#' + entry.interfaceId;
    element.append(sourceBadge, name, id);
    element.onpointerdown = budgetedInputHandler('picker-choice-pointerdown',
      (event) => event.preventDefault());
    element.onclick = budgetedInputHandler('picker-choice', () => chooseInterface(entry));
  }
  job.menu.appendChild(element);
}

function openPicker() {
  $('pickmenu').classList.add('open');
  $('pick').setAttribute('aria-expanded', 'true');
}

function closePicker() {
  pickerSearchJob = null;
  pickerSearchEpoch++;
  pickerPendingMove = 0;
  $('pickmenu').classList.remove('open');
  $('pickmenu').removeAttribute('aria-busy');
  $('pickmenu').inert = false;
  $('pick').setAttribute('aria-expanded', 'false');
  $('pick').removeAttribute('aria-activedescendant');
  pickerActiveRow = null;
  pickerActive = -1;
  syncPickerLabel();
}

function movePickerActive(delta) {
  if( pickerMatches.length === 0 ) {
    pickerPendingMove += delta;
    return;
  }
  pickerActive = pickerActive < 0
    ? (delta > 0 ? 0 : pickerMatches.length - 1)
    : ((pickerActive + delta) % pickerMatches.length + pickerMatches.length) %
      pickerMatches.length;
  pickerActiveRow?.classList.remove('active');
  const row = $('pickoption-' + pickerActive);
  pickerActiveRow = row;
  row.classList.add('active');
  $('pick').setAttribute('aria-activedescendant', row.id);
  requestAnimationFrame(() => {
    if( pickerActiveRow === row ) row.scrollIntoView({ block: 'nearest' });
  });
}

function chooseInterface(entry) {
  chosen = entry.key;
  stateCopyJob = null;
  state = {};
  draftState = {};
  renderedControlsKey = null;
  renderedControlsScope = null;
  controlDrawJob = null;
  setStateDirty(false);
  clearRuntimeInteractionChrome();
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
      clearStageSurface();
      closeOpMenu();
      $('tree').innerHTML = '';
      $('controls').innerHTML = '';
      $('stateactions').hidden = true;
      renderedTreeRows.clear();
      renderedControlsKey = null;
      renderedControlsScope = null;
      controlDrawJob = null;
    }
    return;
  }

  $('status').textContent = 'built ' + new Date().toLocaleTimeString() +
    (runtimeMode === 'static' ? ' — static (no CS2 bytecode)' :
      runtimeMode === 'unavailable' ? ' — runtime unavailable' :
      runtimeMode === 'typescript' ? ' — TypeScript CS2VM' : ' — C CS2VM/WASM');
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

function drawStage(iface, { upsert = null, upsertBatches = null, dedupePartial = false } = {}) {
  const shell = $('stage');
  /* The pure renderer harness intentionally omits the React bundle. Retain a
     shell fallback there; a live mounted root never lets React and the painter
     own the same element. */
  const stage = reactStageSurface || (!reactStageMount ? shell : null);
  if( !stage ) {
    scheduleCooperativeTask(() => {
      if( iface === currentInterface() ) drawStage(iface, { upsert, upsertBatches, dedupePartial });
    });
    return;
  }
  const wire = $('wire').checked;
  /* The interaction channel carries only paintable boxes. Wire diagnostics
     opt into the most recent full tree, which arrives independently in idle
     64-widget chunks and therefore never bloats an input response. */
  const fullBoxes = (wire && iface.treeBoxes?.length ? iface.treeBoxes : iface.boxes) || [];
  const fullEntries = !wire && Array.isArray(iface.stageEntries) &&
    iface.stageEntries.length === fullBoxes.length ? iface.stageEntries : null;
  const partialBatches = Array.isArray(upsertBatches) ? upsertBatches
    : Array.isArray(upsert) ? [upsert] : null;
  const partial = !wire && partialBatches !== null;
  const boxes = partial ? null : fullBoxes;
  const root = fullBoxes[0];
  const width = iface.viewport?.width || (root ? Math.max(root.w, 32) : 256);
  const height = iface.viewport?.height || (root ? Math.max(root.h, 32) : 128);
  shell.style.width = width + 'px';
  shell.style.height = height + 'px';
  shell.className = $('wire').checked ? 'wire' : '';
  shell.tabIndex = 0;
  shell.setAttribute('aria-label', 'Interactive React interface preview');
  $('dims').textContent = width + '×' + height + ' — interface ' + iface.interfaceId +
    ' — live React tree';

  const job = stageDrawJob = {
    epoch: ++stageDrawEpoch,
    iface, stage, boxes, entries: partial ? null : fullEntries,
    wire, width, height, partial,
    /* Runtime patches can contain thousands of changed widgets. Coalesce
       them inside the same cooperative budget as DOM reconciliation instead
       of doing an unmeasured Map/array pass in the message handler. */
    partialBatches: partial ? partialBatches : null,
    partialBatchIndex: 0, partialEntryIndex: 0,
    partialMap: partial && dedupePartial ? new Map() : null, partialIterator: null,
    phase: partial && dedupePartial ? 'merge' : 'scan', index: 0,
    desired: partial ? null : [], desiredKeys: partial ? null : new Set(),
    cleanupIterator: null, cursor: null,
    slices: 0, startedAt: performance.now(),
  };
  /* Start immediately, but surrender before 4ms. Subsequent slices are frame
     tasks, so an input event can always run between them. */
  runStageSlice(job);
}

function stageBoxKey(iface, box, index, renderKey = null) {
  return iface.interfaceId + ':' +
    (renderKey || box.ref?.key || box.name || box.fileId || index);
}

function stageBoxSignature(box, iface, role, modelSurface) {
  const sequence = box.presentation?.sequence ?? box.props.seq ?? -1;
  return JSON.stringify([
    role, box.x, box.y, box.w, box.h, box.clip, box.props, box.presentation,
    iface.spriteSource, iface.modelSource, modelSurface,
    role === 'model' && sequence >= 0 ? box.ref?.generation : null,
  ]);
}

function createStageBox(box, iface, role, modelSurface) {
  const element = document.createElement('div');
  element.className = 'box ' + role;
  element.style.left = (modelSurface?.left ?? box.x) + 'px';
  element.style.top = (modelSurface?.top ?? box.y) + 'px';
  element.style.width = (modelSurface?.width ?? box.w) + 'px';
  element.style.height = (modelSurface?.height ?? box.h) + 'px';
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
  paint(element, box, iface, modelSurface);
  return element;
}

const STAGE_SLICE_BUDGET_MS = 3;
const stageSliceMetrics = {
  count: 0, maxMs: 0, overBudget: 0, completed: 0, stale: 0,
  maxSlices: 0, maxCompletionMs: 0,
};
globalThis.__cs2domStageSliceMetrics = stageSliceMetrics;

function scheduleStageSlice(job) {
  scheduleCooperativeTask(() => runStageSlice(job));
}

function runStageSlice(job) {
  if( job !== stageDrawJob || job.epoch !== stageDrawEpoch ) {
    stageSliceMetrics.stale++;
    return;
  }
  const startedAt = performance.now();
  job.slices++;
  let keepGoing = true;
  do {
    if( job.phase === 'merge' ) mergeStagePatch(job);
    else if( job.phase === 'scan' ) scanStageBox(job);
    else if( job.phase === 'commit' ) commitStageBox(job);
    else if( job.phase === 'cleanup' ) cleanupStageBox(job);
    else if( job.phase === 'order' ) orderStageBox(job);
    else keepGoing = false;
    if( job.phase === 'done' ) keepGoing = false;
  } while( keepGoing && performance.now() - startedAt < STAGE_SLICE_BUDGET_MS );

  const elapsed = performance.now() - startedAt;
  stageSliceMetrics.count++;
  stageSliceMetrics.maxMs = Math.max(stageSliceMetrics.maxMs, elapsed);
  if( elapsed >= 10 ) stageSliceMetrics.overBudget++;
  if( job.phase === 'done' ) {
    stageSliceMetrics.completed++;
    stageSliceMetrics.maxSlices = Math.max(stageSliceMetrics.maxSlices, job.slices);
    stageSliceMetrics.maxCompletionMs = Math.max(
      stageSliceMetrics.maxCompletionMs, performance.now() - job.startedAt);
    stageDrawJob = null;
    scheduleStageRectRead();
  } else scheduleStageSlice(job);
}

function mergeStagePatch(job) {
  while( job.partialBatchIndex < job.partialBatches.length ) {
    const batch = job.partialBatches[job.partialBatchIndex] || [];
    if( job.partialEntryIndex < batch.length ) {
      const entry = batch[job.partialEntryIndex++];
      if( entry?.key && entry.box ) job.partialMap.set(entry.key, entry);
      return;
    }
    job.partialBatchIndex++;
    job.partialEntryIndex = 0;
  }
  job.partialIterator = job.partialMap.values();
  job.phase = 'scan';
}

function nextStagePatchEntry(job) {
  if( job.partialIterator ) return job.partialIterator.next();
  while( job.partialBatchIndex < job.partialBatches.length ) {
    const batch = job.partialBatches[job.partialBatchIndex] || [];
    if( job.partialEntryIndex < batch.length )
      return { value: batch[job.partialEntryIndex++], done: false };
    job.partialBatchIndex++;
    job.partialEntryIndex = 0;
  }
  return { value: undefined, done: true };
}

function scanStageBox(job) {
  let box;
  let index;
  let patchEntry = null;
  let projectionEntry = null;
  if( job.partial ) {
    const next = nextStagePatchEntry(job);
    if( next.done ) {
      job.phase = 'done';
      return;
    }
    index = job.index++;
    patchEntry = next.value;
    box = patchEntry?.box;
  } else {
    const length = job.entries?.length ?? job.boxes.length;
    if( job.index >= length ) {
      job.phase = 'commit';
      job.index = 0;
      return;
    }
    index = job.index++;
    projectionEntry = job.entries?.[index] || null;
    box = projectionEntry?.box || job.boxes[index];
  }
  if( !box ) return;
  const role = roleOf(box.type);
  /* Runtime hit testing owns empty cells; normal paint does not need their DOM. */
  if( box.emitted === false || box.effectiveHidden || box.culled ||
      !job.wire && role === 'graphic' && !(box.props.sprite >= 0) ) return;
  const modelSurface = role === 'model'
    ? modelRenderSurface(box, job.width, job.height) : null;
  /* Worker keys intentionally omit the inspector's interface prefix. Preserve
     that committed logical key across full and partial draws so recreating a
     dynamic VM ref in the same parent/sub-id slot replaces, rather than
     duplicates, its existing DOM owner. Static/no-runtime previews retain the
     deterministic box-derived fallback. */
  const key = stageBoxKey(
    job.iface, box, index, patchEntry?.key || projectionEntry?.key || null);
  /* The worker sends only changed entries in a partial patch. Its monotonic
     runtime version is therefore a complete invalidation token and avoids a
     second JSON serialization of every changed widget on the UI thread. */
  const signature = job.partial
    ? job.iface.runtimeVersion
    : stageBoxSignature(box, job.iface, role, modelSurface);
  const previous = renderedStageBoxes.get(key);
  const item = { key, box, role, modelSurface, signature, previous };
  if( job.partial ) commitStageItem(job, item);
  else {
    job.desired.push(item);
    job.desiredKeys.add(key);
  }
}

function commitStageBox(job) {
  if( job.index >= job.desired.length ) {
    if( job.partial ) job.phase = 'done';
    else {
      job.phase = 'cleanup';
      job.cleanupIterator = renderedStageBoxes.entries();
    }
    return;
  }
  const item = job.desired[job.index++];
  commitStageItem(job, item);
}

function commitStageItem(job, item) {
  let element = item.previous?.element;
  if( !element || element.parentElement !== job.stage ||
      item.previous.signature !== item.signature ) {
    const replacementAnchor = job.partial ? element?.nextElementSibling : null;
    if( element ) {
      if( stageBoxesByName.get(element.dataset.name) === element )
        stageBoxesByName.delete(element.dataset.name);
      retireStageElement(element);
    }
    element = createStageBox(item.box, job.iface, item.role, item.modelSurface);
    job.stage.insertBefore(element,
      job.partial && replacementAnchor?.parentElement === job.stage
        ? replacementAnchor : null);
    const token = ++modelEpoch;
    element.__paintToken = token;
    element.__stageKey = item.key;
    renderedStageBoxes.set(item.key, { element, signature: item.signature });
    const isCurrent = () => element.__paintToken === token &&
      renderedStageBoxes.get(item.key)?.element === element &&
      element.parentElement === job.stage;
    if( item.role === 'text' )
      paintCacheText(element, item.box, job.iface, isCurrent).catch(() => false);
    if( item.role === 'model' ) {
      element.__modelOwner = item.key;
      paintModel(element, item.box, job.iface, isCurrent,
        item.modelSurface, item.key, token);
    }
  } else renderedStageBoxes.set(item.key, { element, signature: item.signature });
  item.element = element;
  element.setAttribute('aria-label',
    item.box.name + ', ' + item.box.kind + ', file ' + item.box.fileId);
  element.dataset.name = item.box.name;
  stageBoxesByName.set(item.box.name, element);
  if( outlinedStageName === item.box.name ) element.classList.add('outline');
}

function cleanupStageBox(job) {
  const next = job.cleanupIterator.next();
  if( next.done ) {
    job.phase = 'order';
    job.index = 0;
    job.cursor = job.stage.firstElementChild;
    return;
  }
  const [key, previous] = next.value;
  if( job.desiredKeys.has(key) ) return;
  renderedStageBoxes.delete(key);
  if( stageBoxesByName.get(previous.element.dataset.name) === previous.element )
    stageBoxesByName.delete(previous.element.dataset.name);
  retireStageElement(previous.element);
}

function orderStageBox(job) {
  if( job.index >= job.desired.length ) {
    job.phase = 'done';
    return;
  }
  const element = job.desired[job.index++].element;
    if( element === job.cursor ) job.cursor = job.cursor.nextElementSibling;
    else job.stage.insertBefore(element, job.cursor || null);
}

function retireStageElement(element) {
  element.__paintToken = 0;
  if( element.__modelTimer ) clearTimeout(element.__modelTimer);
  if( element.__modelOwner ) modelRenderController?.cancel(element.__modelOwner);
  element.remove();
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
        if( props.tiled ) {
          /* Native-sized CSS tiling remains O(1) on the input thread. A canvas
             createPattern/fillRect callback scaled with widget area and could
             become a surprise long task after the sliced reconcile finished. */
          element.style.backgroundImage = 'url("' + url + '")';
          element.style.backgroundRepeat = 'repeat';
          element.style.imageRendering = 'pixelated';
        } else {
          const image = new Image();
          image.onerror = () => {
            element.classList.add('unknown');
            element.textContent = 'sprite ' + props.sprite;
          };
          image.alt = '';
          element.appendChild(image);
          image.src = url;
        }
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
const modelRenderMetrics = { count: 0, maxEnqueueMs: 0, overBudget: 0 };
globalThis.__cs2domModelRenderMetrics = modelRenderMetrics;
function ensureModelRenderController() {
  if( modelRenderController ) return modelRenderController;
  modelRenderController = createModelRenderController({
    onBudgetViolation: ({ elapsed }) => {
      modelRenderMetrics.overBudget++;
      console.warn('cs2dom model enqueue exceeded 10ms:', elapsed.toFixed(1) + 'ms');
    },
  });
  return modelRenderController;
}

function paintModel(element, box, iface, isCurrent, surface, owner, token) {
  if( box.w <= 0 || box.h <= 0 || !surface || surface.width <= 0 || surface.height <= 0 ) return;
  const marker = element.querySelector('.model-marker');
  const source = box.presentation?.source || {
    kind: [327, 328].includes(box.props.clientCode) && box.props.model < 0
      ? 'playerSelf' : 'model',
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
  /* Window and worker performance clocks are not required to share an origin.
     Animation phase crosses the boundary as an epoch timestamp instead. */
  const startedAt = Date.now();

  const renderFrame = () => {
    if( !isCurrent() ) return;
    let ticket;
    try {
      ticket = ensureModelRenderController().render({
        owner, token, modelUrl: url,
        animationUrl: sequence >= 0 ? '/model/seq/' + sequence + '.anim' : null,
        startedAt,
        width: surface.width, height: surface.height,
        widgetX: surface.widgetX, widgetY: surface.widgetY,
        widgetWidth: surface.widgetWidth, widgetHeight: surface.widgetHeight,
        zoom: box.props.zoom, xAngle: box.props.xAngle,
        yAngle: box.props.yAngle, zAngle: box.props.zAngle,
        xOffset: box.props.xOffset, yOffset: box.props.yOffset,
        orthographic: box.presentation?.orthographic ?? Boolean(box.props.orthographic),
        fixedZoom: box.presentation?.fixedZoom ?? Boolean(box.props.fixedZoom),
        composed: Boolean(source.composed),
        /* The worker decides whether OffscreenCanvas/ImageBitmap is available.
           A bitmap still avoids a multi-megabyte structured clone when this
           browser lacks bitmaprenderer: the 2D context can draw it directly. */
        preferBitmap: true,
        fallbackMaxDimension: 512,
      });
      modelRenderMetrics.count++;
      modelRenderMetrics.maxEnqueueMs = Math.max(
        modelRenderMetrics.maxEnqueueMs, ticket.enqueueMs);
    } catch( error ) {
      if( marker ) marker.textContent = 'model unavailable';
      element.dataset.error = error.message;
      return;
    }
    ticket.completion.then((frame) => {
      if( frame.stale || !isCurrent() ) {
        frame.bitmap?.close?.();
        return;
      }
      let canvas = element.querySelector('canvas');
      if( !canvas ) {
        canvas = document.createElement('canvas');
        element.prepend(canvas);
      }
      if( canvas.width !== frame.width ) canvas.width = frame.width;
      if( canvas.height !== frame.height ) canvas.height = frame.height;
      paintModelFrame(canvas, frame);
      element.classList.add('ready');
      element.removeAttribute('data-error');
      if( frame.wait > 0 && isCurrent() )
        element.__modelTimer = setTimeout(renderFrame, frame.wait);
    }, (error) => {
      if( isCurrent() ) {
        if( marker ) marker.textContent = 'model unavailable';
        element.dataset.error = error.message;
      }
    });
  };
  renderFrame();
}

function paintModelFrame(canvas, frame) {
  if( frame.bitmap ) {
    let context = null;
    if( canvas.__modelContextKind !== '2d' ) {
      try { context = canvas.getContext('bitmaprenderer'); }
      catch { context = null; }
      if( context ) canvas.__modelContextKind = 'bitmaprenderer';
    }
    if( context || canvas.__modelContextKind === 'bitmaprenderer' ) {
      (context || canvas.getContext('bitmaprenderer')).transferFromImageBitmap(frame.bitmap);
      return;
    }
    const twoD = canvas.getContext('2d');
    canvas.__modelContextKind = '2d';
    try { twoD.drawImage(frame.bitmap, 0, 0); }
    finally { frame.bitmap.close?.(); }
    return;
  }
  /* RGBA is only emitted by workers without transferable bitmap support and
     is capped at 512px on its largest axis before it reaches this task. */
  const context = canvas.getContext('2d');
  if( !context ) throw new Error('model canvas cannot accept RGBA fallback');
  canvas.__modelContextKind = '2d';
  const rgba = new Uint8ClampedArray(frame.rgba);
  context.putImageData(new ImageData(rgba, frame.width, frame.height), 0, 0);
}

function colour(value) {
  const rgb = (value | 0) & 0xffffff;
  return '#' + rgb.toString(16).padStart(6, '0');
}

const TREE_SLICE_BUDGET_MS = 4;
const treeSliceMetrics = {
  count: 0, maxMs: 0, overBudget: 0, completed: 0,
  maxSlices: 0, maxCompletionMs: 0,
};
globalThis.__cs2domTreeSliceMetrics = treeSliceMetrics;

/*
 * Bankmain's inspector contains ~1,700 rows. Reconciling them in one idle
 * callback still blocks a pointer event that arrives during that callback.
 * Keep the existing inspector usable while keyed rows converge in <=4ms
 * slices. content-visibility prevents thousands of off-screen rows from
 * becoming one deferred style/layout spike.
 */
function drawTree(iface) {
  const tree = $('tree');
  const boxes = iface.treeBoxes?.length ? iface.treeBoxes : (iface.boxes || []);
  treeDrawJob = {
    iface, tree, boxes, desiredKeys: new Set(),
    depth: new Map(), index: 0, cursor: tree.firstElementChild,
    cleanupIterator: null, cleanupDone: false,
    slices: 0, startedAt: performance.now(),
  };
  tree.setAttribute('aria-busy', 'true');
  scheduleTreeSlice(treeDrawJob);
}

function scheduleTreeSlice(job) {
  scheduleCooperativeTask(() => runTreeSlice(job));
}

function runTreeSlice(job, deadline) {
  if( job !== treeDrawJob ) return;
  const startedAt = performance.now();
  job.slices++;
  const hasTime = () => performance.now() - startedAt < TREE_SLICE_BUDGET_MS &&
    (!deadline || deadline.didTimeout || deadline.timeRemaining() > 1);

  do {
    if( job.index < job.boxes.length ) reconcileTreeRow(job, job.index++);
    else {
      if( !job.cleanupIterator ) job.cleanupIterator = renderedTreeRows.entries();
      const next = job.cleanupIterator.next();
      if( next.done ) { job.cleanupDone = true; break; }
      if( !job.desiredKeys.has(next.value[0]) ) {
        next.value[1].row.remove();
        renderedTreeRows.delete(next.value[0]);
      }
    }
  } while( hasTime() );

  const pending = job.index < job.boxes.length || !job.cleanupDone;
  if( !pending ) {
    job.tree.removeAttribute('aria-busy');
    treeSliceMetrics.completed++;
    treeSliceMetrics.maxSlices = Math.max(treeSliceMetrics.maxSlices, job.slices);
    treeSliceMetrics.maxCompletionMs = Math.max(
      treeSliceMetrics.maxCompletionMs, performance.now() - job.startedAt);
    treeDrawJob = null;
  }

  const elapsed = performance.now() - startedAt;
  treeSliceMetrics.count++;
  treeSliceMetrics.maxMs = Math.max(treeSliceMetrics.maxMs, elapsed);
  if( elapsed >= 10 ) treeSliceMetrics.overBudget++;
  if( pending ) scheduleTreeSlice(job);
}

function reconcileTreeRow(job, index) {
  const box = job.boxes[index];
  if( !box ) return;
  const linkedLevel = box.layer === null ? 0 : (job.depth.get(box.layer) ?? 0) + 1;
  const level = Number.isInteger(box.depth) ? box.depth : linkedLevel;
  job.depth.set(box.fileId, level);

  const bound = box.dynamic.length
    ? ' <span class="bound">◆ ' + box.dynamic.join(' ') + '</span>' : '';
  const events = box.events.length
    ? ' <span class="rowlabel">' + box.events.join(' ') + '</span>' : '';
  const nativeDynamic = box.native && box.native.dynamic
    ? ' <span class="bound">child ' + box.native.childIndex + '</span>' : '';
  const flags = [
    box.effectiveHidden ? 'hidden' : '',
    box.culled ? 'culled' : '',
    box.emitted === false && !box.effectiveHidden && !box.culled ? 'not walked' : '',
  ].filter(Boolean);
  const visibility = flags.length
    ? ' <span class="rowlabel">[' + flags.join(', ') + ']</span>' : '';
  const html = '&nbsp;'.repeat(level * 2) + box.fileId + ' ' + box.name +
    ' <span class="rowlabel">' + box.kind.toLowerCase() + ' ' +
    box.w + '×' + box.h + '</span>' + nativeDynamic + bound + events + visibility;
  const key = stageBoxKey(job.iface, box, index);
  const previous = renderedTreeRows.get(key);
  const reusable = previous?.row?.parentElement === job.tree;
  const row = reusable ? previous.row : document.createElement('div');
  if( !reusable || previous.html !== html ) row.innerHTML = html;
  row.dataset.boxName = box.name;
  renderedTreeRows.set(key, { row, html });
  job.desiredKeys.add(key);

  if( row === job.cursor ) job.cursor = job.cursor.nextElementSibling;
  else job.tree.insertBefore(row, job.cursor);
}

function controlsRenderKey(iface) {
  return chosen + '|' + iface.interfaceId + '|' + iface.source;
}

function drawControls(iface, force = false) {
  const controls = $('controls');
  const inputs = iface.inputs || [];
  const nextKey = inputs;
  const nextScope = controlsRenderKey(iface);
  $('stateactions').hidden = !inputs.length;
  if( !force && nextKey === renderedControlsKey && nextScope === renderedControlsScope ) return;
  if( !force && controlDrawJob?.key === nextKey && controlDrawJob.scope === nextScope ) return;
  const root = document.createElement('div');
  root.className = 'control-set';
  root.setAttribute('aria-busy', 'true');
  const note = (iface.unmodelled || []).length ? document.createElement('div') : null;
  if( note ) {
    note.className = 'unmodelled';
    const heading = document.createElement('b');
    heading.textContent = 'shown as 0';
    note.appendChild(heading);
    root.appendChild(note);
  }
  const job = controlDrawJob = {
    controls, root, note, iface, key: nextKey, scope: nextScope,
    unmodelledIndex: 0, inputIndex: 0, phase: 'unmodelled', cleanupCursor: null,
    slices: 0, startedAt: performance.now(),
  };
  scheduleCooperativeTask(() => runControlSlice(job));
}

const controlSliceMetrics = {
  count: 0, maxMs: 0, overBudget: 0, completed: 0,
  maxSlices: 0, maxCompletionMs: 0,
};
globalThis.__cs2domControlSliceMetrics = controlSliceMetrics;

function runControlSlice(job) {
  if( job !== controlDrawJob ) return;
  const startedAt = performance.now();
  job.slices = (job.slices || 0) + 1;
  job.startedAt ??= startedAt;
  do {
    if( job.phase === 'unmodelled' ) appendUnmodelledControl(job);
    else if( job.phase === 'inputs' ) appendInputControl(job);
    else if( job.phase === 'commit' ) commitControlRoot(job);
    else if( job.phase === 'cleanup' ) cleanupPreviousControlRoot(job);
    else break;
  } while( job.phase !== 'done' && performance.now() - startedAt < 4 );
  const elapsed = performance.now() - startedAt;
  controlSliceMetrics.count++;
  controlSliceMetrics.maxMs = Math.max(controlSliceMetrics.maxMs, elapsed);
  if( elapsed >= 10 ) controlSliceMetrics.overBudget++;
  if( job.phase === 'done' ) {
    job.root.removeAttribute('aria-busy');
    renderedControlsKey = job.key;
    renderedControlsScope = job.scope;
    controlDrawJob = null;
    controlSliceMetrics.completed++;
    controlSliceMetrics.maxSlices = Math.max(controlSliceMetrics.maxSlices, job.slices);
    controlSliceMetrics.maxCompletionMs = Math.max(
      controlSliceMetrics.maxCompletionMs, performance.now() - job.startedAt);
  } else scheduleCooperativeTask(() => runControlSlice(job));
}

function appendUnmodelledControl(job) {
  const values = job.iface.unmodelled || [];
  if( job.unmodelledIndex < values.length ) {
    const row = document.createElement('div');
    row.textContent = values[job.unmodelledIndex++];
    job.note.appendChild(row);
  } else job.phase = 'inputs';
}

function appendInputControl(job) {
  const inputs = job.iface.inputs || [];
  if( job.inputIndex < inputs.length ) {
    const input = inputs[job.inputIndex++];
    job.root.appendChild(
      input.control.kind === 'inventory' ? inventoryControl(input,
        () => job === controlDrawJob || job.root.parentElement === job.controls)
      : input.control.kind === 'text' ? textControl(input)
      : sliderControl(input));
    return;
  }
  if( inputs.length === 0 ) {
    const empty = document.createElement('div');
    empty.className = 'rowlabel';
    empty.textContent = job.iface.source !== 'authored'
      ? 'This is the static cache record. Scripts referenced by its hooks are loaded ' +
        'beside the decompiled TSX in Cache records.'
      : 'Nothing here reads host state — every prop is fixed at build time, so this ' +
        'interface needs no scripts at all.';
    job.root.appendChild(empty);
  }
  job.phase = 'commit';
}

function commitControlRoot(job) {
  job.controls.appendChild(job.root);
  job.cleanupCursor = job.controls.firstElementChild;
  job.phase = 'cleanup';
}

function cleanupPreviousControlRoot(job) {
  const current = job.cleanupCursor;
  if( !current || current === job.root ) {
    job.phase = 'done';
    return;
  }
  job.cleanupCursor = current.nextElementSibling;
  current.remove();
}

function ensure(key, fallback) {
  return key in draftState ? draftState[key] : fallback;
}

function setStateDirty(dirty, cleanLabel = 'No pending changes') {
  stateDirty = dirty;
  $('save-state').disabled = !dirty;
  $('revert-state').disabled = !dirty;
  const note = $('state-note');
  note.textContent = dirty ? 'Unsaved changes' : cleanLabel;
  note.classList.toggle('dirty', dirty);
}

function saveStateDraft() {
  if( stateCopyJob ) return;
  $('controls').inert = true;
  $('save-state').disabled = true;
  $('revert-state').disabled = true;
  $('state-note').textContent = 'Saving…';
  const job = stateCopyJob = {
    next: {}, phase: 'state', stateIterator: null, draftIterator: null,
  };
  scheduleCooperativeTask(() => runStateCopySlice(job));
}

function runStateCopySlice(job) {
  if( job !== stateCopyJob ) return;
  job.stateIterator ||= ownEntries(state);
  job.draftIterator ||= ownEntries(draftState);
  const startedAt = performance.now();
  do {
    const iterator = job.phase === 'state' ? job.stateIterator : job.draftIterator;
    const next = iterator.next();
    if( next.done ) {
      if( job.phase === 'state' ) { job.phase = 'draft'; continue; }
      state = job.next;
      draftState = {};
      stateCopyJob = null;
      $('controls').inert = false;
      setStateDirty(false, 'State saved');
      clearRuntimeInteractionChrome();
      refresh();
      return;
    }
    job.next[next.value[0]] = next.value[1];
  } while( performance.now() - startedAt < 4 );
  scheduleCooperativeTask(() => runStateCopySlice(job));
}

function* ownEntries(object) {
  for( const key in object ) if( Object.hasOwn(object, key) ) yield [key, object[key]];
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

  slider.oninput = budgetedInputHandler('host-state-range', () => {
    draftState[input.key] = Number(slider.value);
    readout.textContent = slider.value;
    slider.removeAttribute('title');
    setStateDirty(true);
  });
  return wrap;
}

function textControl(input) {
  const value = ensure(input.key, input.initial ?? '');
  const field = document.createElement('input');
  field.type = 'text';
  field.value = String(value);
  field.style.width = '100%';

  const { wrap } = controlShell(input, field);
  field.oninput = budgetedInputHandler('host-state-text', () => {
    draftState[input.key] = field.value;
    field.placeholder = '';
    setStateDirty(true);
  });
  return wrap;
}

/*
 * An inventory is not one number, so it does not get a slider. The rows are
 * (item, count) pairs under invobj:<id>, which is the shape inv_getnum and
 * inv_total ask about in src/host.js.
 */
function inventoryControl(input, isCurrent = () => true) {
  const key = 'invobj:' + input.id;
  let contents = ensure(key, {});
  let editableContents = null;

  const editContents = (edit) => {
    /* An untouched control is not committed state. Once the user edits it, clone
       first so the outgoing request gets a new, explicit inventory value. */
    if( draftState[key] !== contents ) contents = draftState[key] = editableContents;
    edit(contents);
    setStateDirty(true);
  };

  const body = document.createElement('div');
  body.className = 'inv';

  let redrawEpoch = 0;
  const redraw = () => {
    const epoch = ++redrawEpoch;
    editableContents = {};
    body.inert = true;
    body.setAttribute('aria-busy', 'true');
    const iterator = ownEntries(contents);
    const pump = () => {
      if( epoch !== redrawEpoch || !isCurrent() ) return;
      const startedAt = performance.now();
      do {
        if( body.firstElementChild ) {
          body.firstElementChild.remove();
          continue;
        }
        const next = iterator.next();
        if( !next.done ) {
          editableContents[next.value[0]] = next.value[1];
          appendRow(next.value[0], next.value[1]);
          continue;
        }
        appendAdd();
        body.inert = false;
        body.removeAttribute('aria-busy');
        return;
      } while( performance.now() - startedAt < 4 );
      scheduleCooperativeTask(pump);
    };
    scheduleCooperativeTask(pump);
  };

  const appendRow = (obj, count) => {
    const row = document.createElement('div');
    row.className = 'invrow';
    row.innerHTML = '<span>obj ' + obj + '</span>';
    const amount = document.createElement('input');
    amount.type = 'number'; amount.value = String(count); amount.min = '0';
    amount.oninput = budgetedInputHandler('host-state-inventory', () => {
      editContents((next) => { next[obj] = Number(amount.value) || 0; });
    });
    const drop = document.createElement('button');
    drop.textContent = '×';
    drop.onclick = budgetedInputHandler('host-state-inventory-drop', () => {
      editContents((next) => { delete next[obj]; });
      row.remove();
    });
    row.appendChild(amount);
    row.appendChild(drop);
    body.appendChild(row);
  };

  const appendAdd = () => {
    const add = document.createElement('button');
    add.textContent = '+ item';
    add.onclick = () => {
      const obj = prompt('item id');
      if( obj === null ) return;
      const id = Number(obj) || 0;
      editContents((next) => { next[id] = 1; });
      appendRow(id, 1);
      body.appendChild(add);
    };
    body.appendChild(add);
  };
  redraw();

  const { wrap } = controlShell(input, body);
  return wrap;
}

function drawRecords(iface) {
  const records = $('records');
  const root = document.createElement('div');
  root.className = 'record-set';
  root.setAttribute('aria-busy', 'true');
  const job = recordDrawJob = {
    epoch: ++recordDrawEpoch, records, root, iface,
    phase: 'warnings', warningGroup: 0, warningIndex: 0,
    cleanupCursor: null, sourceIndex: 0, openedFirst: false,
  };
  runRecordSlice(job);
}

const RECORD_SLICE_BUDGET_MS = 4;
const RECORD_TEXT_CHUNK = 16384;
const recordSliceMetrics = {
  count: 0, maxMs: 0, overBudget: 0, completed: 0,
  maxSlices: 0, maxCompletionMs: 0,
};
globalThis.__cs2domRecordSliceMetrics = recordSliceMetrics;

function runRecordSlice(job) {
  if( job !== recordDrawJob || job.epoch !== recordDrawEpoch ) return;
  const startedAt = performance.now();
  job.slices = (job.slices || 0) + 1;
  job.startedAt ??= startedAt;
  do {
    if( job.phase === 'warnings' ) appendRecordWarning(job);
    else if( job.phase === 'commit' ) commitRecordRoot(job);
    else if( job.phase === 'cleanup' ) cleanupPreviousRecordRoot(job);
    else if( job.phase === 'sources' ) appendRecordSource(job);
    else break;
  } while( job.phase !== 'done' &&
    performance.now() - startedAt < RECORD_SLICE_BUDGET_MS );
  const elapsed = performance.now() - startedAt;
  recordSliceMetrics.count++;
  recordSliceMetrics.maxMs = Math.max(recordSliceMetrics.maxMs, elapsed);
  if( elapsed >= 10 ) recordSliceMetrics.overBudget++;
  if( job.phase === 'done' ) {
    job.root.removeAttribute('aria-busy');
    recordSliceMetrics.completed++;
    recordSliceMetrics.maxSlices = Math.max(recordSliceMetrics.maxSlices, job.slices);
    recordSliceMetrics.maxCompletionMs = Math.max(
      recordSliceMetrics.maxCompletionMs, performance.now() - job.startedAt);
    recordDrawJob = null;
  } else scheduleCooperativeTask(() => runRecordSlice(job));
}

function appendRecordWarning(job) {
  const groups = [data?.warnings || [], runtimeWarnings];
  while( job.warningGroup < groups.length &&
      job.warningIndex >= groups[job.warningGroup].length ) {
    job.warningGroup++;
    job.warningIndex = 0;
  }
  if( job.warningGroup >= groups.length ) {
    job.phase = 'commit';
    return;
  }
  const note = document.createElement('div');
  note.className = 'warn';
  note.textContent = '⚠ ' + groups[job.warningGroup][job.warningIndex++];
  job.root.appendChild(note);
}

function commitRecordRoot(job) {
  job.records.appendChild(job.root);
  activeRecordRoot = job.root;
  job.cleanupCursor = job.records.firstElementChild;
  job.phase = 'cleanup';
}

function cleanupPreviousRecordRoot(job) {
  const current = job.cleanupCursor;
  if( !current || current === job.root ) {
    job.phase = 'sources';
    return;
  }
  job.cleanupCursor = current.nextElementSibling;
  current.remove();
}

function recordSourceAt(job, index) {
  const iface = job.iface;
  let offset = 0;
  if( iface.reactSource ) {
    if( index === offset ) return [
      'decompiled/' + iface.name + '.tsx (read-only view)', iface.reactSource,
    ];
    offset++;
  }
  if( index === offset++ ) return ['interfaces/' + iface.name + '.if', iface.interfaceText];
  if( index === offset++ ) return ['interfaces/' + iface.name + '.compack', iface.compackText];
  const script = (iface.scripts || [])[index - offset];
  return script ? ['scripts/' + script.name + '.cs2', script.source] : null;
}

function appendRecordSource(job) {
  const source = recordSourceAt(job, job.sourceIndex++);
  if( !source ) {
    job.phase = 'done';
    return;
  }
  const details = document.createElement('details');
  details.className = 'record';
  const title = document.createElement('summary');
  title.className = 'filename';
  title.textContent = source[0];
  const block = document.createElement('pre');
  details.appendChild(title);
  details.appendChild(block);
  details.__recordText = String(source[1] ?? '');
  details.__recordOffset = 0;
  details.ontoggle = budgetedInputHandler('record-toggle', () => {
    if( details.open ) scheduleRecordText(details);
  });
  job.root.appendChild(details);
  /* Keep the generated React view visible as before, but populate even that
     one source in small text nodes instead of one potentially huge task. */
  if( !job.openedFirst ) {
    job.openedFirst = true;
    details.open = true;
    scheduleRecordText(details);
  }
}

function scheduleRecordText(details) {
  if( details.__recordLoaded || details.__recordQueued ) return;
  details.__recordQueued = true;
  details.setAttribute('aria-busy', 'true');
  details.__recordNext = null;
  if( recordTextTail ) recordTextTail.__recordNext = details;
  else recordTextHead = details;
  recordTextTail = details;
  if( recordTextFrame ) return;
  recordTextFrame = 1;
  scheduleCooperativeTask(runRecordTextSlice);
}

function runRecordTextSlice() {
  recordTextFrame = 0;
  const startedAt = performance.now();
  while( recordTextHead &&
      performance.now() - startedAt < RECORD_SLICE_BUDGET_MS ) {
    const details = recordTextHead;
    recordTextHead = details.__recordNext;
    details.__recordNext = null;
    if( !recordTextHead ) recordTextTail = null;
    details.__recordQueued = false;
    if( !details.open || details.parentElement !== activeRecordRoot ||
        activeRecordRoot?.parentElement !== $('records') ) continue;
    const text = details.__recordText;
    const offset = details.__recordOffset;
    const chunk = text.slice(offset, offset + RECORD_TEXT_CHUNK);
    if( chunk ) {
      const span = document.createElement('span');
      span.textContent = chunk;
      details.lastElementChild.appendChild(span);
      details.__recordOffset += chunk.length;
    }
    if( details.__recordOffset < text.length ) scheduleRecordText(details);
    else {
      details.__recordLoaded = true;
      details.removeAttribute('aria-busy');
      details.__recordText = '';
    }
  }
  if( recordTextHead && !recordTextFrame ) {
    recordTextFrame = 1;
    scheduleCooperativeTask(runRecordTextSlice);
  }
  const elapsed = performance.now() - startedAt;
  recordSliceMetrics.count++;
  recordSliceMetrics.maxMs = Math.max(recordSliceMetrics.maxMs, elapsed);
  if( elapsed >= 10 ) recordSliceMetrics.overBudget++;
}

function scheduleRecordsDraw(iface) {
  pendingRecordsIface = iface;
  if( recordsDrawFrame ) return;
  recordsDrawFrame = requestAnimationFrame(() => {
    recordsDrawFrame = 0;
    const pending = pendingRecordsIface;
    pendingRecordsIface = null;
    if( pending === currentInterface() ) drawRecords(pending);
  });
}

/* ---- live React host interaction --------------------------------------- */

function currentInterface() {
  return data && data.interfaces && data.interfaces[0];
}

function resetRuntimeInteraction() {
  disposeRuntimeSession(runtimeSession);
  heldButtons.clear();
  interactionEpoch++;
  runtimeCycle = 0;
  closeOpMenu();
  runtimeSession = null;
  pendingRuntimeSession = null;
  runtimeMode = 'unavailable';
}

function clearRuntimeInteractionChrome() {
  heldButtons.clear();
  interactionEpoch++;
  runtimeCycle = 0;
  closeOpMenu();
}

function disposeRuntimeSession(session) {
  if( !session?.controller || session.generation !== session.controller.session ) return;
  session.controller.dispose();
}

function stagePoint(event) {
  const rect = stageRectCache;
  const iface = currentInterface();
  const width = iface?.viewport?.width || 0;
  const height = iface?.viewport?.height || 0;
  /* getBoundingClientRect() in a pointer handler can synchronously lay out the
     entire inspector. Geometry is sampled on an animation frame instead. */
  const x = rect?.width ? Math.floor((event.clientX - rect.left) * width / rect.width) : -1;
  const y = rect?.height ? Math.floor((event.clientY - rect.top) * height / rect.height) : -1;
  return { x, y };
}

function scheduleStageRectRead() {
  if( stageRectFrame ) return;
  stageRectFrame = requestAnimationFrame(() => {
    stageRectFrame = 0;
    const rect = $('stage').getBoundingClientRect();
    stageRectCache = {
      left: rect.left, top: rect.top, width: rect.width, height: rect.height,
    };
  });
}

function noteRuntimeError(error) {
  const message = error?.message || String(error);
  if( !runtimeWarnings.includes(message) ) runtimeWarnings.push(message);
  $('status').textContent = 'script interaction stopped — ' + message;
  $('status').className = 'status bad';
  const iface = currentInterface();
  if( iface ) scheduleRecordsDraw(iface);
}

function scheduleRuntimeRender(iface, runtime, refreshTree = true, stagePatch = null) {
  const incomingBatches = stagePatch?.upsertBatches ||
    (stagePatch?.upsert?.length ? [stagePatch.upsert] : []);
  const upsertCount = stagePatch?.upsertCount ??
    incomingBatches.reduce((total, batch) => total + (batch?.length || 0), 0);
  const removeCount = stagePatch?.removeCount ?? stagePatch?.remove?.length ?? 0;
  const full = !stagePatch || stagePatch.reset || stagePatch.orderChanged ||
    removeCount || !upsertCount;
  if( !pendingRuntimeRender || pendingRuntimeRender.iface !== iface ||
      pendingRuntimeRender.runtime !== runtime ) {
    pendingRuntimeRender = {
      iface, runtime, refreshTree, full, dedupePartial: false,
      upsertBatches: full ? [] : incomingBatches.slice(),
    };
  } else {
    pendingRuntimeRender.refreshTree ||= refreshTree;
    pendingRuntimeRender.full ||= full;
    if( pendingRuntimeRender.full ) pendingRuntimeRender.upsertBatches.length = 0;
    else if( upsertCount ) {
      pendingRuntimeRender.dedupePartial = true;
      pendingRuntimeRender.upsertBatches.push(...incomingBatches);
    }
  }
  if( runtimeRenderFrame ) return;
  runtimeRenderFrame = requestAnimationFrame(() => {
    runtimeRenderFrame = 0;
    const pending = pendingRuntimeRender;
    pendingRuntimeRender = null;
    if( !pending || pending.runtime !== runtimeController || pending.iface !== currentInterface() ) return;
    drawStage(pending.iface,
      pending.full ? {} : {
        upsertBatches: pending.upsertBatches,
        dedupePartial: pending.dedupePartial,
      });
    if( pending.refreshTree ) scheduleRuntimeTreeRefresh(runtimeSession);
  });
}

function scheduleTreeDraw(iface) {
  if( iface === currentInterface() ) drawTree(iface);
}

function dispatchRuntime(input) {
  const iface = currentInterface();
  const session = runtimeSession;
  if( !iface || !session?.controller || session.controller.readyState !== 'ready' ) return null;
  try {
    const ticket = session.controller.dispatch(input);
    observeRuntimeInteraction('enqueue', ticket.enqueueMs);
    ticket.completion.catch((error) => {
      if( session === runtimeSession && session.generation === session.controller.session )
        noteRuntimeError(error);
    });
    return ticket;
  } catch( error ) {
    noteRuntimeError(error);
    return null;
  }
}

let runtimeTreeRefresh = 0;
function scheduleRuntimeTreeRefresh(session) {
  if( !session?.installed || session !== runtimeSession || runtimeTreeRefresh ) return;
  /* The inspector is diagnostic, not part of hit testing. Stream at most one
     full tree per quiet window instead of 1,700 boxes after every 20ms tick. */
  runtimeTreeRefresh = setTimeout(() => {
    runtimeTreeRefresh = 0;
    if( session === runtimeSession && session.controller.readyState === 'ready' )
      requestRuntimeTree(session);
  }, 150);
}

function pointerButton(button) {
  return button >= 0 && button <= 2 ? button : null;
}

function liveOpMenu() {
  if( opMenuElement?.parentElement === $('stage') ) return opMenuElement;
  opMenuElement = null;
  return null;
}

function closeOpMenu() {
  const menu = liveOpMenu();
  opMenuElement = null;
  menu?.remove();
}

function opMenuOutside(menu, event, margin = 10) {
  /* Never force style/layout from a pointer handler. The popup records its
     client-space bounds on the next frame; until then it conservatively owns
     input for one frame. */
  const rect = menu.__clientRect;
  if( !rect ) return false;
  return event.clientX < rect.left - margin || event.clientX >= rect.right + margin ||
    event.clientY < rect.top - margin || event.clientY >= rect.bottom + margin;
}

function dismissOpMenu() {
  interactionEpoch++;
  closeOpMenu();
  if( runtimeController?.readyState === 'ready' ) dispatchRuntime({ type: 'menu_close' });
}

function openOpMenu(menu, point) {
  closeOpMenu();
  if( !Array.isArray(menu) || menu.length === 0 ) return;
  const shell = document.createElement('div');
  shell.className = 'opmenu';
  const viewport = currentInterface()?.viewport || { width: 0, height: 0 };
  shell.style.left = Math.max(0, Math.min(viewport.width - 150, point.x)) + 'px';
  shell.style.top = Math.max(0, Math.min(viewport.height - 24, point.y)) + 'px';
  /* The native minimenu owns its press and selects on mouse-down. Preventing
     the browser focus default is equally important here: focusing a child
     button blurs #stage, whose focus-lost handler closes the menu before the
     subsequent click can run. */
  shell.onpointerdown = budgetedInputHandler('menu-pointerdown', (event) => {
    event.preventDefault();
    event.stopPropagation();
  });
  for( const item of menu ) {
    const button = document.createElement('button');
    button.type = 'button';
    button.textContent = item.text || ('Option ' + item.opIndex);
    let selected = false;
    const choose = () => {
      if( selected ) return;
      selected = true;
      closeOpMenu();
      dispatchRuntime({ type: 'op', target: item.component, opIndex: item.opIndex });
    };
    button.onpointerdown = budgetedInputHandler('menu-option-pointerdown', (event) => {
      if( event.button !== 0 && event.button !== 2 ) return;
      event.preventDefault();
      event.stopPropagation();
      choose();
    });
    /* detail=0 is keyboard activation. Pointer activation already selected
       on its native mouse-down edge, and the guard makes either path safe. */
    button.onclick = budgetedInputHandler('menu-option-click', (event) => {
      event.preventDefault();
      choose();
    });
    button.oncontextmenu = (event) => event.preventDefault();
    shell.appendChild(button);
  }
  opMenuElement = shell;
  $('stage').appendChild(shell);
  requestAnimationFrame(() => {
    if( shell.parentElement !== $('stage') ) return;
    const rect = shell.getBoundingClientRect();
    shell.__clientRect = {
      left: rect.left, top: rect.top, right: rect.right, bottom: rect.bottom,
    };
  });
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

function outlineStageBox(name) {
  if( outlinedStageName ) stageBoxesByName.get(outlinedStageName)?.classList.remove('outline');
  outlinedStageName = name;
  if( name ) stageBoxesByName.get(name)?.classList.add('outline');
}

const INPUT_HANDLER_BUDGET_MS = 10;
const inputHandlerMetrics = Object.create(null);
globalThis.__cs2domInputHandlerMetrics = inputHandlerMetrics;
function budgetedInputHandler(name, handler) {
  return (event) => {
    const startedAt = performance.now();
    try {
      return handler(event);
    } finally {
      const elapsed = performance.now() - startedAt;
      const metric = inputHandlerMetrics[name] ||= { count: 0, maxMs: 0, overBudget: 0 };
      metric.count++;
      metric.lastMs = elapsed;
      metric.maxMs = Math.max(metric.maxMs, elapsed);
      if( elapsed >= INPUT_HANDLER_BUDGET_MS ) {
        metric.overBudget++;
        if( metric.overBudget === 1 )
          console.warn('cs2dom input handler exceeded 10ms:', name, elapsed.toFixed(1) + 'ms');
      }
    }
  };
}

const treeView = $('tree');
treeView.onmouseover = budgetedInputHandler('tree-mouseover', (event) => {
  const row = event.target.closest('[data-box-name]');
  if( !row || !treeView.contains(row) || row.contains(event.relatedTarget) ) return;
  outlineStageBox(row.dataset.boxName);
});
treeView.onmouseout = budgetedInputHandler('tree-mouseout', (event) => {
  const row = event.target.closest('[data-box-name]');
  if( !row || row.contains(event.relatedTarget) ) return;
  outlineStageBox(null);
});

const stage = $('stage');
if( typeof ResizeObserver === 'function' )
  new ResizeObserver(scheduleStageRectRead).observe(stage);
window.addEventListener('resize', scheduleStageRectRead, { passive: true });
document.addEventListener('scroll', scheduleStageRectRead, { passive: true, capture: true });
stage.onpointermove = budgetedInputHandler('pointermove', (event) => {
  const menu = liveOpMenu();
  if( menu ) {
    /* choose-option owns the pointer and closes only after leaving its 10px
       margin; no hover hooks leak through the popup while it is open. */
    if( opMenuOutside(menu, event) ) dismissOpMenu();
    return;
  }
  dispatchRuntime({ type: 'pointer_move', ...stagePoint(event) });
});
stage.onpointerdown = budgetedInputHandler('pointerdown', (event) => {
  const button = pointerButton(event.button);
  if( button === null || runtimeController?.readyState !== 'ready' ) return;
  event.preventDefault();
  const menu = liveOpMenu();
  if( menu ) {
    if( !opMenuOutside(menu, event) ) return;
    /* An outside left press only dismisses; an outside right press dismisses
       and continues below to reopen at the new point, matching choose-option. */
    dismissOpMenu();
    if( button !== 2 ) {
      stage.focus({ preventScroll: true });
      return;
    }
  }
  const gestureEpoch = ++interactionEpoch;
  stage.focus({ preventScroll: true });
  try { stage.setPointerCapture(event.pointerId); } catch { /* already captured */ }
  heldButtons.add(button);
  const point = stagePoint(event);
  const ticket = dispatchRuntime({ type: 'pointer_down', button, ...point });
  if( button === 2 ) ticket?.completion.then((outcome) => {
    if( gestureEpoch === interactionEpoch &&
        runtimeController?.session === runtimeSession?.generation &&
        runtimeController?.interaction?.menuOpen &&
        outcome.result?.interaction?.menuOpen ) openOpMenu(outcome.result.menu, point);
  }, () => {});
});
stage.onpointerup = budgetedInputHandler('pointerup', (event) => {
  const button = pointerButton(event.button);
  if( button === null || runtimeController?.readyState !== 'ready' ) return;
  event.preventDefault();
  heldButtons.delete(button);
  const menu = liveOpMenu();
  if( menu ) {
    if( opMenuOutside(menu, event) ) dismissOpMenu();
    try { stage.releasePointerCapture(event.pointerId); } catch { /* not captured */ }
    return;
  }
  dispatchRuntime({ type: 'pointer_up', button, ...stagePoint(event) });
  try { stage.releasePointerCapture(event.pointerId); } catch { /* not captured */ }
});
stage.onpointercancel = budgetedInputHandler('pointercancel', () => {
  /* A cancelled browser gesture is not a mouse-up. Synthesizing one can fire
     onRelease (and a deferred draggable click) for a press the OS cancelled. */
  heldButtons.clear();
  interactionEpoch++;
  closeOpMenu();
  dispatchRuntime({ type: 'focus_lost' });
});
stage.onpointerleave = budgetedInputHandler('pointerleave', () => {
  if( liveOpMenu() ) dismissOpMenu();
  else if( heldButtons.size === 0 ) dispatchRuntime({ type: 'pointer_move', x: -1, y: -1 });
});
stage.addEventListener('wheel', budgetedInputHandler('wheel', (event) => {
  if( runtimeController?.readyState !== 'ready' || event.deltaY === 0 ) return;
  event.preventDefault();
  const menu = liveOpMenu();
  if( menu ) {
    if( opMenuOutside(menu, event) ) dismissOpMenu();
    return;
  }
  dispatchRuntime({ type: 'wheel', wheel: -event.deltaY, ...stagePoint(event) });
}), { passive: false });
stage.oncontextmenu = budgetedInputHandler('contextmenu', (event) => {
  if( runtimeController?.readyState === 'ready' ) event.preventDefault();
});
stage.onkeydown = budgetedInputHandler('keydown', (event) => {
  if( runtimeController?.readyState !== 'ready' ) return;
  const keyTyped = osrsKey(event);
  const keyPressed = keyCharacter(event);
  if( keyTyped < 0 && keyPressed < 0 ) return;
  event.preventDefault();
  if( !event.repeat && keyTyped >= 0 )
    dispatchRuntime({ type: 'key_down', keyTyped, keyPressed: Math.max(0, keyPressed) });
  dispatchRuntime({ type: 'key', keyTyped, keyPressed });
});
stage.onkeyup = budgetedInputHandler('keyup', (event) => {
  if( runtimeController?.readyState !== 'ready' ) return;
  const keyTyped = osrsKey(event);
  if( keyTyped < 0 ) return;
  event.preventDefault();
  dispatchRuntime({ type: 'key_up', keyTyped, keyPressed: 0 });
});
stage.onblur = budgetedInputHandler('blur', () => {
  heldButtons.clear();
  interactionEpoch++;
  closeOpMenu();
  dispatchRuntime({ type: 'focus_lost' });
});

function scheduleRuntimeTick() {
  setTimeout(() => {
    if( runtimeController?.readyState === 'ready' && document.visibilityState === 'visible' ) {
      const ticket = dispatchRuntime({ type: 'tick', cycle: ++runtimeCycle });
      if( ticket ) return ticket.completion.then(scheduleRuntimeTick, scheduleRuntimeTick);
    }
    scheduleRuntimeTick();
  }, 20);
}
scheduleRuntimeTick();

$('pick').onfocus = budgetedInputHandler('picker-focus', () => {
  renderPicker('');
  openPicker();
  $('pick').select();
});
$('pick').oninput = budgetedInputHandler('picker-input', () => {
  renderPicker($('pick').value);
  openPicker();
});
$('pick').onkeydown = budgetedInputHandler('picker-keydown', (event) => {
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
});
document.addEventListener('pointerdown', budgetedInputHandler('document-pointerdown', (event) => {
  if( !$('pickmenu').classList.contains('open') ) return;
  if( !$('picker').contains(event.target) ) closePicker();
}));
$('wire').onchange = budgetedInputHandler('wire-change', () => {
  const iface = currentInterface();
  if( iface ) scheduleCooperativeTask(() => drawStage(iface));
});
$('save-state').onclick = budgetedInputHandler('host-state-save', () => {
  saveStateDraft();
});
$('revert-state').onclick = budgetedInputHandler('host-state-revert', () => {
  draftState = {};
  setStateDirty(false, 'Draft discarded');
  const iface = data && data.interfaces && data.interfaces[0];
  if( iface ) scheduleCooperativeTask(() => drawControls(iface, true));
});
$('controls').onkeydown = budgetedInputHandler('controls-keydown', (event) => {
  if( event.key === 'Enter' && (event.metaKey || event.ctrlKey) && stateDirty ) {
    event.preventDefault();
    $('save-state').click();
  }
});
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
    indexCatalog();
  } catch { /* The current picker remains usable while the server rebuilds. */ }
}

new EventSource('/events').onmessage = () => {
  /* Source saves invalidate the live script/tree session, but not the editor shell. Keep the
     picker, Host-state draft, focus and pane scroll positions alive; replace
     only the preview, runtime tree and generated cache/script records. */
  clearRuntimeInteractionChrome();
  refreshCatalogQuietly();
  refresh({ hotReload: true });
};
refresh();
</script>
</body>
</html>`;
}
