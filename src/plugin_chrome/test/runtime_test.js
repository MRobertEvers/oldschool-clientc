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
  value.insertBefore = (child, reference) => {
    const at = value.children.indexOf(reference);
    if (at < 0) return value.appendChild(child);
    child.parentNode = value;
    value.children.splice(at, 0, child);
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
  command(18, { w: 12, v: 1, text: 'B' }),
  command(8, { w: 13, v: 5, label: 'Gameframe', s: 113 }),
  command(17, { w: 13, v: 3, x: 1 }),
  command(18, { w: 13, v: 0, x: 1, text: 'auto',
    label: 'Same|label', detail: 'Uses the lane default' }),
  command(18, { w: 13, v: 1, x: 0, text: 'missing/frame',
    label: 'Same|label', detail: 'Provider is not installed' }),
  command(18, { w: 13, v: 2, x: 1, text: 'ready/frame',
    label: 'Ready', detail: 'Available now' }),
  command(15, { w: 13, v: 1, text: 'missing/frame' }),
  command(8, { w: 14, v: 9, label: 'Woodcutting',
    text: 'XP/hr 12.3K · XP 4,200', cw: 2, s: 114 }),
  command(8, { w: 15, v: 9, label: 'Client Settings', cw: 3, s: 115 }),
  command(8, { w: 16, v: 0, text: 'Session', cw: 1, s: 116 }),
  command(8, { w: 17, v: 0, text: 'No loot recorded yet.', cw: 2, s: 117 }),
  command(8, { w: 18, v: 0, label: 'Total value', text: '12,450 gp', cw: 3, s: 118 }),
  command(8, { w: 19, v: 0, label: 'Level progress', text: 'Level 72 to 73', cw: 4, s: 119 }),
  command(15, { w: 19, v: 68 }),
  command(8, { w: 20, v: 0, label: 'Tracker unavailable', text: 'Log in first', cw: 5, s: 120 })
];
runtime.receive({
  protocol: 1, type: 'rail.snapshot', registryRevision: 1,
  selectionGeneration: 8, pageGeneration: 20, activePlugin: 0,
  lastSelectedPlugin: 0, selectedEntry: 0, expanded: true, entries
});
const selectedPluginRailButton = ids['tpc-rail-list'].children[1];
selectedPluginRailButton.fire('click');
assert.strictEqual(posted[posted.length - 1].type, 'rail.select');
assert.strictEqual(posted[posted.length - 1].pluginIndex, 0,
  'a plugin rail button always emits its page destination, even while another face is visible');
assert.strictEqual(posted[posted.length - 1].selectionGeneration, 8);
runtime.receive({
  protocol: 1, type: 'page.snapshot', pageGeneration: 20,
  panel: 3, title: 'Plugin 0', checkStyle: 0, commands
});
assert.strictEqual(runtime.inspect().widgetCount, 20, 'all semantic control kinds are retained');
assert(!ids['tpc-pane'].hidden, 'one selected page is visible');
const dropdownRow = ids['tpc-content'].children.find(item => item._tpcRecord.handle === 3);
const plainDrop = dropdownRow._tpcRecord.control;
assert.strictEqual(plainDrop.tagName, 'BUTTON',
  'a dropdown is a page-drawn button, never a native select whose menu blocks the host thread');
assert.strictEqual(plainDrop.children[0].innerText, 'Two', 'the closed button shows the selection');
assert.match(plainDrop.style.backgroundImage, /PanelBody\.png/,
  'the closed dropdown wears the panel tile, as dbg_push_field_chrome does');
assert.match(plainDrop.children[1].style.backgroundImage, /ScrollDown\.png/,
  'the closed dropdown points its arrow down');
const structuredRow = ids['tpc-content'].children.find(item => item._tpcRecord.handle === 13);
const structured = structuredRow._tpcRecord.control;
assert.match(structured.children[0].innerText, /Provider is not installed/,
  'availability detail is visible in the selected option');
function openList() {
  const host = ids['tpc-shell'];
  const back = host.children.find(item => item.className === 'tpc-dropdown-backdrop');
  return back ? back.children[0].children[0].children : null;
}
assert.strictEqual(openList(), null, 'no list exists until the button is pressed');
structured.fire('click');
let rows = openList();
assert(rows, 'pressing the button opens the list');
assert.strictEqual(structured.getAttribute('aria-expanded'), 'true');
assert.match(structured.children[1].style.backgroundImage, /ScrollUp\.png/,
  'the open dropdown points its arrow up');
assert.strictEqual(rows.length, 3);
assert.strictEqual(rows[0].innerText, rows[1].innerText
  .replace('Provider is not installed', 'Uses the lane default'),
  'duplicate delimiter-containing labels remain presentation, not identity');
assert.strictEqual(rows[1].getAttribute('aria-disabled'), 'true');
assert.strictEqual(rows[1].getAttribute('aria-selected'), 'true',
  'a missing disabled saved choice remains visibly selected');
assert(rows[1].classList.contains('tpc-dropdown-disabled'));
assert(rows[1].classList.contains('tpc-dropdown-alt'), 'bands alternate on the option index');
const typedBeforeDisabled = typed.length;
rows[1].fire('click');
assert.strictEqual(typed.length, typedBeforeDisabled,
  'a disabled structured row cannot emit a pick intent');
assert(openList(), 'and the list stays open over it');
rows[2].fire('click');
assert.strictEqual(openList(), null, 'a pick closes the list');
assert.strictEqual(typed[typed.length - 1].v, 2);
assert.strictEqual(typed[typed.length - 1].text, 'ready/frame',
  'an enabled pick returns its stable value rather than its label');
assert.match(structured.children[0].innerText, /Ready/,
  'the button shows the pick before the host confirms it, as a select did');
assert.strictEqual(structured.getAttribute('aria-expanded'), 'false');
/* The keyboard: arrows move over enabled rows only, Enter picks, Escape
 * closes without picking. */
structured.fire('keydown', { keyCode: 40 });
assert(openList(), 'Down on a closed dropdown opens it');
document.onkeydown({ keyCode: 38 });
rows = openList();
assert(rows[0].classList.contains('tpc-dropdown-hover'), 'Up skips the disabled row');
document.onkeydown({ keyCode: 27 });
assert.strictEqual(openList(), null, 'Escape closes the list');
assert.strictEqual(document.onkeydown, null, 'and hands the keyboard back');
assert.strictEqual(typed[typed.length - 1].v, 2, 'without picking');
structured.fire('click');
const typedBeforeBackdrop = typed.length;
ids['tpc-shell'].children.find(item => item.className === 'tpc-dropdown-backdrop')
  .fire('mousedown', { target: ids['tpc-shell'] });
assert.strictEqual(openList(), null, 'a press outside the list closes it');
assert.strictEqual(typed.length, typedBeforeBackdrop, 'and picks nothing');
const buttonRow = ids['tpc-content'].children.find(item => item._tpcRecord.handle === 4);
assert.strictEqual(buttonRow._tpcRecord.control.style.backgroundSize,
  '18px 18px,18px 18px,10px 18px', '2x button bake maps to the 1x row grid');
const separatorRow = ids['tpc-content'].children.find(item => item._tpcRecord.handle === 9);
assert.strictEqual(separatorRow.children[0].className, 'tpc-separator-line',
  'separator retains a normal row but paints only its one-pixel center rule');
const actionRow = ids['tpc-content'].children.find(item => item._tpcRecord.handle === 14);
const actionButton = actionRow._tpcRecord.control;
assert.strictEqual(actionButton.tagName, 'BUTTON',
  'a semantic action row is a native focusable button, not a clickable table cell');
assert.strictEqual(actionButton.type, 'button');
assert.strictEqual(actionButton.children[0].innerText, 'Woodcutting',
  'the action row retains its primary label separately');
assert.strictEqual(actionButton.children[1].innerText, 'XP/hr 12.3K · XP 4,200',
  'the action row retains its live summary separately');
assert.match(actionButton.getAttribute('aria-label'), /Woodcutting\. XP\/hr 12\.3K/,
  'the native button exposes both fields as one accessible name');
assert.strictEqual(actionRow.getElementsByClassName('tpc-check').length, 0,
  'an action row has no management switch');
assert.strictEqual(actionRow.getElementsByClassName('tpc-row-well').length, 0,
  'an action row has no management settings well');
actionButton.fire('click');
assert.deepStrictEqual(typed[typed.length - 1], {
  k: 2, p: 3, w: 14, v: 0, text: '', x: 0, y: 0, g: 20, s: 114
}, 'the native action-row button emits the existing ACTION intent');

const managedRow = ids['tpc-content'].children.find(item => item._tpcRecord.handle === 5);
assert.strictEqual(managedRow._tpcRecord.control.tagName, 'TABLE',
  'an ordinary management row keeps its existing three-zone table');
assert.strictEqual(managedRow.getElementsByClassName('tpc-row-well').length, 1);
assert.strictEqual(managedRow.getElementsByClassName('tpc-check').length, 1);
const lockedManagedRow = ids['tpc-content'].children.find(item => item._tpcRecord.handle === 15);
assert.strictEqual(lockedManagedRow._tpcRecord.control.tagName, 'TABLE',
  'a locked management row remains distinct from a semantic action row');
assert.strictEqual(lockedManagedRow.getElementsByClassName('tpc-row-well').length, 1);
assert.strictEqual(lockedManagedRow.getElementsByClassName('tpc-check').length, 0);

const headingRow = ids['tpc-content'].children.find(item => item._tpcRecord.handle === 16);
assert.strictEqual(headingRow.children[0].tagName, 'H2',
  'a panel heading remains a native heading instead of flattening to a span');
assert.strictEqual(headingRow.children[0].innerText, 'Session');
const paragraphRow = ids['tpc-content'].children.find(item => item._tpcRecord.handle === 17);
assert.strictEqual(paragraphRow.children[0].tagName, 'P');
assert.strictEqual(paragraphRow.children[0].innerText, 'No loot recorded yet.');
const keyValueRow = ids['tpc-content'].children.find(item => item._tpcRecord.handle === 18);
assert.strictEqual(keyValueRow.children[0].tagName, 'DL');
assert.strictEqual(keyValueRow.children[0].children[0].tagName, 'DT');
assert.strictEqual(keyValueRow.children[0].children[0].innerText, 'Total value');
assert.strictEqual(keyValueRow.children[0].children[1].tagName, 'DD');
assert.strictEqual(keyValueRow.children[0].children[1].innerText, '12,450 gp');
const progressRow = ids['tpc-content'].children.find(item => item._tpcRecord.handle === 19);
assert.strictEqual(progressRow._tpcRecord.control.tagName, 'PROGRESS');
assert.strictEqual(progressRow._tpcRecord.control.value, 68);
assert.strictEqual(progressRow._tpcRecord.control.getAttribute('aria-valuenow'), '68');
assert.strictEqual(progressRow._tpcRecord.valueNode.innerText, 'Level 72 to 73');
const errorRow = ids['tpc-content'].children.find(item => item._tpcRecord.handle === 20);
assert.strictEqual(errorRow.children[0].tagName, 'P');
assert.strictEqual(errorRow.children[0].getAttribute('role'), 'alert');
assert.strictEqual(errorRow._tpcRecord.caption.innerText, 'Tracker unavailable');
assert.strictEqual(errorRow._tpcRecord.valueNode.innerText, ' Log in first');

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
custom.clientWidth = 294;
const layoutCountBeforeResize = posted.filter(message => message.type === 'layout').length;
global_.onresize();
const resizedLayout = posted[posted.length - 1];
assert.strictEqual(resizedLayout.type, 'layout');
assert.strictEqual(resizedLayout.customWidth, 294,
  'layout publishes the browser-computed custom content-box width');
assert.strictEqual(resizedLayout.scaleMilli, 2000,
  'custom width remains logical while layout preserves the presenter DPR');
global_.onresize();
assert.strictEqual(posted.filter(message => message.type === 'layout').length,
  layoutCountBeforeResize + 1, 'unchanged custom geometry is retained-deduplicated');
custom.clientLeft = 1;
custom.clientTop = 1;
custom.clientHeight = 120;
const typedBeforeBorder = typed.length;
custom.fire('mousedown', { clientX: 0, clientY: 20 });
custom.fire('mouseup', { clientX: 50, clientY: 20 });
assert.strictEqual(typed.length, typedBeforeBorder,
  'a press beginning on chrome-owned custom border cannot activate plugin content');
custom.fire('mousedown', { clientX: 50, clientY: 25 });
custom.fire('mouseup', { clientX: 50, clientY: 25 });
assert.strictEqual(typed[typed.length - 1].k, 8, 'custom activation uses its semantic intent');
assert.strictEqual(typed[typed.length - 1].g, 20);
assert.strictEqual(typed[typed.length - 1].s, 107);
assert(typed[typed.length - 1].x >= 0 && typed[typed.length - 1].y >= 0,
  'custom content-box coordinates remain logical after DPR mapping');

/*
 * A well's secondary click must never reach the document-level handler that
 * builds the generic "Choose Option" popup: that popup's W.CUSTOM row offers
 * only "Select", so if the click bubbled there it would bury whatever menu
 * the well itself just answered with (Check/Ignore/Collapse -- whatever the
 * plugin wired to on_menu) under a popup that cannot reach any of it.
 */
let customMenuPrevented = false;
let customMenuStopped = false;
custom.fire('contextmenu', {
  clientX: 50, clientY: 25,
  preventDefault() { customMenuPrevented = true; },
  stopPropagation() { customMenuStopped = true; }
});
assert.strictEqual(typed[typed.length - 1].k, 9, 'the secondary click uses the CUSTOM_MENU intent');
assert.strictEqual(typed[typed.length - 1].g, 20);
assert.strictEqual(typed[typed.length - 1].s, 107);
assert(customMenuPrevented, 'the well cancels the host view\'s own context menu');
assert(customMenuStopped,
  'and stops it bubbling, or the document-level popup would still open over it');

const visitsBeforeDelta = runtime.inspect().renderVisits;
const structuredValueNode = structured.children[0];
assert.strictEqual(runtime.receive({
  protocol: 1, type: 'page.delta', pageGeneration: 20,
  commands: [command(11, { w: 8, text: 'Updated readout' })]
}), true);
assert.strictEqual(runtime.inspect().renderVisits, visitsBeforeDelta + 1,
  'a one-widget delta visits only that retained widget');
assert.strictEqual(structured.children[0], structuredValueNode,
  'an unrelated dropdown is neither traversed nor rebuilt');

const visitsBeforeActionSummary = runtime.inspect().renderVisits;
const retainedActionButton = actionButton;
assert.strictEqual(runtime.receive({
  protocol: 1, type: 'page.delta', pageGeneration: 20,
  commands: [command(11, { w: 14, text: 'XP/hr 13.1K · XP 4,500' })]
}), true);
assert.strictEqual(runtime.inspect().renderVisits, visitsBeforeActionSummary + 1,
  'an action-row summary delta visits only that retained row');
assert.strictEqual(actionRow._tpcRecord.control, retainedActionButton,
  'an action-row summary patch preserves the native button and its focus identity');
assert.strictEqual(actionButton.children[0].innerText, 'Woodcutting');
assert.strictEqual(actionButton.children[1].innerText, 'XP/hr 13.1K · XP 4,500');
const visitsBeforeEmptySummary = runtime.inspect().renderVisits;
assert.strictEqual(runtime.receive({
  protocol: 1, type: 'page.delta', pageGeneration: 20,
  commands: [command(11, { w: 14, text: '' })]
}), true);
assert.strictEqual(runtime.inspect().renderVisits, visitsBeforeEmptySummary + 1);
assert.strictEqual(actionRow._tpcRecord.control, retainedActionButton);
assert.strictEqual(actionButton.children[1].style.display, 'none',
  'removing the optional summary collapses only its retained secondary line');

const visitsBeforeSelection = runtime.inspect().renderVisits;
assert.strictEqual(runtime.receive({
  protocol: 1, type: 'page.delta', pageGeneration: 20,
  commands: [command(15, { w: 13, v: 2, text: 'ready/frame' })]
}), true);
assert.strictEqual(runtime.inspect().renderVisits, visitsBeforeSelection + 1,
  'a selection delta visits only its dropdown');
assert.strictEqual(structured.children[0], structuredValueNode,
  'changing selection does not reconstruct the button');
assert.match(structured.children[0].innerText, /Ready/);

const visitsBeforeOptions = runtime.inspect().renderVisits;
assert.strictEqual(runtime.receive({
  protocol: 1, type: 'page.delta', pageGeneration: 20,
  commands: [
    command(17, { w: 13, v: 2, x: 1 }),
    command(18, { w: 13, v: 0, x: 1, text: 'compact', label: 'Compact' }),
    command(18, { w: 13, v: 1, x: 1, text: 'roomy', label: 'Roomy' }),
    command(15, { w: 13, v: 1, text: 'roomy' })
  ]
}), true);
assert.strictEqual(runtime.inspect().renderVisits, visitsBeforeOptions + 1,
  'an option header, its items, and selection coalesce to one widget render');
assert.strictEqual(structured.children[0].innerText, 'Roomy');
structured.fire('click');
assert.strictEqual(openList().length, 2, 'the list is built from the replaced options');
assert.strictEqual(openList()[1].getAttribute('aria-selected'), 'true');
const openRowsBeforeRefresh = openList();
assert.strictEqual(runtime.receive({
  protocol: 1, type: 'page.delta', pageGeneration: 20,
  commands: [
    command(17, { w: 13, v: 3, x: 1 }),
    command(18, { w: 13, v: 0, x: 1, text: 'compact', label: 'Compact' }),
    command(18, { w: 13, v: 1, x: 1, text: 'roomy', label: 'Roomy' }),
    command(18, { w: 13, v: 2, x: 1, text: 'wide', label: 'Wide' })
  ]
}), true);
assert.notStrictEqual(openList(), openRowsBeforeRefresh,
  'an open list rebuilds in place when its options change');
assert.strictEqual(openList().length, 3);
const staleRow = openList()[2];

/* Same handle/kind, new page and serial: detached old DOM must remain stale. */
ids['tpc-content'].scrollTop = 37;
runtime.receive({
  protocol: 1, type: 'page.snapshot', pageGeneration: 21, panel: 3,
  title: 'Replacement', commands: [
    command(3, { text: 'Replacement' }),
    command(8, { w: 1, v: 1, label: 'Enabled', s: 202 })
  ]
});
assert.strictEqual(ids['tpc-content'].scrollTop, 0,
  'a replacement page starts at the top instead of inheriting the prior page scroll');
const typedBefore = typed.length;
oldCheckbox.fire('change');
assert.strictEqual(typed.length, typedBefore,
  'listener from prior page cannot retarget a recycled handle');
assert.strictEqual(openList(), null, 'a replacement page closes the list it tore down');
staleRow.fire('click');
assert.strictEqual(typed.length, typedBefore,
  'a structured pick from the prior generation is equally stale');
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

assert.strictEqual(runtime.receive({
  protocol: 1, type: 'page.delta', pageGeneration: 20,
  commands: [command(11, { w: 1, text: 'stale' })]
}), false, 'a stale-generation delta is reported as rejected to its host');
assert.strictEqual(runtime.inspect().pageGeneration, 21, 'old-generation delta is ignored');

assert.strictEqual(runtime.receive({
  protocol: 1, type: 'page.delta', pageGeneration: 21,
  commands: [command(17, { w: 1, v: 513 })]
}), false, 'an option list above the shared 512-entry protocol cap is rejected atomically');
assert.strictEqual(runtime.inspect().widgetCount, 1,
  'oversized option rejection leaves the retained page unchanged');

runtime.receive({
  protocol: 1, type: 'rail.snapshot', registryRevision: 1,
  selectionGeneration: 9, pageGeneration: 22, activePlugin: -1,
  lastSelectedPlugin: 0, selectedEntry: 0, expanded: false, entries
});
assert.strictEqual(runtime.inspect().widgetCount, 0, 'collapse releases the selected page');
assert.strictEqual(runtime.inspect().railEntries, 33, 'collapse retains the shared rail');
assert.strictEqual(ids['tpc-rail-list'].children.length, 33);

/*
 * A page BOUNDARY as the host actually emits one: the page being discarded is
 * closed under ITS identity, and the page replacing it arrives as a snapshot
 * under the NEW one, back to back.
 *
 * That pairing is what a plugin's own "Settings" button produces -- a second
 * face of the same plugin, so the rail never changes and the close is not
 * preceded by any rail traffic at all. The runtime has to mount the snapshot
 * that follows a close rather than treating the close as the end of the
 * conversation.
 */
runtime.receive({
  protocol: 1, type: 'rail.snapshot', registryRevision: 1,
  selectionGeneration: 10, pageGeneration: 30, activePlugin: 0,
  lastSelectedPlugin: 0, selectedEntry: 0, expanded: true, entries
});
runtime.receive({
  protocol: 1, type: 'page.snapshot', pageGeneration: 30, panel: 3,
  title: 'XP Tracker', commands: [
    command(3, { text: 'XP Tracker' }),
    command(8, { w: 4, v: 1, label: 'Reset all', s: 301 })
  ]
});
assert.strictEqual(runtime.inspect().pageGeneration, 30);
assert(runtime.inspect().widgetCount > 0, 'the page face mounts');

runtime.receive({ protocol: 1, type: 'page.close', pageGeneration: 30 });
runtime.receive({
  protocol: 1, type: 'page.snapshot', pageGeneration: 31, panel: 4,
  title: 'XP Tracker', commands: [
    command(3, { p: 4, text: 'XP Tracker' }),
    command(8, { p: 4, w: 5, v: 1, label: 'Save between sessions', s: 302 })
  ]
});
assert.strictEqual(runtime.inspect().pageGeneration, 31,
  'a snapshot after a close mounts the page it names');
assert(runtime.inspect().widgetCount > 0,
  'and its controls are there -- a close followed by a snapshot is a page '
  + 'REPLACEMENT, not the end of the page');

/* And the rail catching up a frame later must not disturb it: it carries the
 * identity the page stream already stated. */
runtime.receive({
  protocol: 1, type: 'rail.snapshot', registryRevision: 1,
  selectionGeneration: 10, pageGeneration: 31, activePlugin: 0,
  lastSelectedPlugin: 0, selectedEntry: 0, expanded: true, entries
});
assert(runtime.inspect().widgetCount > 0,
  'the rail catching up leaves the mounted page alone');

/*
 * The right-click popup.
 *
 * The page must never let the host view's own context menu open: its Reload
 * re-runs the bundle, and the host goes on addressing generations the
 * reloaded page has never heard of. Every right click is therefore cancelled,
 * and answered with the client's "Choose Option" popup built out of the same
 * intents the left-click controls post.
 */
function rightClick(target, x, y) {
  let prevented = false;
  const event = {
    target, clientX: x || 0, clientY: y || 0,
    preventDefault() { prevented = true; }
  };
  const result = document.oncontextmenu(event);
  assert.strictEqual(result, false, 'the native menu is cancelled');
  assert(prevented, 'and cancelled through preventDefault, not only the return');
  assert.strictEqual(event.returnValue, false, 'MSHTML is cancelled by returnValue');
  return event;
}

function popupRows() {
  return ids['tpc-shell'].getElementsByClassName('tpc-minimenu-option');
}

function popupLabels() {
  return popupRows().map(row => row.innerText);
}

const railEntry = ids['tpc-rail-list'].children[1];
rightClick(railEntry, 30, 200);
assert.deepStrictEqual(popupLabels(), ['Hide', 'Cancel'],
  'the open plugin\'s stone offers the verb that closes it, and Cancel');
popupRows()[0].fire('click');
assert.deepStrictEqual(posted[posted.length - 1], {
  protocol: 1, type: 'rail.select', sequence: posted[posted.length - 1].sequence,
  pluginIndex: 0, selectionGeneration: 10
}, 'a popup row posts the intent the left click posts, and nothing else');
assert.strictEqual(popupRows().length, 0, 'choosing a row dismisses the popup');

let checkRow = null;
for (const row of ids['tpc-content'].children) if (row._tpcRecord) checkRow = row;
assert(checkRow && checkRow._tpcRecord.handle === 5, 'the page mounted its checkbox row');
rightClick(checkRow._tpcRecord.control, 120, 60);
assert.deepStrictEqual(popupLabels(), ['Turn on', 'Close', 'Cancel'],
  'a control offers its own verb, then the page close, then Cancel');
popupRows()[0].fire('click');
assert.deepStrictEqual(typed[typed.length - 1], {
  k: 3, p: 4, w: 5, v: 1, text: '', x: 0, y: 0, g: 31, s: 302
}, 'the toggle row carries the widget identity the change handler carries');

rightClick(ids['tpc-content'], 120, 60);
assert.deepStrictEqual(popupLabels(), ['Close', 'Cancel'],
  'bare page chrome offers only what the page itself can do');
document.onkeydown({ keyCode: 27 });
assert.strictEqual(popupRows().length, 0, 'Escape dismisses the popup');

rightClick(railEntry, 30, 200);
assert.strictEqual(popupRows().length, 2, 'the popup reopens on the next right click');
runtime.receive({ protocol: 1, type: 'page.close', pageGeneration: 31 });
assert.strictEqual(popupRows().length, 0,
  'a page torn down under the popup takes the popup with it');
while (runtime.takeMessage()) {}
runtime.receive({ protocol: 1, type: 'page.close', pageGeneration: 0 });
assert.strictEqual(runtime.takeMessage(), '',
  'a closed page does not retain an unrouteable generation-zero layout');

/*
 * A row given a new identity keeps its place.
 *
 * `panel.reidentify` replaces one row's identity without rebuilding the page,
 * and the host states that as REMOVE then ADD between unchanged neighbours.
 * An ADD that could only append moved such a row to the bottom -- invisible in
 * the tracker that happens to re-identify its first row, wrong everywhere else.
 */
runtime.receive({
  protocol: 1, type: 'page.snapshot', pageGeneration: 40, panel: 5,
  title: 'Loot Tracker', commands: [
    command(3, { p: 5, text: 'Loot Tracker' }),
    command(8, { p: 5, w: 1, v: 0, text: 'Session', s: 401 }),
    command(8, { p: 5, w: 2, v: 0, text: 'Coins', s: 402 }),
    command(8, { p: 5, w: 3, v: 0, text: 'Bones', s: 403 }),
    command(8, { p: 5, w: 4, v: 0, text: 'Total', s: 404 })
  ]
});
const pageOrder = () => ids['tpc-content'].children
  .filter(item => item._tpcRecord).map(item => item._tpcRecord.handle);
assert.deepStrictEqual(pageOrder(), [1, 2, 3, 4],
  'a snapshot carries no positions and mounts in the order it arrives');

runtime.receive({
  protocol: 1, type: 'page.delta', pageGeneration: 40, commands: [
    command(9, { p: 5, w: 2 }),
    command(8, { p: 5, w: 2, v: 0, text: 'Coins', s: 412, b: 3 })
  ]
});
assert.deepStrictEqual(pageOrder(), [1, 2, 3, 4],
  're-identifying a MIDDLE row leaves the page in the same order');
assert.strictEqual(
  ids['tpc-content'].children.filter(item => item._tpcRecord)[1]._tpcRecord.serial, 412,
  'and the row that stayed put is the new identity, not the old one');

runtime.receive({
  protocol: 1, type: 'page.delta', pageGeneration: 40, commands: [
    command(8, { p: 5, w: 9, v: 0, text: 'Footer', s: 420 })
  ]
});
assert.deepStrictEqual(pageOrder(), [1, 2, 3, 4, 9],
  'an ADD with no position still appends, so an older stream is still valid');

runtime.receive({
  protocol: 1, type: 'page.delta', pageGeneration: 40, commands: [
    command(8, { p: 5, w: 10, v: 0, text: 'Stray', s: 421, b: 77 })
  ]
});
assert.deepStrictEqual(pageOrder(), [1, 2, 3, 4, 9, 10],
  'an anchor this page does not hold appends rather than throwing');

runtime.receive({
  protocol: 1, type: 'page.delta', pageGeneration: 40, commands: [
    command(9, { p: 5, w: 10 }),
    command(8, { p: 5, w: 10, v: 0, text: 'Stray', s: 422, b: 1 })
  ]
});
assert.deepStrictEqual(pageOrder(), [10, 1, 2, 3, 4, 9],
  'naming the first row moves the re-added one to the top, not merely near it');

/*
 * railHidden: the game lane's own pop-out column carries the destinations.
 *
 * The page draws no rail and reserves no width for one, but the page pane
 * still opens and closes on the snapshot's selection. The fixture has no CSS
 * engine, so the pane's measured width is modelled on the two stylesheets: a
 * 362px view whose pane stops at the rail's track less the 6px frame seam
 * (326) while the rail shows, and runs flush to the right edge (362) under
 * tpc-rail-hidden. The layout the host is told must be the pane's own box.
 */
const railPane = ids['tpc-pane'];
Object.defineProperty(railPane, 'clientWidth', {
  configurable: true,
  get() { return ids['tpc-shell'].classList.contains('tpc-rail-hidden') ? 362 : 326; }
});
const railBoxNode = ids['tpc-rail-list'].parentNode;
const lastLayout = () => posted.filter(message => message.type === 'layout').pop();
runtime.receive({
  protocol: 1, type: 'rail.snapshot', registryRevision: 1,
  selectionGeneration: 11, pageGeneration: 50, activePlugin: 0,
  lastSelectedPlugin: 0, selectedEntry: 0, expanded: true, railHidden: true, entries
});
assert(ids['tpc-shell'].classList.contains('tpc-rail-hidden'),
  'a railHidden snapshot marks the shell');
assert.strictEqual(runtime.inspect().railHidden, true);
assert.strictEqual(railBoxNode.hidden, true, 'the rail box takes no input while hidden');
assert.strictEqual(railBoxNode.style.display, 'none', 'and does not display');
assert.strictEqual(runtime.inspect().railEntries, 33, 'the entries are still retained');
assert(!ids['tpc-shell'].classList.contains('tpc-collapsed'),
  'a destination selected from the game still expands the pane');
runtime.receive({
  protocol: 1, type: 'page.snapshot', pageGeneration: 50, panel: 6,
  title: 'XP Tracker', commands: [
    command(3, { p: 6, text: 'XP Tracker' }),
    command(8, { p: 6, w: 1, v: 0, text: 'Session', s: 501 })
  ]
});
assert.strictEqual(runtime.inspect().pageGeneration, 50, 'the page mounts with the rail hidden');
assert(!railPane.hidden, 'and its pane is visible');
let hiddenLayout = lastLayout();
assert.strictEqual(hiddenLayout.pageGeneration, 50);
assert.strictEqual(hiddenLayout.width, 362,
  'with the rail hidden the layout is the flush pane, no rail track excluded from it');

runtime.receive({
  protocol: 1, type: 'rail.snapshot', registryRevision: 1,
  selectionGeneration: 11, pageGeneration: 50, activePlugin: 0,
  lastSelectedPlugin: 0, selectedEntry: 0, expanded: true, railHidden: false, entries
});
assert(!ids['tpc-shell'].classList.contains('tpc-rail-hidden'),
  'a railHidden:false snapshot removes the class');
assert.strictEqual(railBoxNode.hidden, false);
assert.strictEqual(railBoxNode.style.display, '');
const shownLayout = lastLayout();
assert.notStrictEqual(shownLayout, hiddenLayout, 'the toggle re-reports the layout');
assert.strictEqual(shownLayout.width, 326,
  'with the rail shown the layout is the pane alone, never the rail\'s 42px');

runtime.receive({
  protocol: 1, type: 'rail.snapshot', registryRevision: 1,
  selectionGeneration: 11, pageGeneration: 50, activePlugin: 0,
  lastSelectedPlugin: 0, selectedEntry: 0, expanded: true, railHidden: true, entries
});
hiddenLayout = lastLayout();
assert.strictEqual(hiddenLayout.width, 362);
runtime.receive({
  protocol: 1, type: 'rail.snapshot', registryRevision: 1,
  selectionGeneration: 12, pageGeneration: 50, activePlugin: -1,
  lastSelectedPlugin: 0, selectedEntry: 0, expanded: false, railHidden: true, entries
});
assert(ids['tpc-shell'].classList.contains('tpc-collapsed'),
  'a hidden rail still collapses the pane when the game deselects');
assert.strictEqual(runtime.inspect().widgetCount, 0, 'and releases the page');
runtime.receive({
  protocol: 1, type: 'rail.snapshot', registryRevision: 1,
  selectionGeneration: 12, pageGeneration: 50, activePlugin: -1,
  lastSelectedPlugin: 0, selectedEntry: 0, expanded: false, entries
});
assert(!ids['tpc-shell'].classList.contains('tpc-rail-hidden'),
  'a snapshot without the field shows the rail');
Object.defineProperty(railPane, 'clientWidth', { configurable: true, writable: true, value: 320 });

assert.strictEqual(runtime.receive('{bad json'), false, 'malformed host input is ignored');
console.log('modern plugin chrome runtime: ok');
