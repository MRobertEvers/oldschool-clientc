/*
 * The web chrome executor's page half: chrome commands in, DOM controls out.
 *
 * The wasm client owns the widget MODEL; this file owns the DOM. Commands
 * arrive through window.torirsChromeApply (called from C by EM_JS, see
 * src/ui/torirs_chrome_exec_web.c) and each one creates, updates or destroys a
 * node. What the user does goes back the other way as intents, queued here and
 * pulled one at a time by the client.
 *
 * NOTHING HERE MAKES THE CLIENT WAIT. Every entry point returns immediately;
 * the queue is drained on the client's own frame. That is torirs_channel.js's
 * state rule applied to a second consumer of the same idea -- the renderer must
 * never block on a panel.
 *
 * The look is the same skin the native chrome wears (toridbg_theme_osrs), the
 * same values panel.html already spells in CSS. One extraction, three
 * renderers now.
 */
(function (global) {
  'use strict';

  /* Command kinds — enum ToriRSChromeCmdKind, in order. */
  var CMD = {
    SYNC_BEGIN: 1, SYNC_END: 2,
    PANEL_OPEN: 3, PANEL_CLOSE: 4, PANEL_TITLE: 5, PANEL_RECT: 6, PANEL_TAB: 7,
    WIDGET_ADD: 8, WIDGET_REMOVE: 9, WIDGET_LABEL: 10, WIDGET_TEXT: 11,
    WIDGET_CHECKED: 12, WIDGET_HIDDEN: 13, WIDGET_COLOR: 14, WIDGET_SELECTED: 15,
    WIDGET_OPTIONS: 16, WIDGET_OPTION: 17
  };

  /* Widget kinds — enum ToriDbgWidgetKind. */
  var W = {
    LABEL: 0, CHECKBOX: 1, TEXTINPUT: 2, SEPARATOR: 3, MENUITEM: 4,
    DROPDOWN: 5, MODELVIEW: 6, BUTTON: 7, TABSTRIP: 8, FREE: 9
  };

  /* Intent kinds — enum ToriRSChromeIntentKind. */
  var INTENT = { ACTIVATE: 1, TOGGLE: 2, TEXT: 3, PICK: 4, TAB: 5, CLOSE: 6 };

  var STYLE = [
    '.torirs-chrome{position:fixed;right:16px;top:16px;width:340px;max-height:70vh;',
    'display:flex;flex-direction:column;z-index:9999;',
    'background:#5D5447;border:1px solid #000;color:#FFF;',
    'font:12px/1.45 "Helvetica Neue",Arial,sans-serif;',
    'text-shadow:1px 1px 0 rgba(0,0,0,.75);box-shadow:0 6px 24px rgba(0,0,0,.45)}',
    '.torirs-chrome-title{background:#000;color:#5D5447;font-weight:700;',
    'padding:3px 8px;display:flex;justify-content:space-between;cursor:default}',
    '.torirs-chrome-title button{background:none;border:0;color:#C8C8C8;cursor:pointer;',
    'font:inherit;padding:0 2px}',
    '.torirs-chrome-tabs{display:flex;gap:2px;padding:4px 6px 0;border-bottom:1px solid #000;',
    'flex-wrap:wrap}',
    '.torirs-chrome-tabs button{background:rgba(0,0,0,.35);border:1px solid #000;',
    'border-bottom:0;color:#C8C8C8;cursor:pointer;font:inherit;padding:2px 8px}',
    '.torirs-chrome-tabs button.on{background:#5D5447;color:#FFF}',
    '.torirs-chrome-body{overflow-y:auto;padding:6px 8px 8px}',
    '.torirs-chrome-row{display:flex;align-items:center;gap:6px;margin:3px 0}',
    '.torirs-chrome-row>span.lbl{color:#C8C8C8;flex:0 0 96px}',
    '.torirs-chrome-row input[type=text],.torirs-chrome-row select{flex:1;min-width:0;',
    'background:#000;border:1px solid #3E3529;color:#FFF;font:inherit;padding:2px 4px}',
    '.torirs-chrome-row input[type=text]:focus,.torirs-chrome-row select:focus{',
    'outline:0;border-color:#FF0}',
    '.torirs-chrome-row button.act{background:#000;border:1px solid #3E3529;color:#FFF;',
    'cursor:pointer;font:inherit;padding:2px 10px}',
    '.torirs-chrome-row button.act:hover{border-color:#FF0;color:#FF0}',
    '.torirs-chrome-sep{border-top:1px solid #000;margin:6px 0}',
    '.torirs-chrome-row.hidden{display:none}'
  ].join('');

  function ChromeHost() {
    this.root = null;
    this.body = null;
    this.tabsEl = null;
    this.titleEl = null;
    /* Panels and widgets by chrome HANDLE, matching the C mirror: a command
     * names a handle, so the lookup should be a property access rather than a
     * search. */
    this.panels = {};
    this.widgets = {};
    this.intents = [];
    /* The panel whose tabs the strip is showing. There is one window, so this
     * is which panel owns it -- not a list. */
    this.tabPanel = -1;
  }

  ChromeHost.prototype.open = function () {
    if (this.root) return true;
    if (!global.document) return false;

    if (!document.getElementById('torirs-chrome-style')) {
      var style = document.createElement('style');
      style.id = 'torirs-chrome-style';
      style.textContent = STYLE;
      document.head.appendChild(style);
    }

    this.root = document.createElement('div');
    this.root.className = 'torirs-chrome';

    this.titleEl = document.createElement('div');
    this.titleEl.className = 'torirs-chrome-title';
    var name = document.createElement('span');
    name.textContent = 'Plugins';
    var close = document.createElement('button');
    close.type = 'button';
    close.textContent = '✕';
    close.title = 'Close';
    var self = this;
    close.addEventListener('click', function () {
      /* Reported, not acted on: the MODEL decides whether the window is up,
       * and it hides the panel when it receives this. Closing the DOM here as
       * well would take the window down twice and leave the model thinking it
       * is still open. */
      self.push({ k: INTENT.CLOSE, p: self.tabPanel, w: -1, v: 0, text: '' });
    });
    this.titleEl.appendChild(name);
    this.titleEl.appendChild(close);

    this.tabsEl = document.createElement('div');
    this.tabsEl.className = 'torirs-chrome-tabs';

    this.body = document.createElement('div');
    this.body.className = 'torirs-chrome-body';

    this.root.appendChild(this.titleEl);
    this.root.appendChild(this.tabsEl);
    this.root.appendChild(this.body);
    document.body.appendChild(this.root);
    return true;
  };

  ChromeHost.prototype.close = function () {
    if (this.root && this.root.parentNode) this.root.parentNode.removeChild(this.root);
    this.root = null;
    this.body = null;
    this.tabsEl = null;
    this.titleEl = null;
    this.panels = {};
    this.widgets = {};
    this.tabPanel = -1;
  };

  ChromeHost.prototype.push = function (intent) {
    /* Bounded: a page left open behind a client that stopped draining must not
     * grow a queue forever. The oldest go first, because the newest are what
     * the user last did and therefore what they are waiting to see. */
    if (this.intents.length >= 64) this.intents.shift();
    this.intents.push(intent);
  };

  ChromeHost.prototype.takeIntent = function () {
    if (!this.intents.length) return '';
    return JSON.stringify(this.intents.shift());
  };

  /* Which rows are on screen: not hidden, and on the active tab. The one place
   * the two tests are combined, matching ToriRSChromeMirror_Shown. */
  ChromeHost.prototype.reflow = function () {
    var panel = this.panels[this.tabPanel];
    var active = panel ? panel.activeTab : 0;
    for (var handle in this.widgets) {
      if (!Object.prototype.hasOwnProperty.call(this.widgets, handle)) continue;
      var w = this.widgets[handle];
      var shown = !w.hidden && (w.tab < 0 || w.tab === active);
      w.row.classList.toggle('hidden', !shown);
    }
  };

  ChromeHost.prototype.renderTabs = function () {
    var panel = this.panels[this.tabPanel];
    var self = this;
    this.tabsEl.textContent = '';
    if (!panel || !panel.tabs || panel.tabs.length < 2) return;
    panel.tabs.forEach(function (title, index) {
      var b = document.createElement('button');
      b.type = 'button';
      b.textContent = title;
      if (index === panel.activeTab) b.className = 'on';
      b.addEventListener('click', function () {
        self.push({ k: INTENT.TAB, p: self.tabPanel, w: panel.strip, v: index, text: '' });
      });
      self.tabsEl.appendChild(b);
    });
  };

  ChromeHost.prototype.makeWidget = function (cmd) {
    var self = this;
    var row = document.createElement('div');
    row.className = 'torirs-chrome-row';

    var entry = {
      row: row, kind: cmd.v, panel: cmd.p, tab: cmd.tab,
      hidden: false, control: null, options: []
    };

    function labelled(control) {
      if (cmd.label) {
        var lbl = document.createElement('span');
        lbl.className = 'lbl';
        lbl.textContent = cmd.label;
        row.appendChild(lbl);
      }
      row.appendChild(control);
    }

    switch (cmd.v) {
      case W.CHECKBOX: {
        var box = document.createElement('input');
        box.type = 'checkbox';
        var text = document.createElement('span');
        text.textContent = cmd.label || '';
        box.addEventListener('change', function () {
          self.push({ k: INTENT.TOGGLE, p: cmd.p, w: cmd.w, v: box.checked ? 1 : 0, text: '' });
        });
        row.appendChild(box);
        row.appendChild(text);
        entry.control = box;
        entry.labelNode = text;
        break;
      }
      case W.TEXTINPUT: {
        var input = document.createElement('input');
        input.type = 'text';
        /* On change, not on input: an intent per keystroke would send the
         * model a value for every half-typed state, and the chrome's own
         * in-canvas input commits the same way. */
        input.addEventListener('change', function () {
          self.push({ k: INTENT.TEXT, p: cmd.p, w: cmd.w, v: 0, text: input.value });
        });
        labelled(input);
        entry.control = input;
        break;
      }
      case W.DROPDOWN: {
        var select = document.createElement('select');
        select.addEventListener('change', function () {
          self.push({
            k: INTENT.PICK, p: cmd.p, w: cmd.w,
            v: select.selectedIndex, text: select.value
          });
        });
        labelled(select);
        entry.control = select;
        break;
      }
      case W.BUTTON:
      case W.MENUITEM: {
        var button = document.createElement('button');
        button.type = 'button';
        button.className = 'act';
        button.textContent = cmd.text || cmd.label || '';
        button.addEventListener('click', function () {
          self.push({ k: INTENT.ACTIVATE, p: cmd.p, w: cmd.w, v: 0, text: '' });
        });
        row.appendChild(button);
        entry.control = button;
        break;
      }
      case W.SEPARATOR: {
        row.className = 'torirs-chrome-sep';
        break;
      }
      case W.TABSTRIP: {
        /* The strip is chrome, not a row: it lives in the header, and its
         * titles arrive as this widget's OPTIONS. The row stays empty and
         * hidden so the handle still has a node to be removed by. */
        row.classList.add('hidden');
        var panel = this.panels[cmd.p];
        if (panel) {
          panel.strip = cmd.w;
          this.tabPanel = cmd.p;
        }
        break;
      }
      default: {
        var span = document.createElement('span');
        span.textContent = cmd.text || cmd.label || '';
        row.appendChild(span);
        entry.control = span;
        break;
      }
    }

    this.body.appendChild(row);
    this.widgets[cmd.w] = entry;
  };

  ChromeHost.prototype.apply = function (cmd) {
    if (!this.root) return;
    var w = this.widgets[cmd.w];
    var panel;

    switch (cmd.k) {
      case CMD.PANEL_OPEN:
        this.panels[cmd.p] = { tabs: [], activeTab: 0, strip: -1 };
        if (cmd.text && this.titleEl) this.titleEl.firstChild.textContent = cmd.text;
        break;

      case CMD.PANEL_CLOSE:
        delete this.panels[cmd.p];
        /* Its rows go with it: the seam says so once and means all of them,
         * so a DOM node left behind would be a control with no model. */
        for (var handle in this.widgets) {
          if (!Object.prototype.hasOwnProperty.call(this.widgets, handle)) continue;
          if (this.widgets[handle].panel !== cmd.p) continue;
          var node = this.widgets[handle].row;
          if (node.parentNode) node.parentNode.removeChild(node);
          delete this.widgets[handle];
        }
        if (this.tabPanel === cmd.p) { this.tabPanel = -1; this.renderTabs(); }
        break;

      case CMD.PANEL_TITLE:
        if (this.titleEl) this.titleEl.firstChild.textContent = cmd.text;
        break;

      case CMD.PANEL_TAB:
        panel = this.panels[cmd.p];
        if (panel) { panel.activeTab = cmd.v; this.renderTabs(); this.reflow(); }
        break;

      case CMD.WIDGET_ADD:
        this.makeWidget(cmd);
        this.reflow();
        break;

      case CMD.WIDGET_REMOVE:
        if (w) {
          if (w.row.parentNode) w.row.parentNode.removeChild(w.row);
          delete this.widgets[cmd.w];
        }
        break;

      case CMD.WIDGET_LABEL:
        if (w && w.labelNode) w.labelNode.textContent = cmd.label;
        else if (w) {
          var lbl = w.row.querySelector('span.lbl');
          if (lbl) lbl.textContent = cmd.label;
        }
        break;

      case CMD.WIDGET_TEXT:
        if (!w) break;
        if (w.kind === W.TEXTINPUT) {
          /* Never while it has focus: the model is echoing a value the user is
           * still editing, and writing it back would move the caret and undo
           * whatever they typed since the last commit. */
          if (document.activeElement !== w.control) w.control.value = cmd.text;
        } else if (w.control) {
          w.control.textContent = cmd.text;
        }
        break;

      case CMD.WIDGET_CHECKED:
        if (w && w.kind === W.CHECKBOX) w.control.checked = !!cmd.v;
        break;

      case CMD.WIDGET_HIDDEN:
        if (w) { w.hidden = !!cmd.v; this.reflow(); }
        break;

      case CMD.WIDGET_OPTIONS:
        if (!w) break;
        w.options = [];
        w.optionsWanted = cmd.v;
        if (w.kind === W.DROPDOWN && w.control) w.control.textContent = '';
        break;

      case CMD.WIDGET_OPTION:
        if (!w) break;
        w.options[cmd.v] = cmd.text;
        if (w.kind === W.TABSTRIP) {
          panel = this.panels[w.panel];
          if (panel) { panel.tabs[cmd.v] = cmd.text; this.renderTabs(); }
        } else if (w.kind === W.DROPDOWN && w.control) {
          var option = document.createElement('option');
          option.textContent = cmd.text;
          w.control.appendChild(option);
        }
        break;

      case CMD.WIDGET_SELECTED:
        if (!w) break;
        if (w.kind === W.DROPDOWN && w.control) w.control.selectedIndex = cmd.v;
        break;

      default:
        break;
    }
  };

  var host = new ChromeHost();

  /* The hooks C looks for. Their PRESENCE is the availability test -- the
   * client asks before it binds, so a cached page without them degrades to
   * in-canvas chrome instead of a window that silently does nothing. */
  global.torirsChromeOpen = function () { return host.open(); };
  global.torirsChromeClose = function () { host.close(); };
  global.torirsChromeApply = function (cmd) { host.apply(cmd); };
  global.torirsChromeTakeIntent = function () { return host.takeIntent(); };

  /* Exported for the node tests, which drive the same host against a fake
   * document -- no browser, no wasm, matching web/test/channel_*.js. */
  if (typeof module !== 'undefined' && module.exports)
    module.exports = { ChromeHost: ChromeHost, CMD: CMD, W: W, INTENT: INTENT };
})(typeof window !== 'undefined' ? window : globalThis);
