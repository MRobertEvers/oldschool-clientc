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
main { display: grid; grid-template-columns: 1fr 300px 380px; overflow: hidden; }
section { overflow: auto; border-left: 1px solid var(--line); }
section:first-child { border-left: 0; }
h2 {
  font: 600 10px var(--mono); letter-spacing: .12em; text-transform: uppercase;
  color: var(--dim); margin: 0; padding: 8px 12px;
  border-bottom: 1px solid var(--line); position: sticky; top: 0; background: var(--panel);
}
#stage { display: grid; place-items: center; padding: 16px; min-height: 0; }
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
  <div class="spacer"></div>
  <div class="metric" id="m-frame">frame <b>—</b></div>
  <div class="metric" id="m-calls">host <b>—</b></div>
  <div class="metric" id="m-nodes">nodes <b>—</b></div>
  <div class="metric" id="m-draws">draws <b>—</b></div>
</header>
<main>
  <section>
    <h2>Preview</h2>
    <div id="stage"><canvas id="surface" width="765" height="503"></canvas></div>
    <div id="log"></div>
  </section>
  <section>
    <h2>Host state</h2>
    <div id="state"></div>
  </section>
  <section>
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

const $ = (id) => document.getElementById(id);
const canvas = $('surface');
const log = $('log');

/* ------------------------------------------------------------------
 * State that survives a remount
 *
 * The picker selection, the host-state drafts and the records tab belong to
 * the PAGE. A source save replaces the session and must not disturb any of
 * them — that is the one hot-reload rule worth keeping from the old runtime.
 * ------------------------------------------------------------------ */
const chrome = {
  selected: null,
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
  log.textContent = '';

  const detail = await fetch(\`/api/interface?key=\${encodeURIComponent(key)}\`)
    .then((response) => response.json())
    .catch((error) => ({ error: String(error) }));
  if( detail.error ) { log.textContent = detail.error; return; }

  chrome.records = detail.records ?? { if: '', cs2: '', js: '' };
  renderRecords();

  const scripts = new ScriptRegistry();
  for( const [id, source] of Object.entries(detail.scripts ?? {}) )
  {
    try { scripts.addModule(await import(/* @vite-ignore */ toModuleUrl(source))); }
    catch( error ) { log.textContent += \`script \${id}: \${error.message}\\n\`; }
  }

  runtime = mountInterface({
    canvas, scripts,
    onWarning: (message) => { log.textContent += \`\${message}\\n\`; },
    onFrame: (painted, session) => { if( painted ) metrics(session); },
  });
  runtime.resize(detail.width ?? 765, detail.height ?? 503);

  /* Drafts are applied BEFORE onLoad runs, so the first paint is against the
   * state the developer chose rather than against zero. */
  for( const [id, value] of chrome.drafts ) applyDraft(runtime.session, id, value);

  for( const entry of detail.onLoad ?? [] )
    runtime.session.driver.dispatch(entry.scriptId, entry.args ?? [], { reason: 'onload' });

  runtime.start();
  renderState(detail.state ?? []);
}

function toModuleUrl(source) {
  return URL.createObjectURL(new Blob([source], { type: 'text/javascript' }));
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
  events.addEventListener('changed', () => { if( chrome.selected ) mount(chrome.selected); });
}

/* Fit the canvas to its pane, without laying the interface out in device
 * pixels — the cache's coordinates are CSS pixels and scaling them would make
 * every authored position wrong by the ratio. */
const stage = $('stage');
new ResizeObserver(() => {
  if( !runtime ) return;
  const width = Math.max(320, Math.floor(stage.clientWidth - 32));
  const height = Math.max(240, Math.floor(stage.clientHeight - 32));
  runtime.resize(width, height);
}).observe(stage);

fetch('/api/catalogue')
  .then((response) => response.json())
  .then((entries) => { catalogue = entries; })
  .catch(() => { log.textContent = 'could not load the interface catalogue'; });
`;
}
