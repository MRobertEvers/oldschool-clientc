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
  WIDGET_GRAPH_ITEM: 26, WIDGET_TOOLTIP: 27, WIDGET_CHORD: 28 };
const W = { BUTTON: 7, TREE: 13, VLIST: 14, ICONBUTTON: 15, SLIDER: 16, RADIO: 17, CODE: 18, GRAPH: 19 };
const INTENT = { ACTIVATE: 1, TOGGLE: 3, PICK: 5, EXPAND: 10, SCROLL: 11, VALUE: 12,
  CODE_EDIT: 14, GRAPH: 15, GRAPH_WHEEL: 16 };
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
  cmd(CMD.WIDGET_TOOLTIP, { w: 9, label: 'Pack the scripts', text: 'F5' })
];
runtime.receive({ protocol: 1, type: 'page.snapshot', pageGeneration: 20, panel: 3,
  title: 'Editor', checkStyle: 0, commands });
const rows = ids['tpc-content'].children;
const byHandle = h => rows.find(r => r._tpcRecord && r._tpcRecord.handle === h)._tpcRecord;
assert.strictEqual(runtime.inspect().widgetCount, 9, 'every editor kind is retained');
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

console.log('chrome widgets dom: ok');
