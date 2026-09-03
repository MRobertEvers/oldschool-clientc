/*
 * Web host for the canonical retained plugin-chrome bundle.
 *
 * The application owns exactly one iframe and that iframe owns exactly one
 * rail/page state machine. Plugins supply bounded metadata, semantic command
 * PODs and bitmap pixels; they never supply markup or script. The old wasm
 * hooks below are intentionally only adapters into protocol 1 so the browser
 * no longer has a second implementation of the chrome.
 */
(function (global) {
  'use strict';

  const PROTOCOL = 1;
  const MAX_PENDING = 128;
  const MAX_PENDING_BYTES = 8 * 1024 * 1024;
  const MAX_BATCH_COMMANDS = 8192;
  const MAX_CUSTOM_PIXELS = 1200000;
  const MAX_INTENTS = 64;
  /* The rail's allocation, in CSS pixels: the shared bundle lays the rail out
   * at TORIRS_CHROME_M_RAIL_W (42, the gameframe popout strip's own width,
   * frame included), and the dock must reserve exactly that. */
  const RAIL_WIDTH = 42;
  const PANEL_MIN = 280;
  const PANEL_MAX = 640;
  const PANEL_DEFAULT = 360;
  const GAME_MIN = 765;

  /* Kept literal so web/test/chrome_enum_sync_test.js can compare the shared
   * command boundary directly with the C header. */
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
    CLOSE: 7, CUSTOM_ACTIVATE: 8
  };

  const THEME = {
    protocol: PROTOCOL,
    type: 'theme',
    revision: 1,
    assets: {
      panelBody: 'skin/PanelBody.png',
      pluginIcon: 'skin/PluginIcon.png',
      buttonLeft: 'skin/ButtonLeft.png',
      buttonMiddle: 'skin/ButtonMid.png',
      buttonRight: 'skin/ButtonRight.png',
      checkOn: 'skin/CheckOn.png',
      checkOff: 'skin/CheckOff.png',
      checkBoxOn: 'skin/CheckBoxOn.png',
      checkBoxOff: 'skin/CheckBoxOff.png',
      dropdownBody: 'skin/DropdownBody.png',
      scrollUp: 'skin/ScrollUp.png',
      scrollDown: 'skin/ScrollDown.png',
      scrollTrack: 'skin/ScrollTrack.png',
      scrollGripTop: 'skin/ScrollGripTop.png',
      scrollGripMiddle: 'skin/ScrollGripMid.png',
      scrollGripBottom: 'skin/ScrollGripBottom.png',
      close: 'skin/CloseButton.png',
      frameTopLeft: 'skin/FrameTopLeft.png',
      frameTop: 'skin/FrameTop.png',
      frameTopRight: 'skin/FrameTopRight.png',
      frameLeft: 'skin/FrameLeft.png',
      frameRight: 'skin/FrameRight.png',
      frameBottomLeft: 'skin/FrameBottomLeft.png',
      frameBottom: 'skin/FrameBottom.png',
      frameBottomRight: 'skin/FrameBottomRight.png'
    }
  };

  function integer(value, fallback) {
    return typeof value === 'number' && Number.isFinite(value) && Math.floor(value) === value
      ? value : fallback;
  }

  function unsigned(value) { return Number(value) >>> 0; }
  function boundedText(value, max) { return String(value == null ? '' : value).slice(0, max); }

  function copy(value) {
    /* Every crossing is detached from wasm temporaries and from the runtime's
     * retained state. Protocol envelopes contain JSON data only. */
    return JSON.parse(JSON.stringify(value));
  }

  function classSet(node, name, enabled) {
    if (!node || !node.classList) return;
    node.classList.toggle(name, !!enabled);
  }

  class PluginChromeHost {
    constructor(root, document_) {
      this.global = root;
      this.document = document_;
      this.mount = null;
      this.frame = null;
      this.runtime = null;
      this.pending = [];
      this.pendingBytes = 0;
      this.pendingSerial = 0;
      this.intents = [];
      this.batch = [];
      this.collecting = false;
      this.batchOverflow = false;
      this.deliveryLost = false;
      this.executorOpen = false;
      this.pageOpen = false;
      this.pagePanel = -1;
      this.pageGeneration = 0;
      this.checkStyle = 0;
      this.title = 'Plugins';
      this.lastOutboundSequence = 0;
      this.lastLayoutKey = '';
      this.layoutMode = 'closed';
      this.customRevisions = {};
      this.widgetSerials = {};
      this.rail = {
        protocol: PROTOCOL, type: 'rail.snapshot', registryRevision: 0,
        selectionGeneration: 0, pageGeneration: 0, activePlugin: -1,
        lastSelectedPlugin: -1, selectedEntry: -1, expanded: false, entries: []
      };
      this.ensureFrame();
      this.installResize();
    }

    ensureFrame() {
      if (this.frame || !this.document || !this.document.createElement) return !!this.frame;
      this.mount = this.document.getElementById &&
        this.document.getElementById('plugin-chrome-mount');
      if (!this.mount) return false;
      const frame = this.document.createElement('iframe');
      frame.id = 'plugin-chrome-frame';
      frame.className = 'torirs-plugin-chrome-frame';
      frame.title = 'Plugin tools';
      frame.src = 'plugin_chrome/modern.html';
      frame.setAttribute('sandbox', 'allow-scripts allow-same-origin');
      frame.setAttribute('referrerpolicy', 'no-referrer');
      frame.addEventListener('load', () => { this.bindFrame(); });
      this.mount.appendChild(frame);
      this.frame = frame;
      return true;
    }

    bindFrame() {
      let child;
      try { child = this.frame && this.frame.contentWindow; }
      catch (error) { child = null; }
      if (!child || !child.ToriRSPluginChrome) return false;
      return this.attachRuntime(child.ToriRSPluginChrome, child);
    }

    /* Public for the deterministic Node/browser harness. Production reaches
     * it exactly once from the iframe load event. */
    attachRuntime(runtime, child) {
      if (!runtime || typeof runtime.receive !== 'function') return false;
      this.runtime = runtime;
      if (child) child.torirsPluginChromePostMessage = json => { this.fromRuntime(json); };
      let accepted = runtime.receive(copy(THEME)) !== false;
      const queued = this.pending;
      this.pending = [];
      this.pendingBytes = 0;
      /* A replaced iframe starts its outbound sequence at one again. */
      this.lastOutboundSequence = 0;
      for (let i = 0; i < queued.length; i++)
        if (runtime.receive(queued[i].message) === false) accepted = false;
      if (!accepted) this.deliveryLost = true;
      return accepted;
    }

    installResize() {
      if (!this.global || typeof this.global.addEventListener !== 'function') return;
      this.global.addEventListener('resize', () => { this.updateLayout(); });
    }

    send(envelope) {
      const message = copy(envelope);
      if (this.runtime) {
        try { return this.runtime.receive(message) !== false; }
        catch (error) { return false; }
      }
      const bytes = JSON.stringify(message).length;
      if (bytes > MAX_PENDING_BYTES) return false;
      let key = '';
      if (message.type === 'theme' || message.type === 'rail.snapshot') key = message.type;
      else if (message.type === 'rail.icon') key = `rail.icon:${message.pluginIndex}`;
      else if (message.type === 'custom.bitmap')
        key = `custom:${message.pageGeneration}:${message.widget}:${message.widgetSerial}`;
      else if (message.type === 'page.snapshot' || message.type === 'page.close') key = 'page';
      else key = `ordered:${++this.pendingSerial}`;

      /* Build the candidate queue off to the side. A rejected send must leave
       * every previously accepted ordered delta intact; otherwise returning
       * false would still have damaged the only copy waiting for the iframe. */
      const next = this.pending.slice();
      let nextBytes = this.pendingBytes;
      for (let i = next.length - 1; i >= 0; i--) {
        const pending = next[i];
        const replace = key && pending.key === key;
        const supersededPage = key === 'page' &&
          (pending.message.type === 'page.delta' || pending.key === 'page');
        if (replace || supersededPage) {
          nextBytes -= pending.bytes;
          next.splice(i, 1);
        }
      }
      if (next.length >= MAX_PENDING || nextBytes + bytes > MAX_PENDING_BYTES)
        return false;
      next.push({ message, bytes, key });
      this.pending = next;
      this.pendingBytes = nextBytes + bytes;
      return true;
    }

    takeDeliveryLoss() {
      const lost = this.deliveryLost;
      this.deliveryLost = false;
      return lost;
    }

    normalizeRail(snapshot) {
      const rawEntries = Array.isArray(snapshot && snapshot.entries)
        ? snapshot.entries.slice(0, 33) : [];
      const entries = [];
      for (let i = 0; i < rawEntries.length; i++) {
        const raw = rawEntries[i] || {};
        const pluginIndex = integer(raw.pluginIndex, integer(raw.p, -1));
        const kind = integer(raw.kind, pluginIndex === -2 ? 1 : 2);
        if (!((kind === 1 && pluginIndex === -2) || (kind === 2 && pluginIndex >= 0)))
          continue;
        entries.push({
          kind,
          pluginIndex,
          preferredWidth: integer(raw.preferredWidth, integer(raw.pw, PANEL_DEFAULT)),
          title: boundedText(raw.title || 'Plugin', 63),
          iconAsset: boundedText(raw.iconAsset != null ? raw.iconAsset : raw.icon, 127),
          badge: boundedText(raw.badge, 15),
          attention: !!raw.attention
        });
      }
      return {
        protocol: PROTOCOL,
        type: 'rail.snapshot',
        registryRevision: unsigned(snapshot &&
          (snapshot.registryRevision != null ? snapshot.registryRevision : snapshot.r)),
        selectionGeneration: unsigned(snapshot &&
          (snapshot.selectionGeneration != null ? snapshot.selectionGeneration : snapshot.g)),
        pageGeneration: unsigned(snapshot &&
          (snapshot.pageGeneration != null ? snapshot.pageGeneration : snapshot.pg)),
        activePlugin: integer(snapshot &&
          (snapshot.activePlugin != null ? snapshot.activePlugin : snapshot.a), -1),
        lastSelectedPlugin: integer(snapshot &&
          (snapshot.lastSelectedPlugin != null ? snapshot.lastSelectedPlugin : snapshot.l), -1),
        selectedEntry: integer(snapshot &&
          (snapshot.selectedEntry != null ? snapshot.selectedEntry : snapshot.s), -1),
        expanded: !!(snapshot &&
          (snapshot.expanded != null ? snapshot.expanded : snapshot.x)),
        entries
      };
    }

    railSync(snapshot) {
      const next = this.normalizeRail(snapshot);
      const previous = this.rail;
      const oldGeneration = this.effectivePageGeneration();
      const nextGeneration = next.pageGeneration || next.selectionGeneration;
      if (this.pageOpen && oldGeneration && nextGeneration && oldGeneration !== nextGeneration) {
        this.send({ protocol: PROTOCOL, type: 'page.close', pageGeneration: oldGeneration });
        this.resetPage();
      }
      this.rail = next;
      this.updateLayout();
      if (this.send(next)) return true;
      this.rail = previous;
      this.updateLayout();
      return false;
    }

    receiveProtocol(message) {
      if (!message || message.protocol !== PROTOCOL || typeof message.type !== 'string')
        return false;
      if (message.type === 'rail.snapshot') return this.railSync(message);
      if (message.type === 'page.snapshot') {
        const generation = unsigned(message.pageGeneration);
        const panel = integer(message.panel, -1);
        if (!generation || panel < 0) return false;
        if (!this.send(message)) return false;
        this.pageOpen = true;
        this.pageGeneration = generation;
        this.pagePanel = panel;
        this.title = boundedText(message.title || 'Plugins', 191);
        this.checkStyle = integer(message.checkStyle, this.checkStyle);
      } else if (message.type === 'page.close') {
        const generation = unsigned(message.pageGeneration);
        if (generation && this.pageGeneration && generation !== this.pageGeneration)
          return this.send(message);
        const sent = this.send(message);
        if (sent) this.resetPage();
        return sent;
      }
      return message.type === 'page.snapshot' ? true : this.send(message);
    }

    railIcon(pluginIndex, revision, width, height, rgbaBase64) {
      const envelope = {
        protocol: PROTOCOL,
        type: 'rail.icon',
        pluginIndex: integer(pluginIndex, -1),
        revision: unsigned(revision),
        width: integer(width, 0),
        height: integer(height, 0),
        rgbaBase64: boundedText(rgbaBase64, 64 * 64 * 6)
      };
      if (envelope.pluginIndex < 0 || !envelope.revision || envelope.width < 0 ||
          envelope.height < 0 || envelope.width > 64 || envelope.height > 64)
        return false;
      return this.send(envelope);
    }

    effectivePageGeneration() {
      return this.rail.pageGeneration || this.rail.selectionGeneration || this.pageGeneration;
    }

    open() {
      if (!this.ensureFrame()) return false;
      this.executorOpen = true;
      this.updateLayout();
      return true;
    }

    resetPage() {
      this.batch = [];
      this.collecting = false;
      this.batchOverflow = false;
      this.pageOpen = false;
      this.pagePanel = -1;
      this.pageGeneration = 0;
      this.title = 'Plugins';
      this.customRevisions = {};
      this.widgetSerials = {};
      /* Intents belong to the page generation that authored them. Keeping a
       * queued result across replacement lets a recycled handle in the next
       * page receive it before the C-side fence gets a chance to object. */
      this.intents = [];
    }

    updateWidgetIdentities(commands, generation, reset) {
      if (reset) this.widgetSerials = {};
      for (let i = 0; i < commands.length; i++) {
        const command = commands[i];
        if (command.w < 0) continue;
        const key = String(command.w);
        if (command.k === CMD.WIDGET_ADD) {
          this.widgetSerials[key] = {
            panel: command.p,
            generation: unsigned(generation),
            serial: unsigned(command.s)
          };
        } else if (command.k === CMD.WIDGET_REMOVE) {
          delete this.widgetSerials[key];
        }
      }
    }

    end() {
      const generation = this.pageGeneration || this.effectivePageGeneration();
      if (this.pageOpen && generation)
        this.send({ protocol: PROTOCOL, type: 'page.close', pageGeneration: generation });
      this.executorOpen = false;
      this.resetPage();
      /* Executor shutdown represents a collapse. Keep the authoritative rail
       * buttons and the sole iframe, but release the page allocation now. */
      if (this.rail.expanded) {
        this.rail = copy(this.rail);
        this.rail.expanded = false;
        this.send(this.rail);
      }
      this.updateLayout();
      this.restoreGameFocus();
    }

    normalizeCommand(raw) {
      return {
        k: integer(raw && raw.k, 0), p: integer(raw && raw.p, -1),
        w: integer(raw && raw.w, -1), tab: integer(raw && raw.tab, -1),
        v: integer(raw && raw.v, 0), c: unsigned(raw && raw.c),
        x: integer(raw && raw.x, 0), y: integer(raw && raw.y, 0),
        cw: integer(raw && raw.cw, 0), ch: integer(raw && raw.ch, 0),
        label: boundedText(raw && raw.label, 63), text: boundedText(raw && raw.text, 191),
        detail: boundedText(raw && raw.detail, 191),
        s: unsigned(raw && raw.s)
      };
    }

    apply(raw) {
      const command = this.normalizeCommand(raw);
      if (command.k === CMD.SYNC_BEGIN) {
        this.collecting = true;
        this.batch = [];
        this.batchOverflow = false;
        return true;
      }
      if (command.k === CMD.SYNC_END) {
        const delivered = this.collecting && !this.batchOverflow
          ? this.commit(this.batch) : false;
        this.collecting = false;
        this.batch = [];
        this.batchOverflow = false;
        return delivered;
      }
      if (this.collecting) {
        if (!this.batchOverflow && this.batch.length < MAX_BATCH_COMMANDS - 2) {
          this.batch.push(command);
          return true;
        }
        else {
          this.batch = [];
          this.batchOverflow = true;
          return false;
        }
      }
      return this.commit([command]);
    }

    commit(commands) {
      if (!commands || !commands.length) return false;
      const generation = this.effectivePageGeneration();
      if (!generation) return false;
      let containsOpen = false;
      let containsClose = false;
      let panel = this.pagePanel;
      let title = this.title;
      let checkStyle = this.checkStyle;
      for (let i = 0; i < commands.length; i++) {
        const command = commands[i];
        if (command.k === CMD.CHECK_STYLE) checkStyle = command.v;
        else if (command.k === CMD.PANEL_OPEN) {
          containsOpen = true;
          panel = command.p;
          title = command.text || title;
        } else if (command.k === CMD.PANEL_TITLE && command.p === panel) {
          title = command.text || title;
        } else if (command.k === CMD.PANEL_CLOSE && command.p === panel) {
          containsClose = true;
        }
      }

      const snapshot = containsOpen || !this.pageOpen ||
        generation !== this.pageGeneration || panel !== this.pagePanel;
      if (snapshot) {
        if (panel < 0) return false;
        const initial = [];
        for (let i = 0; i < commands.length; i++)
          if (commands[i].k !== CMD.PANEL_CLOSE) initial.push(commands[i]);
        if (!this.send({
          protocol: PROTOCOL,
          type: 'page.snapshot',
          pageGeneration: generation,
          panel,
          title: title || 'Plugins',
          checkStyle,
          commands: initial
        })) return false;
        this.updateWidgetIdentities(initial, generation, true);
        this.pageOpen = true;
        this.pagePanel = panel;
        this.pageGeneration = generation;
        this.title = title || 'Plugins';
        this.checkStyle = checkStyle;
        return true;
      }

      if (containsClose) {
        if (!this.send({ protocol: PROTOCOL, type: 'page.close', pageGeneration: generation }))
          return false;
        this.resetPage();
        this.checkStyle = checkStyle;
        return true;
      }
      if (!this.send({
        protocol: PROTOCOL,
        type: 'page.delta',
        pageGeneration: generation,
        commands
      })) return false;
      this.updateWidgetIdentities(commands, generation, false);
      this.title = title;
      this.checkStyle = checkStyle;
      return true;
    }

    argbToRgbaBase64(argb, width, height) {
      const count = width * height;
      if (!argb || count <= 0 || argb.length < count ||
          !this.global.Uint8Array || typeof this.global.btoa !== 'function') return '';
      const bytes = new this.global.Uint8Array(count * 4);
      for (let i = 0; i < count; i++) {
        const pixel = Number(argb[i]) >>> 0;
        const at = i * 4;
        bytes[at] = (pixel >>> 16) & 255;
        bytes[at + 1] = (pixel >>> 8) & 255;
        bytes[at + 2] = pixel & 255;
        bytes[at + 3] = (pixel >>> 24) & 255;
      }
      let binary = '';
      for (let at = 0; at < bytes.length; at += 32768)
        binary += String.fromCharCode.apply(null, bytes.subarray(
          at, Math.min(bytes.length, at + 32768)));
      return this.global.btoa(binary);
    }

    customFrame(panel, widget, generation, serial, scaleMilli, width, height, argb) {
      panel = integer(panel, -1);
      widget = integer(widget, -1);
      generation = unsigned(generation);
      serial = unsigned(serial);
      width = integer(width, 0);
      height = integer(height, 0);
      scaleMilli = integer(scaleMilli, 0);
      if (panel < 0 || widget < 0 || !generation || !serial || scaleMilli <= 0 ||
          width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
          width * height > MAX_CUSTOM_PIXELS) return false;
      const rgbaBase64 = this.argbToRgbaBase64(argb, width, height);
      if (!rgbaBase64) return false;
      const key = `${generation}:${serial}`;
      let revision = (unsigned(this.customRevisions[key]) + 1) >>> 0;
      if (!revision) revision = 1;
      this.customRevisions[key] = revision;
      return this.send({
        protocol: PROTOCOL,
        type: 'custom.bitmap',
        pageGeneration: generation,
        panel,
        widget,
        widgetSerial: serial,
        revision,
        scaleMilli,
        width,
        height,
        rgbaBase64
      });
    }

    acceptSequence(message) {
      const sequence = unsigned(message && message.sequence);
      if (!sequence) return false;
      if (this.lastOutboundSequence &&
          ((sequence - this.lastOutboundSequence) | 0) <= 0) return false;
      this.lastOutboundSequence = sequence;
      return true;
    }

    fromRuntime(raw) {
      let message = raw;
      if (typeof raw === 'string') {
        try { message = JSON.parse(raw); }
        catch (error) { return false; }
      }
      if (!message || message.protocol !== PROTOCOL || !this.acceptSequence(message)) return false;
      if (message.type === 'widget.intent') {
        const intent = message.intent || {};
        const normalized = {
          k: integer(intent.k, 0), p: integer(intent.p, -1),
          w: integer(intent.w, -1), v: integer(intent.v, 0),
          text: boundedText(intent.text, 191), x: integer(intent.x, 0),
          y: integer(intent.y, 0), g: unsigned(intent.g), s: unsigned(intent.s)
        };
        if (!this.pageOpen || normalized.k < INTENT.ACTIVATE ||
            normalized.k > INTENT.CUSTOM_ACTIVATE ||
            normalized.g !== this.pageGeneration || normalized.p !== this.pagePanel)
          return false;
        if (normalized.k === INTENT.CLOSE) {
          if (normalized.w !== -1) return false;
        } else {
          const expected = normalized.w >= 0 && this.widgetSerials[String(normalized.w)];
          if (!expected || expected.panel !== normalized.p ||
              expected.generation !== normalized.g || expected.serial !== normalized.s)
            return false;
        }
        if (this.intents.length >= MAX_INTENTS) this.intents.shift();
        this.intents.push(normalized);
        return true;
      }
      if (message.type === 'rail.select') {
        const plugin = integer(message.pluginIndex, -1);
        const generation = unsigned(message.selectionGeneration);
        if (generation !== this.rail.selectionGeneration || plugin < -2 || plugin === -1)
          return false;
        const exported = this.global.Module &&
          this.global.Module._ToriRSChromeExecWeb_RequestSelect;
        const fallback = this.global.torirsChromeRequestSelect;
        try {
          if (typeof exported === 'function') exported(plugin, generation);
          else if (typeof fallback === 'function') fallback(plugin, generation);
          else return false;
        } catch (error) { return false; }
        return true;
      }
      if (message.type === 'layout') return this.publishLayout(message);
      if (message.type === 'editor.focus') {
        if (message.focused && unsigned(message.pageGeneration) !== this.pageGeneration)
          return false;
        this.editorFocused = !!message.focused;
        if (typeof this.global.torirsPluginChromeEditorFocus === 'function')
          this.global.torirsPluginChromeEditorFocus(this.editorFocused, unsigned(message.pageGeneration));
        return true;
      }
      return false;
    }

    takeIntent() { return this.intents.length ? JSON.stringify(this.intents.shift()) : ''; }

    preferredWidth() {
      let width = PANEL_DEFAULT;
      for (let i = 0; i < this.rail.entries.length; i++) {
        if (this.rail.entries[i].pluginIndex === this.rail.selectedEntry) {
          width = this.rail.entries[i].preferredWidth;
          break;
        }
      }
      return Math.max(PANEL_MIN, Math.min(PANEL_MAX, integer(width, PANEL_DEFAULT)));
    }

    availableWidth() {
      const root = this.document && (this.document.getElementById('torirs-app') ||
        this.document.getElementById('app-content'));
      let width = root && root.clientWidth;
      if ((!width || width <= 0) && root && root.getBoundingClientRect)
        width = root.getBoundingClientRect().width;
      if ((!width || width <= 0) && this.global) width = this.global.innerWidth;
      return width > 0 ? width : Number.POSITIVE_INFINITY;
    }

    updateLayout() {
      if (!this.mount) return;
      const layout = this.document.getElementById && this.document.getElementById('app-content');
      const app = this.document.getElementById && this.document.getElementById('torirs-app');
      const game = this.document.getElementById && this.document.getElementById('game-region');
      const hasRail = this.rail.entries.length > 0;
      const expanded = hasRail && this.rail.expanded;
      const paneWidth = this.preferredWidth();
      let mode = 'closed';
      if (expanded)
        mode = this.availableWidth() >= GAME_MIN + RAIL_WIDTH + PANEL_MIN
          ? 'split' : 'exclusive';
      else if (hasRail) mode = 'collapsed';
      this.layoutMode = mode;
      classSet(layout, 'torirs-chrome-split', mode === 'split');
      classSet(layout, 'torirs-chrome-exclusive', mode === 'exclusive');
      classSet(layout, 'torirs-chrome-collapsed', mode === 'collapsed');
      classSet(app, 'torirs-chrome-exclusive', mode === 'exclusive');
      this.mount.hidden = !hasRail;
      if (game) game.hidden = mode === 'exclusive';
      if (this.mount.style) {
        const total = RAIL_WIDTH + paneWidth;
        this.mount.style.width = mode === 'split' ? `${total}px`
          : (mode === 'exclusive' ? '100%' : (mode === 'collapsed' ? `${RAIL_WIDTH}px` : '0px'));
        this.mount.style.flex = mode === 'split' ? `0 0 ${total}px`
          : (mode === 'exclusive' ? '1 1 100%'
            : (mode === 'collapsed' ? `0 0 ${RAIL_WIDTH}px` : '0 0 0px'));
      }
      const html = this.document.documentElement || this.document.body;
      if (html && html.style && html.style.setProperty)
        html.style.setProperty('--torirs-dock-width',
          mode === 'split' ? `${RAIL_WIDTH + paneWidth}px`
            : (mode === 'collapsed' ? `${RAIL_WIDTH}px` : '0px'));
    }

    publishLayout(message) {
      const generation = unsigned(message.selectionGeneration);
      if (!generation || generation !== this.rail.selectionGeneration) return false;
      const width = Math.max(0, integer(message.width, 0));
      const height = Math.max(0, integer(message.height, 0));
      const scale = Math.max(1, integer(message.scaleMilli, 1000));
      const sizeClass = integer(message.sizeClass, 0);
      const visible = message.visible ? 1 : 0;
      const gameVisible = this.layoutMode === 'exclusive' ? 0 : 1;
      const key = [generation, width, height, scale, sizeClass, visible, gameVisible].join(':');
      if (key === this.lastLayoutKey) return true;
      this.lastLayoutKey = key;
      const exported = this.global.Module &&
        this.global.Module._ToriRSChromeExecWeb_RequestLayout;
      const fallback = this.global.torirsChromeRequestLayout;
      try {
        if (typeof exported === 'function')
          exported(generation, width, height, scale, sizeClass, visible, gameVisible);
        else if (typeof fallback === 'function')
          fallback(generation, width, height, scale, sizeClass, visible, gameVisible);
        else return false;
      } catch (error) { return false; }
      return true;
    }

    restoreGameFocus() {
      const canvas = this.document.getElementById && this.document.getElementById('canvas');
      if (!canvas || typeof canvas.focus !== 'function') return;
      try { canvas.focus({ preventScroll: true }); }
      catch (error) { canvas.focus(); }
    }
  }

  function install(host) {
    global.torirsChromeOpen = () => host.open();
    global.torirsChromeEnd = () => { host.end(); };
    global.torirsChromeClose = () => { host.end(); };
    global.torirsChromeApply = command => { host.apply(command); };
    /* Wasm sends one retained transaction per call. Keeping the legacy single
     * command hook makes fixtures readable, while production pays one bridge
     * crossing and one JSON parse for the complete delta. */
    global.torirsChromeApplyBatch = commands => {
      if (!Array.isArray(commands)) return false;
      if (host.takeDeliveryLoss()) return false;
      let accepted = true;
      for (let i = 0; i < commands.length; i++)
        if (host.apply(commands[i]) === false) accepted = false;
      return accepted;
    };
    global.torirsChromeTakeDeliveryLoss = () => host.takeDeliveryLoss();
    global.torirsChromeTakeIntent = () => host.takeIntent();
    global.torirsChromeRailSync = snapshot => host.railSync(snapshot);
    global.torirsChromeRailIcon = (plugin, revision, width, height, rgbaBase64) =>
      host.railIcon(plugin, revision, width, height, rgbaBase64);
    global.torirsChromeCustom = (panel, widget, generation, serial, scaleMilli,
      width, height, argb) =>
      host.customFrame(panel, widget, generation, serial, scaleMilli, width, height, argb);
    /* Canonical protocol entry point for new wasm hooks and browser tests. */
    global.torirsChromeReceive = envelope => {
      let message = envelope;
      if (typeof envelope === 'string') {
        try { message = JSON.parse(envelope); }
        catch (error) { return false; }
      }
      return host.receiveProtocol(message);
    };
  }

  const exported = { PluginChromeHost, install, PROTOCOL, CMD, W, INTENT, THEME };
  if (typeof module !== 'undefined' && module.exports) module.exports = exported;
  if (global && global.document) {
    const host = new PluginChromeHost(global, global.document);
    global.torirsPluginChromeHost = host;
    install(host);
  }
})(typeof window !== 'undefined' ? window : globalThis);
