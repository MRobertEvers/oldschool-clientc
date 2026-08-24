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
    outline: 1px solid var(--line);
  }
  #stage.native { overflow: hidden; background: #000; }
  .nativeframe {
    position: absolute; z-index: 0; inset: 0; width: 100%; height: 100%;
    object-fit: fill; image-rendering: pixelated;
  }
  .box { position: absolute; overflow: hidden; }
  .box.text { display: flex; white-space: pre; }
  .box.model, .box.unknown {
    border: 1px dashed #4a4335; color: var(--dim);
    font: 10px var(--mono); display: flex; align-items: center; justify-content: center;
  }
  .box.model.ready { border: 0; }
  .box.model canvas { width: 100%; height: 100%; image-rendering: pixelated; }
  .box img { width: 100%; height: 100%; image-rendering: pixelated; }
  .box.outline { outline: 1px solid var(--accent); outline-offset: 0; }
  #stage.wire .box { outline: 1px solid rgba(226,169,63,.35); }
  /* The native framebuffer already contains every pixel. Boxes remain as
     transparent inspector hit regions; painting them again would put CSS's
     approximate fonts/sprites back over the authoritative C render. */
  #stage.native .box {
    z-index: 1; overflow: visible; background: transparent !important;
    border: 0 !important; color: transparent !important; opacity: 1 !important;
    text-shadow: none !important; pointer-events: none;
  }
  #stage.native .box > * { display: none !important; }
  #stage.native .box.outline {
    outline: 1px solid var(--accent); background: rgba(226,169,63,.08) !important;
  }

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
    <div id="controls"></div>
  </section>

  <section>
    <h2>Cache records</h2>
    <div id="records"></div>
  </section>
</main>

<script src="/toridraw/ev_wasm.js"></script>
<script>
const state = {};            // "varp:300" -> value
let data = null;
let catalog = [];
let chosen = null;
let pickerMatches = [];
let pickerActive = -1;

const $ = (id) => document.getElementById(id);

async function refresh() {
  if( catalog.length === 0 ) {
    const listing = await fetch('/catalog').then((response) => response.json());
    catalog = listing.interfaces || [];
    if( !chosen ) chosen = catalog[0] && catalog[0].key;
    populatePicker();
  }
  const query = new URLSearchParams({
    state: JSON.stringify(state),
    interface: chosen || '',
  });
  const response = await fetch('/state?' + query);
  data = await response.json();
  render();
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
  closePicker();
  refresh();
}

function render() {
  const records = $('records');
  if( data.error ) {
    $('status').textContent = 'error';
    $('status').className = 'status bad';
    records.innerHTML = '<div class="error"></div>';
    records.firstChild.textContent = data.error;
    $('stage').innerHTML = '';
    $('tree').innerHTML = '';
    return;
  }

  $('status').textContent = 'built ' + new Date().toLocaleTimeString();
  $('status').className = 'status fresh';
  setTimeout(() => { $('status').className = 'status'; }, 900);

  if( !$('pickmenu').classList.contains('open') ) syncPickerLabel();
  const iface = data.interfaces[0];
  if( !iface ) return;

  drawStage(iface);
  drawTree(iface);
  drawControls(iface);
  drawRecords(iface);
}

function drawStage(iface) {
  const stage = $('stage');
  const root = iface.boxes[0];
  const width = root ? Math.max(root.w, 32) : 256;
  const height = root ? Math.max(root.h, 32) : 128;
  stage.style.width = width + 'px';
  stage.style.height = height + 'px';
  const native = Boolean(iface.nativeFrame);
  stage.className = (native ? 'native ' : '') + ($('wire').checked ? 'wire' : '');
  $('dims').textContent = width + '×' + height + ' — interface ' + iface.interfaceId +
    (native ? ' — C client' : '');

  stage.innerHTML = '';
  const epoch = ++modelEpoch;
  const originX = root ? root.x : 0;
  const originY = root ? root.y : 0;

  if( native ) {
    const frame = document.createElement('img');
    frame.className = 'nativeframe';
    frame.alt = 'Interface ' + iface.interfaceId + ' rendered by the C client';
    frame.src = iface.nativeFrame;
    frame.onerror = () => {
      /* A missing native binary/cache should not make the editor blank. Paint
         the diagnostic DOM fallback in-place and keep the error visible. */
      stage.classList.remove('native');
      frame.remove();
      for( const record of fallbackPaint ) {
        paint(record.element, record.box, iface);
        if( roleOf(record.box.type) === 'model' )
          paintModel(record.element, record.box, iface, epoch);
      }
      $('status').textContent = 'C preview unavailable — showing diagnostic DOM';
      $('status').className = 'status bad';
    };
    stage.appendChild(frame);
  }

  const fallbackPaint = [];

  for( const box of iface.boxes ) {
    const element = document.createElement('div');
    element.className = 'box ' + roleOf(box.type);
    element.style.left = (box.x - originX) + 'px';
    element.style.top = (box.y - originY) + 'px';
    element.style.width = box.w + 'px';
    element.style.height = box.h + 'px';
    if( box.emitted === false || box.effectiveHidden || box.culled )
      element.style.display = 'none';
    if( box.clip && box.w > 0 && box.h > 0 ) {
      const top = Math.max(0, box.clip.top - box.y);
      const right = Math.max(0, box.x + box.w - box.clip.right);
      const bottom = Math.max(0, box.y + box.h - box.clip.bottom);
      const left = Math.max(0, box.clip.left - box.x);
      if( top || right || bottom || left )
        element.style.clipPath = 'inset(' + top + 'px ' + right + 'px ' +
          bottom + 'px ' + left + 'px)';
    }
    if( !native && box.props.transparency )
      element.style.opacity = String(1 - box.props.transparency / 255);
    element.title = box.name + ' — ' + box.kind + ' (file ' + box.fileId + ')';
    element.dataset.name = box.name;

    if( native ) fallbackPaint.push({ element, box });
    else paint(element, box, iface);
    stage.appendChild(element);
    if( !native && roleOf(box.type) === 'model' ) paintModel(element, box, iface, epoch);
  }
}

function roleOf(type) {
  return ({ 0: 'layer', 2: 'inv', 3: 'rect', 4: 'text', 5: 'graphic', 6: 'model', 8: 'text', 9: 'line' })[type] || 'unknown';
}

function paint(element, box, iface) {
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
        const image = document.createElement('img');
        image.src = '/sprite/' + iface.spriteSource + '/' + props.sprite + '.png';
        image.alt = '';
        image.onerror = () => {
          element.classList.add('unknown');
          element.textContent = 'sprite ' + props.sprite;
          image.remove();
        };
        element.appendChild(image);
      }
      break;
    }
    case 'line':
      element.style.background = colour(props.color);
      element.style.height = Math.max(1, props.lineWidth | 0) + 'px';
      break;
    case 'model':
      element.textContent = props.model >= 0 ? 'loading model ' + props.model : 'loading player';
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
    setPan: wrap('ev_w_set_pan', null, ['number', 'number']),
    render: wrap('ev_w_render', 'number', ['number', 'number', 'number', 'number', 'number', 'number']),
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

function paintModel(element, box, iface, epoch) {
  if( box.w <= 0 || box.h <= 0 ) return;
  const player = box.props.clientCode === 328 && box.props.model < 0;
  if( box.props.model < 0 && !player ) {
    element.textContent = 'model source unavailable';
    return;
  }
  const url = player
    ? '/model/' + iface.modelSource + '/player.model'
    : '/model/' + iface.modelSource + '/' + box.props.model + '.model';
  modelQueue = modelQueue.then(async () => {
    const [{ bytes, textures }, wasm] = await Promise.all([fetchModel(url), wasmRenderer()]);
    if( epoch !== modelEpoch || !element.isConnected ) return;
    const magic = String.fromCharCode(...bytes.subarray(0, 4));
    const faces = pushWasm(wasm, bytes, (ptr, size) =>
      magic === 'EVH1' ? wasm.setModelHd(ptr, size) : wasm.setModel(ptr, size));
    if( magic !== 'EVH1' ) wasm.clearModelHd();
    pushWasm(wasm, textures, (ptr, size) => wasm.setTextures(ptr, size));
    wasm.clearAnim();
    if( box.props.seq >= 0 ) {
      try {
        const response = await fetch('/model/seq/' + box.props.seq + '.anim');
        if( response.ok ) {
          const animation = new Uint8Array(await response.arrayBuffer());
          pushWasm(wasm, animation, (ptr, size) => wasm.setAnim(ptr, size));
        }
      } catch { /* bind pose is a valid fallback */ }
    }
    if( !faces ) throw new Error('model decode failed');

    const width = Math.max(1, Math.min(1024, box.w | 0));
    const height = Math.max(1, Math.min(1024, box.h | 0));
    wasm.setPan(box.props.xOffset | 0, box.props.yOffset | 0);
    const ptr = wasm.render(
      width, height, box.props.yAngle | 0, box.props.xAngle | 0,
      Math.max(1, box.props.zoom | 0), box.props.seq >= 0 ? 0 : -1);
    if( !ptr ) throw new Error('model render failed');
    const rgba = new Uint8ClampedArray(
      wasm.mod.HEAPU8.slice(ptr, ptr + width * height * 4));
    if( epoch !== modelEpoch || !element.isConnected ) return;
    const canvas = document.createElement('canvas');
    canvas.width = width; canvas.height = height;
    canvas.getContext('2d').putImageData(new ImageData(rgba, width, height), 0, 0);
    element.textContent = '';
    element.classList.add('ready');
    element.appendChild(canvas);
  }).catch((error) => {
    if( epoch === modelEpoch && element.isConnected ) {
      element.textContent = 'model unavailable';
      element.title += ' — ' + error.message;
    }
  });
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
    const level = box.layer === null ? 0 : (depth.get(box.layer) ?? 0) + 1;
    depth.set(box.fileId, level);

    const row = document.createElement('div');
    const bound = box.dynamic.length ? ' <span class="bound">◆ ' + box.dynamic.join(' ') + '</span>' : '';
    const events = box.events.length ? ' <span class="rowlabel">' + box.events.join(' ') + '</span>' : '';
    row.innerHTML = '&nbsp;'.repeat(level * 2) +
      box.fileId + ' ' + box.name +
      ' <span class="rowlabel">' + box.kind.toLowerCase() + ' ' +
      box.w + '×' + box.h + '</span>' + bound + events;
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

function drawControls(iface) {
  const controls = $('controls');
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
  return key in state ? state[key] : fallback;
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
    state[input.key] = Number(slider.value);
    readout.textContent = slider.value;
    refresh();
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
  field.oninput = () => { state[input.key] = field.value; refresh(); };
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
    /* An untouched control is not native state. Once the user edits it, clone
       first so the outgoing request gets a new, explicit inventory value. */
    contents = state[key] = { ...contents };
    edit(contents);
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
        refresh();
      };
      const drop = document.createElement('button');
      drop.textContent = '×';
      drop.onclick = () => {
        editContents((next) => { delete next[obj]; });
        redraw();
        refresh();
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
      refresh();
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

  for( const warning of (data.warnings || []) ) {
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
$('add').onclick = async () => {
  const name = prompt('component name (a file, ui/<name>.tsx)');
  if( !name ) return;
  const response = await fetch('/new', { method: 'POST', body: JSON.stringify({ name }) });
  const result = await response.json();
  if( result.error ) alert(result.error);
};

new EventSource('/events').onmessage = () => { catalog = []; refresh(); };
refresh();
</script>
</body>
</html>`;
}
