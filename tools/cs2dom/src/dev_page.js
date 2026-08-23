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
  button, select, input {
    font: 12px var(--mono); color: var(--ink); background: #241f19;
    border: 1px solid var(--line); border-radius: 3px; padding: 4px 8px;
  }
  button { cursor: pointer; }
  button:hover { border-color: var(--accent); }

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
  .box { position: absolute; overflow: hidden; }
  .box.text { display: flex; white-space: pre; }
  .box.model, .box.unknown {
    border: 1px dashed #4a4335; color: var(--dim);
    font: 10px var(--mono); display: flex; align-items: center; justify-content: center;
  }
  .box img { width: 100%; height: 100%; image-rendering: pixelated; }
  .box.outline { outline: 1px solid var(--accent); outline-offset: 0; }
  #stage.wire .box { outline: 1px solid rgba(226,169,63,.35); }

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
  <select id="pick"></select>
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

<script>
const state = {};            // "varp:300" -> value
let data = null;
let chosen = 0;

const $ = (id) => document.getElementById(id);

async function refresh() {
  const response = await fetch('/state?state=' + encodeURIComponent(JSON.stringify(state)));
  data = await response.json();
  render();
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

  const pick = $('pick');
  if( pick.options.length !== data.interfaces.length ||
      [...pick.options].some((o, i) => o.textContent !== data.interfaces[i].name) ) {
    pick.innerHTML = '';
    data.interfaces.forEach((iface, i) => {
      const option = document.createElement('option');
      option.value = i; option.textContent = iface.name;
      pick.appendChild(option);
    });
  }
  if( chosen >= data.interfaces.length ) chosen = 0;
  pick.value = String(chosen);

  const iface = data.interfaces[chosen];
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
  stage.className = $('wire').checked ? 'wire' : '';
  $('dims').textContent = width + '×' + height + ' — interface ' + iface.interfaceId;

  stage.innerHTML = '';
  const originX = root ? root.x : 0;
  const originY = root ? root.y : 0;

  for( const box of iface.boxes ) {
    const element = document.createElement('div');
    element.className = 'box ' + roleOf(box.type);
    element.style.left = (box.x - originX) + 'px';
    element.style.top = (box.y - originY) + 'px';
    element.style.width = box.w + 'px';
    element.style.height = box.h + 'px';
    if( box.props.hidden ) element.style.display = 'none';
    if( box.props.transparency ) element.style.opacity = String(1 - box.props.transparency / 255);
    element.title = box.name + ' — ' + box.kind + ' (file ' + box.fileId + ')';
    element.dataset.name = box.name;

    paint(element, box);
    stage.appendChild(element);
  }
}

function roleOf(type) {
  return ({ 0: 'layer', 2: 'inv', 3: 'rect', 4: 'text', 5: 'graphic', 6: 'model', 9: 'line' })[type] || 'unknown';
}

function paint(element, box) {
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
        image.src = '/sprite/' + props.sprite + '.png';
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
      element.textContent = 'model ' + props.model;
      break;
    case 'layer':
      break;
    default:
      element.textContent = box.kind;
  }
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
    empty.textContent = 'Nothing here reads host state — every prop is fixed at build ' +
                        'time, so this interface needs no scripts at all.';
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
  if( !(key in state) ) state[key] = fallback;
  return state[key];
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
  const contents = ensure(key, {});

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
        contents[obj] = Number(amount.value) || 0;
        refresh();
      };
      const drop = document.createElement('button');
      drop.textContent = '×';
      drop.onclick = () => { delete contents[obj]; redraw(); refresh(); };
      row.appendChild(amount);
      row.appendChild(drop);
      body.appendChild(row);
    }
    const add = document.createElement('button');
    add.textContent = '+ item';
    add.onclick = () => {
      const obj = prompt('item id');
      if( obj === null ) return;
      contents[Number(obj) || 0] = 1;
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

$('pick').onchange = (event) => { chosen = Number(event.target.value); render(); };
$('wire').onchange = () => render();
$('add').onclick = async () => {
  const name = prompt('component name (a file, ui/<name>.tsx)');
  if( !name ) return;
  const response = await fetch('/new', { method: 'POST', body: JSON.stringify({ name }) });
  const result = await response.json();
  if( result.error ) alert(result.error);
};

new EventSource('/events').onmessage = () => refresh();
refresh();
</script>
</body>
</html>`;
}
