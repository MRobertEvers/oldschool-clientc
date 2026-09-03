'use strict';

const assert = require('assert');
const path = require('path');
const { createRuntime } = require(path.join('..', 'runtime.js'));
const codec = require(path.join('..', 'codec-es3.js'));

function node(tag) {
  const value = {
    tagName: String(tag).toUpperCase(),
    children: [], parentNode: null, firstChild: null,
    className: '', hidden: false, title: '', value: '', checked: false,
    selectedIndex: -1, clientWidth: 320, clientHeight: 500,
    style: { setProperty(k, v) { this[k] = v; } },
    _attrs: {}, _events: {}
  };
  value.classList = {
    contains(name) { return value.className.split(/\s+/).includes(name); },
    add(name) { if (!this.contains(name)) value.className = `${value.className} ${name}`.trim(); },
    remove(name) { value.className = value.className.split(/\s+/).filter(v => v && v !== name).join(' '); },
    toggle(name, on) { if (on) this.add(name); else this.remove(name); }
  };
  value.appendChild = child => {
    child.parentNode = value;
    value.children.push(child);
    value.firstChild = value.children[0] || null;
    return child;
  };
  value.removeChild = child => {
    const at = value.children.indexOf(child);
    if (at >= 0) value.children.splice(at, 1);
    child.parentNode = null;
    value.firstChild = value.children[0] || null;
    return child;
  };
  value.setAttribute = (key, item) => { value._attrs[key] = String(item); };
  value.getAttribute = key => Object.prototype.hasOwnProperty.call(value._attrs, key)
    ? value._attrs[key] : null;
  value.addEventListener = (kind, fn) => {
    (value._events[kind] = value._events[kind] || []).push(fn);
  };
  value.fire = (kind, event = {}) => {
    const delivered = Object.assign({
      target: value, clientX: 8, clientY: 8, key: '', pointerId: 1
    }, event);
    (value._events[kind] || []).forEach(fn => fn(delivered));
    if (typeof value[`on${kind}`] === 'function') value[`on${kind}`](delivered);
  };
  value.focus = () => {};
  value.getBoundingClientRect = () => ({ left: 0, top: 0, width: 200, height: 100 });
  value.getElementsByClassName = name => {
    const found = [];
    function walk(item) {
      if (item.classList && item.classList.contains(name)) found.push(item);
      item.children.forEach(walk);
    }
    walk(value);
    return found;
  };
  value.contains = child => {
    for (let at = child; at; at = at.parentNode) if (at === value) return true;
    return false;
  };
  if (value.tagName === 'CANVAS') {
    value.getContext = () => ({ putImageData() {} });
    value.toDataURL = () => 'data:image/png;base64,AAAA';
  }
  return value;
}

function fixture() {
  const ids = {};
  const document = {
    documentElement: node('html'),
    activeElement: null,
    createElement: node,
    getElementById(id) { return ids[id] || null; }
  };
  function add(id, tag, parent) {
    const item = node(tag);
    item.id = id;
    ids[id] = item;
    if (parent) parent.appendChild(item);
    return item;
  }
  const shell = add('tpc-shell', 'div');
  shell.className = 'tpc-shell tpc-collapsed';
  const pane = add('tpc-pane', 'section', shell);
  pane.hidden = true;
  const frame = node('div');
  pane.appendChild(frame);
  ['tl', 't', 'tr', 'l', 'r', 'bl', 'b', 'br'].forEach(name => {
    const item = node('i');
    item.className = `tpc-frame-${name}`;
    frame.appendChild(item);
  });
  const title = add('tpc-title', 'h1', pane);
  title.textContent = 'Plugins';
  add('tpc-close', 'button', pane);
  const tabs = add('tpc-tabs', 'nav', pane);
  tabs.hidden = true;
  add('tpc-content', 'main', pane);
  const railHost = node('nav');
  shell.appendChild(railHost);
  add('tpc-rail-list', 'div', railHost);
  add('tpc-status', 'div');
  return { document, ids };
}

function command(k, values) {
  return Object.assign({
    k, p: 3, w: -1, tab: -1, v: 0, c: 0,
    x: 0, y: 0, cw: 0, ch: 0, label: '', text: '', s: 0
  }, values || {});
}

const { document, ids } = fixture();
const posted = [];
const typed = [];
const timers = [];
const global_ = {
  document,
  ToriRSPluginChromeCodec: codec,
  devicePixelRatio: 2,
  Uint8ClampedArray,
  ImageData: class ImageData { constructor(data, width, height) {
    this.data = data; this.width = width; this.height = height;
  } },
  atob(value) { return Buffer.from(value, 'base64').toString('binary'); },
  torirsPluginChromePostMessage(value) { posted.push(JSON.parse(value)); },
  torirsChromeIntentPosted(value) { typed.push(value); },
  setTimeout(fn) { timers.push(fn); return timers.length; },
  addEventListener() {}
};
const runtime = createRuntime(global_, document);
assert(runtime, 'runtime boots against the canonical document');

const entries = [{
  kind: 1, pluginIndex: -2, preferredWidth: 320,
  title: 'Manage Plugins', iconAsset: '', badge: '', attention: false
}];
for (let i = 0; i < 32; i++) entries.push({
  kind: 2, pluginIndex: i, preferredWidth: 320,
  title: `Plugin ${i}`, iconAsset: `p${i}.png`, badge: i === 31 ? '9' : '',
  attention: i === 31
});
assert(runtime.receive({
  protocol: 1, type: 'rail.snapshot', registryRevision: 1,
  selectionGeneration: 7, pageGeneration: 1, activePlugin: -1,
  lastSelectedPlugin: -1, selectedEntry: -2, expanded: false, entries
}));
assert.strictEqual(runtime.inspect().railEntries, 33, 'Manage and all 32 plugins render');
assert.strictEqual(ids['tpc-rail-list'].children.length, 33, 'one retained button per entry');
const manage = ids['tpc-rail-list'].children[0];
manage.fire('click');
assert.deepStrictEqual(posted[posted.length - 1], {
  protocol: 1, type: 'rail.select', sequence: posted[posted.length - 1].sequence,
  pluginIndex: -2, selectionGeneration: 7
}, 'Manage click carries its sentinel and shell generation');

runtime.receive({
  protocol: 1, type: 'rail.icon', pluginIndex: 31, revision: 2,
  width: 2, height: 2, url: 'torirs://bitmap/rail/31/2'
});
assert.strictEqual(ids['tpc-rail-list'].children[32].children[0].tagName, 'IMG',
  'authored icon replaces fallback on its retained entry');
assert(ids['tpc-rail-list'].children[32].classList.contains('tpc-attention'),
  'attention remains host-owned styling');
runtime.receive({
  protocol: 1, type: 'theme', revision: 2, assets: {
    buttonLeft: 'skin/ButtonLeft.png', buttonMiddle: 'skin/ButtonMid.png',
    buttonRight: 'skin/ButtonRight.png', dropdownBody: 'skin/DropdownBody.png',
    scrollDown: 'skin/ScrollDown.png', checkOn: 'skin/CheckOn.png',
    checkOff: 'skin/CheckOff.png'
  }
});

const commands = [
  command(3, { text: 'Plugin 0' }),
  command(8, { w: 1, v: 1, label: 'Enabled', s: 101 }),
  command(12, { w: 1, v: 1 }),
  command(8, { w: 2, v: 2, label: 'Name', text: 'Rune', s: 102 }),
  command(8, { w: 3, v: 5, label: 'Mode', s: 103 }),
  command(17, { w: 3, v: 2 }),
  command(18, { w: 3, v: 0, text: 'One' }),
  command(18, { w: 3, v: 1, text: 'Two' }),
  command(15, { w: 3, v: 1 }),
  command(8, { w: 4, v: 7, text: 'Run', s: 104 }),
  command(8, { w: 5, v: 9, label: 'Row', cw: 1, s: 105 }),
  command(8, { w: 6, v: 11, label: 'Notes', text: 'a\nb', ch: 4, s: 106 }),
  command(8, { w: 7, v: 12, label: 'Chart', ch: 120, s: 107 }),
  command(8, { w: 8, v: 0, text: 'Readout', s: 108 }),
  command(8, { w: 9, v: 3, s: 109 }),
  command(8, { w: 10, v: 6, label: 'Preview', s: 110 }),
  command(8, { w: 11, v: 10, label: 'Colour', text: '#112233', s: 111 }),
  command(8, { w: 12, v: 8, s: 112 }),
  command(17, { w: 12, v: 2 }),
  command(18, { w: 12, v: 0, text: 'A' }),
  command(18, { w: 12, v: 1, text: 'B' })
];
runtime.receive({
  protocol: 1, type: 'rail.snapshot', registryRevision: 1,
  selectionGeneration: 8, pageGeneration: 20, activePlugin: 0,
  lastSelectedPlugin: 0, selectedEntry: 0, expanded: true, entries
});
runtime.receive({
  protocol: 1, type: 'page.snapshot', pageGeneration: 20,
  panel: 3, title: 'Plugin 0', checkStyle: 0, commands
});
assert.strictEqual(runtime.inspect().widgetCount, 12, 'all semantic control kinds are retained');
assert(!ids['tpc-pane'].hidden, 'one selected page is visible');
const dropdownRow = ids['tpc-content'].children.find(item => item._tpcRecord.handle === 3);
assert.match(dropdownRow._tpcRecord.control.style.backgroundImage,
  /ScrollDown\.png[\s\S]*DropdownBody\.png/,
  'the themed select retains both the authored arrow and tiled body');
assert.strictEqual(dropdownRow._tpcRecord.control.style.backgroundSize, '14px 14px,auto',
  'dropdown arrow uses row height minus the authored two-pixel inset');
const buttonRow = ids['tpc-content'].children.find(item => item._tpcRecord.handle === 4);
assert.strictEqual(buttonRow._tpcRecord.control.style.backgroundSize,
  '18px 18px,18px 18px,10px 18px', '2x button bake maps to the 1x row grid');
const separatorRow = ids['tpc-content'].children.find(item => item._tpcRecord.handle === 9);
assert.strictEqual(separatorRow.children[0].className, 'tpc-separator-line',
  'separator retains a normal row but paints only its one-pixel center rule');

const oldCheckboxRow = ids['tpc-content'].children.find(item => item._tpcRecord.handle === 1);
const oldCheckbox = oldCheckboxRow.children[0];
oldCheckbox.checked = false;
oldCheckbox.fire('change');
assert.deepStrictEqual(typed[typed.length - 1], {
  k: 3, p: 3, w: 1, v: 0, text: '', x: 0, y: 0, g: 20, s: 101
}, 'every widget intent carries page generation and semantic serial');

runtime.receive({
  protocol: 1, type: 'custom.bitmap', pageGeneration: 20,
  panel: 3, widget: 7, widgetSerial: 999, revision: 1,
  scaleMilli: 2000, width: 200, height: 100,
  url: 'torirs://bitmap/custom/stale'
});
const customRow = ids['tpc-content'].children.find(item => item._tpcRecord.handle === 7);
assert.strictEqual(customRow._tpcRecord.control.style.height, '120px',
  'custom command height wins over the bitmap aspect ratio');
assert.strictEqual(customRow.children[customRow.children.length - 1].children.length, 0,
  'wrong custom serial is rejected');
runtime.receive({
  protocol: 1, type: 'custom.bitmap', pageGeneration: 20,
  panel: 3, widget: 7, widgetSerial: 107, revision: 2,
  scaleMilli: 2000, width: 200, height: 100,
  url: 'torirs://bitmap/custom/20/107/2'
});
const custom = customRow.children[customRow.children.length - 1];
assert.strictEqual(custom.children[0].tagName, 'IMG', 'matching custom bitmap is retained');
custom.fire('mousedown', { clientX: 50, clientY: 25 });
custom.fire('mouseup', { clientX: 50, clientY: 25 });
assert.strictEqual(typed[typed.length - 1].k, 8, 'custom activation uses its semantic intent');
assert.strictEqual(typed[typed.length - 1].g, 20);
assert.strictEqual(typed[typed.length - 1].s, 107);

/* Same handle/kind, new page and serial: detached old DOM must remain stale. */
runtime.receive({
  protocol: 1, type: 'page.snapshot', pageGeneration: 21, panel: 3,
  title: 'Replacement', commands: [
    command(3, { text: 'Replacement' }),
    command(8, { w: 1, v: 1, label: 'Enabled', s: 202 })
  ]
});
const typedBefore = typed.length;
oldCheckbox.fire('change');
assert.strictEqual(typed.length, typedBefore,
  'listener from prior page cannot retarget a recycled handle');
const newCheckbox = ids['tpc-content'].children[0].children[0];
newCheckbox.checked = true;
newCheckbox.fire('change');
assert.strictEqual(typed[typed.length - 1].g, 21);
assert.strictEqual(typed[typed.length - 1].s, 202,
  'replacement listener carries the new identity');
assert(ids['tpc-pane'].classList.contains('tpc-no-tabs'),
  'a replacement page without tabs recovers the compact content top');

document.activeElement = newCheckbox;
ids['tpc-content'].fire('focusin', { target: newCheckbox });
assert.strictEqual(posted[posted.length - 1].type, 'editor.focus');
assert.strictEqual(posted[posted.length - 1].focused, true,
  'HTML editor focus is handed to the host once');
const secondEditor = node('input');
ids['tpc-content'].appendChild(secondEditor);
ids['tpc-content'].fire('focusout', { target: newCheckbox });
document.activeElement = secondEditor;
ids['tpc-content'].fire('focusin', { target: secondEditor });
while (timers.length) timers.shift()();
assert.notStrictEqual(posted[posted.length - 1].focused, false,
  'control-to-control focus does not pulse ownership back to the game');
ids['tpc-content'].fire('focusout', { target: secondEditor });
document.activeElement = null;
while (timers.length) timers.shift()();
assert.strictEqual(posted[posted.length - 1].type, 'editor.focus');
assert.strictEqual(posted[posted.length - 1].focused, false,
  'settled editor blur returns keyboard ownership');

runtime.receive({
  protocol: 1, type: 'page.delta', pageGeneration: 20,
  commands: [command(11, { w: 1, text: 'stale' })]
});
assert.strictEqual(runtime.inspect().pageGeneration, 21, 'old-generation delta is ignored');

runtime.receive({
  protocol: 1, type: 'rail.snapshot', registryRevision: 1,
  selectionGeneration: 9, pageGeneration: 22, activePlugin: -1,
  lastSelectedPlugin: 0, selectedEntry: 0, expanded: false, entries
});
assert.strictEqual(runtime.inspect().widgetCount, 0, 'collapse releases the selected page');
assert.strictEqual(runtime.inspect().railEntries, 33, 'collapse retains the shared rail');
assert.strictEqual(ids['tpc-rail-list'].children.length, 33);

assert.strictEqual(runtime.receive('{bad json'), false, 'malformed host input is ignored');
console.log('modern plugin chrome runtime: ok');
