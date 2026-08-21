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
  /* The window is inserted NEXT TO the stage rather than appended to the end
   * of the page, so the fake has to be able to say where "next to" is. */
  node.insertBefore = function (c, ref) {
    var i = ref ? node.children.indexOf(ref) : -1;
    c.parentNode = node;
    if (i < 0) node.children.push(c);
    else node.children.splice(i, 0, c);
    return c;
  };
  Object.defineProperty(node, 'nextSibling', {
    get: function () {
      if (!node.parentNode) return null;
      var i = node.parentNode.children.indexOf(node);
      return (i >= 0 && node.parentNode.children[i + 1]) || null;
    }
  });
  /* An iframe is a window with a document of its own, which is the whole
   * point of using one: the chrome's controls are then out of reach of the
   * host page's stylesheet and its key listeners. */
  if (node.tagName === 'IFRAME') node.contentDocument = makeDocument();
  /*
   * A canvas that can be drawn on, because the skin path turns baked pixels
   * into data: URLs through one -- and a fake that could not would test the
   * "this browser has no canvas, stay flat" fallback while the shipping path
   * went unchecked, which is the exact trap this file exists to avoid.
   *
   * It records what it was handed rather than rasterising: what is being
   * pinned is that the host decodes the right number of pixels and composes
   * the frame out of the right nine, not that a canvas works.
   */
  if (node.tagName === 'CANVAS') {
    node.width = 0;
    node.height = 0;
    node._puts = [];
    node._draws = [];
    node.getContext = function () {
      return {
        putImageData: function (img) { node._puts.push(img); },
        drawImage: function (img, x, y) { node._draws.push({ src: img.src, x: x, y: y }); }
      };
    };
    node.toDataURL = function () {
      return 'data:image/png;base64,#' + node.width + 'x' + node.height +
        '/' + node._puts.length + '+' + node._draws.length;
    };
  }
  /* The canvas is measured for its box, and the frame for where the row put
   * it -- the window lines up with the picture rather than with the row, so
   * both a size and a top have to be answerable here. */
  node.getBoundingClientRect = function () {
    /* A top margin moves the box down -- the one layout rule the host
     * depends on, because it lines the window up by measuring the gap that
     * is left and correcting it. A fake that ignored the margin would let a
     * correction that never converges pass. */
    var top = (node._rectT || 0) + (parseFloat(node.style.marginTop) || 0);
    var w = node._rectW || 0;
    var h = node._rectH || 0;
    /* `bottom` and `right` are the far edges a real DOMRect carries, and the
     * open dropdown's placement is written against them: it hangs the list off
     * the button's BOTTOM and flips it above when that would leave the root. */
    return { width: w, height: h, left: 0, top: top, bottom: top + h, right: w };
  };
  /* Scrolling a row into view is a no-op here: what it does is browser
   * behaviour, and what matters is that the host may call it. */
  node.scrollIntoView = function () {};
  /* Attributes are recorded rather than modelled: the host sets `role` and
   * `aria-expanded` on the dropdown so a screen reader can follow it, and a
   * fake with no setAttribute would make that a crash instead of a feature. */
  node._attrs = {};
  node.setAttribute = function (k, v) { node._attrs[k] = String(v); };
  node.getAttribute = function (k) {
    return Object.prototype.hasOwnProperty.call(node._attrs, k) ? node._attrs[k] : null;
  };
  /* The chrome publishes its docked width as a CSS variable on the page, which
   * is the one style write that does not go through a named property. */
  node.style.setProperty = function (k, v) { node.style[k] = v; };
  node.style.removeProperty = function (k) { delete node.style[k]; };
  node.addEventListener = function (t, f) { (node._listeners[t] = node._listeners[t] || []).push(f); };
  /* Taken off again, because a control that opens a list adds listeners for as
   * long as the list is up -- a fake with no removal would let a leak of them
   * pass, and the symptom of that one is a dismisser firing at a list that is
   * no longer there. */
  node.removeEventListener = function (t, f) {
    node._listeners[t] = (node._listeners[t] || []).filter(function (x) { return x !== f; });
  };
  /* Listeners get an event, because in a browser they always do -- a handler
   * that calls ev.stopPropagation() should not be the thing that discovers the
   * fake was passing undefined. `over` carries the fields a particular event
   * has (a key, say), which is how the keyboard paths are driven. */
  node.fire = function (t, over) {
    var ev = { type: t, target: node, stopPropagation: function () {}, preventDefault: function () {} };
    for (var k in (over || {})) ev[k] = over[k];
    (node._listeners[t] || []).forEach(function (f) { f(ev); });
  };
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

function makeDocument(byId) {
  var listeners = {};
  var doc = {
    documentElement: makeNode('html'),
    head: makeNode('head'),
    body: makeNode('body'),
    activeElement: null,
    title: '',
    createElement: makeNode,
    getElementById: function (id) { return (byId && byId[id]) || null; },
    /* The DOCUMENT takes listeners too, which is where an open dropdown puts
     * the press that dismisses it -- the one listener in this host that is not
     * on a node it made. */
    addEventListener: function (t, f) { (listeners[t] = listeners[t] || []).push(f); },
    removeEventListener: function (t, f) {
      listeners[t] = (listeners[t] || []).filter(function (x) { return x !== f; });
    },
    fire: function (t, over) {
      var ev = {
        type: t, target: null,
        stopPropagation: function () {}, preventDefault: function () {}
      };
      for (var k in (over || {})) ev[k] = over[k];
      (listeners[t] || []).slice().forEach(function (f) { f(ev); });
    },
    listenerCount: function (t) { return (listeners[t] || []).length; }
  };
  return doc;
}

var document = makeDocument();

/*
 * The page's globals the host reaches for, and no more.
 *
 * `open` and `ResizeObserver` are here because the window now has somewhere to
 * go: a tab of its own, and a height that has to follow the canvas. Both are
 * feature-tested by the host, so leaving them out would silently test the
 * fallback instead of the path that ships -- which is the trap this fake is
 * meant to avoid, not set.
 */
var popups = [];
var observers = [];
var global_ = {
  document: document,
  /* The three the skin decode reaches for. Node has the last two already; they
   * are named here because the host reads them off `global`, which in a browser
   * is `window` and here is this object. */
  atob: function (b64) { return Buffer.from(b64, 'base64').toString('binary'); },
  Uint8ClampedArray: Uint8ClampedArray,
  ImageData: function (data, w, h) { this.data = data; this.width = w; this.height = h; },
  Image: function () { this.src = ''; },
  open: function () {
    var win = {
      document: makeDocument(),
      closed: false,
      close: function () { this.closed = true; }
    };
    popups.push(win);
    return win;
  },
  ResizeObserver: function (fn) {
    this.observe = function (target) { observers.push({ fn: fn, target: target }); };
    this.disconnect = function () {
      observers = observers.filter(function (o) { return o.fn !== fn; });
    };
  }
};
global_.window = global_;

var src = fs.readFileSync(path.join(__dirname, '..', 'torirs_chrome.js'), 'utf8');
var moduleShim = { exports: {} };
new Function('window', 'globalThis', 'document', 'module', 'console', src)(
  global_, global_, document, moduleShim, console);

var ChromeHost = moduleShim.exports.ChromeHost;
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

/* ---- the panel's own box -------------------------------------------------- */

/*
 * PANEL_RECT was dropped entirely, so a panel the model sized to its content
 * still rendered in the page's fixed 340px column and ellipsised every name
 * that did not fit. Width is honoured and clamped; position is not, because
 * this window is a corner overlay rather than something laid out in canvas
 * coordinates.
 */
apply(cmd(CMD.PANEL_RECT, { p: 0, x: 30, y: 40, cw: 420, ch: 900 }));
check(root().style.width === '444px', "a panel's width reaches the page, plus its padding");
apply(cmd(CMD.PANEL_RECT, { p: 0, x: 30, y: 40, cw: 4000, ch: 900 }));
check(root().style.width === '560px', 'and is clamped rather than taking the screen');
apply(cmd(CMD.PANEL_RECT, { p: 0, x: 30, y: 40, cw: 10, ch: 900 }));
check(root().style.width === '240px', 'clamped at the bottom too');

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
var wrap = pick.children.filter(function (c) { return c.classList.contains('colorpick'); })[0];
check(wrap !== undefined, 'a colour field gets its own group');
var swatch = wrap.children.filter(function (c) { return c.classList.contains('swatch'); })[0];
var hexField = wrap.children.filter(function (c) { return c.type === 'text'; })[0];
check(swatch && swatch.type === 'color', "the swatch is the platform's picker");
check(hexField && hexField.value === '#FFCC00', 'the hex field shows the value');
check(swatch && swatch.value === '#FFCC00', 'and the swatch follows it');
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

/* ---- the HSL16 colour row -------------------------------------------------
 *
 * The model's picker is three HSL16 axis bars, which a page cannot draw. It
 * uses the platform's idiom instead -- <input type=color> beside the hex --
 * which is a trade the DROPDOWN no longer makes (see the section below: the
 * list a <select> opens is the operating system's and cannot be made to look
 * like the game, so the page builds its own). What matters is that BOTH
 * controls commit through one path, TEXT, so the quantiser stays in C where
 * the palette is; a second conversion in JS would be a second place for a
 * colour to land on a different entry than the one the game draws.
 */
apply(cmd(CMD.WIDGET_ADD, { w: 30, v: W.COLORPICK, tab: -1, label: 'True tile', text: '#0feff9' }));
var colorRow = addedRow(30);
var colorWrap = colorRow.children.filter(function (c) { return c.classList.contains('colorpick'); })[0];
check(!!colorWrap, 'a colour row gets a swatch/hex pair rather than the generic branch');
check(colorWrap.children[0].type === 'color', 'the swatch is the platform colour input');
check(colorWrap.children[1].type === 'text', 'and the hex stays typeable');

apply(cmd(CMD.WIDGET_TEXT, { w: 30, text: '#123456' }));
check(colorWrap.children[0].value === '#123456', 'the model drives the swatch');
check(colorWrap.children[1].value === '#123456', 'and the hex field');

colorWrap.children[0].value = '#ff0000';
colorWrap.children[0].fire('change');
var picked = intents();
check(picked.length === 1 && picked[0].k === INTENT.TEXT, 'the swatch commits as a TEXT intent');
check(picked[0].w === 30 && picked[0].text === '#ff0000', 'carrying what the user chose');

colorWrap.children[1].value = '#00ff00';
colorWrap.children[1].fire('change');
var typed = intents();
check(typed.length === 1 && typed[0].k === INTENT.TEXT, 'and so does the hex field');
check(typed[0].text === '#00ff00', 'carrying what was typed');

/* The model's focus is downstream of the browser's here, so the page must not
 * act on it -- calling .focus() on the way back would fight the caret the user
 * just placed. Applying it is asserted to be harmless rather than absent. */
apply(cmd(CMD.WIDGET_FOCUS, { w: 30, v: 1 }));
check(intents().length === 0, 'a focus command is inert on the page');

/* ---- the dropdown, which is not a <select> ---------------------------------
 *
 * It was one. The closed button could be made to look like the game -- the
 * tile, the frame, the arrow at the right -- but the LIST a <select> opens is
 * drawn by the operating system and no page may style it, so the one part of
 * the window the user opens in order to LOOK at it was the one part that could
 * not match. The page builds script_9114's list itself now, and what is pinned
 * here is everything a <select> used to do for free: it opens, it names the
 * option that was chosen, it shuts on the next press anywhere else, and it
 * still takes the keyboard.
 */
var DD_OPTS = ['First item in tab', 'Digit (1, 2, 3)', 'Roman numeral (I, II, III)', 'Hide tab bar'];

apply(cmd(CMD.WIDGET_ADD, { w: 40, v: W.DROPDOWN, tab: -1, label: 'Tab display' }));
apply(cmd(CMD.WIDGET_OPTIONS, { w: 40, v: DD_OPTS.length }));
DD_OPTS.forEach(function (text, i) {
  apply(cmd(CMD.WIDGET_OPTION, { w: 40, v: i, text: text }));
});
apply(cmd(CMD.WIDGET_SELECTED, { w: 40, v: 0, text: DD_OPTS[0] }));
apply(cmd(CMD.WIDGET_TEXT, { w: 40, text: DD_OPTS[0] }));

var ddRow = addedRow(40);
var ddBtn = ddRow.children.filter(function (c) { return c.classList.contains('dd'); })[0];
check(!!ddBtn, 'a dropdown gets the shared field box, not a <select>');
check(ddBtn.tagName === 'SPAN', 'which is a span, because a select cannot be styled open');
check(ddBtn.children[0].textContent === DD_OPTS[0], 'showing the model\'s value');

/** The list, if one is up. It is placed against the window ROOT rather than
 *  the row, so that is where to look for it. */
function ddList() {
  return root().children.filter(function (c) {
    return c.classList.contains('torirs-chrome-ddlist');
  })[0];
}

check(!ddList(), 'and no list until it is asked for');

intents();
ddBtn.fire('mousedown');
var list = ddList();
check(!!list, 'pressing the button opens a list');
check(list.children.length === DD_OPTS.length, 'with one row per option');
check(list.children[2].textContent === DD_OPTS[2], 'each carrying its own text');
check(ddBtn.classList.contains('open'), 'and the button says it is open, for the up arrow');
check(intents().length === 0, 'opening a list chooses nothing on its own');

/* The press that opens must not reach the document dismisser on the same
 * event, or the button would open the list and shut it again. */
check(!!ddList(), 'the press that opened it did not also dismiss it');

list.children[2].fire('mousedown');
got = intents();
check(got.length === 1 && got[0].k === INTENT.PICK, 'choosing a row queues a PICK');
check(got[0].w === 40 && got[0].v === 2, 'naming the handle and the option index');
check(got[0].text === DD_OPTS[2], 'and the option itself');
check(!ddList(), 'and the list shuts behind it');
check(!ddBtn.classList.contains('open'), 'the button with it');

/* The VALUE is the model's answer, not the row that was clicked: one place
 * decides what the setting became. */
check(ddBtn.children[0].textContent === DD_OPTS[0], 'the button still shows the model value');
apply(cmd(CMD.WIDGET_SELECTED, { w: 40, v: 2, text: DD_OPTS[2] }));
apply(cmd(CMD.WIDGET_TEXT, { w: 40, text: DD_OPTS[2] }));
check(ddBtn.children[0].textContent === DD_OPTS[2], 'and follows it when it answers');

/* Modal, the way every dropdown in this game is: the next press anywhere else
 * takes it down, whatever that press was for. */
ddBtn.fire('mousedown');
check(!!ddList(), 'it opens again');
document.fire('mousedown');
check(!ddList(), 'a press elsewhere dismisses it');
check(document.listenerCount('mousedown') === 0, 'and takes its listener off the document');

/* Pressing the button while it is open shuts it rather than reopening. */
ddBtn.fire('mousedown');
ddBtn.fire('mousedown');
check(!ddList(), 'the button toggles');

/* The keyboard, which the <select> had for free. */
ddBtn.fire('keydown', { key: 'ArrowDown' });
check(!!ddList(), 'an arrow key opens the list');
ddList().children.forEach(function (r) { r.classList.remove('cursor'); });
ddBtn.fire('keydown', { key: 'ArrowDown' });
check(ddList().children[3].classList.contains('cursor'), 'and the arrows move a cursor row');
ddBtn.fire('keydown', { key: 'ArrowUp' });
check(ddList().children[2].classList.contains('cursor'), 'both ways');
check(
  ddList().children.filter(function (r) { return r.classList.contains('cursor'); }).length === 1,
  'one row at a time');
intents();
ddBtn.fire('keydown', { key: 'Enter' });
got = intents();
check(got.length === 1 && got[0].k === INTENT.PICK && got[0].v === 2, 'Enter chooses the cursor row');
check(!ddList(), 'and shuts the list');

ddBtn.fire('keydown', { key: 'ArrowDown' });
check(!!ddList(), 'reopened');
intents();
ddBtn.fire('keydown', { key: 'Escape' });
check(!ddList(), 'Escape shuts it');
check(intents().length === 0, 'and changes nothing');

/* A list being restated is not the list that is open, and an index into the
 * old one means nothing against the new. */
ddBtn.fire('mousedown');
check(!!ddList(), 'open again');
apply(cmd(CMD.WIDGET_OPTIONS, { w: 40, v: 2 }));
check(!ddList(), 'restating the options shuts the open list');
apply(cmd(CMD.WIDGET_OPTION, { w: 40, v: 0, text: 'Only' }));
apply(cmd(CMD.WIDGET_OPTION, { w: 40, v: 1, text: 'Two' }));
ddBtn.fire('mousedown');
check(ddList().children.length === 2, 'and the next open shows the new list');
document.fire('mousedown');

/* A hidden row, a tab switch or the row going away all leave a list hanging
 * off a button that is not there. */
ddBtn.fire('mousedown');
apply(cmd(CMD.WIDGET_HIDDEN, { w: 40, v: 1 }));
check(!ddList(), 'hiding the row takes its list with it');
apply(cmd(CMD.WIDGET_HIDDEN, { w: 40, v: 0 }));
ddBtn.fire('mousedown');
apply(cmd(CMD.WIDGET_REMOVE, { w: 40 }));
check(!ddList(), 'and so does removing it');
check(document.listenerCount('mousedown') === 0, 'with no listener left behind');
ADD_ORDER.pop();

/* ---- the two ways out ------------------------------------------------------ */

/*
 * A window that can be abandoned but not committed is the failure CONFIRM
 * exists to prevent, and the page had only a close box. Both are reported
 * rather than acted on -- the model decides whether the window is up.
 */
var titleBar = root().children[0];
var okBtn = titleBar.children.filter(function (c) { return c.classList.contains('ok'); })[0];
var closeBtn = titleBar.children.filter(function (c) { return c.classList.contains('close'); })[0];
check(okBtn !== undefined, 'the title bar offers Ok');
check(closeBtn !== undefined, 'and Close');

intents();
okBtn.fire('click');
got = intents();
check(got.length === 1 && got[0].k === INTENT.CONFIRM, 'Ok reports a CONFIRM');
closeBtn.fire('click');
got = intents();
check(got.length === 1 && got[0].k === INTENT.CLOSE, 'Close reports a CLOSE, not a confirm');
check(document.body.children.length === 1, 'and neither takes the window down itself');

/* ---- the baked skin -------------------------------------------------------
 *
 * The window wears the game's own art, handed over from C at open time. What
 * is pinned here is the half a C test cannot see: that the metrics reach the
 * stylesheet, that the sprites become data: URLs the sheet names, that the
 * nine frame pieces are composed into one image in the right order, and -- the
 * one that matters most -- that a MISSING sprite leaves the window flat rather
 * than half-skinned.
 */

/** The live stylesheet: the last <style> the host wrote into this document. */
function sheet() {
  var styles = document.head.children.filter(function (c) { return c.tagName === 'STYLE'; });
  return styles.length ? styles[styles.length - 1].textContent : '';
}

/** A solid-colour sprite as base64 RGBA, the shape C sends. */
function fakeSprite(w, h) {
  var bytes = Buffer.alloc(w * h * 4, 0x7F);
  return bytes.toString('base64');
}

/* Every slot the sheet names, plus the eight frame pieces it composes. */
var SKIN = moduleShim.exports.SKIN;
var SKIN_NEEDED = [
  SKIN.PANEL_BODY, SKIN.DROPDOWN_BODY, SKIN.CHECK_ON, SKIN.CHECK_OFF,
  SKIN.SCROLL_UP, SKIN.SCROLL_DOWN, SKIN.SCROLL_TRACK, SKIN.SCROLL_GRIP_MID,
  SKIN.FRAME_TOP_LEFT, SKIN.FRAME_TOP, SKIN.FRAME_TOP_RIGHT,
  SKIN.FRAME_LEFT, SKIN.FRAME_RIGHT,
  SKIN.FRAME_BOTTOM_LEFT, SKIN.FRAME_BOTTOM, SKIN.FRAME_BOTTOM_RIGHT
];

function sendSkin(skip) {
  global_.torirsChromeSkinMetrics({ rowH: 18, labelW: 104, box: 17, frame: 3 });
  SKIN_NEEDED.forEach(function (slot) {
    if (slot === skip) return;
    global_.torirsChromeSkinSprite(slot, 4, 4, fakeSprite(4, 4));
  });
  global_.torirsChromeSkinDone();
}

var chromeRoot = document.body.children[0].contentDocument
  ? document.body.children[0].contentDocument.body.children[0]
  : document.body.children[0];

/* One sprite short: flat, and visibly so. A frame with no tile behind it reads
 * as a rendering fault, where a complete flat window reads as a theme. */
sendSkin(SKIN.PANEL_BODY);
check(!chromeRoot.classList.contains('skinned'), 'one sprite short leaves the window flat');

/* The whole set: skinned, and the sheet names the images. */
sendSkin(-1);
check(chromeRoot.classList.contains('skinned'), 'the full set skins the window');
check(sheet().indexOf('.torirs-chrome.skinned') >= 0, 'the skin sheet was written');
check(sheet().indexOf('border-image:url(data:image/png') >= 0,
  'the panel wears the nine-slice frame');
check(sheet().indexOf('data:image/png;base64,#9x9/0+8') >= 0,
  'the frame is composed from EIGHT pieces on a 9x9 canvas -- the centre is left empty');

/*
 * The metrics reach the layout.
 *
 * 18 chrome pixels at the page's 2x is a 36px row, and the point of sending
 * them at all is that the page stops laying out on numbers of its own. Checked
 * through the sheet rather than a computed style, because the fake document
 * has no cascade -- what is being pinned is that the number crossed, not that
 * a browser applies CSS.
 */
global_.torirsChromeSkinMetrics({ rowH: 40 });
global_.torirsChromeSkinDone();
check(sheet().indexOf('height:80px') >= 0, 'a metric from C lands in the stylesheet');
global_.torirsChromeSkinMetrics({ rowH: 18 });
global_.torirsChromeSkinDone();

/* ---- removal and close ---------------------------------------------------- */

var before = rows().length;
apply(cmd(CMD.WIDGET_REMOVE, { w: 4 }));
check(rows().length === before - 1, 'a removed widget loses its row');

apply(cmd(CMD.PANEL_CLOSE, { p: 0 }));
check(rows().length === 0, "closing a panel takes every one of its rows, unannounced");

global_.torirsChromeClose();
check(document.body.children.length === 0, 'closing the window removes its root');

/* ---- Ok and Close name the panel, tab strip or not ------------------------- */

/*
 * The close mark was DEAD in the shipped window, and this is the fixture that
 * hid it: everything above builds a TABSTRIP, and the panel the title bar
 * addressed used to be latched when a strip arrived. The plugin window is
 * PAGED, not tabbed -- no strip ever arrives -- so the buttons named panel -1,
 * the model validated that away, and the mark did nothing at all.
 *
 * So this runs the same two buttons on a window that never sees a strip.
 */
(function () {
  var h = new ChromeHost();
  check(h.open() === true, 'a window with no tab strip opens');
  h.apply(cmd(CMD.PANEL_OPEN, { p: 4, text: 'Plugins' }));
  h.apply(cmd(CMD.WIDGET_ADD, { w: 1, v: W.LISTROW, tab: -1, label: 'windemo' }));

  var bar = h.root.children[0];
  function button(cls) {
    return bar.children.filter(function (c) { return c.classList.contains(cls); })[0];
  }
  function queued() {
    var out = [];
    for (;;) { var j = h.takeIntent(); if (!j) break; out.push(JSON.parse(j)); }
    return out;
  }

  queued();
  button('close').fire('click');
  var one = queued();
  check(one.length === 1 && one[0].k === INTENT.CLOSE, 'Close reports a CLOSE');
  check(one[0].p === 4, 'naming the panel the window is showing, with no strip to say so');

  button('ok').fire('click');
  one = queued();
  check(one.length === 1 && one[0].k === INTENT.CONFIRM, 'Ok reports a CONFIRM');
  check(one[0].p === 4, 'naming the same panel');
  h.close();
})();

/* ---- beside the canvas, in a frame of its own ------------------------------ */

/*
 * Where the window GOES. Pinned to a corner it covered the part of the frame
 * the player was looking at; every executor with a real window of its own had
 * already answered this the other way. So: an iframe next to the stage, as
 * tall as the canvas, tracking it.
 *
 * The page is swapped for one that HAS a canvas -- the sections above ran
 * against one that does not, which is the floating fallback and is checked by
 * their passing at all.
 */
(function () {
  var stage = makeNode('div');
  var canvas = makeNode('canvas');
  var main = makeNode('main');
  var log = makeNode('section');
  var page = makeDocument({ canvas: canvas });

  canvas._rectW = 765;
  canvas._rectH = 503;
  stage.appendChild(canvas);
  main.appendChild(stage);
  /* A second flex child, so "inserted next to the stage" is a claim with
   * somewhere else to be wrong: appended, it would land after this. */
  main.appendChild(log);
  page.body.appendChild(main);
  global_.document = page;

  var h = new ChromeHost();
  check(h.open() === true, 'the window opens against a page with a canvas');
  check(page.body.children[0] === main, 'and does not go in the page body');

  var frame = main.children.filter(function (c) { return c.tagName === 'IFRAME'; })[0];
  check(frame !== undefined, 'it mounts in an iframe');
  check(main.children.indexOf(frame) === 1, 'inserted next to the stage, not at the end');
  check(frame.contentDocument.body.children[0] === h.root, 'the chrome is built inside it');
  check(h.root.classList.contains('framed'), 'and is tagged as filling a window of its own');
  check(frame.style.height === '503px', "the frame is exactly as tall as the canvas");

  /* The picture's height changes with the page's scaled modes, the browser
   * window and the game's own Display setting. All three end in the canvas's
   * box changing, which is why the observer watches the box. */
  canvas._rectH = 720;
  observers.forEach(function (o) { o.fn(); });
  check(frame.style.height === '720px', 'and follows it when the canvas is rescaled');

  /*
   * And it starts where the PICTURE starts, not where the row does. In the
   * page's scaled modes the canvas is centred and letterboxed inside the
   * stage, so a window that matched only the height hung above the picture it
   * is meant to read as one object with.
   */
  canvas._rectT = 108;
  observers.forEach(function (o) { o.fn(); });
  check(frame.style.marginTop === '108px', 'the frame begins on the canvas top edge');
  observers.forEach(function (o) { o.fn(); });
  check(frame.style.marginTop === '108px', 'and holds there once nothing is moving');

  /* The page pins its own corner chrome clear of the docked window by this. */
  check(page.documentElement.style['--torirs-dock-width'] === '340px',
    'the docked width reaches the page as a CSS variable');

  /* Width still comes from the model, and now lands on the frame -- widening
   * the root inside a 340px iframe would do nothing but clip. */
  h.apply(cmd(CMD.PANEL_OPEN, { p: 0, text: 'Plugins' }));
  h.apply(cmd(CMD.PANEL_RECT, { p: 0, cw: 420, ch: 900 }));
  check(frame.style.width === '444px', "the model's width sizes the frame");
  check(h.root.style.width !== '444px', 'not the root inside it');
  check(page.documentElement.style['--torirs-dock-width'] === '444px',
    'and the page hears about the wider window');

  /* ---- popped out into a tab of its own ------------------------------------ */

  h.apply(cmd(CMD.WIDGET_ADD, { w: 7, v: W.BUTTON, tab: -1, text: 'Save' }));
  var savedRow = h.body.children[0];
  var popBtn = h.root.children[0].children.filter(function (c) {
    return c.classList.contains('popout');
  })[0];
  check(popBtn !== undefined, 'the title bar offers a pop-out');

  popBtn.fire('click');
  var popup = popups[popups.length - 1];
  check(popup !== undefined, 'clicking it opens a tab');
  check(popup.document.body.children[0] === h.root, 'and the window moves into it');
  check(main.children.indexOf(frame) < 0, 'the frame beside the canvas goes away');
  check(page.documentElement.style['--torirs-dock-width'] === '0px',
    'and the page stops reserving room beside the picture');
  check(h.body.children[0] === savedRow, 'the SAME row nodes moved, rather than being rebuilt');

  /* Which matters because a rebuilt page would be an empty one: the seam
   * emits deltas, so nothing would re-announce a widget that had not changed.
   * The listener riding along is the visible half of that. */
  (function () {
    var before = h.intents.length;
    savedRow.children[0].fire('click');
    check(h.intents.length === before + 1, 'and their listeners came with them');
  })();

  popBtn.fire('click');
  check(popup.closed === true, 'putting it back closes the tab');
  var back = main.children.filter(function (c) { return c.tagName === 'IFRAME'; })[0];
  check(back !== undefined, 'and it docks beside the canvas again');
  check(back.contentDocument.body.children[0] === h.root, 'with the same window in it');
  check(back.style.height === '720px', 'matching the canvas it came back to');
  check(back.style.marginTop === '108px', 'and starting where it starts');
  check(page.documentElement.style['--torirs-dock-width'] === '340px',
    'with the page told to keep clear of it again');

  /* ---- the popped-out tab, closed from its own title bar -------------------- */

  /*
   * The presentation went away without the model hearing. Reported as a CLOSE,
   * exactly as the SDL executor reports its window's X: the model is what
   * decides whether the window is up, and the host takes the executor down on
   * the answer.
   */
  popBtn.fire('click');
  var second = popups[popups.length - 1];
  check(second.closed === false, 'popped out again');
  while (h.takeIntent()) { /* drain */ }
  second.closed = true;
  var reported = JSON.parse(h.takeIntent() || '{}');
  check(reported.k === INTENT.CLOSE, 'closing the tab reports a CLOSE');
  check(reported.p === 0, 'naming the panel that was in it');
  check(h.root === null, 'and the window is let go with the document it was in');

  h.close();
  global_.document = document;
})();

if (failures) {
  console.error(checks + ' checks, ' + failures + ' failure(s)');
  process.exit(1);
}
console.log(checks + ' checks, 0 failures');
