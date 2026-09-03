(function (global, factory) {
  'use strict';

  const exported = { createRuntime: factory, protocol: 1 };
  if (typeof module !== 'undefined' && module.exports) module.exports = exported;
  if (!global || !global.document) return;
  const runtime = factory(global, global.document);
  if (runtime) global.ToriRSPluginChrome = runtime;
})(typeof window !== 'undefined' ? window :
  (typeof global !== 'undefined' ? global : this), function createRuntime(global, document) {
  'use strict';

  const PROTOCOL = 1;
  const MAX_MESSAGES = 64;
  const MAX_ENTRIES = 33;
  const MAX_OPTIONS = 4096;
  const MAX_TEXT = 191;
  const Codec = global.ToriRSPluginChromeCodec;
  if (!Codec) return null;

  const CMD = {
    SYNC_BEGIN: 1, SYNC_END: 2,
    PANEL_OPEN: 3, PANEL_CLOSE: 4, PANEL_TITLE: 5, PANEL_RECT: 6, PANEL_TAB: 7,
    WIDGET_ADD: 8, WIDGET_REMOVE: 9, WIDGET_LABEL: 10, WIDGET_TEXT: 11,
    WIDGET_CHECKED: 12, WIDGET_HIDDEN: 13, WIDGET_COLOR: 14,
    WIDGET_SELECTED: 15, WIDGET_FOCUS: 16, WIDGET_OPTIONS: 17,
    WIDGET_OPTION: 18, CHECK_STYLE: 19
  };
  const W = {
    LABEL: 0, CHECKBOX: 1, TEXTINPUT: 2, SEPARATOR: 3, MENUITEM: 4,
    DROPDOWN: 5, MODELVIEW: 6, BUTTON: 7, TABSTRIP: 8, LISTROW: 9,
    COLORPICK: 10, TEXTAREA: 11, CUSTOM: 12, FREE: 13
  };
  const INTENT = {
    ACTIVATE: 1, ACTION: 2, TOGGLE: 3, TEXT: 4, PICK: 5, TAB: 6,
    CLOSE: 7, CUSTOM_ACTIVATE: 8
  };
  const ROW_ACTION = 0x1;
  const ROW_LOCKED = 0x2;

  /* Chrome 39 has no dependable keyed-collection implementation on every API-22 image.
   * Handles are small integers, so a prefixed own-property store is enough. */
  function Store() {
    this.values = {};
    this.count = 0;
  }
  Store.prototype.key = function (key) { return `$${key}`; };
  Store.prototype.get = function (key) { return this.values[this.key(key)]; };
  Store.prototype.set = function (key, value) {
    const name = this.key(key);
    if (!Object.prototype.hasOwnProperty.call(this.values, name)) this.count++;
    this.values[name] = value;
  };
  Store.prototype.drop = function (key) {
    const name = this.key(key);
    if (!Object.prototype.hasOwnProperty.call(this.values, name)) return;
    delete this.values[name];
    this.count--;
  };
  Store.prototype.clear = function () { this.values = {}; this.count = 0; };
  Store.prototype.each = function (fn) {
    for (const name in this.values)
      if (Object.prototype.hasOwnProperty.call(this.values, name)) fn(this.values[name]);
  };

  const shell = document.getElementById('tpc-shell');
  const pane = document.getElementById('tpc-pane');
  const title = document.getElementById('tpc-title');
  const close = document.getElementById('tpc-close');
  const tabs = document.getElementById('tpc-tabs');
  const content = document.getElementById('tpc-content');
  const rail = document.getElementById('tpc-rail-list');
  const status = document.getElementById('tpc-status');
  if (!shell || !pane || !title || !close || !tabs || !content || !rail) return null;
  const legacy = shell.getAttribute && shell.getAttribute('data-tpc-legacy') === '1';
  const legacyPixels = legacy && !global.ToriRSAndroid;
  toggleClass(shell, 'tpc-android-fonts', legacy && !legacyPixels);

  const state = {
    rail: {
      registryRevision: 0, selectionGeneration: 0, pageGeneration: 0,
      activePlugin: -1, lastSelectedPlugin: -1, selectedEntry: -1,
      expanded: false, entries: []
    },
    railNodes: new Store(),
    icons: new Store(),
    themeRevision: 0,
    theme: {},
    pageGeneration: 0,
    panel: -1,
    activeTab: 0,
    checkStyle: 0,
    widgets: new Store(),
    sequence: 0,
    messages: [],
    intents: [],
    lastLayout: '',
    applying: false,
    editorFocused: false,
    focusToken: 0
  };

  /* Packaged Android cannot inject inline code under this bundle's CSP. Its
   * JavascriptInterface is therefore discovered here, in trusted local code. */
  if (typeof global.torirsChromeIntentPosted !== 'function' &&
      global.ToriRSAndroid && typeof global.ToriRSAndroid.intent === 'function') {
    global.torirsChromeIntentPosted = function (copy) {
      global.ToriRSAndroid.intent(
        copy.k, copy.p, copy.w, copy.v, copy.text,
        copy.x, copy.y, copy.g, copy.s);
    };
  }

  function integer(value, fallback) {
    return typeof value === 'number' && isFinite(value) && Math.floor(value) === value
      ? value : fallback;
  }

  function unsigned(value) {
    return Number(value) >>> 0;
  }

  function text(value, maximum = MAX_TEXT) {
    return String(value == null ? '' : value).slice(0, maximum);
  }

  function array(value) {
    return Object.prototype.toString.call(value) === '[object Array]';
  }

  function trimmed(value) {
    return text(value, 4096).replace(/^\s+|\s+$/g, '');
  }

  function bind(node, kind, handler) {
    node[`on${kind}`] = handler;
  }

  function hasClass(node, name) {
    return new RegExp(`(^|\\s)${name}(\\s|$)`).test(node.className || '');
  }

  function toggleClass(node, name, enabled) {
    const current = node.className || '';
    if (enabled && !hasClass(node, name)) node.className = trimmed(`${current} ${name}`);
    else if (!enabled && hasClass(node, name))
      node.className = trimmed(current.replace(new RegExp(`(^|\\s)${name}(?=\\s|$)`, 'g'), ' '));
  }

  function hidden(node, value) {
    node.hidden = !!value;
    node.style.display = value ? 'none' : '';
  }

  function clear(node) {
    while (node.firstChild) node.removeChild(node.firstChild);
  }

  function setText(node, value) {
    if (node) node.innerText = text(value, 4096);
  }

  function nextSequence() {
    state.sequence = (state.sequence + 1) >>> 0;
    if (!state.sequence) state.sequence = 1;
    return state.sequence;
  }

  function safeUrl(value, allowBlob = true) {
    const url = trimmed(value);
    if (!url || /[\u0000-\u001f]/.test(url)) return '';
    if (/^(?:\.\.?\/|[A-Za-z0-9_.-]+\/)/.test(url) && !/\.\./.test(url)) return url;
    if (/^torirs:\/\//i.test(url)) return url;
    if (/^https:\/\/torirs\.local(?:\/|$)/i.test(url)) return url;
    if (!legacyPixels && /^data:image\/(?:png|gif|bmp|webp);base64,[A-Za-z0-9+/=]+$/i.test(url)) return url;
    if (!legacyPixels && allowBlob && /^blob:/i.test(url)) return url;
    return '';
  }

  function cssUrl(url) {
    return url ? `url("${url.replace(/["\\]/g, '\\$&')}")` : 'none';
  }

  function skinButton(button) {
    if (!button || legacyPixels) return;
    const left = safeUrl(state.theme.buttonLeft);
    const middle = safeUrl(state.theme.buttonMiddle);
    const right = safeUrl(state.theme.buttonRight);
    if (left && middle && right) {
      button.style.backgroundImage = `${cssUrl(left)},${cssUrl(right)},${cssUrl(middle)}`;
      button.style.backgroundRepeat = 'no-repeat,no-repeat,repeat-x';
      button.style.backgroundPosition = 'left center,right center,center';
      /* The source art is the 2x (36px-high) bake. Chrome geometry is in the
       * authored 1x units; DPR=2 displays these pixels one-for-one. */
      button.style.backgroundSize = '18px 18px,18px 18px,10px 18px';
    } else if (middle) {
      button.style.backgroundImage = cssUrl(middle);
      button.style.backgroundRepeat = 'repeat-x';
      button.style.backgroundPosition = 'center';
      button.style.backgroundSize = '10px 18px';
    }
  }

  function skinField(control) {
    if (!control || legacyPixels) return;
    const body = safeUrl(state.theme.dropdownBody) || 'skin/DropdownBody.png';
    const isSelect = String(control.tagName || '').toLowerCase() === 'select';
    const arrow = isSelect && (safeUrl(state.theme.scrollDown) || 'skin/ScrollDown.png');
    if (body && arrow) {
      control.style.backgroundImage = `${cssUrl(arrow)},${cssUrl(body)}`;
      control.style.backgroundPosition = 'right 2px center,left top';
      control.style.backgroundRepeat = 'no-repeat,repeat';
      control.style.backgroundSize = '14px 14px,auto';
    } else if (body) {
      control.style.backgroundImage = cssUrl(body);
      control.style.backgroundRepeat = 'repeat';
    }
  }

  function setTabsVisible(visible) {
    hidden(tabs, !visible);
    toggleClass(pane, 'tpc-no-tabs', !visible);
  }

  function sizeLegacyViewport() {
    if (!legacy) return;
    const doc = document.documentElement || {};
    const body = document.body || {};
    const height = doc.clientHeight || body.clientHeight || 0;
    if (height > 0) {
      shell.style.height = `${height}px`;
      pane.style.height = `${height}px`;
      rail.style.height = `${height}px`;
    }
  }

  function normalizeIntent(input) {
    return {
      k: integer(input && input.k, 0),
      p: integer(input && input.p, -1),
      w: integer(input && input.w, -1),
      v: integer(input && input.v, 0),
      text: text(input && input.text),
      x: integer(input && input.x, 0),
      y: integer(input && input.y, 0),
      g: unsigned(input && input.g),
      s: unsigned(input && input.s)
    };
  }

  function queueEnvelope(envelope, widgetIntent) {
    const copy = Codec.parse(Codec.stringify(envelope));
    if (state.messages.length >= MAX_MESSAGES) state.messages.shift();
    state.messages.push(copy);
    if (widgetIntent) {
      const raw = normalizeIntent(widgetIntent);
      if (state.intents.length >= MAX_MESSAGES) state.intents.shift();
      state.intents.push(raw);
      if (typeof global.torirsChromeIntentPosted === 'function') {
        try {
          global.torirsChromeIntentPosted({
            k: raw.k, p: raw.p, w: raw.w, v: raw.v, text: raw.text,
            x: raw.x, y: raw.y, g: raw.g, s: raw.s
          });
        }
        catch (error) { /* Host failure cannot break retained UI. */ }
        return;
      }
    }
    const encoded = Codec.stringify(copy);
    const post = typeof global.torirsPluginChromePostMessage === 'function'
      ? global.torirsPluginChromePostMessage
      : (global.ToriRSAndroid && typeof global.ToriRSAndroid.postMessage === 'function'
        ? value => global.ToriRSAndroid.postMessage(value)
        : null);
    if (post) {
      try { post(encoded); }
      catch (error) { /* Pull queue remains the fallback. */ }
    }
  }

  function postRailSelect(node) {
    const pluginIndex = integer(node && node._tpcPluginIndex, -1);
    const generation = unsigned(node && node._tpcSelectionGeneration);
    if (pluginIndex === -1 || generation === 0) return;
    queueEnvelope({
      protocol: PROTOCOL,
      type: 'rail.select',
      sequence: nextSequence(),
      pluginIndex,
      selectionGeneration: generation
    });
  }

  function postWidget(record, kind, value = 0, newText = '', x = 0, y = 0) {
    if (!record || state.widgets.get(record.handle) !== record ||
        state.pageGeneration !== record.generation || state.panel !== record.panel) return;
    const intent = normalizeIntent({
      k: kind, p: record.panel, w: record.handle, v: value, text: newText,
      x, y, g: record.generation, s: record.serial
    });
    queueEnvelope({
      protocol: PROTOCOL,
      type: 'widget.intent',
      sequence: nextSequence(),
      intent
    }, intent);
  }

  function editable(node) {
    if (!node || !node.tagName) return false;
    const tag = String(node.tagName).toUpperCase();
    return tag === 'INPUT' || tag === 'SELECT' || tag === 'TEXTAREA';
  }

  function postEditorFocus(focused) {
    focused = !!focused;
    if (state.editorFocused === focused) return;
    state.editorFocused = focused;
    queueEnvelope({
      protocol: PROTOCOL,
      type: 'editor.focus',
      sequence: nextSequence(),
      focused,
      pageGeneration: state.pageGeneration
    });
  }

  function decodeRgba(width, height, encoded) {
    const pixels = width * height;
    if (legacyPixels || !global.atob || !global.Uint8ClampedArray || !global.ImageData ||
        width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
        pixels > 4194304) return '';
    try {
      const binary = global.atob(text(encoded, pixels * 6));
      if (binary.length !== pixels * 4) return '';
      const bytes = new global.Uint8ClampedArray(binary.length);
      for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i) & 255;
      const canvas = document.createElement('canvas');
      canvas.width = width;
      canvas.height = height;
      const context = canvas.getContext && canvas.getContext('2d');
      if (!context) return '';
      context.putImageData(new global.ImageData(bytes, width, height), 0, 0);
      return canvas.toDataURL('image/png');
    }
    catch (error) { return ''; }
  }

  function appendImage(parent, url, className, alt) {
    if (!url) return false;
    if (legacyPixels && /\.png(?:[?#]|$)/i.test(url)) {
      const transparent = document.createElement('span');
      transparent.className = className;
      transparent.title = alt || '';
      transparent.style.display = 'block';
      transparent.style.filter = "progid:DXImageTransform.Microsoft.AlphaImageLoader(src='" +
        url.replace(/'/g, '%27') + "',sizingMethod='scale')";
      parent.appendChild(transparent);
      return true;
    }
    const image = document.createElement('img');
    image.className = className;
    image.alt = alt || '';
    image.src = url;
    parent.appendChild(image);
    return true;
  }

  function messageBitmapUrl(message) {
    const direct = safeUrl(message && message.url);
    if (direct) return direct;
    if (message && message.rgbaBase64)
      return decodeRgba(integer(message.width, 0), integer(message.height, 0), message.rgbaBase64);
    return '';
  }

  function fallbackIcon(entry) {
    const themed = safeUrl(state.theme.pluginIcon);
    if (themed) return themed;
    return '';
  }

  function renderRailButton(entry, button) {
    const selected = entry.pluginIndex === state.rail.selectedEntry;
    const expanded = selected && state.rail.expanded;
    const cached = state.icons.get(entry.pluginIndex);
    const iconUrl = cached && cached.url ? cached.url : fallbackIcon(entry);
    button._tpcPluginIndex = entry.pluginIndex;
    button._tpcSelectionGeneration = state.rail.selectionGeneration;
    button.title = entry.title;
    button.setAttribute('aria-label', entry.title +
      (entry.badge ? `, ${entry.badge}` : '') +
      (entry.attention ? ', needs attention' : ''));
    button.setAttribute('aria-selected', selected ? 'true' : 'false');
    button.setAttribute('aria-expanded', expanded ? 'true' : 'false');
    toggleClass(button, 'tpc-attention', entry.attention);
    clear(button);

    if (iconUrl) {
      appendImage(button, iconUrl, 'tpc-rail-icon', '');
    } else {
      const fallback = document.createElement('span');
      fallback.className = 'tpc-rail-fallback';
      setText(fallback, trimmed(entry.title).slice(0, 1).toUpperCase() || 'P');
      button.appendChild(fallback);
    }
    if (entry.badge) {
      const badge = document.createElement('span');
      badge.className = 'tpc-badge';
      setText(badge, entry.badge);
      button.appendChild(badge);
    }
    if (entry.attention) {
      const attention = document.createElement('i');
      attention.className = 'tpc-attention-dot';
      attention.setAttribute('aria-hidden', 'true');
      button.appendChild(attention);
    }
  }

  function rebuildRail() {
    clear(rail);
    state.railNodes.clear();
    for (let index = 0; index < state.rail.entries.length; index++) {
      const entry = state.rail.entries[index];
      const button = document.createElement('button');
      button.type = 'button';
      button.className = 'tpc-rail-entry';
      bind(button, 'click', () => postRailSelect(button));
      state.railNodes.set(entry.pluginIndex, button);
      rail.appendChild(button);
      renderRailButton(entry, button);
    }
  }

  function updateRailNodes() {
    for (let index = 0; index < state.rail.entries.length; index++) {
      const entry = state.rail.entries[index];
      const button = state.railNodes.get(entry.pluginIndex);
      if (button) renderRailButton(entry, button);
    }
  }

  function sanitizeRailEntry(input) {
    const kind = integer(input && input.kind, 0);
    const pluginIndex = integer(input && input.pluginIndex, -1);
    if ((kind !== 1 && kind !== 2) || (kind === 1 ? pluginIndex !== -2 : pluginIndex < 0))
      return null;
    return {
      kind,
      pluginIndex,
      preferredWidth: integer(input.preferredWidth, 320),
      title: text(input.title || (kind === 1 ? 'Manage Plugins' : 'Plugin'), 63),
      iconAsset: text(input.iconAsset, 63),
      badge: text(input.badge, 23),
      attention: !!input.attention
    };
  }

  function applyRailSnapshot(message) {
    const entries = [];
    const source = array(message.entries) ? message.entries : [];
    for (let i = 0; i < source.length && entries.length < MAX_ENTRIES; i++) {
      const entry = sanitizeRailEntry(source[i]);
      if (entry) entries.push(entry);
    }
    const revision = unsigned(message.registryRevision);
    const rebuild = revision !== state.rail.registryRevision ||
      entries.length !== state.rail.entries.length || state.railNodes.count !== entries.length;
    state.rail = {
      registryRevision: revision,
      selectionGeneration: unsigned(message.selectionGeneration),
      pageGeneration: unsigned(message.pageGeneration),
      activePlugin: integer(message.activePlugin, -1),
      lastSelectedPlugin: integer(message.lastSelectedPlugin, -1),
      selectedEntry: integer(message.selectedEntry, -1),
      expanded: !!message.expanded,
      entries
    };
    toggleClass(shell, 'tpc-collapsed', !state.rail.expanded);
    if (rebuild) rebuildRail(); else updateRailNodes();
    if (!state.rail.expanded) clearPage(false);
    reportLayout();
  }

  function applyRailIcon(message) {
    const pluginIndex = integer(message.pluginIndex, -1);
    const revision = unsigned(message.revision);
    const width = integer(message.width, 0);
    const height = integer(message.height, 0);
    if (pluginIndex < 0 || !revision || width < 0 || height < 0 || width > 64 || height > 64)
      return;
    const current = state.icons.get(pluginIndex);
    if (current && current.revision === revision) return;
    const url = width && height ? messageBitmapUrl(message) : '';
    state.icons.set(pluginIndex, { revision, width, height, url });
    let entry = null;
    for (let i = 0; i < state.rail.entries.length; i++)
      if (state.rail.entries[i].pluginIndex === pluginIndex) { entry = state.rail.entries[i]; break; }
    const button = state.railNodes.get(pluginIndex);
    if (entry && button) renderRailButton(entry, button);
  }

  function applyTheme(message) {
    const revision = unsigned(message.revision);
    if (!revision || revision === state.themeRevision) return;
    const assets = message.assets && typeof message.assets === 'object' ? message.assets : {};
    const allowed = [
      'panelBody', 'pluginIcon', 'buttonLeft', 'buttonMiddle', 'buttonRight',
      'checkOn', 'checkOff', 'checkBoxOn', 'checkBoxOff', 'dropdownBody',
      'scrollUp', 'scrollDown', 'scrollTrack', 'scrollGripTop', 'scrollGripMiddle',
      'scrollGripBottom', 'close', 'frameTopLeft', 'frameTop', 'frameTopRight',
      'frameLeft', 'frameRight', 'frameBottomLeft', 'frameBottom', 'frameBottomRight'
    ];
    const next = {};
    for (let i = 0; i < allowed.length; i++) next[allowed[i]] = safeUrl(assets[allowed[i]]);
    state.theme = next;
    state.themeRevision = revision;
    pane.style.backgroundImage = cssUrl(next.panelBody);
    if (rail.parentNode) rail.parentNode.style.backgroundImage = cssUrl(next.panelBody);
    close.style.backgroundImage = cssUrl(next.close);
    const frameIds = ['tl', 't', 'tr', 'l', 'r', 'bl', 'b', 'br'];
    const frameAssets = ['frameTopLeft', 'frameTop', 'frameTopRight', 'frameLeft',
      'frameRight', 'frameBottomLeft', 'frameBottom', 'frameBottomRight'];
    for (let i = 0; i < frameIds.length; i++) {
      const node = document.getElementById(`tpc-frame-${frameIds[i]}`);
      if (node) node.style.backgroundImage = cssUrl(next[frameAssets[i]]);
    }
    updateRailNodes();
    state.widgets.each(renderWidget);
  }

  function clearPage(report = true) {
    postEditorFocus(false);
    state.widgets.clear();
    clear(content);
    clear(tabs);
    setTabsVisible(false);
    hidden(pane, true);
    state.pageGeneration = 0;
    state.panel = -1;
    state.activeTab = 0;
    setText(title, 'Plugins');
    if (report) reportLayout();
  }

  function normalizeCommand(input) {
    return {
      k: integer(input && input.k, 0), p: integer(input && input.p, -1),
      w: integer(input && input.w, -1), tab: integer(input && input.tab, -1),
      v: integer(input && input.v, 0), c: unsigned(input && input.c),
      x: integer(input && input.x, 0), y: integer(input && input.y, 0),
      cw: integer(input && input.cw, 0), ch: integer(input && input.ch, 0),
      label: text(input && input.label, 63), text: text(input && input.text),
      s: unsigned(input && input.s)
    };
  }

  function labelNode(labelValue) {
    const label = document.createElement('span');
    label.className = 'tpc-label';
    setText(label, labelValue);
    return label;
  }

  function attachLabel(row, record, control) {
    if (record.label) row.appendChild(labelNode(record.label));
    row.appendChild(control);
  }

  function createWidget(command) {
    if (command.p !== state.panel || command.w < 0 || command.v < 0 || command.v > W.FREE)
      return;
    removeWidget(command.w);
    const row = document.createElement('div');
    row.className = 'tpc-row';
    const record = {
      handle: command.w, panel: command.p, generation: state.pageGeneration,
      serial: command.s, kind: command.v, tab: command.tab, shape: command.cw,
      rows: command.ch, label: command.label, text: command.text, color: command.c,
      checked: false, selected: -1, hidden: false, focused: false,
      options: [], optionsRevision: 0, customRevision: 0, customScale: 1000,
      row, control: null
    };
    row._tpcRecord = record;

    switch (record.kind) {
      case W.LABEL: {
        const value = document.createElement('span');
        value.className = 'tpc-value';
        row.appendChild(value);
        record.control = value;
        break;
      }
      case W.CHECKBOX: {
        const box = document.createElement('input');
        box.type = 'checkbox';
        box.className = 'tpc-check';
        bind(box, 'change', () => postWidget(record, INTENT.TOGGLE, box.checked ? 1 : 0));
        row.appendChild(box);
        const caption = document.createElement('span');
        row.appendChild(caption);
        record.control = box;
        record.caption = caption;
        break;
      }
      case W.TEXTINPUT: {
        const input = document.createElement('input');
        input.type = 'text';
        input.className = 'tpc-field tpc-text-field';
        input.maxLength = MAX_TEXT;
        bind(input, 'change', () => postWidget(record, INTENT.TEXT, 0, input.value));
        attachLabel(row, record, input);
        record.control = input;
        break;
      }
      case W.TEXTAREA: {
        row.className += ' tpc-tall-row';
        const area = document.createElement('textarea');
        area.className = 'tpc-field tpc-textarea';
        area.maxLength = MAX_TEXT;
        area.rows = record.rows > 0 ? Math.min(record.rows, 32) : 4;
        bind(area, 'change', () => postWidget(record, INTENT.TEXT, 0, area.value));
        attachLabel(row, record, area);
        record.control = area;
        break;
      }
      case W.SEPARATOR: {
        row.className += ' tpc-separator';
        const line = document.createElement('span');
        line.className = 'tpc-separator-line';
        row.appendChild(line);
        break;
      }
      case W.MENUITEM:
      case W.BUTTON: {
        const button = document.createElement('button');
        button.type = 'button';
        button.className = record.kind === W.BUTTON ? 'tpc-button' : 'tpc-menu';
        bind(button, 'click', () => postWidget(record, INTENT.ACTIVATE));
        row.appendChild(button);
        record.control = button;
        break;
      }
      case W.DROPDOWN: {
        const select = document.createElement('select');
        select.className = 'tpc-field tpc-select';
        bind(select, 'change', () => postWidget(record, INTENT.PICK, select.selectedIndex));
        attachLabel(row, record, select);
        record.control = select;
        break;
      }
      case W.MODELVIEW: {
        const model = document.createElement('div');
        model.className = 'tpc-model';
        row.appendChild(model);
        record.control = model;
        break;
      }
      case W.TABSTRIP:
        hidden(row, true);
        record.control = tabs;
        break;
      case W.LISTROW: {
        const holder = document.createElement('div');
        holder.className = 'tpc-list-row';
        const action = document.createElement('button');
        action.type = 'button';
        action.className = 'tpc-row-action';
        bind(action, 'click', () => postWidget(record,
          (record.shape & ROW_ACTION) ? INTENT.ACTION : INTENT.ACTIVATE));
        holder.appendChild(action);
        record.caption = action;
        if (!(record.shape & ROW_LOCKED)) {
          const toggle = document.createElement('input');
          toggle.type = 'checkbox';
          toggle.className = 'tpc-check';
          bind(toggle, 'change', () => postWidget(record, INTENT.TOGGLE, toggle.checked ? 1 : 0));
          holder.appendChild(toggle);
          record.toggle = toggle;
        }
        row.appendChild(holder);
        record.control = holder;
        break;
      }
      case W.COLORPICK: {
        const holder = document.createElement('span');
        holder.className = 'tpc-color';
        const swatch = document.createElement('input');
        swatch.type = legacy ? 'text' : 'color';
        swatch.className = 'tpc-field tpc-color-swatch';
        const hex = document.createElement('input');
        hex.type = 'text';
        hex.className = 'tpc-field tpc-color-text';
        hex.maxLength = 7;
        bind(swatch, 'change', () => {
          hex.value = swatch.value;
          postWidget(record, INTENT.TEXT, 0, swatch.value);
        });
        bind(hex, 'change', () => postWidget(record, INTENT.TEXT, 0, hex.value));
        holder.appendChild(swatch);
        holder.appendChild(hex);
        attachLabel(row, record, holder);
        record.control = holder;
        record.swatch = swatch;
        record.hex = hex;
        break;
      }
      case W.CUSTOM: {
        row.className += ' tpc-tall-row';
        const custom = document.createElement('div');
        custom.className = 'tpc-custom';
        custom.style.height = `${Math.max(48, Math.min(512, record.rows || 120))}px`;
        custom.tabIndex = 0;
        bind(custom, 'mousedown', event => {
          record.pointer = { x: event.clientX, y: event.clientY };
        });
        bind(custom, 'mouseup', event => {
          if (!record.pointer) return;
          record.pointer = null;
          const bounds = custom.getBoundingClientRect();
          const boundsWidth = bounds.width || (bounds.right - bounds.left);
          const boundsHeight = bounds.height || (bounds.bottom - bounds.top);
          if (!boundsWidth || !boundsHeight) return;
          const px = Math.max(0, Math.min(record.bitmapWidth - 1,
            Math.floor((event.clientX - bounds.left) * record.bitmapWidth / boundsWidth)));
          const py = Math.max(0, Math.min(record.bitmapHeight - 1,
            Math.floor((event.clientY - bounds.top) * record.bitmapHeight / boundsHeight)));
          postWidget(record, INTENT.CUSTOM_ACTIVATE, 0, '',
            Math.floor(px * 1000 / Math.max(1, record.customScale)),
            Math.floor(py * 1000 / Math.max(1, record.customScale)));
        });
        bind(custom, 'keydown', event => {
          const code = event.keyCode || event.which || 0;
          if (code === 13 || code === 32) postWidget(record, INTENT.CUSTOM_ACTIVATE);
        });
        if (record.label) row.appendChild(labelNode(record.label));
        row.appendChild(custom);
        record.control = custom;
        break;
      }
      case W.FREE:
      default:
        hidden(row, true);
        break;
    }
    content.appendChild(row);
    state.widgets.set(record.handle, record);
    renderWidget(record);
    reconcileTabsAndVisibility();
  }

  function removeWidget(handle) {
    const record = state.widgets.get(handle);
    if (!record) return;
    if (record.row.parentNode) record.row.parentNode.removeChild(record.row);
    if (record.kind === W.TABSTRIP) setTabsVisible(false);
    state.widgets.drop(handle);
    reconcileTabsAndVisibility();
  }

  function renderOptions(record) {
    if (!record || !record.control) return;
    if (record.kind === W.DROPDOWN) {
      const select = record.control;
      clear(select);
      for (let index = 0; index < record.options.length; index++) {
        const option = document.createElement('option');
        setText(option, record.options[index]);
        select.appendChild(option);
      }
      if (record.selected >= 0 && record.selected < record.options.length)
        select.selectedIndex = record.selected;
    } else if (record.kind === W.TABSTRIP) {
      clear(tabs);
      for (let index = 0; index < record.options.length; index++) {
        const button = document.createElement('button');
        button.type = 'button';
        setText(button, record.options[index]);
        button.setAttribute('aria-selected', index === state.activeTab ? 'true' : 'false');
        bind(button, 'click', () => postWidget(record, INTENT.TAB, index));
        tabs.appendChild(button);
      }
      setTabsVisible(record.options.length !== 0);
    }
  }

  function renderWidget(record) {
    if (!record || state.applying) return;
    switch (record.kind) {
      case W.LABEL:
        setText(record.control, record.text || record.label);
        if (record.color) {
          const raw = (record.color & 0xffffff).toString(16);
          record.control.style.color = `#${'000000'.slice(raw.length)}${raw}`;
        }
        break;
      case W.CHECKBOX:
        record.control.checked = !!record.checked;
        if (!legacyPixels) {
          const square = state.checkStyle === 1;
          const on = safeUrl(square ? state.theme.checkBoxOn : state.theme.checkOn) ||
            (square ? 'skin/CheckBoxOn.png' : 'skin/CheckOn.png');
          const off = safeUrl(square ? state.theme.checkBoxOff : state.theme.checkOff) ||
            (square ? 'skin/CheckBoxOff.png' : 'skin/CheckOff.png');
          const side = square ? 18 : 17;
          record.control.style.width = `${side}px`;
          record.control.style.height = `${side}px`;
          record.control.style.minWidth = `${side}px`;
          record.control.style.backgroundSize = `${side}px ${side}px`;
          record.control.style.backgroundImage = cssUrl(
            record.checked ? on : off);
        }
        setText(record.caption, record.label || record.text);
        break;
      case W.TEXTINPUT:
      case W.TEXTAREA:
        skinField(record.control);
        if (record.control.value !== record.text) record.control.value = record.text;
        break;
      case W.MENUITEM:
      case W.BUTTON:
        skinButton(record.control);
        setText(record.control, record.text || record.label);
        break;
      case W.DROPDOWN:
        skinField(record.control);
        renderOptions(record);
        break;
      case W.TABSTRIP:
        renderOptions(record);
        break;
      case W.MODELVIEW:
        setText(record.control, record.label || record.text || 'Model preview');
        break;
      case W.LISTROW:
        skinButton(record.caption);
        setText(record.caption, record.label || record.text);
        if (record.toggle) record.toggle.checked = !!record.checked;
        break;
      case W.COLORPICK: {
        const value = /^#[0-9a-f]{6}$/i.test(record.text) ? record.text : '#000000';
        record.swatch.value = value;
        record.hex.value = record.text || value;
        break;
      }
      default:
        break;
    }
    hidden(record.row, !!record.hidden || (record.tab >= 0 && record.tab !== state.activeTab) ||
      record.kind === W.TABSTRIP || record.kind === W.FREE);
    if (record.focused && record.control && record.control.focus) {
      try { record.control.focus({ preventScroll: true }); }
      catch (error) { record.control.focus(); }
    }
  }

  function reconcileTabsAndVisibility() {
    if (state.applying) return;
    state.widgets.each(renderWidget);
  }

  function applyCommand(raw) {
    const command = normalizeCommand(raw);
    if (command.k === CMD.CHECK_STYLE) {
      state.checkStyle = command.v;
      toggleClass(shell, 'tpc-check-square', command.v === 1);
      reconcileTabsAndVisibility();
      return;
    }
    if (command.k === CMD.PANEL_OPEN) {
      if (state.panel < 0) state.panel = command.p;
      if (command.p === state.panel && command.text) setText(title, command.text);
      return;
    }
    if (command.p !== state.panel) return;
    const record = state.widgets.get(command.w);
    switch (command.k) {
      case CMD.PANEL_CLOSE:
        clearPage();
        break;
      case CMD.PANEL_TITLE:
        setText(title, command.text);
        break;
      case CMD.PANEL_TAB:
        state.activeTab = command.v;
        reconcileTabsAndVisibility();
        break;
      case CMD.PANEL_RECT:
        break;
      case CMD.WIDGET_ADD:
        createWidget(command);
        break;
      case CMD.WIDGET_REMOVE:
        removeWidget(command.w);
        break;
      case CMD.WIDGET_LABEL:
        if (record) { record.label = command.label; renderWidget(record); }
        break;
      case CMD.WIDGET_TEXT:
        if (record) { record.text = command.text; renderWidget(record); }
        break;
      case CMD.WIDGET_CHECKED:
        if (record) { record.checked = !!command.v; renderWidget(record); }
        break;
      case CMD.WIDGET_HIDDEN:
        if (record) { record.hidden = !!command.v; renderWidget(record); }
        break;
      case CMD.WIDGET_COLOR:
        if (record) { record.color = command.c; renderWidget(record); }
        break;
      case CMD.WIDGET_SELECTED:
        if (record) { record.selected = command.v; renderWidget(record); }
        break;
      case CMD.WIDGET_FOCUS:
        if (record) { record.focused = !!command.v; renderWidget(record); }
        break;
      case CMD.WIDGET_OPTIONS:
        if (record) {
          const count = Math.max(0, Math.min(MAX_OPTIONS, command.v));
          record.options = new Array(count);
          for (let i = 0; i < count; i++) record.options[i] = '';
          record.optionsRevision++;
          renderWidget(record);
        }
        break;
      case CMD.WIDGET_OPTION:
        if (record && command.v >= 0 && command.v < record.options.length) {
          record.options[command.v] = command.text;
          record.optionsRevision++;
          renderWidget(record);
        }
        break;
      default:
        break;
    }
  }

  function applyPageSnapshot(message) {
    const generation = unsigned(message.pageGeneration);
    const panel = integer(message.panel, -1);
    if (!generation || panel < 0) return;
    clearPage(false);
    state.pageGeneration = generation;
    state.panel = panel;
    state.activeTab = 0;
    state.checkStyle = integer(message.checkStyle, 0);
    setText(title, message.title || 'Plugins');
    hidden(pane, false);
    toggleClass(shell, 'tpc-collapsed', false);
    const commands = array(message.commands) ? message.commands : [];
    state.applying = true;
    try { for (let i = 0; i < commands.length; i++) applyCommand(commands[i]); }
    finally { state.applying = false; }
    reconcileTabsAndVisibility();
    reportLayout();
  }

  function applyPageDelta(message) {
    if (!state.pageGeneration || unsigned(message.pageGeneration) !== state.pageGeneration) return;
    const commands = array(message.commands) ? message.commands : [];
    state.applying = true;
    try { for (let i = 0; i < commands.length; i++) applyCommand(commands[i]); }
    finally { state.applying = false; }
    reconcileTabsAndVisibility();
  }

  function applyCustomBitmap(message) {
    const generation = unsigned(message.pageGeneration);
    const panel = integer(message.panel, -1);
    const handle = integer(message.widget, -1);
    const serial = unsigned(message.widgetSerial);
    const revision = unsigned(message.revision);
    const width = integer(message.width, 0);
    const height = integer(message.height, 0);
    const record = state.widgets.get(handle);
    if (!record || record.kind !== W.CUSTOM || generation !== state.pageGeneration ||
        panel !== state.panel || record.panel !== panel || record.serial !== serial ||
        !revision || revision === record.customRevision || width <= 0 || height <= 0 ||
        width > 4096 || height > 4096) return;
    const url = messageBitmapUrl(message);
    if (!url) return;
    clear(record.control);
    if (!appendImage(record.control, url, '', record.label || '')) return;
    record.customRevision = revision;
    record.customScale = Math.max(1, integer(message.scaleMilli, 1000));
    record.bitmapWidth = width;
    record.bitmapHeight = height;
  }

  function reportLayout() {
    sizeLegacyViewport();
    const visible = pane.style.display !== 'none' && !!state.pageGeneration;
    const width = visible ? Math.max(0, pane.clientWidth || 0) : 0;
    const height = visible ? Math.max(0, pane.clientHeight || 0) : 0;
    const scaleMilli = Math.max(1, Math.round(Number(global.devicePixelRatio || 1) * 1000));
    const sizeClass = width < 320 ? 0 : (width >= 480 ? 2 : 1);
    const key = [state.rail.selectionGeneration, state.pageGeneration, width, height,
      scaleMilli, sizeClass, visible ? 1 : 0].join(':');
    if (key === state.lastLayout) return;
    state.lastLayout = key;
    queueEnvelope({
      protocol: PROTOCOL,
      type: 'layout',
      sequence: nextSequence(),
      selectionGeneration: state.rail.selectionGeneration,
      pageGeneration: state.pageGeneration,
      width,
      height,
      scaleMilli,
      sizeClass,
      visible,
      gameVisible: true
    });
  }

  function receive(input) {
    let message = input;
    if (typeof input === 'string') {
      try { message = Codec.parse(input); }
      catch (error) { return false; }
    }
    if (!message || typeof message !== 'object' || integer(message.protocol, 0) !== PROTOCOL)
      return false;
    switch (message.type) {
      case 'rail.snapshot': applyRailSnapshot(message); break;
      case 'rail.icon': applyRailIcon(message); break;
      case 'theme': applyTheme(message); break;
      case 'page.snapshot': applyPageSnapshot(message); break;
      case 'page.delta': applyPageDelta(message); break;
      case 'page.close':
        if (!message.pageGeneration || unsigned(message.pageGeneration) === state.pageGeneration)
          clearPage();
        break;
      case 'custom.bitmap': applyCustomBitmap(message); break;
      default: return false;
    }
    return true;
  }

  function takeMessage() {
    return state.messages.length ? Codec.stringify(state.messages.shift()) : '';
  }

  function takeIntent() {
    return state.intents.length ? Codec.stringify(state.intents.shift()) : '';
  }

  bind(close, 'click', () => {
    if (state.panel < 0 || !state.pageGeneration) return;
    const intent = normalizeIntent({
      k: INTENT.CLOSE, p: state.panel, w: -1,
      g: state.pageGeneration, s: 0
    });
    queueEnvelope({
      protocol: PROTOCOL, type: 'widget.intent', sequence: nextSequence(), intent
    }, intent);
  });

  bind(content, 'focusin', event => {
    event = event || global.event || {};
    if (!editable(event.target || event.srcElement)) return;
    state.focusToken++;
    postEditorFocus(true);
  });
  bind(content, 'focusout', event => {
    event = event || global.event || {};
    if (!editable(event.target || event.srcElement)) return;
    const token = ++state.focusToken;
    const settle = () => {
      if (token !== state.focusToken) return;
      const active = document.activeElement;
      if (editable(active) && content.contains && content.contains(active)) return;
      postEditorFocus(false);
    };
    if (typeof global.setTimeout === 'function') global.setTimeout(settle, 0);
    else settle();
  });

  global.onresize = reportLayout;
  sizeLegacyViewport();

  return {
    protocol: PROTOCOL,
    receive,
    takeMessage,
    takeIntent,
    inspect() {
      return {
        railEntries: state.rail.entries.length,
        selectedEntry: state.rail.selectedEntry,
        expanded: state.rail.expanded,
        pageGeneration: state.pageGeneration,
        panel: state.panel,
        widgetCount: state.widgets.count,
        queuedMessages: state.messages.length
      };
    },
    constants: { CMD, W, INTENT }
  };
});
