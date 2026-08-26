/*
 * The dev page.
 *
 * Three panes, and the split between them is the architecture rather than a
 * layout choice:
 *
 *   PREVIEW   one canvas. There is no element per widget, so a rebuild that
 *             creates three thousand components costs three thousand draw
 *             calls and no DOM at all.
 *
 *   STATE     the host slices a script can read, as controls. A slice nobody
 *             can move is a slice that cannot be tested.
 *
 *   RECORDS   the `.if`, the `.cs2` and the generated JavaScript, side by
 *             side. This pane is the point as much as the preview is — the
 *             records are what actually ships.
 *
 * The chrome is plain DOM built once and mutated by id. It is deliberately not
 * a framework: the page has perhaps forty controls, they change at human
 * cadence, and the one thing that must never happen is a re-render of the
 * chrome disturbing the canvas or the caret in a state field mid-edit.
 *
 * Hot reload replaces the SESSION, not the page. The picker selection, the
 * state drafts and the keyboard focus all live in the chrome and survive.
 */

export function canvasDevPage({ title = 'cs2dom' } = {}) {
    return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${title}</title>
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
  display: flex; align-items: center; gap: 14px; padding: 8px 14px;
  border-bottom: 1px solid var(--line); background: var(--panel);
}
header h1 {
  font: 600 12px var(--mono); margin: 0; letter-spacing: .1em;
  text-transform: uppercase; color: var(--accent);
}
.spacer { flex: 1; }
.metric { font: 11px var(--mono); color: var(--dim); white-space: nowrap; }
.metric b { color: var(--ink); font-weight: 600; }
.metric.bad b { color: var(--bad); }
.metric.good b { color: var(--hot); }
input, select, button {
  font: 12px var(--mono); color: var(--ink); background: #241f19;
  border: 1px solid var(--line); border-radius: 3px; padding: 4px 8px;
}
button { cursor: pointer; }
button:hover { border-color: var(--accent); }
/* The PREVIEW gets the room.
   A 765-wide interface and two fixed panels do not fit in a 800px window,
   and the loser is always the preview -- it is the only column that can
   shrink, so it scales down to a thumbnail and the interface looks wrong
   when it is merely small. The panels collapse instead, by hand or by
   default on a narrow window. */
main { display: grid; grid-template-columns: minmax(360px, 1fr) 280px 340px; overflow: hidden; }
body[data-panels=off] main { grid-template-columns: 1fr; }
body[data-panels=off] #panel-state,
body[data-panels=off] #panel-records { display: none; }
#panels[aria-pressed=false] { color: var(--dim); }
section { overflow: auto; border-left: 1px solid var(--line); }
section:first-child { border-left: 0; }
/*
 * The preview section is a ROWS grid so #stage has a height of its own.
 *
 * Left to size itself, #stage is exactly as tall as the canvas inside it —
 * so measuring it to decide how much the canvas may grow reads back the
 * canvas's current size, and the zoom can never rise above whatever it
 * already is. It was pinned at 1x on every display for that reason.
 */
#panel-preview { display: grid; grid-template-rows: auto 1fr auto auto; overflow: hidden; }
h2 {
  font: 600 10px var(--mono); letter-spacing: .12em; text-transform: uppercase;
  color: var(--dim); margin: 0; padding: 8px 12px;
  border-bottom: 1px solid var(--line); position: sticky; top: 0; background: var(--panel);
}
#stage { display: grid; place-items: center; padding: 16px; min-height: 0; overflow: auto; }
/* The canvas is the whole preview. Pixel art, so no smoothing anywhere in
   the chain — the context disables it too, but a CSS upscale would undo it. */
canvas {
  background: #000; image-rendering: pixelated;
  box-shadow: 0 0 0 1px var(--line), 0 12px 40px rgba(0,0,0,.5);
}
.row { display: flex; align-items: center; gap: 8px; padding: 5px 12px; }
.row label { flex: 1; font: 11px var(--mono); color: var(--dim); }
.row input[type=range] { flex: 2; }
.row input[type=number], .row input[type=text] { width: 88px; }
.tabs { display: flex; gap: 2px; padding: 6px 12px 0; }
.tabs button {
  border-radius: 3px 3px 0 0; border-bottom-color: transparent; color: var(--dim);
}
.tabs button[aria-selected=true] { color: var(--accent); border-color: var(--line); background: var(--panel); }
pre {
  margin: 0; padding: 10px 12px; font: 11px/1.55 var(--mono);
  white-space: pre; overflow-x: auto; color: var(--ink);
}
pre.empty { color: var(--dim); font-style: italic; }
.picker { position: relative; width: min(420px, 40vw); }
#pick { width: 100%; }
.pickmenu {
  display: none; position: absolute; z-index: 100; top: calc(100% + 5px); left: 0; right: 0;
  max-height: min(60vh, 480px); overflow-y: auto; padding: 4px;
  border: 1px solid #413a2e; border-radius: 5px; background: #181510;
  box-shadow: 0 14px 36px rgba(0,0,0,.5);
}
.pickmenu.open { display: block; }
.pickmenu div { padding: 5px 8px; border-radius: 3px; cursor: pointer; font: 12px var(--mono); }
.pickmenu div:hover, .pickmenu div[aria-selected=true] { background: #2b251c; color: var(--accent); }
.pickgroup {
  padding: 7px 8px 3px; color: var(--dim); font: 600 9px var(--mono);
  letter-spacing: .1em; text-transform: uppercase; cursor: default;
}
.pickgroup:hover { background: none; color: var(--dim); }
#log { padding: 6px 12px; font: 11px var(--mono); color: var(--bad); white-space: pre-wrap; }
#status {
  padding: 6px 12px; font: 11px var(--mono); color: var(--dim); white-space: pre-wrap;
}
#status[data-state=busy] { color: var(--accent); }
#status[data-state=bad] { color: var(--bad); }
</style>
</head>
<body>
<header>
  <h1>cs2dom</h1>
  <div class="picker">
    <input id="pick" type="search" placeholder="interface…" autocomplete="off" spellcheck="false">
    <div class="pickmenu" id="pickmenu"></div>
  </div>
  <button id="reload" title="Re-run the interface from scratch">Remount</button>
  <button id="panels" title="Show or hide the side panels">Panels</button>
  <div class="spacer"></div>
  <div class="metric" id="m-zoom">1x</div>
  <div class="metric" id="m-frame">frame <b>—</b></div>
  <div class="metric" id="m-calls">host <b>—</b></div>
  <div class="metric" id="m-nodes">nodes <b>—</b></div>
  <div class="metric" id="m-draws">draws <b>—</b></div>
</header>
<main>
  <section id="panel-preview">
    <h2>Preview</h2>
    <div id="stage"><canvas id="surface" width="765" height="503"></canvas></div>
    <div id="status">select an interface</div>
    <div id="log"></div>
  </section>
  <section id="panel-state">
    <h2>Host state</h2>
    <div id="state"></div>
  </section>
  <section id="panel-records">
    <h2>Records</h2>
    <div class="tabs" id="tabs">
      <button data-pane="if" aria-selected="true">.if</button>
      <button data-pane="cs2" aria-selected="false">.cs2</button>
      <button data-pane="js" aria-selected="false">javascript</button>
    </div>
    <pre id="records" class="empty">select an interface</pre>
  </section>
</main>
<script type="module" src="/dev-client.js"></script>
</body>
</html>`;
}

/**
 * The page's own script.
 *
 * Served separately so it is a real module the browser caches and the debugger
 * can step through, rather than a string inside the document.
 */
export function canvasDevClient() {
    return `/* cs2dom dev page. Chrome only — the runtime lives in browser_runtime.js. */
import { mountInterface } from '/src/browser_runtime.js';
import { ScriptRegistry } from '/src/cs2_driver.js';
import { bakeInterface } from '/src/if_to_tree.js';
import { HostConfig } from '/src/host_config.js';

const $ = (id) => document.getElementById(id);
const canvas = $('surface');
const log = $('log');
const statusLine = $('status');
const zoom = $('m-zoom');

/* Mounting an interface is not instant -- 200-odd scripts are fetched and
 * compiled before the first frame -- and a mount that dies mid-way used to
 * leave the previous picture (or a black canvas) with nothing said. Every
 * stage reports here, so "nothing happened" is always distinguishable from
 * "still working" and from "it failed at stage N". */
function status(text, state = 'busy') {
  statusLine.textContent = text;
  statusLine.dataset.state = state;
}

/* ------------------------------------------------------------------
 * State that survives a remount
 *
 * The picker selection, the host-state drafts and the records tab belong to
 * the PAGE. A source save replaces the session and must not disturb any of
 * them — that is the one hot-reload rule worth keeping from the old runtime.
 * ------------------------------------------------------------------ */
const chrome = {
  selected: null,
  /* The ROOT the interface is laid out in — fixed, never the pane's size. */
  size: { width: 765, height: 503 },
  scale: 1,
  drafts: new Map(),
  pane: 'if',
  records: { if: '', cs2: '', js: '' },
};

let runtime = null;
let catalogue = [];

/* ------------------------------------------------------------------
 * Mounting
 * ------------------------------------------------------------------ */

async function mount(key) {
  runtime?.dispose();
  runtime = null;
  log.textContent = '';
  status('loading ' + key + '…');

  try
  {
    await mountStages(key);
  }
  catch( error )
  {
    status('mount failed: ' + (error?.message ?? error), 'bad');
    log.textContent += (error?.stack ?? String(error)) + '\\n';
  }
}

async function mountStages(key) {
  status('fetching ' + key + '…');
  const detail = await fetch(\`/api/interface?key=\${encodeURIComponent(key)}\`)
    .then((response) => response.json());
  if( detail.error ) { status(detail.error, 'bad'); log.textContent = detail.error; return; }
  if( !(detail.interfaceId >= 0) )
  {
    status(detail.name + ' has no id in 3_interfaces.pack', 'bad');
    return;
  }

  chrome.records = detail.records ?? { if: '', cs2: '', js: '' };
  renderRecords();

  const scripts = new ScriptRegistry();
  const sources = new Map();

  status('compiling ' + Object.keys(detail.scripts ?? {}).length + ' scripts…');
  await installScripts(scripts, sources, detail.scripts ?? {});

  status('mounting…');
  /* The config tables are the page's, so the host and the asset source share
   * one growing view of them -- a widget drawing an obj icon reads the same
   * record the script that placed it read. */
  const config = new HostConfig();
  runtime = mountInterface({
    canvas, scripts, config,
    onWarning: (message) => { log.textContent += \`\${message}\\n\`; },
    onFrame: (painted, session) => { if( painted ) metrics(session); },
  });
  chrome.size = { width: detail.width ?? 765, height: detail.height ?? 503 };
  runtime.resize(chrome.size.width, chrome.size.height);
  fit();

  const session = runtime.session;
  const tree = session.host.tree;

  /*
   * A group a running script reaches into is a MOUNT, not an error.
   *
   * The client answers a component park by loading that interface, so the
   * loader has to be able to bake one. Without this the very first cross-group
   * reference parks forever: the loader answered "not loaded" every frame, the
   * driver never settled, and the session painted nothing at all while looking
   * perfectly healthy — 700 frames, 700 parks, no error.
   */
  runtime.loader.interfaces = {
    hasGroup: (id) => tree.hasGroup(id),
    mount: (id) => mountGroup(session, scripts, sources, id),
  };
  runtime.loader.configs = configSource(config);

  status('baking ' + detail.name + '…');
  const baked = bakeInterface({
    tree,
    ifText: detail.ifText ?? detail.records?.if ?? '',
    compackText: detail.compackText ?? '',
    interfaceId: detail.interfaceId,
  });

  /* Drafts are applied BEFORE onLoad runs, so the first paint is against the
   * state the developer chose rather than against zero. */
  for( const [id, value] of chrome.drafts ) applyDraft(session, id, value);

  /* The bake's own bindings, in pack order and each carrying the component it
   * belongs to — the server's onLoad list knows neither. */
  status('running ' + baked.onLoad.length + ' onload hooks…');
  for( const entry of baked.onLoad )
    session.driver.dispatch(entry.scriptId, entry.args ?? [],
      { reason: 'onload', componentId: entry.componentId });
  await session.driver.settle({ wait: false });

  /* The mount transmit pass the reference runs: a cache-authored transmit
   * hook is the only thing that ever paints some widgets, and at mount
   * nothing has changed yet, so the ordinary filtered pump fires none. */
  session.pump.dispatchAll();
  await session.driver.settle({ wait: false });

  hideSpillover(tree, detail.interfaceId);

  runtime.start();
  renderState(detail.state ?? []);
  status(detail.name + ' · ' + detail.interfaceId, 'ok');

  /* A handle for the browser console and for headless probes. */
  window.__cs2dev = { runtime, detail, scripts, baked };
}

/**
 * Every script the session has, as ONE module.
 *
 * A CS2 script calls its callees by the bare emitted name (cs2_1972), so a
 * per-script module puts each callee outside the caller's scope and the first
 * cross-script call dies with "cs2_1972 is not defined" — after the import
 * succeeded, so nothing is logged and the canvas just stays blank. A mount
 * that brings new scripts therefore recompiles the whole accumulated set
 * rather than adding a second module beside the first.
 */
async function installScripts(scripts, sources, incoming) {
  let added = 0;
  for( const [id, source] of Object.entries(incoming) )
    if( !sources.has(id) ) { sources.set(id, source); added++; }
  if( !added ) return 0;
  try { scripts.addModule(await import(/* @vite-ignore */ toModuleUrl([...sources.values()].join('\\n\\n')))); }
  catch( error ) { log.textContent += \`scripts: \${error.message}\\n\`; }
  return added;
}

/**
 * Bake a group the runtime asked for, and hide it.
 *
 * Baked, NOT opened: a cc_find or if_getlayer naming another group brings
 * the PACK into the tree and nothing more, so its onload does not run and its
 * roots stay hidden until something opens them. Running them here created
 * widgets the reference never had, and every later dynamic id came out wrong.
 */
async function mountGroup(session, scripts, sources, id) {
  const payload = await fetch(\`/api/group?id=\${id | 0}\`)
    .then((response) => (response.ok ? response.json() : null))
    .catch(() => null);
  if( !payload || payload.error )
  {
    log.textContent += \`mount \${id}: \${payload?.error ?? 'not served'}\\n\`;
    return false;
  }

  await installScripts(scripts, sources, payload.scripts ?? {});
  bakeInterface({
    tree: session.host.tree,
    ifText: payload.ifText,
    compackText: payload.compackText ?? '',
    interfaceId: id,
  });
  hideGroupRoots(session.host.tree, id);
  return true;
}

/* park kind -> the HostConfig table it lands in. */
const CONFIG_TABLES = {
  enum: 'enums', struct: 'structs', obj: 'objects', npc: 'npcs',
  loc: 'locs', inv: 'inventories', mapelement: 'mapElements',
  param: 'params',
};

/* The kinds whose park carries a PARAM id in its second slot. */
const PARAM_PARKS = new Set(['obj', 'npc', 'loc', 'struct']);

/*
 * Config rows, one round trip each.
 *
 * A row the content tree does not have is a MISS, not a failure: the host has
 * a documented answer for every config op against an absent id and needs the
 * load to have completed to reach it. The miss is remembered so the same id
 * is not fetched once per park -- a spellbook walking an enum parks on it
 * every iteration.
 */
function configSource(config) {
  const misses = new Set();

  async function fetchRow(kind, table, id) {
    const key = kind + ':' + id;
    if( config.has(table, id) ) return true;
    if( misses.has(key) ) return false;
    const payload = await fetch('/api/config?kind=' + kind + '&id=' + (id | 0))
      .then((response) => (response.ok ? response.json() : null))
      .catch(() => null);
    if( !payload || payload.error || !payload.record ) { misses.add(key); return false; }
    config[table][String(id)] = payload.record;
    return true;
  }

  return {
    hasSync(kind, id) {
      const table = CONFIG_TABLES[kind];
      if( !table ) return false;
      return config.has(table, id) || misses.has(kind + ':' + id);
    },
    async load(kind, id, extra = null) {
      const table = CONFIG_TABLES[kind];
      if( !table ) return false;
      /*
       * oc_param parks on the OBJ and wants two rows: the object and the
       * param it is being asked for. Fetching only the first leaves the
       * second missing, so the retry parks again on the same pair and the
       * driver never settles -- which is a hang, not a slow frame.
       */
      const wantParam = PARAM_PARKS.has(kind) && Number.isInteger(extra) && extra >= 0;
      const rows = await Promise.all([
        fetchRow(kind, table, id),
        wantParam ? fetchRow('param', 'params', extra) : true,
      ]);
      return rows[0] && rows[1];
    },
  };
}

/** Hide every root of group. */
function hideGroupRoots(tree, group) {
  for( const node of tree.nodes )
  {
    if( node.freed || node.parent >= 0 || node.componentId < 0 ) continue;
    if( ((node.componentId >>> 16) & 0xffff) !== (group & 0xffff) ) continue;
    tree.setHidden(node.index, true);
  }
}

/**
 * Hide every root that is not the interface being previewed.
 *
 * task_interface_open sweeps spillover as its LAST step, after onload and
 * after the transmit pass — because a script that reached into another group
 * may have un-hidden its root in between, and left standing that group paints
 * over the one actually being looked at.
 */
function hideSpillover(tree, group) {
  for( const node of tree.nodes )
  {
    if( node.freed || node.parent >= 0 || node.componentId < 0 ) continue;
    if( ((node.componentId >>> 16) & 0xffff) === (group & 0xffff) ) continue;
    tree.setHidden(node.index, true);
  }
}

/*
 * A lowered script is a FRAGMENT, not a module.
 *
 * emitScript leaves three names free -- H, K and PARK -- because the emitter
 * has no business knowing where the host and the intrinsics live, and because
 * the AOT harness supplies them through new Function('K', 'PARK', ...) where an
 * import statement would be a syntax error.
 *
 * The browser has no such enclosing scope, so the two module-level names are
 * imported here. Missing them does not fail the import: an unresolved free
 * identifier in an ES module throws when the LINE runs, so a script whose
 * executed path happened not to reach K. worked and one that did died with
 * "ReferenceError: K is not defined" from inside the frame loop, with the
 * canvas simply staying blank.
 *
 * A blob: URL is opaque, so it is NOT a base any relative or root-absolute
 * specifier can resolve against -- "/src/cs2_intrinsics.js" fails with
 * "Invalid relative url or base scheme isn't hierarchical". The specifiers are
 * therefore made fully absolute against the serving origin.
 */
const SCRIPT_MODULE_PREAMBLE =
  "import * as K from '" + location.origin + "/src/cs2_intrinsics.js';\\n" +
  "import { HOST_PARK as PARK } from '" + location.origin + "/src/generated/cs2_host_park.js';\\n";

function toModuleUrl(source) {
  return URL.createObjectURL(
    new Blob([SCRIPT_MODULE_PREAMBLE, source], { type: 'text/javascript' }));
}

/*
 * Fit the canvas to its pane by SCALING it, never by resizing the root.
 *
 * The cache authored every position against a fixed root — 765x503 is what
 * the reference client runs and what the parity captures were taken at — so
 * handing the interface the pane's size instead lays it out at a size no
 * reference ever had.
 *
 * The canvas then FILLS the pane, fractional scales included. Snapping to
 * an integer is kinder to pixel art -- at 2x every source pixel is exactly
 * four -- but it also threw away half the room whenever the pane held 1.9
 * copies of the interface, and a picture at 1x in a pane with space for 1.9
 * reads as broken rather than as principled. The zoom is reported in the
 * header so the number is never a mystery.
 */
function fit() {
  if( !runtime ) return;
  const { width, height } = chrome.size;
  const room = Math.min(
    (stage.clientWidth - 32) / width,
    (stage.clientHeight - 32) / height);
  const scale = Math.max(0.1, room);
  chrome.scale = scale;
  canvas.style.width = Math.floor(width * scale) + 'px';
  canvas.style.height = Math.floor(height * scale) + 'px';
  zoom.textContent = scale.toFixed(2).replace(/\\.?0+$/, '') + 'x';
}

function applyDraft(session, id, value) {
  const [kind, raw] = id.split(':');
  const number = Number(raw);
  if( kind === 'varp' ) session.host.state.setVarp(number, Number(value));
  else if( kind === 'varbit' ) session.host.state.setVarbit(number, Number(value));
  else if( kind === 'varc' ) session.host.state.setVarc(number, Number(value));
}

/* ------------------------------------------------------------------
 * Metrics
 * ------------------------------------------------------------------ */

function metrics(session) {
  const frame = session.stats;
  set('m-frame', \`\${frame.painted}/\${frame.frames}\`);
  set('m-calls', String(session.host.calls));
  set('m-nodes', String(session.tree.liveCount));
  set('m-draws', String(session.emitter.commands.length));
}

function set(id, text) { $(id).querySelector('b').textContent = text; }

/* ------------------------------------------------------------------
 * Host state controls
 * ------------------------------------------------------------------ */

function renderState(slices) {
  const host = $('state');
  host.textContent = '';
  for( const slice of slices )
  {
    const row = document.createElement('div');
    row.className = 'row';
    const label = document.createElement('label');
    label.textContent = slice.label ?? slice.id;
    const input = document.createElement('input');
    input.type = slice.type === 'text' ? 'text' : 'number';
    input.value = chrome.drafts.get(slice.id) ?? slice.value ?? 0;
    if( slice.min !== undefined ) input.min = slice.min;
    if( slice.max !== undefined ) input.max = slice.max;
    input.addEventListener('input', () => {
      chrome.drafts.set(slice.id, input.value);
      if( !runtime ) return;
      applyDraft(runtime.session, slice.id, input.value);
      /* A var write from the page is a SERVER write, not a script one: it has
       * to arm the transmit pump, or nothing re-reads it. */
      const number = Number(slice.id.split(':')[1]);
      runtime.session.pump.noteVarChanged(number);
    });
    row.append(label, input);
    host.append(row);
  }
}

/* ------------------------------------------------------------------
 * Records
 * ------------------------------------------------------------------ */

function renderRecords() {
  const pane = $('records');
  const text = chrome.records[chrome.pane] ?? '';
  pane.textContent = text || 'nothing to show';
  pane.classList.toggle('empty', !text);
}

$('tabs').addEventListener('click', (event) => {
  const button = event.target.closest('button');
  if( !button ) return;
  chrome.pane = button.dataset.pane;
  for( const other of $('tabs').children )
    other.setAttribute('aria-selected', String(other === button));
  renderRecords();
});

/* ------------------------------------------------------------------
 * Picker
 * ------------------------------------------------------------------ */

const pick = $('pick');
const pickmenu = $('pickmenu');

pick.addEventListener('focus', () => renderPicker(pick.value));
pick.addEventListener('input', () => renderPicker(pick.value));
pick.addEventListener('blur', () => setTimeout(() => pickmenu.classList.remove('open'), 120));

function renderPicker(query) {
  const needle = query.trim().toLowerCase();
  const matches = catalogue
    .filter((entry) => !needle || entry.name.toLowerCase().includes(needle))
    .slice(0, 200);
  pickmenu.textContent = '';
  let group = null;
  for( const entry of matches )
  {
    if( entry.source !== group )
    {
      group = entry.source;
      const heading = document.createElement('div');
      heading.className = 'pickgroup';
      heading.textContent = group;
      pickmenu.append(heading);
    }
    const row = document.createElement('div');
    row.textContent = entry.label ?? entry.name;
    row.setAttribute('aria-selected', String(entry.key === chrome.selected));
    row.addEventListener('mousedown', () => {
      chrome.selected = entry.key;
      pick.value = entry.name;
      pickmenu.classList.remove('open');
      mount(entry.key);
    });
    pickmenu.append(row);
  }
  pickmenu.classList.add('open');
}

$('reload').addEventListener('click', () => { if( chrome.selected ) mount(chrome.selected); });

/* ------------------------------------------------------------------
 * Live reload
 *
 * The server says a source changed; the page remounts the SESSION. Nothing
 * above this line is rebuilt, so drafts, selection and focus survive.
 * ------------------------------------------------------------------ */
if( 'EventSource' in globalThis )
{
  const events = new EventSource('/events');
  /*
   * A RESTARTED server means new code, and this document is holding the old.
   *
   * Its /src/... imports were fetched once and an ES module is cached for
   * the life of the document, so a tab left open across a server restart runs
   * the modules it started with -- a remount rebuilds the session out of the
   * same stale painter and tree. Every runtime fix was invisible until the
   * page itself was reloaded, which looks exactly like a fix that did not
   * work.
   */
  let boot = null;
  events.addEventListener('hello', (event) => {
    if( boot === null ) { boot = event.data; return; }
    if( boot !== event.data ) location.reload();
  });
  events.addEventListener('reload', () => location.reload());
  events.addEventListener('changed', () => { if( chrome.selected ) mount(chrome.selected); });
}

const stage = $('stage');
new ResizeObserver(fit).observe(stage);

/*
 * The side panels start collapsed on a window too narrow to hold the
 * preview at 1:1. Scaling the canvas is honest -- the geometry is the
 * reference's either way -- but a half-size interface reads as a rendering
 * bug, and the panels are the part of the page that can afford to go.
 */
const panels = $('panels');
function setPanels(on) {
  document.body.dataset.panels = on ? 'on' : 'off';
  panels.setAttribute('aria-pressed', String(on));
  fit();
}
panels.addEventListener('click', () => setPanels(document.body.dataset.panels === 'off'));
setPanels(innerWidth >= 765 + 32 + 280 + 340);

fetch('/api/catalogue')
  .then((response) => response.json())
  .then((entries) => { catalogue = entries; })
  .catch(() => { log.textContent = 'could not load the interface catalogue'; });
`;
}
