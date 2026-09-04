(function (global, factory) {
    'use strict';
    var exported = { createRuntime: factory, protocol: 1 };
    if (typeof module !== 'undefined' && module.exports)
        module.exports = exported;
    if (!global || !global.document)
        return;
    var runtime = factory(global, global.document);
    if (runtime)
        global.ToriRSPluginChrome = runtime;
})(typeof window !== 'undefined' ? window :
    (typeof global !== 'undefined' ? global : this), function createRuntime(global, document) {
    'use strict';
    var PROTOCOL = 1;
    var MAX_MESSAGES = 64;
    var MAX_ENTRIES = 33;
    /* Must equal TORIRS_CHROME_OPTION_MAX: the model and both browser runtimes
     * accept the same bounded list rather than truncating at presentation. */
    var MAX_OPTIONS = 512;
    var MAX_PAGE_COMMANDS = 8190; /* 8192 including SYNC_BEGIN/SYNC_END. */
    var MAX_TEXT = 191;
    var Codec = global.ToriRSPluginChromeCodec;
    if (!Codec)
        return null;
    var CMD = {
        SYNC_BEGIN: 1, SYNC_END: 2,
        PANEL_OPEN: 3, PANEL_CLOSE: 4, PANEL_TITLE: 5, PANEL_RECT: 6, PANEL_TAB: 7,
        WIDGET_ADD: 8, WIDGET_REMOVE: 9, WIDGET_LABEL: 10, WIDGET_TEXT: 11,
        WIDGET_CHECKED: 12, WIDGET_HIDDEN: 13, WIDGET_COLOR: 14,
        WIDGET_SELECTED: 15, WIDGET_FOCUS: 16, WIDGET_OPTIONS: 17,
        WIDGET_OPTION: 18, CHECK_STYLE: 19, WIDGET_HEIGHT: 20
    };
    var W = {
        LABEL: 0, CHECKBOX: 1, TEXTINPUT: 2, SEPARATOR: 3, MENUITEM: 4,
        DROPDOWN: 5, MODELVIEW: 6, BUTTON: 7, TABSTRIP: 8, LISTROW: 9,
        COLORPICK: 10, TEXTAREA: 11, CUSTOM: 12, FREE: 13
    };
    var INTENT = {
        ACTIVATE: 1, ACTION: 2, TOGGLE: 3, TEXT: 4, PICK: 5, TAB: 6,
        CLOSE: 7, CUSTOM_ACTIVATE: 8
    };
    var ROW_ACTION = 0x1;
    var ROW_LOCKED = 0x2;
    var RENDER = {
        FULL: 1, VISIBILITY: 2, OPTIONS: 4, SELECTION: 8, FOCUS: 16, CONTENT: 32
    };
    /* The XP runtime has no dependable keyed-collection implementation.
     * Handles are small integers, so a prefixed own-property store is enough. */
    function Store() {
        this.values = {};
        this.count = 0;
    }
    Store.prototype.key = function (key) { return "$".concat(key); };
    Store.prototype.get = function (key) { return this.values[this.key(key)]; };
    Store.prototype.set = function (key, value) {
        var name = this.key(key);
        if (!Object.prototype.hasOwnProperty.call(this.values, name))
            this.count++;
        this.values[name] = value;
    };
    Store.prototype.drop = function (key) {
        var name = this.key(key);
        if (!Object.prototype.hasOwnProperty.call(this.values, name))
            return;
        delete this.values[name];
        this.count--;
    };
    Store.prototype.clear = function () { this.values = {}; this.count = 0; };
    Store.prototype.each = function (fn) {
        for (var name_1 in this.values)
            if (Object.prototype.hasOwnProperty.call(this.values, name_1))
                fn(this.values[name_1]);
    };
    var shell = document.getElementById('tpc-shell');
    var pane = document.getElementById('tpc-pane');
    var title = document.getElementById('tpc-title');
    var close = document.getElementById('tpc-close');
    var tabs = document.getElementById('tpc-tabs');
    var content = document.getElementById('tpc-content');
    var rail = document.getElementById('tpc-rail-list');
    /* The rail's own framed box, which the list lives inside. Only the legacy
     * page hands the two an explicit height; on the modern page the grid does. */
    var railBox = document.getElementById('tpc-rail') || (rail && rail.parentNode);
    var status = document.getElementById('tpc-status');
    if (!shell || !pane || !title || !close || !tabs || !content || !rail)
        return null;
    var legacy = shell.getAttribute && shell.getAttribute('data-tpc-legacy') === '1';
    var state = {
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
    function text(value, maximum) {
        if (maximum === void 0) { maximum = MAX_TEXT; }
        return String(value == null ? '' : value).slice(0, maximum);
    }
    function array(value) {
        return Object.prototype.toString.call(value) === '[object Array]';
    }
    function trimmed(value) {
        return text(value, 4096).replace(/^\s+|\s+$/g, '');
    }
    function bind(node, kind, handler) {
        node["on".concat(kind)] = handler;
    }
    function hasClass(node, name) {
        return new RegExp("(^|\\s)".concat(name, "(\\s|$)")).test(node.className || '');
    }
    function toggleClass(node, name, enabled) {
        var current = node.className || '';
        if (enabled && !hasClass(node, name))
            node.className = trimmed("".concat(current, " ").concat(name));
        else if (!enabled && hasClass(node, name))
            node.className = trimmed(current.replace(new RegExp("(^|\\s)".concat(name, "(?=\\s|$)"), 'g'), ' '));
    }
    function hidden(node, value) {
        node.hidden = !!value;
        node.style.display = value ? 'none' : '';
    }
    function clear(node) {
        while (node.firstChild)
            node.removeChild(node.firstChild);
    }
    function setText(node, value) {
        if (node)
            node.innerText = text(value, 4096);
    }
    function nextSequence() {
        state.sequence = (state.sequence + 1) >>> 0;
        if (!state.sequence)
            state.sequence = 1;
        return state.sequence;
    }
    function safeUrl(value, allowBlob) {
        if (allowBlob === void 0) { allowBlob = true; }
        var url = trimmed(value);
        if (!url || /[\u0000-\u001f]/.test(url))
            return '';
        if (/^(?:\.\.?\/|[A-Za-z0-9_.-]+\/)/.test(url) && !/\.\./.test(url))
            return url;
        if (/^torirs:\/\//i.test(url))
            return url;
        if (/^https:\/\/torirs\.local(?:\/|$)/i.test(url))
            return url;
        if (!legacy && /^data:image\/(?:png|gif|bmp|webp);base64,[A-Za-z0-9+/=]+$/i.test(url))
            return url;
        if (!legacy && allowBlob && /^blob:/i.test(url))
            return url;
        return '';
    }
    function cssUrl(url) {
        return url ? "url(\"".concat(url.replace(/["\\]/g, '\\$&'), "\")") : 'none';
    }
    function skinButton(button) {
        if (!button || legacy)
            return;
        var left = safeUrl(state.theme.buttonLeft);
        var middle = safeUrl(state.theme.buttonMiddle);
        var right = safeUrl(state.theme.buttonRight);
        if (left && middle && right) {
            button.style.backgroundImage = "".concat(cssUrl(left), ",").concat(cssUrl(right), ",").concat(cssUrl(middle));
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
        }
        else if (middle) {
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
        if (!control)
            return;
        control.checked = checked;
        if (legacy)
            return;
        var square = state.checkStyle === 1;
        var on = safeUrl(square ? state.theme.checkBoxOn : state.theme.checkOn) ||
            (square ? 'skin/CheckBoxOn.png' : 'skin/CheckOn.png');
        var off = safeUrl(square ? state.theme.checkBoxOff : state.theme.checkOff) ||
            (square ? 'skin/CheckBoxOff.png' : 'skin/CheckOff.png');
        var side = square ? 18 : 17;
        control.style.width = "".concat(side, "px");
        control.style.height = "".concat(side, "px");
        control.style.minWidth = "".concat(side, "px");
        control.style.backgroundSize = "".concat(side, "px ").concat(side, "px");
        control.style.backgroundImage = cssUrl(checked ? on : off);
    }
    function skinField(control) {
        if (!control || legacy)
            return;
        var body = safeUrl(state.theme.dropdownBody) || 'skin/DropdownBody.png';
        var isSelect = String(control.tagName || '').toLowerCase() === 'select';
        var arrow = isSelect && (safeUrl(state.theme.scrollDown) || 'skin/ScrollDown.png');
        if (body && arrow) {
            control.style.backgroundImage = "".concat(cssUrl(arrow), ",").concat(cssUrl(body));
            control.style.backgroundPosition = 'right 2px center,left top';
            control.style.backgroundRepeat = 'no-repeat,repeat';
            control.style.backgroundSize = '14px 14px,auto';
        }
        else if (body) {
            control.style.backgroundImage = cssUrl(body);
            control.style.backgroundRepeat = 'repeat';
        }
    }
    function setTabsVisible(visible) {
        hidden(tabs, !visible);
        toggleClass(pane, 'tpc-no-tabs', !visible);
    }
    function sizeLegacyViewport() {
        if (!legacy)
            return;
        var doc = document.documentElement || {};
        var body = document.body || {};
        var height = doc.clientHeight || body.clientHeight || 0;
        if (height > 0) {
            shell.style.height = "".concat(height, "px");
            pane.style.height = "".concat(height, "px");
            /* The BOX gets the viewport height, and the list fills what is left
             * inside its frame. Sizing the list itself to the viewport hangs its
             * last entries under the bottom frame piece, where they are clipped. */
            if (railBox && railBox.style)
                railBox.style.height = "".concat(height, "px");
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
        var copy = Codec.parse(Codec.stringify(envelope));
        if (widgetIntent) {
            var raw = normalizeIntent(widgetIntent);
            if (typeof global.torirsChromeIntentPosted === 'function') {
                try {
                    var accepted = global.torirsChromeIntentPosted({
                        k: raw.k, p: raw.p, w: raw.w, v: raw.v, text: raw.text,
                        x: raw.x, y: raw.y, g: raw.g, s: raw.s
                    });
                    if (accepted !== false)
                        return true;
                }
                catch (error) { /* Host failure cannot break retained UI. */ }
            }
        }
        var encoded = Codec.stringify(copy);
        var post = typeof global.torirsPluginChromePostMessage === 'function'
            ? global.torirsPluginChromePostMessage : null;
        if (post) {
            try {
                if (post(encoded) === true)
                    return true;
            }
            catch (error) { /* Pull queue remains the fallback. */ }
        }
        if (state.messages.length >= MAX_MESSAGES) {
            state.messageOverflow = true;
            if (widgetIntent)
                state.intentOverflow = true;
            return false;
        }
        state.messages.push(copy);
        if (widgetIntent) {
            var raw = normalizeIntent(widgetIntent);
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
        var pluginIndex = integer(node && node._tpcPluginIndex, -1);
        var generation = unsigned(node && node._tpcSelectionGeneration);
        if (pluginIndex === -1 || generation === 0)
            return;
        queueEnvelope({
            protocol: PROTOCOL,
            type: 'rail.select',
            sequence: nextSequence(),
            pluginIndex: pluginIndex,
            selectionGeneration: generation
        });
    }
    function postWidget(record, kind, value, newText, x, y) {
        if (value === void 0) { value = 0; }
        if (newText === void 0) { newText = ''; }
        if (x === void 0) { x = 0; }
        if (y === void 0) { y = 0; }
        if (!record || state.widgets.get(record.handle) !== record ||
            state.pageGeneration !== record.generation || state.panel !== record.panel)
            return;
        var intent = normalizeIntent({
            k: kind, p: record.panel, w: record.handle, v: value, text: newText,
            x: x,
            y: y,
            g: record.generation, s: record.serial
        });
        queueEnvelope({
            protocol: PROTOCOL,
            type: 'widget.intent',
            sequence: nextSequence(),
            intent: intent
        }, intent);
    }
    function editable(node) {
        if (!node || !node.tagName)
            return false;
        var tag = String(node.tagName).toUpperCase();
        return tag === 'INPUT' || tag === 'SELECT' || tag === 'TEXTAREA';
    }
    function postEditorFocus(focused) {
        focused = !!focused;
        if (state.editorFocused === focused)
            return;
        state.editorFocused = focused;
        queueEnvelope({
            protocol: PROTOCOL,
            type: 'editor.focus',
            sequence: nextSequence(),
            focused: focused,
            pageGeneration: state.pageGeneration
        });
    }
    function decodeRgba(width, height, encoded) {
        var pixels = width * height;
        if (legacy || !global.atob || !global.Uint8ClampedArray || !global.ImageData ||
            width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
            pixels > 4194304)
            return '';
        try {
            var binary = global.atob(text(encoded, pixels * 6));
            if (binary.length !== pixels * 4)
                return '';
            var bytes = new global.Uint8ClampedArray(binary.length);
            for (var i = 0; i < binary.length; i++)
                bytes[i] = binary.charCodeAt(i) & 255;
            var canvas = document.createElement('canvas');
            canvas.width = width;
            canvas.height = height;
            var context = canvas.getContext && canvas.getContext('2d');
            if (!context)
                return '';
            context.putImageData(new global.ImageData(bytes, width, height), 0, 0);
            return canvas.toDataURL('image/png');
        }
        catch (error) {
            return '';
        }
    }
    function appendImage(parent, url, className, alt) {
        if (!url)
            return false;
        if (legacy && /\.png(?:[?#]|$)/i.test(url)) {
            var transparent = document.createElement('span');
            transparent.className = className;
            transparent.title = alt || '';
            transparent.style.display = 'block';
            transparent.style.filter = "progid:DXImageTransform.Microsoft.AlphaImageLoader(src='" +
                url.replace(/'/g, '%27') + "',sizingMethod='scale')";
            parent.appendChild(transparent);
            return true;
        }
        var image = document.createElement('img');
        image.className = className;
        image.alt = alt || '';
        image.src = url;
        parent.appendChild(image);
        return true;
    }
    function messageBitmapUrl(message) {
        var direct = safeUrl(message && message.url);
        if (direct)
            return direct;
        if (message && message.rgbaBase64)
            return decodeRgba(integer(message.width, 0), integer(message.height, 0), message.rgbaBase64);
        return '';
    }
    function fallbackIcon(entry) {
        var themed = safeUrl(state.theme.pluginIcon);
        if (themed)
            return themed;
        return '';
    }
    function renderRailButton(entry, button) {
        var selected = entry.pluginIndex === state.rail.selectedEntry;
        var expanded = selected && state.rail.expanded;
        var cached = state.icons.get(entry.pluginIndex);
        var iconUrl = cached && cached.url ? cached.url : fallbackIcon(entry);
        button._tpcPluginIndex = entry.pluginIndex;
        button._tpcSelectionGeneration = state.rail.selectionGeneration;
        button.title = entry.title;
        button.setAttribute('aria-label', entry.title +
            (entry.badge ? ", ".concat(entry.badge) : '') +
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
        }
        else {
            var fallback = document.createElement('span');
            fallback.className = 'tpc-rail-fallback';
            setText(fallback, trimmed(entry.title).slice(0, 1).toUpperCase() || 'P');
            button.appendChild(fallback);
        }
        if (entry.badge) {
            var badge = document.createElement('span');
            badge.className = 'tpc-badge';
            setText(badge, entry.badge);
            button.appendChild(badge);
        }
        if (entry.attention) {
            var attention = document.createElement('i');
            attention.className = 'tpc-attention-dot';
            attention.setAttribute('aria-hidden', 'true');
            button.appendChild(attention);
        }
    }
    function rebuildRail() {
        clear(rail);
        state.railNodes.clear();
        var _loop_1 = function (index) {
            var entry = state.rail.entries[index];
            var button = document.createElement('button');
            button.type = 'button';
            button.className = 'tpc-rail-entry';
            bind(button, 'click', function () { return postRailSelect(button); });
            state.railNodes.set(entry.pluginIndex, button);
            rail.appendChild(button);
            renderRailButton(entry, button);
        };
        for (var index = 0; index < state.rail.entries.length; index++) {
            _loop_1(index);
        }
    }
    function updateRailNodes() {
        for (var index = 0; index < state.rail.entries.length; index++) {
            var entry = state.rail.entries[index];
            var button = state.railNodes.get(entry.pluginIndex);
            if (button)
                renderRailButton(entry, button);
        }
    }
    function sanitizeRailEntry(input) {
        var kind = integer(input && input.kind, 0);
        var pluginIndex = integer(input && input.pluginIndex, -1);
        if ((kind !== 1 && kind !== 2) || (kind === 1 ? pluginIndex !== -2 : pluginIndex < 0))
            return null;
        return {
            kind: kind,
            pluginIndex: pluginIndex,
            preferredWidth: integer(input.preferredWidth, 320),
            title: text(input.title || (kind === 1 ? 'Manage Plugins' : 'Plugin'), 63),
            iconAsset: text(input.iconAsset, 63),
            badge: text(input.badge, 23),
            attention: !!input.attention
        };
    }
    function applyRailSnapshot(message) {
        var entries = [];
        var source = array(message.entries) ? message.entries : [];
        for (var i = 0; i < source.length && entries.length < MAX_ENTRIES; i++) {
            var entry = sanitizeRailEntry(source[i]);
            if (entry)
                entries.push(entry);
        }
        var revision = unsigned(message.registryRevision);
        var rebuild = revision !== state.rail.registryRevision ||
            entries.length !== state.rail.entries.length || state.railNodes.count !== entries.length;
        state.rail = {
            registryRevision: revision,
            selectionGeneration: unsigned(message.selectionGeneration),
            pageGeneration: unsigned(message.pageGeneration),
            activePlugin: integer(message.activePlugin, -1),
            lastSelectedPlugin: integer(message.lastSelectedPlugin, -1),
            selectedEntry: integer(message.selectedEntry, -1),
            expanded: !!message.expanded,
            entries: entries
        };
        toggleClass(shell, 'tpc-collapsed', !state.rail.expanded);
        if (rebuild)
            rebuildRail();
        else
            updateRailNodes();
        if (!state.rail.expanded)
            clearPage(false);
        reportLayout();
    }
    function applyRailIcon(message) {
        var pluginIndex = integer(message.pluginIndex, -1);
        var revision = unsigned(message.revision);
        var width = integer(message.width, 0);
        var height = integer(message.height, 0);
        if (pluginIndex < 0 || !revision || width < 0 || height < 0 || width > 64 || height > 64)
            return;
        var current = state.icons.get(pluginIndex);
        if (current && current.revision === revision)
            return;
        var url = width && height ? messageBitmapUrl(message) : '';
        state.icons.set(pluginIndex, { revision: revision, width: width, height: height, url: url });
        var entry = null;
        for (var i = 0; i < state.rail.entries.length; i++)
            if (state.rail.entries[i].pluginIndex === pluginIndex) {
                entry = state.rail.entries[i];
                break;
            }
        var button = state.railNodes.get(pluginIndex);
        if (entry && button)
            renderRailButton(entry, button);
    }
    function applyTheme(message) {
        var revision = unsigned(message.revision);
        if (!revision || revision === state.themeRevision)
            return;
        var assets = message.assets && typeof message.assets === 'object' ? message.assets : {};
        var allowed = [
            'panelBody', 'pluginIcon', 'buttonLeft', 'buttonMiddle', 'buttonRight',
            'checkOn', 'checkOff', 'checkBoxOn', 'checkBoxOff', 'dropdownBody',
            'scrollUp', 'scrollDown', 'scrollTrack', 'scrollGripTop', 'scrollGripMiddle',
            'scrollGripBottom', 'close', 'frameTopLeft', 'frameTop', 'frameTopRight',
            'frameLeft', 'frameRight', 'frameBottomLeft', 'frameBottom', 'frameBottomRight'
        ];
        var next = {};
        for (var i = 0; i < allowed.length; i++)
            next[allowed[i]] = safeUrl(assets[allowed[i]]);
        state.theme = next;
        state.themeRevision = revision;
        pane.style.backgroundImage = cssUrl(next.panelBody);
        if (rail.parentNode)
            rail.parentNode.style.backgroundImage = cssUrl(next.panelBody);
        close.style.backgroundImage = cssUrl(next.close);
        var frameIds = ['tl', 't', 'tr', 'l', 'r', 'bl', 'b', 'br'];
        var frameAssets = ['frameTopLeft', 'frameTop', 'frameTopRight', 'frameLeft',
            'frameRight', 'frameBottomLeft', 'frameBottom', 'frameBottomRight'];
        /* Two frames wear the same nine pieces: the page's panel, and the rail --
         * which is the gameframe popout strip's own 42px strip and has to read as
         * one of them, not as a flat column beside one. */
        var framePrefixes = ['tpc-frame-', 'tpc-railframe-'];
        for (var p = 0; p < framePrefixes.length; p++) {
            for (var i = 0; i < frameIds.length; i++) {
                var node = document.getElementById(framePrefixes[p] + frameIds[i]);
                if (node)
                    node.style.backgroundImage = cssUrl(next[frameAssets[i]]);
            }
        }
        updateRailNodes();
        state.widgets.each(function (record) { return queueWidgetRender(record, RENDER.CONTENT); });
    }
    function indexWidget(record) {
        if (record.tab >= 0) {
            var bucket = state.widgetsByTab.get(record.tab);
            if (!bucket) {
                bucket = new Store();
                state.widgetsByTab.set(record.tab, bucket);
            }
            bucket.set(record.handle, record);
        }
        if (record.kind === W.CHECKBOX || record.kind === W.LISTROW)
            state.checkWidgets.set(record.handle, record);
        if (record.kind === W.TABSTRIP)
            state.tabStrips.set(record.handle, record);
        if (record.kind === W.CUSTOM)
            state.customWidgets.set(record.handle, record);
    }
    function unindexWidget(record) {
        if (record.tab >= 0) {
            var bucket = state.widgetsByTab.get(record.tab);
            if (bucket) {
                bucket.drop(record.handle);
                if (!bucket.count)
                    state.widgetsByTab.drop(record.tab);
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
        if (!record || state.widgets.get(record.handle) !== record)
            return;
        record.renderFlags |= flags;
        if (!record.renderQueued) {
            record.renderQueued = true;
            state.renderQueue.push(record);
        }
        if (!state.applying)
            flushWidgetRenders();
    }
    function queueTabVisibility(tab) {
        var bucket = state.widgetsByTab.get(tab);
        if (bucket)
            bucket.each(function (record) { return queueWidgetRender(record, RENDER.VISIBILITY); });
    }
    function flushWidgetRenders() {
        var queued = state.renderQueue;
        state.renderQueue = [];
        for (var i = 0; i < queued.length; i++) {
            var record = queued[i];
            var flags = record.renderFlags;
            record.renderFlags = 0;
            record.renderQueued = false;
            if (state.widgets.get(record.handle) === record)
                renderWidgetParts(record, flags);
        }
    }
    function clearPage(report) {
        if (report === void 0) { report = true; }
        /* Every row the popup holds addresses a widget of the page being torn
         * down. postWidget would drop them, but a menu left standing over a
         * cleared pane is a menu about nothing. */
        popupHide();
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
        if (report)
            reportLayout();
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
            s: unsigned(input && input.s)
        };
    }
    function labelNode(labelValue) {
        var label = document.createElement('span');
        label.className = 'tpc-label';
        setText(label, labelValue);
        return label;
    }
    function attachLabel(row, record, control) {
        if (record.label)
            row.appendChild(labelNode(record.label));
        row.appendChild(control);
    }
    /** Resolve a pointer only inside a custom well's drawable content box. */
    function customContentPoint(custom, event) {
        var bounds = custom.getBoundingClientRect();
        var width = custom.clientWidth || 0;
        var height = custom.clientHeight || 0;
        var left = bounds.left + Math.max(0, integer(custom.clientLeft, 0));
        var top = bounds.top + Math.max(0, integer(custom.clientTop, 0));
        var x = event.clientX - left;
        var y = event.clientY - top;
        if (!width || !height || x < 0 || y < 0 || x >= width || y >= height)
            return null;
        return { x: x, y: y, width: width, height: height };
    }
    function createWidget(command) {
        if (command.p !== state.panel || command.w < 0 || command.v < 0 || command.v > W.FREE)
            return;
        removeWidget(command.w);
        var row = document.createElement('div');
        row.className = 'tpc-row';
        var record = {
            handle: command.w, panel: command.p, generation: state.pageGeneration,
            serial: command.s, kind: command.v, tab: command.tab, shape: command.cw,
            rows: command.ch, label: command.label, text: command.text, color: command.c,
            checked: false, selected: -1, hidden: false, focused: false,
            options: [], structuredOptions: false, optionsRevision: 0,
            customRevision: 0, customScale: 1000,
            renderFlags: 0, renderQueued: false,
            row: row,
            control: null
        };
        row._tpcRecord = record;
        switch (record.kind) {
            case W.LABEL: {
                var value = document.createElement('span');
                value.className = 'tpc-value';
                row.appendChild(value);
                record.control = value;
                break;
            }
            case W.CHECKBOX: {
                var box_1 = document.createElement('input');
                box_1.type = 'checkbox';
                box_1.className = 'tpc-check';
                bind(box_1, 'change', function () { return postWidget(record, INTENT.TOGGLE, box_1.checked ? 1 : 0); });
                row.appendChild(box_1);
                var caption = document.createElement('span');
                row.appendChild(caption);
                record.control = box_1;
                record.caption = caption;
                break;
            }
            case W.TEXTINPUT: {
                var input_1 = document.createElement('input');
                input_1.type = 'text';
                input_1.className = 'tpc-field tpc-text-field';
                input_1.maxLength = MAX_TEXT;
                bind(input_1, 'change', function () { return postWidget(record, INTENT.TEXT, 0, input_1.value); });
                attachLabel(row, record, input_1);
                record.control = input_1;
                break;
            }
            case W.TEXTAREA: {
                row.className += ' tpc-tall-row';
                var area_1 = document.createElement('textarea');
                area_1.className = 'tpc-field tpc-textarea';
                area_1.maxLength = MAX_TEXT;
                area_1.rows = record.rows > 0 ? Math.min(record.rows, 32) : 4;
                bind(area_1, 'change', function () { return postWidget(record, INTENT.TEXT, 0, area_1.value); });
                attachLabel(row, record, area_1);
                record.control = area_1;
                break;
            }
            case W.SEPARATOR: {
                row.className += ' tpc-separator';
                var line = document.createElement('span');
                line.className = 'tpc-separator-line';
                row.appendChild(line);
                break;
            }
            case W.MENUITEM:
            case W.BUTTON: {
                var button = document.createElement('button');
                button.type = 'button';
                button.className = record.kind === W.BUTTON ? 'tpc-button' : 'tpc-menu';
                bind(button, 'click', function () { return postWidget(record, INTENT.ACTIVATE); });
                row.appendChild(button);
                record.control = button;
                break;
            }
            case W.DROPDOWN: {
                var select_1 = document.createElement('select');
                select_1.className = 'tpc-field tpc-select';
                bind(select_1, 'change', function () {
                    var index = select_1.selectedIndex;
                    var option = index >= 0 && index < record.options.length
                        ? record.options[index] : null;
                    if (record.structuredOptions) {
                        if (!option || !option.enabled) {
                            renderWidgetSelection(record);
                            return;
                        }
                        postWidget(record, INTENT.PICK, index, option.value);
                    }
                    else
                        postWidget(record, INTENT.PICK, index);
                });
                attachLabel(row, record, select_1);
                record.control = select_1;
                break;
            }
            case W.MODELVIEW: {
                var model = document.createElement('div');
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
                var locked_1 = !!(record.shape & ROW_LOCKED);
                var action_1 = !!(record.shape & ROW_ACTION);
                var holder = document.createElement('table');
                holder.className = 'tpc-list-table';
                holder.setAttribute('role', 'presentation');
                holder.setAttribute('cellspacing', '0');
                holder.setAttribute('cellpadding', '0');
                var body = document.createElement('tbody');
                var line = document.createElement('tr');
                var nameCell = document.createElement('td');
                nameCell.className = 'tpc-name-cell';
                var name_2 = document.createElement('span');
                name_2.className = 'tpc-row-name';
                /* Same zones as the native chrome: everything left of the switch opens
                 * the row when it has an action (a locked row is all action zone), and
                 * a row with no action toggles from its name so no zone is inert. */
                bind(nameCell, 'click', function () {
                    if (locked_1 || action_1)
                        postWidget(record, INTENT.ACTION);
                    else if (record.toggle) {
                        record.toggle.checked = !record.toggle.checked;
                        postWidget(record, INTENT.TOGGLE, record.toggle.checked ? 1 : 0);
                    }
                });
                nameCell.appendChild(name_2);
                line.appendChild(nameCell);
                if (action_1) {
                    var wellCell = document.createElement('td');
                    wellCell.className = 'tpc-well-cell';
                    var well = document.createElement('button');
                    well.type = 'button';
                    well.className = 'tpc-row-well';
                    for (var dot = 0; dot < 3; dot++) {
                        var mark = document.createElement('i');
                        mark.className = "tpc-dot tpc-dot-".concat(dot);
                        well.appendChild(mark);
                    }
                    bind(well, 'click', function () { return postWidget(record, INTENT.ACTION); });
                    wellCell.appendChild(well);
                    line.appendChild(wellCell);
                    record.well = well;
                }
                if (!locked_1) {
                    var toggleCell = document.createElement('td');
                    toggleCell.className = 'tpc-toggle-cell';
                    var toggle_1 = document.createElement('input');
                    toggle_1.type = 'checkbox';
                    toggle_1.className = 'tpc-check';
                    bind(toggle_1, 'change', function () { return postWidget(record, INTENT.TOGGLE, toggle_1.checked ? 1 : 0); });
                    toggleCell.appendChild(toggle_1);
                    line.appendChild(toggleCell);
                    record.toggle = toggle_1;
                }
                body.appendChild(line);
                holder.appendChild(body);
                row.appendChild(holder);
                record.caption = name_2;
                record.control = holder;
                break;
            }
            case W.COLORPICK: {
                var holder = document.createElement('span');
                holder.className = 'tpc-color';
                var swatch_1 = document.createElement('input');
                /* MSHTML lacks the colour input used by current browser engines. */
                swatch_1.type = legacy ? 'text' : 'color';
                swatch_1.className = 'tpc-field tpc-color-swatch';
                var hex_1 = document.createElement('input');
                hex_1.type = 'text';
                hex_1.className = 'tpc-field tpc-color-text';
                hex_1.maxLength = 7;
                bind(swatch_1, 'change', function () {
                    hex_1.value = swatch_1.value;
                    postWidget(record, INTENT.TEXT, 0, swatch_1.value);
                });
                bind(hex_1, 'change', function () { return postWidget(record, INTENT.TEXT, 0, hex_1.value); });
                holder.appendChild(swatch_1);
                holder.appendChild(hex_1);
                attachLabel(row, record, holder);
                record.control = holder;
                record.swatch = swatch_1;
                record.hex = hex_1;
                break;
            }
            case W.CUSTOM: {
                row.className += ' tpc-tall-row';
                var custom_1 = document.createElement('div');
                custom_1.className = 'tpc-custom';
                custom_1.style.height = customHeightPx(record.rows);
                custom_1.tabIndex = 0;
                bind(custom_1, 'mousedown', function (event) {
                    record.pointer = customContentPoint(custom_1, event);
                });
                bind(custom_1, 'mouseup', function (event) {
                    if (!record.pointer)
                        return;
                    record.pointer = null;
                    var point = customContentPoint(custom_1, event);
                    if (!point)
                        return;
                    var px = Math.max(0, Math.min(record.bitmapWidth - 1, Math.floor(point.x * record.bitmapWidth / point.width)));
                    var py = Math.max(0, Math.min(record.bitmapHeight - 1, Math.floor(point.y * record.bitmapHeight / point.height)));
                    postWidget(record, INTENT.CUSTOM_ACTIVATE, 0, '', Math.floor(px * 1000 / Math.max(1, record.customScale)), Math.floor(py * 1000 / Math.max(1, record.customScale)));
                });
                bind(custom_1, 'keydown', function (event) {
                    var code = event.keyCode || event.which || 0;
                    if (code === 13 || code === 32)
                        postWidget(record, INTENT.CUSTOM_ACTIVATE);
                });
                if (record.label)
                    row.appendChild(labelNode(record.label));
                row.appendChild(custom_1);
                record.control = custom_1;
                break;
            }
            case W.FREE:
            default:
                hidden(row, true);
                break;
        }
        content.appendChild(row);
        state.widgets.set(record.handle, record);
        indexWidget(record);
        queueWidgetRender(record, RENDER.FULL);
    }
    function removeWidget(handle) {
        var record = state.widgets.get(handle);
        if (!record)
            return;
        if (record.row.parentNode)
            record.row.parentNode.removeChild(record.row);
        unindexWidget(record);
        if (record.kind === W.TABSTRIP && !state.tabStrips.count)
            setTabsVisible(false);
        state.widgets.drop(handle);
    }
    function renderOptions(record) {
        if (!record || !record.control)
            return;
        if (record.kind === W.DROPDOWN) {
            var select = record.control;
            clear(select);
            for (var index = 0; index < record.options.length; index++) {
                var item = record.options[index];
                var option = document.createElement('option');
                if (record.structuredOptions) {
                    var detail = item && item.detail ? " \u2014 ".concat(item.detail) : '';
                    setText(option, "".concat(item ? item.label : '').concat(detail));
                    option.value = item ? item.value : '';
                    option.disabled = !(item && item.enabled);
                    option.setAttribute('aria-disabled', option.disabled ? 'true' : 'false');
                    if (item && item.detail) {
                        option.title = item.detail;
                        option.setAttribute('aria-label', "".concat(item.label, ". ").concat(item.detail));
                    }
                }
                else
                    setText(option, item);
                select.appendChild(option);
            }
            if (record.selected >= 0 && record.selected < record.options.length)
                select.selectedIndex = record.selected;
        }
        else if (record.kind === W.TABSTRIP) {
            clear(tabs);
            var _loop_2 = function (index) {
                var button = document.createElement('button');
                button.type = 'button';
                setText(button, record.options[index]);
                button.setAttribute('aria-selected', index === state.activeTab ? 'true' : 'false');
                bind(button, 'click', function () { return postWidget(record, INTENT.TAB, index); });
                tabs.appendChild(button);
            };
            for (var index = 0; index < record.options.length; index++) {
                _loop_2(index);
            }
            setTabsVisible(record.options.length !== 0);
        }
    }
    function renderWidgetContent(record) {
        switch (record.kind) {
            case W.LABEL:
                setText(record.control, record.text || record.label);
                if (record.color) {
                    var raw = (record.color & 0xffffff).toString(16);
                    record.control.style.color = "#".concat('000000'.slice(raw.length)).concat(raw);
                }
                break;
            case W.CHECKBOX:
                skinCheck(record.control, !!record.checked);
                setText(record.caption, record.label || record.text);
                break;
            case W.TEXTINPUT:
            case W.TEXTAREA:
                skinField(record.control);
                if (record.control.value !== record.text)
                    record.control.value = record.text;
                break;
            case W.MENUITEM:
            case W.BUTTON:
                skinButton(record.control);
                setText(record.control, record.text || record.label);
                break;
            case W.DROPDOWN:
                skinField(record.control);
                break;
            case W.MODELVIEW:
                setText(record.control, record.label || record.text || 'Model preview');
                break;
            case W.LISTROW:
                setText(record.caption, record.label || record.text);
                if (record.well)
                    record.well.title = text(record.label || record.text, 63);
                if (record.toggle)
                    skinCheck(record.toggle, !!record.checked);
                break;
            case W.COLORPICK: {
                var value = /^#[0-9a-f]{6}$/i.test(record.text) ? record.text : '#000000';
                record.swatch.value = value;
                record.hex.value = record.text || value;
                break;
            }
            default:
                break;
        }
    }
    function renderWidget(record) {
        if (!record)
            return;
        renderWidgetContent(record);
        if (record.kind === W.DROPDOWN || record.kind === W.TABSTRIP)
            renderOptions(record);
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
            record.control.selectedIndex = record.selected >= 0 &&
                record.selected < record.options.length ? record.selected : -1;
    }
    function renderWidgetFocus(record) {
        if (record.focused && record.control && record.control.focus) {
            try {
                record.control.focus({ preventScroll: true });
            }
            catch (error) {
                record.control.focus();
            }
        }
    }
    function renderWidgetParts(record, flags) {
        state.renderVisits++;
        if (flags & RENDER.FULL) {
            renderWidget(record);
            return;
        }
        if (flags & RENDER.OPTIONS)
            renderOptions(record);
        if (flags & RENDER.SELECTION)
            renderWidgetSelection(record);
        if (flags & RENDER.VISIBILITY)
            renderWidgetVisibility(record);
        if (flags & RENDER.FOCUS)
            renderWidgetFocus(record);
        if (flags & RENDER.CONTENT)
            renderWidgetContent(record);
    }
    /* One clamp for a custom well's height, so the ADD that first states it
     * and the WIDGET_HEIGHT that moves it cannot disagree. */
    function customHeightPx(rows) {
        return "".concat(Math.max(48, Math.min(512, rows || 120)), "px");
    }
    function applyCommand(raw) {
        var command = normalizeCommand(raw);
        if (command.k === CMD.CHECK_STYLE) {
            state.checkStyle = command.v;
            toggleClass(shell, 'tpc-check-square', command.v === 1);
            state.checkWidgets.each(function (record) { return queueWidgetRender(record, RENDER.CONTENT); });
            return;
        }
        if (command.k === CMD.PANEL_OPEN) {
            if (state.panel < 0)
                state.panel = command.p;
            if (command.p === state.panel && command.text)
                setText(title, command.text);
            return;
        }
        if (command.p !== state.panel)
            return;
        var record = state.widgets.get(command.w);
        switch (command.k) {
            case CMD.PANEL_CLOSE:
                clearPage();
                break;
            case CMD.PANEL_TITLE:
                setText(title, command.text);
                break;
            case CMD.PANEL_TAB:
                if (state.activeTab !== command.v) {
                    var previous = state.activeTab;
                    state.activeTab = command.v;
                    queueTabVisibility(previous);
                    queueTabVisibility(state.activeTab);
                    state.tabStrips.each(function (tabstrip) {
                        return queueWidgetRender(tabstrip, RENDER.OPTIONS);
                    });
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
                    if (record.control)
                        record.control.style.height = customHeightPx(record.rows);
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
                    var count = Math.max(0, Math.min(MAX_OPTIONS, command.v));
                    record.structuredOptions = !!command.x;
                    record.options = new Array(count);
                    for (var i = 0; i < count; i++)
                        record.options[i] =
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
        var commands = array(message.commands) ? message.commands : [];
        if (commands.length > MAX_PAGE_COMMANDS)
            return null;
        for (var i = 0; i < commands.length; i++) {
            var command = commands[i] || {};
            var kind = integer(command.k, 0);
            var value = integer(command.v, 0);
            if (kind === CMD.WIDGET_OPTIONS && (value < 0 || value > MAX_OPTIONS))
                return null;
            if (kind === CMD.WIDGET_OPTION && (value < 0 || value >= MAX_OPTIONS))
                return null;
        }
        return commands;
    }
    function applyPageSnapshot(message) {
        var generation = unsigned(message.pageGeneration);
        var panel = integer(message.panel, -1);
        var commands = pageCommands(message);
        if (!generation || panel < 0 || !commands)
            return false;
        clearPage(false);
        state.pageGeneration = generation;
        state.panel = panel;
        state.activeTab = 0;
        state.checkStyle = integer(message.checkStyle, 0);
        setText(title, message.title || 'Plugins');
        hidden(pane, false);
        toggleClass(shell, 'tpc-collapsed', false);
        state.applying = true;
        try {
            for (var i = 0; i < commands.length; i++)
                applyCommand(commands[i]);
        }
        finally {
            state.applying = false;
        }
        flushWidgetRenders();
        reportLayout();
        return true;
    }
    function applyPageDelta(message) {
        var commands = pageCommands(message);
        if (!state.pageGeneration || unsigned(message.pageGeneration) !== state.pageGeneration ||
            !commands)
            return false;
        state.applying = true;
        try {
            for (var i = 0; i < commands.length; i++)
                applyCommand(commands[i]);
        }
        finally {
            state.applying = false;
        }
        flushWidgetRenders();
        /* A retained height/visibility/text change can make the content scrollbar
         * appear or disappear without resizing the outer window. That changes a
         * full-width custom well, so publish its post-layout content width too. */
        reportLayout();
        return true;
    }
    function applyCustomBitmap(message) {
        var generation = unsigned(message.pageGeneration);
        var panel = integer(message.panel, -1);
        var handle = integer(message.widget, -1);
        var serial = unsigned(message.widgetSerial);
        var revision = unsigned(message.revision);
        var width = integer(message.width, 0);
        var height = integer(message.height, 0);
        var record = state.widgets.get(handle);
        if (!record || record.kind !== W.CUSTOM || generation !== state.pageGeneration ||
            panel !== state.panel || record.panel !== panel || record.serial !== serial ||
            !revision || revision === record.customRevision || width <= 0 || height <= 0 ||
            width > 4096 || height > 4096)
            return;
        var url = messageBitmapUrl(message);
        if (!url)
            return;
        clear(record.control);
        if (!appendImage(record.control, url, '', record.label || ''))
            return;
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
        var visible = pane.style.display !== 'none' && !!state.pageGeneration;
        var width = visible ? Math.max(0, pane.clientWidth || 0) : 0;
        var height = visible ? Math.max(0, pane.clientHeight || 0) : 0;
        var customWidth = 0;
        if (visible)
            state.customWidgets.each(function (record) {
                /* CUSTOM is full-width in both browser styles. clientWidth is its
                 * content box: pane/content padding, the scrollbar and the well border
                 * have therefore already been removed by the browser's own layout. */
                if (!customWidth && record.kind === W.CUSTOM && !record.hidden && record.control)
                    customWidth = Math.max(0, integer(record.control.clientWidth, 0));
            });
        var scaleMilli = Math.max(1, Math.round(Number(global.devicePixelRatio || 1) * 1000));
        var sizeClass = width < 320 ? 0 : (width >= 480 ? 2 : 1);
        var key = [state.rail.selectionGeneration, state.pageGeneration, width, height,
            customWidth, scaleMilli, sizeClass, visible ? 1 : 0].join(':');
        if (key === state.lastLayout)
            return;
        state.lastLayout = key;
        queueEnvelope({
            protocol: PROTOCOL,
            type: 'layout',
            sequence: nextSequence(),
            selectionGeneration: state.rail.selectionGeneration,
            pageGeneration: state.pageGeneration,
            width: width,
            height: height,
            customWidth: customWidth,
            scaleMilli: scaleMilli,
            sizeClass: sizeClass,
            visible: visible,
            gameVisible: true
        });
    }
    function receive(input) {
        var message = input;
        if (typeof input === 'string') {
            try {
                message = Codec.parse(input);
            }
            catch (error) {
                return false;
            }
        }
        if (!message || typeof message !== 'object' || integer(message.protocol, 0) !== PROTOCOL)
            return false;
        switch (message.type) {
            case 'rail.snapshot':
                applyRailSnapshot(message);
                break;
            case 'rail.icon':
                applyRailIcon(message);
                break;
            case 'theme':
                applyTheme(message);
                break;
            case 'page.snapshot': return applyPageSnapshot(message);
            case 'page.delta': return applyPageDelta(message);
            case 'page.close':
                if (!message.pageGeneration || unsigned(message.pageGeneration) === state.pageGeneration)
                    clearPage();
                break;
            case 'custom.bitmap':
                applyCustomBitmap(message);
                break;
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
        var overflow = state.intentOverflow;
        state.intentOverflow = false;
        return overflow;
    }
    function postPanelClose() {
        if (state.panel < 0 || !state.pageGeneration)
            return;
        var intent = normalizeIntent({
            k: INTENT.CLOSE, p: state.panel, w: -1,
            g: state.pageGeneration, s: 0
        });
        queueEnvelope({
            protocol: PROTOCOL, type: 'widget.intent', sequence: nextSequence(),
            intent: intent
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
    var MINIMENU_CLICK_BIAS = 11;
    var popup = { back: null, box: null, keydown: null };
    function insideNode(node, ancestor) {
        for (var at = node; at; at = at.parentNode)
            if (at === ancestor)
                return true;
        return false;
    }
    function popupHost() {
        return document.body || shell.parentNode || shell;
    }
    function popupHide() {
        var back = popup.back;
        if (!back)
            return;
        popup.back = null;
        popup.box = null;
        if (back.parentNode)
            back.parentNode.removeChild(back);
        document.onkeydown = popup.keydown || null;
        popup.keydown = null;
    }
    function popupKeyDown(event) {
        event = event || global.event || {};
        if ((event.keyCode || event.which || 0) !== 27) {
            if (typeof popup.keydown === 'function')
                return popup.keydown(event);
            return undefined;
        }
        popupHide();
        return false;
    }
    function addRow(rows, label, run) {
        rows.push({ label: text(label, 63), run: run });
    }
    function cancelEvent(event) {
        if (event.preventDefault)
            event.preventDefault();
        event.returnValue = false;
        return false;
    }
    /* The editable control a click landed in, or null. A SELECT is editable to
     * the focus reporter and not to this menu: its own list is the popup. */
    function fieldOf(node) {
        if (!node || !node.tagName)
            return null;
        var tag = String(node.tagName).toUpperCase();
        if (tag === 'TEXTAREA')
            return node;
        if (tag === 'INPUT' && node.type !== 'checkbox' && node.type !== 'color')
            return node;
        return null;
    }
    function railRows(rows, button) {
        var pluginIndex = integer(button._tpcPluginIndex, -1);
        var open = pluginIndex === state.rail.selectedEntry && state.rail.expanded;
        addRow(rows, open ? 'Hide' : 'Open', function () { return postRailSelect(button); });
    }
    function toggleRow(rows, record, box) {
        addRow(rows, record.checked ? 'Turn off' : 'Turn on', function () {
            box.checked = !record.checked;
            postWidget(record, INTENT.TOGGLE, box.checked ? 1 : 0);
        });
    }
    function recordRows(rows, record) {
        var caption = trimmed(record.text || record.label);
        switch (record.kind) {
            case W.MENUITEM:
            case W.BUTTON:
                addRow(rows, caption || 'Select', function () { return postWidget(record, INTENT.ACTIVATE); });
                break;
            case W.CHECKBOX:
                toggleRow(rows, record, record.control);
                break;
            case W.LISTROW:
                if (record.shape & (ROW_ACTION | ROW_LOCKED))
                    addRow(rows, 'Settings', function () { return postWidget(record, INTENT.ACTION); });
                if (record.toggle)
                    toggleRow(rows, record, record.toggle);
                break;
            case W.CUSTOM:
                addRow(rows, caption || 'Select', function () { return postWidget(record, INTENT.CUSTOM_ACTIVATE); });
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
        var selection = typeof field.selectionStart === 'number' &&
            typeof field.selectionEnd === 'number'
            ? field.selectionEnd > field.selectionStart : true;
        var command = function (name) {
            if (field.focus)
                field.focus();
            try {
                document.execCommand(name);
            }
            catch (error) { /* An engine that refuses the edit leaves the field alone. */ }
        };
        if (selection) {
            addRow(rows, 'Copy', function () { return command('copy'); });
            addRow(rows, 'Cut', function () { return command('cut'); });
        }
        addRow(rows, 'Select all', function () {
            if (field.focus)
                field.focus();
            if (field.select)
                field.select();
        });
    }
    function popupRowsFor(target) {
        var rows = [];
        var host = popupHost();
        var record = null;
        var railButton = null;
        var field = null;
        for (var node = target; node && node !== host; node = node.parentNode) {
            if (!field)
                field = fieldOf(node);
            if (!record && node._tpcRecord)
                record = node._tpcRecord;
            if (!railButton && integer(node._tpcPluginIndex, -1) !== -1)
                railButton = node;
        }
        if (railButton)
            railRows(rows, railButton);
        else if (record)
            recordRows(rows, record);
        if (field)
            fieldRows(rows, field);
        if (!railButton && state.panel >= 0 && state.pageGeneration)
            addRow(rows, 'Close', postPanelClose);
        addRow(rows, 'Cancel', popupHide);
        return rows;
    }
    function popupPlace(x, y) {
        var doc = document.documentElement || {};
        var viewWidth = integer(doc.clientWidth, 0) || integer(global.innerWidth, 0);
        var viewHeight = integer(doc.clientHeight, 0) || integer(global.innerHeight, 0);
        var width = integer(popup.box.offsetWidth, 0);
        var height = integer(popup.box.offsetHeight, 0);
        var left = x - Math.floor(width / 2);
        var top = y - MINIMENU_CLICK_BIAS;
        if (width && viewWidth && left + width > viewWidth)
            left = viewWidth - width;
        if (height && viewHeight && top + height > viewHeight)
            top = viewHeight - height;
        if (left < 0)
            left = 0;
        if (top < 0)
            top = 0;
        popup.box.style.left = "".concat(left, "px");
        popup.box.style.top = "".concat(top, "px");
    }
    function popupShow(rows, x, y) {
        popupHide();
        if (!rows.length)
            return;
        var back = document.createElement('div');
        back.className = 'tpc-minimenu-backdrop';
        var box = document.createElement('div');
        box.className = 'tpc-minimenu';
        var heading = document.createElement('div');
        heading.className = 'tpc-minimenu-title';
        setText(heading, 'Choose Option');
        box.appendChild(heading);
        var separator = document.createElement('div');
        separator.className = 'tpc-minimenu-separator';
        box.appendChild(separator);
        var list = document.createElement('div');
        list.className = 'tpc-minimenu-list';
        var _loop_3 = function (i) {
            var entry = rows[i];
            var option = document.createElement('button');
            option.type = 'button';
            option.className = 'tpc-minimenu-option';
            option.title = entry.label;
            setText(option, entry.label);
            bind(option, 'click', function () {
                popupHide();
                entry.run();
            });
            list.appendChild(option);
        };
        for (var i = 0; i < rows.length; i++) {
            _loop_3(i);
        }
        box.appendChild(list);
        back.appendChild(box);
        /* The backdrop is what dismisses the popup, so no document-wide mouse
         * handler has to exist for it -- a press on the popup itself is the row's
         * own, and every other press closes. */
        bind(back, 'mousedown', function (event) {
            event = event || global.event || {};
            if (insideNode(event.target || event.srcElement, box))
                return undefined;
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
    bind(document, 'contextmenu', function (event) {
        event = event || global.event || {};
        var target = event.target || event.srcElement || null;
        var repeat = popup.box && insideNode(target, popup.box);
        popupHide();
        if (!repeat)
            popupShow(popupRowsFor(target), integer(event.clientX, 0), integer(event.clientY, 0));
        return cancelEvent(event);
    });
    bind(content, 'focusin', function (event) {
        event = event || global.event || {};
        if (!editable(event.target || event.srcElement))
            return;
        state.focusToken++;
        postEditorFocus(true);
    });
    bind(content, 'focusout', function (event) {
        event = event || global.event || {};
        if (!editable(event.target || event.srcElement))
            return;
        var token = ++state.focusToken;
        var settle = function () {
            if (token !== state.focusToken)
                return;
            var active = document.activeElement;
            if (editable(active) && content.contains && content.contains(active))
                return;
            postEditorFocus(false);
        };
        if (typeof global.setTimeout === 'function')
            global.setTimeout(settle, 0);
        else
            settle();
    });
    /* A resize moves everything the popup was pointing at, and the host resizes
     * this view whenever a page opens or closes. */
    global.onresize = function () {
        popupHide();
        reportLayout();
    };
    sizeLegacyViewport();
    return {
        protocol: PROTOCOL,
        receive: receive,
        takeMessage: takeMessage,
        takeIntent: takeIntent,
        takeIntentOverflow: takeIntentOverflow,
        inspect: function () {
            return {
                railEntries: state.rail.entries.length,
                selectedEntry: state.rail.selectedEntry,
                expanded: state.rail.expanded,
                pageGeneration: state.pageGeneration,
                panel: state.panel,
                widgetCount: state.widgets.count,
                queuedMessages: state.messages.length,
                renderVisits: state.renderVisits
            };
        },
        constants: { CMD: CMD, W: W, INTENT: INTENT }
    };
});
