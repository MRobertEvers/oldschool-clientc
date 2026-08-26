/*
 * The dev page: the real client in an iframe, and what the interface compiles to.
 *
 * Three panes. The PREVIEW is `build-web/index.html` -- the official client,
 * compiled to WebAssembly, drawing with toridraw and hit-testing with its own
 * code. Nothing on this page renders an interface, which is the entire point:
 * the picture cannot disagree with the client's because it IS the client's.
 *
 * Interaction is the client's too. SDL owns that canvas, so hovering, clicking,
 * right-click menus and scrolling are handled inside the iframe by the same code
 * that handles them in the desktop build. This page does not hit-test.
 *
 * What this page adds is the two things the client has no opinion about: which
 * interface to open and what state to open it against (the STATE pane, which
 * posts cmdbus frames in), and what the thing on screen is made of (the RECORDS
 * pane -- `.if`, `.cs2`, and that CS2 lowered to JavaScript).
 */

export function clientDevPage({ title = 'cs2dom', build = 'dev' } = {}) {
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
/* The PREVIEW gets the room. A 765-wide client and two fixed panels do not fit
   in a small window, and the loser is always the preview -- it is the only
   column that can shrink. The panels collapse instead. */
main { display: grid; grid-template-columns: minmax(360px, 1fr) 300px 340px; overflow: hidden; }
body[data-panels=off] main { grid-template-columns: 1fr; }
body[data-panels=off] #panel-state,
body[data-panels=off] #panel-records { display: none; }
#panels[aria-pressed=false] { color: var(--dim); }
section { overflow: auto; border-left: 1px solid var(--line); }
section:first-child { border-left: 0; }
#panel-preview { display: grid; grid-template-rows: auto 1fr auto auto; overflow: hidden; }
h2 {
  font: 600 10px var(--mono); letter-spacing: .12em; text-transform: uppercase;
  color: var(--dim); margin: 0; padding: 8px 12px;
  border-bottom: 1px solid var(--line); position: sticky; top: 0; background: var(--panel);
}
#stage { display: grid; place-items: center; padding: 16px; min-height: 0; overflow: auto; }
/* The client sizes its own canvas; the frame is just a window onto it. No
   border-radius and no scaling -- anything this page did to the pixels would
   be a difference between the preview and the game that this page invented. */
#client {
  width: 100%; height: 100%; border: 0; background: #000;
  box-shadow: 0 0 0 1px var(--line), 0 12px 40px rgba(0,0,0,.5);
}
.row { display: flex; align-items: center; gap: 8px; padding: 5px 12px; }
.row label { flex: 1; font: 11px var(--mono); color: var(--dim); }
.row input[type=number], .row input[type=text] { width: 84px; }
.row input.wide { width: 100%; }
.group {
  padding: 8px 12px 4px; color: var(--dim); font: 600 9px var(--mono);
  letter-spacing: .1em; text-transform: uppercase;
  border-top: 1px solid var(--line); margin-top: 6px;
}
.group:first-child { border-top: 0; margin-top: 0; }
.hint { padding: 2px 12px 8px; font: 10px var(--mono); color: var(--dim); }
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
#log { padding: 6px 12px; font: 11px var(--mono); color: var(--bad); white-space: pre-wrap; }
#status {
  padding: 6px 12px; font: 11px var(--mono); color: var(--dim); white-space: pre-wrap;
}
#status[data-state=busy] { color: var(--accent); }
#status[data-state=bad] { color: var(--bad); }
#status[data-state=good] { color: var(--hot); }
</style>
</head>
<body>
<header>
  <h1>cs2dom</h1>
  <div class="picker">
    <input id="pick" type="search" placeholder="interface…" autocomplete="off" spellcheck="false">
    <div class="pickmenu" id="pickmenu"></div>
  </div>
  <button id="reboot" title="Restart the client and open this interface again">Reboot</button>
  <button id="panels" title="Show or hide the side panels">Panels</button>
  <div class="spacer"></div>
  <div class="metric" id="m-client">client <b>—</b></div>
  <div class="metric" id="m-iface">iface <b>—</b></div>
  <div class="metric">build <b>${build}</b></div>
</header>
<main>
  <section id="panel-preview">
    <h2>Preview · the client</h2>
    <div id="stage"><iframe id="client" title="ToriRS"></iframe></div>
    <div id="status">select an interface</div>
    <div id="log"></div>
  </section>
  <section id="panel-state">
    <h2>State</h2>
    <div id="state"></div>
  </section>
  <section id="panel-records">
    <h2>Records</h2>
    <div class="tabs" id="tabs">
      <button data-pane="if" aria-selected="true">.if</button>
      <button data-pane="compack" aria-selected="false">.compack</button>
      <button data-pane="cs2" aria-selected="false">.cs2</button>
      <button data-pane="js" aria-selected="false">javascript</button>
    </div>
    <pre id="records" class="empty">select an interface</pre>
  </section>
</main>
<script type="module" src="/dev-client.js?v=${build}"></script>
</body>
</html>`;
}

/**
 * The page's own script.
 *
 * Served separately so it is a real module the browser caches and the debugger
 * can step through, rather than a string inside the document.
 */
export function clientDevScript() {
    return `
import {
    concatFrames, execText, openRoot, runScript, setVarbit, setVarp,
} from '/src/cmd_frames.js';

const $ = (id) => document.getElementById(id);
const frame = $('client');
const status = $('status');

let catalogue = [];
let current = null;      /* the catalogue entry on screen */
let clientReady = false;
let pending = [];        /* frames posted before the client said it was up */

/* ---------------------------------------------------------------------- *
 * Talking to the client
 * ---------------------------------------------------------------------- */

/*
 * The client announces itself once, from the point its frame loop starts
 * (web_announce_ready in src/main.c). Before that there is no export to call,
 * so frames are HELD rather than dropped -- unlike the client's own listener,
 * this side knows a boot is in progress and that waiting is the right answer.
 */
window.addEventListener('message', (event) => {
    if( event.source !== frame.contentWindow ) return;
    if( !event.data || event.data.type !== 'torirs-ready' ) return;
    clientReady = true;
    metric('m-client', 'running', 'good');
    const held = pending;
    pending = [];
    for( const batch of held ) post(batch);
    if( current ) say(\`\${current.name} · interface \${current.interfaceId}\`, 'good');
});

function post(batch) {
    if( !clientReady ) { pending.push(batch); return; }
    const buffer = batch.buffer.slice(batch.byteOffset, batch.byteOffset + batch.byteLength);
    frame.contentWindow.postMessage({ type: 'torirs-cmdbus', buffer }, '*', [buffer]);
}

/** Several commands in one message: one postMessage, one drain, one frame. */
const send = (...frames) => post(concatFrames(frames));

/* ---------------------------------------------------------------------- *
 * Booting the client on an interface
 * ---------------------------------------------------------------------- */

/*
 * The client's command line is its query string, and the interface id is a
 * positional argument -- offline, it is the only root and no world is loaded
 * (src/main.c, App_OpenRootInterface). Booting straight onto the interface
 * rather than booting empty and then commanding it open costs nothing and
 * means the first frame drawn is already the right one.
 */
function bootClient(entry) {
    clientReady = false;
    pending = [];
    metric('m-client', 'booting');
    metric('m-iface', entry.interfaceId >= 0 ? String(entry.interfaceId) : '—');
    /*
     * The client's command line, exactly as the native one is spelled:
     *
     *     torirs <cache_dir> --rev <name> <interface_id>
     *
     * The cache directory is the FIRST POSITIONAL and there is no flag for it.
     * Leaving it out does not fail -- the interface id slides into its place
     * and the client boots against a cache directory named "600", reporting
     * "dat2 cache=600 iface=0" and then drawing an empty room.
     */
    const args = [CACHE_DIR, '--rev', REVISION, String(entry.interfaceId)];
    const query = args.map((a) => \`arg=\${encodeURIComponent(a)}\`).join('&');
    /* Cache-busted so a reboot is a real reboot: the same src would leave the
     * old wasm instance running and nothing would happen. */
    frame.src = \`/client/index.html?\${query}&io=/io&fullcanvas=1&t=\${Date.now()}\`;
}

/* ---------------------------------------------------------------------- *
 * Choosing an interface
 * ---------------------------------------------------------------------- */

async function open(entry) {
    if( !entry ) return;
    current = entry;
    $('pick').value = entry.name;
    say(\`opening \${entry.name}…\`, 'busy');
    if( entry.interfaceId < 0 )
        return say(\`\${entry.name} has no id in 3_interfaces.pack; the client cannot open it\`, 'bad');
    bootClient(entry);
    await loadRecords(entry);
}

async function loadRecords(entry) {
    try
    {
        const response = await fetch(\`/api/records?key=\${encodeURIComponent(entry.key)}\`);
        const data = await response.json();
        if( data.error ) throw new Error(data.error);
        records = data.records;
        showPane(pane);
        $('log').textContent = (data.errors ?? []).join('\\n');
    }
    catch( error )
    {
        records = null;
        $('log').textContent = \`records: \${error.message}\`;
    }
}

/* ---------------------------------------------------------------------- *
 * The state pane
 * ---------------------------------------------------------------------- */

/*
 * Every control here writes through the command bus, which is to say it writes
 * exactly where the game writes. There is no preview-only copy of a varp to
 * disagree with the client's.
 */
function buildStatePane() {
    const pane = $('state');
    pane.innerHTML = '';

    pane.append(
        group('Variables'),
        pair('varp', 'id', 'value', (id, value) => send(setVarp(id, value))),
        pair('varbit', 'id', 'value', (id, value) => send(setVarbit(id, value))),
        group('Run'),
        scriptRow(),
        group('Interface'),
        openRow(),
        group('Command'),
        textRow('::', (text) => send(execText(text)),
            'sent to the server; needs a connection'));
}

function group(label) {
    const div = document.createElement('div');
    div.className = 'group';
    div.textContent = label;
    return div;
}

function pair(label, aLabel, bLabel, apply) {
    const row = document.createElement('div');
    row.className = 'row';
    const name = document.createElement('label');
    name.textContent = label;
    const a = numberInput(aLabel);
    const b = numberInput(bLabel);
    const go = document.createElement('button');
    go.textContent = 'set';
    const fire = () => {
        if( a.value === '' ) return;
        apply(Number(a.value), Number(b.value || 0));
    };
    go.onclick = fire;
    b.onkeydown = (e) => { if( e.key === 'Enter' ) fire(); };
    row.append(name, a, b, go);
    return row;
}

function scriptRow() {
    const row = document.createElement('div');
    row.className = 'row';
    const name = document.createElement('label');
    name.textContent = 'script';
    const id = numberInput('id');
    const args = document.createElement('input');
    args.type = 'text';
    args.placeholder = 'args';
    const go = document.createElement('button');
    go.textContent = 'run';
    go.onclick = () => {
        if( id.value === '' ) return;
        const list = args.value.split(',').map((s) => s.trim()).filter(Boolean).map(Number);
        try { send(runScript(Number(id.value), list)); }
        catch( error ) { $('log').textContent = error.message; }
    };
    row.append(name, id, args, go);
    return row;
}

function openRow() {
    const row = document.createElement('div');
    row.className = 'row';
    const name = document.createElement('label');
    name.textContent = 'open id';
    const id = numberInput('id');
    const go = document.createElement('button');
    go.textContent = 'open';
    go.onclick = () => { if( id.value !== '' ) send(openRoot(Number(id.value))); };
    row.append(name, id, go);
    return row;
}

function textRow(label, apply, hint) {
    const wrap = document.createElement('div');
    const row = document.createElement('div');
    row.className = 'row';
    const name = document.createElement('label');
    name.textContent = label;
    const text = document.createElement('input');
    text.type = 'text';
    text.className = 'wide';
    text.onkeydown = (e) => { if( e.key === 'Enter' && text.value ) apply(text.value); };
    row.append(name, text);
    wrap.append(row);
    if( hint )
    {
        const note = document.createElement('div');
        note.className = 'hint';
        note.textContent = hint;
        wrap.append(note);
    }
    return wrap;
}

function numberInput(placeholder) {
    const input = document.createElement('input');
    input.type = 'number';
    input.placeholder = placeholder;
    return input;
}

/* ---------------------------------------------------------------------- *
 * Records
 * ---------------------------------------------------------------------- */

let records = null;
let pane = 'if';

function showPane(which) {
    pane = which;
    for( const button of $('tabs').children )
        button.setAttribute('aria-selected', String(button.dataset.pane === which));
    const box = $('records');
    const text = records ? records[which] : '';
    box.textContent = text || (records ? '(empty)' : 'select an interface');
    box.className = text ? '' : 'empty';
}

$('tabs').onclick = (event) => {
    if( event.target.dataset.pane ) showPane(event.target.dataset.pane);
};

/* ---------------------------------------------------------------------- *
 * The picker
 * ---------------------------------------------------------------------- */

const pickInput = $('pick');
const pickMenu = $('pickmenu');

function renderMenu(filter) {
    const needle = filter.trim().toLowerCase();
    const hits = catalogue
        .filter((e) => !needle || e.label.toLowerCase().includes(needle))
        .slice(0, 300);
    pickMenu.innerHTML = '';
    for( const entry of hits )
    {
        const row = document.createElement('div');
        row.textContent = entry.label;
        row.onmousedown = (event) => { event.preventDefault(); closeMenu(); open(entry); };
        pickMenu.append(row);
    }
    pickMenu.classList.toggle('open', hits.length > 0);
}

const closeMenu = () => pickMenu.classList.remove('open');
pickInput.oninput = () => renderMenu(pickInput.value);
pickInput.onfocus = () => renderMenu(pickInput.value);
pickInput.onblur = () => setTimeout(closeMenu, 120);
pickInput.onkeydown = (event) => {
    if( event.key === 'Escape' ) closeMenu();
    if( event.key === 'Enter' )
    {
        const first = catalogue.find(
            (e) => e.label.toLowerCase().includes(pickInput.value.trim().toLowerCase()));
        if( first ) { closeMenu(); open(first); }
    }
};

/* ---------------------------------------------------------------------- *
 * Chrome
 * ---------------------------------------------------------------------- */

$('reboot').onclick = () => { if( current ) open(current); };
$('panels').onclick = () => {
    const off = document.body.dataset.panels === 'off';
    document.body.dataset.panels = off ? 'on' : 'off';
    $('panels').setAttribute('aria-pressed', String(off));
};

function say(text, state) {
    status.textContent = text;
    if( state ) status.dataset.state = state; else delete status.dataset.state;
}

function metric(id, value, tone) {
    const box = $(id);
    box.querySelector('b').textContent = value;
    box.className = 'metric' + (tone ? \` \${tone}\` : '');
}

/* ---------------------------------------------------------------------- *
 * Boot
 * ---------------------------------------------------------------------- */

let REVISION = 'osrs239';
let CACHE_DIR = 'cache.osrs239';

/*
 * A tab that outlives its server is running code that no longer exists: the
 * document imported each module once, so a fix landed while it stayed open is
 * invisible in it. The boot id changing is what says the process restarted.
 */
function listen() {
    const events = new EventSource('/events');
    let boot = null;
    events.addEventListener('hello', (event) => {
        if( boot && boot !== event.data ) return location.reload();
        boot = event.data;
    });
    events.addEventListener('reload', () => location.reload());
    events.addEventListener('changed', async () => {
        await loadCatalogue();
        if( current ) open(current);
    });
}

async function loadCatalogue() {
    catalogue = await (await fetch('/api/catalogue')).json();
}

(async function start() {
    const project = await (await fetch('/api/project')).json();
    REVISION = project.revision ?? 'osrs239';
    if( project.cacheDir ) CACHE_DIR = project.cacheDir;
    if( !project.hasCache )
        say('no cache is configured; the client has nothing to read', 'bad');
    await loadCatalogue();
    buildStatePane();
    listen();

    const wanted = new URLSearchParams(location.search).get('open');
    const entry = wanted
        ? catalogue.find((e) => e.name === wanted || String(e.interfaceId) === wanted)
        : null;
    if( entry ) open(entry);
    else if( !wanted ) say(\`\${catalogue.length} interfaces · pick one\`);
    else say(\`no interface named \${wanted}\`, 'bad');
})();
`;
}
