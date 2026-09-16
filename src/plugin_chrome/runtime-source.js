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
  /* Must equal TORIRS_CHROME_OPTION_MAX: the model and both browser runtimes
   * accept the same bounded list rather than truncating at presentation. */
  const MAX_OPTIONS = 512;
  const MAX_PAGE_COMMANDS = 8190; /* 8192 including SYNC_BEGIN/SYNC_END. */
  const MAX_TEXT = 191;
  const Codec = global.ToriRSPluginChromeCodec;
  if (!Codec) return null;

  const CMD = {
    SYNC_BEGIN: 1, SYNC_END: 2,
    PANEL_OPEN: 3, PANEL_CLOSE: 4, PANEL_TITLE: 5, PANEL_RECT: 6, PANEL_TAB: 7,
    WIDGET_ADD: 8, WIDGET_REMOVE: 9, WIDGET_LABEL: 10, WIDGET_TEXT: 11,
    WIDGET_CHECKED: 12, WIDGET_HIDDEN: 13, WIDGET_COLOR: 14,
    WIDGET_SELECTED: 15, WIDGET_FOCUS: 16, WIDGET_OPTIONS: 17,
    WIDGET_OPTION: 18, CHECK_STYLE: 19, WIDGET_HEIGHT: 20
  };
  const W = {
    LABEL: 0, CHECKBOX: 1, TEXTINPUT: 2, SEPARATOR: 3, MENUITEM: 4,
    DROPDOWN: 5, MODELVIEW: 6, BUTTON: 7, TABSTRIP: 8, LISTROW: 9,
    COLORPICK: 10, TEXTAREA: 11, CUSTOM: 12, FREE: 13
  };
  const INTENT = {
    ACTIVATE: 1, ACTION: 2, TOGGLE: 3, TEXT: 4, PICK: 5, TAB: 6,
    CLOSE: 7, CUSTOM_ACTIVATE: 8, CUSTOM_MENU: 9
  };
  const ROW_ACTION = 0x1;
  const ROW_LOCKED = 0x2;
  const LABEL = {
    PLAIN: 0, HEADING: 1, PARAGRAPH: 2, KEY_VALUE: 3, PROGRESS: 4, ERROR: 5,
    STYLE_COUNT: 6
  };
  const RENDER = {
    FULL: 1, VISIBILITY: 2, OPTIONS: 4, SELECTION: 8, FOCUS: 16, CONTENT: 32
  };

  /* The XP runtime has no dependable keyed-collection implementation.
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
  /* The rail's own framed box, which the list lives inside. Only the legacy
   * page hands the two an explicit height; on the modern page the grid does. */
  const railBox = document.getElementById('tpc-rail') || (rail && rail.parentNode);
  const status = document.getElementById('tpc-status');
  if (!shell || !pane || !title || !close || !tabs || !content || !rail) return null;
  const legacy = shell.getAttribute && shell.getAttribute('data-tpc-legacy') === '1';

  const state = {
    rail: {
      registryRevision: 0, selectionGeneration: 0, pageGeneration: 0,
      activePlugin: -1, lastSelectedPlugin: -1, selectedEntry: -1,
      expanded: false, railHidden: false, entries: []
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
    widgetsByTab: new Store(),
    checkWidgets: new Store(),
    tabStrips: new Store(),
    customWidgets: new Store(),
    renderQueue: [],
    renderVisits: 0,
    sequence: 0,
    messages: [],
    intents: [],
    messageOverflow: false,
    intentOverflow: false,
    lastLayout: '',
    applying: false,
    editorFocused: false,
    focusToken: 0
  };

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
    if (!legacy && /^data:image\/(?:png|gif|bmp|webp);base64,[A-Za-z0-9+/=]+$/i.test(url)) return url;
    if (!legacy && allowBlob && /^blob:/i.test(url)) return url;
    return '';
  }

  function cssUrl(url) {
    return url ? `url("${url.replace(/["\\]/g, '\\$&')}")` : 'none';
  }

  function skinButton(button) {
    if (!button || legacy) return;
    const left = safeUrl(state.theme.buttonLeft);
    const middle = safeUrl(state.theme.buttonMiddle);
    const right = safeUrl(state.theme.buttonRight);
    if (left && middle && right) {
      button.style.backgroundImage = `${cssUrl(left)},${cssUrl(right)},${cssUrl(middle)}`;
      button.style.backgroundRepeat = 'no-repeat,no-repeat,repeat-x';
      button.style.backgroundPosition = 'left center,right center,left center';
      /* The source art is the 2x (36px-high) bake. Chrome geometry is in the
       * authored 1x units; DPR=2 displays these pixels one-for-one. */
      button.style.backgroundSize = '18px 18px,18px 18px,10px 18px';
      /* The middle stops UNDER the caps. A cap's art carries a transparent
       * bleed margin and two notched corners, so a middle tiled across the
       * whole width shows through both -- the tile running past the ends of
       * the end caps. The inset is the caps' opaque width (16), not their 18px
       * cell, so the middle tucks under them rather than stopping short and
       * leaving a seam. Set here as well as in the stylesheet because the
       * downlevel page styles only the middle: its caps arrive from this
       * theme, and so must their inset -- which is why this has to state the
       * same geometry modern.css does, down to the box model, or the two pages
       * draw the same theme two different sizes.
       *
       * The inset is a transparent BORDER, and the vertical padding it frees
       * is what centres the caption on the plate: a button centres its LINE
       * BOX, and p12's box is not centred on its own ink (a 12px ascent for
       * accents nothing writes, over a 10px capital). Two pixels of top
       * padding leave the 18px box exactly the 16px line box, so the baseline
       * is the padding edge plus the ascent -- 14, the middle of the art's
       * opaque 1..17 band -- with no free space to halve onto a blurred row.
       * See the long note on the .tpc-button rule in modern.css. */
      button.style.boxSizing = 'border-box';
      button.style.backgroundOrigin = 'border-box,border-box,padding-box';
      button.style.backgroundClip = 'border-box,border-box,padding-box';
      button.style.borderStyle = 'solid';
      button.style.borderColor = 'transparent';
      button.style.borderTopWidth = '0';
      button.style.borderBottomWidth = '0';
      button.style.borderLeftWidth = '16px';
      button.style.borderRightWidth = '16px';
      button.style.paddingLeft = '0';
      button.style.paddingRight = '0';
      button.style.paddingTop = '2px';
      button.style.lineHeight = '16px';
      button.style.overflow = 'hidden';
    } else if (middle) {
      button.style.backgroundImage = cssUrl(middle);
      button.style.backgroundRepeat = 'repeat-x';
      button.style.backgroundPosition = 'left center';
      button.style.backgroundSize = '10px 18px';
      /* No caps: the tile is the whole button and needs no inset, so the
       * border the cap case carries has to come back off -- a theme can turn
       * its caps off, and a stale 16px border would keep their space. The
       * caption's own 2px nudge stays: the plate is the same 18px art. */
      button.style.boxSizing = 'border-box';
      button.style.backgroundClip = 'border-box';
      button.style.borderLeftWidth = '0';
      button.style.borderRightWidth = '0';
      button.style.paddingLeft = '6px';
      button.style.paddingRight = '6px';
      button.style.paddingTop = '2px';
      button.style.lineHeight = '16px';
    }
  }

  /* The interfaces' own tick/cross (or the square well) at its baked side,
   * shared by a checkbox row and a roster row's switch. */
  function skinCheck(control, checked) {
    if (!control) return;
    control.checked = checked;
    if (legacy) return;
    const square = state.checkStyle === 1;
    const on = safeUrl(square ? state.theme.checkBoxOn : state.theme.checkOn) ||
      (square ? 'skin/CheckBoxOn.png' : 'skin/CheckOn.png');
    const off = safeUrl(square ? state.theme.checkBoxOff : state.theme.checkOff) ||
      (square ? 'skin/CheckBoxOff.png' : 'skin/CheckOff.png');
    const side = square ? 18 : 17;
    control.style.width = `${side}px`;
    control.style.height = `${side}px`;
    control.style.minWidth = `${side}px`;
    control.style.backgroundSize = `${side}px ${side}px`;
    control.style.backgroundImage = cssUrl(checked ? on : off);
  }

  function skinField(control) {
    if (!control || legacy) return;
    const body = safeUrl(state.theme.dropdownBody) || 'skin/DropdownBody.png';
    control.style.backgroundImage = cssUrl(body);
    control.style.backgroundRepeat = 'repeat';
  }

  /* The closed dropdown, as dbg_push_dropdown_button draws it: the PANEL tile
   * (not the lighter dropdown parchment, which is the open list's), and the
   * scrollbar's own down arrow -- its up arrow while the list is open. */
  function skinDropdown(record) {
    if (!record.control || legacy) return;
    const body = safeUrl(state.theme.panelBody) || 'skin/PanelBody.png';
    const open = dropdown.record === record;
    const arrow = open
      ? safeUrl(state.theme.scrollUp) || 'skin/ScrollUp.png'
      : safeUrl(state.theme.scrollDown) || 'skin/ScrollDown.png';
    record.control.style.backgroundImage = cssUrl(body);
    record.arrow.style.backgroundImage = cssUrl(arrow);
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
      /* The BOX gets the viewport height, and the list fills what is left
       * inside its frame. Sizing the list itself to the viewport hangs its
       * last entries under the bottom frame piece, where they are clipped. */
      if (railBox && railBox.style) railBox.style.height = `${height}px`;
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
    if (widgetIntent) {
      const raw = normalizeIntent(widgetIntent);
      if (typeof global.torirsChromeIntentPosted === 'function') {
        try {
          const accepted = global.torirsChromeIntentPosted({
            k: raw.k, p: raw.p, w: raw.w, v: raw.v, text: raw.text,
            x: raw.x, y: raw.y, g: raw.g, s: raw.s
          });
          if (accepted !== false) return true;
        }
        catch (error) { /* Host failure cannot break retained UI. */ }
      }
    }
    const encoded = Codec.stringify(copy);
    const post = typeof global.torirsPluginChromePostMessage === 'function'
      ? global.torirsPluginChromePostMessage : null;
    if (post) {
      try { if (post(encoded) === true) return true; }
      catch (error) { /* Pull queue remains the fallback. */ }
    }
    if (state.messages.length >= MAX_MESSAGES) {
      state.messageOverflow = true;
      if (widgetIntent) state.intentOverflow = true;
      return false;
    }
    state.messages.push(copy);
    if (widgetIntent) {
      const raw = normalizeIntent(widgetIntent);
      if (state.intents.length >= MAX_MESSAGES) {
        state.intentOverflow = true;
        state.messages.pop();
        return false;
      }
      state.intents.push(raw);
    }
    return true;
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
    return tag === 'INPUT' || tag === 'TEXTAREA';
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
    if (legacy || !global.atob || !global.Uint8ClampedArray || !global.ImageData ||
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
    if (legacy && /\.png(?:[?#]|$)/i.test(url)) {
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
    /* Old MSHTML has no attribute selectors, so the selected stone also carries
     * a className the legacy stylesheet can reach. */
    toggleClass(button, 'tpc-rail-entry-selected', selected);
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
      /* The game lane's own pop-out column carries the plugin destinations,
       * so this page draws no rail and reserves no width for one. Selection
       * still arrives in this snapshot and the page pane still opens and
       * closes on it; only the rail box goes. The entries are retained so the
       * rail comes back intact when the lane stops carrying them. */
      railHidden: !!message.railHidden,
      entries
    };
    toggleClass(shell, 'tpc-collapsed', !state.rail.expanded);
    toggleClass(shell, 'tpc-rail-hidden', state.rail.railHidden);
    if (railBox) hidden(railBox, state.rail.railHidden);
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
    /* Two frames wear the same nine pieces: the page's panel, and the rail --
     * which is the gameframe popout strip's own 42px strip and has to read as
     * one of them, not as a flat column beside one. */
    const framePrefixes = ['tpc-frame-', 'tpc-railframe-'];
    for (let p = 0; p < framePrefixes.length; p++) {
      for (let i = 0; i < frameIds.length; i++) {
        const node = document.getElementById(framePrefixes[p] + frameIds[i]);
        if (node) node.style.backgroundImage = cssUrl(next[frameAssets[i]]);
      }
    }
    updateRailNodes();
    state.widgets.each(record => queueWidgetRender(record, RENDER.CONTENT));
  }

  function indexWidget(record) {
    if (record.tab >= 0) {
      let bucket = state.widgetsByTab.get(record.tab);
      if (!bucket) {
        bucket = new Store();
        state.widgetsByTab.set(record.tab, bucket);
      }
      bucket.set(record.handle, record);
    }
    if (record.kind === W.CHECKBOX || record.kind === W.LISTROW)
      state.checkWidgets.set(record.handle, record);
    if (record.kind === W.TABSTRIP) state.tabStrips.set(record.handle, record);
    if (record.kind === W.CUSTOM) state.customWidgets.set(record.handle, record);
  }

  function unindexWidget(record) {
    if (record.tab >= 0) {
      const bucket = state.widgetsByTab.get(record.tab);
      if (bucket) {
        bucket.drop(record.handle);
        if (!bucket.count) state.widgetsByTab.drop(record.tab);
      }
    }
    state.checkWidgets.drop(record.handle);
    state.tabStrips.drop(record.handle);
    state.customWidgets.drop(record.handle);
  }

  /* The DOM half of retained execution. A command already names the exact
   * widget/property, so applying a delta queues that record directly. The
   * per-record bit and flags coalesce an OPTIONS header plus all of its items
   * into one render, without an end-of-transaction walk over every widget. */
  function queueWidgetRender(record, flags) {
    if (!record || state.widgets.get(record.handle) !== record) return;
    record.renderFlags |= flags;
    if (!record.renderQueued) {
      record.renderQueued = true;
      state.renderQueue.push(record);
    }
    if (!state.applying) flushWidgetRenders();
  }

  function queueTabVisibility(tab) {
    const bucket = state.widgetsByTab.get(tab);
    if (bucket) bucket.each(record => queueWidgetRender(record, RENDER.VISIBILITY));
  }

  function flushWidgetRenders() {
    const queued = state.renderQueue;
    state.renderQueue = [];
    for (let i = 0; i < queued.length; i++) {
      const record = queued[i];
      const flags = record.renderFlags;
      record.renderFlags = 0;
      record.renderQueued = false;
      if (state.widgets.get(record.handle) === record)
        renderWidgetParts(record, flags);
    }
  }

  function clearPage(report = true) {
    /* Every row the popup holds addresses a widget of the page being torn
     * down. postWidget would drop them, but a menu left standing over a
     * cleared pane is a menu about nothing. */
    popupHide();
    dropdownHide();
    postEditorFocus(false);
    state.widgets.clear();
    state.widgetsByTab.clear();
    state.checkWidgets.clear();
    state.tabStrips.clear();
    state.customWidgets.clear();
    state.renderQueue = [];
    clear(content);
    /* A new semantic page starts at its beginning. The DOM keeps scrollTop
     * when its children are replaced, so without this a long tracker/settings
     * page can leave the next page's Back row clipped above the viewport. */
    content.scrollTop = 0;
    content.scrollLeft = 0;
    clear(tabs);
    tabs.scrollLeft = 0;
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
      detail: text(input && input.detail),
      s: unsigned(input && input.s),
      /* WIDGET_ADD's insertion anchor: the handle the new row goes before.
       * -1 -- and an absent field, which is every stream authored before this
       * one -- means append. */
      b: integer(input && input.b, -1)
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

  /** Resolve a pointer only inside a custom well's drawable content box. */
  function customContentPoint(custom, event) {
    const bounds = custom.getBoundingClientRect();
    const width = custom.clientWidth || 0;
    const height = custom.clientHeight || 0;
    const left = bounds.left + Math.max(0, integer(custom.clientLeft, 0));
    const top = bounds.top + Math.max(0, integer(custom.clientTop, 0));
    const x = event.clientX - left;
    const y = event.clientY - top;
    if (!width || !height || x < 0 || y < 0 || x >= width || y >= height) return null;
    return { x, y, width, height };
  }

  // A well's point in the LOGICAL units the plugin painted in.
  //
  // One place, because the primary and secondary click must map a point the
  // same way or a band would be picked by one button and missed by the other:
  // through the published bitmap first (which is what the host fences the
  // coordinates against) and then back out of the well's own scale.
  function customLocalX(record, point) {
    const px = Math.max(0, Math.min(record.bitmapWidth - 1,
      Math.floor(point.x * record.bitmapWidth / point.width)));
    return Math.floor(px * 1000 / Math.max(1, record.customScale));
  }

  function customLocalY(record, point) {
    const py = Math.max(0, Math.min(record.bitmapHeight - 1,
      Math.floor(point.y * record.bitmapHeight / point.height)));
    return Math.floor(py * 1000 / Math.max(1, record.customScale));
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
      checked: false, selected: -1, shown: -1, hidden: false, focused: false,
      options: [], structuredOptions: false, optionsRevision: 0,
      customRevision: 0, customScale: 1000,
      renderFlags: 0, renderQueued: false,
      row, control: null
    };
    row._tpcRecord = record;

    switch (record.kind) {
      case W.LABEL: {
        if (record.shape === LABEL.HEADING) {
          row.className += ' tpc-section-row';
          const heading = document.createElement('h2');
          heading.className = 'tpc-section-heading';
          row.appendChild(heading);
          record.control = heading;
        } else if (record.shape === LABEL.PARAGRAPH) {
          row.className += ' tpc-paragraph-row';
          const paragraph = document.createElement('p');
          paragraph.className = 'tpc-paragraph';
          row.appendChild(paragraph);
          record.control = paragraph;
        } else if (record.shape === LABEL.KEY_VALUE) {
          row.className += ' tpc-key-value-row';
          const pair = document.createElement('dl');
          pair.className = 'tpc-key-value';
          const key = document.createElement('dt');
          key.className = 'tpc-key';
          const value = document.createElement('dd');
          value.className = 'tpc-value';
          pair.appendChild(key);
          pair.appendChild(value);
          row.appendChild(pair);
          record.control = pair;
          record.caption = key;
          record.valueNode = value;
        } else if (record.shape === LABEL.PROGRESS) {
          row.className += ' tpc-progress-row';
          const group = document.createElement('div');
          group.className = 'tpc-progress';
          const header = document.createElement('div');
          header.className = 'tpc-progress-header';
          const caption = document.createElement('span');
          caption.className = 'tpc-progress-label';
          const detail = document.createElement('span');
          detail.className = 'tpc-progress-detail';
          header.appendChild(caption);
          header.appendChild(detail);
          let meter;
          if (legacy) {
            meter = document.createElement('div');
            meter.className = 'tpc-progress-meter tpc-progress-meter-legacy';
            meter.setAttribute('role', 'progressbar');
            const fill = document.createElement('span');
            fill.className = 'tpc-progress-fill';
            meter.appendChild(fill);
            record.progressFill = fill;
          } else {
            meter = document.createElement('progress');
            meter.className = 'tpc-progress-meter';
            meter.max = 100;
          }
          group.appendChild(header);
          group.appendChild(meter);
          row.appendChild(group);
          record.control = meter;
          record.caption = caption;
          record.valueNode = detail;
        } else if (record.shape === LABEL.ERROR) {
          row.className += ' tpc-error-row';
          const error = document.createElement('p');
          error.className = 'tpc-error';
          error.setAttribute('role', 'alert');
          const caption = document.createElement('strong');
          caption.className = 'tpc-error-label';
          const detail = document.createElement('span');
          detail.className = 'tpc-error-detail';
          error.appendChild(caption);
          error.appendChild(detail);
          row.appendChild(error);
          record.control = error;
          record.caption = caption;
          record.valueNode = detail;
        } else {
          const value = document.createElement('span');
          value.className = 'tpc-value';
          row.appendChild(value);
          record.control = value;
        }
        break;
      }
      case W.CHECKBOX: {
        row.className += ' tpc-toggle-row';
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
        row.className += ' tpc-field-row';
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
        row.className += ' tpc-tall-row tpc-field-row';
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
        row.className += ' tpc-button-row';
        const button = document.createElement('button');
        button.type = 'button';
        button.className = record.kind === W.BUTTON ? 'tpc-button' : 'tpc-menu';
        bind(button, 'click', () => postWidget(record, INTENT.ACTIVATE));
        row.appendChild(button);
        record.control = button;
        break;
      }
      case W.DROPDOWN: {
        row.className += ' tpc-field-row';
        /* A button and a list this page draws itself, never a SELECT: a
         * native select's open list is the host's own menu, and on macOS that
         * menu runs a modal tracking loop on the main thread -- the thread the
         * game's frame loop runs on -- so the whole client froze for as long
         * as the list stood open. @see dropdownShow. */
        const drop = document.createElement('button');
        drop.type = 'button';
        drop.className = 'tpc-field tpc-dropdown';
        drop.setAttribute('aria-haspopup', 'listbox');
        drop.setAttribute('aria-expanded', 'false');
        const value = document.createElement('span');
        value.className = 'tpc-dropdown-value';
        drop.appendChild(value);
        const arrow = document.createElement('span');
        arrow.className = 'tpc-dropdown-arrow';
        drop.appendChild(arrow);
        bind(drop, 'click', () => {
          if (dropdown.record === record) dropdownHide();
          else dropdownShow(record);
        });
        bind(drop, 'keydown', event => dropdownButtonKey(record, event));
        attachLabel(row, record, drop);
        record.control = drop;
        record.valueNode = value;
        record.arrow = arrow;
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
        /* The retained chrome's roster row: the name, then a settings well
         * (three dots in field chrome) when the row opens something, then the
         * tick/cross switch pinned at the right edge. A table rather than a
         * flex row so XP MSHTML lays the same three zones out. */
        const locked = !!(record.shape & ROW_LOCKED);
        const action = !!(record.shape & ROW_ACTION);
        /* LOCKED without the management ACTION affordance is the semantic
         * ACTION_ROW shape. Give it one real form control rather than making a
         * table cell pretend to be a button: keyboard activation, focus, and
         * its accessible name then come from the browser. Keep the primary
         * label and live summary in distinct retained nodes so a TEXT delta can
         * patch the summary without replacing the control. */
        if (locked && !action) {
          const button = document.createElement('button');
          button.type = 'button';
          button.className = 'tpc-action-row';
          const primary = document.createElement('span');
          primary.className = 'tpc-action-label';
          const summary = document.createElement('span');
          summary.className = 'tpc-action-summary';
          button.appendChild(primary);
          button.appendChild(summary);
          bind(button, 'click', () => postWidget(record, INTENT.ACTION));
          row.appendChild(button);
          record.control = button;
          record.caption = primary;
          record.summary = summary;
          record.actionButton = button;
          break;
        }
        const holder = document.createElement('table');
        holder.className = 'tpc-list-table';
        holder.setAttribute('role', 'presentation');
        holder.setAttribute('cellspacing', '0');
        holder.setAttribute('cellpadding', '0');
        const body = document.createElement('tbody');
        const line = document.createElement('tr');
        const nameCell = document.createElement('td');
        nameCell.className = 'tpc-name-cell';
        const name = document.createElement('span');
        name.className = 'tpc-row-name';
        /* Same zones as the native chrome: everything left of the switch opens
         * the row when it has an action (a locked row is all action zone), and
         * a row with no action toggles from its name so no zone is inert. */
        bind(nameCell, 'click', () => {
          if (locked || action) postWidget(record, INTENT.ACTION);
          else if (record.toggle) {
            record.toggle.checked = !record.toggle.checked;
            postWidget(record, INTENT.TOGGLE, record.toggle.checked ? 1 : 0);
          }
        });
        nameCell.appendChild(name);
        line.appendChild(nameCell);
        if (action) {
          const wellCell = document.createElement('td');
          wellCell.className = 'tpc-well-cell';
          const well = document.createElement('button');
          well.type = 'button';
          well.className = 'tpc-row-well';
          for (let dot = 0; dot < 3; dot++) {
            const mark = document.createElement('i');
            mark.className = `tpc-dot tpc-dot-${dot}`;
            well.appendChild(mark);
          }
          bind(well, 'click', () => postWidget(record, INTENT.ACTION));
          wellCell.appendChild(well);
          line.appendChild(wellCell);
          record.well = well;
        }
        if (!locked) {
          const toggleCell = document.createElement('td');
          toggleCell.className = 'tpc-toggle-cell';
          const toggle = document.createElement('input');
          toggle.type = 'checkbox';
          toggle.className = 'tpc-check';
          bind(toggle, 'change', () => postWidget(record, INTENT.TOGGLE, toggle.checked ? 1 : 0));
          toggleCell.appendChild(toggle);
          line.appendChild(toggleCell);
          record.toggle = toggle;
        }
        body.appendChild(line);
        holder.appendChild(body);
        row.appendChild(holder);
        record.caption = name;
        record.control = holder;
        break;
      }
      case W.COLORPICK: {
        const holder = document.createElement('span');
        holder.className = 'tpc-color';
        const swatch = document.createElement('input');
        /* MSHTML lacks the colour input used by current browser engines. */
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
        custom.style.height = customHeightPx(record.rows);
        custom.tabIndex = 0;
        bind(custom, 'mousedown', event => {
          record.pointer = customContentPoint(custom, event);
        });
        bind(custom, 'mouseup', event => {
          if (!record.pointer) return;
          record.pointer = null;
          const point = customContentPoint(custom, event);
          if (!point) return;
          postWidget(record, INTENT.CUSTOM_ACTIVATE, 0, '',
            customLocalX(record, point), customLocalY(record, point));
        });
        // The secondary click, with the SAME coordinate mapping as the
        // release above: a well is one control, so where it was clicked is
        // the whole of what it is told, and a menu click that arrived without
        // coordinates would name no band. The browser's own context menu is
        // suppressed here and only here, so the page's menu still works
        // everywhere a well is not.
        bind(custom, 'contextmenu', event => {
          const point = customContentPoint(custom, event);
          if (!point) return;
          if (event.preventDefault) event.preventDefault();
          else event.returnValue = false;
          record.pointer = null;
          postWidget(record, INTENT.CUSTOM_MENU, 0, '',
            customLocalX(record, point), customLocalY(record, point));
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
    /*
     * WHERE, not just what. The command's "b" names the row this one must go
     * before, by handle -- the same handle space "w" uses -- because an ADD is
     * not always an append: a row can be given a new identity between
     * unchanged neighbours, and the host states that as REMOVE then ADD.
     *
     * Absent, -1, or a handle this page does not hold means append. The first
     * keeps a stream authored before this field valid; the last is what the
     * in-order snapshot carries, where a row's successor does not exist yet.
     */
    const before = state.widgets.get(integer(command.b, -1));
    if (before && before.row.parentNode === content) content.insertBefore(row, before.row);
    else content.appendChild(row);
    state.widgets.set(record.handle, record);
    indexWidget(record);
    queueWidgetRender(record, RENDER.FULL);
  }

  function removeWidget(handle) {
    const record = state.widgets.get(handle);
    if (!record) return;
    if (dropdown.record === record) dropdownHide();
    if (record.row.parentNode) record.row.parentNode.removeChild(record.row);
    unindexWidget(record);
    if (record.kind === W.TABSTRIP && !state.tabStrips.count) setTabsVisible(false);
    state.widgets.drop(handle);
  }

  function renderOptions(record) {
    if (!record || !record.control) return;
    if (record.kind === W.DROPDOWN) {
      /* The rows exist only while the list is open, so a closed dropdown's
       * options are data and nothing to rebuild; an open one is rebuilt in
       * place over the same button. */
      renderDropdownValue(record, record.selected);
      if (dropdown.record === record) dropdownShow(record);
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

  function renderWidgetContent(record) {
    switch (record.kind) {
      case W.LABEL:
        if (record.shape === LABEL.KEY_VALUE) {
          setText(record.caption, record.label || 'Value');
          setText(record.valueNode, record.text || '\u2014');
        } else if (record.shape === LABEL.PROGRESS) {
          setText(record.caption, record.label || 'Progress');
          setText(record.valueNode, record.text || `${Math.max(0, record.selected)}%`);
        } else if (record.shape === LABEL.ERROR) {
          setText(record.caption, record.label || 'Plugin error');
          setText(record.valueNode, record.text ? ` ${record.text}` : '');
        } else {
          setText(record.control, record.text || record.label);
        }
        if (record.color && record.control) {
          const raw = (record.color & 0xffffff).toString(16);
          record.control.style.color = `#${'000000'.slice(raw.length)}${raw}`;
        }
        break;
      case W.CHECKBOX:
        skinCheck(record.control, !!record.checked);
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
        skinDropdown(record);
        break;
      case W.MODELVIEW:
        setText(record.control, record.label || record.text || 'Model preview');
        break;
      case W.LISTROW:
        if (record.actionButton) {
          const primary = record.label || 'Open';
          const summary = record.text || '';
          const accessible = summary ? `${primary}. ${summary}` : primary;
          setText(record.caption, primary);
          setText(record.summary, summary);
          hidden(record.summary, !summary);
          record.actionButton.title = text(accessible, 255);
          record.actionButton.setAttribute('aria-label', text(accessible, 255));
        } else {
          setText(record.caption, record.label || record.text);
          if (record.well) record.well.title = text(record.label || record.text, 63);
        }
        if (record.toggle) skinCheck(record.toggle, !!record.checked);
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
  }

  function renderWidget(record) {
    if (!record) return;
    renderWidgetContent(record);
    if (record.kind === W.DROPDOWN || record.kind === W.TABSTRIP)
      renderOptions(record);
    renderWidgetSelection(record);
    renderWidgetVisibility(record);
    renderWidgetFocus(record);
  }

  function renderWidgetVisibility(record) {
    hidden(record.row, !!record.hidden ||
      (record.tab >= 0 && record.tab !== state.activeTab) ||
      record.kind === W.TABSTRIP || record.kind === W.FREE);
  }

  function renderWidgetSelection(record) {
    if (record.kind === W.DROPDOWN && record.control)
      renderDropdownValue(record, record.selected);
    else if (record.kind === W.LABEL && record.shape === LABEL.PROGRESS && record.control) {
      const value = Math.max(0, Math.min(100, integer(record.selected, 0)));
      if (record.progressFill) record.progressFill.style.width = `${value}%`;
      else record.control.value = value;
      record.control.setAttribute('aria-valuemin', '0');
      record.control.setAttribute('aria-valuemax', '100');
      record.control.setAttribute('aria-valuenow', String(value));
      if (!record.text) setText(record.valueNode, `${value}%`);
    }
  }

  function renderWidgetFocus(record) {
    if (record.focused && record.control && record.control.focus) {
      try { record.control.focus({ preventScroll: true }); }
      catch (error) { record.control.focus(); }
    }
  }

  function renderWidgetParts(record, flags) {
    state.renderVisits++;
    if (flags & RENDER.FULL) {
      renderWidget(record);
      return;
    }
    if (flags & RENDER.OPTIONS) renderOptions(record);
    if (flags & RENDER.SELECTION) renderWidgetSelection(record);
    if (flags & RENDER.VISIBILITY) renderWidgetVisibility(record);
    if (flags & RENDER.FOCUS) renderWidgetFocus(record);
    if (flags & RENDER.CONTENT) renderWidgetContent(record);
  }

  /* One clamp for a custom well's height, so the ADD that first states it
   * and the WIDGET_HEIGHT that moves it cannot disagree. */
  function customHeightPx(rows) {
    return `${Math.max(48, Math.min(512, rows || 120))}px`;
  }

  function applyCommand(raw) {
    const command = normalizeCommand(raw);
    if (command.k === CMD.CHECK_STYLE) {
      state.checkStyle = command.v;
      toggleClass(shell, 'tpc-check-square', command.v === 1);
      state.checkWidgets.each(record => queueWidgetRender(record, RENDER.CONTENT));
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
        if (state.activeTab !== command.v) {
          const previous = state.activeTab;
          state.activeTab = command.v;
          queueTabVisibility(previous);
          queueTabVisibility(state.activeTab);
          state.tabStrips.each(tabstrip =>
            queueWidgetRender(tabstrip, RENDER.OPTIONS));
        }
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
        if (record) {
          record.label = command.label;
          queueWidgetRender(record, RENDER.CONTENT);
        }
        break;
      case CMD.WIDGET_TEXT:
        if (record) {
          record.text = command.text;
          queueWidgetRender(record, RENDER.CONTENT);
        }
        break;
      case CMD.WIDGET_CHECKED:
        if (record) {
          record.checked = !!command.v;
          queueWidgetRender(record, RENDER.CONTENT);
        }
        break;
      case CMD.WIDGET_HEIGHT:
        /* Resized in place, deliberately without renderWidget: the well holds
         * drawn content a rebuilt element would lose, and a list growing a
         * row is the ordinary case rather than the exceptional one. */
        if (record && record.kind === W.CUSTOM) {
          record.rows = command.ch;
          if (record.control) record.control.style.height = customHeightPx(record.rows);
        }
        break;
      case CMD.WIDGET_HIDDEN:
        if (record) {
          record.hidden = !!command.v;
          queueWidgetRender(record, RENDER.VISIBILITY);
        }
        break;
      case CMD.WIDGET_COLOR:
        if (record) {
          record.color = command.c;
          queueWidgetRender(record, RENDER.CONTENT);
        }
        break;
      case CMD.WIDGET_SELECTED:
        if (record) {
          record.selected = command.v;
          queueWidgetRender(record, RENDER.SELECTION);
        }
        break;
      case CMD.WIDGET_FOCUS:
        if (record) {
          record.focused = !!command.v;
          queueWidgetRender(record, RENDER.FOCUS);
        }
        break;
      case CMD.WIDGET_OPTIONS:
        if (record) {
          const count = Math.max(0, Math.min(MAX_OPTIONS, command.v));
          record.structuredOptions = !!command.x;
          record.options = new Array(count);
          for (let i = 0; i < count; i++) record.options[i] =
            record.structuredOptions
              ? { value: '', label: '', enabled: false, detail: '' }
              : '';
          record.optionsRevision++;
          queueWidgetRender(record, RENDER.OPTIONS);
        }
        break;
      case CMD.WIDGET_OPTION:
        if (record && command.v >= 0 && command.v < record.options.length) {
          record.options[command.v] = record.structuredOptions
            ? { value: command.text, label: command.label,
                enabled: !!command.x, detail: command.detail }
            : command.text;
          record.optionsRevision++;
          queueWidgetRender(record, RENDER.OPTIONS);
        }
        break;
      default:
        break;
    }
  }

  function pageCommands(message) {
    const commands = array(message.commands) ? message.commands : [];
    if (commands.length > MAX_PAGE_COMMANDS) return null;
    for (let i = 0; i < commands.length; i++) {
      const command = commands[i] || {};
      const kind = integer(command.k, 0);
      const value = integer(command.v, 0);
      if (kind === CMD.WIDGET_OPTIONS && (value < 0 || value > MAX_OPTIONS)) return null;
      if (kind === CMD.WIDGET_OPTION && (value < 0 || value >= MAX_OPTIONS)) return null;
    }
    return commands;
  }

  function applyPageSnapshot(message) {
    const generation = unsigned(message.pageGeneration);
    const panel = integer(message.panel, -1);
    const commands = pageCommands(message);
    if (!generation || panel < 0 || !commands) return false;
    clearPage(false);
    state.pageGeneration = generation;
    state.panel = panel;
    state.activeTab = 0;
    state.checkStyle = integer(message.checkStyle, 0);
    setText(title, message.title || 'Plugins');
    hidden(pane, false);
    toggleClass(shell, 'tpc-collapsed', false);
    state.applying = true;
    try { for (let i = 0; i < commands.length; i++) applyCommand(commands[i]); }
    finally { state.applying = false; }
    flushWidgetRenders();
    reportLayout();
    return true;
  }

  function applyPageDelta(message) {
    const commands = pageCommands(message);
    if (!state.pageGeneration || unsigned(message.pageGeneration) !== state.pageGeneration ||
        !commands) return false;
    state.applying = true;
    try { for (let i = 0; i < commands.length; i++) applyCommand(commands[i]); }
    finally { state.applying = false; }
    flushWidgetRenders();
    /* A retained height/visibility/text change can make the content scrollbar
     * appear or disappear without resizing the outer window. That changes a
     * full-width custom well, so publish its post-layout content width too. */
    reportLayout();
    return true;
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
    /* A closed page has no semantic generation to fence a layout with. The
     * rail snapshot already carries its collapsed state, so do not retain an
     * unrouteable generation-zero layout in the pull fallback. */
    if (!state.pageGeneration) {
      state.lastLayout = '';
      return;
    }
    const visible = pane.style.display !== 'none' && !!state.pageGeneration;
    /* The PANE's own box, never the view: with the rail shown the pane stops
     * at the rail's track (less the shared 6px frame seam), and with the rail
     * hidden (tpc-rail-hidden) it runs flush to the right edge, so neither
     * case counts the rail's 42px. */
    const width = visible ? Math.max(0, pane.clientWidth || 0) : 0;
    const height = visible ? Math.max(0, pane.clientHeight || 0) : 0;
    let customWidth = 0;
    if (visible) state.customWidgets.each(record => {
      /* CUSTOM is full-width in both browser styles. clientWidth is its
       * content box: pane/content padding, the scrollbar and the well border
       * have therefore already been removed by the browser's own layout. */
      if (!customWidth && record.kind === W.CUSTOM && !record.hidden && record.control)
        customWidth = Math.max(0, integer(record.control.clientWidth, 0));
    });
    const scaleMilli = Math.max(1, Math.round(Number(global.devicePixelRatio || 1) * 1000));
    const sizeClass = width < 320 ? 0 : (width >= 480 ? 2 : 1);
    const key = [state.rail.selectionGeneration, state.pageGeneration, width, height,
      customWidth, scaleMilli, sizeClass, visible ? 1 : 0, state.rail.railHidden ? 1 : 0].join(':');
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
      customWidth,
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
      case 'page.snapshot': return applyPageSnapshot(message);
      case 'page.delta': return applyPageDelta(message);
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
    if (state.messageOverflow || state.intentOverflow) {
      state.messageOverflow = false;
      state.intentOverflow = false;
      return Codec.stringify({ protocol: PROTOCOL, type: 'transport.loss' });
    }
    return state.messages.length ? Codec.stringify(state.messages.shift()) : '';
  }

  function takeIntent() {
    return state.intents.length ? Codec.stringify(state.intents.shift()) : '';
  }

  function takeIntentOverflow() {
    const overflow = state.intentOverflow;
    state.intentOverflow = false;
    return overflow;
  }

  function postPanelClose() {
    if (state.panel < 0 || !state.pageGeneration) return;
    const intent = normalizeIntent({
      k: INTENT.CLOSE, p: state.panel, w: -1,
      g: state.pageGeneration, s: 0
    });
    queueEnvelope({
      protocol: PROTOCOL, type: 'widget.intent', sequence: nextSequence(), intent
    }, intent);
  }

  bind(close, 'click', postPanelClose);

  /* ---- the right-click popup ----------------------------------------------
   *
   * The hazard is the HOST VIEW's own context menu, not a gap in this page.
   * Its Reload entry re-runs the bundle, and the page that comes back has no
   * rail, no theme and no widgets while the host still believes every
   * generation and handle it has handed out is live -- the next command it
   * sends is addressed to a page that cannot answer for it, which is where
   * the plugin API goes down. So the page cancels the native menu on every
   * engine and answers the click itself.
   *
   * What it answers with is the client's own "Choose Option" popup
   * (uitree_minimenu.c / emit_minimenu), in CSS: a 16px title bar set in the
   * BODY colour on black, 15px rows in white that go accent-yellow under the
   * cursor, and Cancel at the bottom. Rows post the intents the left-click
   * controls already post -- a right click is another way to press the thing
   * under it, never a second protocol, so nothing about the host contract
   * changes.
   *
   * The popup is clipped by the host's allocation like every other pixel this
   * page draws, and the rail's allocation is 42 wide: rail rows are therefore
   * the reference's own bare verbs, which fit it.
   */

  /* Desktop layout's click_y_bias (line box 16 - 5): the click lands on the
   * title bar, as it does in the canvas. */
  const MINIMENU_CLICK_BIAS = 11;
  const popup = { back: null, box: null, keydown: null };

  function insideNode(node, ancestor) {
    for (let at = node; at; at = at.parentNode) if (at === ancestor) return true;
    return false;
  }

  function popupHost() {
    return document.body || shell.parentNode || shell;
  }

  function popupHide() {
    const back = popup.back;
    if (!back) return;
    popup.back = null;
    popup.box = null;
    if (back.parentNode) back.parentNode.removeChild(back);
    document.onkeydown = popup.keydown || null;
    popup.keydown = null;
  }

  function popupKeyDown(event) {
    event = event || global.event || {};
    if ((event.keyCode || event.which || 0) !== 27) {
      if (typeof popup.keydown === 'function') return popup.keydown(event);
      return undefined;
    }
    popupHide();
    return false;
  }

  function addRow(rows, label, run) {
    rows.push({ label: text(label, 63), run });
  }

  function cancelEvent(event) {
    if (event.preventDefault) event.preventDefault();
    event.returnValue = false;
    return false;
  }

  /* The text field a click landed in, or null. */
  function fieldOf(node) {
    if (!node || !node.tagName) return null;
    const tag = String(node.tagName).toUpperCase();
    if (tag === 'TEXTAREA') return node;
    if (tag === 'INPUT' && node.type !== 'checkbox' && node.type !== 'color') return node;
    return null;
  }

  function railRows(rows, button) {
    const pluginIndex = integer(button._tpcPluginIndex, -1);
    const open = pluginIndex === state.rail.selectedEntry && state.rail.expanded;
    addRow(rows, open ? 'Hide' : 'Open', () => postRailSelect(button));
  }

  function toggleRow(rows, record, box) {
    addRow(rows, record.checked ? 'Turn off' : 'Turn on', () => {
      box.checked = !record.checked;
      postWidget(record, INTENT.TOGGLE, box.checked ? 1 : 0);
    });
  }

  function recordRows(rows, record) {
    const caption = trimmed(record.text || record.label);
    switch (record.kind) {
      case W.MENUITEM:
      case W.BUTTON:
        addRow(rows, caption || 'Select', () => postWidget(record, INTENT.ACTIVATE));
        break;
      case W.CHECKBOX:
        toggleRow(rows, record, record.control);
        break;
      case W.LISTROW:
        if ((record.shape & ROW_LOCKED) && !(record.shape & ROW_ACTION))
          addRow(rows, record.label || 'Open', () => postWidget(record, INTENT.ACTION));
        else if (record.shape & (ROW_ACTION | ROW_LOCKED))
          addRow(rows, 'Settings', () => postWidget(record, INTENT.ACTION));
        if (record.toggle) toggleRow(rows, record, record.toggle);
        break;
      case W.CUSTOM:
        addRow(rows, caption || 'Select', () => postWidget(record, INTENT.CUSTOM_ACTIVATE));
        break;
      default:
        break;
    }
  }

  /* Cut and copy act on the selection the click left standing, and are left
   * out when there is none -- which is the greying-out the native menu does.
   * Paste is not offered because no engine here permits a scripted one; the
   * keyboard still pastes. */
  function fieldRows(rows, field) {
    const selection = typeof field.selectionStart === 'number' &&
      typeof field.selectionEnd === 'number'
      ? field.selectionEnd > field.selectionStart : true;
    const command = name => {
      if (field.focus) field.focus();
      try { document.execCommand(name); }
      catch (error) { /* An engine that refuses the edit leaves the field alone. */ }
    };
    if (selection) {
      addRow(rows, 'Copy', () => command('copy'));
      addRow(rows, 'Cut', () => command('cut'));
    }
    addRow(rows, 'Select all', () => {
      if (field.focus) field.focus();
      if (field.select) field.select();
    });
  }

  function popupRowsFor(target) {
    const rows = [];
    const host = popupHost();
    let record = null;
    let railButton = null;
    let field = null;
    for (let node = target; node && node !== host; node = node.parentNode) {
      if (!field) field = fieldOf(node);
      if (!record && node._tpcRecord) record = node._tpcRecord;
      if (!railButton && integer(node._tpcPluginIndex, -1) !== -1) railButton = node;
    }
    if (railButton) railRows(rows, railButton);
    else if (record) recordRows(rows, record);
    if (field) fieldRows(rows, field);
    if (!railButton && state.panel >= 0 && state.pageGeneration)
      addRow(rows, 'Close', postPanelClose);
    addRow(rows, 'Cancel', popupHide);
    return rows;
  }

  function popupPlace(x, y) {
    const doc = document.documentElement || {};
    const viewWidth = integer(doc.clientWidth, 0) || integer(global.innerWidth, 0);
    const viewHeight = integer(doc.clientHeight, 0) || integer(global.innerHeight, 0);
    const width = integer(popup.box.offsetWidth, 0);
    const height = integer(popup.box.offsetHeight, 0);
    let left = x - Math.floor(width / 2);
    let top = y - MINIMENU_CLICK_BIAS;
    if (width && viewWidth && left + width > viewWidth) left = viewWidth - width;
    if (height && viewHeight && top + height > viewHeight) top = viewHeight - height;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    popup.box.style.left = `${left}px`;
    popup.box.style.top = `${top}px`;
  }

  function popupShow(rows, x, y) {
    popupHide();
    if (!rows.length) return;
    const back = document.createElement('div');
    back.className = 'tpc-minimenu-backdrop';
    const box = document.createElement('div');
    box.className = 'tpc-minimenu';
    const heading = document.createElement('div');
    heading.className = 'tpc-minimenu-title';
    setText(heading, 'Choose Option');
    box.appendChild(heading);
    const separator = document.createElement('div');
    separator.className = 'tpc-minimenu-separator';
    box.appendChild(separator);
    const list = document.createElement('div');
    list.className = 'tpc-minimenu-list';
    for (let i = 0; i < rows.length; i++) {
      const entry = rows[i];
      const option = document.createElement('button');
      option.type = 'button';
      option.className = 'tpc-minimenu-option';
      option.title = entry.label;
      setText(option, entry.label);
      bind(option, 'click', () => {
        popupHide();
        entry.run();
      });
      list.appendChild(option);
    }
    box.appendChild(list);
    back.appendChild(box);
    /* The backdrop is what dismisses the popup, so no document-wide mouse
     * handler has to exist for it -- a press on the popup itself is the row's
     * own, and every other press closes. */
    bind(back, 'mousedown', event => {
      event = event || global.event || {};
      if (insideNode(event.target || event.srcElement, box)) return undefined;
      popupHide();
      return cancelEvent(event);
    });
    popupHost().appendChild(back);
    popup.back = back;
    popup.box = box;
    popup.keydown = document.onkeydown;
    document.onkeydown = popupKeyDown;
    popupPlace(x, y);
  }

  /* ---- the open dropdown list ---------------------------------------------
   *
   * dbg_build_dropdown_list in CSS: a list the same width as its button,
   * hanging from the button's bottom edge, rows of 20 inside 2px of frame,
   * ten rows before it scrolls, and the bar inside the frame rather than
   * beside it. It flips above the button only when below has no room.
   *
   * Drawn by this page rather than by the engine because the engine's list is
   * a native menu, and a native menu blocks the thread that opened it until it
   * closes -- on macOS that is the game's main thread. Nothing here waits.
   */
  const DROP_ROW_H = 20;
  const DROP_PAD = 2;
  const DROP_ROWS = 10;
  const dropdown = {
    record: null, back: null, box: null, scroller: null, rows: [],
    hover: -1, keydown: null
  };

  function optionText(record, index) {
    const item = record.options[index];
    if (!record.structuredOptions) return text(item);
    if (!item) return '';
    return item.detail ? `${item.label} — ${item.detail}` : item.label;
  }

  function optionEnabled(record, index) {
    if (index < 0 || index >= record.options.length) return false;
    if (!record.structuredOptions) return true;
    const item = record.options[index];
    return !!(item && item.enabled);
  }

  /* SHOWN is what the button says, which runs ahead of SELECTED between a
   * pick and the host's answer to it -- the same lead a native select had. */
  function renderDropdownValue(record, index) {
    const valid = index >= 0 && index < record.options.length;
    record.shown = valid ? index : -1;
    const caption = valid ? optionText(record, index) : '';
    setText(record.valueNode, caption);
    record.control.title = text(caption, 255);
  }

  function dropdownHover(index) {
    const rows = dropdown.rows;
    if (dropdown.hover >= 0 && dropdown.hover < rows.length)
      toggleClass(rows[dropdown.hover], 'tpc-dropdown-hover', false);
    dropdown.hover = optionEnabled(dropdown.record, index) ? index : -1;
    if (dropdown.hover >= 0)
      toggleClass(rows[dropdown.hover], 'tpc-dropdown-hover', true);
  }

  function dropdownReveal(index) {
    const scroller = dropdown.scroller;
    const top = index * DROP_ROW_H;
    const view = Math.min(dropdown.rows.length, DROP_ROWS) * DROP_ROW_H;
    if (top < scroller.scrollTop) scroller.scrollTop = top;
    else if (top + DROP_ROW_H > scroller.scrollTop + view)
      scroller.scrollTop = top + DROP_ROW_H - view;
  }

  function dropdownHide() {
    const record = dropdown.record;
    const back = dropdown.back;
    if (!record) return;
    dropdown.record = null;
    dropdown.back = null;
    dropdown.box = null;
    dropdown.scroller = null;
    dropdown.rows = [];
    dropdown.hover = -1;
    if (back && back.parentNode) back.parentNode.removeChild(back);
    document.onkeydown = dropdown.keydown || null;
    dropdown.keydown = null;
    toggleClass(record.control, 'tpc-dropdown-open', false);
    record.control.setAttribute('aria-expanded', 'false');
    skinDropdown(record);
  }

  function dropdownPick(index) {
    const record = dropdown.record;
    if (!optionEnabled(record, index)) return;
    dropdownHide();
    if (record.control.focus) record.control.focus();
    if (index === record.shown) return;
    renderDropdownValue(record, index);
    if (record.structuredOptions)
      postWidget(record, INTENT.PICK, index, record.options[index].value);
    else
      postWidget(record, INTENT.PICK, index);
  }

  /* The next enabled option after FROM, walking by STEP, or -1. */
  function dropdownStep(record, from, step) {
    for (let at = from + step; at >= 0 && at < record.options.length; at += step)
      if (optionEnabled(record, at)) return at;
    return -1;
  }

  /* The next enabled option after the cursor whose caption starts with KEY,
   * wrapping -- a select's own type-to-jump. */
  function dropdownSeek(record, from, key) {
    const count = record.options.length;
    for (let i = 1; i <= count; i++) {
      const at = (from + i + count) % count;
      if (optionEnabled(record, at) &&
          optionText(record, at).charAt(0).toUpperCase() === key) return at;
    }
    return -1;
  }

  function dropdownKeyDown(event) {
    event = event || global.event || {};
    const record = dropdown.record;
    const code = event.keyCode || event.which || 0;
    const from = dropdown.hover >= 0 ? dropdown.hover : record.shown;
    let next = -1;
    switch (code) {
      case 27:
        dropdownHide();
        return cancelEvent(event);
      case 9:
        dropdownHide();
        return undefined;
      case 13:
      case 32:
        if (dropdown.hover >= 0) dropdownPick(dropdown.hover);
        return cancelEvent(event);
      case 38: next = dropdownStep(record, from, -1); break;
      case 40: next = dropdownStep(record, from, 1); break;
      case 36: next = dropdownStep(record, -1, 1); break;
      case 35: next = dropdownStep(record, record.options.length, -1); break;
      case 33: next = dropdownStep(record, Math.max(0, from - DROP_ROWS + 1), -1); break;
      case 34:
        next = dropdownStep(record, Math.min(record.options.length - 1, from + DROP_ROWS - 1), 1);
        break;
      default:
        if ((code >= 48 && code <= 57) || (code >= 65 && code <= 90))
          next = dropdownSeek(record, from, String.fromCharCode(code));
        else return undefined;
    }
    if (next >= 0) {
      dropdownHover(next);
      dropdownReveal(next);
    }
    return cancelEvent(event);
  }

  function dropdownButtonKey(record, event) {
    event = event || global.event || {};
    const code = event.keyCode || event.which || 0;
    /* Open: the document handler owns every key. Closed: the arrows open the
     * list, and Enter/Space reach the button's own click. */
    if (dropdown.record === record || (code !== 38 && code !== 40)) return undefined;
    dropdownShow(record);
    return cancelEvent(event);
  }

  function dropdownPlace(record) {
    const doc = document.documentElement || {};
    const viewHeight = integer(doc.clientHeight, 0) || integer(global.innerHeight, 0);
    const bounds = record.control.getBoundingClientRect();
    const height = Math.min(record.options.length, DROP_ROWS) * DROP_ROW_H + 2 * DROP_PAD;
    const boxTop = Math.round(bounds.top);
    let top = Math.round(bounds.bottom);
    if (viewHeight && top + height > viewHeight) {
      const below = viewHeight - top;
      if (height <= boxTop) top = boxTop - height;
      else if (boxTop > below) top = 0;
      else top = viewHeight - height;
      if (top < 0) top = 0;
    }
    dropdown.box.style.left = `${Math.round(bounds.left)}px`;
    dropdown.box.style.top = `${top}px`;
    dropdown.box.style.width = `${Math.round(bounds.right - bounds.left)}px`;
    dropdown.box.style.height = `${height}px`;
    dropdown.scroller.style.height = `${height - 2 * DROP_PAD}px`;
  }

  function dropdownShow(record) {
    popupHide();
    const reopening = dropdown.record === record;
    const scrollTop = reopening ? dropdown.scroller.scrollTop : -1;
    dropdownHide();
    if (!record.options.length) return;

    const back = document.createElement('div');
    back.className = 'tpc-dropdown-backdrop';
    const box = document.createElement('div');
    box.className = 'tpc-dropdown-list';
    box.setAttribute('role', 'listbox');
    if (!legacy)
      box.style.backgroundImage = cssUrl(safeUrl(state.theme.dropdownBody) ||
        'skin/DropdownBody.png');
    const scroller = document.createElement('div');
    scroller.className = 'tpc-dropdown-scroller';
    const rows = [];
    for (let index = 0; index < record.options.length; index++) {
      const row = document.createElement('div');
      const enabled = optionEnabled(record, index);
      /* The bands alternate on the OPTION index, so they scroll with the text. */
      row.className = `tpc-dropdown-option${index & 1 ? ' tpc-dropdown-alt' : ''}` +
        `${enabled ? '' : ' tpc-dropdown-disabled'}`;
      row.setAttribute('role', 'option');
      row.setAttribute('aria-selected', index === record.shown ? 'true' : 'false');
      row.setAttribute('aria-disabled', enabled ? 'false' : 'true');
      const caption = optionText(record, index);
      setText(row, caption);
      row.title = text(caption, 255);
      /* A row answers only while it is in the standing list: an event queued
       * behind the list's own close, or a rebuild, must not land on a list it
       * is no longer part of. */
      const live = () => dropdown.rows[index] === row;
      bind(row, 'mouseover', () => { if (live()) dropdownHover(index); });
      bind(row, 'mouseout', () => {
        if (live() && dropdown.hover === index) dropdownHover(-1);
      });
      /* The press keeps focus on the button, as a select's list does. */
      bind(row, 'mousedown', event => cancelEvent(event || global.event || {}));
      bind(row, 'click', () => { if (live()) dropdownPick(index); });
      scroller.appendChild(row);
      rows.push(row);
    }
    box.appendChild(scroller);
    back.appendChild(box);
    /* Any press outside the list closes it and goes no further, as the
     * right-click popup's backdrop does -- including a press on the button,
     * which is how the button closes its own list. */
    bind(back, 'mousedown', event => {
      event = event || global.event || {};
      if (insideNode(event.target || event.srcElement, box)) return undefined;
      dropdownHide();
      return cancelEvent(event);
    });
    popupHost().appendChild(back);

    dropdown.record = record;
    dropdown.back = back;
    dropdown.box = box;
    dropdown.scroller = scroller;
    dropdown.rows = rows;
    dropdown.keydown = document.onkeydown;
    document.onkeydown = dropdownKeyDown;
    toggleClass(record.control, 'tpc-dropdown-open', true);
    record.control.setAttribute('aria-expanded', 'true');
    skinDropdown(record);
    dropdownPlace(record);
    /* The chosen option starts under the cursor and in view; a list rebuilt
     * because its options changed keeps the scroll it had. */
    dropdownHover(record.shown);
    if (scrollTop >= 0) scroller.scrollTop = scrollTop;
    else if (record.shown >= 0) dropdownReveal(record.shown);
  }

  bind(document, 'contextmenu', event => {
    event = event || global.event || {};
    const target = event.target || event.srcElement || null;
    const repeat = popup.box && insideNode(target, popup.box);
    dropdownHide();
    popupHide();
    if (!repeat)
      popupShow(popupRowsFor(target), integer(event.clientX, 0), integer(event.clientY, 0));
    return cancelEvent(event);
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

  /* A resize moves everything the popup was pointing at, and the host resizes
   * this view whenever a page opens or closes. */
  global.onresize = () => {
    popupHide();
    dropdownHide();
    reportLayout();
  };
  sizeLegacyViewport();

  return {
    protocol: PROTOCOL,
    receive,
    takeMessage,
    takeIntent,
    takeIntentOverflow,
    inspect() {
      return {
        railEntries: state.rail.entries.length,
        selectedEntry: state.rail.selectedEntry,
        expanded: state.rail.expanded,
        railHidden: state.rail.railHidden,
        pageGeneration: state.pageGeneration,
        panel: state.panel,
        widgetCount: state.widgets.count,
        queuedMessages: state.messages.length,
        renderVisits: state.renderVisits
      };
    },
    constants: { CMD, W, INTENT }
  };
});
