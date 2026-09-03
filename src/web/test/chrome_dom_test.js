/* The web adapter driven against a fake application document and a recording
 * canonical runtime. Canonical DOM behavior itself lives in
 * plugin_chrome/test/runtime_test.js; this pins the one-frame host boundary. */
'use strict';

const assert = require('assert');
const path = require('path');

function node(tag) {
  const value = {
    tagName: String(tag).toUpperCase(), children: [], parentNode: null,
    className: '', hidden: false, style: {}, _attrs: {}, _events: {},
    clientWidth: 0, clientHeight: 500
  };
  value.classList = {
    contains(name) { return value.className.split(/\s+/).includes(name); },
    add(name) { if (!this.contains(name)) value.className = `${value.className} ${name}`.trim(); },
    remove(name) {
      value.className = value.className.split(/\s+/).filter(v => v && v !== name).join(' ');
    },
    toggle(name, on) { if (on) this.add(name); else this.remove(name); }
  };
  value.style.setProperty = (name, item) => { value.style[name] = item; };
  value.appendChild = child => {
    child.parentNode = value;
    value.children.push(child);
    return child;
  };
  value.removeChild = child => {
    const index = value.children.indexOf(child);
    if (index >= 0) value.children.splice(index, 1);
    child.parentNode = null;
  };
  value.setAttribute = (name, item) => { value._attrs[name] = String(item); };
  value.getAttribute = name => value._attrs[name] || null;
  value.addEventListener = (name, fn) => {
    (value._events[name] = value._events[name] || []).push(fn);
  };
  value.fire = name => (value._events[name] || []).forEach(fn => fn({ target: value }));
  value.getBoundingClientRect = () => ({ width: value.clientWidth, height: value.clientHeight });
  value.focus = () => { value.focused = true; };
  if (value.tagName === 'IFRAME') value.contentWindow = {};
  return value;
}

function fixture() {
  const ids = {};
  const document = {
    documentElement: node('html'), body: node('body'),
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
  const app = add('torirs-app', 'div', document.body);
  app.clientWidth = 900;
  const layout = add('app-content', 'main', app);
  layout.clientWidth = 900;
  const game = add('game-region', 'div', layout);
  const canvas = add('canvas', 'canvas', game);
  const mount = add('plugin-chrome-mount', 'aside', layout);
  mount.hidden = true;
  return { document, ids, app, layout, game, canvas, mount };
}

const built = fixture();
const selects = [];
const layouts = [];
const resize = [];
const focused = [];
const global_ = {
  document: built.document,
  innerWidth: 900,
  Uint8Array,
  btoa(value) { return Buffer.from(value, 'binary').toString('base64'); },
  addEventListener(name, fn) { if (name === 'resize') resize.push(fn); },
  Module: {
    _ToriRSChromeExecWeb_RequestSelect(plugin, generation) {
      selects.push({ plugin, generation });
    },
    _ToriRSChromeExecWeb_RequestLayout(...args) { layouts.push(args); }
  },
  torirsPluginChromeEditorFocus(value, generation) { focused.push({ value, generation }); }
};
global_.window = global_;

const modulePath = path.join(__dirname, '..', 'torirs_chrome.js');
const priorWindow = global.window;
const priorDocument = global.document;
global.window = global_;
global.document = built.document;
delete require.cache[require.resolve(modulePath)];
const exported = require(modulePath);
global.window = priorWindow;
global.document = priorDocument;

const host = global_.torirsPluginChromeHost;
assert(host instanceof exported.PluginChromeHost);
assert.strictEqual(built.mount.children.length, 1, 'one app-owned frame is mounted at boot');
const frame = built.mount.children[0];
assert.strictEqual(frame.tagName, 'IFRAME');
assert.strictEqual(frame.src, 'plugin_chrome/modern.html');
assert.strictEqual(frame.getAttribute('sandbox'), 'allow-scripts allow-same-origin');

const received = [];
const runtime = {
  receive(message) { received.push(JSON.parse(JSON.stringify(message))); return true; }
};
assert(host.attachRuntime(runtime, frame.contentWindow));
assert.strictEqual(received[0].type, 'theme', 'theme reaches runtime before retained state');
assert.strictEqual(received[0].assets.buttonMiddle, 'skin/ButtonMid.png');

const entries = [{ kind: 1, p: -2, pw: 320, title: 'Manage Plugins', icon: '', badge: '' }];
for (let i = 0; i < 32; i++) entries.push({
  kind: 2, p: i, pw: i === 31 ? 400 : 320,
  title: `Plugin ${i}`, icon: `p${i}.png`, badge: i === 31 ? '9' : '',
  attention: i === 31
});

global_.torirsChromeRailSync({
  r: 4, g: 7, pg: 20, a: 31, l: 31, s: 31, x: 1, entries
});
let message = received[received.length - 1];
assert.strictEqual(message.type, 'rail.snapshot');
assert.strictEqual(message.protocol, 1);
assert.strictEqual(message.entries.length, 33, 'Manage and every plugin share one rail');
assert.strictEqual(message.entries[0].pluginIndex, -2, 'Manage sentinel survives translation');
assert.strictEqual(message.entries[32].iconAsset, 'p31.png');
assert.strictEqual(host.layoutMode, 'exclusive', 'compact web allocation replaces the game');
assert.strictEqual(built.game.hidden, true);
assert.strictEqual(built.mount.children[0], frame, 'rail updates never replace the iframe');

assert.strictEqual(global_.torirsChromeOpen(), true);
assert.strictEqual(typeof global_.torirsChromeApplyBatch, 'function',
  'the Wasm executor has one-call retained transaction delivery');
global_.torirsChromeApply({ k: exported.CMD.SYNC_BEGIN });
global_.torirsChromeApply({ k: exported.CMD.CHECK_STYLE, p: -1, w: -1, v: 1 });
global_.torirsChromeApply({
  k: exported.CMD.PANEL_OPEN, p: 3, w: -1, v: 1, text: 'Plugin 31'
});
global_.torirsChromeApply({
  k: exported.CMD.WIDGET_ADD, p: 3, w: 5, tab: -1, v: exported.W.CHECKBOX,
  label: 'Enabled', s: 501
});
global_.torirsChromeApply({ k: exported.CMD.WIDGET_CHECKED, p: 3, w: 5, v: 1 });
global_.torirsChromeApply({ k: exported.CMD.SYNC_END });
message = received[received.length - 1];
assert.strictEqual(message.type, 'page.snapshot', 'first transaction is one atomic snapshot');
assert.strictEqual(message.pageGeneration, 20);
assert.strictEqual(message.checkStyle, 1);
assert.strictEqual(message.commands.find(command => command.w === 5).s, 501,
  'widget semantic serial crosses unchanged');

assert.strictEqual(global_.torirsChromeApplyBatch([
  { k: exported.CMD.SYNC_BEGIN },
  { k: exported.CMD.WIDGET_LABEL, p: 3, w: 5, label: 'Live' },
  { k: exported.CMD.SYNC_END }
]), true);
message = received[received.length - 1];
assert.strictEqual(message.type, 'page.delta', 'later transaction is a retained delta');
assert.strictEqual(message.commands.length, 1);

const beforeOverflow = received.length;
global_.torirsChromeApply({ k: exported.CMD.SYNC_BEGIN });
for (let i = 0; i < 8193; i++) global_.torirsChromeApply({
  k: exported.CMD.WIDGET_LABEL, p: 3, w: 5, label: `overflow ${i}`
});
global_.torirsChromeApply({ k: exported.CMD.SYNC_END });
assert.strictEqual(received.length, beforeOverflow,
  'an oversized transaction drops atomically instead of committing its tail');
global_.torirsChromeApply({ k: exported.CMD.SYNC_BEGIN });
global_.torirsChromeApply({ k: exported.CMD.WIDGET_LABEL, p: 3, w: 5, label: 'Recovered' });
global_.torirsChromeApply({ k: exported.CMD.SYNC_END });
assert.strictEqual(received.length, beforeOverflow + 1,
  'the overflow latch resets at the next transaction');

global_.torirsChromeRailIcon(31, 3, 1, 1, 'ESIz/w==');
message = received[received.length - 1];
assert.deepStrictEqual({
  type: message.type, plugin: message.pluginIndex, revision: message.revision,
  width: message.width, height: message.height, rgba: message.rgbaBase64
}, { type: 'rail.icon', plugin: 31, revision: 3, width: 1, height: 1, rgba: 'ESIz/w==' });

assert(global_.torirsChromeCustom(3, 7, 20, 7007, 2000, 1, 1,
  new Uint32Array([0xff112233])));
message = received[received.length - 1];
assert.strictEqual(message.type, 'custom.bitmap');
assert.strictEqual(message.widgetSerial, 7007);
assert.deepStrictEqual([...Buffer.from(message.rgbaBase64, 'base64')], [0x11, 0x22, 0x33, 0xff],
  'call-scoped ARGB is copied into canonical RGBA order');
assert.strictEqual(global_.torirsChromeCustom(3, 7, 20, 7007, 1000,
  4096, 4096, new Uint32Array(0)), false,
  'custom products above the bounded pixel budget are rejected before allocation');

frame.contentWindow.torirsPluginChromePostMessage(JSON.stringify({
  protocol: 1, type: 'rail.select', sequence: 1,
  pluginIndex: -2, selectionGeneration: 7
}));
assert.deepStrictEqual(selects, [{ plugin: -2, generation: 7 }]);
frame.contentWindow.torirsPluginChromePostMessage(JSON.stringify({
  protocol: 1, type: 'widget.intent', sequence: 2,
  intent: { k: 3, p: 3, w: 5, v: 0, text: '', x: 0, y: 0, g: 20, s: 501 }
}));
assert.deepStrictEqual(JSON.parse(global_.torirsChromeTakeIntent()),
  { k: 3, p: 3, w: 5, v: 0, text: '', x: 0, y: 0, g: 20, s: 501 });
assert.strictEqual(global_.torirsChromeTakeIntent(), '');
frame.contentWindow.torirsPluginChromePostMessage(JSON.stringify({
  protocol: 1, type: 'widget.intent', sequence: 3,
  intent: { k: 3, p: 3, w: 5, v: 1, text: '', x: 0, y: 0, g: 20, s: 500 }
}));
frame.contentWindow.torirsPluginChromePostMessage(JSON.stringify({
  protocol: 1, type: 'widget.intent', sequence: 4,
  intent: { k: 3, p: 3, w: 5, v: 1, text: '', x: 0, y: 0, g: 19, s: 501 }
}));
assert.strictEqual(global_.torirsChromeTakeIntent(), '',
  'ordinary stale-generation and recycled-serial intents are rejected');
frame.contentWindow.torirsPluginChromePostMessage(JSON.stringify({
  protocol: 1, type: 'layout', sequence: 5, selectionGeneration: 7,
  pageGeneration: 20, width: 400, height: 500, scaleMilli: 2000,
  sizeClass: 1, visible: true, gameVisible: true
}));
assert.deepStrictEqual(layouts[0], [7, 400, 500, 2000, 1, 1, 0],
  'outer presenter corrects gameVisible for exclusive mode');
frame.contentWindow.torirsPluginChromePostMessage(JSON.stringify({
  protocol: 1, type: 'editor.focus', sequence: 6, focused: true, pageGeneration: 20
}));
assert.deepStrictEqual(focused, [{ value: true, generation: 20 }]);

/* Same sequence and stale rail generation are rejected at the outer boundary. */
frame.contentWindow.torirsPluginChromePostMessage(JSON.stringify({
  protocol: 1, type: 'rail.select', sequence: 6,
  pluginIndex: 31, selectionGeneration: 7
}));
frame.contentWindow.torirsPluginChromePostMessage(JSON.stringify({
  protocol: 1, type: 'rail.select', sequence: 7,
  pluginIndex: 31, selectionGeneration: 6
}));
assert.strictEqual(selects.length, 1);

frame.contentWindow.torirsPluginChromePostMessage(JSON.stringify({
  protocol: 1, type: 'widget.intent', sequence: 8,
  intent: { k: 3, p: 3, w: 5, v: 0, text: '', x: 0, y: 0, g: 20, s: 501 }
}));

global_.torirsChromeRailSync({
  r: 4, g: 8, pg: 20, a: 31, l: 31, s: 31, x: 0, entries
});
assert.strictEqual(host.layoutMode, 'collapsed');
assert.strictEqual(built.mount.style.width, '42px');
assert.strictEqual(built.game.hidden, false);
assert.strictEqual(built.mount.children.length, 1);
assert.strictEqual(built.mount.children[0], frame, 'collapse retains the one document');

global_.torirsChromeRailSync({
  r: 4, g: 9, pg: 21, a: 2, l: 2, s: 2, x: 1, entries
});
assert.strictEqual(global_.torirsChromeTakeIntent(), '',
  'page replacement discards intents queued by the prior generation');
global_.torirsChromeOpen();
global_.torirsChromeApply({ k: exported.CMD.SYNC_BEGIN });
global_.torirsChromeApply({ k: exported.CMD.PANEL_OPEN, p: 3, text: 'Plugin 2' });
global_.torirsChromeApply({
  k: exported.CMD.WIDGET_ADD, p: 3, w: 5, v: exported.W.BUTTON, text: 'Run', s: 900
});
global_.torirsChromeApply({ k: exported.CMD.SYNC_END });
message = received[received.length - 1];
assert.strictEqual(message.type, 'page.snapshot');
assert.strictEqual(message.pageGeneration, 21);
assert.strictEqual(built.mount.children[0], frame, 'selection replacement reuses the document');

global_.torirsChromeClose();
assert.strictEqual(host.layoutMode, 'collapsed');
assert.strictEqual(built.mount.children[0], frame, 'executor shutdown retains the shared rail document');
assert.strictEqual(received[received.length - 2].type, 'page.close');
assert.strictEqual(received[received.length - 1].type, 'rail.snapshot');
assert.strictEqual(built.canvas.focused, true, 'collapse hands focus back to the game canvas');

console.log('web canonical plugin chrome adapter: ok');
