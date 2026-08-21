/*
 * The web chrome executor's page half, driven against a fake document.
 *
 * Node only -- no browser, no wasm, no build step, matching channel_*.js. What
 * it pins is the half a C test cannot reach: that a command stream produces the
 * right DOM nodes, that tab and hidden state combine into one visibility answer,
 * and that a control the user touches queues an intent naming the chrome handle
 * it came from.
 *
 * The fake document is the smallest one the host actually uses. Growing it past
 * that would be testing the fake.
 */
var assert = require('assert');
var fs = require('fs');
var path = require('path');

/* ---- a document, in about forty lines ------------------------------------ */

function makeNode(tag) {
  var node = {
    tagName: String(tag).toUpperCase(),
    children: [],
    parentNode: null,
    style: {},
    _class: '',
    _text: '',
    _listeners: {},
    type: '',
    value: '',
    checked: false,
    selectedIndex: -1,
    title: ''
  };
  node.classList = {
    add: function (c) { if (node._class.indexOf(c) < 0) node._class = (node._class + ' ' + c).trim(); },
    remove: function (c) {
      node._class = node._class.split(/\s+/).filter(function (x) { return x && x !== c; }).join(' ');
    },
    contains: function (c) { return node._class.split(/\s+/).indexOf(c) >= 0; },
    toggle: function (c, on) { if (on) node.classList.add(c); else node.classList.remove(c); }
  };
  Object.defineProperty(node, 'className', {
    get: function () { return node._class; },
    set: function (v) { node._class = v || ''; }
  });
  Object.defineProperty(node, 'textContent', {
    get: function () { return node._text; },
    set: function (v) { node._text = v == null ? '' : String(v); node.children = []; }
  });
  Object.defineProperty(node, 'firstChild', {
    get: function () { return node.children[0] || null; }
  });
  node.appendChild = function (c) { c.parentNode = node; node.children.push(c); return c; };
  node.removeChild = function (c) {
    var i = node.children.indexOf(c);
    if (i >= 0) { node.children.splice(i, 1); c.parentNode = null; }
    return c;
  };
  node.addEventListener = function (t, f) { (node._listeners[t] = node._listeners[t] || []).push(f); };
  node.fire = function (t) { (node._listeners[t] || []).forEach(function (f) { f(); }); };
  node.querySelector = function (sel) {
    /* Only the one selector the host uses. */
    var want = sel.replace('span.', '');
    for (var i = 0; i < node.children.length; i++) {
      var c = node.children[i];
      if (c.tagName === want.toUpperCase() && c.classList.contains('lbl')) return c;
      if (c.classList.contains('lbl')) return c;
    }
    return null;
  };
  return node;
}

var document = {
  head: makeNode('head'),
  body: makeNode('body'),
  activeElement: null,
  createElement: makeNode,
  getElementById: function () { return null; }
};

var global_ = { document: document };
global_.window = global_;

var src = fs.readFileSync(path.join(__dirname, '..', 'torirs_chrome.js'), 'utf8');
var moduleShim = { exports: {} };
new Function('window', 'globalThis', 'document', 'module', 'console', src)(
  global_, global_, document, moduleShim, console);

var CMD = moduleShim.exports.CMD;
var W = moduleShim.exports.W;
var INTENT = moduleShim.exports.INTENT;

/* ---- helpers -------------------------------------------------------------- */

var checks = 0;
var failures = 0;
function check(cond, what) {
  checks++;
  if (cond) return;
  failures++;
  console.error('FAIL: ' + what);
}

function cmd(k, over) {
  var base = { k: k, p: 0, w: -1, tab: -1, v: 0, c: 0, x: 0, y: 0, cw: 0, ch: 0, label: '', text: '' };
  for (var key in (over || {})) base[key] = over[key];
  return base;
}
function apply(c) { global_.torirsChromeApply(c); }
function intents() {
  var out = [];
  for (;;) {
    var s = global_.torirsChromeTakeIntent();
    if (!s) break;
    out.push(JSON.parse(s));
  }
  return out;
}
/* The host puts one root in the page; its three children are the title bar,
 * the tab strip and the body. Reached positionally because the host exposes no
 * handles -- a page never needs them, and neither does this. */
function root() { return document.body.children[0]; }
function tabStrip() { return root().children[1]; }
function rows() { return root().children[2].children; }

/* ---- the run -------------------------------------------------------------- */

check(global_.torirsChromeOpen() === true, 'the window opens');
check(document.body.children.length === 1, 'and puts exactly one root in the page');

apply(cmd(CMD.PANEL_OPEN, { p: 0, v: 0, text: 'Plugins' }));

/* A tab strip: its titles arrive as this widget's options, and it renders in
 * the header rather than as a row. */
apply(cmd(CMD.WIDGET_ADD, { w: 1, v: W.TABSTRIP, tab: -1 }));
apply(cmd(CMD.WIDGET_OPTIONS, { w: 1, v: 2 }));
apply(cmd(CMD.WIDGET_OPTION, { w: 1, v: 0, text: 'Plugins' }));
apply(cmd(CMD.WIDGET_OPTION, { w: 1, v: 1, text: 'Beam Demo' }));

var tabsEl = tabStrip();
check(tabsEl.children.length === 2, 'the strip renders one button per tab');
check(tabsEl.children[0].classList.contains('on'), 'tab 0 starts selected');

/* Rows on two different tabs, plus one on every tab. */
apply(cmd(CMD.WIDGET_ADD, { w: 2, v: W.CHECKBOX, tab: 0, label: 'enabled' }));
apply(cmd(CMD.WIDGET_ADD, { w: 3, v: W.TEXTINPUT, tab: 1, label: 'colour' }));
apply(cmd(CMD.WIDGET_ADD, { w: 4, v: W.BUTTON, tab: 1, text: 'Save' }));
apply(cmd(CMD.WIDGET_ADD, { w: 5, v: W.LABEL, tab: -1, text: 'always' }));

check(rows().length === 5, 'every widget got a row, strip included');

/* Rows are appended in ADD order, so a handle's row is its position in the
 * order the adds above used. */
var ADD_ORDER = [1, 2, 3, 4, 5];
function rowFor(handle) {
  var at = ADD_ORDER.indexOf(handle);
  if (at < 0) throw new Error('handle ' + handle + ' was never added');
  return rows()[at];
}
/* Later sections add more; kept as one list so a row's position stays the
 * order it was announced in, which is the property being relied on. */
function addedRow(handle) { ADD_ORDER.push(handle); return rowFor(handle); }
check(!rowFor(2).classList.contains('hidden'), "the active tab's row shows");
check(rowFor(3).classList.contains('hidden'), "an inactive tab's row does not");
check(!rowFor(5).classList.contains('hidden'), 'a row on every tab shows');

apply(cmd(CMD.PANEL_TAB, { p: 0, v: 1 }));
check(rowFor(2).classList.contains('hidden'), 'switching hides the old tab');
check(!rowFor(3).classList.contains('hidden'), 'switching shows the new tab');
check(!rowFor(5).classList.contains('hidden'), 'and a global row survives it');
check(tabsEl.children[1].classList.contains('on'), 'the strip follows the switch');

/* hidden AND tab are AND-ed, the same rule the C mirror applies. */
apply(cmd(CMD.WIDGET_HIDDEN, { w: 3, v: 1 }));
check(rowFor(3).classList.contains('hidden'), 'hidden beats active-tab');
apply(cmd(CMD.WIDGET_HIDDEN, { w: 3, v: 0 }));
check(!rowFor(3).classList.contains('hidden'), 'unhiding on the active tab restores it');

/* ---- intents -------------------------------------------------------------- */

intents(); /* drain anything the setup queued */

rowFor(4).children[0].fire('click');
var got = intents();
check(got.length === 1, 'a button click queues one intent');
check(got[0].k === INTENT.ACTIVATE && got[0].w === 4, 'naming the chrome handle it came from');

apply(cmd(CMD.PANEL_TAB, { p: 0, v: 0 }));
intents();
var box = rowFor(2).children[0];
box.checked = true;
box.fire('change');
got = intents();
check(got.length === 1 && got[0].k === INTENT.TOGGLE, 'a checkbox queues a toggle');
check(got[0].v === 1, 'carrying its new state');

apply(cmd(CMD.PANEL_TAB, { p: 0, v: 1 }));
intents();
var input = rowFor(3).children[1];
input.value = '#00FF00';
input.fire('change');
got = intents();
check(got.length === 1 && got[0].k === INTENT.TEXT, 'an edit queues a text intent');
check(got[0].text === '#00FF00', 'carrying the whole new value');

/* A model echo must not fight the user: text is not written back into a field
 * that has focus, or the caret jumps and what they typed is undone. */
document.activeElement = input;
apply(cmd(CMD.WIDGET_TEXT, { w: 3, text: '#000000' }));
check(input.value === '#00FF00', 'a focused field is not overwritten by an echo');
document.activeElement = null;
apply(cmd(CMD.WIDGET_TEXT, { w: 3, text: '#000000' }));
check(input.value === '#000000', 'an unfocused one is');

/* Clicking a tab reports, and does NOT switch on its own: the model decides. */
intents();
tabsEl.children[0].fire('click');
got = intents();
check(got.length === 1 && got[0].k === INTENT.TAB && got[0].v === 0, 'a tab click reports it');

/* ---- the kinds the page used to have no branch for ------------------------ */

/*
 * LISTROW, COLORPICK and MODELVIEW all fell through to the generic branch,
 * which renders a bare span. The roster is built out of LISTROWs, so the whole
 * plugin list arrived as a column of unclickable text: no switch, no way into
 * a plugin's settings. Checked here per kind, and the roster's two DIFFERENT
 * outcomes checked separately -- a row you can toggle but not open is the
 * failure this kind exists to prevent.
 */
apply(cmd(CMD.WIDGET_ADD, { w: 20, v: W.LISTROW, tab: -1, label: 'windemo', cw: 1 }));
apply(cmd(CMD.WIDGET_CHECKED, { w: 20, v: 1 }));

var listRow = addedRow(20);
check(listRow !== null, 'a roster row gets a row');
var rowName = listRow.children.filter(function (c) { return c.classList.contains('rowname'); });
var rowAct = listRow.children.filter(function (c) { return c.classList.contains('rowact'); });
var rowSw = listRow.children.filter(function (c) { return c.classList.contains('rowsw'); });
check(rowName.length === 1 && rowName[0].textContent === 'windemo', 'carrying its name');
check(rowAct.length === 1, 'and its settings affordance, because cw said it has one');
check(rowSw.length === 1, 'and its switch');
check(rowSw[0].checked === true, 'which follows WIDGET_CHECKED');

intents();
rowSw[0].checked = false;
rowSw[0].fire('change');
got = intents();
check(got.length === 1 && got[0].k === INTENT.TOGGLE, "the switch reports a TOGGLE");
check(got[0].v === 0 && got[0].w === 20, 'naming its handle and new state');

rowAct[0].fire('click');
got = intents();
check(got.length === 1 && got[0].k === INTENT.ACTION, "the affordance reports an ACTION, not a toggle");
check(got[0].w === 20, 'for the same row');

/* A row with no action must not grow one: the flag is part of its shape. */
apply(cmd(CMD.WIDGET_ADD, { w: 21, v: W.LISTROW, tab: -1, label: 'lua', cw: 0 }));
check(
  addedRow(21).children.filter(function (c) { return c.classList.contains('rowact'); }).length === 0,
  'a row without an action gets no affordance');

/* COLORPICK: the hex field is the control, and the swatch follows it. */
apply(cmd(CMD.WIDGET_ADD, { w: 22, v: W.COLORPICK, tab: -1, label: 'beam colour' }));
apply(cmd(CMD.WIDGET_TEXT, { w: 22, text: '#FFCC00' }));
var pick = addedRow(22);
var hexField = pick.children.filter(function (c) { return c.classList.contains('hex'); })[0];
var swatch = pick.children.filter(function (c) { return c.classList.contains('swatch'); })[0];
check(hexField && hexField.value === '#FFCC00', 'a colour field shows its hex');
check(swatch && swatch.style.background === '#FFCC00', 'and the swatch follows it');
hexField.value = '#102030';
hexField.fire('change');
got = intents();
check(got.length === 1 && got[0].k === INTENT.TEXT, 'editing the hex reports a TEXT intent');
check(got[0].text === '#102030', 'carrying the new value');

/* MODELVIEW: a placeholder, but a real element -- it takes the focus. */
apply(cmd(CMD.WIDGET_ADD, { w: 23, v: W.MODELVIEW, tab: -1, label: 'preview' }));
check(
  addedRow(23).children.filter(function (c) { return c.classList.contains('modelview'); }).length === 1,
  'a model view gets a placeholder rather than an empty row');

/* WIDGET_COLOR was not handled at all: the roster greys a plugin that failed
 * to load, and dropping the command loses that signal entirely. */
apply(cmd(CMD.WIDGET_COLOR, { w: 21, c: 0x9F9F9F }));
check(
  rowFor(21).children.filter(function (c) { return c.classList.contains('rowname'); })[0]
    .style.color === '#9f9f9f',
  'a widget colour reaches the page');

/* ---- removal and close ---------------------------------------------------- */

var before = rows().length;
apply(cmd(CMD.WIDGET_REMOVE, { w: 4 }));
check(rows().length === before - 1, 'a removed widget loses its row');

apply(cmd(CMD.PANEL_CLOSE, { p: 0 }));
check(rows().length === 0, "closing a panel takes every one of its rows, unannounced");

global_.torirsChromeClose();
check(document.body.children.length === 0, 'closing the window removes its root');

if (failures) {
  console.error(checks + ' checks, ' + failures + ' failure(s)');
  process.exit(1);
}
console.log(checks + ' checks, 0 failures');
