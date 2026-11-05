'use strict';
/*
 * The map editor's chrome widgets in the canonical DOM runtime
 * (src/plugin_chrome/runtime.js): one page snapshot carrying every new kind,
 * checked for the controls it builds, then each control driven and checked
 * for the intent it reports -- the same commands the C executors send
 * (ui/torirs_chrome_exec.h) and the same intents they expect back.
 *
 * Node only, the fake DOM is the runtime test's shape plus createTextNode.
 */

const assert = require('assert');
const path = require('path');
const root = path.join(__dirname, '..', '..', 'plugin_chrome');
const { createRuntime } = require(path.join(root, 'runtime.js'));
const codec = require(path.join(root, 'codec-es3.js'));

function node(tag) {
  const value = {
    tagName: String(tag).toUpperCase(),
    children: [], parentNode: null, firstChild: null, lastChild: null,
    className: '', hidden: false, title: '', value: '', checked: false,
    clientWidth: 320, clientHeight: 500, scrollTop: 0, innerText: '',
    style: { setProperty(k, v) { this[k] = v; } },
    _attrs: {}
  };
  const relink = () => {
    value.firstChild = value.children[0] || null;
    value.lastChild = value.children[value.children.length - 1] || null;
  };
  value.appendChild = child => {
    child.parentNode = value; value.children.push(child); relink(); return child;
  };
  value.removeChild = child => {
    const at = value.children.indexOf(child);
    if (at >= 0) value.children.splice(at, 1);
    child.parentNode = null; relink(); return child;
  };
  value.insertBefore = (child, reference) => {
    const at = value.children.indexOf(reference);
    if (at < 0) return value.appendChild(child);
    child.parentNode = value; value.children.splice(at, 0, child); relink(); return child;
  };
  value.setAttribute = (k, v) => { value._attrs[k] = String(v); };
  value.getAttribute = k => Object.prototype.hasOwnProperty.call(value._attrs, k) ? value._attrs[k] : null;
  value.removeAttribute = k => { delete value._attrs[k]; };
  value.fire = (kind, event = {}) => {
    const delivered = Object.assign({ target: value, clientX: 8, clientY: 8, keyCode: 0,
      preventDefault() {}, stopPropagation() {} }, event);
    if (typeof value[`on${kind}`] === 'function') value[`on${kind}`](delivered);
  };
  value.focus = () => {};
  value.getBoundingClientRect = () => ({ left: 0, top: 0, width: 200, height: 100 });
  value.contains = child => { for (let at = child; at; at = at.parentNode) if (at === value) return true; return false; };
  return value;
}

function textOf(item) {
  if (item.nodeText !== undefined) return item.nodeText;
  return (item.innerText || '') + item.children.map(textOf).join('');
}

const ids = {};
const document = {
  documentElement: node('html'),
  activeElement: null,
  createElement: node,
  createTextNode(text) { const t = node('#text'); t.nodeText = String(text); return t; },
  getElementById(id) { return ids[id] || null; }
};
function add(id, tag, parent) {
  const item = node(tag); item.id = id; ids[id] = item;
  if (parent) parent.appendChild(item);
  return item;
}
const shell = add('tpc-shell', 'div');
const pane = add('tpc-pane', 'section', shell);
add('tpc-title', 'h1', pane);
add('tpc-close', 'button', pane);
add('tpc-tabs', 'nav', pane).hidden = true;
add('tpc-content', 'main', pane);
const railHost = node('nav');
shell.appendChild(railHost);
add('tpc-rail-list', 'div', railHost);
add('tpc-status', 'div');

const typed = [];
const global_ = {
  document, ToriRSPluginChromeCodec: codec, devicePixelRatio: 1,
  torirsPluginChromePostMessage() { return true; },
  torirsChromeIntentPosted(v) { typed.push(v); },
  setTimeout() { return 1; }, addEventListener() {}
};
const runtime = createRuntime(global_, document);
assert(runtime, 'runtime boots');

/* Enum values straight from the runtime's own export is not available, so the
 * numbers are the C header's; chrome_enum_sync_test.js pins the runtime to it. */
const CMD = { PANEL_OPEN: 3, WIDGET_ADD: 8, WIDGET_CHECKED: 12, PANEL_MODE: 21,
  WIDGET_VALUE: 22, WIDGET_ROW: 23, WIDGET_SPAN: 24, WIDGET_GRAPH: 25,
  WIDGET_GRAPH_ITEM: 26, WIDGET_TOOLTIP: 27, WIDGET_CHORD: 28, SCALE: 29 };
const W = { MODELVIEW: 6, BUTTON: 7, TREE: 13, VLIST: 14, ICONBUTTON: 15, SLIDER: 16, RADIO: 17, CODE: 18, GRAPH: 19 };
const INTENT = { ACTIVATE: 1, TOGGLE: 3, PICK: 5, EXPAND: 10, SCROLL: 11, VALUE: 12,
  CODE_EDIT: 14, GRAPH: 15, GRAPH_WHEEL: 16, GESTURE: 17 };
const GESTURE = { PAN: 1, PINCH: 2, TWIST: 3 };
function cmd(k, values) {
  return Object.assign({ k, p: 3, w: -1, tab: -1, v: 0, c: 0, x: 0, y: 0, cw: 0, ch: 0,
    label: '', text: '', detail: '', s: 0, b: -1 }, values || {});
}

runtime.receive({
  protocol: 1, type: 'rail.snapshot', registryRevision: 1, selectionGeneration: 8,
  pageGeneration: 20, activePlugin: 0, lastSelectedPlugin: 0, selectedEntry: 0,
  expanded: true, entries: [{ kind: 1, pluginIndex: -2, preferredWidth: 320, title: 'Manage',
    iconAsset: '', badge: '', attention: false }]
});
const commands = [
  cmd(CMD.PANEL_OPEN, { text: 'Editor' }),
  cmd(CMD.PANEL_MODE, { v: 1, x: 0 }),
  cmd(CMD.WIDGET_ADD, { w: 1, v: W.TREE, label: 'interface 160', cw: 2, s: 201 }),
  cmd(CMD.WIDGET_VALUE, { w: 1, v: 1 }),
  cmd(CMD.WIDGET_ADD, { w: 2, v: W.VLIST, label: 'Results', ch: 4, s: 202 }),
  cmd(CMD.WIDGET_VALUE, { w: 2, v: -1, x: 10, y: 5000 }),
  cmd(CMD.WIDGET_ROW, { w: 2, v: 10, text: 'loc_10' }),
  cmd(CMD.WIDGET_ROW, { w: 2, v: 11, text: 'loc_11' }),
  cmd(CMD.WIDGET_ADD, { w: 3, v: W.ICONBUTTON, label: 'Raise', cw: 31, s: 203 }),
  cmd(CMD.WIDGET_VALUE, { w: 3, v: 31 }),
  cmd(CMD.WIDGET_CHECKED, { w: 3, v: 1 }),
  cmd(CMD.WIDGET_TOOLTIP, { w: 3, label: 'Raise terrain' }),
  cmd(CMD.WIDGET_CHORD, { w: 3, label: 'R' }),
  cmd(CMD.WIDGET_ADD, { w: 4, v: W.SLIDER, label: 'Radius', s: 204 }),
  cmd(CMD.WIDGET_VALUE, { w: 4, v: 4, x: 1, y: 16, cw: 1 }),
  cmd(CMD.WIDGET_ADD, { w: 5, v: W.RADIO, label: 'Level 0', cw: 1, s: 205 }),
  cmd(CMD.WIDGET_ADD, { w: 6, v: W.RADIO, label: 'Level 1', cw: 1, s: 206 }),
  cmd(CMD.WIDGET_CHECKED, { w: 5, v: 1 }),
  cmd(CMD.WIDGET_ADD, { w: 7, v: W.CODE, ch: 3, s: 207 }),
  cmd(CMD.WIDGET_VALUE, { w: 7, v: 0, x: 0, y: 3 }),
  cmd(CMD.WIDGET_ROW, { w: 7, v: 0, text: '[opnpc1,hans]' }),
  cmd(CMD.WIDGET_SPAN, { w: 7, v: 0, x: 1, y: 6, c: 0xff981f }),
  cmd(CMD.WIDGET_ROW, { w: 7, v: 1, text: 'mes("hi");' }),
  cmd(CMD.WIDGET_ADD, { w: 8, v: W.GRAPH, label: 'Surface', ch: 100, s: 208 }),
  cmd(CMD.WIDGET_GRAPH, { w: 8, v: 3 }),
  cmd(CMD.WIDGET_GRAPH_ITEM, { w: 8, tab: 0, x: 0, y: 0, cw: 20, ch: 20, c: 0x406020, v: 77 }),
  cmd(CMD.WIDGET_GRAPH_ITEM, { w: 8, tab: 2, x: 0, y: 0, cw: 30, ch: 40, c: 0xffff00 }),
  cmd(CMD.WIDGET_GRAPH_ITEM, { w: 8, tab: 3, x: 2, y: 30, c: 0xffffff, label: 'm50_50' }),
  cmd(CMD.WIDGET_ADD, { w: 9, v: W.BUTTON, text: 'Build the lane', s: 209 }),
  cmd(CMD.WIDGET_CHORD, { w: 9, label: 'Ctrl+B' }),
  cmd(CMD.WIDGET_TOOLTIP, { w: 9, label: 'Pack the scripts', text: 'F5' }),
  cmd(CMD.WIDGET_ADD, { w: 10, v: W.MODELVIEW, cw: 120, ch: 96, s: 210 })
];
runtime.receive({ protocol: 1, type: 'page.snapshot', pageGeneration: 20, panel: 3,
  title: 'Editor', checkStyle: 0, commands });
const rows = ids['tpc-content'].children;
const byHandle = h => rows.find(r => r._tpcRecord && r._tpcRecord.handle === h)._tpcRecord;
assert.strictEqual(runtime.inspect().widgetCount, 10, 'every editor kind is retained');
assert(/tpc-modal/.test(shell.className), 'PANEL_MODE modal puts the page over a veil');

/* TREE */
const tree = byHandle(1);
assert.strictEqual(tree.row.style.paddingLeft, '24px', 'depth indents 12px a level');
assert.match(tree.expander.style.backgroundImage, /TreeClosed\.png/, 'a closed expander');
tree.expander.fire('click');
assert.deepStrictEqual([typed[typed.length - 1].k, typed[typed.length - 1].v], [INTENT.EXPAND, 2],
  'the expander reports EXPAND to open');
tree.row.fire('click');
assert.strictEqual(typed[typed.length - 1].k, INTENT.ACTIVATE, 'the row reports ACTIVATE');

/* VLIST */
const list = byHandle(2);
assert.strictEqual(list.control.style.height, '80px', 'four 20px rows');
assert.strictEqual(list.spacer.style.height, '100000px', 'the scroll range is the whole count');
assert.deepStrictEqual(list.lineNodes.map(n => n.innerText), ['loc_10', 'loc_11', '', ''],
  'the window shows the rows it was sent, blank for the rest');
assert.strictEqual(list.control.scrollTop, 200, 'scrolled to the model\'s first row');
list.control.scrollTop = 400;
list.control.fire('scroll');
assert.deepStrictEqual([typed[typed.length - 1].k, typed[typed.length - 1].v], [INTENT.SCROLL, 20],
  'a user scroll reports the new first row');
list.control.fire('click', { clientY: 30 });
assert.deepStrictEqual([typed[typed.length - 1].k, typed[typed.length - 1].v], [INTENT.PICK, 21],
  'a click picks by absolute row');

/* ICONBUTTON + TOOLTIP */
const icon = byHandle(3);
assert.match(icon.icon.style.backgroundImage, /ToolRaise\.png/, 'the slot names the packaged skin PNG');
assert.strictEqual(icon.control.getAttribute('aria-pressed'), 'true', 'pressed state');
assert.strictEqual(icon.row.title, 'Raise terrain  [R]',
  'the tooltip is the hover text, ending with the chord the button wears');
assert.strictEqual(icon.badge.tagName, 'KBD', 'the chord badge is a key');
assert.strictEqual(icon.badge.className, 'tpc-chord-badge', 'styled as the badge');
assert.strictEqual(icon.badge.innerText, 'R', 'WIDGET_CHORD fills the badge');
assert.strictEqual(icon.badge.parentNode, icon.control, 'inside the button, over its corner');
assert(!icon.badge.hidden, 'a chord shows its badge');
icon.control.fire('click');
assert.strictEqual(typed[typed.length - 1].k, INTENT.ACTIVATE, 'an icon button activates');

/* SLIDER */
const slider = byHandle(4);
assert.deepStrictEqual([slider.control.min, slider.control.max, slider.control.value], ['1', '16', '4'],
  'range and value from WIDGET_VALUE');
slider.control.value = '9';
slider.control.fire('change');
assert.deepStrictEqual([typed[typed.length - 1].k, typed[typed.length - 1].v], [INTENT.VALUE, 9],
  'a moved slider reports VALUE');

/* RADIO */
const r0 = byHandle(5);
const r1 = byHandle(6);
assert(r0.control.checked && !r1.control.checked && r0.control.name === r1.control.name,
  'one group, one chosen');
r1.control.checked = true;
r1.control.fire('change');
assert.deepStrictEqual([typed[typed.length - 1].k, typed[typed.length - 1].v, typed[typed.length - 1].w],
  [INTENT.TOGGLE, 1, 6], 'choosing a radio reports TOGGLE on');

/* CODE */
const code = byHandle(7);
const line0 = code.lineNodes[0];
assert.strictEqual(line0.children[0].innerText, '1', 'a gutter of line numbers');
assert.strictEqual(textOf(line0.children[1]), '[opnpc1,hans]', 'the line text');
assert.strictEqual(line0.children[1].children[1].style.color, '#ff981f', 'a span colours its run');
line0.fire('click');
const editor = code.lineNodes[0].lastChild;
assert.strictEqual(editor.tagName, 'INPUT', 'a clicked line becomes editable');
editor.value = '[opnpc1,hans2]';
editor.fire('change');
let last = typed[typed.length - 1];
assert.deepStrictEqual([last.k, last.v, last.x, last.text], [INTENT.CODE_EDIT, 0, 7, '[opnpc1,hans2]'],
  'an edit reports CODE_EDIT REPLACE with the line and its text');
editor.selectionStart = 3; editor.selectionEnd = 3;
editor.fire('keydown', { keyCode: 13 });
last = typed[typed.length - 1];
assert.deepStrictEqual([last.k, last.v, last.x, last.y], [INTENT.CODE_EDIT, 0, 8, 3],
  'Enter reports a SPLIT at the caret');

/* GRAPH */
const graph = byHandle(8);
assert.strictEqual(graph.control.style.height, '100px', 'the well height');
assert.strictEqual(graph.control.children.length, 3, 'one node per item');
assert.strictEqual(graph.control.children[0].getAttribute('data-key'), '77', 'rects keep their key');
assert.strictEqual(textOf(graph.control.children[2]), 'm50_50', 'captions are text');
graph.control.fire('mousedown', { clientX: 5, clientY: 6 });
last = typed[typed.length - 1];
assert.deepStrictEqual([last.k, last.v, last.x, last.y], [INTENT.GRAPH, 1, 5, 6], 'a press reports GRAPH PRESS');
graph.control.fire('mousemove', { clientX: 9, clientY: 9 });
assert.strictEqual(typed[typed.length - 1].v, 2, 'moving while pressed is a DRAG');
graph.control.fire('mouseup', { clientX: 9, clientY: 9 });
assert.strictEqual(typed[typed.length - 1].v, 3, 'and the release');
graph.control.fire('mousemove', { clientX: 3, clientY: 3 });
assert.deepStrictEqual([typed[typed.length - 1].v, typed[typed.length - 1].x], [5, 3],
  'entering a keyed cell is one HOVER');
const before = typed.length;
graph.control.fire('mousemove', { clientX: 4, clientY: 4 });
assert.strictEqual(typed.length, before, 'hovering inside the key it already names sends nothing');
graph.control.fire('mousemove', { clientX: 50, clientY: 50 });
assert.strictEqual(typed[typed.length - 1].v, 5, 'leaving the keyed cell is one HOVER');
graph.control.fire('wheel', { clientX: 5, clientY: 5, deltaY: 120 });
assert.deepStrictEqual([typed[typed.length - 1].k, typed[typed.length - 1].v], [INTENT.GRAPH_WHEEL, 1],
  'the wheel reports GRAPH_WHEEL');

/* Gesture frames (workbench 6.1): ctrl+wheel is a continuous pinch about the
 * pointer, a touch pair pans, pinches and twists -- GESTURE intents the model
 * routes by the canvas's own rules, "dx dy fingers scale_q16 radians_q16 phase". */
const gestureNumbers = intent => intent.text.split(' ').map(Number);
graph.control.fire('wheel', { clientX: 5, clientY: 6, deltaY: -100, ctrlKey: true });
last = typed[typed.length - 1];
assert.deepStrictEqual([last.k, last.v, last.x, last.y], [INTENT.GESTURE, GESTURE.PINCH, 5, 6],
  'ctrl+wheel over a well is a PINCH about the pointer');
assert.strictEqual(gestureNumbers(last)[3], Math.round(1.25 * 65536), 'a notch is x1.25, as on the canvas');
graph.control.fire('touchstart', { touches: [{ clientX: 10, clientY: 10 }, { clientX: 30, clientY: 10 }] });
const touchesBefore = typed.length;
graph.control.fire('touchmove', { touches: [{ clientX: 10, clientY: 10 }, { clientX: 30, clientY: 10 }] });
assert.strictEqual(typed.length, touchesBefore, 'the first pair only sets the baseline');
graph.control.fire('touchmove', { touches: [{ clientX: 14, clientY: 14 }, { clientX: 54, clientY: 14 }] });
const pairFrames = typed.slice(touchesBefore);
assert.deepStrictEqual(pairFrames.map(i => [i.k, i.v]),
  [[INTENT.GESTURE, GESTURE.PAN], [INTENT.GESTURE, GESTURE.PINCH], [INTENT.GESTURE, GESTURE.TWIST]],
  'a moving pair is a PAN, a PINCH and a TWIST');
assert.deepStrictEqual(gestureNumbers(pairFrames[0]).slice(0, 3), [14, 4, 2],
  'the pan is the midpoint\'s travel, two fingers');
assert.deepStrictEqual([pairFrames[1].x, pairFrames[1].y], [34, 14], 'about the midpoint');
assert.strictEqual(gestureNumbers(pairFrames[1])[3], 2 * 65536, 'the spread doubled: x2');
graph.control.fire('touchend', { touches: [] });
assert.strictEqual(gestureNumbers(typed[typed.length - 1])[5], 2, 'lifting ends the gesture');
{
  /* A tap stays the browser's click: one finger's start and end are not
   * swallowed, and post nothing. */
  let prevented = 0;
  const tapBefore = typed.length;
  graph.control.fire('touchstart', { touches: [{ clientX: 5, clientY: 5 }], preventDefault() { prevented++; } });
  graph.control.fire('touchend', { touches: [], preventDefault() { prevented++; } });
  assert.strictEqual(prevented, 0, 'a tap is not swallowed: the browser still clicks');
  assert.strictEqual(typed.length, tapBefore, 'and a tap posts no gesture');
}

/* BUTTON + chord: a caption span and a KBD keycap inside the button. */
const button = byHandle(9);
assert(/tpc-has-chord/.test(button.control.className), 'a button with a chord says so');
assert.strictEqual(button.control.children.length, 2, 'caption, then key');
assert.strictEqual(button.captionNode.className, 'tpc-button-caption', 'the caption span ellipses');
assert.strictEqual(button.captionNode.innerText, 'Build the lane', 'the caption');
assert.strictEqual(button.keycap.tagName, 'KBD', 'the keycap is a KBD');
assert.strictEqual(button.keycap.className, 'tpc-keycap', 'styled as the keycap');
assert.strictEqual(button.keycap.innerText, 'Ctrl+B', 'WIDGET_CHORD fills the key');
assert.strictEqual(button.row.title, 'Pack the scripts  [F5]', 'its own tooltip chord wins');
button.control.fire('click');
assert.deepStrictEqual([typed[typed.length - 1].k, typed[typed.length - 1].w], [INTENT.ACTIVATE, 9],
  'a button with a key still activates');

/* MODELVIEW: one finger orbits (a PAN the model turns into VIEW_ORBIT). */
const model = byHandle(10);
model.control.fire('touchstart', { touches: [{ clientX: 20, clientY: 20 }] });
model.control.fire('touchmove', { touches: [{ clientX: 20, clientY: 20 }] });
model.control.fire('touchmove', { touches: [{ clientX: 26, clientY: 17 }] });
last = typed[typed.length - 1];
assert.deepStrictEqual([last.k, last.w, last.v], [INTENT.GESTURE, 10, GESTURE.PAN],
  'a finger on a model view is a PAN');
assert.deepStrictEqual(gestureNumbers(last).slice(0, 3), [6, -3, 1], 'by its travel, one finger');
model.control.fire('wheel', { clientX: 20, clientY: 20, deltaY: 100, ctrlKey: true });
last = typed[typed.length - 1];
assert.deepStrictEqual([last.k, last.v], [INTENT.GESTURE, GESTURE.PINCH], 'ctrl+wheel zooms a model view');

/* Deltas update in place. */
runtime.receive({ protocol: 1, type: 'page.delta', pageGeneration: 20, commands: [
  cmd(CMD.WIDGET_VALUE, { w: 1, v: 2 }),
  cmd(CMD.WIDGET_VALUE, { w: 2, v: 11, x: 10, y: 5000 }),
  cmd(CMD.WIDGET_TOOLTIP, { w: 9, label: 'Pack the scripts', text: '' }),
  cmd(CMD.WIDGET_CHORD, { w: 3, label: '' })
] });
assert.strictEqual(button.row.title, 'Pack the scripts  [Ctrl+B]',
  'with no chord of its own the tooltip ends with the keycap\'s');
assert(icon.badge.hidden, 'a cleared chord hides the badge');
assert.strictEqual(icon.row.title, 'Raise terrain', 'and drops out of the tooltip');
runtime.receive({ protocol: 1, type: 'page.delta', pageGeneration: 20, commands: [
  cmd(CMD.WIDGET_CHORD, { w: 9, label: '' })
] });
assert.strictEqual(button.keycap, null, 'a cleared chord takes the key away');
assert(!/tpc-has-chord/.test(button.control.className), 'and the class');
assert.strictEqual(button.control.children.length, 0, 'the button holds its caption as text again');
assert.strictEqual(button.control.innerText, 'Build the lane', 'the caption');
assert.strictEqual(button.row.title, 'Pack the scripts', 'the tooltip has no chord left to end with');
assert.match(tree.expander.style.backgroundImage, /TreeOpen\.png/, 'VALUE opens the tree row in place');
assert(/tpc-selected/.test(list.lineNodes[1].className), 'VALUE selects the list row in place');

/* A long caption with no chord: held whole as the button's text, which both
 * stylesheets end in an ellipsis -- as the in-canvas chrome elides a
 * chord-less caption that does not fit (chrome_widget_sync_button.c) --
 * rather than clip it mid-glyph. */
runtime.receive({ protocol: 1, type: 'page.delta', pageGeneration: 20, commands: [
  cmd(11 /* WIDGET_TEXT */, { w: 9, text: 'Rebuild every collision map in the square' })
] });
assert.strictEqual(button.control.children.length, 0, 'a chord-less long caption is plain text');
assert.strictEqual(button.control.innerText, 'Rebuild every collision map in the square',
  'held whole: the stylesheet ellipses it, the runtime does not cut it');
{
  const fs = require('fs');
  /* Every declaration of the rules whose selector list is exactly `selector`. */
  const declarations = (file, selector) => {
    const css = fs.readFileSync(path.join(root, file), 'utf8').replace(/\/\*[\s\S]*?\*\//g, '');
    const out = [];
    const rule = /([^{}]+)\{([^{}]*)\}/g;
    for (let m = rule.exec(css); m; m = rule.exec(css))
      if (m[1].trim().replace(/\s+/g, ' ') === selector) out.push(m[2]);
    return out.join(';');
  };
  for (const file of ['modern.css', 'legacy-ie8.css']) {
    const plain = declarations(file, '.tpc-button, .tpc-menu');
    assert.match(plain, /overflow:\s*hidden/, `${file}: a plain button hides what overflows`);
    assert.match(plain, /text-overflow:\s*ellipsis/, `${file}: and ends it in an ellipsis`);
    assert.match(plain, /white-space:\s*nowrap/, `${file}: on one line`);
    assert.match(declarations(file, '.tpc-button-caption'), /text-overflow:\s*ellipsis/,
      `${file}: a chord button's caption span ellipses`);
    assert.match(declarations(file, '.tpc-chord-badge'), /text-overflow:\s*ellipsis/,
      `${file}: an over-wide badge ellipses rather than cuts`);
  }
}

/* SKIN TIERS. The runtime draws a packaged slot from skin@2x/ or skin@3x/
 * (tools/chrome_skin_tiers.py) when the host staged them (theme.skinTiers)
 * and the display needs them: the smallest tier whose pixels cover the drawn
 * CSS size times devicePixelRatio, sized back to the authored CSS size. The
 * sizes it sizes back to are pinned to the PNGs the tools wrote. */
{
  const fs = require('fs');
  const res = path.join(__dirname, '..', '..', '..', 'res', 'plugin_chrome');
  const pngSize = (dir, name) => {
    const bytes = fs.readFileSync(path.join(res, dir, `${name}.png`));
    return [bytes.readUInt32BE(16), bytes.readUInt32BE(20)];
  };
  const html = document.documentElement;
  const button = byHandle(9);
  const radio = byHandle(5);
  const iconAt = () => /"([^"]+)"/.exec(icon.icon.style.backgroundImage)[1];
  let revision = 40;
  const theme = (skinTiers, ratio) => {
    global_.devicePixelRatio = ratio;
    const message = { protocol: 1, type: 'theme', revision: ++revision, assets: {
      panelBody: 'skin/PanelBody.png', buttonLeft: 'skin/ButtonLeft.png',
      buttonMiddle: 'skin/ButtonMid.png', buttonRight: 'skin/ButtonRight.png',
      checkOn: 'skin/CheckOn.png', checkOff: 'skin/CheckOff.png', close: 'skin/CloseButton.png',
      frameTop: 'skin/FrameTop.png' } };
    if (skinTiers !== undefined) message.skinTiers = skinTiers;
    runtime.receive(message);
  };

  /* A host that staged no tiers: the 1x files at every ratio, no tier class. */
  theme(undefined, 3);
  assert.strictEqual(iconAt(), 'skin/ToolRaise.png', 'no skinTiers: the 1x file even at DPR 3');
  assert(!/tpc-skin-tiers/.test(html.className), 'no skinTiers: the stylesheet asks for no tier');
  assert.match(button.control.style.backgroundImage, /^url\("skin\/ButtonLeft\.png"\)/,
    'no skinTiers: the button caps stay 1x');

  /* Staged: the tier follows the ratio, the nearest at or above it. */
  const cases = [[1, 'skin/'], [1.25, 'skin@2x/'], [1.5, 'skin@2x/'], [2, 'skin@2x/'],
    [2.5, 'skin@3x/'], [3, 'skin@3x/'], [4, 'skin@3x/']];
  for (const [ratio, dir] of cases) {
    theme(3, ratio);
    assert.strictEqual(iconAt(), `${dir}ToolRaise.png`, `DPR ${ratio} draws the icon from ${dir}`);
    assert.strictEqual(icon.icon.style.backgroundSize, '16px 16px',
      `DPR ${ratio}: the icon is sized back to its 16px 1x size`);
    assert.match(radio.control.style.backgroundImage, new RegExp(`"${dir}RadioOn\\.png"`),
      `DPR ${ratio}: the radio mark too`);
    assert.match(tree.expander.style.backgroundImage, new RegExp(`"${dir}Tree(?:Open|Closed)\\.png"`),
      `DPR ${ratio}: the tree arrow too`);
  }
  assert(/tpc-skin-tiers/.test(html.className), 'skinTiers 3 turns on the image-set rules');
  assert.match(ids['tpc-pane'].style.backgroundImage, /"skin@3x\/PanelBody\.png"/,
    'the pane tile is drawn from its tier');
  assert.strictEqual(ids['tpc-pane'].style.backgroundSize, '88px 60px',
    'and tiles at its 1x size');

  /* The button caps are 36px art drawn at 18: 1x art covers DPR 2, the 2x
   * tier covers DPR 3, and nothing draws 1x art stretched past its pixels. */
  theme(3, 2);
  assert.match(button.control.style.backgroundImage,
    /^url\("skin\/ButtonLeft\.png"\),url\("skin\/ButtonRight\.png"\),url\("skin\/ButtonMid\.png"\)$/,
    'DPR 2: the 36px caps already cover an 18px cap');
  theme(3, 3);
  assert.match(button.control.style.backgroundImage,
    /^url\("skin@2x\/ButtonLeft\.png"\),url\("skin@2x\/ButtonRight\.png"\),url\("skin@2x\/ButtonMid\.png"\)$/,
    'DPR 3: the caps come from the 2x tier');
  assert.strictEqual(button.control.style.backgroundSize, '18px 18px,18px 18px,10px 18px',
    'at their authored size');

  /* Every slot: an ICONBUTTON per slot, its drawn size the 1x PNG's, and the
   * tier files exactly twice and three times it. */
  const slots = [];
  for (let slot = 0; slot < 58; slot++) {
    slots.push(cmd(CMD.WIDGET_ADD, { w: 500 + slot, v: W.ICONBUTTON, label: 'S', cw: slot, s: 600 + slot }));
    slots.push(cmd(CMD.WIDGET_VALUE, { w: 500 + slot, v: slot }));
  }
  runtime.receive({ protocol: 1, type: 'page.delta', pageGeneration: 20, commands: slots });
  theme(3, 1.5);
  for (let slot = 0; slot < 58; slot++) {
    const record = byHandle(500 + slot);
    const file = /"skin@2x\/([A-Za-z]+)\.png"/.exec(record.icon.style.backgroundImage);
    assert(file, `slot ${slot} is drawn from the 2x tier at DPR 1.5`);
    const [w, h] = pngSize('skin', file[1]);
    assert.strictEqual(record.icon.style.backgroundSize, `${w}px ${h}px`,
      `slot ${slot} (${file[1]}) is sized back to its 1x PNG`);
    assert.deepStrictEqual(pngSize('skin@2x', file[1]), [w * 2, h * 2], `${file[1]} 2x is twice 1x`);
    assert.deepStrictEqual(pngSize('skin@3x', file[1]), [w * 3, h * 3], `${file[1]} 3x is three times`);
  }

  /* A later theme without tiers takes them all back. */
  theme(1, 3);
  assert.strictEqual(iconAt(), 'skin/ToolRaise.png', 'skinTiers 1: back to the 1x file');
  assert(!/tpc-skin-tiers/.test(html.className), 'and the tier rules off');

  /* The chrome's zoom is drawn through as well: the tier covers
   * devicePixelRatio TIMES the zoom, and a zoom change repaints. The C side
   * has already divided the page's own ratio out of SCALE
   * (ToriRSChrome_SetPageScale), so the two never count the display twice. */
  const scale = (v, d) => runtime.receive({ protocol: 1, type: 'page.delta', pageGeneration: 20,
    commands: [cmd(CMD.SCALE, { p: -1, v, cw: d })] });
  theme(3, 1);
  assert.strictEqual(iconAt(), 'skin/ToolRaise.png', 'DPR 1, no zoom: the 1x file');
  assert(!/tpc-dpr-fractional/.test(html.className), 'DPR 1, no zoom: a whole scale, pixelated');
  scale(8, 4);
  assert.strictEqual(shell.style.zoom, '2', 'View 200 % on a 1x page zooms the shell 2x');
  assert.strictEqual(iconAt(), 'skin@2x/ToolRaise.png', 'and repaints the icon from the 2x tier, not 1x blown up');
  assert(!/tpc-dpr-fractional/.test(html.className), '2 is a whole scale: pixelated stays');
  scale(5, 4);
  assert.strictEqual(iconAt(), 'skin@2x/ToolRaise.png', 'a 1.25x zoom: the tier above it');
  assert(/tpc-dpr-fractional/.test(html.className), '1.25 is fractional: the page lets the browser resample');
  /* Auto on a 2x page: the C side says 8 / 8, no zoom, and the page is the
   * physical size the canvas chrome is -- never 2x on top of DPR 2. */
  theme(3, 2);
  scale(8, 8);
  assert.strictEqual(shell.style.zoom || '', '', 'Auto on a DPR-2 page (SCALE 8 / 8) draws the shell unzoomed');
  assert.strictEqual(iconAt(), 'skin@2x/ToolRaise.png', 'from the 2x tier, for the browser\'s own 2x');
  assert(!/tpc-dpr-fractional/.test(html.className), 'DPR 2 unzoomed is whole');
  /* View 100 % on that 2x page: half the browser's 2x. */
  scale(4, 8);
  assert.strictEqual(shell.style.zoom, '0.5', 'View 100 % on a DPR-2 page halves the zoom');
  assert.strictEqual(iconAt(), 'skin/ToolRaise.png', 'and the 1x art covers it');
  /* A 1.5x display at Auto: 6 / 6, unzoomed, and fractional all the same. */
  theme(3, 1.5);
  scale(6, 6);
  assert.strictEqual(shell.style.zoom || '', '', 'Auto on a DPR-1.5 page is unzoomed');
  assert(/tpc-dpr-fractional/.test(html.className), 'but DPR 1.5 is fractional: no pixelated');
  scale(4, 4);
  theme(1, 1);
  assert(!/tpc-dpr-fractional/.test(html.className), 'back to 1x, the class is gone');
  global_.devicePixelRatio = 1;
}

/* The downlevel page has no flex: the caption keeps clear of the absolutely
 * placed key by padding sized from the key itself, not a fixed 40px. */
{
  const legacyIds = {};
  const legacyDocument = {
    documentElement: node('html'), activeElement: null, createElement: node,
    createTextNode(t) { const n = node('#text'); n.nodeText = String(t); return n; },
    getElementById(id) { return legacyIds[id] || null; }
  };
  const put = (id, tag, parent) => {
    const item = node(tag); item.id = id; legacyIds[id] = item;
    if (parent) parent.appendChild(item);
    return item;
  };
  const legacyShell = put('tpc-shell', 'div');
  legacyShell.setAttribute('data-tpc-legacy', '1');
  const legacyPane = put('tpc-pane', 'section', legacyShell);
  put('tpc-title', 'h1', legacyPane);
  put('tpc-close', 'button', legacyPane);
  put('tpc-tabs', 'nav', legacyPane).hidden = true;
  put('tpc-content', 'main', legacyPane);
  const legacyRail = node('nav');
  legacyShell.appendChild(legacyRail);
  put('tpc-rail-list', 'div', legacyRail);
  put('tpc-status', 'div');
  const legacy = createRuntime(Object.assign({}, global_, { document: legacyDocument }), legacyDocument);
  assert(legacy, 'the legacy runtime boots');
  legacy.receive({
    protocol: 1, type: 'rail.snapshot', registryRevision: 1, selectionGeneration: 8,
    pageGeneration: 20, activePlugin: 0, lastSelectedPlugin: 0, selectedEntry: 0,
    expanded: true, entries: [{ kind: 1, pluginIndex: -2, preferredWidth: 320, title: 'Manage',
      iconAsset: '', badge: '', attention: false }]
  });
  legacy.receive({ protocol: 1, type: 'page.snapshot', pageGeneration: 20, panel: 3,
    title: 'Editor', checkStyle: 0, commands: [
      cmd(CMD.PANEL_OPEN, { text: 'Editor' }),
      cmd(CMD.WIDGET_ADD, { w: 1, v: W.BUTTON, text: 'Redo', s: 301 }),
      cmd(CMD.WIDGET_CHORD, { w: 1, label: 'Ctrl+Shift+Z' }),
      cmd(CMD.WIDGET_ADD, { w: 2, v: W.BUTTON, text: 'Copy', s: 302 }),
      cmd(CMD.WIDGET_CHORD, { w: 2, label: 'C' })
    ] });
  const legacyRows = legacyIds['tpc-content'].children;
  const legacyHandle = h => legacyRows.find(r => r._tpcRecord && r._tpcRecord.handle === h)._tpcRecord;
  const redo = legacyHandle(1);
  const copy = legacyHandle(2);
  const pad = record => parseInt(record.control.style.paddingRight, 10);
  assert(redo.keycap && pad(redo) > 40, `a wide key gets room past the old fixed 40px (${redo.control.style.paddingRight})`);
  assert(pad(redo) >= 'Ctrl+Shift+Z'.length * 6 + 3 + 4, 'room for the whole key, its inset and its gap');
  assert(pad(copy) < pad(redo), 'a narrow key takes less');
  legacy.receive({ protocol: 1, type: 'page.delta', pageGeneration: 20, commands: [
    cmd(CMD.WIDGET_CHORD, { w: 1, label: '' })
  ] });
  assert.strictEqual(redo.control.style.paddingRight, '', 'a cleared chord gives the padding back');
}

/* SCALE: the chrome scale as a rational; the page zooms its shell by it,
 * keeps it across a page snapshot, and drops the zoom again at 1x. */
{
  assert.strictEqual(shell.style.zoom || '', '', 'a page never told its scale is at 1x');
  runtime.receive({ protocol: 1, type: 'page.delta', pageGeneration: 20, commands: [
    cmd(CMD.SCALE, { p: -1, v: 6, cw: 4 })
  ] });
  assert.strictEqual(shell.style.zoom, '1.5', 'SCALE 6 / 4 zooms the shell to 1.5');
  assert.strictEqual(runtime.inspect().scaleQ, 6, 'and the runtime holds the rational');
  assert.strictEqual(runtime.inspect().scaleDen, 4, 'over its denominator');
  runtime.receive({ protocol: 1, type: 'page.snapshot', pageGeneration: 21, panel: 3,
    title: 'Editor', checkStyle: 0, commands: [cmd(CMD.PANEL_OPEN, { text: 'Editor' })] });
  assert.strictEqual(shell.style.zoom, '1.5', 'a new page keeps the chrome scale');
  runtime.receive({ protocol: 1, type: 'page.delta', pageGeneration: 21, commands: [
    cmd(CMD.SCALE, { p: -1, v: 5, cw: 4 })
  ] });
  assert.strictEqual(shell.style.zoom, '1.25', '5 / 4 is 1.25');
  runtime.receive({ protocol: 1, type: 'page.delta', pageGeneration: 21, commands: [
    cmd(CMD.SCALE, { p: -1, v: 4, cw: 4 })
  ] });
  assert.strictEqual(shell.style.zoom, '', 'back at 1x the zoom is gone');
  runtime.receive({ protocol: 1, type: 'page.snapshot', pageGeneration: 22, panel: 3,
    title: 'Editor', checkStyle: 0, scaleQ: 7, scaleDen: 4,
    commands: [cmd(CMD.PANEL_OPEN, { text: 'Editor' })] });
  assert.strictEqual(shell.style.zoom, '1.75', 'a snapshot that carries the scale applies it');
}

/* VLIST cells (workbench 6.2): WIDGET_ADD's shape says the rows are cells;
 * each row draws a picture well, the name, the dim part, its keycaps and the
 * id, the caption is a header in the same columns, and a keycap's click is a
 * PICK whose x is the cell (the row body's click stays x 0). */
{
  const US = '\x1f';
  runtime.receive({ protocol: 1, type: 'page.snapshot', pageGeneration: 23, panel: 3,
    title: 'Catalog', checkStyle: 0, commands: [
      cmd(CMD.PANEL_OPEN, { text: 'Catalog' }),
      cmd(CMD.WIDGET_ADD, { w: 1, v: W.VLIST, label: `${US}name${US}${US}shapes${US}id`, cw: 1, ch: 4, s: 301 }),
      cmd(CMD.WIDGET_VALUE, { w: 1, v: -1, x: 0, y: 212 }),
      cmd(CMD.WIDGET_ROW, { w: 1, v: 0, text: `9${US}fence_wood${US}Fence${US}0 2 9${US}980` }),
      cmd(CMD.WIDGET_ROW, { w: 1, v: 1, text: `${US}fence_wood_corner${US}${US}3${US}981` }),
      cmd(CMD.WIDGET_ADD, { w: 2, v: W.VLIST, label: 'Plain', ch: 2, s: 302 }),
      cmd(CMD.WIDGET_VALUE, { w: 2, v: -1, x: 0, y: 2 }),
      cmd(CMD.WIDGET_ROW, { w: 2, v: 0, text: `a${US}b` })
    ] });
  const cellRows = ids['tpc-content'].children;
  const cellHandle = h => cellRows.find(r => r._tpcRecord && r._tpcRecord.handle === h)._tpcRecord;
  const cells = cellHandle(1);
  const plainList = cellHandle(2);
  assert(cells.cells && !plainList.cells, 'the shape bit makes a cells list, and only that one');
  const head = cells.row.children.find(c => /tpc-vlist-head/.test(c.className));
  assert(head, 'a cells caption is a header row');
  assert.deepStrictEqual(head.children.map(c => c.className),
    ['tpc-cell-names', 'tpc-cell-keys', 'tpc-cell-right'], 'the header has the columns, no picture');
  assert.strictEqual(textOf(head), 'nameshapesid', 'the header says the columns');
  const first = cells.lineNodes[0];
  assert(/tpc-cells/.test(first.className), 'a row is a cells row');
  assert.deepStrictEqual(first.children.map(c => c.className),
    ['tpc-cell-thumb', 'tpc-cell-names', 'tpc-cell-keys', 'tpc-cell-right'],
    'picture, names, keycaps, id');
  assert.strictEqual(textOf(first.children[1]), 'fence_wood Fence', 'the name, then the display name');
  assert.strictEqual(first.children[1].children[1].className, 'tpc-cell-dim', 'the display name is dimmed');
  assert.strictEqual(first.title, 'fence_wood Fence', 'the title says the name whole (R2)');
  const keys = first.children[2].children;
  assert.deepStrictEqual(keys.map(k => [k.tagName, k.innerText]), [['KBD', '0'], ['KBD', '2'], ['KBD', '9']],
    'the shapes are keycaps');
  assert.strictEqual(textOf(first.children[3]), '980', 'the id');
  assert.strictEqual(cells.lineNodes[1].children[1].children.length, 1, 'no display name, no dim part');
  let stopped = false;
  keys[1].fire('click', { stopPropagation() { stopped = true; } });
  assert.deepStrictEqual([typed[typed.length - 1].k, typed[typed.length - 1].v, typed[typed.length - 1].x],
    [INTENT.PICK, 0, 2], 'keycap 2 picks row 0 at cell 2');
  assert(stopped, 'and does not reach the row body\'s click');
  cells.control.fire('click', { clientY: 30 });
  assert.deepStrictEqual([typed[typed.length - 1].k, typed[typed.length - 1].v, typed[typed.length - 1].x],
    [INTENT.PICK, 1, 0], 'the row body picks at cell 0');
  assert.strictEqual(plainList.lineNodes[0].innerText, `a${US}b`, 'a plain list shows its text as it is');
}

/* GRAPH images (workbench 6.4): the square picker's world map on the web. The
 * native picture -- 128x128 0xAARRGGBB words, what image_publish hands the
 * scene and ToriRSChromeExecWeb_GraphImage hands the page -- goes through the
 * real host (web/torirs_chrome.js) as a graph.image envelope keyed by its
 * scene id, and the runtime draws every IMAGE item naming that id onto one
 * canvas under the well's other items. The canvas's pixels over the item hash
 * to the native sprite's. Fresh page and runtime, a fake 2D canvas. */
{
  const { PluginChromeHost } = require(path.join(__dirname, '..', '..', 'web', 'torirs_chrome.js'));
  function canvasNode() {
    const c = node('canvas');
    c.width = 0;
    c.height = 0;
    c.getContext = () => {
      if (c._ctx) return c._ctx;
      c._ctx = {
        globalAlpha: 1, imageSmoothingEnabled: true, drawn: [],
        buffer() {
          if (!c._px || c._px.length !== c.width * c.height * 4)
            c._px = new Uint8ClampedArray(c.width * c.height * 4);
          return c._px;
        },
        clearRect() { this.buffer().fill(0); this.drawn = []; },
        putImageData(image, x, y) {
          const b = this.buffer();
          for (let r = 0; r < image.height; r++)
            for (let q = 0; q < image.width; q++)
              for (let k = 0; k < 4; k++)
                b[((y + r) * c.width + x + q) * 4 + k] = image.data[(r * image.width + q) * 4 + k];
        },
        drawImage(src, sx, sy, sw, sh, dx, dy, dw, dh) {
          this.drawn.push({ alpha: this.globalAlpha, dx, dy, dw, dh, smooth: this.imageSmoothingEnabled });
          const b = this.buffer();
          const s = src._px;
          for (let r = 0; r < dh; r++)
            for (let q = 0; q < dw; q++) {
              const tx = dx + q;
              const ty = dy + r;
              if (tx < 0 || ty < 0 || tx >= c.width || ty >= c.height) continue;
              const ux = sx + Math.floor(q * sw / dw);
              const uy = sy + Math.floor(r * sh / dh);
              for (let k = 0; k < 4; k++) b[(ty * c.width + tx) * 4 + k] = s[(uy * src.width + ux) * 4 + k];
            }
        }
      };
      return c._ctx;
    };
    return c;
  }
  const imageIds = {};
  const imageDocument = {
    documentElement: node('html'), activeElement: null,
    createElement: tag => (String(tag).toLowerCase() === 'canvas' ? canvasNode() : node(tag)),
    createTextNode(t) { const n = node('#text'); n.nodeText = String(t); return n; },
    getElementById(id) { return imageIds[id] || null; }
  };
  const put = (id, tag, parent) => {
    const item = node(tag); item.id = id; imageIds[id] = item;
    if (parent) parent.appendChild(item);
    return item;
  };
  const imageShell = put('tpc-shell', 'div');
  const imagePane = put('tpc-pane', 'section', imageShell);
  put('tpc-title', 'h1', imagePane);
  put('tpc-close', 'button', imagePane);
  put('tpc-tabs', 'nav', imagePane).hidden = true;
  put('tpc-content', 'main', imagePane);
  const imageRail = node('nav');
  imageShell.appendChild(imageRail);
  put('tpc-rail-list', 'div', imageRail);
  put('tpc-status', 'div');
  class FakeImageData {
    constructor(data, width, height) { this.data = data; this.width = width; this.height = height; }
  }
  const imageGlobal = {
    document: imageDocument, ToriRSPluginChromeCodec: codec, devicePixelRatio: 1,
    torirsPluginChromePostMessage() { return true; },
    setTimeout() { return 1; }, addEventListener() {},
    atob: s => Buffer.from(String(s), 'base64').toString('latin1'),
    Uint8ClampedArray, ImageData: FakeImageData
  };
  const imageRuntime = createRuntime(imageGlobal, imageDocument);
  const host = new PluginChromeHost({
    btoa: s => Buffer.from(String(s), 'latin1').toString('base64'), Uint8Array, addEventListener() {}
  }, null);
  assert(host.attachRuntime(imageRuntime, null), 'the host attaches the runtime');

  /* A native picture: every pixel its own colour, alpha included. */
  const SCENE = 0x4C000003;
  const argb = new Uint32Array(128 * 128);
  for (let i = 0; i < argb.length; i++)
    argb[i] = ((0xC0 + (i & 63)) << 24 | ((i * 2654435761) & 0xFFFFFF)) >>> 0;
  const fnv = bytes => {
    let h = 2166136261;
    for (let i = 0; i < bytes.length; i++) h = Math.imul(h ^ bytes[i], 16777619) >>> 0;
    return h;
  };
  const native = new Uint8Array(argb.length * 4);
  for (let i = 0; i < argb.length; i++) {
    native[i * 4] = (argb[i] >>> 16) & 255;
    native[i * 4 + 1] = (argb[i] >>> 8) & 255;
    native[i * 4 + 2] = argb[i] & 255;
    native[i * 4 + 3] = argb[i] >>> 24;
  }
  const page = generation => ({
    protocol: 1, type: 'page.snapshot', pageGeneration: generation, panel: 3, title: 'Squares',
    checkStyle: 0, commands: [
      cmd(CMD.PANEL_OPEN, { text: 'Squares' }),
      cmd(CMD.WIDGET_ADD, { w: 1, v: W.GRAPH, label: 'Surface', ch: 300, s: 301 }),
      cmd(CMD.WIDGET_GRAPH, { w: 1, v: 4 }),
      cmd(CMD.WIDGET_GRAPH_ITEM, { w: 1, tab: 0, x: 0, y: 0, cw: 300, ch: 300, c: 0x1a1712 }),
      cmd(CMD.WIDGET_GRAPH_ITEM, { w: 1, tab: 4, x: 22, y: 12, cw: 128, ch: 128, c: 0, v: SCENE }),
      cmd(CMD.WIDGET_GRAPH_ITEM, { w: 1, tab: 4, x: 150, y: 12, cw: 128, ch: 128, c: 128, v: SCENE + 1 }),
      cmd(CMD.WIDGET_GRAPH_ITEM, { w: 1, tab: 1, x: 22, y: 12, cw: 60, ch: 60, c: 0xffff00, v: 0 })
    ]
  });
  const imageRows = imageIds['tpc-content'].children;
  const well = () => imageRows.find(r => r._tpcRecord && r._tpcRecord.handle === 1)._tpcRecord.control;

  imageRuntime.receive(page(40));
  let canvas = well().children[0];
  assert.strictEqual(canvas.tagName, 'CANVAS', 'the well\'s first child is its picture canvas');
  assert.strictEqual(canvas.className, 'tpc-graph-canvas', 'drawn under the items');
  assert.strictEqual(canvas.getAttribute('data-images'), '0', 'no picture sent yet: nothing drawn');
  assert.strictEqual(well().children.length, 3, 'the canvas, then one node per rect (images are not nodes)');
  assert.strictEqual(canvas.width, 278, 'the canvas spans the images (150 + 128)');

  assert.strictEqual(host.graphImage(SCENE, 1, 128, 128, argb), true, 'the host takes the picture');
  canvas = well().children[0];
  assert.strictEqual(canvas.getAttribute('data-images'), '1', 'the picture landing redraws the well');
  {
    const region = new Uint8Array(128 * 128 * 4);
    for (let r = 0; r < 128; r++)
      for (let q = 0; q < 128; q++)
        for (let k = 0; k < 4; k++)
          region[(r * 128 + q) * 4 + k] = canvas._px[((12 + r) * canvas.width + 22 + q) * 4 + k];
    assert.strictEqual(fnv(region), fnv(native), 'the canvas over the item hashes to the native sprite');
  }
  assert.strictEqual(canvas._ctx.drawn.length, 1, 'one drawImage, the unsent id draws nothing');
  assert.strictEqual(canvas._ctx.drawn[0].smooth, false, 'drawn sharp, never smoothed');

  /* The item's colour is its transparency. */
  assert.strictEqual(host.graphImage(SCENE + 1, 1, 128, 128, argb), true, 'a second picture');
  canvas = well().children[0];
  assert.strictEqual(canvas.getAttribute('data-images'), '2', 'both drawn');
  assert(Math.abs(canvas._ctx.drawn[1].alpha - 127 / 255) < 1e-9, 'trans 128 draws at alpha 127/255');

  /* A picture outlives the page: a new snapshot draws it at once. */
  imageRuntime.receive(page(41));
  assert.strictEqual(well().children[0].getAttribute('data-images'), '2', 'a new page draws the kept pictures');

  /* Refused: a bad size, a bad id, a short payload. */
  assert.strictEqual(imageRuntime.receive({ protocol: 1, type: 'graph.image', sceneId: 7, width: 2, height: 2,
    rgbaBase64: 'AAAA' }), false, 'a payload shorter than its size is refused');
  assert.strictEqual(imageRuntime.receive({ protocol: 1, type: 'graph.image', sceneId: 0, width: 1, height: 1,
    rgbaBase64: 'AAAAAA==' }), false, 'scene id 0 is refused');
  assert.strictEqual(host.graphImage(9, 1, 2048, 2, argb), false, 'a picture over 1024 a side is refused');

  /* A replaced iframe is given the pictures again when it attaches. */
  const replayed = [];
  host.attachRuntime({ receive(message) { replayed.push(message); return true; } }, null);
  assert(replayed.some(m => m.type === 'graph.image' && m.sceneId === SCENE && m.width === 128),
    'the kept pictures replay to a new runtime');
}

console.log('chrome widgets dom: ok');
